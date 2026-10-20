#pragma once

#include "apps/editor/gui/panels/settings_view.hpp"

#include "engine/core/tags/tag.hpp"

#include <string>
#include <vector>

namespace Shard::Engine::Projects{ struct ShadowSettings; }

namespace Shard::Editor::GUI{

    /// @brief Unreal-style "Project Settings" window : what the project file (and its project.cfg) holds, by category, with
    /// a search through all of them. The changes apply at once ; Save writes them, Revert reloads what is on disk.
    class ProjectSettingsPanel
    {
        public:
            ProjectSettingsPanel();

            void Draw(bool* open);

            /// The project or its console variables differ from what was last saved (or loaded)
            bool IsDirty() const;
            /// Writes the project file and project.cfg. False (and an error shown) when one could not be written.
            bool Save();
            /// Reloads the project file and project.cfg, dropping the changes not saved
            void Revert();

        private:
            void DrawGeneralCategory();
            void DrawBuildCategory();
            void DrawPhysicsCategory();
            void DrawRenderingCategory();
            void DrawConsoleVariablesCategory();

            // Tags (project_settings_tags.cpp)
            void DrawTagsCategory();
            void DrawTagNode(Engine::Core::Tag tag);
            void DrawTagPopups();
            void DrawTagReferenceList();
            /// Gives the registry the project's tags again, after an edit of them
            void ApplyTags();
            bool AddTag(const std::string& name, std::string* error);
            void DeleteTag(const std::string& name);
            /// Renames the tag and what is under it, adds the redirects, and renames them in the open world
            bool RenameTag(const std::string& from, const std::string& to, std::string* error);
            /// Fills m_TagReferences : the open world as it is, the other worlds of the project as their files are
            void FindTagReferences(Engine::Core::Tag tag);

            /// The row that edits the sun's cascades : a bar with a handle per split and the color of each cascade in the debug view.
            /// True when something changed.
            bool DrawCascadeEditor(Engine::Projects::ShadowSettings& shadows, const Engine::Projects::ShadowSettings& defaults);

            /// One read-only path, with a button that shows it in the file explorer
            void PathRow(const char* label, const std::string& path, const char* tooltip);

            /// Hands the physics and rendering settings to the engine
            void ApplyToEngine();
            /// Writes the project layer of the console variables to project.cfg
            bool WriteProjectConfig() const;
            std::string GetProjectConfigPath() const;

            SettingsView m_View;

            /// The cascade the cascade editor edits the color of
            int m_SelectedCascade = 0;

            /// The project layer of the console variables was changed since project.cfg was last written or read
            /// (the project file keeps its own saved state : Project::HasUnsavedChanges)
            bool m_CVarsDirty = false;

            /// Project::HasUnsavedChanges serializes the project : its answer is kept for a moment (see IsDirty)
            mutable bool m_ProjectDirty = false;
            mutable double m_LastDirtyCheck = -1.0;

            // Build : the world assets of the project, gathered once per frame the category is drawn
            std::vector<std::string> m_WorldAssets;

            // Console variables : the text being typed in one value, applied when the field is left
            std::string m_EditingCVar;
            char m_CVarBuffer[256] = "";
            bool m_ProjectCVarsOnly = false;

            // Tags : the new tag box, and what the popups (add a child, rename, delete) work on
            char m_NewTagBuffer[256] = "";
            std::string m_NewTagError;
            std::string m_TagPopupTarget;
            char m_TagNameBuffer[256] = "";
            std::string m_TagError;
            std::string m_TagReferencesOf;
            std::vector<std::string> m_TagReferences;
    };
}
