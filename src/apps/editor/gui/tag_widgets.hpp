#pragma once

#include "engine/core/tags/tag.hpp"
#include "engine/core/tags/tag_container.hpp"
#include "engine/core/tags/tag_query.hpp"

namespace Shard::Editor::GUI::TagWidgets{

    bool TagContainerEditor(const char* id, Engine::Core::TagContainer& tags);

    bool TagPicker(const char* id, Engine::Core::Tag& tag);

    bool TagQueryInput(const char* id, Engine::Core::TagQuery& query);

    bool TagTree(Engine::Core::Tag& picked, const Engine::Core::TagContainer* marked = nullptr);
}
