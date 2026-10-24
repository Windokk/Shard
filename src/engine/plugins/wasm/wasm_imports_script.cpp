#include <algorithm>
#include <cstring>
#include <deque>
#include <unordered_map>

#include <shard/shard_scripts.h>

#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/wasm/wasm_imports.hpp"
#include "engine/plugins/wasm/wasm_marshal.hpp"

// shard.scripts for a sandboxed plugin. A script type of the sandbox is a script type of the host (shard.scripts, c_scripts.cpp) whose
// callbacks are trampolines into the module. The object of a script lives in the module's memory, since its code can reach nothing else ;
// the host keeps storage of the same size for it, in which it reads and writes the object's described fields (the inspector, the world
// file, a copy of the actor). The two are kept equal field by field : before a callback runs, what the host changed since the last time is
// copied into the module's object ; after it, what the module changed is copied back. (A field both sides changed in between keeps the host's
// value : an edit in the inspector wins.) Only the described fields travel : the rest of the object (a std::string, a pointer) is the module's.

namespace Shard::Engine::Plugins::Wasm {

    using namespace Marshal;

    namespace {
        constexpr ValType I = ValType::I32, L = ValType::I64, F = ValType::F32;

        // The callbacks of the wasm32 ShardScriptCallbacks, in the order of the struct
        enum Cb : uint32_t {
            CConstruct, CDestruct, CAttach, COnCreate, COnWorldLoaded, COnWorldUnloaded, COnPlay, COnTick, COnFixedTick, COnLateTick, COnStop, COnDestroyed,
            COnActivated, COnDeactivated, COnContactAdded, COnContactPersisted, COnContactEnded, CTextGet, CTextSet, CbCount
        };
        constexpr uint32_t kCbOffsets[CbCount] = {
            Layout::ShardScriptCallbacks::o_construct, Layout::ShardScriptCallbacks::o_destruct, Layout::ShardScriptCallbacks::o_attach,
            Layout::ShardScriptCallbacks::o_on_create, Layout::ShardScriptCallbacks::o_on_world_loaded, Layout::ShardScriptCallbacks::o_on_world_unloaded,
            Layout::ShardScriptCallbacks::o_on_play, Layout::ShardScriptCallbacks::o_on_tick, Layout::ShardScriptCallbacks::o_on_fixed_tick,
            Layout::ShardScriptCallbacks::o_on_late_tick, Layout::ShardScriptCallbacks::o_on_stop, Layout::ShardScriptCallbacks::o_on_destroyed,
            Layout::ShardScriptCallbacks::o_on_activated, Layout::ShardScriptCallbacks::o_on_deactivated, Layout::ShardScriptCallbacks::o_on_contact_added,
            Layout::ShardScriptCallbacks::o_on_contact_persisted, Layout::ShardScriptCallbacks::o_on_contact_ended, Layout::ShardScriptCallbacks::o_text_get,
            Layout::ShardScriptCallbacks::o_text_set,
        };

        struct Object {
            uint32_t at = 0;                        ///< the object, in the module's memory
            std::vector<uint8_t> shadow;            ///< the described fields as they were when the two sides last agreed
        };

        struct WasmScript;

        // Every object of the sandbox, by the host storage that stands for it
        struct Directory final : Registration {
            Sandbox* sandbox = nullptr;
            std::unordered_map<void*, std::pair<WasmScript*, Object>> objects;
            ~Directory() override
            {
                for (auto& [storage, entry] : objects) sandbox->heap.Free(entry.second.at);
            }
        };

        struct WasmScript final : Registration {
            Sandbox* sandbox = nullptr;
            Directory* directory = nullptr;
            uint32_t user = 0;
            uint32_t size = 0, align = 0;
            uint32_t cb[CbCount] = {};
            std::vector<std::pair<uint32_t, uint32_t>> ranges;      ///< (offset, size) of the fields both sides share
            uint32_t shadowSize = 0;
            ShardScriptCallbacks native{};
            std::string text;                                       ///< the answer of the last text_get
        };

        // Host -> module, for the fields the host changed since the sides last agreed
        void Push(WasmScript& t, void* storage, Object& o)
        {
            uint8_t* mem = t.sandbox->instance->MemoryAt(o.at, t.size);
            if (!mem) return;
            const uint8_t* host = static_cast<const uint8_t*>(storage);
            uint32_t at = 0;
            for (const auto& [offset, n] : t.ranges) {
                if (std::memcmp(host + offset, o.shadow.data() + at, n) != 0) {
                    std::memcpy(mem + offset, host + offset, n);
                    std::memcpy(o.shadow.data() + at, host + offset, n);
                }
                at += n;
            }
        }

        // Module -> host, for the fields the module changed (all of them when `force`)
        void Pull(WasmScript& t, void* storage, Object& o, bool force)
        {
            const uint8_t* mem = t.sandbox->instance->MemoryAt(o.at, t.size);
            if (!mem) return;
            uint8_t* host = static_cast<uint8_t*>(storage);
            uint32_t at = 0;
            for (const auto& [offset, n] : t.ranges) {
                if (force || std::memcmp(mem + offset, o.shadow.data() + at, n) != 0) {
                    std::memcpy(host + offset, mem + offset, n);
                    std::memcpy(o.shadow.data() + at, mem + offset, n);
                }
                at += n;
            }
        }

        // Runs one callback of the script on the object : the fields are brought up to date first, and the module's changes taken back after
        ShardResult CallObject(WasmScript* t, void* storage, Cb callback, const FuncType& type, std::vector<uint64_t> extra)
        {
            Sandbox& s = *t->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            if (s.dead) return SHARD_E_INTERNAL;
            auto it = t->directory->objects.find(storage);
            if (it == t->directory->objects.end()) return SHARD_E_STALE_HANDLE;
            Object& o = it->second.second;
            Push(*t, storage, o);
            std::vector<uint64_t> args{ t->user, o.at };
            args.insert(args.end(), extra.begin(), extra.end());
            const ShardResult result = s.CallResult(t->cb[callback], type, args, Budget::Frame);
            auto again = t->directory->objects.find(storage);                  // the callback may have destroyed it
            if (!s.dead && again != t->directory->objects.end()) Pull(*t, storage, again->second.second, false);
            return result;
        }

        const FuncType kFn{ { I, I }, { I } };
        const FuncType kTick{ { I, I, F }, { I } };
        const FuncType kContact{ { I, I, I }, { I } };

        template <Cb C>
        ShardResult SHARD_CALL Fn(void* user, void* object) { return CallObject(static_cast<WasmScript*>(user), object, C, kFn, {}); }

        template <Cb C>
        ShardResult SHARD_CALL Tick(void* user, void* object, float dt)
        {
            uint32_t bits;
            std::memcpy(&bits, &dt, 4);
            return CallObject(static_cast<WasmScript*>(user), object, C, kTick, { bits });
        }

        template <Cb C>
        ShardResult SHARD_CALL Contact(void* user, void* object, const ShardContact* contact)
        {
            auto* t = static_cast<WasmScript*>(user);
            Sandbox& s = *t->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            namespace Lc = Layout::ShardContact;
            const uint32_t at = s.dead ? 0 : s.heap.Temp(Lc::kSize, 8);
            if (!at) return SHARD_E_INTERNAL;
            uint8_t bytes[Lc::kSize] = {};
            Store<uint32_t>(bytes, Lc::o_structSize, Lc::kSize);
            Store<uint64_t>(bytes, Lc::o_other, contact->other);
            std::memcpy(bytes + Lc::o_point, contact->point, 12);
            std::memcpy(bytes + Lc::o_normal, contact->normal, 12);
            Store<float>(bytes, Lc::o_penetration, contact->penetration);
            s.instance->Write(at, bytes, sizeof(bytes));
            return CallObject(t, object, C, kContact, { at });
        }

        ShardResult SHARD_CALL AttachTrampoline(void* user, void* object, ShardActor owner)
        {
            static const FuncType type{ { I, I, L }, { I } };
            return CallObject(static_cast<WasmScript*>(user), object, CAttach, type, { owner });
        }

        ShardResult SHARD_CALL ConstructTrampoline(void* user, void* storage)
        {
            auto* t = static_cast<WasmScript*>(user);
            Sandbox& s = *t->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            if (s.dead) return SHARD_E_INTERNAL;
            const uint32_t at = s.heap.Alloc(t->size, t->align);
            if (!at) return SHARD_E_OUT_OF_MEMORY;
            Object object;
            object.at = at;
            object.shadow.assign(t->shadowSize, 0);
            t->directory->objects[storage] = { t, std::move(object) };
            const ShardResult result = s.CallResult(t->cb[CConstruct], kFn, { t->user, at }, Budget::Frame);
            auto it = t->directory->objects.find(storage);
            if (result != SHARD_OK || s.dead || it == t->directory->objects.end()) {
                if (it != t->directory->objects.end()) t->directory->objects.erase(it);
                s.heap.Free(at);
                return result != SHARD_OK ? result : SHARD_E_INTERNAL;
            }
            Pull(*t, storage, it->second.second, true);                            // its defaults are the host's starting point
            return SHARD_OK;
        }

        void SHARD_CALL DestructTrampoline(void* user, void* storage)
        {
            auto* t = static_cast<WasmScript*>(user);
            Sandbox& s = *t->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            auto it = t->directory->objects.find(storage);
            if (it == t->directory->objects.end()) return;
            const uint32_t at = it->second.second.at;
            if (!s.dead) {
                static const FuncType type{ { I, I }, {} };
                std::vector<uint64_t> out;
                s.CallTable(t->cb[CDestruct], type, { t->user, at }, Budget::Frame, out);
            }
            t->directory->objects.erase(storage);
            s.heap.Free(at);
        }

        // The strings the plugin keeps itself (accessor fields) : the module writes the answer to a ShardStr, which is copied out
        void SHARD_CALL TextGetTrampoline(void* user, void* object, uint32_t offset, ShardStr* out)
        {
            auto* t = static_cast<WasmScript*>(user);
            Sandbox& s = *t->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            *out = ShardStr{ nullptr, 0 };
            if (s.dead) return;
            auto it = t->directory->objects.find(object);
            if (it == t->directory->objects.end()) return;
            const uint32_t slot = s.heap.Temp(16, 8);
            if (!slot) return;
            const uint8_t zero[16] = {};
            s.instance->Write(slot, zero, 16);
            static const FuncType type{ { I, I, I, I }, {} };
            std::vector<uint64_t> results;
            if (!s.CallTable(t->cb[CTextGet], type, { t->user, it->second.second.at, offset, slot }, Budget::Frame, results)) return;
            uint8_t str[16] = {};
            s.instance->Read(slot, str, 16);
            const uint32_t data = Load<uint32_t>(str, 0);
            const uint64_t size = Load<uint64_t>(str, 8);
            const uint8_t* bytes = size && size <= (16u << 20) ? s.instance->MemoryAt(data, uint32_t(size)) : nullptr;
            t->text.assign(bytes ? reinterpret_cast<const char*>(bytes) : "", bytes ? size_t(size) : 0);
            *out = ShardStr{ t->text.data(), t->text.size() };
        }

        ShardResult SHARD_CALL TextSetTrampoline(void* user, void* object, uint32_t offset, ShardStr text)
        {
            auto* t = static_cast<WasmScript*>(user);
            Sandbox& s = *t->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            if (s.dead) return SHARD_E_INTERNAL;
            const uint32_t at = s.heap.Temp(16 + text.size, 16);
            if (!at) return SHARD_E_OUT_OF_MEMORY;
            uint8_t str[16] = {};
            Store<uint32_t>(str, 0, text.size ? at + 16 : 0);
            Store<uint64_t>(str, 8, text.size);
            s.instance->Write(at, str, 16);
            if (text.size) s.instance->Write(at + 16, text.data, uint32_t(text.size));
            static const FuncType type{ { I, I, I, I }, { I } };
            return CallObject(t, object, CTextSet, type, { offset, at });
        }

        // The module's object behind the host's storage, brought up to date : its changes go back to the host when the outermost call ends
        bool GiveObject(Sandbox& s, Directory& directory, Instance& in, void* storage, uint64_t out)
        {
            auto it = directory.objects.find(storage);
            if (it == directory.objects.end()) return false;
            WasmScript* t = it->second.first;
            Push(*t, storage, it->second.second);
            const uint32_t at = it->second.second.at;
            in.Write(uint32_t(out), &at, 4);
            Directory* dir = &directory;
            s.OnCallEnd([t, dir, storage] {
                auto found = dir->objects.find(storage);
                if (found != dir->objects.end()) Pull(*t, storage, found->second.second, false);
            });
            return true;
        }
    }

    void AddScriptImports(Sandbox& s, ImportList& imports, const HostApis& apis)
    {
        const ShardScriptsApi* scripts = apis.scripts;
        if (!scripts) {
            for (const char* name : { "scripts.register_script", "scripts.get_object", "scripts.add_to_actor" })
                imports.AddManual(name, [](Instance&, const uint64_t*, uint64_t* r) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; });
            return;
        }

        auto directory = [&s]() -> Directory& {
            for (auto& r : s.registrations)
                if (auto* d = dynamic_cast<Directory*>(r.get())) return *d;
            Directory* created = s.Register<Directory>();
            created->sandbox = &s;
            return *created;
        };

        imports.AddManual("scripts.register_script", [&s, scripts, directory](Instance& in, const uint64_t* a, uint64_t* r) {
            namespace Ls = Layout::ShardScriptDesc;
            const uint8_t* d = Span(in, a[1], Ls::kSize, "descriptor");
            if (!d) return false;
            uint8_t desc[Ls::kSize];
            std::memcpy(desc, d, Ls::kSize);
            const uint32_t size = Load<uint32_t>(desc, Ls::o_size), align = Load<uint32_t>(desc, Ls::o_align), fieldCount = Load<uint32_t>(desc, Ls::o_fieldCount);

            std::string name;
            std::deque<std::string> strings;
            std::vector<ShardFieldDesc> fields;
            if (!ReadStr(in, a[1] + Ls::o_name, name, 256) || !ReadFieldDescs(in, Load<uint32_t>(desc, Ls::o_fields), fieldCount, fields, strings)) return false;

            // The callbacks : table indices ; a slot the module's struct is too short for stays empty
            const uint32_t callbacksAt = Load<uint32_t>(desc, Ls::o_callbacks);
            uint32_t table[CbCount] = {};
            if (callbacksAt) {
                uint32_t structSize = 0;
                if (!in.Read(callbacksAt, &structSize, 4)) { in.HostTrap("callbacks out of the memory"); return false; }
                const uint8_t* cb = Span(in, callbacksAt, std::min<uint32_t>(structSize, Layout::ShardScriptCallbacks::kSize), "callbacks");
                if (!cb) return false;
                const uint32_t have = std::min<uint32_t>(structSize, Layout::ShardScriptCallbacks::kSize);
                for (uint32_t i = 0; i < CbCount; ++i)
                    if (kCbOffsets[i] + 4 <= have) table[i] = Load<uint32_t>(cb, kCbOffsets[i]);
            }
            if (!table[CConstruct] || !table[CDestruct]) { r[0] = uint32_t(SHARD_E_INVALID_ARG); in.HostTrap("a script needs construct and destruct"); return false; }

            auto* t = s.Register<WasmScript>();
            t->sandbox = &s;
            t->directory = &directory();
            t->user = Load<uint32_t>(desc, Ls::o_user);
            t->size = size;
            t->align = align;
            std::memcpy(t->cb, table, sizeof(table));
            // The fields both sides share : every one but the plugin's own strings
            for (const ShardFieldDesc& f : fields) {
                if (f.flags & SHARD_FIELD_ACCESSOR) continue;
                uint32_t fieldSize = 0, fieldAlign = 0;
                if (!CAbi::FieldLayoutOf(f.type, fieldSize, fieldAlign) || uint64_t(f.offset) + fieldSize > size) { r[0] = uint32_t(SHARD_E_INVALID_ARG); return true; }
                t->ranges.emplace_back(f.offset, fieldSize);
                t->shadowSize += fieldSize;
            }

            // What the host calls : trampolines, for the slots the module filled
            ShardScriptCallbacks& n = t->native;
            n.structSize = sizeof(ShardScriptCallbacks);
            n.construct = &ConstructTrampoline;
            n.destruct = &DestructTrampoline;
            if (t->cb[CAttach]) n.attach = &AttachTrampoline;
            if (t->cb[COnCreate]) n.on_create = &Fn<COnCreate>;
            if (t->cb[COnWorldLoaded]) n.on_world_loaded = &Fn<COnWorldLoaded>;
            if (t->cb[COnWorldUnloaded]) n.on_world_unloaded = &Fn<COnWorldUnloaded>;
            if (t->cb[COnPlay]) n.on_play = &Fn<COnPlay>;
            if (t->cb[COnTick]) n.on_tick = &Tick<COnTick>;
            if (t->cb[COnFixedTick]) n.on_fixed_tick = &Tick<COnFixedTick>;
            if (t->cb[COnLateTick]) n.on_late_tick = &Tick<COnLateTick>;
            if (t->cb[COnStop]) n.on_stop = &Fn<COnStop>;
            if (t->cb[COnDestroyed]) n.on_destroyed = &Fn<COnDestroyed>;
            if (t->cb[COnActivated]) n.on_activated = &Fn<COnActivated>;
            if (t->cb[COnDeactivated]) n.on_deactivated = &Fn<COnDeactivated>;
            if (t->cb[COnContactAdded]) n.on_contact_added = &Contact<COnContactAdded>;
            if (t->cb[COnContactPersisted]) n.on_contact_persisted = &Contact<COnContactPersisted>;
            if (t->cb[COnContactEnded]) n.on_contact_ended = &Contact<COnContactEnded>;
            if (t->cb[CTextGet]) n.text_get = &TextGetTrampoline;
            if (t->cb[CTextSet]) n.text_set = &TextSetTrampoline;

            ShardScriptDesc native;
            std::memset(&native, 0, sizeof(native));
            native.structSize = sizeof(ShardScriptDesc);
            native.size = size;
            native.align = align;
            native.fieldCount = fieldCount;
            native.name = ToStr(name);
            native.fields = fields.empty() ? nullptr : fields.data();
            native.user = t;
            native.callbacks = &n;
            r[0] = uint32_t(scripts->register_script(s.ctx, &native));
            return true;
        });

        imports.AddManual("scripts.get_object", [&s, scripts, directory](Instance& in, const uint64_t* a, uint64_t* r) {
            std::string name;
            if (!ReadStr(in, a[2], name, 256) || !Span(in, a[3], 4, "output")) return false;
            void* storage = nullptr;
            r[0] = uint32_t(scripts->get_object(s.ctx, a[1], ToStr(name), &storage));
            const uint32_t none = 0;
            in.Write(uint32_t(a[3]), &none, 4);
            if (int32_t(uint32_t(r[0])) != SHARD_OK || !storage) return true;
            if (!GiveObject(s, directory(), in, storage, a[3])) r[0] = uint32_t(SHARD_E_NOT_FOUND);
            return true;
        });

        imports.AddManual("scripts.add_to_actor", [&s, scripts, directory](Instance& in, const uint64_t* a, uint64_t* r) {
            std::string name;
            if (!ReadStr(in, a[2], name, 256) || !Span(in, a[3], 4, "output")) return false;
            void* storage = nullptr;
            r[0] = uint32_t(scripts->add_to_actor(s.ctx, a[1], ToStr(name), &storage));
            const uint32_t none = 0;
            in.Write(uint32_t(a[3]), &none, 4);
            if (int32_t(uint32_t(r[0])) != SHARD_OK || !storage) return true;
            if (!GiveObject(s, directory(), in, storage, a[3])) r[0] = uint32_t(SHARD_E_NOT_FOUND);
            return true;
        });
    }
}
