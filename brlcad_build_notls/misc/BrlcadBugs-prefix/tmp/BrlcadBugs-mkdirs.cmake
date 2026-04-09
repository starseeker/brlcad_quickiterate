# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/misc/bugs")
  file(MAKE_DIRECTORY "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/misc/bugs")
endif()
file(MAKE_DIRECTORY
  "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/misc/bugs_out"
  "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/misc/BrlcadBugs-prefix"
  "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/misc/BrlcadBugs-prefix/tmp"
  "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/misc/BrlcadBugs-prefix/src/BrlcadBugs-stamp"
  "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/misc/BrlcadBugs-prefix/src"
  "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/misc/BrlcadBugs-prefix/src/BrlcadBugs-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/misc/BrlcadBugs-prefix/src/BrlcadBugs-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/misc/BrlcadBugs-prefix/src/BrlcadBugs-stamp${cfgdir}") # cfgdir has leading slash
endif()
