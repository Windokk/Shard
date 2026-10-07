set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/submodules/googletest ${CMAKE_BINARY_DIR}/submodules/googletest)

file(GLOB_RECURSE SHARD_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(ShardTests ${SHARD_TEST_SOURCES})

target_include_directories(ShardTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ShardTests PRIVATE Engine gtest gtest_main)
target_compile_definitions(ShardTests PRIVATE BUILD_ENGINE)

include(GoogleTest)
# PRE_TEST: discover at test time, so a missing runtime DLL fails a test run instead of the build
# (a failed post-build discovery also deletes ShardTests.exe).
gtest_discover_tests(ShardTests DISCOVERY_MODE PRE_TEST)
