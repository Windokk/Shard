#pragma once

#include <cstdint>
#include <string>

// Wrapper over ImGuiNotify, adds sticky progress toasts. UI thread only, between NewFrame and Render. RenderFrame() once per frame.
namespace Shard::Editor::GUI::Notifications
{
    void RenderFrame();

    void Info(const char* fmt, ...);
    void Success(const char* fmt, ...);
    void Warning(const char* fmt, ...);
    void Error(const char* fmt, ...);

    using ProgressId = uint32_t;

    ProgressId BeginProgress(const std::string& title, const std::string& message = std::string());

    void UpdateProgress(ProgressId id, float progress01, const std::string& message = std::string());

    void EndProgress(ProgressId id, bool success = true, const std::string& message = std::string());
}
