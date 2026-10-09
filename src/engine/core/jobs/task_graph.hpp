#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/jobs/job_system.hpp"

namespace Shard::Engine::Core {

    /// @brief A DAG of tasks. Build it once (Add / DependsOn), Run it as many times as you like (every frame).
    /// A task starts as soon as all its prerequisites have finished, independent tasks run in parallel on the pool.
    ///
    /// Not thread-safe: build and Run from one thread, and do not Run the same graph twice at the same time.
    class TaskGraph {
    public:
        using TaskId = uint32_t;
        static constexpr TaskId kInvalid = ~0u;

        /// @brief Adds a task. The name is for diagnostics (cycle reports, profilers).
        TaskId Add(std::string name, std::function<void()> fn);

        /// @brief `task` may only start after `prerequisite` finished. Returns false for an invalid id or a self-edge;
        /// duplicate edges are ignored.
        bool DependsOn(TaskId task, TaskId prerequisite);

        size_t Size() const { return m_Nodes.size(); }

        /// @brief True if the graph has no cycle. Otherwise fills `error` (if given) with the tasks involved.
        bool Validate(std::string* error = nullptr) const;

        /// @brief Runs every task once and returns when the last one finished. Returns false (and runs nothing) if
        /// the graph has a cycle.
        bool Run(JobSystem& js);

    private:
        struct Node {
            std::string name;
            std::function<void()> fn;
            std::vector<TaskId> successors;
            uint32_t dependencyCount = 0;
        };

        void Execute(JobSystem& js, JobGroup& group, TaskId first);

        std::vector<Node> m_Nodes;
        std::unique_ptr<std::atomic<uint32_t>[]> m_Remaining;      // per run: prerequisites still unfinished
        size_t m_RemainingSize = 0;
    };
}
