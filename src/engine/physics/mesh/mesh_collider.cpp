#define GLM_ENABLE_EXPERIMENTAL
#include "mesh_collider.hpp"

#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <coacd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include <glm/gtx/component_wise.hpp>

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Physics
{
    namespace
    {
        constexpr uint32_t kMagic = 0x4C4F4353;
        constexpr size_t kMaxCookedEntryBytes = size_t(1) << 30;
        constexpr size_t kMaxDecomposedTriangles = 150000;

        uint64_t Fnv(uint64_t hash, const void* data, size_t size)
        {
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
            return hash;
        }

        template<class T>
        uint64_t FnvValue(uint64_t hash, T value)
        {
            return Fnv(hash, &value, sizeof(T));
        }

        bool Included(const TriangleSoup& soup, size_t triangle, uint64_t mask)
        {
            const uint32_t submesh = triangle < soup.triangleSubmesh.size() ? soup.triangleSubmesh[triangle] : 0;
            return ((mask >> std::min<uint32_t>(submesh, 63)) & 1ull) != 0;
        }

        struct Picked
        {
            std::vector<uint32_t> triangles;
            std::vector<uint32_t> vertices;
            glm::vec3 boundsMin = glm::vec3(std::numeric_limits<float>::max());
            glm::vec3 boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
        };

        bool Pick(const TriangleSoup& soup, uint64_t mask, Picked& out, std::string* error)
        {
            std::vector<uint8_t> used(soup.positions.size(), 0);
            const size_t count = soup.TriangleCount();
            for (size_t t = 0; t < count; ++t)
            {
                if (!Included(soup, t, mask)) continue;
                const uint32_t a = soup.indices[t * 3], b = soup.indices[t * 3 + 1], c = soup.indices[t * 3 + 2];
                if (a >= used.size() || b >= used.size() || c >= used.size()) continue;
                out.triangles.push_back(uint32_t(t));
                used[a] = used[b] = used[c] = 1;
            }
            for (uint32_t v = 0; v < used.size(); ++v)
            {
                if (!used[v]) continue;
                out.vertices.push_back(v);
                out.boundsMin = glm::min(out.boundsMin, soup.positions[v]);
                out.boundsMax = glm::max(out.boundsMax, soup.positions[v]);
            }
            if (out.triangles.empty())
            {
                if (error) *error = "the mesh has no triangle in the selected submeshes";
                return false;
            }
            return true;
        }

        JPH::Vec3 ToVec(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }

        JPH::ShapeRefC BuildExact(const TriangleSoup& soup, const Picked& picked, const MeshColliderSettings& settings, std::string* error)
        {
            JPH::VertexList vertices;
            vertices.reserve(soup.positions.size());
            for (const glm::vec3& p : soup.positions)
                vertices.push_back(JPH::Float3(p.x, p.y, p.z));

            JPH::IndexedTriangleList triangles;
            triangles.reserve(picked.triangles.size());
            for (uint32_t t : picked.triangles)
            {
                const uint32_t submesh = t < soup.triangleSubmesh.size() ? soup.triangleSubmesh[t] : 0;
                triangles.push_back(JPH::IndexedTriangle(soup.indices[t * 3], soup.indices[t * 3 + 1], soup.indices[t * 3 + 2], 0, submesh));
            }

            JPH::MeshShapeSettings shape(vertices, triangles);
            shape.mActiveEdgeCosThresholdAngle = std::cos(glm::radians(std::clamp(settings.edgeAngle, 0.0f, 90.0f)));
            shape.mPerTriangleUserData = soup.submeshCount > 1;
            auto result = shape.Create();
            if (result.HasError())
            {
                if (error) *error = std::string(result.GetError().c_str());
                return nullptr;
            }
            return result.Get();
        }

        std::vector<glm::vec3> ReducePoints(const std::vector<glm::vec3>& points)
        {
            std::vector<glm::vec3> directions;
            constexpr int kDirections = 160;
            const float golden = 3.14159265f * (3.0f - std::sqrt(5.0f));
            for (int i = 0; i < kDirections; ++i)
            {
                const float y = 1.0f - 2.0f * (float(i) + 0.5f) / float(kDirections);
                const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
                directions.emplace_back(std::cos(golden * float(i)) * r, y, std::sin(golden * float(i)) * r);
            }
            std::vector<glm::vec3> out;
            for (const glm::vec3& d : directions)
            {
                float best = std::numeric_limits<float>::lowest();
                const glm::vec3* pick = nullptr;
                for (const glm::vec3& p : points)
                {
                    const float s = glm::dot(p, d);
                    if (s > best) { best = s; pick = &p; }
                }
                if (pick) out.push_back(*pick);
            }
            return out;
        }

        JPH::ShapeRefC BuildHullFromPoints(const std::vector<glm::vec3>& source, float convexRadius, int maxVertices, std::string* error)
        {
            std::vector<glm::vec3> points = source.size() > 2048 ? ReducePoints(source) : source;
            if (points.size() < 4)
            {
                if (error) *error = "not enough points for a convex hull";
                return nullptr;
            }
            glm::vec3 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
            JPH::Array<JPH::Vec3> joltPoints;
            joltPoints.reserve(points.size());
            for (const glm::vec3& p : points)
            {
                low = glm::min(low, p);
                high = glm::max(high, p);
                joltPoints.push_back(ToVec(p));
            }
            const float extent = std::max({ high.x - low.x, high.y - low.y, high.z - low.z, 1e-6f });
            float tolerance = std::max(1e-3f, extent * 1e-4f);
            const int limit = std::min(maxVertices, int(JPH::ConvexHullShape::cMaxPointsInHull));
            std::string last;
            for (int attempt = 0; attempt < 14; ++attempt)
            {
                JPH::ConvexHullShapeSettings settings(joltPoints, std::max(0.0f, convexRadius));
                settings.mHullTolerance = tolerance;
                auto result = settings.Create();
                if (!result.HasError())
                {
                    const JPH::ConvexHullShape* hull = static_cast<const JPH::ConvexHullShape*>(result.Get().GetPtr());
                    if (int(hull->GetNumPoints()) <= limit)
                        return result.Get();
                }
                else
                {
                    last = std::string(result.GetError().c_str());
                }
                tolerance *= 2.0f;
            }
            if (error) *error = last.empty() ? "the convex hull has too many vertices" : last;
            return nullptr;
        }

        std::vector<glm::vec3> PointsOf(const TriangleSoup& soup, const Picked& picked)
        {
            std::vector<glm::vec3> points;
            points.reserve(picked.vertices.size());
            for (uint32_t v : picked.vertices)
                points.push_back(soup.positions[v]);
            return points;
        }

        JPH::ShapeRefC BuildBounds(const Picked& picked, std::string* error)
        {
            const glm::vec3 half = (picked.boundsMax - picked.boundsMin) * 0.5f;
            const glm::vec3 center = (picked.boundsMax + picked.boundsMin) * 0.5f;
            JPH::BoxShapeSettings box(ToVec(glm::max(half, glm::vec3(1e-3f))), 0.0f);
            auto boxResult = box.Create();
            if (boxResult.HasError())
            {
                if (error) *error = std::string(boxResult.GetError().c_str());
                return nullptr;
            }
            if (glm::dot(center, center) < 1e-12f)
                return boxResult.Get();
            JPH::RotatedTranslatedShapeSettings wrap(ToVec(center), JPH::Quat::sIdentity(), boxResult.Get());
            auto wrapped = wrap.Create();
            if (wrapped.HasError())
            {
                if (error) *error = std::string(wrapped.GetError().c_str());
                return nullptr;
            }
            return wrapped.Get();
        }

        JPH::ShapeRefC BuildHulls(const TriangleSoup& soup, const Picked& picked, const MeshColliderSettings& settings, std::string* error)
        {
            if (picked.triangles.size() > kMaxDecomposedTriangles)
            {
                if (error) *error = "the mesh has too many triangles to be decomposed";
                return nullptr;
            }

            static std::mutex gDecomposition;
            std::lock_guard<std::mutex> lock(gDecomposition);

            coacd::Mesh input;
            std::vector<int> remap(soup.positions.size(), -1);
            {
                const double cell = std::max(double(glm::compMax(picked.boundsMax - picked.boundsMin)), 1e-6) * 1e-5;
                struct Cell { int64_t x, y, z; bool operator==(const Cell& o) const { return x == o.x && y == o.y && z == o.z; } };
                struct CellHash { size_t operator()(const Cell& c) const { return size_t(c.x * 73856093ll) ^ size_t(c.y * 19349663ll) ^ size_t(c.z * 83492791ll); } };
                std::unordered_map<Cell, int, CellHash> welded;
                for (uint32_t v : picked.vertices)
                {
                    const glm::vec3& p = soup.positions[v];
                    const Cell key{ int64_t(std::llround(double(p.x) / cell)), int64_t(std::llround(double(p.y) / cell)), int64_t(std::llround(double(p.z) / cell)) };
                    const auto [it, added] = welded.try_emplace(key, int(input.vertices.size()));
                    if (added)
                        input.vertices.push_back({ double(p.x), double(p.y), double(p.z) });
                    remap[v] = it->second;
                }
            }
            for (uint32_t t : picked.triangles)
            {
                const int a = remap[soup.indices[t * 3]], b = remap[soup.indices[t * 3 + 1]], c = remap[soup.indices[t * 3 + 2]];
                if (a >= 0 && b >= 0 && c >= 0 && a != b && b != c && a != c)
                    input.indices.push_back({ a, b, c });
            }
            if (input.indices.empty())
            {
                if (error) *error = "the mesh has no usable triangle";
                return nullptr;
            }

            std::vector<coacd::Mesh> parts;
            try
            {
                coacd::set_log_level("off");
                parts = coacd::CoACD(input, std::clamp(double(settings.concavity), 0.01, 1.0), std::max(1, settings.maxHulls), "off", 50, 2000, 20, 150, 3,
                                     false, true, false, std::clamp(settings.maxHullVertices, 8, 256), false, 0.01, "ch", 0, false);
            }
            catch (const std::exception& e)
            {
                if (error) *error = std::string("decomposition failed : ") + e.what();
                return nullptr;
            }

            JPH::StaticCompoundShapeSettings compound;
            JPH::ShapeRefC only;
            int built = 0;
            for (const coacd::Mesh& part : parts)
            {
                std::vector<glm::vec3> points;
                points.reserve(part.vertices.size());
                for (const auto& v : part.vertices)
                    points.emplace_back(float(v[0]), float(v[1]), float(v[2]));
                JPH::ShapeRefC hull = BuildHullFromPoints(points, settings.convexRadius, settings.maxHullVertices, nullptr);
                if (!hull) continue;
                only = hull;
                compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), hull.GetPtr());
                ++built;
            }
            if (built == 0)
            {
                if (error) *error = "the decomposition produced no usable hull";
                return nullptr;
            }
            if (built == 1)
                return only;
            auto result = compound.Create();
            if (result.HasError())
            {
                if (error) *error = std::string(result.GetError().c_str());
                return nullptr;
            }
            return result.Get();
        }
    }

    MeshApproximation ResolveApproximation(MeshApproximation requested, bool dynamic)
    {
        if (requested == MeshApproximation::Auto)
            return dynamic ? MeshApproximation::Hulls : MeshApproximation::Exact;
        if (requested == MeshApproximation::Exact && dynamic)
            return MeshApproximation::Hull;
        return requested;
    }

    const char* ApproximationName(MeshApproximation approximation)
    {
        switch (approximation)
        {
            case MeshApproximation::Auto:   return "Auto";
            case MeshApproximation::Exact:  return "Exact";
            case MeshApproximation::Hull:   return "Hull";
            case MeshApproximation::Hulls:  return "Hulls";
            case MeshApproximation::Bounds: return "Bounds";
        }
        return "Auto";
    }

    uint64_t ParseSubmeshMask(const std::string& list)
    {
        uint64_t mask = 0;
        size_t i = 0;
        const size_t n = list.size();
        auto readNumber = [&](int& value) {
            while (i < n && !std::isdigit(static_cast<unsigned char>(list[i])) && list[i] != '-') ++i;
            if (i >= n || !std::isdigit(static_cast<unsigned char>(list[i]))) return false;
            long v = 0;
            while (i < n && std::isdigit(static_cast<unsigned char>(list[i])) && v < 100000) v = v * 10 + (list[i++] - '0');
            value = int(v);
            return true;
        };
        int first = 0;
        while (readNumber(first))
        {
            int last = first;
            if (i < n && list[i] == '-')
            {
                ++i;
                if (!readNumber(last)) last = first;
            }
            for (int s = first; s <= last && s < 64; ++s)
                mask |= 1ull << s;
        }
        return mask == 0 ? ~0ull : mask;
    }

    uint64_t MeshColliderKeyHash(const MeshColliderSettings& settings, MeshApproximation resolved)
    {
        uint64_t hash = 1469598103934665603ull;
        hash = FnvValue(hash, int(resolved));
        hash = FnvValue(hash, settings.submeshMask);
        switch (resolved)
        {
            case MeshApproximation::Exact:
                hash = FnvValue(hash, settings.edgeAngle);
                break;
            case MeshApproximation::Hull:
                hash = FnvValue(hash, settings.convexRadius);
                hash = FnvValue(hash, settings.maxHullVertices);
                break;
            case MeshApproximation::Hulls:
                hash = FnvValue(hash, settings.convexRadius);
                hash = FnvValue(hash, settings.maxHullVertices);
                hash = FnvValue(hash, settings.concavity);
                hash = FnvValue(hash, settings.maxHulls);
                break;
            default:
                break;
        }
        return hash;
    }

    JPH::ShapeRefC BuildMeshShape(const TriangleSoup& soup, const MeshColliderSettings& settings, MeshApproximation resolved, std::string* error)
    {
        if (soup.positions.empty() || soup.indices.size() < 3)
        {
            if (error) *error = "the mesh is empty";
            return nullptr;
        }
        Picked picked;
        if (!Pick(soup, settings.submeshMask, picked, error))
            return nullptr;

        switch (resolved)
        {
            case MeshApproximation::Exact:
                return BuildExact(soup, picked, settings, error);
            case MeshApproximation::Hull:
                return BuildHullFromPoints(PointsOf(soup, picked), settings.convexRadius, settings.maxHullVertices, error);
            case MeshApproximation::Bounds:
                return BuildBounds(picked, error);
            case MeshApproximation::Hulls:
            {
                JPH::ShapeRefC shape = BuildHulls(soup, picked, settings, error);
                if (!shape)
                    return BuildHullFromPoints(PointsOf(soup, picked), settings.convexRadius, settings.maxHullVertices, nullptr);
                return shape;
            }
            case MeshApproximation::Auto:
                break;
        }
        if (error) *error = "the approximation is not resolved";
        return nullptr;
    }

    std::vector<uint8_t> SerializeMeshShape(const JPH::Shape& shape)
    {
        std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
        JPH::StreamOutWrapper out(stream);
        JPH::Shape::ShapeToIDMap shapes;
        JPH::Shape::MaterialToIDMap materials;
        shape.SaveWithChildren(out, shapes, materials);
        if (out.IsFailed())
            return {};
        const std::string bytes = stream.str();
        return std::vector<uint8_t>(bytes.begin(), bytes.end());
    }

    JPH::ShapeRefC DeserializeMeshShape(const uint8_t* bytes, size_t size)
    {
        if (!bytes || size == 0)
            return nullptr;
        std::stringstream stream(std::string(reinterpret_cast<const char*>(bytes), size), std::ios::in | std::ios::binary);
        JPH::StreamInWrapper in(stream);
        JPH::Shape::IDToShapeMap shapes;
        JPH::Shape::IDToMaterialMap materials;
        auto result = JPH::Shape::sRestoreWithChildren(in, shapes, materials);
        if (result.HasError())
            return nullptr;
        return result.Get();
    }

    bool ExtractShapeTriangles(const JPH::Shape& shape, std::vector<glm::vec3>& vertices, size_t maxTriangles)
    {
        constexpr int kBatch = 64;
        const JPH::AABox everything(JPH::Vec3::sReplicate(-1.0e6f), JPH::Vec3::sReplicate(1.0e6f));
        JPH::AllHitCollisionCollector<JPH::TransformedShapeCollector> leaves;
        shape.CollectTransformedShapes(everything, shape.GetCenterOfMass(), JPH::Quat::sIdentity(), JPH::Vec3::sReplicate(1.0f), JPH::SubShapeIDCreator(), leaves, JPH::ShapeFilter());

        JPH::Float3 batch[kBatch * 3];
        const JPH::PhysicsMaterial* materials[kBatch];
        size_t triangles = 0;
        for (const JPH::TransformedShape& leaf : leaves.mHits)
        {
            JPH::Shape::GetTrianglesContext context;
            leaf.GetTrianglesStart(context, everything, JPH::RVec3::sZero());
            for (;;)
            {
                const int count = leaf.GetTrianglesNext(context, kBatch, batch, materials);
                if (count == 0)
                    break;
                for (int t = 0; t < count; ++t)
                {
                    if (triangles >= maxTriangles)
                        return false;
                    for (int v = 0; v < 3; ++v)
                        vertices.emplace_back(batch[t * 3 + v].x, batch[t * 3 + v].y, batch[t * 3 + v].z);
                    ++triangles;
                }
            }
        }
        return true;
    }

    bool MeshSubmeshAt(const JPH::Shape& shape, JPH::SubShapeID subShape, uint32_t& submesh)
    {
        JPH::SubShapeID remainder;
        const JPH::Shape* leaf = shape.GetLeafShape(subShape, remainder);
        if (!leaf || leaf->GetSubType() != JPH::EShapeSubType::Mesh)
            return false;
        submesh = static_cast<const JPH::MeshShape*>(leaf)->GetTriangleUserData(remainder);
        return true;
    }

    BakeReport BakeMeshCollision(const TriangleSoup& soup, uint64_t stamp, const std::string& sourcePath, bool hulls)
    {
        BakeReport report;
        CookedCollision::File file;
        file.stamp = stamp;
        const MeshColliderSettings settings;
        std::vector<MeshApproximation> kinds = { MeshApproximation::Exact, MeshApproximation::Hull };
        if (hulls)
            kinds.push_back(MeshApproximation::Hulls);
        for (MeshApproximation kind : kinds)
        {
            std::string error;
            JPH::ShapeRefC shape = BuildMeshShape(soup, settings, kind, &error);
            const std::vector<uint8_t> bytes = shape ? SerializeMeshShape(*shape) : std::vector<uint8_t>();
            if (bytes.empty())
            {
                ++report.failed;
                if (report.error.empty())
                    report.error = std::string(ApproximationName(kind)) + " : " + (error.empty() ? "cannot serialize" : error);
                continue;
            }
            file.entries[MeshColliderKeyHash(settings, kind)] = bytes;
            ++report.built;
        }
        if (report.built > 0 && !CookedCollision::Write(CookedCollision::PathFor(sourcePath), file))
        {
            report.failed += report.built;
            report.built = 0;
            report.error = "cannot write " + CookedCollision::PathFor(sourcePath);
        }
        return report;
    }

    namespace CookedCollision
    {
        namespace
        {
            uint32_t Environment()
            {
                using JPH::uint64;
                uint32_t flags = uint32_t(JPH_VERSION_ID);
#ifdef JPH_DOUBLE_PRECISION
                flags ^= 0x80000000u;
#endif
                return flags;
            }

            template<class T>
            void Put(std::string& out, T value)
            {
                out.append(reinterpret_cast<const char*>(&value), sizeof(T));
            }

            template<class T>
            bool Take(const std::string& in, size_t& at, T& value)
            {
                if (in.size() - at < sizeof(T)) return false;
                std::memcpy(&value, in.data() + at, sizeof(T));
                at += sizeof(T);
                return true;
            }
        }

        std::string PathFor(const std::string& sourcePath)
        {
            return sourcePath + ".scol";
        }

        bool Read(const std::string& path, File& out)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) return false;
            const std::streamsize size = file.tellg();
            if (size <= 0) return false;
            file.seekg(0);
            std::string bytes(static_cast<size_t>(size), '\0');
            if (!file.read(bytes.data(), size)) return false;

            size_t at = 0;
            uint32_t magic = 0, version = 0, environment = 0, count = 0;
            File result;
            if (!Take(bytes, at, magic) || !Take(bytes, at, version) || !Take(bytes, at, environment) || !Take(bytes, at, result.stamp) || !Take(bytes, at, count))
                return false;
            if (magic != kMagic || version != kVersion || environment != Environment())
                return false;
            for (uint32_t i = 0; i < count; ++i)
            {
                uint64_t key = 0, length = 0;
                if (!Take(bytes, at, key) || !Take(bytes, at, length)) return false;
                if (length > kMaxCookedEntryBytes || length > bytes.size() - at) return false;
                result.entries[key].assign(bytes.begin() + std::ptrdiff_t(at), bytes.begin() + std::ptrdiff_t(at + length));
                at += size_t(length);
            }
            out = std::move(result);
            return true;
        }

        bool Write(const std::string& path, const File& file)
        {
            std::string bytes;
            Put(bytes, kMagic);
            Put(bytes, kVersion);
            Put(bytes, Environment());
            Put(bytes, file.stamp);
            Put(bytes, uint32_t(file.entries.size()));
            for (const auto& [key, data] : file.entries)
            {
                Put(bytes, key);
                Put(bytes, uint64_t(data.size()));
                bytes.append(reinterpret_cast<const char*>(data.data()), data.size());
            }
            const std::string temporary = path + ".tmp";
            {
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                if (!out) return false;
                out.write(bytes.data(), std::streamsize(bytes.size()));
                if (!out) return false;
            }
            std::error_code ec;
            std::filesystem::rename(temporary, path, ec);
            if (ec)
            {
                std::filesystem::remove(temporary, ec);
                return false;
            }
            return true;
        }

        bool Store(const std::string& sourcePath, uint64_t stamp, uint64_t key, const std::vector<uint8_t>& bytes)
        {
            const std::string path = PathFor(sourcePath);
            File file;
            if (!Read(path, file) || file.stamp != stamp)
            {
                file = File();
                file.stamp = stamp;
            }
            file.entries[key] = bytes;
            return Write(path, file);
        }
    }

    MeshColliderCache::MeshColliderCache() = default;

    MeshColliderCache::~MeshColliderCache()
    {
        for (Job& job : m_Jobs)
            if (job.future.valid())
                job.future.wait();
    }

    bool MeshColliderCache::LoadSoup(Filesystem::AssetID asset, std::shared_ptr<const TriangleSoup>& out, std::string& error)
    {
        const uint64_t stamp = m_Source.stamp ? m_Source.stamp(asset) : 0;
        if (m_Soup && m_SoupAsset == asset.GetAsInt() && m_SoupStamp == stamp)
        {
            out = m_Soup;
            return true;
        }
        if (!m_Source)
        {
            error = "no mesh source is registered";
            return false;
        }
        auto soup = std::make_shared<TriangleSoup>();
        if (!m_Source.load(asset, *soup) || soup->positions.empty() || soup->indices.size() < 3)
        {
            error = "the mesh could not be read";
            return false;
        }
        m_Soup = soup;
        m_SoupAsset = asset.GetAsInt();
        m_SoupStamp = stamp;
        out = m_Soup;
        return true;
    }

    JPH::ShapeRefC MeshColliderCache::LoadCooked(Filesystem::AssetID asset, uint64_t key, uint64_t& stamp, std::string& cookedSource)
    {
        if (!m_Source.path)
            return nullptr;
        cookedSource = m_Source.path(asset);
        if (cookedSource.empty())
            return nullptr;
        stamp = m_Source.stamp ? m_Source.stamp(asset) : 0;
        CookedCollision::File file;
        if (!CookedCollision::Read(CookedCollision::PathFor(cookedSource), file))
            return nullptr;
        if (stamp != 0 && file.stamp != 0 && file.stamp != stamp)
            return nullptr;
        const auto it = file.entries.find(key);
        if (it == file.entries.end())
            return nullptr;
        return DeserializeMeshShape(it->second.data(), it->second.size());
    }

    void MeshColliderCache::Store(const Job& job, const JPH::Shape& shape)
    {
        if (!m_WriteCooked || job.cookedSource.empty())
            return;
        const std::vector<uint8_t> bytes = SerializeMeshShape(shape);
        if (!bytes.empty() && !CookedCollision::Store(job.cookedSource, job.stamp, job.key, bytes))
            DEBUG_WARNING("Mesh collider : cannot write " + CookedCollision::PathFor(job.cookedSource));
    }

    MeshColliderResult MeshColliderCache::Acquire(const MeshColliderSettings& settings, bool dynamic)
    {
        MeshColliderResult result;
        result.used = ResolveApproximation(settings.approximation, dynamic);
        if (settings.mesh.GetAsInt() == 0)
        {
            result.error = "no mesh is set";
            return result;
        }

        const uint64_t key = MeshColliderKeyHash(settings, result.used);
        const uint64_t composite = Fnv(key, &settings.mesh, sizeof(settings.mesh));

        const auto found = m_Shapes.find(composite);
        if (found != m_Shapes.end())
        {
            result.shape = found->second.shape;
            return result;
        }

        for (const Job& job : m_Jobs)
        {
            if (job.composite != composite) continue;
            MeshColliderSettings fallback = settings;
            fallback.approximation = MeshApproximation::Hull;
            MeshColliderResult hull = Acquire(fallback, true);
            hull.refining = true;
            return hull;
        }

        if (m_Failed.count(composite))
        {
            result.error = "the collider of this mesh could not be built";
            return result;
        }

        Job job;
        job.composite = composite;
        job.key = key;
        job.asset = settings.mesh.GetAsInt();
        if (JPH::ShapeRefC cooked = LoadCooked(settings.mesh, key, job.stamp, job.cookedSource))
        {
            m_Shapes[composite] = { cooked, job.asset };
            result.shape = cooked;
            return result;
        }

        std::shared_ptr<const TriangleSoup> soup;
        if (!LoadSoup(settings.mesh, soup, result.error))
        {
            m_Failed[composite] = 1;
            return result;
        }

        if (result.used == MeshApproximation::Hulls && m_Async)
        {
            MeshColliderSettings copy = settings;
            const MeshApproximation resolved = result.used;
            job.future = std::async(std::launch::async, [soup, copy, resolved]() {
                return BuildMeshShape(*soup, copy, resolved, nullptr);
            });
            m_Jobs.push_back(std::move(job));

            MeshColliderSettings fallback = settings;
            fallback.approximation = MeshApproximation::Hull;
            MeshColliderResult hull = Acquire(fallback, true);
            hull.refining = true;
            return hull;
        }

        result.shape = BuildMeshShape(*soup, settings, result.used, &result.error);
        if (!result.shape)
        {
            m_Failed[composite] = 1;
            return result;
        }
        m_Shapes[composite] = { result.shape, job.asset };
        Store(job, *result.shape);
        return result;
    }

    void MeshColliderCache::Poll()
    {
        for (size_t i = 0; i < m_Jobs.size();)
        {
            Job& job = m_Jobs[i];
            if (!job.future.valid() || job.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                ++i;
                continue;
            }
            JPH::ShapeRefC shape;
            try { shape = job.future.get(); } catch (...) {}
            if (shape)
            {
                m_Shapes[job.composite] = { shape, job.asset };
                Store(job, *shape);
            }
            else
            {
                m_Failed[job.composite] = 1;
            }
            ++m_Generation;
            m_Jobs.erase(m_Jobs.begin() + std::ptrdiff_t(i));
        }
    }

    void MeshColliderCache::WaitIdle()
    {
        for (Job& job : m_Jobs)
            if (job.future.valid())
                job.future.wait();
        Poll();
    }

    void MeshColliderCache::Invalidate(Filesystem::AssetID asset)
    {
        WaitIdle();
        for (auto it = m_Shapes.begin(); it != m_Shapes.end();)
            it = it->second.asset == asset.GetAsInt() ? m_Shapes.erase(it) : std::next(it);
        m_Failed.clear();
        if (m_SoupAsset == asset.GetAsInt())
        {
            m_Soup.reset();
            m_SoupAsset = 0;
        }
        ++m_Generation;
    }

    void MeshColliderCache::Clear()
    {
        WaitIdle();
        m_Shapes.clear();
        m_Failed.clear();
        m_Soup.reset();
        m_SoupAsset = 0;
        ++m_Generation;
    }
}
