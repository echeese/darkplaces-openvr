#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <signal.h>
#include <limits.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#include "quakedef.h"

qboolean			isDedicated;

int nostdout = 0;

char *basedir = ".";
#if CACHEENABLE
char *cachedir = "/tmp";
#endif

extern cvar_t	timestamps;
extern cvar_t	timeformat;

/* The translation table between the graphical font and plain ASCII  --KB */
static char qfont_table[256] = {
	'\0', '#',  '#',  '#',  '#',  '.',  '#',  '#',
	'#',  9,    10,   '#',  ' ',  13,   '.',  '.',
	'[',  ']',  '0',  '1',  '2',  '3',  '4',  '5',
	'6',  '7',  '8',  '9',  '.',  '<',  '=',  '>',
	' ',  '!',  '"',  '#',  '$',  '%',  '&',  '\'',
	'(',  ')',  '*',  '+',  ',',  '-',  '.',  '/',
	'0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',
	'8',  '9',  ':',  ';',  '<',  '=',  '>',  '?',
	'@',  'A',  'B',  'C',  'D',  'E',  'F',  'G',
	'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',
	'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',
	'X',  'Y',  'Z',  '[',  '\\', ']',  '^',  '_',
	'`',  'a',  'b',  'c',  'd',  'e',  'f',  'g',
	'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',
	'p',  'q',  'r',  's',  't',  'u',  'v',  'w',
	'x',  'y',  'z',  '{',  '|',  '}',  '~',  '<',

	'<',  '=',  '>',  '#',  '#',  '.',  '#',  '#',
	'#',  '#',  ' ',  '#',  ' ',  '>',  '.',  '.',
	'[',  ']',  '0',  '1',  '2',  '3',  '4',  '5',
	'6',  '7',  '8',  '9',  '.',  '<',  '=',  '>',
	' ',  '!',  '"',  '#',  '$',  '%',  '&',  '\'',
	'(',  ')',  '*',  '+',  ',',  '-',  '.',  '/',
	'0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',
	'8',  '9',  ':',  ';',  '<',  '=',  '>',  '?',
	'@',  'A',  'B',  'C',  'D',  'E',  'F',  'G',
	'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',
	'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',
	'X',  'Y',  'Z',  '[',  '\\', ']',  '^',  '_',
	'`',  'a',  'b',  'c',  'd',  'e',  'f',  'g',
	'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o', 
	'p',  'q',  'r',  's',  't',  'u',  'v',  'w',
	'x',  'y',  'z',  '{',  '|',  '}',  '~',  '<'
};

// =======================================================================
// General routines
// =======================================================================

void Sys_DebugNumber(int y, int val)
{
}

/*
void Sys_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];
	
	va_start (argptr,fmt);
	vsprintf (text,fmt,argptr);
	va_end (argptr);
	Qprintf(stderr, "%s", text);
	
	Con_Print (text);
}

void Sys_Printf (char *fmt, ...)
{

    va_list     argptr;
    char        text[1024], *t_p;
    int         l, r;

	if (nostdout)
		return;

    va_start (argptr,fmt);
    vsprintf (text,fmt,argptr);
    va_end (argptr);

    l = strlen(text);
    t_p = text;

// make sure everything goes through, even though we are non-blocking
    while (l)
    {
        r = write (1, text, l);
        if (r != l)
            sleep (0);
        if (r > 0)
        {
            t_p += r;
            l -= r;
        }
    }

}
*/

#define MAX_PRINT_MSG	4096
void Sys_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		start[MAX_PRINT_MSG];	// String we started with
	char		stamp[MAX_PRINT_MSG];	// Time stamp
	char		final[MAX_PRINT_MSG];	// String we print

	time_t		mytime = 0;
	struct tm	*local = NULL;

	unsigned char		*p;

	va_start (argptr, fmt);
	vsnprintf (start, sizeof(start), fmt, argptr);
	va_end (argptr);

	if (nostdout)
		return;

	if (timestamps.value) {
		mytime = time (NULL);
		local = localtime (&mytime);
		strftime (stamp, sizeof (stamp), timeformat.string, local);
		
		snprintf (final, sizeof (final), "%s%s", stamp, start);
	} else {
		snprintf (final, sizeof (final), "%s", start);
	}

	for (p = (unsigned char *) final; *p; p++) {
		putc (qfont_table[*p], stdout);
	}
	fflush (stdout);
}

#if 0
static char end1[] =
"\x1b[?7h\x1b[40m\x1b[2J\x1b[0;1;41m\x1b[1;1H                QUAKE: The Doomed Dimension \x1b[33mby \x1b[44mid\x1b[41m Software                      \x1b[2;1H  ----------------------------------------------------------------------------  \x1b[3;1H           CALL 1-800-IDGAMES TO ORDER OR FOR TECHNICAL SUPPORT                 \x1b[4;1H             PRICE: $45.00 (PRICES MAY VARY OUTSIDE THE US.)                    \x1b[5;1H                                                                                \x1b[6;1H  \x1b[37mYes! You only have one fourth of this incredible epic. That is because most   \x1b[7;1H   of you have paid us nothing or at most, very little. You could steal the     \x1b[8;1H   game from a friend. But we both know you'll be punished by God if you do.    \x1b[9;1H        \x1b[33mWHY RISK ETERNAL DAMNATION? CALL 1-800-IDGAMES AND BUY NOW!             \x1b[10;1H             \x1b[37mRemember, we love you almost as much as He does.                   \x1b[11;1H                                                                                \x1b[12;1H            \x1b[33mProgramming: \x1b[37mJohn Carmack, Michael Abrash, John Cash                \x1b[13;1H       \x1b[33mDesign: \x1b[37mJohn Romero, Sandy Petersen, American McGee, Tim Willits         \x1b[14;1H                     \x1b[33mArt: \x1b[37mAdrian Carmack, Kevin Cloud                           \x1b[15;1H               \x1b[33mBiz: \x1b[37mJay Wilbur, Mike Wilson, Donna Jackson                      \x1b[16;1H            \x1b[33mProjects: \x1b[37mShawn Green   \x1b[33mSupport: \x1b[37mBarrett Alexander                  \x1b[17;1H              \x1b[33mSound Effects: \x1b[37mTrent Reznor and Nine Inch Nails                   \x1b[18;1H  For other information or details on ordering outside the US, check out the    \x1b[19;1H     files accompanying QUAKE or our website at http://www.idsoftware.com.      \x1b[20;1H    \x1b[0;41mQuake is a trademark of Id Software, inc., (c)1996 Id Software, inc.        \x1b[21;1H     All rights reserved. NIN logo is a r
egistered trademark licensed to        \x1b[22;1H                 Nothing Interactive, Inc. All rights reserved.                 \x1b[40m\x1b[23;1H\x1b[0m";
static char end2[] =
"\x1b[?7h\x1b[40m\x1b[2J\x1b[0;1;41m\x1b[1;1H        QUAKE \x1b[33mby \x1b[44mid\x1b[41m Software                                                    \x1b[2;1H -----------------------------------------------------------------------------  \x1b[3;1H        \x1b[37mWhy did you quit from the registered version of QUAKE? Did the          \x1b[4;1H        scary monsters frighten you? Or did Mr. Sandman tug at your             \x1b[5;1H        little lids? No matter! What is important is you love our               \x1b[6;1H        game, and gave us your money. Congratulations, you are probably         \x1b[7;1H        not a thief.                                                            \x1b[8;1H                                                           Thank You.           \x1b[9;1H        \x1b[33;44mid\x1b[41m Software is:                                                         \x1b[10;1H        PROGRAMMING: \x1b[37mJohn Carmack, Michael Abrash, John Cash                    \x1b[11;1H        \x1b[33mDESIGN: \x1b[37mJohn Romero, Sandy Petersen, American McGee, Tim Willits        \x1b[12;1H        \x1b[33mART: \x1b[37mAdrian Carmack, Kevin Cloud                                        \x1b[13;1H        \x1b[33mBIZ: \x1b[37mJay Wilbur, Mike Wilson     \x1b[33mPROJECTS MAN: \x1b[37mShawn Green              \x1b[14;1H        \x1b[33mBIZ ASSIST: \x1b[37mDonna Jackson        \x1b[33mSUPPORT: \x1b[37mBarrett Alexander             \x1b[15;1H        \x1b[33mSOUND EFFECTS AND MUSIC: \x1b[37mTrent Reznor and Nine Inch Nails               \x1b[16;1H                                                                                \x1b[17;1H        If you need help running QUAKE refer to the text files in the           \x1b[18;1H        QUAKE directory, or our website at http://www.idsoftware.com.           \x1b[19;1H        If all else fails, call our technical support at 1-800-IDGAMES.         \x1b[20;1H      \x1b[0;41mQuake is a trademark of Id Software, inc., (c)1996 Id Software, inc.      \x1b[21;1H        All rights reserved. N
IN logo is a registered trademark licensed        \x1b[22;1H             to Nothing Interactive, Inc. All rights reserved.                  \x1b[23;1H\x1b[40m\x1b[0m";

#endif
void Sys_Quit (void)
{
	Host_Shutdown();
    fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);
#if 0
	if (registered.value)
		printf("%s", end2);
	else
		printf("%s", end1);
#endif
	fflush(stdout);
	exit(0);
}

void Sys_Error (char *error, ...)
{ 
    va_list     argptr;
    char        string[1024];

// change stdin to non blocking
    fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);
    
    va_start (argptr,error);
    vsprintf (string,error,argptr);
    va_end (argptr);
	fprintf(stderr, "Error: %s\n", string);

	Host_Shutdown ();
	exit (1);

} 

void Sys_Warn (char *warning, ...)
{ 
    va_list     argptr;
    char        string[1024];
    
    va_start (argptr,warning);
    vsprintf (string,warning,argptr);
    va_end (argptr);
	fprintf(stderr, "Warning: %s", string);
} 

/*
============
Sys_FileTime

returns -1 if not present
============
*/
int	Sys_FileTime (char *path)
{
	struct	stat	buf;
	
	if (stat (path,&buf) == -1)
		return -1;
	
	return buf.st_mtime;
}


void Sys_mkdir (char *path)
{
    mkdir (path, 0777);
}

int Sys_FileOpenRead (char *path, int *handle)
{
	int	h;
	struct stat	fileinfo;
    
	
	h = open (path, O_RDONLY, 0666);
	*handle = h;
	if (h == -1)
		return -1;
	
	if (fstat (h,&fileinfo) == -1)
		Sys_Error ("Error fstating %s", path);

	return fileinfo.st_size;
}

int Sys_FileOpenWrite (char *path)
{
	int     handle;

	umask (0);
	
	handle = open(path,O_RDWR | O_CREAT | O_TRUNC
	, 0666);

	if (handle == -1)
		Sys_Error ("Error opening %s: %s", path,strerror(errno));

	return handle;
}

int Sys_FileWrite (int handle, void *src, int count)
{
	return write (handle, src, count);
}

void Sys_FileClose (int handle)
{
	close (handle);
}

void Sys_FileSeek (int handle, int position)
{
	lseek (handle, position, SEEK_SET);
}

int Sys_FileRead (int handle, void *dest, int count)
{
    return read (handle, dest, count);
}

void Sys_DebugLog(char *file, char *fmt, ...)
{
    va_list argptr; 
    static char data[1024];
    int fd;
    
    va_start(argptr, fmt);
    vsprintf(data, fmt, argptr);
    va_end(argptr);
//    fd = open(file, O_WRONLY | O_BINARY | O_CREAT | O_APPEND, 0666);
    fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0666);
    write(fd, data, strlen(data));
    close(fd);
}

double Sys_DoubleTime (void)
{
	static int first = true;
	static double oldtime = 0.0, basetime = 0.0;
	double newtime;
	struct timeval tp;
	struct timezone tzp; 

	gettimeofday(&tp, &tzp);

	newtime = (double) ((unsigned long) tp.tv_sec) + tp.tv_usec/1000000.0 - basetime;

	if (first)
	{
		first = false;
		basetime = newtime;
		newtime = 0.0;
	}

	if (newtime < oldtime)
		Sys_Error("Sys_DoubleTime: time running backwards??\n");

	oldtime = newtime;

	return newtime;
}

// =======================================================================
// Sleeps for microseconds
// =======================================================================

static volatile int oktogo;

void alarm_handler(int x)
{
	oktogo=1;
}

void floating_point_exception_handler(int whatever)
{
//	Sys_Warn("floating point exception\n");
	signal(SIGFPE, floating_point_exception_handler);
}

char *Sys_ConsoleInput(void)
{
    static char text[256];
    int     len;
	fd_set	fdset;
    struct timeval timeout;

	if (cls.state == ca_dedicated) {
		FD_ZERO(&fdset);
		FD_SET(0, &fdset); // stdin
		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		if (select (1, &fdset, NULL, NULL, &timeout) == -1 || !FD_ISSET(0, &fdset))
			return NULL;

		len = read (0, text, sizeof(text));
		if (len < 1)
			return NULL;
		text[len-1] = 0;    // rip off the /n and terminate

		return text;
	}
	return NULL;
}

void Sys_Sleep(void)
{
	usleep(1);
}

int main (int c, char **v)
{

	double oldtime, newtime;

//	static char cwd[1024];

//	signal(SIGFPE, floating_point_exception_handler);
	signal(SIGFPE, SIG_IGN);

	memset(&host_parms, 0, sizeof(host_parms));

	COM_InitArgv(c, v);
	host_parms.argc = com_argc;
	host_parms.argv = com_argv;

	host_parms.memsize = DEFAULTMEM * 1024*1024;

	host_parms.basedir = basedir;

	fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);

	Host_Init();

	if (COM_CheckParm("-nostdout"))
		nostdout = 1;
	else
	{
		fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);
		printf ("Linux DarkPlaces -- Version %0.3f (build %i)\n", VERSION, buildnumber);
	}

	oldtime = Sys_DoubleTime () - 0.1;
	while (1)
	{
		// find time spent rendering last frame
		newtime = Sys_DoubleTime ();

		Host_Frame (newtime - oldtime);

		oldtime = newtime;
	}
	return 0;
}
