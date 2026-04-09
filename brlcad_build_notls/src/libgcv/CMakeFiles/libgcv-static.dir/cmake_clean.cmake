file(REMOVE_RECURSE
  "../../lib/libgcv.a"
  "../../lib/libgcv.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/libgcv-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
