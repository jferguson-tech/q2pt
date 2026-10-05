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
// rpt_world.c -- turns a .bsp into the path tracer's world description

#include "rpt_local.h"

/*
The light compiler worked in lightmap units, where 255 is a fully lit
texture. A diffuse surface shows albedo / pi of the irradiance it receives,
so pi / 255 converts lightmap units to the tracer's irradiance.

A surface light added value * cos * cos / d^2 per unit area, which is
already physical: its radiance is value * LIGHT_UNIT.

A point light of strength L added (L - d) * cos: linear falloff, dark at
distance L. Nothing physical does that, so the tracer gets an inverse square
light that is as bright as the original at half its range:
intensity / (L/2)^2 = L/2. It is dimmer than the original nearer than that
and reaches further.
*/
#define	LIGHT_UNIT	(3.14159265f / 255.0f)
#define	POINT_LIGHT_INTENSITY(l)	((l) * (l) * (l) / 8.0f * LIGHT_UNIT)

#define	SKY_SIZE	256

typedef struct
{
	image_t	*image;
	int		flags;
	int		value;
	int		body;		// simulated body of liquid it is the surface of, or -1
} matkey_t;

static pt_texture_t		*w_textures;
static const void		**w_texkeys;		// what each texture was made from, to share them
static int				w_numtextures, w_maxtextures;

static pt_material_t	*w_materials;
static matkey_t			*w_matkeys;
static int				w_nummaterials, w_maxmaterials;

typedef struct
{
	float		*positions;		// 9 per triangle
	float		*uvs;			// 6 per triangle
	uint32_t	*materials;
	int			num, max;
} trilist_t;

static trilist_t		w_world;		// model 0, what the backend loads
static trilist_t		w_inline;		// every other model, each in its own space
static trilist_t		*w_list;		// the one being filled
static int				w_inlinefirst[MAX_MAP_MODELS], w_inlinecount[MAX_MAP_MODELS];
static int				w_nummodels;
static uint32_t			*w_indices;

static pt_point_light_t	*w_lights;
static int				w_numlights, w_maxlights;

static uint32_t			*w_skyfaces[6];

//=============================================================================

static int W_AddTexture (int width, int height, const uint32_t *pixels, const void *key)
{
	int		i;

	if (key)
		for (i=0 ; i<w_numtextures ; i++)
			if (w_texkeys[i] == key)
				return i;

	if (w_numtextures == w_maxtextures)
	{
		w_maxtextures = w_maxtextures ? w_maxtextures * 2 : 256;
		w_textures = realloc (w_textures, w_maxtextures * sizeof(*w_textures));
		w_texkeys = realloc ((void *)w_texkeys, w_maxtextures * sizeof(*w_texkeys));
	}
	w_textures[w_numtextures].width = width;
	w_textures[w_numtextures].height = height;
	w_textures[w_numtextures].pixels = pixels;
	w_texkeys[w_numtextures] = key;
	return w_numtextures++;
}

/*
===============
W_Reflectivity

The image's average colour scaled so its largest channel is 1, which is
what the light compiler tinted surface lights with
===============
*/
static void W_Reflectivity (image_t *image, float *color)
{
	double	sum[3];
	float	max;
	int		i, count;

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
	for (i=0 ; i<3 ; i++)
		color[i] = max > 0 ? sum[i] / max : 1;
}

static int W_AddMaterial (texinfo_t *tex, int body)
{
	char			name[MAX_QPATH];
	image_t			*image;
	pt_material_t	*mat;
	matinfo_t		info;
	int				i, flags, value;
	qboolean		classic = false;

	flags = LittleLong (tex->flags) & (SURF_LIGHT|SURF_SKY|SURF_WARP|SURF_TRANS33|SURF_TRANS66|SURF_FLOWING);
	value = (flags & SURF_LIGHT) ? LittleLong (tex->value) : 0;

	Com_sprintf (name, sizeof(name), "textures/%.32s.wal", tex->texture);
	image = R_FindImage (name, it_wall);

	for (i=0 ; i<w_nummaterials ; i++)
		if (w_matkeys[i].image == image && w_matkeys[i].flags == flags && w_matkeys[i].value == value
			&& w_matkeys[i].body == body)
			return i;

	if (w_nummaterials == w_maxmaterials)
	{
		w_maxmaterials = w_maxmaterials ? w_maxmaterials * 2 : 256;
		w_materials = realloc (w_materials, w_maxmaterials * sizeof(*w_materials));
		w_matkeys = realloc (w_matkeys, w_maxmaterials * sizeof(*w_matkeys));
	}
	w_matkeys[w_nummaterials].image = image;
	w_matkeys[w_nummaterials].flags = flags;
	w_matkeys[w_nummaterials].value = value;
	w_matkeys[w_nummaterials].body = body;

	mat = &w_materials[w_nummaterials];
	memset (mat, 0, sizeof(*mat));
	mat->texture = image ? W_AddTexture (image->width, image->height, image->pixels, image) : -1;
	mat->alpha = 1;
	mat->normal_texture = -1;
	mat->anim_next = -1;

	R_MaterialInfo (name, &info);
	if ((flags & SURF_WARP) && r_watermode == 0)
	{	// classic: a flat sheet whose texture swims, lit by nothing but itself
		info.roughness = 1;
		info.metallic = 0;
		info.bump = 0;
		mat->flags |= PT_MAT_WARP;
		classic = true;
	}
	else if (flags & SURF_WARP)
	{	// water, slime, lava: a smooth, rippling surface that soaks up light
		info.roughness = 0.05f;
		info.metallic = 0;
		info.bump = 0;
		mat->flags |= PT_MAT_WAVES;
		R_WaterAbsorb (image, name, mat->absorb);
	}
	else if (flags & (SURF_TRANS33|SURF_TRANS66))
	{	// glass and force fields
		info.roughness = 0.05f;
		info.metallic = 0;
		info.bump = 0;
	}
	mat->roughness = info.roughness;
	if (flags & SURF_FLOWING)
		mat->scroll[0] = -1.0f / 40;	// one repeat every 40 seconds, as ref_gl scrolls it
	// screens, buttons and indicator lights that the map did not make into
	// lights still glow where their picture is bright
	if (info.glow > 0 && value <= 0 && !(flags & (SURF_SKY|SURF_WARP)))
	{
		mat->flags |= PT_MAT_EMIT_BRIGHT;
		mat->emission[0] = mat->emission[1] = mat->emission[2] = info.glow * r_detailglow;
	}
	mat->metallic = info.metallic;
	if (image && info.bump > 0 && !(flags & SURF_SKY))
		mat->normal_texture = W_AddTexture (image->width, image->height,
			R_ImageNormalMap (image, &info), &image->normalmap);

	if (flags & SURF_SKY)
		mat->flags |= PT_MAT_SKY;
	else if (classic)
	{
		if (flags & SURF_TRANS33)
			mat->alpha = 0.33f;
		else if (flags & SURF_TRANS66)
			mat->alpha = 0.66f;
		mat->emission[0] = mat->emission[1] = mat->emission[2] = 0.35f;
	}
	else
	{
		if (flags & SURF_TRANS33)
			mat->alpha = 0.33f;
		else if (flags & SURF_TRANS66)
			mat->alpha = 0.66f;

		if (value > 0)
		{
			float	color[3];

			color[0] = color[1] = color[2] = 1;
			if (image)
				W_Reflectivity (image, color);
			for (i=0 ; i<3 ; i++)
				mat->emission[i] = color[i] * value * LIGHT_UNIT * r_surfacelight
					* ((flags & SURF_WARP) ? r_liquidglow : 1);

			// to the eye a lamp is its texture, a bit over full brightness
			mat->emission_seen = r_lampglow;
		}
	}

	return w_nummaterials++;
}

/*
===============
W_TexinfoMaterial

The material for a texinfo, with the rest of its animation linked on
===============
*/
static int W_TexinfoMaterial (texinfo_t *texinfos, int numtexinfo, int *texmat, int texnum)
{
	int		material, next, nextmaterial;

	if (texmat[texnum] >= 0)
		return texmat[texnum];

	material = W_AddMaterial (&texinfos[texnum], -1);
	texmat[texnum] = material;		// before recursing: animations are loops

	next = LittleLong (texinfos[texnum].nexttexinfo);
	if (next > 0 && next < numtexinfo && next != texnum)
	{
		nextmaterial = W_TexinfoMaterial (texinfos, numtexinfo, texmat, next);
		if (nextmaterial != material && w_materials[material].anim_next < 0)
			w_materials[material].anim_next = nextmaterial;
	}
	return material;
}

static void W_AddTriangle (float *a, float *b, float *c, float *uva, float *uvb, float *uvc, int material)
{
	trilist_t	*list = w_list;
	float		*p, *uv;

	if (list->num == list->max)
	{
		list->max = list->max ? list->max * 2 : 16384;
		list->positions = realloc (list->positions, list->max * 9 * sizeof(float));
		list->uvs = realloc (list->uvs, list->max * 6 * sizeof(float));
		list->materials = realloc (list->materials, list->max * sizeof(uint32_t));
	}

	p = list->positions + list->num * 9;
	VectorCopy (a, p);
	VectorCopy (b, (p + 3));
	VectorCopy (c, (p + 6));

	uv = list->uvs + list->num * 6;
	uv[0] = uva[0]; uv[1] = uva[1];
	uv[2] = uvb[0]; uv[3] = uvb[1];
	uv[4] = uvc[0]; uv[5] = uvc[1];

	list->materials[list->num] = material;
	list->num++;
}

//=============================================================================

static void *W_Lump (byte *base, int filelen, int lump, int elemsize, int *count)
{
	dheader_t	*header = (dheader_t *)base;
	int			ofs, len;

	ofs = LittleLong (header->lumps[lump].fileofs);
	len = LittleLong (header->lumps[lump].filelen);
	if (ofs < 0 || len < 0 || ofs > filelen - len || len % elemsize)
		ri.Sys_Error (ERR_DROP, "R_LoadWorld: bad lump %d", lump);
	*count = len / elemsize;
	return base + ofs;
}

/*
===============
W_LoadFaces

Fan triangulates one model's faces into w_list. Returns how many models
the map has.
===============
*/
static int W_LoadFaces (byte *base, int filelen, int modelnum)
{
	dvertex_t	*verts;
	dedge_t		*edges;
	int			*surfedges;
	dface_t		*faces, *face;
	texinfo_t	*texinfos, *tex;
	dplane_t	*planes;
	dmodel_t	*models;
	int			numverts, numedges, numsurfedges, numfaces, numtexinfo, numplanes, nummodels;
	int			*texmat;
	int			i, j, k, e, firstface, facecount, numedgesface, firstedge, texnum, planenum, material;
	float		points[64][3], uvs[64][2];
	vec3_t		normal, d1, d2, cross;
	float		width, height;

	verts = W_Lump (base, filelen, LUMP_VERTEXES, sizeof(*verts), &numverts);
	edges = W_Lump (base, filelen, LUMP_EDGES, sizeof(*edges), &numedges);
	surfedges = W_Lump (base, filelen, LUMP_SURFEDGES, sizeof(*surfedges), &numsurfedges);
	faces = W_Lump (base, filelen, LUMP_FACES, sizeof(*faces), &numfaces);
	texinfos = W_Lump (base, filelen, LUMP_TEXINFO, sizeof(*texinfos), &numtexinfo);
	planes = W_Lump (base, filelen, LUMP_PLANES, sizeof(*planes), &numplanes);
	models = W_Lump (base, filelen, LUMP_MODELS, sizeof(*models), &nummodels);
	if (nummodels < 1)
		ri.Sys_Error (ERR_DROP, "R_LoadWorld: map with no models");
	if (nummodels > MAX_MAP_MODELS)
		nummodels = MAX_MAP_MODELS;
	if (modelnum >= nummodels)
		return nummodels;

	texmat = malloc ((numtexinfo + 1) * sizeof(int));
	for (i=0 ; i<numtexinfo ; i++)
		texmat[i] = -1;

	firstface = LittleLong (models[modelnum].firstface);
	facecount = LittleLong (models[modelnum].numfaces);
	if (firstface < 0 || facecount < 0 || firstface + facecount > numfaces)
		ri.Sys_Error (ERR_DROP, "R_LoadWorld: bad model %d", modelnum);

	for (i=0, face=faces+firstface ; i<facecount ; i++, face++)
	{
		texnum = LittleShort (face->texinfo);
		planenum = (unsigned short)LittleShort (face->planenum);
		numedgesface = LittleShort (face->numedges);
		firstedge = LittleLong (face->firstedge);

		if (texnum < 0 || texnum >= numtexinfo || planenum >= numplanes
			|| numedgesface < 3 || numedgesface > 64
			|| firstedge < 0 || firstedge + numedgesface > numsurfedges)
			continue;

		tex = &texinfos[texnum];
		if (LittleLong (tex->flags) & SURF_NODRAW)
			continue;

		material = W_TexinfoMaterial (texinfos, numtexinfo, texmat, texnum);

		width = height = 64;
		if (w_matkeys[material].image)
		{
			width = w_matkeys[material].image->width;
			height = w_matkeys[material].image->height;
		}

		for (j=0 ; j<numedgesface ; j++)
		{
			e = LittleLong (surfedges[firstedge + j]);
			if (e >= 0)
				k = e < numedges ? LittleShort (edges[e].v[0]) & 0xffff : 0;
			else
				k = -e < numedges ? LittleShort (edges[-e].v[1]) & 0xffff : 0;
			if (k >= numverts)
				k = 0;

			for (e=0 ; e<3 ; e++)
				points[j][e] = LittleFloat (verts[k].point[e]);
			uvs[j][0] = (DotProduct (points[j], tex->vecs[0]) + tex->vecs[0][3]) / width;
			uvs[j][1] = (DotProduct (points[j], tex->vecs[1]) + tex->vecs[1][3]) / height;
		}

		// the tracer wants counter clockwise from the front
		for (e=0 ; e<3 ; e++)
			normal[e] = LittleFloat (planes[planenum].normal[e]);
		if (LittleShort (face->side))
			VectorNegate (normal, normal);

		// a level liquid surface of the world itself can be simulated: it
		// gets the material of the body of liquid it belongs to
		if (r_watermode == 2 && modelnum == 0 && (LittleLong (tex->flags) & SURF_WARP)
			&& fabs (normal[2]) > 0.99f)
		{
			char	texname[40];
			int		body;

			Com_sprintf (texname, sizeof(texname), "%.32s", tex->texture);
			strlwr (texname);
			body = R_WaterBody (w_matkeys[material].image, texname, points[0][2], points, numedgesface);
			if (body >= 0)
			{
				material = W_AddMaterial (tex, body);
				R_WaterSetMaterial (body, material);
			}
		}

		for (j=2 ; j<numedgesface ; j++)
		{
			VectorSubtract (points[j-1], points[0], d1);

			VectorSubtract (points[j], points[0], d2);
			CrossProduct (d1, d2, cross);
			if (DotProduct (cross, normal) >= 0)
				W_AddTriangle (points[0], points[j-1], points[j], uvs[0], uvs[j-1], uvs[j], material);
			else
				W_AddTriangle (points[0], points[j], points[j-1], uvs[0], uvs[j], uvs[j-1], material);
		}
	}

	free (texmat);
	return nummodels;
}

#define	MAX_LIGHT_TARGETS	1024

typedef struct
{
	char	name[32];
	vec3_t	origin;
} lighttarget_t;

/*
===============
W_LoadLights

The map's point light entities. A light that names a target, or is given a
cone, is a spotlight, as the light compiler had it.
===============
*/
static void W_LoadLights (byte *base, int filelen)
{
	static lighttarget_t	targets[MAX_LIGHT_TARGETS];
	char	*entstring, *data, *token;
	char	key[MAX_KEY], classname[64], target[32], targetname[32];
	vec3_t	origin, color, dir;
	float	light, cone, angle;
	int		len, i, pass, style, numtargets;
	qboolean hasorigin, hascone, hasangle, spot;
	pt_point_light_t	*out;

	data = W_Lump (base, filelen, LUMP_ENTITIES, 1, &len);
	entstring = malloc (len + 1);
	memcpy (entstring, data, len);
	entstring[len] = 0;

	numtargets = 0;

	// first everything that can be pointed at, then the lights
	for (pass=0 ; pass<2 ; pass++)
	{
		data = entstring;
		for (;;)
		{
			token = COM_Parse (&data);
			if (!data || token[0] != '{')
				break;

			classname[0] = target[0] = targetname[0] = 0;
			VectorClear (origin);
			color[0] = color[1] = color[2] = 1;
			light = 300;
			style = 0;
			cone = 10;
			angle = 0;
			hasorigin = hascone = hasangle = false;

			for (;;)
			{
				token = COM_Parse (&data);
				if (!data || token[0] == '}')
					break;
				strncpy (key, token, sizeof(key)-1);
				key[sizeof(key)-1] = 0;

				token = COM_Parse (&data);
				if (!data)
					break;

				if (!strcmp (key, "classname"))
				{
					strncpy (classname, token, sizeof(classname)-1);
					classname[sizeof(classname)-1] = 0;
				}
				else if (!strcmp (key, "target"))
				{
					strncpy (target, token, sizeof(target)-1);
					target[sizeof(target)-1] = 0;
				}
				else if (!strcmp (key, "targetname"))
				{
					strncpy (targetname, token, sizeof(targetname)-1);
					targetname[sizeof(targetname)-1] = 0;
				}
				else if (!strcmp (key, "origin"))
					hasorigin = sscanf (token, "%f %f %f", &origin[0], &origin[1], &origin[2]) == 3;
				else if (!strcmp (key, "light") || !strcmp (key, "_light"))
					light = atof (token);
				else if (!strcmp (key, "_color"))
					sscanf (token, "%f %f %f", &color[0], &color[1], &color[2]);
				else if (!strcmp (key, "style") || !strcmp (key, "_style"))
					style = atoi (token);
				else if (!strcmp (key, "_cone"))
				{
					cone = atof (token);
					hascone = true;
				}
				else if (!strcmp (key, "angle"))
				{
					angle = atof (token);
					hasangle = true;
				}
			}

			if (pass == 0)
			{
				if (targetname[0] && hasorigin && numtargets < MAX_LIGHT_TARGETS)
				{
					strcpy (targets[numtargets].name, targetname);
					VectorCopy (origin, targets[numtargets].origin);
					numtargets++;
				}
				continue;
			}

			if (strcmp (classname, "light") || !hasorigin || light <= 0)
				continue;

			// which way a spotlight points
			spot = false;
			if (target[0])
			{
				for (i=0 ; i<numtargets ; i++)
				{
					if (!strcmp (targets[i].name, target))
					{
						VectorSubtract (targets[i].origin, origin, dir);
						spot = VectorNormalize (dir) > 0;
						break;
					}
				}
			}
			else if (hascone && hasangle)
			{
				if (angle == -1)			// up
					VectorSet (dir, 0, 0, 1);
				else if (angle == -2)		// down
					VectorSet (dir, 0, 0, -1);
				else
					VectorSet (dir, cos (angle * (M_PI / 180)), sin (angle * (M_PI / 180)), 0);
				spot = true;
			}

			if (w_numlights == w_maxlights)
			{
				w_maxlights = w_maxlights ? w_maxlights * 2 : 256;
				w_lights = realloc (w_lights, w_maxlights * sizeof(*w_lights));
			}
			out = &w_lights[w_numlights++];
			memset (out, 0, sizeof(*out));
			for (i=0 ; i<3 ; i++)
			{
				out->origin[i] = origin[i];
				out->intensity[i] = color[i] * POINT_LIGHT_INTENSITY (light) * r_pointlight;
			}
			// flickering and switchable lights follow their light style
			out->style = (style > 0 && style < MAX_LIGHTSTYLES) ? style : 0;
			if (spot)
			{
				if (cone < 1)
					cone = 1;
				if (cone > 89)
					cone = 89;
				VectorCopy (dir, out->direction);
				out->cone_cos = cos (cone * (M_PI / 180));
			}
		}
	}

	free (entstring);
}

/*
===============
W_LoadSky

Resamples the six sky images into the tracer's cube layout. The direction
to image mapping is the one ref_gl draws the sky box with.
===============
*/
static void W_LoadSky (char *skyname, int *faces)
{
	static char	*suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
	static int	skytexorder[6] = {0, 2, 1, 3, 4, 5};
	static int	vec_to_st[6][3] =
	{
		{-2,3,1},
		{2,3,-1},

		{1,3,2},
		{-1,3,-2},

		{-2,-1,3},
		{-2,1,-3}
	};
	char		pathname[MAX_QPATH];
	image_t		*images[6], *image;
	int			i, j, x, y, a, b, c, axis, sx, sy;
	float		d[3], av[3], s, t, dv;

	for (i=0 ; i<6 ; i++)
		faces[i] = -1;
	if (!skyname[0])
		return;

	for (i=0 ; i<6 ; i++)
	{
		Com_sprintf (pathname, sizeof(pathname), "env/%s%s.tga", skyname, suf[i]);
		images[i] = R_FindImage (pathname, it_sky);
		if (!images[i])
		{
			Com_sprintf (pathname, sizeof(pathname), "env/%s%s.pcx", skyname, suf[i]);
			images[i] = R_FindImage (pathname, it_sky);
		}
		if (!images[i])
		{
			ri.Con_Printf (PRINT_ALL, "Couldn't load sky %s\n", pathname);
			return;
		}
	}

	for (i=0 ; i<6 ; i++)
	{
		a = i / 2;
		b = (a + 1) % 3;
		c = (a + 2) % 3;

		if (!w_skyfaces[i])
			w_skyfaces[i] = malloc (SKY_SIZE * SKY_SIZE * sizeof(uint32_t));

		for (y=0 ; y<SKY_SIZE ; y++)
		{
			for (x=0 ; x<SKY_SIZE ; x++)
			{
				d[a] = (i & 1) ? -1 : 1;
				d[b] = 2 * (x + 0.5f) / SKY_SIZE - 1;
				d[c] = 2 * (y + 0.5f) / SKY_SIZE - 1;

				av[0] = fabs (d[0]);
				av[1] = fabs (d[1]);
				av[2] = fabs (d[2]);
				if (av[0] > av[1] && av[0] > av[2])
					axis = d[0] < 0 ? 1 : 0;
				else if (av[1] > av[2] && av[1] > av[0])
					axis = d[1] < 0 ? 3 : 2;
				else
					axis = d[2] < 0 ? 5 : 4;

				j = vec_to_st[axis][2];
				dv = j > 0 ? d[j - 1] : -d[-j - 1];
				if (dv < 0.001)
					dv = 0.001;
				j = vec_to_st[axis][0];
				s = (j < 0 ? -d[-j - 1] : d[j - 1]) / dv;
				j = vec_to_st[axis][1];
				t = (j < 0 ? -d[-j - 1] : d[j - 1]) / dv;

				s = (s + 1) * 0.5;
				t = 1.0 - (t + 1) * 0.5;

				image = images[skytexorder[axis]];
				sx = s * image->width;
				sy = t * image->height;
				if (sx < 0) sx = 0;
				if (sx >= image->width) sx = image->width - 1;
				if (sy < 0) sy = 0;
				if (sy >= image->height) sy = image->height - 1;

				w_skyfaces[i][y * SKY_SIZE + x] = image->pixels[sy * image->width + sx];
			}
		}

		faces[i] = W_AddTexture (SKY_SIZE, SKY_SIZE, w_skyfaces[i], NULL);
	}
}

/*
===============
R_LoadWorld

Hands the map to the backend. name is the .bsp path; NULL unloads.
===============
*/
void R_LoadWorld (char *name, char *skyname)
{
	pt_world_t	world;
	byte		*base;
	dheader_t	*header;
	int			filelen, i;

	w_nummodels = 0;
	if (!name || !name[0])
	{
		R_WaterReset ();
		rpt.backend->load_world (rpt.backend, NULL);
		return;
	}

	filelen = ri.FS_LoadFile (name, (void **)&base);
	if (!base)
		ri.Sys_Error (ERR_DROP, "R_LoadWorld: %s not found", name);

	header = (dheader_t *)base;
	if (filelen < (int)sizeof(*header) || LittleLong (header->ident) != IDBSPHEADER
		|| LittleLong (header->version) != BSPVERSION)
		ri.Sys_Error (ERR_DROP, "R_LoadWorld: %s is not a version %d bsp", name, BSPVERSION);

	w_numtextures = w_nummaterials = w_numlights = 0;
	R_WaterReset ();
	w_world.num = w_inline.num = 0;

	memset (&world, 0, sizeof(world));

	w_list = &w_world;
	w_nummodels = W_LoadFaces (base, filelen, 0);

	// doors, lifts and the like: kept here and added to each frame where they are
	w_list = &w_inline;
	for (i=1 ; i<w_nummodels ; i++)
	{
		w_inlinefirst[i] = w_inline.num;
		W_LoadFaces (base, filelen, i);
		w_inlinecount[i] = w_inline.num - w_inlinefirst[i];
	}

	w_indices = realloc (w_indices, (w_world.num * 3 + 1) * sizeof(uint32_t));
	for (i=0 ; i<w_world.num*3 ; i++)
		w_indices[i] = i;

	W_LoadLights (base, filelen);
	W_LoadSky (skyname, world.sky_textures);
	ri.FS_FreeFile (base);

	world.textures = w_textures;
	world.num_textures = w_numtextures;
	world.materials = w_materials;
	world.num_materials = w_nummaterials;
	world.positions = w_world.positions;
	world.uvs = w_world.uvs;
	world.num_vertices = w_world.num * 3;
	world.indices = w_indices;
	world.tri_materials = w_world.materials;
	world.num_triangles = w_world.num;
	world.lights = w_lights;
	world.num_lights = w_numlights;
	world.sky_scale = r_skyscale;

	R_WaterFinish ();		// before the backend copies the materials
	rpt.backend->load_world (rpt.backend, &world);

	ri.Con_Printf (PRINT_ALL, "%s: %d triangles, %d materials, %d textures, %d point lights, sky \"%s\"%s\n",
		name, w_world.num, w_nummaterials, w_numtextures, w_numlights, skyname,
		world.sky_textures[0] < 0 ? " (not loaded)" : "");
}

/*
===============
R_InlineModel

The triangles of inline model "*num", in the model's own space
===============
*/
int R_InlineModel (int num, float **positions, float **uvs, uint32_t **materials)
{
	if (num < 1 || num >= w_nummodels)
		return 0;

	*positions = w_inline.positions + w_inlinefirst[num] * 9;
	*uvs = w_inline.uvs + w_inlinefirst[num] * 6;
	*materials = w_inline.materials + w_inlinefirst[num];
	return w_inlinecount[num];
}

/*
===============
R_WorldMaterial

A material index from R_InlineModel, as a material and the image it uses
===============
*/
void R_WorldMaterial (int index, pt_material_t *material, image_t **image)
{
	*material = w_materials[index];
	*image = w_matkeys[index].image;
}

pt_material_t *R_WorldMaterialPtr (int index)
{
	return &w_materials[index];
}
