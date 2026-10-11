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

            /// @brief Sets the skybox's material up for its environment map (pipeline, cubemap). The renderer draws
            /// the sky from the environment proxy, which carries this material : a skybox dropped from its world
            /// stops being drawn at the next extraction.
            void CreateDrawCommands();

        private:

            std::shared_ptr<Rendering::EnvironmentMap> m_EnvMap;
            std::shared_ptr<Rendering::Material> m_Material;
    };
}