#pragma once

// A DIAGNOSTIC TRACE (2026-10-10) OF THE S PEN'S TOUCH-DOWN BLINK on a
// hover-lit face — the Settings dialog's choice combo ("the blink is not gone,
// it's still there") and the open pull-down menu's row ("the word Pick Colors
// blinks"): the platform's motion events, the pointer leave and its reason,
// the pen latch, the choice editor's and the modal's state writers, their
// damage requests and one line per painted frame of the dialog and the menu,
// so the planner reads the sequence off `adb logcat -s warptempo:I` (the
// Android backend pumps stderr into logcat line by line, platform_android.cpp's
// log_pump; the laptop shows it on the terminal). TO BE REMOVED, every
// TRACE_COMBO site with this header, once the cause is recorded at its owner.
//
// GATED: a line is written only while the gate stands — A PULL-DOWN MENU IS
// OPEN OR THE SETTINGS EDITOR IS OPEN (refresh_gate, written by the GUI at the
// run loop's settled tail and at every frame's paint, main.cpp and
// paint_handler.cpp; the platform reads the last word, it cannot see the GUI's
// state), so ordinary use stays silent. Each line is
// `trace: <ms> <site> <fields>`, the ms a monotonic (steady_clock) stamp so
// the sequence can be timed. Single-threaded: every writer and reader runs on
// the GUI's one thread (the log pump only reads the pipe).

#include <chrono>
#include <cstdarg>
#include <cstdio>

namespace trace_combo {

inline bool& gate() {
    static bool g = false;
    return g;
}

inline long long now_ms() {
    using namespace std::chrono;
    return static_cast<long long>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
            .count());
}

[[gnu::format(printf, 2, 3)]]
inline void emit(const char* site, const char* fmt, ...) {
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "trace: %lld %s %s\n", now_ms(), site, buf);
}

// The gate's one writer; a flip writes its own line, so the trace's window
// is visible in the log.
inline void refresh_gate(bool on, const char* where) {
    if (gate() == on) return;
    gate() = on;
    std::fprintf(stderr, "trace: %lld gate %s at=%s\n", now_ms(),
                 on ? "on" : "off", where);
}

} // namespace trace_combo

#define TRACE_COMBO(site, ...)                                           \
    do {                                                                 \
        if (::trace_combo::gate()) ::trace_combo::emit(site, __VA_ARGS__); \
    } while (0)
