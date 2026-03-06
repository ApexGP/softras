# CPU 软件光栅化渲染器

纯 C++17 实现的 CPU 软件渲染管线，无需 OpenGL / Vulkan / 任何图形库依赖。
模拟 GPU 可编程管线（顶点着色器 + 片元着色器），输出 PPM 图像序列，可由 ffmpeg 合成视频。

## 目录结构

```
shader/
├── rasterizer/
│   ├── math.h          # vec2 / vec3 / vec4 / mat4 及 GLSL 风格数学函数
│   ├── framebuffer.h   # 帧缓冲（颜色缓冲 + 深度缓冲 + PPM 输出）
│   ├── texture.h       # Texture2D（双线性采样）
│   └── pipeline.h      # 核心：模板化渲染管线 Pipeline<Vertex, Varying>
├── demo/
│   ├── demo_quad.cpp   # Demo 1：全屏四边形过程式着色
│   └── demo_cube.cpp   # Demo 2：旋转 3D 立方体 + Blinn-Phong 光照
├── assets/
│   ├── quad/           # demo_quad 输出的 PPM 帧序列
│   └── cube/           # demo_cube 输出的 PPM 帧序列
├── media/              # ffmpeg 合成的 MP4 视频
└── Makefile            # 构建入口
```

## 快速开始

### 依赖

- g++ 支持 C++17（GCC 7+ 或 Clang 5+）
- ffmpeg（合成视频，可选）

### 构建并运行

```sh
make test
```

生成：
- `media/output_quad.mp4`：1920×1080，60fps，3 秒，全屏过程式着色效果
- `media/output_cube.mp4`：960×540，60fps，3 秒，旋转 3D 立方体

PPM 帧序列分别写入 `assets/quad/` 和 `assets/cube/`，不会清空已有文件。

仅编译（不渲染）：

```sh
make
```

单独编译某个 demo：

```sh
make demo_quad
make demo_cube
```

清理编译产物（保留 PPM 和视频）：

```sh
make clean
```

---

## API 使用说明

### 1. 引入头文件

```cpp
#include "rasterizer/math.h"
#include "rasterizer/framebuffer.h"
#include "rasterizer/pipeline.h"
#include "rasterizer/texture.h"  // 可选，使用纹理时引入
```

---

### 2. 定义顶点格式和 Varying

`Varying` 是顶点着色器到片元着色器之间的插值数据，**第一个字段必须是 `vec4 clipPos`**（clip space 位置）。

```cpp
struct MyVertex {
    vec3 pos;
    vec3 normal;
    vec2 uv;
};

struct MyVarying {
    vec4 clipPos;   // ← 必须第一个，管线内部使用
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
};
```

> **约束**：`Varying` 必须是 standard-layout struct，且所有成员均为 `float` 或由 `float` 组成的结构体（vec2/vec3/vec4）。管线通过逐 float 插值实现透视正确的 varying 传递。

---

### 3. 创建帧缓冲

```cpp
Framebuffer fb(1920, 1080);
fb.clear(vec4{0.f, 0.f, 0.f, 1.f});  // 清空为黑色，深度重置为 1.0
```

---

### 4. 创建并配置管线

```cpp
Pipeline<MyVertex, MyVarying> pipe;

// 可选配置
pipe.cullBackFace     = true;   // 背面剔除（默认开）
pipe.depthTestEnabled = true;   // 深度测试（默认开）
```

---

### 5. 设置顶点着色器

顶点着色器接收一个 `MyVertex`，返回填好 `clipPos` 的 `MyVarying`。
通过 lambda 捕获 MVP 矩阵等 uniform 数据。

```cpp
mat4 mvp = proj * view * model;

pipe.setVertexShader([&](const MyVertex& v) -> MyVarying {
    vec4 clip = mvp * vec4{v.pos.x, v.pos.y, v.pos.z, 1.f};
    return {
        clip,           // clipPos（必须）
        v.pos,          // worldPos
        v.normal,
        v.uv
    };
});
```

---

### 6. 设置片元着色器

片元着色器接收插值后的 `MyVarying`，返回 `vec4` RGBA 颜色（范围 [0, 1]）。

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

### 7. 提交绘制调用

```cpp
std::vector<MyVertex> vertices = { /* ... */ };
std::vector<int>      indices  = { 0, 1, 2,  0, 2, 3 };  // 每 3 个为一个三角形（CCW）

pipe.draw(vertices, indices, fb);
```

---

### 8. 输出图像

```cpp
fb.savePPM("output.ppm");
```

---

### 纹理采样

```cpp
// 从 RGB 字节数组创建纹理
Texture2D tex = Texture2D::fromRGB8(width, height, rgbData);

// 在片元着色器中采样（双线性，repeat wrap）
pipe.setFragmentShader([&](const MyVarying& f) -> vec4 {
    return tex.sample(f.uv);
});
```

---

### 完整最小示例：彩色三角形

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
        {{ 0.f,  0.7f, 0.f}, {1.f, 0.f, 0.f}},  // 顶，红
        {{-0.7f,-0.7f, 0.f}, {0.f, 1.f, 0.f}},  // 左下，绿
        {{ 0.7f,-0.7f, 0.f}, {0.f, 0.f, 1.f}},  // 右下，蓝
    };
    std::vector<int> idx = {0, 1, 2};

    pipe.draw(verts, idx, fb);
    fb.savePPM("triangle.ppm");
    return 0;
}
```

编译：

```sh
g++ -std=c++17 -O3 triangle.cpp -o triangle && ./triangle
```

---

## 数学库速查（rasterizer/math.h）

### 向量类型

| 类型 | 字段 |
|------|------|
| `vec2` | `x, y` |
| `vec3` | `x, y, z` |
| `vec4` | `x, y, z, w`；`.xyz()` 返回 `vec3` |

所有类型支持 `+` `-` `*` `/` 及对应 `+=` 等运算符，标量与向量混合运算同 GLSL。

### 矩阵

```cpp
mat4 M = mat4::identity();
mat4 T = translate(vec3{1.f, 0.f, 0.f});
mat4 R = rotate(angle, vec3{0.f, 1.f, 0.f});   // 弧度
mat4 S = scale(vec3{2.f, 2.f, 2.f});
mat4 P = perspective(fovY, aspect, near, far);  // fovY 弧度
mat4 V = lookAt(eye, center, up);
mat4 MVP = P * V * M;   // 列主序，从右向左乘
```

### 常用函数

```cpp
dot(a, b)          length(v)          normalize(v)
cross(a, b)        reflect(I, N)
sin(v)  cos(v)     exp(v)  tanh(v)
mix(a, b, t)       clamp(v, lo, hi)   smoothstep(e0, e1, x)
pow(v, p)          abs(v)             fract(v)
```

---

## 渲染管线内部流程

```
顶点数组 + 索引
        │
        ▼
  [顶点着色器]  ─── 用户定义，输出 clipPos + Varying
        │
        ▼
  [Near-plane 裁剪]  w <= 0 的三角形丢弃
        │
        ▼
  [透视除法]  clip → NDC（÷w）
        │
        ▼
  [视口变换]  NDC → 屏幕像素坐标（y 轴翻转）
        │
        ▼
  [背面剔除]  屏幕空间有符号面积 < 0（CCW 为正面）
        │
        ▼
  [包围盒裁剪 + 光栅化]  edge function 逐像素测试
        │
        ▼
  [深度测试]  z-buffer，小于当前深度则通过
        │
        ▼
  [透视正确插值]  val/w 加权平均后 × w_interp
        │
        ▼
  [片元着色器]  ─── 用户定义，输出 RGBA
        │
        ▼
  [写入帧缓冲]
```

## 已知限制

- Near-plane 裁剪为保守策略（含 w≤0 顶点的三角形整体丢弃），不做多边形裁剪
- 单线程渲染，1080p 每帧约需数秒
- 不支持 alpha blending
- 纹理不支持 mipmap
