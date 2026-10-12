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

    /// @brief Set the light's type
    /// @param type The new type (Directional, Spot, Point...)
    void Light::SetType(Rendering::LightType type)
    {
        if(!activated)
            return;

        lightData->type = (int)type;

        UpdateExposedValues();
    }

    /// @brief Set the light's intensity
    /// @param intensity The new intensity
    void Light::SetIntensity(float intensity)
    {
        if(!activated)
            return;

        lightData->intensity = intensity;

        UpdateExposedValues();
    }

    /// @brief Set the light's position in the world
    /// @param postion The new position (in world units)
    void Light::SetPosition(glm::vec3 postion)
    {
        if(!activated)
            return;

        lightData->position = glm::vec4(postion, 0);

}

    /// @brief Set the light's direction (Only for spot and directionnal lights)
    /// @param direction The new direction
    void Light::SetDirection(glm::vec3 direction)
    {
        if(!activated)
            return;

        lightData->direction = glm::vec4(glm::normalize(direction), 0);

}
    
    /// @brief Set the radius of the light (Only for spot and point lights)
    /// @param radius The new radius (in world units)
    void Light::SetRadius(float radius)
    {
        if(!activated)
            return;

        lightData->radius = radius;

        UpdateExposedValues();
    }

    /// @brief Sets the color of the light
    /// @param color The new color
    void Light::SetColor(COL_RGB color)
    {
        if(!activated)
            return;

        lightData->color = glm::vec4((glm::vec3)color, 0);

        UpdateExposedValues();
    }

    /// @brief Set the outer cuttof (Only for spot lights)
    /// @param cutoff The new cutoff, in degrees
    void Light::SetOuterCutoff(float cutoff)
    {
        if(!activated)
            return;

        lightData->outerCutoff = glm::cos(glm::radians(cutoff));

        UpdateExposedValues();
    }

    /// @brief Set the inner cuttof (Only for spot lights)
    /// @param cutoff The new cutoff, in degrees
    void Light::SetInnerCuttoff(float cutoff)
    {
        if(!activated)
            return;

        lightData->innerCutoff = glm::cos(glm::radians(cutoff));

        UpdateExposedValues();
    }

    /// @brief Set wether this light should cast shadows
    /// @param castShadows true : casts shadows, false : doesn't cast shadows
    void Light::SetCastShadow(bool castShadows)
    {
        if(!activated)
            return;

        lightData->castShadow = castShadows;

        UpdateExposedValues();
    }

    /// @brief Set the size of the light's emitter, for the soft shadows of the offline renderers
    /// @param sourceSize Radius of the emitting sphere in world units (point / spot), angular radius of the sun disc in degrees (directional). 0 : hard shadows
    void Light::SetSourceSize(float sourceSize)
    {
        if(!activated)
            return;

        lightData->sourceSize = std::max(sourceSize, 0.0f);

        UpdateExposedValues();
    }

    /// @brief Set what this light does for baked lighting
    /// @param mode Dynamic (never baked), Stationary (indirect light baked) or Static (fully baked)
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

        // Levels saved before the serialization went through the reflection : the light's type was a lowercase
        // string called "light_type", and the shadow flag "castShadow"
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

        // A light follows its actor : its position and direction are not fields, they come from the transform
        glm::vec3 pos(0.0f);
        glm::vec3 dir(0.0f, -1.0f, 0.0f);

        if (parent && parent->transform)
        {
            pos = parent->transform->GetWorldPosition();
            dir = parent->transform->GetWorldForward();
        }

        SetPosition(pos);
        SetDirection(dir);

        // The fields are applied through their setters (OnFieldChanged), which feed the renderer's light data
        DeserializeReflected(data, false);
    }

    ordered_json Light::Serialize()
    {
        // The fields mirror the light data, which is what the setters wrote to
        UpdateExposedValues();

        return SerializeReflected("light");
    }

    /// @brief Getter for this light component's data
    /// @return A copy of this light component's data
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
        if(event.field->name == "lightType")
        {
            SetType(lightType);
        }
        if(event.field->name == "intensity"){
            SetIntensity(intensity);
        }
        if(event.field->name == "radius"){
            SetRadius(radius);
        }
        if(event.field->name == "color")
        {
            SetColor(glm::vec3(color));
        }
        if(event.field->name == "outerCutoff")
        {
            SetOuterCutoff(outerCutoff);
        }
        if(event.field->name == "innerCutoff")
        {
            SetInnerCuttoff(innerCutoff);
        }
        if(event.field->name == "castShadows")
        {
            SetCastShadow(castShadows);
        }
        if(event.field->name == "sourceSize")
        {
            SetSourceSize(sourceSize);
        }
        if(event.field->name == "bakeMode")
        {
            SetBakeMode(bakeMode);
        }
    }

}