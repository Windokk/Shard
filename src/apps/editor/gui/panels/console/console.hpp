#pragma once

#include "engine/core/diagnostics/logger.hpp"
#include "engine/core/config/cvar.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

struct ImGuiInputTextCallbackData;

namespace Shard::Editor::Core{

    class EditorMainWindow;
}

namespace Shard::Editor::GUI{

    /// The console : the log of the engine, and a command line under it (commands and variables of engine/core/config/cvar.hpp).
    class Console
    {
        public:

            Console() = default;
            ~Console();

            void SetParentWindow(Core::EditorMainWindow* parent);
            void Draw();

            /// Puts the keyboard focus in the command line, whatever has it now
            void FocusCommandLine() { m_FocusInput = true; m_FocusWindow = true; }

            void Clear();

        private:

            using Suggestion = Engine::Core::CVarRegistry::Suggestion;

            enum class Kind : uint8_t {
                Log,            ///< what the engine logged
                Command,        ///< a line the user ran
                Output,         ///< what the command answered
                OutputError     ///< what the command refused with
            };

            struct Entry {
                uint64_t id = 0;
                Kind kind = Kind::Log;
                Engine::Debugging::Level level = Engine::Debugging::Level::Log;
                bool continuation = false;      ///< another line of the same message : drawn without time and badge
                std::string time;
                std::string source;             ///< "file.cpp:123" for what the engine logged
                std::string text;
                std::string lower;              ///< text, lowercase, for the filter
                int repeat = 1;                 ///< how many times in a row
            };

            struct PendingLog {
                Engine::Debugging::Level level;
                std::string text;
            };

            // ---- Log ----
            void AddEntry(Kind kind, Engine::Debugging::Level level, const std::string& text, const std::string& source = {});
            void AddMessage(Kind kind, Engine::Debugging::Level level, const std::string& message, const std::string& source);
            void DrainPending();
            void DrawToolbar();
            void DrawLog(float footerHeight);
            bool Passes(const Entry& entry) const;
            void CopyEntries(const std::vector<const Entry*>& entries) const;
            std::string FormatForCopy(const Entry& entry) const;

            // ---- Command line ----
            void DrawCommandLine();
            void DrawSuggestions(float x, float y, float width);
            void DrawHint();
            void RunCommandLine();
            void RegisterEditorCommands();

            static int InputCallback(ImGuiInputTextCallbackData* data);
            void OnInput(ImGuiInputTextCallbackData* data);
            void RefreshSuggestions(const char* text, int cursor);
            void AcceptSuggestion(ImGuiInputTextCallbackData* data, int index);
            void AcceptSuggestionInBuffer(int index);
            void BrowseHistory(ImGuiInputTextCallbackData* data, bool up);
            void CloseSuggestions();

            static void DrawShortcutsHelp();

            Core::EditorMainWindow* m_Parent = nullptr;

            // The logger calls the sink from any thread : lines wait here until the next frame. Shared with the sink, which
            // the logger keeps for as long as it lives.
            struct SharedLog {
                std::mutex mutex;
                std::vector<PendingLog> pending;
                bool alive = true;
            };
            std::shared_ptr<SharedLog> m_Shared = std::make_shared<SharedLog>();

            std::deque<Entry> m_Entries;
            uint64_t m_NextId = 1;
            size_t m_MaxEntries = 5000;
            int m_Counts[5] = {0, 0, 0, 0, 0};

            // Filters and view
            bool m_ShowLevel[5] = {true, true, true, true, true};
            char m_Filter[128] = "";
            bool m_ShowTime = true;
            bool m_ShowSource = false;
            bool m_AutoScroll = true;
            bool m_FocusFilter = false;
            bool m_ScrollToBottom = false;
            float m_ScrollDelta = 0.0f;
            int m_ScrollJump = 0;               // -1 : to the top, 1 : to the bottom
            int m_NewSinceBottom = 0;

            // Selection in the log
            std::set<uint64_t> m_Selected;
            uint64_t m_Anchor = 0;
            std::vector<const Entry*> m_Visible;    // the lines the filters keep, this frame

            // Command line
            char m_Input[1024] = "";
            std::vector<std::string> m_History;
            int m_HistoryPos = -1;              // -1 : typing a new line
            std::string m_HistoryDraft;         // what was typed before the history was browsed
            bool m_FocusInput = false;
            bool m_FocusWindow = false;
            bool m_SwallowEnter = false;        // Enter picked a suggestion : it must not run the line as well
            bool m_ReplaceBuffer = false;       // m_Input was changed from outside, the widget has to take it
            bool m_CursorToEnd = false;

            // Suggestions
            std::vector<Suggestion> m_Suggestions;
            std::vector<std::string> m_HistoryMatches;  // in history-search mode
            int m_Selection = 0;
            bool m_SelectionExplicit = false;   // the user moved in the menu : Enter picks it instead of running the line
            bool m_Suppressed = false;          // closed with Escape, until the text changes
            bool m_HistorySearch = false;       // Ctrl+R
            int m_WordStart = 0;
            int m_WordEnd = 0;
            int m_WordIndex = 0;                // which word of the line is being completed
            std::string m_WordText;
            std::string m_LastText;
            int m_PendingAccept = -1;           // a click on a suggestion, applied at the next frame
            std::string m_PendingBuffer;        // the new content of the command line when m_ReplaceBuffer is set
            int m_PopupTop = 0;                 // first suggestion shown
            float m_PopupMinX = 0, m_PopupMinY = 0, m_PopupMaxX = 0, m_PopupMaxY = 0;
            bool m_MouseOverPopup = false;
            bool m_WasAtBottom = true;
            float m_InputX = 0.0f;
            float m_InputY = 0.0f;
            float m_InputWidth = 0.0f;
            bool m_InputActive = false;
    };

}
