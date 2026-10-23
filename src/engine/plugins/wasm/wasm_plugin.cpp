#include "engine/plugins/wasm/wasm_plugin.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>

#include <shard/shard_commands.h>
#include <shard/shard_content.h>
#include <shard/shard_cvars.h>
#include <shard/shard_host.h>
#include <shard/shard_plugin.h>

#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/plugins/wasm/wasm_runtime.hpp"

namespace Shard::Engine::Plugins {

    using Core::Err;
    using Core::Ok;

    namespace {
        using namespace Wasm;

        // The callbacks of the wasm32 ShardPluginCallbacks the host calls, in the order of the struct (4 bytes each after structSize)
        enum Slot : uint32_t { OnLoad, OnInitialize, OnEnable, OnDisable, OnUnload, OnPrepareReload, OnRestore, OnFrameBegin, OnFrameEnd, SlotCount };

        struct Sandbox;
        // A console command whose code is in the sandbox
        struct Command {
            Sandbox* sandbox = nullptr;
            uint32_t run = 0;                       ///< table index
            uint32_t user = 0;
            std::string output;                     ///< the text the last run gave, copied out of the sandbox
        };

        struct Sandbox {
            std::unique_ptr<Instance> instance;
            uint32_t scratch = 0;                   ///< a page of the sandbox's memory only the host writes in : the arguments of the callbacks it makes
            std::vector<std::unique_ptr<Command>> commands;
            ShardPluginCtx ctx = 0;
            uint32_t user = 0;                      ///< the plugin's `user`, an address in its own memory : handed back as it is
            uint32_t callbacks[SlotCount] = {};     ///< table indices, 0 : none
            uint64_t fuel = 0;                      ///< the budget : per frame, and per call outside a frame
            uint64_t frameLeft = 0;                 ///< what is left of this frame's
            bool dead = false;                      ///< after a trap : the instance's state can no longer be trusted
            ShardPluginCallbacks native{};          ///< what CAbiPlugin calls
        };

        std::mutex gMutex;
        std::map<PluginOwner, std::unique_ptr<Sandbox>> gSandboxes;

        void Log(ShardPluginCtx ctx, uint32_t level, const std::string& message)
        {
            GetCHostApi()->log(ctx, level, ShardStr{ message.data(), message.size() });
        }

        ShardResult Invoke(void* user, Slot slot, std::vector<uint64_t> args, const FuncType& type)
        {
            auto* s = static_cast<Sandbox*>(user);
            if (s->dead) return (slot == OnDisable || slot == OnUnload) ? SHARD_OK : SHARD_E_INTERNAL;
            const bool inFrame = slot == OnFrameBegin || slot == OnFrameEnd;
            s->instance->SetFuel(inFrame ? s->frameLeft : s->fuel);
            args.insert(args.begin(), { uint64_t(s->user), uint64_t(s->ctx) });
            auto r = s->instance->CallIndirect(s->callbacks[slot], type, args);
            if (inFrame) s->frameLeft = s->instance->Fuel();
            if (!r) {
                s->dead = true;
                const bool overBudget = s->instance->LastTrap().kind == TrapKind::OutOfFuel;
                Log(s->ctx, SHARD_LOG_ERROR, "sandbox : " + r.GetError().message +
                                                 (overBudget ? (inFrame ? " : over its frame budget (budgets.frameMs)" : " : over its budget for one call (budgets.frameMs)") : "") +
                                                 " (the plugin is stopped)");
                return SHARD_E_INTERNAL;
            }
            return r.Value().empty() ? SHARD_E_INTERNAL : ShardResult(int32_t(uint32_t(r.Value()[0])));
        }

        const FuncType kLifecycle{ { ValType::I32, ValType::I64 }, { ValType::I32 } };
        const FuncType kFrame{ { ValType::I32, ValType::I64, ValType::I64 }, { ValType::I32 } };

        template <Slot S> ShardResult SHARD_CALL Lifecycle(void* user, ShardPluginCtx) { return Invoke(user, S, {}, kLifecycle); }
        template <Slot S> ShardResult SHARD_CALL Frame(void* user, ShardPluginCtx, uint64_t frame) { return Invoke(user, S, { frame }, kFrame); }

        float AsFloat(uint64_t bits) { const uint32_t b = uint32_t(bits); float f; std::memcpy(&f, &b, 4); return f; }

        // A wasm32 ShardStr { u32 data, u32 pad, u64 size } at `at`, as a host ShardStr pointing into the sandbox's memory (valid for the call)
        bool ReadStr(Instance& in, uint32_t at, ShardStr& out)
        {
            uint32_t data = 0;
            uint64_t size = 0;
            if (!in.Read(at, &data, 4) || !in.Read(at + 8, &size, 8) || size > 1024 * 1024) { in.HostTrap("string out of the memory"); return false; }
            const uint8_t* bytes = in.MemoryAt(data, uint32_t(size));
            if (!bytes) { in.HostTrap("string out of the memory"); return false; }
            out = ShardStr{ reinterpret_cast<const char*>(bytes), size };
            return true;
        }

        const void* Interface(const char* id)
        {
            const void* api = nullptr;
            GetCHostApi()->query_interface(ShardStr{ id, std::strlen(id) }, 1, 0, &api);
            return api;
        }
        const ShardCVarsApi* CVarsApi() { return static_cast<const ShardCVarsApi*>(Interface(SHARD_IFACE_CVARS)); }
        const ShardCommandsApi* CommandsApi() { return static_cast<const ShardCommandsApi*>(Interface(SHARD_IFACE_COMMANDS)); }
        const ShardContentApi* ContentApi() { return static_cast<const ShardContentApi*>(Interface(SHARD_IFACE_CONTENT)); }

        bool HasPermission(ShardPluginCtx ctx, const char* permission)
        {
            const void* api = nullptr;
            const char* id = SHARD_IFACE_PLUGIN;
            GetCHostApi()->query_interface(ShardStr{ id, std::strlen(id) }, 1, 1, &api);
            const auto* plugin = static_cast<const ShardPluginApi*>(api);
            return plugin && plugin->has_permission(ctx, ShardStr{ permission, std::strlen(permission) }) == SHARD_TRUE;
        }

        // A command typed in the console : its words are copied into the scratch page as wasm32 ShardStr, the callback runs in the sandbox
        // as (i32 user, i64 ctx, i32 args, i32 argCount, i32 output) -> i32, and the text it points `output` to is copied back out
        ShardResult SHARD_CALL RunCommand(void* user, ShardPluginCtx, const ShardStr* args, uint32_t argCount, ShardStr* output)
        {
            auto* c = static_cast<Command*>(user);
            Sandbox& s = *c->sandbox;
            if (s.dead) return SHARD_E_INTERNAL;
            Instance& in = *s.instance;
            const uint32_t outAt = s.scratch, argsAt = s.scratch + 16;
            uint64_t textAt = uint64_t(argsAt) + 16ull * argCount;
            if (textAt > uint64_t(s.scratch) + 65536) return SHARD_E_INVALID_ARG;
            const uint8_t zero[16] = {};
            in.Write(outAt, zero, 16);
            for (uint32_t i = 0; i < argCount; ++i) {
                if (textAt + args[i].size > uint64_t(s.scratch) + 65536) return SHARD_E_INVALID_ARG;      // more than the page holds
                const uint32_t data = uint32_t(textAt), pad = 0;
                const uint64_t size = args[i].size;
                in.Write(argsAt + 16 * i, &data, 4);
                in.Write(argsAt + 16 * i + 4, &pad, 4);
                in.Write(argsAt + 16 * i + 8, &size, 8);
                if (size) in.Write(data, args[i].data, uint32_t(size));
                textAt += size;
            }
            static const FuncType type{ { ValType::I32, ValType::I64, ValType::I32, ValType::I32, ValType::I32 }, { ValType::I32 } };
            in.SetFuel(s.fuel);
            auto r = in.CallIndirect(c->run, type, { c->user, s.ctx, argsAt, argCount, outAt });
            if (!r) {
                s.dead = true;
                Log(s.ctx, SHARD_LOG_ERROR, "sandbox : " + r.GetError().message + " (the plugin is stopped)");
                return SHARD_E_INTERNAL;
            }
            uint32_t data = 0;
            uint64_t size = 0;
            in.Read(outAt, &data, 4);
            in.Read(outAt + 8, &size, 8);
            const uint8_t* text = size <= 65536 ? in.MemoryAt(data, uint32_t(size)) : nullptr;
            c->output.assign(text ? reinterpret_cast<const char*>(text) : "", text ? size_t(size) : 0);
            *output = ShardStr{ c->output.data(), c->output.size() };
            return ShardResult(int32_t(uint32_t(r.Value()[0])));
        }

        // The host functions the sandbox may import. The ctx a sandbox passes is never trusted : every import acts for the sandbox's own plugin.
        std::vector<HostImport> Imports(Sandbox& s)
        {
            std::vector<HostImport> imports;
            imports.push_back({ "shard", "log", { { ValType::I64, ValType::I32, ValType::I32 }, {} }, [&s](Instance& in, const uint64_t* a, uint64_t*) {
                uint32_t data = 0;
                uint64_t size = 0;
                if (!in.Read(uint32_t(a[2]), &data, 4) || !in.Read(uint32_t(a[2]) + 8, &size, 8)) { in.HostTrap("message out of the memory"); return false; }
                if (size > 64 * 1024) { in.HostTrap("message longer than 64 KiB"); return false; }
                const uint8_t* bytes = in.MemoryAt(data, uint32_t(size));
                if (!bytes) { in.HostTrap("message out of the memory"); return false; }
                const uint32_t level = std::min<uint32_t>(uint32_t(a[1]), SHARD_LOG_ERROR);
                GetCHostApi()->log(s.ctx, level, ShardStr{ reinterpret_cast<const char*>(bytes), size });
                return true;
            } });

            // shard.cvars : the native functions of the C ABI, which scope every handle to the plugin ; the sandbox's arguments are read
            // from its memory (a ShardStr by value is passed by address in wasm32) and the results written back there
            const ShardCVarsApi* cvars = CVarsApi();
            if (cvars) {
                const ValType I = ValType::I32, L = ValType::I64, F = ValType::F32;
                imports.push_back({ "shard", "cvars.create_float", { { L, I, F, I, I, F, F, I }, { I } }, [&s, cvars](Instance& in, const uint64_t* a, uint64_t* r) {
                    ShardStr name, help;
                    if (!ReadStr(in, uint32_t(a[1]), name) || !ReadStr(in, uint32_t(a[3]), help) || !in.MemoryAt(uint32_t(a[7]), 8)) return false;
                    ShardCVar out = 0;
                    r[0] = uint32_t(cvars->create_float(s.ctx, name, AsFloat(a[2]), help, uint32_t(a[4]), AsFloat(a[5]), AsFloat(a[6]), &out));
                    return in.Write(uint32_t(a[7]), &out, 8);
                } });
                imports.push_back({ "shard", "cvars.create_int", { { L, I, I, I, I, I, I, I }, { I } }, [&s, cvars](Instance& in, const uint64_t* a, uint64_t* r) {
                    ShardStr name, help;
                    if (!ReadStr(in, uint32_t(a[1]), name) || !ReadStr(in, uint32_t(a[3]), help) || !in.MemoryAt(uint32_t(a[7]), 8)) return false;
                    ShardCVar out = 0;
                    r[0] = uint32_t(cvars->create_int(s.ctx, name, int32_t(a[2]), help, uint32_t(a[4]), int32_t(a[5]), int32_t(a[6]), &out));
                    return in.Write(uint32_t(a[7]), &out, 8);
                } });
                imports.push_back({ "shard", "cvars.get_float", { { L, L, I }, { I } }, [&s, cvars](Instance& in, const uint64_t* a, uint64_t* r) {
                    if (!in.MemoryAt(uint32_t(a[2]), 4)) { in.HostTrap("output out of the memory"); return false; }
                    float out = 0.0f;
                    r[0] = uint32_t(cvars->get_float(s.ctx, a[1], &out));
                    return in.Write(uint32_t(a[2]), &out, 4);
                } });
                imports.push_back({ "shard", "cvars.get_int", { { L, L, I }, { I } }, [&s, cvars](Instance& in, const uint64_t* a, uint64_t* r) {
                    if (!in.MemoryAt(uint32_t(a[2]), 4)) { in.HostTrap("output out of the memory"); return false; }
                    int32_t out = 0;
                    r[0] = uint32_t(cvars->get_int(s.ctx, a[1], &out));
                    return in.Write(uint32_t(a[2]), &out, 4);
                } });
                imports.push_back({ "shard", "cvars.set_float", { { L, L, F }, { I } }, [&s, cvars](Instance&, const uint64_t* a, uint64_t* r) {
                    r[0] = uint32_t(cvars->set_float(s.ctx, a[1], AsFloat(a[2])));
                    return true;
                } });
                imports.push_back({ "shard", "cvars.set_int", { { L, L, I }, { I } }, [&s, cvars](Instance&, const uint64_t* a, uint64_t* r) {
                    r[0] = uint32_t(cvars->set_int(s.ctx, a[1], int32_t(a[2])));
                    return true;
                } });
            }

            // shard.commands : the callback is an index in the sandbox's table, called by RunCommand
            if (const ShardCommandsApi* commands = CommandsApi()) {
                const ValType I = ValType::I32, L = ValType::I64;
                imports.push_back({ "shard", "commands.add", { { L, I, I, I, I }, { I } }, [&s, commands](Instance& in, const uint64_t* a, uint64_t* r) {
                    ShardStr name, help;
                    if (!ReadStr(in, uint32_t(a[1]), name) || !ReadStr(in, uint32_t(a[2]), help)) return false;
                    auto command = std::make_unique<Command>();
                    command->sandbox = &s;
                    command->run = uint32_t(a[3]);
                    command->user = uint32_t(a[4]);
                    r[0] = uint32_t(commands->add(s.ctx, name, help, &RunCommand, command.get()));
                    if (int32_t(uint32_t(r[0])) == SHARD_OK) s.commands.push_back(std::move(command));
                    return true;
                } });
            }

            // shard.content, permission "fs.read:content" : a file of the plugin's own content folder, copied into a buffer of the sandbox.
            // Only its own : no mod:// address of another module, and no host path (resolve is not offered)
            if (const ShardContentApi* content = ContentApi()) {
                const ValType I = ValType::I32, L = ValType::I64;
                imports.push_back({ "shard", "content.read", { { L, I, I, L, I }, { I } }, [&s, content](Instance& in, const uint64_t* a, uint64_t* r) {
                    ShardStr path;
                    if (!ReadStr(in, uint32_t(a[1]), path)) return false;
                    const uint32_t buffer = uint32_t(a[2]), sizeAt = uint32_t(a[4]);
                    const uint64_t capacity = a[3];
                    if (capacity > 0xFFFFFFFFull || !in.MemoryAt(buffer, uint32_t(capacity)) || !in.MemoryAt(sizeAt, 8)) {
                        in.HostTrap("buffer out of the memory");
                        return false;
                    }
                    if (!HasPermission(s.ctx, "fs.read:content")) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
                    char uri[1024];
                    uint64_t uriSize = 0;
                    // A path inside the folder : nothing absolute, no way up (the mounts check it too ; this does not rely on it)
                    const std::string relative(path.data, size_t(path.size));
                    if (relative.find("..") != std::string::npos || relative.find(':') != std::string::npos || relative.find('\\') != std::string::npos ||
                        (!relative.empty() && relative[0] == '/')) {
                        r[0] = uint32_t(SHARD_E_INVALID_ARG);
                        return true;
                    }
                    ShardResult result = content->uri_of(s.ctx, path, uri, sizeof(uri), &uriSize);
                    if (result == SHARD_OK && uriSize > sizeof(uri)) result = SHARD_E_INVALID_ARG;
                    if (result != SHARD_OK) { r[0] = uint32_t(result); return true; }
                    void* data = nullptr;
                    uint64_t size = 0;
                    result = content->read(s.ctx, ShardStr{ uri, uriSize }, &data, &size);
                    if (result != SHARD_OK) { r[0] = uint32_t(result); return true; }
                    in.Write(buffer, data, uint32_t(std::min(size, capacity)));
                    in.Write(sizeAt, &size, 8);             // the whole size : a buffer too small gets the start, and learns what it needs
                    GetCHostApi()->free(s.ctx, data);
                    r[0] = uint32_t(SHARD_OK);
                    return true;
                } });
            }
            return imports;
        }
    }

    Core::Result<IPlugin*> CreateWasmPlugin(PluginOwner owner, const PluginManifest& manifest, const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return Err("cannot read '" + path + "'");
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        Limits limits;
        const double memoryMB = manifest.budgetMemoryMB > 0 ? manifest.budgetMemoryMB : kWasmDefaultMemoryMB;
        limits.maxMemoryPages = std::max<uint32_t>(1, uint32_t(memoryMB * 1024 * 1024 / 65536));
        auto module = Module::Decode(bytes.data(), bytes.size(), limits);
        if (!module) return Err(module.GetError().message);

        auto s = std::make_unique<Sandbox>();
        s->ctx = ShardPluginCtx(owner);
        const double frameMs = manifest.budgetFrameMs > 0 ? manifest.budgetFrameMs : kWasmDefaultFrameMs;
        s->fuel = uint64_t(frameMs * double(kWasmFuelPerMs));
        s->frameLeft = s->fuel;

        auto instance = Instance::Create(module.Value(), Imports(*s), s->fuel);
        if (!instance) return Err(instance.GetError().message);
        s->instance = std::move(instance.Value());
        Instance& in = *s->instance;

        // The entry point fills a wasm32 ShardPluginInfo, like a native one fills the host's : the host adds a page to the sandbox's memory
        // and hands its address over. The module's allocator never saw that page, so nothing of the module is in it.
        const FuncType entryType{ { ValType::I32, ValType::I32 }, { ValType::I32 } };
        bool hasEntry = false;
        for (const Module::Export& e : module.Value()->Exports())
            if (e.kind == 0 && e.name == manifest.entry) hasEntry = e.type == entryType;
        if (!hasEntry) return Err("the module does not export '" + manifest.entry + "' as (i32 host, i32 info) -> i32");
        uint32_t page = 0;
        if (!in.GrowMemory(1, page)) return Err("the module has no memory (or no room left in it) for its ShardPluginInfo");
        const uint32_t infoAt = page * 65536;
        s->scratch = infoAt;                        // read once the entry returns ; the page then carries the arguments of later callbacks
        in.SetFuel(s->fuel);
        auto entered = in.Call(manifest.entry, { 0, infoAt });
        if (!entered) return Err("its entry point trapped : " + entered.GetError().message);
        const ShardResult result = ShardResult(int32_t(uint32_t(entered.Value()[0])));
        if (result != SHARD_OK) return Err("its entry point '" + manifest.entry + "' refused to load (" + std::to_string(result) + ")");

        // wasm32 ShardPluginInfo : structSize 0, abiMajor 4, abiMinor 8, id { data 16, size 24 }, user 32, callbacks 36 ; 40 bytes
        uint32_t structSize = 0, abiMajor = 0, abiMinor = 0, callbacksAt = 0;
        in.Read(infoAt, &structSize, 4);
        in.Read(infoAt + 4, &abiMajor, 4);
        in.Read(infoAt + 8, &abiMinor, 4);
        in.Read(infoAt + 32, &s->user, 4);
        in.Read(infoAt + 36, &callbacksAt, 4);
        if (structSize < 40) return Err("its ShardPluginInfo is too small (" + std::to_string(structSize) + " bytes, wasm32 needs 40)");
        uint32_t callbacksSize = 0;
        if (!in.Read(callbacksAt, &callbacksSize, 4) || callbacksSize < 4 + 4 * 5) return Err("its ShardPluginCallbacks is missing or too small");
        for (uint32_t i = 0; i < SlotCount && 4 + 4 * (i + 1) <= callbacksSize; ++i)
            if (!in.Read(callbacksAt + 4 + 4 * i, &s->callbacks[i], 4)) return Err("its ShardPluginCallbacks is out of its memory");

        // The native view CAbiPlugin works with : the same checks as a C plugin's
        ShardPluginCallbacks& cb = s->native;
        cb.structSize = sizeof(ShardPluginCallbacks);
        if (s->callbacks[OnLoad]) cb.on_load = Lifecycle<OnLoad>;
        if (s->callbacks[OnInitialize]) cb.on_initialize = Lifecycle<OnInitialize>;
        if (s->callbacks[OnEnable]) cb.on_enable = Lifecycle<OnEnable>;
        if (s->callbacks[OnDisable]) cb.on_disable = Lifecycle<OnDisable>;
        if (s->callbacks[OnUnload]) cb.on_unload = Lifecycle<OnUnload>;
        if (s->callbacks[OnFrameBegin]) cb.on_frame_begin = Frame<OnFrameBegin>;
        if (s->callbacks[OnFrameEnd]) cb.on_frame_end = Frame<OnFrameEnd>;
        // Hot reload goes through a new instance (shard.state is not imported yet) : no on_prepare_reload, so the plugin is not reloadable

        ShardPluginInfo info;
        std::memset(&info, 0, sizeof(info));
        info.structSize = sizeof(ShardPluginInfo);
        info.abiMajor = abiMajor;
        info.abiMinor = abiMinor;
        info.id = ShardStr{ manifest.id.data(), manifest.id.size() };
        info.user = s.get();
        info.callbacks = &cb;
        const std::string problem = ValidateCPluginInfo(info);
        if (!problem.empty()) return Err(problem);

        IPlugin* plugin = CreateCPlugin(owner, info);
        std::lock_guard<std::mutex> lock(gMutex);
        gSandboxes[owner] = std::move(s);           // a reload replaces the old sandbox, whose plugin object is gone by then
        return Ok(plugin);
    }

    void ReleaseWasmPlugin(PluginOwner owner)
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gSandboxes.erase(owner);
    }

    void BeginWasmFrame()
    {
        std::lock_guard<std::mutex> lock(gMutex);
        for (auto& [owner, sandbox] : gSandboxes) sandbox->frameLeft = sandbox->fuel;
    }
}
