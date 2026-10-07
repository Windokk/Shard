# ShardReflect : the reflection generator (clang tooling). It reads the CLASS()/STRUCT()/FIELD() annotated headers
# and writes the *.reflection.hpp files next to them. Needs LLVM + Clang + zlib : when they are missing the tool is
# skipped (the generated files are committed, only a change of a reflected class needs the tool).

find_package(ZLIB QUIET)
find_package(LLVM QUIET CONFIG)
find_package(Clang QUIET CONFIG)

if(NOT (ZLIB_FOUND AND LLVM_FOUND AND Clang_FOUND))
    message(STATUS "ShardReflect skipped : LLVM / Clang / zlib not found")
    return()
endif()

message(STATUS "ShardReflect : LLVM ${LLVM_PACKAGE_VERSION} (${LLVM_DIR})")

if(LLVM_LINK_LLVM_DYLIB)
    set(SHARD_REFLECT_LLVM_LIBS LLVM)
else()
    llvm_map_components_to_libnames(SHARD_REFLECT_LLVM_LIBS support core irreader)
endif()

add_executable(ShardReflect src/main.cpp)

target_include_directories(ShardReflect SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(ShardReflect PRIVATE ${LLVM_DEFINITIONS})

target_link_libraries(ShardReflect PRIVATE
    clangTooling
    clangASTMatchers
    clangAST
    clangBasic
    clangFrontend
    ${SHARD_REFLECT_LLVM_LIBS}
)

# build/tools/ShardReflect(.exe) : where build.sh / build.bat run it from
set_target_properties(ShardReflect PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}/tools")
foreach(OUTPUTCONFIG Debug Release RelWithDebInfo MinSizeRel)
    set_target_properties(ShardReflect PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${OUTPUT_DIR}/tools")
endforeach()
