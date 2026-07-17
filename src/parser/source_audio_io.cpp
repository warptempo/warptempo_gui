#include "source_audio_io.h"

#include "wav_io.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

std::expected<void, std::string> load_source_range_to_buffer(const std::string& src_path,
                                 size_t begin_frame,
                                 size_t end_frame,
                                 std::vector<float>& out_samples,
                                 int& out_sample_rate,
                                 int& out_channels) {
    if (end_frame <= begin_frame) {
        return std::unexpected("end_frame <= begin_frame");
    }
    WavInfo info;
    auto samples = wav_read_range(src_path, static_cast<int64_t>(begin_frame),
                                  static_cast<int64_t>(end_frame), &info);
    if (!samples) return std::unexpected(samples.error());
    out_sample_rate = info.sample_rate;
    out_channels    = info.channels;
    out_samples     = std::move(*samples);
    return {};
}
