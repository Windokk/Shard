#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "engine/core/containers/string_id.hpp"

namespace Shard::Engine::Core {

    /// @brief One hierarchical name : "Enemy.Undead.Skeleton". Each dot goes one level down the tree of the TagRegistry, and a tag
    /// also counts as each of its parents (see TagContainer::HasTag).
    ///
    /// A Tag is the StringId of its name : copying and comparing it costs one integer. The tree it belongs to (its parent, its
    /// children, its text) is the registry's : a tag the registry does not know matches only itself.
    class Tag {
    public:
        constexpr Tag() = default;
        static constexpr Tag FromId(StringId id) { Tag tag; tag.m_Id = id; return tag; }

        /// @brief The declared tag of that name (redirects followed), or the empty tag : a name nobody declared is a typo,
        /// reported once per name. Use this from code ; what a file holds is read with TagRegistry::Resolve.
        static Tag Request(std::string_view name);

        constexpr StringId GetId() const { return m_Id; }
        /// The empty tag (default constructed) is "no tag"
        constexpr bool IsValid() const { return m_Id.IsValid(); }

        /// "Enemy.Undead" for "Enemy.Undead.Skeleton", the empty tag for a root or an unknown tag
        Tag GetParent() const;
        /// Strictly under `parent` (a tag is not its own child)
        bool IsChildOf(Tag parent) const;
        /// This tag, or one under it : what HasTag tests
        bool Matches(Tag tagOrParent) const;

        /// The name, "" for the empty tag
        std::string ToString() const;

        friend constexpr bool operator==(Tag a, Tag b) { return a.m_Id == b.m_Id; }
        friend constexpr bool operator!=(Tag a, Tag b) { return a.m_Id != b.m_Id; }
        /// By hash : stable on every platform, not alphabetical
        friend constexpr bool operator<(Tag a, Tag b) { return a.m_Id < b.m_Id; }

    private:
        StringId m_Id;
    };

    /// @brief Declares a tag from code (TagSource::Code) and returns it. What SH_DECLARE_TAG expands to.
    Tag DeclareNativeTag(std::string_view name);
}

namespace std {
    template <>
    struct hash<Shard::Engine::Core::Tag> {
        size_t operator()(Shard::Engine::Core::Tag tag) const noexcept { return static_cast<size_t>(tag.GetId().Value()); }
    };
}

/// @brief Declares, from a .cpp, a tag a C++ system depends on : it is in the tree before any world loads, and the editor can
/// neither rename nor delete it. `SH_DECLARE_TAG(TagDoorLocked, "Interaction.Door.Locked");` then `HasTag(TagDoorLocked)`.
#define SH_DECLARE_TAG(variable, name) \
    static const ::Shard::Engine::Core::Tag variable = ::Shard::Engine::Core::DeclareNativeTag(name)
