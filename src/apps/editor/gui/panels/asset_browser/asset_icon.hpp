#pragma once

#include "engine/assets/vfs/filesystem.hpp"

#include <imgui.h>

namespace Shard::Editor::GUI {

    void DrawAssetIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max, Engine::Filesystem::Type type, bool isDirectory, ImU32 accent,
                       const char* extension);
}
