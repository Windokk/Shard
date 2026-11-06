#pragma once

#include <variant>
#include <stdexcept>
#include <memory>
#include <optional>

#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Rendering {

    class Shader;
    class Pipeline;
    class CommandBuffer;

    enum class Opacity{
        Opaque,
        Masked,
        Translucent
    };

    using NumericValue = std::variant<bool, float, int, glm::vec2, glm::vec3, glm::vec4, glm::mat4>;

    class Material {
        public:

            virtual ~Material() = default;

            virtual void SetScalarParameter(
                const std::string& name,
                const NumericValue& value) = 0;

            virtual void SetTextureParameter(
                const std::string& name,
                uint64_t texture) = 0;

            virtual std::optional<NumericValue>
            GetScalarParameter(const std::string& name) = 0;

            virtual uint64_t GetTextureParameter(const std::string& name) const = 0;

            virtual uint32_t GetTexturesCount() const = 0;

            std::shared_ptr<Shader> GetShader() const { return m_Shader; }
            std::shared_ptr<Pipeline> GetPipeline() const { return m_Pipeline; }

            void SetAssetID(Filesystem::AssetID assetID) {
                m_AssetID = assetID;
            }

            Filesystem::AssetID GetAssetID() {
                return m_AssetID;
            }

            static void PreloadDefaultTextures();

            static std::shared_ptr<Material> Create(std::shared_ptr<Shader> shader, std::shared_ptr<Pipeline> pipeline, bool receivesShadows = true, Opacity opacity = Opacity::Opaque);

            bool GetRecieveShadows() const { return m_ReceivesShadows; }

            Opacity GetOpacity() const { return m_Opacity; }

            bool IsDoubleSided() const { return m_DoubleSided; }
            void SetDoubleSided(bool doubleSided) { m_DoubleSided = doubleSided; }

            // Hot reload: copy the content of `other` but keep this object, so the asset id and pointers to it stay valid. Graphics thread.
            void AssignContent(const Material& other)
            {
                m_Shader = other.m_Shader;
                m_Pipeline = other.m_Pipeline;
                m_ReceivesShadows = other.m_ReceivesShadows;
                m_Opacity = other.m_Opacity;
                m_DoubleSided = other.m_DoubleSided;
                m_ScalarParameters = other.m_ScalarParameters;
                m_SamplersParameters = other.m_SamplersParameters;
            }

            void SetPipeline(std::shared_ptr<Pipeline> pipeline){
                m_Pipeline = pipeline;
            }

        protected:

            std::shared_ptr<Shader> m_Shader;
            std::shared_ptr<Pipeline> m_Pipeline;
            bool m_ReceivesShadows = false;
            Opacity m_Opacity;
            bool m_DoubleSided = false;
            std::unordered_map<std::string, NumericValue> m_ScalarParameters;
            std::unordered_map<std::string, uint32_t> m_SamplersParameters;
            Filesystem::AssetID m_AssetID;
    };

}