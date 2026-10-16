set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/submodules/googletest ${CMAKE_BINARY_DIR}/submodules/googletest SYSTEM)

file(GLOB_RECURSE SHARD_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(ShardTests ${SHARD_TEST_SOURCES})

target_include_directories(ShardTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ShardTests PRIVATE Engine gtest gtest_main)
target_compile_definitions(ShardTests PRIVATE BUILD_ENGINE SHARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(GoogleTest)
# PRE_TEST: discover at test time, so a missing runtime DLL fails a test run instead of the build
# (a failed post-build discovery also deletes ShardTests.exe).
gtest_discover_tests(ShardTests DISCOVERY_MODE PRE_TEST)

# The simulation regression test : the headless runner plays a recorded replay of tests/determinism back and checks that the
# state of the simulation hashes to what the replay says, every 60 frames. No window, no GPU, no sound card : it runs the same
# on a developer's machine and on the CI, on Windows and on Linux, which is what the replay has to prove.
# To record it again after a change that is meant to alter the simulation :
#   ShardHeadless --game <GameModule> --project tests/determinism/project/determinism_project.json
#                 --replay tests/determinism/golden/determinism.replay --write-replay tests/determinism/golden/determinism.replay
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
