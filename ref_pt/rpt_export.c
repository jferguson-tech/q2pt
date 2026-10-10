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
// rpt_export.c -- the planes of every frame of a film, for training a network
//
// With pt_render_export set while a demo is rendered (pt_render), each frame
// is made twice over and saved beside its PNG as frameNNNNN.planes: once
// with one path a pixel and no bounces, for what the card can tell of the
// first surface cheaply and what every light would put on it with nothing
// in the way, and once with all the paths asked for, for what the light at
// it comes to. The file is planes of numbers, one for each PT_PLANE_ of
// pt.h, with a table of what they are and a block of text about the frame.
// neural/README.md says the format in full; neural/planes.py reads it.

#include "rpt_local.h"

static cvar_t	*pt_render_export;
static qboolean	ex_warned;
static float	*ex_plane[PT_NUM_PLANES];
static int		ex_width, ex_height;
static int		ex_clamped;				// values too large for half precision this frame

// how each plane is stored in the file
#define	EX_F16	0
#define	EX_F32	1
#define	EX_U8	2
#define	EX_I32	5

#define	EX_HEADER	64
#define	EX_ENTRY	48
#define	EX_ALIGN	64

static const struct
{
	const char	*name;
	int			plane, type, channels;
} ex_planes[PT_NUM_PLANES] = {
	{"position", PT_PLANE_POSITION, EX_F32, 3},
	{"normal", PT_PLANE_NORMAL, EX_F16, 3},
	{"depth", PT_PLANE_DEPTH, EX_F16, 1},
	{"albedo", PT_PLANE_ALBEDO, EX_U8, 3},
	{"specular", PT_PLANE_SPECULAR, EX_U8, 3},
	{"roughness", PT_PLANE_ROUGHNESS, EX_U8, 1},
	{"emission", PT_PLANE_EMISSION, EX_F16, 3},
	{"metallic", PT_PLANE_METALLIC, EX_U8, 1},
	{"material", PT_PLANE_MATERIAL, EX_I32, 1},
	{"triangle", PT_PLANE_TRIANGLE, EX_I32, 2},
	{"direct_diffuse", PT_PLANE_DIRECT_DIFFUSE, EX_F16, 3},
	{"direct_specular", PT_PLANE_DIRECT_SPECULAR, EX_F16, 3},
	{"ray_diffuse", PT_PLANE_RAY_DIFFUSE, EX_F16, 3},
	{"ray_specular", PT_PLANE_RAY_SPECULAR, EX_F16, 3},
	{"light_diffuse", PT_PLANE_LIGHT_DIFFUSE, EX_F16, 3},
	{"light_specular", PT_PLANE_LIGHT_SPECULAR, EX_F16, 3},
	{"light_layers", PT_PLANE_LIGHT_LAYERS, EX_F16, 3},
	{"light_extra", PT_PLANE_LIGHT_EXTRA, EX_F16, 3},
	{"picture", PT_PLANE_PICTURE, EX_F16, 3},
};

void R_InitExport (void)
{
	pt_render_export = ri.Cvar_Get ("pt_render_export", "0", 0);
	ex_warned = false;
}

void R_ShutdownExport (void)
{
	int		i;

	for (i=0 ; i<PT_NUM_PLANES ; i++)
	{
		free (ex_plane[i]);
		ex_plane[i] = NULL;
	}
	ex_width = ex_height = 0;
}

qboolean R_Exporting (void)
{
	return pt_render_export->value != 0;
}

/*
===============
R_ExportSettings

Inputs and target must line up to the pixel and be plain light: no offset
within the pixel, no filtering over space, exposure 1
===============
*/
void R_ExportSettings (pt_view_t *view)
{
	view->antialias = 0;
	view->denoise = 0;
	view->exposure = 1;
	view->auto_exposure = 0;
	view->bloom = 0;
	view->blur = 0;
}

static qboolean Ex_Buffers (int w, int h)
{
	int		i;

	if (w == ex_width && h == ex_height && ex_plane[0])
		return true;
	R_ShutdownExport ();
	for (i=0 ; i<PT_NUM_PLANES ; i++)
	{
		ex_plane[i] = malloc ((size_t)w * h * ex_planes[i].channels * sizeof(float));
		if (!ex_plane[i])
		{
			R_ShutdownExport ();
			return false;
		}
	}
	ex_width = w;
	ex_height = h;
	return true;
}

// a float as a half precision float, rounded to the nearest, held to the
// largest half there is
static uint16_t Ex_Half (float f)
{
	uint32_t	bits, sign, exponent, mantissa;
	int			e;

	memcpy (&bits, &f, sizeof(bits));
	sign = (bits >> 16) & 0x8000u;
	exponent = (bits >> 23) & 0xffu;
	mantissa = bits & 0x7fffffu;
	if (exponent == 0xffu)
		return (uint16_t)(sign | 0x7c00u | (mantissa ? 0x200u : 0));		// infinity or not a number
	e = (int)exponent - 127 + 15;
	if (e >= 31)
	{
		ex_clamped++;
		return (uint16_t)(sign | 0x7bffu);
	}
	if (e <= 0)
	{
		if (e < -10)
			return (uint16_t)sign;
		// below the smallest normal half: fewer bits of the mantissa are kept
		mantissa |= 0x800000u;
		{
			const int shift = 14 - e;
			const uint32_t half = mantissa >> shift;
			const uint32_t rest = mantissa & ((1u << shift) - 1), midway = 1u << (shift - 1);
			if (rest > midway || (rest == midway && (half & 1)))
				return (uint16_t)(sign | (half + 1));
			return (uint16_t)(sign | half);
		}
	}
	{
		uint32_t half = ((uint32_t)e << 10) | (mantissa >> 13);
		const uint32_t rest = mantissa & 0x1fffu;
		if (rest > 0x1000u || (rest == 0x1000u && (half & 1)))
			half++;		// may carry into the exponent, which is right
		if (half >= 0x7c00u)
		{
			ex_clamped++;
			half = 0x7bffu;
		}
		return (uint16_t)(sign | half);
	}
}

static void Ex_PutLE32 (byte *at, uint32_t v)
{
	at[0] = v & 255;
	at[1] = (v >> 8) & 255;
	at[2] = (v >> 16) & 255;
	at[3] = (v >> 24) & 255;
}

static void Ex_PutLE64 (byte *at, uint64_t v)
{
	Ex_PutLE32 (at, (uint32_t)v);
	Ex_PutLE32 (at + 4, (uint32_t)(v >> 32));
}

static size_t Ex_Bytes (int i)
{
	size_t	one = ex_planes[i].type == EX_U8 ? 1 : (ex_planes[i].type == EX_F16 ? 2 : 4);

	return (size_t)ex_width * ex_height * ex_planes[i].channels * one;
}

static size_t Ex_Aligned (size_t at)
{
	return (at + EX_ALIGN - 1) / EX_ALIGN * EX_ALIGN;
}

/*
===============
Ex_Write

The file: a header, the table of planes, the text about the frame, then
the planes, each starting at a multiple of 64 bytes. All little endian.
===============
*/
static qboolean Ex_Write (const char *path, refdef_t *fd, pt_view_t *view, int paths, int frame)
{
	FILE		*f;
	byte		header[EX_HEADER], entry[EX_ENTRY];
	char		meta[4096], pad[EX_ALIGN];
	size_t		offsets[PT_NUM_PLANES], at, meta_len, data_at, n, k;
	int			i, c, count;
	uint16_t	*halves = NULL;
	byte		*bytes = NULL;
	int32_t		*ints = NULL;

	// what the frame was
	meta[0] = 0;
	Com_sprintf (meta, sizeof(meta),
		"format=q2pt planes 1\nmap=%s\nframe=%i\ntime=%.4f\nwidth=%i\nheight=%i\n"
		"origin=%.4f %.4f %.4f\nforward=%.6f %.6f %.6f\nright=%.6f %.6f %.6f\nup=%.6f %.6f %.6f\n"
		"fov_x=%.4f\nfov_y=%.4f\npaths=%i\nbounces=%i\nlight_samples=%i\nfog=%i\nfog_density=%.6f\n"
		"exposure=1\nframe_lights=%i\nframe_triangles=%i\n",
		R_WorldName (), frame, fd->time, ex_width, ex_height,
		view->origin[0], view->origin[1], view->origin[2],
		view->forward[0], view->forward[1], view->forward[2],
		view->right[0], view->right[1], view->right[2],
		view->up[0], view->up[1], view->up[2],
		view->fov_x, view->fov_y, paths, view->bounces, view->light_samples, view->fog, view->fog_density,
		view->scene ? view->scene->num_lights : 0, view->scene ? view->scene->num_triangles : 0);
	at = strlen (meta);
	at += snprintf (meta + at, sizeof(meta) - at, "styles=");
	for (i=0 ; i<MAX_LIGHTSTYLES && at < sizeof(meta) - 16 ; i++)
		at += snprintf (meta + at, sizeof(meta) - at, "%s%.3f", i ? " " : "", fd->lightstyles ? fd->lightstyles[i].white : 1.0f);
	// how many values were too large for half precision is known only once
	// they are written: it is put in afterwards, in a field of fixed width
	at += snprintf (meta + at, sizeof(meta) - at, "\nclamped=%10i\n", 0);
	meta_len = at;

	// where everything goes
	data_at = Ex_Aligned (EX_HEADER + EX_ENTRY * PT_NUM_PLANES + meta_len);
	at = data_at;
	for (i=0 ; i<PT_NUM_PLANES ; i++)
	{
		offsets[i] = at;
		at = Ex_Aligned (at + Ex_Bytes (i));
	}

	f = fopen (path, "wb");
	if (!f)
		return false;

	memset (header, 0, sizeof(header));
	memcpy (header, "Q2PTPLNS", 8);
	Ex_PutLE32 (header + 8, 1);
	Ex_PutLE32 (header + 12, ex_width);
	Ex_PutLE32 (header + 16, ex_height);
	Ex_PutLE32 (header + 20, PT_NUM_PLANES);
	Ex_PutLE32 (header + 24, EX_HEADER);
	Ex_PutLE32 (header + 28, EX_HEADER + EX_ENTRY * PT_NUM_PLANES);
	Ex_PutLE32 (header + 32, (uint32_t)meta_len);
	Ex_PutLE32 (header + 36, (uint32_t)data_at);
	fwrite (header, 1, sizeof(header), f);

	for (i=0 ; i<PT_NUM_PLANES ; i++)
	{
		memset (entry, 0, sizeof(entry));
		strncpy ((char *)entry, ex_planes[i].name, 23);
		Ex_PutLE32 (entry + 24, ex_planes[i].type);
		Ex_PutLE32 (entry + 28, ex_planes[i].channels);
		Ex_PutLE64 (entry + 32, offsets[i]);
		Ex_PutLE64 (entry + 40, Ex_Bytes (i));
		fwrite (entry, 1, sizeof(entry), f);
	}
	fwrite (meta, 1, meta_len, f);
	memset (pad, 0, sizeof(pad));
	at = EX_HEADER + EX_ENTRY * PT_NUM_PLANES + meta_len;
	if (data_at > at)
		fwrite (pad, 1, data_at - at, f);

	// the planes, each converted to how it is stored
	ex_clamped = 0;
	n = (size_t)ex_width * ex_height;
	for (i=0 ; i<PT_NUM_PLANES ; i++)
	{
		const float	*src = ex_plane[i];

		count = ex_planes[i].channels;
		switch (ex_planes[i].type)
		{
		case EX_F32:
			fwrite (src, sizeof(float), n * count, f);
			break;
		case EX_F16:
			if (!halves)
				halves = malloc (n * 3 * sizeof(uint16_t));
			if (!halves)
				goto fail;
			for (k=0 ; k<n*count ; k++)
				halves[k] = Ex_Half (src[k]);
			fwrite (halves, sizeof(uint16_t), n * count, f);
			break;
		case EX_U8:
			if (!bytes)
				bytes = malloc (n * 3);
			if (!bytes)
				goto fail;
			for (k=0 ; k<n*count ; k++)
			{
				const float v = src[k] * 255.0f + 0.5f;
				bytes[k] = v <= 0 ? 0 : (v >= 255 ? 255 : (byte)v);
			}
			fwrite (bytes, 1, n * count, f);
			break;
		default:
			if (!ints)
				ints = malloc (n * 2 * sizeof(int32_t));
			if (!ints)
				goto fail;
			for (k=0 ; k<n*count ; k++)
				ints[k] = (int32_t)src[k];
			fwrite (ints, sizeof(int32_t), n * count, f);
			break;
		}
		at = offsets[i] + Ex_Bytes (i);
		c = (int)(Ex_Aligned (at) - at);
		if (c > 0)
			fwrite (pad, 1, c, f);
	}

	// the count of clamped values, into its field in the text
	{
		char	*field = strstr (meta, "clamped=");
		if (field)
		{
			char	digits[16];
			Com_sprintf (digits, sizeof(digits), "%10i", ex_clamped);
			memcpy (field + 8, digits, 10);
			fseek (f, (long)(EX_HEADER + EX_ENTRY * PT_NUM_PLANES), SEEK_SET);
			fwrite (meta, 1, meta_len, f);
		}
	}

	free (halves);
	free (bytes);
	free (ints);
	return fclose (f) == 0;

fail:
	free (halves);
	free (bytes);
	free (ints);
	fclose (f);
	return false;
}

/*
===============
R_ExportFrame

In place of the offline passes: the inputs from one path with no bounces,
then the target from the passes added up, then the file. The target is the
last view rendered, so the frame's PNG is of it as usual.
===============
*/
qboolean R_ExportFrame (refdef_t *fd, pt_view_t *view, int passes, const char *dir, int frame)
{
	pt_backend_t	*be = rpt.backend;
	pt_view_t		input;
	char			path[MAX_OSPATH];
	int				w, h, i;

	if (!be->plane_size || !be->read_plane)
	{
		if (!ex_warned)
			ri.Con_Printf (PRINT_ALL, "This renderer has no planes to export.\n");
		ex_warned = true;
		return false;
	}

	input = *view;
	input.samples = 1;
	input.bounces = 0;
	input.restart = 1;
	input.planes = 1;
	be->render_view (be, &input);
	if (!be->plane_size (be, &w, &h) || !Ex_Buffers (w, h))
		return false;
	for (i=0 ; i<PT_PLANE_LIGHT_DIFFUSE ; i++)
		if (be->read_plane (be, ex_planes[i].plane, ex_plane[i]) != ex_planes[i].channels)
			return false;

	for (i=0 ; i<passes ; i++)
	{
		view->restart = i == 0;
		be->render_view (be, view);
	}
	for (i=PT_PLANE_LIGHT_DIFFUSE ; i<PT_NUM_PLANES ; i++)
		if (be->read_plane (be, ex_planes[i].plane, ex_plane[i]) != ex_planes[i].channels)
			return false;

	Com_sprintf (path, sizeof(path), "%s/frame%05d.planes", dir, frame);
	if (!Ex_Write (path, fd, view, passes * view->samples, frame))
	{
		ri.Con_Printf (PRINT_ALL, "Couldn't write %s\n", path);
		return false;
	}
	return true;
}
