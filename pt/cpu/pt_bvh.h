// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
#pragma once

#include "pt_math.h"

#include <immintrin.h>
#include <vector>

namespace PT_NS {

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

// Bounding volume hierarchy over a triangle soup. Each node has up to four
// children and holds their boxes, so one SSE test decides which of them a
// ray enters, and a ray passes through half as many nodes as with two.
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
	static const uint32_t kNone = ~0u;
	struct Node
	{
		// the children's boxes, one child per lane. An unused lane has a
		// box at infinity that no ray reaches.
		float		lox[4], loy[4], loz[4], hix[4], hiy[4], hiz[4];
		uint32_t	child[4];	// inner: node index. leaf: first triangle. unused: kNone
		uint32_t	count[4];	// triangles in a leaf, 0 for an inner node
	};
	struct Node2;				// what the builder makes first: two children to a node
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

	Ref BuildNode(std::vector<Node2> &out, BuildPrim *prims, uint32_t first, uint32_t count, float *lo, float *hi);
	uint32_t Collapse(const std::vector<Node2> &src, uint32_t index);

	// which children the ray enters before tmax, as a 4 bit mask, and where
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
	const __m128 x0 = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.lox), r.ox), r.ix);
	const __m128 x1 = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.hix), r.ox), r.ix);
	const __m128 y0 = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.loy), r.oy), r.iy);
	const __m128 y1 = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.hiy), r.oy), r.iy);
	const __m128 z0 = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.loz), r.oz), r.iz);
	const __m128 z1 = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.hiz), r.oz), r.iz);
	const __m128 enter = _mm_max_ps(_mm_max_ps(_mm_min_ps(x0, x1), _mm_min_ps(y0, y1)),
		_mm_max_ps(_mm_min_ps(z0, z1), r.tmin));
	const __m128 leave = _mm_min_ps(_mm_min_ps(_mm_max_ps(x0, x1), _mm_max_ps(y0, y1)),
		_mm_min_ps(_mm_max_ps(z0, z1), _mm_set1_ps(tmax)));
	_mm_storeu_ps(tnear, enter);
	return _mm_movemask_ps(_mm_cmple_ps(enter, leave));
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

	// the lowest set bit of a 4 bit mask
	static const uint8_t kFirst[16] = {0, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};

	const RayPack pack(ray);
	struct Entry { uint32_t node; float tnear; };
	Entry stack[128];
	int sp = 0;
	uint32_t ni = 0;
	float tmax = ray.tmax;
	bool found = false;

	for (;;)
	{
		const Node &n = nodes_[ni];
		float tnear[4];
		int mask = HitChildren(n, pack, tmax, tnear);

		// nearest child first, so that a hit in it can rule the others out
		Entry inner[4];
		int num_inner = 0;
		while (mask)
		{
			int c = kFirst[mask];
			for (int rest = mask & (mask - 1); rest; rest &= rest - 1)
				if (tnear[kFirst[rest]] < tnear[c])
					c = kFirst[rest];
			mask &= ~(1 << c);
			if (tnear[c] >= tmax)
				break;		// and so are all the others

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
			else if (n.child[c] != kNone)
				inner[num_inner++] = {n.child[c], tnear[c]};
		}

		if (num_inner && inner[0].tnear < tmax)
		{
			// into the nearest; the rest wait, the farthest at the bottom
			for (int k = num_inner - 1; k > 0; k--)
				if (sp < 128)
					stack[sp++] = inner[k];
			ni = inner[0].node;
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
	uint32_t stack[128];
	int sp = 0;
	uint32_t ni = 0;

	for (;;)
	{
		const Node &n = nodes_[ni];
		float tnear[4];
		const int mask = HitChildren(n, pack, ray.tmax, tnear);

		bool descend = false;
		for (int c = 0; c < 4; c++)
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
			else if (n.child[c] == kNone)
				continue;
			else if (descend)
			{
				if (sp < 128)
					stack[sp++] = n.child[c];
			}
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

} // namespace PT_NS
