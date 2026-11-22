#include "collision_diagnostics.hpp"

#include <algorithm>

#include "engine/core/collision/collision_classes.hpp"

namespace Shard::Engine::Collision {

    const char* ToString(DiagnosticKind kind)
    {
        switch (kind) {
            case DiagnosticKind::DeadRule: return "Dead rule";
            case DiagnosticKind::ShadowedRule: return "Shadowed rule";
            case DiagnosticKind::IsolatedProfile: return "Isolated profile";
            case DiagnosticKind::OneSidedPair: return "One sided pair";
            case DiagnosticKind::ImpossibleListener: return "Impossible listener";
            case DiagnosticKind::BrokenProfile: return "Broken profile";
        }
        return "";
    }

    namespace {
        struct Resolved {
            ResolvedProfile profile;
            size_t ownRules = 0;
            ClassId moving = kInvalidClass;
            ClassId fixed = kInvalidClass;
        };

        struct Context {
            ClassTable table{ 1u << 20 };
            std::vector<Resolved> profiles;
        };

        void Build(const ProfileRegistry& registry, Context& ctx, std::vector<Diagnostic>* out)
        {
            for (const Profile* profile : registry.All()) {
                Resolved r;
                std::string error;
                if (!registry.Resolve(profile->name, r.profile, &error)) {
                    if (out) out->push_back({ DiagnosticKind::BrokenProfile, profile->name, {}, -1, error });
                    continue;
                }
                r.ownRules = profile->rules.size();
                r.moving = ctx.table.Acquire(r.profile, true);
                r.fixed = ctx.table.Acquire(r.profile, false);
                ctx.profiles.push_back(std::move(r));
            }
        }

        bool Participates(const ResolvedProfile& p)
        {
            return p.mode == Mode::Solid || p.mode == Mode::Sensor;
        }
    }

    std::vector<Diagnostic> Analyze(const ProfileRegistry& registry, const std::vector<ListenerSpec>& listeners)
    {
        std::vector<Diagnostic> out;
        Context ctx;
        Build(registry, ctx, &out);

        for (const Resolved& r : ctx.profiles) {
            const ClassInfo& info = ctx.table.Get(r.moving);
            std::vector<std::vector<size_t>> matches(info.rules.size());
            for (size_t i = 0; i < info.rules.size(); ++i) {
                if (info.dynamicRule[i])
                    continue;
                for (size_t q = 0; q < ctx.profiles.size(); ++q)
                    if (info.rules[i].target.Matches(ctx.profiles[q].profile.identity))
                        matches[i].push_back(q);
            }

            for (size_t i = 0; i < r.ownRules && i < info.rules.size(); ++i) {
                if (info.dynamicRule[i])
                    continue;
                const std::string text = info.rules[i].target.ToString();
                if (matches[i].empty()) {
                    out.push_back({ DiagnosticKind::DeadRule, r.profile.name, {}, int(i + 1),
                        "rule " + std::to_string(i + 1) + " (" + text + ") matches no collision identity in the project" });
                    continue;
                }
                for (size_t j = 0; j < i; ++j) {
                    if (info.dynamicRule[j] || matches[j].empty())
                        continue;
                    const bool covered = std::includes(matches[j].begin(), matches[j].end(), matches[i].begin(), matches[i].end());
                    if (covered) {
                        out.push_back({ DiagnosticKind::ShadowedRule, r.profile.name, {}, int(i + 1),
                            "rule " + std::to_string(i + 1) + " (" + text + ") never applies : rule " + std::to_string(j + 1) + " (" +
                            info.rules[j].target.ToString() + ") matches first" });
                        break;
                    }
                }
            }
        }

        for (const Resolved& r : ctx.profiles) {
            if (!Participates(r.profile))
                continue;
            bool any = false;
            for (const Resolved& q : ctx.profiles) {
                if (!Participates(q.profile))
                    continue;
                for (ClassId a : { r.moving, r.fixed })
                    for (ClassId b : { q.moving, q.fixed })
                        if (ctx.table.PairResponse(a, b) != Response::Ignore)
                            any = true;
            }
            if (!any)
                out.push_back({ DiagnosticKind::IsolatedProfile, r.profile.name, {}, -1, "profile '" + r.profile.name + "' collides with nothing" });
        }

        for (size_t i = 0; i < ctx.profiles.size(); ++i) {
            for (size_t j = i + 1; j < ctx.profiles.size(); ++j) {
                const Resolved& p = ctx.profiles[i];
                const Resolved& q = ctx.profiles[j];
                if (!Participates(p.profile) || !Participates(q.profile))
                    continue;
                const Response pq = ctx.table.SideResponse(p.moving, q.moving);
                const Response qp = ctx.table.SideResponse(q.moving, p.moving);
                if ((pq == Response::Ignore) != (qp == Response::Ignore)) {
                    const Resolved& active = pq == Response::Ignore ? q : p;
                    const Resolved& passive = pq == Response::Ignore ? p : q;
                    out.push_back({ DiagnosticKind::OneSidedPair, active.profile.name, passive.profile.name, -1,
                        "'" + active.profile.name + "' reacts to '" + passive.profile.name + "' but '" + passive.profile.name +
                        "' ignores it, so the pair never interacts" });
                }
            }
        }

        for (const ListenerSpec& listener : listeners) {
            std::vector<Diagnostic> found = AnalyzeListener(registry, listener);
            out.insert(out.end(), found.begin(), found.end());
        }
        return out;
    }

    std::vector<Diagnostic> AnalyzeListener(const ProfileRegistry& registry, const ListenerSpec& listener)
    {
        std::vector<Diagnostic> out;
        Context ctx;
        Build(registry, ctx, nullptr);

        const Response wanted = listener.kind == EventKind::Hit ? Response::Block : Response::Overlap;
        const std::string against = listener.other.IsEmpty() ? "anything" : "'" + listener.other.ToString() + "'";
        const std::string who = listener.owner.empty() ? std::string("a listener") : "'" + listener.owner + "'";
        bool anyProfile = false;
        bool anyPossible = false;
        for (const Resolved& p : ctx.profiles) {
            if (!listener.profile.empty() && p.profile.name != listener.profile)
                continue;
            if (!Participates(p.profile))
                continue;
            anyProfile = true;
            bool possible = false;
            for (const Resolved& q : ctx.profiles) {
                if (!listener.other.IsEmpty() && !listener.other.Matches(q.profile.identity))
                    continue;
                for (ClassId a : { p.moving, p.fixed })
                    for (ClassId b : { q.moving, q.fixed })
                        if (ctx.table.PairResponse(a, b) == wanted)
                            possible = true;
            }
            anyPossible = anyPossible || possible;
            if (!possible && !listener.profile.empty())
                out.push_back({ DiagnosticKind::ImpossibleListener, p.profile.name, {}, -1,
                    who + " waits for " + ToString(listener.kind) + " between '" + p.profile.name + "' and " + against + ", which the rules never produce" });
        }
        if (listener.profile.empty() && anyProfile && !anyPossible)
            out.push_back({ DiagnosticKind::ImpossibleListener, {}, {}, -1,
                who + " waits for " + ToString(listener.kind) + " with " + against + ", which no profile produces" });
        return out;
    }
}
