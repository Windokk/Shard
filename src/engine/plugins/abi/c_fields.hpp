#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <shard/shard_components.h>

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/plugins/abi/c_internal.hpp"

namespace Shard::Engine::Plugins::CAbi {

    struct CField {
        std::string name;
        std::string tooltip;
        uint32_t type = 0;
        uint32_t offset = 0;
        uint32_t size = 0;
        uint32_t flags = 0;
        float min = 0.0f;
        float max = 0.0f;
        int text = -1;
        bool accessor = false;
        bool actor = false;
    };

    // `descriptor` points into `infos`, `texts` and `blobs` into `fields`: built once, never moved.
    struct FieldSet {
        std::deque<CField> fields;
        ClassDescriptor descriptor;
        std::deque<FieldInfo> infos;
        std::vector<const CField*> blobs;
        std::vector<const CField*> texts;
    };

    struct FieldHooks {
        void* user = nullptr;
        void* object = nullptr;
        void (*textGet)(void* user, void* object, uint32_t offset, ShardStr* out) = nullptr;
        ShardResult (*textSet)(void* user, void* object, uint32_t offset, ShardStr text) = nullptr;
        std::function<std::string(ShardActor)> actorPath;
        std::function<ShardActor(const std::string&)> actorAt;
        bool pending[SHARD_COMPONENT_MAX_STRINGS] = {};
    };

    bool BuildFieldSet(FieldSet& out, const std::string& full, const ShardFieldDesc* fields, uint32_t count, uint32_t size, size_t storageBase, size_t textBase, bool script, std::string& why);

    void ZeroHandleFields(const FieldSet& set, unsigned char* storage);

    void PullTexts(const FieldSet& set, PluginOwner owner, const unsigned char* storage, std::string* texts, FieldHooks* hooks = nullptr);
    void PushTexts(const FieldSet& set, PluginOwner owner, unsigned char* storage, const std::string* texts, FieldHooks* hooks = nullptr);
    void ResolveActorFields(const FieldSet& set, unsigned char* storage, const std::string* texts, FieldHooks& hooks);
    void SerializeBlobs(const FieldSet& set, PluginOwner owner, const unsigned char* storage, nlohmann::ordered_json& out);
    void DeserializeBlobs(const FieldSet& set, PluginOwner owner, unsigned char* storage, const nlohmann::json& in);
    void FreeFieldBlobs(const FieldSet& set, PluginOwner owner, unsigned char* storage);
    void CopyFields(const FieldSet& set, PluginOwner owner, unsigned char* to, std::string* toTexts, const unsigned char* from, const std::string* fromTexts);
}
