// demo/showcase.cpp — 功能展示 Demo
// 演示：SH 裁剪 / Mipmap 纹理地板 / Alpha Blending / 线框模式

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../rasterizer/framebuffer.h"
#include "../rasterizer/math.h"
#include "../rasterizer/pipeline.h"
#include "../rasterizer/texture.h"

// ── 通用顶点 / Varying ─────────────────────────
struct Vertex {
    vec3 pos;
    vec3 normal;
    vec2 uv;
    vec3 color;
};

struct Varying {
    vec4 clipPos;  // 必须第一个
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
    vec3 color;
};

// ── 立方体几何 ─────────────────────────────────
static std::vector<Vertex> makeCubeVertices(vec3 col)
{
    struct Face {
        vec3 n;
        vec3 verts[4];
        vec2 uvs[4];
    };
    const Face faces[] = {
        {{1, 0, 0},
         {{1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}},
         {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
        {{-1, 0, 0},
         {{-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}, {-1, -1, -1}},
         {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
        {{0, 1, 0},
         {{-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}},
         {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
        {{0, -1, 0},
         {{-1, -1, 1}, {-1, -1, -1}, {1, -1, -1}, {1, -1, 1}},
         {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
        {{0, 0, 1},
         {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},
         {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
        {{0, 0, -1},
         {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
         {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
    };
    std::vector<Vertex> verts;
    for (auto &face : faces) {
        for (int i = 0; i < 4; ++i) {
            verts.push_back({face.verts[i], face.n, face.uvs[i], col});
        }
    }
    return verts;
}

static std::vector<int> makeCubeIndices()
{
    std::vector<int> idx;
    for (int f = 0; f < 6; ++f) {
        int b = f * 4;
        idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    }
    return idx;
}

// ── 棋盘格地板几何 ──────────────────────────────
// 一块大平面，UV 范围 [0, tileCount]，fragment 着色器采样纹理
static std::vector<Vertex> makeFloorVertices(float halfSize, vec3 col)
{
    float h = halfSize;
    float tc = halfSize * 0.5f;  // 6 格铺砌（原 24 格太密，mip 后全糊）
    return {
        {{-h, 0, -h}, {0, 1, 0}, {0, 0}, col},
        {{-h, 0, h}, {0, 1, 0}, {0, tc}, col},
        {{h, 0, h}, {0, 1, 0}, {tc, tc}, col},
        {{h, 0, -h}, {0, 1, 0}, {tc, 0}, col},
    };
}

static std::vector<int> makeFloorIndices()
{
    return {0, 1, 2, 0, 2, 3};
}

// ── 棋盘格纹理（程序生成）──────────────────────
// 使用 8×8 像素的方块，而非 1×1 交替：
//   1px 交替在第 1 级 mip（2×2 均值）后直接变均匀灰，完全失去图案
//   8px 方块在 3 级 mip 后才退化，能在合理视距内清晰显示
static Texture2D makeCheckerTexture(int res)
{
    const int sq = res / 8;  // 方块大小：8 格 × 8 格
    std::vector<vec4> pixels(static_cast<size_t>(res * res));
    for (int y = 0; y < res; ++y) {
        for (int x = 0; x < res; ++x) {
            bool checker = (((x / sq) ^ (y / sq)) & 1) != 0;
            pixels[y * res + x] =
                checker ? vec4{0.9f, 0.9f, 0.9f, 1.f} : vec4{0.12f, 0.12f, 0.16f, 1.f};
        }
    }
    Texture2D tex = Texture2D::fromVec4(res, res, pixels.data());
    tex.generateMipmaps();
    return tex;
}

int main(int argc, char *argv[])
{
    const int kWidth = 16 * 60;
    const int kHeight = 9 * 60;
    const float kFPS = 60.f;
    const int kSeconds = (argc > 1) ? std::atoi(argv[1]) : 3;
    const int kFrames = kSeconds * static_cast<int>(kFPS);
    const float kAspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);

    // ── 场景参数 ──────────────────────────────────
    const vec3 kLightDir = normalize(vec3{1.f, 2.f, 1.5f});
    const vec3 kAmbient = {0.12f, 0.12f, 0.15f};
    // 近平面 0.4 —— 相机绕近旋转时地板三角形会触发 SH 裁剪
    mat4 proj = perspective(0.9f, kAspect, 0.4f, 60.f);

    // ── 棋盘格纹理（Feature 2: mipmap）──────────
    Texture2D checker = makeCheckerTexture(64);

    // ── 几何数据 ──────────────────────────────────
    auto floorVerts = makeFloorVertices(12.f, {1.f, 1.f, 1.f});
    auto floorIdx = makeFloorIndices();

    auto blueCubeVerts = makeCubeVertices({0.3f, 0.5f, 0.9f});
    auto cubeIdx = makeCubeIndices();

    auto orangeCubeVerts = makeCubeVertices({0.95f, 0.55f, 0.15f});
    auto greenCubeVerts = makeCubeVertices({0.25f, 0.85f, 0.35f});
    auto yellowCubeVerts = makeCubeVertices({0.95f, 0.9f, 0.2f});

    // ── 管线 ──────────────────────────────────────
    Pipeline<Vertex, Varying> pipe;

    Framebuffer fb(kWidth, kHeight);

    for (int frame = 0; frame < kFrames; ++frame) {
        float t = static_cast<float>(frame) / kFPS;

        // 相机轨道：绕原点半径 7，高度 3，慢速旋转
        float camAng = t * 0.4f;
        vec3 camPos = {std::sin(camAng) * 7.f, 3.0f, std::cos(camAng) * 7.f};
        mat4 view = lookAt(camPos, {0.f, 0.5f, 0.f}, {0.f, 1.f, 0.f});

        // 帧缓冲清空
        fb.clear(vec4{0.04f, 0.04f, 0.06f, 1.f});

        // ── 设置通用顶点着色器（接受 model 矩阵引用）──
        auto makeVS = [&](const mat4 &model, const mat4 &mvp) {
            return [model, mvp, &camPos](const Vertex &v) -> Varying {
                vec4 wp = model * vec4{v.pos.x, v.pos.y, v.pos.z, 1.f};
                vec4 n4 = model * vec4{v.normal.x, v.normal.y, v.normal.z, 0.f};
                return {mvp * vec4{v.pos.x, v.pos.y, v.pos.z, 1.f}, wp.xyz(), normalize(n4.xyz()),
                        v.uv, v.color};
            };
        };

        // ── Blinn-Phong 片元着色器（不含纹理）──────
        auto blinnPhongFS = [&](const Varying &f) -> vec4 {
            vec3 N = normalize(f.normal);
            vec3 L = kLightDir;
            vec3 V = normalize(camPos - f.worldPos);
            vec3 H = normalize(L + V);
            float diff = std::max(0.f, dot(N, L));
            float spec = std::pow(std::max(0.f, dot(N, H)), 48.f);
            vec3 col = f.color * (kAmbient + diff * vec3{0.78f, 0.78f, 0.72f}) +
                       vec3{spec * 0.4f, spec * 0.4f, spec * 0.4f};
            col = clamp(col, 0.f, 1.f);
            return {col.x, col.y, col.z, 1.f};
        };

        // ────────────────────────────────────────────
        // Pass 1：不透明物体（背面剔除，深度写入）
        // ────────────────────────────────────────────
        pipe.cullBackFace = true;
        pipe.depthTestEnabled = true;
        pipe.depthWriteEnabled = true;
        pipe.wireframe = false;
        pipe.blendEnabled = false;

        // -- 地板（Feature 1: SH 裁剪；Feature 2: mipmap LOD）--
        {
            mat4 model = translate(vec3{0.f, -1.f, 0.f});
            mat4 mvp = proj * view * model;
            pipe.setVertexShader(makeVS(model, mvp));
            pipe.setFragmentShader([&](const Varying &f) -> vec4 {
                // LOD 基于摄像机距离，远处使用更高 mip 级别
                float dist = length(camPos - f.worldPos);
                // LOD 系数 0.05：与 UV 铺砌 6 格 + 8px 方块配套
                // dist=8 → lod≈0.5（清晰），dist=30 → lod≈1.3（开始模糊）
                float lod = std::log2(dist * 0.05f + 1.f);
                vec4 tc = checker.sampleLod(f.uv, lod);
                // 简单漫反射（地板法线固定朝上）
                float diff = std::max(0.f, dot(vec3{0.f, 1.f, 0.f}, kLightDir));
                vec3 col = tc.xyz() * (kAmbient + diff * vec3{0.75f, 0.75f, 0.72f});
                col = clamp(col, 0.f, 1.f);
                return {col.x, col.y, col.z, 1.f};
            });
            pipe.draw(floorVerts, floorIdx, fb);
        }

        // -- 左侧静止蓝色立方体 --
        {
            mat4 model = translate(vec3{-2.8f, -0.2f, -1.f}) * scale(vec3{0.8f, 0.8f, 0.8f});
            mat4 mvp = proj * view * model;
            pipe.setVertexShader(makeVS(model, mvp));
            pipe.setFragmentShader(blinnPhongFS);
            pipe.draw(blueCubeVerts, cubeIdx, fb);
        }

        // -- 右侧旋转橙色立方体 --
        {
            mat4 model = translate(vec3{2.5f, -0.2f, 0.f}) * rotate(t * 1.2f, {0.f, 1.f, 0.f}) *
                         scale(vec3{0.75f, 0.75f, 0.75f});
            mat4 mvp = proj * view * model;
            pipe.setVertexShader(makeVS(model, mvp));
            pipe.setFragmentShader(blinnPhongFS);
            pipe.draw(orangeCubeVerts, cubeIdx, fb);
        }

        // ────────────────────────────────────────────
        // Pass 2：半透明绿色立方体（Feature 3: alpha blending）
        // 关闭深度写入，双面渲染，src-over 合成
        // ────────────────────────────────────────────
        {
            pipe.cullBackFace = false;
            pipe.depthWriteEnabled = false;
            pipe.blendEnabled = true;

            mat4 model = translate(vec3{0.f, 0.2f, 0.5f}) * rotate(t * 0.7f, {0.f, 1.f, 0.f}) *
                         rotate(t * 0.3f, {1.f, 0.f, 0.f}) * scale(vec3{0.9f, 0.9f, 0.9f});
            mat4 mvp = proj * view * model;
            pipe.setVertexShader(makeVS(model, mvp));
            pipe.setFragmentShader([&](const Varying &f) -> vec4 {
                vec3 N = normalize(f.normal);
                float diff = std::max(0.f, dot(N, kLightDir));
                vec3 col = f.color * (kAmbient + diff * vec3{0.78f, 0.78f, 0.72f});
                col = clamp(col, 0.f, 1.f);
                return {col.x, col.y, col.z, 0.45f};  // α = 0.45
            });
            pipe.draw(greenCubeVerts, cubeIdx, fb);
        }

        // ────────────────────────────────────────────
        // Pass 3：线框立方体（Feature 4: wireframe）
        // ────────────────────────────────────────────
        {
            pipe.cullBackFace = false;
            pipe.depthWriteEnabled = true;
            pipe.blendEnabled = false;
            pipe.wireframe = true;
            pipe.wireframeWidth = 1.5f;

            mat4 model = translate(vec3{0.5f, 0.6f, -2.8f}) * rotate(t * 0.5f, {0.3f, 1.f, 0.2f}) *
                         scale(vec3{0.7f, 0.7f, 0.7f});
            mat4 mvp = proj * view * model;
            pipe.setVertexShader(makeVS(model, mvp));
            pipe.setFragmentShader([](const Varying &f) -> vec4 {
                (void) f;
                return {0.95f, 0.9f, 0.2f, 1.f};  // 黄色线框
            });
            pipe.draw(yellowCubeVerts, cubeIdx, fb);
        }

        fb.writePPM(stdout);
        std::fprintf(stderr, "showcase frame %d/%d\r", frame + 1, kFrames);
        std::fflush(stderr);
    }
    std::fprintf(stderr, "\n");
    return 0;
}
