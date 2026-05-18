# BRL-CAD Quick Iterate – Copilot Agent Instructions

## Repository Layout

```
brlcad_quickiterate/
├── brlcad/          # BRL-CAD source tree
├── bext_output/     # Pre-built BRL-CAD external dependencies (bext)
│   ├── install/     # Runtime-installed dependency artifacts
│   └── noinstall/   # Build-time-only dependency artifacts
└── .github/
    └── copilot-instructions.md   # This file
```

## Required System Dependencies

**IMPORTANT: The drawing stack modernization work heavily involves the BRL-CAD Qt
stack (libqtcad, dm-qtgl, dm-swrast, qged, archer).  Qt must be installed and
`-DBRLCAD_ENABLE_QT=ON` must be used at every phase.  Never build with
`-DBRLCAD_ENABLE_QT=OFF` unless specifically asked to test the non-Qt path.**

Install system dependencies before configuring or building with bext Qt:

```bash
sudo apt-get update
# X11 and OpenGL
sudo apt-get install -y xserver-xorg-dev libx11-dev libxi-dev libxext-dev \
  libglu1-mesa-dev libfontconfig-dev libgl-dev xvfb
# Additional runtime libs required by bext Qt6Gui linkage
sudo apt-get install -y libegl1 libmd4c0
# Build tools
sudo apt-get install -y astyle re2c xsltproc libxml2-utils
# XCB / input packages required by the Qt6 XCB platform plugin on Linux
# See: https://doc.qt.io/qt-6/linux-requirements.html
sudo apt-get install -y \
  libfontconfig1-dev libfreetype6-dev \
  libx11-xcb-dev libxfixes-dev libxrender-dev \
  libxcb1-dev libxcb-cursor-dev libxcb-glx0-dev \
  libxcb-keysyms1-dev libxcb-image0-dev libxcb-shm0-dev \
  libxcb-icccm4-dev libxcb-sync-dev libxcb-xfixes0-dev \
  libxcb-shape0-dev libxcb-randr0-dev libxcb-render-util0-dev \
  libxcb-util-dev libxcb-xinerama0-dev libxcb-xkb-dev \
  libxkbcommon-dev libxkbcommon-x11-dev
```

These packages are also pre-installed by `.github/workflows/copilot-setup-steps.yml`
so they will be present in every Copilot agent session automatically.
The Qt toolchain itself is expected to come from `bext_output/install` (via
`-DCMAKE_PREFIX_PATH`), not from system Qt development packages.

## Configuring BRL-CAD

Use the pre-built dependencies in `bext_output/` together with the flags below to
minimize configure and build time.  Run these commands from the **repository root**:

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
mkdir -p brlcad_build
cmake -S "$REPO_ROOT/brlcad" -B "$REPO_ROOT/brlcad_build" \
  -DBRLCAD_EXT_DIR="$REPO_ROOT/bext_output" \
  -DCMAKE_PREFIX_PATH="$REPO_ROOT/bext_output/install" \
  -DBRLCAD_EXTRADOCS=OFF \
  -DBRLCAD_ENABLE_STEP=OFF \
  -DBRLCAD_ENABLE_GDAL=OFF \
  -DBRLCAD_ENABLE_OPENGL=ON
  -DBRLCAD_ENABLE_QT=ON
```

Expected configure time: ~55 seconds on a fresh build directory (a few seconds on a re-configure).

## Building BRL-CAD

After a successful configure, build with:

```bash
cmake --build /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build -j$(nproc)
```

To build only a specific target (e.g. `libbu`):

```bash
cmake --build /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build --target libbu -j$(nproc)
```

Key Qt-dependent targets to verify when touching the drawing stack:
- `libqtcad`   — Qt camera, event, and binding abstractions (libqtcad.so)
- `dm-qtgl`    — OpenGL display manager Qt plugin
- `dm-swrast`  — software rasterizer display manager Qt plugin
- `qged`       — QGED main window binary
- `archer`     — Archer GUI application

## Important Notes

- **bext_output is pre-built** – do not delete or rebuild it unless strictly necessary; rebuilding bext from source takes a very long time.
- **Build directory is outside the source tree** – always configure with a separate build directory (e.g. `brlcad_build/` at the repo root) so that source-tree integrity checks inside CMake pass.
- **distcheck.yml** – BRL-CAD's cmake system validates that `brlcad/.github/workflows/distcheck.yml` is present and up to date.  The copy committed in this repo was generated from the current source tree; if the source tree is updated you may need to re-generate it by running cmake once, letting it fail, then copying the generated file from `<build_dir>/CMakeTmp/distcheck.yml` into `brlcad/.github/workflows/distcheck.yml`.
- **Ninja is available** but the default Unix Makefiles generator was measured to be faster in this environment for fresh configures.  Either generator works for builds.
- BRL-CAD enforces strict compiler warnings (including `-Werror`) by default, so compiler version matters.  The environment provides GCC 13.
