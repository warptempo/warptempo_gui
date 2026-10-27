#include "score_audio.h"

#include "wav_io.h"   // wav_probe, checked_audio_sample_count — called, never changed

#include <algorithm>
#include <bit>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <system_error>
#include <utility>

// THE SAMPLES ARE READ AS THE FILE HOLDS THEM: a WAV's PCM16 is
// little-endian, and so is every host this program runs on (the laptop's
// x86-64, the tablet's arm64), so the payload's bytes are the int16_t words
// with no swap.
static_assert(std::endian::native == std::endian::little,
              "the score's soundtrack is read as little-endian PCM16");

namespace {

constexpr const char* kAudioWav = "audio.wav";

// The one stderr line every refusal and every failure prints (the head):
// the file, the reason, the road the viewer takes instead.
void say_page_road(const std::filesystem::path& file, const std::string& why) {
    std::fprintf(stderr,
                 "warptempo_gui: No score soundtrack from '%s': %s; the "
                 "viewer turns the pages\n",
                 file.string().c_str(), why.c_str());
}

uint32_t le32(const unsigned char* b) {
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16) |
           (static_cast<uint32_t>(b[3]) << 24);
}

struct FileCloser {
    void operator()(std::FILE* f) const { if (f) std::fclose(f); }
};

}  // namespace

GuiScoreAudio::~GuiScoreAudio() { shutdown(); }

void GuiScoreAudio::shutdown() {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    worker_ = std::jthread{};
    data_.reset();
    frames_ = 0;
    rate_   = 0;
    status_.store(kAbsent, std::memory_order_relaxed);
}

GuiScoreAudio::State GuiScoreAudio::state() const {
    switch (status_.load(std::memory_order_acquire)) {
        case kLoading: return State::Loading;
        case kReady:   return State::Ready;
        default:       return State::Absent;
    }
}

void GuiScoreAudio::start(const std::filesystem::path& folder,
                          int64_t pages_rate) {
    shutdown();
    const std::filesystem::path file = folder / kAudioWav;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        say_page_road(file, "there is no such file");
        return;
    }
    // THE PROBE, on this thread: a header read, so the face is decided at the
    // folder's read and the rate is known while the payload loads.
    const auto info = wav_probe(file.string());
    if (!info) {
        say_page_road(file, info.error());
        return;
    }
    if (info->format != WavSampleFormat::Pcm16) {
        say_page_road(file, "it is not 16-bit PCM");
        return;
    }
    if (info->channels != 2) {
        say_page_road(file, "it is not stereo");
        return;
    }
    if (info->sample_rate <= 0 || info->frames <= 0) {
        say_page_road(file, "it holds no samples");
        return;
    }
    if (static_cast<int64_t>(info->sample_rate) != pages_rate) {
        say_page_road(file, "its rate " + std::to_string(info->sample_rate) +
                                " is not pages.txt's " +
                                std::to_string(pages_rate));
        return;
    }
    const auto elements = checked_audio_sample_count(info->frames, 2);
    if (!elements) {
        say_page_road(file, elements.error());
        return;
    }
    frames_ = info->frames;
    rate_   = info->sample_rate;
    status_.store(kLoading, std::memory_order_relaxed);
    worker_ = std::jthread(
        [this, file, n = *elements](std::stop_token stop) {
            load(stop, file, n);
        });
}

void GuiScoreAudio::load(std::stop_token stop, std::filesystem::path file,
                         std::size_t elements) {
    const auto fail = [&](const std::string& why) {
        say_page_road(file, why);
        status_.store(kFailed, std::memory_order_release);
    };
    std::unique_ptr<std::FILE, FileCloser> f(
        std::fopen(file.string().c_str(), "rb"));
    if (!f) {
        fail(append_errno_detail("could not open it", errno));
        return;
    }
    // THE RIFF WALK TO `data` (ffmpeg writes `fmt `, a `LIST`, then `data`):
    // the 12-byte RIFF/WAVE header, then each chunk's 8-byte header, a chunk
    // that is not `data` skipped by its size, word-aligned. The probe has
    // already read the same container.
    unsigned char head[12];
    if (std::fread(head, 1, 12, f.get()) != 12 ||
        std::memcmp(head, "RIFF", 4) != 0 ||
        std::memcmp(head + 8, "WAVE", 4) != 0) {
        fail("not a RIFF/WAVE file");
        return;
    }
    uint32_t data_size = 0;
    for (;;) {
        unsigned char chunk[8];
        if (std::fread(chunk, 1, 8, f.get()) != 8) {
            fail("no data chunk");
            return;
        }
        const uint32_t size = le32(chunk + 4);
        if (std::memcmp(chunk, "data", 4) == 0) {
            data_size = size;
            break;
        }
        const long skip = static_cast<long>(size) + static_cast<long>(size & 1u);
        if (std::fseek(f.get(), skip, SEEK_CUR) != 0) {
            fail("a chunk runs past the file's end");
            return;
        }
    }
    if (static_cast<uint64_t>(data_size) < elements * sizeof(int16_t)) {
        fail("its data chunk is shorter than its frames");
        return;
    }
    // THE ALLOCATION, uninitialised (the read fills every element); a host
    // that cannot give it is the one line and the page road.
    std::unique_ptr<int16_t[]> data(new (std::nothrow) int16_t[elements]);
    if (!data) {
        fail("there is no memory for it");
        return;
    }
    // ONE READ OF THE PAYLOAD, in 8 MB chunks so a restart or the teardown
    // waits at most one chunk.
    constexpr std::size_t kChunk = std::size_t{1} << 22;   // elements
    std::size_t at = 0;
    while (at < elements) {
        if (stop.stop_requested()) return;
        const std::size_t n = std::min(kChunk, elements - at);
        if (std::fread(data.get() + at, sizeof(int16_t), n, f.get()) != n) {
            fail("could not read it whole");
            return;
        }
        at += n;
    }
    data_ = std::move(data);
    status_.store(kReady, std::memory_order_release);
}
