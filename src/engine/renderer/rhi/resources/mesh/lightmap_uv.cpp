#include "lightmap_uv.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <numeric>
#include <thread>
#include <type_traits>
#include <unordered_map>

#include "engine/core/math/rect_packer.hpp"

namespace Shard::Engine::Rendering {

    namespace {

        struct PositionKey
        {
            int64_t x, y, z;
            bool operator==(const PositionKey& o) const { return x == o.x && y == o.y && z == o.z; }
        };

        struct PositionKeyHash
        {
            size_t operator()(const PositionKey& k) const
            {
                uint64_t h = uint64_t(k.x) * 0x9E3779B97F4A7C15ull;
                h ^= (uint64_t(k.y) + 0x7F4A7C15ull + (h << 6) + (h >> 2)) * 0xC2B2AE3D27D4EB4Full;
                h ^= (uint64_t(k.z) + 0x165667B1ull + (h << 6) + (h >> 2)) * 0x27D4EB2F165667C5ull;
                return size_t(h ^ (h >> 29));
            }
        };

        struct Triangle
        {
            uint32_t v[3] = { 0, 0, 0 };
            glm::vec3 normal = glm::vec3(0.0f);     // unit, or zero for a triangle without an area
            glm::vec3 facing = glm::vec3(0.0f);     // what the charts are grown with : the mesh's smooth normal over the triangle when it has one (see BuildGroupGeometry)
            float area = 0.0f;
            int chart = -1;
            int region = 0;                         // the set of triangles a chart is being grown in (see BuildCharts)
            bool valid = true;                      // false : an index outside the vertex array, left out
        };

        // The triangles of one group and which of them share an edge (compressed rows : the neighbours of triangle t are
        // neighbours[firstNeighbour[t] .. firstNeighbour[t + 1]))
        struct GroupGeometry
        {
            std::vector<Triangle> triangles;
            std::vector<uint32_t> firstNeighbour;
            std::vector<uint32_t> neighbours;
        };

        struct Chart
        {
            size_t group = 0;
            std::vector<uint32_t> triangles;        // indices in the group's triangle list
            glm::vec3 normalSum = glm::vec3(0.0f);  // the triangles' normals weighted by their area
            glm::vec3 axisU = glm::vec3(1, 0, 0);   // the plane the chart is projected onto
            glm::vec3 axisV = glm::vec3(0, 0, 1);
            float cosTurn = 1.0f;                   // the turn applied to the projected points to take the least room
            float sinTurn = 0.0f;
            glm::vec2 minCorner = glm::vec2(0.0f);  // of the turned points : what is subtracted to put the chart at the origin
            float width = 0.0f;                     // of the turned chart, in the mesh's units
            float height = 0.0f;
            glm::vec2 offset = glm::vec2(0.0f);     // where the layout put the chart's origin, in the unit square
        };

        // A finely tessellated surface (cloth, scans) has face normals that jitter by tens of degrees from one triangle to the
        // next while the surface itself is smooth : grown on those, the charts stop after a triangle or two and the lightmap
        // becomes a heap of slivers. Each triangle's `facing` is its normal averaged (by area) with its neighbours', a few
        // rings deep, over the neighbours that face about the same way as it does - so noise is averaged out while a real
        // crease (a wall meeting a floor) is not blurred across, and still cuts a chart.
        void SmoothFacing(GroupGeometry& geometry)
        {
            constexpr int iterations = 20;
            constexpr float cosLimit = 0.8f;        // about 37 degrees : the neighbours that count as the same surface
            const size_t count = geometry.triangles.size();
            std::vector<glm::vec3> current(count), next(count);
            for (size_t t = 0; t < count; t++)
                current[t] = geometry.triangles[t].normal;

            for (int pass = 0; pass < iterations; pass++)
            {
                for (size_t t = 0; t < count; t++)
                {
                    const Triangle& tri = geometry.triangles[t];
                    if (tri.area <= 0.0f)
                    {
                        next[t] = current[t];
                        continue;
                    }
                    glm::vec3 sum = current[t] * tri.area;
                    for (uint32_t n = geometry.firstNeighbour[t]; n < geometry.firstNeighbour[t + 1]; n++)
                    {
                        const Triangle& other = geometry.triangles[geometry.neighbours[n]];
                        // The face normals decide who is a neighbour of the same surface, so the filter cannot creep around a crease
                        if (other.area > 0.0f && glm::dot(other.normal, tri.normal) >= cosLimit)
                            sum += current[geometry.neighbours[n]] * other.area;
                    }
                    const float length = glm::length(sum);
                    next[t] = length > 1e-12f ? sum / length : current[t];
                }
                current.swap(next);
            }

            for (size_t t = 0; t < count; t++)
                if (geometry.triangles[t].area > 0.0f)
                    geometry.triangles[t].facing = current[t];
        }

        void BuildGroupGeometry(const LightmapUnwrapGroup& group, GroupGeometry& out)
        {
            const size_t triangleCount = group.indexCount / 3;
            out.triangles.assign(triangleCount, Triangle{});

            for (size_t t = 0; t < triangleCount; t++)
            {
                Triangle& tri = out.triangles[t];
                for (int k = 0; k < 3; k++)
                {
                    tri.v[k] = group.indices[t * 3 + k];
                    if (tri.v[k] >= group.vertexCount)
                        tri.valid = false;
                }
                if (!tri.valid)
                    continue;

                const glm::vec3& p0 = group.positions[tri.v[0]];
                const glm::vec3 c = glm::cross(group.positions[tri.v[1]] - p0, group.positions[tri.v[2]] - p0);
                const float length = glm::length(c);
                tri.area = 0.5f * length;
                if (length > 1e-20f && std::isfinite(length))
                    tri.normal = c / length;
                else
                    tri.area = 0.0f;

                tri.facing = tri.normal;
            }

            // Triangles that share an edge are neighbours - judged on the vertices' positions, not their indices : a mesh
            // splits its vertices wherever a normal or a texture coordinate changes, which must not cut a chart
            glm::vec3 boundsMin(std::numeric_limits<float>::max()), boundsMax(std::numeric_limits<float>::lowest());
            for (size_t v = 0; v < group.vertexCount; v++)
            {
                boundsMin = glm::min(boundsMin, group.positions[v]);
                boundsMax = glm::max(boundsMax, group.positions[v]);
            }
            const float quantum = std::max(glm::length(boundsMax - boundsMin) * 1e-6f, 1e-9f);

            std::vector<uint32_t> welded(group.vertexCount);
            {
                std::unordered_map<PositionKey, uint32_t, PositionKeyHash> byPosition;
                byPosition.reserve(group.vertexCount);
                for (size_t v = 0; v < group.vertexCount; v++)
                {
                    const glm::vec3& p = group.positions[v];
                    const PositionKey key{ int64_t(std::llround(p.x / quantum)), int64_t(std::llround(p.y / quantum)), int64_t(std::llround(p.z / quantum)) };
                    welded[v] = byPosition.emplace(key, uint32_t(v)).first->second;
                }
            }

            struct EdgeRef { uint64_t key; uint32_t triangle; };
            std::vector<EdgeRef> edges;
            edges.reserve(triangleCount * 3);
            for (size_t t = 0; t < triangleCount; t++)
            {
                const Triangle& tri = out.triangles[t];
                if (!tri.valid)
                    continue;
                for (int k = 0; k < 3; k++)
                {
                    uint32_t a = welded[tri.v[k]], b = welded[tri.v[(k + 1) % 3]];
                    if (a == b)
                        continue;
                    if (a > b)
                        std::swap(a, b);
                    edges.push_back({ (uint64_t(a) << 32) | b, uint32_t(t) });
                }
            }
            std::sort(edges.begin(), edges.end(), [](const EdgeRef& l, const EdgeRef& r) {
                return l.key != r.key ? l.key < r.key : l.triangle < r.triangle;
            });

            std::vector<std::pair<uint32_t, uint32_t>> links;
            for (size_t i = 0; i < edges.size();)
            {
                size_t j = i + 1;
                while (j < edges.size() && edges[j].key == edges[i].key)
                    j++;

                // More than two triangles on an edge is rare (a non manifold mesh) : link the first few of them
                const size_t count = std::min<size_t>(j - i, 6);
                for (size_t a = 0; a < count; a++)
                    for (size_t b = a + 1; b < count; b++)
                        if (edges[i + a].triangle != edges[i + b].triangle)
                            links.emplace_back(edges[i + a].triangle, edges[i + b].triangle);
                i = j;
            }

            out.firstNeighbour.assign(triangleCount + 1, 0);
            for (const auto& link : links)
            {
                out.firstNeighbour[link.first + 1]++;
                out.firstNeighbour[link.second + 1]++;
            }
            for (size_t t = 0; t < triangleCount; t++)
                out.firstNeighbour[t + 1] += out.firstNeighbour[t];

            out.neighbours.assign(out.firstNeighbour[triangleCount], 0);
            std::vector<uint32_t> cursor(out.firstNeighbour.begin(), out.firstNeighbour.end() - 1);
            for (const auto& link : links)
            {
                out.neighbours[cursor[link.first]++] = link.second;
                out.neighbours[cursor[link.second]++] = link.first;
            }

            SmoothFacing(out);
        }

        // The projection plane of a chart : the plane its triangles face on average
        void ChartFrame(Chart& chart)
        {
            const float normalLength = glm::length(chart.normalSum);
            const glm::vec3 normal = normalLength > 1e-12f ? chart.normalSum / normalLength : glm::vec3(0.0f, 1.0f, 0.0f);

            const glm::vec3 absNormal = glm::abs(normal);
            glm::vec3 helper(1.0f, 0.0f, 0.0f);
            if (absNormal.y <= absNormal.x && absNormal.y <= absNormal.z)
                helper = glm::vec3(0.0f, 1.0f, 0.0f);
            else if (absNormal.z <= absNormal.x && absNormal.z <= absNormal.y)
                helper = glm::vec3(0.0f, 0.0f, 1.0f);

            chart.axisU = glm::normalize(glm::cross(normal, helper));
            chart.axisV = glm::cross(normal, chart.axisU);
        }

        // Whether two triangles of the plane share some area (touching along an edge or at a corner is not sharing)
        bool TrianglesOverlap(const glm::vec2* a, const glm::vec2* b, float epsilon)
        {
            for (int pass = 0; pass < 2; pass++)
            {
                const glm::vec2* t = pass == 0 ? a : b;
                for (int k = 0; k < 3; k++)
                {
                    const glm::vec2 edge = t[(k + 1) % 3] - t[k];
                    const float length = glm::length(edge);
                    if (length < 1e-20f)
                        continue;
                    const glm::vec2 axis(-edge.y / length, edge.x / length);

                    float minA = 1e30f, maxA = -1e30f, minB = 1e30f, maxB = -1e30f;
                    for (int i = 0; i < 3; i++)
                    {
                        const float da = glm::dot(a[i], axis), db = glm::dot(b[i], axis);
                        minA = std::min(minA, da); maxA = std::max(maxA, da);
                        minB = std::min(minB, db); maxB = std::max(maxB, db);
                    }
                    if (maxA <= minB + epsilon || maxB <= minA + epsilon)
                        return false;
                }
            }
            return true;
        }

        // Whether the chart folds over itself once flattened : two of its triangles land on the same part of the plane. That is
        // what a patch that winds around (a curl, a horn, a ridge seen from its side) does, and it would give two parts of
        // the surface the same texels. A bucket grid keeps the test close to linear.
        bool ChartOverlaps(const Chart& chart, const LightmapUnwrapGroup& group, const GroupGeometry& geometry)
        {
            const size_t count = chart.triangles.size();
            if (count < 2)
                return false;

            std::vector<glm::vec2> corners(count * 3);
            glm::vec2 lo(1e30f), hi(-1e30f);
            for (size_t i = 0; i < count; i++)
            {
                const Triangle& tri = geometry.triangles[chart.triangles[i]];
                for (int k = 0; k < 3; k++)
                {
                    const glm::vec3& p = group.positions[tri.v[k]];
                    corners[i * 3 + k] = glm::vec2(glm::dot(p, chart.axisU), glm::dot(p, chart.axisV));
                    lo = glm::min(lo, corners[i * 3 + k]);
                    hi = glm::max(hi, corners[i * 3 + k]);
                }
            }

            const glm::vec2 extent = glm::max(hi - lo, glm::vec2(1e-12f));
            // Two triangles that overlap by less than a centimeter are the relief of the surface (a weave, a crumpled cloth), not a
            // part that folds over : the light they would share is the same to within a fraction of a texel
            constexpr float kOverlapTolerance = 0.01f;
            const float epsilon = std::max(1e-5f * std::max(extent.x, extent.y), kOverlapTolerance);
            const int cells = int(std::clamp(std::sqrt(double(count)) * 1.5, 1.0, 256.0));
            std::vector<std::vector<uint32_t>> grid(size_t(cells) * cells);

            auto cellOf = [&](float value, float low, float size) {
                return std::clamp(int((value - low) / size * float(cells)), 0, cells - 1);
            };

            for (size_t i = 0; i < count; i++)
            {
                const glm::vec2* t = &corners[i * 3];
                const glm::vec2 tl = glm::min(t[0], glm::min(t[1], t[2])), th = glm::max(t[0], glm::max(t[1], t[2]));
                const int x0 = cellOf(tl.x, lo.x, extent.x), x1 = cellOf(th.x, lo.x, extent.x);
                const int y0 = cellOf(tl.y, lo.y, extent.y), y1 = cellOf(th.y, lo.y, extent.y);

                for (int y = y0; y <= y1; y++)
                    for (int x = x0; x <= x1; x++)
                    {
                        std::vector<uint32_t>& cell = grid[size_t(y) * cells + x];
                        for (uint32_t other : cell)
                            if (TrianglesOverlap(t, &corners[size_t(other) * 3], epsilon))
                                return true;
                        cell.push_back(uint32_t(i));
                    }
            }
            return false;
        }

        void MergeSmallCharts(const LightmapUnwrapGroup& group, GroupGeometry& geometry, const LightmapUnwrapSettings& settings,
                              std::vector<Chart>& charts);

        // Grows the group's charts : from the largest triangle not in a chart yet, across shared edges, for as long as the
        // neighbour faces about the same way as the chart does. A chart that would fold over itself when flattened is thrown
        // back and grown again with stricter angles, down to a single triangle.
        void BuildCharts(size_t groupIndex, const LightmapUnwrapGroup& group, GroupGeometry& geometry,
                         const LightmapUnwrapSettings& settings, std::vector<Chart>& charts)
        {
            struct Region { std::vector<uint32_t> triangles; float angleScale; int depth; };

            std::vector<Region> pending(1);
            pending[0].angleScale = 1.0f;
            pending[0].depth = 0;
            for (uint32_t t = 0; t < geometry.triangles.size(); t++)
                if (geometry.triangles[t].valid)
                    pending[0].triangles.push_back(t);

            // While a chart is being grown its triangles carry this tag, which cannot be the index of a chart
            constexpr int kGrowing = std::numeric_limits<int>::max();

            int nextRegion = 1;
            std::vector<uint32_t> stack;
            while (!pending.empty())
            {
                Region region = std::move(pending.back());
                pending.pop_back();

                const float cosChart = std::cos(glm::radians(settings.maxChartAngleDegrees * region.angleScale));
                const float cosSeed = std::cos(glm::radians(settings.maxSeedAngleDegrees * region.angleScale));

                const int regionId = nextRegion++;
                for (uint32_t t : region.triangles)
                {
                    geometry.triangles[t].region = regionId;
                    geometry.triangles[t].chart = -1;
                }

                std::stable_sort(region.triangles.begin(), region.triangles.end(), [&](uint32_t a, uint32_t b) {
                    return geometry.triangles[a].area > geometry.triangles[b].area;
                });

                for (uint32_t seed : region.triangles)
                {
                    Triangle& seedTriangle = geometry.triangles[seed];
                    if (seedTriangle.chart >= 0 || seedTriangle.region != regionId)
                        continue;

                    Chart chart;
                    chart.group = groupIndex;
                    seedTriangle.chart = kGrowing;
                    chart.triangles.push_back(seed);
                    chart.normalSum = seedTriangle.facing * seedTriangle.area;

                    stack.clear();
                    stack.push_back(seed);
                    while (!stack.empty())
                    {
                        const uint32_t current = stack.back();
                        stack.pop_back();

                        for (uint32_t n = geometry.firstNeighbour[current]; n < geometry.firstNeighbour[current + 1]; n++)
                        {
                            const uint32_t candidateIndex = geometry.neighbours[n];
                            Triangle& candidate = geometry.triangles[candidateIndex];
                            if (candidate.chart >= 0 || candidate.region != regionId)
                                continue;

                            // A triangle without an area has no facing : it goes along with whatever it touches
                            bool accept = candidate.area <= 0.0f;
                            if (!accept)
                            {
                                const float sumLength = glm::length(chart.normalSum);
                                const glm::vec3 average = sumLength > 1e-12f ? chart.normalSum / sumLength : seedTriangle.facing;
                                accept = glm::dot(candidate.facing, average) >= cosChart &&
                                         (seedTriangle.area <= 0.0f || glm::dot(candidate.facing, seedTriangle.facing) >= cosSeed);
                            }

                            if (!accept)
                                continue;

                            candidate.chart = kGrowing;
                            chart.triangles.push_back(candidateIndex);
                            chart.normalSum += candidate.facing * candidate.area;
                            stack.push_back(candidateIndex);
                        }
                    }

                    ChartFrame(chart);
                    if (chart.triangles.size() > 1 && region.depth < 8 && ChartOverlaps(chart, group, geometry))
                    {
                        // Grown again with stricter angles, on its own : the triangles are free again for that
                        Region retry;
                        retry.triangles = chart.triangles;
                        retry.angleScale = region.angleScale * 0.6f;
                        retry.depth = region.depth + 1;
                        for (uint32_t t : retry.triangles)
                        {
                            geometry.triangles[t].chart = -1;
                            geometry.triangles[t].region = 0;       // out of this region's growth : the retry takes them
                        }
                        pending.push_back(std::move(retry));
                        continue;
                    }

                    const int chartIndex = int(charts.size());
                    for (uint32_t t : chart.triangles)
                        geometry.triangles[t].chart = chartIndex;
                    charts.push_back(std::move(chart));
                }
            }

            MergeSmallCharts(group, geometry, settings, charts);
        }

        // The leftovers of the growth above : on a surface with fine relief, the triangles that did not fit a big chart each
        // end up in a chart of their own, a few triangles wide - a few texels of lightmap with a seam all around them, and a
        // visible patch of wrong light. Every chart smaller than the settings' minimum is merged into the neighbour it shares
        // most edges with (smallest first, so a run of slivers collects into one), as long as that neighbour does not face
        // away from it. What is merged is tiny, so what it gets wrong when projected onto the neighbour's plane stays far
        // below a texel.
        void MergeSmallCharts(const LightmapUnwrapGroup& group, GroupGeometry& geometry, const LightmapUnwrapSettings& settings,
                              std::vector<Chart>& charts)
        {
            if (settings.minChartArea <= 0.0f || charts.size() < 2)
                return;

            const float cosMerge = std::cos(glm::radians(settings.maxMergeAngleDegrees));
            std::vector<float> area(charts.size(), 0.0f);
            for (size_t c = 0; c < charts.size(); c++)
                for (uint32_t t : charts[c].triangles)
                    area[c] += geometry.triangles[t].area;

            std::vector<uint32_t> order(charts.size());
            std::iota(order.begin(), order.end(), 0u);
            std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return area[a] != area[b] ? area[a] < area[b] : a < b; });

            std::vector<int> mergedInto(charts.size(), -1);
            auto resolve = [&](int c) { while (mergedInto[c] >= 0) c = mergedInto[c]; return c; };

            std::unordered_map<int, int> shared;
            for (uint32_t index : order)
            {
                if (mergedInto[index] >= 0 || area[index] >= settings.minChartArea)
                    continue;

                Chart& small = charts[index];
                const float smallLength = glm::length(small.normalSum);
                if (smallLength <= 1e-12f)
                    continue;
                const glm::vec3 smallNormal = small.normalSum / smallLength;

                shared.clear();
                for (uint32_t t : small.triangles)
                    for (uint32_t n = geometry.firstNeighbour[t]; n < geometry.firstNeighbour[t + 1]; n++)
                    {
                        const int other = geometry.triangles[geometry.neighbours[n]].chart;
                        if (other >= 0 && resolve(other) != int(index))
                            shared[resolve(other)]++;
                    }

                int best = -1, bestCount = 0;
                for (const auto& entry : shared)
                {
                    const Chart& candidate = charts[entry.first];
                    const float length = glm::length(candidate.normalSum);
                    if (length <= 1e-12f || glm::dot(candidate.normalSum / length, smallNormal) < cosMerge)
                        continue;
                    // The most shared edges, then the larger chart, then the lower index : deterministic whatever the map's order
                    if (entry.second > bestCount || (entry.second == bestCount && (area[entry.first] > area[best] || (area[entry.first] == area[best] && entry.first < best))))
                    {
                        best = entry.first;
                        bestCount = entry.second;
                    }
                }
                if (best < 0)
                    continue;

                Chart& target = charts[best];
                target.triangles.insert(target.triangles.end(), small.triangles.begin(), small.triangles.end());
                target.normalSum += small.normalSum;
                area[best] += area[index];
                for (uint32_t t : small.triangles)
                    geometry.triangles[t].chart = best;
                small.triangles.clear();
                mergedInto[index] = best;
            }

            // Compact : the emptied charts leave, the others keep their order
            std::vector<int> remap(charts.size(), -1);
            std::vector<Chart> kept;
            kept.reserve(charts.size());
            for (size_t c = 0; c < charts.size(); c++)
                if (!charts[c].triangles.empty())
                {
                    remap[c] = int(kept.size());
                    kept.push_back(std::move(charts[c]));
                }
            for (Chart& chart : kept)
            {
                std::sort(chart.triangles.begin(), chart.triangles.end());       // a merged chart is flattened in a stable order
                ChartFrame(chart);
                for (uint32_t t : chart.triangles)
                    geometry.triangles[t].chart = remap[geometry.triangles[t].chart];
            }
            charts = std::move(kept);
            (void)group;
        }

        // Convex hull of the points (Andrew's monotone chain), counter clockwise, without the first point repeated
        std::vector<glm::vec2> ConvexHull(std::vector<glm::vec2> points)
        {
            std::sort(points.begin(), points.end(), [](const glm::vec2& a, const glm::vec2& b) {
                return a.x != b.x ? a.x < b.x : a.y < b.y;
            });
            points.erase(std::unique(points.begin(), points.end()), points.end());
            if (points.size() < 3)
                return points;

            auto cross = [](const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) {
                return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
            };

            std::vector<glm::vec2> hull(points.size() * 2);
            size_t k = 0;
            for (size_t i = 0; i < points.size(); i++)
            {
                while (k >= 2 && cross(hull[k - 2], hull[k - 1], points[i]) <= 0.0f)
                    k--;
                hull[k++] = points[i];
            }
            for (size_t i = points.size() - 1, lower = k + 1; i > 0; i--)
            {
                while (k >= lower && cross(hull[k - 2], hull[k - 1], points[i - 1]) <= 0.0f)
                    k--;
                hull[k++] = points[i - 1];
            }
            hull.resize(k - 1);
            return hull;
        }

        // Turns the points (the angle's cosine and sine) and measures the box that holds them
        void MeasureTurned(const std::vector<glm::vec2>& points, float c, float s, glm::vec2& outMin, glm::vec2& outMax)
        {
            outMin = glm::vec2(std::numeric_limits<float>::max());
            outMax = glm::vec2(std::numeric_limits<float>::lowest());
            for (const glm::vec2& p : points)
            {
                const glm::vec2 turned(p.x * c + p.y * s, -p.x * s + p.y * c);
                outMin = glm::min(outMin, turned);
                outMax = glm::max(outMax, turned);
            }
        }

        // Flattens a chart : projects it onto the plane its triangles face, then turns the result to the angle where the box
        // that holds it is the smallest (one of the hull's edges lies along a side of the smallest box), longest side first
        void FlattenChart(Chart& chart, const LightmapUnwrapGroup& group, const GroupGeometry& geometry)
        {
            std::vector<glm::vec2> points;
            for (uint32_t t : chart.triangles)
            {
                const Triangle& tri = geometry.triangles[t];
                for (int k = 0; k < 3; k++)
                {
                    const glm::vec3& p = group.positions[tri.v[k]];
                    points.emplace_back(glm::dot(p, chart.axisU), glm::dot(p, chart.axisV));
                }
            }

            const std::vector<glm::vec2> hull = ConvexHull(points);

            float bestCos = 1.0f, bestSin = 0.0f;
            glm::vec2 bestMin, bestMax;
            MeasureTurned(hull, bestCos, bestSin, bestMin, bestMax);
            float bestArea = (bestMax.x - bestMin.x) * (bestMax.y - bestMin.y);

            if (hull.size() >= 3)
            {
                // A curved patch has hundreds of hull edges : a spread of them is enough
                const size_t step = std::max<size_t>(1, hull.size() / 96);
                for (size_t i = 0; i < hull.size(); i += step)
                {
                    const glm::vec2 edge = hull[(i + 1) % hull.size()] - hull[i];
                    const float length = glm::length(edge);
                    if (length < 1e-12f)
                        continue;

                    const float c = edge.x / length, s = edge.y / length;
                    glm::vec2 lo, hi;
                    MeasureTurned(hull, c, s, lo, hi);
                    const float area = (hi.x - lo.x) * (hi.y - lo.y);
                    if (area < bestArea * 0.9999f)
                    {
                        bestArea = area;
                        bestCos = c;
                        bestSin = s;
                        bestMin = lo;
                        bestMax = hi;
                    }
                }
            }

            // Longest side along u : the layout then packs rows of wide charts
            if (bestMax.y - bestMin.y > bestMax.x - bestMin.x)
            {
                const float c = -bestSin, s = bestCos;
                bestCos = c;
                bestSin = s;
                MeasureTurned(hull, bestCos, bestSin, bestMin, bestMax);
            }

            chart.cosTurn = bestCos;
            chart.sinTurn = bestSin;
            chart.minCorner = bestMin;
            chart.width = std::max(bestMax.x - bestMin.x, 1e-6f);
            chart.height = std::max(bestMax.y - bestMin.y, 1e-6f);
        }

        // ---------------------------------------------------------------------------------------------------------------------
        // The layout : everything the unwrapping decides, apart from the triangles' own coordinates (which follow from it).
        // This is what the cache keeps.
        // ---------------------------------------------------------------------------------------------------------------------

        constexpr uint32_t kNoChart = 0xFFFFFFFFu;

        // How a chart is laid on the unit square : project onto (axisU, axisV), turn by (cosTurn, sinTurn), move the corner to the
        // origin (minCorner), scale and place
        struct ChartPlacement
        {
            glm::vec3 axisU = glm::vec3(1, 0, 0);
            glm::vec3 axisV = glm::vec3(0, 0, 1);
            float cosTurn = 1.0f;
            float sinTurn = 0.0f;
            glm::vec2 minCorner = glm::vec2(0.0f);
            glm::vec2 offset = glm::vec2(0.0f);
        };
        static_assert(sizeof(ChartPlacement) == 12 * sizeof(float), "ChartPlacement is written to the cache as raw bytes");

        struct UnwrapLayout
        {
            float scale = 0.0f;
            std::vector<ChartPlacement> charts;
            std::vector<std::vector<uint32_t>> triangleChart;      // per group, per triangle : its chart, or kNoChart
        };

        glm::vec2 PlacedPoint(const ChartPlacement& chart, float scale, const glm::vec3& p)
        {
            const float x = glm::dot(p, chart.axisU), y = glm::dot(p, chart.axisV);
            const glm::vec2 turned(x * chart.cosTurn + y * chart.sinTurn, -x * chart.sinTurn + y * chart.cosTurn);
            return (turned - chart.minCorner) * scale + chart.offset;
        }

        // Runs body(i) for i in [0, count) on as many threads as there are cores. A stage of the unwrapping is a loop over
        // groups, or charts, that do not depend on each other.
        template <typename Body>
        void ParallelFor(size_t count, size_t grain, Body&& body)
        {
            const size_t chunks = (count + grain - 1) / grain;
            const size_t threads = std::min<size_t>(std::max(1u, std::thread::hardware_concurrency()), chunks);
            if (threads <= 1)
            {
                for (size_t i = 0; i < count; i++)
                    body(i);
                return;
            }

            std::atomic<size_t> next{ 0 };
            auto worker = [&] {
                for (;;)
                {
                    const size_t begin = next.fetch_add(grain);
                    if (begin >= count)
                        return;
                    const size_t end = std::min(count, begin + grain);
                    for (size_t i = begin; i < end; i++)
                        body(i);
                }
            };

            std::vector<std::thread> pool;
            for (size_t t = 1; t < threads; t++)
                pool.emplace_back(worker);
            worker();
            for (std::thread& thread : pool)
                thread.join();
        }

        // The cutting and the flattening of the charts, then the packing of them
        UnwrapLayout ComputeLayout(const std::vector<LightmapUnwrapGroup>& groups, const LightmapUnwrapSettings& settings)
        {
            UnwrapLayout layout;
            layout.triangleChart.resize(groups.size());

            // Charts never span two groups, so each group is cut on its own (a group is usually a material slot, and one of them
            // is most of a big scene : the groups are the coarse grain, the charts below it the fine one)
            std::vector<GroupGeometry> geometries(groups.size());
            std::vector<std::vector<Chart>> groupCharts(groups.size());
            ParallelFor(groups.size(), 1, [&](size_t g) {
                if (!groups[g].positions || !groups[g].indices || groups[g].indexCount < 3)
                    return;

                BuildGroupGeometry(groups[g], geometries[g]);
                BuildCharts(g, groups[g], geometries[g], settings, groupCharts[g]);
            });

            std::vector<Chart> charts;
            std::vector<size_t> firstChartOfGroup(groups.size(), 0);
            for (size_t g = 0; g < groups.size(); g++)
            {
                firstChartOfGroup[g] = charts.size();
                for (Chart& chart : groupCharts[g])
                    charts.push_back(std::move(chart));
                groupCharts[g] = std::vector<Chart>();
            }

            // Which chart every triangle is in, now that the charts of all the groups are in one list
            for (size_t g = 0; g < groups.size(); g++)
            {
                const GroupGeometry& geometry = geometries[g];
                std::vector<uint32_t>& triangleChart = layout.triangleChart[g];
                triangleChart.assign(geometry.triangles.size(), kNoChart);
                for (size_t t = 0; t < geometry.triangles.size(); t++)
                    if (geometry.triangles[t].valid && geometry.triangles[t].chart >= 0)
                        triangleChart[t] = uint32_t(firstChartOfGroup[g] + size_t(geometry.triangles[t].chart));
            }

            if (charts.empty())
                return layout;

            ParallelFor(charts.size(), 256, [&](size_t i) {
                FlattenChart(charts[i], groups[charts[i].group], geometries[charts[i].group]);
            });

            // ---- Layout : the largest scale at which every chart, with its padding, still fits in the unit square ----
            double chartArea = 0.0;
            for (const Chart& chart : charts)
                chartArea += double(chart.width) * double(chart.height);

            std::vector<Core::PackRect> rects(charts.size());
            auto fits = [&](float scale, float padding) {
                for (size_t i = 0; i < charts.size(); i++)
                {
                    rects[i].w = charts[i].width * scale + padding;
                    rects[i].h = charts[i].height * scale + padding;
                }
                return Core::PackRectsShelf(rects, 1.0f, 1.0f, 1) == 1;
            };

            float padding = std::max(settings.padding, 0.0f);
            const float scaleLimit = float(1.0 / std::sqrt(std::max(chartArea, 1e-30)));

            // A mesh with a great many charts cannot spare the same gap around each : the gap shrinks until they fit at all
            for (int attempt = 0; attempt < 14 && !fits(scaleLimit * 1e-4f, padding); attempt++)
                padding *= 0.5f;
            if (!fits(scaleLimit * 1e-4f, padding))
                padding = 0.0f;

            float low = scaleLimit * 1e-4f, high = scaleLimit;
            for (int iteration = 0; iteration < 28; iteration++)
            {
                const float middle = 0.5f * (low + high);
                if (fits(middle, padding))
                    low = middle;
                else
                    high = middle;
            }

            layout.scale = low;
            fits(layout.scale, padding);

            layout.charts.resize(charts.size());
            for (size_t i = 0; i < charts.size(); i++)
            {
                ChartPlacement& placement = layout.charts[i];
                placement.axisU = charts[i].axisU;
                placement.axisV = charts[i].axisV;
                placement.cosTurn = charts[i].cosTurn;
                placement.sinTurn = charts[i].sinTurn;
                placement.minCorner = charts[i].minCorner;
                placement.offset = glm::vec2(rects[i].x + padding * 0.5f, rects[i].y + padding * 0.5f);
            }
            return layout;
        }

        // The vertices cut where a chart ends, with their coordinates in the layout
        LightmapUnwrapResult AssembleResult(const std::vector<LightmapUnwrapGroup>& groups, const UnwrapLayout& layout)
        {
            LightmapUnwrapResult result;
            result.groups.resize(groups.size());

            ParallelFor(groups.size(), 1, [&](size_t g) {
                const LightmapUnwrapGroup& group = groups[g];
                LightmapUnwrapGroupResult& out = result.groups[g];
                const std::vector<uint32_t>& triangleChart = layout.triangleChart[g];
                if (triangleChart.empty())
                    return;

                std::unordered_map<uint64_t, uint32_t> copies;
                copies.reserve(group.vertexCount);
                out.indices.reserve(triangleChart.size() * 3);

                for (size_t t = 0; t < triangleChart.size(); t++)
                {
                    if (triangleChart[t] == kNoChart)
                        continue;

                    const ChartPlacement& chart = layout.charts[triangleChart[t]];
                    for (int k = 0; k < 3; k++)
                    {
                        const uint32_t v = group.indices[t * 3 + k];
                        const uint64_t key = (uint64_t(triangleChart[t]) << 32) | v;
                        auto [it, inserted] = copies.emplace(key, uint32_t(out.sourceVertex.size()));
                        if (inserted)
                        {
                            out.sourceVertex.push_back(v);
                            out.uv.push_back(glm::clamp(PlacedPoint(chart, layout.scale, group.positions[v]), glm::vec2(0.0f), glm::vec2(1.0f)));
                        }
                        out.indices.push_back(it->second);
                    }
                }
            });

            // The areas, for whoever sizes a lightmap from them
            for (size_t g = 0; g < groups.size(); g++)
            {
                const LightmapUnwrapGroupResult& out = result.groups[g];
                for (size_t t = 0; t + 2 < out.indices.size(); t += 3)
                {
                    const glm::vec3& p0 = groups[g].positions[out.sourceVertex[out.indices[t]]];
                    const glm::vec3& p1 = groups[g].positions[out.sourceVertex[out.indices[t + 1]]];
                    const glm::vec3& p2 = groups[g].positions[out.sourceVertex[out.indices[t + 2]]];
                    result.surfaceArea += 0.5f * glm::length(glm::cross(p1 - p0, p2 - p0));

                    const glm::vec2 e1 = out.uv[out.indices[t + 1]] - out.uv[out.indices[t]];
                    const glm::vec2 e2 = out.uv[out.indices[t + 2]] - out.uv[out.indices[t]];
                    result.uvArea += 0.5f * std::abs(e1.x * e2.y - e1.y * e2.x);
                }
            }

            result.chartCount = uint32_t(layout.charts.size());
            result.success = result.surfaceArea > 0.0f && result.chartCount > 0;
            return result;
        }

        // ---------------------------------------------------------------------------------------------------------------------
        // The cache : cutting a mesh of millions of triangles takes seconds, and the same mesh is unwrapped at every load.
        // The layout is a pure function of the geometry and the settings, so it is kept in a file named after a hash of them.
        // ---------------------------------------------------------------------------------------------------------------------

        // Changes with every change of the algorithm that would give another layout for the same mesh
        constexpr uint32_t kAlgorithmVersion = 2;

        std::mutex& CacheMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::string& CacheDirectory()
        {
            static std::string directory;
            return directory;
        }

        uint64_t HashWords(uint64_t hash, const void* data, size_t bytes)
        {
            const uint8_t* p = static_cast<const uint8_t*>(data);
            size_t i = 0;
            for (; i + 8 <= bytes; i += 8)
            {
                uint64_t word;
                std::memcpy(&word, p + i, 8);
                hash = (hash ^ word) * 0x9E3779B97F4A7C15ull;
                hash ^= hash >> 29;
            }
            uint64_t tail = 0;
            if (i < bytes)
                std::memcpy(&tail, p + i, bytes - i);
            hash = (hash ^ tail ^ (uint64_t(bytes) << 56)) * 0xC2B2AE3D27D4EB4Full;
            return hash ^ (hash >> 32);
        }

        uint64_t HashInput(const std::vector<LightmapUnwrapGroup>& groups, const LightmapUnwrapSettings& settings)
        {
            uint64_t hash = 0xCBF29CE484222325ull;
            const uint32_t version = kAlgorithmVersion;
            hash = HashWords(hash, &version, sizeof(version));
            hash = HashWords(hash, &settings.maxChartAngleDegrees, sizeof(float));
            hash = HashWords(hash, &settings.maxSeedAngleDegrees, sizeof(float));
            hash = HashWords(hash, &settings.padding, sizeof(float));
            hash = HashWords(hash, &settings.minChartArea, sizeof(float));
            hash = HashWords(hash, &settings.maxMergeAngleDegrees, sizeof(float));

            for (const LightmapUnwrapGroup& group : groups)
            {
                hash = HashWords(hash, &group.vertexCount, sizeof(group.vertexCount));
                hash = HashWords(hash, &group.indexCount, sizeof(group.indexCount));
                if (group.positions)
                    hash = HashWords(hash, group.positions, group.vertexCount * sizeof(glm::vec3));
                if (group.indices)
                    hash = HashWords(hash, group.indices, group.indexCount * sizeof(uint32_t));
            }
            return hash;
        }

        struct CacheHeader
        {
            char magic[4];
            uint32_t version;
            uint64_t key;
            float scale;
            uint32_t chartCount;
            uint32_t groupCount;
            uint32_t reserved;
        };
        static_assert(std::is_trivially_copyable_v<CacheHeader>, "CacheHeader is written to disk as raw bytes");

        std::filesystem::path CachePath(uint64_t key)
        {
            std::lock_guard<std::mutex> lock(CacheMutex());
            if (CacheDirectory().empty())
                return {};

            char name[40];
            std::snprintf(name, sizeof(name), "%016llx.luv", static_cast<unsigned long long>(key));
            return std::filesystem::path(CacheDirectory()) / name;
        }

        bool LoadCachedLayout(uint64_t key, const std::vector<LightmapUnwrapGroup>& groups, UnwrapLayout& out)
        {
            const std::filesystem::path path = CachePath(key);
            if (path.empty())
                return false;

            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in.is_open())
                return false;

            const uint64_t fileSize = uint64_t(in.tellg());
            in.seekg(0, std::ios::beg);

            CacheHeader header{};
            if (fileSize < sizeof(header) || !in.read(reinterpret_cast<char*>(&header), sizeof(header)))
                return false;
            if (std::memcmp(header.magic, "SHLU", 4) != 0 || header.version != kAlgorithmVersion || header.key != key ||
                header.groupCount != groups.size())
                return false;

            // The file must be exactly the size its header and the input say it is
            uint64_t expected = sizeof(header) + uint64_t(header.chartCount) * sizeof(ChartPlacement);
            for (const LightmapUnwrapGroup& group : groups)
                expected += sizeof(uint32_t) * (1 + group.indexCount / 3);
            if (fileSize != expected)
                return false;

            UnwrapLayout layout;
            layout.scale = header.scale;
            layout.charts.resize(header.chartCount);
            in.read(reinterpret_cast<char*>(layout.charts.data()), std::streamsize(layout.charts.size() * sizeof(ChartPlacement)));

            layout.triangleChart.resize(groups.size());
            for (size_t g = 0; g < groups.size(); g++)
            {
                uint32_t count = 0;
                in.read(reinterpret_cast<char*>(&count), sizeof(count));
                if (!in || count != groups[g].indexCount / 3)
                    return false;

                layout.triangleChart[g].resize(count);
                in.read(reinterpret_cast<char*>(layout.triangleChart[g].data()), std::streamsize(count * sizeof(uint32_t)));
                for (uint32_t chart : layout.triangleChart[g])
                    if (chart != kNoChart && chart >= header.chartCount)
                        return false;
            }
            if (!in)
                return false;

            out = std::move(layout);
            return true;
        }

        void StoreCachedLayout(uint64_t key, const UnwrapLayout& layout)
        {
            const std::filesystem::path path = CachePath(key);
            if (path.empty())
                return;

            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);

            // Written to a file of its own and moved into place, so a reader (or a second writer, another thread unwrapping
            // the same mesh) never sees half a file
            static std::atomic<uint64_t> counter{ 0 };
            std::filesystem::path temporary = path;
            temporary += "." + std::to_string(counter.fetch_add(1)) + "." + std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id()) & 0xFFFFFF) + ".tmp";

            {
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                if (!out.is_open())
                    return;

                CacheHeader header{};
                std::memcpy(header.magic, "SHLU", 4);
                header.version = kAlgorithmVersion;
                header.key = key;
                header.scale = layout.scale;
                header.chartCount = uint32_t(layout.charts.size());
                header.groupCount = uint32_t(layout.triangleChart.size());
                out.write(reinterpret_cast<const char*>(&header), sizeof(header));
                out.write(reinterpret_cast<const char*>(layout.charts.data()), std::streamsize(layout.charts.size() * sizeof(ChartPlacement)));
                for (const std::vector<uint32_t>& triangleChart : layout.triangleChart)
                {
                    const uint32_t count = uint32_t(triangleChart.size());
                    out.write(reinterpret_cast<const char*>(&count), sizeof(count));
                    out.write(reinterpret_cast<const char*>(triangleChart.data()), std::streamsize(triangleChart.size() * sizeof(uint32_t)));
                }
                out.flush();
                if (!out.good())
                {
                    out.close();
                    std::filesystem::remove(temporary, ec);
                    return;
                }
            }

            std::filesystem::rename(temporary, path, ec);
            if (ec)
            {
                // Windows does not replace a file by renaming over it : another thread got there first, its file is as good
                std::filesystem::remove(temporary, ec);
            }
        }
    }

    void SetLightmapUnwrapCacheDirectory(const std::string& directory)
    {
        std::lock_guard<std::mutex> lock(CacheMutex());
        CacheDirectory() = directory;
    }

    LightmapUnwrapResult UnwrapForLightmap(const std::vector<LightmapUnwrapGroup>& groups, const LightmapUnwrapSettings& settings)
    {
        const uint64_t key = HashInput(groups, settings);

        UnwrapLayout layout;
        bool fromCache = LoadCachedLayout(key, groups, layout);
        if (!fromCache)
        {
            layout = ComputeLayout(groups, settings);
            if (!layout.charts.empty())
                StoreCachedLayout(key, layout);
        }

        if (layout.charts.empty())
        {
            LightmapUnwrapResult empty;
            empty.groups.resize(groups.size());
            return empty;
        }

        LightmapUnwrapResult result = AssembleResult(groups, layout);
        result.fromCache = fromCache;
        return result;
    }
}
