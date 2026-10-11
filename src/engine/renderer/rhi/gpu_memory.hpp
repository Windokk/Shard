#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace Shard::Engine::Rendering {

    /// @brief What the engine asked the GPU to hold, by kind : the resources add their size when they allocate and take it
    /// back when they are destroyed. It is the engine's own bookkeeping (driver padding and the like are not seen), which is
    /// what tells where the video memory goes - the OS counter only gives one total for the process.
    namespace GpuMemory {

        enum class Kind : uint8_t { Texture = 0, RenderTarget, Mesh, Buffer, COUNT };

        inline const char* Name(Kind kind)
        {
            switch (kind)
            {
                case Kind::Texture:      return "textures";
                case Kind::RenderTarget: return "render targets";
                case Kind::Mesh:         return "meshes";
                case Kind::Buffer:       return "buffers";
                default:                 return "?";
            }
        }

        inline std::array<std::atomic<int64_t>, size_t(Kind::COUNT)>& Counters()
        {
            static std::array<std::atomic<int64_t>, size_t(Kind::COUNT)> counters{};
            return counters;
        }

        inline void Add(Kind kind, int64_t bytes) { Counters()[size_t(kind)].fetch_add(bytes, std::memory_order_relaxed); }
        inline uint64_t Bytes(Kind kind)
        {
            const int64_t value = Counters()[size_t(kind)].load(std::memory_order_relaxed);
            return value > 0 ? uint64_t(value) : 0;
        }

        inline uint64_t Total()
        {
            uint64_t total = 0;
            for (size_t i = 0; i < size_t(Kind::COUNT); i++)
                total += Bytes(Kind(i));
            return total;
        }

        inline std::string Report()
        {
            std::string text;
            for (size_t i = 0; i < size_t(Kind::COUNT); i++)
            {
                if (i)
                    text += ", ";
                text += std::string(Name(Kind(i))) + " " + std::to_string(Bytes(Kind(i)) / (1024 * 1024)) + " MB";
            }
            return text + " (total " + std::to_string(Total() / (1024 * 1024)) + " MB)";
        }
    }
}
