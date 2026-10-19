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

    /// A file two layers both provide, where the higher one did not say it replaces the lower one (content.overrides)
    struct ContentConflict {
        std::string logicalPath;
        std::string keptFrom;           ///< plugin id whose file is used
        std::string rejectedFrom;       ///< plugin id whose file is ignored
    };

    /// @brief The content folders of the loaded modules, as one layered file tree.
    ///
    /// Every module with content mounts its folder as `mod://<id>/` : a file is reachable by that address whatever else is loaded.
    /// The same files form a second view, by *logical path* (`materials/stone.mat`) : the layers are in load order, a later one
    /// wins over an earlier one only if it lists the path (or "*") in content.overrides. Otherwise the earlier one is kept and the
    /// clash is reported.
    class ContentMounts {
    public:
        struct MountPoint {
            std::string pluginId;
            std::string ns;                     ///< asset namespace
            std::string root;                   ///< absolute folder, no trailing '/'
            PluginOwner owner = 0;
            ModuleKind kind = ModuleKind::None;
            std::vector<std::string> overrides;
        };

        /// Layers are kept in the order they are mounted : mount in load order.
        bool Mount(const PluginManifest& manifest, PluginOwner owner, std::string* error = nullptr);
        /// Mounts a folder that is not a plugin's (the project's own resources, a test)
        bool MountFolder(const std::string& id, const std::string& folder, PluginOwner owner, std::vector<std::string> overrides = {});
        size_t Unmount(PluginOwner owner);
        void Clear();

        const std::vector<MountPoint>& Mounts() const { return m_Mounts; }
        const MountPoint* Find(const std::string& pluginId) const;

        // ---- mod:// addresses ----
        static bool IsModUri(const std::string& uri);
        /// "mod://com.acme.weather/materials/rain.mat" -> ("com.acme.weather", "materials/rain.mat")
        static bool ParseModUri(const std::string& uri, std::string& pluginId, std::string& path);
        static std::string MakeModUri(const std::string& pluginId, const std::string& path);

        /// The file a mod:// address names. nullopt if the plugin is not mounted or the file is missing. Never leaves the mount's folder.
        std::optional<std::string> Resolve(const std::string& uri) const;
        std::optional<std::string> ReadText(const std::string& uri) const;
        bool Exists(const std::string& uri) const { return Resolve(uri).has_value(); }

        /// The files under `subdir` of one module, relative paths with '/', sorted
        std::vector<std::string> List(const std::string& pluginId, const std::string& subdir = {}) const;

        // ---- Logical paths (layers) ----
        struct Resolved {
            std::string pluginId;
            std::string file;                   ///< absolute path
            std::string uri;
        };
        /// The file that wins for a logical path across all the layers
        std::optional<Resolved> ResolveLogical(const std::string& logicalPath) const;
        /// Every logical path any layer provides, sorted, once
        std::vector<std::string> ListLogical(const std::string& subdir = {}) const;
        std::vector<ContentConflict> FindConflicts() const;

        // ---- Ids ----
        /// @brief The AssetID-style GUID of a module's file : derived from "<namespace>/<path>", so it is the same on every machine
        /// and does not depend on when the file was first imported.
        static Core::Guid AssetGuid(const std::string& assetNamespace, const std::string& path);
        Core::Guid AssetGuid(const std::string& pluginId, const std::string& path, bool* found) const;

        // ---- Data (tables and translations) ----
        /// `tables/<name>.json` of every layer, merged in layer order : objects merge key by key, anything else is replaced by the later layer
        nlohmann::json LoadTable(const std::string& name) const;
        std::vector<std::string> TableNames() const;
        /// `lang/<language>.json` of every layer merged. Keys are free-form ("ui.start").
        nlohmann::json LoadLanguage(const std::string& language) const;
        std::string Translate(const std::string& key, const std::string& language, const std::string& fallback = {}) const;

    private:
        static std::string Normalize(const std::string& path);
        bool Overrides(const MountPoint& mount, const std::string& logicalPath) const;

        std::vector<MountPoint> m_Mounts;
    };

    // ---- Datapacks ----

    /// @brief Checks a datapack's (or any module's) content folder against the formats the engine knows :
    ///   worlds/*.world, prefabs/*.json, tables/*.json, lang/*.json, patches/*.patch, components/*.json must parse and have the right shape.
    /// Folders the engine does not know are fine (assets, game specific data). Returns one line per problem.
    std::vector<std::string> ValidateContentFolder(const std::string& contentRoot);

    /// The problems of a world's JSON : the shape of its actors, their components and its settings. `name` is only for the messages.
    std::vector<std::string> ValidateWorldJson(const nlohmann::json& world, const std::string& name);

    /// A component made only of fields, declared by a datapack : components/<name>.json
    struct DeclaredField {
        std::string name;
        std::string type;               ///< bool, int, float, string, vec3, color
        nlohmann::json defaultValue;
    };
    struct DeclaredComponent {
        std::string name;
        std::vector<DeclaredField> fields;
    };
    Core::Result<DeclaredComponent> ParseDeclaredComponent(const nlohmann::json& json);

    /// @brief Applies a world patch (patches/*.patch) to a world file's JSON :
    ///   { "world": "sponza.world", "ops": [
    ///       { "op": "addActor",        "parent": "Room/Table" (optional), "actor": { "name": "Lamp", "components": [...] } },
    ///       { "op": "removeActor",     "path": "Room/Chair" },
    ///       { "op": "setComponent",    "path": "Room/Lamp", "type": "light", "fields": { "intensity": 3.0 } },
    ///       { "op": "addComponent",    "path": "Room/Lamp", "component": { "type": "..." } },
    ///       { "op": "removeComponent", "path": "Room/Lamp", "type": "light" },
    ///       { "op": "setSettings",     "settings": { "ambient_color": [0.1, 0.1, 0.1] } } ] }
    /// Actors are found by their names, separated by '/'. Nothing is applied if an op is wrong : the world is left as it was.
    Core::Result<bool> ApplyWorldPatch(nlohmann::json& world, const nlohmann::json& patch);
}
