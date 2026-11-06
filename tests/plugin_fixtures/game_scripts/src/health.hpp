#pragma once

#include <shard/game.hpp>

class Health : public Shard::Script {
public:
    float maxHealth = 100.f;
    float current = 100.f;
    int32_t timesHurt = 0;
    Shard::Text label;
    Shard::AssetId icon;

    void OnCreate() override;
    void OnPlay() override;
    void OnTick(float deltaTime) override;
    void OnFixedTick(float fixedDeltaTime) override;
    void OnLateTick(float deltaTime) override;
    void OnStop() override;
    void OnDestroyed() override;

    void Damage(float amount);

private:
    int m_NotAField = 7;
};
