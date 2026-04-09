# CMake generated Testfile for 
# Source directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/fastgen
# Build directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/regress/gcv/fastgen
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[regress-fastgen]=] "/usr/local/bin/cmake" "-DEXEC=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/bin/fast4-g" "-P" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/regress/gcv/fastgen/regress-fastgen.cmake")
set_tests_properties([=[regress-fastgen]=] PROPERTIES  LABELS "Regression" TIMEOUT "300" _BACKTRACE_TRIPLES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/misc/CMake/BRLCAD_Targets.cmake;897;add_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/fastgen/CMakeLists.txt;1;brlcad_regression_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/fastgen/CMakeLists.txt;0;")
