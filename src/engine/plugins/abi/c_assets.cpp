#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <shard/shard_assets.h>

#include "engine/assets/assetID.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/assets/vfs/filesystem.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/world/engine.hpp"

// shard.assets : the assets of the project by path and id, the files the plugin reads and writes in it. What needs the renderer (the geometry of
// a mesh, the pixels of an image) is asked of the engine root (c_abi_assets.cpp) : the renderer is above this layer.

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        Filesystem::AssetIDManager* Assets()
        {
            Core::IEngineContext* engine = Core::gSharedEnginePtr;
            return engine ? engine->GetAssetIDManager() : nullptr;
        }

        uint32_t KindOf(Filesystem::Type type)
        {
            using Filesystem::Type;
            switch (type) {
                case Type::T_IMAGE: return SHARD_ASSET_TEXTURE;
                case Type::T_MODEL: return SHARD_ASSET_MESH;
                case Type::T_MATERIAL: return SHARD_ASSET_MATERIAL;
                case Type::T_SHADER:
                case Type::T_COMPUTE_SHADER: return SHARD_ASSET_SHADER;
                case Type::T_WORLD: return SHARD_ASSET_WORLD;
                case Type::T_SOUND: return SHARD_ASSET_SOUND;
                case Type::T_CUSTOM: return SHARD_ASSET_PLUGIN;
                default: return SHARD_ASSET_UNKNOWN;
            }
        }

        std::string Lower(std::string s)
        {
            for (char& c : s) c = char(std::tolower((unsigned char)c));
            return s;
        }

        // The name of the plugin asset type that owns an extension ("" : none)
        std::string TypeNameOf(const std::string& extension)
        {
            const std::string wanted = Lower(extension);
            for (const auto& type : PluginManager::Get().Extensions().Get<Core::Resources::AssetTypeExtension>("asset.type"))
                for (const std::string& e : type->extensions)
                    if (Lower(e) == wanted) return type->name;
            return {};
        }

        int64_t ModifiedTime(const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto time = std::filesystem::last_write_time(path, ec);
            if (ec) return 0;
            return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
        }

        // The ShardAssetInfo of an asset ; the strings point into `path` and `typeName`
        ShardAssetInfo Describe(const Filesystem::AssetInfos& infos, std::string& path, std::string& typeName)
        {
            ShardAssetInfo out{};
            out.structSize = sizeof(ShardAssetInfo);
            out.id = infos.baseInfos.ID.GetAsInt();
            out.kind = KindOf(infos.baseInfos.type);
            path = infos.baseInfos.nameInProject;
            std::replace(path.begin(), path.end(), '\\', '/');
            out.path = ToStr(path);
            if (out.kind == SHARD_ASSET_PLUGIN) {
                typeName = TypeNameOf(infos.baseInfos.extension);
                out.typeName = ToStr(typeName);
            }
            const std::filesystem::path file(infos.baseInfos.path.GetNativePath());
            std::error_code ec;
            const auto size = std::filesystem::is_regular_file(file, ec) ? std::filesystem::file_size(file, ec) : 0;
            out.sizeBytes = ec ? 0 : uint64_t(size);
            out.modifiedTime = ModifiedTime(file);
            return out;
        }

        void CopyInfo(const ShardAssetInfo& full, ShardAssetInfo* out)
        {
            const uint32_t asked = out->structSize;
            std::memcpy(out, &full, std::min<size_t>(asked, sizeof(ShardAssetInfo)));
            out->structSize = asked;
        }

        ShardResult SHARD_CALL Resolve(ShardStr path, int32_t* id)
        {
            try {
                if (!id) return SHARD_E_INVALID_ARG;
                *id = 0;
                Filesystem::AssetIDManager* assets = Assets();
                if (!assets) return SHARD_E_UNSUPPORTED;
                std::string name = ToString(path);
                std::replace(name.begin(), name.end(), '\\', '/');
                if (name.empty()) return SHARD_E_INVALID_ARG;
                const Filesystem::AssetID found = assets->GetIDFromNameInProject(name);
                if (!assets->GetAssetFromID(found)) return SHARD_E_NOT_FOUND;
                *id = found.GetAsInt();
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Info(int32_t id, ShardAssetInfo* out)
        {
            try {
                if (!out || out->structSize < sizeof(uint32_t)) return SHARD_E_INVALID_ARG;
                Filesystem::AssetIDManager* assets = Assets();
                if (!assets) return SHARD_E_UNSUPPORTED;
                auto infos = assets->GetAssetFromID(Filesystem::AssetID(id));
                if (!infos) return SHARD_E_NOT_FOUND;
                std::string path, typeName;
                CopyInfo(Describe(*infos, path, typeName), out);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL PathOf(int32_t id, char* buffer, uint64_t capacity, uint64_t* size)
        {
            try {
                Filesystem::AssetIDManager* assets = Assets();
                if (!assets) return SHARD_E_UNSUPPORTED;
                auto infos = assets->GetAssetFromID(Filesystem::AssetID(id));
                if (!infos) return SHARD_E_NOT_FOUND;
                std::string path = infos->baseInfos.nameInProject;
                std::replace(path.begin(), path.end(), '\\', '/');
                return CopyOut(path, buffer, capacity, size);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL List(ShardStr subdir, uint32_t kind, ShardAssetListFn fn, void* user)
        {
            try {
                if (!fn) return SHARD_E_INVALID_ARG;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                Filesystem::AssetIDManager* assets = Assets();
                if (!assets) return SHARD_E_UNSUPPORTED;
                std::string prefix = ToString(subdir);
                std::replace(prefix.begin(), prefix.end(), '\\', '/');
                while (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
                struct Row { std::string path; std::shared_ptr<Filesystem::AssetInfos> infos; };
                std::vector<Row> rows;
                for (const auto& [id, infos] : assets->AssetIDMap) {
                    if (!infos || infos->baseInfos.isDirectory) continue;
                    if (kind != SHARD_ASSET_UNKNOWN && KindOf(infos->baseInfos.type) != kind) continue;
                    std::string path = infos->baseInfos.nameInProject;
                    std::replace(path.begin(), path.end(), '\\', '/');
                    if (!prefix.empty() && path.compare(0, prefix.size(), prefix) != 0) continue;
                    if (!prefix.empty() && path.size() > prefix.size() && path[prefix.size()] != '/') continue;
                    rows.push_back({ std::move(path), infos });
                }
                std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.path < b.path; });
                for (const Row& row : rows) {
                    std::string path, typeName;
                    const ShardAssetInfo info = Describe(*row.infos, path, typeName);
                    const ShardResult result = fn(user, &info);
                    if (result != SHARD_OK) return result;
                }
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // The file a path in the project (or a mod:// address) names
        bool FileOf(PluginContext& ctx, const std::string& path, std::string& file)
        {
            if (ContentMounts::IsModUri(path)) {
                const auto resolved = ctx.Content().Resolve(path);
                if (!resolved) return false;
                file = *resolved;
                return true;
            }
            Core::IEngineContext* engine = Core::gSharedEnginePtr;
            Filesystem::FileManager* files = engine ? engine->GetFileManager() : nullptr;
            Filesystem::AssetIDManager* assets = Assets();
            if (!files || !assets || path.find("..") != std::string::npos) return false;
            if (auto infos = assets->GetAssetFromID(assets->GetIDFromNameInProject(path))) {
                file = infos->baseInfos.path.GetNativePath();
                return true;
            }
            // A file that is in the project's resources but not indexed (yet)
            const std::filesystem::path guess = std::filesystem::path(files->GetProjectResRoot().GetNativePath()) / path;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(guess, ec)) return false;
            file = guess.string();
            return true;
        }

        ShardResult SHARD_CALL Read(ShardPluginCtx handle, ShardStr path, void** data, uint64_t* size)
        {
            try {
                if (!data || !size) return SHARD_E_INVALID_ARG;
                *data = nullptr;
                *size = 0;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                std::string file;
                if (!FileOf(*ctx, ToString(path), file)) return SHARD_E_NOT_FOUND;
                std::ifstream in(file, std::ios::binary);
                if (!in) return SHARD_E_NOT_FOUND;
                const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                void* block = ctx->Memory().Allocate(bytes.empty() ? 1 : bytes.size(), 16);
                if (!block) return SHARD_E_OUT_OF_MEMORY;
                if (!bytes.empty()) std::memcpy(block, bytes.data(), bytes.size());
                *data = block;
                *size = bytes.size();
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Write(ShardPluginCtx handle, ShardStr path, const void* data, uint64_t size, int32_t* id)
        {
            try {
                if (id) *id = 0;
                if (!data && size) return SHARD_E_INVALID_ARG;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                Core::IEngineContext* engine = Core::gSharedEnginePtr;
                Filesystem::FileManager* files = engine ? engine->GetFileManager() : nullptr;
                if (!files) return SHARD_E_UNSUPPORTED;
                std::string name = ToString(path);
                std::replace(name.begin(), name.end(), '\\', '/');
                // Inside the project's resources : no "..", no drive, no absolute path
                if (name.empty() || name[0] == '/' || name.find("..") != std::string::npos || name.find(':') != std::string::npos) return SHARD_E_INVALID_ARG;
                const std::filesystem::path target = std::filesystem::path(files->GetProjectResRoot().GetNativePath()) / name;
                std::error_code ec;
                std::filesystem::create_directories(target.parent_path(), ec);
                {
                    std::ofstream out(target, std::ios::binary | std::ios::trunc);
                    if (!out) return SHARD_E_INTERNAL;
                    if (size) out.write(static_cast<const char*>(data), std::streamsize(size));
                    if (!out) return SHARD_E_INTERNAL;
                }
                const Filesystem::AssetID registered = files->RegisterAsset(Filesystem::Path(target.string(), true));
                if (id) *id = registered.GetAsInt();
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        int32_t SHARD_CALL Exists(ShardStr path)
        {
            try {
                std::string name = ToString(path);
                std::replace(name.begin(), name.end(), '\\', '/');
                Filesystem::AssetIDManager* assets = Assets();
                if (assets && assets->GetAssetFromID(assets->GetIDFromNameInProject(name))) return SHARD_TRUE;
                Core::IEngineContext* engine = Core::gSharedEnginePtr;
                Filesystem::FileManager* files = engine ? engine->GetFileManager() : nullptr;
                if (!files || name.find("..") != std::string::npos) return SHARD_FALSE;
                std::error_code ec;
                return std::filesystem::is_regular_file(std::filesystem::path(files->GetProjectResRoot().GetNativePath()) / name, ec) ? SHARD_TRUE : SHARD_FALSE;
            } catch (...) { return SHARD_FALSE; }
        }

        ShardResult SHARD_CALL MeshInfo(int32_t id, ShardMeshInfo* out)
        {
            try {
                if (!out || out->structSize < sizeof(uint32_t)) return SHARD_E_INVALID_ARG;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                return AssetMeshInfo(id, out);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL MeshRead(int32_t id, ShardMeshRead* io)
        {
            try {
                if (!io || io->structSize < offsetof(ShardMeshRead, positions)) return SHARD_E_INVALID_ARG;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                return AssetMeshRead(id, io);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ImageInfo(int32_t id, uint32_t* width, uint32_t* height)
        {
            try {
                if (!width || !height) return SHARD_E_INVALID_ARG;
                return AssetImageInfo(id, width, height);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ImageRead(int32_t id, uint8_t* out, uint64_t capacity, uint64_t* size)
        {
            try {
                if (!size || (!out && capacity)) return SHARD_E_INVALID_ARG;
                return AssetImageRead(id, out, capacity, size);
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    bool ResolveProjectFile(PluginContext& ctx, const std::string& path, std::string& file) { return FileOf(ctx, path, file); }

    const void* AssetsApi()
    {
        static const ShardAssetsApi api = {
            sizeof(ShardAssetsApi), &Resolve, &Info, &PathOf, &List, &Read, &Write, &Exists, &MeshInfo, &MeshRead, &ImageInfo, &ImageRead };
        return &api;
    }
}
