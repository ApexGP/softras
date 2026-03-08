# API Reference

**English** | [中文](../zh-CN/api.md)

## Include Headers

```cpp
#include "rasterizer/math.h"
#include "rasterizer/framebuffer.h"
#include "rasterizer/pipeline.h"
#include "rasterizer/texture.h"  // include when using textures
```

---

## Define Vertex and Varying

`Varying` carries interpolated data from the vertex shader to the fragment shader.
**The first field must be `vec4 clipPos`** (clip-space position).

```cpp
struct MyVertex {
    vec3 pos;
    vec3 normal;
    vec2 uv;
};

struct MyVarying {
    vec4 clipPos;   // ← must be first; used internally by the pipeline
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
};
```

> **Constraint**: `Varying` must be a standard-layout struct whose members are all `float` or structs composed of `float` (vec2/vec3/vec4). The pipeline interpolates varyings component-by-component to achieve perspective-correct results.

---

## Create a Framebuffer

```cpp
Framebuffer fb(1920, 1080);
fb.clear(vec4{0.f, 0.f, 0.f, 1.f});  // clear to black; depth reset to 1.0
```

---

## Create and Configure the Pipeline

```cpp
Pipeline<MyVertex, MyVarying> pipe;

// Basic toggles
pipe.cullBackFace      = true;   // back-face culling (default: on)
pipe.depthTestEnabled  = true;   // depth test      (default: on)
pipe.depthWriteEnabled = true;   // depth write     (default: on; disable for transparent passes)

// Alpha blending (src-over)
pipe.blendEnabled = false;       // default: off

// Wireframe mode
pipe.wireframe      = false;     // default: off
pipe.wireframeWidth = 1.0f;      // wireframe line width in pixels

// Multithreading
pipe.threadCount = 0;            // 0 = auto (hardware_concurrency), 1 = single-threaded
```

---

## Set the Vertex Shader

```cpp
mat4 mvp = proj * view * model;

pipe.setVertexShader([&](const MyVertex& v) -> MyVarying {
    vec4 clip = mvp * vec4{v.pos.x, v.pos.y, v.pos.z, 1.f};
    return { clip, v.pos, v.normal, v.uv };
});
```

---

## Set the Fragment Shader

```cpp
pipe.setFragmentShader([&](const MyVarying& f) -> vec4 {
    vec3 N = normalize(f.normal);
    vec3 L = normalize(vec3{1.f, 2.f, 1.f});
    float diff = std::max(0.f, dot(N, L));
    vec3 col = vec3{0.8f, 0.5f, 0.3f} * (0.1f + diff);
    return {col.x, col.y, col.z, 1.f};
});
```

---

## Submit a Draw Call

```cpp
std::vector<MyVertex> vertices = { /* ... */ };
std::vector<int>      indices  = { 0, 1, 2,  0, 2, 3 };  // 3 indices per triangle (CCW winding)

pipe.draw(vertices, indices, fb);
```

---

## Save the Image

```cpp
fb.savePPM("output.ppm");
```

---

## Texture Sampling

```cpp
// Create from raw RGB byte array
Texture2D tex = Texture2D::fromRGB8(width, height, rgbData);

// Basic bilinear sampling (no mipmap)
tex.sample(f.uv);

// Generate mipmaps and sample trilinearly
tex.generateMipmaps();
tex.sampleLod(f.uv, lod);  // lod is a floating-point mip level
```

---

## Alpha Blending (Transparent Multi-Pass)

```cpp
// Pass 1: opaque geometry — normal depth write
pipe.blendEnabled      = false;
pipe.depthWriteEnabled = true;
pipe.draw(opaqueVerts, opaqueIdx, fb);

// Pass 2: transparent geometry — read depth, no write
pipe.blendEnabled      = true;
pipe.depthWriteEnabled = false;
pipe.draw(transVerts, transIdx, fb);
```

---

## Minimal Example: Colored Triangle

```cpp
#include "rasterizer/math.h"
#include "rasterizer/framebuffer.h"
#include "rasterizer/pipeline.h"

struct Vert    { vec3 pos; vec3 color; };
struct Varying { vec4 clipPos; vec3 color; };

int main() {
    Framebuffer fb(800, 600);
    fb.clear(vec4{0.f, 0.f, 0.f, 1.f});

    Pipeline<Vert, Varying> pipe;
    pipe.depthTestEnabled = false;
    pipe.cullBackFace     = false;

    pipe.setVertexShader([](const Vert& v) -> Varying {
        return { vec4{v.pos.x, v.pos.y, v.pos.z, 1.f}, v.color };
    });
    pipe.setFragmentShader([](const Varying& f) -> vec4 {
        return { f.color.x, f.color.y, f.color.z, 1.f };
    });

    std::vector<Vert> verts = {
        {{ 0.f,  0.7f, 0.f}, {1.f, 0.f, 0.f}},  // top,        red
        {{-0.7f,-0.7f, 0.f}, {0.f, 1.f, 0.f}},  // bottom-left, green
        {{ 0.7f,-0.7f, 0.f}, {0.f, 0.f, 1.f}},  // bottom-right, blue
    };
    std::vector<int> idx = {0, 1, 2};

    pipe.draw(verts, idx, fb);
    fb.savePPM("triangle.ppm");
    return 0;
}
```

Compile:

```sh
g++ -std=c++17 -O3 -I. triangle.cpp -o triangle && ./triangle
```

---

## Math Reference (rasterizer/math.h)

### Vector Types

| Type   | Fields                                |
| ------ | ------------------------------------- |
| `vec2` | `x, y`                                |
| `vec3` | `x, y, z`                             |
| `vec4` | `x, y, z, w`; `.xyz()` returns `vec3` |

All types support `+` `-` `*` `/` and compound assignment operators.
Scalar–vector mixed arithmetic follows GLSL conventions.

### Matrices

```cpp
mat4 M = mat4::identity();
mat4 T = translate(vec3{1.f, 0.f, 0.f});
mat4 R = rotate(angle, vec3{0.f, 1.f, 0.f});   // angle in radians
mat4 S = scale(vec3{2.f, 2.f, 2.f});
mat4 P = perspective(fovY, aspect, near, far);  // fovY in radians
mat4 V = lookAt(eye, center, up);
mat4 MVP = P * V * M;   // column-major; multiply right to left
```

### Common Functions

```cpp
dot(a, b)          length(v)          normalize(v)
cross(a, b)        reflect(I, N)
sin(v)  cos(v)     exp(v)  tanh(v)
mix(a, b, t)       clamp(v, lo, hi)   smoothstep(e0, e1, x)
pow(v, p)          abs(v)             fract(v)
```

---

## Pipeline Internals

```
vertex array + index buffer
        │
        ▼
  [Vertex Shader]   user-defined; outputs clipPos + Varying
        │
        ▼
  [SH Clipping]     Sutherland-Hodgman, 6 homogeneous planes, fan triangulation
        │
        ▼
  [Perspective Divide]   clip → NDC  (÷w)
        │
        ▼
  [Viewport Transform]   NDC → screen pixels  (y-flip)
        │
        ▼
  [Back-face Culling]    signed screen-space area < 0  (CCW = front)
        │
        ▼
  [AABB Clip + Multithreaded Rasterization]   edge function stepping, row-parallel
        │
        ▼
  [Depth Test]      z-buffer; depthWriteEnabled controls write-back
        │
        ▼
  [Perspective-Correct Interpolation]   val/w weighted, then × w_interp
        │
        ▼
  [Fragment Shader]  user-defined; outputs RGBA
        │
        ▼
  [Alpha Blending / Framebuffer Write]   src-over (optional)
```
