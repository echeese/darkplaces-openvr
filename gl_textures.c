
#include "quakedef.h"

cvar_t	r_max_size = {CVAR_SAVE, "r_max_size", "2048"};
cvar_t	r_max_scrapsize = {CVAR_SAVE, "r_max_scrapsize", "256"};
cvar_t	r_picmip = {CVAR_SAVE, "r_picmip", "0"};
cvar_t	r_lerpimages = {CVAR_SAVE, "r_lerpimages", "1"};
cvar_t	r_precachetextures = {CVAR_SAVE, "r_precachetextures", "1"};

int		gl_filter_min = GL_LINEAR_MIPMAP_LINEAR;
int		gl_filter_mag = GL_LINEAR;


static mempool_t *texturemempool;
static mempool_t *texturedatamempool;
static mempool_t *textureprocessingmempool;

// note: this must not conflict with TEXF_ flags in r_textures.h
// cleared when a texture is uploaded
#define GLTEXF_UPLOAD 0x00010000
// bitmask for mismatch checking
#define GLTEXF_IMPORTANTBITS (0)
// set when image is uploaded and freed
#define GLTEXF_DESTROYED 0x00040000

// size of images which hold fragment textures, ignores picmip and max_size
static int block_size;

// really this number only governs gltexnuminuse
#define MAX_GLTEXTURES 65536

// since there is only one set of GL texture numbers, we have to track them
// globally, everything else is per texture pool
static qbyte *gltexnuminuse;

typedef struct
{
	int textype;
	int inputbytesperpixel;
	int internalbytesperpixel;
	int glformat;
	int glinternalformat;
	int align;
}
textypeinfo_t;

static textypeinfo_t textype_qpalette       = {TEXTYPE_QPALETTE, 1, 4, GL_RGBA, 3, 1};
static textypeinfo_t textype_rgb            = {TEXTYPE_RGB     , 3, 3, GL_RGB , 3, 3};
static textypeinfo_t textype_rgba           = {TEXTYPE_RGBA    , 4, 4, GL_RGBA, 3, 1};
static textypeinfo_t textype_qpalette_alpha = {TEXTYPE_QPALETTE, 1, 4, GL_RGBA, 4, 1};
static textypeinfo_t textype_rgba_alpha     = {TEXTYPE_RGBA    , 4, 4, GL_RGBA, 4, 1};

// a tiling texture (most common type)
#define GLIMAGETYPE_TILE 0
// a fragments texture (contains one or more fragment textures)
#define GLIMAGETYPE_FRAGMENTS 1

// a gltextureimage can have one (or more if fragments) gltextures inside
typedef struct gltextureimage_s
{
	struct gltextureimage_s *imagechain;
	int texturecount;
	int type; // one of the GLIMAGETYPE_ values
	int texnum; // GL texture slot number
	int width, height;
	int bytesperpixel; // bytes per pixel
	int glformat; // GL_RGB or GL_RGBA
	int glinternalformat; // 3 or 4
	int flags;
	short *blockallocation; // fragment allocation
}
gltextureimage_t;

typedef struct gltexture_s
{
	// this field is exposed to the R_GetTexture macro, for speed reasons
	// (must be identical in rtexture_t)
	int texnum; // GL texture slot number

	// pointer to texturepool (check this to see if the texture is allocated)
	struct gltexturepool_s *pool;
	// pointer to next texture in texturepool chain
	struct gltexture_s *chain;
	// pointer into gltextureimage array
	gltextureimage_t *image;
	// name of the texture (this might be removed someday), no duplicates
	char *identifier;
	// location in the image, and size
	int x, y, width, height;
	// copy of the original texture supplied to the upload function, for re-uploading or deferred uploads (non-precached)
	qbyte *inputtexels;
	// to identify cache mismatchs (this might be removed someday)
	int crc;
	// flags supplied to the LoadTexture function
	// (might be altered to remove TEXF_ALPHA), and GLTEXF_ private flags
	int flags;
	// pointer to one of the textype_ structs
	textypeinfo_t *textype;
}
gltexture_t;

#define TEXTUREPOOL_SENTINEL 0xC0DEDBAD

typedef struct gltexturepool_s
{
	int sentinel;
	struct gltextureimage_s *imagechain;
	struct gltexture_s *gltchain;
	struct gltexturepool_s *next;
}
gltexturepool_t;

static gltexturepool_t *gltexturepoolchain = NULL;

static qbyte *resizebuffer = NULL, *colorconvertbuffer;
static int resizebuffersize = 0;
static qbyte *texturebuffer;
static int texturebuffersize = 0;

static int realmaxsize = 0;

static textypeinfo_t *R_GetTexTypeInfo(int textype, int flags)
{
	if (flags & TEXF_ALPHA)
	{
		switch(textype)
		{
		case TEXTYPE_QPALETTE:
			return &textype_qpalette_alpha;
		case TEXTYPE_RGB:
			Host_Error("R_GetTexTypeInfo: RGB format has no alpha, TEXF_ALPHA not allowed\n");
			return NULL;
		case TEXTYPE_RGBA:
			return &textype_rgba_alpha;
		default:
			Host_Error("R_GetTexTypeInfo: unknown texture format\n");
			return NULL;
		}
	}
	else
	{
		switch(textype)
		{
		case TEXTYPE_QPALETTE:
			return &textype_qpalette;
		case TEXTYPE_RGB:
			return &textype_rgb;
		case TEXTYPE_RGBA:
			return &textype_rgba;
		default:
			Host_Error("R_GetTexTypeInfo: unknown texture format\n");
			return NULL;
		}
	}
}

static void R_UploadTexture(gltexture_t *t);

static void R_PrecacheTexture(gltexture_t *glt)
{
	int precache;
	precache = false;
	if (glt->flags & TEXF_ALWAYSPRECACHE)
		precache = true;
	else if (r_precachetextures.integer >= 2)
		precache = true;
	else if (r_precachetextures.integer >= 1)
		if (glt->flags & TEXF_PRECACHE)
			precache = true;

	if (precache)
		R_UploadTexture(glt);
}

int R_RealGetTexture(rtexture_t *rt)
{
	if (rt)
	{
		gltexture_t *glt;
		glt = (gltexture_t *)rt;
		if (glt->flags & GLTEXF_UPLOAD)
			R_UploadTexture(glt);
		glt->texnum = glt->image->texnum;
		return glt->image->texnum;
	}
	else
		return 0;
}

void R_FreeTexture(rtexture_t *rt)
{
	gltexture_t *glt, **gltpointer;
	gltextureimage_t *image, **gltimagepointer;
	GLuint texnum;

	glt = (gltexture_t *)rt;
	if (glt == NULL)
		Host_Error("R_FreeTexture: texture == NULL\n");

	for (gltpointer = &glt->pool->gltchain;*gltpointer && *gltpointer != glt;gltpointer = &(*gltpointer)->chain);
	if (*gltpointer == glt)
		*gltpointer = glt->chain;
	else
		Host_Error("R_FreeTexture: texture \"%s\" not linked in pool\n", glt->identifier);

	// note: if freeing a fragment texture, this will not make the claimed
	// space available for new textures unless all other fragments in the
	// image are also freed
	image = glt->image;
	image->texturecount--;
	if (image->texturecount < 1)
	{
		for (gltimagepointer = &glt->pool->imagechain;*gltimagepointer && *gltimagepointer != image;gltimagepointer = &(*gltimagepointer)->imagechain);
		if (*gltimagepointer == image)
			*gltimagepointer = image->imagechain;
		else
			Host_Error("R_FreeTexture: image not linked in pool\n");
		if (image->texnum)
		{
			texnum = image->texnum;
			gltexnuminuse[image->texnum] = 0;
			qglDeleteTextures(1, &texnum);
		}
		if (image->blockallocation)
			Mem_Free(image->blockallocation);
		Mem_Free(image);
	}

	if (glt->identifier)
		Mem_Free(glt->identifier);
	if (glt->inputtexels)
		Mem_Free(glt->inputtexels);
	Mem_Free(glt);
}

static gltexture_t *R_FindTexture (gltexturepool_t *pool, char *identifier)
{
	gltexture_t	*glt;

	if (!identifier)
		return NULL;

	for (glt = pool->gltchain;glt;glt = glt->chain)
		if (glt->identifier && !strcmp (identifier, glt->identifier))
			return glt;

	return NULL;
}

rtexturepool_t *R_AllocTexturePool(void)
{
	gltexturepool_t *pool;
	if (texturemempool == NULL)
		return NULL;
	pool = Mem_Alloc(texturemempool, sizeof(gltexturepool_t));
	if (pool == NULL)
		return NULL;
	pool->next = gltexturepoolchain;
	gltexturepoolchain = pool;
	pool->sentinel = TEXTUREPOOL_SENTINEL;
	return (rtexturepool_t *)pool;
}

void R_FreeTexturePool(rtexturepool_t **rtexturepool)
{
	gltexturepool_t *pool, **poolpointer;
	if (rtexturepool == NULL)
		return;
	if (*rtexturepool == NULL)
		return;
	pool = (gltexturepool_t *)(*rtexturepool);
	*rtexturepool = NULL;
	if (pool->sentinel != TEXTUREPOOL_SENTINEL)
		Host_Error("R_FreeTexturePool: pool already freed\n");
	for (poolpointer = &gltexturepoolchain;*poolpointer && *poolpointer != pool;poolpointer = &(*poolpointer)->next);
	if (*poolpointer == pool)
		*poolpointer = pool->next;
	else
		Host_Error("R_FreeTexturePool: pool not linked\n");
	while (pool->gltchain)
		R_FreeTexture((rtexture_t *)pool->gltchain);
	if (pool->imagechain)
		Sys_Error("R_FreeTexturePool: not all images freed\n");
	Mem_Free(pool);
}


typedef struct
{
	char *name;
	int minification, magnification;
}
glmode_t;

static glmode_t modes[] =
{
	{"GL_NEAREST", GL_NEAREST, GL_NEAREST},
	{"GL_LINEAR", GL_LINEAR, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}
};

extern int gl_backend_rebindtextures;

static void GL_TextureMode_f (void)
{
	int i;
	gltextureimage_t *image;
	gltexturepool_t *pool;

	if (Cmd_Argc() == 1)
	{
		for (i = 0;i < 6;i++)
		{
			if (gl_filter_min == modes[i].minification)
			{
				Con_Printf ("%s\n", modes[i].name);
				return;
			}
		}
		Con_Printf ("current filter is unknown???\n");
		return;
	}

	for (i = 0;i < 6;i++)
		if (!Q_strcasecmp (modes[i].name, Cmd_Argv(1) ) )
			break;
	if (i == 6)
	{
		Con_Printf ("bad filter name\n");
		return;
	}

	gl_filter_min = modes[i].minification;
	gl_filter_mag = modes[i].magnification;

	// change all the existing mipmap texture objects
	// FIXME: force renderer(/client/something?) restart instead?
	for (pool = gltexturepoolchain;pool;pool = pool->next)
	{
		for (image = pool->imagechain;image;image = image->imagechain)
		{
			// only update already uploaded images
			if (!(image->flags & GLTEXF_UPLOAD))
			{
				qglBindTexture(GL_TEXTURE_2D, image->texnum);
				if (image->flags & TEXF_MIPMAP)
					qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
				else
					qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_mag);
				qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_mag);
			}
		}
	}
	gl_backend_rebindtextures = true;
}

static int R_CalcTexelDataSize (gltexture_t *glt)
{
	int width2, height2, size;
	if (glt->flags & TEXF_FRAGMENT)
		size = glt->width * glt->height;
	else
	{
		if (r_max_size.integer > realmaxsize)
			Cvar_SetValue("r_max_size", realmaxsize);
		// calculate final size
		for (width2 = 1;width2 < glt->width;width2 <<= 1);
		for (height2 = 1;height2 < glt->height;height2 <<= 1);
		for (width2 >>= r_picmip.integer;width2 > r_max_size.integer;width2 >>= 1);
		for (height2 >>= r_picmip.integer;height2 > r_max_size.integer;height2 >>= 1);
		if (width2 < 1) width2 = 1;
		if (height2 < 1) height2 = 1;

		size = 0;
		if (glt->flags & TEXF_MIPMAP)
		{
			while (width2 > 1 || height2 > 1)
			{
				size += width2 * height2;
				if (width2 > 1)
					width2 >>= 1;
				if (height2 > 1)
					height2 >>= 1;
			}
			size++; // count the last 1x1 mipmap
		}
		else
			size = width2*height2;
	}
	size *= glt->textype->internalbytesperpixel;

	return size;
}

void R_TextureStats_PrintTotal(void)
{
	int glsize, inputsize, total = 0, totalt = 0, totalp = 0, loaded = 0, loadedt = 0, loadedp = 0;
	gltexture_t *glt;
	gltexturepool_t *pool;
	for (pool = gltexturepoolchain;pool;pool = pool->next)
	{
		for (glt = pool->gltchain;glt;glt = glt->chain)
		{
			glsize = R_CalcTexelDataSize(glt);
			inputsize = glt->width * glt->height * glt->textype->inputbytesperpixel;

			total++;
			totalt += glsize;
			totalp += inputsize;
			if (!(glt->flags & GLTEXF_UPLOAD))
			{
				loaded++;
				loadedt += glsize;
				loadedp += inputsize;
			}
		}
	}
	Con_Printf("total: %i (%.3fMB, %.3fMB original), uploaded %i (%.3fMB, %.3fMB original), upload on demand %i (%.3fMB, %.3fMB original)\n", total, totalt / 1048576.0, totalp / 1048576.0, loaded, loadedt / 1048576.0, loadedp / 1048576.0, total - loaded, (totalt - loadedt) / 1048576.0, (totalp - loadedp) / 1048576.0);
}

static void R_TextureStats_f(void)
{
	int loaded;
	gltexture_t *glt;
	gltexturepool_t *pool;
	Con_Printf("glsize input crc  loaded mip alpha name\n");
	for (pool = gltexturepoolchain;pool;pool = pool->next)
	{
		for (glt = pool->gltchain;glt;glt = glt->chain)
		{
			loaded = !(glt->flags & GLTEXF_UPLOAD);
			Con_Printf("%c%4i%c%c%4i%c %04X %s %s %s %s\n", loaded ? '[' : ' ', (R_CalcTexelDataSize(glt) + 1023) / 1024, loaded ? ']' : ' ', glt->inputtexels ? '[' : ' ', (glt->width * glt->height * glt->textype->inputbytesperpixel + 1023) / 1024, glt->inputtexels ? ']' : ' ', glt->crc, loaded ? "loaded" : "      ", (glt->flags & TEXF_MIPMAP) ? "mip" : "   ", (glt->flags & TEXF_ALPHA) ? "alpha" : "     ", glt->identifier ? glt->identifier : "<unnamed>");
		}
		Con_Printf("pool %10p\n", pool);
	}
	R_TextureStats_PrintTotal();
}

char engineversion[40];

static void r_textures_start(void)
{
	// deal with size limits of various drivers (3dfx in particular)
	qglGetIntegerv(GL_MAX_TEXTURE_SIZE, &realmaxsize);
	CHECKGLERROR

	// use the largest scrap texture size we can (not sure if this is really a good idea)
	for (block_size = 1;block_size < realmaxsize && block_size < r_max_scrapsize.integer;block_size <<= 1);

	texturemempool = Mem_AllocPool("Texture Info");
	texturedatamempool = Mem_AllocPool("Texture Storage (not yet uploaded)");
	textureprocessingmempool = Mem_AllocPool("Texture Processing Buffers");
	gltexnuminuse = Mem_Alloc(texturemempool, MAX_GLTEXTURES);
}

static void r_textures_shutdown(void)
{
	rtexturepool_t *temp;
	while(gltexturepoolchain)
	{
		temp = (rtexturepool_t *) gltexturepoolchain;
		R_FreeTexturePool(&temp);
	}

	resizebuffersize = 0;
	texturebuffersize = 0;
	resizebuffer = NULL;
	colorconvertbuffer = NULL;
	texturebuffer = NULL;
	gltexnuminuse = NULL;
	Mem_FreePool(&texturemempool);
	Mem_FreePool(&texturedatamempool);
	Mem_FreePool(&textureprocessingmempool);
}

static void r_textures_newmap(void)
{
}

void R_Textures_Init (void)
{
	Cmd_AddCommand ("gl_texturemode", &GL_TextureMode_f);
	Cmd_AddCommand("r_texturestats", R_TextureStats_f);
	Cvar_RegisterVariable (&r_max_scrapsize);
	Cvar_RegisterVariable (&r_max_size);
	Cvar_RegisterVariable (&r_picmip);
	Cvar_RegisterVariable (&r_lerpimages);
	Cvar_RegisterVariable (&r_precachetextures);
	gltexnuminuse = NULL;

	R_RegisterModule("R_Textures", r_textures_start, r_textures_shutdown, r_textures_newmap);
}

void R_Textures_Frame (void)
{
	// could do procedural texture animation here, if we keep track of which
	// textures were accessed this frame...

	// free the resize buffers
	resizebuffersize = 0;
	if (resizebuffer)
	{
		Mem_Free(resizebuffer);
		resizebuffer = NULL;
	}
	if (colorconvertbuffer)
	{
		Mem_Free(colorconvertbuffer);
		colorconvertbuffer = NULL;
	}
}

void R_MakeResizeBufferBigger(int size)
{
	if (resizebuffersize < size)
	{
		resizebuffersize = size;
		if (resizebuffer)
			Mem_Free(resizebuffer);
		if (colorconvertbuffer)
			Mem_Free(colorconvertbuffer);
		resizebuffer = Mem_Alloc(textureprocessingmempool, resizebuffersize);
		colorconvertbuffer = Mem_Alloc(textureprocessingmempool, resizebuffersize);
		if (!resizebuffer || !colorconvertbuffer)
			Host_Error("R_Upload: out of memory\n");
	}
}

static void R_Upload(gltexture_t *glt, qbyte *data)
{
	int mip, width, height, internalformat;
	qbyte *prevbuffer;
	prevbuffer = data;

	qglBindTexture(GL_TEXTURE_2D, glt->image->texnum);
	CHECKGLERROR

	gl_backend_rebindtextures = true;

	glt->flags &= ~GLTEXF_UPLOAD;

	if (glt->flags & TEXF_FRAGMENT)
	{
		if (glt->image->flags & GLTEXF_UPLOAD)
		{
			Con_DPrintf("uploaded new fragments image\n");
			R_MakeResizeBufferBigger(glt->image->width * glt->image->height * glt->image->bytesperpixel);
			glt->image->flags &= ~GLTEXF_UPLOAD;
			memset(resizebuffer, 255, glt->image->width * glt->image->height * glt->image->bytesperpixel);
			qglTexImage2D (GL_TEXTURE_2D, 0, glt->image->glinternalformat, glt->image->width, glt->image->height, 0, glt->image->glformat, GL_UNSIGNED_BYTE, resizebuffer);
			CHECKGLERROR
			qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_mag);
			CHECKGLERROR
			qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_mag);
			CHECKGLERROR
		}

		if (prevbuffer == NULL)
		{
			R_MakeResizeBufferBigger(glt->image->width * glt->image->height * glt->image->bytesperpixel);
			memset(resizebuffer, 255, glt->width * glt->height * glt->image->bytesperpixel);
			prevbuffer = resizebuffer;
		}
		else if (glt->textype->textype == TEXTYPE_QPALETTE)
		{
			// promote paletted to RGBA, so we only have to worry about RGB and
			// RGBA in the rest of this code
			R_MakeResizeBufferBigger(glt->image->width * glt->image->height * glt->image->bytesperpixel);
			Image_Copy8bitRGBA(prevbuffer, colorconvertbuffer, glt->width * glt->height, d_8to24table);
			prevbuffer = colorconvertbuffer;
		}

		qglTexSubImage2D(GL_TEXTURE_2D, 0, glt->x, glt->y, glt->width, glt->height, glt->image->glformat, GL_UNSIGNED_BYTE, prevbuffer);
		CHECKGLERROR
		glt->texnum = glt->image->texnum;
		return;
	}

	glt->image->flags &= ~GLTEXF_UPLOAD;

	// these are rounded up versions of the size to do better resampling
	for (width = 1;width < glt->width;width <<= 1);
	for (height = 1;height < glt->height;height <<= 1);

	R_MakeResizeBufferBigger(width * height * glt->image->bytesperpixel);

	if (prevbuffer == NULL)
	{
		width = glt->image->width;
		height = glt->image->height;
		memset(resizebuffer, 255, width * height * glt->image->bytesperpixel);
		prevbuffer = resizebuffer;
	}
	else
	{
		if (glt->textype->textype == TEXTYPE_QPALETTE)
		{
			// promote paletted to RGBA, so we only have to worry about RGB and
			// RGBA in the rest of this code
			Image_Copy8bitRGBA(prevbuffer, colorconvertbuffer, glt->width * glt->height, d_8to24table);
			prevbuffer = colorconvertbuffer;
		}

		if (glt->width != width || glt->height != height)
		{
			Image_Resample(prevbuffer, glt->width, glt->height, resizebuffer, width, height, glt->image->bytesperpixel, r_lerpimages.integer);
			prevbuffer = resizebuffer;
		}

		// apply picmip/max_size limitations
		while (width > glt->image->width || height > glt->image->height)
		{
			Image_MipReduce(prevbuffer, resizebuffer, &width, &height, glt->image->width, glt->image->height, glt->image->bytesperpixel);
			prevbuffer = resizebuffer;
		}
	}

	// 3 and 4 are converted by the driver to it's preferred format for the current display mode
	internalformat = 3;
	if (glt->flags & TEXF_ALPHA)
		internalformat = 4;

	mip = 0;
	qglTexImage2D(GL_TEXTURE_2D, mip++, internalformat, width, height, 0, glt->image->glformat, GL_UNSIGNED_BYTE, prevbuffer);
	CHECKGLERROR
	if (glt->flags & TEXF_MIPMAP)
	{
		while (width > 1 || height > 1)
		{
			Image_MipReduce(prevbuffer, resizebuffer, &width, &height, 1, 1, glt->image->bytesperpixel);
			prevbuffer = resizebuffer;

			qglTexImage2D(GL_TEXTURE_2D, mip++, internalformat, width, height, 0, glt->image->glformat, GL_UNSIGNED_BYTE, prevbuffer);
			CHECKGLERROR
		}

		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
		CHECKGLERROR
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_mag);
		CHECKGLERROR
	}
	else
	{
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_mag);
		CHECKGLERROR
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_mag);
		CHECKGLERROR
	}
	glt->texnum = glt->image->texnum;
}

static void R_FindImageForTexture(gltexture_t *glt)
{
	int i, j, best, best2, x, y, w, h;
	textypeinfo_t *texinfo;
	gltexturepool_t *pool;
	gltextureimage_t *image, **imagechainpointer;
	texinfo = glt->textype;
	pool = glt->pool;

	// remains -1 until uploaded
	glt->texnum = -1;

	x = 0;
	y = 0;
	w = glt->width;
	h = glt->height;
	if (glt->flags & TEXF_FRAGMENT)
	{
		for (imagechainpointer = &pool->imagechain;*imagechainpointer;imagechainpointer = &(*imagechainpointer)->imagechain)
		{
			image = *imagechainpointer;
			if (image->type != GLIMAGETYPE_FRAGMENTS)
				continue;
			if (image->glformat != texinfo->glformat || image->glinternalformat != texinfo->glinternalformat)
				continue;
			if (glt->width > image->width || glt->height > image->height)
				continue;

			// got a fragments texture, find a place in it if we can
			for (best = image->width, i = 0;i < image->width - w;i += texinfo->align)
			{
				for (best2 = 0, j = 0;j < w;j++)
				{
					if (image->blockallocation[i+j] >= best)
						break;
					if (best2 < image->blockallocation[i+j])
						best2 = image->blockallocation[i+j];
				}
				if (j == w)
				{
					// this is a valid spot
					x = i;
					y = best = best2;
				}
			}

			if (best + h > image->height)
				continue;

			for (i = 0;i < w;i++)
				image->blockallocation[x + i] = best + h;

			glt->x = x;
			glt->y = y;
			glt->image = image;
			image->texturecount++;
			return;
		}

		image = Mem_Alloc(texturemempool, sizeof(gltextureimage_t));
		if (image == NULL)
			Sys_Error("R_FindImageForTexture: ran out of memory\n");
		image->type = GLIMAGETYPE_FRAGMENTS;
		// make sure the created image is big enough for the fragment
		for (image->width = block_size;image->width < glt->width;image->width <<= 1);
		for (image->height = block_size;image->height < glt->height;image->height <<= 1);
		image->blockallocation = Mem_Alloc(texturemempool, image->width * sizeof(short));
		memset(image->blockallocation, 0, image->width * sizeof(short));

		x = 0;
		y = 0;
		for (i = 0;i < w;i++)
			image->blockallocation[x + i] = y + h;
	}
	else
	{
		for (imagechainpointer = &pool->imagechain;*imagechainpointer;imagechainpointer = &(*imagechainpointer)->imagechain);

		image = Mem_Alloc(texturemempool, sizeof(gltextureimage_t));
		if (image == NULL)
			Sys_Error("R_FindImageForTexture: ran out of memory\n");
		image->type = GLIMAGETYPE_TILE;
		image->blockallocation = NULL;

		// calculate final size
		if (r_max_size.integer > realmaxsize)
			Cvar_SetValue("r_max_size", realmaxsize);
		for (image->width = 1;image->width < glt->width;image->width <<= 1);
		for (image->height = 1;image->height < glt->height;image->height <<= 1);
		for (image->width >>= r_picmip.integer;image->width > r_max_size.integer;image->width >>= 1);
		for (image->height >>= r_picmip.integer;image->height > r_max_size.integer;image->height >>= 1);
		if (image->width < 1) image->width = 1;
		if (image->height < 1) image->height = 1;
	}
	image->glinternalformat = texinfo->glinternalformat;
	image->glformat = texinfo->glformat;
	image->flags = (glt->flags & (TEXF_MIPMAP | TEXF_ALPHA)) | GLTEXF_UPLOAD;
	image->bytesperpixel = texinfo->internalbytesperpixel;
	for (i = 1;i < MAX_GLTEXTURES;i++)
		if (!gltexnuminuse[i])
			break;
	if (i < MAX_GLTEXTURES)
		gltexnuminuse[image->texnum = i] = true;
	else
		Sys_Error("R_FindImageForTexture: ran out of GL textures\n");
	*imagechainpointer = image;
	image->texturecount++;

	glt->x = x;
	glt->y = y;
	glt->image = image;
}

// note: R_FindImageForTexture must be called before this
static void R_UploadTexture (gltexture_t *glt)
{
	if (!(glt->flags & GLTEXF_UPLOAD))
		return;

	R_Upload(glt, glt->inputtexels);
	if (glt->inputtexels)
	{
		Mem_Free(glt->inputtexels);
		glt->inputtexels = NULL;
		glt->flags |= GLTEXF_DESTROYED;
	}
	else if (glt->flags & GLTEXF_DESTROYED)
		Con_Printf("R_UploadTexture: Texture %s already uploaded and destroyed.  Can not upload original image again.  Uploaded blank texture.\n", glt->identifier);
}

static gltexture_t *R_SetupTexture(gltexturepool_t *pool, char *identifier, int crc, int width, int height, int flags, textypeinfo_t *texinfo, qbyte *data)
{
	gltexture_t *glt;
	glt = Mem_Alloc(texturemempool, sizeof(gltexture_t));
	if (identifier)
	{
		glt->identifier = Mem_Alloc(texturemempool, strlen(identifier)+1);
		strcpy (glt->identifier, identifier);
	}
	else
		glt->identifier = NULL;
	glt->pool = pool;
	glt->chain = pool->gltchain;
	pool->gltchain = glt;
	glt->crc = crc;
	glt->width = width;
	glt->height = height;
	glt->flags = flags;
	glt->textype = texinfo;

	if (data)
	{
		glt->inputtexels = Mem_Alloc(texturedatamempool, glt->width * glt->height * texinfo->inputbytesperpixel);
		if (glt->inputtexels == NULL)
			Sys_Error("R_SetupTexture: out of memory\n");
		memcpy(glt->inputtexels, data, glt->width * glt->height * texinfo->inputbytesperpixel);
	}
	else
		glt->inputtexels = NULL;

	R_FindImageForTexture(glt);
	R_PrecacheTexture(glt);

	return glt;
}

/*
================
R_LoadTexture
================
*/
rtexture_t *R_LoadTexture (rtexturepool_t *rtexturepool, char *identifier, int width, int height, qbyte *data, int textype, int flags)
{
	int i;
	gltexture_t *glt;
	gltexturepool_t *pool = (gltexturepool_t *)rtexturepool;
	textypeinfo_t *texinfo;
	unsigned short crc;

	if (cls.state == ca_dedicated)
		return NULL;

	texinfo = R_GetTexTypeInfo(textype, flags);

	if (flags & TEXF_FRAGMENT)
		if ((width * texinfo->internalbytesperpixel) & 3)
			Host_Error("R_LoadTexture: incompatible width for fragment");

	// clear the alpha flag if the texture has no transparent pixels
	switch(textype)
	{
	case TEXTYPE_QPALETTE:
		if (flags & TEXF_ALPHA)
		{
			flags &= ~TEXF_ALPHA;
			for (i = 0;i < width * height;i++)
			{
				if (data[i] == 255)
				{
					flags |= TEXF_ALPHA;
					break;
				}
			}
		}
		break;
	case TEXTYPE_RGB:
		if (flags & TEXF_ALPHA)
			Host_Error("R_LoadTexture: RGB has no alpha, don't specify TEXF_ALPHA\n");
		break;
	case TEXTYPE_RGBA:
		if (flags & TEXF_ALPHA)
		{
			flags &= ~TEXF_ALPHA;
			for (i = 0;i < width * height;i++)
			{
				if (data[i * 4 + 3] < 255)
				{
					flags |= TEXF_ALPHA;
					break;
				}
			}
		}
		break;
	default:
		Host_Error("R_LoadTexture: unknown texture type\n");
	}

	// LordHavoc: do a CRC to confirm the data really is the same as previous occurances.
	if (data == NULL)
		crc = 0;
	else
		crc = CRC_Block(data, width*height*texinfo->inputbytesperpixel);

	// see if the texture is already present
	if (identifier && (glt = R_FindTexture(pool, identifier)))
	{
		if (crc == glt->crc && width == glt->width && height == glt->height && texinfo == glt->textype && ((flags ^ glt->flags) & TEXF_IMPORTANTBITS) == 0 && ((flags ^ glt->flags) & GLTEXF_IMPORTANTBITS) == 0)
		{
			Con_Printf("R_LoadTexture: exact match with existing texture %s\n", identifier);
			return (rtexture_t *)glt; // exact match, use existing
		}
		Con_Printf("R_LoadTexture: cache mismatch on %s, replacing old texture\n", identifier);
		R_FreeTexture((rtexture_t *)glt);
	}

	return (rtexture_t *)R_SetupTexture(pool, identifier, crc, width, height, flags | GLTEXF_UPLOAD, texinfo, data);
}

int R_TextureHasAlpha(rtexture_t *rt)
{
	gltexture_t *glt;
	if (!rt)
		return false;
	glt = (gltexture_t *)rt;
	return (glt->flags & TEXF_ALPHA) != 0;
}

int R_TextureWidth(rtexture_t *rt)
{
	if (!rt)
		return false;
	return ((gltexture_t *)rt)->width;
}

int R_TextureHeight(rtexture_t *rt)
{
	if (!rt)
		return false;
	return ((gltexture_t *)rt)->height;
}

void R_FragmentLocation(rtexture_t *rt, int *x, int *y, float *fx1, float *fy1, float *fx2, float *fy2)
{
	gltexture_t *glt;
	float iwidth, iheight;
	if (cls.state == ca_dedicated)
	{
		if (x)
			*x = 0;
		if (y)
			*y = 0;
		if (fx1 || fy1 || fx2 || fy2)
		{
			if (fx1)
				*fx1 = 0;
			if (fy1)
				*fy1 = 0;
			if (fx2)
				*fx2 = 1;
			if (fy2)
				*fy2 = 1;
		}
		return;
	}
	if (!rt)
		Host_Error("R_FragmentLocation: no texture supplied\n");
	glt = (gltexture_t *)rt;
	if (glt->flags & TEXF_FRAGMENT)
	{
		if (x)
			*x = glt->x;
		if (y)
			*y = glt->y;
		if (fx1 || fy1 || fx2 || fy2)
		{
			iwidth = 1.0f / glt->image->width;
			iheight = 1.0f / glt->image->height;
			if (fx1)
				*fx1 = glt->x * iwidth;
			if (fy1)
				*fy1 = glt->y * iheight;
			if (fx2)
				*fx2 = (glt->x + glt->width) * iwidth;
			if (fy2)
				*fy2 = (glt->y + glt->height) * iheight;
		}
	}
	else
	{
		if (x)
			*x = 0;
		if (y)
			*y = 0;
		if (fx1 || fy1 || fx2 || fy2)
		{
			if (fx1)
				*fx1 = 0;
			if (fy1)
				*fy1 = 0;
			if (fx2)
				*fx2 = 1;
			if (fy2)
				*fy2 = 1;
		}
	}
}

int R_CompatibleFragmentWidth(int width, int textype, int flags)
{
	textypeinfo_t *texinfo = R_GetTexTypeInfo(textype, flags);
	while ((width * texinfo->internalbytesperpixel) & 3)
		width++;
	return width;
}

void R_UpdateTexture(rtexture_t *rt, qbyte *data)
{
	gltexture_t *glt;
	if (rt == NULL)
		Host_Error("R_UpdateTexture: no texture supplied\n");
	if (data == NULL)
		Host_Error("R_UpdateTexture: no data supplied\n");
	glt = (gltexture_t *)rt;

	// if it has not been uploaded yet, update the data that will be used when it is
	if (glt->inputtexels)
		memcpy(glt->inputtexels, data, glt->width * glt->height * glt->textype->inputbytesperpixel);
	else
		R_Upload(glt, data);
}

