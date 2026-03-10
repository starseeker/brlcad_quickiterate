set(
  system_man3_ADOC
  libbu.adoc
  libbv.adoc
  libdm.adoc
  libicv.adoc
  libpkg.adoc
  librt.adoc
  libwdb.adoc
  redblack.adoc
)

ADD_ASCIIDOC("MAN3" system_man3_ADOC "" "")

cmakefiles(
  CMakeLists.txt
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
