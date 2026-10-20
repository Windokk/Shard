#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "engine/core/tags/tag.hpp"
#include "engine/core/tags/tag_container.hpp"

namespace Shard::Engine::Core {

    /// @brief A condition over a TagContainer, built once and tested against any number of them. Its text form is what files store
    /// and what is typed in the editor :
    ///
    ///     Enemy & !(Enemy.Boss | Status.Dead)
    ///
    /// `&` is and, `|` is or (looser than `&`), `!` is not, parentheses group. A bare name is HasTag, `=Name` is HasTagExact.
    /// The empty query matches everything.
    class TagQuery {
    public:
        TagQuery() = default;

        // ---- Building ----

        /// HasTag(tag). Of the empty tag : matches nothing.
        static TagQuery Has(Tag tag);
        /// HasTagExact(tag). Of the empty tag : matches nothing.
        static TagQuery Exact(Tag tag);

        /// Every one of these tags (HasTag), in this order. The names are read like the text form's : an unknown one is kept and reported.
        static TagQuery All(std::initializer_list<std::string_view> names);
        static TagQuery All(const TagContainer& tags);
        /// At least one of these tags. Of no tag at all : matches nothing.
        static TagQuery Any(std::initializer_list<std::string_view> names);
        static TagQuery Any(const TagContainer& tags);
        /// None of these tags
        static TagQuery None(std::initializer_list<std::string_view> names);
        static TagQuery None(const TagContainer& tags);

        TagQuery And(const TagQuery& other) const;
        TagQuery Or(const TagQuery& other) const;
        TagQuery Not() const;

        // ---- Text ----

        /// @brief The query that text describes. A text that is not valid logs why and gives the empty query (which matches everything).
        /// The names are read like a file's (TagRegistry::Resolve) : an unknown name is kept and reported.
        static TagQuery Parse(std::string_view text);
        /// @brief Parse without logging : false and the reason when the text is not valid (out is then left alone)
        /// @param keep false : the unknown names are neither reported nor remembered (see TagRegistry::Resolve), for a text being typed
        static bool TryParse(std::string_view text, TagQuery& out, std::string* error = nullptr, bool keep = true);
        /// The text form, with the fewest parentheses that keep its meaning. Parse(ToString()) gives the same query.
        std::string ToString() const;

        // ---- Testing ----

        bool Matches(const TagContainer& tags) const;
        bool IsEmpty() const { return m_Nodes.empty(); }

        /// The tags the query names, in the order they appear
        std::vector<Tag> GetTags() const;
        /// The same query, each tag replaced by what `remap` gives for it (a renamed tag). The empty tag removes nothing : it is kept.
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
        /// @brief A tag every match has (HasTag) : the query itself, or one term of its top-level `&`. The empty tag when there is none
        /// (an `|`, a `!`...). What an index starts from before testing the rest.
        Tag GetRequiredTag() const;

        friend bool operator==(const TagQuery& a, const TagQuery& b);
        friend bool operator!=(const TagQuery& a, const TagQuery& b) { return !(a == b); }

    private:
        enum class Kind : uint8_t { Has, Exact, Not, And, Or };

        /// The nodes are stored flat, children before their parent : the root is the last one
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
        /// Copies the nodes of `other` after this query's, returns the index of its root there
        uint32_t Append(const TagQuery& other);

        bool Evaluate(uint32_t node, const TagContainer& tags) const;
        void Write(uint32_t node, std::string& out, int parentPrecedence) const;
        uint32_t Root() const { return uint32_t(m_Nodes.size() - 1); }

        friend class TagQueryParser;

        std::vector<Node> m_Nodes;
    };
}
