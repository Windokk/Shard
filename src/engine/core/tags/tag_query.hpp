#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "engine/core/tags/tag.hpp"
#include "engine/core/tags/tag_container.hpp"

namespace Shard::Engine::Core {

    // Text form: `Enemy & !(Enemy.Boss | Status.Dead)`. `|` binds looser than `&`, `=Name` is HasTagExact, empty query matches everything.
    class TagQuery {
    public:
        TagQuery() = default;

        static TagQuery Has(Tag tag);
        static TagQuery Exact(Tag tag);

        static TagQuery All(std::initializer_list<std::string_view> names);
        static TagQuery All(const TagContainer& tags);
        static TagQuery Any(std::initializer_list<std::string_view> names);
        static TagQuery Any(const TagContainer& tags);
        static TagQuery None(std::initializer_list<std::string_view> names);
        static TagQuery None(const TagContainer& tags);

        TagQuery And(const TagQuery& other) const;
        TagQuery Or(const TagQuery& other) const;
        TagQuery Not() const;

        static TagQuery Parse(std::string_view text);
        static bool TryParse(std::string_view text, TagQuery& out, std::string* error = nullptr, bool keep = true);
        std::string ToString() const;

        bool Matches(const TagContainer& tags) const;
        bool IsEmpty() const { return m_Nodes.empty(); }

        std::vector<Tag> GetTags() const;
        template<class F>
        TagQuery Remapped(F&& remap) const
        {
            TagQuery q = *this;
            for (Node& node : q.m_Nodes)
                if (node.tag.IsValid()) {
                    const Tag mapped = remap(node.tag);
                    if (mapped.IsValid())
                        node.tag = mapped;
                }
            return q;
        }
        Tag GetRequiredTag() const;

        friend bool operator==(const TagQuery& a, const TagQuery& b);
        friend bool operator!=(const TagQuery& a, const TagQuery& b) { return !(a == b); }

    private:
        enum class Kind : uint8_t { Has, Exact, Not, And, Or };

        struct Node {
            Kind kind = Kind::Has;
            Tag tag;
            std::vector<uint32_t> children;

            friend bool operator==(const Node& a, const Node& b) { return a.kind == b.kind && a.tag == b.tag && a.children == b.children; }
        };

        static TagQuery Leaf(Kind kind, Tag tag);
        static TagQuery AllOf(const std::vector<Tag>& tags);
        static TagQuery AnyOf(const std::vector<Tag>& tags);
        static TagQuery Combine(Kind kind, const std::vector<TagQuery>& parts);
        uint32_t Append(const TagQuery& other);

        bool Evaluate(uint32_t node, const TagContainer& tags) const;
        void Write(uint32_t node, std::string& out, int parentPrecedence) const;
        uint32_t Root() const { return uint32_t(m_Nodes.size() - 1); }

        friend class TagQueryParser;

        std::vector<Node> m_Nodes;
    };
}
