/*
	vid_3dfxsvga.c

	OpenGL device driver for 3Dfx chipsets running Linux

	Copyright (C) 1996-1997  Id Software, Inc.
	Copyright (C) 1999,2000  Nelson Rush.
	Copyright (C) 1999,2000  contributors of the QuakeForge project
	Please see the file "AUTHORS" for a list of contributors

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to:

		Free Software Foundation, Inc.
		59 Temple Place - Suite 330
		Boston, MA  02111-1307, USA

	$Id$
*/

#include "quakedef.h"
#include "sys.h"
#include "console.h"
#include "sbar.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#include <dlfcn.h>

#include <GL/gl.h>
#include <GL/fxmesa.h>
#include <glide/sst1vid.h>

#define WARP_WIDTH              320
#define WARP_HEIGHT             200


//unsigned short	d_8to16table[256];
unsigned		d_8to24table[256];
unsigned char	d_15to8table[65536];

cvar_t		vid_mode = {"vid_mode","0",false};

viddef_t	vid;	// global video state

static void	*dlhand = NULL;

static fxMesaContext fc = NULL;
static int	scr_width, scr_height;

int	VID_options_items = 0;

/*-----------------------------------------------------------------------*/

//int	texture_mode = GL_NEAREST;
//int	texture_mode = GL_NEAREST_MIPMAP_NEAREST;
//int	texture_mode = GL_NEAREST_MIPMAP_LINEAR;
int	texture_mode = GL_LINEAR;
//int	texture_mode = GL_LINEAR_MIPMAP_NEAREST;
//int	texture_mode = GL_LINEAR_MIPMAP_LINEAR;

int	texture_extension_number = 1;

float		gldepthmin, gldepthmax;

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;

void (*qglColorTableEXT) (int, int, int, int, int, const void*);
void (*qgl3DfxSetPaletteEXT) (GLuint *);
void (*qglMTexCoord2f) (GLenum, GLfloat, GLfloat);
void (*qglSelectTexture) (GLenum);

int gl_mtex_enum = 0;

// LordHavoc: in GLX these are never set, simply provided to make the rest of the code work
qboolean is8bit = false;
qboolean isPermedia = false;
qboolean isATI = false;
qboolean isG200 = false;
qboolean isRagePro = false;
qboolean gl_mtexable = false;
qboolean gl_arrays = false;

/*-----------------------------------------------------------------------*/
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
}

void D_EndDirectRect (int x, int y, int width, int height)
{
}

void VID_Shutdown(void)
{
	if (!fc)
		return;

	fxMesaDestroyContext(fc);
}

void signal_handler(int sig)
{
	printf("Received signal %d, exiting...\n", sig);
	Host_Shutdown();
	abort();
	//Sys_Quit();
	exit(0);
}

void InitSig(void)
{
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGQUIT, signal_handler);
	signal(SIGILL, signal_handler);
	signal(SIGTRAP, signal_handler);
//	signal(SIGIOT, signal_handler);
	signal(SIGBUS, signal_handler);
//	signal(SIGFPE, signal_handler);
	signal(SIGSEGV, signal_handler);
	signal(SIGTERM, signal_handler);
}

/*
	CheckMultiTextureExtensions

	Check for ARB, SGIS, or EXT multitexture support
*/
void
CheckMultiTextureExtensions ( void )
{
	Con_Printf ("Checking for multitexture... ");
	if (COM_CheckParm ("-nomtex"))
	{
		Con_Printf ("disabled\n");
		return;
	}
	dlhand = dlopen (NULL, RTLD_LAZY);
	if (dlhand == NULL)
	{
		Con_Printf ("unable to check\n");
		return;
	}
	if (strstr(gl_extensions, "GL_ARB_multitexture "))
	{
		Con_Printf ("GL_ARB_multitexture\n");
		qglMTexCoord2f = (void *)dlsym(dlhand, "glMultiTexCoord2fARB");
		qglSelectTexture = (void *)dlsym(dlhand, "glActiveTextureARB");
		gl_mtex_enum = GL_TEXTURE0_ARB;
		gl_mtexable = true;
	} else if (strstr(gl_extensions, "GL_SGIS_multitexture "))
	{
		Con_Printf ("GL_SGIS_multitexture\n");
		qglMTexCoord2f = (void *)dlsym(dlhand, "glMTexCoord2fSGIS");
		qglSelectTexture = (void *)dlsym(dlhand, "glSelectTextureSGIS");
		gl_mtex_enum = TEXTURE0_SGIS;
		gl_mtexable = true;
	} else {
		Con_Printf ("none found\n");
	}
	dlclose(dlhand);
	dlhand = NULL;		
}


typedef void (GLAPIENTRY *gl3DfxSetDitherModeEXT_FUNC) (GrDitherMode_t mode);

/*
===============
GL_Init
===============
*/
void GL_Init (void)
{
	gl_vendor = glGetString (GL_VENDOR);
	Con_Printf ("GL_VENDOR: %s\n", gl_vendor);
	gl_renderer = glGetString (GL_RENDERER);
	Con_Printf ("GL_RENDERER: %s\n", gl_renderer);

	gl_version = glGetString (GL_VERSION);
	Con_Printf ("GL_VERSION: %s\n", gl_version);
	gl_extensions = glGetString (GL_EXTENSIONS);
	Con_Printf ("GL_EXTENSIONS: %s\n", gl_extensions);

	CheckMultiTextureExtensions ();

	glClearColor (1,0,0,0);
	glCullFace(GL_FRONT);
	glEnable(GL_TEXTURE_2D);

	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.666);

	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel (GL_FLAT);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	Con_Printf ("Dithering: ");

	dlhand = dlopen (NULL, RTLD_LAZY);

	if (dlhand == NULL) {
		Con_SafePrintf ("unable to set.\n");
		return;
	}

	if (strstr(gl_extensions, "3DFX_set_dither_mode")) {
		gl3DfxSetDitherModeEXT_FUNC dither_select = NULL;

		dither_select = (void *) dlsym(dlhand, "gl3DfxSetDitherModeEXT");

		if (COM_CheckParm ("-dither_2x2")) {
			dither_select(GR_DITHER_2x2);
			Con_Printf ("2x2.\n");
		} else if (COM_CheckParm ("-dither_4x4")) {
			dither_select(GR_DITHER_4x4);
			Con_Printf ("4x4.\n");
		} else {
			glDisable(GL_DITHER);
			Con_Printf ("disabled.\n");
		}
	}
	dlclose(dlhand);
	dlhand = NULL;
}

/*
=================
GL_BeginRendering

=================
*/
void GL_BeginRendering (int *x, int *y, int *width, int *height)
{
	*x = *y = 0;
	*width = scr_width;
	*height = scr_height;

//    if (!wglMakeCurrent( maindc, baseRC ))
//		Sys_Error ("wglMakeCurrent failed");

//	glViewport (*x, *y, *width, *height);
}


void GL_EndRendering (void)
{
	glFlush();
	fxMesaSwapBuffers();
}

static int resolutions[][3]={
	{ 320,	200,	GR_RESOLUTION_320x200 },
	{ 320,	240,	GR_RESOLUTION_320x240 },
	{ 400,	256,	GR_RESOLUTION_400x256 },
	{ 400,	300,	GR_RESOLUTION_400x300 },
	{ 512,	256,	GR_RESOLUTION_512x256 },
	{ 512,	384,	GR_RESOLUTION_512x384 },
	{ 640,	200,	GR_RESOLUTION_640x200 },
	{ 640,	350,	GR_RESOLUTION_640x350 },
	{ 640,	400,	GR_RESOLUTION_640x400 },
	{ 640,	480,	GR_RESOLUTION_640x480 },
	{ 800,	600,	GR_RESOLUTION_800x600 },
	{ 856,	480,	GR_RESOLUTION_856x480 },
	{ 960,	720,	GR_RESOLUTION_960x720 },
#ifdef GR_RESOLUTION_1024x768
	{ 1024,	768,	GR_RESOLUTION_1024x768 },
#endif
#ifdef GR_RESOLUTION_1152x864
	{ 1152,	864,	GR_RESOLUTION_1152x864 },
#endif
#ifdef GR_RESOLUTION_1280x960
	{ 1280,	960,	GR_RESOLUTION_1280x960 },
#endif
#ifdef GR_RESOLUTION_1280x1024
	{ 1280,	1024,	GR_RESOLUTION_1280x1024 },
#endif
#ifdef GR_RESOLUTION_1600x1024
	{ 1600,	1024,	GR_RESOLUTION_1600x1024 },
#endif
#ifdef GR_RESOLUTION_1600x1200
	{ 1600,	1200,	GR_RESOLUTION_1600x1200 },
#endif
#ifdef GR_RESOLUTION_1792x1344
	{ 1792,	1344,	GR_RESOLUTION_1792x1344 },
#endif
#ifdef GR_RESOLUTION_1856x1392
	{ 1856,	1392,	GR_RESOLUTION_1856x1392 },
#endif
#ifdef GR_RESOLUTION_1920x1440
	{ 1920,	1440,	GR_RESOLUTION_1920x1440 },
#endif
#ifdef GR_RESOLUTION_2048x1536
	{ 2048,	1536,	GR_RESOLUTION_2048x1536 },
#endif
#ifdef GR_RESOLUTION_2048x2048
	{ 2048,	2048,	GR_RESOLUTION_2048x2048 }
#endif
};

#define NUM_RESOLUTIONS		(sizeof(resolutions)/(sizeof(int)*3))


static int
findres(int *width, int *height)
{
	int i;

	for(i=0; i < NUM_RESOLUTIONS; i++) {
		if((*width <= resolutions[i][0]) &&
		   (*height <= resolutions[i][1])) {
			*width = resolutions[i][0];
			*height = resolutions[i][1];
			return resolutions[i][2];
		}
	}

	*width = 640;
	*height = 480;
	return GR_RESOLUTION_640x480;
}

qboolean VID_Is8bit(void)
{
	return is8bit;
}

typedef void (GLAPIENTRY *glColorTableEXT_FUNC) (GLenum, GLenum, GLsizei, 
		GLenum, GLenum, const GLvoid *);
typedef void (GLAPIENTRY *gl3DfxSetPaletteEXT_FUNC) (GLuint *pal);

void VID_Init8bitPalette()
{
	// Check for 8bit Extensions and initialize them.
	int i;

	dlhand = dlopen (NULL, RTLD_LAZY);

	Con_SafePrintf ("8-bit GL extensions: ");

	if (dlhand == NULL) {
		Con_SafePrintf ("unable to check.\n");
		return;
	}

	if (COM_CheckParm("-no8bit")) {
		Con_SafePrintf("disabled.\n");
		return;
	}

	if (strstr(gl_extensions, "3DFX_set_global_palette") && (qgl3DfxSetPaletteEXT = dlsym(dlhand, "gl3DfxSetPaletteEXT")) != NULL)
	{
		GLubyte table[256][4];
		char *oldpal;

		Con_SafePrintf("3DFX_set_global_palette.\n");
		glEnable( GL_SHARED_TEXTURE_PALETTE_EXT );
		oldpal = (char *) d_8to24table; //d_8to24table3dfx;
		for (i=0;i<256;i++)
		{
			table[i][2] = *oldpal++;
			table[i][1] = *oldpal++;
			table[i][0] = *oldpal++;
			table[i][3] = 255;
			oldpal++;
		}
		qgl3DfxSetPaletteEXT((GLuint *)table);
		is8bit = true;
	} else if (strstr(gl_extensions, "GL_EXT_shared_texture_palette")) {
		char thePalette[256*3];
		char *oldPalette, *newPalette;
		glColorTableEXT_FUNC load_texture = NULL;

		Con_SafePrintf("GL_EXT_shared.\n");
		load_texture = (void *) dlsym(dlhand, "glColorTableEXT");

		glEnable( GL_SHARED_TEXTURE_PALETTE_EXT );
		oldPalette = (char *) d_8to24table; //d_8to24table3dfx;
		newPalette = thePalette;
		for (i=0;i<256;i++) {
			*newPalette++ = *oldPalette++;
			*newPalette++ = *oldPalette++;
			*newPalette++ = *oldPalette++;
			oldPalette++;
		}
		load_texture(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGB, 256, GL_RGB, GL_UNSIGNED_BYTE, (void *) thePalette);
		is8bit = true;
	} else {
		Con_SafePrintf ("not found.\n");
	}

	dlclose(dlhand);
	dlhand = NULL;
}

extern void Check_Gamma (unsigned char *pal);
void VID_Setup15to8Palette ();

void VID_Init(unsigned char *palette)
{
	int i;
	GLint attribs[32];
	char	gldir[MAX_OSPATH];
	int width = 640, height = 480;

// set vid parameters
	attribs[0] = FXMESA_DOUBLEBUFFER;
	attribs[1] = FXMESA_ALPHA_SIZE;
	attribs[2] = 1;
	attribs[3] = FXMESA_DEPTH_SIZE;
	attribs[4] = 1;
	attribs[5] = FXMESA_NONE;

	if ((i = COM_CheckParm("-width")) != 0)
		width = atoi(com_argv[i+1]);
	if ((i = COM_CheckParm("-height")) != 0)
		height = atoi(com_argv[i+1]);

	if ((i = COM_CheckParm("-conwidth")) != 0)
		vid.conwidth = atoi(com_argv[i+1]);
	else
		vid.conwidth = 640;

	vid.conwidth &= 0xfff8; // make it a multiple of eight

	if (vid.conwidth < 320)
		vid.conwidth = 320;

	// pick a conheight that matches with correct aspect
	vid.conheight = vid.conwidth*3 / 4;

	if ((i = COM_CheckParm("-conheight")) != 0)
		vid.conheight = atoi(com_argv[i+1]);
	if (vid.conheight < 200)
		vid.conheight = 200;

	fc = fxMesaCreateContext(0, findres(&width, &height), GR_REFRESH_75Hz,
		attribs);
	if (!fc)
		Sys_Error("Unable to create 3DFX context.\n");

	scr_width = width;
	scr_height = height;

	fxMesaMakeCurrent(fc);

	if (vid.conheight > height)
		vid.conheight = height;
	if (vid.conwidth > width)
		vid.conwidth = width;
	vid.width = vid.conwidth;
	vid.height = vid.conheight;

	vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);

	InitSig(); // trap evil signals

	GL_Init();

	snprintf(gldir, sizeof(gldir), "%s/glquake", com_gamedir);
	Sys_mkdir (gldir);

	VID_SetPalette(palette);

	Check_Gamma(palette);

	// Check for 3DFX Extensions and initialize them.
	VID_Init8bitPalette();

	if (is8bit) // LordHavoc: avoid calculating 15to8 table if it won't be used
		VID_Setup15to8Palette ();

	Con_SafePrintf ("Video mode %dx%d initialized.\n", width, height);

	vid.recalc_refdef = 1;				// force a surface cache flush
}

void VID_ExtraOptionDraw(unsigned int options_draw_cursor)
{
/* Port specific Options menu entrys */
}

void VID_ExtraOptionCmd(int option_cursor)
{
/*
	switch(option_cursor)
	{
	case 12:  // Always start with 12
	break;
	}
*/
}
void VID_InitCvars ()
{
}

void VID_SetCaption (char *text)
{
}

void VID_HandlePause (qboolean pause)
{
}
