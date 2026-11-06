# SDK outside the repo: install it alone, build the plugin template against it with no path into src/, load it headless and run the simulation twice (--check-plugins).
# With another compiler (CXX=..., or VCVARS=... for MSVC) it also shows the ABI does not depend on the compiler.

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

execute_process(COMMAND "${CMAKE_COMMAND}" --install "${ENGINE_BUILD}" --component sdk --prefix "${WORK}/install"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "installing the SDK failed\n${out}\n${err}")
endif()
set(sdk "${WORK}/install/sdk")
if(NOT EXISTS "${sdk}/include/shard/shard.h" OR NOT EXISTS "${sdk}/cmake/ShardSDKConfig.cmake")
    message(FATAL_ERROR "the installed SDK is incomplete : ${sdk}")
endif()

file(COPY "${sdk}/templates/plugin/" DESTINATION "${WORK}/src")

set(configure "${CMAKE_COMMAND}" -S "${WORK}/src" -B "${WORK}/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "-DCMAKE_PREFIX_PATH=${sdk}")
if(VCVARS)
    # MSVC gets its environment from vcvars64.bat, so the build runs through a batch file.
    file(TO_NATIVE_PATH "${VCVARS}" vcvars)
    file(WRITE "${WORK}/build.bat"
"@echo off
call \"${vcvars}\" >nul || exit /b 1
\"${CMAKE_COMMAND}\" -S \"${WORK}/src\" -B \"${WORK}/build\" -G \"NMake Makefiles\" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \"-DCMAKE_PREFIX_PATH=${sdk}\" || exit /b 1
\"${CMAKE_COMMAND}\" --build \"${WORK}/build\" || exit /b 1
\"${CMAKE_COMMAND}\" --build \"${WORK}/build\" --target template_plugin_package || exit /b 1
")
    execute_process(COMMAND cmd /c "${WORK}/build.bat" RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "building the template with MSVC failed\n${out}\n${err}")
    endif()
else()
    list(APPEND configure -G "${GENERATOR}")
    if(MAKE_PROGRAM)
        list(APPEND configure "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
    endif()
    if(CXX)
        list(APPEND configure "-DCMAKE_CXX_COMPILER=${CXX}")
    endif()
    execute_process(COMMAND ${configure} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "configuring the template failed\n${out}\n${err}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK}/build" RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "building the template failed\n${out}\n${err}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK}/build" --target template_plugin_package RESULT_VARIABLE result OUTPUT_VARIABLE pkg ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "packing the template with the SDK's ShardPkg failed\n${pkg}\n${err}")
    endif()
endif()
if(out MATCHES "[Ww]arning")
    message(FATAL_ERROR "the template does not build without a warning\n${out}")
endif()

# Compile commands must name the installed SDK, never the repository.
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
file(READ "${WORK}/build/compile_commands.json" commands)
# Some generators put include folders in response files (MinGW Makefiles: includes_CXX.rsp).
file(GLOB_RECURSE response_files "${WORK}/build/CMakeFiles/*.rsp")
foreach(response ${response_files})
    file(READ "${response}" text)
    string(APPEND commands "\n${text}")
endforeach()
string(REPLACE "\\\\" "/" commands "${commands}")
string(FIND "${commands}" "${repository}/sdk" in_repo_sdk)
string(FIND "${commands}" "${repository}/src" in_repo_src)
if(NOT in_repo_sdk EQUAL -1 OR NOT in_repo_src EQUAL -1)
    message(FATAL_ERROR "the template was compiled with a path into the repository\n${commands}")
endif()
string(FIND "${commands}" "${sdk}/include" in_sdk)
if(in_sdk EQUAL -1)
    message(FATAL_ERROR "the template was not compiled against the installed SDK\n${commands}")
endif()

set(plugin "${WORK}/build/plugins/com.example.template")
if(NOT EXISTS "${plugin}/shard-plugin.json")
    message(FATAL_ERROR "no plugin folder in ${WORK}/build/plugins")
endif()
if(NOT EXISTS "${sdk}/bin/ShardPkg${CMAKE_EXECUTABLE_SUFFIX}" AND NOT EXISTS "${sdk}/bin/ShardPkg.exe")
    message(FATAL_ERROR "the installed SDK has no bin/ShardPkg")
endif()
set(archive "${WORK}/build/plugins/com.example.template.shardplugin")
if(NOT EXISTS "${archive}")
    message(FATAL_ERROR "no ${archive}")
endif()
file(SIZE "${archive}" archive_size)
if(NOT archive_size GREATER 0)
    message(FATAL_ERROR "${archive} is empty")
endif()

execute_process(
    COMMAND "${HEADLESS}" --check-plugins --game "${GAME}" --project "${PROJECT}" --plugin "${plugin}" --frames 60
    WORKING_DIRECTORY "${WORKDIR}"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE result TIMEOUT 300)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "--check-plugins with the template failed with ${result}\n${output}\n${errors}")
endif()
if(NOT "${output}${errors}" MATCHES "\\[com\\.example\\.template\\] loaded")
    message(FATAL_ERROR "the template plugin did not load\n${output}\n${errors}")
endif()
if(NOT output MATCHES "same state with 1 and [0-9]+ job threads")
    message(FATAL_ERROR "unexpected output\n${output}")
endif()
message(STATUS "the template, built against the installed SDK, loaded and ran")
