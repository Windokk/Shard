# shard_sdk_add_plugin : builds a plugin of the C ABI against the Shard SDK alone, and lays it out the way the engine finds it.
#
#   find_package(ShardSDK 1.0 REQUIRED)
#   shard_sdk_add_plugin(weather
#       SOURCES      src/weather.cpp ...       # files, and / or :
#       SOURCE_DIRS  src                       # folders : every .cpp / .cc / .cxx / .c under them is part of the plugin (found again when a file is added)
#       MANIFEST     shard-plugin.json        # the plugin's manifest : the SDK adds "abi", "linkage" and "binaries" to it
#     # or, without a manifest of your own :
#       ID           com.acme.weather         # reverse DNS, lowercase
#       VERSION      1.4.2
#       [KIND        plugin|module]           # default : plugin
#       [MODULE_KIND code|genre|mod]          # default : code, when KIND is module
#       [HOSTS       engine player headless editor]   # default : engine
#       [DEPENDS     shard.audio "com.acme.clouds@^2.1" ...]   # "id" or "id@range" ; "id?" for an optional one
#       [DESCRIPTION "..."]
#       [AFFECTS_SIMULATION] [NO_SIMULATION]  # default : modules affect the simulation, plugins do not
#     # either way :
#       [CONTENT_DIR content]                 # a folder of the source tree copied next to the plugin as its content
#       [OUTPUT_DIR  <dir>]                   # where plugin folders go (default : <build>/plugins)
#       [PACKAGE]                             # a target <target>_package that makes a .shardplugin with ShardPkg (SHARD_PKG_EXECUTABLE, or found on PATH)
#       [TARGET wasm]                         # a sandboxed plugin (tier 2) : compiled to WebAssembly with a wasi-sdk (SHARD_WASM_TOOLCHAIN, see
#                                             # ShardSDKWasm.cmake), <OUTPUT_DIR>/<id>/plugin.wasm, loaded without signature or consent
#       [PERMISSIONS fs.read:content input ...]  # what a sandboxed plugin may use beyond the basics (it is refused the rest)
#   )
#
# The result :
#   <OUTPUT_DIR>/<id>/shard-plugin.json
#   <OUTPUT_DIR>/<id>/bin/<platform>/<library>
#   <OUTPUT_DIR>/<id>/content/...
# The library is shared, its symbols hidden but the entry point (SHARD_PLUGIN_EXPORT), and built with the warnings the headers promise to
# pass (-Wall -Wextra -pedantic). Point the engine at the folder : ShardHeadless --plugin <OUTPUT_DIR>/<id>, or the project's plugins folder.

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ShardSDKWasm.cmake")

# The platform key a manifest names its binary under ("windows-x86_64", "linux-arm64"...) : the same as the engine's CurrentPlatformKey()
function(shard_sdk_platform out)
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" _os)
    if(_os STREQUAL "darwin")
        set(_os macos)
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
        set(_arch arm64)
    else()
        set(_arch x86_64)
    endif()
    set(${out} "${_os}-${_arch}" PARENT_SCOPE)
endfunction()

# Puts the library of `target` in <root>/bin/<platform>/ (every configuration). Its import library (.lib / .dll.a on Windows) stays in the
# build folder : nothing links against a plugin, and it would be shipped with it.
function(shard_sdk_place_binary target root platform)
    set(_bin "${root}/bin/${platform}")
    set(_imports "${CMAKE_CURRENT_BINARY_DIR}/shard_import_libraries/${target}")
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${_bin}"
        RUNTIME_OUTPUT_DIRECTORY "${_bin}"
        ARCHIVE_OUTPUT_DIRECTORY "${_imports}")
    foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER ${_cfg} _CFG)
        set_target_properties(${target} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${_CFG} "${_bin}"
            RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${_bin}"
            ARCHIVE_OUTPUT_DIRECTORY_${_CFG} "${_imports}")
    endforeach()
endfunction()

# Writes <root>/shard-plugin.json from the arguments (no MANIFEST). Used by shard_sdk_add_plugin and by the engine's own shard_add_plugin.
#   shard_sdk_write_manifest(TARGET t ROOT dir PLATFORM p ID id NAME n VERSION v KIND k [MODULE_KIND mk] [ABI c1] HOSTS ... DEPENDS ...
#                            ENGINE_REQUIRES ... AFFECTS true|false [CONTENT] [DESCRIPTION d])
function(shard_sdk_write_manifest)
    set(options CONTENT)
    set(one TARGET ROOT PLATFORM ID NAME VERSION KIND MODULE_KIND ABI AFFECTS DESCRIPTION BINARY_KEY BINARY_FILE TIER)
    set(multi HOSTS DEPENDS ENGINE_REQUIRES PERMISSIONS)
    cmake_parse_arguments(M "${options}" "${one}" "${multi}" ${ARGN})

    set(_hosts "")
    foreach(_h ${M_HOSTS})
        if(_hosts)
            string(APPEND _hosts ", ")
        endif()
        string(APPEND _hosts "\"${_h}\"")
    endforeach()

    # dependencies : "id", "id@range", "id?" or "id?@range"
    set(_deps "")
    foreach(_d ${M_DEPENDS})
        string(REGEX MATCH "^([^@?]+)(\\?)?(@(.+))?$" _m "${_d}")
        set(_id "${CMAKE_MATCH_1}")
        set(_optional "false")
        if(CMAKE_MATCH_2)
            set(_optional "true")
        endif()
        set(_range "*")
        if(CMAKE_MATCH_4)
            set(_range "${CMAKE_MATCH_4}")
        endif()
        if(_deps)
            string(APPEND _deps ",\n    ")
        endif()
        string(APPEND _deps "{ \"id\": \"${_id}\", \"version\": \"${_range}\", \"optional\": ${_optional} }")
    endforeach()

    set(_requires "")
    foreach(_r ${M_ENGINE_REQUIRES})
        if(_requires)
            string(APPEND _requires ", ")
        endif()
        string(APPEND _requires "\"${_r}\"")
    endforeach()

    set(_moduleKind "")
    if(M_MODULE_KIND)
        set(_moduleKind "\n  \"moduleKind\": \"${M_MODULE_KIND}\",")
    endif()
    set(_content "")
    if(M_CONTENT)
        set(_content ",\n  \"content\": { \"mount\": \"content\" }")
    endif()
    set(_abi "")
    if(M_ABI)
        set(_abi "\n  \"abi\": \"${M_ABI}\",")
    endif()
    set(_description "")
    if(M_DESCRIPTION)
        set(_description "\n  \"description\": \"${M_DESCRIPTION}\",")
    endif()

    set(_tier "")
    if(M_TIER)
        set(_tier "
  \"tier\": ${M_TIER},")
    endif()
    set(_permissions "")
    if(M_PERMISSIONS)
        set(_list "")
        foreach(_p ${M_PERMISSIONS})
            if(_list)
                string(APPEND _list ", ")
            endif()
            string(APPEND _list "\"${_p}\"")
        endforeach()
        set(_permissions "
  \"permissions\": [${_list}],")
    endif()
    set(_binaries "\"${M_PLATFORM}\": \"bin/${M_PLATFORM}/$<TARGET_FILE_NAME:${M_TARGET}>\"")
    if(M_BINARY_KEY)
        set(_binaries "\"${M_BINARY_KEY}\": \"${M_BINARY_FILE}\"")
    endif()
    file(GENERATE OUTPUT "${M_ROOT}/shard-plugin.json" CONTENT
"{
  \"schema\": 1,
  \"id\": \"${M_ID}\",
  \"name\": \"${M_NAME}\",${_description}
  \"version\": \"${M_VERSION}\",
  \"kind\": \"${M_KIND}\",${_moduleKind}
  \"linkage\": \"dynamic\",${_abi}${_tier}${_permissions}
  \"hosts\": [${_hosts}],
  \"engine\": { \"api\": \"1.0\", \"requires\": [${_requires}] },
  \"binaries\": { ${_binaries} },
  \"dependencies\": [
    ${_deps}
  ],
  \"simulation\": { \"affects\": ${M_AFFECTS}, \"deterministic\": true }${_content}
}
")
endfunction()

function(shard_sdk_add_plugin target)
    set(options AFFECTS_SIMULATION NO_SIMULATION PACKAGE)
    set(one ID NAME VERSION KIND MODULE_KIND CONTENT_DIR OUTPUT_DIR DESCRIPTION MANIFEST TARGET)
    set(multi SOURCES SOURCE_DIRS HOSTS DEPENDS ENGINE_REQUIRES PERMISSIONS)
    cmake_parse_arguments(P "${options}" "${one}" "${multi}" ${ARGN})

    # A game is a folder of files: the ones under SOURCE_DIRS, found again by CMake whenever one is added or removed
    foreach(_dir ${P_SOURCE_DIRS})
        if(NOT IS_ABSOLUTE "${_dir}")
            set(_dir "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}")
        endif()
        file(GLOB_RECURSE _found CONFIGURE_DEPENDS "${_dir}/*.cpp" "${_dir}/*.cc" "${_dir}/*.cxx" "${_dir}/*.c")
        list(APPEND P_SOURCES ${_found})
    endforeach()
    if(NOT P_SOURCES)
        message(FATAL_ERROR "shard_sdk_add_plugin(${target}) : no source file (SOURCES, or SOURCE_DIRS with files in them)")
    endif()
    if(NOT SHARD_SDK_INCLUDE_DIR)
        get_filename_component(SHARD_SDK_INCLUDE_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../include" ABSOLUTE)
    endif()
    if(NOT P_OUTPUT_DIR)
        set(P_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins")
    endif()

    # The id and version : from the manifest given, or from the arguments
    if(P_MANIFEST)
        if(NOT IS_ABSOLUTE "${P_MANIFEST}")
            set(P_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/${P_MANIFEST}")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${P_MANIFEST}")
        file(READ "${P_MANIFEST}" _manifest)
        string(JSON P_ID ERROR_VARIABLE _err GET "${_manifest}" id)
        if(_err)
            message(FATAL_ERROR "shard_sdk_add_plugin(${target}) : ${P_MANIFEST} has no \"id\" (${_err})")
        endif()
    elseif(NOT P_ID OR NOT P_VERSION)
        message(FATAL_ERROR "shard_sdk_add_plugin(${target}) needs MANIFEST, or ID and VERSION")
    endif()

    shard_sdk_platform(_platform)
    set(_root "${P_OUTPUT_DIR}/${P_ID}")

    set(_wasm FALSE)
    if(P_TARGET STREQUAL "wasm")
        set(_wasm TRUE)
    elseif(P_TARGET AND NOT P_TARGET STREQUAL "native")
        message(FATAL_ERROR "shard_sdk_add_plugin(${target}) : TARGET is wasm or native")
    endif()

    if(_wasm)
        # A sandboxed plugin : one module for every platform, next to its manifest
        shard_sdk_add_wasm_module(${target} OUTPUT "${_root}/plugin.wasm" SOURCES ${P_SOURCES} INCLUDE_DIRS "${SHARD_SDK_INCLUDE_DIR}")
    else()
        add_library(${target} SHARED ${P_SOURCES})
        target_include_directories(${target} PRIVATE "${SHARD_SDK_INCLUDE_DIR}")
        set_target_properties(${target} PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON PREFIX "")
        get_target_property(_cxx ${target} CXX_STANDARD)
        if(NOT _cxx)
            set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
        endif()
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -pedantic)
            if(MINGW)
                # The plugin carries its C and C++ runtimes : nothing to install next to the engine for it
                target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
            endif()
        endif()
        shard_sdk_place_binary(${target} "${_root}" "${_platform}")

    endif()

    if(P_MANIFEST)
        # The manifest as written, plus what only the build knows
        if(_wasm)
            string(JSON _manifest SET "${_manifest}" abi "\"wasm1\"")
            string(JSON _manifest SET "${_manifest}" tier "2")
            string(JSON _manifest SET "${_manifest}" linkage "\"dynamic\"")
            string(JSON _manifest SET "${_manifest}" binaries "{ \"wasm\": \"plugin.wasm\" }")
        else()
            string(JSON _manifest SET "${_manifest}" abi "\"c1\"")
            string(JSON _manifest SET "${_manifest}" linkage "\"dynamic\"")
            string(JSON _manifest SET "${_manifest}" binaries "{ \"${_platform}\": \"bin/${_platform}/$<TARGET_FILE_NAME:${target}>\" }")
        endif()
        file(GENERATE OUTPUT "${_root}/shard-plugin.json" CONTENT "${_manifest}\n")
    else()
        if(NOT P_NAME)
            set(P_NAME ${target})
        endif()
        if(NOT P_KIND)
            set(P_KIND plugin)
        endif()
        if(P_KIND STREQUAL "module" AND NOT P_MODULE_KIND)
            set(P_MODULE_KIND code)
        endif()
        if(NOT P_HOSTS)
            set(P_HOSTS engine)
        endif()
        set(_affects "false")
        if(P_AFFECTS_SIMULATION OR (P_KIND STREQUAL "module" AND NOT P_NO_SIMULATION))
            set(_affects "true")
        endif()
        set(_content_flag "")
        if(P_CONTENT_DIR)
            set(_content_flag CONTENT)
        endif()
        set(_abi_name c1)
        set(_extra "")
        if(_wasm)
            set(_abi_name wasm1)
            set(_extra TIER 2 BINARY_KEY wasm BINARY_FILE plugin.wasm PERMISSIONS ${P_PERMISSIONS})
        endif()
        shard_sdk_write_manifest(TARGET ${target} ROOT "${_root}" PLATFORM ${_platform} ID ${P_ID} NAME ${P_NAME} VERSION ${P_VERSION} ${_extra}
            KIND ${P_KIND} MODULE_KIND "${P_MODULE_KIND}" ABI ${_abi_name} HOSTS ${P_HOSTS} DEPENDS ${P_DEPENDS} ENGINE_REQUIRES ${P_ENGINE_REQUIRES}
            AFFECTS ${_affects} DESCRIPTION "${P_DESCRIPTION}" ${_content_flag})
    endif()

    if(P_CONTENT_DIR AND _wasm)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/${P_CONTENT_DIR}" "${_root}/content"
            COMMENT "Copying the content of ${P_ID}")
    elseif(P_CONTENT_DIR)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/${P_CONTENT_DIR}" "${_root}/content"
            COMMENT "Copying the content of ${P_ID}")
    endif()

    if(P_PACKAGE)
        if(NOT SHARD_PKG_EXECUTABLE)
            find_program(SHARD_PKG_EXECUTABLE NAMES ShardPkg shardpkg HINTS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../bin")
        endif()
        if(SHARD_PKG_EXECUTABLE)
            add_custom_target(${target}_package
                COMMAND "${SHARD_PKG_EXECUTABLE}" pack "${_root}" --out "${P_OUTPUT_DIR}/${P_ID}.shardplugin"
                DEPENDS ${target}
                COMMENT "Packing ${P_ID} into ${P_ID}.shardplugin")
        else()
            message(WARNING "shard_sdk_add_plugin(${target}) : PACKAGE needs ShardPkg (set SHARD_PKG_EXECUTABLE) : no ${target}_package target")
        endif()
    endif()

    set_property(TARGET ${target} PROPERTY SHARD_PLUGIN_ID "${P_ID}")
    set_property(TARGET ${target} PROPERTY SHARD_PLUGIN_DIR "${_root}")
endfunction()
