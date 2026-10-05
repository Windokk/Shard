#pragma once

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/renderer/material/shader.hpp"

#include "apps/editor/gui/panels/asset_editor.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>
#include <memory>

namespace Shard::Editor::GUI{

    enum class MatParamKind{
        Bool,
        Int,
        Float,
        Vec2,
        Vec3,
        Vec4,
        Mat4,
        Texture
    };

    // One editable material uniform/sampler, mirrored 1:1 with a `.mat` file's "uniforms" entries
    // (see Engine::Serialization::DeserializeMaterial). Mat4 is parsed/round-tripped but not editable
    // here - 16 loose floats isn't "quick", and materials rarely expose a raw matrix parameter.
    struct MatParam{
        std::string name;
        MatParamKind kind;

        bool boolValue = false;
        int intValue = 0;
        float floatValue = 0.0f;
        glm::vec2 vec2Value{0.0f};
        glm::vec3 vec3Value{0.0f};
        glm::vec4 vec4Value{0.0f};
        glm::mat4 mat4Value{1.0f};
        std::string texturePath;
    };

    // Quick, double-click-from-the-asset-browser material inspector (Unity-style "click a .mat, edit
    // its exposed shader parameters"). Reads/writes the `.mat` JSON directly rather than going through
    // the runtime Material object, since Material has no API to enumerate its own parameters - the
    // shader's reflected uniform/sampler list (Shader::GetActiveUniformsMap/GetActiveSamplersMap) is
    // what drives the "Add Parameter" picker instead.
    class MaterialEditorPanel : public IAssetEditor
    {
        public:
            void Open(const Engine::Filesystem::Path& path) override;
            void Draw() override;
            bool IsOpen() const override { return isOpen; }

        private:
            void Load();
            void Save();
            bool DrawParam(MatParam& param);
            void DrawAddParameterPopup();
            void ApplyParamLive(const MatParam& param);

            bool isOpen = false;
            bool focusRequested = false;
            bool dirty = false;

            Engine::Filesystem::Path path;
            std::string shaderPath;
            bool receivesShadows = true;
            int renderModeIndex = 0; // 0 = opaque, 1 = masked, 2 = translucent

            std::shared_ptr<Engine::Rendering::Shader> shader;
            std::vector<MatParam> params;
    };
}
