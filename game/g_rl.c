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
// g_rl.c -- the game's side of being driven by another program (see g_rl.h
// and server/sv_rl.c): turning an action into the player's move for a frame,
// saying when an episode is over, and adding up what a step was worth.
//
// The player is moved by ClientThink and by nothing else, with one command
// of RL_STEP_MSEC in each server frame.

#include "g_local.h"
#include "g_rl.h"

void ClientThink (edict_t *ent, usercmd_t *cmd);

rl_shared_t	*rl_block;			// NULL when nothing is driving the game

// degrees turned to the left in one step
const float rl_yaw_bins[RL_YAW_BINS] =
	{-30, -18, -10, -5, -2.5f, -1, -0.4f, 0, 0.4f, 1, 2.5f, 5, 10, 18, 30};
// degrees the view goes down in one step
const float rl_pitch_bins[RL_PITCH_BINS] =
	{-20, -10, -5, -2, -0.7f, 0, 0.7f, 2, 5, 10, 20};

// the weapons an action can change to, in the order of the game's keys 1 to 0
const char *rl_weapons[RL_WEAPONS] =
{
	"Blaster", "Shotgun", "Super Shotgun", "Machinegun", "Chaingun",
	"Grenade Launcher", "Rocket Launcher", "HyperBlaster", "Railgun", "BFG10K"
};

// The light at the player's feet comes from the client's renderer, and
// monsters far away do not notice a player in the dark. A server with no
// client has no such reading: the player counts as standing in plain light.
#define	RL_LIGHT_LEVEL	128

static qboolean	rl_looked;		// the server's cvar has been read
static float	rl_dealt, rl_taken;		// damage since the last observation
static int		rl_kills_seen;

/*
================
RL_Active
================
*/
qboolean RL_Active (void)
{
	void	*block;
	char	*s;

	if (!rl_looked)
	{
		rl_looked = true;
		s = gi.cvar (RL_BLOCK_CVAR, "", 0)->string;
		block = NULL;
		if (s[0] && sscanf (s, "%p", &block) == 1)
			rl_block = (rl_shared_t *)block;
	}
	return rl_block != NULL;
}

/*
================
RL_Damage

Called by T_Damage with the health a hit took off and what armour held back.
================
*/
void RL_Damage (edict_t *targ, edict_t *attacker, int take, int saved)
{
	edict_t	*player;

	if (!rl_block)
		return;
	player = &g_edicts[1];

	if (targ == player)
		rl_taken += take + saved;
	else if (attacker == player && (targ->svflags & SVF_MONSTER) && targ->health > 0)
		rl_dealt += take < targ->health ? take : targ->health;	// not what is done to a corpse
}

/*
================
RL_Hash

A number made from the state of everything in the map. Two runs that agree
on it at every step have gone the same way.
================
*/
static unsigned RL_HashBytes (unsigned h, void *data, int len)
{
	byte	*b = (byte *)data;

	while (len--)
		h = (h ^ *b++) * 16777619u;
	return h;
}

static unsigned RL_Hash (void)
{
	unsigned	h = 2166136261u;
	edict_t		*e;
	int			i;

	for (i=0, e=g_edicts ; i<globals.num_edicts ; i++, e++)
	{
		if (!e->inuse)
			continue;
		h = RL_HashBytes (h, &i, sizeof(i));
		h = RL_HashBytes (h, &e->s, sizeof(e->s));
		h = RL_HashBytes (h, e->velocity, sizeof(e->velocity));
		h = RL_HashBytes (h, &e->health, sizeof(e->health));
		h = RL_HashBytes (h, &e->nextthink, sizeof(e->nextthink));
	}
	e = &g_edicts[1];
	if (e->client)
	{
		h = RL_HashBytes (h, &e->client->ps.pmove, sizeof(e->client->ps.pmove));
		h = RL_HashBytes (h, e->client->v_angle, sizeof(e->client->v_angle));
		h = RL_HashBytes (h, e->client->pers.inventory, sizeof(e->client->pers.inventory));
	}
	return h;
}

/*
================
RL_Act

The action becomes the one command the player is given for this frame.
The view turns by the bins chosen; the command carries the angle wanted less
the offset the game keeps, as a client's does.
================
*/
static void RL_Act (void)
{
	edict_t		*ent = &g_edicts[1];
	gclient_t	*client = ent->client;
	usercmd_t	cmd;
	int			*a;
	int			i, n;
	float		angle;
	gitem_t		*it;

	if (!client || !ent->inuse)
		return;

	a = rl_block->act_teacher ? rl_block->teacher : rl_block->action;
	for (i=0 ; i<RL_ACT_BRANCHES ; i++)
		if (a[i] < 0)
			a[i] = 0;
	if (a[RL_ACT_FORWARD] > 2) a[RL_ACT_FORWARD] = 2;
	if (a[RL_ACT_STRAFE] > 2) a[RL_ACT_STRAFE] = 2;
	if (a[RL_ACT_UP] > 2) a[RL_ACT_UP] = 2;
	if (a[RL_ACT_YAW] >= RL_YAW_BINS) a[RL_ACT_YAW] = RL_YAW_BINS-1;
	if (a[RL_ACT_PITCH] >= RL_PITCH_BINS) a[RL_ACT_PITCH] = RL_PITCH_BINS-1;
	if (a[RL_ACT_WEAPON] > RL_WEAPONS) a[RL_ACT_WEAPON] = 0;

	// a change of weapon is the "use" command a number key sends
	n = a[RL_ACT_WEAPON];
	if (n && ent->health > 0)
	{
		it = FindItem ((char *)rl_weapons[n-1]);
		if (it && it->use && it != client->pers.weapon && it != client->newweapon
			&& client->pers.inventory[ITEM_INDEX(it)])
			it->use (ent, it);
	}

	memset (&cmd, 0, sizeof(cmd));
	cmd.msec = RL_STEP_MSEC;
	cmd.lightlevel = RL_LIGHT_LEVEL;
	// the speeds a client sends when running; Pmove holds them to its own top speed
	cmd.forwardmove = (a[RL_ACT_FORWARD] - 1) * 400;
	cmd.sidemove = (a[RL_ACT_STRAFE] - 1) * 400;
	cmd.upmove = (a[RL_ACT_UP] - 1) * 400;
	if (a[RL_ACT_FIRE])
		cmd.buttons |= BUTTON_ATTACK;

	angle = client->v_angle[PITCH] + rl_pitch_bins[a[RL_ACT_PITCH]];
	if (angle > 89)
		angle = 89;
	if (angle < -89)
		angle = -89;
	cmd.angles[PITCH] = ANGLE2SHORT(angle) - client->ps.pmove.delta_angles[PITCH];
	angle = client->v_angle[YAW] + rl_yaw_bins[a[RL_ACT_YAW]];
	cmd.angles[YAW] = ANGLE2SHORT(angle) - client->ps.pmove.delta_angles[YAW];
	cmd.angles[ROLL] = -client->ps.pmove.delta_angles[ROLL];

	ClientThink (ent, &cmd);
}

/*
================
RL_Observe

Fills in everything the other program reads after a step or a reset.
================
*/
static void RL_Observe (qboolean reset)
{
	edict_t		*ent = &g_edicts[1];
	int			i;

	if (reset)
	{
		rl_block->step = 0;
		rl_block->done = RL_DONE_NO;
		rl_dealt = rl_taken = 0;
		rl_kills_seen = level.killed_monsters;
	}
	else
		rl_block->step++;

	if (!rl_block->done)
	{
		if (level.intermissiontime)
			rl_block->done = RL_DONE_EXIT;
		else if (ent->health <= 0)
			rl_block->done = RL_DONE_DEATH;
		else if (rl_block->time_limit > 0 && rl_block->step >= rl_block->time_limit)
			rl_block->done = RL_DONE_TIME;
	}

	for (i=0 ; i<RL_GAIN_FLOATS ; i++)
		rl_block->gain[i] = 0;
	rl_block->gain[RL_GAIN_DEALT] = rl_dealt;
	rl_block->gain[RL_GAIN_TAKEN] = rl_taken;
	rl_block->gain[RL_GAIN_KILLS] = level.killed_monsters - rl_kills_seen;
	rl_kills_seen = level.killed_monsters;

	RL_Perceive (ent, rl_taken);
	rl_dealt = rl_taken = 0;

	Teach_Think (ent);

	rl_block->death_means = rl_block->death_by = 0;
	if (rl_block->done == RL_DONE_DEATH)
	{
		rl_block->death_means = meansOfDeath & ~MOD_FRIENDLY_FIRE;
		if (ent->enemy && ent->enemy != ent)
			rl_block->death_by = RL_MonsterType (ent->enemy);
	}

	VectorCopy (ent->s.origin, rl_block->origin);
	if (ent->client)
		VectorCopy (ent->client->v_angle, rl_block->angles);
	rl_block->monsters_total = level.total_monsters;
	rl_block->monsters_killed = level.killed_monsters;
	rl_block->hash = RL_Hash ();
}

/*
================
RL_Command

"sv rl <what>", from the server only.
================
*/
void RL_Command (void)
{
	char	*what;

	if (!RL_Active ())
	{
		gi.cprintf (NULL, PRINT_HIGH, "The game is not being driven.\n");
		return;
	}

	what = gi.argv(2);
	if (!strcmp (what, "act"))
		RL_Act ();
	else if (!strcmp (what, "observe"))
		RL_Observe (false);
	else if (!strcmp (what, "reset"))
	{
		// the game library can have random numbers of its own, apart from
		// the server's that were seeded before the map was loaded
		srand ((unsigned)rl_block->seed);
		// Building the navigation graph moves doors and lifts about. They are
		// put back, but not into the same places in the server's lists, so
		// the episode is begun again from a clean map, with the graph on file.
		if (rl_block->flags & RL_FLAG_NOTARGET)
			g_edicts[1].flags |= FL_NOTARGET;
		if (rl_block->flags & RL_FLAG_NOMONSTERS)
		{
			edict_t	*e;
			int		i;

			for (i=game.maxclients+1, e=g_edicts+i ; i<globals.num_edicts ; i++, e++)
				if (e->inuse && (e->svflags & SVF_MONSTER))
					G_FreeEdict (e);
		}
		rl_block->restart = Teach_Reset () && gi.cvar ("rl_nav", "", 0)->string[0];
		RL_Observe (true);
	}
}
