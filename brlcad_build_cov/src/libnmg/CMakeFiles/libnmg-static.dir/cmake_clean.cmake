file(REMOVE_RECURSE
  "../../lib/libnmg.a"
  "../../lib/libnmg.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/libnmg-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
