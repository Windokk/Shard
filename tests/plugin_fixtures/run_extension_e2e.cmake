# Runs ShardHeadless with the test plugin and checks that the engine took what the plugin added through the extension points :
# a render feature, a render pipeline, an asset type, an importer. Variables : HEADLESS GAME PLUGIN PROJECT SAMPLE OUT
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
file(WRITE "${OUT}/script.txt"
"3 render.features
3 shardtest.assetkind
4 asset.import ${SAMPLE} ${OUT}/imported
5 render.pipeline shard-test-pipeline
7 render.features
8 plugin.disable shard.test.plugin
9 render.features
10 render.pipeline
")
execute_process(
    COMMAND "${HEADLESS}" --game "${GAME}" --plugin "${PLUGIN}" --project "${PROJECT}" --frames 14 --script "${OUT}/script.txt"
    WORKING_DIRECTORY "${WORKDIR}"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE result TIMEOUT 120)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "ShardHeadless exited with ${result}\n${output}\n${errors}")
endif()

set(expected
    "Asset type : shard-test-asset"
    "[TESTPLUGIN] feature activated"
    "render.features : features : shard-test-feature"
    "imported by the test plugin"
    "[TESTPLUGIN] pipeline activated"
    "Render pipeline : shard-test-pipeline"
    "[TESTPLUGIN] pipeline deactivated"
    "[TESTPLUGIN] feature deactivated"
    "render.features : features :\n"
    "render.pipeline = forward+")
foreach(line IN LISTS expected)
    string(FIND "${output}" "${line}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "missing from the output : ${line}\n${output}")
    endif()
endforeach()

file(READ "${OUT}/imported/imported.txt" imported)
if(NOT imported MATCHES "HELLO SHARD IMPORTER")
    message(FATAL_ERROR "the importer's output is not what it should be : ${imported}")
endif()
