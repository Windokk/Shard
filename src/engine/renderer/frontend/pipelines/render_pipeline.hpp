#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/renderer/rhi/render_snapshot.hpp"
#include "engine/renderer/rhi/render_pass.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"

namespace Shard::Engine::Rendering {

    class Renderer;

    class RenderPipeline
    {
        public:

            virtual ~RenderPipeline() = default;

            virtual const char* GetType() const = 0;

            virtual void Activate(Renderer& renderer) = 0;

            virtual void Deactivate(Renderer& renderer) = 0;

            virtual void BeforeFrame(Renderer& renderer, const RenderSnapshot& snapshot) { (void)renderer; (void)snapshot; }

            virtual void OnPassesCleared(Renderer& renderer) { (void)renderer; }

            virtual std::string Route(const std::string& requested, const DrawCommand& command) const
            {
                (void)command;
                return requested;
            }

            virtual void RouteRemoval(const std::string& requested, std::vector<std::string>& passes) const { passes.push_back(requested); }

            virtual void AdjustDependencies(std::unordered_map<std::string, std::vector<std::string>>& dependencies) const { (void)dependencies; }

            virtual bool SupportsView(const DebugViewState& view) const { (void)view; return true; }
    };

    struct RenderPipelineInfo
    {
        std::string name;
        std::string type;
        std::string description;
        std::string path;
        nlohmann::json settings;
    };

    class RenderPipelineRegistry
    {
        public:

            using Factory = std::function<std::unique_ptr<RenderPipeline>(const nlohmann::json& settings)>;

            static RenderPipelineRegistry& Get();

            void RegisterType(const std::string& type, const std::string& description, Factory factory);

            bool UnregisterType(const std::string& type);

            void ScanDirectory(const std::string& directory);

            std::vector<RenderPipelineInfo> List() const;

            std::unique_ptr<RenderPipeline> Create(const std::string& name) const;

            bool Has(const std::string& name) const;

        private:

            struct TypeEntry
            {
                std::string description;
                Factory factory;
            };

            mutable std::mutex m_Mutex;
            std::unordered_map<std::string, TypeEntry> m_Types;
            std::vector<RenderPipelineInfo> m_Assets;
    };

    struct RenderPipelineExtension
    {
        std::string type;
        std::string description;
        RenderPipelineRegistry::Factory factory;
    };

    void RegisterBuiltinRenderPipelines();

}
