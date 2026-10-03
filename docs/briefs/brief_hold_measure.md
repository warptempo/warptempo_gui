# BRIEF (TEMPORARY, NEVER COMMITTED): one logcat line per pen edge in the Android pen road, for the hold measurement (architect 2026-10-01: "let's do that")

Model: Opus, effort high. You are THE CODER: edit only `src/gui/platform_android.cpp`; build nothing on the laptop (the planner builds the APK); read-only git only; no commits. This change is INSTRUMENTATION THAT COMES OUT AGAIN in a follow-up brief; keep it to the fewest lines and mark each with `// TEMP HOLD MEASUREMENT 2026-10-01` so the removal is a grep.

## WHAT
In `GuiPlatform`'s motion-event handler (the switch on `masked`, ~lines 1640–1830), add an `__android_log_print(ANDROID_LOG_INFO, kLogTag, ...)` at these four edges, for the PEN only (`is_pen(...)` / `pen_present` as the arms already decide):
1. HOVER_EXIT (the arm where `!pen_in_plane` leads to `end_pen_hover()` — log the raw action too, since a hover above the plane takes the same arm: include `masked` and whether it was a true `AMOTION_EVENT_ACTION_HOVER_EXIT`),
2. HOVER_ENTER (the `!pen_hovering_` → `pointer_enter` branch),
3. DOWN (the pen's tip down, the `AMOTION_EVENT_ACTION_DOWN` arm, pen contact only),
4. UP (the `AMOTION_EVENT_ACTION_UP` arm, pen lift only).

Each line prints: the edge's name, `AMotionEvent_getEventTime(event)` in ms (it is ns; divide by 1,000,000 with integer math, print as %lld), and the process's monotonic clock in ms at the moment of handling (`monotonic_ms()` if the platform has it in scope, else `clock_gettime(CLOCK_MONOTONIC)` done inline). Format: `hold: <EDGE> ev=<ms> now=<ms>`. Nothing else changes: no behaviour, no early return, no state.

## REPORT
The exact lines added with their line numbers, and confirm the tag is `kLogTag` ("warptempo") at INFO so `adb logcat -s warptempo:I` shows them.
