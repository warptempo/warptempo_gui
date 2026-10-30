#pragma once

// The one GUI body, called by the one entry point, android_main
// (platform_android.cpp; the laptop GUI and its main() dropped, architect
// 2026-10-30). It does everything from the locale check on: the signal
// dispositions, the device config, the platform's init, and then THE PROJECT
// LOOP — the AppState and subsystem construction, the hook wiring, the run
// loop and the teardown, once per project the process opens (the loop
// contract is at platform.h and at the loop itself) — returning the
// process's exit status. It takes no argument (the tablet has no command
// line): the project to open is the device config's `last_project` or the
// first valid project in name order (startup_source, project_model.h).
int gui_main();
