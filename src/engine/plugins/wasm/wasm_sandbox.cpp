#include "engine/plugins/wasm/wasm_sandbox.hpp"

#include <algorithm>
#include <cstring>

#include <shard/shard_components.h>

#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/plugins/wasm/wasm_marshal.hpp"

namespace Shard::Engine::Plugins::Wasm {

    namespace {
        constexpr uint64_t kPageSize = 65536;
        constexpr uint32_t kPersistentChunkPages = 4;               // 256 KiB
        constexpr uint32_t kTempChunkPages = 8;                     // 512 KiB

        uint32_t SizeClass(uint32_t size)
        {
            uint32_t c = 16;
            while (c < size && c < (1u << 30)) c <<= 1;
            return c;
        }
    }

    // ---- SandboxHeap ----

    bool SandboxHeap::NewRegion(std::vector<Region>& regions, uint64_t minBytes, uint32_t minPages)
    {
        if (!m_In) return false;
        const uint64_t needed = (minBytes + kPageSize - 1) / kPageSize;
        if (needed == 0 || needed > 0xFFFF) return false;
        uint64_t pages = std::max<uint64_t>(minPages, needed);
        uint32_t first = 0;
        if (!m_In->GrowMemory(uint32_t(pages), first)) {
            // The usual step does not fit : the least that does the job may
            if (pages == needed || !m_In->GrowMemory(uint32_t(needed), first)) return false;
            pages = needed;
        }
        if ((uint64_t(first) + pages) * kPageSize > 0x100000000ull) return false;
        regions.push_back(Region{ uint32_t(uint64_t(first) * kPageSize), uint32_t(pages * kPageSize - (pages == 65536 ? 1 : 0)), 0 });
        m_Reserved += pages * kPageSize;
        return true;
    }

    uint32_t SandboxHeap::Alloc(uint32_t size, uint32_t align)
    {
        if (!m_In || size == 0 || align > 16 || (align & (align - 1)) != 0) return 0;
        const uint32_t cls = SizeClass(size);
        if (cls < size) return 0;
        auto& freeList = m_Free[cls];
        uint32_t at = 0;
        if (!freeList.empty()) {
            at = freeList.back();
            freeList.pop_back();
        } else {
            Region* region = nullptr;
            for (Region& r : m_Persistent)
                if (r.size - r.used >= cls) { region = &r; break; }
            if (!region) {
                if (!NewRegion(m_Persistent, cls, kPersistentChunkPages)) return 0;
                region = &m_Persistent.back();
            }
            at = region->base + region->used;
            region->used += cls;
        }
        m_Live[at] = cls;
        if (uint8_t* p = m_In->MemoryAt(at, cls)) std::memset(p, 0, cls);
        return at;
    }

    bool SandboxHeap::Free(uint32_t at)
    {
        auto it = m_Live.find(at);
        if (it == m_Live.end()) return false;
        m_Free[it->second].push_back(at);
        m_Live.erase(it);
        return true;
    }

    uint32_t SandboxHeap::Temp(uint64_t size, uint32_t align)
    {
        if (!m_In || align == 0 || (align & (align - 1)) != 0 || align > 4096 || size > 0xFFFFFFF0ull) return 0;
        const uint64_t need = std::max<uint64_t>(size, 1);
        // The current region, then the ones after it (kept from an earlier call), then a new one
        for (size_t i = m_TempAt; i < m_Temp.size(); ++i) {
            Region& r = m_Temp[i];
            const uint64_t start = (uint64_t(r.base) + r.used + align - 1) / align * align;
            if (start + need <= uint64_t(r.base) + r.size) {
                r.used = uint32_t(start + need - r.base);
                m_TempAt = i;
                return uint32_t(start);
            }
        }
        if (!NewRegion(m_Temp, need + align, kTempChunkPages)) return 0;
        m_TempAt = m_Temp.size() - 1;
        Region& r = m_Temp.back();
        const uint64_t start = (uint64_t(r.base) + align - 1) / align * align;
        r.used = uint32_t(start + need - r.base);
        return uint32_t(start);
    }

    void SandboxHeap::ResetTemp()
    {
        for (Region& r : m_Temp) r.used = 0;
        m_TempAt = 0;
    }

    void SandboxHeap::TempRelease(const Mark& mark)
    {
        if (m_Temp.empty()) return;
        for (size_t i = mark.region + 1; i < m_Temp.size(); ++i) m_Temp[i].used = 0;
        m_Temp[mark.region].used = mark.used;
        m_TempAt = mark.region;
    }

    uint32_t SandboxHeap::Intern(const std::string& text)
    {
        if (text.empty()) return 0;
        auto it = m_Interned.find(text);
        if (it != m_Interned.end()) return it->second;
        const uint32_t at = Alloc(uint32_t(text.size() + 1), 1);
        if (!at) return 0;
        m_In->Write(at, text.data(), uint32_t(text.size()));        // the zero terminator is already there : Alloc zeroes
        m_Interned.emplace(text, at);
        return at;
    }

    // ---- Sandbox ----

    void Sandbox::Log(uint32_t level, const std::string& message)
    {
        GetCHostApi()->log(ctx, level, ShardStr{ message.data(), message.size() });
    }

    void Sandbox::Kill(const std::string& message)
    {
        if (dead) return;
        dead = true;
        Log(SHARD_LOG_ERROR, "sandbox : " + message + " (the plugin is stopped)");
    }

    bool Sandbox::Enter(Budget budget, const std::function<Core::Result<std::vector<uint64_t>>()>& invoke, std::vector<uint64_t>& results, std::string* message)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (dead || !instance) return false;
        const bool outermost = depth == 0;
        if (outermost) {
            callInFrame = budget == Budget::Frame && frameOpen.load();
            instance->SetFuel(callInFrame ? frameLeft : fuel);
            fuelAtStart = callInFrame ? frameLeft : fuel;
        }
        ++depth;
        auto r = invoke();
        --depth;
        if (outermost) {
            const uint64_t left = instance->Fuel();
            fuelUsed = fuelAtStart > left ? fuelAtStart - left : 0;
            fuelUsedTotal += fuelUsed;
            if (callInFrame) frameLeft = left;
        }
        if (!r) {
            if (message) *message = r.GetError().message;
            if (!dead) {
                const bool overBudget = instance->LastTrap().kind == TrapKind::OutOfFuel;
                std::string text = r.GetError().message;
                if (overBudget) text += callInFrame ? " : over its frame budget (budgets.frameMs)" : " : over its budget for one call (budgets.frameMs)";
                Kill(text);
            }
            if (outermost) EndOutermost(false);
            return false;
        }
        if (outermost) EndOutermost(true);
        results = std::move(r.Value());
        return true;
    }

    bool Sandbox::CallTable(uint32_t index, const FuncType& type, const std::vector<uint64_t>& args, Budget budget, std::vector<uint64_t>& results)
    {
        return Enter(budget, [&] { return instance->CallIndirect(index, type, args); }, results, nullptr);
    }

    bool Sandbox::CallExport(const std::string& name, const std::vector<uint64_t>& args, Budget budget, std::vector<uint64_t>& results, std::string* message)
    {
        return Enter(budget, [&] { return instance->Call(name, args); }, results, message);
    }

    ShardResult Sandbox::CallResult(uint32_t index, const FuncType& type, const std::vector<uint64_t>& args, Budget budget)
    {
        std::vector<uint64_t> out;
        if (!CallTable(index, type, args, budget, out)) return SHARD_E_INTERNAL;
        return out.empty() ? SHARD_E_INTERNAL : ShardResult(int32_t(uint32_t(out[0])));
    }

    // The outermost call returned : what the module changed in its copies goes back to the host, the temporary memory is free again. After a
    // trap the module's state is not to be trusted, so nothing it wrote is passed on.
    void Sandbox::EndOutermost(bool ok)
    {
        if (ok && instance) {
            for (const TempMirror& m : m_Temp) {
                if (!m.writeBack) continue;
                if (const uint8_t* p = instance->MemoryAt(m.at, m.size)) std::memcpy(m.host, p, m.size);
            }
            for (auto& fn : m_AfterCall) fn();
            // The blobs the module holds a pointer to : validated again (the blob may be gone), written only when something changed
            if (apis.components) {
                for (auto it = m_Blobs.begin(); it != m_Blobs.end();) {
                    void* host = nullptr;
                    uint64_t size = 0;
                    const BlobMirror& m = it->second;
                    if (apis.components->blob_data(ctx, it->first, &host, &size) != SHARD_OK || host != m.host || size != m.size) {
                        it = m_Blobs.erase(it);
                        continue;
                    }
                    if (const uint8_t* p = instance->MemoryAt(m.at, uint32_t(m.size)))
                        if (m.size && std::memcmp(host, p, size_t(m.size)) != 0) std::memcpy(host, p, size_t(m.size));
                    ++it;
                }
            }
        }
        m_AfterCall.clear();
        m_Temp.clear();
        m_TempIndex.clear();
        heap.ResetTemp();
        if (!ok) m_Blobs.clear();
    }

    uint32_t Sandbox::MirrorTemp(void* host, uint32_t size, bool writeBack)
    {
        auto it = m_TempIndex.find(host);
        if (it != m_TempIndex.end() && m_Temp[it->second].size == size) {
            m_Temp[it->second].writeBack = m_Temp[it->second].writeBack || writeBack;
            return m_Temp[it->second].at;
        }
        const uint32_t at = heap.Temp(size, 16);
        if (!at) return 0;
        if (size && !instance->Write(at, host, size)) return 0;
        m_TempIndex[host] = m_Temp.size();
        m_Temp.push_back(TempMirror{ host, at, size, writeBack });
        return at;
    }

    uint32_t Sandbox::MirrorBlob(ShardBlob blob, void* host, uint64_t size)
    {
        auto it = m_Blobs.find(blob);
        if (it != m_Blobs.end() && it->second.host == host && it->second.size == size) return it->second.at;
        if (it != m_Blobs.end()) DropBlobMirror(blob);
        if (size > 0xFFFFFFF0ull) return 0;
        const uint32_t at = heap.Alloc(uint32_t(std::max<uint64_t>(size, 1)), 16);
        if (!at) return 0;
        if (size && !instance->Write(at, host, uint32_t(size))) { heap.Free(at); return 0; }
        m_Blobs[blob] = BlobMirror{ at, host, size };
        return at;
    }

    void Sandbox::DropBlobMirror(ShardBlob blob)
    {
        auto it = m_Blobs.find(blob);
        if (it == m_Blobs.end()) return;
        const BlobMirror m = it->second;
        m_Blobs.erase(it);
        // Written back first (the host is about to resize or free the blob) : only if it is still the same one
        void* host = nullptr;
        uint64_t size = 0;
        if (apis.components && apis.components->blob_data(ctx, blob, &host, &size) == SHARD_OK && host == m.host && size == m.size)
            if (const uint8_t* p = instance->MemoryAt(m.at, uint32_t(m.size)))
                if (m.size) std::memcpy(host, p, size_t(m.size));
        heap.Free(m.at);
    }

    bool Sandbox::ReturnString(Instance& in, uint64_t sret, ShardStr text)
    {
        uint8_t* out = Marshal::Span(in, sret, Layout::ShardStr::kSize, "result");
        if (!out) return false;
        std::memset(out, 0, Layout::ShardStr::kSize);
        if (text.data && text.size) {
            const uint32_t at = heap.Intern(std::string(text.data, size_t(text.size)));
            if (!at) { in.HostTrap("no room left in the memory for a string"); return false; }
            out = Marshal::Span(in, sret, Layout::ShardStr::kSize, "result");     // the heap may have grown the memory
            if (!out) return false;
            Marshal::Store<uint32_t>(out, Layout::ShardStr::o_data, at);
            Marshal::Store<uint64_t>(out, Layout::ShardStr::o_size, text.size);
        }
        return true;
    }
}
