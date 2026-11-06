set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/submodules/googletest ${CMAKE_BINARY_DIR}/submodules/googletest SYSTEM)

file(GLOB_RECURSE SHARD_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
list(FILTER SHARD_TEST_SOURCES EXCLUDE REGEX "/plugin_fixtures/")

shard_add_plugin(
    NAME         ShardTestEditorPlugin
    ID           shard.test.editor
    VERSION      1.0.0
    HOSTS        editor
    EDITOR_UI
    SOURCES      ${CMAKE_CURRENT_SOURCE_DIR}/plugin_fixtures/editor_plugin/editor_plugin.cpp
    OUTPUT_DIR   ${CMAKE_BINARY_DIR}/test_plugins
)

shard_add_plugin(
    NAME         ShardTestPlugin
    ID           shard.test.plugin
    VERSION      1.0.0
    KIND         module
    MODULE_KIND  code
    HOSTS        engine
    SOURCES      ${CMAKE_CURRENT_SOURCE_DIR}/plugin_fixtures/test_plugin/test_plugin.cpp
    OUTPUT_DIR   ${CMAKE_BINARY_DIR}/test_plugins
)

shard_add_plugin(
    NAME         ShardTestCPlugin
    ID           shard.test.cplugin
    VERSION      1.0.0
    KIND         module
    MODULE_KIND  code
    HOSTS        engine
    ABI          c1
    SOURCES      ${CMAKE_CURRENT_SOURCE_DIR}/plugin_fixtures/c_plugin/c_plugin.c
    CONTENT_DIR  plugin_fixtures/c_plugin/content
    OUTPUT_DIR   ${CMAKE_BINARY_DIR}/test_plugins
)
set_target_properties(ShardTestCPlugin PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

shard_add_plugin(
    NAME         ShardTestCConsumer
    ID           shard.test.cconsumer
    VERSION      1.0.0
    HOSTS        engine
    ABI          c1
    SOURCES      ${CMAKE_CURRENT_SOURCE_DIR}/plugin_fixtures/c_consumer/c_consumer.c
    OUTPUT_DIR   ${CMAKE_BINARY_DIR}/test_plugins
)
set_target_properties(ShardTestCConsumer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

shard_add_plugin(
    NAME         ShardTestSdkSpin
    ID           shard.test.spin
    VERSION      1.0.0
    HOSTS        engine
    ABI          c1
    SOURCES      ${CMAKE_CURRENT_SOURCE_DIR}/plugin_fixtures/sdk_spin/spin.cpp
    OUTPUT_DIR   ${CMAKE_BINARY_DIR}/test_plugins
)
set_target_properties(ShardTestSdkSpin PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

file(GLOB SHARD_TEST_GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/plugin_fixtures/game_scripts/src/*.cpp)
shard_add_plugin(
    NAME         ShardTestGameScripts
    ID           shard.test.gamescripts
    VERSION      1.0.0
    HOSTS        engine
    ABI          c1
    SOURCES      ${SHARD_TEST_GAME_SOURCES}
    CONTENT_DIR  plugin_fixtures/game_scripts/content
    OUTPUT_DIR   ${CMAKE_BINARY_DIR}/test_plugins
)
set_target_properties(ShardTestGameScripts PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

shard_add_plugin(
    NAME         ShardTestAbi10
    ID           shard.test.abi10
    VERSION      1.0.0
    HOSTS        engine
    ABI          c1
    SOURCES      ${CMAKE_CURRENT_SOURCE_DIR}/plugin_fixtures/abi_1_0/abi_1_0_plugin.c
    OUTPUT_DIR   ${CMAKE_BINARY_DIR}/test_plugins
)
target_include_directories(ShardTestAbi10 BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/plugin_fixtures/abi_1_0/include)
set_target_properties(ShardTestAbi10 PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

add_executable(ShardTests ${SHARD_TEST_SOURCES})

target_include_directories(ShardTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ShardTests PRIVATE Engine gtest gtest_main)
target_compile_definitions(ShardTests PRIVATE BUILD_ENGINE SHARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}" SHARD_TEST_PLUGIN_DIR="${CMAKE_BINARY_DIR}/test_plugins" SHARD_GAME_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins/game")
add_dependencies(ShardTests ShardTestPlugin ShardTestEditorPlugin ShardTestCPlugin ShardTestCConsumer ShardTestSdkSpin ShardTestGameScripts ShardTestAbi10 GameModule)

include(GoogleTest)
# PRE_TEST: a missing runtime DLL fails a test run instead of the build (a failed post-build discovery also deletes ShardTests.exe).
gtest_discover_tests(ShardTests DISCOVERY_MODE PRE_TEST)

# Headless replay test of tests/determinism, state hash checked every 60 frames. To re-record after an intended change:
# ShardHeadless --game <GameModule> --project tests/determinism/project/determinism_project.json --replay tests/determinism/golden/determinism.replay --write-replay tests/determinism/golden/determinism.replay
add_test(NAME Determinism.Resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/engine_resources ${CMAKE_BINARY_DIR}/engine_resources)
set_tests_properties(Determinism.Resources PROPERTIES FIXTURES_SETUP determinism_engine_resources)

add_test(NAME Determinism.Replay
    COMMAND ShardHeadless
        --game $<TARGET_FILE:GameModule>
        --project ${CMAKE_SOURCE_DIR}/tests/determinism/project/determinism_project.json
        --replay ${CMAKE_SOURCE_DIR}/tests/determinism/golden/determinism.replay
        --require-checks
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(Determinism.Replay PROPERTIES FIXTURES_REQUIRED determinism_engine_resources TIMEOUT 120)

add_test(NAME Plugins.ExtensionPoints
    COMMAND ${CMAKE_COMMAND}
        -DHEADLESS=$<TARGET_FILE:ShardHeadless>
        -DGAME=$<TARGET_FILE:GameModule>
        -DPLUGIN=${CMAKE_BINARY_DIR}/test_plugins/shard.test.plugin
        -DPROJECT=${CMAKE_SOURCE_DIR}/tests/determinism/project/determinism_project.json
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/plugin_fixtures/sample.shardsrc
        -DOUT=${CMAKE_BINARY_DIR}/plugin_e2e
        -DWORKDIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/plugin_fixtures/run_extension_e2e.cmake)
set_tests_properties(Plugins.ExtensionPoints PROPERTIES FIXTURES_REQUIRED determinism_engine_resources TIMEOUT 180)

add_test(NAME Tools.AssetValidatorOnExampleProject
    COMMAND ShardAssetValidator --project ${CMAKE_SOURCE_DIR}/example_project/example_project.json --game $<TARGET_FILE:GameModule> --strict --quiet
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME Tools.AssetValidatorFindsProblems
    COMMAND ${CMAKE_COMMAND} -DVALIDATOR=$<TARGET_FILE:ShardAssetValidator> -DOUT=${CMAKE_BINARY_DIR}/validator_negative
        -P ${CMAKE_SOURCE_DIR}/tests/plugin_fixtures/run_validator_negative.cmake)

add_test(NAME Plugins.CheckPlugins
    COMMAND ${CMAKE_COMMAND} -DHEADLESS=$<TARGET_FILE:ShardHeadless> -DGAME=$<TARGET_FILE:GameModule>
        -DPROJECT=${CMAKE_SOURCE_DIR}/tests/determinism/project/determinism_project.json -DWORKDIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/plugin_fixtures/run_check_plugins.cmake)
set_tests_properties(Plugins.CheckPlugins PROPERTIES FIXTURES_REQUIRED determinism_engine_resources TIMEOUT 300)
add_test(NAME Plugins.ServerHandshake
    COMMAND ${CMAKE_COMMAND} -DHEADLESS=$<TARGET_FILE:ShardHeadless> -DGAME=$<TARGET_FILE:GameModule>
        -DPROJECT=${CMAKE_SOURCE_DIR}/tests/determinism/project/determinism_project.json -DWORKDIR=${CMAKE_BINARY_DIR} -DPORT=47321
        -P ${CMAKE_SOURCE_DIR}/tests/plugin_fixtures/run_check_server.cmake)
set_tests_properties(Plugins.ServerHandshake PROPERTIES FIXTURES_REQUIRED determinism_engine_resources TIMEOUT 200 RUN_SERIAL TRUE)

find_program(SHARD_WASM_CLANG NAMES clang)
if(SHARD_WASM_CLANG)
    add_test(NAME Wasm.SdkSignatures
        COMMAND ${CMAKE_COMMAND} -DCLANG=${SHARD_WASM_CLANG} -DSOURCE=${CMAKE_SOURCE_DIR}/tests/plugin_fixtures/wasm_plugin/wasm_plugin.c
            -DINCLUDE=${CMAKE_SOURCE_DIR}/sdk/include -DOUT=${CMAKE_BINARY_DIR}/wasm_plugin.s
            -P ${CMAKE_SOURCE_DIR}/tests/plugin_fixtures/run_wasm_signatures.cmake)
endif()
find_program(SHARD_WASM_LD NAMES wasm-ld wasm-ld-20 wasm-ld-19 wasm-ld-18)
if(SHARD_WASM_CLANG AND SHARD_WASM_LD)
    set(SHARD_TEST_WASM_DIR ${CMAKE_BINARY_DIR}/test_plugins/shard.test.wasm)
    file(MAKE_DIRECTORY ${SHARD_TEST_WASM_DIR}/content)
    file(WRITE ${SHARD_TEST_WASM_DIR}/content/hello.txt "hello")
    file(WRITE ${SHARD_TEST_WASM_DIR}/shard-plugin.json [=[{
  "schema": 1, "id": "shard.test.wasm", "version": "1.0.0", "kind": "module", "moduleKind": "code", "linkage": "dynamic", "hosts": ["engine"],
  "tier": 2, "abi": "wasm1", "binaries": { "wasm": "plugin.wasm" }, "permissions": ["fs.read:content", "input"], "content": { "mount": "content" }
}
]=])
    file(GLOB SHARD_SDK_C_HEADERS ${CMAKE_SOURCE_DIR}/sdk/include/shard/*.h)
    add_custom_command(OUTPUT ${SHARD_TEST_WASM_DIR}/plugin.wasm
        COMMAND ${SHARD_WASM_CLANG} --target=wasm32 -ffreestanding -nostdlib -fno-builtin -std=c11 -Wall -Wextra -pedantic -Werror -O2
            -I${CMAKE_SOURCE_DIR}/sdk/include -c ${CMAKE_SOURCE_DIR}/tests/plugin_fixtures/wasm_plugin/wasm_plugin.c -o ${CMAKE_BINARY_DIR}/wasm_plugin.o
        COMMAND ${SHARD_WASM_LD} --no-entry --export=shard_plugin_entry --export=shard_wasm_host_table --export=shard_wasm_ui_table --export=shard_wasm_gizmo_table --export-table -o ${SHARD_TEST_WASM_DIR}/plugin.wasm
            ${CMAKE_BINARY_DIR}/wasm_plugin.o
        DEPENDS ${CMAKE_SOURCE_DIR}/tests/plugin_fixtures/wasm_plugin/wasm_plugin.c ${SHARD_SDK_C_HEADERS}
        COMMENT "Building the sandboxed test plugin (wasm32)"
        VERBATIM)
    add_custom_target(ShardTestWasmPlugin ALL DEPENDS ${SHARD_TEST_WASM_DIR}/plugin.wasm)
    add_dependencies(ShardTests ShardTestWasmPlugin)
    target_compile_definitions(ShardTests PRIVATE SHARD_TEST_WASM_PLUGIN_DIR="${SHARD_TEST_WASM_DIR}")
endif()

set(SHARD_TEST_WASM_TOOLCHAIN "" CACHE PATH "wasi-sdk (34 or later) or wasi sysroot : adds the sandboxed build of the game fixture")
if(NOT SHARD_TEST_WASM_TOOLCHAIN AND DEFINED ENV{SHARD_WASM_TOOLCHAIN})
    set(SHARD_TEST_WASM_TOOLCHAIN "$ENV{SHARD_WASM_TOOLCHAIN}")
endif()
if(SHARD_TEST_WASM_TOOLCHAIN AND SHARD_WASM_CLANG AND SHARD_WASM_LD)
    include(${CMAKE_SOURCE_DIR}/sdk/cmake/ShardSDKPlugin.cmake)
    set(SHARD_WASM_TOOLCHAIN "${SHARD_TEST_WASM_TOOLCHAIN}")
    set(SHARD_TEST_WASM_GAME_SOURCES ${SHARD_TEST_GAME_SOURCES})
    list(FILTER SHARD_TEST_WASM_GAME_SOURCES EXCLUDE REGEX "(components_demo|services_demo|platform_demo)\\.cpp$")
    shard_sdk_add_plugin(ShardTestWasmGame
        TARGET       wasm
        ID           shard.test.wasmgame
        VERSION      1.0.0
        KIND         module
        MODULE_KIND  code
        HOSTS        engine
        SOURCES      ${SHARD_TEST_WASM_GAME_SOURCES}
        CONTENT_DIR  plugin_fixtures/game_scripts/content
        OUTPUT_DIR   ${CMAKE_BINARY_DIR}/test_plugins
        PERMISSIONS  fs.read:content input audio physics worlds.load actors.spawn editor)
    add_dependencies(ShardTests ShardTestWasmGame)
    target_compile_definitions(ShardTests PRIVATE SHARD_TEST_WASM_GAME_DIR="${CMAKE_BINARY_DIR}/test_plugins/shard.test.wasmgame")
endif()

set(SHARD_SDK_TEST_CXX "" CACHE STRING "C++ compiler for Sdk.OutOfTree (empty : the engine's)")
set(SHARD_SDK_TEST_VCVARS "" CACHE FILEPATH "vcvars64.bat of an MSVC installation : adds Sdk.OutOfTreeMsvc")
set(_sdk_cxx "${SHARD_SDK_TEST_CXX}")
if(NOT _sdk_cxx)
    set(_sdk_cxx "${CMAKE_CXX_COMPILER}")
endif()
set(_sdk_test_args
    -DENGINE_BUILD=${CMAKE_BINARY_DIR}
    -DHEADLESS=$<TARGET_FILE:ShardHeadless>
    -DGAME=$<TARGET_FILE:GameModule>
    -DPROJECT=${CMAKE_SOURCE_DIR}/tests/determinism/project/determinism_project.json
    -DWORKDIR=${CMAKE_BINARY_DIR})
add_test(NAME Sdk.OutOfTree
    COMMAND ${CMAKE_COMMAND} ${_sdk_test_args} -DWORK=${CMAKE_BINARY_DIR}/sdk_out_of_tree "-DGENERATOR=${CMAKE_GENERATOR}"
        -DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM} -DCXX=${_sdk_cxx}
        -P ${CMAKE_SOURCE_DIR}/tests/plugin_fixtures/run_sdk_out_of_tree.cmake)
set_tests_properties(Sdk.OutOfTree PROPERTIES FIXTURES_REQUIRED determinism_engine_resources TIMEOUT 400)
if(SHARD_TEST_WASM_TOOLCHAIN AND SHARD_WASM_CLANG AND SHARD_WASM_LD)
    add_test(NAME Sdk.OutOfTreeWasm
        COMMAND ${CMAKE_COMMAND} ${_sdk_test_args} -DWORK=${CMAKE_BINARY_DIR}/sdk_out_of_tree_wasm "-DGENERATOR=${CMAKE_GENERATOR}"
            -DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM} "-DTOOLCHAIN=${SHARD_TEST_WASM_TOOLCHAIN}" "-DBUILTINS=${SHARD_WASM_BUILTINS}"
            -P ${CMAKE_SOURCE_DIR}/tests/plugin_fixtures/run_sdk_out_of_tree_wasm.cmake)
    set_tests_properties(Sdk.OutOfTreeWasm PROPERTIES FIXTURES_REQUIRED determinism_engine_resources TIMEOUT 400)
endif()
if(SHARD_SDK_TEST_VCVARS)
    add_test(NAME Sdk.OutOfTreeMsvc
        COMMAND ${CMAKE_COMMAND} ${_sdk_test_args} -DWORK=${CMAKE_BINARY_DIR}/sdk_out_of_tree_msvc -DVCVARS=${SHARD_SDK_TEST_VCVARS}
            -P ${CMAKE_SOURCE_DIR}/tests/plugin_fixtures/run_sdk_out_of_tree.cmake)
    set_tests_properties(Sdk.OutOfTreeMsvc PROPERTIES FIXTURES_REQUIRED determinism_engine_resources TIMEOUT 400)
endif()

# docs/docs/api_reference.md is generated from the headers of the SDK (scripts/gen_api_reference.py) : it has to be what they give, and the
# revisions the headers announce have to be the ones the host offers. Regenerate it with the ShardApiReference target.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME Docs.ApiReference COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/scripts/gen_api_reference.py --check)
    # What the sandbox of tier 2 plugins agrees with the module on is generated from the same headers (shard_wasm_imports.h, wasm_layout_gen.hpp,
    # wasm_imports_gen.cpp) : it has to be what they give. Regenerate it with the ShardWasmImports target.
    add_test(NAME Wasm.GeneratedImports COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/scripts/gen_wasm_imports.py --check)
    add_custom_target(ShardWasmImports
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/scripts/gen_wasm_imports.py
        COMMENT "Writing the sandbox's imports from sdk/include"
        VERBATIM)
    add_custom_target(ShardApiReference
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/scripts/gen_api_reference.py
        COMMENT "Writing docs/docs/api_reference.md from sdk/include"
        VERBATIM)
endif()
