cmake_minimum_required(VERSION 3.20)

project(Atlast VERSION 0.1.0 LANGUAGES CXX)

find_package(SQLite3 REQUIRED)

add_executable(atlast
    src/cli.cpp
    src/database.cpp
    src/git.cpp
    src/indexer.cpp
    src/main.cpp
    src/search.cpp
)

target_compile_features(atlast PRIVATE cxx_std_23)
target_compile_definitions(atlast PRIVATE ATLAST_VERSION="${PROJECT_VERSION}")
set_target_properties(atlast PROPERTIES CXX_EXTENSIONS OFF)
if(TARGET SQLite3::SQLite3)
    target_link_libraries(atlast PRIVATE SQLite3::SQLite3)
else()
    target_link_libraries(atlast PRIVATE SQLite::SQLite3)
endif()

install(TARGETS atlast RUNTIME DESTINATION bin)
install(FILES README.md DESTINATION share/doc/atlast)

include(CTest)

if(BUILD_TESTING)
    if(WIN32)
        set(GIT_EXECUTABLE "")
    else()
        find_package(Git QUIET)
    endif()
    add_test(
        NAME atlast_mvp
        COMMAND ${CMAKE_COMMAND}
            "-DATLAST_EXECUTABLE=$<TARGET_FILE:atlast>"
            "-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/test-data"
            "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/mvp.cmake"
    )
endif()
