#include <cstring>

#include <shard/shard_commands.h>
#include <shard/shard_content.h>
#include <shard/shard_host.h>

#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/plugins/wasm/wasm_imports.hpp"
#include "engine/plugins/wasm/wasm_marshal.hpp"

// The `ctx` a module passes is ignored, an import always acts for the sandbox's plugin

namespace Shard::Engine::Plugins::Wasm {

    using namespace Marshal;

    namespace {
        constexpr ValType I = ValType::I32, L = ValType::I64;
    }

    const FuncType& SignatureOf(const char* name)
    {
        for (const ImportSignature& s : AllImportSignatures())
            if (std::strcmp(s.name, name) == 0) return s.type;
        static const FuncType none;
        return none;
    }

    void ImportList::Add(const char* name, FuncType type, HostFunction function)
    {
        Sandbox* s = &m_Sandbox;
        m_Imports.push_back(HostImport{ "shard", name, std::move(type), [s, fn = std::move(function)](Instance& in, const uint64_t* a, uint64_t* r) {
            if (s->dead) { in.HostTrap("the plugin was stopped"); return false; }
            const bool ok = fn(in, a, r);
            if (ok && s->dead) { in.HostTrap("the plugin was stopped"); return false; }
            return ok;
        } });
    }

    void ImportList::AddOther(const char* module, const char* name, FuncType type, HostFunction function)
    {
        m_Imports.push_back(HostImport{ module, name, std::move(type), std::move(function) });
    }

    HostApis QueryHostApis()
    {
        return LoadHostApis([](const char* id, uint32_t minor) -> const void* {
            const void* table = nullptr;
            GetCHostApi()->query_interface(ShardStr{ id, std::strlen(id) }, 1, minor, &table);
            return table;
        });
    }

    void ResetRegistrations(Sandbox& sandbox)
    {
        std::lock_guard<std::recursive_mutex> lock(sandbox.mutex);
        sandbox.registrations.clear();
    }

    namespace {
        struct WasmCommand final : Registration {
            Sandbox* sandbox = nullptr;
            uint32_t run = 0;
            uint32_t user = 0;
            uint32_t outAt = 0;
            std::string output;
        };

        // Args are copied into module memory as wasm32 ShardStr. Callback signature is (i32 user, i64 ctx, i32 args, i32 argCount, i32 output) -> i32, then the output text is copied back out.
        ShardResult SHARD_CALL RunCommand(void* user, ShardPluginCtx, const ShardStr* args, uint32_t argCount, ShardStr* output)
        {
            auto* c = static_cast<WasmCommand*>(user);
            Sandbox& s = *c->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            if (s.dead) return SHARD_E_INTERNAL;
            uint64_t total = 16ull * argCount;
            for (uint32_t i = 0; i < argCount; ++i) total += args[i].size;
            if (total > (16ull << 20)) return SHARD_E_INVALID_ARG;
            Instance& in = *s.instance;
            const uint32_t argsAt = s.heap.Temp(total + 16, 16);
            if (!argsAt) return SHARD_E_OUT_OF_MEMORY;
            uint64_t textAt = uint64_t(argsAt) + 16ull * argCount;
            for (uint32_t i = 0; i < argCount; ++i) {
                uint8_t entry[16] = {};
                const uint32_t data = args[i].size ? uint32_t(textAt) : 0;
                std::memcpy(entry, &data, 4);
                std::memcpy(entry + 8, &args[i].size, 8);
                in.Write(argsAt + 16 * i, entry, 16);
                if (args[i].size) in.Write(uint32_t(textAt), args[i].data, uint32_t(args[i].size));
                textAt += args[i].size;
            }
            const uint8_t zero[16] = {};
            in.Write(c->outAt, zero, 16);
            static const FuncType type{ { I, L, I, I, I }, { I } };
            std::vector<uint64_t> out;
            if (!s.CallTable(c->run, type, { c->user, s.ctx, argsAt, argCount, c->outAt }, Budget::Call, out) || out.empty()) return SHARD_E_INTERNAL;

            uint8_t slot[16] = {};
            in.Read(c->outAt, slot, 16);
            const uint32_t data = Load<uint32_t>(slot, 0);
            const uint64_t size = Load<uint64_t>(slot, 8);
            const uint8_t* text = size <= 65536 ? in.MemoryAt(data, uint32_t(size)) : nullptr;
            c->output.assign(text ? reinterpret_cast<const char*>(text) : "", text ? size_t(size) : 0);
            *output = ShardStr{ c->output.data(), c->output.size() };
            return ShardResult(int32_t(uint32_t(out[0])));
        }

        struct ListCall {
            Sandbox* sandbox;
            uint32_t fn;
            uint32_t user;
        };
        ShardResult SHARD_CALL ListTrampoline(void* user, ShardStr path)
        {
            auto* call = static_cast<ListCall*>(user);
            Sandbox& s = *call->sandbox;
            const uint32_t at = s.heap.Temp(16 + path.size, 16);
            if (!at) return SHARD_E_OUT_OF_MEMORY;
            uint8_t entry[16] = {};
            const uint32_t data = path.size ? at + 16 : 0;
            std::memcpy(entry, &data, 4);
            std::memcpy(entry + 8, &path.size, 8);
            s.instance->Write(at, entry, 16);
            if (path.size) s.instance->Write(at + 16, path.data, uint32_t(path.size));
            static const FuncType type{ { I, I }, { I } };
            return s.CallResult(call->fn, type, { call->user, at }, Budget::Call);
        }

        bool IsOwnUri(const Sandbox& s, const std::string& uri)
        {
            const std::string prefix = "mod://" + s.pluginId + "/";
            return uri.compare(0, prefix.size(), prefix) == 0 && uri.find("..") == std::string::npos && uri.find('\\') == std::string::npos;
        }
    }

    void AddCoreImports(Sandbox& s, ImportList& imports, const HostApis& apis)
    {
        imports.AddManual("log", [&s](Instance& in, const uint64_t* a, uint64_t*) {
            std::string message;
            if (!ReadStr(in, a[2], message, 64 * 1024)) return false;
            s.Log(std::min<uint32_t>(uint32_t(a[1]), SHARD_LOG_ERROR), message);
            return true;
        });

        imports.AddManual("alloc", [&s](Instance&, const uint64_t* a, uint64_t* r) {
            r[0] = (a[1] == 0 || a[1] > 0xFFFFFFFFull) ? 0 : s.heap.Alloc(uint32_t(a[1]), uint32_t(std::min<uint64_t>(a[2], 32)));
            return true;
        });
        imports.AddManual("free", [&s](Instance& in, const uint64_t* a, uint64_t*) {
            if (a[1] != 0 && !s.heap.Free(uint32_t(a[1]))) { in.HostTrap("free of memory the host did not give"); return false; }
            return true;
        });

        imports.AddManual("query_interface", [&s](Instance& in, const uint64_t* a, uint64_t* r) {
            std::string id;
            if (!ReadStr(in, a[0], id, 256)) return false;
            r[0] = uint32_t(SHARD_E_NOT_FOUND);
            for (const OfferedInterface& offered : kOfferedInterfaces) {
                if (id != offered.id) continue;
                if (!s.Allowed(offered.permission)) return true;
                const void* table = nullptr;
                r[0] = uint32_t(GetCHostApi()->query_interface(ShardStr{ id.data(), id.size() }, uint32_t(a[1]), uint32_t(a[2]), &table));
                return true;
            }
            return true;
        });

        if (const ShardCommandsApi* commands = apis.commands) {
            imports.AddManual("commands.add", [&s, commands](Instance& in, const uint64_t* a, uint64_t* r) {
                std::string name, help;
                if (!ReadStr(in, a[1], name) || !ReadStr(in, a[2], help)) return false;
                auto* command = s.Register<WasmCommand>();
                command->sandbox = &s;
                command->run = uint32_t(a[3]);
                command->user = uint32_t(a[4]);
                command->outAt = s.heap.Alloc(16, 8);
                if (!command->outAt) { r[0] = uint32_t(SHARD_E_OUT_OF_MEMORY); return true; }
                r[0] = uint32_t(commands->add(s.ctx, ToStr(name), ToStr(help), &RunCommand, command));
                return true;
            });
        } else {
            imports.AddManual("commands.add", [](Instance&, const uint64_t*, uint64_t* r) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; });
        }

        const ShardContentApi* content = apis.content;
        imports.AddManual("content.read", [&s, content](Instance& in, const uint64_t* a, uint64_t* r) {
            if (!content || !s.Allowed(Permission::FsReadContent)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
            std::string uri;
            if (!ReadStr(in, a[1], uri, 4096)) return false;
            if (!Span(in, a[2], 4, "data") || !Span(in, a[3], 8, "size")) return false;
            if (!IsOwnUri(s, uri)) { r[0] = uint32_t(SHARD_E_INVALID_ARG); return true; }
            void* data = nullptr;
            uint64_t size = 0;
            ShardResult result = content->read(s.ctx, ShardStr{ uri.data(), uri.size() }, &data, &size);
            if (result != SHARD_OK) { r[0] = uint32_t(result); return true; }
            uint32_t at = 0;
            if (size > 0xFFFFFFF0ull || !(at = s.heap.Alloc(uint32_t(std::max<uint64_t>(size, 1)), 16))) result = SHARD_E_OUT_OF_MEMORY;
            else {
                if (size) in.Write(at, data, uint32_t(size));
                in.Write(uint32_t(a[2]), &at, 4);
                in.Write(uint32_t(a[3]), &size, 8);
            }
            GetCHostApi()->free(s.ctx, data);
            r[0] = uint32_t(result);
            return true;
        });
        imports.AddManual("content.list", [&s, content](Instance& in, const uint64_t* a, uint64_t* r) {
            if (!content || !s.Allowed(Permission::FsReadContent)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
            std::string pluginId, subdir;
            if (!ReadStr(in, a[1], pluginId, 256) || !ReadStr(in, a[2], subdir, 1024)) return false;
            if ((!pluginId.empty() && pluginId != s.pluginId) || subdir.find("..") != std::string::npos) { r[0] = uint32_t(SHARD_E_INVALID_ARG); return true; }
            ListCall call{ &s, uint32_t(a[3]), uint32_t(a[4]) };
            r[0] = uint32_t(content->list(s.ctx, ShardStr{ pluginId.data(), pluginId.size() }, ShardStr{ subdir.data(), subdir.size() }, &ListTrampoline, &call));
            return true;
        });
    }
}
