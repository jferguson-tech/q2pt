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
} matkey_t;

static pt_texture_t		*w_textures;
static image_t			**w_teximages;
static int				w_numtextures, w_maxtextures;

static pt_material_t	*w_materials;
static matkey_t			*w_matkeys;
static int				w_nummaterials, w_maxmaterials;

static float			*w_positions, *w_uvs;
static uint32_t			*w_indices, *w_trimaterials;
static int				w_numtris, w_maxtris;

static pt_point_light_t	*w_lights;
static int				w_numlights, w_maxlights;

static uint32_t			*w_skyfaces[6];

//=============================================================================

static int W_AddTexture (int width, int height, const uint32_t *pixels, image_t *image)
{
	int		i;

	if (image)
		for (i=0 ; i<w_numtextures ; i++)
			if (w_teximages[i] == image)
				return i;

	if (w_numtextures == w_maxtextures)
	{
		w_maxtextures = w_maxtextures ? w_maxtextures * 2 : 256;
		w_textures = realloc (w_textures, w_maxtextures * sizeof(*w_textures));
		w_teximages = realloc (w_teximages, w_maxtextures * sizeof(*w_teximages));
	}
	w_textures[w_numtextures].width = width;
	w_textures[w_numtextures].height = height;
	w_textures[w_numtextures].pixels = pixels;
	w_teximages[w_numtextures] = image;
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

static int W_AddMaterial (texinfo_t *tex)
{
	char			name[MAX_QPATH];
	image_t			*image;
	pt_material_t	*mat;
	int				i, flags, value;

	flags = LittleLong (tex->flags) & (SURF_LIGHT|SURF_SKY|SURF_WARP|SURF_TRANS33|SURF_TRANS66);
	value = (flags & SURF_LIGHT) ? LittleLong (tex->value) : 0;

	Com_sprintf (name, sizeof(name), "textures/%.32s.wal", tex->texture);
	image = R_FindImage (name, it_wall);

	for (i=0 ; i<w_nummaterials ; i++)
		if (w_matkeys[i].image == image && w_matkeys[i].flags == flags && w_matkeys[i].value == value)
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

	mat = &w_materials[w_nummaterials];
	memset (mat, 0, sizeof(*mat));
	mat->texture = image ? W_AddTexture (image->width, image->height, image->pixels, image) : -1;
	mat->alpha = 1;

	if (flags & SURF_SKY)
		mat->flags |= PT_MAT_SKY;
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
				mat->emission[i] = color[i] * value * LIGHT_UNIT;
		}
	}

	return w_nummaterials++;
}

static void W_AddTriangle (float *a, float *b, float *c, float *uva, float *uvb, float *uvc, int material)
{
	float	*p, *uv;
	int		i;

	if (w_numtris == w_maxtris)
	{
		w_maxtris = w_maxtris ? w_maxtris * 2 : 16384;
		w_positions = realloc (w_positions, w_maxtris * 9 * sizeof(float));
		w_uvs = realloc (w_uvs, w_maxtris * 6 * sizeof(float));
		w_indices = realloc (w_indices, w_maxtris * 3 * sizeof(uint32_t));
		w_trimaterials = realloc (w_trimaterials, w_maxtris * sizeof(uint32_t));
	}

	p = w_positions + w_numtris * 9;
	VectorCopy (a, p);
	VectorCopy (b, (p + 3));
	VectorCopy (c, (p + 6));

	uv = w_uvs + w_numtris * 6;
	uv[0] = uva[0]; uv[1] = uva[1];
	uv[2] = uvb[0]; uv[3] = uvb[1];
	uv[4] = uvc[0]; uv[5] = uvc[1];

	for (i=0 ; i<3 ; i++)
		w_indices[w_numtris * 3 + i] = w_numtris * 3 + i;
	w_trimaterials[w_numtris] = material;
	w_numtris++;
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

Fan triangulates the world model's faces
===============
*/
static void W_LoadFaces (byte *base, int filelen)
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

	texmat = malloc ((numtexinfo + 1) * sizeof(int));
	for (i=0 ; i<numtexinfo ; i++)
		texmat[i] = -1;

	firstface = LittleLong (models[0].firstface);
	facecount = LittleLong (models[0].numfaces);
	if (firstface < 0 || facecount < 0 || firstface + facecount > numfaces)
		ri.Sys_Error (ERR_DROP, "R_LoadWorld: bad world model");

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

		if (texmat[texnum] < 0)
			texmat[texnum] = W_AddMaterial (tex);
		material = texmat[texnum];

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
}

/*
===============
W_LoadLights

The map's point light entities
===============
*/
static void W_LoadLights (byte *base, int filelen)
{
	char	*entstring, *data, *token;
	char	key[MAX_KEY], classname[64];
	vec3_t	origin, color;
	float	light;
	int		len, i;
	qboolean hasorigin;

	data = W_Lump (base, filelen, LUMP_ENTITIES, 1, &len);
	entstring = malloc (len + 1);
	memcpy (entstring, data, len);
	entstring[len] = 0;

	data = entstring;
	for (;;)
	{
		token = COM_Parse (&data);
		if (!data || token[0] != '{')
			break;

		classname[0] = 0;
		VectorClear (origin);
		color[0] = color[1] = color[2] = 1;
		light = 300;
		hasorigin = false;

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
			else if (!strcmp (key, "origin"))
				hasorigin = sscanf (token, "%f %f %f", &origin[0], &origin[1], &origin[2]) == 3;
			else if (!strcmp (key, "light") || !strcmp (key, "_light"))
				light = atof (token);
			else if (!strcmp (key, "_color"))
				sscanf (token, "%f %f %f", &color[0], &color[1], &color[2]);
		}

		if (strcmp (classname, "light") || !hasorigin || light <= 0)
			continue;

		if (w_numlights == w_maxlights)
		{
			w_maxlights = w_maxlights ? w_maxlights * 2 : 256;
			w_lights = realloc (w_lights, w_maxlights * sizeof(*w_lights));
		}
		for (i=0 ; i<3 ; i++)
		{
			w_lights[w_numlights].origin[i] = origin[i];
			w_lights[w_numlights].intensity[i] = color[i] * POINT_LIGHT_INTENSITY (light);
		}
		w_numlights++;
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
	int			filelen;

	if (!name || !name[0])
	{
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

	w_numtextures = w_nummaterials = w_numtris = w_numlights = 0;

	memset (&world, 0, sizeof(world));
	W_LoadFaces (base, filelen);
	W_LoadLights (base, filelen);
	W_LoadSky (skyname, world.sky_textures);
	ri.FS_FreeFile (base);

	world.textures = w_textures;
	world.num_textures = w_numtextures;
	world.materials = w_materials;
	world.num_materials = w_nummaterials;
	world.positions = w_positions;
	world.uvs = w_uvs;
	world.num_vertices = w_numtris * 3;
	world.indices = w_indices;
	world.tri_materials = w_trimaterials;
	world.num_triangles = w_numtris;
	world.lights = w_lights;
	world.num_lights = w_numlights;
	world.sky_scale = 2.0f;

	rpt.backend->load_world (rpt.backend, &world);

	ri.Con_Printf (PRINT_ALL, "%s: %d triangles, %d materials, %d textures, %d point lights, sky \"%s\"%s\n",
		name, w_numtris, w_nummaterials, w_numtextures, w_numlights, skyname,
		world.sky_textures[0] < 0 ? " (not loaded)" : "");
}
