#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/core/collision/collision_types.hpp"
#include "engine/core/tags/tag.hpp"
#include "engine/core/tags/tag_container.hpp"
#include "engine/core/tags/tag_query.hpp"

namespace Shard::Engine::Collision {

    struct Rule {
        Core::TagQuery target;
        Response response = Response::Ignore;
    };

    enum class Combine : uint8_t { Average = 0, Min = 1, Max = 2, Multiply = 3 };

    struct PhysicsMaterial {
        std::string name;
        float friction = 0.2f;
        float restitution = 0.0f;
        float density = 1000.0f;
        Combine combine = Combine::Average;
        std::string surface;
    };

    struct EventOptions {
        bool overlaps = true;
        bool hits = true;
        float minImpactSpeed = 0.0f;

        friend bool operator==(const EventOptions& a, const EventOptions& b)
        {
            return a.overlaps == b.overlaps && a.hits == b.hits && a.minImpactSpeed == b.minImpactSpeed;
        }
    };

    struct Profile {
        std::string name;
        std::string base;
        Core::TagContainer identity;
        std::vector<Rule> rules;
        std::optional<Response> defaultResponse;
        std::optional<Mode> mode;
        std::string material;
        std::optional<EventOptions> events;
    };

    struct ResolvedProfile {
        std::string name;
        std::vector<std::string> chain;
        Core::TagContainer identity;
        std::vector<Rule> rules;
        Response defaultResponse = Response::Ignore;
        Mode mode = Mode::Solid;
        std::string material;
        EventOptions events;
    };

    enum class Source : uint8_t {
        Project = 0,
        Plugin = 1,
        Code = 2,
    };

    constexpr const char* kDefaultStaticProfile = "Default.Static";
    constexpr const char* kDefaultMovingProfile = "Default.Moving";

    const char* ToString(Combine value);
    bool TryParse(std::string_view text, Combine& out);
    float CombineValues(Combine mode, float a, float b);

    class ProfileRegistry {
    public:
        static ProfileRegistry& Get();

        static bool IsValidName(std::string_view name, std::string* error = nullptr);

        void SetDeclarations(Source source, std::string_view owner, std::vector<Profile> profiles);
        void RemoveDeclarations(Source source, std::string_view owner);
        void Declare(Source source, std::string_view owner, Profile profile);

        void SetCollisionRoots(Source source, std::string_view owner, std::vector<std::string> roots);
        bool IsCollisionTag(Core::Tag tag) const;
        std::vector<Core::Tag> CollisionRoots() const;

        void SetMaterials(Source source, std::string_view owner, std::vector<PhysicsMaterial> materials);
        const PhysicsMaterial* FindMaterial(std::string_view name) const;
        std::vector<std::string> MaterialNames() const;

        const Profile* Find(std::string_view name) const;
        bool Exists(std::string_view name) const { return Find(name) != nullptr; }
        bool Resolve(std::string_view name, ResolvedProfile& out, std::string* error = nullptr) const;
        std::vector<std::string> Names() const;
        std::vector<const Profile*> All() const;
        std::string OwnerOf(std::string_view name) const;

        uint64_t GetVersion() const { return m_Version; }

        void Clear();

    private:
        struct Entry {
            Source source = Source::Project;
            std::string owner;
            Profile profile;
        };
        struct MaterialEntry {
            Source source = Source::Project;
            std::string owner;
            PhysicsMaterial material;
        };
        struct Roots {
            Source source = Source::Project;
            std::string owner;
            std::vector<Core::Tag> tags;
        };

        ProfileRegistry();
        void InstallBuiltins();
        void EnsureBuiltinTags() const;
        bool ResolveInto(const Profile& profile, ResolvedProfile& out, std::vector<std::string>& visiting, std::string* error) const;

        std::vector<Entry> m_Entries;
        std::vector<Roots> m_Roots;
        std::vector<MaterialEntry> m_Materials;
        uint64_t m_Version = 1;
        mutable uint64_t m_TagVersion = 0;
        mutable const void* m_TagRegistry = nullptr;
    };

    Core::TagContainer FilterCollisionTags(const Core::TagContainer& tags);
}
