#pragma once
#include <cstdint>
#include <memory>

struct GuiNoiseVoice;   // noise.h

// Audio playback engine. Owns ONE audio device and drives itself from that
// device's callback thread. The device half is an AAudio output stream
// (playback_aaudio.cpp, the one implementation since the laptop GUI's drop,
// architect 2026-10-30), while the render body, the cursor predictor and
// every main-thread helper below live in playback_common.{h,cpp}. THIS
// HEADER IS THE CONTRACT; the device's proofs are at its definitions.
// The sample buffer is borrowed, not owned: the caller must keep the
// pointer passed to init() valid until shutdown() returns, and must call
// stop()+shutdown() before tearing down the source.
//
// THE DEVICE CARRIES A SECOND VOICE (architect 2026-10-28): the app's one
// noise (noise.h), attached before init (attach_noise below) and summed by the
// callback under the engine's own voice on every callback — playing or
// silent, so the bed plays between plays and under every one of them — and
// the sum clipped to [-1, 1] (playback_common.h's head). The two output
// channels carry both; the noise reads its own bank and never the bound
// buffer, so every clause below about the bound buffer stands as written.
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
//   - Audio thread (the device's callback thread, AAudio's data callback):
//     reads and writes the cursor via relaxed
//     atomics, lowers the session word's playing bit at a natural end by a
//     generation-qualified compare-exchange (playback_common.h). No
//     allocation, no I/O, no locks.
//   - Main thread: calls init/play/stop/shutdown; takes is_playing() per tick
//     and cursor() / cursor_precise() per paint (the design note's one
//     observation, below).
//
// PLAYBACK PLAYS THE BOUND BUFFER AT ITS OWN RATE (the source's, or the score
// soundtrack's since 2026-10-26 night, GuiBoundAudio). The read is fractional —
// the device may ask for frames at a rate the buffer is not recorded at, and
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
// resync events: playhead jumps via move_playhead, discrete zooms via the shared
// apply_zoom_change helper, the resize zoom-out reclamp,
// follow's page-in (follow_scroll_if_needed) and the follow toggle's lit edge
// — AND, SINCE 2026-09-17, THE LOOP WRAP. (The C key's recenter, center_viewport_on_playhead, shares
// the discrete move's resync but meets no live session: `c` stops playback
// before it centres.)
// Every GUI launch still runs [start, end) once and stops at the natural end
// (architect 2026-07-30, all audition looping removed), so on those sessions
// the read position only ever advances and the predictor has no backward
// jump to miss. THE ONE LOOPING SESSION IS THE CAR'S — play_loop below, the
// head unit's play of the trim with the render player closed (architect
// 2026-09-17) — and its wrap IS a backward jump: the render body moves the
// read position back by the window's length on the audio thread, and the
// main thread learns of it through consume_loop_wrap on the run loop's tick,
// which resyncs at once. Between the wrap and that tick the predictor sits
// clamped at the mirror's end — up to one tick, accepted, the same class as
// the launch lead below — and the resync then lands the line at the loop's
// start plus the drift, the cycle stamp carrying the wrapped cursor.
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
// accuracy about the same raw line, not compensation. The whole ruling — why
// the compensated line went, every correction that stays and on what ground,
// and that it is not re-proposed — is the record above playback_publish_play
// (playback_common.cpp), its one owner.
//
// THE RESYNC IS EVENT-DRIVEN, AND THE REASON IS THE TOOL'S PLAY LENGTHS
// (architect 2026-09-02; a periodic cadence was proposed that day and does
// NOT land). Every resync is an EVENT — FOURTEEN
// call sites at this writing (re-counted 2026-09-17), in classes: the zooms
// (apply_zoom_change, apply_strip_drag_zoom's final frame,
// apply_zoom_to_start), the centring jump, follow's page and the on-edge of the follow
// toggle, the map-change re-land (reseat_playhead_to), the resize whose
// level moved, the pointer ends (the nav drag's release and force-end, the
// touch hard end), and — the one class whose event is the AUDIO THREAD'S
// rather than the user's — THE LOOP WRAP (main.cpp's tick, on
// consume_loop_wrap; 2026-09-17). Grep `resync_predictor` and re-count;
// never inherit this number.
// EVERY RESYNC ANCHORS ON THE AUDIO THREAD'S CYCLE STAMP — (the read cursor,
// the instant that cursor's frame enters the output port) — and not on the
// main thread's `now`, which sat anywhere inside the period before that fill
// and re-rolled that whole phase into the line at every pan end or page
// turn. So a resync's step is the accumulated DRIFT alone.
// A session that is launched and left alone — the architect's own `c`,
// Space, no pan — runs the
// whole play on steady_clock against the DAC's crystal with no re-anchor at
// all. THAT IS AFFORDABLE BECAUSE THE DRIFT BETWEEN EVENTS IS BELOW THE
// FRAME GRID AT THIS TOOL'S PLAY LENGTHS: the clocks part at 10–100 ppm, so
// 0.6–6 ms per minute, and this is a spot-check instrument for segments of
// up to ~30 s (the trim/render design, the memory-vs-disk preview cutoff),
// where that is 0.6–3 ms — up to about three pixels at the tablet's working
// zoom (`c` is one working column per pixel, 46 frames or about 1.04 ms
// there), and well inside the ±10 ms band in which a
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
// line.) A device-clock predictor stays set aside: the perceptual argument
// above is the whole ruling.
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

// THE LOOP TARGET'S SENTINEL: a launch whose loop target is this plays its
// window ONCE and takes the natural end (every GUI launch — play() forwards
// it); any value >= 0 is the DOMAIN frame the render body wraps to when the
// read position reaches the window's end (play_loop below: the car's loop of
// the trim, architect 2026-09-17 — NOTHING LOOPS' second sanctioned exception
// beside the render player's Repeat One). The engine keeps it in the command
// packet's third word (playback_common.h).
constexpr int64_t kPlaybackNoLoop = -1;

// THE BOUND BUFFER'S RECORD (architect 2026-10-26 night, the score's
// soundtrack: "not every source file will be 44.1k; detect it from the source
// audio" — NOTHING CONVERTS): what rebind_buffer binds, interleaved STEREO at
// its own rate in one of TWO FORMATS, exactly one pointer non-null —
//   `f32`  float32, the in-tree WAV reader's own decode: the project source,
//          the target preview and a render player item, all at the source's
//          rate (the player's decode refuses any other);
//   `i16`  PCM16 held as it lies in the file, the score's soundtrack
//          (score_audio.h), at whatever rate its file carries.
// The render body reads the sample through the format and the buffer at
// `source_rate / output_rate` per output frame (playback_common.h's head), so
// a buffer of any rate plays at its own speed on the device as it was opened,
// which is never reopened for it. `domain_offset` is the domain coordinate of
// the buffer's frame 0 (the head comment). Built by its two named faces.
struct GuiBoundAudio {
    const float*   f32           = nullptr;
    const int16_t* i16           = nullptr;
    int64_t        total_frames  = 0;
    int            source_rate   = 0;
    int64_t        domain_offset = 0;

    static GuiBoundAudio float32(const float* samples, int64_t total_frames,
                                 int source_rate, int64_t domain_offset) {
        GuiBoundAudio b;
        b.f32           = samples;
        b.total_frames  = total_frames;
        b.source_rate   = source_rate;
        b.domain_offset = domain_offset;
        return b;
    }
    static GuiBoundAudio pcm16(const int16_t* samples, int64_t total_frames,
                               int source_rate) {
        GuiBoundAudio b;
        b.i16          = samples;
        b.total_frames = total_frames;
        b.source_rate  = source_rate;
        return b;
    }
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

    // ATTACH THE SECOND VOICE (2026-10-28, noise.h): the app's one noise
    // voice, summed by every callback of this engine from its first to its
    // shutdown. CALLED BEFORE init() — run_project's first act on its
    // engine — and never again, so the pointer never moves while a callback
    // can run. Main thread only; null attaches nothing. The device forwards it
    // to the engine state (GuiPlaybackState::noise).
    void attach_noise(GuiNoiseVoice* voice);

    // Begin playback at `start_sample`, stopping when the cursor reaches or
    // passes `end_sample` (exclusive). PLAYS THE WINDOW ONCE AND STOPS — no
    // GUI launch loops (architect 2026-07-30), so reaching the end is always
    // the natural-end teardown; this face forwards kPlaybackNoLoop and the
    // looping face is play_loop below.
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

    // THE LOOPING LAUNCH (architect 2026-09-17, the car's play of the trim
    // with the render player closed — NOTHING LOOPS' second sanctioned
    // exception, beside the render player's Repeat One): plays [start_sample,
    // end_sample) and then WRAPS TO `loop_begin` FOREVER, until stop() or a
    // newer publish supersedes it. Every clause of play()'s contract holds —
    // the same packet, the same generation, the same supersession — with one
    // more word in the packet, the wrap target; the wrap itself is
    // SAMPLE-ACCURATE ON THE AUDIO THREAD (the render body subtracts the
    // window's length from the fractional read position and goes on filling
    // the same block, so no sample is dropped or doubled and the increment's
    // phase carries across the seam). All three are DOMAIN coordinates.
    // REFUSED, with nothing written, where the loop would hold fewer than two
    // frames or `start_sample` lies outside [loop_begin, end_sample) — the
    // caller forms the window right (GuiPlaybackLifecycle::car_toggle_playback
    // is the one caller) and the publish is the belt. The wrap is a RESYNC
    // EVENT to the main thread, through consume_loop_wrap below.
    void play_loop(int64_t start_sample, int64_t end_sample,
                   int64_t loop_begin);

    // HAS THE LOOP WRAPPED SINCE THE LAST CALL — main thread only, ONE
    // CALLER: the run loop's tick (main.cpp's ma_playing branch), which
    // answers a true with resync_predictor and full waveform-and-clock damage
    // (the rare-discrete-event shape, the launch's own). A COUNT DIFFERENCE
    // against a main-thread-private mirror of the audio thread's wrap
    // counter, so a wrap per tick and ten per tick answer alike, and the
    // mirror catches up whole at each call. Always false on a once-through
    // session.
    bool consume_loop_wrap();

    // Stop playback and block until any in-flight audio callback has exited,
    // normally within about two of the device's callback periods. THE DEVICE
    // COUNTS CALLBACK INVOCATIONS for that proof — the AAudio stream's data
    // callback keeps running (silent) between plays, the device staying live
    // from init to shutdown, so two counted invocations after the session
    // word's playing bit is
    // lowered prove the callback is out of the sample buffer (and a fill that
    // was in flight at the lowering can commit no terminal: its exchange
    // expects the word its gate acquired, which the lowering changed). THE
    // DEVICE IS NOT STOPPED HERE. The wait has no deadline: it returns
    // only once the callback has quiesced, so a stalled or dead device hangs
    // here rather than letting the caller mutate a buffer the audio thread may
    // still read. Safe to call when not playing; it still fences. Main thread
    // only. The cursor retains its last value, which is what the predictor's
    // last observation rests on, so a main-thread read after the stop still
    // answers where playback stopped — through cursor(), never off the atomic
    // itself.
    void stop();

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
    // init that failed — no AAudio stream — after which
    // play() is the documented silent no-op). The two are ONE ANSWER because
    // they are one fact to every consumer: nothing will sound.
    // IT IS NOT A NATURAL END, and that is what it exists to separate: a
    // silent engine leaves the session word's playing bit down exactly as a
    // window that reached its end does, and a consumer reading is_playing()
    // alone would take the
    // one for the other — the render player's tick forks on this BEFORE its
    // natural-end test and PAUSES instead of advancing (GuiRenderPlayer::tick),
    // where otherwise it would walk a folder at tick rate with nothing to play
    // it on. Main thread only, like is_playing(). THIS IS THE READ AND
    // NOTHING ELSE: it reopens nothing, so a dead stream stays dead
    // to it until a LAUNCH PRESS reopens through
    // ensure_device_available_for_play below (or the render player's own play
    // road reaches play(), which reopens at its head): a dead stream waits for
    // a launch press. THREE READERS, re-grepped
    // 2026-10-28, and all are TICKS: the render player's (a dead stream
    // mid-play must PAUSE, not advance), the A/B audition's,
    // GuiAbAudition::fire_if_due, which ends the whole act
    // through the one stop body and cards
    // kPlaybackDeviceUnavailableCard, and the score viewer's
    // (GuiScoreViewer::tick, Live transport), which pauses the held cursor
    // and cards the same sentence for a dropped soundtrack. Plus the
    // reopen's own post-reopen read
    // inside ensure_device_available_for_play. A tick is where this belongs
    // and a press is not: a press reopens.
    // The launch FACES do not read it — they read device_absent below, the
    // never-came-up half alone, because the press reopens what this latches.
    bool    device_unavailable() const;

    // THE DROP ITSELF (architect 2026-10-28, the noise lamp's darkening): the
    // error callback's `stream_dead` latch ALONE — true from a route drop
    // under a live stream until the first reopen attempt closes that stream.
    // `impl_ && impl_->stream_dead` (acquire). THE LATCH HAS TWO MAIN-THREAD READERS THAT RESET THE
    // NOISE VOICE (2026-10-28, noise.h's head, the two roads): (1) THIS
    // MEMBER, whose one caller is gui_main's per-tick poll beside
    // noise.tick() (main.cpp), which, while the latch stands, LIT OR NOT,
    // DARKENS the NOISE lamp and resets its voice
    // (GuiNoise::reset_on_dead_stream, idempotent, so the per-tick repeat
    // over the dead interval costs nothing) and reopens nothing; (2) the
    // device's own load of the latch at a reopen (reopen_stream_if_dead
    // and launch_window's post-publish arm, through close_dead_stream,
    // playback_aaudio.cpp), which resets the voice ahead of the close that
    // clears the latch, so a press that reaches the device before the tick
    // consumes no drop unseen. A pure read; main thread only.
    // NOT device_unavailable: the latch alone, not the null stream, so the
    // reset runs only over the interval a drop opened, and a refused
    // reopen's null stream is the press's to answer — a lit press refused
    // there cards and keeps its lamp lit, silent (noise.h's head), which the
    // poll must not then darken.
    bool    stream_dead() const;

    // WHAT A FACE READS (architect 2026-09-02, the truthful-buttons rule over
    // the reopen at the press): THE NEVER-CAME-UP HALF ALONE. The press
    // reopens a dead stream, so only a device that never came up greys a
    // launch button — a face that read the latch would grey Play on the
    // tablet after every route drop while Space, the same act, reopened and
    // played (the twin rule: a button greys only when every admitted variant
    // would change nothing): `!impl_ || !impl_->device_ready` — the
    // `stream_dead` latch and
    // the null stream are NOT terms, being exactly what the press reopens.
    // FOUR READERS, re-grepped 2026-10-28: the two PLAY-face predicates
    // space_launch_would_play and ab_audition_preflight_ok, the render
    // player's modal
    // Play/Pause face, whose arm in render_player_button_enabled takes this
    // as its LEADING term (the player's own road reaches play(), which
    // reopens, so the latch is exactly what a press repairs there too), and
    // the score viewer's Play face, the PlayPause arm of
    // score_viewer_button_enabled (score_viewer.h), which greys the key when
    // the soundtrack is not ready or this answers true.
    // Main thread only; a pure read.
    bool    device_absent() const;

    // THE REOPEN AT THE PRESS (architect 2026-09-02). Asked at a LAUNCH press
    // by the three launch gates — toggle_playback's target pre-sum gate,
    // GuiAbAudition::start's preflight and the launch body's belt
    // (playback_lifecycle.cpp, ab_audition.cpp) — which card
    // kPlaybackDeviceUnavailableCard only when this answers FALSE; by the
    // car's Play (car_play_playback, which cards alike, and the two media
    // Play arms, GuiCarTransport::on_media_command and the render player's,
    // which ignore the answer); by THE NOISE LAMP'S LIT PRESS (architect
    // 2026-10-28, the `w` case of input_key_dispatch.cpp: the bed sounds only
    // through the running stream's callback, so the lit press is a launch
    // press for the device question; a FALSE cards and the lamp still
    // lights). No road reopens without a press. It closes a dead stream — the
    // noise voice reset first, its lamp dark (close_dead_stream, noise.h's
    // head) — and reopens a dead or null one at the head — play()'s own
    // first two lines, hoisted here so a route that dropped (a headphone
    // pulled, a Bluetooth route gone) comes back at the NEXT PRESS
    // instead of leaving every main-window launch road carding forever
    // behind a read that never reopened — AND THEN STARTS IT, because the
    // question a launch gate asks is whether this device will SOUND, not
    // whether a stream object stands: a
    // reopened stream is stopped, and an init whose start was refused leaves a
    // non-null stopped one, neither of which device_unavailable can see. Left
    // to play()'s own start — which runs AFTER the publish — a refused start
    // lowers the playing bit and closes the stream while the launch body has
    // already seeded the scanner and returned true, so the next tick reads
    // that bit as a natural end: a silent false launch with no card. The start
    // is idempotent, so an ordinary play makes no device call here; a refused
    // one closes the stream and answers false, which is the card. It does not
    // replace play()'s own reopen and start or its post-publish race check:
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

    // Swap the borrowed sample buffer without tearing down the device.
    // Caller must call stop() first; stop() returns only after the audio
    // callback has quiesced, so the swap cannot race the callback. THE NEW
    // BUFFER IS STEREO, AT ITS OWN RATE AND IN ITS OWN FORMAT (GuiBoundAudio,
    // above; architect 2026-10-26 night): the channel count is the one thing
    // that must match what the device was init()'d with — the pointer, the
    // format, the frame count, the rate and the domain offset all change, in
    // the same moment, and the device is not reopened. The cursor is reset to
    // 0; the end_sample is reset to 0 so the next play() supplies a fresh
    // range. Used by the target render to rebind playback to the
    // freshly-rendered target_buffer and to swap back to source.wav when
    // leaving target view, by the render player for its item and by the score
    // viewer for the score's soundtrack (each binding back to the view's
    // buffer at its close).
    void rebind_buffer(const GuiBoundAudio& buffer);

    // Tear down the device. Blocks until the audio callback has drained.
    // Call before the sample buffer dies (at shutdown).
    void shutdown();

    // Opaque to consumers, but public so the audio callback
    // (playback_aaudio.cpp, a free function outside the class) can reach it.
    struct Impl;

private:
    // THE ONE LAUNCH ROAD BEHIND BOTH PUBLIC FACES (2026-09-17): play() and
    // play_loop() differ in the packet's third word alone, so the device
    // keeps its head-check, its publish and its post-publish race
    // check in this one body and the two faces forward to it —
    // kPlaybackNoLoop from play(), the caller's target from play_loop().
    void launch_window(int64_t start_sample, int64_t end_sample,
                       int64_t loop_begin);

    std::unique_ptr<Impl> impl_;
};
