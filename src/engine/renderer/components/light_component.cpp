#include "light_component.hpp"

#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/frontend/render_world_data.hpp"
#include "engine/world/components/transform.hpp"
#include "engine/world/actor.hpp"

#include "engine/world/engine.hpp"
#include "engine/core/diagnostics/logger.hpp"

#include "light_component.reflection.hpp"

#include "glm/ext.hpp"

#include <algorithm>

using namespace Shard::Engine::Core;

namespace Shard::Engine::Objects::Components{

    void Light::UpdateExposedValues(){
        type = (Rendering::LightType)lightData->type;
        radius = lightData->radius;
        intensity = lightData->intensity;
        outerCutoff = glm::degrees(glm::acos(lightData->outerCutoff));
        innerCutoff = glm::degrees(glm::acos(lightData->innerCutoff));
        color = glm::vec3(lightData->color);
        castShadows = lightData->castShadow;
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

        if(parent && parent->world && parent->world->IsLoaded())
            GetEngineContext()->GetRenderer()->GetLightManager()->Update(lightIndex);

        UpdateExposedValues();
    }

    /// @brief Set the light's intensity
    /// @param intensity The new intensity
    void Light::SetIntensity(float intensity)
    {
        if(!activated)
            return;

        lightData->intensity = intensity;

        if(parent && parent->world && parent->world->IsLoaded())
            GetEngineContext()->GetRenderer()->GetLightManager()->Update(lightIndex);
        
        UpdateExposedValues();
    }

    /// @brief Set the light's position in the world
    /// @param postion The new position (in world units)
    void Light::SetPosition(glm::vec3 postion)
    {
        if(!activated)
            return;

        lightData->position = glm::vec4(postion, 0);

        if(parent && parent->world && parent->world->IsLoaded())
            GetEngineContext()->GetRenderer()->GetLightManager()->Update(lightIndex);
    }

    /// @brief Set the light's direction (Only for spot and directionnal lights)
    /// @param direction The new direction
    void Light::SetDirection(glm::vec3 direction)
    {
        if(!activated)
            return;

        lightData->direction = glm::vec4(glm::normalize(direction), 0);
            
        if(parent && parent->world && parent->world->IsLoaded())
            GetEngineContext()->GetRenderer()->GetLightManager()->Update(lightIndex);
    }
    
    /// @brief Set the radius of the light (Only for spot and point lights)
    /// @param radius The new radius (in world units)
    void Light::SetRadius(float radius)
    {
        if(!activated)
            return;

        lightData->radius = radius;
        
        if(parent && parent->world && parent->world->IsLoaded())
            GetEngineContext()->GetRenderer()->GetLightManager()->Update(lightIndex);

        UpdateExposedValues();
    }

    /// @brief Sets the color of the light
    /// @param color The new color
    void Light::SetColor(COL_RGB color)
    {
        if(!activated)
            return;

        lightData->color = glm::vec4((glm::vec3)color, 0);
        
        if(parent && parent->world && parent->world->IsLoaded())
            GetEngineContext()->GetRenderer()->GetLightManager()->Update(lightIndex);

        UpdateExposedValues();
    }

    /// @brief Set the outer cuttof (Only for spot lights)
    /// @param cutoff The new cutoff, in degrees
    void Light::SetOuterCutoff(float cutoff)
    {
        if(!activated)
            return;

        lightData->outerCutoff = glm::cos(glm::radians(cutoff));
        
        if(parent && parent->world && parent->world->IsLoaded())
            GetEngineContext()->GetRenderer()->GetLightManager()->Update(lightIndex);

        UpdateExposedValues();
    }

    /// @brief Set the inner cuttof (Only for spot lights)
    /// @param cutoff The new cutoff, in degrees
    void Light::SetInnerCuttoff(float cutoff)
    {
        if(!activated)
            return;

        lightData->innerCutoff = glm::cos(glm::radians(cutoff));
        
        if(parent && parent->world && parent->world->IsLoaded())
            GetEngineContext()->GetRenderer()->GetLightManager()->Update(lightIndex);

        UpdateExposedValues();
    }

    /// @brief Set the light's index in the scene
    /// @param index This new light's index
    void Light::SetLightIndex(int index)
    {
        if(lightIndex != -1 || !activated)
            return;
        
        if(parent && parent->world && parent->world->IsLoaded()){
            GetEngineContext()->GetRenderer()->GetLightManager()->AddLight(index, lightData);
            GetEngineContext()->GetRenderer()->GetLightManager()->Update(index);
            lightIndex = index;
        }
    }
    
    /// @brief Set wether this light should cast shadows
    /// @param castShadows true : casts shadows, false : doesn't cast shadows
    void Light::SetCastShadow(bool castShadows)
    {
        if(!activated)
            return;

        lightData->castShadow = castShadows;
        
        if(parent && parent->world && parent->world->IsLoaded())
            GetEngineContext()->GetRenderer()->GetLightManager()->Update(lightIndex);
            
        UpdateExposedValues();
    }

    /// @brief Re-registers this light with the LightManager (mirrors the registration Actor::AddComponent
    /// does for a freshly-created light), so a light turned back on in the editor lights the scene again.
    void Light::Activate()
    {
        bool wasInactive = !activated;
        Component::Activate();

        if(!wasInactive || lightIndex != -1)
            return;

        if(!parent || !parent->world || !parent->world->IsLoaded())
            return;

        auto& lights = parent->world->Ext<Rendering::RenderWorldData>().lights;
        SetLightIndex((int)lights.size());
        lights.push_back(AsShared<Light>());
    }

    void Light::OnTransformChanged(uint8_t changes)
    {
        if(changes & TransformPosition)
            SetPosition(parent->transform->GetWorldPosition());

        if(changes & TransformRotation)
            SetDirection(parent->transform->GetWorldForward());
    }

    /// @brief Pulls this light out of the LightManager's active buffer (mirrors World::RemoveComponent's
    /// Light branch) without fully destroying the component, so it stops lighting the scene until
    /// Activate() re-adds it.
    void Light::DeActivate()
    {
        if(!activated){
            Component::DeActivate();
            return;
        }

        Component::DeActivate();

        if(!parent || !parent->world || lightIndex == -1)
            return;

        auto& lights = parent->world->Ext<Rendering::RenderWorldData>().lights;
        int index = lightIndex;

        if(index < 0 || index >= (int)lights.size())
            return;

        GetEngineContext()->GetRenderer()->GetLightManager()->RemoveLight(index);

        std::rotate(lights.begin() + index, lights.begin() + index + 1, lights.end());
        lights.pop_back();

        // Everything past the removed slot shifted down by one - resync each light's cached index.
        for(size_t i = index; i < lights.size(); ++i)
            lights[i]->ReindexTo((int)i);

        lightIndex = -1;
    }

    void Light::Deserialize(const json componentData)
    {
        auto getString = [&](const char* key) -> std::optional<std::string>
        {
            if (!componentData.contains(key) || !componentData[key].is_string())
                return std::nullopt;
            return componentData[key].get<std::string>();
        };

        auto getFloat = [&](const char* key, float fallback = 0.0f) -> float
        {
            if (!componentData.contains(key) || !componentData[key].is_number())
                return fallback;
            return componentData[key].get<float>();
        };

        auto getBool = [&](const char* key, bool fallback = false) -> bool
        {
            if (!componentData.contains(key) || !componentData[key].is_boolean())
                return fallback;
            return componentData[key].get<bool>();
        };

        auto lightTypeOpt = getString("light_type");
        if (!lightTypeOpt)
        {
            DEBUG_ERROR("Light missing 'light_type'");
            return;
        }

        // ---------------- TYPE ----------------
        if (*lightTypeOpt == "directional")
            SetType(Rendering::LightType::Directional);
        else if (*lightTypeOpt == "point")
            SetType(Rendering::LightType::Point);
        else if (*lightTypeOpt == "spot")
            SetType(Rendering::LightType::Spot);
        else
        {
            DEBUG_ERROR("Unknown light type: " + *lightTypeOpt);
            return;
        }

        // ---------------- TRANSFORM SAFETY ----------------
        glm::vec3 pos(0.0f);
        glm::vec3 dir(0.0f, -1.0f, 0.0f);

        if (parent && parent->transform)
        {
            pos = parent->transform->GetWorldPosition();
            dir = parent->transform->GetWorldForward();
        }

        SetPosition(pos);
        SetDirection(dir);

        // ---------------- REQUIRED VALUES ----------------
        float intensity = getFloat("intensity", 1.0f);
        float radius    = getFloat("radius", 10.0f);

        SetIntensity(intensity);
        SetRadius(radius);

        // ---------------- COLOR ----------------
        glm::vec3 color(1.0f);

        if (componentData.contains("color") && componentData["color"].is_object())
        {
            const auto& c = componentData["color"];

            auto safe = [&](const char* k) -> float
            {
                if (c.contains(k) && c[k].is_number())
                    return c[k].get<float>();
                return 1.0f;
            };

            color = glm::vec3(
                safe("r"),
                safe("g"),
                safe("b")
            );
        }
        else
        {
            DEBUG_ERROR("Light missing or invalid 'color', defaulting to white");
        }

        SetColor(COL_RGB(color.r, color.g, color.b));

        // ---------------- SPOTLIGHT PARAMS ----------------
        SetInnerCuttoff(getFloat("innerCutoff", 0.0f));
        SetOuterCutoff(getFloat("outerCutoff", 0.0f));

        // ---------------- SHADOW ----------------
        SetCastShadow(getBool("castShadow", false));

        // ---------------- ACTIVE ----------------
        if (getBool("active", false))
            Activate();
        else
            DeActivate();
    }

    ordered_json Light::Serialize()
    {
        ordered_json comp;

        comp["type"] = "light";

        comp["active"] = activated;

        switch(lightData->type){
            case (int)Rendering::LightType::Directional:{
                comp["light_type"] = "directional";
                break;
            }
            case (int)Rendering::LightType::Spot:{
                comp["light_type"] = "spot";
                break;
            }
            case (int)Rendering::LightType::Point:{
                comp["light_type"] = "point";
                break;
            }
        }

        comp["intensity"] = lightData->intensity;
        comp["radius"] = lightData->radius;
        comp["color"]["r"] = lightData->color.r;
        comp["color"]["g"] = lightData->color.g;
        comp["color"]["b"] = lightData->color.b;
        comp["innerCutoff"] = glm::degrees(glm::acos(lightData->innerCutoff));
        comp["outerCutoff"] = glm::degrees(glm::acos(lightData->outerCutoff));
        comp["castShadow"] = static_cast<bool>(lightData->castShadow);

        return comp;
    }

    /// @brief Getter for this light component's data
    /// @return A copy of this light component's data
    Rendering::LightData Light::GetData()
    {
        return *lightData.get();
    }

    void Light::Destroy()
    {
        GetEngineContext()->GetRenderer()->GetLightManager()->RemoveLight(lightIndex);
    }

    std::shared_ptr<Component> Light::Clone() const
    {
        auto cloned = Object::Create<Light>(*this);

        cloned->lightData = std::make_shared<Rendering::LightData>(*lightData);
        cloned->lightIndex = -1;

        return cloned;
    }

    void Light::OnFieldChanged(const FieldChangedEvent &event)
    {
        if(event.field->name == "type")
        {
            SetType(type);
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
    }

}