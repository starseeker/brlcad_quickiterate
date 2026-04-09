file(REMOVE_RECURSE
  "../../lib/libdm.a"
  "../../lib/libdm.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C CXX)
  include(CMakeFiles/libdm-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
