#include <cstring>
#include <memory>

#include "engine/plugins/wasm/wasm_imports.hpp"
#include "engine/plugins/wasm/wasm_marshal.hpp"

// Minimal WASI for a wasi-sdk libc: console goes to the plugin log, empty args and env, no files, clock or randomness. Anything else imported is refused at load by name.

namespace Shard::Engine::Plugins::Wasm {

    using namespace Marshal;

    namespace {
        constexpr ValType I = ValType::I32, L = ValType::I64;
        constexpr const char* kModule = "wasi_snapshot_preview1";
        constexpr uint32_t kBadFd = 8, kNoSys = 52, kInvalid = 28;

        struct Console {
            std::string pending[3];
            void Write(Sandbox& s, uint32_t fd, const char* bytes, size_t size)
            {
                std::string& line = pending[fd];
                for (size_t i = 0; i < size; ++i) {
                    if (bytes[i] == '\n' || line.size() >= 4096) {
                        if (bytes[i] != '\n') line += bytes[i];
                        s.Log(fd == 2 ? SHARD_LOG_WARNING : SHARD_LOG_INFO, line);
                        line.clear();
                    } else {
                        line += bytes[i];
                    }
                }
            }
        };
    }

    void AddWasiImports(Sandbox& s, ImportList& imports, const Module& module)
    {
        bool any = false;
        for (const Module::Import& imp : module.Imports()) any = any || imp.module == kModule;
        if (!any) return;
        auto console = std::make_shared<Console>();
        auto add = [&](const char* name, FuncType type, HostFunction fn) { imports.AddOther(kModule, name, std::move(type), std::move(fn)); };

        add("proc_exit", { { I }, {} }, [](Instance& in, const uint64_t* a, uint64_t*) {
            in.HostTrap("the module called exit(" + std::to_string(int32_t(uint32_t(a[0]))) + ")");
            return false;
        });

        add("fd_write", { { I, I, I, I }, { I } }, [&s, console](Instance& in, const uint64_t* a, uint64_t* r) {
            const uint32_t fd = uint32_t(a[0]);
            if (fd != 1 && fd != 2) { r[0] = kBadFd; return true; }
            const uint32_t count = uint32_t(a[2]);
            if (count > 1024) { r[0] = kInvalid; return true; }
            const uint8_t* iovs = Span(in, a[1], uint64_t(count) * 8, "iovec");
            if (!iovs) return false;
            std::vector<uint8_t> list(iovs, iovs + size_t(count) * 8);
            uint64_t total = 0;
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t at = Load<uint32_t>(list.data(), i * 8), n = Load<uint32_t>(list.data(), i * 8 + 4);
                total += n;
                if (total > (1u << 20)) { r[0] = kInvalid; return true; }
                if (n == 0) continue;
                const uint8_t* bytes = Span(in, at, n, "output");
                if (!bytes) return false;
                console->Write(s, fd, reinterpret_cast<const char*>(bytes), n);
            }
            const uint32_t written = uint32_t(total);
            if (!in.Write(uint32_t(a[3]), &written, 4)) { in.HostTrap("nwritten out of the memory"); return false; }
            r[0] = 0;
            return true;
        });

        add("fd_fdstat_get", { { I, I }, { I } }, [](Instance& in, const uint64_t* a, uint64_t* r) {
            if (uint32_t(a[0]) > 2) { r[0] = kBadFd; return true; }
            uint8_t stat[24] = {};
            stat[0] = 2;
            if (!in.Write(uint32_t(a[1]), stat, sizeof(stat))) { in.HostTrap("fdstat out of the memory"); return false; }
            r[0] = 0;
            return true;
        });
        add("fd_close", { { I }, { I } }, [](Instance&, const uint64_t* a, uint64_t* r) { r[0] = uint32_t(a[0]) <= 2 ? 0 : kBadFd; return true; });
        add("fd_seek", { { I, L, I, I }, { I } }, [](Instance&, const uint64_t*, uint64_t* r) { r[0] = 29; return true; });
        add("fd_read", { { I, I, I, I }, { I } }, [](Instance&, const uint64_t*, uint64_t* r) { r[0] = kBadFd; return true; });
        add("fd_prestat_get", { { I, I }, { I } }, [](Instance&, const uint64_t*, uint64_t* r) { r[0] = kBadFd; return true; });
        add("fd_prestat_dir_name", { { I, I, I }, { I } }, [](Instance&, const uint64_t*, uint64_t* r) { r[0] = kBadFd; return true; });

        auto zeroTwo = [](Instance& in, const uint64_t* a, uint64_t* r) {
            const uint32_t zero = 0;
            if (!in.Write(uint32_t(a[0]), &zero, 4) || !in.Write(uint32_t(a[1]), &zero, 4)) { in.HostTrap("output out of the memory"); return false; }
            r[0] = 0;
            return true;
        };
        add("environ_sizes_get", { { I, I }, { I } }, zeroTwo);
        add("args_sizes_get", { { I, I }, { I } }, zeroTwo);
        add("environ_get", { { I, I }, { I } }, [](Instance&, const uint64_t*, uint64_t* r) { r[0] = 0; return true; });
        add("args_get", { { I, I }, { I } }, [](Instance&, const uint64_t*, uint64_t* r) { r[0] = 0; return true; });

        add("clock_time_get", { { I, L, I }, { I } }, [](Instance&, const uint64_t*, uint64_t* r) { r[0] = kNoSys; return true; });
        add("clock_res_get", { { I, I }, { I } }, [](Instance&, const uint64_t*, uint64_t* r) { r[0] = kNoSys; return true; });
        add("random_get", { { I, I }, { I } }, [](Instance&, const uint64_t*, uint64_t* r) { r[0] = kNoSys; return true; });
        add("sched_yield", { {}, { I } }, [](Instance&, const uint64_t*, uint64_t* r) { r[0] = 0; return true; });
    }
}
