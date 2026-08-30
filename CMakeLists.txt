cmake_minimum_required(VERSION 3.20)

project(Atlast LANGUAGES CXX)

find_package(SQLite3 REQUIRED)

add_executable(atlast src/main.cpp)

target_compile_features(atlast PRIVATE cxx_std_23)
set_target_properties(atlast PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(atlast PRIVATE SQLite3::SQLite3)

include(CTest)

if(BUILD_TESTING)
    add_test(
        NAME atlast_mvp
        COMMAND ${CMAKE_COMMAND}
            "-DATLAST_EXECUTABLE=$<TARGET_FILE:atlast>"
            "-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/test-data"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/mvp.cmake"
    )
endif()
