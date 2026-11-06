#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering::Raytracing {

    // Mirrors BVHNode in path_trace.comp, 32 bytes std430. triCount 0 is an internal node, right child = leftFirst + 1.
    struct BVHNode
    {
        glm::vec3 boundsMin;
        uint32_t  leftFirst;
        glm::vec3 boundsMax;
        uint32_t  triCount;
    };

    struct BVHPrimitive
    {
        glm::vec3 centroid;
        glm::vec3 boundsMin;
        glm::vec3 boundsMax;
    };

    class BVHBuilder
    {
        public:
            // outOrder[i] is the original index of the primitive at slot i. Callers must reorder their payload arrays the same way. onProgress is monotonic and may run on a worker thread.
            using ProgressFn = std::function<void(float)>;
            static std::vector<BVHNode> Build(const std::vector<BVHPrimitive>& primitives, std::vector<uint32_t>& outOrder,
                const ProgressFn& onProgress = {});
    };

}
