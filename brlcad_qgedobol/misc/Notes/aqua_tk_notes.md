# Aqua (Cocoa) Tk Support — Required bext Changes
#
# See also: brlcad/misc/CMake/BRLCAD_User_Options.cmake (BRLCAD_ENABLE_AQUA option)
#           brlcad/src/libtclcad/obol_view_macos.mm     (NSOpenGL rendering path)
#
# bext repository: https://github.com/BRL-CAD/bext/tree/main/tk

## Background

BRL-CAD's bext dependency package (https://github.com/BRL-CAD/bext) builds Tk
from source using the Tk autotools configure+make system.  Currently (as of the
tk submodule in bext main) the macOS configure invocation does NOT pass
`--enable-aqua`, so bext always builds X11 Tk on macOS — even when a native
macOS Aqua build is desired.

The `BRLCAD_ENABLE_AQUA` CMake option in BRL-CAD (off by default) is now wired
to:
  - disable X11 (BRLCAD_ENABLE_X11=OFF)
  - require TCL_TK_SYSTEM_GRAPHICS="aqua" for Tk validation
  - compile obol_view_macos.mm with NSOpenGL rendering
  - link OpenGL.framework + Cocoa.framework into libtclcad

However these BRL-CAD-side changes are inert until bext also builds Aqua Tk.

## Required bext Change

In the bext repository, file `tk/tk_configure.cmake.in`, the Unix configure
invocation must add `--enable-aqua` when building on macOS:

### Patch (apply to BRL-CAD/bext tk/tk_configure.cmake.in)

```diff
--- a/tk/tk_configure.cmake.in
+++ b/tk/tk_configure.cmake.in
@@ -1,5 +1,14 @@
 if (NOT WIN32)

   if (APPLE)
+
+    # Determine whether to build with Aqua (Cocoa) or X11.
+    # The BRLCAD_TK_AQUA cache variable is set to ON by the parent
+    # bext build when the consumer sets -DBRLCAD_ENABLE_AQUA=ON.
+    if (BRLCAD_TK_AQUA)
+      set(TK_AQUA_FLAG "--enable-aqua")
+    else()
+      set(TK_AQUA_FLAG "")
+    endif()

     execute_process(
       COMMAND xcrun --show-sdk-path
@@ -22,7 +31,7 @@ if (NOT WIN32)

   execute_process(
-    COMMAND @TK_SRC_DIR@/unix/configure --prefix=@CMAKE_BUNDLE_INSTALL_PREFIX@ ${WITH_TCL_FLAG}
+    COMMAND @TK_SRC_DIR@/unix/configure --prefix=@CMAKE_BUNDLE_INSTALL_PREFIX@ ${WITH_TCL_FLAG} ${TK_AQUA_FLAG}
     RESULT_VARIABLE TK_RET
     WORKING_DIRECTORY "@TK_BIN_DIR@"
     OUTPUT_VARIABLE MSG
```

### How BRLCAD_TK_AQUA propagates

In `bext/tk/CMakeLists.txt`, the ExternalProject_Add configure step currently
uses a CMake script (`tk_configure.cmake`).  The `BRLCAD_TK_AQUA` variable
must be passed to that script, e.g.:

```cmake
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/tk_configure.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/tk_configure.cmake
  @ONLY
)
```

is already in place.  The `@BRLCAD_TK_AQUA@` substitution works if the parent
bext CMakeLists sets:

```cmake
if(BRLCAD_ENABLE_AQUA AND APPLE)
  set(BRLCAD_TK_AQUA ON)
else()
  set(BRLCAD_TK_AQUA OFF)
endif()
```

before calling `configure_file`.

## Additional Notes

### Tk headers with Aqua

When Tk is built with `--enable-aqua`, the installed `tk.h` sets `MAC_OSX_TK`
and `tkDecls.h` declares `Tk_MacOSXGetNSWindowForDrawable` and friends.
`obol_view_macos.mm` depends on `Tk_MacOSXGetNSWindowForDrawable` to locate
the Tk window's NSWindow for NSOpenGLView embedding.

### AGL (Apple GL) — NOT used

The legacy AGL (Apple GL) API was part of the Carbon framework, deprecated in
macOS 10.9 (Mavericks), and its types were removed in macOS 14 (Sonoma).
BRL-CAD does NOT use AGL.  The correct replacement for AGL on modern macOS is
NSOpenGLContext (Cocoa) as used in obol_view_macos.mm, or Metal for newer
projects.

### NSOpenGL deprecation

NSOpenGLContext and NSOpenGLView are deprecated since macOS 10.14 (Mojave) in
favour of Metal.  However they remain available and functional through at least
macOS 15 (Sequoia, 2024) and are the correct path for hardware-accelerated
OpenGL rendering in a Cocoa/Tk window since Obol/Coin3D requires OpenGL.

### Retina / HiDPI

`obol_view_macos.mm` calls `[glview setWantsBestResolutionOpenGLSurface:NO]`
which means the GL surface uses logical pixels (points) rather than physical
Retina pixels.  To enable full Retina rendering, set this to YES and scale the
GL viewport by `[glview convertRectToBacking:[glview bounds]].size`.
