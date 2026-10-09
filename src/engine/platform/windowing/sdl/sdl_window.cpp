#include "engine/platform/windowing/sdl/sdl_window.hpp"

#include "engine/platform/devices/sdl/sdl_keymap.hpp"
#include "engine/platform/hardware/system_info.hpp"
#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Core::Platform {

    void SDLWindow::Init(const std::string& title, const int& width, const int& height,
                         const bool& fullscreen, const int& vsync, const uint32_t& api)
    {
        // SDL reference counts its subsystems : the clipboard and the dialogs can share the video one.
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
            DEBUG_FATAL("Failed to init SDL : ", SDL_GetError());

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

        SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (fullscreen)
            flags |= SDL_WINDOW_FULLSCREEN;

        // Fullscreen uses the desktop resolution of the display, whatever the requested size.
        m_Window = SDL_CreateWindow(title.c_str(), width, height, flags);
        if (!m_Window)
        {
            // Some displays (a virtual X server, a software renderer) offer no multisampled visual : retry without MSAA.
            DEBUG_WARNING("Failed to create the SDL window with MSAA (", SDL_GetError(), "), retrying without.");
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
            m_Window = SDL_CreateWindow(title.c_str(), width, height, flags);
        }
        if (!m_Window)
        {
            const std::string error = SDL_GetError();
            SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD);
            DEBUG_FATAL("Failed to create SDL window : ", error);
            return;
        }

        m_GLContext = SDL_GL_CreateContext(m_Window);
        if (!m_GLContext)
        {
            const std::string error = SDL_GetError();
            SDL_DestroyWindow(m_Window);
            m_Window = nullptr;
            SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD);
            DEBUG_FATAL("Failed to create the OpenGL context : ", error);
            return;
        }

        SDL_GL_MakeCurrent(m_Window, m_GLContext);
        SDL_GL_SetSwapInterval(vsync);
        m_ShouldClose = false;
    }

    void SDLWindow::PollEvents()
    {
        SDL_Event native;
        while (SDL_PollEvent(&native))
        {
            OnNativeEvent(native);
            Translate(native);
        }
    }

    void SDLWindow::Translate(const SDL_Event& native)
    {
        OSEvent event;
        switch (native.type)
        {
            case SDL_EVENT_QUIT:
                m_ShouldClose = true;
                event.type = OSEventType::Quit;
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (native.window.windowID != SDL_GetWindowID(m_Window)) return;
                m_ShouldClose = true;
                event.type = OSEventType::WindowCloseRequested;
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                event.type = OSEventType::WindowResized;
                event.width = native.window.data1;
                event.height = native.window.data2;
                break;
            case SDL_EVENT_WINDOW_MOVED:
                event.type = OSEventType::WindowMoved;
                event.x = native.window.data1;
                event.y = native.window.data2;
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED: event.type = OSEventType::WindowFocusGained; break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:   event.type = OSEventType::WindowFocusLost; break;
            case SDL_EVENT_WINDOW_MINIMIZED:    event.type = OSEventType::WindowMinimized; break;
            case SDL_EVENT_WINDOW_RESTORED:     event.type = OSEventType::WindowRestored; break;
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                event.type = OSEventType::DisplayScaleChanged;
                event.scale = GetContentScale();
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            {
                Input::Key key;
                if (!ScancodeToKey(native.key.scancode, key)) return;
                event.type = native.type == SDL_EVENT_KEY_DOWN ? OSEventType::KeyDown : OSEventType::KeyUp;
                event.key = key;
                event.repeat = native.key.repeat;
                break;
            }
            case SDL_EVENT_TEXT_INPUT:
                event.type = OSEventType::TextInput;
                event.text = native.text.text ? native.text.text : "";
                break;
            case SDL_EVENT_MOUSE_MOTION:
                event.type = OSEventType::MouseMotion;
                event.mouseX = native.motion.x;
                event.mouseY = native.motion.y;
                event.deltaX = native.motion.xrel;
                event.deltaY = native.motion.yrel;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                Input::MouseButton button;
                if (!SDLButtonToMouseButton(native.button.button, button)) return;
                event.type = native.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? OSEventType::MouseButtonDown : OSEventType::MouseButtonUp;
                event.button = button;
                event.mouseX = native.button.x;
                event.mouseY = native.button.y;
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL:
                event.type = OSEventType::MouseWheel;
                event.deltaX = native.wheel.x;
                event.deltaY = native.wheel.y;
                event.mouseX = native.wheel.mouse_x;
                event.mouseY = native.wheel.mouse_y;
                break;
            case SDL_EVENT_DROP_FILE:
                event.type = OSEventType::FileDropped;
                event.text = native.drop.data ? native.drop.data : "";
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                event.type = OSEventType::GamepadConnected;
                event.deviceId = native.gdevice.which;
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                event.type = OSEventType::GamepadDisconnected;
                event.deviceId = native.gdevice.which;
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                if (!SDLGamepadButtonToButton(native.gbutton.button, event.gamepadButton)) return;
                event.type = native.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? OSEventType::GamepadButtonDown : OSEventType::GamepadButtonUp;
                event.deviceId = native.gbutton.which;
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            {
                if (!SDLGamepadAxisToAxis(native.gaxis.axis, event.gamepadAxis)) return;
                event.type = OSEventType::GamepadAxisMotion;
                event.deviceId = native.gaxis.which;
                // Sticks span the whole int16 range, triggers only its positive half
                const float value = native.gaxis.value >= 0 ? native.gaxis.value / 32767.0f : native.gaxis.value / 32768.0f;
                event.axisValue = value;
                break;
            }
            default:
                return;
        }
        DispatchEvent(event);
    }

    void SDLWindow::SwapBuffers()
    {
        SDL_GL_SwapWindow(m_Window);
    }

    int SDLWindow::GetFramebufferWidth() const
    {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(m_Window, &w, &h);
        return w;
    }

    int SDLWindow::GetFramebufferHeight() const
    {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(m_Window, &w, &h);
        return h;
    }

    int SDLWindow::GetBytesPerPixel() const
    {
        int r = 0, g = 0, b = 0, a = 0;
        SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &r);
        SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &g);
        SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &b);
        SDL_GL_GetAttribute(SDL_GL_ALPHA_SIZE, &a);
        return (r + g + b + a) / 8;
    }

    void SDLWindow::SetTitle(const std::string& title)
    {
        SDL_SetWindowTitle(m_Window, title.c_str());
    }

    void SDLWindow::ToggleFullscreen()
    {
        const bool fullscreen = (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN) != 0;
        SDL_SetWindowFullscreen(m_Window, !fullscreen);
    }

    void SDLWindow::Destroy() const
    {
        if (m_GLContext)
            SDL_GL_DestroyContext(m_GLContext);
        if (m_Window)
            SDL_DestroyWindow(m_Window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD);
    }

    float SDLWindow::GetContentScale() const
    {
        const float scale = m_Window ? SDL_GetWindowDisplayScale(m_Window) : 1.0f;
        return scale > 0.0f ? scale : 1.0f;
    }

    std::vector<MonitorInfos> SDLWindow::GetMonitors() const
    {
        std::vector<MonitorInfos> monitors;

        int count = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&count);
        if (!displays)
            return monitors;

        const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
        for (int i = 0; i < count; ++i)
        {
            MonitorInfos info;
            if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displays[i]))
            {
                info.width = mode->w;
                info.height = mode->h;
                info.refreshRate = static_cast<int>(mode->refresh_rate + 0.5f);
            }
            SDL_Rect bounds{};
            if (SDL_GetDisplayBounds(displays[i], &bounds))
            {
                info.x = bounds.x;
                info.y = bounds.y;
            }
            info.contentScale = SDL_GetDisplayContentScale(displays[i]);
            info.hdr = SDL_GetBooleanProperty(SDL_GetDisplayProperties(displays[i]), SDL_PROP_DISPLAY_HDR_ENABLED_BOOLEAN, false);
            info.primary = displays[i] == primary;
            if (const char* name = SDL_GetDisplayName(displays[i]))
                info.name = name;
            monitors.push_back(std::move(info));
        }
        SDL_free(displays);
        return monitors;
    }

    SystemInfos SDLWindow::GetSystemInfos() const
    {
        SystemInfos ret{};

        const int version = SDL_GetVersion();
        ret.windowHost = WindowHost::SDL3;
        ret.windowHostVersion = std::to_string(SDL_VERSIONNUM_MAJOR(version)) + "." +
                                std::to_string(SDL_VERSIONNUM_MINOR(version)) + "." +
                                std::to_string(SDL_VERSIONNUM_MICRO(version));

        ret.monitors = GetMonitors();
        ret.connectedMonitorsCount = static_cast<int>(ret.monitors.size());

        const SystemInfo& system = GetSystemInfo();
        ret.cpuBrand = system.cpu.brand;
        ret.cpuLogicalCores = static_cast<int>(system.cpu.logicalCores);
        ret.totalRamMB = system.memory.totalPhysical / (1024 * 1024);

        return ret;
    }

    void* SDLWindow::GLProcLoader(const char* name)
    {
        return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
    }
}
