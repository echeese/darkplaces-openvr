
#include "quakedef.h"
#include "image.h"

cvar_t r_lightningbeam_thickness = {CVAR_SAVE, "r_lightningbeam_thickness", "4"};
cvar_t r_lightningbeam_scroll = {CVAR_SAVE, "r_lightningbeam_scroll", "5"};
cvar_t r_lightningbeam_repeatdistance = {CVAR_SAVE, "r_lightningbeam_repeatdistance", "1024"};
cvar_t r_lightningbeam_color_red = {CVAR_SAVE, "r_lightningbeam_color_red", "1"};
cvar_t r_lightningbeam_color_green = {CVAR_SAVE, "r_lightningbeam_color_green", "1"};
cvar_t r_lightningbeam_color_blue = {CVAR_SAVE, "r_lightningbeam_color_blue", "1"};
cvar_t r_lightningbeam_qmbtexture = {CVAR_SAVE, "r_lightningbeam_qmbtexture", "0"};

rtexture_t *r_lightningbeamtexture;
rtexture_t *r_lightningbeamqmbtexture;
rtexturepool_t *r_lightningbeamtexturepool;

int r_lightningbeamelements[18] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11};

void r_lightningbeams_start(void)
{
	r_lightningbeamtexturepool = R_AllocTexturePool();
	r_lightningbeamtexture = NULL;
	r_lightningbeamqmbtexture = NULL;
}

void r_lightningbeams_setupqmbtexture(void)
{
	r_lightningbeamqmbtexture = loadtextureimage(r_lightningbeamtexturepool, "textures/particles/lightning.pcx", 0, 0, false, TEXF_ALPHA | TEXF_PRECACHE);
	if (r_lightningbeamqmbtexture == NULL)
		Cvar_SetValueQuick(&r_lightningbeam_qmbtexture, false);
}

void r_lightningbeams_setuptexture(void)
{
#if 0
#define BEAMWIDTH 128
#define BEAMHEIGHT 64
#define PATHPOINTS 8
	int i, j, px, py, nearestpathindex, imagenumber;
	float particlex, particley, particlexv, particleyv, dx, dy, s, maxpathstrength;
	qbyte *pixels;
	int *image;
	struct {float x, y, strength;} path[PATHPOINTS], temppath;

	image = Mem_Alloc(tempmempool, BEAMWIDTH * BEAMHEIGHT * sizeof(int));
	pixels = Mem_Alloc(tempmempool, BEAMWIDTH * BEAMHEIGHT * sizeof(qbyte[4]));

	for (imagenumber = 0, maxpathstrength = 0.0339476;maxpathstrength < 0.5;imagenumber++, maxpathstrength += 0.01)
	{
	for (i = 0;i < PATHPOINTS;i++)
	{
		path[i].x = lhrandom(0, 1);
		path[i].y = lhrandom(0.2, 0.8);
		path[i].strength = lhrandom(0, 1);
	}
	for (i = 0;i < PATHPOINTS;i++)
	{
		for (j = i + 1;j < PATHPOINTS;j++)
		{
			if (path[j].x < path[i].x)
			{
				temppath = path[j];
				path[j] = path[i];
				path[i] = temppath;
			}
		}
	}
	particlex = path[0].x;
	particley = path[0].y;
	particlexv = lhrandom(0, 0.02);
	particlexv = lhrandom(-0.02, 0.02);
	memset(image, 0, BEAMWIDTH * BEAMHEIGHT * sizeof(int));
	for (i = 0;i < 65536;i++)
	{
		for (nearestpathindex = 0;nearestpathindex < PATHPOINTS;nearestpathindex++)
			if (path[nearestpathindex].x > particlex)
				break;
		nearestpathindex %= PATHPOINTS;
		dx = path[nearestpathindex].x + lhrandom(-0.01, 0.01);dx = bound(0, dx, 1) - particlex;if (dx < 0) dx += 1;
		dy = path[nearestpathindex].y + lhrandom(-0.01, 0.01);dy = bound(0, dy, 1) - particley;
		s = path[nearestpathindex].strength / sqrt(dx*dx+dy*dy);
		particlexv = particlexv /* (1 - lhrandom(0.08, 0.12))*/ + dx * s;
		particleyv = particleyv /* (1 - lhrandom(0.08, 0.12))*/ + dy * s;
		particlex += particlexv * maxpathstrength;particlex -= (int) particlex;
		particley += particleyv * maxpathstrength;particley = bound(0, particley, 1);
		px = particlex * BEAMWIDTH;
		py = particley * BEAMHEIGHT;
		if (px >= 0 && py >= 0 && px < BEAMWIDTH && py < BEAMHEIGHT)
			image[py*BEAMWIDTH+px] += 16;
	}

	for (py = 0;py < BEAMHEIGHT;py++)
	{
		for (px = 0;px < BEAMWIDTH;px++)
		{
			pixels[(py*BEAMWIDTH+px)*4+0] = bound(0, image[py*BEAMWIDTH+px] * 1.0f, 255.0f);
			pixels[(py*BEAMWIDTH+px)*4+1] = bound(0, image[py*BEAMWIDTH+px] * 1.0f, 255.0f);
			pixels[(py*BEAMWIDTH+px)*4+2] = bound(0, image[py*BEAMWIDTH+px] * 1.0f, 255.0f);
			pixels[(py*BEAMWIDTH+px)*4+3] = 255;
		}
	}

	Image_WriteTGARGBA(va("lightningbeam%i.tga", imagenumber), BEAMWIDTH, BEAMHEIGHT, pixels);
	}

	r_lightningbeamtexture = R_LoadTexture2D(r_lightningbeamtexturepool, "lightningbeam", BEAMWIDTH, BEAMHEIGHT, pixels, TEXTYPE_RGBA, TEXF_PRECACHE, NULL);

	Mem_Free(pixels);
	Mem_Free(image);
#else
#define BEAMWIDTH 64
#define BEAMHEIGHT 128
	float r, g, b, intensity, fx, width, center;
	int x, y;
	qbyte *data, *noise1, *noise2;

	data = Mem_Alloc(tempmempool, BEAMWIDTH * BEAMHEIGHT * 4);
	noise1 = Mem_Alloc(tempmempool, BEAMHEIGHT * BEAMHEIGHT);
	noise2 = Mem_Alloc(tempmempool, BEAMHEIGHT * BEAMHEIGHT);
	fractalnoise(noise1, BEAMHEIGHT, BEAMHEIGHT / 8);
	fractalnoise(noise2, BEAMHEIGHT, BEAMHEIGHT / 16);

	for (y = 0;y < BEAMHEIGHT;y++)
	{
		width = 0.15;//((noise1[y * BEAMHEIGHT] * (1.0f / 256.0f)) * 0.1f + 0.1f);
		center = (noise1[y * BEAMHEIGHT + (BEAMHEIGHT / 2)] / 256.0f) * (1.0f - width * 2.0f) + width;
		for (x = 0;x < BEAMWIDTH;x++, fx++)
		{
			fx = (((float) x / BEAMWIDTH) - center) / width;
			intensity = 1.0f - sqrt(fx * fx);
			if (intensity > 0)
				intensity = pow(intensity, 2) * ((noise2[y * BEAMHEIGHT + x] * (1.0f / 256.0f)) * 0.33f + 0.66f);
			intensity = bound(0, intensity, 1);
			r = intensity * 1.0f;
			g = intensity * 1.0f;
			b = intensity * 1.0f;
			data[(y * BEAMWIDTH + x) * 4 + 0] = (qbyte)(bound(0, r, 1) * 255.0f);
			data[(y * BEAMWIDTH + x) * 4 + 1] = (qbyte)(bound(0, g, 1) * 255.0f);
			data[(y * BEAMWIDTH + x) * 4 + 2] = (qbyte)(bound(0, b, 1) * 255.0f);
			data[(y * BEAMWIDTH + x) * 4 + 3] = (qbyte)255;
		}
	}

	r_lightningbeamtexture = R_LoadTexture2D(r_lightningbeamtexturepool, "lightningbeam", BEAMWIDTH, BEAMHEIGHT, data, TEXTYPE_RGBA, TEXF_PRECACHE, NULL);
	Mem_Free(noise1);
	Mem_Free(noise2);
	Mem_Free(data);
#endif
}

void r_lightningbeams_shutdown(void)
{
	r_lightningbeamtexture = NULL;
	r_lightningbeamqmbtexture = NULL;
	R_FreeTexturePool(&r_lightningbeamtexturepool);
}

void r_lightningbeams_newmap(void)
{
}

void R_LightningBeams_Init(void)
{
	Cvar_RegisterVariable(&r_lightningbeam_thickness);
	Cvar_RegisterVariable(&r_lightningbeam_scroll);
	Cvar_RegisterVariable(&r_lightningbeam_repeatdistance);
	Cvar_RegisterVariable(&r_lightningbeam_color_red);
	Cvar_RegisterVariable(&r_lightningbeam_color_green);
	Cvar_RegisterVariable(&r_lightningbeam_color_blue);
	Cvar_RegisterVariable(&r_lightningbeam_qmbtexture);
	R_RegisterModule("R_LightningBeams", r_lightningbeams_start, r_lightningbeams_shutdown, r_lightningbeams_newmap);
}

void R_CalcLightningBeamPolygonVertex3f(float *v, const float *start, const float *end, const float *offset)
{
	// near right corner
	VectorAdd     (start, offset, (v + 0));
	// near left corner
	VectorSubtract(start, offset, (v + 3));
	// far left corner
	VectorSubtract(end  , offset, (v + 6));
	// far right corner
	VectorAdd     (end  , offset, (v + 9));
}

void R_CalcLightningBeamPolygonTexCoord2f(float *tc, float t1, float t2)
{
	if (r_lightningbeam_qmbtexture.integer)
	{
		// near right corner
		tc[0] = t1;tc[1] = 0;
		// near left corner
		tc[2] = t1;tc[3] = 1;
		// far left corner
		tc[4] = t2;tc[5] = 1;
		// far right corner
		tc[6] = t2;tc[7] = 0;
	}
	else
	{
		// near right corner
		tc[0] = 0;tc[1] = t1;
		// near left corner
		tc[2] = 1;tc[3] = t1;
		// far left corner
		tc[4] = 1;tc[5] = t2;
		// far right corner
		tc[6] = 0;tc[7] = t2;
	}
}

void R_FogLightningBeam_Vertex3f_Color4f(const float *v, float *c, int numverts, float r, float g, float b, float a)
{
	int i;
	vec3_t fogvec;
	float ifog;
	for (i = 0;i < numverts;i++, v += 3, c += 4)
	{
		VectorSubtract(v, r_vieworigin, fogvec);
		ifog = 1 - exp(fogdensity/DotProduct(fogvec,fogvec));
		c[0] = r * ifog;
		c[1] = g * ifog;
		c[2] = b * ifog;
		c[3] = a;
	}
}

float beamrepeatscale;

void R_DrawLightningBeamCallback(const void *calldata1, int calldata2)
{
	const beam_t *b = calldata1;
	rmeshstate_t m;
	vec3_t beamdir, right, up, offset;
	float length, t1, t2;

	R_Mesh_Matrix(&r_identitymatrix);

	// calculate beam direction (beamdir) vector and beam length
	// get difference vector
	VectorSubtract(b->end, b->start, beamdir);
	// find length of difference vector
	length = sqrt(DotProduct(beamdir, beamdir));
	// calculate scale to make beamdir a unit vector (normalized)
	t1 = 1.0f / length;
	// scale beamdir so it is now normalized
	VectorScale(beamdir, t1, beamdir);

	// calculate up vector such that it points toward viewer, and rotates around the beamdir
	// get direction from start of beam to viewer
	VectorSubtract(r_vieworigin, b->start, up);
	// remove the portion of the vector that moves along the beam
	// (this leaves only a vector pointing directly away from the beam)
	t1 = -DotProduct(up, beamdir);
	VectorMA(up, t1, beamdir, up);
	// now we have a vector pointing away from the beam, now we need to normalize it
	VectorNormalizeFast(up);
	// generate right vector from forward and up, the result is already normalized
	// (CrossProduct returns a vector of multiplied length of the two inputs)
	CrossProduct(beamdir, up, right);

	// calculate T coordinate scrolling (start and end texcoord along the beam)
	t1 = cl.time * -r_lightningbeam_scroll.value;// + beamrepeatscale * DotProduct(b->start, beamdir);
	t1 = t1 - (int) t1;
	t2 = t1 + beamrepeatscale * length;

	// the beam is 3 polygons in this configuration:
	//  *   2
	//   * *
	// 1******
	//   * *
	//  *   3
	// they are showing different portions of the beam texture, creating an
	// illusion of a beam that appears to curl around in 3D space
	// (and realize that the whole polygon assembly orients itself to face
	//  the viewer)

	memset(&m, 0, sizeof(m));
	if (r_lightningbeam_qmbtexture.integer)
		m.tex[0] = R_GetTexture(r_lightningbeamqmbtexture);
	else
		m.tex[0] = R_GetTexture(r_lightningbeamtexture);
	m.pointer_texcoord[0] = varray_texcoord2f[0];
	R_Mesh_State_Texture(&m);

	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE);
	GL_DepthMask(false);
	GL_DepthTest(true);
	if (r_lightningbeam_qmbtexture.integer && r_lightningbeamqmbtexture == NULL)
		r_lightningbeams_setupqmbtexture();
	if (!r_lightningbeam_qmbtexture.integer && r_lightningbeamtexture == NULL)
		r_lightningbeams_setuptexture();

	// polygon 1, verts 0-3
	VectorScale(right, r_lightningbeam_thickness.value, offset);
	R_CalcLightningBeamPolygonVertex3f(varray_vertex3f + 0, b->start, b->end, offset);
	// polygon 2, verts 4-7
	VectorAdd(right, up, offset);
	VectorScale(offset, r_lightningbeam_thickness.value * 0.70710681f, offset);
	R_CalcLightningBeamPolygonVertex3f(varray_vertex3f + 12, b->start, b->end, offset);
	// polygon 3, verts 8-11
	VectorSubtract(right, up, offset);
	VectorScale(offset, r_lightningbeam_thickness.value * 0.70710681f, offset);
	R_CalcLightningBeamPolygonVertex3f(varray_vertex3f + 24, b->start, b->end, offset);
	R_CalcLightningBeamPolygonTexCoord2f(varray_texcoord2f[0] + 0, t1, t2);
	R_CalcLightningBeamPolygonTexCoord2f(varray_texcoord2f[0] + 8, t1 + 0.33, t2 + 0.33);
	R_CalcLightningBeamPolygonTexCoord2f(varray_texcoord2f[0] + 16, t1 + 0.66, t2 + 0.66);
	GL_VertexPointer(varray_vertex3f);

	if (fogenabled)
	{
		// per vertex colors if fog is used
		GL_ColorPointer(varray_color4f);
		R_FogLightningBeam_Vertex3f_Color4f(varray_vertex3f, varray_color4f, 12, r_lightningbeam_color_red.value, r_lightningbeam_color_green.value, r_lightningbeam_color_blue.value, 1);
	}
	else
	{
		// solid color if fog is not used
		GL_Color(r_lightningbeam_color_red.value, r_lightningbeam_color_green.value, r_lightningbeam_color_blue.value, 1);
	}

	// draw the 3 polygons as one batch of 6 triangles using the 12 vertices
	R_Mesh_Draw(12, 6, r_lightningbeamelements);
}

void R_DrawLightningBeams(void)
{
	int i;
	beam_t *b;
	vec3_t org;

	if (!cl_beams_polygons.integer)
		return;

	beamrepeatscale = 1.0f / r_lightningbeam_repeatdistance.value;
	for (i = 0, b = cl_beams;i < cl_max_beams;i++, b++)
	{
		if (b->model && b->endtime >= cl.time && b->lightning)
		{
			VectorAdd(b->start, b->end, org);
			VectorScale(org, 0.5f, org);
			R_MeshQueue_AddTransparent(org, R_DrawLightningBeamCallback, b, 0);
		}
	}
}

