#pragma once

#include <set>
#include <string>
#include <vector>

#include "engine/plugins/manifest.hpp"

namespace Shard::Engine::Plugins {

    struct Rejection {
        enum class Kind : uint8_t {
            WrongHost,
            Duplicate,
            Incompatible,
            MissingDependency,
            Conflict,
            Cycle,
            Disabled,
            Untrusted,
        };
        std::string id;
        std::string version;
        Kind kind = Kind::Incompatible;
        std::string reason;
    };

    struct ResolveOptions {
        Host host = Host::Player;
        SemVer engineVersion;
        std::set<std::string> engineFeatures;
        std::set<std::string> disabled;
        std::set<std::string> required;
        std::vector<std::pair<std::string, VersionRange>> projectRanges;
    };

    struct ResolveResult {
        std::vector<size_t> order;
        std::vector<Rejection> rejected;
        std::vector<std::string> errors;
        std::vector<std::pair<std::string, std::string>> services;
        bool Ok() const { return errors.empty(); }
    };

    // Load order ties are broken by id, never by discovery order. Of conflicting plugins the id that sorts last is dropped unless the project requires it.
    ResolveResult ResolvePlugins(const std::vector<PluginManifest>& candidates, const ResolveOptions& options);
}
