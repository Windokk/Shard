#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Shard::Editor::GUI{

    class PluginsPanel
    {
        public:
            void Draw(bool* open);

        private:
            void DrawList();
            void DrawDetails();
            void DrawProblems();
            void DrawInstall();
            void OpenImportDialog(bool bundle, bool folder);
            void ProcessImports();

            struct ImportQueue{
                std::mutex mutex;
                std::vector<std::string> paths;
                bool bundle = false;
                bool pending = false;
                std::string lastFolder;
            };

            std::shared_ptr<ImportQueue> m_Imports = std::make_shared<ImportQueue>();
            char m_InstallOnly[256] = {};
            std::string m_Selected;
            std::string m_LastMessage;
            bool m_LastMessageIsError = false;
    };
}
