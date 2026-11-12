#include <algorithm>
#include <cstring>

#include <shard/shard_debug.h>
#include <shard/shard_time.h>

#include "engine/plugins/wasm/wasm_imports.hpp"
#include "engine/plugins/wasm/wasm_marshal.hpp"


namespace Shard::Engine::Plugins::Wasm {

    using namespace Marshal;

    namespace {
        constexpr uint64_t kDebugTextBytes = 256;
        constexpr uint64_t kDebugMaxPoints = 1u << 20;
        constexpr uint64_t kDebugMaxTriangles = 1u << 18;

        bool ReadDebugText(Instance& in, uint64_t at, std::string& out)
        {
            const uint8_t* p = Span(in, at, Layout::ShardStr::kSize, "string");
            if (!p) return false;
            const uint32_t data = Load<uint32_t>(p, Layout::ShardStr::o_data);
            const uint64_t size = std::min<uint64_t>(Load<uint64_t>(p, Layout::ShardStr::o_size), kDebugTextBytes);
            out.clear();
            if (size == 0) return true;
            const uint8_t* bytes = Span(in, data, size, "string");
            if (!bytes) return false;
            out.assign(reinterpret_cast<const char*>(bytes), size_t(size));
            return true;
        }
    }

    void AddTimeImports(Sandbox& s, ImportList& imports, const HostApis& apis)
    {
        const ShardTimeApi* api = apis.time;
        // The module gets the simulation's time, never the machine's clock : it stays the same in a replay and the host cannot be timed.
        imports.AddManual("time.get", [&s, api](Instance& in, const uint64_t* a, uint64_t* r) {
            (void)s;
            if (!api || !SHARD_HAS(api, get)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
            OutArg<ShardTimeInfo> out(in, a[0]);
            if (!out.ok) return false;
            const ShardResult result = api->get(&out.value);
            if (result == SHARD_OK) out.value.realTime = out.value.simTime;
            r[0] = uint32_t(result);
            return out.Commit();
        });
    }

    void AddDebugImports(Sandbox& s, ImportList& imports, const HostApis& apis)
    {
        const ShardDebugApi* api = apis.debug;
        (void)s;
        imports.AddManual("debug.lines", [api](Instance& in, const uint64_t* a, uint64_t*) {
            if (!api || !SHARD_HAS(api, lines)) return true;
            std::vector<float> points;
            const uint64_t count = std::min<uint64_t>(uint32_t(a[1]), kDebugMaxPoints) & ~uint64_t(1);
            if (!ReadFloats(in, a[0], count * 3, points)) return false;
            api->lines(points.empty() ? nullptr : points.data(), uint32_t(count), uint32_t(a[2]), F32(a[3]), uint32_t(a[4]));
            return true;
        });
        imports.AddManual("debug.triangles", [api](Instance& in, const uint64_t* a, uint64_t*) {
            if (!api || !SHARD_HAS(api, triangles)) return true;
            std::vector<float> vertices;
            const uint64_t count = std::min<uint64_t>(uint32_t(a[1]), kDebugMaxTriangles);
            if (!ReadFloats(in, a[0], count * 9, vertices)) return false;
            api->triangles(vertices.empty() ? nullptr : vertices.data(), uint32_t(count), uint32_t(a[2]), F32(a[3]), uint32_t(a[4]));
            return true;
        });
        imports.AddManual("debug.box", [api](Instance& in, const uint64_t* a, uint64_t*) {
            if (!api || !SHARD_HAS(api, box)) return true;
            Float3 center(in, a[0]);
            if (!center.ok) return false;
            Float3 half(in, a[1]);
            if (!half.ok) return false;
            if (a[2] == 0) {
                api->box(center.v, half.v, nullptr, uint32_t(a[3]), F32(a[4]), uint32_t(a[5]));
                return true;
            }
            FloatArray<4> rotation(in, a[2]);
            if (!rotation.ok) return false;
            api->box(center.v, half.v, rotation.v, uint32_t(a[3]), F32(a[4]), uint32_t(a[5]));
            return true;
        });
        imports.AddManual("debug.text", [api](Instance& in, const uint64_t* a, uint64_t*) {
            if (!api || !SHARD_HAS(api, text)) return true;
            Float3 position(in, a[0]);
            if (!position.ok) return false;
            std::string text;
            if (!ReadDebugText(in, a[1], text)) return false;
            api->text(position.v, ToStr(text), uint32_t(a[2]), F32(a[3]), uint32_t(a[4]));
            return true;
        });
        imports.AddManual("debug.text_screen", [api](Instance& in, const uint64_t* a, uint64_t*) {
            if (!api || !SHARD_HAS(api, text_screen)) return true;
            std::string text;
            if (!ReadDebugText(in, a[2], text)) return false;
            api->text_screen(F32(a[0]), F32(a[1]), ToStr(text), uint32_t(a[3]), F32(a[4]));
            return true;
        });
    }
}
