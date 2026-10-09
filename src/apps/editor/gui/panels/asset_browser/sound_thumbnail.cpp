#include "sound_thumbnail.hpp"

#include "engine/audio/waveform.hpp"
#include "engine/renderer/features/immediate/thumbnail_service.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace Shard::Editor::GUI {

    namespace {
        constexpr uint32_t kSize = Engine::Rendering::ThumbnailService::kCellSize;

        constexpr uint32_t kMarginX = 8;
        constexpr uint32_t kMarginY = 10;
        constexpr uint32_t kLaneGap = 4;
        constexpr uint32_t kMaxLanes = 2;
        // One column per pixel
        constexpr uint32_t kColumns = kSize - 2 * kMarginX;

        // Louder than this (-26 dB) the waveform is scaled to fill its lane : a thumbnail is there to tell
        // sounds apart by their shape, and a quiet one would otherwise be a flat line. Anything quieter
        // is scaled by this much at most, so near-silence doesn't get blown up into noise.
        constexpr float kNormalizeFloor = 0.05f;

        struct Color
        {
            float r, g, b;
        };

        // Same tile background as the mesh and material thumbnails
        constexpr Color kBackground = {0.145f, 0.145f, 0.157f};
        constexpr Color kAxis = {1.0f, 1.0f, 1.0f};
        // The asset browser's sound accent, dimmed for the peaks and lightened for the RMS core
        constexpr Color kPeak = {0.09f, 0.52f, 0.43f};
        constexpr Color kRms = {0.38f, 0.90f, 0.78f};

        constexpr float kAxisAlpha = 0.12f;

        uint8_t ToByte(float v)
        {
            return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        }

        void Blend(uint8_t* pixel, const Color& color, float alpha)
        {
            const float source[3] = {color.r, color.g, color.b};
            for (int i = 0; i < 3; ++i)
            {
                const float dst = pixel[i] / 255.0f;
                pixel[i] = ToByte(dst + (source[i] - dst) * alpha);
            }
        }

        // A vertical run of pixels in column x between two fractional heights, anti-aliased at both ends
        void FillSpan(std::vector<uint8_t>& rgba, uint32_t x, float top, float bottom, const Color& color, float alpha)
        {
            const int first = std::max(0, int(std::floor(top)));
            const int last = std::min(int(kSize), int(std::ceil(bottom)));

            for (int y = first; y < last; ++y)
            {
                const float coverage = std::min(float(y + 1), bottom) - std::max(float(y), top);
                if (coverage > 0.0f)
                    Blend(&rgba[(size_t(y) * kSize + x) * 4], color, coverage * alpha);
            }
        }
    }

    bool DrawSoundThumbnail(const Engine::Filesystem::Path& file, std::vector<uint8_t>& rgba)
    {
        Engine::Audio::Waveform waveform;
        {
            const std::string encoded = file.ReadFile();
            if (!Engine::Audio::ComputeWaveform(encoded.data(), encoded.size(), kColumns, kMaxLanes, waveform))
                return false;
        }

        rgba.resize(size_t(kSize) * kSize * 4);
        for (size_t i = 0; i < rgba.size(); i += 4)
        {
            rgba[i + 0] = ToByte(kBackground.r);
            rgba[i + 1] = ToByte(kBackground.g);
            rgba[i + 2] = ToByte(kBackground.b);
            rgba[i + 3] = 255;
        }

        float peak = 0.0f;
        for (const Engine::Audio::WaveformColumn& column : waveform.data)
            peak = std::max({peak, std::fabs(column.min), std::fabs(column.max)});
        const float scale = 1.0f / std::max(peak, kNormalizeFloor);

        const float laneHeight = float(kSize - 2 * kMarginY - kLaneGap * (waveform.lanes - 1)) / float(waveform.lanes);

        for (uint32_t lane = 0; lane < waveform.lanes; ++lane)
        {
            const float laneTop = float(kMarginY) + float(lane) * (laneHeight + float(kLaneGap));
            // Centered on a pixel row, so the axis is one crisp line
            const float center = std::floor(laneTop + laneHeight * 0.5f) + 0.5f;
            const float half = laneHeight * 0.5f - 1.0f;

            // The axis reaches a little past the waveform on both sides
            for (uint32_t x = kMarginX - 2; x < kSize - kMarginX + 2; ++x)
                FillSpan(rgba, x, center - 0.5f, center + 0.5f, kAxis, kAxisAlpha);

            for (uint32_t column = 0; column < waveform.columns; ++column)
            {
                const Engine::Audio::WaveformColumn& data = waveform.At(lane, column);
                const uint32_t x = kMarginX + column;

                // Always at least a pixel thick, so a silent stretch still reads as a line
                const float top = std::min(center - 0.5f, center - std::clamp(data.max * scale, -1.0f, 1.0f) * half);
                const float bottom = std::max(center + 0.5f, center - std::clamp(data.min * scale, -1.0f, 1.0f) * half);
                FillSpan(rgba, x, top, bottom, kPeak, 1.0f);

                const float rms = std::clamp(data.rms * scale, 0.0f, 1.0f) * half;
                const float rmsTop = std::min(center - 0.5f, std::max(top, center - rms));
                const float rmsBottom = std::max(center + 0.5f, std::min(bottom, center + rms));
                FillSpan(rgba, x, rmsTop, rmsBottom, kRms, 1.0f);
            }
        }

        return true;
    }
}
