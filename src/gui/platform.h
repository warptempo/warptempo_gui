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
// loop leaves) and a RUN STOP (request_run_stop — the Open project picker's reopen;
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
// stand (a capture is the nav drag's, refused under a modal), the touch
// disambiguation window has resolved (it is what delivered the press), and
// the key that committed is a session key the repeat probe never arms. The
// next set's first configure is redelivered explicitly (redeliver_geometry),
// since the window will not send one for a size that did not change.
#ifdef __ANDROID__
#include "platform_android.h"
#else
#include "platform_wayland.h"
#endif
