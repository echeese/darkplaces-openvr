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
// vid.h -- video driver defs

#ifndef VID_H
#define VID_H

extern int cl_available;

typedef struct
{
	// these are set with VID_GetWindowSize and can change from frame to frame
	int realx;
	int realy;
	int realwidth;
	int realheight;

	int conwidth;
	int conheight;
} viddef_t;

// global video state
extern viddef_t vid;
extern void (*vid_menudrawfn)(void);
extern void (*vid_menukeyfn)(int key);

extern int vid_hidden;
extern int vid_activewindow;

extern cvar_t vid_fullscreen;
extern cvar_t vid_width;
extern cvar_t vid_height;
extern cvar_t vid_bitsperpixel;
extern cvar_t vid_mouse;

// brand of graphics chip
extern const char *gl_vendor;
// graphics chip model and other information
extern const char *gl_renderer;
// begins with 1.0.0, 1.1.0, 1.2.0, 1.2.1, 1.3.0, 1.3.1, or 1.4.0
extern const char *gl_version;
// extensions list, space separated
extern const char *gl_extensions;
// WGL, GLX, or AGL
extern const char *gl_platform;
// another extensions list, containing platform-specific extensions that are
// not in the main list
extern const char *gl_platformextensions;
// name of driver library (opengl32.dll, libGL.so.1, or whatever)
extern char gl_driver[256];

// compatibility hacks
extern qboolean isG200;
extern qboolean isRagePro;

// LordHavoc: GLX_SGI_video_sync and WGL_EXT_swap_control
extern int gl_videosyncavailable;

typedef struct
{
	const char *name;
	void **funcvariable;
}
gl_extensionfunctionlist_t;

typedef struct
{
	const char *name;
	const gl_extensionfunctionlist_t *funcs;
	int *enablevariable;
	const char *disableparm;
}
gl_extensioninfo_t;

int GL_OpenLibrary(const char *name);
void GL_CloseLibrary(void);
void *GL_GetProcAddress(const char *name);
int GL_CheckExtension(const char *name, const gl_extensionfunctionlist_t *funcs, const char *disableparm, int silent);

void VID_Shared_Init(void);

void GL_Init (void);

void VID_CheckExtensions(void);

void VID_Init (void);
int VID_Mode(int fullscreen, int width, int height, int bpp);
// Called at startup

void VID_Shutdown (void);
// Called at shutdown

int VID_SetMode (int modenum);
// sets the mode; only used by the Quake engine for resetting to mode 0 (the
// base mode) on memory allocation failures

// sets hardware gamma correction, returns false if the device does not
// support gamma control
int VID_SetGamma (float prescale, float gamma, float scale, float base);

void VID_GetWindowSize (int *x, int *y, int *width, int *height);

void VID_Finish (void);

void VID_Restart_f(void);

void VID_Open (void);
void VID_Close (void);

#endif

