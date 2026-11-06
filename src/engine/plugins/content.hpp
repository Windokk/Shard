#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/core/containers/result.hpp"
#include "engine/core/math/guid.hpp"
#include "engine/plugins/hooks.hpp"
#include "engine/plugins/manifest.hpp"

namespace Shard::Engine::Plugins {

    struct ContentConflict {
        std::string logicalPath;
        std::string keptFrom;
        std::string rejectedFrom;
    };

    // Content folders of loaded modules as one layered tree. mod://<id>/ reaches any file. By logical path, a later layer wins only if it lists the path (or "*") in content.overrides, else the clash is reported.
    class ContentMounts {
    public:
        struct MountPoint {
            std::string pluginId;
            std::string ns;
            std::string root;
            PluginOwner owner = 0;
            ModuleKind kind = ModuleKind::None;
            std::vector<std::string> overrides;
        };

        bool Mount(const PluginManifest& manifest, PluginOwner owner, std::string* error = nullptr);
        bool MountFolder(const std::string& id, const std::string& folder, PluginOwner owner, std::vector<std::string> overrides = {});
        size_t Unmount(PluginOwner owner);
        void Clear();

        const std::vector<MountPoint>& Mounts() const { return m_Mounts; }
        const MountPoint* Find(const std::string& pluginId) const;

        static bool IsModUri(const std::string& uri);
        static bool ParseModUri(const std::string& uri, std::string& pluginId, std::string& path);
        static std::string MakeModUri(const std::string& pluginId, const std::string& path);

        std::optional<std::string> Resolve(const std::string& uri) const;
        std::optional<std::string> ReadText(const std::string& uri) const;
        bool Exists(const std::string& uri) const { return Resolve(uri).has_value(); }

        std::vector<std::string> List(const std::string& pluginId, const std::string& subdir = {}) const;

        struct Resolved {
            std::string pluginId;
            std::string file;
            std::string uri;
        };
        std::optional<Resolved> ResolveLogical(const std::string& logicalPath) const;
        std::vector<std::string> ListLogical(const std::string& subdir = {}) const;
        std::vector<ContentConflict> FindConflicts() const;

        static Core::Guid AssetGuid(const std::string& assetNamespace, const std::string& path);
        Core::Guid AssetGuid(const std::string& pluginId, const std::string& path, bool* found) const;

        nlohmann::json LoadTable(const std::string& name) const;
        std::vector<std::string> TableNames() const;
        nlohmann::json LoadLanguage(const std::string& language) const;
        std::string Translate(const std::string& key, const std::string& language, const std::string& fallback = {}) const;

    private:
        static std::string Normalize(const std::string& path);
        bool Overrides(const MountPoint& mount, const std::string& logicalPath) const;

        std::vector<MountPoint> m_Mounts;
    };

    std::vector<std::string> ValidateContentFolder(const std::string& contentRoot);

    std::vector<std::string> ValidateWorldJson(const nlohmann::json& world, const std::string& name);

    struct DeclaredField {
        std::string name;
        std::string type;
        nlohmann::json defaultValue;
    };
    struct DeclaredComponent {
        std::string name;
        std::vector<DeclaredField> fields;
    };
    Core::Result<DeclaredComponent> ParseDeclaredComponent(const nlohmann::json& json);

    // World patch: { world, ops: [addActor | removeActor | setComponent | addComponent | removeComponent | setSettings] }. Actors found by '/'-joined names. All or nothing.
    Core::Result<bool> ApplyWorldPatch(nlohmann::json& world, const nlohmann::json& patch);
}
