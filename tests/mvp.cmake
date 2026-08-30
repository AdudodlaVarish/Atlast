if(NOT DEFINED ATLAST_EXECUTABLE OR NOT DEFINED TEST_DIRECTORY)
    message(FATAL_ERROR "ATLAST_EXECUTABLE and TEST_DIRECTORY are required")
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
file(MAKE_DIRECTORY "${TEST_DIRECTORY}/source")

set(database "${TEST_DIRECTORY}/atlast.db")
set(source "${TEST_DIRECTORY}/source")

file(WRITE "${source}/network.txt"
    "Upload failed because of a connection timeout. Retry after one second.\n")
file(WRITE "${source}/migration.md"
    "# Database migration\nThe migration uses a shadow table.\n")
file(WRITE "${source}/ignored.bin" "connection timeout")

function(run_atlast expected_result output_variable)
    execute_process(
        COMMAND "${ATLAST_EXECUTABLE}" ${ARGN}
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT actual_result EQUAL expected_result)
        message(FATAL_ERROR
            "Command failed with ${actual_result}, expected ${expected_result}\n"
            "stdout:\n${output}\nstderr:\n${error}")
    endif()
    set(${output_variable} "${output}" PARENT_SCOPE)
endfunction()

run_atlast(0 first_index index "${source}" --db "${database}")
if(NOT first_index MATCHES "Indexed: 2")
    message(FATAL_ERROR "Expected two indexed files:\n${first_index}")
endif()

run_atlast(0 connection_search search "connection timeout" --db "${database}")
if(NOT connection_search MATCHES "network.txt" OR
   connection_search MATCHES "ignored.bin")
    message(FATAL_ERROR "Unexpected connection search:\n${connection_search}")
endif()

run_atlast(0 second_index index "${source}" --db "${database}")
if(NOT second_index MATCHES "Unchanged: 2")
    message(FATAL_ERROR "Expected unchanged files:\n${second_index}")
endif()

file(WRITE "${source}/network.txt"
    "Upload now succeeds. The replacement marker is hummingbird.\n")
file(REMOVE "${source}/migration.md")

run_atlast(0 third_index index "${source}" --db "${database}")
if(NOT third_index MATCHES "Indexed: 1" OR
   NOT third_index MATCHES "Removed: 1")
    message(FATAL_ERROR "Expected one update and one removal:\n${third_index}")
endif()

run_atlast(0 old_search search "connection" --db "${database}")
if(NOT old_search MATCHES "No results")
    message(FATAL_ERROR "Old content remained searchable:\n${old_search}")
endif()

run_atlast(0 new_search search "hummingbird" --limit 1 --db "${database}")
if(NOT new_search MATCHES "network.txt")
    message(FATAL_ERROR "Updated content was not searchable:\n${new_search}")
endif()

run_atlast(2 invalid_limit search "hummingbird" --limit 0 --db "${database}")

message(STATUS "Atlast MVP end-to-end test passed")
