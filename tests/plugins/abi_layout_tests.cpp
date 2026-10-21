#include <gtest/gtest.h>

#include <cstddef>

#include <shard/shard.h>

// Pins the layout of every struct of sdk/include/shard (but shard_experimental.h, which promises nothing). A failure here means an ABI struct changed : that needs an ABI revision
// (SHARD_ABI_MINOR, and the numbers below updated in the same change), never a quiet edit. 64 bit targets only (x86_64, arm64).
// The lines were written by a program that prints sizeof / offsetof of each field, then read : regenerate them the same way.

#if INTPTR_MAX == INT64_MAX

static_assert(sizeof(ShardStr) == 16, "ShardStr");
static_assert(offsetof(ShardStr, data) == 0, "ShardStr.data");
static_assert(offsetof(ShardStr, size) == 8, "ShardStr.size");

static_assert(sizeof(ShardCommandsApi) == 16, "ShardCommandsApi");
static_assert(offsetof(ShardCommandsApi, structSize) == 0, "ShardCommandsApi.structSize");
static_assert(offsetof(ShardCommandsApi, add) == 8, "ShardCommandsApi.add");

static_assert(sizeof(ShardFieldDesc) == 56, "ShardFieldDesc");
static_assert(offsetof(ShardFieldDesc, structSize) == 0, "ShardFieldDesc.structSize");
static_assert(offsetof(ShardFieldDesc, type) == 4, "ShardFieldDesc.type");
static_assert(offsetof(ShardFieldDesc, name) == 8, "ShardFieldDesc.name");
static_assert(offsetof(ShardFieldDesc, offset) == 24, "ShardFieldDesc.offset");
static_assert(offsetof(ShardFieldDesc, flags) == 28, "ShardFieldDesc.flags");
static_assert(offsetof(ShardFieldDesc, min) == 32, "ShardFieldDesc.min");
static_assert(offsetof(ShardFieldDesc, max) == 36, "ShardFieldDesc.max");
static_assert(offsetof(ShardFieldDesc, tooltip) == 40, "ShardFieldDesc.tooltip");

static_assert(sizeof(ShardComponentDesc) == 48, "ShardComponentDesc");
static_assert(offsetof(ShardComponentDesc, structSize) == 0, "ShardComponentDesc.structSize");
static_assert(offsetof(ShardComponentDesc, size) == 4, "ShardComponentDesc.size");
static_assert(offsetof(ShardComponentDesc, align) == 8, "ShardComponentDesc.align");
static_assert(offsetof(ShardComponentDesc, fieldCount) == 12, "ShardComponentDesc.fieldCount");
static_assert(offsetof(ShardComponentDesc, name) == 16, "ShardComponentDesc.name");
static_assert(offsetof(ShardComponentDesc, fields) == 32, "ShardComponentDesc.fields");
static_assert(offsetof(ShardComponentDesc, defaults) == 40, "ShardComponentDesc.defaults");

static_assert(sizeof(ShardComponentsApi) == 88, "ShardComponentsApi");                                        // 56 until 1.1
static_assert(offsetof(ShardComponentsApi, structSize) == 0, "ShardComponentsApi.structSize");
static_assert(offsetof(ShardComponentsApi, register_type) == 8, "ShardComponentsApi.register_type");
static_assert(offsetof(ShardComponentsApi, find_type) == 16, "ShardComponentsApi.find_type");
static_assert(offsetof(ShardComponentsApi, type_layout) == 24, "ShardComponentsApi.type_layout");
static_assert(offsetof(ShardComponentsApi, field_count) == 32, "ShardComponentsApi.field_count");
static_assert(offsetof(ShardComponentsApi, field_at) == 40, "ShardComponentsApi.field_at");
static_assert(offsetof(ShardComponentsApi, find_field) == 48, "ShardComponentsApi.find_field");
static_assert(offsetof(ShardComponentsApi, blob_create) == 56, "ShardComponentsApi.blob_create");             // 1.2
static_assert(offsetof(ShardComponentsApi, blob_data) == 64, "ShardComponentsApi.blob_data");                 // 1.2
static_assert(offsetof(ShardComponentsApi, blob_resize) == 72, "ShardComponentsApi.blob_resize");             // 1.2
static_assert(offsetof(ShardComponentsApi, blob_free) == 80, "ShardComponentsApi.blob_free");                 // 1.2

static_assert(sizeof(ShardContentApi) == 48, "ShardContentApi");
static_assert(offsetof(ShardContentApi, structSize) == 0, "ShardContentApi.structSize");
static_assert(offsetof(ShardContentApi, uri_of) == 8, "ShardContentApi.uri_of");
static_assert(offsetof(ShardContentApi, resolve) == 16, "ShardContentApi.resolve");
static_assert(offsetof(ShardContentApi, resolve_logical) == 24, "ShardContentApi.resolve_logical");
static_assert(offsetof(ShardContentApi, read) == 32, "ShardContentApi.read");
static_assert(offsetof(ShardContentApi, list) == 40, "ShardContentApi.list");

static_assert(sizeof(ShardCVarsApi) == 104, "ShardCVarsApi");
static_assert(offsetof(ShardCVarsApi, structSize) == 0, "ShardCVarsApi.structSize");
static_assert(offsetof(ShardCVarsApi, create_bool) == 8, "ShardCVarsApi.create_bool");
static_assert(offsetof(ShardCVarsApi, create_int) == 16, "ShardCVarsApi.create_int");
static_assert(offsetof(ShardCVarsApi, create_float) == 24, "ShardCVarsApi.create_float");
static_assert(offsetof(ShardCVarsApi, create_string) == 32, "ShardCVarsApi.create_string");
static_assert(offsetof(ShardCVarsApi, get_bool) == 40, "ShardCVarsApi.get_bool");
static_assert(offsetof(ShardCVarsApi, get_int) == 48, "ShardCVarsApi.get_int");
static_assert(offsetof(ShardCVarsApi, get_float) == 56, "ShardCVarsApi.get_float");
static_assert(offsetof(ShardCVarsApi, get_string) == 64, "ShardCVarsApi.get_string");
static_assert(offsetof(ShardCVarsApi, set_bool) == 72, "ShardCVarsApi.set_bool");
static_assert(offsetof(ShardCVarsApi, set_int) == 80, "ShardCVarsApi.set_int");
static_assert(offsetof(ShardCVarsApi, set_float) == 88, "ShardCVarsApi.set_float");
static_assert(offsetof(ShardCVarsApi, set_string) == 96, "ShardCVarsApi.set_string");

static_assert(sizeof(ShardQuery) == 32, "ShardQuery");
static_assert(offsetof(ShardQuery, structSize) == 0, "ShardQuery.structSize");
static_assert(offsetof(ShardQuery, withCount) == 4, "ShardQuery.withCount");
static_assert(offsetof(ShardQuery, withoutCount) == 8, "ShardQuery.withoutCount");
static_assert(offsetof(ShardQuery, with) == 16, "ShardQuery.with");
static_assert(offsetof(ShardQuery, without) == 24, "ShardQuery.without");

static_assert(sizeof(ShardChunk) == 24, "ShardChunk");
static_assert(offsetof(ShardChunk, structSize) == 0, "ShardChunk.structSize");
static_assert(offsetof(ShardChunk, count) == 4, "ShardChunk.count");
static_assert(offsetof(ShardChunk, entities) == 8, "ShardChunk.entities");
static_assert(offsetof(ShardChunk, columns) == 16, "ShardChunk.columns");

static_assert(sizeof(ShardEcsApi) == 88, "ShardEcsApi");
static_assert(offsetof(ShardEcsApi, structSize) == 0, "ShardEcsApi.structSize");
static_assert(offsetof(ShardEcsApi, each_chunk) == 8, "ShardEcsApi.each_chunk");
static_assert(offsetof(ShardEcsApi, count) == 16, "ShardEcsApi.count");
static_assert(offsetof(ShardEcsApi, is_alive) == 24, "ShardEcsApi.is_alive");
static_assert(offsetof(ShardEcsApi, get) == 32, "ShardEcsApi.get");
static_assert(offsetof(ShardEcsApi, parent) == 40, "ShardEcsApi.parent");
static_assert(offsetof(ShardEcsApi, spawn) == 48, "ShardEcsApi.spawn");
static_assert(offsetof(ShardEcsApi, destroy) == 56, "ShardEcsApi.destroy");
static_assert(offsetof(ShardEcsApi, add) == 64, "ShardEcsApi.add");
static_assert(offsetof(ShardEcsApi, remove) == 72, "ShardEcsApi.remove");
static_assert(offsetof(ShardEcsApi, set_parent) == 80, "ShardEcsApi.set_parent");

static_assert(sizeof(ShardLocalTransform) == 40, "ShardLocalTransform");
static_assert(offsetof(ShardLocalTransform, position) == 0, "ShardLocalTransform.position");
static_assert(offsetof(ShardLocalTransform, rotation) == 12, "ShardLocalTransform.rotation");
static_assert(offsetof(ShardLocalTransform, scale) == 28, "ShardLocalTransform.scale");

static_assert(sizeof(ShardActorLink) == 4, "ShardActorLink");
static_assert(offsetof(ShardActorLink, objectId) == 0, "ShardActorLink.objectId");

static_assert(sizeof(ShardEventsApi) == 56, "ShardEventsApi");                                                // 48 until 1.1
static_assert(offsetof(ShardEventsApi, structSize) == 0, "ShardEventsApi.structSize");
static_assert(offsetof(ShardEventsApi, subscribe) == 8, "ShardEventsApi.subscribe");
static_assert(offsetof(ShardEventsApi, subscribe_world) == 16, "ShardEventsApi.subscribe_world");
static_assert(offsetof(ShardEventsApi, unsubscribe) == 24, "ShardEventsApi.unsubscribe");
static_assert(offsetof(ShardEventsApi, emit) == 32, "ShardEventsApi.emit");
static_assert(offsetof(ShardEventsApi, emit_world) == 40, "ShardEventsApi.emit_world");
static_assert(offsetof(ShardEventsApi, describe) == 48, "ShardEventsApi.describe");                           // 1.2

static_assert(sizeof(ShardReport) == 32, "ShardReport");
static_assert(offsetof(ShardReport, structSize) == 0, "ShardReport.structSize");
static_assert(offsetof(ShardReport, host) == 8, "ShardReport.host");
static_assert(offsetof(ShardReport, add) == 16, "ShardReport.add");
static_assert(offsetof(ShardReport, message) == 24, "ShardReport.message");

static_assert(sizeof(ShardValidatorItem) == 40, "ShardValidatorItem");
static_assert(offsetof(ShardValidatorItem, structSize) == 0, "ShardValidatorItem.structSize");
static_assert(offsetof(ShardValidatorItem, name) == 8, "ShardValidatorItem.name");
static_assert(offsetof(ShardValidatorItem, validate) == 24, "ShardValidatorItem.validate");
static_assert(offsetof(ShardValidatorItem, user) == 32, "ShardValidatorItem.user");

static_assert(sizeof(ShardAssetTypeItem) == 56, "ShardAssetTypeItem");
static_assert(offsetof(ShardAssetTypeItem, structSize) == 0, "ShardAssetTypeItem.structSize");
static_assert(offsetof(ShardAssetTypeItem, extensionCount) == 4, "ShardAssetTypeItem.extensionCount");
static_assert(offsetof(ShardAssetTypeItem, name) == 8, "ShardAssetTypeItem.name");
static_assert(offsetof(ShardAssetTypeItem, extensions) == 24, "ShardAssetTypeItem.extensions");
static_assert(offsetof(ShardAssetTypeItem, load) == 32, "ShardAssetTypeItem.load");
static_assert(offsetof(ShardAssetTypeItem, unload) == 40, "ShardAssetTypeItem.unload");
static_assert(offsetof(ShardAssetTypeItem, user) == 48, "ShardAssetTypeItem.user");

static_assert(sizeof(ShardImporterItem) == 48, "ShardImporterItem");
static_assert(offsetof(ShardImporterItem, structSize) == 0, "ShardImporterItem.structSize");
static_assert(offsetof(ShardImporterItem, extensionCount) == 4, "ShardImporterItem.extensionCount");
static_assert(offsetof(ShardImporterItem, name) == 8, "ShardImporterItem.name");
static_assert(offsetof(ShardImporterItem, extensions) == 24, "ShardImporterItem.extensions");
static_assert(offsetof(ShardImporterItem, import) == 32, "ShardImporterItem.import");
static_assert(offsetof(ShardImporterItem, user) == 40, "ShardImporterItem.user");

static_assert(sizeof(ShardExtensionsApi) == 48, "ShardExtensionsApi");
static_assert(offsetof(ShardExtensionsApi, structSize) == 0, "ShardExtensionsApi.structSize");
static_assert(offsetof(ShardExtensionsApi, add_validator) == 8, "ShardExtensionsApi.add_validator");
static_assert(offsetof(ShardExtensionsApi, add_asset_type) == 16, "ShardExtensionsApi.add_asset_type");
static_assert(offsetof(ShardExtensionsApi, add_importer) == 24, "ShardExtensionsApi.add_importer");
static_assert(offsetof(ShardExtensionsApi, remove) == 32, "ShardExtensionsApi.remove");
static_assert(offsetof(ShardExtensionsApi, get_asset) == 40, "ShardExtensionsApi.get_asset");

static_assert(sizeof(ShardHookArgs) == 80, "ShardHookArgs");
static_assert(offsetof(ShardHookArgs, structSize) == 0, "ShardHookArgs.structSize");
static_assert(offsetof(ShardHookArgs, cancelled) == 4, "ShardHookArgs.cancelled");
static_assert(offsetof(ShardHookArgs, reserved) == 5, "ShardHookArgs.reserved");
static_assert(offsetof(ShardHookArgs, name) == 8, "ShardHookArgs.name");
static_assert(offsetof(ShardHookArgs, subject) == 24, "ShardHookArgs.subject");
static_assert(offsetof(ShardHookArgs, detail) == 40, "ShardHookArgs.detail");
static_assert(offsetof(ShardHookArgs, frame) == 56, "ShardHookArgs.frame");
static_assert(offsetof(ShardHookArgs, reason) == 64, "ShardHookArgs.reason");

static_assert(sizeof(ShardHooksApi) == 24, "ShardHooksApi");
static_assert(offsetof(ShardHooksApi, structSize) == 0, "ShardHooksApi.structSize");
static_assert(offsetof(ShardHooksApi, on) == 8, "ShardHooksApi.on");
static_assert(offsetof(ShardHooksApi, off) == 16, "ShardHooksApi.off");

static_assert(sizeof(ShardHostApi) == 64, "ShardHostApi");
static_assert(offsetof(ShardHostApi, structSize) == 0, "ShardHostApi.structSize");
static_assert(offsetof(ShardHostApi, abiMajor) == 4, "ShardHostApi.abiMajor");
static_assert(offsetof(ShardHostApi, abiMinor) == 8, "ShardHostApi.abiMinor");
static_assert(offsetof(ShardHostApi, engineVersion) == 16, "ShardHostApi.engineVersion");
static_assert(offsetof(ShardHostApi, log) == 32, "ShardHostApi.log");
static_assert(offsetof(ShardHostApi, alloc) == 40, "ShardHostApi.alloc");
static_assert(offsetof(ShardHostApi, free) == 48, "ShardHostApi.free");
static_assert(offsetof(ShardHostApi, query_interface) == 56, "ShardHostApi.query_interface");

static_assert(sizeof(ShardWorldInfo) == 24, "ShardWorldInfo");
static_assert(offsetof(ShardWorldInfo, structSize) == 0, "ShardWorldInfo.structSize");
static_assert(offsetof(ShardWorldInfo, assetId) == 4, "ShardWorldInfo.assetId");
static_assert(offsetof(ShardWorldInfo, name) == 8, "ShardWorldInfo.name");

static_assert(sizeof(ShardPluginCallbacks) == 128, "ShardPluginCallbacks");
static_assert(offsetof(ShardPluginCallbacks, structSize) == 0, "ShardPluginCallbacks.structSize");
static_assert(offsetof(ShardPluginCallbacks, on_load) == 8, "ShardPluginCallbacks.on_load");
static_assert(offsetof(ShardPluginCallbacks, on_initialize) == 16, "ShardPluginCallbacks.on_initialize");
static_assert(offsetof(ShardPluginCallbacks, on_enable) == 24, "ShardPluginCallbacks.on_enable");
static_assert(offsetof(ShardPluginCallbacks, on_disable) == 32, "ShardPluginCallbacks.on_disable");
static_assert(offsetof(ShardPluginCallbacks, on_unload) == 40, "ShardPluginCallbacks.on_unload");
static_assert(offsetof(ShardPluginCallbacks, on_prepare_reload) == 48, "ShardPluginCallbacks.on_prepare_reload");
static_assert(offsetof(ShardPluginCallbacks, on_restore) == 56, "ShardPluginCallbacks.on_restore");
static_assert(offsetof(ShardPluginCallbacks, on_frame_begin) == 64, "ShardPluginCallbacks.on_frame_begin");
static_assert(offsetof(ShardPluginCallbacks, on_frame_end) == 72, "ShardPluginCallbacks.on_frame_end");
static_assert(offsetof(ShardPluginCallbacks, on_content_mounted) == 80, "ShardPluginCallbacks.on_content_mounted");
static_assert(offsetof(ShardPluginCallbacks, on_world_loaded) == 88, "ShardPluginCallbacks.on_world_loaded");
static_assert(offsetof(ShardPluginCallbacks, on_world_unloaded) == 96, "ShardPluginCallbacks.on_world_unloaded");
static_assert(offsetof(ShardPluginCallbacks, on_play_start) == 104, "ShardPluginCallbacks.on_play_start");
static_assert(offsetof(ShardPluginCallbacks, on_play_stop) == 112, "ShardPluginCallbacks.on_play_stop");
static_assert(offsetof(ShardPluginCallbacks, on_tool_task) == 120, "ShardPluginCallbacks.on_tool_task");

static_assert(sizeof(ShardPluginInfo) == 48, "ShardPluginInfo");
static_assert(offsetof(ShardPluginInfo, structSize) == 0, "ShardPluginInfo.structSize");
static_assert(offsetof(ShardPluginInfo, abiMajor) == 4, "ShardPluginInfo.abiMajor");
static_assert(offsetof(ShardPluginInfo, abiMinor) == 8, "ShardPluginInfo.abiMinor");
static_assert(offsetof(ShardPluginInfo, id) == 16, "ShardPluginInfo.id");
static_assert(offsetof(ShardPluginInfo, user) == 32, "ShardPluginInfo.user");
static_assert(offsetof(ShardPluginInfo, callbacks) == 40, "ShardPluginInfo.callbacks");

static_assert(sizeof(ShardPluginApi) == 48, "ShardPluginApi");                                                // 32 in 1.0
static_assert(offsetof(ShardPluginApi, structSize) == 0, "ShardPluginApi.structSize");
static_assert(offsetof(ShardPluginApi, id) == 8, "ShardPluginApi.id");
static_assert(offsetof(ShardPluginApi, version) == 16, "ShardPluginApi.version");
static_assert(offsetof(ShardPluginApi, directory) == 24, "ShardPluginApi.directory");
static_assert(offsetof(ShardPluginApi, host) == 32, "ShardPluginApi.host");                                   // 1.1
static_assert(offsetof(ShardPluginApi, has_permission) == 40, "ShardPluginApi.has_permission");               // 1.1

static_assert(sizeof(ShardService) == 32, "ShardService");
static_assert(offsetof(ShardService, structSize) == 0, "ShardService.structSize");
static_assert(offsetof(ShardService, major) == 4, "ShardService.major");
static_assert(offsetof(ShardService, minor) == 8, "ShardService.minor");
static_assert(offsetof(ShardService, table) == 16, "ShardService.table");
static_assert(offsetof(ShardService, self) == 24, "ShardService.self");

static_assert(sizeof(ShardServicesApi) == 40, "ShardServicesApi");
static_assert(offsetof(ShardServicesApi, structSize) == 0, "ShardServicesApi.structSize");
static_assert(offsetof(ShardServicesApi, provide) == 8, "ShardServicesApi.provide");
static_assert(offsetof(ShardServicesApi, get) == 16, "ShardServicesApi.get");
static_assert(offsetof(ShardServicesApi, resolve) == 24, "ShardServicesApi.resolve");
static_assert(offsetof(ShardServicesApi, release) == 32, "ShardServicesApi.release");

static_assert(sizeof(ShardStateApi) == 32, "ShardStateApi");
static_assert(offsetof(ShardStateApi, structSize) == 0, "ShardStateApi.structSize");
static_assert(offsetof(ShardStateApi, set) == 8, "ShardStateApi.set");
static_assert(offsetof(ShardStateApi, get) == 16, "ShardStateApi.get");
static_assert(offsetof(ShardStateApi, has) == 24, "ShardStateApi.has");

static_assert(sizeof(ShardSystemCtx) == 40, "ShardSystemCtx");
static_assert(offsetof(ShardSystemCtx, structSize) == 0, "ShardSystemCtx.structSize");
static_assert(offsetof(ShardSystemCtx, deltaTime) == 4, "ShardSystemCtx.deltaTime");
static_assert(offsetof(ShardSystemCtx, fixedDeltaTime) == 8, "ShardSystemCtx.fixedDeltaTime");
static_assert(offsetof(ShardSystemCtx, fixedStep) == 12, "ShardSystemCtx.fixedStep");
static_assert(offsetof(ShardSystemCtx, fixedStepCount) == 16, "ShardSystemCtx.fixedStepCount");
static_assert(offsetof(ShardSystemCtx, frame) == 24, "ShardSystemCtx.frame");
static_assert(offsetof(ShardSystemCtx, ecs) == 32, "ShardSystemCtx.ecs");

static_assert(sizeof(ShardSystemDesc) == 96, "ShardSystemDesc");
static_assert(offsetof(ShardSystemDesc, structSize) == 0, "ShardSystemDesc.structSize");
static_assert(offsetof(ShardSystemDesc, phase) == 4, "ShardSystemDesc.phase");
static_assert(offsetof(ShardSystemDesc, flags) == 8, "ShardSystemDesc.flags");
static_assert(offsetof(ShardSystemDesc, readCount) == 12, "ShardSystemDesc.readCount");
static_assert(offsetof(ShardSystemDesc, writeCount) == 16, "ShardSystemDesc.writeCount");
static_assert(offsetof(ShardSystemDesc, afterCount) == 20, "ShardSystemDesc.afterCount");
static_assert(offsetof(ShardSystemDesc, beforeCount) == 24, "ShardSystemDesc.beforeCount");
static_assert(offsetof(ShardSystemDesc, name) == 32, "ShardSystemDesc.name");
static_assert(offsetof(ShardSystemDesc, reads) == 48, "ShardSystemDesc.reads");
static_assert(offsetof(ShardSystemDesc, writes) == 56, "ShardSystemDesc.writes");
static_assert(offsetof(ShardSystemDesc, after) == 64, "ShardSystemDesc.after");
static_assert(offsetof(ShardSystemDesc, before) == 72, "ShardSystemDesc.before");
static_assert(offsetof(ShardSystemDesc, run) == 80, "ShardSystemDesc.run");
static_assert(offsetof(ShardSystemDesc, user) == 88, "ShardSystemDesc.user");

static_assert(sizeof(ShardSystemsApi) == 32, "ShardSystemsApi");
static_assert(offsetof(ShardSystemsApi, structSize) == 0, "ShardSystemsApi.structSize");
static_assert(offsetof(ShardSystemsApi, add) == 8, "ShardSystemsApi.add");
static_assert(offsetof(ShardSystemsApi, set_enabled) == 16, "ShardSystemsApi.set_enabled");
static_assert(offsetof(ShardSystemsApi, remove) == 24, "ShardSystemsApi.remove");

#endif

static_assert(sizeof(ShardResult) == 4 && sizeof(ShardPluginCtx) == 8 && sizeof(ShardCVar) == 8 && SHARD_CVAR_READONLY == 1u, "handles and results have a fixed size");
static_assert(sizeof(ShardComponentType) == 8 && sizeof(ShardEntity) == 8 && sizeof(ShardEcs) == 8 && sizeof(ShardState) == 8, "handles have a fixed size");
static_assert(sizeof(ShardSubscription) == 8 && sizeof(ShardHookToken) == 8 && sizeof(ShardServiceRef) == 8, "handles have a fixed size");
static_assert(SHARD_OK == 0 && SHARD_E_INVALID_ARG < 0 && SHARD_E_INTERNAL < SHARD_E_INVALID_ARG, "errors are negative");

// The values plugins compile in : they never change inside a major revision
static_assert(SHARD_E_INVALID_ARG == -1 && SHARD_E_NOT_FOUND == -2 && SHARD_E_ALREADY_EXISTS == -3 && SHARD_E_WRONG_THREAD == -4, "result codes");
static_assert(SHARD_E_STALE_HANDLE == -5 && SHARD_E_UNSUPPORTED == -6 && SHARD_E_OUT_OF_MEMORY == -7 && SHARD_E_INTERNAL == -8, "result codes");
static_assert(SHARD_FIELD_F32 == 1 && SHARD_FIELD_F64 == 2 && SHARD_FIELD_I32 == 3 && SHARD_FIELD_U32 == 4 && SHARD_FIELD_I64 == 5 && SHARD_FIELD_U64 == 6, "field types");
static_assert(SHARD_FIELD_BOOL == 7 && SHARD_FIELD_VEC2 == 8 && SHARD_FIELD_VEC3 == 9 && SHARD_FIELD_VEC4 == 10 && SHARD_FIELD_QUAT == 11 && SHARD_FIELD_COLOR == 12, "field types");
static_assert(SHARD_FIELD_BLOB == 13 && sizeof(ShardBlob) == 8, "blobs (1.2)");
static_assert(SHARD_FIELD_RUNTIME == 1 && SHARD_FIELD_PRIVATE == 2 && SHARD_COMPONENT_MAX_SIZE == 1024 && SHARD_COMPONENT_MAX_FIELDS == 64, "field flags and limits");
static_assert(SHARD_PHASE_INPUT == 0 && SHARD_PHASE_PRE_SIM == 1 && SHARD_PHASE_FIXED == 2 && SHARD_PHASE_POST_PHYSICS == 3, "phases");
static_assert(SHARD_PHASE_UPDATE == 4 && SHARD_PHASE_LATE == 5 && SHARD_PHASE_EXTRACT == 6, "phases");
static_assert(SHARD_SYSTEM_EXCLUSIVE == 1 && SHARD_SYSTEM_MAIN_THREAD == 2, "system flags");
static_assert(SHARD_PRIORITY_EARLIEST == -200 && SHARD_PRIORITY_EARLY == -100 && SHARD_PRIORITY_NORMAL == 0 && SHARD_PRIORITY_LATE == 100 && SHARD_PRIORITY_LATEST == 200, "priorities");
static_assert(SHARD_LOG_INFO == 0 && SHARD_LOG_WARNING == 1 && SHARD_LOG_ERROR == 2, "log levels");
static_assert(SHARD_ABI_MAJOR == 1 && SHARD_ABI_MINOR == 2, "the ABI revision of these headers");

TEST(AbiLayout, HeadersCompileAsCppAndPinTheirLayout)
{
    // The assertions above are the test : this case makes sure the file is part of the run
    EXPECT_EQ(sizeof(ShardStr), 16u);
}
