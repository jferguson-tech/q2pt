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
// g_rl_plan.c -- what the scripted player should do next to get out of the
// map: a job (reach the exit, press a button, walk into a trigger, shoot a
// switch, pick up a key, kill a monster) and the cost of the way to it from
// every node.
//
// The plan is worked out from the map's entities as they are now, and is
// the same whatever was planned before. It starts from the exit: the
// triggers that fire a target_changelevel. If the way there is shut, the
// route that would be taken with every door and lift obliging is followed
// to the first that is not, and the jobs become whatever sends that one:
// the buttons and triggers that target it, the key such a trigger wants, a
// monster whose death fires it. If the way to those is shut too, the same
// is done again from there, a few deep.

#include "g_local.h"
#include "g_rl.h"
#include "g_rl_nav.h"

#define	PLAN_MAX_JOBS	32
#define	PLAN_MAX_GOALS	1024
#define	PLAN_NEAR		400		// places kept for a thing done from a distance
#define	PLAN_DEPTH		6

plan_job_t	plan_jobs[PLAN_MAX_JOBS];
int			plan_num_jobs;

static short	*plan_node_job;		// for each node, 1 + the job it is a place for, or 0
static int		plan_goals[PLAN_MAX_GOALS];
static int		plan_num_goals;
static float	*plan_hope;			// costs with every mover obliging
static cvar_t	*plan_debug;

static void Plan_Senders (char *targetname, int depth);

static void Plan_AddJob (int kind, edict_t *e)
{
	int		i;

	for (i=0 ; i<plan_num_jobs ; i++)
		if (plan_jobs[i].ent == e && plan_jobs[i].kind == kind)
			return;
	if (plan_num_jobs == PLAN_MAX_JOBS)
		return;
	plan_jobs[plan_num_jobs].kind = kind;
	plan_jobs[plan_num_jobs].ent = e;
	plan_num_jobs++;
}

/*
================
Plan_Sender

e is something that, when it fires, fires the thing wanted. Adds the job
that makes e fire, or looks further back for what fires e.
================
*/
static void Plan_Sender (edict_t *e, int depth)
{
	edict_t	*player = &g_edicts[1];
	edict_t	*k;
	int		i;

	if (!e->inuse || !e->classname || depth > PLAN_DEPTH)
		return;

	if (!strcmp (e->classname, "func_button"))
	{
		if (e->moveinfo.state != 1)		// pressed already, and not yet back
			return;
		if (e->max_health)
			Plan_AddJob (PLAN_SHOOT, e);
		else if (!e->targetname)
			Plan_AddJob (PLAN_TOUCH, e);
		else
			Plan_Senders (e->targetname, depth + 1);
		return;
	}

	if (!strcmp (e->classname, "trigger_multiple") || !strcmp (e->classname, "trigger_once"))
	{
		if (e->solid == SOLID_TRIGGER)
			Plan_AddJob (PLAN_TOUCH, e);
		else if (e->targetname)
			Plan_Senders (e->targetname, depth + 1);	// to be switched on first
		return;
	}

	if (!strcmp (e->classname, "trigger_key"))
	{
		if (e->item && !player->client->pers.inventory[ITEM_INDEX(e->item)])
		{	// the key first, if it lies in this map
			for (i=game.maxclients+1, k=g_edicts+i ; i<globals.num_edicts ; i++, k++)
				if (k->inuse && k->item == e->item && k->solid == SOLID_TRIGGER)
					Plan_AddJob (PLAN_PICKUP, k);
			return;
		}
		if (e->targetname)
			Plan_Senders (e->targetname, depth + 1);
		return;
	}

	if (e->svflags & SVF_MONSTER)
	{
		if (e->health <= 0)
			return;
		if (e->solid == SOLID_NOT || (e->svflags & SVF_NOCLIENT))
			Plan_Senders (e->targetname, depth + 1);	// to be brought in first
		else
			Plan_AddJob (PLAN_KILL, e);
		return;
	}

	// a door or lift that passes its use on: whatever sends it sends this
	if (!strcmp (e->classname, "func_door") || !strcmp (e->classname, "func_door_rotating")
		|| !strcmp (e->classname, "func_plat"))
	{
		if (e->max_health)
			Plan_AddJob (PLAN_SHOOT, e);
		else if (e->targetname)
			Plan_Senders (e->targetname, depth + 1);
		else
			Plan_AddJob (PLAN_TOUCH, e);
		return;
	}

	// relays, counters and the like are fired by name
	if (e->targetname && strcmp (e->classname, "trigger_always") && strcmp (e->classname, "func_timer"))
		Plan_Senders (e->targetname, depth + 1);
}

/*
================
Plan_Senders

Everything that fires the entities of this name: those that target it, and
monsters that fire it when they die.
================
*/
static void Plan_Senders (char *targetname, int depth)
{
	edict_t	*e;
	int		i;

	if (!targetname || depth > PLAN_DEPTH)
		return;
	for (i=game.maxclients+1, e=g_edicts+i ; i<globals.num_edicts ; i++, e++)
	{
		if (!e->inuse)
			continue;
		if ((e->target && !strcmp (e->target, targetname))
			|| (e->deathtarget && !strcmp (e->deathtarget, targetname))
			|| (e->killtarget && !strcmp (e->killtarget, targetname) && (e->svflags & SVF_MONSTER)))
			Plan_Sender (e, depth);
	}
}

/*
================
Plan_LeadsBack

True when a target_changelevel's map is one of those the player is not to
go to. The map is named as "base2$start", "*bunk1" at the end of a unit, or
after a film as "eou1_.cin+*bunk1$start".
================
*/
static qboolean Plan_LeadsBack (char *map)
{
	char	name[MAX_QPATH], *s, *list;
	int		len;

	s = strrchr (map, '+');
	s = s ? s + 1 : map;
	if (*s == '*')
		s++;
	Com_sprintf (name, sizeof(name), "%s", s);
	s = strchr (name, '$');
	if (s)
		*s = 0;
	len = strlen (name);

	for (list = rl_block->back ; *list ; )
	{
		while (*list == ' ')
			list++;
		s = list;
		while (*list && *list != ' ')
			list++;
		if (list - s == len && !Q_strncasecmp (s, name, len))
			return true;
	}
	return false;
}

/*
================
Plan_Exits

The jobs that end the map: the triggers that fire a target_changelevel. With
several ways out, those to maps the player is not to go to are left out
when there is any other.
================
*/
static void Plan_Exits (void)
{
	edict_t	*e;
	int		i, pass;

	for (pass=0 ; pass<2 && !plan_num_jobs ; pass++)
		for (i=game.maxclients+1, e=g_edicts+i ; i<globals.num_edicts ; i++, e++)
		{
			if (!e->inuse || !e->classname || strcmp (e->classname, "target_changelevel") || !e->map)
				continue;
			if (!pass && Plan_LeadsBack (e->map))
				continue;
			Plan_Senders (e->targetname, 0);
		}
	for (i=0 ; i<plan_num_jobs ; i++)
		if (plan_jobs[i].kind == PLAN_TOUCH)
			plan_jobs[i].kind = PLAN_EXIT;
}

/*
================
Plan_Point

Where a job is done: the middle of the thing's box
================
*/
void Plan_Point (plan_job_t *job, vec3_t out)
{
	edict_t	*e = job->ent;

	if (job->kind == PLAN_PICKUP || job->kind == PLAN_KILL)
	{
		VectorCopy (e->s.origin, out);
		return;
	}
	VectorAdd (e->absmin, e->absmax, out);
	VectorScale (out, 0.5f, out);
}

/*
================
Plan_ShotAt

A point of a thing to look at so that a shot reaches it: its middle if a
shot at that gets through, or else one of a grid of points through its box,
since a switch behind bars shows only in part. Returns false when none does.
================
*/
qboolean Plan_ShotAt (edict_t *e, vec3_t eye, vec3_t out)
{
	static const float	at[3] = {0.5f, 0.2f, 0.8f};
	static const float	off[5][2] = {{0, 0}, {4, 0}, {-4, 0}, {0, 4}, {0, -4}};
	edict_t	*player = &g_edicts[1];
	trace_t	tr;
	int		i, j, k, m;
	vec3_t	dir, from, end;
	float	len;

	for (i=0 ; i<3 ; i++)
	for (j=0 ; j<3 ; j++)
	for (k=0 ; k<3 ; k++)
	{
		out[0] = e->absmin[0] + (e->absmax[0] - e->absmin[0]) * at[i];
		out[1] = e->absmin[1] + (e->absmax[1] - e->absmin[1]) * at[j];
		out[2] = e->absmin[2] + (e->absmax[2] - e->absmin[2]) * at[k];
		// The shot leaves the gun, which is held a hand to the right of the
		// eye and a hand below it, and flies the way the eye looks: so it
		// is the gun's line that has to be clear, as far as the thing.
		// And clear by a margin: the view settles to within a third of a
		// degree, which far off is a few units to any side.
		VectorSubtract (out, eye, dir);
		len = VectorNormalize (dir);
		for (m=0 ; m<5 ; m++)
		{
			VectorSet (from, eye[0] + (8 + off[m][0]) * dir[1], eye[1] - (8 + off[m][0]) * dir[0],
				eye[2] - 8 + off[m][1]);
			VectorMA (from, len + 64, dir, end);
			tr = gi.trace (from, NULL, NULL, end, player, MASK_SHOT);
			if (tr.ent != e)
				break;
		}
		if (m == 5)
			return true;
	}
	return false;
}

// The places a thing can be shot from take many traces to find, and do not
// change while the doors stand as they do: they are kept for a few seconds.
#define	PLAN_KEPT	8
typedef struct
{
	edict_t	*ent;
	int		frame;
	int		count;
	int		nodes[PLAN_NEAR];
} plan_kept_t;
static plan_kept_t	plan_kept[PLAN_KEPT];
static int			plan_kept_level = -1;

/*
================
Plan_Places

Marks the nodes from which each job can be done, and lists them as goals.
For a thing to touch or pick up, the nodes inside its box, or failing that
the few nearest it. For a thing to shoot, nodes with a clear line to it.
================
*/
static void Plan_Places (void)
{
	edict_t		*player = &g_edicts[1];
	plan_job_t	*job;
	int			j, n, found, near[4], keep;
	float		d, near_d[4], reach;
	vec3_t		p, q, mid;
	trace_t		tr;
	int			k, m;
	plan_kept_t	*kept;

	memset (plan_node_job, 0, nav_num_nodes * sizeof(short));
	plan_num_goals = 0;

	if (plan_kept_level != level.framenum - rl_block->step)
	{	// a new episode
		memset (plan_kept, 0, sizeof(plan_kept));
		plan_kept_level = level.framenum - rl_block->step;
	}

	for (j=0, job=plan_jobs ; j<plan_num_jobs ; j++, job++)
	{
		Plan_Point (job, mid);
		found = 0;

		kept = NULL;
		if (job->kind == PLAN_SHOOT)
		{
			for (k=0 ; k<PLAN_KEPT ; k++)
				if (plan_kept[k].ent == job->ent)
					kept = &plan_kept[k];
			if (kept && level.framenum - kept->frame < 50)
			{
				for (k=0 ; k<kept->count ; k++)
					if (!plan_node_job[kept->nodes[k]] && plan_num_goals < PLAN_MAX_GOALS)
					{
						plan_node_job[kept->nodes[k]] = j + 1;
						plan_goals[plan_num_goals++] = kept->nodes[k];
					}
				continue;
			}
			if (!kept)
			{	// the slot longest unused
				kept = &plan_kept[0];
				for (k=1 ; k<PLAN_KEPT ; k++)
					if (plan_kept[k].frame < kept->frame)
						kept = &plan_kept[k];
			}
			kept->ent = job->ent;
			kept->frame = level.framenum;
			kept->count = 0;
		}

		keep = 4;
		for (k=0 ; k<keep ; k++)
		{
			near[k] = -1;
			near_d[k] = 1e30f;
		}
		reach = job->kind == PLAN_SHOOT ? 700 : 96;

		for (n=0 ; n<nav_num_nodes ; n++)
		{
			if (plan_node_job[n])
				continue;
			Nav_NodeOrigin (n, p);
			VectorSubtract (p, mid, q);
			d = VectorLength (q);
			if (d > reach + 600)
				continue;

			if (job->kind != PLAN_SHOOT && job->kind != PLAN_KILL
				&& p[0] > job->ent->absmin[0] - 15 && p[0] < job->ent->absmax[0] + 15
				&& p[1] > job->ent->absmin[1] - 15 && p[1] < job->ent->absmax[1] + 15
				&& p[2] > job->ent->absmin[2] - 30 && p[2] < job->ent->absmax[2] + 22)
			{	// the player's box would be in the thing's
				plan_node_job[n] = j + 1;
				if (plan_num_goals < PLAN_MAX_GOALS)
					plan_goals[plan_num_goals++] = n;
				found++;
				continue;
			}
			if (d > reach)
				continue;

			// with nothing of the world between the eye and it
			p[2] += 22;
			if (job->kind == PLAN_SHOOT)
			{	// every such place: the nearest may all lie past the very
				// door the shot is to open
				if (!Plan_ShotAt (job->ent, p, q))
					continue;
				if (kept->count < PLAN_NEAR && plan_num_goals < PLAN_MAX_GOALS)
				{
					plan_node_job[n] = j + 1;
					plan_goals[plan_num_goals++] = n;
					kept->nodes[kept->count++] = n;
				}
				continue;
			}
			else
			{
				tr = gi.trace (p, NULL, NULL, mid, player, MASK_SOLID);
				if (tr.fraction < 1 && tr.ent != job->ent)
					continue;
			}
			for (k=0 ; k<keep ; k++)
				if (d < near_d[k])
				{
					for (m=keep-1 ; m>k ; m--)
					{
						near[m] = near[m-1];
						near_d[m] = near_d[m-1];
					}
					near[k] = n;
					near_d[k] = d;
					break;
				}
		}

		// for a thing to shoot any of the near ones will do; for the rest,
		// only when no node was inside
		if (found || job->kind == PLAN_SHOOT)
			continue;
		for (k=0 ; k<keep ; k++)
			if (near[k] != -1 && plan_num_goals < PLAN_MAX_GOALS)
			{
				plan_node_job[near[k]] = j + 1;
				plan_goals[plan_num_goals++] = near[k];
			}
	}
}

/*
================
Plan_Update

Works out the jobs and fills togo with the cost from every node to the
nearest place one of them can be done. Returns false when there is nothing
the player can get to that would help.
================
*/
qboolean Plan_Update (int anchor, float *togo)
{
	int			depth, tries, n, i, steps;
	nav_link_t	*l, *best;
	float		c, bestc;
	edict_t		*group[16], *laser;
	int			count;

	if (!plan_node_job)
	{
		plan_node_job = gi.TagMalloc ((nav_num_nodes + 1) * sizeof(short), TAG_LEVEL);
		plan_hope = gi.TagMalloc ((nav_num_nodes + 1) * sizeof(float), TAG_LEVEL);
	}

	plan_debug = gi.cvar ("rl_debug", "0", 0);
	plan_num_jobs = 0;
	Plan_Exits ();
	memset (nav_hopeless, 0, sizeof(nav_hopeless));

	for (depth=0 ; depth<PLAN_DEPTH ; depth++)
	{
		if (!plan_num_jobs)
			return false;
		Plan_Places ();
		if (!plan_num_goals)
			return false;
		Nav_CostsToAny (plan_goals, plan_num_goals, togo);
		if (plan_debug->value)
		{
			gi.dprintf ("plan depth %i: %i jobs, %i places, %s\n", depth, plan_num_jobs, plan_num_goals,
				togo[anchor] < NAV_FAR ? "way open" : "way shut");
			for (i=0 ; i<plan_num_jobs ; i++)
				gi.dprintf ("   job %i on %i %s\n", plan_jobs[i].kind, (int)(plan_jobs[i].ent - g_edicts),
					plan_jobs[i].ent->classname);
		}
		if (togo[anchor] < NAV_FAR)
			return true;

		// The way is shut. Which door or lift shuts it, and what sends that?
		for (tries=0 ; tries<12 ; tries++)
		{
			nav_hopeful = true;
			Nav_CostsToAny (plan_goals, plan_num_goals, plan_hope);
			best = NULL;
			if (plan_hope[anchor] < NAV_FAR)
			{	// along the hopeful route to the first link that is not there now
				n = anchor;
				for (steps=0 ; steps<nav_num_nodes && plan_hope[n] > 0 ; steps++)
				{
					best = NULL;
					bestc = NAV_FAR;
					for (i=0, l=&nav_links[nav_nodes[n].first_link] ; i<nav_nodes[n].num_links ; i++, l++)
					{
						if (plan_hope[l->to] >= NAV_FAR || !Nav_LinkOpen (l))
							continue;
						c = l->cost + plan_hope[l->to];
						if (c < bestc)
						{
							bestc = c;
							best = l;
						}
					}
					if (!best)
						break;
					nav_hopeful = false;
					if (!Nav_LinkOpen (best))
						break;
					nav_hopeful = true;
					n = best->to;
					best = NULL;
				}
			}
			nav_hopeful = false;
			if (!best)
				return false;		// no route even with every door obliging

			plan_num_jobs = 0;
			laser = Haz_LinkLaser (best);
			if (laser)
			{	// shut by a beam: whatever switches it
				if (plan_debug->value)
					gi.dprintf ("   shut by laser %i (link %i -> %i)\n", (int)(laser - g_edicts), best->from, best->to);
				Plan_Senders (laser->targetname, 1);
				if (plan_num_jobs)
					break;
				nav_hopeless[laser - g_edicts] = 1;
				continue;
			}
			if (plan_debug->value)
				gi.dprintf ("   shut by %i %s (link %i -> %i type %i wants %i)\n", best->ent,
					g_edicts[best->ent].classname, best->from, best->to, best->type, best->state);
			// best is the link that is shut; its mover's group is what to send
			count = Nav_MoverGroup (&g_edicts[best->ent], group, 16);
			for (i=0 ; i<count ; i++)
			{
				if (group[i]->max_health)
					Plan_AddJob (PLAN_SHOOT, group[i]);
				else if (group[i]->targetname)
					Plan_Senders (group[i]->targetname, 1);
			}
			if (plan_num_jobs)
				break;
			// nothing sends it: plan as if it were not there to be sent
			for (i=0 ; i<count ; i++)
				nav_hopeless[group[i] - g_edicts] = 1;
		}
		if (tries == 12)
			return false;
	}
	return false;
}

/*
================
Plan_JobAt

The job that can be done from a node, or NULL
================
*/
plan_job_t *Plan_JobAt (int node)
{
	if (!plan_node_job || node < 0 || !plan_node_job[node])
		return NULL;
	return &plan_jobs[plan_node_job[node] - 1];
}
