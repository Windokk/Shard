#include "renderdoc.hpp"

#include <memory>
#include <vector>

#include "renderdoc_app.h"

#include "engine/core/diagnostics/logger.hpp"
#include "engine/platform/lib_loader/dynlib.hpp"

namespace Shard::Engine::Rendering {

    namespace {

        using Core::Platform::DynLib;

        RENDERDOC_API_1_4_0* Api(void* api)
        {
            return static_cast<RENDERDOC_API_1_4_0*>(api);
        }
    }

    RenderDoc& RenderDoc::Get()
    {
        static RenderDoc instance;
        return instance;
    }

    bool RenderDoc::Attach(bool loadIfMissing, const std::string& hint)
    {
        if (m_Api)
            return true;

        auto library = std::make_unique<DynLib>();

        const std::string name = DynLib::FileName("renderdoc");

        // Started from RenderDoc : it is in the process already
        bool found = library->OpenIfLoaded(name);

        if (!found && loadIfMissing)
        {
            std::vector<std::string> candidates;
            if (!hint.empty())
                candidates.push_back(hint);
#if defined(_WIN32)
            candidates.push_back("C:/Program Files/RenderDoc/renderdoc.dll");
#else
            candidates.push_back("librenderdoc.so");
#endif
            candidates.push_back(name);

            for (const std::string& candidate : candidates)
            {
                if (library->Open(candidate))
                {
                    found = true;
                    break;
                }
            }
        }

        if (!found)
            return false;

        auto getApi = library->Symbol<pRENDERDOC_GetAPI>("RENDERDOC_GetAPI");
        if (!getApi)
        {
            DEBUG_WARNING("renderdoc is loaded but has no RENDERDOC_GetAPI.");
            return false;
        }

        void* api = nullptr;
        if (getApi(eRENDERDOC_API_Version_1_4_0, &api) != 1 || !api)
        {
            DEBUG_WARNING("RenderDoc could not give its in-application API (version 1.4.0).");
            return false;
        }

        m_Api = api;
        m_Library = library.release();

        // No overlay on top of the picture, the captures in a folder of their own
        Api(m_Api)->SetCaptureOptionU32(eRENDERDOC_Option_AllowVSync, 1);
        DEBUG_INFO("RenderDoc ", Version(), " is attached : `render.capture` takes a capture of the next frame.");
        return true;
    }

    std::string RenderDoc::Version() const
    {
        if (!m_Api)
            return "";

        int major = 0, minor = 0, patch = 0;
        Api(m_Api)->GetAPIVersion(&major, &minor, &patch);
        return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    }

    void RenderDoc::SetCapturePrefix(const std::string& prefix)
    {
        if (m_Api)
            Api(m_Api)->SetCaptureFilePathTemplate(prefix.c_str());
    }

    bool RenderDoc::TriggerCapture(uint32_t frames)
    {
        if (!m_Api)
            return false;

        if (frames <= 1)
            Api(m_Api)->TriggerCapture();
        else
            Api(m_Api)->TriggerMultiFrameCapture(frames);
        return true;
    }

    uint32_t RenderDoc::CaptureCount() const
    {
        return m_Api ? Api(m_Api)->GetNumCaptures() : 0;
    }

    std::string RenderDoc::GetCapture(uint32_t index) const
    {
        if (!m_Api)
            return "";

        uint32_t length = 0;
        if (!Api(m_Api)->GetCapture(index, nullptr, &length, nullptr) || length == 0)
            return "";

        std::string path(length, '\0');
        if (!Api(m_Api)->GetCapture(index, path.data(), &length, nullptr))
            return "";

        while (!path.empty() && path.back() == '\0')
            path.pop_back();
        return path;
    }

    bool RenderDoc::OpenReplayUI(const std::string& capture)
    {
        if (!m_Api)
            return false;

        return Api(m_Api)->LaunchReplayUI(1, capture.empty() ? nullptr : capture.c_str()) != 0;
    }

}
