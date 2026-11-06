#pragma once

#include <glm/glm.hpp>

#include "engine/world/components/component.hpp"

#include "engine/assets/reflection/attributes.hpp"

namespace Shard::Engine::Objects::Components
{
    class CLASS() Volume : public Component{
        public:
            Volume(std::shared_ptr<Actor> parent, uint32_t local_id);

            FIELD(Editable, tooltip=Half of the size of the box along each axis)
            glm::vec3 halfExtent = glm::vec3(1.0f);
    };
}
