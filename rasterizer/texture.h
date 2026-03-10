// rasterizer/texture.h — 2D texture with bilinear sampling
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

#include "math.h"

class Texture2D
{
public:
    int width = 0, height = 0;

    Texture2D() = default;

    // Create from RGBA unsigned char array (4 bytes per pixel, row-major)
    static Texture2D fromRGBA8(int w, int h, const unsigned char *data)
    {
        Texture2D tex;
        tex.width = w;
        tex.height = h;
        tex.pixels.resize(static_cast<size_t>(w * h));
        for (int i = 0; i < w * h; ++i) {
            tex.pixels[i] = {data[i * 4 + 0] / 255.f, data[i * 4 + 1] / 255.f,
                             data[i * 4 + 2] / 255.f, data[i * 4 + 3] / 255.f};
        }
        return tex;
    }

    // Create from RGB unsigned char array (3 bytes per pixel)
    static Texture2D fromRGB8(int w, int h, const unsigned char *data)
    {
        Texture2D tex;
        tex.width = w;
        tex.height = h;
        tex.pixels.resize(static_cast<size_t>(w * h));
        for (int i = 0; i < w * h; ++i) {
            tex.pixels[i] = {data[i * 3 + 0] / 255.f, data[i * 3 + 1] / 255.f,
                             data[i * 3 + 2] / 255.f, 1.f};
        }
        return tex;
    }

    // Create from vec4 float array (row-major)
    static Texture2D fromVec4(int w, int h, const vec4 *data)
    {
        Texture2D tex;
        tex.width = w;
        tex.height = h;
        tex.pixels.assign(data, data + w * h);
        return tex;
    }

    // Bilinear sampling; UV in [0,1]; boundary uses repeat wrap
    vec4 sample(vec2 uv) const
    {
        if (pixels.empty()) return {1.f, 0.f, 1.f, 1.f};  // error color: magenta
        return bilinear(pixels, width, height, uv);
    }

    // Generate full mipmap chain (call after creation, before first sample).
    // Each level is a 2×2 average downsample of the previous, down to 1×1.
    void generateMipmaps()
    {
        mips_.clear();
        mipW_.clear();
        mipH_.clear();

        const std::vector<vec4> *src = &pixels;
        int sw = width, sh = height;

        while (sw > 1 || sh > 1) {
            int dw = std::max(1, sw / 2);
            int dh = std::max(1, sh / 2);
            std::vector<vec4> level(static_cast<size_t>(dw * dh));
            for (int y = 0; y < dh; ++y) {
                for (int x = 0; x < dw; ++x) {
                    // Average of four neighbors (border clamped for odd dimensions)
                    int x1 = std::min(x * 2, sw - 1);
                    int x2 = std::min(x * 2 + 1, sw - 1);
                    int y1 = std::min(y * 2, sh - 1);
                    int y2 = std::min(y * 2 + 1, sh - 1);
                    vec4 c = (*src)[y1 * sw + x1] * 0.25f + (*src)[y1 * sw + x2] * 0.25f +
                             (*src)[y2 * sw + x1] * 0.25f + (*src)[y2 * sw + x2] * 0.25f;
                    level[y * dw + x] = c;
                }
            }
            mips_.push_back(std::move(level));
            mipW_.push_back(dw);
            mipH_.push_back(dh);
            src = &mips_.back();
            sw = dw;
            sh = dh;
        }
    }

    // Trilinear sampling; lod is a floating-point mip level (requires generateMipmaps; falls back to sample() if mipmaps absent)
    vec4 sampleLod(vec2 uv, float lod) const
    {
        if (pixels.empty()) return {1.f, 0.f, 1.f, 1.f};
        if (mips_.empty() || lod <= 0.f) return sample(uv);

        float flod = std::min(lod, static_cast<float>(mips_.size()));
        int level0 = static_cast<int>(std::floor(flod));
        int level1 = level0 + 1;
        float t = flod - static_cast<float>(level0);

        // sample level0
        vec4 c0;
        if (level0 == 0) {
            c0 = bilinear(pixels, width, height, uv);
        } else {
            int l = level0 - 1;
            c0 = bilinear(mips_[l], mipW_[l], mipH_[l], uv);
        }
        if (t < 1e-4f) return c0;

        // sample level1 (trilinear blend)
        vec4 c1;
        int l1 = level1 - 1;
        if (l1 < static_cast<int>(mips_.size())) {
            c1 = bilinear(mips_[l1], mipW_[l1], mipH_[l1], uv);
        } else {
            // beyond max level: clamp to smallest mip
            int last = static_cast<int>(mips_.size()) - 1;
            c1 = bilinear(mips_[last], mipW_[last], mipH_[last], uv);
        }
        return mix(c0, c1, t);
    }

private:
    std::vector<vec4> pixels;
    // mipmap chain: mips_[0] is level 1 (width/2 × height/2), and so on
    std::vector<std::vector<vec4>> mips_;
    std::vector<int> mipW_, mipH_;

    // Bilinear sampling helper (works on any mip level)
    static vec4 bilinear(const std::vector<vec4> &buf, int w, int h, vec2 uv)
    {
        float u = uv.x - std::floor(uv.x);
        float v = uv.y - std::floor(uv.y);

        float fx = u * static_cast<float>(w) - 0.5f;
        float fy = v * static_cast<float>(h) - 0.5f;

        int x0 = static_cast<int>(std::floor(fx));
        int y0 = static_cast<int>(std::floor(fy));
        float tx = fx - static_cast<float>(x0);
        float ty = fy - static_cast<float>(y0);

        int x1 = (x0 + 1) % w;
        int y1 = (y0 + 1) % h;
        x0 = ((x0 % w) + w) % w;
        y0 = ((y0 % h) + h) % h;

        vec4 c00 = buf[y0 * w + x0];
        vec4 c10 = buf[y0 * w + x1];
        vec4 c01 = buf[y1 * w + x0];
        vec4 c11 = buf[y1 * w + x1];

        return mix(mix(c00, c10, tx), mix(c01, c11, tx), ty);
    }
};
