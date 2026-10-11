#include "billboard_component.hpp"

#include <algorithm>

#include "engine/core/diagnostics/logger.hpp"
#include "engine/renderer/features/billboards/billboard_atlas.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/components/transform.hpp"
#include "engine/world/engine.hpp"

#include "billboard_component.reflection.hpp"

namespace Shard::Engine::Objects::Components {

    Billboard::Billboard(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {
        atlasFirstSprite = Rendering::BillboardAtlas::Get().GetBuiltin(Rendering::BuiltinSprite::Generic);
    }

    void Billboard::Init(Filesystem::Path texturePath, int spritesCount, float size, Rendering::BillboardFacingType facingType, bool ScreenSpaceSize,
                bool DepthTest, bool EditorOnly, bool Pickable, uint32_t sprite, COL_RGBA tint)
    {
        this->spritePath = texturePath.full;
        this->spritesCount = std::max(spritesCount, 1);
        this->size = size;
        this->facingType = facingType;
        this->ScreenSpaceSize = ScreenSpaceSize;
        this->DepthTest = DepthTest;
        this->EditorOnly = EditorOnly;
        this->Pickable = Pickable;
        this->sprite = sprite;
        this->tint = tint;

        ResolveSprite();
    }

    void Billboard::ResolveSprite()
    {
        Rendering::BillboardAtlas& atlas = Rendering::BillboardAtlas::Get();

        if (spritePath.empty())
        {
            atlasFirstSprite = atlas.GetBuiltin(Rendering::BuiltinSprite::Generic);
            return;
        }

        // A path of the project, unless it already is a full one
        Filesystem::Path file(spritePath);
        if (!file.Exists())
        {
            Core::IEngineContext* engine = GetEngineContext();
            if (engine && engine->GetFileManager())
                file = engine->GetFileManager()->GetProjectResRoot() / spritePath;
        }

        atlasFirstSprite = atlas.RegisterFile(file, static_cast<uint32_t>(std::max(spritesCount, 1)));
    }

    uint32_t Billboard::GetAtlasSprite() const
    {
        // A sheet of one cell, or the generic marker, has no other frame to pick
        const uint32_t frames = static_cast<uint32_t>(std::max(spritesCount, 1));
        return atlasFirstSprite + std::min(sprite, frames - 1);
    }

    glm::dvec3 Billboard::GetPosition() const
    {
        if (parent && parent->transform)
            return glm::dvec3(parent->transform->GetWorldPosition());
        return glm::dvec3(0.0);
    }

    void Billboard::UpdateSize(float new_size)
    {
        size = std::max(new_size, 0.0f);
    }

    void Billboard::OnFieldChanged(const FieldChangedEvent& event)
    {
        if (event.field->name == "spritePath" || event.field->name == "spritesCount")
            ResolveSprite();
    }

    std::shared_ptr<Component> Billboard::Clone() const
    {
        // The atlas sprite is a plain id : the copy shares the sheet
        return Object::Create<Billboard>(*this);
    }

}
