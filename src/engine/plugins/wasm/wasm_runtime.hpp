#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/containers/result.hpp"

// The WebAssembly runtime of sandboxed (tier 2) plugins : docs/design-notes/wasm-sandbox.fr.md. A narrow interface over Wasmtime (its C API,
// cmake/ShardWasmtime.cmake), the only file that includes it being wasm_runtime.cpp. What runs in it sees nothing but its linear memory and the
// host functions it is given : the module is validated and compiled to machine code by Wasmtime (Cranelift), every instruction costs fuel,
// NaNs are canonical (a run gives the same bits on every machine) and a trap stops the call, never the host.

struct wasmtime_module;

namespace Shard::Engine::Plugins::Wasm {

    enum class ValType : uint8_t { I32 = 0x7F, I64 = 0x7E, F32 = 0x7D, F64 = 0x7C, FuncRef = 0x70, ExternRef = 0x6F };

    struct FuncType {
        std::vector<ValType> params;
        std::vector<ValType> results;
        bool operator==(const FuncType& o) const { return params == o.params && results == o.results; }
    };

    /// Limits a module and its instances are held to : the module comes from someone else
    struct Limits {
        uint32_t maxModuleSize = 64u * 1024 * 1024;
        uint32_t maxMemoryPages = 1024;             ///< 64 MiB : memory.grow fails past it, a module that needs more at start is refused
        uint32_t maxTableSize = 100000;
    };

    /// The name a module exports its function table under (what wasm-ld gives it with --export-table) : the host calls callbacks through it
    inline constexpr const char* kTableExport = "__indirect_function_table";
    /// The name a module exports its memory under (wasm-ld's default) : host functions read and write the sandbox's memory through it
    inline constexpr const char* kMemoryExport = "memory";

    /// @brief A validated and compiled module. Immutable : any number of instances can be made from it.
    class Module {
    public:
        struct Import { std::string module, name; FuncType type; };
        struct Export { std::string name; uint8_t kind = 0; FuncType type; };    ///< kind : 0 function, 1 table, 2 memory, 3 global ; type : functions only

        ~Module();
        Module(const Module&) = delete;
        Module& operator=(const Module&) = delete;

        /// @brief Validates and compiles a binary module. Fails on anything malformed, on what the sandbox does not allow (imported memories,
        /// tables or globals, threads, 64-bit or shared memories, relaxed SIMD...) and past the limits.
        static Core::Result<std::shared_ptr<const Module>> Decode(const uint8_t* data, size_t size, const Limits& limits = {});

        /// @brief The compiled code of this module, to keep and give to Deserialize later (Wasmtime's .cwasm). It only suits the same version of
        /// Wasmtime, configured the same way, on the same kind of machine : a local cache, never something to distribute.
        Core::Result<std::vector<uint8_t>> Serialize() const;
        /// @brief A module from the output of Serialize, without compiling it again. The bytes are trusted as machine code : only ever pass
        /// what this host wrote itself. Fails (and the caller compiles the module from its source) when Wasmtime does not accept them.
        static Core::Result<std::shared_ptr<const Module>> Deserialize(const uint8_t* data, size_t size, const Limits& limits = {});

        /// The version of the Wasmtime this was built with (a cache key)
        static const char* RuntimeVersion();

        const std::vector<Import>& Imports() const { return m_Imports; }
        const std::vector<Export>& Exports() const { return m_Exports; }
        const Limits& GetLimits() const { return m_Limits; }

    private:
        friend class Instance;
        Module() = default;
        /// Reads the imports and exports of m_Module and applies the sandbox's rules to them
        Core::Result<bool> Describe();

        wasmtime_module* m_Module = nullptr;
        Limits m_Limits;
        std::vector<Import> m_Imports;
        std::vector<Export> m_Exports;
    };

    enum class TrapKind : uint8_t {
        None, Unreachable, OutOfFuel, MemoryOutOfBounds, DivideByZero, IntegerOverflow, InvalidConversion, IndirectCallNull,
        IndirectCallType, TableOutOfBounds, CallStackExhausted, Host, Other
    };
    const char* TrapName(TrapKind kind);

    class Instance;

    /// A host function : reads its arguments (raw bits : i32 in the low half, f32 as its bit pattern), writes its results. Returning false traps
    /// (the reason given to Instance::HostTrap).
    using HostFunction = std::function<bool(Instance& instance, const uint64_t* args, uint64_t* results)>;

    struct HostImport {
        std::string module, name;
        FuncType type;
        HostFunction function;
    };

    struct Trap {
        TrapKind kind = TrapKind::None;
        std::string message;
    };

    /// @brief One running copy of a module, in a store of its own : its memory, globals, table and fuel. Not thread safe : one call at a time.
    class Instance {
    public:
        ~Instance();
        Instance(const Instance&) = delete;
        Instance& operator=(const Instance&) = delete;

        /// @brief Links the imports (by module and name, with the exact type), lays out memory, table and segments, runs the start function
        /// with `startFuel`.
        static Core::Result<std::unique_ptr<Instance>> Create(std::shared_ptr<const Module> module, const std::vector<HostImport>& imports,
                                                             uint64_t startFuel = 10'000'000);

        /// @brief Calls an exported function. Fails with the trap (or a wrong name / argument count). Fuel left is in Fuel() after.
        /// May be called from inside a host function (the module imported it, it calls back into the module) : the call then runs on the same
        /// stack and with the fuel that is left. A trap in it comes back as an error, and the host function usually fails too.
        Core::Result<std::vector<uint64_t>> Call(const std::string& exportName, const std::vector<uint64_t>& args);
        /// @brief Calls the function in slot `tableIndex` of the exported table (kTableExport), checking it has the type `expected` : a
        /// callback the module handed over as a function pointer
        Core::Result<std::vector<uint64_t>> CallIndirect(uint32_t tableIndex, const FuncType& expected, const std::vector<uint64_t>& args);

        void SetFuel(uint64_t fuel);
        uint64_t Fuel() const;

        // ---- Memory, for host functions : every access is checked ----

        uint32_t MemorySize();
        /// The `size` bytes at `offset`, or null when they are not all inside the memory (or the module exports none). Valid until the
        /// module runs again (a memory.grow may move it).
        uint8_t* MemoryAt(uint32_t offset, uint32_t size);
        bool Read(uint32_t offset, void* out, uint32_t size);
        bool Write(uint32_t offset, const void* data, uint32_t size);
        /// memory.grow from the host : false when the module has no memory or the limit would be passed. `oldPages` : the first new page
        bool GrowMemory(uint32_t pages, uint32_t& oldPages);

        /// The value of an exported global of type i32 or i64 (wasm-ld exports the address of a data symbol that way : --export=name)
        bool ExportedGlobal(const std::string& name, uint64_t& value);
        /// True if the module exports a function of that name
        bool HasFunction(const std::string& name) const;

        /// For a host function about to return false
        void HostTrap(std::string message) { m_HostTrapMessage = std::move(message); }

        const Module& GetModule() const { return *m_Module; }
        /// The trap of the last call that failed
        const Trap& LastTrap() const { return m_Trap; }

        struct State;               ///< the Wasmtime store, instance, memory and table (wasm_runtime.cpp)

    private:
        friend struct HostCall;
        Instance();

        Core::Result<std::vector<uint64_t>> Run(const void* func, const FuncType& type, const std::vector<uint64_t>& args);

        std::shared_ptr<const Module> m_Module;
        std::unique_ptr<State> m_State;
        Trap m_Trap;
        std::string m_HostTrapMessage;
        int m_Depth = 0;                        ///< calls into the module that have not returned (more than 1 : a host function called back)
        bool m_HostFailed = false;              ///< a host function of the current call returned false
    };

    // ---- Encoding helpers : tests and tools build modules byte by byte ----

    void WriteU32(std::vector<uint8_t>& out, uint32_t value);           ///< unsigned LEB128
    void WriteS32(std::vector<uint8_t>& out, int32_t value);            ///< signed LEB128
    void WriteS64(std::vector<uint8_t>& out, int64_t value);
}
