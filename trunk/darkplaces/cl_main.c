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
// cl_main.c  -- client main loop

#include "quakedef.h"
#include "cl_collision.h"
#include "cl_video.h"

// we need to declare some mouse variables here, because the menu system
// references them even when on a unix system.

// these two are not intended to be set directly
cvar_t cl_name = {CVAR_SAVE, "_cl_name", "player"};
cvar_t cl_color = {CVAR_SAVE, "_cl_color", "0"};
cvar_t cl_pmodel = {CVAR_SAVE, "_cl_pmodel", "0"};

cvar_t cl_shownet = {0, "cl_shownet","0"};
cvar_t cl_nolerp = {0, "cl_nolerp", "0"};

cvar_t cl_itembobheight = {0, "cl_itembobheight", "8"};
cvar_t cl_itembobspeed = {0, "cl_itembobspeed", "0.5"};

cvar_t lookspring = {CVAR_SAVE, "lookspring","0"};
cvar_t lookstrafe = {CVAR_SAVE, "lookstrafe","0"};
cvar_t sensitivity = {CVAR_SAVE, "sensitivity","3", 1, 30};

cvar_t m_pitch = {CVAR_SAVE, "m_pitch","0.022"};
cvar_t m_yaw = {CVAR_SAVE, "m_yaw","0.022"};
cvar_t m_forward = {CVAR_SAVE, "m_forward","1"};
cvar_t m_side = {CVAR_SAVE, "m_side","0.8"};

cvar_t freelook = {CVAR_SAVE, "freelook", "1"};

cvar_t r_draweffects = {0, "r_draweffects", "1"};

cvar_t cl_explosions = {CVAR_SAVE, "cl_explosions", "1"};
cvar_t cl_stainmaps = {CVAR_SAVE, "cl_stainmaps", "1"};

mempool_t *cl_scores_mempool;
mempool_t *cl_refdef_mempool;

client_static_t	cls;
client_state_t	cl;
// FIXME: put these on hunk?
entity_t		cl_entities[MAX_EDICTS];
entity_t		cl_static_entities[MAX_STATIC_ENTITIES];
lightstyle_t	cl_lightstyle[MAX_LIGHTSTYLES];

typedef struct effect_s
{
	int active;
	vec3_t origin;
	float starttime;
	float framerate;
	int modelindex;
	int startframe;
	int endframe;
	// these are for interpolation
	int frame;
	double frame1time;
	double frame2time;
}
cl_effect_t;

#define MAX_EFFECTS 256

static cl_effect_t cl_effect[MAX_EFFECTS];


/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState (void)
{
	int			i;

	if (!sv.active)
		Host_ClearMemory ();

	Mem_EmptyPool(cl_scores_mempool);

// wipe the entire cl structure
	memset (&cl, 0, sizeof(cl));

	SZ_Clear (&cls.message);

// clear other arrays
	memset(cl_entities, 0, sizeof(cl_entities));
	memset(cl_lightstyle, 0, sizeof(cl_lightstyle));
	memset(cl_temp_entities, 0, sizeof(cl_temp_entities));
	memset(cl_beams, 0, sizeof(cl_beams));
	memset(cl_dlights, 0, sizeof(cl_dlights));
	memset(cl_effect, 0, sizeof(cl_effect));
	CL_Screen_NewMap();
	CL_Particles_Clear();
	// LordHavoc: have to set up the baseline info for alpha and other stuff
	for (i = 0;i < MAX_EDICTS;i++)
	{
		ClearStateToDefault(&cl_entities[i].state_baseline);
		ClearStateToDefault(&cl_entities[i].state_previous);
		ClearStateToDefault(&cl_entities[i].state_current);
	}

	CL_CGVM_Clear();
}

void CL_LerpUpdate(entity_t *e)
{
	entity_persistent_t *p;
	entity_render_t *r;
	p = &e->persistent;
	r = &e->render;

	if (p->modelindex != e->state_current.modelindex)
	{
		// reset all interpolation information
		p->modelindex = e->state_current.modelindex;
		p->frame1 = p->frame2 = e->state_current.frame;
		p->frame1time = p->frame2time = cl.time;
		p->framelerp = 1;
	}
	else if (p->frame2 != e->state_current.frame)
	{
		// transition to new frame
		p->frame1 = p->frame2;
		p->frame1time = p->frame2time;
		p->frame2 = e->state_current.frame;
		p->frame2time = cl.time;
		p->framelerp = 0;
	}
	else
	{
		// update transition
		p->framelerp = (cl.time - p->frame2time) * 10;
		p->framelerp = bound(0, p->framelerp, 1);
	}

	r->model = cl.model_precache[e->state_current.modelindex];
	Mod_CheckLoaded(r->model);
	r->frame = e->state_current.frame;
	r->frame1 = p->frame1;
	r->frame2 = p->frame2;
	r->framelerp = p->framelerp;
	r->frame1time = p->frame1time;
	r->frame2time = p->frame2time;
}

/*
=====================
CL_Disconnect

Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect (void)
{
	if (cls.state == ca_dedicated)
		return;

// stop sounds (especially looping!)
	S_StopAllSounds (true);

	// clear contents blends
	cl.cshifts[0].percent = 0;
	cl.cshifts[1].percent = 0;
	cl.cshifts[2].percent = 0;
	cl.cshifts[3].percent = 0;

	cl.worldmodel = NULL;

	if (cls.demoplayback)
		CL_StopPlayback ();
	else if (cls.state == ca_connected)
	{
		if (cls.demorecording)
			CL_Stop_f ();

		Con_DPrintf ("Sending clc_disconnect\n");
		SZ_Clear (&cls.message);
		MSG_WriteByte (&cls.message, clc_disconnect);
		NET_SendUnreliableMessage (cls.netcon, &cls.message);
		SZ_Clear (&cls.message);
		NET_Close (cls.netcon);
		cls.state = ca_disconnected; // prevent this code from executing again during Host_ShutdownServer
		// if running a local server, shut it down
		if (sv.active)
			Host_ShutdownServer(false);
	}
	cls.state = ca_disconnected;

	cls.demoplayback = cls.timedemo = false;
	cls.signon = 0;
}

void CL_Disconnect_f (void)
{
	CL_Disconnect ();
	if (sv.active)
		Host_ShutdownServer (false);
}




/*
=====================
CL_EstablishConnection

Host should be either "local" or a net address to be passed on
=====================
*/
void CL_EstablishConnection (char *host)
{
	if (cls.state == ca_dedicated)
		return;

	if (cls.demoplayback)
		return;

	CL_Disconnect ();

	cls.netcon = NET_Connect (host);
	if (!cls.netcon)
		Host_Error ("CL_Connect: connect failed\n");
	Con_DPrintf ("CL_EstablishConnection: connected to %s\n", host);

	cls.demonum = -1;			// not in the demo loop now
	cls.state = ca_connected;
	cls.signon = 0;				// need all the signon messages before playing

	CL_ClearState ();
}

/*
==============
CL_PrintEntities_f
==============
*/
static void CL_PrintEntities_f (void)
{
	entity_t	*ent;
	int			i, j;
	char		name[32];

	for (i = 0, ent = cl_entities;i < MAX_EDICTS /*cl.num_entities*/;i++, ent++)
	{
		if (!ent->state_current.active)
			continue;
		if (!ent->render.model)
			continue;

		Con_Printf ("%3i:", i);
		if (!ent->render.model)
		{
			Con_Printf ("EMPTY\n");
			continue;
		}
		strncpy(name, ent->render.model->name, 25);
		name[25] = 0;
		for (j = strlen(name);j < 25;j++)
			name[j] = ' ';
		Con_Printf ("%s:%04i (%5i %5i %5i) [%3i %3i %3i] %4.2f %5.3f\n", name, ent->render.frame, (int) ent->render.origin[0], (int) ent->render.origin[1], (int) ent->render.origin[2], (int) ent->render.angles[0] % 360, (int) ent->render.angles[1] % 360, (int) ent->render.angles[2] % 360, ent->render.scale, ent->render.alpha);
	}
}


/*
===============
CL_LerpPoint

Determines the fraction between the last two messages that the objects
should be put at.
===============
*/
static float CL_LerpPoint (void)
{
	float	f;

	// dropped packet, or start of demo
	if (cl.mtime[1] < cl.mtime[0] - 0.1)
		cl.mtime[1] = cl.mtime[0] - 0.1;

	cl.time = bound(cl.mtime[1], cl.time, cl.mtime[0]);

	// LordHavoc: lerp in listen games as the server is being capped below the client (usually)
	f = cl.mtime[0] - cl.mtime[1];
	if (!f || cl_nolerp.integer || cls.timedemo || (sv.active && svs.maxclients == 1))
	{
		cl.time = cl.mtime[0];
		return 1;
	}

	f = (cl.time - cl.mtime[1]) / f;
	return bound(0, f, 1);
}

static void CL_RelinkStaticEntities(void)
{
	int i;
	for (i = 0;i < cl.num_statics && r_refdef.numentities < MAX_VISEDICTS;i++)
	{
		Mod_CheckLoaded(cl_static_entities[i].render.model);
		r_refdef.entities[r_refdef.numentities++] = &cl_static_entities[i].render;
	}
}

/*
===============
CL_RelinkEntities
===============
*/
static void CL_RelinkNetworkEntities()
{
	entity_t	*ent;
	int			i, glowcolor, effects;
	float		f, d, bobjrotate, bobjoffset, dlightradius, glowsize, lerp;
	vec3_t		oldorg, neworg, delta, dlightcolor;

	bobjrotate = ANGLEMOD(100*cl.time);
	if (cl_itembobheight.value)
		bobjoffset = (cos(cl.time * cl_itembobspeed.value * (2.0 * M_PI)) + 1.0) * 0.5 * cl_itembobheight.value;
	else
		bobjoffset = 0;

// start on the entity after the world
	for (i = 1, ent = cl_entities + 1;i < MAX_EDICTS /*cl.num_entities*/;i++, ent++)
	{
		// if the object wasn't included in the latest packet, remove it
		if (!ent->state_current.active)
			continue;

		VectorCopy(ent->persistent.trail_origin, oldorg);

		if (!ent->state_previous.active)
		{
			// only one state available
			lerp = 1;
			VectorCopy (ent->state_current.origin, oldorg); // skip trails
			VectorCopy (ent->state_current.origin, neworg);
			VectorCopy (ent->state_current.angles, ent->render.angles);
		}
		else
		{
			// if the delta is large, assume a teleport and don't lerp
			VectorSubtract(ent->state_current.origin, ent->state_previous.origin, delta);
			// LordHavoc: increased tolerance from 100 to 200, and now to 1000
			if ((sv.active && svs.maxclients == 1 && !(ent->state_current.flags & RENDER_STEP)) || cls.timedemo || DotProduct(delta, delta) > 1000*1000 || cl_nolerp.integer)
				lerp = 1;
			else
			{
				f = ent->state_current.time - ent->state_previous.time;
				if (f > 0)
					lerp = (cl.time - ent->state_previous.time) / f;
				else
					lerp = 1;
			}
			if (lerp >= 1)
			{
				// no interpolation
				VectorCopy (ent->state_current.origin, neworg);
				VectorCopy (ent->state_current.angles, ent->render.angles);
			}
			else
			{
				// interpolate the origin and angles
				VectorMA(ent->state_previous.origin, lerp, delta, neworg);
				VectorSubtract(ent->state_current.angles, ent->state_previous.angles, delta);
				if (delta[0] < -180) delta[0] += 360;else if (delta[0] >= 180) delta[0] -= 360;
				if (delta[1] < -180) delta[1] += 360;else if (delta[1] >= 180) delta[1] -= 360;
				if (delta[2] < -180) delta[2] += 360;else if (delta[2] >= 180) delta[2] -= 360;
				VectorMA(ent->state_previous.angles, lerp, delta, ent->render.angles);
			}
		}

		VectorCopy (neworg, ent->persistent.trail_origin);
		// persistent.modelindex will be updated by CL_LerpUpdate
		if (ent->state_current.modelindex != ent->persistent.modelindex)
			VectorCopy(neworg, oldorg);

		VectorCopy (neworg, ent->render.origin);
		ent->render.flags = ent->state_current.flags;
		ent->render.effects = effects = ent->state_current.effects;
		if (cl.scores == NULL || !ent->state_current.colormap)
			ent->render.colormap = -1; // no special coloring
		else
			ent->render.colormap = cl.scores[ent->state_current.colormap - 1].colors; // color it
		ent->render.skinnum = ent->state_current.skin;
		ent->render.alpha = ent->state_current.alpha * (1.0f / 255.0f); // FIXME: interpolate?
		ent->render.scale = ent->state_current.scale * (1.0f / 16.0f); // FIXME: interpolate?

		// update interpolation info
		CL_LerpUpdate(ent);

		// handle effects now...
		dlightradius = 0;
		dlightcolor[0] = 0;
		dlightcolor[1] = 0;
		dlightcolor[2] = 0;

		// LordHavoc: if the entity has no effects, don't check each
		if (effects)
		{
			if (effects & EF_BRIGHTFIELD)
				CL_EntityParticles (ent);
			if (effects & EF_MUZZLEFLASH)
				ent->persistent.muzzleflash = 100.0f;
			if (effects & EF_DIMLIGHT)
			{
				dlightcolor[0] += 200.0f;
				dlightcolor[1] += 200.0f;
				dlightcolor[2] += 200.0f;
			}
			if (effects & EF_BRIGHTLIGHT)
			{
				dlightcolor[0] += 400.0f;
				dlightcolor[1] += 400.0f;
				dlightcolor[2] += 400.0f;
			}
			// LordHavoc: added EF_RED and EF_BLUE
			if (effects & EF_RED) // red
			{
				dlightcolor[0] += 200.0f;
				dlightcolor[1] +=  20.0f;
				dlightcolor[2] +=  20.0f;
			}
			if (effects & EF_BLUE) // blue
			{
				dlightcolor[0] +=  20.0f;
				dlightcolor[1] +=  20.0f;
				dlightcolor[2] += 200.0f;
			}
			if (effects & EF_FLAME)
			{
				if (ent->render.model)
				{
					vec3_t mins, maxs;
					int temp;
					mins[0] = neworg[0] - 16.0f;
					mins[1] = neworg[1] - 16.0f;
					mins[2] = neworg[2] - 16.0f;
					maxs[0] = neworg[0] + 16.0f;
					maxs[1] = neworg[1] + 16.0f;
					maxs[2] = neworg[2] + 16.0f;
					// how many flames to make
					temp = (int) (cl.time * 300) - (int) (cl.oldtime * 300);
					CL_FlameCube(mins, maxs, temp);
				}
				d = lhrandom(200, 250);
				dlightcolor[0] += d * 1.0f;
				dlightcolor[1] += d * 0.7f;
				dlightcolor[2] += d * 0.3f;
			}
			if (effects & EF_STARDUST)
			{
				if (ent->render.model)
				{
					vec3_t mins, maxs;
					int temp;
					mins[0] = neworg[0] - 16.0f;
					mins[1] = neworg[1] - 16.0f;
					mins[2] = neworg[2] - 16.0f;
					maxs[0] = neworg[0] + 16.0f;
					maxs[1] = neworg[1] + 16.0f;
					maxs[2] = neworg[2] + 16.0f;
					// how many particles to make
					temp = (int) (cl.time * 200) - (int) (cl.oldtime * 200);
					CL_Stardust(mins, maxs, temp);
				}
				d = 100;
				dlightcolor[0] += d * 1.0f;
				dlightcolor[1] += d * 0.7f;
				dlightcolor[2] += d * 0.3f;
			}
		}

		if (ent->persistent.muzzleflash > 0)
		{
			vec3_t v, v2;

			AngleVectors (ent->render.angles, v, NULL, NULL);

			v2[0] = v[0] * 18 + neworg[0];
			v2[1] = v[1] * 18 + neworg[1];
			v2[2] = v[2] * 18 + neworg[2] + 16;
			CL_TraceLine(neworg, v2, v, NULL, 0, true);

			CL_AllocDlight (NULL, v, ent->persistent.muzzleflash, 1, 1, 1, 0, 0);
			ent->persistent.muzzleflash -= cl.frametime * 1000;
		}

		// LordHavoc: if the model has no flags, don't check each
		if (ent->render.model && ent->render.model->flags)
		{
			if (ent->render.model->flags & EF_ROTATE)
			{
				ent->render.angles[1] = bobjrotate;
				ent->render.origin[2] += bobjoffset;
			}
			// only do trails if present in the previous frame as well
			if (ent->state_previous.active)
			{
				if (ent->render.model->flags & EF_GIB)
					CL_RocketTrail (oldorg, neworg, 2, ent);
				else if (ent->render.model->flags & EF_ZOMGIB)
					CL_RocketTrail (oldorg, neworg, 4, ent);
				else if (ent->render.model->flags & EF_TRACER)
				{
					CL_RocketTrail (oldorg, neworg, 3, ent);
					dlightcolor[0] += 0x10;
					dlightcolor[1] += 0x40;
					dlightcolor[2] += 0x10;
				}
				else if (ent->render.model->flags & EF_TRACER2)
				{
					CL_RocketTrail (oldorg, neworg, 5, ent);
					dlightcolor[0] += 0x50;
					dlightcolor[1] += 0x30;
					dlightcolor[2] += 0x10;
				}
				else if (ent->render.model->flags & EF_ROCKET)
				{
					CL_RocketTrail (oldorg, ent->render.origin, 0, ent);
					dlightcolor[0] += 200.0f;
					dlightcolor[1] += 160.0f;
					dlightcolor[2] +=  80.0f;
				}
				else if (ent->render.model->flags & EF_GRENADE)
				{
					if (ent->render.alpha == -1) // LordHavoc: Nehahra dem compatibility
						CL_RocketTrail (oldorg, neworg, 7, ent);
					else
						CL_RocketTrail (oldorg, neworg, 1, ent);
				}
				else if (ent->render.model->flags & EF_TRACER3)
				{
					CL_RocketTrail (oldorg, neworg, 6, ent);
					dlightcolor[0] += 0x50;
					dlightcolor[1] += 0x20;
					dlightcolor[2] += 0x40;
				}
			}
		}
		// LordHavoc: customizable glow
		glowsize = ent->state_current.glowsize; // FIXME: interpolate?
		glowcolor = ent->state_current.glowcolor;
		if (glowsize)
		{
			qbyte *tempcolor = (qbyte *)&d_8to24table[glowcolor];
			// * 4 for the expansion from 0-255 to 0-1023 range,
			// / 255 to scale down byte colors
			glowsize *= (4.0f / 255.0f);
			VectorMA(dlightcolor, glowsize, tempcolor, dlightcolor);
		}
		// LordHavoc: customizable trail
		if (ent->render.flags & RENDER_GLOWTRAIL)
			CL_RocketTrail2 (oldorg, neworg, glowcolor, ent);

		if (dlightcolor[0] || dlightcolor[1] || dlightcolor[2])
		{
			vec3_t vec;
			VectorCopy(neworg, vec);
			// hack to make glowing player light shine on their gun
			if (i == cl.viewentity && !chase_active.integer)
				vec[2] += 30;
			CL_AllocDlight (NULL, vec, 1, dlightcolor[0], dlightcolor[1], dlightcolor[2], 0, 0);
		}

		if (chase_active.integer)
		{
			if (ent->render.flags & RENDER_VIEWMODEL)
				continue;
		}
		else
		{
			if (i == cl.viewentity || (ent->render.flags & RENDER_EXTERIORMODEL))
				continue;
		}

		if (ent->render.model == NULL)
			continue;
		if (effects & EF_NODRAW)
			continue;
		if (r_refdef.numentities < MAX_VISEDICTS)
			r_refdef.entities[r_refdef.numentities++] = &ent->render;
	}
}

void CL_LerpPlayer(float frac)
{
	int i;
	float d;

	if (cl.entitydatabase.numframes)
	{
		cl.viewentorigin[0] = cl.viewentoriginold[0] + frac * (cl.viewentoriginnew[0] - cl.viewentoriginold[0]);
		cl.viewentorigin[1] = cl.viewentoriginold[1] + frac * (cl.viewentoriginnew[1] - cl.viewentoriginold[1]);
		cl.viewentorigin[2] = cl.viewentoriginold[2] + frac * (cl.viewentoriginnew[2] - cl.viewentoriginold[2]);
	}
	else
		VectorCopy (cl_entities[cl.viewentity].render.origin, cl.viewentorigin);

	cl.viewzoom = cl.viewzoomold + frac * (cl.viewzoomnew - cl.viewzoomold);

	for (i = 0;i < 3;i++)
		cl.velocity[i] = cl.mvelocity[1][i] + frac * (cl.mvelocity[0][i] - cl.mvelocity[1][i]);

	if (cls.demoplayback)
	{
		// interpolate the angles
		for (i = 0;i < 3;i++)
		{
			d = cl.mviewangles[0][i] - cl.mviewangles[1][i];
			if (d > 180)
				d -= 360;
			else if (d < -180)
				d += 360;
			cl.viewangles[i] = cl.mviewangles[1][i] + frac*d;
		}
	}
}

void CL_Effect(vec3_t org, int modelindex, int startframe, int framecount, float framerate)
{
	int i;
	cl_effect_t *e;
	if (!modelindex) // sanity check
		return;
	for (i = 0, e = cl_effect;i < MAX_EFFECTS;i++, e++)
	{
		if (e->active)
			continue;
		e->active = true;
		VectorCopy(org, e->origin);
		e->modelindex = modelindex;
		e->starttime = cl.time;
		e->startframe = startframe;
		e->endframe = startframe + framecount;
		e->framerate = framerate;

		e->frame = 0;
		e->frame1time = cl.time;
		e->frame2time = cl.time;
		break;
	}
}

static void CL_RelinkEffects()
{
	int i, intframe;
	cl_effect_t *e;
	entity_t *vis;
	float frame;

	for (i = 0, e = cl_effect;i < MAX_EFFECTS;i++, e++)
	{
		if (e->active)
		{
			frame = (cl.time - e->starttime) * e->framerate + e->startframe;
			intframe = frame;
			if (intframe < 0 || intframe >= e->endframe)
			{
				e->active = false;
				memset(e, 0, sizeof(*e));
				continue;
			}

			if (intframe != e->frame)
			{
				e->frame = intframe;
				e->frame1time = e->frame2time;
				e->frame2time = cl.time;
			}

			if (r_draweffects.integer && (vis = CL_NewTempEntity()))
			{
				// interpolation stuff
				vis->render.frame1 = intframe;
				vis->render.frame2 = intframe + 1;
				if (vis->render.frame2 >= e->endframe)
					vis->render.frame2 = -1; // disappear
				vis->render.framelerp = frame - intframe;
				vis->render.frame1time = e->frame1time;
				vis->render.frame2time = e->frame2time;

				// normal stuff
				VectorCopy(e->origin, vis->render.origin);
				vis->render.model = cl.model_precache[e->modelindex];
				vis->render.frame = vis->render.frame2;
				vis->render.colormap = -1; // no special coloring
				vis->render.scale = 1;
				vis->render.alpha = 1;
			}
		}
	}
}

void CL_RelinkEntities (void)
{
	float frac;

	// fraction from previous network update to current
	frac = CL_LerpPoint ();

	CL_DecayLights ();
	CL_RelinkStaticEntities();
	CL_RelinkNetworkEntities();
	CL_TraceLine_ScanForBModels();
	CL_RelinkEffects();
	CL_MoveParticles();
	CL_UpdateTEnts();

	CL_LerpPlayer(frac);
}


/*
===============
CL_ReadFromServer

Read all incoming data from the server
===============
*/
int CL_ReadFromServer (void)
{
	int ret, netshown;

	cl.oldtime = cl.time;
	cl.time += cl.frametime;

	netshown = false;
	do
	{
		ret = CL_GetMessage ();
		if (ret == -1)
			Host_Error ("CL_ReadFromServer: lost server connection");
		if (!ret)
			break;

		cl.last_received_message = realtime;

		if (cl_shownet.integer)
			netshown = true;

		CL_ParseServerMessage ();
	}
	while (ret && cls.state == ca_connected);

	if (netshown)
		Con_Printf ("\n");

	r_refdef.numentities = 0;
	if (cls.state == ca_connected && cl.worldmodel)
	{
		CL_RelinkEntities ();

		// run cgame code (which can add more entities)
		CL_CGVM_Frame();
	}

//
// bring the links up to date
//
	return 0;
}

/*
=================
CL_SendCmd
=================
*/
void CL_SendCmd (void)
{
	usercmd_t		cmd;

	if (cls.state != ca_connected)
		return;

	if (cls.signon == SIGNONS)
	{
	// get basic movement from keyboard
		CL_BaseMove (&cmd);

		IN_PreMove(); // OS independent code

	// allow mice or other external controllers to add to the move
		IN_Move (&cmd);

		IN_PostMove(); // OS independent code

	// send the unreliable message
		CL_SendMove (&cmd);
	}
#ifndef NOROUTINGFIX
	else
	{
		// LordHavoc: fix for NAT routing of netquake:
		// bounce back a clc_nop message to the newly allocated server port,
		// to establish a routing connection for incoming frames,
		// the server waits for this before sending anything
		if (realtime > cl.sendnoptime)
		{
			Con_DPrintf("sending clc_nop to get server's attention\n");
			cl.sendnoptime = realtime + 3;
			MSG_WriteByte(&cls.message, clc_nop);
		}
	}
#endif

	if (cls.demoplayback)
	{
		SZ_Clear (&cls.message);
		return;
	}

// send the reliable message
	if (!cls.message.cursize)
		return;		// no message at all

	if (!NET_CanSendMessage (cls.netcon))
	{
		Con_DPrintf ("CL_WriteToServer: can't send\n");
		return;
	}

	if (NET_SendMessage (cls.netcon, &cls.message) == -1)
		Host_Error ("CL_WriteToServer: lost server connection");

	SZ_Clear (&cls.message);
}

// LordHavoc: pausedemo command
static void CL_PauseDemo_f (void)
{
	cls.demopaused = !cls.demopaused;
	if (cls.demopaused)
		Con_Printf("Demo paused\n");
	else
		Con_Printf("Demo unpaused\n");
}

/*
======================
CL_PModel_f
LordHavoc: Intended for Nehahra, I personally think this is dumb, but Mindcrime won't listen.
======================
*/
static void CL_PModel_f (void)
{
	int i;
	eval_t *val;

	if (Cmd_Argc () == 1)
	{
		Con_Printf ("\"pmodel\" is \"%s\"\n", cl_pmodel.string);
		return;
	}
	i = atoi(Cmd_Argv(1));

	if (cmd_source == src_command)
	{
		if (cl_pmodel.integer == i)
			return;
		Cvar_SetValue ("_cl_pmodel", i);
		if (cls.state == ca_connected)
			Cmd_ForwardToServer ();
		return;
	}

	host_client->pmodel = i;
	if ((val = GETEDICTFIELDVALUE(host_client->edict, eval_pmodel)))
		val->_float = i;
}

/*
======================
CL_Fog_f
======================
*/
static void CL_Fog_f (void)
{
	if (Cmd_Argc () == 1)
	{
		Con_Printf ("\"fog\" is \"%f %f %f %f\"\n", fog_density, fog_red, fog_green, fog_blue);
		return;
	}
	fog_density = atof(Cmd_Argv(1));
	fog_red = atof(Cmd_Argv(2));
	fog_green = atof(Cmd_Argv(3));
	fog_blue = atof(Cmd_Argv(4));
}

/*
=================
CL_Init
=================
*/
void CL_Init (void)
{
	cl_scores_mempool = Mem_AllocPool("client player info");

	cl_refdef_mempool = Mem_AllocPool("refdef");
	memset(&r_refdef, 0, sizeof(r_refdef));
	r_refdef.entities = Mem_Alloc(cl_refdef_mempool, sizeof(entity_render_t *) * MAX_VISEDICTS);

	SZ_Alloc (&cls.message, 1024, "cls.message");

	CL_InitInput ();
	CL_InitTEnts ();

//
// register our commands
//
	Cvar_RegisterVariable (&cl_name);
	Cvar_RegisterVariable (&cl_color);
	if (gamemode == GAME_NEHAHRA)
		Cvar_RegisterVariable (&cl_pmodel);
	Cvar_RegisterVariable (&cl_upspeed);
	Cvar_RegisterVariable (&cl_forwardspeed);
	Cvar_RegisterVariable (&cl_backspeed);
	Cvar_RegisterVariable (&cl_sidespeed);
	Cvar_RegisterVariable (&cl_movespeedkey);
	Cvar_RegisterVariable (&cl_yawspeed);
	Cvar_RegisterVariable (&cl_pitchspeed);
	Cvar_RegisterVariable (&cl_anglespeedkey);
	Cvar_RegisterVariable (&cl_shownet);
	Cvar_RegisterVariable (&cl_nolerp);
	Cvar_RegisterVariable (&lookspring);
	Cvar_RegisterVariable (&lookstrafe);
	Cvar_RegisterVariable (&sensitivity);
	Cvar_RegisterVariable (&freelook);

	Cvar_RegisterVariable (&m_pitch);
	Cvar_RegisterVariable (&m_yaw);
	Cvar_RegisterVariable (&m_forward);
	Cvar_RegisterVariable (&m_side);

	Cvar_RegisterVariable (&cl_itembobspeed);
	Cvar_RegisterVariable (&cl_itembobheight);

	Cmd_AddCommand ("entities", CL_PrintEntities_f);
	Cmd_AddCommand ("bitprofile", CL_BitProfile_f);
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("record", CL_Record_f);
	Cmd_AddCommand ("stop", CL_Stop_f);
	Cmd_AddCommand ("playdemo", CL_PlayDemo_f);
	Cmd_AddCommand ("timedemo", CL_TimeDemo_f);

	Cmd_AddCommand ("fog", CL_Fog_f);

	// LordHavoc: added pausedemo
	Cmd_AddCommand ("pausedemo", CL_PauseDemo_f);
	if (gamemode == GAME_NEHAHRA)
		Cmd_AddCommand ("pmodel", CL_PModel_f);

	Cvar_RegisterVariable(&r_draweffects);
	Cvar_RegisterVariable(&cl_explosions);
	Cvar_RegisterVariable(&cl_stainmaps);

	CL_Parse_Init();
	CL_Particles_Init();
	CL_Screen_Init();
	CL_CGVM_Init();

	CL_Video_Init();
}

