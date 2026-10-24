# Building a plugin for the sandbox (tier 2, WebAssembly) : shard_sdk_add_plugin(... TARGET wasm) uses this.
#
# What it needs, found in this order :
#   SHARD_WASM_TOOLCHAIN  (CMake variable or environment variable) : a wasi-sdk (bin/clang++, share/wasi-sysroot) or a wasi sysroot alone
#                         (include/wasm32-wasip1, lib/wasm32-wasip1 : the "noeh" variant, without exception handling, is what is linked)
#   WASI_SDK_PATH         the variable wasi-sdk's own toolchain files use
#   clang / clang++ and wasm-ld on the PATH when the toolchain is only a sysroot
# wasi-sdk 34 or later (docs/design-notes/wasm-sandbox.fr.md section 7). The C++ is compiled without exceptions (SHARD_SDK_NO_EXCEPTIONS : a
# refusal of the host that the SDK would throw stops the plugin instead), as a "reactor" : the module exports _initialize, which the host runs
# before the entry point (the static constructors of a game's files register their pieces).

include_guard(GLOBAL)

# Sets SHARD_WASM_CLANG, SHARD_WASM_CLANGXX, SHARD_WASM_LD and SHARD_WASM_SYSROOT, or stops with the reason
function(shard_sdk_wasm_toolchain)
    get_property(_done GLOBAL PROPERTY SHARD_WASM_TOOLCHAIN_FOUND)
    if(_done)
        return()
    endif()
    set(_root "${SHARD_WASM_TOOLCHAIN}")
    if(NOT _root)
        set(_root "$ENV{SHARD_WASM_TOOLCHAIN}")
    endif()
    if(NOT _root)
        set(_root "${WASI_SDK_PATH}")
    endif()
    if(NOT _root)
        set(_root "$ENV{WASI_SDK_PATH}")
    endif()
    if(NOT _root)
        message(FATAL_ERROR "TARGET wasm needs a wasi-sdk (34 or later) or its sysroot : set SHARD_WASM_TOOLCHAIN to its folder")
    endif()
    file(TO_CMAKE_PATH "${_root}" _root)
    if(EXISTS "${_root}/share/wasi-sysroot/include/wasm32-wasip1")
        set(_sysroot "${_root}/share/wasi-sysroot")
    elseif(EXISTS "${_root}/include/wasm32-wasip1")
        set(_sysroot "${_root}")
    else()
        message(FATAL_ERROR "SHARD_WASM_TOOLCHAIN (${_root}) is neither a wasi-sdk 34 or later nor its sysroot (no include/wasm32-wasip1)")
    endif()
    find_program(_clang NAMES clang HINTS "${_root}/bin" NO_DEFAULT_PATH)
    find_program(_clang NAMES clang)
    find_program(_clangxx NAMES clang++ HINTS "${_root}/bin" NO_DEFAULT_PATH)
    find_program(_clangxx NAMES clang++)
    find_program(_ld NAMES wasm-ld wasm-ld-21 wasm-ld-20 wasm-ld-19 HINTS "${_root}/bin" NO_DEFAULT_PATH)
    find_program(_ld NAMES wasm-ld wasm-ld-21 wasm-ld-20 wasm-ld-19)
    if(NOT _clang OR NOT _clangxx OR NOT _ld)
        message(FATAL_ERROR "TARGET wasm needs clang, clang++ and wasm-ld (in ${_root}/bin or on the PATH)")
    endif()
    # compiler-rt's builtins for wasm32 (__multi3...) : SHARD_WASM_BUILTINS, else what a wasi-sdk's clang says, else a libclang_rt.builtins.a under the toolchain
    set(_builtins "${SHARD_WASM_BUILTINS}")
    if(NOT _builtins)
        set(_builtins "$ENV{SHARD_WASM_BUILTINS}")
    endif()
    if(NOT _builtins)
        execute_process(COMMAND "${_clang}" --target=wasm32-wasip1 --print-libgcc-file-name OUTPUT_VARIABLE _printed ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_printed AND EXISTS "${_printed}")
            set(_builtins "${_printed}")
        endif()
    endif()
    if(NOT _builtins)
        file(GLOB_RECURSE _found "${_root}/*libclang_rt.builtins*.a")
        foreach(_candidate ${_found})
            if(_candidate MATCHES "wasip1|wasm32" AND NOT _candidate MATCHES "threads")
                set(_builtins "${_candidate}")
                break()
            endif()
        endforeach()
    endif()
    if(NOT _builtins)
        message(FATAL_ERROR "TARGET wasm needs libclang_rt.builtins.a for wasm32 (the libclang_rt asset of the wasi-sdk release) : set SHARD_WASM_BUILTINS to it")
    endif()
    set_property(GLOBAL PROPERTY SHARD_WASM_BUILTINS_FILE "${_builtins}")
    set_property(GLOBAL PROPERTY SHARD_WASM_SYSROOT "${_sysroot}")
    set_property(GLOBAL PROPERTY SHARD_WASM_CLANG "${_clang}")
    set_property(GLOBAL PROPERTY SHARD_WASM_CLANGXX "${_clangxx}")
    set_property(GLOBAL PROPERTY SHARD_WASM_LD "${_ld}")
    set_property(GLOBAL PROPERTY SHARD_WASM_TOOLCHAIN_FOUND TRUE)
endfunction()

# shard_sdk_add_wasm_module(<target> OUTPUT <file.wasm> SOURCES ... [INCLUDE_DIRS ...])
# A custom target that compiles each source with clang for wasm32-wasip1 and links them with the sysroot's libc and libc++ into OUTPUT.
function(shard_sdk_add_wasm_module target)
    cmake_parse_arguments(W "" "OUTPUT" "SOURCES;INCLUDE_DIRS;DEFINITIONS" ${ARGN})
    shard_sdk_wasm_toolchain()
    foreach(_name SHARD_WASM_BUILTINS_FILE SHARD_WASM_SYSROOT SHARD_WASM_CLANG SHARD_WASM_CLANGXX SHARD_WASM_LD)
        get_property(${_name} GLOBAL PROPERTY ${_name})
    endforeach()
    set(_sys "${SHARD_WASM_SYSROOT}")
    set(_lib "${_sys}/lib/wasm32-wasip1")
    set(_flags --target=wasm32-wasip1 "--sysroot=${_sys}" -fno-exceptions -fvisibility=hidden -O2 -DSHARD_SDK_NO_EXCEPTIONS)
    set(_cxx_flags -std=c++17 -isystem "${_sys}/include/wasm32-wasip1/noeh/c++/v1" -isystem "${_sys}/include/wasm32-wasip1")
    set(_c_flags -std=c11 -isystem "${_sys}/include/wasm32-wasip1")
    set(_includes "")
    foreach(_dir ${W_INCLUDE_DIRS})
        list(APPEND _includes "-I${_dir}")
    endforeach()
    set(_defs "")
    foreach(_d ${W_DEFINITIONS})
        list(APPEND _defs "-D${_d}")
    endforeach()

    set(_objects "")
    set(_index 0)
    foreach(_src ${W_SOURCES})
        math(EXPR _index "${_index}+1")
        get_filename_component(_name "${_src}" NAME)
        set(_obj "${CMAKE_CURRENT_BINARY_DIR}/${target}_wasm/${_index}_${_name}.o")
        get_filename_component(_ext "${_src}" EXT)
        if(_ext STREQUAL ".c")
            set(_compiler "${SHARD_WASM_CLANG}")
            set(_lang_flags ${_c_flags})
        else()
            set(_compiler "${SHARD_WASM_CLANGXX}")
            set(_lang_flags ${_cxx_flags})
        endif()
        add_custom_command(OUTPUT "${_obj}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/${target}_wasm"
            COMMAND "${_compiler}" ${_flags} ${_lang_flags} ${_includes} ${_defs} -MD -MF "${_obj}.d" -c "${_src}" -o "${_obj}"
            DEPENDS "${_src}"
            DEPFILE "${_obj}.d"
            COMMENT "Compiling ${_name} for wasm32"
            VERBATIM)
        list(APPEND _objects "${_obj}")
    endforeach()
    get_filename_component(_out_dir "${W_OUTPUT}" DIRECTORY)
    add_custom_command(OUTPUT "${W_OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_out_dir}"
        COMMAND "${SHARD_WASM_LD}" --no-entry --export-dynamic --export=shard_plugin_entry --export=shard_wasm_host_table --export=shard_wasm_ui_table --export=shard_wasm_gizmo_table --export=_initialize --export-table
            -o "${W_OUTPUT}" "${_lib}/crt1-reactor.o" ${_objects} "${_lib}/noeh/libc++.a" "${_lib}/noeh/libc++abi.a" "${_lib}/libc.a" "${SHARD_WASM_BUILTINS_FILE}"
        DEPENDS ${_objects}
        COMMENT "Linking ${W_OUTPUT}"
        VERBATIM)
    add_custom_target(${target} ALL DEPENDS "${W_OUTPUT}")
endfunction()
