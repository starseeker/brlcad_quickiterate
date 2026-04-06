# CMake generated Testfile for 
# Source directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/lwo
# Build directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/lwo
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[regress-lwo-g]=] "/usr/local/bin/cmake" "-DEXEC=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/bin/gcv" "-P" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/lwo/regress-lwo-g.cmake")
set_tests_properties([=[regress-lwo-g]=] PROPERTIES  LABELS "Regression" TIMEOUT "300" _BACKTRACE_TRIPLES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/misc/CMake/BRLCAD_Targets.cmake;897;add_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/CMakeLists.txt;210;brlcad_regression_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/lwo/CMakeLists.txt;2;gcv_regress_util;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/lwo/CMakeLists.txt;0;")
