# Compiler warnings for Shard's own targets (never the submodules : their code is not ours to fix).
#
# shard_enable_warnings(<target>...) turns on -Wall -Wextra for the C++ of each target, and keeps the third party quiet :
#   - the include directories of the submodules become SYSTEM (-isystem), so their headers do not warn in our files
#   - the third party sources compiled straight into one of our targets (imgui, ImGuizmo, volk, ufbx, glad...) get -w
# The submodules added with add_subdirectory(... SYSTEM) in the root CMakeLists.txt already give SYSTEM include directories.

option(SHARD_WARNINGS_AS_ERRORS "Treat the warnings of Shard's own code as errors" OFF)

# What is not our code : the submodules, the vendored third-party folders and the generated GL loader
set(SHARD_THIRD_PARTY_REGEX "/submodules/|/third-party/|/renderer/rhi/backends/glad/")

function(shard_enable_warnings)
    foreach(target IN LISTS ARGN)
        if(MSVC)
            # Not tried yet : the MinGW / GCC build is the one used. /W4 is the MSVC counterpart of -Wall -Wextra.
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
            if(SHARD_WARNINGS_AS_ERRORS)
                target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/WX>)
            endif()
        else()
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-missing-field-initializers>)
            if(SHARD_WARNINGS_AS_ERRORS)
                target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Werror>)
            endif()
        endif()

        # Third party include directories -> SYSTEM. A directory given both ways is searched as a system one (GCC and Clang
        # drop the -I), so adding them again is enough.
        foreach(property INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES)
            get_target_property(directories ${target} ${property})
            if(NOT directories)
                continue()
            endif()
            set(thirdParty "")
            foreach(directory IN LISTS directories)
                if(directory MATCHES "${SHARD_THIRD_PARTY_REGEX}" AND NOT directory MATCHES "^\\$<")
                    list(APPEND thirdParty "${directory}")
                endif()
            endforeach()
            if(thirdParty)
                if(property STREQUAL "INCLUDE_DIRECTORIES")
                    target_include_directories(${target} SYSTEM PRIVATE ${thirdParty})
                else()
                    target_include_directories(${target} SYSTEM INTERFACE ${thirdParty})
                endif()
            endif()
        endforeach()

        # Third party sources compiled into the target
        get_target_property(sources ${target} SOURCES)
        if(sources)
            set(thirdPartySources "")
            foreach(source IN LISTS sources)
                if(source MATCHES "${SHARD_THIRD_PARTY_REGEX}")
                    list(APPEND thirdPartySources "${source}")
                endif()
            endforeach()
            if(thirdPartySources)
                set_source_files_properties(${thirdPartySources} TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS "$<IF:$<BOOL:${MSVC}>,/w,-w>")
            endif()
        endif()
    endforeach()
endfunction()
