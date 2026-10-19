#pragma once

#include <string>

namespace Shard::Editor::GUI{

    /// @brief The plugins of the running editor : their state, what they cost, what they registered, and the buttons to stop,
    /// start and reload them. What a plugin could not do is written next to it.
    class PluginsPanel
    {
        public:
            void Draw(bool* open);

        private:
            void DrawList();
            void DrawDetails();
            void DrawProblems();

            std::string m_Selected;
            std::string m_LastMessage;
            bool m_LastMessageIsError = false;
    };
}
