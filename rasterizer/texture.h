// rasterizer/texture.h — 2D 纹理，支持双线性采样
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

    // 从 RGBA unsigned char 数组创建（每像素 4 字节，行主序）
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

    // 从 RGB unsigned char 数组创建（每像素 3 字节）
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

    // 从 vec4 数组创建（float，行主序）
    static Texture2D fromVec4(int w, int h, const vec4 *data)
    {
        Texture2D tex;
        tex.width = w;
        tex.height = h;
        tex.pixels.assign(data, data + w * h);
        return tex;
    }

    // 双线性采样，UV 范围 [0,1]，边界采用 repeat wrap
    vec4 sample(vec2 uv) const
    {
        if (pixels.empty()) return {1.f, 0.f, 1.f, 1.f};  // 错误颜色：洋红
        return bilinear(pixels, width, height, uv);
    }

    // 生成完整 mipmap 链（需在创建纹理后、首次采样前调用）
    // 每级为上一级 2×2 均值下采样，直到 1×1
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
                    // 四邻居均值（处理奇数尺寸时边界重复）
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

    // 三线性采样（需先调用 generateMipmaps）
    // lod=0 等同于 sample()；lod 越大采样越模糊
    // 若未生成 mipmap，则退化为 sample()
    vec4 sampleLod(vec2 uv, float lod) const
    {
        if (pixels.empty()) return {1.f, 0.f, 1.f, 1.f};
        if (mips_.empty() || lod <= 0.f) return sample(uv);

        float flod = std::min(lod, static_cast<float>(mips_.size()));
        int level0 = static_cast<int>(std::floor(flod));
        int level1 = level0 + 1;
        float t = flod - static_cast<float>(level0);

        // level0 采样
        vec4 c0;
        if (level0 == 0) {
            c0 = bilinear(pixels, width, height, uv);
        } else {
            int l = level0 - 1;
            c0 = bilinear(mips_[l], mipW_[l], mipH_[l], uv);
        }
        if (t < 1e-4f) return c0;

        // level1 采样（三线性）
        vec4 c1;
        int l1 = level1 - 1;
        if (l1 < static_cast<int>(mips_.size())) {
            c1 = bilinear(mips_[l1], mipW_[l1], mipH_[l1], uv);
        } else {
            // 超出最大层，取最小 mip
            int last = static_cast<int>(mips_.size()) - 1;
            c1 = bilinear(mips_[last], mipW_[last], mipH_[last], uv);
        }
        return mix(c0, c1, t);
    }

private:
    std::vector<vec4> pixels;
    // mipmap 链：mips_[0] 为第 1 级（width/2 × height/2），依此类推
    std::vector<std::vector<vec4>> mips_;
    std::vector<int> mipW_, mipH_;

    // 双线性采样辅助（对任意层）
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
