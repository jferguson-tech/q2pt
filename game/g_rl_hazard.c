/*
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
// g_rl_hazard.c -- what hurts the player where it stands: laser beams,
// triggers that hurt, lava and slime; and the ways out of the map that are
// not the way on, which end the episode as surely.
//
// The navigation graph is grown through laser beams as if they were not
// there, since most can be switched off. Here each link is given the beam
// it passes through, if any, and counts as shut while that beam is on; the
// planner then looks for what switches the beam as it does for a door.
//
// When the episode names the exit that counts (rl_block->onward), the
// triggers of every other exit are kept clear of in the same two ways: a
// link through one is shut, and a step into one is refused.
//
// Lava, slime and hurting triggers are left out of the graph when it is
// grown, but the player is not always on the graph: it sidesteps, backs off
// and dodges. Nav_StepSafe (g_rl_nav.c) is asked before any such step.

#include "g_local.h"
#include "g_rl.h"
#include "g_rl_nav.h"

#define	HAZ_MAX_LASERS	64
#define	HAZ_MAX_HURTS	128
#define	HAZ_MAX_EXITS	16

typedef struct
{
	edict_t	*ent;
	vec3_t	from, to;		// the beam, to the first wall
	vec3_t	mins, maxs;		// its bounds
} haz_laser_t;

static haz_laser_t	haz_lasers[HAZ_MAX_LASERS];
static int			haz_num_lasers;
static edict_t		*haz_hurts[HAZ_MAX_HURTS];
static int			haz_num_hurts;
static byte			*haz_link_laser;	// for each link, 1 + the laser it passes through, or 0
static edict_t		*haz_exits[HAZ_MAX_EXITS];	// triggers of the exits that are not the way on
static int			haz_num_exits;
static byte			*haz_link_exit;		// for each link, 1 if it passes through one of them

/*
================
Haz_BoxOnBeam

True when the line from a to b passes through the box
================
*/
static qboolean Haz_BoxOnBeam (vec3_t mins, vec3_t maxs, vec3_t a, vec3_t b)
{
	float	t0 = 0, t1 = 1, d, u, v, swap;
	int		i;

	for (i=0 ; i<3 ; i++)
	{
		d = b[i] - a[i];
		if (fabs (d) < 0.001f)
		{
			if (a[i] < mins[i] || a[i] > maxs[i])
				return false;
			continue;
		}
		u = (mins[i] - a[i]) / d;
		v = (maxs[i] - a[i]) / d;
		if (u > v)
		{
			swap = u;
			u = v;
			v = swap;
		}
		if (u > t0)
			t0 = u;
		if (v < t1)
			t1 = v;
		if (t0 > t1)
			return false;
	}
	return true;
}

static qboolean Haz_LaserOn (haz_laser_t *z)
{
	return z->ent->inuse && (z->ent->spawnflags & 1);
}

/*
================
Haz_Beam

Where a laser's beam runs: from the laser toward the thing it is aimed at,
or the way it faces, to the first wall. Worked out here and not read from
the laser, which does not aim until the map has run for a second.
================
*/
static void Haz_Beam (haz_laser_t *z)
{
	edict_t	*e = z->ent, *aim;
	vec3_t	dir, end;
	trace_t	tr;
	int		i;

	aim = e->enemy;
	if (!aim && e->target)
		aim = G_Find (NULL, FOFS(targetname), e->target);
	if (aim)
	{
		VectorMA (aim->absmin, 0.5f, aim->size, end);
		if (!aim->size[0] && !aim->size[1] && !aim->size[2] && !aim->absmin[0] && !aim->absmin[1])
			VectorCopy (aim->s.origin, end);
		VectorSubtract (end, e->s.origin, dir);
		VectorNormalize (dir);
	}
	else if (e->movedir[0] || e->movedir[1] || e->movedir[2])
		VectorCopy (e->movedir, dir);
	else if (e->s.angles[0] == 0 && e->s.angles[1] == -1 && e->s.angles[2] == 0)
		VectorSet (dir, 0, 0, 1);
	else if (e->s.angles[0] == 0 && e->s.angles[1] == -2 && e->s.angles[2] == 0)
		VectorSet (dir, 0, 0, -1);
	else
		AngleVectors (e->s.angles, dir, NULL, NULL);

	VectorCopy (e->s.origin, z->from);
	VectorMA (z->from, 2048, dir, end);
	tr = gi.trace (z->from, NULL, NULL, end, e, CONTENTS_SOLID);
	VectorCopy (tr.endpos, z->to);
	for (i=0 ; i<3 ; i++)
	{
		z->mins[i] = (z->from[i] < z->to[i] ? z->from[i] : z->to[i]) - 4;
		z->maxs[i] = (z->from[i] > z->to[i] ? z->from[i] : z->to[i]) + 4;
	}
}

/*
================
Haz_InExit

True when the player's box at origin touches the trigger of an exit that is
not the way on
================
*/
static qboolean Haz_InExit (vec3_t origin)
{
	edict_t	*e;
	int		i;

	for (i=0 ; i<haz_num_exits ; i++)
	{
		e = haz_exits[i];
		if (!e->inuse)
			continue;
		if (origin[0] + 17 < e->absmin[0] || origin[0] - 17 > e->absmax[0]
			|| origin[1] + 17 < e->absmin[1] || origin[1] - 17 > e->absmax[1]
			|| origin[2] + 33 < e->absmin[2] || origin[2] - 25 > e->absmax[2])
			continue;
		return true;
	}
	return false;
}

/*
================
Haz_FindExits

Lists the triggers that fire a target_changelevel to anywhere but the map
the episode is to end by reaching, and marks the links that pass through one.
================
*/
static void Haz_FindExits (void)
{
	edict_t		*e, *t;
	nav_link_t	*l;
	int			i, s, steps;
	vec3_t		a, b, d, p;

	haz_num_exits = 0;
	haz_link_exit = NULL;
	if (!rl_block->onward[0])
		return;
	for (i=game.maxclients+1, e=g_edicts+i ; i<globals.num_edicts ; i++, e++)
	{
		if (!e->inuse || !e->classname || !e->target || haz_num_exits == HAZ_MAX_EXITS)
			continue;
		if (strcmp (e->classname, "trigger_multiple") && strcmp (e->classname, "trigger_once"))
			continue;
		for (t=NULL ; (t = G_Find (t, FOFS(targetname), e->target)) != NULL ; )
			if (t->classname && !strcmp (t->classname, "target_changelevel") && t->map
				&& Q_stricmp (t->map, rl_block->onward))
			{
				haz_exits[haz_num_exits++] = e;
				break;
			}
	}
	if (!haz_num_exits || !nav_num_links)
		return;

	haz_link_exit = gi.TagMalloc (nav_num_links, TAG_LEVEL);
	for (i=0, l=nav_links ; i<nav_num_links ; i++, l++)
	{
		Nav_NodeOrigin (l->from, a);
		Nav_NodeOrigin (l->to, b);
		VectorSubtract (b, a, d);
		steps = 1 + (int)(VectorLength (d) / 8);
		for (s=0 ; s<=steps ; s++)
		{
			VectorMA (a, (float)s / steps, d, p);
			if (Haz_InExit (p))
			{
				haz_link_exit[i] = 1;
				break;
			}
		}
	}
}

qboolean Haz_LinkExit (nav_link_t *l)
{
	return haz_link_exit && haz_link_exit[l - nav_links];
}

/*
================
Haz_Find

Lists the map's lasers and hurting triggers, and marks the links of the
navigation graph that pass through a beam. Called when the graph is loaded.
================
*/
void Haz_Find (void)
{
	edict_t		*e;
	haz_laser_t	*z;
	nav_link_t	*l;
	int			i, k, s, steps;
	vec3_t		a, b, d, p, mins, maxs;
	float		top, len;

	haz_num_lasers = haz_num_hurts = 0;
	for (i=game.maxclients+1, e=g_edicts+i ; i<globals.num_edicts ; i++, e++)
	{
		if (!e->inuse || !e->classname)
			continue;
		if (!strcmp (e->classname, "target_laser") && haz_num_lasers < HAZ_MAX_LASERS)
		{
			haz_lasers[haz_num_lasers].ent = e;
			Haz_Beam (&haz_lasers[haz_num_lasers]);
			haz_num_lasers++;
		}
		else if (!strcmp (e->classname, "trigger_hurt") && haz_num_hurts < HAZ_MAX_HURTS)
			haz_hurts[haz_num_hurts++] = e;
	}

	Haz_FindExits ();

	haz_link_laser = NULL;
	if (!haz_num_lasers || !nav_num_links)
		return;
	haz_link_laser = gi.TagMalloc (nav_num_links, TAG_LEVEL);

	for (i=0, l=nav_links ; i<nav_num_links ; i++, l++)
	{
		Nav_NodeOrigin (l->from, a);
		Nav_NodeOrigin (l->to, b);
		// crouched all the way under a low roof; a jump goes over the line
		// between its ends
		top = l->type == NAV_DUCK ? 4 : l->type == NAV_JUMP ? 80 : 32;
		for (k=0, z=haz_lasers ; k<haz_num_lasers ; k++, z++)
		{
			if ((a[0] < b[0] ? a[0] : b[0]) - 20 > z->maxs[0] || (a[0] > b[0] ? a[0] : b[0]) + 20 < z->mins[0]
				|| (a[1] < b[1] ? a[1] : b[1]) - 20 > z->maxs[1] || (a[1] > b[1] ? a[1] : b[1]) + 20 < z->mins[1]
				|| (a[2] < b[2] ? a[2] : b[2]) - 28 > z->maxs[2] || (a[2] > b[2] ? a[2] : b[2]) + top + 4 < z->mins[2])
				continue;
			VectorSubtract (b, a, d);
			len = VectorLength (d);
			steps = 1 + (int)(len / 8);
			for (s=0 ; s<=steps ; s++)
			{
				VectorMA (a, (float)s / steps, d, p);
				VectorSet (mins, p[0] - 18, p[1] - 18, p[2] - 26);
				VectorSet (maxs, p[0] + 18, p[1] + 18, p[2] + top + 2);
				if (Haz_BoxOnBeam (mins, maxs, z->from, z->to))
					break;
			}
			if (s <= steps)
			{
				haz_link_laser[i] = k + 1;
				break;
			}
		}
	}
}

/*
================
Haz_LinkLaser

The laser, switched on, whose beam a link passes through, or NULL
================
*/
edict_t *Haz_LinkLaser (nav_link_t *l)
{
	int		k;

	if (!haz_link_laser)
		return NULL;
	k = haz_link_laser[l - nav_links];
	if (!k || !Haz_LaserOn (&haz_lasers[k-1]))
		return NULL;
	return haz_lasers[k-1].ent;
}

/*
================
Haz_RayBeam

How far along a ray from the eye the nearest beam that is on is met, or -1
if none is within max. A beam has no width to hit, so a ray that passes
close by one counts: within 8 units, and 6 more for every 100 of distance,
which is half of what lies between one ray of the view and the next. A
beam across the view is then met by some ray however far off it is, as a
thin bright line is seen.
================
*/
float Haz_RayBeam (vec3_t eye, vec3_t dir, float max)
{
	haz_laser_t	*z;
	vec3_t		v, w, p, q;
	float		vv, dv, dw, vw, den, t, u, best = -1;
	int			i;

	for (i=0, z=haz_lasers ; i<haz_num_lasers ; i++, z++)
	{
		if (!Haz_LaserOn (z))
			continue;
		// the nearest points of the ray eye + t dir and the beam from + u v
		VectorSubtract (z->to, z->from, v);
		VectorSubtract (eye, z->from, w);
		vv = DotProduct (v, v);
		if (vv < 1)
			continue;
		dv = DotProduct (dir, v);
		dw = DotProduct (dir, w);
		vw = DotProduct (v, w);
		den = vv - dv * dv;
		t = den > 0.001f * vv ? (dv * vw - vv * dw) / den : 0;
		if (t < 0)
			t = 0;
		if (t > max)
			t = max;
		u = (vw + t * dv) / vv;
		if (u < 0)
			u = 0;
		if (u > 1)
			u = 1;
		// and the ray's nearest to that point of the beam
		t = u * dv - dw;
		if (t < 0)
			t = 0;
		if (t > max)
			continue;
		VectorMA (eye, t, dir, p);
		VectorMA (z->from, u, v, q);
		VectorSubtract (p, q, p);
		if (VectorLength (p) > 8 + 0.06f * t)
			continue;
		if (best < 0 || t < best)
			best = t;
	}
	return best;
}

/*
================
Haz_At

True when the player standing at origin would be hurt by what is there: a
beam, a trigger that hurts, or lava or slime about its feet. An exit that is
not the way on counts too.
================
*/
qboolean Haz_At (vec3_t origin, qboolean ducked)
{
	edict_t		*e;
	haz_laser_t	*z;
	vec3_t		mins, maxs, p;
	int			i;

	VectorSet (mins, origin[0] - 18, origin[1] - 18, origin[2] - 26);
	VectorSet (maxs, origin[0] + 18, origin[1] + 18, origin[2] + (ducked ? 6 : 34));

	for (i=0, z=haz_lasers ; i<haz_num_lasers ; i++, z++)
		if (Haz_LaserOn (z) && Haz_BoxOnBeam (mins, maxs, z->from, z->to))
			return true;

	for (i=0 ; i<haz_num_hurts ; i++)
	{
		e = haz_hurts[i];
		if (!e->inuse || e->solid != SOLID_TRIGGER)
			continue;
		if (maxs[0] < e->absmin[0] || mins[0] > e->absmax[0] || maxs[1] < e->absmin[1]
			|| mins[1] > e->absmax[1] || maxs[2] < e->absmin[2] || mins[2] > e->absmax[2])
			continue;
		return true;
	}

	if (Haz_InExit (origin))
		return true;

	VectorSet (p, origin[0], origin[1], origin[2] - 23);
	return (gi.pointcontents (p) & (CONTENTS_LAVA|CONTENTS_SLIME)) != 0;
}
