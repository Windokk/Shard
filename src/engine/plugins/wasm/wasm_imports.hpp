#pragma once

#include <deque>
#include <string>
#include <vector>

#include "engine/plugins/wasm/wasm_sandbox.hpp"

// The imports of a sandboxed plugin (module "shard") : one per function of the interfaces the sandbox offers, named "<interface>.<function>"
// like the module's side of the generated header (sdk/include/shard/shard_wasm_imports.h). Most are generated (wasm_imports_gen.cpp) ; the
// ones that take callbacks, descriptors with pointers or hand out pointers into host memory are written by hand in wasm_imports_*.cpp.

namespace Shard::Engine::Plugins::Wasm {

    struct ImportSignature {
        const char* name;
        FuncType type;
        bool manual;                ///< written by hand (the generator only knows its signature)
    };

    /// Every import of the module "shard", generated and by hand
    const std::vector<ImportSignature>& AllImportSignatures();
    /// The wasm32 signature of one of them (for the hand written ones)
    const FuncType& SignatureOf(const char* name);

    class ImportList {
    public:
        explicit ImportList(Sandbox& sandbox) : m_Sandbox(sandbox) {}

        /// An import of the module "shard". Nothing runs for a plugin that was stopped : an import called after that traps.
        void Add(const char* name, FuncType type, HostFunction function);
        /// ... with the signature the generator derived from the headers
        void AddManual(const char* name, HostFunction function) { Add(name, SignatureOf(name), std::move(function)); }
        /// An import of another module (WASI)
        void AddOther(const char* module, const char* name, FuncType type, HostFunction function);

        std::vector<HostImport> Take() { return std::move(m_Imports); }

    private:
        Sandbox& m_Sandbox;
        std::vector<HostImport> m_Imports;
    };

    /// The tables of the C ABI of the host, by interface (null for one this host does not have)
    HostApis QueryHostApis();

    void AddGeneratedImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);          // wasm_imports_gen.cpp
    void AddCoreImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);               // wasm_imports_core.cpp : the host, commands, content, state
    void AddEcsImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);                // wasm_imports_ecs.cpp : components, ecs, systems, events
    void AddScriptImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);             // wasm_imports_script.cpp : scripts
    void AddEditorImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);             // wasm_imports_editor.cpp : shard.editor
    void AddWasiImports(Sandbox& sandbox, ImportList& imports, const Module& module);               // wasm_wasi.cpp : the few WASI functions a libc needs

    /// The `count` wasm32 ShardFieldDesc at `at` in the module's memory, as host ones. The strings they point to are kept in `strings`, which has to
    /// outlive the call into the host that reads them.
    bool ReadFieldDescs(Instance& in, uint64_t at, uint32_t count, std::vector<ShardFieldDesc>& out, std::deque<std::string>& strings);

    /// Forgets what the module registered with the host's previous context (commands, systems, handlers, script types)
    void ResetRegistrations(Sandbox& sandbox);
}
