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
        bool manual;
    };

    /// Every import of the module "shard", generated and by hand
    const std::vector<ImportSignature>& AllImportSignatures();
    const FuncType& SignatureOf(const char* name);

    class ImportList {
    public:
        explicit ImportList(Sandbox& sandbox) : m_Sandbox(sandbox) {}

        // Imports of module "shard". A stopped plugin runs nothing, an import called after that traps.
        void Add(const char* name, FuncType type, HostFunction function);
        void AddManual(const char* name, HostFunction function) { Add(name, SignatureOf(name), std::move(function)); }
        void AddOther(const char* module, const char* name, FuncType type, HostFunction function);

        std::vector<HostImport> Take() { return std::move(m_Imports); }

    private:
        Sandbox& m_Sandbox;
        std::vector<HostImport> m_Imports;
    };

    HostApis QueryHostApis();

    void AddGeneratedImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);
    void AddCoreImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);
    void AddEcsImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);
    void AddScriptImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);
    void AddEditorImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);
    void AddTimersImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);
    void AddTimeImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);
    void AddDebugImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);
    void AddAssetsImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);
    void AddJobsImports(Sandbox& sandbox, ImportList& imports, const HostApis& apis);
    void AddWasiImports(Sandbox& sandbox, ImportList& imports, const Module& module);

    /// Runs the jobs the module queued and did not wait for, in order, on the main thread, inside the frame's fuel.
    void RunPendingJobs(Sandbox& sandbox);

    bool ReadFieldDescs(Instance& in, uint64_t at, uint32_t count, std::vector<ShardFieldDesc>& out, std::deque<std::string>& strings);

    void ResetRegistrations(Sandbox& sandbox);
}
