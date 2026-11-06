#pragma once

#include <unordered_map>

#include "engine/renderer/rhi/material/material.hpp"

namespace Shard::Engine::Rendering {

    class VKMaterial final : public Material
    {
        public:

            VKMaterial(std::shared_ptr<Shader> shader, std::shared_ptr<Pipeline> pipeline, bool receivesShadows, Opacity opacity);

            void SetScalarParameter(const std::string& name, const NumericValue& value) override;
            void SetTextureParameter(const std::string& name, uint64_t texture) override;
            std::optional<NumericValue> GetScalarParameter(const std::string& name) override;
            uint64_t GetTextureParameter(const std::string& name) const override;
            uint32_t GetTexturesCount() const override { return static_cast<uint32_t>(m_SamplersParameters.size()); }

            void Bind(std::shared_ptr<Shader> shader = nullptr);

            static void PreloadDefaultTextures();

        private:

            uint32_t GetDefaultTexture(const std::string& samplerName);
    };

}
