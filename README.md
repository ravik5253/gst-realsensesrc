## gst-realsensesrc — Intel RealSense GStreamer Source for DeepStream/GStreamer

A custom GStreamer source element (`realsensesrc`) that streams frames from an Intel RealSense D435i camera using Intel librealsense2. The element outputs a single RGB buffer where the top half contains the color image and the bottom half encodes depth values. Designed to be deployed into NVIDIA DeepStream's plugin directory but works with standard GStreamer as well.

### Features
- **Element**: `realsensesrc` (source)
- **Format**: `video/x-raw, format=RGB`
- **Output layout**: Top half = RGB color; bottom half = depth encoded into RGB (custom encoding)
- **Alignment options**: None, align-to-color, align-to-depth
- **Configurable color/depth resolutions and FPS**
- **Optional advanced-mode preset** via JSON (e.g., `PresetFile.json`) for D435i

### Requirements
- CMake ≥ 3.15
- A C++ compiler (GCC/Clang) with C++14 support
- GStreamer 1.0 development packages:
  - `gstreamer-1.0`, `gstreamer-video-1.0`, `gstreamer-audio-1.0`
  - On Debian/Ubuntu: `sudo apt install cmake g++ pkg-config libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev`
- Intel RealSense SDK 2.0 (librealsense2) with headers: `sudo apt install librealsense2-dev`
- An Intel RealSense D435i device
- Optional: NVIDIA DeepStream 7.1 (set your `DEEPSTREAM_PATH`)

> Note: The current implementation starts only when the connected camera reports its name as D435i. Other RealSense models will be rejected at start.

### Build and Install
The CMake file is configured to place the built library in the DeepStream plugin directory by default:
`<DEEPSTREAM_PATH>/lib/gst-plugins`.

If your DeepStream path differs, either edit `CMakeLists.txt` and change `DEEPSTREAM_PATH`, or set `GST_PLUGIN_PATH` to a directory you control and copy the resulting library there.

```bash
cd gst-realsensesrc
mkdir -p build && cd build
cmake ..
# Build (may require sudo if the output directory is under /opt)
cmake --build . -j

# If you did not build directly to your desired GST plugin dir, copy the .so:
# sudo cp libgstrealsensesrc.so <DEEPSTREAM_PATH>/lib/gst-plugins/

# Make sure GStreamer can discover the plugin (if using gst-launch directly):
export GST_PLUGIN_PATH=<DEEPSTREAM_PATH>/lib/gst-plugins:${GST_PLUGIN_PATH}

# Verify the plugin is visible
gst-inspect-1.0 realsensesrc | cat
```

If the build fails due to permissions when writing under system directories, either run the build with sufficient permissions or modify `CMakeLists.txt` to output to a writable directory and copy the resulting `.so` afterward.

### Quick Start
Display the multiplexed frame (color + encoded depth stacked vertically):
```bash
gst-launch-1.0 -v realsensesrc ! videoconvert ! autovideosink
```

Specify alignment and stream modes explicitly:
```bash
gst-launch-1.0 -v \
  realsensesrc align=1 \
    color-width=1280 color-height=720 color-fps=30 \
    depth-width=640 depth-height=480 depth-fps=30 \
  ! videoconvert ! autovideosink
```

Apply an advanced-mode preset (D435i only):
```bash
gst-launch-1.0 -v \
  realsensesrc preset-file="./ShortRangePreset.json" \
  ! videoconvert ! autovideosink
```

### Element Properties
All properties are configurable via `gst-launch-1.0` or programmatically through GObject properties.

- **align** (int): Alignment between color and depth
  - 0 = None, 1 = Color, 2 = Depth; default: 1 (Color)
- **color-width** (int): Width of color stream. Default: 1280. Valid examples include 1920, 1280, 960, 848, 640, 424, 320
- **color-height** (int): Height of color stream. Default: 720. Valid examples include 1080, 720, 540, 480, 360, 240, 180
- **color-fps** (int): FPS for color stream. Default: 30. Valid examples include 6, 15, 30, 60 (depending on resolution)
- **depth-width** (int): Width of depth stream. Default: 640. Valid examples include 1280, 848, 640, 480, 424
- **depth-height** (int): Height of depth stream. Default: 480. Valid examples include 720, 480, 360, 270, 240
- **depth-fps** (int): FPS for depth stream. Default: 30. Valid examples include 6, 15, 30, 60, 90 (depending on resolution)
- **preset-file** (string): Path to a RealSense JSON preset loaded in advanced mode at pipeline start. Optional; D435i only.

> The element validates width/height/fps combinations against a list of supported modes. If an invalid combination is provided, it reverts to defaults and logs a warning or refuses to start.

### Output Format and Demultiplexing
- Caps: `video/x-raw, format=RGB`
- Resolution: width equals color width; height equals `color-height * 2` (top half color, bottom half depth-encoded).
- Depth is encoded into RGB bytes in a custom way by the plugin. If you need separate color and depth streams, you must split the buffer accordingly in your downstream element/app. A dedicated `rsdemux` element is referenced in comments but not provided in this repository.

### Troubleshooting
- "No RealSense devices found": Connect a D435i and ensure user permissions/udev rules are installed for RealSense.
- "Selected device is not an Intel RealSense D435i": This element currently supports only the D435i model.
- Invalid mode warnings/errors: Choose a valid `(width, height, fps)` combination from the supported sets.
- Plugin not found by `gst-inspect-1.0`: Ensure `GST_PLUGIN_PATH` includes the directory containing `libgstrealsensesrc.so`.
- Permission errors writing to `/opt/...`: Build to a writable directory and copy with `sudo`, or adjust `DEEPSTREAM_PATH` in `CMakeLists.txt`.

### Development Notes
- Library target: `gstrealsensesrc`
- Plugin registration name: `realsensesrc`
- Source pad caps template restricts to RGB format


