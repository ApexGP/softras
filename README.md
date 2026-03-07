# CPU 软件光栅化渲染器

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus&logoColor=white)
![Header Only](https://img.shields.io/badge/header--only-4%20files-brightgreen)
![No Dependencies](https://img.shields.io/badge/dependencies-none-brightgreen)
![Platform](https://img.shields.io/badge/platform-linux-lightgrey?logo=linux&logoColor=white)

纯 C++ 实现的 CPU 软件渲染管线，无需 OpenGL / Vulkan / 任何图形库依赖。
模拟 GPU 可编程管线（顶点着色器 + 片元着色器），输出 PPM 图像序列，可由 ffmpeg 合成视频。

## 功能特性

- 模板化渲染管线 `Pipeline<Vertex, Varying>`，着色器为用户定义的 lambda
- 透视正确插值（Varying 按 `val/w` 加权）
- Sutherland-Hodgman 六平面齐次空间裁剪
- 多线程光栅化（线程池，按行分块，无锁）
- 增量式光栅化（edge function 步进 + 深度先行）
- Mipmap 三线性采样（`generateMipmaps()` + `sampleLod()`）
- Alpha blending（src-over）及独立深度写入开关
- 线框（wireframe）渲染模式

## 目录结构

```
shader/
├── rasterizer/         # 核心头文件（header-only）
│   ├── math.h          #   vec2/vec3/vec4/mat4 + GLSL 风格函数
│   ├── framebuffer.h   #   颜色缓冲 + 深度缓冲 + PPM 输出
│   ├── texture.h       #   Texture2D，双线性/三线性采样
│   └── pipeline.h      #   Pipeline<Vertex, Varying>
├── demo/               # 示例程序
│   ├── quad.cpp        #   全屏四边形过程式着色（1920×1080）
│   ├── cube.cpp        #   旋转立方体 + Blinn-Phong（960×540）
│   └── showcase.cpp    #   综合功能展示（960×540）
├── docs/
│   └── api.md          # API 使用文档
├── assets/             # PPM 帧序列（渲染产物）
├── media/              # MP4 视频（ffmpeg 合成产物）
└── Makefile
```

## 快速开始

### 依赖

- g++ C++17（GCC 7+ 或 Clang 5+）
- ffmpeg（合成视频，可选）

### 构建并渲染

```sh
make test              # 默认渲染 3 秒（180 帧）
make test DURATION=5   # 自定义时长
make                   # 仅编译
make clean             # 删除编译产物（保留 PPM 和视频）
```

生成：

| 文件                 | 分辨率          | 内容                   |
| -------------------- | --------------- | ---------------------- |
| `media/quad.mp4`     | 1920×1080 60fps | 全屏过程式着色         |
| `media/cube.mp4`     | 960×540 60fps   | 旋转立方体 Blinn-Phong |
| `media/showcase.mp4` | 960×540 60fps   | 功能综合展示           |

DURATION 变化时自动清除旧帧重新渲染；DURATION 不变且源码未改动时跳过渲染和编码。

## 文档

- [API 使用说明](docs/api.md) — 管线配置、着色器、纹理、blending、数学库速查、管线流程图
