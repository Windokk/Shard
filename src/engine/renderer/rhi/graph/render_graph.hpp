#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/renderer/rhi/renderer_api.hpp"

namespace Shard::Engine::Rendering {

    /// @brief What a pass does with a resource of the render graph. The graph derives the order of the passes, the ones that
    /// can be skipped and the barriers between two of them from these declarations alone : nothing here knows a graphics API.
    enum class ResourceUsage : uint8_t
    {
        None = 0,
        ColorTarget,        ///< written as a colour attachment
        DepthTarget,        ///< written (or tested) as the depth attachment
        ShaderRead,         ///< sampled by a shader
        StorageRead,        ///< read through a storage buffer / image
        StorageWrite,       ///< written through a storage buffer / image (compute, image store)
        TransferSrc,
        TransferDst,
        IndirectArgs,       ///< indirect draw / dispatch arguments
        VertexInput,        ///< vertex / index data
        Present             ///< handed to the display
    };

    const char* ResourceUsageName(ResourceUsage usage);
    bool IsWriteUsage(ResourceUsage usage);

    /// The bits of the API's own barrier that make what `from` wrote visible to `to` (what a backend that has no per resource
    /// barriers, OpenGL, issues between two passes). None when the API tracks that dependency itself.
    MemoryBarrierBit BarrierBitsFor(ResourceUsage from, ResourceUsage to);

    /// @brief What a pass says about one resource it touches.
    struct PassAccess
    {
        std::string resource;
        ResourceUsage usage = ResourceUsage::None;
    };

    /// @brief A resource of the graph. Declaring one is optional : a name used by a pass without a description is a resource of unknown size.
    struct GraphResourceDesc
    {
        std::string name;

        /// Owned by somebody else (the viewport, the swapchain, a shadow atlas that outlives the frame) : always live, never aliased,
        /// and a pass that writes one is never skipped (somebody looks at the result).
        bool imported = false;

        bool isBuffer = false;

        /// Textures
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t layers = 1;
        uint32_t samples = 1;
        uint32_t bytesPerTexel = 0;
        bool mipmapped = false;

        /// Buffers
        uint64_t bufferBytes = 0;

        uint64_t SizeBytes() const;
    };

    /// @brief One pass as the graph sees it : a name and what it reads and writes.
    struct GraphPassDesc
    {
        std::string name;
        std::vector<PassAccess> accesses;

        /// Passes that have to run before this one whatever the resources say (the dependencies of RenderPass registration)
        std::vector<std::string> after;

        /// Never skipped, even if nothing reads what it writes (a pass that declares no resource is treated the same way : the graph
        /// has no way to tell what it is for)
        bool sideEffects = false;

        /// A pass that is not enabled is not part of the graph : it is left out, and so is whatever only it fed
        bool enabled = true;
    };

    /// A change of use of a resource between two passes. A backend turns it into a pipeline barrier (and an image layout
    /// transition), or into the closest memory barrier it has.
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
        bool culled = false;            ///< nothing that matters reads what this pass writes
        bool disabled = false;
        std::vector<GraphBarrier> barriersBefore;
        MemoryBarrierBit memoryBarrierBefore = MemoryBarrierBit::None;      ///< BarrierBitsFor of every barrier above, combined
    };

    struct ResourceLifetime
    {
        std::string name;
        bool imported = false;
        int firstPass = -1;             ///< position in the execution order of the first pass that touches it (-1 : none does)
        int lastPass = -1;
        int aliasSlot = -1;             ///< transient resources that share a slot never live at the same time
        uint64_t sizeBytes = 0;
    };

    /// Memory that several transient resources take turns in
    struct AliasSlot
    {
        uint64_t sizeBytes = 0;
        std::vector<std::string> resources;
    };

    struct CompiledGraph
    {
        bool valid = false;
        std::string error;                              ///< why it is not valid (a cycle...)

        std::vector<CompiledPass> passes;               ///< in declaration order, culled ones included
        std::vector<int> order;                         ///< indices into `passes` : what runs, in the order it runs
        std::vector<ResourceLifetime> resources;
        std::vector<AliasSlot> aliasSlots;

        uint64_t transientBytesWithoutAliasing = 0;
        uint64_t transientBytesWithAliasing = 0;

        const CompiledPass* Find(const std::string& name) const;
        int CulledCount() const;
    };

    /// @brief The declarative render graph : passes say which resources they read and write, Compile() gives back the order they
    /// run in, which of them can be skipped, the barriers between them and which transient resources can share memory.
    ///
    /// Pure analysis, no GPU : the same graph compiles in a headless run, in a test, and under every backend, which is what turns
    /// the declarations into pipeline barriers (Vulkan) or into glMemoryBarrier calls (OpenGL).
    ///
    /// Rules, in the order they apply :
    ///   - a resource is produced by the passes that write it (read-write counts), in the order they were declared, one after the other ;
    ///   - the passes that only read it come after the last producer ;
    ///   - `after` edges come on top of that ;
    ///   - a pass that is not needed by any root (a pass with side effects, a pass that writes an imported resource, a pass that
    ///     declares nothing) is culled ;
    ///   - a resource that is read and never written by a live pass is an input of the frame (nothing is ordered before it).
    class RenderGraph
    {
        public:

            void Clear();

            /// Describes a resource (size, kind). Calling it again replaces the description.
            void DeclareResource(const GraphResourceDesc& desc);

            /// Adds a pass, after the ones declared so far. A pass with a name that exists replaces it, keeping its place.
            void AddPass(const GraphPassDesc& pass);
            void RemovePass(const std::string& name);

            bool HasPass(const std::string& name) const;
            size_t PassCount() const { return m_Passes.size(); }

            CompiledGraph Compile() const;

            /// A text listing (the passes in order, what each reads and writes, the barriers, the culled passes, the aliasing)
            static std::string ToText(const CompiledGraph& graph, const RenderGraph& source);
            /// Graphviz : passes as boxes, resources as ellipses, culled passes greyed out
            static std::string ToDot(const CompiledGraph& graph, const RenderGraph& source);

            const std::vector<GraphPassDesc>& GetPasses() const { return m_Passes; }
            const GraphResourceDesc* FindResource(const std::string& name) const;

        private:

            std::vector<GraphPassDesc> m_Passes;
            std::unordered_map<std::string, size_t> m_PassIndex;
            std::unordered_map<std::string, GraphResourceDesc> m_Resources;
    };

}
