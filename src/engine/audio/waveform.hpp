#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Shard::Engine::Audio
{
    struct WaveformColumn
    {
        float min = 0.0f;
        float max = 0.0f;
        float rms = 0.0f;
    };

    struct Waveform
    {
        uint32_t lanes = 0;
        uint32_t columns = 0;
        uint32_t channels = 0;
        uint32_t sampleRate = 0;
        uint64_t frames = 0;

        std::vector<WaveformColumn> data;

        const WaveformColumn& At(uint32_t lane, uint32_t column) const { return data[size_t(lane) * columns + column]; }
        double DurationSeconds() const { return sampleRate ? double(frames) / double(sampleRate) : 0.0; }
    };

    bool ComputeWaveform(const void* encoded, size_t size, uint32_t columns, uint32_t maxLanes, Waveform& out);
}
