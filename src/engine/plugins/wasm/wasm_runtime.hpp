#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/containers/result.hpp"

struct wasmtime_module;

namespace Shard::Engine::Plugins::Wasm {

    enum class ValType : uint8_t { I32 = 0x7F, I64 = 0x7E, F32 = 0x7D, F64 = 0x7C, FuncRef = 0x70, ExternRef = 0x6F };

    struct FuncType {
        std::vector<ValType> params;
        std::vector<ValType> results;
        bool operator==(const FuncType& o) const { return params == o.params && results == o.results; }
    };

    struct Limits {
        uint32_t maxModuleSize = 64u * 1024 * 1024;
        uint32_t maxMemoryPages = 1024;
        uint32_t maxTableSize = 100000;
    };

    inline constexpr const char* kTableExport = "__indirect_function_table";
    inline constexpr const char* kMemoryExport = "memory";

    class Module {
    public:
        struct Import { std::string module, name; FuncType type; };
        struct Export { std::string name; uint8_t kind = 0; FuncType type; };

        ~Module();
        Module(const Module&) = delete;
        Module& operator=(const Module&) = delete;

        static Core::Result<std::shared_ptr<const Module>> Decode(const uint8_t* data, size_t size, const Limits& limits = {});

        // Wasmtime .cwasm: only valid for the same Wasmtime version, config and machine type. A local cache, never to distribute.
        Core::Result<std::vector<uint8_t>> Serialize() const;
        // Bytes are trusted as machine code, only pass what this host wrote. Fails if Wasmtime rejects them, then the caller compiles from source.
        static Core::Result<std::shared_ptr<const Module>> Deserialize(const uint8_t* data, size_t size, const Limits& limits = {});

        static const char* RuntimeVersion();

        const std::vector<Import>& Imports() const { return m_Imports; }
        const std::vector<Export>& Exports() const { return m_Exports; }
        const Limits& GetLimits() const { return m_Limits; }

    private:
        friend class Instance;
        Module() = default;
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

    class Instance {
    public:
        ~Instance();
        Instance(const Instance&) = delete;
        Instance& operator=(const Instance&) = delete;

        static Core::Result<std::unique_ptr<Instance>> Create(std::shared_ptr<const Module> module, const std::vector<HostImport>& imports,
                                                             uint64_t startFuel = 10'000'000);

        Core::Result<std::vector<uint64_t>> Call(const std::string& exportName, const std::vector<uint64_t>& args);
        Core::Result<std::vector<uint64_t>> CallIndirect(uint32_t tableIndex, const FuncType& expected, const std::vector<uint64_t>& args);

        void SetFuel(uint64_t fuel);
        uint64_t Fuel() const;

        uint32_t MemorySize();
        // Valid until the module runs again, memory.grow may move it
        uint8_t* MemoryAt(uint32_t offset, uint32_t size);
        bool Read(uint32_t offset, void* out, uint32_t size);
        bool Write(uint32_t offset, const void* data, uint32_t size);
        bool GrowMemory(uint32_t pages, uint32_t& oldPages);

        bool ExportedGlobal(const std::string& name, uint64_t& value);
        bool HasFunction(const std::string& name) const;

        void HostTrap(std::string message) { m_HostTrapMessage = std::move(message); }

        const Module& GetModule() const { return *m_Module; }
        const Trap& LastTrap() const { return m_Trap; }

        struct State;

    private:
        friend struct HostCall;
        Instance();

        Core::Result<std::vector<uint64_t>> Run(const void* func, const FuncType& type, const std::vector<uint64_t>& args);

        std::shared_ptr<const Module> m_Module;
        std::unique_ptr<State> m_State;
        Trap m_Trap;
        std::string m_HostTrapMessage;
        int m_Depth = 0;
        bool m_HostFailed = false;
    };

    void WriteU32(std::vector<uint8_t>& out, uint32_t value);
    void WriteS32(std::vector<uint8_t>& out, int32_t value);
    void WriteS64(std::vector<uint8_t>& out, int64_t value);
}
