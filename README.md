# CPU Software Rasterizer

**English** | [中文](docs/zh-CN/README.md)

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus&logoColor=white)
![Header Only](https://img.shields.io/badge/header--only-4%20files-brightgreen)
![No Dependencies](https://img.shields.io/badge/dependencies-none-brightgreen)
![Platform](https://img.shields.io/badge/platform-linux-lightgrey?logo=linux&logoColor=white)

A header-only CPU software rendering pipeline written in C++17.
No OpenGL, Vulkan, or any graphics library required.
Simulates a GPU programmable pipeline (vertex + fragment shaders), outputs PPM image sequences, and can be encoded to MP4 via ffmpeg.

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
│
├── assets/             # PPM frame sequences (render output)
├── media/              # MP4 videos (ffmpeg output)
└── Makefile
```

## Getting Started

### Requirements

- C++17 compiler (GCC 7+ or Clang 5+)
- ffmpeg (optional, for video encoding)

### Build & Render

```sh
make test              # Render 3 seconds (180 frames) by default
make test DURATION=5   # Custom duration
make                   # Build only
make clean             # Remove build artifacts (keeps PPM and videos)
```

Output:

| File                 | Resolution      | Content                     |
| -------------------- | --------------- | --------------------------- |
| `media/quad.mp4`     | 1920×1080 60fps | Fullscreen procedural shade |
| `media/cube.mp4`     | 960×540 60fps   | Rotating cube, Blinn-Phong  |
| `media/showcase.mp4` | 960×540 60fps   | Feature showcase            |

When `DURATION` changes, old frames are automatically purged and re-rendered.
When `DURATION` and source files are unchanged, rendering and encoding are skipped.

## Demo

https://github.com/user-attachments/assets/9c588869-1eeb-4a42-b85b-e28fe471d623

## Documentation

- [API Reference](docs/en-US/API.md) — pipeline config, shaders, textures, blending, math reference, pipeline flow
