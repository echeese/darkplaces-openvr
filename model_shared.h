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

#ifndef __MODEL__
#define __MODEL__

#ifndef SYNCTYPE_T
#define SYNCTYPE_T
typedef enum {ST_SYNC=0, ST_RAND } synctype_t;
#endif

/*

d*_t structures are on-disk representations
m*_t structures are in-memory

*/

typedef enum {mod_brush, mod_sprite, mod_alias} modtype_t;

#include "model_brush.h"
#include "model_sprite.h"
#include "model_alias.h"

typedef struct model_s
{
	char		name[MAX_QPATH];
	qboolean	needload;		// bmodels and sprites don't cache normally

	modtype_t	type;
	int			aliastype; // LordHavoc: Q2 model support
	int			fullbright; // LordHavoc: if true (normally only for sprites) the model/sprite/bmodel is always rendered fullbright
	int			numframes;
	synctype_t	synctype;
	
	int			flags;

// volume occupied by the model graphics
	vec3_t		mins, maxs;
	float		radius;

// solid volume for clipping 
	qboolean	clipbox;
	vec3_t		clipmins, clipmaxs;

// brush model
	int			firstmodelsurface, nummodelsurfaces;

	int			numsubmodels;
	dmodel_t	*submodels;

	int			numplanes;
	mplane_t	*planes;

	int			numleafs;		// number of visible leafs, not counting 0
	mleaf_t		*leafs;

	int			numvertexes;
	mvertex_t	*vertexes;

	int			numedges;
	medge_t		*edges;

	int			numnodes;
	mnode_t		*nodes;

	int			numtexinfo;
	mtexinfo_t	*texinfo;

	int			numsurfaces;
	msurface_t	*surfaces;

	int			numsurfedges;
	int			*surfedges;

	int			numclipnodes;
	dclipnode_t	*clipnodes;

	int			nummarksurfaces;
	msurface_t	**marksurfaces;

	hull_t		hulls[MAX_MAP_HULLS];

	int			numtextures;
	texture_t	**textures;

	byte		*visdata;
	byte		*lightdata;
	char		*entities;

	// LordHavoc: useful for sprites and models
	int			numtris;
	int			numskins;
	int			skinanimrange[1024]; // array of start and length pairs, note: offset from ->cache.data
	int			skinanim[1024*5]; // texture numbers for each frame (indexed by animrange), note: offset from ->cache.data, second note: normal pants shirt glow body (normal contains no shirt/pants/glow colors and body is normal + pants + shirt, but not glow)

// additional model data
	cache_user_t	cache;		// only access through Mod_Extradata

} model_t;

//============================================================================

void	Mod_Init (void);
void	Mod_ClearAll (void);
model_t *Mod_ForName (char *name, qboolean crash);
void	*Mod_Extradata (model_t *mod);	// handles caching
void	Mod_TouchModel (char *name);

mleaf_t *Mod_PointInLeaf (float *p, model_t *model);
byte	*Mod_LeafPVS (mleaf_t *leaf, model_t *model);

extern model_t	*loadmodel;
extern char	loadname[32];	// for hunk tags

extern model_t *Mod_LoadModel (model_t *mod, qboolean crash);

extern float RadiusFromBounds (vec3_t mins, vec3_t maxs);
extern model_t *Mod_FindName (char *name);
#endif	// __MODEL__
