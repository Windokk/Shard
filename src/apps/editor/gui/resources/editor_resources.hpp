#pragma once

#include "icon_atlas.hpp"

namespace Shard::Editor::GUI {

    class EditorResources
    {
        public:
            // Access the singleton instance
            static EditorResources& Instance()
            {
                static EditorResources instance; // thread-safe in C++11+
                return instance;
            }

            // Initialize resources (build atlas, load icons, etc.)
            void Init()
            {
                if (!iconAtlas)
                {
                    iconAtlas = std::make_shared<IconAtlas>();

                    // Example: list of icon file paths
                    std::vector<std::string> iconPaths = {
                        "editor_resources/icons/image.png",
                        "editor_resources/icons/audio.png",
                        "editor_resources/icons/font.png",
                        "editor_resources/icons/shader.png",
                        "editor_resources/icons/compute.png",
                        "editor_resources/icons/text.png",
                        "editor_resources/icons/code.png",
                        "editor_resources/icons/level.png",
                        "editor_resources/icons/model.png",
                        "editor_resources/icons/material.png",
                        "editor_resources/icons/config.png",
                        "editor_resources/icons/folder.png"
                    };

                    iconAtlas->Build(iconPaths, 1024);
                }
            }

            std::shared_ptr<IconAtlas> GetIconAtlas() { return iconAtlas; }

            void Shutdown()
            {
                iconAtlas.reset();
            }

        private:
            EditorResources() = default;
            ~EditorResources() = default;

            EditorResources(const EditorResources&) = delete;
            EditorResources& operator=(const EditorResources&) = delete;

            std::shared_ptr<IconAtlas> iconAtlas;
    };
}