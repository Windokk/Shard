#pragma once

#include <glm/glm.hpp>

#include "engine/world/components/component.hpp"

#include "engine/assets/reflection/attributes.hpp"

namespace Shard::Engine::Objects::Components
{
    // Abstract base for components that occupy a bounding-box region of the world (GI probe volumes,
    // and future volume types - post-process, reflection, trigger, ...). Not directly usable on its
    // own (no Clone()/GetDescriptor() override - GetDescriptor() stays pure virtual from Component), so
    // it never appears in the editor's "Add Component" list by itself. The box is only data : the editor
    // is what draws it over the scene, a game never does.
    class CLASS() Volume : public Component{
        public:
            Volume(std::shared_ptr<Actor> parent, uint32_t local_id);

            FIELD(Editable)
            glm::vec3 halfExtent = glm::vec3(1.0f);
    };
}
