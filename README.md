# CPU Software Rasterizer

**English** | [中文](docs/zh-CN/README.md)

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus&logoColor=white)
![Header Only](https://img.shields.io/badge/header--only-4%20files-brightgreen)
![Video](https://img.shields.io/badge/video-libx264-orange)
![Platform](https://img.shields.io/badge/platform-linux-lightgrey?logo=linux&logoColor=white)

A header-only CPU software rendering pipeline written in C++17.
No OpenGL, Vulkan, or any graphics library required.
Simulates a GPU programmable pipeline (vertex + fragment shaders), streams PPM frames directly to a built-in encoder (`ppm2mp4`) via pipe — no intermediate files on disk.

## Features

- Templated pipeline `Pipeline<Vertex, Varying>` — shaders are user-defined lambdas
- Perspective-correct interpolation (Varying weighted by `val/w`)
- Sutherland-Hodgman clipping against all 6 homogeneous frustum planes
- Multithreaded rasterization (thread pool, lock-free row partitioning)
- Incremental rasterization (edge function stepping + early depth rejection)
- Trilinear mipmap sampling (`generateMipmaps()` + `sampleLod()`)
- Alpha blending (src-over) with independent depth write toggle
- Wireframe rendering mode

## Directory Structure

```
softras/
├── rasterizer/         # Core headers (header-only)
│   ├── math.h          #   vec2/vec3/vec4/mat4 + GLSL-style math
│   ├── framebuffer.h   #   Color buffer + depth buffer + PPM output
│   ├── texture.h       #   Texture2D, bilinear/trilinear sampling
│   └── pipeline.h      #   Pipeline<Vertex, Varying>
├── demo/               # Example programs
│   ├── quad.cpp        #   Fullscreen procedural shader (1920×1080)
│   ├── cube.cpp        #   Rotating cube + Blinn-Phong (960×540)
│   └── showcase.cpp    #   Feature showcase (960×540)
├── tools/              # Build tools
│   ├── ppm2mp4.cpp     #   PPM frame stream → H.264/MP4 encoder (libx264)
│   └── mp4mux.h        #   Minimal ISOBMFF/MP4 muxer (ftyp+mdat+moov)
├── media/              # MP4 output (e.g. quad-3s.mp4)
└── Makefile
```

## Getting Started

### Requirements

- C++17 compiler (GCC 7+ or Clang 5+)
- `libx264` (for `ppm2mp4`, the built-in video encoder)

Install on Ubuntu/Debian:

```sh
sudo apt install build-essential libx264-dev
```

### ppm2mp4

`tools/ppm2mp4` is a self-contained tool built alongside the demos. It reads a raw PPM frame stream from stdin, encodes it with libx264 (H.264 baseline profile), and writes a self-contained MP4 file — no ffmpeg required.

```
Usage: ppm2mp4 --fps <N> -o <output.mp4> [--duration <seconds>]

  --fps <N>           Frames per second (required)
  -o <file>           Output MP4 path (required)
  --duration <secs>   Video duration; used to compute total frames for
                      progress display (optional, total = fps × duration)
```

The Makefile pipes demo output directly into `ppm2mp4`:

```sh
./build/quad 3 | ./build/ppm2mp4 --fps 60 --duration 3 -o media/quad-3s.mp4
```

### Build & Render

```sh
make test              # Render 3 seconds (180 frames) by default
make test DURATION=5   # Custom duration
make                   # Build only
make clean             # Remove build/ (keeps videos)
make clean-media       # Remove build/ and media/
```

Output:

| File                    | Resolution      | Content                      |
| ----------------------- | --------------- | ---------------------------- |
| `media/quad-3s.mp4`     | 1920×1080 60fps | Fullscreen procedural shader |
| `media/cube-3s.mp4`     | 960×540 60fps   | Rotating cube, Blinn-Phong   |
| `media/showcase-3s.mp4` | 960×540 60fps   | Feature showcase             |

When `DURATION` changes, the target filename changes (e.g. `quad-5s.mp4`) so Make automatically rebuilds.
When `DURATION` and source files are unchanged, rendering and encoding are skipped.

## Demo

https://github.com/user-attachments/assets/9c588869-1eeb-4a42-b85b-e28fe471d623

## Documentation

- [API Reference](docs/en-US/API.md) — pipeline config, shaders, textures, blending, math reference, pipeline flow
