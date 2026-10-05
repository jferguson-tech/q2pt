// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson

#include "pt_bvh.h"

#include <algorithm>
#include <cfloat>

namespace pt {

namespace {

struct Box
{
	Vec3 lo{FLT_MAX, FLT_MAX, FLT_MAX};
	Vec3 hi{-FLT_MAX, -FLT_MAX, -FLT_MAX};

	void Grow(Vec3 p) { lo = Min(lo, p); hi = Max(hi, p); }
	void Grow(const Box &b) { lo = Min(lo, b.lo); hi = Max(hi, b.hi); }
	float HalfArea() const
	{
		const Vec3 e = hi - lo;
		return e.x * e.y + e.y * e.z + e.z * e.x;
	}
};

const int kBins = 16;
const uint32_t kLeafSize = 4;

} // namespace

struct Bvh::BuildPrim
{
	Box			box;
	Vec3		centroid;
	uint32_t	index;
};

void Bvh::Build(const Vec3 *verts, uint32_t num_tris)
{
	nodes_.clear();
	tris_.clear();
	if (!num_tris)
		return;

	std::vector<BuildPrim> prims(num_tris);
	for (uint32_t i = 0; i < num_tris; i++)
	{
		BuildPrim &p = prims[i];
		p.box.Grow(verts[i * 3]);
		p.box.Grow(verts[i * 3 + 1]);
		p.box.Grow(verts[i * 3 + 2]);
		p.centroid = (p.box.lo + p.box.hi) * 0.5f;
		p.index = i;
	}

	nodes_.reserve(num_tris * 2);
	nodes_.emplace_back();
	BuildNode(0, prims.data(), 0, num_tris);

	tris_.resize(num_tris);
	for (uint32_t i = 0; i < num_tris; i++)
	{
		const uint32_t src = prims[i].index;
		tris_[i].p0 = verts[src * 3];
		tris_[i].e1 = verts[src * 3 + 1] - verts[src * 3];
		tris_[i].e2 = verts[src * 3 + 2] - verts[src * 3];
		tris_[i].index = src;
	}
}

// binned surface area heuristic
void Bvh::BuildNode(uint32_t node, BuildPrim *prims, uint32_t first, uint32_t count)
{
	Box bounds, cbounds;
	for (uint32_t i = 0; i < count; i++)
	{
		bounds.Grow(prims[first + i].box);
		cbounds.Grow(prims[first + i].centroid);
	}
	for (int a = 0; a < 3; a++)
	{
		nodes_[node].bmin[a] = bounds.lo[a];
		nodes_[node].bmax[a] = bounds.hi[a];
	}

	const Vec3 extent = cbounds.hi - cbounds.lo;
	int axis = 0;
	if (extent.y > extent.x) axis = 1;
	if (extent.z > extent[axis]) axis = 2;

	uint32_t mid = first;
	if (count > kLeafSize && extent[axis] > 0.0f)
	{
		Box binbox[kBins];
		uint32_t bincount[kBins] = {};
		const float scale = kBins / extent[axis];

		for (uint32_t i = 0; i < count; i++)
		{
			const BuildPrim &p = prims[first + i];
			int b = (int)((p.centroid[axis] - cbounds.lo[axis]) * scale);
			if (b >= kBins) b = kBins - 1;
			binbox[b].Grow(p.box);
			bincount[b]++;
		}

		float rightarea[kBins];
		Box acc;
		for (int b = kBins - 1; b > 0; b--)
		{
			acc.Grow(binbox[b]);
			rightarea[b] = acc.HalfArea();
		}

		float best = FLT_MAX;
		int bestsplit = -1;
		uint32_t nleft = 0;
		acc = Box();
		for (int b = 0; b < kBins - 1; b++)
		{
			if (bincount[b])
				acc.Grow(binbox[b]);
			nleft += bincount[b];
			if (!nleft || nleft == count)
				continue;
			const float cost = acc.HalfArea() * nleft + rightarea[b + 1] * (count - nleft);
			if (cost < best)
			{
				best = cost;
				bestsplit = b;
			}
		}

		if (bestsplit >= 0)
		{
			BuildPrim *m = std::partition(prims + first, prims + first + count,
				[&](const BuildPrim &p)
				{
					int b = (int)((p.centroid[axis] - cbounds.lo[axis]) * scale);
					if (b >= kBins) b = kBins - 1;
					return b <= bestsplit;
				});
			mid = (uint32_t)(m - prims);
		}
	}

	if (mid == first || mid == first + count)
	{
		if (count <= kLeafSize * 2)
		{
			nodes_[node].left = first;
			nodes_[node].count = count;
			return;
		}
		// many triangles on one spot: split down the middle
		mid = first + count / 2;
		std::nth_element(prims + first, prims + mid, prims + first + count,
			[axis](const BuildPrim &a, const BuildPrim &b) { return a.centroid[axis] < b.centroid[axis]; });
	}

	const uint32_t left = (uint32_t)nodes_.size();
	nodes_.emplace_back();
	nodes_.emplace_back();
	nodes_[node].left = left;
	nodes_[node].count = 0;
	BuildNode(left, prims, first, mid - first);
	BuildNode(left + 1, prims, mid, first + count - mid);
}

bool Bvh::Intersect(const Ray &ray, Hit &hit) const
{
	if (nodes_.empty())
		return false;

	const Vec3 inv(1.0f / ray.d.x, 1.0f / ray.d.y, 1.0f / ray.d.z);
	struct Entry { uint32_t node; float tnear; };
	Entry stack[64];
	int sp = 0;
	uint32_t ni = 0;
	float tmax = ray.tmax;
	bool found = false;
	float tn;

	if (!HitBox(nodes_[0], ray, inv, tmax, tn))
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
				if (HitTri(tri, ray, tmax, t, u, v))
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
		else
		{
			float t0, t1;
			const bool h0 = HitBox(nodes_[n.left], ray, inv, tmax, t0);
			const bool h1 = HitBox(nodes_[n.left + 1], ray, inv, tmax, t1);
			if (h0 && h1)
			{
				// nearer child first
				if (t0 <= t1)
				{
					stack[sp++] = {n.left + 1, t1};
					ni = n.left;
				}
				else
				{
					stack[sp++] = {n.left, t0};
					ni = n.left + 1;
				}
				continue;
			}
			if (h0) { ni = n.left; continue; }
			if (h1) { ni = n.left + 1; continue; }
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

} // namespace pt
