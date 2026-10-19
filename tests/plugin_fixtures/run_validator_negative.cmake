# A project with a database entry that has no file, a world with the wrong shape, and a plugin that cannot load : the validator has to say so and exit with 1.
# Variables : VALIDATOR OUT
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/resources" "${OUT}/plugins/broken")
file(WRITE "${OUT}/project.json" "{\"versionMajor\":0,\"projectResources\":\"resources\",\"pluginsFolder\":\"plugins\",\"assetDatabase\":\"db.json\",\"buildSettings\":[\"a.world\",\"gone.world\"]}")
file(WRITE "${OUT}/db.json" "{\"a.world\":{\"id\":1},\"ghost.png\":{\"id\":2},\"dup.world\":{\"id\":2}}")
file(WRITE "${OUT}/resources/a.world" "{\"actors\":[{\"components\":[{\"nottype\":1}]}]}")
file(WRITE "${OUT}/resources/dup.world" "{\"name\":\"dup\",\"actors\":[]}")
file(WRITE "${OUT}/plugins/broken/shard-plugin.json" "{\"id\":\"Bad Id\"}")
execute_process(COMMAND "${VALIDATOR}" --project "${OUT}/project.json"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE result)
if(result EQUAL 0)
    message(FATAL_ERROR "the validator accepted a broken project\n${output}\n${errors}")
endif()
foreach(expected
        "ghost.png"                    # in the database, not on disk
        "gone.world"                   # in the build list, not on disk
        "used twice"                   # the same asset id twice
        "a world needs a name"
        "has no name")                 # an actor with no name
    string(FIND "${errors}" "${expected}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "the validator did not report : ${expected}\n${errors}")
    endif()
endforeach()
