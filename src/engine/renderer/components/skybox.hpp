#pragma once

#include "engine/world/world_object.hpp"

namespace Shard::Engine::Rendering{
    class Shader;
    class Material;
    class EnvironmentMap;
    class Cubemap;
}


namespace Shard::Engine::Objects
{
    class Skybox : public WorldObject{

        public:

            Skybox(std::shared_ptr<Rendering::EnvironmentMap> envMap, std::shared_ptr<Rendering::Material> material);

            void SetEnvironmentMap(std::shared_ptr<Rendering::EnvironmentMap> envMap);

            void SetMaterial(std::shared_ptr<Rendering::Material> material);

            void Destroy() override;

            std::shared_ptr<Rendering::EnvironmentMap> GetEnvMap() const { return m_EnvMap; }
            std::shared_ptr<Rendering::Material> GetMaterial() const { return m_Material; }

            void CreateDrawCommands();

        private:

            std::shared_ptr<Rendering::EnvironmentMap> m_EnvMap;
            std::shared_ptr<Rendering::Material> m_Material;
    };
}