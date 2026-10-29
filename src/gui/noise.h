#pragma once

// THE NOISE — THE BACKGROUND SOUND (architect 2026-10-28). A lamp key, NOISE
// on bare `w` (RedesignButton::IconNoise, app_state.h), lights a bed of sound
// that plays CONTINUOUSLY as A SECOND VOICE mixed under everything the
// playback engine plays — work playback, the A/B audition, the render player,
// the score's soundtrack — and over its silence between plays, AT ONE LEVEL,
// NEVER DUCKED and never paused by any of them; only the lamp, a route drop
// (which darkens it, below), a change of the `noise` key (below) or closing
// the app silences it. The lamp is OFF AT EVERY LAUNCH: no device key holds
// it (his "iPhone requires enabling each time"). THE BED SOUNDS ONLY THROUGH
// THE RUNNING STREAM'S CALLBACK, so the device question is the lamp's too
// (architect 2026-10-28): the LIT PRESS asks the launch press's reopen
// (GuiPlayback::ensure_device_available_for_play, at the `w` case of
// input_key_dispatch.cpp — a refusal cards and still lights the lamp, silent
// as a load in flight is), and THE LAMP FOLLOWS THE NO-AUTO-RECOVER RULE
// (architect 2026-10-28, the iPhone's Background Sounds the precedent: its
// toggle goes off when the route changes): a route drop (a headphone
// connected or taken off) DARKENS the lamp and resets the voice, lit or not,
// at THE FIRST MAIN-THREAD ACT THAT READS THE DROP'S LATCH — a lit LED says
// the noise plays, and on a dead stream it does not. THE RESET'S TWO ROADS
// (2026-10-28, one body, noise_voice_reset_on_dead_stream below):
//   1. THE POLL — gui_main's beside tick() (GuiPlayback::stream_dead,
//      GuiNoise::reset_on_dead_stream), for the press-less drop, the one
//      road that reads the latch without reopening;
//   2. EVERY REOPEN — reopen_stream_if_dead's dead arm, and launch_window's
//      post-publish dead arm (playback_aaudio.cpp), ahead of the
//      close_stream that clears the latch: a press that reaches the device
//      before the tick (on Android input drains ahead of the tick) — a Play
//      press, an audition start, the NOISE key's own lit press, THE CAR'S
//      PLAY at a Bluetooth connect — darkens the lamp there, so a lit lamp
//      never resumes its old sequence on the new stream (at a connect the
//      lamp goes dark, the iPhone's behavior), and a dark lamp's fade tail
//      never plays after the reopen.
// Nothing reopens by itself, the error callback's "nothing recovers by
// itself" whole for the bed as for the music; the next lit press is an
// ordinary one and keeps asking the launch press's device question.
//
// THE SOUND IS A FOLDER UNDER `noise/` BESIDE THE DEVICE CONFIG, named by the
// `noise` device key (device_config_path()'s parent / "noise" / the key's
// value — `noise/rain/` for the first run's `noise=rain`;
// is_noise_folder_name, device_config.h, owns the name's grammar). Present
// plays; AN ABSENT FOLDER IS THE ORDINARY STATE and greys the key with no
// card and no line. Every
// regular file whose name ends `.wav` (lowercase, exact) is a SEGMENT. THE
// ADMITTED SHAPE: PCM16, two channels, at least one frame, every file at THE
// FIRST FILE'S RATE (one rate per folder), each element count passing
// checked_audio_sample_count's allocation policy (wav_io.h, read-only use of
// src/audio_io — score_audio.cpp's road; NO UNFREEZE, the bytes read by the
// GUI-side RIFF walk). An unlistable folder, or a `.wav` out of shape, is ONE
// stderr line naming the folder or the file and the reason, and the whole
// folder unavailable (class 5, advisory). A folder of ONE file is a legal
// folder: the one segment loops onto itself through the crossfade (draw_next,
// noise.cpp). The files are whatever he converts by his own means to the
// admitted shape; on the tablet the folder is the laptop's, mirrored whole by
// the sync script (scripts/warptempo_sync's mirror_noise).
//
// THE SEGMENTS PLAY IN RANDOM ORDER, never the same twice in a row (but for a
// folder of one), joined by an EQUAL-POWER CROSSFADE (√(1−t) and √t,
// a² + b² = 1: the right curve for two uncorrelated noise beds; his "a must")
// of `noise_crossfade_s` whole seconds, a permanent device key (architect
// 2026-10-28 night: the crossfade is a recording's relationship to itself, so
// a later sound may want another value); a lit press starts a fresh
// random segment from its start (his "random start = the segment road, no
// mid-segment offset"). The bed FADES IN over kNoiseFadeInMs at the lit press
// — from when the load lands, the LED lighting at once — and OUT over
// kNoiseFadeOutMs at the dark press, both linear amplitude ramps; a press
// during either fade reverses from the current level, no restart. The two
// fades are SETTLED (architect 2026-10-28, on the glass, after one day as
// tuning keys), the lamp's own and the same for every sound. The level is the
// permanent device key `noise_level` in dB, applied to the files as they are:
// NATURAL LOUDNESS VARIATION KEPT, no per-segment equalizing (his ruling).
//
// THE OUTPUT CLIPS IN-APP: the two voices are summed and every sample clamped
// to [-1, 1] after them (playback_clip_block, playback_common.h — the
// engine's, not this file's).
//
// CLOSED (docs/engineering/closed_questions.md, this file their owner):
// unfreezing src/audio_io for a PCM16 source copy, a lighter audition source,
// a fixed concatenation of the segments, equalizing the segments' loudness, a
// memory ceiling for the bank (GuiNoiseBank's head).

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// THE TWO FADES (architect 2026-10-28, settled on the glass at 200 and 40 ms):
// linear amplitude ramps, the envelope's rate per output frame derived from
// these and the device's rate (noise_mix_block). Both positive, so a fade is
// never a cut.
inline constexpr int kNoiseFadeInMs  = 200;
inline constexpr int kNoiseFadeOutMs = 40;

// THE BANK — IMMUTABLE ONCE PUBLISHED. Every segment's samples, interleaved
// stereo PCM16 HELD AS THE FILE HOLDS THEM, nothing converted (score_audio.h's
// ruling: "better to have speed"), at the folder's one `rate`. Built whole on
// the loader thread and published by ONE atomic pointer store (release) into
// GuiNoiseVoice::bank; read by the audio thread through its seq_cst load;
// freed by GuiNoise::change_folder under its fence (below) or by
// GuiNoise::shutdown, after every engine is down.
//
// THE WHOLE NAMED FOLDER IS RESIDENT FROM THE FIRST LIT PRESS UNTIL THE APP
// CLOSES OR THE `noise` KEY NAMES ANOTHER FOLDER (the planner's call on the
// architect's deferral, architect agreed 2026-10-28; his "whatever's in
// memory is dumped if we change the subfolder name", 2026-10-28). NOTHING
// LIMITS THE SIZE (architect 2026-10-28, closed): docs/HELP.md names the
// sensible ceiling, about 35 minutes, about 400 MB at 48 kHz. The bank is
// read by the audio thread with no lock, so a buffer may be freed only once
// no fill can be reading it: at the app's end, every engine down, or at a key
// change behind change_folder's in_fill fence, which proves it for the one
// bank at once. A rolling window of loaded segments would free buffers under
// a playing audio thread at every segment — a fence per segment for a memory
// saving the tablet does not need. A dark lamp keeps the bank; the next lit
// press plays at once.
struct GuiNoiseBank {
    struct Segment {
        std::unique_ptr<int16_t[]> samples;   // frames * 2, interleaved
        int64_t                    frames = 0;
    };
    int                  rate = 0;
    std::vector<Segment> segments;
};

// THE VOICE — the audio thread's state and the main thread's control words,
// ONE INSTANCE FOR THE APP'S LIFE (GuiNoise owns it; every project's engine
// mixes it, GuiPlayback::attach_noise).
//
// THE CONTROL WORDS, all std::atomic. Written by the main thread and read by
// the audio thread: `bank` (null until the load lands, then the one published
// bank; null again from a key change until the next load lands), `target`
// (the lamp: true lit), `gain` (linear, 10^(noise_level/20)), `crossfade_ms`
// (the `noise_crossfade_s` key in ms, set_crossfade_s's one conversion; read
// at each transition's draw) and `seed` (written at every lit press
// BEFORE `target`'s release store; read when a sequence starts, so the
// target's acquire load orders it). Written by the audio thread and read by
// the main thread: `in_fill` (below).
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
// THE FIRST EXCEPTION — THE MAIN THREAD ON A DEAD STREAM (2026-10-28,
// noise_voice_reset_on_dead_stream; its callers the poll's
// GuiNoise::reset_on_dead_stream and the reopen's reset in
// playback_aaudio.cpp, the head's two roads): on a dead AAudio stream NO FILL
// RUNS — the framework retires the data callback thread BEFORE the error
// callback runs, and the latch is published (release) from that error
// callback — so a main thread that has read the latch (acquire, through
// GuiPlayback::stream_dead or the reopen's own load of `stream_dead`) is the
// ONE thread that can touch these fields until a reopen starts a new
// callback thread; and every reopen is the main thread's own
// (reopen_stream_if_dead, playback_aaudio.cpp), ordered after that write by
// program order and before the new thread by its start. JACK has no latch,
// so the exception never arises on the laptop.
// THE SECOND EXCEPTION — THE MAIN THREAD BEHIND THE IN_FILL FENCE
// (2026-10-28, GuiNoise::change_folder): with `bank` stored null and
// `in_fill` read false (seq_cst both), no fill is inside its body and every
// later fill finds no bank and touches no private field, so the main thread
// may reset them (change_folder's protocol states the proof).
// THE LAMP SURVIVES A PROJECT SWITCH (device state; the planner's call,
// architect agreed 2026-10-28): the bed gaps for the device's reopen (one
// AAudio stream stop and start on the tablet) and resumes where it was, no
// fade.
struct GuiNoiseVoice {
    std::atomic<const GuiNoiseBank*> bank{nullptr};
    std::atomic<bool>                target{false};
    std::atomic<float>               gain{1.0f};
    std::atomic<int32_t>             crossfade_ms{4000};
    std::atomic<uint64_t>            seed{0};
    // THE FILL'S PRESENCE (2026-10-28, the planner's protocol for the live
    // release; change_folder): set TRUE by the audio thread at the fill's
    // head — after the suspended device's return, BEFORE the bank's load —
    // and FALSE at every exit of the fill (a scope guard, every early return
    // included), both seq_cst, as the fill's bank load is. Read only by
    // change_folder's spin, after its seq_cst null store to `bank`: the
    // Dekker pair that proves no fill holds the old bank when it is freed.
    std::atomic<bool>                in_fill{false};

    double   env     = 0.0;
    int      cur     = -1;
    double   pos_cur = 0.0;
    int      nxt     = -1;
    double   pos_nxt = 0.0;
    int64_t  xf_len  = 0;
    uint64_t rng     = 0;
};

// THE AUDIO-THREAD BODY: SUM the noise into `frame_count` output frames that
// already hold the project voice or silence (the stride is
// playback_write_silence's: 1 on JACK's per-port buffers, the channel count on
// AAudio's interleaved one), at the device's `output_rate`. NO ALLOCATION, NO
// LOCK, NO SYSCALL, NO I/O. A suspended device (`output_rate` 0) returns at
// once; every other fill marks `in_fill` and loads `bank` first, and a fill
// with no bank (no load yet, a lit lamp's load in flight — the fade-in begins
// when the load lands — or a key change's release) returns touching no
// private field. A dark, silent voice then returns too — the idle arm, the
// common case: the music path pays four atomic words per fill (in_fill's
// store, the bank's load, target's load, in_fill's clear) and one branch, at
// a fill every ~5 ms, nothing. Called by both backends' callbacks on BOTH
// arms, the silence arm's and the render body's, before the clip
// (playback.cpp, playback_aaudio.cpp).
void noise_mix_block(GuiNoiseVoice& voice, float* const* channel_buffers,
                     int64_t stride, int64_t frame_count, int channel_count,
                     uint32_t output_rate);

// THE VOICE RESET ON A DEAD STREAM, THE ONE BODY (2026-10-28; the head's two
// roads): `target` stored false (release) and the audio-thread-private fields
// to the no-sequence state noise_mix_block's idle arm reads (noise.cpp's
// reset_voice_sequence) — so the next lit press starts a fresh random segment
// with the fade-in from silence. PRECONDITION: THE STREAM IS DEAD, READ
// THROUGH THE LATCH ON THIS THREAD JUST BEFORE THE CALL; no fill runs, and
// the reopen that follows is this thread's own — the first exception at
// GuiNoiseVoice. Idempotent. Two callers: GuiNoise::reset_on_dead_stream (the
// poll) and playback_aaudio.cpp's reset ahead of a dead stream's close (the
// reopen). NOT at every close_stream: a project switch and the shutdown close
// a live stream, and the lamp survives those (the thread note's last
// paragraph).
void noise_voice_reset_on_dead_stream(GuiNoiseVoice& voice);

// THE MAIN THREAD'S OBJECT — ONE FOR THE APP, owned by gui_main (main.cpp),
// constructed after the device config is read and shut down after the project
// loop, every engine down by then. AppState::noise reaches it by pointer.
class GuiNoise {
public:
    GuiNoise() = default;
    ~GuiNoise();
    GuiNoise(const GuiNoise&)            = delete;
    GuiNoise& operator=(const GuiNoise&) = delete;

    // THE PROBE, at gui_main's construction and at every key change
    // (change_folder): the folder is device_config_path().parent_path() /
    // "noise" / `folder_name`, listed; every `.wav` probed under the admitted
    // shape (the head), the list (paths, frames, element counts) kept for the
    // loader and the name kept in folder_name_. THE COMPOSITION IS SAFE by
    // is_last_project_name's argument for projects: the config grammar
    // (is_noise_folder_name, device_config.h) admits exactly one non-empty
    // path component, no `/`, not `.` or `..`, so the path cannot leave
    // `noise/`. No folder, or none of its files a `.wav`: unavailable,
    // SILENT. An unlistable folder or a `.wav` out of shape: one stderr line,
    // unavailable. PRECONDITION: no loader exists (construction, or
    // change_folder's join).
    void probe(const std::string& folder_name);

    // THE KEY'S ENABLED PREDICATE: the probe admitted the folder and no load
    // has failed.
    bool available() const;
    // THE LED: the voice's `target`, true from the lit press — before the
    // load lands.
    bool lit() const;

    // THE CHOKEPOINT — bare `w` and the NOISE key's synthesized chord, two
    // producers, one route. PRECONDITION available(); unavailable, a silent
    // no-op (the greyed key's lift never reaches here; the key is the
    // keyboard's half of that rule). Flips `target`: on the LIT edge the seed
    // (steady_clock ns, nonzero) is stored before `target` (release) and, the
    // bank neither loaded nor loading, THE LOADER starts; on the DARK edge
    // nothing else — the audio thread fades out and ends the sequence. The
    // device question of the lit edge is the `w` case's, ahead of this call
    // (input_key_dispatch.cpp); a lamp a route drop darkened
    // (noise_voice_reset_on_dead_stream, the poll's or a reopen's) or a key
    // change darkened (change_folder) comes back only through this lit edge.
    void toggle();

    // THE PER-TICK POLL (main.cpp's tick, every tick): a load that failed
    // under a lit lamp darkens the lamp and leaves the key grey from then on
    // (available() reads the failure, until a key change names a folder
    // afresh); the roster's per-tick comparator repaints. Nothing else: the
    // dead stream is gui_main's poll beside this call (2026-10-28,
    // reset_on_dead_stream below), because the playback object is there and
    // not here.
    void tick();

    // THE POLL'S VOICE RESET ON A DEAD STREAM (architect 2026-10-28, the
    // no-auto-recover rule at the head), LIT OR NOT: the one body,
    // noise_voice_reset_on_dead_stream, on this object's voice — the lamp
    // dark and the sequence ended, so the next lit press starts a fresh
    // random segment with the fade-in from silence. WITHOUT THE RESET the
    // stale voice would run a fade-out blip at the next Play press's reopen,
    // the mixer finishing the dark edge it never got to run on the dead
    // stream — whether the lamp was lit at the drop or a dark press by hand
    // fell within the 40 ms fade-out just before it (why the poll asks no
    // lamp). PRECONDITION: THE STREAM IS DEAD, READ THROUGH THE LATCH
    // (GuiPlayback::stream_dead true on this thread, just before the call) —
    // the first exception to the voice's one-audio-thread rule, stated at
    // GuiNoiseVoice. ONE CALLER OF THIS METHOD: gui_main's poll beside tick()
    // (main.cpp), every tick the stream stands dead, for the press-less drop;
    // the writes are idempotent, so the repeat costs nothing. A press that
    // reopens before the tick resets through the free function itself, ahead
    // of the close that clears the latch (the head's second road,
    // playback_aaudio.cpp). JACK's stream_dead is always false, so it is
    // never called on the laptop. Nothing is damaged here: the roster's
    // per-tick comparator carries the NOISE key's face edge.
    void reset_on_dead_stream();

    // THE KEY CHANGE, LIVE (architect 2026-10-28: "whatever's in memory is
    // dumped if we change the subfolder name"; the planner's protocol).
    // Called by the settings editor's `noise` arm (apply_device_setting) on
    // the main thread with the new name, while an engine may be running and
    // its callback filling. THE PROTOCOL, in order:
    //   1. `target` stored false (release): the lamp darkens (the roster's
    //      per-tick comparator carries the face edge).
    //   2. The loader, if joinable: request_stop and join (a load in flight
    //      ends within one 8 MB chunk; nothing is published after this line —
    //      the publish is the loader's last act, and it is gone).
    //   3. `bank` stored nullptr (seq_cst).
    //   4. Spin while `in_fill` (seq_cst load) reads true, yielding between
    //      reads. THE DEKKER ARGUMENT: the fill's in_fill store precedes its
    //      bank load in the seq_cst total order, and this thread's null store
    //      precedes its in_fill load; so either that fill's bank load saw the
    //      null (and it touched nothing) or this load sees in_fill true and
    //      waits. When in_fill reads false, the fill that may have held the
    //      old bank has exited (its clear is seq_cst, so the exit
    //      happens-before this read), and EVERY LATER FILL's bank load is
    //      after this read in the total order and sees the null. Bound: one
    //      fill, a few ms at most; on a dead or suspended stream no fill runs
    //      and in_fill is false at once.
    //   5. The audio-thread-private fields reset to no-sequence
    //      (reset_voice_sequence, noise.cpp) — the second exception at
    //      GuiNoiseVoice, step 4's wait its proof: no fill is inside its
    //      body, and every later one returns before reading them until a new
    //      bank lands, whose publish orders these writes before the next fill
    //      that reads them.
    //   6. bank_ freed (the memory dumped), `status_` stored Idle (a prior
    //      Failed does not stick to the new folder), the file list cleared,
    //      then probe(folder_name): the next lit press loads the new folder.
    // A folder the probe refuses or cannot find leaves available() false: the
    // key greys, no card (the absent-folder rule), the config still holding
    // the name he typed. BACKEND-NEUTRAL: AAudio's data callback and JACK's
    // process callback are each one thread, the one fill this fence waits on.
    // A DARK-LAMP COMMIT takes the same road — step 1 a no-op, the bank freed
    // all the same (his ruling: the memory is dumped on a name change, lit or
    // not). A SAME-NAME COMMIT never reaches here: the editor's arm answers
    // it unchanged first.
    void change_folder(const std::string& folder_name);

    // THE TWO DEVICE KEYS, LIVE: the level's linear gain read by the audio
    // thread at its next fill, the crossfade read at the next transition's
    // draw. Seeded by gui_main from the config, written by the settings
    // editor's two arms (apply_device_setting, the typed line's and the value
    // list's one body). THE CROSSFADE TAKES THE KEY'S SECONDS and this setter
    // is the ONE conversion to the voice's milliseconds (the voice's lengths,
    // the two fades among them, are ms).
    void set_level_db(int db);
    void set_crossfade_s(int seconds);

    // THE VOICE every project's engine mixes (GuiPlayback::attach_noise).
    GuiNoiseVoice* voice() { return &voice_; }

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

    GuiNoiseVoice     voice_;
    // Written by probe() on the main thread while no loader exists.
    std::vector<File> files_;
    int               rate_      = 0;
    bool              available_ = false;
    // THE KEY'S NAME, as probe() was last given it: the stderr line's path
    // and the folder a key change replaces.
    std::string       folder_name_;
    // THE LOAD'S STATE: Idle until the first lit press (and again from a key
    // change), Loading while the worker reads, Ready (released after the
    // bank's publish) or Failed.
    std::atomic<int>  status_{kIdle};
    // THE BANK'S OWNER: set by the worker before its publish, freed by
    // change_folder behind its fence or by shutdown() after the join.
    std::unique_ptr<GuiNoiseBank> bank_;
    std::jthread      worker_;
};
