#include "apps/editor/gui/notifications.hpp"

#include "apps/editor/gui/ImGuiNotify.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>
#include <utility>

namespace Shard::Editor::GUI::Notifications
{
    namespace
    {
        // ImGuiNotify auto-dismisses after NOTIFY_DEFAULT_DISMISS ms. A progress toast is inserted
        // with this much longer window AND has its creation clock refreshed on every UpdateProgress(),
        // so in practice it only ever disappears when EndProgress() shortens it. This value is just the
        // safety net for a caller that starts a progress toast and then goes silent forever.
        constexpr int kStickyDismissMs = 60 * 1000;

        struct ProgressEntry
        {
            int         toastId  = -1;   // id inside ImGuiNotify, or -1 once the toast has expired
            std::string title;
            std::string message;
            float       progress = 0.0f;
        };

        std::unordered_map<ProgressId, ProgressEntry> g_progress;
        ProgressId                                    g_nextProgressId = 1;

        void PushSimple(ImGuiToastType type, const char* fmt, va_list args)
        {
            char buffer[1024];
            vsnprintf(buffer, sizeof(buffer), fmt, args);

            // Pass a const char* so overload resolution can't pick ImGuiToast's private
            // setContent(const char*, va_list) - on the MS x64 ABI va_list is char*, which a plain
            // char[] argument would bind to.
            const char* message = buffer;

            ImGuiToast toast(type, NOTIFY_DEFAULT_DISMISS);
            toast.setContent("%s", message);
            ImGui::InsertNotification(toast);
        }

        // Returns the live toast backing `entry`, recreating it if it expired while the caller was
        // between updates (e.g. a long stall with no UpdateProgress() calls). Never returns nullptr.
        ImGuiToast* ResolveToast(ProgressEntry& entry)
        {
            if (ImGuiToast* toast = ImGui::FindNotification(entry.toastId))
                return toast;

            ImGuiToast recreated(ImGuiToastType::Info, kStickyDismissMs);
            recreated.setTitle("%s", entry.title.c_str());
            recreated.setContent("%s", entry.message.c_str());
            recreated.setProgress(entry.progress);
            entry.toastId = ImGui::InsertNotificationWithId(recreated);
            return ImGui::FindNotification(entry.toastId);
        }
    }

    void RenderFrame()
    {
        ImGui::RenderNotifications();
    }

    void Info(const char* fmt, ...)    { va_list a; va_start(a, fmt); PushSimple(ImGuiToastType::Info, fmt, a);    va_end(a); }
    void Success(const char* fmt, ...) { va_list a; va_start(a, fmt); PushSimple(ImGuiToastType::Success, fmt, a); va_end(a); }
    void Warning(const char* fmt, ...) { va_list a; va_start(a, fmt); PushSimple(ImGuiToastType::Warning, fmt, a); va_end(a); }
    void Error(const char* fmt, ...)   { va_list a; va_start(a, fmt); PushSimple(ImGuiToastType::Error, fmt, a);   va_end(a); }

    ProgressId BeginProgress(const std::string& title, const std::string& message)
    {
        const ProgressId id = g_nextProgressId++;

        ProgressEntry entry;
        entry.title   = title;
        entry.message = message;
        entry.progress = 0.0f;

        ImGuiToast toast(ImGuiToastType::Info, kStickyDismissMs);
        toast.setTitle("%s", title.c_str());
        if (!message.empty())
            toast.setContent("%s", message.c_str());
        toast.setProgress(0.0f);
        entry.toastId = ImGui::InsertNotificationWithId(toast);

        g_progress.emplace(id, std::move(entry));
        return id;
    }

    void UpdateProgress(ProgressId id, float progress01, const std::string& message)
    {
        const auto it = g_progress.find(id);
        if (it == g_progress.end())
            return;

        ProgressEntry& entry = it->second;
        entry.progress = std::clamp(progress01, 0.0f, 1.0f);
        if (!message.empty())
            entry.message = message;

        ImGuiToast* toast = ResolveToast(entry);
        toast->setProgress(entry.progress);
        toast->setContent("%s", entry.message.c_str());
        toast->setDismissTime(kStickyDismissMs);
        toast->refreshCreationTime();
    }

    void EndProgress(ProgressId id, bool success, const std::string& message)
    {
        const auto it = g_progress.find(id);
        if (it == g_progress.end())
            return;

        ProgressEntry& entry = it->second;

        ImGuiToast* toast = ResolveToast(entry);
        toast->setType(success ? ImGuiToastType::Success : ImGuiToastType::Error);
        toast->setProgress(success ? 1.0f : -1.0f);
        toast->setContent("%s", message.empty() ? entry.message.c_str() : message.c_str());
        toast->setDismissTime(NOTIFY_DEFAULT_DISMISS);
        toast->refreshCreationTime();

        g_progress.erase(it);
    }
}
