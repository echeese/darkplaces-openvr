/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"
#include "image.h"
#include "wad.h"

#include "cl_video.h"
#include "cl_dyntexture.h"

dp_font_t dp_fonts[MAX_FONTS] = {{0}};

cvar_t r_textshadow = {CVAR_SAVE, "r_textshadow", "0", "draws a shadow on all text to improve readability (note: value controls offset, 1 = 1 pixel, 1.5 = 1.5 pixels, etc)"};
cvar_t r_textbrightness = {CVAR_SAVE, "r_textbrightness", "0", "additional brightness for text color codes (0 keeps colors as is, 1 makes them all white)"};

cachepic_t *r_crosshairs[NUMCROSSHAIRS+1];

//=============================================================================
/* Support Routines */

#define FONT_FILESIZE 13468
#define MAX_CACHED_PICS 1024
#define CACHEPICHASHSIZE 256
static cachepic_t *cachepichash[CACHEPICHASHSIZE];
static cachepic_t cachepics[MAX_CACHED_PICS];
static int numcachepics;

static rtexturepool_t *drawtexturepool;

static unsigned char concharimage[FONT_FILESIZE] =
{
#include "lhfont.h"
};

static rtexture_t *draw_generateconchars(void)
{
	int i;
	unsigned char buffer[65536][4], *data = NULL;
	double random;

	data = LoadTGA_BGRA (concharimage, FONT_FILESIZE);
// Gold numbers
	for (i = 0;i < 8192;i++)
	{
		random = lhrandom (0.0,1.0);
		buffer[i][2] = 83 + (unsigned char)(random * 64);
		buffer[i][1] = 71 + (unsigned char)(random * 32);
		buffer[i][0] = 23 + (unsigned char)(random * 16);
		buffer[i][3] = data[i*4+0];
	}
// White chars
	for (i = 8192;i < 32768;i++)
	{
		random = lhrandom (0.0,1.0);
		buffer[i][2] = 95 + (unsigned char)(random * 64);
		buffer[i][1] = 95 + (unsigned char)(random * 64);
		buffer[i][0] = 95 + (unsigned char)(random * 64);
		buffer[i][3] = data[i*4+0];
	}
// Gold numbers
	for (i = 32768;i < 40960;i++)
	{
		random = lhrandom (0.0,1.0);
		buffer[i][2] = 83 + (unsigned char)(random * 64);
		buffer[i][1] = 71 + (unsigned char)(random * 32);
		buffer[i][0] = 23 + (unsigned char)(random * 16);
		buffer[i][3] = data[i*4+0];
	}
// Red chars
	for (i = 40960;i < 65536;i++)
	{
		random = lhrandom (0.0,1.0);
		buffer[i][2] = 96 + (unsigned char)(random * 64);
		buffer[i][1] = 43 + (unsigned char)(random * 32);
		buffer[i][0] = 27 + (unsigned char)(random * 32);
		buffer[i][3] = data[i*4+0];
	}

#if 0
	Image_WriteTGABGRA ("gfx/generated_conchars.tga", 256, 256, &buffer[0][0]);
#endif

	Mem_Free(data);
	return R_LoadTexture2D(drawtexturepool, "conchars", 256, 256, &buffer[0][0], TEXTYPE_BGRA, TEXF_ALPHA | TEXF_PRECACHE, NULL);
}

static char *pointerimage =
	"333333332......."
	"26777761........"
	"2655541........."
	"265541.........."
	"2654561........."
	"26414561........"
	"251.14561......."
	"21...14561......"
	"1.....141......."
	".......1........"
	"................"
	"................"
	"................"
	"................"
	"................"
	"................"
;

static rtexture_t *draw_generatemousepointer(void)
{
	int i;
	unsigned char buffer[256][4];
	for (i = 0;i < 256;i++)
	{
		if (pointerimage[i] == '.')
		{
			buffer[i][0] = 0;
			buffer[i][1] = 0;
			buffer[i][2] = 0;
			buffer[i][3] = 0;
		}
		else
		{
			buffer[i][0] = (pointerimage[i] - '0') * 16;
			buffer[i][1] = (pointerimage[i] - '0') * 16;
			buffer[i][2] = (pointerimage[i] - '0') * 16;
			buffer[i][3] = 255;
		}
	}
	return R_LoadTexture2D(drawtexturepool, "mousepointer", 16, 16, &buffer[0][0], TEXTYPE_BGRA, TEXF_ALPHA | TEXF_PRECACHE, NULL);
}

static char *crosshairtexdata[NUMCROSSHAIRS] =
{
	"................"
	"................"
	"................"
	"...33......33..."
	"...355....553..."
	"....577..775...."
	".....77..77....."
	"................"
	"................"
	".....77..77....."
	"....577..775...."
	"...355....553..."
	"...33......33..."
	"................"
	"................"
	"................"
	,
	"................"
	"................"
	"................"
	"...3........3..."
	"....5......5...."
	".....7....7....."
	"......7..7......"
	"................"
	"................"
	"......7..7......"
	".....7....7....."
	"....5......5...."
	"...3........3..."
	"................"
	"................"
	"................"
	,
	"................"
	".......77......."
	".......77......."
	"................"
	"................"
	".......44......."
	".......44......."
	".77..44..44..77."
	".77..44..44..77."
	".......44......."
	".......44......."
	"................"
	"................"
	".......77......."
	".......77......."
	"................"
	,
	"................"
	"................"
	"................"
	"................"
	"................"
	"................"
	"................"
	"................"
	"........7777777."
	"........752....."
	"........72......"
	"........7......."
	"........7......."
	"........7......."
	"........7......."
	"................"
	,
	"................"
	"................"
	"................"
	"................"
	"................"
	"........7......."
	"................"
	"........4......."
	".....7.4.4.7...."
	"........4......."
	"................"
	"........7......."
	"................"
	"................"
	"................"
	"................"
	,
	"................"
	"................"
	"................"
	"................"
	"................"
	"................"
	"................"
	".......55......."
	".......55......."
	"................"
	"................"
	"................"
	"................"
	"................"
	"................"
	"................"
};

static rtexture_t *draw_generatecrosshair(int num)
{
	int i;
	char *in;
	unsigned char data[16*16][4];
	in = crosshairtexdata[num];
	for (i = 0;i < 16*16;i++)
	{
		if (in[i] == '.')
		{
			data[i][0] = 255;
			data[i][1] = 255;
			data[i][2] = 255;
			data[i][3] = 0;
		}
		else
		{
			data[i][0] = data[i][1] = data[i][2] = (unsigned char) ((int) (in[i] - '0') * 127 / 7 + 128);
			data[i][3] = 255;
		}
	}
	return R_LoadTexture2D(drawtexturepool, va("crosshair%i", num+1), 16, 16, &data[0][0], TEXTYPE_BGRA, TEXF_ALPHA | TEXF_PRECACHE, NULL);
}

static rtexture_t *draw_generateditherpattern(void)
{
#if 1
	int x, y;
	unsigned char data[8*8*4];
	for (y = 0;y < 8;y++)
	{
		for (x = 0;x < 8;x++)
		{
			data[(y*8+x)*4+0] = data[(y*8+x)*4+1] = data[(y*8+x)*4+2] = ((x^y) & 4) ? 255 : 0;
			data[(y*8+x)*4+3] = 255;
		}
	}
	return R_LoadTexture2D(drawtexturepool, "ditherpattern", 8, 8, data, TEXTYPE_BGRA, TEXF_FORCENEAREST | TEXF_PRECACHE, NULL);
#else
	unsigned char data[16];
	memset(data, 255, sizeof(data));
	data[0] = data[1] = data[2] = data[12] = data[13] = data[14] = 0;
	return R_LoadTexture2D(drawtexturepool, "ditherpattern", 2, 2, data, TEXTYPE_BGRA, TEXF_FORCENEAREST | TEXF_PRECACHE, NULL);
#endif
}

/*
================
Draw_CachePic
================
*/
// FIXME: move this to client somehow
static cachepic_t	*Draw_CachePic_Compression (const char *path, qboolean persistent, qboolean allow_compression)
{
	int crc, hashkey;
	cachepic_t *pic;
	int flags;
	fs_offset_t lmpsize;
	unsigned char *lmpdata;
	char lmpname[MAX_QPATH];

	// check whether the picture has already been cached
	crc = CRC_Block((unsigned char *)path, strlen(path));
	hashkey = ((crc >> 8) ^ crc) % CACHEPICHASHSIZE;
	for (pic = cachepichash[hashkey];pic;pic = pic->chain)
		if (!strcmp (path, pic->name))
			return pic;

	if (numcachepics == MAX_CACHED_PICS)
	{
		Con_Printf ("Draw_CachePic: numcachepics == MAX_CACHED_PICS\n");
		// FIXME: support NULL in callers?
		return cachepics; // return the first one
	}
	pic = cachepics + (numcachepics++);
	strlcpy (pic->name, path, sizeof(pic->name));
	// link into list
	pic->chain = cachepichash[hashkey];
	cachepichash[hashkey] = pic;

	// check whether it is an dynamic texture (if so, we can directly use its texture handler)
	pic->tex = CL_GetDynTexture( path );
	// if so, set the width/height, too
	if( pic->tex ) {
		pic->width = R_TextureWidth(pic->tex);
		pic->height = R_TextureHeight(pic->tex);
		// we're done now (early-out)
		return pic;
	}

	flags = TEXF_ALPHA;
	if (persistent)
		flags |= TEXF_PRECACHE;
	if (!strcmp(path, "gfx/colorcontrol/ditherpattern"))
		flags |= TEXF_CLAMP;
	if(allow_compression && gl_texturecompression_2d.integer)
		flags |= TEXF_COMPRESS;

	// load a high quality image from disk if possible
	pic->tex = loadtextureimage(drawtexturepool, path, false, flags, true);
	if (pic->tex == NULL && !strncmp(path, "gfx/", 4))
	{
		// compatibility with older versions which did not require gfx/ prefix
		pic->tex = loadtextureimage(drawtexturepool, path + 4, false, flags, true);
	}
	// if a high quality image was loaded, set the pic's size to match it, just
	// in case there's no low quality version to get the size from
	if (pic->tex)
	{
		pic->width = R_TextureWidth(pic->tex);
		pic->height = R_TextureHeight(pic->tex);
	}

	// now read the low quality version (wad or lmp file), and take the pic
	// size from that even if we don't upload the texture, this way the pics
	// show up the right size in the menu even if they were replaced with
	// higher or lower resolution versions
	dpsnprintf(lmpname, sizeof(lmpname), "%s.lmp", path);
	if (!strncmp(path, "gfx/", 4) && (lmpdata = FS_LoadFile(lmpname, tempmempool, false, &lmpsize)))
	{
		if (lmpsize >= 9)
		{
			pic->width = lmpdata[0] + lmpdata[1] * 256 + lmpdata[2] * 65536 + lmpdata[3] * 16777216;
			pic->height = lmpdata[4] + lmpdata[5] * 256 + lmpdata[6] * 65536 + lmpdata[7] * 16777216;
			// if no high quality replacement image was found, upload the original low quality texture
			if (!pic->tex)
				pic->tex = R_LoadTexture2D(drawtexturepool, path, pic->width, pic->height, lmpdata + 8, TEXTYPE_PALETTE, flags & ~TEXF_COMPRESS, palette_bgra_transparent);
		}
		Mem_Free(lmpdata);
	}
	else if ((lmpdata = W_GetLumpName (path + 4)))
	{
		if (!strcmp(path, "gfx/conchars"))
		{
			// conchars is a raw image and with color 0 as transparent instead of 255
			pic->width = 128;
			pic->height = 128;
			// if no high quality replacement image was found, upload the original low quality texture
			if (!pic->tex)
				pic->tex = R_LoadTexture2D(drawtexturepool, path, 128, 128, lmpdata, TEXTYPE_PALETTE, flags & ~TEXF_COMPRESS, palette_bgra_font);
		}
		else
		{
			pic->width = lmpdata[0] + lmpdata[1] * 256 + lmpdata[2] * 65536 + lmpdata[3] * 16777216;
			pic->height = lmpdata[4] + lmpdata[5] * 256 + lmpdata[6] * 65536 + lmpdata[7] * 16777216;
			// if no high quality replacement image was found, upload the original low quality texture
			if (!pic->tex)
				pic->tex = R_LoadTexture2D(drawtexturepool, path, pic->width, pic->height, lmpdata + 8, TEXTYPE_PALETTE, flags & ~TEXF_COMPRESS, palette_bgra_transparent);
		}
	}

	// if it's not found on disk, check if it's one of the builtin images
	if (pic->tex == NULL)
	{
		if (pic->tex == NULL && !strcmp(path, "gfx/conchars"))
			pic->tex = draw_generateconchars();
		if (pic->tex == NULL && !strcmp(path, "ui/mousepointer"))
			pic->tex = draw_generatemousepointer();
		if (pic->tex == NULL && !strcmp(path, "gfx/prydoncursor001"))
			pic->tex = draw_generatemousepointer();
		if (pic->tex == NULL && !strcmp(path, "gfx/crosshair1"))
			pic->tex = draw_generatecrosshair(0);
		if (pic->tex == NULL && !strcmp(path, "gfx/crosshair2"))
			pic->tex = draw_generatecrosshair(1);
		if (pic->tex == NULL && !strcmp(path, "gfx/crosshair3"))
			pic->tex = draw_generatecrosshair(2);
		if (pic->tex == NULL && !strcmp(path, "gfx/crosshair4"))
			pic->tex = draw_generatecrosshair(3);
		if (pic->tex == NULL && !strcmp(path, "gfx/crosshair5"))
			pic->tex = draw_generatecrosshair(4);
		if (pic->tex == NULL && !strcmp(path, "gfx/crosshair6"))
			pic->tex = draw_generatecrosshair(5);
		if (pic->tex == NULL && !strcmp(path, "gfx/colorcontrol/ditherpattern"))
			pic->tex = draw_generateditherpattern();
		// default textures for light sprites
		// todo: improve them
		if (pic->tex == NULL && !strcmp(path, "gfx/editlights/cursor"))
			pic->tex = draw_generatecrosshair(0);
		if (pic->tex == NULL && !strcmp(path, "gfx/editlights/light"))
			pic->tex = draw_generatecrosshair(0);
		if (pic->tex == NULL && !strcmp(path, "gfx/editlights/noshadow"))
			pic->tex = draw_generatecrosshair(0);
		if (pic->tex == NULL && !strcmp(path, "gfx/editlights/cubemap"))
			pic->tex = draw_generatecrosshair(0);
		if (pic->tex == NULL && !strcmp(path, "gfx/editlights/selection"))
			pic->tex = draw_generatecrosshair(0);
		if (pic->tex == NULL)
		{
			// don't complain about missing gfx/crosshair images
			if (strncmp(path, "gfx/crosshair", 13))
				Con_Printf("Draw_CachePic: failed to load %s\n", path);
			pic->tex = r_texture_notexture;
		}
		pic->width = R_TextureWidth(pic->tex);
		pic->height = R_TextureHeight(pic->tex);
	}

	return pic;
}
cachepic_t	*Draw_CachePic (const char *path, qboolean persistent)
{
	return Draw_CachePic_Compression(path, persistent, true);
}

cachepic_t *Draw_NewPic(const char *picname, int width, int height, int alpha, unsigned char *pixels_bgra)
{
	int crc, hashkey;
	cachepic_t *pic;

	crc = CRC_Block((unsigned char *)picname, strlen(picname));
	hashkey = ((crc >> 8) ^ crc) % CACHEPICHASHSIZE;
	for (pic = cachepichash[hashkey];pic;pic = pic->chain)
		if (!strcmp (picname, pic->name))
			break;

	if (pic)
	{
		if (pic->tex && pic->width == width && pic->height == height)
		{
			R_UpdateTexture(pic->tex, pixels_bgra, 0, 0, width, height);
			return pic;
		}
	}
	else
	{
		if (pic == NULL)
		{
			if (numcachepics == MAX_CACHED_PICS)
			{
				Con_Printf ("Draw_NewPic: numcachepics == MAX_CACHED_PICS\n");
				// FIXME: support NULL in callers?
				return cachepics; // return the first one
			}
			pic = cachepics + (numcachepics++);
			strlcpy (pic->name, picname, sizeof(pic->name));
			// link into list
			pic->chain = cachepichash[hashkey];
			cachepichash[hashkey] = pic;
		}
	}

	pic->width = width;
	pic->height = height;
	if (pic->tex)
		R_FreeTexture(pic->tex);
	pic->tex = R_LoadTexture2D(drawtexturepool, picname, width, height, pixels_bgra, TEXTYPE_BGRA, alpha ? TEXF_ALPHA : 0, NULL);
	return pic;
}

void Draw_FreePic(const char *picname)
{
	int crc;
	int hashkey;
	cachepic_t *pic;
	// this doesn't really free the pic, but does free it's texture
	crc = CRC_Block((unsigned char *)picname, strlen(picname));
	hashkey = ((crc >> 8) ^ crc) % CACHEPICHASHSIZE;
	for (pic = cachepichash[hashkey];pic;pic = pic->chain)
	{
		if (!strcmp (picname, pic->name) && pic->tex)
		{
			R_FreeTexture(pic->tex);
			pic->width = 0;
			pic->height = 0;
			return;
		}
	}
}

extern int con_linewidth; // to force rewrapping
static void LoadFont(qboolean override, const char *name, dp_font_t *fnt)
{
	int i;
	float maxwidth;
	char widthfile[MAX_QPATH];
	char *widthbuf;
	fs_offset_t widthbufsize;

	if(override || !fnt->texpath[0])
		strlcpy(fnt->texpath, name, sizeof(fnt->texpath));

	if(drawtexturepool == NULL)
		return; // before gl_draw_start, so will be loaded later

	fnt->tex = Draw_CachePic_Compression(fnt->texpath, true, false)->tex;
	if(fnt->tex == r_texture_notexture)
	{
		fnt->tex = Draw_CachePic_Compression("gfx/conchars", true, false)->tex;
		strlcpy(widthfile, "gfx/conchars.width", sizeof(widthfile));
	}
	else
		dpsnprintf(widthfile, sizeof(widthfile), "%s.width", fnt->texpath);

	// unspecified width == 1 (base width)
	for(i = 1; i < 256; ++i)
		fnt->width_of[i] = 1;

	// FIXME load "name.width", if it fails, fill all with 1
	if((widthbuf = (char *) FS_LoadFile(widthfile, tempmempool, true, &widthbufsize)))
	{
		float extraspacing = 0;
		const char *p = widthbuf;
		int ch = 0;

		while(ch < 256)
		{
			if(!COM_ParseToken_Simple(&p, false, false))
				return;

			if(!strcmp(com_token, "extraspacing"))
			{
				if(!COM_ParseToken_Simple(&p, false, false))
					return;
				extraspacing = atof(com_token);
			}
			else
				fnt->width_of[ch++] = atof(com_token) + extraspacing;
		}

		Mem_Free(widthbuf);
	}

	maxwidth = fnt->width_of[1];
	for(i = 2; i < 256; ++i)
		maxwidth = max(maxwidth, fnt->width_of[i]);
	fnt->width_of[0] = maxwidth;

	if(fnt == FONT_CONSOLE)
		con_linewidth = -1; // rewrap console in next frame
}

static dp_font_t *FindFont(const char *title)
{
	int i;
	for(i = 0; i < MAX_FONTS; ++i)
		if(!strcmp(dp_fonts[i].title, title))
			return &dp_fonts[i];
	return NULL;
}

static void LoadFont_f()
{
	dp_font_t *f;
	int i;
	if(Cmd_Argc() < 2)
	{
		Con_Printf("Available font commands:\n");
		for(i = 0; i < MAX_FONTS; ++i)
			Con_Printf("  loadfont %s gfx/tgafile\n", dp_fonts[i].title);
		return;
	}
	f = FindFont(Cmd_Argv(1));
	if(f == NULL)
	{
		Con_Printf("font function not found\n");
		return;
	}
	LoadFont(true, (Cmd_Argc() < 3) ? "gfx/conchars" : Cmd_Argv(2), f);
}

/*
===============
Draw_Init
===============
*/
static void gl_draw_start(void)
{
	int i;
	drawtexturepool = R_AllocTexturePool();

	numcachepics = 0;
	memset(cachepichash, 0, sizeof(cachepichash));

	for(i = 0; i < MAX_FONTS; ++i)
		LoadFont(false, va("gfx/font_%s", dp_fonts[i].title), &dp_fonts[i]);

	for (i = 1;i <= NUMCROSSHAIRS;i++)
		r_crosshairs[i] = Draw_CachePic(va("gfx/crosshair%i", i), true);

	// draw the loading screen so people have something to see in the newly opened window
	SCR_UpdateLoadingScreen(true);
}

static void gl_draw_shutdown(void)
{
	R_FreeTexturePool(&drawtexturepool);

	numcachepics = 0;
	memset(cachepichash, 0, sizeof(cachepichash));
}

static void gl_draw_newmap(void)
{
}

void GL_Draw_Init (void)
{
	int i, j;
	Cvar_RegisterVariable(&r_textshadow);
	Cvar_RegisterVariable(&r_textbrightness);
	Cmd_AddCommand ("loadfont",LoadFont_f, "loadfont function tganame loads a font; example: loadfont console gfx/veramono; loadfont without arguments lists the available functions");
	R_RegisterModule("GL_Draw", gl_draw_start, gl_draw_shutdown, gl_draw_newmap);

	strlcpy(FONT_DEFAULT->title, "default", sizeof(FONT_DEFAULT->title));
		strlcpy(FONT_DEFAULT->texpath, "gfx/conchars", sizeof(FONT_DEFAULT->texpath));
	strlcpy(FONT_CONSOLE->title, "console", sizeof(FONT_CONSOLE->title));
	strlcpy(FONT_SBAR->title, "sbar", sizeof(FONT_SBAR->title));
	strlcpy(FONT_NOTIFY->title, "notify", sizeof(FONT_NOTIFY->title));
	strlcpy(FONT_CHAT->title, "chat", sizeof(FONT_CHAT->title));
	strlcpy(FONT_CENTERPRINT->title, "centerprint", sizeof(FONT_CENTERPRINT->title));
	strlcpy(FONT_INFOBAR->title, "infobar", sizeof(FONT_INFOBAR->title));
	strlcpy(FONT_MENU->title, "menu", sizeof(FONT_MENU->title));
	for(i = 0, j = 0; i < MAX_FONTS; ++i)
		if(!FONT_USER[i].title[0])
			dpsnprintf(FONT_USER[i].title, sizeof(FONT_USER[i].title), "user%d", j++);
}

static void _DrawQ_Setup(void)
{
	if (r_refdef.draw2dstage)
		return;
	r_refdef.draw2dstage = true;
	CHECKGLERROR
	qglViewport(r_view.x, vid.height - (r_view.y + r_view.height), r_view.width, r_view.height);CHECKGLERROR
	GL_ColorMask(r_view.colormask[0], r_view.colormask[1], r_view.colormask[2], 1);
	GL_SetupView_Mode_Ortho(0, 0, vid_conwidth.integer, vid_conheight.integer, -10, 100);
	qglDepthFunc(GL_LEQUAL);CHECKGLERROR
	qglDisable(GL_POLYGON_OFFSET_FILL);CHECKGLERROR
	GL_CullFace(GL_FRONT); // quake is backwards, this culls back faces
	R_Mesh_Matrix(&identitymatrix);

	GL_DepthMask(true);
	GL_DepthRange(0, 1);
	GL_PolygonOffset(0, 0);
	GL_DepthTest(false);
	GL_Color(1,1,1,1);
	GL_AlphaTest(false);
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if (gl_support_fragment_shader)
	{
		qglUseProgramObjectARB(0);CHECKGLERROR
	}
}

static void _DrawQ_ProcessDrawFlag(int flags)
{
	_DrawQ_Setup();
	CHECKGLERROR
	if(flags == DRAWFLAG_ADDITIVE)
		GL_BlendFunc(GL_SRC_ALPHA, GL_ONE);
	else if(flags == DRAWFLAG_MODULATE)
		GL_BlendFunc(GL_DST_COLOR, GL_ZERO);
	else if(flags == DRAWFLAG_2XMODULATE)
		GL_BlendFunc(GL_DST_COLOR,GL_SRC_COLOR);
	else
		GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void DrawQ_Pic(float x, float y, cachepic_t *pic, float width, float height, float red, float green, float blue, float alpha, int flags)
{
	float floats[20];

	_DrawQ_ProcessDrawFlag(flags);
	GL_Color(red, green, blue, alpha);

	R_Mesh_VertexPointer(floats, 0, 0);
	R_Mesh_ColorPointer(NULL, 0, 0);
	R_Mesh_ResetTextureState();
	if (pic)
	{
		if (width == 0)
			width = pic->width;
		if (height == 0)
			height = pic->height;
		R_Mesh_TexBind(0, R_GetTexture(pic->tex));
		R_Mesh_TexCoordPointer(0, 2, floats + 12, 0, 0);

      // AK07: lets be texel correct on the corners
      {
         float horz_offset = 0.5f / pic->width;
         float vert_offset = 0.5f / pic->height;

		   floats[12] = 0.0f + horz_offset;floats[13] = 0.0f + vert_offset;
		   floats[14] = 1.0f - horz_offset;floats[15] = 0.0f + vert_offset;
		   floats[16] = 1.0f - horz_offset;floats[17] = 1.0f - vert_offset;
		   floats[18] = 0.0f + horz_offset;floats[19] = 1.0f - vert_offset;
      }
	}

	floats[2] = floats[5] = floats[8] = floats[11] = 0;
	floats[0] = floats[9] = x;
	floats[1] = floats[4] = y;
	floats[3] = floats[6] = x + width;
	floats[7] = floats[10] = y + height;

	R_Mesh_Draw(0, 4, 2, polygonelements, 0, 0);
}

void DrawQ_Fill(float x, float y, float width, float height, float red, float green, float blue, float alpha, int flags)
{
	float floats[12];

	_DrawQ_ProcessDrawFlag(flags);
	GL_Color(red, green, blue, alpha);

	R_Mesh_VertexPointer(floats, 0, 0);
	R_Mesh_ColorPointer(NULL, 0, 0);
	R_Mesh_ResetTextureState();

	floats[2] = floats[5] = floats[8] = floats[11] = 0;
	floats[0] = floats[9] = x;
	floats[1] = floats[4] = y;
	floats[3] = floats[6] = x + width;
	floats[7] = floats[10] = y + height;

	R_Mesh_Draw(0, 4, 2, polygonelements, 0, 0);
}

// color tag printing
static vec4_t string_colors[] =
{
	// Quake3 colors
	// LordHavoc: why on earth is cyan before magenta in Quake3?
	// LordHavoc: note: Doom3 uses white for [0] and [7]
	{0.0, 0.0, 0.0, 1.0}, // black
	{1.0, 0.0, 0.0, 1.0}, // red
	{0.0, 1.0, 0.0, 1.0}, // green
	{1.0, 1.0, 0.0, 1.0}, // yellow
	{0.0, 0.0, 1.0, 1.0}, // blue
	{0.0, 1.0, 1.0, 1.0}, // cyan
	{1.0, 0.0, 1.0, 1.0}, // magenta
	{1.0, 1.0, 1.0, 1.0}, // white
	// [515]'s BX_COLOREDTEXT extension
	{1.0, 1.0, 1.0, 0.5}, // half transparent
	{0.5, 0.5, 0.5, 1.0}  // half brightness
	// Black's color table
	//{1.0, 1.0, 1.0, 1.0},
	//{1.0, 0.0, 0.0, 1.0},
	//{0.0, 1.0, 0.0, 1.0},
	//{0.0, 0.0, 1.0, 1.0},
	//{1.0, 1.0, 0.0, 1.0},
	//{0.0, 1.0, 1.0, 1.0},
	//{1.0, 0.0, 1.0, 1.0},
	//{0.1, 0.1, 0.1, 1.0}
};

#define STRING_COLORS_COUNT	(sizeof(string_colors) / sizeof(vec4_t))

static void DrawQ_GetTextColor(float color[4], int colorindex, float r, float g, float b, float a, qboolean shadow)
{
	float v = r_textbrightness.value;
	Vector4Copy(string_colors[colorindex], color);
	Vector4Set(color, (color[0] * (1-v) + v) * r, (color[1] * (1-v) + v) * g, (color[2] * (1-v) + v) * b, color[3] * a);
	if (shadow)
	{
		float shadowalpha = color[0]+color[1]+color[2] * 0.8;
		Vector4Set(color, 0, 0, 0, color[3] * bound(0, shadowalpha, 1));
	}
}

static float DrawQ_String_Font_UntilX(float startx, float starty, const char *text, size_t *maxlen, float w, float h, float basered, float basegreen, float baseblue, float basealpha, int flags, int *outcolor, qboolean ignorecolorcodes, const dp_font_t *fnt, float maxx)
{
	int num, shadow, colorindex = STRING_COLOR_DEFAULT;
	size_t i;
	float x = startx, y, s, t, u, v;
	float *av, *at, *ac;
	float color[4];
	int batchcount;
	float vertex3f[QUADELEMENTS_MAXQUADS*4*3];
	float texcoord2f[QUADELEMENTS_MAXQUADS*4*2];
	float color4f[QUADELEMENTS_MAXQUADS*4*4];
	qboolean checkwidth;

	if (*maxlen < 1)
		*maxlen = 1<<30;

	// when basealpha == 0, skip as much as possible (just return width)
	if(basealpha > 0)
	{
		_DrawQ_ProcessDrawFlag(flags);

		R_Mesh_ColorPointer(color4f, 0, 0);
		R_Mesh_ResetTextureState();
		R_Mesh_TexBind(0, R_GetTexture(fnt->tex));
		R_Mesh_TexCoordPointer(0, 2, texcoord2f, 0, 0);
		R_Mesh_VertexPointer(vertex3f, 0, 0);
	}

	ac = color4f;
	at = texcoord2f;
	av = vertex3f;
	batchcount = 0;
	checkwidth = (maxx >= startx);

	for (shadow = r_textshadow.value != 0 && basealpha > 0;shadow >= 0;shadow--)
	{
		if (!outcolor || *outcolor == -1)
			colorindex = STRING_COLOR_DEFAULT;
		else
			colorindex = *outcolor;
		if(basealpha > 0)
			DrawQ_GetTextColor(color, colorindex, basered, basegreen, baseblue, basealpha, shadow);

		x = startx;
		y = starty;
		if (shadow)
		{
			x += r_textshadow.value;
			y += r_textshadow.value;
		}
		for (i = 0;i < *maxlen && text[i];i++)
		{
			if (text[i] == ' ')
			{
				if(checkwidth)
					if(x + fnt->width_of[' '] * w > maxx)
						break; // oops, can't draw this
				x += fnt->width_of[' '] * w;
				continue;
			}
			if (text[i] == STRING_COLOR_TAG && !ignorecolorcodes && i + 1 < *maxlen)
			{
				if (text[i+1] == STRING_COLOR_TAG)
				{
					i++;
				}
				else if (text[i+1] >= '0' && text[i+1] <= '9')
				{
					colorindex = text[i+1] - '0';
					DrawQ_GetTextColor(color, colorindex, basered, basegreen, baseblue, basealpha, shadow);
					i++;
					continue;
				}
			}
			num = (unsigned char) text[i];
			if(checkwidth)
				if(x + fnt->width_of[num] * w > maxx)
					break; // oops, can't draw this
			if(basealpha > 0)
			{
				// FIXME make these smaller to just include the occupied part of the character for slightly faster rendering
				s = (num & 15)*0.0625f + (0.5f / 256.0f);
				t = (num >> 4)*0.0625f + (0.5f / 256.0f);
				u = 0.0625f - (1.0f / 256.0f);
				v = 0.0625f - (1.0f / 256.0f);
				ac[ 0] = color[0];ac[ 1] = color[1];ac[ 2] = color[2];ac[ 3] = color[3];
				ac[ 4] = color[0];ac[ 5] = color[1];ac[ 6] = color[2];ac[ 7] = color[3];
				ac[ 8] = color[0];ac[ 9] = color[1];ac[10] = color[2];ac[11] = color[3];
				ac[12] = color[0];ac[13] = color[1];ac[14] = color[2];ac[15] = color[3];
				at[ 0] = s  ;at[ 1] = t  ;
				at[ 2] = s+u;at[ 3] = t  ;
				at[ 4] = s+u;at[ 5] = t+v;
				at[ 6] = s  ;at[ 7] = t+v;
				av[ 0] = x  ;av[ 1] = y  ;av[ 2] = 10;
				av[ 3] = x+w;av[ 4] = y  ;av[ 5] = 10;
				av[ 6] = x+w;av[ 7] = y+h;av[ 8] = 10;
				av[ 9] = x  ;av[10] = y+h;av[11] = 10;
				ac += 16;
				at += 8;
				av += 12;
				batchcount++;
				if (batchcount >= QUADELEMENTS_MAXQUADS)
				{
					if (basealpha >= (1.0f / 255.0f))
					{
						GL_LockArrays(0, batchcount * 4);
						R_Mesh_Draw(0, batchcount * 4, batchcount * 2, quadelements, 0, 0);
						GL_LockArrays(0, 0);
					}
					batchcount = 0;
					ac = color4f;
					at = texcoord2f;
					av = vertex3f;
				}
			}
			x += fnt->width_of[num] * w;
		}
		if(checkwidth)
			*maxlen = i;
		checkwidth = 0; // we've done all we had to
	}
	if (basealpha > 0)
	{
		if (batchcount > 0)
		{
			GL_LockArrays(0, batchcount * 4);
			R_Mesh_Draw(0, batchcount * 4, batchcount * 2, quadelements, 0, 0);
			GL_LockArrays(0, 0);
		}
	}

	if (outcolor)
		*outcolor = colorindex;

	// note: this relies on the proper text (not shadow) being drawn last
	return x;
}

float DrawQ_String_Font(float startx, float starty, const char *text, size_t maxlen, float w, float h, float basered, float basegreen, float baseblue, float basealpha, int flags, int *outcolor, qboolean ignorecolorcodes, const dp_font_t *fnt)
{
	return DrawQ_String_Font_UntilX(startx, starty, text, &maxlen, w, h, basered, basegreen, baseblue, basealpha, flags, outcolor, ignorecolorcodes, fnt, startx-1);
}

float DrawQ_String(float startx, float starty, const char *text, size_t maxlen, float w, float h, float basered, float basegreen, float baseblue, float basealpha, int flags, int *outcolor, qboolean ignorecolorcodes)
{
	return DrawQ_String_Font(startx, starty, text, maxlen, w, h, basered, basegreen, baseblue, basealpha, flags, outcolor, ignorecolorcodes, &dp_fonts[0]);
}

float DrawQ_TextWidth_Font_UntilWidth(const char *text, size_t *maxlen, float scalex, float scaley, qboolean ignorecolorcodes, const dp_font_t *fnt, float maxWidth)
{
	return DrawQ_String_Font_UntilX(0, 0, text, maxlen, scalex, scaley, 1, 1, 1, 0, 0, NULL, ignorecolorcodes, fnt, maxWidth);
}

float DrawQ_TextWidth_Font(const char *text, size_t maxlen, float scalex, float scaley, qboolean ignorecolorcodes, const dp_font_t *fnt)
{
	return DrawQ_TextWidth_Font_UntilWidth(text, &maxlen, scalex, scaley, ignorecolorcodes, fnt, -1);
}

#if 0
// not used
static int DrawQ_BuildColoredText(char *output2c, size_t maxoutchars, const char *text, int maxreadchars, qboolean ignorecolorcodes, int *outcolor)
{
	int color, numchars = 0;
	char *outputend2c = output2c + maxoutchars - 2;
	if (!outcolor || *outcolor == -1)
		color = STRING_COLOR_DEFAULT;
	else
		color = *outcolor;
	if (!maxreadchars)
		maxreadchars = 1<<30;
	textend = text + maxreadchars;
	while (text != textend && *text)
	{
		if (*text == STRING_COLOR_TAG && !ignorecolorcodes && text + 1 != textend)
		{
			if (text[1] == STRING_COLOR_TAG)
				text++;
			else if (text[1] >= '0' && text[1] <= '9')
			{
				color = text[1] - '0';
				text += 2;
				continue;
			}
		}
		if (output2c >= outputend2c)
			break;
		*output2c++ = *text++;
		*output2c++ = color;
		numchars++;
	}
	output2c[0] = output2c[1] = 0;
	if (outcolor)
		*outcolor = color;
	return numchars;
}
#endif

void DrawQ_SuperPic(float x, float y, cachepic_t *pic, float width, float height, float s1, float t1, float r1, float g1, float b1, float a1, float s2, float t2, float r2, float g2, float b2, float a2, float s3, float t3, float r3, float g3, float b3, float a3, float s4, float t4, float r4, float g4, float b4, float a4, int flags)
{
	float floats[36];

	_DrawQ_ProcessDrawFlag(flags);

	R_Mesh_VertexPointer(floats, 0, 0);
	R_Mesh_ColorPointer(floats + 20, 0, 0);
	R_Mesh_ResetTextureState();
	if (pic)
	{
		if (width == 0)
			width = pic->width;
		if (height == 0)
			height = pic->height;
		R_Mesh_TexBind(0, R_GetTexture(pic->tex));
		R_Mesh_TexCoordPointer(0, 2, floats + 12, 0, 0);
		floats[12] = s1;floats[13] = t1;
		floats[14] = s2;floats[15] = t2;
		floats[16] = s4;floats[17] = t4;
		floats[18] = s3;floats[19] = t3;
	}

	floats[2] = floats[5] = floats[8] = floats[11] = 0;
	floats[0] = floats[9] = x;
	floats[1] = floats[4] = y;
	floats[3] = floats[6] = x + width;
	floats[7] = floats[10] = y + height;
	floats[20] = r1;floats[21] = g1;floats[22] = b1;floats[23] = a1;
	floats[24] = r2;floats[25] = g2;floats[26] = b2;floats[27] = a2;
	floats[28] = r4;floats[29] = g4;floats[30] = b4;floats[31] = a4;
	floats[32] = r3;floats[33] = g3;floats[34] = b3;floats[35] = a3;

	R_Mesh_Draw(0, 4, 2, polygonelements, 0, 0);
}

void DrawQ_Mesh (drawqueuemesh_t *mesh, int flags)
{
	_DrawQ_ProcessDrawFlag(flags);

	R_Mesh_VertexPointer(mesh->data_vertex3f, 0, 0);
	R_Mesh_ColorPointer(mesh->data_color4f, 0, 0);
	R_Mesh_ResetTextureState();
	R_Mesh_TexBind(0, R_GetTexture(mesh->texture));
	R_Mesh_TexCoordPointer(0, 2, mesh->data_texcoord2f, 0, 0);

	GL_LockArrays(0, mesh->num_vertices);
	R_Mesh_Draw(0, mesh->num_vertices, mesh->num_triangles, mesh->data_element3i, 0, 0);
	GL_LockArrays(0, 0);
}

void DrawQ_LineLoop (drawqueuemesh_t *mesh, int flags)
{
	int num;

	_DrawQ_ProcessDrawFlag(flags);

	GL_Color(1,1,1,1);
	CHECKGLERROR
	qglBegin(GL_LINE_LOOP);
	for (num = 0;num < mesh->num_vertices;num++)
	{
		if (mesh->data_color4f)
			GL_Color(mesh->data_color4f[num*4+0], mesh->data_color4f[num*4+1], mesh->data_color4f[num*4+2], mesh->data_color4f[num*4+3]);
		qglVertex2f(mesh->data_vertex3f[num*3+0], mesh->data_vertex3f[num*3+1]);
	}
	qglEnd();
	CHECKGLERROR
}

//[515]: this is old, delete
void DrawQ_Line (float width, float x1, float y1, float x2, float y2, float r, float g, float b, float alpha, int flags)
{
	_DrawQ_ProcessDrawFlag(flags);

	CHECKGLERROR
	qglLineWidth(width);CHECKGLERROR

	GL_Color(r,g,b,alpha);
	CHECKGLERROR
	qglBegin(GL_LINES);
	qglVertex2f(x1, y1);
	qglVertex2f(x2, y2);
	qglEnd();
	CHECKGLERROR
}

void DrawQ_SetClipArea(float x, float y, float width, float height)
{
	_DrawQ_Setup();

	// We have to convert the con coords into real coords
	// OGL uses top to bottom
	GL_Scissor((int)(x * ((float)vid.width / vid_conwidth.integer)), (int)(y * ((float) vid.height / vid_conheight.integer)), (int)(width * ((float)vid.width / vid_conwidth.integer)), (int)(height * ((float)vid.height / vid_conheight.integer)));

	GL_ScissorTest(true);
}

void DrawQ_ResetClipArea(void)
{
	_DrawQ_Setup();
	GL_ScissorTest(false);
}

void DrawQ_Finish(void)
{
	r_refdef.draw2dstage = false;
}

static float blendvertex3f[9] = {-5000, -5000, 10, 10000, -5000, 10, -5000, 10000, 10};
void R_DrawGamma(void)
{
	float c[4];
	if (!vid_usinghwgamma)
	{
		// all the blends ignore depth
		R_Mesh_VertexPointer(blendvertex3f, 0, 0);
		R_Mesh_ColorPointer(NULL, 0, 0);
		R_Mesh_ResetTextureState();
		GL_DepthMask(true);
		GL_DepthRange(0, 1);
		GL_PolygonOffset(0, 0);
		GL_DepthTest(false);
		if (v_color_enable.integer)
		{
			c[0] = v_color_white_r.value;
			c[1] = v_color_white_g.value;
			c[2] = v_color_white_b.value;
		}
		else
			c[0] = c[1] = c[2] = v_contrast.value;
		if (c[0] >= 1.01f || c[1] >= 1.01f || c[2] >= 1.01f)
		{
			GL_BlendFunc(GL_DST_COLOR, GL_ONE);
			while (c[0] >= 1.01f || c[1] >= 1.01f || c[2] >= 1.01f)
			{
				GL_Color(bound(0, c[0] - 1, 1), bound(0, c[1] - 1, 1), bound(0, c[2] - 1, 1), 1);
				R_Mesh_Draw(0, 3, 1, polygonelements, 0, 0);
				VectorScale(c, 0.5, c);
			}
		}
		if (v_color_enable.integer)
		{
			c[0] = v_color_black_r.value;
			c[1] = v_color_black_g.value;
			c[2] = v_color_black_b.value;
		}
		else
			c[0] = c[1] = c[2] = v_brightness.value;
		if (c[0] >= 0.01f || c[1] >= 0.01f || c[2] >= 0.01f)
		{
			GL_BlendFunc(GL_ONE, GL_ONE);
			GL_Color(c[0], c[1], c[2], 1);
			R_Mesh_Draw(0, 3, 1, polygonelements, 0, 0);
		}
	}
}

