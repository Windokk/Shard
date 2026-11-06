#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "engine/core/math/math.hpp"

namespace Shard::Engine::Core {

    // Default box is empty (min = +inf, max = -inf): expanding it by anything gives that thing.
    struct AABB {
        DVec3 min = DVec3(std::numeric_limits<double>::infinity());
        DVec3 max = DVec3(-std::numeric_limits<double>::infinity());

        AABB() = default;
        AABB(const DVec3& lo, const DVec3& hi) : min(lo), max(hi) {}

        static AABB FromCenterExtents(const DVec3& center, const DVec3& halfExtents) { return AABB(center - halfExtents, center + halfExtents); }
        static AABB Infinite() {
            const double inf = std::numeric_limits<double>::infinity();
            return AABB(DVec3(-inf), DVec3(inf));
        }

        bool IsValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
        bool IsFinite() const {
            return IsValid() && std::isfinite(min.x) && std::isfinite(min.y) && std::isfinite(min.z)
                && std::isfinite(max.x) && std::isfinite(max.y) && std::isfinite(max.z);
        }

        DVec3 Center() const { return (min + max) * 0.5; }
        DVec3 HalfExtents() const { return (max - min) * 0.5; }

        void Expand(const DVec3& p) { min = glm::min(min, p); max = glm::max(max, p); }
        void Expand(const AABB& o) { min = glm::min(min, o.min); max = glm::max(max, o.max); }
        AABB Inflated(double margin) const { return AABB(min - DVec3(margin), max + DVec3(margin)); }

        static AABB Union(const AABB& a, const AABB& b) { AABB r = a; r.Expand(b); return r; }

        bool Overlaps(const AABB& o) const {
            return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y && min.z <= o.max.z && max.z >= o.min.z;
        }
        bool Contains(const AABB& o) const {
            return min.x <= o.min.x && max.x >= o.max.x && min.y <= o.min.y && max.y >= o.max.y && min.z <= o.min.z && max.z >= o.max.z;
        }
        bool Contains(const DVec3& p) const {
            return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
        }

        // Half-area is what the SAH uses, the factor 2 keeps it the real area.
        double SurfaceArea() const {
            if (!IsValid()) return 0.0;
            const DVec3 d = max - min;
            return 2.0 * (d.x * d.y + d.y * d.z + d.z * d.x);
        }

        double DistanceSquared(const DVec3& p) const {
            const DVec3 q = glm::max(min - p, glm::max(p - max, DVec3(0.0)));
            return glm::dot(q, q);
        }
    };

    struct SpatialRay {
        DVec3 origin = DVec3(0.0);
        DVec3 direction = DVec3(0.0, 0.0, 1.0);
        double maxT = std::numeric_limits<double>::infinity();

        bool Intersect(const AABB& box, double& tEnter) const;
        AABB Bounds() const;
    };

    struct SpatialPlane {
        DVec3 normal = DVec3(0.0, 1.0, 0.0);
        double d = 0.0;
    };

    struct SpatialFrustum {
        std::array<SpatialPlane, 6> planes;

        // Gribb-Hartmann plane extraction. `zeroToOne` for clip depth in [0, 1] (Vulkan, D3D).
        static SpatialFrustum FromViewProjection(const DMat4& viewProjection, bool zeroToOne = false);
        // Conservative: a box near a frustum corner can be a false positive.
        bool Intersects(const AABB& box) const;
    };

    template <typename Sig> class FunctionRef;
    template <typename R, typename... A>
    class FunctionRef<R(A...)> {
    public:
        template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, FunctionRef>>>
        FunctionRef(F&& f)
            : obj_(const_cast<void*>(static_cast<const void*>(std::addressof(f))))
            , call_([](void* o, A... a) -> R { return (*static_cast<std::remove_reference_t<F>*>(o))(std::forward<A>(a)...); }) {}
        R operator()(A... a) const { return call_(obj_, std::forward<A>(a)...); }
    private:
        void* obj_;
        R (*call_)(void*, A...);
    };

    enum class SpatialIndexKind : uint8_t { BVH, Grid, Octree };

    struct SpatialRayHit {
        uint64_t id = 0;
        double t = 0.0;
    };

    // Returns candidates by box only. Not thread-safe (const queries may run together). Visitor order unspecified, vector overloads sort.
    class ISpatialIndex {
    public:
        using Id = uint64_t;
        using Visitor = FunctionRef<bool(Id, const AABB&)>;

        virtual ~ISpatialIndex() = default;

        virtual bool Insert(Id id, const AABB& box) = 0;
        virtual bool Update(Id id, const AABB& box) = 0;
        virtual bool Remove(Id id) = 0;
        virtual void Clear() = 0;
        virtual size_t Size() const = 0;
        virtual bool Contains(Id id) const = 0;
        virtual bool GetBox(Id id, AABB& out) const = 0;
        virtual SpatialIndexKind Kind() const = 0;

        void QueryAABB(const AABB& box, Visitor visit) const;
        void QuerySphere(const DVec3& center, double radius, Visitor visit) const;
        void QueryFrustum(const SpatialFrustum& frustum, Visitor visit) const;

        void QueryAABB(const AABB& box, std::vector<Id>& out) const;
        void QuerySphere(const DVec3& center, double radius, std::vector<Id>& out) const;
        void QueryFrustum(const SpatialFrustum& frustum, std::vector<Id>& out) const;
        void QueryRay(const SpatialRay& ray, std::vector<SpatialRayHit>& out) const;

    protected:
        using RegionTest = FunctionRef<bool(const AABB&)>;
        // `region` must never say false for a node box if an item inside could pass. Returns false if the visitor stopped.
        virtual bool Traverse(const AABB& bounds, RegionTest region, Visitor visit) const = 0;
    };

    // Fat boxes (real box grown by `margin`): small moves leave the tree alone, bigger ones re-insert (SAH descent, AVL rotations).
    class BVHIndex final : public ISpatialIndex {
    public:
        explicit BVHIndex(double margin = 0.25) : margin_(margin < 0.0 ? 0.0 : margin) {}

        bool Insert(Id id, const AABB& box) override;
        bool Update(Id id, const AABB& box) override;
        bool Remove(Id id) override;
        void Clear() override;
        size_t Size() const override { return leaves_.size(); }
        bool Contains(Id id) const override { return leaves_.count(id) != 0; }
        bool GetBox(Id id, AABB& out) const override;
        SpatialIndexKind Kind() const override { return SpatialIndexKind::BVH; }

        int Height() const { return root_ < 0 ? 0 : nodes_[root_].height; }
        bool Validate() const;

    protected:
        bool Traverse(const AABB& bounds, RegionTest region, Visitor visit) const override;

    private:
        struct Node {
            AABB fat;
            AABB tight;
            Id id = 0;
            int parent = -1;
            int child1 = -1;
            int child2 = -1;
            int height = 0;
            bool IsLeaf() const { return child1 < 0; }
        };

        int AllocNode();
        void FreeNode(int node);
        void InsertLeaf(int leaf);
        void RemoveLeaf(int leaf);
        int Balance(int node);
        void Refit(int fromNode);
        bool ValidateNode(int node, int parent) const;

        std::vector<Node> nodes_;
        std::unordered_map<Id, int> leaves_;
        int root_ = -1;
        int freeList_ = -1;
        double margin_;
    };

    class GridIndex final : public ISpatialIndex {
    public:
        explicit GridIndex(double cellSize = 32.0) : cellSize_(cellSize > 0.0 ? cellSize : 32.0) {}

        bool Insert(Id id, const AABB& box) override;
        bool Update(Id id, const AABB& box) override;
        bool Remove(Id id) override;
        void Clear() override;
        size_t Size() const override { return items_.size(); }
        bool Contains(Id id) const override { return items_.count(id) != 0; }
        bool GetBox(Id id, AABB& out) const override;
        SpatialIndexKind Kind() const override { return SpatialIndexKind::Grid; }

        double CellSize() const { return cellSize_; }
        size_t CellCount() const { return cells_.size(); }
        size_t OversizedCount() const { return oversized_.size(); }

        static constexpr double kMaxCellsPerItem = 512.0;

    protected:
        bool Traverse(const AABB& bounds, RegionTest region, Visitor visit) const override;

    private:
        struct CellKey {
            int64_t x = 0, y = 0, z = 0;
            bool operator==(const CellKey& o) const { return x == o.x && y == o.y && z == o.z; }
        };
        struct CellKeyHash {
            size_t operator()(const CellKey& k) const noexcept;
        };
        struct Item {
            AABB box;
            CellKey lo, hi;
            bool oversized = false;
        };

        CellKey KeyOf(const DVec3& p) const;
        AABB CellBox(const CellKey& k) const;
        static double CellsCovered(const CellKey& lo, const CellKey& hi);
        void AddToCells(Id id, const Item& item);
        void RemoveFromCells(Id id, const Item& item);

        std::unordered_map<CellKey, std::vector<Id>, CellKeyHash> cells_;
        std::unordered_map<Id, Item> items_;
        std::vector<Id> oversized_;
        AABB extents_;
        double cellSize_;
    };

    class OctreeIndex final : public ISpatialIndex {
    public:
        static constexpr uint32_t kMaxDepthLimit = 20;

        OctreeIndex(const DVec3& center = DVec3(0.0), double halfSize = 4096.0, uint32_t maxDepth = 8);

        bool Insert(Id id, const AABB& box) override;
        bool Update(Id id, const AABB& box) override;
        bool Remove(Id id) override;
        void Clear() override;
        size_t Size() const override { return items_.size(); }
        bool Contains(Id id) const override { return items_.count(id) != 0; }
        bool GetBox(Id id, AABB& out) const override;
        SpatialIndexKind Kind() const override { return SpatialIndexKind::Octree; }

        size_t NodeCount() const { return nodes_.size() - freeNodes_.size(); }
        size_t OutsideCount() const { return outside_.size(); }

    protected:
        bool Traverse(const AABB& bounds, RegionTest region, Visitor visit) const override;

    private:
        struct Node {
            AABB bounds;
            std::vector<Id> items;
            std::array<int32_t, 8> children;
            int32_t parent = -1;
            uint32_t count = 0;
            uint8_t octant = 0;
            Node() { children.fill(-1); }
        };
        struct Item {
            AABB box;
            uint32_t depth = 0;
            uint32_t ix = 0, iy = 0, iz = 0;
            bool outside = false;
        };

        void Place(const AABB& box, Item& item) const;
        static uint32_t OctantAt(const Item& item, uint32_t level);
        int Alloc();
        void Release(int node);
        void Recompute(int node);
        size_t PathTo(const Item& item, int* path) const;
        void Link(Id id, const Item& item);
        void Unlink(Id id, const Item& item);

        std::vector<Node> nodes_;
        std::vector<int> freeNodes_;
        std::unordered_map<Id, Item> items_;
        std::vector<Id> outside_;
        DVec3 rootMin_;
        double rootSize_;
        uint32_t maxDepth_;
    };

    struct SpatialIndexConfig {
        SpatialIndexKind kind = SpatialIndexKind::BVH;
        double bvhMargin = 0.25;
        double gridCellSize = 32.0;
        DVec3 octreeCenter = DVec3(0.0);
        double octreeHalfSize = 4096.0;
        uint32_t octreeMaxDepth = 8;
    };

    std::unique_ptr<ISpatialIndex> CreateSpatialIndex(const SpatialIndexConfig& config = {});
}
