// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
#pragma once

#include "pt_math.h"

#include <vector>

namespace pt {

struct Ray
{
	Vec3	o, d;
	float	tmin, tmax;
};

struct Hit
{
	float		t, u, v;	// u, v: barycentric weights of vertices 1 and 2
	uint32_t	tri;		// index as given to Build
};

// Bounding volume hierarchy over a triangle soup
class Bvh
{
public:
	// verts holds 3 positions per triangle
	void Build(const Vec3 *verts, uint32_t num_tris);

	bool Empty() const { return tris_.empty(); }

	// closest hit in (tmin, tmax)
	bool Intersect(const Ray &ray, Hit &hit) const;

	// true if blocks(tri, u, v) says yes for any triangle in (tmin, tmax)
	template <class F>
	bool AnyHit(const Ray &ray, F blocks) const;

private:
	struct Node
	{
		float		bmin[3];
		uint32_t	left;		// inner: children are left and left+1. leaf: first triangle
		float		bmax[3];
		uint32_t	count;		// 0 for inner nodes
	};
	struct Tri
	{
		Vec3		p0, e1, e2;
		uint32_t	index;
	};
	struct BuildPrim;

	void BuildNode(uint32_t node, BuildPrim *prims, uint32_t first, uint32_t count);

	static bool HitBox(const Node &n, const Ray &ray, Vec3 inv, float tmax, float &tnear);
	static bool HitTri(const Tri &tri, const Ray &ray, float tmax, float &t, float &u, float &v);

	std::vector<Node>	nodes_;
	std::vector<Tri>	tris_;
};

inline bool Bvh::HitBox(const Node &n, const Ray &ray, Vec3 inv, float tmax, float &tnear)
{
	float t0 = ray.tmin, t1 = tmax;
	for (int a = 0; a < 3; a++)
	{
		float ta = (n.bmin[a] - ray.o[a]) * inv[a];
		float tb = (n.bmax[a] - ray.o[a]) * inv[a];
		if (ta > tb) { const float s = ta; ta = tb; tb = s; }
		if (ta > t0) t0 = ta;
		if (tb < t1) t1 = tb;
	}
	tnear = t0;
	return t0 <= t1;
}

// Moller-Trumbore
inline bool Bvh::HitTri(const Tri &tri, const Ray &ray, float tmax, float &t, float &u, float &v)
{
	const Vec3 p = Cross(ray.d, tri.e2);
	const float det = Dot(tri.e1, p);
	if (det > -1e-12f && det < 1e-12f)
		return false;
	const float inv = 1.0f / det;
	const Vec3 s = ray.o - tri.p0;
	u = Dot(s, p) * inv;
	if (u < 0.0f || u > 1.0f)
		return false;
	const Vec3 q = Cross(s, tri.e1);
	v = Dot(ray.d, q) * inv;
	if (v < 0.0f || u + v > 1.0f)
		return false;
	t = Dot(tri.e2, q) * inv;
	return t > ray.tmin && t < tmax;
}

template <class F>
bool Bvh::AnyHit(const Ray &ray, F blocks) const
{
	if (nodes_.empty())
		return false;

	const Vec3 inv(1.0f / ray.d.x, 1.0f / ray.d.y, 1.0f / ray.d.z);
	uint32_t stack[64];
	int sp = 0;
	uint32_t ni = 0;
	float tn;

	if (!HitBox(nodes_[0], ray, inv, ray.tmax, tn))
		return false;

	for (;;)
	{
		const Node &n = nodes_[ni];
		if (n.count)
		{
			for (uint32_t i = 0; i < n.count; i++)
			{
				const Tri &tri = tris_[n.left + i];
				float t, u, v;
				if (HitTri(tri, ray, ray.tmax, t, u, v) && blocks(tri.index, u, v))
					return true;
			}
		}
		else
		{
			const bool h0 = HitBox(nodes_[n.left], ray, inv, ray.tmax, tn);
			const bool h1 = HitBox(nodes_[n.left + 1], ray, inv, ray.tmax, tn);
			if (h0 && h1)
			{
				stack[sp++] = n.left + 1;
				ni = n.left;
				continue;
			}
			if (h0) { ni = n.left; continue; }
			if (h1) { ni = n.left + 1; continue; }
		}
		if (!sp)
			return false;
		ni = stack[--sp];
	}
}

} // namespace pt
