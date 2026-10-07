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
// rpt_export.c -- what a denoiser outside the game works from
//
// With pt_render_export set, a film (pt_render, see rpt_offline.c) is not
// saved as pictures but as the buffers behind them, one file a frame.
//
// The light is kept in the parts the tracer makes it in, because they want
// different treatment: the picture is
//     reflectance * diffuse light + specular reflectance * specular light
//         + layers + exact
// where the two lights have the surface's own colour divided out, "layers"
// is what see-through things in front and the air add, and "exact" is what
// has no noise in it at all (what a surface emits, the frame's point
// lights, the sky).
//
// Everything is linear light before exposure, glow and grading. Each pass is
// made from nothing by the backend and added up here, so the same code
// serves both backends, and with pt_render_blur the passes are spread over
// the time the shutter is open: every buffer then holds its average over
// that time, as the picture does.
//
// The file: a header of 256 bytes (see export_header_t), then 75 planes of
// width * height 16 bit floats, top row first.
//
//    0-47  four sets of 4 paths a pixel, each on its own, 12 planes a set:
//          diffuse light r g b, specular light r g b, layers r g b, then how
//          much the brightness of each of the three varied between the
//          set's paths (the variance of a single path). One set is a
//          picture of 4 paths, two together one of 8, all four one of 16. A
//          film of fewer than 16 paths has fewer sets (the header says how
//          many) and the rest are 0.
//   48-50  exact light
//   51-53  diffuse reflectance
//   54-56  specular reflectance
//   57-59  normal
//   60     distance, under 0 where there is nothing
//   61-62  x and y of where it was on the last frame, less where it is now;
//          EXPORT_NO_MOTION where that is not known. On a blurred frame
//          "now" is the middle of the time the shutter was open and "the
//          last frame" the moment that one's shutter closed, so the way
//          from one blurred picture to the next is this over (1 - blur / 2).
//   63-71  diffuse light, specular light and layers from all the paths
//          asked for
//   72-74  the picture from all the paths: what the noisy sets would come
//          to, for training against and for comparing with

#include "rpt_local.h"

#define	EXPORT_SETS			4
#define	EXPORT_SET_PATHS	4
#define	EXPORT_SET_PLANES	12
#define	EXPORT_ONCE			15		// exact, two reflectances, normal, distance, motion
#define	EXPORT_ALL			12		// three lights and the picture from all the paths
#define	EXPORT_PLANES		(EXPORT_SETS * EXPORT_SET_PLANES + EXPORT_ONCE + EXPORT_ALL)
#define	EXPORT_PASS_PATHS	32		// paths a pass once the sets are made
#define	EXPORT_NO_MOTION	30000.0f

typedef struct
{
	char	magic[4];			// "PTXB"
	int		version;			// 2
	int		width, height;
	int		planes;
	int		sets, set_paths;
	int		paths;				// in the last twelve planes
	int		flags;				// 1: follows the frame before (the motion means something); 2: blurred
	int		frame;
	float	time;
	float	blur;				// share of the frame's time the shutter was open
	float	camera[14];			// origin, forward, right, up, fov_x, fov_y
	float	last_camera[14];	// the frame before, if flags & 1
	char	pad[256 - 48 - 112];
} export_header_t;

static cvar_t	*pt_render_export;

static int		exp_width, exp_height;
static float	*exp_read;				// one buffer as the backend gives it
static float	*exp_sets;				// EXPORT_SETS * EXPORT_SET_PLANES planes, summed
static float	*exp_once;				// 12 planes summed: exact, reflectances, normal
static float	*exp_depth;				// summed where there is a surface
static float	*exp_depth_count;
static float	*exp_position;			// 3 planes, summed
static float	*exp_all;				// EXPORT_ALL planes, weighted by paths
static qboolean	exp_ready;				// a frame is waiting to be written
static export_header_t exp_header;

void R_InitExport (void)
{
	pt_render_export = ri.Cvar_Get ("pt_render_export", "0", 0);
}

void R_ShutdownExport (void)
{
	free (exp_read);
	free (exp_sets);
	free (exp_once);
	free (exp_depth);
	free (exp_depth_count);
	free (exp_position);
	free (exp_all);
	exp_read = exp_sets = exp_once = exp_depth = exp_depth_count = exp_position = exp_all = NULL;
	exp_width = exp_height = 0;
	exp_ready = false;
}

qboolean R_Exporting (void)
{
	return pt_render_export->value != 0;
}

static qboolean Exp_Room (int width, int height)
{
	size_t	size = (size_t)width * height;

	if (width == exp_width && height == exp_height && exp_all)
		return true;
	R_ShutdownExport ();
	exp_read = malloc (size * 4 * sizeof(float));
	exp_sets = malloc (size * EXPORT_SETS * EXPORT_SET_PLANES * sizeof(float));
	exp_once = malloc (size * 12 * sizeof(float));
	exp_depth = malloc (size * sizeof(float));
	exp_depth_count = malloc (size * sizeof(float));
	exp_position = malloc (size * 3 * sizeof(float));
	exp_all = malloc (size * EXPORT_ALL * sizeof(float));
	if (!exp_read || !exp_sets || !exp_once || !exp_depth || !exp_depth_count || !exp_position || !exp_all)
	{
		R_ShutdownExport ();
		return false;
	}
	exp_width = width;
	exp_height = height;
	return true;
}

// the bits of k the other way round, as a fraction: 0, 1/2, 1/4, 3/4, ...
// so that however many passes there are, they lie evenly over the time
static float Exp_Spread (unsigned k)
{
	float	f = 0.5f, r = 0;

	for ( ; k ; k >>= 1, f *= 0.5f)
		if (k & 1)
			r += f;
	return r;
}

static void Exp_Camera (float *out, const float *origin, const float *forward, const float *right, const float *up,
	float fov_x, float fov_y)
{
	memcpy (out, origin, 3 * sizeof(float));
	memcpy (out + 3, forward, 3 * sizeof(float));
	memcpy (out + 6, right, 3 * sizeof(float));
	memcpy (out + 9, up, 3 * sizeof(float));
	out[12] = fov_x;
	out[13] = fov_y;
}

// reads one of the backend's buffers and adds its colour, times weight, to
// three planes; with squares, the square of its brightness to a fourth
static qboolean Exp_Add (int buffer, float *planes, float weight, float *squares)
{
	size_t	size = (size_t)exp_width * exp_height, i;
	int		width, height, c;
	float	lum;

	if (!rpt.backend->read_buffer (rpt.backend, buffer, exp_read, exp_width * exp_height, &width, &height)
		|| width != exp_width || height != exp_height)
		return false;
	for (c=0 ; c<3 ; c++)
		for (i=0 ; i<size ; i++)
			planes[size * c + i] += exp_read[i * 4 + c] * weight;
	if (squares)
	{
		for (i=0 ; i<size ; i++)
		{
			lum = 0.2126f * exp_read[i * 4] + 0.7152f * exp_read[i * 4 + 1] + 0.0722f * exp_read[i * 4 + 2];
			squares[i] += lum * lum;
		}
	}
	return true;
}

/*
===============
R_ExportRender

Makes the frame's buffers. last is the eye of the frame before, or NULL if
this frame does not follow on from it.
===============
*/
qboolean R_ExportRender (const pt_view_t *view, int paths, float blur, const pt_camera_t *last)
{
	static const int lights[3] = {PT_BUFFER_DIFFUSE_LIGHT, PT_BUFFER_SPECULAR_LIGHT, PT_BUFFER_LAYERS};
	pt_view_t	pass, base;
	pt_scene_t	moment_scene;
	int			width = 0, height = 0;
	int			k, set, made, n, c, sets;
	size_t		size, i;
	float		t, *planes;

	exp_ready = false;
	if (!rpt.backend->read_buffer)
		return false;

	// nothing may be carried over, filtered or favoured: the passes are
	// independent samples of the same picture
	base = *view;
	base.scale = 1;
	base.restart = 1;
	base.denoise = 0;
	base.auto_exposure = 0;
	base.exposure = 1;
	base.bloom = 0;
	base.debug = 0;
	base.antialias = 1;

	if (blur > 0)
		R_OfflineMomentsBegin (view);

	// as many sets as the paths asked for fill, and at least one
	sets = (paths + EXPORT_SET_PATHS - 1) / EXPORT_SET_PATHS;
	if (sets < 1)
		sets = 1;
	if (sets > EXPORT_SETS)
		sets = EXPORT_SETS;

	made = 0;
	for (k=0 ; made<paths || k<sets*EXPORT_SET_PATHS ; k++)
	{
		set = k < sets * EXPORT_SET_PATHS ? k / EXPORT_SET_PATHS : -1;
		n = set >= 0 ? 1 : (paths - made < EXPORT_PASS_PATHS ? paths - made : EXPORT_PASS_PATHS);

		if (blur > 0)
		{
			// 1 is now, 0 the frame before; the shutter closes now
			t = 1 - blur * (1 - Exp_Spread (k));
			R_OfflineMoment (&base, t, &pass, &moment_scene);
			// where each point was a frame ago is still wanted: it is what
			// says how it moved over the picture
			if (pass.scene == &moment_scene)
				moment_scene.prev_positions = view->scene->prev_positions;
		}
		else
			pass = base;
		pass.samples = n;
		rpt.backend->render_view (rpt.backend, &pass);

		if (k == 0)
		{
			// the size of what was traced
			rpt.backend->read_buffer (rpt.backend, PT_BUFFER_COLOUR, NULL, 0, &width, &height);
			if (width <= 0 || height <= 0 || !Exp_Room (width, height))
				return false;
			size = (size_t)width * height;
			memset (exp_sets, 0, size * EXPORT_SETS * EXPORT_SET_PLANES * sizeof(float));
			memset (exp_once, 0, size * 12 * sizeof(float));
			memset (exp_depth, 0, size * sizeof(float));
			memset (exp_depth_count, 0, size * sizeof(float));
			memset (exp_position, 0, size * 3 * sizeof(float));
			memset (exp_all, 0, size * EXPORT_ALL * sizeof(float));
		}
		size = (size_t)exp_width * exp_height;

		// the three noisy lights: into the sum of all the paths, and into
		// their set with the squares the variance comes from
		for (c=0 ; c<3 ; c++)
		{
			planes = set >= 0 ? exp_sets + size * (set * EXPORT_SET_PLANES + c * 3) : NULL;
			if (!Exp_Add (lights[c], exp_all + size * c * 3, n, NULL))
				return false;
			if (planes)
			{
				// what was just read is still in exp_read
				float	*squares = exp_sets + size * (set * EXPORT_SET_PLANES + 9 + c);
				float	lum;
				int		ch;

				for (ch=0 ; ch<3 ; ch++)
					for (i=0 ; i<size ; i++)
						planes[size * ch + i] += exp_read[i * 4 + ch];
				for (i=0 ; i<size ; i++)
				{
					lum = 0.2126f * exp_read[i * 4] + 0.7152f * exp_read[i * 4 + 1] + 0.0722f * exp_read[i * 4 + 2];
					squares[i] += lum * lum;
				}
			}
		}
		if (!Exp_Add (PT_BUFFER_COLOUR, exp_all + size * 9, n, NULL))
			return false;
		made += n;
		if (set < 0)
			continue;

		if (!Exp_Add (PT_BUFFER_EXACT, exp_once, 1, NULL)
			|| !Exp_Add (PT_BUFFER_ALBEDO, exp_once + size * 3, 1, NULL)
			|| !Exp_Add (PT_BUFFER_SPECULAR, exp_once + size * 6, 1, NULL)
			|| !Exp_Add (PT_BUFFER_NORMAL, exp_once + size * 9, 1, NULL))
			return false;
		// the distance came with the normal
		for (i=0 ; i<size ; i++)
		{
			if (exp_read[i * 4 + 3] >= 0)
			{
				exp_depth[i] += exp_read[i * 4 + 3];
				exp_depth_count[i] += 1;
			}
		}
		if (!Exp_Add (PT_BUFFER_POSITION, exp_position, 1, NULL))
			return false;
	}

	memset (&exp_header, 0, sizeof(exp_header));
	memcpy (exp_header.magic, "PTXB", 4);
	exp_header.version = 2;
	exp_header.width = exp_width;
	exp_header.height = exp_height;
	exp_header.planes = EXPORT_PLANES;
	exp_header.sets = sets;
	exp_header.set_paths = EXPORT_SET_PATHS;
	exp_header.paths = made;
	exp_header.flags = (last ? 1 : 0) | (blur > 0 ? 2 : 0);
	exp_header.time = view->time;
	exp_header.blur = blur;
	Exp_Camera (exp_header.camera, view->origin, view->forward, view->right, view->up, view->fov_x, view->fov_y);
	if (last)
		Exp_Camera (exp_header.last_camera, last->origin, last->forward, last->right, last->up, last->fov_x, last->fov_y);
	exp_ready = true;
	return true;
}

// to a 16 bit float, rounded to the nearest
static unsigned short Exp_Half (float f)
{
	union { float f; unsigned u; } v;
	unsigned	sign, mantissa;
	int			exponent;

	v.f = f;
	sign = (v.u >> 16) & 0x8000;
	exponent = (int)((v.u >> 23) & 0xff) - 127 + 15;
	mantissa = v.u & 0x7fffff;

	if (exponent >= 31)
	{
		if (((v.u >> 23) & 0xff) == 0xff && mantissa)
			return sign;				// not a number: nothing
		return sign | 0x7bff;			// too big: the biggest there is
	}
	if (exponent <= 0)
	{
		if (exponent < -10)
			return sign;				// too small: nothing
		mantissa = (mantissa | 0x800000) >> (1 - exponent);
		return sign | ((mantissa + 0x1000) >> 13);
	}
	// a carry out of the mantissa goes into the exponent, as it should
	return sign | ((exponent << 10) + ((mantissa + 0x1000) >> 13));
}

/*
===============
R_ExportWrite

After the frame has been shown: save its buffers
===============
*/
qboolean R_ExportWrite (const char *path, int frame)
{
	FILE			*f;
	unsigned short	*row;
	const float		*last = exp_header.last_camera;
	const float		*last_forward = last + 3, *last_right = last + 6, *last_up = last + 9;
	size_t			size, i;
	int				p, x, y, width = exp_width, height = exp_height, low;
	float			scale, tx, ty, z, v[3], motion[2], mean, value;
	qboolean		ok = true;

	// between maps there is no view and nothing to save: the frame is
	// simply not there, and the one after does not follow on
	if (!exp_ready)
		return true;
	exp_ready = false;
	exp_header.frame = frame;

	size = (size_t)width * height;
	row = malloc (width * sizeof(unsigned short));
	f = fopen (path, "wb");
	if (!f || !row)
	{
		if (f)
			fclose (f);
		free (row);
		return false;
	}
	ok = fwrite (&exp_header, sizeof(exp_header), 1, f) == 1;

#define	WRITE_ROW()	(ok = ok && fwrite (row, sizeof(unsigned short), width, f) == (size_t)width)

	// the sets: nine planes of light, then three of how much it varied
	scale = 1.0f / EXPORT_SET_PATHS;
	for (p=0 ; p<EXPORT_SETS*EXPORT_SET_PLANES ; p++)
	{
		int		set = p / EXPORT_SET_PLANES, plane = p % EXPORT_SET_PLANES;
		const float	*sum = exp_sets + size * (set * EXPORT_SET_PLANES + (plane - 9) * 3);

		for (y=0 ; y<height ; y++)
		{
			for (x=0 ; x<width ; x++)
			{
				i = (size_t)y * width + x;
				if (plane < 9)
					value = exp_sets[size * p + i] * scale;
				else
				{
					// of one path: from the mean of the squares and the square of the mean
					mean = (0.2126f * sum[i] + 0.7152f * sum[size + i] + 0.0722f * sum[size * 2 + i]) * scale;
					value = (exp_sets[size * p + i] * scale - mean * mean) * (EXPORT_SET_PATHS / (EXPORT_SET_PATHS - 1.0f));
					if (value < 0)
						value = 0;
				}
				row[x] = Exp_Half (value);
			}
			WRITE_ROW ();
		}
	}

	// exact light, the two reflectances, the normal
	low = exp_header.sets * EXPORT_SET_PATHS;
	scale = 1.0f / low;
	for (p=0 ; p<12 ; p++)
	{
		for (y=0 ; y<height ; y++)
		{
			for (x=0 ; x<width ; x++)
				row[x] = Exp_Half (exp_once[size * p + (size_t)y * width + x] * scale);
			WRITE_ROW ();
		}
	}

	for (y=0 ; y<height ; y++)
	{
		for (x=0 ; x<width ; x++)
		{
			i = (size_t)y * width + x;
			row[x] = Exp_Half (exp_depth_count[i] > 0 ? exp_depth[i] / exp_depth_count[i] : -1);
		}
		WRITE_ROW ();
	}

	// where each point was on the last frame's picture
	tx = tan (last[12] * M_PI / 360.0);
	ty = tan (last[13] * M_PI / 360.0);
	for (p=0 ; p<2 ; p++)
	{
		for (y=0 ; y<height ; y++)
		{
			for (x=0 ; x<width ; x++)
			{
				i = (size_t)y * width + x;
				motion[0] = motion[1] = EXPORT_NO_MOTION;
				if ((exp_header.flags & 1) && exp_depth_count[i] > 0)
				{
					v[0] = exp_position[i] * scale - last[0];
					v[1] = exp_position[size + i] * scale - last[1];
					v[2] = exp_position[size * 2 + i] * scale - last[2];
					z = DotProduct (v, last_forward);
					if (z > 0.01f)
					{
						motion[0] = (DotProduct (v, last_right) / (z * tx) * 0.5f + 0.5f) * width - 0.5f - x;
						motion[1] = (0.5f - DotProduct (v, last_up) / (z * ty) * 0.5f) * height - 0.5f - y;
						if (fabs (motion[0]) > 20000 || fabs (motion[1]) > 20000)
							motion[0] = motion[1] = EXPORT_NO_MOTION;
					}
				}
				row[x] = Exp_Half (motion[p]);
			}
			WRITE_ROW ();
		}
	}

	scale = exp_header.paths > 0 ? 1.0f / exp_header.paths : 0;
	for (p=0 ; p<EXPORT_ALL ; p++)
	{
		for (y=0 ; y<height ; y++)
		{
			for (x=0 ; x<width ; x++)
				row[x] = Exp_Half (exp_all[size * p + (size_t)y * width + x] * scale);
			WRITE_ROW ();
		}
	}
#undef WRITE_ROW

	if (fclose (f))
		ok = false;
	free (row);
	return ok;
}
