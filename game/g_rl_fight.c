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
// g_rl_fight.c -- the scripted player's fighting: which monster to shoot,
// where to point to hit it, and with what.

#include "g_local.h"
#include "g_rl.h"
#include "g_rl_nav.h"

#define	FIGHT_RANGE		900.0f		// monsters farther off are left alone

extern const char *rl_weapons[RL_WEAPONS];

/*
================
Fight_Sees

True when a shot from the eye would reach the monster's middle or its head:
nothing solid in between, and no window or grating either, which a look
passes and a shot does not
================
*/
qboolean Fight_Sees (edict_t *ent, vec3_t eye, edict_t *m)
{
	vec3_t	p;
	trace_t	tr;

	VectorAdd (m->absmin, m->absmax, p);
	VectorScale (p, 0.5f, p);
	if (!gi.inPVS (eye, p))
		return false;
	tr = gi.trace (eye, NULL, NULL, p, ent, MASK_SHOT);
	if (tr.fraction >= 1 || tr.ent == m)
		return true;
	p[2] = m->absmax[2] - 8;
	tr = gi.trace (eye, NULL, NULL, p, ent, MASK_SHOT);
	return tr.fraction >= 1 || tr.ent == m;
}

/*
================
Fight_Target

The monster to shoot: the nearest one in sight and in range that is alive
and not on the player's side. One that has noticed the player counts as a
third nearer than it is, so that it is dealt with before one still asleep.
One behind the player counts as up to four times as far as one dead ahead.
================
*/
edict_t *Fight_Target (edict_t *ent, vec3_t eye)
{
	edict_t	*e, *best;
	int		i;
	float	d, bestd;
	vec3_t	v, forward;

	AngleVectors (ent->client->v_angle, forward, NULL, NULL);
	best = NULL;
	bestd = FIGHT_RANGE;
	for (i=game.maxclients+1, e=g_edicts+i ; i<globals.num_edicts ; i++, e++)
	{
		if (!e->inuse || !(e->svflags & SVF_MONSTER) || e->health <= 0 || e->deadflag)
			continue;
		if ((e->monsterinfo.aiflags & AI_GOOD_GUY) || !e->takedamage)
			continue;
		// one waiting to be brought in by a trigger is not there yet, though
		// it has a place and its health
		if (e->solid == SOLID_NOT || (e->svflags & SVF_NOCLIENT))
			continue;
		if (e->classname && !strcmp (e->classname, "misc_insane"))
			continue;
		VectorSubtract (e->s.origin, eye, v);
		d = VectorLength (v);
		if (d >= FIGHT_RANGE)
			continue;
		if (e->enemy == ent)
			d *= 0.67f;
		// and one the view is on already counts as nearer than one it would
		// have to swing round to, so that the aim stays on what it is on
		d *= 1 + (1 - DotProduct (v, forward) / (VectorLength (v) + 1)) * 1.5f;
		if (d >= bestd || !Fight_Sees (ent, eye, e))
			continue;
		bestd = d;
		best = e;
	}
	return best;
}

/*
================
Fight_Aim

The view angles that point the weapon in hand at the monster. A weapon that
throws something is pointed where the monster will be when it arrives, going
as it goes now.
================
*/
void Fight_Aim (edict_t *ent, edict_t *enemy, vec3_t eye, float *yaw, float *pitch, float *dist)
{
	vec3_t	p, d;
	float	speed, time;
	char	*name;

	VectorAdd (enemy->absmin, enemy->absmax, p);
	VectorScale (p, 0.5f, p);
	VectorSubtract (p, eye, d);
	*dist = VectorLength (d);

	speed = 0;
	name = ent->client->pers.weapon ? ent->client->pers.weapon->pickup_name : "";
	if (!strcmp (name, "Blaster") || !strcmp (name, "HyperBlaster"))
		speed = 1000;
	else if (!strcmp (name, "Rocket Launcher"))
		speed = 650;
	else if (!strcmp (name, "BFG10K"))
		speed = 400;
	if (speed)
	{
		time = *dist / speed;
		VectorMA (p, time, enemy->velocity, p);
		VectorSubtract (p, eye, d);
	}

	*yaw = RAD2DEG_F(atan2 (d[1], d[0]));
	*pitch = -RAD2DEG_F(atan2 (d[2], sqrt (d[0]*d[0] + d[1]*d[1])));
}

/*
================
Fight_Weapon

The weapon to hold for a monster at this distance, as the action numbers
them, or 0 to keep the one in hand: the first in the list for the distance
that is carried and has something to fire.
================
*/
int Fight_Weapon (edict_t *ent, float dist)
{
	// by action number: 1 blaster, 2 shotgun, 3 super shotgun, 4 machinegun,
	// 5 chaingun, 6 grenade launcher, 7 rocket launcher, 8 hyperblaster,
	// 9 railgun, 10 BFG
	static const int	close_list[] = {3, 5, 2, 8, 4, 1, 0};
	static const int	mid_list[] = {5, 8, 7, 4, 3, 9, 2, 1, 0};
	static const int	far_list[] = {9, 7, 4, 5, 8, 1, 0};
	const int	*list;
	gclient_t	*client = ent->client;
	gitem_t		*it, *ammo;
	int			i, index;

	list = dist < 200 ? close_list : dist < 550 ? mid_list : far_list;
	for (i=0 ; list[i] ; i++)
	{
		it = FindItem ((char *)rl_weapons[list[i] - 1]);
		if (!it)
			continue;
		index = ITEM_INDEX(it);
		if (!client->pers.inventory[index])
			continue;
		if (it->ammo)
		{
			ammo = FindItem (it->ammo);
			if (!ammo || client->pers.inventory[ITEM_INDEX(ammo)] < it->quantity)
				continue;
		}
		if (client->pers.weapon == it || client->newweapon == it)
			return 0;
		return list[i];
	}
	return 0;
}

/*
================
Fight_Hunter

The nearest monster that is after the player, near, and coming, whether or
not it can be seen yet. With one about, the player waits for it facing its
way and does not walk on with it at its back.
================
*/
edict_t *Fight_Hunter (edict_t *ent, vec3_t eye)
{
	edict_t	*e, *best;
	int		i;
	float	d, bestd;
	vec3_t	v;

	best = NULL;
	bestd = 600;
	for (i=game.maxclients+1, e=g_edicts+i ; i<globals.num_edicts ; i++, e++)
	{
		if (!e->inuse || !(e->svflags & SVF_MONSTER) || e->health <= 0 || e->deadflag
			|| e->enemy != ent || (e->monsterinfo.aiflags & AI_GOOD_GUY)
			|| e->solid == SOLID_NOT || (e->svflags & SVF_NOCLIENT))
			continue;
		VectorSubtract (e->s.origin, eye, v);
		d = VectorLength (v);
		if (d >= bestd || !gi.inPVS (eye, e->s.origin))
			continue;
		// on its way here: one that stays where it is, out of sight, is not
		// waited for
		if (-DotProduct (e->velocity, v) < 30 * d)
			continue;
		bestd = d;
		best = e;
	}
	return best;
}

/*
================
Fight_Clear

False when a shot along the view would set off a barrel near enough to hurt
the one who fired it
================
*/
qboolean Fight_Clear (edict_t *ent, vec3_t eye)
{
	vec3_t	forward, end;
	trace_t	tr;

	AngleVectors (ent->client->v_angle, forward, NULL, NULL);
	VectorMA (eye, 300, forward, end);
	tr = gi.trace (eye, NULL, NULL, end, ent, MASK_SHOT);
	return !(tr.fraction < 1 && tr.ent && tr.ent->classname && !strcmp (tr.ent->classname, "misc_explobox"));
}

/*
================
Fight_Reaches

True when a shot would reach the monster from where the weapon is, which is
a hand's breadth below the eye: over a rail or a ledge's edge the eye can
see what the gun cannot hit.
================
*/
qboolean Fight_Reaches (edict_t *ent, vec3_t eye, edict_t *m)
{
	vec3_t	from, p;
	trace_t	tr;

	VectorCopy (eye, from);
	from[2] -= 8;
	VectorAdd (m->absmin, m->absmax, p);
	VectorScale (p, 0.5f, p);
	tr = gi.trace (from, NULL, NULL, p, ent, MASK_SHOT);
	return tr.fraction >= 1 || tr.ent == m;
}
