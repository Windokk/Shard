#include <algorithm>
#include <cstring>
#include <vector>

#include <shard/shard_assets.h>
#include <shard/shard_host.h>

#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/plugins/wasm/wasm_imports.hpp"
#include "engine/plugins/wasm/wasm_marshal.hpp"

// shard.assets for a sandboxed plugin : project paths only, nothing the module names reaches the machine.

namespace Shard::Engine::Plugins::Wasm {

    using namespace Marshal;

    namespace {
        constexpr uint64_t kMaxPath = 4096;
        constexpr uint64_t kMaxElements = 16u << 20;

        bool IsOwnUri(const Sandbox& s, const std::string& uri)
        {
            const std::string prefix = "mod://" + s.pluginId + "/";
            return uri.compare(0, prefix.size(), prefix) == 0 && uri.find("..") == std::string::npos && uri.find('\\') == std::string::npos;
        }

        // std::filesystem would let an absolute path replace the project's root, and ':' starts a drive or a scheme.
        bool IsProjectPath(const std::string& path)
        {
            if (path.empty() || path.size() > kMaxPath) return false;
            if (path[0] == '/' || path[0] == '\\') return false;
            if (path.find("..") != std::string::npos || path.find(':') != std::string::npos || path.find('\\') != std::string::npos) return false;
            return path.find('\0') == std::string::npos;
        }

        uint64_t MemoryCap(const Sandbox& s) { return uint64_t(s.instance->GetModule().GetLimits().maxMemoryPages) * 65536; }

        uint32_t Intern(Sandbox& s, const char* data, uint64_t size) { return size ? s.heap.Intern(std::string(data, size_t(size))) : 0; }

        // The strings are interned (project paths, a finite set), so they live longer than the header promises.
        bool WriteInfo(Sandbox& s, Instance& in, uint32_t at, const ShardAssetInfo& info)
        {
            namespace La = Layout::ShardAssetInfo;
            const uint8_t* raw = Span(in, at, 4, "asset info");
            if (!raw) return false;
            const uint32_t asked = Load<uint32_t>(raw, La::o_structSize);
            if (asked < 4) return false;
            const uint32_t pathAt = Intern(s, info.path.data, info.path.size);
            const uint32_t typeAt = Intern(s, info.typeName.data, info.typeName.size);
            uint8_t bytes[La::kSize] = {};
            Store<uint32_t>(bytes, La::o_structSize, asked);
            Store<int32_t>(bytes, La::o_id, info.id);
            Store<uint32_t>(bytes, La::o_kind, info.kind);
            Store<uint32_t>(bytes, La::o_path, pathAt);
            Store<uint64_t>(bytes, La::o_path + Layout::ShardStr::o_size, pathAt ? info.path.size : 0);
            Store<uint32_t>(bytes, La::o_typeName, typeAt);
            Store<uint64_t>(bytes, La::o_typeName + Layout::ShardStr::o_size, typeAt ? info.typeName.size : 0);
            Store<uint64_t>(bytes, La::o_sizeBytes, info.sizeBytes);
            Store<int64_t>(bytes, La::o_modifiedTime, info.modifiedTime);
            const uint32_t n = std::min<uint32_t>(asked, La::kSize);
            if (!in.Write(at + 4, bytes + 4, n - 4)) { in.HostTrap("asset info out of the memory"); return false; }
            return true;
        }

        struct ListCall {
            Sandbox* sandbox;
            uint32_t fn;
            uint32_t user;
        };
        ShardResult SHARD_CALL ListTrampoline(void* user, const ShardAssetInfo* asset)
        {
            namespace La = Layout::ShardAssetInfo;
            auto* call = static_cast<ListCall*>(user);
            Sandbox& s = *call->sandbox;
            if (s.dead || !asset) return SHARD_E_INTERNAL;
            const SandboxHeap::Mark mark = s.heap.TempMark();
            const uint32_t at = s.heap.Temp(La::kSize, La::kAlign);
            if (!at) return SHARD_E_OUT_OF_MEMORY;
            uint8_t head[4] = {};
            Store<uint32_t>(head, 0, La::kSize);
            s.instance->Write(at, head, 4);
            if (!WriteInfo(s, *s.instance, at, *asset)) return SHARD_E_INTERNAL;
            static const FuncType type{ { ValType::I32, ValType::I32 }, { ValType::I32 } };
            const ShardResult result = s.CallResult(call->fn, type, { call->user, at }, Budget::Call);
            s.heap.TempRelease(mark);
            return result;
        }
    }

    void AddAssetsImports(Sandbox& s, ImportList& imports, const HostApis& apis)
    {
        const ShardAssetsApi* api = apis.assets;
        auto allowed = [&s, api] { return api && s.Allowed(Permission::FsReadProject); };

        imports.AddManual("assets.info", [&s, api, allowed](Instance& in, const uint64_t* a, uint64_t* r) {
            if (!allowed() || !SHARD_HAS(api, info)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
            if (!Span(in, a[1], 4, "asset info")) return false;
            ShardAssetInfo info{};
            info.structSize = sizeof(ShardAssetInfo);
            const ShardResult result = api->info(int32_t(uint32_t(a[0])), &info);
            r[0] = uint32_t(result);
            if (result != SHARD_OK) return true;
            return WriteInfo(s, in, uint32_t(a[1]), info);
        });

        imports.AddManual("assets.list", [&s, api, allowed](Instance& in, const uint64_t* a, uint64_t* r) {
            if (!allowed() || !SHARD_HAS(api, list)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
            std::string subdir;
            if (!ReadStr(in, a[0], subdir, kMaxPath)) return false;
            if (!subdir.empty() && !IsProjectPath(subdir)) { r[0] = uint32_t(SHARD_E_INVALID_ARG); return true; }
            if (a[2] == 0) { r[0] = uint32_t(SHARD_E_INVALID_ARG); return true; }
            ListCall call{ &s, uint32_t(a[2]), uint32_t(a[3]) };
            r[0] = uint32_t(api->list(ToStr(subdir), uint32_t(a[1]), &ListTrampoline, &call));
            return true;
        });

        imports.AddManual("assets.read", [&s, api, allowed](Instance& in, const uint64_t* a, uint64_t* r) {
            if (!allowed() || !SHARD_HAS(api, read)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
            std::string path;
            if (!ReadStr(in, a[1], path, kMaxPath)) return false;
            if (!Span(in, a[2], 4, "data") || !Span(in, a[3], 8, "size")) return false;
            const bool mod = path.compare(0, 6, "mod://") == 0;
            if (mod ? !(s.Allowed(Permission::FsReadContent) && IsOwnUri(s, path)) : !IsProjectPath(path)) { r[0] = uint32_t(SHARD_E_INVALID_ARG); return true; }
            if (!mod && SHARD_HAS(api, resolve) && SHARD_HAS(api, info)) {
                int32_t id = 0;
                ShardAssetInfo info{};
                info.structSize = sizeof(ShardAssetInfo);
                if (api->resolve(ToStr(path), &id) == SHARD_OK && api->info(id, &info) == SHARD_OK && info.sizeBytes > MemoryCap(s)) {
                    r[0] = uint32_t(SHARD_E_OUT_OF_MEMORY);
                    return true;
                }
            }
            void* data = nullptr;
            uint64_t size = 0;
            ShardResult result = api->read(s.ctx, ToStr(path), &data, &size);
            if (result != SHARD_OK) { r[0] = uint32_t(result); return true; }
            uint32_t at = 0;
            if (size > 0xFFFFFFF0ull || !(at = s.heap.Alloc(uint32_t(std::max<uint64_t>(size, 1)), 16))) result = SHARD_E_OUT_OF_MEMORY;
            else {
                if (size) in.Write(at, data, uint32_t(size));
                in.Write(uint32_t(a[2]), &at, 4);
                in.Write(uint32_t(a[3]), &size, 8);
            }
            GetCHostApi()->free(s.ctx, data);
            r[0] = uint32_t(result);
            return true;
        });

        imports.AddManual("assets.write", [&s, api, allowed](Instance& in, const uint64_t* a, uint64_t* r) {
            if (!allowed() || !s.Allowed(Permission::FsWriteProject) || !SHARD_HAS(api, write)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
            std::string path;
            if (!ReadStr(in, a[1], path, kMaxPath)) return false;
            std::vector<uint8_t> bytes;
            if (!ReadBytes(in, a[2], a[3], bytes)) return false;
            if (a[4] != 0 && !Span(in, a[4], 4, "id")) return false;
            if (!IsProjectPath(path)) { r[0] = uint32_t(SHARD_E_INVALID_ARG); return true; }
            int32_t id = 0;
            const ShardResult result = api->write(s.ctx, ToStr(path), bytes.empty() ? nullptr : bytes.data(), bytes.size(), a[4] ? &id : nullptr);
            if (a[4] != 0) in.Write(uint32_t(a[4]), &id, 4);
            r[0] = uint32_t(result);
            return true;
        });

        imports.AddManual("assets.exists", [&s, api, allowed](Instance& in, const uint64_t* a, uint64_t* r) {
            (void)s;
            r[0] = 0;
            if (!allowed() || !SHARD_HAS(api, exists)) return true;
            std::string path;
            if (!ReadStr(in, a[0], path, kMaxPath)) return false;
            if (IsProjectPath(path)) r[0] = uint32_t(api->exists(ToStr(path)));
            return true;
        });

        imports.AddManual("assets.mesh_read", [&s, api, allowed](Instance& in, const uint64_t* a, uint64_t* r) {
            namespace Lm = Layout::ShardMeshRead;
            (void)s;
            if (!allowed() || !SHARD_HAS(api, mesh_read)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
            const uint8_t* raw = Span(in, a[1], Lm::kSize, "mesh");
            if (!raw) return false;
            uint8_t head[Lm::kSize];
            std::memcpy(head, raw, Lm::kSize);
            if (Load<uint32_t>(head, Lm::o_structSize) < Lm::o_positions) { r[0] = uint32_t(SHARD_E_INVALID_ARG); return true; }
            const uint32_t vertexCapacity = Load<uint32_t>(head, Lm::o_vertexCapacity), indexCapacity = Load<uint32_t>(head, Lm::o_indexCapacity);
            const uint32_t sliceCapacity = Load<uint32_t>(head, Lm::o_sliceCapacity);
            const uint32_t positionsAt = Load<uint32_t>(head, Lm::o_positions), normalsAt = Load<uint32_t>(head, Lm::o_normals);
            const uint32_t uvsAt = Load<uint32_t>(head, Lm::o_uvs), indicesAt = Load<uint32_t>(head, Lm::o_indices), slicesAt = Load<uint32_t>(head, Lm::o_slices);
            if (vertexCapacity > kMaxElements || indexCapacity > kMaxElements || sliceCapacity > kMaxElements) {
                in.HostTrap("a mesh read of more than 16 million elements");
                return false;
            }
            if ((positionsAt && !Span(in, positionsAt, uint64_t(vertexCapacity) * 12, "positions")) || (normalsAt && !Span(in, normalsAt, uint64_t(vertexCapacity) * 12, "normals")) ||
                (uvsAt && !Span(in, uvsAt, uint64_t(vertexCapacity) * 8, "uvs")) || (indicesAt && !Span(in, indicesAt, uint64_t(indexCapacity) * 4, "indices")) ||
                (slicesAt && !Span(in, slicesAt, uint64_t(sliceCapacity) * Layout::ShardMeshSlice::kSize, "slices")))
                return false;

            std::vector<float> positions(positionsAt ? size_t(vertexCapacity) * 3 : 0), normals(normalsAt ? size_t(vertexCapacity) * 3 : 0), uvs(uvsAt ? size_t(vertexCapacity) * 2 : 0);
            std::vector<uint32_t> indices(indicesAt ? indexCapacity : 0);
            std::vector<ShardMeshSlice> slices(slicesAt ? sliceCapacity : 0);
            static_assert(sizeof(ShardMeshSlice) == Layout::ShardMeshSlice::kSize);
            ShardMeshRead io{};
            io.structSize = sizeof(ShardMeshRead);
            io.vertexCapacity = positionsAt || normalsAt || uvsAt ? vertexCapacity : 0;
            io.indexCapacity = indicesAt ? indexCapacity : 0;
            io.sliceCapacity = slicesAt ? sliceCapacity : 0;
            io.positions = positions.empty() ? nullptr : positions.data();
            io.normals = normals.empty() ? nullptr : normals.data();
            io.uvs = uvs.empty() ? nullptr : uvs.data();
            io.indices = indices.empty() ? nullptr : indices.data();
            io.slices = slices.empty() ? nullptr : slices.data();
            const ShardResult result = api->mesh_read(int32_t(uint32_t(a[0])), &io);
            r[0] = uint32_t(result);
            if (result != SHARD_OK) return true;

            const uint32_t vertices = std::min(io.vertexCount, io.vertexCapacity), indexCount = std::min(io.indexCount, io.indexCapacity), sliceCount = std::min(io.sliceCount, io.sliceCapacity);
            bool ok = true;
            if (positionsAt && vertices) ok = ok && in.Write(positionsAt, positions.data(), vertices * 12);
            if (normalsAt && vertices) ok = ok && in.Write(normalsAt, normals.data(), vertices * 12);
            if (uvsAt && vertices) ok = ok && in.Write(uvsAt, uvs.data(), vertices * 8);
            if (indicesAt && indexCount) ok = ok && in.Write(indicesAt, indices.data(), indexCount * 4);
            if (slicesAt && sliceCount) ok = ok && in.Write(slicesAt, slices.data(), sliceCount * Layout::ShardMeshSlice::kSize);
            ok = ok && in.Write(uint32_t(a[1]) + Lm::o_vertexCount, &io.vertexCount, 4) && in.Write(uint32_t(a[1]) + Lm::o_indexCount, &io.indexCount, 4) &&
                 in.Write(uint32_t(a[1]) + Lm::o_sliceCount, &io.sliceCount, 4);
            if (!ok) { in.HostTrap("mesh out of the memory"); return false; }
            return true;
        });

        imports.AddManual("assets.mesh_skin_read", [&s, api, allowed](Instance& in, const uint64_t* a, uint64_t* r) {
            namespace Ls = Layout::ShardSkinRead;
            (void)s;
            if (!allowed() || !SHARD_HAS(api, mesh_skin_read)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
            const uint8_t* raw = Span(in, a[1], Ls::kSize, "skin");
            if (!raw) return false;
            uint8_t head[Ls::kSize];
            std::memcpy(head, raw, Ls::kSize);
            if (Load<uint32_t>(head, Ls::o_structSize) < Ls::o_inverseBind) { r[0] = uint32_t(SHARD_E_INVALID_ARG); return true; }
            const uint32_t vertexCapacity = Load<uint32_t>(head, Ls::o_vertexCapacity), nodeCapacity = Load<uint32_t>(head, Ls::o_nodeCapacity);
            const uint32_t jointCapacity = Load<uint32_t>(head, Ls::o_jointCapacity);
            const uint32_t jointsAt = Load<uint32_t>(head, Ls::o_joints), weightsAt = Load<uint32_t>(head, Ls::o_weights), nodesAt = Load<uint32_t>(head, Ls::o_nodes);
            const uint32_t jointNodesAt = Load<uint32_t>(head, Ls::o_jointNodes), inverseBindAt = Load<uint32_t>(head, Ls::o_inverseBind);
            if (vertexCapacity > kMaxElements || nodeCapacity > kMaxElements || jointCapacity > kMaxElements) {
                in.HostTrap("a skin read of more than 16 million elements");
                return false;
            }
            static_assert(sizeof(ShardSkinNode) == Layout::ShardSkinNode::kSize);
            if ((jointsAt && !Span(in, jointsAt, uint64_t(vertexCapacity) * 8, "joints")) || (weightsAt && !Span(in, weightsAt, uint64_t(vertexCapacity) * 16, "weights")) ||
                (nodesAt && !Span(in, nodesAt, uint64_t(nodeCapacity) * Layout::ShardSkinNode::kSize, "nodes")) ||
                (jointNodesAt && !Span(in, jointNodesAt, uint64_t(jointCapacity) * 4, "joint nodes")) ||
                (inverseBindAt && !Span(in, inverseBindAt, uint64_t(jointCapacity) * 64, "inverse bind")))
                return false;

            std::vector<uint16_t> joints(jointsAt ? size_t(vertexCapacity) * 4 : 0);
            std::vector<float> weights(weightsAt ? size_t(vertexCapacity) * 4 : 0), inverseBind(inverseBindAt ? size_t(jointCapacity) * 16 : 0);
            std::vector<ShardSkinNode> nodes(nodesAt ? nodeCapacity : 0);
            std::vector<int32_t> jointNodes(jointNodesAt ? jointCapacity : 0);
            ShardSkinRead io{};
            io.structSize = sizeof(ShardSkinRead);
            io.vertexCapacity = jointsAt || weightsAt ? vertexCapacity : 0;
            io.nodeCapacity = nodesAt ? nodeCapacity : 0;
            io.jointCapacity = jointNodesAt || inverseBindAt ? jointCapacity : 0;
            io.joints = joints.empty() ? nullptr : joints.data();
            io.weights = weights.empty() ? nullptr : weights.data();
            io.nodes = nodes.empty() ? nullptr : nodes.data();
            io.jointNodes = jointNodes.empty() ? nullptr : jointNodes.data();
            io.inverseBind = inverseBind.empty() ? nullptr : inverseBind.data();
            const ShardResult result = api->mesh_skin_read(int32_t(uint32_t(a[0])), &io);
            r[0] = uint32_t(result);
            if (result != SHARD_OK) return true;

            const uint32_t vertices = std::min(io.vertexCount, io.vertexCapacity), nodeCount = std::min(io.nodeCount, io.nodeCapacity), jointCount = std::min(io.jointCount, io.jointCapacity);
            bool ok = true;
            if (jointsAt && vertices) ok = ok && in.Write(jointsAt, joints.data(), vertices * 8);
            if (weightsAt && vertices) ok = ok && in.Write(weightsAt, weights.data(), vertices * 16);
            if (nodesAt && nodeCount) ok = ok && in.Write(nodesAt, nodes.data(), nodeCount * Layout::ShardSkinNode::kSize);
            if (jointNodesAt && jointCount) ok = ok && in.Write(jointNodesAt, jointNodes.data(), jointCount * 4);
            if (inverseBindAt && jointCount) ok = ok && in.Write(inverseBindAt, inverseBind.data(), jointCount * 64);
            ok = ok && in.Write(uint32_t(a[1]) + Ls::o_vertexCount, &io.vertexCount, 4) && in.Write(uint32_t(a[1]) + Ls::o_nodeCount, &io.nodeCount, 4) &&
                 in.Write(uint32_t(a[1]) + Ls::o_jointCount, &io.jointCount, 4);
            if (!ok) { in.HostTrap("skin out of the memory"); return false; }
            return true;
        });

        imports.AddManual("assets.image_read", [&s, api, allowed](Instance& in, const uint64_t* a, uint64_t* r) {
            (void)s;
            if (!allowed() || !SHARD_HAS(api, image_read)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
            const uint64_t capacity = a[2];
            if (!Span(in, a[3], 8, "size")) return false;
            if (capacity > 0 && !Span(in, a[1], capacity, "pixels")) return false;
            std::vector<uint8_t> pixels(a[1] && capacity ? size_t(capacity) : 0);
            uint64_t size = 0;
            const ShardResult result = api->image_read(int32_t(uint32_t(a[0])), pixels.empty() ? nullptr : pixels.data(), pixels.size(), &size);
            r[0] = uint32_t(result);
            if (result != SHARD_OK) return true;
            const uint64_t copied = std::min<uint64_t>(size, pixels.size());
            if (copied) in.Write(uint32_t(a[1]), pixels.data(), uint32_t(copied));
            in.Write(uint32_t(a[3]), &size, 8);
            return true;
        });
    }
}
