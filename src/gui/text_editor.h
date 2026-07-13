#pragma once

#include "gui_input.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>

// Generic in-place text editor for a small constrained vocabulary, used
// by the top-flag editor. State is a POD-ish struct so callers can
// keep it inline on AppState. Validation, commit semantics, and visual
// rendering are the caller's concerns; this module only handles the
// keyboard-driven mutation of `pending` + cursor state and exposes hooks
// for blink and the parse-failure red flash.
//
// Reuse: hover popups and bottom-flag iteration syntax supply different
// validators and writers but reuse this state shape and keystroke routing.

namespace text_editor {

// Maximum characters allowed in `pending`. The cap 52 covers a hypothetical
// full-double BASE and SCALE in the payload `BASE*SCALE:a.aa` — worst
// case 23 chars per value (17 significant digits plus point and exponent):
// 23 + 1 + 23 + 1 + 4 = 52. BASE is pinned to the N.NN grammar (4
// chars at the bracket ceiling), so committable payloads are far shorter;
// the generous cap is kept deliberately — it costs nothing and SCALE
// remains a full double in shortest round-trip form. An operation that
// would grow the pending past this cap refuses atomically and sets the red
// state (the whole edit lands or the buffer is untouched); shrinking and
// non-growing edits are always accepted, so an over-cap pending loaded from
// a hand-edited file can be trimmed back to canonical form.
constexpr int kMaxPendingChars = 52;
// Iteration-mode FlagPayload editing widens the accepted grammar
// to admit the inline `+[lo, hi]` bracket (`+[-99.99, +99.99]`, 17 chars
// with the display space), so the cap is the full-precision payload plus
// the bracket: 52 + 17 = 69.
constexpr int kMaxPendingCharsFlagIter = 69;
// BPM popup. `<beats>@[<lo>,<hi>]`: beats a positive int (up to 10
// digits), lo/hi full doubles in shortest round-trip form (up to 23 chars
// each): 10 + 2 + 23 + 1 + 23 + 1 = 60.
constexpr int kMaxPendingCharsBpm = 60;
// Settings prompt. Sized for the free-text provenance fields (url, notes),
// which are generous for a typical URL or note. The scalar keys are still
// bounded by their commit-time validators, not by this cap. 1024 bytes is
// the editor's growth ceiling: a wider on-disk value still loads, displays
// (running off the right edge while typing — no horizontal scroll), commits
// and persists unchanged, and can be shortened, but the editor will not grow
// any pending past the cap.
constexpr int kMaxPendingCharsSettings = 1024;

// Vocabulary the editor accepts on the keyboard. Different call sites
// edit different payload shapes; the kind selects which keys produce a
// printable character. The flag editor uses FlagPayload (digits,
// letters, `.`, `*`, `:`); the iteration popup uses IterationBracket
// (digits, `.`, `+`, `-`, `,`, `[`, `]`); the BPM popup uses
// BpmBracket (digits, `@`, `,`, `[`, `]`); the settings-prompt editor
// uses SettingsAssignment (letters, digits, `.`, `=`, `_`, `-`, `:`).
enum class Kind {
    FlagPayload,
    IterationBracket,
    BpmBracket,
    SettingsAssignment,
};

// State for a single editable rect.
struct State {
    // Identifier of the entity being edited. -1 means "not editing".
    // The caller decides what this means (a marker index for the flag editor).
    int target = -1;

    // Vocabulary discriminator. The caller sets this in `enter()` and
    // the keystroke handler routes printable detection accordingly.
    Kind kind = Kind::FlagPayload;

    // When true on a FlagPayload editor, the accepted grammar is
    // widened to admit the inline iteration bracket characters
    // (`+ - [ ] ,`) and the longer FlagIter length cap. Set at `enter()`
    // from iteration_mode_enabled; does nothing for other kinds.
    bool iter_grammar = false;

    // Editable text — currently the canonical post-pipe payload.
    std::string pending;

    // Frozen prefix (rendered to the left of `pending`) carrying time
    // and metadata flags. Display-only; not editable.
    std::string locked_prefix;

    // Byte index into `pending`. Clamped to [0, pending.size()].
    int cursor_pos = 0;

    // -1 means no selection. When >= 0, the selected range is
    // [min(selection_anchor, cursor_pos), max(...)). Set by shift-
    // extended motion (Shift+Left/Right/Home/End) and Ctrl+A; cleared
    // by non-shift motion, any printable insertion, any delete /
    // backspace, and by enter/deactivate.
    int selection_anchor = -1;

    // True after a failed commit. Cleared by any keystroke that mutates
    // `pending`.
    bool red = false;

    // Cursor blink: monotonic timestamp at which the cursor became
    // visible. The renderer tests `(now - blink_epoch) % period < period/2`
    // to decide whether to draw the bar this frame.
    std::chrono::steady_clock::time_point blink_epoch =
        std::chrono::steady_clock::now();
};

inline bool is_active(const State& s) { return s.target >= 0; }

inline bool has_selection(const State& s) {
    return s.selection_anchor >= 0 &&
           s.selection_anchor != s.cursor_pos;
}
inline int selection_start(const State& s) {
    if (!has_selection(s)) return 0;
    return std::min(s.selection_anchor, s.cursor_pos);
}
inline int selection_end(const State& s) {
    if (!has_selection(s)) return 0;
    return std::max(s.selection_anchor, s.cursor_pos);
}

// Reset `s` so `is_active` returns false.
void deactivate(State& s);

// Begin editing `target` with the given locked prefix and seed pending.
// Cursor lands at end of pending. `kind` selects the vocabulary the
// keystroke handler will accept while this editor is active.
void enter(State& s, int target,
           std::string locked_prefix,
           std::string initial_pending,
           Kind kind = Kind::FlagPayload,
           bool iter_grammar = false);

// Apply a key event to the editor. Returns true if the key was consumed
// — the caller should NOT route a consumed key to other handlers.
//
// Special return-value semantics for commit / discard:
//   - consumed_commit: editor wants the caller to validate-and-commit.
//   - consumed_cancel: editor wants the caller to discard (Esc).
//
// Pure printable characters and motion keys are handled internally and
// reported as plain `consumed`.
//
// Keys are platform-neutral GuiKey values; mods is the GuiInputState
// populated by the platform boundary (see gui_input.h).
enum class KeyAction {
    NotConsumed,
    Consumed,
    CommitRequested,
    CancelRequested,
    CopyRequested,
    CutRequested,
    PasteRequested,
};

KeyAction handle_key(State& s, GuiKey key, GuiInputState mods);

// Clipboard primitives, used by the input handler to bridge the editor's
// selection model to the session clipboard. selected_text returns the
// highlighted substring (empty if no selection). replace_selection is the
// paste/cut primitive: it sanitizes `raw` to printable ASCII (0x20..0x7e —
// dropping control chars, newlines, tabs, non-ASCII), then atomically either
// replaces the selection with the FULL sanitized text or, when that would
// grow the pending past the field's per-Kind cap, refuses the whole operation
// and sets the red state (buffer and selection untouched). Cut calls it with
// an empty string, an always-accepted shrink that simply deletes the
// selection.
std::string selected_text(const State& s);
void        replace_selection(State& s, const std::string& raw);

// Render-side helper: returns true if the cursor should be drawn this
// frame. Period is 1000ms (~500ms on, ~500ms off) and resets at every
// `pending` mutation so the cursor stays visible immediately after a
// keystroke.
bool cursor_visible_now(const State& s);

// Translate a click x-coordinate to a byte index into `pending` based on
// the monospace per-character advance and a known text-left x (the char-0
// origin). Both `advance > 0` and `text_left_x >= 0` must hold; callers
// gate on those before invoking. The returned index is clamped to
// [0, pending_size]. Shared by the flag editor's click-to-caret path and
// the F2.1 mouse drag-to-select path in input_handler.cpp.
int byte_index_from_click_x(double click_x, double text_left_x,
                            double advance, int pending_size);

} // namespace text_editor
