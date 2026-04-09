file(REMOVE_RECURSE
  "../../lib/libbn.a"
  "../../lib/libbn.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/libbn-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
