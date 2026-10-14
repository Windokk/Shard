#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/renderer/rhi/render_snapshot.hpp"
#include "engine/renderer/rhi/render_pass.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"

namespace Shard::Engine::Rendering {

    class Renderer;

    /// @brief How a frame is built from the scene : which passes there are, what they draw into, which draw goes to which. The renderer owns the
    /// parts every pipeline shares (the shadow maps, the SSAO, the HDR scene target, the picture it is resolved into, the overlays), a pipeline adds
    /// and removes the passes that are its own and tells the renderer where the scene's draws go.
    ///
    /// A pipeline is picked by name (`render.pipeline`, the project's rendering settings) and swapped at run time : the passes of the one that goes
    /// are removed, the ones of the new one registered, and the scene submits its draws again.
    class RenderPipeline
    {
        public:

            virtual ~RenderPipeline() = default;

            virtual const char* GetType() const = 0;

            /// Registers the passes of the pipeline (and sets up the passes the renderer owns for it). The graphics thread.
            virtual void Activate(Renderer& renderer) = 0;

            /// Takes them away again
            virtual void Deactivate(Renderer& renderer) = 0;

            /// Once per frame, after the scene was synchronised and before anything is drawn : what the pipeline's passes need of the frame (the
            /// camera's matrices, the textures its passes read)
            virtual void BeforeFrame(Renderer& renderer, const RenderSnapshot& snapshot) { (void)renderer; (void)snapshot; }

            /// The draw lists of the renderer were emptied (a world was reloaded) : what the pipeline keeps in them goes back
            virtual void OnPassesCleared(Renderer& renderer) { (void)renderer; }

            /// The pass a draw command goes to when the scene submits it to `requested` (the scene always asks for "ForwardPass")
            virtual std::string Route(const std::string& requested, const DrawCommand& command) const
            {
                (void)command;
                return requested;
            }

            /// The passes to look into to remove what was submitted to `requested`
            virtual void RouteRemoval(const std::string& requested, std::vector<std::string>& passes) const { passes.push_back(requested); }

            /// Changes the dependencies the passes were registered with before the render graph is built (a pass that has to come after one of the pipeline's)
            virtual void AdjustDependencies(std::unordered_map<std::string, std::vector<std::string>>& dependencies) const { (void)dependencies; }

            /// Whether the pipeline draws the frame in this debug view. A view that it does not (the normals, the wireframe...) is drawn by the forward
            /// pipeline : the renderer switches to it as long as the view is on.
            virtual bool SupportsView(const DebugViewState& view) const { (void)view; return true; }
    };

    /// What the registry knows of a pipeline : a type (the code that builds it) and, when it comes from a `.renderpipeline` file, the asset it is
    struct RenderPipelineInfo
    {
        std::string name;               ///< what it is picked by : the file name without its extension, or the type for a built-in one
        std::string type;
        std::string description;
        std::string path;               ///< the file, empty for a built-in
        nlohmann::json settings;        ///< the asset's "settings" object, given to the pipeline when it is made
    };

    /// @brief The render pipelines that can be picked : the types the engine (and the game modules) register, and the assets that name one of them.
    ///
    /// An asset is a small JSON file (`pipelines/deferred.renderpipeline`) :
    ///     { "type": "deferred", "description": "...", "settings": { } }
    /// The engine's own are in engine_resources/pipelines, a project may have more in its resources.
    class RenderPipelineRegistry
    {
        public:

            using Factory = std::function<std::unique_ptr<RenderPipeline>(const nlohmann::json& settings)>;

            static RenderPipelineRegistry& Get();

            /// A kind of pipeline. Registering a type again replaces it.
            void RegisterType(const std::string& type, const std::string& description, Factory factory);

            /// Reads the `.renderpipeline` files of a directory (not recursive)
            void ScanDirectory(const std::string& directory);

            /// Every pipeline that can be picked : the assets, then the types no asset names
            std::vector<RenderPipelineInfo> List() const;

            std::unique_ptr<RenderPipeline> Create(const std::string& name) const;

            bool Has(const std::string& name) const;

        private:

            struct TypeEntry
            {
                std::string description;
                Factory factory;
            };

            std::unordered_map<std::string, TypeEntry> m_Types;
            std::vector<RenderPipelineInfo> m_Assets;
    };

    /// Registers the pipelines of the engine (forward+, deferred). Called once by the renderer.
    void RegisterBuiltinRenderPipelines();

}
