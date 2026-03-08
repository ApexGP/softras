# API 使用说明

[English](../en-US/api.md) | **中文**

## 引入头文件

```cpp
#include "rasterizer/math.h"
#include "rasterizer/framebuffer.h"
#include "rasterizer/pipeline.h"
#include "rasterizer/texture.h"  // 使用纹理时引入
```

---

## 定义顶点格式和 Varying

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

## 创建帧缓冲

```cpp
Framebuffer fb(1920, 1080);
fb.clear(vec4{0.f, 0.f, 0.f, 1.f});  // 清空为黑色，深度重置为 1.0
```

---

## 创建并配置管线

```cpp
Pipeline<MyVertex, MyVarying> pipe;

// 基础控制
pipe.cullBackFace      = true;   // 背面剔除（默认开）
pipe.depthTestEnabled  = true;   // 深度测试（默认开）
pipe.depthWriteEnabled = true;   // 深度写入（默认开，半透明 pass 关闭）

// Alpha blending（src-over）
pipe.blendEnabled = false;       // 默认关

// 线框模式
pipe.wireframe      = false;     // 默认关
pipe.wireframeWidth = 1.0f;      // 线框像素宽度

// 多线程
pipe.threadCount = 0;            // 0 = 自动（hardware_concurrency），1 = 单线程
```

---

## 设置顶点着色器

```cpp
mat4 mvp = proj * view * model;

pipe.setVertexShader([&](const MyVertex& v) -> MyVarying {
    vec4 clip = mvp * vec4{v.pos.x, v.pos.y, v.pos.z, 1.f};
    return { clip, v.pos, v.normal, v.uv };
});
```

---

## 设置片元着色器

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

## 提交绘制调用

```cpp
std::vector<MyVertex> vertices = { /* ... */ };
std::vector<int>      indices  = { 0, 1, 2,  0, 2, 3 };  // 每 3 个为一个三角形（CCW）

pipe.draw(vertices, indices, fb);
```

---

## 输出图像

```cpp
fb.savePPM("output.ppm");
```

---

## 纹理采样

```cpp
// 从 RGB 字节数组创建纹理
Texture2D tex = Texture2D::fromRGB8(width, height, rgbData);

// 基础双线性采样（无 mipmap）
tex.sample(f.uv);

// 生成 mipmap 并三线性采样
tex.generateMipmaps();
tex.sampleLod(f.uv, lod);  // lod 为浮点 mip 级别
```

---

## Alpha Blending（半透明多 Pass）

```cpp
// Pass 1：渲染不透明物体（正常写入深度）
pipe.blendEnabled      = false;
pipe.depthWriteEnabled = true;
pipe.draw(opaqueVerts, opaqueIdx, fb);

// Pass 2：渲染半透明物体（读深度不写深度）
pipe.blendEnabled      = true;
pipe.depthWriteEnabled = false;
pipe.draw(transVerts, transIdx, fb);
```

---

## 完整最小示例：彩色三角形

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
g++ -std=c++17 -O3 -I. triangle.cpp -o triangle && ./triangle
```

---

## 数学库速查（rasterizer/math.h）

### 向量类型

| 类型   | 字段                               |
| ------ | ---------------------------------- |
| `vec2` | `x, y`                             |
| `vec3` | `x, y, z`                          |
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
  [SH 六平面裁剪]  齐次空间 Sutherland-Hodgman，Fan 三角化
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
  [包围盒裁剪 + 多线程光栅化]  edge function 步进，按行并行
        │
        ▼
  [深度测试]  z-buffer；depthWriteEnabled 控制是否回写
        │
        ▼
  [透视正确插值]  val/w 加权平均后 × w_interp
        │
        ▼
  [片元着色器]  ─── 用户定义，输出 RGBA
        │
        ▼
  [Alpha Blending / 写入帧缓冲]  src-over（可选）
```
