file(GLOB_RECURSE BUILD_DIR_CONTENTS RELATIVE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/distcheck-odd_pathnames/1 Odd_ build dir ++" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/distcheck-odd_pathnames/1 Odd_ build dir ++/*")
# Ninja keeps a running log in .ninja_log - if we're using ninja itself to run the
# tests, we can't clear this file. Ignore it, as it's not a sign of a problem with
# the clean logic.
list(REMOVE_ITEM BUILD_DIR_CONTENTS .ninja_log)
if(BUILD_DIR_CONTENTS)
  message("Files present after distclean in /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/distcheck-odd_pathnames/1 Odd_ build dir ++:")
  foreach(filename ${BUILD_DIR_CONTENTS})
    message("${filename}")
  endforeach(filename ${BUILD_DIR_CONTENTS})
  message(FATAL_ERROR "distclean failed in /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/distcheck-odd_pathnames/1 Odd_ build dir ++")
endif(BUILD_DIR_CONTENTS)
