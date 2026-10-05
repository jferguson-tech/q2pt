// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
//
// CPU backend: a unidirectional path tracer with next event estimation,
// presented through GDI.

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
#include <vector>

namespace {

using namespace pt;

const float kRayOffset = 0.03f;		// keeps bounce rays off the surface they leave
const int kLightCandidates = 8;		// per shading point
const float kMaxSample = 40.0f;		// luminance clamp per path, tames fireflies
const float kGlobalLightChance = 0.2f;	// how often a light is picked map wide instead of nearby

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

// ----------------------------------------------------------------- world

struct Texture
{
	int						width = 0, height = 0;
	std::vector<uint32_t>	pixels;
	Vec3					average{1, 1, 1};

	Vec3 Sample(float u, float v) const
	{
		int x = (int)std::floor(u * width) % width;
		int y = (int)std::floor(v * height) % height;
		if (x < 0) x += width;
		if (y < 0) y += height;
		return Decode(pixels[(size_t)y * width + x]);
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
	int			texture = -1;
	Vec3		emission;			// average
	Vec3		emission_per_texel;	// emission / average texel
	float		alpha = 1.0f;
	uint32_t	flags = 0;
	bool		emissive = false;
};

struct Tri
{
	Vec3		p0, e1, e2;
	Vec3		n;			// unit, towards the counter clockwise side
	float		area;
	float		uv[3][2];
	uint32_t	material;
};

struct Light
{
	uint32_t	tri;		// or ~0u for a point light
	Vec3		origin;		// point lights; centroid for triangles
	Vec3		emission;	// radiance for triangles, intensity for points
	float		pdf;		// chance of being picked
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
	{
		Texture &t = w->textures[i];
		t.width = in->textures[i].width;
		t.height = in->textures[i].height;
		const size_t count = (size_t)t.width * t.height;
		t.pixels.assign(in->textures[i].pixels, in->textures[i].pixels + count);

		Vec3 sum;
		for (size_t p = 0; p < count; p++)
			sum += Decode(t.pixels[p]);
		if (count)
			t.average = Max(sum / (float)count, Vec3(1e-4f));
	}

	w->materials.resize(in->num_materials);
	for (int i = 0; i < in->num_materials; i++)
	{
		Material &m = w->materials[i];
		const pt_material_t &src = in->materials[i];
		m.texture = (src.texture >= 0 && src.texture < in->num_textures) ? src.texture : -1;
		m.emission = Vec3(src.emission);
		m.alpha = src.alpha;
		m.flags = src.flags;
		m.emissive = MaxComponent(m.emission) > 0.0f && !(m.flags & PT_MAT_SKY);
		m.emission_per_texel = m.emission;
		if (m.texture >= 0)
		{
			const Vec3 avg = w->textures[m.texture].average;
			m.emission_per_texel = Vec3(m.emission.x / avg.x, m.emission.y / avg.y, m.emission.z / avg.z);
		}
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
		t.p0 = soup[(size_t)i * 3];
		t.e1 = soup[(size_t)i * 3 + 1] - t.p0;
		t.e2 = soup[(size_t)i * 3 + 2] - t.p0;
		const Vec3 c = Cross(t.e1, t.e2);
		const float len = Length(c);
		t.area = len * 0.5f;
		t.n = len > 0.0f ? c / len : Vec3(0, 0, 1);
		t.material = in->tri_materials[i] < (uint32_t)in->num_materials ? in->tri_materials[i] : 0;
	}
	w->bvh.Build(soup.data(), (uint32_t)in->num_triangles);

	// everything that emits, with a chance of being picked proportional to its power
	std::vector<float> power;
	for (int i = 0; i < in->num_triangles; i++)
	{
		const Tri &t = w->tris[i];
		const Material &m = w->materials[t.material];
		if (!m.emissive || t.area <= 1e-6f)
			continue;
		Light l;
		l.tri = (uint32_t)i;
		l.origin = t.p0 + (t.e1 + t.e2) * (1.0f / 3.0f);
		l.emission = m.emission;
		l.pdf = 0;
		w->lights.push_back(l);
		power.push_back(Luminance(m.emission) * t.area * kPi);
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

// ------------------------------------------------------------ integrator

Vec3 Albedo(const World &w, const Tri &t, const Material &m, float u, float v)
{
	if (m.texture < 0)
		return Vec3(1, 1, 1);
	const float b0 = 1.0f - u - v;
	return w.textures[m.texture].Sample(
		t.uv[0][0] * b0 + t.uv[1][0] * u + t.uv[2][0] * v,
		t.uv[0][1] * b0 + t.uv[1][1] * u + t.uv[2][1] * v);
}

// Unoccluded light arriving at p from one light, picked by resampling a few
// candidates in proportion to what each would contribute.
Vec3 Direct(const World &w, Vec3 p, Vec3 n, Rng &rng)
{
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
			const Vec3 d = y - p;
			const float dist2 = Dot(d, d);
			if (dist2 <= 1e-6f)
				continue;
			const float cosx = Dot(n, d) / std::sqrt(dist2);
			if (cosx <= 0.0f)
				continue;
			f = l.emission * (cosx / dist2);
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

	if (wsum <= 0.0f)
		return Vec3();

	Ray shadow;
	shadow.o = p + n * kRayOffset;
	shadow.d = chosen_y - shadow.o;
	shadow.tmin = 0.0f;
	shadow.tmax = 0.999f;
	const bool blocked = w.bvh.AnyHit(shadow, [&](uint32_t tri, float, float)
	{
		const Material &m = w.materials[w.tris[tri].material];
		return m.alpha >= 1.0f || rng.Float() < m.alpha;
	});
	if (blocked)
		return Vec3();

	return chosen_f * (wsum / (kLightCandidates * chosen_phat));
}

Vec3 Trace(const World &w, Ray ray, Rng &rng, int max_bounces)
{
	Vec3 radiance, throughput(1, 1, 1);

	for (int depth = 0; ; depth++)
	{
		Hit hit;
		const Tri *tri;
		const Material *mat;

		// surfaces that let light through are crossed at random
		for (int skips = 0; ; skips++)
		{
			if (!w.bvh.Intersect(ray, hit))
				return radiance;
			tri = &w.tris[hit.tri];
			mat = &w.materials[tri->material];
			if (mat->alpha >= 1.0f || skips >= 16 || rng.Float() < mat->alpha)
				break;
			ray.tmin = hit.t + 0.01f;
		}

		if (mat->flags & PT_MAT_SKY)
			return radiance + throughput * w.Sky(ray.d);

		const Vec3 p = ray.o + ray.d * hit.t;
		const bool front = Dot(tri->n, ray.d) < 0.0f;
		const Vec3 n = front ? tri->n : -tri->n;
		const Vec3 albedo = Albedo(w, *tri, *mat, hit.u, hit.v);

		// later bounces get emitters through Direct(), so only count them when seen
		if (depth == 0 && mat->emissive && front)
			radiance += throughput * mat->emission_per_texel * albedo;

		radiance += throughput * albedo * Direct(w, p, n, rng) * kInvPi;

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

		const float r1 = rng.Float(), r2 = rng.Float();
		const float r = std::sqrt(r1), phi = 2.0f * kPi * r2;
		Vec3 t, b;
		Basis(n, t, b);
		ray.o = p + n * kRayOffset;
		ray.d = t * (r * std::cos(phi)) + b * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1.0f - r1));
		ray.tmin = 0.0f;
		ray.tmax = FLT_MAX;
	}
}

// --------------------------------------------------------------- backend

// everything that invalidates what has been accumulated
struct AccumKey
{
	float		origin[3], forward[3], right[3], up[3];
	float		fov_x, fov_y;
	int			width, height, bounces;
	unsigned	world;
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
	unsigned				world_epoch = 0;

	std::vector<Vec3>		accum;			// sum of samples, render sized
	std::vector<uint32_t>	ldr;			// tone mapped, render sized
	int						rw = 0, rh = 0;
	int						accum_samples = 0;
	uint32_t				frame = 0;
	AccumKey				key{};
	char					stats[128] = "";
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
	s->world_epoch++;
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
		const float x = c[i];
		float y = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
		y = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
		out = (out << 8) | g_to_display[(int)(y * 4096.0f)];
	}
	return out;
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

	AccumKey key{};
	memcpy(key.origin, view->origin, sizeof(key.origin));
	memcpy(key.forward, view->forward, sizeof(key.forward));
	memcpy(key.right, view->right, sizeof(key.right));
	memcpy(key.up, view->up, sizeof(key.up));
	key.fov_x = view->fov_x;
	key.fov_y = view->fov_y;
	key.width = rw;
	key.height = rh;
	key.bounces = bounces;
	key.world = s->world_epoch;
	if (memcmp(&key, &s->key, sizeof(key)) || s->accum.size() != (size_t)rw * rh)
	{
		s->key = key;
		s->rw = rw;
		s->rh = rh;
		s->accum.assign((size_t)rw * rh, Vec3());
		s->ldr.assign((size_t)rw * rh, 0);
		s->accum_samples = 0;
	}

	const World &w = *s->world;
	const Vec3 origin(view->origin), forward(view->forward), right(view->right), up(view->up);
	const float tx = std::tan(view->fov_x * kPi / 360.0f);
	const float ty = std::tan(view->fov_y * kPi / 360.0f);
	const uint32_t frame = s->frame++;
	const float inv_total = view->exposure / (float)(s->accum_samples + samples);

	s->pool.Run(rh, [&](int y)
	{
		for (int x = 0; x < rw; x++)
		{
			Vec3 &acc = s->accum[(size_t)y * rw + x];
			for (int i = 0; i < samples; i++)
			{
				Rng rng(Hash((uint32_t)(y * rw + x), frame * (uint32_t)samples + (uint32_t)i));
				const float sx = (2.0f * (x + rng.Float()) / rw - 1.0f) * tx;
				const float sy = (1.0f - 2.0f * (y + rng.Float()) / rh) * ty;

				Ray ray;
				ray.o = origin;
				ray.d = Normalize(forward + right * sx + up * sy);
				ray.tmin = 0.0f;
				ray.tmax = FLT_MAX;

				Vec3 c = Trace(w, ray, rng, bounces);
				const float lum = Luminance(c);
				uint32_t bits;
				memcpy(&bits, &lum, sizeof(bits));
				if ((bits & 0x7f800000u) == 0x7f800000u)
					continue;		// NaN or infinite: drop the sample
				if (lum > kMaxSample)
					c *= kMaxSample / lum;
				acc += c;
			}
			s->ldr[(size_t)y * rw + x] = ToneMap(acc * inv_total);
		}
	});
	s->accum_samples += samples;

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

	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	snprintf(s->stats, sizeof(s->stats), "%dx%d %dspp %d bounces %.1f ms, %d accumulated, %d threads",
		rw, rh, samples, bounces, ms, s->accum_samples, s->pool.Threads());
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
