/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Jonathan Ferguson */

#include "pt_material.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI				3.14159265358979f

/*
Everything below is measured in texels of the picture and in natural log
units of brightness, so a value means the same on a 16 texel button and a
256 texel wall.
*/

#define LOG_FLOOR		0.01f	/* added to brightness before its log: black is not infinitely dark */

/* the dither and noise of 256 colour art is taken out before anything is measured */
#define NOISE_RADIUS	2
#define NOISE_SPACE		1.2f	/* how far the smoothing reaches */
#define NOISE_STEP		0.25f	/* brightness steps smaller than this are noise, larger are edges */

/* detail is what is left between these two blurs: finer is noise, wider is not relief */
#define FINE			0.6f
#define WIDE			3.0f

/* what is this much darker than the picture as a whole is a hole or a gap */
#define HOLE_BELOW		1.0f
#define HOLE_RANGE		0.5f
#define HOLE_DEPTH		0.6f	/* and lies this deep */

/* where the colour itself changes the brightness change is paint, not light */
#define PAINT_COLOUR	0.05f	/* a change of chromaticity this large is certainly paint */
#define COLOUR_KNOWN	0.06f	/* below this much light a pixel's colour cannot be told */

/* painted light: a highlight is a slope towards the light, a shadow one away from it */
#define SHADE_NOISE		0.06f	/* smaller differences are not followed */
#define SHADE_MOST		0.7f	/* more than light and shade explain: let go of, not followed */
#define LIGHT_X			(-0.70710678f)	/* towards the light: the top left */
#define LIGHT_Y			(-0.70710678f)
#define CARRY			0.45f	/* 1 / how far a step found at a highlight carries */
#define ACROSS			0.5f	/* holds neighbouring lines of the picture together */
#define PAINTED			1.0f	/* how much of the height comes from painted light ... */
#define DARK_DEEP		0.5f	/* ... and how much from dark being deep */
#define DARK_DEEP_ALONE	0.8f	/* where there is no painted light to read */
#define BRIGHT_RAISED	0.3f	/* a bright spot is raised this much as surely as a dark one is sunk */
#define DEEP_MOST		1.4f

#define SOFTEN			0.6f	/* last, the height is smoothed this much */
#define RELIEF			2.0f	/* slope given for a slope read, at a bump of 1 */
#define STEEPEST		1.5f	/* slopes level off towards this */

/* roughness: grooves hold dirt, rust and wood are matte, what was painted lighter is polished */
#define ROUGH_GROOVE	0.15f
#define ROUGH_WARM		0.12f
#define ROUGH_BRIGHT	0.10f
#define ROUGH_SOFTEN	0.8f
#define ROUGH_LESS		0.15f	/* the most a texel is made smoother than the material ... */
#define ROUGH_MORE		0.3f	/* ... and the most it is made rougher */

/*
Metal. Bare steel was painted grey, or faintly blue or brown; rust, paint,
wood, cloth and skin were painted vivid. A texel is metal or it is not: the
line is drawn through how vivid it is as it is shown, 1 - least / most of
its red, green and blue.
*/
#define COLOUR_SPREAD	1.5f	/* the dither's colours are averaged this far before they are judged */
#define VIVID_FLOOR		0.02f	/* added under the brightest of the three: black is not vivid */
#define METAL_LINE		0.22f	/* this vivid or more is not metal ... */
#define METAL_LINE_SURE	0.28f	/* ... or this, in a picture known to be of metal */
#define ONE_KIND_FROM	0.06f	/* a picture whose texels differ less than this in how vivid they are is all
								   of one kind, and is judged as a whole by its average; one whose texels ... */
#define ONE_KIND_TO		0.11f	/* ... differ by this or more is judged texel by texel */
#define TOO_DARK		0.05f	/* shown darker than this is a gap or black rubber, whatever its colour */
#define DARK_COUNTS		0.1f	/* and below this a texel counts for less in the picture's average */
#define METAL_PATCH		1.0f	/* metal and what covers it come in patches at least about this far across */
#define METAL_EDGE		1.0f	/* texels of the map the edge of a patch is spread over */
#define METAL_SOME		0.05f	/* a picture this much metal or more has the rest of it for a covering */
#define COVER_ROUGHER	0.25f	/* what covers metal is this much rougher than the metal */

/* the painted light the colours are relieved of */
#define LIT_MOST		1.2f	/* a difference well past this is a lamp or a marking, not a highlight */
#define SHADOW_KEPT		0.5f	/* what is darker may be dirt or depth as well as shadow: this much of it stays */

#define MARGIN			8		/* texels of the picture's own continuation put round one that is padded */
#define LARGEST			1024	/* repeating pictures wider or higher than this get a map of their own size */
#define LARGEST_AT_ALL	2048	/* and beyond this none */

typedef struct
{
	int		w, h;		/* the picture */
	int		gw, gh;		/* the grid worked on, a power of two each way */
	int		ox, oy;		/* where the picture starts in it */
	int		scale;		/* the map is this many times finer */
	size_t	count;		/* gw * gh */
} grid_t;

static float clamp01(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static float smoothstep(float a, float b, float v)
{
	const float t = clamp01((v - a) / (b - a));

	return t * t * (3.0f - 2.0f * t);
}

/* v where it is small, falling back to nothing where it is far past most */
static float let_go(float v, float most)
{
	const float r = v / most;

	return v / (1.0f + r * r * r * r);
}

static int is_power_of_two(int n)
{
	return n > 0 && (n & (n - 1)) == 0;
}

/*
One axis of the grid. A picture that repeats and is a power of two long is
worked on as it is, round and round. Any other is set in the middle of a
longer grid, with its own continuation on either side: itself again if it
repeats, its mirror image if it does not.
*/
static int grid_axis(int n, int repeats, int *start)
{
	int		size = 1;

	if (repeats && is_power_of_two(n))
	{
		*start = 0;
		return n;
	}
	while (size < n + 2 * MARGIN)
		size *= 2;
	*start = (size - n) / 2;
	return size;
}

/* the picture's pixel that grid position i (counted from the picture's first) shows */
static int picture_index(int i, int n, int repeats)
{
	if (repeats)
	{
		i %= n;
		return i < 0 ? i + n : i;
	}
	i %= 2 * n;
	if (i < 0)
		i += 2 * n;
	return i < n ? i : 2 * n - 1 - i;
}

/* a Gaussian blur of a grid that goes round at its edges; in and out may be the same */
static void blur(const float *in, float *out, float *tmp, const grid_t *g, float sigma)
{
	float	kernel[64], sum = 0.0f, v;
	int		r = (int)ceilf(sigma * 3.0f), i, x, y;
	const int w = g->gw, h = g->gh;

	if (r > 31)
		r = 31;
	for (i = -r; i <= r; i++)
	{
		kernel[i + r] = expf(-(float)(i * i) / (2.0f * sigma * sigma));
		sum += kernel[i + r];
	}
	for (i = 0; i <= 2 * r; i++)
		kernel[i] /= sum;

	for (y = 0; y < h; y++)
	{
		const float *row = in + (size_t)y * w;

		for (x = 0; x < w; x++)
		{
			v = 0.0f;
			for (i = -r; i <= r; i++)
				v += kernel[i + r] * row[(x + i) & (w - 1)];
			tmp[(size_t)y * w + x] = v;
		}
	}
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			v = 0.0f;
			for (i = -r; i <= r; i++)
				v += kernel[i + r] * tmp[(size_t)((y + i) & (h - 1)) * w + x];
			out[(size_t)y * w + x] = v;
		}
	}
}

/*
Smooths away differences smaller than NOISE_STEP and leaves larger ones
standing, so the speckle of dithering goes and a one texel highlight stays.
*/
static void remove_noise(const float *in, float *out, const grid_t *g)
{
	float	space[2 * NOISE_RADIUS + 1][2 * NOISE_RADIUS + 1];
	const float range = 1.0f / (2.0f * NOISE_STEP * NOISE_STEP);
	const int w = g->gw, h = g->gh;
	int		x, y, dx, dy;

	for (dy = -NOISE_RADIUS; dy <= NOISE_RADIUS; dy++)
		for (dx = -NOISE_RADIUS; dx <= NOISE_RADIUS; dx++)
			space[dy + NOISE_RADIUS][dx + NOISE_RADIUS] = expf(-(float)(dx * dx + dy * dy) / (2.0f * NOISE_SPACE * NOISE_SPACE));

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			const float here = in[(size_t)y * w + x];
			float	sum = 0.0f, weights = 0.0f;

			for (dy = -NOISE_RADIUS; dy <= NOISE_RADIUS; dy++)
			{
				const float *row = in + (size_t)((y + dy) & (h - 1)) * w;

				for (dx = -NOISE_RADIUS; dx <= NOISE_RADIUS; dx++)
				{
					const float there = row[(x + dx) & (w - 1)];
					const float weight = space[dy + NOISE_RADIUS][dx + NOISE_RADIUS] * expf(-(there - here) * (there - here) * range);

					sum += weight * there;
					weights += weight;
				}
			}
			out[(size_t)y * w + x] = sum / weights;
		}
	}
}

/* the brightness half of the grid is darker than: what the picture is mostly made of */
static float typical(const float *v, size_t count)
{
	enum { BINS = 512 };
	const float lo = logf(LOG_FLOOR), hi = logf(1.0f + LOG_FLOOR);
	size_t	bins[BINS], i, seen = 0;
	int		b;

	memset(bins, 0, sizeof(bins));
	for (i = 0; i < count; i++)
	{
		b = (int)((v[i] - lo) / (hi - lo) * BINS);
		bins[b < 0 ? 0 : (b >= BINS ? BINS - 1 : b)]++;
	}
	for (b = 0; b < BINS - 1; b++)
	{
		seen += bins[b];
		if (seen * 2 >= count)
			break;
	}
	return lo + ((float)b + 0.5f) * (hi - lo) / BINS;
}

/*
A Fourier transform of n complex numbers, in place, n a power of two.
table holds the cosine and sine of 2 pi j / table_n for j under table_n / 2.
*/
static void transform(float *a, int n, const float *table, int table_n, int inverse)
{
	const int step = table_n / n;
	int		i, j, k, bit, len;

	for (i = 1, j = 0; i < n; i++)
	{
		for (bit = n >> 1; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
		{
			const float re = a[2 * i], im = a[2 * i + 1];

			a[2 * i] = a[2 * j];
			a[2 * i + 1] = a[2 * j + 1];
			a[2 * j] = re;
			a[2 * j + 1] = im;
		}
	}
	for (len = 2; len <= n; len <<= 1)
	{
		const int half = len / 2, stride = step * (n / len);

		for (i = 0; i < n; i += len)
		{
			for (k = 0; k < half; k++)
			{
				const float wr = table[2 * k * stride];
				const float wi = inverse ? table[2 * k * stride + 1] : -table[2 * k * stride + 1];
				float	*p = a + 2 * (i + k), *q = a + 2 * (i + k + half);
				const float re = q[0] * wr - q[1] * wi, im = q[0] * wi + q[1] * wr;

				q[0] = p[0] - re;
				q[1] = p[1] - im;
				p[0] += re;
				p[1] += im;
			}
		}
	}
}

/* the same of a w by h grid; line has room for 2 * h */
static void transform_grid(float *a, int w, int h, const float *table, int table_n, int inverse, float *line)
{
	int		x, y;

	for (y = 0; y < h; y++)
		transform(a + 2 * (size_t)y * w, w, table, table_n, inverse);
	for (x = 0; x < w; x++)
	{
		for (y = 0; y < h; y++)
		{
			line[2 * y] = a[2 * ((size_t)y * w + x)];
			line[2 * y + 1] = a[2 * ((size_t)y * w + x) + 1];
		}
		transform(line, h, table, table_n, inverse);
		for (y = 0; y < h; y++)
		{
			a[2 * ((size_t)y * w + x)] = line[2 * y];
			a[2 * ((size_t)y * w + x) + 1] = line[2 * y + 1];
		}
	}
}

/* radians per texel of place j of n; the one place that stands for both signs is left out */
static int frequency(int j, int n, float *k)
{
	if (n > 1 && j == n / 2)
		return 0;
	*k = (float)(j < n / 2 || n == 1 ? j : j - n) * (2.0f * PI / (float)n);
	return 1;
}

static int set_grid(grid_t *g, int width, int height, int repeats)
{
	if (width <= 0 || height <= 0 || width > LARGEST_AT_ALL || height > LARGEST_AT_ALL)
		return 0;
	g->w = width;
	g->h = height;
	g->gw = grid_axis(width, repeats, &g->ox);
	g->gh = grid_axis(height, repeats, &g->oy);
	g->scale = pt_material_detail_scale(width, height, repeats);
	g->count = (size_t)g->gw * g->gh;
	return 1;
}

int pt_material_detail_scale(int width, int height, int repeats)
{
	return (!repeats || width > LARGEST || height > LARGEST) ? 1 : 2;
}

/*
What is read from the picture. slopes: gw * scale by gh * scale pairs, the
height's slope to the right and down the picture, or with want_height its
height and nothing. The rest is on the grid itself. rough: how much rougher
or smoother than the picture's average each texel is. metal: how much of
what lies about each texel is metal, 0 - 1, so that 0.5 is the edge of a
patch; metal_share is how much of the picture is. lit: the painted light
found there, in log units, above 0 for a highlight and below for a shadow.
All NULL if there is no memory.
*/
typedef struct
{
	grid_t	g;
	float	*slopes;
	float	*rough;
	float	*metal;
	float	*lit;
	float	metal_share;
} reading_t;

static void release(reading_t *r)
{
	free(r->slopes);
	free(r->rough);
	free(r->metal);
	free(r->lit);
	r->slopes = r->rough = r->metal = r->lit = NULL;
}

static int read_picture(reading_t *r, const uint32_t *pixels, int width, int height, const pt_material_from_t *from, int want_height)
{
	grid_t	*g = &r->g;
	float	linear[256];
	float	*block, *lum, *clean, *wide, *band, *tmp, *cr, *cg, *known, *warm, *paint, *ar, *ag, *ab, *z, *line, *table;
	float	body, mean;
	size_t	i, big_count;
	int		x, y, bw, bh, table_n;

	r->slopes = r->rough = r->metal = r->lit = NULL;
	if (!set_grid(g, width, height, from->repeats))
		return 0;

	bw = g->gw * g->scale;
	bh = g->gh * g->scale;
	big_count = (size_t)bw * bh;
	table_n = bw > bh ? bw : bh;

	block = (float *)malloc((g->count * 15 + (size_t)bh * 2 + (size_t)table_n) * sizeof(float));
	r->slopes = (float *)calloc(big_count * 2, sizeof(float));
	r->rough = (float *)malloc(g->count * sizeof(float));
	r->metal = (float *)malloc(g->count * sizeof(float));
	r->lit = (float *)malloc(g->count * sizeof(float));
	if (!block || !r->slopes || !r->rough || !r->metal || !r->lit)
	{
		free(block);
		release(r);
		return 0;
	}
	lum = block;
	clean = lum + g->count;
	wide = clean + g->count;
	band = wide + g->count;
	tmp = band + g->count;
	cr = tmp + g->count;
	cg = cr + g->count;
	known = cg + g->count;
	warm = known + g->count;
	paint = warm + g->count;
	ar = paint + g->count;
	ag = ar + g->count;
	ab = ag + g->count;
	z = ab + g->count;				/* 2 * count */
	line = z + 2 * g->count;		/* 2 * bh */
	table = line + 2 * (size_t)bh;	/* table_n */

	for (x = 0; x < 256; x++)
		linear[x] = powf((float)x / 255.0f, 2.2f);
	for (x = 0; x < table_n / 2; x++)
	{
		table[2 * x] = cosf(2.0f * PI * (float)x / (float)table_n);
		table[2 * x + 1] = sinf(2.0f * PI * (float)x / (float)table_n);
	}

	/* each texel's brightness, its colour apart from brightness, and whether it is rust or wood coloured */
	for (y = 0; y < g->gh; y++)
	{
		const uint32_t *row = pixels + (size_t)picture_index(y - g->oy, height, from->repeats) * width;

		for (x = 0; x < g->gw; x++)
		{
			const uint32_t c = row[picture_index(x - g->ox, width, from->repeats)];
			const int	r8 = c & 0xff, g8 = (c >> 8) & 0xff, b8 = (c >> 16) & 0xff;
			const float red = linear[r8], green = linear[g8], blue = linear[b8];
			const float sum = red + green + blue + 1.0e-4f;
			const int	most = r8 > g8 ? (r8 > b8 ? r8 : b8) : (g8 > b8 ? g8 : b8);
			const int	least = r8 < g8 ? (r8 < b8 ? r8 : b8) : (g8 < b8 ? g8 : b8);

			i = (size_t)y * g->gw + x;
			lum[i] = logf(red * 0.2126f + green * 0.7152f + blue * 0.0722f + LOG_FLOOR);
			known[i] = clamp01(sum / COLOUR_KNOWN);
			cr[i] = red / sum * known[i];
			cg[i] = green / sum * known[i];
			ar[i] = red;
			ag[i] = green;
			ab[i] = blue;
			warm[i] = 0.0f;
			if (r8 >= g8 && g8 >= b8 && most > 0)
				warm[i] = smoothstep(0.3f, 0.55f, (float)(most - least) / (float)most)
					* clamp01(((float)most / 255.0f - 0.08f) / 0.1f);
		}
	}

	remove_noise(lum, clean, g);
	body = typical(clean, g->count);
	blur(clean, wide, tmp, g, WIDE);
	blur(clean, band, tmp, g, FINE);
	for (i = 0; i < g->count; i++)
	{
		band[i] -= wide[i];
		r->lit[i] = clean[i] - wide[i];		/* as sharp as it was painted; finished below */
	}

	/* roughness, while the pieces are at hand */
	for (i = 0; i < g->count; i++)
		r->rough[i] = ROUGH_GROOVE * clamp01(-band[i] / 0.6f) + ROUGH_WARM * warm[i]
			- ROUGH_BRIGHT * (clamp01((wide[i] - body) / 1.4f + 0.5f) * 2.0f - 1.0f);
	blur(r->rough, r->rough, tmp, g, ROUGH_SOFTEN);
	mean = 0.0f;
	for (i = 0; i < g->count; i++)
		mean += r->rough[i];
	mean /= (float)g->count;
	for (i = 0; i < g->count; i++)
		r->rough[i] -= mean;

	/* metal */
	r->metal_share = 0.0f;
	if (from->metallic > 0.0f)
	{
		const float limit = from->metal_known ? METAL_LINE_SURE : METAL_LINE;
		const float dark = logf(powf(TOO_DARK, 2.2f) + LOG_FLOOR);
		double	seen = 0.0, sum = 0.0, squares = 0.0;
		float	*vivid = r->metal;
		float	spread, own;
		size_t	metal = 0;

		blur(ar, ar, tmp, g, COLOUR_SPREAD);
		blur(ag, ag, tmp, g, COLOUR_SPREAD);
		blur(ab, ab, tmp, g, COLOUR_SPREAD);

		/* how vivid each texel is, and over the picture itself how vivid on average and how varied */
		for (y = 0; y < g->gh; y++)
		{
			for (x = 0; x < g->gw; x++)
			{
				float	hi, lo;

				i = (size_t)y * g->gw + x;
				hi = ar[i] > ag[i] ? (ar[i] > ab[i] ? ar[i] : ab[i]) : (ag[i] > ab[i] ? ag[i] : ab[i]);
				lo = ar[i] < ag[i] ? (ar[i] < ab[i] ? ar[i] : ab[i]) : (ag[i] < ab[i] ? ag[i] : ab[i]);
				/* as they are shown, which is how they were chosen */
				hi = powf(hi > 0.0f ? hi : 0.0f, 1.0f / 2.2f);
				lo = powf(lo > 0.0f ? lo : 0.0f, 1.0f / 2.2f);
				vivid[i] = (hi - lo) / (hi + VIVID_FLOOR);

				if (x >= g->ox && x < g->ox + width && y >= g->oy && y < g->oy + height)
				{
					const float counts = (1.0f - clamp01((body - HOLE_BELOW - clean[i]) / HOLE_RANGE)) * clamp01(hi / DARK_COUNTS);

					seen += counts;
					sum += counts * vivid[i];
					squares += counts * vivid[i] * vivid[i];
				}
			}
		}
		mean = seen > 1.0e-6 ? (float)(sum / seen) : 0.0f;
		spread = seen > 1.0e-6 ? (float)(squares / seen) - mean * mean : 0.0f;
		own = smoothstep(ONE_KIND_FROM, ONE_KIND_TO, sqrtf(spread > 0.0f ? spread : 0.0f));

		for (i = 0; i < g->count; i++)
		{
			const float hole = clamp01((body - HOLE_BELOW - clean[i]) / HOLE_RANGE);

			r->metal[i] = (mean + (vivid[i] - mean) * own < limit && hole < 0.5f && clean[i] > dark) ? 1.0f : 0.0f;
		}
		/* a texel alone among the other kind is noise: each goes with what lies about it */
		blur(r->metal, r->metal, tmp, g, METAL_PATCH);
		for (y = 0; y < height; y++)
			for (x = 0; x < width; x++)
				metal += r->metal[(size_t)(y + g->oy) * g->gw + x + g->ox] > 0.5f;
		r->metal_share = (float)metal / (float)((size_t)width * height);
	}
	else
		memset(r->metal, 0, g->count * sizeof(float));

	/* how far each texel's colour is from the colour around it: 1 is certainly paint */
	{
		float	*mr = lum, *mg = z;		/* lum is done with, z not yet begun */

		blur(cr, mr, tmp, g, WIDE);
		blur(cg, mg, tmp, g, WIDE);
		blur(known, wide, tmp, g, WIDE);
		for (i = 0; i < g->count; i++)
		{
			const float k = known[i] > 1.0e-4f ? 1.0f / known[i] : 0.0f;
			const float dr = cr[i] * k - mr[i] / (wide[i] + 1.0e-3f);
			const float dg = cg[i] * k - mg[i] / (wide[i] + 1.0e-3f);

			paint[i] = (1.0f - expf(-(dr * dr + dg * dg) / (2.0f * PAINT_COLOUR * PAINT_COLOUR))) * known[i];
		}
	}

	/*
	Two readings of the same detail, set side by side as the real and
	imaginary halves of one grid so that one transform does for both.
	*/
	for (i = 0; i < g->count; i++)
	{
		const float hole = clamp01((body - HOLE_BELOW - clean[i]) / HOLE_RANGE);
		float	shade, deep, lit;

		/* painted light: black says nothing of which way a surface faces, nor does paint */
		shade = band[i] * (1.0f - hole) * (1.0f - paint[i]);
		shade = shade > SHADE_NOISE ? shade - SHADE_NOISE : (shade < -SHADE_NOISE ? shade + SHADE_NOISE : 0.0f);
		shade = let_go(shade, SHADE_MOST);

		/*
		Dark is deep, though a painted stripe or letter is not. Rust and
		dirt are a change of colour too, but they gather in what is
		recessed, so the darker of them still counts in full.
		*/
		deep = band[i];
		if (deep > 0.0f)
			deep *= BRIGHT_RAISED * (1.0f - 0.7f * paint[i]);
		else
			deep *= 1.0f - 0.7f * paint[i] * (1.0f - warm[i]);
		deep = let_go(deep, DEEP_MOST) - HOLE_DEPTH * hole;

		z[2 * i] = from->painted_light ? shade : 0.0f;
		z[2 * i + 1] = deep;

		/* the same painted light, kept sharp, for taking out of the colours */
		lit = r->lit[i] * (1.0f - hole) * (1.0f - paint[i]);
		lit = lit > SHADE_NOISE ? lit - SHADE_NOISE : (lit < -SHADE_NOISE ? lit + SHADE_NOISE : 0.0f);
		lit = let_go(lit, LIT_MOST);
		r->lit[i] = lit < 0.0f ? lit * (1.0f - SHADOW_KEPT) : lit;
	}
	transform_grid(z, g->gw, g->gh, table, table_n, 0, line);

	/* from the two readings to the height, and from the height to its slopes, wave by wave */
	{
		const float shift = 0.5f / (float)g->scale - 0.5f;		/* where the finer map's first pixel lies */
		const float dark_deep = from->painted_light ? DARK_DEEP : DARK_DEEP_ALONE;
		int		jx, jy;

		for (jy = 0; jy < g->gh; jy++)
		{
			for (jx = 0; jx < g->gw; jx++)
			{
				float	kx, ky, er, ei, cre, cim, hr, hi, dr, di, soften, turn, tc, ts;
				const float *f, *b;
				float	*out;

				if (!frequency(jx, g->gw, &kx) || !frequency(jy, g->gh, &ky))
					continue;
				f = z + 2 * ((size_t)jy * g->gw + jx);
				b = z + 2 * ((size_t)((g->gh - jy) & (g->gh - 1)) * g->gw + ((g->gw - jx) & (g->gw - 1)));

				/* apart again: the transform of the real half and of the imaginary half */
				er = (f[0] + b[0]) * 0.5f;
				ei = (f[1] - b[1]) * 0.5f;
				cre = (f[1] + b[1]) * 0.5f;
				cim = -(f[0] - b[0]) * 0.5f;

				hr = dark_deep * cre;
				hi = dark_deep * cim;
				if (from->painted_light)
				{
					/*
					Brightness is the height's slope away from the light, so
					the height is brightness added up along the light's way:
					a highlight is the near edge of something raised, a
					shadow its far edge.
					*/
					const float along = LIGHT_X * kx + LIGHT_Y * ky, cross = -LIGHT_Y * kx + LIGHT_X * ky;
					const float gain = PAINTED * along / (along * along + CARRY * CARRY + ACROSS * cross * cross);

					hr += -ei * gain;
					hi += er * gain;
				}
				soften = expf(-0.5f * SOFTEN * SOFTEN * (kx * kx + ky * ky));
				hr *= soften;
				hi *= soften;

				if (want_height)
				{
					dr = hr;
					di = hi;
				}
				else
				{
					/* slope to the right in the real half, slope down the picture in the imaginary */
					dr = -ky * hr - kx * hi;
					di = -ky * hi + kx * hr;
				}
				turn = (kx + ky) * shift;
				tc = cosf(turn);
				ts = sinf(turn);
				out = r->slopes + 2 * ((size_t)(jy < g->gh / 2 || g->gh == 1 ? jy : jy + bh - g->gh) * bw
					+ (jx < g->gw / 2 || g->gw == 1 ? jx : jx + bw - g->gw));
				out[0] = dr * tc - di * ts;
				out[1] = dr * ts + di * tc;
			}
		}
	}
	transform_grid(r->slopes, bw, bh, table, table_n, 1, line);
	{
		const float scale = 1.0f / (float)g->count;

		for (i = 0; i < big_count * 2; i++)
			r->slopes[i] *= scale;
	}

	free(block);
	return 1;
}

static uint32_t to_byte(float v)
{
	return (uint32_t)(clamp01(v) * 255.0f + 0.5f);
}

int pt_material_read(const uint32_t *pixels, int width, int height, const pt_material_from_t *from, pt_material_maps_t *maps)
{
	reading_t	r;
	uint32_t	*map;
	const grid_t *g = &r.g;
	float		amount, spread, cover, edge;
	int			x, y, bw, mw, mh;

	memset(maps, 0, sizeof(*maps));
	if (!read_picture(&r, pixels, width, height, from, 0))
		return 0;
	/* beside metal, what is not metal lies over it: rust, paint, dirt. In a picture with no metal it is the thing itself */
	cover = from->metallic <= 0.0f ? 0.0f : COVER_ROUGHER * (from->metal_known ? 1.0f : clamp01(r.metal_share / METAL_SOME));
	/* across the straight edge of a patch r.metal rises by 1 / (sqrt(2 pi) METAL_PATCH) a texel of the picture */
	edge = 2.5066283f * METAL_PATCH * (float)g->scale / METAL_EDGE;
	mw = width * g->scale;
	mh = height * g->scale;
	map = (uint32_t *)malloc((size_t)mw * mh * sizeof(uint32_t));
	if (!map)
	{
		release(&r);
		return 0;
	}

	bw = g->gw * g->scale;
	amount = from->bump * RELIEF;
	/* a mirror or a sheet of glass stays one: the picture varies what is already rough */
	spread = from->roughness * 2.5f;
	spread = spread < 0.2f ? 0.2f : (spread > 1.0f ? 1.0f : spread);

	for (y = 0; y < mh; y++)
	{
		const float fy = ((float)y + 0.5f) / (float)g->scale - 0.5f + (float)g->oy;
		const int	y0 = (int)floorf(fy);
		const float ay = fy - (float)y0;
		const size_t row0 = (size_t)(y0 & (g->gh - 1)) * g->gw, row1 = (size_t)((y0 + 1) & (g->gh - 1)) * g->gw;
		const float *slope = r.slopes + 2 * ((size_t)(y + g->oy * g->scale) * bw + (size_t)g->ox * g->scale);

		for (x = 0; x < mw; x++, slope += 2)
		{
			const float fx = ((float)x + 0.5f) / (float)g->scale - 0.5f + (float)g->ox;
			const int	x0 = (int)floorf(fx), xa = x0 & (g->gw - 1), xb = (x0 + 1) & (g->gw - 1);
			const float ax = fx - (float)x0;
			const float w00 = (1.0f - ax) * (1.0f - ay), w01 = ax * (1.0f - ay), w10 = (1.0f - ax) * ay, w11 = ax * ay;
			float		sx = slope[0] * amount, sy = slope[1] * amount, level, len, rough, metal;

			level = 1.0f / sqrtf(1.0f + (sx * sx + sy * sy) / (STEEPEST * STEEPEST));
			sx *= level;
			sy *= level;
			len = 1.0f / sqrtf(sx * sx + sy * sy + 1.0f);

			/* all or nothing, but for the edge of a patch */
			metal = r.metal[row0 + xa] * w00 + r.metal[row0 + xb] * w01
				+ r.metal[row1 + xa] * w10 + r.metal[row1 + xb] * w11;
			metal = clamp01(0.5f + (metal - 0.5f) * edge);

			rough = spread * (r.rough[row0 + xa] * w00 + r.rough[row0 + xb] * w01
				+ r.rough[row1 + xa] * w10 + r.rough[row1 + xb] * w11);
			rough = from->roughness + (rough < -ROUGH_LESS ? -ROUGH_LESS : (rough > ROUGH_MORE ? ROUGH_MORE : rough))
				+ cover * (1.0f - metal);
			if (rough < 0.04f)
				rough = 0.04f;

			map[(size_t)y * mw + x] = to_byte(-sx * len * 0.5f + 0.5f) | (to_byte(-sy * len * 0.5f + 0.5f) << 8)
				| (to_byte(metal * from->metallic) << 16) | (to_byte(rough) << 24);
		}
	}
	maps->detail = map;
	maps->detail_width = mw;
	maps->detail_height = mh;

	if (from->painted_light && from->delight > 0.0f)
	{
		uint32_t	*colour = (uint32_t *)malloc((size_t)width * height * sizeof(uint32_t));
		int			changed = 0, k;

		/* without the memory for it the picture stays as it is */
		for (y = 0; colour && y < height; y++)
		{
			const float *lit = r.lit + (size_t)(y + g->oy) * g->gw + g->ox;

			for (x = 0; x < width; x++)
			{
				/* the picture's numbers are not linear light: 2.2 of theirs to one of light's */
				const float gain = expf(-from->delight * lit[x] * (1.0f / 2.2f));
				const uint32_t c = pixels[(size_t)y * width + x];
				uint32_t	out = c & 0xff000000u;

				for (k = 0; k < 24; k += 8)
				{
					const uint32_t v = (uint32_t)((float)((c >> k) & 0xff) * gain + 0.5f);

					out |= (v > 255 ? 255 : v) << k;
				}
				colour[(size_t)y * width + x] = out;
				changed |= out != c;
			}
		}
		if (colour && !changed)
		{
			free(colour);
			colour = NULL;
		}
		if (colour)
		{
			maps->colour = colour;
			maps->colour_width = width;
			maps->colour_height = height;
		}
	}

	release(&r);
	return 1;
}

unsigned char *pt_material_height(const uint32_t *pixels, int width, int height, const pt_material_from_t *from)
{
	reading_t		r;
	unsigned char	*map;
	const grid_t	*g = &r.g;
	int				x, y, bw, mw, mh;

	if (!read_picture(&r, pixels, width, height, from, 1))
		return NULL;
	mw = width * g->scale;
	mh = height * g->scale;
	map = (unsigned char *)malloc((size_t)mw * mh);
	if (!map)
	{
		release(&r);
		return NULL;
	}
	bw = g->gw * g->scale;
	for (y = 0; y < mh; y++)
		for (x = 0; x < mw; x++)
			map[(size_t)y * mw + x] = (unsigned char)to_byte(0.5f + 0.25f * from->bump
				* r.slopes[2 * ((size_t)(y + g->oy * g->scale) * bw + (size_t)(x + g->ox * g->scale))]);
	release(&r);
	return map;
}
