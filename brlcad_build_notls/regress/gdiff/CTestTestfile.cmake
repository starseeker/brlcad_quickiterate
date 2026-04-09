# CMake generated Testfile for 
# Source directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gdiff
# Build directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/regress/gdiff
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[regress-gdiff]=] "/usr/local/bin/cmake" "-DEXEC=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/bin/gdiff" "-P" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/regress/gdiff/regress-gdiff.cmake")
set_tests_properties([=[regress-gdiff]=] PROPERTIES  LABELS "Regression" TIMEOUT "300" _BACKTRACE_TRIPLES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/misc/CMake/BRLCAD_Targets.cmake;897;add_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gdiff/CMakeLists.txt;4;brlcad_regression_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gdiff/CMakeLists.txt;0;")
