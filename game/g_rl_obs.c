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
// g_rl_obs.c -- what the driven player perceives (see g_rl.h): its own
// state, a grid of rays across its field of view, and the things in that
// field with nothing between them and the eye. Nothing behind a wall and no
// place in the map's coordinates is given.

#include "g_local.h"
#include "g_rl.h"

// RL_ENT_TYPE of a monster is its place in this list, from 1. Items give
// their place in the game's item list.
static const char *rl_monsters[] =
{
	"monster_soldier_light", "monster_soldier", "monster_soldier_ss",
	"monster_infantry", "monster_gunner", "monster_berserk", "monster_gladiator",
	"monster_tank", "monster_tank_commander", "monster_medic", "monster_flipper",
	"monster_chick", "monster_parasite", "monster_flyer", "monster_brain",
	"monster_floater", "monster_hover", "monster_mutant", "monster_supertank",
	"monster_boss2", "monster_jorg", "monster_makron", NULL
};

static const char *rl_ammo[6] = {"Shells", "Bullets", "Grenades", "Rockets", "Cells", "Slugs"};
extern const char *rl_weapons[RL_WEAPONS];

qboolean Pickup_Health (edict_t *ent, edict_t *other);

#define	DEG2RAD(a)	((a) * (float)(M_PI / 180.0))
#define	RAD2DEG(a)	((a) * (float)(180.0 / M_PI))

static float	rl_tan_x, rl_tan_y;		// of half the field of view

/*
================
RL_Self
================
*/
static void RL_Self (edict_t *ent, float hurt)
{
	gclient_t	*client = ent->client;
	float		*s = rl_block->self;
	vec3_t		flat, forward, left;
	gitem_t		*it;
	int			i, index;

	memset (s, 0, sizeof(rl_block->self));

	s[RL_SELF_HEALTH] = ent->health;
	index = ArmorIndex (ent);
	if (index)
		s[RL_SELF_ARMOR] = client->pers.inventory[index];
	for (i=0 ; i<6 ; i++)
	{
		it = FindItem ((char *)rl_ammo[i]);
		if (it)
			s[RL_SELF_AMMO+i] = client->pers.inventory[ITEM_INDEX(it)];
	}
	for (i=0 ; i<RL_WEAPONS ; i++)
	{
		it = FindItem ((char *)rl_weapons[i]);
		if (!it)
			continue;
		if (client->pers.inventory[ITEM_INDEX(it)])
			s[RL_SELF_OWNED+i] = 1;
		if (client->pers.weapon == it)
			s[RL_SELF_WEAPON] = i+1;
	}
	s[RL_SELF_READY] = client->weaponstate == WEAPON_READY;

	VectorSet (flat, 0, client->v_angle[YAW], 0);
	AngleVectors (flat, forward, left, NULL);
	VectorInverse (left);		// AngleVectors gives the right
	s[RL_SELF_VEL] = DotProduct (ent->velocity, forward);
	s[RL_SELF_VEL+1] = DotProduct (ent->velocity, left);
	s[RL_SELF_VEL+2] = ent->velocity[2];
	s[RL_SELF_PITCH] = client->v_angle[PITCH];
	s[RL_SELF_YAW_SIN] = sin (DEG2RAD(client->v_angle[YAW]));
	s[RL_SELF_YAW_COS] = cos (DEG2RAD(client->v_angle[YAW]));
	s[RL_SELF_GROUND] = ent->groundentity != NULL;
	s[RL_SELF_WATER] = ent->waterlevel;
	s[RL_SELF_DUCKED] = (client->ps.pmove.pm_flags & PMF_DUCKED) != 0;
	s[RL_SELF_HURT] = hurt;
	if (ent->waterlevel == 3 && ent->air_finished > level.time)
		s[RL_SELF_AIR] = ent->air_finished - level.time;
	else if (ent->waterlevel < 3)
		s[RL_SELF_AIR] = 12;
	for (i=0 ; i<game.num_items ; i++)
		if ((itemlist[i].flags & IT_KEY) && client->pers.inventory[i])
			s[RL_SELF_KEYS] += 1;
}

/*
================
RL_Rays

The rays go through the middles of the cells of a grid laid over the view,
as through the pixels of a small picture of it, top row first, left to right.
================
*/
static void RL_Rays (edict_t *ent, vec3_t eye, vec3_t forward, vec3_t left, vec3_t up)
{
	int			x, y, n, mask, kind;
	float		sx, sy;
	vec3_t		dir, end;
	trace_t		tr;
	edict_t		*hit;

	// From under a liquid its surface cannot be traced against: the trace
	// would start inside it.
	mask = MASK_SHOT;
	if (!(gi.pointcontents (eye) & MASK_WATER))
		mask |= MASK_WATER;

	n = 0;
	for (y=0 ; y<RL_RAYS_Y ; y++)
	{
		sy = rl_tan_y * (1 - (2*y + 1) / (float)RL_RAYS_Y);
		for (x=0 ; x<RL_RAYS_X ; x++, n++)
		{
			sx = rl_tan_x * (1 - (2*x + 1) / (float)RL_RAYS_X);
			VectorMA (forward, sx, left, dir);
			VectorMA (dir, sy, up, dir);
			VectorNormalize (dir);
			VectorMA (eye, RL_RAY_RANGE, dir, end);

			tr = gi.trace (eye, NULL, NULL, end, ent, mask);
			hit = tr.ent;

			if (tr.fraction >= 1)
				kind = RL_HIT_NONE;
			else if (tr.surface && (tr.surface->flags & SURF_SKY))
				kind = RL_HIT_SKY;
			else if (tr.contents & CONTENTS_LAVA)
				kind = RL_HIT_LAVA;
			else if (tr.contents & CONTENTS_SLIME)
				kind = RL_HIT_SLIME;
			else if (tr.contents & CONTENTS_WATER)
				kind = RL_HIT_WATER;
			else if (!hit || hit == world)
				kind = RL_HIT_WORLD;
			else if (hit->svflags & SVF_MONSTER)
				kind = hit->health > 0 ? RL_HIT_MONSTER : RL_HIT_CORPSE;
			else if (hit->solid == SOLID_BSP)
				kind = RL_HIT_MOVER;
			else
				kind = RL_HIT_OTHER;

			rl_block->ray_dist[n] = tr.fraction * RL_RAY_RANGE;
			rl_block->ray_slope[n] = tr.fraction < 1 ? tr.plane.normal[2] : 0;
			rl_block->ray_kind[n] = kind;
		}
	}
}

/*
================
RL_MonsterType

A monster's place in rl_monsters, from 1, or 0 for anything else
================
*/
int RL_MonsterType (edict_t *e)
{
	int		i;

	if (e->classname)
		for (i=0 ; rl_monsters[i] ; i++)
			if (!strcmp (e->classname, rl_monsters[i]))
				return i+1;
	return 0;
}

/*
================
RL_Kind

What sort of thing an entity is to look at, or RL_KIND_NONE for what is not
reported: the world's own brushes, triggers, and things with no model.
================
*/
static int RL_Kind (edict_t *e, int *type)
{
	*type = 0;

	if (e->svflags & SVF_MONSTER)
	{
		*type = RL_MonsterType (e);
		return (e->health > 0 && !e->deadflag) ? RL_KIND_MONSTER : RL_KIND_CORPSE;
	}

	if (e->item)
	{
		*type = ITEM_INDEX(e->item);
		if (e->item->flags & IT_KEY)
			return RL_KIND_KEY;
		if (e->item->flags & IT_WEAPON)
			return RL_KIND_WEAPON;
		if (e->item->flags & IT_AMMO)
			return RL_KIND_AMMO;
		if (e->item->flags & IT_ARMOR)
			return RL_KIND_ARMOR;
		if (e->item->pickup == Pickup_Health)
			return RL_KIND_HEALTH;
		return RL_KIND_POWERUP;
	}

	if (e->movetype == MOVETYPE_FLYMISSILE || (e->movetype == MOVETYPE_BOUNCE && e->owner))
		return RL_KIND_MISSILE;

	if (!e->classname)
		return RL_KIND_NONE;
	if (!strcmp (e->classname, "misc_explobox"))
		return RL_KIND_BARREL;
	if (!strcmp (e->classname, "func_button"))
		return RL_KIND_BUTTON;
	if (!strncmp (e->classname, "func_door", 9))
		return RL_KIND_DOOR;
	if (!strcmp (e->classname, "func_plat") || !strcmp (e->classname, "func_train"))
		return RL_KIND_LIFT;
	if (e->solid == SOLID_BBOX)
		return RL_KIND_OTHER;
	return RL_KIND_NONE;
}

/*
================
RL_Seen

True when nothing opaque lies between the eye and the thing: its middle is
tried, then a point near its top, so that a head over a wall counts.
================
*/
static qboolean RL_Seen (edict_t *ent, vec3_t eye, edict_t *e, vec3_t mid)
{
	trace_t	tr;
	vec3_t	top;

	if (!gi.inPVS (eye, mid))
		return false;

	tr = gi.trace (eye, NULL, NULL, mid, ent, MASK_OPAQUE);
	if (tr.fraction >= 1 || tr.ent == e)
		return true;

	VectorCopy (mid, top);
	top[2] += 0.4f * (e->absmax[2] - e->absmin[2]);
	tr = gi.trace (eye, NULL, NULL, top, ent, MASK_OPAQUE);
	return tr.fraction >= 1 || tr.ent == e;
}

/*
================
RL_Ents

The RL_ENTS nearest things in view, nearest first.
================
*/
static void RL_Ents (edict_t *ent, vec3_t eye, vec3_t forward, vec3_t left, vec3_t up)
{
	edict_t	*e;
	int		i, j, n, kind, type;
	vec3_t	mid, d, facing;
	float	x, y, z, dist, margin;
	float	*row;

	memset (rl_block->ents, 0, sizeof(rl_block->ents));
	n = 0;

	for (i=game.maxclients+1, e=g_edicts+i ; i<globals.num_edicts ; i++, e++)
	{
		if (!e->inuse || !e->s.modelindex || (e->svflags & SVF_NOCLIENT))
			continue;
		kind = RL_Kind (e, &type);
		if (kind == RL_KIND_NONE)
			continue;

		VectorAdd (e->absmin, e->absmax, mid);
		VectorScale (mid, 0.5f, mid);
		VectorSubtract (mid, eye, d);
		x = DotProduct (d, forward);
		if (x < 1)
			continue;
		y = DotProduct (d, left);
		z = DotProduct (d, up);
		// inside the view, or near enough to its edge that part of it shows
		margin = 0.5f * (e->absmax[0] - e->absmin[0]);
		if (fabs (y) > x * rl_tan_x + margin || fabs (z) > x * rl_tan_y + margin)
			continue;
		dist = VectorLength (d);
		if (dist > RL_RAY_RANGE)
			continue;

		// its place among those found so far, before the cost of a trace
		for (j=n ; j>0 && rl_block->ents[j-1][RL_ENT_DIST] > dist ; j--)
			;
		if (j >= RL_ENTS)
			continue;
		if (!RL_Seen (ent, eye, e, mid))
			continue;

		if (n < RL_ENTS)
			n++;
		memmove (rl_block->ents[j+1], rl_block->ents[j], (n-1-j) * sizeof(rl_block->ents[0]));

		row = rl_block->ents[j];
		row[RL_ENT_KIND] = kind;
		row[RL_ENT_TYPE] = type;
		row[RL_ENT_YAW] = RAD2DEG(atan2 (y, x));
		row[RL_ENT_PITCH] = -RAD2DEG(atan2 (z, sqrt (x*x + y*y)));
		row[RL_ENT_DIST] = dist;
		row[RL_ENT_VEL] = DotProduct (e->velocity, forward);
		row[RL_ENT_VEL+1] = DotProduct (e->velocity, left);
		row[RL_ENT_VEL+2] = DotProduct (e->velocity, up);
		row[RL_ENT_HURT] = (kind == RL_KIND_MONSTER && (e->s.skinnum & 1)) ? 1 : 0;
		AngleVectors (e->s.angles, facing, NULL, NULL);
		row[RL_ENT_FACING] = dist > 0 ? -DotProduct (facing, d) / dist : 0;
		row[RL_ENT_HEIGHT] = e->absmax[2] - e->absmin[2];
		row[RL_ENT_WIDTH] = e->absmax[0] - e->absmin[0];
	}
}

/*
================
RL_Perceive
================
*/
void RL_Perceive (edict_t *ent, float hurt)
{
	vec3_t	eye, forward, left, up;

	if (!ent->client)
		return;

	rl_tan_x = tan (DEG2RAD(RL_FOV_X * 0.5f));
	rl_tan_y = tan (DEG2RAD(RL_FOV_Y * 0.5f));

	VectorCopy (ent->s.origin, eye);
	eye[2] += ent->viewheight;
	AngleVectors (ent->client->v_angle, forward, left, up);
	VectorInverse (left);

	RL_Self (ent, hurt);
	RL_Rays (ent, eye, forward, left, up);
	RL_Ents (ent, eye, forward, left, up);

}
