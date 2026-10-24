#include "engine/plugins/wasm/wasm_plugin.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>

#include <shard/shard_host.h>

#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/plugins/crypto.hpp"
#include "engine/plugins/wasm/wasm_imports.hpp"
#include "engine/plugins/wasm/wasm_marshal.hpp"
#include "engine/plugins/wasm/wasm_sandbox.hpp"

namespace Shard::Engine::Plugins {

    using Core::Err;
    using Core::Ok;

    namespace {
        using namespace Wasm;
        namespace fs = std::filesystem;

        std::mutex gMutex;
        std::map<PluginOwner, std::unique_ptr<Sandbox>> gSandboxes;
        std::atomic<uint64_t> gCacheHits{ 0 }, gCacheMisses{ 0 }, gCacheWrites{ 0 };

        // The offsets of the callbacks in the wasm32 ShardPluginCallbacks, by slot
        constexpr uint32_t kSlotOffsets[SlotCount] = {
            Layout::ShardPluginCallbacks::o_on_load, Layout::ShardPluginCallbacks::o_on_initialize, Layout::ShardPluginCallbacks::o_on_enable,
            Layout::ShardPluginCallbacks::o_on_disable, Layout::ShardPluginCallbacks::o_on_unload, Layout::ShardPluginCallbacks::o_on_prepare_reload,
            Layout::ShardPluginCallbacks::o_on_restore, Layout::ShardPluginCallbacks::o_on_frame_begin, Layout::ShardPluginCallbacks::o_on_frame_end,
            Layout::ShardPluginCallbacks::o_on_content_mounted, Layout::ShardPluginCallbacks::o_on_world_loaded,
            Layout::ShardPluginCallbacks::o_on_world_unloaded, Layout::ShardPluginCallbacks::o_on_play_start, Layout::ShardPluginCallbacks::o_on_play_stop,
        };

        // ---- The callbacks the host makes : trampolines into the sandbox ----

        // A plugin that trapped is stopped for good : it answers an error to whatever comes, and nothing to being switched off
        template <Slot S>
        ShardResult SHARD_CALL Lifecycle(void* user, ShardPluginCtx)
        {
            auto* s = static_cast<Sandbox*>(user);
            if (s->dead) return (S == OnDisable || S == OnUnload) ? SHARD_OK : SHARD_E_INTERNAL;
            if (S == OnLoad) ResetRegistrations(*s);            // what the previous context registered went with it
            return s->CallResult(s->callbacks[S], kTypeLifecycle, { s->user, s->ctx }, Budget::Call);
        }

        template <Slot S>
        ShardResult SHARD_CALL Frame(void* user, ShardPluginCtx, uint64_t frame)
        {
            auto* s = static_cast<Sandbox*>(user);
            if (s->dead) return SHARD_E_INTERNAL;
            return s->CallResult(s->callbacks[S], kTypeFrame, { s->user, s->ctx, frame }, Budget::Frame);
        }

        template <Slot S>
        ShardResult SHARD_CALL State(void* user, ShardPluginCtx, ShardState state)
        {
            auto* s = static_cast<Sandbox*>(user);
            if (s->dead) return SHARD_E_INTERNAL;
            return s->CallResult(s->callbacks[S], kTypeState, { s->user, s->ctx, state }, Budget::Call);
        }

        // The world a callback is told about, as a wasm32 ShardWorldInfo in the module's memory
        template <Slot S>
        ShardResult SHARD_CALL World(void* user, ShardPluginCtx, const ShardWorldInfo* world)
        {
            auto* s = static_cast<Sandbox*>(user);
            std::lock_guard<std::recursive_mutex> lock(s->mutex);
            if (s->dead) return SHARD_E_INTERNAL;
            namespace Lw = Layout::ShardWorldInfo;
            const uint32_t nameSize = world ? uint32_t(world->name.size) : 0;
            const uint32_t at = s->heap.Temp(Lw::kSize + nameSize, 8);
            if (!at) return SHARD_E_OUT_OF_MEMORY;
            uint8_t bytes[Lw::kSize] = {};
            Marshal::Store<uint32_t>(bytes, Lw::o_structSize, Lw::kSize);
            if (world) {
                Marshal::Store<int32_t>(bytes, Lw::o_assetId, world->assetId);
                Marshal::Store<uint32_t>(bytes, Lw::o_name, nameSize ? at + Lw::kSize : 0);
                Marshal::Store<uint64_t>(bytes, Lw::o_name + 8, nameSize);
                if (nameSize) s->instance->Write(at + Lw::kSize, world->name.data, nameSize);
            }
            s->instance->Write(at, bytes, sizeof(bytes));
            return s->CallResult(s->callbacks[S], kTypeWorld, { s->user, s->ctx, at }, Budget::Call);
        }

        // ---- The compiled module, from the cache when there is one ----

        std::string CacheFile(const std::string& directory, const std::vector<uint8_t>& bytes)
        {
            const std::string hash = Crypto::ToHex(Crypto::Sha256Of(bytes.data(), bytes.size()));
            return directory + "/wasm/" + hash + "." + Module::RuntimeVersion() + ".cwasm";
        }

        // The compiled code is a file of this host's own making : nothing but a plain integrity trailer (a SHA-256) guards it against a
        // half written file. It is machine code that the host runs, so the cache folder is as trusted as the engine's own files.
        std::shared_ptr<const Module> ReadCached(const std::string& file, const Limits& limits)
        {
            std::ifstream in(file, std::ios::binary);
            if (!in) return nullptr;
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (data.size() <= 32) return nullptr;
            const Crypto::Sha256Digest digest = Crypto::Sha256Of(data.data(), data.size() - 32);
            if (std::memcmp(digest.data(), data.data() + data.size() - 32, 32) != 0) return nullptr;
            auto module = Module::Deserialize(data.data(), data.size() - 32, limits);
            return module ? module.Value() : nullptr;
        }

        void WriteCached(const std::string& file, const Module& module)
        {
            auto compiled = module.Serialize();
            if (!compiled) return;
            std::vector<uint8_t>& data = compiled.Value();
            const Crypto::Sha256Digest digest = Crypto::Sha256Of(data.data(), data.size());
            data.insert(data.end(), digest.begin(), digest.end());
            std::error_code ec;
            fs::create_directories(fs::path(file).parent_path(), ec);
            // Written whole under another name, then renamed : another process never reads half of it
            const std::string temporary = file + ".tmp" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            {
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                if (!out) return;
                out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
                if (!out) { out.close(); fs::remove(temporary, ec); return; }
            }
            fs::rename(temporary, file, ec);
            if (ec) fs::remove(temporary, ec); else ++gCacheWrites;
        }

        Core::Result<std::shared_ptr<const Module>> LoadModule(const std::vector<uint8_t>& bytes, const Limits& limits, const std::string& cacheDirectory)
        {
            std::string cached;
            if (!cacheDirectory.empty()) {
                cached = CacheFile(cacheDirectory, bytes);
                if (auto module = ReadCached(cached, limits)) { ++gCacheHits; return Ok(std::move(module)); }
            }
            auto module = Module::Decode(bytes.data(), bytes.size(), limits);
            if (!module) return module;
            if (!cached.empty()) { ++gCacheMisses; WriteCached(cached, *module.Value()); }
            return module;
        }
    }

    Core::Result<IPlugin*> CreateWasmPlugin(PluginOwner owner, const PluginManifest& manifest, const std::string& path, const std::string& cacheDirectory)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return Err("cannot read '" + path + "'");
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        Limits limits;
        const double memoryMB = manifest.budgetMemoryMB > 0 ? manifest.budgetMemoryMB : kWasmDefaultMemoryMB;
        limits.maxMemoryPages = std::max<uint32_t>(1, uint32_t(memoryMB * 1024 * 1024 / 65536));
        auto module = LoadModule(bytes, limits, cacheDirectory);
        if (!module) return Err(module.GetError().message);

        auto s = std::make_unique<Sandbox>();
        s->owner = owner;
        s->ctx = ShardPluginCtx(owner);
        s->pluginId = manifest.id;
        s->apis = QueryHostApis();
        for (uint32_t p = 1; p < kPermissionCount; ++p)
            s->permissions[p] = std::find(manifest.permissions.begin(), manifest.permissions.end(), kPermissionNames[p]) != manifest.permissions.end();
        const double frameMs = manifest.budgetFrameMs > 0 ? manifest.budgetFrameMs : kWasmDefaultFrameMs;
        s->fuel = uint64_t(frameMs * double(kWasmFuelPerMs));
        s->frameLeft = s->fuel;

        ImportList imports(*s);
        AddGeneratedImports(*s, imports, s->apis);
        AddCoreImports(*s, imports, s->apis);
        AddEcsImports(*s, imports, s->apis);
        AddScriptImports(*s, imports, s->apis);
        AddEditorImports(*s, imports, s->apis);
        AddWasiImports(*s, imports, *module.Value());
        auto instance = Instance::Create(module.Value(), imports.Take(), s->fuel);
        if (!instance) return Err(instance.GetError().message);
        s->instance = std::move(instance.Value());
        s->heap.Attach(s->instance.get());
        Instance& in = *s->instance;

        // The entry point must be there with the right shape before anything of the module runs
        const FuncType entryType{ { ValType::I32, ValType::I32 }, { ValType::I32 } };
        bool hasEntry = false;
        for (const Module::Export& e : module.Value()->Exports())
            if (e.kind == 0 && e.name == manifest.entry) hasEntry = e.type == entryType;
        if (!hasEntry) return Err("the module does not export '" + manifest.entry + "' as (i32 host, i32 info) -> i32");

        // The C and C++ runtime of the module (static constructors), when it has one : a wasi-sdk reactor exports _initialize, a module linked
        // by hand with --export=__wasm_call_ctors the second
        std::vector<uint64_t> ignored;
        std::string why;
        const char* init = in.HasFunction("_initialize") ? "_initialize" : in.HasFunction("__wasm_call_ctors") ? "__wasm_call_ctors" : nullptr;
        if (init && !s->CallExport(init, {}, Budget::Call, ignored, &why)) return Err(std::string("its ") + init + " trapped : " + why);

        // The entry point fills a wasm32 ShardPluginInfo, like a native one fills the host's. The host table is the one the module built (its
        // address is exported), or 0 for a module that reaches the host through its imports directly.
        namespace Li = Layout::ShardPluginInfo;
        uint64_t hostAt = 0;
        if (!in.ExportedGlobal("shard_wasm_host_table", hostAt)) hostAt = 0;
        const uint32_t infoAt = s->heap.Alloc(Li::kSize, 8);
        if (!infoAt) return Err("the module has no room left in its memory for its ShardPluginInfo");
        uint64_t tableAt = 0;                       // the widgets and the gizmo painter the module built from the imports, for its editor callbacks
        if (in.ExportedGlobal("shard_wasm_ui_table", tableAt)) s->uiTableAt = uint32_t(tableAt);
        if (in.ExportedGlobal("shard_wasm_gizmo_table", tableAt)) s->gizmoTableAt = uint32_t(tableAt);
        std::vector<uint64_t> entered;
        if (!s->CallExport(manifest.entry, { hostAt, infoAt }, Budget::Call, entered, &why)) return Err("its entry point trapped : " + why);
        const ShardResult result = entered.empty() ? SHARD_E_INTERNAL : ShardResult(int32_t(uint32_t(entered[0])));
        if (result != SHARD_OK) return Err("its entry point '" + manifest.entry + "' refused to load (" + std::to_string(result) + ")");

        uint8_t info[Li::kSize] = {};
        in.Read(infoAt, info, Li::kSize);
        s->heap.Free(infoAt);
        const uint32_t structSize = Marshal::Load<uint32_t>(info, Li::o_structSize);
        const uint32_t abiMajor = Marshal::Load<uint32_t>(info, Li::o_abiMajor), abiMinor = Marshal::Load<uint32_t>(info, Li::o_abiMinor);
        s->user = Marshal::Load<uint32_t>(info, Li::o_user);
        const uint32_t callbacksAt = Marshal::Load<uint32_t>(info, Li::o_callbacks);
        if (structSize < Li::kSize) return Err("its ShardPluginInfo is too small (" + std::to_string(structSize) + " bytes, wasm32 needs " + std::to_string(Li::kSize) + ")");
        uint32_t callbacksSize = 0;
        if (!in.Read(callbacksAt, &callbacksSize, 4) || callbacksSize < 4 + 4 * 5) return Err("its ShardPluginCallbacks is missing or too small");
        for (uint32_t i = 0; i < SlotCount; ++i)
            if (kSlotOffsets[i] + 4 <= callbacksSize && !in.Read(callbacksAt + kSlotOffsets[i], &s->callbacks[i], 4)) return Err("its ShardPluginCallbacks is out of its memory");

        // The native view CAbiPlugin works with : the same checks as a C plugin's
        ShardPluginCallbacks cb;
        std::memset(&cb, 0, sizeof(cb));
        cb.structSize = sizeof(ShardPluginCallbacks);
        if (s->callbacks[OnLoad]) cb.on_load = Lifecycle<OnLoad>;
        if (s->callbacks[OnInitialize]) cb.on_initialize = Lifecycle<OnInitialize>;
        if (s->callbacks[OnEnable]) cb.on_enable = Lifecycle<OnEnable>;
        if (s->callbacks[OnDisable]) cb.on_disable = Lifecycle<OnDisable>;
        if (s->callbacks[OnUnload]) cb.on_unload = Lifecycle<OnUnload>;
        if (s->callbacks[OnPrepareReload]) cb.on_prepare_reload = State<OnPrepareReload>;
        if (s->callbacks[OnRestore]) cb.on_restore = State<OnRestore>;
        if (s->callbacks[OnFrameBegin]) cb.on_frame_begin = Frame<OnFrameBegin>;
        if (s->callbacks[OnFrameEnd]) cb.on_frame_end = Frame<OnFrameEnd>;
        if (s->callbacks[OnContentMounted]) cb.on_content_mounted = Lifecycle<OnContentMounted>;
        if (s->callbacks[OnWorldLoaded]) cb.on_world_loaded = World<OnWorldLoaded>;
        if (s->callbacks[OnWorldUnloaded]) cb.on_world_unloaded = World<OnWorldUnloaded>;
        if (s->callbacks[OnPlayStart]) cb.on_play_start = World<OnPlayStart>;
        if (s->callbacks[OnPlayStop]) cb.on_play_stop = World<OnPlayStop>;
        s->native = cb;

        ShardPluginInfo native;
        std::memset(&native, 0, sizeof(native));
        native.structSize = sizeof(ShardPluginInfo);
        native.abiMajor = abiMajor;
        native.abiMinor = abiMinor;
        native.id = ShardStr{ manifest.id.data(), manifest.id.size() };
        native.user = s.get();
        native.callbacks = &s->native;
        const std::string problem = ValidateCPluginInfo(native);
        if (!problem.empty()) return Err(problem);

        IPlugin* plugin = CreateCPlugin(owner, native);
        std::lock_guard<std::mutex> lock(gMutex);
        gSandboxes[owner] = std::move(s);           // a reload replaces the old sandbox, whose plugin object is gone by then
        return Ok(plugin);
    }

    void ReleaseWasmPlugin(PluginOwner owner)
    {
        std::unique_ptr<Sandbox> gone;
        {
            std::lock_guard<std::mutex> lock(gMutex);
            auto it = gSandboxes.find(owner);
            if (it == gSandboxes.end()) return;
            gone = std::move(it->second);
            gSandboxes.erase(it);
        }
    }

    void BeginWasmFrame()
    {
        std::lock_guard<std::mutex> lock(gMutex);
        for (auto& [owner, sandbox] : gSandboxes) {
            sandbox->frameLeft = sandbox->fuel;
            sandbox->frameOpen = true;
        }
    }

    void EndWasmFrame()
    {
        std::lock_guard<std::mutex> lock(gMutex);
        for (auto& [owner, sandbox] : gSandboxes) sandbox->frameOpen = false;
    }

    bool CallWasmExport(PluginOwner owner, const std::string& name, const std::vector<uint64_t>& args, std::vector<uint64_t>& results, std::string* error)
    {
        Sandbox* sandbox = nullptr;
        {
            std::lock_guard<std::mutex> lock(gMutex);
            auto it = gSandboxes.find(owner);
            if (it != gSandboxes.end()) sandbox = it->second.get();
        }
        if (!sandbox) { if (error) *error = "no sandbox for that plugin"; return false; }
        std::string why;
        const bool ok = sandbox->CallExport(name, args, Budget::Call, results, &why);
        if (!ok && error) *error = why.empty() ? "the plugin was stopped" : why;
        return ok;
    }

    bool WriteWasmMemory(PluginOwner owner, uint32_t offset, const void* data, uint32_t size)
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gSandboxes.find(owner);
        return it != gSandboxes.end() && it->second->instance && it->second->instance->Write(offset, data, size);
    }

    bool ReadWasmMemory(PluginOwner owner, uint32_t offset, void* out, uint32_t size)
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gSandboxes.find(owner);
        return it != gSandboxes.end() && it->second->instance && it->second->instance->Read(offset, out, size);
    }

    uint64_t WasmFuelUsed(PluginOwner owner)
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gSandboxes.find(owner);
        return it == gSandboxes.end() ? 0 : it->second->fuelUsedTotal;
    }

    WasmCacheStats GetWasmCacheStats() { return WasmCacheStats{ gCacheHits.load(), gCacheMisses.load(), gCacheWrites.load() }; }
}
