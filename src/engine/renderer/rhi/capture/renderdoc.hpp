#pragma once

#include <cstdint>
#include <string>

namespace Shard::Engine::Rendering {

    // RenderDoc must be in the process before the graphics API initialises: start from RenderDoc, or Attach(true) before device creation (SHARD_RENDERDOC=1, a renderdoc.dll path, or render.renderdoc).
    class RenderDoc
    {
        public:

            static RenderDoc& Get();

            bool Attach(bool loadIfMissing, const std::string& hint = "");

            bool IsAvailable() const { return m_Api != nullptr; }
            std::string Version() const;

            void SetCapturePrefix(const std::string& prefix);

            bool TriggerCapture(uint32_t frames = 1);

            uint32_t CaptureCount() const;
            std::string GetCapture(uint32_t index) const;

            bool OpenReplayUI(const std::string& capture = "");

        private:

            RenderDoc() = default;

            void* m_Api = nullptr;
            void* m_Library = nullptr;
    };

}
