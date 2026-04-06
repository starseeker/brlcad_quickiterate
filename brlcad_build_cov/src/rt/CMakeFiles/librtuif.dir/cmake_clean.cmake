file(REMOVE_RECURSE
  "../../lib/librtuif.a"
  "../../lib/librtuif.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C CXX)
  include(CMakeFiles/librtuif.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
