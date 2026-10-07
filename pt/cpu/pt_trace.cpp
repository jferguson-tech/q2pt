// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson

#include "pt_trace.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

namespace PT_NS {

namespace {

const float kGlobalLightChance = 0.2f;	// how often a light is picked map wide instead of nearby
const float kMinAlpha = 0.002f;			// GGX gets numerically touchy below this

void TexCoord(const Tri &t, float u, float v, float &s, float &tt)
{
	const float b0 = 1.0f - u - v;
	s = t.uv[0][0] * b0 + t.uv[1][0] * u + t.uv[2][0] * v;
	tt = t.uv[0][1] * b0 + t.uv[1][1] * u + t.uv[2][1] * v;
}

bool IsHole(const Tri &t, float u, float v)
{
	if (!(t.mat->flags & PT_MAT_ALPHA_TEST) || !t.mat->texture)
		return false;
	float s, tt;
	TexCoord(t, u, v, s, tt);
	return (t.mat->texture->Texel(s, tt) >> 24) < 128;
}

// true if nothing stops light between the surface and target
// How much of the light from target reaches the surface: 0 if something is
// in the way, otherwise 1 times whatever rippling liquid on the way does to
// it, which gathers the light in some places and thins it in others.
float Visible(const Scene &sc, const Surface &s, Vec3 target, Rng &rng)
{
	Ray shadow;
	shadow.o = s.p + s.ng * kRayOffset;
	shadow.d = target - shadow.o;
	shadow.tmin = 0.0f;
	shadow.tmax = 0.999f;

	float through = 1.0f;
	const auto blocks = [&](const Tri &t, float u, float v)
	{
		if (IsHole(t, u, v) || BackOfGlass(t, shadow.d))
			return false;
		if (t.mat->alpha >= 1.0f || rng.Float() < t.mat->alpha)
			return true;
		if (t.mat->caustic_map)
			through *= sc.Caustic(*t.mat, t.p0 + t.e1 * u + t.e2 * v);
		return false;
	};

#ifdef PT_AVX2_KERNELS
	// The trees with eight children answer sooner, and say the same or that
	// they are unsure, as when a surface that light gets through by chance is
	// in the way: see Bvh::AnyHit8. The trees below are then asked, as they
	// always were.
	const auto kind = [&](const Tri &t, float u, float v)
	{
		if (IsHole(t, u, v) || BackOfGlass(t, shadow.d))
			return 0;
		return t.mat->alpha >= 1.0f ? 1 : 2;
	};
	const int world = sc.world->bvh.AnyHit8(shadow, [&](uint32_t i, float u, float v) { return kind(sc.world->tris[i], u, v); });
	if (world == 1)
		return 0.0f;
	if (world == 0)
	{
		const int frame = sc.frame->bvh.AnyHit8(shadow, [&](uint32_t i, float u, float v) { return kind(sc.frame->tris[i], u, v); });
		if (frame == 1)
			return 0.0f;
		if (frame == 0)
			return through;
		// nothing of the world's was in the way, so only the frame is left to ask
		if (sc.frame->bvh.AnyHit(shadow, [&](uint32_t i, float u, float v) { return blocks(sc.frame->tris[i], u, v); }))
			return 0.0f;
		return through;
	}
#endif

	if (sc.world->bvh.AnyHit(shadow, [&](uint32_t i, float u, float v) { return blocks(sc.world->tris[i], u, v); }))
		return 0.0f;
	if (sc.frame->bvh.AnyHit(shadow, [&](uint32_t i, float u, float v) { return blocks(sc.frame->tris[i], u, v); }))
		return 0.0f;
	return through;
}

// ---- GGX microfacet reflection, height correlated Smith shadowing ----

Vec3 Fresnel(Vec3 f0, float voh)
{
	const float m = 1.0f - (voh < 0.0f ? 0.0f : voh);
	const float m5 = m * m * m * m * m;
	return f0 + (Vec3(1, 1, 1) - f0) * m5;
}

float SmithLambda(float alpha, float nox)	// the square root term shared by G1 and G2
{
	return std::sqrt(alpha * alpha + (1.0f - alpha * alpha) * nox * nox);
}

// the specular lobe's value times the cosine at the light: f * (n.wi)
Vec3 SpecularTimesCos(const Surface &s, Vec3 wi)
{
	const float nol = Dot(s.n, wi), nov = Dot(s.n, s.wo);
	if (nol <= 0.0f || nov <= 0.0f)
		return Vec3();
	const Vec3 h = Normalize(s.wo + wi);
	const float noh = Dot(s.n, h), voh = Dot(s.wo, h);
	const float a2 = s.alpha * s.alpha;
	const float d = noh * noh * (a2 - 1.0f) + 1.0f;
	const float ndf = a2 / (kPi * d * d);
	const float vis = 0.5f / (nol * SmithLambda(s.alpha, nov) + nov * SmithLambda(s.alpha, nol));
	return Fresnel(s.f0, voh) * (ndf * vis * nol);
}

// Light arriving from direction wi with radiance-times-solid-angle e
// (irradiance on a surface facing it), through each lobe
Lit Reflect(const Surface &s, Vec3 wi, Vec3 e)
{
	Lit out;
	if (s.medium)
	{
		out.diffuse = e;		// air has no facing
		return out;
	}
	const float nol = Dot(s.n, wi);
	if (nol <= 0.0f)
		return out;
	out.diffuse = e * nol;
	if (s.light_sampled_spec)
		out.specular = e * SpecularTimesCos(s, wi);
	return out;
}

float Importance(const Surface &s, const Lit &l)
{
	return Luminance(s.kd * l.diffuse) * kInvPi + Luminance(l.specular);
}

// a point light's contribution, ignoring occlusion
Lit PointLight(const Surface &s, const Light &l, float scale)
{
	const Vec3 d = l.origin - s.p;
	const float dist2 = Dot(d, d);
	if (dist2 <= 1e-6f || scale <= 0.0f)
		return Lit();
	return Reflect(s, d / std::sqrt(dist2), l.emission * (scale / dist2));
}

} // namespace

bool BackOfGlass(const Tri &tri, Vec3 dir)
{
	// sprites, beams and particles are flat things meant to be seen from both sides
	return tri.mat->alpha < 1.0f && !(tri.mat->flags & (PT_MAT_BLACK | PT_MAT_EMIT_TEXTURE))
		&& Dot(tri.n, dir) > 0.0f;
}

bool Finite(float f)
{
	uint32_t bits;
	memcpy(&bits, &f, sizeof(bits));
	return (bits & 0x7f800000u) != 0x7f800000u;
}

Vec3 ClampSample(Vec3 c, float max_luminance)
{
	const float lum = Luminance(c);
	if (!Finite(lum))
		return Vec3();
	return lum > max_luminance ? c * (max_luminance / lum) : c;
}

bool Closest(const Scene &sc, Ray &ray, Rng &rng, bool camera, bool cross, Hit &hit, const Tri *&tri, HeldRays held)
{
	for (int skips = 0; ; skips++)
	{
		const auto in_world = [&](uint32_t t, float, float) { return !BackOfGlass(sc.world->tris[t], ray.d); };
		const auto in_frame = [&](uint32_t t, float, float)
		{
			const Tri &tri = sc.frame->tris[t];
			if (held != kHeldToo && ((tri.mat->flags & PT_MAT_HELD) != 0) != (held == kHeldOnly))
				return false;
			return !BackOfGlass(tri, ray.d);
		};
		bool found = false;
#ifdef PT_AVX2_KERNELS
		bool unsure = false;
#endif
		if (held != kHeldOnly)
		{
#ifdef PT_AVX2_KERNELS
			// The trees with eight children find the same triangle sooner, or
			// say that they cannot be sure of it: see Bvh::Intersect8. The
			// trees below are then asked, as they always were.
			found = sc.world->bvh.Intersect8(ray, hit, in_world, unsure);
			if (unsure)
				found = sc.world->bvh.IntersectIf(ray, hit, in_world);
#else
			found = sc.world->bvh.IntersectIf(ray, hit, in_world);
#endif
		}
		Ray r = ray;
		if (found)
			r.tmax = hit.t;
		Hit h;
		bool found_frame;
		if (held != kHeldToo)
		{
			// The tree with eight children is not asked about plain
			// triangles, and what the eye carries is not marked out to it
			// (that would cost every ray while playing, when it is not
			// kept apart): the frame's tree is small, so ask it about all
			// of them.
			found_frame = sc.frame->bvh.IntersectIf(r, h, in_frame);
		}
		else
		{
#ifdef PT_AVX2_KERNELS
			found_frame = sc.frame->bvh.Intersect8(r, h, in_frame, unsure);
			if (unsure)
				found_frame = sc.frame->bvh.IntersectIf(r, h, in_frame);
#else
			found_frame = sc.frame->bvh.IntersectIf(r, h, in_frame);
#endif
		}
		if (found_frame)
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

// Epic's fit to the specular lobe's total reflectance (Karis, "Physically
// Based Shading on Mobile")
Vec3 Surface::SpecularAlbedo() const
{
	const float nov = std::max(0.0f, Dot(n, wo));
	const float r0 = -roughness + 1.0f;
	const float r1 = -0.0275f * roughness + 0.0425f;
	const float r2 = -0.572f * roughness + 1.04f;
	const float r3 = 0.022f * roughness - 0.04f;
	const float a004 = std::min(r0 * r0, std::exp2(-9.28f * nov)) * r0 + r1;
	const float scale = -1.04f * a004 + r2, bias = 1.04f * a004 + r3;
	return f0 * scale + Vec3(bias);
}

bool ClayCovers(const Material &mat)
{
	return mat.alpha >= 1.0f || (mat.flags & PT_MAT_WAVES);
}

// A view mode's say over what the surface is made of (pt_view_t's view_mode).
// What the surface emits is worked out from its colour, which is left alone.
static void ViewMode(int mode, const Material &mat, Surface &s)
{
	if (mode == PT_VIEW_CLAY && ClayCovers(mat))
	{
		s.kd = Vec3(0.5f);
		s.f0 = Vec3(0.04f);
		s.roughness = 1.0f;
	}
	else if (mode == PT_VIEW_MIRROR)
		s.roughness = 0.0f;
}

void MakeSurface(const Scene &sc, const Tri &tri, const Hit &hit, const Ray &ray, Surface &s, bool smooth)
{
	const Material &mat = tri.mat->At(sc.anim_frame);
	s.tri = &tri;
	s.mat = &mat;
	s.medium = false;
	s.p = ray.o + ray.d * hit.t;
	s.wo = -ray.d;
	s.front = Dot(tri.n, ray.d) < 0.0f;
	s.ng = s.front ? tri.n : -tri.n;

	float u, v;
	TexCoord(tri, hit.u, hit.v, u, v);
	if (mat.flags & PT_MAT_WARP)
	{
		// each coordinate sways with the other, an eighth of a repeat either way
		const float u0 = u;
		u += 0.125f * std::sin(v * 8.0f + sc.time);
		v += 0.125f * std::sin(u0 * 8.0f + sc.time);
	}
	u += mat.scroll_u * sc.time;
	v += mat.scroll_v * sc.time;

	// a simulated surface: the wave picture holds its slopes and height
	const bool simulated = mat.wave_map && sc.wave_strength > 0.0f && sc.Map(mat.wave_map);
	float wave_x = 0.0f, wave_y = 0.0f, wave_height = 0.0f;
	if (simulated)
	{
		float wu, wv;
		Scene::WaveCoord(mat, s.p, wu, wv);
		uint32_t texel[4];
		float w[4];
		sc.Map(mat.wave_map)->Corners(wu, wv, texel, w);
		for (int k = 0; k < 4; k++)
		{
			wave_x += (float)(texel[k] & 0xff) * w[k];
			wave_y += (float)((texel[k] >> 8) & 0xff) * w[k];
			wave_height += (float)((texel[k] >> 16) & 0xff) * w[k];
		}
		wave_x = (wave_x * (1.0f / 255.0f) - 0.5f) * sc.wave_strength;
		wave_y = (wave_y * (1.0f / 255.0f) - 0.5f) * sc.wave_strength;
		wave_height = (wave_height * (1.0f / 255.0f) - 0.5f) * 8.0f * sc.wave_strength;
		// what the texture stands for lies below the surface, so a tilted
		// surface shows it shifted, as through a lens
		u += wave_x * 0.5f;
		v += wave_y * 0.5f;
	}

	s.colour = !mat.texture ? Vec3(1, 1, 1) : (smooth ? mat.texture->Smooth(u, v) : Decode(mat.texture->Texel(u, v)));
	if (mat.emission_map)
		s.glow = smooth ? mat.emission_map->Smooth(u, v) : Decode(mat.emission_map->Texel(u, v));
	s.roughness = mat.roughness;

	Vec3 n = s.ng;
	if (tri.smooth)
	{
		n = Normalize(tri.vn[0] * (1.0f - hit.u - hit.v) + tri.vn[1] * hit.u + tri.vn[2] * hit.v);
		if (!s.front)
			n = -n;
	}
	if (mat.normal_texture)
	{
		// x, y, z and roughness, each 0-255
		float c[4];
		if (smooth)
		{
			uint32_t t[4];
			float w[4];
			mat.normal_texture->Corners(u, v, t, w);
			for (int k = 0; k < 4; k++)
				c[k] = (float)((t[0] >> (k * 8)) & 0xff) * w[0] + (float)((t[1] >> (k * 8)) & 0xff) * w[1]
					+ (float)((t[2] >> (k * 8)) & 0xff) * w[2] + (float)((t[3] >> (k * 8)) & 0xff) * w[3];
		}
		else
		{
			const uint32_t t = mat.normal_texture->Texel(u, v);
			for (int k = 0; k < 4; k++)
				c[k] = (float)((t >> (k * 8)) & 0xff);
		}
		const float tx = c[0] * (2.0f / 255.0f) - 1.0f;
		const float ty = c[1] * (2.0f / 255.0f) - 1.0f;
		const float tz = c[2] * (2.0f / 255.0f) - 1.0f;
		n = Normalize(tri.tu * tx + tri.tv * ty + n * tz);
		s.roughness = c[3] * (1.0f / 255.0f);
	}
	if (simulated)
	{
		// the surface is level, facing up or (seen from below) down
		n = Normalize(Vec3(-wave_x, -wave_y, 1.0f)) * (n.z < 0.0f ? -1.0f : 1.0f);
		// crests gather the light in the liquid and troughs spread it
		s.colour *= std::min(std::max(1.0f + wave_height * 0.35f, 0.6f), 1.8f);
	}
	else if ((mat.flags & PT_MAT_WAVES) && sc.wave_strength > 0.0f)
	{
		// a few crossing ripples, enough to break up a reflection
		const float t = sc.time;
		const float a = std::sin(s.p.x * 0.071f + t * 1.7f) + std::sin(s.p.y * 0.053f - t * 1.3f)
			+ 0.5f * std::sin((s.p.x + s.p.y) * 0.19f + t * 2.9f);
		const float b = std::sin(s.p.y * 0.067f + t * 1.5f) + std::sin(s.p.x * 0.047f - t * 1.9f)
			+ 0.5f * std::sin((s.p.x - s.p.y) * 0.17f + t * 2.3f);
		Vec3 t1, t2;
		Basis(n, t1, t2);
		n = Normalize(n + t1 * (a * 0.02f * sc.wave_strength) + t2 * (b * 0.02f * sc.wave_strength));
	}
	// a normal tilted away from the viewer is no use for shading
	if (Dot(n, s.wo) < 0.02f || Dot(n, s.ng) <= 0.0f)
		n = s.ng;
	s.n = n;

	if (mat.flags & PT_MAT_BLACK)
	{
		s.kd = Vec3();
		s.f0 = Vec3();
	}
	else
	{
		s.kd = s.colour * (1.0f - mat.metallic);
		s.f0 = Vec3(0.04f) * (1.0f - mat.metallic) + s.colour * mat.metallic;
		if (sc.view_mode)
			ViewMode(sc.view_mode, mat, s);
	}
	s.alpha = std::max(s.roughness * s.roughness, kMinAlpha);
	s.light_sampled_spec = s.roughness >= kLightSampledRoughness;
}

Vec3 Emitted(const Surface &s, bool seen)
{
	const Material &m = *s.mat;
	if (m.emission_map)
		return m.emission * s.glow;
	if (m.flags & PT_MAT_EMIT_BRIGHT)
	{
		// the lit parts of a screen or a button: bright texels glow, dark ones do not
		const float level = (MaxComponent(s.colour) - 0.12f) * (1.0f / 0.3f);
		const float t = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
		return m.emission * s.colour * (t * t * (3.0f - 2.0f * t));
	}
	return (seen && m.emission_seen > 0.0f) ? s.colour * m.emission_seen : m.emission_per_texel * s.colour;
}

// Light thrown back up by a simulated liquid surface: the dancing patches on
// walls and ceilings near water. The surface is treated as a mirror for the
// light just sampled, so its image lies as far below the surface as the
// light is above, and the waves' gathering of light shapes what comes back.
// y is the point on the light, e what it would send straight here.
static void WaterBounce(const Scene &sc, const Surface &s, Vec3 y, Vec3 e, Lit &out, Rng &rng)
{
	if (s.medium)
		return;
	for (const World::Water &b : sc.world->waters)
	{
		if (s.p.z <= b.z + 1.0f || y.z <= b.z + 1.0f || s.p.z - b.z > 512.0f)
			continue;
		const Vec3 image(y.x, y.y, 2.0f * b.z - y.z);
		const Vec3 d = image - s.p;
		const Vec3 q = s.p + d * ((b.z - s.p.z) / d.z);		// where the path meets the surface
		if (q.x < b.min_x || q.x > b.max_x || q.y < b.min_y || q.y > b.max_y)
			continue;

		const float len2 = Dot(d, d);
		const Vec3 wi = d * (1.0f / std::sqrt(len2));
		const float m = 1.0f + wi.z;	// 1 - cosine of the angle at the water
		const float fresnel = 0.02f + 0.98f * m * m * m * m * m;
		const float gain = fresnel * sc.Caustic(*b.mat, q);
		if (gain <= 0.001f)
			continue;

		// e was for the straight path; this one is as long as the way to the image
		const Vec3 straight = y - s.p;
		const Lit add = Reflect(s, wi, e * (gain * Dot(straight, straight) / len2));
		if (Importance(s, add) <= 0.0f)
			continue;

		// both legs must be clear
		const Vec3 above = q + Vec3(0.0f, 0.0f, 0.1f);
		if (Visible(sc, s, above, rng) <= 0.0f)
			continue;
		Surface at{};
		at.medium = true;
		at.p = above;
		if (Visible(sc, at, y, rng) <= 0.0f)
			continue;

		out.diffuse += add.diffuse;
		out.specular += add.specular;
		return;		// one body will do
	}
}

#ifdef PT_AVX2_KERNELS

// Where in the tables a light is looked for, DirectLights finds the place
// with these in the AVX2 build. They come to the places std::upper_bound and
// a search along the list come to, without the branches those take.

// how many of the count sorted values come before the first above x
static size_t FirstAbove(const float *sorted, size_t count, float x)
{
	size_t at = 0;
	for (size_t n = count; n > 1; )
	{
		const size_t half = n / 2;
		at = sorted[at + half - 1] <= x ? at + half : at;
		n -= half;
	}
	return at + (count && sorted[at] <= x ? 1 : 0);
}

// the same of a cell's table, which has room for LightGrid::kPerCell, 24,
// values whatever count is: all are looked at at once
static_assert(LightGrid::kPerCell == 24, "the searches of a cell's tables take three lots of eight");

static int FirstAboveInCell(const float *sorted, int count, float x)
{
	const __m256 xs = _mm256_set1_ps(x);
	const unsigned below =
		(unsigned)_mm256_movemask_ps(_mm256_cmp_ps(_mm256_loadu_ps(sorted), xs, _CMP_LE_OQ))
		| (unsigned)_mm256_movemask_ps(_mm256_cmp_ps(_mm256_loadu_ps(sorted + 8), xs, _CMP_LE_OQ)) << 8
		| (unsigned)_mm256_movemask_ps(_mm256_cmp_ps(_mm256_loadu_ps(sorted + 16), xs, _CMP_LE_OQ)) << 16;
	// sorted, so those not above x are the first so many
#ifdef _MSC_VER
	return (int)__popcnt(below & ((1u << count) - 1));
#else
	return __builtin_popcount(below & ((1u << count) - 1));
#endif
}

// the last of the first count places of a cell's list of lights that holds
// this one, or -1
static int LastPlaceOf(const uint32_t *list, int count, uint32_t light)
{
	const __m256i want = _mm256_set1_epi32((int)light);
	unsigned found =
		(unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(_mm256_loadu_si256((const __m256i *)list), want)))
		| (unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(_mm256_loadu_si256((const __m256i *)(list + 8)), want))) << 8
		| (unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(_mm256_loadu_si256((const __m256i *)(list + 16)), want))) << 16;
	found &= (1u << count) - 1;
	if (!found)
		return -1;
#ifdef _MSC_VER
	return 31 - (int)_lzcnt_u32(found);
#else
	return 31 - __builtin_clz(found);
#endif
}

#endif	// PT_AVX2_KERNELS

static Lit DirectLights(const Scene &sc, const Surface &s, Rng &rng, bool first_hit)
{

	const int candidates = std::max(1, first_hit ? sc.light_samples : sc.light_samples / 2);
	const World &w = *sc.world;
	Lit none;
	if (w.lights.empty())
		return none;

	// Candidates are weighed by the light they would put on the surface, which
	// is cheap; how the surface reflects it is worked out for the winner only.
	float wsum = 0.0f;
	Vec3 chosen_y, chosen_wi, chosen_e;
	float chosen_phat = 0.0f;

	const LightGrid &g = w.grid;
	const int k = LightGrid::kPerCell;
	const size_t cell = g.count.empty() ? 0 : g.Cell(s.p);
	const int in_cell = g.count.empty() ? 0 : g.count[cell];
	const float *cell_cdf = in_cell ? &g.cdf[cell * k] : nullptr;
	const float use_global = in_cell ? kGlobalLightChance : 1.0f;

	for (int i = 0; i < candidates; i++)
	{
		// mostly from the list for this cell, sometimes from the whole map so nothing is missed
		size_t li;
		float cell_pdf = 0.0f;
		if (rng.Float() < use_global)
		{
#ifdef PT_AVX2_KERNELS
			li = std::min(FirstAbove(w.light_cdf.data(), w.light_cdf.size(), rng.Float()), w.lights.size() - 1);
			const int j = in_cell ? LastPlaceOf(&g.light[cell * k], in_cell, (uint32_t)li) : -1;
			if (j >= 0)
				cell_pdf = g.pdf[cell * k + j];
#else
			li = std::min((size_t)(std::upper_bound(w.light_cdf.begin(), w.light_cdf.end(), rng.Float())
				- w.light_cdf.begin()), w.lights.size() - 1);
			for (int j = 0; j < in_cell; j++)
				if (g.light[cell * k + j] == li)
					cell_pdf = g.pdf[cell * k + j];
#endif
		}
		else
		{
#ifdef PT_AVX2_KERNELS
			const int j = std::min(FirstAboveInCell(cell_cdf, in_cell, rng.Float()), in_cell - 1);
#else
			const int j = std::min((int)(std::upper_bound(cell_cdf, cell_cdf + in_cell, rng.Float()) - cell_cdf), in_cell - 1);
#endif
			li = g.light[cell * k + j];
			cell_pdf = g.pdf[cell * k + j];
		}
		const Light &l = w.lights[li];
		float pdf = use_global * l.pdf + (1.0f - use_global) * cell_pdf;

		Vec3 y, e;
		if (l.tri != ~0u)
		{
			const Tri &t = w.tris[l.tri];
			float a = rng.Float(), b = rng.Float();
			if (a + b > 1.0f) { a = 1.0f - a; b = 1.0f - b; }
			y = t.p0 + t.e1 * a + t.e2 * b;
			e = l.emission;
			pdf /= t.area;
		}
		else
		{
			y = l.origin;
			const float scale = sc.StyleScale(l.style);
			if (scale <= 0.0f)
				continue;
			e = l.emission * scale;
		}

		const Vec3 d = y - s.p;
		const float dist2 = Dot(d, d);
		if (dist2 <= 1e-6f)
			continue;
		const Vec3 wi = d * (1.0f / std::sqrt(dist2));
		if (l.cone_cos > 0.0f && l.tri == ~0u && -Dot(wi, l.dir) < l.cone_cos)
			continue;		// outside the spotlight's cone
		const float nol = s.medium ? 1.0f : Dot(s.n, wi);
		if (nol <= 0.0f)
			continue;
		float geom = 1.0f / dist2;
		if (l.tri != ~0u)
		{
			const float cosy = -Dot(w.tris[l.tri].n, wi);
			if (cosy <= 0.0f)
				continue;
			geom *= cosy;
		}
		e *= geom;

		const float phat = Luminance(e) * nol;
		if (phat <= 0.0f)
			continue;
		const float weight = phat / pdf;
		wsum += weight;
		if (rng.Float() * wsum < weight)
		{
			chosen_y = y;
			chosen_wi = wi;
			chosen_e = e;
			chosen_phat = phat;
		}
	}

	if (wsum <= 0.0f)
		return none;
	chosen_e *= wsum / (candidates * chosen_phat);

	Lit out;
	const float clear = Visible(sc, s, chosen_y, rng);
	if (clear > 0.0f)
		out = Reflect(s, chosen_wi, chosen_e * clear);
	if (!sc.world->waters.empty())
		WaterBounce(sc, s, chosen_y, chosen_e, out, rng);
	return out;
}

// Light from the sky: a direction is drawn where the sky is bright and the
// light counts if nothing solid is in the way. Only tried as often as the
// sky can be seen from around here at all.
static Lit DirectSky(const Scene &sc, const Surface &s, Rng &rng)
{
	const World &w = *sc.world;
	Lit none;
	if (w.sky_cdf.empty())
		return none;

	float chance = 1.0f;
	if (!w.sky_chance.empty())
		chance = w.sky_chance[w.grid.Cell(s.p)];
	if (chance <= 0.0f || rng.Float() >= chance)
		return none;

	float pdf;
	const Vec3 sky_dir = w.SampleSky(rng, pdf);
	if (pdf <= 0.0f)
		return none;
	const Vec3 wi = sc.FromSky(sky_dir);
	if (!s.medium && (Dot(s.n, wi) <= 0.0f || Dot(s.ng, wi) <= 0.0f))
		return none;

	Ray ray;
	ray.o = s.p + s.ng * kRayOffset;
	ray.d = wi;
	ray.tmin = 0.0f;
	ray.tmax = FLT_MAX;
	Hit hit;
	const Tri *tri;
	if (!Closest(sc, ray, rng, false, true, hit, tri) || !(tri->mat->flags & PT_MAT_SKY))
		return none;

	return Reflect(s, wi, w.Sky(sky_dir) * (1.0f / (pdf * chance)));
}

Lit DirectWorld(const Scene &sc, const Surface &s, Rng &rng, bool first_hit)
{
	Lit lit = DirectLights(sc, s, rng, first_hit);
	const Lit sky = DirectSky(sc, s, rng);
	lit.diffuse += sky.diffuse;
	lit.specular += sky.specular;
	return lit;
}

Vec3 DirectMedium(const Scene &sc, Vec3 p, Rng &rng)
{
	Surface s{};
	s.medium = true;
	s.p = p;
	s.kd = Vec3(1, 1, 1);
	s.light_sampled_spec = false;
	const Lit world = DirectWorld(sc, s, rng, false), frame = DirectFrameOne(sc, s, rng);
	return world.diffuse + frame.diffuse;
}

Lit DirectFrameOne(const Scene &sc, const Surface &s, Rng &rng)
{

	const std::vector<Light> &lights = sc.frame->lights;
	Lit none;
	if (lights.empty())
		return none;

	// picked exactly in proportion to its unoccluded contribution
	float total = 0.0f;
	for (const Light &l : lights)
		total += Importance(s, PointLight(s, l, 1.0f));
	if (total <= 0.0f)
		return none;

	float pick = rng.Float() * total;
	for (const Light &l : lights)
	{
		Lit f = PointLight(s, l, 1.0f);
		const float imp = Importance(s, f);
		pick -= imp;
		if (pick <= 0.0f && imp > 0.0f)
		{
			const float clear = Visible(sc, s, l.origin, rng);
			if (clear <= 0.0f)
				return none;
			f.diffuse *= clear * total / imp;
			f.specular *= clear * total / imp;
			return f;
		}
	}
	return none;
}

Lit DirectFrameAll(const Scene &sc, const Surface &s, Rng &rng)
{
	Lit sum;
	for (const Light &l : sc.frame->lights)
	{
		const Lit f = PointLight(s, l, 1.0f);
		const float clear = Importance(s, f) > 0.0f ? Visible(sc, s, l.origin, rng) : 0.0f;
		if (clear > 0.0f)
		{
			sum.diffuse += f.diffuse * clear;
			sum.specular += f.specular * clear;
		}
	}
	return sum;
}

// keeps a sampled direction on the outside of the real surface
Vec3 AboveSurface(const Surface &s, Vec3 wi)
{
	const float below = Dot(wi, s.ng);
	return below < 0.0f ? wi - s.ng * (2.0f * below) : wi;
}

Vec3 SampleDiffuse(const Surface &s, Rng &rng)
{
	const float r1 = rng.Float(), r2 = rng.Float();
	const float r = std::sqrt(r1), phi = 2.0f * kPi * r2;
	Vec3 t, b;
	Basis(s.n, t, b);
	return AboveSurface(s, t * (r * std::cos(phi)) + b * (r * std::sin(phi)) + s.n * std::sqrt(std::max(0.0f, 1.0f - r1)));
}

// Samples the normals visible from wo (Heitz, "Sampling the GGX Distribution
// of Visible Normals", JCGT 2018) and reflects wo about the one drawn
bool SampleSpecular(const Surface &s, Rng &rng, Vec3 &wi, Vec3 &weight)
{
	Vec3 t, b;
	Basis(s.n, t, b);
	const Vec3 ve(Dot(s.wo, t), Dot(s.wo, b), Dot(s.wo, s.n));
	if (ve.z <= 0.0f)
		return false;

	const float a = s.alpha;
	const Vec3 vh = Normalize(Vec3(a * ve.x, a * ve.y, ve.z));
	const float lensq = vh.x * vh.x + vh.y * vh.y;
	const Vec3 t1 = lensq > 0.0f ? Vec3(-vh.y, vh.x, 0.0f) / std::sqrt(lensq) : Vec3(1, 0, 0);
	const Vec3 t2 = Cross(vh, t1);

	const float u1 = rng.Float(), u2 = rng.Float();
	const float r = std::sqrt(u1), phi = 2.0f * kPi * u2;
	const float p1 = r * std::cos(phi);
	float p2 = r * std::sin(phi);
	const float sblend = 0.5f * (1.0f + vh.z);
	p2 = (1.0f - sblend) * std::sqrt(std::max(0.0f, 1.0f - p1 * p1)) + sblend * p2;
	const Vec3 nh = t1 * p1 + t2 * p2 + vh * std::sqrt(std::max(0.0f, 1.0f - p1 * p1 - p2 * p2));
	const Vec3 hl = Normalize(Vec3(a * nh.x, a * nh.y, std::max(0.0f, nh.z)));
	const Vec3 h = t * hl.x + b * hl.y + s.n * hl.z;

	const float voh = Dot(s.wo, h);
	wi = h * (2.0f * voh) - s.wo;
	const float nol = Dot(s.n, wi), nov = ve.z;
	if (nol <= 0.0f || voh <= 0.0f)
		return false;
	wi = AboveSurface(s, wi);

	// value * cos / pdf comes down to F * G2 / G1(wo)
	const float lv = SmithLambda(a, nov), ll = SmithLambda(a, nol);
	const float g2_over_g1 = nol * (nov + lv) / (nov * ll + nol * lv);
	weight = Fresnel(s.f0, voh) * g2_over_g1;
	return true;
}

Vec3 Radiance(const Scene &sc, Ray ray, Rng &rng, bool camera, bool count_emitters, int depth, int max_bounces,
	float *reached)
{
	Vec3 radiance, throughput(1, 1, 1);
	if (reached)
		*reached = FLT_MAX;

	for (;; depth++, camera = false)
	{
		Hit hit;
		const Tri *tri;
		if (!Closest(sc, ray, rng, camera, true, hit, tri))
			return radiance;
		if (reached)
		{
			*reached = hit.t;
			reached = nullptr;
		}
		if (tri->mat->flags & PT_MAT_SKY)
			return (camera || count_emitters || sc.world->sky_cdf.empty())
				? radiance + throughput * sc.Sky(ray.d) : radiance;

		Surface s;
		MakeSurface(sc, *tri, hit, ray, s);
		const Material &mat = *s.mat;

		if (mat.emissive && s.front && (camera || count_emitters || !mat.sampled))
			radiance += throughput * Emitted(s, camera);

		const Vec3 ks = s.SpecularAlbedo();
		const float ld = Luminance(s.kd), ls = Luminance(ks);
		if (ld + ls <= 0.0f)
			return radiance;		// reflects nothing

		const Lit world = DirectWorld(sc, s, rng, false), frame = DirectFrameOne(sc, s, rng);
		radiance += throughput * (s.kd * (world.diffuse + frame.diffuse) * kInvPi + world.specular + frame.specular);

		if (depth >= max_bounces)
			return radiance;

		// continue through one lobe, chosen by how much each reflects
		float pick_spec = ls / (ld + ls);
		if (ld > 0.0f && ls > 0.0f)
			pick_spec = std::min(0.95f, std::max(0.05f, pick_spec));
		if (sc.reflections < 2)
		{
			// shiny surfaces still show highlights from lights, but nothing is
			// followed off them
			if (ld <= 0.0f)
				return radiance;
			pick_spec = 0.0f;
		}

		Vec3 wi;
		if (rng.Float() < pick_spec)
		{
			Vec3 weight;
			if (!SampleSpecular(s, rng, wi, weight))
				return radiance;
			throughput *= weight * (1.0f / pick_spec);
			count_emitters = !s.light_sampled_spec;
		}
		else
		{
			wi = SampleDiffuse(s, rng);
			throughput *= s.kd * (1.0f / (1.0f - pick_spec));
			count_emitters = false;
		}

		// paths that carry little are ended at random, the rest made to count for them
		if (depth >= 1)
		{
			const float survive = std::min(1.0f, std::max(0.1f, MaxComponent(throughput)));
			if (rng.Float() >= survive)
				return radiance;
			throughput *= 1.0f / survive;
		}

		ray.o = s.p + s.ng * kRayOffset;
		ray.d = wi;
		ray.tmin = 0.0f;
		ray.tmax = FLT_MAX;
	}
}

} // namespace PT_NS
