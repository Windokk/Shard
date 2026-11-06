execute_process(
    COMMAND "${HEADLESS}" --check-plugins --game "${GAME}" --project "${PROJECT}" --frames 180
    WORKING_DIRECTORY "${WORKDIR}"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE result TIMEOUT 300)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "--check-plugins failed with ${result}\n${output}\n${errors}")
endif()
if(NOT output MATCHES "same state with 1 and [0-9]+ job threads")
    message(FATAL_ERROR "unexpected output\n${output}")
endif()
