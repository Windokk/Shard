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

    /// Where the declaration of a tag comes from. A tag can be declared by several sources at once.
    enum class TagSource : uint8_t {
        Project = 1 << 0,       ///< the project file ("tags"), edited in Project settings > Tags
        Plugin  = 1 << 1,       ///< a plugin's manifest ("tags")
        Code    = 1 << 2,       ///< SH_DECLARE_TAG
    };

    struct TagDeclaration {
        TagSource source = TagSource::Project;
        std::string owner;      ///< the plugin id for TagSource::Plugin, empty otherwise
        std::string comment;
    };

    /// One node of the tree
    struct TagInfo {
        std::string name;
        Tag tag;
        Tag parent;                                 ///< empty for a root
        std::vector<Tag> children;                  ///< sorted by name
        std::vector<TagDeclaration> declarations;   ///< empty : the tag is only there because a child of it is declared
        uint8_t depth = 1;                          ///< 1 for a root

        /// TagSource bits of the declarations of this tag
        uint8_t Sources() const;
        /// The first comment one of its declarations gives, "" if none does
        const std::string& Comment() const;
    };

    /// @brief The tree of every tag the project knows : the project's own, the loaded plugins', and the ones code declares. Built
    /// again whenever a source changes (the project is loaded or its tags edited, a plugin is loaded or unloaded).
    ///
    /// Not thread-safe : declarations change on the main thread, outside parallel systems ; reading from several threads at once is fine.
    class TagRegistry {
    public:
        /// Levels a name can have ("A.B.C" has 3)
        static constexpr size_t kMaxDepth = 8;

        /// The registry of the executable (in a plugin library, the host's once the library is linked, see Link)
        static TagRegistry& Get();
        /// @brief Points this library at the registry of the executable that loaded it : the tags its code declared while it was
        /// being loaded are declared there. A no-op when `host` is this library's own.
        static void Link(TagRegistry* host);

        /// Segments of A-Z a-z 0-9 _ separated by single dots, at most kMaxDepth of them
        static bool IsValidName(std::string_view name, std::string* error = nullptr);

        // ---- Declarations ----

        /// @brief Declares `name` (its parents are in the tree with it). False (logged) for a name that is not valid.
        bool Declare(std::string_view name, TagSource source, std::string_view owner = {}, std::string_view comment = {});
        /// @brief Replaces everything (source, owner) declared by `tags` (name, comment) : the project's tags, a plugin's.
        /// The names that are not valid are skipped (logged).
        void SetDeclarations(TagSource source, std::string_view owner, const std::vector<std::pair<std::string, std::string>>& tags);
        void RemoveDeclarations(TagSource source, std::string_view owner);

        /// @brief Old name -> new name : a file that names the old one gets the new one (followed transitively, see ApplyRedirects)
        void SetRedirects(std::map<std::string, std::string> redirects);
        const std::map<std::string, std::string>& GetRedirects() const { return m_Redirects; }
        /// The name a redirect sends `name` to, `name` itself when there is none
        std::string ApplyRedirects(std::string_view name) const;

        // ---- Lookup ----

        /// The declared tag of that name (redirects followed), the empty tag otherwise. Logs nothing.
        Tag Find(std::string_view name) const;
        /// @brief For what a file holds : the tag of that name (redirects followed), kept even when nobody declares it so that saving
        /// writes it back (it then matches only itself). An unknown name is reported once. The empty tag for "" or an invalid name.
        /// @param keep false : only look, the name of an unknown tag is neither reported nor remembered (a query being typed)
        Tag Resolve(std::string_view name, bool keep = true);
        /// Logs, once per name, that nobody declares `name`
        void ReportUnknown(std::string_view name);

        bool IsKnown(Tag tag) const { return m_Nodes.count(tag.GetId().Value()) != 0; }
        /// Null for a tag that is not in the tree
        const TagInfo* GetInfo(Tag tag) const;
        Tag GetParent(Tag tag) const;
        /// `tag` is `tagOrParent`, or is under it
        bool Matches(Tag tag, Tag tagOrParent) const;
        /// The text of any tag this registry ever saw (declared, resolved from a file), "" for the empty tag
        std::string NameOf(Tag tag) const;

        /// The tags without a parent, sorted by name
        std::vector<Tag> GetRoots() const;
        /// Every tag of the tree, sorted by name
        std::vector<Tag> GetAll() const;
        /// The tags under `tag` (and `tag` itself when includeSelf, known or not), sorted by name
        void CollectDescendants(Tag tag, std::vector<Tag>& out, bool includeSelf = true) const;

        /// Changes every time the tree does : what a cache of it compares
        uint64_t GetVersion() const { return m_Version; }

        /// Forgets everything (tests)
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
        std::unordered_map<uint64_t, std::string> m_Names;      // every name ever seen : a tag that left the tree keeps its text
        std::map<std::string, std::string> m_Redirects;
        std::unordered_set<uint64_t> m_ReportedUnknown;
        uint64_t m_Version = 1;
    };
}
