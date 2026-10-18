#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

#include "engine/core/math/math.hpp"

namespace Shard::Engine::Core {

    /// @brief Which cell of the world grid a position is in. 64 bits per axis : with kCellSize = 1024 m the grid reaches
    /// ~9.4e21 km, far beyond a galaxy.
    struct CellCoord {
        int64_t x = 0;
        int64_t y = 0;
        int64_t z = 0;

        bool operator==(const CellCoord& o) const { return x == o.x && y == o.y && z == o.z; }
        bool operator!=(const CellCoord& o) const { return !(*this == o); }
        CellCoord operator+(const CellCoord& o) const { return {x + o.x, y + o.y, z + o.z}; }
        CellCoord operator-(const CellCoord& o) const { return {x - o.x, y - o.y, z - o.z}; }
        bool IsZero() const { return x == 0 && y == 0 && z == 0; }
    };

    /// Side of a cell in meters. A power of two : cell * size is exact in a double, and `local` keeps its full precision.
    inline constexpr double kCellSize = 1024.0;

    /// @brief A position in the world as (cell, local) : an integer cell of the grid plus a double offset inside it.
    ///
    /// A single double loses a millimetre at 10 million km ; this does not, wherever the position is, because the part that
    /// grows (the cell) is an integer. What matters is the DIFFERENCE of two positions : Offset(a, b) is exact when they are
    /// near each other, even when both are very far from the origin. Keep positions in this form, subtract, and only then
    /// narrow to float.
    ///
    /// After any operation the position is normalised : local is in [0, kCellSize) on every axis.
    struct WorldPosition {
        CellCoord cell;
        DVec3 local = DVec3(0.0);

        WorldPosition() = default;
        WorldPosition(const CellCoord& c, const DVec3& l) : cell(c), local(l) { Normalize(); }

        /// From a plain double position (exact up to the double's own precision at that distance)
        static WorldPosition FromDVec3(const DVec3& p) { return WorldPosition(CellCoord{}, p); }

        /// Moves whole cells out of `local` so that it is back in [0, kCellSize) on every axis
        void Normalize()
        {
            const auto fix = [](int64_t& c, double& l) {
                if (l >= 0.0 && l < kCellSize)
                    return;
                // Not a number : pinned. Beyond what 64-bit cells can hold (4e18 cells * 1024 m, ~10^6 light years ; a float position
                // near FLT_MAX is far past it) : the cell saturates and `local` keeps the rest, so the position degrades to a plain
                // double instead of collapsing onto the edge. `local` is then outside [0, kCellSize) : only there.
                constexpr double kMaxCells = 4.0e18;
                if (!std::isfinite(l)) { l = 0.0; return; }
                const double whole = std::floor(l / kCellSize);
                if (whole > kMaxCells || whole < -kMaxCells) {
                    // Only up to the edge : a position that already sits there (cell saturated, `local` out of range) is left as it is,
                    // so that normalising it again, which every operation does, changes nothing and the cell cannot run away.
                    const double room = whole > 0 ? kMaxCells - double(c) : -kMaxCells - double(c);
                    const double move = whole > 0 ? std::min(whole, std::max(room, 0.0)) : std::max(whole, std::min(room, 0.0));
                    c += static_cast<int64_t>(move);
                    l -= move * kCellSize;
                    return;
                }
                c += static_cast<int64_t>(whole);
                l -= whole * kCellSize;
                if (l >= kCellSize) { l -= kCellSize; c += 1; }     // rounding at the very edge
                if (l < 0.0) { l += kCellSize; c -= 1; }
            };
            fix(cell.x, local.x);
            fix(cell.y, local.y);
            fix(cell.z, local.z);
        }

        /// False for a position past the reach of the cells (see Normalize) : it is then a plain double
        bool InCellRange() const
        {
            return local.x >= 0.0 && local.x < kCellSize && local.y >= 0.0 && local.y < kCellSize && local.z >= 0.0 && local.z < kCellSize;
        }

        /// The position as one double : lossy far from the origin, which is what the cells are for
        DVec3 ToDVec3() const
        {
            return DVec3(double(cell.x) * kCellSize + local.x,
                         double(cell.y) * kCellSize + local.y,
                         double(cell.z) * kCellSize + local.z);
        }

        /// This position moved by `delta` meters (exact : the delta is added to `local`, then cells are carried)
        WorldPosition operator+(const DVec3& delta) const { return WorldPosition(cell, local + delta); }
        WorldPosition operator-(const DVec3& delta) const { return WorldPosition(cell, local - delta); }
        WorldPosition& operator+=(const DVec3& delta) { local += delta; Normalize(); return *this; }

        bool operator==(const WorldPosition& o) const { return cell == o.cell && local == o.local; }
        bool operator!=(const WorldPosition& o) const { return !(*this == o); }

        /// @brief a - b in meters. The cells are subtracted as integers first, so the result keeps the double's full
        /// precision when a and b are close, however far from the origin they both are.
        static DVec3 Offset(const WorldPosition& a, const WorldPosition& b)
        {
            const CellCoord d = a.cell - b.cell;
            return DVec3(double(d.x) * kCellSize + (a.local.x - b.local.x),
                         double(d.y) * kCellSize + (a.local.y - b.local.y),
                         double(d.z) * kCellSize + (a.local.z - b.local.z));
        }

        /// @brief This position as a float offset from `origin` : the camera-relative position the GPU wants.
        Vec3 RelativeTo(const WorldPosition& origin) const { return Vec3(Offset(*this, origin)); }
    };

    inline DVec3 operator-(const WorldPosition& a, const WorldPosition& b) { return WorldPosition::Offset(a, b); }
}

namespace std {
    template <> struct hash<Shard::Engine::Core::CellCoord> {
        size_t operator()(const Shard::Engine::Core::CellCoord& c) const noexcept
        {
            size_t h = std::hash<int64_t>()(c.x);
            h ^= std::hash<int64_t>()(c.y) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h ^= std::hash<int64_t>()(c.z) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            return h;
        }
    };
}
