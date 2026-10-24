#include "engine/plugins/wasm/wasm_runtime.hpp"

#include <algorithm>
#include <cstring>

#include <wasmtime.h>

namespace Shard::Engine::Plugins::Wasm {

    using Core::Err;
    using Core::Ok;

    namespace {
        constexpr uint64_t kPageSize = 65536;

        // The configuration every sandbox runs with : deterministic (fuel, canonical NaNs, no threads, no relaxed SIMD), the proposals a
        // plugin built by Clang for wasm32 may use, nothing that would let a module share memory or reach the host
        wasm_engine_t* Engine()
        {
            static wasm_engine_t* engine = [] {
                wasm_config_t* config = wasm_config_new();
                wasmtime_config_consume_fuel_set(config, true);
                wasmtime_config_cranelift_nan_canonicalization_set(config, true);
                wasmtime_config_wasm_threads_set(config, false);
                wasmtime_config_wasm_relaxed_simd_set(config, false);
                wasmtime_config_wasm_memory64_set(config, false);
                wasmtime_config_wasm_multi_memory_set(config, false);
                wasmtime_config_wasm_gc_set(config, false);
                wasmtime_config_wasm_function_references_set(config, false);
                wasmtime_config_wasm_exceptions_set(config, false);
                wasmtime_config_wasm_stack_switching_set(config, false);
                wasmtime_config_wasm_component_model_set(config, false);
                wasmtime_config_max_wasm_stack_set(config, 512 * 1024);         // a module that recurses without end traps, the host does not overflow
                return wasm_engine_new_with_config(config);                    // takes the config
            }();
            return engine;
        }

        std::string ErrorMessage(wasmtime_error_t* error)
        {
            wasm_name_t message;
            wasmtime_error_message(error, &message);
            std::string text(message.data, message.size);
            wasm_byte_vec_delete(&message);
            wasmtime_error_delete(error);
            // "failed to parse WebAssembly module\n\nCaused by:\n    0: ...\n    1: unexpected end-of-file" : the lines, one after the other
            std::string joined;
            size_t at = 0;
            while (at <= text.size()) {
                size_t end = text.find('\n', at);
                if (end == std::string::npos) end = text.size();
                std::string line = text.substr(at, end - at);
                const size_t first = line.find_first_not_of(" \t\r");
                line = first == std::string::npos ? std::string() : line.substr(first);
                if (line.size() > 3 && line[0] >= '0' && line[0] <= '9' && line[1] == ':') line = line.substr(3);    // "0: "
                if (!line.empty() && line != "Caused by:") joined += (joined.empty() ? "" : " : ") + line;
                at = end + 1;
            }
            return joined.empty() ? text : joined;
        }

        std::string TrapMessage(wasm_trap_t* trap)
        {
            wasm_message_t message;
            wasm_trap_message(trap, &message);
            std::string text(message.data, message.size);
            wasm_byte_vec_delete(&message);
            while (!text.empty() && text.back() == '\0') text.pop_back();
            return text;
        }

        // The cause at the end of one of Wasmtime's messages (after the backtrace and "Caused by:")
        std::string LastLine(const std::string& text)
        {
            size_t end = text.find_last_not_of(" \r\n\t");
            if (end == std::string::npos) return text;
            size_t start = text.find_last_of('\n', end);
            start = start == std::string::npos ? 0 : start + 1;
            while (start < end && text[start] == ' ') ++start;
            return text.substr(start, end - start + 1);
        }

        TrapKind KindOf(const wasm_trap_t* trap)
        {
            wasmtime_trap_code_t code;
            if (!wasmtime_trap_code(trap, &code)) return TrapKind::Host;        // made by a host function
            switch (code) {
                case WASMTIME_TRAP_CODE_STACK_OVERFLOW: return TrapKind::CallStackExhausted;
                case WASMTIME_TRAP_CODE_MEMORY_OUT_OF_BOUNDS: return TrapKind::MemoryOutOfBounds;
                case WASMTIME_TRAP_CODE_TABLE_OUT_OF_BOUNDS: return TrapKind::TableOutOfBounds;
                case WASMTIME_TRAP_CODE_INDIRECT_CALL_TO_NULL: return TrapKind::IndirectCallNull;
                case WASMTIME_TRAP_CODE_BAD_SIGNATURE: return TrapKind::IndirectCallType;
                case WASMTIME_TRAP_CODE_INTEGER_OVERFLOW: return TrapKind::IntegerOverflow;
                case WASMTIME_TRAP_CODE_INTEGER_DIVISION_BY_ZERO: return TrapKind::DivideByZero;
                case WASMTIME_TRAP_CODE_BAD_CONVERSION_TO_INTEGER: return TrapKind::InvalidConversion;
                case WASMTIME_TRAP_CODE_UNREACHABLE_CODE_REACHED: return TrapKind::Unreachable;
                case WASMTIME_TRAP_CODE_OUT_OF_FUEL: return TrapKind::OutOfFuel;
                default: return TrapKind::Other;
            }
        }

        bool ToValType(wasm_valkind_t kind, ValType& out)
        {
            switch (kind) {
                case WASM_I32: out = ValType::I32; return true;
                case WASM_I64: out = ValType::I64; return true;
                case WASM_F32: out = ValType::F32; return true;
                case WASM_F64: out = ValType::F64; return true;
                case WASM_FUNCREF: out = ValType::FuncRef; return true;
                case WASM_EXTERNREF: out = ValType::ExternRef; return true;
                default: return false;
            }
        }

        bool ReadTypes(const wasm_valtype_vec_t* vec, std::vector<ValType>& out)
        {
            for (size_t i = 0; i < vec->size; ++i) {
                ValType t;
                if (!ToValType(wasm_valtype_kind(vec->data[i]), t)) return false;
                out.push_back(t);
            }
            return true;
        }

        bool ReadFuncType(const wasm_functype_t* type, FuncType& out)
        {
            out = {};
            return ReadTypes(wasm_functype_params(type), out.params) && ReadTypes(wasm_functype_results(type), out.results);
        }

        // Only numbers cross between the host and a sandbox
        wasm_functype_t* MakeFuncType(const FuncType& type)
        {
            auto vec = [](const std::vector<ValType>& types, wasm_valtype_vec_t& out) {
                wasm_valtype_vec_new_uninitialized(&out, types.size());
                for (size_t i = 0; i < types.size(); ++i) {
                    wasm_valkind_t kind = WASM_I32;
                    switch (types[i]) {
                        case ValType::I64: kind = WASM_I64; break;
                        case ValType::F32: kind = WASM_F32; break;
                        case ValType::F64: kind = WASM_F64; break;
                        default: break;
                    }
                    out.data[i] = wasm_valtype_new(kind);
                }
            };
            wasm_valtype_vec_t params, results;
            vec(type.params, params);
            vec(type.results, results);
            return wasm_functype_new(&params, &results);
        }

        wasmtime_val_t ToVal(ValType type, uint64_t bits)
        {
            wasmtime_val_t v;
            std::memset(&v, 0, sizeof(v));
            switch (type) {
                case ValType::I64: v.kind = WASMTIME_I64; v.of.i64 = int64_t(bits); break;
                case ValType::F32: { v.kind = WASMTIME_F32; const uint32_t b = uint32_t(bits); std::memcpy(&v.of.f32, &b, 4); break; }
                case ValType::F64: v.kind = WASMTIME_F64; std::memcpy(&v.of.f64, &bits, 8); break;
                default: v.kind = WASMTIME_I32; v.of.i32 = int32_t(uint32_t(bits)); break;
            }
            return v;
        }

        uint64_t FromVal(const wasmtime_val_t& v)
        {
            switch (v.kind) {
                case WASMTIME_I32: return uint32_t(v.of.i32);
                case WASMTIME_I64: return uint64_t(v.of.i64);
                case WASMTIME_F32: { uint32_t b; std::memcpy(&b, &v.of.f32, 4); return b; }
                case WASMTIME_F64: { uint64_t b; std::memcpy(&b, &v.of.f64, 8); return b; }
                default: return 0;
            }
        }
    }

    void WriteU32(std::vector<uint8_t>& out, uint32_t value)
    {
        do { uint8_t b = value & 0x7F; value >>= 7; if (value) b |= 0x80; out.push_back(b); } while (value);
    }
    void WriteS64(std::vector<uint8_t>& out, int64_t value)
    {
        bool more = true;
        while (more) {
            uint8_t b = value & 0x7F;
            value >>= 7;                        // arithmetic shift
            more = !((value == 0 && !(b & 0x40)) || (value == -1 && (b & 0x40)));
            if (more) b |= 0x80;
            out.push_back(b);
        }
    }
    void WriteS32(std::vector<uint8_t>& out, int32_t value) { WriteS64(out, value); }

    const char* TrapName(TrapKind kind)
    {
        switch (kind) {
            case TrapKind::None: return "none";
            case TrapKind::Unreachable: return "unreachable";
            case TrapKind::OutOfFuel: return "out of fuel";
            case TrapKind::MemoryOutOfBounds: return "memory access out of bounds";
            case TrapKind::DivideByZero: return "integer divide by zero";
            case TrapKind::IntegerOverflow: return "integer overflow";
            case TrapKind::InvalidConversion: return "invalid conversion to integer";
            case TrapKind::IndirectCallNull: return "indirect call to a null table entry";
            case TrapKind::IndirectCallType: return "indirect call type mismatch";
            case TrapKind::TableOutOfBounds: return "table index out of bounds";
            case TrapKind::CallStackExhausted: return "call stack exhausted";
            case TrapKind::Host: return "host function failed";
            case TrapKind::Other: return "trap";
        }
        return "?";
    }

    // ---- Modules ----

    Module::~Module()
    {
        if (m_Module) wasmtime_module_delete(m_Module);
    }

    Core::Result<bool> Module::Describe()
    {
        wasm_importtype_vec_t imports;
        wasmtime_module_imports(m_Module, &imports);
        std::string problem;
        for (size_t i = 0; i < imports.size && problem.empty(); ++i) {
            const wasm_name_t* mod = wasm_importtype_module(imports.data[i]);
            const wasm_name_t* name = wasm_importtype_name(imports.data[i]);
            Module::Import imp{ std::string(mod->data, mod->size), std::string(name->data, name->size), {} };
            const wasm_externtype_t* type = wasm_importtype_type(imports.data[i]);
            // The sandbox owns its memory, table and globals : it never gets the host's (nor another sandbox's)
            if (wasm_externtype_kind(type) != WASM_EXTERN_FUNC) problem = "only functions can be imported (" + imp.module + "." + imp.name + ")";
            else if (!ReadFuncType(wasm_externtype_as_functype_const(type), imp.type)) problem = "import " + imp.module + "." + imp.name + " takes references";
            else m_Imports.push_back(std::move(imp));
        }
        wasm_importtype_vec_delete(&imports);
        if (!problem.empty()) return Err("WebAssembly : " + problem);

        wasm_exporttype_vec_t exports;
        wasmtime_module_exports(m_Module, &exports);
        for (size_t i = 0; i < exports.size; ++i) {
            const wasm_name_t* name = wasm_exporttype_name(exports.data[i]);
            const wasm_externtype_t* type = wasm_exporttype_type(exports.data[i]);
            Module::Export e{ std::string(name->data, name->size), 0, {} };
            switch (wasm_externtype_kind(type)) {
                case WASM_EXTERN_FUNC: e.kind = 0; ReadFuncType(wasm_externtype_as_functype_const(type), e.type); break;
                case WASM_EXTERN_TABLE: e.kind = 1; break;
                case WASM_EXTERN_MEMORY: e.kind = 2; break;
                default: e.kind = 3; break;
            }
            m_Exports.push_back(std::move(e));
        }
        wasm_exporttype_vec_delete(&exports);
        return Ok(true);
    }

    Core::Result<std::shared_ptr<const Module>> Module::Decode(const uint8_t* data, size_t size, const Limits& limits)
    {
        if (size > limits.maxModuleSize) return Err("WebAssembly : module too large");
        std::shared_ptr<Module> module(new Module());
        module->m_Limits = limits;
        if (wasmtime_error_t* error = wasmtime_module_new(Engine(), data, size, &module->m_Module))
            return Err("WebAssembly : " + ErrorMessage(error));
        auto described = module->Describe();
        if (!described) return Err(described.GetError().message);
        return Ok(std::shared_ptr<const Module>(std::move(module)));
    }

    const char* Module::RuntimeVersion() { return WASMTIME_VERSION; }

    Core::Result<std::vector<uint8_t>> Module::Serialize() const
    {
        wasm_byte_vec_t bytes;
        if (wasmtime_error_t* error = wasmtime_module_serialize(m_Module, &bytes)) return Err("WebAssembly : " + ErrorMessage(error));
        std::vector<uint8_t> out(bytes.data, bytes.data + bytes.size);
        wasm_byte_vec_delete(&bytes);
        return Ok(std::move(out));
    }

    Core::Result<std::shared_ptr<const Module>> Module::Deserialize(const uint8_t* data, size_t size, const Limits& limits)
    {
        std::shared_ptr<Module> module(new Module());
        module->m_Limits = limits;
        if (wasmtime_error_t* error = wasmtime_module_deserialize(Engine(), data, size, &module->m_Module))
            return Err("WebAssembly : " + ErrorMessage(error));
        auto described = module->Describe();
        if (!described) return Err(described.GetError().message);
        return Ok(std::shared_ptr<const Module>(std::move(module)));
    }

    // ---- Instances ----

    struct Instance::State {
        wasmtime_store_t* store = nullptr;
        wasmtime_context_t* context = nullptr;
        wasmtime_instance_t instance{};
        bool hasMemory = false, hasTable = false;
        wasmtime_memory_t memory{};
        wasmtime_table_t table{};
        wasmtime_caller_t* caller = nullptr;            // while a host function runs : its memory is reachable before the instance exists (start)

        struct Host { Instance* instance; HostFunction function; FuncType type; };
        std::vector<std::unique_ptr<Host>> hosts;

        ~State() { if (store) wasmtime_store_delete(store); }

        /// The context to use right now : inside a host function, the caller's (the store is lent to it), otherwise the store's
        wasmtime_context_t* Ctx() const { return caller ? wasmtime_caller_context(caller) : context; }

        bool CurrentMemory(wasmtime_memory_t& out)
        {
            if (hasMemory) { out = memory; return true; }
            wasmtime_extern_t item;
            if (caller && wasmtime_caller_export_get(caller, kMemoryExport, std::strlen(kMemoryExport), &item) && item.kind == WASMTIME_EXTERN_MEMORY) {
                out = item.of.memory;
                return true;
            }
            return false;
        }
    };

    // What Wasmtime calls for an import : the arguments as raw bits to the HostFunction, its results back, a false turned into a trap
    struct HostCall {
        static wasm_trap_t* Trampoline(void* env, wasmtime_caller_t* caller, const wasmtime_val_t* args, size_t nargs, wasmtime_val_t* results, size_t nresults)
        {
            auto* host = static_cast<Instance::State::Host*>(env);
            Instance& in = *host->instance;
            if (nargs > 16 || nresults > 16) {
                static const char kTooMany[] = "a host function with more than 16 parameters or results";
                return wasmtime_trap_new(kTooMany, sizeof(kTooMany) - 1);
            }
            uint64_t a[16] = {}, r[16] = {};
            for (size_t i = 0; i < nargs; ++i) a[i] = FromVal(args[i]);

            wasmtime_caller_t* previous = in.m_State->caller;
            in.m_State->caller = caller;
            in.m_HostTrapMessage.clear();
            bool ok = false;
            try {
                ok = host->function(in, a, r);
            } catch (const std::exception& e) {
                in.m_HostTrapMessage = std::string("the host function threw : ") + e.what();
            } catch (...) {
                in.m_HostTrapMessage = "the host function threw";
            }
            in.m_State->caller = previous;
            if (!ok) {
                if (in.m_HostTrapMessage.empty()) in.m_HostTrapMessage = TrapName(TrapKind::Host);
                in.m_HostFailed = true;             // Wasmtime hands it back as an error of the call, not as a trap with a code
                return wasmtime_trap_new(in.m_HostTrapMessage.data(), in.m_HostTrapMessage.size());
            }
            for (size_t i = 0; i < nresults; ++i) results[i] = ToVal(host->type.results[i], r[i]);
            return nullptr;
        }
    };

    Instance::Instance() : m_State(std::make_unique<State>()) {}
    Instance::~Instance() = default;

    Core::Result<std::unique_ptr<Instance>> Instance::Create(std::shared_ptr<const Module> module, const std::vector<HostImport>& imports, uint64_t startFuel)
    {
        std::unique_ptr<Instance> in(new Instance());
        State& s = *in->m_State;
        const Limits& limits = module->m_Limits;
        s.store = wasmtime_store_new(Engine(), nullptr, nullptr);
        s.context = wasmtime_store_context(s.store);
        // One instance, one memory, one table per store : the limits are those of the module
        wasmtime_store_limiter(s.store, int64_t(limits.maxMemoryPages) * int64_t(kPageSize), int64_t(limits.maxTableSize), 1, 1, 1);

        wasmtime_linker_t* linker = wasmtime_linker_new(Engine());
        std::string problem;
        for (const Module::Import& imp : module->m_Imports) {
            auto it = std::find_if(imports.begin(), imports.end(), [&](const HostImport& h) { return h.module == imp.module && h.name == imp.name; });
            if (it == imports.end()) { problem = "unresolved import " + imp.module + "." + imp.name; break; }
            if (!(it->type == imp.type)) { problem = "import " + imp.module + "." + imp.name + " has another type than the host's"; break; }
            s.hosts.push_back(std::make_unique<State::Host>(State::Host{ in.get(), it->function, it->type }));
            wasm_functype_t* type = MakeFuncType(it->type);
            wasmtime_error_t* error = wasmtime_linker_define_func(linker, imp.module.data(), imp.module.size(), imp.name.data(), imp.name.size(), type,
                                                                  &HostCall::Trampoline, s.hosts.back().get(), nullptr);
            wasm_functype_delete(type);
            if (error) { problem = ErrorMessage(error); break; }
        }
        if (!problem.empty()) { wasmtime_linker_delete(linker); return Err("WebAssembly : " + problem); }

        in->m_Module = std::move(module);
        in->SetFuel(startFuel);
        in->m_Depth = 1;
        wasm_trap_t* trap = nullptr;
        wasmtime_error_t* error = wasmtime_linker_instantiate(linker, s.context, in->m_Module->m_Module, &s.instance, &trap);
        in->m_Depth = 0;
        wasmtime_linker_delete(linker);
        if (error) return Err("WebAssembly : " + ErrorMessage(error));
        if (trap) {
            const std::string message = TrapMessage(trap);
            wasm_trap_delete(trap);
            return Err("WebAssembly : start function : " + message);
        }

        wasmtime_extern_t item;
        if (wasmtime_instance_export_get(s.context, &s.instance, kMemoryExport, std::strlen(kMemoryExport), &item) && item.kind == WASMTIME_EXTERN_MEMORY) {
            s.memory = item.of.memory;
            s.hasMemory = true;
        }
        if (wasmtime_instance_export_get(s.context, &s.instance, kTableExport, std::strlen(kTableExport), &item) && item.kind == WASMTIME_EXTERN_TABLE) {
            s.table = item.of.table;
            s.hasTable = true;
        }
        return Ok(std::move(in));
    }

    void Instance::SetFuel(uint64_t fuel)
    {
        if (wasmtime_error_t* error = wasmtime_context_set_fuel(m_State->Ctx(), fuel)) wasmtime_error_delete(error);
    }

    uint64_t Instance::Fuel() const
    {
        uint64_t fuel = 0;
        if (wasmtime_error_t* error = wasmtime_context_get_fuel(m_State->Ctx(), &fuel)) { wasmtime_error_delete(error); return 0; }
        return fuel;
    }

    uint32_t Instance::MemorySize()
    {
        wasmtime_memory_t memory;
        if (!m_State->CurrentMemory(memory)) return 0;
        return uint32_t(std::min<size_t>(wasmtime_memory_data_size(m_State->Ctx(), &memory), 0xFFFFFFFFu));
    }

    uint8_t* Instance::MemoryAt(uint32_t offset, uint32_t size)
    {
        wasmtime_memory_t memory;
        if (!m_State->CurrentMemory(memory)) return nullptr;
        if (uint64_t(offset) + size > wasmtime_memory_data_size(m_State->Ctx(), &memory)) return nullptr;
        return wasmtime_memory_data(m_State->Ctx(), &memory) + offset;
    }
    bool Instance::Read(uint32_t offset, void* out, uint32_t size)
    {
        uint8_t* p = MemoryAt(offset, size);
        if (!p) return false;
        if (size) std::memcpy(out, p, size);
        return true;
    }
    bool Instance::Write(uint32_t offset, const void* data, uint32_t size)
    {
        uint8_t* p = MemoryAt(offset, size);
        if (!p) return false;
        if (size) std::memcpy(p, data, size);
        return true;
    }

    bool Instance::GrowMemory(uint32_t pages, uint32_t& oldPages)
    {
        wasmtime_memory_t memory;
        if (!m_State->CurrentMemory(memory)) return false;
        uint64_t previous = 0;
        if (wasmtime_error_t* error = wasmtime_memory_grow(m_State->Ctx(), &memory, pages, &previous)) { wasmtime_error_delete(error); return false; }
        oldPages = uint32_t(previous);
        return true;
    }

    bool Instance::ExportedGlobal(const std::string& name, uint64_t& value)
    {
        wasmtime_extern_t item;
        if (!wasmtime_instance_export_get(m_State->Ctx(), &m_State->instance, name.data(), name.size(), &item) || item.kind != WASMTIME_EXTERN_GLOBAL) return false;
        wasmtime_val_t v;
        wasmtime_global_get(m_State->Ctx(), &item.of.global, &v);
        if (v.kind == WASMTIME_I32) value = uint32_t(v.of.i32);
        else if (v.kind == WASMTIME_I64) value = uint64_t(v.of.i64);
        else return false;
        return true;
    }

    bool Instance::HasFunction(const std::string& name) const
    {
        for (const Module::Export& x : m_Module->m_Exports)
            if (x.kind == 0 && x.name == name) return true;
        return false;
    }

    Core::Result<std::vector<uint64_t>> Instance::Call(const std::string& exportName, const std::vector<uint64_t>& args)
    {
        const Module::Export* e = nullptr;
        for (const Module::Export& x : m_Module->m_Exports)
            if (x.kind == 0 && x.name == exportName) e = &x;
        wasmtime_extern_t item;
        if (!e || !wasmtime_instance_export_get(m_State->Ctx(), &m_State->instance, exportName.data(), exportName.size(), &item) ||
            item.kind != WASMTIME_EXTERN_FUNC)
            return Err("WebAssembly : no exported function \"" + exportName + "\"");
        return Run(&item.of.func, e->type, args);
    }

    Core::Result<std::vector<uint64_t>> Instance::CallIndirect(uint32_t tableIndex, const FuncType& expected, const std::vector<uint64_t>& args)
    {
        auto fail = [&](TrapKind kind) -> Core::Result<std::vector<uint64_t>> {
            m_Trap = { kind, TrapName(kind) };
            return Err(std::string("WebAssembly : ") + TrapName(kind));
        };
        if (!m_State->hasTable) return Err(std::string("WebAssembly : the module exports no table (") + kTableExport + ")");
        wasmtime_val_t slot;
        if (!wasmtime_table_get(m_State->Ctx(), &m_State->table, tableIndex, &slot)) return fail(TrapKind::TableOutOfBounds);
        if (slot.kind != WASMTIME_FUNCREF || slot.of.funcref.store_id == 0) return fail(TrapKind::IndirectCallNull);
        const wasmtime_func_t func = slot.of.funcref;
        wasm_functype_t* type = wasmtime_func_type(m_State->Ctx(), &func);
        FuncType actual;
        const bool read = ReadFuncType(type, actual);
        wasm_functype_delete(type);
        if (!read || !(actual == expected)) return fail(TrapKind::IndirectCallType);
        return Run(&func, expected, args);
    }

    Core::Result<std::vector<uint64_t>> Instance::Run(const void* function, const FuncType& type, const std::vector<uint64_t>& args)
    {
        if (args.size() != type.params.size()) return Err("WebAssembly : wrong argument count");
        if (m_Depth > 0 && !m_State->caller) return Err("WebAssembly : the instance is already running");
        m_Trap = {};
        m_HostFailed = false;
        std::vector<wasmtime_val_t> in(args.size()), out(type.results.size());
        for (size_t i = 0; i < args.size(); ++i) in[i] = ToVal(type.params[i], args[i]);
        ++m_Depth;
        wasm_trap_t* trap = nullptr;
        wasmtime_error_t* error = wasmtime_func_call(m_State->Ctx(), static_cast<const wasmtime_func_t*>(function), in.data(), in.size(),
                                                     out.data(), out.size(), &trap);
        --m_Depth;
        if (error || trap) {
            // Wasmtime's text starts with the wasm backtrace : the journal gets the cause, the backtrace stays out
            const TrapKind kind = m_HostFailed ? TrapKind::Host : trap ? KindOf(trap) : TrapKind::Other;
            const std::string text = error ? ErrorMessage(error) : TrapMessage(trap);
            if (trap) wasm_trap_delete(trap);
            m_Trap.kind = kind;
            if (kind == TrapKind::Host) m_Trap.message = std::string(TrapName(kind)) + " : " + m_HostTrapMessage;
            else if (kind != TrapKind::Other) m_Trap.message = TrapName(kind);
            else m_Trap.message = LastLine(text);
            return Err("WebAssembly : " + m_Trap.message);
        }
        std::vector<uint64_t> results(out.size());
        for (size_t i = 0; i < out.size(); ++i) results[i] = FromVal(out[i]);
        return Ok(std::move(results));
    }
}
