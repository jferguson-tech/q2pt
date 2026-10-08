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
// rpt_scene.c -- everything that moves, rebuilt each frame as world space
// triangles for the path tracer: entities, beams, particles, dynamic lights

#include "rpt_local.h"

static float			*s_positions, *s_uvs, *s_normals;
static float			*s_prev;			// 9 per triangle: where it was last frame

// Where each entity was last frame, so the tracer can follow what moves.
// Entities carry no identity from one frame to the next; they are matched
// by their place in the list, or failing that by the nearest of the same model.
typedef struct
{
	struct model_s	*model;		// NULL if not something to follow
	vec3_t			origin;
	vec3_t			axis[3];
	int				frame, oldframe;	// a model's pose: between which two of its frames,
	float			backlerp;			// and how far back toward the older
} entstate_t;

static entstate_t		s_was[MAX_ENTITIES], s_now[MAX_ENTITIES];
static int				s_numwas;
static uint32_t			*s_trimaterials;
static int				s_numtris, s_maxtris;

static pt_material_t	*s_materials;
static int				s_nummaterials, s_maxmaterials;

static pt_point_light_t	s_lights[MAX_DLIGHTS];

// world material index -> this frame's material index
static int				s_worldremap[MAX_MAP_TEXINFO];
static int				s_worldremapframe[MAX_MAP_TEXINFO];
static int				s_framecount;

static float			s_linear[256];		// palette byte to linear light

//=============================================================================

// mat must have been memset before it was filled in
static int S_FindMaterial (pt_material_t *mat)
{
	int		i;

	for (i=s_nummaterials-1 ; i>=0 ; i--)
		if (!memcmp (&s_materials[i], mat, sizeof(*mat)))
			return i;

	if (s_nummaterials == s_maxmaterials)
	{
		s_maxmaterials = s_maxmaterials ? s_maxmaterials * 2 : 256;
		s_materials = realloc (s_materials, s_maxmaterials * sizeof(*s_materials));
	}
	s_materials[s_nummaterials] = *mat;
	return s_nummaterials++;
}

static int S_Material (int texture, float r, float g, float b, float alpha, unsigned flags)
{
	pt_material_t	mat;

	memset (&mat, 0, sizeof(mat));
	mat.texture = texture;
	mat.emission[0] = r;
	mat.emission[1] = g;
	mat.emission[2] = b;
	mat.alpha = alpha;
	mat.flags = flags;
	mat.roughness = 0.7f;
	mat.normal_texture = -1;
	mat.anim_next = -1;
	return S_FindMaterial (&mat);
}

// a material for a model's skin, made of whatever the skin's name suggests
static int S_ImageMaterial (image_t *image, float r, float g, float b, float alpha, unsigned flags)
{
	pt_material_t	mat;
	matinfo_t		info;

	memset (&mat, 0, sizeof(mat));
	mat.texture = R_ImageTexture (image);
	mat.emission[0] = r;
	mat.emission[1] = g;
	mat.emission[2] = b;
	mat.alpha = alpha;
	mat.flags = flags;
	mat.roughness = 0.7f;
	mat.normal_texture = -1;
	mat.anim_next = -1;
	if (image)
	{
		R_MaterialInfo (image->name, &info);
		mat.roughness = info.roughness;
		mat.metallic = info.metallic_unread;
		mat.normal_texture = R_ImageNormalTexture (image);
		if (mat.normal_texture >= 0 && image->normal_metal)
			mat.flags |= PT_MAT_METAL_TEXTURE | PT_MAT_METAL_PAINTED;
	}
	return S_FindMaterial (&mat);
}

// where the triangle just added was last frame
static void S_Prev (const float *a, const float *b, const float *c)
{
	float	*p = s_prev + (s_numtris - 1) * 9;

	p[0] = a[0]; p[1] = a[1]; p[2] = a[2];
	p[3] = b[0]; p[4] = b[1]; p[5] = b[2];
	p[6] = c[0]; p[7] = c[1]; p[8] = c[2];
}

/*
=============
S_Before

Notes where entity number index is now and returns where it was last frame,
or NULL if it was not there to be found
=============
*/
static entstate_t *S_Before (int index, entity_t *e, vec3_t origin, vec3_t axis[3])
{
	entstate_t	*now, *was, *best;
	vec3_t		d;
	float		dist, bestdist;
	int			i;

	now = &s_now[index];
	now->model = e->model;
	VectorCopy (origin, now->origin);
	memcpy (now->axis, axis, sizeof(now->axis));

	was = &s_was[index];
	if (index < s_numwas && was->model == e->model)
	{
		VectorSubtract (was->origin, origin, d);
		if (DotProduct (d, d) < 256 * 256)
			return was;
	}

	best = NULL;
	bestdist = 128 * 128;
	for (i=0, was=s_was ; i<s_numwas ; i++, was++)
	{
		if (was->model != e->model)
			continue;
		VectorSubtract (was->origin, origin, d);
		dist = DotProduct (d, d);
		if (dist < bestdist)
		{
			bestdist = dist;
			best = was;
		}
	}
	return best;
}

/*
=============
R_EntityMoved

How far entity number index has moved since last frame, or 0 if it was not
there then. Only meaningful before R_BuildScene has run for this frame.
=============
*/
float R_EntityMoved (int index, entity_t *e)
{
	vec3_t	d;

	if (index < 0 || index >= s_numwas || s_was[index].model != e->model)
		return 0;
	VectorSubtract (e->origin, s_was[index].origin, d);
	return VectorLength (d);
}

// the normals of the triangle just added, for smooth shading
static void S_Normals (const float *a, const float *b, const float *c)
{
	float	*n = s_normals + (s_numtris - 1) * 9;

	n[0] = a[0]; n[1] = a[1]; n[2] = a[2];
	n[3] = b[0]; n[4] = b[1]; n[5] = b[2];
	n[6] = c[0]; n[7] = c[1]; n[8] = c[2];
}

static void S_Triangle (const float *a, const float *b, const float *c,
	float as, float at, float bs, float bt, float cs, float ct, int material)
{
	float	*p, *uv;
	vec3_t	d1, d2, normal;

	if (s_numtris == s_maxtris)
	{
		s_maxtris = s_maxtris ? s_maxtris * 2 : 8192;
		s_positions = realloc (s_positions, s_maxtris * 9 * sizeof(float));
		s_normals = realloc (s_normals, s_maxtris * 9 * sizeof(float));
		s_prev = realloc (s_prev, s_maxtris * 9 * sizeof(float));
		s_uvs = realloc (s_uvs, s_maxtris * 6 * sizeof(float));
		s_trimaterials = realloc (s_trimaterials, s_maxtris * sizeof(uint32_t));
	}

	p = s_positions + s_numtris * 9;
	p[0] = a[0]; p[1] = a[1]; p[2] = a[2];
	p[3] = b[0]; p[4] = b[1]; p[5] = b[2];
	p[6] = c[0]; p[7] = c[1]; p[8] = c[2];

	uv = s_uvs + s_numtris * 6;
	uv[0] = as; uv[1] = at;
	uv[2] = bs; uv[3] = bt;
	uv[4] = cs; uv[5] = ct;

	s_trimaterials[s_numtris++] = material;

	// flat until S_Normals says otherwise
	VectorSubtract (b, a, d1);
	VectorSubtract (c, a, d2);
	CrossProduct (d1, d2, normal);
	VectorNormalize (normal);
	S_Normals (normal, normal, normal);
	S_Prev (a, b, c);		// not known to have moved
}

/*
=============
S_EntityAxis

The rotation ref_gl gives an entity: yaw about Z, then pitch about Y, then
roll about X, with the roll the other way round for alias models.
axis[i] is where the model's i axis ends up.
=============
*/
static void S_EntityAxis (entity_t *e, qboolean alias, vec3_t axis[3])
{
	float	sy, cy, sp, cp, sr, cr, roll;

	roll = alias ? -e->angles[2] : e->angles[2];
	sy = sin (e->angles[1] * (M_PI / 180));
	cy = cos (e->angles[1] * (M_PI / 180));
	sp = sin (e->angles[0] * (M_PI / 180));
	cp = cos (e->angles[0] * (M_PI / 180));
	sr = sin (roll * (M_PI / 180));
	cr = cos (roll * (M_PI / 180));

	axis[0][0] = cy * cp;
	axis[0][1] = sy * cp;
	axis[0][2] = -sp;
	axis[1][0] = cy * sp * sr - sy * cr;
	axis[1][1] = sy * sp * sr + cy * cr;
	axis[1][2] = cp * sr;
	axis[2][0] = cy * sp * cr + sy * sr;
	axis[2][1] = sy * sp * cr - cy * sr;
	axis[2][2] = cp * cr;
}

static void S_Transform (const float *in, vec3_t origin, vec3_t axis[3], float *out)
{
	out[0] = origin[0] + in[0] * axis[0][0] + in[1] * axis[1][0] + in[2] * axis[2][0];
	out[1] = origin[1] + in[0] * axis[0][1] + in[1] * axis[1][1] + in[2] * axis[2][1];
	out[2] = origin[2] + in[0] * axis[0][2] + in[1] * axis[1][2] + in[2] * axis[2][2];
}

//=============================================================================

/*
=============
S_AddInline

A door, lift or other piece of the map
=============
*/
static void S_AddInline (entity_t *e, model_t *mod, int index)
{
	float			*positions, *uvs, a[3], b[3], c[3], pa[3], pb[3], pc[3];
	entstate_t		*before;
	uint32_t		*materials;
	pt_material_t	mat;
	image_t			*image;
	vec3_t			axis[3];
	int				i, count, m;

	count = R_InlineModel (mod->inlinenum, &positions, &uvs, &materials);
	if (!count)
		return;

	S_EntityAxis (e, false, axis);
	before = S_Before (index, e, e->origin, axis);

	for (i=0 ; i<count ; i++)
	{
		m = materials[i];
		if (s_worldremapframe[m] != s_framecount)
		{
			R_WorldMaterial (m, &mat, &image);
			if (mat.flags & PT_MAT_SKY)
				s_worldremap[m] = -1;
			else
			{
				mat.texture = R_ImageTexture (image);
				mat.normal_texture = mat.normal_texture >= 0 ? R_ImageNormalTexture (image) : -1;
				mat.anim_next = -1;
				if (e->flags & RF_TRANSLUCENT)
					mat.alpha *= e->alpha;
				s_worldremap[m] = S_FindMaterial (&mat);
			}
			s_worldremapframe[m] = s_framecount;
		}
		if (s_worldremap[m] < 0)
			continue;

		S_Transform (positions + i * 9, e->origin, axis, a);
		S_Transform (positions + i * 9 + 3, e->origin, axis, b);
		S_Transform (positions + i * 9 + 6, e->origin, axis, c);
		S_Triangle (a, b, c, uvs[i*6], uvs[i*6+1], uvs[i*6+2], uvs[i*6+3], uvs[i*6+4], uvs[i*6+5], s_worldremap[m]);
		if (before)
		{
			S_Transform (positions + i * 9, before->origin, before->axis, pa);
			S_Transform (positions + i * 9 + 3, before->origin, before->axis, pb);
			S_Transform (positions + i * 9 + 6, before->origin, before->axis, pc);
			S_Prev (pa, pb, pc);
		}
	}
}

/*
=============
S_AddAlias

A model, posed between two of its frames
=============
*/
static void S_AddAlias (entity_t *e, model_t *mod, int index)
{
	static float	verts[MAX_VERTS][3], normals[MAX_VERTS][3], prevverts[MAX_VERTS][3];
	entstate_t		*before;
	qboolean		shell;
	dmdl_t			*hdr;
	daliasframe_t	*frame, *oldframe, *wasframe, *wasoldframe;
	dtrivertx_t		*v, *ov;
	dtriangle_t		*tri;
	dstvert_t		*st;
	image_t			*skin;
	vec3_t			axis[3], origin, local;
	float			frontlerp, backlerp, alpha, emit[3], sscale, tscale;
	unsigned		flags;
	int				i, j, material, framenum, oldframenum;

	hdr = mod->data;

	framenum = e->frame;
	oldframenum = e->oldframe;
	if (framenum < 0 || framenum >= hdr->num_frames)
		framenum = 0;
	if (oldframenum < 0 || oldframenum >= hdr->num_frames)
		oldframenum = 0;

	backlerp = e->backlerp;
	frontlerp = 1.0 - backlerp;
	frame = (daliasframe_t *)((byte *)hdr + hdr->ofs_frames + framenum * hdr->framesize);
	oldframe = (daliasframe_t *)((byte *)hdr + hdr->ofs_frames + oldframenum * hdr->framesize);
	v = frame->verts;
	ov = oldframe->verts;

	S_EntityAxis (e, true, axis);
	for (j=0 ; j<3 ; j++)
		origin[j] = e->origin[j] + backlerp * (e->oldorigin[j] - e->origin[j]);
	before = S_Before (index, e, origin, axis);
	s_now[index].frame = framenum;
	s_now[index].oldframe = oldframenum;
	s_now[index].backlerp = backlerp;

	// last frame: where the entity was then, in the pose it had then. A
	// weapon in hand moves by its pose alone, and without it what was seen
	// of it before would be looked for in the wrong place.
	wasframe = wasoldframe = NULL;
	if (before && before->frame >= 0 && before->frame < hdr->num_frames
		&& before->oldframe >= 0 && before->oldframe < hdr->num_frames)
	{
		wasframe = (daliasframe_t *)((byte *)hdr + hdr->ofs_frames + before->frame * hdr->framesize);
		wasoldframe = (daliasframe_t *)((byte *)hdr + hdr->ofs_frames + before->oldframe * hdr->framesize);
	}

	for (i=0 ; i<hdr->num_xyz ; i++)
	{
		for (j=0 ; j<3 ; j++)
			local[j] = (ov[i].v[j] * oldframe->scale[j] + oldframe->translate[j]) * backlerp
				+ (v[i].v[j] * frame->scale[j] + frame->translate[j]) * frontlerp;
		S_Transform (local, origin, axis, verts[i]);
		if (wasframe)
		{
			for (j=0 ; j<3 ; j++)
				local[j] = (wasoldframe->verts[i].v[j] * wasoldframe->scale[j] + wasoldframe->translate[j]) * before->backlerp
					+ (wasframe->verts[i].v[j] * wasframe->scale[j] + wasframe->translate[j]) * (1.0 - before->backlerp);
			S_Transform (local, before->origin, before->axis, prevverts[i]);
		}
		else if (before)
			S_Transform (local, before->origin, before->axis, prevverts[i]);
		else
			VectorCopy (verts[i], prevverts[i]);
	}

	// select skin
	skin = e->skin;		// custom player skin
	if (!skin)
	{
		if (e->skinnum >= 0 && e->skinnum < mod->numskins)
			skin = mod->skins[e->skinnum];
		if (!skin && mod->numskins > 0)
			skin = mod->skins[0];
	}

	alpha = (e->flags & RF_TRANSLUCENT) ? e->alpha : 1;
	flags = 0;
	emit[0] = emit[1] = emit[2] = 0;
	if (e->flags & RF_VIEWERMODEL)
		flags |= PT_MAT_CAMERA_INVISIBLE;	// the player's own body: shadows, but not in the way
	if (e->flags & RF_WEAPONMODEL)
		flags |= PT_MAT_HELD;				// the weapon in hand turns with the eye: no motion blur of its own
	shell = (e->flags & (RF_SHELL_RED|RF_SHELL_GREEN|RF_SHELL_BLUE|RF_SHELL_DOUBLE|RF_SHELL_HALF_DAM)) != 0;
	if (shell)
	{
		// A power-up shell: the client sends the model a second time for it. It
		// is drawn puffed out, see-through and glowing in the shell's colour.
		if (e->flags & (RF_SHELL_RED|RF_SHELL_DOUBLE))
			emit[0] = 1;
		if (e->flags & (RF_SHELL_GREEN|RF_SHELL_DOUBLE|RF_SHELL_HALF_DAM))
			emit[1] = 1;
		if (e->flags & (RF_SHELL_BLUE|RF_SHELL_HALF_DAM))
			emit[2] = 1;
	}
	else if (e->flags & RF_FULLBRIGHT)
	{
		emit[0] = emit[1] = emit[2] = 1;
		flags |= PT_MAT_EMIT_TEXTURE;
	}
	if (shell)
		material = S_Material (-1, emit[0], emit[1], emit[2], alpha < 1 ? alpha : 0.3f, PT_MAT_BLACK);
	else
		material = S_ImageMaterial (skin, emit[0], emit[1], emit[2], alpha, flags);

	// vertex normals: each vertex takes the area weighted average of its triangles
	memset (normals, 0, hdr->num_xyz * sizeof(normals[0]));
	tri = (dtriangle_t *)((byte *)hdr + hdr->ofs_tris);
	for (i=0 ; i<hdr->num_tris ; i++, tri++)
	{
		vec3_t	d1, d2, facenormal;

		VectorSubtract (verts[tri->index_xyz[2]], verts[tri->index_xyz[0]], d1);
		VectorSubtract (verts[tri->index_xyz[1]], verts[tri->index_xyz[0]], d2);
		CrossProduct (d1, d2, facenormal);
		for (j=0 ; j<3 ; j++)
			VectorAdd (normals[tri->index_xyz[j]], facenormal, normals[tri->index_xyz[j]]);
	}
	for (i=0 ; i<hdr->num_xyz ; i++)
	{
		VectorNormalize (normals[i]);
		if (shell)
		{
			VectorMA (verts[i], 3, normals[i], verts[i]);
			VectorMA (prevverts[i], 3, normals[i], prevverts[i]);
		}
	}

	sscale = 1.0 / hdr->skinwidth;
	tscale = 1.0 / hdr->skinheight;
	st = (dstvert_t *)((byte *)hdr + hdr->ofs_st);
	tri = (dtriangle_t *)((byte *)hdr + hdr->ofs_tris);

	// the file winds its triangles clockwise
	for (i=0 ; i<hdr->num_tris ; i++, tri++)
	{
		S_Triangle (verts[tri->index_xyz[0]], verts[tri->index_xyz[2]], verts[tri->index_xyz[1]],
			(st[tri->index_st[0]].s + 0.5) * sscale, (st[tri->index_st[0]].t + 0.5) * tscale,
			(st[tri->index_st[2]].s + 0.5) * sscale, (st[tri->index_st[2]].t + 0.5) * tscale,
			(st[tri->index_st[1]].s + 0.5) * sscale, (st[tri->index_st[1]].t + 0.5) * tscale,
			material);
		S_Normals (normals[tri->index_xyz[0]], normals[tri->index_xyz[2]], normals[tri->index_xyz[1]]);
		S_Prev (prevverts[tri->index_xyz[0]], prevverts[tri->index_xyz[2]], prevverts[tri->index_xyz[1]]);
	}
}

/*
=============
S_AddSprite

A picture that always faces the viewer, lit by nothing but itself
=============
*/
static void S_AddSprite (entity_t *e, model_t *mod, vec3_t right, vec3_t up)
{
	dsprite_t	*spr;
	dsprframe_t	*frame;
	image_t		*image;
	vec3_t		point[4];
	int			framenum, material, i;

	spr = mod->data;
	framenum = e->frame % spr->numframes;
	if (framenum < 0)
		framenum += spr->numframes;
	frame = &spr->frames[framenum];
	image = mod->skins[framenum];
	if (!image)
		return;

	for (i=0 ; i<3 ; i++)
	{
		point[0][i] = e->origin[i] - frame->origin_y * up[i] - frame->origin_x * right[i];
		point[1][i] = e->origin[i] + (frame->height - frame->origin_y) * up[i] - frame->origin_x * right[i];
		point[2][i] = e->origin[i] + (frame->height - frame->origin_y) * up[i] + (frame->width - frame->origin_x) * right[i];
		point[3][i] = e->origin[i] - frame->origin_y * up[i] + (frame->width - frame->origin_x) * right[i];
	}

	material = S_Material (R_ImageTexture (image), 1, 1, 1,
		(e->flags & RF_TRANSLUCENT) ? e->alpha : 1, PT_MAT_ALPHA_TEST|PT_MAT_EMIT_TEXTURE);

	// counter clockwise seen from the viewer
	S_Triangle (point[0], point[3], point[2], 0, 1, 1, 1, 1, 0, material);
	S_Triangle (point[0], point[2], point[1], 0, 1, 1, 0, 0, 0, material);
}

/*
=============
S_AddBeam

A glowing square rod from origin to oldorigin
=============
*/
static void S_AddBeam (entity_t *e)
{
	vec3_t		dir, u, v, corner[2][4];
	float		radius;
	uint32_t	color;
	int			i, j, k, material;

	VectorSubtract (e->oldorigin, e->origin, dir);
	if (VectorNormalize (dir) == 0)
		return;
	PerpendicularVector (u, dir);
	CrossProduct (dir, u, v);

	radius = e->frame * 0.5;
	for (i=0 ; i<4 ; i++)
	{
		float su = (i == 0 || i == 3) ? -radius : radius;
		float sv = (i < 2) ? -radius : radius;

		for (j=0 ; j<3 ; j++)
		{
			corner[0][i][j] = e->origin[j] + u[j] * su + v[j] * sv;
			corner[1][i][j] = e->oldorigin[j] + u[j] * su + v[j] * sv;
		}
	}

	color = d_8to24table[e->skinnum & 0xff];
	material = S_Material (-1, s_linear[color & 0xff] * 2, s_linear[(color >> 8) & 0xff] * 2,
		s_linear[(color >> 16) & 0xff] * 2, e->alpha, PT_MAT_BLACK);

	for (i=0 ; i<4 ; i++)
	{
		k = (i + 1) & 3;
		// both windings: a beam should glow whichever way it is seen
		S_Triangle (corner[0][i], corner[0][k], corner[1][k], 0, 0, 0, 0, 0, 0, material);
		S_Triangle (corner[0][i], corner[1][k], corner[1][i], 0, 0, 0, 0, 0, 0, material);
		S_Triangle (corner[0][i], corner[1][k], corner[0][k], 0, 0, 0, 0, 0, 0, material);
		S_Triangle (corner[0][i], corner[1][i], corner[1][k], 0, 0, 0, 0, 0, 0, material);
	}
}

#define	DOT_SIZE	16

static int	s_dottexture = -1;		// a white disc on nothing, for particles

/*
=============
R_SceneShutdown

The backend is going, and its textures with it
=============
*/
void R_SceneShutdown (void)
{
	s_dottexture = -1;
}

// the picture every particle is cut from: opaque inside a circle, a hole outside
static int S_DotTexture (void)
{
	static uint32_t	pixels[DOT_SIZE * DOT_SIZE];
	pt_texture_t	tex;
	float			dx, dy;
	int				x, y;

	if (s_dottexture >= 0)
		return s_dottexture;

	for (y=0 ; y<DOT_SIZE ; y++)
	{
		for (x=0 ; x<DOT_SIZE ; x++)
		{
			dx = (x + 0.5f) * (2.0f / DOT_SIZE) - 1;
			dy = (y + 0.5f) * (2.0f / DOT_SIZE) - 1;
			// white all over, so that the colour comes from the material alone
			pixels[y * DOT_SIZE + x] = dx * dx + dy * dy <= 1 ? 0xffffffffu : 0x00ffffffu;
		}
	}
	tex.width = tex.height = DOT_SIZE;
	tex.pixels = pixels;
	s_dottexture = rpt.backend->texture_create (rpt.backend, &tex);
	return s_dottexture;
}

/*
=============
S_AddParticles

Each a small glowing disc facing the viewer, the size ref_gl draws them: a
square cut round by its texture
=============
*/
static void S_AddParticles (refdef_t *fd, vec3_t forward, vec3_t right, vec3_t up)
{
	static int	cache[256][4], cacheframe[256][4];
	particle_t	*p;
	vec3_t		corner[4];
	uint32_t	color;
	float		radius;
	int			i, j, level, index, dot;

	if (!fd->num_particles)
		return;
	dot = S_DotTexture ();

	for (i=0, p=fd->particles ; i<fd->num_particles ; i++, p++)
	{
		index = p->color & 0xff;
		level = p->alpha >= 0.75 ? 3 : (p->alpha >= 0.5 ? 2 : (p->alpha >= 0.25 ? 1 : 0));
		if (cacheframe[index][level] != s_framecount)
		{
			color = d_8to24table[index];
			cache[index][level] = S_Material (dot, s_linear[color & 0xff], s_linear[(color >> 8) & 0xff],
				s_linear[(color >> 16) & 0xff], (level + 1) * 0.25f,
				PT_MAT_BLACK | (dot >= 0 ? PT_MAT_ALPHA_TEST : 0));
			cacheframe[index][level] = s_framecount;
		}

		// far ones are drawn larger, as ref_gl does, to keep them from disappearing
		radius = (p->origin[0] - fd->vieworg[0]) * forward[0]
			+ (p->origin[1] - fd->vieworg[1]) * forward[1]
			+ (p->origin[2] - fd->vieworg[2]) * forward[2];
		radius = radius < 20 ? 0.4f : 0.4f + radius * 0.0015f;

		for (j=0 ; j<3 ; j++)
		{
			corner[0][j] = p->origin[j] - right[j] * radius - up[j] * radius;
			corner[1][j] = p->origin[j] + right[j] * radius - up[j] * radius;
			corner[2][j] = p->origin[j] + right[j] * radius + up[j] * radius;
			corner[3][j] = p->origin[j] - right[j] * radius + up[j] * radius;
		}
		S_Triangle (corner[0], corner[1], corner[2], 0, 0, 1, 0, 1, 1, cache[index][level]);
		S_Triangle (corner[0], corner[2], corner[3], 0, 0, 1, 1, 0, 1, cache[index][level]);
	}
}

/*
=============
R_BuildScene
=============
*/
void R_BuildScene (refdef_t *fd, pt_scene_t *scene)
{
	entity_t	*e;
	model_t		*mod;
	vec3_t		forward, right, up;
	int			i, j, numlights;

	if (!s_linear[255])
		for (i=0 ; i<256 ; i++)
			s_linear[i] = pow (i / 255.0, 2.2);

	s_framecount++;
	s_numtris = 0;
	s_nummaterials = 0;

	AngleVectors (fd->viewangles, forward, right, up);

	for (i=0, e=fd->entities ; i<fd->num_entities ; i++, e++)
	{
		if (i < MAX_ENTITIES)
			s_now[i].model = NULL;
		if (e->flags & RF_BEAM)
		{
			S_AddBeam (e);
			continue;
		}

		mod = e->model;
		if (!mod)
			continue;
		// not one of ours: see R_IsModel. Better not drawn than read.
		if (!R_IsModel (mod))
			continue;

		switch (mod->type)
		{
		case mod_inline:
			S_AddInline (e, mod, i);
			break;
		case mod_alias:
			S_AddAlias (e, mod, i);
			break;
		case mod_sprite:
			S_AddSprite (e, mod, right, up);
			break;
		default:
			break;
		}
	}

	S_AddParticles (fd, forward, right, up);

	numlights = 0;
	for (i=0 ; i<fd->num_dlights && numlights<MAX_DLIGHTS ; i++)
	{
		if (fd->dlights[i].intensity <= 0)
			continue;
		for (j=0 ; j<3 ; j++)
		{
			s_lights[numlights].origin[j] = fd->dlights[i].origin[j];
			s_lights[numlights].intensity[j] = fd->dlights[i].color[j] * POINT_LIGHT_INTENSITY (fd->dlights[i].intensity);
		}
		s_lights[numlights].style = 0;
		numlights++;
	}

	memset (scene, 0, sizeof(*scene));
	scene->materials = s_materials;
	scene->num_materials = s_nummaterials;
	scene->positions = s_positions;
	scene->uvs = s_uvs;
	scene->normals = s_normals;
	scene->prev_positions = s_prev;

	s_numwas = fd->num_entities < MAX_ENTITIES ? fd->num_entities : MAX_ENTITIES;
	memcpy (s_was, s_now, s_numwas * sizeof(s_was[0]));
	scene->tri_materials = s_trimaterials;
	scene->num_triangles = s_numtris;
	scene->lights = s_lights;
	scene->num_lights = numlights;
}
