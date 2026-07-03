#include "source_audio_io.h"

#include "audio_reader.h"
#include "wav_io.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

std::expected<void, std::string> write_trimmed_wav(const std::string& src_path,
                       const std::string& out_path,
                       size_t begin_frame,
                       size_t end_frame) {
    if (end_frame <= begin_frame) {
        return std::unexpected("end_frame <= begin_frame");
    }

    auto reader = AudioReader::open(src_path);
    if (!reader) {
        return std::unexpected("could not open source '" + src_path + "'");
    }
    const AudioFileInfo& src_info = reader->info();
    if (static_cast<int64_t>(end_frame) > src_info.frames) {
        return std::unexpected("end_frame exceeds source length");
    }
    auto seeked = reader->seek_to_frame(static_cast<int64_t>(begin_frame));
    if (!seeked) return std::unexpected(seeked.error());

    auto writer = WavWriter::open_file(out_path, WavSampleFormat::Float32,
                                       src_info.channels,
                                       src_info.sample_rate);
    if (!writer) {
        return std::unexpected("could not create output '" + out_path +
                               "': " + writer.error());
    }

    const size_t kChunk = 65536;
    std::vector<float> buf(kChunk * static_cast<size_t>(src_info.channels));
    size_t remaining = end_frame - begin_frame;
    while (remaining > 0) {
        const int64_t want = static_cast<int64_t>(std::min(kChunk, remaining));
        auto read = read_frames_exact(*reader, buf.data(), want);
        if (!read) return std::unexpected(read.error());
        auto wrote = writer->write_frames(buf.data(), want);
        if (!wrote) {
            return std::unexpected("write failed for '" + out_path + "': " +
                                   wrote.error());
        }
        remaining -= static_cast<size_t>(want);
    }

    auto closed = writer->close();
    if (!closed) {
        return std::unexpected("close failed for '" + out_path + "': " +
                               closed.error());
    }
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
    auto reader = AudioReader::open(src_path);
    if (!reader) {
        return std::unexpected("could not open '" + src_path + "'");
    }
    const AudioFileInfo& src_info = reader->info();
    if (static_cast<int64_t>(end_frame) > src_info.frames) {
        return std::unexpected("end_frame " + std::to_string(end_frame)
                               + " exceeds source length "
                               + std::to_string(src_info.frames));
    }
    auto seeked = reader->seek_to_frame(static_cast<int64_t>(begin_frame));
    if (!seeked) return std::unexpected(seeked.error());

    out_sample_rate = src_info.sample_rate;
    out_channels    = src_info.channels;
    const size_t n_frames = end_frame - begin_frame;
    auto sample_count =
        checked_audio_sample_count(static_cast<int64_t>(n_frames), out_channels);
    if (!sample_count) return std::unexpected(sample_count.error());
    out_samples.assign(*sample_count, 0.0f);

    auto read = read_frames_exact(*reader, out_samples.data(),
                                  static_cast<int64_t>(n_frames));
    if (!read) return std::unexpected(read.error());
    return {};
}
