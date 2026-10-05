#pragma once

#include "cubemap.hpp"

namespace Shard::Engine::Rendering{

    

    class EnvironmentMap
    {
        public:

            static std::shared_ptr<EnvironmentMap> Create(TextureSpecifications& specs, const std::vector<Filesystem::Path> imageFiles);

            static std::shared_ptr<EnvironmentMap> Create(TextureSpecifications& specs, const Filesystem::Path hdrFile);

            const TextureSpecifications& GetInfos() const { return m_Infos; }

            std::shared_ptr<Cubemap> GetCubemap() const { return m_Cubemap; }
            std::shared_ptr<Cubemap> GetIrradiance() const { return m_IrradianceMap; }
            std::shared_ptr<Cubemap> GetPrefilter() const { return m_PrefilterMap; }
            std::shared_ptr<Texture2D> GetBRDFLUT() const { return m_BrdfLUT; }

            void SetAssetID(Filesystem::AssetID assetID)
            {
                m_AssetID = assetID;
            }

            Filesystem::AssetID GetAssetID() const
            {
                return m_AssetID;
            }

        protected:

            Filesystem::AssetID m_AssetID;

            TextureSpecifications m_Infos;

            std::shared_ptr<Cubemap> m_Cubemap;
            std::shared_ptr<Cubemap> m_IrradianceMap;
            std::shared_ptr<Cubemap> m_PrefilterMap;
            std::shared_ptr<Texture2D> m_BrdfLUT;
    };

    class EnvironmentMapGenerator
    {
        public:

            virtual std::shared_ptr<EnvironmentMap> GenerateFromFiles(TextureSpecifications& specs, const std::vector<Filesystem::Path> imageFiles) = 0;
            virtual std::shared_ptr<EnvironmentMap> GenerateFromHDR(TextureSpecifications& specs, const Filesystem::Path hdrFile) = 0;
    };

}