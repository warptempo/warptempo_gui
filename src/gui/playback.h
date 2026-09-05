#pragma once
#include "av_sync_stats.h"   // GuiAudioStats (the AV sync panel's audio half)

#include <cstdint>
#include <memory>

// Audio playback engine. Owns ONE audio device and drives itself from that
// device's callback thread. The device half is per backend — a JACK client on
// Linux (playback.cpp), an AAudio output stream on Android
// (playback_aaudio.cpp), exactly one of the two compiled into a given binary —
// while the render body, the cursor predictor and every main-thread helper
// below are shared verbatim (playback_common.{h,cpp}). EVERY CLAUSE IN THIS
// HEADER IS THE CONTRACT ON BOTH BACKENDS; where a clause is proved
// differently by each device, it names both proofs.
// The sample buffer is borrowed, not owned: the caller must keep the
// pointer passed to init() valid until shutdown() returns, and must call
// stop()+shutdown() before tearing down the source.
//
// One bind API, and the domain offset travels with the buffer: init() and
// rebind_buffer() take the bound buffer's domain offset — the displayed-domain
// coordinate that buffer frame 0 represents (0 for source.wav, and the
// trim-mapped full-target-frame anchor for the target buffer) — and
// store it in the same moment as the buffer pointer, under the same
// refuse-while-playing conditions, so the pair can never be observed
// inconsistent. Every public position surface (play()'s bounds, cursor(),
// domain_begin()/domain_end()) speaks the bound buffer's displayed-domain
// coordinates; no call site hand-translates, so position reporting can never
// skew across a rebind — a reader racing a rebind sees the currently-bound
// buffer's own domain, never a cursor from one binding combined with an
// offset from another. Internal audio-callback math stays buffer-local; the
// translation happens only at this public API boundary.
//
// Thread model:
//   - Audio thread (the device's callback thread: JACK's process thread,
//     AAudio's data callback): reads and writes the cursor via relaxed
//     atomics, lowers the session word's playing bit at a natural end by a
//     generation-qualified compare-exchange (playback_common.h). No
//     allocation, no I/O, no locks.
//   - Main thread: calls init/play/stop/shutdown; takes is_playing() per tick
//     and cursor() / cursor_precise() per paint (the design note's one
//     observation, below).
//
// PLAYBACK PLAYS THE SOURCE AT THE SOURCE'S OWN RATE. The read is fractional —
// the device may ask for frames at a rate the source is not recorded at, and
// the render body's one increment rescales for exactly that (playback_common.h)
// — but there is no SPEED factor on top of it any more: the variable-speed
// machinery and its `playback_speed` settings key retired together
// (architect 2026-08-27, the architect running 1.0 everywhere in his one live
// project). What it did was naive sample-rate rescaling — speed 0.7 read every
// 0.7th source sample, shifting pitch along with tempo.
//
// Predictor design note
// ---------------------
// The cursor predictor is a free-running linear extrapolator anchored at
// (anchor_sample, anchor_ns) and re-anchored only at events of acceptable
// visible discontinuity, never inside the audio callback. The set of
// resync events: playhead jumps via move_playhead, zoom in/out via the shared
// apply_zoom_change helper, the resize zoom-out reclamp,
// follow-mode off-to-on,
// follow-scroll auto-shift, horizontal pan via scroll_viewport
// (the alt+wheel stepped pan and PageUp/PageDown), and viewport recenter via
// center_viewport_on_playhead (C key). There is no loop-wrap resync event any
// more: LOOPING IS GONE (architect 2026-07-30, all audition looping removed),
// so the read position only ever advances and the free-running predictor has no
// backward jump to miss — playback runs [start, end) once and stops at the
// natural end.
//
// THE LINE IS THE RAW PREDICTOR AND IT LEADS THE SOUND (architect 2026-09-03,
// after a blind comparison at his own rig against a fully compensated build).
// play() anchors at the PUBLISH INSTANT, the position is read at the bare
// `now`, and no latency figure of either device enters either term — so the
// painted line lights at `press + D` (the compositor's paint-to-light delay)
// while the sound starts at `press + phase + L_audio` (the audio thread's
// pickup phase, 0 to one period, then the device's output latency), and the
// two part by `L_audio − D + phase`, differently on every launch. His words
// for it: "two continuous streams that we sort of happen to pick up here and
// there — sometimes they match, sometimes they don't, and that's exactly what
// they are." A compensated predictor — the anchor moved onto the audio
// thread's first fill, the reported output latency added at every anchor
// under a per-epoch re-anchor, a self-measured display lead added to the
// position read, and a natural-end hold keeping the line alive until the last
// queued frame had been heard — was built on 2026-09-01/02 and rolled back
// whole on 2026-09-03. It is in git history; nothing in this tree reads a
// latency figure any more. THE ONE PIECE THAT STAYED is the RESYNC'S ANCHOR
// (below): it takes a stamp rather than the main thread's `now`, which is
// accuracy about the same raw line, not compensation.
//
// THE RESYNC IS EVENT-DRIVEN, AND THE REASON IS THE TOOL'S PLAY LENGTHS
// (architect 2026-09-02, the truthfulness deep dive's item C; the periodic
// cadence proposed there does NOT land). Every resync is an EVENT — SIXTEEN
// call sites at this writing, in classes: the zooms (apply_zoom_change,
// apply_strip_drag_zoom's final frame, apply_zoom_to_start), the DISCRETE
// pan (scroll_viewport with continuous=false — a drag pans without one and
// re-anchors once at its end), the centring jump, follow's page and the
// on-edges of the follow and centred toggles, the map-change re-land
// (reseat_playhead_to), the resize whose level moved, and the pointer ends
// (the nav drag's and the overview drag's release and force-end, the touch
// hard end). Grep `resync_predictor` and re-count; never inherit this number.
// EVERY RESYNC ANCHORS ON THE AUDIO THREAD'S CYCLE STAMP — (the read cursor,
// the instant that cursor's frame enters the output port) — and not on the
// main thread's `now`, which sat anywhere inside the period before that fill
// and re-rolled that whole phase into the line at every pan end, page turn or
// `c`. So a resync's step is the accumulated DRIFT alone.
// A session that is launched and left alone — the architect's own `c`,
// Space, no pan, and the `y` pin, which derives its camera per frame and
// resyncs nothing (the statement is at derive_centered_viewport) — runs the
// whole play on steady_clock against the DAC's crystal with no re-anchor at
// all. THAT IS AFFORDABLE BECAUSE THE DRIFT BETWEEN EVENTS IS BELOW THE
// FRAME GRID AT THIS TOOL'S PLAY LENGTHS: the clocks part at 10–100 ppm, so
// 0.6–6 ms per minute, and this is a spot-check instrument for segments of
// up to ~30 s (the trim/render design, the memory-vs-disk preview cutoff),
// where that is 0.6–3 ms — half a pixel to two and a half at the working
// zoom (`c` is 1.25 ms/px), and well inside the ±10 ms band in which a
// picture/sound offset is invisible. THE BAND IS WHAT CARRIES THE RULING,
// not the pixel count: the drift is imperceptible because the ear and the
// eye do not resolve it, and a couple of pixels of line at a spot-check
// zoom is simply the same statement in the other unit. The gain from a
// cadence exists only on the multi-minute plays the tool is not for, while
// its cost is a per-play risk of exactly the "imperceptible" class plus
// machinery. The event resync is kept because it is FREE, not because a play
// without one would be wrong: its step is the drift alone and it lands in the
// same frame as the reflow that masks it (the criterion below).
//
// THE OLD PERCEPTUAL ARGUMENT IS RETIRED, NOT CARRIED. A continuous
// audio-thread timestamp publish with main-thread extrapolation against the
// latest publish used to be rejected here on perceptual grounds — a 100 Hz
// re-anchor "produces a periodic high-frequency signal the user is sensitive
// to". That was an argument against the OLD `now` anchoring, which re-rolled
// the period-wide phase residual and could step by a whole buffer period;
// today's anchor is the stamp, whose step is the drift alone — about one
// source frame, 0.02 px at the working zoom, fifty re-anchors under a single
// pixel — so a periodic re-anchor would not be VISIBLE. It is not built
// because it is not NEEDED. (The cycle stamp above is that publish's DATA
// without its cadence: the main thread reads it only at the resync events,
// never per frame, so motion between resyncs stays the smooth wall-clock
// line.) A JACK-clock predictor — jack_frame_time as the clock, every resync
// a no-op on JACK — stays set aside on the one reason that survives: keeping
// a JACK-only clock out of the shared body (its smoothness half went with
// the perceptual argument).
//
// The masking criterion for the chosen design is single-frame: each
// resync's discontinuity — the accumulated drift since the last anchor —
// must land in the same monitor frame as the viewport reflow it is
// co-located with. Future predictor work must preserve this constraint
// or argue explicitly to overturn it. The masking criterion holds across
// all zoom levels because drift visibility scales inversely with
// viewport-event frequency, keeping per-event accumulated drift sub-pixel
// at every zoom and the snap masked by the user
// motion at the resync site.

class GuiPlayback {
public:
    GuiPlayback();
    ~GuiPlayback();

    GuiPlayback(const GuiPlayback&)            = delete;
    GuiPlayback& operator=(const GuiPlayback&) = delete;

    // Bring up the audio device at the given source sample rate and channel
    // count and bind it to `samples` / `total_frames`, whose frame 0
    // represents displayed-domain coordinate `domain_offset` (see the head
    // comment). Returns true on success. On failure, logs to stderr and
    // leaves the object in an un-initialised state where play() is a silent
    // no-op.
    bool init(int sample_rate, int channels, const float* samples,
              int64_t total_frames, int64_t domain_offset);

    // Begin playback at `start_sample`, stopping when the cursor reaches or
    // passes `end_sample` (exclusive). PLAYS THE WINDOW ONCE AND STOPS — there
    // is no looping (architect 2026-07-30), so reaching the end is always the
    // natural-end teardown.
    // Both are DOMAIN coordinates of the bound buffer; the domain offset
    // is subtracted here, before the internal buffer-local clamps and
    // early-return checks. Safe to call while already playing, and safe
    // against the previous run's own natural end: the publish writes the
    // window as a COMMAND PACKET tagged with a new generation of the session
    // word, a fill consumes only the packet of the generation its gate
    // acquired and renders against its own private copy of that window, and
    // the old run's terminal is a compare-exchange expecting exactly the word
    // its callback's gate acquired, which this publish changes — so a fill in
    // flight when the publish lands finishes its block of the old run under
    // the old run's window, its terminal fails, and the next callback
    // acquires this publication and seats it (playback_common.h, the session
    // word and the packet). No teardown happens here: the callback keeps
    // running and the new packet simply supersedes the old run's.
    void play(int64_t start_sample, int64_t end_sample);

    // Stop playback and block until any in-flight audio callback has exited,
    // normally within about two of the device's callback periods. BOTH BACKENDS
    // COUNT CALLBACK INVOCATIONS for that proof — a JACK client's process
    // callback and an AAudio stream's data callback both keep running (silent)
    // between plays, each backend's device staying live from init to shutdown,
    // so two counted invocations after the session word's playing bit is
    // lowered prove the callback is out of the sample buffer (and a fill that
    // was in flight at the lowering can commit no terminal: its exchange
    // expects the word its gate acquired, which the lowering changed). THE
    // DEVICE IS NOT STOPPED HERE on
    // either platform. The wait has no deadline on either backend: it returns
    // only once the callback has quiesced, so a stalled or dead device hangs
    // here rather than letting the caller mutate a buffer the audio thread may
    // still read. Safe to call when not playing; it still fences. Main thread
    // only. The cursor retains its last value, which is what the predictor's
    // last observation rests on, so a main-thread read after the stop still
    // answers where playback stopped — through cursor(), never off the atomic
    // itself.
    void stop();

    // Take the device out of its running state, so that a player at rest is
    // not an active audio output. Call it only after stop(), whose fence is
    // what proves the callback is out of the sample buffer; this adds no
    // fence of its own. The next play() brings the device back through the
    // same start the reopen roads take, so a caller need do nothing to
    // resume, and neither device_unavailable() nor device_absent() changes
    // meaning across it: a suspended device is neither dead nor absent, it is
    // one that will sound at the next press.
    //
    // It exists for the car (architect 2026-09-04). On Android an AAudio
    // stream that stays started while the render player is paused keeps the
    // Bluetooth link streaming silence, which the head unit reads as an
    // active player and resolves against the session's "paused" by flipping
    // its display back to playing — so the player's pause must reach the
    // device, and this is how. It is the render player's alone: the ONE call
    // site is the stop body's player fork (playback_lifecycle.cpp), and the
    // main window's plays keep the no-click lifecycle whole (the head of
    // playback_aaudio.cpp owns that ruling and this narrowing of it).
    // JACK does nothing here: the laptop has no head unit and no link to
    // suspend, and its client stays connected between plays exactly as
    // before. Main thread only.
    void suspend_stream();

    // Re-anchor the free-running cursor predictor on the audio thread's cycle
    // stamp: the read cursor at the instant its frame enters the output port
    // (the design note). Call from the main thread at events where a small
    // visible discontinuity is acceptable (jumps, viewport reflows) so the
    // predictor remains a smooth linear function of wall-clock between
    // resyncs; the step is the drift since the last anchor. Safe to call when
    // not playing — the next play() will overwrite the anchor — and a no-op on
    // a session whose first fill has not run (the stamp still carries the
    // previous session's generation, and the launch anchor is already right).
    void resync_predictor();

    // Snapshot accessors. Safe from the main thread. During a graph
    // suspension, cursor() holds at the last audio position rather than
    // extrapolating, and after a natural end or any stop it answers the read
    // cursor at rest. cursor() reports the DOMAIN position: the internal
    // buffer-local cursor plus the bound buffer's domain offset. IT IS THE
    // ONE POSITION FACE — painting and resting reads take the same line at
    // the same instant (architect 2026-09-03: a second, lead-free face lived
    // beside it while the display lead did, and went with it).
    bool    is_playing() const;
    int64_t cursor()     const;

    // THERE IS NO DEVICE TO PLAY ON (2026-08-28, the render player's rule):
    // true whenever this engine cannot produce sound, WHICHEVER WAY it cannot
    // — the device that went away under a live stream (the AAudio disconnect
    // latch, a headphone pulled or a Bluetooth route dropped, and the reopen
    // that was refused after it) AND the device that never came up at all (an
    // init that failed — no JACK server, no AAudio stream — after which
    // play() is the documented silent no-op). The two are ONE ANSWER because
    // they are one fact to every consumer: nothing will sound.
    // IT IS NOT A NATURAL END, and that is what it exists to separate: a
    // silent engine leaves the session word's playing bit down exactly as a
    // window that reached its end does, and a consumer reading is_playing()
    // alone would take the
    // one for the other — the render player's tick forks on this BEFORE its
    // natural-end test and PAUSES instead of advancing (GuiRenderPlayer::tick),
    // where otherwise it would walk a folder at tick rate with nothing to play
    // it on. The JACK backend answers the never-came-up half alone; it records
    // nothing for a server that vanishes mid-play, with the reason at its
    // definition. Main thread only, like is_playing(). THIS IS THE READ AND
    // NOTHING ELSE: it reopens nothing, so on Android a dead stream stays dead
    // to it until a LAUNCH PRESS reopens through
    // ensure_device_available_for_play below (or the render player's own play
    // road reaches play(), which reopens at its head). TWO READERS, re-grepped
    // 2026-09-04, and both are TICKS: the render player's (a dead stream
    // mid-play must PAUSE, not advance) and — since that day's ruling — the
    // A/B audition's, GuiAbAudition::fire_if_due, which ends the whole act
    // through the one stop body and cards
    // kPlaybackDeviceUnavailableCard. Plus the reopen's own post-reopen read
    // inside ensure_device_available_for_play. A tick is where this belongs
    // and a press is not: a press reopens.
    // The launch FACES do not read it — they read device_absent below, the
    // never-came-up half alone, because the press reopens what this latches.
    bool    device_unavailable() const;

    // WHAT A FACE READS (architect 2026-09-02, the truthful-buttons rule over
    // the reopen at the press): THE NEVER-CAME-UP HALF ALONE. The press
    // reopens a dead stream, so only a device that never came up greys a
    // launch button — a face that read the latch would grey Play on the
    // tablet after every route drop while Space, the same act, reopened and
    // played (the twin rule: a button greys only when every admitted variant
    // would change nothing). JACK: `!impl_ || !impl_->client_active`,
    // identical to device_unavailable there (this backend has no latch).
    // AAudio: `!impl_ || !impl_->device_ready` — the `stream_dead` latch and
    // the null stream are NOT terms, being exactly what the press reopens.
    // THREE READERS, re-grepped 2026-09-02: the two PLAY-face predicates
    // space_launch_would_play and ab_audition_preflight_ok, and — since the
    // four-tier review's R-17a landed that day — the render player's modal
    // Play/Pause face, whose arm in render_player_button_enabled takes this
    // as its LEADING term (the player's own road reaches play(), which
    // reopens, so the latch is exactly what a press repairs there too).
    // Main thread only; a pure read.
    bool    device_absent() const;

    // WHAT THE DEVICE SAYS ABOUT ITSELF, READ ON DEMAND (architect 2026-09-03,
    // Help → AV Sync Stats). The backend's own name, its rate, its period and
    // — where the platform reports a trustworthy one — its OUTPUT LATENCY, the
    // figure the panel's net line needs (the type and the derivation are at
    // av_sync_stats.h). ONE CALLER: the panel's per-frame row refresh, which
    // runs only while the panel stands, so with the panel down nothing here is
    // asked and nothing is read.
    //
    // A PLAIN QUERY, NOT A CACHED ATOMIC, and that is the shape the ruling
    // asked for: a latency instrument lived on the JACK backend from
    // 2026-09-01 to 2026-09-03 — port latency ranges pushed in by a latency
    // callback, cached, and printed on every change — and went with the
    // playback leads it fed. JACK asks the client and both ports here, on the
    // main thread, at the moment the panel asks. AAUDIO REPORTS NO LATENCY
    // (`latency_known` false): the car's Bluetooth route is large, variable
    // and unreported, and the framework's figures do not carry it — the record
    // is at that backend's Impl. Main thread only; a pure read.
    GuiAudioStats audio_stats() const;

    // THE REOPEN AT THE PRESS (architect 2026-09-02, the four-tier review's
    // R-3). Asked at a LAUNCH press by the three launch gates —
    // toggle_playback's target pre-sum gate, GuiAbAudition::start's preflight
    // and the launch body's belt (playback_lifecycle.cpp, ab_audition.cpp) —
    // which card kPlaybackDeviceUnavailableCard only when this answers FALSE.
    // JACK answers `!device_unavailable()` and touches nothing (this backend
    // has no latch to clear — the mid-play loss is not recorded, the reason
    // at device_unavailable's definition). AAudio closes a dead stream and
    // reopens a dead or null one at the head — play()'s own first two lines,
    // hoisted here so a route that dropped on the tablet (a headphone pulled,
    // a Bluetooth route gone) comes back at the NEXT PRESS instead of leaving
    // every main-window launch road carding forever behind a read that never
    // reopened — AND THEN STARTS IT, because the question a launch gate asks
    // is whether this device will SOUND, not whether a stream object stands:
    // a reopened stream is stopped, and an init whose start was refused
    // leaves a non-null stopped one, neither of which device_unavailable can
    // see. Left to play()'s own start — which runs AFTER the publish — a
    // refused start lowers the playing bit and closes the stream while the
    // launch body has already seeded the scanner and returned true, so the
    // next tick reads that bit as a natural end: a silent false launch with
    // no card. The start is idempotent, so an ordinary play makes no device
    // call here; a refused one closes the stream and answers false, which is
    // the card. It does not replace play()'s own reopen and start or its
    // post-publish race check:
    // the player's road reaches play() with no gate ahead of it, and the
    // ordering argument at play()'s second latch read rests on the head
    // check standing there. Main thread only; non-const because it may open
    // and start a device.
    bool    ensure_device_available_for_play();

    // Continuous (sub-frame) counterpart of cursor(): the pre-truncation
    // extrapolated position as a double, in the SAME domain cursor() reports
    // (the bound buffer's domain offset added once, no translation). cursor()
    // returns floor(this) clamped to the window; the two agree exactly at the
    // clamped window end. A pure reader with no side effects — the same
    // observation cursor() is, without its store: cursor(), called alongside
    // it on the main thread, owns the graph-suspended re-anchor, and this
    // derives the same anchor without writing it. The
    // scanner's DRAWN pixel is derived from this so a per-frame viewport rescale
    // (a strip-drag zoom) slides it smoothly instead of stepping on integer
    // frames; the integer cursor() stays the domain / change-detection anchor.
    double  cursor_precise() const;

    // The bound buffer's domain extent, for call-site range policy:
    // domain_begin() is the domain offset (the domain coordinate of buffer
    // frame 0), domain_end() is offset plus the bound total frames (the
    // exclusive end). Main thread only, like cursor().
    int64_t domain_begin() const;
    int64_t domain_end()   const;

    // Swap the borrowed sample buffer pointer without tearing down the
    // device. Caller must call stop() first; stop() returns only after the
    // audio callback has quiesced, so the swap cannot race the callback. The
    // new buffer must match the sample rate and channel count the device was
    // init()'d with — only `samples`, `total_frames`, and `domain_offset`
    // (the domain coordinate of the new buffer's frame 0) change. The cursor
    // is reset to 0; the end_sample is reset to 0 so the next play() supplies
    // a fresh range. Used by the target render to rebind playback to the
    // freshly-rendered target_buffer, and to swap back to source.wav
    // when leaving target view.
    void rebind_buffer(const float* samples, int64_t total_frames,
                       int64_t domain_offset);

    // Tear down the device. Blocks until the audio callback has drained.
    // Call before the sample buffer dies (at shutdown).
    void shutdown();

    // Opaque to consumers, but public so the audio callback in
    // playback.cpp (a free function outside the class) can reach it.
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};
