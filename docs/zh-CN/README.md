# CPU 软件光栅化渲染器

[English](../../README.md) | **中文**

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus&logoColor=white)
![Header Only](https://img.shields.io/badge/header--only-4%20files-brightgreen)
![Video](https://img.shields.io/badge/video-libx264-orange)
![Platform](https://img.shields.io/badge/platform-linux-lightgrey?logo=linux&logoColor=white)

纯 C++ 实现的 CPU 软件渲染管线，无需 OpenGL / Vulkan / 任何图形库依赖。
模拟 GPU 可编程管线（顶点着色器 + 片元着色器），通过 pipe 将 PPM 帧直接流式传输给内置编码器 `ppm2mp4`，磁盘零中间文件。

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
softras/
├── rasterizer/         # 核心头文件（header-only）
│   ├── math.h          #   vec2/vec3/vec4/mat4 + GLSL 风格函数
│   ├── framebuffer.h   #   颜色缓冲 + 深度缓冲 + PPM 输出
│   ├── texture.h       #   Texture2D，双线性/三线性采样
│   └── pipeline.h      #   Pipeline<Vertex, Varying>
├── demo/               # 示例程序
│   ├── quad.cpp        #   全屏四边形过程式着色（1920×1080）
│   ├── cube.cpp        #   旋转立方体 + Blinn-Phong（960×540）
│   └── showcase.cpp    #   综合功能展示（960×540）
├── tools/              # 构建工具
│   ├── ppm2mp4.cpp     #   PPM 帧流 → H.264/MP4 编码器（libx264）
│   └── mp4mux.h        #   最小化 ISOBMFF/MP4 封装器（ftyp+mdat+moov）
├── media/              # MP4 输出（如 quad-3s.mp4）
└── Makefile
```

## 快速开始

### 依赖

- C++17（GCC 7+ 或 Clang 5+）
- `libx264`（供内置工具 `ppm2mp4` 使用）

Ubuntu/Debian 安装：

```sh
sudo apt install build-essential libx264-dev
```

### ppm2mp4

`tools/ppm2mp4` 是随 demo 一同构建的独立工具。它从 stdin 读取原始 PPM 帧流，使用 libx264（H.264 baseline profile）编码，并写出自包含的 MP4 文件，无需 ffmpeg。

```
Usage: ppm2mp4 --fps <N> -o <output.mp4> [--duration <seconds>]

  --fps <N>            帧率（必填）
  -o <file>            输出 MP4 路径（必填）
  --duration <secs>    视频时长（秒），内部计算总帧数 = fps × duration，
                       用于进度显示（可选）
```

Makefile 将 demo 输出直接 pipe 进 `ppm2mp4`：

```sh
./build/quad 3 | ./build/ppm2mp4 --fps 60 --duration 3 -o media/quad-3s.mp4
```

### 构建并渲染

```sh
make test              # 默认渲染 3 秒（180 帧）
make test DURATION=5   # 自定义时长
make                   # 仅编译
make clean             # 删除 build/（保留视频）
make clean-media       # 删除 build/ 和 media/
```

生成：

| 文件                    | 分辨率          | 内容                   |
| ----------------------- | --------------- | ---------------------- |
| `media/quad-3s.mp4`     | 1920×1080 60fps | 全屏过程式着色         |
| `media/cube-3s.mp4`     | 960×540 60fps   | 旋转立方体 Blinn-Phong |
| `media/showcase-3s.mp4` | 960×540 60fps   | 功能综合展示           |

DURATION 变化时目标文件名随之改变（如 `quad-5s.mp4`），Make 自动触发重建；DURATION 不变且源码未改动时跳过渲染和编码。

## 渲染示例

https://github.com/user-attachments/assets/9c588869-1eeb-4a42-b85b-e28fe471d623

## 文档

- [API 使用说明](API.md) — 管线配置、着色器、纹理、blending、数学库速查、管线流程图
