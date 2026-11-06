#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

#include "engine/core/tags/tag.hpp"

namespace Shard::Engine::Core {

    // Matching goes one way: holding "Enemy.Undead.Skeleton" has "Enemy", not the reverse.
    class TagContainer {
    public:
        class Listener {
        public:
            virtual ~Listener() = default;
            virtual void OnTagsChanged(const TagContainer& added, const TagContainer& removed, bool quiet) = 0;
        };

        TagContainer() = default;
        TagContainer(std::initializer_list<Tag> tags);
        explicit TagContainer(const std::vector<Tag>& tags);

        TagContainer(const TagContainer& other) : m_Tags(other.m_Tags) {}
        TagContainer(TagContainer&& other) noexcept : m_Tags(std::move(other.m_Tags)) {}
        TagContainer& operator=(const TagContainer& other) { Assign(other); return *this; }
        TagContainer& operator=(TagContainer&& other) { Assign(other); return *this; }

        bool Add(Tag tag);
        bool Remove(Tag tag);
        size_t RemoveWithChildren(Tag tag);
        void Clear();
        void Assign(const TagContainer& other, bool quiet = false);

        bool HasTag(Tag tag) const;
        bool HasTagExact(Tag tag) const;
        bool HasAny(const TagContainer& other) const;
        bool HasAll(const TagContainer& other) const;
        bool HasAnyExact(const TagContainer& other) const;
        bool HasAllExact(const TagContainer& other) const;

        bool IsEmpty() const { return m_Tags.empty(); }
        size_t Size() const { return m_Tags.size(); }
        const std::vector<Tag>& GetTags() const { return m_Tags; }
        std::vector<Tag>::const_iterator begin() const { return m_Tags.begin(); }
        std::vector<Tag>::const_iterator end() const { return m_Tags.end(); }

        std::vector<std::string> ToStrings() const;
        std::string ToString() const;

        static TagContainer Difference(const TagContainer& a, const TagContainer& b);

        friend bool operator==(const TagContainer& a, const TagContainer& b) { return a.m_Tags == b.m_Tags; }
        friend bool operator!=(const TagContainer& a, const TagContainer& b) { return a.m_Tags != b.m_Tags; }

        void SetListener(Listener* listener) { m_Listener = listener; }

    private:
        void Notify(const TagContainer& added, const TagContainer& removed, bool quiet);
        bool Insert(Tag tag);

        std::vector<Tag> m_Tags;
        Listener* m_Listener = nullptr;
    };
}
