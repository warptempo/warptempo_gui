#pragma once

#include "gui_input.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

// Generic in-place text editor for a small constrained vocabulary, used
// by the top-flag editor. State is a POD-ish struct so callers can
// keep it inline on AppState. Validation, commit semantics, and visual
// rendering are the caller's concerns; this module only handles the
// keyboard-driven mutation of `pending` + cursor state and exposes hooks
// for blink and the parse-failure red flash.
//
// Reuse: the several editor surfaces supply different
// validators and writers but reuse this state shape and keystroke routing.
//
// THE TEXT IS UTF-8 (architect 2026-08-02, the relaxation that retired the
// "non-ASCII unsupported" stance — a fossil of the deleted cairo monospace
// path). `pending` holds any well-formed UTF-8 the typed path or the filter
// admits, and the free-text settings fields round-trip it; the STRUCTURAL
// grammars this editor feeds (labels, numeric spellings, settings keys) stay
// ASCII, but that is their validators' business at commit and not this
// module's. Three properties make it work:
//
//   - INDICES STAY BYTE INDICES. `pending`, the shaped run's per-byte pen
//     offsets (text_shape::byte_offsets_px) and the click-to-byte search all
//     address by byte, so there is exactly one coordinate and nothing to
//     convert between.
//   - EVERY STEP MOVES A WHOLE CODEPOINT. Left/Right, BackSpace/Delete, the
//     Ctrl word walks and the double-click run selector all land on codepoint
//     boundaries, so `cursor_pos` and `selection_anchor` are boundaries by
//     construction and no operation can split a character. Home/End and Ctrl+A
//     take the string's extremes, which are boundaries trivially.
//   - GRAPHEME CLUSTERS ARE OUT OF SCOPE, deliberately and in the same class as
//     the no-bidi and no-font-fallback exclusions: a combining mark is its own
//     codepoint, so it carets and deletes separately from the base character it
//     sits on. Accepted — the free-text fields are provenance notes, not a text
//     processor.
//
// Rendering follows from the same one face: an uncovered glyph draws .notdef
// (no fallback), and shaping is single-direction LTR.

namespace text_editor {

// EVERY CAP BELOW IS IN BYTES — `pending.size()`, which is what every cap test
// reads. With UTF-8 admitted a multi-byte character spends the bytes it costs,
// so a field's cap is a storage bound rather than a character count. The names
// are historical; nothing measures characters anywhere in this module.
//
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
// the editor's growth ceiling: a wider on-disk value still loads, displays,
// commits and persists unchanged, and can be shortened, but the editor will not
// grow any pending past the cap.
//
// HORIZONTAL SCROLLING EXISTS NOW, AND ONLY ON ONE SURFACE (row 5, 2026-08-01).
// This note used to record "running off the right edge while typing — no
// horizontal scroll" as the settings prompt's accepted behaviour. The FLAG
// editor grew a view offset when it became the unrolled flag box, because that
// box is clamped on-window and its text genuinely does not fit; the bottom-strip
// editors are UNCHANGED and still run off the right edge, deliberately — the
// feature is scoped to the surface that needed it, not adopted product-wide.
// See `view_offset_px` below for which side writes it.
constexpr int kMaxPendingCharsSettings = 1024;
// Render-commit prompt (bare `'`). Holds a render entry's identifier relative
// to renders/ — `<batch_dir>/<basename>` (e.g. `1_iterations/01`) or a
// bare basename. Program-written batch/entry names are short; 256 is a
// generous ceiling for the relative path a user types or Tab-completes.
constexpr int kMaxPendingCharsRenderCommit = 256;

// Vocabulary the editor accepts on the keyboard. Different call sites
// edit different payload shapes; the kind now selects only the length cap
// (handle_key accepts any character-bearing key and defers grammar to the
// commit-time validator). The flag editor uses FlagPayload (payload text,
// iteration grammar included); the BPM popup uses BpmBracket;
// the settings-prompt editor uses SettingsAssignment (`key=value`); the
// render-commit prompt uses RenderCommit (a render entry's relative-path
// identifier, resolved against the renders/ listing at commit).
enum class Kind {
    FlagPayload,
    BpmBracket,
    SettingsAssignment,
    RenderCommit,
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

    // Byte index into `pending`, always on a UTF-8 CODEPOINT BOUNDARY (the
    // invariant and the routes that maintain it are at the head of this file).
    // Clamped to [0, pending.size()].
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

    // HORIZONTAL VIEW OFFSET, in pixels: how far the visible text window has
    // scrolled right through `pending`. Byte 0 paints at (text origin -
    // view_offset_px), so a positive value hides text off the box's left edge.
    //
    // ONE WRITER, ONE READER, AND BOTH ARE THE FLAG EDITOR. Its unrolled box is
    // clamped fully on-window, so a long payload does not fit and the view must
    // travel to keep the caret visible; the painter recomputes the MINIMAL
    // offset that shows the caret each frame (scroll only as far as it must, in
    // whichever direction the caret left the window) and the click-to-byte path
    // reads the same published geometry. The BOTTOM-STRIP editors never touch
    // it — they are monospace, unclamped, and behave exactly as before — so this
    // field is 0 for their whole session. It lives on State rather than beside
    // the geometry because it is SESSION state: it must survive from frame to
    // frame and die with the edit, which enter/deactivate already own.
    double view_offset_px = 0.0;

    // Cursor blink: monotonic timestamp at which the cursor became
    // visible. The renderer tests `(now - blink_epoch) % period < period/2`
    // to decide whether to draw the bar this frame.
    std::chrono::steady_clock::time_point blink_epoch =
        std::chrono::steady_clock::now();
};

// THE UTF-8 PRIMITIVE, public because one site outside the editors needs the
// same answer: a continuation byte is 10xxxxxx, and every other byte STARTS a
// codepoint. That single test is what every boundary walk in this module is
// built from — it needs no sequence length and degrades safely on malformed
// bytes.
//
// The ONE outside caller is the render-commit editor's Tab autocomplete
// (input_key_dispatch.cpp), which backs its byte-wise longest-common-prefix off
// to a boundary before seeding `pending`. It is exposed rather than the
// boundary walks themselves because that is the whole of what the caller needs:
// prev_codepoint_boundary always steps back a WHOLE codepoint, which is the
// wrong answer for a prefix that already ends on a complete one.
bool is_utf8_continuation_byte(unsigned char b);

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

// The editor's key-classification owner: the single truth of which
// keys the editor consumes, and how. It is side-effect-free (no State), because
// handle_key's MEMBERSHIP — which branch a key enters — depends only on key+mods;
// State decides EFFECTS within a branch (e.g. Ctrl+C returns CopyRequested vs
// plain Consumed by has_selection), never whether a key is owned. Three
// consumers share it: handle_key gates membership on it (NotEditorKey returns
// NotConsumed before any branch), modal_editor_key_blocked admits exactly the
// non-NotEditorKey set plus its gate-level carve-outs, and repeat_eligible's
// in-editor arm repeats exactly MotionEditKey | PrintableKey. Because all three
// read this one function, the strict-modifier rule is stated ONCE: no consumer
// re-enumerates a key.
enum class KeyClass {
    NotEditorKey,   // not the editor's — the keyboard-modal gate drops it
    SessionKey,     // BARE Escape, Return/KpEnter — end the session, never repeat
    ChordKey,       // CTRL-EXACT A/C/X/V — one-shot
    // Left/Right/Home/End/BackSpace/Delete under ctrl/shift in ANY combination
    // but NEVER alt — repeats. Ctrl and Shift bind here (word jump/erase,
    // selection extend, and the plain gesture where a combination adds nothing,
    // as in any one-line text field); alt binds nothing, so an alt-carrying
    // motion press is NotEditorKey.
    MotionEditKey,
    // No ctrl/alt and a character-bearing codepoint: >= 0x20, not DEL, not a
    // surrogate, within Unicode. Not ASCII-limited — the platform resolves a
    // full codepoint and the editor UTF-8 encodes it. Repeats.
    PrintableKey,
};

// Classify key+mods against the editor's owned keymap, reproducing handle_key's
// exact predicates and precedence: the session keys before the Ctrl chords, the
// chords (specific A/C/X/V) before motion, motion (ctrl/shift, never alt) before
// printable. A key that could satisfy two predicates classifies as the one
// handle_key acts on (Ctrl+A is a chord, not printable — printable already
// excludes ctrl; Ctrl+Left is MotionEditKey — the chord shape list is A/C/X/V
// only). NO ARM ADMITS ALT: session is bare-exact, the chords ctrl-exact, motion
// ctrl/shift-only, printable already excludes ctrl and alt. So a press wearing a
// modifier its arm does not bind (Ctrl+Escape, Ctrl+Enter, Ctrl+Shift+V,
// Ctrl+Alt+A, Alt+Left, Ctrl+Alt+BackSpace) is simply NotEditorKey: unbound and
// not-ours need not be told apart, because every keyboard-modal editor drops
// both at the gate and nothing downstream can act on either.
KeyClass classify_key(GuiKey key, GuiInputState mods);

KeyAction handle_key(State& s, GuiKey key, GuiInputState mods);

// Clipboard primitives, used by the input handler to bridge the editor's
// selection model to the SYSTEM clipboard. selected_text returns the
// highlighted substring (empty if no selection — and always a whole number of
// codepoints, since both endpoints are boundaries).
//
// replace_selection is the paste/cut primitive and THE ONE INCOMING-TEXT
// FILTER. Its acceptance rule:
//   - PRINTABLE ASCII (0x20..0x7e) passes.
//   - WELL-FORMED MULTI-BYTE UTF-8 passes verbatim — the sequence's length
//     matches its lead byte, every continuation byte is one, and the decoded
//     value is neither overlong, a surrogate, nor past U+10FFFF.
//   - Everything else is DROPPED byte by byte: ASCII control characters
//     (0x00..0x1f, including newlines and tabs, plus DEL) and any malformed
//     byte. A malformed byte costs itself and nothing after it — the scan
//     resumes at the next byte, so the good text around it survives.
// It then atomically either replaces the selection with the FULL filtered text
// or, when that would grow the pending past the field's per-Kind byte cap,
// refuses the whole operation and sets the red state (buffer and selection
// untouched). Cut calls it with an empty string, an always-accepted shrink that
// simply deletes the selection.
std::string selected_text(const State& s);
void        replace_selection(State& s, const std::string& raw);

// Render-side helper: returns true if the cursor should be drawn this
// frame. Period is 1000ms (~500ms on, ~500ms off) and resets at every
// `pending` mutation so the cursor stays visible immediately after a
// keystroke.
bool cursor_visible_now(const State& s);

// THE ONE CLICK-X -> BYTE MAPPING (row 7 deleted its monospace predecessor,
// byte_index_from_click_x, with the face): `boundaries` is the per-byte pen offset vector the
// shaping chokepoint produces (text_shape::byte_offsets_px — one entry per byte
// boundary, index 0 at 0.0 and the last at the run's full width), and
// `text_origin_x` is the window x that boundary 0 paints at (already carrying
// any view offset). There is no advance to divide by with a proportional face,
// so the mapping is a SEARCH, and the rule is NEAREST BOUNDARY: the caret lands
// on whichever byte edge is closest to the click, ties going to the LOWER index.
// Clicking the left half of a glyph puts the caret before it and the right half
// after it, which is what every text field does. The returned index is within
// [0, boundaries.size() - 1] by construction; an empty vector returns 0.
//
// IT NEVER RETURNS A MID-CODEPOINT INDEX, structurally rather than by a guard:
// byte_offsets_px gives every byte of a cluster its cluster's START offset, so
// a multi-byte character's interior boundaries tie with its first one — and the
// tie rule keeps the LOWER index, which is the boundary. The same argument
// covers a ligature's interior bytes.
int byte_index_from_shaped_x(double click_x, double text_origin_x,
                             const std::vector<double>& boundaries);

// Select the maximal run of the clicked character's class under byte index
// `pos`, setting selection_anchor to the run start and cursor_pos to its end
// (an empty pending leaves no selection; a non-empty pending always selects at
// least one character). This is a selection-specific classifier DISTINCT from
// the Ctrl+Left/Right cursor-motion scanners — the desktop double-click
// convention (select the clicked class's run, never crossing a class change)
// differs from cursor-motion word-walking, and every toolkit (GTK/Qt/Kate/
// Firefox) ships a tailored selection heuristic rather than reusing its motion
// boundaries. Three classes, the Qt/Kate "programmer's word" variant: WORD is
// [A-Za-z0-9_] plus any byte >= 0x80 (so a multi-byte character is one uniform
// run and a selection edge lands only on codepoint boundaries), WHITESPACE
// is ' ' and '\t', PUNCTUATION is every other byte — so clicking `=`, `.`, or
// `::` selects that punctuation run. The double-click path in the input handler
// calls this after mapping the click x to a byte index
// (byte_index_from_shaped_x).
void select_word_at(State& s, int pos);

} // namespace text_editor
