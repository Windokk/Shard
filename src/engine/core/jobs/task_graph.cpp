#include "engine/core/jobs/task_graph.hpp"

#include <algorithm>

namespace Shard::Engine::Core {

    TaskGraph::TaskId TaskGraph::Add(std::string name, std::function<void()> fn) {
        m_Nodes.push_back(Node{std::move(name), std::move(fn), {}, 0});
        return static_cast<TaskId>(m_Nodes.size() - 1);
    }

    bool TaskGraph::DependsOn(TaskId task, TaskId prerequisite) {
        if (task >= m_Nodes.size() || prerequisite >= m_Nodes.size() || task == prerequisite) return false;
        auto& succ = m_Nodes[prerequisite].successors;
        if (std::find(succ.begin(), succ.end(), task) != succ.end()) return true;   // already there
        succ.push_back(task);
        ++m_Nodes[task].dependencyCount;
        return true;
    }

    bool TaskGraph::Validate(std::string* error) const {
        // Kahn's algorithm: repeatedly remove tasks without unfinished prerequisites. Whatever is left is on, or
        // downstream of, a cycle.
        std::vector<uint32_t> deps(m_Nodes.size());
        std::vector<TaskId> ready;
        for (TaskId i = 0; i < m_Nodes.size(); ++i) {
            deps[i] = m_Nodes[i].dependencyCount;
            if (deps[i] == 0) ready.push_back(i);
        }
        size_t visited = 0;
        while (!ready.empty()) {
            const TaskId id = ready.back();
            ready.pop_back();
            ++visited;
            for (TaskId s : m_Nodes[id].successors)
                if (--deps[s] == 0) ready.push_back(s);
        }
        if (visited == m_Nodes.size()) return true;
        if (error) {
            *error = "TaskGraph has a dependency cycle involving:";
            for (TaskId i = 0; i < m_Nodes.size(); ++i)
                if (deps[i] != 0) *error += " '" + m_Nodes[i].name + "'";
        }
        return false;
    }

    bool TaskGraph::Run(JobSystem& js) {
        if (m_Nodes.empty()) return true;
        if (!Validate()) return false;

        if (m_RemainingSize != m_Nodes.size()) {
            m_Remaining = std::make_unique<std::atomic<uint32_t>[]>(m_Nodes.size());
            m_RemainingSize = m_Nodes.size();
        }
        for (TaskId i = 0; i < m_Nodes.size(); ++i)
            m_Remaining[i].store(m_Nodes[i].dependencyCount, std::memory_order_relaxed);

        JobGroup group;
        for (TaskId i = 0; i < m_Nodes.size(); ++i)
            if (m_Nodes[i].dependencyCount == 0)
                js.Submit(group, [this, &js, &group, i] { Execute(js, group, i); });
        js.Wait(group);
        return true;
    }

    void TaskGraph::Execute(JobSystem& js, JobGroup& group, TaskId id) {
        // Runs `id`, then the successors it unblocked. All but one of them become new jobs (so other threads can take
        // them); the last one is run by this same job in the loop: no queue round-trip, and the data the finished task
        // just wrote is still hot in this core's cache.
        std::vector<TaskId> ready;
        for (;;) {
            Node& node = m_Nodes[id];
            if (node.fn) node.fn();

            ready.clear();
            for (TaskId s : node.successors) {
                // acq_rel: the release publishes our writes to whoever runs the successor, the acquire (on the thread
                // that brings the counter to 0) makes every prerequisite's writes visible to it.
                if (m_Remaining[s].fetch_sub(1, std::memory_order_acq_rel) == 1) ready.push_back(s);
            }
            if (ready.empty()) return;

            for (size_t i = 1; i < ready.size(); ++i) {
                const TaskId next = ready[i];
                js.Submit(group, [this, &js, &group, next] { Execute(js, group, next); });
            }
            id = ready[0];
        }
    }
}
