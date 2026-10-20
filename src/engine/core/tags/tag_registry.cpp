#include "tag_registry.hpp"

#include <algorithm>

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Core {

    namespace {
        /// The registry this copy of the code talks to : its own until a host links it
        TagRegistry*& Current()
        {
            static TagRegistry own;
            static TagRegistry* current = &own;
            return current;
        }

        bool IsNameChar(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        }

        bool SameDeclarer(const TagDeclaration& d, TagSource source, std::string_view owner)
        {
            return d.source == source && d.owner == owner;
        }
    }

    uint8_t TagInfo::Sources() const
    {
        uint8_t bits = 0;
        for (const TagDeclaration& d : declarations)
            bits |= uint8_t(d.source);
        return bits;
    }

    const std::string& TagInfo::Comment() const
    {
        static const std::string none;
        for (const TagDeclaration& d : declarations)
            if (!d.comment.empty())
                return d.comment;
        return none;
    }

    TagRegistry& TagRegistry::Get()
    {
        return *Current();
    }

    void TagRegistry::Link(TagRegistry* host)
    {
        TagRegistry*& current = Current();
        if (!host || host == current)
            return;

        // What the library's SH_DECLARE_TAG declared while it was being loaded (static initialization runs before the link)
        for (const Entry& e : current->m_Entries)
            if (e.declaration.source == TagSource::Code)
                host->Declare(e.name, TagSource::Code, e.declaration.owner, e.declaration.comment);

        current = host;
    }

    bool TagRegistry::IsValidName(std::string_view name, std::string* error)
    {
        auto fail = [&](const std::string& why) {
            if (error)
                *error = why;
            return false;
        };

        if (name.empty())
            return fail("the name is empty");

        size_t segments = 1;
        size_t segmentLength = 0;
        for (char c : name) {
            if (c == '.') {
                if (segmentLength == 0)
                    return fail("'" + std::string(name) + "' has an empty segment (two dots, or a dot at an end)");
                segmentLength = 0;
                ++segments;
                continue;
            }
            if (!IsNameChar(c))
                return fail("'" + std::string(name) + "' holds '" + std::string(1, c) + "' : only A-Z a-z 0-9 _ and dots are allowed");
            ++segmentLength;
        }
        if (segmentLength == 0)
            return fail("'" + std::string(name) + "' ends with a dot");
        if (segments > kMaxDepth)
            return fail("'" + std::string(name) + "' has " + std::to_string(segments) + " levels, at most " + std::to_string(kMaxDepth) + " are allowed");
        return true;
    }

    bool TagRegistry::Declare(std::string_view name, TagSource source, std::string_view owner, std::string_view comment)
    {
        std::string error;
        if (!IsValidName(name, &error)) {
            DEBUG_ERROR("Tags : cannot declare a tag : " + error);
            return false;
        }

        for (Entry& e : m_Entries) {
            if (e.name == name && SameDeclarer(e.declaration, source, owner)) {
                if (!comment.empty() && e.declaration.comment != comment) {
                    e.declaration.comment = std::string(comment);
                    Rebuild();
                }
                return true;
            }
        }

        m_Entries.push_back(Entry{ std::string(name), TagDeclaration{ source, std::string(owner), std::string(comment) } });
        Rebuild();
        return true;
    }

    void TagRegistry::SetDeclarations(TagSource source, std::string_view owner, const std::vector<std::pair<std::string, std::string>>& tags)
    {
        m_Entries.erase(std::remove_if(m_Entries.begin(), m_Entries.end(), [&](const Entry& e) {
            return SameDeclarer(e.declaration, source, owner);
        }), m_Entries.end());

        for (const auto& [name, comment] : tags) {
            std::string error;
            if (!IsValidName(name, &error)) {
                DEBUG_ERROR("Tags : skipped a tag" + (owner.empty() ? std::string() : " of " + std::string(owner)) + " : " + error);
                continue;
            }
            m_Entries.push_back(Entry{ name, TagDeclaration{ source, std::string(owner), comment } });
        }

        Rebuild();
    }

    void TagRegistry::RemoveDeclarations(TagSource source, std::string_view owner)
    {
        const size_t before = m_Entries.size();
        m_Entries.erase(std::remove_if(m_Entries.begin(), m_Entries.end(), [&](const Entry& e) {
            return SameDeclarer(e.declaration, source, owner);
        }), m_Entries.end());

        if (m_Entries.size() != before)
            Rebuild();
    }

    void TagRegistry::SetRedirects(std::map<std::string, std::string> redirects)
    {
        m_Redirects = std::move(redirects);
        ++m_Version;
    }

    std::string TagRegistry::ApplyRedirects(std::string_view name) const
    {
        std::string current(name);
        // A chain (A -> B -> C) is followed ; a loop stops after as many steps as there are redirects
        for (size_t step = 0; step <= m_Redirects.size(); ++step) {
            auto it = m_Redirects.find(current);
            if (it == m_Redirects.end() || it->second == current)
                break;
            current = it->second;
        }
        return current;
    }

    Tag TagRegistry::Find(std::string_view name) const
    {
        if (name.empty())
            return Tag();
        const Tag tag = Tag::FromId(StringId(ApplyRedirects(name)));
        return IsKnown(tag) ? tag : Tag();
    }

    Tag TagRegistry::Resolve(std::string_view name, bool keep)
    {
        if (name.empty())
            return Tag();

        const std::string target = ApplyRedirects(name);
        const Tag tag = Tag::FromId(StringId(target));
        if (IsKnown(tag))
            return tag;

        std::string error;
        if (!IsValidName(target, &error)) {
            if (keep)
                DEBUG_WARNING("Tags : ignored '" + target + "' : " + error);
            return Tag();
        }
        if (!keep)
            return tag;

        // Kept by name, so that what the file held is written back
        Remember(target);
        ReportUnknown(target);
        return tag;
    }

    void TagRegistry::ReportUnknown(std::string_view name)
    {
        const uint64_t id = StringId(name).Value();
        if (m_ReportedUnknown.insert(id).second)
            DEBUG_WARNING("Tags : '" + std::string(name) + "' is not declared (by the project, a plugin or code) : it matches only itself");
    }

    const TagInfo* TagRegistry::GetInfo(Tag tag) const
    {
        auto it = m_Nodes.find(tag.GetId().Value());
        return it == m_Nodes.end() ? nullptr : &it->second;
    }

    Tag TagRegistry::GetParent(Tag tag) const
    {
        const TagInfo* info = GetInfo(tag);
        return info ? info->parent : Tag();
    }

    bool TagRegistry::Matches(Tag tag, Tag tagOrParent) const
    {
        if (!tag.IsValid() || !tagOrParent.IsValid())
            return false;
        if (tag == tagOrParent)
            return true;

        const TagInfo* info = GetInfo(tag);
        const TagInfo* target = GetInfo(tagOrParent);
        if (!info || !target || target->depth >= info->depth)
            return false;

        // Up as many levels as separate them : the ancestor at that depth is the only one that can be it
        for (uint8_t depth = info->depth; depth > target->depth && info; --depth)
            info = GetInfo(info->parent);
        return info && info->tag == tagOrParent;
    }

    std::string TagRegistry::NameOf(Tag tag) const
    {
        if (!tag.IsValid())
            return std::string();
        auto it = m_Names.find(tag.GetId().Value());
        return it != m_Names.end() ? it->second : tag.GetId().ToString();
    }

    std::vector<Tag> TagRegistry::GetRoots() const
    {
        std::vector<const TagInfo*> roots;
        for (const auto& [id, info] : m_Nodes)
            if (!info.parent.IsValid())
                roots.push_back(&info);
        std::sort(roots.begin(), roots.end(), [](const TagInfo* a, const TagInfo* b) { return a->name < b->name; });

        std::vector<Tag> out;
        out.reserve(roots.size());
        for (const TagInfo* info : roots)
            out.push_back(info->tag);
        return out;
    }

    std::vector<Tag> TagRegistry::GetAll() const
    {
        std::vector<const TagInfo*> all;
        all.reserve(m_Nodes.size());
        for (const auto& [id, info] : m_Nodes)
            all.push_back(&info);
        std::sort(all.begin(), all.end(), [](const TagInfo* a, const TagInfo* b) { return a->name < b->name; });

        std::vector<Tag> out;
        out.reserve(all.size());
        for (const TagInfo* info : all)
            out.push_back(info->tag);
        return out;
    }

    void TagRegistry::CollectDescendants(Tag tag, std::vector<Tag>& out, bool includeSelf) const
    {
        if (!tag.IsValid())
            return;
        if (includeSelf)
            out.push_back(tag);
        if (const TagInfo* info = GetInfo(tag))
            for (Tag child : info->children)
                CollectDescendants(child, out, true);
    }

    void TagRegistry::Clear()
    {
        m_Entries.clear();
        m_Nodes.clear();
        m_Names.clear();
        m_Redirects.clear();
        m_ReportedUnknown.clear();
        ++m_Version;
    }

    void TagRegistry::Remember(const std::string& name)
    {
        const uint64_t id = StringId(name).Value();
        auto [it, inserted] = m_Names.emplace(id, name);
        if (!inserted && it->second != name)
            DEBUG_ERROR("Tags : '" + name + "' and '" + it->second + "' have the same hash : rename one of them");
    }

    void TagRegistry::Rebuild()
    {
        m_Nodes.clear();

        for (const Entry& e : m_Entries) {
            // The tag and each of its parents : "A.B.C" makes A, A.B and A.B.C
            Tag child;
            size_t end = e.name.size();
            while (true) {
                const std::string name = e.name.substr(0, end);
                const Tag tag = Tag::FromId(StringId(name));
                Remember(name);

                auto [it, inserted] = m_Nodes.try_emplace(tag.GetId().Value());
                TagInfo& info = it->second;
                if (inserted) {
                    info.name = name;
                    info.tag = tag;
                    info.depth = uint8_t(std::count(name.begin(), name.end(), '.') + 1);
                }
                if (end == e.name.size())
                    info.declarations.push_back(e.declaration);
                if (child.IsValid() && std::find(info.children.begin(), info.children.end(), child) == info.children.end())
                    info.children.push_back(child);

                const size_t dot = name.rfind('.');
                if (dot == std::string::npos)
                    break;
                child = tag;
                end = dot;
            }
        }

        for (auto& [id, info] : m_Nodes) {
            const size_t dot = info.name.rfind('.');
            if (dot != std::string::npos)
                info.parent = Tag::FromId(StringId(std::string_view(info.name).substr(0, dot)));
            std::sort(info.children.begin(), info.children.end(), [this](Tag a, Tag b) { return NameOf(a) < NameOf(b); });
        }

        ++m_Version;
    }

    // ---- Tag ----

    Tag Tag::Request(std::string_view name)
    {
        TagRegistry& registry = TagRegistry::Get();
        const Tag tag = registry.Find(name);
        if (!tag.IsValid() && !name.empty())
            registry.ReportUnknown(registry.ApplyRedirects(name));
        return tag;
    }

    Tag Tag::GetParent() const
    {
        return TagRegistry::Get().GetParent(*this);
    }

    bool Tag::IsChildOf(Tag parent) const
    {
        return *this != parent && TagRegistry::Get().Matches(*this, parent);
    }

    bool Tag::Matches(Tag tagOrParent) const
    {
        return TagRegistry::Get().Matches(*this, tagOrParent);
    }

    std::string Tag::ToString() const
    {
        return TagRegistry::Get().NameOf(*this);
    }

    Tag DeclareNativeTag(std::string_view name)
    {
        if (!TagRegistry::Get().Declare(name, TagSource::Code))
            return Tag();
        return Tag::FromId(StringId(name));
    }
}
