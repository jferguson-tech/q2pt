/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Jonathan Ferguson */

#include "pt_water.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct pt_water_s
{
	int			width, height;
	float		min_x, min_y;
	float		cell;			/* world units per cell */
	float		*h, *v;			/* height and its rate of change */
	float		*tmp;
	uint32_t	*waves, *caustics;
	unsigned char *open;	/* per cell: is there liquid here */
	int			covered;		/* has pt_water_cover been called */
	float		leftover;		/* time not yet simulated */
};

pt_water_t *pt_water_create(float min_x, float min_y, float max_x, float max_y, float cell_size, int max_cells)
{
	pt_water_t	*w;
	float		size_x = max_x - min_x, size_y = max_y - min_y, larger;
	size_t		count;

	if (size_x <= 0 || size_y <= 0 || cell_size <= 0 || max_cells < 4)
		return NULL;

	/* square cells, coarsened until the longer side fits */
	larger = size_x > size_y ? size_x : size_y;
	if (larger / cell_size > max_cells)
		cell_size = larger / max_cells;

	w = (pt_water_t *)calloc(1, sizeof(*w));
	if (!w)
		return NULL;
	w->min_x = min_x;
	w->min_y = min_y;
	w->cell = cell_size;
	w->width = (int)ceilf(size_x / cell_size);
	w->height = (int)ceilf(size_y / cell_size);
	if (w->width < 2) w->width = 2;
	if (w->height < 2) w->height = 2;

	count = (size_t)w->width * w->height;
	w->h = (float *)calloc(count, sizeof(float));
	w->v = (float *)calloc(count, sizeof(float));
	w->tmp = (float *)calloc(count, sizeof(float));
	w->waves = (uint32_t *)calloc(count, sizeof(uint32_t));
	w->caustics = (uint32_t *)calloc(count, sizeof(uint32_t));
	w->open = (unsigned char *)malloc(count);
	if (w->open)
		memset(w->open, 1, count);
	if (!w->open || !w->h || !w->v || !w->tmp || !w->waves || !w->caustics)
	{
		pt_water_destroy(w);
		return NULL;
	}
	return w;
}

void pt_water_destroy(pt_water_t *w)
{
	if (!w)
		return;
	free(w->h);
	free(w->v);
	free(w->tmp);
	free(w->waves);
	free(w->caustics);
	free(w->open);
	free(w);
}

int pt_water_width(const pt_water_t *w) { return w->width; }
int pt_water_height(const pt_water_t *w) { return w->height; }
float pt_water_cell(const pt_water_t *w) { return w->cell; }

void pt_water_disturb(pt_water_t *w, float x, float y, float radius, float amount)
{
	const float cx = (x - w->min_x) / w->cell, cy = (y - w->min_y) / w->cell;
	float r = radius / w->cell;
	int x0, y0, x1, y1, ix, iy;

	if (r < 1.5f)
		r = 1.5f;
	x0 = (int)floorf(cx - r); x1 = (int)ceilf(cx + r);
	y0 = (int)floorf(cy - r); y1 = (int)ceilf(cy + r);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > w->width - 1) x1 = w->width - 1;
	if (y1 > w->height - 1) y1 = w->height - 1;

	for (iy = y0; iy <= y1; iy++)
	{
		for (ix = x0; ix <= x1; ix++)
		{
			const float dx = ix + 0.5f - cx, dy = iy + 0.5f - cy;
			const float d = sqrtf(dx * dx + dy * dy) / r;
			if (d < 1.0f && w->open[(size_t)iy * w->width + ix])
			{
				/* a smooth dent, so it sends out a clean ring */
				const float fall = 0.5f * (1.0f + cosf(d * 3.14159265f));
				w->h[(size_t)iy * w->width + ix] -= amount * fall;
			}
		}
	}
}

/* a neighbour of a cell whose own value is self: the edge of the rectangle
   and cells without liquid act as walls, which waves bounce off */
static float at(const pt_water_t *w, const float *f, int x, int y, float self)
{
	if (x < 0 || y < 0 || x >= w->width || y >= w->height || !w->open[(size_t)y * w->width + x])
		return self;
	return f[(size_t)y * w->width + x];
}

void pt_water_cover(pt_water_t *w, const float a[2], const float b[2], const float c[2])
{
	const float *p[3];
	float e[3][3], lo[2], hi[2], area;
	int i, x, y, x0, y0, x1, y1;

	if (!w->covered)
	{
		memset(w->open, 0, (size_t)w->width * w->height);
		w->covered = 1;
	}

	p[0] = a; p[1] = b; p[2] = c;
	area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
	if (area == 0.0f)
		return;

	/* edge i as a line: distance = e[0]*x + e[1]*y + e[2], positive inside */
	for (i = 0; i < 3; i++)
	{
		const float *s = p[i], *t = p[(i + 1) % 3];
		float nx = -(t[1] - s[1]), ny = t[0] - s[0];
		const float len = sqrtf(nx * nx + ny * ny);
		if (len == 0.0f)
			return;
		if (area < 0.0f) { nx = -nx; ny = -ny; }
		e[i][0] = nx / len;
		e[i][1] = ny / len;
		e[i][2] = -(e[i][0] * s[0] + e[i][1] * s[1]);
	}

	for (i = 0; i < 2; i++)
	{
		lo[i] = a[i] < b[i] ? (a[i] < c[i] ? a[i] : c[i]) : (b[i] < c[i] ? b[i] : c[i]);
		hi[i] = a[i] > b[i] ? (a[i] > c[i] ? a[i] : c[i]) : (b[i] > c[i] ? b[i] : c[i]);
	}
	x0 = (int)floorf((lo[0] - w->min_x) / w->cell) - 1; x1 = (int)ceilf((hi[0] - w->min_x) / w->cell) + 1;
	y0 = (int)floorf((lo[1] - w->min_y) / w->cell) - 1; y1 = (int)ceilf((hi[1] - w->min_y) / w->cell) + 1;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > w->width - 1) x1 = w->width - 1;
	if (y1 > w->height - 1) y1 = w->height - 1;

	/* a cell counts if the triangle comes within half a cell of its middle,
	   so that neighbouring triangles leave no gaps between them */
	for (y = y0; y <= y1; y++)
	{
		for (x = x0; x <= x1; x++)
		{
			const float cx = w->min_x + (x + 0.5f) * w->cell, cy = w->min_y + (y + 0.5f) * w->cell;
			for (i = 0; i < 3; i++)
				if (e[i][0] * cx + e[i][1] * cy + e[i][2] < -0.5f * w->cell)
					break;
			if (i == 3)
				w->open[(size_t)y * w->width + x] = 1;
		}
	}
}

static void step_once(pt_water_t *w, float dt, float speed, float damping)
{
	const int width = w->width, height = w->height;
	/* the wave equation: acceleration is c^2 times how much the neighbours
	   stand above this cell */
	const float c2 = (speed * speed) / (w->cell * w->cell);
	const float keep = powf(1.0f - damping, dt);
	int x, y;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			const size_t i = (size_t)y * width + x;
			if (!w->open[i])
				continue;
			const float lap = at(w, w->h, x - 1, y, w->h[i]) + at(w, w->h, x + 1, y, w->h[i])
				+ at(w, w->h, x, y - 1, w->h[i]) + at(w, w->h, x, y + 1, w->h[i]) - 4.0f * w->h[i];
			w->v[i] = (w->v[i] + lap * c2 * dt) * keep;
		}
	}
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
		{
			const size_t i = (size_t)y * width + x;
			w->h[i] = (w->h[i] + w->v[i] * dt) * keep;
		}
}

void pt_water_step(pt_water_t *w, float dt, float speed, float damping)
{
	/* The explicit scheme is stable while a wave crosses less than about
	   0.7 of a cell per step, so the step is as small as that needs. */
	float step = 0.5f * w->cell / (speed > 1.0f ? speed : 1.0f);
	int guard = 0;

	if (damping < 0.0f) damping = 0.0f;
	if (damping > 0.999f) damping = 0.999f;
	if (dt > 0.1f)
		dt = 0.1f;		/* after a hitch, do not try to catch up */
	if (step > 1.0f / 60.0f)
		step = 1.0f / 60.0f;

	w->leftover += dt;
	while (w->leftover >= step && guard++ < 32)
	{
		step_once(w, step, speed, damping);
		w->leftover -= step;
	}
	if (guard >= 32)
		w->leftover = 0.0f;
}

static uint32_t byte_of(float v)
{
	if (v < 0.0f) v = 0.0f;
	if (v > 1.0f) v = 1.0f;
	return (uint32_t)(v * 255.0f + 0.5f);
}

const uint32_t *pt_water_waves(pt_water_t *w, float wave_scale)
{
	const float inv = wave_scale / (2.0f * w->cell);
	int x, y;

	for (y = 0; y < w->height; y++)
	{
		for (x = 0; x < w->width; x++)
		{
			const float here = w->h[(size_t)y * w->width + x];
			const float sx = (at(w, w->h, x + 1, y, here) - at(w, w->h, x - 1, y, here)) * inv;
			const float sy = (at(w, w->h, x, y + 1, here) - at(w, w->h, x, y - 1, here)) * inv;
			const float height = here * wave_scale;
			w->waves[(size_t)y * w->width + x] =
				byte_of(sx * PT_WATER_SLOPE_SCALE + 0.5f)
				| (byte_of(sy * PT_WATER_SLOPE_SCALE + 0.5f) << 8)
				| (byte_of(height * 0.125f + 0.5f) << 16)
				| 0xff000000u;
		}
	}
	return w->waves;
}

const uint32_t *pt_water_caustics(pt_water_t *w, float strength)
{
	/*
	Where the surface curves like a lens, hollow side up, it gathers the
	light passing through; where it bulges it spreads it. The curvature of
	the height field says which, and by how much.
	*/
	const float gain = strength * 6.0f / w->cell;
	int x, y, pass;

	for (y = 0; y < w->height; y++)
	{
		for (x = 0; x < w->width; x++)
		{
			const float here = w->h[(size_t)y * w->width + x];
			const float lap = at(w, w->h, x - 1, y, here) + at(w, w->h, x + 1, y, here)
				+ at(w, w->h, x, y - 1, here) + at(w, w->h, x, y + 1, here) - 4.0f * here;
			float bright = 1.0f + lap * gain;
			if (bright < 0.15f) bright = 0.15f;
			if (bright > PT_WATER_CAUSTIC_MAX) bright = PT_WATER_CAUSTIC_MAX;
			w->tmp[(size_t)y * w->width + x] = bright;
		}
	}

	/* light spreads a little on the way down */
	for (pass = 0; pass < 1; pass++)
	{
		for (y = 0; y < w->height; y++)
		{
			for (x = 0; x < w->width; x++)
			{
				const float here = w->tmp[(size_t)y * w->width + x];
				const float blurred = (at(w, w->tmp, x - 1, y, here) + at(w, w->tmp, x + 1, y, here)
					+ at(w, w->tmp, x, y - 1, here) + at(w, w->tmp, x, y + 1, here)
					+ 4.0f * here) * 0.125f;
				const uint32_t b = byte_of(blurred / PT_WATER_CAUSTIC_MAX);
				w->caustics[(size_t)y * w->width + x] = b | (b << 8) | (b << 16) | 0xff000000u;
			}
		}
	}
	return w->caustics;
}
