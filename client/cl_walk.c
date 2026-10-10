/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2026 Jonathan Ferguson

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
// cl_walk.c -- a camera carried through a map on its own, for training data
//
//	pt_walk <map> [frames] [paths a pixel] [frames a second] [quit]
//
// loads the map as a game of one and renders that many frames of it as
// pt_render does a demo (so with pt_render_export set, with their planes),
// from a camera that is carried about the map by itself. It goes in bursts
// of a few frames each, as a demo plays them: from a place taken from the
// map's own entities (where the player starts, where things lie, where
// monsters stand, where the lights are), towards another in sight of it,
// looking some way that is not straight into a wall, with a field of view
// of its own, and between bursts it jumps elsewhere. A burst may set the
// map's flickering lights to a moment of their own and put a few lights of
// its own about the camera, as a flash or a bolt would. The weapon in hand
// and the status bar are left out. The same map gives the same walk every
// time; see cl_render.c for the rest.

#include "client.h"

#define	WALK_BURST			8		// frames to a burst
#define	MAX_WALK_POINTS		4096
#define	MAX_WALK_LIGHTS		3

static vec3_t	walk_points[MAX_WALK_POINTS];
static int		walk_numpoints;
static uint32_t	walk_base, walk_seed;
static qboolean	walk_active;
static float	walk_gun, walk_hud;		// what cl_gun and pt_render_hud were

// the burst under way
static int		walk_burst = -1;
static vec3_t	walk_from, walk_dir;
static float	walk_speed;
static float	walk_yaw, walk_yaw_rate, walk_pitch, walk_pitch_rate, walk_fov;
static int		walk_numlights;
static vec3_t	walk_light_at[MAX_WALK_LIGHTS];
static float	walk_light_colour[MAX_WALK_LIGHTS][3], walk_light_strength[MAX_WALK_LIGHTS];

static uint32_t Walk_Rand (void)
{
	walk_seed = walk_seed * 1664525u + 1013904223u;
	return walk_seed >> 8;
}

static float Walk_Between (float a, float b)
{
	return a + (b - a) * ((Walk_Rand () & 0xffffff) / 16777216.0f);
}

// is there nothing solid between the two, with a little room on each side?
static qboolean Walk_Clear (vec3_t a, vec3_t b)
{
	vec3_t	mins = {-8, -8, -8}, maxs = {8, 8, 8};
	trace_t	t;

	t = CM_BoxTrace (a, b, mins, maxs, 0, MASK_SOLID);
	return t.fraction >= 1 && !t.startsolid && !t.allsolid;
}

/*
===============
Walk_Points

Everywhere the map puts something: a camera can stand a little above each
===============
*/
static void Walk_Points (void)
{
	char		*ents, *token, key[64], classname[64];
	vec3_t		org, down, mins = {0, 0, 0}, maxs = {0, 0, 0};
	qboolean	have;

	walk_numpoints = 0;
	ents = CM_EntityString ();
	while (ents && walk_numpoints < MAX_WALK_POINTS)
	{
		token = COM_Parse (&ents);
		if (!ents || token[0] != '{')
			break;
		have = false;
		classname[0] = 0;
		while (ents)
		{
			token = COM_Parse (&ents);
			if (!ents || token[0] == '}')
				break;
			strncpy (key, token, sizeof(key) - 1);
			key[sizeof(key) - 1] = 0;
			token = COM_Parse (&ents);
			if (!ents)
				break;
			if (!strcmp (key, "origin") && sscanf (token, "%f %f %f", &org[0], &org[1], &org[2]) == 3)
				have = true;
			else if (!strcmp (key, "classname"))
			{
				strncpy (classname, token, sizeof(classname) - 1);
				classname[sizeof(classname) - 1] = 0;
			}
		}
		// what stands or lies on the floor, where a player could be: not
		// lights, which hang anywhere, nor what has no place of its own
		if (!have || (strncmp (classname, "info_player", 11) && strncmp (classname, "item_", 5)
			&& strncmp (classname, "weapon_", 7) && strncmp (classname, "ammo_", 5)
			&& strncmp (classname, "monster_", 8) && strncmp (classname, "misc_", 5)
			&& strncmp (classname, "path_corner", 11) && strncmp (classname, "point_combat", 12)))
			continue;
		org[2] += 24;
		if (CM_PointContents (org, 0) & CONTENTS_SOLID)
			continue;
		// with a floor not far below
		VectorSet (down, org[0], org[1], org[2] - 128);
		if (CM_BoxTrace (org, down, mins, maxs, 0, MASK_SOLID).fraction >= 1)
			continue;
		// Inside the map, not in the void around it: a map is closed, so
		// from within it there is something in every direction
		{
			static vec3_t		ways[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
			vec3_t				far, mins = {0, 0, 0}, maxs = {0, 0, 0};
			int					k;

			for (k=0 ; k<6 ; k++)
			{
				VectorMA (org, 16384, ways[k], far);
				if (CM_BoxTrace (org, far, mins, maxs, 0, MASK_SOLID).fraction >= 1)
					break;
			}
			if (k < 6)
				continue;
		}
		VectorCopy (org, walk_points[walk_numpoints]);
		walk_numpoints++;
	}
}

/*
===============
Walk_Burst

Where the next burst goes, how it looks and what lights it brings
===============
*/
static void Walk_Burst (int burst, int fps)
{
	vec3_t	angles, forward, to, at;
	float	dist, most;
	int		i, tries;

	walk_burst = burst;
	walk_seed = walk_base ^ ((uint32_t)burst * 2654435761u);
	for (i=0 ; i<4 ; i++)
		Walk_Rand ();

	// from one of the map's places towards another in sight of it, or
	// failing that, turning where it is
	i = Walk_Rand () % walk_numpoints;
	VectorCopy (walk_points[i], walk_from);
	VectorClear (walk_dir);
	walk_speed = 0;
	for (tries=0 ; tries<20 ; tries++)
	{
		float	*p = walk_points[Walk_Rand () % walk_numpoints];

		VectorSubtract (p, walk_from, to);
		dist = VectorLength (to);
		if (dist < 32 || dist > 600 || !Walk_Clear (walk_from, p))
			continue;
		VectorScale (to, 1 / dist, walk_dir);
		walk_speed = Walk_Between (60, 280);
		// no further than there in the burst
		most = dist * fps / WALK_BURST;
		if (walk_speed > most)
			walk_speed = most;
		break;
	}

	// a way to look that is not into a wall or a corner close by: room
	// ahead and to each side of the view
	for (tries=0 ; tries<16 ; tries++)
	{
		int		k;

		walk_yaw = Walk_Between (0, 360);
		walk_pitch = Walk_Between (-25, 25);
		for (k=0 ; k<5 ; k++)
		{
			VectorSet (angles, walk_pitch + (k == 1 ? -30 : (k == 2 ? 30 : 0)), walk_yaw + (k == 3 ? -35 : (k == 4 ? 35 : 0)), 0);
			AngleVectors (angles, forward, NULL, NULL);
			VectorMA (walk_from, k == 0 ? 192 : 128, forward, at);
			if (!Walk_Clear (walk_from, at))
				break;
		}
		if (k == 5)
			break;
	}
	walk_yaw_rate = Walk_Between (-60, 60);
	walk_pitch_rate = Walk_Between (-15, 15);
	walk_fov = Walk_Between (75, 110);

	// the map's flickering lights at a moment of their own
	Cvar_SetValue ("cl_lightstyle_offset", (float)(Walk_Rand () % 30000));

	// lights of its own, half the time: about the camera, in the open
	walk_numlights = 0;
	if (Walk_Between (0, 1) < 0.5f)
	{
		const int want = 1 + Walk_Rand () % MAX_WALK_LIGHTS;
		for (i=0 ; i<want ; i++)
		{
			float	*c = walk_light_colour[walk_numlights];
			float	top;

			VectorSet (angles, Walk_Between (-60, 60), Walk_Between (0, 360), 0);
			AngleVectors (angles, forward, NULL, NULL);
			VectorMA (walk_from, Walk_Between (40, 250), forward, at);
			if (!Walk_Clear (walk_from, at))
				continue;
			VectorCopy (at, walk_light_at[walk_numlights]);
			// warm, cool or white, the brightest part full
			c[0] = Walk_Between (0.3f, 1);
			c[1] = Walk_Between (0.3f, 1);
			c[2] = Walk_Between (0.3f, 1);
			top = c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
			VectorScale (c, 1 / top, c);
			walk_light_strength[walk_numlights] = Walk_Between (150, 350);
			walk_numlights++;
		}
	}
}

/*
===============
CL_WalkBegin

The map is up: find its places and get out of the way
===============
*/
void CL_WalkBegin (const char *map)
{
	const char	*s;

	walk_base = 2166136261u;
	for (s=map ; *s ; s++)
		walk_base = (walk_base ^ (uint32_t)(unsigned char)*s) * 16777619u;
	Walk_Points ();
	if (!walk_numpoints)
	{
		// a map with nothing in it: from where the player stands
		walk_points[0][0] = cl.frame.playerstate.pmove.origin[0] * 0.125f;
		walk_points[0][1] = cl.frame.playerstate.pmove.origin[1] * 0.125f;
		walk_points[0][2] = cl.frame.playerstate.pmove.origin[2] * 0.125f + 22;
		walk_numpoints = 1;
	}
	walk_burst = -1;
	walk_gun = Cvar_VariableValue ("cl_gun");
	walk_hud = Cvar_VariableValue ("pt_render_hud");
	Cvar_SetValue ("cl_gun", 0);
	Cvar_SetValue ("pt_render_hud", 0);
	Cbuf_AddText ("god\nnotarget\n");
	walk_active = true;
	Com_Printf ("Walking %s: %i places\n", map, walk_numpoints);
}

/*
===============
CL_WalkFrame

Before a frame is rendered: where the camera is for it
===============
*/
void CL_WalkFrame (int frame, int fps)
{
	const int	burst = frame / WALK_BURST, in = frame % WALK_BURST;
	const float	t = (float)in / fps;
	vec3_t		at;
	float		pitch, yaw;
	int			i;

	if (!walk_active)
		return;
	if (burst != walk_burst)
		Walk_Burst (burst, fps);

	VectorMA (walk_from, walk_speed * t, walk_dir, at);
	yaw = walk_yaw + walk_yaw_rate * t;
	pitch = walk_pitch + walk_pitch_rate * t;
	if (pitch > 80)
		pitch = 80;
	if (pitch < -80)
		pitch = -80;
	Cvar_Set ("pt_camera", va("%.3f %.3f %.3f %.3f %.3f %.3f", at[0], at[1], at[2], pitch, yaw, walk_fov));

	for (i=0 ; i<walk_numlights ; i++)
	{
		cdlight_t	*dl = CL_AllocDlight (-1 - i);

		VectorCopy (walk_light_at[i], dl->origin);
		dl->radius = walk_light_strength[i];
		dl->color[0] = walk_light_colour[i][0];
		dl->color[1] = walk_light_colour[i][1];
		dl->color[2] = walk_light_colour[i][2];
		dl->die = cl.time + 1000 / fps + 1;
	}
}

void CL_WalkEnd (void)
{
	if (!walk_active)
		return;
	walk_active = false;
	Cvar_Set ("pt_camera", "");
	Cvar_SetValue ("cl_lightstyle_offset", 0);
	Cvar_SetValue ("cl_gun", walk_gun);
	Cvar_SetValue ("pt_render_hud", walk_hud);
}
