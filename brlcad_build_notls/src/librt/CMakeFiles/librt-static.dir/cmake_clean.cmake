file(REMOVE_RECURSE
  "../../lib/librt.a"
  "../../lib/librt.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C CXX)
  include(CMakeFiles/librt-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
