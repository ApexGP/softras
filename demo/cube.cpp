// demo/cube.cpp — 旋转 3D 立方体 Demo
// 演示完整 3D 管线：mat4 MVP 变换 + 深度测试 + Phong 光照

#include <cmath>
#include <cstdio>
#include <vector>

#include "../rasterizer/framebuffer.h"
#include "../rasterizer/math.h"
#include "../rasterizer/pipeline.h"

// ── 顶点格式 ──────────────────────────────────
struct CubeVertex {
    vec3 pos;     // 模型空间位置
    vec3 normal;  // 模型空间法线
    vec3 color;   // 顶点颜色
};

// ── Varying 格式（第一个字段必须是 clipPos）──
struct CubeVarying {
    vec4 clipPos;   // clip space 位置（管线使用，必须第一个）
    vec3 worldPos;  // 世界空间位置（用于光照）
    vec3 normal;    // 世界空间法线
    vec3 color;     // 插值颜色
};

// ── 立方体几何数据 ─────────────────────────────
// 每面 2 个三角形，共 6 面 × 2 = 12 个三角形
static std::vector<CubeVertex> makeCubeVertices()
{
    // 6 个面，每面 4 顶点，逆时针排列（从外面看）
    // 面法线：+X -X +Y -Y +Z -Z
    struct Face {
        vec3 n;
        vec3 verts[4];
        vec3 col;
    };
    const Face faces[] = {
        {{1, 0, 0}, {{1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}}, {0.9f, 0.3f, 0.3f}},  // +X 红
        {{-1, 0, 0},
         {{-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}, {-1, -1, -1}},
         {0.3f, 0.9f, 0.3f}},                                                               // -X 绿
        {{0, 1, 0}, {{-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}}, {0.3f, 0.3f, 0.9f}},  // +Y 蓝
        {{0, -1, 0},
         {{-1, -1, 1}, {-1, -1, -1}, {1, -1, -1}, {1, -1, 1}},
         {0.9f, 0.9f, 0.3f}},                                                               // -Y 黄
        {{0, 0, 1}, {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}, {0.3f, 0.9f, 0.9f}},  // +Z 青
        {{0, 0, -1},
         {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
         {0.9f, 0.3f, 0.9f}},  // -Z 紫
    };

    std::vector<CubeVertex> verts;
    for (auto &face : faces) {
        for (int i = 0; i < 4; ++i) {
            verts.push_back({face.verts[i], face.n, face.col});
        }
    }
    return verts;
}

// 每面 4 顶点，产生 2 个三角形（CCW）
static std::vector<int> makeCubeIndices()
{
    std::vector<int> idx;
    for (int f = 0; f < 6; ++f) {
        int base = f * 4;
        idx.insert(idx.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    }
    return idx;
}

int main()
{
    const int kWidth = 16 * 60;  // 960
    const int kHeight = 9 * 60;  // 540
    const int kFrames = 60 * 3;  // 180 帧
    const float kFPS = 60.f;
    const float kAspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);

    // ── 场景参数 ──────────────────────────────────
    const vec3 kLightDir = normalize(vec3{1.f, 2.f, 3.f});
    const vec3 kAmbient = {0.15f, 0.15f, 0.18f};
    const vec3 kCamPos = {0.f, 0.f, 4.5f};

    mat4 proj = perspective(0.8f, kAspect, 0.1f, 100.f);
    mat4 view = lookAt(kCamPos, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});

    auto cubeVerts = makeCubeVertices();
    auto cubeIndices = makeCubeIndices();

    // ── 构建管线 ──────────────────────────────────
    Pipeline<CubeVertex, CubeVarying> pipe;
    pipe.cullBackFace = true;
    pipe.depthTestEnabled = true;

    Framebuffer fb(kWidth, kHeight);
    char buf[256];

    for (int frame = 0; frame < kFrames; ++frame) {
        float time = static_cast<float>(frame) / kFPS;

        // 模型矩阵：绕 Y 轴旋转 + 轻微绕 X 轴倾斜
        mat4 model = rotate(time * 1.1f, {0.f, 1.f, 0.f}) * rotate(time * 0.4f, {1.f, 0.f, 0.f});
        mat4 mvp = proj * view * model;
        // 法线矩阵（model 仅含旋转，逆转置 = 本身）
        mat4 normalMat = model;

        // ── 顶点着色器 ──────────────────────────────
        pipe.setVertexShader([&](const CubeVertex &v) -> CubeVarying {
            vec4 worldPos4 = model * vec4{v.pos.x, v.pos.y, v.pos.z, 1.f};
            vec4 n4 = normalMat * vec4{v.normal.x, v.normal.y, v.normal.z, 0.f};
            return {mvp * vec4{v.pos.x, v.pos.y, v.pos.z, 1.f}, worldPos4.xyz(),
                    normalize(n4.xyz()), v.color};
        });

        // ── 片元着色器：Blinn-Phong ──────────────────
        pipe.setFragmentShader([&](const CubeVarying &f) -> vec4 {
            vec3 N = normalize(f.normal);
            vec3 L = kLightDir;
            vec3 V = normalize(kCamPos - f.worldPos);
            vec3 H = normalize(L + V);

            float diff = std::max(0.f, dot(N, L));
            float spec = std::pow(std::max(0.f, dot(N, H)), 64.f);

            vec3 col = f.color * (kAmbient + diff * vec3(0.8f, 0.8f, 0.75f)) +
                       vec3(spec * 0.5f, spec * 0.5f, spec * 0.5f);
            col = clamp(col, 0.f, 1.f);
            return {col.x, col.y, col.z, 1.f};
        });

        fb.clear(vec4{0.05f, 0.05f, 0.08f, 1.f});
        pipe.draw(cubeVerts, cubeIndices, fb);

        std::snprintf(buf, sizeof(buf), "assets/cube/frame-%02d.ppm", frame);
        fb.savePPM(buf);
        std::printf("cube frame %d/%d\n", frame + 1, kFrames);
    }

    return 0;
}
