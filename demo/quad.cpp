// demo/quad.cpp — 全屏四边形 Demo
// 使用新 API 复现原始着色器效果（非线性轨迹迭代 + 透镜扭曲）
//
// 渲染流程：
//   两个三角形铺满屏幕，顶点着色器直接输出 NDC 坐标，
//   片元着色器接收 UV（中心化）并执行原有的过程式着色算法。

#include <cmath>
#include <cstdio>
#include <vector>

#include "../rasterizer/framebuffer.h"
#include "../rasterizer/math.h"
#include "../rasterizer/pipeline.h"

// ── 顶点格式 ──────────────────────────────────
struct QuadVertex {
    vec2 ndc;  // 已经是 NDC 坐标，直接传给 clipPos
    vec2 uv;   // 归一化 UV [0,1]
};

// ── Varying 格式（第一个字段必须是 clipPos）──
struct QuadVarying {
    vec4 clipPos;  // 必须第一个！
    vec2 uv;
};

// ── 片元着色算法────
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

int main()
{
    const int kWidth = 16 * 120;  // 1920
    const int kHeight = 9 * 120;  // 1080
    const int kFrames = 60 * 3;   // 180 帧
    const float kFPS = 60.f;
    const float kAspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);

    // ── 全屏 quad 顶点（两个三角形，CCW）────────
    // NDC: (-1,-1) 左下，(1,1) 右上
    // UV:  (0,0)   左上，(1,1) 右下（PPM 坐标系，y 向下）
    std::vector<QuadVertex> verts = {
        {{-1.f, -1.f}, {0.f, 1.f}},
        {{1.f, -1.f}, {1.f, 1.f}},
        {{1.f, 1.f}, {1.f, 0.f}},
        {{-1.f, 1.f}, {0.f, 0.f}},
    };
    // 两个三角形，CCW 为正面（逆时针）
    std::vector<int> indices = {0, 1, 2, 0, 2, 3};

    // ── 构建管线 ─────────────────────────────────
    Pipeline<QuadVertex, QuadVarying> pipe;
    pipe.depthTestEnabled = false;  // 2D 效果，不需要深度测试
    pipe.cullBackFace = false;      // 全屏 quad，两面都可见

    pipe.setVertexShader([](const QuadVertex &v) -> QuadVarying {
        // NDC 直接作为 clip pos，w=1
        return {vec4{v.ndc.x, v.ndc.y, 0.f, 1.f}, v.uv};
    });

    Framebuffer fb(kWidth, kHeight);
    char buf[256];

    for (int frame = 0; frame < kFrames; ++frame) {
        float time = static_cast<float>(frame) / kFPS;

        // 片元着色器捕获 time 和分辨率
        pipe.setFragmentShader([&](const QuadVarying &f) -> vec4 {
            // 将 UV [0,1] 转换为以中心为原点的坐标
            vec2 uv = f.uv;
            float cx = (uv.x * 2.f - 1.f) * kAspect;
            float cy = -(uv.y * 2.f - 1.f);  // PPM y 向下，翻转
            return shaderEffect({cx, cy}, time);
        });

        fb.clear();
        pipe.draw(verts, indices, fb);

        std::snprintf(buf, sizeof(buf), "assets/quad/frame-%02d.ppm", frame);
        fb.savePPM(buf);
        std::printf("quad frame %d/%d\n", frame + 1, kFrames);
    }

    return 0;
}
