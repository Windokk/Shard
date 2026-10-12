#pragma once

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace Shard::Engine::Core {

    /// A rectangle to place in one of several equal bins (an atlas page) : `w` and `h` are the input, `x`, `y` and `bin` what
    /// PackRectsShelf() fills in (`x`, `y` is the corner with the smallest coordinates).
    struct PackRect
    {
        float w = 0.0f;
        float h = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        int bin = -1;       // -1 : not placed
    };

    /// @brief Packs rectangles into at most `maxBins` bins of `binWidth` x `binHeight` with the "shelf" heuristic : the
    /// rectangles, tallest first, go left to right along horizontal shelves (the first one that fits is used), a new shelf is
    /// opened when none does, a new bin when the bins are full. Simple, deterministic (the same input always gives the same
    /// layout) and good enough for atlas building : the waste is mostly the space above the shorter rectangles of a shelf.
    /// @return how many bins were used, or -1 when a rectangle is bigger than a bin, or does not fit in `maxBins`. The rects
    /// that were placed have their `bin` set even then.
    inline int PackRectsShelf(std::vector<PackRect>& rects, float binWidth, float binHeight, int maxBins = 1 << 20)
    {
        struct Shelf { float y; float height; float cursor; };
        struct Bin { std::vector<Shelf> shelves; float nextY = 0.0f; };
        constexpr size_t kShelvesLookedAt = 24;

        std::vector<size_t> order(rects.size());
        std::iota(order.begin(), order.end(), size_t(0));
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (rects[a].h != rects[b].h) return rects[a].h > rects[b].h;
            return rects[a].w > rects[b].w;
        });

        for (PackRect& r : rects)
            r.bin = -1;

        std::vector<Bin> bins;
        bool failed = false;

        for (size_t index : order)
        {
            PackRect& r = rects[index];
            if (r.w > binWidth || r.h > binHeight)
            {
                failed = true;
                continue;
            }

            bool placed = false;
            for (size_t b = 0; b < bins.size() && !placed; b++)
            {
                Bin& bin = bins[b];

                // Only the latest shelves are looked at : the rectangles come tallest first, so an old shelf is full (or too
                // short for what is left), and a layout of ten thousands of rectangles stays linear instead of quadratic
                const size_t shelfCount = bin.shelves.size();
                const size_t firstShelf = shelfCount > kShelvesLookedAt ? shelfCount - kShelvesLookedAt : 0;
                for (size_t s = shelfCount; s > firstShelf; s--)
                {
                    Shelf& shelf = bin.shelves[s - 1];
                    if (r.h <= shelf.height && shelf.cursor + r.w <= binWidth)
                    {
                        r.x = shelf.cursor;
                        r.y = shelf.y;
                        r.bin = int(b);
                        shelf.cursor += r.w;
                        placed = true;
                        break;
                    }
                }

                if (!placed && bin.nextY + r.h <= binHeight)
                {
                    bin.shelves.push_back({ bin.nextY, r.h, r.w });
                    r.x = 0.0f;
                    r.y = bin.nextY;
                    r.bin = int(b);
                    bin.nextY += r.h;
                    placed = true;
                }
            }

            if (!placed)
            {
                if (int(bins.size()) >= maxBins)
                {
                    failed = true;
                    continue;
                }

                bins.emplace_back();
                Bin& bin = bins.back();
                bin.shelves.push_back({ 0.0f, r.h, r.w });
                bin.nextY = r.h;
                r.x = 0.0f;
                r.y = 0.0f;
                r.bin = int(bins.size()) - 1;
            }
        }

        return failed ? -1 : int(bins.size());
    }
}
