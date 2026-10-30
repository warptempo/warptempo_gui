#include "noise.h"

#include "device_config.h"   // device_config_path — the folder's parent
#include "wav_io.h"          // wav_probe, checked_audio_sample_count — called,
                             // never changed

#include <algorithm>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <system_error>
#include <utility>

// THE SAMPLES ARE READ AS THE FILE HOLDS THEM: a WAV's PCM16 is little-endian,
// and so is the host this program runs on (the tablet's arm64), so the
// payload's bytes are the int16_t words with no swap
// (score_audio.cpp's own assertion).
static_assert(std::endian::native == std::endian::little,
              "the noise's segments are read as little-endian PCM16");

namespace {

// The one stderr line every refusal and every failure prints (noise.h's
// head): the folder or the file, the reason, the key's answer.
void say_no_noise(const std::filesystem::path& file, const std::string& why) {
    std::fprintf(stderr,
                 "warptempo_gui: No noise from '%s': %s; the NOISE key stays "
                 "dark\n",
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

// THE FILL'S EXIT MARK (noise.h, GuiNoiseVoice::in_fill): clears the flag,
// seq_cst, at every exit of noise_mix_block past its head, every early return
// included.
struct InFillGuard {
    std::atomic<bool>& flag;
    ~InFillGuard() { flag.store(false, std::memory_order_seq_cst); }
};

// THE XORSHIFT64 STEP (Marsaglia's 13 / 7 / 17), the audio thread's one
// generator: no allocation, no state outside the voice.
inline uint64_t xorshift64(uint64_t& x) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x;
}

// THE SEED'S NONZERO STAND-IN: xorshift's state must never be zero (it would
// stay zero), so a zero seed is replaced by this odd constant (the golden
// ratio's 64-bit fraction).
constexpr uint64_t kNoiseSeedFallback = 0x9E3779B97F4A7C15ull;

// A DRAW THAT IS NOT `cur`: uniform over the other count − 1 segments, the
// one skip past `cur`. WITH ONE SEGMENT THE ONE SEGMENT FOLLOWS ITSELF — there
// is no other, and a folder of one is a legal folder (noise.h's head): the
// returned `cur` fades in from its start under its own tail, so the one
// recording loops onto itself through the crossfade.
inline int draw_next(uint64_t& rng, int count, int cur) {
    if (count <= 1) return cur;
    const int r = static_cast<int>(xorshift64(rng) %
                                   static_cast<uint64_t>(count - 1));
    return r >= cur ? r + 1 : r;
}

// ONE STEREO FRAME AT A FRACTIONAL POSITION — the render body's own linear
// interpolation (playback_common.cpp), a PCM16 word read as its value over
// 32768. THE ONE BELT: both indices are clamped to the segment's last frame
// (the last-sample fallback), so no position can index past a buffer. A
// segment shorter than the crossfade gets no further guard: the crossfade is
// capped at half the outgoing segment (the transition below), and his
// folders hold minutes-long recordings.
inline void read_frame(const GuiNoiseBank::Segment& seg, double pos,
                       double out[2]) {
    const int64_t last = seg.frames - 1;
    const double  fl   = std::floor(pos);
    int64_t i = static_cast<int64_t>(fl);
    if (i < 0) i = 0;
    if (i > last) i = last;
    const int64_t j = i < last ? i + 1 : last;
    const double frac = pos - fl;
    const int16_t* a = seg.samples.get() + static_cast<size_t>(i) * 2;
    const int16_t* b = seg.samples.get() + static_cast<size_t>(j) * 2;
    for (int c = 0; c < 2; ++c) {
        const double va = a[c] / 32768.0;
        const double vb = b[c] / 32768.0;
        out[c] = (1.0 - frac) * va + frac * vb;
    }
}

// THE NO-SEQUENCE STATE of the audio-thread-private fields — `env` 0, `cur`
// and `nxt` -1, both positions 0, `xf_len` 0 — written on the main thread.
// Two callers, each stating its own proof that no fill runs:
// noise_voice_reset_on_dead_stream (the dead-stream exception at
// GuiNoiseVoice) and GuiNoise::change_folder (the in_fill fence).
void reset_voice_sequence(GuiNoiseVoice& v) {
    v.env     = 0.0;
    v.cur     = -1;
    v.pos_cur = 0.0;
    v.nxt     = -1;
    v.pos_nxt = 0.0;
    v.xf_len  = 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// The audio thread
// ---------------------------------------------------------------------------

void noise_mix_block(GuiNoiseVoice& v, float* const* channel_buffers,
                     int64_t stride, int64_t frame_count, int channel_count,
                     uint32_t output_rate) {
    // A SUSPENDED DEVICE: the buffer is already silence and the positions
    // hold, the render body's own answer.
    if (output_rate == 0) return;
    // THE FILL'S PRESENCE, then THE BANK (GuiNoise::change_folder's fence,
    // noise.h): in_fill stored true BEFORE the bank's load, both seq_cst, and
    // cleared at every exit below by the guard.
    v.in_fill.store(true, std::memory_order_seq_cst);
    const InFillGuard guard{v.in_fill};
    const GuiNoiseBank* bank = v.bank.load(std::memory_order_seq_cst);
    // NO BANK — no load yet, a lit lamp's load in flight (nothing to play,
    // and the fade-in begins when the load lands — his "the LED lights at
    // once, the fade-in begins when the load lands"), or a key change's
    // release: the fill returns TOUCHING NO PRIVATE FIELD, the property
    // change_folder's reset rests on. (A sequence never stands without a
    // bank: `cur` is drawn only below this line, and change_folder resets it
    // with the bank.)
    if (bank == nullptr) return;
    const bool target = v.target.load(std::memory_order_acquire);
    // THE IDLE ARM — the lamp dark and the bed silent, the common case.
    if (!target && v.env == 0.0 && v.cur < 0) return;
    const int count = static_cast<int>(bank->segments.size());
    if (count <= 0) return;

    // START A SEQUENCE at a lit lamp with none standing: the generator seeded
    // from the press's seed (ordered by `target`'s acquire above), a segment
    // drawn uniformly, from its start; the envelope as it stands, which is 0:
    // a sequence ends only where the fade-out reached silence (below), so a
    // press during a fade-out finds `cur` standing and reverses the fade
    // instead of starting here.
    if (target && v.cur < 0) {
        uint64_t s = v.seed.load(std::memory_order_relaxed);
        if (s == 0) s = kNoiseSeedFallback;
        v.rng     = s;
        v.cur     = static_cast<int>(xorshift64(v.rng) %
                                     static_cast<uint64_t>(count));
        v.pos_cur = 0.0;
        v.nxt     = -1;
        v.pos_nxt = 0.0;
        v.xf_len  = 0;
    }
    if (v.cur < 0) return;

    // THE FILL'S CONSTANTS: the noise's own increment (the render body's rate
    // rescaling — the bank's rate over the stream's, 48000/48000 on the
    // tablet), the two envelope steps per output frame from the two fade
    // constants (noise.h, both positive), and the level, read once per fill,
    // so a settings change to the level applies at the next fill.
    const double rate      = static_cast<double>(output_rate);
    const double increment = static_cast<double>(bank->rate) / rate;
    const double up        = 1000.0 / (kNoiseFadeInMs * rate);
    const double down      = 1000.0 / (kNoiseFadeOutMs * rate);
    const double gain      = v.gain.load(std::memory_order_relaxed);

    for (int64_t n = 0; n < frame_count; ++n) {
        // THE ENVELOPE moves toward the lamp at its own rate, clamped to
        // [0, 1]: a press during either fade reverses from the current level.
        if (target) {
            v.env += up;
            if (v.env > 1.0) v.env = 1.0;
        } else {
            v.env -= down;
            if (v.env < 0.0) v.env = 0.0;
        }

        // THE SWAP: the outgoing segment ran out under a standing crossfade,
        // so the incoming one is the sequence's now.
        if (v.nxt >= 0 &&
            v.pos_cur >= static_cast<double>(
                             bank->segments[static_cast<size_t>(v.cur)].frames)) {
            v.cur     = v.nxt;
            v.pos_cur = v.pos_nxt;
            v.nxt     = -1;
            v.pos_nxt = 0.0;
        }
        const GuiNoiseBank::Segment& sc =
            bank->segments[static_cast<size_t>(v.cur)];

        // THE TRANSITION: within the crossfade of the outgoing segment's end,
        // the next segment is drawn (never the same, but for a folder of one)
        // and fades in from its start. The length is `crossfade_ms` in the
        // bank's frames, at most half the outgoing segment.
        if (v.nxt < 0) {
            const int64_t ms = v.crossfade_ms.load(std::memory_order_relaxed);
            int64_t xf = ms * static_cast<int64_t>(bank->rate) / 1000;
            if (xf > sc.frames / 2) xf = sc.frames / 2;
            if (v.pos_cur >= static_cast<double>(sc.frames - xf)) {
                v.nxt     = draw_next(v.rng, count, v.cur);
                v.pos_nxt = 0.0;
                v.xf_len  = xf;
            }
        }

        // THE SAMPLE: the outgoing segment alone, or THE EQUAL-POWER
        // CROSSFADE of the two — √(1−t) and √t, t the outgoing segment's
        // progress through the crossfade.
        double unit[2];
        read_frame(sc, v.pos_cur, unit);
        if (v.nxt >= 0) {
            double t = 1.0;
            if (v.xf_len > 0) {
                t = (v.pos_cur - static_cast<double>(sc.frames - v.xf_len)) /
                    static_cast<double>(v.xf_len);
                if (t < 0.0) t = 0.0;
                if (t > 1.0) t = 1.0;
            }
            double incoming[2];
            read_frame(bank->segments[static_cast<size_t>(v.nxt)], v.pos_nxt,
                       incoming);
            const double a = std::sqrt(1.0 - t);
            const double b = std::sqrt(t);
            unit[0] = a * unit[0] + b * incoming[0];
            unit[1] = a * unit[1] + b * incoming[1];
        }

        // SUMMED UNDER THE PROJECT VOICE (or into the silence): the level and
        // the envelope, nothing else. Stereo sources on a stereo device; a
        // channel past the second reads the second.
        const double k = gain * v.env;
        for (int c = 0; c < channel_count; ++c) {
            float* dst = channel_buffers[c] + n * stride;
            *dst += static_cast<float>(unit[c < 2 ? c : 1] * k);
        }

        v.pos_cur += increment;
        if (v.nxt >= 0) v.pos_nxt += increment;

        // THE SEQUENCE ENDS when a dark lamp's fade reaches silence, so the
        // next lit press starts a fresh random segment from its start.
        if (!target && v.env == 0.0) {
            v.cur     = -1;
            v.nxt     = -1;
            v.pos_cur = 0.0;
            v.pos_nxt = 0.0;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// The main thread
// ---------------------------------------------------------------------------

GuiNoise::~GuiNoise() { shutdown(); }

void GuiNoise::shutdown() {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    worker_ = std::jthread{};
    voice_.target.store(false, std::memory_order_relaxed);
    voice_.bank.store(nullptr, std::memory_order_relaxed);
    bank_.reset();
}

void GuiNoise::probe(const std::string& folder_name) {
    files_.clear();
    rate_        = 0;
    available_   = false;
    folder_name_ = folder_name;
    const std::filesystem::path config = device_config_path();
    if (config.empty()) return;
    // The one component under `noise/` (noise.h, probe's note: the config
    // grammar keeps the name a single non-empty component).
    const std::filesystem::path folder =
        config.parent_path() / "noise" / folder_name_;
    std::error_code ec;
    // NO FOLDER IS THE ORDINARY STATE (noise.h's head): silent.
    if (!std::filesystem::is_directory(folder, ec)) return;
    std::vector<std::filesystem::path> wavs;
    std::filesystem::directory_iterator it(folder, ec);
    if (ec) {
        say_no_noise(folder,
                     "the folder could not be listed: " + ec.message());
        return;
    }
    for (const std::filesystem::directory_entry& e : it) {
        std::error_code fec;
        if (!e.is_regular_file(fec)) continue;
        const std::string name = e.path().filename().string();
        if (name.size() <= 4 ||
            name.compare(name.size() - 4, 4, ".wav") != 0)
            continue;
        wavs.push_back(e.path());
    }
    // A FOLDER WITH NO `.wav` is absent too: silent.
    if (wavs.empty()) return;
    std::sort(wavs.begin(), wavs.end(),
              [](const std::filesystem::path& a,
                 const std::filesystem::path& b) {
                  return a.filename().string() < b.filename().string();
              });
    std::vector<File> files;
    int rate = 0;
    for (const std::filesystem::path& p : wavs) {
        const auto info = wav_probe(p.string());
        if (!info) { say_no_noise(p, info.error()); return; }
        if (info->format != WavSampleFormat::Pcm16) {
            say_no_noise(p, "it is not 16-bit PCM");
            return;
        }
        if (info->channels != 2) {
            say_no_noise(p, "it is not stereo");
            return;
        }
        if (info->sample_rate <= 0 || info->frames <= 0) {
            say_no_noise(p, "it holds no samples");
            return;
        }
        if (rate == 0) {
            rate = info->sample_rate;
        } else if (info->sample_rate != rate) {
            say_no_noise(p, "its rate " + std::to_string(info->sample_rate) +
                                " is not the folder's " +
                                std::to_string(rate));
            return;
        }
        const auto elements = checked_audio_sample_count(info->frames, 2);
        if (!elements) { say_no_noise(p, elements.error()); return; }
        files.push_back(File{p, info->frames, *elements});
    }
    files_     = std::move(files);
    rate_      = rate;
    available_ = true;
}

bool GuiNoise::available() const {
    return available_ &&
           status_.load(std::memory_order_acquire) != kFailed;
}

bool GuiNoise::lit() const {
    return voice_.target.load(std::memory_order_relaxed);
}

void GuiNoise::toggle() {
    if (!available()) return;
    if (lit()) {
        // THE DARK EDGE: the audio thread fades out and ends the sequence.
        voice_.target.store(false, std::memory_order_release);
        return;
    }
    // THE LIT EDGE: the seed first, then the lamp (release), so a sequence
    // the audio thread starts on this press draws from this press's seed.
    uint64_t seed = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    if (seed == 0) seed = kNoiseSeedFallback;
    voice_.seed.store(seed, std::memory_order_relaxed);
    voice_.target.store(true, std::memory_order_release);
    // THE LOADER, ONCE PER FOLDER: the bank neither loaded nor loading.
    if (status_.load(std::memory_order_acquire) == kIdle) {
        status_.store(kLoading, std::memory_order_relaxed);
        worker_ = std::jthread(
            [this](std::stop_token stop) { load(stop); });
    }
}

void GuiNoise::tick() {
    if (lit() && status_.load(std::memory_order_acquire) == kFailed) {
        // The lamp darkens; available() reads the failure from here on.
        voice_.target.store(false, std::memory_order_release);
    }
}

void noise_voice_reset_on_dead_stream(GuiNoiseVoice& v) {
    // The lamp first; then the voice's private fields, written on this thread
    // under the dead-stream exception (GuiNoiseVoice's thread note): no fill
    // runs until a reopen, which is this thread's own and comes after.
    v.target.store(false, std::memory_order_release);
    reset_voice_sequence(v);
}

void GuiNoise::reset_on_dead_stream() {
    noise_voice_reset_on_dead_stream(voice_);
}

void GuiNoise::change_folder(const std::string& folder_name) {
    // 1. The lamp darkens.
    voice_.target.store(false, std::memory_order_release);
    // 2. The loader gone: nothing is published after this line.
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    worker_ = std::jthread{};
    // 3. The bank unpublished, then 4. THE FENCE (noise.h, change_folder's
    // Dekker argument): wait out the one fill that may hold the old bank.
    voice_.bank.store(nullptr, std::memory_order_seq_cst);
    while (voice_.in_fill.load(std::memory_order_seq_cst))
        std::this_thread::yield();
    // 5. The private fields to no-sequence, behind the fence (the second
    // exception at GuiNoiseVoice).
    reset_voice_sequence(voice_);
    // 6. The memory dumped, the state afresh, the new folder probed.
    bank_.reset();
    status_.store(kIdle, std::memory_order_release);
    files_.clear();
    probe(folder_name);
}

void GuiNoise::set_level_db(int db) {
    voice_.gain.store(static_cast<float>(std::pow(10.0, db / 20.0)),
                      std::memory_order_relaxed);
}

void GuiNoise::set_crossfade_s(int seconds) {
    voice_.crossfade_ms.store(static_cast<int32_t>(seconds) * 1000,
                              std::memory_order_relaxed);
}

void GuiNoise::load(std::stop_token stop) {
    auto bank = std::make_unique<GuiNoiseBank>();
    bank->rate = rate_;
    bank->segments.reserve(files_.size());
    const auto fail = [&](const std::filesystem::path& file,
                          const std::string& why) {
        say_no_noise(file, why);
        status_.store(kFailed, std::memory_order_release);
    };
    for (const File& file : files_) {
        if (stop.stop_requested()) return;
        std::unique_ptr<std::FILE, FileCloser> f(
            std::fopen(file.path.string().c_str(), "rb"));
        if (!f) {
            fail(file.path, append_errno_detail("could not open it", errno));
            return;
        }
        // THE RIFF WALK TO `data` (score_audio.cpp's own: a converter writes
        // `fmt `, perhaps a `LIST`, then `data`): the 12-byte RIFF/WAVE
        // header, then each chunk's 8-byte header, a chunk that is not `data`
        // skipped by its size, word-aligned. The probe has already read the
        // same container.
        unsigned char head[12];
        if (std::fread(head, 1, 12, f.get()) != 12 ||
            std::memcmp(head, "RIFF", 4) != 0 ||
            std::memcmp(head + 8, "WAVE", 4) != 0) {
            fail(file.path, "not a RIFF/WAVE file");
            return;
        }
        uint32_t data_size = 0;
        for (;;) {
            unsigned char chunk[8];
            if (std::fread(chunk, 1, 8, f.get()) != 8) {
                fail(file.path, "no data chunk");
                return;
            }
            const uint32_t size = le32(chunk + 4);
            if (std::memcmp(chunk, "data", 4) == 0) {
                data_size = size;
                break;
            }
            const long skip =
                static_cast<long>(size) + static_cast<long>(size & 1u);
            if (std::fseek(f.get(), skip, SEEK_CUR) != 0) {
                fail(file.path, "a chunk runs past the file's end");
                return;
            }
        }
        if (static_cast<uint64_t>(data_size) <
            file.elements * sizeof(int16_t)) {
            fail(file.path, "its data chunk is shorter than its frames");
            return;
        }
        // THE ALLOCATION, uninitialized (the read fills every element); a host
        // that cannot give it is the one line and the dark key.
        std::unique_ptr<int16_t[]> data(new (std::nothrow)
                                            int16_t[file.elements]);
        if (!data) {
            fail(file.path, "there is no memory for it");
            return;
        }
        // THE PAYLOAD in 8 MB chunks, so the app's close or a key change
        // waits at most one.
        constexpr std::size_t kChunk = std::size_t{1} << 22;   // elements
        std::size_t at = 0;
        while (at < file.elements) {
            if (stop.stop_requested()) return;
            const std::size_t n = std::min(kChunk, file.elements - at);
            if (std::fread(data.get() + at, sizeof(int16_t), n, f.get()) != n) {
                fail(file.path, "could not read it whole");
                return;
            }
            at += n;
        }
        GuiNoiseBank::Segment seg;
        seg.samples = std::move(data);
        seg.frames  = file.frames;
        bank->segments.push_back(std::move(seg));
    }
    // THE ONE PUBLISH: the owner kept for change_folder and shutdown, the
    // pointer released to the audio thread, then the state.
    bank_ = std::move(bank);
    voice_.bank.store(bank_.get(), std::memory_order_release);
    status_.store(kReady, std::memory_order_release);
}
