# shard_add_plugin : builds a plugin for Shard and writes its shard-plugin.json next to it.
#
#   shard_add_plugin(
#       NAME         weather                 # the CMake target
#       ID           com.acme.weather        # the plugin id (reverse DNS, lowercase)
#       VERSION      1.4.2
#       SOURCES      weather.cpp ...
#       [KIND        plugin|module]          # default : plugin
#       [MODULE_KIND code|genre|mod]         # default : code, when KIND is module
#       [HOSTS       engine player headless] # default : engine
#       [DEPENDS     shard.audio "com.acme.clouds@^2.1" ...]   # "id" or "id@range" ; append "?" to the id for an optional one
#       [ENGINE_REQUIRES largeWorld determinism]
#       [EDITOR_UI]                          # the plugin draws with ImGui (editor windows) : ImGui is compiled into it, see engine/plugins/editor_imgui.hpp
#       [AFFECTS_SIMULATION]                 # the plugin changes what the simulation computes (default for modules)
#       [CONTENT_DIR content]                # a folder of the source tree copied next to the plugin as its content
#       [OUTPUT_DIR  <dir>]                  # where plugin folders go (default : <build>/plugins)
#   )
#
# The plugin is laid out the way the engine finds it :
#   <OUTPUT_DIR>/<ID>/shard-plugin.json
#   <OUTPUT_DIR>/<ID>/bin/<platform>/<library>
#   <OUTPUT_DIR>/<ID>/content/...
# It links the engine, so it sees the same headers as the game module (BUILD_GAME), and exports its entry point with SHARD_PLUGIN.

function(shard_add_plugin)
    set(options AFFECTS_SIMULATION NO_SIMULATION EDITOR_UI)
    set(one NAME ID VERSION KIND MODULE_KIND CONTENT_DIR OUTPUT_DIR DESCRIPTION)
    set(multi SOURCES HOSTS DEPENDS ENGINE_REQUIRES)
    cmake_parse_arguments(P "${options}" "${one}" "${multi}" ${ARGN})

    if(NOT P_NAME OR NOT P_ID OR NOT P_VERSION)
        message(FATAL_ERROR "shard_add_plugin needs NAME, ID and VERSION")
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
    if(NOT P_OUTPUT_DIR)
        set(P_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins")
    endif()

    # The platform key the manifest names the binary under (same as CurrentPlatformKey() in manifest.cpp)
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" _os)
    if(_os STREQUAL "darwin")
        set(_os macos)
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
        set(_arch arm64)
    else()
        set(_arch x86_64)
    endif()
    set(_platform "${_os}-${_arch}")

    set(_root "${P_OUTPUT_DIR}/${P_ID}")
    set(_bin "${_root}/bin/${_platform}")

    set(_imgui_sources "")
    if(P_EDITOR_UI)
        set(_imgui_sources
            ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui.cpp
            ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui_widgets.cpp
            ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui_tables.cpp
            ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui_draw.cpp)
    endif()
    add_library(${P_NAME} SHARED ${P_SOURCES} ${_imgui_sources})
    target_compile_definitions(${P_NAME} PRIVATE BUILD_GAME)
    target_include_directories(${P_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/
        ${CMAKE_SOURCE_DIR}/submodules/json/single_include/
        ${CMAKE_SOURCE_DIR}/submodules/jolt/
        ${CMAKE_SOURCE_DIR}/submodules/
        ${CMAKE_SOURCE_DIR}/submodules/glm
        ${CMAKE_SOURCE_DIR}/submodules/freetype/include/
    )
    if(P_EDITOR_UI)
        target_include_directories(${P_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/submodules/imgui/)
        set_source_files_properties(${_imgui_sources} PROPERTIES COMPILE_OPTIONS "-w")
    endif()
    target_link_libraries(${P_NAME} PRIVATE Engine)
    set_target_properties(${P_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${_bin}"
        RUNTIME_OUTPUT_DIRECTORY "${_bin}"
        ARCHIVE_OUTPUT_DIRECTORY "${_bin}"
    )
    foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER ${_cfg} _CFG)
        set_target_properties(${P_NAME} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${_CFG} "${_bin}"
            RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${_bin}"
            ARCHIVE_OUTPUT_DIRECTORY_${_CFG} "${_bin}"
        )
    endforeach()

    # hosts -> JSON array
    set(_hosts "")
    foreach(_h ${P_HOSTS})
        if(_hosts)
            string(APPEND _hosts ", ")
        endif()
        string(APPEND _hosts "\"${_h}\"")
    endforeach()

    # dependencies : "id", "id@range", "id?" or "id?@range"
    set(_deps "")
    foreach(_d ${P_DEPENDS})
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
    foreach(_r ${P_ENGINE_REQUIRES})
        if(_requires)
            string(APPEND _requires ", ")
        endif()
        string(APPEND _requires "\"${_r}\"")
    endforeach()

    set(_affects "false")
    if(P_AFFECTS_SIMULATION OR (P_KIND STREQUAL "module" AND NOT P_NO_SIMULATION))
        set(_affects "true")
    endif()

    set(_moduleKind "")
    if(P_MODULE_KIND)
        set(_moduleKind "\n  \"moduleKind\": \"${P_MODULE_KIND}\",")
    endif()
    set(_content "")
    if(P_CONTENT_DIR)
        set(_content ",\n  \"content\": { \"mount\": \"content\" }")
    endif()
    set(_description "")
    if(P_DESCRIPTION)
        set(_description "\n  \"description\": \"${P_DESCRIPTION}\",")
    endif()

    file(GENERATE OUTPUT "${_root}/shard-plugin.json" CONTENT
"{
  \"schema\": 1,
  \"id\": \"${P_ID}\",
  \"name\": \"${P_NAME}\",${_description}
  \"version\": \"${P_VERSION}\",
  \"kind\": \"${P_KIND}\",${_moduleKind}
  \"linkage\": \"dynamic\",
  \"hosts\": [${_hosts}],
  \"engine\": { \"api\": \"1.0\", \"requires\": [${_requires}] },
  \"binaries\": { \"${_platform}\": \"bin/${_platform}/$<TARGET_FILE_NAME:${P_NAME}>\" },
  \"dependencies\": [
    ${_deps}
  ],
  \"simulation\": { \"affects\": ${_affects}, \"deterministic\": true }${_content}
}
")

    if(P_CONTENT_DIR)
        add_custom_command(TARGET ${P_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/${P_CONTENT_DIR}" "${_root}/content"
            COMMENT "Copying the content of ${P_ID}")
    endif()

    if(COMMAND shard_enable_warnings)
        shard_enable_warnings(${P_NAME})
    endif()
endfunction()
