#include "engine/core/spatial/spatial_index.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Shard::Engine::Core {

    namespace {
        constexpr double kInf = std::numeric_limits<double>::infinity();

        template <typename T>
        void SwapPop(std::vector<T>& v, const T& value)
        {
            const auto it = std::find(v.begin(), v.end(), value);
            if (it == v.end()) return;
            *it = v.back();
            v.pop_back();
        }
    }

    // ---- Geometry ---------------------------------------------------------------------------------------------

    bool SpatialRay::Intersect(const AABB& box, double& tEnter) const
    {
        double t0 = 0.0;
        double t1 = maxT;
        for (int a = 0; a < 3; ++a) {
            const double o = origin[a];
            const double d = direction[a];
            if (d == 0.0) {                         // parallel to this slab : inside it or never
                if (o < box.min[a] || o > box.max[a]) return false;
                continue;
            }
            const double inv = 1.0 / d;
            double ta = (box.min[a] - o) * inv;
            double tb = (box.max[a] - o) * inv;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
        tEnter = t0;
        return true;
    }

    AABB SpatialRay::Bounds() const
    {
        AABB b;
        for (int a = 0; a < 3; ++a) {
            const double o = origin[a];
            const double d = direction[a];
            if (d == 0.0) { b.min[a] = o; b.max[a] = o; }
            else if (std::isfinite(maxT)) {
                const double e = o + d * maxT;
                b.min[a] = std::min(o, e);
                b.max[a] = std::max(o, e);
            }
            else if (d > 0.0) { b.min[a] = o; b.max[a] = kInf; }
            else { b.min[a] = -kInf; b.max[a] = o; }
        }
        return b;
    }

    SpatialFrustum SpatialFrustum::FromViewProjection(const DMat4& m, bool zeroToOne)
    {
        // Row i of the matrix (glm is column-major : m[column][row])
        const auto row = [&](int i) { return DVec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
        const DVec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        const DVec4 raw[6] = {
            r3 + r0, r3 - r0, r3 + r1, r3 - r1,
            zeroToOne ? r2 : r3 + r2,
            r3 - r2,
        };
        SpatialFrustum f;
        for (int i = 0; i < 6; ++i) {
            const double len = glm::length(DVec3(raw[i]));
            const double inv = len > 0.0 ? 1.0 / len : 1.0;
            f.planes[i].normal = DVec3(raw[i]) * inv;
            f.planes[i].d = raw[i].w * inv;
        }
        return f;
    }

    bool SpatialFrustum::Intersects(const AABB& box) const
    {
        for (const SpatialPlane& p : planes) {
            // The corner of the box furthest along the normal : if even that one is behind the plane, the box is outside
            double dist = p.d;
            for (int a = 0; a < 3; ++a) {
                const double n = p.normal[a];
                if (n > 0.0) dist += n * box.max[a];
                else if (n < 0.0) dist += n * box.min[a];
            }
            if (dist < 0.0) return false;
        }
        return true;
    }

    // ---- ISpatialIndex queries --------------------------------------------------------------------------------

    void ISpatialIndex::QueryAABB(const AABB& box, Visitor visit) const
    {
        if (!box.IsValid()) return;
        Traverse(box, [&](const AABB& b) { return b.Overlaps(box); }, visit);
    }

    void ISpatialIndex::QuerySphere(const DVec3& center, double radius, Visitor visit) const
    {
        if (!(radius >= 0.0)) return;
        const double r2 = radius * radius;
        Traverse(AABB::FromCenterExtents(center, DVec3(radius)), [&](const AABB& b) { return b.DistanceSquared(center) <= r2; }, visit);
    }

    void ISpatialIndex::QueryFrustum(const SpatialFrustum& frustum, Visitor visit) const
    {
        Traverse(AABB::Infinite(), [&](const AABB& b) { return frustum.Intersects(b); }, visit);
    }

    void ISpatialIndex::QueryAABB(const AABB& box, std::vector<Id>& out) const
    {
        out.clear();
        QueryAABB(box, [&](Id id, const AABB&) { out.push_back(id); return true; });
        std::sort(out.begin(), out.end());
    }

    void ISpatialIndex::QuerySphere(const DVec3& center, double radius, std::vector<Id>& out) const
    {
        out.clear();
        QuerySphere(center, radius, [&](Id id, const AABB&) { out.push_back(id); return true; });
        std::sort(out.begin(), out.end());
    }

    void ISpatialIndex::QueryFrustum(const SpatialFrustum& frustum, std::vector<Id>& out) const
    {
        out.clear();
        QueryFrustum(frustum, [&](Id id, const AABB&) { out.push_back(id); return true; });
        std::sort(out.begin(), out.end());
    }

    void ISpatialIndex::QueryRay(const SpatialRay& ray, std::vector<SpatialRayHit>& out) const
    {
        out.clear();
        if (!(ray.maxT >= 0.0)) return;
        const auto region = [&](const AABB& b) { double t; return ray.Intersect(b, t); };
        Traverse(ray.Bounds(), region, [&](Id id, const AABB& b) {
            double t = 0.0;
            ray.Intersect(b, t);
            out.push_back({id, t});
            return true;
        });
        std::sort(out.begin(), out.end(), [](const SpatialRayHit& a, const SpatialRayHit& b) { return a.t != b.t ? a.t < b.t : a.id < b.id; });
    }

    // ---- BVH --------------------------------------------------------------------------------------------------

    int BVHIndex::AllocNode()
    {
        if (freeList_ >= 0) {
            const int n = freeList_;
            freeList_ = nodes_[n].parent;
            nodes_[n] = Node{};
            return n;
        }
        nodes_.emplace_back();
        return static_cast<int>(nodes_.size()) - 1;
    }

    void BVHIndex::FreeNode(int node)
    {
        nodes_[node] = Node{};
        nodes_[node].parent = freeList_;
        nodes_[node].height = -1;
        freeList_ = node;
    }

    void BVHIndex::InsertLeaf(int leaf)
    {
        nodes_[leaf].parent = -1;
        if (root_ < 0) { root_ = leaf; return; }

        const AABB leafBox = nodes_[leaf].fat;

        // Walk down to the sibling that makes the tree grow the least (surface area heuristic)
        int index = root_;
        while (!nodes_[index].IsLeaf()) {
            const int c1 = nodes_[index].child1;
            const int c2 = nodes_[index].child2;

            const double area = nodes_[index].fat.SurfaceArea();
            const double combinedArea = AABB::Union(nodes_[index].fat, leafBox).SurfaceArea();
            const double cost = 2.0 * combinedArea;                 // new parent over this node
            const double inheritance = 2.0 * (combinedArea - area); // pushing the leaf lower

            const auto childCost = [&](int c) {
                const double grown = AABB::Union(leafBox, nodes_[c].fat).SurfaceArea();
                return nodes_[c].IsLeaf() ? grown + inheritance : grown - nodes_[c].fat.SurfaceArea() + inheritance;
            };
            const double cost1 = childCost(c1);
            const double cost2 = childCost(c2);

            if (cost < cost1 && cost < cost2) break;
            index = cost1 < cost2 ? c1 : c2;
        }

        const int sibling = index;
        const int oldParent = nodes_[sibling].parent;
        const int newParent = AllocNode();      // may reallocate : no reference is held across it
        nodes_[newParent].parent = oldParent;
        nodes_[newParent].fat = AABB::Union(leafBox, nodes_[sibling].fat);
        nodes_[newParent].height = nodes_[sibling].height + 1;
        nodes_[newParent].child1 = sibling;
        nodes_[newParent].child2 = leaf;
        nodes_[sibling].parent = newParent;
        nodes_[leaf].parent = newParent;

        if (oldParent >= 0) {
            if (nodes_[oldParent].child1 == sibling) nodes_[oldParent].child1 = newParent;
            else nodes_[oldParent].child2 = newParent;
        }
        else {
            root_ = newParent;
        }

        Refit(nodes_[leaf].parent);
    }

    void BVHIndex::Refit(int index)
    {
        while (index >= 0) {
            index = Balance(index);
            Node& n = nodes_[index];
            const Node& a = nodes_[n.child1];
            const Node& b = nodes_[n.child2];
            n.height = 1 + std::max(a.height, b.height);
            n.fat = AABB::Union(a.fat, b.fat);
            index = n.parent;
        }
    }

    void BVHIndex::RemoveLeaf(int leaf)
    {
        if (leaf == root_) { root_ = -1; return; }

        const int parent = nodes_[leaf].parent;
        const int grand = nodes_[parent].parent;
        const int sibling = nodes_[parent].child1 == leaf ? nodes_[parent].child2 : nodes_[parent].child1;

        if (grand >= 0) {
            if (nodes_[grand].child1 == parent) nodes_[grand].child1 = sibling;
            else nodes_[grand].child2 = sibling;
            nodes_[sibling].parent = grand;
            FreeNode(parent);
            Refit(grand);
        }
        else {
            root_ = sibling;
            nodes_[sibling].parent = -1;
            FreeNode(parent);
        }
    }

    // Rotates a node up when its children's heights differ by more than 1 (returns the new root of that subtree)
    int BVHIndex::Balance(int iA)
    {
        if (nodes_[iA].IsLeaf() || nodes_[iA].height < 2) return iA;

        const int iB = nodes_[iA].child1;
        const int iC = nodes_[iA].child2;
        const int balance = nodes_[iC].height - nodes_[iB].height;

        // `up` takes A's place, A becomes its child1 ; `other` is up's child that stays with `up`, the two grandchildren are
        // split between A and up so that the taller one stays under up
        const auto rotate = [&](int up, int stay, bool upIsChild2) {
            const int iF = nodes_[up].child1;
            const int iG = nodes_[up].child2;
            nodes_[up].child1 = iA;
            nodes_[up].parent = nodes_[iA].parent;
            nodes_[iA].parent = up;

            const int gp = nodes_[up].parent;
            if (gp >= 0) {
                if (nodes_[gp].child1 == iA) nodes_[gp].child1 = up;
                else nodes_[gp].child2 = up;
            }
            else {
                root_ = up;
            }

            const bool keepF = nodes_[iF].height > nodes_[iG].height;
            const int stays = keepF ? iF : iG;      // remains a child of `up`
            const int moves = keepF ? iG : iF;      // goes down to A
            nodes_[up].child2 = stays;
            if (upIsChild2) nodes_[iA].child2 = moves; else nodes_[iA].child1 = moves;
            nodes_[moves].parent = iA;

            const int aOther = stay;                // A's remaining original child
            nodes_[iA].fat = AABB::Union(nodes_[aOther].fat, nodes_[moves].fat);
            nodes_[up].fat = AABB::Union(nodes_[iA].fat, nodes_[stays].fat);
            nodes_[iA].height = 1 + std::max(nodes_[aOther].height, nodes_[moves].height);
            nodes_[up].height = 1 + std::max(nodes_[iA].height, nodes_[stays].height);
            return up;
        };

        if (balance > 1) return rotate(iC, iB, true);
        if (balance < -1) return rotate(iB, iC, false);
        return iA;
    }

    bool BVHIndex::Insert(Id id, const AABB& box)
    {
        if (!box.IsFinite()) return false;
        if (leaves_.count(id)) return Update(id, box);

        const int leaf = AllocNode();
        nodes_[leaf].id = id;
        nodes_[leaf].tight = box;
        nodes_[leaf].fat = box.Inflated(margin_);
        nodes_[leaf].height = 0;
        leaves_[id] = leaf;
        InsertLeaf(leaf);
        return true;
    }

    bool BVHIndex::Update(Id id, const AABB& box)
    {
        const auto it = leaves_.find(id);
        if (it == leaves_.end() || !box.IsFinite()) return false;

        const int leaf = it->second;
        nodes_[leaf].tight = box;
        if (nodes_[leaf].fat.Contains(box)) return true;        // still inside its fat box : the tree is untouched

        RemoveLeaf(leaf);
        nodes_[leaf].fat = box.Inflated(margin_);
        nodes_[leaf].child1 = nodes_[leaf].child2 = -1;
        InsertLeaf(leaf);
        return true;
    }

    bool BVHIndex::Remove(Id id)
    {
        const auto it = leaves_.find(id);
        if (it == leaves_.end()) return false;
        RemoveLeaf(it->second);
        FreeNode(it->second);
        leaves_.erase(it);
        return true;
    }

    void BVHIndex::Clear()
    {
        nodes_.clear();
        leaves_.clear();
        root_ = -1;
        freeList_ = -1;
    }

    bool BVHIndex::GetBox(Id id, AABB& out) const
    {
        const auto it = leaves_.find(id);
        if (it == leaves_.end()) return false;
        out = nodes_[it->second].tight;
        return true;
    }

    bool BVHIndex::Traverse(const AABB&, RegionTest region, Visitor visit) const
    {
        if (root_ < 0) return true;
        std::vector<int> stack;
        stack.reserve(64);
        stack.push_back(root_);
        while (!stack.empty()) {
            const Node& n = nodes_[stack.back()];
            stack.pop_back();
            if (!region(n.fat)) continue;
            if (n.IsLeaf()) {
                if (region(n.tight) && !visit(n.id, n.tight)) return false;
            }
            else {
                stack.push_back(n.child1);
                stack.push_back(n.child2);
            }
        }
        return true;
    }

    bool BVHIndex::ValidateNode(int index, int parent) const
    {
        const Node& n = nodes_[index];
        if (n.parent != parent) return false;
        if (n.IsLeaf()) return n.height == 0 && n.fat.Contains(n.tight);
        if (n.child1 < 0 || n.child2 < 0) return false;
        const Node& a = nodes_[n.child1];
        const Node& b = nodes_[n.child2];
        if (n.height != 1 + std::max(a.height, b.height)) return false;
        if (!n.fat.Contains(a.fat) || !n.fat.Contains(b.fat)) return false;
        if (std::abs(a.height - b.height) > 1) return false;
        return ValidateNode(n.child1, index) && ValidateNode(n.child2, index);
    }

    bool BVHIndex::Validate() const
    {
        if (root_ < 0) return leaves_.empty();
        return ValidateNode(root_, -1);
    }

    // ---- Grid -------------------------------------------------------------------------------------------------

    size_t GridIndex::CellKeyHash::operator()(const CellKey& k) const noexcept
    {
        uint64_t h = static_cast<uint64_t>(k.x) * 0x9E3779B97F4A7C15ull;
        h ^= (static_cast<uint64_t>(k.y) + 0x7F4A7C15ull + (h << 6) + (h >> 2)) * 0xC2B2AE3D27D4EB4Full;
        h ^= (static_cast<uint64_t>(k.z) + 0x165667B1ull + (h << 6) + (h >> 2)) * 0x85EBCA77C2B2AE63ull;
        h ^= h >> 29;
        return static_cast<size_t>(h);
    }

    GridIndex::CellKey GridIndex::KeyOf(const DVec3& p) const
    {
        // Clamped so a huge coordinate cannot overflow the integer
        constexpr double kLimit = 4.0e18;
        const auto f = [&](double v) { return static_cast<int64_t>(std::clamp(std::floor(v / cellSize_), -kLimit, kLimit)); };
        return {f(p.x), f(p.y), f(p.z)};
    }

    AABB GridIndex::CellBox(const CellKey& k) const
    {
        const DVec3 lo(double(k.x) * cellSize_, double(k.y) * cellSize_, double(k.z) * cellSize_);
        return AABB(lo, lo + DVec3(cellSize_));
    }

    double GridIndex::CellsCovered(const CellKey& lo, const CellKey& hi)
    {
        return (double(hi.x) - double(lo.x) + 1.0) * (double(hi.y) - double(lo.y) + 1.0) * (double(hi.z) - double(lo.z) + 1.0);
    }

    void GridIndex::AddToCells(Id id, const Item& item)
    {
        if (item.oversized) { oversized_.push_back(id); return; }
        for (int64_t z = item.lo.z; z <= item.hi.z; ++z)
            for (int64_t y = item.lo.y; y <= item.hi.y; ++y)
                for (int64_t x = item.lo.x; x <= item.hi.x; ++x)
                    cells_[CellKey{x, y, z}].push_back(id);
    }

    void GridIndex::RemoveFromCells(Id id, const Item& item)
    {
        if (item.oversized) { SwapPop(oversized_, id); return; }
        for (int64_t z = item.lo.z; z <= item.hi.z; ++z)
            for (int64_t y = item.lo.y; y <= item.hi.y; ++y)
                for (int64_t x = item.lo.x; x <= item.hi.x; ++x) {
                    const auto it = cells_.find(CellKey{x, y, z});
                    if (it == cells_.end()) continue;
                    SwapPop(it->second, id);
                    if (it->second.empty()) cells_.erase(it);
                }
    }

    bool GridIndex::Insert(Id id, const AABB& box)
    {
        if (!box.IsFinite()) return false;
        if (items_.count(id)) Remove(id);

        Item item;
        item.box = box;
        item.lo = KeyOf(box.min);
        item.hi = KeyOf(box.max);
        item.oversized = CellsCovered(item.lo, item.hi) > kMaxCellsPerItem;
        AddToCells(id, item);
        items_[id] = item;
        extents_.Expand(box);
        return true;
    }

    bool GridIndex::Update(Id id, const AABB& box)
    {
        const auto it = items_.find(id);
        if (it == items_.end() || !box.IsFinite()) return false;

        Item& item = it->second;
        const CellKey lo = KeyOf(box.min);
        const CellKey hi = KeyOf(box.max);
        const bool oversized = CellsCovered(lo, hi) > kMaxCellsPerItem;
        if (oversized == item.oversized && (oversized || (lo == item.lo && hi == item.hi))) {
            item.box = box;                     // same cells : only the box changes
            extents_.Expand(box);
            return true;
        }
        Remove(id);
        return Insert(id, box);
    }

    bool GridIndex::Remove(Id id)
    {
        const auto it = items_.find(id);
        if (it == items_.end()) return false;
        RemoveFromCells(id, it->second);
        items_.erase(it);
        if (items_.empty()) extents_ = AABB();
        return true;
    }

    void GridIndex::Clear()
    {
        cells_.clear();
        items_.clear();
        oversized_.clear();
        extents_ = AABB();
    }

    bool GridIndex::GetBox(Id id, AABB& out) const
    {
        const auto it = items_.find(id);
        if (it == items_.end()) return false;
        out = it->second.box;
        return true;
    }

    bool GridIndex::Traverse(const AABB& bounds, RegionTest region, Visitor visit) const
    {
        for (const Id id : oversized_) {
            const AABB& box = items_.at(id).box;
            if (region(box) && !visit(id, box)) return false;
        }
        if (items_.empty() || !bounds.IsValid() || !bounds.Overlaps(extents_)) return true;

        AABB clip;
        clip.min = glm::max(bounds.min, extents_.min);
        clip.max = glm::min(bounds.max, extents_.max);
        const CellKey lo = KeyOf(clip.min);
        const CellKey hi = KeyOf(clip.max);

        // An item in several cells is met once per cell : remember the ones already handled
        std::unordered_set<Id> seen;
        bool stopped = false;
        const auto visitCell = [&](const CellKey& key, const std::vector<Id>& ids) {
            if (!region(CellBox(key))) return;
            for (const Id id : ids) {
                const Item& item = items_.at(id);
                if (!(item.lo == item.hi) && !seen.insert(id).second) continue;
                if (region(item.box) && !visit(id, item.box)) { stopped = true; return; }
            }
        };

        if (CellsCovered(lo, hi) > double(cells_.size())) {
            // Fewer occupied cells than cells in the region : walk the occupied ones
            for (const auto& [key, ids] : cells_) {
                if (key.x < lo.x || key.x > hi.x || key.y < lo.y || key.y > hi.y || key.z < lo.z || key.z > hi.z) continue;
                visitCell(key, ids);
                if (stopped) return false;
            }
            return true;
        }

        for (int64_t z = lo.z; z <= hi.z; ++z)
            for (int64_t y = lo.y; y <= hi.y; ++y)
                for (int64_t x = lo.x; x <= hi.x; ++x) {
                    const CellKey key{x, y, z};
                    const auto it = cells_.find(key);
                    if (it == cells_.end()) continue;
                    visitCell(key, it->second);
                    if (stopped) return false;
                }
        return true;
    }

    // ---- Octree -----------------------------------------------------------------------------------------------

    OctreeIndex::OctreeIndex(const DVec3& center, double halfSize, uint32_t maxDepth)
        : rootSize_((halfSize > 0.0 ? halfSize : 1.0) * 2.0)
        , maxDepth_(std::min(maxDepth, kMaxDepthLimit))
    {
        rootMin_ = center - DVec3(rootSize_ * 0.5);
        Alloc();                                // the root, always node 0
    }

    int OctreeIndex::Alloc()
    {
        if (!freeNodes_.empty()) {
            const int n = freeNodes_.back();
            freeNodes_.pop_back();
            nodes_[n] = Node{};
            return n;
        }
        nodes_.emplace_back();
        return static_cast<int>(nodes_.size()) - 1;
    }

    void OctreeIndex::Release(int node)
    {
        nodes_[node] = Node{};
        freeNodes_.push_back(node);
    }

    void OctreeIndex::Recompute(int node)
    {
        Node& n = nodes_[node];
        AABB b;
        for (const Id id : n.items) b.Expand(items_.at(id).box);
        for (const int32_t c : n.children)
            if (c >= 0) b.Expand(nodes_[c].bounds);
        n.bounds = b;
    }

    void OctreeIndex::Place(const AABB& box, Item& item) const
    {
        item.box = box;
        const DVec3 rel = (box.Center() - rootMin_) / rootSize_;
        if (!(rel.x >= 0.0 && rel.x < 1.0 && rel.y >= 0.0 && rel.y < 1.0 && rel.z >= 0.0 && rel.z < 1.0)) {
            item.outside = true;
            return;
        }
        item.outside = false;

        // Deepest level whose cells are still at least as big as the item : the item then fits in its (loose) node
        const DVec3 size = box.max - box.min;
        const double extent = std::max({size.x, size.y, size.z});
        uint32_t depth = maxDepth_;
        if (extent > 0.0) {
            const double ratio = rootSize_ / extent;
            depth = ratio < 2.0 ? 0u : static_cast<uint32_t>(std::min(std::floor(std::log2(ratio)), double(maxDepth_)));
        }
        item.depth = depth;
        const double n = double(1u << depth);
        const uint32_t last = (1u << depth) - 1u;
        item.ix = std::min(last, static_cast<uint32_t>(rel.x * n));
        item.iy = std::min(last, static_cast<uint32_t>(rel.y * n));
        item.iz = std::min(last, static_cast<uint32_t>(rel.z * n));
    }

    uint32_t OctreeIndex::OctantAt(const Item& item, uint32_t level)
    {
        const uint32_t shift = item.depth - level;
        return ((item.ix >> shift) & 1u) | (((item.iy >> shift) & 1u) << 1) | (((item.iz >> shift) & 1u) << 2);
    }

    size_t OctreeIndex::PathTo(const Item& item, int* path) const
    {
        int node = 0;
        size_t n = 0;
        path[n++] = node;
        for (uint32_t level = 1; level <= item.depth; ++level) {
            node = nodes_[node].children[OctantAt(item, level)];
            path[n++] = node;
        }
        return n;
    }

    void OctreeIndex::Link(Id id, const Item& item)
    {
        if (item.outside) { outside_.push_back(id); return; }
        int node = 0;
        nodes_[0].count++;
        nodes_[0].bounds.Expand(item.box);
        for (uint32_t level = 1; level <= item.depth; ++level) {
            const uint32_t oct = OctantAt(item, level);
            int child = nodes_[node].children[oct];
            if (child < 0) {
                child = Alloc();                // may reallocate nodes_
                nodes_[child].parent = node;
                nodes_[child].octant = static_cast<uint8_t>(oct);
                nodes_[node].children[oct] = child;
            }
            node = child;
            nodes_[node].count++;
            nodes_[node].bounds.Expand(item.box);
        }
        nodes_[node].items.push_back(id);
    }

    void OctreeIndex::Unlink(Id id, const Item& item)
    {
        if (item.outside) { SwapPop(outside_, id); return; }
        int path[kMaxDepthLimit + 2];
        const size_t n = PathTo(item, path);
        SwapPop(nodes_[path[n - 1]].items, id);
        for (size_t i = n; i-- > 0;) {
            const int node = path[i];
            nodes_[node].count--;
            if (i > 0 && nodes_[node].count == 0) {
                nodes_[path[i - 1]].children[nodes_[node].octant] = -1;
                Release(node);
            }
            else {
                Recompute(node);
            }
        }
    }

    bool OctreeIndex::Insert(Id id, const AABB& box)
    {
        if (!box.IsFinite()) return false;
        if (items_.count(id)) Remove(id);
        Item item;
        Place(box, item);
        // The node bounds are recomputed from items_, so the item has to be there before it is linked
        items_[id] = item;
        Link(id, item);
        return true;
    }

    bool OctreeIndex::Update(Id id, const AABB& box)
    {
        const auto it = items_.find(id);
        if (it == items_.end() || !box.IsFinite()) return false;

        Item moved;
        Place(box, moved);
        Item& item = it->second;
        const bool sameNode = moved.outside == item.outside
            && (moved.outside || (moved.depth == item.depth && moved.ix == item.ix && moved.iy == item.iy && moved.iz == item.iz));
        if (sameNode) {
            item.box = box;
            if (!item.outside) {
                int path[kMaxDepthLimit + 2];
                const size_t n = PathTo(item, path);
                for (size_t i = n; i-- > 0;) Recompute(path[i]);
            }
            return true;
        }
        Remove(id);
        return Insert(id, box);
    }

    bool OctreeIndex::Remove(Id id)
    {
        const auto it = items_.find(id);
        if (it == items_.end()) return false;
        const Item item = it->second;
        // Unlink recomputes the bounds from items_ : the removed item must no longer be counted... it is only in the node's list until
        // the SwapPop, which comes first
        Unlink(id, item);
        items_.erase(it);
        return true;
    }

    void OctreeIndex::Clear()
    {
        nodes_.clear();
        freeNodes_.clear();
        items_.clear();
        outside_.clear();
        Alloc();
    }

    bool OctreeIndex::GetBox(Id id, AABB& out) const
    {
        const auto it = items_.find(id);
        if (it == items_.end()) return false;
        out = it->second.box;
        return true;
    }

    bool OctreeIndex::Traverse(const AABB&, RegionTest region, Visitor visit) const
    {
        for (const Id id : outside_) {
            const AABB& box = items_.at(id).box;
            if (region(box) && !visit(id, box)) return false;
        }

        int stack[8 * kMaxDepthLimit + 8];
        int top = 0;
        if (nodes_[0].count > 0) stack[top++] = 0;
        while (top > 0) {
            const Node& n = nodes_[stack[--top]];
            if (!region(n.bounds)) continue;
            for (const Id id : n.items) {
                const AABB& box = items_.at(id).box;
                if (region(box) && !visit(id, box)) return false;
            }
            for (const int32_t c : n.children)
                if (c >= 0) stack[top++] = c;
        }
        return true;
    }

    // ---- Factory ----------------------------------------------------------------------------------------------

    std::unique_ptr<ISpatialIndex> CreateSpatialIndex(const SpatialIndexConfig& config)
    {
        switch (config.kind) {
            case SpatialIndexKind::Grid:   return std::make_unique<GridIndex>(config.gridCellSize);
            case SpatialIndexKind::Octree: return std::make_unique<OctreeIndex>(config.octreeCenter, config.octreeHalfSize, config.octreeMaxDepth);
            case SpatialIndexKind::BVH:
            default:                       return std::make_unique<BVHIndex>(config.bvhMargin);
        }
    }
}
