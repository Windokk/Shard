#include "tag_query.hpp"

#include <algorithm>

#include "engine/core/diagnostics/logger.hpp"
#include "engine/core/tags/tag_registry.hpp"

namespace Shard::Engine::Core {

    // ---- Building ----

    TagQuery TagQuery::Leaf(Kind kind, Tag tag)
    {
        TagQuery q;
        // No tag : matches nothing (the empty query would match everything)
        if (!tag.IsValid())
            q.m_Nodes.push_back(Node{ Kind::Or, Tag(), {} });
        else
            q.m_Nodes.push_back(Node{ kind, tag, {} });
        return q;
    }

    uint32_t TagQuery::Append(const TagQuery& other)
    {
        const uint32_t offset = uint32_t(m_Nodes.size());
        for (Node node : other.m_Nodes) {
            for (uint32_t& child : node.children)
                child += offset;
            m_Nodes.push_back(std::move(node));
        }
        return uint32_t(m_Nodes.size() - 1);
    }

    TagQuery TagQuery::Combine(Kind kind, const std::vector<TagQuery>& parts)
    {
        // The empty query matches everything : it adds nothing to an `&`, and makes an `|` always true
        std::vector<const TagQuery*> kept;
        for (const TagQuery& part : parts) {
            if (part.IsEmpty()) {
                if (kind == Kind::Or)
                    return TagQuery();
                continue;
            }
            kept.push_back(&part);
        }

        if (kept.size() == 1)
            return *kept.front();
        if (kept.empty() && kind == Kind::And)
            return TagQuery();

        TagQuery q;
        std::vector<uint32_t> children;
        for (const TagQuery* part : kept) {
            const Node& root = part->m_Nodes.back();
            // (a & b) & c is a & b & c
            if (root.kind == kind && !root.children.empty()) {
                const uint32_t offset = uint32_t(q.m_Nodes.size());
                q.Append(*part);
                q.m_Nodes.pop_back();
                for (uint32_t child : root.children)
                    children.push_back(child + offset);
            }
            else
                children.push_back(q.Append(*part));
        }
        q.m_Nodes.push_back(Node{ kind, Tag(), std::move(children) });
        return q;
    }

    TagQuery TagQuery::Has(Tag tag) { return Leaf(Kind::Has, tag); }
    TagQuery TagQuery::Exact(Tag tag) { return Leaf(Kind::Exact, tag); }

    namespace {
        /// Like the text form : an unknown name is kept (and reported), so a typo makes the term match nothing instead of vanishing.
        /// In the order given, which is the order the text form will show them in.
        std::vector<Tag> Resolved(std::initializer_list<std::string_view> names)
        {
            std::vector<Tag> tags;
            for (std::string_view name : names) {
                const Tag tag = TagRegistry::Get().Resolve(name);
                if (tag.IsValid() && std::find(tags.begin(), tags.end(), tag) == tags.end())
                    tags.push_back(tag);
            }
            return tags;
        }
    }

    TagQuery TagQuery::AllOf(const std::vector<Tag>& tags)
    {
        std::vector<TagQuery> parts;
        for (Tag tag : tags)
            parts.push_back(Has(tag));
        return Combine(Kind::And, parts);
    }

    TagQuery TagQuery::AnyOf(const std::vector<Tag>& tags)
    {
        if (tags.empty()) {
            // Of nothing : an `|` without terms, which matches nothing (its text is "()")
            TagQuery q;
            q.m_Nodes.push_back(Node{ Kind::Or, Tag(), {} });
            return q;
        }
        std::vector<TagQuery> parts;
        for (Tag tag : tags)
            parts.push_back(Has(tag));
        return Combine(Kind::Or, parts);
    }

    TagQuery TagQuery::All(std::initializer_list<std::string_view> names) { return AllOf(Resolved(names)); }
    TagQuery TagQuery::Any(std::initializer_list<std::string_view> names) { return AnyOf(Resolved(names)); }
    TagQuery TagQuery::None(std::initializer_list<std::string_view> names)
    {
        const std::vector<Tag> tags = Resolved(names);
        return tags.empty() ? TagQuery() : AnyOf(tags).Not();
    }

    TagQuery TagQuery::All(const TagContainer& tags) { return AllOf(tags.GetTags()); }
    TagQuery TagQuery::Any(const TagContainer& tags) { return AnyOf(tags.GetTags()); }
    TagQuery TagQuery::None(const TagContainer& tags) { return tags.IsEmpty() ? TagQuery() : AnyOf(tags.GetTags()).Not(); }

    TagQuery TagQuery::And(const TagQuery& other) const { return Combine(Kind::And, { *this, other }); }
    TagQuery TagQuery::Or(const TagQuery& other) const { return Combine(Kind::Or, { *this, other }); }

    TagQuery TagQuery::Not() const
    {
        TagQuery q;
        if (IsEmpty()) {
            // Not everything : nothing
            q.m_Nodes.push_back(Node{ Kind::Or, Tag(), {} });
            return q;
        }
        // !!a is a : a `!` is only ever made here, after its child's nodes, so they are all the nodes before it
        if (m_Nodes.back().kind == Kind::Not) {
            q.m_Nodes.assign(m_Nodes.begin(), m_Nodes.end() - 1);
            return q;
        }
        const uint32_t child = q.Append(*this);
        q.m_Nodes.push_back(Node{ Kind::Not, Tag(), { child } });
        return q;
    }

    // ---- Testing ----

    bool TagQuery::Evaluate(uint32_t index, const TagContainer& tags) const
    {
        const Node& node = m_Nodes[index];
        switch (node.kind) {
            case Kind::Has:   return tags.HasTag(node.tag);
            case Kind::Exact: return tags.HasTagExact(node.tag);
            case Kind::Not:   return !Evaluate(node.children.front(), tags);
            case Kind::And:
                for (uint32_t child : node.children)
                    if (!Evaluate(child, tags))
                        return false;
                return true;
            case Kind::Or:
                for (uint32_t child : node.children)
                    if (Evaluate(child, tags))
                        return true;
                return false;
        }
        return false;
    }

    bool TagQuery::Matches(const TagContainer& tags) const
    {
        return IsEmpty() || Evaluate(Root(), tags);
    }

    std::vector<Tag> TagQuery::GetTags() const
    {
        std::vector<Tag> out;
        for (const Node& node : m_Nodes)
            if (node.tag.IsValid())
                out.push_back(node.tag);
        return out;
    }

    Tag TagQuery::GetRequiredTag() const
    {
        if (IsEmpty())
            return Tag();
        const Node& root = m_Nodes.back();
        if (root.kind == Kind::Has)
            return root.tag;
        if (root.kind == Kind::And)
            for (uint32_t child : root.children)
                if (m_Nodes[child].kind == Kind::Has)
                    return m_Nodes[child].tag;
        return Tag();
    }

    bool operator==(const TagQuery& a, const TagQuery& b)
    {
        return a.m_Nodes == b.m_Nodes;
    }

    // ---- Text ----

    void TagQuery::Write(uint32_t index, std::string& out, int parentPrecedence) const
    {
        const Node& node = m_Nodes[index];
        const int precedence = node.kind == Kind::Or ? 1 : node.kind == Kind::And ? 2 : 3;

        if (node.kind == Kind::Or && node.children.empty()) {
            out += "()";
            return;
        }

        const bool parentheses = precedence < parentPrecedence;
        if (parentheses)
            out += '(';

        switch (node.kind) {
            case Kind::Has:   out += node.tag.ToString(); break;
            case Kind::Exact: out += "=" + node.tag.ToString(); break;
            case Kind::Not:
                out += '!';
                Write(node.children.front(), out, 3);
                break;
            case Kind::And:
            case Kind::Or:
                for (size_t i = 0; i < node.children.size(); ++i) {
                    if (i)
                        out += node.kind == Kind::And ? " & " : " | ";
                    // An `&` inside an `&` only exists when Combine could not flatten it : it keeps its parentheses
                    Write(node.children[i], out, precedence + (m_Nodes[node.children[i]].kind == node.kind ? 1 : 0));
                }
                break;
        }

        if (parentheses)
            out += ')';
    }

    std::string TagQuery::ToString() const
    {
        std::string out;
        if (!IsEmpty())
            Write(Root(), out, 0);
        return out;
    }

    /// Recursive descent over the grammar :
    ///   or     := and ('|' and)*
    ///   and    := unary ('&' unary)*
    ///   unary  := '!' unary | '(' or ')' | '(' ')' | '='? name
    class TagQueryParser {
    public:
        TagQueryParser(std::string_view text, bool keep) : m_Text(text), m_Keep(keep) {}

        bool Run(TagQuery& out, std::string& error)
        {
            SkipSpaces();
            if (m_Pos == m_Text.size()) {
                out = TagQuery();
                return true;
            }

            TagQuery q;
            if (!ParseOr(q))
                return Fail(error);
            SkipSpaces();
            if (m_Pos != m_Text.size()) {
                m_Error = "unexpected '" + std::string(1, m_Text[m_Pos]) + "'";
                return Fail(error);
            }
            out = std::move(q);
            return true;
        }

    private:
        bool Fail(std::string& error)
        {
            error = m_Error + " at column " + std::to_string(m_Pos + 1) + " of \"" + std::string(m_Text) + "\"";
            return false;
        }

        void SkipSpaces()
        {
            while (m_Pos < m_Text.size() && (m_Text[m_Pos] == ' ' || m_Text[m_Pos] == '\t'))
                ++m_Pos;
        }

        bool Accept(char c)
        {
            SkipSpaces();
            if (m_Pos < m_Text.size() && m_Text[m_Pos] == c) {
                ++m_Pos;
                return true;
            }
            return false;
        }

        bool ParseOr(TagQuery& out)
        {
            std::vector<TagQuery> parts(1);
            if (!ParseAnd(parts.back()))
                return false;
            while (Accept('|')) {
                parts.emplace_back();
                if (!ParseAnd(parts.back()))
                    return false;
            }
            out = parts.size() == 1 ? std::move(parts.front()) : TagQuery::Combine(TagQuery::Kind::Or, parts);
            return true;
        }

        bool ParseAnd(TagQuery& out)
        {
            std::vector<TagQuery> parts(1);
            if (!ParseUnary(parts.back()))
                return false;
            while (Accept('&')) {
                parts.emplace_back();
                if (!ParseUnary(parts.back()))
                    return false;
            }
            out = parts.size() == 1 ? std::move(parts.front()) : TagQuery::Combine(TagQuery::Kind::And, parts);
            return true;
        }

        bool ParseUnary(TagQuery& out)
        {
            if (Accept('!')) {
                TagQuery inner;
                if (!ParseUnary(inner))
                    return false;
                out = inner.Not();
                return true;
            }

            if (Accept('(')) {
                if (Accept(')')) {
                    out = TagQuery::Any(TagContainer());
                    return true;
                }
                if (!ParseOr(out))
                    return false;
                if (!Accept(')')) {
                    m_Error = "a ')' is missing";
                    return false;
                }
                return true;
            }

            const bool exact = Accept('=');
            SkipSpaces();
            const size_t start = m_Pos;
            while (m_Pos < m_Text.size()) {
                const char c = m_Text[m_Pos];
                const bool nameChar = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
                if (!nameChar)
                    break;
                ++m_Pos;
            }
            if (m_Pos == start) {
                m_Error = m_Pos < m_Text.size() ? "a tag name was expected, not '" + std::string(1, m_Text[m_Pos]) + "'" : "a tag name was expected";
                return false;
            }

            const std::string_view name = m_Text.substr(start, m_Pos - start);
            std::string why;
            if (!TagRegistry::IsValidName(name, &why)) {
                m_Pos = start;
                m_Error = why;
                return false;
            }

            const Tag tag = TagRegistry::Get().Resolve(name, m_Keep);
            out = exact ? TagQuery::Exact(tag) : TagQuery::Has(tag);
            return true;
        }

        std::string_view m_Text;
        bool m_Keep = true;
        size_t m_Pos = 0;
        std::string m_Error;
    };

    bool TagQuery::TryParse(std::string_view text, TagQuery& out, std::string* error, bool keep)
    {
        std::string why;
        TagQueryParser parser(text, keep);
        if (parser.Run(out, why))
            return true;
        if (error)
            *error = why;
        return false;
    }

    TagQuery TagQuery::Parse(std::string_view text)
    {
        TagQuery q;
        std::string error;
        if (!TryParse(text, q, &error))
            DEBUG_ERROR("Tags : invalid tag query : " + error);
        return q;
    }
}
