#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeID.h>

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "engine/assets/assetID.hpp"

namespace Shard::Engine::Objects { class Actor; }

namespace Shard::Engine::Physics
{
    enum class MeshApproximation : int {
        Auto = 0,
        Exact,
        Hull,
        Hulls,
        Bounds
    };

    struct TriangleSoup
    {
        std::vector<glm::vec3> positions;
        std::vector<uint32_t> indices;
        std::vector<uint32_t> triangleSubmesh;
        uint32_t submeshCount = 0;

        size_t TriangleCount() const { return indices.size() / 3; }
    };

    struct MeshSource
    {
        std::function<bool(Filesystem::AssetID, TriangleSoup&)> load;
        std::function<std::string(Filesystem::AssetID)> path;
        std::function<uint64_t(Filesystem::AssetID)> stamp;
        std::function<Filesystem::AssetID(Objects::Actor&)> modelMesh;

        explicit operator bool() const { return bool(load); }
    };

    struct MeshColliderSettings
    {
        Filesystem::AssetID mesh;
        MeshApproximation approximation = MeshApproximation::Auto;
        uint64_t submeshMask = ~0ull;
        float convexRadius = 0.02f;
        float concavity = 0.05f;
        int maxHulls = 16;
        int maxHullVertices = 64;
        float edgeAngle = 5.0f;
    };

    struct MeshColliderResult
    {
        JPH::ShapeRefC shape;
        MeshApproximation used = MeshApproximation::Exact;
        bool refining = false;
        std::string error;
    };

    MeshApproximation ResolveApproximation(MeshApproximation requested, bool dynamic);
    const char* ApproximationName(MeshApproximation approximation);
    uint64_t ParseSubmeshMask(const std::string& list);
    uint64_t MeshColliderKeyHash(const MeshColliderSettings& settings, MeshApproximation resolved);

    JPH::ShapeRefC BuildMeshShape(const TriangleSoup& soup, const MeshColliderSettings& settings, MeshApproximation resolved, std::string* error);
    std::vector<uint8_t> SerializeMeshShape(const JPH::Shape& shape);
    JPH::ShapeRefC DeserializeMeshShape(const uint8_t* bytes, size_t size);

    struct BakeReport
    {
        int built = 0;
        int failed = 0;
        std::string error;
    };

    BakeReport BakeMeshCollision(const TriangleSoup& soup, uint64_t stamp, const std::string& sourcePath, bool hulls);

    bool ExtractShapeTriangles(const JPH::Shape& shape, std::vector<glm::vec3>& vertices, size_t maxTriangles);

    bool MeshSubmeshAt(const JPH::Shape& shape, JPH::SubShapeID subShape, uint32_t& submesh);

    namespace CookedCollision
    {
        constexpr uint32_t kVersion = 1;

        struct File
        {
            uint64_t stamp = 0;
            std::unordered_map<uint64_t, std::vector<uint8_t>> entries;
        };

        std::string PathFor(const std::string& sourcePath);
        bool Read(const std::string& path, File& out);
        bool Write(const std::string& path, const File& file);
        bool Store(const std::string& sourcePath, uint64_t stamp, uint64_t key, const std::vector<uint8_t>& bytes);
    }

    class MeshColliderCache
    {
        public:
            MeshColliderCache();
            ~MeshColliderCache();

            void SetSource(MeshSource source) { m_Source = std::move(source); }
            const MeshSource& Source() const { return m_Source; }
            void SetWriteCooked(bool enabled) { m_WriteCooked = enabled; }
            void SetAsync(bool enabled) { m_Async = enabled; }

            MeshColliderResult Acquire(const MeshColliderSettings& settings, bool dynamic);

            void Poll();
            void WaitIdle();
            void Invalidate(Filesystem::AssetID asset);
            void Clear();

            uint64_t Generation() const { return m_Generation; }
            size_t Size() const { return m_Shapes.size(); }
            size_t PendingJobs() const { return m_Jobs.size(); }

        private:
            struct Entry
            {
                JPH::ShapeRefC shape;
                int asset = 0;
            };

            struct Job
            {
                uint64_t composite = 0;
                uint64_t key = 0;
                int asset = 0;
                uint64_t stamp = 0;
                std::string cookedSource;
                std::future<JPH::ShapeRefC> future;
            };

            bool LoadSoup(Filesystem::AssetID asset, std::shared_ptr<const TriangleSoup>& out, std::string& error);
            JPH::ShapeRefC LoadCooked(Filesystem::AssetID asset, uint64_t key, uint64_t& stamp, std::string& cookedSource);
            void Store(const Job& job, const JPH::Shape& shape);

            MeshSource m_Source;
            bool m_WriteCooked = false;
            bool m_Async = true;
            uint64_t m_Generation = 0;
            std::unordered_map<uint64_t, Entry> m_Shapes;
            std::vector<Job> m_Jobs;
            std::unordered_map<uint64_t, int> m_Failed;
            int m_SoupAsset = 0;
            uint64_t m_SoupStamp = 0;
            std::shared_ptr<const TriangleSoup> m_Soup;
    };
}
