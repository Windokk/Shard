#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering::Raytracing {

    // GPU-facing BVH node - mirrors the GLSL `BVHNode` struct in path_trace.comp field-for-field
    // (32 bytes, no padding under std430 : a 4-byte scalar right after a vec3 packs into the vec3's own
    // alignment slack). leftFirst is a nœud interne : index of the left child (right = leftFirst + 1,
    // children are always allocated as a contiguous pair) - for a leaf, it's the index of the first
    // triangle. triCount == 0 means "internal node", > 0 means "leaf with triCount triangles".
    struct BVHNode
    {
        glm::vec3 boundsMin;
        uint32_t  leftFirst;
        glm::vec3 boundsMax;
        uint32_t  triCount;
    };

    // Minimal per-primitive info the builder needs - callers build one of these per triangle (or
    // whatever primitive type) from their own geometry.
    struct BVHPrimitive
    {
        glm::vec3 centroid;
        glm::vec3 boundsMin;
        glm::vec3 boundsMax;
    };

    class BVHBuilder
    {
        public:
            // Builds a BVH over `primitives` using a top-down, binned-SAH split. Returns the flat node
            // array (root at index 0) and fills `outOrder` with the permutation applied to the
            // primitives : outOrder[i] is the ORIGINAL index of the primitive now sitting at slot i.
            // The caller must reorder its own parallel payload arrays (triangle positions/attributes,
            // ...) using this same permutation, since leaf nodes address their triangles by contiguous
            // range in that reordered space (leftFirst / leftFirst + triCount).
            //
            // onProgress, if set, is invoked with a monotonically increasing fraction in [0, 1] as the
            // build advances (roughly, primitives committed to a finished leaf / total). It may be
            // called many times and from whatever thread Build() runs on - keep it cheap and
            // thread-safe (the DDGI probe build runs this on a worker thread, see ProbeManager).
            using ProgressFn = std::function<void(float)>;
            static std::vector<BVHNode> Build(const std::vector<BVHPrimitive>& primitives, std::vector<uint32_t>& outOrder,
                const ProgressFn& onProgress = {});
    };

}
