# CMake generated Testfile for 
# Source directory: /home/rab/code/v-type
# Build directory: /home/rab/code/v-type/build-asan
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[level_roundtrip_test]=] "/home/rab/code/v-type/build-asan/level_roundtrip_test")
set_tests_properties([=[level_roundtrip_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/rab/code/v-type/CMakeLists.txt;194;add_test;/home/rab/code/v-type/CMakeLists.txt;0;")
subdirs("DefconDraw")
