#include "engine/platform/replay/input_replay.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "engine/platform/devices/gamepad.hpp"

namespace Shard::Engine::Core::Platform {

    using namespace Shard::Engine::Input;

    namespace {
        // In the order of the Key enum : the names in the files stay the same if the enum is reordered, this table is what
        // has to follow (the test of the replay checks a few entries at both ends and in the middle)
        const char* const kKeyNames[] = {
            "Space", "Apostrophe", "Comma", "Minus", "Period", "Slash",
            "K0", "K1", "K2", "K3", "K4", "K5", "K6", "K7", "K8", "K9",
            "Semicolon", "Equal",
            "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
            "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
            "LeftBracket", "Backslash", "RightBracket", "GraveAccent", "World1", "World2",
            "Escape", "Enter", "Tab", "Backspace", "Insert", "Delete", "Right", "Left", "Down", "Up",
            "PageUp", "PageDown", "Home", "End", "CapsLock", "ScrollLock", "NumLock", "PrintScreen", "Pause",
            "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16", "F17",
            "F18", "F19", "F20", "F21", "F22", "F23", "F24", "F25",
            "Keypad0", "Keypad1", "Keypad2", "Keypad3", "Keypad4", "Keypad5", "Keypad6", "Keypad7", "Keypad8", "Keypad9",
            "KeypadDecimal", "KeypadDivide", "KeypadMultiply", "KeypadSubtract", "KeypadAdd", "KeypadEnter", "KeypadEqual",
            "LeftShift", "LeftControl", "LeftAlt", "LeftSuper", "RightShift", "RightControl", "RightAlt", "RightSuper", "Menu",
        };
        constexpr size_t kKeyNameCount = sizeof(kKeyNames) / sizeof(kKeyNames[0]);
        static_assert(kKeyNameCount == static_cast<size_t>(Key::Menu) + 1, "kKeyNames has to list every Key, in the order of the enum");

        std::string FloatText(float value)
        {
            uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            char text[16];
            std::snprintf(text, sizeof(text), "#%08x", bits);
            return text;
        }

        bool ParseFloat(const std::string& token, float& out)
        {
            if (token.empty())
                return false;
            if (token[0] == '#')
            {
                if (token.size() < 2 || token.size() > 9)
                    return false;
                char* end = nullptr;
                const unsigned long bits = std::strtoul(token.c_str() + 1, &end, 16);
                if (*end != '\0')
                    return false;
                const uint32_t bits32 = static_cast<uint32_t>(bits);
                std::memcpy(&out, &bits32, sizeof(out));
                return true;
            }
            char* end = nullptr;
            out = std::strtof(token.c_str(), &end);
            return *end == '\0';
        }

        bool ParseUnsigned(const std::string& token, uint64_t& out)
        {
            if (token.empty())
                return false;
            char* end = nullptr;
            out = std::strtoull(token.c_str(), &end, 10);
            return *end == '\0';
        }

        std::string HexEncode(const std::string& text)
        {
            static const char* digits = "0123456789abcdef";
            std::string out;
            out.reserve(text.size() * 2);
            for (unsigned char c : text)
            {
                out.push_back(digits[c >> 4]);
                out.push_back(digits[c & 0xF]);
            }
            return out;
        }

        bool HexDecode(const std::string& hex, std::string& out)
        {
            if (hex.size() % 2 != 0)
                return false;
            out.clear();
            auto value = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            for (size_t i = 0; i < hex.size(); i += 2)
            {
                const int hi = value(hex[i]), lo = value(hex[i + 1]);
                if (hi < 0 || lo < 0)
                    return false;
                out.push_back(static_cast<char>(hi * 16 + lo));
            }
            return true;
        }

        std::string HashText(uint64_t hash)
        {
            char text[17];
            std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
            return text;
        }

        std::vector<std::string> Split(const std::string& line)
        {
            std::vector<std::string> tokens;
            std::istringstream stream(line);
            std::string token;
            while (stream >> token)
                tokens.push_back(token);
            return tokens;
        }
    }

    const char* KeyName(Key key)
    {
        const size_t index = static_cast<size_t>(key);
        return index < kKeyNameCount ? kKeyNames[index] : "";
    }

    bool KeyFromName(std::string_view name, Key& out)
    {
        for (size_t i = 0; i < kKeyNameCount; ++i)
        {
            if (name == kKeyNames[i])
            {
                out = static_cast<Key>(i);
                return true;
            }
        }
        return false;
    }

    // ---- ReplayData -----------------------------------------------------------------------------------------------

    uint64_t ReplayData::Length() const
    {
        uint64_t length = frames;
        if (!events.empty())
            length = std::max(length, events.back().frame + 1);
        if (!checks.empty())
            length = std::max(length, checks.back().frames);
        return length;
    }

    std::string ReplayData::Serialize() const
    {
        std::ostringstream out;
        out << "shard-replay " << kVersion << "\n";
        out << "seed " << seed << "\n";
        if (fixedStep > 0.0f)
            out << "fixed-step " << FloatText(fixedStep) << "\n";
        if (!world.empty())
            out << "world " << world << "\n";
        out << "frames " << Length() << "\n";
        for (const std::string& plugin : plugins)
            out << "plugin " << plugin << "\n";
        for (const Check& check : checks)
        {
            out << "check " << check.frames << " " << HashText(check.hash);
            if (check.hasParts)
                out << " " << HashText(check.parts[0]) << " " << HashText(check.parts[1]) << " " << HashText(check.parts[2]);
            out << "\n";
        }

        for (const TimedEvent& timed : events)
        {
            const OSEvent& e = timed.event;
            out << "@" << timed.frame << " ";
            switch (e.type)
            {
                case OSEventType::KeyDown:            out << "key-down " << KeyName(e.key); break;
                case OSEventType::KeyUp:              out << "key-up " << KeyName(e.key); break;
                case OSEventType::MouseButtonDown:    out << "mouse-down " << static_cast<int>(e.button); break;
                case OSEventType::MouseButtonUp:      out << "mouse-up " << static_cast<int>(e.button); break;
                case OSEventType::MouseMotion:        out << "mouse-move " << FloatText(e.mouseX) << " " << FloatText(e.mouseY) << " " << FloatText(e.deltaX) << " " << FloatText(e.deltaY); break;
                case OSEventType::MouseWheel:         out << "wheel " << FloatText(e.deltaX) << " " << FloatText(e.deltaY); break;
                case OSEventType::TextInput:          out << "text " << HexEncode(e.text); break;
                case OSEventType::WindowFocusLost:    out << "focus-lost"; break;
                case OSEventType::GamepadConnected:   out << "pad-connect " << e.deviceId; break;
                case OSEventType::GamepadDisconnected: out << "pad-disconnect " << e.deviceId; break;
                case OSEventType::GamepadButtonDown:  out << "pad-down " << e.deviceId << " " << static_cast<int>(e.gamepadButton); break;
                case OSEventType::GamepadButtonUp:    out << "pad-up " << e.deviceId << " " << static_cast<int>(e.gamepadButton); break;
                case OSEventType::GamepadAxisMotion:  out << "pad-axis " << e.deviceId << " " << static_cast<int>(e.gamepadAxis) << " " << FloatText(e.axisValue); break;
                default:                              out << "# (event not recorded)"; break;
            }
            out << "\n";
        }
        return out.str();
    }

    bool ReplayData::Parse(std::string_view text, ReplayData& out, std::string& error)
    {
        out = ReplayData{};
        bool sawHeader = false;
        size_t lineNumber = 0;

        auto fail = [&](const std::string& why) {
            error = "line " + std::to_string(lineNumber) + " : " + why;
            return false;
        };

        std::istringstream stream{std::string(text)};
        std::string line;
        while (std::getline(stream, line))
        {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const std::vector<std::string> t = Split(line);
            if (t.empty() || t[0][0] == '#')
                continue;

            if (!sawHeader)
            {
                uint64_t version = 0;
                if (t[0] != "shard-replay" || t.size() != 2 || !ParseUnsigned(t[1], version))
                    return fail("not a replay (the first line is \"shard-replay <version>\")");
                if (version != static_cast<uint64_t>(kVersion))
                    return fail("replay format version " + t[1] + " is not supported (this build reads version " + std::to_string(kVersion) + ")");
                sawHeader = true;
                continue;
            }

            if (t[0] == "seed")
            {
                if (t.size() != 2 || !ParseUnsigned(t[1], out.seed)) return fail("seed <number>");
            }
            else if (t[0] == "fixed-step")
            {
                if (t.size() != 2 || !ParseFloat(t[1], out.fixedStep)) return fail("fixed-step <float>");
            }
            else if (t[0] == "world")
            {
                if (t.size() != 2) return fail("world <path>");
                out.world = t[1];
            }
            else if (t[0] == "frames")
            {
                if (t.size() != 2 || !ParseUnsigned(t[1], out.frames)) return fail("frames <number>");
            }
            else if (t[0] == "plugin")
            {
                if (t.size() != 4) return fail("plugin <id> <version> <hash>");
                out.plugins.push_back(t[1] + " " + t[2] + " " + t[3]);
            }
            else if (t[0] == "check")
            {
                Check check;
                if ((t.size() != 3 && t.size() != 6) || !ParseUnsigned(t[1], check.frames)) return fail("check <frames> <hash> [<transforms> <physics> <scripts>]");
                auto parseHash = [](const std::string& token, uint64_t& value) {
                    char* end = nullptr;
                    value = std::strtoull(token.c_str(), &end, 16);
                    return !token.empty() && *end == '\0';
                };
                if (!parseHash(t[2], check.hash)) return fail("check : the hashes are hexadecimal");
                if (t.size() == 6)
                {
                    check.hasParts = true;
                    for (size_t i = 0; i < 3; ++i)
                        if (!parseHash(t[3 + i], check.parts[i])) return fail("check : the hashes are hexadecimal");
                }
                out.checks.push_back(check);
            }
            else if (t[0][0] == '@')
            {
                TimedEvent timed;
                if (!ParseUnsigned(t[0].substr(1), timed.frame) || t.size() < 2) return fail("@<frame> <event> ...");
                OSEvent& e = timed.event;
                const std::string& kind = t[1];
                uint64_t number = 0;

                auto needs = [&](size_t count) { return t.size() == 2 + count; };
                if (kind == "key-down" || kind == "key-up")
                {
                    if (!needs(1) || !KeyFromName(t[2], e.key)) return fail("unknown key \"" + (t.size() > 2 ? t[2] : std::string()) + "\"");
                    e.type = kind == "key-down" ? OSEventType::KeyDown : OSEventType::KeyUp;
                }
                else if (kind == "mouse-down" || kind == "mouse-up")
                {
                    if (!needs(1) || !ParseUnsigned(t[2], number) || number > static_cast<uint64_t>(MouseButton::Button7)) return fail(kind + " <button 0-7>");
                    e.type = kind == "mouse-down" ? OSEventType::MouseButtonDown : OSEventType::MouseButtonUp;
                    e.button = static_cast<MouseButton>(number);
                }
                else if (kind == "mouse-move")
                {
                    if (!needs(4) || !ParseFloat(t[2], e.mouseX) || !ParseFloat(t[3], e.mouseY) || !ParseFloat(t[4], e.deltaX) || !ParseFloat(t[5], e.deltaY))
                        return fail("mouse-move <x> <y> <dx> <dy>");
                    e.type = OSEventType::MouseMotion;
                }
                else if (kind == "wheel")
                {
                    if (!needs(2) || !ParseFloat(t[2], e.deltaX) || !ParseFloat(t[3], e.deltaY)) return fail("wheel <dx> <dy>");
                    e.type = OSEventType::MouseWheel;
                }
                else if (kind == "text")
                {
                    if (!needs(1) || !HexDecode(t[2], e.text)) return fail("text <hex>");
                    e.type = OSEventType::TextInput;
                }
                else if (kind == "focus-lost")
                {
                    if (!needs(0)) return fail("focus-lost");
                    e.type = OSEventType::WindowFocusLost;
                }
                else if (kind == "pad-connect" || kind == "pad-disconnect")
                {
                    if (!needs(1) || !ParseUnsigned(t[2], number)) return fail(kind + " <id>");
                    e.type = kind == "pad-connect" ? OSEventType::GamepadConnected : OSEventType::GamepadDisconnected;
                    e.deviceId = static_cast<uint32_t>(number);
                }
                else if (kind == "pad-down" || kind == "pad-up")
                {
                    uint64_t button = 0;
                    if (!needs(2) || !ParseUnsigned(t[2], number) || !ParseUnsigned(t[3], button) || button >= static_cast<uint64_t>(GamepadButton::Count))
                        return fail(kind + " <id> <button>");
                    e.type = kind == "pad-down" ? OSEventType::GamepadButtonDown : OSEventType::GamepadButtonUp;
                    e.deviceId = static_cast<uint32_t>(number);
                    e.gamepadButton = static_cast<GamepadButton>(button);
                }
                else if (kind == "pad-axis")
                {
                    uint64_t axis = 0;
                    if (!needs(3) || !ParseUnsigned(t[2], number) || !ParseUnsigned(t[3], axis) || axis >= static_cast<uint64_t>(GamepadAxis::Count) || !ParseFloat(t[4], e.axisValue))
                        return fail("pad-axis <id> <axis> <value>");
                    e.type = OSEventType::GamepadAxisMotion;
                    e.deviceId = static_cast<uint32_t>(number);
                    e.gamepadAxis = static_cast<GamepadAxis>(axis);
                }
                else
                    return fail("unknown event \"" + kind + "\"");

                if (!out.events.empty() && timed.frame < out.events.back().frame)
                    return fail("events have to be in frame order");
                out.events.push_back(std::move(timed));
            }
            else
                return fail("unknown line \"" + t[0] + "\"");
        }

        if (!sawHeader)
        {
            error = "empty file : not a replay";
            return false;
        }

        std::stable_sort(out.checks.begin(), out.checks.end(), [](const Check& a, const Check& b) { return a.frames < b.frames; });
        return true;
    }

    bool ReplayData::Save(const std::string& path, std::string& error) const
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            error = "cannot write " + path;
            return false;
        }
        const std::string text = Serialize();
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file)
        {
            error = "cannot write " + path;
            return false;
        }
        return true;
    }

    bool ReplayData::Load(const std::string& path, ReplayData& out, std::string& error)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            error = "cannot read " + path;
            return false;
        }
        std::ostringstream text;
        text << file.rdbuf();
        if (!Parse(text.str(), out, error))
        {
            error = path + " : " + error;
            return false;
        }
        return true;
    }

    // ---- recorder, player -------------------------------------------------------------------------------------------

    bool ReplayRecorder::IsRecorded(const OSEvent& event)
    {
        switch (event.type)
        {
            case OSEventType::KeyDown:          return !event.repeat;
            case OSEventType::KeyUp:
            case OSEventType::MouseButtonDown:
            case OSEventType::MouseButtonUp:
            case OSEventType::MouseMotion:
            case OSEventType::MouseWheel:
            case OSEventType::TextInput:
            case OSEventType::WindowFocusLost:
            case OSEventType::GamepadConnected:
            case OSEventType::GamepadDisconnected:
            case OSEventType::GamepadButtonDown:
            case OSEventType::GamepadButtonUp:
            case OSEventType::GamepadAxisMotion:
                return true;
            default:
                return false;
        }
    }

    void ReplayRecorder::Attach(OSEventSource& source)
    {
        Detach();
        m_Source = &source;
        m_Listener = source.AddEventListener([this](const OSEvent& event) {
            if (IsRecorded(event))
                m_Data.events.push_back({m_Frame, event});
        });
    }

    void ReplayRecorder::Detach()
    {
        if (m_Source)
            m_Source->RemoveEventListener(m_Listener);
        m_Source = nullptr;
    }

    void ReplayPlayer::Deliver(uint64_t frame, const std::function<void(const OSEvent&)>& post)
    {
        while (m_Next < m_Data.events.size() && m_Data.events[m_Next].frame <= frame)
            post(m_Data.events[m_Next++].event);
    }

    const ReplayData::Check* ReplayPlayer::CheckAfter(uint64_t frames) const
    {
        for (const ReplayData::Check& check : m_Data.checks)
            if (check.frames == frames)
                return &check;
        return nullptr;
    }
}
