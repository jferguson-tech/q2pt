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
// g_rl_nav.c -- the map as a graph of places the player can stand, grown
// from where the player starts by trying moves with the engine's own Pmove.
//
// A ghost of the player is put at a node at rest and given a move: forward
// on one of sixteen headings, and where that is stopped or falls, the same
// with a jump and the same crouched; in water, forward while rising and
// sinking; at a ladder, up it. Each is run in steps of RL_STEP_MSEC, the
// length of every step the player is ever moved by, until the ghost stands
// or swims somewhere about a step's run away. That place becomes a node, or
// is joined to a node already there, and the move becomes a link.
//
// Doors and lifts (func_door, func_door_rotating, func_plat) are movers: each
// has the place it starts in, its home, and the far end of its travel. The
// graph is first grown with every mover at home. Then each group of movers
// that work together is put at its far end, the nodes about it are tried
// again and the flood goes on from whatever that opens. A link found near a
// mover is marked with it and with the end it was found at; one found at
// both ends is not marked. A node on a mover has a twin where the mover's
// other end puts it, and the two are joined by a link that is the ride.
// Whether a marked link can be used is a matter of where the mover is, or
// can be got to be, when a route is planned.
//
// Monsters are not there for the ghost: they move, and are meant to be
// dead by the time the player passes. Barrels and the like are. Lava, slime
// and triggers that hurt end a move: nothing is linked through them.
//
// Building takes a few seconds, so the graph is kept in a file per map in
// the folder named by the cvar rl_nav.

#include "g_local.h"
#include "g_rl.h"
#include "g_rl_nav.h"

#define	NAV_FILE_MAGIC		0x3156414e		// "NAV1"
#define	NAV_FILE_VERSION	7

// What stops the ghost is what stops the player. While the graph is built
// the monsters are made not solid, so of the things with a box only those
// that stay where they are, such as barrels, are in its way.
#define	NAV_MASK		MASK_PLAYERSOLID

#define	NAV_MAX_NODES	60000
#define	NAV_MAX_LINKS	1200000
#define	NAV_HEADINGS	16
#define	NAV_REACH		24.0f		// a move has arrived when it has gone this far
#define	NAV_MERGE_XY	20.0f		// an arrival this near a node is at that node
#define	NAV_MERGE_Z		16.0f
#define	NAV_CELL		32.0f
#define	NAV_HASH		(1<<17)

#define	NAV_MAX_MOVERS	256

typedef struct
{
	edict_t		*ent;
	int			group;				// movers that move together have one number
	qboolean	self;				// works by itself when the player comes to it
	vec3_t		home, away;			// its origin at each end
	vec3_t		home_angles, away_angles;
	vec3_t		mins[2], maxs[2];	// its box at home and away, grown by the player's
} nav_mover_t;

static nav_mover_t	nav_movers[NAV_MAX_MOVERS];
static int			nav_num_movers;
static short		nav_mover_of[MAX_EDICTS];	// index+1 into nav_movers, by entity number
static int			nav_pass_group = -1;		// the group now at its far end, while building

nav_node_t	*nav_nodes;
nav_link_t	*nav_links;
int			nav_num_nodes, nav_num_links;

static int	*nav_cell_head;		// first node of each cell of the hash, or -1
static int	*nav_cell_next;		// the next node in the same cell
static int	*nav_back_first;	// links into each node: nav_back[nav_back_first[n]] ...
static int	*nav_back;
static float	nav_step = RL_STEP_MSEC * 0.001f;

// =================================================================== ghost

typedef struct
{
	pmove_state_t	s;
	vec3_t		origin;
	vec3_t		velocity;
	qboolean	ground;
	edict_t		*ground_ent;
	int			waterlevel;
	int			watertype;
} ghost_t;

static edict_t	*nav_pass;

#define	NAV_MAX_HURTS	128
static edict_t	*nav_hurts[NAV_MAX_HURTS];		// the map's trigger_hurt entities
static int		nav_num_hurts;

static trace_t Nav_Trace (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end)
{
	return gi.trace (start, mins, maxs, end, nav_pass, NAV_MASK);
}

static void Ghost_Place (ghost_t *g, short *origin, qboolean ducked)
{
	memset (g, 0, sizeof(*g));
	g->s.pm_type = PM_NORMAL;
	g->s.gravity = sv_gravity->value;
	g->s.origin[0] = origin[0];
	g->s.origin[1] = origin[1];
	g->s.origin[2] = origin[2];
	if (ducked)
		g->s.pm_flags = PMF_DUCKED;
	VectorSet (g->origin, origin[0]*0.125f, origin[1]*0.125f, origin[2]*0.125f);
}

/*
================
Ghost_Step

One command of RL_STEP_MSEC, through the engine's Pmove
================
*/
static void Ghost_Step (ghost_t *g, float yaw, int forward, int up)
{
	pmove_t	pm;

	memset (&pm, 0, sizeof(pm));
	pm.s = g->s;
	pm.trace = Nav_Trace;
	pm.pointcontents = gi.pointcontents;
	pm.cmd.msec = RL_STEP_MSEC;
	pm.cmd.angles[YAW] = ANGLE2SHORT(yaw);
	pm.cmd.forwardmove = forward;
	pm.cmd.upmove = up;

	gi.Pmove (&pm);

	g->s = pm.s;
	VectorSet (g->origin, pm.s.origin[0]*0.125f, pm.s.origin[1]*0.125f, pm.s.origin[2]*0.125f);
	VectorSet (g->velocity, pm.s.velocity[0]*0.125f, pm.s.velocity[1]*0.125f, pm.s.velocity[2]*0.125f);
	g->ground = pm.groundentity != NULL;
	g->ground_ent = pm.groundentity;
	g->waterlevel = pm.waterlevel;
	g->watertype = pm.watertype;
}

/*
================
Nav_Hurts

True where the player would be burned, dissolved or hurt by a trigger
================
*/
static qboolean Nav_Hurts (ghost_t *g)
{
	edict_t	*e;
	int		i;

	if (g->waterlevel && (g->watertype & (CONTENTS_LAVA|CONTENTS_SLIME)))
		return true;

	for (i=0 ; i<nav_num_hurts ; i++)
	{
		e = nav_hurts[i];
		if (g->origin[0] + 16 < e->absmin[0] || g->origin[0] - 16 > e->absmax[0]
			|| g->origin[1] + 16 < e->absmin[1] || g->origin[1] - 16 > e->absmax[1]
			|| g->origin[2] + 32 < e->absmin[2] || g->origin[2] - 24 > e->absmax[2])
			continue;
		return true;
	}
	return false;
}

// =================================================================== nodes

void Nav_NodeOrigin (int n, vec3_t out)
{
	VectorSet (out, nav_nodes[n].origin[0]*0.125f, nav_nodes[n].origin[1]*0.125f,
		nav_nodes[n].origin[2]*0.125f);
}

static int Nav_Cell (float x, float y, float z)
{
	unsigned	h;

	h = (unsigned)(int)floor (x / NAV_CELL) * 73856093u
		^ (unsigned)(int)floor (y / NAV_CELL) * 19349663u
		^ (unsigned)(int)floor (z / NAV_CELL) * 83492791u;
	return h & (NAV_HASH-1);
}

static void Nav_CellAdd (int n)
{
	vec3_t	p;
	int		c;

	Nav_NodeOrigin (n, p);
	c = Nav_Cell (p[0], p[1], p[2]);
	nav_cell_next[n] = nav_cell_head[c];
	nav_cell_head[c] = n;
}

/*
================
Nav_Near

The nearest node within xy and z of the point, or -1. on says what the point
stands on: 0 for the world, when nodes on movers are passed over, or a
mover's entity number, when only nodes on that mover at that end of its
travel (high) are looked at. -1 for any node at all.
================
*/
static int	nav_skip[8];		// nodes Nav_Near is to pass over
static int	nav_num_skip;

static int Nav_Near (vec3_t p, float xy, float z, int on, int high)
{
	int		ix, iy, iz, n, k, best;
	float	d, dz, bestd;
	vec3_t	q;
	int		reach = (int)ceil (xy / NAV_CELL);
	int		reachz = (int)ceil (z / NAV_CELL);

	best = -1;
	bestd = xy * xy;
	for (ix=-reach ; ix<=reach ; ix++)
	for (iy=-reach ; iy<=reach ; iy++)
	for (iz=-reachz ; iz<=reachz ; iz++)
	{
		for (n = nav_cell_head[Nav_Cell (p[0] + ix*NAV_CELL, p[1] + iy*NAV_CELL, p[2] + iz*NAV_CELL)] ;
			n != -1 ; n = nav_cell_next[n])
		{
			if (on == 0 && (nav_nodes[n].flags & NODE_MOVER))
				continue;
			if (on > 0 && (!(nav_nodes[n].flags & NODE_MOVER) || nav_nodes[n].ent != on
				|| ((nav_nodes[n].flags & NODE_HIGH) != 0) != (high != 0)))
				continue;
			for (k=0 ; k<nav_num_skip && nav_skip[k] != n ; k++)
				;
			if (k < nav_num_skip)
				continue;
			Nav_NodeOrigin (n, q);
			dz = fabs (q[2] - p[2]);
			if (dz > z)
				continue;
			d = (q[0]-p[0])*(q[0]-p[0]) + (q[1]-p[1])*(q[1]-p[1]);
			// two hash cells can be one bucket: a node is then met twice, which does no harm
			if (d < bestd || (d == bestd && best != -1 && n < best))
			{
				bestd = d;
				best = n;
			}
		}
	}
	return best;
}

/*
================
Nav_Straight

True when the player's box can be slid in a straight line from p to q. The
box is the player's full width, so that a gap the player would wedge in does
not pass, and has its bottom lifted by a stair's height, so that a stair does
not stop it. Crouched, it is as low as the crouched player.
================
*/
qboolean Nav_Straight (edict_t *pass, vec3_t p, vec3_t q, qboolean ducked, int mask)
{
	trace_t	tr;

	tr = gi.trace (p, tv(-16,-16,-6), tv(16,16, ducked ? 4 : 32), q, pass, mask);
	return tr.fraction >= 1 && !tr.startsolid;
}

/*
================
Nav_Join

The node an arrival at p belongs to: the nearest one within reach that the
player could go straight on to from p, or -1. Being near a node is not being
at it: the nearest may be past a corner the player would catch on.
================
*/
static int Nav_Join (vec3_t p, qboolean ducked, int on, int high)
{
	int		n;
	vec3_t	q;

	nav_num_skip = 0;
	while (nav_num_skip < 8)
	{
		n = Nav_Near (p, NAV_MERGE_XY, NAV_MERGE_Z, on, high);
		if (n == -1)
			break;
		Nav_NodeOrigin (n, q);
		if (Nav_Straight (nav_pass, p, q, ducked, NAV_MASK))
			break;
		nav_skip[nav_num_skip++] = n;
	}
	nav_num_skip = 0;
	return n;
}

static int Nav_AddNode (ghost_t *g, int flags, edict_t *mover)
{
	nav_node_t	*node;

	if (nav_num_nodes == NAV_MAX_NODES)
		return -1;
	node = &nav_nodes[nav_num_nodes];
	memset (node, 0, sizeof(*node));
	node->origin[0] = g->s.origin[0];
	node->origin[1] = g->s.origin[1];
	node->origin[2] = g->s.origin[2];
	node->flags = flags;
	if (mover)
	{
		node->flags |= NODE_MOVER;
		node->ent = mover - g_edicts;
	}
	Nav_CellAdd (nav_num_nodes);
	return nav_num_nodes++;
}

/*
================
Nav_Crosses

True when the straight line from a to b passes through the box
================
*/
static qboolean Nav_Crosses (vec3_t a, vec3_t b, vec3_t mins, vec3_t maxs)
{
	int		k;
	float	t0 = 0, t1 = 1, lo, hi, d, t;

	for (k=0 ; k<3 ; k++)
	{
		d = b[k] - a[k];
		if (fabs (d) < 0.001f)
		{
			if (a[k] < mins[k] || a[k] > maxs[k])
				return false;
			continue;
		}
		lo = (mins[k] - a[k]) / d;
		hi = (maxs[k] - a[k]) / d;
		if (lo > hi)
		{
			t = lo;
			lo = hi;
			hi = t;
		}
		if (lo > t0)
			t0 = lo;
		if (hi < t1)
			t1 = hi;
		if (t0 > t1)
			return false;
	}
	return true;
}

/*
================
Nav_AddLink

A link that passes where a mover is, at either end of its travel, is marked
with the mover and with the end it stood at when the link was found.
================
*/
static void Nav_AddLink (int from, int to, int type, float cost, int ent)
{
	nav_link_t	*l;
	nav_mover_t	*m;
	int			i;
	vec3_t		a, b;

	if (from == to || nav_num_links == NAV_MAX_LINKS)
		return;
	l = &nav_links[nav_num_links++];
	memset (l, 0, sizeof(*l));
	l->from = from;
	l->to = to;
	l->type = type;
	l->cost = cost;
	l->ent = ent;
	if (ent)
		return;

	Nav_NodeOrigin (from, a);
	Nav_NodeOrigin (to, b);
	for (i=0, m=nav_movers ; i<nav_num_movers ; i++, m++)
		if (Nav_Crosses (a, b, m->mins[0], m->maxs[0]) || Nav_Crosses (a, b, m->mins[1], m->maxs[1]))
		{
			l->ent = m->ent - g_edicts;
			l->state = (m->group == nav_pass_group) ? NAV_AWAY : NAV_HOME;
			return;
		}
}

// =================================================================== moves

/*
================
Nav_Arrive

The ghost has come to rest at the end of a move from a node. Joins it to the
node that is there, or makes one.
================
*/
static void Nav_Arrive (int from, ghost_t *g, int type, int steps, float landing)
{
	int			to, flags, on, high;
	float		cost, hurt;
	nav_mover_t	*m;

	// what it stands on, if that is a mover, and which end the mover is at
	on = high = 0;
	if (g->ground_ent && nav_mover_of[g->ground_ent - g_edicts])
	{
		m = &nav_movers[nav_mover_of[g->ground_ent - g_edicts] - 1];
		on = g->ground_ent - g_edicts;
		high = m->group == nav_pass_group;
	}

	to = Nav_Join (g->origin, (g->s.pm_flags & PMF_DUCKED) != 0, on, high);
	if (to == -1)
	{
		flags = 0;
		if (!g->ground && g->waterlevel >= 2)
			flags |= NODE_WATER;
		if (g->s.pm_flags & PMF_DUCKED)
			flags |= NODE_DUCK;
		if (on && high)
			flags |= NODE_HIGH;
		to = Nav_AddNode (g, flags, on ? g->ground_ent : NULL);
		if (to == -1)
			return;
	}

	cost = steps * nav_step;
	// A landing harder than this costs health, as P_FallingDamage reckons it.
	// Ten seconds are charged for each point, so that such a drop is taken
	// only where there is no other way; one that would cost half the
	// player's health is not linked at all.
	hurt = landing * landing * 0.0001f;
	if (hurt > 30)
	{
		hurt = (hurt - 30) * 0.5f;
		if (hurt >= 50)
			return;
		cost += hurt * 10;
	}
	Nav_AddLink (from, to, type, cost, 0);
}

/*
================
Nav_Run

Runs one move by the ghost from a starting place. Returns false when it went
nowhere or came to grief. On success g is where it ended, *steps how long it
took and *landing the speed of the hardest fall on the way.
================
*/
static qboolean Nav_Run (ghost_t *g, short *start, qboolean ducked, float yaw, int type, int swim_up,
	int *steps, float *landing)
{
	int			step, up, grounded;
	float		dx, dy, moved, last, startz;

	Ghost_Place (g, start, ducked);
	startz = g->origin[2];
	*landing = 0;
	last = 0;
	grounded = 0;

	for (step=1 ; step<=60 ; step++)
	{
		up = 0;
		if (type == NAV_JUMP && step == 1)
			up = 400;
		else if (type == NAV_DUCK)
			up = -400;
		else if (type == NAV_CLIMB)
			up = 400;
		else if (type == NAV_SWIM)
			up = swim_up;

		Ghost_Step (g, yaw, 400, up);

		if (Nav_Hurts (g))
			return false;
		if (!g->ground && g->velocity[2] < -*landing)
			*landing = -g->velocity[2];

		dx = g->origin[0] - start[0]*0.125f;
		dy = g->origin[1] - start[1]*0.125f;
		moved = sqrt (dx*dx + dy*dy);
		if (type == NAV_SWIM || type == NAV_CLIMB)
			moved += fabs (g->origin[2] - startz);

		if (g->ground || g->waterlevel >= 2)
		{
			if (type == NAV_CLIMB && !g->ground)
			{	// still on the ladder: go on while it rises
				if (g->velocity[2] <= 0 && step > 2)
					return false;
				continue;
			}
			if (moved >= NAV_REACH)
			{
				*steps = step;
				return true;
			}
			// not there yet: give up when the last step gained nothing
			if (moved - last < 2 && step > 1)
				return false;
			if (++grounded > 4)
				return false;
			last = moved;
		}
		else if (type != NAV_SWIM && type != NAV_CLIMB && step > 40)
			return false;		// a fall with no end
	}
	return false;
}

/*
================
Nav_Move

Tries one move from a node and links where it ends. Returns false when the
move went nowhere or came to grief: the caller may try it another way.
*fell is set when the move ended more than a stair below where it began.

A move is only kept if it also works from a little way off: the player
following the graph is never exactly on a node, and a move that clears a
ledge or threads a gap from one spot only is no use to it. So the move is
run again from a few units behind the node and from either side, wherever
the player's box can be slid there, and each must end near where the first
did.
================
*/
static qboolean Nav_Move (int from, float yaw, int type, int swim_up, qboolean *fell)
{
	static const float	offsets[3][2] = {{-6, 0}, {0, 4}, {0, -4}};	// back, left, right
	ghost_t		g, other;
	nav_node_t	*node = &nav_nodes[from];
	qboolean	ducked = type == NAV_DUCK || (node->flags & NODE_DUCK);
	int			steps, other_steps, i;
	float		landing, other_landing, dx, dy;
	vec3_t		origin, angles, forward, right, p;
	short		start[3];
	trace_t		tr;

	if (fell)
		*fell = false;
	if (!Nav_Run (&g, node->origin, ducked, yaw, type, swim_up, &steps, &landing))
		return false;

	if (type != NAV_SWIM && type != NAV_CLIMB)
	{
		Nav_NodeOrigin (from, origin);
		VectorSet (angles, 0, yaw, 0);
		AngleVectors (angles, forward, right, NULL);
		for (i=0 ; i<3 ; i++)
		{
			VectorMA (origin, offsets[i][0], forward, p);
			VectorMA (p, -offsets[i][1], right, p);
			tr = gi.trace (origin, tv(-16,-16,-24), tv(16,16, ducked ? 4 : 32), p, nav_pass, NAV_MASK);
			if (tr.fraction < 1 || tr.startsolid)
				continue;		// the player cannot be there, so need not start there
			start[0] = (short)(p[0] * 8);
			start[1] = (short)(p[1] * 8);
			start[2] = node->origin[2];
			if (!Nav_Run (&other, start, ducked, yaw, type, swim_up, &other_steps, &other_landing))
				return false;
			dx = other.origin[0] - g.origin[0];
			dy = other.origin[1] - g.origin[1];
			if (dx*dx + dy*dy > 24*24 || fabs (other.origin[2] - g.origin[2]) > 18)
				return false;
			if (other_landing > landing)
				landing = other_landing;
		}
	}

	if (fell && g.origin[2] < node->origin[2]*0.125f - 18)
		*fell = true;
	Nav_Arrive (from, &g, type, steps, landing);
	return true;
}

/*
================
Nav_Expand

Tries every move from a node, with the movers where they stand
================
*/
static void Nav_Expand (int n)
{
	int			h, up;
	float		yaw;
	qboolean	ok, fell;
	vec3_t		start, end, forward, angles;
	trace_t		tr;

	for (h=0 ; h<NAV_HEADINGS ; h++)
	{
		if (nav_num_links > NAV_MAX_LINKS - 8)
			return;
		yaw = h * (360.0f / NAV_HEADINGS);

		if (nav_nodes[n].flags & NODE_WATER)
		{
			for (up=-400 ; up<=400 ; up+=400)
				Nav_Move (n, yaw, NAV_SWIM, up, NULL);
			// and out of the water over its edge
			Nav_Move (n, yaw, NAV_JUMP, 0, NULL);
			continue;
		}

		ok = Nav_Move (n, yaw, (nav_nodes[n].flags & NODE_DUCK) ? NAV_DUCK : NAV_WALK, 0, &fell);
		if (!ok || fell)
		{	// stopped, or over an edge: a jump may clear it, a crouch may fit under it
			Nav_Move (n, yaw, NAV_JUMP, 0, NULL);
			if (!ok && !(nav_nodes[n].flags & NODE_DUCK))
				Nav_Move (n, yaw, NAV_DUCK, 0, NULL);
		}

		// a ladder ahead
		Nav_NodeOrigin (n, start);
		VectorSet (angles, 0, yaw, 0);
		AngleVectors (angles, forward, NULL, NULL);
		VectorMA (start, 24, forward, end);
		tr = gi.trace (start, tv(-16,-16,-24), tv(16,16,32), end, nav_pass, NAV_MASK|CONTENTS_LADDER);
		if (tr.fraction < 1 && (tr.contents & CONTENTS_LADDER))
			Nav_Move (n, yaw, NAV_CLIMB, 0, NULL);
	}
}

// ================================================================== movers

/*
================
Nav_PlaceMover

Puts a mover at home or at the far end of its travel
================
*/
static void Nav_PlaceMover (nav_mover_t *m, qboolean away)
{
	float	*origin = away ? m->away : m->home;
	float	*angles = away ? m->away_angles : m->home_angles;

	VectorCopy (origin, m->ent->s.origin);
	VectorCopy (angles, m->ent->s.angles);
	gi.linkentity (m->ent);
}

static void Nav_PlaceGroup (int group, qboolean away)
{
	int		i;

	for (i=0 ; i<nav_num_movers ; i++)
		if (nav_movers[i].group == group)
			Nav_PlaceMover (&nav_movers[i], away);
}

/*
================
Nav_FindMovers

Lists the map's doors and lifts with both ends of their travel. Called at
the start of every episode, before anything has moved. With boxes set, each
is put at its far end and back to measure the room it takes there, which
only the building of the graph needs.
================
*/
static void Nav_FindMovers (qboolean boxes)
{
	edict_t		*e, *o;
	nav_mover_t	*m;
	int			i, j, k, groups;
	qboolean	door, rotating, plat;

	nav_num_movers = 0;
	memset (nav_mover_of, 0, sizeof(nav_mover_of));
	groups = 0;

	for (i=game.maxclients+1, e=g_edicts+i ; i<globals.num_edicts ; i++, e++)
	{
		if (!e->inuse || !e->classname || nav_num_movers == NAV_MAX_MOVERS)
			continue;
		door = !strcmp (e->classname, "func_door");
		rotating = !strcmp (e->classname, "func_door_rotating");
		plat = !strcmp (e->classname, "func_plat");
		if (!door && !rotating && !plat)
			continue;

		m = &nav_movers[nav_num_movers];
		memset (m, 0, sizeof(*m));
		m->ent = e;
		VectorCopy (e->s.origin, m->home);
		VectorCopy (e->s.angles, m->home_angles);
		VectorCopy (m->home, m->away);
		VectorCopy (m->home_angles, m->away_angles);
		if (rotating)
		{
			if (VectorCompare (m->home_angles, e->moveinfo.start_angles))
				VectorCopy (e->moveinfo.end_angles, m->away_angles);
			else
				VectorCopy (e->moveinfo.start_angles, m->away_angles);
		}
		else if (VectorCompare (m->home, e->moveinfo.start_origin))
			VectorCopy (e->moveinfo.end_origin, m->away);
		else
			VectorCopy (e->moveinfo.start_origin, m->away);

		// A door with no name and no health opens to whoever walks up to it.
		// A lift with no name rises under whoever steps on it and comes back.
		m->self = !e->targetname && (plat || !e->health);

		// in a group with any earlier mover of the same team or the same name
		m->group = -1;
		for (j=0 ; j<nav_num_movers && m->group == -1 ; j++)
		{
			o = nav_movers[j].ent;
			if ((e->teammaster && e->teammaster == o->teammaster)
				|| (e->targetname && o->targetname && !strcmp (e->targetname, o->targetname)))
				m->group = nav_movers[j].group;
		}
		if (m->group == -1)
			m->group = groups++;

		nav_mover_of[i] = nav_num_movers + 1;
		nav_num_movers++;
	}

	// a group works by itself only if every mover in it does
	for (i=0 ; i<nav_num_movers ; i++)
		if (!nav_movers[i].self)
			for (j=0 ; j<nav_num_movers ; j++)
				if (nav_movers[j].group == nav_movers[i].group)
					nav_movers[j].self = false;

	if (!boxes)
		return;

	for (i=0, m=nav_movers ; i<nav_num_movers ; i++, m++)
		for (k=1 ; k>=0 ; k--)
		{
			Nav_PlaceMover (m, k);
			// where the player's origin cannot be for the mover: its box grown
			// by the player's, and above it as far as one standing on it
			VectorSet (m->mins[k], m->ent->absmin[0] - 16, m->ent->absmin[1] - 16, m->ent->absmin[2] - 32);
			VectorSet (m->maxs[k], m->ent->absmax[0] + 16, m->ent->absmax[1] + 16, m->ent->absmax[2] + 28);
		}
}

// =================================================================== build

static byte	*nav_expanded;		// nodes whose moves have been tried with every mover at home

/*
================
Nav_Flood

Expands every node from first on, and those that makes, until none is left
================
*/
static void Nav_Flood (int first)
{
	int		n;

	for (n=first ; n<nav_num_nodes && nav_num_links < NAV_MAX_LINKS - 8 ; n++)
	{
		if (nav_expanded[n])
			continue;
		nav_expanded[n] = 1;
		Nav_Expand (n);
	}
}

/*
================
Nav_Twins

Gives every node that stands on a mover of the group its twin at the mover's
other end, joined to it by the ride each way. away says which end the group
is at now; the nodes at the other end are the ones looked at.
================
*/
static void Nav_Twins (int group, qboolean away)
{
	int			n, i, twin, count;
	nav_node_t	*node;
	nav_mover_t	*m;
	nav_link_t	*l;
	ghost_t		g;
	vec3_t		move, p;
	float		time;

	count = nav_num_nodes;
	for (n=0 ; n<count ; n++)
	{
		node = &nav_nodes[n];
		if (!(node->flags & NODE_MOVER) || !nav_mover_of[node->ent])
			continue;
		m = &nav_movers[nav_mover_of[node->ent] - 1];
		if (m->group != group || ((node->flags & NODE_HIGH) != 0) == away)
			continue;
		// one that has its ride already has its twin
		for (i=0, l=nav_links ; i<nav_num_links ; i++, l++)
			if (l->type == NAV_RIDE && l->from == n)
				break;
		if (i < nav_num_links)
			continue;

		if (away)
			VectorSubtract (m->away, m->home, move);
		else
			VectorSubtract (m->home, m->away, move);
		if (VectorLength (move) < 8)
			continue;		// a door that turns: nothing rides it

		Nav_NodeOrigin (n, p);
		VectorAdd (p, move, p);
		twin = Nav_Near (p, 4, 4, node->ent, away);
		if (twin == -1)
		{
			Ghost_Place (&g, node->origin, (node->flags & NODE_DUCK) != 0);
			for (i=0 ; i<3 ; i++)
				g.s.origin[i] += (short)(move[i] * 8);
			twin = Nav_AddNode (&g, (node->flags & NODE_DUCK) | (away ? NODE_HIGH : 0), m->ent);
			if (twin == -1)
				return;
		}
		// the ride: its length at the mover's speed, and a wait before it starts
		time = VectorLength (move) / (m->ent->moveinfo.speed > 0 ? m->ent->moveinfo.speed : 100) + 1;
		Nav_AddLink (n, twin, NAV_RIDE, time, node->ent);
		Nav_AddLink (twin, n, NAV_RIDE, time, node->ent);
	}
}

static void Nav_Build (void)
{
	edict_t		*e, *player = &g_edicts[1];
	ghost_t		g;
	int			i, n, round, group, groups, before, count;
	nav_mover_t	*m;
	vec3_t		p;

	nav_pass = player;
	nav_num_nodes = nav_num_links = 0;
	nav_expanded = gi.TagMalloc (NAV_MAX_NODES, TAG_LEVEL);

	nav_num_hurts = 0;
	for (i=game.maxclients+1, e=g_edicts+i ; i<globals.num_edicts ; i++, e++)
		if (e->inuse && e->classname && !strcmp (e->classname, "trigger_hurt")
			&& nav_num_hurts < NAV_MAX_HURTS)
			nav_hurts[nav_num_hurts++] = e;

	// monsters out of the way
	for (i=game.maxclients+1, e=g_edicts+i ; i<globals.num_edicts ; i++, e++)
		if (e->inuse && (e->svflags & SVF_MONSTER) && e->solid == SOLID_BBOX)
		{
			e->solid = SOLID_NOT;
			gi.linkentity (e);
		}

	Nav_FindMovers (true);
	groups = 0;
	for (i=0 ; i<nav_num_movers ; i++)
		if (nav_movers[i].group >= groups)
			groups = nav_movers[i].group + 1;

	// from where the player stands, let fall to the floor
	Ghost_Place (&g, player->client->ps.pmove.origin, false);
	for (i=0 ; i<20 && !g.ground ; i++)
		Ghost_Step (&g, 0, 0, 0);
	Nav_AddNode (&g, 0, (g.ground_ent && nav_mover_of[g.ground_ent - g_edicts]) ? g.ground_ent : NULL);

	// Round after round, until one adds no node: everything at home, then
	// each group of movers in turn at its far end.
	for (round=0 ; round<6 ; round++)
	{
		before = nav_num_nodes;
		nav_pass_group = -1;
		Nav_Flood (0);

		for (group=0 ; group<groups ; group++)
		{
			Nav_PlaceGroup (group, true);
			nav_pass_group = group;
			count = nav_num_nodes;

			Nav_Twins (group, true);

			// the nodes about the group's movers, tried again as things now stand
			for (n=0 ; n<count ; n++)
			{
				Nav_NodeOrigin (n, p);
				for (i=0, m=nav_movers ; i<nav_num_movers ; i++, m++)
				{
					if (m->group != group)
						continue;
					if ((p[0] > m->mins[0][0] - 64 && p[0] < m->maxs[0][0] + 64
							&& p[1] > m->mins[0][1] - 64 && p[1] < m->maxs[0][1] + 64
							&& p[2] > m->mins[0][2] - 64 && p[2] < m->maxs[0][2] + 64)
						|| (p[0] > m->mins[1][0] - 64 && p[0] < m->maxs[1][0] + 64
							&& p[1] > m->mins[1][1] - 64 && p[1] < m->maxs[1][1] + 64
							&& p[2] > m->mins[1][2] - 64 && p[2] < m->maxs[1][2] + 64))
					{
						// one standing on the mover at home is not there now
						if (!((nav_nodes[n].flags & NODE_MOVER) && !(nav_nodes[n].flags & NODE_HIGH)
							&& nav_mover_of[nav_nodes[n].ent]
							&& nav_movers[nav_mover_of[nav_nodes[n].ent] - 1].group == group))
							Nav_Expand (n);
						break;
					}
				}
			}
			// and on from the nodes this has made, which are not tried again at home
			for (n=count ; n<nav_num_nodes && nav_num_links < NAV_MAX_LINKS - 8 ; n++)
			{
				nav_expanded[n] = 1;
				Nav_Expand (n);
			}

			nav_pass_group = -1;
			Nav_PlaceGroup (group, false);
			// the twins at home of nodes found on the movers at their far end
			// are expanded with everything at home in the next round
			Nav_Twins (group, false);
		}

		if (nav_num_nodes == before && round)
			break;
	}

	// The map is not as it was after this: the caller starts it over.
	for (i=game.maxclients+1, e=g_edicts+i ; i<globals.num_edicts ; i++, e++)
		if (e->inuse && e->solid == SOLID_NOT && (e->svflags & SVF_MONSTER))
		{
			e->solid = SOLID_BBOX;
			gi.linkentity (e);
		}
}

/*
================
Nav_Index

Puts the links in the order of the nodes they leave, and lists for every
node the links that come into it.
================
*/
static int Nav_LinkOrder (const void *a, const void *b)
{
	const nav_link_t	*x = a, *y = b;

	if (x->from != y->from)
		return x->from - y->from;
	if (x->to != y->to)
		return x->to - y->to;
	if (x->type != y->type)
		return x->type - y->type;
	if (x->ent != y->ent)
		return x->ent - y->ent;
	return x->state - y->state;
}

static void Nav_Index (void)
{
	int		i, n;

	nav_link_t	*l, *last;

	qsort (nav_links, nav_num_links, sizeof(nav_link_t), Nav_LinkOrder);

	// One link of a kind between two nodes. Where the same move was found
	// with a mover at home and again with it away, or was found once clear
	// of any mover, it does not depend on the mover.
	last = NULL;
	for (i=0, n=0, l=nav_links ; i<nav_num_links ; i++, l++)
	{
		if (last && last->from == l->from && last->to == l->to && last->type == l->type)
		{
			if (l->type != NAV_RIDE && (!last->ent || last->ent != l->ent || last->state != l->state))
			{
				last->ent = 0;
				last->state = 0;
			}
			if (l->cost < last->cost)
				last->cost = l->cost;
			continue;
		}
		last = &nav_links[n++];
		*last = *l;
	}
	nav_num_links = n;

	for (n=0 ; n<nav_num_nodes ; n++)
		nav_nodes[n].num_links = 0;
	for (i=0 ; i<nav_num_links ; i++)
		nav_nodes[nav_links[i].from].num_links++;
	for (n=0, i=0 ; n<nav_num_nodes ; n++)
	{
		nav_nodes[n].first_link = i;
		i += nav_nodes[n].num_links;
	}

	nav_back_first = gi.TagMalloc ((nav_num_nodes + 1) * sizeof(int), TAG_LEVEL);
	nav_back = gi.TagMalloc ((nav_num_links + 1) * sizeof(int), TAG_LEVEL);
	for (i=0 ; i<nav_num_links ; i++)
		nav_back_first[nav_links[i].to + 1]++;
	for (n=0 ; n<nav_num_nodes ; n++)
		nav_back_first[n+1] += nav_back_first[n];
	{
		int	*fill = gi.TagMalloc ((nav_num_nodes + 1) * sizeof(int), TAG_LEVEL);

		for (i=0 ; i<nav_num_links ; i++)
			nav_back[nav_back_first[nav_links[i].to] + fill[nav_links[i].to]++] = i;
		gi.TagFree (fill);
	}

	for (i=0 ; i<NAV_HASH ; i++)
		nav_cell_head[i] = -1;
	for (n=0 ; n<nav_num_nodes ; n++)
		Nav_CellAdd (n);
}

// ==================================================================== file

static char *Nav_FileName (void)
{
	static char	name[MAX_OSPATH];
	char		*dir;

	dir = gi.cvar ("rl_nav", "", 0)->string;
	if (!dir[0])
		return NULL;
	Com_sprintf (name, sizeof(name), "%s/%s.nav", dir, level.mapname);
	return name;
}

static qboolean Nav_Read (void)
{
	FILE	*f;
	int		head[4];
	char	*name = Nav_FileName ();

	if (!name)
		return false;
	f = fopen (name, "rb");
	if (!f)
		return false;
	if (fread (head, sizeof(head), 1, f) != 1 || head[0] != NAV_FILE_MAGIC || head[1] != NAV_FILE_VERSION
		|| head[2] < 1 || head[2] > NAV_MAX_NODES || head[3] < 0 || head[3] > NAV_MAX_LINKS
		|| fread (nav_nodes, sizeof(nav_node_t), head[2], f) != (size_t)head[2]
		|| fread (nav_links, sizeof(nav_link_t), head[3], f) != (size_t)head[3])
	{
		fclose (f);
		return false;
	}
	fclose (f);
	nav_num_nodes = head[2];
	nav_num_links = head[3];
	return true;
}

/*
================
Nav_Write

Written under another name and then renamed, so that of several servers that
build the same map at once none reads half of another's file.
================
*/
static void Nav_Write (void)
{
	FILE	*f;
	int		head[4];
	char	*name = Nav_FileName ();
	char	temp[MAX_OSPATH];

	if (!name)
		return;
	Com_sprintf (temp, sizeof(temp), "%s.%i.tmp", name, rand ());
	f = fopen (temp, "wb");
	if (!f)
	{
		gi.dprintf ("cannot write %s\n", temp);
		return;
	}
	head[0] = NAV_FILE_MAGIC;
	head[1] = NAV_FILE_VERSION;
	head[2] = nav_num_nodes;
	head[3] = nav_num_links;
	fwrite (head, sizeof(head), 1, f);
	fwrite (nav_nodes, sizeof(nav_node_t), nav_num_nodes, f);
	fwrite (nav_links, sizeof(nav_link_t), nav_num_links, f);
	fclose (f);
	rename (temp, name);
}

qboolean Nav_Load (void)
{
	qboolean	built = false;

	nav_nodes = gi.TagMalloc (NAV_MAX_NODES * sizeof(nav_node_t), TAG_LEVEL);
	nav_links = gi.TagMalloc (NAV_MAX_LINKS * sizeof(nav_link_t), TAG_LEVEL);
	nav_cell_head = gi.TagMalloc (NAV_HASH * sizeof(int), TAG_LEVEL);
	nav_cell_next = gi.TagMalloc (NAV_MAX_NODES * sizeof(int), TAG_LEVEL);
	memset (nav_cell_head, -1, NAV_HASH * sizeof(int));

	if (!Nav_Read ())
	{
		Nav_Build ();
		built = true;
	}
	else
		Nav_FindMovers (false);
	Nav_Index ();
	if (built)
		Nav_Write ();
	return built;
}

// ================================================================== routes

/*
================
Nav_MoverAt

Which end of its travel a mover is at: NAV_HOME, NAV_AWAY, or 0 between them
================
*/
int Nav_MoverAt (edict_t *e)
{
	nav_mover_t	*m;
	vec3_t		d;

	if (!nav_mover_of[e - g_edicts])
		return 0;
	m = &nav_movers[nav_mover_of[e - g_edicts] - 1];
	VectorSubtract (e->s.origin, m->home, d);
	if (VectorLength (d) < 1)
	{
		VectorSubtract (e->s.angles, m->home_angles, d);
		if (VectorLength (d) < 1)
			return NAV_HOME;
	}
	VectorSubtract (e->s.origin, m->away, d);
	if (VectorLength (d) < 1)
	{
		VectorSubtract (e->s.angles, m->away_angles, d);
		if (VectorLength (d) < 1)
			return NAV_AWAY;
	}
	return 0;
}

qboolean Nav_MoverSelf (edict_t *e)
{
	return nav_mover_of[e - g_edicts] && nav_movers[nav_mover_of[e - g_edicts] - 1].self;
}

qboolean Nav_LinkOpen (nav_link_t *l)
{
	edict_t		*e;
	qboolean	plat;

	if (!l->ent)
		return true;
	e = &g_edicts[l->ent];
	if (!e->inuse || !nav_mover_of[l->ent])
		return true;		// a door that is no longer there

	plat = !strcmp (e->classname, "func_plat");

	// Only a lift that works by itself is ridden: how to send any other is
	// not known here.
	if (l->type == NAV_RIDE)
		return plat && Nav_MoverSelf (e);

	if (Nav_MoverAt (e) == l->state)
		return true;
	if (!Nav_MoverSelf (e))
		return false;
	// A door that works by itself opens as the player comes to it, and a
	// lift comes back down by itself. But a lift goes up only under someone:
	// a link that wants it up is there for the one who rode it, and for
	// nobody else until it is up.
	if (plat && l->state == NAV_AWAY)
		return (nav_nodes[l->from].flags & NODE_MOVER) && nav_nodes[l->from].ent == l->ent;
	return true;
}

/*
================
Nav_Nearest

The nearest node the player could go straight to (Nav_Straight), so that a
node past a wall, a grating or a corner is not taken. Nearer ones that fail
are passed over. When none passes, the nearest of all is given.
================
*/
int Nav_Nearest (edict_t *ent, vec3_t origin)
{
	int		n, first, pass, on;
	vec3_t	q;

	if (!nav_num_nodes)
		return -1;

	// On a mover, the node on that mover that is nearest: the one below or
	// its twin above, whichever the ride is nearer.
	if (ent->groundentity && nav_mover_of[ent->groundentity - g_edicts])
	{
		on = ent->groundentity - g_edicts;
		n = Nav_Near (origin, 40, 30, on, 0);
		if (n == -1)
			n = Nav_Near (origin, 40, 30, on, 1);
		if (n != -1)
			return n;
	}

	first = -1;
	nav_num_skip = 0;
	for (pass=0 ; pass<2 ; pass++)
	{
		while (nav_num_skip < 8)
		{
			// not on a mover, so not at a node that is: a door's sill may be
			// a hand's breadth above the floor before it and still not be
			// stepped onto from there
			on = ent->groundentity || ent->waterlevel >= 2 ? 0 : -1;
			n = pass ? Nav_Near (origin, 96, 64, on, 0) : Nav_Near (origin, 40, 30, on, 0);
			if (n == -1)
				break;
			if (first == -1)
				first = n;
			Nav_NodeOrigin (n, q);
			if (Nav_Straight (ent, origin, q, true, MASK_SOLID))
			{
				nav_num_skip = 0;
				return n;
			}
			nav_skip[nav_num_skip++] = n;
		}
	}
	nav_num_skip = 0;
	return first;
}

/*
================
Nav_Costs

Dijkstra's algorithm on a binary heap: backwards from a goal, over the links
into each node, or forwards from a start, over the links out of it
================
*/
static void Nav_Costs (int goal, float *togo, qboolean forward);

void Nav_CostsTo (int goal, float *togo)
{
	Nav_Costs (goal, togo, false);
}

void Nav_CostsFrom (int start, float *cost)
{
	Nav_Costs (start, cost, true);
}

static void Nav_Costs (int goal, float *togo, qboolean forward)
{
	static int	*heap, *where;
	static int	heap_for;
	int			count, n, i, k, parent, child, last, m, first, end;
	nav_link_t	*l;
	float		c;

	if (heap_for != nav_num_nodes || !heap)
	{
		heap = gi.TagMalloc ((nav_num_nodes + 1) * sizeof(int), TAG_LEVEL);
		where = gi.TagMalloc ((nav_num_nodes + 1) * sizeof(int), TAG_LEVEL);
		heap_for = nav_num_nodes;
	}

	for (n=0 ; n<nav_num_nodes ; n++)
	{
		togo[n] = NAV_FAR;
		where[n] = -1;
	}
	if (goal < 0 || goal >= nav_num_nodes)
		return;

	togo[goal] = 0;
	heap[0] = goal;
	where[goal] = 0;
	count = 1;

	while (count)
	{
		n = heap[0];
		where[n] = -2;		// settled
		last = heap[--count];
		if (count)
		{	// sift the last one down from the top
			k = 0;
			while (1)
			{
				child = 2*k + 1;
				if (child >= count)
					break;
				if (child + 1 < count && togo[heap[child+1]] < togo[heap[child]])
					child++;
				if (togo[last] <= togo[heap[child]])
					break;
				heap[k] = heap[child];
				where[heap[k]] = k;
				k = child;
			}
			heap[k] = last;
			where[last] = k;
		}

		first = forward ? nav_nodes[n].first_link : nav_back_first[n];
		end = forward ? first + nav_nodes[n].num_links : nav_back_first[n+1];
		for (i=first ; i<end ; i++)
		{
			l = forward ? &nav_links[i] : &nav_links[nav_back[i]];
			m = forward ? l->to : l->from;
			if (where[m] == -2 || !Nav_LinkOpen (l))
				continue;
			c = togo[n] + l->cost;
			if (c >= togo[m])
				continue;
			togo[m] = c;
			k = where[m];
			if (k < 0)
				k = count++;
			while (k > 0)
			{	// sift up
				parent = (k - 1) / 2;
				if (togo[heap[parent]] <= c)
					break;
				heap[k] = heap[parent];
				where[heap[k]] = k;
				k = parent;
			}
			heap[k] = m;
			where[m] = k;
		}
	}
}
