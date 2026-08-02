#include "wav_io.h"

#include "pcm24.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <system_error>
#include <utility>

static_assert(std::endian::native == std::endian::little);
// fseek offset casts in this file rely on 64-bit long.
static_assert(sizeof(long) == 8);

// Terminal message strings in this file carry sentence-initial capitals and
// spell WAV as the acronym in prose (architect approval 2026-08-02, the
// terminal capitalization pass — text-only, otherwise byte-identical
// output). They reach the terminal as standalone messages after the
// program-name prefix (the CLI's bare "warptempo_cli: %s" load and
// projection sites), so the capital belongs here at the definition; where a
// caller shows them after its own category words the embedded capital is the
// accepted cost recorded for the six GUI-painted refusals
// (warp_frame_map_build.cpp). FourCC chunk ids (fmt, data) and
// WAVE_FORMAT_EXTENSIBLE field names stay verbatim — they are data.

namespace {

struct FileCloser {
    void operator()(FILE* f) const
    {
        if (f) std::fclose(f);
    }
};

using FilePtr = std::unique_ptr<FILE, FileCloser>;

std::string implausible_alloc_message(uint64_t bytes)
{
    const uint64_t mib =
        (bytes + 1024ull * 1024ull - 1) / (1024ull * 1024ull);
    return "Implausibly large audio allocation (" + std::to_string(mib) +
           " MiB); refusing";
}

struct ByteSource {
    FILE* file = nullptr;

    bool read(void* out, size_t size)
    {
        return std::fread(out, 1, size, file) == size;
    }

    bool seek(uint64_t offset)
    {
        return std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0;
    }

    uint64_t tell() const
    {
        const long pos = std::ftell(file);
        return pos < 0 ? 0 : static_cast<uint64_t>(pos);
    }

    uint64_t size()
    {
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

} // namespace

std::string append_errno_detail(const char* message, int err)
{
    std::string out(message);
    if (err != 0) {
        out += " (";
        out += std::error_code(err, std::generic_category()).message();
        out += ")";
    }
    return out;
}

std::expected<size_t, std::string>
checked_audio_sample_count(int64_t frames, int channels)
{
    if (frames < 0 || channels <= 0) {
        return std::unexpected("Invalid audio shape (frames=" +
                               std::to_string(frames) + ", channels=" +
                               std::to_string(channels) + ")");
    }

    const uint64_t samples =
        static_cast<uint64_t>(frames) * static_cast<uint64_t>(channels);
    if (channels > 0 &&
        samples / static_cast<uint64_t>(channels) !=
            static_cast<uint64_t>(frames)) {
        return std::unexpected("Audio allocation is too large");
    }
    const uint64_t bytes = samples * sizeof(float);
    if (bytes / sizeof(float) != samples) {
        return std::unexpected("Audio allocation is too large");
    }
    if (bytes > kMaxPlausibleAudioAllocBytes) {
        return std::unexpected(implausible_alloc_message(bytes));
    }
    if (samples > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return std::unexpected("Audio allocation is too large");
    }
    return static_cast<size_t>(samples);
}

namespace {

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
        return std::unexpected("Short WAV header");
    }
    if (!fourcc_eq(riff, "RIFF") || !fourcc_eq(wave, "WAVE")) {
        return std::unexpected("Not a RIFF/WAVE file");
    }

    const uint64_t file_size = src.size();

    // The RIFF size counts everything after its own 8-byte header (the WAVE
    // tag onward), so a spec-conformant container satisfies 8 + riff_size ==
    // physical file size; the in-tree WavWriter patches exactly this value at
    // close. A declaration short of or past the physical EOF contradicts the
    // container and is adversarial input that no in-tree writer can produce, so
    // it refuses here like the duplicate-chunk and truncated-chunk guards.
    const uint32_t riff_size = read_u32(u32buf);
    if (static_cast<uint64_t>(riff_size) + 8 != file_size) {
        return std::unexpected(
            "WAV RIFF size contradicts file size (declared " +
            std::to_string(static_cast<uint64_t>(riff_size) + 8) +
            " vs physical " + std::to_string(file_size) + ")");
    }
    bool fmt_seen = false;
    bool data_seen = false;
    WavLayout layout;
    uint16_t tag = 0;
    uint16_t bits = 0;
    uint16_t valid_bits = 0;

    while (src.tell() + 8 <= file_size) {
        char id[4];
        if (!src.read(id, 4) || !src.read(u32buf, 4)) {
            return std::unexpected("Truncated WAV chunk header");
        }
        const uint32_t chunk_size = read_u32(u32buf);
        const uint64_t payload = src.tell();
        const uint64_t chunk_payload_size = chunk_size;
        // A chunk whose declared payload runs past the physical EOF is an
        // ordinary truncated or corrupted file — adversarial input — and
        // refuses here.
        if (chunk_payload_size > file_size - payload) {
            return std::unexpected("WAV chunk extends past end of file");
        }

        if (fourcc_eq(id, "fmt ")) {
            if (fmt_seen) {
                return std::unexpected("Duplicate WAV fmt chunk");
            }
            if (chunk_payload_size < 16) {
                return std::unexpected("WAV fmt chunk is too short");
            }
            std::array<unsigned char, 40> fmt{};
            const size_t to_read =
                std::min<size_t>(fmt.size(), chunk_payload_size);
            if (!src.read(fmt.data(), to_read)) {
                return std::unexpected("Truncated WAV fmt chunk");
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
                // cbSize declares the extension bytes following the 18-byte
                // base header, so 18 + cbSize must fit inside the chunk
                // payload. The in-tree writer never authors extensible fmt,
                // so a declaration running past its own chunk is adversarial
                // container corruption refused at the owner boundary. Wide
                // arithmetic so the sum cannot wrap.
                if (18 + static_cast<uint64_t>(cb_size) > chunk_payload_size) {
                    return std::unexpected(
                        "WAVE_FORMAT_EXTENSIBLE cbSize exceeds its fmt chunk");
                }
                valid_bits = read_u16(fmt, 18);
                if (guid_is_subformat(fmt.data() + 24, 1)) {
                    tag = 1;
                } else {
                    return std::unexpected("Unsupported WAVE_FORMAT_EXTENSIBLE subformat");
                }
            }

            if (layout.info.channels <= 0 || layout.info.sample_rate <= 0 ||
                layout.block_align == 0) {
                return std::unexpected("Invalid WAV fmt values");
            }
            if (tag == 1 && valid_bits == 16 && bits == 16) {
                layout.info.format = WavSampleFormat::Pcm16;
            } else if (tag == 1 && valid_bits == 24 && bits == 24) {
                layout.info.format = WavSampleFormat::Pcm24;
            } else {
                return std::unexpected("Unsupported WAV sample format");
            }

            // The multiply stays wide so a header-claimed channel count whose
            // true alignment exceeds the u16 block_align field cannot wrap into
            // a false match; the mismatch refusal below then covers it.
            const uint32_t expected_align =
                static_cast<uint32_t>(layout.info.channels) * (bits / 8u);
            if (layout.block_align != expected_align) {
                return std::unexpected("WAV block alignment mismatch");
            }
            // For the PCM/Float formats accepted above, byte_rate (fmt
            // payload offset 8) is fully determined by sample_rate *
            // block_align, and the in-tree writer always authors exactly
            // that product; a contradiction is adversarial container
            // corruption refused at the owner boundary. The multiply stays
            // wide so no 32-bit wrap can fake a match.
            if (static_cast<uint64_t>(read_u32(fmt, 4)) * layout.block_align !=
                read_u32(fmt, 8)) {
                return std::unexpected(
                    "WAV byte rate contradicts sample rate and block alignment");
            }
            fmt_seen = true;
        } else if (fourcc_eq(id, "data")) {
            if (data_seen) {
                return std::unexpected("Duplicate WAV data chunk");
            }
            layout.data_offset = payload;
            layout.data_size = chunk_payload_size;
            data_seen = true;
        }

        uint64_t next =
            payload + chunk_payload_size + (chunk_payload_size & 1u);
        // The FINAL chunk's pad byte may be legitimately absent: common
        // taggers and encoders do not pad the last chunk of the file, and
        // the RIFF size then counts the unpadded content (the equality
        // guard above already matched it against the physical size).
        // Chunk CONTENT past EOF refused at the top of the loop, so an
        // overshooting `next` here can only be that absent final pad —
        // end the walk at the physical EOF instead of stepping past it,
        // so the final seek lands exactly on EOF rather than off the end.
        if (next > file_size) next = file_size;
        if (!src.seek(next)) {
            return std::unexpected("Failed to skip WAV chunk");
        }
    }

    // A conforming chunk walk consumes the container exactly: every chunk
    // advances by its payload plus its odd-payload pad byte (absent-final-pad
    // clamp above), so the final skip lands precisely on the physical EOF. A
    // walk that halts short of EOF left fewer than eight dangling bytes — a
    // truncated chunk header, adversarial (the RIFF-size equality guard above
    // passes on a file whose dangling 1..7 trailing bytes are counted into
    // riff_size, so this catch is not redundant with it).
    if (src.tell() != file_size) {
        return std::unexpected("Truncated WAV chunk header");
    }

    if (!fmt_seen) return std::unexpected("WAV fmt chunk not found");
    if (!data_seen) return std::unexpected("WAV data chunk not found");
    if (layout.data_size % layout.block_align != 0) {
        return std::unexpected("WAV data size is not frame-aligned");
    }
    layout.info.frames =
        static_cast<int64_t>(layout.data_size / layout.block_align);
    // Zero frames is unusable audio, so it hard-fails here at the owner
    // boundary instead of proceeding into the load-lenient marker flow.
    if (layout.info.frames == 0) {
        return std::unexpected("WAV data chunk holds zero frames");
    }
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
        return std::unexpected("Invalid WAV frame range");
    }
    if (info_out) *info_out = layout.info;

    const int64_t frames = end_frame - begin_frame;
    auto sample_count = checked_audio_sample_count(frames, layout.info.channels);
    if (!sample_count) return std::unexpected(sample_count.error());
    std::vector<float> out(*sample_count);
    if (*sample_count == 0) return out;

    const uint64_t byte_offset =
        layout.data_offset +
        static_cast<uint64_t>(begin_frame) * layout.block_align;
    const uint64_t bytes =
        static_cast<uint64_t>(frames) *
        static_cast<uint64_t>(layout.block_align);
    if (layout.block_align != 0 &&
        bytes / static_cast<uint64_t>(layout.block_align) !=
            static_cast<uint64_t>(frames)) {
        return std::unexpected("WAV read is too large");
    }
    if (bytes > kMaxPlausibleAudioAllocBytes) {
        return std::unexpected(implausible_alloc_message(bytes));
    }
    if (bytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return std::unexpected("WAV read is too large");
    }
    std::vector<unsigned char> raw(static_cast<size_t>(bytes));
    if (!src.seek(byte_offset) || !src.read(raw.data(), raw.size())) {
        return std::unexpected("Truncated WAV data");
    }

    decode_wav_samples(raw.data(), layout.info.format,
                       static_cast<int64_t>(*sample_count), out.data());
    return out;
}

// Decodes a PCM 16 or PCM 24 payload to interleaved float32. Every bit
// pattern of both formats decodes to a finite value by construction.
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
        } else {
            int32_t v = static_cast<int32_t>(raw[rp]) |
                        (static_cast<int32_t>(raw[rp + 1]) << 8) |
                        (static_cast<int32_t>(raw[rp + 2]) << 16);
            if (v & 0x00800000) v |= static_cast<int32_t>(0xff000000);
            out[static_cast<size_t>(i)] = pcm24_float_from_code(v);
            rp += 3;
        }
    }
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

bool wav_exceeds_riff_limits(uint64_t header_span, uint64_t data_bytes,
                             uint64_t frames_written)
{
    if (data_bytes > std::numeric_limits<uint32_t>::max()) return true;
    const uint64_t riff_size = header_span + data_bytes - 8;
    return riff_size > std::numeric_limits<uint32_t>::max() ||
           frames_written > std::numeric_limits<uint32_t>::max();
}

bool wav_projected_exceeds_riff_limits(int channels, uint64_t frames)
{
    const uint64_t block_align = static_cast<uint64_t>(channels) * 3;
    const uint64_t data_bytes = frames * block_align;
    if (block_align > 0 && data_bytes / block_align != frames) return true;
    const uint64_t header_span = 44;
    return wav_exceeds_riff_limits(header_span, data_bytes, frames);
}

std::expected<WavInfo, std::string> wav_probe(const std::string& path)
{
    FilePtr f(std::fopen(path.c_str(), "rb"));
    if (!f) {
        const int err = errno;
        return std::unexpected(
            append_errno_detail("Failed to open WAV file", err));
    }
    ByteSource src;
    src.file = f.get();
    auto parsed = parse_wav_layout(src);
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
wav_read_range(const std::string& path, int64_t begin_frame, int64_t end_frame,
               WavInfo* info_out)
{
    FilePtr f(std::fopen(path.c_str(), "rb"));
    if (!f) {
        const int err = errno;
        return std::unexpected(
            append_errno_detail("Failed to open WAV file", err));
    }
    ByteSource src;
    src.file = f.get();
    return read_range_from_source(src, begin_frame, end_frame, info_out);
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
    channels_ = other.channels_;
    sample_rate_ = other.sample_rate_;
    frames_written_ = other.frames_written_;
    data_bytes_ = other.data_bytes_;
    riff_size_offset_ = other.riff_size_offset_;
    data_size_offset_ = other.data_size_offset_;
    closed_ = other.closed_;
    scratch_ = std::move(other.scratch_);
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
WavWriter::open_file(const std::string& path, int channels, int sample_rate)
{
    // The writer emits PCM 24 only and narrows the caller's geometry into the
    // RIFF fmt fields as given.
    FILE* f = std::fopen(path.c_str(), "wb+");
    if (!f) {
        const int err = errno;
        return std::unexpected(
            append_errno_detail("Failed to create WAV file", err));
    }

    WavWriter w;
    w.sink_kind_ = SinkKind::File;
    w.file_ = f;
    w.channels_ = channels;
    w.sample_rate_ = sample_rate;
    w.closed_ = false;
    auto ok = w.write_header();
    if (!ok) {
        // The header never landed; close and remove the just-created file so
        // a failed open leaves no stray artifact for the caller to clean up.
        std::fclose(w.file_);
        w.file_ = nullptr;
        w.closed_ = true;
        std::remove(path.c_str());
        return std::unexpected(ok.error());
    }
    return w;
}

std::expected<WavWriter, std::string>
WavWriter::open_memory(std::vector<char>& out, int channels, int sample_rate)
{
    // The writer emits PCM 24 only and narrows the caller's geometry into the
    // RIFF fmt fields as given.
    out.clear();
    WavWriter w;
    w.sink_kind_ = SinkKind::Memory;
    w.memory_ = &out;
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
        return std::unexpected("Invalid WAV frame write");
    }
    if (frames == 0) return {};

    const uint64_t samples =
        static_cast<uint64_t>(frames) * static_cast<uint64_t>(channels_);
    if (channels_ > 0 &&
        samples / static_cast<uint64_t>(channels_) !=
            static_cast<uint64_t>(frames)) {
        return std::unexpected("WAV write is too large");
    }
    const uint64_t bytes = samples * 3;
    if (bytes / 3 != samples ||
        bytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
        data_bytes_ > std::numeric_limits<uint64_t>::max() - bytes ||
        frames_written_ > std::numeric_limits<uint64_t>::max() -
                              static_cast<uint64_t>(frames)) {
        return std::unexpected("WAV write is too large");
    }
    const uint64_t post_data_bytes = data_bytes_ + bytes;
    const uint64_t post_frames =
        frames_written_ + static_cast<uint64_t>(frames);
    const uint64_t header_span = data_size_offset_ + 4;
    if (wav_exceeds_riff_limits(header_span, post_data_bytes, post_frames)) {
        if (post_data_bytes > std::numeric_limits<uint32_t>::max()) {
            return std::unexpected("WAV data chunk exceeds RIFF size limit");
        }
        return std::unexpected("WAV file exceeds RIFF size limit");
    }

    scratch_.resize(static_cast<size_t>(bytes));
    size_t wp = 0;
    for (uint64_t i = 0; i < samples; ++i) {
        // Breach guard: nothing program-generated is non-finite (the
        // read boundary rejects non-finite sources), so a NaN or
        // infinity here means an internal contract broke — refuse
        // loudly rather than silently normalizing it into the lattice.
        if (!std::isfinite(interleaved[i])) {
            return std::unexpected("Non-finite sample in PCM 24 write");
        }
        const uint32_t code =
            static_cast<uint32_t>(pcm24_code_from_float(interleaved[i]));
        scratch_[wp++] = static_cast<unsigned char>(code & 0xff);
        scratch_[wp++] = static_cast<unsigned char>((code >> 8) & 0xff);
        scratch_[wp++] = static_cast<unsigned char>((code >> 16) & 0xff);
    }
    if (auto ok = write_bytes(scratch_.data(), scratch_.size()); !ok) {
        return ok;
    }
    data_bytes_ = post_data_bytes;
    frames_written_ = post_frames;
    return {};
}

std::expected<void, std::string> WavWriter::close()
{
    if (closed_) return {};
    const uint64_t header_span = data_size_offset_ + 4;
    if (wav_exceeds_riff_limits(header_span, data_bytes_, frames_written_)) {
        if (data_bytes_ > std::numeric_limits<uint32_t>::max()) {
            return std::unexpected("WAV data chunk exceeds RIFF size limit");
        }
        return std::unexpected("WAV file exceeds RIFF size limit");
    }
    // Every payload this writer produces is even by construction: PCM24 frames
    // are 3 bytes times the stereo channel pair (6), because sources are
    // stereo-only and output channels equal source channels. The RIFF
    // odd-payload pad byte is therefore unreachable and deliberately
    // unimplemented; a future channel-vocabulary change must revisit this.
    const uint64_t riff_size = header_span + data_bytes_ - 8;
    auto ok = patch_u32(riff_size_offset_, static_cast<uint32_t>(riff_size));
    if (!ok) return ok;
    ok = patch_u32(data_size_offset_, static_cast<uint32_t>(data_bytes_));
    if (!ok) return ok;
    if (sink_kind_ == SinkKind::File) {
        errno = 0;
        if (std::fflush(file_) != 0) {
            const int err = errno;
            // fclose releases the stream even on failure; report the flush errno.
            std::fclose(file_);
            file_ = nullptr;
            closed_ = true;
            return std::unexpected(
                append_errno_detail("Failed to close WAV file", err));
        }
        errno = 0;
        if (std::fclose(file_) != 0) {
            const int err = errno;
            file_ = nullptr;
            closed_ = true;
            return std::unexpected(
                append_errno_detail("Failed to close WAV file", err));
        }
        file_ = nullptr;
    }
    closed_ = true;
    return {};
}

std::expected<void, std::string> WavWriter::write_header()
{
    const uint16_t bits = 24;
    const uint16_t block_align = static_cast<uint16_t>(channels_ * (bits / 8));
    const uint32_t byte_rate =
        static_cast<uint32_t>(sample_rate_) * block_align;
    std::vector<unsigned char> h;
    h.insert(h.end(), {'R', 'I', 'F', 'F'});
    riff_size_offset_ = h.size();
    append_u32(h, 0);
    h.insert(h.end(), {'W', 'A', 'V', 'E'});
    h.insert(h.end(), {'f', 'm', 't', ' '});
    append_u32(h, 16);
    append_u16(h, 1);  // WAVE_FORMAT_PCM
    append_u16(h, static_cast<uint16_t>(channels_));
    append_u32(h, static_cast<uint32_t>(sample_rate_));
    append_u32(h, byte_rate);
    append_u16(h, block_align);
    append_u16(h, bits);
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
                append_errno_detail("Failed to write WAV data", err));
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
                append_errno_detail("Failed to patch WAV header", err));
        }
        errno = 0;
        if (std::fwrite(b, 1, sizeof(b), file_) != sizeof(b)) {
            const int err = errno;
            return std::unexpected(
                append_errno_detail("Failed to patch WAV header", err));
        }
        errno = 0;
        if (std::fseek(file_, 0, SEEK_END) != 0) {
            const int err = errno;
            return std::unexpected(
                append_errno_detail("Failed to patch WAV header", err));
        }
    } else if (sink_kind_ == SinkKind::Memory) {
        if (offset + sizeof(b) > memory_->size()) {
            return std::unexpected("Invalid WAV memory patch offset");
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
