#pragma once

#include <string>

#include "engine/world/components/component.hpp"

#include "engine/assets/reflection/attributes.hpp"
#include "engine/assets/vfs/filesystem.hpp"

#include "engine/core/color.hpp"

namespace Shard::Engine::Rendering {

    // Scoped and outside the component namespace, "Camera" would hide the Camera component there
    enum class BillboardFacingType : uint8_t{
        Camera = 0, AxisY = 1, Fixed = 2
    };
}

namespace Shard::Engine::Objects::Components {

    // Camera-facing quad. Image is a sprite sheet of `spritesCount` equal cells in a row, `sprite` picks the cell. All sheets are packed in one atlas (BillboardAtlas), so all billboards of a world are one draw call.
    class CLASS() Billboard : public Component
    {
        public :

            Billboard(std::shared_ptr<Actor> parent, uint32_t local_id);

            void Init(Filesystem::Path texturePath, int spritesCount, float size = 48.0f, Rendering::BillboardFacingType facingType = Rendering::BillboardFacingType::Camera, bool ScreenSpaceSize = true,
                bool DepthTest = true, bool EditorOnly = false, bool Pickable = true, uint32_t sprite = 0, COL_RGBA tint = 1.0f);

            void UpdateSize(float new_size);

            void OnFieldChanged(const FieldChangedEvent& event) override;

            std::shared_ptr<Component> Clone() const override;

            glm::dvec3 GetPosition() const;

            uint32_t GetAtlasSprite() const;

            FIELD(Editable, range=0.1f|500.0f, tooltip=Pixels when Screen Space Size is on, otherwise meters)
            float size = 48.0f;

            FIELD(Editable, tooltip=How the quad is oriented relative to the camera)
            Rendering::BillboardFacingType facingType = Rendering::BillboardFacingType::Camera;

            FIELD(Editable, tooltip=Keep a constant size on the screen, whatever the distance)
            bool ScreenSpaceSize = true;

            FIELD(Editable, tooltip=Hidden by the geometry in front of it)
            bool DepthTest = true;

            FIELD(Editable, tooltip=Only drawn while the editor shows its helpers)
            bool EditorOnly = false;

            FIELD(Editable, tooltip=Clicking it in the editor viewport selects its actor)
            bool Pickable = true;

            FIELD(Editable, tooltip=Which cell of the sprite sheet to draw)
            uint32_t sprite = 0;

            FIELD(Editable, range=1|64, tooltip=How many cells the image holds, side by side)
            int spritesCount = 1;

            FIELD(Editable, tooltip=Image of the project, such as textures/icons.png. Empty uses the generic marker)
            std::string spritePath;

            FIELD(Editable, tooltip=Color multiplied with the sprite)
            COL_RGBA tint = 1.0f;

        private:

            void ResolveSprite();

            uint32_t atlasFirstSprite = 3;

            DECLARE_DESCRIPTOR(Billboard)
    };

}
