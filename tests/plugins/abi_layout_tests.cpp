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


static_assert(sizeof(ShardFieldDesc) == 56, "ShardFieldDesc");
static_assert(offsetof(ShardFieldDesc, structSize) == 0, "ShardFieldDesc.structSize");
static_assert(offsetof(ShardFieldDesc, type) == 4, "ShardFieldDesc.type");
static_assert(offsetof(ShardFieldDesc, name) == 8, "ShardFieldDesc.name");
static_assert(offsetof(ShardFieldDesc, offset) == 24, "ShardFieldDesc.offset");
static_assert(offsetof(ShardFieldDesc, flags) == 28, "ShardFieldDesc.flags");
static_assert(offsetof(ShardFieldDesc, min) == 32, "ShardFieldDesc.min");
static_assert(offsetof(ShardFieldDesc, max) == 36, "ShardFieldDesc.max");
static_assert(offsetof(ShardFieldDesc, tooltip) == 40, "ShardFieldDesc.tooltip");


static_assert(sizeof(ShardComponentsApi) == 104, "ShardComponentsApi");                                       // 88 until 1.2, 56 until 1.1
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
static_assert(offsetof(ShardComponentsApi, string_set) == 88, "ShardComponentsApi.string_set");               // 1.3
static_assert(offsetof(ShardComponentsApi, string_get) == 96, "ShardComponentsApi.string_get");               // 1.3

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

static_assert(sizeof(ShardEcsApi) == 96, "ShardEcsApi");                                                      // 88 until 1.2
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
static_assert(offsetof(ShardEcsApi, set_local_transform) == 88, "ShardEcsApi.set_local_transform");           // 1.3

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

static_assert(sizeof(ShardPluginApi) == 56, "ShardPluginApi");                                                // 32 in 1.0, 48 in 1.1
static_assert(offsetof(ShardPluginApi, structSize) == 0, "ShardPluginApi.structSize");
static_assert(offsetof(ShardPluginApi, id) == 8, "ShardPluginApi.id");
static_assert(offsetof(ShardPluginApi, version) == 16, "ShardPluginApi.version");
static_assert(offsetof(ShardPluginApi, directory) == 24, "ShardPluginApi.directory");
static_assert(offsetof(ShardPluginApi, host) == 32, "ShardPluginApi.host");                                   // 1.1
static_assert(offsetof(ShardPluginApi, has_permission) == 40, "ShardPluginApi.has_permission");               // 1.1
static_assert(offsetof(ShardPluginApi, random_stream) == 48, "ShardPluginApi.random_stream");                 // 1.2 (ABI 1.5)
static_assert(sizeof(ShardRandomState) == 16, "ShardRandomState");
static_assert(offsetof(ShardRandomState, state) == 0 && offsetof(ShardRandomState, increment) == 8, "ShardRandomState fields");

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
// ABI 1.4 : actors, scripts, input

static_assert(sizeof(ShardContact) == 48, "ShardContact");
static_assert(offsetof(ShardContact, other) == 8, "ShardContact.other");
static_assert(offsetof(ShardContact, point) == 16, "ShardContact.point");
static_assert(offsetof(ShardContact, normal) == 28, "ShardContact.normal");
static_assert(offsetof(ShardContact, penetration) == 40, "ShardContact.penetration");

// ABI 1.5 : physics, audio, worlds, the editor
static_assert(sizeof(ShardAudioApi) == 40, "ShardAudioApi");
static_assert(offsetof(ShardAudioApi, play_one_shot) == 32, "ShardAudioApi.play_one_shot");
static_assert(sizeof(ShardGizmoApi) == 64 && offsetof(ShardGizmoApi, text) == 56, "ShardGizmoApi");
static_assert(SHARD_FIELD_ACTOR == 16 && SHARD_FIELD_ACCESSOR == 4, "actor references and accessor strings (1.5)");
static_assert(SHARD_LAYER_STATIC == 1 && SHARD_LAYER_MOVING == 2 && SHARD_LAYER_ALL == 3, "ray layers");
static_assert(sizeof(ShardScriptsApi) == 32, "ShardScriptsApi");
static_assert(offsetof(ShardScriptsApi, register_script) == 8, "ShardScriptsApi.register_script");
static_assert(offsetof(ShardScriptsApi, add_to_actor) == 24, "ShardScriptsApi.add_to_actor");
// ---- changed in 1.7 ----
static_assert(sizeof(ShardCommandsApi) == 24, "ShardCommandsApi");
static_assert(offsetof(ShardCommandsApi, add) == 8, "ShardCommandsApi.add");
static_assert(offsetof(ShardCommandsApi, execute) == 16, "ShardCommandsApi.execute");
static_assert(sizeof(ShardComponentCallbacks) == 40, "ShardComponentCallbacks");
static_assert(offsetof(ShardComponentCallbacks, on_added) == 8, "ShardComponentCallbacks.on_added");
static_assert(offsetof(ShardComponentCallbacks, on_removed) == 16, "ShardComponentCallbacks.on_removed");
static_assert(offsetof(ShardComponentCallbacks, on_changed) == 24, "ShardComponentCallbacks.on_changed");
static_assert(offsetof(ShardComponentCallbacks, user) == 32, "ShardComponentCallbacks.user");
static_assert(sizeof(ShardComponentDesc) == 56, "ShardComponentDesc");
static_assert(offsetof(ShardComponentDesc, defaults) == 40, "ShardComponentDesc.defaults");
static_assert(offsetof(ShardComponentDesc, callbacks) == 48, "ShardComponentDesc.callbacks");
static_assert(sizeof(ShardActorsApi) == 288, "ShardActorsApi");
static_assert(offsetof(ShardActorsApi, erase_component_field) == 232, "ShardActorsApi.erase_component_field");
static_assert(offsetof(ShardActorsApi, get_stable_id) == 240, "ShardActorsApi.get_stable_id");
static_assert(offsetof(ShardActorsApi, find_by_stable_id) == 248, "ShardActorsApi.find_by_stable_id");
static_assert(offsetof(ShardActorsApi, get_world_id) == 256, "ShardActorsApi.get_world_id");
static_assert(offsetof(ShardActorsApi, get_world_bounds) == 264, "ShardActorsApi.get_world_bounds");
static_assert(offsetof(ShardActorsApi, find_in_sphere) == 272, "ShardActorsApi.find_in_sphere");
static_assert(offsetof(ShardActorsApi, find_in_box) == 280, "ShardActorsApi.find_in_box");
static_assert(sizeof(ShardScriptCallbacks) == 176, "ShardScriptCallbacks");
static_assert(offsetof(ShardScriptCallbacks, text_set) == 152, "ShardScriptCallbacks.text_set");
static_assert(offsetof(ShardScriptCallbacks, on_edit_tick) == 160, "ShardScriptCallbacks.on_edit_tick");
static_assert(offsetof(ShardScriptCallbacks, on_field_changed) == 168, "ShardScriptCallbacks.on_field_changed");
static_assert(sizeof(ShardScriptDesc) == 64, "ShardScriptDesc");
static_assert(offsetof(ShardScriptDesc, callbacks) == 48, "ShardScriptDesc.callbacks");
static_assert(offsetof(ShardScriptDesc, flags) == 56, "ShardScriptDesc.flags");
static_assert(sizeof(ShardRayHit) == 72, "ShardRayHit");
static_assert(offsetof(ShardRayHit, distance) == 40, "ShardRayHit.distance");
static_assert(offsetof(ShardRayHit, collider) == 48, "ShardRayHit.collider");
static_assert(offsetof(ShardRayHit, userData) == 56, "ShardRayHit.userData");
static_assert(offsetof(ShardRayHit, triangle) == 64, "ShardRayHit.triangle");
static_assert(offsetof(ShardRayHit, box) == 68, "ShardRayHit.box");
static_assert(sizeof(ShardQueryFilter) == 40, "ShardQueryFilter");
static_assert(offsetof(ShardQueryFilter, layers) == 4, "ShardQueryFilter.layers");
static_assert(offsetof(ShardQueryFilter, ignore) == 8, "ShardQueryFilter.ignore");
static_assert(offsetof(ShardQueryFilter, flags) == 16, "ShardQueryFilter.flags");
static_assert(offsetof(ShardQueryFilter, tag) == 24, "ShardQueryFilter.tag");
static_assert(sizeof(ShardColliderDesc) == 112, "ShardColliderDesc");
static_assert(offsetof(ShardColliderDesc, kind) == 4, "ShardColliderDesc.kind");
static_assert(offsetof(ShardColliderDesc, vertexCount) == 8, "ShardColliderDesc.vertexCount");
static_assert(offsetof(ShardColliderDesc, vertices) == 24, "ShardColliderDesc.vertices");
static_assert(offsetof(ShardColliderDesc, owner) == 48, "ShardColliderDesc.owner");
static_assert(offsetof(ShardColliderDesc, userData) == 56, "ShardColliderDesc.userData");
static_assert(offsetof(ShardColliderDesc, friction) == 64, "ShardColliderDesc.friction");
static_assert(offsetof(ShardColliderDesc, position) == 72, "ShardColliderDesc.position");
static_assert(offsetof(ShardColliderDesc, rotation) == 96, "ShardColliderDesc.rotation");
static_assert(sizeof(ShardPhysicsApi) == 192, "ShardPhysicsApi");
static_assert(offsetof(ShardPhysicsApi, raycast) == 104, "ShardPhysicsApi.raycast");
static_assert(offsetof(ShardPhysicsApi, raycast_all) == 112, "ShardPhysicsApi.raycast_all");
static_assert(offsetof(ShardPhysicsApi, overlap_sphere) == 120, "ShardPhysicsApi.overlap_sphere");
static_assert(offsetof(ShardPhysicsApi, overlap_box) == 128, "ShardPhysicsApi.overlap_box");
static_assert(offsetof(ShardPhysicsApi, overlap_capsule) == 136, "ShardPhysicsApi.overlap_capsule");
static_assert(offsetof(ShardPhysicsApi, sweep_sphere) == 144, "ShardPhysicsApi.sweep_sphere");
static_assert(offsetof(ShardPhysicsApi, sweep_box) == 152, "ShardPhysicsApi.sweep_box");
static_assert(offsetof(ShardPhysicsApi, collider_create) == 160, "ShardPhysicsApi.collider_create");
static_assert(offsetof(ShardPhysicsApi, collider_set_pose) == 168, "ShardPhysicsApi.collider_set_pose");
static_assert(offsetof(ShardPhysicsApi, collider_update) == 176, "ShardPhysicsApi.collider_update");
static_assert(offsetof(ShardPhysicsApi, collider_destroy) == 184, "ShardPhysicsApi.collider_destroy");
static_assert(sizeof(ShardInputEvent) == 40, "ShardInputEvent");
static_assert(offsetof(ShardInputEvent, kind) == 4, "ShardInputEvent.kind");
static_assert(offsetof(ShardInputEvent, key) == 8, "ShardInputEvent.key");
static_assert(offsetof(ShardInputEvent, mods) == 12, "ShardInputEvent.mods");
static_assert(offsetof(ShardInputEvent, flags) == 16, "ShardInputEvent.flags");
static_assert(offsetof(ShardInputEvent, codepoint) == 20, "ShardInputEvent.codepoint");
static_assert(offsetof(ShardInputEvent, x) == 24, "ShardInputEvent.x");
static_assert(offsetof(ShardInputEvent, dx) == 32, "ShardInputEvent.dx");
static_assert(sizeof(ShardInputApi) == 200, "ShardInputApi");
static_assert(offsetof(ShardInputApi, gamepad_rumble) == 120, "ShardInputApi.gamepad_rumble");
static_assert(offsetof(ShardInputApi, event_count) == 128, "ShardInputApi.event_count");
static_assert(offsetof(ShardInputApi, event_at) == 136, "ShardInputApi.event_at");
static_assert(offsetof(ShardInputApi, modifiers) == 144, "ShardInputApi.modifiers");
static_assert(offsetof(ShardInputApi, window_size) == 152, "ShardInputApi.window_size");
static_assert(offsetof(ShardInputApi, clipboard_get) == 160, "ShardInputApi.clipboard_get");
static_assert(offsetof(ShardInputApi, clipboard_set) == 168, "ShardInputApi.clipboard_set");
static_assert(offsetof(ShardInputApi, set_cursor_shape) == 176, "ShardInputApi.set_cursor_shape");
static_assert(offsetof(ShardInputApi, set_text_input) == 184, "ShardInputApi.set_text_input");
static_assert(offsetof(ShardInputApi, capture) == 192, "ShardInputApi.capture");
static_assert(sizeof(ShardWorldsApi) == 48, "ShardWorldsApi");
static_assert(offsetof(ShardWorldsApi, spawn_prefab) == 32, "ShardWorldsApi.spawn_prefab");
static_assert(offsetof(ShardWorldsApi, world_at) == 40, "ShardWorldsApi.world_at");
static_assert(sizeof(ShardEditorApi) == 104, "ShardEditorApi");
static_assert(offsetof(ShardEditorApi, set_panel_visible) == 72, "ShardEditorApi.set_panel_visible");
static_assert(offsetof(ShardEditorApi, undo_push) == 80, "ShardEditorApi.undo_push");
static_assert(offsetof(ShardEditorApi, add_asset_opener) == 88, "ShardEditorApi.add_asset_opener");
static_assert(offsetof(ShardEditorApi, add_asset_template) == 96, "ShardEditorApi.add_asset_template");
static_assert(sizeof(ShardUiApi) == 936, "ShardUiApi");
static_assert(offsetof(ShardUiApi, is_item_double_clicked) == 616, "ShardUiApi.is_item_double_clicked");
static_assert(offsetof(ShardUiApi, draw_line) == 624, "ShardUiApi.draw_line");
static_assert(offsetof(ShardUiApi, draw_polyline) == 632, "ShardUiApi.draw_polyline");
static_assert(offsetof(ShardUiApi, draw_bezier) == 680, "ShardUiApi.draw_bezier");
static_assert(offsetof(ShardUiApi, push_clip_rect) == 696, "ShardUiApi.push_clip_rect");
static_assert(offsetof(ShardUiApi, get_cursor_screen_pos) == 712, "ShardUiApi.get_cursor_screen_pos");
static_assert(offsetof(ShardUiApi, invisible_button) == 760, "ShardUiApi.invisible_button");
static_assert(offsetof(ShardUiApi, get_mouse_pos) == 768, "ShardUiApi.get_mouse_pos");
static_assert(offsetof(ShardUiApi, is_key_down) == 832, "ShardUiApi.is_key_down");
static_assert(offsetof(ShardUiApi, image) == 872, "ShardUiApi.image");
static_assert(offsetof(ShardUiApi, begin_drag_drop_source) == 888, "ShardUiApi.begin_drag_drop_source");
static_assert(offsetof(ShardUiApi, end_drag_drop_target) == 928, "ShardUiApi.end_drag_drop_target");
// ---- shard.jobs ----
static_assert(sizeof(ShardJobsApi) == 72, "ShardJobsApi");
static_assert(offsetof(ShardJobsApi, worker_count) == 8, "ShardJobsApi.worker_count");
static_assert(offsetof(ShardJobsApi, run) == 16, "ShardJobsApi.run");
static_assert(offsetof(ShardJobsApi, parallel_for) == 24, "ShardJobsApi.parallel_for");
static_assert(offsetof(ShardJobsApi, parallel_for_async) == 32, "ShardJobsApi.parallel_for_async");
static_assert(offsetof(ShardJobsApi, is_done) == 40, "ShardJobsApi.is_done");
static_assert(offsetof(ShardJobsApi, wait) == 48, "ShardJobsApi.wait");
static_assert(offsetof(ShardJobsApi, release) == 56, "ShardJobsApi.release");
static_assert(offsetof(ShardJobsApi, post_main) == 64, "ShardJobsApi.post_main");
// ---- shard.time ----
static_assert(sizeof(ShardTimeInfo) == 56, "ShardTimeInfo");
static_assert(offsetof(ShardTimeInfo, playing) == 4, "ShardTimeInfo.playing");
static_assert(offsetof(ShardTimeInfo, frame) == 8, "ShardTimeInfo.frame");
static_assert(offsetof(ShardTimeInfo, fixedStep) == 16, "ShardTimeInfo.fixedStep");
static_assert(offsetof(ShardTimeInfo, simTime) == 24, "ShardTimeInfo.simTime");
static_assert(offsetof(ShardTimeInfo, realTime) == 32, "ShardTimeInfo.realTime");
static_assert(offsetof(ShardTimeInfo, deltaTime) == 40, "ShardTimeInfo.deltaTime");
static_assert(offsetof(ShardTimeInfo, fixedAlpha) == 52, "ShardTimeInfo.fixedAlpha");
static_assert(sizeof(ShardStateHash) == 56, "ShardStateHash");
static_assert(offsetof(ShardStateHash, frame) == 8, "ShardStateHash.frame");
static_assert(offsetof(ShardStateHash, physicsSteps) == 16, "ShardStateHash.physicsSteps");
static_assert(offsetof(ShardStateHash, transforms) == 24, "ShardStateHash.transforms");
static_assert(offsetof(ShardStateHash, physics) == 32, "ShardStateHash.physics");
static_assert(offsetof(ShardStateHash, scripts) == 40, "ShardStateHash.scripts");
static_assert(offsetof(ShardStateHash, total) == 48, "ShardStateHash.total");
static_assert(sizeof(ShardTimeApi) == 40, "ShardTimeApi");
static_assert(offsetof(ShardTimeApi, get) == 8, "ShardTimeApi.get");
static_assert(offsetof(ShardTimeApi, set_time_scale) == 16, "ShardTimeApi.set_time_scale");
static_assert(offsetof(ShardTimeApi, set_fixed_delta_time) == 24, "ShardTimeApi.set_fixed_delta_time");
static_assert(offsetof(ShardTimeApi, state_hash) == 32, "ShardTimeApi.state_hash");
// ---- shard.assets ----
static_assert(sizeof(ShardAssetInfo) == 64, "ShardAssetInfo");
static_assert(offsetof(ShardAssetInfo, id) == 4, "ShardAssetInfo.id");
static_assert(offsetof(ShardAssetInfo, kind) == 8, "ShardAssetInfo.kind");
static_assert(offsetof(ShardAssetInfo, path) == 16, "ShardAssetInfo.path");
static_assert(offsetof(ShardAssetInfo, typeName) == 32, "ShardAssetInfo.typeName");
static_assert(offsetof(ShardAssetInfo, sizeBytes) == 48, "ShardAssetInfo.sizeBytes");
static_assert(offsetof(ShardAssetInfo, modifiedTime) == 56, "ShardAssetInfo.modifiedTime");
static_assert(sizeof(ShardMeshInfo) == 40, "ShardMeshInfo");
static_assert(offsetof(ShardMeshInfo, vertexCount) == 4, "ShardMeshInfo.vertexCount");
static_assert(offsetof(ShardMeshInfo, boundsMin) == 16, "ShardMeshInfo.boundsMin");
static_assert(offsetof(ShardMeshInfo, boundsMax) == 28, "ShardMeshInfo.boundsMax");
static_assert(sizeof(ShardMeshSlice) == 12, "ShardMeshSlice");
static_assert(sizeof(ShardMeshRead) == 72, "ShardMeshRead");
static_assert(offsetof(ShardMeshRead, vertexCapacity) == 4, "ShardMeshRead.vertexCapacity");
static_assert(offsetof(ShardMeshRead, vertexCount) == 16, "ShardMeshRead.vertexCount");
static_assert(offsetof(ShardMeshRead, positions) == 32, "ShardMeshRead.positions");
static_assert(offsetof(ShardMeshRead, slices) == 64, "ShardMeshRead.slices");
static_assert(sizeof(ShardAssetsApi) == 96, "ShardAssetsApi");
static_assert(offsetof(ShardAssetsApi, resolve) == 8, "ShardAssetsApi.resolve");
static_assert(offsetof(ShardAssetsApi, info) == 16, "ShardAssetsApi.info");
static_assert(offsetof(ShardAssetsApi, path_of) == 24, "ShardAssetsApi.path_of");
static_assert(offsetof(ShardAssetsApi, list) == 32, "ShardAssetsApi.list");
static_assert(offsetof(ShardAssetsApi, read) == 40, "ShardAssetsApi.read");
static_assert(offsetof(ShardAssetsApi, write) == 48, "ShardAssetsApi.write");
static_assert(offsetof(ShardAssetsApi, exists) == 56, "ShardAssetsApi.exists");
static_assert(offsetof(ShardAssetsApi, mesh_info) == 64, "ShardAssetsApi.mesh_info");
static_assert(offsetof(ShardAssetsApi, mesh_read) == 72, "ShardAssetsApi.mesh_read");
static_assert(offsetof(ShardAssetsApi, image_info) == 80, "ShardAssetsApi.image_info");
static_assert(offsetof(ShardAssetsApi, image_read) == 88, "ShardAssetsApi.image_read");
// ---- shard.render ----
static_assert(sizeof(ShardSubmesh) == 12, "ShardSubmesh");
static_assert(sizeof(ShardMeshDesc) == 88, "ShardMeshDesc");
static_assert(offsetof(ShardMeshDesc, vertexFlags) == 4, "ShardMeshDesc.vertexFlags");
static_assert(offsetof(ShardMeshDesc, flags) == 20, "ShardMeshDesc.flags");
static_assert(offsetof(ShardMeshDesc, vertices) == 24, "ShardMeshDesc.vertices");
static_assert(offsetof(ShardMeshDesc, indices) == 32, "ShardMeshDesc.indices");
static_assert(offsetof(ShardMeshDesc, submeshes) == 40, "ShardMeshDesc.submeshes");
static_assert(offsetof(ShardMeshDesc, boundsMin) == 48, "ShardMeshDesc.boundsMin");
static_assert(offsetof(ShardMeshDesc, name) == 72, "ShardMeshDesc.name");
static_assert(sizeof(ShardMeshRange) == 24, "ShardMeshRange");
static_assert(offsetof(ShardMeshRange, firstVertex) == 4, "ShardMeshRange.firstVertex");
static_assert(offsetof(ShardMeshRange, vertices) == 16, "ShardMeshRange.vertices");
static_assert(sizeof(ShardTextureDesc) == 48, "ShardTextureDesc");
static_assert(offsetof(ShardTextureDesc, width) == 4, "ShardTextureDesc.width");
static_assert(offsetof(ShardTextureDesc, layers) == 12, "ShardTextureDesc.layers");
static_assert(offsetof(ShardTextureDesc, format) == 16, "ShardTextureDesc.format");
static_assert(offsetof(ShardTextureDesc, flags) == 20, "ShardTextureDesc.flags");
static_assert(offsetof(ShardTextureDesc, pixels) == 24, "ShardTextureDesc.pixels");
static_assert(offsetof(ShardTextureDesc, name) == 32, "ShardTextureDesc.name");
static_assert(sizeof(ShardTextureRegion) == 40, "ShardTextureRegion");
static_assert(offsetof(ShardTextureRegion, x) == 4, "ShardTextureRegion.x");
static_assert(offsetof(ShardTextureRegion, width) == 16, "ShardTextureRegion.width");
static_assert(offsetof(ShardTextureRegion, rowPitch) == 24, "ShardTextureRegion.rowPitch");
static_assert(offsetof(ShardTextureRegion, pixels) == 32, "ShardTextureRegion.pixels");
static_assert(sizeof(ShardRenderTransform) == 56, "ShardRenderTransform");
static_assert(offsetof(ShardRenderTransform, rotation) == 24, "ShardRenderTransform.rotation");
static_assert(offsetof(ShardRenderTransform, scale) == 40, "ShardRenderTransform.scale");
static_assert(sizeof(ShardRenderObjectDesc) == 96, "ShardRenderObjectDesc");
static_assert(offsetof(ShardRenderObjectDesc, mesh) == 8, "ShardRenderObjectDesc.mesh");
static_assert(offsetof(ShardRenderObjectDesc, materials) == 16, "ShardRenderObjectDesc.materials");
static_assert(offsetof(ShardRenderObjectDesc, pickActor) == 32, "ShardRenderObjectDesc.pickActor");
static_assert(offsetof(ShardRenderObjectDesc, transform) == 40, "ShardRenderObjectDesc.transform");
static_assert(sizeof(ShardSprite) == 60, "ShardSprite");
static_assert(offsetof(ShardSprite, rotation) == 12, "ShardSprite.rotation");
static_assert(offsetof(ShardSprite, size) == 16, "ShardSprite.size");
static_assert(offsetof(ShardSprite, color) == 24, "ShardSprite.color");
static_assert(offsetof(ShardSprite, uvRect) == 28, "ShardSprite.uvRect");
static_assert(offsetof(ShardSprite, axis) == 44, "ShardSprite.axis");
static_assert(offsetof(ShardSprite, custom) == 56, "ShardSprite.custom");
static_assert(sizeof(ShardSpriteBatchDesc) == 64, "ShardSpriteBatchDesc");
static_assert(offsetof(ShardSpriteBatchDesc, facing) == 4, "ShardSpriteBatchDesc.facing");
static_assert(offsetof(ShardSpriteBatchDesc, maxSprites) == 20, "ShardSpriteBatchDesc.maxSprites");
static_assert(offsetof(ShardSpriteBatchDesc, texture) == 24, "ShardSpriteBatchDesc.texture");
static_assert(offsetof(ShardSpriteBatchDesc, material) == 32, "ShardSpriteBatchDesc.material");
static_assert(offsetof(ShardSpriteBatchDesc, origin) == 40, "ShardSpriteBatchDesc.origin");
static_assert(sizeof(ShardInstance) == 40, "ShardInstance");
static_assert(sizeof(ShardInstanceBatchDesc) == 56, "ShardInstanceBatchDesc");
static_assert(offsetof(ShardInstanceBatchDesc, mesh) == 8, "ShardInstanceBatchDesc.mesh");
static_assert(offsetof(ShardInstanceBatchDesc, materials) == 16, "ShardInstanceBatchDesc.materials");
static_assert(offsetof(ShardInstanceBatchDesc, maxInstances) == 28, "ShardInstanceBatchDesc.maxInstances");
static_assert(offsetof(ShardInstanceBatchDesc, origin) == 32, "ShardInstanceBatchDesc.origin");
static_assert(sizeof(ShardVertex2D) == 20, "ShardVertex2D");
static_assert(offsetof(ShardVertex2D, color) == 8, "ShardVertex2D.color");
static_assert(offsetof(ShardVertex2D, uv) == 12, "ShardVertex2D.uv");
static_assert(sizeof(ShardDrawApi) == 56, "ShardDrawApi");
static_assert(offsetof(ShardDrawApi, draw_geometry) == 8, "ShardDrawApi.draw_geometry");
static_assert(offsetof(ShardDrawApi, set_transform) == 16, "ShardDrawApi.set_transform");
static_assert(offsetof(ShardDrawApi, set_scissor) == 24, "ShardDrawApi.set_scissor");
static_assert(offsetof(ShardDrawApi, set_clip_mask) == 32, "ShardDrawApi.set_clip_mask");
static_assert(offsetof(ShardDrawApi, clear) == 40, "ShardDrawApi.clear");
static_assert(offsetof(ShardDrawApi, draw_triangles) == 48, "ShardDrawApi.draw_triangles");
static_assert(sizeof(ShardOverlayFrame) == 48, "ShardOverlayFrame");
static_assert(offsetof(ShardOverlayFrame, width) == 4, "ShardOverlayFrame.width");
static_assert(offsetof(ShardOverlayFrame, dpiScale) == 12, "ShardOverlayFrame.dpiScale");
static_assert(offsetof(ShardOverlayFrame, deltaTime) == 16, "ShardOverlayFrame.deltaTime");
static_assert(offsetof(ShardOverlayFrame, frame) == 24, "ShardOverlayFrame.frame");
static_assert(offsetof(ShardOverlayFrame, playing) == 32, "ShardOverlayFrame.playing");
static_assert(offsetof(ShardOverlayFrame, originX) == 36, "ShardOverlayFrame.originX");
static_assert(sizeof(ShardOverlayDesc) == 56, "ShardOverlayDesc");
static_assert(offsetof(ShardOverlayDesc, order) == 8, "ShardOverlayDesc.order");
static_assert(offsetof(ShardOverlayDesc, name) == 16, "ShardOverlayDesc.name");
static_assert(offsetof(ShardOverlayDesc, target) == 32, "ShardOverlayDesc.target");
static_assert(offsetof(ShardOverlayDesc, draw) == 40, "ShardOverlayDesc.draw");
static_assert(offsetof(ShardOverlayDesc, user) == 48, "ShardOverlayDesc.user");
static_assert(sizeof(ShardCameraInfo) == 208, "ShardCameraInfo");
static_assert(offsetof(ShardCameraInfo, width) == 8, "ShardCameraInfo.width");
static_assert(offsetof(ShardCameraInfo, fovDegrees) == 20, "ShardCameraInfo.fovDegrees");
static_assert(offsetof(ShardCameraInfo, position) == 40, "ShardCameraInfo.position");
static_assert(offsetof(ShardCameraInfo, rotation) == 64, "ShardCameraInfo.rotation");
static_assert(offsetof(ShardCameraInfo, view) == 80, "ShardCameraInfo.view");
static_assert(offsetof(ShardCameraInfo, projection) == 144, "ShardCameraInfo.projection");
static_assert(sizeof(ShardRenderApi) == 304, "ShardRenderApi");
static_assert(offsetof(ShardRenderApi, mesh_create) == 8, "ShardRenderApi.mesh_create");
static_assert(offsetof(ShardRenderApi, mesh_destroy) == 40, "ShardRenderApi.mesh_destroy");
static_assert(offsetof(ShardRenderApi, texture_create) == 48, "ShardRenderApi.texture_create");
static_assert(offsetof(ShardRenderApi, texture_destroy) == 88, "ShardRenderApi.texture_destroy");
static_assert(offsetof(ShardRenderApi, material_load) == 96, "ShardRenderApi.material_load");
static_assert(offsetof(ShardRenderApi, material_destroy) == 128, "ShardRenderApi.material_destroy");
static_assert(offsetof(ShardRenderApi, object_create) == 136, "ShardRenderApi.object_create");
static_assert(offsetof(ShardRenderApi, object_set_pose) == 176, "ShardRenderApi.object_set_pose");
static_assert(offsetof(ShardRenderApi, object_destroy) == 184, "ShardRenderApi.object_destroy");
static_assert(offsetof(ShardRenderApi, sprite_batch_create) == 192, "ShardRenderApi.sprite_batch_create");
static_assert(offsetof(ShardRenderApi, instance_batch_create) == 224, "ShardRenderApi.instance_batch_create");
static_assert(offsetof(ShardRenderApi, instance_batch_destroy) == 240, "ShardRenderApi.instance_batch_destroy");
static_assert(offsetof(ShardRenderApi, overlay_add) == 248, "ShardRenderApi.overlay_add");
static_assert(offsetof(ShardRenderApi, geometry_destroy) == 272, "ShardRenderApi.geometry_destroy");
static_assert(offsetof(ShardRenderApi, camera_info) == 280, "ShardRenderApi.camera_info");
static_assert(offsetof(ShardRenderApi, world_to_screen) == 288, "ShardRenderApi.world_to_screen");
static_assert(offsetof(ShardRenderApi, screen_to_ray) == 296, "ShardRenderApi.screen_to_ray");
// ---- shard.debug ----
static_assert(sizeof(ShardDebugApi) == 120, "ShardDebugApi");
static_assert(offsetof(ShardDebugApi, enabled) == 8, "ShardDebugApi.enabled");
static_assert(offsetof(ShardDebugApi, line) == 16, "ShardDebugApi.line");
static_assert(offsetof(ShardDebugApi, triangles) == 88, "ShardDebugApi.triangles");
static_assert(offsetof(ShardDebugApi, text) == 96, "ShardDebugApi.text");
static_assert(offsetof(ShardDebugApi, text_screen) == 104, "ShardDebugApi.text_screen");
static_assert(offsetof(ShardDebugApi, clear) == 112, "ShardDebugApi.clear");
// ---- shard.reflect ----
static_assert(sizeof(ShardValue) == 56, "ShardValue");
static_assert(offsetof(ShardValue, type) == 0, "ShardValue.type");
static_assert(offsetof(ShardValue, i) == 8, "ShardValue.i");
static_assert(offsetof(ShardValue, d) == 16, "ShardValue.d");
static_assert(offsetof(ShardValue, f) == 24, "ShardValue.f");
static_assert(offsetof(ShardValue, s) == 40, "ShardValue.s");
static_assert(sizeof(ShardTypeInfo) == 64, "ShardTypeInfo");
static_assert(offsetof(ShardTypeInfo, kind) == 4, "ShardTypeInfo.kind");
static_assert(offsetof(ShardTypeInfo, fieldCount) == 8, "ShardTypeInfo.fieldCount");
static_assert(offsetof(ShardTypeInfo, name) == 16, "ShardTypeInfo.name");
static_assert(offsetof(ShardTypeInfo, plugin) == 32, "ShardTypeInfo.plugin");
static_assert(offsetof(ShardTypeInfo, category) == 48, "ShardTypeInfo.category");
static_assert(sizeof(ShardFieldInfo) == 56, "ShardFieldInfo");
static_assert(offsetof(ShardFieldInfo, valueType) == 4, "ShardFieldInfo.valueType");
static_assert(offsetof(ShardFieldInfo, enumCount) == 12, "ShardFieldInfo.enumCount");
static_assert(offsetof(ShardFieldInfo, min) == 16, "ShardFieldInfo.min");
static_assert(offsetof(ShardFieldInfo, name) == 24, "ShardFieldInfo.name");
static_assert(offsetof(ShardFieldInfo, tooltip) == 40, "ShardFieldInfo.tooltip");
static_assert(sizeof(ShardParamDesc) == 48, "ShardParamDesc");
static_assert(offsetof(ShardParamDesc, type) == 4, "ShardParamDesc.type");
static_assert(offsetof(ShardParamDesc, name) == 16, "ShardParamDesc.name");
static_assert(offsetof(ShardParamDesc, tooltip) == 32, "ShardParamDesc.tooltip");
static_assert(sizeof(ShardFunctionDesc) == 96, "ShardFunctionDesc");
static_assert(offsetof(ShardFunctionDesc, flags) == 4, "ShardFunctionDesc.flags");
static_assert(offsetof(ShardFunctionDesc, name) == 16, "ShardFunctionDesc.name");
static_assert(offsetof(ShardFunctionDesc, category) == 32, "ShardFunctionDesc.category");
static_assert(offsetof(ShardFunctionDesc, tooltip) == 48, "ShardFunctionDesc.tooltip");
static_assert(offsetof(ShardFunctionDesc, params) == 64, "ShardFunctionDesc.params");
static_assert(offsetof(ShardFunctionDesc, results) == 72, "ShardFunctionDesc.results");
static_assert(offsetof(ShardFunctionDesc, call) == 80, "ShardFunctionDesc.call");
static_assert(offsetof(ShardFunctionDesc, user) == 88, "ShardFunctionDesc.user");
static_assert(sizeof(ShardFunctionInfo) == 80, "ShardFunctionInfo");
static_assert(offsetof(ShardFunctionInfo, name) == 16, "ShardFunctionInfo.name");
static_assert(offsetof(ShardFunctionInfo, plugin) == 64, "ShardFunctionInfo.plugin");
static_assert(sizeof(ShardReflectApi) == 104, "ShardReflectApi");
static_assert(offsetof(ShardReflectApi, type_count) == 8, "ShardReflectApi.type_count");
static_assert(offsetof(ShardReflectApi, type_at) == 16, "ShardReflectApi.type_at");
static_assert(offsetof(ShardReflectApi, type_find) == 24, "ShardReflectApi.type_find");
static_assert(offsetof(ShardReflectApi, field_at) == 32, "ShardReflectApi.field_at");
static_assert(offsetof(ShardReflectApi, field_enum_at) == 40, "ShardReflectApi.field_enum_at");
static_assert(offsetof(ShardReflectApi, function_add) == 48, "ShardReflectApi.function_add");
static_assert(offsetof(ShardReflectApi, function_remove) == 56, "ShardReflectApi.function_remove");
static_assert(offsetof(ShardReflectApi, function_count) == 64, "ShardReflectApi.function_count");
static_assert(offsetof(ShardReflectApi, function_at) == 72, "ShardReflectApi.function_at");
static_assert(offsetof(ShardReflectApi, function_find) == 80, "ShardReflectApi.function_find");
static_assert(offsetof(ShardReflectApi, function_param_at) == 88, "ShardReflectApi.function_param_at");
static_assert(offsetof(ShardReflectApi, function_call) == 96, "ShardReflectApi.function_call");
// ---- shard.net ----
static_assert(sizeof(ShardNetAddress) == 24, "ShardNetAddress");
static_assert(offsetof(ShardNetAddress, port) == 4, "ShardNetAddress.port");
static_assert(offsetof(ShardNetAddress, host) == 8, "ShardNetAddress.host");
static_assert(sizeof(ShardNetEvent) == 64, "ShardNetEvent");
static_assert(offsetof(ShardNetEvent, kind) == 4, "ShardNetEvent.kind");
static_assert(offsetof(ShardNetEvent, socket) == 8, "ShardNetEvent.socket");
static_assert(offsetof(ShardNetEvent, listener) == 16, "ShardNetEvent.listener");
static_assert(offsetof(ShardNetEvent, peer) == 24, "ShardNetEvent.peer");
static_assert(offsetof(ShardNetEvent, data) == 40, "ShardNetEvent.data");
static_assert(offsetof(ShardNetEvent, size) == 48, "ShardNetEvent.size");
static_assert(offsetof(ShardNetEvent, error) == 56, "ShardNetEvent.error");
static_assert(sizeof(ShardNetApi) == 88, "ShardNetApi");
static_assert(offsetof(ShardNetApi, udp_open) == 8, "ShardNetApi.udp_open");
static_assert(offsetof(ShardNetApi, tcp_listen) == 16, "ShardNetApi.tcp_listen");
static_assert(offsetof(ShardNetApi, tcp_connect) == 24, "ShardNetApi.tcp_connect");
static_assert(offsetof(ShardNetApi, send) == 32, "ShardNetApi.send");
static_assert(offsetof(ShardNetApi, send_to) == 40, "ShardNetApi.send_to");
static_assert(offsetof(ShardNetApi, close) == 48, "ShardNetApi.close");
static_assert(offsetof(ShardNetApi, local_port) == 56, "ShardNetApi.local_port");
static_assert(offsetof(ShardNetApi, queued_bytes) == 64, "ShardNetApi.queued_bytes");
static_assert(offsetof(ShardNetApi, set_option) == 72, "ShardNetApi.set_option");
static_assert(offsetof(ShardNetApi, poll) == 80, "ShardNetApi.poll");
// ---- numbers of 1.7 ----
static_assert(SHARD_VERTEX_POSITION == 1 && SHARD_VERTEX_NORMAL == 2 && SHARD_VERTEX_TANGENT == 4 && SHARD_VERTEX_UV0 == 8 && SHARD_VERTEX_UV1 == 16, "vertex attributes");
static_assert(SHARD_VERTEX_COLOR == 32 && SHARD_VERTEX_JOINTS == 64 && SHARD_VERTEX_WEIGHTS == 128 && SHARD_MESH_INDEX16 == 1, "vertex attributes");
static_assert(SHARD_TEXTURE_R8 == 1 && SHARD_TEXTURE_RGBA8 == 3 && SHARD_TEXTURE_R32F == 6 && SHARD_TEXTURE_RENDER_TARGET == 16, "texture formats and flags");
static_assert(SHARD_BLEND_OPAQUE == 0 && SHARD_BLEND_ALPHA == 2 && SHARD_BLEND_PREMULTIPLIED == 4 && SHARD_SPRITE_FACE_FIXED == 2, "sprites");
static_assert(SHARD_CLIP_DISABLE == 0 && SHARD_CLIP_SET == 1 && SHARD_CLIP_SET_INVERSE == 2 && SHARD_CLIP_INTERSECT == 3, "clip masks");
static_assert(SHARD_VALUE_NONE == 0 && SHARD_VALUE_BOOL == 1 && SHARD_VALUE_STRING == 11 && SHARD_VALUE_ACTOR == 12 && SHARD_VALUE_ANY == 16, "reflect values");
static_assert(SHARD_NET_EVENT_CONNECTED == 1 && SHARD_NET_EVENT_DATA == 3 && SHARD_NET_EVENT_ERROR == 5 && SHARD_NET_OPT_NODELAY == 1, "net");
static_assert(SHARD_EVENT_KEY_DOWN == 1 && SHARD_EVENT_TEXT == 3 && SHARD_EVENT_WHEEL == 7 && SHARD_EVENT_RESIZED == 10 && SHARD_CAPTURE_GAMEPAD == 4, "input events");
static_assert(SHARD_COLLIDER_MESH == 1 && SHARD_COLLIDER_BOXES == 2 && SHARD_QUERY_COLLIDERS_ONLY == 1 && SHARD_SCRIPT_EDIT_MODE == 1, "physics and scripts");
static_assert(SHARD_ASSET_MESH == 1 && SHARD_ASSET_TEXTURE == 2 && SHARD_ASSET_PLUGIN == 8, "asset kinds");

static_assert(SHARD_KEY_A == 65 && SHARD_KEY_ESCAPE == 256 && SHARD_KEY_F1 == 290 && SHARD_KEY_KP_0 == 320 && SHARD_KEY_MENU == 348, "key numbers");
static_assert(SHARD_PAD_DPAD_RIGHT == 14 && SHARD_AXIS_RIGHT_TRIGGER == 5 && SHARD_CURSOR_DISABLED == 2, "pad and cursor numbers");

static_assert(SHARD_FIELD_STRING == 14 && SHARD_FIELD_ASSET == 15 && SHARD_COMPONENT_MAX_STRINGS == 8, "text and assets (1.3)");
static_assert(SHARD_FIELD_RUNTIME == 1 && SHARD_FIELD_PRIVATE == 2 && SHARD_COMPONENT_MAX_SIZE == 1024 && SHARD_COMPONENT_MAX_FIELDS == 64, "field flags and limits");
static_assert(SHARD_PHASE_INPUT == 0 && SHARD_PHASE_PRE_SIM == 1 && SHARD_PHASE_FIXED == 2 && SHARD_PHASE_POST_PHYSICS == 3, "phases");
static_assert(SHARD_PHASE_UPDATE == 4 && SHARD_PHASE_LATE == 5 && SHARD_PHASE_EXTRACT == 6, "phases");
static_assert(SHARD_SYSTEM_EXCLUSIVE == 1 && SHARD_SYSTEM_MAIN_THREAD == 2, "system flags");
static_assert(SHARD_PRIORITY_EARLIEST == -200 && SHARD_PRIORITY_EARLY == -100 && SHARD_PRIORITY_NORMAL == 0 && SHARD_PRIORITY_LATE == 100 && SHARD_PRIORITY_LATEST == 200, "priorities");
static_assert(SHARD_LOG_INFO == 0 && SHARD_LOG_WARNING == 1 && SHARD_LOG_ERROR == 2, "log levels");
static_assert(SHARD_ABI_MAJOR == 1 && SHARD_ABI_MINOR == 7, "the ABI revision of these headers");

TEST(AbiLayout, HeadersCompileAsCppAndPinTheirLayout)
{
    // The assertions above are the test : this case makes sure the file is part of the run
    EXPECT_EQ(sizeof(ShardStr), 16u);
}
