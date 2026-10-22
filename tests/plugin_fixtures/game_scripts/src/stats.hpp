// What the scripts of the fixture saw, for the test to read (see module.cpp): one header shared by every file of the game
#pragma once

#include <string>

struct Stats {
    int created = 0, played = 0, ticks = 0, fixedTicks = 0, lateTicks = 0, stopped = 0, destroyed = 0;
    int minions = 0, spawnedFromJson = 0, undeclaredTag = -1, crossScript = 0, driftSteps = 0;
    float lastDt = 0.f;
    float lastFixedDt = 0.f;
    std::string foundName;
    // 1.5 : references, accessor strings, physics, worlds, audio, panels
    int aimed = 0, contactsAdded = 0, contactsEnded = 0, rayHits = 0, worldLoad = -1000, oneShot = -1000, playAudio = -1000;
    int prefab = 0, missingPrefab = 0, panelDraws = 0, panelClicks = 0, forced = 0;
    float contactNormalY = 0.f, contactPenetration = -1.f, rayDistance = -1.f, rayNormalY = 0.f, targetDistance = -1.f;
    std::string lastNote, contactOther, rayActor;
    // 1.5 : random streams
    int randomFirst = 0, randomSame = -1, randomOther = -1;
    float sphereX = 0.f, sphereY = 0.f, sphereZ = 0.f;
    // The typed handles of the engine's components
    int typedRefused = 0, velocitySet = 0, shapeCount = 0, shapeCountAfterRemove = 0, hasListener = 0, hasListenerAfterRemove = -1, noListener = 0;
    float lightIntensity = 0.f, listenerDoppler = 0.f;
};

inline Stats& GetStats()
{
    static Stats stats;
    return stats;
}
