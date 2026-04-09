file(REMOVE_RECURSE
  "../../lib/libbrep.a"
  "../../lib/libbrep.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang CXX)
  include(CMakeFiles/libbrep-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
