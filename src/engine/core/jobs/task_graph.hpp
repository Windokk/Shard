#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/jobs/job_system.hpp"

namespace Shard::Engine::Core {

    class TaskGraph {
    public:
        using TaskId = uint32_t;
        static constexpr TaskId kInvalid = ~0u;

        TaskId Add(std::string name, std::function<void()> fn);

        bool DependsOn(TaskId task, TaskId prerequisite);

        size_t Size() const { return m_Nodes.size(); }

        bool Validate(std::string* error = nullptr) const;

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
        std::unique_ptr<std::atomic<uint32_t>[]> m_Remaining;
        size_t m_RemainingSize = 0;
    };
}
