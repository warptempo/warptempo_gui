#pragma once

// THE NOISE — THE BACKGROUND SOUND (architect 2026-10-28; THE LEVEL IS ITS
// ONE SWITCH since 2026-11-05). A bed of sound plays CONTINUOUSLY as A SECOND
// VOICE mixed under everything the playback engine plays — work playback,
// the A/B audition, the render player, the score's soundtrack — and over its
// silence between plays, AT ONE LEVEL, NEVER DUCKED and never paused by any
// of them. THE LEVEL IS THE SWITCH (architect 2026-11-05, the two level
// cases, mock s46_m1: "ableton has no sliders that also act as buttons";
// "the only off switch is -inf, and that should actually shut it off so
// reduce footprint"): the `noise_level` device key — a decimal dB or -inf,
// THE LEVEL GRAMMAR, device_config.h — set by the band's NOISE case
// (RedesignButton::IconNoiseLevel, app_state.h: a slider, dragged relative
// on the square-law travel, a long press setting 0 dB) and by the settings
// editor's typed line; ABOVE -inf THE BED PLAYS, AT -inf IT IS OFF. There is
// no lamp and no toggle (the NOISE lamp on bare `w`, 2026-10-28 to
// 2026-11-05, RETIRED with the cases; his "iPhone requires enabling each
// time" reversed on the same word): THE BED PLAYS FROM THE LAUNCH at the
// persisted level — gui_main seats set_level_db after the probe, and an
// audible level with a folder there lights the voice and starts the loader
// before the first project opens; the engine is per project, so before a
// project's stream runs nothing sounds, and the bed begins when the first
// stream does (the fade-in once the load has landed). THE BED SOUNDS ONLY
// THROUGH THE RUNNING STREAM'S CALLBACK, and nothing asks the device question
// for it: the music's reopen is the bed's (below). Only the level at -inf, a
// change of the `noise` key to a folder that is absent or refused (below) or
// closing the app silences it for good; A ROUTE DROP PAUSES IT (next).
//
// A ROUTE DROP STOPS THE VOICE WITH ITS STREAM, AND THE BED RESUMES AT THE
// NEXT REOPEN (architect 2026-11-05, the route drop's reading, confirmed by
// the planner's "Decisions" item; reversing 2026-10-28's darkening of the
// lamp, the iPhone's Background Sounds precedent, which went with the lamp):
// a route drop (a headphone connected or taken off) kills the AAudio stream,
// and on a dead stream nothing sounds; THE LEVEL IS UNTOUCHED and so is the
// voice's `target` — noise_voice_reset_on_dead_stream (below) resets the
// SEQUENCE ALONE, so the next stream the music opens finds an audible,
// sequence-less voice and starts a fresh random segment with the fade-in.
// THE RESET'S TWO ROADS (2026-10-28, one body, noise_voice_reset_on_dead_
// stream below):
//   1. THE POLL — gui_main's beside tick() (GuiPlayback::stream_dead,
//      GuiNoise::reset_on_dead_stream), for the press-less drop, the one
//      road that reads the latch without reopening;
//   2. EVERY REOPEN — reopen_stream_if_dead's dead arm, and launch_window's
//      post-publish dead arm (playback_aaudio.cpp), ahead of the
//      close_stream that clears the latch: a press that reaches the device
//      before the tick (on Android input drains ahead of the tick) — a Play
//      press, an audition start, THE CAR'S PLAY at a Bluetooth connect —
//      resets the sequence there, so the old sequence never resumes
//      mid-segment on the new stream and a fade tail never plays after the
//      reopen: a fresh segment, the fade-in.
// THE NO-AUTO-RECOVER RULE HOLDS: nothing reopens by itself, the error
// callback's "nothing recovers by itself" whole for the bed as for the music
// — the bed is silent from the drop until the next press that reopens the
// stream (Play, an audition, the car's Play at a connect), and that reopen
// is the bed's resume.
//
// THE SOUND IS A FOLDER UNDER `noise/` BESIDE THE DEVICE CONFIG, named by the
// `noise` device key (device_config_path()'s parent / "noise" / the key's
// value — `noise/rain/` for the first run's `noise=rain`;
// is_noise_folder_name, device_config.h, owns the name's grammar). Present
// plays; AN ABSENT FOLDER IS THE ORDINARY STATE and greys the NOISE case
// (its fill and its number in the disabled ink, the slider inert) with no
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
// a later sound may want another value); a rise from -inf starts a fresh
// random segment from its start (his "random start = the segment road, no
// mid-segment offset"). The bed FADES IN over kNoiseFadeInMs at the rise
// above -inf — from when the load lands — and OUT over kNoiseFadeOutMs at
// the fall to -inf, both linear amplitude ramps; a crossing during either
// fade reverses from the current level, no restart (a drag wobbling across
// -inf only darkens and relights the voice, nothing freed and nothing
// reloaded — a 400 MB bank is not reloaded per finger wobble; the release
// is the lift's, GuiNoise::release_if_silent below). The two fades are
// SETTLED (architect 2026-10-28, on the glass, after one day as tuning
// keys), the switch's own and the same for every sound. The level is the
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
#include <limits>
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
// THE WHOLE NAMED FOLDER IS RESIDENT FROM THE FIRST RISE ABOVE -inf UNTIL
// THE APP CLOSES, THE `noise` KEY NAMES ANOTHER FOLDER, OR THE LEVEL IS LEFT
// AT -inf (the planner's call on the architect's deferral, architect agreed
// 2026-10-28; his "whatever's in memory is dumped if we change the subfolder
// name", 2026-10-28; THE -inf RELEASE 2026-11-05: "the only off switch is
// -inf, and that should actually shut it off so reduce footprint" — the
// bank freed at the slider's LIFT on -inf, or at a typed -inf commit,
// GuiNoise::release_if_silent, never mid-drag). NOTHING LIMITS THE SIZE
// (architect 2026-10-28, closed): docs/HELP.md names the sensible ceiling,
// about 35 minutes, about 400 MB at 48 kHz. The bank is read by the audio
// thread with no lock, so a buffer may be freed only once no fill can be
// reading it: at the app's end, every engine down, or behind the in_fill
// fence (release_bank, the body change_folder and release_if_silent share),
// which proves it for the one bank at once. A rolling window of loaded
// segments would free buffers under a playing audio thread at every segment
// — a fence per segment for a memory saving the tablet does not need. A
// drag that crosses -inf and comes back keeps the bank (the fade reverses,
// nothing reloads); a lift on -inf frees it, and the next rise reloads.
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
// bank; null again from a key change or a -inf release until the next load
// lands), `target` (THE SWITCH: true while the level stands above -inf with
// a folder available, GuiNoise::apply_switch), `gain` (linear,
// 10^(noise_level/20), 0 at -inf), `crossfade_ms` (the `noise_crossfade_s`
// key in ms, set_crossfade_s's one conversion; read at each transition's
// draw) and `seed` (written at every rise above -inf BEFORE `target`'s
// release store; read when a sequence starts, so the target's acquire load
// orders it). Written by the audio thread and read by the main thread:
// `in_fill` (below).
//
// THE AUDIO-THREAD-PRIVATE FIELDS — `env` (the fade envelope, 0..1), `cur`
// (the playing segment, -1 no sequence), `pos_cur` (its fractional frame, in
// the bank's frames), `nxt` (the segment fading in, -1 no crossfade
// standing), `pos_nxt`, `xf_len` (this transition's crossfade in the bank's
// frames) and `rng` (xorshift64) — are touched by ONE AUDIO THREAD AT A TIME.
// THE ENGINE IS PER PROJECT AND THE VOICE PER APP: at a project switch the old
// engine's shutdown quiesces and closes its device (AAudioStream_close,
// which returns only after its callback thread has left)
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
// program order and before the new thread by its start.
// THE SECOND EXCEPTION — THE MAIN THREAD BEHIND THE IN_FILL FENCE
// (2026-10-28, GuiNoise::release_bank, the body change_folder and
// release_if_silent share): with `bank` stored null and `in_fill` read false
// (seq_cst both), no fill is inside its body and every later fill finds no
// bank and touches no private field, so the main thread may reset them
// (release_bank's protocol states the proof).
// THE BED SURVIVES A PROJECT SWITCH (device state; the planner's call,
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
// playback_write_silence's: the channel count on AAudio's interleaved
// buffer), at the device's `output_rate`. NO ALLOCATION, NO
// LOCK, NO SYSCALL, NO I/O. A suspended device (`output_rate` 0) returns at
// once; every other fill marks `in_fill` and loads `bank` first, and a fill
// with no bank (no load yet, an audible level's load in flight — the fade-in
// begins when the load lands — or a key change's or a -inf lift's release)
// returns touching no private field. A switched-off, silent voice then
// returns too — the idle arm, the common case on a fresh install's -inf:
// the music path pays four atomic words per fill (in_fill's
// store, the bank's load, target's load, in_fill's clear) and one branch, at
// a fill every ~5 ms, nothing. Called by the device's callback on BOTH arms,
// the silence arm's and the render body's, before the clip
// (playback_aaudio.cpp).
void noise_mix_block(GuiNoiseVoice& voice, float* const* channel_buffers,
                     int64_t stride, int64_t frame_count, int channel_count,
                     uint32_t output_rate);

// THE VOICE RESET ON A DEAD STREAM, THE ONE BODY (2026-10-28; the head's two
// roads; THE SEQUENCE ALONE since 2026-11-05, the route drop's reading): the
// audio-thread-private fields to the no-sequence state noise_mix_block's
// idle arm reads (noise.cpp's reset_voice_sequence) — `target` and the level
// UNTOUCHED, so an audible voice starts a fresh random segment with the
// fade-in from silence on the next stream the music opens, and a switched-
// off voice's fade tail never plays after the reopen. (Until 2026-11-05 it
// also stored `target` false, the lamp's darkening at a drop; the lamp is
// gone and the bed resumes at the next reopen by his ruling.) PRECONDITION:
// THE STREAM IS DEAD, READ THROUGH THE LATCH ON THIS THREAD JUST BEFORE THE
// CALL; no fill runs, and the reopen that follows is this thread's own — the
// first exception at GuiNoiseVoice. Idempotent. Two callers:
// GuiNoise::reset_on_dead_stream (the poll) and playback_aaudio.cpp's reset
// ahead of a dead stream's close (the reopen). NOT at every close_stream: a
// project switch and the shutdown close a live stream, and the bed survives
// those (the thread note's last paragraph).
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

    // THE CASE'S ENABLED PREDICATE (the NOISE case greys on false, its fill
    // and number in the disabled ink; redesign_button_enabled's arm): the
    // probe admitted the folder and no load has failed.
    bool available() const;
    // THE SWITCH AS THE VOICE READS IT: `target`, true while the level stands
    // above -inf with a folder available (apply_switch) — before the load
    // lands. A reader of the voice's own word, no painter's (the case paints
    // its level, never this bit).
    bool lit() const;

    // THE LEVEL, LIVE AND THE ONE SWITCH (architect 2026-11-05; the head):
    // `db` a value of THE LEVEL GRAMMAR (device_config.h) — a decimal dB in
    // [-60.0, 0.0] or kLevelDbOff. Stores the gain (10^(db/20), 0 at -inf)
    // for the audio thread's next fill, then applies the switch
    // (apply_switch): ABOVE -inf with available() true and the voice dark,
    // THE RISE — the seed (steady_clock ns, nonzero) stored before `target`
    // (release) and, the bank neither loaded nor loading, THE LOADER started
    // once per folder; AT -inf with the voice lit, THE FALL — `target`
    // false, the audio thread's 40 ms fade-out ending the sequence. Nothing
    // else: no free, no reload — a drag that crosses -inf and comes back only
    // darkens and relights the voice (the fade reverses from where it is),
    // and the bank is freed at the lift alone (release_if_silent). THE
    // CALLERS: gui_main after the probe (the launch's seat: the bed from the
    // launch), the NOISE case's every drag motion and its long press
    // (GuiInputHandler::apply_level_case, input_pointer.cpp) and the settings
    // editor's typed `noise_level` arm (apply_device_setting). Unavailable,
    // the gain is stored and the switch stays off: a later change_folder to
    // a folder that is there lights it at this level.
    void set_level_db(double db);
    // THE -inf RELEASE (architect 2026-11-05, "that should actually shut it
    // off so reduce footprint"): with the level at -inf, the bank is FREED
    // behind the fence (release_bank, change_folder's steps 1..6 with the
    // SAME folder — the file list kept, `status_` Idle, so the next rise
    // above -inf reloads: the load lands and the fade-in runs). Above -inf,
    // or with nothing loaded or loading, a no-op. THE CALLERS: the NOISE
    // case's lift and its button-lost end (GuiInputHandler::settle_level_
    // case) and the typed -inf commit (apply_device_setting) — never a drag
    // motion (a 400 MB bank is not reloaded per finger wobble, the head). On
    // the main thread while an engine may be filling: the fence proves no
    // fill holds the bank (release_bank's protocol); the fade-out the fall
    // began is cut where the fence lands, the voice silent at -inf anyway
    // (gain 0).
    void release_if_silent();

    // THE PER-TICK POLL (main.cpp's tick, every tick): a load that failed
    // under an audible level darkens the voice and leaves the case grey from
    // then on (available() reads the failure, until a key change names a
    // folder afresh); the roster's per-tick comparator repaints. Nothing
    // else: the dead stream is gui_main's poll beside this call (2026-10-28,
    // reset_on_dead_stream below), because the playback object is there and
    // not here.
    void tick();

    // THE POLL'S VOICE RESET ON A DEAD STREAM (architect 2026-10-28, the
    // no-auto-recover rule at the head; the sequence alone since
    // 2026-11-05), AUDIBLE OR NOT: the one body,
    // noise_voice_reset_on_dead_stream, on this object's voice — the sequence
    // ended, `target` and the level untouched, so the next stream the music
    // opens starts a fresh random segment with the fade-in from silence (the
    // bed's resume at the reopen, the head). WITHOUT THE RESET the stale
    // voice would resume mid-segment, or run a fade-out blip, at the next
    // Play press's reopen — the mixer finishing what it never got to run on
    // the dead stream — whether the level was audible at the drop or a fall
    // to -inf fell within the 40 ms fade-out just before it (why the poll
    // asks no level). PRECONDITION: THE STREAM IS DEAD, READ THROUGH THE
    // LATCH (GuiPlayback::stream_dead true on this thread, just before the
    // call) — the first exception to the voice's one-audio-thread rule,
    // stated at GuiNoiseVoice. ONE CALLER OF THIS METHOD: gui_main's poll
    // beside tick() (main.cpp), every tick the stream stands dead, for the
    // press-less drop; the writes are idempotent, so the repeat costs
    // nothing. A press that reopens before the tick resets through the free
    // function itself, ahead of the close that clears the latch (the head's
    // second road, playback_aaudio.cpp). Nothing is damaged here: the case
    // paints its level, which a drop does not move.
    void reset_on_dead_stream();

    // THE KEY CHANGE, LIVE (architect 2026-10-28: "whatever's in memory is
    // dumped if we change the subfolder name"; the planner's protocol).
    // Called by the settings editor's `noise` arm (apply_device_setting) on
    // the main thread with the new name, while an engine may be running and
    // its callback filling: release_bank (the protocol below), the file list
    // cleared, probe(folder_name), then apply_switch — AN AUDIBLE LEVEL
    // RELIGHTS AT ONCE over a folder the probe admits (the level is the
    // switch, 2026-11-05: the new folder loads and fades in when it lands);
    // a folder the probe refuses or cannot find leaves available() false:
    // the case greys, no card (the absent-folder rule), the config still
    // holding the name he typed, and the next admitted folder lights again.
    // A SAME-NAME COMMIT never reaches here: the editor's arm answers it
    // unchanged first.
    // THE RELEASE PROTOCOL (release_bank, noise.cpp — ONE BODY for this
    // method and for release_if_silent, the -inf lift's), in order:
    //   1. `target` stored false (release): the voice darkens.
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
    //      Failed does not stick; the next rise loads afresh).
    // AAudio's data callback is one thread, the one fill this fence waits
    // on. A COMMIT AT -inf takes the same road — step 1 a no-op, the bank
    // freed all the same (his ruling: the memory is dumped on a name change,
    // audible or not).
    void change_folder(const std::string& folder_name);

    // THE CROSSFADE, LIVE: read at the next transition's draw. Seeded by
    // gui_main from the config, written by the settings editor's arm
    // (apply_device_setting, the typed line's and the value list's one
    // body). IT TAKES THE KEY'S SECONDS and this setter is the ONE conversion
    // to the voice's milliseconds (the voice's lengths, the two fades among
    // them, are ms).
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
    // THE SWITCH'S ONE BODY (set_level_db, change_folder): the voice lit on
    // an audible level over an available folder (the seed, `target`, the
    // loader once per folder), dark otherwise; a no-op where it already
    // stands as the level says.
    void apply_switch();
    // THE RELEASE, steps 1..6 of the protocol at change_folder: the voice
    // dark, the loader joined, the bank unpublished behind the fence and
    // freed, the state Idle. The file list and the folder's name stay (the
    // -inf lift's reload needs no second probe); change_folder clears and
    // re-probes after it.
    void release_bank();

    GuiNoiseVoice     voice_;
    // THE LEVEL AS SET (set_level_db's last `db`, kLevelDbOff until the
    // launch's seat): the switch's input, read by apply_switch and
    // release_if_silent on the main thread.
    double            level_db_ = -std::numeric_limits<double>::infinity();
    // Written by probe() on the main thread while no loader exists.
    std::vector<File> files_;
    int               rate_      = 0;
    bool              available_ = false;
    // THE KEY'S NAME, as probe() was last given it: the stderr line's path
    // and the folder a key change replaces.
    std::string       folder_name_;
    // THE LOAD'S STATE: Idle until the first rise above -inf (and again from a
    // key change or a -inf release), Loading while the worker reads, Ready
    // (released after the bank's publish) or Failed.
    std::atomic<int>  status_{kIdle};
    // THE BANK'S OWNER: set by the worker before its publish, freed by
    // release_bank behind its fence or by shutdown() after the join.
    std::unique_ptr<GuiNoiseBank> bank_;
    std::jthread      worker_;
};
