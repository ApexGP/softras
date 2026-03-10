// rasterizer/math.h — GLSL-style vector/matrix math library
#pragma once
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────
//  vec2
// ─────────────────────────────────────────────
struct vec2 {
    float x = 0, y = 0;
    vec2() = default;
    vec2(float x, float y) : x(x), y(y) {}
    explicit vec2(float v) : x(v), y(v) {}
};

inline vec2 operator+(vec2 a, vec2 b)
{
    return {a.x + b.x, a.y + b.y};
}
inline vec2 operator+(vec2 a, float s)
{
    return {a.x + s, a.y + s};
}
inline vec2 operator+(float s, vec2 a)
{
    return a + s;
}
inline vec2 operator-(vec2 a, vec2 b)
{
    return {a.x - b.x, a.y - b.y};
}
inline vec2 operator-(vec2 a, float s)
{
    return {a.x - s, a.y - s};
}
inline vec2 operator-(vec2 a)
{
    return {-a.x, -a.y};
}
inline vec2 operator*(vec2 a, vec2 b)
{
    return {a.x * b.x, a.y * b.y};
}
inline vec2 operator*(vec2 a, float s)
{
    return {a.x * s, a.y * s};
}
inline vec2 operator*(float s, vec2 a)
{
    return a * s;
}
inline vec2 operator/(vec2 a, vec2 b)
{
    return {a.x / b.x, a.y / b.y};
}
inline vec2 operator/(vec2 a, float s)
{
    return {a.x / s, a.y / s};
}
inline vec2 &operator+=(vec2 &a, vec2 b)
{
    a = a + b;
    return a;
}
inline vec2 &operator-=(vec2 &a, vec2 b)
{
    a = a - b;
    return a;
}
inline vec2 &operator*=(vec2 &a, float s)
{
    a = a * s;
    return a;
}
inline vec2 &operator/=(vec2 &a, float s)
{
    a = a / s;
    return a;
}

// ─────────────────────────────────────────────
//  vec3
// ─────────────────────────────────────────────
struct vec3 {
    float x = 0, y = 0, z = 0;
    vec3() = default;
    vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    explicit vec3(float v) : x(v), y(v), z(v) {}
};

inline vec3 operator+(vec3 a, vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline vec3 operator+(vec3 a, float s)
{
    return {a.x + s, a.y + s, a.z + s};
}
inline vec3 operator+(float s, vec3 a)
{
    return a + s;
}
inline vec3 operator-(vec3 a, vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline vec3 operator-(vec3 a, float s)
{
    return {a.x - s, a.y - s, a.z - s};
}
inline vec3 operator-(vec3 a)
{
    return {-a.x, -a.y, -a.z};
}
inline vec3 operator*(vec3 a, vec3 b)
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}
inline vec3 operator*(vec3 a, float s)
{
    return {a.x * s, a.y * s, a.z * s};
}
inline vec3 operator*(float s, vec3 a)
{
    return a * s;
}
inline vec3 operator/(vec3 a, vec3 b)
{
    return {a.x / b.x, a.y / b.y, a.z / b.z};
}
inline vec3 operator/(vec3 a, float s)
{
    return {a.x / s, a.y / s, a.z / s};
}
inline vec3 &operator+=(vec3 &a, vec3 b)
{
    a = a + b;
    return a;
}
inline vec3 &operator-=(vec3 &a, vec3 b)
{
    a = a - b;
    return a;
}
inline vec3 &operator*=(vec3 &a, float s)
{
    a = a * s;
    return a;
}
inline vec3 &operator/=(vec3 &a, float s)
{
    a = a / s;
    return a;
}

// ─────────────────────────────────────────────
//  vec4
// ─────────────────────────────────────────────
struct vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    vec4() = default;
    vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    explicit vec4(float v) : x(v), y(v), z(v), w(v) {}
    vec4(vec3 v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}

    vec3 xyz() const
    {
        return {x, y, z};
    }
};

inline vec4 operator+(vec4 a, vec4 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}
inline vec4 operator+(vec4 a, float s)
{
    return {a.x + s, a.y + s, a.z + s, a.w + s};
}
inline vec4 operator+(float s, vec4 a)
{
    return a + s;
}
inline vec4 operator-(vec4 a, vec4 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}
inline vec4 operator-(vec4 a, float s)
{
    return {a.x - s, a.y - s, a.z - s, a.w - s};
}
inline vec4 operator-(vec4 a)
{
    return {-a.x, -a.y, -a.z, -a.w};
}
inline vec4 operator*(vec4 a, vec4 b)
{
    return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w};
}
inline vec4 operator*(vec4 a, float s)
{
    return {a.x * s, a.y * s, a.z * s, a.w * s};
}
inline vec4 operator*(float s, vec4 a)
{
    return a * s;
}
inline vec4 operator/(vec4 a, vec4 b)
{
    return {a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w};
}
inline vec4 operator/(vec4 a, float s)
{
    return {a.x / s, a.y / s, a.z / s, a.w / s};
}
inline vec4 &operator+=(vec4 &a, vec4 b)
{
    a = a + b;
    return a;
}
inline vec4 &operator-=(vec4 &a, vec4 b)
{
    a = a - b;
    return a;
}
inline vec4 &operator*=(vec4 &a, float s)
{
    a = a * s;
    return a;
}
inline vec4 &operator/=(vec4 &a, float s)
{
    a = a / s;
    return a;
}

// ─────────────────────────────────────────────
//  mat4  (column-major, consistent with OpenGL/GLSL)
// ─────────────────────────────────────────────
struct mat4 {
    float m[4][4] = {};  // m[col][row]

    static mat4 identity()
    {
        mat4 r{};
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.f;
        return r;
    }
};

inline vec4 operator*(const mat4 &M, vec4 v)
{
    return {
        M.m[0][0] * v.x + M.m[1][0] * v.y + M.m[2][0] * v.z + M.m[3][0] * v.w,
        M.m[0][1] * v.x + M.m[1][1] * v.y + M.m[2][1] * v.z + M.m[3][1] * v.w,
        M.m[0][2] * v.x + M.m[1][2] * v.y + M.m[2][2] * v.z + M.m[3][2] * v.w,
        M.m[0][3] * v.x + M.m[1][3] * v.y + M.m[2][3] * v.z + M.m[3][3] * v.w,
    };
}

inline mat4 operator*(const mat4 &A, const mat4 &B)
{
    mat4 R{};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k) R.m[c][r] += A.m[k][r] * B.m[c][k];
    return R;
}

// ─────────────────────────────────────────────
//  Common matrix constructors
// ─────────────────────────────────────────────
inline mat4 translate(vec3 t)
{
    mat4 M = mat4::identity();
    M.m[3][0] = t.x;
    M.m[3][1] = t.y;
    M.m[3][2] = t.z;
    return M;
}

inline mat4 scale(vec3 s)
{
    mat4 M = mat4::identity();
    M.m[0][0] = s.x;
    M.m[1][1] = s.y;
    M.m[2][2] = s.z;
    return M;
}

// Rotation around an arbitrary axis (angle in radians)
inline mat4 rotate(float angle, vec3 axis)
{
    float c = std::cos(angle), s = std::sin(angle);
    float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    axis = {axis.x / len, axis.y / len, axis.z / len};
    float t = 1.f - c;
    float x = axis.x, y = axis.y, z = axis.z;
    mat4 M = mat4::identity();
    M.m[0][0] = t * x * x + c;
    M.m[1][0] = t * x * y - s * z;
    M.m[2][0] = t * x * z + s * y;
    M.m[0][1] = t * x * y + s * z;
    M.m[1][1] = t * y * y + c;
    M.m[2][1] = t * y * z - s * x;
    M.m[0][2] = t * x * z - s * y;
    M.m[1][2] = t * y * z + s * x;
    M.m[2][2] = t * z * z + c;
    return M;
}

// Perspective projection (fovY in radians, aspect = w/h, near/far are positive)
inline mat4 perspective(float fovY, float aspect, float near, float far)
{
    float f = 1.f / std::tan(fovY * 0.5f);
    mat4 M{};
    M.m[0][0] = f / aspect;
    M.m[1][1] = f;
    M.m[2][2] = (far + near) / (near - far);
    M.m[2][3] = -1.f;
    M.m[3][2] = 2.f * far * near / (near - far);
    return M;
}

// View matrix (lookAt)
inline mat4 lookAt(vec3 eye, vec3 center, vec3 up)
{
    auto norm3 = [](vec3 v) -> vec3 {
        float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        return {v.x / l, v.y / l, v.z / l};
    };
    auto cross3 = [](vec3 a, vec3 b) -> vec3 {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    auto dot3 = [](vec3 a, vec3 b) -> float { return a.x * b.x + a.y * b.y + a.z * b.z; };
    vec3 f = norm3({center.x - eye.x, center.y - eye.y, center.z - eye.z});
    vec3 r = norm3(cross3(f, up));
    vec3 u = cross3(r, f);
    mat4 M = mat4::identity();
    M.m[0][0] = r.x;
    M.m[1][0] = r.y;
    M.m[2][0] = r.z;
    M.m[0][1] = u.x;
    M.m[1][1] = u.y;
    M.m[2][1] = u.z;
    M.m[0][2] = -f.x;
    M.m[1][2] = -f.y;
    M.m[2][2] = -f.z;
    M.m[3][0] = -dot3(r, eye);
    M.m[3][1] = -dot3(u, eye);
    M.m[3][2] = dot3(f, eye);
    return M;
}

// ─────────────────────────────────────────────
//  Scalar/vector math functions (GLSL-style)
// ─────────────────────────────────────────────
inline float clamp(float v, float lo, float hi)
{
    return std::clamp(v, lo, hi);
}
inline float mix(float a, float b, float t)
{
    return a + (b - a) * t;
}
inline float fract(float v)
{
    return v - std::floor(v);
}
inline float smoothstep(float e0, float e1, float x)
{
    float t = clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

inline float dot(vec2 a, vec2 b)
{
    return a.x * b.x + a.y * b.y;
}
inline float dot(vec3 a, vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline float dot(vec4 a, vec4 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

inline float length(vec2 v)
{
    return std::sqrt(dot(v, v));
}
inline float length(vec3 v)
{
    return std::sqrt(dot(v, v));
}

inline vec2 normalize(vec2 v)
{
    return v / length(v);
}
inline vec3 normalize(vec3 v)
{
    return v / length(v);
}

inline vec3 cross(vec3 a, vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline vec3 reflect(vec3 I, vec3 N)
{
    return I - 2.f * dot(N, I) * N;
}

inline vec2 abs(vec2 v)
{
    return {std::fabs(v.x), std::fabs(v.y)};
}
inline vec3 abs(vec3 v)
{
    return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)};
}

inline vec2 sin(vec2 v)
{
    return {std::sin(v.x), std::sin(v.y)};
}
inline vec3 sin(vec3 v)
{
    return {std::sin(v.x), std::sin(v.y), std::sin(v.z)};
}
inline vec4 sin(vec4 v)
{
    return {std::sin(v.x), std::sin(v.y), std::sin(v.z), std::sin(v.w)};
}

inline vec2 cos(vec2 v)
{
    return {std::cos(v.x), std::cos(v.y)};
}
inline vec3 cos(vec3 v)
{
    return {std::cos(v.x), std::cos(v.y), std::cos(v.z)};
}

inline vec4 exp(vec4 v)
{
    return {std::exp(v.x), std::exp(v.y), std::exp(v.z), std::exp(v.w)};
}
inline vec4 tanh(vec4 v)
{
    return {std::tanh(v.x), std::tanh(v.y), std::tanh(v.z), std::tanh(v.w)};
}

inline vec3 mix(vec3 a, vec3 b, float t)
{
    return a * (1.f - t) + b * t;
}
inline vec4 mix(vec4 a, vec4 b, float t)
{
    return a * (1.f - t) + b * t;
}
inline vec3 clamp(vec3 v, float lo, float hi)
{
    return {clamp(v.x, lo, hi), clamp(v.y, lo, hi), clamp(v.z, lo, hi)};
}
inline vec4 clamp(vec4 v, float lo, float hi)
{
    return {clamp(v.x, lo, hi), clamp(v.y, lo, hi), clamp(v.z, lo, hi), clamp(v.w, lo, hi)};
}
inline vec3 pow(vec3 v, float p)
{
    return {std::pow(v.x, p), std::pow(v.y, p), std::pow(v.z, p)};
}

// Swizzle helpers (kept for legacy compatibility)
inline vec4 swizzle_xyyx(vec2 v)
{
    return {v.x, v.y, v.y, v.x};
}
inline vec2 swizzle_yx(vec2 v)
{
    return {v.y, v.x};
}
