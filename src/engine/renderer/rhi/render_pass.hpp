#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/renderer/rhi/graph/render_graph.hpp"
#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"

namespace Shard::Engine::Rendering {

    using NumericValue = std::variant<bool, float, int, glm::vec2, glm::vec3, glm::vec4, glm::mat4>;

    enum class DebugViewRole : uint8_t { None = 0, Forward, GBuffer, Lighting, WireOverlay };

    struct RenderPass{
        std::shared_ptr<Framebuffer> target = nullptr;
        bool clearColor = true;
        bool clearDepth = true;
        std::map<std::string, NumericValue> customUniforms;
        std::map<std::string, uint64_t> customSamplers;
        bool overridePipeline = false;
        std::shared_ptr<Pipeline> customPipeline = nullptr;
        std::string shaderVariant;
        bool allowResize = true;
        bool depthPrepass = false;
        bool allowCulling = true;
        DebugViewRole debugViewRole = DebugViewRole::None;
        // Mat4 in customUniforms to frustum-cull against instead of the camera ("lightSpaceMatrix" for shadow passes). Only used when allowCulling is false.
        std::string cullMatrixUniform;
        bool enabled = true;
        // Skips the whole pass when the hash of its inputs is unchanged. Only valid if the output is a pure function of them, camera frustum culling is not hashed.
        bool cacheOutput = false;
        uint64_t cachedStateHash = 0;
        bool hasCachedState = false;
        MemoryBarrierBit barrierAfter = MemoryBarrierBit::None;
        std::vector<DrawCommand> drawList = {};
        std::unordered_map<uint64_t, size_t> drawCommandsLookup;
        std::vector<DrawCommand>* externalDrawList = nullptr;

        std::vector<PassAccess> graphAccesses;
        bool graphSideEffects = false;

        RenderPass& Reads(const std::string& resource, ResourceUsage usage = ResourceUsage::ShaderRead) { graphAccesses.push_back({resource, usage}); return *this; }
        RenderPass& Writes(const std::string& resource, ResourceUsage usage = ResourceUsage::StorageWrite) { graphAccesses.push_back({resource, usage}); return *this; }
    };
}
