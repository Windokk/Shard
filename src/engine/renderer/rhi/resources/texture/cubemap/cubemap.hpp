#pragma once

#include "engine/renderer/rhi/resources/texture/texture.hpp"

namespace Shard::Engine::Rendering {
    class Cubemap
    {
        public:

            virtual ~Cubemap() = default;

            virtual void Bind(uint32_t slot = 0) const = 0;

            uint32_t GetWidth() const { return m_Specifications.width; }
            uint32_t GetHeight() const { return m_Specifications.height; }

            const TextureSpecifications& GetSpecification() const { return m_Specifications; }

            virtual uint32_t GetHandle() const = 0;

            virtual bool IsValid() const = 0;

            static std::shared_ptr<Cubemap> Create(const TextureSpecifications& specs, std::array<unsigned char*, 6> data);

            static std::shared_ptr<Cubemap> Create(TextureSpecifications& specs, const std::array<Filesystem::Path, 6> imageFiles);

        protected:
            TextureSpecifications m_Specifications;
    };

    class CubemapArray
    {
        public:

            virtual ~CubemapArray() = default;

            virtual void Bind(uint32_t slot = 0) const = 0;

            uint32_t GetWidth() const { return m_Specifications.width; }
            uint32_t GetHeight() const { return m_Specifications.height; }
            
            virtual uint32_t GetCount() const = 0;

            const TextureSpecifications& GetSpecification() const { return m_Specifications; }

            virtual uint32_t GetHandle() const = 0;

            virtual bool IsValid() const = 0;
            
            static std::shared_ptr<CubemapArray> Create(const TextureSpecifications& specs, std::vector<std::array<unsigned char*, 6>> data);

            static std::shared_ptr<CubemapArray> Create(TextureSpecifications& specs, const std::vector<std::array<Filesystem::Path, 6>> imageFiles);

        protected:
            TextureSpecifications m_Specifications;
    };
}