// rasterizer/pipeline.h — Templated CPU rasterization pipeline
// See docs/en-US/API.md or docs/zh-CN/API.md for usage

#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "framebuffer.h"
#include "math.h"

// ─────────────────────────────────────────────
//  Varying linear interpolation helper
//  Treats Varying as a float array for barycentric interpolation.
//  Requires Varying to be standard-layout with only float members.
// ─────────────────────────────────────────────
namespace detail {

// Interpolate any Varying with float members by byte offset.
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

// Perspective-correct interpolation: interpolate varying/w then divide by interpolated 1/w.
// Skips the first 4 floats (clipPos); caller handles them separately.
template <typename V>
V perspectiveCorrectLerp(const V &a, const V &b, const V &c, float wa, float wb, float wc, float ba,
                         float bb, float bc)
{
    // Per-vertex 1/w
    float iwa = 1.f / wa, iwb = 1.f / wb, iwc = 1.f / wc;

    // Interpolated 1/w
    float iw = iwa * ba + iwb * bb + iwc * bc;
    if (std::fabs(iw) < 1e-7f) iw = 1e-7f;

    // For each float member (skip first 4: clipPos, overwritten by caller)
    constexpr size_t N = sizeof(V) / sizeof(float);
    V result{};
    const float *fa = reinterpret_cast<const float *>(&a);
    const float *fb = reinterpret_cast<const float *>(&b);
    const float *fc = reinterpret_cast<const float *>(&c);
    float *fr = reinterpret_cast<float *>(&result);
    for (size_t i = 4; i < N; ++i) {
        float val = fa[i] * iwa * ba + fb[i] * iwb * bb + fc[i] * iwc * bc;
        fr[i] = val / iw;
    }
    return result;
}

// Edge function (signed area × 2)
inline float edgeFunc(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

// Sutherland-Hodgman single-plane clipping.
// Plane equation: a*x + b*y + c*z + d*w >= 0 is inside.
// Requires V to be standard-layout with vec4 clipPos as the first field.
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
            // Intersection: linearly interpolate all float fields
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
//  detail::ThreadPool — Fixed-size thread pool.
//  Worker threads spin in an internal loop waiting for tasks, woken via condition variable.
//  runAndWait() submits a batch of tasks and blocks until all are complete.
//  Note: do not call draw() on the same Pipeline from multiple threads simultaneously.
// ─────────────────────────────────────────────
namespace detail {

class ThreadPool
{
public:
    // Create n worker threads (caller/main thread also participates: n+1 total concurrent)
    explicit ThreadPool(unsigned int n) : stop_(false), generation_(0), doneCount_(0)
    {
        workers_.reserve(n);
        for (unsigned int i = 0; i < n; ++i) workers_.emplace_back([this] { workerLoop(); });
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lk(mu_);
            stop_ = true;
            ++generation_;
        }
        startCv_.notify_all();
        for (auto &w : workers_) w.join();
    }

    // Distribute numItems work items [0, numItems) to worker threads and the main thread.
    // fn(i) is called concurrently by the main thread and all workers; returns when all items are done.
    void runAndWait(int numItems, const std::function<void(int)> &fn)
    {
        if (numItems <= 0) return;
        {
            std::unique_lock<std::mutex> lk(mu_);
            nextItem_.store(0, std::memory_order_relaxed);
            totalItems_ = numItems;
            batchFn_ = &fn;
            doneCount_ = 0;
            ++generation_;
        }
        startCv_.notify_all();  // wake all workers
        drainItems();           // main thread also consumes tasks instead of spinning
        std::unique_lock<std::mutex> lk(mu_);
        doneCv_.wait(lk, [this] { return doneCount_ == (int) workers_.size(); });
    }

    unsigned int size() const
    {
        return (unsigned int) workers_.size();
    }

private:
    std::vector<std::thread> workers_;
    std::mutex mu_;
    std::condition_variable startCv_;  // workers wait for a new batch
    std::condition_variable doneCv_;   // main thread waits for batch completion
    bool stop_;
    int generation_;
    int doneCount_;

    // Atomic work counter: threads claim tasks lock-free via fetch_add
    std::atomic<int> nextItem_{0};
    int totalItems_{0};
    const std::function<void(int)> *batchFn_{nullptr};

    // Shared by main and worker threads: atomically claim tasks until exhausted
    void drainItems()
    {
        int id;
        while ((id = nextItem_.fetch_add(1, std::memory_order_relaxed)) < totalItems_)
            (*batchFn_)(id);
    }

    void workerLoop()
    {
        int lastGen = 0;
        while (true) {
            {
                std::unique_lock<std::mutex> lk(mu_);
                startCv_.wait(lk, [&] { return generation_ != lastGen || stop_; });
                if (stop_) return;
                lastGen = generation_;
            }
            drainItems();
            {
                std::unique_lock<std::mutex> lk(mu_);
                if (++doneCount_ == (int) workers_.size()) doneCv_.notify_one();
            }
        }
    }
};

}  // namespace detail

// ─────────────────────────────────────────────
//  TriangleData: post-vertex-shader triangle data (used by the rasterization stage)
//
//  Incremental field notes:
//    edgeFunc(ax,ay,bx,by,px,py) = (bx-ax)*(py-ay) - (by-ay)*(px-ax)
//    ∂/∂px = -(by-ay)   ∂/∂py = (bx-ax)  → per-row/per-column update is a single add
//    ndcZ = invArea*(nz0*e0+nz1*e1+nz2*e2) → also linearly steppable
// ─────────────────────────────────────────────
template <typename Varying>
struct TriangleData {
    Varying v0, v1, v2;
    // screen-space coordinates
    float sx0, sy0;
    float sx1, sy1;
    float sx2, sy2;
    // NDC z (per vertex)
    float nz0, nz1, nz2;
    // clip-space w (for perspective-correct interpolation)
    float w0, w1, w2;
    float invArea;
    // bounding box
    int minX, maxX, minY, maxY;

    // ── precomputed incremental fields (used in the rasterization inner loop) ──
    // edge function step per pixel in x and y
    float dE0dx, dE1dx, dE2dx;
    float dE0dy, dE1dy, dE2dy;
    // edge function values at (minX+0.5, minY+0.5)
    float e0Start, e1Start, e2Start;
    // ndcZ step and start value (ndcZ = invArea*(nz0*e0+nz1*e1+nz2*e2), linear)
    float dNdcZdx, dNdcZdy, ndcZStart;
    // inside test direction: when invArea<0, inside ⟺ all ei≤0
    bool insideNeg;
    // wireframe: screen-space length of each edge (for distance-to-edge calculation)
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

    // Back-face culling toggle (default: on; culls clockwise faces)
    bool cullBackFace = true;
    // Depth test toggle (default: on)
    bool depthTestEnabled = true;
    // Depth write toggle (default: on; disable for transparent passes: test but no write)
    bool depthWriteEnabled = true;
    // Wireframe rendering mode (default: off; draws edge pixels only)
    bool wireframe = false;
    // Wireframe line width in pixels (default: 1)
    float wireframeWidth = 1.0f;
    // Alpha blending: src_over composite dst = src·α + dst·(1-α) (default: off)
    // Note: correct transparent rendering requires back-to-front sorting; the pipeline does not sort automatically.
    bool blendEnabled = false;
    // Number of parallel threads (0 or 1 = single-threaded, >1 = row-parallel)
    // Defaults to all hardware threads to fully utilise the CPU.
    unsigned int threadCount = std::max(1u, std::thread::hardware_concurrency());

    void setVertexShader(VertexShader vs)
    {
        vertexShader = std::move(vs);
    }
    void setFragmentShader(FragmentShader fs)
    {
        fragmentShader = std::move(fs);
    }

    // Draw a triangle list.
    // vertices: vertex array
    // indices:  index array; every 3 indices form one triangle
    // fb:       target framebuffer
    void draw(const std::vector<Vertex> &vertices, const std::vector<int> &indices,
              Framebuffer &fb) const
    {
        if (!vertexShader || !fragmentShader) return;

        // 1. Vertex shading (single-threaded to avoid lambda-capture races)
        std::vector<Varying> varyings(vertices.size());
        for (size_t i = 0; i < vertices.size(); ++i) varyings[i] = vertexShader(vertices[i]);

        // 2. Geometry processing: clip / perspective divide / viewport transform / back-face cull → collect valid triangles
        using Tri = TriangleData<Varying>;
        std::vector<Tri> tris;
        tris.reserve(indices.size() / 3);

        float hw = static_cast<float>(fb.width) * 0.5f;
        float hh = static_cast<float>(fb.height) * 0.5f;

        size_t triCount = indices.size() / 3;
        for (size_t t = 0; t < triCount; ++t) {
            // Sutherland-Hodgman homogeneous-space polygon clipping (6 planes)
            static constexpr float kPlanes[6][4] = {
                {1, 0, 0, 1},   // left:  x + w >= 0
                {-1, 0, 0, 1},  // right: -x + w >= 0
                {0, 1, 0, 1},   // bottom: y + w >= 0
                {0, -1, 0, 1},  // top:   -y + w >= 0
                {0, 0, 1, 1},   // near:   z + w >= 0
                {0, 0, -1, 1},  // far:   -z + w >= 0
            };
            std::vector<Varying> poly = {varyings[indices[t * 3 + 0]], varyings[indices[t * 3 + 1]],
                                         varyings[indices[t * 3 + 2]]};
            for (int p = 0; p < 6 && poly.size() >= 3; ++p)
                poly = detail::clipPolygonAgainstPlane(poly, kPlanes[p][0], kPlanes[p][1],
                                                       kPlanes[p][2], kPlanes[p][3]);
            if (poly.size() < 3) continue;

            // Fan triangulation: (poly[0], poly[f], poly[f+1])
            for (size_t f = 1; f + 1 < poly.size(); ++f) {
                const Varying &va = poly[0];
                const Varying &vb = poly[f];
                const Varying &vc = poly[f + 1];

                vec4 c0 = getClipPos(va);
                vec4 c1 = getClipPos(vb);
                vec4 c2 = getClipPos(vc);

                // Guard against near-zero w after clipping
                if (c0.w < 1e-7f || c1.w < 1e-7f || c2.w < 1e-7f) continue;

                // Perspective divide → NDC
                float iw0 = 1.f / c0.w, iw1 = 1.f / c1.w, iw2 = 1.f / c2.w;
                float nx0 = c0.x * iw0, ny0 = c0.y * iw0, nz0 = c0.z * iw0;
                float nx1 = c1.x * iw1, ny1 = c1.y * iw1, nz1 = c1.z * iw1;
                float nx2 = c2.x * iw2, ny2 = c2.y * iw2, nz2 = c2.z * iw2;

                // Viewport transform
                float sx0 = (nx0 + 1.f) * hw;
                float sy0 = (1.f - ny0) * hh;
                float sx1 = (nx1 + 1.f) * hw;
                float sy1 = (1.f - ny1) * hh;
                float sx2 = (nx2 + 1.f) * hw;
                float sy2 = (1.f - ny2) * hh;

                // Back-face culling
                float area = detail::edgeFunc(sx0, sy0, sx1, sy1, sx2, sy2);
                if (cullBackFace && area >= 0.f) continue;
                if (std::fabs(area) < 1e-5f) continue;

                // AABB clipped to framebuffer bounds
                int minX = std::max(0, static_cast<int>(std::floor(std::min({sx0, sx1, sx2}))));
                int maxX =
                    std::min(fb.width - 1, static_cast<int>(std::ceil(std::max({sx0, sx1, sx2}))));
                int minY = std::max(0, static_cast<int>(std::floor(std::min({sy0, sy1, sy2}))));
                int maxY =
                    std::min(fb.height - 1, static_cast<int>(std::ceil(std::max({sy0, sy1, sy2}))));
                if (minX > maxX || minY > maxY) continue;

                tris.push_back(Tri{va,  vb,  vc,   sx0,  sy0,  sx1,        sy1,  sx2,  sy2,  nz0,
                                   nz1, nz2, c0.w, c1.w, c2.w, 1.f / area, minX, maxX, minY, maxY});

                // Precompute incremental fields
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

                    // Wireframe: precompute screen-space edge lengths
                    tr.edgeLen01 = std::sqrt((sx1 - sx0) * (sx1 - sx0) + (sy1 - sy0) * (sy1 - sy0));
                    tr.edgeLen12 = std::sqrt((sx2 - sx1) * (sx2 - sx1) + (sy2 - sy1) * (sy2 - sy1));
                    tr.edgeLen20 = std::sqrt((sx0 - sx2) * (sx0 - sx2) + (sy0 - sy2) * (sy0 - sy2));
                }
            }  // fan loop
        }  // triangle loop

        if (tris.empty()) return;

        // 3. Rasterization: fine-grained tile tasks; main thread participates alongside nWorkers worker threads.
        //    Atomic counter for task claiming gives automatic load balancing (works for homogeneous and P/E-core architectures).
        unsigned int nTotal = std::max(1u, threadCount);  // total concurrency including main thread
        unsigned int nWorkers = nTotal - 1;               // thread pool worker count

        if (nWorkers == 0) {
            rasterizeRows(tris, fb, 0, fb.height - 1);
        } else {
            // Lazy init: rebuild thread pool on first draw() or when threadCount changes
            if (!pool_ || pool_->size() != nWorkers) pool_.reset(new detail::ThreadPool(nWorkers));

            // Fine-grained tasks: nTotal×16 tasks to reduce tail latency; effective for any multi-core topology.
            // Example: 16 threads × 1080 rows → rowsPerTask=4, numTasks=270
            int rowsPerTask = std::max(1, fb.height / (static_cast<int>(nTotal) * 16));
            int numTasks = (fb.height + rowsPerTask - 1) / rowsPerTask;

            const auto taskFn = [&](int tid) {
                int startRow = tid * rowsPerTask;
                int endRow = std::min(fb.height - 1, startRow + rowsPerTask - 1);
                rasterizeRows(tris, fb, startRow, endRow);
            };

            pool_->runAndWait(numTasks, taskFn);
        }
    }

private:
    VertexShader vertexShader;
    FragmentShader fragmentShader;

    // Thread pool: lazily initialised, rebuilt when threadCount changes
    mutable std::unique_ptr<detail::ThreadPool> pool_;

    // Read clipPos via member pointer (Varying must have vec4 clipPos as its first field)
    static const vec4 &getClipPos(const Varying &v)
    {
        return *reinterpret_cast<const vec4 *>(&v);
    }
    static vec4 &getClipPos(Varying &v)
    {
        return *reinterpret_cast<vec4 *>(&v);
    }

    // Rasterize row range [rowStart, rowEnd] (inclusive).
    // Thread row ranges are non-overlapping, so reads/writes to fb are race-free.
    // Inner loop uses incremental stepping: 3 additions per pixel instead of 3 edgeFunc calls.
    using Tri = TriangleData<Varying>;
    void rasterizeRows(const std::vector<Tri> &tris, Framebuffer &fb, int rowStart,
                       int rowEnd) const
    {
        for (const Tri &tri : tris) {
            // Intersect this thread's row range with the triangle's bounding box
            int minY = std::max(tri.minY, rowStart);
            int maxY = std::min(tri.maxY, rowEnd);
            if (minY > maxY) continue;

            // Step edge values from the bounding-box origin to this thread's first row
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
                    // Inside test (no multiply)
                    if (tri.insideNeg ? (e0 > 0.f || e1 > 0.f || e2 > 0.f)
                                      : (e0 < 0.f || e1 < 0.f || e2 < 0.f))
                        continue;

                    // Wireframe: skip pixels farther than wireframeWidth from any edge
                    if (wireframe) {
                        float d0 = std::fabs(e0) / (tri.edgeLen12 + 1e-6f);
                        float d1 = std::fabs(e1) / (tri.edgeLen20 + 1e-6f);
                        float d2 = std::fabs(e2) / (tri.edgeLen01 + 1e-6f);
                        if (std::min({d0, d1, d2}) > wireframeWidth) continue;
                    }

                    // Early depth test: skip occluded pixels before computing varyings
                    float depth = ndcZ * 0.5f + 0.5f;
                    if (depthTestEnabled && !fb.rawDepthTest(px, py, depth, depthWriteEnabled))
                        continue;

                    // Barycentric coordinates
                    float ba = e0 * tri.invArea;
                    float bb = e1 * tri.invArea;
                    float bc = e2 * tri.invArea;

                    // Perspective-correct varying interpolation (clipPos overwritten below)
                    Varying frag = detail::perspectiveCorrectLerp(tri.v0, tri.v1, tri.v2, tri.w0,
                                                                  tri.w1, tri.w2, ba, bb, bc);
                    getClipPos(frag) = {getClipPos(tri.v0).x * ba + getClipPos(tri.v1).x * bb +
                                            getClipPos(tri.v2).x * bc,
                                        getClipPos(tri.v0).y * ba + getClipPos(tri.v1).y * bb +
                                            getClipPos(tri.v2).y * bc,
                                        ndcZ, tri.w0 * ba + tri.w1 * bb + tri.w2 * bc};

                    // Alpha blending (src_over)
                    vec4 color = fragmentShader(frag);
                    if (blendEnabled) {
                        vec4 dst = fb.rawGetPixel(px, py);
                        float alpha = std::clamp(color.w, 0.f, 1.f);
                        float inv = 1.f - alpha;
                        color = {color.x * alpha + dst.x * inv, color.y * alpha + dst.y * inv,
                                 color.z * alpha + dst.z * inv, alpha + dst.w * inv};
                    }
                    fb.rawSetPixel(px, py, color);
                }
            }
        }
    }
};
