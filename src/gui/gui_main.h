#pragma once

// The one GUI body both entry points call — Linux's main() (main.cpp, which is
// the argument check and nothing else) and Android's android_main. It does
// everything from the locale check on: the signal dispositions, the AppState
// and subsystem construction, the hook wiring, the run loop and the teardown,
// returning the process's exit status. `source_path` is the audio file to
// load, and where it comes from is the CALLER'S convention — the command line
// on Linux, the backend's own answer on Android.
int gui_main(const char* source_path);
