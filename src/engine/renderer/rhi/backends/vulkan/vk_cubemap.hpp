#pragma once

#include <array>
#include <vector>

#include "engine/renderer/rhi/resources/texture/cubemap/cubemap.hpp"

#include "vk_image.hpp"

namespace Shard::Engine::Rendering {

    class VKCubemap final : public Cubemap
    {
        public:

            /// `faces` : the pixels of each face (+X, -X, +Y, -Y, +Z, -Z), null for a face nothing is written to
            VKCubemap(const TextureSpecifications& specs, std::array<unsigned char*, 6> faces);
            ~VKCubemap() override = default;

            void Bind(uint32_t slot = 0) const override;
            uint32_t GetHandle() const override { return m_Resource.GetId(); }
            bool IsValid() const override { return m_Resource.image.IsValid(); }

            VK::TextureResource& Resource() { return m_Resource; }

        private:

            mutable VK::TextureResource m_Resource;
    };

    class VKCubemapArray final : public CubemapArray
    {
        public:

            VKCubemapArray(const TextureSpecifications& specs, std::vector<std::array<unsigned char*, 6>> data);
            ~VKCubemapArray() override = default;

            void Bind(uint32_t slot = 0) const override;
            uint32_t GetCount() const override { return m_Count; }
            uint32_t GetHandle() const override { return m_Resource.GetId(); }
            bool IsValid() const override { return m_Resource.image.IsValid(); }

            VK::TextureResource& Resource() { return m_Resource; }

        private:

            mutable VK::TextureResource m_Resource;
            uint32_t m_Count = 0;
    };

    namespace VK {

        /// Creates the image of a cube map (or of an array of them) and fills the faces that have pixels
        bool CreateCubeImage(Image& image, const TextureSpecifications& specs, uint32_t cubes, bool array, const std::vector<std::array<unsigned char*, 6>>& data, const char* name);
    }

}
