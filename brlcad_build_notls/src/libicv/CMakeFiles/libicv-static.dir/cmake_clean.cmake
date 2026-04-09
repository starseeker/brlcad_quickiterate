file(REMOVE_RECURSE
  "../../lib/libicv.a"
  "../../lib/libicv.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C CXX)
  include(CMakeFiles/libicv-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
