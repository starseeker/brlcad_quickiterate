set(
  specifications_ADOC
  BRL_CAD_g_format_V5.adoc
  BRL_CAD_g_format_V6.adoc
)

ADD_ASCIIDOC("HTML" specifications_ADOC "" "")

cmakefiles(
  CMakeLists.txt
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
