#include "light_component.hpp"

#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/components/render_world_data.hpp"
#include "engine/world/components/transform.hpp"
#include "engine/world/actor.hpp"

#include "engine/world/engine.hpp"
#include "engine/core/diagnostics/logger.hpp"

#include "light_component.reflection.hpp"

#include "glm/ext.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

using namespace Shard::Engine::Core;

namespace Shard::Engine::Objects::Components{

    void Light::UpdateExposedValues(){
        lightType = (Rendering::LightType)lightData->type;
        radius = lightData->radius;
        intensity = lightData->intensity;
        outerCutoff = glm::degrees(glm::acos(lightData->outerCutoff));
        innerCutoff = glm::degrees(glm::acos(lightData->innerCutoff));
        color = glm::vec3(lightData->color);
        castShadows = lightData->castShadow;
        sourceSize = lightData->sourceSize;
        bakeMode = (Rendering::LightBakeMode)lightData->bakeMode;
    }

    Light::Light(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {
        lightData = std::make_shared<Rendering::LightData>();

        std::shared_ptr<Transform> tr = parent->transform;

        lightData->position = glm::vec4(tr->GetWorldPosition(), 0);
        lightData->direction = glm::vec4(tr->GetWorldForward(), 0);
        
        UpdateExposedValues();
    }

    void Light::SetType(Rendering::LightType type)
    {
        if(!activated)
            return;

        lightData->type = (int)type;

        UpdateExposedValues();
    }

    void Light::SetIntensity(float intensity)
    {
        if(!activated)
            return;

        lightData->intensity = intensity;

        UpdateExposedValues();
    }

    void Light::SetPosition(glm::vec3 postion)
    {
        if(!activated)
            return;

        lightData->position = glm::vec4(postion, 0);

}

    void Light::SetDirection(glm::vec3 direction)
    {
        if(!activated)
            return;

        lightData->direction = glm::vec4(glm::normalize(direction), 0);

}
    
    void Light::SetRadius(float radius)
    {
        if(!activated)
            return;

        lightData->radius = radius;

        UpdateExposedValues();
    }

    void Light::SetColor(COL_RGB color)
    {
        if(!activated)
            return;

        lightData->color = glm::vec4((glm::vec3)color, 0);

        UpdateExposedValues();
    }

    void Light::SetOuterCutoff(float cutoff)
    {
        if(!activated)
            return;

        lightData->outerCutoff = glm::cos(glm::radians(cutoff));

        UpdateExposedValues();
    }

    void Light::SetInnerCuttoff(float cutoff)
    {
        if(!activated)
            return;

        lightData->innerCutoff = glm::cos(glm::radians(cutoff));

        UpdateExposedValues();
    }

    void Light::SetCastShadow(bool castShadows)
    {
        if(!activated)
            return;

        lightData->castShadow = castShadows;

        UpdateExposedValues();
    }

    // Emitter size for soft shadows of the offline renderers. Radius in world units (point/spot), angular radius of the sun disc in degrees (directional). 0 is hard shadows.
    void Light::SetSourceSize(float sourceSize)
    {
        if(!activated)
            return;

        lightData->sourceSize = std::max(sourceSize, 0.0f);

        UpdateExposedValues();
    }

    void Light::SetBakeMode(Rendering::LightBakeMode mode)
    {
        if(!activated)
            return;

        lightData->bakeMode = (int)mode;

        UpdateExposedValues();
    }

    void Light::Deserialize(const json componentData)
    {
        json data = componentData;

        // Old levels: type was a lowercase string "light_type", shadow flag was "castShadow"
        if (data.is_object())
        {
            if (!data.contains("lightType") && data.contains("light_type") && data["light_type"].is_string())
            {
                std::string legacyType = data["light_type"].get<std::string>();
                if (!legacyType.empty())
                    legacyType[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(legacyType[0])));
                data["lightType"] = legacyType;
            }

            if (!data.contains("castShadows") && data.contains("castShadow"))
                data["castShadows"] = data["castShadow"];
        }

        // Position and direction are not fields, they come from the transform
        glm::vec3 pos(0.0f);
        glm::vec3 dir(0.0f, -1.0f, 0.0f);

        if (parent && parent->transform)
        {
            pos = parent->transform->GetWorldPosition();
            dir = parent->transform->GetWorldForward();
        }

        SetPosition(pos);
        SetDirection(dir);

        DeserializeReflected(data, false);
    }

    ordered_json Light::Serialize()
    {
        UpdateExposedValues();

        return SerializeReflected("light");
    }

    Rendering::LightData Light::GetData()
    {
        return *lightData.get();
    }

    void Light::Destroy()
    {
    }

    std::shared_ptr<Component> Light::Clone() const
    {
        auto cloned = Object::Create<Light>(*this);

        cloned->lightData = std::make_shared<Rendering::LightData>(*lightData);

        return cloned;
    }

    void Light::OnFieldChanged(const FieldChangedEvent &event)
    {
        if(std::strcmp(event.field->name, "lightType") == 0)
        {
            SetType(lightType);
        }
        if(std::strcmp(event.field->name, "intensity") == 0){
            SetIntensity(intensity);
        }
        if(std::strcmp(event.field->name, "radius") == 0){
            SetRadius(radius);
        }
        if(std::strcmp(event.field->name, "color") == 0)
        {
            SetColor(glm::vec3(color));
        }
        if(std::strcmp(event.field->name, "outerCutoff") == 0)
        {
            SetOuterCutoff(outerCutoff);
        }
        if(std::strcmp(event.field->name, "innerCutoff") == 0)
        {
            SetInnerCuttoff(innerCutoff);
        }
        if(std::strcmp(event.field->name, "castShadows") == 0)
        {
            SetCastShadow(castShadows);
        }
        if(std::strcmp(event.field->name, "sourceSize") == 0)
        {
            SetSourceSize(sourceSize);
        }
        if(std::strcmp(event.field->name, "bakeMode") == 0)
        {
            SetBakeMode(bakeMode);
        }
    }

}