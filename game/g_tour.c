/*
Copyright (C) 1997-2001 Id Software, Inc.

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
// g_tour.c -- the player taken round a map along a path from a file
//
// For making pictures without anyone playing: with the cvar tour_file set
// when the player enters a map (cheats must be allowed), the player is put
// at each place in the file in turn, one a server frame, looking the way it
// says, and can fire as he goes. He cannot be hurt and passes through
// everything.
//
// The file is text. A line is
//     x y z pitch yaw [fire [clip]]
// where x y z is the eye, and fire is 0 for nothing or 1-6 for a shot from
// the blaster, rocket launcher, grenade launcher, railgun, hyperblaster or
// BFG. A line beginning with # is skipped.
//
// clip numbers the stretches of the tour that are wanted, from 1; 0 is the
// way between them. On coming to a stretch the console command in the cvar
// tour_clip_begin is run, with the first %d in it replaced by the number, and
// on leaving it the one in tour_clip_end: they are how a tour starts and
// stops a film being made of it (pt_offline, see ref_pt/rpt_offline.c).
// tour_end is run when the file runs out; tour_notarget keeps monsters from
// noticing the player.

#include "g_local.h"

#define	TOUR_SETTLE		8		// server frames at the first place before setting off

typedef struct
{
	vec3_t	eye;
	float	pitch, yaw;
	int		fire;
	int		clip;
} tourpoint_t;

static tourpoint_t	*tour_points;
static int			tour_count;
static int			tour_at;		// under 0 while settling
static int			tour_clip;		// the stretch the last frame was in
static edict_t		*tour_ent;

qboolean Tour_Active (edict_t *ent)
{
	return tour_points && ent == tour_ent;
}

static void Tour_Free (void)
{
	if (tour_points)
		gi.TagFree (tour_points);
	tour_points = NULL;
	tour_count = 0;
	tour_ent = NULL;
}

/*
==================
Tour_Begin

Called when the player enters a map
==================
*/
void Tour_Begin (edict_t *ent)
{
	cvar_t		*file;
	FILE		*f;
	char		line[256];
	tourpoint_t	p;
	int			room;

	Tour_Free ();

	file = gi.cvar ("tour_file", "", 0);
	if (!file->string[0])
		return;
	if (deathmatch->value && !sv_cheats->value)
	{
		gi.cprintf (ent, PRINT_HIGH, "A tour needs '+set cheats 1'.\n");
		return;
	}
	f = fopen (file->string, "r");
	if (!f)
	{
		gi.cprintf (ent, PRINT_HIGH, "Couldn't open %s\n", file->string);
		return;
	}

	room = 0;
	while (fgets (line, sizeof(line), f))
	{
		if (line[0] == '#')
			continue;
		p.fire = 0;
		p.clip = 0;
		if (sscanf (line, "%f %f %f %f %f %d %d", &p.eye[0], &p.eye[1], &p.eye[2], &p.pitch, &p.yaw, &p.fire, &p.clip) < 5)
			continue;
		if (tour_count == room)
		{
			tourpoint_t	*bigger;

			room = room ? room * 2 : 1024;
			bigger = gi.TagMalloc (room * sizeof(tourpoint_t), TAG_LEVEL);
			if (tour_points)
			{
				memcpy (bigger, tour_points, tour_count * sizeof(tourpoint_t));
				gi.TagFree (tour_points);
			}
			tour_points = bigger;
		}
		tour_points[tour_count++] = p;
	}
	fclose (f);

	if (!tour_count)
	{
		gi.cprintf (ent, PRINT_HIGH, "Nothing to follow in %s\n", file->string);
		Tour_Free ();
		return;
	}

	tour_ent = ent;
	tour_at = -TOUR_SETTLE;
	tour_clip = 0;
	ent->movetype = MOVETYPE_NOCLIP;
	ent->solid = SOLID_NOT;
	ent->flags |= FL_GODMODE;
	if (gi.cvar ("tour_notarget", "0", 0)->value)
		ent->flags |= FL_NOTARGET;
	gi.cprintf (ent, PRINT_HIGH, "Tour: %d places from %s\n", tour_count, file->string);
}

// the level is going away, and what was allocated with it
void Tour_End (void)
{
	tour_points = NULL;
	tour_count = 0;
	tour_ent = NULL;
}

// runs the console command a cvar holds, a number in place of its first %d
static void Tour_Command (const char *cvar, int number)
{
	char	*command, *mark;
	char	text[512];

	command = gi.cvar ((char *)cvar, "", 0)->string;
	if (!command[0] || strlen (command) > sizeof(text) - 32)
		return;
	mark = strstr (command, "%d");
	if (mark)
		Com_sprintf (text, sizeof(text), "%.*s%d%s\n", (int)(mark - command), command, number, mark + 2);
	else
		Com_sprintf (text, sizeof(text), "%s\n", command);
	gi.AddCommandString (text);
}

static void Tour_Fire (edict_t *ent, const tourpoint_t *p)
{
	vec3_t	angles, forward, right, up, start;
	int		flash;

	angles[PITCH] = p->pitch;
	angles[YAW] = p->yaw;
	angles[ROLL] = 0;
	AngleVectors (angles, forward, right, up);
	VectorMA ((float *)p->eye, 24, forward, start);
	VectorMA (start, 8, right, start);
	VectorMA (start, -8, up, start);

	switch (p->fire)
	{
	case 1:
		fire_blaster (ent, start, forward, 15, 1000, EF_BLASTER, false);
		flash = MZ_BLASTER;
		break;
	case 2:
		fire_rocket (ent, start, forward, 100, 650, 120, 120);
		flash = MZ_ROCKET;
		break;
	case 3:
		fire_grenade (ent, start, forward, 120, 600, 2.5, 160);
		flash = MZ_GRENADE;
		break;
	case 4:
		fire_rail (ent, start, forward, 100, 200);
		flash = MZ_RAILGUN;
		break;
	case 5:
		fire_blaster (ent, start, forward, 20, 1000, EF_HYPERBLASTER, true);
		flash = MZ_HYPERBLASTER;
		break;
	case 6:
		fire_bfg (ent, start, forward, 200, 400, 1000);
		flash = MZ_BFG;
		break;
	default:
		return;
	}

	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent - g_edicts);
	gi.WriteByte (flash);
	gi.multicast (ent->s.origin, MULTICAST_PVS);
}

/*
==================
Tour_Move

At the start of the player's end of a server frame: on to the next place
==================
*/
void Tour_Move (edict_t *ent)
{
	const tourpoint_t	*p;
	int					clip;

	if (!Tour_Active (ent))
		return;

	clip = (tour_at >= 0 && tour_at < tour_count) ? tour_points[tour_at].clip : 0;
	if (clip != tour_clip)
	{
		if (tour_clip)
			Tour_Command ("tour_clip_end", tour_clip);
		if (clip)
			Tour_Command ("tour_clip_begin", clip);
		tour_clip = clip;
	}
	if (tour_at >= tour_count)
	{
		gi.cprintf (ent, PRINT_HIGH, "Tour: done\n");
		Tour_Free ();
		Tour_Command ("tour_end", 0);
		return;
	}

	p = &tour_points[tour_at < 0 ? 0 : tour_at];
	VectorCopy (p->eye, ent->s.origin);
	ent->s.origin[2] -= ent->viewheight;
	VectorClear (ent->velocity);
	ent->client->v_angle[PITCH] = p->pitch;
	ent->client->v_angle[YAW] = p->yaw;
	ent->client->v_angle[ROLL] = 0;
	gi.linkentity (ent);

	if (tour_at >= 0 && p->fire)
		Tour_Fire (ent, p);
	tour_at++;
}

/*
==================
Tour_View

At the end of it: the view is exactly the place and the way given, with
none of the bobbing and leaning of a player on foot. The client is told
not to work the view out for itself.
==================
*/
void Tour_View (edict_t *ent)
{
	gclient_t	*client = ent->client;
	int			i;

	if (!Tour_Active (ent))
		return;

	client->ps.pmove.pm_type = PM_FREEZE;
	client->ps.pmove.pm_flags |= PMF_NO_PREDICTION;
	for (i=0 ; i<3 ; i++)
	{
		client->ps.pmove.origin[i] = ent->s.origin[i] * 8.0;
		client->ps.pmove.velocity[i] = 0;
		client->ps.viewangles[i] = client->v_angle[i];
	}
	VectorClear (client->ps.viewoffset);
	client->ps.viewoffset[2] = ent->viewheight;
	VectorClear (client->ps.kick_angles);
	client->ps.blend[3] = 0;
}
