#include "engine/platform/windowing/headless/headless_platform.hpp"

#include "engine/platform/hardware/system_info.hpp"

namespace Shard::Engine::Core::Platform {

    // ---- window ---------------------------------------------------------------------------------------------------

    void HeadlessWindow::Init(const std::string& title, const int& width, const int& height,
                              const bool& fullscreen, const int& vsync, const uint32_t& api)
    {
        (void)fullscreen;
        (void)vsync;
        (void)api;
        m_Title = title;
        m_Width = width > 0 ? width : m_Width;
        m_Height = height > 0 ? height : m_Height;
        m_CloseRequested = false;
        m_Pending.clear();
    }

    void HeadlessWindow::PostEvent(const OSEvent& event)
    {
        m_Pending.push_back(event);
    }

    void HeadlessWindow::PollEvents()
    {
        // What a listener posts while it runs waits for the next poll, like events that arrive during a real one
        std::deque<OSEvent> events;
        events.swap(m_Pending);
        for (const OSEvent& event : events)
        {
            if (event.type == OSEventType::Quit || event.type == OSEventType::WindowCloseRequested)
                m_CloseRequested = true;
            else if (event.type == OSEventType::WindowResized && event.width > 0 && event.height > 0)
            {
                m_Width = event.width;
                m_Height = event.height;
            }
            DispatchEvent(event);
        }
    }

    SystemInfos HeadlessWindow::GetSystemInfos() const
    {
        SystemInfos info{};
        info.gpu_vendor = "none";
        info.gpu_renderer = "none (headless)";
        info.gl_version = "none";
        info.windowHost = WindowHost::Headless;
        info.windowHostVersion = "-";
        info.connectedMonitorsCount = 0;

        const SystemInfo& system = GetSystemInfo();
        info.cpuBrand = system.cpu.brand;
        info.cpuLogicalCores = static_cast<int>(system.cpu.logicalCores);
        info.totalRamMB = system.memory.totalPhysical / (1024 * 1024);
        return info;
    }

    // ---- input ----------------------------------------------------------------------------------------------------

    void HeadlessInput::Init()
    {
        m_Keys.fill(false);
        m_PreviousKeys.fill(false);
        m_Buttons.fill(false);
        m_PreviousButtons.fill(false);
        m_Pads.clear();

        if (m_Window && !m_Listening)
        {
            m_Listener = m_Window->AddEventListener([this](const OSEvent& event) { OnEvent(event); });
            m_Listening = true;
        }
    }

    void HeadlessInput::Tick()
    {
        m_PreviousKeys = m_Keys;
        m_PreviousButtons = m_Buttons;
        for (Pad& pad : m_Pads)
            pad.previousButtons = pad.buttons;
    }

    void HeadlessInput::Shutdown()
    {
        if (m_Window && m_Listening)
        {
            m_Window->RemoveEventListener(m_Listener);
            m_Listening = false;
        }
        m_Pads.clear();
    }

    void HeadlessInput::OnEvent(const OSEvent& event)
    {
        switch (event.type)
        {
            case OSEventType::KeyDown:
            case OSEventType::KeyUp:
                m_Keys[static_cast<size_t>(event.key)] = event.type == OSEventType::KeyDown;
                break;
            case OSEventType::MouseButtonDown:
            case OSEventType::MouseButtonUp:
                m_Buttons[static_cast<size_t>(event.button)] = event.type == OSEventType::MouseButtonDown;
                break;
            case OSEventType::MouseMotion:
                // Captured : the cursor is unbounded and moves by the relative motion (like a disabled cursor)
                if (m_Captured)
                {
                    m_CursorX += event.deltaX;
                    m_CursorY += event.deltaY;
                }
                else
                {
                    m_CursorX = event.mouseX;
                    m_CursorY = event.mouseY;
                }
                break;
            case OSEventType::GamepadConnected:
                if (!FindPad(event.deviceId))
                {
                    Pad pad;
                    pad.id = event.deviceId;
                    m_Pads.push_back(pad);
                }
                break;
            case OSEventType::GamepadDisconnected:
                for (auto it = m_Pads.begin(); it != m_Pads.end(); ++it)
                    if (it->id == event.deviceId) { m_Pads.erase(it); break; }
                break;
            case OSEventType::GamepadButtonDown:
            case OSEventType::GamepadButtonUp:
                if (Pad* pad = FindPad(event.deviceId))
                    pad->buttons[static_cast<size_t>(event.gamepadButton)] = event.type == OSEventType::GamepadButtonDown;
                break;
            case OSEventType::GamepadAxisMotion:
                if (Pad* pad = FindPad(event.deviceId))
                    pad->axes[static_cast<size_t>(event.gamepadAxis)] = event.axisValue;
                break;
            case OSEventType::WindowFocusLost:
                m_Keys.fill(false);
                m_Buttons.fill(false);
                break;
            default:
                break;
        }
    }

    HeadlessInput::Pad* HeadlessInput::FindPad(uint32_t id)
    {
        for (Pad& pad : m_Pads)
            if (pad.id == id)
                return &pad;
        return nullptr;
    }

    bool HeadlessInput::IsGamepadButtonDown(int pad, Input::GamepadButton button) const
    {
        const Pad* p = GetPad(pad);
        return p && p->buttons[static_cast<size_t>(button)];
    }

    bool HeadlessInput::WasGamepadButtonPressed(int pad, Input::GamepadButton button) const
    {
        const Pad* p = GetPad(pad);
        return p && !p->previousButtons[static_cast<size_t>(button)] && p->buttons[static_cast<size_t>(button)];
    }

    bool HeadlessInput::WasGamepadButtonReleased(int pad, Input::GamepadButton button) const
    {
        const Pad* p = GetPad(pad);
        return p && p->previousButtons[static_cast<size_t>(button)] && !p->buttons[static_cast<size_t>(button)];
    }

    float HeadlessInput::GetGamepadAxis(int pad, Input::GamepadAxis axis, float deadZone) const
    {
        const Pad* p = GetPad(pad);
        if (!p)
            return 0.0f;
        const float value = p->axes[static_cast<size_t>(axis)];
        return (value < deadZone && value > -deadZone) ? 0.0f : value;
    }

    // ---- platform -------------------------------------------------------------------------------------------------

    void HeadlessPlatform::CreateWindow(const std::string& title, const int& width, const int& height,
                                        const bool& fullscreen, const int& vsync, const uint32_t& api)
    {
        m_Window = std::make_unique<HeadlessWindow>();
        m_Window->Init(title, width, height, fullscreen, vsync, api);
    }

    void HeadlessPlatform::CreateInput()
    {
        m_Input = std::make_unique<HeadlessInput>(m_Window.get());
        m_Input->Init();
    }
}
