#include "gl_gpu_profiler.hpp"

#include "engine/renderer/rhi/backends/opengl/gl_utils.hpp"

namespace Shard::Engine::Rendering {

    GLGpuProfiler::~GLGpuProfiler()
    {
        for (Frame& frame : m_Frames)
            for (Scope& scope : frame.scopes)
                for (uint32_t q : scope.queries)
                    if (q)
                        glDeleteQueries(1, &q);
        for (uint32_t q : m_FreeQueries)
            glDeleteQueries(1, &q);
    }

    uint32_t GLGpuProfiler::AcquireQuery()
    {
        if (!m_FreeQueries.empty())
        {
            uint32_t q = m_FreeQueries.back();
            m_FreeQueries.pop_back();
            return q;
        }
        GLuint q = 0;
        glGenQueries(1, &q);
        return q;
    }

    void GLGpuProfiler::Collect(Frame& frame)
    {
        // Complete when every scope's end timestamp is available
        if (frame.used == 0)
        {
            frame.pending = false;
            return;
        }

        for (size_t i = 0; i < frame.used; i++)
        {
            GLint available = 0;
            glGetQueryObjectiv(frame.scopes[i].queries[1], GL_QUERY_RESULT_AVAILABLE, &available);
            if (!available)
                return;
        }

        std::vector<Timing> timings;
        timings.reserve(frame.used);
        for (size_t i = 0; i < frame.used; i++)
        {
            GLuint64 begin = 0, end = 0;
            glGetQueryObjectui64v(frame.scopes[i].queries[0], GL_QUERY_RESULT, &begin);
            glGetQueryObjectui64v(frame.scopes[i].queries[1], GL_QUERY_RESULT, &end);
            timings.push_back({frame.scopes[i].name, end >= begin ? float(double(end - begin) * 1e-6) : 0.0f});
        }
        PublishLatest(std::move(timings));
        frame.pending = false;
    }

    void GLGpuProfiler::BeginFrame()
    {
        if (!m_Enabled)
            return;

        m_Current = (m_Current + 1) % kFrames;
        Frame& frame = m_Frames[m_Current];

        // The frame this slot held is read now or never : the oldest of the ring, its results are long done
        if (frame.pending)
            Collect(frame);

        frame.used = 0;
        frame.pending = false;
        m_Open.clear();
        m_InFrame = true;
    }

    void GLGpuProfiler::EndFrame()
    {
        if (!m_InFrame)
            return;
        m_InFrame = false;
        m_Frames[m_Current].pending = m_Frames[m_Current].used > 0;

        // Read whatever finished since (newest first would skip frames : go oldest to newest)
        for (int i = 1; i < kFrames; i++)
        {
            Frame& frame = m_Frames[(m_Current + i) % kFrames];
            if (frame.pending)
                Collect(frame);
        }
    }

    void GLGpuProfiler::Begin(const char* name)
    {
        if (!m_InFrame)
            return;
        Frame& frame = m_Frames[m_Current];
        if (frame.used == frame.scopes.size())
        {
            frame.scopes.emplace_back();
            frame.scopes.back().queries[0] = AcquireQuery();
            frame.scopes.back().queries[1] = AcquireQuery();
        }
        Scope& scope = frame.scopes[frame.used];
        scope.name = name;
        glQueryCounter(scope.queries[0], GL_TIMESTAMP);
        m_Open.push_back(frame.used);
        frame.used++;
    }

    void GLGpuProfiler::End()
    {
        if (!m_InFrame || m_Open.empty())
            return;
        Frame& frame = m_Frames[m_Current];
        glQueryCounter(frame.scopes[m_Open.back()].queries[1], GL_TIMESTAMP);
        m_Open.pop_back();
    }
}
