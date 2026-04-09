file(REMOVE_RECURSE
  "../../lib/libbg.a"
  "../../lib/libbg.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C CXX)
  include(CMakeFiles/libbg-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
