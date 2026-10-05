#include "engine/core/engine.hpp"

#include "glfw_window.hpp"

namespace Shard::Game::Core::Platform{

    void GLFWInput::SetWindow(GLFWwindow * window)
    {
        win = window;
    }

    void GLFWInput::Init() {
        mCurrentKeyState.clear();
        mPreviousKeyState.clear();
        mCurrentMouseState.clear();
        mPreviousMouseState.clear();

        glfwSetKeyCallback(win, [](GLFWwindow* w, int key, int sc, int action, int mods) {
            auto* window = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w));
            if (window) {
                window->inputManager->KeyCallback(key, action);
            }
        });

        glfwSetMouseButtonCallback(win, [](GLFWwindow* w, int button, int action, int mods) {
            auto* window = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w));
            if (window) {
                window->inputManager->MouseCallback(button, action);
            }
        });
    }

    void GLFWInput::Tick()
    {
        mPreviousKeyState = mCurrentKeyState;
        mPreviousMouseState = mCurrentMouseState;
    }

    void GLFWInput::Shutdown()
    {
        mCurrentKeyState.clear();
        mPreviousKeyState.clear();
        mCurrentMouseState.clear();
        mPreviousMouseState.clear();
    }

    void GLFWInput::KeyCallback(int key, int action)
    {
        if (key >= 0) {
            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                mCurrentKeyState[key] = true;
            } else if (action == GLFW_RELEASE) {
                mCurrentKeyState[key] = false;
            }
        }
    }

    void GLFWInput::MouseCallback(int button, int action)
    {
        if (button >= 0) {
            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                mCurrentMouseState[button] = true;
            } else if (action == GLFW_RELEASE) {
                mCurrentMouseState[button] = false;
            }
        }
    }

    bool GLFWInput::IsKeyDown(Engine::Input::Key key) const {
        auto it = mCurrentKeyState.find(ToGLFWKey(key));
        return it != mCurrentKeyState.end() && it->second;
    }

    bool GLFWInput::IsKeyUp(Engine::Input::Key key) const {
        return !IsKeyDown(key);
    }

    bool GLFWInput::WasKeyPressed(Engine::Input::Key key) const {
        int glfwKey = ToGLFWKey(key);
        bool prev = false, curr = false;
        
        auto prevIt = mPreviousKeyState.find(glfwKey);
        if (prevIt != mPreviousKeyState.end()) prev = prevIt->second;

        auto currIt = mCurrentKeyState.find(glfwKey);
        if (currIt != mCurrentKeyState.end()) curr = currIt->second;

        return !prev && curr;
    }

    bool GLFWInput::WasKeyReleased(Engine::Input::Key key) const {
        int glfwKey = ToGLFWKey(key);

        bool prev = false, curr = false;

        auto prevIt = mPreviousKeyState.find(glfwKey);
        if (prevIt != mPreviousKeyState.end()) prev = prevIt->second;

        auto currIt = mCurrentKeyState.find(glfwKey);
        if (currIt != mCurrentKeyState.end()) curr = currIt->second;

        return prev && !curr;
    }

    bool GLFWInput::IsMouseDown(Engine::Input::MouseButton button) const
    {
        auto it = mCurrentMouseState.find(ToGLFWMouseButton(button));
        return it != mCurrentMouseState.end() && it->second;
    }

    bool GLFWInput::IsMouseUp(Engine::Input::MouseButton button) const
    {
        return !IsMouseDown(button);
    }

    bool GLFWInput::WasMousePressed(Engine::Input::MouseButton button) const
    {
        int glfwButton = ToGLFWMouseButton(button);

        bool prev = false, curr = false;

        auto prevIt = mPreviousMouseState.find(glfwButton);
        if (prevIt != mPreviousMouseState.end()) prev = prevIt->second;

        auto currIt = mCurrentMouseState.find(glfwButton);
        if (currIt != mCurrentMouseState.end()) curr = currIt->second;

        return !prev && curr;
    }

    bool GLFWInput::WasMouseReleased(Engine::Input::MouseButton button) const
    {
        int glfwButton = ToGLFWMouseButton(button);

        bool prev = false, curr = false;

        auto prevIt = mPreviousMouseState.find(glfwButton);
        if (prevIt != mPreviousMouseState.end()) prev = prevIt->second;

        auto currIt = mCurrentMouseState.find(glfwButton);
        if (currIt != mCurrentMouseState.end()) curr = currIt->second;

        return prev && !curr;
    }
    
    void GLFWInput::SetCursorVisibility(Engine::Core::Platform::CursorVisibility visibility) const
    {
        glfwSetInputMode(win, GLFW_CURSOR, visibility == Engine::Core::Platform::CursorVisibility::Visible ? GLFW_CURSOR_NORMAL : (visibility == Engine::Core::Platform::CursorVisibility::Hidden ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_DISABLED));
        if (visibility == Engine::Core::Platform::CursorVisibility::Disabled && glfwRawMouseMotionSupported()){
            glfwSetInputMode(win, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
        else if(glfwRawMouseMotionSupported()){
            glfwSetInputMode(win, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }
    }

    void GLFWInput::GetCursorPos(double *x, double *y) const
    {
        glfwGetCursorPos(win, x, y);
    }

    void GLFWInput::SetCursorPos(double x, double y) const
    {
        glfwSetCursorPos(win, x, y);
    }
};