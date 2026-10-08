// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
//
// What the tracer traces: the static world and the per frame scene.
#pragma once

#include "../include/pt.h"
#include "pt_bvh.h"

#include <memory>
#include <vector>

namespace PT_NS {

const uint32_t kDynamic = 0x80000000u;	// triangle index bit: belongs to the frame, not the world

void InitColourTables();
extern float g_to_linear[256];

inline Vec3 Decode(uint32_t rgba)
{
	return Vec3(g_to_linear[rgba & 0xff], g_to_linear[(rgba >> 8) & 0xff], g_to_linear[(rgba >> 16) & 0xff]);
}

struct Texture
{
	int						width = 0, height = 0;
	std::vector<uint32_t>	pixels;
	Vec3					average{1, 1, 1};

	void Set(const pt_texture_t &src);

	uint32_t Texel(float u, float v) const
	{
		int x = (int)std::floor(u * width) % width;
		int y = (int)std::floor(v * height) % height;
		if (x < 0) x += width;
		if (y < 0) y += height;
		return pixels[(size_t)y * width + x];
	}

	// the four texels around a point and how much each counts, wrapping
	void Corners(float u, float v, uint32_t texel[4], float weight[4]) const
	{
		const float fx = u * width - 0.5f, fy = v * height - 0.5f;
		const float flx = std::floor(fx), fly = std::floor(fy);
		const float ax = fx - flx, ay = fy - fly;
		int x0 = (int)flx % width, y0 = (int)fly % height;
		if (x0 < 0) x0 += width;
		if (y0 < 0) y0 += height;
		const int x1 = x0 + 1 == width ? 0 : x0 + 1, y1 = y0 + 1 == height ? 0 : y0 + 1;
		texel[0] = pixels[(size_t)y0 * width + x0];
		texel[1] = pixels[(size_t)y0 * width + x1];
		texel[2] = pixels[(size_t)y1 * width + x0];
		texel[3] = pixels[(size_t)y1 * width + x1];
		weight[0] = (1.0f - ax) * (1.0f - ay);
		weight[1] = ax * (1.0f - ay);
		weight[2] = (1.0f - ax) * ay;
		weight[3] = ax * ay;
	}

	// bilinear, in linear light
	Vec3 Smooth(float u, float v) const
	{
		uint32_t t[4];
		float w[4];
		Corners(u, v, t, w);
		return Decode(t[0]) * w[0] + Decode(t[1]) * w[1] + Decode(t[2]) * w[2] + Decode(t[3]) * w[3];
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
	const Texture	*normal_texture = nullptr;
	const Texture	*emission_map = nullptr;	// what glows, in place of the rules below
	const Material	*anim_next = nullptr;
	int				anim_length = 1;	// materials in the animation this one starts
	Vec3			emission;			// average
	Vec3			emission_per_texel;	// multiply by the texel to get emitted radiance
	float			alpha = 1.0f;
	float			emission_seen = 0.0f;
	float			scroll_u = 0.0f, scroll_v = 0.0f;
	int				wave_map = 0, caustic_map = 0;	// handle + 1
	float			wave_rect[4] = {0, 0, 0, 0};
	Vec3			absorb;
	float			roughness = 1.0f;
	float			metallic = 0.0f;
	uint32_t		flags = 0;
	bool			emissive = false;
	bool			sampled = false;	// reached through the light lists, so not counted when hit by chance

	void Set(const pt_material_t &src, const Texture *tex, const Texture *normal_tex, const Texture *emission_tex);

	// the material showing at this step of its animation
	const Material &At(int frame) const
	{
		const Material *m = this;
		for (int i = anim_length > 1 ? frame % anim_length : 0; i > 0 && m->anim_next; i--)
			m = m->anim_next;
		return *m;
	}
};

struct Tri
{
	Vec3			p0, e1, e2;
	Vec3			n;			// unit, towards the counter clockwise side
	Vec3			tu, tv;		// unit directions in which u and v grow
	Vec3			vn[3];		// vertex normals, when smooth
	float			area;
	float			uv[3][2];
	const Material	*mat;
	bool			smooth;

	void Set(Vec3 a, Vec3 b, Vec3 c);	// needs uv filled in
};

struct Light
{
	uint32_t	tri;		// or ~0u for a point light
	Vec3		origin;		// point lights; centroid for triangles
	Vec3		emission;	// radiance for triangles, intensity for points
	float		pdf;		// chance of being picked map wide
	int			style;		// point lights: which light style scales it
	Vec3		dir;		// spotlights: where it points
	float		cone_cos;	// and how wide; 0 = all round
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
	bool					has_waves = false;

	// a simulated body of liquid: where its surface is and which material carries its maps
	struct Water
	{
		float			min_x, min_y, max_x, max_y, z;
		const Material	*mat;
	};
	std::vector<Water>		waters;

	Vec3 Sky(Vec3 d) const;

	// The sky as a light: a direction drawn in proportion to how bright the
	// sky is there, and the chance per unit solid angle of drawing it. Both
	// are in the sky's own frame, before any turning.
	std::vector<float>		sky_cdf;		// over 6 faces of sky_res * sky_res texels
	int						sky_res = 0;
	float					sky_total = 0.0f;	// integral of luminance over the sphere
	Vec3 SampleSky(Rng &rng, float &pdf) const;

	// per light grid cell, how often it is worth looking for the sky from there
	std::vector<float>		sky_chance;
};

// what the host hands over each frame
struct Frame
{
	std::vector<Material>	materials;
	std::vector<Tri>		tris;
	std::vector<Vec3>		prev;		// 3 per triangle: its corners last frame; may be empty
	Bvh						bvh;
	std::vector<Light>		lights;		// point lights only
	bool					has_held = false;	// something in it is carried by the eye (PT_MAT_HELD)
	uint32_t				hash = 0;	// changes when anything in it does
};

struct Scene
{
	const World	*world = nullptr;
	const Frame	*frame = nullptr;
	const float	*light_styles = nullptr;
	int			num_light_styles = 0;
	int			anim_frame = 0;
	float		time = 0.0f;

	// the sky box turns about an axis
	Vec3		sky_axis{0, 0, 1};
	float		sky_sin = 0.0f, sky_cos = 1.0f;

	Vec3 Turn(Vec3 v, float s) const	// about sky_axis, by the angle whose sine is s
	{
		return v * sky_cos + Cross(sky_axis, v) * s + sky_axis * (Dot(sky_axis, v) * (1.0f - sky_cos));
	}
	Vec3 Sky(Vec3 world_dir) const { return world->Sky(Turn(world_dir, -sky_sin)); }
	Vec3 FromSky(Vec3 sky_dir) const { return Turn(sky_dir, sky_sin); }

	// settings, see pt_view_t
	int			light_samples = 8;
	float		max_sample = 40.0f;
	float		wave_strength = 1.0f;
	bool		filter_textures = true;
	int			reflections = 2;
	int			view_mode = 0;
	float		metal_colour = 0.0f;	// see pt_view_t
	int			reflection_bounces = 3;
	float		reflection_rate = 1.0f;
	bool		refraction = true;
	float		fog_density = 0.0f;		// 0 = clear air

	const Tri &TriAt(uint32_t index) const
	{
		return (index & kDynamic) ? frame->tris[index & ~kDynamic] : world->tris[index];
	}

	// textures made with texture_create, by handle
	const std::vector<std::unique_ptr<Texture>> *handles = nullptr;

	const Texture *Map(int handle_plus_one) const
	{
		if (!handles || handle_plus_one <= 0 || handle_plus_one > (int)handles->size())
			return nullptr;
		return (*handles)[handle_plus_one - 1].get();
	}

	// where a point falls on a liquid's maps
	static void WaveCoord(const Material &m, Vec3 p, float &u, float &v)
	{
		u = (p.x - m.wave_rect[0]) * m.wave_rect[2];
		v = (p.y - m.wave_rect[1]) * m.wave_rect[3];
	}

	// how much the waves brighten light passing through the surface at p
	float Caustic(const Material &m, Vec3 p) const
	{
		const Texture *t = Map(m.caustic_map);
		if (!t)
			return 1.0f;
		float u, v;
		WaveCoord(m, p, u, v);
		if (u < 0.0f || v < 0.0f || u > 1.0f || v > 1.0f)
			return 1.0f;
		uint32_t texel[4];
		float w[4];
		t->Corners(u, v, texel, w);
		return ((texel[0] & 0xff) * w[0] + (texel[1] & 0xff) * w[1] + (texel[2] & 0xff) * w[2] + (texel[3] & 0xff) * w[3])
			* (4.0f / 255.0f);
	}

	float StyleScale(int style) const
	{
		return (style > 0 && style < num_light_styles) ? light_styles[style] : 1.0f;
	}
};

std::unique_ptr<World> BuildWorld(const pt_world_t *in);
void BuildFrame(Frame &f, const pt_scene_t *in, const std::vector<std::unique_ptr<Texture>> &textures);
uint32_t HashBytes(const void *data, size_t bytes, uint32_t h);

} // namespace PT_NS
