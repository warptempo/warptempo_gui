#pragma once

#include "gui_input.h"
#include "marker_measure.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
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
// HORIZONTAL SCROLLING EXISTS ON EVERY EDITOR (the flag editor from row 5,
// 2026-08-01; the DIALOG editors' field from 2026-08-13, architect at his
// live test — a recalled `notes=` value is longer than the field and "the
// viewport in the text field is always stuck on the left edge of the text").
// This note used to record "running off the right edge while typing — no
// horizontal scroll" as the settings prompt's accepted behaviour, and then
// recorded the feature as scoped to the flag editor alone; both readings are
// retired. ONE mechanism serves both surfaces — `view_offset_px` below, whose
// minimal-travel rule each painter applies — so the two cannot scroll
// differently.
constexpr int kMaxPendingCharsSettings = 1024;
// The history mode's commit-title editor (Ctrl+S while the view stands).
// Holds the checkpoint's commit message — one line, prefilled with `Update
// <id>` and free UTF-8 text the user may rewrite. 256 is a generous ceiling,
// well past the ~50-character summary line a commit title is written to be.
// (The `h` view's load prompt took the same ceiling until 2026-08-28, when
// the field-less picker replaced it; that picker retired the next day and the
// view's `'` raises a plain confirmation now, with nothing to type at all.)
constexpr int kMaxPendingCharsCommitTitle = 256;
// The marker MEASURE editor (bare `/`, its bottom-row button, the double-click
// on the blue box). The cap IS the load bound, taken from its one owner rather
// than re-spelled: kMaxMarkerMeasureBytes (marker_measure.h) is what both file
// parsers refuse past, and the two must be the same number for "a measure that
// commits here loads back" to hold exactly. Only the type changes — every cap
// in this module is an int, which is what the cap tests read.
constexpr int kMaxPendingCharsMeasure =
    static_cast<int>(kMaxMarkerMeasureBytes);
// The MEASURE PROPAGATE's paste-offset editor (Ctrl+Alt+/). Holds one SIGNED
// decimal integer — the number of measures to add to every DIRECT measure the
// paste writes. 6 is exactly the longest legal spelling, `-99999`: the offset
// is applied to a measure number bracketed at 99999 (marker_measure.h), so an
// offset outside +/-99999 could not produce an in-bracket result from any
// in-bracket source and there is nothing longer to type. A tight bound rather
// than a policy cap, the measure field's own arrangement.
constexpr int kMaxPendingCharsMeasureOffset = 6;

// Vocabulary the editor accepts on the keyboard. Different call sites
// edit different payload shapes; the kind now selects only the length cap
// (handle_key accepts any character-bearing key and defers grammar to the
// commit-time validator). The flag editor uses FlagPayload (payload text,
// iteration grammar included); the BPM popup uses BpmBracket;
// the settings-prompt editor uses SettingsAssignment (`key=value`); the
// history mode's commit-title editor uses CommitTitle (free one-line text,
// the message the checkpoint commit carries); the MARKER MEASURE editor uses
// MeasureText (the ` //<measure>` suffix a marker line may carry — an ASCII
// GRAMMAR since the field's 2026-08-20 rebrand, judged at the commit by
// marker_measure.h and not at all on the keyboard); and the MEASURE
// PROPAGATE's paste-offset editor uses MeasureOffset (one signed decimal
// integer, likewise judged at its commit and not on the keyboard). THERE ARE
// SIX KINDS AND FOUR OF THEM ARE DIALOG EDITORS — the MeasureText kind was
// architect-blessed 2026-08-19 and MeasureOffset arrived with the measure
// propagate on 2026-08-20; TWO KINDS RETIRED WHOLE on 2026-08-28 (architect,
// R22/R23: "we're not allowing free-form typing there") — LoadInPlace, the
// `h` view's typed load prompt, and OpenProject, the Open project prompt's
// field, both replaced by the FIELD-LESS PICKER over the folder overlay
// (AppState::Picker, app_state.h), which is a modal owner and not an editor.
// THIS ENUM IS THE AUTHORITATIVE LIST of the editors, and the roster that
// matters for modality is AppState::dialog_editor_session, which NAMES the
// four.
enum class Kind {
    FlagPayload,
    BpmBracket,
    SettingsAssignment,
    CommitTitle,
    MeasureText,
    MeasureOffset,
};

// THE MODAL SESSION ID SOURCE — one monotonic counter for the whole program,
// handing out an id that names exactly ONE raise of exactly ONE modal surface
// for the life of the process (it starts at 1, so 0 is reliably "no session").
//
// IT IS HOMED HERE because the text editors are the most numerous of the
// product's modal surfaces and `enter` below is their one activation route,
// which is what makes the stamping STRUCTURAL rather than disciplinary: no
// opener can forget to take an id, and each new dialog editor has inherited
// the identity for free — the commit-title editor in 2026-08-07 and the
// measure paste-offset editor in 2026-08-20, neither touching this counter.
// THE THREE SURFACES THAT ARE NOT EDITORS take their ids from this same
// counter at their own one raise route each — the PROMPT (PromptState::present,
// app_state.h), the RENDER PLAYER (GuiRenderPlayer::open) and the PICKER
// (GuiInputHandler::open_project_picker, through
// AppState::Picker) — so the ids never collide across the classes and one
// integer compare answers "is this published geometry the surface that owns
// input right now" (the doctrine and the comparison's one owner are at
// AppState::ModalDialogGeometry and
// GuiInputHandler::modal_dialog_stash_current).
//
// THE COUNTER IS THE PROCESS'S, NOT A PROJECT'S: it is a function-local static
// that lives for the life of the process and only ever grows, so ids never
// repeat across the reopens gui_main's loop runs (main.cpp) — a geometry
// published under one project's session can never be mistaken for a surface
// raised under the next.
uint64_t next_session_id();

// State for a single editable rect.
struct State {
    // Identifier of the entity being edited. -1 means "not editing".
    // The caller decides what this means (a marker index for the flag editor).
    int target = -1;

    // THIS EDITING SESSION'S ID, taken from next_session_id() above at every
    // `enter` and never rewritten while the session stands (deactivate leaves
    // it alone — `target` is what says "not editing", and a dead session's id
    // must not be reusable). The TOP-STRIP FLAG editor takes one too and
    // nothing ever reads it: it publishes no dialog, so it has no geometry to
    // validate. What the four DIALOG editors' ids are for is at
    // AppState::ModalDialogGeometry.
    uint64_t session = 0;

    // Vocabulary discriminator. The caller sets this in `enter()` and
    // the keystroke handler routes printable detection accordingly.
    Kind kind = Kind::FlagPayload;

    // When true on a FlagPayload editor, the accepted grammar is
    // widened to admit the inline iteration bracket characters
    // (`+ - [ ] ,`) and the longer FlagIter length cap. Set at `enter()`
    // from iteration_mode_enabled; does nothing for other kinds.
    bool iter_grammar = false;

    // Editable text — the whole of what the editor holds and the whole of what
    // its painter draws. A surface whose value has uneditable neighbours (the
    // marker's position and disabled bit for the flag payload, the "BPM: "
    // label for the bracket editor) leaves them to the marker's own fields and
    // to the painter; this buffer never carries them.
    std::string pending;

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
    // THE PAINTERS WRITE IT AND THE PAINTERS READ IT — two of them since
    // 2026-08-13, one rule. THE FLAG EDITOR's unrolled box is clamped fully
    // on-window and THE DIALOG EDITORS' inset field is a fixed 520px, so on
    // both surfaces a long buffer does not fit and the view must travel to
    // keep the caret visible.
    //
    // THE MINIMAL-TRAVEL RULE, which both painters apply and neither may
    // reinvent: each frame, scroll only as far as the caret demands and in
    // whichever direction it left the window, then clamp to the run's own
    // travel (so the view never shows blank space past the end of the text).
    // A caret walking right pushes the view right one glyph at a time and
    // walking back left pulls it back the same way — it never jumps, and it
    // never moves at all while the caret sits inside the window. The caret's
    // own column is RESERVED at the right edge so a caret at end-of-text
    // stands fully inside rather than half past it.
    //
    // THE CLICK-TO-BYTE PATH NEEDS NO TERM FOR IT: each painter folds the
    // offset into the ORIGIN it publishes (FlagEditorBox::text_origin_x,
    // AppState::DialogEditorText::text_origin_x — both "the window x byte 0
    // paints at"), so editor_byte_index_at's nearest-boundary search, the
    // F2.1 text drag and the double-click word select all land correctly on a
    // scrolled field with no knowledge that it scrolled.
    //
    // It lives on State rather than beside either geometry because it is
    // SESSION state: it must survive from frame to frame — recomputing it from
    // nothing each paint would jitter a caret resting mid-string — and must
    // die with the edit. enter/deactivate already own exactly that lifetime
    // and are its only reset sites; a change of which editor a dialog hosts is
    // structurally a change of State, so nothing else needs keeping in step.
    double view_offset_px = 0.0;

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

// Begin editing `target` with the given seed pending.
// Cursor lands at end of pending. `kind` selects the vocabulary the
// keystroke handler will accept while this editor is active.
void enter(State& s, int target,
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
    // A TYPED CHARACTER THE FIELD HAD NO ROOM FOR (architect 2026-08-30, the
    // strictness ruling): consumed exactly as Consumed is — the buffer and the
    // selection are untouched and the field is red — but told apart from it so
    // the DISPATCH LAYER can say why. This module has no GuiNotifications and
    // wants none (it is the pure editor over one buffer), so the refusal is
    // REPORTED here and CARDED at route_modal_editor_key, the one place every
    // editor's keys pass through.
    OverCapacity,
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
//
// IT ANSWERS FALSE ON EXACTLY THAT REFUSAL (architect 2026-08-30, the
// strictness ruling; true on every applied path, the empty-insert shrink
// included) so the caller can say WHY the field went red — the paste's card is
// raised by the input handler, which knows a press happened, this module having
// no notifications of its own. The keyboard's own over-capacity refusal
// reports through KeyAction::OverCapacity instead.
std::string selected_text(const State& s);
bool        replace_selection(State& s, const std::string& raw);

// Render-side helper: returns true if the cursor should be drawn this
// frame. Period is 1000ms (~500ms on, ~500ms off) and resets at every
// `pending` mutation so the cursor stays visible immediately after a
// keystroke.
bool cursor_visible_now(const State& s);

// RESTART THE BLINK PHASE — the caret becomes visible NOW and the period runs
// from here. Every editing key inside this module calls it for the reason
// above; it is PUBLIC for one caller outside (2026-08-13), the modal dialog's
// focus ring, which restarts the phase when the ring walks back onto the FIELD
// so a field that has just been focused shows its caret at once rather than
// possibly landing in the dark half of a period it kept running through.
void touch_blink(State& s);

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
