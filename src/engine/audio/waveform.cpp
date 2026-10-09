#include "waveform.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <miniaudio/miniaudio.h>

namespace Shard::Engine::Audio
{
    namespace
    {
        constexpr ma_uint64 kChunkFrames = 4096;

        // The sound is reduced in a single pass, without knowing its length first (miniaudio can't tell it
        // for Vorbis, and has to decode the whole file to tell it for MP3) : frames are folded into blocks
        // of `blockSize` frames, and whenever there are kMaxBlocks of them neighbours are merged in pairs
        // and the block size doubles. The result is always between kMaxBlocks / 2 and kMaxBlocks blocks
        // (a few hundred : several per output column), whatever the length. Must stay even.
        constexpr size_t kMaxBlocks = 1024;

        struct Peak
        {
            float min = FLT_MAX;
            float max = -FLT_MAX;
            double sumSq = 0.0;

            void Merge(const Peak& other)
            {
                min = std::min(min, other.min);
                max = std::max(max, other.max);
                sumSq += other.sumSq;
            }
        };

        struct DecoderGuard
        {
            ma_decoder* decoder;
            ~DecoderGuard() { ma_decoder_uninit(decoder); }
        };
    }

    bool ComputeWaveform(const void* encoded, size_t size, uint32_t columns, uint32_t maxLanes, Waveform& out)
    {
        if (!encoded || size == 0 || columns == 0 || maxLanes == 0)
            return false;

        // f32, and the file's own channel count / sample rate (only the shape matters here)
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
        ma_decoder decoder;
        if (ma_decoder_init_memory(encoded, size, &config, &decoder) != MA_SUCCESS)
            return false;
        DecoderGuard guard{&decoder};

        const uint32_t channels = decoder.outputChannels;
        if (channels == 0)
            return false;

        const uint32_t lanes = std::min(channels, maxLanes);
        std::vector<uint32_t> channelsInLane(lanes, 0);
        for (uint32_t c = 0; c < channels; ++c)
            ++channelsInLane[c % lanes];

        std::vector<float> chunk(size_t(kChunkFrames) * channels);

        std::vector<Peak> blocks;          // block-major : blocks[block * lanes + lane]
        std::vector<Peak> current(lanes);
        uint64_t blockSize = 1;
        uint64_t currentFrames = 0;
        uint64_t totalFrames = 0;

        while (true)
        {
            ma_uint64 read = 0;
            ma_result result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), kChunkFrames, &read);

            for (ma_uint64 f = 0; f < read; ++f)
            {
                const float* frame = chunk.data() + size_t(f) * channels;
                for (uint32_t c = 0; c < channels; ++c)
                {
                    Peak& peak = current[c % lanes];
                    const float v = frame[c];
                    peak.min = std::min(peak.min, v);
                    peak.max = std::max(peak.max, v);
                    peak.sumSq += double(v) * double(v);
                }

                if (++currentFrames < blockSize)
                    continue;

                blocks.insert(blocks.end(), current.begin(), current.end());
                std::fill(current.begin(), current.end(), Peak{});
                currentFrames = 0;

                if (blocks.size() / lanes == kMaxBlocks)
                {
                    for (size_t b = 0; b < kMaxBlocks / 2; ++b)
                        for (uint32_t lane = 0; lane < lanes; ++lane)
                        {
                            Peak merged = blocks[(2 * b) * lanes + lane];
                            merged.Merge(blocks[(2 * b + 1) * lanes + lane]);
                            blocks[b * lanes + lane] = merged;
                        }
                    blocks.resize(kMaxBlocks / 2 * lanes);
                    blockSize *= 2;
                }
            }

            totalFrames += read;

            if (result != MA_SUCCESS || read == 0)
                break;
        }

        if (totalFrames == 0)
            return false;

        // The partial block at the end of the sound
        if (currentFrames > 0)
            blocks.insert(blocks.end(), current.begin(), current.end());

        const size_t blockCount = blocks.size() / lanes;

        out = Waveform{};
        out.lanes = lanes;
        out.columns = columns;
        out.channels = channels;
        out.sampleRate = decoder.outputSampleRate;
        out.frames = totalFrames;
        out.data.assign(size_t(lanes) * columns, WaveformColumn{});

        for (uint32_t column = 0; column < columns; ++column)
        {
            const uint64_t first = totalFrames * column / columns;
            const uint64_t last = std::max(first + 1, totalFrames * (column + 1) / columns);

            const size_t firstBlock = std::min<size_t>(blockCount - 1, size_t(first / blockSize));
            const size_t lastBlock = std::min<size_t>(blockCount - 1, size_t((last - 1) / blockSize));

            for (uint32_t lane = 0; lane < lanes; ++lane)
            {
                Peak merged;
                uint64_t frames = 0;
                for (size_t b = firstBlock; b <= lastBlock; ++b)
                {
                    merged.Merge(blocks[b * lanes + lane]);
                    frames += std::min<uint64_t>(blockSize, totalFrames - b * blockSize);
                }

                WaveformColumn& result = out.data[size_t(lane) * columns + column];
                result.min = merged.min;
                result.max = merged.max;
                result.rms = float(std::sqrt(merged.sumSq / double(frames * channelsInLane[lane])));
            }
        }

        return true;
    }
}
