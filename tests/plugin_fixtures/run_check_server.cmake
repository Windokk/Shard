# A headless server answers the plugin handshake and a second headless process, with the same plugins, is accepted ; one that left the game out is refused.
# The two run at the same time (execute_process with two commands starts both). Variables : HEADLESS GAME PROJECT WORKDIR PORT
function(run_pair clientArgs expectedCode expectedText)
    execute_process(
        COMMAND "${HEADLESS}" --server --port ${PORT} --game "${GAME}" --project "${PROJECT}" --frames 600
        COMMAND "${HEADLESS}" --check-server 127.0.0.1:${PORT} --game "${GAME}" --project "${PROJECT}" ${clientArgs}
        WORKING_DIRECTORY "${WORKDIR}"
        RESULTS_VARIABLE results OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
    list(GET results 1 clientResult)
    if(NOT clientResult EQUAL ${expectedCode})
        message(FATAL_ERROR "the client ended with ${clientResult}, expected ${expectedCode}\n${output}\n${errors}")
    endif()
    string(FIND "${output}" "${expectedText}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "missing from the client's output : ${expectedText}\n${output}\n${errors}")
    endif()
endfunction()

run_pair("" 0 "[SERVER] accepted")
math(EXPR PORT "${PORT} + 1")
run_pair("--disable-plugin;game" 3 "[SERVER] refused")
