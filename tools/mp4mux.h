// tools/mp4mux.h — Minimal ISO Base Media File Format (MP4) muxer
//
// Supports a single H.264 video track at constant frame rate.
// Strategy: buffer all encoded samples in RAM, then write
//   ftyp + mdat + moov  in one pass on finalize().
//
// Limitations (acceptable for this project):
//   - Total file size must fit in 32-bit offset (< ~4 GB)
//   - Baseline / no-B-frame profile assumed (PTS == DTS, no ctts needed)
//   - No audio track

#pragma once

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ─── Low-level big-endian write helpers ──────────────────────────────────────
namespace mp4detail {

static inline void w8(std::vector<uint8_t> &b, uint8_t v)
{
    b.push_back(v);
}

static inline void w16(std::vector<uint8_t> &b, uint16_t v)
{
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v & 0xff));
}

static inline void w32(std::vector<uint8_t> &b, uint32_t v)
{
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    b.push_back(static_cast<uint8_t>(v & 0xff));
}

static inline void wfourcc(std::vector<uint8_t> &b, const char *cc)
{
    b.push_back(static_cast<uint8_t>(cc[0]));
    b.push_back(static_cast<uint8_t>(cc[1]));
    b.push_back(static_cast<uint8_t>(cc[2]));
    b.push_back(static_cast<uint8_t>(cc[3]));
}

static inline void wbytes(std::vector<uint8_t> &b, const uint8_t *data, size_t len)
{
    b.insert(b.end(), data, data + len);
}

static inline void wzeros(std::vector<uint8_t> &b, size_t count)
{
    b.insert(b.end(), count, 0);
}

// Start a box: reserve 4-byte size + write fourcc. Returns position of size field.
static inline size_t box_begin(std::vector<uint8_t> &b, const char *fourcc)
{
    size_t pos = b.size();
    w32(b, 0);  // size placeholder
    wfourcc(b, fourcc);
    return pos;
}

// Patch the box size field at pos (must fit in uint32_t).
static inline void box_end(std::vector<uint8_t> &b, size_t pos)
{
    uint32_t size = static_cast<uint32_t>(b.size() - pos);
    b[pos] = static_cast<uint8_t>((size >> 24) & 0xff);
    b[pos + 1] = static_cast<uint8_t>((size >> 16) & 0xff);
    b[pos + 2] = static_cast<uint8_t>((size >> 8) & 0xff);
    b[pos + 3] = static_cast<uint8_t>(size & 0xff);
}

// FullBox header: version (1 byte) + flags (3 bytes).
static inline void fullbox(std::vector<uint8_t> &b, uint8_t version = 0, uint32_t flags = 0)
{
    w8(b, version);
    b.push_back(static_cast<uint8_t>((flags >> 16) & 0xff));
    b.push_back(static_cast<uint8_t>((flags >> 8) & 0xff));
    b.push_back(static_cast<uint8_t>(flags & 0xff));
}

}  // namespace mp4detail

// ─── Mp4Muxer ────────────────────────────────────────────────────────────────
class Mp4Muxer
{
public:
    // fps: integer frames-per-second (timescale = fps, each sample duration = 1)
    Mp4Muxer(const std::string &path, int width, int height, int fps)
        : path_(path), width_(width), height_(height), fps_(fps)
    {
    }

    // Supply SPS and PPS raw NAL data (without start code and without the
    // 4-byte AVCC length prefix added by x264 when b_annexb=0).
    // Must be called before the first addSample().
    void setSPSPPS(const uint8_t *sps, int sps_len, const uint8_t *pps, int pps_len)
    {
        sps_.assign(sps, sps + sps_len);
        pps_.assign(pps, pps + pps_len);
    }

    // Append one encoded video sample (one frame).
    //   data        — one or more AVCC NAL units concatenated
    //                 (each NAL = [4-byte BE length][NAL data])
    //   size        — total byte count of data
    //   is_keyframe — true for IDR frames (written into stss)
    void addSample(const uint8_t *data, int size, bool is_keyframe)
    {
        mdat_data_.insert(mdat_data_.end(), data, data + size);
        sample_sizes_.push_back(static_cast<uint32_t>(size));
        if (is_keyframe) {
            // stss uses 1-based sample indices
            keyframe_indices_.push_back(static_cast<uint32_t>(sample_sizes_.size()));
        }
    }

    // Write the complete MP4 file to path_. Returns true on success.
    bool finalize()
    {
        if (sps_.empty() || pps_.empty()) {
            std::fprintf(stderr, "mp4mux: SPS/PPS not set\n");
            return false;
        }
        if (sample_sizes_.empty()) {
            std::fprintf(stderr, "mp4mux: no samples\n");
            return false;
        }

        FILE *f = std::fopen(path_.c_str(), "wb");
        if (!f) {
            std::perror("mp4mux: fopen");
            return false;
        }

        const uint32_t num_samples = static_cast<uint32_t>(sample_sizes_.size());

        // ── ftyp (24 bytes, fixed) ────────────────────────────────────────────
        std::vector<uint8_t> ftyp;
        ftyp.reserve(24);
        {
            size_t p = mp4detail::box_begin(ftyp, "ftyp");
            mp4detail::wfourcc(ftyp, "isom");  // major brand
            mp4detail::w32(ftyp, 0);           // minor version
            mp4detail::wfourcc(ftyp, "isom");  // compatible brand
            mp4detail::wfourcc(ftyp, "avc1");  // compatible brand
            mp4detail::box_end(ftyp, p);
        }
        assert(ftyp.size() == 24);

        // stco chunk_offset = ftyp(24) + mdat_header(8)
        const uint32_t mdat_data_offset = static_cast<uint32_t>(ftyp.size()) + 8;

        // ── moov (built in memory so all sizes are known) ──────────────────────
        std::vector<uint8_t> moov;
        moov.reserve(4096 + num_samples * 4 * 3);
        build_moov(moov, num_samples, mdat_data_offset);

        // ── Write: ftyp → mdat → moov ─────────────────────────────────────────
        std::fwrite(ftyp.data(), 1, ftyp.size(), f);

        // mdat header (8 bytes: size + "mdat")
        {
            uint32_t mdat_box_size = 8 + static_cast<uint32_t>(mdat_data_.size());
            uint8_t hdr[8];
            hdr[0] = static_cast<uint8_t>((mdat_box_size >> 24) & 0xff);
            hdr[1] = static_cast<uint8_t>((mdat_box_size >> 16) & 0xff);
            hdr[2] = static_cast<uint8_t>((mdat_box_size >> 8) & 0xff);
            hdr[3] = static_cast<uint8_t>(mdat_box_size & 0xff);
            hdr[4] = 'm';
            hdr[5] = 'd';
            hdr[6] = 'a';
            hdr[7] = 't';
            std::fwrite(hdr, 1, 8, f);
        }

        // mdat payload
        std::fwrite(mdat_data_.data(), 1, mdat_data_.size(), f);

        // moov
        std::fwrite(moov.data(), 1, moov.size(), f);

        std::fclose(f);
        return true;
    }

private:
    std::string path_;
    int width_, height_, fps_;

    std::vector<uint8_t> sps_, pps_;
    std::vector<uint8_t> mdat_data_;          // all sample bytes concatenated
    std::vector<uint32_t> sample_sizes_;      // per-sample byte count (for stsz)
    std::vector<uint32_t> keyframe_indices_;  // 1-based indices of IDR frames (for stss)

    // Build the entire moov box into b.
    void build_moov(std::vector<uint8_t> &b, uint32_t num_samples, uint32_t mdat_data_offset) const
    {
        using namespace mp4detail;

        const uint32_t timescale = static_cast<uint32_t>(fps_);
        // Duration in timescale ticks: 1 tick per frame.
        const uint32_t duration = num_samples;

        size_t moov_pos = box_begin(b, "moov");

        // ── mvhd ──────────────────────────────────────────────────────────────
        {
            size_t p = box_begin(b, "mvhd");
            fullbox(b, 0, 0);
            w32(b, 0);           // creation_time
            w32(b, 0);           // modification_time
            w32(b, timescale);   // timescale
            w32(b, duration);    // duration
            w32(b, 0x00010000);  // rate = 1.0 (16.16 fixed)
            w16(b, 0x0100);      // volume = 1.0 (8.8 fixed)
            wzeros(b, 10);       // reserved
            // Unity matrix
            w32(b, 0x00010000);
            w32(b, 0);
            w32(b, 0);
            w32(b, 0);
            w32(b, 0x00010000);
            w32(b, 0);
            w32(b, 0);
            w32(b, 0);
            w32(b, 0x40000000);
            wzeros(b, 24);  // pre_defined[6]
            w32(b, 2);      // next_track_id (1 track used → next = 2)
            box_end(b, p);
        }

        // ── trak ──────────────────────────────────────────────────────────────
        {
            size_t trak_pos = box_begin(b, "trak");

            // tkhd
            {
                size_t p = box_begin(b, "tkhd");
                fullbox(b, 0, 0x000003);  // track_enabled | track_in_movie
                w32(b, 0);                // creation_time
                w32(b, 0);                // modification_time
                w32(b, 1);                // track_id = 1
                w32(b, 0);                // reserved
                w32(b, duration);         // duration (movie timescale)
                wzeros(b, 8);             // reserved[2]
                w16(b, 0);                // layer
                w16(b, 0);                // alternate_group
                w16(b, 0);                // volume (0 for video)
                w16(b, 0);                // reserved
                // Unity matrix
                w32(b, 0x00010000);
                w32(b, 0);
                w32(b, 0);
                w32(b, 0);
                w32(b, 0x00010000);
                w32(b, 0);
                w32(b, 0);
                w32(b, 0);
                w32(b, 0x40000000);
                w32(b, static_cast<uint32_t>(width_) << 16);   // width  (16.16)
                w32(b, static_cast<uint32_t>(height_) << 16);  // height (16.16)
                box_end(b, p);
            }

            // mdia
            {
                size_t mdia_pos = box_begin(b, "mdia");

                // mdhd
                {
                    size_t p = box_begin(b, "mdhd");
                    fullbox(b, 0, 0);
                    w32(b, 0);          // creation_time
                    w32(b, 0);          // modification_time
                    w32(b, timescale);  // timescale
                    w32(b, duration);   // duration
                    w16(b, 0x55c4);     // language = "und" (ISO-639-2 packed)
                    w16(b, 0);          // pre_defined
                    box_end(b, p);
                }

                // hdlr
                {
                    size_t p = box_begin(b, "hdlr");
                    fullbox(b, 0, 0);
                    w32(b, 0);           // pre_defined
                    wfourcc(b, "vide");  // handler_type
                    wzeros(b, 12);       // reserved[3]
                    const char *name = "VideoHandler";
                    wbytes(b, reinterpret_cast<const uint8_t *>(name),
                           std::strlen(name) + 1);  // include null terminator
                    box_end(b, p);
                }

                // minf
                {
                    size_t minf_pos = box_begin(b, "minf");

                    // vmhd (flags = 1 as per spec)
                    {
                        size_t p = box_begin(b, "vmhd");
                        fullbox(b, 0, 0x000001);
                        w16(b, 0);     // graphicsMode
                        wzeros(b, 6);  // opcolor[3]
                        box_end(b, p);
                    }

                    // dinf → dref → url
                    {
                        size_t dinf_pos = box_begin(b, "dinf");
                        {
                            size_t dref_pos = box_begin(b, "dref");
                            fullbox(b, 0, 0);
                            w32(b, 1);  // entry_count = 1
                            {
                                // url: self-contained (flags = 0x000001)
                                size_t p = box_begin(b, "url ");
                                fullbox(b, 0, 0x000001);
                                box_end(b, p);
                            }
                            box_end(b, dref_pos);
                        }
                        box_end(b, dinf_pos);
                    }

                    // stbl
                    {
                        size_t stbl_pos = box_begin(b, "stbl");

                        // stsd → avc1 → avcC
                        {
                            size_t stsd_pos = box_begin(b, "stsd");
                            fullbox(b, 0, 0);
                            w32(b, 1);  // entry_count = 1

                            // avc1 (VisualSampleEntry)
                            {
                                size_t avc1_pos = box_begin(b, "avc1");
                                wzeros(b, 6);  // reserved
                                w16(b, 1);     // data_reference_index
                                wzeros(b,
                                       16);  // pre_defined (2) + reserved (2) + pre_defined[3] (12)
                                w16(b, static_cast<uint16_t>(width_));
                                w16(b, static_cast<uint16_t>(height_));
                                w32(b, 0x00480000);  // horiz_resolution = 72 dpi
                                w32(b, 0x00480000);  // vert_resolution = 72 dpi
                                w32(b, 0);           // reserved
                                w16(b, 1);           // frame_count
                                wzeros(b, 32);       // compressor_name (Pascal string, 32 bytes)
                                w16(b, 0x0018);      // depth = 24
                                w16(b, 0xffff);      // pre_defined = -1

                                // avcC (AVCDecoderConfigurationRecord)
                                {
                                    size_t avcc_pos = box_begin(b, "avcC");
                                    w8(b, 1);         // configurationVersion
                                    w8(b, sps_[1]);   // AVCProfileIndication
                                    w8(b, sps_[2]);   // profile_compatibility
                                    w8(b, sps_[3]);   // AVCLevelIndication
                                    w8(b, 0xff);      // lengthSizeMinusOne = 3 → 4-byte lengths
                                    w8(b, 0xe0 | 1);  // numSPS = 1
                                    w16(b, static_cast<uint16_t>(sps_.size()));
                                    wbytes(b, sps_.data(), sps_.size());
                                    w8(b, 1);  // numPPS = 1
                                    w16(b, static_cast<uint16_t>(pps_.size()));
                                    wbytes(b, pps_.data(), pps_.size());
                                    box_end(b, avcc_pos);
                                }

                                box_end(b, avc1_pos);
                            }
                            box_end(b, stsd_pos);
                        }

                        // stts: all frames have delta = 1
                        {
                            size_t p = box_begin(b, "stts");
                            fullbox(b, 0, 0);
                            w32(b, 1);            // entry_count = 1
                            w32(b, num_samples);  // sample_count
                            w32(b, 1);            // sample_delta = 1 tick
                            box_end(b, p);
                        }

                        // stss: keyframe (IDR) sample indices (1-based)
                        if (!keyframe_indices_.empty()) {
                            size_t p = box_begin(b, "stss");
                            fullbox(b, 0, 0);
                            w32(b, static_cast<uint32_t>(keyframe_indices_.size()));
                            for (uint32_t idx : keyframe_indices_) w32(b, idx);
                            box_end(b, p);
                        }

                        // stsc: one chunk containing all samples
                        {
                            size_t p = box_begin(b, "stsc");
                            fullbox(b, 0, 0);
                            w32(b, 1);            // entry_count = 1
                            w32(b, 1);            // first_chunk = 1
                            w32(b, num_samples);  // samples_per_chunk = all
                            w32(b, 1);            // sample_description_index = 1
                            box_end(b, p);
                        }

                        // stsz: per-sample byte counts
                        {
                            size_t p = box_begin(b, "stsz");
                            fullbox(b, 0, 0);
                            w32(b, 0);  // sample_size = 0 (variable)
                            w32(b, num_samples);
                            for (uint32_t sz : sample_sizes_) w32(b, sz);
                            box_end(b, p);
                        }

                        // stco: single chunk at mdat_data_offset
                        {
                            size_t p = box_begin(b, "stco");
                            fullbox(b, 0, 0);
                            w32(b, 1);                 // entry_count = 1
                            w32(b, mdat_data_offset);  // chunk_offset
                            box_end(b, p);
                        }

                        box_end(b, stbl_pos);
                    }

                    box_end(b, minf_pos);
                }

                box_end(b, mdia_pos);
            }

            box_end(b, trak_pos);
        }

        box_end(b, moov_pos);
    }
};
