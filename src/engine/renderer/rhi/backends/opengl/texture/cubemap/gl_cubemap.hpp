#pragma once

#include "engine/renderer/rhi/resources/texture/cubemap/cubemap.hpp"

namespace Shard::Engine::Rendering{

    class GLCubemap : public Cubemap{
        public:

            GLCubemap(const TextureSpecifications& specs, std::array<unsigned char*, 6> faces);

            void Bind(uint32_t slot = 0) const override;

            bool IsValid() const override;

            ~GLCubemap() override;

            uint32_t GetHandle() const override { return m_ID; }

        private:
            uint32_t m_ID;
    };

    class GLCubemapArray : public CubemapArray{
        public:

            GLCubemapArray(const TextureSpecifications& specs, std::vector<std::array<unsigned char*, 6>> data);

            void Bind(uint32_t slot = 0) const override;
            
            bool IsValid() const override;

            uint32_t GetCount() const override;

            ~GLCubemapArray() override;

            uint32_t GetHandle() const override { return m_ID; }

        private:
            uint32_t m_ID;

            uint32_t m_CubemapCount;
    };
}