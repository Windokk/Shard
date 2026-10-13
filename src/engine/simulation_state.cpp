#include "engine/simulation_state.hpp"

#include <algorithm>
#include <vector>

#include "engine/assets/serialization/reflection/reflection_serializer.hpp"
#include "engine/core/determinism/state_hash.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/physics/physics_world_data.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"

namespace Shard::Engine::Core {

    namespace {
        // The ids of a hash table, in ascending order : the order the table iterates in is not part of the state
        template <typename Map>
        std::vector<int> SortedIds(const Map& map)
        {
            std::vector<int> ids;
            ids.reserve(map.size());
            for (const auto& entry : map)
                ids.push_back(entry.first);
            std::sort(ids.begin(), ids.end());
            return ids;
        }
    }

    std::string SimulationStateHash::Describe() const
    {
        return "state " + Determinism::HashToString(total) + " (transforms " + Determinism::HashToString(transforms) +
               ", physics " + Determinism::HashToString(physics) + ", scripts " + Determinism::HashToString(scripts) +
               ") after " + std::to_string(frame) + " frames, " + std::to_string(physicsSteps) + " physics steps";
    }

    SimulationStateHash HashSimulationState(IEngineContext& engine)
    {
        using Determinism::StateHasher;

        SimulationStateHash result;
        result.frame = engine.GetFrameNumber();

        Worlds::WorldManager* worlds = engine.GetWorldManager();
        const int worldCount = worlds ? worlds->GetLoadedWorldCount() : 0;

        StateHasher transformHash, physicsHash, scriptHash;

        for (int w = 0; w < worldCount; ++w)
        {
            Worlds::World* world = worlds->GetWorldAt(w);
            if (!world)
                continue;

            transformHash.I32(w).String(world->GetName());
            physicsHash.I32(w);
            scriptHash.I32(w);

            for (int id : SortedIds(world->transforms))
            {
                const auto& transform = world->transforms.at(id);
                if (!transform)
                    continue;
                transformHash.I32(id).Bool(transform->Active())
                    .Vec3(transform->GetPosition()).Quat(transform->GetRotationQuat()).Vec3(transform->GetScale());
            }

            const auto& bodies = world->Ext<Physics::PhysicsWorldData>().bodies;
            for (int id : SortedIds(bodies))
            {
                const auto& body = bodies.at(id);
                if (!body)
                    continue;
                physicsHash.I32(id).Bool(body->Active()).Bool(!body->GetBodyID().IsInvalid());
                if (!body->GetBodyID().IsInvalid())
                    physicsHash.Vec3(body->GetPosition()).Quat(body->GetRotation())
                        .Vec3(body->GetLinearVelocity()).Vec3(body->GetAngularVelocity());
            }

            // The game's own state : every reflected field of the scripts, the read-only ones too (those are exactly the
            // runtime state : what the script computed, not what the editor set), in the text form the fields are saved with
            // (the JSON library prints a float with the shortest text that reads back as the same float, on every platform)
            const Serialization::ReflectionContext context{engine.GetAssetIDManager()};
            for (int id : SortedIds(world->scripts))
            {
                const auto& script = world->scripts.at(id);
                if (!script)
                    continue;
                scriptHash.I32(id).Bool(script->Active());
                const ClassDescriptor* descriptor = script->GetDescriptor();
                if (!descriptor)
                {
                    scriptHash.String(script->Serialize().dump());
                    continue;
                }
                scriptHash.String(descriptor->name);
                for (const FieldInfo* field : descriptor->fields)
                {
                    nlohmann::ordered_json value;
                    if (Serialization::WriteValue(*field, field->type, reinterpret_cast<const uint8_t*>(script.get()) + field->offset, value, context))
                        scriptHash.String(field->name).String(value.dump());
                }
            }
        }

        if (Physics::PhysicsManager* physics = engine.GetPhysicsManager())
            result.physicsSteps = physics->GetStepCount();

        result.transforms = transformHash.Digest();
        result.physics = physicsHash.Digest();
        result.scripts = scriptHash.Digest();

        StateHasher total;
        total.U64(result.physicsSteps).U64(result.transforms).U64(result.physics).U64(result.scripts);
        result.total = total.Digest();
        return result;
    }
}
