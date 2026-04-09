file(REMOVE_RECURSE
  "../../lib/librender.a"
  "../../lib/librender.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/librender-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
