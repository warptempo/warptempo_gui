#pragma once
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
//   - Main thread: calls init/play/stop/shutdown; takes snapshot() per tick
//     (the playing bit and the natural-end hold from one observation) and
//     cursor() / cursor_precise() per paint (the design note's one
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
// EVERY ANCHOR IS A HEARD INSTANT (architect 2026-09-01): the pair is
// (sample, the steady_clock instant that sample leaves the loudspeaker), and
// one figure — the device's reported output latency, `output_latency_frames`
// at the output rate, the JACK backend's figure and AAudio's zero by ruling —
// is added at every anchor write: ONE FIGURE PER EPOCH, re-anchored at the
// change (a quantum change moves it mid-session; the reader records the
// offset its anchor was built with and, when the live figure differs,
// re-anchors from the latest stamp exactly as a resync does, so the cursor and
// the natural-end deadline never mix two epochs). The audio thread
// supplies the instants: at the top of each fill it reads the clock once and
// publishes, under a seqlock, the stamp (read cursor, the instant that
// cursor's frame enters the port, the session's generation) — the SEAT when
// a fill consumes its session's command packet, the fill-end stamp on every
// fill (playback_common.h, the cycle stamp). The launch anchor is the seat's
// stamp plus that epoch's offset, latched once by the first predictor read
// that finds a stamp carrying the published generation (the publish anchors
// at (start, "await the seat") and the line RESTS on the launch frame until
// the first sound is heard — never a backward step); every resync anchors at
// the latest stamp plus that epoch's offset, so a resync's step is the
// wall-clock DRIFT since the last anchor and nothing else — neither the
// latency nor the period-wide phase residual the old `now` anchoring
// re-rolled. THE NATURAL-END HOLD is the same figure at the far end: the
// render body swaps the session word from playing to ended at the window's
// end (one generation-qualified exchange, playback_common.h), cursor() keeps
// extrapolating (clamped at the end) while natural_end_holding() answers true
// — until the last frame the ending fill consumed has been heard — and the
// run loop's tick tears the scanner down only then, so the line vanishes when
// the sound does. is_playing() is untouched by the hold. THE READERS ARE ONE
// OBSERVATION: cursor(), heard_cursor(), cursor_precise(),
// natural_end_holding() and
// snapshot() are faces over one main-thread body that reconciles the anchor
// FIRST — re-anchoring from the stamp where the live figure differs from the
// anchor's, and, from the first read that sees the ended bit, onto the
// TERMINAL STAMP itself, (end, the instant the last sound is heard) — and
// then forms the position and the hold's verdict from that one stamp/offset
// pair and one clock read: through the hold the anchor IS the stamp the
// deadline is read from, so the line reaches the window's end exactly as the
// hold ends and a teardown decided on the verdict always has that line under
// it (playback_common.h, the readers' block).
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
// So a session that is launched and left alone — the architect's own `c`,
// Space, no pan, and the `y` pin, which derives its camera per frame and
// resyncs nothing (the statement is at derive_centered_viewport) — runs the
// whole play on steady_clock against the DAC's crystal with no re-anchor at
// all. THAT IS AFFORDABLE BECAUSE THE DRIFT BETWEEN EVENTS IS BELOW THE
// FRAME GRID AT THIS TOOL'S PLAY LENGTHS: the clocks part at 10–100 ppm, so
// 0.6–6 ms per minute, and this is a spot-check instrument for segments of
// up to ~30 s (the trim/render design, the memory-vs-disk preview cutoff),
// where that is 0.6–3 ms — under a pixel at the working zoom and inside the
// ±10 ms band in which a picture/sound offset is invisible. The gain from a
// cadence exists only on the multi-minute plays the tool is not for, while
// its cost is a per-play risk of exactly the "imperceptible" class plus
// machinery. The event resync is kept because it is FREE, not because a play
// without one would be wrong: its step is the drift alone and it lands in the
// same frame as the reflow that masks it (the criterion below). AND A LONG
// PLAY STILL RESOLVES: the natural-end hold re-anchors onto the TERMINAL
// STAMP, so whatever drift a multi-minute play accumulated is taken back
// there in one step — the recorded, accepted behaviour.
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
// resync's discontinuity — the accumulated drift since the last anchor, now
// that the anchor carries the heard instant — must land in the same monitor
// frame as the viewport reflow it is
// co-located with. Future predictor work must preserve this constraint
// or argue explicitly to overturn it. The masking criterion holds across
// all zoom levels because drift visibility scales inversely with
// viewport-event frequency, keeping per-event accumulated drift sub-pixel
// at every zoom and the snap masked by the user
// motion at the resync site.

// ONE OBSERVATION'S TWO ANSWERS (GuiPlayback::snapshot): the session word's
// playing bit and the natural-end hold's verdict, from one load of the word,
// one clock read and one latency epoch — the observation having re-anchored
// the predictor first, so the paint that follows reads the line the verdict
// was decided on. (A third member, the predicted cursor, rode here for the
// day of 2026-09-01: the tick by its own ruling never wrote it into the
// scanner — the pre-paint owns that advance — so it was a producer-only
// field and retired that evening; the terminal decision it was added to
// support is carried by the anchor the observation stores, not by a value
// handed back.)
struct GuiPlaybackSnapshot {
    bool playing             = false;
    bool natural_end_holding = false;
};

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
    // only. The cursor retains its last value so the main thread can snapshot
    // where it stopped.
    void stop();

    // Re-anchor the free-running cursor predictor at the audio thread's
    // current cursor and the instant that cursor is HEARD — its cycle stamp
    // plus the device's reported latency (the design note). Call from the main
    // thread at events where a small visible discontinuity is acceptable
    // (jumps, viewport reflows) so the predictor remains a
    // smooth linear function of wall-clock between resyncs; the step is the
    // drift since the last anchor. Safe to call when not playing — the next
    // play() will overwrite the anchor — and a no-op on a session the audio
    // thread has not seated yet (the launch anchor is the seat's to latch).
    void resync_predictor();

    // THE DISPLAY LEAD (architect 2026-09-02): how far ahead of `now` the
    // predictor's POSITION is read, so the painted playhead lands where the
    // sound will be when its pixel turns into light. The platform owns the
    // figure (GuiPlatform::display_lead_ns — measured through the
    // compositor's presentation feedback on the laptop, 0 by ruling on the
    // tablet) and main.cpp's pre-paint hook writes it here once per painted
    // frame, ahead of that frame's cursor() / cursor_precise() reads. It
    // moves the position alone — the natural-end hold keeps the bare clock
    // (the field's contract, playback_common.h) — and of the position faces
    // it moves only the ones that PAINT: heard_cursor() below reads the same
    // line at `now`. Main thread only; a no-op
    // before init.
    void set_display_lead_ns(int64_t lead_ns);

    // Snapshot accessors. Safe from the main thread. During a graph suspension,
    // cursor() holds at the last audio position rather than extrapolating.
    // cursor() reports the DOMAIN position: the internal buffer-local cursor
    // plus the bound buffer's domain offset. Through the natural-end hold
    // (below) cursor() goes on extrapolating to the window's end after
    // is_playing() has dropped.
    bool    is_playing() const;
    int64_t cursor()     const;

    // THE HEARD CURSOR — cursor() WITHOUT THE DISPLAY LEAD (architect ruling
    // 2026-09-02, converting the codex finding that the paint-only lead had
    // moved the render player's pause point). THE CONTRACT, one sentence per
    // face: PAINTING PREDICTS AHEAD, because the pixel lights after the read
    // — cursor(), cursor_precise() and the clock, the scrub and the scanner
    // that draw from them; A RESTING WRITE RECORDS WHERE THE EAR WAS, because
    // a stored position is content, not a picture, and must not depend on
    // which output the window happens to be on. This is that second face.
    // ONE BODY, ONE FORK: the shared observation takes an `apply_display_lead`
    // parameter that reaches its position read alone (playback_common.h), so
    // this is cursor()'s own anchor reconciliation, hold arithmetic and store
    // with the lead term zero — never a second predictor. On Android the two
    // answer identically (that backend's lead is 0 by ruling), as they do
    // wherever the device is suspended and the predictor holds at the integer
    // cursor.
    // THE ONE READER TODAY is the render player's two resting writes — the
    // live pause and the dead-device pause (render_player.cpp). The main
    // window's own stop parks nothing: stop_playback_if_playing leaves
    // `playhead_cursor_sample` untouched by design, so it needs no face here.
    // Main thread only, and a STORING reader like cursor().
    int64_t heard_cursor() const;

    // THE NATURAL-END HOLD (the design note): true from the render body's
    // natural end — is_playing() already false — until the last frame it
    // queued has been heard. The run loop's tick keeps the scanner through it
    // and takes the one stop body when it ends; every stop road clears it at
    // once. False while playing and after any stop. Main thread only. A
    // STORING reader like cursor(): the one observation it is a face of
    // reconciles the latency epoch before it answers (the design note).
    bool    natural_end_holding() const;

    // THE TICK'S READ — the playing bit and the hold's verdict from ONE
    // observation (the design note): one load of the session word answers
    // both, and the observation re-anchors the predictor first — where the
    // live figure had moved under the anchor, and onto the terminal stamp
    // from the first read of the ended bit — so a terminal decision taken on
    // `natural_end_holding` can never run ahead of the re-anchor the cursor's
    // next paint would have made, and the line it leaves under the paint
    // reaches the window's end exactly as the hold ends. Stores like
    // cursor(). Main thread only.
    GuiPlaybackSnapshot snapshot() const;

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
    // road reaches play(), which reopens at its head). ONE READER: the render
    // player's tick (a dead stream mid-play must PAUSE, not advance), plus the
    // reopen's own post-reopen read inside ensure_device_available_for_play.
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
    // it on the main thread, owns the graph-suspended re-anchor, the launch
    // latch and the latency-epoch re-anchor, and this derives the same anchor
    // without writing it. The
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

// IS THE TRANSPORT LIVE TO THE EAR — playing, or in the natural-end hold with
// its last frames still leaving the loudspeaker. THE READERS THAT MEAN "is
// the sound still going" ask this (2026-09-01): bare Space's stop/play fork,
// the waveform scrub's stop arm, and the placement press's `was_playing`
// capture, whose keep-alive reseek plays on from the click; a press in the
// hold's last few frames then STOPS or RESEEKS as it would have a moment
// earlier, instead of launching over a session the face still shows as live.
// The readers that mean "is the audio thread inside the buffer" — the
// conditional stops ahead of a rebind, the quiescence reasoning — keep
// is_playing(), which the hold does not touch. ONE OWNER, so the two terms
// are never spelled apart.
inline bool playback_sounding(const GuiPlayback& playback) {
    return playback.is_playing() || playback.natural_end_holding();
}
