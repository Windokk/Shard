#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/renderer/rhi/renderer_api.hpp"

namespace Shard::Engine::Rendering {

    enum class ResourceUsage : uint8_t
    {
        None = 0,
        ColorTarget,
        DepthTarget,
        ShaderRead,
        StorageRead,
        StorageWrite,
        TransferSrc,
        TransferDst,
        IndirectArgs,
        VertexInput,
        Present
    };

    const char* ResourceUsageName(ResourceUsage usage);
    bool IsWriteUsage(ResourceUsage usage);

    MemoryBarrierBit BarrierBitsFor(ResourceUsage from, ResourceUsage to);

    struct PassAccess
    {
        std::string resource;
        ResourceUsage usage = ResourceUsage::None;
    };

    struct GraphResourceDesc
    {
        std::string name;

        // Owned elsewhere (viewport, swapchain, persistent shadow atlas): always live, never aliased, writers never skipped.
        bool imported = false;

        bool isBuffer = false;

        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t layers = 1;
        uint32_t samples = 1;
        uint32_t bytesPerTexel = 0;
        bool mipmapped = false;

        uint64_t bufferBytes = 0;

        uint64_t SizeBytes() const;
    };

    struct GraphPassDesc
    {
        std::string name;
        std::vector<PassAccess> accesses;

        std::vector<std::string> after;

        bool sideEffects = false;

        bool enabled = true;
    };

    struct GraphBarrier
    {
        std::string resource;
        ResourceUsage from = ResourceUsage::None;
        ResourceUsage to = ResourceUsage::None;
    };

    struct CompiledPass
    {
        std::string name;
        int declarationIndex = 0;
        bool culled = false;
        bool disabled = false;
        std::vector<GraphBarrier> barriersBefore;
        MemoryBarrierBit memoryBarrierBefore = MemoryBarrierBit::None;
    };

    struct ResourceLifetime
    {
        std::string name;
        bool imported = false;
        int firstPass = -1;
        int lastPass = -1;
        int aliasSlot = -1;
        uint64_t sizeBytes = 0;
    };

    struct AliasSlot
    {
        uint64_t sizeBytes = 0;
        std::vector<std::string> resources;
    };

    struct CompiledGraph
    {
        bool valid = false;
        std::string error;

        std::vector<CompiledPass> passes;
        std::vector<int> order;
        std::vector<ResourceLifetime> resources;
        std::vector<AliasSlot> aliasSlots;

        uint64_t transientBytesWithoutAliasing = 0;
        uint64_t transientBytesWithAliasing = 0;

        const CompiledPass* Find(const std::string& name) const;
        int CulledCount() const;
    };

    // Pure analysis, no GPU. Producers of a resource run in declaration order, readers after the last producer, `after` edges on top.
    // A pass no root needs is culled (roots: side effects, imported writes, no declarations).
    class RenderGraph
    {
        public:

            void Clear();

            void DeclareResource(const GraphResourceDesc& desc);

            void AddPass(const GraphPassDesc& pass);
            void RemovePass(const std::string& name);

            bool HasPass(const std::string& name) const;
            size_t PassCount() const { return m_Passes.size(); }

            CompiledGraph Compile() const;

            static std::string ToText(const CompiledGraph& graph, const RenderGraph& source);
            static std::string ToDot(const CompiledGraph& graph, const RenderGraph& source);

            const std::vector<GraphPassDesc>& GetPasses() const { return m_Passes; }
            const GraphResourceDesc* FindResource(const std::string& name) const;

        private:

            std::vector<GraphPassDesc> m_Passes;
            std::unordered_map<std::string, size_t> m_PassIndex;
            std::unordered_map<std::string, GraphResourceDesc> m_Resources;
    };

}
