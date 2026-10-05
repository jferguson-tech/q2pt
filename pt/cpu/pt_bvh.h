// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
#pragma once

#include "pt_math.h"

#include <immintrin.h>
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

// Bounding volume hierarchy over a triangle soup. Each node holds the boxes
// of both its children, so one SSE test decides which of them a ray enters.
class Bvh
{
public:
	// verts holds 3 positions per triangle
	void Build(const Vec3 *verts, uint32_t num_tris);

	bool Empty() const { return tris_.empty(); }

	// closest hit in (tmin, tmax)
	bool Intersect(const Ray &ray, Hit &hit) const
	{
		return IntersectIf(ray, hit, [](uint32_t, float, float) { return true; });
	}

	// closest hit among the triangles for which accept(tri, u, v) says yes.
	// Unlike restarting the ray past a rejected triangle, this cannot step
	// over another triangle lying in the same plane.
	template <class F>
	bool IntersectIf(const Ray &ray, Hit &hit, F accept) const;

	// true if blocks(tri, u, v) says yes for any triangle in (tmin, tmax)
	template <class F>
	bool AnyHit(const Ray &ray, F blocks) const;

private:
	struct Node
	{
		// per axis: child 0 min, child 1 min, child 0 max, child 1 max
		float		bx[4], by[4], bz[4];
		uint32_t	child[2];	// inner: node index. leaf: first triangle
		uint32_t	count[2];	// triangles in a leaf, 0 for an inner node
	};
	struct Tri
	{
		Vec3		p0, e1, e2;
		uint32_t	index;
	};
	struct BuildPrim;
	struct Ref
	{
		uint32_t	index, count;
	};

	Ref BuildNode(BuildPrim *prims, uint32_t first, uint32_t count, float *lo, float *hi);

	// which children the ray enters before tmax, as a 2 bit mask, and where
	struct RayPack
	{
		__m128	ox, oy, oz, ix, iy, iz, tmin;

		explicit RayPack(const Ray &r)
			: ox(_mm_set1_ps(r.o.x)), oy(_mm_set1_ps(r.o.y)), oz(_mm_set1_ps(r.o.z)),
			  ix(_mm_set1_ps(1.0f / r.d.x)), iy(_mm_set1_ps(1.0f / r.d.y)), iz(_mm_set1_ps(1.0f / r.d.z)),
			  tmin(_mm_set1_ps(r.tmin)) {}
	};
	static int HitChildren(const Node &n, const RayPack &r, float tmax, float *tnear);
	static bool HitTri(const Tri &tri, const Ray &ray, float tmax, float &t, float &u, float &v);

	std::vector<Node>	nodes_;		// nodes_[0] is the root
	std::vector<Tri>	tris_;
};

inline int Bvh::HitChildren(const Node &n, const RayPack &r, float tmax, float *tnear)
{
	const __m128 tx = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.bx), r.ox), r.ix);
	const __m128 ty = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.by), r.oy), r.iy);
	const __m128 tz = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.bz), r.oz), r.iz);
	// swap the min and max halves so each lane sees both of its slab's planes
	const __m128 sx = _mm_shuffle_ps(tx, tx, _MM_SHUFFLE(1, 0, 3, 2));
	const __m128 sy = _mm_shuffle_ps(ty, ty, _MM_SHUFFLE(1, 0, 3, 2));
	const __m128 sz = _mm_shuffle_ps(tz, tz, _MM_SHUFFLE(1, 0, 3, 2));
	const __m128 enter = _mm_max_ps(_mm_max_ps(_mm_min_ps(tx, sx), _mm_min_ps(ty, sy)),
		_mm_max_ps(_mm_min_ps(tz, sz), r.tmin));
	const __m128 leave = _mm_min_ps(_mm_min_ps(_mm_max_ps(tx, sx), _mm_max_ps(ty, sy)),
		_mm_min_ps(_mm_max_ps(tz, sz), _mm_set1_ps(tmax)));
	_mm_storel_pi((__m64 *)tnear, enter);
	return _mm_movemask_ps(_mm_cmple_ps(enter, leave)) & 3;
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
bool Bvh::IntersectIf(const Ray &ray, Hit &hit, F accept) const
{
	if (nodes_.empty())
		return false;

	const RayPack pack(ray);
	struct Entry { uint32_t node; float tnear; };
	Entry stack[64];
	int sp = 0;
	uint32_t ni = 0;
	float tmax = ray.tmax;
	bool found = false;

	for (;;)
	{
		const Node &n = nodes_[ni];
		float tnear[2];
		const int mask = HitChildren(n, pack, tmax, tnear);

		// nearer child first, so a hit in it can rule the other out
		const int first = (mask == 3 && tnear[1] < tnear[0]) ? 1 : 0;
		uint32_t next = ~0u;
		for (int k = 0; k < 2; k++)
		{
			const int c = first ^ k;
			if (!(mask & (1 << c)) || tnear[c] >= tmax)
				continue;
			if (n.count[c])
			{
				for (uint32_t i = 0; i < n.count[c]; i++)
				{
					const Tri &tri = tris_[n.child[c] + i];
					float t, u, v;
					if (HitTri(tri, ray, tmax, t, u, v) && accept(tri.index, u, v))
					{
						tmax = t;
						hit.t = t;
						hit.u = u;
						hit.v = v;
						hit.tri = tri.index;
						found = true;
					}
				}
			}
			else if (next == ~0u)
				next = n.child[c];
			else
				stack[sp++] = {n.child[c], tnear[c]};
		}

		if (next != ~0u)
		{
			ni = next;
			continue;
		}
		for (;;)
		{
			if (!sp)
				return found;
			const Entry e = stack[--sp];
			if (e.tnear < tmax)
			{
				ni = e.node;
				break;
			}
		}
	}
}

template <class F>
bool Bvh::AnyHit(const Ray &ray, F blocks) const
{
	if (nodes_.empty())
		return false;

	const RayPack pack(ray);
	uint32_t stack[64];
	int sp = 0;
	uint32_t ni = 0;

	for (;;)
	{
		const Node &n = nodes_[ni];
		float tnear[2];
		const int mask = HitChildren(n, pack, ray.tmax, tnear);

		bool descend = false;
		for (int c = 0; c < 2; c++)
		{
			if (!(mask & (1 << c)))
				continue;
			if (n.count[c])
			{
				for (uint32_t i = 0; i < n.count[c]; i++)
				{
					const Tri &tri = tris_[n.child[c] + i];
					float t, u, v;
					if (HitTri(tri, ray, ray.tmax, t, u, v) && blocks(tri.index, u, v))
						return true;
				}
			}
			else if (descend)
				stack[sp++] = n.child[c];
			else
			{
				ni = n.child[c];
				descend = true;
			}
		}
		if (descend)
			continue;
		if (!sp)
			return false;
		ni = stack[--sp];
	}
}

} // namespace pt
