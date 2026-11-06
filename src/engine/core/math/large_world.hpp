#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

#include "engine/core/math/math.hpp"

namespace Shard::Engine::Core {

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

    // Meters per cell. Power of two so cell * size is exact in a double.
    inline constexpr double kCellSize = 1024.0;

    // (cell, local) position, normalised so local is in [0, kCellSize). Subtract positions first, narrow to float after.
    struct WorldPosition {
        CellCoord cell;
        DVec3 local = DVec3(0.0);

        WorldPosition() = default;
        WorldPosition(const CellCoord& c, const DVec3& l) : cell(c), local(l) { Normalize(); }

        static WorldPosition FromDVec3(const DVec3& p) { return WorldPosition(CellCoord{}, p); }

        void Normalize()
        {
            const auto fix = [](int64_t& c, double& l) {
                if (l >= 0.0 && l < kCellSize)
                    return;
                // NaN is pinned. Past 64-bit cells the cell saturates and `local` keeps the rest (plain double, out of range).
                constexpr double kMaxCells = 4.0e18;
                if (!std::isfinite(l)) { l = 0.0; return; }
                const double whole = std::floor(l / kCellSize);
                if (whole > kMaxCells || whole < -kMaxCells) {
                    const double room = whole > 0 ? kMaxCells - double(c) : -kMaxCells - double(c);
                    const double move = whole > 0 ? std::min(whole, std::max(room, 0.0)) : std::max(whole, std::min(room, 0.0));
                    c += static_cast<int64_t>(move);
                    l -= move * kCellSize;
                    return;
                }
                c += static_cast<int64_t>(whole);
                l -= whole * kCellSize;
                if (l >= kCellSize) { l -= kCellSize; c += 1; }
                if (l < 0.0) { l += kCellSize; c -= 1; }
            };
            fix(cell.x, local.x);
            fix(cell.y, local.y);
            fix(cell.z, local.z);
        }

        bool InCellRange() const
        {
            return local.x >= 0.0 && local.x < kCellSize && local.y >= 0.0 && local.y < kCellSize && local.z >= 0.0 && local.z < kCellSize;
        }

        DVec3 ToDVec3() const
        {
            return DVec3(double(cell.x) * kCellSize + local.x,
                         double(cell.y) * kCellSize + local.y,
                         double(cell.z) * kCellSize + local.z);
        }

        WorldPosition operator+(const DVec3& delta) const { return WorldPosition(cell, local + delta); }
        WorldPosition operator-(const DVec3& delta) const { return WorldPosition(cell, local - delta); }
        WorldPosition& operator+=(const DVec3& delta) { local += delta; Normalize(); return *this; }

        bool operator==(const WorldPosition& o) const { return cell == o.cell && local == o.local; }
        bool operator!=(const WorldPosition& o) const { return !(*this == o); }

        static DVec3 Offset(const WorldPosition& a, const WorldPosition& b)
        {
            const CellCoord d = a.cell - b.cell;
            return DVec3(double(d.x) * kCellSize + (a.local.x - b.local.x),
                         double(d.y) * kCellSize + (a.local.y - b.local.y),
                         double(d.z) * kCellSize + (a.local.z - b.local.z));
        }

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
