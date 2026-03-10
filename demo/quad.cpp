// demo/quad.cpp — Full-screen quad demo
// Recreates the original shader effect (nonlinear trajectory iteration + lens distortion) using the new API.
//
// Rendering approach:
//   Two triangles fill the screen. The vertex shader outputs NDC coordinates directly.
//   The fragment shader receives a centred UV and runs the procedural shading algorithm.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../rasterizer/framebuffer.h"
#include "../rasterizer/math.h"
#include "../rasterizer/pipeline.h"

// ── Vertex format ─────────────────────────────
struct QuadVertex {
    vec2 ndc;  // already in NDC; passed directly to clipPos
    vec2 uv;   // normalised UV [0,1]
};

// ── Varying format (first field must be clipPos) ──
struct QuadVarying {
    vec4 clipPos;  // must be first!
    vec2 uv;
};

// ── Fragment shading algorithm ────
static vec4 shaderEffect(vec2 uv_centered, float time)
{
    const float kMaxIteration = 8.f;

    vec2 lensFactor = vec2(0.f);
    vec2 iterState = vec2(0.f);
    float lensVal = 4.f - 4.f * std::fabs(0.7f - dot(uv_centered, uv_centered));
    lensFactor.x = lensVal;
    lensFactor.y = lensVal;
    vec2 velocity = uv_centered * lensFactor;

    vec4 colorAccum = vec4(0.f);
    while (iterState.y < kMaxIteration) {
        iterState.y += 1.f;
        vec2 step = swizzle_yx(velocity) * iterState.y + iterState + time;
        velocity = velocity + cos(step) / iterState.y + 0.7f;
        colorAccum =
            colorAccum + (sin(swizzle_xyyx(velocity)) + 1.f) * std::fabs(velocity.x - velocity.y);
    }

    vec4 exponent = vec4(lensFactor.x - 4.f) - vec4{-1.f, 1.f, 2.f, 0.f} * uv_centered.y;
    vec4 denom = colorAccum + vec4(1e-3f);
    vec4 color = tanh(vec4(5.f) * exp(exponent) / denom);
    color.w = 1.f;
    return color;
}

int main(int argc, char *argv[])
{
    const int kWidth = 16 * 120;  // 1920
    const int kHeight = 9 * 120;  // 1080
    const float kFPS = 60.f;
    const int kSeconds = (argc > 1) ? std::atoi(argv[1]) : 3;
    const int kFrames = kSeconds * static_cast<int>(kFPS);
    const float kAspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);

    // ── Full-screen quad vertices (two triangles, CCW) ────────
    // NDC: (-1,-1) bottom-left, (1,1) top-right
    // UV:  (0,0)   top-left,    (1,1) bottom-right (PPM coords: y points down)
    std::vector<QuadVertex> verts = {
        {{-1.f, -1.f}, {0.f, 1.f}},
        {{1.f, -1.f}, {1.f, 1.f}},
        {{1.f, 1.f}, {1.f, 0.f}},
        {{-1.f, 1.f}, {0.f, 0.f}},
    };
    // Two triangles, CCW winding (counter-clockwise = front face)
    std::vector<int> indices = {0, 1, 2, 0, 2, 3};

    // ── Build pipeline ───────────────────────────────
    Pipeline<QuadVertex, QuadVarying> pipe;
    pipe.depthTestEnabled = false;  // 2D effect, no depth test needed
    pipe.cullBackFace = false;      // full-screen quad, both faces visible

    pipe.setVertexShader([](const QuadVertex &v) -> QuadVarying {
        // NDC passed directly as clip pos, w=1
        return {vec4{v.ndc.x, v.ndc.y, 0.f, 1.f}, v.uv};
    });

    Framebuffer fb(kWidth, kHeight);

    for (int frame = 0; frame < kFrames; ++frame) {
        float time = static_cast<float>(frame) / kFPS;

        // Fragment shader captures time and resolution
        pipe.setFragmentShader([&](const QuadVarying &f) -> vec4 {
            // Convert UV [0,1] to centred coordinates
            vec2 uv = f.uv;
            float cx = (uv.x * 2.f - 1.f) * kAspect;
            float cy = -(uv.y * 2.f - 1.f);  // PPM y points down, flip
            return shaderEffect({cx, cy}, time);
        });

        fb.clear();
        pipe.draw(verts, indices, fb);

        fb.writePPM(stdout);
        std::fprintf(stderr, "quad frame %d/%d\r", frame + 1, kFrames);
        std::fflush(stderr);
    }
    std::fprintf(stderr, "\n");
    return 0;
}
