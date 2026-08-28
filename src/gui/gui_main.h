#pragma once

// The one GUI body both entry points call — Linux's main() (main.cpp, which is
// the argument check and nothing else) and Android's android_main. It does
// everything from the locale check on: the signal dispositions, the device
// config, the platform's init, and then THE PROJECT LOOP — the AppState and
// subsystem construction, the hook wiring, the run loop and the teardown, once
// per project the process opens (the loop contract is at platform.h and at the
// loop itself) — returning the process's exit status. `argument` is the
// laptop's optional command-line source (`warptempo_gui <wav>`, which must be
// a project's source under the device config's projects path) or nullptr for
// "no argument", which both backends can pass: the project to open is then the
// device config's `last_project` or the first valid project in name order
// (startup_source, project_model.h). Android always passes nullptr.
int gui_main(const char* argument);
