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
// sbar.c -- status bar code

#include "quakedef.h"

typedef struct sbarpic_s
{
	char name[32];
}
sbarpic_t;

static sbarpic_t sbarpics[256];
static int numsbarpics;

static sbarpic_t *Sbar_NewPic(const char *name)
{
	strcpy(sbarpics[numsbarpics].name, name);
	// precache it
	// FIXME: precache on every renderer restart (or move this to client)
	Draw_CachePic(sbarpics[numsbarpics].name, true);
	return sbarpics + (numsbarpics++);
}

sbarpic_t *sb_disc;

#define STAT_MINUS 10 // num frame for '-' stats digit
sbarpic_t *sb_nums[2][11];
sbarpic_t *sb_colon, *sb_slash;
sbarpic_t *sb_ibar;
sbarpic_t *sb_sbar;
sbarpic_t *sb_scorebar;
// AK only used by NEX
sbarpic_t *sb_sbar_minimal;
sbarpic_t *sb_sbar_overlay;

// AK changed the bound to 9
sbarpic_t *sb_weapons[7][9]; // 0 is active, 1 is owned, 2-5 are flashes
sbarpic_t *sb_ammo[4];
sbarpic_t *sb_sigil[4];
sbarpic_t *sb_armor[3];
sbarpic_t *sb_items[32];

// 0-4 are based on health (in 20 increments)
// 0 is static, 1 is temporary animation
sbarpic_t *sb_faces[5][2];

sbarpic_t *sb_face_invis;
sbarpic_t *sb_face_quad;
sbarpic_t *sb_face_invuln;
sbarpic_t *sb_face_invis_invuln;

qboolean sb_showscores;

int sb_lines;			// scan lines to draw

sbarpic_t *rsb_invbar[2];
sbarpic_t *rsb_weapons[5];
sbarpic_t *rsb_items[2];
sbarpic_t *rsb_ammo[3];
sbarpic_t *rsb_teambord;		// PGM 01/19/97 - team color border

//MED 01/04/97 added two more weapons + 3 alternates for grenade launcher
sbarpic_t *hsb_weapons[7][5];   // 0 is active, 1 is owned, 2-5 are flashes
//MED 01/04/97 added array to simplify weapon parsing
int hipweapons[4] = {HIT_LASER_CANNON_BIT,HIT_MJOLNIR_BIT,4,HIT_PROXIMITY_GUN_BIT};
//MED 01/04/97 added hipnotic items array
sbarpic_t *hsb_items[2];

//GAME_SOM stuff:
sbarpic_t *somsb_health;
sbarpic_t *somsb_ammo[4];
sbarpic_t *somsb_armor[3];

sbarpic_t *zymsb_crosshair_center;
sbarpic_t *zymsb_crosshair_line;
sbarpic_t *zymsb_crosshair_health;
sbarpic_t *zymsb_crosshair_ammo;
sbarpic_t *zymsb_crosshair_clip;
sbarpic_t *zymsb_crosshair_background;
sbarpic_t *zymsb_crosshair_left1;
sbarpic_t *zymsb_crosshair_left2;
sbarpic_t *zymsb_crosshair_right;

cvar_t showfps = {CVAR_SAVE, "showfps", "0", "shows your rendered fps (frames per second)"};
cvar_t showtime = {CVAR_SAVE, "showtime", "0", "shows current time of day (useful on screenshots)"};
cvar_t showtime_format = {CVAR_SAVE, "showtime_format", "%H:%M:%S", "format string for time of day"};
cvar_t showdate = {CVAR_SAVE, "showdate", "0", "shows current date (useful on screenshots)"};
cvar_t showdate_format = {CVAR_SAVE, "showdate_format", "%Y-%m-%d", "format string for date"};
cvar_t sbar_alpha_bg = {CVAR_SAVE, "sbar_alpha_bg", "0.4", "opacity value of the statusbar background image"};
cvar_t sbar_alpha_fg = {CVAR_SAVE, "sbar_alpha_fg", "1", "opacity value of the statusbar weapon/item icons and numbers"};

cvar_t cl_deathscoreboard = {0, "cl_deathscoreboard", "1", "shows scoreboard (+showscores) while dead"};

void Sbar_MiniDeathmatchOverlay (int x, int y);
void Sbar_DeathmatchOverlay (void);
void Sbar_IntermissionOverlay (void);
void Sbar_FinaleOverlay (void);


/*
===============
Sbar_ShowScores

Tab key down
===============
*/
void Sbar_ShowScores (void)
{
	if (sb_showscores)
		return;
	sb_showscores = true;
}

/*
===============
Sbar_DontShowScores

Tab key up
===============
*/
void Sbar_DontShowScores (void)
{
	sb_showscores = false;
}

void sbar_start(void)
{
	int i;

	numsbarpics = 0;

	if (gamemode == GAME_NETHERWORLD)
	{
	}
	else if (gamemode == GAME_SOM)
	{
		sb_disc = Sbar_NewPic("gfx/disc");

		for (i = 0;i < 10;i++)
			sb_nums[0][i] = Sbar_NewPic (va("gfx/num_%i",i));

		somsb_health = Sbar_NewPic("gfx/hud_health");
		somsb_ammo[0] = Sbar_NewPic("gfx/sb_shells");
		somsb_ammo[1] = Sbar_NewPic("gfx/sb_nails");
		somsb_ammo[2] = Sbar_NewPic("gfx/sb_rocket");
		somsb_ammo[3] = Sbar_NewPic("gfx/sb_cells");
		somsb_armor[0] = Sbar_NewPic("gfx/sb_armor1");
		somsb_armor[1] = Sbar_NewPic("gfx/sb_armor2");
		somsb_armor[2] = Sbar_NewPic("gfx/sb_armor3");
	}
	else if (gamemode == GAME_NEXUIZ)
	{
		for (i = 0;i < 10;i++)
			sb_nums[0][i] = Sbar_NewPic (va("gfx/num_%i",i));
		sb_nums[0][10] = Sbar_NewPic ("gfx/num_minus");

		sb_ammo[0] = Sbar_NewPic ("gfx/sb_shells");
		sb_ammo[1] = Sbar_NewPic ("gfx/sb_bullets");
		sb_ammo[2] = Sbar_NewPic ("gfx/sb_rocket");
		sb_ammo[3] = Sbar_NewPic ("gfx/sb_cells");

		sb_items[2] = Sbar_NewPic ("gfx/sb_slowmo");
		sb_items[3] = Sbar_NewPic ("gfx/sb_invinc");
		sb_items[4] = Sbar_NewPic ("gfx/sb_energy");
		sb_items[5] = Sbar_NewPic ("gfx/sb_str");

		sb_sbar = Sbar_NewPic("gfx/sbar");
		sb_sbar_minimal = Sbar_NewPic("gfx/sbar_minimal");
		sb_sbar_overlay = Sbar_NewPic("gfx/sbar_overlay");

		for(i = 0; i < 9;i++)
			sb_weapons[0][i] = Sbar_NewPic(va("gfx/inv_weapon%i",i));
	}
	else if (gamemode == GAME_ZYMOTIC)
	{
		zymsb_crosshair_center = Sbar_NewPic ("gfx/hud/crosshair_center");
		zymsb_crosshair_line = Sbar_NewPic ("gfx/hud/crosshair_line");
		zymsb_crosshair_health = Sbar_NewPic ("gfx/hud/crosshair_health");
		zymsb_crosshair_clip = Sbar_NewPic ("gfx/hud/crosshair_clip");
		zymsb_crosshair_ammo = Sbar_NewPic ("gfx/hud/crosshair_ammo");
		zymsb_crosshair_background = Sbar_NewPic ("gfx/hud/crosshair_background");
		zymsb_crosshair_left1 = Sbar_NewPic ("gfx/hud/crosshair_left1");
		zymsb_crosshair_left2 = Sbar_NewPic ("gfx/hud/crosshair_left2");
		zymsb_crosshair_right = Sbar_NewPic ("gfx/hud/crosshair_right");
	}
	else
	{
		sb_disc = Sbar_NewPic("gfx/disc");

		for (i = 0;i < 10;i++)
		{
			sb_nums[0][i] = Sbar_NewPic (va("gfx/num_%i",i));
			sb_nums[1][i] = Sbar_NewPic (va("gfx/anum_%i",i));
		}

		sb_nums[0][10] = Sbar_NewPic ("gfx/num_minus");
		sb_nums[1][10] = Sbar_NewPic ("gfx/anum_minus");

		sb_colon = Sbar_NewPic ("gfx/num_colon");
		sb_slash = Sbar_NewPic ("gfx/num_slash");

		sb_weapons[0][0] = Sbar_NewPic ("gfx/inv_shotgun");
		sb_weapons[0][1] = Sbar_NewPic ("gfx/inv_sshotgun");
		sb_weapons[0][2] = Sbar_NewPic ("gfx/inv_nailgun");
		sb_weapons[0][3] = Sbar_NewPic ("gfx/inv_snailgun");
		sb_weapons[0][4] = Sbar_NewPic ("gfx/inv_rlaunch");
		sb_weapons[0][5] = Sbar_NewPic ("gfx/inv_srlaunch");
		sb_weapons[0][6] = Sbar_NewPic ("gfx/inv_lightng");

		sb_weapons[1][0] = Sbar_NewPic ("gfx/inv2_shotgun");
		sb_weapons[1][1] = Sbar_NewPic ("gfx/inv2_sshotgun");
		sb_weapons[1][2] = Sbar_NewPic ("gfx/inv2_nailgun");
		sb_weapons[1][3] = Sbar_NewPic ("gfx/inv2_snailgun");
		sb_weapons[1][4] = Sbar_NewPic ("gfx/inv2_rlaunch");
		sb_weapons[1][5] = Sbar_NewPic ("gfx/inv2_srlaunch");
		sb_weapons[1][6] = Sbar_NewPic ("gfx/inv2_lightng");

		for (i = 0;i < 5;i++)
		{
			sb_weapons[2+i][0] = Sbar_NewPic (va("gfx/inva%i_shotgun",i+1));
			sb_weapons[2+i][1] = Sbar_NewPic (va("gfx/inva%i_sshotgun",i+1));
			sb_weapons[2+i][2] = Sbar_NewPic (va("gfx/inva%i_nailgun",i+1));
			sb_weapons[2+i][3] = Sbar_NewPic (va("gfx/inva%i_snailgun",i+1));
			sb_weapons[2+i][4] = Sbar_NewPic (va("gfx/inva%i_rlaunch",i+1));
			sb_weapons[2+i][5] = Sbar_NewPic (va("gfx/inva%i_srlaunch",i+1));
			sb_weapons[2+i][6] = Sbar_NewPic (va("gfx/inva%i_lightng",i+1));
		}

		sb_ammo[0] = Sbar_NewPic ("gfx/sb_shells");
		sb_ammo[1] = Sbar_NewPic ("gfx/sb_nails");
		sb_ammo[2] = Sbar_NewPic ("gfx/sb_rocket");
		sb_ammo[3] = Sbar_NewPic ("gfx/sb_cells");

		sb_armor[0] = Sbar_NewPic ("gfx/sb_armor1");
		sb_armor[1] = Sbar_NewPic ("gfx/sb_armor2");
		sb_armor[2] = Sbar_NewPic ("gfx/sb_armor3");

		sb_items[0] = Sbar_NewPic ("gfx/sb_key1");
		sb_items[1] = Sbar_NewPic ("gfx/sb_key2");
		sb_items[2] = Sbar_NewPic ("gfx/sb_invis");
		sb_items[3] = Sbar_NewPic ("gfx/sb_invuln");
		sb_items[4] = Sbar_NewPic ("gfx/sb_suit");
		sb_items[5] = Sbar_NewPic ("gfx/sb_quad");

		sb_sigil[0] = Sbar_NewPic ("gfx/sb_sigil1");
		sb_sigil[1] = Sbar_NewPic ("gfx/sb_sigil2");
		sb_sigil[2] = Sbar_NewPic ("gfx/sb_sigil3");
		sb_sigil[3] = Sbar_NewPic ("gfx/sb_sigil4");

		sb_faces[4][0] = Sbar_NewPic ("gfx/face1");
		sb_faces[4][1] = Sbar_NewPic ("gfx/face_p1");
		sb_faces[3][0] = Sbar_NewPic ("gfx/face2");
		sb_faces[3][1] = Sbar_NewPic ("gfx/face_p2");
		sb_faces[2][0] = Sbar_NewPic ("gfx/face3");
		sb_faces[2][1] = Sbar_NewPic ("gfx/face_p3");
		sb_faces[1][0] = Sbar_NewPic ("gfx/face4");
		sb_faces[1][1] = Sbar_NewPic ("gfx/face_p4");
		sb_faces[0][0] = Sbar_NewPic ("gfx/face5");
		sb_faces[0][1] = Sbar_NewPic ("gfx/face_p5");

		sb_face_invis = Sbar_NewPic ("gfx/face_invis");
		sb_face_invuln = Sbar_NewPic ("gfx/face_invul2");
		sb_face_invis_invuln = Sbar_NewPic ("gfx/face_inv2");
		sb_face_quad = Sbar_NewPic ("gfx/face_quad");

		sb_sbar = Sbar_NewPic ("gfx/sbar");
		sb_ibar = Sbar_NewPic ("gfx/ibar");
		sb_scorebar = Sbar_NewPic ("gfx/scorebar");

	//MED 01/04/97 added new hipnotic weapons
		if (gamemode == GAME_HIPNOTIC)
		{
			hsb_weapons[0][0] = Sbar_NewPic ("gfx/inv_laser");
			hsb_weapons[0][1] = Sbar_NewPic ("gfx/inv_mjolnir");
			hsb_weapons[0][2] = Sbar_NewPic ("gfx/inv_gren_prox");
			hsb_weapons[0][3] = Sbar_NewPic ("gfx/inv_prox_gren");
			hsb_weapons[0][4] = Sbar_NewPic ("gfx/inv_prox");

			hsb_weapons[1][0] = Sbar_NewPic ("gfx/inv2_laser");
			hsb_weapons[1][1] = Sbar_NewPic ("gfx/inv2_mjolnir");
			hsb_weapons[1][2] = Sbar_NewPic ("gfx/inv2_gren_prox");
			hsb_weapons[1][3] = Sbar_NewPic ("gfx/inv2_prox_gren");
			hsb_weapons[1][4] = Sbar_NewPic ("gfx/inv2_prox");

			for (i = 0;i < 5;i++)
			{
				hsb_weapons[2+i][0] = Sbar_NewPic (va("gfx/inva%i_laser",i+1));
				hsb_weapons[2+i][1] = Sbar_NewPic (va("gfx/inva%i_mjolnir",i+1));
				hsb_weapons[2+i][2] = Sbar_NewPic (va("gfx/inva%i_gren_prox",i+1));
				hsb_weapons[2+i][3] = Sbar_NewPic (va("gfx/inva%i_prox_gren",i+1));
				hsb_weapons[2+i][4] = Sbar_NewPic (va("gfx/inva%i_prox",i+1));
			}

			hsb_items[0] = Sbar_NewPic ("gfx/sb_wsuit");
			hsb_items[1] = Sbar_NewPic ("gfx/sb_eshld");
		}
		else if (gamemode == GAME_ROGUE)
		{
			rsb_invbar[0] = Sbar_NewPic ("gfx/r_invbar1");
			rsb_invbar[1] = Sbar_NewPic ("gfx/r_invbar2");

			rsb_weapons[0] = Sbar_NewPic ("gfx/r_lava");
			rsb_weapons[1] = Sbar_NewPic ("gfx/r_superlava");
			rsb_weapons[2] = Sbar_NewPic ("gfx/r_gren");
			rsb_weapons[3] = Sbar_NewPic ("gfx/r_multirock");
			rsb_weapons[4] = Sbar_NewPic ("gfx/r_plasma");

			rsb_items[0] = Sbar_NewPic ("gfx/r_shield1");
			rsb_items[1] = Sbar_NewPic ("gfx/r_agrav1");

	// PGM 01/19/97 - team color border
			rsb_teambord = Sbar_NewPic ("gfx/r_teambord");
	// PGM 01/19/97 - team color border

			rsb_ammo[0] = Sbar_NewPic ("gfx/r_ammolava");
			rsb_ammo[1] = Sbar_NewPic ("gfx/r_ammomulti");
			rsb_ammo[2] = Sbar_NewPic ("gfx/r_ammoplasma");
		}
	}
}

void sbar_shutdown(void)
{
}

void sbar_newmap(void)
{
}

void Sbar_Init (void)
{
	Cmd_AddCommand ("+showscores", Sbar_ShowScores, "show scoreboard");
	Cmd_AddCommand ("-showscores", Sbar_DontShowScores, "hide scoreboard");
	Cvar_RegisterVariable (&showfps);
	Cvar_RegisterVariable (&showtime);
	Cvar_RegisterVariable (&showtime_format);
	Cvar_RegisterVariable (&showdate);
	Cvar_RegisterVariable (&showdate_format);
	Cvar_RegisterVariable (&sbar_alpha_bg);
	Cvar_RegisterVariable (&sbar_alpha_fg);
	Cvar_RegisterVariable (&cl_deathscoreboard);

	R_RegisterModule("sbar", sbar_start, sbar_shutdown, sbar_newmap);
}


//=============================================================================

// drawing routines are relative to the status bar location

int sbar_x, sbar_y;

/*
=============
Sbar_DrawPic
=============
*/
void Sbar_DrawPic (int x, int y, sbarpic_t *sbarpic)
{
	DrawQ_Pic (sbar_x + x, sbar_y + y, sbarpic->name, 0, 0, 1, 1, 1, sbar_alpha_fg.value, 0);
}

void Sbar_DrawAlphaPic (int x, int y, sbarpic_t *sbarpic, float alpha)
{
	DrawQ_Pic (sbar_x + x, sbar_y + y, sbarpic->name, 0, 0, 1, 1, 1, alpha, 0);
}

/*
================
Sbar_DrawCharacter

Draws one solid graphics character
================
*/
void Sbar_DrawCharacter (int x, int y, int num)
{
	DrawQ_String (sbar_x + x + 4 , sbar_y + y, va("%c", num), 0, 8, 8, 1, 1, 1, sbar_alpha_fg.value, 0);
}

/*
================
Sbar_DrawString
================
*/
void Sbar_DrawString (int x, int y, char *str)
{
	DrawQ_String (sbar_x + x, sbar_y + y, str, 0, 8, 8, 1, 1, 1, sbar_alpha_fg.value, 0);
}

/*
=============
Sbar_DrawNum
=============
*/
void Sbar_DrawNum (int x, int y, int num, int digits, int color)
{
	char str[32], *ptr;
	int l, frame;

	l = sprintf(str, "%i", num);
	ptr = str;
	if (l > digits)
		ptr += (l-digits);
	if (l < digits)
		x += (digits-l)*24;

	while (*ptr)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else
			frame = *ptr -'0';

		Sbar_DrawPic (x, y, sb_nums[color][frame]);
		x += 24;

		ptr++;
	}
}

/*
=============
Sbar_DrawXNum
=============
*/

void Sbar_DrawXNum (int x, int y, int num, int digits, int lettersize, float r, float g, float b, float a, int flags)
{
	char str[32], *ptr;
	int l, frame;

	l = sprintf(str, "%i", num);
	ptr = str;
	if (l > digits)
		ptr += (l-digits);
	if (l < digits)
		x += (digits-l) * lettersize;

	while (*ptr)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else
			frame = *ptr -'0';

		DrawQ_Pic (sbar_x + x, sbar_y + y, sb_nums[0][frame]->name,lettersize,lettersize,r,g,b,a * sbar_alpha_fg.value,flags);
		x += lettersize;

		ptr++;
	}
}

//=============================================================================


int Sbar_IsTeammatch()
{
	// currently only nexuiz uses the team score board
	return ((gamemode == GAME_NEXUIZ)
		&& (teamplay.integer > 0));
}

/*
===============
Sbar_SortFrags
===============
*/
static int fragsort[MAX_SCOREBOARD];
static int scoreboardlines;

//[515]: Sbar_GetPlayer for csqc "getplayerkey" func
int Sbar_GetPlayer (int index)
{
	if(index < 0)
	{
		index = -1-index;
		if(index >= scoreboardlines)
			return -1;
		index = fragsort[index];
	}
	if(index >= scoreboardlines)
		return -1;
	return index;
}

static scoreboard_t teams[MAX_SCOREBOARD];
static int teamsort[MAX_SCOREBOARD];
static int teamlines;
void Sbar_SortFrags (void)
{
	int i, j, k, color;

	// sort by frags
	scoreboardlines = 0;
	for (i=0 ; i<cl.maxclients ; i++)
	{
		if (cl.scores[i].name[0])
		{
			fragsort[scoreboardlines] = i;
			scoreboardlines++;
		}
	}

	for (i=0 ; i<scoreboardlines ; i++)
		for (j=0 ; j<scoreboardlines-1-i ; j++)
			if (cl.scores[fragsort[j]].frags < cl.scores[fragsort[j+1]].frags)
			{
				k = fragsort[j];
				fragsort[j] = fragsort[j+1];
				fragsort[j+1] = k;
			}

	teamlines = 0;
	if (Sbar_IsTeammatch ())
	{
		// now sort players by teams.
		for (i=0 ; i<scoreboardlines ; i++)
		{
			for (j=0 ; j<scoreboardlines-1-i ; j++)
			{
				if (cl.scores[fragsort[j]].colors < cl.scores[fragsort[j+1]].colors)
				{
					k = fragsort[j];
					fragsort[j] = fragsort[j+1];
					fragsort[j+1] = k;
				}
			}
		}

		// calculate team scores
		color = -1;
		for (i=0 ; i<scoreboardlines ; i++)
		{
			if (color != (cl.scores[fragsort[i]].colors & 15))
			{
				color = cl.scores[fragsort[i]].colors & 15;
				teamlines++;

				if (color == 4)
					strcpy(teams[teamlines-1].name, "^1Red Team");
				else if (color == 13)
					strcpy(teams[teamlines-1].name, "^4Blue Team");
				else if (color == 9)
					strcpy(teams[teamlines-1].name, "^6Pink Team");
				else if (color == 12)
					strcpy(teams[teamlines-1].name, "^3Yellow Team");
				else
					strcpy(teams[teamlines-1].name, "Total Team Score");

				teams[teamlines-1].frags = 0;
				teams[teamlines-1].colors = color + 16 * color;
			}

			if (cl.scores[fragsort[i]].frags != -666)
			{
				// do not add spedcators
				// (ugly hack for nexuiz)
				teams[teamlines-1].frags += cl.scores[fragsort[i]].frags;
			}
		}

		// now sort teams by scores.
		for (i=0 ; i<teamlines ; i++)
			teamsort[i] = i;
		for (i=0 ; i<teamlines ; i++)
		{
			for (j=0 ; j<teamlines-1-i ; j++)
			{
				if (teams[teamsort[j]].frags < teams[teamsort[j+1]].frags)
				{
					k = teamsort[j];
					teamsort[j] = teamsort[j+1];
					teamsort[j+1] = k;
				}
			}
		}
	}
}

/*
===============
Sbar_SoloScoreboard
===============
*/
void Sbar_SoloScoreboard (void)
{
	char	str[80];
	int		minutes, seconds, tens, units;
	int		l;

	if (gamemode != GAME_NEXUIZ) {
		sprintf (str,"Monsters:%3i /%3i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);
		Sbar_DrawString (8, 4, str);

		sprintf (str,"Secrets :%3i /%3i", cl.stats[STAT_SECRETS], cl.stats[STAT_TOTALSECRETS]);
		Sbar_DrawString (8, 12, str);
	}

// time
	minutes = cl.time / 60;
	seconds = cl.time - 60*minutes;
	tens = seconds / 10;
	units = seconds - 10*tens;
	sprintf (str,"Time :%3i:%i%i", minutes, tens, units);
	Sbar_DrawString (184, 4, str);

// draw level name
	if (gamemode == GAME_NEXUIZ) {
		l = (int) strlen (cl.worldmodel->name);
		Sbar_DrawString (232 - l*4, 12, cl.worldmodel->name);
	} else {
		l = (int) strlen (cl.levelname);
		Sbar_DrawString (232 - l*4, 12, cl.levelname);
	}
}

/*
===============
Sbar_DrawScoreboard
===============
*/
void Sbar_DrawScoreboard (void)
{
	Sbar_SoloScoreboard ();
	if (cl.gametype == GAME_DEATHMATCH)
		Sbar_DeathmatchOverlay ();
}

//=============================================================================

// AK to make DrawInventory smaller
static void Sbar_DrawWeapon(int nr, float fade, int active)
{
	// width = 300, height = 100
	const int w_width = 300, w_height = 100, w_space = 10;
	const float w_scale = 0.4;

	DrawQ_Pic(vid_conwidth.integer - (w_width + w_space) * w_scale, (w_height + w_space) * w_scale * nr + w_space, sb_weapons[0][nr]->name, w_width * w_scale, w_height * w_scale, (active) ? 1 : 0.6, active ? 1 : 0.6, active ? 1 : 1, fade * sbar_alpha_fg.value, DRAWFLAG_ADDITIVE);
	//DrawQ_String(vid_conwidth.integer - (w_space + font_size ), (w_height + w_space) * w_scale * nr + w_space, va("%i",nr+1), 0, font_size, font_size, 1, 0, 0, fade, 0);

	if (active)
		DrawQ_Fill(vid_conwidth.integer - (w_width + w_space) * w_scale, (w_height + w_space) * w_scale * nr + w_space, w_width * w_scale, w_height * w_scale, 0.3, 0.3, 0.3, fade * sbar_alpha_fg.value, DRAWFLAG_ADDITIVE);
}

/*
===============
Sbar_DrawInventory
===============
*/
void Sbar_DrawInventory (void)
{
	int i;
	char num[6];
	float time;
	int flashon;

	if (gamemode == GAME_ROGUE)
	{
		if ( cl.stats[STAT_ACTIVEWEAPON] >= RIT_LAVA_NAILGUN )
			Sbar_DrawAlphaPic (0, -24, rsb_invbar[0], sbar_alpha_bg.value);
		else
			Sbar_DrawAlphaPic (0, -24, rsb_invbar[1], sbar_alpha_bg.value);
	}
	else
		Sbar_DrawAlphaPic (0, -24, sb_ibar, sbar_alpha_bg.value);

	// weapons
	for (i=0 ; i<7 ; i++)
	{
		if (cl.stats[STAT_ITEMS] & (IT_SHOTGUN<<i) )
		{
			time = cl.item_gettime[i];
			flashon = (int)((cl.time - time)*10);
			if (flashon >= 10)
			{
				if ( cl.stats[STAT_ACTIVEWEAPON] == (IT_SHOTGUN<<i)  )
					flashon = 1;
				else
					flashon = 0;
			}
			else
				flashon = (flashon%5) + 2;

			Sbar_DrawAlphaPic (i*24, -16, sb_weapons[flashon][i], sbar_alpha_bg.value);
		}
	}

	// MED 01/04/97
	// hipnotic weapons
	if (gamemode == GAME_HIPNOTIC)
	{
		int grenadeflashing=0;
		for (i=0 ; i<4 ; i++)
		{
			if (cl.stats[STAT_ITEMS] & (1<<hipweapons[i]) )
			{
				time = cl.item_gettime[hipweapons[i]];
				flashon = (int)((cl.time - time)*10);
				if (flashon >= 10)
				{
					if ( cl.stats[STAT_ACTIVEWEAPON] == (1<<hipweapons[i])  )
						flashon = 1;
					else
						flashon = 0;
				}
				else
					flashon = (flashon%5) + 2;

				// check grenade launcher
				if (i==2)
				{
					if (cl.stats[STAT_ITEMS] & HIT_PROXIMITY_GUN)
					{
						if (flashon)
						{
							grenadeflashing = 1;
							Sbar_DrawPic (96, -16, hsb_weapons[flashon][2]);
						}
					}
				}
				else if (i==3)
				{
					if (cl.stats[STAT_ITEMS] & (IT_SHOTGUN<<4))
					{
						if (!grenadeflashing)
							Sbar_DrawPic (96, -16, hsb_weapons[flashon][3]);
					}
					else
						Sbar_DrawPic (96, -16, hsb_weapons[flashon][4]);
				}
				else
					Sbar_DrawPic (176 + (i*24), -16, hsb_weapons[flashon][i]);
			}
		}
	}

	if (gamemode == GAME_ROGUE)
	{
		// check for powered up weapon.
		if ( cl.stats[STAT_ACTIVEWEAPON] >= RIT_LAVA_NAILGUN )
			for (i=0;i<5;i++)
				if (cl.stats[STAT_ACTIVEWEAPON] == (RIT_LAVA_NAILGUN << i))
					Sbar_DrawPic ((i+2)*24, -16, rsb_weapons[i]);
	}

	// ammo counts
	for (i=0 ; i<4 ; i++)
	{
		sprintf (num, "%3i",cl.stats[STAT_SHELLS+i] );
		if (num[0] != ' ')
			Sbar_DrawCharacter ( (6*i+1)*8 - 2, -24, 18 + num[0] - '0');
		if (num[1] != ' ')
			Sbar_DrawCharacter ( (6*i+2)*8 - 2, -24, 18 + num[1] - '0');
		if (num[2] != ' ')
			Sbar_DrawCharacter ( (6*i+3)*8 - 2, -24, 18 + num[2] - '0');
	}

	// items
	for (i=0 ; i<6 ; i++)
		if (cl.stats[STAT_ITEMS] & (1<<(17+i)))
		{
			//MED 01/04/97 changed keys
			if (gamemode != GAME_HIPNOTIC || (i>1))
				Sbar_DrawPic (192 + i*16, -16, sb_items[i]);
		}

	//MED 01/04/97 added hipnotic items
	// hipnotic items
	if (gamemode == GAME_HIPNOTIC)
	{
		for (i=0 ; i<2 ; i++)
			if (cl.stats[STAT_ITEMS] & (1<<(24+i)))
				Sbar_DrawPic (288 + i*16, -16, hsb_items[i]);
	}

	if (gamemode == GAME_ROGUE)
	{
		// new rogue items
		for (i=0 ; i<2 ; i++)
			if (cl.stats[STAT_ITEMS] & (1<<(29+i)))
				Sbar_DrawPic (288 + i*16, -16, rsb_items[i]);
	}
	else
	{
		// sigils
		for (i=0 ; i<4 ; i++)
			if (cl.stats[STAT_ITEMS] & (1<<(28+i)))
				Sbar_DrawPic (320-32 + i*8, -16, sb_sigil[i]);
	}
}

//=============================================================================

/*
===============
Sbar_DrawFrags
===============
*/
void Sbar_DrawFrags (void)
{
	int i, k, l, x, f;
	char num[12];
	scoreboard_t *s;
	unsigned char *c;

	Sbar_SortFrags ();

	// draw the text
	l = min(scoreboardlines, 4);

	x = 23 * 8;

	for (i = 0;i < l;i++)
	{
		k = fragsort[i];
		s = &cl.scores[k];

		// draw background
		c = (unsigned char *)&palette_complete[(s->colors & 0xf0) + 8];
		DrawQ_Fill (sbar_x + x + 10, sbar_y     - 23, 28, 4, c[0] * (1.0f / 255.0f), c[1] * (1.0f / 255.0f), c[2] * (1.0f / 255.0f), c[3] * (1.0f / 255.0f) * sbar_alpha_fg.value, 0);
		c = (unsigned char *)&palette_complete[((s->colors & 15)<<4) + 8];
		DrawQ_Fill (sbar_x + x + 10, sbar_y + 4 - 23, 28, 3, c[0] * (1.0f / 255.0f), c[1] * (1.0f / 255.0f), c[2] * (1.0f / 255.0f), c[3] * (1.0f / 255.0f) * sbar_alpha_fg.value, 0);

		// draw number
		f = s->frags;
		sprintf (num, "%3i",f);

		if (k == cl.viewentity - 1)
		{
			Sbar_DrawCharacter ( x      + 2, -24, 16);
			Sbar_DrawCharacter ( x + 32 - 4, -24, 17);
		}
		Sbar_DrawCharacter (x +  8, -24, num[0]);
		Sbar_DrawCharacter (x + 16, -24, num[1]);
		Sbar_DrawCharacter (x + 24, -24, num[2]);
		x += 32;
	}
}

//=============================================================================


/*
===============
Sbar_DrawFace
===============
*/
void Sbar_DrawFace (void)
{
	int f;

// PGM 01/19/97 - team color drawing
// PGM 03/02/97 - fixed so color swatch only appears in CTF modes
	if (gamemode == GAME_ROGUE && !cl.islocalgame && (teamplay.integer > 3) && (teamplay.integer < 7))
	{
		char num[12];
		scoreboard_t *s;
		unsigned char *c;

		s = &cl.scores[cl.viewentity - 1];
		// draw background
		Sbar_DrawPic (112, 0, rsb_teambord);
		c = (unsigned char *)&palette_complete[(s->colors & 0xf0) + 8];
		DrawQ_Fill (sbar_x + 113, vid_conheight.integer-SBAR_HEIGHT+3, 22, 9, c[0] * (1.0f / 255.0f), c[1] * (1.0f / 255.0f), c[2] * (1.0f / 255.0f), c[3] * (1.0f / 255.0f) * sbar_alpha_fg.value, 0);
		c = (unsigned char *)&palette_complete[((s->colors & 15)<<4) + 8];
		DrawQ_Fill (sbar_x + 113, vid_conheight.integer-SBAR_HEIGHT+12, 22, 9, c[0] * (1.0f / 255.0f), c[1] * (1.0f / 255.0f), c[2] * (1.0f / 255.0f), c[3] * (1.0f / 255.0f) * sbar_alpha_fg.value, 0);

		// draw number
		f = s->frags;
		sprintf (num, "%3i",f);

		if ((s->colors & 0xf0)==0)
		{
			if (num[0] != ' ')
				Sbar_DrawCharacter(109, 3, 18 + num[0] - '0');
			if (num[1] != ' ')
				Sbar_DrawCharacter(116, 3, 18 + num[1] - '0');
			if (num[2] != ' ')
				Sbar_DrawCharacter(123, 3, 18 + num[2] - '0');
		}
		else
		{
			Sbar_DrawCharacter ( 109, 3, num[0]);
			Sbar_DrawCharacter ( 116, 3, num[1]);
			Sbar_DrawCharacter ( 123, 3, num[2]);
		}

		return;
	}
// PGM 01/19/97 - team color drawing

	if ( (cl.stats[STAT_ITEMS] & (IT_INVISIBILITY | IT_INVULNERABILITY) ) == (IT_INVISIBILITY | IT_INVULNERABILITY) )
		Sbar_DrawPic (112, 0, sb_face_invis_invuln);
	else if (cl.stats[STAT_ITEMS] & IT_QUAD)
		Sbar_DrawPic (112, 0, sb_face_quad );
	else if (cl.stats[STAT_ITEMS] & IT_INVISIBILITY)
		Sbar_DrawPic (112, 0, sb_face_invis );
	else if (cl.stats[STAT_ITEMS] & IT_INVULNERABILITY)
		Sbar_DrawPic (112, 0, sb_face_invuln);
	else
	{
		f = cl.stats[STAT_HEALTH] / 20;
		f = bound(0, f, 4);
		Sbar_DrawPic (112, 0, sb_faces[f][cl.time <= cl.faceanimtime]);
	}
}

void Sbar_ShowFPS(void)
{
	float fps_x, fps_y, fps_scalex, fps_scaley, fps_height;
	char fpsstring[32];
	char timestring[32];
	char datestring[32];
	qboolean red = false;
	fpsstring[0] = 0;
	timestring[0] = 0;
	datestring[0] = 0;
	if (showfps.integer)
	{
		float calc;
		if (showfps.integer > 1)
		{
			static double currtime, frametimes[32];
			double newtime, total;
			int count, i;
			static int framecycle = 0;

			newtime = Sys_DoubleTime();
			frametimes[framecycle] = newtime - currtime;
			total = 0;
			count = 0;
			while(total < 0.2 && count < 32 && frametimes[i = (framecycle - count) & 31])
			{
				total += frametimes[i];
				count++;
			}
			framecycle++;
			framecycle &= 31;
			if (showfps.integer == 2)
				calc = (((double)count / total) + 0.5);
			else // showfps 3, rapid update
				calc = ((1.0 / (newtime - currtime)) + 0.5);
			currtime = newtime;
		}
		else
		{
			static double nexttime = 0, lasttime = 0;
			static float framerate = 0;
			static int framecount = 0;
			double newtime;
			newtime = Sys_DoubleTime();
			if (newtime >= nexttime)
			{
				framerate = ((float)framecount / (newtime - lasttime) + 0.5);
				lasttime = newtime;
				nexttime = max(nexttime + 1, lasttime - 1);
				framecount = 0;
			}
			framecount++;
			calc = framerate;
		}

		if ((red = (calc < 1.0f)))
			dpsnprintf(fpsstring, sizeof(fpsstring), "%4i spf", (int)(1.0f / calc + 0.5));
		else
			dpsnprintf(fpsstring, sizeof(fpsstring), "%4i fps", (int)(calc + 0.5));
	}
	if (showtime.integer)
		strlcpy(timestring, Sys_TimeString(showtime_format.string), sizeof(timestring));
	if (showdate.integer)
		strlcpy(datestring, Sys_TimeString(showdate_format.string), sizeof(datestring));
	if (fpsstring[0] || timestring[0])
	{
		fps_scalex = 12;
		fps_scaley = 12;
		fps_height = fps_scaley * ((fpsstring[0] != 0) + (timestring[0] != 0) + (datestring[0] != 0));
		//fps_y = vid_conheight.integer - sb_lines; // yes this may draw over the sbar
		//fps_y = bound(0, fps_y, vid_conheight.integer - fps_height);
		fps_y = vid_conheight.integer - fps_height;
		if (fpsstring[0])
		{
			fps_x = vid_conwidth.integer - fps_scalex * strlen(fpsstring);
			DrawQ_Fill(fps_x, fps_y, fps_scalex * strlen(fpsstring), fps_scaley, 0, 0, 0, 0.5, 0);
			if (red)
				DrawQ_String(fps_x, fps_y, fpsstring, 0, fps_scalex, fps_scaley, 1, 0, 0, 1, 0);
			else
				DrawQ_String(fps_x, fps_y, fpsstring, 0, fps_scalex, fps_scaley, 1, 1, 1, 1, 0);
			fps_y += fps_scaley;
		}
		if (timestring[0])
		{
			fps_x = vid_conwidth.integer - fps_scalex * strlen(timestring);
			DrawQ_Fill(fps_x, fps_y, fps_scalex * strlen(timestring), fps_scaley, 0, 0, 0, 0.5, 0);
			DrawQ_String(fps_x, fps_y, timestring, 0, fps_scalex, fps_scaley, 1, 1, 1, 1, 0);
			fps_y += fps_scaley;
		}
		if (datestring[0])
		{
			fps_x = vid_conwidth.integer - fps_scalex * strlen(datestring);
			DrawQ_Fill(fps_x, fps_y, fps_scalex * strlen(datestring), fps_scaley, 0, 0, 0, 0.5, 0);
			DrawQ_String(fps_x, fps_y, datestring, 0, fps_scalex, fps_scaley, 1, 1, 1, 1, 0);
			fps_y += fps_scaley;
		}
	}
}

void Sbar_DrawGauge(float x, float y, const char *picname, float width, float height, float rangey, float rangeheight, float c1, float c2, float c1r, float c1g, float c1b, float c1a, float c2r, float c2g, float c2b, float c2a, float c3r, float c3g, float c3b, float c3a, int drawflags)
{
	float r[5];
	c2 = bound(0, c2, 1);
	c1 = bound(0, c1, 1 - c2);
	r[0] = 0;
	r[1] = rangey + rangeheight * (c2 + c1);
	r[2] = rangey + rangeheight * (c2);
	r[3] = rangey;
	r[4] = height;
	if (r[1] > r[0])
		DrawQ_SuperPic(x, y + r[0], picname, width, (r[1] - r[0]), 0,(r[0] / height), c3r,c3g,c3b,c3a, 1,(r[0] / height), c3r,c3g,c3b,c3a, 0,(r[1] / height), c3r,c3g,c3b,c3a, 1,(r[1] / height), c3r,c3g,c3b,c3a, drawflags);
	if (r[2] > r[1])
		DrawQ_SuperPic(x, y + r[1], picname, width, (r[2] - r[1]), 0,(r[1] / height), c1r,c1g,c1b,c1a, 1,(r[1] / height), c1r,c1g,c1b,c1a, 0,(r[2] / height), c1r,c1g,c1b,c1a, 1,(r[2] / height), c1r,c1g,c1b,c1a, drawflags);
	if (r[3] > r[2])
		DrawQ_SuperPic(x, y + r[2], picname, width, (r[3] - r[2]), 0,(r[2] / height), c2r,c2g,c2b,c2a, 1,(r[2] / height), c2r,c2g,c2b,c2a, 0,(r[3] / height), c2r,c2g,c2b,c2a, 1,(r[3] / height), c2r,c2g,c2b,c2a, drawflags);
	if (r[4] > r[3])
		DrawQ_SuperPic(x, y + r[3], picname, width, (r[4] - r[3]), 0,(r[3] / height), c3r,c3g,c3b,c3a, 1,(r[3] / height), c3r,c3g,c3b,c3a, 0,(r[4] / height), c3r,c3g,c3b,c3a, 1,(r[4] / height), c3r,c3g,c3b,c3a, drawflags);
}

/*
===============
Sbar_Draw
===============
*/
extern float v_dmg_time, v_dmg_roll, v_dmg_pitch;
extern cvar_t v_kicktime;
void Sbar_Draw (void)
{
	if(cl.csqc_vidvars.drawenginesbar)	//[515]: csqc drawsbar
	{
		if (cl.intermission == 1)
		{
			Sbar_IntermissionOverlay();
			return;
		}
		else if (cl.intermission == 2)
		{
			Sbar_FinaleOverlay();
			return;
		}

		if (gamemode == GAME_NETHERWORLD)
		{
		}
		else if (gamemode == GAME_SOM)
		{
			if (sb_showscores || (cl.stats[STAT_HEALTH] <= 0 && cl_deathscoreboard.integer))
				Sbar_DrawScoreboard ();
			else if (sb_lines)
			{
				// this is the top left of the sbar area
				sbar_x = 0;
				sbar_y = vid_conheight.integer - 24*3;

				// armor
				if (cl.stats[STAT_ARMOR])
				{
					if (cl.stats[STAT_ITEMS] & IT_ARMOR3)
						Sbar_DrawPic(0, 0, somsb_armor[2]);
					else if (cl.stats[STAT_ITEMS] & IT_ARMOR2)
						Sbar_DrawPic(0, 0, somsb_armor[1]);
					else if (cl.stats[STAT_ITEMS] & IT_ARMOR1)
						Sbar_DrawPic(0, 0, somsb_armor[0]);
					Sbar_DrawNum(24, 0, cl.stats[STAT_ARMOR], 3, cl.stats[STAT_ARMOR] <= 25);
				}

				// health
				Sbar_DrawPic(0, 24, somsb_health);
				Sbar_DrawNum(24, 24, cl.stats[STAT_HEALTH], 3, cl.stats[STAT_HEALTH] <= 25);

				// ammo icon
				if (cl.stats[STAT_ITEMS] & IT_SHELLS)
					Sbar_DrawPic(0, 48, somsb_ammo[0]);
				else if (cl.stats[STAT_ITEMS] & IT_NAILS)
					Sbar_DrawPic(0, 48, somsb_ammo[1]);
				else if (cl.stats[STAT_ITEMS] & IT_ROCKETS)
					Sbar_DrawPic(0, 48, somsb_ammo[2]);
				else if (cl.stats[STAT_ITEMS] & IT_CELLS)
					Sbar_DrawPic(0, 48, somsb_ammo[3]);
				Sbar_DrawNum(24, 48, cl.stats[STAT_AMMO], 3, false);
				if (cl.stats[STAT_SHELLS])
					Sbar_DrawNum(24 + 3*24, 48, cl.stats[STAT_SHELLS], 1, true);
			}
		}
		else if (gamemode == GAME_NEXUIZ)
		{
			sbar_y = vid_conheight.integer - 47;
			sbar_x = (vid_conwidth.integer - 640)/2;

			if (sb_showscores || (cl.stats[STAT_HEALTH] <= 0 && cl_deathscoreboard.integer))
			{
				Sbar_DrawAlphaPic (0, 0, sb_scorebar, sbar_alpha_bg.value);
				Sbar_DrawScoreboard ();
			}
			else if (sb_lines)
			{
				int i;
				double time;
				float fade;

				// we have a max time 2s (min time = 0)
				if ((time = cl.time - cl.weapontime) < 2)
				{
					fade = (1.0 - 0.5 * time);
					fade *= fade;
					for (i = 0; i < 8;i++)
						if (cl.stats[STAT_ITEMS] & (1 << i))
							Sbar_DrawWeapon(i + 1, fade, (i + 2 == cl.stats[STAT_ACTIVEWEAPON]));

					if((cl.stats[STAT_ITEMS] & (1<<12)))
						Sbar_DrawWeapon(0, fade, (cl.stats[STAT_ACTIVEWEAPON] == 1));
				}

				//if (!cl.islocalgame)
				//	Sbar_DrawFrags ();

				if (sb_lines > 24)
					Sbar_DrawAlphaPic (0, 0, sb_sbar, sbar_alpha_fg.value);
				else
					Sbar_DrawAlphaPic (0, 0, sb_sbar_minimal, sbar_alpha_fg.value);

				// special items
				if (cl.stats[STAT_ITEMS] & IT_INVULNERABILITY)
				{
					// Nexuiz has no anum pics
					//Sbar_DrawNum (36, 0, 666, 3, 1);
					// Nexuiz has no disc pic
					//Sbar_DrawPic (0, 0, sb_disc);
				}

				// armor
				Sbar_DrawXNum ((340-3*24), 12, cl.stats[STAT_ARMOR], 3, 24, 0.6,0.7,0.8,1,0);

				// health
				if(cl.stats[STAT_HEALTH] > 100)
					Sbar_DrawXNum((154-3*24),12,cl.stats[STAT_HEALTH],3,24,1,1,1,1,0);
				else if(cl.stats[STAT_HEALTH] <= 25 && cl.time - (int)cl.time > 0.5)
					Sbar_DrawXNum((154-3*24),12,cl.stats[STAT_HEALTH],3,24,0.7,0,0,1,0);
				else
					Sbar_DrawXNum((154-3*24),12,cl.stats[STAT_HEALTH],3,24,0.6,0.7,0.8,1,0);

				// AK dont draw ammo for the laser
				if(cl.stats[STAT_ACTIVEWEAPON] != 12)
				{
					if (cl.stats[STAT_ITEMS] & NEX_IT_SHELLS)
						Sbar_DrawPic (519, 0, sb_ammo[0]);
					else if (cl.stats[STAT_ITEMS] & NEX_IT_BULLETS)
						Sbar_DrawPic (519, 0, sb_ammo[1]);
					else if (cl.stats[STAT_ITEMS] & NEX_IT_ROCKETS)
						Sbar_DrawPic (519, 0, sb_ammo[2]);
					else if (cl.stats[STAT_ITEMS] & NEX_IT_CELLS)
						Sbar_DrawPic (519, 0, sb_ammo[3]);

					if(cl.stats[STAT_AMMO] <= 10)
						Sbar_DrawXNum ((519-3*24), 12, cl.stats[STAT_AMMO], 3, 24, 0.7, 0,0,1,0);
					else
						Sbar_DrawXNum ((519-3*24), 12, cl.stats[STAT_AMMO], 3, 24, 0.6, 0.7,0.8,1,0);

				}

				if (sb_lines > 24)
					DrawQ_Pic(sbar_x,sbar_y,sb_sbar_overlay->name,0,0,1,1,1,1,DRAWFLAG_MODULATE);
			}

			//if (vid_conwidth.integer > 320 && cl.gametype == GAME_DEATHMATCH)
			//	Sbar_MiniDeathmatchOverlay (0, 17);
		}
		else if (gamemode == GAME_ZYMOTIC)
		{
	#if 1
			float scale = 64.0f / 256.0f;
			float kickoffset[3];
			VectorClear(kickoffset);
			if (v_dmg_time > 0)
			{
				kickoffset[0] = (v_dmg_time/v_kicktime.value*v_dmg_roll) * 10 * scale;
				kickoffset[1] = (v_dmg_time/v_kicktime.value*v_dmg_pitch) * 10 * scale;
			}
			sbar_x = (vid_conwidth.integer - 256 * scale)/2 + kickoffset[0];
			sbar_y = (vid_conheight.integer - 256 * scale)/2 + kickoffset[1];
			// left1 16, 48 : 126 -66
			// left2 16, 128 : 196 -66
			// right 176, 48 : 196 -136
			Sbar_DrawGauge(sbar_x +  16 * scale, sbar_y +  48 * scale, zymsb_crosshair_left1->name, 64*scale,  80*scale, 78*scale,  -66*scale, cl.stats[STAT_AMMO]  * (1.0 / 200.0), cl.stats[STAT_SHELLS]  * (1.0 / 200.0), 0.8f,0.8f,0.0f,1.0f, 0.8f,0.5f,0.0f,1.0f, 0.3f,0.3f,0.3f,1.0f, DRAWFLAG_NORMAL);
			Sbar_DrawGauge(sbar_x +  16 * scale, sbar_y + 128 * scale, zymsb_crosshair_left2->name, 64*scale,  80*scale, 68*scale,  -66*scale, cl.stats[STAT_NAILS] * (1.0 / 200.0), cl.stats[STAT_ROCKETS] * (1.0 / 200.0), 0.8f,0.8f,0.0f,1.0f, 0.8f,0.5f,0.0f,1.0f, 0.3f,0.3f,0.3f,1.0f, DRAWFLAG_NORMAL);
			Sbar_DrawGauge(sbar_x + 176 * scale, sbar_y +  48 * scale, zymsb_crosshair_right->name, 64*scale, 160*scale, 148*scale, -136*scale, cl.stats[STAT_ARMOR]  * (1.0 / 300.0), cl.stats[STAT_HEALTH]  * (1.0 / 300.0), 0.0f,0.5f,1.0f,1.0f, 1.0f,0.0f,0.0f,1.0f, 0.3f,0.3f,0.3f,1.0f, DRAWFLAG_NORMAL);
			DrawQ_Pic(sbar_x + 120 * scale, sbar_y + 120 * scale, zymsb_crosshair_center->name, 16 * scale, 16 * scale, 1, 1, 1, 1, DRAWFLAG_NORMAL);
	#else
			float scale = 128.0f / 256.0f;
			float healthstart, healthheight, healthstarttc, healthendtc;
			float shieldstart, shieldheight, shieldstarttc, shieldendtc;
			float ammostart, ammoheight, ammostarttc, ammoendtc;
			float clipstart, clipheight, clipstarttc, clipendtc;
			float kickoffset[3], offset;
			VectorClear(kickoffset);
			if (v_dmg_time > 0)
			{
				kickoffset[0] = (v_dmg_time/v_kicktime.value*v_dmg_roll) * 10 * scale;
				kickoffset[1] = (v_dmg_time/v_kicktime.value*v_dmg_pitch) * 10 * scale;
			}
			sbar_x = (vid_conwidth.integer - 256 * scale)/2 + kickoffset[0];
			sbar_y = (vid_conheight.integer - 256 * scale)/2 + kickoffset[1];
			offset = 0; // TODO: offset should be controlled by recoil (question: how to detect firing?)
			DrawQ_SuperPic(sbar_x +  120           * scale, sbar_y + ( 88 - offset) * scale, zymsb_crosshair_line->name, 16 * scale, 36 * scale, 0,0, 1,1,1,1, 1,0, 1,1,1,1, 0,1, 1,1,1,1, 1,1, 1,1,1,1, 0);
			DrawQ_SuperPic(sbar_x + (132 + offset) * scale, sbar_y + 120            * scale, zymsb_crosshair_line->name, 36 * scale, 16 * scale, 0,1, 1,1,1,1, 0,0, 1,1,1,1, 1,1, 1,1,1,1, 1,0, 1,1,1,1, 0);
			DrawQ_SuperPic(sbar_x +  120           * scale, sbar_y + (132 + offset) * scale, zymsb_crosshair_line->name, 16 * scale, 36 * scale, 1,1, 1,1,1,1, 0,1, 1,1,1,1, 1,0, 1,1,1,1, 0,0, 1,1,1,1, 0);
			DrawQ_SuperPic(sbar_x + ( 88 - offset) * scale, sbar_y + 120            * scale, zymsb_crosshair_line->name, 36 * scale, 16 * scale, 1,0, 1,1,1,1, 1,1, 1,1,1,1, 0,0, 1,1,1,1, 0,1, 1,1,1,1, 0);
			healthheight = cl.stats[STAT_HEALTH] * (152.0f / 300.0f);
			shieldheight = cl.stats[STAT_ARMOR] * (152.0f / 300.0f);
			healthstart = 204 - healthheight;
			shieldstart = healthstart - shieldheight;
			healthstarttc = healthstart * (1.0f / 256.0f);
			healthendtc = (healthstart + healthheight) * (1.0f / 256.0f);
			shieldstarttc = shieldstart * (1.0f / 256.0f);
			shieldendtc = (shieldstart + shieldheight) * (1.0f / 256.0f);
			ammoheight = cl.stats[STAT_SHELLS] * (62.0f / 200.0f);
			ammostart = 114 - ammoheight;
			ammostarttc = ammostart * (1.0f / 256.0f);
			ammoendtc = (ammostart + ammoheight) * (1.0f / 256.0f);
			clipheight = cl.stats[STAT_AMMO] * (122.0f / 200.0f);
			clipstart = 190 - clipheight;
			clipstarttc = clipstart * (1.0f / 256.0f);
			clipendtc = (clipstart + clipheight) * (1.0f / 256.0f);
			if (healthheight > 0) DrawQ_SuperPic(sbar_x + 0 * scale, sbar_y + healthstart * scale, zymsb_crosshair_health->name, 256 * scale, healthheight * scale, 0,healthstarttc, 1.0f,0.0f,0.0f,1.0f, 1,healthstarttc, 1.0f,0.0f,0.0f,1.0f, 0,healthendtc, 1.0f,0.0f,0.0f,1.0f, 1,healthendtc, 1.0f,0.0f,0.0f,1.0f, DRAWFLAG_NORMAL);
			if (shieldheight > 0) DrawQ_SuperPic(sbar_x + 0 * scale, sbar_y + shieldstart * scale, zymsb_crosshair_health->name, 256 * scale, shieldheight * scale, 0,shieldstarttc, 0.0f,0.5f,1.0f,1.0f, 1,shieldstarttc, 0.0f,0.5f,1.0f,1.0f, 0,shieldendtc, 0.0f,0.5f,1.0f,1.0f, 1,shieldendtc, 0.0f,0.5f,1.0f,1.0f, DRAWFLAG_NORMAL);
			if (ammoheight > 0)   DrawQ_SuperPic(sbar_x + 0 * scale, sbar_y + ammostart   * scale, zymsb_crosshair_ammo->name,   256 * scale, ammoheight   * scale, 0,ammostarttc,   0.8f,0.8f,0.0f,1.0f, 1,ammostarttc,   0.8f,0.8f,0.0f,1.0f, 0,ammoendtc,   0.8f,0.8f,0.0f,1.0f, 1,ammoendtc,   0.8f,0.8f,0.0f,1.0f, DRAWFLAG_NORMAL);
			if (clipheight > 0)   DrawQ_SuperPic(sbar_x + 0 * scale, sbar_y + clipstart   * scale, zymsb_crosshair_clip->name,   256 * scale, clipheight   * scale, 0,clipstarttc,   1.0f,1.0f,0.0f,1.0f, 1,clipstarttc,   1.0f,1.0f,0.0f,1.0f, 0,clipendtc,   1.0f,1.0f,0.0f,1.0f, 1,clipendtc,   1.0f,1.0f,0.0f,1.0f, DRAWFLAG_NORMAL);
			DrawQ_Pic(sbar_x + 0 * scale, sbar_y + 0 * scale, zymsb_crosshair_background->name, 256 * scale, 256 * scale, 1, 1, 1, 1, DRAWFLAG_NORMAL);
			DrawQ_Pic(sbar_x + 120 * scale, sbar_y + 120 * scale, zymsb_crosshair_center->name, 16 * scale, 16 * scale, 1, 1, 1, 1, DRAWFLAG_NORMAL);
	#endif
		}
		else // Quake and others
		{
			sbar_y = vid_conheight.integer - SBAR_HEIGHT;
			if (cl.gametype == GAME_DEATHMATCH && gamemode != GAME_TRANSFUSION)
				sbar_x = 0;
			else
				sbar_x = (vid_conwidth.integer - 320)/2;

			if (sb_lines > 24)
			{
				if (gamemode != GAME_GOODVSBAD2)
					Sbar_DrawInventory ();
				if (!cl.islocalgame && gamemode != GAME_TRANSFUSION)
					Sbar_DrawFrags ();
			}

			if (sb_showscores || (cl.stats[STAT_HEALTH] <= 0 && cl_deathscoreboard.integer))
			{
				if (gamemode != GAME_GOODVSBAD2)
					Sbar_DrawAlphaPic (0, 0, sb_scorebar, sbar_alpha_bg.value);
				Sbar_DrawScoreboard ();
			}
			else if (sb_lines)
			{
				Sbar_DrawAlphaPic (0, 0, sb_sbar, sbar_alpha_bg.value);

				// keys (hipnotic only)
				//MED 01/04/97 moved keys here so they would not be overwritten
				if (gamemode == GAME_HIPNOTIC)
				{
					if (cl.stats[STAT_ITEMS] & IT_KEY1)
						Sbar_DrawPic (209, 3, sb_items[0]);
					if (cl.stats[STAT_ITEMS] & IT_KEY2)
						Sbar_DrawPic (209, 12, sb_items[1]);
				}
				// armor
				if (gamemode != GAME_GOODVSBAD2)
				{
					if (cl.stats[STAT_ITEMS] & IT_INVULNERABILITY)
					{
						Sbar_DrawNum (24, 0, 666, 3, 1);
						Sbar_DrawPic (0, 0, sb_disc);
					}
					else
					{
						if (gamemode == GAME_ROGUE)
						{
							Sbar_DrawNum (24, 0, cl.stats[STAT_ARMOR], 3, cl.stats[STAT_ARMOR] <= 25);
							if (cl.stats[STAT_ITEMS] & RIT_ARMOR3)
								Sbar_DrawPic (0, 0, sb_armor[2]);
							else if (cl.stats[STAT_ITEMS] & RIT_ARMOR2)
								Sbar_DrawPic (0, 0, sb_armor[1]);
							else if (cl.stats[STAT_ITEMS] & RIT_ARMOR1)
								Sbar_DrawPic (0, 0, sb_armor[0]);
						}
						else
						{
							Sbar_DrawNum (24, 0, cl.stats[STAT_ARMOR], 3, cl.stats[STAT_ARMOR] <= 25);
							if (cl.stats[STAT_ITEMS] & IT_ARMOR3)
								Sbar_DrawPic (0, 0, sb_armor[2]);
							else if (cl.stats[STAT_ITEMS] & IT_ARMOR2)
								Sbar_DrawPic (0, 0, sb_armor[1]);
							else if (cl.stats[STAT_ITEMS] & IT_ARMOR1)
								Sbar_DrawPic (0, 0, sb_armor[0]);
						}
					}
				}

				// face
				Sbar_DrawFace ();

				// health
				Sbar_DrawNum (154, 0, cl.stats[STAT_HEALTH], 3, cl.stats[STAT_HEALTH] <= 25);

				// ammo icon
				if (gamemode == GAME_ROGUE)
				{
					if (cl.stats[STAT_ITEMS] & RIT_SHELLS)
						Sbar_DrawPic (224, 0, sb_ammo[0]);
					else if (cl.stats[STAT_ITEMS] & RIT_NAILS)
						Sbar_DrawPic (224, 0, sb_ammo[1]);
					else if (cl.stats[STAT_ITEMS] & RIT_ROCKETS)
						Sbar_DrawPic (224, 0, sb_ammo[2]);
					else if (cl.stats[STAT_ITEMS] & RIT_CELLS)
						Sbar_DrawPic (224, 0, sb_ammo[3]);
					else if (cl.stats[STAT_ITEMS] & RIT_LAVA_NAILS)
						Sbar_DrawPic (224, 0, rsb_ammo[0]);
					else if (cl.stats[STAT_ITEMS] & RIT_PLASMA_AMMO)
						Sbar_DrawPic (224, 0, rsb_ammo[1]);
					else if (cl.stats[STAT_ITEMS] & RIT_MULTI_ROCKETS)
						Sbar_DrawPic (224, 0, rsb_ammo[2]);
				}
				else
				{
					if (cl.stats[STAT_ITEMS] & IT_SHELLS)
						Sbar_DrawPic (224, 0, sb_ammo[0]);
					else if (cl.stats[STAT_ITEMS] & IT_NAILS)
						Sbar_DrawPic (224, 0, sb_ammo[1]);
					else if (cl.stats[STAT_ITEMS] & IT_ROCKETS)
						Sbar_DrawPic (224, 0, sb_ammo[2]);
					else if (cl.stats[STAT_ITEMS] & IT_CELLS)
						Sbar_DrawPic (224, 0, sb_ammo[3]);
				}

				Sbar_DrawNum (248, 0, cl.stats[STAT_AMMO], 3, cl.stats[STAT_AMMO] <= 10);

			}

			if (vid_conwidth.integer > 320 && cl.gametype == GAME_DEATHMATCH)
			{
				if (gamemode == GAME_TRANSFUSION)
					Sbar_MiniDeathmatchOverlay (0, 0);
				else
					Sbar_MiniDeathmatchOverlay (324, vid_conheight.integer - sb_lines);
			}
		}
	}

	Sbar_ShowFPS();

	if(cl.csqc_vidvars.drawcrosshair)
		R_Draw2DCrosshair();

	if (cl_prydoncursor.integer)
		DrawQ_Pic((cl.cmd.cursor_screen[0] + 1) * 0.5 * vid_conwidth.integer, (cl.cmd.cursor_screen[1] + 1) * 0.5 * vid_conheight.integer, va("gfx/prydoncursor%03i", cl_prydoncursor.integer), 0, 0, 1, 1, 1, 1, 0);
}

//=============================================================================

/*
==================
Sbar_DeathmatchOverlay

==================
*/
float Sbar_PrintScoreboardItem(scoreboard_t *s, float x, float y)
{
	int minutes;
	unsigned char *c;
	if (cls.protocol == PROTOCOL_QUAKEWORLD)
	{
		// draw colors behind score
		c = (unsigned char *)&palette_complete[(s->colors & 0xf0) + 8];
		DrawQ_Fill(x + 14*8, y+1, 32, 3, c[0] * (1.0f / 255.0f), c[1] * (1.0f / 255.0f), c[2] * (1.0f / 255.0f), c[3] * (1.0f / 255.0f) * sbar_alpha_fg.value, 0);
		c = (unsigned char *)&palette_complete[((s->colors & 15)<<4) + 8];
		DrawQ_Fill(x + 14*8, y+4, 32, 3, c[0] * (1.0f / 255.0f), c[1] * (1.0f / 255.0f), c[2] * (1.0f / 255.0f), c[3] * (1.0f / 255.0f) * sbar_alpha_fg.value, 0);
		// print the text
		//DrawQ_String(x, y, va("%c%4i %s", (s - cl.scores) == cl.playerentity - 1 ? 13 : ' ', (int) s->frags, s->name), 0, 8, 8, 1, 1, 1, 1 * sbar_alpha_fg.value, 0);
		DrawQ_ColoredString(x, y, va("%c%4i %2i %4i %4i %-4s %s", (s - cl.scores) == cl.playerentity - 1 ? 13 : ' ', bound(0, s->qw_ping, 9999), bound(0, s->qw_packetloss, 99), minutes,(int) s->frags, cl.qw_teamplay ? s->qw_team : "", s->name), 0, 8, 8, 1, 1, 1, 1 * sbar_alpha_fg.value, 0, NULL );
	}
	else
	{
		minutes = (int)((cl.intermission ? cl.completed_time - s->qw_entertime : realtime - s->qw_entertime) / 60.0);
		// draw colors behind score
		c = (unsigned char *)&palette_complete[(s->colors & 0xf0) + 8];
		DrawQ_Fill(x + 1*8, y+1, 32, 3, c[0] * (1.0f / 255.0f), c[1] * (1.0f / 255.0f), c[2] * (1.0f / 255.0f), c[3] * (1.0f / 255.0f) * sbar_alpha_fg.value, 0);
		c = (unsigned char *)&palette_complete[((s->colors & 15)<<4) + 8];
		DrawQ_Fill(x + 1*8, y+4, 32, 3, c[0] * (1.0f / 255.0f), c[1] * (1.0f / 255.0f), c[2] * (1.0f / 255.0f), c[3] * (1.0f / 255.0f) * sbar_alpha_fg.value, 0);
		// print the text
		//DrawQ_String(x, y, va("%c%4i %s", (s - cl.scores) == cl.playerentity - 1 ? 13 : ' ', (int) s->frags, s->name), 0, 8, 8, 1, 1, 1, 1 * sbar_alpha_fg.value, 0);
		DrawQ_ColoredString(x, y, va("%c%4i %s", (s - cl.scores) == cl.playerentity - 1 ? 13 : ' ', (int) s->frags, s->name), 0, 8, 8, 1, 1, 1, 1 * sbar_alpha_fg.value, 0, NULL );
	}
	return 8;
}

void Sbar_DeathmatchOverlay (void)
{
	int i, x, y;
	cachepic_t *pic;

	// request new ping times every two second
	if (cl.last_ping_request < realtime - 2)
	{
		cl.last_ping_request = realtime;
		if (cls.protocol == PROTOCOL_QUAKEWORLD)
		{
			MSG_WriteByte(&cls.netcon->message, qw_clc_stringcmd);
			MSG_WriteString(&cls.netcon->message, "pings");
		}
	}

	pic = Draw_CachePic ("gfx/ranking", true);
	DrawQ_Pic ((vid_conwidth.integer - pic->width)/2, 8, "gfx/ranking", 0, 0, 1, 1, 1, 1 * sbar_alpha_fg.value, 0);

	// scores
	Sbar_SortFrags ();
	// draw the text
	if (cls.protocol == PROTOCOL_QUAKEWORLD)
		x = (vid_conwidth.integer - (6 + 17 + 15) * 8) / 2;
	else
		x = (vid_conwidth.integer - (6 + 15) * 8) / 2;
	y = 40;

	if (Sbar_IsTeammatch ())
	{
		// show team scores first
		for (i = 0;i < teamlines && y < vid_conheight.integer;i++)
			y += Sbar_PrintScoreboardItem((teams + teamsort[i]), x, y);
		y += 5;
	}

	for (i = 0;i < scoreboardlines && y < vid_conheight.integer;i++)
		y += Sbar_PrintScoreboardItem(cl.scores + fragsort[i], x, y);
}

/*
==================
Sbar_DeathmatchOverlay

==================
*/
void Sbar_MiniDeathmatchOverlay (int x, int y)
{
	int i, numlines;

	// decide where to print
	if (gamemode == GAME_TRANSFUSION)
		numlines = (vid_conwidth.integer - x + 127) / 128;
	else
		numlines = (vid_conheight.integer - y + 7) / 8;

	// give up if there isn't room
	if (x >= vid_conwidth.integer || y >= vid_conheight.integer || numlines < 1)
		return;

	// scores
	Sbar_SortFrags ();

	//find us
	for (i = 0; i < scoreboardlines; i++)
		if (fragsort[i] == cl.playerentity - 1)
			break;

	// figure out start
	i -= numlines/2;
	i = min(i, scoreboardlines - numlines);
	i = max(i, 0);

	if (gamemode == GAME_TRANSFUSION)
	{
		for (;i < scoreboardlines && x < vid_conwidth.integer;i++)
			x += 128 + Sbar_PrintScoreboardItem(cl.scores + fragsort[i], x, y);
	}
	else
	{
		for (;i < scoreboardlines && y < vid_conheight.integer;i++)
			y += Sbar_PrintScoreboardItem(cl.scores + fragsort[i], x, y);
	}
}

/*
==================
Sbar_IntermissionOverlay

==================
*/
void Sbar_IntermissionOverlay (void)
{
	int		dig;
	int		num;

	if (cl.gametype == GAME_DEATHMATCH)
	{
		Sbar_DeathmatchOverlay ();
		return;
	}

	sbar_x = (vid_conwidth.integer - 320) >> 1;
	sbar_y = (vid_conheight.integer - 200) >> 1;

	DrawQ_Pic (sbar_x + 64, sbar_y + 24, "gfx/complete", 0, 0, 1, 1, 1, 1 * sbar_alpha_fg.value, 0);
	DrawQ_Pic (sbar_x + 0, sbar_y + 56, "gfx/inter", 0, 0, 1, 1, 1, 1 * sbar_alpha_fg.value, 0);

// time
	dig = cl.completed_time/60;
	Sbar_DrawNum (160, 64, dig, 3, 0);
	num = cl.completed_time - dig*60;
	if (gamemode != GAME_NEXUIZ)
		Sbar_DrawPic (234,64,sb_colon);
	Sbar_DrawPic (246,64,sb_nums[0][num/10]);
	Sbar_DrawPic (266,64,sb_nums[0][num%10]);

	Sbar_DrawNum (160, 104, cl.stats[STAT_SECRETS], 3, 0);
	if (gamemode != GAME_NEXUIZ)
		Sbar_DrawPic (232, 104, sb_slash);
	Sbar_DrawNum (240, 104, cl.stats[STAT_TOTALSECRETS], 3, 0);

	Sbar_DrawNum (160, 144, cl.stats[STAT_MONSTERS], 3, 0);
	if (gamemode != GAME_NEXUIZ)
		Sbar_DrawPic (232, 144, sb_slash);
	Sbar_DrawNum (240, 144, cl.stats[STAT_TOTALMONSTERS], 3, 0);

}


/*
==================
Sbar_FinaleOverlay

==================
*/
void Sbar_FinaleOverlay (void)
{
	cachepic_t	*pic;

	pic = Draw_CachePic ("gfx/finale", true);
	DrawQ_Pic((vid_conwidth.integer - pic->width)/2, 16, "gfx/finale", 0, 0, 1, 1, 1, 1 * sbar_alpha_fg.value, 0);
}

