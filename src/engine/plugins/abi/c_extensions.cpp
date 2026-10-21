#include <cstring>
#include <memory>

#include <shard/shard_extensions.h>

#include "engine/assets/asset_importer.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/world/engine.hpp"

// shard.extensions : the C items are wrapped in the C++ items the subsystems already read (ToolValidatorItem, AssetTypeExtension,
// AssetImporterExtension), added through the plugin's AssetRegistrar so that they leave with it.

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        // ---- Reports ----

        struct ReportTarget {
            std::vector<std::string>* lines = nullptr;
            std::string* message = nullptr;
        };

        void SHARD_CALL ReportAdd(void* host, ShardStr line)
        {
            try {
                auto* target = static_cast<ReportTarget*>(host);
                if (target && target->lines) target->lines->push_back(ToString(line));
            } catch (...) {}
        }

        void SHARD_CALL ReportMessage(void* host, ShardStr text)
        {
            try {
                auto* target = static_cast<ReportTarget*>(host);
                if (target && target->message) *target->message = ToString(text);
            } catch (...) {}
        }

        ShardReport MakeReport(ReportTarget& target)
        {
            ShardReport report;
            report.structSize = sizeof(ShardReport);
            report.host = &target;
            report.add = &ReportAdd;
            report.message = &ReportMessage;
            return report;
        }

        std::vector<std::string> ReadExtensions(const ShardStr* extensions, uint32_t count)
        {
            std::vector<std::string> out;
            for (uint32_t i = 0; i < count; ++i) out.push_back(ToString(extensions[i]));
            return out;
        }

        // ---- The items ----

        ShardResult SHARD_CALL AddValidator(ShardPluginCtx handle, const ShardValidatorItem* item)
        {
            try {
                if (!item || item->structSize < sizeof(ShardValidatorItem) || !item->validate || !item->name.size) return SHARD_E_INVALID_ARG;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                auto validator = std::make_shared<ToolValidatorItem>();
                validator->name = ToString(item->name);
                const auto fn = item->validate;
                void* user = item->user;
                std::atomic<uint32_t>* active = ActiveCounter(*ctx);
                validator->validate = [fn, user, handle, active](const std::string& projectFile) {
                    ActiveScope scope(active, PluginOwner(handle));
                    std::vector<std::string> problems;
                    ReportTarget target{ &problems, nullptr };
                    const ShardReport report = MakeReport(target);
                    const ShardResult result = fn(user, handle, ToStr(projectFile), &report);
                    if (result != SHARD_OK) problems.push_back("the check failed (" + std::to_string(result) + ")");
                    return problems;
                };
                const std::string name = validator->name;
                return ctx->Assets().Add<ToolValidatorItem>(SHARD_POINT_TOOL_VALIDATOR, name, std::move(validator)) ? SHARD_OK : SHARD_E_ALREADY_EXISTS;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL AddAssetType(ShardPluginCtx handle, const ShardAssetTypeItem* item)
        {
            try {
                if (!item || item->structSize < sizeof(ShardAssetTypeItem) || !item->load || !item->name.size) return SHARD_E_INVALID_ARG;
                if (item->extensionCount == 0 || !item->extensions) return SHARD_E_INVALID_ARG;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                auto type = std::make_shared<Core::Resources::AssetTypeExtension>();
                type->name = ToString(item->name);
                type->extensions = ReadExtensions(item->extensions, item->extensionCount);
                const auto load = item->load;
                const auto unload = item->unload;
                void* user = item->user;
                std::atomic<uint32_t>* active = ActiveCounter(*ctx);
                type->info.load = [load, unload, user, handle, active](const std::string& path, const Filesystem::AssetInfos& infos) -> std::shared_ptr<void> {
                    void* resource = nullptr;
                    {
                        ActiveScope scope(active, PluginOwner(handle));
                        resource = load(user, handle, ToStr(path), ToStr(infos.baseInfos.path.full));
                    }
                    if (!resource) return nullptr;
                    // Handed back to the plugin once the cache lets it go ; after the plugin is gone there is nobody to give it to
                    return std::shared_ptr<void>(resource, [unload, user, handle, active](void* r) {
                        ActiveScope scope(active, PluginOwner(handle));
                        if (unload && FindContext(handle)) unload(user, handle, r);
                    });
                };
                const std::string name = type->name;
                return ctx->Assets().Add<Core::Resources::AssetTypeExtension>(SHARD_POINT_ASSET_TYPE, name, std::move(type)) ? SHARD_OK : SHARD_E_ALREADY_EXISTS;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL AddImporter(ShardPluginCtx handle, const ShardImporterItem* item)
        {
            try {
                if (!item || item->structSize < sizeof(ShardImporterItem) || !item->import || !item->name.size) return SHARD_E_INVALID_ARG;
                if (item->extensionCount == 0 || !item->extensions) return SHARD_E_INVALID_ARG;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                auto importer = std::make_shared<Filesystem::AssetImporterExtension>();
                importer->name = ToString(item->name);
                importer->extensions = ReadExtensions(item->extensions, item->extensionCount);
                const auto fn = item->import;
                void* user = item->user;
                std::atomic<uint32_t>* active = ActiveCounter(*ctx);
                importer->import = [fn, user, handle, active](const std::string& source, const std::string& outputDirectory) {
                    ActiveScope scope(active, PluginOwner(handle));
                    Filesystem::ImportResult result;
                    ReportTarget target{ &result.outputs, &result.message };
                    const ShardReport report = MakeReport(target);
                    const ShardResult code = fn(user, handle, ToStr(source), ToStr(outputDirectory), &report);
                    result.ok = code == SHARD_OK;
                    if (!result.ok && result.message.empty()) result.message = "the importer failed (" + std::to_string(code) + ")";
                    return result;
                };
                const std::string name = importer->name;
                return ctx->Assets().Add<Filesystem::AssetImporterExtension>(SHARD_POINT_ASSET_IMPORTER, name, std::move(importer)) ? SHARD_OK : SHARD_E_ALREADY_EXISTS;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL RemoveItem(ShardPluginCtx handle, ShardStr point, ShardStr name)
        {
            try {
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                return ctx->Extensions().Remove(ToString(point), ToString(name), ctx->Owner()) ? SHARD_OK : SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL GetAsset(ShardPluginCtx handle, ShardStr typeName, ShardStr path, void** out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = nullptr;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                Core::IEngineContext* engine = ctx->Engine();
                Core::Resources::ResourcesManager* resources = engine ? engine->GetResourcesManager() : nullptr;
                if (!resources) return SHARD_E_UNSUPPORTED;
                const std::string wanted = ToString(typeName);
                for (const ExtensionRegistry::Item& item : ctx->Extensions().List(SHARD_POINT_ASSET_TYPE)) {
                    if (item.owner != ctx->Owner() || item.name != wanted || item.typeName != typeid(Core::Resources::AssetTypeExtension).name()) continue;
                    auto type = std::static_pointer_cast<Core::Resources::AssetTypeExtension>(item.object);
                    std::shared_ptr<void> resource = resources->Get<void>(type->kind, ToString(path));
                    if (!resource) return SHARD_E_NOT_FOUND;
                    *out = resource.get();      // the cache keeps it alive
                    return SHARD_OK;
                }
                return SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    const void* ExtensionsApi()
    {
        static const ShardExtensionsApi api = { sizeof(ShardExtensionsApi), &AddValidator, &AddAssetType, &AddImporter, &RemoveItem, &GetAsset };
        return &api;
    }
}
