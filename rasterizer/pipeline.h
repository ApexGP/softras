// rasterizer/pipeline.h — 模板化 CPU 光栅化渲染管线
//
// 用法示例：
//   struct MyVertex  { vec3 pos; vec2 uv; };
//   struct MyVarying { vec4 clipPos; vec2 uv; };   // clipPos 必须存在
//
//   Pipeline<MyVertex, MyVarying> pipe;
//   pipe.setVertexShader([](const MyVertex& v) -> MyVarying {
//       return { mvp * vec4(v.pos, 1.f), v.uv };
//   });
//   pipe.setFragmentShader([](const MyVarying& f) -> vec4 {
//       return { f.uv.x, f.uv.y, 0.f, 1.f };
//   });
//   pipe.draw(vertices, indices, framebuffer);
//
// 约定：
//   - Varying 必须含 `vec4 clipPos` 字段（clip space 位置）
//   - indices 以 3 个一组描述三角形，逆时针为正面
//   - 深度测试使用 NDC z 值（[-1,1] 映射到 [0,1]）
//   - threadCount > 1 时按行范围并行，各线程负责不重叠的行 → 无锁无 data race

#pragma once
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "framebuffer.h"
#include "math.h"

// ─────────────────────────────────────────────
//  Varying 线性插值辅助
//  通过将 Varying 视为 float 数组进行重心坐标插值
//  要求 Varying 是 standard-layout 且只含 float 成员
// ─────────────────────────────────────────────
namespace detail {

// 对任意含 float 成员的 Varying 按字节偏移插值
// N = sizeof(Varying) / sizeof(float)
template <typename V>
V lerpVarying(const V &a, const V &b, const V &c, float ba, float bb, float bc)
{
    constexpr size_t N = sizeof(V) / sizeof(float);
    V result{};
    const float *fa = reinterpret_cast<const float *>(&a);
    const float *fb = reinterpret_cast<const float *>(&b);
    const float *fc = reinterpret_cast<const float *>(&c);
    float *fr = reinterpret_cast<float *>(&result);
    for (size_t i = 0; i < N; ++i) fr[i] = fa[i] * ba + fb[i] * bb + fc[i] * bc;
    return result;
}

// 透视正确插值：将 varying/w 插值后再除以 1/w
template <typename V>
V perspectiveCorrectLerp(const V &a, const V &b, const V &c, float wa, float wb, float wc, float ba,
                         float bb, float bc)
{
    // 各顶点的 1/w
    float iwa = 1.f / wa, iwb = 1.f / wb, iwc = 1.f / wc;

    // 插值后的 1/w
    float iw = iwa * ba + iwb * bb + iwc * bc;
    if (std::fabs(iw) < 1e-7f) iw = 1e-7f;

    // 对每个 float 成员：值/w 的加权平均，再乘以 w_interp
    constexpr size_t N = sizeof(V) / sizeof(float);
    V result{};
    const float *fa = reinterpret_cast<const float *>(&a);
    const float *fb = reinterpret_cast<const float *>(&b);
    const float *fc = reinterpret_cast<const float *>(&c);
    float *fr = reinterpret_cast<float *>(&result);
    for (size_t i = 0; i < N; ++i) {
        float val = fa[i] * iwa * ba + fb[i] * iwb * bb + fc[i] * iwc * bc;
        fr[i] = val / iw;
    }
    return result;
}

// edge function（有符号面积 × 2）
inline float edgeFunc(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

// Sutherland-Hodgman 单平面裁剪
// 平面方程：a*x + b*y + c*z + d*w >= 0 为内侧
// 要求 V 是 standard-layout 且第一字段为 vec4 clipPos
template <typename V>
std::vector<V> clipPolygonAgainstPlane(const std::vector<V> &poly, float a, float b, float c,
                                       float d)
{
    std::vector<V> out;
    out.reserve(poly.size() + 1);
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const V &curr = poly[i];
        const V &next = poly[(i + 1) % n];
        const vec4 &cc = *reinterpret_cast<const vec4 *>(&curr);
        const vec4 &cn = *reinterpret_cast<const vec4 *>(&next);
        float dc = a * cc.x + b * cc.y + c * cc.z + d * cc.w;
        float dn = a * cn.x + b * cn.y + c * cn.z + d * cn.w;
        if (dc >= 0.f) out.push_back(curr);
        if ((dc >= 0.f) != (dn >= 0.f)) {
            // 交点：线性插值所有 float 字段
            float t = dc / (dc - dn);
            constexpr size_t N = sizeof(V) / sizeof(float);
            V interp{};
            const float *fc = reinterpret_cast<const float *>(&curr);
            const float *fn = reinterpret_cast<const float *>(&next);
            float *fi = reinterpret_cast<float *>(&interp);
            for (size_t k = 0; k < N; ++k) fi[k] = fc[k] + t * (fn[k] - fc[k]);
            out.push_back(interp);
        }
    }
    return out;
}

}  // namespace detail

// ─────────────────────────────────────────────
//  detail::ThreadPool — 固定大小线程池
//  工作线程在内部循环等待任务，通过条件变量唤醒
//  runAndWait() 提交一批任务并阻塞直到全部完成
//  注意：同一 Pipeline 对象不应从多个线程同时调用 draw()
// ─────────────────────────────────────────────
namespace detail {

class ThreadPool
{
public:
    explicit ThreadPool(unsigned int n) : pending_(0), stop_(false)
    {
        workers_.reserve(n);
        for (unsigned int i = 0; i < n; ++i) workers_.emplace_back([this] { workerLoop(); });
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lk(mu_);
            stop_ = true;
        }
        workCv_.notify_all();
        for (auto &w : workers_) w.join();
    }

    // 提交一批任务并阻塞直到全部完成
    void runAndWait(std::vector<std::function<void()>> tasks)
    {
        if (tasks.empty()) return;
        {
            std::unique_lock<std::mutex> lk(mu_);
            pending_ = tasks.size();
            for (auto &t : tasks) queue_.push_back(std::move(t));
        }
        workCv_.notify_all();
        std::unique_lock<std::mutex> lk(mu_);
        doneCv_.wait(lk, [this] { return pending_ == 0; });
    }

    unsigned int size() const
    {
        return static_cast<unsigned int>(workers_.size());
    }

private:
    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> queue_;
    std::mutex mu_;
    std::condition_variable workCv_;  // 唤醒工作线程
    std::condition_variable doneCv_;  // 通知主线程完成
    size_t pending_;
    bool stop_;

    void workerLoop()
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(mu_);
                workCv_.wait(lk, [this] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) return;
                task = std::move(queue_.front());
                queue_.pop_front();
            }
            task();
            {
                std::unique_lock<std::mutex> lk(mu_);
                if (--pending_ == 0) doneCv_.notify_one();
            }
        }
    }
};

}  // namespace detail

// ─────────────────────────────────────────────
//  TriangleData：顶点着色后的三角形数据（供光栅化阶段使用）
//
//  增量字段说明：
//    edgeFunc(ax,ay,bx,by,px,py) = (bx-ax)*(py-ay) - (by-ay)*(px-ax)
//    ∂/∂px = -(by-ay)   ∂/∂py = (bx-ax)  → 每行/每列只需做加法
//    ndcZ = invArea*(nz0*e0+nz1*e1+nz2*e2) → 同样线性可步进
// ─────────────────────────────────────────────
template <typename Varying>
struct TriangleData {
    Varying v0, v1, v2;
    // 屏幕坐标
    float sx0, sy0;
    float sx1, sy1;
    float sx2, sy2;
    // NDC z（顶点）
    float nz0, nz1, nz2;
    // clip space w（透视正确插值用）
    float w0, w1, w2;
    float invArea;
    // 包围盒
    int minX, maxX, minY, maxY;

    // ── 预计算增量字段（光栅化内层循环用）──
    // edge function 在 px/py 方向的步长
    float dE0dx, dE1dx, dE2dx;
    float dE0dy, dE1dy, dE2dy;
    // 在 (minX+0.5, minY+0.5) 处的 edge 初始值
    float e0Start, e1Start, e2Start;
    // ndcZ 步长及起点值（ndcZ = invArea*(nz0*e0+nz1*e1+nz2*e2)，线性）
    float dNdcZdx, dNdcZdy, ndcZStart;
    // inside 测试方向：invArea<0 时 inside ⟺ 所有 ei≤0
    bool insideNeg;
    // 线框模式：各边屏幕空间长度（用于边距离计算）
    float edgeLen01, edgeLen12, edgeLen20;
};

// ─────────────────────────────────────────────
//  Pipeline<Vertex, Varying>
// ─────────────────────────────────────────────
template <typename Vertex, typename Varying>
class Pipeline
{
public:
    using VertexShader = std::function<Varying(const Vertex &)>;
    using FragmentShader = std::function<vec4(const Varying &)>;

    // 背面剔除开关（默认开启，剔除顺时针面）
    bool cullBackFace = true;
    // 深度测试开关（默认开启）
    bool depthTestEnabled = true;
    // 深度写入开关（默认开启；关闭时仅测试不写入，适合半透明渲染）
    bool depthWriteEnabled = true;
    // 线框渲染模式（默认关闭；开启时仅绘制边缘像素）
    bool wireframe = false;
    // 线框宽度（单位：像素，默认 1）
    float wireframeWidth = 1.0f;
    // Alpha Blending：src_over 合成 dst = src·α + dst·(1-α)（默认关闭）
    // 注意：正确的半透明渲染需要从后往前排序，管线不自动排序
    bool blendEnabled = false;
    // 并行线程数（0 或 1 为单线程，>1 按行分块并行）
    // 默认取硬件核心数的一半，避免占满 CPU
    unsigned int threadCount = std::max(1u, std::thread::hardware_concurrency() / 2);

    void setVertexShader(VertexShader vs)
    {
        vertexShader = std::move(vs);
    }
    void setFragmentShader(FragmentShader fs)
    {
        fragmentShader = std::move(fs);
    }

    // 绘制三角形列表
    // vertices: 顶点数组
    // indices:  索引数组，每 3 个构成一个三角形
    // fb:       目标帧缓冲
    void draw(const std::vector<Vertex> &vertices, const std::vector<int> &indices,
              Framebuffer &fb) const
    {
        if (!vertexShader || !fragmentShader) return;

        // 1. 顶点着色（单线程，避免 lambda capture 竞争）
        std::vector<Varying> varyings(vertices.size());
        for (size_t i = 0; i < vertices.size(); ++i) varyings[i] = vertexShader(vertices[i]);

        // 2. 几何处理：裁剪 / 透视除法 / 视口变换 / 背面剔除 → 收集有效三角形
        using Tri = TriangleData<Varying>;
        std::vector<Tri> tris;
        tris.reserve(indices.size() / 3);

        float hw = static_cast<float>(fb.width) * 0.5f;
        float hh = static_cast<float>(fb.height) * 0.5f;

        size_t triCount = indices.size() / 3;
        for (size_t t = 0; t < triCount; ++t) {
            // Sutherland-Hodgman 齐次空间多边形裁剪（6个裁剪平面）
            static constexpr float kPlanes[6][4] = {
                {1, 0, 0, 1},   // 左：x + w >= 0
                {-1, 0, 0, 1},  // 右：-x + w >= 0
                {0, 1, 0, 1},   // 下：y + w >= 0
                {0, -1, 0, 1},  // 上：-y + w >= 0
                {0, 0, 1, 1},   // 近：z + w >= 0
                {0, 0, -1, 1},  // 远：-z + w >= 0
            };
            std::vector<Varying> poly = {varyings[indices[t * 3 + 0]], varyings[indices[t * 3 + 1]],
                                         varyings[indices[t * 3 + 2]]};
            for (int p = 0; p < 6 && poly.size() >= 3; ++p)
                poly = detail::clipPolygonAgainstPlane(poly, kPlanes[p][0], kPlanes[p][1],
                                                       kPlanes[p][2], kPlanes[p][3]);
            if (poly.size() < 3) continue;

            // Fan 三角化：(poly[0], poly[f], poly[f+1])
            for (size_t f = 1; f + 1 < poly.size(); ++f) {
                const Varying &va = poly[0];
                const Varying &vb = poly[f];
                const Varying &vc = poly[f + 1];

                vec4 c0 = getClipPos(va);
                vec4 c1 = getClipPos(vb);
                vec4 c2 = getClipPos(vc);

                // 裁剪后 w 应 > 0，防御极小值
                if (c0.w < 1e-7f || c1.w < 1e-7f || c2.w < 1e-7f) continue;

                // 透视除法 → NDC
                float iw0 = 1.f / c0.w, iw1 = 1.f / c1.w, iw2 = 1.f / c2.w;
                float nx0 = c0.x * iw0, ny0 = c0.y * iw0, nz0 = c0.z * iw0;
                float nx1 = c1.x * iw1, ny1 = c1.y * iw1, nz1 = c1.z * iw1;
                float nx2 = c2.x * iw2, ny2 = c2.y * iw2, nz2 = c2.z * iw2;

                // 视口变换
                float sx0 = (nx0 + 1.f) * hw;
                float sy0 = (1.f - ny0) * hh;
                float sx1 = (nx1 + 1.f) * hw;
                float sy1 = (1.f - ny1) * hh;
                float sx2 = (nx2 + 1.f) * hw;
                float sy2 = (1.f - ny2) * hh;

                // 背面剔除
                float area = detail::edgeFunc(sx0, sy0, sx1, sy1, sx2, sy2);
                if (cullBackFace && area >= 0.f) continue;
                if (std::fabs(area) < 1e-5f) continue;

                // 包围盒（裁剪到帧缓冲范围）
                int minX = std::max(0, static_cast<int>(std::floor(std::min({sx0, sx1, sx2}))));
                int maxX =
                    std::min(fb.width - 1, static_cast<int>(std::ceil(std::max({sx0, sx1, sx2}))));
                int minY = std::max(0, static_cast<int>(std::floor(std::min({sy0, sy1, sy2}))));
                int maxY =
                    std::min(fb.height - 1, static_cast<int>(std::ceil(std::max({sy0, sy1, sy2}))));
                if (minX > maxX || minY > maxY) continue;

                tris.push_back(Tri{va,  vb,  vc,   sx0,  sy0,  sx1,        sy1,  sx2,  sy2,  nz0,
                                   nz1, nz2, c0.w, c1.w, c2.w, 1.f / area, minX, maxX, minY, maxY});

                // 预计算增量字段
                {
                    Tri &tr = tris.back();
                    tr.dE0dx = -(sy2 - sy1);
                    tr.dE0dy = sx2 - sx1;  // edge v1→v2
                    tr.dE1dx = -(sy0 - sy2);
                    tr.dE1dy = sx0 - sx2;  // edge v2→v0
                    tr.dE2dx = -(sy1 - sy0);
                    tr.dE2dy = sx1 - sx0;  // edge v0→v1

                    float fpx0 = static_cast<float>(minX) + 0.5f;
                    float fpy0 = static_cast<float>(minY) + 0.5f;
                    tr.e0Start = detail::edgeFunc(sx1, sy1, sx2, sy2, fpx0, fpy0);
                    tr.e1Start = detail::edgeFunc(sx2, sy2, sx0, sy0, fpx0, fpy0);
                    tr.e2Start = detail::edgeFunc(sx0, sy0, sx1, sy1, fpx0, fpy0);

                    float ia = tr.invArea;
                    tr.dNdcZdx = ia * (nz0 * tr.dE0dx + nz1 * tr.dE1dx + nz2 * tr.dE2dx);
                    tr.dNdcZdy = ia * (nz0 * tr.dE0dy + nz1 * tr.dE1dy + nz2 * tr.dE2dy);
                    tr.ndcZStart = ia * (nz0 * tr.e0Start + nz1 * tr.e1Start + nz2 * tr.e2Start);
                    tr.insideNeg = (tr.invArea < 0.f);

                    // 线框模式：预计算各边屏幕空间长度
                    tr.edgeLen01 = std::sqrt((sx1 - sx0) * (sx1 - sx0) + (sy1 - sy0) * (sy1 - sy0));
                    tr.edgeLen12 = std::sqrt((sx2 - sx1) * (sx2 - sx1) + (sy2 - sy1) * (sy2 - sy1));
                    tr.edgeLen20 = std::sqrt((sx0 - sx2) * (sx0 - sx2) + (sy0 - sy2) * (sy0 - sy2));
                }
            }  // fan loop
        }  // triangle loop

        if (tris.empty()) return;

        // 3. 光栅化：按行范围拆分给线程池，行范围不重叠 → 无锁
        unsigned int nThreads = std::max(1u, threadCount);
        if (nThreads == 1) {
            rasterizeRows(tris, fb, 0, fb.height - 1);
        } else {
            // 懒惰初始化：首次 draw() 或 threadCount 变化时重建线程池
            if (!pool_ || pool_->size() != nThreads) pool_.reset(new detail::ThreadPool(nThreads));

            std::vector<std::function<void()>> tasks;
            tasks.reserve(nThreads);
            int rowsPerThread = fb.height / static_cast<int>(nThreads);
            int startRow = 0;
            for (unsigned int tid = 0; tid < nThreads; ++tid) {
                int endRow = (tid == nThreads - 1) ? fb.height - 1 : startRow + rowsPerThread - 1;
                tasks.emplace_back([this, &tris, &fb, startRow, endRow]() {
                    rasterizeRows(tris, fb, startRow, endRow);
                });
                startRow = endRow + 1;
            }
            pool_->runAndWait(std::move(tasks));
        }
    }

private:
    VertexShader vertexShader;
    FragmentShader fragmentShader;

    // 线程池：懒惰初始化，threadCount 变化时自动重建
    mutable std::unique_ptr<detail::ThreadPool> pool_;

    // 通过成员指针读取 clipPos（要求 Varying 第一个字段为 vec4 clipPos）
    static const vec4 &getClipPos(const Varying &v)
    {
        return *reinterpret_cast<const vec4 *>(&v);
    }
    static vec4 &getClipPos(Varying &v)
    {
        return *reinterpret_cast<vec4 *>(&v);
    }

    // 光栅化指定行范围 [rowStart, rowEnd]（闭区间）
    // 各线程行范围不重叠，故对 fb 的读写无竞争
    // 内层循环使用增量步进：每像素 3 次加法替代 3 次 edgeFunc 计算
    using Tri = TriangleData<Varying>;
    void rasterizeRows(const std::vector<Tri> &tris, Framebuffer &fb, int rowStart,
                       int rowEnd) const
    {
        for (const Tri &tri : tris) {
            // 当前线程负责的行与三角形包围盒的交集
            int minY = std::max(tri.minY, rowStart);
            int maxY = std::min(tri.maxY, rowEnd);
            if (minY > maxY) continue;

            // 从包围盒起点步进到当前线程首行
            int rowOff = minY - tri.minY;
            float rowOff_f = static_cast<float>(rowOff);
            float e0_row = tri.e0Start + tri.dE0dy * rowOff_f;
            float e1_row = tri.e1Start + tri.dE1dy * rowOff_f;
            float e2_row = tri.e2Start + tri.dE2dy * rowOff_f;
            float ndcZ_row = tri.ndcZStart + tri.dNdcZdy * rowOff_f;

            for (int py = minY; py <= maxY; ++py, e0_row += tri.dE0dy, e1_row += tri.dE1dy,
                     e2_row += tri.dE2dy, ndcZ_row += tri.dNdcZdy) {
                float e0 = e0_row, e1 = e1_row, e2 = e2_row, ndcZ = ndcZ_row;

                for (int px = tri.minX; px <= tri.maxX;
                     ++px, e0 += tri.dE0dx, e1 += tri.dE1dx, e2 += tri.dE2dx, ndcZ += tri.dNdcZdx) {
                    // inside 测试（无乘法）
                    if (tri.insideNeg ? (e0 > 0.f || e1 > 0.f || e2 > 0.f)
                                      : (e0 < 0.f || e1 < 0.f || e2 < 0.f))
                        continue;

                    // 线框模式：仅绘制距各边不超过 wireframeWidth 像素的像素
                    if (wireframe) {
                        float d0 = std::fabs(e0) / (tri.edgeLen12 + 1e-6f);
                        float d1 = std::fabs(e1) / (tri.edgeLen20 + 1e-6f);
                        float d2 = std::fabs(e2) / (tri.edgeLen01 + 1e-6f);
                        if (std::min({d0, d1, d2}) > wireframeWidth) continue;
                    }

                    // 深度先行，跳过被遮挡像素的 varying 计算
                    float depth = ndcZ * 0.5f + 0.5f;
                    if (depthTestEnabled && !fb.depthTest(px, py, depth, depthWriteEnabled))
                        continue;

                    // 重心坐标
                    float ba = e0 * tri.invArea;
                    float bb = e1 * tri.invArea;
                    float bc = e2 * tri.invArea;

                    // 透视正确 Varying 插值
                    Varying frag = detail::perspectiveCorrectLerp(tri.v0, tri.v1, tri.v2, tri.w0,
                                                                  tri.w1, tri.w2, ba, bb, bc);
                    getClipPos(frag) = {getClipPos(tri.v0).x * ba + getClipPos(tri.v1).x * bb +
                                            getClipPos(tri.v2).x * bc,
                                        getClipPos(tri.v0).y * ba + getClipPos(tri.v1).y * bb +
                                            getClipPos(tri.v2).y * bc,
                                        ndcZ, tri.w0 * ba + tri.w1 * bb + tri.w2 * bc};

                    // Alpha Blending（src_over）
                    vec4 color = fragmentShader(frag);
                    if (blendEnabled) {
                        vec4 dst = fb.getPixel(px, py);
                        float alpha = std::clamp(color.w, 0.f, 1.f);
                        float inv = 1.f - alpha;
                        color = {color.x * alpha + dst.x * inv, color.y * alpha + dst.y * inv,
                                 color.z * alpha + dst.z * inv, alpha + dst.w * inv};
                    }
                    fb.setPixel(px, py, color);
                }
            }
        }
    }
};
