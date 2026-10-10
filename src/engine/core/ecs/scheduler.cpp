#include "engine/core/ecs/scheduler.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace Shard::Engine::Core::Ecs {

    const char* PhaseName(Phase phase) {
        switch (phase) {
            case Phase::Input:       return "Input";
            case Phase::PreSim:      return "PreSim";
            case Phase::Fixed:       return "Fixed";
            case Phase::PostPhysics: return "PostPhysics";
            case Phase::Update:      return "Update";
            case Phase::Late:        return "Late";
            case Phase::Extract:     return "Extract";
            default:                 return "?";
        }
    }

    namespace {
        /// Two systems can't overlap if one writes something the other reads or writes (or if one declared itself exclusive)
        bool Conflicts(const SystemDesc& a, const SystemDesc& b) {
            if (a.exclusive || b.exclusive) return true;
            return Intersects(a.writes, b.writes) || Intersects(a.writes, b.reads) || Intersects(a.reads, b.writes);
        }

        void AddUnique(std::vector<uint32_t>& list, uint32_t value) {
            if (std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
        }
    }

    Scheduler::System* Scheduler::Find(const std::string& name) {
        for (auto& system : m_Systems) if (system->desc.name == name) return system.get();
        return nullptr;
    }

    const Scheduler::System* Scheduler::Find(const std::string& name) const {
        for (const auto& system : m_Systems) if (system->desc.name == name) return system.get();
        return nullptr;
    }

    bool Scheduler::Has(const std::string& name) const { return Find(name) != nullptr; }

    Scheduler::SystemId Scheduler::Add(SystemDesc desc) {
        if (!desc.run || desc.phase >= Phase::Count || Find(desc.name)) return kInvalid;
        Normalize(desc.reads);
        Normalize(desc.writes);
        auto system = std::make_unique<System>();
        system->sequence = m_NextSequence++;
        MarkDirty(desc.phase);
        system->desc = std::move(desc);
        m_Systems.push_back(std::move(system));
        return m_Systems.back()->sequence;
    }

    bool Scheduler::Remove(const std::string& name) {
        for (auto it = m_Systems.begin(); it != m_Systems.end(); ++it) {
            if ((*it)->desc.name != name) continue;
            MarkDirty((*it)->desc.phase);
            m_Systems.erase(it);
            return true;
        }
        return false;
    }

    bool Scheduler::SetEnabled(const std::string& name, bool enabled) {
        System* system = Find(name);
        if (!system) return false;
        if (system->enabled != enabled) {
            system->enabled = enabled;
            MarkDirty(system->desc.phase);
        }
        return true;
    }

    Scheduler::PhaseGraph& Scheduler::Compile(Phase phase) {
        PhaseGraph& graph = m_Graphs[static_cast<size_t>(phase)];
        if (!graph.dirty) return graph;

        graph = PhaseGraph{};
        graph.dirty = false;

        // The enabled systems of the phase, by registration order
        std::vector<System*> systems;
        for (auto& system : m_Systems)
            if (system->enabled && system->desc.phase == phase) systems.push_back(system.get());
        std::sort(systems.begin(), systems.end(), [](const System* a, const System* b) { return a->sequence < b->sequence; });

        const uint32_t n = static_cast<uint32_t>(systems.size());
        std::unordered_map<std::string, uint32_t> byName;
        for (uint32_t i = 0; i < n; ++i) byName[systems[i]->desc.name] = i;

        // Data conflicts : the earlier registered goes first
        std::vector<std::vector<uint32_t>> dataPreds(n);
        for (uint32_t j = 0; j < n; ++j)
            for (uint32_t i = 0; i < j; ++i)
                if (Conflicts(systems[i]->desc, systems[j]->desc)) dataPreds[j].push_back(i);

        // Explicit constraints
        std::vector<std::vector<uint32_t>> preds = dataPreds;
        for (uint32_t j = 0; j < n; ++j) {
            for (const std::string& other : systems[j]->desc.after) {
                auto it = byName.find(other);
                if (it == byName.end()) { graph.error += "'" + systems[j]->desc.name + "' runs after unknown system '" + other + "' (in phase " + PhaseName(phase) + "). "; continue; }
                if (it->second != j) AddUnique(preds[j], it->second);
            }
            for (const std::string& other : systems[j]->desc.before) {
                auto it = byName.find(other);
                if (it == byName.end()) { graph.error += "'" + systems[j]->desc.name + "' runs before unknown system '" + other + "' (in phase " + PhaseName(phase) + "). "; continue; }
                if (it->second != j) AddUnique(preds[it->second], j);
            }
        }

        // Kahn's algorithm, always taking the READY system with the lowest registration number : the order is stable
        auto topological = [n](const std::vector<std::vector<uint32_t>>& p, std::vector<uint32_t>& out) {
            std::vector<uint32_t> remaining(n, 0);
            std::vector<std::vector<uint32_t>> successors(n);
            for (uint32_t j = 0; j < n; ++j) { remaining[j] = static_cast<uint32_t>(p[j].size()); for (uint32_t i : p[j]) successors[i].push_back(j); }
            std::vector<bool> done(n, false);
            for (uint32_t step = 0; step < n; ++step) {
                uint32_t pick = n;
                for (uint32_t j = 0; j < n; ++j) if (!done[j] && remaining[j] == 0) { pick = j; break; }
                if (pick == n) return false;                                    // nothing ready but systems left : a cycle
                done[pick] = true;
                out.push_back(pick);
                for (uint32_t s : successors[pick]) --remaining[s];
            }
            return true;
        };

        std::vector<uint32_t> order;
        if (!topological(preds, order)) {
            graph.valid = false;
            graph.error += std::string("The after / before constraints of phase ") + PhaseName(phase) + " form a cycle ; they are ignored. ";
            preds = dataPreds;
            order.clear();
            topological(preds, order);                                          // data edges only point backwards : always succeeds
        }

        // Re-index everything by position in the final order
        std::vector<uint32_t> position(n);
        for (uint32_t k = 0; k < n; ++k) position[order[k]] = k;
        graph.order.reserve(n);
        graph.predecessors.resize(n);
        for (uint32_t k = 0; k < n; ++k) {
            graph.order.push_back(systems[order[k]]);
            for (uint32_t p : preds[order[k]]) graph.predecessors[k].push_back(position[p]);
        }
        return graph;
    }

    std::vector<std::string> Scheduler::Order(Phase phase) {
        std::vector<std::string> names;
        for (System* system : Compile(phase).order) names.push_back(system->desc.name);
        return names;
    }

    bool Scheduler::Validate(std::string* error) {
        bool ok = true;
        std::string all;
        for (size_t p = 0; p < static_cast<size_t>(Phase::Count); ++p) {
            PhaseGraph& graph = Compile(static_cast<Phase>(p));
            if (!graph.error.empty()) { ok = false; all += graph.error; }
        }
        if (error) *error = all;
        return ok;
    }

    void Scheduler::Execute(Phase phase, Registry& registry, JobSystem* jobs, const FrameTiming& timing, int fixedStep) {
        PhaseGraph& graph = Compile(phase);
        if (graph.order.empty()) { registry.FlushDeferred(); return; }

        m_RunRegistry = &registry;
        m_RunJobs = jobs;
        m_RunTiming = &timing;
        m_RunFixedStep = fixedStep;

        auto runSystem = [this, phase](System* system) {
            const bool fixed = phase == Phase::Fixed;
            SystemContext context{*m_RunRegistry, system->commands, m_RunJobs,
                                  fixed ? m_RunTiming->fixedDeltaTime : m_RunTiming->deltaTime, m_RunTiming->fixedDeltaTime,
                                  m_RunFixedStep, m_RunTiming->fixedSteps, m_Frame};
            system->commands.Clear();
            const auto start = std::chrono::steady_clock::now();
            system->desc.run(context);
            system->lastMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            system->averageMs = system->averageMs == 0.0 ? system->lastMs : system->averageMs * 0.95 + system->lastMs * 0.05;
        };

        const bool parallel = jobs && jobs->ThreadCount() > 1 && graph.order.size() > 1;
        if (!parallel) {
            for (System* system : graph.order) runSystem(system);
        } else {
            if (!graph.tasksBuilt) {
                graph.tasks = TaskGraph{};
                graph.taskIds.clear();
                for (System* system : graph.order)
                    graph.taskIds.push_back(graph.tasks.Add(system->desc.name, [runSystem, system] { runSystem(system); }));
                for (size_t k = 0; k < graph.order.size(); ++k)
                    for (uint32_t p : graph.predecessors[k]) graph.tasks.DependsOn(graph.taskIds[k], graph.taskIds[p]);
                graph.tasksBuilt = true;
            }
            graph.tasks.Run(*jobs);
        }

        // Apply the recorded structural changes, system by system in registration order : same result serial or parallel
        std::vector<System*> byRegistration = graph.order;
        std::sort(byRegistration.begin(), byRegistration.end(), [](const System* a, const System* b) { return a->sequence < b->sequence; });
        for (System* system : byRegistration)
            if (!system->commands.Empty()) registry.Playback(system->commands);

        // Entities whose destruction was asked while something was iterating
        registry.FlushDeferred();
    }

    std::vector<Scheduler::SystemStats> Scheduler::Stats() {
        std::vector<SystemStats> out;
        for (size_t p = 0; p < static_cast<size_t>(Phase::Count); ++p) {
            const Phase phase = static_cast<Phase>(p);
            for (System* system : Compile(phase).order)
                out.push_back(SystemStats{system->desc.name, phase, true, system->lastMs, system->averageMs});
            for (auto& system : m_Systems)                                  // the disabled ones, after
                if (!system->enabled && system->desc.phase == phase)
                    out.push_back(SystemStats{system->desc.name, phase, false, system->lastMs, system->averageMs});
        }
        return out;
    }

    std::string Scheduler::Describe() {
        std::ostringstream out;
        Phase current = Phase::Count;
        for (const SystemStats& s : Stats()) {
            if (s.phase != current) { current = s.phase; out << PhaseName(current) << "\n"; }
            out << "  " << std::left << std::setw(28) << s.name << (s.enabled ? "" : "(off) ") << std::fixed << std::setprecision(3)
                << s.lastMs << " ms  (avg " << s.averageMs << ")\n";
        }
        return out.str();
    }

    void Scheduler::RunPhase(Phase phase, Registry& registry, JobSystem* jobs, const FrameTiming& timing, int fixedStep) {
        Execute(phase, registry, jobs, timing, fixedStep);
    }

    void Scheduler::RunFrame(Registry& registry, JobSystem* jobs, const FrameTiming& timing) {
        Execute(Phase::Input, registry, jobs, timing, 0);
        Execute(Phase::PreSim, registry, jobs, timing, 0);
        for (int step = 0; step < timing.fixedSteps; ++step)
            Execute(Phase::Fixed, registry, jobs, timing, step);
        Execute(Phase::PostPhysics, registry, jobs, timing, 0);
        Execute(Phase::Update, registry, jobs, timing, 0);
        Execute(Phase::Late, registry, jobs, timing, 0);
        Execute(Phase::Extract, registry, jobs, timing, 0);
        ++m_Frame;
    }
}
