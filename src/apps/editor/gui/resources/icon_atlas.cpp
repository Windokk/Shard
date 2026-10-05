#define STB_RECT_PACK_IMPLEMENTATION
#include <stb/stb_rect_pack.h>

#include "icon_atlas.hpp"

#include <stb/stb_image.h>

namespace Shard::Editor::GUI {
    
    bool IconAtlas::Build(const std::vector<std::string>& iconPaths, int atlasSize)
    {
        width = atlasSize;
        height = atlasSize;

        // Map paths to enum order
        std::vector<Engine::Filesystem::Type> iconTypes = {
            Engine::Filesystem::Type::T_IMAGE,
            Engine::Filesystem::Type::T_SOUND,
            Engine::Filesystem::Type::T_FONT,
            Engine::Filesystem::Type::T_SHADER,
            Engine::Filesystem::Type::T_COMPUTE_SHADER,
            Engine::Filesystem::Type::T_TEXT,
            Engine::Filesystem::Type::T_SCRIPT,
            Engine::Filesystem::Type::T_LEVEL,
            Engine::Filesystem::Type::T_MODEL,
            Engine::Filesystem::Type::T_MATERIAL,
            Engine::Filesystem::Type::T_CONFIG,
            Engine::Filesystem::Type::T_DIRECTORY
        };

        assert(iconPaths.size() == iconTypes.size() && "iconPaths must match number of enum types");

        std::vector<stbrp_rect> rects(iconPaths.size());
        std::vector<unsigned char*> images(iconPaths.size());
        int channels;

        // Load images
        for (size_t i = 0; i < iconPaths.size(); i++)
        {
            images[i] = stbi_load(iconPaths[i].c_str(),
                                &rects[i].w,
                                &rects[i].h,
                                &channels,
                                4);
            if (!images[i])
            {
                fprintf(stderr, "Failed to load icon: %s\n", iconPaths[i].c_str());
                return false;
            }
            rects[i].id = static_cast<int>(i);
        }

        // Pack rectangles
        stbrp_context ctx;
        std::vector<stbrp_node> nodes(atlasSize);
        stbrp_init_target(&ctx, atlasSize, atlasSize, nodes.data(), atlasSize);
        stbrp_pack_rects(&ctx, rects.data(), rects.size());

        // Allocate atlas pixels
        std::vector<unsigned char> atlasPixels(width * height * 4, 0);

        for (size_t i = 0; i < rects.size(); i++)
        {
            auto& rect = rects[i];
            if (!rect.was_packed) continue;

            unsigned char* src = images[rect.id];

            for (int y = 0; y < rect.h; y++)
            {
                unsigned char* dstRow = &atlasPixels[((rect.y + y) * width + rect.x) * 4];
                unsigned char* srcRow = &src[(y * rect.w) * 4];
                memcpy(dstRow, srcRow, rect.w * 4);
            }

            float u0 = rect.x / (float)width;
            float v0 = rect.y / (float)height;
            float u1 = (rect.x + rect.w) / (float)width;
            float v1 = (rect.y + rect.h) / (float)height;

            // Assign region using the enum
            regions[iconTypes[i]] = { {u0, v0}, {u1, v1} };
        }

        // Free images
        for (auto img : images)
            stbi_image_free(img);

        // Create engine texture

        Engine::Rendering::TextureSpecifications specs;

        specs.wrapS = Engine::Rendering::TextureWrap::ClampEdge;
        specs.wrapT = Engine::Rendering::TextureWrap::ClampEdge;
        specs.minFilter = Engine::Rendering::TextureFilter::Linear;
        specs.magFilter = Engine::Rendering::TextureFilter::Linear;
        specs.width = width;
        specs.height = height;

        texture = Engine::Rendering::Texture2D::Create(specs, atlasPixels.data());

        return true;
    }
    
    const AtlasRegion &IconAtlas::GetRegion(const Engine::Filesystem::Type type) const
    {
        auto it = regions.find(type);
        assert(it != regions.end() && "Requested IconType not found in atlas!");
        return it->second;
    }
}