#include "console.hpp"

#include "apps/editor/gui/main_window.hpp"

#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

namespace Shard::Editor::GUI{

    using Engine::Debugging::Level;
    using Engine::Core::CVarRegistry;

    namespace {

        constexpr int kLevelCount = 5;
        const char* const kLevelNames[kLevelCount] = {"Log", "Info", "Warn", "Error", "Fatal"};
        const char* const kLevelBadges[kLevelCount] = {"[LOG]  ", "[INFO] ", "[WARN] ", "[ERROR]", "[FATAL]"};

        const ImVec4 kLevelColors[kLevelCount] = {
            ImVec4(0.80f, 0.80f, 0.84f, 1.0f),
            ImVec4(0.55f, 0.82f, 1.00f, 1.0f),
            ImVec4(1.00f, 0.80f, 0.30f, 1.0f),
            ImVec4(1.00f, 0.42f, 0.42f, 1.0f),
            ImVec4(1.00f, 0.25f, 0.25f, 1.0f),
        };
        const ImVec4 kDim(0.46f, 0.47f, 0.52f, 1.0f);
        const ImVec4 kCommandColor(0.55f, 1.00f, 0.62f, 1.0f);
        const ImVec4 kOutputColor(0.86f, 0.90f, 0.96f, 1.0f);
        const ImVec4 kCommandTag(1.00f, 0.74f, 0.35f, 1.0f);
        const ImVec4 kVariableTag(0.50f, 0.85f, 1.00f, 1.0f);

        std::string Lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        }

        std::string NowAsText()
        {
            const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm local{};
#if defined(_WIN32)
            localtime_s(&local, &now);
#else
            localtime_r(&now, &local);
#endif
            char buffer[16];
            std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
            return buffer;
        }

        ImU32 Color(const ImVec4& c, float alpha = 1.0f)
        {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
        }

        int LevelIndex(Level level) { return std::clamp(static_cast<int>(level), 0, kLevelCount - 1); }

        void SplitLogLine(const std::string& line, std::string& source, std::string& message)
        {
            size_t start = 0;
            if (line.size() > 2 && line[0] == '[') {
                const size_t close = line.find(']');
                if (close != std::string::npos)
                    start = close + 1;
                while (start < line.size() && line[start] == ' ')
                    ++start;
            }

            const size_t open = line.find('(', start);
            if (open != std::string::npos && open - start < 48) {
                const size_t close = line.find(") ", open);
                if (close != std::string::npos) {
                    const std::string location = line.substr(open + 1, close - open - 1);
                    const size_t colon = location.rfind(':');
                    bool looksLikeLocation = colon != std::string::npos && colon + 1 < location.size() && location.find(' ') == std::string::npos;
                    for (size_t i = colon + 1; looksLikeLocation && i < location.size(); ++i)
                        looksLikeLocation = std::isdigit(static_cast<unsigned char>(location[i])) != 0;
                    if (looksLikeLocation) {
                        source = location;
                        message = line.substr(close + 2);
                        return;
                    }
                }
            }
            message = line.substr(start);
        }

        std::vector<std::string> SplitLines(const std::string& text)
        {
            std::vector<std::string> lines;
            size_t begin = 0;
            while (begin <= text.size()) {
                size_t end = text.find('\n', begin);
                if (end == std::string::npos) end = text.size();
                std::string line = text.substr(begin, end - begin);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(std::move(line));
                begin = end + 1;
            }
            while (lines.size() > 1 && lines.back().empty()) lines.pop_back();
            return lines;
        }

        bool IsSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
    }

    Console::~Console()
    {
        {
            std::lock_guard<std::mutex> lock(m_Shared->mutex);
            m_Shared->alive = false;
        }

        CVarRegistry& registry = CVarRegistry::Global();
        for (const char* name : {"clear", "history", "quit", "exit"})
            registry.UnregisterCommand(name);
    }

    void Console::SetParentWindow(Core::EditorMainWindow* parent)
    {
        m_Parent = parent;

        std::shared_ptr<SharedLog> shared = m_Shared;
        Engine::Debugging::GetLogger().AddSink([shared](const Level& level, const std::string& message) {
            std::lock_guard<std::mutex> lock(shared->mutex);
            if (shared->alive)
                shared->pending.push_back({level, message});
        });

        RegisterEditorCommands();
    }

    void Console::RegisterEditorCommands()
    {
        CVarRegistry& registry = CVarRegistry::Global();

        registry.RegisterCommand("clear", "clear : empties the console", [this](const std::vector<std::string>&) {
            Clear();
            return Engine::Core::CommandResult{true, {}};
        });

        registry.RegisterCommand("history", "history : the lines you ran", [this](const std::vector<std::string>&) {
            if (m_History.empty())
                return Engine::Core::CommandResult{true, "Nothing was run yet"};
            std::string out;
            for (size_t i = 0; i < m_History.size(); ++i)
                out += std::to_string(i + 1) + "  " + m_History[i] + (i + 1 < m_History.size() ? "\n" : "");
            return Engine::Core::CommandResult{true, out};
        });

        auto quit = [this](const std::vector<std::string>&) {
            if (m_Parent)
                m_Parent->RequestExit();
            return Engine::Core::CommandResult{true, "Closing the editor..."};
        };
        registry.RegisterCommand("quit", "quit : closes the editor", quit);
        registry.RegisterCommand("exit", "exit : closes the editor", quit);
    }

    void Console::Clear()
    {
        m_Entries.clear();
        m_Selected.clear();
        m_Anchor = 0;
        for (int& count : m_Counts) count = 0;
        m_NewSinceBottom = 0;
    }

    void Console::AddEntry(Kind kind, Level level, const std::string& text, const std::string& source)
    {
        Entry entry;
        entry.id = m_NextId++;
        entry.kind = kind;
        entry.level = level;
        entry.time = NowAsText();
        entry.source = source;
        entry.text = text;
        entry.lower = Lower(text);
        m_Entries.push_back(std::move(entry));

        if (kind == Kind::Log)
            m_Counts[LevelIndex(level)]++;
        if (!m_WasAtBottom)
            m_NewSinceBottom++;

        while (m_Entries.size() > m_MaxEntries) {
            const Entry& oldest = m_Entries.front();
            if (oldest.kind == Kind::Log && !oldest.continuation)
                m_Counts[LevelIndex(oldest.level)] = std::max(0, m_Counts[LevelIndex(oldest.level)] - oldest.repeat);
            m_Selected.erase(oldest.id);
            m_Entries.pop_front();
        }
    }

    void Console::AddMessage(Kind kind, Level level, const std::string& message, const std::string& source)
    {
        const std::vector<std::string> lines = SplitLines(message);

        if (lines.size() == 1 && kind == Kind::Log && !m_Entries.empty()) {
            Entry& last = m_Entries.back();
            if (last.kind == Kind::Log && !last.continuation && last.level == level && last.text == lines[0] && last.source == source) {
                last.repeat++;
                last.time = NowAsText();
                m_Counts[LevelIndex(level)]++;
                return;
            }
        }

        for (size_t i = 0; i < lines.size(); ++i) {
            AddEntry(kind, level, lines[i], i == 0 ? source : std::string());
            m_Entries.back().continuation = i > 0;
        }
    }

    void Console::DrainPending()
    {
        std::vector<PendingLog> pending;
        {
            std::lock_guard<std::mutex> lock(m_Shared->mutex);
            pending.swap(m_Shared->pending);
        }

        for (const PendingLog& log : pending) {
            std::string source, message;
            SplitLogLine(log.text, source, message);
            AddMessage(Kind::Log, log.level, message, source);
        }
    }

    bool Console::Passes(const Entry& entry) const
    {
        if (entry.kind == Kind::Log && !m_ShowLevel[LevelIndex(entry.level)])
            return false;

        if (m_Filter[0] != '\0') {
            const std::string filter = Lower(m_Filter);
            if (entry.lower.find(filter) == std::string::npos && Lower(entry.source).find(filter) == std::string::npos)
                return false;
        }
        return true;
    }

    std::string Console::FormatForCopy(const Entry& entry) const
    {
        std::string line;
        if (!entry.continuation) {
            line += "[" + entry.time + "] ";
            if (entry.kind == Kind::Log)
                line += std::string(kLevelBadges[LevelIndex(entry.level)]) + " ";
        }
        line += entry.text;
        if (entry.repeat > 1)
            line += " (x" + std::to_string(entry.repeat) + ")";
        if (!entry.source.empty())
            line += "  (" + entry.source + ")";
        return line;
    }

    void Console::CopyEntries(const std::vector<const Entry*>& entries) const
    {
        std::string text;
        for (const Entry* entry : entries)
            text += FormatForCopy(*entry) + "\n";
        ImGui::SetClipboardText(text.c_str());
    }

    void Console::DrawShortcutsHelp()
    {
        ImGui::TextUnformatted("Command line");
        ImGui::Separator();
        ImGui::BulletText("Tab : take the highlighted suggestion");
        ImGui::BulletText("Up / Down : move in the suggestions, or in the history");
        ImGui::BulletText("Enter : run (or take the suggestion you moved to)");
        ImGui::BulletText("Esc : clear the line");
        ImGui::BulletText("Ctrl+R : search the history");
        ImGui::BulletText("` (the key under Esc) : jump to the command line");
        ImGui::Spacing();
        ImGui::TextUnformatted("Log");
        ImGui::Separator();
        ImGui::BulletText("Ctrl+L : clear        Ctrl+F : filter");
        ImGui::BulletText("PageUp / PageDown : scroll   Ctrl+PageUp / PageDown : top / bottom");
        ImGui::BulletText("Click : select   Ctrl / Shift+click : several lines");
        ImGui::BulletText("Ctrl+C : copy the selection   Ctrl+A : select all");
        ImGui::BulletText("Right click : menu   Double click on a command : run it again");
    }

    void Console::DrawToolbar()
    {
        if (ImGui::Button("Clear"))
            Clear();
        ImGui::SameLine();
        if (ImGui::Button("Copy")) {
            std::vector<const Entry*> visible;
            for (const Entry& entry : m_Entries)
                if (Passes(entry))
                    visible.push_back(&entry);
            CopyEntries(visible);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copies the lines the filters keep");

        ImGui::SameLine();
        ImGui::TextDisabled("|");

        for (int i = 0; i < kLevelCount; ++i) {
            ImGui::SameLine();
            const ImVec4 base = kLevelColors[i];
            const bool on = m_ShowLevel[i];
            ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(base.x * 0.35f, base.y * 0.35f, base.z * 0.35f, 1.0f) : ImVec4(0.16f, 0.16f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(base.x * 0.5f, base.y * 0.5f, base.z * 0.5f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(base.x * 0.65f, base.y * 0.65f, base.z * 0.65f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, on ? base : kDim);
            const std::string label = std::string(kLevelNames[i]) + " " + std::to_string(m_Counts[i]) + "##level" + std::to_string(i);
            if (ImGui::Button(label.c_str()))
                m_ShowLevel[i] = !m_ShowLevel[i];
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Show / hide the %s lines", kLevelNames[i]);
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (m_FocusFilter) {
            ImGui::SetKeyboardFocusHere();
            m_FocusFilter = false;
        }
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - 90.0f));
        ImGui::InputTextWithHint("##consoleFilter", "Filter (Ctrl+F)", m_Filter, sizeof(m_Filter), ImGuiInputTextFlags_EscapeClearsAll);

        ImGui::SameLine();
        if (ImGui::Button("Options"))
            ImGui::OpenPopup("##consoleOptions");
        if (ImGui::BeginPopup("##consoleOptions")) {
            ImGui::Checkbox("Timestamps", &m_ShowTime);
            ImGui::Checkbox("Source file and line", &m_ShowSource);
            ImGui::Checkbox("Follow the newest line", &m_AutoScroll);
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("?");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            DrawShortcutsHelp();
            ImGui::EndTooltip();
        }
    }

    void Console::DrawLog(float footerHeight)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.078f, 0.09f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
        ImGui::BeginChild("##consoleLog", ImVec2(0, -footerHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_None);

        m_Visible.clear();
        for (const Entry& entry : m_Entries)
            if (Passes(entry))
                m_Visible.push_back(&entry);

        const float textHeight = ImGui::GetTextLineHeight();
        const float rowHeight = textHeight + 5.0f;
        const float timeWidth = ImGui::CalcTextSize("00:00:00").x + 10.0f;
        const float badgeWidth = ImGui::CalcTextSize("[ERROR]").x + 8.0f;
        const ImGuiIO& io = ImGui::GetIO();

        ImDrawList* draw = ImGui::GetWindowDrawList();
        bool menuRequested = false;

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_Visible.size()), rowHeight);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const Entry& entry = *m_Visible[static_cast<size_t>(i)];
                const bool selected = m_Selected.count(entry.id) != 0;

                const ImVec2 origin = ImGui::GetCursorScreenPos();
                const float width = std::max(ImGui::GetContentRegionAvail().x, 50.0f);

                if (entry.kind == Kind::Log && (entry.level == Level::Error || entry.level == Level::Fatal))
                    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + rowHeight), Color(kLevelColors[LevelIndex(entry.level)], 0.10f));
                else if (i & 1)
                    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + rowHeight), Color(ImVec4(1, 1, 1, 1), 0.025f));

                ImGui::PushID(static_cast<int>(entry.id & 0x7fffffff));
                const bool clicked = ImGui::Selectable("##row", selected, ImGuiSelectableFlags_None, ImVec2(0, rowHeight));
                const bool hovered = ImGui::IsItemHovered() && !m_MouseOverPopup;

                if (clicked && !m_MouseOverPopup) {
                    if (io.KeyShift && m_Anchor != 0) {
                        int from = -1;
                        for (size_t k = 0; k < m_Visible.size(); ++k)
                            if (m_Visible[k]->id == m_Anchor) { from = static_cast<int>(k); break; }
                        if (from >= 0) {
                            if (!io.KeyCtrl) m_Selected.clear();
                            for (int k = std::min(from, i); k <= std::max(from, i); ++k)
                                m_Selected.insert(m_Visible[static_cast<size_t>(k)]->id);
                        }
                    } else if (io.KeyCtrl) {
                        if (selected) m_Selected.erase(entry.id); else m_Selected.insert(entry.id);
                        m_Anchor = entry.id;
                    } else {
                        m_Selected.clear();
                        m_Selected.insert(entry.id);
                        m_Anchor = entry.id;
                    }
                }

                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) {
                    m_Selected.clear();
                    m_Selected.insert(entry.id);
                    m_Anchor = entry.id;
                }

                if (hovered && entry.kind == Kind::Command && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    m_PendingBuffer = entry.text.size() >= 2 ? entry.text.substr(2) : entry.text;
                    m_ReplaceBuffer = true;
                    m_FocusInput = true;
                    m_CursorToEnd = true;
                }

                if (ImGui::BeginPopupContextItem("##rowMenu")) {
                    menuRequested = true;
                    if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                        std::vector<const Entry*> chosen;
                        for (const Entry* e : m_Visible)
                            if (m_Selected.count(e->id)) chosen.push_back(e);
                        CopyEntries(chosen);
                    }
                    if (ImGui::MenuItem("Copy all visible lines")) CopyEntries(m_Visible);
                    if (ImGui::MenuItem("Select all", "Ctrl+A")) {
                        m_Selected.clear();
                        for (const Entry* e : m_Visible) m_Selected.insert(e->id);
                    }
                    ImGui::Separator();
                    if (entry.kind == Kind::Command && ImGui::MenuItem("Run again")) {
                        std::snprintf(m_Input, sizeof(m_Input), "%s", entry.text.size() >= 2 ? entry.text.c_str() + 2 : entry.text.c_str());
                        RunCommandLine();
                    }
                    if (entry.kind == Kind::Log) {
                        const int index = LevelIndex(entry.level);
                        if (ImGui::MenuItem((std::string("Show only ") + kLevelNames[index] + " lines").c_str())) {
                            for (int k = 0; k < kLevelCount; ++k) m_ShowLevel[k] = (k == index);
                        }
                        if (ImGui::MenuItem("Show all levels"))
                            for (bool& show : m_ShowLevel) show = true;
                    }
                    if (ImGui::MenuItem("Clear the console", "Ctrl+L")) Clear();
                    ImGui::EndPopup();
                }
                ImGui::PopID();

                const float y = origin.y + (rowHeight - textHeight) * 0.5f;
                float x = origin.x + 6.0f;

                draw->PushClipRect(origin, ImVec2(origin.x + width, origin.y + rowHeight), true);

                ImVec4 textColor = kOutputColor;
                if (entry.kind == Kind::Log)
                    textColor = kLevelColors[LevelIndex(entry.level)];
                else if (entry.kind == Kind::Command)
                    textColor = kCommandColor;
                else if (entry.kind == Kind::OutputError)
                    textColor = kLevelColors[3];

                if (!entry.continuation) {
                    if (m_ShowTime) {
                        draw->AddText(ImVec2(x, y), Color(kDim), entry.time.c_str());
                        x += timeWidth;
                    }
                    if (entry.kind == Kind::Log) {
                        draw->AddText(ImVec2(x, y), Color(textColor), kLevelBadges[LevelIndex(entry.level)]);
                        x += badgeWidth;
                    }
                } else {
                    x += (m_ShowTime ? timeWidth : 0.0f) + badgeWidth;
                }

                draw->AddText(ImVec2(x, y), Color(textColor), entry.text.c_str());
                float endX = x + ImGui::CalcTextSize(entry.text.c_str()).x;

                if (entry.repeat > 1) {
                    const std::string count = "  x" + std::to_string(entry.repeat);
                    draw->AddText(ImVec2(endX, y), Color(kDim), count.c_str());
                    endX += ImGui::CalcTextSize(count.c_str()).x;
                }

                if (m_ShowSource && !entry.source.empty()) {
                    const float sourceWidth = ImGui::CalcTextSize(entry.source.c_str()).x;
                    draw->AddText(ImVec2(origin.x + width - sourceWidth - 8.0f, y), Color(kDim), entry.source.c_str());
                }

                draw->PopClipRect();

                if (hovered && endX > origin.x + width - 4.0f) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
                    ImGui::TextUnformatted(entry.text.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
        }
        clipper.End();

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
                std::vector<const Entry*> chosen;
                for (const Entry* e : m_Visible)
                    if (m_Selected.count(e->id)) chosen.push_back(e);
                CopyEntries(chosen);
            }
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) {
                m_Selected.clear();
                for (const Entry* e : m_Visible) m_Selected.insert(e->id);
            }
        }

        if (m_ScrollDelta != 0.0f) {
            ImGui::SetScrollY(ImGui::GetScrollY() + m_ScrollDelta);
            m_ScrollDelta = 0.0f;
        }
        if (m_ScrollJump < 0)
            ImGui::SetScrollY(0.0f);
        const bool stickToBottom = m_ScrollToBottom || m_ScrollJump > 0 || (m_AutoScroll && m_WasAtBottom);
        if (stickToBottom) {
            ImGui::SetScrollHereY(1.0f);
            m_NewSinceBottom = 0;
        }
        m_ScrollToBottom = false;
        m_ScrollJump = 0;
        m_WasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f || stickToBottom;
        if (m_WasAtBottom)
            m_NewSinceBottom = 0;

        if (!m_WasAtBottom && m_NewSinceBottom > 0) {
            const std::string label = "New lines : " + std::to_string(m_NewSinceBottom) + "  (go to the bottom)";
            const float buttonWidth = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - buttonWidth - 18.0f, ImGui::GetScrollY() + ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - 6.0f));
            if (ImGui::Button(label.c_str())) {
                m_ScrollToBottom = true;
            }
        }
        (void)menuRequested;

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    void Console::CloseSuggestions()
    {
        m_Suggestions.clear();
        m_HistoryMatches.clear();
        m_Selection = 0;
        m_SelectionExplicit = false;
    }

    void Console::RefreshSuggestions(const char* text, int cursor)
    {
        const std::string line(text);

        if (line != m_LastText) {
            m_LastText = line;
            m_Selection = 0;
            m_SelectionExplicit = false;
            m_Suppressed = false;
            m_PopupTop = 0;
        }

        cursor = std::clamp(cursor, 0, static_cast<int>(line.size()));

        int start = cursor;
        while (start > 0 && !IsSpace(line[static_cast<size_t>(start - 1)])) --start;
        int end = cursor;
        while (end < static_cast<int>(line.size()) && !IsSpace(line[static_cast<size_t>(end)])) ++end;

        m_WordStart = start;
        m_WordEnd = end;
        m_WordText = line.substr(static_cast<size_t>(start), static_cast<size_t>(cursor - start));

        int wordIndex = 0;
        for (int i = 0; i < start;) {
            while (i < start && IsSpace(line[static_cast<size_t>(i)])) ++i;
            if (i >= start) break;
            while (i < start && !IsSpace(line[static_cast<size_t>(i)])) ++i;
            ++wordIndex;
        }
        m_WordIndex = wordIndex;

        m_Suggestions.clear();
        m_HistoryMatches.clear();

        if (m_HistorySearch) {
            const std::string needle = Lower(line);
            for (size_t i = m_History.size(); i-- > 0;)
                if (needle.empty() || Lower(m_History[i]).find(needle) != std::string::npos)
                    m_HistoryMatches.push_back(m_History[i]);
            if (m_HistoryMatches.size() > 40) m_HistoryMatches.resize(40);
        }
        else if (!m_Suppressed) {
            CVarRegistry& registry = CVarRegistry::Global();

            if (wordIndex == 0) {
                if (!m_WordText.empty())
                    m_Suggestions = registry.Suggest(m_WordText, 40);
            }
            else if (wordIndex == 1) {
                size_t b = 0;
                while (b < line.size() && IsSpace(line[b])) ++b;
                size_t e = b;
                while (e < line.size() && !IsSpace(line[e])) ++e;
                const std::string command = Lower(line.substr(b, e - b));

                if (command == "get" || command == "set" || command == "reset" || command == "toggle")
                    m_Suggestions = registry.Suggest(m_WordText, 40, false, true);
                else if (command == "help" || command == "find")
                    m_Suggestions = registry.Suggest(m_WordText, 40, true, true);
            }

            if (m_Suggestions.size() == 1 && Lower(m_Suggestions[0].name) == Lower(m_WordText))
                m_Suggestions.clear();
        }

        const int count = m_HistorySearch ? static_cast<int>(m_HistoryMatches.size()) : static_cast<int>(m_Suggestions.size());
        m_Selection = count > 0 ? std::clamp(m_Selection, 0, count - 1) : 0;
    }

    void Console::AcceptSuggestion(ImGuiInputTextCallbackData* data, int index)
    {
        if (m_HistorySearch) {
            if (index < 0 || index >= static_cast<int>(m_HistoryMatches.size()))
                return;
            const std::string chosen = m_HistoryMatches[static_cast<size_t>(index)];
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, chosen.c_str());
            data->CursorPos = data->BufTextLen;
            data->SelectionStart = data->SelectionEnd = data->CursorPos;
            m_HistorySearch = false;
            CloseSuggestions();
            return;
        }

        if (index < 0 || index >= static_cast<int>(m_Suggestions.size()))
            return;

        const std::string name = m_Suggestions[static_cast<size_t>(index)].name;
        data->DeleteChars(m_WordStart, m_WordEnd - m_WordStart);
        data->InsertChars(m_WordStart, name.c_str());
        int cursor = m_WordStart + static_cast<int>(name.size());
        data->CursorPos = cursor;

        if (cursor == data->BufTextLen) {
            data->InsertChars(cursor, " ");
            cursor++;
        }
        data->CursorPos = cursor;
        data->SelectionStart = data->SelectionEnd = cursor;

        CloseSuggestions();
    }

    void Console::AcceptSuggestionInBuffer(int index)
    {
        std::string line(m_Input);

        if (m_HistorySearch) {
            if (index < 0 || index >= static_cast<int>(m_HistoryMatches.size()))
                return;
            m_PendingBuffer = m_HistoryMatches[static_cast<size_t>(index)];
            m_HistorySearch = false;
        }
        else {
            if (index < 0 || index >= static_cast<int>(m_Suggestions.size()))
                return;
            const std::string name = m_Suggestions[static_cast<size_t>(index)].name;
            const size_t start = std::min(static_cast<size_t>(m_WordStart), line.size());
            const size_t end = std::min(static_cast<size_t>(std::max(m_WordEnd, m_WordStart)), line.size());
            line.replace(start, end - start, name);
            if (start + name.size() >= line.size())
                line += " ";
            m_PendingBuffer = line;
        }

        m_ReplaceBuffer = true;
        m_CursorToEnd = true;
        m_FocusInput = true;
        CloseSuggestions();
    }

    void Console::BrowseHistory(ImGuiInputTextCallbackData* data, bool up)
    {
        if (m_History.empty())
            return;

        const int previous = m_HistoryPos;
        if (up) {
            if (m_HistoryPos == -1) {
                m_HistoryDraft = data->Buf;
                m_HistoryPos = static_cast<int>(m_History.size()) - 1;
            }
            else if (m_HistoryPos > 0)
                --m_HistoryPos;
        }
        else if (m_HistoryPos != -1) {
            if (++m_HistoryPos >= static_cast<int>(m_History.size()))
                m_HistoryPos = -1;
        }

        if (previous == m_HistoryPos)
            return;

        const std::string& line = m_HistoryPos >= 0 ? m_History[static_cast<size_t>(m_HistoryPos)] : m_HistoryDraft;
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, line.c_str());
        data->CursorPos = data->BufTextLen;
        data->SelectionStart = data->SelectionEnd = data->CursorPos;
    }

    int Console::InputCallback(ImGuiInputTextCallbackData* data)
    {
        static_cast<Console*>(data->UserData)->OnInput(data);
        return 0;
    }

    void Console::OnInput(ImGuiInputTextCallbackData* data)
    {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
            if (m_ReplaceBuffer) {
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, m_PendingBuffer.c_str());
                data->CursorPos = data->BufTextLen;
                data->SelectionStart = data->SelectionEnd = data->CursorPos;
                m_ReplaceBuffer = false;
                m_HistoryPos = -1;
            }
            if (m_CursorToEnd) {
                data->CursorPos = data->BufTextLen;
                data->SelectionStart = data->SelectionEnd = data->CursorPos;
                m_CursorToEnd = false;
            }

            RefreshSuggestions(data->Buf, data->CursorPos);

            const bool popup = m_HistorySearch ? !m_HistoryMatches.empty() : !m_Suggestions.empty();
            if (popup && m_SelectionExplicit && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
                AcceptSuggestion(data, m_Selection);
                m_SwallowEnter = true;
            }
            return;
        }

        if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
            const bool popup = m_HistorySearch ? !m_HistoryMatches.empty() : !m_Suggestions.empty();
            if (popup)
                AcceptSuggestion(data, m_Selection);
            return;
        }

        if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
            const bool up = data->EventKey == ImGuiKey_UpArrow;
            const int count = m_HistorySearch ? static_cast<int>(m_HistoryMatches.size()) : static_cast<int>(m_Suggestions.size());

            if (count > 0) {
                m_Selection = (m_Selection + (up ? -1 : 1) + count) % count;
                m_SelectionExplicit = true;
            }
            else {
                BrowseHistory(data, up);
            }
        }
    }

    void Console::RunCommandLine()
    {
        std::string line = m_Input;
        const size_t first = line.find_first_not_of(" \t");
        const size_t last = line.find_last_not_of(" \t");
        m_Input[0] = '\0';
        m_HistoryPos = -1;
        m_HistorySearch = false;
        m_LastText.clear();
        CloseSuggestions();

        if (first == std::string::npos)
            return;
        line = line.substr(first, last - first + 1);

        if (m_History.empty() || m_History.back() != line)
            m_History.push_back(line);
        if (m_History.size() > 200)
            m_History.erase(m_History.begin());

        AddMessage(Kind::Command, Level::Log, "> " + line, {});

        const Engine::Core::CommandResult result = CVarRegistry::Global().Execute(line);
        if (!result.output.empty())
            AddMessage(result.ok ? Kind::Output : Kind::OutputError, result.ok ? Level::Info : Level::Error, result.output, {});

        m_ScrollToBottom = true;
    }

    void Console::DrawSuggestions(float x, float y, float width)
    {
        const bool history = m_HistorySearch;
        const int count = history ? static_cast<int>(m_HistoryMatches.size()) : static_cast<int>(m_Suggestions.size());
        if (count == 0 || (!m_InputActive && m_PendingAccept < 0)) {
            m_MouseOverPopup = false;
            return;
        }

        const float rowHeight = ImGui::GetTextLineHeight() + 6.0f;
        const int maxRows = 10;
        const int rows = std::min(count, maxRows);

        if (m_Selection < m_PopupTop) m_PopupTop = m_Selection;
        if (m_Selection >= m_PopupTop + rows) m_PopupTop = m_Selection - rows + 1;
        m_PopupTop = std::clamp(m_PopupTop, 0, std::max(0, count - rows));

        const float popupWidth = std::clamp(width, 520.0f, 900.0f);
        const ImVec2 position(x, y - 6.0f);

        ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(popupWidth, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.10f, 0.105f, 0.125f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.32f, 0.40f, 1.0f));
        ImGui::BeginTooltip();

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float rowWidth = popupWidth - 12.0f;
        const std::string needle = Lower(history ? std::string(m_Input) : m_WordText);

        float nameColumn = 0.0f;
        for (int i = m_PopupTop; i < m_PopupTop + rows; ++i) {
            const std::string& name = history ? m_HistoryMatches[static_cast<size_t>(i)] : m_Suggestions[static_cast<size_t>(i)].name;
            nameColumn = std::max(nameColumn, ImGui::CalcTextSize(name.c_str()).x);
        }
        nameColumn = std::min(nameColumn + 16.0f, rowWidth * 0.5f);

        ImVec2 popupMin(1e9f, 1e9f), popupMax(-1e9f, -1e9f);
        for (int i = m_PopupTop; i < m_PopupTop + rows; ++i) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImVec2 q(p.x + rowWidth, p.y + rowHeight);
            popupMin = ImVec2(std::min(popupMin.x, p.x), std::min(popupMin.y, p.y));
            popupMax = ImVec2(std::max(popupMax.x, q.x), std::max(popupMax.y, q.y));

            const bool hovered = mouse.x >= p.x && mouse.x < q.x && mouse.y >= p.y && mouse.y < q.y;
            if (hovered && ImGui::GetIO().MouseDelta.x * ImGui::GetIO().MouseDelta.x + ImGui::GetIO().MouseDelta.y * ImGui::GetIO().MouseDelta.y > 0.0f)
                m_Selection = i;
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                m_PendingAccept = i;

            if (i == m_Selection)
                draw->AddRectFilled(p, q, Color(ImVec4(0.30f, 0.45f, 0.75f, 1.0f), m_SelectionExplicit ? 0.60f : 0.35f), 4.0f);

            draw->PushClipRect(p, q, true);
            const float y0 = p.y + 3.0f;
            float cursorX = p.x + 6.0f;

            if (history) {
                draw->AddText(ImVec2(cursorX, y0), Color(kDim), "history");
                cursorX += ImGui::CalcTextSize("history").x + 10.0f;
                draw->AddText(ImVec2(cursorX, y0), Color(kOutputColor), m_HistoryMatches[static_cast<size_t>(i)].c_str());
            }
            else {
                const Suggestion& s = m_Suggestions[static_cast<size_t>(i)];
                const bool isCommand = s.kind == Suggestion::Kind::Command;
                const char* tag = isCommand ? "cmd" : "var";
                draw->AddText(ImVec2(cursorX, y0), Color(isCommand ? kCommandTag : kVariableTag, 0.85f), tag);
                cursorX += ImGui::CalcTextSize("cmd").x + 10.0f;

                const std::string lowerName = Lower(s.name);
                const size_t at = needle.empty() ? std::string::npos : lowerName.find(needle);
                const ImU32 nameColor = Color(ImVec4(0.92f, 0.93f, 0.96f, 1.0f));
                const ImU32 matchColor = Color(ImVec4(1.0f, 0.86f, 0.35f, 1.0f));
                if (at == std::string::npos) {
                    draw->AddText(ImVec2(cursorX, y0), nameColor, s.name.c_str());
                }
                else {
                    const std::string before = s.name.substr(0, at);
                    const std::string match = s.name.substr(at, needle.size());
                    const std::string after = s.name.substr(at + needle.size());
                    float nx = cursorX;
                    draw->AddText(ImVec2(nx, y0), nameColor, before.c_str());
                    nx += ImGui::CalcTextSize(before.c_str()).x;
                    draw->AddText(ImVec2(nx, y0), matchColor, match.c_str());
                    nx += ImGui::CalcTextSize(match.c_str()).x;
                    draw->AddText(ImVec2(nx, y0), nameColor, after.c_str());
                }

                float detailX = cursorX + nameColumn;
                if (!isCommand) {
                    const std::string value = "= " + s.value;
                    const bool changed = s.value != s.defaultValue;
                    draw->AddText(ImVec2(detailX, y0), Color(changed ? ImVec4(0.55f, 1.0f, 0.62f, 1.0f) : kDim), value.c_str());
                    detailX += std::max(ImGui::CalcTextSize(value.c_str()).x + 14.0f, 90.0f);
                }
                if (!s.help.empty())
                    draw->AddText(ImVec2(detailX, y0), Color(kDim), s.help.c_str());
            }
            draw->PopClipRect();

            ImGui::Dummy(ImVec2(rowWidth, rowHeight));
        }

        ImGui::Spacing();
        std::string footer = history ? "Ctrl+R history   Tab / Enter take   Esc clear" : "Tab take   Up / Down move   Enter run   Esc clear   Ctrl+R history";
        if (count > rows)
            footer += "   (" + std::to_string(m_PopupTop + 1) + "-" + std::to_string(m_PopupTop + rows) + " of " + std::to_string(count) + ")";
        ImGui::TextColored(kDim, "%s", footer.c_str());

        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        m_PopupMinX = windowPos.x;
        m_PopupMinY = windowPos.y;
        m_PopupMaxX = windowPos.x + windowSize.x;
        m_PopupMaxY = windowPos.y + windowSize.y;
        m_MouseOverPopup = mouse.x >= m_PopupMinX && mouse.x <= m_PopupMaxX && mouse.y >= m_PopupMinY && mouse.y <= m_PopupMaxY;
        (void)popupMin; (void)popupMax;

        ImGui::EndTooltip();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    void Console::DrawHint()
    {
        std::string line(m_Input);
        std::vector<std::string> words = CVarRegistry::Tokenize(line);
        std::string hint;

        if (!words.empty()) {
            CVarRegistry& registry = CVarRegistry::Global();
            const std::string command = Lower(words[0]);
            std::string subject = words[0];
            if ((command == "set" || command == "get" || command == "reset" || command == "toggle") && words.size() >= 2)
                subject = words[1];

            if (auto info = registry.Describe(subject)) {
                if (info->kind == Suggestion::Kind::Command) {
                    hint = info->help;
                }
                else {
                    hint = info->name + " = " + info->value + "   (" + info->type + ", default " + info->defaultValue + (info->readOnly ? ", read-only" : "") + ")";
                    if (!info->help.empty())
                        hint += "   " + info->help;
                }
            }
        }

        if (hint.empty())
            ImGui::TextDisabled("Type a command or a variable. 'help' lists the commands, '?' (top right) the keys.");
        else
            ImGui::TextColored(kDim, "%s", hint.c_str());
    }

    void Console::DrawCommandLine()
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kCommandColor, ">");
        ImGui::SameLine();

        if (m_PendingAccept >= 0) {
            AcceptSuggestionInBuffer(m_PendingAccept);
            m_PendingAccept = -1;
        }

        if (m_FocusInput) {
            ImGui::SetKeyboardFocusHere();
            m_FocusInput = false;
        }

        ImGui::SetNextItemWidth(-1.0f);
        const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll |
                                          ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory |
                                          ImGuiInputTextFlags_CallbackAlways;
        const bool entered = ImGui::InputTextWithHint("##consoleCommandLine", "command or variable", m_Input, sizeof(m_Input), flags, InputCallback, this);

        m_InputActive = ImGui::IsItemActive();
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        m_InputX = itemMin.x;
        m_InputY = itemMin.y;
        m_InputWidth = ImGui::GetItemRectSize().x;

        if (entered) {
            if (m_SwallowEnter)
                m_SwallowEnter = false;
            else
                RunCommandLine();
            m_FocusInput = true;
        }

        if (m_InputActive && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_R)) {
            m_HistorySearch = !m_HistorySearch;
            m_Selection = 0;
            m_SelectionExplicit = false;
            m_LastText.clear();
        }
        if (!m_InputActive && m_HistorySearch && m_PendingAccept < 0 && !m_MouseOverPopup)
            m_HistorySearch = false;

        DrawHint();
    }

    void Console::Draw()
    {
        if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false) && !ImGui::GetIO().WantTextInput) {
            FocusCommandLine();
        }
        if (m_FocusWindow) {
            ImGui::SetNextWindowFocus();
            m_FocusWindow = false;
        }

        ImGui::Begin("Console");

        DrainPending();


        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_L))
                Clear();
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))
                m_FocusFilter = true;
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_PageUp))
                m_ScrollJump = -1;
            else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_PageDown))
                m_ScrollJump = 1;
            else if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
                m_ScrollDelta = -ImGui::GetWindowHeight() * 0.5f;
            else if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
                m_ScrollDelta = ImGui::GetWindowHeight() * 0.5f;
        }

        DrawToolbar();

        const ImGuiStyle& style = ImGui::GetStyle();
        const float footerHeight = ImGui::GetFrameHeight() + ImGui::GetTextLineHeight() + style.ItemSpacing.y * 3.0f + 4.0f;
        DrawLog(footerHeight);

        ImGui::Spacing();
        DrawCommandLine();

        DrawSuggestions(m_InputX, m_InputY, m_InputWidth);

        ImGui::End();
    }
}
