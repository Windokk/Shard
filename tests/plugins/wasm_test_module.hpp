#pragma once

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "engine/plugins/wasm/wasm_runtime.hpp"

// Builds WebAssembly modules byte by byte for the tests of the sandbox : no compiler or assembler is needed to run them.

using namespace Shard::Engine::Plugins::Wasm;

namespace WasmTest {
    constexpr uint8_t I32 = 0x7F, I64 = 0x7E, F32 = 0x7D, F64 = 0x7C;

    struct Func { uint32_t type; std::vector<std::pair<uint32_t, uint8_t>> locals; std::vector<uint8_t> body; };

    struct Builder {
        std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> types;
        std::vector<std::tuple<std::string, std::string, uint32_t>> imports;
        std::vector<Func> funcs;
        int memoryMin = -1, memoryMax = -1;
        int tableSize = -1;
        std::vector<std::tuple<std::string, uint8_t, uint32_t>> exports;
        std::vector<std::pair<uint32_t, std::vector<uint32_t>>> elements;
        std::vector<std::pair<uint32_t, std::string>> data;
        int start = -1;

        uint32_t Type(std::vector<uint8_t> params, std::vector<uint8_t> results)
        {
            types.push_back({ std::move(params), std::move(results) });
            return uint32_t(types.size() - 1);
        }
        uint32_t Import(std::string module, std::string name, uint32_t type)
        {
            imports.emplace_back(std::move(module), std::move(name), type);
            return uint32_t(imports.size() - 1);
        }
        // Returns the function index (imports first)
        uint32_t Function(uint32_t type, std::vector<uint8_t> body, std::vector<std::pair<uint32_t, uint8_t>> locals = {}, const char* exportName = nullptr)
        {
            funcs.push_back({ type, std::move(locals), std::move(body) });
            const uint32_t index = uint32_t(imports.size() + funcs.size() - 1);
            if (exportName) exports.emplace_back(exportName, 0, index);
            return index;
        }

        static void Section(std::vector<uint8_t>& out, uint8_t id, const std::vector<uint8_t>& content)
        {
            out.push_back(id);
            WriteU32(out, uint32_t(content.size()));
            out.insert(out.end(), content.begin(), content.end());
        }
        static void Name(std::vector<uint8_t>& out, const std::string& s) { WriteU32(out, uint32_t(s.size())); out.insert(out.end(), s.begin(), s.end()); }

        std::vector<uint8_t> Build() const
        {
            std::vector<uint8_t> out = { 0, 'a', 's', 'm', 1, 0, 0, 0 };
            std::vector<uint8_t> s;
            if (!types.empty()) {
                s.clear(); WriteU32(s, uint32_t(types.size()));
                for (auto& [p, r] : types) {
                    s.push_back(0x60);
                    WriteU32(s, uint32_t(p.size())); s.insert(s.end(), p.begin(), p.end());
                    WriteU32(s, uint32_t(r.size())); s.insert(s.end(), r.begin(), r.end());
                }
                Section(out, 1, s);
            }
            if (!imports.empty()) {
                s.clear(); WriteU32(s, uint32_t(imports.size()));
                for (auto& [m, n, t] : imports) { Name(s, m); Name(s, n); s.push_back(0); WriteU32(s, t); }
                Section(out, 2, s);
            }
            if (!funcs.empty()) {
                s.clear(); WriteU32(s, uint32_t(funcs.size()));
                for (auto& f : funcs) WriteU32(s, f.type);
                Section(out, 3, s);
            }
            if (tableSize >= 0) {
                s.clear(); WriteU32(s, 1); s.push_back(0x70); s.push_back(1); WriteU32(s, uint32_t(tableSize)); WriteU32(s, uint32_t(tableSize));
                Section(out, 4, s);
            }
            if (memoryMin >= 0) {
                s.clear(); WriteU32(s, 1);
                if (memoryMax >= 0) { s.push_back(1); WriteU32(s, uint32_t(memoryMin)); WriteU32(s, uint32_t(memoryMax)); }
                else { s.push_back(0); WriteU32(s, uint32_t(memoryMin)); }
                Section(out, 5, s);
            }
            // Like wasm-ld : the memory and the table are exported, the host reaches them through these names
            auto all = exports;
            if (memoryMin >= 0) all.emplace_back(kMemoryExport, 2, 0);
            if (tableSize >= 0) all.emplace_back(kTableExport, 1, 0);
            if (!all.empty()) {
                s.clear(); WriteU32(s, uint32_t(all.size()));
                for (auto& [n, k, i] : all) { Name(s, n); s.push_back(k); WriteU32(s, i); }
                Section(out, 7, s);
            }
            if (start >= 0) { s.clear(); WriteU32(s, uint32_t(start)); Section(out, 8, s); }
            if (!elements.empty()) {
                s.clear(); WriteU32(s, uint32_t(elements.size()));
                for (auto& [offset, fs] : elements) {
                    WriteU32(s, 0); s.push_back(0x41); WriteS32(s, int32_t(offset)); s.push_back(0x0B);
                    WriteU32(s, uint32_t(fs.size())); for (uint32_t f : fs) WriteU32(s, f);
                }
                Section(out, 9, s);
            }
            if (!funcs.empty()) {
                s.clear(); WriteU32(s, uint32_t(funcs.size()));
                for (auto& f : funcs) {
                    std::vector<uint8_t> body;
                    WriteU32(body, uint32_t(f.locals.size()));
                    for (auto& [count, type] : f.locals) { WriteU32(body, count); body.push_back(type); }
                    body.insert(body.end(), f.body.begin(), f.body.end());
                    WriteU32(s, uint32_t(body.size()));
                    s.insert(s.end(), body.begin(), body.end());
                }
                Section(out, 10, s);
            }
            if (!data.empty()) {
                s.clear(); WriteU32(s, uint32_t(data.size()));
                for (auto& [offset, bytes] : data) {
                    WriteU32(s, 0); s.push_back(0x41); WriteS32(s, int32_t(offset)); s.push_back(0x0B);
                    Name(s, bytes);
                }
                Section(out, 11, s);
            }
            return out;
        }
    };

    // Instruction helpers
    inline std::vector<uint8_t> operator+(std::vector<uint8_t> a, const std::vector<uint8_t>& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
    inline std::vector<uint8_t> I32Const(int32_t v) { std::vector<uint8_t> o = { 0x41 }; WriteS32(o, v); return o; }
    inline std::vector<uint8_t> I64Const(int64_t v) { std::vector<uint8_t> o = { 0x42 }; WriteS64(o, v); return o; }
    inline std::vector<uint8_t> Get(uint32_t i) { std::vector<uint8_t> o = { 0x20 }; WriteU32(o, i); return o; }
    inline std::vector<uint8_t> Set(uint32_t i) { std::vector<uint8_t> o = { 0x21 }; WriteU32(o, i); return o; }
    inline std::vector<uint8_t> Call(uint32_t f) { std::vector<uint8_t> o = { 0x10 }; WriteU32(o, f); return o; }
    inline std::vector<uint8_t> Op(std::initializer_list<uint8_t> bytes) { return std::vector<uint8_t>(bytes); }

    inline std::unique_ptr<Instance> Make(const Builder& b, const std::vector<HostImport>& imports = {}, const Limits& limits = {})
    {
        const std::vector<uint8_t> bytes = b.Build();
        auto module = Module::Decode(bytes.data(), bytes.size(), limits);
        EXPECT_TRUE(module) << module.GetError().message;
        if (!module) return nullptr;
        auto instance = Instance::Create(module.Value(), imports);
        EXPECT_TRUE(instance) << instance.GetError().message;
        if (!instance) return nullptr;
        instance.Value()->SetFuel(10'000'000);
        return std::move(instance.Value());
    }

    inline uint64_t One(Instance& in, const std::string& name, std::vector<uint64_t> args)
    {
        auto r = in.Call(name, args);
        EXPECT_TRUE(r) << r.GetError().message;
        return r && !r.Value().empty() ? r.Value()[0] : ~0ull;
    }

    inline uint32_t Bits(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }
    inline float AsF32(uint64_t v) { uint32_t b = uint32_t(v); float f; std::memcpy(&f, &b, 4); return f; }
    inline double AsF64(uint64_t v) { double d; std::memcpy(&d, &v, 8); return d; }
}
