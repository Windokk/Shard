#include "engine/renderer/rhi/render_snapshot.hpp"

#include "engine/renderer/rhi/render_thread.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"

namespace Shard::Engine::Rendering {

    OverlayGeometry::~OverlayGeometry()
    {
        if (!gpu[0] && !gpu[1] && !gpu[2])
            return;
        const uint32_t ids[3] = { gpu[0], gpu[1], gpu[2] };
        // The buffers belong to the graphics thread
        RenderThread::Get().Post([ids] {
            if (RendererAPI* api = RendererAPI::Current())
                api->ReleaseOverlayGeometry(ids);
        });
    }

    RenderView BuildView(const CameraProxy& camera)
    {
        RenderView view;
        view.view = camera.view;
        view.projection = camera.projection;
        view.position = camera.position;
        view.orthographic = camera.orthographic;
        return view;
    }

    bool BuildMainView(const RenderSnapshot& snapshot, RenderView& out)
    {
        if (!snapshot.camera.valid)
            return false;

        out = BuildView(snapshot.camera);
        return true;
    }

    RenderSnapshot& RenderSnapshotBuffer::BeginWrite()
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        const int writeIndex = 1 - m_Current;

        // The renderer may still be reading what was current before the last Publish()
        m_Released.wait(lock, [&] { return m_Readers[writeIndex] == 0; });

        m_Writing = true;
        return m_Buffers[writeIndex];
    }

    void RenderSnapshotBuffer::Publish()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_Writing)
            return;

        m_Current = 1 - m_Current;
        m_Writing = false;
        m_Published++;
    }

    RenderSnapshotBuffer::ReadLease RenderSnapshotBuffer::Acquire()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Readers[m_Current]++;
        return ReadLease(this, m_Current);
    }

    void RenderSnapshotBuffer::ReleaseRead(int index)
    {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Readers[index]--;
        }
        m_Released.notify_all();
    }
}
