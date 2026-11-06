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

    struct AssetDragPayload{
        int count = 0;
        char nameInProject[kMaxAssetDragItems][kMaxAssetPathLen];
    };

    inline void SetAssetDragDropPayload(const std::vector<std::string>& namesInProject)
    {
        // static: ImGui reads the payload bytes every frame the drag is held, long after this returns.
        static AssetDragPayload payload;

        payload.count = std::min((int)namesInProject.size(), kMaxAssetDragItems);
        for (int i = 0; i < payload.count; i++)
        {
            strncpy(payload.nameInProject[i], namesInProject[i].c_str(), kMaxAssetPathLen - 1);
            payload.nameInProject[i][kMaxAssetPathLen - 1] = '\0';
        }

        ImGui::SetDragDropPayload(kAssetPayloadType, &payload, sizeof(AssetDragPayload));
    }

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
