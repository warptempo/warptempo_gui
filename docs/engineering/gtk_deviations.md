# Where the interface departs from GTK

GTK 3 and the Arc-Dark theme are the interface's inspirations (architect 2026-11-27). This file is the ONE list of
the places where the program knowingly does something else, so they can be looked at together. One line each:
ours, GTK's, why, and the owner in code whose comment holds the rule. What the program takes FROM GTK is not
listed anywhere: each such rule names its GTK source at its owner.

A line here is not closed: it is a standing choice, open to his word. A new departure gets its line here in the
arc that makes it; one that is brought to GTK's leaves.

GTK's numbers below were read at the source (gtk-3-24: `gtksettings.c`, `gtktooltip.c`, `gtkentry.c`; xfce4-notifyd's
`xfce-notify-common.h`) on 2026-11-27. GTK's side of the "Pointer and widgets" lines is from that day's survey and
its links (GTK's documentation of each property), not re-read line by line.

## Timing and slop

| What | Ours | GTK's | Why | Owner |
|---|---|---|---|---|
| The double tap's window | 600 ms, from the first tap's lift to the second's press | 400 ms (`gtk-double-click-time`) | A relaxed double tap with the pen or a finger leaves the glass and comes back; "an even 600", the product's one beat (architect 2026-09-29) | `kDoubleClickMs` = `kHoldBeatMs`, `double_tap_at` |
| The double tap's slack | 9 U (18 device px) | 5 px (`gtk-double-click-distance`) | The same hand, on glass | `kDoubleClickSlackPx` |
| The drag gate (a press becomes a drag) | 9 U (18 device px) | 8 px (`gtk-dnd-drag-threshold`) | 9 dp on this panel, a fingertip's roll (architect 2026-11-05) | `kDragMovedThresholdPx` |
| The grab gate (a marker, a trim cap) | 18 U, twice the drag gate | none: GTK has the one threshold | A tap that selects a marker or frames the trim must not drag it | `kGrabMovedThresholdPx` |
| The long press | 300 ms, read at the lift; nothing marks the moment it is crossed | 500 ms (`gtk-long-press-time`), and GtkGestureLongPress fires while the press is still held | Run on the tablet and ruled settled ("very good") | `kHoldDelayMs` |
| The hold-repeat (the on-screen keyboard's keys, the repeating keys) | 600 ms, then 25 a second | none of its own (a desktop's key repeat is the display server's) | His labwc setting | `kHoldBeatMs`, `set_repeat_info` (platform_android.cpp) |
| A timed card's life | 5 s | 10 s (xfce4-notifyd's `EXPIRE_TIMEOUT_DEFAULT`) | How long a sentence stays readable; Plasma's default (2026-08-29) | `kNotificationMs` |
| The caret after a very long pen hold | A pen held in a field more than about eleven seconds and then moved starts the blink again | The caret stays steady | Matching it takes one more stored bit; no plain use holds a caret drag that long (2026-11-27) | `text_editor::pointer_seated_caret` |

## Pointer and widgets

| What | Ours | GTK's | Why | Owner |
|---|---|---|---|---|
| The tooltip's seat | On its key's face: left edge on the face's, 1 U away, under a top-row key and above every other; gated by the TIPS lamp; a press ends the browse chain | At the pointer, under the cursor; no lamp | The pen covers what sits under it (architect 2026-11-19). The timing IS GTK's | `tooltip_box_rect`, `kTooltipGapU` |
| A tap on a list's row | Acts: opens, plays, applies | Selects; a double click activates (`activate-on-single-click` off by default; Thunar offers single click as an option) | One pointer, no double click on a row | `finish_folder_overlay_release` |
| A list's scroll | A drag, with no fling, no glow at the ends, no indicator | Kinetic scrolling, the overshoot glow, a scroll indicator | "No scrollbar" (architect 2026-11-19) | folder_overlay.h |
| The scrub | The seek is made at the lift, the sound paused while the knob is held | GtkRange changes its value all through the drag | The audio (architect 2026-10-02, 2026-10-26) | input_handler.h's THE PLAY-SCRUB'S POINTER HALF |
| The trim bar | Arc's scrollbar look, two ends and a bridge; a press on the bare trough moves nothing (its double tap frames the trim) | A scrollbar's trough warps the slider to the press | It is a two-ended trim control wearing a scrollbar's paint | input_trim.cpp's head |
| NOISE | An entry that is a relative slider: a drag on a square-law travel of 800 U, a tap toggles | An entry is not a slider; GtkScale is absolute | The one level control beside the keys | `apply_level_case` |
| The pointer in a text field | A tap or a drag seats the caret; it never selects. No double tap for a word, no triple tap for all. Selecting is the on-screen keyboard's Shift with LEFT / RIGHT | A drag selects; a double click selects a word, a triple click the line | Ruled: the pen places, the keyboard selects | `set_editor_caret_from_x` |
| A question | No focus, no default answer, no key answers it: its buttons alone | A default response on Enter, Esc cancels | No hardware keyboard (architect 2026-10-30, 2026-11-02) | `AppState::PromptState` |
| Hover | The pen alone; after a lift, none until 18 U of motion or a leave | Any pointer; no latch | A finger never hovers; the latch keeps a lifted pen from re-lighting what it just pressed | `GuiInputHandler::pen_hot_latch_` |
| The radio pairs | A press on the lit member selects the other | A press on the active radio button does nothing | A shortcut: one key's seat switches both ways | `kIconRowRightRun` |

## Look and words

| What | Ours | GTK's / Arc's | Why | Owner |
|---|---|---|---|---|
| The answers' paint | Every answer alike | GTK's default is alike too; Arc offers `suggested-action` (blue) and `destructive-action` (red) to an app that asks | Not taken | `paint_modal_page` |
| The title bar's name | Left, 6 U after the note | GTK's own header is left too; his desktop's centered titles are xfwm's | Closed (closed_questions.md) | `paint_title_bar` |
| "Settings" | The word | Xfce says "Preferences" | Not taken | the roster's tooltips |
| The on-screen keyboard | The program's own | GTK has none | The tablet's one keyboard | onscreen_keyboard.h |
