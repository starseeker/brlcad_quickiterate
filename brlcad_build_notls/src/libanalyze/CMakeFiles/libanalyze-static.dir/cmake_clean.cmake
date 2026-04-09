file(REMOVE_RECURSE
  "../../lib/libanalyze.a"
  "../../lib/libanalyze.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C CXX)
  include(CMakeFiles/libanalyze-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
