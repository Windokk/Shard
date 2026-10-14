#pragma once

#include <string>

#include "engine/world/components/component.hpp"

#include "engine/assets/reflection/attributes.hpp"
#include "engine/assets/vfs/filesystem.hpp"

#include "engine/core/color.hpp"

namespace Shard::Engine::Rendering {

    /// How a billboard turns. Scoped, and out of the component namespace : "Camera" would hide the Camera component there.
    enum class BillboardFacingType : uint8_t{
        Camera = 0, AxisY = 1, Fixed = 2
    };
}

namespace Shard::Engine::Objects::Components {

    /// @brief A quad drawn at the actor's position that faces the camera : a sprite, a marker, a particle-like card...
    /// The image is a sprite sheet of `spritesCount` equal cells in a row ; `sprite` picks the cell. The renderer packs every sheet in
    /// one atlas (see BillboardAtlas), so all the billboards of a world are a single draw call.
    /// @note The engine's own types (lights, cameras, probe volumes) have their editor icon without carrying this component : see
    /// the billboard providers of the RenderExtractor, which is where a future type (bones, IK rigs...) adds its handle.
    class CLASS() Billboard : public Component
    {
        public :

            Billboard(std::shared_ptr<Actor> parent, uint32_t local_id);

            /// Sets the whole component at once
            /// @param texturePath An image of the project (a sprite sheet of `spritesCount` cells in a row)
            void Init(Filesystem::Path texturePath, int spritesCount, float size = 48.0f, Rendering::BillboardFacingType facingType = Rendering::BillboardFacingType::Camera, bool ScreenSpaceSize = true,
                bool DepthTest = true, bool EditorOnly = false, bool Pickable = true, uint32_t sprite = 0, COL_RGBA tint = 1.0f);

            void UpdateSize(float new_size);

            void OnFieldChanged(const FieldChangedEvent& event) override;

            std::shared_ptr<Component> Clone() const override;

            /// Where the quad is drawn : its actor's position
            glm::dvec3 GetPosition() const;

            /// The id of this billboard's sprite in the BillboardAtlas : the first cell of the sheet plus `sprite`
            uint32_t GetAtlasSprite() const;

            /// Pixels when ScreenSpaceSize, else meters
            FIELD(Editable, range=0.1f|500.0f, tooltip=Pixels when Screen Space Size is on, otherwise meters)
            float size = 48.0f;

            FIELD(Editable, tooltip=How the quad is oriented relative to the camera)
            Rendering::BillboardFacingType facingType = Rendering::BillboardFacingType::Camera;

            /// A constant size on the screen, whatever the distance
            FIELD(Editable, tooltip=Keep a constant size on the screen, whatever the distance)
            bool ScreenSpaceSize = true;

            /// Hidden by the geometry in front of it
            FIELD(Editable, tooltip=Hidden by the geometry in front of it)
            bool DepthTest = true;

            /// Only drawn while the editor shows its helpers
            FIELD(Editable, tooltip=Only drawn while the editor shows its helpers)
            bool EditorOnly = false;

            /// The editor selects its actor when it is clicked
            FIELD(Editable, tooltip=Clicking it in the editor viewport selects its actor)
            bool Pickable = true;

            /// Which cell of the sheet
            FIELD(Editable, tooltip=Which cell of the sprite sheet to draw)
            uint32_t sprite = 0;

            /// How many cells the image holds, side by side
            FIELD(Editable, range=1|64, tooltip=How many cells the image holds, side by side)
            int spritesCount = 1;

            /// An image of the project ("textures/icons.png"). Empty : the generic marker.
            FIELD(Editable, tooltip=Image of the project, such as textures/icons.png. Empty uses the generic marker)
            std::string spritePath;

            FIELD(Editable, tooltip=Color multiplied with the sprite)
            COL_RGBA tint = 1.0f;

        private:

            /// Registers the sheet in the atlas (decodes the image here, on the main thread, the atlas texture is made by the renderer)
            void ResolveSprite();

            uint32_t atlasFirstSprite = 3;      // BuiltinSprite::Generic

            DECLARE_DESCRIPTOR(Billboard)
    };

}
