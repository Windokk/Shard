#pragma once

#include <cstdint>
#include <string>

namespace Shard::Engine::Rendering {

    /// @brief RenderDoc, from inside the application : captures a frame of the GPU commands (what the passes drew, the state, the resources) on
    /// either graphics backend, to be opened in the RenderDoc UI.
    ///
    /// RenderDoc has to be in the process before the graphics API is initialised : either the application was started from RenderDoc (it is
    /// there already), or Attach(true) loads it before the device is created (`SHARD_RENDERDOC=1`, or the path of renderdoc.dll, or the
    /// `render.renderdoc` variable). The passes are named in the capture (see RendererAPI::PushDebugGroup).
    class RenderDoc
    {
        public:

            static RenderDoc& Get();

            /// Finds RenderDoc in the process, or (with `loadIfMissing`) loads it : `hint` is the path of renderdoc.dll, empty for the default install
            /// folder. True if the API is there.
            bool Attach(bool loadIfMissing, const std::string& hint = "");

            bool IsAvailable() const { return m_Api != nullptr; }
            std::string Version() const;

            /// The captures are written as `<prefix>_frame<N>.rdc`
            void SetCapturePrefix(const std::string& prefix);

            /// Captures the next `frames` frames that are presented
            bool TriggerCapture(uint32_t frames = 1);

            uint32_t CaptureCount() const;
            std::string GetCapture(uint32_t index) const;

            /// Shows the RenderDoc UI (with a capture open if a path is given)
            bool OpenReplayUI(const std::string& capture = "");

        private:

            RenderDoc() = default;

            void* m_Api = nullptr;          // RENDERDOC_API_1_4_0
            void* m_Library = nullptr;      // the DynLib, when it was loaded here
    };

}
