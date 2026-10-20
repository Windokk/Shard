#pragma once

#include "engine/core/tags/tag.hpp"
#include "engine/core/tags/tag_container.hpp"
#include "engine/core/tags/tag_query.hpp"

namespace Shard::Editor::GUI::TagWidgets{

    /// @brief The tags as chips (click one to remove it), then a + that opens the tree of the project's tags to add one. Takes
    /// the rest of the line, and more lines when the chips need them. True when `tags` changed.
    bool TagContainerEditor(const char* id, Engine::Core::TagContainer& tags);

    /// @brief One tag : a button with its name, that opens the tree to pick another (or none). True when `tag` changed.
    bool TagPicker(const char* id, Engine::Core::Tag& tag);

    /// @brief A query typed as text (`Enemy & !Enemy.Boss`), checked as it is typed and applied when the field is left (or Enter is
    /// pressed) if it is valid. The + next to it inserts the name of a tag picked in the tree. True when `query` changed.
    bool TagQueryInput(const char* id, Engine::Core::TagQuery& query);

    /// @brief The tree of the project's tags with a search box, for the inside of a popup. True when a tag was clicked (it is in `picked`).
    /// @param marked the tags shown as already chosen (may be null)
    bool TagTree(Engine::Core::Tag& picked, const Engine::Core::TagContainer* marked = nullptr);
}
