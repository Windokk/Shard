#pragma once

#include <string>
#include <vector>

#include "engine/core/collision/collision_profile.hpp"

namespace Shard::Engine::Collision {

    enum class DiagnosticKind : uint8_t {
        DeadRule,
        ShadowedRule,
        IsolatedProfile,
        OneSidedPair,
        ImpossibleListener,
        BrokenProfile,
    };

    struct Diagnostic {
        DiagnosticKind kind = DiagnosticKind::DeadRule;
        std::string profile;
        std::string other;
        int rule = -1;
        std::string message;
    };

    struct ListenerSpec {
        std::string owner;
        std::string profile;
        EventKind kind = EventKind::OverlapBegin;
        Core::TagQuery other;
    };

    const char* ToString(DiagnosticKind kind);

    std::vector<Diagnostic> Analyze(const ProfileRegistry& registry, const std::vector<ListenerSpec>& listeners = {});
    std::vector<Diagnostic> AnalyzeListener(const ProfileRegistry& registry, const ListenerSpec& listener);
}
