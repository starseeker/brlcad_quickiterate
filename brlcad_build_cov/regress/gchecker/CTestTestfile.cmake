# CMake generated Testfile for 
# Source directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gchecker
# Build directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gchecker
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[regress-gchecker]=] "/usr/local/bin/cmake" "-DEXEC=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/bin/gchecker" "-P" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gchecker/regress-gchecker.cmake")
set_tests_properties([=[regress-gchecker]=] PROPERTIES  LABELS "Regression" TIMEOUT "300" _BACKTRACE_TRIPLES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/misc/CMake/BRLCAD_Targets.cmake;897;add_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gchecker/CMakeLists.txt;5;brlcad_regression_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gchecker/CMakeLists.txt;0;")
