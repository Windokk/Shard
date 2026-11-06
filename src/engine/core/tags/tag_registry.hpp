#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "engine/core/tags/tag.hpp"

namespace Shard::Engine::Core {

    enum class TagSource : uint8_t {
        Project = 1 << 0,
        Plugin  = 1 << 1,
        Code    = 1 << 2,
    };

    struct TagDeclaration {
        TagSource source = TagSource::Project;
        std::string owner;
        std::string comment;
    };

    struct TagInfo {
        std::string name;
        Tag tag;
        Tag parent;
        std::vector<Tag> children;
        std::vector<TagDeclaration> declarations;
        uint8_t depth = 1;

        uint8_t Sources() const;
        const std::string& Comment() const;
    };

    // Not thread-safe: declarations change on the main thread, concurrent reads are fine.
    class TagRegistry {
    public:
        static constexpr size_t kMaxDepth = 8;

        static TagRegistry& Get();
        // No-op when `host` is this library's own registry.
        static void Link(TagRegistry* host);

        static bool IsValidName(std::string_view name, std::string* error = nullptr);

        bool Declare(std::string_view name, TagSource source, std::string_view owner = {}, std::string_view comment = {});
        void SetDeclarations(TagSource source, std::string_view owner, const std::vector<std::pair<std::string, std::string>>& tags);
        void RemoveDeclarations(TagSource source, std::string_view owner);

        void SetRedirects(std::map<std::string, std::string> redirects);
        const std::map<std::string, std::string>& GetRedirects() const { return m_Redirects; }
        std::string ApplyRedirects(std::string_view name) const;

        Tag Find(std::string_view name) const;
        Tag Resolve(std::string_view name, bool keep = true);
        void ReportUnknown(std::string_view name);

        bool IsKnown(Tag tag) const { return m_Nodes.count(tag.GetId().Value()) != 0; }
        const TagInfo* GetInfo(Tag tag) const;
        Tag GetParent(Tag tag) const;
        bool Matches(Tag tag, Tag tagOrParent) const;
        std::string NameOf(Tag tag) const;

        std::vector<Tag> GetRoots() const;
        std::vector<Tag> GetAll() const;
        void CollectDescendants(Tag tag, std::vector<Tag>& out, bool includeSelf = true) const;

        uint64_t GetVersion() const { return m_Version; }

        void Clear();

    private:
        struct Entry {
            std::string name;
            TagDeclaration declaration;
        };

        void Rebuild();
        void Remember(const std::string& name);

        std::vector<Entry> m_Entries;
        std::unordered_map<uint64_t, TagInfo> m_Nodes;
        std::unordered_map<uint64_t, std::string> m_Names;
        std::map<std::string, std::string> m_Redirects;
        std::unordered_set<uint64_t> m_ReportedUnknown;
        uint64_t m_Version = 1;
    };
}
