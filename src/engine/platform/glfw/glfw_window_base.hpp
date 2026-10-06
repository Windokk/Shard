#pragma once

#include "engine/platform/iwindow.hpp"
#include "engine/platform/glfw/glfw_input.hpp"

namespace Shard::Engine::Core::Platform {

    // Common base of the GLFW-backed windows (editor main window, player window).
    // GLFWInput finds its window through the GLFW user pointer: every derived window must set it with
    // glfwSetWindowUserPointer(window, static_cast<GLFWWindowBase*>(this)).
    class GLFWWindowBase : public IWindow {
    public:
        void SetGLFWInputManager(GLFWInput* manager) { inputManager = manager; }

        GLFWInput* inputManager = nullptr;
    };
}
