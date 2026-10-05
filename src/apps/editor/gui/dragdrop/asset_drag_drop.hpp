#pragma once

#include "imgui/imgui.h"

#include <string>
#include <vector>
#include <cstring>
#include <algorithm>

namespace Shard::Editor::GUI::DragDrop{

    inline constexpr const char* kAssetPayloadType = "SHARD_ASSET_ITEMS";
    inline constexpr int kMaxAssetDragItems = 32;
    inline constexpr int kMaxAssetPathLen = 256;

    // Fixed-size POD payload (ImGui just memcpy's whatever's handed to SetDragDropPayload) carrying
    // each dragged asset's nameInProject : the same key ResourcesManager/AssetIDManager already index
    // everything by (see Asset::nameInProject in asset_browser.hpp), so a receiver can feed it straight
    // into GetTexture/GetMesh/GetIDFromNameInProject without rederiving anything from a raw filesystem path.
    struct AssetDragPayload{
        int count = 0;
        char nameInProject[kMaxAssetDragItems][kMaxAssetPathLen];
    };

    // Call from inside an existing ImGui::BeginDragDropSource()/EndDragDropSource() block.
    inline void SetAssetDragDropPayload(const std::vector<std::string>& namesInProject)
    {
        // static: ImGui re-reads the payload's bytes every frame the drag is held, well after this
        // function returns, so the buffer backing it can't be a stack temporary.
        static AssetDragPayload payload;

        payload.count = std::min((int)namesInProject.size(), kMaxAssetDragItems);
        for (int i = 0; i < payload.count; i++)
        {
            strncpy(payload.nameInProject[i], namesInProject[i].c_str(), kMaxAssetPathLen - 1);
            payload.nameInProject[i][kMaxAssetPathLen - 1] = '\0';
        }

        ImGui::SetDragDropPayload(kAssetPayloadType, &payload, sizeof(AssetDragPayload));
    }

    // Call from inside `if (ImGui::BeginDragDropTarget())`. Returns the dropped asset name(s) (empty
    // if nothing of this payload type was released here this frame).
    inline std::vector<std::string> AcceptAssetDragDropPayload(ImGuiDragDropFlags flags = 0)
    {
        std::vector<std::string> result;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayloadType, flags))
        {
            const AssetDragPayload* data = (const AssetDragPayload*)payload->Data;
            for (int i = 0; i < data->count; i++)
                result.emplace_back(data->nameInProject[i]);
        }

        return result;
    }

    // Peek at an in-flight asset drag without accepting it - lets a source/target preview what's
    // being dragged (e.g. showing "3 assets" under the cursor, or highlighting a compatible target).
    inline std::vector<std::string> PeekAssetDragDropPayload()
    {
        std::vector<std::string> result;

        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        if (payload && payload->IsDataType(kAssetPayloadType))
        {
            const AssetDragPayload* data = (const AssetDragPayload*)payload->Data;
            for (int i = 0; i < data->count; i++)
                result.emplace_back(data->nameInProject[i]);
        }

        return result;
    }
}
