file(REMOVE_RECURSE
  "../../lib/libpkg.a"
  "../../lib/libpkg.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/libpkg-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
