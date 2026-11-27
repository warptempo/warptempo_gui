#include "playback.h"

#include "playback_common.h"
#include "noise.h"  // noise_mix_block — the second voice the callback sums

#include <aaudio/AAudio.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>

// THE AAUDIO DEVICE HALF of playback.h's contract — THE DEVICE HALF, the one
// implementation (architect 2026-10-30, the tablet the one platform). The
// ENGINE is playback_common.{h,cpp}, so what lives here is one AAudio output
// stream: opening it, its two callbacks, stop()'s quiescence fence (its proof
// at GuiPlayback::stop()), the disconnect rule, and the device worker that
// makes every AAudio control call (THE SCREEN NEVER WAITS ON ANDROID'S AUDIO,
// below).
//
// THE RATE. The stream is opened at AAUDIO_UNSPECIFIED and the GRANTED rate
// becomes the engine's `output_rate`, so the render body's one increment
// (`source_rate / output_rate`, the head of playback_common.h) does the
// 44100 -> whatever-the-device-wants rescaling through the fractional read the
// body already performs.
// The tablet's own speaker grants 48000 and its hardware IS 48 k — 44.1 can
// never be native there — so this ratio is live on every play, and the
// architect's standing ruling is that the audition resample is preview-
// inaudible (nothing vendor-dependent, no second resampler). The ratio is
// recomputed at every open, which is what makes a reopen onto a DIFFERENT
// device (a DAC plugged in after a disconnect) correct without any other edit.
//
// THE STREAM'S LIFECYCLE: STARTED AT EVERY SUCCESSFUL OPEN, NEVER STOPPED
// BETWEEN PLAYS (architect 2026-11-26: "the car has a stutter ... when connecting to
// Bluetooth, and the Keep Alive prevents that"). Over the car's Bluetooth link
// every play on a stopped-and-restarted stream stutters throughout, while a
// stream that is started once and kept running is clean. The backend starts the
// stream ONCE, at open (init, and the reopen after a disconnect or a
// release), and stops it
// only where the stream is about to be CLOSED: shutdown, the dead-stream
// reopen and RELEASE (below). All close through close_stream_handle, which
// holds the file's ONE requestStop call. OPEN AND START ARE ONE BODY
// (open_and_start_stream, the device job's open): a stream is opened and
// its start requested, or nothing is left — a refused start request closes
// it there — so A STANDING STREAM IS ONE WHOSE START WAS ACCEPTED
// (AAudioStream_requestStart is asynchronous: the stream is STARTING or
// STARTED, and nothing here waits for its first callback or needs to), and
// a refused start leaves the ordinary no-stream state (`device_ready` true,
// no stream), which the next press reopens. The opens are init's and every
// reopen's (reopen_stream_if_dead: the launch gates', the car's Play and
// NOISE's wake, the callers at ensure_device_available_for_play's
// declaration; play()'s own head for the roads that reach it ungated; the
// release's two ends), so an ordinary play neither starts nor stops the
// stream (its underrun count is the device worker's read, handed over as a
// note: report_xrun_count).
//
// THE EVIDENCE (2026-09-12, on the architect's car): a fresh continuous stream
// is clean for hours, while every play on a stopped-and-restarted stream
// crackles throughout — the beginning, the middle and the end of a 30-40 s
// stretch, never twice at the same spot, so it is the restart itself and not
// the settling of its first seconds. THE RULING HAS NO NARROWING: the render
// player's rest does not stop the stream either. What the head unit makes of a
// stream that runs while the session says paused (a running stream the
// Bluetooth link reads as an active player) is therefore the PUBLISH side's business
// (GuiRenderPlayer::publish_media_state), and no TRANSPORT road touches the
// device between plays: play and stop leave it as it is. (The one other act
// on it at rest is the RELEASE lamp's, below: its press closes the stream and
// its three ends reopen it, architect 2026-11-26.) THE STREAM STARTED PER PLAY ON THE ORDINARY PATH
// (the mixer's own start and stop, as a media player's) IS RULED OUT
// (2026-11-26, on his test in the car: "I just tried YouTube, and after about
// 10 seconds that crackling would come back ... it's not a viable option").
//
// So the stream is started once per successful open (at launch, with no
// audition under it, and again at a reopen after a disconnect: a launch press,
// the render player's Play through reopen_stream_if_dead, NOISE's wake), and
// an ordinary play() or stop() requests neither a start nor a stop. BETWEEN
// PLAYS THE STREAM RUNS AND THE PROJECT VOICE WRITES SILENCE — its gate on the
// session word's playing bit
// takes the silence arm and the render body is never reached, so the idle
// project voice reads no project samples; the noise bed, when audible, still
// plays through the same callback, and the stream carries silence when both
// voices are quiet. The running stream costs the tablet a few mW of an
// already screen-on device, which is the price the architect accepted for the
// car's clean stream.
// THE KEEP-ALIVE HOLDS THE STREAM RUNNING BETWEEN PLAYS BUT WHILE RELEASE IS
// LIT (architect 2026-11-26: "the silent stream is there hiding, and we're
// actually disabling the hidden"; "Okay to build the keep alive
// suppression"). In the car the Bluetooth codec is switched once a drive
// (Developer options, HD audio off), and he makes that switch with this
// stream off the link, which, kept running, it is always on. The car does
// not refuse a switch on a streaming link: it sometimes misses one
// (Android's Bluetooth stack gives the car up after 15 s, the link drops and
// comes back), and whether an idle link spares the miss is unproven (the
// tablet's log of the car test: docs/engineering/android_notes.md §11.4a).
// The top icon row's RELEASE lamp (RedesignButton::IconRelease)
// closes the stream ON PURPOSE: the act (GuiPlayback::release_device) is the
// dead-stream state below entered by choice — the latch, the rate 0, the
// playing bit down, no stream — plus the one fact `suppressed`, the lamp.
// THE FIRST OF THREE ENDS IT, and that reopen restarts the keep-alive with
// the stream: the app's return from another screen (GuiPlaybackLifecycle::
// note_window_activation), a play — any road that reopens a dead stream:
// the launch gates, the car's Play, NOISE's wake, the render player's and
// the score viewer's play — and a second press on the lit lamp. No timer.
// The fact is cleared in the one body every reopen runs
// (reopen_stream_if_dead), at the attempt, a refused reopen included; while
// a device job is in flight past its bound (THE RULE below) nothing is
// attempted and the fact stands. NOT A STANDING MODE
// AND NOT A POWER CONTROL: between the release and its end the device is
// simply absent, and once it ends the stream runs between plays as ruled.
//
// THE TABLET'S IDLE DRAIN IS THIS RUNNING STREAM, KNOWN (architect 2026-09-29,
// closing the power exploration): he closes the app when he is done, and no
// Car Mode lamp, idle timer or power cap is built or re-proposed (RELEASE
// above is none of the three: it idles the car's link for one switch, and the
// stream per play stays ruled out). (A
// step-shaped click at the START of a play, from the render body beginning
// mid-waveform at whatever sample value sits there with no ramp, belongs to the
// render body and not to the device stream, and is untouched here.)
//
// THE SCREEN NEVER WAITS ON ANDROID'S AUDIO (architect 2026-11-26: "Keep the
// screen live while Android's audio is stuck") — THE RULE: the worker, the
// bound, pending, the adoption, stated here once; every other site cites
// it. In the car he toggled
// the Bluetooth codec until the car dropped the link, and Android's audio HAL
// retried the A2DP start for 8 s and 10 s. The stream operations — the
// open, the start, the stop request, the close — are binder calls into that
// same audio server, and even a count read can wait on a lock Android holds
// across such a call (report_xrun_count's block); a screen thread inside one
// sits there with it — the "isn't responding" dialog, whose Close would cost
// unsaved work and the undo history. So:
//   * THE WORKER, ONE CONTROL THREAD. EVERY AAUDIO CONTROL CALL — the
//     builder, openStream and the getters read at the open, requestStart,
//     requestStop, close — RUNS ON ONE DEVICE WORKER THREAD of the engine
//     (device_worker_main: a plain thread of Impl asleep on a condition
//     variable between jobs — no polling, no timer, no wake at rest —
//     started with the engine and joined in its destructor, after
//     shutdown). THE WORKER MAKES EVERY AAUDIO CALL ON A STREAM, the two
//     underrun reads included (AAudioStream_getXRunCount, handed over as
//     NOTES the screen thread never waits for: Impl's note store,
//     report_xrun_count); the screen thread and the two callbacks make
//     none.
//   * THE JOB, ONE SHAPE FOR EVERY ROAD (DeviceJob): {a stream to close, or
//     none; open after, or not}. The close is close_stream_handle
//     (requestStop, then AAudioStream_close); the open is
//     open_and_start_stream, OPEN AND START OR LEAVE NOTHING. THE ROADS:
//     RELEASE (release_device) {close, no open}; every reopen of a dead or
//     null stream (reopen_stream_if_dead: the launch gates, the car's Play,
//     NOISE's wake, launch_window's head, the release's two ends) {close
//     what stands, open} — ONE job, so the close precedes the open on one
//     thread; launch_window's post-publish dead arm {close, no open}; init
//     {none, open}; shutdown {close, no open}.
//   * THE BOUND. The screen thread posts the job and waits for its answer AT
//     MOST kDeviceJobWaitMs (run_device_job). INSIDE THE BOUND THE SCREEN
//     THREAD IS PARKED, so the worker's calls land in the order written
//     with nothing of the screen thread's between them — the worker is its
//     hands, every proof in this file reads as written — and the answer is
//     adopted at the wait's return: the synchronous result, the press what
//     it always was. init and shutdown wait WITH NO BOUND (a piece's open
//     and close keep waiting for Android: nothing is unsaved there).
//   * PENDING. Past the bound the device is PENDING until Android answers:
//     no stream is adopted (Impl::stream is null from the post), NOTHING IS
//     PUBLISHED (launch_window returns at its head), and no further job is
//     posted — ONE JOB IN FLIGHT, NO QUEUE, every road that would post
//     answering "nothing done" (shutdown excepted, which waits the job
//     out). NO QUEUED PLAY: the press plays at once or not at all. The
//     press's gate cards "Sound device is not answering"
//     (playback_lifecycle.h's chooser; the two overlays' launches card it
//     and rest where they were to start, the card block there), the sound
//     keys grey (device_absent_or_pending) and NOISE's wake is skipped. One
//     stderr line says the overrun and one the answer.
//   * THE ADOPTION. The answer is adopted ON THE SCREEN THREAD
//     (adopt_late_answer, seat_device_answer): at gui_main's tick, every
//     tick, ahead of its dead-stream poll and RELEASE's face settle — one
//     plain bool compare at rest — and at the head of every road that would
//     post (input drains ahead of the tick, so a press may arrive in the
//     pass the answer landed). It seats the stream or leaves none,
//     completes a release (the latch, its log line) and clears the pending
//     fact. AN UNADOPTED STREAM has had its start requested and accepted
//     and MAY already be calling back: with the playing bit down any
//     callback of it writes silence and the bed, exactly the keep-alive,
//     and stop()'s fence holds against it whether or not one has run (the
//     mark is Impl's; the proof's (d) at GuiPlayback::stop()).
// NOT BUILT (his ruling): a watchdog on missing callbacks, a queued or
// deferred play, an automatic release at the doze, a bound on a piece's open
// or close.
//
// THREADING, above playback.h's own thread model. Four threads touch this
// file:
//   * MAIN, the screen thread, owns the stream handle and every plain field
//     of Impl beside the worker's slot (THE SCREEN THREAD'S FIELDS, at
//     Impl): it posts the device jobs, waits for or adopts their answers,
//     publishes, stops, and posts the two underrun notes without waiting.
//     It makes no AAudio call on a stream. No lock guards those fields and
//     none is needed: no other thread reads them.
//   * THE DEVICE WORKER makes every AAudio call on a stream (THE RULE
//     above), the underrun reads among them. Of Impl it touches its slot
//     alone, under the worker's mutex (the job and the notes in, the answer
//     out), its own underrun floor (Impl::xrun_at_launch), plus what the
//     device has always written: the callbacks' registration (Impl's
//     address) and the engine's `output_rate` atomic. It reads no
//     screen-thread field — a job's and a note's inputs are copied in at
//     the post.
//   * The DATA CALLBACK thread runs the engine's render body, the noise mix
//     and the clip, and raises and lowers its own mark (Impl::in_callback,
//     stop()'s fence) — no allocation, no I/O, no locks, no AAudio call.
//   * The ERROR CALLBACK thread writes four atomics — the error, the rate,
//     the latch and the session word's playing bit, a seq_cst fence between
//     the latch and the bit — and logs. It calls NO
//     AAudio stream function, which is Google's own rule and the reason the
//     spike used a detached thread to do its closing; this backend needs no
//     detached thread because it does not close there at all — the next
//     launch press or NOISE's wake (ensure_device_available_for_play) or
//     play() hands the dead stream to the worker's close and reopens.
//   * THE RELEASE (release_device) is MAIN's: stop()'s fence, then the job
//     {close, no open}. `suppressed` is written at the post and the latch
//     at the adoption, once AAudioStream_close has joined the callback
//     thread, so no callback is alive to race the latch's store.

namespace {

const char* perf_mode_name(int32_t m) {
    switch (m) {
        case AAUDIO_PERFORMANCE_MODE_NONE:         return "NONE";
        case AAUDIO_PERFORMANCE_MODE_POWER_SAVING: return "POWER_SAVING";
        case AAUDIO_PERFORMANCE_MODE_LOW_LATENCY:  return "LOW_LATENCY";
        default:                                   return "?";
    }
}

const char* sharing_mode_name(int32_t m) {
    switch (m) {
        case AAUDIO_SHARING_MODE_EXCLUSIVE: return "EXCLUSIVE";
        case AAUDIO_SHARING_MODE_SHARED:    return "SHARED";
        default:                            return "?";
    }
}

// THE DEVICE JOB'S BOUND (architect 2026-11-26; THE RULE at the head): how
// long the screen thread waits for a device job's answer before the device
// is pending. THE ORDINARY TIMES, from his tablet's log (openStream called
// -> returned; requestStart called -> returned): Bluetooth, the Legacy path,
// 10–37 ms and 25–30 ms; the speaker, MMAP, 79–265 ms and 20–25 ms — so an
// open and its start take under 70 ms on Bluetooth and under 300 ms on the
// speaker, and one second is past anything an answering device takes. The
// stalls it is for ran 8 s and 10 s (the audio HAL retrying the car's A2DP
// start). init and shutdown wait with no bound.
constexpr int kDeviceJobWaitMs = 1000;

// THE DEVICE JOB, ONE SHAPE FOR EVERY ROAD (THE RULE at the head): what the
// screen thread hands the worker — COPIES, so the job reads no screen-thread
// field. `close` is the stream to close first, or none; `open` asks for the
// open and start after it (open_and_start_stream), or nothing; `source_rate`
// is the bound source's rate as it stood at the post, for the open's log
// line alone.
struct DeviceJob {
    AAudioStream* close       = nullptr;
    bool          open        = false;
    int           source_rate = 0;
};

// THE JOB'S ANSWER, what the worker hands back under its mutex: `stream` is
// the stream left standing — opened, its start requested and accepted — or
// none (no open asked, an open refused, or a start refused and the stream
// closed inside the job); `opened` says the open itself was granted a usable
// stream, whatever its start then answered (init's "the device came up").
struct DeviceAnswer {
    AAudioStream* stream = nullptr;
    bool          opened = false;
};

// AN UNDERRUN NOTE (architect 2026-11-26; the rule and its reason at
// report_xrun_count): one of the two underrun reads, handed to the worker
// with NO WAIT. STAMP is a publish's — the session's floor, launch_window's
// tail; REPORT is stop()'s tail. `stream` is the adopted stream's handle,
// copied at the post. THE STORE (Impl's `notes`) is kDeviceNoteSlots long,
// fixed and allocation-free: a play is one STAMP and a stop one REPORT, each
// run within microseconds by an answering worker, so the slots fill only
// while the worker is held inside Android as plays and stops go on.
enum class DeviceNoteKind { Stamp, Report };
struct DeviceNote {
    DeviceNoteKind kind   = DeviceNoteKind::Stamp;
    AAudioStream*  stream = nullptr;
};
constexpr int kDeviceNoteSlots = 8;

// HOW THE SCREEN THREAD TAKES AN ANSWER (take_device_answer): only if one is
// ready (the adoption's read), after at most the bound (a press's job), or
// whenever it comes (init, shutdown).
enum class DeviceTake { IfReady, Bounded, Unbounded };

}  // namespace

struct GuiPlayback::Impl {
    // The portable engine (playback_common.h). Everything else in this struct
    // is AAudio's own.
    //
    // THIS BACKEND REPORTS NO LATENCY FIGURE, and after 2026-09-03 there is
    // nothing on the other side of that to record: nothing compensates, the
    // raw predictor runs, and the line leads the sound by the route's own
    // latency (the ruling and its
    // arithmetic are at playback_common.cpp's record above
    // playback_publish_play). What stays worth saying is WHY no figure would
    // be published here even if one were wanted: in the car the tablet plays
    // over Bluetooth, whose link delay is large and variable, and the figures
    // the framework offers (AAudioStream_getTimestamp, its own latency
    // estimate) do not carry it — they are REPORTED AND UNTRUSTWORTHY on that
    // route rather than absent, so any figure would be a guess that is wrong
    // most of the time. On the tablet's own SPEAKER the latency is by contrast
    // stable and reportable (the 10–25 ms class), so a figure exists for one
    // route and not the other, and an answer that depends on where the sound
    // happens to be going is worse than no answer.
    //
    // WHAT THE EYE GETS ON THIS PLATFORM is smaller than the audio lead,
    // because the display lag runs the other way: the painted position is
    // ahead by L_audio and the pixel arrives L_display late, so the error at
    // light is L_audio − L_display — 10–25 ms against SurfaceFlinger's
    // 22–33 ms, roughly −23…+3 ms net. And IN THE CAR IT COSTS NOTHING AT ALL
    // (recorded 2026-09-02): there
    // the render player stands, and under it the waveform scanner is not
    // sampled or painted (main.cpp's pre-paint hook returns above it) — the
    // only moving picture is the modal row's play-scrub and its clock, neither
    // registered against a waveform.
    GuiPlaybackState state;

    // THE SCREEN THREAD'S FIELDS ARE ITS OWN (THE RULE at the head; the
    // THREADING block): `stream`, `device_ready`, `suppressed`, the job's
    // record below and `session_stamped` are read and written on the screen
    // thread alone — written at a device job's post and at its answer's
    // adoption (run_device_job, seat_device_answer), never by the worker,
    // which is handed copies (DeviceJob) and hands back an answer under its
    // mutex.
    //
    // `stream` is THE ADOPTED STREAM. Standing, its start was requested and
    // accepted (the lifecycle block at the head: open and start are one
    // body). Null, there
    // is none: never opened, refused, handed to a job's close (null from the
    // post, detach_stream), or opened by a job whose answer is not adopted
    // yet.
    AAudioStream* stream       = nullptr;
    // init() bound a source AND the device came up — the open granted a
    // stream, whatever its start answered — or the engine came up RELEASED
    // (the seated lamp, init's own arm), the stream then reopened at the
    // first press as after a drop. play()/stop() gate on it.
    bool          device_ready = false;
    // THE KEEP-ALIVE SUPPRESSED (architect 2026-11-26; the lifecycle block's
    // RELEASE paragraph): true from release_device until the first reopen
    // attempt (reopen_stream_if_dead, its one clearer, which logs the ender).
    // MAIN THREAD ONLY: one writer of the set (release_device), one of the
    // clear; read by device_released, the lamp's live fact. IT OUTLIVES
    // shutdown() because init() begins with shutdown() and the open's seat
    // runs before init, so the seat's fact must reach init's released arm;
    // across a project switch it is carried by the device key `lit_lamps`
    // (the lamp is the device's), this engine being per project
    // (run_project's own GuiPlayback) and the next one seated afresh.
    bool          suppressed   = false;
    // THE PENDING FACT (THE RULE at the head): A DEVICE JOB IS IN FLIGHT —
    // posted, its answer not adopted. Set at the post (run_device_job),
    // cleared at the adoption (seat_device_answer). Inside the bound the
    // screen thread is parked in the wait, so every other reader that finds
    // it true reads a job past its bound: device_pending's answer. ONE JOB
    // IN FLIGHT, NO QUEUE: while it stands no road posts (the precondition
    // at run_device_job; shutdown waits the job out). `job_closes` and
    // `job_opens` say which job it was, for the log's two lines;
    // `job_releases` says its adoption completes a release (release_device,
    // complete_release); `job_posted_at` is the post's instant on this
    // thread, from which the log's late answer line counts how long the
    // screen's call stood pending (seat_device_answer).
    bool          job_in_flight = false;
    bool          job_closes    = false;
    bool          job_opens     = false;
    bool          job_releases  = false;
    std::chrono::steady_clock::time_point job_posted_at{};
    // A STAMP WAS POSTED SINCE THE LAST REPORT (architect 2026-11-26; the
    // underrun notes, report_xrun_count): set at launch_window's tail, where
    // every publish posts its STAMP; cleared where stop() posts the REPORT
    // and wherever the stream leaves this thread's hands (detach_stream).
    // IT GATES THE REPORT: a stop with no session standing — none launched
    // since the last stop, or since the stream was adopted — posts nothing,
    // takes no lock and wakes nothing. Standing, it also says a stream is
    // adopted and no job is in flight: the stamp was posted past
    // launch_window's head, and every job's post clears it.
    bool          session_stamped = false;

    // Written by the error callback, read by the main thread. `dead` is the
    // disconnect latch: the stream is finished and only a close-and-reopen can
    // produce sound again.
    //
    // THE LATCH'S MAIN-THREAD WRITES (architect 2026-11-26, the device
    // worker; the error callback's own store is at the callback): CLEARED
    // ON THE SCREEN THREAD AT EVERY JOB'S POST (detach_stream) — after the
    // noise voice's reset where one is owed (close_dead_stream) and before
    // the job is handed over — and NEVER by the job or by the adoption.
    // At that moment the only stream that can exist is the one the job will
    // close: (1) a DEAD one whose error callback has already stored the
    // latch — this thread just read it true (the reopen's dead arm,
    // launch_window's post-publish arm) — so that callback's one remaining
    // write is the bit's lowering, which the job's close joins before the
    // job opens anything and before any publish (nothing is published while
    // a job is in flight, launch_window's head); (2) one closed with no
    // open after (RELEASE, shutdown), dead or LIVE — a live one's error
    // callback may still store the latch after this clear, over a stream
    // being closed, where RELEASE's adoption raises the latch anyway and
    // shutdown leaves no device, the next init's own shutdown clearing it
    // again over no stream; or (3) none. AN OPENING JOB IS POSTED ONLY IN
    // (1) AND (3) (reopen_stream_if_dead, which posts nothing over a live
    // stream; init, after its shutdown), so no store of an old stream's
    // callback can land on the new stream's latch, and no clear follows the
    // new stream's open: AN UNADOPTED STREAM WHOSE ERROR CALLBACK FIRED IS
    // ADOPTED AS AN ORDINARY DEAD STREAM, its latch standing. A start
    // refused on a stream whose error callback had fired leaves the latch
    // over no stream — the released state without its fact — which every
    // reader and the next reopen take as a drop's. RAISED ON THE SCREEN
    // THREAD by init's released arm (no stream) and BY RELEASE AT ITS
    // CLOSE'S ADOPTION (complete_release): the callback thread joined, so
    // gui_main's poll, which resets the noise voice's sequence on the
    // latch, meets no running fill.
    std::atomic<bool>    stream_dead{false};
    std::atomic<int32_t> stream_error{0};

    // A DATA CALLBACK IS INSIDE (architect 2026-11-26): stored true, seq_cst,
    // as the callback's first act past its null test, and cleared, seq_cst,
    // at every exit (InCallbackGuard), the whole callback under it — both
    // arms, the noise mix, the clip. Written by the callback thread alone,
    // read by stop()'s wait alone: the quiescence fence, whose proof is
    // stated once at GuiPlayback::stop().
    std::atomic<bool> in_callback{false};

    // THE SESSION'S UNDERRUN FLOOR, WORKER THREAD ONLY (the underrun notes,
    // report_xrun_count). The stream's own counter
    // is cumulative and monotonic for the stream's life, so what a session is
    // worth is a DIFFERENCE — and the subtrahend has to be the count read for
    // THE LAUNCH (its STAMP note), not for the previous stop: the stream stays
    // started between plays and keeps issuing silent callbacks (the lifecycle
    // block at the head of this file), so an underrun in an idle interval —
    // the whole stretch before the first play included — belongs to no
    // session at all and must not be charged to the audio the user just
    // heard. Both counts are read when the worker reaches each note —
    // ordinarily microseconds after the publish and the stop, in a stall
    // later — so the session is the difference of those two reads, not an
    // exact publish-to-stop count (report_xrun_count's THE SESSION).
    //
    // -1 IS NO SESSION STANDING, which is the state between a stop and the
    // next launch and the state a fresh stream opens in. THE WORKER'S WRITES
    // (run_device_note, device_worker_main): a publish's STAMP note seats the
    // launch's count, stop()'s REPORT note says the difference and voids it
    // back to -1, every device job voids it with the stream the counter
    // belongs to, and a dropped note voids it (the note store's drop rule,
    // below). Diagnostic only: no card, no state, no behaviour reads it.
    int32_t xrun_at_launch = -1;

    // THE WORKER'S SLOT (THE RULE at the head), every field of it under
    // `worker_mutex` alone: the job in — `job`, with `job_posted` true from
    // the post until the worker takes it — the answer out — `answer`, with
    // `answer_ready` true from the job's return until the screen thread
    // takes it — and the engine's end, `worker_quit` (the destructor's).
    // THE MUTEX IS HELD FOR THE HAND-OFFS ALONE, never across an AAudio
    // call, so the screen thread never waits on it for the device.
    // `worker_wake` is the worker's sleep (a job or a note posted, or the
    // end); `answer_wake` is the screen thread's wait (an answer ready). One
    // hand-off there and one back: two thread wakes a job, none at rest.
    // THE NOTE STORE (DeviceNote; the underrun notes, report_xrun_count) is
    // in the slot too: `notes[0, note_count)` in the order posted, appended
    // by the screen thread (post_device_note), which wakes the worker and
    // WAITS FOR NOTHING — one wake a note, no answer — and taken whole by
    // the worker, which runs them in that order AHEAD OF A JOB TAKEN WITH
    // THEM. THE DROP RULE: a note posted on a full store is not kept and
    // `note_dropped` is raised; the worker, at its next take, runs the
    // notes it holds — all posted before the drop — and then VOIDS THE
    // FLOOR, so a count is never said against a floor that is not its own
    // session's: a diagnostic lost in a stall, nothing else.
    std::mutex              worker_mutex;
    std::condition_variable worker_wake;
    std::condition_variable answer_wake;
    std::array<DeviceNote, kDeviceNoteSlots> notes{};
    int                     note_count   = 0;
    bool                    note_dropped = false;
    DeviceJob               job;
    bool                    job_posted   = false;
    DeviceAnswer            answer;
    bool                    answer_ready = false;
    bool                    worker_quit  = false;
    // THE DEVICE WORKER (device_worker_main): started in the engine's
    // constructor, joined in its destructor after shutdown.
    std::thread             worker;
};

namespace {

// THE CALLBACK'S EXIT MARK (Impl::in_callback; the proof at
// GuiPlayback::stop()): clears the mark, seq_cst, at every exit of
// playback_data_callback past its first act — noise.cpp's InFillGuard's shape.
struct InCallbackGuard {
    std::atomic<bool>& flag;
    ~InCallbackGuard() { flag.store(false, std::memory_order_seq_cst); }
};

aaudio_data_callback_result_t playback_data_callback(AAudioStream* /*stream*/,
                                                     void* user,
                                                     void* audio_data,
                                                     int32_t num_frames) {
    auto* impl = static_cast<GuiPlayback::Impl*>(user);
    if (!impl) return AAUDIO_CALLBACK_RESULT_STOP;
    // THE MARK, FIRST (stop()'s fence, its proof at GuiPlayback::stop()):
    // stored true before the gate's load below, both seq_cst, and cleared at
    // every exit by the guard.
    impl->in_callback.store(true, std::memory_order_seq_cst);
    const InCallbackGuard guard{impl->in_callback};

    // ONE INTERLEAVED BUFFER, so the two channels are the same base pointer one
    // float apart and the stride is the channel count
    // (playback_write_silence's contract). init refuses a stream that was not
    // granted exactly two channels, so this pair is always the whole buffer.
    float* out = static_cast<float*>(audio_data);
    float* channel_buffers[kPlaybackOutputChannels] = { out, out + 1 };
    const int     channel_count = impl->state.channels;
    const int64_t frames        = static_cast<int64_t>(num_frames);

    // THE GATE is ONE load of the session word (playback_common.h), seq_cst
    // and after the mark's store above — the order stop()'s fence rests on
    // (GuiPlayback::stop()) — and an acquire, as every seq_cst load is:
    // the callback gates on its playing bit and
    // hands that same word to the render body as its terminal's expected
    // value and as the one generation whose command packet the fill may
    // consume — loaded here and nowhere later in the burst, so a stop or a
    // publish after this load changes the word and fails the fill's
    // terminal, the fill renders under its own generation's window
    // regardless, and the next callback acquires and seats the new
    // publication.
    const uint64_t session_word =
        impl->state.session.load(std::memory_order_seq_cst);
    if (!playback_session_playing(session_word)) {
        playback_write_silence(channel_buffers, channel_count,
                               kPlaybackOutputChannels, 0, frames);
        // CONTINUE, never STOP: this arm is what the stream spends most of its
        // life in (the lifecycle block at the head — the stream stays
        // started between plays), and a stopped-by-the-callback stream would
        // both break the lifecycle's one fact — a standing stream is one
        // whose start was accepted and never stopped — and put a restart
        // into the next play. NO SAMPLE OF
        // THE BOUND BUFFER IS READ HERE — the noise below reads its own bank —
        // which is what a callback that reads the bit down owes stop()'s
        // fence (GuiPlayback::stop()) and what makes an idle stream harmless
        // to a buffer the main thread is about to replace.
    } else {
        playback_render_block(impl->state, session_word, channel_buffers,
                              kPlaybackOutputChannels, frames, channel_count);
    }
    // THE SECOND VOICE AND THE CLIP, ON BOTH ARMS (2026-10-28,
    // playback_common.h's head): the noise summed under what the arm wrote
    // (the render arm's block the project voice, unscaled at 0 dB) — the
    // stream runs between plays, so the bed plays there too — then every
    // sample clamped. A BLOCK THAT CLAMPED COUNTS (2026-11-05, the clip
    // lamp: GuiPlaybackState::clipped_blocks, one release increment the GUI
    // tick reads with acquire; no allocation, no syscall).
    if (impl->state.noise != nullptr)
        noise_mix_block(*impl->state.noise, channel_buffers,
                        kPlaybackOutputChannels, frames, channel_count,
                        impl->state.output_rate.load(
                            std::memory_order_relaxed));
    if (playback_clip_block(channel_buffers, kPlaybackOutputChannels, frames,
                            channel_count))
        impl->state.clipped_blocks.fetch_add(1, std::memory_order_release);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void playback_error_callback(AAudioStream* /*stream*/, void* user,
                             aaudio_result_t error) {
    auto* impl = static_cast<GuiPlayback::Impl*>(user);
    if (!impl) return;

    // THE DISCONNECT (a headphone or USB DAC pulled, a route change the
    // framework cannot follow). The error code is deliberately NOT switched
    // on: a documented Android bug reports TIMEOUT where DISCONNECTED is
    // meant, so ANY error means the stream is finished.
    //
    // Four atomic writes, one fence and a log line; NO AAudio call, per the
    // head comment's threading rule. Clearing `output_rate` puts the engine
    // in its SUSPENDED state, which is exactly the behaviour playback.h's
    // graph-suspension clause asks for: the render body would emit silence and
    // hold, and cursor() holds at the last audio position instead of
    // extrapolating. Lowering the session word's playing bit makes
    // is_playing() false, so the run loop's next tick reads this as a natural
    // end and tears the scanner down through the product's one stop body. A
    // fill in flight that reaches its natural end after this finds the word
    // changed under its gate's value and abandons its terminal (the word's
    // comment, playback_common.h), so the lowering this arm performs is the
    // one that stands; and the next reopen (the launch press's, or play()'s
    // own head) closes this stream, and play() publishes a fresh generation,
    // so no session is left stranded behind the lowered flag.
    //
    // THE LATCH IS PUBLISHED BEFORE THE BIT IS LOWERED, WITH A seq_cst FENCE
    // BETWEEN (2026-09-01, the disconnect outranking a concurrent publish):
    // play() publishes a fresh playing generation and then re-checks the
    // latch behind its own seq_cst fence, and the two-case argument at that
    // site rests on this order — a publish store here could otherwise land
    // between play()'s dead check and its publish and be OVERWRITTEN by the
    // fresh generation, leaving `stream_dead` true, `output_rate` 0 and the
    // playing bit up forever (the source view's tick has no device fork to
    // notice). With the latch first, whichever thread's fence is earlier in
    // the fences' total order, the other sees its write: play() either sees
    // the latch and lowers the bit it just published, or its publish is
    // ordered before this fetch_and, which lowers it.
    //
    // NOTHING RECOVERS BY ITSELF: no auto-resume, no reconnect timer, no
    // retry. The next LAUNCH PRESS closes this stream and opens a new one at
    // the new device's own granted rate — the main window's three launch
    // gates through ensure_device_available_for_play, the render player's
    // road through play()'s own head (reopen_stream_if_dead, one body for
    // both) — or NOISE's wake, the level case's lift over the dead stream
    // (ensure_device_available_for_play, architect 2026-11-24). The user
    // presses play or taps NOISE again — the same answer the product gives
    // every other rare, loud fault. THE NOISE BED FOLLOWS THIS RULE TOO
    // (architect 2026-10-28; the sequence alone, 2026-11-05): the
    // voice's sequence resets, audible or not, at the first main-thread act
    // that reads this latch — gui_main's poll on GuiPlayback::stream_dead at
    // the next tick, or, a press reaching the device first, the reopen's own
    // close of the dead stream (close_dead_stream) — its level and switch
    // untouched, so the bed is silent until that next press reopens and
    // resumes on the new stream with a fresh segment (noise.h's head, the
    // two roads).
    impl->stream_error.store(static_cast<int32_t>(error), std::memory_order_relaxed);
    impl->state.output_rate.store(0, std::memory_order_relaxed);
    impl->stream_dead.store(true, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    impl->state.session.fetch_and(~kSessionPlayingBit, std::memory_order_release);

    std::fprintf(stderr,
        "warptempo_gui: AAudio stream error %d (%s); stream is dead, playback "
        "stopped. The next play press reopens the device.\n",
        static_cast<int>(error), AAudio_convertResultToText(error));
}

// THE JOB'S CLOSE (THE RULE at the head) — WORKER THREAD ONLY. THE FILE'S ONE
// requestStop call, and it is here because the stream is being CLOSED
// (shutdown, the dead-stream reopen, RELEASE, or a start refused inside the
// job's own open) — never between plays. The close is the stronger fence
// anyway: it blocks until the callback thread is gone, which is what makes
// it safe on a stream this function may be stopping while a callback is
// mid-flight.
void close_stream_handle(AAudioStream* s) {
    AAudioStream_requestStop(s);
    AAudioStream_close(s);  // blocks until the callback thread is gone
}

// THE JOB'S OPEN: OPEN AND START, OR LEAVE NOTHING (THE RULE and the
// lifecycle block at the head) — WORKER THREAD ONLY. Opens one output stream,
// publishes its GRANTED rate as the engine's output rate, says the two log
// lines and requests the start; a refused start request closes the stream
// here, so the answer's stream is one whose start was accepted, or none.
// EVERY SETTER IS A REQUEST:
// the rate, the performance mode and the sharing mode are all asked for and
// read back, and only the format and the channel count are load-bearing
// enough to refuse over — the render body writes interleaved stereo floats
// and has no other shape. IT READS NO SCREEN-THREAD FIELD: `source_rate` is
// the job's copy, and of `impl` it takes the address (the callbacks' user
// pointer) and the engine's `output_rate` atomic alone. THE LATCH IS NOT
// TOUCHED HERE: it was cleared at the job's post, before this stream
// existed, and from the open on it is the new stream's error callback's
// alone (Impl::stream_dead).
void open_and_start_stream(GuiPlayback::Impl& impl, int source_rate,
                           DeviceAnswer& answer) {
    AAudioStreamBuilder* builder = nullptr;
    aaudio_result_t r = AAudio_createStreamBuilder(&builder);
    if (r != AAUDIO_OK || builder == nullptr) {
        std::fprintf(stderr,
            "warptempo_gui: AAudio stream builder failed (%s); "
            "playback disabled.\n", AAudio_convertResultToText(r));
        return;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    // SHARED, not EXCLUSIVE: exclusive mode is refused outright on the USB
    // path and invites extra disconnects everywhere, and it buys nothing the
    // engine can use.
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    // LOW_LATENCY is a REQUEST and the granted mode is logged below. It costs
    // nothing when refused (the framework falls back to NONE) and shortens the
    // gap between a play() and its first sound when granted, which is the
    // whole feel of auditioning an edit.
    AAudioStreamBuilder_setPerformanceMode(builder,
                                           AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, kPlaybackOutputChannels);
    // UNSPECIFIED, and the granted rate becomes output_rate: asking for 44100
    // on 48 k hardware only moves the resample into the framework, where it is
    // neither ours to reason about nor better than the render body's own.
    AAudioStreamBuilder_setSampleRate(builder, AAUDIO_UNSPECIFIED);
    AAudioStreamBuilder_setDeviceId(builder, AAUDIO_UNSPECIFIED);
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_MEDIA);
    AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_MUSIC);
    AAudioStreamBuilder_setDataCallback(builder, playback_data_callback, &impl);
    AAudioStreamBuilder_setErrorCallback(builder, playback_error_callback, &impl);

    AAudioStream* stream = nullptr;
    r = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (r != AAUDIO_OK || stream == nullptr) {
        std::fprintf(stderr,
            "warptempo_gui: AAudio openStream failed (%s); playback disabled.\n",
            AAudio_convertResultToText(r));
        return;
    }

    const int32_t        rate     = AAudioStream_getSampleRate(stream);
    const int32_t        chans    = AAudioStream_getChannelCount(stream);
    const aaudio_format_t format  = AAudioStream_getFormat(stream);
    if (rate <= 0 || chans != kPlaybackOutputChannels ||
        format != AAUDIO_FORMAT_PCM_FLOAT) {
        std::fprintf(stderr,
            "warptempo_gui: AAudio granted an unusable stream "
            "(rate=%d, channels=%d, format=%d; wanted PCM_FLOAT stereo at any "
            "rate); playback disabled.\n",
            static_cast<int>(rate), static_cast<int>(chans),
            static_cast<int>(format));
        AAudioStream_close(stream);
        return;
    }
    answer.opened = true;

    // Two bursts of buffering. One burst underruns on any scheduling hiccup;
    // more than two buys latency the audition does not want.
    const int32_t burst = AAudioStream_getFramesPerBurst(stream);
    if (burst > 0) AAudioStream_setBufferSizeInFrames(stream, burst * 2);

    // THE GRANTED RATE, PUBLISHED BEFORE THE START: the first callback reads
    // it (the noise mix), and the post stored 0 before this job began.
    impl.state.output_rate.store(static_cast<uint32_t>(rate),
                                 std::memory_order_relaxed);

    // getHardwareSampleRate is API 34 and minSdk here is 30, so it is a WEAK
    // reference behind __builtin_available (the build passes
    // -D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__, without which a newer symbol
    // is a hard build error no guard can open). Its delta from the granted
    // rate is exactly how much framework resampling is happening UNDER the
    // render body's own — worth knowing in the log, worth nothing if absent.
    int32_t hardware_rate = -1;
    if (__builtin_available(android 34, *)) {
        hardware_rate = AAudioStream_getHardwareSampleRate(stream);
    }

    std::fprintf(stderr,
        "warptempo_gui: Audio backend: AAudio, output_sample_rate=%d, "
        "source_sample_rate=%d, channels=%d\n",
        static_cast<int>(rate), source_rate,
        static_cast<int>(chans));
    std::fprintf(stderr,
        "warptempo_gui: AAudio granted: burst=%d, buffer=%d, perf=%s, "
        "sharing=%s, deviceId=%d, hwRate=%d\n",
        static_cast<int>(burst),
        static_cast<int>(AAudioStream_getBufferSizeInFrames(stream)),
        perf_mode_name(AAudioStream_getPerformanceMode(stream)),
        sharing_mode_name(AAudioStream_getSharingMode(stream)),
        static_cast<int>(AAudioStream_getDeviceId(stream)),
        static_cast<int>(hardware_rate));

    // THE START, IN THE OPEN'S OWN BODY (the lifecycle block at the head): a
    // standing stream is one whose start was accepted. The request is
    // asynchronous — accepted, the stream is STARTING or STARTED, its first
    // callback perhaps not yet run, and nothing waits for one (stop()'s
    // fence holds whichever stream calls). No stream this call can meet
    // is STOPPING — the file's one requestStop is close_stream_handle's, on a
    // stream about to be closed — so the state is never waited out here. A
    // REFUSED START CLOSES THE STREAM HERE, the engine suspended again
    // first, and the answer carries none: the ordinary no-stream state, which
    // the next press reopens.
    r = AAudioStream_requestStart(stream);
    if (r != AAUDIO_OK) {
        std::fprintf(stderr,
            "warptempo_gui: AAudio requestStart failed (%s); playback "
            "stopped.\n", AAudio_convertResultToText(r));
        impl.state.output_rate.store(0, std::memory_order_relaxed);
        close_stream_handle(stream);
        return;
    }
    answer.stream = stream;
}

// The underrun notes' body (defined beside report_xrun_count, below).
void run_device_note(GuiPlayback::Impl& impl, const DeviceNote& note);

// THE DEVICE WORKER'S BODY (THE RULE at the head): asleep on `worker_wake`
// until a job or a note is posted or the engine ends. The notes and a job
// are taken under the mutex and RUN WITH THE MUTEX RELEASED: THE NOTES FIRST,
// IN THE ORDER POSTED, THEN THE JOB — its close first, then its open, so the
// close precedes the open on this one thread — and the job's answer is
// stored under the mutex and announced (a note has no answer). The slot
// holds at most one job and one answer, by the screen thread's own rule (one
// job in flight, no queue: run_device_job).
// A NOTE NEVER READS A CLOSED STREAM: notes are posted only on an adopted
// stream with no job in flight (Impl::session_stamped; launch_window's tail
// stands past its head's check), so every note in the slot beside a job was
// posted before that job, and the job that closes a note's stream is posted
// after the note and run after it — here, or at a later take. A worker held
// inside a note's read is a worker held inside Android as in a job: a job
// posted behind it waits its bound and the device reads pending (THE RULE).
// THE FLOOR IS VOIDED at a dropped note (the note store's drop rule, at
// Impl) and AT EVERY JOB: the underrun counter belongs to the stream, the
// next one opens at zero and has had no session on it, so the floor goes
// with the stream the job closes, and a fresh stream starts with none.
void device_worker_main(GuiPlayback::Impl* impl) {
    for (;;) {
        std::array<DeviceNote, kDeviceNoteSlots> notes;
        int       note_count = 0;
        bool      dropped    = false;
        DeviceJob job;
        bool      have_job   = false;
        {
            std::unique_lock<std::mutex> lock(impl->worker_mutex);
            impl->worker_wake.wait(lock, [impl] {
                return impl->note_count > 0 || impl->job_posted ||
                       impl->worker_quit;
            });
            notes      = impl->notes;
            note_count = impl->note_count;
            dropped    = impl->note_dropped;
            have_job   = impl->job_posted;
            if (have_job) job = impl->job;
            impl->note_count   = 0;
            impl->note_dropped = false;
            impl->job_posted   = false;
            // THE END is the destructor's, after shutdown has adopted every
            // answer, so neither a job nor a note waits behind it.
            if (note_count == 0 && !have_job) return;
        }
        for (int i = 0; i < note_count; ++i)
            run_device_note(*impl, notes[static_cast<size_t>(i)]);
        if (dropped) impl->xrun_at_launch = -1;
        if (!have_job) continue;
        impl->xrun_at_launch = -1;
        DeviceAnswer answer;
        if (job.close != nullptr) close_stream_handle(job.close);
        if (job.open) open_and_start_stream(*impl, job.source_rate, answer);
        {
            std::lock_guard<std::mutex> lock(impl->worker_mutex);
            impl->answer       = answer;
            impl->answer_ready = true;
        }
        impl->answer_wake.notify_one();
    }
}

// THE POST'S HEAD — SCREEN THREAD, every device job's first step
// (run_device_job): the adopted stream leaves this thread's hands — the
// handle returned is the job's to close, or none stood — and the engine is
// left as with no device.
AAudioStream* detach_stream(GuiPlayback::Impl& impl) {
    AAudioStream* s = impl.stream;
    impl.stream = nullptr;
    // Suspended for the engine the moment the device leaves (the error
    // callback's own clause), so a cursor read across the close holds. The
    // job's open stores the granted rate after this store: the post precedes
    // the job.
    impl.state.output_rate.store(0, std::memory_order_relaxed);
    // THE LATCH IS CLEARED HERE, AT THE POST, AND WITH NO STREAM TOO (the
    // rule and why the clear is safe: Impl::stream_dead): the released state
    // is the latch over no stream (release_device), and its reopen's post
    // here is what leaves a refused reopen the ordinary dead state — no
    // stream, no latch — as it leaves a drop's.
    impl.stream_dead.store(false, std::memory_order_relaxed);
    impl.stream_error.store(0, std::memory_order_relaxed);
    // NO SESSION'S STAMP OUTLIVES ITS STREAM ON THIS THREAD
    // (Impl::session_stamped): a stop after this posts no REPORT. The floor
    // itself is the worker's, voided in the job this head posts
    // (device_worker_main). Every stream is opened by a job posted past
    // this head — init() shuts down first and the dead-stream reopen's job
    // closes ahead of its open — so this is the whole seed a fresh stream
    // needs.
    impl.session_stamped = false;
    return s;
}

// THE JOB IN FLIGHT, NAMED for the log's two lines (Impl::job_closes /
// job_opens, written at the post).
const char* device_job_name(const GuiPlayback::Impl& impl) {
    if (!impl.job_closes) return "an open";
    return impl.job_opens ? "a close and open" : "a close";
}

// TAKE THE WORKER'S ANSWER OUT OF ITS SLOT — SCREEN THREAD; answers whether
// one was taken. The mutex is the worker's hand-off lock, held by it for a
// store alone, so IfReady costs one uncontended lock and the two waits sleep
// on `answer_wake` until the job's return (Bounded: or kDeviceJobWaitMs).
bool take_device_answer(GuiPlayback::Impl& impl, DeviceTake how,
                        DeviceAnswer& out) {
    std::unique_lock<std::mutex> lock(impl.worker_mutex);
    const auto ready = [&impl] { return impl.answer_ready; };
    switch (how) {
        case DeviceTake::IfReady:
            break;
        case DeviceTake::Bounded:
            impl.answer_wake.wait_for(
                lock, std::chrono::milliseconds(kDeviceJobWaitMs), ready);
            break;
        case DeviceTake::Unbounded:
            impl.answer_wake.wait(lock, ready);
            break;
    }
    if (!impl.answer_ready) return false;
    out               = impl.answer;
    impl.answer_ready = false;
    return true;
}

// THE RELEASED STATE, COMPLETED AT THE CLOSE'S ADOPTION (release_device's
// step 3; Impl::stream_dead's rule): the latch raised over no stream — the
// job's close has joined the callback thread, so gui_main's poll, which
// resets the noise voice's sequence on this latch, meets no running fill —
// and the log's line, said where its fact became true.
void complete_release(GuiPlayback::Impl& impl) {
    impl.stream_dead.store(true, std::memory_order_release);
    std::fprintf(stderr,
        "warptempo_gui: Sound device released: stream closed, the "
        "keep-alive suppressed until a play, the app's return or a "
        "second press.\n");
}

// THE ADOPTION'S ONE BODY (THE RULE at the head) — SCREEN THREAD: the job's
// answer becomes the screen thread's state. The stream is seated or left
// null (a seated one's start was accepted: the open's own body); the
// pending fact is cleared; a release's close completes the released state
// (complete_release).
// THE LATCH IS NOT CLEARED HERE: a stream whose error callback fired before
// its adoption is seated as an ordinary dead stream, the next press's to
// reopen (Impl::stream_dead). `late` is an answer that came past the bound,
// and says the log's answer line: N is the time FROM THE JOB'S POST ON THIS
// THREAD TO THIS ADOPTION (Impl::job_posted_at) — how long the screen's call
// stood pending, notes the worker ran ahead of the job included; the
// adoption is within one tick of the answer (the tick's, or shutdown's wait
// at the answer itself) — and the line says what the job left.
void seat_device_answer(GuiPlayback::Impl& impl, const DeviceAnswer& answer,
                        bool late) {
    impl.stream        = answer.stream;
    impl.job_in_flight = false;
    if (late) {
        const long long pending_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - impl.job_posted_at).count();
        std::fprintf(stderr,
            "warptempo_gui: Sound device answered %s after %lld ms, leaving "
            "%s.\n", device_job_name(impl), pending_ms,
            answer.stream != nullptr ? "a stream" : "no stream");
    }
    if (impl.job_releases) {
        impl.job_releases = false;
        complete_release(impl);
    }
}

// THE ADOPTION OF AN ANSWER THAT CAME PAST THE BOUND (THE RULE at the head;
// GuiPlayback::adopt_device_answer is gui_main's tick's face of this) — SCREEN
// THREAD. AT REST ONE PLAIN BOOL COMPARE on a screen-thread field: no atomic,
// no lock. While a job is in flight — which outside run_device_job's own
// wait means past its bound — one read of the worker's slot, and the seat
// once the answer is there. Called at the tick and at the head of every road
// that would post (reopen_stream_if_dead, release_device).
void adopt_late_answer(GuiPlayback::Impl& impl) {
    if (!impl.job_in_flight) return;
    DeviceAnswer answer;
    if (take_device_answer(impl, DeviceTake::IfReady, answer))
        seat_device_answer(impl, answer, /*late=*/true);
}

// POST ONE DEVICE JOB AND WAIT FOR ITS ANSWER (THE RULE at the head) — SCREEN
// THREAD, the one road onto the worker: every AAudio control call this file
// makes begins here. The job closes what stands (detach_stream, the post's
// head) and opens after, or not (`open`).
// ONE JOB IN FLIGHT, NO QUEUE — THE PRECONDITION: no job is in flight. Every
// caller has asked: reopen_stream_if_dead and release_device answer "nothing
// done" while one is, shutdown waits it out, init follows its own shutdown,
// and launch_window's dead arm stands past a head that answered a stream
// standing. A job with nothing to close and nothing to open asks nothing of
// Android and is adopted here, the worker left asleep.
// THE WAIT: `how` is Bounded on every press's road and Unbounded for init
// and shutdown. INSIDE THE BOUND THE SCREEN THREAD IS PARKED HERE, so the
// worker's calls run in the order written with none of this thread's
// between them, and the answer is adopted before this returns — the
// synchronous result. PAST THE BOUND this says the log's overrun line and
// returns with the job in flight: THE DEVICE IS PENDING (Impl::job_in_flight)
// until adopt_late_answer seats the answer. Returns the answer adopted in
// this call — over a pending job an empty one, no stream.
DeviceAnswer run_device_job(GuiPlayback::Impl& impl, bool open,
                            DeviceTake how) {
    AAudioStream* const close = detach_stream(impl);
    DeviceAnswer answer;
    if (close == nullptr && !open) {
        seat_device_answer(impl, answer, /*late=*/false);
        return answer;
    }
    impl.job_in_flight = true;
    impl.job_closes    = close != nullptr;
    impl.job_opens     = open;
    impl.job_posted_at = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(impl.worker_mutex);
        impl.job        = DeviceJob{close, open, impl.state.source_rate};
        impl.job_posted = true;
    }
    impl.worker_wake.notify_one();
    if (take_device_answer(impl, how, answer)) {
        seat_device_answer(impl, answer, /*late=*/false);
        return answer;
    }
    std::fprintf(stderr,
        "warptempo_gui: Sound device is not answering: %s still running "
        "after %d ms; the device is pending until Android answers.\n",
        device_job_name(impl), kDeviceJobWaitMs);
    return DeviceAnswer{};
}

// THE DEAD STREAM'S CLOSE, the two sites that read the latch true on the main
// thread and close what it names: reopen_stream_if_dead's dead arm (the job
// opens after) and launch_window's post-publish dead arm (it does not). THE
// NOISE VOICE'S SEQUENCE IS RESET HERE, ON THE SCREEN THREAD, AHEAD OF THE
// POST, whose head clears the latch (detach_stream): the latch was read true
// on this thread just before (each caller's own acquire load), no fill runs
// on a dead stream, and the reopen that follows is the job this thread
// posts after the reset, its new callback thread started by the worker
// after the post — the precondition of noise_voice_reset_on_dead_stream
// (noise.h, the first exception at GuiNoiseVoice). NOT at the post itself: a
// project switch (init's shutdown) and the app's shutdown close a LIVE
// stream, and the bed survives those (noise.h's thread note). The voice is
// attached before the device opens (GuiPlaybackState::noise, run_project's
// first act on its engine); the null test is that field's own contract,
// null mixing nothing.
void close_dead_stream(GuiPlayback::Impl& impl, bool open_after) {
    if (impl.state.noise != nullptr)
        noise_voice_reset_on_dead_stream(*impl.state.noise);
    run_device_job(impl, open_after, DeviceTake::Bounded);
}

// THE DEAD-STREAM REOPEN, ONE BODY FOR TWO CALLERS (the disconnect rule at the
// error callback): a dead stream is closed and a dead or null one reopened
// HERE — one device job posted from the main thread, {close what stands,
// open}, so the worker closes before it opens — at the new device's own
// granted rate, OPENED AND STARTED (the open's one body). Answers whether a
// stream stands after it. Called at the head of the one launch road
// (launch_window, which play() and play_loop() both forward to) — the
// render player's road, which reaches play() with no gate ahead of it — and
// by ensure_device_started, the body of ensure_device_available_for_play, the
// reopen at the press (architect 2026-09-02), whose callers are inventoried
// once at its declaration (playback.h: the launch gates, the car's Play and
// NOISE's wake), and of end_device_release, the release's own ends: before
// it the gates READ the latch ahead of play() and so never let play() reopen,
// leaving the tablet mute behind the card after every route drop. A failed
// reopen leaves playback disabled until the next press — no retry, no timer —
// and, with `device_ready` still true, is exactly the "no stream standing"
// shape device_unavailable reads.
// A LIVE STREAM STANDING POSTS NOTHING: the ordinary play's road through
// here is a few plain compares and the latch's load — no lock of the
// worker's, no wake.
// WHILE A JOB IS IN FLIGHT IT DOES NOTHING (THE RULE at the head: one job in
// flight, no queue): the head adopts an answer that has landed (a press may
// arrive in the pass it did, ahead of the tick); with none, it answers
// false — nothing reopened, nothing posted, `suppressed` left as it stands.
// A REOPEN THAT OVERRUNS ITS BOUND answers false too, the device pending.
// THE DEAD ARM RESETS THE NOISE VOICE'S SEQUENCE FIRST (2026-10-28, the
// no-auto-recover rule's second road, noise.h's head; the sequence alone,
// 2026-11-05): close_dead_stream below, ahead of the post that clears
// the latch, so a press that reaches here before gui_main's poll has read
// the drop — a press on either road — ends the old sequence instead of
// resuming it mid-segment on the new stream; an audible bed starts a fresh
// segment there, the reopen its resume.
// IT ENDS A RELEASE (architect 2026-11-26; the lifecycle block's RELEASE
// paragraph): the one body every reopen runs — the play's (launch_window's
// head), the press's (ensure_device_started: the launch gates, the car's
// Play, NOISE's wake) and the release's own two ends (the return and the
// second press, GuiPlayback::end_device_release) — so `suppressed` is
// cleared HERE, at the attempt, and the lamp cannot outlive it: a refused
// open ends the suppression too (the user ended it) and leaves the ordinary
// dead state. `ender` names the road in the one logcat line said at that
// clear (the car test's evidence). The released state is the latch over no
// stream (release_device), so the dead arm below takes it as a drop's: the
// noise voice's sequence reset, then the job — nothing to close there, the
// post clearing the latch — and its open.
bool reopen_stream_if_dead(GuiPlayback::Impl& impl, const char* ender) {
    adopt_late_answer(impl);
    if (impl.job_in_flight) return false;
    if (impl.suppressed) {
        impl.suppressed = false;
        std::fprintf(stderr,
            "warptempo_gui: Sound device release ended by %s; reopening the "
            "stream, the keep-alive with it.\n", ender);
    }
    const bool dead = impl.stream_dead.load(std::memory_order_acquire);
    if (!dead && impl.stream != nullptr) return true;
    if (dead) close_dead_stream(impl, /*open_after=*/true);
    else      run_device_job(impl, /*open=*/true, DeviceTake::Bounded);
    return impl.stream != nullptr;
}

// THE PLAY'S ENDER WORD (reopen_stream_if_dead's `ender`): every reopen at a
// launch, a car's Play or NOISE's wake.
constexpr const char* kReleaseEndedByPlay = "a play or a wake";

// THE PRESS'S REOPEN, ONE BODY (the contract at
// ensure_device_available_for_play, whose body this is): the press's road and
// the release's two ends (end_device_release) share it, differing in the
// ender word alone. The reopen opens AND starts (the open's one body), so
// the answer is device_unavailable's own reading taken after it: a stream
// standing — its start accepted — and no latch. A job in flight, an overrun
// and a refused open or start all leave no stream and answer false.
bool ensure_device_started(GuiPlayback::Impl& impl, const char* ender) {
    if (!impl.device_ready) return false;
    reopen_stream_if_dead(impl, ender);
    return !(impl.stream_dead.load(std::memory_order_acquire) ||
             impl.stream == nullptr);
}

// THE UNDERRUN COUNT, SAID ONCE PER SESSION THAT HAD ONE (architect
// 2026-09-02). AAudio's own counter is the only
// evidence this backend has that its buffer — burst × 2, one burst of slack —
// lost a callback to a scheduling hiccup, and without this line nothing reads
// it: an underrun on this platform is simply an audible tick nobody can
// account for afterwards. So stop() asks for it at the tail of its fence and
// ONE stderr line is printed — which on this backend IS a logcat line, every
// diagnostic going down the redirected fd the log pump drains
// (platform_android.cpp's logcat sink) — WHEN THE SESSION LOST SOMETHING, so
// a clean session says nothing at all.
//
// THE READ IS THE DEVICE WORKER'S, HANDED OVER AS A NOTE THE SCREEN THREAD
// NEVER WAITS FOR (architect 2026-11-26; THE RULE at the head of this file:
// the worker makes every AAudio call on a stream). WHY: on the MMAP path
// (the tablet's speaker) AAudioStream_getXRunCount returns a member of the
// client stream (AudioStreamInternal::getXRunCount), but ON THE LEGACY PATH
// (Bluetooth: the car, the headphones) it is AudioStreamTrack::getXRunCount
// -> AudioTrack::getUnderrunCount(), which takes AudioTrack::mLock — and the
// track's own callback thread holds that lock ACROSS BINDER CALLS INTO THE
// AUDIO SERVER (restoreTrack_l from processAudioBuffer / obtainBuffer after
// the track is invalidated, its createTrack retried with 500 ms sleeps;
// restartIfDisabled's start in releaseBuffer). So in exactly the car's
// stall a Play or a Stop on the screen thread could wait on that lock, and a
// diagnostic must not be able to hold the screen. TWO NOTES (DeviceNote,
// post_device_note): a publish posts STAMP at launch_window's tail and
// stop() posts REPORT at its own, each one lock of the worker's, one append
// and one wake, no wait and no answer; the worker runs them in order, ahead
// of any job posted after them (device_worker_main), and OWNS THE FLOOR
// (Impl::xrun_at_launch). The count is therefore read when the worker
// reaches the note — at once from an answering device; in a stall, when
// Android lets go.
//
// THE SESSION IS THE INTERVAL BETWEEN THE WORKER'S TWO READS — the STAMP note
// of the last publish and the REPORT note of this stop, each read when the
// worker reaches it (above): ordinarily microseconds after the publish and
// the stop, so the interval is the session; in a stall later, so the figure
// may take in idle frames after the stop or miss the session's start. A
// difference of two samples, not an exact publish-to-stop count. That is
// what the launch floor buys (xrun_at_launch, the field's own contract): the
// stream runs and callbacks silence between plays, so subtracting the
// PREVIOUS STOP's figure instead would charge every idle underrun — and the
// whole interval before the first play — to the audio the user just heard.
// The stream-lifetime figure rides in the same line, unsubtracted, because it
// is the one number that says whether this device drops frames at all.
//
// A DIAGNOSTIC AND NOTHING MORE: no notification card, no status line, no
// engine behaviour. An underrun is already audible, and the product cannot
// grow the buffer without giving up the LOW_LATENCY mode the car's transport
// wants — so the honest answer is to be able to name what happened, not to
// react to it. A negative return (the call unimplemented on a path, an
// invalid stream) is not a count and is dropped.
//
// THE LINE IS SAID because this stream is inside a tablet the app is the only
// window onto: nothing else reports its xruns.
//
// THE REPORT NOTE'S BODY — WORKER THREAD ONLY.
void report_xrun_count(GuiPlayback::Impl& impl, AAudioStream* stream) {
    // NO SESSION STANDING on the worker's side: the floor voided since the
    // stamp — by a job, or by a dropped note. (A stop with no launch since
    // the last one posts no REPORT at all: Impl::session_stamped, the screen
    // thread's gate.) There is nothing to charge, and the idle stream's own
    // underruns are exactly what must not be said here.
    if (impl.xrun_at_launch < 0) return;
    const int32_t count   = AAudioStream_getXRunCount(stream);
    const int32_t session = count - impl.xrun_at_launch;
    // THE SESSION ENDS HERE whatever the figure, so the floor is voided before
    // the report: a second stop with no play between it and this one says
    // nothing, rather than re-saying this session under a stale floor.
    impl.xrun_at_launch = -1;
    // The counter only ever RISES for a live stream and the floor is never
    // negative (the stamp's own clamp), so one test covers the clean session
    // and the non-count alike: a difference at or below zero is either no
    // news or a negative return — the call unimplemented on a path, an
    // invalid stream.
    if (session <= 0) return;
    std::fprintf(stderr,
        "warptempo_gui: AAudio underruns: %d this session, %d since the "
        "stream opened\n",
        static_cast<int>(session), static_cast<int>(count));
}

// ONE UNDERRUN NOTE, RUN — WORKER THREAD ONLY (device_worker_main). STAMP
// seats the session's floor at the count as the worker reads it: a negative
// return is not a count, so the floor takes zero and the report's own
// compare drops the pair. REPORT is report_xrun_count above.
void run_device_note(GuiPlayback::Impl& impl, const DeviceNote& note) {
    if (note.kind == DeviceNoteKind::Report) {
        report_xrun_count(impl, note.stream);
        return;
    }
    const int32_t at_launch = AAudioStream_getXRunCount(note.stream);
    impl.xrun_at_launch = at_launch > 0 ? at_launch : 0;
}

// POST ONE UNDERRUN NOTE — SCREEN THREAD, NO WAIT (the rule at
// report_xrun_count; the store and its drop rule at Impl's slot): one lock
// of the worker's, held for the append alone, and one wake. The note carries
// the adopted stream's handle as it stands. PRECONDITION, stated and not
// asserted: a stream is adopted and no job is in flight — launch_window's
// tail stands past its head's check, and stop() posts only under
// Impl::session_stamped.
void post_device_note(GuiPlayback::Impl& impl, DeviceNoteKind kind) {
    {
        std::lock_guard<std::mutex> lock(impl.worker_mutex);
        if (impl.note_count == kDeviceNoteSlots) {
            impl.note_dropped = true;
        } else {
            impl.notes[static_cast<size_t>(impl.note_count++)] =
                DeviceNote{kind, impl.stream};
        }
    }
    impl.worker_wake.notify_one();
}

}  // namespace

// THE DEVICE WORKER'S LIFETIME IS THE ENGINE'S (THE RULE at the head): started
// here, asleep until the first job; ended in the destructor AFTER shutdown,
// which has waited out and adopted every job, so the worker is asleep with
// nothing posted when it is told to end and joined.
GuiPlayback::GuiPlayback() : impl_(std::make_unique<Impl>()) {
    impl_->worker = std::thread(device_worker_main, impl_.get());
}
GuiPlayback::~GuiPlayback() {
    shutdown();
    {
        std::lock_guard<std::mutex> lock(impl_->worker_mutex);
        impl_->worker_quit = true;
    }
    impl_->worker_wake.notify_one();
    impl_->worker.join();
}

// The forward (contract at the declaration): before init, so no data
// callback exists to race the store.
void GuiPlayback::attach_noise(GuiNoiseVoice* voice) {
    impl_->state.noise = voice;
}

bool GuiPlayback::init(int sample_rate, int channels, const float* samples,
                       int64_t total_frames, int64_t domain_offset) {
    shutdown(); // idempotent

    if (!playback_bind_and_validate(impl_->state, sample_rate, channels,
                                    samples, total_frames, domain_offset)) {
        return false;
    }
    // THE OPEN WITH RELEASE SEATED LIT (architect 2026-11-26: the sticky
    // lamp, seated before this init by GuiInputHandler::seat_lit_lamps,
    // which reaches release_device while no device stands): the engine
    // comes up IN THE RELEASED STATE — bound, `device_ready`, NO STREAM
    // OPENED, the latch up over no stream exactly as release_device leaves
    // it — so nothing streams, the noise bed included, until the first of
    // the release's three ends reopens through reopen_stream_if_dead, and
    // every launch gate meets the state as it meets a drop. shutdown()
    // above leaves `suppressed` standing; nothing runs, so this thread
    // writes the latch alone.
    if (impl_->suppressed) {
        impl_->device_ready = true;
        impl_->stream_dead.store(true, std::memory_order_release);
        std::fprintf(stderr,
            "warptempo_gui: Sound device released at the open: no stream "
            "opened until a play, the app's return or a second press.\n");
        return true;
    }
    // THE OPEN, ON THE WORKER, WITH NO BOUND (THE RULE at the head: a piece's
    // open keeps waiting for Android): one job, {none, open} — shutdown()
    // above left no stream and no job in flight — adopted before the wait
    // returns. OPENED AND STARTED IN ONE BODY (open_and_start_stream), so
    // the stream's one start happens here — at launch, under no audition —
    // instead of at the first play (the lifecycle block at the head of this
    // file). THE DEVICE CAME UP IFF THE OPEN WAS GRANTED A STREAM
    // (DeviceAnswer::opened): a refused open leaves the engine unbound and
    // absent; a refused START has already logged and closed its stream, and
    // playback is not disabled by it — `device_ready` with no stream, the
    // ordinary no-stream state, which the next press reopens.
    const DeviceAnswer answer =
        run_device_job(*impl_, /*open=*/true, DeviceTake::Unbounded);
    if (!answer.opened) {
        playback_clear_binding(impl_->state);
        return false;
    }
    impl_->device_ready = true;
    return true;
}

void GuiPlayback::play(int64_t start_sample, int64_t end_sample) {
    launch_window(start_sample, end_sample, kPlaybackNoLoop);
}

void GuiPlayback::play_loop(int64_t start_sample, int64_t end_sample,
                            int64_t loop_begin) {
    launch_window(start_sample, end_sample, loop_begin);
}

bool GuiPlayback::consume_loop_wrap() {
    if (!impl_) return false;
    return playback_consume_loop_wrap(impl_->state);
}

// The one launch road behind both faces (the contract at playback.h): the
// head check, the publish and the post-publish race check below are written
// once here, and the two faces differ in the packet's third word alone.
void GuiPlayback::launch_window(int64_t start_sample, int64_t end_sample,
                                int64_t loop_begin) {
    if (!impl_->device_ready) return;

    // THE HEAD CHECK: a dead stream closed and a dead or null one reopened,
    // opened AND started (reopen_stream_if_dead, the one body the launch
    // press's ensure_device_available_for_play also calls). It stays here
    // even though the main window's gates have already run it at the press —
    // the render player's play road has no gate ahead of this call, and the
    // two-case argument below rests on this check standing at the head. A
    // failed reopen returns silently: the gates have carded, and the two
    // overlays' roads read device_pending right after this returns (a
    // pending device rests them where they were to start, carded) and
    // device_unavailable at their ticks.
    // NOTHING IS PUBLISHED WHILE A DEVICE JOB IS IN FLIGHT (THE RULE at the
    // head of this file): the reopen answers false over a job in flight —
    // one past its bound, or this call's own reopen overrunning — and this
    // returns HERE, so nothing raises the playing bit from a job's post to
    // its answer's adoption. (Every post finds the bit down or coming down:
    // stop() ahead of RELEASE's, the lowerings ahead of shutdown's and of
    // the post-publish arm's below, and over a dead stream the error
    // callback's own, which the job's close joins before it opens
    // anything.) Every publish therefore follows the adoption of every job
    // posted before it, and a stream the worker has opened but this thread
    // has not adopted is never handed a session: it writes silence and the
    // bed. NO QUEUED PLAY: the press plays at once or not at all.
    if (!reopen_stream_if_dead(*impl_, kReleaseEndedByPlay)) return;

    // THE PUBLISH MAKES NO DEVICE CALL: past the head a stream stands, and a
    // standing stream is one whose start was accepted (the lifecycle block
    // at the head), so
    // the command packet and the session word that releases it are all the
    // callback's gate is waiting to see. The main
    // thread's window mirror, its cursor and the launch anchor — the publish
    // instant, nothing waiting on a seat — are predictor state the callback
    // never reads; a fill consumes only the packet its own generation
    // qualifies.
    if (!playback_publish_play(impl_->state, start_sample, end_sample,
                               loop_begin))
        return;

    // THE DISCONNECT OUTRANKS THE PUBLISH (2026-09-01). The dead check at the
    // head of this call and the publish just above are two steps, and the
    // error callback can run between them: its lowering of the playing bit
    // would then be OVERWRITTEN by the fresh generation the publish stored,
    // leaving the latch true, the rate 0 and the bit up on a stream that
    // will never call back — the scanner live forever at the held cursor.
    // So the latch is asked AGAIN here, behind a seq_cst fence, and the
    // error callback stores the latch BEFORE its fetch_and behind a fence of
    // its own. TWO CASES, by the fences' total order: (1) this fence precedes
    // the callback's — then the publish store, sequenced before this fence,
    // is what the callback's fetch_and (sequenced after its fence) modifies,
    // and it lowers the bit this publish set; (2) the callback's fence
    // precedes this one — then the latch store, sequenced before the
    // callback's fence, is visible to this load, and the bit is lowered
    // here. Either way the published bit comes down, and the road taken is
    // the lost device's: lower the bit, close the dead stream — through
    // close_dead_stream, so the noise voice is reset as at the head's reopen
    // (this load read the latch true; noise.h's head, the second road), the
    // job {close, no open} — and the next press reopens. A close that
    // overruns its bound leaves the device pending with the bit already
    // down. THE THIRD LEG, which the fences do not cover: the
    // callback stores the latch, fences, and is preempted BEFORE its
    // fetch_and while this call sees the latch at its head, has the dead
    // stream closed and a new one opened, and publishes the fresh generation
    // — the late fetch_and would then lower the NEW session's bit. What
    // excludes it is the job's AAudioStream_close (close_stream_handle, on
    // the worker), which returns only after the callback thread has
    // left (the error callback runs on that thread — the header's own rule
    // that it may call neither requestStop nor close is the self-join it
    // would be), so the delayed fetch_and has landed before the close
    // returns — and the close precedes the job's open, the job's return
    // precedes its answer's adoption, and the publish follows the adoption
    // (the head, above): so it has landed before the reopen and the publish.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (impl_->stream_dead.load(std::memory_order_acquire)) {
        impl_->state.session.fetch_and(~kSessionPlayingBit,
                                       std::memory_order_release);
        close_dead_stream(*impl_, /*open_after=*/false);
        return;
    }

    // THE SESSION'S UNDERRUN FLOOR, asked for at the publish's own tail
    // because a launch's session begins here: the stream has been running
    // and callbacking since the last stop, and the underruns of that idle
    // interval are not the audio about to be heard (report_xrun_count, above,
    // is the one reader). EVERY PUBLISH RE-STAMPS IT, a republish over a live
    // session included — the console's seek (GuiPlaybackLifecycle::
    // seek_live_session_by, architect 2026-11-20) and the render player's
    // live reseek — so the stop's underrun line counts from the LAST publish,
    // not from the play's launch. THE STAMP IS A NOTE TO THE DEVICE WORKER,
    // POSTED AND NOT WAITED FOR (the rule and its reason at
    // report_xrun_count): the worker reads the count when it reaches the note
    // — ordinarily microseconds from here, in a stall later — and keeps the
    // floor, so the line's session is the difference of the worker's two
    // reads (report_xrun_count's THE SESSION).
    // The stream stands here, adopted, no job in flight — the head's reopen
    // answered true — and the stop that ends this session posts its REPORT
    // under the bool set here.
    impl_->session_stamped = true;
    post_device_note(*impl_, DeviceNoteKind::Stamp);
}

void GuiPlayback::resync_predictor() {
    if (!impl_) return;
    playback_resync_predictor(impl_->state);
}

void GuiPlayback::stop() {
    if (!impl_->device_ready) return;
    // THE DEVICE IS NOT TOUCHED HERE: the stream keeps running and the
    // project voice keeps writing silence (the noise bed, when audible, still
    // plays), which is the whole of the lifecycle at the head of this file.
    // Lower the playing bit, keeping the generation (the session word's
    // comment, playback_common.h: a fill in flight can commit no terminal
    // once this has landed — its exchange expects exactly the word its gate
    // acquired, playing bit up, and this fetch_and changed that word) — then
    // THE QUIESCENCE FENCE below. Then, with no callback in the render arm,
    // the session's UNDERRUN COUNT is asked for where a session stands — a
    // REPORT note posted to the device worker and not waited for, which says
    // the line if the count rose between the worker's read for the publish's
    // STAMP and its read for this REPORT (report_xrun_count, above — a
    // logcat line and nothing else). A
    // stop with no session posts nothing (Impl::session_stamped).
    //
    // THE QUIESCENCE FENCE WAITS ONLY ON OUR OWN CODE (architect 2026-11-26:
    // "stop() waits only while our own callback is inside, never for the
    // device to call again"; the noise voice's protocol, GuiNoise::
    // change_folder's step 4). The data callback marks itself inside
    // (Impl::in_callback): stored true, seq_cst, as its first act, and
    // cleared, seq_cst, at every exit (InCallbackGuard); its gate's one load
    // of the session word is seq_cst and follows that store. Here the
    // lowering is seq_cst and the mark is loaded, seq_cst, after it, the wait
    // yielding while it reads true. THE PROOF, stated here once (every other
    // site cites this one):
    //  (a) NO CALLBACK IS IN THE RENDER ARM WHEN stop() RETURNS, AND NONE
    //      ENTERS IT UNTIL THE NEXT PUBLISH. Four seq_cst operations stand
    //      in one total order: the callback's mark store, then its word load;
    //      this lowering, then the mark load below. Either the callback's
    //      word load follows the lowering — it reads the bit down (the only
    //      writes that raise the bit are this thread's own publishes, each
    //      sequenced before this stop or after it), takes the silence arm and
    //      reads no sample of the bound buffer — or it precedes the lowering,
    //      so its mark store precedes the mark load, which reads that store
    //      or a later write of the mark — that callback's own clear or a
    //      later callback's write — so by the time the load reads false that
    //      callback has left. A session the terminal ended is the second
    //      case: its callback read the bit up, so its load precedes this
    //      fetch_and, which writes the word whatever bit it finds.
    //  (b) WHAT A FILL DID IS ORDERED BEFORE WHAT THE CALLER DOES NEXT. The
    //      exit's clear is seq_cst, so the false the mark load reads is the
    //      clear of the last callback whose mark store preceded that load
    //      (or the mark's first false, no callback ever inside), and the load
    //      synchronizes with it: every read and store that fill and every
    //      fill before it made — the samples, the cursor, the cycle stamp,
    //      the wrap count, a terminal, the audio-thread-private window —
    //      happens before stop() returns, so before anything the caller
    //      mutates after it. THIS IS WHAT rebind_buffer, THE BUFFER'S FREE AND
    //      THE WRAP COUNT'S LEVEL REST ON (playback_rebind_buffer;
    //      playback_publish_play's THE WRAP COUNT A SESSION STARTS FROM).
    //  (c) THE WAIT IS MADE ONLY OF OUR OWN CALLBACKS' RUNTIMES. The
    //      callback makes no blocking call — no lock, no allocation, no I/O,
    //      no AAudio call (the head's THREADING block) — so each raising of
    //      the mark lasts one callback's own runtime. The wait ends at the
    //      first load that finds no callback inside; a load can find the
    //      NEXT callback inside (it entered after the lowering, reads the
    //      bit down and touches no sample, but its mark is up), so
    //      back-to-back callbacks can each add their own runtime — ordinarily
    //      the wait is one callback's or none. NO FUTURE CALLBACK IS NEEDED
    //      TO END IT, AND NO STATE OF THE DEVICE CAN EXTEND IT: a stream that
    //      stops calling leaves the mark down and stop() returns at once, as
    //      it does over a dead stream or none. The evidence: the car's stall
    //      of 2026-11-26, the stream's start accepted and its data callback
    //      uncalled for 10 s (architect 2026-11-26).
    //  (d) THE MARK IS ONE BOOL BECAUSE AT MOST ONE STREAM'S DATA CALLBACK
    //      THREAD EXISTS AT A TIME, ADOPTED OR NOT. The device worker makes
    //      every close and every open, one job at a time (one job in
    //      flight, no queue: run_device_job), and a job closes before it
    //      opens; an opening job is posted only where no stream stands but
    //      the one it is handed to close (init after its own shutdown;
    //      reopen_stream_if_dead over no stream or a dead one), and
    //      AAudioStream_close returns only once the callback thread has
    //      left, its last clear landed. So (a)-(c) hold as well against a
    //      stream the worker has opened whose answer this thread has not
    //      adopted (THE RULE at the head of this file): its callbacks raise
    //      and lower this same mark, Impl's, and each reads the bit down —
    //      nothing is published while a job is in flight (launch_window's
    //      head) — so a stop() or a rebind over a pending device is fenced
    //      as over an adopted one. The ERROR callback's thread touches no
    //      sample and is not marked.
    impl_->state.session.fetch_and(~kSessionPlayingBit,
                                   std::memory_order_seq_cst);
    while (impl_->in_callback.load(std::memory_order_seq_cst))
        std::this_thread::yield();
    if (impl_->session_stamped) {
        impl_->session_stamped = false;
        post_device_note(*impl_, DeviceNoteKind::Report);
    }
}

bool GuiPlayback::is_playing() const {
    return playback_is_playing(impl_->state);
}

// THE THREE WAYS THIS DEVICE CANNOT SOUND (contract at the declaration), and
// they are one answer because they are one fact to the consumer:
//  - `device_ready` false — init() never opened a stream at all, or shutdown()
//    has closed it; play() is its documented silent no-op;
//  - the error callback's `stream_dead` latch — a device that went away under
//    a running stream (a headphone pulled, a Bluetooth route dropped), or the
//    latch RELEASE raises over the stream it closed (release_device);
//  - no stream standing with `device_ready` still true — what a REFUSED REOPEN
//    leaves behind: the reopen (reopen_stream_if_dead, at a launch press or
//    at play()'s head) closes a dead stream (clearing the latch) and returns
//    when nothing can be opened or started. That is the same lost device one
//    step later. AND WHAT A PENDING DEVICE READS AS, with no term of its own
//    (THE RULE at the head of this file): `stream` is null from a job's post
//    until its answer's adoption, so while a device job is in flight past
//    its bound this answers true — no adopted stream, nothing will sound.
// Answering false on any of them would let the render player's tick read the
// silence as a natural end and walk the folder with nothing to play it on.
// A READ ONLY (contract at the declaration): the reopen is the press's, below.
bool GuiPlayback::device_unavailable() const {
    if (!impl_ || !impl_->device_ready) return true;
    return impl_->stream_dead.load(std::memory_order_acquire) ||
           impl_->stream == nullptr;
}

// THE PRESS'S REOPEN (contract at the declaration; architect 2026-09-02): the
// head of play(), hoisted to the launch gates so a dead stream is reopened
// where the main window asks its device question instead of read and carded.
// THE QUESTION IS WHETHER THE DEVICE WILL SOUND, AND THE OPEN'S OWN BODY
// ANSWERS IT: a reopened stream is opened AND started in one device job
// (open_and_start_stream; the lifecycle block at the head of this file), and
// a refused start request closes it there, so a stream standing after the
// reopen is one whose start was accepted and there is no stream object that
// would not sound for this member to see past. The start therefore never
// falls to play() — after its publish, where a refusal would lower the
// playing bit under a launch body that had already seeded the scanner and
// returned true: a silent false launch with no card. A stream already
// running (the ordinary play) posts no job and makes no device call here.
// The reopened stream's start happens at the launch press, ahead of play()'s
// publish; a render player's Play, which has no gate, reaches the same
// reopen in play() itself.
// The answer is device_unavailable's own reading taken AFTER the reopen — the
// latch cleared at the job's post, the stream standing or not — so the gates
// card exactly when nothing stands to sound: nothing could be opened or
// started, or THE DEVICE IS PENDING (THE RULE at the head) — a job in flight
// past its bound, this press's own reopen or an earlier road's, under which
// nothing is attempted — and the gate's chooser names which
// (playback_device_refusal_card, playback_lifecycle.h). A device that never
// came up (`device_ready` false) is not opened by a press: init's failure
// logged its line, play() is the documented silent no-op, and this answers
// false at once as the read does. The error callback may still fire between
// this reopen and play()'s publish; play()'s own head check and its
// post-publish fence answer that, unchanged.
// The body is ensure_device_started's (above), shared with the release's two
// ends; this road's ender word is the play's.
bool GuiPlayback::ensure_device_available_for_play() {
    if (!impl_) return false;
    return ensure_device_started(*impl_, kReleaseEndedByPlay);
}

// RELEASE (contract at the declaration; the lifecycle block's RELEASE
// paragraph at the head of this file). THE SEQUENCE:
//   1. stop(), on main — the playing bit lowered and THE FENCE (its proof at
//      GuiPlayback::stop()), so no callback is inside the bound buffer when
//      it returns, however the device stands; the session's underrun line
//      asked for as at any stop, its note run ahead of the close below;
//   2. the job {close, no open}, posted from main and run on the device
//      worker (THE RULE at the head) — the file's one requestStop and
//      AAudioStream_close, which returns only once the callback thread has
//      left (the same close shutdown() and the dead-stream reopen use): the
//      handle is freed with no callback alive, the error callback's
//      included. `suppressed` IS SET AT THE POST, so the lamp lights with
//      the press whatever Android then takes;
//   3. AT THE CLOSE'S ADOPTION (complete_release) the latch raised over no
//      stream — the drop's state for every reader: device_unavailable true,
//      stream_dead true (gui_main's poll resets the noise voice's sequence
//      on it, no fill running), `output_rate` 0 (the post's), the playing
//      bit down (stop's) — and the log's line.
// INSIDE THE BOUND the three are one press, complete when this returns. A
// CLOSE THAT OVERRUNS leaves the device pending with the lamp lit: the
// released state completes at the adoption (the tick's), and until then
// nothing reopens — the release's ends answer "nothing done" over a job in
// flight.
// Over a stream already dead or absent (a drop, a refused reopen) the same
// three run, the job closing what stands; with nothing standing no job is
// posted and the state completes at once. With no device standing yet
// (the open's seat, ahead of init: GuiInputHandler::seat_lit_lamps calls this
// directly, a seat and no toggle) only the fact is set, nothing is posted,
// and init comes up released. ALREADY RELEASED, it does nothing — the seat's
// no-op and the reason the seat can call it unconditionally on a lit word.
// WHILE A DEVICE JOB IS IN FLIGHT IT DOES NOTHING (one job in flight, no
// queue): its key is greyed then (device_absent_or_pending), and the seat
// runs before init, where none can be.
void GuiPlayback::release_device() {
    if (!impl_ || impl_->suppressed) return;
    adopt_late_answer(*impl_);
    if (impl_->job_in_flight) return;
    if (impl_->device_ready) {
        stop();
        impl_->suppressed   = true;
        impl_->job_releases = true;
        run_device_job(*impl_, /*open=*/false, DeviceTake::Bounded);
        return;
    }
    impl_->suppressed = true;
}

// THE RELEASE'S OWN TWO ENDS (contract at the declaration): the press's
// reopen with the end's word, no play. Over a device job in flight — the
// release's own close still unanswered — the reopen attempts nothing and
// this answers false, `suppressed` standing (reopen_stream_if_dead).
bool GuiPlayback::end_device_release(GuiDeviceReleaseEnd why) {
    if (!impl_ || !impl_->suppressed) return false;
    return ensure_device_started(
        *impl_, why == GuiDeviceReleaseEnd::Return ? "the app's return"
                                                   : "a second press");
}

// The lamp's live fact (contract at the declaration).
bool GuiPlayback::device_released() const {
    return impl_ && impl_->suppressed;
}

// THE FACE'S READ (contract at the declaration): the first of
// device_unavailable's three ways — init never opened a stream, or
// shutdown() closed it — OR THE DEVICE PENDING (device_pending below). The
// latch and the null stream of a refused reopen are what a launch press
// reopens, so a face that counted them would grey a button whose press
// plays; under a pending device the press would do nothing.
bool GuiPlayback::device_absent_or_pending() const {
    return !impl_ || !impl_->device_ready || impl_->job_in_flight;
}

// THE DEVICE IS PENDING (contract at the declaration; THE RULE at the head
// of this file): Impl::job_in_flight, the screen thread's own field — a job
// posted whose answer is not adopted, which outside run_device_job's wait is
// a job past its bound.
bool GuiPlayback::device_pending() const {
    return impl_ && impl_->job_in_flight;
}

// THE TICK'S ADOPTION (contract at the declaration): the file's one adoption
// body, at rest one plain bool compare.
void GuiPlayback::adopt_device_answer() {
    adopt_late_answer(*impl_);
}

// THE DROP'S LATCH ALONE (contract at the declaration): the error callback's
// `stream_dead`, set once per drop (and by RELEASE over the stream it
// closed, at its close's adoption, and init's released arm) and cleared by
// the first reopen attempt (the post's head, detach_stream, inside
// reopen_stream_if_dead; the latch's main-thread writes are inventoried at
// Impl::stream_dead) — never the null stream a refused reopen or a pending
// device leaves, which device_unavailable also counts.
bool GuiPlayback::stream_dead() const {
    return impl_ && impl_->stream_dead.load(std::memory_order_acquire);
}

int64_t GuiPlayback::cursor() const {
    if (!impl_) return 0;
    return playback_cursor(impl_->state);
}

// The clip lamp's read (contract at the declaration): the callback's
// counter, acquire.
uint64_t GuiPlayback::clipped_blocks() const {
    if (!impl_) return 0;
    return impl_->state.clipped_blocks.load(std::memory_order_acquire);
}

GuiPlaybackPosition GuiPlayback::position_at(int64_t at_ns) const {
    if (!impl_) return GuiPlaybackPosition{};
    return playback_position_at(impl_->state, at_ns);
}

int64_t GuiPlayback::domain_begin() const {
    if (!impl_) return 0;
    return playback_domain_begin(impl_->state);
}

int64_t GuiPlayback::domain_end() const {
    if (!impl_) return 0;
    return playback_domain_end(impl_->state);
}

void GuiPlayback::rebind_buffer(const GuiBoundAudio& buffer) {
    if (!impl_) return;
    playback_rebind_buffer(impl_->state, buffer);
}

// THE CLOSE, ON THE WORKER, WITH NO BOUND (THE RULE at the head of this
// file: a piece's close keeps waiting for Android; nothing is unsaved
// there). A JOB IN FLIGHT IS WAITED OUT FIRST AND ITS ANSWER ADOPTED — the
// one road that meets a job in flight and does not answer "nothing done" —
// so the stream that job left standing is the one closed below and nothing
// of the worker's outlives this call. Then the job {close, no open} stops
// the stream and joins the callback thread before the binding is cleared;
// with no stream standing nothing is posted.
void GuiPlayback::shutdown() {
    if (!impl_) return;
    if (impl_->job_in_flight) {
        DeviceAnswer late;
        take_device_answer(*impl_, DeviceTake::Unbounded, late);
        seat_device_answer(*impl_, late, /*late=*/true);
    }
    impl_->state.session.fetch_and(~kSessionPlayingBit,
                                   std::memory_order_relaxed);
    run_device_job(*impl_, /*open=*/false, DeviceTake::Unbounded);
    impl_->device_ready = false;
    playback_clear_binding(impl_->state);
}
