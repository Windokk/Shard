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
            void Draw(std::shared_ptr<Engine::Objects::Actor> actor, int selectionCount = 1);

            void SetAssetBrowser(AssetBrowser* browser) { assetBrowser = browser; }

        private:
            void DrawActorInfo(std::shared_ptr<Engine::Objects::Actor> actor);
            void DrawAddComponentMenu(std::shared_ptr<Engine::Objects::Actor> actor);
            bool DrawComponent(std::shared_ptr<Engine::Objects::Components::Component> comp);
            void DrawField(const FieldInfo* field, void* value,
                        std::shared_ptr<Engine::Objects::Components::Component> comp,
                        const Container* container = nullptr, const int valueIndexInContainer = -1);

            void DrawRecordedField(const FieldInfo* field, std::shared_ptr<Engine::Objects::Components::Component> comp,
                        Commands::FieldOwnerLocator locate = nullptr);

            void CommitPendingEdits();

            AssetBrowser* assetBrowser = nullptr;

            std::shared_ptr<Engine::Objects::Actor> drawnActor;

            struct PendingFieldEdit {
                std::shared_ptr<Engine::Objects::Components::Component> component;
                const FieldInfo* field;
                Commands::FieldOwnerLocator locate;
                Commands::ValueBuffer before;
            };
            std::vector<PendingFieldEdit> pendingFieldEdits;

            std::unique_ptr<Commands::ActorSnapshotCommand> pendingActorEdit;

            std::shared_ptr<Engine::Objects::Actor> nameEditActor;
            std::string nameBeforeEdit;
    };
}