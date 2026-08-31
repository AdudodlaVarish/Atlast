if(NOT DEFINED ATLAST_EXECUTABLE OR NOT DEFINED TEST_DIRECTORY)
    message(FATAL_ERROR "ATLAST_EXECUTABLE and TEST_DIRECTORY are required")
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
file(MAKE_DIRECTORY "${TEST_DIRECTORY}/source")
file(MAKE_DIRECTORY "${TEST_DIRECTORY}/source-two")

set(database "${TEST_DIRECTORY}/atlast.db")
set(source "${TEST_DIRECTORY}/source")
set(source_two "${TEST_DIRECTORY}/source-two")

file(WRITE "${source}/network.txt"
    "Upload failed because of a connection timeout. Retry after one second.\n")
file(WRITE "${source}/migration.md"
    "# Database migration\nThe migration uses a shadow table.\n")
file(WRITE "${source}/ignored.bin" "connection timeout")
file(WRITE "${source_two}/second.txt" "The second source is waiting.\n")

foreach(ignored_directory IN ITEMS node_modules .git .next dist build coverage)
    file(MAKE_DIRECTORY "${source}/${ignored_directory}")
    file(WRITE "${source}/${ignored_directory}/ignored.txt"
        "generateddependencysecret")
endforeach()

file(MAKE_DIRECTORY "${source}/custom_python_environment")
file(WRITE "${source}/custom_python_environment/pyvenv.cfg"
    "home = /python\n")
file(WRITE "${source}/custom_python_environment/dependency.py"
    "generateddependencysecret")

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

run_atlast(0 second_source_index index "${source_two}" --db "${database}")
if(NOT second_source_index MATCHES "Indexed: 1")
    message(FATAL_ERROR "Expected one file in second source:\n${second_source_index}")
endif()

run_atlast(0 sources_output sources --db "${database}")
if(NOT sources_output MATCHES "Root: .*source" OR
   NOT sources_output MATCHES "Root: .*source-two" OR
   NOT sources_output MATCHES "Files: 2" OR
   NOT sources_output MATCHES "Files: 1" OR
   NOT sources_output MATCHES "Last indexed: [0-9]")
    message(FATAL_ERROR "Expected indexed source details:\n${sources_output}")
endif()

run_atlast(0 connection_search search "connection timeout" --db "${database}")
if(NOT connection_search MATCHES "network.txt" OR
   connection_search MATCHES "ignored.bin")
    message(FATAL_ERROR "Unexpected connection search:\n${connection_search}")
endif()

run_atlast(0 path_filter search "connection path:network" --db "${database}")
if(NOT path_filter MATCHES "network.txt")
    message(FATAL_ERROR "Path filter rejected the expected file:\n${path_filter}")
endif()

run_atlast(0 path_filter_miss search "connection path:migration" --db "${database}")
if(NOT path_filter_miss MATCHES "No results")
    message(FATAL_ERROR "Path filter accepted the wrong file:\n${path_filter_miss}")
endif()

run_atlast(0 extension_filter search "connection ext:.TXT" --db "${database}")
if(NOT extension_filter MATCHES "network.txt")
    message(FATAL_ERROR
        "Extension filter rejected the expected file:\n${extension_filter}")
endif()

run_atlast(0 extension_filter_miss search "connection ext:md" --db "${database}")
if(NOT extension_filter_miss MATCHES "No results")
    message(FATAL_ERROR
        "Extension filter accepted the wrong file:\n${extension_filter_miss}")
endif()

run_atlast(0 combined_filters search
    "connection path:network ext:txt modified:1d" --db "${database}")
if(NOT combined_filters MATCHES "network.txt")
    message(FATAL_ERROR "Combined filters failed:\n${combined_filters}")
endif()

run_atlast(0 phrase_with_filter search
    "\"connection timeout\" ext:txt" --db "${database}")
if(NOT phrase_with_filter MATCHES "network.txt")
    message(FATAL_ERROR "FTS phrase with filter failed:\n${phrase_with_filter}")
endif()

run_atlast(2 invalid_extension search "connection ext:" --db "${database}")
run_atlast(2 invalid_modified search "connection modified:recent" --db "${database}")
run_atlast(2 filters_without_text search "ext:txt" --db "${database}")
run_atlast(2 duplicate_filter search
    "connection path:network path:source" --db "${database}")

run_atlast(0 ignored_search search "generateddependencysecret" --db "${database}")
if(NOT ignored_search MATCHES "No results")
    message(FATAL_ERROR "Ignored directory content was indexed:\n${ignored_search}")
endif()

run_atlast(0 second_index index "${source}" --db "${database}")
if(NOT second_index MATCHES "Unchanged: 2")
    message(FATAL_ERROR "Expected unchanged files:\n${second_index}")
endif()

file(WRITE "${source}/network.txt"
    "Upload now succeeds. The replacement marker is hummingbird.\n")
file(REMOVE "${source}/migration.md")
file(WRITE "${source_two}/second.txt"
    "The second source now contains firefly.\n")

run_atlast(0 refresh_output refresh --db "${database}")
if(NOT refresh_output MATCHES "Root: .*source" OR
   NOT refresh_output MATCHES "Root: .*source-two" OR
   NOT refresh_output MATCHES "Indexed: 1" OR
   NOT refresh_output MATCHES "Removed: 1")
    message(FATAL_ERROR "Refresh did not update both sources:\n${refresh_output}")
endif()

run_atlast(0 old_search search "connection" --db "${database}")
if(NOT old_search MATCHES "No results")
    message(FATAL_ERROR "Old content remained searchable:\n${old_search}")
endif()

run_atlast(0 new_search search "hummingbird" --limit 1 --db "${database}")
if(NOT new_search MATCHES "network.txt")
    message(FATAL_ERROR "Updated content was not searchable:\n${new_search}")
endif()

run_atlast(0 second_source_search search "firefly" --db "${database}")
if(NOT second_source_search MATCHES "second.txt")
    message(FATAL_ERROR
        "Refreshed second source was not searchable:\n${second_source_search}")
endif()

run_atlast(2 invalid_limit search "hummingbird" --limit 0 --db "${database}")

run_atlast(0 forget_output forget "${source}" --db "${database}")
if(NOT forget_output MATCHES "Removed: 1" OR
   NOT EXISTS "${source}/network.txt")
    message(FATAL_ERROR "Forget removed the wrong data:\n${forget_output}")
endif()

run_atlast(0 remaining_sources sources --db "${database}")
if(NOT remaining_sources MATCHES "source-two")
    message(FATAL_ERROR "Second source was forgotten too:\n${remaining_sources}")
endif()

run_atlast(0 forgotten_search search "hummingbird" --db "${database}")
if(NOT forgotten_search MATCHES "No results")
    message(FATAL_ERROR "Forgotten content is still searchable:\n${forgotten_search}")
endif()

run_atlast(0 forget_second_output forget "${source_two}" --db "${database}")
if(NOT forget_second_output MATCHES "Removed: 1")
    message(FATAL_ERROR "Second source was not forgotten:\n${forget_second_output}")
endif()

run_atlast(0 empty_sources sources --db "${database}")
if(NOT empty_sources MATCHES "No indexed sources")
    message(FATAL_ERROR "Forgotten sources are still listed:\n${empty_sources}")
endif()

run_atlast(0 empty_refresh refresh --db "${database}")
if(NOT empty_refresh MATCHES "No indexed sources")
    message(FATAL_ERROR "Empty refresh returned unexpected output:\n${empty_refresh}")
endif()

run_atlast(1 missing_source forget "${source}" --db "${database}")

message(STATUS "Atlast MVP end-to-end test passed")
