#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <shard/shard_components.h>
#include <shard/shard_ecs.h>

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/core/ecs/registry.hpp"
#include "engine/plugins/abi/c_fields.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/ecs_components.hpp"

// shard.components : a type described by a C plugin is an ECS component (its block of bytes, what systems work on) and an actor component
// (CAbiComponent : what a world saves and the inspector shows), the second keeping the first up to date on the actor's entity.

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        static_assert(sizeof(ShardEntity) == sizeof(Core::Ecs::Entity), "ShardEntity is an Ecs::Entity");
        static_assert(sizeof(ShardLocalTransform) == sizeof(Worlds::LocalTransform), "ShardLocalTransform mirrors Worlds::LocalTransform");
        static_assert(sizeof(ShardActorLink) == sizeof(Worlds::ActorLink), "ShardActorLink mirrors Worlds::ActorLink");

        struct CType : FieldSet {
            std::string name;                       // full : "<plugin id>.<name>"
            PluginOwner owner = 0;
            Core::Ecs::ComponentId ecs = Core::Ecs::kInvalidComponent;
            uint32_t size = 0;
            uint32_t align = 0;
            std::vector<unsigned char> defaults;
        };

        std::mutex gTypesMutex;
        std::map<std::string, std::shared_ptr<CType>> gTypes;  // the types of the plugins that are enabled, by full name

        struct FieldLayout {
            TypeID type;
            uint32_t size;
            uint32_t align;
            void (*copy)(void*, const void*);
            void (*assign)(void*, const void*);
            void (*destroy)(void*);
            bool (*equals)(const void*, const void*);
        };

        static_assert(sizeof(Filesystem::AssetID) == sizeof(int32_t), "SHARD_FIELD_ASSET is an AssetID");

        template <typename T>
        FieldLayout Layout(TypeID type)
        {
            return FieldLayout{ type, uint32_t(sizeof(T)), uint32_t(alignof(T)), &CopyConstruct<T>, &Assign<T>, &::Destroy<T>, &Equals<T> };
        }

        bool LayoutOf(uint32_t type, FieldLayout& out)
        {
            switch (type) {
                case SHARD_FIELD_F32: out = Layout<float>(TypeID::Float); return true;
                case SHARD_FIELD_F64: out = Layout<double>(TypeID::Double); return true;
                case SHARD_FIELD_I32: out = Layout<int32_t>(TypeID::Int32); return true;
                case SHARD_FIELD_U32: out = Layout<uint32_t>(TypeID::UInt32); return true;
                case SHARD_FIELD_I64: out = Layout<int64_t>(TypeID::Int64); return true;
                case SHARD_FIELD_U64: out = Layout<uint64_t>(TypeID::UInt64); return true;
                case SHARD_FIELD_BOOL: out = Layout<bool>(TypeID::Bool); return true;
                case SHARD_FIELD_VEC2: out = Layout<glm::vec2>(TypeID::Vec2); return true;
                case SHARD_FIELD_VEC3: out = Layout<glm::vec3>(TypeID::Vec3); return true;
                case SHARD_FIELD_VEC4: out = Layout<glm::vec4>(TypeID::Vec4); return true;
                case SHARD_FIELD_QUAT: out = Layout<glm::quat>(TypeID::Quat); return true;
                case SHARD_FIELD_COLOR: out = Layout<glm::vec4>(TypeID::ColorRGBA); return true;
                case SHARD_FIELD_BLOB: out = Layout<uint64_t>(TypeID::UInt64); return true;      // never reflected : saved by CAbiComponent
                case SHARD_FIELD_STRING: out = Layout<uint64_t>(TypeID::UInt64); return true;    // the block holds a blob handle ; the std::string beside it is what is reflected
                case SHARD_FIELD_ASSET: out = Layout<Filesystem::AssetID>(TypeID::Asset); return true;
                case SHARD_FIELD_ACTOR: out = Layout<uint64_t>(TypeID::UInt64); return true;      // a handle ; the path in a std::string beside it is what is reflected
                default: return false;
            }
        }

        bool IsIdentifier(const std::string& s, bool dots)
        {
            if (s.empty() || s.size() > 64) return false;
            if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
            return std::all_of(s.begin(), s.end(), [dots](unsigned char c) { return std::isalnum(c) || c == '_' || (dots && c == '.'); });
        }

        ShardComponentType Handle(Core::Ecs::ComponentId id) { return ShardComponentType(id) + 1; }

        // The ECS id behind a handle, if the global ECS registry knows it
        bool EcsIdOf(ShardComponentType type, Core::Ecs::ComponentId& out)
        {
            if (type == 0 || type > uint64_t(Core::Ecs::ComponentRegistry::Global().Count())) return false;
            out = Core::Ecs::ComponentId(type - 1);
            return true;
        }

        std::shared_ptr<CType> TypeOf(ShardComponentType handle)
        {
            Core::Ecs::ComponentId id;
            if (!EcsIdOf(handle, id)) return nullptr;
            std::lock_guard<std::mutex> lock(gTypesMutex);
            for (const auto& kv : gTypes) if (kv.second->ecs == id) return kv.second;
            return nullptr;
        }

        // ---- Base64 : how a blob is saved in a world file ----

        const char* kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string EncodeBase64(const std::string& bytes)
        {
            std::string out;
            out.reserve((bytes.size() + 2) / 3 * 4);
            size_t i = 0;
            for (; i + 2 < bytes.size(); i += 3) {
                const uint32_t v = (uint32_t(uint8_t(bytes[i])) << 16) | (uint32_t(uint8_t(bytes[i + 1])) << 8) | uint8_t(bytes[i + 2]);
                for (int k = 3; k >= 0; --k) out += kBase64[(v >> (6 * k)) & 63];
            }
            if (i < bytes.size()) {
                const bool two = i + 1 < bytes.size();
                const uint32_t v = (uint32_t(uint8_t(bytes[i])) << 16) | (two ? uint32_t(uint8_t(bytes[i + 1])) << 8 : 0u);
                out += kBase64[(v >> 18) & 63];
                out += kBase64[(v >> 12) & 63];
                out += two ? kBase64[(v >> 6) & 63] : '=';
                out += '=';
            }
            return out;
        }

        bool DecodeBase64(const std::string& text, std::string& out)
        {
            out.clear();
            uint32_t buffer = 0;
            int bits = 0;
            for (char c : text) {
                if (c == '=') break;
                const char* at = c ? std::strchr(kBase64, c) : nullptr;
                if (!at) return false;
                buffer = (buffer << 6) | uint32_t(at - kBase64);
                bits += 6;
                if (bits >= 8) {
                    bits -= 8;
                    out += char((buffer >> bits) & 0xFF);
                }
            }
            return true;
        }

        // ---- Blobs : bytes the host keeps for a plugin, counted against its budget ----

        struct BlobSlot {
            PluginOwner owner = 0;
            void* data = nullptr;
            uint64_t size = 0;
            uint32_t generation = 1;
            bool used = false;
        };
        std::mutex gBlobMutex;
        std::vector<BlobSlot> gBlobs;

        // gBlobMutex held
        BlobSlot* FindBlob(PluginOwner owner, ShardBlob blob)
        {
            const uint64_t index = blob & 0xFFFFFFFFull;
            if (index == 0 || index > gBlobs.size()) return nullptr;
            BlobSlot& slot = gBlobs[size_t(index - 1)];
            return slot.used && slot.owner == owner && slot.generation == uint32_t(blob >> 32) ? &slot : nullptr;
        }

        // gBlobMutex held : the slot is free again, its handle stale
        void EmptySlot(BlobSlot& slot)
        {
            const uint32_t generation = slot.generation + 1 == 0 ? 1 : slot.generation + 1;
            slot = BlobSlot{};
            slot.generation = generation;
        }

        // The memory of a blob : at least 1 byte, so that an empty blob is a valid pointer too
        void* AllocateBlob(PluginContext& ctx, uint64_t size)
        {
            void* data = ctx.Memory().Allocate(size_t(size ? size : 1), 16);
            if (data) std::memset(data, 0, size_t(size ? size : 1));
            return data;
        }

        ShardBlob NewBlob(PluginOwner owner, void* data, uint64_t size)
        {
            std::lock_guard<std::mutex> lock(gBlobMutex);
            size_t index = gBlobs.size();
            for (size_t i = 0; i < gBlobs.size(); ++i) if (!gBlobs[i].used) { index = i; break; }
            if (index == gBlobs.size()) gBlobs.emplace_back();
            BlobSlot& slot = gBlobs[index];
            slot.owner = owner;
            slot.data = data;
            slot.size = size;
            slot.used = true;
            return (ShardBlob(slot.generation) << 32) | ShardBlob(index + 1);
        }

        // Takes the slot out of the table and gives its memory back (false : not one of `owner`'s)
        bool DropBlob(PluginOwner owner, ShardBlob blob, PluginContext& ctx)
        {
            void* data = nullptr;
            {
                std::lock_guard<std::mutex> lock(gBlobMutex);
                BlobSlot* slot = FindBlob(owner, blob);
                if (!slot) return false;
                data = slot->data;
                EmptySlot(*slot);
            }
            ctx.Memory().Free(data);
            return true;
        }

        ShardResult SHARD_CALL BlobCreate(ShardPluginCtx handle, uint64_t size, ShardBlob* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                void* data = AllocateBlob(*ctx, size);
                if (!data) return SHARD_E_OUT_OF_MEMORY;
                *out = NewBlob(PluginOwner(handle), data, size);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL BlobData(ShardPluginCtx handle, ShardBlob blob, void** data, uint64_t* size)
        {
            try {
                if (!data || !size) return SHARD_E_INVALID_ARG;
                std::lock_guard<std::mutex> lock(gBlobMutex);
                BlobSlot* slot = FindBlob(PluginOwner(handle), blob);
                if (!slot) return SHARD_E_STALE_HANDLE;
                *data = slot->data;
                *size = slot->size;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL BlobResize(ShardPluginCtx handle, ShardBlob blob, uint64_t size)
        {
            try {
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                void* old = nullptr;
                uint64_t oldSize = 0;
                {
                    std::lock_guard<std::mutex> lock(gBlobMutex);
                    BlobSlot* slot = FindBlob(PluginOwner(handle), blob);
                    if (!slot) return SHARD_E_STALE_HANDLE;
                    old = slot->data;
                    oldSize = slot->size;
                }
                void* data = AllocateBlob(*ctx, size);
                if (!data) return SHARD_E_OUT_OF_MEMORY;
                std::memcpy(data, old, size_t(std::min(size, oldSize)));
                {
                    std::lock_guard<std::mutex> lock(gBlobMutex);
                    BlobSlot* slot = FindBlob(PluginOwner(handle), blob);
                    if (!slot || slot->data != old) {       // freed or resized meanwhile
                        ctx->Memory().Free(data);
                        return SHARD_E_STALE_HANDLE;
                    }
                    slot->data = data;
                    slot->size = size;
                }
                ctx->Memory().Free(old);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL BlobFree(ShardPluginCtx handle, ShardBlob blob)
        {
            try {
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                return DropBlob(PluginOwner(handle), blob, *ctx) ? SHARD_OK : SHARD_E_STALE_HANDLE;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL StringSet(ShardPluginCtx handle, ShardBlob* field, ShardStr text)
        {
            try {
                if (!field || (text.size && !text.data)) return SHARD_E_INVALID_ARG;
                if (*field) {
                    const ShardResult resized = BlobResize(handle, *field, text.size);
                    if (resized == SHARD_OK) {
                        void* data = nullptr;
                        uint64_t size = 0;
                        const ShardResult found = BlobData(handle, *field, &data, &size);
                        if (found != SHARD_OK) return found;
                        if (text.size) std::memcpy(data, text.data, size_t(text.size));
                        return SHARD_OK;
                    }
                    if (resized != SHARD_E_STALE_HANDLE) return resized;
                    *field = 0;                     // not a live blob of this plugin : the field gets a new one
                }
                ShardBlob blob = 0;
                const ShardResult created = BlobCreate(handle, text.size, &blob);
                if (created != SHARD_OK) return created;
                void* data = nullptr;
                uint64_t size = 0;
                const ShardResult found = BlobData(handle, blob, &data, &size);
                if (found != SHARD_OK) { BlobFree(handle, blob); return found; }
                if (text.size) std::memcpy(data, text.data, size_t(text.size));
                *field = blob;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL StringGet(ShardPluginCtx handle, ShardBlob blob, ShardStr* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = ShardStr{ nullptr, 0 };
                if (blob == 0) return SHARD_OK;
                void* data = nullptr;
                uint64_t size = 0;
                const ShardResult found = BlobData(handle, blob, &data, &size);
                if (found != SHARD_OK) return found;
                *out = ShardStr{ static_cast<const char*>(data), size };
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // ---- The actor component ----

        class CAbiComponent final : public Objects::Components::Component {
        public:
            CAbiComponent(std::shared_ptr<Objects::Actor> parent, uint32_t localId, std::shared_ptr<const CType> type)
                : Component(std::move(parent), localId), m_Type(std::move(type))
            {
                std::memset(m_Storage, 0, sizeof(m_Storage));
                if (!m_Type->defaults.empty()) std::memcpy(m_Storage, m_Type->defaults.data(), m_Type->size);
            }

            static size_t StorageOffset()
            {
#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
                return offsetof(CAbiComponent, m_Storage);
#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic pop
#endif
            }

            /// Where the std::string of the n-th SHARD_FIELD_STRING field is in the object : the offset the reflection reads it at
            static size_t TextOffset(int n)
            {
#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
                return offsetof(CAbiComponent, m_Text) + size_t(n) * sizeof(std::string);
#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic pop
#endif
            }

            std::shared_ptr<Objects::Components::Component> Clone() const override
            {
                Pull();
                auto copy = Core::Object::Create<CAbiComponent>(nullptr, 0, m_Type);
                std::memcpy(copy->m_Storage, m_Storage, m_Type->size);
                // The copy owns copies of the blobs : what play mode does to them stays in play mode
                for (const CField* f : m_Type->blobs) copy->SetBlob(*f, CopyBlob(m_Type->owner, Blob(*f)));
                for (const CField* f : m_Type->texts) {
                    copy->SetBlob(*f, CopyBlob(m_Type->owner, Blob(*f)));
                    copy->m_Text[f->text] = m_Text[f->text];
                }
                copy->activated = activated;
                return copy;
            }

            const ClassDescriptor* GetDescriptor() const override { return &m_Type->descriptor; }

            void Deserialize(const json componentData) override
            {
                DeserializeReflectedFields(componentData, false);
                for (const CField* f : m_Type->blobs) {
                    auto it = componentData.find(f->name);
                    if (it == componentData.end() || !it->is_string()) continue;
                    std::string bytes;
                    if (!DecodeBase64(it->get<std::string>(), bytes)) continue;
                    FreeBlob(m_Type->owner, Blob(*f));
                    SetBlob(*f, BlobFromBytes(m_Type->owner, bytes));
                }
                DeserializeActive(componentData, true);         // Activate pushes the values read
            }

            ordered_json Serialize() override
            {
                Pull();             // what the systems changed is what is saved
                ordered_json out = SerializeReflected(m_Type->name);
                for (const CField* f : m_Type->blobs) {
                    std::string bytes;
                    if (BlobBytes(m_Type->owner, Blob(*f), bytes)) out[f->name] = EncodeBase64(bytes);
                }
                return out;
            }

            void OnFieldChanged(const FieldChangedEvent&) override { Push(); }
            void OnAttached() override { Push(); }
            void Activate() override { Component::Activate(); Push(); }
            void DeActivate() override { Pull(); Component::DeActivate(); Detach(); }
            void Destroy() override
            {
                Pull();
                Detach();
                for (const CField* f : m_Type->blobs) {
                    FreeBlob(m_Type->owner, Blob(*f));
                    SetBlob(*f, 0);
                }
                for (const CField* f : m_Type->texts) {
                    FreeBlob(m_Type->owner, Blob(*f));
                    SetBlob(*f, 0);
                }
                Component::Destroy();
            }

        private:
            ShardBlob Blob(const CField& f) const
            {
                ShardBlob blob = 0;
                std::memcpy(&blob, m_Storage + f.offset, sizeof(blob));
                return blob;
            }
            void SetBlob(const CField& f, ShardBlob blob) { std::memcpy(m_Storage + f.offset, &blob, sizeof(blob)); }

            Core::Ecs::Registry* Ecs() const
            {
                Core::IEngineContext* engine = GetEngineContext();
                return engine ? engine->GetEcs() : nullptr;
            }
            Core::Ecs::Entity EntityOf() const { return parent ? parent->GetEntity() : Core::Ecs::kNullEntity; }

            // The values of this component onto the actor's entity (added there if it has none yet, when that is allowed now)
            void Push()
            {
                if (!activated) return;
                Core::Ecs::Registry* ecs = Ecs();
                const Core::Ecs::Entity entity = EntityOf();
                if (!ecs || !ecs->IsAlive(entity)) return;
                PushTexts();
                if (void* at = ecs->GetRaw(entity, m_Type->ecs)) { std::memcpy(at, m_Storage, m_Type->size); return; }
                if (ecs->CanChangeStructure()) ecs->AddRaw(entity, m_Type->ecs, m_Storage);
            }

            // The text of each string field into the blob its handle designates (a new blob when it has none yet) : what systems read
            void PushTexts()
            {
                for (const CField* f : m_Type->texts) {
                    ShardBlob blob = Blob(*f);
                    std::string current;
                    if (blob && BlobBytes(m_Type->owner, blob, current) && current == m_Text[f->text]) continue;
                    StringSet(ShardPluginCtx(m_Type->owner), &blob, ToStr(m_Text[f->text]));
                    SetBlob(*f, blob);
                }
            }

            // What the systems wrote on the entity back into this component
            void Pull() const
            {
                Core::Ecs::Registry* ecs = Ecs();
                const Core::Ecs::Entity entity = EntityOf();
                if (!ecs || !ecs->IsAlive(entity)) return;
                const void* at = ecs->GetRaw(entity, m_Type->ecs);
                if (!at) return;
                std::memcpy(m_Storage, at, m_Type->size);
                // The text a system wrote into the blobs
                for (const CField* f : m_Type->texts) {
                    std::string bytes;
                    if (!BlobBytes(m_Type->owner, Blob(*f), bytes)) bytes.clear();
                    m_Text[f->text] = std::move(bytes);
                }
            }

            void Detach()
            {
                Core::Ecs::Registry* ecs = Ecs();
                const Core::Ecs::Entity entity = EntityOf();
                if (ecs && ecs->IsAlive(entity) && ecs->HasRaw(entity, m_Type->ecs) && ecs->CanChangeStructure()) ecs->RemoveRaw(entity, m_Type->ecs);
            }

            alignas(16) mutable unsigned char m_Storage[SHARD_COMPONENT_MAX_SIZE];
            mutable std::string m_Text[SHARD_COMPONENT_MAX_STRINGS];        // the string fields, as the inspector and the world file see them
            std::shared_ptr<const CType> m_Type;
        };

        // ---- The table ----

        ShardResult SHARD_CALL RegisterType(ShardPluginCtx handle, const ShardComponentDesc* desc, ShardComponentType* out)
        {
            try {
                if (!out || !desc) return SHARD_E_INVALID_ARG;
                *out = 0;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                auto refuse = [&](const std::string& why) { ctx->Error("component type : " + why); return SHARD_E_INVALID_ARG; };

                if (desc->structSize < sizeof(ShardComponentDesc)) return refuse("its ShardComponentDesc is too small");
                const std::string local = ToString(desc->name);
                if (!IsIdentifier(local, true)) return refuse("'" + local + "' is not a type name (letters, digits, '_' and '.')");
                const std::string full = ctx->Id() + "." + local;
                if (desc->size == 0 || desc->size > SHARD_COMPONENT_MAX_SIZE) return refuse("'" + full + "' : a size of " + std::to_string(desc->size) + " bytes (1 to " + std::to_string(SHARD_COMPONENT_MAX_SIZE) + ")");
                if (desc->align == 0 || desc->align > 16 || (desc->align & (desc->align - 1)) != 0) return refuse("'" + full + "' : an alignment of " + std::to_string(desc->align));
                if (desc->fieldCount > SHARD_COMPONENT_MAX_FIELDS) return refuse("'" + full + "' has more than " + std::to_string(SHARD_COMPONENT_MAX_FIELDS) + " fields");
                if (desc->fieldCount && !desc->fields) return refuse("'" + full + "' : fieldCount without fields");

                auto type = std::make_shared<CType>();
                type->name = full;
                type->owner = PluginOwner(handle);
                type->size = desc->size;
                type->align = desc->align;
                if (desc->defaults) type->defaults.assign(static_cast<const unsigned char*>(desc->defaults), static_cast<const unsigned char*>(desc->defaults) + desc->size);

                std::string why;
                if (!BuildFieldSet(*type, full, desc->fields, desc->fieldCount, desc->size, CAbiComponent::StorageOffset(), CAbiComponent::TextOffset(0), false, why)) return refuse(why);
                // A default blob would be shared by every new component : a blob field starts empty
                if (!type->defaults.empty()) ZeroHandleFields(*type, type->defaults.data());

                {
                    std::lock_guard<std::mutex> lock(gTypesMutex);
                    if (gTypes.count(full)) return SHARD_E_ALREADY_EXISTS;
                }
                // The ECS id is the process's : the same name gets the same id back, which only works for the same layout
                const std::string ecsName = "c:" + full;
                type->ecs = Core::Ecs::ComponentRegistry::Global().Register(ecsName.c_str(), desc->size, desc->align);
                const Core::Ecs::ComponentInfo& ecsInfo = Core::Ecs::ComponentRegistry::Global().Info(type->ecs);
                if (ecsInfo.size != desc->size || ecsInfo.align != desc->align) {
                    ctx->Error("component type '" + full + "' was registered earlier in this run with " + std::to_string(ecsInfo.size) + " bytes : restart the engine to change its layout");
                    return SHARD_E_ALREADY_EXISTS;
                }

                std::shared_ptr<const CType> shared = type;
                if (!ctx->Components().Register(full, Objects::Components::ComponentFactory([shared]() -> std::shared_ptr<Objects::Components::Component> {
                        return Core::Object::Create<CAbiComponent>(nullptr, 0, shared);
                    })))
                    return SHARD_E_ALREADY_EXISTS;
                {
                    std::lock_guard<std::mutex> lock(gTypesMutex);
                    gTypes[full] = type;
                }
                *out = Handle(type->ecs);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL FindType(ShardPluginCtx, ShardStr name, ShardComponentType* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                const std::string wanted = ToString(name);
                if (wanted == "shard.LocalTransform") { *out = Handle(Core::Ecs::ComponentIdOf<Worlds::LocalTransform>()); return SHARD_OK; }
                if (wanted == "shard.ActorLink") { *out = Handle(Core::Ecs::ComponentIdOf<Worlds::ActorLink>()); return SHARD_OK; }
                if (wanted == "shard.Disabled") { *out = Handle(Core::Ecs::ComponentIdOf<Worlds::Disabled>()); return SHARD_OK; }
                std::lock_guard<std::mutex> lock(gTypesMutex);
                auto it = gTypes.find(wanted);
                if (it == gTypes.end()) return SHARD_E_NOT_FOUND;
                *out = Handle(it->second->ecs);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL TypeLayout(ShardPluginCtx, ShardComponentType type, uint32_t* size, uint32_t* align)
        {
            try {
                Core::Ecs::ComponentId id;
                if (!EcsIdOf(type, id)) return SHARD_E_INVALID_ARG;
                const Core::Ecs::ComponentInfo& info = Core::Ecs::ComponentRegistry::Global().Info(id);
                if (size) *size = uint32_t(info.size);
                if (align) *align = uint32_t(info.align);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // Fills what fits of the caller's struct (it set out->structSize)
        ShardResult DescribeField(const CField& field, ShardFieldDesc* out)
        {
            if (!out || out->structSize < sizeof(uint32_t)) return SHARD_E_INVALID_ARG;
            ShardFieldDesc full;
            std::memset(&full, 0, sizeof(full));
            full.structSize = std::min<uint32_t>(out->structSize, sizeof(ShardFieldDesc));
            full.type = field.type;
            full.name = ToStr(field.name);
            full.offset = field.offset;
            full.flags = field.flags;
            full.min = field.min;
            full.max = field.max;
            full.tooltip = ToStr(field.tooltip);
            std::memcpy(out, &full, full.structSize);
            return SHARD_OK;
        }

        ShardResult SHARD_CALL FieldCount(ShardPluginCtx, ShardComponentType type, uint32_t* count)
        {
            try {
                if (!count) return SHARD_E_INVALID_ARG;
                std::shared_ptr<CType> t = TypeOf(type);
                if (!t) return SHARD_E_NOT_FOUND;
                *count = uint32_t(t->fields.size());
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL FieldAt(ShardPluginCtx, ShardComponentType type, uint32_t index, ShardFieldDesc* out)
        {
            try {
                std::shared_ptr<CType> t = TypeOf(type);
                if (!t || index >= t->fields.size()) return SHARD_E_NOT_FOUND;
                return DescribeField(t->fields[index], out);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL FindField(ShardPluginCtx, ShardComponentType type, ShardStr name, ShardFieldDesc* out)
        {
            try {
                std::shared_ptr<CType> t = TypeOf(type);
                if (!t) return SHARD_E_NOT_FOUND;
                const std::string wanted = ToString(name);
                for (const CField& f : t->fields)
                    if (f.name == wanted) return DescribeField(f, out);
                return SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    bool FieldLayoutOf(uint32_t type, uint32_t& size, uint32_t& align)
    {
        FieldLayout layout;
        if (!LayoutOf(type, layout)) return false;
        size = layout.size;
        align = layout.align;
        return true;
    }

    bool IsFieldName(const std::string& name)
    {
        return IsIdentifier(name, false) && name != "type" && name != "active" && name != "tags";
    }

    const void* ComponentsApi()
    {
        static const ShardComponentsApi api = { sizeof(ShardComponentsApi), &RegisterType, &FindType, &TypeLayout, &FieldCount, &FieldAt, &FindField,
                                                &BlobCreate, &BlobData, &BlobResize, &BlobFree, &StringSet, &StringGet };
        return &api;
    }

    ShardBlob CopyBlob(PluginOwner owner, ShardBlob blob)
    {
        std::string bytes;
        return BlobBytes(owner, blob, bytes) ? BlobFromBytes(owner, bytes) : 0;
    }

    void FreeBlob(PluginOwner owner, ShardBlob blob)
    {
        if (!blob) return;
        if (PluginContext* ctx = FindContext(owner)) DropBlob(owner, blob, *ctx);
    }

    bool BlobBytes(PluginOwner owner, ShardBlob blob, std::string& out)
    {
        std::lock_guard<std::mutex> lock(gBlobMutex);
        BlobSlot* slot = FindBlob(owner, blob);
        if (!slot) return false;
        out.assign(static_cast<const char*>(slot->data), size_t(slot->size));
        return true;
    }

    ShardBlob BlobFromBytes(PluginOwner owner, const std::string& bytes)
    {
        PluginContext* ctx = FindContext(owner);
        if (!ctx) return 0;
        void* data = AllocateBlob(*ctx, bytes.size());
        if (!data) return 0;
        if (!bytes.empty()) std::memcpy(data, bytes.data(), bytes.size());
        return NewBlob(owner, data, bytes.size());
    }

    void ReleaseBlobs(PluginOwner owner, PluginContext& ctx)
    {
        std::vector<void*> memory;
        {
            std::lock_guard<std::mutex> lock(gBlobMutex);
            for (BlobSlot& slot : gBlobs) {
                if (!slot.used || slot.owner != owner) continue;
                memory.push_back(slot.data);
                EmptySlot(slot);
            }
        }
        for (void* p : memory) ctx.Memory().Free(p);
    }

    void ReleaseComponents(PluginOwner owner)
    {
        std::lock_guard<std::mutex> lock(gTypesMutex);
        for (auto it = gTypes.begin(); it != gTypes.end();) {
            if (it->second->owner == owner) it = gTypes.erase(it);
            else ++it;
        }
    }

    namespace {
        ShardBlob HandleAt(const unsigned char* storage, const CField& f)
        {
            ShardBlob blob = 0;
            std::memcpy(&blob, storage + f.offset, sizeof(blob));
            return blob;
        }
        void SetHandleAt(unsigned char* storage, const CField& f, ShardBlob blob) { std::memcpy(storage + f.offset, &blob, sizeof(blob)); }
    }

    bool BuildFieldSet(FieldSet& out, const std::string& full, const ShardFieldDesc* fields, uint32_t count, uint32_t size, size_t storageBase, size_t textBase, bool script, std::string& why)
    {
        auto refuse = [&](const std::string& what) { why = what; return false; };
        out.descriptor.name = full;
        std::vector<std::pair<uint32_t, uint32_t>> spans;      // [offset, end) of every field
        for (uint32_t i = 0; i < count; ++i) {
            const ShardFieldDesc& f = fields[i];
            if (f.structSize < sizeof(ShardFieldDesc)) return refuse("'" + full + "' : field " + std::to_string(i) + " has a ShardFieldDesc too small");
            CField field;
            field.name = ToString(f.name);
            if (!IsIdentifier(field.name, false)) return refuse("'" + full + "' : '" + field.name + "' is not a field name");
            if (field.name == "type" || field.name == "active" || field.name == "tags") return refuse("'" + full + "' : the field name '" + field.name + "' is reserved");
            for (const CField& other : out.fields)
                if (other.name == field.name) return refuse("'" + full + "' : the field '" + field.name + "' is described twice");
            FieldLayout layout;
            if (!LayoutOf(f.type, layout)) return refuse("'" + full + "' : the field '" + field.name + "' has an unknown type (" + std::to_string(f.type) + ")");
            if (f.type == SHARD_FIELD_STRING || f.type == SHARD_FIELD_ACTOR) {
                if (f.flags & SHARD_FIELD_PRIVATE) return refuse("'" + full + "' : the string or actor field '" + field.name + "' cannot be private (use a blob)");
                if (out.texts.size() >= SHARD_COMPONENT_MAX_STRINGS) return refuse("'" + full + "' has more than " + std::to_string(SHARD_COMPONENT_MAX_STRINGS) + " string and actor fields");
                field.text = int(out.texts.size());
            }
            if (f.type == SHARD_FIELD_ACTOR && !script) return refuse("'" + full + "' : the field '" + field.name + "' is an actor reference, which only a script can have");
            if ((f.flags & SHARD_FIELD_ACCESSOR) && (f.type != SHARD_FIELD_STRING || !script)) return refuse("'" + full + "' : the field '" + field.name + "' has SHARD_FIELD_ACCESSOR, for a string of a script only");
            field.accessor = (f.flags & SHARD_FIELD_ACCESSOR) != 0;
            field.actor = f.type == SHARD_FIELD_ACTOR;
            if (uint64_t(f.offset) + layout.size > size) return refuse("'" + full + "' : the field '" + field.name + "' goes past the end of the block");
            if (!field.accessor && f.offset % layout.align != 0) return refuse("'" + full + "' : the field '" + field.name + "' is not aligned");
            for (const auto& span : spans)
                if (f.offset < span.second && span.first < f.offset + layout.size) return refuse("'" + full + "' : the field '" + field.name + "' overlaps another");
            spans.emplace_back(f.offset, f.offset + layout.size);
            field.tooltip = ToString(f.tooltip);
            field.type = f.type;
            field.offset = f.offset;
            field.size = layout.size;
            field.flags = f.flags;
            field.min = f.min;
            field.max = f.max;
            out.fields.push_back(std::move(field));
            const CField& kept = out.fields.back();

            if (kept.type == SHARD_FIELD_BLOB) { out.blobs.push_back(&kept); continue; }
            if (kept.type == SHARD_FIELD_STRING || kept.type == SHARD_FIELD_ACTOR) {
                // The storage holds the blob (or the handle of the actor, or the plugin's own string) ; the inspector and the world file work
                // on the std::string beside it
                out.texts.push_back(&kept);
                const FieldLayout text = Layout<std::string>(TypeID::String);
                FieldInfo info{};
                info.name = kept.name.c_str();
                info.type = text.type;
                info.offset = uint32_t(textBase + size_t(kept.text) * sizeof(std::string));
                info.flags = (kept.flags & SHARD_FIELD_RUNTIME) ? uint32_t(ReadOnly) : uint32_t(Editable);
                info.tooltip = kept.tooltip.empty() ? nullptr : kept.tooltip.c_str();
                info.CopyConstruct = text.copy;
                info.Assign = text.assign;
                info.Destroy = text.destroy;
                info.Equals = text.equals;
                out.infos.push_back(info);
                continue;
            }
            if (kept.flags & SHARD_FIELD_PRIVATE) continue;
            FieldInfo info{};
            info.name = kept.name.c_str();
            info.type = layout.type;
            info.offset = uint32_t(storageBase + kept.offset);
            info.flags = (kept.flags & SHARD_FIELD_RUNTIME) ? uint32_t(ReadOnly) : uint32_t(Editable);
            info.min = kept.min;
            info.max = kept.max;
            info.tooltip = kept.tooltip.empty() ? nullptr : kept.tooltip.c_str();
            info.CopyConstruct = layout.copy;
            info.Assign = layout.assign;
            info.Destroy = layout.destroy;
            info.Equals = layout.equals;
            out.infos.push_back(info);
        }
        for (FieldInfo& info : out.infos) out.descriptor.fields.push_back(&info);
        return true;
    }

    void ZeroHandleFields(const FieldSet& set, unsigned char* storage)
    {
        for (const CField* f : set.blobs) SetHandleAt(storage, *f, 0);
        for (const CField* f : set.texts) if (!f->accessor) SetHandleAt(storage, *f, 0);      // an accessor string is the plugin's memory, not a handle
    }

    void PullTexts(const FieldSet& set, PluginOwner owner, const unsigned char* storage, std::string* texts, FieldHooks* hooks)
    {
        for (const CField* f : set.texts) {
            if (f->accessor) {
                if (!hooks || !hooks->textGet) continue;
                ShardStr text{ nullptr, 0 };
                hooks->textGet(hooks->user, hooks->object, f->offset, &text);
                texts[f->text] = ToString(text);
            } else if (f->actor) {
                const ShardActor handle = HandleAt(storage, *f);
                if (handle && hooks && hooks->actorPath) texts[f->text] = hooks->actorPath(handle);
                else if (!handle && !(hooks && hooks->pending[f->text])) texts[f->text].clear();      // a path still waiting for its actor is kept
            } else {
                std::string bytes;
                if (!BlobBytes(owner, HandleAt(storage, *f), bytes)) bytes.clear();
                texts[f->text] = std::move(bytes);
            }
        }
    }

    void PushTexts(const FieldSet& set, PluginOwner owner, unsigned char* storage, const std::string* texts, FieldHooks* hooks)
    {
        for (const CField* f : set.texts) {
            if (f->accessor) {
                if (hooks && hooks->textSet) hooks->textSet(hooks->user, hooks->object, f->offset, ToStr(texts[f->text]));
            } else if (f->actor) {
                ShardActor handle = 0;
                if (!texts[f->text].empty() && hooks && hooks->actorAt) handle = hooks->actorAt(texts[f->text]);
                if (hooks) hooks->pending[f->text] = handle == 0 && !texts[f->text].empty();
                SetHandleAt(storage, *f, handle);
            } else {
                ShardBlob blob = HandleAt(storage, *f);
                std::string current;
                if (blob && BlobBytes(owner, blob, current) && current == texts[f->text]) continue;
                StringSet(ShardPluginCtx(owner), &blob, ToStr(texts[f->text]));
                SetHandleAt(storage, *f, blob);
            }
        }
    }

    void ResolveActorFields(const FieldSet& set, unsigned char* storage, const std::string* texts, FieldHooks& hooks)
    {
        if (!hooks.actorAt) return;
        for (const CField* f : set.texts) {
            if (!f->actor || !hooks.pending[f->text]) continue;
            const ShardActor handle = hooks.actorAt(texts[f->text]);
            if (!handle) continue;
            SetHandleAt(storage, *f, handle);
            hooks.pending[f->text] = false;
        }
    }

    void SerializeBlobs(const FieldSet& set, PluginOwner owner, const unsigned char* storage, ordered_json& out)
    {
        for (const CField* f : set.blobs) {
            std::string bytes;
            if (BlobBytes(owner, HandleAt(storage, *f), bytes)) out[f->name] = EncodeBase64(bytes);
        }
    }

    void DeserializeBlobs(const FieldSet& set, PluginOwner owner, unsigned char* storage, const json& in)
    {
        for (const CField* f : set.blobs) {
            auto it = in.find(f->name);
            if (it == in.end() || !it->is_string()) continue;
            std::string bytes;
            if (!DecodeBase64(it->get<std::string>(), bytes)) continue;
            FreeBlob(owner, HandleAt(storage, *f));
            SetHandleAt(storage, *f, BlobFromBytes(owner, bytes));
        }
    }

    void FreeFieldBlobs(const FieldSet& set, PluginOwner owner, unsigned char* storage)
    {
        for (const CField* f : set.blobs) { FreeBlob(owner, HandleAt(storage, *f)); SetHandleAt(storage, *f, 0); }
        for (const CField* f : set.texts) {
            if (f->accessor) continue;                          // the plugin's string : its destructor frees it
            if (!f->actor) FreeBlob(owner, HandleAt(storage, *f));
            SetHandleAt(storage, *f, 0);
        }
    }

    void CopyFields(const FieldSet& set, PluginOwner owner, unsigned char* to, std::string* toTexts, const unsigned char* from, const std::string* fromTexts)
    {
        for (const CField& f : set.fields) {
            if (f.accessor || f.actor) {
                toTexts[f.text] = fromTexts[f.text];            // the caller pushes it : an accessor into the new object, an actor reference once the copy is in a world
            } else if (f.type == SHARD_FIELD_BLOB || f.type == SHARD_FIELD_STRING) {
                SetHandleAt(to, f, CopyBlob(owner, HandleAt(from, f)));
                if (f.type == SHARD_FIELD_STRING) toTexts[f.text] = fromTexts[f.text];
            } else {
                std::memcpy(to + f.offset, from + f.offset, f.size);
            }
        }
    }

}
