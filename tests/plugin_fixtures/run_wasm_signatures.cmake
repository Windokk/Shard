# Callback and import signatures must match what the host calls and links (src/engine/plugins/wasm). wasm32 struct layouts are checked by the compile itself (SHARD_WASM_CHECK_LAYOUT).
execute_process(
    COMMAND "${CLANG}" --target=wasm32 -ffreestanding -nostdlib -fno-builtin -std=c11 -Wall -Wextra -pedantic -Werror -O2 "-I${INCLUDE}" -S "${SOURCE}" -o "${OUT}"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "clang failed with ${result}\n${output}\n${errors}")
endif()
file(READ "${OUT}" assembly)
foreach(expected
        "shard_plugin_entry (i32, i32) -> (i32)"
        "on_load (i32, i64) -> (i32)"
        "on_frame_begin (i32, i64, i64) -> (i32)"
        "on_prepare_reload (i32, i64, i64) -> (i32)"
        "on_world_loaded (i32, i64, i32) -> (i32)"
        "echo (i32, i64, i32, i32, i32) -> (i32)"
        "spin_run (i32, i64, i32) -> (i32)"
        "spin_chunk (i32, i32) -> (i32)"
        "on_ping (i32, i64, i32, i32, i64) -> ()"
        "list_one (i32, i32) -> (i32)"
        "mover_construct (i32, i32) -> (i32)"
        "mover_destruct (i32, i32) -> ()"
        "mover_attach (i32, i32, i64) -> (i32)"
        "mover_tick (i32, i32, f32) -> (i32)"
        "mover_contact (i32, i32, i32) -> (i32)"
        # A ShardStr by value goes by address. A returned one goes through a pointer passed first.
        "shard_wasm_host_log (i64, i32, i32) -> ()"
        "shard_wasm_host_alloc (i64, i64, i64) -> (i32)"
        "shard_wasm_interface_offered (i32, i32, i32) -> (i32)"
        "shard_wasm_plugin_id (i32, i64) -> ()"
        "shard_wasm_cvars_create_float (i64, i32, f32, i32, i32, f32, f32, i32) -> (i32)"
        "shard_wasm_cvars_get_string (i64, i64, i32, i64, i32) -> (i32)"
        "shard_wasm_ecs_each_chunk (i64, i32, i32, i32) -> (i32)"
        "shard_wasm_systems_add (i64, i32) -> (i32)"
        "shard_wasm_actors_find_by_tag (i64, i32, i32, i32, i32) -> (i32)"
        "shard_wasm_input_get_cursor (i32, i32) -> (i32)"
        "shard_wasm_physics_raycast (i32, i32, f32, i32, i64, i32) -> (i32)"
        "import_name	shard_wasm_cvars_create_float, cvars.create_float"
        "import_name	shard_wasm_plugin_id, plugin.id")
    string(FIND "${assembly}" "${expected}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "expected \"${expected}\" in what Clang made of ${SOURCE}\n${assembly}")
    endif()
endforeach()
