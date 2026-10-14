#include "render_pipeline.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Rendering {

    RenderPipelineRegistry& RenderPipelineRegistry::Get()
    {
        static RenderPipelineRegistry registry;
        return registry;
    }

    void RenderPipelineRegistry::RegisterType(const std::string& type, const std::string& description, Factory factory)
    {
        m_Types[type] = TypeEntry{description, std::move(factory)};
    }

    void RenderPipelineRegistry::ScanDirectory(const std::string& directory)
    {
        std::error_code ec;
        if (directory.empty() || !std::filesystem::is_directory(directory, ec))
            return;

        for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".renderpipeline")
                continue;

            std::ifstream file(entry.path(), std::ios::binary);
            nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
            if (json.is_discarded() || !json.is_object() || !json.contains("type") || !json["type"].is_string())
            {
                DEBUG_WARNING("Render pipeline asset ignored (not a JSON object with a type) : ", entry.path().string());
                continue;
            }

            RenderPipelineInfo info;
            info.name = entry.path().stem().string();
            info.type = json["type"].get<std::string>();
            info.description = json.value("description", "");
            info.path = entry.path().generic_string();
            info.settings = json.contains("settings") && json["settings"].is_object() ? json["settings"] : nlohmann::json::object();

            // A file read again (a rescan, a hot reload) replaces what it said
            m_Assets.erase(std::remove_if(m_Assets.begin(), m_Assets.end(), [&](const RenderPipelineInfo& known) { return known.name == info.name; }), m_Assets.end());
            m_Assets.push_back(std::move(info));
        }
    }

    std::vector<RenderPipelineInfo> RenderPipelineRegistry::List() const
    {
        std::vector<RenderPipelineInfo> out = m_Assets;

        std::set<std::string> named;
        for (const RenderPipelineInfo& asset : m_Assets)
            named.insert(asset.type);

        for (const auto& [type, entry] : m_Types)
        {
            if (named.count(type))
                continue;

            RenderPipelineInfo info;
            info.name = type;
            info.type = type;
            info.description = entry.description;
            info.settings = nlohmann::json::object();
            out.push_back(std::move(info));
        }

        std::sort(out.begin(), out.end(), [](const RenderPipelineInfo& a, const RenderPipelineInfo& b) { return a.name < b.name; });
        return out;
    }

    bool RenderPipelineRegistry::Has(const std::string& name) const
    {
        for (const RenderPipelineInfo& asset : m_Assets)
        {
            if (asset.name == name)
                return m_Types.count(asset.type) != 0;
        }
        return m_Types.count(name) != 0;
    }

    std::unique_ptr<RenderPipeline> RenderPipelineRegistry::Create(const std::string& name) const
    {
        // An asset by that name, else a type
        std::string type = name;
        nlohmann::json settings = nlohmann::json::object();
        for (const RenderPipelineInfo& asset : m_Assets)
        {
            if (asset.name == name)
            {
                type = asset.type;
                settings = asset.settings;
                break;
            }
        }

        auto it = m_Types.find(type);
        if (it == m_Types.end())
            return nullptr;

        return it->second.factory(settings);
    }

}
