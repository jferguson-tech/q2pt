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
// rpt_water.c -- the map's bodies of liquid and their wave simulations
//
// The map knows nothing of pools, only of faces drawn with a liquid
// texture. Level faces of the same liquid at the same height that lie near
// each other are taken to be one body. Each body gets a simulation (in
// ../pt/water, which knows nothing of the game) that is poked where the
// player, monsters, items and splashes touch its surface. What that beats
// to froth the simulation keeps; the spray it throws up is kept here.

#include "rpt_local.h"
#include "../pt/water/pt_water.h"

#define	MAX_WATER_BODIES	48
#define	MAX_BODY_MATERIALS	8
#define	BODY_JOIN_DISTANCE	96		// faces this near each other are the same body

typedef struct
{
	image_t		*image;
	float		z;
	float		mins[2], maxs[2];
	int			materials[MAX_BODY_MATERIALS];	// world material indices: its faces need not all be alike
	int			nummaterials;
	qboolean	lava;
	qboolean	clear;				// water, as against lava and slime
	float		absorb[3];
	int			parent;				// the body this one turned out to be part of, or itself
	pt_water_t	*sim;				// only bodies that are their own parent have one
	int			wave_texture, caustic_texture;		// backend handles, -1 = none
} waterbody_t;

// the shape of the liquid, kept until the simulations are made
typedef struct
{
	int		body;
	float	p[3][2];
	float	stream[2];		// which way the liquid flows here, or 0, 0
} watertri_t;

#define	WATER_GRAVITY	386.0f	// a unit is about an inch: waves 64 units deep run at about 160 a second
#define	LAVA_GRAVITY	54.0f	// heavy liquids move slowly
#define	STREAM_SPEED	48.0f	// units a second: what the map calls flowing
#define	BODY_AMOUNT		1.75f	// how much of a player or a monster is in the liquid, as pt_water_move counts it
#define	FOAM_LIFE		2.5f	// seconds for half the froth to go
#define	CHURN_SPEED		110.0f	// what moves through the liquid faster than this leaves froth behind

// a drop of spray on its way up and down again
typedef struct
{
	vec3_t	origin, velocity;
	float	radius;
	int		body;			// where it came from and falls back to
} waterdrop_t;

#define	MAX_WATER_DROPS	768
#define	DROP_GRAVITY	800.0f

static waterbody_t	w_bodies[MAX_WATER_BODIES];
static int			w_numbodies;
static watertri_t	*w_tris;
static int			w_numtris, w_maxtris;
static float		w_lasttime;
static vec3_t		w_lasteye;
static waterdrop_t	w_drops[MAX_WATER_DROPS];
static int			w_numdrops;

/*
===============
R_WaterReset

Before a map is loaded: forget the last one's
===============
*/
void R_WaterReset (void)
{
	int		i;

	for (i=0 ; i<w_numbodies ; i++)
	{
		if (w_bodies[i].sim)
			pt_water_destroy (w_bodies[i].sim);
		if (rpt.backend && w_bodies[i].wave_texture >= 0)
			rpt.backend->texture_destroy (rpt.backend, w_bodies[i].wave_texture);
		if (rpt.backend && w_bodies[i].caustic_texture >= 0)
			rpt.backend->texture_destroy (rpt.backend, w_bodies[i].caustic_texture);
	}
	memset (w_bodies, 0, sizeof(w_bodies));
	w_numbodies = 0;
	free (w_tris);
	w_tris = NULL;
	w_numtris = w_maxtris = 0;
	w_numdrops = 0;
}

static float W_Random (void)
{
	return (rand () & 0x7fff) / 32767.0f;
}

/*
===============
W_Spray

Something has hit the surface of a body at x, y: drops fly up, the more and
the higher the harder it was
===============
*/
static void W_Spray (int body, float x, float y, float hard, float spread)
{
	waterdrop_t	*d;
	float		angle, out;
	int			i, count;

	if (hard > 2)
		hard = 2;
	count = (int)(hard * 14) + 1;
	for (i=0 ; i<count && w_numdrops<MAX_WATER_DROPS ; i++)
	{
		d = &w_drops[w_numdrops++];
		angle = W_Random () * 6.2831853f;
		out = sqrt (W_Random ());
		d->origin[0] = x + cos (angle) * out * spread;
		d->origin[1] = y + sin (angle) * out * spread;
		d->origin[2] = w_bodies[body].z + 0.5f;
		// outwards from the middle, and mostly up
		d->velocity[0] = cos (angle) * out * (30 + 50 * hard);
		d->velocity[1] = sin (angle) * out * (30 + 50 * hard);
		d->velocity[2] = (0.35f + 0.65f * W_Random ()) * (90 + 110 * hard);
		d->radius = 0.3f + 0.45f * W_Random ();
		d->body = body;
	}
}

/*
===============
R_WaterDrop

The spray in the air, one drop at a time: false when there are no more
===============
*/
qboolean R_WaterDrop (int index, float *origin, float *radius)
{
	if (index < 0 || index >= w_numdrops)
		return false;
	VectorCopy (w_drops[index].origin, origin);
	*radius = w_drops[index].radius;
	return true;
}

/*
===============
R_WaterAbsorb

How quickly a liquid soaks up each colour of light, from the colour of its
texture: what it does not show, it has absorbed
===============
*/
void R_WaterAbsorb (image_t *image, const char *name, float *absorb)
{
	double	sum[3];
	float	max, strength;
	int		i, count;

	absorb[0] = 0.004f; absorb[1] = 0.002f; absorb[2] = 0.001f;
	if (!image)
		return;

	sum[0] = sum[1] = sum[2] = 0;
	count = image->width * image->height;
	for (i=0 ; i<count ; i++)
	{
		sum[0] += image->pixels[i] & 0xff;
		sum[1] += (image->pixels[i] >> 8) & 0xff;
		sum[2] += (image->pixels[i] >> 16) & 0xff;
	}
	max = sum[0] > sum[1] ? sum[0] : sum[1];
	if (sum[2] > max)
		max = sum[2];
	if (max <= 0)
		return;

	// lava and slime are murky; water is fairly clear
	strength = strstr (name, "lava") ? 0.05f : (strstr (name, "slime") || strstr (name, "sewer") ? 0.012f : 0.005f);
	for (i=0 ; i<3 ; i++)
		absorb[i] = (1.03f - sum[i] / max) * strength;
}

static void W_AddShape (int body, float points[][3], int numpoints, const float *stream)
{
	watertri_t	*t;
	int			i;

	for (i=2 ; i<numpoints ; i++)
	{
		if (w_numtris == w_maxtris)
		{
			w_maxtris = w_maxtris ? w_maxtris * 2 : 1024;
			w_tris = realloc (w_tris, w_maxtris * sizeof(*w_tris));
			if (!w_tris)
				ri.Sys_Error (ERR_FATAL, "W_AddShape: out of memory");
		}
		t = &w_tris[w_numtris++];
		t->body = body;
		t->p[0][0] = points[0][0];   t->p[0][1] = points[0][1];
		t->p[1][0] = points[i-1][0]; t->p[1][1] = points[i-1][1];
		t->p[2][0] = points[i][0];   t->p[2][1] = points[i][1];
		t->stream[0] = stream[0];    t->stream[1] = stream[1];
	}
}

static qboolean W_Near (waterbody_t *a, waterbody_t *b)
{
	return a->image == b->image && fabs (a->z - b->z) <= 1
		&& a->mins[0] <= b->maxs[0] + BODY_JOIN_DISTANCE && a->maxs[0] >= b->mins[0] - BODY_JOIN_DISTANCE
		&& a->mins[1] <= b->maxs[1] + BODY_JOIN_DISTANCE && a->maxs[1] >= b->mins[1] - BODY_JOIN_DISTANCE;
}

/*
===============
R_WaterBody

The body a level liquid face at height z belongs to, made if need be.
Returns -1 if there is no room for another.
===============
*/
int R_WaterBody (image_t *image, const char *name, float z, float points[][3], int numpoints, const float *stream)
{
	waterbody_t	*b;
	float		mins[2], maxs[2];
	int			i, j;

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -999999;
	for (i=0 ; i<numpoints ; i++)
	{
		for (j=0 ; j<2 ; j++)
		{
			if (points[i][j] < mins[j])
				mins[j] = points[i][j];
			if (points[i][j] > maxs[j])
				maxs[j] = points[i][j];
		}
	}

	for (i=0, b=w_bodies ; i<w_numbodies ; i++, b++)
	{
		if (b->image != image || fabs (b->z - z) > 1)
			continue;
		if (mins[0] > b->maxs[0] + BODY_JOIN_DISTANCE || maxs[0] < b->mins[0] - BODY_JOIN_DISTANCE
			|| mins[1] > b->maxs[1] + BODY_JOIN_DISTANCE || maxs[1] < b->mins[1] - BODY_JOIN_DISTANCE)
			continue;
		for (j=0 ; j<2 ; j++)
		{
			if (mins[j] < b->mins[j])
				b->mins[j] = mins[j];
			if (maxs[j] > b->maxs[j])
				b->maxs[j] = maxs[j];
		}
		W_AddShape (i, points, numpoints, stream);
		return i;
	}

	if (w_numbodies == MAX_WATER_BODIES)
		return -1;
	W_AddShape (w_numbodies, points, numpoints, stream);
	b = &w_bodies[w_numbodies];
	memset (b, 0, sizeof(*b));
	b->image = image;
	b->z = z;
	b->mins[0] = mins[0]; b->mins[1] = mins[1];
	b->maxs[0] = maxs[0]; b->maxs[1] = maxs[1];
	b->parent = w_numbodies;
	b->lava = strstr (name, "lava") != NULL;
	b->clear = !b->lava && !strstr (name, "slime");
	b->wave_texture = b->caustic_texture = -1;
	R_WaterAbsorb (image, name, b->absorb);
	return w_numbodies++;
}

void R_WaterSetMaterial (int body, int material)
{
	waterbody_t	*b;
	int			i;

	if (body < 0 || body >= w_numbodies)
		return;
	b = &w_bodies[body];
	for (i=0 ; i<b->nummaterials ; i++)
		if (b->materials[i] == material)
			return;
	if (b->nummaterials < MAX_BODY_MATERIALS)
		b->materials[b->nummaterials++] = material;
}

/*
===============
W_Sound

Tells each simulation how deep its liquid is: whatever of the map faces up
from under its surface is its bed
===============
*/
static void W_Sound (void)
{
	waterbody_t	*b;
	const float	*positions, *p;
	float		tri[3][3], lo[3], hi[3], nz;
	int			i, j, k, count;

	count = R_WorldTriangles (&positions);
	for (i=0, p=positions ; i<count ; i++, p+=9)
	{
		// counter clockwise from the front: this is which way it faces
		nz = (p[3] - p[0]) * (p[7] - p[1]) - (p[4] - p[1]) * (p[6] - p[0]);
		if (nz <= 0)
			continue;
		for (k=0 ; k<3 ; k++)
		{
			lo[k] = p[k] < p[3+k] ? (p[k] < p[6+k] ? p[k] : p[6+k]) : (p[3+k] < p[6+k] ? p[3+k] : p[6+k]);
			hi[k] = p[k] > p[3+k] ? (p[k] > p[6+k] ? p[k] : p[6+k]) : (p[3+k] > p[6+k] ? p[3+k] : p[6+k]);
		}
		for (j=0, b=w_bodies ; j<w_numbodies ; j++, b++)
		{
			if (!b->sim || lo[2] > b->z - 0.5f || hi[0] < b->mins[0] || lo[0] > b->maxs[0]
				|| hi[1] < b->mins[1] || lo[1] > b->maxs[1])
				continue;
			for (k=0 ; k<3 ; k++)
			{
				tri[k][0] = p[k*3];
				tri[k][1] = p[k*3+1];
				tri[k][2] = p[k*3+2] - b->z;
			}
			pt_water_bed (b->sim, tri[0], tri[1], tri[2]);
		}
	}
}

/*
===============
R_WaterFinish

Once every face has been seen and the bodies have their full extent: start
their simulations and point their materials at the pictures
===============
*/
void R_WaterFinish (void)
{
	waterbody_t		*b, *root;
	pt_material_t	*mat;
	pt_texture_t	tex;
	qboolean		changed;
	int				i, j, k, count;

	// faces come in no useful order, so bodies that started apart may
	// have grown into each other
	do
	{
		changed = false;
		for (i=0 ; i<w_numbodies ; i++)
		{
			if (w_bodies[i].parent != i)
				continue;
			for (j=i+1 ; j<w_numbodies ; j++)
			{
				if (w_bodies[j].parent != j || !W_Near (&w_bodies[i], &w_bodies[j]))
					continue;
				w_bodies[j].parent = i;
				if (w_bodies[j].mins[0] < w_bodies[i].mins[0]) w_bodies[i].mins[0] = w_bodies[j].mins[0];
				if (w_bodies[j].mins[1] < w_bodies[i].mins[1]) w_bodies[i].mins[1] = w_bodies[j].mins[1];
				if (w_bodies[j].maxs[0] > w_bodies[i].maxs[0]) w_bodies[i].maxs[0] = w_bodies[j].maxs[0];
				if (w_bodies[j].maxs[1] > w_bodies[i].maxs[1]) w_bodies[i].maxs[1] = w_bodies[j].maxs[1];
				changed = true;
			}
		}
	} while (changed);

	count = 0;
	for (i=0, b=w_bodies ; i<w_numbodies ; i++, b++)
	{
		if (b->parent != i)
		{	// part of another: w_bodies[b->parent] may itself have been joined since
			while (w_bodies[b->parent].parent != b->parent)
				b->parent = w_bodies[b->parent].parent;
			continue;
		}
		b->sim = pt_water_create (b->mins[0], b->mins[1], b->maxs[0], b->maxs[1], r_watercell, 256);
		if (b->sim)
			count++;
	}

	for (i=0 ; i<w_numtris ; i++)
	{
		root = &w_bodies[w_bodies[w_tris[i].body].parent];
		if (root->sim)
		{
			pt_water_cover (root->sim, w_tris[i].p[0], w_tris[i].p[1], w_tris[i].p[2]);
			if (w_tris[i].stream[0] || w_tris[i].stream[1])
				pt_water_current (root->sim, w_tris[i].p[0], w_tris[i].p[1], w_tris[i].p[2],
					w_tris[i].stream[0] * STREAM_SPEED, w_tris[i].stream[1] * STREAM_SPEED);
		}
	}
	W_Sound ();
	free (w_tris);
	w_tris = NULL;
	w_numtris = w_maxtris = 0;

	for (i=0, b=w_bodies ; i<w_numbodies ; i++, b++)
	{
		if (!b->sim)
			continue;
		tex.width = pt_water_width (b->sim);
		tex.height = pt_water_height (b->sim);
		tex.pixels = pt_water_waves (b->sim, r_waterwaves);
		b->wave_texture = rpt.backend->texture_create (rpt.backend, &tex);
		tex.pixels = pt_water_caustics (b->sim, b->lava ? 0 : r_watercaustics);
		b->caustic_texture = rpt.backend->texture_create (rpt.backend, &tex);
	}

	// every part of a body shows the one simulation
	for (i=0, b=w_bodies ; i<w_numbodies ; i++, b++)
	{
		root = &w_bodies[b->parent];
		if (!root->sim)
			continue;
		for (k=0 ; k<b->nummaterials ; k++)
		{
			mat = R_WorldMaterialPtr (b->materials[k]);
			mat->wave_map = root->wave_texture + 1;
			mat->caustic_map = root->caustic_texture + 1;
			mat->wave_rect[0] = root->mins[0];
			mat->wave_rect[1] = root->mins[1];
			mat->wave_rect[2] = 1.0f / (pt_water_width (root->sim) * pt_water_cell (root->sim));
			mat->wave_rect[3] = 1.0f / (pt_water_height (root->sim) * pt_water_cell (root->sim));

			// Simulated water is clear: what shows is what it reflects and
			// what lies under it, coloured by the depth looked through, with
			// none of the map's picture painted on top. Where the map made
			// the water a light it still lights the room, unseen.
			if (b->clear)
				mat->alpha = 0;
		}
	}

	if (count)
		ri.Con_Printf (PRINT_ALL, "%d simulated bodies of liquid\n", count);
}

static qboolean W_Over (waterbody_t *b, const float *p)
{
	return p[0] >= b->mins[0] && p[0] <= b->maxs[0] && p[1] >= b->mins[1] && p[1] <= b->maxs[1];
}

/*
===============
R_WaterFrame

Pokes each body where things touch its surface, steps it and hands the
backend the new pictures. Called before the scene is built, while
R_EntityMoved still has last frame to compare with.
===============
*/
void R_WaterFrame (refdef_t *fd)
{
	waterbody_t	*b;
	entity_t	*e;
	particle_t	*p;
	vec3_t		eye;
	vec3_t		by;
	waterdrop_t	*drop;
	const float	*sprays;
	float		dt, speed, amount, moved, feet, fell, d[2];
	int			i, j, splashes;

	if (!w_numbodies)
		return;

	dt = fd->time - w_lasttime;
	if (dt < 0 || dt > 0.25f)
		dt = 0;		// a new map, a load or a long pause: nothing sensible to do
	if (!dt || r_waterfoam <= 0)
		w_numdrops = 0;
	VectorCopy (fd->vieworg, eye);
	feet = eye[2] - 46;
	r_waterreach = 0;

	for (i=0, b=w_bodies ; i<w_numbodies ; i++, b++)
	{
		if (!b->sim)
			continue;

		pt_water_foaming (b->sim, b->lava ? 0 : r_waterfoam, FOAM_LIFE);

		if (dt > 0)
		{
			// the player, jumping or falling in
			fell = (w_lasteye[2] - eye[2]) / dt;
			if (W_Over (b, eye) && feet < b->z && w_lasteye[2] - 46 >= b->z && fell > 120 && fell < 4000)
			{
				amount = fell * 0.004f;
				pt_water_disturb (b->sim, eye[0], eye[1], 26, amount > 3 ? 3 : amount);
				pt_water_churn (b->sim, eye[0], eye[1], 34, amount > 1.2f ? 1.2f : amount);
				if (r_waterfoam > 0 && !b->lava)
					W_Spray (i, eye[0], eye[1], amount, 14);
			}

			// the player, wading or swimming at the surface
			if (W_Over (b, eye) && feet < b->z && eye[2] + 10 > b->z)
			{
				d[0] = eye[0] - w_lasteye[0];
				d[1] = eye[1] - w_lasteye[1];
				speed = sqrt (d[0]*d[0] + d[1]*d[1]) / dt;
				if (speed < 1000)	// not a teleport
					pt_water_move (b->sim, eye[0], eye[1], 20, d[0] / dt, d[1] / dt, BODY_AMOUNT, dt);
				// and it splashes as it goes; nobody stands quite still in water
				amount = 0.2f + speed * 0.006f;
				if (amount > 2)
					amount = 2;
				pt_water_disturb (b->sim, eye[0], eye[1], 18, amount * dt * 16);
				if (speed > CHURN_SPEED && speed < 1000)
					pt_water_churn (b->sim, eye[0], eye[1], 18, (speed - CHURN_SPEED) * 0.05f * dt);
			}

			// monsters, items, gibs, projectiles: whatever is at the surface and moving
			for (j=0, e=fd->entities ; j<fd->num_entities ; j++, e++)
			{
				if (!e->model || (e->flags & (RF_BEAM|RF_WEAPONMODEL|RF_VIEWERMODEL)))
					continue;
				if (fabs (e->origin[2] - b->z) > 28 || !W_Over (b, e->origin))
					continue;
				moved = R_EntityMoved (j, e, by);
				if (moved < 0.25f || moved > 1000 * dt)
					continue;
				pt_water_move (b->sim, e->origin[0], e->origin[1], 16, by[0] / dt, by[1] / dt, BODY_AMOUNT, dt);
				speed = sqrt (by[0]*by[0] + by[1]*by[1]) / dt;
				if (speed > CHURN_SPEED)
					pt_water_churn (b->sim, e->origin[0], e->origin[1], 16, (speed - CHURN_SPEED) * 0.05f * dt);
				// what falls in or climbs out makes a splash of its own
				amount = fabs (by[2]) * 0.15f;
				if (amount > 3)
					amount = 3;
				if (amount > 0.05f)
					pt_water_disturb (b->sim, e->origin[0], e->origin[1], 14, amount);
				if (amount > 0.3f)
				{
					pt_water_churn (b->sim, e->origin[0], e->origin[1], 22, amount * 0.4f);
					if (r_waterfoam > 0 && !b->lava && fabs (by[2]) / dt > 120)
						W_Spray (i, e->origin[0], e->origin[1], amount * 0.5f, 10);
				}
			}

			// splashes and bubbles show up as particles at the surface
			splashes = 0;
			for (j=0, p=fd->particles ; j<fd->num_particles && splashes<48 ; j++, p++)
			{
				if (fabs (p->origin[2] - b->z) > 4 || !W_Over (b, p->origin))
					continue;
				pt_water_disturb (b->sim, p->origin[0], p->origin[1], 6, 0.1f);
				pt_water_churn (b->sim, p->origin[0], p->origin[1], 7, 0.06f);
				splashes++;
			}

			// nothing in a building is ever quite still: drips, draughts and
			// pumps keep a little life in the surface, more of it the larger
			// the body
			amount = (b->maxs[0] - b->mins[0]) * (b->maxs[1] - b->mins[1]) * (1.0f / (256*256)) * 1.5f * dt;
			if (amount > 0.5f)
				amount = 0.5f;
			if ((rand () & 0x7fff) < amount * 0x7fff)
				pt_water_disturb (b->sim,
					b->mins[0] + (b->maxs[0] - b->mins[0]) * (rand () & 0x7fff) / 32767.0f,
					b->mins[1] + (b->maxs[1] - b->mins[1]) * (rand () & 0x7fff) / 32767.0f,
					10 + (rand () & 7), b->lava ? 0.5f : 0.25f);

			// heavy liquids move slowly and settle fast
			pt_water_step (b->sim, dt, b->lava ? LAVA_GRAVITY : WATER_GRAVITY, r_waterdamping * (b->lava ? 0.9f : 0.45f));

			// where a wave broke
			splashes = pt_water_spray (b->sim, &sprays);
			for (j=0 ; j<splashes ; j++)
				W_Spray (i, sprays[j*3], sprays[j*3+1], sprays[j*3+2], pt_water_cell (b->sim) * 0.5f);
		}

		rpt.backend->texture_update (rpt.backend, b->wave_texture, pt_water_waves (b->sim, r_waterwaves));
		if (pt_water_reach (b->sim) > r_waterreach)
			r_waterreach = pt_water_reach (b->sim);
		rpt.backend->texture_update (rpt.backend, b->caustic_texture,
			pt_water_caustics (b->sim, b->lava ? 0 : r_watercaustics));
	}

	// the spray in the air rises, falls and is gone where it lands, leaving a ring
	for (i=0, drop=w_drops ; i<w_numdrops ; )
	{
		drop->velocity[2] -= DROP_GRAVITY * dt;
		VectorMA (drop->origin, dt, drop->velocity, drop->origin);
		b = &w_bodies[drop->body];
		if (drop->origin[2] < b->z)
		{
			if (b->sim)
				pt_water_disturb (b->sim, drop->origin[0], drop->origin[1], 5, 0.04f);
			*drop = w_drops[--w_numdrops];
			continue;
		}
		i++;
		drop++;
	}

	w_lasttime = fd->time;
	VectorCopy (eye, w_lasteye);
}

/*
===============
R_WaterEyeUnder

The game says the eye is under water when it is below the level the map
gives the water. A simulated surface stands above that in places and below
it in others, and what is drawn must agree with it: near the surface the
waves have the say. strength is what the heights are drawn times (pt_waves).
===============
*/
qboolean R_WaterEyeUnder (const float *eye, float strength, qboolean under)
{
	waterbody_t	*b;
	float		height;
	int			i, covered;

	if (strength <= 0)
		return under;
	for (i=0, b=w_bodies ; i<w_numbodies ; i++, b++)
	{
		if (!b->sim || !W_Over (b, eye) || fabs (eye[2] - b->z) > PT_WATER_HEIGHT_MAX * strength + 1)
			continue;
		height = pt_water_height_at (b->sim, eye[0], eye[1], r_waterwaves, &covered) * strength;
		if (covered && fabs (eye[2] - b->z) <= fabs (height) + 1)
			return eye[2] < b->z + height;
	}
	return under;
}

/*
===============
R_WaterAround

What the liquid the eye is in does to light. Used when the game says the
view is under water.
===============
*/
void R_WaterAround (const float *eye, float *absorb)
{
	waterbody_t	*b, *best;
	int			i;

	best = NULL;
	for (i=0, b=w_bodies ; i<w_numbodies ; i++, b++)
		if (W_Over (b, eye) && eye[2] < b->z + 16 && (!best || b->z < best->z))
			best = b;

	if (best)
		VectorCopy (best->absorb, absorb);
	else
	{	// not one of ours: ordinary water
		absorb[0] = 0.004f;
		absorb[1] = 0.0015f;
		absorb[2] = 0.001f;
	}
}
