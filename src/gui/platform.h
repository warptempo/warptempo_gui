#pragma once

// The platform seam's ONE include: the backend class is GuiPlatform,
// platform_android.h's — THE ONE BACKEND since the laptop GUI's drop
// (architect 2026-10-30: the tablet the one platform, the GL canvas the one
// road). A consumer includes this and never the backend header.
//
// THE LOOP CONTRACT (architect 2026-08-27, with the project model): ONE
// GuiPlatform per process, MANY run() calls. gui_main (main.cpp) constructs
// the backend and calls init() once, then runs a loop whose every iteration
// builds a project's whole object set, installs its callbacks, calls run(),
// and tears the set down when run() returns. run() RETURNS FOR TWO REASONS —
// an EXIT (request_exit, or the backend's own activity-destroyed end;
// exit_requested() reads true afterwards and the
// loop leaves) and a RUN STOP (request_run_stop — a reopen: the Open project
// picker's, or Revert's on the project already open;
// the window and the input core stand, and the next iteration's run() picks
// them up as they are). should_exit_ is process-scoped and never reset,
// run_stop_requested_ is cleared at the head of every run().
//
// WHAT SURVIVES A RUN STOP, and why each may: the window, its EGL surface and
// the backbuffer (the same window shows the next project);
// has_initial_configure() (the window is adopted, and the next iteration's
// first tick loads on it); the pending damage and any pending vsync callback
// (a callback in
// flight finds the open's hold below standing and presents nothing, and the
// new set's first painted frame is the hold's one frame, which damages the
// whole window);
// the timerfd and its cadence; the input core's modifier state (the physical
// keyboard's live truth), its pointer position and focus, and its touch
// contact bookkeeping (a finger still down on the picker row it tapped to
// choose the project lifts into the next set as an ordinary release, which
// finds no claim and does nothing — the picker is field-less and paints no
// on-screen keyboard, so a row or its Cancel button is the whole surface a
// finger can be resting on). The worker completion fds are the per-project
// workers' own and are RE-REGISTERED by the next iteration and forgotten by
// the teardown (each setter takes -1); the callbacks are re-installed since
// they capture the new objects. NOTHING IS RESET FOR THE CORE'S KEY REPEAT,
// ITS TOUCH WINDOW OR A POINTER CAPTURE, and that is by construction rather
// than by a cancel road: a reopen happens from a key press or a button lift
// on the Open project picker, a modal — no pointer gesture is live, no capture can
// stand (a capture is the nav drag's, refused under a modal), a finger that
// committed opened no touch window (the picker lies off the pan zone and
// outside any editor field, so its down was the pointer on contact — the
// only touch windows left are the waveform's region hold and the editor
// field's), and the key that committed is a session key the repeat probe never arms. The
// next set's first geometry is redelivered explicitly (redeliver_geometry),
// since no INIT_WINDOW comes for the window already adopted.
// PLAYBACK DOES NOT SURVIVE: it is per project, shut down and re-inited with
// the set, so a reopen costs one AAudio stream start (its one transient), the
// accepted price.
//
// THE OPEN'S PRESENTATION HOLD (architect 2026-10-27: an open "paints from
// left to right ... it looks kind of sloppy ... we could withhold the painting
// until it's ready to paint the whole thing"; nothing is shown for the load
// itself, his loads being under half a second; THE HOLD SHOWS THE EMPTY
// WAVEFORM, his ruling of the same night, in place of the last frame the
// glass was shown — the picker frozen mid-tap, or a Revert's prompt): FROM
// THE MOMENT A PROJECT OPEN IS CERTAIN UNTIL THE NEW PROJECT'S FIRST
// COMPLETE PICTURE, EXACTLY ONE FRAME IS PRESENTED — THE NEW SET'S PICTURE
// WITH NO PROJECT LOADED, at the load's entry — and then nothing until the
// finished picture replaces it in one present. ONE RULE FOR EVERY ROAD IN:
// the cold launch, the picker's choice and Revert (a dirty reopen's prompt
// answered, on either) and the pull's reload. THREE BITS — the hold,
// `presentation_held()`, its one frame's debt, and the owed release (the
// release's bullet):
//   * ARMED BY THE PLATFORM ALONE, the hold and its debt together, at the
//     two points an open becomes certain: CONSTRUCTION (the cold launch's
//     first open) and request_run_stop (every run stop is a reopen — the
//     picker's choice answered, Revert, the pull's reload through Revert's
//     road). A
//     cancelled picker or a prompt's Cancel requests no stop and arms
//     nothing; the render entry's and the `h` view's loads in place stop no
//     run and arm nothing.
//   * THE ONE FRAME, present_held_frame(), asked by the GUI at the load's
//     entry (run_project's tick, main.cpp, which says what the frame
//     carries) and nowhere else, so it is painted by THE NEW SET — a fresh
//     AppState with no audio, which is what makes it carry no state of
//     either project — on a configured window. It pays the debt, damages the
//     whole surface and paints and presents it PAST THE HOLD'S GATE AND AT
//     ONCE (posted outside the vsync gate, touching neither of the gate's
//     bits), SYNCHRONOUSLY BECAUSE THE LOAD THAT FOLLOWS BLOCKS THE LOOP: a
//     frame left to the loop's own pacing (the vsync callback) could be
//     answered only after the load returned, beside the finished picture.
//     IT LEAVES THE HOLD STANDING: the frame is a paint, not a release, so
//     presentation_held() reads true across it and input stays dropped. At a
//     cold launch it is the window's first buffer — the glass leaves the
//     system's starting window (the theme's black, Theme.NoTitleBar) for it.
//     Called once per arming;
//     its one caller is gated once per set (main.cpp), and a second call is
//     a program bug (the debt's assert).
//   * WHILE IT STANDS, NO OTHER PAINT AND NO PRESENT: paint_one_frame returns
//     before the pre-paint hook and before any pixel, but for the one
//     frame's own call (its argument), so no as-painted record claims a
//     picture the glass never showed (the cheapest correct paint is none);
//     the damage accumulates and nothing schedules a frame for it (no vsync
//     callback is posted). A surface lost and readopted under it (TERM/INIT)
//     only adds its full damage.
//   * RELEASED BY THE GUI ALONE, release_presentation(), from the loop's
//     tick once the project's load has returned and the waveform plate the
//     paint handler holds is the live view's (GuiPaintHandler::
//     live_plate_published, whose declaration says what COMPLETE checks and
//     why); the release declares the whole window's damage and the frame it
//     brings paints and presents it whole. A RELEASE UNDER A PENDING RUN
//     STOP IS REFUSED: the old set's tick, running in the pass that asked
//     the stop, would otherwise find its own picture complete and present
//     it — the hold a stop arms is the next set's to release. THE ROAD OUT
//     (recorded at release_presentation, platform_android.cpp): the hold
//     reads true until the finished picture is presented — the release is
//     marked OWED and the hold left standing, the pass's tail posts the
//     vsync callback as for any damage, the paint's hold gate lets that
//     frame through, and the hold and the owed release clear together only
//     at its successful post (2026-10-28, Sol's round) — so input stays
//     dropped through the vsync wait and a car command drained in the
//     release's own pass.
//   * A ROAD THAT NEVER RELEASES ENDS THE PROCESS: a refused load exits
//     (fatal_load, run_project), and an exit needs no picture. No timeout
//     and no recovery code: the plate's worker always completes (NO
//     BACKSTOPS).
//   * INPUT WAITS FOR THE PICTURE: while the hold stands the GUI's press-
//     class hooks drop what they are handed (run_project's hook block,
//     main.cpp, which lists them) — a press, a key, a wheel, a touch
//     gesture's begin or a car button would act on the one frame, a
//     picture with no piece, or on a piece the glass has not shown (ON
//     SCREEN IS AS PAINTED). Releases, motion, focus, the close request and
//     Ctrl+Q pass: they find no claim, or end the process.
// What the glass shows meanwhile: from the open's certainty to the load's
// entry — the next tick, a few milliseconds — the last frame it was shown
// (the picker, or the prompt a dirty reopen answered; at a cold launch
// nothing of ours, the system's starting window); then the one frame, the
// empty waveform, through the load and the plate's render; then the
// finished picture.
//
// THE DAMAGE CALL: invalidate_region takes a rect of the GUI's whole
// geometry, which is the surface's (the app paints no window frame,
// 2026-10-24).
//
// THE CANVAS FRAME (2026-10-13, the GPU canvas; canvas_sink.h owns the rule):
// set_canvas_frame(list) names the GUI's one process-wide draw list
// (gui_canvas_frame) — its records, its clip and its floater bit, all the
// GUI's — and on_redraw calls it on the first damage rect of every frame,
// after which the backend reads the list at that frame's present and at
// every later one until the next call — GuiGlCanvas's frame (gl_canvas.h),
// the one canvas road (architect 2026-10-30): the records — or the score
// viewer's page while it stands (2026-10-27) — drawn under a scissor to the
// clip, the clip left out of every upload unless the floater bit stands, and
// the page's report written back into the list (GuiCanvasPage,
// canvas_sink.h). The list outlives every project set (it is the process's),
// so a run stop strands no pointer.
#include "platform_android.h"
