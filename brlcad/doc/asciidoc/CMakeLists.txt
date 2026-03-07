include(${BRLCAD_CMAKE_DIR}/AsciiDoc.cmake)

verbose_add_subdirectory(doc asciidoc/system)
verbose_add_subdirectory(doc asciidoc/articles)
verbose_add_subdirectory(doc asciidoc/books)
verbose_add_subdirectory(doc asciidoc/devguides)
verbose_add_subdirectory(doc asciidoc/lessons)
verbose_add_subdirectory(doc asciidoc/presentations)
verbose_add_subdirectory(doc asciidoc/specifications)

cmakefiles(
  CMakeLists.txt
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
