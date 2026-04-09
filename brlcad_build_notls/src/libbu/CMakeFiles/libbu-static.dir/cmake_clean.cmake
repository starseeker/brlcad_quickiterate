file(REMOVE_RECURSE
  "../../lib/libbu.a"
  "../../lib/libbu.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C CXX)
  include(CMakeFiles/libbu-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
