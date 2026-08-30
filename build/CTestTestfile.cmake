# CMake generated Testfile for 
# Source directory: C:/Atlast
# Build directory: C:/Atlast/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[atlast_mvp]=] "C:/msys64/ucrt64/bin/cmake.exe" "-DATLAST_EXECUTABLE=C:/Atlast/build/atlast.exe" "-DTEST_DIRECTORY=C:/Atlast/build/test-data" "-P" "C:/Atlast/tests/mvp.cmake")
set_tests_properties([=[atlast_mvp]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Atlast/CMakeLists.txt;16;add_test;C:/Atlast/CMakeLists.txt;0;")
