#pragma once

#include <unordered_map>
#include <iostream>

#include "engine/input/keys.hpp"
#include "engine/debugging/logger.hpp"
#include "engine/platform/iplatform.hpp"

#include <GLFW/glfw3.h>

namespace Shard::Game::Core::Platform
{
    class GLFWInput : public Engine::Core::Platform::IInput{
        public:
            void Init() override;
            void SetWindow(GLFWwindow* window);
            void Tick() override;
            void Shutdown() override;

            // Keyboard
            bool IsKeyDown(Engine::Input::Key key) const override;
            bool IsKeyUp(Engine::Input::Key key) const override;
            bool WasKeyPressed(Engine::Input::Key key) const override;
            bool WasKeyReleased(Engine::Input::Key key) const override;

            // Mouse
            bool IsMouseDown(Engine::Input::MouseButton button) const override;
            bool IsMouseUp(Engine::Input::MouseButton button) const override;
            bool WasMousePressed(Engine::Input::MouseButton button) const override;
            bool WasMouseReleased(Engine::Input::MouseButton button) const override;

            void SetCursorVisibility(Engine::Core::Platform::CursorVisibility visibility) const override;
            void GetCursorPos(double* x, double* y) const override;
            void SetCursorPos(double x, double y) const override;


        private:

            void KeyCallback(int key, int action);
            void MouseCallback(int button, int action);

            std::unordered_map<int, bool> mCurrentKeyState;
            std::unordered_map<int, bool> mPreviousKeyState;

            std::unordered_map<int, bool> mCurrentMouseState;
            std::unordered_map<int, bool> mPreviousMouseState;
            
            GLFWwindow* win = nullptr;
    };

    static int ToGLFWKey(Engine::Input::Key key) {
        switch (key) {
            case Engine::Input::Key::A: return GLFW_KEY_A;
            case Engine::Input::Key::B: return GLFW_KEY_B;
            case Engine::Input::Key::C: return GLFW_KEY_C;
            case Engine::Input::Key::D: return GLFW_KEY_D;
            case Engine::Input::Key::E: return GLFW_KEY_E;
            case Engine::Input::Key::F: return GLFW_KEY_F;
            case Engine::Input::Key::G: return GLFW_KEY_G;
            case Engine::Input::Key::H: return GLFW_KEY_H;
            case Engine::Input::Key::I: return GLFW_KEY_I;
            case Engine::Input::Key::J: return GLFW_KEY_J;
            case Engine::Input::Key::K: return GLFW_KEY_K;
            case Engine::Input::Key::L: return GLFW_KEY_L;
            case Engine::Input::Key::M: return GLFW_KEY_M;
            case Engine::Input::Key::W: return GLFW_KEY_W;
            case Engine::Input::Key::S: return GLFW_KEY_S;
            case Engine::Input::Key::Y: return GLFW_KEY_Y;
            case Engine::Input::Key::Z: return GLFW_KEY_Z;
            case Engine::Input::Key::Escape: return GLFW_KEY_ESCAPE;
            case Engine::Input::Key::Space: return GLFW_KEY_SPACE;
            case Engine::Input::Key::LeftControl: return GLFW_KEY_LEFT_CONTROL;
            default: return GLFW_KEY_UNKNOWN;
        }
    }

    static int ToGLFWMouseButton(Engine::Input::MouseButton button) {
        switch (button) {
            case Engine::Input::MouseButton::Left: return GLFW_MOUSE_BUTTON_LEFT;
            case Engine::Input::MouseButton::Right: return GLFW_MOUSE_BUTTON_RIGHT;
            case Engine::Input::MouseButton::Middle: return GLFW_MOUSE_BUTTON_MIDDLE;
            default: return -1;
        }
    }
}
