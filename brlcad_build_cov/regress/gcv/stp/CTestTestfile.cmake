# CMake generated Testfile for 
# Source directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/stp
# Build directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/stp
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[regress-g-stp]=] "/usr/local/bin/cmake" "-DEXEC=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/bin/gcv" "-P" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/stp/regress-g-stp.cmake")
set_tests_properties([=[regress-g-stp]=] PROPERTIES  LABELS "Regression" TIMEOUT "300" _BACKTRACE_TRIPLES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/misc/CMake/BRLCAD_Targets.cmake;897;add_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/CMakeLists.txt;210;brlcad_regression_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/stp/CMakeLists.txt;2;gcv_regress_util;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/stp/CMakeLists.txt;0;")
