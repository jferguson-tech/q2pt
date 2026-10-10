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
// g_rl_teach.c -- the scripted player: what it would do from where the
// player is now, as an action of the kind the driven player takes (g_rl.h).
//
// It is asked after every step, whoever moved the player, and answers from
// the state of the world and of the player alone, not from what it meant to
// do a step ago. What is remembered here is about the player, not about any
// plan: the node it last stood at, how fast its view was turning, and, for
// the explorer, which goal it is on and when it was given it.
//
// It has two modes. As an explorer it walks to one node after another, each
// drawn from the seed, by the cheapest route the navigation graph has. As a
// player it does the planner's jobs (g_rl_plan.c) until the map's exit is
// reached, turns aside for items it has a use for, and fights what it sees
// (g_rl_fight.c).

#include "g_local.h"
#include "g_rl.h"
#include "g_rl_nav.h"

extern const float rl_yaw_bins[RL_YAW_BINS];
extern const float rl_pitch_bins[RL_PITCH_BINS];
extern const char *rl_weapons[RL_WEAPONS];

qboolean Pickup_Health (edict_t *ent, edict_t *other);
edict_t *Fight_Target (edict_t *ent, vec3_t eye);
qboolean Fight_Sees (edict_t *ent, vec3_t eye, edict_t *m);
edict_t *Fight_Hunter (edict_t *ent, vec3_t eye);
qboolean Fight_Clear (edict_t *ent, vec3_t eye);
qboolean Fight_Reaches (edict_t *ent, vec3_t eye, edict_t *m);
void Fight_Aim (edict_t *ent, edict_t *enemy, vec3_t eye, float *yaw, float *pitch, float *dist);
int Fight_Weapon (edict_t *ent, float dist);
qboolean Fight_Melee (edict_t *m);
float Fight_TimeToKill (edict_t *ent, edict_t *m);

// The view's turn may change by no more than this from one step to the
// next, in degrees a step, so that it swings as a hand on a mouse does and
// does not snap.
#define	TEACH_YAW_ACCEL		12.0f
#define	TEACH_PITCH_ACCEL	10.0f

#define	TEACH_STAND			4.0f	// seconds a monster may take to kill and still be stood before

#define	TEACH_ARRIVE		32.0f	// a goal is reached within this of it
#define	TEACH_REPLAN		16		// steps between looks at the doors' states

static float	*teach_togo;		// cost from every node to the goal
static float	*teach_reach;		// cost to every node from the player's
static qboolean	teach_have_plan;
static edict_t	*teach_errand;		// an item being turned aside for
#define	TEACH_MAX_BARRELS	64
static edict_t	*teach_barrels[TEACH_MAX_BARRELS];	// the map's barrels that blow up
static int		teach_num_barrels;
static int		teach_goal = -1;
static int		teach_goal_number;	// how many goals have been drawn
static int		teach_goal_step;	// when this one was
static int		teach_goal_limit;	// steps it may take
static int		teach_planned;		// the step the costs were last worked out
static int		teach_anchor = -1;	// the node the player was last at
static float	teach_last_yaw, teach_last_pitch;	// the view a step ago
static float	teach_yaw_rate, teach_pitch_rate;	// and how it was turning
static float	teach_last_togo;

static float AngleDiff (float a, float b)
{
	float	d = a - b;

	while (d > 180)
		d -= 360;
	while (d < -180)
		d += 360;
	return d;
}

/*
================
Teach_Turn

The bin that turns toward an angle err away as fast as can be done without
passing it, given the rate the view is turning at now.

A bin can be taken when it differs from the present rate by no more than
accel. Of those, the fastest is taken from which the turn can still be
slowed to a stop, a step at a time within accel, before err is used up. If
there is none, the turn is too fast already and the slowest allowed is taken.
================
*/
static float Teach_StopAfter (float rate, const float *bins, int n, float accel)
{
	float	total = 0, next;
	int		i;

	while (rate > 0)
	{
		next = rate;
		for (i=0 ; i<n ; i++)
			if (bins[i] >= 0 && bins[i] >= rate - accel - 0.01f && bins[i] < next)
				next = bins[i];
		if (next >= rate)
			break;
		rate = next;
		total += rate;
	}
	return total;
}

static int Teach_Turn (float err, float rate, const float *bins, int n, float accel)
{
	int		i, best, slowest;
	float	b, sign = err < 0 ? -1 : 1;

	// Within a degree, the bin nearest the error, even if that passes it by a
	// little: an opening no wider than the player is entered only when the
	// feet point at it to a fraction of a degree, and the view comes to that
	// by closing on it from both sides.
	if (fabs (err) <= 1 && fabs (rate) <= 1)
	{
		best = n / 2;
		for (i=0 ; i<n ; i++)
			if (fabs (err - bins[i]) < fabs (err - bins[best]))
				best = i;
		return best;
	}

	best = slowest = -1;
	for (i=0 ; i<n ; i++)
	{
		b = bins[i];
		if (fabs (b - rate) > accel + 0.01f)
			continue;
		if (slowest == -1 || fabs (b) < fabs (bins[slowest]))
			slowest = i;
		if (b * sign < 0)
			continue;
		if (fabs (b) + Teach_StopAfter (fabs (b), bins, n, accel) > fabs (err) + 0.01f)
			continue;
		if (best == -1 || fabs (b) > fabs (bins[best]))
			best = i;
	}
	if (best != -1)
		return best;
	if (slowest != -1)
		return slowest;
	return n / 2;
}

/*
================
Teach_Step

The link to take from a node: the open one with the least cost left
================
*/
static nav_link_t *Teach_Step (int n)
{
	nav_link_t	*l, *best;
	int			i;
	float		c, bestc;

	best = NULL;
	bestc = NAV_FAR;
	if (n < 0)
		return NULL;
	for (i=0, l=&nav_links[nav_nodes[n].first_link] ; i<nav_nodes[n].num_links ; i++, l++)
	{
		if (teach_togo[l->to] >= NAV_FAR || !Nav_LinkOpen (l))
			continue;
		c = l->cost + teach_togo[l->to];
		if (c < bestc)
		{
			bestc = c;
			best = l;
		}
	}
	return best;
}

/*
================
Teach_Try

Tries a link's move from where the player is (Nav_TryLink). It will do when
the player would come to rest at the link's far node, or anywhere the graph
has a node from which the way is shorter than the way from here: 2 then.
1 when it would come to rest unhurt but no nearer, 0 when it would not.
================
*/
static int Teach_Try (edict_t *ent, nav_link_t *link, int *up)
{
	vec3_t	end, to, from, left;
	int		n;

	if (!Nav_TryLink (ent, link, end, up))
		return 0;
	// one that goes nowhere is shut by something, and is no better
	if (fabs (end[0] - ent->s.origin[0]) + fabs (end[1] - ent->s.origin[1]) + fabs (end[2] - ent->s.origin[2]) < 6)
		return 0;
	Nav_NodeOrigin (link->to, to);
	VectorSubtract (ent->s.origin, to, from);
	VectorSubtract (end, to, left);
	if (fabs (left[0]) < 48 && fabs (left[1]) < 48 && fabs (left[2]) < 40
		&& VectorLength (left) < VectorLength (from) - 4)
		return 2;		// at the link's far node, near enough, and nearer than now
	n = Nav_NodeNear (end, 40, 32);
	return n != -1 && teach_togo[n] < teach_togo[teach_anchor] - 0.01f ? 2 : 1;
}

/*
================
Teach_NewGoal

Draws the next node to walk to: one of those that can be reached from where
the player is as the doors and lifts stand, picked by the seed and the
goal's number. With nowhere to go there is no goal, and the teacher stands.
================
*/
static void Teach_NewGoal (void)
{
	unsigned	h;
	int			n, count, pick;

	teach_goal = -1;
	teach_goal_step = rl_block->step;
	teach_planned = rl_block->step;
	teach_last_togo = NAV_FAR;
	if (teach_anchor < 0)
		return;

	Nav_CostsFrom (teach_anchor, teach_togo);
	count = 0;
	for (n=0 ; n<nav_num_nodes ; n++)
		if (teach_togo[n] < NAV_FAR && teach_togo[n] > 1)
			count++;
	if (!count)
		return;

	h = (unsigned)rl_block->seed * 2654435761u + (unsigned)teach_goal_number * 40503u;
	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	pick = h % count;
	for (n=0 ; n<nav_num_nodes ; n++)
		if (teach_togo[n] < NAV_FAR && teach_togo[n] > 1 && !pick--)
			break;
	teach_goal = n;
	teach_goal_number++;

	Nav_CostsTo (teach_goal, teach_togo);
	// four times what the route should take, and five seconds over
	teach_goal_limit = 50 + (int)(teach_togo[teach_anchor] * 40);
	teach_last_togo = teach_togo[teach_anchor];
}

/*
================
Teach_Reset

Returns true when the navigation graph had to be built, and the episode
should be started over on a map nothing has been moved about in.
================
*/
qboolean Teach_Reset (void)
{
	edict_t		*ent = &g_edicts[1], *e;
	qboolean	built;
	int			i;

	built = Nav_Load ();
	Haz_Find ();
	teach_togo = gi.TagMalloc ((nav_num_nodes + 1) * sizeof(float), TAG_LEVEL);
	teach_reach = gi.TagMalloc ((nav_num_nodes + 1) * sizeof(float), TAG_LEVEL);
	teach_goal = -1;
	teach_goal_number = 0;
	teach_anchor = -1;
	teach_planned = -1000;
	teach_have_plan = false;
	teach_errand = NULL;
	teach_yaw_rate = teach_pitch_rate = 0;
	if (ent->client)
	{
		teach_last_yaw = ent->client->v_angle[YAW];
		teach_last_pitch = ent->client->v_angle[PITCH];
	}
	teach_num_barrels = 0;
	for (i=game.maxclients+1, e=g_edicts+i ; i<globals.num_edicts ; i++, e++)
		if (e->inuse && e->classname && !strcmp (e->classname, "misc_explobox")
			&& teach_num_barrels < TEACH_MAX_BARRELS)
			teach_barrels[teach_num_barrels++] = e;

	rl_block->nav_count = nav_num_nodes;
	rl_block->goals_reached = 0;
	rl_block->goals_failed = 0;
	return built;
}

/*
================
Teach_Explore

Keeps the explorer's goal: a new one when this is reached or has taken too
long. Returns false when there is nowhere to go.
================
*/
static qboolean Teach_Explore (vec3_t origin)
{
	vec3_t	node, d;

	Nav_NodeOrigin (teach_goal >= 0 ? teach_goal : teach_anchor, node);
	VectorSubtract (node, origin, d);
	if (teach_goal < 0)
	{	// none could be had: look again now and then, in case a door has opened
		if (!rl_block->step || rl_block->step - teach_goal_step >= TEACH_REPLAN)
			Teach_NewGoal ();
	}
	else if (teach_anchor == teach_goal || VectorLength (d) < TEACH_ARRIVE)
	{
		rl_block->goals_reached++;
		Teach_NewGoal ();
	}
	else if (rl_block->step - teach_goal_step > teach_goal_limit)
	{
		rl_block->goals_failed++;
		Teach_NewGoal ();
	}
	else if (rl_block->step - teach_planned >= TEACH_REPLAN)
	{
		Nav_CostsTo (teach_goal, teach_togo);
		teach_planned = rl_block->step;
	}
	rl_block->nav_goal = teach_goal;
	return teach_goal >= 0;
}

/*
================
Teach_Wants

How many seconds out of its way the player would go for an item lying in the
map, or 0 for one it has no use for: health when hurt, the more the worse
hurt; armour when short of it; a weapon it has not got; ammunition for a
weapon it has when that is running low.
================
*/
static float Teach_Wants (edict_t *ent, edict_t *e)
{
	gclient_t	*client = ent->client;
	gitem_t		*it = e->item, *weapon;
	int			index = ITEM_INDEX(it), armor, i;

	if (it->pickup == Pickup_Health)
	{
		if (ent->health >= 100)
			return 0;
		return 2 + (100 - ent->health) * 0.25f;
	}
	if (it->flags & IT_ARMOR)
	{
		armor = ArmorIndex (ent);
		if (armor && client->pers.inventory[armor] >= 100)
			return 0;
		return 6;
	}
	if (it->flags & IT_WEAPON)
		return client->pers.inventory[index] ? 0 : 45;	// the blaster alone does not get far
	if (it->flags & IT_AMMO)
	{
		if (client->pers.inventory[index] >= 3 * it->quantity)
			return 0;
		for (i=0 ; i<RL_WEAPONS ; i++)
		{
			weapon = FindItem ((char *)rl_weapons[i]);
			if (weapon && weapon->ammo && client->pers.inventory[ITEM_INDEX(weapon)]
				&& FindItem (weapon->ammo) == it)
				return 5;
		}
	}
	return 0;
}

/*
================
Teach_Errand

The item most worth turning aside for from where the player is, or NULL:
the one that costs the least of what it is worth, among those within what
they are worth. teach_reach is the cost from the player's node to every other.
================
*/
static edict_t *Teach_Errand (edict_t *ent)
{
	edict_t	*e, *best;
	int		i, n;
	float	worth, cost, share, best_share;
	vec3_t	p;

	best = NULL;
	best_share = 1;
	for (i=game.maxclients+1, e=g_edicts+i ; i<globals.num_edicts ; i++, e++)
	{
		if (!e->inuse || !e->item || e->solid != SOLID_TRIGGER || (e->svflags & SVF_NOCLIENT))
			continue;
		worth = Teach_Wants (ent, e);
		// nothing is worth much of a walk when the way out is a few steps off
		if (teach_have_plan && teach_togo[teach_anchor] < NAV_FAR && worth > 4 + 2 * teach_togo[teach_anchor])
			worth = 4 + 2 * teach_togo[teach_anchor];
		if (worth <= 0)
			continue;
		// only one that lies at a node, with nothing between the two
		n = Nav_NodeNear (e->s.origin, 28, 40);
		if (n == -1 || teach_reach[n] >= NAV_FAR)
			continue;
		// nothing is dived for: the air does not last
		if (nav_nodes[n].flags & NODE_WATER)
			continue;
		if (teach_have_plan && teach_togo[n] >= NAV_FAR)
			continue;
		Nav_NodeOrigin (n, p);
		if (!Nav_Straight (ent, p, e->s.origin, true, MASK_SOLID))
			continue;
		cost = teach_reach[n];
		share = cost / worth;
		if (share < best_share)
		{
			best_share = share;
			best = e;
		}
	}
	return best;
}

/*
================
Teach_Plan

Keeps the plan of the player that is out to finish the map: looked at again
every TEACH_REPLAN steps and whenever the player is somewhere the plan has
no way from. An item worth the walk comes before the way out.
================
*/
static void Teach_Plan (edict_t *ent)
{
	int		n;

	if (rl_block->step - teach_planned < TEACH_REPLAN && teach_have_plan
		&& teach_togo[teach_anchor] < NAV_FAR
		&& (!teach_errand || (teach_errand->inuse && teach_errand->solid == SOLID_TRIGGER)))
		return;
	teach_planned = rl_block->step;

	// The way out first, so that an item is turned aside for only where the
	// way out can be taken up again: not down a drop there is no way back from.
	teach_have_plan = Plan_Update (teach_anchor, teach_togo);
	Nav_CostsFrom (teach_anchor, teach_reach);
	teach_errand = Teach_Errand (ent);
	if (teach_errand && gi.cvar ("rl_debug", "0", 0)->value)
		gi.dprintf ("errand %s worth %.0f\n", teach_errand->classname, Teach_Wants (ent, teach_errand));
	if (teach_errand)
	{
		n = Nav_NodeNear (teach_errand->s.origin, 28, 40);
		Nav_CostsTo (n, teach_togo);
		teach_have_plan = true;
	}
}

/*
================
Teach_Dodge

Where to step while fighting: the node next to the player's that lies most
to one side of the line to the monster, the side changing every second and
a half, and rather away from a monster that is close. Only single steps on
the level are taken, so that a dodge is never a fall, and only to where the
monster can still be seen from. Returns false when there is none, and the
player stands.
================
*/
static qboolean Teach_Dodge (edict_t *ent, vec3_t origin, edict_t *monster, float dist, vec3_t go)
{
	float		*enemy = monster->s.origin;
	nav_link_t	*l;
	int			i, k;
	float		score, best, side;
	vec3_t		to, across, d, p, v;
	qboolean	found = false, beside, melee;

	melee = Fight_Melee (monster);
	VectorSubtract (enemy, origin, to);
	to[2] = 0;
	VectorNormalize (to);
	side = ((rl_block->step / 15) & 1) ? 1 : -1;
	VectorSet (across, -to[1] * side, to[0] * side, 0);

	best = 0.2f;
	for (i=0, l=&nav_links[nav_nodes[teach_anchor].first_link] ; i<nav_nodes[teach_anchor].num_links ; i++, l++)
	{
		if (l->type != NAV_WALK || l->cost > 0.25f || !Nav_LinkOpen (l))
			continue;
		// nor to where the way cannot be taken up again
		if (teach_have_plan && teach_togo[teach_anchor] < NAV_FAR && teach_togo[l->to] >= NAV_FAR)
			continue;
		Nav_NodeOrigin (l->to, p);
		VectorSubtract (p, origin, d);
		if (fabs (d[2]) > 20)
			continue;
		d[2] = 0;
		if (VectorNormalize (d) < 8)
			continue;
		if (!Nav_StepSafe (ent, d, false))
			continue;
		score = DotProduct (d, across);
		// Away from a barrel, which a stray shot sets off: a step that takes
		// the player from beside one is the best there is, wherever it leads,
		// and a step to beside one is the worst.
		beside = false;
		for (k=0 ; k<teach_num_barrels ; k++)
			if (teach_barrels[k]->inuse)
			{
				VectorSubtract (origin, teach_barrels[k]->s.origin, v);
				v[2] = 0;
				if (VectorNormalize (v) < 220)
				{
					score += 2 * DotProduct (d, v);
					beside = true;
				}
				VectorSubtract (teach_barrels[k]->s.origin, p, v);
				if (VectorLength (v) < 200)
					score -= 1;
			}
		// not round a corner from the monster: the fight is to be finished
		p[2] += ent->viewheight;
		if (!beside && !melee && !Fight_Sees (ent, p, monster))
			continue;
		if (melee && dist < 400)
			score = 0.3f * score - 1.5f * DotProduct (d, to);	// it can only hit from close: keep away
		else if (dist < 250)
			score -= 0.7f * DotProduct (d, to);
		else if (dist > 600)
			score += 0.4f * DotProduct (d, to);
		if (score > best)
		{
			best = score;
			VectorCopy (d, go);
			found = true;
		}
	}
	return found;
}

/*
================
Teach_Think

Fills in the teacher's action for the state the player is in, the guide for
the player that is told the way, and the progress made since the last call.
================
*/
void Teach_Think (edict_t *ent)
{
	gclient_t	*client = ent->client;
	int			*act = rl_block->teacher;
	float		*guide = rl_block->guide;
	nav_link_t	*link, *look;
	plan_job_t	*job;
	edict_t		*mover, *enemy;
	vec3_t		origin, eye, target, at, d, go, node, dir, side, p, q;
	float		yaw, pitch, want_yaw, want_pitch, err, best_err, dist, a, across;
	float		aim_yaw, aim_pitch, aim_dist;
	int			f, s, bf, bs, i, n, up, link_type, tried = 0;
	qboolean	grounded, swimming, hold, ducked, snap, fire, have, exact, wary, launch, begun, stuck, run;
	float		route_yaw, speed, c, aim_err = 0;
	trace_t		tr;

	act[RL_ACT_FORWARD] = 1;
	act[RL_ACT_STRAFE] = 1;
	act[RL_ACT_UP] = 1;
	act[RL_ACT_YAW] = RL_YAW_BINS/2;
	act[RL_ACT_PITCH] = RL_PITCH_BINS/2;
	act[RL_ACT_FIRE] = 0;
	act[RL_ACT_WEAPON] = 0;
	memset (guide, 0, sizeof(rl_block->guide));
	rl_block->gain[RL_GAIN_PROGRESS] = 0;
	rl_block->nav_node = -1;
	rl_block->nav_goal = teach_goal;
	rl_block->link_type = -1;
	rl_block->link_ent = 0;
	rl_block->route_left = -1;
	rl_block->job_kind = 0;
	rl_block->job_ent = 0;
	rl_block->fighting = 0;

	if (!client || !nav_num_nodes || !teach_togo)
		return;

	yaw = client->v_angle[YAW];
	pitch = client->v_angle[PITCH];
	teach_yaw_rate = AngleDiff (yaw, teach_last_yaw);
	teach_pitch_rate = AngleDiff (pitch, teach_last_pitch);
	if (teach_yaw_rate > 30) teach_yaw_rate = 30;
	if (teach_yaw_rate < -30) teach_yaw_rate = -30;
	if (teach_pitch_rate > 20) teach_pitch_rate = 20;
	if (teach_pitch_rate < -20) teach_pitch_rate = -20;
	teach_last_yaw = yaw;
	teach_last_pitch = pitch;

	if (ent->health <= 0 || level.intermissiontime)
		return;

	VectorCopy (ent->s.origin, origin);
	VectorCopy (origin, eye);
	eye[2] += ent->viewheight;
	grounded = ent->groundentity != NULL;
	swimming = ent->waterlevel >= 2;
	ducked = (client->ps.pmove.pm_flags & PMF_DUCKED) != 0;

	// in the air the player is still on the move it left the ground with
	if (grounded || swimming || teach_anchor < 0)
	{
		n = Nav_Nearest (ent, origin);
		if (n >= 0)
			teach_anchor = n;
	}
	rl_block->nav_node = teach_anchor;
	if (teach_anchor < 0)
		return;

	// ---- where to: the explorer's goal, or the plan's next job
	job = NULL;
	if (rl_block->mode == RL_MODE_PLAY)
	{
		Teach_Plan (ent);
		have = teach_have_plan;
		if (have && !teach_errand)
			job = Plan_JobAt (teach_anchor);
		if (teach_errand)
		{
			rl_block->job_kind = PLAN_PICKUP;
			rl_block->job_ent = teach_errand - g_edicts;
		}
		else if (job)
		{
			rl_block->job_kind = job->kind;
			rl_block->job_ent = job->ent - g_edicts;
		}
	}
	else
		have = Teach_Explore (origin);

	if (have && teach_togo[teach_anchor] < NAV_FAR)
	{
		if (teach_last_togo < NAV_FAR)
			rl_block->gain[RL_GAIN_PROGRESS] = teach_last_togo - teach_togo[teach_anchor];
		teach_last_togo = teach_togo[teach_anchor];
		rl_block->route_left = teach_togo[teach_anchor];
	}
	else
		teach_last_togo = NAV_FAR;

	// trial steps are not to end in a hole there is no way out of
	nav_step_togo = have && teach_togo[teach_anchor] < NAV_FAR ? teach_togo : NULL;

	// ---- the feet: along the route, or to the job at hand
	hold = true;
	snap = true;
	fire = false;
	launch = begun = stuck = false;
	up = 1;
	link_type = NAV_WALK;
	VectorClear (go);
	want_yaw = yaw;
	want_pitch = 0;
	VectorCopy (origin, target);
	VectorCopy (origin, at);
	Nav_NodeOrigin (teach_anchor, at);

	link = have ? Teach_Step (teach_anchor) : NULL;
	if (teach_errand && have && teach_togo[teach_anchor] < 0.35f
		&& (teach_togo[teach_anchor] <= 0 || !link
			|| Nav_Straight (ent, origin, teach_errand->s.origin, ducked, MASK_PLAYERSOLID)))
	{	// at the item, with nothing between: onto it
		VectorCopy (teach_errand->s.origin, target);
		link = NULL;
		hold = false;
	}
	else if (job)
	{
		Plan_Point (job, target);
		link = NULL;
		hold = false;
		if (job->kind == PLAN_SHOOT)
		{	// stand, point at it and shoot; the blaster needs no ammunition
			hold = true;
			snap = false;
			if (!Plan_ShotAt (job->ent, eye, target))
			{	// not in the clear from just here: to the node, where it was
				VectorCopy (at, target);
				hold = false;
				snap = true;
			}
			VectorSubtract (target, eye, d);
			want_yaw = RAD2DEG_F(atan2 (d[1], d[0]));
			want_pitch = -RAD2DEG_F(atan2 (d[2], sqrt (d[0]*d[0] + d[1]*d[1])));
			fire = hold && fabs (AngleDiff (want_yaw, yaw)) < 0.6f && fabs (want_pitch - pitch) < 0.6f;
			if (!hold)
				want_pitch = 0;
			if (gi.cvar ("rl_debug", "0", 0)->value > 1)
				gi.dprintf ("   shoot %i at %.0f %.0f %.0f from eye %.0f %.0f %.0f: clear %i pitch %.1f want %.1f\n",
					(int)(job->ent - g_edicts), target[0], target[1], target[2], eye[0], eye[1], eye[2], hold,
					pitch, want_pitch);
		}
		else if ((job->kind == PLAN_TOUCH || job->kind == PLAN_EXIT)
			&& (job->ent->movedir[0] || job->ent->movedir[1])
			&& strcmp (job->ent->classname, "func_button"))
		{	// a trigger that only takes one who faces its way
			snap = false;
			want_yaw = RAD2DEG_F(atan2 (job->ent->movedir[1], job->ent->movedir[0]));
		}
	}
	else if (link)
	{
		hold = false;
		link_type = link->type;
		rl_block->link_type = link->type;
		rl_block->link_ent = link->ent;
		Nav_NodeOrigin (link->to, target);

		switch (link->type)
		{
		case NAV_JUMP:
		case NAV_WALK:
		case NAV_DUCK:
			if (link->type == NAV_DUCK)
				up = 0;
			// A jump, or a walk that took more than one step (it goes off an
			// edge), ends where it did only if it is made as it was when
			// the link was found: from the node, on the heading it was made
			// on. So to the node first; then along the heading, and on along
			// it while in the air or still on the line. Aiming at the landing
			// place from wherever the player is can miss the edge altogether.
			if (link->type != NAV_JUMP && link->steps < 2)
				break;
			if (swimming)
			{
				if (link->type == NAV_JUMP)
					up = 2;
				break;
			}
			a = link->heading * (360.0f / 16);
			VectorSet (dir, cos (a * M_PI / 180), sin (a * M_PI / 180), 0);
			VectorSubtract (origin, at, d);
			d[2] = 0;
			across = d[0] * dir[1] - d[1] * dir[0];		// how far off the line
			// The move is begun when a trial of it from here, as the player
			// is moving now, comes down where the link does (Nav_TryLink).
			// Until then the player goes to the node, crouched so as to
			// stop on it, and stands: the link was found from there, from
			// a standing start. If the trial fails even so, the move is
			// made as it was found and trusted, and kept up once begun.
			speed = sqrt (ent->velocity[0]*ent->velocity[0] + ent->velocity[1]*ent->velocity[1]);
			if (!grounded)
				launch = true;		// begun already
			else if ((tried = Teach_Try (ent, link, &i)) == 2)
				launch = true;
			else if (tried && link->type != NAV_JUMP && DotProduct (d, dir) > 0 && fabs (across) <= 10
				&& DotProduct (ent->velocity, dir) > 50)
				launch = begun = true;		// begun from the node, and not yet over the edge
			else if (VectorLength (d) > 8)
			{
				VectorCopy (at, target);
				up = 0;
			}
			else if (speed > 20)
				hold = true;
			else if (!tried && rl_block->mode == RL_MODE_PLAY)
			{	// standing on the node, and still it ends in harm or goes
				// nowhere: the link is struck out, and the way is worked
				// out again without it
				nav_link_bad[link - nav_links] = 1;
				teach_planned = -1000;
				hold = true;
			}
			else
				launch = true;
			if (gi.cvar ("rl_debug", "0", 0)->value > 1)
				gi.dprintf ("   exact link %i: tried %i launch %i begun %i hold %i off node %.1f speed %.0f bad %i\n",
					(int)(link - nav_links), tried, launch, begun, hold, VectorLength (d), speed,
					nav_link_bad[link - nav_links]);
			if (launch)
			{
				VectorMA (origin, 100, dir, target);
				target[2] = origin[2];
				if (link->type == NAV_JUMP && grounded)
					up = 2;
			}
			break;
		case NAV_CLIMB:
			up = 2;
			break;
		case NAV_RIDE:
			// A lift starts when the player is well inside its edges, where its
			// trigger is: so to its middle, and stay there.
			mover = &g_edicts[link->ent];
			VectorAdd (mover->absmin, mover->absmax, target);
			VectorScale (target, 0.5f, target);
			target[2] = origin[2];
			VectorSubtract (target, origin, d);
			d[2] = 0;
			hold = VectorLength (d) < 8;
			break;
		case NAV_SWIM:
			if (target[2] > origin[2] + 8)
				up = 2;
			else if (target[2] < origin[2] - 8)
				up = 0;
			// A swim of more than a step is one that leaves the water, over
			// a bank or up onto a ledge, and does so only from the right
			// place, facing the right way. As with a jump: when a trial of
			// it from here comes out, along its heading; until then to the
			// node, and to its depth.
			if (link->steps < 2 || !swimming)
				break;
			a = link->heading * (360.0f / 16);
			VectorSet (dir, cos (a * M_PI / 180), sin (a * M_PI / 180), 0);
			VectorSubtract (origin, at, d);
			if (Teach_Try (ent, link, &i) == 2)
			{
				VectorMA (origin, 100, dir, target);
				target[2] = origin[2];
				up = i;
			}
			else if (d[0]*d[0] + d[1]*d[1] > 8*8 || fabs (d[2]) > 8)
			{
				VectorCopy (at, target);
				up = at[2] > origin[2] + 4 ? 2 : at[2] < origin[2] - 4 ? 0 : 1;
			}
			break;
		}
		// under a low roof the player stays down, but a jump or a ladder that
		// leaves from there was found by standing up into it
		if ((nav_nodes[teach_anchor].flags & NODE_DUCK) && link->type != NAV_JUMP && link->type != NAV_CLIMB)
			up = 0;

		// A lift the link needs at one end of its travel and that is at the
		// other, or on its way: wait where the player is, not in its shaft.
		if (link->type != NAV_RIDE && link->ent && !strcmp (g_edicts[link->ent].classname, "func_plat")
			&& Nav_MoverAt (&g_edicts[link->ent]) != link->state)
			hold = true;
	}

	VectorSubtract (target, origin, d);
	dist = VectorLength (d);
	VectorCopy (d, go);

	// Something dead ahead: a corner caught, or a monster in the way. Pressing
	// on into it gets nowhere, so the feet step to the side that is clear
	// and has a clear way on, the side of the link's own line first; with
	// neither clear they back off. The view goes on looking down the route.
	if (!hold && !launch && (link_type == NAV_WALK || link_type == NAV_DUCK) && grounded && dist > 4)
	{
		VectorSet (dir, d[0], d[1], 0);
		VectorNormalize (dir);
		VectorMA (origin, dist < 6 ? dist : 6, dir, p);
		tr = gi.trace (origin, tv(-16,-16,-6), tv(16,16, ducked ? 4 : 32), p, ent, MASK_PLAYERSOLID);
		if (tr.fraction < 1 && tr.ent && tr.ent != world && Nav_MoverAt (tr.ent) == 0
			&& Nav_MoverGroup (tr.ent, &mover, 1))
			hold = true;		// a door on its way open: wait for it
		else if (job && tr.fraction < 1 && tr.ent == job->ent)
			;					// the thing to be pushed: push
		else if (tr.fraction >= 1 && !tr.startsolid && VectorLength (ent->velocity) < 20)
		{	// Clear ahead from the shins up, and yet the player is not moving:
			// something low is in the way that it has not stepped onto. Under
			// a low roof there is no room to step up, standing; a hop does it.
			tr = gi.trace (origin, tv(-16,-16,-23), tv(16,16, ducked ? 4 : 32), p, ent, MASK_PLAYERSOLID);
			if (tr.fraction < 1)
				up = 2;
		}
		else if (tr.fraction < 1 || tr.startsolid)
		{
			// First a little to one side of the way, and then more: a corner
			// caught by a hair is cleared by going on at a slant, where a
			// full step aside would only come back to the same hair. The
			// side the wall's face looks to is tried first.
			static const float	slants[6] = {12, -12, 25, -25, 45, -45};
			float	turn, c, sn;

			turn = (tr.plane.normal[0] * -dir[1] + tr.plane.normal[1] * dir[0]) < 0 ? -1 : 1;
			for (i=0 ; i<6 ; i++)
			{
				c = cos (slants[i] * turn * M_PI / 180);
				sn = sin (slants[i] * turn * M_PI / 180);
				VectorSet (side, dir[0]*c - dir[1]*sn, dir[0]*sn + dir[1]*c, 0);
				VectorMA (origin, 8, side, p);
				if (Nav_Straight (ent, origin, p, ducked, MASK_PLAYERSOLID)
					&& Nav_StepSafe (ent, side, ducked))
					break;
			}
			if (i < 6)
			{
				VectorCopy (side, go);
				goto slanted;
			}

			VectorSet (side, -dir[1], dir[0], 0);		// to the left of the way
			// which side of the line from the node to the target the player is on
			across = (target[0]-at[0]) * (origin[1]-at[1]) - (target[1]-at[1]) * (origin[0]-at[0]);
			if (across > 0)
				VectorInverse (side);
			VectorScale (dir, -1, go);
			if (!Nav_StepSafe (ent, go, ducked))
				hold = stuck = true;		// no room behind either: stand
			for (i=0 ; i<2 ; i++)
			{
				VectorMA (origin, 12, side, p);
				VectorMA (p, 6, dir, q);
				if (Nav_Straight (ent, origin, p, ducked, MASK_PLAYERSOLID)
					&& Nav_Straight (ent, p, q, ducked, MASK_PLAYERSOLID)
					&& Nav_StepSafe (ent, side, ducked))
				{
					VectorCopy (side, go);
					hold = stuck = false;
					break;
				}
				VectorInverse (side);
			}
slanted:	;
		}
	}

	// The view looks down the route a few links on, so that it turns into a
	// corner before the feet do. A jump or a ladder is faced squarely.
	if (snap && !hold)
	{
		want_yaw = RAD2DEG_F(atan2 (d[1], d[0]));
		if (link && (link->type == NAV_WALK || link->type == NAV_DUCK || link->type == NAV_SWIM))
		{
			look = link;
			n = link->to;
			for (i=0 ; i<4 && look ; i++)
			{
				n = look->to;
				look = Teach_Step (n);
				if (look && (look->type == NAV_JUMP || look->type == NAV_CLIMB))
					break;
			}
			Nav_NodeOrigin (n, node);
			node[0] -= origin[0];
			node[1] -= origin[1];
			if (node[0]*node[0] + node[1]*node[1] > 24*24)
				want_yaw = RAD2DEG_F(atan2 (node[1], node[0]));
		}
	}

	// A jump or a walk off an edge lands where it was found to only when it
	// is made on its heading, which the feet can keep to only with the view
	// square to it.
	exact = link && !job && (link->type == NAV_JUMP
		|| ((link->type == NAV_WALK || link->type == NAV_DUCK) && link->steps >= 2));
	route_yaw = want_yaw;

	// ---- a monster in sight: the view and the trigger are for it, and the
	// feet dodge until it is dead
	enemy = rl_block->mode == RL_MODE_PLAY ? Fight_Target (ent, eye) : NULL;
	if (enemy)
	{
		rl_block->fighting = enemy - g_edicts;
		Fight_Aim (ent, enemy, eye, &aim_yaw, &aim_pitch, &aim_dist);
		want_yaw = aim_yaw;
		want_pitch = aim_pitch;
		snap = false;
		// near enough on to hit a thing a foot and a half wide at that distance
		err = RAD2DEG_F(atan2 (18, aim_dist));
		if (err < 1.5f)
			err = 1.5f;
		// (whether it is, is looked at again below, once the view has turned)
		aim_err = err;
		fire = fabs (AngleDiff (aim_yaw, yaw)) < err + 30 && fabs (aim_pitch - pitch) < err * 1.5f + 20;
		act[RL_ACT_WEAPON] = Fight_Weapon (ent, aim_dist);
		if (fire && !Fight_Clear (ent, eye))
		{	// a barrel in the line of fire, close by: no shot from here, and
			// no standing about either. The feet go on along the route.
			fire = false;
			enemy = NULL;
		}
	}
	if (enemy)
	{

		// A jump, a ladder, a ride or a swim is finished first. On the level
		// the feet leave the route to dodge, unless the player is badly hurt
		// and on its way to something that mends it, or at the exit.
		// Nor do they when the gun's line is shut where the eye's is not, as
		// over a rail: the view stays on the monster and the feet walk on
		// until the shots get through.
		// So a jump or a drop is not begun with the view on a monster: the
		// fight is finished at its top, or, if the shots do not reach, the
		// view goes back to the way and the move is made unfought.
		//
		// And a monster that would take more than a few seconds to kill with
		// the weapon in hand is not stood before at all:
		// the feet keep to the route and the shots are for when it is more
		// or less ahead. One well behind is left there, and the view goes
		// back to the way.
		// One that fights hand to hand is run from the same way, so long as
		// it is not close and in the way; then it is backed away from.
		VectorSubtract (enemy->s.origin, origin, p);
		p[2] = 0;
		c = VectorNormalize (p);
		VectorSet (q, d[0], d[1], 0);
		VectorNormalize (q);
		run = link && !hold && Fight_TimeToKill (ent, enemy) > TEACH_STAND
			&& !(Fight_Melee (enemy) && c < 250 && DotProduct (p, q) > 0.3f);
		if (run)
		{
			if (DotProduct (p, q) < -0.2f || Fight_Melee (enemy))
			{
				want_yaw = route_yaw;
				want_pitch = 0;
				snap = true;
				fire = false;
				aim_err = 0;
			}
		}
		else if (grounded && (link_type == NAV_WALK || link_type == NAV_DUCK || exact)
			&& !(job && job->kind == PLAN_EXIT)
			&& !(ent->health < 40 && teach_errand && teach_errand->item->pickup == Pickup_Health))
		{
			if (Fight_Reaches (ent, eye, enemy))
			{
				hold = !Teach_Dodge (ent, origin, enemy, aim_dist, go);
				if (hold && Fight_Melee (enemy) && c < 200)
				{	// no node to step back to: straight away from it then,
					// as far as the trial steps allow
					VectorScale (p, -1, go);
					hold = false;
				}
				if (!hold)
				{
					dist = 100;
					exact = false;
					launch = false;
					link_type = NAV_WALK;
					if (up == 2)
						up = 1;
				}
			}
			else if (exact)
			{
				want_yaw = route_yaw;
				want_pitch = 0;
				snap = true;
				fire = false;
				aim_err = 0;
			}
		}
	}
	else if (rl_block->mode == RL_MODE_PLAY && grounded && (link_type == NAV_WALK || link_type == NAV_DUCK)
		&& !(job && job->kind == PLAN_EXIT) && (enemy = Fight_Hunter (ent, eye)) != NULL
		&& Fight_TimeToKill (ent, enemy) <= TEACH_STAND)
	{	// one is coming: wait for it, facing where it is
		rl_block->fighting = enemy - g_edicts;
		VectorSubtract (enemy->s.origin, eye, p);
		want_yaw = RAD2DEG_F(atan2 (p[1], p[0]));
		want_pitch = 0;
		snap = false;
		hold = true;
		fire = false;
	}

	if (exact && !hold && !snap)
	{	// the view is wanted elsewhere but the feet are on such a move
		want_yaw = route_yaw;
		want_pitch = 0;
		snap = true;
		fire = false;
	}

	// Never standing in what hurts: back to the node, and up out of lava.
	if (hold && Haz_At (origin, ducked))
	{
		VectorSubtract (at, origin, go);
		go[2] = 0;
		if (VectorLength (go) > 4)
		{
			hold = false;
			dist = 100;
		}
		if (ent->waterlevel)
			up = 2;
	}

	// The feet go one of eight ways about the view, 45 degrees apart. So that
	// one of them is exactly the way to the target, the view settles on the
	// heading nearest the one wanted that is a whole number of 45s from it.
	// An opening no wider than the player is not entered any other way. When
	// the view has something to point at, it points at that and the feet
	// take the nearest of the eight.
	a = RAD2DEG_F(atan2 (go[1], go[0]));
	// Out of water onto a bank, and up a ladder, the player goes only the
	// way it faces.
	if (snap && !hold && dist > 1)
	{
		if (link_type == NAV_SWIM || link_type == NAV_CLIMB)
			want_yaw = a;
		else
			want_yaw = a + 45 * floor (AngleDiff (want_yaw, a) / 45 + 0.5f);
	}

	i = Teach_Turn (AngleDiff (want_yaw, yaw), teach_yaw_rate, rl_yaw_bins, RL_YAW_BINS, TEACH_YAW_ACCEL);
	act[RL_ACT_YAW] = i;
	yaw += rl_yaw_bins[i];
	i = Teach_Turn (want_pitch - pitch, teach_pitch_rate, rl_pitch_bins, RL_PITCH_BINS, TEACH_PITCH_ACCEL);
	act[RL_ACT_PITCH] = i;
	// The shot goes where the view points once this step has turned it.
	if (fire && aim_err > 0)
		fire = fabs (AngleDiff (want_yaw, yaw)) < aim_err && fabs (want_pitch - pitch - rl_pitch_bins[i]) < aim_err * 1.5f;
	act[RL_ACT_FIRE] = fire;

	// Of the eight ways the feet can go, the one nearest the way to the
	// target, as the view will stand once this step has turned it.
	bf = bs = 0;
	best_err = 1000;
	for (f=-1 ; f<=1 ; f++)
		for (s=-1 ; s<=1 ; s++)
		{
			if (!f && !s)
				continue;
			// sidemove is to the right, and angles grow to the left
			err = fabs (AngleDiff (a, yaw + RAD2DEG_F(atan2 (-s, f))));
			if (err < best_err)
			{
				best_err = err;
				bf = f;
				bs = s;
			}
		}

	if (launch && grounded && !begun && best_err > 4)
	{	// The trial was of feet going dead along the link's heading; with
		// the view not yet square to it they would go a little to one
		// side, and a small landing place is missed by that. Turn first.
		if (up == 2)
			up = 1;
		hold = true;
		launch = false;
	}
	if (link_type == NAV_CLIMB && fabs (AngleDiff (a, yaw)) > 30)
		up = 1;
	if (hold || (dist <= 2 && link_type == NAV_WALK))
		bf = bs = 0;

	// Before the feet are told anything, the step is tried (Nav_StepSafe):
	// would the player, going that way for a step and then stopping, come to
	// rest on a floor, out of harm? At 100 ms a step it goes 30 units and
	// slides 17 more, so the edge of a ledge is a step away long before it
	// looks it; and with the view on a monster the nearest of the eight ways
	// can be 22 degrees off the one meant. If the step fails it is tried
	// crouched, which is a third as fast; then the ways to either side; and
	// then the feet stay. Staying is tried too, since the player may be
	// sliding: if that fails, whichever way stops it. A jump or a drop that
	// the route makes on purpose, once begun from its node, is let be.
	wary = grounded && !launch && up != 2 && rl_block->mode == RL_MODE_PLAY
		&& (link_type == NAV_WALK || link_type == NAV_DUCK || link_type == NAV_JUMP);
	if (wary && (bf || bs))
	{
		int		tf, ts, k, done[3][3];

		memset (done, 0, sizeof(done));
		for (k=0 ; k<3 ; k++)
		{	// the nearest not yet tried
			tf = ts = 0;
			best_err = 1000;
			for (f=-1 ; f<=1 ; f++)
				for (s=-1 ; s<=1 ; s++)
				{
					if ((!f && !s) || done[f+1][s+1])
						continue;
					err = fabs (AngleDiff (a, yaw + RAD2DEG_F(atan2 (-s, f))));
					if (err < best_err)
					{
						best_err = err;
						tf = f;
						ts = s;
					}
				}
			if (best_err > 50)
				break;
			done[tf+1][ts+1] = 1;
			across = (yaw + RAD2DEG_F(atan2 (-ts, tf))) * M_PI / 180;
			VectorSet (side, cos (across), sin (across), 0);
			if (Nav_StepSafe (ent, side, up == 0))
				break;
			if (up == 1 && Nav_StepSafe (ent, side, true))
			{
				up = 0;
				break;
			}
		}
		if (k < 3 && best_err <= 50)
		{
			bf = tf;
			bs = ts;
		}
		else
		{
			// No way passes. Standing at a node, on a step the graph has
			// from that node, the graph is believed: the step was made
			// from here when it was found, and to stand is to stay for good.
			VectorSubtract (origin, at, side);
			side[2] = 0;
			speed = sqrt (ent->velocity[0]*ent->velocity[0] + ent->velocity[1]*ent->velocity[1]);
			if (!(link && !enemy && VectorLength (side) < 12 && speed < 20))
			{
				bf = bs = 0;
				stuck = true;
			}
		}
	}
	speed = sqrt (ent->velocity[0]*ent->velocity[0] + ent->velocity[1]*ent->velocity[1]);
	if (wary && stuck && !bf && !bs && !enemy && speed < 5 && teach_togo[teach_anchor] < NAV_FAR)
	{
		// Standing off the graph, on a perch it has no node on, with no step
		// that keeps to the level: the step down that ends at the node
		// nearest the goal, if any ends unhurt at one.
		c = teach_togo[teach_anchor] + 5;
		nav_step_drop = 72;
		for (f=-1 ; f<=1 ; f++)
			for (s=-1 ; s<=1 ; s++)
			{
				if (!f && !s)
					continue;
				across = (yaw + RAD2DEG_F(atan2 (-s, f))) * M_PI / 180;
				VectorSet (side, cos (across), sin (across), 0);
				if (!Nav_StepSafe (ent, side, false) || teach_togo[nav_step_node] >= c)
					continue;
				c = teach_togo[nav_step_node];
				bf = f;
				bs = s;
			}
		nav_step_drop = 20;
		if (bf || bs)
			up = 1;
	}
	if (wary && !bf && !bs && !Nav_StepSafe (ent, NULL, up == 0))
	{	// sliding to somewhere it should not be: the way that stops it
		for (f=-1 ; f<=1 && !bf && !bs ; f++)
			for (s=-1 ; s<=1 ; s++)
			{
				if (!f && !s)
					continue;
				across = (yaw + RAD2DEG_F(atan2 (-s, f))) * M_PI / 180;
				VectorSet (side, cos (across), sin (across), 0);
				if (DotProduct (side, ent->velocity) >= 0 || !Nav_StepSafe (ent, side, up == 0))
					continue;
				bf = f;
				bs = s;
				break;
			}
	}

	// In the air the feet still steer a little. They go the way that was
	// meant if a trial of that comes down somewhere safe; failing that they
	// are still, or go whichever way does.
	if (!grounded && !swimming && rl_block->mode == RL_MODE_PLAY && ent->waterlevel < 2)
	{
		qboolean	safe = false;

		if (bf || bs)
		{
			across = (yaw + RAD2DEG_F(atan2 (-bs, bf))) * M_PI / 180;
			VectorSet (side, cos (across), sin (across), 0);
			safe = Nav_AirSafe (ent, side);
		}
		if (!safe && Nav_AirSafe (ent, NULL))
		{
			bf = bs = 0;
			safe = true;
		}
		for (f=-1 ; f<=1 && !safe ; f++)
			for (s=-1 ; s<=1 ; s++)
			{
				if (!f && !s)
					continue;
				across = (yaw + RAD2DEG_F(atan2 (-s, f))) * M_PI / 180;
				VectorSet (side, cos (across), sin (across), 0);
				if (!Nav_AirSafe (ent, side))
					continue;
				bf = f;
				bs = s;
				safe = true;
				break;
			}
	}

	act[RL_ACT_FORWARD] = bf + 1;
	act[RL_ACT_STRAFE] = bs + 1;
	act[RL_ACT_UP] = up;
	if (gi.cvar ("rl_debug", "0", 0)->value > 1)
	{
		qboolean	g;
		void Nav_Predict (edict_t *ent, float yaw, int forward, int side, int up, vec3_t out, qboolean *ground);

		Nav_Predict (ent, yaw, bf * 400, bs * 400, (up - 1) * 400, p, &g);
		gi.dprintf ("   predict %.1f %.1f %.1f gnd %i\n", p[0], p[1], p[2], g);
	}

	if (gi.cvar ("rl_debug", "0", 0)->value)
	{
		gi.dprintf ("%i: at %.1f %.1f %.1f node %i -> %i type %i togo %.2f job %i/%i errand %i enemy %i move %i %i up %i hold %i fire %i hp %i water %i/%x haz %i yaw %.1f want %.1f go %.0f vel %.0f %.0f gnd %i\n",
			rl_block->step, origin[0], origin[1], origin[2], teach_anchor, link ? link->to : -1, link_type,
			teach_togo[teach_anchor] < NAV_FAR ? teach_togo[teach_anchor] : -1.0f,
			rl_block->job_kind, rl_block->job_ent, teach_errand ? (int)(teach_errand - g_edicts) : 0,
			rl_block->fighting, bf, bs, up, hold, fire, ent->health, ent->waterlevel, ent->watertype,
			Haz_At (origin, ducked), client->v_angle[YAW], want_yaw, a, ent->velocity[0], ent->velocity[1], grounded);
		if (rl_block->fighting)
		{
			edict_t	*m = &g_edicts[rl_block->fighting];

			gi.dprintf ("   foe %s at %.0f %.0f %.0f hp %i sees %i yaw %.1f want %.1f pitch %.1f want %.1f rate %.1f weapon %s state %i act %i\n",
				m->classname, m->s.origin[0], m->s.origin[1], m->s.origin[2], m->health,
				Fight_Sees (ent, eye, m), client->v_angle[YAW], want_yaw, client->v_angle[PITCH], want_pitch,
				teach_yaw_rate, client->pers.weapon ? client->pers.weapon->pickup_name : "none",
				client->weaponstate, act[RL_ACT_WEAPON]);
		}
		if (link && link->ent)
			gi.dprintf ("   mover %i %s wants %i is %i self %i\n", link->ent, g_edicts[link->ent].classname,
				link->state, Nav_MoverAt (&g_edicts[link->ent]), Nav_MoverSelf (&g_edicts[link->ent]));
	}

	// the guide: where the route goes next from the view as it is now
	if (dist > 1)
	{
		guide[RL_GUIDE_VALID] = 1;
		guide[RL_GUIDE_YAW] = AngleDiff (RAD2DEG_F(atan2 (d[1], d[0])), client->v_angle[YAW]);
		guide[RL_GUIDE_PITCH] = -RAD2DEG_F(atan2 (d[2], sqrt (d[0]*d[0] + d[1]*d[1]))) - client->v_angle[PITCH];
		guide[RL_GUIDE_DIST] = dist;
	}
}
