#pragma once

// THE SCORE'S SOUNDTRACK (architect 2026-10-26 night: "the score player is
// essentially a separate media player") — `<score folder>/audio.wav`, the
// video's own soundtrack tools/score_extract.py writes beside the pages
// (its AUDIO_WAV), which pages.txt was cut from, so the page holding a frame
// of it is exact by construction. The score viewer plays it (score_viewer.h);
// nothing else does.
//
// A SEPARATE OUTCOME FROM THE PAGES: the score folder's read (score_pages.h)
// is the trigger — GuiScoreAudio::start at the project load and at a typed
// `score_path=` commit, through score_folder_changed (score_viewer.h) — and a
// folder whose pages read well but whose soundtrack does not is a legal score,
// the viewer's PAGE ROAD (its PREV / NEXT face). THE ADMITTED SHAPE: PCM16, two
// channels, a positive rate EQUAL TO pages.txt's `# rate` (the frames the
// pages name are the soundtrack's), at least one frame, its element count
// passing checked_audio_sample_count's allocation policy (wav_io.h, read-only
// use of src/audio_io). Anything else — the file absent, unreadable, 24-bit,
// mono, another rate — is ONE stderr line naming the file and the reason and
// the page road: class 5, advisory, never a load failure, never a card.
//
// HELD AS IT LIES IN THE FILE, PCM16 interleaved stereo (ruling 6: "the audio
// doesn't have to be great; better to have speed" — 265 MB for K.550, 458 MB
// for the Jupiter, float32 would double them), NOTHING CONVERTED: the engine
// reads it through GuiBoundAudio's PCM16 format at its own rate (playback.h).
// No codec library: the probe is the in-tree wav_probe, the bytes one read of
// the `data` chunk's payload after a GUI-side RIFF chunk walk.
//
// THE LOAD IS A WORKER: one std::jthread per start, reading the payload while
// the main thread goes on, publishing through ONE ATOMIC STATE — Loading until
// the read lands, then Ready (release; the main thread's acquire read of it is
// what admits its read of the samples) or Failed. The main thread never blocks
// on it but at a restart and at the project's teardown, which JOIN it (a
// payload read in flight stops at its next chunk). THE MAIN THREAD'S FACE:
// state() — Absent (nothing to play, Failed included), Loading, Ready —
// present() for the viewer's two faces (loaded or loading: the media player's)
// and ready() for its PLAY key (grey until the flag is up: its act would be a
// no-op). Kept for the project's life; the next project's AppState holds its
// own.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>

class GuiScoreAudio {
public:
    enum class State { Absent, Loading, Ready };

    GuiScoreAudio() = default;
    ~GuiScoreAudio();
    GuiScoreAudio(const GuiScoreAudio&)            = delete;
    GuiScoreAudio& operator=(const GuiScoreAudio&) = delete;

    // THE START, for the score folder `folder` whose pages.txt names frames at
    // `pages_rate`: the old load joined and dropped first (shutdown), then the
    // probe on this thread (the admitted shape above; a refusal its one stderr
    // line and Absent), then the worker. PRECONDITION: the engine is not bound
    // to the old samples — only the standing score viewer binds them, and
    // neither of the two callers (the project load, the settings editor's
    // `score_path=` commit) runs while it stands.
    void start(const std::filesystem::path& folder, int64_t pages_rate);
    // THE STOP: the worker asked to stop and joined, the samples freed,
    // Absent. Idempotent; the project's teardown calls it beside the history
    // prefetch's (main.cpp, after playback.shutdown, so the engine holds no
    // pointer into the samples), and the destructor again.
    void shutdown();

    State state() const;
    bool  present() const { return state() != State::Absent; }
    bool  ready() const { return state() == State::Ready; }
    // The probe's shape, valid while present(): the frame count and the rate
    // (pages.txt's own, by the admitted shape).
    int64_t frames() const { return frames_; }
    int     rate() const { return rate_; }
    // The interleaved stereo samples, frames() * 2 of them; null unless
    // ready().
    const int16_t* samples() const { return ready() ? data_.get() : nullptr; }

private:
    enum : int { kAbsent = 0, kLoading = 1, kReady = 2, kFailed = 3 };
    // The worker's body: the RIFF walk to `data`, the allocation, the read in
    // chunks (the stop token asked between them), the publish.
    void load(std::stop_token stop, std::filesystem::path file,
              std::size_t elements);

    std::jthread                worker_;
    std::atomic<int>            status_{kAbsent};
    // Written by the worker before its release of Ready, read by the main
    // thread only after its acquire of Ready (or after the join).
    std::unique_ptr<int16_t[]>  data_;
    // Written by start() on the main thread before the worker exists.
    int64_t                     frames_ = 0;
    int                         rate_   = 0;
};
