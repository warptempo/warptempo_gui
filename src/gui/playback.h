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
//     AAudio's data callback): reads cursor_/speed_, writes
//     cursor_ and is_playing_ via relaxed atomics. No allocation, no I/O,
//     no locks.
//   - Main thread: calls init/play/stop/set_speed/shutdown; snapshots
//     cursor() and is_playing() per redraw.
//
// Playback is naive sample-rate rescaling: speed 0.7 reads every 0.7th
// source sample, which shifts pitch along with tempo. This matches the
// user's Reaper-style workflow expectation.
//
// Predictor design note
// ---------------------
// The cursor predictor is a free-running linear extrapolator anchored at
// (anchor_sample, anchor_ns) and re-anchored only at events of acceptable
// visible discontinuity, never inside the audio callback. The set of
// resync events: playhead jumps via move_playhead, zoom in/out via the shared
// apply_zoom_change helper, the resize zoom-out reclamp,
// set_playback_speed, follow-mode off-to-on,
// follow-scroll auto-shift, horizontal pan via scroll_viewport
// (the alt+wheel stepped pan and PageUp/PageDown), and viewport recenter via
// center_viewport_on_playhead (C key). There is no loop-wrap resync event any
// more: LOOPING IS GONE (architect 2026-07-30, all audition looping removed),
// so the read position only ever advances and the free-running predictor has no
// backward jump to miss — playback runs [start, end) once and stops at the
// natural end.
//
// Two alternatives were considered and rejected. A free-running predictor
// with no resync is insufficient for medium-zoom playback
// over windows long enough for steady_clock vs sample-clock skew to
// accumulate to visible drift. A continuous audio-thread timestamp publish
// with main-thread extrapolation against the latest publish is
// rejected on perceptual grounds: a 100 Hz resync cadence at audio-buffer
// rate produces a periodic high-frequency signal that the user is
// sensitive to, even at sub-sample per-resync amplitudes.
//
// The masking criterion for the chosen design is single-frame: each
// resync's discontinuity (bounded by one audio buffer's worth of samples)
// must land in the same monitor frame as the viewport reflow it is
// co-located with. Future predictor work must preserve this constraint
// or argue explicitly to overturn it. The masking criterion holds across
// all zoom levels because drift visibility scales inversely with
// viewport-event frequency, keeping per-event accumulated drift sub-pixel
// at every zoom and per-event buffer-staleness snap masked by the user 
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
    // early-return checks. Safe to call while already playing — the
    // previous run is torn down cleanly first.
    void play(int64_t start_sample, int64_t end_sample);

    // Stop playback and block until any in-flight audio callback has exited,
    // normally within about two of the device's callback periods (JACK counts
    // process cycles; AAudio waits for the stream's STOPPED state). The wait
    // has no deadline on either backend: it returns only once the callback has
    // quiesced, so a stalled or dead device hangs here rather than letting the
    // caller mutate a buffer the audio thread may still read. Safe to call
    // when not playing; it still fences. Main thread only. The cursor retains
    // its last value so the main thread can snapshot where it stopped.
    void stop();

    // Clamp to [0.10, 1.00] and publish to the audio thread. The change
    // takes effect on the next callback buffer.
    void set_speed(float speed);

    // Re-anchor the free-running cursor predictor at the audio thread's
    // current cursor and the current steady_clock time. Call from the main
    // thread at events where a small visible discontinuity is acceptable
    // (jumps, viewport reflows, speed changes) so the predictor remains a
    // smooth linear function of wall-clock between resyncs. Safe to call
    // when not playing — the next play() will overwrite the anchor.
    void resync_predictor();

    // Snapshot accessors. Safe from the main thread. During a graph suspension,
    // cursor() holds at the last audio position rather than extrapolating.
    // cursor() reports the DOMAIN position: the internal buffer-local cursor
    // plus the bound buffer's domain offset.
    bool    is_playing() const;
    int64_t cursor()     const;

    // Continuous (sub-frame) counterpart of cursor(): the pre-truncation
    // extrapolated position as a double, in the SAME domain cursor() reports
    // (the bound buffer's domain offset added once, no translation). cursor()
    // returns floor(this) clamped to the window; the two agree exactly at the
    // clamped window end. A pure reader with no side effects — cursor(), called
    // alongside it on the main thread, owns the graph-suspended re-anchor. The
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
