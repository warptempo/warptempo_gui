#pragma once

// The one GUI body, called by the one entry point, android_main
// (platform_android.cpp; the laptop GUI and its main() dropped, architect
// 2026-10-30). It does everything from the locale check on: the signal
// dispositions, the device config, the platform's init, and then THE PROJECT
// LOOP — the AppState and subsystem construction, the hook wiring, the run
// loop and the teardown, once per project the process opens (the loop
// contract is at platform.h and at the loop itself) — returning the
// process's exit status. `argument` is an explicit project source (which
// must be a project's source under the device config's projects path) or
// nullptr for "no argument": the project to open is then the device config's
// `last_project` or the first valid project in name order (startup_source,
// project_model.h). android_main always passes nullptr.
int gui_main(const char* argument);
