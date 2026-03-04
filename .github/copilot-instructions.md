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

## Configuring BRL-CAD

Use the pre-built dependencies in `bext_output/` together with the flags below to minimize configure and build time.  Run these commands from the **repository root**:

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
mkdir -p brlcad_build
cmake -S "$REPO_ROOT/brlcad" -B "$REPO_ROOT/brlcad_build" \
  -DBRLCAD_EXT_DIR="$REPO_ROOT/bext_output" \
  -DBRLCAD_EXTRADOCS=OFF \
  -DBRLCAD_ENABLE_STEP=OFF \
  -DBRLCAD_ENABLE_GDAL=OFF \
  -DBRLCAD_ENABLE_QT=OFF
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

## Qt + Obol (Inventor scene-graph) Setup

The `qged` Qt editor uses Obol (Open Inventor) for its 3D viewport.  Obol is pre-built and installed in `bext_output/install/` (as `libObol.so` + cmake config).  To configure and build **with Qt and Obol**:

### 1. Install system Qt6 and display dependencies
```bash
sudo apt-get install -y qt6-base-dev qt6-svg-dev \
  libx11-dev libxi-dev libxext-dev libglu1-mesa-dev xvfb
```

### 2. (Re-)build Obol standalone if needed
The pre-built `bext_output/install/lib/libObol.so` and `libosmesa.so` should already be present.  If they are missing or stale, rebuild:
```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
mkdir -p /tmp/obol_build
cmake -S "$REPO_ROOT/obol" -B /tmp/obol_build \
  -DCMAKE_INSTALL_PREFIX="$REPO_ROOT/bext_output/install" \
  -DCMAKE_BUILD_TYPE=Release \
  -DOBOL_BUILD_TESTS=OFF \
  -DOBOL_BUILD_VIEWER=OFF
cmake --build /tmp/obol_build -j$(nproc)
# Create stub files needed by the install step
touch "$REPO_ROOT/obol/include/SoWinEnterScope.h" \
      "$REPO_ROOT/obol/include/SoWinLeaveScope.h" \
      /tmp/obol_build/obol-default.cfg
cmake --install /tmp/obol_build
# Also copy Obol's bundled osmesa (has OSMesaGetProcAddress)
cp /tmp/obol_build/osmesa_build/src/libosmesa.so \
   "$REPO_ROOT/bext_output/install/lib/libosmesa.so"
cp /tmp/obol_build/lib/libObol.so.20.0.0 \
   "$REPO_ROOT/bext_output/install/lib/libObol.so.20.0.0"
```

### 3. Configure BRL-CAD with Qt + Obol
```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
mkdir -p "$REPO_ROOT/brlcad_build"
cmake -S "$REPO_ROOT/brlcad" -B "$REPO_ROOT/brlcad_build" \
  -DBRLCAD_EXT_DIR="$REPO_ROOT/bext_output" \
  -DBRLCAD_EXTRADOCS=OFF \
  -DBRLCAD_ENABLE_STEP=OFF \
  -DBRLCAD_ENABLE_GDAL=OFF \
  -DBRLCAD_ENABLE_QT=ON \
  -DBRLCAD_ENABLE_OBOL=ON \
  -DOBOL_INSTALL_DIR="$REPO_ROOT/bext_output/install"
```

### 4. Run qged under a virtual display
```bash
export LD_LIBRARY_PATH=$REPO_ROOT/brlcad_build/lib:$REPO_ROOT/bext_output/install/lib:$LD_LIBRARY_PATH
Xvfb :99 -screen 0 1280x800x24 &
DISPLAY=:99 $REPO_ROOT/brlcad_build/bin/qged
```

### Key design decisions
- **Obol is built standalone** (not as a CMake subdirectory of BRL-CAD) to avoid BRL-CAD's strict `-Werror` flags polluting third-party code.
- **`FindObol.cmake`** discovers the installed package via `OBOL_INSTALL_DIR` using CMake CONFIG mode, promotes `Obol::Obol` to GLOBAL scope, and adds the sibling `libosmesa.so` as a transitive link dependency.
- **Inventor headers** are wrapped with `#pragma GCC diagnostic push/pop` + `#undef UP/DOWN` in BRL-CAD source files (`QgObolWidget.h`, `QgObolQuadWidget.h`, `render_ctx.cpp`) to suppress BRL-CAD's strict warnings without modifying Obol's headers.



- **bext_output is pre-built** – do not delete or rebuild it unless strictly necessary; rebuilding bext from source takes a very long time.
- **Build directory is outside the source tree** – always configure with a separate build directory (e.g. `brlcad_build/` at the repo root) so that source-tree integrity checks inside CMake pass.
- **distcheck.yml** – BRL-CAD's cmake system validates that `brlcad/.github/workflows/distcheck.yml` is present and up to date.  The copy committed in this repo was generated from the current source tree; if the source tree is updated you may need to re-generate it by running cmake once, letting it fail, then copying the generated file from `<build_dir>/CMakeTmp/distcheck.yml` into `brlcad/.github/workflows/distcheck.yml`.
- **Ninja is available** but the default Unix Makefiles generator was measured to be faster in this environment for fresh configures.  Either generator works for builds.
- BRL-CAD enforces strict compiler warnings (including `-Werror`) by default, so compiler version matters.  The environment provides GCC 13.
