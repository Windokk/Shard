#pragma once

#include <nlohmann/json.hpp>

#include "engine/world/levels/level.hpp"
#include "engine/world/actor.hpp"

#include <memory>
#include <typeinfo>

#include "engine/assets/reflection/reflection_fields.hpp"

namespace Shard::Editor::GUI{

    class EditorMainWindow;

    class PropertiesPanel
    {
        public:
            void Draw(std::shared_ptr<Engine::Objects::Actor> actor);

        private:
            void DrawActorInfo(std::shared_ptr<Engine::Objects::Actor> actor);
            void DrawAddComponentMenu(std::shared_ptr<Engine::Objects::Actor> actor);
            bool DrawComponent(std::shared_ptr<Engine::Objects::Components::Component> comp);
            void DrawField(const FieldInfo* field, void* value,
                        std::shared_ptr<Engine::Objects::Components::Component> comp,
                        const Container* container = nullptr, const int valueIndexInContainer = -1);
    };
}