# CMake generated Testfile for 
# Source directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad
# Build directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[NOTE: some 'test' tests are expected to fail, 'regress' must pass]=] "/usr/local/bin/cmake" "--build" "." "--target" "print-warning-message")
set_tests_properties([=[NOTE: some 'test' tests are expected to fail, 'regress' must pass]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/CMakeLists.txt;2455;add_test;/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/CMakeLists.txt;0;")
subdirs("misc/tools")
subdirs("src")
subdirs("include")
subdirs("db")
subdirs("doc")
subdirs("sh")
subdirs("bench")
subdirs("regress")
subdirs("misc")
