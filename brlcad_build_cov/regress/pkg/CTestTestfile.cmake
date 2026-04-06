# CMake generated Testfile for 
# Source directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/pkg
# Build directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/pkg
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[regress-pkg]=] "/usr/local/bin/cmake" "-DEXEC=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/pkg/regress_pkg" "-P" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/pkg/regress-pkg.cmake")
set_tests_properties([=[regress-pkg]=] PROPERTIES  LABELS "STAND_ALONE" TIMEOUT "300" _BACKTRACE_TRIPLES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/misc/CMake/BRLCAD_Targets.cmake;897;add_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/pkg/CMakeLists.txt;6;brlcad_regression_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/pkg/CMakeLists.txt;0;")
