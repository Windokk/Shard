#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Shard::Engine::Audio
{
    /// What a waveform shows for one slice of the sound : the extremes of the signal and its average
    /// loudness (RMS). All in [-1, 1] / [0, 1].
    struct WaveformColumn
    {
        float min = 0.0f;
        float max = 0.0f;
        float rms = 0.0f;
    };

    /// A sound reduced to a fixed number of columns, ready to be drawn. `lanes` waveforms are stacked (mono : 1,
    /// stereo : left then right) ; a sound with more channels than lanes folds them (channel c goes to lane c % lanes).
    struct Waveform
    {
        uint32_t lanes = 0;
        uint32_t columns = 0;
        uint32_t channels = 0;
        uint32_t sampleRate = 0;
        uint64_t frames = 0;

        /// Lane-major : lane * columns + column
        std::vector<WaveformColumn> data;

        const WaveformColumn& At(uint32_t lane, uint32_t column) const { return data[size_t(lane) * columns + column]; }
        double DurationSeconds() const { return sampleRate ? double(frames) / double(sampleRate) : 0.0; }
    };

    /// @brief Decodes a whole encoded sound file (wav, mp3, ogg... whatever the audio engine plays) and reduces it to
    /// `columns` columns of peaks and RMS per lane. Thread-safe : it touches nothing but its arguments, so it can run
    /// on a worker thread (decoding a long track takes a while).
    /// @param maxLanes how many lanes the caller can draw at most (1 : everything is folded together)
    /// @return false if the data can't be decoded or holds no audio
    bool ComputeWaveform(const void* encoded, size_t size, uint32_t columns, uint32_t maxLanes, Waveform& out);
}
