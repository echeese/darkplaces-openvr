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
/*
This is a try to make the vm more generic, it is mainly based on the progs.h file.
For the license refer to progs.h.

Generic means, less as possible hard-coded links with the other parts of the engine.
This means no edict_engineprivate struct usage, etc.
The code uses void pointers instead.
*/

#ifndef PROGSVM_H
#define PROGSVM_H

#include "pr_comp.h"			// defs shared with qcc
#include "progdefs.h"			// generated by program cdefs
#include "clprogdefs.h"			// generated by program cdefs

/*
typedef union vm_eval_s
{
	string_t		string;
	float			_float;
	float			vector[3];
	func_t			function;
	int				ivector[3];
	int				_int;
	int				edict;
} vm_eval_t;

typedef struct vm_link_s
{
	int entitynumber;
	struct link_s	*prev, *next;
} vm_link_t;

#define ENTITYGRIDAREAS 16

typedef struct vm_edict_engineprivate_s
{
	// true if this edict is unused
	qboolean free;
	// sv.time when the object was freed (to prevent early reuse which could
	// mess up client interpolation or obscure severe QuakeC bugs)
	float freetime;

	// physics grid areas this edict is linked into
	link_t areagrid[ENTITYGRIDAREAS];
	// since the areagrid can have multiple references to one entity,
	// we should avoid extensive checking on entities already encountered
	int areagridmarknumber;

	// old entity protocol, not used
#ifdef QUAKEENTITIES
	// baseline values
	entity_state_t baseline;
	// LordHavoc: previous frame
	entity_state_t deltabaseline;
#endif

	// LordHavoc: gross hack to make floating items still work
	int suspendedinairflag;
	// used by PushMove to keep track of where objects were before they were
	// moved, in case they need to be moved back
	vec3_t moved_from;
	vec3_t moved_fromangles;
}
vm_edict_engineprivate_t;

// the entire server entity structure
// NOTE: keep this small!  priv and v are dynamic but this struct is not!
typedef struct vm_edict_s
{
	// engine-private fields (stored in dynamically resized array)
	edict_engineprivate_t *e;
	// QuakeC fields (stored in dynamically resized array)
	entvars_t *v;
}
vm_edict_t;
*/

/*// LordHavoc: in an effort to eliminate time wasted on GetEdictFieldValue...  see pr_edict.c for the functions which use these.
extern int eval_gravity;
extern int eval_button3;
extern int eval_button4;
extern int eval_button5;
extern int eval_button6;
extern int eval_button7;
extern int eval_button8;
extern int eval_glow_size;
extern int eval_glow_trail;
extern int eval_glow_color;
extern int eval_items2;
extern int eval_scale;
extern int eval_alpha;
extern int eval_renderamt; // HalfLife support
extern int eval_rendermode; // HalfLife support
extern int eval_fullbright;
extern int eval_ammo_shells1;
extern int eval_ammo_nails1;
extern int eval_ammo_lava_nails;
extern int eval_ammo_rockets1;
extern int eval_ammo_multi_rockets;
extern int eval_ammo_cells1;
extern int eval_ammo_plasma;
extern int eval_idealpitch;
extern int eval_pitch_speed;
extern int eval_viewmodelforclient;
extern int eval_nodrawtoclient;
extern int eval_exteriormodeltoclient;
extern int eval_drawonlytoclient;
extern int eval_ping;
extern int eval_movement;
extern int eval_pmodel;
extern int eval_punchvector;
extern int eval_viewzoom;
extern int eval_clientcolors;
extern int eval_tag_entity;
extern int eval_tag_index;*/

typedef struct prvm_stack_s
{
	int				s;
	mfunction_t		*f;
} prvm_stack_t;


typedef union prvm_eval_s
{
	string_t		string;
	float			_float;
	float			vector[3];
	func_t			function;
	int				ivector[3];
	int				_int;
	int				edict;
} prvm_eval_t;

typedef struct prvm_required_field_s
{
	int type;
	const char *name;
} prvm_required_field_t;


/*typedef struct prvm_link_s
{
	int entitynumber;
	struct link_s	*prev, *next;
} prvm_link_t;*/

// AK: I dont call it engine private cause it doesnt really belongs to the engine
//     it belongs to prvm.
typedef struct prvm_edict_private_s
{
	qboolean free;
	float freetime;
} prvm_edict_private_t;

typedef struct prvm_edict_s
{
	// engine-private fields (stored in dynamically resized array)
	//edict_engineprivate_t *e;
	union
	{
		prvm_edict_private_t *required;
		void *vp;
		edict_engineprivate_t *server;
		// add other private structs as you desire
		// new structs have to start with the elements of prvm_edit_private_t
		// e.g. a new struct has to either look like this:
		//	typedef struct server_edict_private_s {
		//		prvm_edict_private_t base;
		//		vec3_t moved_from;
		//      vec3_t moved_fromangles;
		//		... } server_edict_private_t;
		// or:
		//	typedef struct server_edict_private_s {
		//		qboolean free;
		//		float freetime;
		//		vec3_t moved_from;
		//      vec3_t moved_fromangles;
		//		... } server_edict_private_t;
		// However, the first one should be preferred.
	} priv;
	// QuakeC fields (stored in dynamically resized array)
	union
	{
		void *vp;
		entvars_t		*server;
		cl_entvars_t	*client;
	} fields;
} prvm_edict_t;

#define PRVM_GETEDICTFIELDVALUE(ed, fieldoffset) (fieldoffset ? (prvm_eval_t *)((unsigned char *)ed->fields.vp + fieldoffset) : NULL)
#define PRVM_GETGLOBALFIELDVALUE(fieldoffset) (fieldoffset ? (prvm_eval_t *)((unsigned char *)prog->globals.generic + fieldoffset) : NULL)

/*// this struct is the basic requirement for a qc prog
typedef struct prvm_pr_globalvars_s
{
	int pad[28];
} prvm_pr_globalvars_t;
*/
/*
extern mfunction_t *SV_PlayerPhysicsQC;
extern mfunction_t *EndFrameQC;
//KrimZon - SERVER COMMANDS IN QUAKEC
extern mfunction_t *SV_ParseClientCommandQC;
*/
//============================================================================
/*
typedef struct prvm_builtin_mem_s
{
	void (*init)(void);
	void (*deinit)(void);

	void *mem;
} prvm_builtin_mem_t;
*/

//============================================================================
/*
#define PRVM_FE_NEXTHINK	2
#define PRVM_FE_THINK		4
#define PRVM_FE_FRAME		8
*/
#define PRVM_FE_CLASSNAME   8
#define PRVM_FE_CHAIN		4
#define PRVM_OP_STATE		1

#define	PRVM_MAX_STACK_DEPTH		1024
#define	PRVM_LOCALSTACK_SIZE		16384

typedef void (*prvm_builtin_t) (void);

// [INIT] variables flagged with this token can be initialized by 'you'
// NOTE: external code has to create and free the mempools but everything else is done by prvm !
typedef struct prvm_prog_s
{
	dprograms_t			*progs;
	mfunction_t			*functions;
	char				*strings;
	int					stringssize;
	ddef_t				*fielddefs;
	ddef_t				*globaldefs;
	dstatement_t		*statements;
	int					edict_size;			// in bytes
	int					edictareasize;		// LordHavoc: in bytes (for bound checking)

	int					*statement_linenums; // NULL if not available

	double				*statement_profile; // only incremented if prvm_statementprofiling is on

	union {
		float *generic;
		globalvars_t *server;
		cl_globalvars_t *client;
	} globals;

	int					maxknownstrings;
	int					numknownstrings;
	// this is updated whenever a string is removed or added
	// (simple optimization of the free string search)
	int					firstfreeknownstring;
	const char			**knownstrings;
	unsigned char				*knownstrings_freeable;
	const char			***stringshash;

	// all memory allocations related to this vm_prog (code, edicts, strings)
	mempool_t			*progs_mempool; // [INIT]

	prvm_builtin_t		*builtins; // [INIT]
	int					numbuiltins; // [INIT]

	int					argc;

	int					trace;
	mfunction_t			*xfunction;
	int					xstatement;

	// stacktrace writes into stack[MAX_STACK_DEPTH]
	// thus increase the array, so depth wont be overwritten
	prvm_stack_t		stack[PRVM_MAX_STACK_DEPTH+1];
	int					depth;

	int					localstack[PRVM_LOCALSTACK_SIZE];
	int					localstack_used;

	unsigned short		headercrc; // [INIT]

	unsigned short		filecrc;

	//============================================================================
	// until this point everything also exists (with the pr_ prefix) in the old vm

	// copies of some vars that were former read from sv
	int					num_edicts;
	// number of edicts for which space has been (should be) allocated
	int					max_edicts; // [INIT]
	// used instead of the constant MAX_EDICTS
	int					limit_edicts; // [INIT]

	// number of reserved edicts (allocated from 1)
	int					reserved_edicts; // [INIT]


	prvm_edict_t		*edicts;
	void				*edictsfields;
	void				*edictprivate;

	// size of the engine private struct
	int					edictprivate_size; // [INIT]

	// has to be updated every frame - so the vm time is up-to-date
	// AK changed so time will point to the time field (if there is one) else it points to _time
	// actually should be double, but qc doesnt support it
	float				*time;
	float				_time;

	// allow writing to world entity fields, this is set by server init and
	// cleared before first server frame
	qboolean			allowworldwrites;

	// name of the prog, e.g. "Server", "Client" or "Menu" (used for text output)
	char				*name; // [INIT]

	// flag - used to store general flags like PRVM_GE_SELF, etc.
	int					flag;

	char				*extensionstring; // [INIT]

	qboolean			loadintoworld; // [INIT]

	// used to indicate whether a prog is loaded
	qboolean			loaded;

//	prvm_builtin_mem_t  *mem_list;

// now passes as parameter of PRVM_LoadProgs
//	char				**required_func;
//	int					numrequiredfunc;

	//============================================================================

	ddef_t				*self; // if self != 0 then there is a global self

	//============================================================================
	// function pointers

	void				(*begin_increase_edicts)(void); // [INIT] used by PRVM_MEM_Increase_Edicts
	void				(*end_increase_edicts)(void); // [INIT]

	void				(*init_edict)(prvm_edict_t *edict); // [INIT] used by PRVM_ED_ClearEdict
	void				(*free_edict)(prvm_edict_t *ed); // [INIT] used by PRVM_ED_Free

	void				(*count_edicts)(void); // [INIT] used by PRVM_ED_Count_f

	qboolean			(*load_edict)(prvm_edict_t *ent); // [INIT] used by PRVM_ED_LoadFromFile

	void				(*init_cmd)(void); // [INIT] used by PRVM_InitProg
	void				(*reset_cmd)(void); // [INIT] used by PRVM_ResetProg

	void				(*error_cmd)(const char *format, ...); // [INIT]

} prvm_prog_t;

extern prvm_prog_t * prog;

#define PRVM_MAXPROGS 3
#define PRVM_SERVERPROG 0 // actually not used at the moment
#define PRVM_CLIENTPROG 1
#define PRVM_MENUPROG	2

extern prvm_prog_t prvm_prog_list[PRVM_MAXPROGS];

//============================================================================
// prvm_cmds part

extern prvm_builtin_t vm_sv_builtins[];
extern prvm_builtin_t vm_cl_builtins[];
extern prvm_builtin_t vm_m_builtins[];

extern const int vm_sv_numbuiltins;
extern const int vm_cl_numbuiltins;
extern const int vm_m_numbuiltins;

extern char * vm_sv_extensions;
extern char * vm_cl_extensions;
extern char * vm_m_extensions;

void VM_SV_Cmd_Init(void);
void VM_SV_Cmd_Reset(void);

void VM_CL_Cmd_Init(void);
void VM_CL_Cmd_Reset(void);

void VM_M_Cmd_Init(void);
void VM_M_Cmd_Reset(void);

void VM_Cmd_Init(void);
void VM_Cmd_Reset(void);
//============================================================================

void PRVM_Init (void);

void PRVM_ExecuteProgram (func_t fnum, const char *errormessage);

#define PRVM_Alloc(buffersize) _PRVM_Alloc(buffersize, __FILE__, __LINE__)
#define PRVM_Free(buffer) _PRVM_Free(buffer, __FILE__, __LINE__)
#define PRVM_FreeAll() _PRVM_FreeAll(__FILE__, __LINE__)
void *_PRVM_Alloc (size_t buffersize, const char *filename, int fileline);
void _PRVM_Free (void *buffer, const char *filename, int fileline);
void _PRVM_FreeAll (const char *filename, int fileline);

void PRVM_Profile (int maxfunctions, int mininstructions);
void PRVM_Profile_f (void);
void PRVM_PrintFunction_f (void);

void PRVM_PrintState(void);
void PRVM_CrashAll (void);
void PRVM_Crash (void);

int PRVM_ED_FindFieldOffset(const char *field);
int PRVM_ED_FindGlobalOffset(const char *global);
ddef_t *PRVM_ED_FindField (const char *name);
mfunction_t *PRVM_ED_FindFunction (const char *name);

void PRVM_MEM_IncreaseEdicts(void);

prvm_edict_t *PRVM_ED_Alloc (void);
void PRVM_ED_Free (prvm_edict_t *ed);
void PRVM_ED_ClearEdict (prvm_edict_t *e);

void PRVM_PrintFunctionStatements (const char *name);
void PRVM_ED_Print(prvm_edict_t *ed);
void PRVM_ED_Write (qfile_t *f, prvm_edict_t *ed);
const char *PRVM_ED_ParseEdict (const char *data, prvm_edict_t *ent);

void PRVM_ED_WriteGlobals (qfile_t *f);
void PRVM_ED_ParseGlobals (const char *data);

void PRVM_ED_LoadFromFile (const char *data);

prvm_edict_t *PRVM_EDICT_NUM_ERROR(int n, char *filename, int fileline);
#define	PRVM_EDICT_NUM(n) (((n) >= 0 && (n) < prog->max_edicts) ? prog->edicts + (n) : PRVM_EDICT_NUM_ERROR(n, __FILE__, __LINE__))
#define	PRVM_EDICT_NUM_UNSIGNED(n) (((n) < prog->max_edicts) ? prog->edicts + (n) : PRVM_EDICT_NUM_ERROR(n, __FILE__, __LINE__))

//int NUM_FOR_EDICT_ERROR(prvm_edict_t *e);
#define PRVM_NUM_FOR_EDICT(e) ((int)((prvm_edict_t *)(e) - prog->edicts))
//int PRVM_NUM_FOR_EDICT(prvm_edict_t *e);

#define	PRVM_NEXT_EDICT(e) ((e) + 1)

#define PRVM_EDICT_TO_PROG(e) (PRVM_NUM_FOR_EDICT(e))
//int PRVM_EDICT_TO_PROG(prvm_edict_t *e);
#define PRVM_PROG_TO_EDICT(n) (PRVM_EDICT_NUM(n))
//prvm_edict_t *PRVM_PROG_TO_EDICT(int n);

//============================================================================

#define	PRVM_G_FLOAT(o) (prog->globals.generic[o])
#define	PRVM_G_INT(o) (*(int *)&prog->globals.generic[o])
#define	PRVM_G_EDICT(o) (PRVM_PROG_TO_EDICT(*(int *)&prog->globals.generic[o]))
#define PRVM_G_EDICTNUM(o) PRVM_NUM_FOR_EDICT(PRVM_G_EDICT(o))
#define	PRVM_G_VECTOR(o) (&prog->globals.generic[o])
#define	PRVM_G_STRING(o) (PRVM_GetString(*(string_t *)&prog->globals.generic[o]))
//#define	PRVM_G_FUNCTION(o) (*(func_t *)&prog->globals.generic[o])

// FIXME: make these go away?
#define	PRVM_E_FLOAT(e,o) (((float*)e->fields.vp)[o])
#define	PRVM_E_INT(e,o) (((int*)e->fields.vp)[o])
//#define	PRVM_E_VECTOR(e,o) (&((float*)e->fields.vp)[o])
#define	PRVM_E_STRING(e,o) (PRVM_GetString(*(string_t *)&((float*)e->fields.vp)[o]))

extern	int		prvm_type_size[8]; // for consistency : I think a goal of this sub-project is to
// make the new vm mostly independent from the old one, thus if it's necessary, I copy everything

void PRVM_Init_Exec(void);

void PRVM_ED_PrintEdicts_f (void);
void PRVM_ED_PrintNum (int ent);

const char *PRVM_GetString(int num);
int PRVM_SetEngineString(const char *s);
int PRVM_AllocString(size_t bufferlength, char **pointer);
void PRVM_FreeString(int num);

//============================================================================

// used as replacement for a prog stack
//#define PRVM_DEBUGPRSTACK

#ifdef PRVM_DEBUGPRSTACK
#define PRVM_Begin  if(prog != 0) Con_Printf("prog not 0(prog = %i) in file: %s line: %i!\n", PRVM_GetProgNr(), __FILE__, __LINE__)
#define PRVM_End	prog = 0
#else
#define PRVM_Begin
#define PRVM_End	prog = 0
#endif


//#define PRVM_SAFENAME
#ifndef PRVM_SAFENAME
#	define PRVM_NAME	(prog->name)
#else
#	define PRVM_NAME	(prog->name ? prog->name : "Unknown prog name")
#endif

// helper macro to make function pointer calls easier
#define PRVM_GCALL(func)	if(prog->func) prog->func

#define PRVM_ERROR		prog->error_cmd

// other prog handling functions
qboolean PRVM_SetProgFromString(const char *str);
void PRVM_SetProg(int prognr);

/*
Initializing a vm:
Call InitProg with the num
Set up the fields marked with [INIT] in the prog struct
Load a program with LoadProgs
*/
void PRVM_InitProg(int prognr);
// LoadProgs expects to be called right after InitProg
void PRVM_LoadProgs (const char *filename, int numrequiredfunc, char **required_func, int numrequiredfields, prvm_required_field_t *required_field);
void PRVM_ResetProg(void);

qboolean PRVM_ProgLoaded(int prognr);

int	PRVM_GetProgNr(void);

void VM_Warning(const char *fmt, ...);

// TODO: fill in the params
//void PRVM_Create();

#endif
