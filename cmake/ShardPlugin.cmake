include(${CMAKE_CURRENT_LIST_DIR}/../sdk/cmake/ShardSDKPlugin.cmake)

function(shard_add_plugin)
    set(options AFFECTS_SIMULATION NO_SIMULATION EDITOR_UI)
    set(one NAME ID VERSION KIND MODULE_KIND CONTENT_DIR OUTPUT_DIR DESCRIPTION ABI)
    set(multi SOURCES HOSTS DEPENDS ENGINE_REQUIRES)
    cmake_parse_arguments(P "${options}" "${one}" "${multi}" ${ARGN})

    if(NOT P_NAME OR NOT P_ID OR NOT P_VERSION)
        message(FATAL_ERROR "shard_add_plugin needs NAME, ID and VERSION")
    endif()
    if(NOT P_ABI)
        set(P_ABI cpp)
    endif()
    if(NOT P_ABI STREQUAL "cpp" AND NOT P_ABI STREQUAL "c1")
        message(FATAL_ERROR "shard_add_plugin : ABI must be cpp or c1, not ${P_ABI}")
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

    shard_sdk_platform(_platform)
    set(_root "${P_OUTPUT_DIR}/${P_ID}")

    set(_imgui_sources "")
    if(P_EDITOR_UI)
        set(_imgui_sources
            ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui.cpp
            ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui_widgets.cpp
            ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui_tables.cpp
            ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui_draw.cpp)
    endif()
    add_library(${P_NAME} SHARED ${P_SOURCES} ${_imgui_sources})
    if(P_ABI STREQUAL "c1")
        target_include_directories(${P_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/sdk/include)
        set_target_properties(${P_NAME} PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    else()
        target_compile_definitions(${P_NAME} PRIVATE BUILD_GAME)
        target_include_directories(${P_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/
            ${CMAKE_SOURCE_DIR}/sdk/include/
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
    endif()
    shard_sdk_place_binary(${P_NAME} "${_root}" "${_platform}")

    set(_affects "false")
    if(P_AFFECTS_SIMULATION OR (P_KIND STREQUAL "module" AND NOT P_NO_SIMULATION))
        set(_affects "true")
    endif()
    set(_abi "")
    if(P_ABI STREQUAL "c1")
        set(_abi c1)
    endif()
    set(_content_flag "")
    if(P_CONTENT_DIR)
        set(_content_flag CONTENT)
    endif()
    shard_sdk_write_manifest(TARGET ${P_NAME} ROOT "${_root}" PLATFORM ${_platform} ID ${P_ID} NAME ${P_NAME} VERSION ${P_VERSION}
        KIND ${P_KIND} MODULE_KIND "${P_MODULE_KIND}" ABI "${_abi}" HOSTS ${P_HOSTS} DEPENDS ${P_DEPENDS} ENGINE_REQUIRES ${P_ENGINE_REQUIRES}
        AFFECTS ${_affects} DESCRIPTION "${P_DESCRIPTION}" ${_content_flag})

    if(P_CONTENT_DIR)
        add_custom_command(TARGET ${P_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/${P_CONTENT_DIR}" "${_root}/content"
            COMMENT "Copying the content of ${P_ID}")
    endif()

    if(COMMAND shard_enable_warnings AND P_ABI STREQUAL "cpp")
        shard_enable_warnings(${P_NAME})
    endif()
    if(P_ABI STREQUAL "c1")
        if(MSVC)
            target_compile_options(${P_NAME} PRIVATE /W4 /WX)
        else()
            target_compile_options(${P_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
        endif()
    endif()
endfunction()
