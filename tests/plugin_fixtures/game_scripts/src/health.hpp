#pragma once

#include <shard/game.hpp>

// A script with fields of several kinds : numbers, a text, an asset
class Health : public shard::Script {
public:
    float maxHealth = 100.f;
    float current = 100.f;
    int32_t timesHurt = 0;
    shard::Text label;
    shard::AssetId icon;

    void OnCreate() override;
    void OnPlay() override;
    void OnTick(float deltaTime) override;
    void OnFixedTick(float fixedDeltaTime) override;
    void OnLateTick(float deltaTime) override;
    void OnStop() override;
    void OnDestroyed() override;

    void Damage(float amount);

private:
    int m_NotAField = 7;        // not listed : starts fresh in a copy of the actor
};
