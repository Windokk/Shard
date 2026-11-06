#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/ecs/command_buffer.hpp"
#include "engine/core/ecs/component.hpp"
#include "engine/core/ecs/registry.hpp"
#include "engine/core/jobs/job_system.hpp"
#include "engine/core/jobs/task_graph.hpp"

namespace Shard::Engine::Core::Ecs {

    // Stages always run in this order. Fixed runs 0..N times per frame.
    enum class Phase : uint8_t {
        Input,
        PreSim,
        Fixed,
        PostPhysics,
        Update,
        Late,
        Extract,
        Count
    };

    const char* PhaseName(Phase phase);

    struct SystemContext {
        Registry& registry;
        CommandBuffer& commands;
        JobSystem* jobs;
        float deltaTime;
        float fixedDeltaTime;
        int fixedStep;
        int fixedStepCount;
        uint64_t frame;
    };

    using SystemFn = std::function<void(SystemContext&)>;

    struct SystemDesc {
        std::string name;
        Phase phase = Phase::Update;
        ComponentSet reads;
        ComponentSet writes;
        bool exclusive = false;
        bool mainThread = false;
        std::vector<std::string> after;
        std::vector<std::string> before;
        SystemFn run;

        template <typename... Ts> SystemDesc& Reads()  { (reads.push_back(ComponentIdOf<Ts>()), ...); Normalize(reads); return *this; }
        template <typename... Ts> SystemDesc& Writes() { (writes.push_back(ComponentIdOf<Ts>()), ...); Normalize(writes); return *this; }
        SystemDesc& Exclusive() { exclusive = true; return *this; }
        SystemDesc& MainThread() { mainThread = true; return *this; }
        SystemDesc& After(std::string other) { after.push_back(std::move(other)); return *this; }
        SystemDesc& Before(std::string other) { before.push_back(std::move(other)); return *this; }
    };

    class Scheduler {
    public:
        using SystemId = uint32_t;

        SystemId Add(SystemDesc desc);
        static constexpr SystemId kInvalid = ~0u;

        bool Remove(const std::string& name);
        bool SetEnabled(const std::string& name, bool enabled);
        bool Has(const std::string& name) const;

        struct FrameTiming {
            float deltaTime = 0.0f;
            float fixedDeltaTime = 1.0f / 60.0f;
            int fixedSteps = 0;
        };

        void RunFrame(Registry& registry, JobSystem* jobs, const FrameTiming& timing);

        void RunPhase(Phase phase, Registry& registry, JobSystem* jobs, const FrameTiming& timing, int fixedStep = 0);

        std::vector<std::string> Order(Phase phase);

        bool Validate(std::string* error = nullptr);

        uint64_t FrameCount() const { return m_Frame; }

        using PhaseEndFn = std::function<void(Phase)>;
        void SetPhaseEndHook(PhaseEndFn hook) { m_PhaseEnd = std::move(hook); }

        struct SystemStats {
            std::string name;
            Phase phase;
            bool enabled;
            double lastMs;
            double averageMs;
        };
        std::vector<SystemStats> Stats();
        std::string Describe();

    private:
        struct System {
            SystemDesc desc;
            bool enabled = true;
            uint32_t sequence = 0;
            CommandBuffer commands;
            double lastMs = 0.0;
            double averageMs = 0.0;
        };

        struct PhaseGraph {
            bool dirty = true;
            bool valid = true;
            std::string error;
            std::vector<System*> order;
            std::vector<std::vector<uint32_t>> predecessors;
            TaskGraph tasks;
            std::vector<TaskGraph::TaskId> taskIds;
            bool tasksBuilt = false;
        };

        PhaseGraph& Compile(Phase phase);
        void Execute(Phase phase, Registry& registry, JobSystem* jobs, const FrameTiming& timing, int fixedStep);
        System* Find(const std::string& name);
        const System* Find(const std::string& name) const;
        void MarkDirty(Phase phase) { m_Graphs[static_cast<size_t>(phase)].dirty = true; }

        std::vector<std::unique_ptr<System>> m_Systems;
        PhaseGraph m_Graphs[static_cast<size_t>(Phase::Count)];
        uint32_t m_NextSequence = 0;
        uint64_t m_Frame = 0;
        PhaseEndFn m_PhaseEnd;

        Registry* m_RunRegistry = nullptr;
        JobSystem* m_RunJobs = nullptr;
        const FrameTiming* m_RunTiming = nullptr;
        int m_RunFixedStep = 0;
    };
}
