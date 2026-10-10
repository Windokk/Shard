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

    /// The stages of a frame, always run in this order. Fixed runs 0..N times per frame (as many fixed steps as elapsed time pays for).
    enum class Phase : uint8_t {
        Input,          // read devices, turn them into input state
        PreSim,         // before simulating : load streamed worlds, apply what Input decided
        Fixed,          // fixed time step : physics, gameplay that must not depend on the frame rate
        PostPhysics,    // read physics back, once per frame
        Update,         // variable time step : scripts, animation, audio
        Late,           // after everything moved : cameras, transform sync
        Extract,        // copy what the renderer needs out of the simulation, then render
        Count
    };

    const char* PhaseName(Phase phase);

    /// What a system receives each time it runs
    struct SystemContext {
        Registry& registry;
        CommandBuffer& commands;        // this system's own : structural changes recorded here are applied at the end of the phase
        JobSystem* jobs;                // may be null (serial) ; a system can use it for its own parallel loops
        float deltaTime;                // variable step (Update...) ; the fixed step in the Fixed phase
        float fixedDeltaTime;
        int fixedStep;                  // Fixed phase : index of this step in the frame (0-based)
        int fixedStepCount;             // Fixed phase : how many steps this frame runs
        uint64_t frame;
    };

    using SystemFn = std::function<void(SystemContext&)>;

    /// @brief How a system is declared. `reads` / `writes` are the component types (or any ComponentIdOf<T> "resource" key) it
    /// touches : the scheduler runs two systems of a phase at the same time only if neither writes what the other reads or writes.
    struct SystemDesc {
        std::string name;
        Phase phase = Phase::Update;
        ComponentSet reads;
        ComponentSet writes;
        bool exclusive = false;                 // touches things the declaration cannot express : runs alone, in order
        std::vector<std::string> after;         // explicit ordering by name (systems of the same phase only)
        std::vector<std::string> before;
        SystemFn run;

        template <typename... Ts> SystemDesc& Reads()  { (reads.push_back(ComponentIdOf<Ts>()), ...); Normalize(reads); return *this; }
        template <typename... Ts> SystemDesc& Writes() { (writes.push_back(ComponentIdOf<Ts>()), ...); Normalize(writes); return *this; }
        SystemDesc& Exclusive() { exclusive = true; return *this; }
        SystemDesc& After(std::string other) { after.push_back(std::move(other)); return *this; }
        SystemDesc& Before(std::string other) { before.push_back(std::move(other)); return *this; }
    };

    /// @brief Runs the systems of every phase, in a stable order, in parallel when their data allows it.
    ///
    /// Within a phase, systems are ordered by REGISTRATION : if two of them conflict (one writes what the other reads or writes)
    /// the earlier registered runs first. Systems that don't conflict can run at the same time on the job system. The result of a
    /// phase is therefore the same whether it ran on one thread or on twenty, which is what makes replays reproducible.
    ///
    /// Structural changes made by systems go through their CommandBuffer and are applied, system by system in registration order,
    /// once the phase is over.
    class Scheduler {
    public:
        using SystemId = uint32_t;

        /// @return the id, or kInvalid if the name is already taken or there is no function
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

        /// @brief Runs one whole frame : Input, PreSim, Fixed x N, PostPhysics, Update, Late, Extract.
        void RunFrame(Registry& registry, JobSystem* jobs, const FrameTiming& timing);

        /// @brief Runs a single phase (tests, tools, or a game that drives its own loop)
        void RunPhase(Phase phase, Registry& registry, JobSystem* jobs, const FrameTiming& timing, int fixedStep = 0);

        /// The names of the enabled systems of a phase, in the order they would start
        std::vector<std::string> Order(Phase phase);

        /// False if the explicit after / before constraints form a cycle, or name a system that doesn't exist ; `error` says what.
        bool Validate(std::string* error = nullptr);

        uint64_t FrameCount() const { return m_Frame; }

    private:
        struct System {
            SystemDesc desc;
            bool enabled = true;
            uint32_t sequence = 0;                  // registration order, never reused
            CommandBuffer commands;
        };

        struct PhaseGraph {
            bool dirty = true;
            bool valid = true;
            std::string error;
            std::vector<System*> order;             // enabled systems, in start order
            std::vector<std::vector<uint32_t>> predecessors;     // indices into `order`
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

        // The context of the run in progress, read by the tasks of the TaskGraph
        Registry* m_RunRegistry = nullptr;
        JobSystem* m_RunJobs = nullptr;
        const FrameTiming* m_RunTiming = nullptr;
        int m_RunFixedStep = 0;
    };
}
