// rasterizer/framebuffer.h — Framebuffer (color + depth)
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "math.h"

class Framebuffer
{
public:
    int width, height;

    Framebuffer(int w, int h)
        : width(w),
          height(h),
          colorBuf(static_cast<size_t>(w * h), vec4(0.f)),
          depthBuf(static_cast<size_t>(w * h), 1.f)
    {
    }

    // Clear color and depth buffers
    void clear(vec4 color = vec4(0.f, 0.f, 0.f, 1.f), float depth = 1.f)
    {
        std::fill(colorBuf.begin(), colorBuf.end(), color);
        std::fill(depthBuf.begin(), depthBuf.end(), depth);
    }

    // Depth test: passes if z is less than the current depth value.
    // When write=true (default) the depth buffer is updated on pass; write=false tests only (useful for transparent rendering).
    bool depthTest(int x, int y, float z, bool write = true)
    {
        if (x < 0 || x >= width || y < 0 || y >= height) return false;
        float &cur = depthBuf[index(x, y)];
        if (z < cur) {
            if (write) cur = z;
            return true;
        }
        return false;
    }

    // No-bounds-check variant (internal rasterizer use only; caller must guarantee valid coords)
    inline bool rawDepthTest(int x, int y, float z, bool write = true)
    {
        float &cur = depthBuf[static_cast<size_t>(y * width + x)];
        if (z < cur) {
            if (write) cur = z;
            return true;
        }
        return false;
    }

    void setPixel(int x, int y, vec4 color)
    {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        colorBuf[index(x, y)] = color;
    }

    inline void rawSetPixel(int x, int y, vec4 color)
    {
        colorBuf[static_cast<size_t>(y * width + x)] = color;
    }

    vec4 getPixel(int x, int y) const
    {
        if (x < 0 || x >= width || y < 0 || y >= height) return {};
        return colorBuf[index(x, y)];
    }

    inline const vec4 &rawGetPixel(int x, int y) const
    {
        return colorBuf[static_cast<size_t>(y * width + x)];
    }

    // Save as PPM P6 binary file.
    // Converts the entire frame to an RGB8 byte array in one pass, then writes it with a single fwrite.
    bool savePPM(const std::string &path) const
    {
        FILE *f = std::fopen(path.c_str(), "wb");
        if (!f) {
            std::perror("fopen");
            return false;
        }
        bool ok = writePPM(f);
        std::fclose(f);
        return ok;
    }

    // Write one PPM frame to an already-open file stream (for pipe/streaming output)
    bool writePPM(FILE *f) const
    {
        std::fprintf(f, "P6\n%d %d\n255\n", width, height);

        const size_t npix = static_cast<size_t>(width * height);
        std::vector<unsigned char> buf(npix * 3);
        for (size_t i = 0; i < npix; ++i) {
            const vec4 &c = colorBuf[i];
            buf[i * 3 + 0] = toByte(c.x);
            buf[i * 3 + 1] = toByte(c.y);
            buf[i * 3 + 2] = toByte(c.z);
        }
        size_t written = std::fwrite(buf.data(), 1, buf.size(), f);
        return written == buf.size();
    }

private:
    std::vector<vec4> colorBuf;
    std::vector<float> depthBuf;

    inline int index(int x, int y) const
    {
        return y * width + x;
    }

    static unsigned char toByte(float v)
    {
        v = std::clamp(v, 0.f, 1.f);
        return static_cast<unsigned char>(std::lround(v * 255.f));
    }
};
