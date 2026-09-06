#pragma once

#include "gui_input.h"
#include "marker_measure.h"
#include "value_format.h"

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
// EVERY CAP IS ITS GRAMMAR'S WIDEST SPELLING (architect 2026-09-05): a field
// whose cap is wider than what it can legally commit advertises longer typing
// than it allows, which is the truthfulness defect the roster answers. FIVE
// CAPS ARE TIGHT BOUNDS and each derives its own — the flag payload, the
// iteration bound, the measure (which takes the load bound from its owner),
// the measure paste's offset, and, since 2026-09-06, the BPM bracket, whose
// beats field stopped admitting leading zeros and so gained a widest spelling
// like every other field of that grammar. THE OTHER TWO ARE POLICY CEILINGS
// and each says so where it stands: the settings value and the commit title
// are FREE TEXT, with no widest spelling to be tight against.
//
// Maximum bytes allowed in `pending`, and it belongs to the FLAG PAYLOAD
// editor alone: every other Kind names its own cap below, so this default is
// what FlagPayload reads and nothing else does.
//
// THE DERIVATION. The commit runs `parse_single_canonical_line` — the very
// parse the load runs (warpmarkers_parse.cpp owns the grammar) — so the
// widest committable payload is the widest LOADABLE one. The forms are
// `pass`, `x.yz`, `pass:x.yz`, `N.NN`, `N.NN*SCALE`, `N.NN:x.yz` and
// `N.NN*SCALE:x.yz`, so the widest is TEMPO + `*` + SCALE + `:` + LABEL:
//   TEMPO   4 bytes — `4.00`. parse_tempo_cents pins exactly the N.NN
//           spelling and the bracket pins the integer part to one digit at
//           kTempoMaxCents; the assert below keeps that honest across a
//           bracket retune.
//   SCALE  18 bytes, and this is the term that had to be ANSWERED rather
//           than assumed, the scale being a full double. THE READER IS NOT
//           FREE: parse_positive_value's last arm refuses any spelling that
//           is not the WRITER'S (`format_value_double(v, 4) != s`), and
//           parse_value_double refuses every alphabetic byte before it. So
//           no exponent form, no `01.5000`, and no long digit string that
//           canonicalizes shorter — `1.000000000000000000001` parses and is
//           then refused for not being its own canonical spelling. What is
//           left is exactly the shortest round-trip spelling of a double
//           inside [kScaleMin, kScaleMax] padded to four decimals: one
//           integer digit, the point, and at most SIXTEEN fraction digits —
//           sixteen because the bracket's own ulp is at least 2^-53, wider
//           than 1e-16, so a sixteen-decimal value always sits inside a
//           double's rounding interval there. The bound is attained, at
//           `0.5000000000000001`.
//   LABEL   4 bytes exactly (is_valid_label_format: a lowercase letter, the
//           dot, two lowercase alphanumerics).
// 4 + 1 + 18 + 1 + 4 = 28, spelled whole: `4.00*0.5000000000000001:a.aa`.
// A TIGHT BOUND, not a policy cap. (It was 52 until 2026-09-05, sized for a
// full-double BASE from before tempo became integer cents pinned to N.NN; the
// remainder was advertising typing the commit refuses.)
//
// An operation that would grow the pending past this cap refuses atomically
// and sets the red state (the whole edit lands or the buffer is untouched);
// shrinking and non-growing edits are always accepted. THAT SHRINK LATITUDE
// IS NEVER EXERCISED HERE, and the roster's honest home for it is the
// settings value (kMaxPendingCharsSettings below), whose free text does load
// wider from disk than the editor will grow: this field's seed is composed
// from the store (flag_text, flag_editor.cpp) and the store's every road —
// the load, the history walk, the load in place — comes through the strict
// canonical-line parser that this cap is derived from, so no over-cap
// payload can reach the editor at all.
static_assert(kTempoMaxCents < 1000,
              "a tempo's integer part no longer fits one digit");
constexpr int kMaxPendingChars = 28;
// The ITERATION BOUND editor (a double-click or Enter on one of the two bound
// cells a flag grows in iteration mode). ITS GRAMMAR IS FIXED-WIDTH: a sign,
// one integer digit, the point, two decimals (format_signed_delta_cents,
// warpmarkers.h — the sign is the cells' whole syntax). The integer digit is
// single because the walls the commit and the arrows' step share — the clamp
// window clamp_iter_bracket_to_tempo_bracket states — hold every bound inside
// kTempoMaxCents - kTempoMinCents, i.e. 3.75. So `-3.75` is the widest token
// the field can commit and FIVE BYTES is the cap, a tight bound rather than a
// policy one; the assert keeps the derivation honest across a bracket retune.
// THE CAP AND THE JUDGE AGREE EXACTLY: the commit's reader
// (parse_signed_2dp_cents, flag_editor.cpp) owns that grammar and takes those
// five bytes and nothing else — no surrounding whitespace, no leading zero,
// no second integer digit — so the cap advertises exactly the spellings the
// commit accepts, and a pending arriving by any other road is judged the same.
// (It was 8 until 2026-09-05, left wide so an over-wide spelling would be
// refused by the commit's own name for it. The sixth character is refused by
// the field-full card instead, which is the truthful refusal for a field that
// could never commit one.)
static_assert(kTempoMaxCents - kTempoMinCents < 1000,
              "an iteration bound's integer part no longer fits one digit");
constexpr int kMaxPendingCharsIterBound = 5;
// BPM popup, `<beats>@[<lo>,<hi>]` (parse_bpm_bracket, warpmarkers.h — the
// grammar's one owner):
//   BEATS  4 bytes, a positive integer with no leading zeros, capped at
//          kBpmBeatsMax — so `9999` is the widest there is.
//   LO/HI  18 bytes each. Each is pinned by an equality test to the bpm
//          writer's own spelling (format_value_double at 0 decimals, i.e.
//          to_chars' shortest round-trip form) inside [kBpmMin, kBpmMax] =
//          [10, 400]. A shortest round-trip double never needs more than
//          SEVENTEEN significant digits, and in that bracket every digit of
//          the fixed spelling is significant (the leading integer digit is
//          non-zero, and there are two or three of them), so the spelling is
//          at most seventeen digits plus the point; to_chars picks the
//          scientific form only where it is SHORTER, so 18 bounds both. The
//          bound is attained, at `10.000000000000002` — the double one ulp
//          above 10, whose rounding interval (half an ulp, 2^-50 wide) holds
//          no sixteen-digit decimal.
// 4 + 2 + 18 + 1 + 18 + 1 = 44, and the whole 44 is attained together:
// `9999@[10.000000000000002,10.000000000000004]` is in bracket, in order and
// spelled canonically throughout.
//
// A TIGHT BOUND SINCE 2026-09-06, when the beats arm stopped admitting
// leading zeros (architect: "make it more strict") and the grammar gained one
// spelling per value in every field. It was a POLICY 60 until then — sized
// for a ten-digit beats count and two 23-byte doubles — because a field
// whose widest legal spelling was `however many bytes the field will hold`
// had no tight bound to be derived.
static_assert(kBpmBeatsMax < 10000,
              "a beats count no longer fits four digits");
static_assert(kBpmMin >= 1.0 && kBpmMax < 1000.0,
              "a bpm bound no longer fits three significant integer digits");
constexpr int kMaxPendingCharsBpm = 44;
// Settings prompt. A POLICY CEILING — free text has no widest spelling to be
// tight against. Sized for the free-text provenance fields (url, notes),
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
// <id>` and free UTF-8 text the user may rewrite. A POLICY CEILING — free
// text has no widest spelling — and 256 is generous, well past the
// ~50-character summary line a commit title is written to be.
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
// paste writes. 4 is exactly the longest spelling that could produce a result:
// the offset is applied to a measure number bracketed at kMeasureMaxWhole
// (marker_measure.h), so an offset outside +/-kMeasureMaxWhole could not
// carry any in-bracket source to an in-bracket result, and `-999` is four
// bytes. A tight bound rather than a policy cap, the measure field's own
// arrangement. (It was 6, `-99999`, and came down with the measure ceiling on
// 2026-09-05 — architect approval that date for the frozen header the ceiling
// lives in.)
constexpr int kMaxPendingCharsMeasureOffset = 4;

// Vocabulary the editor accepts on the keyboard. Different call sites
// edit different payload shapes; the kind now selects only the length cap
// (handle_key accepts any character-bearing key and defers grammar to the
// commit-time validator). The flag editor uses FlagPayload (the plain
// canonical payload — tempo, scale, labels, never a bracket); the BPM popup
// uses BpmBracket; the settings-prompt editor uses SettingsAssignment
// (`key=value`); the history mode's commit-title editor uses CommitTitle
// (free one-line text, the message the checkpoint commit carries); the
// MARKER MEASURE editor uses MeasureText (the ` //<measure>` suffix a marker
// line may carry — an ASCII GRAMMAR since the field's 2026-08-20 rebrand,
// judged at the commit by marker_measure.h and not at all on the keyboard);
// the MEASURE PROPAGATE's paste-offset editor uses MeasureOffset (one signed
// decimal integer, likewise judged at its commit and not on the keyboard);
// and the ITERATION BOUND editor uses IterBound (one signed two-decimal
// bound, the text of one of the two bound cells a flag grows in iteration
// mode — which of the two is State::iter_upper below — judged at its commit
// against the bracket's walls). THERE ARE SEVEN KINDS AND FOUR OF THEM ARE
// DIALOG EDITORS; the three top-strip kinds (FlagPayload, MeasureText,
// IterBound) share the flag editor's State and paint in the marker lane.
// The MeasureText kind was architect-blessed 2026-08-19, MeasureOffset
// arrived with the measure propagate on 2026-08-20 and IterBound on
// 2026-09-05, when every cell became a mini flag with its own editor; TWO
// KINDS RETIRED WHOLE on 2026-08-28 (architect, R22/R23: "we're not allowing
// free-form typing there") — LoadInPlace, the `h` view's typed load prompt,
// and OpenProject, the Open project prompt's field, both replaced by the
// FIELD-LESS PICKER over the folder overlay (AppState::Picker, app_state.h),
// which is a modal owner and not an editor. THIS ENUM IS THE AUTHORITATIVE
// LIST of the editors, and the roster that matters for modality is
// AppState::dialog_editor_session, which NAMES the four.
enum class Kind {
    FlagPayload,
    BpmBracket,
    SettingsAssignment,
    CommitTitle,
    MeasureText,
    MeasureOffset,
    IterBound,
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

    // WHICH BOUND AN IterBound SESSION EDITS: false the lower cell, true the
    // upper. A property of the session beside `target`, set at `enter()` and
    // meaningless on every other kind (the flag editor derives the cell from
    // it through iter_bound_editor_side, app_state.h, so no reader spells
    // the bool's meaning twice).
    bool iter_upper = false;

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
    // 2026-08-13, one rule. THE DIALOG EDITORS' inset field is a fixed 520px,
    // so a long buffer does not fit it and the view must travel glyph by
    // glyph to keep the caret visible. THE MARKER-LANE FIELD IS THE OTHER
    // CASE ENTIRELY SINCE 2026-09-06: its box is its own two pads and its own
    // run — no lane cap, no on-window clamp — so the whole run is always
    // inside it and the travel it asks for is SUB-PIXEL, the box having
    // rounded the shaped width down to a whole column and left the caret's
    // reserved column on the clip's right edge. A field cut off at a window
    // edge is not this field's business: the box stands where its box stands
    // and the VIEWPORT PAN is what brings it into view (render.h's
    // render_flag_editor_box carries the ruling).
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
// keystroke handler will accept while this editor is active; `iter_upper`
// names the bound an IterBound session edits and is ignored by every other
// kind.
void enter(State& s, int target,
           std::string initial_pending,
           Kind kind = Kind::FlagPayload,
           bool iter_upper = false);

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
// replace_selection is the paste/cut primitive, THE ONE INCOMING-TEXT FILTER,
// and THE ONE OWNER OF THE PER-KIND BYTE CAP — every route that grows a
// pending buffer comes through it: the paste and the cut (input_handler), the
// settings editor's completion append and its recall prefix
// (settings_editor.cpp), and THE TYPED KEYSTROKE, which handle_key encodes and
// hands over rather than repeating the ladder (its own contribution is the
// KeyAction::OverCapacity it returns when this refuses). Its acceptance rule:
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
// no notifications of its own. The keyboard's over-capacity refusal is the
// SAME verdict wearing another return: handle_key turns this false into
// KeyAction::OverCapacity, which is what its own dispatch layer cards.
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

// THE DOUBLE-CLICK-DRAG'S EXTENSION (2026-09-05): grow the selection BY WORDS
// from a fixed anchor run — [anchor_start, anchor_end), the run
// select_word_at selected at the double-click — toward the byte under the
// pointer, `pos`. The moving end snaps OUTWARD to the boundary of the run
// under the pointer (select_word_at's own three-class rule, never a different
// one): a pointer past the anchor's end selects from anchor_start to that
// run's end, a pointer before the anchor's start selects from that run's
// start to anchor_end, and a pointer back inside the anchor run leaves just
// that run selected. It writes selection_anchor and cursor_pos so the
// cursor rides the moving end (the keyboard's shift-extend shape) and
// restarts the blink; an empty pending selects nothing. Called by the input
// handler's text-drag motion while the drag it armed at a double-click is
// live (EditorTextDragState::by_words).
void extend_selection_by_words(State& s, int anchor_start, int anchor_end,
                               int pos);

} // namespace text_editor
