#pragma once

#include "engine/debugging/logger.hpp"

namespace Shard::Editor::Core{
    
    class EditorMainWindow;
}

namespace Shard::Editor::GUI{


    class Console
    {
        public:
            
            void SetParentWindow(Core::EditorMainWindow* parent);
            void Draw();

        private:

            std::vector<std::pair<Engine::Debugging::Level, std::string>> logs;

            Core::EditorMainWindow* parent = nullptr;
    };


}