#include "source_audio_io.h"

#include <algorithm>
#include <string>
#include <vector>

#include <sndfile.h>

std::expected<void, std::string> write_trimmed_wav(const std::string& src_path,
                       const std::string& out_path,
                       size_t begin_frame,
                       size_t end_frame) {
    if (end_frame <= begin_frame) {
        return std::unexpected("end_frame <= begin_frame");
    }

    SF_INFO src_info{};
    src_info.format = 0;
    SNDFILE* src = sf_open(src_path.c_str(), SFM_READ, &src_info);
    if (!src) {
        return std::unexpected("could not open source '" + src_path + "'");
    }
    if (static_cast<sf_count_t>(end_frame) > src_info.frames) {
        sf_close(src);
        return std::unexpected("end_frame exceeds source length");
    }
    if (sf_seek(src, static_cast<sf_count_t>(begin_frame), SEEK_SET) < 0) {
        sf_close(src);
        return std::unexpected("sf_seek failed");
    }

    SF_INFO out_info = src_info;
    out_info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE* dst = sf_open(out_path.c_str(), SFM_WRITE, &out_info);
    if (!dst) {
        sf_close(src);
        return std::unexpected("could not create output '" + out_path + "'");
    }

    const size_t kChunk = 65536;
    std::vector<float> buf(kChunk * static_cast<size_t>(src_info.channels));
    size_t remaining = end_frame - begin_frame;
    while (remaining > 0) {
        sf_count_t want = static_cast<sf_count_t>(std::min(kChunk, remaining));
        sf_count_t got  = sf_readf_float(src, buf.data(), want);
        if (got <= 0) break;
        if (sf_writef_float(dst, buf.data(), got) != got) {
            sf_close(src);
            sf_close(dst);
            return std::unexpected("short write");
        }
        remaining -= static_cast<size_t>(got);
    }

    sf_close(src);
    sf_close(dst);
    return {};
}

std::expected<void, std::string> load_source_range_to_buffer(const std::string& src_path,
                                 size_t begin_frame,
                                 size_t end_frame,
                                 std::vector<float>& out_samples,
                                 int& out_sample_rate,
                                 int& out_channels) {
    if (end_frame <= begin_frame) {
        return std::unexpected("end_frame <= begin_frame");
    }
    SF_INFO src_info{};
    src_info.format = 0;
    SNDFILE* src = sf_open(src_path.c_str(), SFM_READ, &src_info);
    if (!src) {
        return std::unexpected("could not open '" + src_path + "'");
    }
    if (static_cast<sf_count_t>(end_frame) > src_info.frames) {
        sf_close(src);
        return std::unexpected("end_frame " + std::to_string(end_frame)
                               + " exceeds source length "
                               + std::to_string(src_info.frames));
    }
    if (sf_seek(src, static_cast<sf_count_t>(begin_frame), SEEK_SET) < 0) {
        sf_close(src);
        return std::unexpected("sf_seek failed");
    }

    out_sample_rate = src_info.samplerate;
    out_channels    = src_info.channels;
    const size_t n_frames = end_frame - begin_frame;
    out_samples.assign(n_frames * static_cast<size_t>(src_info.channels), 0.0f);

    const sf_count_t got =
        sf_readf_float(src, out_samples.data(),
                       static_cast<sf_count_t>(n_frames));
    sf_close(src);
    if (got != static_cast<sf_count_t>(n_frames)) {
        return std::unexpected("short read (" + std::to_string(got) + "/"
                               + std::to_string(n_frames) + ")");
    }
    return {};
}
