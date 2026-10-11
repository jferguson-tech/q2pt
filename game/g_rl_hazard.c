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
// triggers that hurt, lava and slime.
//
// The navigation graph is grown through laser beams as if they were not
// there, since most can be switched off. Here each link is given the beam
// it passes through, if any, and counts as shut while that beam is on; the
// planner then looks for what switches the beam as it does for a door.
//
// Lava, slime and hurting triggers are left out of the graph when it is
// grown, but the player is not always on the graph: it sidesteps, backs off
// and dodges. Nav_StepSafe (g_rl_nav.c) is asked before any such step.

#include "g_local.h"
#include "g_rl.h"
#include "g_rl_nav.h"

#define	HAZ_MAX_LASERS	64
#define	HAZ_MAX_HURTS	128

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
Haz_At

True when the player standing at origin would be hurt by what is there: a
beam, a trigger that hurts, or lava or slime about its feet
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

	VectorSet (p, origin[0], origin[1], origin[2] - 23);
	return (gi.pointcontents (p) & (CONTENTS_LAVA|CONTENTS_SLIME)) != 0;
}
