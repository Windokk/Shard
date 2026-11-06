#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "engine/core/containers/string_id.hpp"

namespace Shard::Engine::Core {

    class Tag {
    public:
        constexpr Tag() = default;
        static constexpr Tag FromId(StringId id) { Tag tag; tag.m_Id = id; return tag; }

        static Tag Request(std::string_view name);

        constexpr StringId GetId() const { return m_Id; }
        constexpr bool IsValid() const { return m_Id.IsValid(); }

        Tag GetParent() const;
        bool IsChildOf(Tag parent) const;
        bool Matches(Tag tagOrParent) const;

        std::string ToString() const;

        friend constexpr bool operator==(Tag a, Tag b) { return a.m_Id == b.m_Id; }
        friend constexpr bool operator!=(Tag a, Tag b) { return a.m_Id != b.m_Id; }
        friend constexpr bool operator<(Tag a, Tag b) { return a.m_Id < b.m_Id; }

    private:
        StringId m_Id;
    };

    Tag DeclareNativeTag(std::string_view name);
}

namespace std {
    template <>
    struct hash<Shard::Engine::Core::Tag> {
        size_t operator()(Shard::Engine::Core::Tag tag) const noexcept { return static_cast<size_t>(tag.GetId().Value()); }
    };
}

#define SH_DECLARE_TAG(variable, name) \
    static const ::Shard::Engine::Core::Tag variable = ::Shard::Engine::Core::DeclareNativeTag(name)
