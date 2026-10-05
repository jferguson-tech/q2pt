// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson

#include "pt_bvh.h"

#include <algorithm>
#include <cfloat>
#include <limits>

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

struct Bvh::Node2
{
	// per axis: child 0 min, child 1 min, child 0 max, child 1 max
	float		bx[4], by[4], bz[4];
	uint32_t	child[2];	// inner: node index. leaf: first triangle
	uint32_t	count[2];	// triangles in a leaf, 0 for an inner node
};

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

	std::vector<Node2> pairs;
	pairs.reserve(num_tris);
	float lo[3], hi[3];
	const Ref top = BuildNode(pairs, prims.data(), 0, num_tris, lo, hi);
	if (top.count)
	{
		// everything fitted in one leaf: hang it off a root whose other child is empty
		Node2 root{};
		for (int a = 0; a < 3; a++)
		{
			float *b = a == 0 ? root.bx : (a == 1 ? root.by : root.bz);
			b[0] = lo[a];
			b[2] = hi[a];
			b[1] = b[3] = std::numeric_limits<float>::infinity();
		}
		root.child[0] = top.index;
		root.count[0] = top.count;
		root.child[1] = kNone;
		pairs.push_back(root);
	}

	nodes_.reserve(pairs.size() / 2 + 1);
	Collapse(pairs, 0);

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
Bvh::Ref Bvh::BuildNode(std::vector<Node2> &out, BuildPrim *prims, uint32_t first, uint32_t count, float *lo, float *hi)
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

	const uint32_t node = (uint32_t)out.size();
	out.emplace_back();

	float clo[2][3], chi[2][3];
	const Ref left = BuildNode(out, prims, first, mid - first, clo[0], chi[0]);
	const Ref right = BuildNode(out, prims, mid, first + count - mid, clo[1], chi[1]);

	Node2 &n = out[node];
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

// Makes the node with four children that stands for src[index] and all
// under it. Grandchildren move up beside their parent's sibling: the largest
// inner child is replaced by its own two until there are four, or only
// leaves are left.
uint32_t Bvh::Collapse(const std::vector<Node2> &src, uint32_t index)
{
	struct Item
	{
		float		lo[3], hi[3];
		uint32_t	child, count;
	};
	Item items[4];
	int num = 0;

	const auto take = [&](const Node2 &s, int c)
	{
		if (!s.count[c] && s.child[c] == kNone)
			return;
		Item &it = items[num++];
		it.lo[0] = s.bx[c]; it.hi[0] = s.bx[c + 2];
		it.lo[1] = s.by[c]; it.hi[1] = s.by[c + 2];
		it.lo[2] = s.bz[c]; it.hi[2] = s.bz[c + 2];
		it.child = s.child[c];
		it.count = s.count[c];
	};
	take(src[index], 0);
	take(src[index], 1);

	while (num < 4)
	{
		int widest = -1;
		float area = -1.0f;
		for (int k = 0; k < num; k++)
		{
			if (items[k].count)
				continue;
			const float ex = items[k].hi[0] - items[k].lo[0], ey = items[k].hi[1] - items[k].lo[1],
				ez = items[k].hi[2] - items[k].lo[2];
			const float a = ex * ey + ey * ez + ez * ex;
			if (a > area)
			{
				area = a;
				widest = k;
			}
		}
		if (widest < 0)
			break;
		const Node2 &s = src[items[widest].child];
		items[widest] = items[--num];
		take(s, 0);
		take(s, 1);
	}

	const uint32_t node = (uint32_t)nodes_.size();
	nodes_.emplace_back();
	{
		Node &n = nodes_[node];
		const float inf = std::numeric_limits<float>::infinity();
		for (int k = 0; k < 4; k++)
		{
			const bool used = k < num;
			n.lox[k] = used ? items[k].lo[0] : inf; n.hix[k] = used ? items[k].hi[0] : inf;
			n.loy[k] = used ? items[k].lo[1] : inf; n.hiy[k] = used ? items[k].hi[1] : inf;
			n.loz[k] = used ? items[k].lo[2] : inf; n.hiz[k] = used ? items[k].hi[2] : inf;
			n.child[k] = used ? items[k].child : kNone;
			n.count[k] = used ? items[k].count : 0;
		}
	}
	// the nodes move as more are added, so each child is filled in by index
	for (int k = 0; k < num; k++)
	{
		if (!items[k].count)
		{
			const uint32_t c = Collapse(src, items[k].child);
			nodes_[node].child[k] = c;
		}
	}
	return node;
}

} // namespace pt
