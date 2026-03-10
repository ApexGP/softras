// demo/showcase.cpp — Feature showcase demo
// Demonstrates: SH clipping / mipmap textured floor / alpha blending / wireframe mode

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../rasterizer/framebuffer.h"
#include "../rasterizer/math.h"
#include "../rasterizer/pipeline.h"
#include "../rasterizer/texture.h"

// ── Shared vertex / varying ────────────────────
struct Vertex {
    vec3 pos;
    vec3 normal;
    vec2 uv;
    vec3 color;
};

struct Varying {
    vec4 clipPos;  // must be first
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
    vec3 color;
};

// ── Cube geometry ──────────────────────────────
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

// ── Checkerboard floor geometry ─────────────────
// A large flat plane with UV range [0, tileCount]; the fragment shader samples the texture.
static std::vector<Vertex> makeFloorVertices(float halfSize, vec3 col)
{
    float h = halfSize;
    float tc =
        halfSize * 0.5f;  // 6 tiles (24 was too dense; collapses to solid grey after mipmapping)
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

// ── Checkerboard texture (procedurally generated) ──
// Uses 8×8-pixel blocks instead of 1×1 alternating:
//   1px alternating collapses to uniform grey after the first mip (2×2 average), losing all pattern.
//   8px blocks survive 3 mip levels and remain sharp at reasonable view distances.
static Texture2D makeCheckerTexture(int res)
{
    const int sq = res / 8;  // block size: 8 cells × 8 cells
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

    // ── Scene parameters ──────────────────────────────────
    const vec3 kLightDir = normalize(vec3{1.f, 2.f, 1.5f});
    const vec3 kAmbient = {0.12f, 0.12f, 0.15f};
    // near-plane 0.4 — camera orbiting close triggers SH clipping on large floor triangles
    mat4 proj = perspective(0.9f, kAspect, 0.4f, 60.f);

    // ── Checkerboard texture (Feature 2: mipmap) ──────────
    Texture2D checker = makeCheckerTexture(64);

    // ── Geometry ──────────────────────────────────
    auto floorVerts = makeFloorVertices(12.f, {1.f, 1.f, 1.f});
    auto floorIdx = makeFloorIndices();

    auto blueCubeVerts = makeCubeVertices({0.3f, 0.5f, 0.9f});
    auto cubeIdx = makeCubeIndices();

    auto orangeCubeVerts = makeCubeVertices({0.95f, 0.55f, 0.15f});
    auto greenCubeVerts = makeCubeVertices({0.25f, 0.85f, 0.35f});
    auto yellowCubeVerts = makeCubeVertices({0.95f, 0.9f, 0.2f});

    // ── Pipeline ──────────────────────────────────
    Pipeline<Vertex, Varying> pipe;

    Framebuffer fb(kWidth, kHeight);

    for (int frame = 0; frame < kFrames; ++frame) {
        float t = static_cast<float>(frame) / kFPS;

        // Camera orbit: radius 7 around origin, height 3, slow rotation
        float camAng = t * 0.4f;
        vec3 camPos = {std::sin(camAng) * 7.f, 3.0f, std::cos(camAng) * 7.f};
        mat4 view = lookAt(camPos, {0.f, 0.5f, 0.f}, {0.f, 1.f, 0.f});

        // Clear framebuffer
        fb.clear(vec4{0.04f, 0.04f, 0.06f, 1.f});

        // ── Common vertex shader factory (accepts model matrix by ref) ──
        auto makeVS = [&](const mat4 &model, const mat4 &mvp) {
            return [model, mvp, &camPos](const Vertex &v) -> Varying {
                vec4 wp = model * vec4{v.pos.x, v.pos.y, v.pos.z, 1.f};
                vec4 n4 = model * vec4{v.normal.x, v.normal.y, v.normal.z, 0.f};
                return {mvp * vec4{v.pos.x, v.pos.y, v.pos.z, 1.f}, wp.xyz(), normalize(n4.xyz()),
                        v.uv, v.color};
            };
        };

        // ── Blinn-Phong fragment shader (no texture) ──────
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
        // Pass 1: Opaque objects (back-face culling, depth write)
        // ────────────────────────────────────────────
        pipe.cullBackFace = true;
        pipe.depthTestEnabled = true;
        pipe.depthWriteEnabled = true;
        pipe.wireframe = false;
        pipe.blendEnabled = false;

        // -- Floor (Feature 1: SH clipping; Feature 2: mipmap LOD) --
        {
            mat4 model = translate(vec3{0.f, -1.f, 0.f});
            mat4 mvp = proj * view * model;
            pipe.setVertexShader(makeVS(model, mvp));
            pipe.setFragmentShader([&](const Varying &f) -> vec4 {
                // LOD based on camera distance; higher mip for distant fragments
                float dist = length(camPos - f.worldPos);
                // LOD factor 0.05: tuned to match 6-tile UV + 8px blocks
                // dist=8 → lod≈0.5 (sharp), dist=30 → lod≈1.3 (starts to blur)
                float lod = std::log2(dist * 0.05f + 1.f);
                vec4 tc = checker.sampleLod(f.uv, lod);
                // Simple diffuse (floor normal is always up)
                float diff = std::max(0.f, dot(vec3{0.f, 1.f, 0.f}, kLightDir));
                vec3 col = tc.xyz() * (kAmbient + diff * vec3{0.75f, 0.75f, 0.72f});
                col = clamp(col, 0.f, 1.f);
                return {col.x, col.y, col.z, 1.f};
            });
            pipe.draw(floorVerts, floorIdx, fb);
        }

        // -- Left stationary blue cube --
        {
            mat4 model = translate(vec3{-2.8f, -0.2f, -1.f}) * scale(vec3{0.8f, 0.8f, 0.8f});
            mat4 mvp = proj * view * model;
            pipe.setVertexShader(makeVS(model, mvp));
            pipe.setFragmentShader(blinnPhongFS);
            pipe.draw(blueCubeVerts, cubeIdx, fb);
        }

        // -- Right rotating orange cube --
        {
            mat4 model = translate(vec3{2.5f, -0.2f, 0.f}) * rotate(t * 1.2f, {0.f, 1.f, 0.f}) *
                         scale(vec3{0.75f, 0.75f, 0.75f});
            mat4 mvp = proj * view * model;
            pipe.setVertexShader(makeVS(model, mvp));
            pipe.setFragmentShader(blinnPhongFS);
            pipe.draw(orangeCubeVerts, cubeIdx, fb);
        }

        // ────────────────────────────────────────────
        // Pass 2: Translucent green cube (Feature 3: alpha blending)
        // Depth write off, double-sided, src-over compositing
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
        // Pass 3: Wireframe cube (Feature 4: wireframe)
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
                return {0.95f, 0.9f, 0.2f, 1.f};  // yellow wireframe
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
