#pragma once

#include <nlohmann/json.hpp>

#include "engine/world/world.hpp"
#include "engine/world/actor.hpp"

#include <memory>
#include <typeinfo>

#include "engine/assets/reflection/reflection_fields.hpp"
#include "apps/editor/commands/actor_commands.hpp"

#include <string>
#include <vector>

namespace Shard::Editor::GUI{

    class EditorMainWindow;
    class AssetBrowser;

    class PropertiesPanel
    {
        public:
            void Draw(std::shared_ptr<Engine::Objects::Actor> actor);

            /// The browser the asset fields take the selected asset from / navigate to
            void SetAssetBrowser(AssetBrowser* browser) { assetBrowser = browser; }

        private:
            void DrawActorInfo(std::shared_ptr<Engine::Objects::Actor> actor);
            void DrawAddComponentMenu(std::shared_ptr<Engine::Objects::Actor> actor);
            bool DrawComponent(std::shared_ptr<Engine::Objects::Components::Component> comp);
            void DrawField(const FieldInfo* field, void* value,
                        std::shared_ptr<Engine::Objects::Components::Component> comp,
                        const Container* container = nullptr, const int valueIndexInContainer = -1);

            /// DrawField, and what the widgets changed noted for the undo history
            void DrawRecordedField(const FieldInfo* field, std::shared_ptr<Engine::Objects::Components::Component> comp,
                        Commands::FieldOwnerLocator locate = nullptr);

            /// The edits noted while the widgets were being used go into the undo history once none is
            void CommitPendingEdits();

            AssetBrowser* assetBrowser = nullptr;

            // The actor being drawn
            std::shared_ptr<Engine::Objects::Actor> drawnActor;

            // Undo : the fields changed by the widget being used (a drag, a text box...), with the values they had before it
            struct PendingFieldEdit {
                std::shared_ptr<Engine::Objects::Components::Component> component;
                const FieldInfo* field;
                Commands::FieldOwnerLocator locate;
                Commands::ValueBuffer before;
            };
            std::vector<PendingFieldEdit> pendingFieldEdits;

            // Undo : an edit that is not in the reflected fields (the physics shapes), the whole actor saved
            std::unique_ptr<Commands::ActorSnapshotCommand> pendingActorEdit;

            // Undo : the name box
            std::shared_ptr<Engine::Objects::Actor> nameEditActor;
            std::string nameBeforeEdit;
    };
}