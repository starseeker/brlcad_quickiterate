file(REMOVE_RECURSE
  "../../lib/libtclcad.a"
  "../../lib/libtclcad.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C CXX)
  include(CMakeFiles/libtclcad-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
