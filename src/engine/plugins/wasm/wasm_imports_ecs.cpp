#include <algorithm>
#include <cstring>
#include <deque>

#include <shard/shard_components.h>
#include <shard/shard_ecs.h>
#include <shard/shard_events.h>
#include <shard/shard_systems.h>

#include "engine/plugins/wasm/wasm_imports.hpp"
#include "engine/plugins/wasm/wasm_marshal.hpp"

// The imports whose arguments are descriptors with pointers, callbacks, or pointers into host memory : the components (registering a type,
// blobs, text), the entities a system sees, the systems themselves and the events.
//
// A system of the sandbox is a native system for the scheduler : its function is a trampoline that copies the components it reads into the
// module's memory, runs the module's function through the table, and copies back what the system may write (`writes` of its descriptor).
// Systems of a sandbox are forced onto the main thread, so that its instance is never entered by two threads of the scheduler at once.

namespace Shard::Engine::Plugins::Wasm {

    using namespace Marshal;

    namespace {
        constexpr ValType I = ValType::I32, L = ValType::I64;
        constexpr uint32_t kMaxListed = 256;

        // (see ReadFieldDescs below)
        bool ReadFieldDescsImpl(Instance& in, uint64_t at, uint32_t count, std::vector<ShardFieldDesc>& out, std::deque<std::string>& strings)
        {
            namespace Lf = Layout::ShardFieldDesc;
            out.clear();
            if (count == 0) return true;
            if (count > SHARD_COMPONENT_MAX_FIELDS) { in.HostTrap("more fields than a component can have"); return false; }
            const uint8_t* base = Span(in, at, uint64_t(Lf::kSize) * count, "fields");
            if (!base) return false;
            std::vector<uint8_t> copy(base, base + size_t(Lf::kSize) * count);       // the strings below may grow the memory
            for (uint32_t i = 0; i < count; ++i) {
                const uint8_t* f = copy.data() + size_t(Lf::kSize) * i;
                ShardFieldDesc d;
                std::memset(&d, 0, sizeof(d));
                d.structSize = sizeof(ShardFieldDesc);
                d.type = Load<uint32_t>(f, Lf::o_type);
                d.offset = Load<uint32_t>(f, Lf::o_offset);
                d.flags = Load<uint32_t>(f, Lf::o_flags);
                d.min = Load<float>(f, Lf::o_min);
                d.max = Load<float>(f, Lf::o_max);
                strings.emplace_back();
                if (!ReadStr(in, at + uint64_t(Lf::kSize) * i + Lf::o_name, strings.back(), 256)) return false;
                d.name = ToStr(strings.back());
                strings.emplace_back();
                if (!ReadStr(in, at + uint64_t(Lf::kSize) * i + Lf::o_tooltip, strings.back(), 4096)) return false;
                d.tooltip = ToStr(strings.back());
                out.push_back(d);
            }
            return true;
        }

        // A ShardFieldDesc the host filled (strings in host memory) as a wasm32 one in the module's memory, the strings copied in with it
        bool WriteFieldDesc(Sandbox& s, Instance& in, uint64_t at, const ShardFieldDesc& d)
        {
            namespace Lf = Layout::ShardFieldDesc;
            uint8_t out[Lf::kSize] = {};
            uint32_t structSize = 0;
            in.Read(uint32_t(at), &structSize, 4);
            Store<uint32_t>(out, Lf::o_structSize, structSize ? structSize : Lf::kSize);
            Store<uint32_t>(out, Lf::o_type, d.type);
            Store<uint32_t>(out, Lf::o_offset, d.offset);
            Store<uint32_t>(out, Lf::o_flags, d.flags);
            Store<float>(out, Lf::o_min, d.min);
            Store<float>(out, Lf::o_max, d.max);
            auto text = [&](ShardStr str, uint32_t field) {
                if (!str.data || !str.size) return true;
                const uint32_t copy = s.heap.Intern(std::string(str.data, size_t(str.size)));
                if (!copy) return false;
                Store<uint32_t>(out, field, copy);
                Store<uint64_t>(out, field + 8, str.size);
                return true;
            };
            if (!text(d.name, Lf::o_name) || !text(d.tooltip, Lf::o_tooltip)) { in.HostTrap("no room left in the memory for a field's strings"); return false; }
            return in.Write(uint32_t(at), out, Lf::kSize) || (in.HostTrap("output out of the memory"), false);
        }

        // A query with its arrays of component types, read from the module
        struct QueryData {
            std::vector<ShardComponentType> with, without;
            ShardQuery native{};
        };
        bool ReadQuery(Instance& in, uint64_t at, QueryData& q)
        {
            namespace Lq = Layout::ShardQuery;
            const uint8_t* d = Span(in, at, Lq::kSize, "query");
            if (!d) return false;
            uint8_t copy[Lq::kSize];
            std::memcpy(copy, d, Lq::kSize);
            const uint32_t withCount = Load<uint32_t>(copy, Lq::o_withCount), withoutCount = Load<uint32_t>(copy, Lq::o_withoutCount);
            if (withCount > kMaxListed || withoutCount > kMaxListed) { in.HostTrap("a query with more than 256 components"); return false; }
            auto list = [&](uint32_t pointer, uint32_t count, std::vector<ShardComponentType>& out) {
                out.resize(count);
                if (count == 0) return true;
                const uint8_t* p = Span(in, pointer, uint64_t(count) * 8, "query components");
                if (!p) return false;
                std::memcpy(out.data(), p, size_t(count) * 8);
                return true;
            };
            if (!list(Load<uint32_t>(copy, Lq::o_with), withCount, q.with) || !list(Load<uint32_t>(copy, Lq::o_without), withoutCount, q.without)) return false;
            q.native.structSize = sizeof(ShardQuery);
            q.native.withCount = withCount;
            q.native.withoutCount = withoutCount;
            q.native.with = q.with.empty() ? nullptr : q.with.data();
            q.native.without = q.without.empty() ? nullptr : q.without.data();
            return true;
        }

        bool MayWrite(const Sandbox& s, ShardComponentType type)
        {
            if (!s.run) return true;
            return s.run->exclusive || (s.run->writes && std::find(s.run->writes->begin(), s.run->writes->end(), type) != s.run->writes->end());
        }

        // ---- ecs.each_chunk : the columns go to the module a piece at a time ----

        struct ChunkCall {
            Sandbox* sandbox = nullptr;
            const QueryData* query = nullptr;
            std::vector<uint32_t> sizes;            ///< the size of one block of each column's type
            uint32_t fn = 0, user = 0;
        };

        ShardResult SHARD_CALL ChunkTrampoline(void* user, const ShardChunk* chunk)
        {
            auto* call = static_cast<ChunkCall*>(user);
            Sandbox& s = *call->sandbox;
            Instance& in = *s.instance;
            const size_t columns = call->sizes.size();
            uint64_t bytesPerEntity = 8;
            for (uint32_t size : call->sizes) bytesPerEntity += size;
            const uint32_t piece = uint32_t(std::max<uint64_t>(1, std::min<uint64_t>(chunk->count, (256ull << 10) / bytesPerEntity)));
            static const FuncType type{ { I, I }, { I } };

            for (uint32_t start = 0; start < chunk->count; start += piece) {
                const uint32_t n = std::min(piece, chunk->count - start);
                const SandboxHeap::Mark mark = s.heap.TempMark();
                const uint32_t chunkAt = s.heap.Temp(Layout::ShardChunk::kSize, 8);
                const uint32_t entitiesAt = s.heap.Temp(uint64_t(n) * 8, 16);
                const uint32_t columnsAt = s.heap.Temp(std::max<uint64_t>(columns * 4, 4), 16);
                if (!chunkAt || !entitiesAt || !columnsAt) { s.heap.TempRelease(mark); return SHARD_E_OUT_OF_MEMORY; }
                std::vector<uint32_t> at(columns);
                for (size_t c = 0; c < columns; ++c) {
                    at[c] = s.heap.Temp(uint64_t(n) * call->sizes[c], 16);
                    if (!at[c]) { s.heap.TempRelease(mark); return SHARD_E_OUT_OF_MEMORY; }
                    if (n * uint64_t(call->sizes[c]))
                        in.Write(at[c], static_cast<const uint8_t*>(chunk->columns[c]) + uint64_t(start) * call->sizes[c], uint32_t(uint64_t(n) * call->sizes[c]));
                }
                const size_t windowsBefore = s.windows.size();
                for (size_t c = 0; c < columns; ++c)
                    s.windows.push_back(Sandbox::Window{ static_cast<const uint8_t*>(chunk->columns[c]) + uint64_t(start) * call->sizes[c],
                                                         uint64_t(n) * call->sizes[c], at[c], call->sizes[c] });
                in.Write(entitiesAt, chunk->entities + start, n * 8);
                in.Write(columnsAt, at.data(), uint32_t(columns * 4));
                uint8_t header[Layout::ShardChunk::kSize] = {};
                Store<uint32_t>(header, Layout::ShardChunk::o_structSize, Layout::ShardChunk::kSize);
                Store<uint32_t>(header, Layout::ShardChunk::o_count, n);
                Store<uint32_t>(header, Layout::ShardChunk::o_entities, entitiesAt);
                Store<uint32_t>(header, Layout::ShardChunk::o_columns, columnsAt);
                in.Write(chunkAt, header, sizeof(header));

                const ShardResult result = s.CallResult(call->fn, type, { call->user, chunkAt }, Budget::Frame);
                s.windows.resize(windowsBefore);
                // What the system wrote : only the types it declared (an exclusive system may write any)
                if (!s.dead) {
                    for (size_t c = 0; c < columns; ++c) {
                        if (!MayWrite(s, call->query->with[c]) || n * uint64_t(call->sizes[c]) == 0) continue;
                        if (const uint8_t* p = in.MemoryAt(at[c], uint32_t(uint64_t(n) * call->sizes[c])))
                            std::memcpy(static_cast<uint8_t*>(chunk->columns[c]) + uint64_t(start) * call->sizes[c], p, size_t(n) * call->sizes[c]);
                    }
                }
                s.heap.TempRelease(mark);
                if (result != SHARD_OK) return result;
            }
            return SHARD_OK;
        }

        // ---- shard.systems ----

        struct WasmSystem final : Registration {
            Sandbox* sandbox = nullptr;
            uint32_t run = 0, user = 0;
            bool exclusive = false;
            std::vector<ShardComponentType> writes;
        };

        ShardResult SHARD_CALL SystemTrampoline(void* user, ShardPluginCtx, const ShardSystemCtx* ctx)
        {
            auto* sys = static_cast<WasmSystem*>(user);
            Sandbox& s = *sys->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            if (s.dead) return SHARD_E_INTERNAL;
            namespace Lc = Layout::ShardSystemCtx;
            const uint32_t at = s.heap.Temp(Lc::kSize, 8);
            if (!at) return SHARD_E_OUT_OF_MEMORY;
            uint8_t bytes[Lc::kSize] = {};
            Store<uint32_t>(bytes, Lc::o_structSize, Lc::kSize);
            Store<float>(bytes, Lc::o_deltaTime, ctx->deltaTime);
            Store<float>(bytes, Lc::o_fixedDeltaTime, ctx->fixedDeltaTime);
            Store<int32_t>(bytes, Lc::o_fixedStep, ctx->fixedStep);
            Store<int32_t>(bytes, Lc::o_fixedStepCount, ctx->fixedStepCount);
            Store<uint64_t>(bytes, Lc::o_frame, ctx->frame);
            Store<uint64_t>(bytes, Lc::o_ecs, ctx->ecs);
            s.instance->Write(at, bytes, sizeof(bytes));

            Sandbox::Run run;
            run.ecs = ctx->ecs;
            run.exclusive = sys->exclusive;
            run.writes = &sys->writes;
            Sandbox::Run* previous = s.run;
            s.run = &run;
            static const FuncType type{ { I, L, I }, { I } };
            const ShardResult result = s.CallResult(sys->run, type, { sys->user, s.ctx, at }, Budget::Frame);
            s.run = previous;
            return result;
        }

        // ---- shard.events ----

        struct WasmEvent final : Registration {
            Sandbox* sandbox = nullptr;
            uint32_t fn = 0, user = 0;
        };

        void SHARD_CALL EventTrampoline(void* user, ShardPluginCtx, ShardStr name, const void* payload, uint64_t size)
        {
            auto* e = static_cast<WasmEvent*>(user);
            Sandbox& s = *e->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            if (s.dead) return;
            const uint32_t at = s.heap.Temp(16 + name.size + size + 16, 16);
            if (!at) return;
            const uint32_t nameBytes = at + 16, payloadAt = uint32_t((uint64_t(nameBytes) + name.size + 15) / 16 * 16);
            uint8_t str[16] = {};
            Store<uint32_t>(str, 0, name.size ? nameBytes : 0);
            Store<uint64_t>(str, 8, name.size);
            Instance& in = *s.instance;
            in.Write(at, str, 16);
            if (name.size) in.Write(nameBytes, name.data, uint32_t(name.size));
            if (size) in.Write(payloadAt, payload, uint32_t(size));
            static const FuncType type{ { I, L, I, I, L }, {} };
            std::vector<uint64_t> out;
            s.CallTable(e->fn, type, { e->user, s.ctx, at, payloadAt, size }, Budget::Frame, out);
        }
    }

    bool ReadFieldDescs(Instance& in, uint64_t at, uint32_t count, std::vector<ShardFieldDesc>& out, std::deque<std::string>& strings)
    {
        return ReadFieldDescsImpl(in, at, count, out, strings);
    }

    void AddEcsImports(Sandbox& s, ImportList& imports, const HostApis& apis)
    {
        const ShardComponentsApi* components = apis.components;
        const ShardEcsApi* ecs = apis.ecs;
        const ShardSystemsApi* systems = apis.systems;
        const ShardEventsApi* events = apis.events;
        auto unsupported = [](Instance&, const uint64_t*, uint64_t* r) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; };

        // ---- shard.components ----

        if (!components) {
            for (const char* name : { "components.register_type", "components.field_at", "components.find_field", "components.blob_data",
                                      "components.blob_resize", "components.blob_free", "components.string_get" })
                imports.AddManual(name, unsupported);
        } else {
            imports.AddManual("components.register_type", [&s, components](Instance& in, const uint64_t* a, uint64_t* r) {
                namespace Ld = Layout::ShardComponentDesc;
                OutArg<ShardComponentType> out(in, a[2]);
                if (!out.ok) return false;
                const uint8_t* d = Span(in, a[1], Ld::kSize, "descriptor");
                if (!d) return false;
                uint8_t desc[Ld::kSize];
                std::memcpy(desc, d, Ld::kSize);
                std::deque<std::string> strings;
                std::vector<ShardFieldDesc> fields;
                const uint32_t fieldCount = Load<uint32_t>(desc, Ld::o_fieldCount);
                if (!ReadFieldDescs(in, Load<uint32_t>(desc, Ld::o_fields), fieldCount, fields, strings)) return false;
                std::string name;
                if (!ReadStr(in, a[1] + Ld::o_name, name, 256)) return false;
                ShardComponentDesc native;
                std::memset(&native, 0, sizeof(native));
                native.structSize = sizeof(ShardComponentDesc);
                native.size = Load<uint32_t>(desc, Ld::o_size);
                native.align = Load<uint32_t>(desc, Ld::o_align);
                native.fieldCount = fieldCount;
                native.name = ToStr(name);
                native.fields = fields.empty() ? nullptr : fields.data();
                std::vector<uint8_t> defaults;
                if (const uint32_t at = Load<uint32_t>(desc, Ld::o_defaults)) {
                    if (native.size > SHARD_COMPONENT_MAX_SIZE) { r[0] = uint32_t(SHARD_E_INVALID_ARG); return true; }
                    if (!ReadBytes(in, at, native.size, defaults)) return false;
                    native.defaults = defaults.data();
                }
                r[0] = uint32_t(components->register_type(s.ctx, &native, &out.value));
                return out.Commit();
            });

            // The fields of a type, with their strings copied into the module (they live in the host)
            imports.AddManual("components.field_at", [&s, components](Instance& in, const uint64_t* a, uint64_t* r) {
                if (!Span(in, a[3], Layout::ShardFieldDesc::kSize, "field")) return false;
                ShardFieldDesc d;
                std::memset(&d, 0, sizeof(d));
                d.structSize = sizeof(ShardFieldDesc);
                r[0] = uint32_t(components->field_at(s.ctx, a[1], uint32_t(a[2]), &d));
                return int32_t(uint32_t(r[0])) != SHARD_OK || WriteFieldDesc(s, in, a[3], d);
            });
            imports.AddManual("components.find_field", [&s, components](Instance& in, const uint64_t* a, uint64_t* r) {
                std::string name;
                if (!ReadStr(in, a[2], name, 256)) return false;
                if (!Span(in, a[3], Layout::ShardFieldDesc::kSize, "field")) return false;
                ShardFieldDesc d;
                std::memset(&d, 0, sizeof(d));
                d.structSize = sizeof(ShardFieldDesc);
                r[0] = uint32_t(components->find_field(s.ctx, a[1], ToStr(name), &d));
                return int32_t(uint32_t(r[0])) != SHARD_OK || WriteFieldDesc(s, in, a[3], d);
            });

            // Blobs : the bytes live in the host, the module is given a lasting copy that is written back whenever a call of it ends
            imports.AddManual("components.blob_data", [&s, components](Instance& in, const uint64_t* a, uint64_t* r) {
                if (!Span(in, a[2], 4, "data") || !Span(in, a[3], 8, "size")) return false;
                void* host = nullptr;
                uint64_t size = 0;
                r[0] = uint32_t(components->blob_data(s.ctx, a[1], &host, &size));
                if (int32_t(uint32_t(r[0])) != SHARD_OK) return true;
                const uint32_t at = s.MirrorBlob(a[1], host, size);
                if (!at) { r[0] = uint32_t(SHARD_E_OUT_OF_MEMORY); return true; }
                in.Write(uint32_t(a[2]), &at, 4);
                in.Write(uint32_t(a[3]), &size, 8);
                return true;
            });
            imports.AddManual("components.blob_resize", [&s, components](Instance&, const uint64_t* a, uint64_t* r) {
                s.DropBlobMirror(a[1]);
                r[0] = uint32_t(components->blob_resize(s.ctx, a[1], a[2]));
                return true;
            });
            imports.AddManual("components.blob_free", [&s, components](Instance&, const uint64_t* a, uint64_t* r) {
                s.DropBlobMirror(a[1]);
                r[0] = uint32_t(components->blob_free(s.ctx, a[1]));
                return true;
            });

            imports.AddManual("components.string_get", [&s, components](Instance& in, const uint64_t* a, uint64_t* r) {
                if (!Span(in, a[2], 16, "text")) return false;
                ShardStr text{ nullptr, 0 };
                r[0] = uint32_t(components->string_get(s.ctx, a[1], &text));
                if (int32_t(uint32_t(r[0])) != SHARD_OK) return true;
                uint8_t out[16] = {};
                if (text.data && text.size) {
                    const uint32_t at = s.heap.Temp(text.size + 1, 1);       // valid until the call of the module ends
                    if (!at) { r[0] = uint32_t(SHARD_E_OUT_OF_MEMORY); return true; }
                    in.Write(at, text.data, uint32_t(text.size));
                    Store<uint32_t>(out, 0, at);
                    Store<uint64_t>(out, 8, text.size);
                }
                return in.Write(uint32_t(a[2]), out, 16) || (in.HostTrap("output out of the memory"), false);
            });
        }

        // ---- shard.ecs ----

        if (!ecs || !components) {
            for (const char* name : { "ecs.each_chunk", "ecs.count", "ecs.get" }) imports.AddManual(name, unsupported);
        } else {
            imports.AddManual("ecs.each_chunk", [&s, ecs, components](Instance& in, const uint64_t* a, uint64_t* r) {
                QueryData query;
                if (!ReadQuery(in, a[1], query)) return false;
                ChunkCall call;
                call.sandbox = &s;
                call.query = &query;
                call.fn = uint32_t(a[2]);
                call.user = uint32_t(a[3]);
                for (ShardComponentType type : query.with) {
                    uint32_t size = 0, align = 0;
                    if (components->type_layout(s.ctx, type, &size, &align) != SHARD_OK) { r[0] = uint32_t(SHARD_E_NOT_FOUND); return true; }
                    call.sizes.push_back(size);
                }
                r[0] = uint32_t(ecs->each_chunk(a[0], &query.native, &ChunkTrampoline, &call));
                return true;
            });
            imports.AddManual("ecs.count", [ecs](Instance& in, const uint64_t* a, uint64_t* r) {
                QueryData query;
                if (!ReadQuery(in, a[1], query)) return false;
                OutArg<uint64_t> out(in, a[2]);
                if (!out.ok) return false;
                r[0] = uint32_t(ecs->count(a[0], &query.native, &out.value));
                return out.Commit();
            });
            imports.AddManual("ecs.get", [&s, ecs, components](Instance& in, const uint64_t* a, uint64_t* r) {
                if (!Span(in, a[3], 4, "output")) return false;
                void* host = nullptr;
                r[0] = uint32_t(ecs->get(a[0], a[1], a[2], &host));
                if (int32_t(uint32_t(r[0])) != SHARD_OK) return true;
                uint32_t size = 0, align = 0;
                if (components->type_layout(s.ctx, a[2], &size, &align) != SHARD_OK) { r[0] = uint32_t(SHARD_E_INTERNAL); return true; }
                // A block of the chunk being run is already in the module : the same address
                uint32_t at = 0;
                for (const Sandbox::Window& w : s.windows) {
                    const uint8_t* p = static_cast<const uint8_t*>(host);
                    if (w.blockSize == size && p >= w.host && p < w.host + w.bytes && uint64_t(p - w.host) % size == 0) { at = w.wasm + uint32_t(p - w.host); break; }
                }
                // Otherwise the block stays in the host ; the module works on a copy, written back when its system returns (if the system may write it)
                if (!at) at = s.MirrorTemp(host, size, MayWrite(s, a[2]));
                if (!at) { r[0] = uint32_t(SHARD_E_OUT_OF_MEMORY); return true; }
                in.Write(uint32_t(a[3]), &at, 4);
                return true;
            });
        }

        // ---- shard.systems ----

        if (!systems) {
            imports.AddManual("systems.add", unsupported);
        } else {
            imports.AddManual("systems.add", [&s, systems](Instance& in, const uint64_t* a, uint64_t* r) {
                namespace Ls = Layout::ShardSystemDesc;
                const uint8_t* d = Span(in, a[1], Ls::kSize, "descriptor");
                if (!d) return false;
                uint8_t desc[Ls::kSize];
                std::memcpy(desc, d, Ls::kSize);
                const uint32_t readCount = Load<uint32_t>(desc, Ls::o_readCount), writeCount = Load<uint32_t>(desc, Ls::o_writeCount);
                const uint32_t afterCount = Load<uint32_t>(desc, Ls::o_afterCount), beforeCount = Load<uint32_t>(desc, Ls::o_beforeCount);
                if (readCount > kMaxListed || writeCount > kMaxListed || afterCount > kMaxListed || beforeCount > kMaxListed) {
                    in.HostTrap("a system with more than 256 components or ordering constraints");
                    return false;
                }
                auto types = [&](uint32_t pointer, uint32_t count, std::vector<ShardComponentType>& out) {
                    out.resize(count);
                    if (count == 0) return true;
                    const uint8_t* p = Span(in, pointer, uint64_t(count) * 8, "components");
                    if (!p) return false;
                    std::memcpy(out.data(), p, size_t(count) * 8);
                    return true;
                };
                std::vector<ShardComponentType> reads, writes;
                if (!types(Load<uint32_t>(desc, Ls::o_reads), readCount, reads) || !types(Load<uint32_t>(desc, Ls::o_writes), writeCount, writes)) return false;
                std::vector<std::string> afterText(afterCount), beforeText(beforeCount);
                std::vector<ShardStr> after(afterCount), before(beforeCount);
                for (uint32_t i = 0; i < afterCount; ++i) {
                    if (!ReadStr(in, uint64_t(Load<uint32_t>(desc, Ls::o_after)) + 16ull * i, afterText[i], 512)) return false;
                    after[i] = ToStr(afterText[i]);
                }
                for (uint32_t i = 0; i < beforeCount; ++i) {
                    if (!ReadStr(in, uint64_t(Load<uint32_t>(desc, Ls::o_before)) + 16ull * i, beforeText[i], 512)) return false;
                    before[i] = ToStr(beforeText[i]);
                }
                std::string name;
                if (!ReadStr(in, a[1] + Ls::o_name, name, 256)) return false;

                auto* system = s.Register<WasmSystem>();
                system->sandbox = &s;
                system->run = Load<uint32_t>(desc, Ls::o_run);
                system->user = Load<uint32_t>(desc, Ls::o_user);
                system->exclusive = (Load<uint32_t>(desc, Ls::o_flags) & SHARD_SYSTEM_EXCLUSIVE) != 0;
                system->writes = writes;

                ShardSystemDesc native;
                std::memset(&native, 0, sizeof(native));
                native.structSize = sizeof(ShardSystemDesc);
                native.phase = Load<uint32_t>(desc, Ls::o_phase);
                native.flags = Load<uint32_t>(desc, Ls::o_flags) | SHARD_SYSTEM_MAIN_THREAD;       // an instance has one stack : never two systems at once
                native.readCount = readCount;
                native.writeCount = writeCount;
                native.afterCount = afterCount;
                native.beforeCount = beforeCount;
                native.name = ToStr(name);
                native.reads = reads.empty() ? nullptr : reads.data();
                native.writes = writes.empty() ? nullptr : writes.data();
                native.after = after.empty() ? nullptr : after.data();
                native.before = before.empty() ? nullptr : before.data();
                native.run = &SystemTrampoline;
                native.user = system;
                r[0] = uint32_t(systems->add(s.ctx, &native));
                return true;
            });
        }

        // ---- shard.events ----

        if (!events) {
            for (const char* name : { "events.subscribe", "events.subscribe_world", "events.describe" }) imports.AddManual(name, unsupported);
        } else {
            imports.AddManual("events.subscribe", [&s, events](Instance& in, const uint64_t* a, uint64_t* r) {
                std::string name;
                if (!ReadStr(in, a[1], name, 512)) return false;
                OutArg<ShardSubscription> out(in, a[4]);
                if (!out.ok) return false;
                auto* e = s.Register<WasmEvent>();
                e->sandbox = &s;
                e->fn = uint32_t(a[2]);
                e->user = uint32_t(a[3]);
                r[0] = uint32_t(events->subscribe(s.ctx, ToStr(name), &EventTrampoline, e, &out.value));
                return out.Commit();
            });
            imports.AddManual("events.subscribe_world", [&s, events](Instance& in, const uint64_t* a, uint64_t* r) {
                std::string name;
                if (!ReadStr(in, a[2], name, 512)) return false;
                OutArg<ShardSubscription> out(in, a[5]);
                if (!out.ok) return false;
                auto* e = s.Register<WasmEvent>();
                e->sandbox = &s;
                e->fn = uint32_t(a[3]);
                e->user = uint32_t(a[4]);
                r[0] = uint32_t(events->subscribe_world(s.ctx, int32_t(uint32_t(a[1])), ToStr(name), &EventTrampoline, e, &out.value));
                return out.Commit();
            });
            imports.AddManual("events.describe", [&s, events](Instance& in, const uint64_t* a, uint64_t* r) {
                std::string name;
                if (!ReadStr(in, a[1], name, 512)) return false;
                std::deque<std::string> strings;
                std::vector<ShardFieldDesc> fields;
                if (!ReadFieldDescs(in, a[3], uint32_t(a[4]), fields, strings)) return false;
                r[0] = uint32_t(events->describe(s.ctx, ToStr(name), uint32_t(a[2]), fields.empty() ? nullptr : fields.data(), uint32_t(fields.size())));
                return true;
            });
        }
    }
}
