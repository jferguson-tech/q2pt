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
// g_rl_nav.h -- the map as a graph of places the player can stand and the
// moves that take it from one to the next. See g_rl_nav.c.

#ifndef G_RL_NAV_H
#define G_RL_NAV_H

// how a link is travelled
#define	NAV_WALK	0
#define	NAV_JUMP	1		// jump from the node, then hold forward to the landing
#define	NAV_DUCK	2		// crouched all the way
#define	NAV_SWIM	3
#define	NAV_CLIMB	4		// up a ladder
#define	NAV_RIDE	5		// stand on the lift until it has moved

// the two ends of a door's or a lift's travel
#define	NAV_HOME	1		// where it is when the map starts
#define	NAV_AWAY	2

// what a node is
#define	NODE_WATER	1		// the player swims here
#define	NODE_DUCK	2		// there is no room to stand up
#define	NODE_MOVER	4		// on a lift: ent is the lift
#define	NODE_HIGH	8		// with NODE_MOVER: on it when it is at the far end of its travel

typedef struct
{
	short	origin[3];		// the player's origin there, in eighths of a unit as Pmove keeps it
	byte	flags;
	byte	pad;
	short	ent;			// the lift a NODE_MOVER node stands on
	short	pad2;
	int		first_link;		// its links out are links[first_link] ... for num_links
	int		num_links;
} nav_node_t;

typedef struct
{
	int		from, to;
	float	cost;			// seconds, plus a charge for a hard landing
	byte	type;			// NAV_
	byte	state;			// NAV_HOME or NAV_AWAY: where ent must be for the link to be there
	short	ent;			// a door or lift the link depends on, or the one ridden; 0 for none
	byte	heading;		// the way the move was made, in sixteenths of a turn from east
	byte	steps;			// and how many steps it took
	short	pad;
} nav_link_t;

extern	nav_node_t	*nav_nodes;
extern	nav_link_t	*nav_links;
extern	int			nav_num_nodes, nav_num_links;

// Loads the graph of the map being played, building it first if it was
// never built. Returns true when it had to build: building moves doors and
// lifts about and puts them back, so the episode is started again after it.
qboolean Nav_Load (void);

void Nav_NodeOrigin (int n, vec3_t out);

// the node the player at origin is at, or -1 when none is near
int Nav_Nearest (edict_t *ent, vec3_t origin);

// Fills togo[] with the cost of the cheapest way from every node to goal, as
// the doors and lifts stand now. NAV_FAR where there is no way.
#define	NAV_FAR		1e30f
void Nav_CostsTo (int goal, float *togo);
// to the nearest of several goals
void Nav_CostsToAny (int *goals, int num, float *togo);
// the same from a node to every other
void Nav_CostsFrom (int start, float *cost);

// See g_rl_nav.c: links that hang on movers that could yet be sent count as
// there while nav_hopeful is set, but for the movers marked in nav_hopeless.
extern	qboolean	nav_hopeful;
extern	byte		nav_hopeless[MAX_EDICTS];

// links found not to be there, by their number: see g_rl_nav.c
extern	byte		*nav_link_bad;

// the movers that move together with e, itself among them
int Nav_MoverGroup (edict_t *e, edict_t **list, int max);

// the nearest node within xy and z of a point that is not on a mover, or -1
int Nav_NodeNear (vec3_t p, float xy, float z);

// True when the player's box, its bottom lifted by a stair's height, can be
// slid in a straight line from p to q past everything in mask but pass.
qboolean Nav_Straight (edict_t *pass, vec3_t p, vec3_t q, qboolean ducked, int mask);

// which end of its travel a door or lift is at, or 0 between them; and
// whether it is one that works by itself when the player comes to it
int Nav_MoverAt (edict_t *e);
qboolean Nav_MoverSelf (edict_t *e);

// True when the player, as it stands and moves now, could take one step
// along dir (or none, for NULL) and then stop, and be standing on a floor
// out of harm's way.
qboolean Nav_StepSafe (edict_t *ent, vec3_t dir, qboolean ducked);
extern	float	nav_step_drop;		// how far down such a step may end; a stair unless changed
extern	int		nav_step_node;		// the node the last one that passed ended at
extern	float	*nav_step_togo;		// if set, costs by node: no trial may end where there is no way on

// the same for a player in the air, the feet going along dir until it lands
qboolean Nav_AirSafe (edict_t *ent, vec3_t dir);

// True when a jump, a walk off an edge or a swim, begun from where the
// player is and as it moves now, would bring it to rest unhurt; end is
// where, and up is how a swim is to be swum.
qboolean Nav_TryLink (edict_t *ent, nav_link_t *l, vec3_t end, int *up);

// whether a link can be taken as things stand now
qboolean Nav_LinkOpen (nav_link_t *l);

// g_rl_hazard.c: laser beams, triggers that hurt, lava and slime
void Haz_Find (void);
edict_t *Haz_LinkLaser (nav_link_t *l);
qboolean Haz_At (vec3_t origin, qboolean ducked);

// g_rl_plan.c: what the scripted player should do next to get out
#define	PLAN_EXIT	1		// walk into the trigger that ends the map
#define	PLAN_TOUCH	2		// walk into a button or a trigger
#define	PLAN_SHOOT	3		// shoot a button or a door that opens to a shot
#define	PLAN_PICKUP	4		// walk onto a key
#define	PLAN_KILL	5		// a monster whose death opens the way

typedef struct
{
	int		kind;
	edict_t	*ent;
} plan_job_t;

qboolean Plan_Update (int anchor, float *togo);
plan_job_t *Plan_JobAt (int node);
void Plan_Point (plan_job_t *job, vec3_t out);
qboolean Plan_ShotAt (edict_t *e, vec3_t eye, vec3_t out);

#endif
