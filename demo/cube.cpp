// demo/cube.cpp — Rotating 3D cube demo
// Demonstrates the full 3D pipeline: mat4 MVP transform + depth test + Phong lighting

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../rasterizer/framebuffer.h"
#include "../rasterizer/math.h"
#include "../rasterizer/pipeline.h"

// ── Vertex format ─────────────────────────────
struct CubeVertex {
    vec3 pos;     // model-space position
    vec3 normal;  // model-space normal
    vec3 color;   // vertex color
};

// ── Varying format (first field must be clipPos) ──
struct CubeVarying {
    vec4 clipPos;   // clip-space position (used by pipeline; must be first)
    vec3 worldPos;  // world-space position (for lighting)
    vec3 normal;    // world-space normal
    vec3 color;     // interpolated color
};

// ── Cube geometry ──────────────────────────────
// 6 faces × 2 triangles = 12 triangles total
static std::vector<CubeVertex> makeCubeVertices()
{
    // 6 faces, 4 vertices each, CCW winding (viewed from outside)
    // Face normals: +X -X +Y -Y +Z -Z
    struct Face {
        vec3 n;
        vec3 verts[4];
        vec3 col;
    };
    const Face faces[] = {
        {{1, 0, 0},
         {{1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}},
         {0.9f, 0.3f, 0.3f}},  // +X red
        {{-1, 0, 0},
         {{-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}, {-1, -1, -1}},
         {0.3f, 0.9f, 0.3f}},  // -X green
        {{0, 1, 0},
         {{-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}},
         {0.3f, 0.3f, 0.9f}},  // +Y blue
        {{0, -1, 0},
         {{-1, -1, 1}, {-1, -1, -1}, {1, -1, -1}, {1, -1, 1}},
         {0.9f, 0.9f, 0.3f}},  // -Y yellow
        {{0, 0, 1},
         {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},
         {0.3f, 0.9f, 0.9f}},  // +Z cyan
        {{0, 0, -1},
         {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
         {0.9f, 0.3f, 0.9f}},  // -Z magenta
    };

    std::vector<CubeVertex> verts;
    for (auto &face : faces) {
        for (int i = 0; i < 4; ++i) {
            verts.push_back({face.verts[i], face.n, face.col});
        }
    }
    return verts;
}

// 4 vertices per face → 2 triangles (CCW)
static std::vector<int> makeCubeIndices()
{
    std::vector<int> idx;
    for (int f = 0; f < 6; ++f) {
        int base = f * 4;
        idx.insert(idx.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    }
    return idx;
}

int main(int argc, char *argv[])
{
    const int kWidth = 16 * 60;  // 960
    const int kHeight = 9 * 60;  // 540
    const float kFPS = 60.f;
    const int kSeconds = (argc > 1) ? std::atoi(argv[1]) : 3;
    const int kFrames = kSeconds * static_cast<int>(kFPS);
    const float kAspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);

    // ── Scene parameters ───────────────────────────
    const vec3 kLightDir = normalize(vec3{1.f, 2.f, 3.f});
    const vec3 kAmbient = {0.15f, 0.15f, 0.18f};
    const vec3 kCamPos = {0.f, 0.f, 4.5f};

    mat4 proj = perspective(0.8f, kAspect, 0.1f, 100.f);
    mat4 view = lookAt(kCamPos, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});

    auto cubeVerts = makeCubeVertices();
    auto cubeIndices = makeCubeIndices();

    // ── Build pipeline ─────────────────────────────
    Pipeline<CubeVertex, CubeVarying> pipe;
    pipe.cullBackFace = true;
    pipe.depthTestEnabled = true;

    Framebuffer fb(kWidth, kHeight);

    for (int frame = 0; frame < kFrames; ++frame) {
        float time = static_cast<float>(frame) / kFPS;

        // Model matrix: rotate around Y axis with a slight X-axis tilt
        mat4 model = rotate(time * 1.1f, {0.f, 1.f, 0.f}) * rotate(time * 0.4f, {1.f, 0.f, 0.f});
        mat4 mvp = proj * view * model;
        // Normal matrix (model contains only rotation, so inverse-transpose = itself)
        mat4 normalMat = model;

        // ── Vertex shader ────────────────────────────
        pipe.setVertexShader([&](const CubeVertex &v) -> CubeVarying {
            vec4 worldPos4 = model * vec4{v.pos.x, v.pos.y, v.pos.z, 1.f};
            vec4 n4 = normalMat * vec4{v.normal.x, v.normal.y, v.normal.z, 0.f};
            return {mvp * vec4{v.pos.x, v.pos.y, v.pos.z, 1.f}, worldPos4.xyz(),
                    normalize(n4.xyz()), v.color};
        });

        // ── Fragment shader: Blinn-Phong ─────────────
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

        fb.writePPM(stdout);
    }
    return 0;
}
