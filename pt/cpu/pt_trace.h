// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
//
// Light transport: surfaces, their reflectance, and paths through the scene.
#pragma once

#include "pt_world.h"

namespace PT_NS {

const float kRayOffset = 0.03f;			// keeps rays off the surface they leave

// Below this roughness the specular lobe is too narrow for light sampling
// to find; lights are then picked up when a reflected ray happens to hit them.
const float kLightSampledRoughness = 0.25f;

// A point on a surface, ready to be lit
struct Surface
{
	const Tri		*tri;
	const Material	*mat;		// after animation
	Vec3			p;
	Vec3			ng;			// geometric normal, on the side the ray came from
	Vec3			n;			// shading normal
	Vec3			wo;			// unit, towards where the ray came from
	bool			front;		// seen from the triangle's counter clockwise side
	Vec3			colour;		// the texture's colour here
	Vec3			glow;		// the emission map's, where the material has one
	Vec3			kd;			// diffuse reflectance
	Vec3			f0;			// specular reflectance head on
	float			roughness;
	float			alpha;		// GGX width, roughness squared
	bool			light_sampled_spec;
	bool			medium;		// not a surface at all but a point in the air: no facing, scatters evenly

	// how much the specular lobe reflects in total towards wo, roughly
	Vec3 SpecularAlbedo() const;
};

// light from a sampled light, split by the lobe that reflects it
struct Lit
{
	Vec3	diffuse;	// irradiance: multiply by kd / pi
	Vec3	specular;	// radiance, already through the specular lobe
};

bool Finite(float f);
// limits how bright one sample may be, which tames fireflies
Vec3 ClampSample(Vec3 c, float max_luminance);

// See-through surfaces are one sided, as the game draws them: from behind
// they are not there. A pane of glass modelled as a slab would otherwise tint
// and reflect twice.
bool BackOfGlass(const Tri &tri, Vec3 dir);

// Nearest surface along the ray, in the world or the frame. Holes are stepped
// through, and so are surfaces the camera must not see when the ray comes
// from it. With cross set, surfaces that let light through are crossed at
// random in proportion to how much they pass. ray.tmin moves past what was
// skipped. held says what is done with the frame's triangles that the eye
// carries (PT_MAT_HELD): taken like any other, passed by, or the only ones
// met, for a ray that is cast at them alone.
enum HeldRays { kHeldToo, kNotHeld, kHeldOnly };
bool Closest(const Scene &sc, Ray &ray, Rng &rng, bool camera, bool cross, Hit &hit, const Tri *&tri,
	HeldRays held = kHeldToo);

// smooth: filter the textures, for surfaces the eye sees directly. Further
// along a path the nearest texel is as good and cheaper.
void MakeSurface(const Scene &sc, const Tri &tri, const Hit &hit, const Ray &ray, Surface &s, bool smooth = false);
// in the clay view this material is clay: all that is solid, and liquids
bool ClayCovers(const Material &mat);

// what an emitter sends back along the ray that hit it
Vec3 Emitted(const Surface &s, bool seen);

// one of the world's lights, chosen by resampling
Lit DirectWorld(const Scene &sc, const Surface &s, Rng &rng, bool first_hit);
// the frame's point lights: one chosen, or all of them
Lit DirectFrameOne(const Scene &sc, const Surface &s, Rng &rng);
Lit DirectFrameAll(const Scene &sc, const Surface &s, Rng &rng);

// light arriving at a point in the air from one sampled light and the sky,
// as irradiance on a surface facing it
Vec3 DirectMedium(const Scene &sc, Vec3 p, Rng &rng);

Vec3 SampleDiffuse(const Surface &s, Rng &rng);
// returns false if the sample is unusable; weight is what the lobe reflects of it
bool SampleSpecular(const Surface &s, Rng &rng, Vec3 &wi, Vec3 &weight);

// Radiance arriving back along the ray. camera: the ray left the eye.
// count_emitters: emitters in the light lists count if hit (the bounce that
// made this ray could not have sampled them). depth: bounces already taken.
// reached, if given, gets how far the ray went before it met anything.
Vec3 Radiance(const Scene &sc, Ray ray, Rng &rng, bool camera, bool count_emitters, int depth, int max_bounces,
	float *reached = nullptr);

} // namespace PT_NS
