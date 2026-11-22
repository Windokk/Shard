#include "collision_profile.hpp"

#include <algorithm>

#include "engine/core/tags/tag_registry.hpp"

namespace Shard::Engine::Collision {

    namespace {
        int SourceRank(Source s)
        {
            switch (s) {
                case Source::Project: return 0;
                case Source::Plugin: return 1;
                case Source::Code: return 2;
            }
            return 3;
        }
    }

    const char* ToString(Combine value)
    {
        switch (value) {
            case Combine::Average: return "Average";
            case Combine::Min: return "Min";
            case Combine::Max: return "Max";
            case Combine::Multiply: return "Multiply";
        }
        return "Average";
    }

    bool TryParse(std::string_view text, Combine& out)
    {
        for (Combine c : { Combine::Average, Combine::Min, Combine::Max, Combine::Multiply })
            if (text == ToString(c)) { out = c; return true; }
        return false;
    }

    float CombineValues(Combine mode, float a, float b)
    {
        switch (mode) {
            case Combine::Average: return 0.5f * (a + b);
            case Combine::Min: return a < b ? a : b;
            case Combine::Max: return a < b ? b : a;
            case Combine::Multiply: return a * b;
        }
        return 0.5f * (a + b);
    }

    ProfileRegistry& ProfileRegistry::Get()
    {
        static ProfileRegistry instance;
        return instance;
    }

    ProfileRegistry::ProfileRegistry()
    {
        InstallBuiltins();
    }

    void ProfileRegistry::InstallBuiltins()
    {
        EnsureBuiltinTags();
        const Core::Tag staticTag = Core::Tag::FromId(Core::StringId("Collision.Static"));
        const Core::Tag movingTag = Core::Tag::FromId(Core::StringId("Collision.Moving"));

        Roots roots;
        roots.source = Source::Code;
        roots.owner = "builtin";
        roots.tags = { Core::Tag::FromId(Core::StringId("Collision")) };
        m_Roots.push_back(std::move(roots));

        Profile fixed;
        fixed.name = kDefaultStaticProfile;
        fixed.identity = Core::TagContainer({ staticTag });
        fixed.rules.push_back({ Core::TagQuery::Has(movingTag), Response::Block });
        fixed.defaultResponse = Response::Ignore;
        fixed.mode = Mode::Solid;
        m_Entries.push_back({ Source::Code, "builtin", std::move(fixed) });

        Profile moving;
        moving.name = kDefaultMovingProfile;
        moving.identity = Core::TagContainer({ movingTag });
        moving.rules.push_back({ Core::TagQuery::Has(movingTag).Or(Core::TagQuery::Has(staticTag)), Response::Block });
        moving.defaultResponse = Response::Ignore;
        moving.mode = Mode::Solid;
        m_Entries.push_back({ Source::Code, "builtin", std::move(moving) });
        ++m_Version;
    }

    void ProfileRegistry::EnsureBuiltinTags() const
    {
        Core::TagRegistry& tags = Core::TagRegistry::Get();
        if (tags.GetVersion() == m_TagVersion && m_TagRegistry == &tags)
            return;
        if (!tags.IsKnown(Core::Tag::FromId(Core::StringId("Collision.Static")))) {
            tags.Declare("Collision.Static", Core::TagSource::Code, "collision");
            tags.Declare("Collision.Moving", Core::TagSource::Code, "collision");
        }
        m_TagVersion = tags.GetVersion();
        m_TagRegistry = &tags;
    }

    bool ProfileRegistry::IsValidName(std::string_view name, std::string* error)
    {
        if (name.empty()) {
            if (error) *error = "a profile name is empty";
            return false;
        }
        for (char c : name) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == ' ' || c == '-';
            if (!ok) {
                if (error) *error = "'" + std::string(name) + "' : a profile name is made of letters, digits, '_', '-', '.' and spaces";
                return false;
            }
        }
        return true;
    }

    void ProfileRegistry::SetDeclarations(Source source, std::string_view owner, std::vector<Profile> profiles)
    {
        RemoveDeclarations(source, owner);
        for (Profile& profile : profiles)
            m_Entries.push_back({ source, std::string(owner), std::move(profile) });
        ++m_Version;
    }

    void ProfileRegistry::RemoveDeclarations(Source source, std::string_view owner)
    {
        const size_t before = m_Entries.size() + m_Roots.size() + m_Materials.size();
        m_Materials.erase(std::remove_if(m_Materials.begin(), m_Materials.end(),
            [&](const MaterialEntry& e) { return e.source == source && e.owner == owner; }), m_Materials.end());
        m_Entries.erase(std::remove_if(m_Entries.begin(), m_Entries.end(),
            [&](const Entry& e) { return e.source == source && e.owner == owner; }), m_Entries.end());
        m_Roots.erase(std::remove_if(m_Roots.begin(), m_Roots.end(),
            [&](const Roots& r) { return r.source == source && r.owner == owner; }), m_Roots.end());
        if (before != m_Entries.size() + m_Roots.size() + m_Materials.size())
            ++m_Version;
    }

    void ProfileRegistry::SetMaterials(Source source, std::string_view owner, std::vector<PhysicsMaterial> materials)
    {
        m_Materials.erase(std::remove_if(m_Materials.begin(), m_Materials.end(),
            [&](const MaterialEntry& e) { return e.source == source && e.owner == owner; }), m_Materials.end());
        for (PhysicsMaterial& material : materials)
            m_Materials.push_back({ source, std::string(owner), std::move(material) });
        ++m_Version;
    }

    const PhysicsMaterial* ProfileRegistry::FindMaterial(std::string_view name) const
    {
        const MaterialEntry* best = nullptr;
        for (const MaterialEntry& e : m_Materials) {
            if (e.material.name != name)
                continue;
            if (!best || SourceRank(e.source) < SourceRank(best->source))
                best = &e;
        }
        return best ? &best->material : nullptr;
    }

    std::vector<std::string> ProfileRegistry::MaterialNames() const
    {
        std::vector<std::string> out;
        for (const MaterialEntry& e : m_Materials)
            if (std::find(out.begin(), out.end(), e.material.name) == out.end())
                out.push_back(e.material.name);
        std::sort(out.begin(), out.end());
        return out;
    }

    void ProfileRegistry::Declare(Source source, std::string_view owner, Profile profile)
    {
        m_Entries.push_back({ source, std::string(owner), std::move(profile) });
        ++m_Version;
    }

    void ProfileRegistry::SetCollisionRoots(Source source, std::string_view owner, std::vector<std::string> roots)
    {
        m_Roots.erase(std::remove_if(m_Roots.begin(), m_Roots.end(),
            [&](const Roots& r) { return r.source == source && r.owner == owner; }), m_Roots.end());
        Roots entry;
        entry.source = source;
        entry.owner = owner;
        for (const std::string& name : roots) {
            Core::TagRegistry::Get().Declare(name, Core::TagSource::Project, owner);
            const Core::Tag tag = Core::Tag::Request(name);
            if (tag.IsValid())
                entry.tags.push_back(tag);
        }
        m_Roots.push_back(std::move(entry));
        ++m_Version;
    }

    bool ProfileRegistry::IsCollisionTag(Core::Tag tag) const
    {
        EnsureBuiltinTags();
        if (!tag.IsValid())
            return false;
        for (const Roots& r : m_Roots)
            for (Core::Tag root : r.tags)
                if (tag.Matches(root))
                    return true;
        return false;
    }

    std::vector<Core::Tag> ProfileRegistry::CollisionRoots() const
    {
        std::vector<Core::Tag> out;
        for (const Roots& r : m_Roots)
            for (Core::Tag root : r.tags)
                if (std::find(out.begin(), out.end(), root) == out.end())
                    out.push_back(root);
        return out;
    }

    const Profile* ProfileRegistry::Find(std::string_view name) const
    {
        const Entry* best = nullptr;
        for (const Entry& e : m_Entries) {
            if (e.profile.name != name)
                continue;
            if (!best || SourceRank(e.source) < SourceRank(best->source))
                best = &e;
        }
        return best ? &best->profile : nullptr;
    }

    std::string ProfileRegistry::OwnerOf(std::string_view name) const
    {
        const Entry* best = nullptr;
        for (const Entry& e : m_Entries) {
            if (e.profile.name != name)
                continue;
            if (!best || SourceRank(e.source) < SourceRank(best->source))
                best = &e;
        }
        return best ? best->owner : std::string();
    }

    std::vector<std::string> ProfileRegistry::Names() const
    {
        std::vector<std::string> out;
        for (const Entry& e : m_Entries)
            if (std::find(out.begin(), out.end(), e.profile.name) == out.end())
                out.push_back(e.profile.name);
        std::sort(out.begin(), out.end());
        return out;
    }

    std::vector<const Profile*> ProfileRegistry::All() const
    {
        std::vector<const Profile*> out;
        for (const std::string& name : Names())
            if (const Profile* p = Find(name))
                out.push_back(p);
        return out;
    }

    bool ProfileRegistry::ResolveInto(const Profile& profile, ResolvedProfile& out, std::vector<std::string>& visiting, std::string* error) const
    {
        if (std::find(visiting.begin(), visiting.end(), profile.name) != visiting.end()) {
            if (error) *error = "profile '" + profile.name + "' inherits from itself";
            return false;
        }
        visiting.push_back(profile.name);

        ResolvedProfile base;
        if (!profile.base.empty()) {
            const Profile* parent = Find(profile.base);
            if (!parent) {
                if (error) *error = "profile '" + profile.name + "' has the unknown base '" + profile.base + "'";
                return false;
            }
            if (!ResolveInto(*parent, base, visiting, error))
                return false;
        }

        out = std::move(base);
        out.name = profile.name;
        out.chain.insert(out.chain.begin(), profile.name);
        for (Core::Tag tag : profile.identity)
            out.identity.Add(tag);
        std::vector<Rule> rules = profile.rules;
        rules.insert(rules.end(), out.rules.begin(), out.rules.end());
        out.rules = std::move(rules);
        if (profile.defaultResponse) out.defaultResponse = *profile.defaultResponse;
        if (profile.mode) out.mode = *profile.mode;
        if (!profile.material.empty()) out.material = profile.material;
        if (profile.events) out.events = *profile.events;

        visiting.pop_back();
        return true;
    }

    bool ProfileRegistry::Resolve(std::string_view name, ResolvedProfile& out, std::string* error) const
    {
        EnsureBuiltinTags();
        const Profile* profile = Find(name);
        if (!profile) {
            if (error) *error = "unknown collision profile '" + std::string(name) + "'";
            return false;
        }
        std::vector<std::string> visiting;
        ResolvedProfile resolved;
        if (!ResolveInto(*profile, resolved, visiting, error))
            return false;
        resolved.identity = FilterCollisionTags(resolved.identity);
        out = std::move(resolved);
        return true;
    }

    void ProfileRegistry::Clear()
    {
        m_Entries.clear();
        m_Roots.clear();
        m_Materials.clear();
        InstallBuiltins();
    }

    Core::TagContainer FilterCollisionTags(const Core::TagContainer& tags)
    {
        const ProfileRegistry& registry = ProfileRegistry::Get();
        Core::TagContainer out;
        for (Core::Tag tag : tags)
            if (registry.IsCollisionTag(tag))
                out.Add(tag);
        return out;
    }
}
