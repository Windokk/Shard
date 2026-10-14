#include "render_graph.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <queue>
#include <set>
#include <sstream>

namespace Shard::Engine::Rendering {

    const char* ResourceUsageName(ResourceUsage usage)
    {
        switch (usage)
        {
            case ResourceUsage::ColorTarget:    return "ColorTarget";
            case ResourceUsage::DepthTarget:    return "DepthTarget";
            case ResourceUsage::ShaderRead:     return "ShaderRead";
            case ResourceUsage::StorageRead:    return "StorageRead";
            case ResourceUsage::StorageWrite:   return "StorageWrite";
            case ResourceUsage::TransferSrc:    return "TransferSrc";
            case ResourceUsage::TransferDst:    return "TransferDst";
            case ResourceUsage::IndirectArgs:   return "IndirectArgs";
            case ResourceUsage::VertexInput:    return "VertexInput";
            case ResourceUsage::Present:        return "Present";
            default:                            return "None";
        }
    }

    bool IsWriteUsage(ResourceUsage usage)
    {
        switch (usage)
        {
            case ResourceUsage::ColorTarget:
            case ResourceUsage::DepthTarget:
            case ResourceUsage::StorageWrite:
            case ResourceUsage::TransferDst:
                return true;
            default:
                return false;
        }
    }

    MemoryBarrierBit BarrierBitsFor(ResourceUsage from, ResourceUsage to)
    {
        using B = MemoryBarrierBit;

        switch (from)
        {
            case ResourceUsage::ColorTarget:
            case ResourceUsage::DepthTarget:
                // Attachment writes are ordered for the next draws by the API itself, but not for a texture sampled through a
                // handle the driver cannot see (bindless), which is how the passes read each other's targets
                switch (to)
                {
                    case ResourceUsage::ShaderRead:     return B::TextureFetch;
                    case ResourceUsage::StorageRead:
                    case ResourceUsage::StorageWrite:   return B::ImageAccess | B::TextureFetch;
                    case ResourceUsage::TransferSrc:    return B::TextureUpdate;
                    default:                            return B::None;
                }

            case ResourceUsage::StorageWrite:
            case ResourceUsage::TransferDst:
                switch (to)
                {
                    case ResourceUsage::ShaderRead:     return B::TextureFetch | B::ShaderStorage;
                    case ResourceUsage::StorageRead:
                    case ResourceUsage::StorageWrite:   return B::ShaderStorage | B::ImageAccess;
                    case ResourceUsage::IndirectArgs:   return B::Command | B::ShaderStorage;
                    case ResourceUsage::VertexInput:    return B::VertexAttribArray | B::ShaderStorage;
                    case ResourceUsage::TransferSrc:    return B::BufferUpdate | B::TextureUpdate;
                    case ResourceUsage::ColorTarget:
                    case ResourceUsage::DepthTarget:    return B::TextureFetch;
                    default:                            return B::None;
                }

            default:
                return B::None;
        }
    }

    uint64_t GraphResourceDesc::SizeBytes() const
    {
        if (isBuffer)
            return bufferBytes;

        uint64_t bytes = static_cast<uint64_t>(width) * height * std::max<uint32_t>(layers, 1) * std::max<uint32_t>(samples, 1) * bytesPerTexel;
        if (mipmapped)
            bytes += bytes / 3;     // the chain of mip levels is a third as big again
        return bytes;
    }

    const CompiledPass* CompiledGraph::Find(const std::string& name) const
    {
        for (const CompiledPass& pass : passes)
        {
            if (pass.name == name)
                return &pass;
        }
        return nullptr;
    }

    int CompiledGraph::CulledCount() const
    {
        int count = 0;
        for (const CompiledPass& pass : passes)
        {
            if (pass.culled)
                count++;
        }
        return count;
    }

    void RenderGraph::Clear()
    {
        m_Passes.clear();
        m_PassIndex.clear();
        m_Resources.clear();
    }

    void RenderGraph::DeclareResource(const GraphResourceDesc& desc)
    {
        m_Resources[desc.name] = desc;
    }

    const GraphResourceDesc* RenderGraph::FindResource(const std::string& name) const
    {
        auto it = m_Resources.find(name);
        return it == m_Resources.end() ? nullptr : &it->second;
    }

    void RenderGraph::AddPass(const GraphPassDesc& pass)
    {
        auto it = m_PassIndex.find(pass.name);
        if (it != m_PassIndex.end())
        {
            m_Passes[it->second] = pass;
            return;
        }

        m_PassIndex[pass.name] = m_Passes.size();
        m_Passes.push_back(pass);
    }

    void RenderGraph::RemovePass(const std::string& name)
    {
        auto it = m_PassIndex.find(name);
        if (it == m_PassIndex.end())
            return;

        m_Passes.erase(m_Passes.begin() + it->second);

        m_PassIndex.clear();
        for (size_t i = 0; i < m_Passes.size(); i++)
            m_PassIndex[m_Passes[i].name] = i;
    }

    bool RenderGraph::HasPass(const std::string& name) const
    {
        return m_PassIndex.find(name) != m_PassIndex.end();
    }

    CompiledGraph RenderGraph::Compile() const
    {
        CompiledGraph out;

        const int count = static_cast<int>(m_Passes.size());
        out.passes.resize(count);

        std::vector<bool> live(count, false);

        for (int i = 0; i < count; i++)
        {
            out.passes[i].name = m_Passes[i].name;
            out.passes[i].declarationIndex = i;
            out.passes[i].disabled = !m_Passes[i].enabled;
            live[i] = m_Passes[i].enabled;
        }

        // ---- Edges : producers of a resource one after the other, readers after the last producer, then the explicit ones ----

        std::vector<std::set<int>> predecessors(count);
        std::vector<std::set<int>> successors(count);

        auto addEdge = [&](int from, int to)
        {
            if (from == to || !live[from] || !live[to])
                return;
            successors[from].insert(to);
            predecessors[to].insert(from);
        };

        struct Users
        {
            std::vector<int> writers;
            std::vector<int> readers;
        };
        std::unordered_map<std::string, Users> usersOf;
        std::vector<std::string> resourceNames;     // in first use order, so that the output does not depend on a hash

        for (int i = 0; i < count; i++)
        {
            if (!live[i])
                continue;

            // What this pass writes, so that a resource it reads and writes counts as a write only
            std::set<std::string> writtenHere;
            for (const PassAccess& access : m_Passes[i].accesses)
            {
                if (IsWriteUsage(access.usage))
                    writtenHere.insert(access.resource);
            }

            std::set<std::string> seen;
            for (const PassAccess& access : m_Passes[i].accesses)
            {
                if (access.resource.empty() || !seen.insert(access.resource).second)
                    continue;

                auto [it, inserted] = usersOf.try_emplace(access.resource);
                if (inserted)
                    resourceNames.push_back(access.resource);

                if (writtenHere.count(access.resource))
                    it->second.writers.push_back(i);
                else
                    it->second.readers.push_back(i);
            }
        }

        // The explicit order first : it decides the order of the producers of a resource when they were not declared in it (a pass that was
        // registered later but has to run first)
        std::vector<std::vector<int>> explicitSuccessors(count);
        for (int i = 0; i < count; i++)
        {
            for (const std::string& before : m_Passes[i].after)
            {
                auto it = m_PassIndex.find(before);
                if (it != m_PassIndex.end() && live[it->second] && live[i])
                    explicitSuccessors[it->second].push_back(i);
            }
        }

        std::vector<int8_t> reachMemo(static_cast<size_t>(count) * count, -1);
        std::function<bool(int, int)> reaches = [&](int from, int to) -> bool
        {
            int8_t& memo = reachMemo[static_cast<size_t>(from) * count + to];
            if (memo >= 0)
                return memo == 1;

            memo = 0;       // a cycle would not come back here
            for (int next : explicitSuccessors[from])
            {
                if (next == to || reaches(next, to))
                {
                    memo = 1;
                    break;
                }
            }
            return memo == 1;
        };

        for (const std::string& name : resourceNames)
        {
            const Users& users = usersOf[name];

            // Declaration order, except where an explicit edge says a producer comes before another
            std::vector<int> producers;
            std::vector<int> remaining = users.writers;
            while (!remaining.empty())
            {
                size_t pick = 0;
                for (size_t k = 0; k < remaining.size(); k++)
                {
                    bool blocked = false;
                    for (int other : remaining)
                    {
                        if (other != remaining[k] && reaches(other, remaining[k]))
                        {
                            blocked = true;
                            break;
                        }
                    }
                    if (!blocked)
                    {
                        pick = k;
                        break;
                    }
                }
                producers.push_back(remaining[pick]);
                remaining.erase(remaining.begin() + pick);
            }

            for (size_t k = 1; k < producers.size(); k++)
                addEdge(producers[k - 1], producers[k]);

            if (!producers.empty())
            {
                for (int reader : users.readers)
                {
                    // A reader that names the passes it comes after reads what they wrote, which is not the last write of the frame when
                    // the resource is written again later (the ping-pong targets of a jump flood read the pass before them, and are
                    // written again by the passes after). The producers that come after the ones it names have to wait for it.
                    int namedProducer = -1;
                    for (size_t k = 0; k < producers.size(); k++)
                    {
                        if (std::find(m_Passes[reader].after.begin(), m_Passes[reader].after.end(), m_Passes[producers[k]].name) != m_Passes[reader].after.end())
                            namedProducer = static_cast<int>(k);
                    }

                    if (namedProducer < 0)
                    {
                        addEdge(producers.back(), reader);
                        continue;
                    }

                    for (size_t k = static_cast<size_t>(namedProducer) + 1; k < producers.size(); k++)
                    {
                        if (!reaches(producers[k], reader))
                            addEdge(reader, producers[k]);
                    }
                }
            }
        }

        for (int i = 0; i < count; i++)
        {
            for (const std::string& before : m_Passes[i].after)
            {
                auto it = m_PassIndex.find(before);
                if (it != m_PassIndex.end())
                    addEdge(static_cast<int>(it->second), i);
            }
        }

        // ---- Culling : whatever no root needs is skipped ----

        std::vector<bool> needed(count, false);
        std::vector<int> stack;

        for (int i = 0; i < count; i++)
        {
            if (!live[i])
                continue;

            bool root = m_Passes[i].sideEffects || m_Passes[i].accesses.empty();

            for (const PassAccess& access : m_Passes[i].accesses)
            {
                if (IsWriteUsage(access.usage) || access.usage == ResourceUsage::Present)
                {
                    const GraphResourceDesc* desc = FindResource(access.resource);
                    if (desc && desc->imported)
                        root = true;
                }
                if (access.usage == ResourceUsage::Present)
                    root = true;
            }

            if (root)
            {
                needed[i] = true;
                stack.push_back(i);
            }
        }

        while (!stack.empty())
        {
            const int current = stack.back();
            stack.pop_back();

            for (int predecessor : predecessors[current])
            {
                if (!needed[predecessor])
                {
                    needed[predecessor] = true;
                    stack.push_back(predecessor);
                }
            }
        }

        for (int i = 0; i < count; i++)
        {
            if (live[i] && !needed[i])
                out.passes[i].culled = true;
        }

        // ---- Order : Kahn, the pass declared first goes first among the ones that are ready ----

        std::vector<int> inDegree(count, 0);
        std::priority_queue<int, std::vector<int>, std::greater<int>> ready;
        int runnable = 0;

        for (int i = 0; i < count; i++)
        {
            if (!live[i] || out.passes[i].culled)
                continue;

            runnable++;
            for (int predecessor : predecessors[i])
            {
                if (live[predecessor] && !out.passes[predecessor].culled)
                    inDegree[i]++;
            }

            if (inDegree[i] == 0)
                ready.push(i);
        }

        while (!ready.empty())
        {
            const int current = ready.top();
            ready.pop();
            out.order.push_back(current);

            for (int next : successors[current])
            {
                if (out.passes[next].culled)
                    continue;

                if (--inDegree[next] == 0)
                    ready.push(next);
            }
        }

        if (static_cast<int>(out.order.size()) != runnable)
        {
            std::ostringstream error;
            error << "Render graph cycle between :";
            for (int i = 0; i < count; i++)
            {
                if (live[i] && !out.passes[i].culled && inDegree[i] > 0)
                    error << " " << m_Passes[i].name;
            }
            out.error = error.str();
            out.order.clear();
            return out;
        }

        // ---- Barriers : a change of use between two passes ----

        struct State
        {
            ResourceUsage usage = ResourceUsage::None;
        };
        std::unordered_map<std::string, State> states;

        std::unordered_map<std::string, ResourceLifetime> lifetimes;
        std::vector<std::string> lifetimeOrder;

        for (size_t position = 0; position < out.order.size(); position++)
        {
            const int passIndex = out.order[position];
            const GraphPassDesc& desc = m_Passes[passIndex];
            CompiledPass& compiled = out.passes[passIndex];

            std::set<std::pair<std::string, ResourceUsage>> emitted;

            for (const PassAccess& access : desc.accesses)
            {
                if (access.resource.empty())
                    continue;

                State& state = states[access.resource];

                // Attachment writes after attachment writes are ordered by the API ; a storage write needs its barrier even after itself
                const bool sameUse = state.usage == access.usage && access.usage != ResourceUsage::StorageWrite;

                if (state.usage != ResourceUsage::None && !sameUse && emitted.insert({access.resource, access.usage}).second)
                {
                    GraphBarrier barrier;
                    barrier.resource = access.resource;
                    barrier.from = state.usage;
                    barrier.to = access.usage;
                    compiled.barriersBefore.push_back(barrier);
                    compiled.memoryBarrierBefore = compiled.memoryBarrierBefore | BarrierBitsFor(barrier.from, barrier.to);
                }

                state.usage = access.usage;

                auto [it, inserted] = lifetimes.try_emplace(access.resource);
                ResourceLifetime& lifetime = it->second;
                if (inserted)
                {
                    lifetime.name = access.resource;
                    lifetime.firstPass = static_cast<int>(position);
                    lifetimeOrder.push_back(access.resource);
                }
                lifetime.lastPass = static_cast<int>(position);
            }
        }

        // ---- Lifetimes and aliasing : transient resources that are never live together share memory ----

        for (const std::string& name : lifetimeOrder)
        {
            ResourceLifetime lifetime = lifetimes[name];
            if (const GraphResourceDesc* desc = FindResource(name))
            {
                lifetime.imported = desc->imported;
                lifetime.sizeBytes = desc->SizeBytes();
            }
            out.resources.push_back(lifetime);
        }

        std::vector<int> byStart;
        for (size_t i = 0; i < out.resources.size(); i++)
        {
            if (!out.resources[i].imported && out.resources[i].sizeBytes > 0)
                byStart.push_back(static_cast<int>(i));
        }
        std::stable_sort(byStart.begin(), byStart.end(), [&](int a, int b) { return out.resources[a].firstPass < out.resources[b].firstPass; });

        std::vector<int> slotFreeAfter;     // last position of the resource a slot holds

        for (int index : byStart)
        {
            ResourceLifetime& lifetime = out.resources[index];
            out.transientBytesWithoutAliasing += lifetime.sizeBytes;

            int best = -1;
            uint64_t bestGrowth = 0;

            for (size_t slot = 0; slot < out.aliasSlots.size(); slot++)
            {
                if (slotFreeAfter[slot] >= lifetime.firstPass)
                    continue;       // still in use when this resource starts

                const uint64_t growth = lifetime.sizeBytes > out.aliasSlots[slot].sizeBytes ? lifetime.sizeBytes - out.aliasSlots[slot].sizeBytes : 0;
                if (best < 0 || growth < bestGrowth || (growth == bestGrowth && out.aliasSlots[slot].sizeBytes < out.aliasSlots[best].sizeBytes))
                {
                    best = static_cast<int>(slot);
                    bestGrowth = growth;
                }
            }

            if (best < 0)
            {
                best = static_cast<int>(out.aliasSlots.size());
                out.aliasSlots.emplace_back();
                slotFreeAfter.push_back(-1);
            }

            AliasSlot& slot = out.aliasSlots[best];
            slot.sizeBytes = std::max(slot.sizeBytes, lifetime.sizeBytes);
            slot.resources.push_back(lifetime.name);
            slotFreeAfter[best] = lifetime.lastPass;
            lifetime.aliasSlot = best;
        }

        for (const AliasSlot& slot : out.aliasSlots)
            out.transientBytesWithAliasing += slot.sizeBytes;

        out.valid = true;
        return out;
    }

    std::string RenderGraph::ToText(const CompiledGraph& graph, const RenderGraph& source)
    {
        std::ostringstream out;

        if (!graph.valid)
        {
            out << "Render graph is not valid : " << graph.error << "\n";
            return out.str();
        }

        out << "Render graph : " << graph.order.size() << " passes run, " << graph.CulledCount() << " culled\n";

        int position = 0;
        for (int index : graph.order)
        {
            const CompiledPass& pass = graph.passes[index];
            const GraphPassDesc& desc = source.GetPasses()[index];

            out << "  " << position++ << ". " << pass.name << "\n";

            for (const PassAccess& access : desc.accesses)
                out << "       " << (IsWriteUsage(access.usage) ? "writes " : "reads  ") << access.resource << " (" << ResourceUsageName(access.usage) << ")\n";

            for (const GraphBarrier& barrier : pass.barriersBefore)
                out << "       barrier " << barrier.resource << " : " << ResourceUsageName(barrier.from) << " -> " << ResourceUsageName(barrier.to) << "\n";
        }

        for (const CompiledPass& pass : graph.passes)
        {
            if (pass.culled)
                out << "  culled : " << pass.name << "\n";
            else if (pass.disabled)
                out << "  disabled : " << pass.name << "\n";
        }

        if (!graph.aliasSlots.empty())
        {
            out << "  transient memory : " << graph.transientBytesWithoutAliasing / 1024 << " KiB without aliasing, "
                << graph.transientBytesWithAliasing / 1024 << " KiB with (" << graph.aliasSlots.size() << " slots)\n";

            for (size_t slot = 0; slot < graph.aliasSlots.size(); slot++)
            {
                out << "    slot " << slot << " (" << graph.aliasSlots[slot].sizeBytes / 1024 << " KiB) :";
                for (const std::string& resource : graph.aliasSlots[slot].resources)
                    out << " " << resource;
                out << "\n";
            }
        }

        return out.str();
    }

    namespace {
        std::string DotId(const std::string& prefix, const std::string& name)
        {
            std::string id = prefix;
            for (char c : name)
                id += (std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
            return id;
        }
    }

    std::string RenderGraph::ToDot(const CompiledGraph& graph, const RenderGraph& source)
    {
        std::ostringstream out;
        out << "digraph RenderGraph {\n  rankdir=LR;\n  node [fontname=\"Helvetica\"];\n";

        std::set<std::string> resourcesShown;

        for (size_t i = 0; i < graph.passes.size(); i++)
        {
            const CompiledPass& pass = graph.passes[i];
            const GraphPassDesc& desc = source.GetPasses()[i];

            const char* style = pass.disabled ? "style=dotted,color=gray" : pass.culled ? "style=dashed,color=gray,fontcolor=gray" : "style=filled,fillcolor=\"#cfe2ff\"";
            out << "  " << DotId("p_", pass.name) << " [shape=box," << style << ",label=\"" << pass.name << "\"];\n";

            for (const PassAccess& access : desc.accesses)
            {
                resourcesShown.insert(access.resource);

                if (IsWriteUsage(access.usage))
                    out << "  " << DotId("p_", pass.name) << " -> " << DotId("r_", access.resource) << " [label=\"" << ResourceUsageName(access.usage) << "\"];\n";
                else
                    out << "  " << DotId("r_", access.resource) << " -> " << DotId("p_", pass.name) << " [label=\"" << ResourceUsageName(access.usage) << "\"];\n";
            }

            for (const std::string& before : desc.after)
                out << "  " << DotId("p_", before) << " -> " << DotId("p_", pass.name) << " [style=dashed,color=gray,arrowhead=open];\n";
        }

        for (const std::string& name : resourcesShown)
        {
            const GraphResourceDesc* desc = source.FindResource(name);
            const bool imported = desc && desc->imported;
            out << "  " << DotId("r_", name) << " [shape=ellipse," << (imported ? "style=filled,fillcolor=\"#d1e7dd\"" : "style=solid") << ",label=\"" << name;
            if (desc && desc->SizeBytes() > 0)
                out << "\\n" << desc->SizeBytes() / 1024 << " KiB";
            out << "\"];\n";
        }

        out << "}\n";
        return out.str();
    }

}
