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
// common.c -- misc functions used in client and server

#include <stdlib.h>
#include <fcntl.h>
#ifndef WIN32
#include <unistd.h>
#endif

#include "quakedef.h"

cvar_t registered = {0, "registered","0"};
cvar_t cmdline = {0, "cmdline","0"};

extern qboolean fs_modified;   // set true if using non-id files

char com_token[1024];
int com_argc;
const char **com_argv;

// LordHavoc: made commandline 1024 characters instead of 256
#define CMDLINE_LENGTH	1024
char com_cmdline[CMDLINE_LENGTH];

int gamemode;
const char *gamename;
const char *gamedirname;
const char *gamescreenshotname;
char com_modname[MAX_OSPATH] = "";


/*
============================================================================

					BYTE ORDER FUNCTIONS

============================================================================
*/

short   ShortSwap (short l)
{
	qbyte    b1,b2;

	b1 = l&255;
	b2 = (l>>8)&255;

	return (b1<<8) + b2;
}

int    LongSwap (int l)
{
	qbyte    b1,b2,b3,b4;

	b1 = l&255;
	b2 = (l>>8)&255;
	b3 = (l>>16)&255;
	b4 = (l>>24)&255;

	return ((int)b1<<24) + ((int)b2<<16) + ((int)b3<<8) + b4;
}

float FloatSwap (float f)
{
	union
	{
		float   f;
		qbyte    b[4];
	} dat1, dat2;


	dat1.f = f;
	dat2.b[0] = dat1.b[3];
	dat2.b[1] = dat1.b[2];
	dat2.b[2] = dat1.b[1];
	dat2.b[3] = dat1.b[0];
	return dat2.f;
}


// Extract integers from buffers

unsigned int BuffBigLong (const qbyte *buffer)
{
	return (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
}

unsigned short BuffBigShort (const qbyte *buffer)
{
	return (buffer[0] << 8) | buffer[1];
}

unsigned int BuffLittleLong (const qbyte *buffer)
{
	return (buffer[3] << 24) | (buffer[2] << 16) | (buffer[1] << 8) | buffer[0];
}

unsigned short BuffLittleShort (const qbyte *buffer)
{
	return (buffer[1] << 8) | buffer[0];
}


/*
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

//
// writing functions
//

void MSG_WriteChar (sizebuf_t *sb, int c)
{
	qbyte    *buf;

	buf = SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteByte (sizebuf_t *sb, int c)
{
	qbyte    *buf;

	buf = SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteShort (sizebuf_t *sb, int c)
{
	qbyte    *buf;

	buf = SZ_GetSpace (sb, 2);
	buf[0] = c&0xff;
	buf[1] = c>>8;
}

void MSG_WriteLong (sizebuf_t *sb, int c)
{
	qbyte    *buf;

	buf = SZ_GetSpace (sb, 4);
	buf[0] = c&0xff;
	buf[1] = (c>>8)&0xff;
	buf[2] = (c>>16)&0xff;
	buf[3] = c>>24;
}

void MSG_WriteFloat (sizebuf_t *sb, float f)
{
	union
	{
		float   f;
		int     l;
	} dat;


	dat.f = f;
	dat.l = LittleLong (dat.l);

	SZ_Write (sb, &dat.l, 4);
}

void MSG_WriteString (sizebuf_t *sb, const char *s)
{
	if (!s)
		SZ_Write (sb, "", 1);
	else
		SZ_Write (sb, s, strlen(s)+1);
}

// used by server (always latest PROTOCOL_DARKPLACES)
void MSG_WriteDPCoord (sizebuf_t *sb, float f)
{
	if (f >= 0)
		MSG_WriteShort (sb, (int)(f + 0.5f));
	else
		MSG_WriteShort (sb, (int)(f - 0.5f));
}

void MSG_WritePreciseAngle (sizebuf_t *sb, float f)
{
	if (f >= 0)
		MSG_WriteShort (sb, (int)(f*(65536.0f/360.0f) + 0.5f) & 65535);
	else
		MSG_WriteShort (sb, (int)(f*(65536.0f/360.0f) - 0.5f) & 65535);
}

// LordHavoc: round to nearest value, rather than rounding toward zero, fixes crosshair problem
void MSG_WriteAngle (sizebuf_t *sb, float f)
{
	if (f >= 0)
		MSG_WriteByte (sb, (int)(f*(256.0f/360.0f) + 0.5f) & 255);
	else
		MSG_WriteByte (sb, (int)(f*(256.0f/360.0f) - 0.5f) & 255);
}

//
// reading functions
//
int msg_readcount;
qboolean msg_badread;

void MSG_BeginReading (void)
{
	msg_readcount = 0;
	msg_badread = false;
}

int MSG_ReadLittleShort (void)
{
	if (msg_readcount+2 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}
	msg_readcount += 2;
	return (short)(net_message.data[msg_readcount-2] | (net_message.data[msg_readcount-1]<<8));
}

int MSG_ReadBigShort (void)
{
	if (msg_readcount+2 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}
	msg_readcount += 2;
	return (short)((net_message.data[msg_readcount-2]<<8) + net_message.data[msg_readcount-1]);
}

int MSG_ReadLittleLong (void)
{
	if (msg_readcount+4 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}
	msg_readcount += 4;
	return net_message.data[msg_readcount-4] | (net_message.data[msg_readcount-3]<<8) | (net_message.data[msg_readcount-2]<<16) | (net_message.data[msg_readcount-1]<<24);
}

int MSG_ReadBigLong (void)
{
	if (msg_readcount+4 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}
	msg_readcount += 4;
	return (net_message.data[msg_readcount-4]<<24) + (net_message.data[msg_readcount-3]<<16) + (net_message.data[msg_readcount-2]<<8) + net_message.data[msg_readcount-1];
}

float MSG_ReadLittleFloat (void)
{
	union
	{
		float f;
		int l;
	} dat;
	if (msg_readcount+4 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}
	msg_readcount += 4;
	dat.l = net_message.data[msg_readcount-4] | (net_message.data[msg_readcount-3]<<8) | (net_message.data[msg_readcount-2]<<16) | (net_message.data[msg_readcount-1]<<24);
	return dat.f;
}

float MSG_ReadBigFloat (void)
{
	union
	{
		float f;
		int l;
	} dat;
	if (msg_readcount+4 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}
	msg_readcount += 4;
	dat.l = (net_message.data[msg_readcount-4]<<24) | (net_message.data[msg_readcount-3]<<16) | (net_message.data[msg_readcount-2]<<8) | net_message.data[msg_readcount-1];
	return dat.f;
}

char *MSG_ReadString (void)
{
	static char string[2048];
	int l,c;
	for (l = 0;l < (int) sizeof(string) - 1 && (c = MSG_ReadChar()) != -1 && c != 0;l++)
		string[l] = c;
	string[l] = 0;
	return string;
}

int MSG_ReadBytes (int numbytes, unsigned char *out)
{
	int l, c;
	for (l = 0;l < numbytes && (c = MSG_ReadChar()) != -1;l++)
		out[l] = c;
	return l;
}

// used by client
float MSG_ReadCoord (void)
{
	if (cl.protocol == PROTOCOL_DARKPLACES2 || cl.protocol == PROTOCOL_DARKPLACES3 || cl.protocol == PROTOCOL_DARKPLACES4 || cl.protocol == PROTOCOL_DARKPLACES5)
		return (signed short) MSG_ReadLittleShort();
	else if (cl.protocol == PROTOCOL_DARKPLACES1)
		return MSG_ReadLittleFloat();
	else
		return MSG_ReadLittleShort() * (1.0f/8.0f);
}


//===========================================================================

void SZ_Alloc (sizebuf_t *buf, int startsize, const char *name)
{
	if (startsize < 256)
		startsize = 256;
	buf->mempool = Mem_AllocPool(name, 0, NULL);
	buf->data = Mem_Alloc(buf->mempool, startsize);
	buf->maxsize = startsize;
	buf->cursize = 0;
}


void SZ_Free (sizebuf_t *buf)
{
	Mem_FreePool(&buf->mempool);
	buf->data = NULL;
	buf->maxsize = 0;
	buf->cursize = 0;
}

void SZ_Clear (sizebuf_t *buf)
{
	buf->cursize = 0;
}

void *SZ_GetSpace (sizebuf_t *buf, int length)
{
	void *data;

	if (buf->cursize + length > buf->maxsize)
	{
		if (!buf->allowoverflow)
			Host_Error ("SZ_GetSpace: overflow without allowoverflow set\n");

		if (length > buf->maxsize)
			Host_Error ("SZ_GetSpace: %i is > full buffer size\n", length);

		buf->overflowed = true;
		Con_Print("SZ_GetSpace: overflow\n");
		SZ_Clear (buf);
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;

	return data;
}

void SZ_Write (sizebuf_t *buf, const void *data, int length)
{
	memcpy (SZ_GetSpace(buf,length),data,length);
}

// LordHavoc: thanks to Fuh for bringing the pure evil of SZ_Print to my
// attention, it has been eradicated from here, its only (former) use in
// all of darkplaces.

static char *hexchar = "0123456789ABCDEF";
void Com_HexDumpToConsole(const qbyte *data, int size)
{
	int i, j, n;
	char text[1024];
	char *cur, *flushpointer;
	const qbyte *d;
	cur = text;
	flushpointer = text + 512;
	for (i = 0;i < size;)
	{
		n = 16;
		if (n > size - i)
			n = size - i;
		d = data + i;
		// print offset
		*cur++ = hexchar[(i >> 12) & 15];
		*cur++ = hexchar[(i >>  8) & 15];
		*cur++ = hexchar[(i >>  4) & 15];
		*cur++ = hexchar[(i >>  0) & 15];
		*cur++ = ':';
		// print hex
		for (j = 0;j < 16;j++)
		{
			if (j < n)
			{
				*cur++ = hexchar[(d[j] >> 4) & 15];
				*cur++ = hexchar[(d[j] >> 0) & 15];
			}
			else
			{
				*cur++ = ' ';
				*cur++ = ' ';
			}
			if ((j & 3) == 0)
				*cur++ = ' ';
		}
		// print text
		for (j = 0;j < 16;j++)
		{
			if (j < n)
			{
				if (d[j] >= ' ' && d[j] <= 127)
					*cur++ = d[j];
				else
					*cur++ = '.';
			}
			else
				*cur++ = ' ';
		}
		*cur++ = '\n';
		i += n;
		if (cur >= flushpointer || i >= size)
		{
			*cur++ = 0;
			Con_Print(text);
			cur = text;
		}
	}
}

void SZ_HexDumpToConsole(const sizebuf_t *buf)
{
	Com_HexDumpToConsole(buf->data, buf->cursize);
}


//============================================================================


/*
==============
COM_ParseToken

Parse a token out of a string
==============
*/
int COM_ParseToken(const char **datapointer, int returnnewline)
{
	int len;
	const char *data = *datapointer;

	len = 0;
	com_token[0] = 0;

	if (!data)
	{
		*datapointer = NULL;
		return false;
	}

// skip whitespace
skipwhite:
	for (;*data <= ' ' && (*data != '\n' || !returnnewline);data++)
	{
		if (*data == 0)
		{
			// end of file
			*datapointer = NULL;
			return false;
		}
	}

	if (data[0] == '/' && data[1] == '/')
	{
		// comment
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}
	else if (data[0] == '/' && data[1] == '*')
	{
		// comment
		data++;
		while (*data && (data[0] != '*' || data[1] != '/'))
			data++;
		data += 2;
		goto skipwhite;
	}
	else if (*data == '\"')
	{
		// quoted string
		for (data++;*data != '\"';data++)
		{
			if (!*data || len >= (int)sizeof(com_token) - 1)
			{
				com_token[0] = 0;
				*datapointer = NULL;
				return false;
			}
			com_token[len++] = *data;
		}
		com_token[len] = 0;
		*datapointer = data+1;
		return true;
	}
	else if (*data == '\n' || *data == '{' || *data == '}' || *data == ')' || *data == '(' || *data == ']' || *data == '[' || *data == '\'' || *data == ':' || *data == ',' || *data == ';')
	{
		// single character
		com_token[len++] = *data++;
		com_token[len] = 0;
		*datapointer = data;
		return true;
	}
	else
	{
		// regular word
		for (;*data > ' ' && *data != '{' && *data != '}' && *data != ')' && *data != '(' && *data != ']' && *data != '[' && *data != '\'' && *data != ':' && *data != ',' && *data != ';';data++)
		{
			if (len >= (int)sizeof(com_token) - 1)
			{
				com_token[0] = 0;
				*datapointer = NULL;
				return false;
			}
			com_token[len++] = *data;
		}
		com_token[len] = 0;
		*datapointer = data;
		return true;
	}
}

/*
==============
COM_ParseTokenConsole

Parse a token out of a string, behaving like the qwcl console
==============
*/
int COM_ParseTokenConsole(const char **datapointer)
{
	int len;
	const char *data = *datapointer;

	len = 0;
	com_token[0] = 0;

	if (!data)
	{
		*datapointer = NULL;
		return false;
	}

// skip whitespace
skipwhite:
	for (;*data <= ' ';data++)
	{
		if (*data == 0)
		{
			// end of file
			*datapointer = NULL;
			return false;
		}
	}

	if (*data == '/' && data[1] == '/')
	{
		// comment
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}
	else if (*data == '\"')
	{
		// quoted string
		for (data++;*data != '\"';data++)
		{
			if (!*data || len >= (int)sizeof(com_token) - 1)
			{
				com_token[0] = 0;
				*datapointer = NULL;
				return false;
			}
			com_token[len++] = *data;
		}
		com_token[len] = 0;
		*datapointer = data+1;
		return true;
	}
	else
	{
		// regular word
		for (;*data > ' ';data++)
		{
			if (len >= (int)sizeof(com_token) - 1)
			{
				com_token[0] = 0;
				*datapointer = NULL;
				return false;
			}
			com_token[len++] = *data;
		}
		com_token[len] = 0;
		*datapointer = data;
		return true;
	}
}


/*
================
COM_CheckParm

Returns the position (1 to argc-1) in the program's argument list
where the given parameter apears, or 0 if not present
================
*/
int COM_CheckParm (const char *parm)
{
	int i;

	for (i=1 ; i<com_argc ; i++)
	{
		if (!com_argv[i])
			continue;               // NEXTSTEP sometimes clears appkit vars.
		if (!strcmp (parm,com_argv[i]))
			return i;
	}

	return 0;
}

/*
================
COM_CheckRegistered

Looks for the pop.txt file and verifies it.
Sets the "registered" cvar.
Immediately exits out if an alternate game was attempted to be started without
being registered.
================
*/
void COM_CheckRegistered (void)
{
	Cvar_Set ("cmdline", com_cmdline);

	if (!FS_FileExists("gfx/pop.lmp"))
	{
		if (fs_modified)
			Con_Print("Playing shareware version, with modification.\nwarning: most mods require full quake data.\n");
		else
			Con_Print("Playing shareware version.\n");
		return;
	}

	Cvar_Set ("registered", "1");
	Con_Print("Playing registered version.\n");
}


/*
================
COM_InitArgv
================
*/
void COM_InitArgv (void)
{
	int i, j, n;
	// reconstitute the command line for the cmdline externally visible cvar
	n = 0;
	for (j = 0;(j < MAX_NUM_ARGVS) && (j < com_argc);j++)
	{
		i = 0;
		while ((n < (CMDLINE_LENGTH - 1)) && com_argv[j][i])
			com_cmdline[n++] = com_argv[j][i++];
		if (n < (CMDLINE_LENGTH - 1))
			com_cmdline[n++] = ' ';
		else
			break;
	}
	com_cmdline[n] = 0;
}


//===========================================================================

// Game mods

typedef struct
{
	const char* prog_name;
	const char* cmdline;
	const char* gamename;
	const char* gamedirname;
	const char* gamescreenshotname;
} gamemode_info_t;

static const gamemode_info_t gamemode_info [] =
{// prog_name		cmdline			gamename				gamedirname	gamescreenshotname

// GAME_NORMAL
{ "",				"-quake",		"DarkPlaces-Quake",		"",			"dp" },
// GAME_HIPNOTIC
{ "hipnotic",		"-hipnotic",	"Darkplaces-Hipnotic",	"hipnotic",	"dp" },
// GAME_ROGUE
{ "rogue",			"-rogue",		"Darkplaces-Rogue",		"rogue",	"dp" },
// GAME_NEHAHRA
{ "nehahra",		"-nehahra",		"DarkPlaces-Nehahra",	"nehahra",	"dp" },
// GAME_NEXUIZ
{ "nexuiz",			"-nexuiz",		"Nexuiz",				"data",		"nexuiz" },
// GAME_TRANSFUSION
{ "transfusion",	"-transfusion",	"Transfusion",			"basetf",	"transfusion" },
// GAME_GOODVSBAD2
{ "gvb2",			"-goodvsbad2",	"GoodVs.Bad2",			"rts",		"gvb2" },
// GAME_TEU
{ "teu",			"-teu",			"TheEvilUnleashed",		"baseteu",	"teu" },
// GAME_BATTLEMECH
{ "battlemech",		"-battlemech",	"Battlemech",			"base",		"battlemech" },
// GAME_ZYMOTIC
{ "zymotic",		"-zymotic",		"Zymotic",				"data",		"zymotic" },
// GAME_FNIGGIUM
{ "fniggium",		"-fniggium",	"Fniggium",				"data",		"fniggium" },
// GAME_SETHERAL
{ "setheral",		"-setheral",	"Setheral",				"data",		"setheral" },
// GAME_SOM
{ "som",			"-som",			"Son of Man",			"sonofman",	"som" },
// GAME_TENEBRAE
{ "tenebrae",		"-tenebrae",	"DarkPlaces-Tenebrae",	"tenebrae",	"dp" },
// GAME_NEOTERIC
{ "neoteric",		"-neoteric",	"Neoteric",				"neobase",	"neo" },
// GAME_OPENQUARTZ
{ "openquartz",		"-openquartz",	"OpenQuartz",			"id1",		"openquartz"},
// GAME_PRYDON
{ "prydon",			"-prydon",		"PrydonGate",			"prydon",	"prydon"},
// GAME_NETHERWORLD
{ "netherworld",	"-netherworld",	"Dark Masters",			"netherworld", 	"nw"},
};

void COM_InitGameType (void)
{
	char name [MAX_OSPATH];
	unsigned int i;

	FS_StripExtension (com_argv[0], name, sizeof (name));
	COM_ToLowerString (name, name, sizeof (name));

	// Check the binary name; default to GAME_NORMAL (0)
	gamemode = GAME_NORMAL;
	for (i = 1; i < sizeof (gamemode_info) / sizeof (gamemode_info[0]); i++)
		if (strstr (name, gamemode_info[i].prog_name))
		{
			gamemode = i;
			break;
		}

	// Look for a command-line option
	for (i = 0; i < sizeof (gamemode_info) / sizeof (gamemode_info[0]); i++)
		if (COM_CheckParm (gamemode_info[i].cmdline))
		{
			gamemode = i;
			break;
		}

	gamename = gamemode_info[gamemode].gamename;
	gamedirname = gamemode_info[gamemode].gamedirname;
	gamescreenshotname = gamemode_info[gamemode].gamescreenshotname;
}


extern void Mathlib_Init(void);
extern void FS_Init (void);

/*
================
COM_Init
================
*/
void COM_Init (void)
{
	Cvar_RegisterVariable (&registered);
	Cvar_RegisterVariable (&cmdline);

	Mathlib_Init();

	FS_Init ();
	Log_Init ();
	COM_CheckRegistered ();
}


/*
============
va

does a varargs printf into a temp buffer, so I don't need to have
varargs versions of all text functions.
FIXME: make this buffer size safe someday
============
*/
char *va(const char *format, ...)
{
	va_list argptr;
	// LordHavoc: now cycles through 8 buffers to avoid problems in most cases
	static char string[8][1024], *s;
	static int stringindex = 0;

	s = string[stringindex];
	stringindex = (stringindex + 1) & 7;
	va_start (argptr, format);
	vsnprintf (s, sizeof (string[0]), format,argptr);
	va_end (argptr);

	return s;
}


//======================================

void COM_ToLowerString (const char *in, char *out, size_t size_out)
{
	if (size_out == 0)
		return;

	while (*in && size_out > 1)
	{
		if (*in >= 'A' && *in <= 'Z')
			*out++ = *in++ + 'a' - 'A';
		else
			*out++ = *in++;
		size_out--;
	}
	*out = '\0';
}

void COM_ToUpperString (const char *in, char *out, size_t size_out)
{
	if (size_out == 0)
		return;

	while (*in && size_out > 1)
	{
		if (*in >= 'a' && *in <= 'z')
			*out++ = *in++ + 'A' - 'a';
		else
			*out++ = *in++;
		size_out--;
	}
	*out = '\0';
}

int COM_StringBeginsWith(const char *s, const char *match)
{
	for (;*s && *match;s++, match++)
		if (*s != *match)
			return false;
	return true;
}

int COM_ReadAndTokenizeLine(const char **text, char **argv, int maxargc, char *tokenbuf, int tokenbufsize, const char *commentprefix)
{
	int argc, commentprefixlength;
	char *tokenbufend;
	const char *l;
	argc = 0;
	tokenbufend = tokenbuf + tokenbufsize;
	l = *text;
	commentprefixlength = 0;
	if (commentprefix)
		commentprefixlength = strlen(commentprefix);
	while (*l && *l != '\n')
	{
		if (*l > ' ')
		{
			if (commentprefixlength && !strncmp(l, commentprefix, commentprefixlength))
			{
				while (*l && *l != '\n')
					l++;
				break;
			}
			if (argc >= maxargc)
				return -1;
			argv[argc++] = tokenbuf;
			if (*l == '"')
			{
				l++;
				while (*l && *l != '"')
				{
					if (tokenbuf >= tokenbufend)
						return -1;
					*tokenbuf++ = *l++;
				}
				if (*l == '"')
					l++;
			}
			else
			{
				while (*l > ' ')
				{
					if (tokenbuf >= tokenbufend)
						return -1;
					*tokenbuf++ = *l++;
				}
			}
			if (tokenbuf >= tokenbufend)
				return -1;
			*tokenbuf++ = 0;
		}
		else
			l++;
	}
	if (*l == '\n')
		l++;
	*text = l;
	return argc;
}

// written by Elric, thanks Elric!
char *SearchInfostring(const char *infostring, const char *key)
{
	static char value [256];
	char crt_key [256];
	size_t value_ind, key_ind;
	char c;

	if (*infostring++ != '\\')
		return NULL;

	value_ind = 0;
	for (;;)
	{
		key_ind = 0;

		// Get the key name
		for (;;)
		{
			c = *infostring++;

			if (c == '\0')
				return NULL;
			if (c == '\\' || key_ind == sizeof (crt_key) - 1)
			{
				crt_key[key_ind] = '\0';
				break;
			}

			crt_key[key_ind++] = c;
		}

		// If it's the key we are looking for, save it in "value"
		if (!strcmp(crt_key, key))
		{
			for (;;)
			{
				c = *infostring++;

				if (c == '\0' || c == '\\' || value_ind == sizeof (value) - 1)
				{
					value[value_ind] = '\0';
					return value;
				}

				value[value_ind++] = c;
			}
		}

		// Else, skip the value
		for (;;)
		{
			c = *infostring++;

			if (c == '\0')
				return NULL;
			if (c == '\\')
				break;
		}
	}
}


//========================================================
// strlcat and strlcpy, from OpenBSD

/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*	$OpenBSD: strlcat.c,v 1.11 2003/06/17 21:56:24 millert Exp $	*/
/*	$OpenBSD: strlcpy.c,v 1.8 2003/06/17 21:56:24 millert Exp $	*/


#ifndef HAVE_STRLCAT
size_t
strlcat(char *dst, const char *src, size_t siz)
{
	register char *d = dst;
	register const char *s = src;
	register size_t n = siz;
	size_t dlen;

	/* Find the end of dst and adjust bytes left but don't go past end */
	while (n-- != 0 && *d != '\0')
		d++;
	dlen = d - dst;
	n = siz - dlen;

	if (n == 0)
		return(dlen + strlen(s));
	while (*s != '\0') {
		if (n != 1) {
			*d++ = *s;
			n--;
		}
		s++;
	}
	*d = '\0';

	return(dlen + (s - src));	/* count does not include NUL */
}
#endif  // #ifndef HAVE_STRLCAT


#ifndef HAVE_STRLCPY
size_t
strlcpy(char *dst, const char *src, size_t siz)
{
	register char *d = dst;
	register const char *s = src;
	register size_t n = siz;

	/* Copy as many bytes as will fit */
	if (n != 0 && --n != 0) {
		do {
			if ((*d++ = *s++) == 0)
				break;
		} while (--n != 0);
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (siz != 0)
			*d = '\0';		/* NUL-terminate dst */
		while (*s++)
			;
	}

	return(s - src - 1);	/* count does not include NUL */
}

#endif  // #ifndef HAVE_STRLCPY
