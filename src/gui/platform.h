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
// picker's, or File → Revert's on the project already open;
// the window and the input core stand, and the next iteration's run() picks
// them up as they are). Both backends implement the pair identically:
// should_exit_ is process-scoped and never reset, run_stop_requested_ is
// cleared at the head of every run().
//
// WHAT SURVIVES A RUN STOP, and why each may: the window, its surface and its
// buffers (the same window shows the next project); has_initial_configure()
// (the window is configured, and the next iteration's first tick loads on
// it); the pending damage and any pending frame callback (the new set damages
// the whole window before its run(), and a callback in flight paints that);
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
// THE DAMAGE PAIR: invalidate_region takes a CLIENT rect — the GUI's whole
// geometry, the window's band outside it — so no client damage ever reaches
// the sizing frame (the restored laptop's, or a band a chrome keeps on the
// maximized window on both devices, window_frame_maximized);
// invalidate_surface damages the WHOLE SURFACE, the band with it. The band
// paints in the chrome's ground and relief quartet, tones of the brass's
// Face, which no Settings act moves (the Waveform row moves the selection
// fill alone, palette_file.h's install_program_palette) and A MATERIALS BAKE
// DOES: the brass's Face is its bake's mean, and the bake reinstalls the
// palette off it (materials.h's BAKE RULE) — at a settled resize, a
// gui_scale commit or True Colors. So the pre-paint hook (main.cpp), the
// settled bake's seat, asks for the whole surface in the first frame after
// any bake (2026-10-21); every other repaint of the band rides the
// backend's own full damage (a configure or a window adoption). Both
// backends implement the pair alike.
//
// THE CANVAS FRAME (2026-10-13, the GPU canvas; canvas_sink.h owns the rule):
// set_canvas_frame(list) names the GUI's one process-wide draw list
// (gui_canvas_frame) — its records, its clip and its floater bit, all the
// GUI's — and on_redraw calls it on the first damage rect of every frame
// under a gl build (CMakeLists.txt's WARPTEMPO_CANVAS, both platforms since
// 2026-10-14), after which the backend reads the list at that frame's
// present and at every later one until the next call — GuiGlCanvas's frame
// (gl_canvas.h), which both backends drive alike: the records drawn under a
// scissor to the clip, the clip left out of every upload unless the floater
// bit stands. The list outlives every project set (it is the process's), so
// a run stop strands no pointer. Under a cpu build no list is ever built and
// the call never comes: the Android cpu build's body stores the pointer and
// reads nothing, the Wayland cpu build's is a no-op (its canvas the cairo
// sink's; the member exists because on_redraw compiles against either road).
#ifdef __ANDROID__
#include "platform_android.h"
#else
#include "platform_wayland.h"
#endif
