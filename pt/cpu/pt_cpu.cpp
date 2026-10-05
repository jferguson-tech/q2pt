// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
//
// CPU backend: a unidirectional path tracer with next event estimation,
// temporal accumulation and an edge aware filter, presented through GDI.

#include "../include/pt.h"
#include "pt_bvh.h"
#include "pt_pool.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace {

using namespace pt;

const float kRayOffset = 0.03f;			// keeps bounce rays off the surface they leave
const int kLightCandidates = 8;			// per shading point
const float kMaxSample = 40.0f;			// luminance clamp per path, tames fireflies
const float kGlobalLightChance = 0.2f;	// how often a light is picked map wide instead of nearby
const uint32_t kDynamic = 0x80000000u;	// triangle index bit: belongs to the frame, not the world
const float kMovingHistory = 32.0f;		// frames of history kept while anything changes
const int kFilterPasses = 4;

float g_to_linear[256];
uint8_t g_to_display[4097];

void InitTables()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	for (int i = 0; i < 256; i++)
		g_to_linear[i] = std::pow(i / 255.0f, 2.2f);
	for (int i = 0; i <= 4096; i++)
		g_to_display[i] = (uint8_t)(std::pow(i / 4096.0f, 1.0f / 2.2f) * 255.0f + 0.5f);
}

Vec3 Decode(uint32_t rgba)
{
	return Vec3(g_to_linear[rgba & 0xff], g_to_linear[(rgba >> 8) & 0xff], g_to_linear[(rgba >> 16) & 0xff]);
}

bool Finite(float f)
{
	uint32_t bits;
	memcpy(&bits, &f, sizeof(bits));
	return (bits & 0x7f800000u) != 0x7f800000u;
}

// ----------------------------------------------------------------- scene

struct Texture
{
	int						width = 0, height = 0;
	std::vector<uint32_t>	pixels;
	Vec3					average{1, 1, 1};

	void Set(const pt_texture_t &src)
	{
		width = src.width;
		height = src.height;
		const size_t count = (size_t)width * height;
		pixels.assign(src.pixels, src.pixels + count);

		Vec3 sum;
		for (size_t p = 0; p < count; p++)
			sum += Decode(pixels[p]);
		if (count)
			average = Max(sum / (float)count, Vec3(1e-4f));
	}

	uint32_t Texel(float u, float v) const
	{
		int x = (int)std::floor(u * width) % width;
		int y = (int)std::floor(v * height) % height;
		if (x < 0) x += width;
		if (y < 0) y += height;
		return pixels[(size_t)y * width + x];
	}

	// clamped, for the sky faces
	Vec3 SampleClamped(float u, float v) const
	{
		int x = (int)(u * width);
		int y = (int)(v * height);
		x = x < 0 ? 0 : (x >= width ? width - 1 : x);
		y = y < 0 ? 0 : (y >= height ? height - 1 : y);
		return Decode(pixels[(size_t)y * width + x]);
	}
};

struct Material
{
	const Texture	*texture = nullptr;
	Vec3			emission;			// average
	Vec3			emission_per_texel;	// multiply by the texel to get emitted radiance
	float			alpha = 1.0f;
	float			emission_seen = 0.0f;
	uint32_t		flags = 0;
	bool			emissive = false;
	bool			sampled = false;	// reached through the light lists, so not counted when hit by chance

	void Set(const pt_material_t &src, const Texture *tex)
	{
		texture = (tex && tex->width > 0 && tex->height > 0) ? tex : nullptr;
		emission = Vec3(src.emission);
		alpha = src.alpha;
		emission_seen = src.emission_seen;
		flags = src.flags;
		emissive = MaxComponent(emission) > 0.0f && !(flags & PT_MAT_SKY);
		emission_per_texel = emission;
		if (texture && !(flags & PT_MAT_EMIT_TEXTURE))
		{
			const Vec3 avg = texture->average;
			emission_per_texel = Vec3(emission.x / avg.x, emission.y / avg.y, emission.z / avg.z);
		}
	}
};

struct Tri
{
	Vec3			p0, e1, e2;
	Vec3			n;			// unit, towards the counter clockwise side
	float			area;
	float			uv[3][2];
	const Material	*mat;

	void Set(Vec3 a, Vec3 b, Vec3 c)
	{
		p0 = a;
		e1 = b - a;
		e2 = c - a;
		const Vec3 x = Cross(e1, e2);
		const float len = Length(x);
		area = len * 0.5f;
		n = len > 0.0f ? x / len : Vec3(0, 0, 1);
	}
};

struct Light
{
	uint32_t	tri;		// or ~0u for a point light
	Vec3		origin;		// point lights; centroid for triangles
	Vec3		emission;	// radiance for triangles, intensity for points
	float		pdf;		// chance of being picked map wide
};

// For each cell of a coarse grid, the lights that matter most there. Sampling
// from the cell a point is in finds nearby lights far more often than picking
// by power over the whole map.
struct LightGrid
{
	static const int kPerCell = 24;

	Vec3		origin;
	float		inv_cell = 0.0f;
	int			dims[3] = {0, 0, 0};
	std::vector<uint32_t>	light;	// kPerCell per cell
	std::vector<float>		pdf;	// chance within the cell
	std::vector<float>		cdf;
	std::vector<uint8_t>	count;

	size_t Cell(Vec3 p) const
	{
		size_t c[3];
		for (int a = 0; a < 3; a++)
		{
			int i = (int)((p[a] - origin[a]) * inv_cell);
			c[a] = (size_t)(i < 0 ? 0 : (i >= dims[a] ? dims[a] - 1 : i));
		}
		return (c[2] * dims[1] + c[1]) * dims[0] + c[0];
	}
};

struct World
{
	std::vector<Texture>	textures;
	std::vector<Material>	materials;
	std::vector<Tri>		tris;
	Bvh						bvh;
	std::vector<Light>		lights;
	std::vector<float>		light_cdf;
	LightGrid				grid;
	int						sky[6] = {-1, -1, -1, -1, -1, -1};
	float					sky_scale = 1.0f;

	Vec3 Sky(Vec3 d) const
	{
		const float ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
		int a = 0;
		if (ay > ax) a = 1;
		if (az > (a ? ay : ax)) a = 2;
		const int b = (a + 1) % 3, c = (a + 2) % 3;
		const float m = std::fabs(d[a]);
		const int face = a * 2 + (d[a] < 0.0f ? 1 : 0);
		if (sky[face] < 0 || m <= 0.0f)
			return Vec3(0, 0, 0);
		return textures[sky[face]].SampleClamped((d[b] / m + 1.0f) * 0.5f, (d[c] / m + 1.0f) * 0.5f) * sky_scale;
	}
};

// what the host hands over each frame
struct Frame
{
	std::vector<Material>	materials;
	std::vector<Tri>		tris;
	Bvh						bvh;
	std::vector<Light>		lights;		// point lights only
	uint32_t				hash = 0;	// changes when anything in it does
};

struct Scene
{
	const World	*world;
	const Frame	*frame;

	const Tri &TriAt(uint32_t index) const
	{
		return (index & kDynamic) ? frame->tris[index & ~kDynamic] : world->tris[index];
	}
};

void BuildLightGrid(World &w, const std::vector<float> &power)
{
	LightGrid &g = w.grid;
	if (w.lights.empty() || w.tris.empty())
		return;

	Vec3 lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (const Tri &t : w.tris)
	{
		const Vec3 p[3] = {t.p0, t.p0 + t.e1, t.p0 + t.e2};
		for (const Vec3 &v : p)
		{
			lo = Min(lo, v);
			hi = Max(hi, v);
		}
	}

	// about 40000 cells, but never finer than a doorway
	const Vec3 extent = hi - lo;
	float cell = std::cbrt(std::max(1.0f, extent.x * extent.y * extent.z) / 40000.0f);
	if (cell < 96.0f)
		cell = 96.0f;
	g.origin = lo;
	g.inv_cell = 1.0f / cell;
	for (int a = 0; a < 3; a++)
		g.dims[a] = std::max(1, (int)std::ceil(extent[a] * g.inv_cell));

	const size_t cells = (size_t)g.dims[0] * g.dims[1] * g.dims[2];
	const int k = LightGrid::kPerCell;
	g.light.assign(cells * k, 0);
	g.pdf.assign(cells * k, 0.0f);
	g.cdf.assign(cells * k, 1.0f);
	g.count.assign(cells, 0);

	const float min_dist2 = cell * cell * 0.75f;	// half the cell diagonal, squared
	std::vector<std::pair<float, uint32_t>> scored(w.lights.size());

	for (size_t ci = 0; ci < cells; ci++)
	{
		const size_t cx = ci % g.dims[0], cy = (ci / g.dims[0]) % g.dims[1], cz = ci / ((size_t)g.dims[0] * g.dims[1]);
		const Vec3 centre = lo + Vec3(cx + 0.5f, cy + 0.5f, cz + 0.5f) * cell;

		for (size_t li = 0; li < w.lights.size(); li++)
		{
			const Vec3 d = w.lights[li].origin - centre;
			scored[li] = {power[li] / std::max(Dot(d, d), min_dist2), (uint32_t)li};
		}
		const size_t keep = std::min((size_t)k, scored.size());
		std::partial_sort(scored.begin(), scored.begin() + keep, scored.end(),
			[](const std::pair<float, uint32_t> &a, const std::pair<float, uint32_t> &b) { return a.first > b.first; });

		float total = 0.0f;
		for (size_t i = 0; i < keep; i++)
			total += scored[i].first;
		if (total <= 0.0f)
			continue;

		float run = 0.0f;
		for (size_t i = 0; i < keep; i++)
		{
			g.light[ci * k + i] = scored[i].second;
			g.pdf[ci * k + i] = scored[i].first / total;
			run += scored[i].first;
			g.cdf[ci * k + i] = run / total;
		}
		g.cdf[ci * k + keep - 1] = 1.0f;
		g.count[ci] = (uint8_t)keep;
	}
}

std::unique_ptr<World> BuildWorld(const pt_world_t *in)
{
	std::unique_ptr<World> w(new World);

	w->textures.resize(in->num_textures);
	for (int i = 0; i < in->num_textures; i++)
		w->textures[i].Set(in->textures[i]);

	w->materials.resize(std::max(1, in->num_materials));
	for (int i = 0; i < in->num_materials; i++)
	{
		const int t = in->materials[i].texture;
		w->materials[i].Set(in->materials[i], (t >= 0 && t < in->num_textures) ? &w->textures[t] : nullptr);
		w->materials[i].sampled = w->materials[i].emissive;
	}

	std::vector<Vec3> soup((size_t)in->num_triangles * 3);
	w->tris.resize(in->num_triangles);
	for (int i = 0; i < in->num_triangles; i++)
	{
		Tri &t = w->tris[i];
		for (int k = 0; k < 3; k++)
		{
			const uint32_t vi = in->indices[i * 3 + k];
			soup[(size_t)i * 3 + k] = Vec3(&in->positions[vi * 3]);
			t.uv[k][0] = in->uvs[vi * 2];
			t.uv[k][1] = in->uvs[vi * 2 + 1];
		}
		t.Set(soup[(size_t)i * 3], soup[(size_t)i * 3 + 1], soup[(size_t)i * 3 + 2]);
		const uint32_t m = in->tri_materials[i];
		t.mat = &w->materials[m < (uint32_t)in->num_materials ? m : 0];
	}
	w->bvh.Build(soup.data(), (uint32_t)in->num_triangles);

	// everything that emits, with a chance of being picked proportional to its power
	std::vector<float> power;
	for (int i = 0; i < in->num_triangles; i++)
	{
		const Tri &t = w->tris[i];
		if (!t.mat->emissive || t.area <= 1e-6f)
			continue;
		Light l;
		l.tri = (uint32_t)i;
		l.origin = t.p0 + (t.e1 + t.e2) * (1.0f / 3.0f);
		l.emission = t.mat->emission;
		l.pdf = 0;
		w->lights.push_back(l);
		power.push_back(Luminance(t.mat->emission) * t.area * kPi);
	}
	for (int i = 0; i < in->num_lights; i++)
	{
		Light l;
		l.tri = ~0u;
		l.origin = Vec3(in->lights[i].origin);
		l.emission = Vec3(in->lights[i].intensity);
		l.pdf = 0;
		const float p = Luminance(l.emission) * 4.0f * kPi;
		if (p <= 0.0f)
			continue;
		w->lights.push_back(l);
		power.push_back(p);
	}

	double total = 0;
	for (float p : power)
		total += p;
	double run = 0;
	w->light_cdf.resize(power.size());
	for (size_t i = 0; i < power.size(); i++)
	{
		w->lights[i].pdf = (float)(power[i] / total);
		run += power[i];
		w->light_cdf[i] = (float)(run / total);
	}
	if (!w->light_cdf.empty())
		w->light_cdf.back() = 1.0f;
	BuildLightGrid(*w, power);

	for (int i = 0; i < 6; i++)
		w->sky[i] = (in->sky_textures[i] >= 0 && in->sky_textures[i] < in->num_textures) ? in->sky_textures[i] : -1;
	w->sky_scale = in->sky_scale;
	return w;
}

uint32_t HashBytes(const void *data, size_t bytes, uint32_t h)
{
	const uint8_t *p = (const uint8_t *)data;
	for (size_t i = 0; i < bytes; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

void BuildFrame(Frame &f, const pt_scene_t *in, const std::vector<std::unique_ptr<Texture>> &textures)
{
	f.materials.clear();
	f.tris.clear();
	f.lights.clear();
	f.hash = 2166136261u;

	std::vector<Vec3> soup;
	if (in)
	{
		f.materials.resize(std::max(1, in->num_materials));
		for (int i = 0; i < in->num_materials; i++)
		{
			const int t = in->materials[i].texture;
			f.materials[i].Set(in->materials[i],
				(t >= 0 && t < (int)textures.size() && textures[t]) ? textures[t].get() : nullptr);
		}

		f.tris.resize(in->num_triangles);
		soup.resize((size_t)in->num_triangles * 3);
		for (int i = 0; i < in->num_triangles; i++)
		{
			Tri &t = f.tris[i];
			for (int k = 0; k < 3; k++)
			{
				soup[(size_t)i * 3 + k] = Vec3(&in->positions[i * 9 + k * 3]);
				t.uv[k][0] = in->uvs[i * 6 + k * 2];
				t.uv[k][1] = in->uvs[i * 6 + k * 2 + 1];
			}
			t.Set(soup[(size_t)i * 3], soup[(size_t)i * 3 + 1], soup[(size_t)i * 3 + 2]);
			const uint32_t m = in->tri_materials[i];
			t.mat = &f.materials[m < (uint32_t)in->num_materials ? m : 0];
		}

		for (int i = 0; i < in->num_lights; i++)
		{
			Light l;
			l.tri = ~0u;
			l.origin = Vec3(in->lights[i].origin);
			l.emission = Vec3(in->lights[i].intensity);
			l.pdf = 0;
			if (Luminance(l.emission) > 0.0f)
				f.lights.push_back(l);
		}

		f.hash = HashBytes(in->positions, (size_t)in->num_triangles * 9 * sizeof(float), f.hash);
		f.hash = HashBytes(in->materials, (size_t)in->num_materials * sizeof(pt_material_t), f.hash);
		f.hash = HashBytes(in->lights, (size_t)in->num_lights * sizeof(pt_point_light_t), f.hash);
	}
	f.bvh.Build(soup.data(), (uint32_t)f.tris.size());
}

// ------------------------------------------------------------ integrator

void TexCoord(const Tri &t, float u, float v, float &s, float &tt)
{
	const float b0 = 1.0f - u - v;
	s = t.uv[0][0] * b0 + t.uv[1][0] * u + t.uv[2][0] * v;
	tt = t.uv[0][1] * b0 + t.uv[1][1] * u + t.uv[2][1] * v;
}

Vec3 Colour(const Tri &t, float u, float v)
{
	if (!t.mat->texture)
		return Vec3(1, 1, 1);
	float s, tt;
	TexCoord(t, u, v, s, tt);
	return Decode(t.mat->texture->Texel(s, tt));
}

Vec3 Albedo(const Tri &t, float u, float v)
{
	return (t.mat->flags & PT_MAT_BLACK) ? Vec3() : Colour(t, u, v);
}

// Radiance leaving an emitter. A lamp bright enough to light a room would show
// as a white blob, so a material can ask to be seen dimmer than it is.
Vec3 Emitted(const Tri &t, float u, float v, bool seen)
{
	const Material &m = *t.mat;
	const Vec3 c = Colour(t, u, v);
	return (seen && m.emission_seen > 0.0f) ? c * m.emission_seen : m.emission_per_texel * c;
}

bool IsHole(const Tri &t, float u, float v)
{
	if (!(t.mat->flags & PT_MAT_ALPHA_TEST) || !t.mat->texture)
		return false;
	float s, tt;
	TexCoord(t, u, v, s, tt);
	return (t.mat->texture->Texel(s, tt) >> 24) < 128;
}

// Nearest surface along the ray, in the world or the frame. Holes are
// stepped through, and so are surfaces the camera must not see when the ray
// comes from it. With cross set, surfaces that let light through are crossed
// at random in proportion to how much they pass.
bool Closest(const Scene &sc, Ray &ray, Rng &rng, bool camera, bool cross, Hit &hit, const Tri *&tri)
{
	for (int skips = 0; ; skips++)
	{
		bool found = sc.world->bvh.Intersect(ray, hit);
		Ray r = ray;
		if (found)
			r.tmax = hit.t;
		Hit h;
		if (sc.frame->bvh.Intersect(r, h))
		{
			hit = h;
			hit.tri |= kDynamic;
			found = true;
		}
		if (!found)
			return false;

		tri = &sc.TriAt(hit.tri);
		const Material &m = *tri->mat;
		const bool skip = skips < 32 && (
			(camera && (m.flags & PT_MAT_CAMERA_INVISIBLE)) ||
			IsHole(*tri, hit.u, hit.v) ||
			(cross && m.alpha < 1.0f && rng.Float() >= m.alpha));
		if (!skip)
			return true;
		ray.tmin = hit.t + 0.01f;
	}
}

// true if nothing stops light between p and target
bool Visible(const Scene &sc, Vec3 p, Vec3 n, Vec3 target, Rng &rng)
{
	Ray shadow;
	shadow.o = p + n * kRayOffset;
	shadow.d = target - shadow.o;
	shadow.tmin = 0.0f;
	shadow.tmax = 0.999f;

	const auto blocks = [&](const Tri &t, float u, float v)
	{
		if (IsHole(t, u, v))
			return false;
		return t.mat->alpha >= 1.0f || rng.Float() < t.mat->alpha;
	};
	if (sc.world->bvh.AnyHit(shadow, [&](uint32_t i, float u, float v) { return blocks(sc.world->tris[i], u, v); }))
		return false;
	return !sc.frame->bvh.AnyHit(shadow, [&](uint32_t i, float u, float v) { return blocks(sc.frame->tris[i], u, v); });
}

// irradiance * cos from a point light, ignoring occlusion
Vec3 PointLight(const Light &l, Vec3 p, Vec3 n)
{
	const Vec3 d = l.origin - p;
	const float dist2 = Dot(d, d);
	if (dist2 <= 1e-6f)
		return Vec3();
	const float cosx = Dot(n, d) / std::sqrt(dist2);
	if (cosx <= 0.0f)
		return Vec3();
	return l.emission * (cosx / dist2);
}

// Light arriving at p from the world's lights: one of them, picked by
// resampling a few candidates in proportion to what each would contribute.
Vec3 DirectWorld(const Scene &sc, Vec3 p, Vec3 n, Rng &rng)
{
	const World &w = *sc.world;
	if (w.lights.empty())
		return Vec3();

	float wsum = 0.0f;
	Vec3 chosen_f, chosen_y;
	float chosen_phat = 0.0f;

	const LightGrid &g = w.grid;
	const int k = LightGrid::kPerCell;
	const size_t cell = g.count.empty() ? 0 : g.Cell(p);
	const int in_cell = g.count.empty() ? 0 : g.count[cell];
	const float *cell_cdf = in_cell ? &g.cdf[cell * k] : nullptr;
	const float use_global = in_cell ? kGlobalLightChance : 1.0f;

	for (int i = 0; i < kLightCandidates; i++)
	{
		// mostly from this cell's list, sometimes from the whole map so nothing is missed
		size_t li;
		float cell_pdf = 0.0f;
		if (rng.Float() < use_global)
		{
			li = std::min((size_t)(std::upper_bound(w.light_cdf.begin(), w.light_cdf.end(), rng.Float())
				- w.light_cdf.begin()), w.lights.size() - 1);
			for (int j = 0; j < in_cell; j++)
				if (g.light[cell * k + j] == li)
					cell_pdf = g.pdf[cell * k + j];
		}
		else
		{
			const int j = std::min((int)(std::upper_bound(cell_cdf, cell_cdf + in_cell, rng.Float()) - cell_cdf), in_cell - 1);
			li = g.light[cell * k + j];
			cell_pdf = g.pdf[cell * k + j];
		}
		const Light &l = w.lights[li];
		const float pick = use_global * l.pdf + (1.0f - use_global) * cell_pdf;

		Vec3 y, f;
		float pdf;
		if (l.tri != ~0u)
		{
			const Tri &t = w.tris[l.tri];
			float a = rng.Float(), b = rng.Float();
			if (a + b > 1.0f) { a = 1.0f - a; b = 1.0f - b; }
			y = t.p0 + t.e1 * a + t.e2 * b;
			const Vec3 d = y - p;
			const float dist2 = Dot(d, d);
			if (dist2 <= 1e-6f)
				continue;
			const Vec3 wi = d / std::sqrt(dist2);
			const float cosx = Dot(n, wi), cosy = -Dot(t.n, wi);
			if (cosx <= 0.0f || cosy <= 0.0f)
				continue;
			f = l.emission * (cosx * cosy / dist2);
			pdf = pick / t.area;
		}
		else
		{
			y = l.origin;
			f = PointLight(l, p, n);
			pdf = pick;
		}

		const float phat = Luminance(f);
		if (phat <= 0.0f)
			continue;
		const float weight = phat / pdf;
		wsum += weight;
		if (rng.Float() * wsum < weight)
		{
			chosen_f = f;
			chosen_y = y;
			chosen_phat = phat;
		}
	}

	if (wsum <= 0.0f || !Visible(sc, p, n, chosen_y, rng))
		return Vec3();
	return chosen_f * (wsum / (kLightCandidates * chosen_phat));
}

// Light arriving at p from the frame's point lights, one shadow ray: the
// light is picked exactly in proportion to its unoccluded contribution.
Vec3 DirectFrameOne(const Scene &sc, Vec3 p, Vec3 n, Rng &rng)
{
	const std::vector<Light> &lights = sc.frame->lights;
	if (lights.empty())
		return Vec3();

	float total = 0.0f;
	for (const Light &l : lights)
		total += Luminance(PointLight(l, p, n));
	if (total <= 0.0f)
		return Vec3();

	float pick = rng.Float() * total;
	for (const Light &l : lights)
	{
		const Vec3 f = PointLight(l, p, n);
		const float lum = Luminance(f);
		pick -= lum;
		if (pick <= 0.0f && lum > 0.0f)
			return Visible(sc, p, n, l.origin, rng) ? f * (total / lum) : Vec3();
	}
	return Vec3();
}

// the same, every light with its own shadow ray
Vec3 DirectFrameAll(const Scene &sc, Vec3 p, Vec3 n, Rng &rng)
{
	Vec3 sum;
	for (const Light &l : sc.frame->lights)
	{
		const Vec3 f = PointLight(l, p, n);
		if (Luminance(f) > 0.0f && Visible(sc, p, n, l.origin, rng))
			sum += f;
	}
	return sum;
}

Vec3 CosineDirection(Vec3 n, Rng &rng)
{
	const float r1 = rng.Float(), r2 = rng.Float();
	const float r = std::sqrt(r1), phi = 2.0f * kPi * r2;
	Vec3 t, b;
	Basis(n, t, b);
	return t * (r * std::cos(phi)) + b * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1.0f - r1));
}

// Radiance arriving back along the ray. camera says the ray left the eye;
// depth counts the bounces already taken.
Vec3 Radiance(const Scene &sc, Ray ray, Rng &rng, bool camera, int depth, int max_bounces)
{
	Vec3 radiance, throughput(1, 1, 1);

	for (;; depth++, camera = false)
	{
		Hit hit;
		const Tri *tri;
		if (!Closest(sc, ray, rng, camera, true, hit, tri))
			return radiance;
		const Material &mat = *tri->mat;

		if (mat.flags & PT_MAT_SKY)
			return radiance + throughput * sc.world->Sky(ray.d);

		const Vec3 p = ray.o + ray.d * hit.t;
		const bool front = Dot(tri->n, ray.d) < 0.0f;
		const Vec3 n = front ? tri->n : -tri->n;
		const Vec3 albedo = Albedo(*tri, hit.u, hit.v);

		// emitters in the light lists reach later bounces through DirectWorld
		if (mat.emissive && front && (camera || !mat.sampled))
			radiance += throughput * Emitted(*tri, hit.u, hit.v, camera);

		if (MaxComponent(albedo) <= 0.0f)
			return radiance;		// reflects nothing

		radiance += throughput * albedo * (DirectWorld(sc, p, n, rng) + DirectFrameOne(sc, p, n, rng)) * kInvPi;

		if (depth >= max_bounces)
			return radiance;

		// cosine weighted bounce: the surface colour is all that is left of the estimator
		throughput *= albedo;
		if (depth >= 2)
		{
			const float survive = std::min(1.0f, std::max(0.05f, MaxComponent(throughput)));
			if (rng.Float() >= survive)
				return radiance;
			throughput *= 1.0f / survive;
		}

		ray.o = p + n * kRayOffset;
		ray.d = CosineDirection(n, rng);
		ray.tmin = 0.0f;
		ray.tmax = FLT_MAX;
	}
}

Vec3 ClampSample(Vec3 c)
{
	const float lum = Luminance(c);
	if (!Finite(lum))
		return Vec3();
	return lum > kMaxSample ? c * (kMaxSample / lum) : c;
}

// --------------------------------------------------------------- backend

struct Camera
{
	Vec3	origin, forward, right, up;
	float	tx = 0, ty = 0;		// tangents of the half angles

	bool operator==(const Camera &o) const { return !memcmp(this, &o, sizeof(*this)); }
};

/*
Per pixel state at render resolution.

Lighting is kept apart from the colour of the surface it falls on: `light`
is what arrives, to be multiplied by `albedo`, with `add` on top for what
needs no filtering (what the surface emits, the frame's point lights). That
lets the noisy part be averaged over time and space without smearing
textures.
*/
struct Pixels
{
	std::vector<Vec3>	pos, normal;
	std::vector<float>	depth;		// along the ray; negative where there is no surface
	std::vector<Vec3>	albedo, add;
	std::vector<Vec3>	light;		// this frame's samples, then accumulated
	std::vector<float>	m1, m2;		// luminance moments of light
	std::vector<float>	length;		// frames accumulated
	std::vector<float>	variance;

	void Resize(size_t n)
	{
		pos.assign(n, Vec3());
		normal.assign(n, Vec3());
		depth.assign(n, -1.0f);
		albedo.assign(n, Vec3());
		add.assign(n, Vec3());
		light.assign(n, Vec3());
		m1.assign(n, 0.0f);
		m2.assign(n, 0.0f);
		length.assign(n, 0.0f);
		variance.assign(n, 0.0f);
	}
};

struct CpuBackend
{
	pt_backend_t	base{};
	pt_log_fn		log = nullptr;
	HWND			hwnd = nullptr;
	HDC				memdc = nullptr;
	HBITMAP			dib = nullptr;
	HGDIOBJ			olddib = nullptr;
	uint32_t		*dibbits = nullptr;		// 0x00RRGGBB, top row first
	int				width = 0, height = 0;
	std::vector<uint32_t> scene;			// 0x00RRGGBB, window sized
	bool			has_view = false;
	pt_view_t		view{};

	Pool					pool;
	std::unique_ptr<World>	world;
	std::vector<std::unique_ptr<Texture>> textures;	// by handle
	Frame					frame;

	Pixels					cur, prev;
	std::vector<Vec3>		filter_a, filter_b;
	std::vector<float>		var_a, var_b;
	std::vector<uint32_t>	ldr;			// tone mapped, render sized
	int						rw = 0, rh = 0;
	bool					have_history = false;
	Camera					prev_camera;
	uint32_t				prev_hash = 0;
	uint32_t				frame_index = 0;
	char					stats[160] = "";
};

CpuBackend *Self(pt_backend_t *b) { return reinterpret_cast<CpuBackend *>(b); }

void Destroy(pt_backend_t *b)
{
	CpuBackend *s = Self(b);
	if (s->memdc)
	{
		if (s->olddib)
			SelectObject(s->memdc, s->olddib);
		DeleteDC(s->memdc);
	}
	if (s->dib)
		DeleteObject(s->dib);
	delete s;
}

void LoadWorld(pt_backend_t *b, const pt_world_t *world)
{
	CpuBackend *s = Self(b);
	s->world.reset();
	s->have_history = false;
	if (!world)
		return;
	s->world = BuildWorld(world);

	if (s->log)
	{
		size_t emitters = 0;
		float tripower = 0, pointpower = 0;
		for (const Light &l : s->world->lights)
		{
			if (l.tri != ~0u) { emitters++; tripower += l.pdf; }
			else pointpower += l.pdf;
		}
		char msg[160];
		snprintf(msg, sizeof(msg), "CPU path tracer: %zu emitting triangles (%.0f%% of power), %zu point lights (%.0f%%)\n",
			emitters, tripower * 100.0f, s->world->lights.size() - emitters, pointpower * 100.0f);
		s->log(msg);
	}
}

int TextureCreate(pt_backend_t *b, const pt_texture_t *texture)
{
	CpuBackend *s = Self(b);
	if (!texture || texture->width <= 0 || texture->height <= 0 || !texture->pixels)
		return -1;

	size_t slot = 0;
	while (slot < s->textures.size() && s->textures[slot])
		slot++;
	if (slot == s->textures.size())
		s->textures.emplace_back();
	s->textures[slot].reset(new Texture);
	s->textures[slot]->Set(*texture);
	return (int)slot;
}

void TextureDestroy(pt_backend_t *b, int handle)
{
	CpuBackend *s = Self(b);
	if (handle >= 0 && handle < (int)s->textures.size())
		s->textures[handle].reset();
}

void ClipView(const CpuBackend *s, int &x0, int &y0, int &x1, int &y1)
{
	x0 = s->view.x < 0 ? 0 : s->view.x;
	y0 = s->view.y < 0 ? 0 : s->view.y;
	x1 = s->view.x + s->view.width;
	y1 = s->view.y + s->view.height;
	if (x1 > s->width) x1 = s->width;
	if (y1 > s->height) y1 = s->height;
}

// filmic curve (Narkowicz's ACES fit), then display gamma
uint32_t ToneMap(Vec3 c)
{
	uint32_t out = 0;
	for (int i = 0; i < 3; i++)
	{
		const float x = c[i] > 0.0f ? c[i] : 0.0f;
		float y = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
		y = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
		out = (out << 8) | g_to_display[(int)(y * 4096.0f)];
	}
	return out;
}

// Traces one pixel: what the eye sees there, and samples of the light on it
void TracePixel(CpuBackend *s, const Scene &sc, const Camera &cam, int x, int y, int samples, int bounces)
{
	const size_t i = (size_t)y * s->rw + x;
	Pixels &px = s->cur;
	Rng rng(Hash((uint32_t)i, s->frame_index));

	Ray ray;
	ray.o = cam.origin;
	ray.d = Normalize(cam.forward
		+ cam.right * ((2.0f * (x + 0.5f) / s->rw - 1.0f) * cam.tx)
		+ cam.up * ((1.0f - 2.0f * (y + 0.5f) / s->rh) * cam.ty));
	ray.tmin = 0.0f;
	ray.tmax = FLT_MAX;

	px.depth[i] = -1.0f;
	px.albedo[i] = Vec3();
	px.add[i] = Vec3();
	px.light[i] = Vec3();
	px.m1[i] = px.m2[i] = 0.0f;

	// Walk through whatever is see-through to the first solid surface. The
	// solid surface is what the filters work on; the layers in front dim it
	// and add their own light on top.
	float through = 1.0f;		// how much of what is behind still shows
	Vec3 front_add;				// from the layers: what they emit
	Vec3 front_light;			// and what they reflect, which is noisy
	Hit hit;
	const Tri *tri;
	for (int layer = 0; ; layer++)
	{
		if (!Closest(sc, ray, rng, true, false, hit, tri))
		{
			px.add[i] = front_add + front_light;
			return;
		}
		const Material &mat = *tri->mat;
		if (mat.flags & PT_MAT_SKY)
		{
			px.add[i] = front_add + front_light + sc.world->Sky(ray.d) * through;
			return;
		}
		if (mat.alpha >= 1.0f || layer >= 8)
			break;

		const bool front = Dot(tri->n, ray.d) < 0.0f;
		const Vec3 albedo = Albedo(*tri, hit.u, hit.v);
		const float share = through * mat.alpha;
		if (mat.emissive && front)
			front_add += Emitted(*tri, hit.u, hit.v, true) * share;
		if (MaxComponent(albedo) > 0.0f)
		{
			const Vec3 p = ray.o + ray.d * hit.t;
			const Vec3 n = front ? tri->n : -tri->n;
			front_light += albedo * (DirectWorld(sc, p, n, rng) + DirectFrameOne(sc, p, n, rng)) * (kInvPi * share);
		}
		through *= 1.0f - mat.alpha;
		ray.tmin = hit.t + 0.01f;
	}

	const Material &mat = *tri->mat;
	const Vec3 p = ray.o + ray.d * hit.t;
	const bool front = Dot(tri->n, ray.d) < 0.0f;
	const Vec3 n = front ? tri->n : -tri->n;
	const Vec3 albedo = Albedo(*tri, hit.u, hit.v);
	const Vec3 shown = albedo * through;

	px.pos[i] = p;
	px.normal[i] = n;
	px.depth[i] = hit.t;
	px.albedo[i] = shown;
	px.add[i] = front_add + shown * DirectFrameAll(sc, p, n, rng) * kInvPi;
	if (mat.emissive && front)
		px.add[i] += Emitted(*tri, hit.u, hit.v, true) * through;

	// the layers' reflected light is filtered along with the surface's own,
	// so express it as light on that surface
	const Vec3 extra = ClampSample(Vec3(
		front_light.x / std::max(shown.x, 0.02f),
		front_light.y / std::max(shown.y, 0.02f),
		front_light.z / std::max(shown.z, 0.02f)));

	Vec3 sum;
	float m1 = 0.0f, m2 = 0.0f;
	if (MaxComponent(albedo) > 0.0f)
	{
		for (int k = 0; k < samples; k++)
		{
			Vec3 c = DirectWorld(sc, p, n, rng) * kInvPi;
			if (bounces > 0)
			{
				Ray bounce;
				bounce.o = p + n * kRayOffset;
				bounce.d = CosineDirection(n, rng);
				bounce.tmin = 0.0f;
				bounce.tmax = FLT_MAX;
				c += Radiance(sc, bounce, rng, false, 1, bounces);
			}
			c = ClampSample(c) + extra;
			const float lum = Luminance(c);
			sum += c;
			m1 += lum;
			m2 += lum * lum;
		}
	}

	const float inv = 1.0f / samples;
	px.light[i] = sum * inv;
	px.m1[i] = m1 * inv;
	px.m2[i] = m2 * inv;
}

// Blends this frame's samples into what earlier frames saw of the same surface
void Accumulate(CpuBackend *s, const Camera &prev_cam, float max_history, int y)
{
	const int rw = s->rw, rh = s->rh;
	Pixels &cur = s->cur;
	const Pixels &prev = s->prev;

	for (int x = 0; x < rw; x++)
	{
		const size_t i = (size_t)y * rw + x;
		cur.length[i] = 0.0f;
		cur.variance[i] = 0.0f;
		if (cur.depth[i] < 0.0f)
			continue;

		Vec3 hist;
		float hm1 = 0.0f, hm2 = 0.0f, hlen = 0.0f, wsum = 0.0f;

		if (s->have_history)
		{
			// where was this point on screen last frame?
			const Vec3 v = cur.pos[i] - prev_cam.origin;
			const float z = Dot(v, prev_cam.forward);
			if (z > 0.01f)
			{
				const float fx = (Dot(v, prev_cam.right) / (z * prev_cam.tx) * 0.5f + 0.5f) * rw - 0.5f;
				const float fy = (0.5f - Dot(v, prev_cam.up) / (z * prev_cam.ty) * 0.5f) * rh - 0.5f;
				const int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
				const float ax = fx - ix, ay = fy - iy;
				const float limit = 1.0f + cur.depth[i] * 0.01f;

				for (int t = 0; t < 4; t++)
				{
					const int qx = ix + (t & 1), qy = iy + (t >> 1);
					if (qx < 0 || qy < 0 || qx >= rw || qy >= rh)
						continue;
					const size_t q = (size_t)qy * rw + qx;
					if (prev.depth[q] < 0.0f || prev.length[q] <= 0.0f)
						continue;
					// same surface: on the same plane, facing the same way
					if (std::fabs(Dot(cur.normal[i], prev.pos[q] - cur.pos[i])) > limit ||
						Dot(cur.normal[i], prev.normal[q]) < 0.9f)
						continue;
					const float w = ((t & 1) ? ax : 1.0f - ax) * ((t >> 1) ? ay : 1.0f - ay);
					if (w <= 0.0f)
						continue;
					hist += prev.light[q] * w;
					hm1 += prev.m1[q] * w;
					hm2 += prev.m2[q] * w;
					hlen += prev.length[q] * w;
					wsum += w;
				}
			}
		}

		float len = 1.0f;
		if (wsum > 0.01f)
		{
			const float inv = 1.0f / wsum;
			hist *= inv;
			hm1 *= inv;
			hm2 *= inv;
			len = std::min(hlen * inv + 1.0f, max_history);
			const float a = 1.0f / len;
			cur.light[i] = hist + (cur.light[i] - hist) * a;
			cur.m1[i] = hm1 + (cur.m1[i] - hm1) * a;
			cur.m2[i] = hm2 + (cur.m2[i] - hm2) * a;
		}
		cur.length[i] = len;

		// how unsure the average still is; with little history, assume very
		float var = std::max(0.0f, cur.m2[i] - cur.m1[i] * cur.m1[i]) / len;
		if (len < 4.0f)
			var = std::max(var, cur.m1[i] * cur.m1[i] * 0.25f + 0.01f);
		cur.variance[i] = var;
	}
}

// One pass of an a-trous wavelet filter. Neighbours count for less the more
// they differ in plane, facing or brightness, and brightness matters less
// where the estimate is still noisy.
void FilterRow(const CpuBackend *s, const std::vector<Vec3> &in, const std::vector<float> &var_in,
	std::vector<Vec3> &out, std::vector<float> &var_out, int step, int y)
{
	static const float kernel[5] = {1.0f / 16, 1.0f / 4, 3.0f / 8, 1.0f / 4, 1.0f / 16};
	const int rw = s->rw, rh = s->rh;
	const Pixels &g = s->cur;

	for (int x = 0; x < rw; x++)
	{
		const size_t i = (size_t)y * rw + x;
		if (g.depth[i] < 0.0f)
		{
			out[i] = in[i];
			var_out[i] = var_in[i];
			continue;
		}

		const Vec3 n = g.normal[i], p = g.pos[i];
		const float lum = Luminance(in[i]);
		const float inv_plane = 1.0f / (1.0f + g.depth[i] * 0.004f);
		const float inv_lum = 1.0f / (4.0f * std::sqrt(var_in[i]) + 1e-3f);

		Vec3 sum = in[i] * (kernel[2] * kernel[2]);
		float wsum = kernel[2] * kernel[2];
		float vsum = var_in[i] * wsum * wsum;

		for (int dy = -2; dy <= 2; dy++)
		{
			const int qy = y + dy * step;
			if (qy < 0 || qy >= rh)
				continue;
			for (int dx = -2; dx <= 2; dx++)
			{
				const int qx = x + dx * step;
				if (qx < 0 || qx >= rw || (!dx && !dy))
					continue;
				const size_t q = (size_t)qy * rw + qx;
				if (g.depth[q] < 0.0f)
					continue;

				float wn = Dot(n, g.normal[q]);
				if (wn <= 0.0f)
					continue;
				wn *= wn; wn *= wn; wn *= wn; wn *= wn; wn *= wn;	// ^32
				const float wz = std::fabs(Dot(n, g.pos[q] - p)) * inv_plane;
				const float wl = std::fabs(Luminance(in[q]) - lum) * inv_lum;
				const float w = kernel[dx + 2] * kernel[dy + 2] * wn * std::exp(-wz - wl);

				sum += in[q] * w;
				vsum += var_in[q] * w * w;
				wsum += w;
			}
		}

		out[i] = sum / wsum;
		var_out[i] = vsum / (wsum * wsum);
	}
}

void RenderView(pt_backend_t *b, const pt_view_t *view)
{
	CpuBackend *s = Self(b);
	s->view = *view;
	s->has_view = true;

	int x0, y0, x1, y1;
	ClipView(s, x0, y0, x1, y1);
	if (x1 <= x0 || y1 <= y0)
		return;

	if (!s->world)
	{
		for (int y = y0; y < y1; y++)
			std::fill(s->scene.begin() + (size_t)y * s->width + x0, s->scene.begin() + (size_t)y * s->width + x1, 0u);
		s->stats[0] = 0;
		return;
	}

	const auto start = std::chrono::steady_clock::now();

	const float scale = view->scale < 0.05f ? 0.05f : (view->scale > 1.0f ? 1.0f : view->scale);
	const int rw = std::max(1, (int)(view->width * scale + 0.5f));
	const int rh = std::max(1, (int)(view->height * scale + 0.5f));
	const int samples = std::max(1, view->samples);
	const int bounces = std::max(0, view->bounces);
	const size_t count = (size_t)rw * rh;

	if (rw != s->rw || rh != s->rh)
	{
		s->rw = rw;
		s->rh = rh;
		s->cur.Resize(count);
		s->prev.Resize(count);
		s->filter_a.assign(count, Vec3());
		s->filter_b.assign(count, Vec3());
		s->var_a.assign(count, 0.0f);
		s->var_b.assign(count, 0.0f);
		s->ldr.assign(count, 0);
		s->have_history = false;
	}

	BuildFrame(s->frame, view->scene, s->textures);
	const auto built = std::chrono::steady_clock::now();

	Camera cam;
	cam.origin = Vec3(view->origin);
	cam.forward = Vec3(view->forward);
	cam.right = Vec3(view->right);
	cam.up = Vec3(view->up);
	cam.tx = std::tan(view->fov_x * kPi / 360.0f);
	cam.ty = std::tan(view->fov_y * kPi / 360.0f);

	Scene sc;
	sc.world = s->world.get();
	sc.frame = &s->frame;
	s->frame_index++;

	s->pool.Run(rh, [&](int y)
	{
		for (int x = 0; x < rw; x++)
			TracePixel(s, sc, cam, x, y, samples, bounces);
	});
	const auto traced = std::chrono::steady_clock::now();

	// with nothing changing the average may run forever and converge
	const bool still = s->have_history && cam == s->prev_camera && s->frame.hash == s->prev_hash;
	const float max_history = still ? 65536.0f : kMovingHistory;
	const Camera prev_cam = s->prev_camera;
	s->pool.Run(rh, [&](int y) { Accumulate(s, prev_cam, max_history, y); });

	const std::vector<Vec3> *in = &s->cur.light;
	const std::vector<float> *var_in = &s->cur.variance;
	for (int pass = 0; pass < kFilterPasses; pass++)
	{
		std::vector<Vec3> &out = (pass & 1) ? s->filter_b : s->filter_a;
		std::vector<float> &var_out = (pass & 1) ? s->var_b : s->var_a;
		s->pool.Run(rh, [&](int y) { FilterRow(s, *in, *var_in, out, var_out, 1 << pass, y); });
		in = &out;
		var_in = &var_out;
	}

	const std::vector<Vec3> &light = *in;
	const float exposure = view->exposure;
	s->pool.Run(rh, [&](int y)
	{
		for (int x = 0; x < rw; x++)
		{
			const size_t i = (size_t)y * rw + x;
			s->ldr[i] = ToneMap((s->cur.albedo[i] * light[i] + s->cur.add[i]) * exposure);
		}
	});

	// stretch to the view with bilinear filtering
	const int vw = view->width, vh = view->height;
	s->pool.Run(y1 - y0, [&](int row)
	{
		const int y = y0 + row;
		const int fy = (int)(((int64_t)(y - view->y) * 2 + 1) * rh * 128 / vh) - 128;	// 8.8 fixed, texel centres
		const int sy0 = std::max(0, fy >> 8), sy1 = std::min(rh - 1, (fy >> 8) + 1);
		const uint32_t wy = fy < 0 ? 0 : (uint32_t)(fy & 255);
		const uint32_t *r0 = &s->ldr[(size_t)sy0 * rw], *r1 = &s->ldr[(size_t)sy1 * rw];
		uint32_t *out = &s->scene[(size_t)y * s->width];

		for (int x = x0; x < x1; x++)
		{
			const int fx = (int)(((int64_t)(x - view->x) * 2 + 1) * rw * 128 / vw) - 128;
			const int sx0 = std::max(0, fx >> 8), sx1 = std::min(rw - 1, (fx >> 8) + 1);
			const uint32_t wx = fx < 0 ? 0 : (uint32_t)(fx & 255);

			uint32_t c = 0;
			for (int shift = 0; shift < 24; shift += 8)
			{
				const uint32_t a = (r0[sx0] >> shift) & 255, bb = (r0[sx1] >> shift) & 255;
				const uint32_t cc = (r1[sx0] >> shift) & 255, d = (r1[sx1] >> shift) & 255;
				const uint32_t top = a * (256 - wx) + bb * wx;
				const uint32_t bot = cc * (256 - wx) + d * wx;
				c |= ((top * (256 - wy) + bot * wy) >> 16) << shift;
			}
			out[x] = c;
		}
	});

	// this frame becomes the history the next one looks back at
	std::swap(s->cur, s->prev);
	s->prev_camera = cam;
	s->prev_hash = s->frame.hash;
	s->have_history = true;

	const auto end = std::chrono::steady_clock::now();
	const auto ms = [](std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point c)
	{
		return std::chrono::duration<double, std::milli>(c - a).count();
	};
	snprintf(s->stats, sizeof(s->stats), "%dx%d %dspp %db: %.1f ms (build %.1f trace %.1f post %.1f) %zu dyn tris%s",
		rw, rh, samples, bounces, ms(start, end), ms(start, built), ms(built, traced), ms(traced, end),
		s->frame.tris.size(), still ? " still" : "");
}

void Present(pt_backend_t *b, const uint32_t *overlay)
{
	CpuBackend *s = Self(b);

	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	if (s->has_view)
		ClipView(s, x0, y0, x1, y1);

	for (int y = 0; y < s->height; y++)
	{
		const uint32_t *ov = &overlay[(size_t)y * s->width];
		const uint32_t *sc = &s->scene[(size_t)y * s->width];
		uint32_t *out = &s->dibbits[(size_t)y * s->width];
		const bool rowin = s->has_view && y >= y0 && y < y1;

		for (int x = 0; x < s->width; x++)
		{
			const uint32_t o = ov[x];
			const uint32_t a = o >> 24;
			const uint32_t bg = (rowin && x >= x0 && x < x1) ? sc[x] : 0;
			// overlay is R,G,B,A bytes; the DIB wants 0x00RRGGBB
			const uint32_t orgb = ((o & 0xff) << 16) | (o & 0xff00) | ((o >> 16) & 0xff);

			if (a == 255)
				out[x] = orgb;
			else if (a == 0)
				out[x] = bg;
			else
			{
				const uint32_t ia = 255 - a;
				const uint32_t rb = (((bg & 0xff00ff) * ia + 0x800080) >> 8) & 0xff00ff;
				const uint32_t g = (((bg & 0x00ff00) * ia + 0x008000) >> 8) & 0x00ff00;
				out[x] = orgb + (rb | g);
			}
		}
	}
	s->has_view = false;

	HDC dc = GetDC(s->hwnd);
	if (dc)
	{
		BitBlt(dc, 0, 0, s->width, s->height, s->memdc, 0, 0, SRCCOPY);
		ReleaseDC(s->hwnd, dc);
	}
}

const char *Stats(pt_backend_t *b)
{
	return Self(b)->stats;
}

} // namespace

extern "C" pt_backend_t *pt_cpu_create(const pt_create_t *ci, char *err, int errlen)
{
	InitTables();

	CpuBackend *s = new CpuBackend;
	s->base.name = "CPU path tracer";
	s->base.destroy = Destroy;
	s->base.load_world = LoadWorld;
	s->base.texture_create = TextureCreate;
	s->base.texture_destroy = TextureDestroy;
	s->base.render_view = RenderView;
	s->base.present = Present;
	s->base.stats = Stats;
	s->log = ci->log;
	s->hwnd = (HWND)ci->hwnd;
	s->width = ci->width;
	s->height = ci->height;
	s->scene.assign((size_t)ci->width * ci->height, 0);

	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
	bmi.bmiHeader.biWidth = ci->width;
	bmi.bmiHeader.biHeight = -ci->height;	// top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void *bits = nullptr;
	s->memdc = CreateCompatibleDC(nullptr);
	if (s->memdc)
		s->dib = CreateDIBSection(s->memdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!s->memdc || !s->dib || !bits)
	{
		snprintf(err, errlen, "could not create a %dx%d framebuffer", ci->width, ci->height);
		Destroy(&s->base);
		return nullptr;
	}
	s->dibbits = (uint32_t *)bits;
	s->olddib = SelectObject(s->memdc, s->dib);

	if (ci->log)
	{
		char msg[128];
		snprintf(msg, sizeof(msg), "CPU path tracer: %d threads, GDI presentation\n", s->pool.Threads());
		ci->log(msg);
	}
	return &s->base;
}
