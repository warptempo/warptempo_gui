#include "wav_io.h"

#include "pcm24.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstring>
#include <limits>
#include <memory>
#include <system_error>
#include <utility>

static_assert(std::endian::native == std::endian::little);

namespace {

struct FileCloser {
    void operator()(FILE* f) const
    {
        if (f) std::fclose(f);
    }
};

using FilePtr = std::unique_ptr<FILE, FileCloser>;

enum class SourceKind { File, Memory };

std::string append_errno_detail(std::string message, int err)
{
    if (err != 0) {
        message += " (";
        message += std::error_code(err, std::generic_category()).message();
        message += ")";
    }
    return message;
}

struct ByteSource {
    SourceKind kind = SourceKind::Memory;
    FILE* file = nullptr;
    std::span<const char> memory;
    uint64_t cursor = 0;

    bool read(void* out, size_t size)
    {
        if (kind == SourceKind::File) {
            return std::fread(out, 1, size, file) == size;
        }
        if (cursor + size > memory.size()) return false;
        std::memcpy(out, memory.data() + cursor, size);
        cursor += size;
        return true;
    }

    bool seek(uint64_t offset)
    {
        if (kind == SourceKind::File) {
            return std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0;
        }
        if (offset > memory.size()) return false;
        cursor = offset;
        return true;
    }

    uint64_t tell() const
    {
        if (kind == SourceKind::File) {
            const long pos = std::ftell(file);
            return pos < 0 ? 0 : static_cast<uint64_t>(pos);
        }
        return cursor;
    }

    uint64_t size()
    {
        if (kind == SourceKind::Memory) return memory.size();
        const long old = std::ftell(file);
        if (old < 0) return 0;
        if (std::fseek(file, 0, SEEK_END) != 0) return 0;
        const long end = std::ftell(file);
        std::fseek(file, old, SEEK_SET);
        return end < 0 ? 0 : static_cast<uint64_t>(end);
    }
};

struct WavLayout {
    WavInfo info;
    uint64_t data_offset = 0;
    uint64_t data_size = 0;
    uint16_t block_align = 0;
};

uint16_t read_u16(const std::array<unsigned char, 40>& b, size_t off)
{
    return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}

uint32_t read_u32(const unsigned char* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t read_u32(const std::array<unsigned char, 40>& b, size_t off)
{
    return read_u32(b.data() + off);
}

bool fourcc_eq(const char id[4], const char* lit)
{
    return std::memcmp(id, lit, 4) == 0;
}

bool guid_is_subformat(const unsigned char* p, uint32_t tag)
{
    const unsigned char tail[12] = {
        0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
        0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
    };
    return read_u32(p) == tag && std::memcmp(p + 4, tail, sizeof(tail)) == 0;
}

std::expected<WavLayout, std::string> parse_wav_layout(ByteSource& src)
{
    char riff[4];
    unsigned char u32buf[4];
    char wave[4];
    if (!src.read(riff, 4) || !src.read(u32buf, 4) || !src.read(wave, 4)) {
        return std::unexpected("short WAV header");
    }
    if (!fourcc_eq(riff, "RIFF") || !fourcc_eq(wave, "WAVE")) {
        return std::unexpected("not a RIFF/WAVE file");
    }

    const uint64_t file_size = src.size();
    bool fmt_seen = false;
    bool data_seen = false;
    WavLayout layout;
    uint16_t tag = 0;
    uint16_t bits = 0;
    uint16_t valid_bits = 0;

    while (src.tell() + 8 <= file_size) {
        char id[4];
        if (!src.read(id, 4) || !src.read(u32buf, 4)) {
            return std::unexpected("truncated WAV chunk header");
        }
        const uint32_t chunk_size = read_u32(u32buf);
        const uint64_t payload = src.tell();
        uint64_t chunk_payload_size = chunk_size;
        bool stop_after_chunk = false;
        if (chunk_payload_size > file_size - payload) {
            if (fourcc_eq(id, "data")) {
                // Streamed encoders may leave the data size unpatched; trust
                // the bytes present, clamped to whole frames.
                chunk_payload_size = file_size - payload;
                if (layout.block_align != 0) {
                    chunk_payload_size -= chunk_payload_size % layout.block_align;
                }
                stop_after_chunk = true;
            } else {
                break;
            }
        }

        if (fourcc_eq(id, "fmt ")) {
            if (chunk_payload_size < 16) {
                return std::unexpected("WAV fmt chunk is too short");
            }
            std::array<unsigned char, 40> fmt{};
            const size_t to_read =
                std::min<size_t>(fmt.size(), chunk_payload_size);
            if (!src.read(fmt.data(), to_read)) {
                return std::unexpected("truncated WAV fmt chunk");
            }
            tag = read_u16(fmt, 0);
            layout.info.channels = read_u16(fmt, 2);
            layout.info.sample_rate = static_cast<int>(read_u32(fmt, 4));
            layout.block_align = read_u16(fmt, 12);
            bits = read_u16(fmt, 14);
            valid_bits = bits;

            if (tag == 0xfffe) {
                if (chunk_payload_size < 40) {
                    return std::unexpected("WAVE_FORMAT_EXTENSIBLE fmt chunk is too short");
                }
                const uint16_t cb_size = read_u16(fmt, 16);
                if (cb_size < 22) {
                    return std::unexpected("WAVE_FORMAT_EXTENSIBLE cbSize is too short");
                }
                valid_bits = read_u16(fmt, 18);
                if (guid_is_subformat(fmt.data() + 24, 1)) {
                    tag = 1;
                } else if (guid_is_subformat(fmt.data() + 24, 3)) {
                    tag = 3;
                } else {
                    return std::unexpected("unsupported WAVE_FORMAT_EXTENSIBLE subformat");
                }
            }

            if (layout.info.channels <= 0 || layout.info.sample_rate <= 0 ||
                layout.block_align == 0) {
                return std::unexpected("invalid WAV fmt values");
            }
            if (tag == 1 && valid_bits == 16 && bits == 16) {
                layout.info.format = WavSampleFormat::Pcm16;
            } else if (tag == 1 && valid_bits == 24 && bits == 24) {
                layout.info.format = WavSampleFormat::Pcm24;
            } else if (tag == 3 && valid_bits == 32 && bits == 32) {
                layout.info.format = WavSampleFormat::Float32;
            } else {
                return std::unexpected("unsupported WAV sample format");
            }

            const uint16_t expected_align =
                static_cast<uint16_t>(layout.info.channels * (bits / 8));
            if (layout.block_align != expected_align) {
                return std::unexpected("WAV block alignment mismatch");
            }
            fmt_seen = true;
        } else if (fourcc_eq(id, "data")) {
            layout.data_offset = payload;
            layout.data_size = chunk_payload_size;
            data_seen = true;
        }

        if (stop_after_chunk) break;

        const uint64_t next =
            payload + chunk_payload_size + (chunk_payload_size & 1u);
        if (!src.seek(next)) {
            return std::unexpected("failed to skip WAV chunk");
        }
    }

    if (!fmt_seen) return std::unexpected("WAV fmt chunk not found");
    if (!data_seen) return std::unexpected("WAV data chunk not found");
    if (layout.data_size % layout.block_align != 0) {
        return std::unexpected("WAV data size is not frame-aligned");
    }
    layout.info.frames =
        static_cast<int64_t>(layout.data_size / layout.block_align);
    return layout;
}

void decode_wav_samples(const unsigned char* raw, WavSampleFormat format,
                        int64_t samples, float* out);

std::expected<std::vector<float>, std::string>
read_range_from_source(ByteSource& src, int64_t begin_frame, int64_t end_frame,
                       WavInfo* info_out)
{
    auto parsed = parse_wav_layout(src);
    if (!parsed) return std::unexpected(parsed.error());
    const WavLayout& layout = *parsed;
    if (begin_frame < 0 || end_frame < begin_frame ||
        end_frame > layout.info.frames) {
        return std::unexpected("invalid WAV frame range");
    }
    if (info_out) *info_out = layout.info;

    const int64_t frames = end_frame - begin_frame;
    const int64_t samples = frames * layout.info.channels;
    std::vector<float> out(static_cast<size_t>(samples));
    if (samples == 0) return out;

    const uint64_t byte_offset =
        layout.data_offset +
        static_cast<uint64_t>(begin_frame) * layout.block_align;
    const size_t bytes =
        static_cast<size_t>(frames) * static_cast<size_t>(layout.block_align);
    std::vector<unsigned char> raw(bytes);
    if (!src.seek(byte_offset) || !src.read(raw.data(), raw.size())) {
        return std::unexpected("truncated WAV data");
    }

    decode_wav_samples(raw.data(), layout.info.format, samples, out.data());
    return out;
}

void decode_wav_samples(const unsigned char* raw, WavSampleFormat format,
                        int64_t samples, float* out)
{
    size_t rp = 0;
    for (int64_t i = 0; i < samples; ++i) {
        if (format == WavSampleFormat::Pcm16) {
            const int16_t v =
                static_cast<int16_t>(raw[rp] | (raw[rp + 1] << 8));
            out[static_cast<size_t>(i)] = static_cast<float>(v) / 32768.0f;
            rp += 2;
        } else if (format == WavSampleFormat::Pcm24) {
            int32_t v = static_cast<int32_t>(raw[rp]) |
                        (static_cast<int32_t>(raw[rp + 1]) << 8) |
                        (static_cast<int32_t>(raw[rp + 2]) << 16);
            if (v & 0x00800000) v |= static_cast<int32_t>(0xff000000);
            out[static_cast<size_t>(i)] = pcm24_float_from_code(v);
            rp += 3;
        } else {
            uint32_t bits32 = static_cast<uint32_t>(raw[rp]) |
                              (static_cast<uint32_t>(raw[rp + 1]) << 8) |
                              (static_cast<uint32_t>(raw[rp + 2]) << 16) |
                              (static_cast<uint32_t>(raw[rp + 3]) << 24);
            float v;
            std::memcpy(&v, &bits32, sizeof(v));
            out[static_cast<size_t>(i)] = v;
            rp += 4;
        }
    }
}

std::expected<ByteSource, std::string> memory_source(std::span<const char> bytes)
{
    ByteSource src;
    src.kind = SourceKind::Memory;
    src.memory = bytes;
    return src;
}

void append_u16(std::vector<unsigned char>& v, uint16_t x)
{
    v.push_back(static_cast<unsigned char>(x & 0xff));
    v.push_back(static_cast<unsigned char>((x >> 8) & 0xff));
}

void append_u32(std::vector<unsigned char>& v, uint32_t x)
{
    v.push_back(static_cast<unsigned char>(x & 0xff));
    v.push_back(static_cast<unsigned char>((x >> 8) & 0xff));
    v.push_back(static_cast<unsigned char>((x >> 16) & 0xff));
    v.push_back(static_cast<unsigned char>((x >> 24) & 0xff));
}

} // namespace

std::expected<WavInfo, std::string> wav_probe(const std::string& path)
{
    FilePtr f(std::fopen(path.c_str(), "rb"));
    if (!f) return std::unexpected("failed to open WAV file");
    ByteSource src;
    src.kind = SourceKind::File;
    src.file = f.get();
    auto parsed = parse_wav_layout(src);
    if (!parsed) return std::unexpected(parsed.error());
    return parsed->info;
}

std::expected<WavInfo, std::string> wav_probe(std::span<const char> bytes)
{
    auto src = memory_source(bytes);
    auto parsed = parse_wav_layout(*src);
    if (!parsed) return std::unexpected(parsed.error());
    return parsed->info;
}

std::expected<std::vector<float>, std::string>
wav_read_full(const std::string& path, WavInfo* info_out)
{
    auto info = wav_probe(path);
    if (!info) return std::unexpected(info.error());
    return wav_read_range(path, 0, info->frames, info_out);
}

std::expected<std::vector<float>, std::string>
wav_read_full(std::span<const char> bytes, WavInfo* info_out)
{
    auto info = wav_probe(bytes);
    if (!info) return std::unexpected(info.error());
    return wav_read_range(bytes, 0, info->frames, info_out);
}

std::expected<std::vector<float>, std::string>
wav_read_range(const std::string& path, int64_t begin_frame, int64_t end_frame,
               WavInfo* info_out)
{
    FilePtr f(std::fopen(path.c_str(), "rb"));
    if (!f) return std::unexpected("failed to open WAV file");
    ByteSource src;
    src.kind = SourceKind::File;
    src.file = f.get();
    return read_range_from_source(src, begin_frame, end_frame, info_out);
}

std::expected<std::vector<float>, std::string>
wav_read_range(std::span<const char> bytes, int64_t begin_frame,
               int64_t end_frame, WavInfo* info_out)
{
    auto src = memory_source(bytes);
    return read_range_from_source(*src, begin_frame, end_frame, info_out);
}

WavReader::WavReader(WavReader&& other) noexcept
{
    *this = std::move(other);
}

WavReader& WavReader::operator=(WavReader&& other) noexcept
{
    if (this == &other) return *this;
    reset();
    file_ = other.file_;
    info_ = other.info_;
    data_offset_ = other.data_offset_;
    block_align_ = other.block_align_;
    cursor_frame_ = other.cursor_frame_;
    other.file_ = nullptr;
    other.info_ = {};
    other.data_offset_ = 0;
    other.block_align_ = 0;
    other.cursor_frame_ = 0;
    return *this;
}

WavReader::~WavReader()
{
    reset();
}

std::expected<WavReader, std::string>
WavReader::open(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        const int err = errno;
        return std::unexpected(
            append_errno_detail("failed to open WAV file", err));
    }

    ByteSource src;
    src.kind = SourceKind::File;
    src.file = f;
    auto parsed = parse_wav_layout(src);
    if (!parsed) {
        std::fclose(f);
        return std::unexpected(parsed.error());
    }

    WavReader out;
    out.file_ = f;
    out.info_ = parsed->info;
    out.data_offset_ = parsed->data_offset;
    out.block_align_ = parsed->block_align;
    auto seeked = out.seek_to_frame(0);
    if (!seeked) return std::unexpected(seeked.error());
    return out;
}

std::expected<void, std::string> WavReader::seek_to_frame(int64_t frame)
{
    if (!file_) return std::unexpected("WAV reader is not open");
    if (frame < 0 || frame > info_.frames) {
        return std::unexpected("invalid WAV frame range");
    }
    const uint64_t byte_offset =
        data_offset_ + static_cast<uint64_t>(frame) * block_align_;
    if (std::fseek(file_, static_cast<long>(byte_offset), SEEK_SET) != 0) {
        return std::unexpected("failed to seek WAV file");
    }
    cursor_frame_ = frame;
    return {};
}

std::expected<int64_t, std::string> WavReader::read_frames(float* out,
                                                           int64_t frames)
{
    if (!file_) return std::unexpected("WAV reader is not open");
    if (frames < 0 || (frames > 0 && out == nullptr)) {
        return std::unexpected("invalid WAV frame read");
    }
    const int64_t available = info_.frames - cursor_frame_;
    const int64_t to_read = std::min(frames, available);
    if (to_read <= 0) return int64_t{0};

    const uint64_t bytes =
        static_cast<uint64_t>(to_read) * static_cast<uint64_t>(block_align_);
    if (bytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return std::unexpected("WAV read is too large");
    }
    std::vector<unsigned char> raw(static_cast<size_t>(bytes));
    if (std::fread(raw.data(), 1, raw.size(), file_) != raw.size()) {
        return std::unexpected("truncated WAV data");
    }
    decode_wav_samples(raw.data(), info_.format, to_read * info_.channels, out);
    cursor_frame_ += to_read;
    return to_read;
}

void WavReader::reset()
{
    if (file_) std::fclose(file_);
    file_ = nullptr;
    info_ = {};
    data_offset_ = 0;
    block_align_ = 0;
    cursor_frame_ = 0;
}

WavWriter::WavWriter(WavWriter&& other) noexcept
{
    *this = std::move(other);
}

WavWriter& WavWriter::operator=(WavWriter&& other) noexcept
{
    if (this == &other) return *this;
    reset();
    sink_kind_ = other.sink_kind_;
    file_ = other.file_;
    memory_ = other.memory_;
    format_ = other.format_;
    channels_ = other.channels_;
    sample_rate_ = other.sample_rate_;
    frames_written_ = other.frames_written_;
    data_bytes_ = other.data_bytes_;
    riff_size_offset_ = other.riff_size_offset_;
    fact_frames_offset_ = other.fact_frames_offset_;
    data_size_offset_ = other.data_size_offset_;
    closed_ = other.closed_;
    other.sink_kind_ = SinkKind::None;
    other.file_ = nullptr;
    other.memory_ = nullptr;
    other.closed_ = true;
    return *this;
}

WavWriter::~WavWriter()
{
    reset();
}

std::expected<WavWriter, std::string>
WavWriter::open_file(const std::string& path, WavSampleFormat format,
                     int channels, int sample_rate)
{
    if ((format != WavSampleFormat::Pcm24 &&
         format != WavSampleFormat::Float32) ||
        channels <= 0 || sample_rate <= 0) {
        return std::unexpected("invalid WAV writer parameters");
    }
    FILE* f = std::fopen(path.c_str(), "wb+");
    if (!f) {
        const int err = errno;
        return std::unexpected(
            append_errno_detail("failed to create WAV file", err));
    }

    WavWriter w;
    w.sink_kind_ = SinkKind::File;
    w.file_ = f;
    w.format_ = format;
    w.channels_ = channels;
    w.sample_rate_ = sample_rate;
    w.closed_ = false;
    auto ok = w.write_header();
    if (!ok) return std::unexpected(ok.error());
    return w;
}

std::expected<WavWriter, std::string>
WavWriter::open_memory(std::vector<char>& out, WavSampleFormat format,
                       int channels, int sample_rate)
{
    if ((format != WavSampleFormat::Pcm24 &&
         format != WavSampleFormat::Float32) ||
        channels <= 0 || sample_rate <= 0) {
        return std::unexpected("invalid WAV writer parameters");
    }
    out.clear();
    WavWriter w;
    w.sink_kind_ = SinkKind::Memory;
    w.memory_ = &out;
    w.format_ = format;
    w.channels_ = channels;
    w.sample_rate_ = sample_rate;
    w.closed_ = false;
    auto ok = w.write_header();
    if (!ok) return std::unexpected(ok.error());
    return w;
}

std::expected<void, std::string> WavWriter::write_frames(
    const float* interleaved, int64_t frames)
{
    if (closed_) return std::unexpected("WAV writer is closed");
    if (frames < 0 || (frames > 0 && interleaved == nullptr)) {
        return std::unexpected("invalid WAV frame write");
    }
    if (frames == 0) return {};

    const uint64_t samples =
        static_cast<uint64_t>(frames) * static_cast<uint64_t>(channels_);
    if (format_ == WavSampleFormat::Pcm24) {
        std::vector<unsigned char> packed(samples * 3);
        size_t wp = 0;
        for (uint64_t i = 0; i < samples; ++i) {
            const uint32_t code =
                static_cast<uint32_t>(pcm24_code_from_float(interleaved[i]));
            packed[wp++] = static_cast<unsigned char>(code & 0xff);
            packed[wp++] = static_cast<unsigned char>((code >> 8) & 0xff);
            packed[wp++] = static_cast<unsigned char>((code >> 16) & 0xff);
        }
        auto ok = write_bytes(packed.data(), packed.size());
        if (!ok) return ok;
        data_bytes_ += packed.size();
    } else {
        const uint64_t bytes = samples * sizeof(float);
        if (bytes > std::numeric_limits<size_t>::max()) {
            return std::unexpected("WAV write is too large");
        }
        auto ok = write_bytes(interleaved, static_cast<size_t>(bytes));
        if (!ok) return ok;
        data_bytes_ += bytes;
    }
    frames_written_ += static_cast<uint64_t>(frames);
    return {};
}

std::expected<void, std::string> WavWriter::close()
{
    if (closed_) return {};
    if (data_bytes_ > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected("WAV data chunk exceeds RIFF size limit");
    }
    const uint64_t riff_size = data_size_offset_ + 4 + data_bytes_ - 8;
    if (riff_size > std::numeric_limits<uint32_t>::max() ||
        frames_written_ > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected("WAV file exceeds RIFF size limit");
    }
    auto ok = patch_u32(riff_size_offset_, static_cast<uint32_t>(riff_size));
    if (!ok) return ok;
    ok = patch_u32(data_size_offset_, static_cast<uint32_t>(data_bytes_));
    if (!ok) return ok;
    if (format_ == WavSampleFormat::Float32) {
        ok = patch_u32(fact_frames_offset_,
                       static_cast<uint32_t>(frames_written_));
        if (!ok) return ok;
    }
    if (sink_kind_ == SinkKind::File) {
        errno = 0;
        if (std::fflush(file_) != 0) {
            const int err = errno;
            file_ = nullptr;
            closed_ = true;
            return std::unexpected(
                append_errno_detail("failed to close WAV file", err));
        }
        errno = 0;
        if (std::fclose(file_) != 0) {
            const int err = errno;
            file_ = nullptr;
            closed_ = true;
            return std::unexpected(
                append_errno_detail("failed to close WAV file", err));
        }
        file_ = nullptr;
    }
    closed_ = true;
    return {};
}

std::expected<void, std::string> WavWriter::write_header()
{
    const uint16_t bits = format_ == WavSampleFormat::Pcm24 ? 24 : 32;
    const uint16_t block_align = static_cast<uint16_t>(channels_ * (bits / 8));
    const uint32_t byte_rate =
        static_cast<uint32_t>(sample_rate_ * block_align);
    std::vector<unsigned char> h;
    h.insert(h.end(), {'R', 'I', 'F', 'F'});
    riff_size_offset_ = h.size();
    append_u32(h, 0);
    h.insert(h.end(), {'W', 'A', 'V', 'E'});
    h.insert(h.end(), {'f', 'm', 't', ' '});
    append_u32(h, 16);
    append_u16(h, format_ == WavSampleFormat::Pcm24 ? 1 : 3);
    append_u16(h, static_cast<uint16_t>(channels_));
    append_u32(h, static_cast<uint32_t>(sample_rate_));
    append_u32(h, byte_rate);
    append_u16(h, block_align);
    append_u16(h, bits);
    if (format_ == WavSampleFormat::Float32) {
        h.insert(h.end(), {'f', 'a', 'c', 't'});
        append_u32(h, 4);
        fact_frames_offset_ = h.size();
        append_u32(h, 0);
    }
    h.insert(h.end(), {'d', 'a', 't', 'a'});
    data_size_offset_ = h.size();
    append_u32(h, 0);
    return write_bytes(h.data(), h.size());
}

std::expected<void, std::string> WavWriter::write_bytes(const void* data,
                                                        size_t size)
{
    if (sink_kind_ == SinkKind::File) {
        errno = 0;
        if (size > 0 && std::fwrite(data, 1, size, file_) != size) {
            const int err = errno;
            return std::unexpected(
                append_errno_detail("failed to write WAV data", err));
        }
    } else if (sink_kind_ == SinkKind::Memory) {
        const char* p = static_cast<const char*>(data);
        memory_->insert(memory_->end(), p, p + size);
    } else {
        return std::unexpected("WAV writer has no sink");
    }
    return {};
}

std::expected<void, std::string> WavWriter::patch_u32(uint64_t offset,
                                                      uint32_t value)
{
    const unsigned char b[4] = {
        static_cast<unsigned char>(value & 0xff),
        static_cast<unsigned char>((value >> 8) & 0xff),
        static_cast<unsigned char>((value >> 16) & 0xff),
        static_cast<unsigned char>((value >> 24) & 0xff),
    };
    if (sink_kind_ == SinkKind::File) {
        errno = 0;
        if (std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) {
            const int err = errno;
            return std::unexpected(
                append_errno_detail("failed to patch WAV header", err));
        }
        errno = 0;
        if (std::fwrite(b, 1, sizeof(b), file_) != sizeof(b)) {
            const int err = errno;
            return std::unexpected(
                append_errno_detail("failed to patch WAV header", err));
        }
        errno = 0;
        if (std::fseek(file_, 0, SEEK_END) != 0) {
            const int err = errno;
            return std::unexpected(
                append_errno_detail("failed to patch WAV header", err));
        }
    } else if (sink_kind_ == SinkKind::Memory) {
        if (offset + sizeof(b) > memory_->size()) {
            return std::unexpected("invalid WAV memory patch offset");
        }
        std::memcpy(memory_->data() + offset, b, sizeof(b));
    }
    return {};
}

void WavWriter::reset()
{
    if (!closed_) {
        (void)close();
    }
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    sink_kind_ = SinkKind::None;
    memory_ = nullptr;
    closed_ = true;
}
