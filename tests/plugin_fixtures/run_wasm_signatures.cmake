# A sandboxed plugin written in C against sdk/include (shard_wasm.h) and compiled by Clang for wasm32 : the signatures of its imports and
# callbacks must be the ones the host links (src/engine/plugins/wasm/wasm_plugin.cpp). Variables : CLANG SOURCE INCLUDE OUT
execute_process(
    COMMAND "${CLANG}" --target=wasm32 -ffreestanding -nostdlib -std=c11 -Wall -Wextra -pedantic -Werror -O2 "-I${INCLUDE}" -S "${SOURCE}" -o "${OUT}"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "clang failed with ${result}\n${output}\n${errors}")
endif()
file(READ "${OUT}" assembly)
foreach(expected
        "shard_plugin_entry (i32, i32) -> (i32)"
        "on_load (i32, i64) -> (i32)"
        "on_enable (i32, i64) -> (i32)"
        "shard_wasm_log (i64, i32, i32) -> ()"
        "shard_wasm_cvar_create_float (i64, i32, f32, i32, i32, f32, f32, i32) -> (i32)"
        "shard_wasm_cvar_get_float (i64, i64, i32) -> (i32)"
        "shard_wasm_cvar_set_float (i64, i64, f32) -> (i32)"
        "shard_wasm_command_add (i64, i32, i32, i32, i32) -> (i32)"
        "echo (i32, i64, i32, i32, i32) -> (i32)"
        "shard_wasm_content_read (i64, i32, i32, i64, i32) -> (i32)"
        "import_name	shard_wasm_cvar_create_float, cvars.create_float")
    string(FIND "${assembly}" "${expected}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "expected \"${expected}\" in what Clang made of ${SOURCE}\n${assembly}")
    endif()
endforeach()
