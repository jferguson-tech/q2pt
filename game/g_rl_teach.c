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
// So far it is an explorer: it walks to one node after another, each drawn
// from the seed, by the cheapest route the navigation graph has.

#include "g_local.h"
#include "g_rl.h"
#include "g_rl_nav.h"

extern const float rl_yaw_bins[RL_YAW_BINS];
extern const float rl_pitch_bins[RL_PITCH_BINS];

// The view's turn may change by no more than this from one step to the
// next, in degrees a step, so that it swings as a hand on a mouse does and
// does not snap.
#define	TEACH_YAW_ACCEL		12.0f
#define	TEACH_PITCH_ACCEL	10.0f

#define	TEACH_ARRIVE		32.0f	// a goal is reached within this of it
#define	TEACH_REPLAN		16		// steps between looks at the doors' states

static float	*teach_togo;		// cost from every node to the goal
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
	edict_t		*ent = &g_edicts[1];
	qboolean	built;

	built = Nav_Load ();
	teach_togo = gi.TagMalloc ((nav_num_nodes + 1) * sizeof(float), TAG_LEVEL);
	teach_goal = -1;
	teach_goal_number = 0;
	teach_anchor = -1;
	teach_yaw_rate = teach_pitch_rate = 0;
	if (ent->client)
	{
		teach_last_yaw = ent->client->v_angle[YAW];
		teach_last_pitch = ent->client->v_angle[PITCH];
	}
	rl_block->nav_count = nav_num_nodes;
	rl_block->goals_reached = 0;
	rl_block->goals_failed = 0;
	return built;
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
	vec3_t		origin, target, at, d, go, node;
	float		yaw, pitch, want_yaw, err, best_err, dist, a;
	int			f, s, bf, bs, i, n, up;
	qboolean	grounded, swimming, hold;
	edict_t		*mover;

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

	if (ent->health <= 0)
		return;

	VectorCopy (ent->s.origin, origin);
	grounded = ent->groundentity != NULL;
	swimming = ent->waterlevel >= 2;

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

	// the goal: a new one when this is reached, or has taken too long
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
	if (teach_goal < 0)
		return;

	if (teach_togo[teach_anchor] < NAV_FAR)
	{
		if (teach_last_togo < NAV_FAR)
			rl_block->gain[RL_GAIN_PROGRESS] = teach_last_togo - teach_togo[teach_anchor];
		teach_last_togo = teach_togo[teach_anchor];
	}

	link = Teach_Step (teach_anchor);
	if (!link)
		return;		// no way from here: stand until the goal times out
	rl_block->link_type = link->type;
	rl_block->link_ent = link->ent;
	rl_block->route_left = teach_togo[teach_anchor];

	Nav_NodeOrigin (link->to, target);
	Nav_NodeOrigin (teach_anchor, at);
	hold = false;
	up = 1;

	switch (link->type)
	{
	case NAV_JUMP:
		if (grounded)
		{	// the jump is made from the node itself, as it was when the link was found
			VectorSubtract (at, origin, d);
			d[2] = 0;
			if (VectorLength (d) > 10)
				VectorCopy (at, target);
			else
				up = 2;
		}
		else if (swimming)
			up = 2;
		break;
	case NAV_DUCK:
		up = 0;
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
		break;
	}
	// under a low roof the player stays down, but a jump or a ladder that
	// leaves from there was found by standing up into it
	if ((nav_nodes[teach_anchor].flags & NODE_DUCK) && link->type != NAV_JUMP && link->type != NAV_CLIMB)
		up = 0;

	// A lift the link needs at one end of its travel and that is at the
	// other, or on its way: wait where the player is, not in its shaft.
	if (link->type != NAV_RIDE && link->ent && Nav_MoverSelf (&g_edicts[link->ent])
		&& !strcmp (g_edicts[link->ent].classname, "func_plat")
		&& Nav_MoverAt (&g_edicts[link->ent]) != link->state)
		hold = true;

	VectorSubtract (target, origin, d);
	dist = VectorLength (d);

	// Something dead ahead: a corner caught, or a monster in the way. Pressing
	// on into it gets nowhere, so the feet step to the side that is clear
	// and has a clear way on, the side of the link's own line first; with
	// neither clear they back off. The view goes on looking down the route.
	VectorCopy (d, go);
	if ((link->type == NAV_WALK || link->type == NAV_DUCK) && grounded && dist > 4)
	{
		vec3_t		dir, side, p, q;
		float		across;
		trace_t		tr;
		qboolean	ducked = (client->ps.pmove.pm_flags & PMF_DUCKED) != 0;

		VectorSet (dir, d[0], d[1], 0);
		VectorNormalize (dir);
		VectorMA (origin, dist < 6 ? dist : 6, dir, p);
		tr = gi.trace (origin, tv(-16,-16,-6), tv(16,16, ducked ? 4 : 32), p, ent, MASK_PLAYERSOLID);
		if (tr.fraction < 1 && tr.ent && tr.ent != world && Nav_MoverSelf (tr.ent)
			&& !Nav_MoverAt (tr.ent))
			hold = true;		// a door on its way open: wait for it
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
			VectorSet (side, -dir[1], dir[0], 0);		// to the left of the way
			// which side of the line from the node to the target the player is on
			across = (target[0]-at[0]) * (origin[1]-at[1]) - (target[1]-at[1]) * (origin[0]-at[0]);
			if (across > 0)
				VectorInverse (side);
			VectorScale (dir, -1, go);
			for (i=0 ; i<2 ; i++)
			{
				VectorMA (origin, 12, side, p);
				VectorMA (p, 6, dir, q);
				if (Nav_Straight (ent, origin, p, ducked, MASK_PLAYERSOLID)
					&& Nav_Straight (ent, p, q, ducked, MASK_PLAYERSOLID))
				{
					VectorCopy (side, go);
					break;
				}
				VectorInverse (side);
			}
		}
	}

	// The view looks down the route a few links on, so that it turns into a
	// corner before the feet do. A jump or a ladder is faced squarely.
	want_yaw = RAD2DEG_F(atan2 (d[1], d[0]));
	if (link->type == NAV_WALK || link->type == NAV_DUCK || link->type == NAV_SWIM)
	{
		look = link;
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

	// The feet go one of eight ways about the view, 45 degrees apart. So that
	// one of them is exactly the way to the target, the view settles on the
	// heading nearest the one wanted that is a whole number of 45s from it.
	// An opening no wider than the player is not entered any other way.
	a = RAD2DEG_F(atan2 (go[1], go[0]));
	if (dist > 1)
		want_yaw = a + 45 * floor (AngleDiff (want_yaw, a) / 45 + 0.5f);

	i = Teach_Turn (AngleDiff (want_yaw, yaw), teach_yaw_rate, rl_yaw_bins, RL_YAW_BINS, TEACH_YAW_ACCEL);
	act[RL_ACT_YAW] = i;
	yaw += rl_yaw_bins[i];
	act[RL_ACT_PITCH] = Teach_Turn (-pitch, teach_pitch_rate, rl_pitch_bins, RL_PITCH_BINS, TEACH_PITCH_ACCEL);

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

	if (link->type == NAV_JUMP && up == 2 && grounded && best_err > 8)
	{	// at the node but not yet square to the jump: turn first
		up = 1;
		hold = true;
	}
	if (link->type == NAV_CLIMB && fabs (AngleDiff (a, yaw)) > 30)
		up = 1;

	if (!hold && (dist > 2 || link->type != NAV_WALK))
	{
		act[RL_ACT_FORWARD] = bf + 1;
		act[RL_ACT_STRAFE] = bs + 1;
	}
	act[RL_ACT_UP] = up;

	if (gi.cvar ("rl_debug", "0", 0)->value)
		gi.dprintf ("%i: at %.1f %.1f %.1f node %i -> %i type %i togo %.2f -> %.2f goal %i move %i %i up %i hold %i err %.1f\n",
			rl_block->step, origin[0], origin[1], origin[2], teach_anchor, link->to, link->type,
			teach_togo[teach_anchor], teach_togo[link->to], teach_goal, bf, bs, up, hold, best_err);
	if (link->ent && gi.cvar ("rl_debug", "0", 0)->value)
		gi.dprintf ("   mover %i %s wants %i is %i self %i z %.0f\n", link->ent, g_edicts[link->ent].classname,
			link->state, Nav_MoverAt (&g_edicts[link->ent]), Nav_MoverSelf (&g_edicts[link->ent]),
			g_edicts[link->ent].s.origin[2]);

	// the guide: where the next node of the route is from the view as it is now
	guide[RL_GUIDE_VALID] = 1;
	guide[RL_GUIDE_YAW] = AngleDiff (RAD2DEG_F(atan2 (d[1], d[0])), client->v_angle[YAW]);
	guide[RL_GUIDE_PITCH] = -RAD2DEG_F(atan2 (d[2], sqrt (d[0]*d[0] + d[1]*d[1]))) - client->v_angle[PITCH];
	guide[RL_GUIDE_DIST] = dist;
}
