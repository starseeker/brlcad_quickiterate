# CMake generated Testfile for 
# Source directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/repository
# Build directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/regress/repository
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[regress-repository]=] "/usr/local/bin/cmake" "-DEXEC=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/regress/repository/repocheck" "-P" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/regress/repository/regress-repository.cmake")
set_tests_properties([=[regress-repository]=] PROPERTIES  LABELS "Regression" TIMEOUT "300" _BACKTRACE_TRIPLES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/misc/CMake/BRLCAD_Targets.cmake;897;add_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/repository/CMakeLists.txt;18;brlcad_regression_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/repository/CMakeLists.txt;0;")
