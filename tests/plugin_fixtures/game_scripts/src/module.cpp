// The plugin itself : one line. The scripts, components and systems of the other files register themselves.
#include <shard/game.hpp>

#include "stats.hpp"

SHARD_GAME_MODULE()

#if defined(_WIN32)
#   define TEST_EXPORT extern "C" __declspec(dllexport)
#else
#   define TEST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

TEST_EXPORT int game_stat(const char* name)
{
    const Stats& s = GetStats();
    const std::string n = name;
    if (n == "created") return s.created;
    if (n == "played") return s.played;
    if (n == "ticks") return s.ticks;
    if (n == "fixedTicks") return s.fixedTicks;
    if (n == "lateTicks") return s.lateTicks;
    if (n == "stopped") return s.stopped;
    if (n == "destroyed") return s.destroyed;
    if (n == "minions") return s.minions;
    if (n == "spawnedFromJson") return s.spawnedFromJson;
    if (n == "undeclaredTag") return s.undeclaredTag;
    if (n == "crossScript") return s.crossScript;
    if (n == "driftSteps") return s.driftSteps;
    if (n == "aimed") return s.aimed;
    if (n == "contactsAdded") return s.contactsAdded;
    if (n == "contactsEnded") return s.contactsEnded;
    if (n == "rayHits") return s.rayHits;
    if (n == "worldLoad") return s.worldLoad;
    if (n == "oneShot") return s.oneShot;
    if (n == "playAudio") return s.playAudio;
    if (n == "prefab") return s.prefab;
    if (n == "missingPrefab") return s.missingPrefab;
    if (n == "panelDraws") return s.panelDraws;
    if (n == "panelClicks") return s.panelClicks;
    if (n == "forced") return s.forced;
    if (n == "randomFirst") return s.randomFirst;
    if (n == "randomSame") return s.randomSame;
    if (n == "randomOther") return s.randomOther;
    if (n == "typedRefused") return s.typedRefused;
    if (n == "velocitySet") return s.velocitySet;
    if (n == "shapeCount") return s.shapeCount;
    if (n == "shapeCountAfterRemove") return s.shapeCountAfterRemove;
    if (n == "hasListener") return s.hasListener;
    if (n == "hasListenerAfterRemove") return s.hasListenerAfterRemove;
    if (n == "noListener") return s.noListener;
    if (n == "menuRuns") return s.menuRuns;
    if (n == "toolbarRuns") return s.toolbarRuns;
    if (n == "inspectorDraws") return s.inspectorDraws;
    if (n == "gizmoDraws") return s.gizmoDraws;
    if (n == "selectionEvents") return s.selectionEvents;
    if (n == "selectionSize") return s.selectionSize;
    if (n == "widgetDraws") return s.widgetDraws;
    if (n == "tabItems") return s.tabItems;
    if (n == "tableCells") return s.tableCells;
    if (n == "pickedIndex") return s.pickedIndex;
    return -1000;
}
TEST_EXPORT float game_dt() { return GetStats().lastDt; }
TEST_EXPORT float game_fixed_dt() { return GetStats().lastFixedDt; }
TEST_EXPORT const char* game_found_name() { return GetStats().foundName.c_str(); }
TEST_EXPORT float game_float(const char* name)
{
    const Stats& s = GetStats();
    const std::string n = name;
    if (n == "contactNormalY") return s.contactNormalY;
    if (n == "contactPenetration") return s.contactPenetration;
    if (n == "rayDistance") return s.rayDistance;
    if (n == "rayNormalY") return s.rayNormalY;
    if (n == "targetDistance") return s.targetDistance;
    if (n == "sphereX") return s.sphereX;
    if (n == "sphereY") return s.sphereY;
    if (n == "sphereZ") return s.sphereZ;
    if (n == "lightIntensity") return s.lightIntensity;
    if (n == "listenerDoppler") return s.listenerDoppler;
    if (n == "contentWidth") return s.contentWidth;
    return -1000.f;
}
TEST_EXPORT const char* game_text(const char* name)
{
    const Stats& s = GetStats();
    const std::string n = name;
    if (n == "lastNote") return s.lastNote.c_str();
    if (n == "inspected") return s.inspected.c_str();
    if (n == "gizmoActor") return s.gizmoActor.c_str();
    if (n == "contactOther") return s.contactOther.c_str();
    if (n == "rayActor") return s.rayActor.c_str();
    return "";
}
TEST_EXPORT void game_reset() { GetStats() = Stats(); }
// A buffer the test of a sandboxed build writes the names it passes to game_stat in (it cannot hand the module a pointer of its own)
TEST_EXPORT char* game_scratch() { static char buffer[128]; return buffer; }
