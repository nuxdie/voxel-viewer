# voxel-viewer

A fast OpenGL voxel viewer for Linux, written in C++17. It opens **MagicaVoxel** models and
**Minecraft WorldEdit** schematics.

![screenshot](docs/screenshot.png)

| Format | Extension | Notes |
|---|---|---|
| MagicaVoxel | `.vox` | Scene graph (`nTRN`/`nGRP`/`nSHP`) with rotations, hidden layers, custom palettes, glass and emissive materials |
| WorldEdit / Sponge schematic | `.schem` | Versions 1, 2 and 3. This is the format WorldEdit uses for 1.13+ |
| WorldEdit / MCEdit legacy schematic | `.schematic` | Numeric block IDs + data values, `AddBlocks`, Schematica mappings |
| Litematica | `.litematic` | All regions, including negative region sizes |
| Minecraft structure block | `.nbt` | Vanilla structure files |

The viewer detects the format from the file's content, so the extension doesn't matter.

## Rendering

- OpenGL 3.3 core. There are no dependencies beyond GLFW and zlib.
- Voxels are stored sparsely in 32³ chunks. Chunks are meshed on all CPU cores using **greedy
  meshing** with per-vertex **ambient occlusion**. Each vertex is 8 bytes.
- Chunks outside the view are skipped (frustum culling). Glass, water and ice are drawn in a
  separate alpha-blended pass, sorted back to front.
- Tested with a 768×76×768 Sponge schematic of 26.6 million voxels: it loads in about 0.75 s,
  meshes in about 0.2 s, and uses 33 MB of GPU memory.
- Minecraft blocks are colored from a built-in table of average texture colors, covering
  about 400 blocks plus rules for dyed, wood, stone and copper variants. Unknown and modded
  blocks get a stable color derived from their name.

## Building

```sh
# Debian / Ubuntu
sudo apt install build-essential cmake libglfw3-dev libgl-dev zlib1g-dev
# Fedora
sudo dnf install gcc-c++ cmake glfw-devel libglvnd-devel zlib-devel
# Arch
sudo pacman -S base-devel cmake glfw zlib

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/voxel-viewer path/to/model.vox
```

Optional: install with `sudo cmake --install build`. You can then run `ctest --test-dir build`
to run the loader tests, which need Python 3.

## NVIDIA GPUs

The viewer works with the proprietary NVIDIA driver (through libglvnd) and with nouveau/Mesa.
At startup it prints which GPU it is rendering on:

```
OpenGL 4.6.0 NVIDIA 570.xx on NVIDIA GeForce RTX 4070/PCIe/SSE2 (NVIDIA Corporation)
```

On hybrid-graphics laptops (Intel/AMD iGPU + NVIDIA dGPU), the viewer sees the NVIDIA driver
(`/proc/driver/nvidia/version`) and requests **PRIME render offload** itself by setting
`__NV_PRIME_RENDER_OFFLOAD=1` and `__GLX_VENDOR_LIBRARY_NAME=nvidia`. If that context
can't be created, it falls back to the default GPU. To keep it on the integrated GPU, pass
`--no-prime`. You can also set the variables yourself, or run `prime-run voxel-viewer ...`.

## Usage

```
voxel-viewer [options] [file...]

  --screenshot FILE.png  render the first file to a PNG and exit
  --size WxH             window / screenshot size (default 1600x1000)
  --hide-decorations     hide torches, flowers, rails and other small blocks
  --no-ao                disable ambient occlusion
  --msaa N               multisample count (default 8, 0 to disable)
  --no-prime             do not request the NVIDIA GPU on hybrid-graphics laptops
```

To open files, pass them on the command line, **drag and drop** them onto the window, or press
**Ctrl+O** to get a file dialog (needs `zenity` or `kdialog`). When you open a single file,
`]` and `[` step through the other supported files in the same folder.

| Input | Action |
|---|---|
| Left drag | Orbit (fly mode: look around) |
| Right / middle drag | Pan |
| Mouse wheel | Zoom |
| `Tab` | Switch between orbit and fly camera |
| `W A S D`, `Q`/`E` (or `Ctrl`/`Space`), `Shift` | Fly: move, down/up, faster |
| `F` | Frame the model (reset view) |
| `1` `2` `3` `4` | Front / right / top / isometric view |
| `PgUp` / `PgDn` (+`Shift` for ×10) | Move the cut-away slice, to see inside buildings layer by layer |
| `Home` | Remove the slice |
| `V` | Show/hide small decorations (torches, flowers, rails, signs, ...) |
| `G` / `B` / `X` / `O` / `L` | Grid / bounding box / wireframe / ambient occlusion / light background |
| `F12` | Save a PNG screenshot to the current directory |
| `H` | Print help · `Esc` quits |

The window title shows the format, dimensions, voxel count and FPS.

`voxinfo` is a small command-line tool built alongside the viewer. It prints a file's
format, size, voxel count, mesh statistics and most common materials, without opening a
window.

## Project layout

```
src/voxel_model.h        sparse chunked voxel storage (Y-up)
src/nbt.*                NBT reader (gzip/zlib, bounds-checked)
src/vox_loader.cpp       MagicaVoxel .vox
src/schematic_loader.cpp .schematic / .schem / .litematic / structure .nbt
src/block_colors.*       Minecraft block colors + legacy numeric ID table
src/mesher.*             multithreaded greedy mesher with ambient occlusion
src/renderer.*           OpenGL 3.3 renderer
src/main.cpp             window, input, file handling (GLFW)
tests/make_samples.py    writes the same build in every format; check_samples.py cross-checks the loaders
```
