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
// cl_parse.c  -- parse a message received from the server

#include "quakedef.h"

char *svc_strings[128] =
{
	"svc_bad",
	"svc_nop",
	"svc_disconnect",
	"svc_updatestat",
	"svc_version",		// [long] server version
	"svc_setview",		// [short] entity number
	"svc_sound",			// <see code>
	"svc_time",			// [float] server time
	"svc_print",			// [string] null terminated string
	"svc_stufftext",		// [string] stuffed into client's console buffer
						// the string should be \n terminated
	"svc_setangle",		// [vec3] set the view angle to this absolute value

	"svc_serverinfo",		// [long] version
						// [string] signon string
						// [string]..[0]model cache [string]...[0]sounds cache
						// [string]..[0]item cache
	"svc_lightstyle",		// [byte] [string]
	"svc_updatename",		// [byte] [string]
	"svc_updatefrags",	// [byte] [short]
	"svc_clientdata",		// <shortbits + data>
	"svc_stopsound",		// <see code>
	"svc_updatecolors",	// [byte] [byte]
	"svc_particle",		// [vec3] <variable>
	"svc_damage",			// [byte] impact [byte] blood [vec3] from

	"svc_spawnstatic",
	"OBSOLETE svc_spawnbinary",
	"svc_spawnbaseline",

	"svc_temp_entity",		// <variable>
	"svc_setpause",
	"svc_signonnum",
	"svc_centerprint",
	"svc_killedmonster",
	"svc_foundsecret",
	"svc_spawnstaticsound",
	"svc_intermission",
	"svc_finale",			// [string] music [string] text
	"svc_cdtrack",			// [byte] track [byte] looptrack
	"svc_sellscreen",
	"svc_cutscene",
	"svc_showlmp",	// [string] iconlabel [string] lmpfile [short] x [short] y
	"svc_hidelmp",	// [string] iconlabel
	"svc_skybox", // [string] skyname
	"", // 38
	"", // 39
	"", // 40
	"", // 41
	"", // 42
	"", // 43
	"", // 44
	"", // 45
	"", // 46
	"", // 47
	"", // 48
	"", // 49
	"svc_cgame", //				50		// [short] length [bytes] data
	"svc_unusedlh1", //			51		// unused
	"svc_effect", //			52		// [vector] org [byte] modelindex [byte] startframe [byte] framecount [byte] framerate
	"svc_effect2", //			53		// [vector] org [short] modelindex [short] startframe [byte] framecount [byte] framerate
	"svc_sound2", //			54		// short soundindex instead of byte
	"svc_spawnbaseline2", //	55		// short modelindex instead of byte
	"svc_spawnstatic2", //		56		// short modelindex instead of byte
	"svc_entities", //			57		// [int] deltaframe [int] thisframe [float vector] eye [variable length] entitydata
	"svc_unusedlh3", //			58
	"svc_spawnstaticsound2", //	59		// [coord3] [short] samp [byte] vol [byte] aten
};

//=============================================================================

cvar_t demo_nehahra = {0, "demo_nehahra", "0"};

qboolean Nehahrademcompatibility; // LordHavoc: to allow playback of the early Nehahra movie segments
int dpprotocol; // LordHavoc: version of network protocol, or 0 if not DarkPlaces

/*
==================
CL_ParseStartSoundPacket
==================
*/
void CL_ParseStartSoundPacket(int largesoundindex)
{
    vec3_t  pos;
    int 	channel, ent;
    int 	sound_num;
    int 	volume;
    int 	field_mask;
    float 	attenuation;
 	int		i;
	           
    field_mask = MSG_ReadByte();

    if (field_mask & SND_VOLUME)
		volume = MSG_ReadByte ();
	else
		volume = DEFAULT_SOUND_PACKET_VOLUME;
	
    if (field_mask & SND_ATTENUATION)
		attenuation = MSG_ReadByte () / 64.0;
	else
		attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;
	
	channel = MSG_ReadShort ();
	if (largesoundindex)
		sound_num = (unsigned short) MSG_ReadShort ();
	else
		sound_num = MSG_ReadByte ();

	if (sound_num >= MAX_SOUNDS)
		Host_Error("CL_ParseStartSoundPacket: sound_num (%i) >= MAX_SOUNDS (%i)\n", sound_num, MAX_SOUNDS);

	ent = channel >> 3;
	channel &= 7;

	if (ent > MAX_EDICTS)
		Host_Error ("CL_ParseStartSoundPacket: ent = %i", ent);
	
	for (i=0 ; i<3 ; i++)
		pos[i] = MSG_ReadCoord ();

    S_StartSound (ent, channel, cl.sound_precache[sound_num], pos, volume/255.0, attenuation);
}

/*
==================
CL_KeepaliveMessage

When the client is taking a long time to load stuff, send keepalive messages
so the server doesn't disconnect.
==================
*/
void CL_KeepaliveMessage (void)
{
	float	time;
	static float lastmsg;
	int		ret;
	int		c;
	sizebuf_t	old;
	qbyte		olddata[8192];
	
	if (sv.active)
		return;		// no need if server is local
	if (cls.demoplayback)
		return;

// read messages from server, should just be nops
	old = net_message;
	memcpy (olddata, net_message.data, net_message.cursize);
	
	do
	{
		ret = CL_GetMessage ();
		switch (ret)
		{
		default:
			Host_Error ("CL_KeepaliveMessage: CL_GetMessage failed");
		case 0:
			break;	// nothing waiting
		case 1:
			Host_Error ("CL_KeepaliveMessage: received a message");
			break;
		case 2:
			c = MSG_ReadByte();
			if (c != svc_nop)
				Host_Error ("CL_KeepaliveMessage: datagram wasn't a nop");
			break;
		}
	} while (ret);

	net_message = old;
	memcpy (net_message.data, olddata, net_message.cursize);

// check time
	time = Sys_DoubleTime ();
	if (time - lastmsg < 5)
		return;
	lastmsg = time;

// write out a nop
	Con_Printf ("--> client to server keepalive\n");

	MSG_WriteByte (&cls.message, clc_nop);
	NET_SendMessage (cls.netcon, &cls.message);
	SZ_Clear (&cls.message);
}

void CL_ParseEntityLump(char *entdata)
{
	const char *data;
	char key[128], value[4096];
	FOG_clear(); // LordHavoc: no fog until set
	R_SetSkyBox(""); // LordHavoc: no environment mapped sky until set
	data = entdata;
	if (!data)
		return;
	if (!COM_ParseToken(&data))
		return; // error
	if (com_token[0] != '{')
		return; // error
	while (1)
	{
		if (!COM_ParseToken(&data))
			return; // error
		if (com_token[0] == '}')
			break; // end of worldspawn
		if (com_token[0] == '_')
			strcpy(key, com_token + 1);
		else
			strcpy(key, com_token);
		while (key[strlen(key)-1] == ' ') // remove trailing spaces
			key[strlen(key)-1] = 0;
		if (!COM_ParseToken(&data))
			return; // error
		strcpy(value, com_token);
		if (!strcmp("sky", key))
			R_SetSkyBox(value);
		else if (!strcmp("skyname", key)) // non-standard, introduced by QuakeForge... sigh.
			R_SetSkyBox(value);
		else if (!strcmp("qlsky", key)) // non-standard, introduced by QuakeLives (EEK)
			R_SetSkyBox(value);
		else if (!strcmp("fog", key))
			sscanf(value, "%f %f %f %f", &fog_density, &fog_red, &fog_green, &fog_blue);
		else if (!strcmp("fog_density", key))
			fog_density = atof(value);
		else if (!strcmp("fog_red", key))
			fog_red = atof(value);
		else if (!strcmp("fog_green", key))
			fog_green = atof(value);
		else if (!strcmp("fog_blue", key))
			fog_blue = atof(value);
	}
}

/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
static void CL_SignonReply (void)
{
	//char 	str[8192];

Con_DPrintf ("CL_SignonReply: %i\n", cls.signon);

	switch (cls.signon)
	{
	case 1:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "prespawn");
		break;

	case 2:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va("name \"%s\"\n", cl_name.string));

		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va("color %i %i\n", cl_color.integer >> 4, cl_color.integer & 15));

		if (cl_pmodel.integer)
		{
			MSG_WriteByte (&cls.message, clc_stringcmd);
			MSG_WriteString (&cls.message, va("pmodel %i\n", cl_pmodel.integer));
		}

		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "spawn");
		break;

	case 3:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "begin");
		break;

	case 4:
		Con_ClearNotify();
		break;
	}
}

/*
==================
CL_ParseServerInfo
==================
*/
qbyte entlife[MAX_EDICTS];
void CL_ParseServerInfo (void)
{
	char *str;
	int i;
	int nummodels, numsounds;
	char model_precache[MAX_MODELS][MAX_QPATH];
	char sound_precache[MAX_SOUNDS][MAX_QPATH];
	entity_t *ent;

	Con_DPrintf ("Serverinfo packet received.\n");
//
// wipe the client_state_t struct
//
	CL_ClearState ();

// parse protocol version number
	i = MSG_ReadLong ();
	if (i != PROTOCOL_VERSION && i != DPPROTOCOL_VERSION1 && i != DPPROTOCOL_VERSION2 && i != DPPROTOCOL_VERSION3 && i != 250)
	{
		Con_Printf ("Server is protocol %i, not %i, %i, %i or %i", i, DPPROTOCOL_VERSION1, DPPROTOCOL_VERSION2, DPPROTOCOL_VERSION3, PROTOCOL_VERSION);
		return;
	}
	Nehahrademcompatibility = false;
	if (i == 250)
		Nehahrademcompatibility = true;
	if (cls.demoplayback && demo_nehahra.integer)
		Nehahrademcompatibility = true;
	dpprotocol = i;
	if (dpprotocol != DPPROTOCOL_VERSION1 && dpprotocol != DPPROTOCOL_VERSION2 && dpprotocol != DPPROTOCOL_VERSION3)
		dpprotocol = 0;

// parse maxclients
	cl.maxclients = MSG_ReadByte ();
	if (cl.maxclients < 1 || cl.maxclients > MAX_SCOREBOARD)
	{
		Con_Printf("Bad maxclients (%u) from server\n", cl.maxclients);
		return;
	}
	cl.scores = Mem_Alloc(cl_scores_mempool, cl.maxclients*sizeof(*cl.scores));

// parse gametype
	cl.gametype = MSG_ReadByte ();

// parse signon message
	str = MSG_ReadString ();
	strncpy (cl.levelname, str, sizeof(cl.levelname)-1);

// seperate the printfs so the server message can have a color
	if (!Nehahrademcompatibility) // no messages when playing the Nehahra movie
	{
		Con_Printf("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
		Con_Printf ("%c%s\n", 2, str);
	}

//
// first we go through and touch all of the precache data that still
// happens to be in the cache, so precaching something else doesn't
// needlessly purge it
//

	Mem_CheckSentinelsGlobal();

	Mod_ClearUsed();

	// disable until we get textures for it
	R_ResetSkyBox();

// precache models
	memset (cl.model_precache, 0, sizeof(cl.model_precache));
	for (nummodels=1 ; ; nummodels++)
	{
		str = MSG_ReadString ();
		if (!str[0])
			break;
		if (nummodels==MAX_MODELS)
		{
			Host_Error ("Server sent too many model precaches\n");
			return;
		}
		if (strlen(str) >= MAX_QPATH)
			Host_Error ("Server sent a precache name of %i characters (max %i)", strlen(str), MAX_QPATH - 1);
		strcpy (model_precache[nummodels], str);
		Mod_TouchModel (str);
	}

// precache sounds
	memset (cl.sound_precache, 0, sizeof(cl.sound_precache));
	for (numsounds=1 ; ; numsounds++)
	{
		str = MSG_ReadString ();
		if (!str[0])
			break;
		if (numsounds==MAX_SOUNDS)
		{
			Host_Error ("Server sent too many sound precaches\n");
			return;
		}
		if (strlen(str) >= MAX_QPATH)
			Host_Error ("Server sent a precache name of %i characters (max %i)", strlen(str), MAX_QPATH - 1);
		strcpy (sound_precache[numsounds], str);
		S_TouchSound (str);
	}

	Mod_PurgeUnused();

//
// now we try to load everything else until a cache allocation fails
//

	for (i=1 ; i<nummodels ; i++)
	{
		// LordHavoc: i == 1 means the first model is the world model
		cl.model_precache[i] = Mod_ForName (model_precache[i], false, false, i == 1);
		if (cl.model_precache[i] == NULL)
		{
			Con_Printf("Model %s not found\n", model_precache[i]);
			//return;
		}
		CL_KeepaliveMessage ();
	}

	S_BeginPrecaching ();
	for (i=1 ; i<numsounds ; i++)
	{
		cl.sound_precache[i] = S_PrecacheSound (sound_precache[i], true);
		CL_KeepaliveMessage ();
	}
	S_EndPrecaching ();

// local state
	ent = &cl_entities[0];
	// entire entity array was cleared, so just fill in a few fields
	ent->state_current.active = true;
	ent->render.model = cl.worldmodel = cl.model_precache[1];
	ent->render.scale = 1;
	ent->render.alpha = 1;
	CL_BoundingBoxForEntity(&ent->render);
	// clear entlife array
	memset(entlife, 0, MAX_EDICTS);

	cl_num_entities = 1;

	R_NewMap ();
	CL_CGVM_Start();

	noclip_anglehack = false;		// noclip is turned off at start

	Mem_CheckSentinelsGlobal();

}

void CL_ValidateState(entity_state_t *s)
{
	model_t *model;

	if (!s->active)
		return;

	if (s->modelindex >= MAX_MODELS)
		Host_Error("CL_ValidateState: modelindex (%i) >= MAX_MODELS (%i)\n", s->modelindex, MAX_MODELS);

	// colormap is client index + 1
	if (s->colormap > cl.maxclients)
		Host_Error ("CL_ValidateState: colormap (%i) > cl.maxclients (%i)", s->colormap, cl.maxclients);

	model = cl.model_precache[s->modelindex];
	Mod_CheckLoaded(model);
	if (model && s->frame >= model->numframes)
	{
		Con_DPrintf("CL_ValidateState: no such frame %i in \"%s\"\n", s->frame, model->name);
		s->frame = 0;
	}
	if (model && s->skin > 0 && s->skin >= model->numskins)
	{
		Con_DPrintf("CL_ValidateState: no such skin %i in \"%s\"\n", s->skin, model->name);
		s->skin = 0;
	}
}

void CL_MoveLerpEntityStates(entity_t *ent)
{
	float odelta[3], adelta[3];
	VectorSubtract(ent->state_current.origin, ent->persistent.neworigin, odelta);
	VectorSubtract(ent->state_current.angles, ent->persistent.newangles, adelta);
	if (!ent->state_previous.active || cls.timedemo || DotProduct(odelta, odelta) > 1000*1000 || cl_nolerp.integer)
	{
		// we definitely shouldn't lerp
		ent->persistent.lerpdeltatime = 0;
		ent->persistent.lerpstarttime = cl.mtime[1];
		VectorCopy(ent->state_current.origin, ent->persistent.oldorigin);
		VectorCopy(ent->state_current.angles, ent->persistent.oldangles);
		VectorCopy(ent->state_current.origin, ent->persistent.neworigin);
		VectorCopy(ent->state_current.angles, ent->persistent.newangles);
	}
	else// if (ent->state_current.flags & RENDER_STEP)
	{
		// monster interpolation
		if (DotProduct(odelta, odelta) + DotProduct(adelta, adelta) > 0.01 || cl.mtime[0] - ent->persistent.lerpstarttime >= 0.1)
		{
			ent->persistent.lerpdeltatime = cl.time - ent->persistent.lerpstarttime;
			ent->persistent.lerpstarttime = cl.mtime[1];
			VectorCopy(ent->persistent.neworigin, ent->persistent.oldorigin);
			VectorCopy(ent->persistent.newangles, ent->persistent.oldangles);
			VectorCopy(ent->state_current.origin, ent->persistent.neworigin);
			VectorCopy(ent->state_current.angles, ent->persistent.newangles);
		}
	}
	/*
	else
	{
		// not a monster
		ent->persistent.lerpstarttime = cl.mtime[1];
		// no lerp if it's singleplayer
		//if (sv.active && svs.maxclients == 1 && !ent->state_current.flags & RENDER_STEP)
		//	ent->persistent.lerpdeltatime = 0;
		//else
			ent->persistent.lerpdeltatime = cl.mtime[0] - cl.mtime[1];
		VectorCopy(ent->persistent.neworigin, ent->persistent.oldorigin);
		VectorCopy(ent->persistent.newangles, ent->persistent.oldangles);
		VectorCopy(ent->state_current.origin, ent->persistent.neworigin);
		VectorCopy(ent->state_current.angles, ent->persistent.newangles);
	}
	*/
}

/*
==================
CL_ParseUpdate

Parse an entity update message from the server
If an entities model or origin changes from frame to frame, it must be
relinked.  Other attributes can change without relinking.
==================
*/
void CL_ParseUpdate (int bits)
{
	int num;
	entity_t *ent;
	entity_state_t new;

	if (bits & U_MOREBITS)
		bits |= (MSG_ReadByte()<<8);
	if ((bits & U_EXTEND1) && (!Nehahrademcompatibility))
	{
		bits |= MSG_ReadByte() << 16;
		if (bits & U_EXTEND2)
			bits |= MSG_ReadByte() << 24;
	}

	if (bits & U_LONGENTITY)
		num = (unsigned) MSG_ReadShort ();
	else
		num = (unsigned) MSG_ReadByte ();

	if (num >= MAX_EDICTS)
		Host_Error("CL_ParseUpdate: entity number (%i) >= MAX_EDICTS (%i)\n", num, MAX_EDICTS);
	if (num < 1)
		Host_Error("CL_ParseUpdate: invalid entity number (%i)\n", num);

	ent = cl_entities + num;

	// note: this inherits the 'active' state of the baseline chosen
	// (state_baseline is always active, state_current may not be active if
	// the entity was missing in the last frame)
	if (bits & U_DELTA)
		new = ent->state_current;
	else
	{
		new = ent->state_baseline;
		new.active = true;
	}

	new.number = num;
	new.time = cl.mtime[0];
	new.flags = 0;
	if (bits & U_MODEL)		new.modelindex = (new.modelindex & 0xFF00) | MSG_ReadByte();
	if (bits & U_FRAME)		new.frame = (new.frame & 0xFF00) | MSG_ReadByte();
	if (bits & U_COLORMAP)	new.colormap = MSG_ReadByte();
	if (bits & U_SKIN)		new.skin = MSG_ReadByte();
	if (bits & U_EFFECTS)	new.effects = (new.effects & 0xFF00) | MSG_ReadByte();
	if (bits & U_ORIGIN1)	new.origin[0] = MSG_ReadCoord();
	if (bits & U_ANGLE1)	new.angles[0] = MSG_ReadAngle();
	if (bits & U_ORIGIN2)	new.origin[1] = MSG_ReadCoord();
	if (bits & U_ANGLE2)	new.angles[1] = MSG_ReadAngle();
	if (bits & U_ORIGIN3)	new.origin[2] = MSG_ReadCoord();
	if (bits & U_ANGLE3)	new.angles[2] = MSG_ReadAngle();
	if (bits & U_STEP)		new.flags |= RENDER_STEP;
	if (bits & U_ALPHA)		new.alpha = MSG_ReadByte();
	if (bits & U_SCALE)		new.scale = MSG_ReadByte();
	if (bits & U_EFFECTS2)	new.effects = (new.effects & 0x00FF) | (MSG_ReadByte() << 8);
	if (bits & U_GLOWSIZE)	new.glowsize = MSG_ReadByte();
	if (bits & U_GLOWCOLOR)	new.glowcolor = MSG_ReadByte();
	// apparently the dpcrush demo uses this (unintended, and it uses white anyway)
	if (bits & U_COLORMOD)	MSG_ReadByte();
	if (bits & U_GLOWTRAIL) new.flags |= RENDER_GLOWTRAIL;
	if (bits & U_FRAME2)	new.frame = (new.frame & 0x00FF) | (MSG_ReadByte() << 8);
	if (bits & U_MODEL2)	new.modelindex = (new.modelindex & 0x00FF) | (MSG_ReadByte() << 8);
	if (bits & U_VIEWMODEL)	new.flags |= RENDER_VIEWMODEL;
	if (bits & U_EXTERIORMODEL)	new.flags |= RENDER_EXTERIORMODEL;

	// LordHavoc: to allow playback of the Nehahra movie
	if (Nehahrademcompatibility && (bits & U_EXTEND1))
	{
		// LordHavoc: evil format
		int i = MSG_ReadFloat();
		int j = MSG_ReadFloat() * 255.0f;
		if (i == 2)
		{
			i = MSG_ReadFloat();
			if (i)
				new.effects |= EF_FULLBRIGHT;
		}
		if (j < 0)
			new.alpha = 0;
		else if (j == 0 || j >= 255)
			new.alpha = 255;
		else
			new.alpha = j;
	}

	if (new.active)
		CL_ValidateState(&new);

	ent->state_previous = ent->state_current;
	ent->state_current = new;
	if (ent->state_current.active)
	{
		CL_MoveLerpEntityStates(ent);
		cl_entities_active[ent->state_current.number] = true;
		// mark as visible (no kill this frame)
		entlife[ent->state_current.number] = 2;
	}
}

void CL_ReadEntityFrame(void)
{
	entity_t *ent;
	entity_frame_t entityframe;
	int i;
	EntityFrame_Read(&cl.entitydatabase);
	EntityFrame_FetchFrame(&cl.entitydatabase, EntityFrame_MostRecentlyRecievedFrameNum(&cl.entitydatabase), &entityframe);
	for (i = 0;i < entityframe.numentities;i++)
	{
		// copy the states
		ent = &cl_entities[entityframe.entitydata[i].number];
		ent->state_previous = ent->state_current;
		ent->state_current = entityframe.entitydata[i];
		CL_MoveLerpEntityStates(ent);
		// the entity lives again...
		entlife[ent->state_current.number] = 2;
		cl_entities_active[ent->state_current.number] = true;
	}
	VectorCopy(cl.viewentoriginnew, cl.viewentoriginold);
	VectorCopy(entityframe.eye, cl.viewentoriginnew);
}

void CL_EntityUpdateSetup(void)
{
}

void CL_EntityUpdateEnd(void)
{
	int i;
	// disable entities that disappeared this frame
	for (i = 1;i < MAX_EDICTS;i++)
	{
		// clear only the entities that were active last frame but not this
		// frame, don't waste time clearing all entities (which would cause
		// cache misses)
		if (entlife[i])
		{
			entlife[i]--;
			if (!entlife[i])
				cl_entities[i].state_previous.active = cl_entities[i].state_current.active = 0;
		}
	}
}

/*
==================
CL_ParseBaseline
==================
*/
void CL_ParseBaseline (entity_t *ent, int large)
{
	int i;

	memset(&ent->state_baseline, 0, sizeof(entity_state_t));
	ent->state_baseline.active = true;
	if (large)
	{
		ent->state_baseline.modelindex = (unsigned short) MSG_ReadShort ();
		ent->state_baseline.frame = (unsigned short) MSG_ReadShort ();
	}
	else
	{
		ent->state_baseline.modelindex = MSG_ReadByte ();
		ent->state_baseline.frame = MSG_ReadByte ();
	}
	ent->state_baseline.colormap = MSG_ReadByte();
	ent->state_baseline.skin = MSG_ReadByte();
	for (i = 0;i < 3;i++)
	{
		ent->state_baseline.origin[i] = MSG_ReadCoord ();
		ent->state_baseline.angles[i] = MSG_ReadAngle ();
	}
	ent->state_baseline.alpha = 255;
	ent->state_baseline.scale = 16;
	ent->state_baseline.glowsize = 0;
	ent->state_baseline.glowcolor = 254;
	ent->state_previous = ent->state_current = ent->state_baseline;

	CL_ValidateState(&ent->state_baseline);
}


/*
==================
CL_ParseClientdata

Server information pertaining to this client only
==================
*/
void CL_ParseClientdata (int bits)
{
	int i, j;

	bits &= 0xFFFF;
	if (bits & SU_EXTEND1)
		bits |= (MSG_ReadByte() << 16);
	if (bits & SU_EXTEND2)
		bits |= (MSG_ReadByte() << 24);

	if (bits & SU_VIEWHEIGHT)
		cl.viewheight = MSG_ReadChar ();
	else
		cl.viewheight = DEFAULT_VIEWHEIGHT;

	if (bits & SU_IDEALPITCH)
		cl.idealpitch = MSG_ReadChar ();
	else
		cl.idealpitch = 0;

	VectorCopy (cl.mvelocity[0], cl.mvelocity[1]);
	for (i=0 ; i<3 ; i++)
	{
		if (bits & (SU_PUNCH1<<i) )
		{
			if (dpprotocol)
				cl.punchangle[i] = MSG_ReadPreciseAngle();
			else
				cl.punchangle[i] = MSG_ReadChar();
		}
		else
			cl.punchangle[i] = 0;
		if (bits & (SU_PUNCHVEC1<<i))
			cl.punchvector[i] = MSG_ReadCoord();
		else
			cl.punchvector[i] = 0;
		if (bits & (SU_VELOCITY1<<i) )
			cl.mvelocity[0][i] = MSG_ReadChar()*16;
		else
			cl.mvelocity[0][i] = 0;
	}

	i = MSG_ReadLong ();
	if (cl.items != i)
	{	// set flash times
		for (j=0 ; j<32 ; j++)
			if ( (i & (1<<j)) && !(cl.items & (1<<j)))
				cl.item_gettime[j] = cl.time;
		cl.items = i;
	}

	cl.onground = (bits & SU_ONGROUND) != 0;
	cl.inwater = (bits & SU_INWATER) != 0;

	cl.stats[STAT_WEAPONFRAME] = (bits & SU_WEAPONFRAME) ? MSG_ReadByte() : 0;
	cl.stats[STAT_ARMOR] = (bits & SU_ARMOR) ? MSG_ReadByte() : 0;
	cl.stats[STAT_WEAPON] = (bits & SU_WEAPON) ? MSG_ReadByte() : 0;
	cl.stats[STAT_HEALTH] = MSG_ReadShort();
	cl.stats[STAT_AMMO] = MSG_ReadByte();

	cl.stats[STAT_SHELLS] = MSG_ReadByte();
	cl.stats[STAT_NAILS] = MSG_ReadByte();
	cl.stats[STAT_ROCKETS] = MSG_ReadByte();
	cl.stats[STAT_CELLS] = MSG_ReadByte();

	i = MSG_ReadByte ();

	if (gamemode == GAME_HIPNOTIC || gamemode == GAME_ROGUE)
		cl.stats[STAT_ACTIVEWEAPON] = (1<<i);
	else
		cl.stats[STAT_ACTIVEWEAPON] = i;

	cl.viewzoomold = cl.viewzoomnew; // for interpolation
	if (bits & SU_VIEWZOOM)
	{
		i = MSG_ReadByte();
		if (i < 2)
			i = 2;
		cl.viewzoomnew = (float) i * (1.0f / 255.0f);
	}
	else
		cl.viewzoomnew = 1;

}

/*
=====================
CL_ParseStatic
=====================
*/
void CL_ParseStatic (int large)
{
	entity_t *ent;

	if (cl_num_static_entities >= cl_max_static_entities)
		Host_Error ("Too many static entities");
	ent = &cl_static_entities[cl_num_static_entities++];
	CL_ParseBaseline (ent, large);

// copy it to the current state
	ent->render.model = cl.model_precache[ent->state_baseline.modelindex];
	ent->render.frame = ent->render.frame1 = ent->render.frame2 = ent->state_baseline.frame;
	ent->render.framelerp = 0;
	// make torchs play out of sync
	ent->render.frame1time = ent->render.frame2time = lhrandom(-10, -1);
	ent->render.colormap = -1; // no special coloring
	ent->render.skinnum = ent->state_baseline.skin;
	ent->render.effects = ent->state_baseline.effects;
	ent->render.alpha = 1;
	ent->render.scale = 1;
	ent->render.alpha = 1;

	VectorCopy (ent->state_baseline.origin, ent->render.origin);
	VectorCopy (ent->state_baseline.angles, ent->render.angles);

	CL_BoundingBoxForEntity(&ent->render);

	// This is definitely cheating...
	if (ent->render.model == NULL)
		cl_num_static_entities--;
}

/*
===================
CL_ParseStaticSound
===================
*/
void CL_ParseStaticSound (int large)
{
	vec3_t		org;
	int			sound_num, vol, atten;

	MSG_ReadVector(org);
	if (large)
		sound_num = (unsigned short) MSG_ReadShort ();
	else
		sound_num = MSG_ReadByte ();
	vol = MSG_ReadByte ();
	atten = MSG_ReadByte ();

	S_StaticSound (cl.sound_precache[sound_num], org, vol, atten);
}

void CL_ParseEffect (void)
{
	vec3_t		org;
	int			modelindex, startframe, framecount, framerate;

	MSG_ReadVector(org);
	modelindex = MSG_ReadByte ();
	startframe = MSG_ReadByte ();
	framecount = MSG_ReadByte ();
	framerate = MSG_ReadByte ();

	CL_Effect(org, modelindex, startframe, framecount, framerate);
}

void CL_ParseEffect2 (void)
{
	vec3_t		org;
	int			modelindex, startframe, framecount, framerate;

	MSG_ReadVector(org);
	modelindex = MSG_ReadShort ();
	startframe = MSG_ReadShort ();
	framecount = MSG_ReadByte ();
	framerate = MSG_ReadByte ();

	CL_Effect(org, modelindex, startframe, framecount, framerate);
}

model_t *cl_model_bolt = NULL;
model_t *cl_model_bolt2 = NULL;
model_t *cl_model_bolt3 = NULL;
model_t *cl_model_beam = NULL;

sfx_t *cl_sfx_wizhit;
sfx_t *cl_sfx_knighthit;
sfx_t *cl_sfx_tink1;
sfx_t *cl_sfx_ric1;
sfx_t *cl_sfx_ric2;
sfx_t *cl_sfx_ric3;
sfx_t *cl_sfx_r_exp3;

/*
=================
CL_ParseTEnt
=================
*/
void CL_InitTEnts (void)
{
	cl_sfx_wizhit = S_PrecacheSound ("wizard/hit.wav", false);
	cl_sfx_knighthit = S_PrecacheSound ("hknight/hit.wav", false);
	cl_sfx_tink1 = S_PrecacheSound ("weapons/tink1.wav", false);
	cl_sfx_ric1 = S_PrecacheSound ("weapons/ric1.wav", false);
	cl_sfx_ric2 = S_PrecacheSound ("weapons/ric2.wav", false);
	cl_sfx_ric3 = S_PrecacheSound ("weapons/ric3.wav", false);
	cl_sfx_r_exp3 = S_PrecacheSound ("weapons/r_exp3.wav", false);
}

void CL_ParseBeam (model_t *m)
{
	int i, ent;
	vec3_t start, end;
	beam_t *b;

	ent = MSG_ReadShort ();
	MSG_ReadVector(start);
	MSG_ReadVector(end);

	// override any beam with the same entity
	for (i = 0, b = cl_beams;i < cl_max_beams;i++, b++)
	{
		if (b->entity == ent)
		{
			//b->entity = ent;
			b->model = m;
			b->endtime = cl.time + 0.2;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			return;
		}
	}

	// find a free beam
	for (i = 0, b = cl_beams;i < cl_max_beams;i++, b++)
	{
		if (!b->model || b->endtime < cl.time)
		{
			b->entity = ent;
			b->model = m;
			b->endtime = cl.time + 0.2;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			return;
		}
	}
	Con_Printf ("beam list overflow!\n");
}

void CL_ParseTempEntity (void)
{
	int type;
	vec3_t pos;
	vec3_t dir;
	vec3_t pos2;
	vec3_t color;
	int rnd;
	int colorStart, colorLength, count;
	float velspeed, radius;
	qbyte *tempcolor;

	type = MSG_ReadByte ();
	switch (type)
	{
	case TE_WIZSPIKE:
		// spike hitting wall
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		CL_RunParticleEffect (pos, vec3_origin, 20, 30);
		S_StartSound (-1, 0, cl_sfx_wizhit, pos, 1, 1);
		break;

	case TE_KNIGHTSPIKE:
		// spike hitting wall
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		CL_RunParticleEffect (pos, vec3_origin, 226, 20);
		S_StartSound (-1, 0, cl_sfx_knighthit, pos, 1, 1);
		break;

	case TE_SPIKE:
		// spike hitting wall
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		// LordHavoc: changed to spark shower
		CL_SparkShower(pos, vec3_origin, 15);
		if ( rand() % 5 )
			S_StartSound (-1, 0, cl_sfx_tink1, pos, 1, 1);
		else
		{
			rnd = rand() & 3;
			if (rnd == 1)
				S_StartSound (-1, 0, cl_sfx_ric1, pos, 1, 1);
			else if (rnd == 2)
				S_StartSound (-1, 0, cl_sfx_ric2, pos, 1, 1);
			else
				S_StartSound (-1, 0, cl_sfx_ric3, pos, 1, 1);
		}
		break;
	case TE_SPIKEQUAD:
		// quad spike hitting wall
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		// LordHavoc: changed to spark shower
		CL_SparkShower(pos, vec3_origin, 15);
		CL_AllocDlight (NULL, pos, 200, 0.1f, 0.1f, 1.0f, 1000, 0.2);
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		if ( rand() % 5 )
			S_StartSound (-1, 0, cl_sfx_tink1, pos, 1, 1);
		else
		{
			rnd = rand() & 3;
			if (rnd == 1)
				S_StartSound (-1, 0, cl_sfx_ric1, pos, 1, 1);
			else if (rnd == 2)
				S_StartSound (-1, 0, cl_sfx_ric2, pos, 1, 1);
			else
				S_StartSound (-1, 0, cl_sfx_ric3, pos, 1, 1);
		}
		break;
	case TE_SUPERSPIKE:
		// super spike hitting wall
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		// LordHavoc: changed to dust shower
		CL_SparkShower(pos, vec3_origin, 30);
		if ( rand() % 5 )
			S_StartSound (-1, 0, cl_sfx_tink1, pos, 1, 1);
		else
		{
			rnd = rand() & 3;
			if (rnd == 1)
				S_StartSound (-1, 0, cl_sfx_ric1, pos, 1, 1);
			else if (rnd == 2)
				S_StartSound (-1, 0, cl_sfx_ric2, pos, 1, 1);
			else
				S_StartSound (-1, 0, cl_sfx_ric3, pos, 1, 1);
		}
		break;
	case TE_SUPERSPIKEQUAD:
		// quad super spike hitting wall
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		// LordHavoc: changed to dust shower
		CL_SparkShower(pos, vec3_origin, 30);
		CL_AllocDlight (NULL, pos, 200, 0.1f, 0.1f, 1.0f, 1000, 0.2);
		if ( rand() % 5 )
			S_StartSound (-1, 0, cl_sfx_tink1, pos, 1, 1);
		else
		{
			rnd = rand() & 3;
			if (rnd == 1)
				S_StartSound (-1, 0, cl_sfx_ric1, pos, 1, 1);
			else if (rnd == 2)
				S_StartSound (-1, 0, cl_sfx_ric2, pos, 1, 1);
			else
				S_StartSound (-1, 0, cl_sfx_ric3, pos, 1, 1);
		}
		break;
		// LordHavoc: added for improved blood splatters
	case TE_BLOOD:
		// blood puff
		MSG_ReadVector(pos);
		dir[0] = MSG_ReadChar ();
		dir[1] = MSG_ReadChar ();
		dir[2] = MSG_ReadChar ();
		count = MSG_ReadByte ();
		CL_BloodPuff(pos, dir, count);
		break;
	case TE_BLOOD2:
		// blood puff
		MSG_ReadVector(pos);
		CL_BloodPuff(pos, vec3_origin, 10);
		break;
	case TE_SPARK:
		// spark shower
		MSG_ReadVector(pos);
		dir[0] = MSG_ReadChar ();
		dir[1] = MSG_ReadChar ();
		dir[2] = MSG_ReadChar ();
		count = MSG_ReadByte ();
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		CL_SparkShower(pos, dir, count);
		break;
	case TE_PLASMABURN:
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		CL_AllocDlight (NULL, pos, 200, 1, 1, 1, 1000, 0.2);
		CL_PlasmaBurn(pos);
		break;
		// LordHavoc: added for improved gore
	case TE_BLOODSHOWER:
		// vaporized body
		MSG_ReadVector(pos); // mins
		MSG_ReadVector(pos2); // maxs
		velspeed = MSG_ReadCoord (); // speed
		count = MSG_ReadShort (); // number of particles
		CL_BloodShower(pos, pos2, velspeed, count);
		break;
	case TE_PARTICLECUBE:
		// general purpose particle effect
		MSG_ReadVector(pos); // mins
		MSG_ReadVector(pos2); // maxs
		MSG_ReadVector(dir); // dir
		count = MSG_ReadShort (); // number of particles
		colorStart = MSG_ReadByte (); // color
		colorLength = MSG_ReadByte (); // gravity (1 or 0)
		velspeed = MSG_ReadCoord (); // randomvel
		CL_ParticleCube(pos, pos2, dir, count, colorStart, colorLength, velspeed);
		break;

	case TE_PARTICLERAIN:
		// general purpose particle effect
		MSG_ReadVector(pos); // mins
		MSG_ReadVector(pos2); // maxs
		MSG_ReadVector(dir); // dir
		count = MSG_ReadShort (); // number of particles
		colorStart = MSG_ReadByte (); // color
		CL_ParticleRain(pos, pos2, dir, count, colorStart, 0);
		break;

	case TE_PARTICLESNOW:
		// general purpose particle effect
		MSG_ReadVector(pos); // mins
		MSG_ReadVector(pos2); // maxs
		MSG_ReadVector(dir); // dir
		count = MSG_ReadShort (); // number of particles
		colorStart = MSG_ReadByte (); // color
		CL_ParticleRain(pos, pos2, dir, count, colorStart, 1);
		break;

	case TE_GUNSHOT:
		// bullet hitting wall
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		// LordHavoc: changed to dust shower
		CL_SparkShower(pos, vec3_origin, 15);
		break;

	case TE_GUNSHOTQUAD:
		// quad bullet hitting wall
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		CL_SparkShower(pos, vec3_origin, 15);
		CL_AllocDlight (NULL, pos, 200, 0.1f, 0.1f, 1.0f, 1000, 0.2);
		break;

	case TE_EXPLOSION:
		// rocket explosion
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		CL_ParticleExplosion (pos);
		// LordHavoc: boosted color from 1.0, 0.8, 0.4 to 1.25, 1.0, 0.5
		CL_AllocDlight (NULL, pos, 350, 1.25f, 1.0f, 0.5f, 700, 0.5);
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_EXPLOSIONQUAD:
		// quad rocket explosion
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		CL_ParticleExplosion (pos);
		CL_AllocDlight (NULL, pos, 600, 0.5f, 0.4f, 1.0f, 1200, 0.5);
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_EXPLOSION3:
		// Nehahra movie colored lighting explosion
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		CL_ParticleExplosion (pos);
		CL_AllocDlight (NULL, pos, 350, MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord(), 700, 0.5);
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_EXPLOSIONRGB:
		// colored lighting explosion
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		CL_ParticleExplosion (pos);
		color[0] = MSG_ReadByte() * (1.0 / 255.0);
		color[1] = MSG_ReadByte() * (1.0 / 255.0);
		color[2] = MSG_ReadByte() * (1.0 / 255.0);
		CL_AllocDlight (NULL, pos, 350, color[0], color[1], color[2], 700, 0.5);
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_TAREXPLOSION:
		// tarbaby explosion
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		CL_BlobExplosion (pos);

		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		CL_AllocDlight (NULL, pos, 600, 0.8f, 0.4f, 1.0f, 1200, 0.5);
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_SMALLFLASH:
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		CL_AllocDlight (NULL, pos, 200, 1, 1, 1, 1000, 0.2);
		break;

	case TE_CUSTOMFLASH:
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		radius = MSG_ReadByte() * 8;
		velspeed = (MSG_ReadByte() + 1) * (1.0 / 256.0);
		color[0] = MSG_ReadByte() * (1.0 / 255.0);
		color[1] = MSG_ReadByte() * (1.0 / 255.0);
		color[2] = MSG_ReadByte() * (1.0 / 255.0);
		CL_AllocDlight (NULL, pos, radius, color[0], color[1], color[2], radius / velspeed, velspeed);
		break;

	case TE_FLAMEJET:
		MSG_ReadVector(pos);
		MSG_ReadVector(dir);
		count = MSG_ReadByte();
		CL_Flames(pos, dir, count);
		break;

	case TE_LIGHTNING1:
		// lightning bolts
		if (!cl_model_bolt)
			cl_model_bolt = Mod_ForName("progs/bolt.mdl", true, false, false);
		CL_ParseBeam (cl_model_bolt);
		break;

	case TE_LIGHTNING2:
		// lightning bolts
		if (!cl_model_bolt2)
			cl_model_bolt2 = Mod_ForName("progs/bolt2.mdl", true, false, false);
		CL_ParseBeam (cl_model_bolt2);
		break;

	case TE_LIGHTNING3:
		// lightning bolts
		if (!cl_model_bolt3)
			cl_model_bolt3 = Mod_ForName("progs/bolt3.mdl", true, false, false);
		CL_ParseBeam (cl_model_bolt3);
		break;

// PGM 01/21/97
	case TE_BEAM:
		// grappling hook beam
		if (!cl_model_beam)
			cl_model_beam = Mod_ForName("progs/beam.mdl", true, false, false);
		CL_ParseBeam (cl_model_beam);
		break;
// PGM 01/21/97

// LordHavoc: for compatibility with the Nehahra movie...
	case TE_LIGHTNING4NEH:
		CL_ParseBeam (Mod_ForName(MSG_ReadString(), true, false, false));
		break;

	case TE_LAVASPLASH:
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();
		CL_LavaSplash (pos);
		break;

	case TE_TELEPORT:
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();
		CL_AllocDlight (NULL, pos, 1000, 1.25f, 1.25f, 1.25f, 3000, 99.0f);
//		CL_TeleportSplash (pos);
		break;

	case TE_EXPLOSION2:
		// color mapped explosion
		MSG_ReadVector(pos);
		Mod_FindNonSolidLocation(pos, cl.worldmodel);
		colorStart = MSG_ReadByte ();
		colorLength = MSG_ReadByte ();
		CL_ParticleExplosion2 (pos, colorStart, colorLength);
		tempcolor = (qbyte *)&d_8to24table[(rand()%colorLength) + colorStart];
		CL_AllocDlight (NULL, pos, 350, tempcolor[0] * (1.0f / 255.0f), tempcolor[1] * (1.0f / 255.0f), tempcolor[2] * (1.0f / 255.0f), 700, 0.5);
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	default:
		Host_Error ("CL_ParseTempEntity: bad type %d", type);
	}
}

#define SHOWNET(x) if(cl_shownet.integer==2)Con_Printf ("%3i:%s\n", msg_readcount-1, x);

static qbyte cgamenetbuffer[65536];

/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage (void)
{
	int			cmd;
	int			i, entitiesupdated;
	qbyte		cmdlog[32];
	char		*cmdlogname[32], *temp;
	int			cmdindex, cmdcount = 0;

//
// if recording demos, copy the message out
//
	if (cl_shownet.integer == 1)
		Con_Printf ("%i ",net_message.cursize);
	else if (cl_shownet.integer == 2)
		Con_Printf ("------------------\n");

	cl.onground = false;	// unless the server says otherwise
//
// parse the message
//
	MSG_BeginReading ();

	entitiesupdated = false;

	while (1)
	{
		if (msg_badread)
			Host_Error ("CL_ParseServerMessage: Bad server message");

		cmd = MSG_ReadByte ();

		if (cmd == -1)
		{
			SHOWNET("END OF MESSAGE");
			break;		// end of message
		}

		cmdindex = cmdcount & 31;
		cmdcount++;
		cmdlog[cmdindex] = cmd;

		// if the high bit of the command byte is set, it is a fast update
		if (cmd & 128)
		{
			// LordHavoc: fix for bizarre problem in MSVC that I do not understand (if I assign the string pointer directly it ends up storing a NULL pointer)
			temp = "entity";
			cmdlogname[cmdindex] = temp;
			SHOWNET("fast update");
			if (cls.signon == SIGNONS - 1)
			{
				// first update is the final signon stage
				cls.signon = SIGNONS;
				CL_SignonReply ();
			}
			CL_ParseUpdate (cmd&127);
			continue;
		}

		SHOWNET(svc_strings[cmd]);
		cmdlogname[cmdindex] = svc_strings[cmd];
		if (!cmdlogname[cmdindex])
		{
			// LordHavoc: fix for bizarre problem in MSVC that I do not understand (if I assign the string pointer directly it ends up storing a NULL pointer)
			temp = "<unknown>";
			cmdlogname[cmdindex] = temp;
		}

		// other commands
		switch (cmd)
		{
		default:
			{
				char description[32*64], temp[64];
				int count;
				strcpy(description, "packet dump: ");
				i = cmdcount - 32;
				if (i < 0)
					i = 0;
				count = cmdcount - i;
				i &= 31;
				while(count > 0)
				{
					sprintf(temp, "%3i:%s ", cmdlog[i], cmdlogname[i]);
					strcat(description, temp);
					count--;
					i++;
					i &= 31;
				}
				description[strlen(description)-1] = '\n'; // replace the last space with a newline
				Con_Printf("%s", description);
				Host_Error ("CL_ParseServerMessage: Illegible server message\n");
			}
			break;

		case svc_nop:
			break;

		case svc_time:
			if (!entitiesupdated)
			{
				// this is a new frame, we'll be seeing entities,
				// so prepare for entity updates
				CL_EntityUpdateSetup();
				entitiesupdated = true;
			}
			cl.mtime[1] = cl.mtime[0];
			cl.mtime[0] = MSG_ReadFloat ();
			break;

		case svc_clientdata:
			i = MSG_ReadShort ();
			CL_ParseClientdata (i);
			break;

		case svc_version:
			i = MSG_ReadLong ();
			if (i != PROTOCOL_VERSION && i != DPPROTOCOL_VERSION1 && i != DPPROTOCOL_VERSION2 && i != DPPROTOCOL_VERSION3 && i != 250)
				Host_Error ("CL_ParseServerMessage: Server is protocol %i, not %i, %i, %i or %i", i, DPPROTOCOL_VERSION1, DPPROTOCOL_VERSION2, DPPROTOCOL_VERSION3, PROTOCOL_VERSION);
			Nehahrademcompatibility = false;
			if (i == 250)
				Nehahrademcompatibility = true;
			if (cls.demoplayback && demo_nehahra.integer)
				Nehahrademcompatibility = true;
			dpprotocol = i;
			if (dpprotocol != DPPROTOCOL_VERSION1 && dpprotocol != DPPROTOCOL_VERSION2 && dpprotocol != DPPROTOCOL_VERSION3)
				dpprotocol = 0;
			break;

		case svc_disconnect:
			Host_EndGame ("Server disconnected\n");

		case svc_print:
			Con_Printf ("%s", MSG_ReadString ());
			break;

		case svc_centerprint:
			SCR_CenterPrint (MSG_ReadString ());
			break;

		case svc_stufftext:
			Cbuf_AddText (MSG_ReadString ());
			break;

		case svc_damage:
			V_ParseDamage ();
			break;

		case svc_serverinfo:
			CL_ParseServerInfo ();
			break;

		case svc_setangle:
			for (i=0 ; i<3 ; i++)
				cl.viewangles[i] = MSG_ReadAngle ();
			break;

		case svc_setview:
			cl.viewentity = MSG_ReadShort ();
			// LordHavoc: assume first setview recieved is the real player entity
			if (!cl.playerentity)
				cl.playerentity = cl.viewentity;
			break;

		case svc_lightstyle:
			i = MSG_ReadByte ();
			if (i >= MAX_LIGHTSTYLES)
				Host_Error ("svc_lightstyle >= MAX_LIGHTSTYLES");
			strncpy (cl_lightstyle[i].map,  MSG_ReadString(), MAX_STYLESTRING - 1);
			cl_lightstyle[i].map[MAX_STYLESTRING - 1] = 0;
			cl_lightstyle[i].length = strlen(cl_lightstyle[i].map);
			break;

		case svc_sound:
			CL_ParseStartSoundPacket(false);
			break;

		case svc_sound2:
			CL_ParseStartSoundPacket(true);
			break;

		case svc_stopsound:
			i = MSG_ReadShort();
			S_StopSound(i>>3, i&7);
			break;

		case svc_updatename:
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatename >= cl.maxclients");
			strcpy (cl.scores[i].name, MSG_ReadString ());
			break;

		case svc_updatefrags:
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatefrags >= cl.maxclients");
			cl.scores[i].frags = MSG_ReadShort ();
			break;

		case svc_updatecolors:
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatecolors >= cl.maxclients");
			cl.scores[i].colors = MSG_ReadByte ();
			// update our color cvar if our color changed
			if (i == cl.playerentity - 1)
				Cvar_SetValue ("_cl_color", cl.scores[i].colors);
			break;

		case svc_particle:
			CL_ParseParticleEffect ();
			break;

		case svc_effect:
			CL_ParseEffect ();
			break;

		case svc_effect2:
			CL_ParseEffect2 ();
			break;

		case svc_spawnbaseline:
			i = MSG_ReadShort ();
			if (i < 0 || i >= MAX_EDICTS)
				Host_Error ("CL_ParseServerMessage: svc_spawnbaseline: invalid entity number %i", i);
			CL_ParseBaseline (cl_entities + i, false);
			break;
		case svc_spawnbaseline2:
			i = MSG_ReadShort ();
			if (i < 0 || i >= MAX_EDICTS)
				Host_Error ("CL_ParseServerMessage: svc_spawnbaseline2: invalid entity number %i", i);
			CL_ParseBaseline (cl_entities + i, true);
			break;
		case svc_spawnstatic:
			CL_ParseStatic (false);
			break;
		case svc_spawnstatic2:
			CL_ParseStatic (true);
			break;
		case svc_temp_entity:
			CL_ParseTempEntity ();
			break;

		case svc_setpause:
			cl.paused = MSG_ReadByte ();
			if (cl.paused)
				CDAudio_Pause ();
			else
				CDAudio_Resume ();
			break;

		case svc_signonnum:
			i = MSG_ReadByte ();
			if (i <= cls.signon)
				Host_Error ("Received signon %i when at %i", i, cls.signon);
			cls.signon = i;
			CL_SignonReply ();
			break;

		case svc_killedmonster:
			cl.stats[STAT_MONSTERS]++;
			break;

		case svc_foundsecret:
			cl.stats[STAT_SECRETS]++;
			break;

		case svc_updatestat:
			i = MSG_ReadByte ();
			if (i < 0 || i >= MAX_CL_STATS)
				Host_Error ("svc_updatestat: %i is invalid", i);
			cl.stats[i] = MSG_ReadLong ();
			break;

		case svc_spawnstaticsound:
			CL_ParseStaticSound (false);
			break;

		case svc_spawnstaticsound2:
			CL_ParseStaticSound (true);
			break;

		case svc_cdtrack:
			cl.cdtrack = MSG_ReadByte ();
			cl.looptrack = MSG_ReadByte ();
			if ( (cls.demoplayback || cls.demorecording) && (cls.forcetrack != -1) )
				CDAudio_Play ((qbyte)cls.forcetrack, true);
			else
				CDAudio_Play ((qbyte)cl.cdtrack, true);
			break;

		case svc_intermission:
			cl.intermission = 1;
			cl.completed_time = cl.time;
			break;

		case svc_finale:
			cl.intermission = 2;
			cl.completed_time = cl.time;
			SCR_CenterPrint (MSG_ReadString ());
			break;

		case svc_cutscene:
			cl.intermission = 3;
			cl.completed_time = cl.time;
			SCR_CenterPrint (MSG_ReadString ());
			break;

		case svc_sellscreen:
			Cmd_ExecuteString ("help", src_command);
			break;
		case svc_hidelmp:
			SHOWLMP_decodehide();
			break;
		case svc_showlmp:
			SHOWLMP_decodeshow();
			break;
		case svc_skybox:
			R_SetSkyBox(MSG_ReadString());
			break;
		case svc_cgame:
			{
				int length;
				length = (int) ((unsigned short) MSG_ReadShort());
				for (i = 0;i < length;i++)
					cgamenetbuffer[i] = MSG_ReadByte();
				if (!msg_badread)
					CL_CGVM_ParseNetwork(cgamenetbuffer, length);
			}
			break;
		case svc_entities:
			if (cls.signon == SIGNONS - 1)
			{
				// first update is the final signon stage
				cls.signon = SIGNONS;
				CL_SignonReply ();
			}
			CL_ReadEntityFrame();
			break;
		}
	}

	if (entitiesupdated)
		CL_EntityUpdateEnd();
}

void CL_Parse_Init(void)
{
	// LordHavoc: added demo_nehahra cvar
	Cvar_RegisterVariable (&demo_nehahra);
	if (gamemode == GAME_NEHAHRA)
		Cvar_SetValue("demo_nehahra", 1);
}
