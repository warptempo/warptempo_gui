#pragma once

// The platform seam's ONE include: the backend class is GuiPlatform in every
// build, its header chosen here and nowhere else. A consumer includes this and
// never a backend header, so adding a backend touches this file alone.
//
// THE LOOP CONTRACT (architect 2026-08-27, with the project model): ONE
// GuiPlatform per process, MANY run() calls. gui_main (main.cpp) constructs
// the backend and calls init() once, then runs a loop whose every iteration
// builds a project's whole object set, installs its callbacks, calls run(),
// and tears the set down when run() returns. run() RETURNS FOR TWO REASONS —
// an EXIT (request_exit, or the backend's own connection-loss and
// activity-destroyed ends; exit_requested() reads true afterwards and the
// loop leaves) and a RUN STOP (request_run_stop — a reopen: the Open project
// picker's, or Revert's on the project already open;
// the window and the input core stand, and the next iteration's run() picks
// them up as they are). Both backends implement the pair identically:
// should_exit_ is process-scoped and never reset, run_stop_requested_ is
// cleared at the head of every run().
//
// WHAT SURVIVES A RUN STOP, and why each may: the window, its surface and its
// buffers (the same window shows the next project); has_initial_configure()
// (the window is configured, and the next iteration's first tick loads on
// it); the pending damage and any pending frame callback (a callback in
// flight finds the open's hold below standing and presents nothing, and the
// new set's first painted frame is the release's, which damages the whole
// window);
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
// next set's first configure is redelivered explicitly (redeliver_geometry),
// since the window will not send one for a size that did not change.
// PLAYBACK DOES NOT SURVIVE: it is per project, shut down and re-inited with
// the set, so a reopen costs one AAudio stream start (its one transient), the
// accepted price.
//
// THE OPEN'S PRESENTATION HOLD (architect 2026-10-27: an open "paints from
// left to right ... it looks kind of sloppy ... we could withhold the painting
// until it's ready to paint the whole thing"; nothing is shown for the load
// itself, his loads being under half a second): FROM THE MOMENT A PROJECT
// OPEN IS CERTAIN UNTIL THE NEW PROJECT'S FIRST COMPLETE PICTURE, NOTHING IS
// PRESENTED — the glass keeps the last frame it was shown (the picker, or
// the project a Revert reopens; at a cold launch nothing of ours), and the
// finished picture replaces it in one present. ONE MECHANISM ON BOTH
// BACKENDS, one bit, `presentation_held()`:
//   * ARMED BY THE PLATFORM ALONE, at the two points an open becomes
//     certain: CONSTRUCTION (the cold launch's first open) and
//     request_run_stop (every run stop is a reopen — the picker's choice
//     answered, Revert, the pull's reload through Revert's road). A
//     cancelled picker or a prompt's Cancel requests no stop and arms
//     nothing; the render entry's and the `h` view's loads in place stop no
//     run and arm nothing.
//   * WHILE IT STANDS, NO PAINT AND NO PRESENT: paint_one_frame returns
//     before the pre-paint hook and before any pixel, so no as-painted record
//     claims a picture the glass never showed (the cheapest correct paint
//     is none); the damage accumulates and nothing schedules a frame for it
//     (Wayland asks no frame callback, Android posts no vsync callback). A
//     surface lost and readopted under it (Android's TERM/INIT) only adds
//     its full damage.
//   * RELEASED BY THE GUI ALONE, release_presentation(), from the loop's
//     tick once the project's load has returned and the waveform plate the
//     paint handler holds is the live view's (GuiPaintHandler::
//     live_plate_published, whose declaration says what COMPLETE checks and
//     why); the release declares the whole window's damage and the frame it
//     brings paints and presents it whole. A RELEASE UNDER A PENDING RUN
//     STOP IS REFUSED: the old set's tick, running in the pass that asked
//     the stop, would otherwise find its own picture complete and present
//     it — the hold a stop arms is the next set's to release. THE ROAD OUT
//     IS DIFFERENT PER BACKEND AND RECORDED AT EACH SITE: Wayland paints at
//     once (a cold launch's surface is unmapped, and a frame request on it
//     would never be answered); Android declares the damage and the pass's
//     tail posts the vsync callback as for any damage.
//   * A ROAD THAT NEVER RELEASES ENDS THE PROCESS: a refused load exits
//     (fatal_load, run_project), and an exit needs no picture. No timeout
//     and no recovery code: the plate's worker always completes (NO
//     BACKSTOPS).
//   * INPUT WAITS FOR THE PICTURE: while the hold stands the GUI's press-
//     class hooks drop what they are handed (run_project's hook block,
//     main.cpp, which lists them) — a press, a key, a wheel, a touch
//     gesture's begin or a car button would act on a picture the user has
//     not seen (ON SCREEN IS AS PAINTED). Releases, motion, focus, the
//     close request and Ctrl+Q pass: they find no claim, or end the
//     process.
// What the laptop shows meanwhile: at a cold launch NO WINDOW — xdg-shell
// maps a toplevel only at its first buffer, so the configure is acked and
// the window appears with the finished picture; at a reopen the picker. The
// tablet: at a cold launch whatever the system shows for a window never
// posted to (its starting window, the theme's black background —
// Theme.NoTitleBar); at a reopen the picker.
//
// THE DAMAGE CALL: invalidate_region takes a rect of the GUI's whole
// geometry, which is the surface's on both backends since 2026-10-24 (the
// app paints no window frame: labwc draws the laptop's borders; the app's
// own sizing frame, a band outside the GUI's geometry, stood 2026-10-05 to
// then). Both backends implement it alike. (invalidate_surface, the whole
// surface's damage the pre-paint hook
// asked for after a materials bake while the band wore a material,
// 2026-10-21 to 2026-10-22, is retired.)
//
// THE CANVAS FRAME (2026-10-13, the GPU canvas; canvas_sink.h owns the rule):
// set_canvas_frame(list) names the GUI's one process-wide draw list
// (gui_canvas_frame) — its records, its clip and its floater bit, all the
// GUI's — and on_redraw calls it on the first damage rect of every frame
// under a gl build (CMakeLists.txt's WARPTEMPO_CANVAS, both platforms since
// 2026-10-14), after which the backend reads the list at that frame's
// present and at every later one until the next call — GuiGlCanvas's frame
// (gl_canvas.h), which both backends drive alike: the records — or the score
// viewer's page while it stands (2026-10-27) — drawn under a scissor to the
// clip, the clip left out of every upload unless the floater bit stands, and
// the page's report written back into the list (GuiCanvasPage,
// canvas_sink.h). The list outlives every project set (it is the process's), so
// a run stop strands no pointer. Under a cpu build no list is ever built and
// the call never comes: the Android cpu build's body stores the pointer and
// reads nothing, the Wayland cpu build's is a no-op (its canvas the cairo
// sink's; the member exists because on_redraw compiles against either road).
#ifdef __ANDROID__
#include "platform_android.h"
#else
#include "platform_wayland.h"
#endif
