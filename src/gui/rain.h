#pragma once

// THE RAIN — THE BACKGROUND SOUND (architect 2026-10-28). A lamp key, RAIN on
// bare `w` (RedesignButton::IconRain, app_state.h), lights a bed of rain that
// plays CONTINUOUSLY as A SECOND VOICE mixed under everything the playback
// engine plays — work playback, the A/B audition, the render player, the
// score's soundtrack — and over its silence between plays, AT ONE LEVEL,
// NEVER DUCKED and never paused by any of them; only the lamp, a route drop
// (which darkens it, below) or closing the app silences it. The lamp is OFF
// AT EVERY LAUNCH: no device key holds it (his "iPhone requires enabling each
// time"). THE BED SOUNDS ONLY THROUGH THE
// RUNNING STREAM'S CALLBACK, so the device question is the lamp's too
// (architect 2026-10-28): the LIT PRESS asks the launch press's reopen
// (GuiPlayback::ensure_device_available_for_play, at the `w` case of
// input_key_dispatch.cpp — a refusal cards and still lights the lamp, silent
// as a load in flight is), and THE LAMP FOLLOWS THE NO-AUTO-RECOVER RULE
// (architect 2026-10-28, the iPhone's Background Sounds the precedent: its
// toggle goes off when the route changes): a route drop under a lit lamp (a
// headphone connected or taken off) DARKENS the lamp at the next tick and
// silences the voice (gui_main's poll beside tick(), GuiPlayback::stream_dead,
// GuiRain::darken_on_dead_stream) — a lit LED says the rain plays, and on a
// dead stream it does not. Nothing reopens by itself, the error callback's
// "nothing recovers by itself" whole for the bed as for the music; the next
// lit press is an ordinary one and keeps asking the launch press's device
// question. RAIN is committed as THE ONE SOUND; the folder's name is the hook
// for a later sound choice, which is not built.
//
// THE SOUND IS A FOLDER BESIDE THE DEVICE CONFIG, `sounds/rain/`
// (device_config_path()'s parent — the `tiles/` precedent, materials.h:
// present plays, ABSENT IS THE ORDINARY STATE and greys the key with no card
// and no line). Every regular file whose name ends `.wav` (lowercase, exact)
// is a SEGMENT. THE ADMITTED SHAPE: PCM16, two channels, at least one frame,
// every file at THE FIRST FILE'S RATE (one rate per folder), each element
// count passing checked_audio_sample_count's allocation policy (wav_io.h,
// read-only use of src/audio_io — score_audio.cpp's road; NO UNFREEZE, the
// bytes read by the GUI-side RIFF walk). A `.wav` out of shape is ONE stderr
// line naming the file and the reason, and the whole folder unavailable
// (class 5, advisory). His folder: 17 Apple "Rain" segments, 48 kHz, 70–170 s
// each, about 35 minutes and 400 MB as PCM16, written by the sync script's
// `setup` through ffmpeg (scripts/warptempo_sync).
//
// THE SEGMENTS PLAY IN RANDOM ORDER, never the same twice in a row, joined by
// an EQUAL-POWER CROSSFADE (√(1−t) and √t, a² + b² = 1: the right curve for
// two uncorrelated noise beds; his "a must") of `rain_crossfade_ms`, a tuning
// device key; a lit press starts a fresh random segment from its start (his
// "random start = the segment road, no mid-segment offset"). The bed FADES IN
// over kRainFadeInMs at the lit press — from when the load lands, the LED
// lighting at once — and OUT over kRainFadeOutMs at the dark press, both
// linear amplitude ramps; a press during either fade reverses from the
// current level, no restart. The two fades are SETTLED (architect 2026-10-28,
// on the glass, after one day as tuning keys); the crossfade alone is still a
// tuning key and becomes a constexpr here when he settles it (the tuning
// rule). The level is the permanent device key
// `rain_level` in dB, applied to the files as they are: NATURAL LOUDNESS
// VARIATION KEPT, no per-segment equalizing (his ruling).
//
// THE OUTPUT CLIPS IN-APP: the two voices are summed and every sample clamped
// to [-1, 1] after them (playback_clip_block, playback_common.h — the
// engine's, not this file's).
//
// CLOSED (docs/engineering/closed_questions.md, this file their owner):
// unfreezing src/audio_io for a PCM16 source copy, a lighter audition source,
// a fixed concatenation of the segments, equalizing the segments' loudness.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// THE TWO FADES (architect 2026-10-28, settled on the glass at 200 and 40 ms):
// linear amplitude ramps, the envelope's rate per output frame derived from
// these and the device's rate (rain_mix_block). Both positive, so a fade is
// never a cut.
inline constexpr int kRainFadeInMs  = 200;
inline constexpr int kRainFadeOutMs = 40;

// THE BANK — IMMUTABLE ONCE PUBLISHED. Every segment's samples, interleaved
// stereo PCM16 HELD AS THE FILE HOLDS THEM, nothing converted (score_audio.h's
// ruling: "better to have speed"), at the folder's one `rate`. Built whole on
// the loader thread and published by ONE atomic pointer store (release) into
// GuiRainVoice::bank; read by the audio thread through its acquire load;
// freed only by GuiRain::shutdown, after every engine is down.
//
// ALL OF IT RESIDENT FROM THE FIRST LIT PRESS UNTIL THE APP CLOSES (the
// planner's call on the architect's deferral, architect agreed 2026-10-28;
// about 400 MB for his folder):
// the bank is read by the audio thread with no lock, so a buffer may be freed
// only once no callback can be reading it, and the only moment that is
// provable without a fence on every engine is the app's end. A rolling window
// of loaded segments would free buffers under a playing audio thread — or need
// a second publication protocol to prove it did not — for a memory saving the
// tablet does not need. A dark lamp keeps the bank; the next lit press plays
// at once.
struct GuiRainBank {
    struct Segment {
        std::unique_ptr<int16_t[]> samples;   // frames * 2, interleaved
        int64_t                    frames = 0;
    };
    int                  rate = 0;
    std::vector<Segment> segments;
};

// THE VOICE — the audio thread's state and the main thread's control words,
// ONE INSTANCE FOR THE APP'S LIFE (GuiRain owns it; every project's engine
// mixes it, GuiPlayback::attach_rain).
//
// THE CONTROL WORDS, all std::atomic, written by the main thread and read by
// the audio thread: `bank` (null until the load lands, then the one published
// bank), `target` (the lamp: true lit), `gain` (linear, 10^(rain_level/20)),
// `crossfade_ms` (read at each transition's draw) and `seed` (written at every
// lit press BEFORE `target`'s release store; read when a sequence starts, so
// the target's acquire load orders it).
//
// THE AUDIO-THREAD-PRIVATE FIELDS — `env` (the fade envelope, 0..1), `cur`
// (the playing segment, -1 no sequence), `pos_cur` (its fractional frame, in
// the bank's frames), `nxt` (the segment fading in, -1 no crossfade
// standing), `pos_nxt`, `xf_len` (this transition's crossfade in the bank's
// frames) and `rng` (xorshift64) — are touched by ONE AUDIO THREAD AT A TIME.
// THE ENGINE IS PER PROJECT AND THE VOICE PER APP: at a project switch the old
// engine's shutdown quiesces and closes its device (jack_deactivate,
// AAudioStream_close — each returns only after its callback thread has left)
// before the new engine's init starts a new callback thread, so exactly one
// audio thread ever touches these fields, the hand-off ordered by the device
// close and the thread start.
// THE ONE EXCEPTION — THE MAIN THREAD ON A DEAD STREAM (2026-10-28,
// GuiRain::darken_on_dead_stream): on a dead AAudio stream NO FILL RUNS — the
// framework retires the data callback thread BEFORE the error callback runs,
// and the latch is published (release) from that error callback — so a main
// thread that has read the latch (acquire, through GuiPlayback::stream_dead)
// is the ONE thread that can touch these fields until a reopen starts a new
// callback thread; and every reopen is the main thread's own
// (reopen_stream_if_dead, playback_aaudio.cpp), ordered after that write by
// program order and before the new thread by its start. JACK has no latch,
// so the exception never arises on the laptop.
// THE LAMP SURVIVES A PROJECT SWITCH (device state; the planner's call,
// architect agreed 2026-10-28): the bed gaps for the device's reopen (one
// AAudio stream stop and start on the tablet) and resumes where it was, no
// fade.
struct GuiRainVoice {
    std::atomic<const GuiRainBank*> bank{nullptr};
    std::atomic<bool>               target{false};
    std::atomic<float>              gain{1.0f};
    std::atomic<int32_t>            crossfade_ms{4000};
    std::atomic<uint64_t>           seed{0};

    double   env     = 0.0;
    int      cur     = -1;
    double   pos_cur = 0.0;
    int      nxt     = -1;
    double   pos_nxt = 0.0;
    int64_t  xf_len  = 0;
    uint64_t rng     = 0;
};

// THE AUDIO-THREAD BODY: SUM the rain into `frame_count` output frames that
// already hold the project voice or silence (the stride is
// playback_write_silence's: 1 on JACK's per-port buffers, the channel count on
// AAudio's interleaved one), at the device's `output_rate`. NO ALLOCATION, NO
// LOCK, NO SYSCALL, NO I/O. A suspended device (`output_rate` 0) and a dark,
// silent voice return at once — the idle arm, the common case, one branch
// for the music path; a lit lamp with the load in flight returns too (the
// fade-in begins when the load lands). Called by both backends' callbacks on
// BOTH arms, the silence arm's and the render body's, before the clip
// (playback.cpp, playback_aaudio.cpp).
void rain_mix_block(GuiRainVoice& voice, float* const* channel_buffers,
                    int64_t stride, int64_t frame_count, int channel_count,
                    uint32_t output_rate);

// THE MAIN THREAD'S OBJECT — ONE FOR THE APP, owned by gui_main (main.cpp),
// constructed after the device config is read and shut down after the project
// loop, every engine down by then. AppState::rain reaches it by pointer.
class GuiRain {
public:
    GuiRain() = default;
    ~GuiRain();
    GuiRain(const GuiRain&)            = delete;
    GuiRain& operator=(const GuiRain&) = delete;

    // THE PROBE, ONCE, at gui_main's construction: the folder listed, every
    // `.wav` probed under the admitted shape (the head), the list (paths,
    // frames, element counts) kept for the loader. No folder, or none of its
    // files a `.wav`: unavailable, SILENT. A `.wav` out of shape: one stderr
    // line, unavailable.
    void probe();

    // THE KEY'S ENABLED PREDICATE: the probe admitted the folder and no load
    // has failed.
    bool available() const;
    // THE LED: the voice's `target`, true from the lit press — before the
    // load lands.
    bool lit() const;

    // THE CHOKEPOINT — bare `w` and the RAIN key's synthesized chord, two
    // producers, one route. PRECONDITION available(); unavailable, a silent
    // no-op (the greyed key's lift never reaches here; the key is the
    // keyboard's half of that rule). Flips `target`: on the LIT edge the seed
    // (steady_clock ns, nonzero) is stored before `target` (release) and, the
    // bank neither loaded nor loading, THE LOADER starts; on the DARK edge
    // nothing else — the audio thread fades out and ends the sequence. The
    // device question of the lit edge is the `w` case's, ahead of this call
    // (input_key_dispatch.cpp); a lamp the poll darkened at a route drop
    // (darken_on_dead_stream) comes back only through this lit edge.
    void toggle();

    // THE PER-TICK POLL (main.cpp's tick, every tick): a load that failed
    // under a lit lamp darkens the lamp and leaves the key grey from then on
    // (available() reads the failure); the roster's per-tick comparator
    // repaints. Nothing else: the dead stream under a lit lamp is gui_main's
    // poll beside this call (2026-10-28, darken_on_dead_stream below),
    // because the playback object is there and not here.
    void tick();

    // THE LAMP DARKENED AT A ROUTE DROP (architect 2026-10-28, the
    // no-auto-recover rule at the head): `target` stored false (release) and
    // the audio-thread-private fields reset to the no-sequence state
    // rain_mix_block's idle arm reads — `env` 0, `cur` and `nxt` -1, both
    // positions 0, `xf_len` 0 — so the next lit press starts a fresh random
    // segment with the fade-in from silence. WITHOUT THE RESET the stale
    // voice would run a 40 ms fade-out blip at the next Play press's reopen,
    // the mixer finishing the dark edge it never got to run on the dead
    // stream. PRECONDITION: THE STREAM IS DEAD, READ THROUGH THE LATCH
    // (GuiPlayback::stream_dead true on this thread, just before the call) —
    // the one exception to the voice's one-audio-thread rule, stated at
    // GuiRainVoice. ONE CALLER: gui_main's poll beside tick() (main.cpp),
    // under a lit lamp. JACK's stream_dead is always false, so it is never
    // called on the laptop. Nothing is damaged here: the roster's per-tick
    // comparator carries the RAIN key's face edge.
    void darken_on_dead_stream();

    // THE TWO DEVICE KEYS, LIVE: the level's linear gain read by the audio
    // thread at its next fill, the crossfade read at the next transition's
    // draw. Seeded by gui_main from the config, written by the settings
    // editor's two arms (commit_device_setting).
    void set_level_db(int db);
    void set_crossfade_ms(int ms);

    // THE VOICE every project's engine mixes (GuiPlayback::attach_rain).
    GuiRainVoice* voice() { return &voice_; }

    // THE STOP: the loader asked to stop and joined, the bank unpublished and
    // freed. PRECONDITION: no engine's callback can run (gui_main calls it
    // after the project loop). Idempotent; the destructor calls it again.
    void shutdown();

private:
    enum : int { kIdle = 0, kLoading = 1, kReady = 2, kFailed = 3 };
    struct File {
        std::filesystem::path path;
        int64_t               frames   = 0;
        std::size_t           elements = 0;
    };
    // The loader's body: per file the RIFF walk to `data`, the allocation, the
    // read in 8 MB chunks (the stop token asked between them), then the one
    // publish; any failure one stderr line, nothing published, Failed.
    void load(std::stop_token stop);

    GuiRainVoice      voice_;
    // Written by probe() on the main thread before any loader exists.
    std::vector<File> files_;
    int               rate_      = 0;
    bool              available_ = false;
    // THE LOAD'S STATE: Idle until the first lit press, Loading while the
    // worker reads, Ready (released after the bank's publish) or Failed.
    std::atomic<int>  status_{kIdle};
    // THE BANK'S OWNER: set by the worker before its publish, freed by
    // shutdown() after the join.
    std::unique_ptr<GuiRainBank> bank_;
    std::jthread      worker_;
};
