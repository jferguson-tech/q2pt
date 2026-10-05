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

	nodes_.reserve(num_tris);
	float lo[3], hi[3];
	const Ref top = BuildNode(prims.data(), 0, num_tris, lo, hi);
	if (top.count)
	{
		// everything fitted in one leaf: hang it off a root whose other child is empty
		Node root{};
		for (int a = 0; a < 3; a++)
		{
			float *b = a == 0 ? root.bx : (a == 1 ? root.by : root.bz);
			b[0] = lo[a];
			b[2] = hi[a];
			b[1] = FLT_MAX;
			b[3] = -FLT_MAX;
		}
		root.child[0] = top.index;
		root.count[0] = top.count;
		nodes_.push_back(root);
	}

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

// Binned surface area heuristic. Returns a leaf, or the node made for an
// inner split, and the bounds of everything under it.
Bvh::Ref Bvh::BuildNode(BuildPrim *prims, uint32_t first, uint32_t count, float *lo, float *hi)
{
	Box bounds, cbounds;
	for (uint32_t i = 0; i < count; i++)
	{
		bounds.Grow(prims[first + i].box);
		cbounds.Grow(prims[first + i].centroid);
	}
	for (int a = 0; a < 3; a++)
	{
		lo[a] = bounds.lo[a];
		hi[a] = bounds.hi[a];
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
			return Ref{first, count};
		// many triangles on one spot: split down the middle
		mid = first + count / 2;
		std::nth_element(prims + first, prims + mid, prims + first + count,
			[axis](const BuildPrim &a, const BuildPrim &b) { return a.centroid[axis] < b.centroid[axis]; });
	}

	const uint32_t node = (uint32_t)nodes_.size();
	nodes_.emplace_back();

	float clo[2][3], chi[2][3];
	const Ref left = BuildNode(prims, first, mid - first, clo[0], chi[0]);
	const Ref right = BuildNode(prims, mid, first + count - mid, clo[1], chi[1]);

	Node &n = nodes_[node];
	for (int c = 0; c < 2; c++)
	{
		n.bx[c] = clo[c][0]; n.bx[c + 2] = chi[c][0];
		n.by[c] = clo[c][1]; n.by[c + 2] = chi[c][1];
		n.bz[c] = clo[c][2]; n.bz[c + 2] = chi[c][2];
	}
	n.child[0] = left.index;
	n.count[0] = left.count;
	n.child[1] = right.index;
	n.count[1] = right.count;
	return Ref{node, 0};
}

} // namespace pt
