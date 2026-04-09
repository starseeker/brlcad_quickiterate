# CMake generated Testfile for 
# Source directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/bench
# Build directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/bench
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[benchmark]=] "/usr/bin/sh" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/bin/benchmark" "run" "TIMEFRAME=1")
set_tests_properties([=[benchmark]=] PROPERTIES  LABELS "Benchmark" _BACKTRACE_TRIPLES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/misc/CMake/BRLCAD_Test_Wrappers.cmake:107:EVAL;1;add_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/misc/CMake/BRLCAD_Test_Wrappers.cmake:107:EVAL;0;;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/misc/CMake/BRLCAD_Test_Wrappers.cmake;107;cmake_language;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/bench/CMakeLists.txt;48;brlcad_add_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/bench/CMakeLists.txt;0;")
subdirs("ref")
