#pragma once

#include <set>
#include <string>
#include <vector>

#include "engine/plugins/manifest.hpp"

namespace Shard::Engine::Plugins {

    /// Why a candidate was left out
    struct Rejection {
        enum class Kind : uint8_t {
            WrongHost,          ///< not meant for this executable : normal, not worth a warning
            Duplicate,          ///< another version of the same plugin was picked
            Incompatible,       ///< engine version, API version, platform or required engine feature
            MissingDependency,
            Conflict,
            Cycle,
            Disabled,           ///< the project turned it off
            Untrusted,          ///< its signature is bad, or it is not signed by a publisher the project trusts
        };
        std::string id;
        std::string version;
        Kind kind = Kind::Incompatible;
        std::string reason;
    };

    struct ResolveOptions {
        Host host = Host::Player;
        SemVer engineVersion;
        /// Features of this engine build, for `engine.requires` : "largeWorld", "determinism", "headless"...
        std::set<std::string> engineFeatures;
        /// Plugins the project does not want (its "enabled": false entries)
        std::set<std::string> disabled;
        /// Plugins whose loss is an error, not a warning (the project marks them required)
        std::set<std::string> required;
        /// Ranges the project asks for per plugin id ("^1.4" in the project's list) ; checked when several versions are available
        std::vector<std::pair<std::string, VersionRange>> projectRanges;
    };

    struct ResolveResult {
        /// Indices into the candidate list, in load order
        std::vector<size_t> order;
        std::vector<Rejection> rejected;
        /// Required plugins that could not be loaded, and why : the host should stop
        std::vector<std::string> errors;
        /// service name -> id of the plugin that provides it (among the accepted)
        std::vector<std::pair<std::string, std::string>> services;
        bool Ok() const { return errors.empty(); }
    };

    /// @brief Decides which of the discovered plugins load, and in which order :
    ///   * one version per id (the highest one the project accepts), the wrong host / engine / API / platform dropped,
    ///   * plugins whose dependencies cannot be met dropped (and, one after the other, whatever depended on them),
    ///   * conflicting plugins : the one that sorts last by id is dropped, unless the project requires it,
    ///   * a load order where dependencies, loadAfter / loadBefore come first, ties broken by id : never by the order files were found in.
    /// A dependency written "service:<name>" is met by any accepted plugin that provides that service.
    ResolveResult ResolvePlugins(const std::vector<PluginManifest>& candidates, const ResolveOptions& options);
}
