#pragma once

#include <unordered_map>

#include "engine/renderer/rhi/material/material.hpp"

namespace Shard::Engine::Rendering {

    /// The parameters of a material : the values of the uniforms its shader has and the textures of its samplers. Bound by number, like OpenGL's
    /// materials (see GLMaterial) : the scalars are written into the shader, the textures into the texture units the samplers read.
    class VKMaterial final : public Material
    {
        public:

            VKMaterial(std::shared_ptr<Shader> shader, std::shared_ptr<Pipeline> pipeline, bool receivesShadows, Opacity opacity);

            void SetScalarParameter(const std::string& name, const NumericValue& value) override;
            void SetTextureParameter(const std::string& name, uint64_t texture) override;
            std::optional<NumericValue> GetScalarParameter(const std::string& name) override;
            uint64_t GetTextureParameter(const std::string& name) const override;
            uint32_t GetTexturesCount() const override { return static_cast<uint32_t>(m_SamplersParameters.size()); }

            /// `shader` : the program to bind the material to (a permutation of its own, see RenderPass::shaderVariant), the material's shader if null
            void Bind(std::shared_ptr<Shader> shader = nullptr);

            /// Loads the textures bound to the slots a material leaves empty. Call once, on the thread that uses the resources manager.
            static void PreloadDefaultTextures();

        private:

            uint32_t GetDefaultTexture(const std::string& samplerName);
    };

}
