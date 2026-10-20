#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

#include "engine/core/tags/tag.hpp"

namespace Shard::Engine::Core {

    /// @brief A set of tags : what a world, an actor or a component carries, and what queries are tested against.
    ///
    /// It holds the tags it was given (its explicit tags) and answers with their parents included : a container holding
    /// "Enemy.Undead.Skeleton" has "Enemy" (HasTag), but not exactly (HasTagExact). The matching goes one way only : a container
    /// holding "Enemy" does not have "Enemy.Undead.Skeleton".
    class TagContainer {
    public:
        /// What the owner of a container (a world, an actor, a component) hears when its tags change, to keep its world's index
        /// up to date and send TagsChangedEvent. `quiet` : the change comes from loading, no event is wanted.
        class Listener {
        public:
            virtual ~Listener() = default;
            virtual void OnTagsChanged(const TagContainer& added, const TagContainer& removed, bool quiet) = 0;
        };

        TagContainer() = default;
        TagContainer(std::initializer_list<Tag> tags);
        explicit TagContainer(const std::vector<Tag>& tags);

        /// The tags are copied, the listener is not : it belongs to the object that owns the container
        TagContainer(const TagContainer& other) : m_Tags(other.m_Tags) {}
        TagContainer(TagContainer&& other) noexcept : m_Tags(std::move(other.m_Tags)) {}
        /// Takes the tags of `other` (the listener hears what changed) and keeps its own listener
        TagContainer& operator=(const TagContainer& other) { Assign(other); return *this; }
        TagContainer& operator=(TagContainer&& other) { Assign(other); return *this; }

        // ---- Changing ----

        /// False when the tag was there already, or is empty
        bool Add(Tag tag);
        /// Removes an explicit tag only : removing "Enemy" from {Enemy.Undead} does nothing. False when it was not there.
        bool Remove(Tag tag);
        /// Removes `tag` and every explicit tag under it. Returns how many were removed.
        size_t RemoveWithChildren(Tag tag);
        void Clear();
        /// Takes the tags of `other`. quiet : for loading, the listener is told not to send events.
        void Assign(const TagContainer& other, bool quiet = false);

        // ---- Testing ----

        /// `tag`, or a tag under it, is in the container
        bool HasTag(Tag tag) const;
        /// `tag` itself is in the container
        bool HasTagExact(Tag tag) const;
        /// At least one tag of `other` (with HasTag). False when `other` is empty.
        bool HasAny(const TagContainer& other) const;
        /// Every tag of `other` (with HasTag). True when `other` is empty.
        bool HasAll(const TagContainer& other) const;
        bool HasAnyExact(const TagContainer& other) const;
        bool HasAllExact(const TagContainer& other) const;

        bool IsEmpty() const { return m_Tags.empty(); }
        size_t Size() const { return m_Tags.size(); }
        /// The explicit tags, sorted by hash (the same order on every platform)
        const std::vector<Tag>& GetTags() const { return m_Tags; }
        std::vector<Tag>::const_iterator begin() const { return m_Tags.begin(); }
        std::vector<Tag>::const_iterator end() const { return m_Tags.end(); }

        /// The names, sorted alphabetically : how a file stores them
        std::vector<std::string> ToStrings() const;
        /// "Enemy.Boss, Status.Burning"
        std::string ToString() const;

        /// The tags of `a` that are not in `b` (explicit tags)
        static TagContainer Difference(const TagContainer& a, const TagContainer& b);

        friend bool operator==(const TagContainer& a, const TagContainer& b) { return a.m_Tags == b.m_Tags; }
        friend bool operator!=(const TagContainer& a, const TagContainer& b) { return a.m_Tags != b.m_Tags; }

        void SetListener(Listener* listener) { m_Listener = listener; }

    private:
        void Notify(const TagContainer& added, const TagContainer& removed, bool quiet);
        /// Inserts without notifying : false if already there
        bool Insert(Tag tag);

        std::vector<Tag> m_Tags;
        Listener* m_Listener = nullptr;
    };
}
