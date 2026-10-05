// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
//
// CPU backend: a unidirectional path tracer with next event estimation,
// temporal accumulation and an edge aware filter, presented through GDI.

#include "../include/pt.h"
#include "pt_pool.h"
#include "pt_trace.h"

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

const int kMaxFilterPasses = 4;

// With automatic exposure the picture's typical luminance is brought to this
// before the user's exposure is applied.
const float kTypicalTarget = 0.0054f;
const float kMinDemodulate = 0.02f;		// reflectance floor when lighting is divided by it

// kOver: light from see-through layers in front of the surface, which has no
// reflectance of its own to be multiplied by
// kFog: light scattered towards the eye by the air in front of the surface
enum { kDiffuse, kSpecular, kOver, kFog, kChannels };

uint8_t g_to_display[4097];

void InitTables()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	InitColourTables();
	for (int i = 0; i <= 4096; i++)
		g_to_display[i] = (uint8_t)(std::pow(i / 4096.0f, 1.0f / 2.2f) * 255.0f + 0.5f);
}

struct Camera
{
	Vec3	origin, forward, right, up;
	float	tx = 0, ty = 0;		// tangents of the half angles

	bool operator==(const Camera &o) const { return !memcmp(this, &o, sizeof(*this)); }
};

/*
Per pixel state at render resolution.

Lighting is kept apart from the reflectance of the surface it falls on, and
split by the lobe that reflects it: light[kDiffuse] is multiplied by
albedo[kDiffuse], light[kSpecular] by albedo[kSpecular], with `add` on top
for what needs no filtering (what the surface emits, the frame's point
lights). That lets the noisy part be averaged over time and space without
smearing textures.
*/
struct Pixels
{
	std::vector<Vec3>	pos;
	std::vector<Vec3>	seen;		// where it appears to be: along the eye's ray, even if water bent the view
	std::vector<uint8_t> bent;		// seen through refraction
	std::vector<uint8_t> moved;		// on something that was elsewhere last frame; seen is where
	std::vector<Vec3>	spec_pos;	// where a mirror-like surface's reflection appears to be
	std::vector<uint8_t> spec_ok;	// spec_pos is set
	std::vector<Vec3>	plane;		// geometric normal
	std::vector<Vec3>	normal;		// shading normal
	std::vector<float>	depth;		// along the ray; negative where there is no surface
	std::vector<float>	roughness;
	std::vector<Vec3>	add;
	std::vector<Vec3>	albedo[kChannels];
	std::vector<Vec3>	light[kChannels];		// this frame's samples, then accumulated
	std::vector<float>	m1[kChannels], m2[kChannels];	// luminance moments of light
	std::vector<float>	variance[kChannels];
	std::vector<float>	length;		// frames accumulated
	std::vector<Vec3>	over_pos;	// where what kOver shows appears to be: a reflection sits behind the glass
	std::vector<float>	over_length;	// frames accumulated in kOver, which is followed separately

	void Resize(size_t n)
	{
		pos.assign(n, Vec3());
		seen.assign(n, Vec3());
		bent.assign(n, 0);
		moved.assign(n, 0);
		spec_pos.assign(n, Vec3());
		spec_ok.assign(n, 0);
		plane.assign(n, Vec3());
		normal.assign(n, Vec3());
		depth.assign(n, -1.0f);
		roughness.assign(n, 1.0f);
		add.assign(n, Vec3());
		length.assign(n, 0.0f);
		over_pos.assign(n, Vec3());
		over_length.assign(n, 0.0f);
		for (int c = 0; c < kChannels; c++)
		{
			albedo[c].assign(n, Vec3());
			light[c].assign(n, Vec3());
			m1[c].assign(n, 0.0f);
			m2[c].assign(n, 0.0f);
			variance[c].assign(n, 0.0f);
		}
	}
};

// What the filter reads, packed so that a neighbour is two cache lines
// rather than a dozen
struct FilterGeo
{
	Vec3	pos;
	float	depth;
	Vec3	normal;
	float	roughness;
	Vec3	plane;
	float	pad;
};

// one lane per channel, the fourth unused
struct FilterLight
{
	float	r[4], g[4], b[4];
	float	var[4];
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
	int				shown[4] = {0, 0, 0, 0};	// the part of the window the last view covered
	pt_view_t		view{};

	Pool					pool;
	std::unique_ptr<World>	world;
	std::vector<std::unique_ptr<Texture>> textures;	// by handle
	Frame					frame;

	Pixels					cur, prev;
	std::vector<FilterGeo>	filter_geo;
	std::vector<FilterLight> filter_a, filter_b;
	std::vector<Vec3>		hdr;			// this frame's picture, at traced resolution
	std::vector<Vec3>		near_lo, near_hi;	// range of hdr around each traced pixel
	std::vector<Vec3>		bloom_a, bloom_b;
	// the picture blended over time, at the view's resolution
	std::vector<Vec3>		steady, steady_prev;
	std::vector<float>		steady_count, steady_count_prev;	// how much is in each pixel's blend
	int						out_w = 0, out_h = 0;
	float					auto_exposure = 1.0f;
	float					prev_time = 0.0f;
	std::vector<uint32_t>	ldr;			// tone mapped, render sized
	int						rw = 0, rh = 0;
	bool					have_history = false;
	bool					antialiased = false;	// last frame was
	float					jitter_x = 0.0f, jitter_y = 0.0f;	// this frame's offset within the pixel
	float					moving_history = 32.0f;	// frames of lighting kept while anything changes
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

void TextureUpdate(pt_backend_t *b, int handle, const uint32_t *pixels)
{
	CpuBackend *s = Self(b);
	if (handle < 0 || handle >= (int)s->textures.size() || !s->textures[handle] || !pixels)
		return;
	Texture &t = *s->textures[handle];
	t.pixels.assign(pixels, pixels + (size_t)t.width * t.height);
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

// How the finished picture is graded for the screen
struct Grade
{
	int		curve = 0;			// 0 filmic, 1 neutral, 2 clipped like the original
	float	saturation = 1.0f;
	float	contrast = 1.0f;
};

uint32_t ToneMap(Vec3 c, const Grade &g)
{
	float v[3];
	for (int i = 0; i < 3; i++)
	{
		const float x = c[i] > 0.0f ? c[i] : 0.0f;
		float y;
		switch (g.curve)
		{
		case 1:		// rolls off gently towards white at 4
			y = x * (1.0f + x * (1.0f / 16.0f)) / (1.0f + x);
			break;
		case 2:
			y = x;
			break;
		default:	// Narkowicz's fit to the ACES filmic curve
			y = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
			break;
		}
		v[i] = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
	}

	if (g.saturation != 1.0f || g.contrast != 1.0f)
	{
		const float lum = 0.2126f * v[0] + 0.7152f * v[1] + 0.0722f * v[2];
		for (int i = 0; i < 3; i++)
		{
			float y = lum + (v[i] - lum) * g.saturation;
			if (y < 0.0f) y = 0.0f;
			if (g.contrast != 1.0f)
				y = 0.18f * std::pow(y * (1.0f / 0.18f), g.contrast);	// pivots on mid grey
			v[i] = y > 1.0f ? 1.0f : y;
		}
	}

	return ((uint32_t)g_to_display[(int)(v[0] * 4096.0f)] << 16)
		| ((uint32_t)g_to_display[(int)(v[1] * 4096.0f)] << 8)
		| (uint32_t)g_to_display[(int)(v[2] * 4096.0f)];
}

Vec3 Demodulate(Vec3 light, Vec3 reflectance)
{
	return Vec3(light.x / reflectance.x, light.y / reflectance.y, light.z / reflectance.z);
}

// Light meeting the boundary between two clear materials: how much is
// reflected, and the cosine of the angle the rest leaves at. eta is the
// refractive index on the side the light comes from over that on the other.
// Returns 1 when nothing gets through (total internal reflection).
float Dielectric(float cosi, float eta, float &cost)
{
	const float sin2t = eta * eta * (1.0f - cosi * cosi);
	if (sin2t >= 1.0f)
	{
		cost = 0.0f;
		return 1.0f;
	}
	cost = std::sqrt(1.0f - sin2t);
	const float rs = (eta * cosi - cost) / (eta * cosi + cost);
	const float rp = (cosi - eta * cost) / (cosi + eta * cost);
	return 0.5f * (rs * rs + rp * rp);
}

const float kGlassIndex = 1.5f;
const float kWaterIndex = 1.33f;

// what is left of light after a distance through something that absorbs it
Vec3 Fade(Vec3 absorb, float distance)
{
	if (distance <= 0.0f || MaxComponent(absorb) <= 0.0f)
		return Vec3(1, 1, 1);
	return Vec3(std::exp(-absorb.x * distance), std::exp(-absorb.y * distance), std::exp(-absorb.z * distance));
}

// Traces one pixel: what the eye sees there, and samples of the light on it
void TracePixel(CpuBackend *s, const Scene &sc, const Camera &cam, float jx, float jy, int x, int y, int samples, int bounces)
{
	const size_t i = (size_t)y * s->rw + x;
	Pixels &px = s->cur;
	Rng rng(Hash((uint32_t)i, s->frame_index));

	Ray ray;
	ray.o = cam.origin;
	ray.d = Normalize(cam.forward
		+ cam.right * ((2.0f * (x + 0.5f + jx) / s->rw - 1.0f) * cam.tx)
		+ cam.up * ((1.0f - 2.0f * (y + 0.5f + jy) / s->rh) * cam.ty));
	ray.tmin = 0.0f;
	ray.tmax = FLT_MAX;
	const Vec3 eye_dir = ray.d;

	px.depth[i] = -1.0f;
	px.add[i] = Vec3();
	px.over_pos[i] = Vec3();
	px.bent[i] = 0;
	px.moved[i] = 0;
	px.spec_ok[i] = 0;
	for (int c = 0; c < kChannels; c++)
	{
		px.albedo[c][i] = Vec3();
		px.light[c][i] = Vec3();
		px.m1[c][i] = px.m2[c][i] = 0.0f;
	}

	// Walk through whatever is see-through to the first solid surface. The
	// solid surface is what the filters work on; the layers in front dim it
	// and add their own light on top.
	float through = 1.0f;		// how much of what is behind still shows
	Vec3 tint(1, 1, 1);			// what liquid on the way has left of each colour
	Vec3 absorb(s->view.medium_absorb);	// of the liquid the path is in now; none in air
	float entered = 0.0f;		// where along the current ray that began
	float travelled = 0.0f;		// along the path so far, which water may have bent
	Vec3 front_add;				// from the layers: what they emit
	Vec3 front_diffuse;			// what they scatter, which is noisy
	Vec3 front_mirror;			// and what they mirror, noisy too
	float mirror_weight = 0.0f;	// of the strongest reflection so far
	bool have_layer = false;	// first_layer is set
	Surface first_layer;
	float first_layer_depth = 0.0f;
	bool solid = false;
	Hit hit;
	const Tri *tri;
	Surface surf;
	Vec3 sky;

	for (int layer = 0; through > 0.001f; layer++)
	{
		if (!Closest(sc, ray, rng, true, false, hit, tri))
			break;
		if (tri->mat->flags & PT_MAT_SKY)
		{
			sky = sc.Sky(ray.d) * through;
			break;
		}
		MakeSurface(sc, *tri, hit, ray, surf, sc.filter_textures);
		const Material &mat = *surf.mat;
		tint *= Fade(absorb, hit.t - entered);
		entered = hit.t;
		if (mat.alpha >= 1.0f || layer >= 8)
		{
			solid = true;
			break;
		}

		if (!have_layer)
		{
			have_layer = true;
			first_layer = surf;
			first_layer_depth = travelled + hit.t;
		}
		px.over_pos[i] = mirror_weight > 0.0f ? px.over_pos[i] : surf.p;

		// Glass and water are clear materials with a tint painted on. The
		// boundary reflects some of the light, more at a glancing angle, and
		// lets the rest in; water bends it on the way.
		bool bent = false;
		if (!(mat.flags & PT_MAT_BLACK) && surf.roughness < kLightSampledRoughness)
		{
			const bool liquid = (mat.flags & PT_MAT_WAVES) != 0 && sc.refraction;
			const float cosi = std::min(1.0f, std::max(0.0f, Dot(surf.n, surf.wo)));
			float fresnel, cost = 0.0f;
			float eta = 1.0f;
			if (liquid)
			{
				// its underside faces down: there the eye is in the water looking out
				eta = tri->n.z < -0.5f ? kWaterIndex : 1.0f / kWaterIndex;
				fresnel = Dielectric(cosi, eta, cost);
			}
			else
			{
				// a pane: in one face and out the other with no net bend, and
				// both faces reflect
				const float one = Dielectric(cosi, 1.0f / kGlassIndex, cost);
				fresnel = 2.0f * one / (1.0f + one);
			}

			// with reflections off the light simply all goes through
			if (sc.reflections < 1 && fresnel < 1.0f)
				fresnel = 0.0f;
			if (fresnel > 0.0f)
			{
				Ray mirror;
				mirror.o = surf.p + surf.ng * kRayOffset;
				mirror.d = surf.n * (2.0f * Dot(surf.n, surf.wo)) - surf.wo;
				if (Dot(mirror.d, surf.ng) < 0.0f)
					mirror.d = mirror.d - surf.ng * (2.0f * Dot(mirror.d, surf.ng));
				mirror.tmin = 0.0f;
				mirror.tmax = FLT_MAX;

				float reached = 0.0f;
				const float weight = through * fresnel;
				front_mirror += ClampSample(Radiance(sc, mirror, rng, false, true, 1, sc.reflection_bounces, &reached), sc.max_sample) * weight;

				// A reflection appears to sit behind the glass, as far again
				// as the thing reflected is in front. That is where to look
				// for it in last frame's picture.
				if (weight > mirror_weight)
				{
					mirror_weight = weight;
					px.over_pos[i] = cam.origin + eye_dir * (travelled + hit.t + std::min(reached, 100000.0f));
				}
			}
			through *= 1.0f - fresnel;

			// going in, the liquid starts soaking up light; coming out, it stops
			if (mat.flags & PT_MAT_WAVES)
				absorb = tri->n.z < -0.5f ? Vec3() : mat.absorb;

			if (liquid && fresnel < 1.0f)
			{
				// into (or out of) the water
				travelled += hit.t;
				ray.o = surf.p - surf.ng * kRayOffset;
				ray.d = Normalize(ray.d * eta + surf.n * (eta * cosi - cost));
				if (Dot(ray.d, surf.ng) > 0.0f)
					ray.d = Normalize(ray.d - surf.ng * (2.0f * Dot(ray.d, surf.ng)));
				ray.tmin = 0.0f;
				entered = 0.0f;
				bent = true;
			}
		}

		// the painted tint: covers its share of what is behind and shows lit itself
		const float share = through * mat.alpha;
		if (mat.emissive && surf.front)
			front_add += Emitted(surf, true) * share;
		if (share > 0.0f && MaxComponent(surf.kd) > 0.0f)
		{
			const Lit world = DirectWorld(sc, surf, rng, true), frame = DirectFrameOne(sc, surf, rng);
			front_diffuse += surf.kd * (world.diffuse + frame.diffuse) * (kInvPi * share);
		}
		through *= 1.0f - mat.alpha;
		if (!bent)
			ray.tmin = hit.t + 0.01f;
	}

	// the layers' light is noisy and gets a channel of its own
	const Vec3 over = ClampSample(front_diffuse + front_mirror, sc.max_sample);
	const float over_lum = Luminance(over);
	// whether there is a layer must not depend on what this frame's sample
	// happened to find, or its history would start over at random
	if (have_layer)
	{
		px.albedo[kOver][i] = Vec3(1, 1, 1);
		px.light[kOver][i] = over;
		px.m1[kOver][i] = over_lum;
		px.m2[kOver][i] = over_lum * over_lum;
	}

	if (!solid)
	{
		// Nothing solid behind: sky, or nothing at all. If a layer was
		// crossed it stands in as the surface, so that its light is still
		// averaged over time.
		px.add[i] = front_add + sky * tint;
		if (have_layer)
		{
			px.pos[i] = first_layer.p;
			px.seen[i] = first_layer.p;
			px.plane[i] = first_layer.ng;
			px.normal[i] = first_layer.n;
			px.depth[i] = first_layer_depth;
			px.roughness[i] = first_layer.roughness;
		}
		return;
	}

	const Material &mat = *surf.mat;
	const Vec3 kd = surf.kd * through;
	const Vec3 spec_albedo = Max(surf.SpecularAlbedo(), Vec3(kMinDemodulate));
	const Vec3 ks = spec_albedo * through;
	const bool has_diffuse = MaxComponent(surf.kd) > 0.0f;
	const bool has_specular = MaxComponent(surf.f0) > 0.0f;

	px.pos[i] = surf.p;
	px.seen[i] = travelled > 0.0f ? cam.origin + eye_dir * (travelled + hit.t) : surf.p;
	px.bent[i] = travelled > 0.0f;
	if (!px.bent[i] && (hit.tri & kDynamic) && !sc.frame->prev.empty())
	{
		const Vec3 *corner = &sc.frame->prev[(size_t)(hit.tri & ~kDynamic) * 3];
		const Vec3 before = corner[0] * (1.0f - hit.u - hit.v) + corner[1] * hit.u + corner[2] * hit.v;
		const Vec3 shift = before - surf.p;
		if (Dot(shift, shift) > 1e-6f)
		{
			// history is looked up where this point was, not where it is
			px.seen[i] = before;
			px.moved[i] = 1;
		}
	}
	px.plane[i] = surf.ng;
	px.normal[i] = surf.n;
	px.depth[i] = travelled + hit.t;
	px.roughness[i] = surf.roughness;
	px.albedo[kDiffuse][i] = kd * tint;
	px.albedo[kSpecular][i] = ks * tint;

	// exact, so it skips the filters
	const Lit flash = DirectFrameAll(sc, surf, rng);
	px.add[i] = front_add + (surf.kd * flash.diffuse * kInvPi + flash.specular) * tint * through;
	if (mat.emissive && surf.front)
		px.add[i] += Emitted(surf, true) * tint * through;

	// Air that scatters light: some of what the surface sends is lost on the
	// way, and the air itself glows where light falls through it, which is
	// what shows as shafts. One point along the way is sampled per frame.
	if (sc.fog_density > 0.0f)
	{
		const float reach = have_layer ? first_layer_depth : hit.t;		// the straight part of the view
		const float at = rng.Float() * reach;
		const Vec3 lit = DirectMedium(sc, cam.origin + eye_dir * at, rng);
		const Vec3 glow = ClampSample(lit * (sc.fog_density * (0.25f * kInvPi) * std::exp(-sc.fog_density * at) * reach),
			sc.max_sample);
		const float glow_lum = Luminance(glow);
		px.albedo[kFog][i] = Vec3(1, 1, 1);
		px.light[kFog][i] = glow;
		px.m1[kFog][i] = glow_lum;
		px.m2[kFog][i] = glow_lum * glow_lum;

		const float kept = std::exp(-sc.fog_density * px.depth[i]);
		px.albedo[kDiffuse][i] *= kept;
		px.albedo[kSpecular][i] *= kept;
		px.albedo[kOver][i] *= kept;
		px.add[i] *= kept;
	}

	// a specular path costs as much as a diffuse one; where the lobe reflects
	// little, take it only some of the time
	const float spec_chance = (!has_specular || sc.reflections < 2) ? 0.0f
		: std::min(1.0f, std::max(0.1f, Luminance(surf.SpecularAlbedo()) * 10.0f) * sc.reflection_rate);

	// Paths are best spent where the picture has little to go on: on what
	// has just come into view, and on what is still noisy. What last frame
	// knew of this point says which that is.
	if (sc.adaptive > 1)
	{
		float known = 0.0f, noise = 0.0f;
		if (s->have_history)
		{
			const Camera &pc = s->prev_camera;
			const Pixels &prev = s->prev;
			const Vec3 v = px.seen[i] - pc.origin;
			const float z = Dot(v, pc.forward);
			if (z > 0.01f)
			{
				const int qx = (int)std::floor((Dot(v, pc.right) / (z * pc.tx) * 0.5f + 0.5f) * s->rw);
				const int qy = (int)std::floor((0.5f - Dot(v, pc.up) / (z * pc.ty) * 0.5f) * s->rh);
				if (qx >= 0 && qy >= 0 && qx < s->rw && qy < s->rh)
				{
					const size_t q = (size_t)qy * s->rw + qx;
					if (prev.depth[q] > 0.0f && std::fabs(prev.depth[q] - px.depth[i]) < 0.1f * px.depth[i])
					{
						known = prev.length[q];
						const float mean = prev.m1[kDiffuse][q];
						noise = std::sqrt(std::max(0.0f, prev.m2[kDiffuse][q] - mean * mean)) / (mean + 0.01f);
					}
				}
			}
		}
		if (known < 2.0f)
			samples *= sc.adaptive;
		else if (known < 8.0f)
			samples *= std::max(1, sc.adaptive / 2);
		else if (noise > 2.0f && known < 32.0f)
			samples *= 2;
	}

	Vec3 sum[2];
	float m1[2] = {}, m2[2] = {};
	float spec_reach = -1.0f;
	for (int k = 0; k < samples; k++)
	{
		Vec3 c[2];

		const Lit direct = DirectWorld(sc, surf, rng, true);
		c[kDiffuse] = direct.diffuse * kInvPi;
		c[kSpecular] = Demodulate(direct.specular, spec_albedo);

		if (bounces > 0)
		{
			Ray bounce;
			bounce.o = surf.p + surf.ng * kRayOffset;
			bounce.tmin = 0.0f;
			bounce.tmax = FLT_MAX;

			if (has_diffuse)
			{
				bounce.d = SampleDiffuse(surf, rng);
				c[kDiffuse] += Radiance(sc, bounce, rng, false, false, 1, bounces);
			}
			if (spec_chance > 0.0f && rng.Float() < spec_chance)
			{
				Vec3 weight;
				float reached = 0.0f;
				if (SampleSpecular(surf, rng, bounce.d, weight))
				{
					c[kSpecular] += Demodulate(
						weight * Radiance(sc, bounce, rng, false, !surf.light_sampled_spec, 1, sc.reflection_bounces, &reached),
						spec_albedo) * (1.0f / spec_chance);
					spec_reach = reached;
				}
			}
		}

		for (int ch = 0; ch < 2; ch++)
		{
			c[ch] = ClampSample(c[ch], sc.max_sample);
			const float lum = Luminance(c[ch]);
			sum[ch] += c[ch];
			m1[ch] += lum;
			m2[ch] += lum * lum;
		}
	}

	// A reflection in something close to a mirror appears to sit behind the
	// surface, as far again as the thing reflected is in front of it. That is
	// where to look for it in last frame's picture.
	if (spec_reach >= 0.0f && surf.roughness < kLightSampledRoughness && !px.moved[i] && !px.bent[i])
	{
		px.spec_pos[i] = cam.origin + eye_dir * (hit.t + std::min(spec_reach, 100000.0f));
		px.spec_ok[i] = 1;
	}

	const float inv = 1.0f / samples;
	for (int ch = 0; ch < 2; ch++)
	{
		px.light[ch][i] = sum[ch] * inv;
		px.m1[ch][i] = m1[ch] * inv;
		px.m2[ch][i] = m2[ch] * inv;
	}
}

// Is what pixel q showed last frame the same surface pixel i shows now?
// Normally: on the same plane, facing the same way. Seen through water the
// view wobbles with the ripples and lands on different spots of a bed that
// need not be flat, so there the test is only that it is about as far away.
bool SameSurface(const Pixels &cur, const Pixels &prev, size_t i, size_t q)
{
	if (cur.bent[i])
		return prev.bent[q] && std::fabs(prev.depth[q] - cur.depth[i]) < 0.1f * cur.depth[i] + 1.0f;
	if (cur.moved[i])
	{
		// cur.seen is where this point was last frame: was that what q showed?
		const Vec3 off = prev.pos[q] - cur.seen[i];
		const float slack = 2.0f + cur.depth[i] * 0.02f;
		return Dot(off, off) <= slack * slack;
	}
	return !prev.bent[q]
		&& std::fabs(Dot(cur.plane[i], prev.pos[q] - cur.pos[i])) <= 1.0f + cur.depth[i] * 0.01f
		&& Dot(cur.plane[i], prev.plane[q]) >= 0.9f;
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
		cur.over_length[i] = 0.0f;
		for (int c = 0; c < kChannels; c++)
			cur.variance[c][i] = 0.0f;
		if (cur.depth[i] < 0.0f)
			continue;

		const Vec3 spec_sample = cur.light[kSpecular][i];
		const float spec_m1 = cur.m1[kSpecular][i], spec_m2 = cur.m2[kSpecular][i];
		const bool has_over = cur.albedo[kOver][i].x > 0.0f;
		const Vec3 over_sample = cur.light[kOver][i];
		const float over_m1 = cur.m1[kOver][i], over_m2 = cur.m2[kOver][i];

		Vec3 hist[kChannels];
		float hm1[kChannels] = {}, hm2[kChannels] = {}, hlen = 0.0f, wsum = 0.0f;
		bool have_spot = false;
		int spot_x = 0, spot_y = 0;	// the pixel this point was nearest to last frame

		if (s->have_history)
		{
			// where was this point on screen last frame?
			const Vec3 v = cur.seen[i] - prev_cam.origin;
			const float z = Dot(v, prev_cam.forward);
			if (z > 0.01f)
			{
				// Less this frame's offset within the pixel: history belongs to
				// pixels, and with the eye at rest must come from the very same
				// one, or it would be resampled and crawl about every frame.
				const float fx = (Dot(v, prev_cam.right) / (z * prev_cam.tx) * 0.5f + 0.5f) * rw - 0.5f - s->jitter_x;
				const float fy = (0.5f - Dot(v, prev_cam.up) / (z * prev_cam.ty) * 0.5f) * rh - 0.5f - s->jitter_y;
				const int ix = (int)std::floor(fx + 0.001f), iy = (int)std::floor(fy + 0.001f);
				have_spot = true;
				spot_x = (int)std::floor(fx + 0.5f);
				spot_y = (int)std::floor(fy + 0.5f);
				const float ax = fx - ix, ay = fy - iy;

				for (int t = 0; t < 4; t++)
				{
					const int qx = ix + (t & 1), qy = iy + (t >> 1);
					if (qx < 0 || qy < 0 || qx >= rw || qy >= rh)
						continue;
					const size_t q = (size_t)qy * rw + qx;
					if (prev.depth[q] < 0.0f || prev.length[q] <= 0.0f)
						continue;
					if (!SameSurface(cur, prev, i, q))
						continue;
					const float w = ((t & 1) ? ax : 1.0f - ax) * ((t >> 1) ? ay : 1.0f - ay);
					if (w <= 0.0f)
						continue;
					for (int c = 0; c < kChannels; c++)
					{
						hist[c] += prev.light[c][q] * w;
						hm1[c] += prev.m1[c][q] * w;
						hm2[c] += prev.m2[c][q] * w;
					}
					hlen += prev.length[q] * w;
					wsum += w;
				}
			}
		}

		if (wsum <= 0.01f && s->have_history && have_spot)
		{
			// Nothing usable right where this point was. Along an edge a
			// pixel shows one surface on some frames and its neighbour on
			// others, so the same surface is usually one pixel away.
			for (int dy = -1; dy <= 1; dy++)
			{
				for (int dx = -1; dx <= 1; dx++)
				{
					const int qx = spot_x + dx, qy = spot_y + dy;
					if (qx < 0 || qy < 0 || qx >= rw || qy >= rh)
						continue;
					const size_t q = (size_t)qy * rw + qx;
					if (prev.depth[q] < 0.0f || prev.length[q] <= 0.0f)
						continue;
					if (!SameSurface(cur, prev, i, q))
						continue;
					for (int c = 0; c < kChannels; c++)
					{
						hist[c] += prev.light[c][q];
						hm1[c] += prev.m1[c][q];
						hm2[c] += prev.m2[c][q];
					}
					hlen += prev.length[q];
					wsum += 1.0f;
				}
			}
		}

		float len = 1.0f;
		if (wsum <= 0.01f && s->have_history)
		{
			// Nothing where this point used to be: it may belong to something
			// that moves with the eye, like the weapon in hand. If the same
			// pixel showed much the same surface last frame, carry on from
			// that, but not for long, since it is a guess.
			if (prev.depth[i] > 0.0f && prev.length[i] > 0.0f &&
				std::fabs(prev.depth[i] - cur.depth[i]) < 0.1f * cur.depth[i] &&
				Dot(cur.plane[i], prev.plane[i]) > 0.8f)
			{
				for (int c = 0; c < kChannels; c++)
				{
					hist[c] = prev.light[c][i];
					hm1[c] = prev.m1[c][i];
					hm2[c] = prev.m2[c][i];
				}
				hlen = std::min(prev.length[i], 8.0f);
				wsum = 1.0f;
			}
		}
		if (wsum > 0.01f)
		{
			const float inv = 1.0f / wsum;
			len = std::min(hlen * inv + 1.0f, max_history);
			for (int c = 0; c < kChannels; c++)
			{
				// a mirror shows something else as soon as the eye moves, so
				// smooth reflections keep less of the past while things change
				float keep = len;
				if (c == kSpecular && max_history <= s->moving_history)
					keep = std::min(len, std::max(2.0f, s->moving_history * cur.roughness[i] * 2.0f));
				const float a = 1.0f / keep;
				const Vec3 h = hist[c] * inv;
				const float h1 = hm1[c] * inv, h2 = hm2[c] * inv;
				cur.light[c][i] = h + (cur.light[c][i] - h) * a;
				cur.m1[c][i] = h1 + (cur.m1[c][i] - h1) * a;
				cur.m2[c][i] = h2 + (cur.m2[c][i] - h2) * a;
			}
		}
		cur.length[i] = len;

		// how unsure the average still is; with little history, assume very
		for (int c = 0; c < kChannels; c++)
		{
			float var = std::max(0.0f, cur.m2[c][i] - cur.m1[c][i] * cur.m1[c][i]) / len;
			if (len < 4.0f)
				var = std::max(var, cur.m1[c][i] * cur.m1[c][i] * 0.25f + 0.01f);
			cur.variance[c][i] = var;
		}

		// The same goes for what a mirror-like solid surface reflects.
		if (cur.spec_ok[i] && s->have_history)
		{
			const Vec3 v = cur.spec_pos[i] - prev_cam.origin;
			const float z = Dot(v, prev_cam.forward);
			Vec3 sh;
			float sh1 = 0.0f, sh2 = 0.0f, sw = 0.0f;
			if (z > 0.01f)
			{
				const float fx = (Dot(v, prev_cam.right) / (z * prev_cam.tx) * 0.5f + 0.5f) * rw - 0.5f - s->jitter_x;
				const float fy = (0.5f - Dot(v, prev_cam.up) / (z * prev_cam.ty) * 0.5f) * rh - 0.5f - s->jitter_y;
				const int ix = (int)std::floor(fx + 0.001f), iy = (int)std::floor(fy + 0.001f);
				const float ax = fx - ix, ay = fy - iy;

				for (int t = 0; t < 4; t++)
				{
					const int qx = ix + (t & 1), qy = iy + (t >> 1);
					if (qx < 0 || qy < 0 || qx >= rw || qy >= rh)
						continue;
					const size_t q = (size_t)qy * rw + qx;
					// any surface about as smooth, facing much the same way
					if (prev.depth[q] < 0.0f || prev.length[q] <= 0.0f
						|| std::fabs(prev.roughness[q] - cur.roughness[i]) > 0.1f
						|| Dot(cur.plane[i], prev.plane[q]) < 0.8f)
						continue;
					const float w = ((t & 1) ? ax : 1.0f - ax) * ((t >> 1) ? ay : 1.0f - ay);
					if (w <= 0.0f)
						continue;
					sh += prev.light[kSpecular][q] * w;
					sh1 += prev.m1[kSpecular][q] * w;
					sh2 += prev.m2[kSpecular][q] * w;
					sw += w;
				}
			}
			if (sw > 0.01f)
			{
				const float inv = 1.0f / sw;
				const float n = std::min(std::max(cur.length[i], 2.0f), max_history);
				const float a = 1.0f / n;
				cur.light[kSpecular][i] = sh * inv + (spec_sample - sh * inv) * a;
				cur.m1[kSpecular][i] = sh1 * inv + (spec_m1 - sh1 * inv) * a;
				cur.m2[kSpecular][i] = sh2 * inv + (spec_m2 - sh2 * inv) * a;
				cur.variance[kSpecular][i] = std::max(0.0f,
					cur.m2[kSpecular][i] - cur.m1[kSpecular][i] * cur.m1[kSpecular][i]) / n;
			}
		}

		// Reflections in glass and water do not move across the screen the
		// way the surface behind them does, so their history is looked up
		// where they appear to be instead.
		if (has_over)
		{
			Vec3 oh;
			float oh1 = 0.0f, oh2 = 0.0f, olen = 0.0f, ow = 0.0f;
			if (s->have_history)
			{
				const Vec3 v = cur.over_pos[i] - prev_cam.origin;
				const float z = Dot(v, prev_cam.forward);
				if (z > 0.01f)
				{
					const float fx = (Dot(v, prev_cam.right) / (z * prev_cam.tx) * 0.5f + 0.5f) * rw - 0.5f - s->jitter_x;
					const float fy = (0.5f - Dot(v, prev_cam.up) / (z * prev_cam.ty) * 0.5f) * rh - 0.5f - s->jitter_y;
					const int ix = (int)std::floor(fx + 0.001f), iy = (int)std::floor(fy + 0.001f);
					const float ax = fx - ix, ay = fy - iy;

					for (int t = 0; t < 4; t++)
					{
						const int qx = ix + (t & 1), qy = iy + (t >> 1);
						if (qx < 0 || qy < 0 || qx >= rw || qy >= rh)
							continue;
						const size_t q = (size_t)qy * rw + qx;
						// Any layer light found there will do. How far away a
						// reflection appears to be changes from frame to frame on
						// rippling water, so it cannot be used to tell reflections apart.
						if (prev.over_length[q] <= 0.0f)
							continue;
						const float w = ((t & 1) ? ax : 1.0f - ax) * ((t >> 1) ? ay : 1.0f - ay);
						if (w <= 0.0f)
							continue;
						oh += prev.light[kOver][q] * w;
						oh1 += prev.m1[kOver][q] * w;
						oh2 += prev.m2[kOver][q] * w;
						olen += prev.over_length[q] * w;
						ow += w;
					}
				}
			}

			float n = 1.0f;
			cur.light[kOver][i] = over_sample;
			cur.m1[kOver][i] = over_m1;
			cur.m2[kOver][i] = over_m2;
			if (ow > 0.01f)
			{
				const float inv = 1.0f / ow;
				n = std::min(olen * inv + 1.0f, max_history);
				const float a = 1.0f / n;
				cur.light[kOver][i] = oh * inv + (over_sample - oh * inv) * a;
				cur.m1[kOver][i] = oh1 * inv + (over_m1 - oh1 * inv) * a;
				cur.m2[kOver][i] = oh2 * inv + (over_m2 - oh2 * inv) * a;
			}
			cur.over_length[i] = n;

			float var = std::max(0.0f, cur.m2[kOver][i] - cur.m1[kOver][i] * cur.m1[kOver][i]) / n;
			if (n < 4.0f)
				var = std::max(var, cur.m1[kOver][i] * cur.m1[kOver][i] * 0.25f + 0.01f);
			cur.variance[kOver][i] = var;
		}
	}
}

// One pass of an a-trous wavelet filter. Neighbours count for less the more
// they differ in plane, facing or brightness, and brightness matters less
// where the estimate is still noisy. Sharp reflections are left sharper.
// The three channels ride in the lanes of one SSE register.
void FilterRow(int rw, int rh, const FilterGeo *geo, const FilterLight *in, FilterLight *out, int step, int y)
{
	static const float kernel[5] = {1.0f / 16, 1.0f / 4, 3.0f / 8, 1.0f / 4, 1.0f / 16};
	const __m128 lum_r = _mm_set1_ps(0.2126f), lum_g = _mm_set1_ps(0.7152f), lum_b = _mm_set1_ps(0.0722f);
	const __m128 sign = _mm_set1_ps(-0.0f), zero = _mm_setzero_ps(), one = _mm_set1_ps(1.0f);

	for (int x = 0; x < rw; x++)
	{
		const size_t i = (size_t)y * rw + x;
		const FilterGeo &g = geo[i];
		const FilterLight &centre = in[i];
		if (g.depth < 0.0f)
		{
			out[i] = centre;
			continue;
		}

		const __m128 cr = _mm_loadu_ps(centre.r), cg = _mm_loadu_ps(centre.g), cb = _mm_loadu_ps(centre.b);
		const __m128 cvar = _mm_loadu_ps(centre.var);
		const __m128 lum = _mm_add_ps(_mm_add_ps(_mm_mul_ps(cr, lum_r), _mm_mul_ps(cg, lum_g)), _mm_mul_ps(cb, lum_b));
		const __m128 sigma = _mm_sqrt_ps(cvar);

		// settled already: nothing to gain from blurring it
		if (_mm_movemask_ps(_mm_cmple_ps(sigma, _mm_add_ps(_mm_mul_ps(lum, _mm_set1_ps(0.01f)), _mm_set1_ps(1e-4f)))) == 15)
		{
			out[i] = centre;
			continue;
		}

		// how big a brightness difference is still taken for noise, per channel
		const __m128 tolerance = _mm_setr_ps(4.0f, 1.0f + 3.0f * g.roughness, 0.75f, 6.0f);
		const __m128 inv_lum = _mm_div_ps(one, _mm_add_ps(_mm_mul_ps(tolerance, sigma), _mm_set1_ps(1e-3f)));
		const float inv_plane = 1.0f / (1.0f + g.depth * 0.004f);

		const float w0 = kernel[2] * kernel[2];
		__m128 wsum = _mm_set1_ps(w0);
		__m128 sr = _mm_mul_ps(cr, wsum), sg = _mm_mul_ps(cg, wsum), sb = _mm_mul_ps(cb, wsum);
		__m128 vsum = _mm_mul_ps(cvar, _mm_set1_ps(w0 * w0));

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
				const FilterGeo &h = geo[q];
				if (h.depth < 0.0f)
					continue;

				float wn = Dot(g.normal, h.normal);
				if (wn <= 0.0f)
					continue;
				wn *= wn; wn *= wn; wn *= wn;				// ^8
				const float wz = std::fabs(Dot(g.plane, h.pos - g.pos)) * inv_plane;
				if (wz >= 4.0f)
					continue;
				const float wr = std::fabs(h.roughness - g.roughness) * 8.0f;	// specular only

				const FilterLight &l = in[q];
				const __m128 qr = _mm_loadu_ps(l.r), qg = _mm_loadu_ps(l.g), qb = _mm_loadu_ps(l.b);
				const __m128 qlum = _mm_add_ps(_mm_add_ps(_mm_mul_ps(qr, lum_r), _mm_mul_ps(qg, lum_g)), _mm_mul_ps(qb, lum_b));
				const __m128 wl = _mm_mul_ps(_mm_andnot_ps(sign, _mm_sub_ps(qlum, lum)), inv_lum);

				// weight * exp(-(wz + wl + wr)), with the exponential as (1 - x/4)^4
				__m128 e = _mm_add_ps(_mm_add_ps(wl, _mm_set1_ps(wz)), _mm_setr_ps(0.0f, wr, 0.0f, 0.0f));
				e = _mm_max_ps(zero, _mm_sub_ps(one, _mm_mul_ps(e, _mm_set1_ps(0.25f))));
				e = _mm_mul_ps(e, e);
				e = _mm_mul_ps(e, e);
				const __m128 w = _mm_mul_ps(e, _mm_set1_ps(kernel[dx + 2] * kernel[dy + 2] * wn));

				sr = _mm_add_ps(sr, _mm_mul_ps(qr, w));
				sg = _mm_add_ps(sg, _mm_mul_ps(qg, w));
				sb = _mm_add_ps(sb, _mm_mul_ps(qb, w));
				vsum = _mm_add_ps(vsum, _mm_mul_ps(_mm_loadu_ps(l.var), _mm_mul_ps(w, w)));
				wsum = _mm_add_ps(wsum, w);
			}
		}

		const __m128 inv = _mm_div_ps(one, wsum);
		_mm_storeu_ps(out[i].r, _mm_mul_ps(sr, inv));
		_mm_storeu_ps(out[i].g, _mm_mul_ps(sg, inv));
		_mm_storeu_ps(out[i].b, _mm_mul_ps(sb, inv));
		_mm_storeu_ps(out[i].var, _mm_mul_ps(vsum, _mm_mul_ps(inv, inv)));
	}
}

// low discrepancy sequence: successive values fill [0, 1) evenly
float Halton(uint32_t index, uint32_t base)
{
	float f = 1.0f, r = 0.0f;
	while (index)
	{
		f /= base;
		r += f * (index % base);
		index /= base;
	}
	return r;
}

// The typical brightness of the picture: the geometric mean of its luminance,
// which a few very bright pixels do not drag about the way an average would
float TypicalLuminance(const CpuBackend *s)
{
	double sum = 0.0;
	int count = 0;
	for (int y = 1; y < s->rh; y += 3)
	{
		for (int x = 1; x < s->rw; x += 3)
		{
			sum += std::log(Luminance(s->hdr[(size_t)y * s->rw + x]) + 1e-4f);
			count++;
		}
	}
	return count ? (float)std::exp(sum / count) : 0.0f;
}

// Glow around what is brighter than the screen can show. Works on a half
// size copy: the bright part is taken out, blurred widely, and added back.
void Bloom(CpuBackend *s, float strength)
{
	const int rw = s->rw, rh = s->rh;
	const int bw = std::max(1, rw / 2), bh = std::max(1, rh / 2);
	const size_t count = (size_t)bw * bh;
	if (s->bloom_a.size() != count)
	{
		s->bloom_a.assign(count, Vec3());
		s->bloom_b.assign(count, Vec3());
	}
	std::vector<Vec3> &a = s->bloom_a, &b = s->bloom_b;

	s->pool.Run(bh, [&](int y)
	{
		for (int x = 0; x < bw; x++)
		{
			Vec3 sum;
			for (int k = 0; k < 4; k++)
			{
				const int qx = std::min(rw - 1, x * 2 + (k & 1)), qy = std::min(rh - 1, y * 2 + (k >> 1));
				const Vec3 c = s->hdr[(size_t)qy * rw + qx];
				const float lum = Luminance(c);
				if (lum > 1.0f)
					sum += c * ((lum - 1.0f) / lum);
			}
			a[(size_t)y * bw + x] = sum * 0.25f;
		}
	});

	// three box blurs come close to a gaussian
	const int radius = std::max(1, bh / 48);
	const float norm = 1.0f / (2 * radius + 1);
	for (int pass = 0; pass < 3; pass++)
	{
		s->pool.Run(bh, [&](int y)		// across, a into b
		{
			const Vec3 *row = &a[(size_t)y * bw];
			Vec3 run;
			for (int x = -radius; x <= radius; x++)
				run += row[std::min(bw - 1, std::max(0, x))];
			for (int x = 0; x < bw; x++)
			{
				b[(size_t)y * bw + x] = run * norm;
				run += row[std::min(bw - 1, x + radius + 1)] - row[std::max(0, x - radius)];
			}
		});
		s->pool.Run(bw, [&](int x)		// down, b into a
		{
			Vec3 run;
			for (int y = -radius; y <= radius; y++)
				run += b[(size_t)std::min(bh - 1, std::max(0, y)) * bw + x];
			for (int y = 0; y < bh; y++)
			{
				a[(size_t)y * bw + x] = run * norm;
				run += b[(size_t)std::min(bh - 1, y + radius + 1) * bw + x] - b[(size_t)std::max(0, y - radius) * bw + x];
			}
		});
	}

	s->pool.Run(rh, [&](int y)
	{
		const float fy = (y + 0.5f) * 0.5f - 0.5f;
		const int y0 = std::min(bh - 1, std::max(0, (int)std::floor(fy))), y1 = std::min(bh - 1, y0 + 1);
		const float ay = std::min(1.0f, std::max(0.0f, fy - y0));
		for (int x = 0; x < rw; x++)
		{
			const float fx = (x + 0.5f) * 0.5f - 0.5f;
			const int x0 = std::min(bw - 1, std::max(0, (int)std::floor(fx))), x1 = std::min(bw - 1, x0 + 1);
			const float ax = std::min(1.0f, std::max(0.0f, fx - x0));
			const Vec3 glow = (a[(size_t)y0 * bw + x0] * (1.0f - ax) + a[(size_t)y0 * bw + x1] * ax) * (1.0f - ay)
				+ (a[(size_t)y1 * bw + x0] * (1.0f - ax) + a[(size_t)y1 * bw + x1] * ax) * ay;
			s->hdr[(size_t)y * rw + x] += glow * strength;
		}
	});
}

// what the resolve needs to know about the view it is filling
struct Target
{
	int		x, y, width, height;	// the view, in window pixels
	int		x0, y0, x1, y1;			// the part of it inside the window
};

// Without anti-aliasing: stretch the picture to the view, bilinearly
void UpscaleRow(CpuBackend *s, const Target &t, const Grade &grade, int oy)
{
	const int rw = s->rw, rh = s->rh;
	const int wy = t.y + oy;
	if (wy < t.y0 || wy >= t.y1)
		return;

	const float fy = (oy + 0.5f) * rh / t.height - 0.5f;
	const int sy0 = std::min(rh - 1, std::max(0, (int)std::floor(fy))), sy1 = std::min(rh - 1, sy0 + 1);
	const float ay = std::min(1.0f, std::max(0.0f, fy - sy0));
	uint32_t *out = &s->scene[(size_t)wy * s->width];

	for (int ox = 0; ox < t.width; ox++)
	{
		const int wx = t.x + ox;
		if (wx < t.x0 || wx >= t.x1)
			continue;
		const float fx = (ox + 0.5f) * rw / t.width - 0.5f;
		const int sx0 = std::min(rw - 1, std::max(0, (int)std::floor(fx))), sx1 = std::min(rw - 1, sx0 + 1);
		const float ax = std::min(1.0f, std::max(0.0f, fx - sx0));
		const Vec3 c = (s->hdr[(size_t)sy0 * rw + sx0] * (1.0f - ax) + s->hdr[(size_t)sy0 * rw + sx1] * ax) * (1.0f - ay)
			+ (s->hdr[(size_t)sy1 * rw + sx0] * (1.0f - ax) + s->hdr[(size_t)sy1 * rw + sx1] * ax) * ay;
		out[wx] = ToneMap(c, grade);
	}
}

// Temporal anti-aliasing and upscaling in one.
//
// The picture is traced at a lower resolution than the view, and each frame
// through a different point of every traced pixel. A view pixel takes the
// traced sample nearest to it and blends it into a running average kept at
// the view's own resolution, giving it more say the nearer it landed. Over
// a few frames every view pixel has had samples close to it, and the
// average holds detail the traced resolution alone does not. No single
// frame counts for much, so the offsets do not show as shaking.
//
// The average is found by following the point back to where it was on
// screen. It is kept across different surfaces, because along an edge that
// blend is exactly the smooth edge wanted; what keeps stale history out is
// that it may not stray outside what the traced pixels around it show now.
void ResolveRow(CpuBackend *s, const Camera &cam, const Camera &prev_cam, bool have_history, float max_count,
	const Target &t, const Grade &grade, int oy)
{
	const int rw = s->rw, rh = s->rh, vw = t.width, vh = t.height;
	const float to_low_x = (float)rw / vw, to_low_y = (float)rh / vh;
	const float to_view_x = (float)vw / rw, to_view_y = (float)vh / rh;
	const Vec3 *prev = s->steady_prev.data();
	const float *prev_count = s->steady_count_prev.data();
	const Pixels &was = s->prev;
	const int wy = t.y + oy;
	uint32_t *out_row = (wy >= t.y0 && wy < t.y1) ? &s->scene[(size_t)wy * s->width] : nullptr;

	const float ly = (oy + 0.5f) * to_low_y - 0.5f;
	const int sy = std::min(rh - 1, std::max(0, (int)std::floor(ly - s->jitter_y + 0.5f)));
	const float dy = ly - (sy + s->jitter_y);

	for (int ox = 0; ox < vw; ox++)
	{
		// the traced sample nearest this view pixel, and how near
		const float lx = (ox + 0.5f) * to_low_x - 0.5f;
		const int sx = std::min(rw - 1, std::max(0, (int)std::floor(lx - s->jitter_x + 0.5f)));
		const float dx = lx - (sx + s->jitter_x);
		const float nearness = std::max(0.02f, std::exp(-(dx * dx + dy * dy) * 10.0f));

		const size_t i = (size_t)sy * rw + sx;
		const size_t o = (size_t)oy * vw + ox;
		const Vec3 c = s->hdr[i];
		const bool sky = s->cur.depth[i] < 0.0f;
		Vec3 out = c;
		float count = nearness;

		if (have_history)
		{
			// the sky has no position: use a point far along its ray
			Vec3 p = s->cur.seen[i];
			if (sky)
				p = cam.origin + Normalize(cam.forward
					+ cam.right * ((2.0f * (sx + 0.5f) / rw - 1.0f) * cam.tx)
					+ cam.up * ((1.0f - 2.0f * (sy + 0.5f) / rh) * cam.ty)) * 100000.0f;

			// where the traced sample was last frame, less this frame's
			// offset within the pixel (which the sky point was not seen through)
			const Vec3 v = p - prev_cam.origin;
			const float z = Dot(v, prev_cam.forward);
			bool found = false;
			Vec3 h;
			float hcount = 0.0f;

			if (z > 0.01f)
			{
				const float jx = sky ? 0.0f : s->jitter_x, jy = sky ? 0.0f : s->jitter_y;
				const float fx = (Dot(v, prev_cam.right) / (z * prev_cam.tx) * 0.5f + 0.5f) * rw - 0.5f - jx;
				const float fy = (0.5f - Dot(v, prev_cam.up) / (z * prev_cam.ty) * 0.5f) * rh - 0.5f - jy;

				// this view pixel moved as its traced sample did
				float hx = ox + (fx - sx) * to_view_x, hy = oy + (fy - sy) * to_view_y;

				// Something that moves with the eye, like the weapon in hand,
				// was not where the world says: it was on this same pixel. Take
				// that when the place it should have come from held something
				// else and this pixel held much the same thing.
				if (!sky && (std::fabs(fx - sx) > 0.01f || std::fabs(fy - sy) > 0.01f))
				{
					const int qx = (int)std::floor(fx + 0.5f), qy = (int)std::floor(fy + 0.5f);
					const bool inside = qx >= 0 && qy >= 0 && qx < rw && qy < rh;
					const bool same_there = inside && was.depth[(size_t)qy * rw + qx] >= 0.0f
						&& SameSurface(s->cur, was, i, (size_t)qy * rw + qx);
					const bool same_here = was.depth[i] > 0.0f
						&& std::fabs(was.depth[i] - s->cur.depth[i]) < 0.1f * s->cur.depth[i]
						&& Dot(s->cur.plane[i], was.plane[i]) > 0.8f;
					if (!same_there && same_here)
					{
						hx = (float)ox;
						hy = (float)oy;
					}
				}

				if (hx >= 0.0f && hy >= 0.0f && hx <= vw - 1.0f && hy <= vh - 1.0f)
				{
					const int ix = std::min((int)hx, vw - 2 < 0 ? 0 : vw - 2), iy = std::min((int)hy, vh - 2 < 0 ? 0 : vh - 2);
					const int ix1 = std::min(ix + 1, vw - 1), iy1 = std::min(iy + 1, vh - 1);
					const float ax = hx - ix, ay = hy - iy;
					const size_t q00 = (size_t)iy * vw + ix, q01 = (size_t)iy * vw + ix1;
					const size_t q10 = (size_t)iy1 * vw + ix, q11 = (size_t)iy1 * vw + ix1;

					h = (prev[q00] * (1.0f - ax) + prev[q01] * ax) * (1.0f - ay)
						+ (prev[q10] * (1.0f - ax) + prev[q11] * ax) * ay;
					hcount = (prev_count[q00] * (1.0f - ax) + prev_count[q01] * ax) * (1.0f - ay)
						+ (prev_count[q10] * (1.0f - ax) + prev_count[q11] * ax) * ay;
					found = true;
				}
			}

			if (found && hcount > 0.0f)
			{
				// what the traced pixels around here show now bounds what history may say
				const Vec3 clamped = Max(s->near_lo[i], Min(s->near_hi[i], h));

				// history that had to be pulled back a long way was about
				// something else; let the picture catch up quickly
				const float pulled = Luminance(Max(clamped - h, h - clamped));
				if (pulled > 0.1f * (Luminance(clamped) + 0.02f))
					hcount = std::min(hcount, 4.0f);

				count = std::min(hcount + nearness, max_count);
				out = clamped + (c - clamped) * std::min(1.0f, nearness / count);
			}
		}

		s->steady[o] = out;
		s->steady_count[o] = count;
		if (out_row && t.x + ox >= t.x0 && t.x + ox < t.x1)
			out_row[t.x + ox] = ToneMap(out, grade);
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
		s->filter_geo.assign(count, FilterGeo());
		s->filter_a.assign(count, FilterLight());
		s->filter_b.assign(count, FilterLight());
		s->hdr.assign(count, Vec3());
		s->near_lo.assign(count, Vec3());
		s->near_hi.assign(count, Vec3());
		s->antialiased = false;
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
	sc.light_styles = view->light_styles;
	sc.num_light_styles = view->light_styles ? view->num_light_styles : 0;
	sc.anim_frame = view->anim_frame < 0 ? 0 : view->anim_frame;
	sc.time = view->time;
	sc.handles = &s->textures;
	{
		const Vec3 axis(view->sky_axis);
		const float angle = view->sky_angle * (kPi / 180.0f);
		if (Dot(axis, axis) > 0.5f && angle != 0.0f)
		{
			sc.sky_axis = Normalize(axis);
			sc.sky_sin = std::sin(angle);
			sc.sky_cos = std::cos(angle);
		}
	}
	sc.light_samples = view->light_samples > 0 ? std::min(view->light_samples, 64) : 8;
	sc.max_sample = view->firefly_clamp > 0.0f ? view->firefly_clamp : 40.0f;
	sc.wave_strength = std::max(0.0f, view->wave_strength);
	sc.filter_textures = view->texture_filter != 0;
	sc.reflections = view->reflections;
	sc.reflection_bounces = std::max(1, view->reflection_bounces > 0 ? view->reflection_bounces : bounces);
	sc.reflection_rate = std::max(0.0f, view->reflection_rate);
	sc.adaptive = std::min(std::max(view->adaptive, 1), 16);
	sc.refraction = view->refraction != 0;
	sc.fog_density = view->fog ? std::min(std::max(view->fog_density, 0.0f), 0.05f) : 0.0f;
	if (bounces < 1)
		sc.reflections = 0;		// no bounces at all means none off mirrors either
	s->moving_history = (float)std::min(std::max(view->history, 1), 512);
	const int passes = std::min(std::max(view->denoise, 0), kMaxFilterPasses);
	s->pool.SetLimit(view->threads);
	s->frame_index++;

	// everything outside the camera that changes the picture
	uint32_t hash = s->frame.hash;
	hash = HashBytes(sc.light_styles, (size_t)sc.num_light_styles * sizeof(float), hash);
	hash = HashBytes(&sc.anim_frame, sizeof(sc.anim_frame), hash);
	hash = HashBytes(&bounces, sizeof(bounces), hash);
	{
		const float settings[] = {(float)samples, (float)sc.light_samples, sc.max_sample, sc.wave_strength,
			(float)sc.filter_textures, (float)sc.reflections, (float)sc.reflection_bounces, sc.reflection_rate,
			(float)sc.refraction, view->exposure, sc.fog_density, (float)sc.adaptive};
		hash = HashBytes(settings, sizeof(settings), hash);
	}
	if (s->world->has_waves)
		hash = HashBytes(&sc.time, sizeof(sc.time), hash);
	hash = HashBytes(&sc.sky_sin, sizeof(sc.sky_sin), hash);

	// each frame looks through a slightly different point of every pixel, so
	// that over time edges are seen from all across it
	const bool antialias = view->antialias != 0;
	const float jx = antialias ? Halton(s->frame_index % 16 + 1, 2) - 0.5f : 0.0f;
	const float jy = antialias ? Halton(s->frame_index % 16 + 1, 3) - 0.5f : 0.0f;
	s->jitter_x = jx;
	s->jitter_y = jy;

	s->pool.Run(rh, [&](int y)
	{
		for (int x = 0; x < rw; x++)
			TracePixel(s, sc, cam, jx, jy, x, y, samples, bounces);
	});
	const auto traced = std::chrono::steady_clock::now();

	// with nothing changing the average may run forever and converge
	const bool still = s->have_history && cam == s->prev_camera && hash == s->prev_hash;
	const float max_history = still ? 65536.0f : s->moving_history;
	const Camera prev_cam = s->prev_camera;
	s->pool.Run(rh, [&](int y) { Accumulate(s, prev_cam, max_history, y); });
	const auto accumulated = std::chrono::steady_clock::now();

	// gather what the filter needs, then let it ping-pong between two buffers
	s->pool.Run(rh, [&](int y)
	{
		for (int x = 0; x < rw; x++)
		{
			const size_t i = (size_t)y * rw + x;
			FilterGeo &g = s->filter_geo[i];
			g.pos = s->cur.pos[i];
			g.depth = s->cur.depth[i];
			g.normal = s->cur.normal[i];
			g.roughness = s->cur.roughness[i];
			g.plane = s->cur.plane[i];
			FilterLight &l = s->filter_a[i];
			for (int c = 0; c < kChannels; c++)
			{
				l.r[c] = s->cur.light[c][i].x;
				l.g[c] = s->cur.light[c][i].y;
				l.b[c] = s->cur.light[c][i].z;
				l.var[c] = s->cur.variance[c][i];
			}
		}
	});

	const FilterLight *in = s->filter_a.data();
	for (int pass = 0; pass < passes; pass++)
	{
		FilterLight *out = (pass & 1) ? s->filter_a.data() : s->filter_b.data();
		const FilterGeo *geo = s->filter_geo.data();
		s->pool.Run(rh, [&](int y) { FilterRow(rw, rh, geo, in, out, 1 << pass, y); });
		in = out;
	}
	const auto filtered = std::chrono::steady_clock::now();

	// Exposure: the user's setting times, if wanted, whatever brings this
	// scene's typical brightness to a fixed level.
	const int debug = view->debug;
	const float exposure = debug ? 1.0f : view->exposure * (view->auto_exposure ? s->auto_exposure : 1.0f);
	s->pool.Run(rh, [&](int y)
	{
		for (int x = 0; x < rw; x++)
		{
			const size_t i = (size_t)y * rw + x;
			Vec3 c = s->cur.add[i];
			for (int ch = 0; ch < kChannels; ch++)
				c += s->cur.albedo[ch][i] * Vec3(in[i].r[ch], in[i].g[ch], in[i].b[ch]);

			// one part of the picture on its own, for finding where a fault lies
			switch (debug)
			{
			case 1: c = s->cur.albedo[kDiffuse][i]; break;
			case 2: c = Vec3(in[i].r[kDiffuse], in[i].g[kDiffuse], in[i].b[kDiffuse]); break;
			case 3: c = s->cur.albedo[kSpecular][i] * Vec3(in[i].r[kSpecular], in[i].g[kSpecular], in[i].b[kSpecular]); break;
			case 4: c = s->cur.albedo[kOver][i] * Vec3(in[i].r[kOver], in[i].g[kOver], in[i].b[kOver]); break;
			case 5: c = s->cur.add[i]; break;
			case 6: c = s->cur.normal[i] * 0.25f + Vec3(0.25f); break;
			case 7: c = Vec3(std::min(s->cur.length[i], 32.0f) / 64.0f); break;
			case 8: c = Vec3(std::min(s->cur.over_length[i], 32.0f) / 64.0f); break;
			case 9: c = Vec3(s->cur.bent[i] ? 0.5f : 0.05f); break;
			case 10: c = Vec3(s->cur.depth[i] * 0.002f); break;
			case 11: c = Vec3(in[i].r[kFog], in[i].g[kFog], in[i].b[kFog]); break;
			default: break;
			}
			s->hdr[i] = c * exposure;
		}
	});

	float typical = 0.0f;
	if (!debug)
	{
		// The eye adapts: measured on what was just made, used from the next
		// frame on, and followed over about a second so it does not pump.
		typical = TypicalLuminance(s) / exposure;
		if (typical > 0.0f)
		{
			const float want = std::min(16.0f, std::max(0.125f, kTypicalTarget / typical));
			const float dt = view->time - s->prev_time;
			if (!s->have_history || dt < 0.0f || dt > 1.0f)
				s->auto_exposure = want;
			else
				s->auto_exposure += (want - s->auto_exposure) * (1.0f - std::exp(-dt * 2.5f));
		}
		if (view->bloom > 0.0f)
			Bloom(s, view->bloom);
	}
	s->prev_time = view->time;

	Grade grade;
	grade.curve = view->tonemap;
	grade.saturation = std::max(0.0f, view->saturation);
	grade.contrast = view->contrast > 0.0f ? view->contrast : 1.0f;

	Target target;
	target.x = view->x;
	target.y = view->y;
	target.width = view->width;
	target.height = view->height;
	target.x0 = x0;
	target.y0 = y0;
	target.x1 = x1;
	target.y1 = y1;

	const int vw = view->width, vh = view->height;
	if (s->out_w != vw || s->out_h != vh)
	{
		s->out_w = vw;
		s->out_h = vh;
		const size_t out_count = (size_t)vw * vh;
		s->steady.assign(out_count, Vec3());
		s->steady_prev.assign(out_count, Vec3());
		s->steady_count.assign(out_count, 0.0f);
		s->steady_count_prev.assign(out_count, 0.0f);
		s->antialiased = false;
	}

	const bool resolve_history = s->have_history && antialias && s->antialiased && !debug;
	s->antialiased = antialias;
	if (antialias)
	{
		// the range of colours around each traced pixel, which bounds its history
		s->pool.Run(rh, [&](int y)
		{
			for (int x = 0; x < rw; x++)
			{
				Vec3 lo = s->hdr[(size_t)y * rw + x], hi = lo;
				for (int dy = -1; dy <= 1; dy++)
				{
					const int qy = y + dy;
					if (qy < 0 || qy >= rh)
						continue;
					for (int dx = -1; dx <= 1; dx++)
					{
						const int qx = x + dx;
						if (qx < 0 || qx >= rw)
							continue;
						const Vec3 q = s->hdr[(size_t)qy * rw + qx];
						lo = Min(lo, q);
						hi = Max(hi, q);
					}
				}
				s->near_lo[(size_t)y * rw + x] = lo;
				s->near_hi[(size_t)y * rw + x] = hi;
			}
		});

		const float max_count = cam == prev_cam ? 1024.0f : 16.0f;
		s->pool.Run(vh, [&](int oy) { ResolveRow(s, cam, prev_cam, resolve_history, max_count, target, grade, oy); });
	}
	else
		s->pool.Run(vh, [&](int oy) { UpscaleRow(s, target, grade, oy); });

	// this frame becomes the history the next one looks back at
	std::swap(s->cur, s->prev);
	std::swap(s->steady, s->steady_prev);
	std::swap(s->steady_count, s->steady_count_prev);
	s->prev_camera = cam;
	s->prev_hash = hash;
	s->have_history = true;

	const auto end = std::chrono::steady_clock::now();
	const auto ms = [](std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point c)
	{
		return std::chrono::duration<double, std::milli>(c - a).count();
	};
	snprintf(s->stats, sizeof(s->stats), "%dx%d %dspp %db: %.1f ms (build %.1f trace %.1f history %.1f filter %.1f out %.1f) %zu dyn tris typ %.4f exp %.2f%s",
		rw, rh, samples, bounces, ms(start, end), ms(start, built), ms(built, traced), ms(traced, accumulated),
		ms(accumulated, filtered), ms(filtered, end),
		s->frame.tris.size(), typical, exposure, still ? " still" : "");
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
	s->shown[0] = x0; s->shown[1] = y0; s->shown[2] = x1; s->shown[3] = y1;
	s->has_view = false;

	HDC dc = GetDC(s->hwnd);
	if (dc)
	{
		BitBlt(dc, 0, 0, s->width, s->height, s->memdc, 0, 0, SRCCOPY);
		ReleaseDC(s->hwnd, dc);
	}
}

int ReadPixels(pt_backend_t *b, uint32_t *pixels, int with_overlay)
{
	CpuBackend *s = Self(b);

	for (int y = 0; y < s->height; y++)
	{
		const uint32_t *in = with_overlay ? &s->dibbits[(size_t)y * s->width] : &s->scene[(size_t)y * s->width];
		uint32_t *out = &pixels[(size_t)y * s->width];
		const bool rowin = with_overlay || (y >= s->shown[1] && y < s->shown[3]);

		for (int x = 0; x < s->width; x++)
		{
			const uint32_t c = (rowin && (with_overlay || (x >= s->shown[0] && x < s->shown[2]))) ? in[x] : 0;
			out[x] = ((c >> 16) & 0xff) | (c & 0xff00) | ((c & 0xff) << 16) | 0xff000000u;
		}
	}
	return 1;
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
	s->base.texture_update = TextureUpdate;
	s->base.render_view = RenderView;
	s->base.present = Present;
	s->base.stats = Stats;
	s->base.read_pixels = ReadPixels;
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
