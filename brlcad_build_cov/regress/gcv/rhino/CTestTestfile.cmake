# CMake generated Testfile for 
# Source directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/rhino
# Build directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/rhino
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[regress-rhino-g]=] "/usr/local/bin/cmake" "-DEXEC=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/bin/gcv" "-P" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/rhino/regress-rhino-g.cmake")
set_tests_properties([=[regress-rhino-g]=] PROPERTIES  LABELS "Regression" TIMEOUT "300" _BACKTRACE_TRIPLES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/misc/CMake/BRLCAD_Targets.cmake;897;add_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/CMakeLists.txt;210;brlcad_regression_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/rhino/CMakeLists.txt;1;gcv_regress_util;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/rhino/CMakeLists.txt;0;")
