# qsketch Screenshots

These screenshots demonstrate the `qsketch` editor with six pre-built demo sketches.
Each image shows the Qt UI chrome overlaid with a swrast-rendered sketch wireframe
(yellow lines on black background) in the left viewport pane.

| File | Sketch | Features Shown |
|------|--------|----------------|
| `box.png` | Simple rectangle | Line segments, 4 vertices |
| `rounded_rect.png` | Rectangle with rounded corners | Lines + arcs |
| `annulus.png` | Two concentric circles | CARC segments, multi-contour |
| `C_shape.png` | C-shaped cutout | Mixed line/arc, open contour |
| `gear.png` | Gear profile | Many line segments |
| `multi_contour.png` | Three separate outlines | Independent sub-curves |

## UI Elements Visible

- **Left toolbar**: Editing mode buttons (pick vertex/segment, add vertex, add line, add arc, etc.)
- **Menu bar**: File, Edit, View menus
- **Right panel**: Vertex coordinates table (top) + segment table (bottom)
- **Status bar**: Live UV cursor readout + status messages
- **Viewport**: swrast off-screen rendered sketch wireframe (yellow on black)

## How Screenshots Were Taken

```bash
./brlcad_build/bin/qsketch --screenshot qsketch_images/box.png demo.g box
```

The `--screenshot` flag renders directly from the swrast DM buffer (bypasses Qt paint
events) and composites the result into the Qt window grab.

## Building qsketch

```bash
# Install Qt6 dependencies
sudo apt-get install qt6-base-dev libqt6svg6-dev qt6-tools-dev

# Configure and build
cmake -S brlcad -B brlcad_build \
  -DBRLCAD_EXT_DIR=bext_output \
  -DBRLCAD_ENABLE_QT=ON \
  -DBRLCAD_ENABLE_OPENGL=OFF
cmake --build brlcad_build --target qsketch -j$(nproc)
```
