#pragma once

#include <string>
#include <vector>

#include "engine/platform/keys.hpp"

namespace Shard::Engine::Core::Platform {

    enum CursorVisibility{
        Visible,
        Hidden,
        Disabled
    };

    class IInput {
    public:
        virtual ~IInput() = default;
        virtual void Init() = 0;
        virtual void Tick() = 0;
        virtual void Shutdown() = 0;

        // Keyboard

        /// @brief Returns true if the key is currently held down.
        /// @param key The key to query.
        /// @return True while the key is pressed; false otherwise.
        virtual bool IsKeyDown(Input::Key key) const = 0;
        
        /// @brief Returns true if the key is currently not pressed.
        /// @param key The key to query.
        /// @return True while the key is released; false otherwise.
        virtual bool IsKeyUp(Input::Key key) const = 0;

        /// @brief Returns true only on the tick/frame the key transitions from released to pressed.
        /// @param key The key to query.
        /// @return True for exactly one tick when the key is initially pressed; false if the key is held or not pressed.
        virtual bool WasKeyPressed(Input::Key key) const = 0;
        
        /// @brief Returns true only on the tick/frame the key transitions from pressed to released.
        /// @param key The key to query.
        /// @return True for exactly one tick when the key is initially released; false if the key is held or already released.
        virtual bool WasKeyReleased(Input::Key key) const = 0;

        // Mouse/Cursor

        /// @brief Returns true if the mouse button is currently held down.
        /// @param button The mouse button to query.
        /// @return True while the button is pressed; false otherwise.
        virtual bool IsMouseDown(Input::MouseButton button) const = 0;

        /// @brief Returns true if the mouse button is currently not pressed.
        /// @param button The mouse button to query.
        /// @return True while the button is released; false otherwise.
        virtual bool IsMouseUp(Input::MouseButton button) const = 0;

        /// @brief Returns true only on the tick/frame the mouse button transitions from released to pressed.
        /// @param button The mouse button to query.
        /// @return True for exactly one tick when the button is initially pressed; false if held or not pressed.
        virtual bool WasMousePressed(Input::MouseButton button) const = 0;

        /// @brief Returns true only on the tick/frame the mouse button transitions from pressed to released.
        /// @param button The mouse button to query.
        /// @return True for exactly one tick when the button is initially released; false if held or already released.
        virtual bool WasMouseReleased(Input::MouseButton button) const = 0;

        virtual void SetCursorVisibility(CursorVisibility visible) const = 0;
        virtual void GetCursorPos(double* x, double* y) const = 0;
        virtual void SetCursorPos(double x, double y) const = 0;
    };

}