#include "tag_container.hpp"

#include <algorithm>
#include <iterator>

#include "engine/core/tags/tag_registry.hpp"

namespace Shard::Engine::Core {

    TagContainer::TagContainer(std::initializer_list<Tag> tags)
    {
        for (Tag tag : tags)
            Insert(tag);
    }

    TagContainer::TagContainer(const std::vector<Tag>& tags)
    {
        for (Tag tag : tags)
            Insert(tag);
    }

    bool TagContainer::Insert(Tag tag)
    {
        if (!tag.IsValid())
            return false;
        auto it = std::lower_bound(m_Tags.begin(), m_Tags.end(), tag);
        if (it != m_Tags.end() && *it == tag)
            return false;
        m_Tags.insert(it, tag);
        return true;
    }

    void TagContainer::Notify(const TagContainer& added, const TagContainer& removed, bool quiet)
    {
        if (m_Listener && (!added.IsEmpty() || !removed.IsEmpty()))
            m_Listener->OnTagsChanged(added, removed, quiet);
    }

    bool TagContainer::Add(Tag tag)
    {
        if (!Insert(tag))
            return false;
        Notify(TagContainer{ tag }, TagContainer(), false);
        return true;
    }

    bool TagContainer::Remove(Tag tag)
    {
        auto it = std::lower_bound(m_Tags.begin(), m_Tags.end(), tag);
        if (it == m_Tags.end() || *it != tag)
            return false;
        m_Tags.erase(it);
        Notify(TagContainer(), TagContainer{ tag }, false);
        return true;
    }

    size_t TagContainer::RemoveWithChildren(Tag tag)
    {
        if (!tag.IsValid())
            return 0;

        const TagRegistry& registry = TagRegistry::Get();
        TagContainer removed;
        m_Tags.erase(std::remove_if(m_Tags.begin(), m_Tags.end(), [&](Tag t) {
            if (!registry.Matches(t, tag))
                return false;
            removed.Insert(t);
            return true;
        }), m_Tags.end());

        Notify(TagContainer(), removed, false);
        return removed.Size();
    }

    void TagContainer::Clear()
    {
        if (m_Tags.empty())
            return;
        TagContainer removed(m_Tags);
        m_Tags.clear();
        Notify(TagContainer(), removed, false);
    }

    void TagContainer::Assign(const TagContainer& other, bool quiet)
    {
        if (&other == this)
            return;
        const TagContainer added = Difference(other, *this);
        const TagContainer removed = Difference(*this, other);
        m_Tags = other.m_Tags;
        Notify(added, removed, quiet);
    }

    bool TagContainer::HasTag(Tag tag) const
    {
        if (!tag.IsValid())
            return false;
        if (HasTagExact(tag))
            return true;

        const TagRegistry& registry = TagRegistry::Get();
        for (Tag t : m_Tags)
            if (registry.Matches(t, tag))
                return true;
        return false;
    }

    bool TagContainer::HasTagExact(Tag tag) const
    {
        return tag.IsValid() && std::binary_search(m_Tags.begin(), m_Tags.end(), tag);
    }

    bool TagContainer::HasAny(const TagContainer& other) const
    {
        for (Tag tag : other.m_Tags)
            if (HasTag(tag))
                return true;
        return false;
    }

    bool TagContainer::HasAll(const TagContainer& other) const
    {
        for (Tag tag : other.m_Tags)
            if (!HasTag(tag))
                return false;
        return true;
    }

    bool TagContainer::HasAnyExact(const TagContainer& other) const
    {
        for (Tag tag : other.m_Tags)
            if (HasTagExact(tag))
                return true;
        return false;
    }

    bool TagContainer::HasAllExact(const TagContainer& other) const
    {
        for (Tag tag : other.m_Tags)
            if (!HasTagExact(tag))
                return false;
        return true;
    }

    std::vector<std::string> TagContainer::ToStrings() const
    {
        std::vector<std::string> names;
        names.reserve(m_Tags.size());
        for (Tag tag : m_Tags)
            names.push_back(tag.ToString());
        std::sort(names.begin(), names.end());
        return names;
    }

    std::string TagContainer::ToString() const
    {
        std::string text;
        for (const std::string& name : ToStrings())
            text += (text.empty() ? "" : ", ") + name;
        return text;
    }

    TagContainer TagContainer::Difference(const TagContainer& a, const TagContainer& b)
    {
        TagContainer out;
        std::set_difference(a.m_Tags.begin(), a.m_Tags.end(), b.m_Tags.begin(), b.m_Tags.end(), std::back_inserter(out.m_Tags));
        return out;
    }
}
