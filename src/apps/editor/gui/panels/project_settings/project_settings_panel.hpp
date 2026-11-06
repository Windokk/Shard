#pragma once

#include "apps/editor/gui/panels/settings_view.hpp"

#include "engine/core/tags/tag.hpp"

#include <string>
#include <vector>

namespace Shard::Engine::Projects{ struct ShadowSettings; }

namespace Shard::Editor::GUI{

    class ProjectSettingsPanel
    {
        public:
            ProjectSettingsPanel();

            void Draw(bool* open);

            bool IsDirty() const;
            bool Save();
            void Revert();

        private:
            void DrawGeneralCategory();
            void DrawBuildCategory();
            void DrawPhysicsCategory();
            void DrawRenderingCategory();
            void DrawConsoleVariablesCategory();

            void DrawTagsCategory();
            void DrawTagNode(Engine::Core::Tag tag);
            void DrawTagPopups();
            void DrawTagReferenceList();
            void ApplyTags();
            bool AddTag(const std::string& name, std::string* error);
            void DeleteTag(const std::string& name);
            bool RenameTag(const std::string& from, const std::string& to, std::string* error);
            void FindTagReferences(Engine::Core::Tag tag);

            bool DrawCascadeEditor(Engine::Projects::ShadowSettings& shadows, const Engine::Projects::ShadowSettings& defaults);

            void PathRow(const char* label, const std::string& path, const char* tooltip);

            void ApplyToEngine();
            bool WriteProjectConfig() const;
            std::string GetProjectConfigPath() const;

            SettingsView m_View;

            int m_SelectedCascade = 0;

            bool m_CVarsDirty = false;

            // Project::HasUnsavedChanges serializes the project, so the answer is cached for a moment.
            mutable bool m_ProjectDirty = false;
            mutable double m_LastDirtyCheck = -1.0;

            std::vector<std::string> m_WorldAssets;

            std::string m_EditingCVar;
            char m_CVarBuffer[256] = "";
            bool m_ProjectCVarsOnly = false;

            char m_NewTagBuffer[256] = "";
            std::string m_NewTagError;
            std::string m_TagPopupTarget;
            char m_TagNameBuffer[256] = "";
            std::string m_TagError;
            std::string m_TagReferencesOf;
            std::vector<std::string> m_TagReferences;
    };
}
