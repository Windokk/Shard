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

            /// @brief (Re)submits the skybox's fullscreen draw command to the ForwardPass. Safe to
            /// call repeatedly - the command is updated in place. Must be re-run after every world
            /// load: ClearPassesContent() (world swap) wipes the pass draw lists, and a cached World
            /// won't re-run the constructor that first added it.
            void CreateDrawCommands();

            /// @brief Removes the skybox's draw command from the ForwardPass, so a skybox that is being
            /// dropped from its world stops rendering. No-op if it was never submitted.
            void RemoveDrawCommands();

        private:

            std::shared_ptr<Rendering::EnvironmentMap> m_EnvMap;
            std::shared_ptr<Rendering::Material> m_Material;
    };
}