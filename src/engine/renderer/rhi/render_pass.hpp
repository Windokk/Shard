#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"

namespace Shard::Engine::Rendering {

    using NumericValue = std::variant<bool, float, int, glm::vec2, glm::vec3, glm::vec4, glm::mat4>;

    struct RenderPass{
        std::shared_ptr<Framebuffer> target = nullptr;
        bool clearColor = true;
        bool clearDepth = true;
        std::map<std::string, NumericValue> customUniforms;
        std::map<std::string, uint64_t> customSamplers;
        bool overridePipeline = false;
        std::shared_ptr<Pipeline> customPipeline = nullptr;
        bool allowResize = true;
        bool allowCulling = true;
        // Name of a mat4 in customUniforms to frustum-cull against instead of the active camera - a shadow pass
        // sets "lightSpaceMatrix", so casters outside the light's view volume are never submitted. Only used
        // when allowCulling is false (the camera's frustum would be the wrong volume for such a pass). The
        // result matches what the GPU would clip anyway, it just spares the vertex work. Empty = no culling.
        std::string cullMatrixUniform;
        // Skipped entirely in Renderer::DrawFrame() when false - lets a pass stay registered (keeping
        // its dependents, target, draw list, etc. intact) while producing nothing this frame, e.g. the
        // editor hiding its probe-marker gizmos without touching the ProbeVolume components that
        // actually drive GI (which stay active either way).
        bool enabled = true;
        // Opt-in output caching (shadow maps) : Renderer::DrawFrame() hashes everything this pass's output
        // depends on (target, pipeline, customUniforms, and the contents of its draw list) and skips the
        // whole pass - clear included - when that hash is identical to the one from the last frame it
        // actually ran, leaving the target's previous contents in place. Only valid for a pass whose output
        // is a pure function of those inputs : with allowCulling on, the active camera's frustum would also
        // decide what gets drawn, and the hash doesn't see it. Leave false for anything else.
        bool cacheOutput = false;
        uint64_t cachedStateHash = 0;
        bool hasCachedState = false;
        // Issued once, right after this pass finishes executing (Renderer::EndRenderPass) - needed only
        // when something this pass wrote is later read in a way the driver can't track through normal
        // bind-point synchronization (e.g. a bindless texture handle sampled by a later pass, mirroring
        // why DispatchCompute takes the same MemoryBarrierBit for image-load-store writes read by a
        // subsequent draw). None for every ordinary pass.
        MemoryBarrierBit barrierAfter = MemoryBarrierBit::None;
        std::vector<DrawCommand> drawList = {};
        std::unordered_map<uint64_t, size_t> drawCommandsLookup;
        std::vector<DrawCommand>* externalDrawList = nullptr;
    };
}
