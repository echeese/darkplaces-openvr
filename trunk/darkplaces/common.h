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

#ifndef COMMON_H
#define COMMON_H

// LordHavoc: MSVC has a different name for snprintf
#ifdef WIN32
#define snprintf _snprintf
#endif

//============================================================================

typedef struct sizebuf_s
{
	qboolean	allowoverflow;	// if false, do a Sys_Error
	qboolean	overflowed;		// set to true if the buffer size failed
	qbyte		*data;
	mempool_t	*mempool;
	int			maxsize;
	int			cursize;
} sizebuf_t;

void SZ_Alloc (sizebuf_t *buf, int startsize, const char *name);
void SZ_Free (sizebuf_t *buf);
void SZ_Clear (sizebuf_t *buf);
void *SZ_GetSpace (sizebuf_t *buf, int length);
void SZ_Write (sizebuf_t *buf, const void *data, int length);
void SZ_Print (sizebuf_t *buf, const char *data);	// strcats onto the sizebuf
void SZ_HexDumpToConsole(const sizebuf_t *buf);

//============================================================================
#if !defined(ENDIAN_LITTLE) && !defined(ENDIAN_BIG)
#if  defined(__i386__) || defined(__ia64__) || defined(WIN32) || (defined(__alpha__) || defined(__alpha)) || defined(__arm__) || (defined(__mips__) && defined(__MIPSEL__)) || defined(__LITTLE_ENDIAN__)
#define ENDIAN_LITTLE
#else
#define ENDIAN_BIG
#endif
#endif

short ShortSwap (short l);
int LongSwap (int l);
float FloatSwap (float f);

#ifdef ENDIAN_LITTLE
// little endian
#define BigShort(l) ShortSwap(l)
#define LittleShort(l) (l)
#define BigLong(l) LongSwap(l)
#define LittleLong(l) (l)
#define BigFloat(l) FloatSwap(l)
#define LittleFloat(l) (l)
#elif ENDIAN_BIG
// big endian
#define BigShort(l) (l)
#define LittleShort(l) ShortSwap(l)
#define BigLong(l) (l)
#define LittleLong(l) LongSwap(l)
#define BigFloat(l) (l)
#define LittleFloat(l) FloatSwap(l)
#else
// figure it out at runtime
extern short (*BigShort) (short l);
extern short (*LittleShort) (short l);
extern int (*BigLong) (int l);
extern int (*LittleLong) (int l);
extern float (*BigFloat) (float l);
extern float (*LittleFloat) (float l);
#endif

//============================================================================

void MSG_WriteChar (sizebuf_t *sb, int c);
void MSG_WriteByte (sizebuf_t *sb, int c);
void MSG_WriteShort (sizebuf_t *sb, int c);
void MSG_WriteLong (sizebuf_t *sb, int c);
void MSG_WriteFloat (sizebuf_t *sb, float f);
void MSG_WriteString (sizebuf_t *sb, const char *s);
void MSG_WriteCoord (sizebuf_t *sb, float f);
void MSG_WriteAngle (sizebuf_t *sb, float f);
void MSG_WritePreciseAngle (sizebuf_t *sb, float f);
void MSG_WriteDPCoord (sizebuf_t *sb, float f);

extern	int			msg_readcount;
extern	qboolean	msg_badread;		// set if a read goes beyond end of message

void MSG_BeginReading (void);
int MSG_ReadShort (void);
int MSG_ReadLong (void);
float MSG_ReadFloat (void);
char *MSG_ReadString (void);

#define MSG_ReadChar() (msg_readcount >= net_message.cursize ? (msg_badread = true, -1) : (signed char)net_message.data[msg_readcount++])
#define MSG_ReadByte() (msg_readcount >= net_message.cursize ? (msg_badread = true, -1) : (unsigned char)net_message.data[msg_readcount++])

float MSG_ReadCoord (void);

float MSG_ReadDPCoord (void);

#define MSG_ReadAngle() (MSG_ReadByte() * (360.0f / 256.0f))
#define MSG_ReadPreciseAngle() (MSG_ReadShort() * (360.0f / 65536.0f))

#define MSG_ReadVector(v) {(v)[0] = MSG_ReadCoord();(v)[1] = MSG_ReadCoord();(v)[2] = MSG_ReadCoord();}

extern int dpprotocol;

//============================================================================

int Q_strcasecmp (const char *s1, const char *s2);
int Q_strncasecmp (const char *s1, const char *s2, int n);

//============================================================================

extern char com_token[1024];
extern qboolean com_eof;

int COM_ParseToken (const char **data);

extern char com_basedir[MAX_OSPATH];
extern int com_argc;
extern const char **com_argv;

int COM_CheckParm (const char *parm);
void COM_Init (void);
void COM_InitArgv (void);
void COM_InitGameType (void);

void COM_StripExtension (const char *in, char *out);
void COM_FileBase (const char *in, char *out);
void COM_DefaultExtension (char *path, const char *extension);

char	*va(const char *format, ...);
// does a varargs printf into a temp buffer


//============================================================================

extern int com_filesize;

extern	char	com_gamedir[MAX_OSPATH];

qboolean COM_WriteFile (const char *filename, void *data, int len);
int COM_FOpenFile (const char *filename, QFile **file, qboolean quiet, qboolean zip);

// set by COM_LoadFile functions
extern int loadsize;
qbyte *COM_LoadFile (const char *path, qboolean quiet);

int COM_FileExists(const char *filename);

extern	struct cvar_s	registered;

#define GAME_NORMAL 0
#define GAME_HIPNOTIC 1
#define GAME_ROGUE 2
#define GAME_NEHAHRA 3
#define GAME_TRANSFUSION 4

extern int gamemode;
extern char *gamename;
extern char com_modname[MAX_OSPATH];

// LordHavoc: useful...
void COM_ToLowerString(const char *in, char *out);
void COM_ToUpperString(const char *in, char *out);
int COM_StringBeginsWith(const char *s, const char *match);

typedef struct stringlist_s
{
	struct stringlist_s *next;
	char *text;
} stringlist_t;

int matchpattern(char *in, char *pattern, int caseinsensitive);
stringlist_t *listdirectory(char *path);
void freedirectory(stringlist_t *list);

#endif

