// Shard C++ SDK : the content folders (shard_content.h), the reload state (shard_state.h) and the extension points (shard_extensions.h).
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <shard/shard_content.h>
#include <shard/shard_extensions.h>
#include <shard/shard_state.h>
#include <shard/cpp/detail.hpp>

namespace Shard {

    class Content {
    public:
        explicit Content(ShardPluginCtx ctx) : m_Ctx(ctx) {}

        /// "textures/a.png" -> "mod://<this plugin's id>/textures/a.png"
        std::string UriOf(std::string_view path) const
        {
            std::string out;
            ReadText([&](char* b, uint64_t c, uint64_t* s) { return Api().uri_of(m_Ctx, Str(path), b, c, s); }, out);
            return out;
        }
        /// The absolute file of a mod:// address, if there is one
        std::optional<std::string> Resolve(std::string_view uri) const
        {
            std::string out;
            if (ReadText([&](char* b, uint64_t c, uint64_t* s) { return Api().resolve(m_Ctx, Str(uri), b, c, s); }, out) != SHARD_OK) return std::nullopt;
            return out;
        }
        /// The mod:// address that wins for a logical path across the loaded modules
        std::optional<std::string> ResolveLogical(std::string_view logicalPath) const
        {
            std::string out;
            if (ReadText([&](char* b, uint64_t c, uint64_t* s) { return Api().resolve_logical(m_Ctx, Str(logicalPath), b, c, s); }, out) != SHARD_OK) return std::nullopt;
            return out;
        }
        /// The bytes of a file (a mod:// address)
        std::optional<std::string> Read(std::string_view uri) const
        {
            void* data = nullptr;
            uint64_t size = 0;
            if (Api().read(m_Ctx, Str(uri), &data, &size) != SHARD_OK) return std::nullopt;
            std::string bytes(static_cast<const char*>(data), size_t(size));
            detail::Host()->free(m_Ctx, data);
            return bytes;
        }
        /// The files under `subdir` of a module ("" : this plugin), sorted
        std::vector<std::string> List(std::string_view pluginId = {}, std::string_view subdir = {}) const
        {
            std::vector<std::string> files;
            Api().list(m_Ctx, Str(pluginId), Str(subdir), [](void* user, ShardStr path) -> ShardResult {
                static_cast<std::vector<std::string>*>(user)->push_back(ToString(path));
                return SHARD_OK;
            }, &files);
            return files;
        }

    private:
        static const ShardContentApi& Api() { return detail::Require<ShardContentApi>(SHARD_IFACE_CONTENT); }
        ShardPluginCtx m_Ctx;
    };

    /// What OnPrepareReload writes : values as JSON text (`42`, `"text"`, `{"a": 1}`)
    class StateWriter {
    public:
        explicit StateWriter(ShardState state) : m_State(state) {}
        bool Set(std::string_view key, std::string_view json) const { return Api().set(m_State, Str(key), Str(json)) == SHARD_OK; }
    private:
        static const ShardStateApi& Api() { return detail::Require<ShardStateApi>(SHARD_IFACE_STATE); }
        ShardState m_State;
    };

    /// What OnRestore reads back
    class StateReader {
    public:
        explicit StateReader(ShardState state) : m_State(state) {}
        bool Has(std::string_view key) const { return Api().has(m_State, Str(key)) == SHARD_TRUE; }
        /// The value as JSON text
        std::optional<std::string> Get(std::string_view key) const
        {
            std::string out;
            if (ReadText([&](char* b, uint64_t c, uint64_t* s) { return Api().get(m_State, Str(key), b, c, s); }, out) != SHARD_OK) return std::nullopt;
            return out;
        }
    private:
        static const ShardStateApi& Api() { return detail::Require<ShardStateApi>(SHARD_IFACE_STATE); }
        ShardState m_State;
    };

    /// Problems found by a validator, files written by an importer
    class Report {
    public:
        explicit Report(const ShardReport& report) : m_Report(report) {}
        void Add(std::string_view line) const { m_Report.add(m_Report.host, Str(line)); }
        void Message(std::string_view text) const { m_Report.message(m_Report.host, Str(text)); }
    private:
        const ShardReport& m_Report;
    };

    using ValidatorFn = std::function<void(std::string_view projectFile, Report& report)>;
    using ImporterFn = std::function<bool(std::string_view source, std::string_view outputDirectory, Report& report)>;
    /// An asset type's loader : the resource (yours, opaque to the engine) or null ; `unload` gets it back
    using AssetLoadFn = std::function<void*(std::string_view path, std::string_view file)>;
    using AssetUnloadFn = std::function<void(void* resource)>;

    class Extensions {
    public:
        explicit Extensions(ShardPluginCtx ctx) : m_Ctx(ctx) {}

        void AddValidator(const std::string& name, ValidatorFn fn) const
        {
            ShardValidatorItem item{};
            item.structSize = sizeof(item);
            item.name = Str(name);
            item.validate = [](void* user, ShardPluginCtx ctx, ShardStr projectFile, const ShardReport* report) -> ShardResult {
                Report r(*report);
                return detail::Guard(ctx, "a validator", [&] { (*static_cast<ValidatorFn*>(user))(View(projectFile), r); });
            };
            item.user = detail::Keep(std::move(fn));
            detail::Check(Api().add_validator(m_Ctx, &item), "validator '" + name + "'");
        }

        void AddImporter(const std::string& name, const std::vector<std::string>& extensions, ImporterFn fn) const
        {
            std::vector<ShardStr> exts;
            for (const std::string& e : extensions) exts.push_back(Str(e));
            ShardImporterItem item{};
            item.structSize = sizeof(item);
            item.name = Str(name);
            item.extensions = exts.data();
            item.extensionCount = uint32_t(exts.size());
            item.import = [](void* user, ShardPluginCtx ctx, ShardStr source, ShardStr out, const ShardReport* report) -> ShardResult {
                Report r(*report);
                bool ok = false;
                const ShardResult result = detail::Guard(ctx, "an importer", [&] { ok = (*static_cast<ImporterFn*>(user))(View(source), View(out), r); });
                return result != SHARD_OK ? result : ok ? SHARD_OK : SHARD_E_INTERNAL;
            };
            item.user = detail::Keep(std::move(fn));
            detail::Check(Api().add_importer(m_Ctx, &item), "importer '" + name + "'");
        }

        void AddAssetType(const std::string& name, const std::vector<std::string>& extensions, AssetLoadFn load, AssetUnloadFn unload) const
        {
            std::vector<ShardStr> exts;
            for (const std::string& e : extensions) exts.push_back(Str(e));
            struct Pair { AssetLoadFn load; AssetUnloadFn unload; };
            ShardAssetTypeItem item{};
            item.structSize = sizeof(item);
            item.name = Str(name);
            item.extensions = exts.data();
            item.extensionCount = uint32_t(exts.size());
            item.load = [](void* user, ShardPluginCtx ctx, ShardStr path, ShardStr file) -> void* {
                void* resource = nullptr;
                detail::Guard(ctx, "an asset loader", [&] { resource = static_cast<Pair*>(user)->load(View(path), View(file)); });
                return resource;
            };
            item.unload = [](void* user, ShardPluginCtx ctx, void* resource) {
                detail::Guard(ctx, "an asset unloader", [&] { if (static_cast<Pair*>(user)->unload) static_cast<Pair*>(user)->unload(resource); });
            };
            item.user = detail::Keep(Pair{ std::move(load), std::move(unload) });
            detail::Check(Api().add_asset_type(m_Ctx, &item), "asset type '" + name + "'");
        }

        bool Remove(const std::string& point, const std::string& name) const { return Api().remove(m_Ctx, Str(point), Str(name)) == SHARD_OK; }

        /// The resource of one of this plugin's asset types at `path`, loaded if needed (null if it cannot be)
        void* GetAsset(const std::string& typeName, const std::string& path) const
        {
            void* out = nullptr;
            return Api().get_asset(m_Ctx, Str(typeName), Str(path), &out) == SHARD_OK ? out : nullptr;
        }

    private:
        static const ShardExtensionsApi& Api() { return detail::Require<ShardExtensionsApi>(SHARD_IFACE_EXTENSIONS); }
        ShardPluginCtx m_Ctx;
    };
}
