#pragma once

#include "engine/world/components/script.hpp"
#include "engine/world/components/registry/component_macros.hpp"
#include "engine/assets/assetID.hpp"

#include <memory>
#include <vector>


class CLASS() Gun : public Shard::Engine::Objects::Components::Script{

    public:
        Gun(std::shared_ptr<Actor> parent, uint32_t local_id) : Script(parent, local_id){};

        void OnPlay() override;
        void OnTick() override;

        FIELD(Editable, range=0.0|1000.0)
        float gunShotIntensity = 30.f;

        FIELD(Editable, range=0.0|60.0, tooltip=Seconds before a bullet is destroyed)
        float bulletLifetime = 3.f;

        FIELD(Editable, tooltip=The mesh to shoot)
        Shard::Engine::Filesystem::AssetID mesh;

        FIELD(Editable, tooltip=The material of the mesh to shoot)
        Shard::Engine::Filesystem::AssetID material;

        FIELD(Editable, tooltip=The sound to play on shoot)
        Shard::Engine::Filesystem::AssetID sound;

    private:

        /// @brief Fires a bullet that is destroyed `time` seconds later
        void Shoot(float time);

        struct Bullet {
            std::weak_ptr<Actor> actor;
            float remaining;
        };
        std::vector<Bullet> bullets;

        DECLARE_DESCRIPTOR(Gun);
};

DECLARE_COMPONENT(Gun)