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

        // repeat wrap
        float u = uv.x - std::floor(uv.x);
        float v = uv.y - std::floor(uv.y);

        // 映射到像素坐标（texel 中心在 0.5）
        float fx = u * static_cast<float>(width) - 0.5f;
        float fy = v * static_cast<float>(height) - 0.5f;

        int x0 = static_cast<int>(std::floor(fx));
        int y0 = static_cast<int>(std::floor(fy));
        float tx = fx - static_cast<float>(x0);
        float ty = fy - static_cast<float>(y0);

        // 四个邻居（repeat wrap）
        int x1 = (x0 + 1) % width;
        int y1 = (y0 + 1) % height;
        x0 = ((x0 % width) + width) % width;
        y0 = ((y0 % height) + height) % height;

        vec4 c00 = pixels[y0 * width + x0];
        vec4 c10 = pixels[y0 * width + x1];
        vec4 c01 = pixels[y1 * width + x0];
        vec4 c11 = pixels[y1 * width + x1];

        // 双线性插值
        return mix(mix(c00, c10, tx), mix(c01, c11, tx), ty);
    }

private:
    std::vector<vec4> pixels;
};
