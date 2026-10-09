/*
Copyright (C) 1997-2001 Id Software, Inc.
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
// g_lightball.c -- a ball of light the player throws. It bounces, rolls down
// whatever slopes and comes to rest, lighting the place it is in. It hurts
// nothing and nothing hurts it; walking into it kicks it along.

#include "g_local.h"

#define	BALL_BOUNCE			0.55f	// of the speed into a surface, how much it leaves with
#define	BALL_BOUNCE_MIN		70		// slower into a surface than this, it rolls on it
#define	BALL_BOUNCE_KEEP	0.9f	// of the speed along a surface, how much a bounce leaves
#define	BALL_DRAG			0.35f	// rolling: share of its speed lost a second ...
#define	BALL_DRAG_FLAT		25		// ... and speed lost a second besides
#define	BALL_STOP			12		// rolling on the level slower than this, it stops
#define	BALL_LEVEL			0.995f	// a floor's normal[2] from which it counts as level
#define	BALL_THROW			520
#define	BALL_THROW_UP		140

static const struct
{
	char		*name;
	byte		rgb[3];
} ball_colours[] =
{
	{ "warm",	{ 255, 200, 140 } },
	{ "white",	{ 255, 255, 255 } },
	{ "red",	{ 255, 50, 40 } },
	{ "orange",	{ 255, 130, 30 } },
	{ "yellow",	{ 255, 230, 60 } },
	{ "green",	{ 60, 255, 80 } },
	{ "cyan",	{ 60, 230, 255 } },
	{ "blue",	{ 60, 90, 255 } },
	{ "purple",	{ 190, 70, 255 } },
};

#define	NUM_BALL_COLOURS	( (int)( sizeof(ball_colours) / sizeof(ball_colours[0]) ) )

static int	ball_nextcolour;

/*
=============
LightBall_Collide

Balls knock one another away. Each pair is seen to once, by whichever of the
two comes first.
=============
*/
static void LightBall_Collide (edict_t *ent)
{
	edict_t	*other;
	vec3_t	n;
	float	dist, closing, apart, push;

	for (other=ent+1 ; other<g_edicts+globals.num_edicts ; other++)
	{
		if (!other->inuse || other->movetype != MOVETYPE_BALL)
			continue;
		VectorSubtract (ent->s.origin, other->s.origin, n);
		dist = VectorNormalize (n);
		if (dist >= 2 * LIGHTBALL_RADIUS)
			continue;
		if (dist == 0)
			VectorSet (n, crandom (), crandom (), 0.1f);	// in the same place: any way out will do

		// what a bounce between equals does to each, then enough besides
		// that two lying in one another ease apart
		closing = DotProduct (other->velocity, n) - DotProduct (ent->velocity, n);
		push = closing > 0 ? closing * (1 + BALL_BOUNCE) * 0.5f : 0;
		apart = closing > 0 ? closing * BALL_BOUNCE : -closing;
		if (apart < 40)
			push += (40 - apart) * 0.5f;
		if (push <= 0)
			continue;
		VectorMA (ent->velocity, push, n, ent->velocity);
		VectorMA (other->velocity, -push, n, other->velocity);
		ent->groundentity = other->groundentity = NULL;
		if (closing > 150)
			gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/hgrenb1a.wav"), closing > 400 ? 1 : closing / 400, ATTN_NORM, 0);
	}
}

/*
=============
SV_Physics_Ball

Through the air under gravity. What it meets hard it bounces off; what it
meets gently it rolls along, pulled down a slope and slowed by the rolling,
until on level ground it is slow enough to stop.
=============
*/
void SV_Physics_Ball (edict_t *ent)
{
	trace_t		trace;
	vec3_t		end, floornormal, from;
	edict_t		*ground;
	float		time, into, speed, newspeed, gravity, loudest, unearned;
	qboolean	wasinwater, isinwater, onfloor;
	int			bump;

	SV_RunThink (ent);
	if (!ent->inuse)
		return;

	if (ent->groundentity && !ent->groundentity->inuse)
		ent->groundentity = NULL;

	LightBall_Collide (ent);

	// at rest: nothing to do while what it lies on is still under it
	if (ent->groundentity && VectorCompare (ent->velocity, vec3_origin))
	{
		VectorCopy (ent->s.origin, end);
		end[2] -= 1;
		trace = gi.trace (ent->s.origin, ent->mins, ent->maxs, end, ent, ent->clipmask);
		if (!trace.startsolid && trace.fraction < 1 && trace.plane.normal[2] >= BALL_LEVEL)
			return;
		ent->groundentity = NULL;
	}

	wasinwater = (ent->watertype & MASK_WATER) != 0;
	ent->watertype = gi.pointcontents (ent->s.origin);
	isinwater = (ent->watertype & MASK_WATER) != 0;
	ent->waterlevel = isinwater ? 1 : 0;
	if (wasinwater != isinwater)
		gi.positioned_sound (ent->s.origin, g_edicts, CHAN_AUTO, gi.soundindex ("misc/h2ohit1.wav"), 1, 1, 0);

	SV_CheckVelocity (ent);

	// it sinks, slowly
	gravity = ent->gravity * sv_gravity->value * FRAMETIME;
	if (isinwater)
	{
		gravity *= 0.3f;
		VectorScale (ent->velocity, 0.85f, ent->velocity);
	}
	ent->velocity[2] -= gravity;

	VectorCopy (ent->s.origin, from);
	time = FRAMETIME;
	onfloor = false;
	ground = NULL;
	loudest = 0;
	VectorClear (floornormal);
	for (bump=0 ; bump<4 && time>0 ; bump++)
	{
		VectorMA (ent->s.origin, time, ent->velocity, end);
		trace = gi.trace (ent->s.origin, ent->mins, ent->maxs, end, ent, ent->clipmask);
		if (trace.allsolid)
		{	// shut in by something: wait for it to go
			VectorClear (ent->velocity);
			break;
		}
		VectorCopy (trace.endpos, ent->s.origin);
		if (trace.fraction == 1)
			break;
		time -= time * trace.fraction;

		// The frame's whole fall was added before it set off, though it has
		// met something part way through. How hard it met it is reckoned
		// without the fall not yet come by, which is given back to it after
		// the bounce; else a ball lying still would be thrown up by its own
		// weight, time after time.
		unearned = gravity * (time / FRAMETIME);
		ent->velocity[2] += unearned;
		into = -DotProduct (ent->velocity, trace.plane.normal);
		if (into > BALL_BOUNCE_MIN)
		{
			ClipVelocity (ent->velocity, trace.plane.normal, ent->velocity, 1 + BALL_BOUNCE);
			// what is left along the surface loses a little as well
			speed = DotProduct (ent->velocity, trace.plane.normal);
			VectorMA (ent->velocity, -speed, trace.plane.normal, ent->velocity);
			VectorScale (ent->velocity, BALL_BOUNCE_KEEP, ent->velocity);
			VectorMA (ent->velocity, speed, trace.plane.normal, ent->velocity);
			ent->velocity[2] -= unearned;
			if (into > loudest)
				loudest = into;
		}
		else
		{
			ent->velocity[2] -= unearned;
			ClipVelocity (ent->velocity, trace.plane.normal, ent->velocity, 1);
			if (trace.plane.normal[2] > 0.7)
			{
				onfloor = true;
				ground = trace.ent;
				VectorCopy (trace.plane.normal, floornormal);
			}
		}
	}

	if (loudest > 0 && !isinwater)
	{
		float	volume = loudest / 400;

		gi.sound (ent, CHAN_VOICE, gi.soundindex (random() > 0.5 ? "weapons/hgrenb1a.wav" : "weapons/hgrenb2a.wav"),
			volume > 1 ? 1 : volume, ATTN_NORM, 0);
	}

	if (onfloor)
	{
		speed = VectorLength (ent->velocity);
		newspeed = speed - (speed * BALL_DRAG + BALL_DRAG_FLAT) * FRAMETIME;
		if (newspeed < 0 || (newspeed < BALL_STOP && floornormal[2] >= BALL_LEVEL))
			newspeed = 0;
		// wedged where a slope meets a wall: it is going nowhere, however
		// hard it is pulled
		VectorSubtract (ent->s.origin, from, end);
		if (DotProduct (end, end) < 0.25f)
			newspeed = 0;
		if (speed > 0)
			VectorScale (ent->velocity, newspeed / speed, ent->velocity);
		ent->groundentity = ground;
		ent->groundentity_linkcount = ground->linkcount;
	}
	else
		ent->groundentity = NULL;

	gi.linkentity (ent);
}

/*
=============
LightBall_Touch

Someone has walked into it: off it goes, the way they were going
=============
*/
static void LightBall_Touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	dir;
	float	speed;

	if (!other->client)
		return;

	VectorSubtract (self->s.origin, other->s.origin, dir);
	dir[2] = 0;
	if (VectorNormalize (dir) == 0)
		return;
	speed = DotProduct (other->velocity, dir);
	if (speed < 40)
		return;		// standing by it, or walking away
	speed *= 1.4f;
	if (DotProduct (self->velocity, dir) >= speed)
		return;		// already leaving faster than that: just thrown, or kicked

	VectorScale (dir, speed, self->velocity);
	self->velocity[2] = speed * 0.25f;
	self->groundentity = NULL;
}

// takes away the balls thrown by ent, or by anyone if ent is NULL, but for
// the newest keep of them
static void LightBall_Remove (edict_t *owner, int keep)
{
	edict_t	*e, *oldest;
	int		i, count;

	for (;;)
	{
		count = 0;
		oldest = NULL;
		for (i=0, e=g_edicts ; i<globals.num_edicts ; i++, e++)
		{
			if (!e->inuse || e->movetype != MOVETYPE_BALL)
				continue;
			if (owner && e->owner != owner)
				continue;
			count++;
			if (!oldest || e->timestamp < oldest->timestamp)
				oldest = e;
		}
		if (count <= keep)
			return;
		G_FreeEdict (oldest);
	}
}

/*
=============
Cmd_ThrowLight_f

throwlight			the next colour in turn
throwlight <colour>	warm, white, red, orange, yellow, green, cyan, blue, purple
throwlight clear	takes back the ones this player threw
=============
*/
void Cmd_ThrowLight_f (edict_t *ent)
{
	edict_t	*ball;
	trace_t	trace;
	vec3_t	forward, right, up, eye, start;
	vec3_t	mins = { -LIGHTBALL_RADIUS, -LIGHTBALL_RADIUS, -LIGHTBALL_RADIUS };
	vec3_t	maxs = { LIGHTBALL_RADIUS, LIGHTBALL_RADIUS, LIGHTBALL_RADIUS };
	char	*arg;
	int		i, colour, max, bright;

	if (ent->health <= 0 || ent->deadflag)
		return;

	arg = gi.argv (1);
	if (!Q_stricmp (arg, "clear"))
	{
		LightBall_Remove (ent, 0);
		return;
	}

	colour = -1;
	if (arg[0])
	{
		for (i=0 ; i<NUM_BALL_COLOURS ; i++)
			if (!Q_stricmp (arg, ball_colours[i].name))
				colour = i;
		if (colour < 0)
		{
			gi.cprintf (ent, PRINT_HIGH, "throwlight [clear |");
			for (i=0 ; i<NUM_BALL_COLOURS ; i++)
				gi.cprintf (ent, PRINT_HIGH, " %s", ball_colours[i].name);
			gi.cprintf (ent, PRINT_HIGH, "]\n");
			return;
		}
	}
	else
		colour = ball_nextcolour++ % NUM_BALL_COLOURS;

	// each is a light for the renderer to reckon with, so there is a limit;
	// the oldest goes to make room
	max = (int)gi.cvar ("lightball_max", "6", 0)->value;
	if (max < 1)
		max = 1;
	if (max > 24)
		max = 24;
	LightBall_Remove (NULL, max - 1);

	bright = (int)gi.cvar ("lightball_brightness", "300", 0)->value / 2;
	if (bright < 1)
		bright = 1;
	if (bright > 255)
		bright = 255;

	// from in front of the eye, or as far that way as there is room
	AngleVectors (ent->client->v_angle, forward, right, up);
	VectorCopy (ent->s.origin, eye);
	eye[2] += ent->viewheight - 8;
	VectorMA (eye, 28, forward, start);
	trace = gi.trace (eye, mins, maxs, start, ent, MASK_SOLID);
	if (trace.startsolid)
		return;

	ball = G_Spawn ();
	VectorCopy (trace.endpos, ball->s.origin);
	VectorCopy (trace.endpos, ball->s.old_origin);
	VectorScale (forward, BALL_THROW, ball->velocity);
	VectorMA (ball->velocity, BALL_THROW_UP, up, ball->velocity);
	VectorAdd (ball->velocity, ent->velocity, ball->velocity);
	VectorCopy (mins, ball->mins);
	VectorCopy (maxs, ball->maxs);
	ball->movetype = MOVETYPE_BALL;
	ball->clipmask = MASK_SOLID;
	ball->solid = SOLID_TRIGGER;
	ball->touch = LightBall_Touch;
	ball->owner = ent;
	ball->timestamp = level.time;
	ball->classname = "lightball";
	// what the renderers that do not know of it draw in its place
	ball->s.modelindex = gi.modelindex ("models/objects/grenade2/tris.md2");
	ball->s.renderfx = RF_LIGHTBALL;
	ball->s.skinnum = (int)(ball_colours[colour].rgb[0] | (ball_colours[colour].rgb[1] << 8)
		| (ball_colours[colour].rgb[2] << 16) | ((unsigned)bright << 24));

	gi.sound (ent, CHAN_WEAPON, gi.soundindex ("weapons/hgrent1a.wav"), 1, ATTN_NORM, 0);
	gi.linkentity (ball);
}
