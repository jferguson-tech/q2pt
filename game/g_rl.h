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
// g_rl.h -- the block of memory through which a program outside the game
// drives the player one server frame at a time and reads back what the
// player can perceive. The server (server/sv_rl.c) maps the block and waits
// on the other program; the game (g_rl*.c) fills it in. The other program
// has the same layout in rl/q2env/layout.py: change both together and raise
// RL_VERSION.
//
// Every field is a 32 bit integer, a 32 bit float or a run of bytes whose
// length is a multiple of four, so there is no padding to disagree about.

#ifndef G_RL_H
#define G_RL_H

#define	RL_MAGIC		0x314c5251		// "QRL1"
#define	RL_VERSION		6

// The player is moved by one command of this length in each server frame.
#define	RL_STEP_MSEC	100

// requests
#define	RL_REQ_RESET	1		// load a map and start an episode
#define	RL_REQ_STEP		2		// apply an action and run one server frame
#define	RL_REQ_QUIT		3

// Monsters take no notice of the player, as with the "notarget" cheat. For
// measuring how well the player finds its way with nothing shooting at it;
// never set for a run that counts as play.
#define	RL_FLAG_NOTARGET	1

// The map's monsters are taken out before the episode starts.
#define	RL_FLAG_NOMONSTERS	2

// what the teacher does
#define	RL_MODE_EXPLORE	0		// walks to one node after another
#define	RL_MODE_PLAY	1		// plays the map through to its exit

// how an episode ended
#define	RL_DONE_NO		0
#define	RL_DONE_EXIT	1		// the map's exit was reached
#define	RL_DONE_DEATH	2
#define	RL_DONE_TIME	3		// the step limit ran out

// The action has one choice in each of these branches.
#define	RL_ACT_FORWARD	0		// 0 back, 1 none, 2 forward
#define	RL_ACT_STRAFE	1		// 0 left, 1 none, 2 right
#define	RL_ACT_UP		2		// 0 crouch, 1 none, 2 jump
#define	RL_ACT_YAW		3		// a bin of rl_yaw_bins, degrees to turn left
#define	RL_ACT_PITCH	4		// a bin of rl_pitch_bins, degrees to look down
#define	RL_ACT_FIRE		5		// 0 or 1
#define	RL_ACT_WEAPON	6		// 0 keep, 1-10 change to that weapon
#define	RL_ACT_BRANCHES	7

#define	RL_YAW_BINS		15
#define	RL_PITCH_BINS	11
#define	RL_WEAPONS		10

// What the eye sees: a grid of rays across the field of view, each with the
// distance to what it hit, what kind of thing that was, and how steep.
#define	RL_RAYS_X		24
#define	RL_RAYS_Y		9
#define	RL_RAYS			(RL_RAYS_X*RL_RAYS_Y)
#define	RL_FOV_X		90.0f
#define	RL_FOV_Y		60.0f
#define	RL_RAY_RANGE	2048.0f

#define	RL_HIT_NONE		0		// nothing within range
#define	RL_HIT_WORLD	1
#define	RL_HIT_SKY		2
#define	RL_HIT_WATER	3
#define	RL_HIT_SLIME	4
#define	RL_HIT_LAVA		5
#define	RL_HIT_MOVER	6		// a door, lift, button or other brush that can move
#define	RL_HIT_MONSTER	7
#define	RL_HIT_CORPSE	8
#define	RL_HIT_OTHER	9		// a barrel or anything else solid
#define	RL_HIT_KINDS	10

// The things in the field of view with nothing between them and the eye,
// nearest first.
#define	RL_ENTS			16
#define	RL_ENT_FLOATS	12

#define	RL_ENT_KIND		0		// RL_KIND_
#define	RL_ENT_TYPE		1		// which monster or item: see rl_types in g_rl_obs.c
#define	RL_ENT_YAW		2		// degrees left of the middle of the view
#define	RL_ENT_PITCH	3		// degrees below it
#define	RL_ENT_DIST		4
#define	RL_ENT_VEL		5		// 5-7: its velocity, forward, left and up of the view
#define	RL_ENT_HURT		8		// 1 when it shows its wounded skin
#define	RL_ENT_FACING	9		// cosine between where it faces and the way to the eye
#define	RL_ENT_HEIGHT	10		// height of its box
#define	RL_ENT_WIDTH	11		// width of its box

#define	RL_KIND_NONE		0	// an empty row
#define	RL_KIND_MONSTER		1
#define	RL_KIND_CORPSE		2
#define	RL_KIND_HEALTH		3
#define	RL_KIND_ARMOR		4
#define	RL_KIND_AMMO		5
#define	RL_KIND_WEAPON		6
#define	RL_KIND_KEY			7
#define	RL_KIND_POWERUP		8
#define	RL_KIND_MISSILE		9	// a rocket, grenade, bolt or anything else in flight
#define	RL_KIND_BARREL		10
#define	RL_KIND_BUTTON		11
#define	RL_KIND_DOOR		12
#define	RL_KIND_LIFT		13
#define	RL_KIND_OTHER		14
#define	RL_KINDS			15

// The player's own state.
#define	RL_SELF_HEALTH		0
#define	RL_SELF_ARMOR		1
#define	RL_SELF_AMMO		2	// 2-7: shells, bullets, grenades, rockets, cells, slugs
#define	RL_SELF_WEAPON		8	// the weapon held, 1-10, or 0
#define	RL_SELF_OWNED		9	// 9-18: 1 for each weapon carried
#define	RL_SELF_READY		19	// 1 when the weapon can fire
#define	RL_SELF_VEL			20	// 20-22: velocity, forward, left and up of the view's heading
#define	RL_SELF_PITCH		23	// degrees below level
#define	RL_SELF_YAW_SIN		24
#define	RL_SELF_YAW_COS		25
#define	RL_SELF_GROUND		26
#define	RL_SELF_WATER		27	// 0 dry, 1 feet, 2 waist, 3 under
#define	RL_SELF_DUCKED		28
#define	RL_SELF_HURT		29	// damage taken in the last step
#define	RL_SELF_AIR			30	// seconds of breath left under water, 12 when not
#define	RL_SELF_KEYS		31	// how many keys are carried
// What a glance down shows: the ground a stride (RL_FOOT_REACH) from the
// feet, eight ways round from dead ahead of the view, turning left. With
// the view level the rays do not come down to the ground nearer than some
// 70 units, and a ledge the player stands at is under them.
#define	RL_SELF_DROP		32	// 32-39: how far below the feet the ground is there, 1 for RL_FOOT_DEPTH or more; -0.25 for a wall
#define	RL_SELF_HARM		40	// 40-47: 1 if standing there would hurt: lava, slime, a beam, a trigger that hurts
#define	RL_SELF_FLOATS		48
#define	RL_FOOT_WAYS		8
#define	RL_FOOT_REACH		40.0f
#define	RL_FOOT_DEPTH		128.0f

// Where the route planner would go next, for the player that is told.
#define	RL_GUIDE_VALID		0
#define	RL_GUIDE_YAW		1	// degrees left of the middle of the view
#define	RL_GUIDE_PITCH		2
#define	RL_GUIDE_DIST		3
#define	RL_GUIDE_FLOATS		4

// What a step was worth, for the reward to be made of outside the game.
#define	RL_GAIN_PROGRESS	0	// units gained along the planner's route
#define	RL_GAIN_DEALT		1	// damage done to monsters
#define	RL_GAIN_TAKEN		2	// damage taken, health and armour
#define	RL_GAIN_KILLS		3
#define	RL_GAIN_FLOATS		8

typedef struct
{
	int		magic;
	int		version;
	int		size;			// of this structure

	// ---- written by the other program
	int		request;
	int		action[RL_ACT_BRANCHES];
	int		act_teacher;	// the game plays its own teacher's action, not this one
	int		seed;
	int		skill;			// 0-3
	int		time_limit;		// steps; 0 for none
	int		flags;			// RL_FLAG_
	char	map[64];
	char	back[256];		// maps, with spaces between, whose exits are not the way on
	int		mode;			// RL_MODE_: what the teacher is out to do
	char	demo[256];		// full path of a demo to record the episode into, or empty

	// ---- written by the game and the server
	int		error;			// set when a request could not be carried out
	char	error_text[128];
	int		step;			// steps since the reset
	int		done;			// RL_DONE_
	unsigned	hash;		// of the world's state: equal for equal runs
	int		demo_frames;	// written so far
	int		demo_dropped;	// frames too large for a demo block
	int		teacher[RL_ACT_BRANCHES];	// what the teacher would do from here
	float	gain[RL_GAIN_FLOATS];

	// what the player perceives
	float	self[RL_SELF_FLOATS];
	float	guide[RL_GUIDE_FLOATS];
	float	ray_dist[RL_RAYS];
	float	ray_slope[RL_RAYS];		// z of the surface's normal: 1 floor, 0 wall, -1 ceiling
	int		ray_kind[RL_RAYS];
	float	ents[RL_ENTS][RL_ENT_FLOATS];

	// Not for the player: where it really is, for scoring and for tests.
	float	origin[3];
	float	angles[3];
	int		monsters_total;
	int		monsters_killed;
	int		nav_count;		// nodes in the map's navigation graph
	int		nav_node;		// the one the player is at, or -1
	int		nav_goal;		// the one the teacher is making for
	int		goals_reached;	// by the player, of those the explorer set
	int		goals_failed;	// given up for taking too long
	int		restart;		// the game asks for the reset to be done over
	int		link_type;		// the kind of link the teacher is on (NAV_), or -1 with no way to go
	int		link_ent;		// the door or lift that link depends on, or 0
	float	route_left;		// seconds of route from here to the teacher's goal, or -1
	int		job_kind;		// what the teacher is on its way to do (PLAN_), or 0
	int		job_ent;		// and to which entity
	int		fighting;		// the entity the teacher is shooting at, or 0
	int		death_means;	// when dead: how, as the game's MOD_ numbers have it
	int		death_by;		// and by which monster, as RL_ENT_TYPE numbers them; 0 for none
} rl_shared_t;

// The name of the cvar in which the server leaves the block's address for
// the game, printed with %p. Empty when nothing is driving the game.
#define	RL_BLOCK_CVAR	"rl_block"

#ifdef GAME_INCLUDE
extern	rl_shared_t	*rl_block;

#define	RAD2DEG_F(a)	((float)((a) * (180.0 / M_PI)))

// g_rl_teach.c
qboolean Teach_Reset (void);
void Teach_Think (edict_t *ent);
#endif

#endif
