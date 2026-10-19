#include "engine/plugins/resolver.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <queue>

namespace Shard::Engine::Plugins {

    namespace {
        constexpr const char* kServicePrefix = "service:";

        bool IsServiceDep(const std::string& id) { return id.compare(0, 8, kServicePrefix) == 0; }
        std::string ServiceName(const std::string& id) { return id.substr(8); }
    }

    ResolveResult ResolvePlugins(const std::vector<PluginManifest>& candidates, const ResolveOptions& options)
    {
        ResolveResult result;
        const size_t n = candidates.size();
        std::vector<bool> alive(n, true);

        auto reject = [&](size_t i, Rejection::Kind kind, std::string reason) {
            if (!alive[i]) return;
            alive[i] = false;
            result.rejected.push_back({ candidates[i].id, candidates[i].version.ToString(), kind, std::move(reason) });
            if (options.required.count(candidates[i].id) && kind != Rejection::Kind::WrongHost && kind != Rejection::Kind::Duplicate && kind != Rejection::Kind::Disabled)
                result.errors.push_back("required plugin '" + candidates[i].id + "' cannot be loaded : " + result.rejected.back().reason);
        };

        // 1. Filters that look at a plugin alone
        for (size_t i = 0; i < n; ++i) {
            const PluginManifest& m = candidates[i];
            if (options.disabled.count(m.id)) { reject(i, Rejection::Kind::Disabled, "turned off by the project"); continue; }
            if (!m.RunsOn(options.host)) { reject(i, Rejection::Kind::WrongHost, std::string("not for the ") + HostName(options.host) + " host"); continue; }
            if (!m.engineVersions.Satisfies(options.engineVersion)) {
                reject(i, Rejection::Kind::Incompatible, "needs engine " + m.engineVersions.Text() + ", this is " + options.engineVersion.ToString());
                continue;
            }
            if (m.apiMajor != kPluginApiMajor || m.apiMinor > kPluginApiMinor) {
                reject(i, Rejection::Kind::Incompatible, "built for plugin API " + std::to_string(m.apiMajor) + "." + std::to_string(m.apiMinor) +
                       ", this engine offers " + std::to_string(kPluginApiMajor) + "." + std::to_string(kPluginApiMinor));
                continue;
            }
            bool featuresOk = true;
            for (const std::string& f : m.engineRequires) {
                if (!options.engineFeatures.count(f)) {
                    reject(i, Rejection::Kind::Incompatible, "needs the engine feature '" + f + "', which this build does not have");
                    featuresOk = false;
                    break;
                }
            }
            if (!featuresOk) continue;
            if (m.linkage == Linkage::Dynamic && m.moduleKind != ModuleKind::Datapack && m.BinaryForThisPlatform().empty()) {
                reject(i, Rejection::Kind::Incompatible, "no binary for " + CurrentPlatformKey());
                continue;
            }
        }

        // 2. One version per id : the highest one the project's range accepts (or the highest, without a range)
        {
            std::map<std::string, std::vector<size_t>> byId;
            for (size_t i = 0; i < n; ++i) if (alive[i]) byId[candidates[i].id].push_back(i);
            for (auto& [id, indices] : byId) {
                if (indices.size() < 2) continue;
                const VersionRange* range = nullptr;
                for (const auto& pr : options.projectRanges) if (pr.first == id) range = &pr.second;
                size_t best = SIZE_MAX;
                for (size_t i : indices) {
                    if (range && !range->Satisfies(candidates[i].version)) continue;
                    if (best == SIZE_MAX || candidates[i].version > candidates[best].version) best = i;
                }
                if (best == SIZE_MAX) best = indices[0];       // none fits the project : the range check below reports it
                for (size_t i : indices)
                    if (i != best) reject(i, Rejection::Kind::Duplicate, "version " + candidates[best].version.ToString() + " of the same plugin was picked");
            }
        }
        // The project's own version requirement
        for (const auto& [id, range] : options.projectRanges) {
            for (size_t i = 0; i < n; ++i)
                if (alive[i] && candidates[i].id == id && !range.Satisfies(candidates[i].version))
                    reject(i, Rejection::Kind::Incompatible, "the project asks for " + range.Text() + ", found " + candidates[i].version.ToString());
        }

        auto find = [&](const std::string& id) -> int {
            for (size_t i = 0; i < n; ++i) if (alive[i] && candidates[i].id == id) return int(i);
            return -1;
        };
        auto provider = [&](const std::string& service) -> int {
            for (size_t i = 0; i < n; ++i) {
                if (!alive[i]) continue;
                for (const ProvidedService& p : candidates[i].provides) if (p.service == service) return int(i);
            }
            return -1;
        };

        // 3 + 4. Dependencies and conflicts, until nothing changes (each loss can cost others theirs)
        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t i = 0; i < n; ++i) {
                if (!alive[i]) continue;
                for (const DependencySpec& d : candidates[i].dependencies) {
                    if (d.optional) continue;
                    if (IsServiceDep(d.id)) {
                        if (provider(ServiceName(d.id)) < 0) {
                            reject(i, Rejection::Kind::MissingDependency, "needs the service '" + ServiceName(d.id) + "', which nothing provides");
                            changed = true;
                            break;
                        }
                        continue;
                    }
                    const int dep = find(d.id);
                    if (dep < 0) {
                        reject(i, Rejection::Kind::MissingDependency, "needs '" + d.id + "' " + d.range.Text() + ", which is not available");
                        changed = true;
                        break;
                    }
                    if (!d.range.Satisfies(candidates[size_t(dep)].version)) {
                        reject(i, Rejection::Kind::MissingDependency, "needs '" + d.id + "' " + d.range.Text() + ", found " + candidates[size_t(dep)].version.ToString());
                        changed = true;
                        break;
                    }
                }
            }
            for (size_t i = 0; i < n; ++i) {
                if (!alive[i]) continue;
                for (const DependencySpec& c : candidates[i].conflicts) {
                    const int other = IsServiceDep(c.id) ? provider(ServiceName(c.id)) : find(c.id);
                    if (other < 0 || size_t(other) == i || !alive[size_t(other)]) continue;
                    if (!IsServiceDep(c.id) && !c.range.Satisfies(candidates[size_t(other)].version)) continue;
                    // The project's required plugin stays ; otherwise the id that sorts last goes
                    size_t loser = i;
                    const bool iReq = options.required.count(candidates[i].id) != 0;
                    const bool oReq = options.required.count(candidates[size_t(other)].id) != 0;
                    if (iReq && !oReq) loser = size_t(other);
                    else if (!iReq && oReq) loser = i;
                    else loser = candidates[i].id > candidates[size_t(other)].id ? i : size_t(other);
                    const size_t winner = loser == i ? size_t(other) : i;
                    reject(loser, Rejection::Kind::Conflict, "conflicts with '" + candidates[winner].id + "'");
                    changed = true;
                    break;
                }
            }
        }

        // 5. Order : Kahn's algorithm, always taking the smallest id among the ones that are ready
        std::vector<std::vector<size_t>> after(n);      // after[a] : plugins that must come after a
        std::vector<int> indegree(n, 0);
        auto edge = [&](size_t before, size_t then) {
            if (before == then) return;
            if (std::find(after[before].begin(), after[before].end(), then) != after[before].end()) return;
            after[before].push_back(then);
            ++indegree[then];
        };
        for (size_t i = 0; i < n; ++i) {
            if (!alive[i]) continue;
            for (const DependencySpec& d : candidates[i].dependencies) {
                int dep = IsServiceDep(d.id) ? provider(ServiceName(d.id)) : find(d.id);
                if (dep >= 0) edge(size_t(dep), i);
            }
            for (const std::string& id : candidates[i].loadAfter) { int o = find(id); if (o >= 0) edge(size_t(o), i); }
            for (const std::string& id : candidates[i].loadBefore) { int o = find(id); if (o >= 0) edge(i, size_t(o)); }
        }
        // The engine's own modules come up before everything built on them
        for (size_t i = 0; i < n; ++i) {
            if (!alive[i] || candidates[i].moduleKind != ModuleKind::Engine) continue;
            for (size_t j = 0; j < n; ++j)
                if (alive[j] && candidates[j].moduleKind != ModuleKind::Engine) edge(i, j);
        }
        auto later = [&](size_t a, size_t b) { return candidates[a].id > candidates[b].id; };
        std::priority_queue<size_t, std::vector<size_t>, decltype(later)> ready(later);
        size_t liveCount = 0;
        for (size_t i = 0; i < n; ++i) if (alive[i]) { ++liveCount; if (indegree[i] == 0) ready.push(i); }
        while (!ready.empty()) {
            const size_t i = ready.top();
            ready.pop();
            result.order.push_back(i);
            for (size_t next : after[i]) if (--indegree[next] == 0) ready.push(next);
        }
        if (result.order.size() < liveCount) {
            // Whatever is left sits on, or behind, a cycle
            std::vector<size_t> stuck;
            for (size_t i = 0; i < n; ++i) if (alive[i] && indegree[i] > 0) stuck.push_back(i);
            // Find one cycle to print : walk predecessors among the stuck plugins
            std::string cycleText;
            {
                std::vector<int> state(n, 0);
                std::vector<size_t> path;
                std::function<bool(size_t)> dfs = [&](size_t u) -> bool {
                    state[u] = 1;
                    path.push_back(u);
                    for (size_t v : after[u]) {
                        if (std::find(stuck.begin(), stuck.end(), v) == stuck.end()) continue;
                        if (state[v] == 1) {
                            auto start = std::find(path.begin(), path.end(), v);
                            for (auto it = start; it != path.end(); ++it) cycleText += candidates[*it].id + " -> ";
                            cycleText += candidates[v].id;
                            return true;
                        }
                        if (state[v] == 0 && dfs(v)) return true;
                    }
                    path.pop_back();
                    state[u] = 2;
                    return false;
                };
                for (size_t s : stuck) if (state[s] == 0 && dfs(s)) break;
            }
            for (size_t i : stuck) reject(i, Rejection::Kind::Cycle, "in or behind a dependency cycle : " + (cycleText.empty() ? std::string("?") : cycleText));
        }

        for (size_t i : result.order)
            for (const ProvidedService& p : candidates[i].provides) result.services.emplace_back(p.service, candidates[i].id);
        return result;
    }
}
