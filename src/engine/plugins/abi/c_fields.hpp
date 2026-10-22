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

// The fields a C plugin describes (ShardFieldDesc) for a component (c_components.cpp) or a script (c_scripts.cpp): checked once, turned into
// the reflection the inspector and the world files use, and the helpers that keep the text, blob and asset fields coherent. Not an API.

namespace Shard::Engine::Plugins::CAbi {

    struct CField {
        std::string name;
        std::string tooltip;
        uint32_t type = 0;
        uint32_t offset = 0;
        uint32_t size = 0;                      // bytes in the storage
        uint32_t flags = 0;
        float min = 0.0f;
        float max = 0.0f;
        int text = -1;                          // SHARD_FIELD_STRING and SHARD_FIELD_ACTOR : its place among the strings
        bool accessor = false;                  // SHARD_FIELD_STRING read and written through the script's callbacks
        bool actor = false;                     // SHARD_FIELD_ACTOR : a ShardActor saved by the path of the actor
    };

    /// What the fields of one type are. `descriptor` holds pointers into `infos`, `texts` and `blobs` into `fields` : it is built once and
    /// never moved.
    struct FieldSet {
        std::deque<CField> fields;              // stable : the ShardFieldDesc handed out point into them
        ClassDescriptor descriptor;             // the fields that are shown or saved
        std::deque<FieldInfo> infos;
        std::vector<const CField*> blobs;       // SHARD_FIELD_BLOB : the object owns their blobs
        std::vector<const CField*> texts;       // SHARD_FIELD_STRING (a blob, or an accessor) and SHARD_FIELD_ACTOR : mirrored in std::strings the reflection reads
    };

    /// What a script gives the field helpers to read and write what lives in its object and in the world : its own strings (accessor
    /// fields) and the actors its fields refer to. Null for a component, which has neither.
    struct FieldHooks {
        void* user = nullptr;
        void* object = nullptr;
        void (*textGet)(void* user, void* object, uint32_t offset, ShardStr* out) = nullptr;
        ShardResult (*textSet)(void* user, void* object, uint32_t offset, ShardStr text) = nullptr;
        std::function<std::string(ShardActor)> actorPath;           ///< the path of an actor in its world ("" : not an actor any more)
        std::function<ShardActor(const std::string&)> actorAt;      ///< the actor at a path in the world of the script (0 : none yet)
        bool pending[SHARD_COMPONENT_MAX_STRINGS] = {};             ///< an actor field whose path is known but whose actor was not found yet
    };

    /// Checks `fields` against a storage of `size` bytes and builds the FieldSet. `storageBase` is the offset of the storage in the object that
    /// the reflection reads from, `textBase` that of its array of SHARD_COMPONENT_MAX_STRINGS std::string.
    /// @return false with the reason in `why` (already prefixed by the type's name)
    /// `script` : the fields may be actor references and accessor strings, which only a script can serve.
    bool BuildFieldSet(FieldSet& out, const std::string& full, const ShardFieldDesc* fields, uint32_t count, uint32_t size, size_t storageBase, size_t textBase, bool script, std::string& why);

    /// The handles of the blob and string fields of a block of defaults are zeroed (a default blob would be shared by every new object)
    void ZeroHandleFields(const FieldSet& set, unsigned char* storage);

    /// The text each string field's blob holds, into the std::strings (after the plugin changed them)
    void PullTexts(const FieldSet& set, PluginOwner owner, const unsigned char* storage, std::string* texts, FieldHooks* hooks = nullptr);
    /// The std::strings into the blobs of the string fields (a new blob where there is none), the accessor strings and the actor references
    void PushTexts(const FieldSet& set, PluginOwner owner, unsigned char* storage, const std::string* texts, FieldHooks* hooks = nullptr);
    /// Finds the actors of the references whose path was known before the actor was (a world that is still loading)
    void ResolveActorFields(const FieldSet& set, unsigned char* storage, const std::string* texts, FieldHooks& hooks);
    /// The blob fields as base64 under their name, and back
    void SerializeBlobs(const FieldSet& set, PluginOwner owner, const unsigned char* storage, nlohmann::ordered_json& out);
    void DeserializeBlobs(const FieldSet& set, PluginOwner owner, unsigned char* storage, const nlohmann::json& in);
    /// Gives the blobs of the blob and string fields back, and clears the handles
    void FreeFieldBlobs(const FieldSet& set, PluginOwner owner, unsigned char* storage);
    /// Copies the fields of `from` into `to` (a fresh object): plain fields byte for byte, blobs and texts as copies of their own
    void CopyFields(const FieldSet& set, PluginOwner owner, unsigned char* to, std::string* toTexts, const unsigned char* from, const std::string* fromTexts);
}
