#pragma once

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"

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
            void ApplyDoubleSidedLive();

            bool isOpen = false;
            bool focusRequested = false;
            bool dirty = false;

            Engine::Filesystem::Path path;
            std::string shaderPath;
            bool receivesShadows = true;
            bool doubleSided = false;
            int renderModeIndex = 0;

            std::shared_ptr<Engine::Rendering::Shader> shader;
            std::vector<MatParam> params;
    };
}
