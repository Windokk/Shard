#include "vk_profiler.hpp"

namespace Shard::Engine::Rendering {

    using namespace VK;

    VKGpuProfiler::VKGpuProfiler()
    {
        Context& context = Context::Get();

        for (Frame& frame : m_Frames)
        {
            VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            info.queryCount = kMaxScopes * 2;
            VK_CHECK(vkCreateQueryPool(context.GetDevice(), &info, nullptr, &frame.pool));
            frame.names.resize(kMaxScopes);
            vkResetQueryPool(context.GetDevice(), frame.pool, 0, kMaxScopes * 2);
        }
    }

    VKGpuProfiler::~VKGpuProfiler()
    {
        if (!Context::Exists())
            return;

        Context::Get().WaitIdle();
        for (Frame& frame : m_Frames)
        {
            if (frame.pool)
                vkDestroyQueryPool(Context::Get().GetDevice(), frame.pool, nullptr);
        }
    }

    void VKGpuProfiler::Collect(Frame& frame)
    {
        if (frame.used == 0)
        {
            frame.pending = false;
            return;
        }

        Context& context = Context::Get();
        std::vector<uint64_t> results(size_t(frame.used) * 2 * 2);      // a value and its availability for each query

        const VkResult result = vkGetQueryPoolResults(context.GetDevice(), frame.pool, 0, frame.used * 2, results.size() * sizeof(uint64_t), results.data(),
                                                      2 * sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (result == VK_NOT_READY)
        {
            // Everything has to be there
            for (uint32_t i = 0; i < frame.used * 2; i++)
            {
                if (results[i * 2 + 1] == 0)
                    return;
            }
        }
        else if (result != VK_SUCCESS)
            return;

        const double period = context.GetCaps().timestampPeriodNs;
        std::vector<Timing> timings;
        timings.reserve(frame.used);
        for (uint32_t i = 0; i < frame.used; i++)
        {
            const uint64_t begin = results[(i * 2) * 2];
            const uint64_t end = results[(i * 2 + 1) * 2];
            timings.push_back({frame.names[i], end >= begin ? static_cast<float>(double(end - begin) * period * 1e-6) : 0.0f});
        }

        PublishLatest(std::move(timings));
        frame.pending = false;
    }

    void VKGpuProfiler::BeginFrame()
    {
        if (!m_Enabled)
            return;

        m_Current = (m_Current + 1) % kFrames;
        Frame& frame = m_Frames[m_Current];

        // The frame this slot held is read now or never
        if (frame.pending)
            Collect(frame);

        vkResetQueryPool(Context::Get().GetDevice(), frame.pool, 0, kMaxScopes * 2);
        frame.used = 0;
        frame.pending = false;
        m_Open.clear();
        m_InFrame = true;
    }

    void VKGpuProfiler::EndFrame()
    {
        if (!m_InFrame)
            return;

        m_InFrame = false;
        m_Frames[m_Current].pending = m_Frames[m_Current].used > 0;

        for (int i = 1; i < kFrames; i++)
        {
            Frame& frame = m_Frames[(m_Current + i) % kFrames];
            if (frame.pending)
                Collect(frame);
        }
    }

    void VKGpuProfiler::Begin(const char* name)
    {
        if (!m_InFrame)
            return;

        Frame& frame = m_Frames[m_Current];
        if (frame.used >= kMaxScopes)
            return;

        frame.names[frame.used] = name;
        vkCmdWriteTimestamp2(Context::Get().Cmd(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, frame.pool, frame.used * 2);
        m_Open.push_back(frame.used);
        frame.used++;
    }

    void VKGpuProfiler::End()
    {
        if (!m_InFrame || m_Open.empty())
            return;

        Frame& frame = m_Frames[m_Current];
        vkCmdWriteTimestamp2(Context::Get().Cmd(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, frame.pool, m_Open.back() * 2 + 1);
        m_Open.pop_back();
    }

}
