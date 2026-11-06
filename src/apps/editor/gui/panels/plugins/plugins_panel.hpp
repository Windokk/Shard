#pragma once

#include <string>

namespace Shard::Editor::GUI{

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
