// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
//
// Light transport: surfaces, their reflectance, and paths through the scene.
#pragma once

#include "pt_world.h"

namespace pt {

const float kRayOffset = 0.03f;			// keeps rays off the surface they leave
const float kMaxSample = 40.0f;			// luminance clamp per path, tames fireflies

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
	Vec3			kd;			// diffuse reflectance
	Vec3			f0;			// specular reflectance head on
	float			roughness;
	float			alpha;		// GGX width, roughness squared
	bool			light_sampled_spec;

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
Vec3 ClampSample(Vec3 c);

// Nearest surface along the ray, in the world or the frame. Holes are stepped
// through, and so are surfaces the camera must not see when the ray comes
// from it. With cross set, surfaces that let light through are crossed at
// random in proportion to how much they pass. ray.tmin moves past what was
// skipped.
bool Closest(const Scene &sc, Ray &ray, Rng &rng, bool camera, bool cross, Hit &hit, const Tri *&tri);

void MakeSurface(const Scene &sc, const Tri &tri, const Hit &hit, const Ray &ray, Surface &s);

// what an emitter sends back along the ray that hit it
Vec3 Emitted(const Surface &s, bool seen);

// one of the world's lights, chosen by resampling
Lit DirectWorld(const Scene &sc, const Surface &s, Rng &rng, bool first_hit);
// the frame's point lights: one chosen, or all of them
Lit DirectFrameOne(const Scene &sc, const Surface &s, Rng &rng);
Lit DirectFrameAll(const Scene &sc, const Surface &s, Rng &rng);

Vec3 SampleDiffuse(const Surface &s, Rng &rng);
// returns false if the sample is unusable; weight is what the lobe reflects of it
bool SampleSpecular(const Surface &s, Rng &rng, Vec3 &wi, Vec3 &weight);

// Radiance arriving back along the ray. camera: the ray left the eye.
// count_emitters: emitters in the light lists count if hit (the bounce that
// made this ray could not have sampled them). depth: bounces already taken.
Vec3 Radiance(const Scene &sc, Ray ray, Rng &rng, bool camera, bool count_emitters, int depth, int max_bounces);

} // namespace pt
