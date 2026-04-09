file(REMOVE_RECURSE
  "../../lib/libwdb.a"
  "../../lib/libwdb.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C CXX)
  include(CMakeFiles/libwdb-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
