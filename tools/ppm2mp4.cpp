// tools/ppm2mp4.cpp — Read PPM frame stream from stdin, encode to MP4 via libx264
//
// Reads a binary PPM (P6) stream from stdin (the same format written by
// Framebuffer::writePPM).  Encodes each frame with libx264 (baseline profile,
// auto thread count) and muxes the result into a self-contained MP4 file.

#include <stdint.h>  // must precede x264.h (x264.h requires C-style uint*_t)
#include <x264.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "mp4mux.h"

// ─── PPM header parser ────────────────────────────────────────────────────────
// Reads one "P6\n<W> <H>\n255\n" header from f.
// Returns false on EOF or parse error.
static bool read_ppm_header(FILE *f, int &w, int &h)
{
    char magic[3] = {};
    if (std::fscanf(f, "%2s", magic) != 1) return false;
    if (std::strcmp(magic, "P6") != 0) return false;
    int maxval = 0;
    if (std::fscanf(f, " %d %d %d", &w, &h, &maxval) != 3) return false;
    // Consume exactly one whitespace byte separating the header from pixel data
    std::fgetc(f);
    return w > 0 && h > 0 && maxval == 255;
}

// ─── RGB → YUV I420 (BT.601 studio swing) ────────────────────────────────────
static inline uint8_t clamp_u8(int v)
{
    return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}

// rgb:      packed RGB24, row-major, top-to-bottom
// y/cb/cr:  separate planar buffers with given strides
static void rgb_to_i420(const uint8_t *rgb, int w, int h, uint8_t *y_plane, uint8_t *cb_plane,
                        uint8_t *cr_plane, int y_stride, int c_stride)
{
    // Luma: one Y sample per pixel
    for (int row = 0; row < h; ++row) {
        const uint8_t *src = rgb + row * w * 3;
        uint8_t *y = y_plane + row * y_stride;
        for (int col = 0; col < w; ++col) {
            int r = src[col * 3 + 0];
            int g = src[col * 3 + 1];
            int b = src[col * 3 + 2];
            y[col] = clamp_u8(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
        }
    }
    // Chroma: 2×2 downsampled — use top-left pixel of each block
    for (int row = 0; row < h / 2; ++row) {
        const uint8_t *src = rgb + (row * 2) * w * 3;
        uint8_t *cb = cb_plane + row * c_stride;
        uint8_t *cr = cr_plane + row * c_stride;
        for (int col = 0; col < w / 2; ++col) {
            int r = src[col * 2 * 3 + 0];
            int g = src[col * 2 * 3 + 1];
            int b = src[col * 2 * 3 + 2];
            cb[col] = clamp_u8(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            cr[col] = clamp_u8(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
    }
}

// ─── Encode helper ────────────────────────────────────────────────────────────
// Encodes one picture (or flushes when pic_in == nullptr).
// Returns true if a sample was produced (bytes > 0).
static bool encode_frame(x264_t *enc, x264_picture_t *pic_in, x264_picture_t *pic_out,
                         Mp4Muxer &muxer)
{
    x264_nal_t *nals = nullptr;
    int nnal = 0;
    int bytes = x264_encoder_encode(enc, &nals, &nnal, pic_in, pic_out);
    if (bytes <= 0 || nnal <= 0) return false;

    // Collect all slice NAL units into one MP4 sample.
    // Filter out parameter-set and filler NALs that should not appear in mdat
    // (b_repeat_headers=0 + b_aud=0 already suppresses most; this is defensive).
    std::vector<uint8_t> sample;
    sample.reserve(static_cast<size_t>(bytes));
    for (int i = 0; i < nnal; ++i) {
        int t = nals[i].i_type;
        if (t == NAL_SPS || t == NAL_PPS || t == NAL_AUD || t == NAL_FILLER) continue;
        const uint8_t *d = nals[i].p_payload;
        int sz = nals[i].i_payload;
        sample.insert(sample.end(), d, d + sz);
    }
    if (sample.empty()) return false;

    muxer.addSample(sample.data(), static_cast<int>(sample.size()), pic_out->b_keyframe != 0);
    return true;
}

// ─── main ────────────────────────────────────────────────────────────────────
static void usage(const char *prog)
{
    std::fprintf(stderr, "Usage: %s --fps <N> -o <output.mp4> [--duration <seconds>]\n", prog);
}

int main(int argc, char *argv[])
{
    int fps = 0;
    int duration = 0;  // seconds; 0 = unknown → no N/total in progress
    std::string output;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fps = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration = std::atoi(argv[++i]);
        }
    }
    if (fps <= 0 || output.empty()) {
        usage(argv[0]);
        return 1;
    }

    // Compute total expected frames from duration × fps (0 if duration not given)
    const int total_frames = (duration > 0) ? duration * fps : 0;

    // ── Read first PPM header to discover frame dimensions ───────────────────
    int width = 0, height = 0;
    if (!read_ppm_header(stdin, width, height)) {
        std::fprintf(stderr, "ppm2mp4: failed to read first PPM header (empty input?)\n");
        return 1;
    }

    // ── x264 encoder setup ───────────────────────────────────────────────────
    x264_param_t param;
    x264_param_default_preset(&param, "medium", nullptr);
    // Baseline profile: disables B-frames → PTS == DTS, no ctts box needed.
    x264_param_apply_profile(&param, "baseline");

    param.i_width = width;
    param.i_height = height;
    param.i_fps_num = static_cast<uint32_t>(fps);
    param.i_fps_den = 1;
    param.i_threads = X264_THREADS_AUTO;  // same as ffmpeg default
    param.i_csp = X264_CSP_I420;
    param.b_annexb = 0;          // AVCC: 4-byte BE length prefix (MP4-compatible)
    param.b_repeat_headers = 0;  // SPS/PPS only in avcC, not inline in bitstream
    param.b_aud = 0;             // suppress Access Unit Delimiter NALs

    x264_t *enc = x264_encoder_open(&param);
    if (!enc) {
        std::fprintf(stderr, "ppm2mp4: x264_encoder_open failed\n");
        return 1;
    }

    // ── Extract SPS / PPS for avcC box ───────────────────────────────────────
    // With b_annexb=0: p_payload = [4-byte BE length][NAL data]
    x264_nal_t *hdr_nals = nullptr;
    int hdr_nals_n = 0;
    x264_encoder_headers(enc, &hdr_nals, &hdr_nals_n);

    const uint8_t *sps_data = nullptr;
    int sps_len = 0;
    const uint8_t *pps_data = nullptr;
    int pps_len = 0;
    for (int i = 0; i < hdr_nals_n; ++i) {
        if (hdr_nals[i].i_type == NAL_SPS) {
            sps_data = hdr_nals[i].p_payload + 4;
            sps_len = hdr_nals[i].i_payload - 4;
        } else if (hdr_nals[i].i_type == NAL_PPS) {
            pps_data = hdr_nals[i].p_payload + 4;
            pps_len = hdr_nals[i].i_payload - 4;
        }
    }
    if (!sps_data || !pps_data || sps_len <= 0 || pps_len <= 0) {
        std::fprintf(stderr, "ppm2mp4: failed to extract SPS/PPS from encoder headers\n");
        x264_encoder_close(enc);
        return 1;
    }

    // ── MP4 muxer ─────────────────────────────────────────────────────────────
    Mp4Muxer muxer(output, width, height, fps);
    muxer.setSPSPPS(sps_data, sps_len, pps_data, pps_len);

    // ── Allocate input picture ────────────────────────────────────────────────
    x264_picture_t pic_in, pic_out;
    if (x264_picture_alloc(&pic_in, X264_CSP_I420, width, height) < 0) {
        std::fprintf(stderr, "ppm2mp4: x264_picture_alloc failed\n");
        x264_encoder_close(enc);
        return 1;
    }
    x264_picture_init(&pic_out);

    // ── Encode loop ───────────────────────────────────────────────────────────
    // Layout: read pixels → encode → try to read next header → repeat
    std::vector<uint8_t> rgb_buf(static_cast<size_t>(width * height * 3));
    int64_t pts = 0;
    int total_out = 0;

    while (true) {
        // Read pixel data for the current frame (header already consumed)
        size_t nread = std::fread(rgb_buf.data(), 1, rgb_buf.size(), stdin);
        if (nread != rgb_buf.size()) break;  // EOF or short read → stop

        // Convert RGB24 → YUV I420
        rgb_to_i420(rgb_buf.data(), width, height, pic_in.img.plane[0], pic_in.img.plane[1],
                    pic_in.img.plane[2], pic_in.img.i_stride[0], pic_in.img.i_stride[1]);
        pic_in.i_pts = pts++;

        if (encode_frame(enc, &pic_in, &pic_out, muxer)) ++total_out;

        // Print progress after feeding each frame to the encoder
        if (total_frames > 0) {
            std::fprintf(stderr, "ppm2mp4: encoding frame %lld/%d    \r",
                         static_cast<long long>(pts), total_frames);
        } else {
            std::fprintf(stderr, "ppm2mp4: encoding frame %lld    \r", static_cast<long long>(pts));
        }
        std::fflush(stderr);

        // Try to read the next PPM header; stop on EOF
        int w2 = 0, h2 = 0;
        if (!read_ppm_header(stdin, w2, h2)) break;
        if (w2 != width || h2 != height) {
            std::fprintf(stderr, "\nppm2mp4: frame size changed (%dx%d → %dx%d), stopping\n", width,
                         height, w2, h2);
            break;
        }
    }

    // ── Flush x264 look-ahead / delayed frames ────────────────────────────────
    while (x264_encoder_delayed_frames(enc) > 0) {
        if (encode_frame(enc, nullptr, &pic_out, muxer)) ++total_out;
    }

    std::fprintf(stderr, "\nppm2mp4: %d frames encoded → %s\n", total_out, output.c_str());

    // ── Cleanup ───────────────────────────────────────────────────────────────
    x264_picture_clean(&pic_in);
    x264_encoder_close(enc);

    if (!muxer.finalize()) {
        std::fprintf(stderr, "ppm2mp4: finalize failed\n");
        return 1;
    }

    return 0;
}
