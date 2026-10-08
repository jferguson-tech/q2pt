// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
//
// What every pass shares: how the scene is laid out on the card, and, for
// the passes that trace, surfaces, lights and paths. It follows the CPU
// backend (pt/cpu/pt_trace.cpp) function for function, so that the two make
// the same picture.

// same values as PT_MAT_* in pt.h
const uint MAT_SKY = 1u;
const uint MAT_ALPHA_TEST = 2u;
const uint MAT_EMIT_TEXTURE = 4u;
const uint MAT_CAMERA_INVISIBLE = 8u;
const uint MAT_BLACK = 16u;
const uint MAT_WAVES = 32u;
const uint MAT_EMIT_BRIGHT = 64u;
const uint MAT_WARP = 128u;
const uint MAT_METAL_TEXTURE = 512u;
const uint MAT_METAL_PAINTED = 1024u;

// pt_view_t's view_mode, as in pt.h
const int VIEW_NORMAL = 0;
const int VIEW_CLAY = 1;
const int VIEW_MIRROR = 2;
const int VIEW_FURNACE = 3;
const int VIEW_LIGHTING = 4;
const int VIEW_DIRECT = 5;
const int VIEW_INDIRECT = 6;
const int VIEW_BASE_COLOUR = 7;
const int VIEW_NORMALS = 8;
const int VIEW_ROUGHNESS = 9;
const int VIEW_METAL = 10;
const int VIEW_GLOW = 11;
const int VIEW_BOUNCES = 12;
const float FURNACE_LIGHT = 0.5;

// Material.bits
const uint BIT_EMISSIVE = 1u;
const uint BIT_SAMPLED = 2u;		// reached through the light lists, so not counted when hit by chance

const float PI = 3.14159265;
const float INV_PI = 0.31830989;
const float RAY_OFFSET = 0.03;				// keeps rays off the surface they leave
const float LIGHT_SAMPLED_ROUGHNESS = 0.25;	// below this a highlight is found by its own path
const float MIN_ALPHA = 0.002;				// GGX gets numerically touchy below this
const float GLOBAL_LIGHT_CHANCE = 0.2;		// how often a light is picked map wide instead of nearby
const int PER_CELL = 24;					// lights listed per cell of the grid
const float MIN_DEMODULATE = 0.02;			// reflectance floor when lighting is divided by it

struct Tri
{
	vec2	uv0, uv1, uv2;
	uint	material;
	uint	pad;
};

struct Material
{
	int		texture;			// slots in textures, or -1
	uint	flags;
	float	alpha;
	float	roughness;
	vec4	emission;			// rgb: average emitted; a: radiance when seen directly, if set
	vec4	emission_per_texel;	// rgb: times the texel gives what is emitted; a: metallic
	int		normal_texture;
	int		emission_map;
	int		anim_next;			// the material shown next in its animation, or -1
	int		anim_length;
	vec4	absorb;				// rgb: how fast a liquid soaks up light
	vec4	wave_rect;			// where a simulated liquid's maps lie
	vec2	scroll;
	int		wave_map;			// slots, or -1
	int		caustic_map;
	uint	bits;
	uint	pad0, pad1, pad2;
};

struct Light
{
	vec3	origin;		// point lights; centroid for triangles
	uint	tri;		// or 0xffffffff for a point light
	vec3	emission;	// radiance for triangles, intensity for points
	float	pdf;		// chance of being picked map wide
	vec3	dir;		// spotlights: where it points
	float	cone_cos;	// and how wide; 0 = all round
	int		style;
	int		pad0, pad1, pad2;
};

layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;

layout(std140, set = 0, binding = 1) uniform Frame
{
	vec4	origin;			// w: tangent of half the horizontal field of view
	vec4	forward;		// w: tangent of half the vertical one
	vec4	right;			// w: this frame's offset within the pixel, x
	vec4	up;				// w: and y
	vec4	prev_origin;	// the same for the frame before
	vec4	prev_forward;
	vec4	prev_right;
	vec4	prev_up;
	ivec4	sky_a;			// textures of the sky's faces +X -X +Y -Y,
	ivec4	sky_b;			// +Z -Z; -1 where there is none
	vec4	sky_turn;		// xyz: the axis the sky turns about; w: sine of the angle
	vec4	sky_misc;		// cosine, brightness of a white texel, integral of its luminance, time
	ivec4	counts;			// lights of the map, lights of the frame, frame number, animation step
	ivec4	bases;			// y, w: first triangle of the map's glass and of the frame's; x: the view mode; z: what is done about noise, see pt_view_t
	ivec4	grid_dims;		// xyz; w: there is a grid
	vec4	grid_origin;	// xyz; w: one over the cell size
	ivec4	table_at;		// in tables: map wide light cdf, grid pdf, grid cdf, sky chance
	ivec4	table_at2;		// in tables: sky cdf; sky resolution; in indices: grid lights, grid counts
	ivec4	settings;		// bounces, light samples, reflections, reflection bounces
	vec4	settings_f;		// brightest a path may be, reflection rate, wave strength, fog density
	ivec4	settings2;		// refraction, smooth textures, paths a pixel, debug view
	vec4	medium;			// xyz: what the liquid the eye is in soaks up; w: exposure
	ivec4	output_i;		// tone curve, filter passes, frames kept while moving, there is history
	vec4	output_f;		// saturation, contrast, bloom, nothing has changed since last frame
	ivec4	frame_has;		// smooth normals, where things were last frame, which of each pair of images is this frame's, anti-aliasing
	ivec4	size;			// xy: of the picture being traced; z: simulated bodies of liquid
	vec4	water_rect[8];	// each body's extent: min x, min y, max x, max y
	vec4	water_at[8];	// x: the height of its surface; y: the material that carries its maps
	ivec4	out_size;		// xy: of the finished picture, the size of the view
	vec4	open_origin;	// motion blur: the eye as the shutter opened; w: there is blur
	vec4	open_forward;
	vec4	open_right;
	vec4	open_up;
	ivec4	held;			// x: first triangle of the frame carried by the eye (the weapon in hand); y: how many
	vec4	painted;		// x: what a metal painted dark reflects, see pt_view_t's metal_colour
} fr;

// the map and what moves, each as three corners per triangle, what goes with
// each triangle, and its materials
layout(std430, set = 0, binding = 2) readonly buffer WorldCorners { float v[]; } world_corners;
layout(std430, set = 0, binding = 3) readonly buffer WorldTris { Tri t[]; } world_tris;
layout(std430, set = 0, binding = 4) readonly buffer WorldMaterials { Material m[]; } world_materials;
layout(std430, set = 0, binding = 5) readonly buffer FrameCorners { float v[]; } frame_corners;
layout(std430, set = 0, binding = 6) readonly buffer FrameTris { Tri t[]; } frame_tris;
layout(std430, set = 0, binding = 7) readonly buffer FrameMaterials { Material m[]; } frame_materials;

layout(set = 0, binding = 8) uniform texture2D textures[];
layout(set = 0, binding = 9) uniform sampler nearest_sampler;
layout(set = 0, binding = 10) uniform sampler smooth_sampler;

layout(std430, set = 0, binding = 11) readonly buffer WorldLights { Light l[]; } world_lights;
layout(std430, set = 0, binding = 12) readonly buffer FrameLights { Light l[]; } frame_lights;
// 256 light styles, then the tables the frame block says where to find
layout(std430, set = 0, binding = 13) readonly buffer Tables { float v[]; } tables;
layout(std430, set = 0, binding = 14) readonly buffer Indices { uint v[]; } indices;
layout(std430, set = 0, binding = 15) readonly buffer FrameNormals { float v[]; } frame_normals;
layout(std430, set = 0, binding = 16) readonly buffer FramePrev { float v[]; } frame_prev;

// Per pixel, at the size the picture is traced at. Where there are two of
// something, one is this frame's and the other the last one's, in turn.
layout(set = 0, binding = 17, rgba16f) uniform image2D img_surface[2];	// shading normal, distance (negative: none)
layout(set = 0, binding = 18, rgba32f) uniform image2D img_seen;		// where to look for it in the last frame; roughness
layout(set = 0, binding = 19, rgba16f) uniform image2D img_albedo[2];	// what the diffuse and specular light are multiplied by
layout(set = 0, binding = 20, rgba16f) uniform image2D img_noisy[3];	// this frame's diffuse, specular and layer light
layout(set = 0, binding = 21, rgba16f) uniform image2D img_extra;		// light that needs no filtering
layout(set = 0, binding = 22, rgba16f) uniform image2D img_kept[6];		// the three gathered over time; a: frames
layout(set = 0, binding = 23, rgba16f) uniform image2D img_filter[6];	// the three being filtered
layout(set = 0, binding = 24, rgba8) uniform image2D img_picture;
layout(std430, set = 0, binding = 25) buffer Meter { uint v[]; } meter;	// sums for the exposure
layout(set = 0, binding = 26, rgba16f) uniform image2D img_hdr;			// the picture put together, exposed, before grading
layout(set = 0, binding = 27, rgba16f) uniform image2D img_bloom[2];	// its glow, at half size in the corner of each
// these two are the size of the view, which may be larger than what is traced
layout(set = 0, binding = 28, rgba16f) uniform image2D img_steady[2];	// the finished picture gathered over frames; a: how much stands behind it
layout(set = 0, binding = 29, rgba16f) uniform image2D img_graded;		// the picture graded for the screen, as traced

float Luminance(vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float MaxComponent(vec3 c)
{
	return max(c.r, max(c.g, c.b));
}

vec3 ToLinear(vec3 c)
{
	return pow(c, vec3(2.2));
}

// the colour that stands for a number of bounces, see PT_VIEW_BOUNCES
vec3 BounceColour(float bounces)
{
	const vec3 ramp[8] = vec3[8](vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0),
		vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 1.0), vec3(1.0, 1.0, 1.0));
	const float at = clamp(bounces, 0.0, 7.0);
	const int below = min(int(at), 6);
	return mix(ramp[below], ramp[below + 1], at - float(below));
}

#ifdef TRACING

// ----------------------------------------------------------------- chance

uint rng_state;

uint RandBits()
{
	// PCG hash step
	rng_state = rng_state * 747796405u + 2891336453u;
	const uint w = ((rng_state >> ((rng_state >> 28u) + 4u)) ^ rng_state) * 277803737u;
	return (w >> 22u) ^ w;
}

// [0, 1)
float Rand()
{
	return float(RandBits() >> 8) * (1.0 / 16777216.0);
}

uint Hash(uint a, uint b)
{
	uint h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
	h ^= h >> 16; h *= 0x85EBCA6Bu;
	h ^= h >> 13; h *= 0xC2B2AE35u;
	h ^= h >> 16;
	return h;
}

void Basis(vec3 n, out vec3 t, out vec3 b)
{
	const float s = n.z >= 0.0 ? 1.0 : -1.0;
	const float a = -1.0 / (s + n.z);
	const float c = n.x * n.y * a;
	t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
	b = vec3(c, s + n.y * n.y * a, -n.y);
}

// the first of count sorted values in tables, from base on, greater than x
int UpperBound(int base, int count, float x)
{
	int lo = 0, hi = count;
	while (lo < hi)
	{
		const int mid = (lo + hi) >> 1;
		if (tables.v[base + mid] <= x)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

// ------------------------------------------------------------ the scene

vec3 Corner(bool moving, int tri, int k)
{
	const int i = tri * 9 + k * 3;
	return moving ? vec3(frame_corners.v[i], frame_corners.v[i + 1], frame_corners.v[i + 2])
		: vec3(world_corners.v[i], world_corners.v[i + 1], world_corners.v[i + 2]);
}

Material MaterialOf(bool moving, uint index)
{
	if (moving)
		return frame_materials.m[index];
	return world_materials.m[index];
}

vec4 Texel(int slot, vec2 uv, bool smooth_it)
{
	if (smooth_it)
		return textureLod(sampler2D(textures[nonuniformEXT(slot)], smooth_sampler), uv, 0.0);
	return textureLod(sampler2D(textures[nonuniformEXT(slot)], nearest_sampler), uv, 0.0);
}

// about the sky's axis, by the angle whose sine is s
vec3 Turn(vec3 v, float s)
{
	const vec3 axis = fr.sky_turn.xyz;
	return v * fr.sky_misc.x + cross(axis, v) * s + axis * (dot(axis, v) * (1.0 - fr.sky_misc.x));
}

// The sky is a cube of six pictures around the eye. On the face along axis
// a, with b and c the next two axes in turn, a direction d lands at
//   u = (d[b] / |d[a]| + 1) / 2,  v = (d[c] / |d[a]| + 1) / 2
// d is in the sky's own frame, before any turning.
vec3 SkyAt(vec3 d)
{
	const vec3 a = abs(d);
	int axis = 0;
	if (a.y > a.x) axis = 1;
	if (a.z > (axis == 1 ? a.y : a.x)) axis = 2;
	const int face = axis * 2 + (d[axis] < 0.0 ? 1 : 0);
	const int tex = face < 4 ? fr.sky_a[face] : fr.sky_b[face - 4];
	if (tex < 0 || a[axis] <= 0.0)
		return vec3(0.0);
	const vec2 uv = vec2(d[(axis + 1) % 3], d[(axis + 2) % 3]) / a[axis] * 0.5 + 0.5;
	const ivec2 size = textureSize(sampler2D(textures[nonuniformEXT(tex)], nearest_sampler), 0);
	const ivec2 at = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
	return ToLinear(texelFetch(sampler2D(textures[nonuniformEXT(tex)], nearest_sampler), at, 0).rgb) * fr.sky_misc.y;
}

vec3 Sky(vec3 world_dir)
{
	return SkyAt(Turn(world_dir, -fr.sky_turn.w));
}

struct Hit
{
	bool	moving;		// part of the frame, not the map
	int		tri;
	vec2	bary;		// weights of corners 1 and 2
	float	t;
};

// The instances of the scene carry one of these masks: everything but what
// the eye carries (the weapon in hand) has MASK_SCENE, that has MASK_HELD.
const uint MASK_SCENE = 1u;
const uint MASK_HELD = 2u;
const uint MASK_ALL = 0xffu;

// the nearest thing along the ray among the instances the mask lets through,
// taking every triangle as it comes; glass is only met from its front
bool Nearest(vec3 origin, vec3 dir, float tmin, float tmax, uint mask, out Hit hit)
{
	rayQueryEXT query;
	rayQueryInitializeEXT(query, scene, gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT,
		mask, origin, tmin, dir, tmax);
	while (rayQueryProceedEXT(query))
		;
	if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT)
		return false;

	// instances: 0 the map, 1 its glass, 2 what moves, 3 its glass, 4 what the eye carries
	const int instance = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true);
	hit.moving = instance >= 2;
	hit.tri = rayQueryGetIntersectionPrimitiveIndexEXT(query, true)
		+ (instance == 4 ? fr.held.x : ((instance & 1) != 0 ? fr.bases[instance] : 0));
	hit.bary = rayQueryGetIntersectionBarycentricsEXT(query, true);
	hit.t = rayQueryGetIntersectionTEXT(query, true);
	return true;
}

bool Nearest(vec3 origin, vec3 dir, float tmin, float tmax, out Hit hit)
{
	return Nearest(origin, dir, tmin, tmax, MASK_ALL, hit);
}

Tri TriOf(Hit hit)
{
	if (hit.moving)
		return frame_tris.t[hit.tri];
	return world_tris.t[hit.tri];
}

vec2 TexCoord(Tri tri, vec2 bary)
{
	return tri.uv0 * (1.0 - bary.x - bary.y) + tri.uv1 * bary.x + tri.uv2 * bary.y;
}

bool IsHole(Material mat, Tri tri, vec2 bary)
{
	if ((mat.flags & MAT_ALPHA_TEST) == 0u || mat.texture < 0)
		return false;
	return Texel(mat.texture, TexCoord(tri, bary), false).a < 0.5;
}

// The nearest thing a path meets: not what only casts shadows (for the
// eye), not the holes in a grating, and, for light finding its way through
// (cross), a see-through surface only as often as it is opaque.
bool Closest(vec3 origin, vec3 dir, inout float tmin, bool camera, bool cross_through, uint mask, out Hit hit, out Material mat)
{
	for (int skips = 0; ; skips++)
	{
		if (!Nearest(origin, dir, tmin, 1.0e30, mask, hit))
			return false;
		const Tri tri = TriOf(hit);
		mat = MaterialOf(hit.moving, tri.material);
		const bool skip = skips < 32 && (
			(camera && (mat.flags & MAT_CAMERA_INVISIBLE) != 0u) ||
			IsHole(mat, tri, hit.bary) ||
			(cross_through && mat.alpha < 1.0 && Rand() >= mat.alpha) ||
			(fr.bases.x == VIEW_FURNACE && (mat.flags & MAT_BLACK) != 0u));
		if (!skip)
			return true;
		tmin = hit.t + 0.01;
	}
}

bool Closest(vec3 origin, vec3 dir, inout float tmin, bool camera, bool cross_through, out Hit hit, out Material mat)
{
	return Closest(origin, dir, tmin, camera, cross_through, MASK_ALL, hit, mat);
}

// how much the waves of a simulated liquid brighten light passing through
// its surface at p
float Caustic(Material mat, vec3 p)
{
	if (mat.caustic_map < 0)
		return 1.0;
	const vec2 at = (p.xy - mat.wave_rect.xy) * mat.wave_rect.zw;
	if (at.x < 0.0 || at.y < 0.0 || at.x > 1.0 || at.y > 1.0)
		return 1.0;
	return Texel(mat.caustic_map, at, true).r * 4.0;
}

// How much of the light from target reaches a point: 0 if something is in
// the way, otherwise 1 times whatever rippling liquid on the way does to
// it, which gathers the light in some places and thins it in others.
float Visible(vec3 p, vec3 target)
{
	const vec3 d = target - p;
	float tmin = 0.0;
	float through = 1.0;
	for (int skips = 0; skips < 16; skips++)
	{
		Hit hit;
		if (!Nearest(p, d, tmin, 0.999, hit))
			return through;
		const Tri tri = TriOf(hit);
		const Material mat = MaterialOf(hit.moving, tri.material);
		if (!IsHole(mat, tri, hit.bary))
		{
			if (mat.alpha >= 1.0 || Rand() < mat.alpha)
				return 0.0;
			through *= Caustic(mat, p + d * hit.t);
		}
		tmin = hit.t + 1.0e-4;
	}
	return 0.0;
}

// --------------------------------------------------------------- surfaces

struct Surface
{
	vec3	p;
	vec3	ng;			// geometric normal, on the side the ray came from
	vec3	n;			// shading normal
	vec3	wo;			// unit, towards where the ray came from
	vec3	tri_n;		// the triangle's own normal, whichever side was met
	bool	front;		// seen from the triangle's counter clockwise side
	vec3	colour;		// the texture's colour here
	vec3	glow;		// the emission map's, where the material has one
	vec3	kd;			// diffuse reflectance
	vec3	f0;			// specular reflectance head on
	float	roughness;
	float	metallic;	// here: the material's, or its texture's
	float	alpha;		// GGX width, roughness squared
	bool	light_sampled_spec;
	bool	medium;		// not a surface at all but a point in the air: no facing, scatters evenly
	Material mat;		// after animation
};

// What a metal reflects, worked out from the colour it was painted
// (MAT_METAL_PAINTED): pt_material_metal_colour in pt/material/pt_material.h,
// which says why, written again here. The two are to be kept the same.
const float METAL_PAINTED = 0.012;
const float METAL_HUE_KEPT = 0.6;

vec3 MetalColour(vec3 c, float level)
{
	if (level <= 0.0)
		return c;
	const float y = max(Luminance(c), 1.0e-6);
	const float bright = max(level * sqrt(y * (1.0 / METAL_PAINTED)), y);
	const float keep = (METAL_HUE_KEPT + (1.0 - METAL_HUE_KEPT) * y / bright) * min(MaxComponent(c) * (1.0 / 0.004), 1.0);
	const vec3 tinted = (vec3(1.0) + (c / y - vec3(1.0)) * keep) * bright;
	return tinted / max(MaxComponent(tinted), 1.0);
}

struct Lit
{
	vec3	diffuse;
	vec3	specular;
};

// the material showing at this step of its animation
Material Animated(Material mat, bool moving)
{
	if (moving || mat.anim_length <= 1)
		return mat;
	for (int i = fr.counts.w % mat.anim_length; i > 0 && mat.anim_next >= 0; i--)
		mat = world_materials.m[mat.anim_next];
	return mat;
}

// Epic's fit to the specular lobe's total reflectance (Karis, "Physically
// Based Shading on Mobile")
vec3 SpecularAlbedo(Surface s)
{
	const float nov = max(0.0, dot(s.n, s.wo));
	const float r0 = 1.0 - s.roughness;
	const float r1 = -0.0275 * s.roughness + 0.0425;
	const float r2 = -0.572 * s.roughness + 1.04;
	const float r3 = 0.022 * s.roughness - 0.04;
	const float a004 = min(r0 * r0, exp2(-9.28 * nov)) * r0 + r1;
	const float scale = -1.04 * a004 + r2, bias = 1.04 * a004 + r3;
	return s.f0 * scale + vec3(bias);
}

void MakeSurface(Hit hit, Material base, vec3 origin, vec3 dir, bool smooth_it, out Surface s)
{
	const Tri tri = TriOf(hit);
	const Material mat = Animated(base, hit.moving);
	const vec3 p0 = Corner(hit.moving, hit.tri, 0), p1 = Corner(hit.moving, hit.tri, 1), p2 = Corner(hit.moving, hit.tri, 2);
	const vec3 e1 = p1 - p0, e2 = p2 - p0;
	const vec3 x = cross(e1, e2);
	const float len = length(x);

	s.mat = mat;
	s.medium = false;
	s.p = origin + dir * hit.t;
	s.wo = -dir;
	s.tri_n = len > 0.0 ? x / len : vec3(0.0, 0.0, 1.0);
	s.front = dot(s.tri_n, dir) < 0.0;
	s.ng = s.front ? s.tri_n : -s.tri_n;

	vec2 uv = TexCoord(tri, hit.bary);
	if ((mat.flags & MAT_WARP) != 0u)
	{
		// each coordinate sways with the other, an eighth of a repeat either way
		const float u0 = uv.x;
		uv.x += 0.125 * sin(uv.y * 8.0 + fr.sky_misc.w);
		uv.y += 0.125 * sin(u0 * 8.0 + fr.sky_misc.w);
	}
	uv += mat.scroll * fr.sky_misc.w;

	// a simulated surface: the wave picture holds its slopes and height
	const float wave_strength = fr.settings_f.z;
	const bool simulated = mat.wave_map >= 0 && wave_strength > 0.0;
	vec2 wave = vec2(0.0);
	float wave_height = 0.0;
	if (simulated)
	{
		const vec2 at = (s.p.xy - mat.wave_rect.xy) * mat.wave_rect.zw;
		const vec4 w = Texel(mat.wave_map, at, true);
		wave = (w.rg - 0.5) * wave_strength;
		wave_height = (w.b - 0.5) * 8.0 * wave_strength;
		// what the texture stands for lies below the surface, so a tilted
		// surface shows it shifted, as through a lens
		uv += wave * 0.5;
	}

	s.colour = mat.texture < 0 ? vec3(1.0) : ToLinear(Texel(mat.texture, uv, smooth_it).rgb);
	s.glow = mat.emission_map >= 0 ? ToLinear(Texel(mat.emission_map, uv, smooth_it).rgb) : vec3(0.0);
	s.roughness = mat.roughness;
	float metallic = mat.emission_per_texel.a;

	vec3 n = s.ng;
	if (hit.moving && fr.frame_has.x != 0)
	{
		const int i = hit.tri * 9;
		const vec3 n0 = vec3(frame_normals.v[i], frame_normals.v[i + 1], frame_normals.v[i + 2]);
		const vec3 n1 = vec3(frame_normals.v[i + 3], frame_normals.v[i + 4], frame_normals.v[i + 5]);
		const vec3 n2 = vec3(frame_normals.v[i + 6], frame_normals.v[i + 7], frame_normals.v[i + 8]);
		const vec3 sum = n0 * (1.0 - hit.bary.x - hit.bary.y) + n1 * hit.bary.x + n2 * hit.bary.y;
		if (dot(sum, sum) > 1.0e-8)
		{
			n = normalize(sum);
			if (!s.front)
				n = -n;
		}
	}
	if (mat.normal_texture >= 0)
	{
		// x, y, z and roughness; the texture's own directions across the triangle
		const vec4 c = Texel(mat.normal_texture, uv, smooth_it);
		const vec2 d1 = tri.uv1 - tri.uv0, d2 = tri.uv2 - tri.uv0;
		const float det = d1.x * d2.y - d2.x * d1.y;
		vec3 tu, tv;
		if (abs(det) > 1.0e-12)
		{
			tu = normalize(e1 * d2.y - e2 * d1.y) * (det < 0.0 ? -1.0 : 1.0);
			tv = normalize(e2 * d1.x - e1 * d2.x) * (det < 0.0 ? -1.0 : 1.0);
		}
		else
			Basis(s.tri_n, tu, tv);
		vec3 t = c.xyz * 2.0 - 1.0;
		if ((mat.flags & MAT_METAL_TEXTURE) != 0u)
		{
			// the third number is metal: z is what is left of the normal's length
			t.z = sqrt(max(1.0 - dot(t.xy, t.xy), 0.01));
			metallic = c.b;
		}
		n = normalize(tu * t.x + tv * t.y + n * t.z);
		s.roughness = c.a;
	}
	if (simulated)
	{
		// the surface is level, facing up or (seen from below) down
		n = normalize(vec3(-wave, 1.0)) * (n.z < 0.0 ? -1.0 : 1.0);
		// crests gather the light in the liquid and troughs spread it
		s.colour *= clamp(1.0 + wave_height * 0.35, 0.6, 1.8);
	}
	else if ((mat.flags & MAT_WAVES) != 0u && wave_strength > 0.0)
	{
		// a few crossing ripples, enough to break up a reflection
		const float t = fr.sky_misc.w;
		const float a = sin(s.p.x * 0.071 + t * 1.7) + sin(s.p.y * 0.053 - t * 1.3)
			+ 0.5 * sin((s.p.x + s.p.y) * 0.19 + t * 2.9);
		const float b = sin(s.p.y * 0.067 + t * 1.5) + sin(s.p.x * 0.047 - t * 1.9)
			+ 0.5 * sin((s.p.x - s.p.y) * 0.17 + t * 2.3);
		vec3 t1, t2;
		Basis(n, t1, t2);
		n = normalize(n + t1 * (a * 0.02 * wave_strength) + t2 * (b * 0.02 * wave_strength));
	}
	// a normal tilted away from the viewer is no use for shading
	if (dot(n, s.wo) < 0.02 || dot(n, s.ng) <= 0.0)
		n = s.ng;
	s.n = n;

	s.metallic = metallic;
	if ((mat.flags & MAT_BLACK) != 0u)
	{
		s.kd = vec3(0.0);
		s.f0 = vec3(0.0);
	}
	else
	{
		s.kd = s.colour * (1.0 - metallic);
		s.f0 = vec3(0.04) * (1.0 - metallic)
			+ ((mat.flags & MAT_METAL_PAINTED) != 0u ? MetalColour(s.colour, fr.painted.x) : s.colour) * metallic;

		// A view mode's say over what the surface is made of (pt_view_t's
		// view_mode). What the surface emits is worked out from its colour,
		// which clay and mirror leave alone.
		const int view_mode = fr.bases.x;
		if (view_mode != VIEW_NORMAL)
		{
			if (view_mode == VIEW_CLAY && (mat.alpha >= 1.0 || (mat.flags & MAT_WAVES) != 0u))
			{
				s.kd = vec3(0.5);
				s.f0 = vec3(0.04);
				s.roughness = 1.0;
				s.mat.alpha = 1.0;		// liquids: solid to the eye
			}
			else if (view_mode == VIEW_MIRROR)
				s.roughness = 0.0;
			else if (view_mode == VIEW_FURNACE)
			{
				s.kd = vec3(1.0);
				s.f0 = vec3(0.04);
				s.roughness = 1.0;
				s.mat.alpha = 1.0;		// everything: solid to the eye
				s.colour = vec3(0.0);	// and nothing glows
				s.glow = vec3(0.0);
			}
		}
	}
	s.alpha = max(s.roughness * s.roughness, MIN_ALPHA);
	s.light_sampled_spec = s.roughness >= LIGHT_SAMPLED_ROUGHNESS;
}

// what the surface gives off; seen is for the eye looking straight at it
vec3 Emitted(Surface s, bool seen)
{
	const Material m = s.mat;
	if (m.emission_map >= 0)
		return m.emission.rgb * s.glow;
	if ((m.flags & MAT_EMIT_BRIGHT) != 0u)
	{
		// the lit parts of a screen or a button: bright texels glow, dark ones do not
		const float t = clamp((MaxComponent(s.colour) - 0.12) * (1.0 / 0.3), 0.0, 1.0);
		return m.emission.rgb * s.colour * (t * t * (3.0 - 2.0 * t));
	}
	return (seen && m.emission.a > 0.0) ? s.colour * m.emission.a : m.emission_per_texel.rgb * s.colour;
}

// the lighting only view: the surface as it would be were its texture white
void WhiteSurface(inout Surface s)
{
	if ((s.mat.flags & MAT_BLACK) != 0u)
		return;
	s.kd = vec3(1.0 - s.metallic);
	s.f0 = vec3(0.04 * (1.0 - s.metallic) + s.metallic);
}

// what a view of one thing known of the surface shows (VIEW_BASE_COLOUR to
// VIEW_GLOW)
vec3 SurfaceChannel(int mode, Surface s)
{
	// but for the colour, shown as the number it is: the display's curve
	// undoes this one
	if (mode == VIEW_BASE_COLOUR)
		return s.colour;
	if (mode == VIEW_NORMALS)
		return ToLinear(clamp(0.5 + 0.5 * s.n, 0.0, 1.0));
	if (mode == VIEW_ROUGHNESS)
		return ToLinear(vec3(clamp(s.roughness, 0.0, 1.0)));
	if (mode == VIEW_METAL)
		return ToLinear(vec3(clamp(s.metallic, 0.0, 1.0)));
	if (mode == VIEW_GLOW && (s.mat.bits & BIT_EMISSIVE) != 0u && s.front)
	{
		const vec3 e = Emitted(s, true);
		return e / max(1.0, MaxComponent(e));
	}
	return vec3(0.0);
}

// ---- GGX microfacet reflection, height correlated Smith shadowing ----

vec3 Fresnel(vec3 f0, float voh)
{
	const float m = 1.0 - max(voh, 0.0);
	const float m5 = m * m * m * m * m;
	return f0 + (vec3(1.0) - f0) * m5;
}

float SmithLambda(float alpha, float nox)	// the square root term shared by G1 and G2
{
	return sqrt(alpha * alpha + (1.0 - alpha * alpha) * nox * nox);
}

// the specular lobe's value times the cosine at the light: f * (n.wi)
vec3 SpecularTimesCos(Surface s, vec3 wi)
{
	const float nol = dot(s.n, wi), nov = dot(s.n, s.wo);
	if (nol <= 0.0 || nov <= 0.0)
		return vec3(0.0);
	const vec3 h = normalize(s.wo + wi);
	const float noh = dot(s.n, h), voh = dot(s.wo, h);
	const float a2 = s.alpha * s.alpha;
	const float d = noh * noh * (a2 - 1.0) + 1.0;
	const float ndf = a2 / (PI * d * d);
	const float vis = 0.5 / (nol * SmithLambda(s.alpha, nov) + nov * SmithLambda(s.alpha, nol));
	return Fresnel(s.f0, voh) * (ndf * vis * nol);
}

// Light arriving from direction wi with radiance-times-solid-angle e
// (irradiance on a surface facing it), through each lobe
Lit Reflect(Surface s, vec3 wi, vec3 e)
{
	Lit lit = Lit(vec3(0.0), vec3(0.0));
	if (s.medium)
	{
		lit.diffuse = e;		// air has no facing
		return lit;
	}
	const float nol = dot(s.n, wi);
	if (nol <= 0.0)
		return lit;
	lit.diffuse = e * nol;
	if (s.light_sampled_spec)
		lit.specular = e * SpecularTimesCos(s, wi);
	return lit;
}

float Importance(Surface s, Lit l)
{
	return Luminance(s.kd * l.diffuse) * INV_PI + Luminance(l.specular);
}

vec3 ClampSample(vec3 c, float max_luminance)
{
	const float lum = Luminance(c);
	if (isnan(lum) || isinf(lum))
		return vec3(0.0);
	return lum > max_luminance ? c * (max_luminance / lum) : c;
}

// the point a ray leaves a surface from; air has nowhere to stand off
vec3 Leave(Surface s)
{
	return s.medium ? s.p : s.p + s.ng * RAY_OFFSET;
}

// ----------------------------------------------------------------- lights

int GridCell(vec3 p)
{
	const ivec3 c = clamp(ivec3((p - fr.grid_origin.xyz) * fr.grid_origin.w), ivec3(0), fr.grid_dims.xyz - 1);
	return (c.z * fr.grid_dims.y + c.y) * fr.grid_dims.x + c.x;
}

// Light thrown back up by a simulated liquid surface: the dancing patches on
// walls and ceilings near water. The surface is treated as a mirror for the
// light just sampled, so its image lies as far below the surface as the
// light is above, and the gathering of light by the waves shapes what comes
// back. y is the point on the light, e what it would send straight here.
void WaterBounce(Surface s, vec3 y, vec3 e, inout Lit lit)
{
	if (s.medium)
		return;
	for (int i = 0; i < fr.size.z; i++)
	{
		const vec4 rect = fr.water_rect[i];
		const float z = fr.water_at[i].x;
		if (s.p.z <= z + 1.0 || y.z <= z + 1.0 || s.p.z - z > 512.0)
			continue;
		const vec3 image = vec3(y.xy, 2.0 * z - y.z);
		const vec3 d = image - s.p;
		const vec3 q = s.p + d * ((z - s.p.z) / d.z);		// where the path meets the surface
		if (q.x < rect.x || q.x > rect.z || q.y < rect.y || q.y > rect.w)
			continue;

		const float len2 = dot(d, d);
		const vec3 wi = d * inversesqrt(len2);
		const float m = 1.0 + wi.z;	// 1 - cosine of the angle at the water
		const float fresnel = 0.02 + 0.98 * m * m * m * m * m;
		const float gain = fresnel * Caustic(world_materials.m[int(fr.water_at[i].y)], q);
		if (gain <= 0.001)
			continue;

		// e was for the straight path; this one is as long as the way to the image
		const vec3 straight = y - s.p;
		const Lit add = Reflect(s, wi, e * (gain * dot(straight, straight) / len2));
		if (Importance(s, add) <= 0.0)
			continue;

		// both legs must be clear
		const vec3 above = q + vec3(0.0, 0.0, 0.1);
		if (Visible(Leave(s), above) <= 0.0 || Visible(above, y) <= 0.0)
			continue;

		lit.diffuse += add.diffuse;
		lit.specular += add.specular;
		return;		// one body will do
	}
}

// The map's lights. A handful of candidates are weighed by the light they
// would put on the surface, which is cheap; one is drawn in proportion, and
// only for that one is it found out whether anything is in the way.
Lit DirectLights(Surface s, bool first_hit)
{
	Lit none = Lit(vec3(0.0), vec3(0.0));
	const int num_lights = fr.counts.x;
	if (num_lights <= 0)
		return none;
	const int candidates = max(1, first_hit ? fr.settings.y : fr.settings.y / 2);

	float wsum = 0.0;
	vec3 chosen_y = vec3(0.0), chosen_wi = vec3(0.0), chosen_e = vec3(0.0);
	float chosen_phat = 0.0;

	const bool has_grid = fr.grid_dims.w != 0;
	const int cell = has_grid ? GridCell(s.p) : 0;
	const int in_cell = has_grid ? int(indices.v[fr.table_at2.w + cell]) : 0;
	const float use_global = in_cell > 0 ? GLOBAL_LIGHT_CHANCE : 1.0;
	const int cell_lights = fr.table_at2.z + cell * PER_CELL;
	const int cell_pdfs = fr.table_at.y + cell * PER_CELL;
	const int cell_cdf = fr.table_at.z + cell * PER_CELL;

	for (int i = 0; i < candidates; i++)
	{
		// mostly from the list for this cell, sometimes from the whole map so nothing is missed
		uint li;
		float cell_pdf = 0.0;
		if (Rand() < use_global)
		{
			li = uint(min(UpperBound(fr.table_at.x, num_lights, Rand()), num_lights - 1));
			for (int j = 0; j < in_cell; j++)
				if (indices.v[cell_lights + j] == li)
					cell_pdf = tables.v[cell_pdfs + j];
		}
		else
		{
			const int j = min(UpperBound(cell_cdf, in_cell, Rand()), in_cell - 1);
			li = indices.v[cell_lights + j];
			cell_pdf = tables.v[cell_pdfs + j];
		}
		const Light l = world_lights.l[li];
		float pdf = use_global * l.pdf + (1.0 - use_global) * cell_pdf;

		vec3 y, e, light_n = vec3(0.0);
		const bool area = l.tri != 0xffffffffu;
		if (area)
		{
			const vec3 p0 = Corner(false, int(l.tri), 0);
			const vec3 e1 = Corner(false, int(l.tri), 1) - p0, e2 = Corner(false, int(l.tri), 2) - p0;
			float a = Rand(), b = Rand();
			if (a + b > 1.0)
			{
				a = 1.0 - a;
				b = 1.0 - b;
			}
			y = p0 + e1 * a + e2 * b;
			e = l.emission;
			const vec3 x = cross(e1, e2);
			const float len = length(x);
			if (len <= 0.0)
				continue;
			light_n = x / len;
			pdf /= len * 0.5;
		}
		else
		{
			y = l.origin;
			const float scale = (l.style > 0 && l.style < 256) ? tables.v[l.style] : 1.0;
			if (scale <= 0.0)
				continue;
			e = l.emission * scale;
		}

		const vec3 d = y - s.p;
		const float dist2 = dot(d, d);
		if (dist2 <= 1.0e-6)
			continue;
		const vec3 wi = d * inversesqrt(dist2);
		if (l.cone_cos > 0.0 && !area && -dot(wi, l.dir) < l.cone_cos)
			continue;		// outside the spotlight's cone
		const float nol = s.medium ? 1.0 : dot(s.n, wi);
		if (nol <= 0.0)
			continue;
		float geom = 1.0 / dist2;
		if (area)
		{
			const float cosy = -dot(light_n, wi);
			if (cosy <= 0.0)
				continue;
			geom *= cosy;
		}
		e *= geom;

		const float phat = Luminance(e) * nol;
		if (phat <= 0.0 || pdf <= 0.0)
			continue;
		const float weight = phat / pdf;
		wsum += weight;
		if (Rand() * wsum < weight)
		{
			chosen_y = y;
			chosen_wi = wi;
			chosen_e = e;
			chosen_phat = phat;
		}
	}

	if (wsum <= 0.0)
		return none;
	chosen_e *= wsum / (float(candidates) * chosen_phat);

	Lit lit = none;
	const float clear = Visible(Leave(s), chosen_y);
	if (clear > 0.0)
		lit = Reflect(s, chosen_wi, chosen_e * clear);
	if (fr.size.z > 0)
		WaterBounce(s, chosen_y, chosen_e, lit);
	return lit;
}

// a direction drawn where the sky is bright, in the sky's own frame, and the
// chance per unit solid angle of drawing it
vec3 SampleSky(out float pdf)
{
	const int res = fr.table_at2.y;
	const int count = 6 * res * res;
	const int pick = min(UpperBound(fr.table_at2.x, count, Rand()), count - 1);
	const int face = pick / (res * res);
	const int texel = pick % (res * res);
	const float u = (float(texel % res) + Rand()) / float(res), v = (float(texel / res) + Rand()) / float(res);

	const int a = face / 2;
	vec3 d;
	d[a] = (face & 1) != 0 ? -1.0 : 1.0;
	d[(a + 1) % 3] = 2.0 * u - 1.0;
	d[(a + 2) % 3] = 2.0 * v - 1.0;
	d = normalize(d);

	// texels were weighted by luminance times solid angle, so per unit solid
	// angle the chance is just the luminance over its integral
	pdf = Luminance(SkyAt(d)) / fr.sky_misc.z;
	return d;
}

// Light from the sky: a direction is drawn where the sky is bright and the
// light counts if nothing solid is in the way. Only tried as often as the
// sky can be seen from around here at all.
Lit DirectSky(Surface s)
{
	Lit none = Lit(vec3(0.0), vec3(0.0));
	if (fr.table_at2.y <= 0 || fr.sky_misc.z <= 0.0)
		return none;

	float chance = 1.0;
	if (fr.grid_dims.w != 0)
		chance = tables.v[fr.table_at.w + GridCell(s.p)];
	if (chance <= 0.0 || Rand() >= chance)
		return none;

	float pdf;
	const vec3 sky_dir = SampleSky(pdf);
	if (pdf <= 0.0)
		return none;
	const vec3 wi = Turn(sky_dir, fr.sky_turn.w);
	if (!s.medium && (dot(s.n, wi) <= 0.0 || dot(s.ng, wi) <= 0.0))
		return none;

	Hit hit;
	Material mat;
	float tmin = 0.0;
	if (!Closest(Leave(s), wi, tmin, false, true, hit, mat) || (mat.flags & MAT_SKY) == 0u)
		return none;

	return Reflect(s, wi, SkyAt(sky_dir) * (1.0 / (pdf * chance)));
}

Lit DirectWorld(Surface s, bool first_hit)
{
	if (fr.bases.x == VIEW_FURNACE)
		return Lit(vec3(0.0), vec3(0.0));		// no light is lit
	Lit lit = DirectLights(s, first_hit);
	const Lit sky = DirectSky(s);
	lit.diffuse += sky.diffuse;
	lit.specular += sky.specular;
	return lit;
}

// a point light's contribution, ignoring occlusion
Lit PointLight(Surface s, Light l)
{
	const vec3 d = l.origin - s.p;
	const float dist2 = dot(d, d);
	if (dist2 <= 1.0e-6)
		return Lit(vec3(0.0), vec3(0.0));
	return Reflect(s, d * inversesqrt(dist2), l.emission / dist2);
}

// The frame's lights (muzzle flashes, explosions): one of them, picked
// exactly in proportion to what it would give with nothing in the way.
Lit DirectFrameOne(Surface s)
{
	Lit none = Lit(vec3(0.0), vec3(0.0));
	const int count = fr.counts.y;
	if (count <= 0 || fr.bases.x == VIEW_FURNACE)
		return none;

	float total = 0.0;
	for (int i = 0; i < count; i++)
		total += Importance(s, PointLight(s, frame_lights.l[i]));
	if (total <= 0.0)
		return none;

	float pick = Rand() * total;
	for (int i = 0; i < count; i++)
	{
		Lit f = PointLight(s, frame_lights.l[i]);
		const float imp = Importance(s, f);
		pick -= imp;
		if (pick <= 0.0 && imp > 0.0)
		{
			if (Visible(Leave(s), frame_lights.l[i].origin) <= 0.0)
				return none;
			f.diffuse *= total / imp;
			f.specular *= total / imp;
			return f;
		}
	}
	return none;
}

// all of them, exactly: for what the eye sees directly
Lit DirectFrameAll(Surface s)
{
	Lit sum = Lit(vec3(0.0), vec3(0.0));
	if (fr.bases.x == VIEW_FURNACE)
		return sum;
	for (int i = 0; i < fr.counts.y; i++)
	{
		const Lit f = PointLight(s, frame_lights.l[i]);
		if (Importance(s, f) > 0.0 && Visible(Leave(s), frame_lights.l[i].origin) > 0.0)
		{
			sum.diffuse += f.diffuse;
			sum.specular += f.specular;
		}
	}
	return sum;
}

// light on a point in the air, from all round
vec3 DirectMedium(vec3 p)
{
	Surface s;
	s.medium = true;
	s.p = p;
	s.ng = vec3(0.0, 0.0, 1.0);
	s.n = s.ng;
	s.wo = s.ng;
	s.kd = vec3(1.0);
	s.f0 = vec3(0.0);
	s.roughness = 1.0;
	s.metallic = 0.0;
	s.alpha = 1.0;
	s.light_sampled_spec = false;
	return DirectWorld(s, false).diffuse + DirectFrameOne(s).diffuse;
}

// ------------------------------------------------------------------ paths

// keeps a sampled direction on the outside of the real surface
vec3 AboveSurface(Surface s, vec3 wi)
{
	const float below = dot(wi, s.ng);
	return below < 0.0 ? wi - s.ng * (2.0 * below) : wi;
}

vec3 SampleDiffuse(Surface s)
{
	const float r1 = Rand(), r2 = Rand();
	const float r = sqrt(r1), phi = 2.0 * PI * r2;
	vec3 t, b;
	Basis(s.n, t, b);
	return AboveSurface(s, t * (r * cos(phi)) + b * (r * sin(phi)) + s.n * sqrt(max(0.0, 1.0 - r1)));
}

// Samples the normals visible from wo (Heitz, "Sampling the GGX Distribution
// of Visible Normals", JCGT 2018) and reflects wo about the one drawn
bool SampleSpecular(Surface s, out vec3 wi, out vec3 weight)
{
	vec3 t, b;
	Basis(s.n, t, b);
	const vec3 ve = vec3(dot(s.wo, t), dot(s.wo, b), dot(s.wo, s.n));
	wi = vec3(0.0);
	weight = vec3(0.0);
	if (ve.z <= 0.0)
		return false;

	const float a = s.alpha;
	const vec3 vh = normalize(vec3(a * ve.x, a * ve.y, ve.z));
	const float lensq = vh.x * vh.x + vh.y * vh.y;
	const vec3 t1 = lensq > 0.0 ? vec3(-vh.y, vh.x, 0.0) * inversesqrt(lensq) : vec3(1.0, 0.0, 0.0);
	const vec3 t2 = cross(vh, t1);

	const float u1 = Rand(), u2 = Rand();
	const float r = sqrt(u1), phi = 2.0 * PI * u2;
	const float p1 = r * cos(phi);
	float p2 = r * sin(phi);
	const float sblend = 0.5 * (1.0 + vh.z);
	p2 = (1.0 - sblend) * sqrt(max(0.0, 1.0 - p1 * p1)) + sblend * p2;
	const vec3 nh = t1 * p1 + t2 * p2 + vh * sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2));
	const vec3 hl = normalize(vec3(a * nh.x, a * nh.y, max(0.0, nh.z)));
	const vec3 h = t * hl.x + b * hl.y + s.n * hl.z;

	const float voh = dot(s.wo, h);
	wi = h * (2.0 * voh) - s.wo;
	const float nol = dot(s.n, wi), nov = ve.z;
	if (nol <= 0.0 || voh <= 0.0)
		return false;
	wi = AboveSurface(s, wi);

	// value * cos / pdf comes down to F * G2 / G1(wo)
	const float lv = SmithLambda(a, nov), ll = SmithLambda(a, nol);
	const float g2_over_g1 = nol * (nov + lv) / (nov * ll + nol * lv);
	weight = Fresnel(s.f0, voh) * g2_over_g1;
	return true;
}

// The light a ray brings back, following it from surface to surface.
// count_emitters: lights it runs into count, because nothing has sampled
// them for it. reached: how far off the first thing it met was.
// rays_followed is raised by the number of rays the path was made of.
int rays_followed = 0;

vec3 Radiance(vec3 origin, vec3 dir, bool count_emitters, int depth, int max_bounces, out float reached)
{
	vec3 radiance = vec3(0.0), throughput = vec3(1.0);
	reached = 1.0e30;
	bool first = true;
	// in the white furnace every path ends in the same light
	const bool furnace = fr.bases.x == VIEW_FURNACE;

	for (;; depth++)
	{
		Hit hit;
		Material base;
		float tmin = 0.0;
		rays_followed++;
		if (!Closest(origin, dir, tmin, false, true, hit, base))
			return furnace ? radiance + throughput * FURNACE_LIGHT : radiance;
		if (first)
		{
			reached = hit.t;
			first = false;
		}
		if ((base.flags & MAT_SKY) != 0u)
		{
			if (furnace)
				return radiance + throughput * FURNACE_LIGHT;
			return (count_emitters || fr.table_at2.y <= 0) ? radiance + throughput * Sky(dir) : radiance;
		}

		Surface s;
		MakeSurface(hit, base, origin, dir, false, s);

		if ((s.mat.bits & BIT_EMISSIVE) != 0u && s.front && (count_emitters || (s.mat.bits & BIT_SAMPLED) == 0u))
			radiance += throughput * Emitted(s, false);

		const vec3 ks = SpecularAlbedo(s);
		const float ld = Luminance(s.kd), ls = Luminance(ks);
		if (ld + ls <= 0.0)
			return radiance;		// reflects nothing

		const Lit world = DirectWorld(s, false), frame = DirectFrameOne(s);
		radiance += throughput * (s.kd * (world.diffuse + frame.diffuse) * INV_PI + world.specular + frame.specular);

		if (depth >= max_bounces)
			return furnace ? radiance + throughput * FURNACE_LIGHT : radiance;

		// continue through one lobe, chosen by how much each reflects
		float pick_spec = ls / (ld + ls);
		if (ld > 0.0 && ls > 0.0)
			pick_spec = clamp(pick_spec, 0.05, 0.95);
		if (fr.settings.z < 2)
		{
			// shiny surfaces still show highlights from lights, but nothing is
			// followed off them
			if (ld <= 0.0)
				return radiance;
			pick_spec = 0.0;
		}

		vec3 wi;
		if (Rand() < pick_spec)
		{
			vec3 weight;
			if (!SampleSpecular(s, wi, weight))
				return radiance;
			throughput *= weight * (1.0 / pick_spec);
			count_emitters = !s.light_sampled_spec;
		}
		else
		{
			wi = SampleDiffuse(s);
			throughput *= s.kd * (1.0 / (1.0 - pick_spec));
			count_emitters = false;
		}

		// paths that carry little are ended at random, the rest made to count for them
		if (depth >= 1)
		{
			const float survive = clamp(MaxComponent(throughput), 0.1, 1.0);
			if (Rand() >= survive)
				return radiance;
			throughput *= 1.0 / survive;
		}

		origin = s.p + s.ng * RAY_OFFSET;
		dir = wi;
	}
}

#endif
