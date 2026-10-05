#pragma once

#include <cstdint>
#include <string>

// Editor-facing wrapper around ImGuiNotify (editor/gui/ImGuiNotify.hpp). Keeps the raw
// ImGuiToast/InsertNotification plumbing out of feature code and adds the piece ImGuiNotify has no
// concept of on its own : a sticky notification with a live progress bar that you drive over the
// course of a long operation (BeginProgress -> UpdateProgress* -> EndProgress).
//
// All functions must be called from the UI thread, between ImGui::NewFrame() and ImGui::Render().
// RenderFrame() has to be called exactly once per frame for anything to show up.
namespace Shard::Editor::GUI::Notifications
{
    // Draw every live notification. Call once per frame, after all other ImGui windows.
    void RenderFrame();

    // Fire-and-forget toasts (auto-dismiss after a few seconds). printf-style formatting.
    void Info(const char* fmt, ...);
    void Success(const char* fmt, ...);
    void Warning(const char* fmt, ...);
    void Error(const char* fmt, ...);

    // Handle to an in-progress notification. 0 is never a valid handle.
    using ProgressId = uint32_t;

    // Open a sticky notification with a 0% progress bar and return its handle. It stays on screen
    // (kept alive frame to frame) until EndProgress() is called for that handle.
    ProgressId BeginProgress(const std::string& title, const std::string& message = std::string());

    // Move the bar for an open progress notification. progress01 is clamped to [0, 1]. An empty
    // message leaves the current line untouched; a non-empty one replaces it (e.g. "Building BVH - 42%").
    // A stale / unknown handle is ignored.
    void UpdateProgress(ProgressId id, float progress01, const std::string& message = std::string());

    // Close an open progress notification, converting it into a short-lived success or error toast
    // that fades on its own. A stale / unknown handle is ignored.
    void EndProgress(ProgressId id, bool success = true, const std::string& message = std::string());
}
