#pragma once
#include <cstdint>
#include <memory>

struct GuiNoiseVoice;   // noise.h

// Audio playback engine. Owns ONE audio device and drives itself from that
// device's callback thread. The device half is an AAudio output stream
// (playback_aaudio.cpp, the one implementation), while the render body, the cursor predictor and
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
// The engine's own voice is summed UNSCALED, at 0 dB (architect 2026-11-11,
// playback_common.h's head).
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
//     and position_at() per paint (the scanner's advance; cursor() the same
//     observation at `now` for every other reader — the design note's one
//     observation, below).
//   - Device worker (architect 2026-11-26; THE SCREEN NEVER WAITS ON
//     ANDROID'S AUDIO, the rule's one owner, at the head of
//     playback_aaudio.cpp): makes every call on the device's stream — open
//     and start, stop and close — one job at a time, posted by the main
//     thread, which waits for the answer at most one second (init and
//     shutdown with no bound) and otherwise adopts it at a later tick
//     (adopt_device_answer below); until then the device is PENDING
//     (device_pending below). The session's underrun count is its read
//     too, asked by a note the main thread never waits for.
//
// PLAYBACK PLAYS THE BOUND BUFFER AT ITS OWN RATE (the source's, or the score
// soundtrack's, 2026-10-26 night, GuiBoundAudio). The read is fractional —
// the device may ask for frames at a rate the buffer is not recorded at, and
// the render body's one increment rescales for exactly that (playback_common.h)
// — but there is no SPEED factor on top of it (architect 2026-08-27, the
// architect running 1.0 everywhere in his one live project): a speed factor
// is naive sample-rate rescaling, shifting pitch along with tempo.
//
// Predictor design note
// ---------------------
// The cursor predictor is a free-running linear extrapolator anchored at
// (anchor_sample, anchor_ns) and re-anchored only at events of acceptable
// visible discontinuity, never inside the audio callback. The set of
// resync events: playhead jumps via move_playhead, discrete zooms via the shared
// apply_zoom_change helper, the resize zoom-out reclamp,
// follow's page-in (follow_scroll_if_needed — during a play the follow
// scroll's off-screen-left arm alone; the scroll's own placements,
// Viewport::follow_scanner, are pan frames and resync nothing) and the
// follow toggle's lit edge — AND THE LOOP WRAP (2026-09-17). (The C key's recenter, center_viewport_on_playhead, shares
// the discrete move's resync but meets no live session: `c` stops playback
// before it centres.) THE CONSOLE'S SEEK (architect 2026-11-20,
// GuiPlaybackLifecycle::seek_live_session_by) is a jump of the read position
// too, and it takes no resync: it is a NEW PUBLISH over the live session,
// whose launch anchor (the target, the publish instant) is the re-anchor.
// Every GUI launch still runs [start, end) once and stops at the natural end
// (architect 2026-07-30, all audition looping removed), so on those sessions
// the read position only ever advances between publishes and the predictor
// has no backward jump to miss. THE ONE LOOPING SESSION IS THE CAR'S — play_loop below, the
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
// `now` — or, by the scanner's per-frame advance, at ITS FRAME'S OWN VSYNC
// TIME, a few ms before `now` and the same every frame (position_at below,
// architect 2026-11-16: the time is the frame's, not a latency figure; the
// lead is less by that constant and nothing compensates it) —
// and no latency figure of either device enters either term — so the
// painted line lights at `press + D` (the compositor's paint-to-light delay)
// while the sound starts at `press + phase + L_audio` (the audio thread's
// pickup phase, 0 to one period, then the device's output latency), and the
// two part by `L_audio − D + phase`, differently on every launch. His words
// for it: "two continuous streams that we sort of happen to pick up here and
// there — sometimes they match, sometimes they don't, and that's exactly what
// they are." NO COMPENSATION: no anchor on the audio thread's first fill, no
// output latency added at an anchor, no display lead added to the position
// read, no natural-end hold keeping the line alive until the last queued
// frame is heard; nothing in this tree reads a latency figure. The RESYNC'S
// ANCHOR (below) takes a stamp rather than the main thread's `now`, which is
// accuracy about the same raw line, not compensation. The whole ruling —
// why there is no compensated line, every correction that stands and on
// what ground, and that it is not re-proposed — is the record above
// playback_publish_play (playback_common.cpp), its one owner.
//
// THE RESYNC IS EVENT-DRIVEN, AND THE REASON IS THE TOOL'S PLAY LENGTHS
// (architect 2026-09-02; no periodic cadence). Every resync is an EVENT —
// NINE call sites, in classes: the zooms
// (apply_zoom_change, apply_strip_drag_zoom's final frame,
// apply_zoom_to_start), the discrete viewport move's shared tail
// (finish_discrete_viewport_move: the centring jump, follow's page-in), the
// on-edge of the follow toggle, the playhead re-land (reseat_playhead), the
// resize, the touch navigation's end (end_touch_nav), and — the one class
// whose event is the AUDIO THREAD'S rather than the user's — THE LOOP WRAP
// (main.cpp's tick, on consume_loop_wrap; 2026-09-17). The console's seek is
// a jump with no resync call (its re-anchor is its publish's launch anchor,
// above). Grep `resync_predictor` and re-count; never inherit this number.
// EVERY RESYNC ANCHORS ON THE AUDIO THREAD'S CYCLE STAMP — (the read cursor,
// the instant that cursor's frame enters the output port) — and not on the
// main thread's `now`, which sits anywhere inside the period before that
// fill and would re-roll that whole phase into the line at every pan end or
// page turn. So a resync's step is the accumulated DRIFT alone.
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
// NO PERIODIC RE-ANCHOR, AND NOT ON PERCEPTUAL GROUNDS: a continuous
// audio-thread timestamp publish with main-thread extrapolation against the
// latest publish would step, against a `now` anchor, by the period-wide phase
// residual — a periodic high-frequency signal; against the stamp its step is
// the drift alone — about one source frame, 0.02 px at the working zoom,
// fifty re-anchors under a single pixel — so a periodic re-anchor would not
// be VISIBLE. It is not built because it is not NEEDED. (The cycle stamp
// above is that publish's DATA without its cadence: the main thread reads it
// only at the resync events, never per frame, so motion between resyncs
// stays the smooth wall-clock line.) A device-clock predictor stays set
// aside: the ±10 ms band above is the whole ruling.
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

// THE RELEASE'S OWN TWO ENDS (architect 2026-11-26; GuiPlayback::
// end_device_release): the app's return from another screen and a second
// press on the lit lamp. The third end, a play, is every reopen's own and
// takes no word here.
enum class GuiDeviceReleaseEnd { Return, SecondPress };

// A PLAY POSITION IN BOTH FORMS, of one observation (GuiPlayback::
// position_at, where the contract is): the integer domain cursor and the
// pre-truncation double it is the floor of.
struct GuiPlaybackPosition {
    int64_t cursor  = 0;
    double  precise = 0.0;
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
    // no-op. THE OPEN RUNS ON THE DEVICE WORKER AND IS WAITED FOR WITH NO
    // BOUND (architect 2026-11-26: a piece's open keeps waiting for
    // Android), the stream OPENED AND STARTED in one body; a start refused
    // on a granted stream is still a success here — the device came up,
    // no stream stands, and the next press reopens.
    // WITH RELEASE SEATED LIT (release_device below, called before
    // this init) a success binds the source and opens NO stream: the engine
    // comes up released, `device_ready` with the latch up, and the stream
    // opens at the first of the release's three ends (architect 2026-11-26).
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
    // caller forms the window right and the publish is the belt. TWO
    // CALLERS, both GuiPlaybackLifecycle's: the car's play of the trim
    // (car_toggle_playback, from the loop's begin) and THE CONSOLE'S SEEK
    // over a live loop (seek_live_session_by, architect 2026-11-20: the same
    // window and target, a new start inside them — so the sought loop is the
    // loop still; the predictor's floor is that start until a resync anchors
    // on the session's wrapped stamp, the loop target from then on). The wrap is a
    // RESYNC EVENT to the main thread, through consume_loop_wrap below; a
    // seek is not a wrap and moves no count.
    void play_loop(int64_t start_sample, int64_t end_sample,
                   int64_t loop_begin);

    // HAS THE LOOP WRAPPED SINCE THE LAST CALL — main thread only, ONE
    // CALLER: the run loop's tick (main.cpp's ma_playing branch), which
    // answers a true with resync_predictor and full waveform-and-clock damage
    // (the rare-discrete-event shape). A COUNT DIFFERENCE
    // against a main-thread-private mirror of the audio thread's wrap
    // counter, so a wrap per tick and ten per tick answer alike, and the
    // mirror catches up whole at each call. A true is answered only once the
    // wrapping fill's cycle stamp is published (the count is stored after
    // it, 2026-11-20), so the resync that follows anchors on a wrapped
    // cursor. ALWAYS FALSE ON A ONCE-THROUGH SESSION: every once-through
    // window is published from rest or over a live once-through session (a
    // live reseek), never over a loop, and a publish from rest levels the
    // mirror with the counter after the old session's last fill has landed
    // (playback_publish_play, 2026-11-20), so no wrap of a previous loop
    // reaches it — and its own window never wraps.
    bool consume_loop_wrap();

    // Stop playback and return only once no audio callback is inside the
    // bound buffer: the session word's playing bit is lowered, and the wait
    // runs only while our own data callback is inside — made only of our
    // own callbacks' runtimes, ordinarily one or none (a callback entering
    // after the lowering can add its own) — never for the device to call
    // again: no future callback is needed to end it, so a device that stops
    // calling, a dead stream or none (released, release_device below;
    // pending, device_pending below) cannot hold it (the proof at
    // playback_aaudio.cpp's GuiPlayback::stop(), architect 2026-11-26).
    // Once it returns no callback is in the render arm until the next
    // publish, and everything a fill did is ordered before what the
    // caller does next (and a fill that was in flight at the lowering can
    // commit no terminal: its exchange expects the word its gate acquired,
    // which the lowering changed). THE DEVICE IS NOT STOPPED HERE: the
    // stream keeps running, silent, between plays. Safe to call when not
    // playing; it still fences. Main thread
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
    // On a loop whose stamp stands below the predictor's floor (the
    // session's read wrapped below its published start) it first lowers the
    // floor to the loop target (playback_resync_predictor, 2026-11-20).
    void resync_predictor();

    // Snapshot accessors. Safe from the main thread. During a graph
    // suspension, cursor() holds at the last audio position rather than
    // extrapolating, and after a natural end or any stop it answers the read
    // cursor at rest. cursor() reports the DOMAIN position: the internal
    // buffer-local cursor plus the bound buffer's domain offset. IT IS THE
    // ONE POSITION LINE — painting and resting reads take the same raw line
    // (architect 2026-09-03), this face at
    // `now`, the scanner's advance at its frame's time (position_at below).
    bool    is_playing() const;
    int64_t cursor()     const;

    // THE CLIP LAMP'S COUNTER (architect 2026-11-05; render.h's THE TWO
    // LAMPS): how many callback blocks this engine's clip has clamped a
    // sample of since the engine was built — the audio thread's count
    // (GuiPlaybackState::clipped_blocks), read with acquire. Monotonic for
    // the engine's life, playing or not, whatever is bound; a stream reopen
    // does not reset it. Main thread only, like cursor(): the tick compares
    // it with the count it last saw and lights the lamp iff it moved.
    uint64_t clipped_blocks() const;

    // THERE IS NO DEVICE TO PLAY ON (2026-08-28, the render player's rule):
    // true whenever this engine cannot produce sound, WHICHEVER WAY it cannot
    // — the device that went away under a live stream (the AAudio disconnect
    // latch, a headphone pulled or a Bluetooth route dropped, and the reopen
    // that was refused after it; RELEASE's closed stream reads as that
    // latch, release_device below) AND the device that never came up at all (an
    // init that failed — no AAudio stream — after which
    // play() is the documented silent no-op). The two are ONE ANSWER because
    // they are one fact to every consumer: nothing will sound. A PENDING
    // DEVICE ANSWERS TRUE AS WELL (architect 2026-11-26; device_pending
    // below): no stream is adopted from a device job's post until its
    // answer's adoption, so the body needs no term for it.
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
    // a launch press (or NOISE's wake, below). THREE TICK READERS: the
    // render player's (a
    // dead stream mid-play must PAUSE, not advance), the A/B audition's,
    // GuiAbAudition::fire_if_due, which ends the whole act
    // through the one stop body and cards the device's sentence
    // (playback_device_refusal_card, playback_lifecycle.h: the chooser
    // between kPlaybackDeviceUnavailableCard and the pending device's),
    // and the score viewer's
    // (GuiScoreViewer::tick, Live transport), which pauses the held cursor
    // and cards the same way for a dropped soundtrack. ONE LIFT READER:
    // the NOISE case's (GuiInputHandler::finish_level_press_release for the
    // tap, take_chrome_press for a moved press; architect 2026-11-24), read
    // live to decide whether the lift wakes the stream through the reopen
    // below — nothing on screen shows the answer — and asked together with
    // device_pending: THE WAKE IS SKIPPED WHILE THE DEVICE IS PENDING
    // (architect 2026-11-26), where no reopen is attempted. Plus the
    // reopen's own post-reopen read
    // inside ensure_device_available_for_play. A launch press does not read
    // it: a launch press reopens.
    // The launch FACES do not read it — they read device_absent_or_pending
    // below, the never-came-up half and the pending device, because the
    // press reopens what this latches.
    bool    device_unavailable() const;

    // THE DROP ITSELF (architect 2026-10-28, the noise voice's reset): the
    // error callback's `stream_dead` latch ALONE — true from a route drop
    // under a live stream until the first reopen attempt closes that stream,
    // and over RELEASE's closed stream the same, from its close's adoption
    // (release_device below).
    // `impl_ && impl_->stream_dead` (acquire). THE LATCH HAS TWO MAIN-THREAD
    // READERS THAT RESET THE NOISE VOICE'S SEQUENCE (2026-10-28, noise.h's
    // head, the two roads; the sequence alone, 2026-11-05, the level
    // and the switch untouched, the bed resuming at the next reopen): (1)
    // THIS MEMBER, whose one caller is gui_main's per-tick poll beside
    // noise.tick() (main.cpp), which, while the latch stands, audible or
    // not, resets the voice's sequence (GuiNoise::reset_on_dead_stream,
    // idempotent, so the per-tick repeat over the dead interval costs
    // nothing) and reopens nothing; (2) the device's own load of the latch at
    // a reopen (reopen_stream_if_dead and launch_window's post-publish arm,
    // through close_dead_stream, playback_aaudio.cpp), which resets the
    // sequence ahead of the device job's post, which clears the latch, so a
    // press that
    // reaches the device before the tick consumes no drop unseen. A pure
    // read; main thread only. NOT device_unavailable: the latch alone, not
    // the null stream, so the reset runs only over the interval a drop
    // opened, and a refused reopen's null stream is the press's to answer.
    bool    stream_dead() const;

    // WHAT A FACE READS (architect 2026-09-02, the truthful-buttons rule over
    // the reopen at the press; the pending half architect 2026-11-26): THE
    // NEVER-CAME-UP HALF, OR THE DEVICE PENDING. The press
    // reopens a dead stream, so of device_unavailable's ways only a device
    // that never came up greys a
    // launch button — a face that read the latch would grey Play on the
    // tablet after every route drop while Space, the same act, reopened and
    // played (the twin rule: a button greys only when every admitted variant
    // would change nothing): `!impl_ || !impl_->device_ready` — the
    // `stream_dead` latch and
    // the null stream of a refused reopen are NOT terms, being exactly what
    // the press reopens. WHILE THE DEVICE IS PENDING (device_pending below:
    // a device job in flight past its bound) THE SOUND KEYS GREY TOO
    // (architect 2026-11-26): no reopen is attempted over a job in flight,
    // so the press would play nothing, and the grey lifts at the answer's
    // adoption. ONE MEMBER ANSWERS BOTH, the face's whole question.
    // FIVE READERS: the two PLAY-face predicates
    // space_launch_would_play and ab_audition_preflight_ok, the render
    // player's modal
    // Play/Pause face, whose arm in render_player_button_enabled takes this
    // as its LEADING term (the player's own road reaches play(), which
    // reopens, so the latch is exactly what a press repairs there too), and
    // the score viewer's Play face, the PlayPause arm of
    // score_viewer_button_enabled (score_viewer.h), which greys the key when
    // the soundtrack is not ready or this answers true, and the RELEASE
    // key's enabled arm in redesign_button_enabled (app_state.h), which
    // greys it when this answers true: no stream to release and no end
    // could reopen one (architect 2026-11-26) — a lit RELEASE is then lit
    // and inert, its lit arm reading AppState::device_released.
    // THE PENDING EDGE IS DAMAGED BY THE FACES' OWN COMPARATORS, the tick's
    // as-painted walks that hold each face's painted enabled bit against
    // these live predicates (main.cpp: the roster's, the render player's
    // row's, the score viewer's row's), so the keys grey and light in the
    // frame after the edge and nothing is damaged at rest.
    // Main thread only; a pure read.
    bool    device_absent_or_pending() const;

    // IS THE DEVICE PENDING (architect 2026-11-26; THE SCREEN NEVER WAITS ON
    // ANDROID'S AUDIO, playback_aaudio.cpp's head): true from the moment a
    // device job passes its bound — a reopen, RELEASE's close — until its
    // answer is adopted (adopt_device_answer below, or the head of the next
    // road that would post). While it stands: no stream is adopted
    // (device_unavailable true), nothing is published (play() is a silent
    // no-op), ensure_device_available_for_play and end_device_release
    // attempt nothing and answer false, release_device does nothing, and
    // stop() and rebind_buffer are as ever. ITS READERS: the card's chooser
    // (playback_device_refusal_card, playback_lifecycle.h — every gate that
    // cards a false device answer), RELEASE's own press
    // (GuiPlaybackLifecycle::toggle_device_release, which cards an
    // overrun), NOISE's two lift reads beside device_unavailable (above),
    // the two overlays' launches, which read it right after their play() and
    // rest where they were to start (GuiRenderPlayer::rest_if_launch_pending,
    // GuiScoreViewer::play_live_from and its live reseeks), and the render
    // player's tick's card. Main thread only; a pure read of a main-thread
    // field.
    bool    device_pending() const;

    // ADOPT THE DEVICE'S ANSWER (architect 2026-11-26; THE ADOPTION at
    // playback_aaudio.cpp's head): where a device job answered past its
    // bound, seat its answer — the stream standing or none, a release
    // completed, the pending fact cleared, the log's answer line said. ONE
    // CALLER: gui_main's tick, every tick, ahead of the dead-stream poll
    // and RELEASE's face settle (main.cpp), so a release's raised latch and
    // the faces' edge are read in the tick that adopted. AT REST ONE PLAIN
    // BOOL COMPARE on a main-thread field — no atomic, no lock; while
    // pending, one read of the worker's answer slot. The roads that would
    // post a job adopt at their own heads through the same body. Main
    // thread only.
    void    adopt_device_answer();

    // THE REOPEN AT THE PRESS (architect 2026-09-02). Asked at a LAUNCH press
    // by the three launch gates — toggle_playback's target pre-sum gate,
    // GuiAbAudition::start's preflight and the launch body's belt
    // (playback_lifecycle.cpp, ab_audition.cpp) — which card the device's
    // sentence (playback_device_refusal_card, playback_lifecycle.h) only
    // when this answers FALSE; by the
    // car's Play (car_toggle_playback's play arm, which cards alike, and the two media
    // Play arms, GuiCarTransport::on_media_command and the render player's,
    // which ignore the answer); and BY NOISE'S WAKE
    // (GuiInputHandler::wake_device_for_level_case, architect 2026-11-24: the
    // level case's lift over a dead stream), which plays nothing — the
    // reopen is the whole act, the bed resuming through the
    // running callback — and cards alike. (The bed itself asks nothing:
    // every press's reopen is the bed's resume, noise.h's head.) NO ROAD
    // REOPENS A ROUTE DROP WITHOUT A PRESS; the one reopen that runs with no
    // press is a RELEASE's end at the app's return (end_device_release,
    // architect 2026-11-26), which reopens only what the lamp closed. It closes a dead stream — the noise voice's sequence reset
    // first (close_dead_stream, noise.h's head) — and reopens a dead or null
    // one at the head — play()'s own
    // first two lines, hoisted here so a route that dropped (a headphone
    // pulled, a Bluetooth route gone) comes back at the NEXT PRESS
    // instead of leaving every main-window launch road carding forever
    // behind a read that never reopened — OPENED AND STARTED, because the
    // question a launch gate asks is whether this device will SOUND, not
    // whether a stream object stands, AND THE OPEN'S OWN BODY ANSWERS IT
    // (architect 2026-11-26): the reopen opens and starts in one device job
    // or leaves nothing, a refused start request closing its stream there,
    // so a stream that stands is one whose start was accepted. A start left
    // to play() would run AFTER the publish, where a refusal lowers the
    // playing bit and closes the stream while the launch body has already
    // seeded the scanner and returned true, the next tick reading that bit
    // as a natural end: a silent false launch with no card. A stream already running posts no
    // job, so an ordinary play makes no device call here; a refused open or
    // start answers false, which is the card. It does not
    // replace play()'s own reopen or its post-publish race check:
    // the player's road reaches play() with no gate ahead of it, and the
    // ordering argument at play()'s second latch read rests on the head
    // check standing there. Main thread only; non-const because it may open
    // and start a device. IT ENDS A RELEASE (device_released below): its
    // reopen is the one body every reopen runs, which clears the fact.
    // THE REOPEN IS ONE DEVICE JOB, WAITED FOR AT MOST ONE SECOND (architect
    // 2026-11-26; the rule at playback_aaudio.cpp's head): inside the bound
    // the answer is the synchronous one; A REOPEN THAT OVERRUNS ANSWERS
    // FALSE WITH THE DEVICE PENDING (device_pending above), and WHILE A JOB
    // IS IN FLIGHT THIS ATTEMPTS NOTHING and answers false, a standing
    // release's fact left as it is — the gates then card "Sound device is
    // not answering" through the chooser. NO QUEUED PLAY: the press plays
    // at once or not at all.
    bool    ensure_device_available_for_play();

    // RELEASE — THE KEEP-ALIVE SUPPRESSED (architect 2026-11-26; the rule,
    // his reason and its three ends at playback_aaudio.cpp's lifecycle
    // block). ONE ACT: whatever plays is stopped through stop()
    // and its fence, then the stream is CLOSED (the device's one close,
    // which joins the callback thread — a device job on the worker, waited
    // for at most one second) and the engine is left exactly as a
    // drop leaves it for every reader — device_unavailable and stream_dead
    // true, the playing bit down, no stream — plus the fact
    // device_released answers. THE FACT IS SET AS THE CLOSE IS POSTED, so
    // the lamp lights with the press; A CLOSE THAT OVERRUNS ITS BOUND leaves
    // the device pending (device_pending above) under the lit lamp, and the
    // released state completes — the latch, the log's line — when the
    // answer is adopted. The GUI's own teardown (the scanner, a
    // transport's state) is its caller's, who runs the one stop body first
    // (GuiPlaybackLifecycle::toggle_device_release). A no-op while already
    // released — which is what makes it THE SEAT as well as the key's act:
    // the open's seat of the sticky lamp (GuiInputHandler::seat_lit_lamps)
    // calls it directly, asking for the released state, never toggling.
    // CALLED BEFORE init() (that seat) it closes nothing, posts nothing and
    // init comes up released, opening no stream. A NO-OP TOO WHILE A DEVICE
    // JOB IS IN FLIGHT (its key is greyed then). THE THREE ENDS: any reopen
    // (ensure_device_available_for_play above, play()'s head) and
    // end_device_release below.
    void    release_device();

    // THE RELEASE ENDED BY THE APP'S RETURN OR THE LIT LAMP'S SECOND PRESS:
    // ensure_device_available_for_play's own reopen, nothing
    // published — the stream reopened and started, silence (and the bed)
    // running, the keep-alive restored — its answer the same, the overrun's
    // false with the device pending included. A no-op
    // answering false when nothing is released, AND WHILE A DEVICE JOB IS
    // IN FLIGHT (the release's own close not yet answered): nothing is
    // attempted and the fact stands. Main thread only.
    bool    end_device_release(GuiDeviceReleaseEnd why);

    // IS THE KEEP-ALIVE SUPPRESSED — the RELEASE lamp's live fact: true from
    // release_device until the first reopen attempt, a refused one included.
    // Its readers: the tick's lamp settle (GuiInputHandler::
    // settle_device_release_lamp, the face's mirror AppState::
    // device_released) and the sticky lamps' writer (main.cpp's
    // refresh_lit_lamps), and the return's watch (GuiPlaybackLifecycle::
    // note_window_activation). Main thread only; a pure read.
    bool    device_released() const;

    // THE POSITION AT A GIVEN TIME, BOTH FORMS FROM ONE OBSERVATION
    // (architect 2026-11-16, the beats: the scanner's position at its
    // frame's own time): cursor()'s observation with the predictor
    // evaluated at `at_ns` — a CLOCK_MONOTONIC instant in ns, the
    // predictor's own clock (playback_common.cpp's steady_now_ns) — or at
    // `now` when `at_ns` is 0. `cursor` is cursor()'s integer (the domain /
    // change-detection anchor), `precise` its pre-truncation double in the
    // SAME domain (the bound buffer's domain offset added once): cursor ==
    // floor(precise), the two agreeing exactly at the clamped window end,
    // BOTH OF THE ONE EVALUATION, so the gate that asks the integer and the
    // place drawn from the double cannot disagree. The scanner's DRAWN
    // place is derived from `precise` so a per-frame viewport rescale (a
    // strip-drag zoom) slides it smoothly instead of stepping on integer
    // frames. The storing reader, as cursor() is (the graph-suspended
    // re-anchor, taken at the real `now` whatever `at_ns` says). ONE
    // CALLER: main.cpp's pre-paint scanner advance, which hands the frame
    // time of the vsync whose frame is being painted
    // (GuiPlatform::painting_vsync_frame_ns), so in a steady play the
    // line's step from one frame to the next is the speed times the
    // spacing of their two vsyncs exactly. AN `at_ns` BEFORE THE ANCHOR
    // (the first frame of a launch made in the same loop pass, a resync's
    // stamp taken after the vsync) extrapolates backward on the same line
    // and is floored at the session's floor, as any position is: the
    // publish's own start until a resync anchors on the session's wrapped
    // stamp, the loop target from then on (GuiPlaybackState::start_sample's
    // comment).
    GuiPlaybackPosition position_at(int64_t at_ns) const;

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

    // Tear down the device. Blocks until the audio callback has drained:
    // the close runs on the device worker and is waited for WITH NO BOUND
    // (architect 2026-11-26: a piece's close keeps waiting for Android), a
    // device job still in flight waited out and adopted first.
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
