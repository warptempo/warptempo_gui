#include "text_editor.h"

#include <algorithm>
#include <cmath>

namespace text_editor {

namespace {

void touch_blink(State& s) {
    s.blink_epoch = std::chrono::steady_clock::now();
}

// Remove the selected range from `pending`, place the cursor at the
// selection start, and clear the anchor. No-op if no selection.
void erase_selection(State& s) {
    if (!has_selection(s)) return;
    const int a = selection_start(s);
    const int b = selection_end(s);
    s.pending.erase(static_cast<size_t>(a),
                    static_cast<size_t>(b - a));
    s.cursor_pos = a;
    s.selection_anchor = -1;
}

bool is_word_char(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z');
}

// Index of the boundary to the left of `pos`: skip separators
// immediately left, then the word run beyond them.
int prev_word_boundary(const std::string& s, int pos) {
    int i = pos;
    while (i > 0 && !is_word_char(s[static_cast<size_t>(i - 1)])) --i;
    while (i > 0 &&  is_word_char(s[static_cast<size_t>(i - 1)])) --i;
    return i;
}

// Index of the boundary to the right of `pos`: skip separators
// immediately right, then the word run beyond them.
int next_word_boundary(const std::string& s, int pos) {
    const int n = static_cast<int>(s.size());
    int i = pos;
    while (i < n && !is_word_char(s[static_cast<size_t>(i)])) ++i;
    while (i < n &&  is_word_char(s[static_cast<size_t>(i)])) ++i;
    return i;
}

// Character class for the double-click run selector (below). Three classes,
// the Qt/Kate "programmer's word" convention: WORD is [A-Za-z0-9_] plus any
// byte >= 0x80 (UTF-8 lead/continuation bytes classify as word so multibyte
// characters stay whole — the free-text fields carry UTF-8); WHITESPACE is
// ' ' and '\t'; PUNCTUATION is every other byte. Distinct from the
// cursor-motion word class above (which excludes '_' and treats every
// non-word byte as a separator to skip). The >= 0x80 rule guarantees only that
// a selection never SPLITS a multibyte character and keeps a correctly-probed
// word whole; it does NOT correct a byte-shifted probe when non-ASCII earlier in
// the line has already shifted the click-to-byte mapping (see
// byte_index_from_click_x's accepted-limitation note).
enum class CharClass { Word, Whitespace, Punctuation };

CharClass classify(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u >= 0x80) return CharClass::Word;
    if (u == ' ' || u == '\t') return CharClass::Whitespace;
    if ((c >= '0' && c <= '9') ||
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        c == '_') return CharClass::Word;
    return CharClass::Punctuation;
}

} // namespace

// Monospace mapping: one glyph == one `advance`, and the returned index is a
// BYTE offset. That equates byte to glyph, which holds only for ASCII — Cairo
// renders a multibyte UTF-8 character as ONE glyph, so any non-ASCII byte earlier
// in the line shifts every later click target (and the byte-derived highlight
// geometry). Accepted limitation, no correction: the program-generated corpus is
// ASCII, and non-ASCII is reachable only through user-created content (e.g. a
// hand-renamed render entry). Pre-existing and shared by the caret path — the
// click-to-caret site funnels through this same mapping with the identical shift.
int byte_index_from_click_x(double click_x, double text_left_x,
                            double advance, int pending_size) {
    const double offset = click_x - text_left_x;
    int idx = static_cast<int>(std::nearbyint(offset / advance));
    return std::clamp(idx, 0, pending_size);
}

void select_word_at(State& s, int pos) {
    const int n = static_cast<int>(s.pending.size());
    if (n <= 0) {
        s.selection_anchor = -1;
        s.cursor_pos = 0;
        return;
    }
    pos = std::clamp(pos, 0, n);
    // Select the maximal run of the clicked character's class, never crossing a
    // class change — the desktop double-click convention (GTK/Qt/Kate/Firefox),
    // deliberately distinct from the Ctrl+Left/Right skip-separators scanners. A
    // click on punctuation selects that punctuation run, not the following word.
    // pos == n (click past the last glyph) has no character AT n, so classify by
    // n-1 and select the trailing run.
    const int probe = (pos == n) ? pos - 1 : pos;
    const CharClass cls = classify(s.pending[static_cast<size_t>(probe)]);
    int start = probe;
    while (start > 0 &&
           classify(s.pending[static_cast<size_t>(start - 1)]) == cls) --start;
    int end = probe + 1;
    while (end < n &&
           classify(s.pending[static_cast<size_t>(end)]) == cls) ++end;
    // A run is always at least one character when pending is non-empty, so the
    // empty-selection arm above survives only for the empty-pending case.
    s.selection_anchor = start;
    s.cursor_pos       = end;
    touch_blink(s);
}

std::string selected_text(const State& s) {
    if (!has_selection(s)) return std::string();
    const int a = selection_start(s);
    const int b = selection_end(s);
    return s.pending.substr(static_cast<size_t>(a),
                            static_cast<size_t>(b - a));
}

void replace_selection(State& s, const std::string& raw) {
    // Filter the incoming text to printable ASCII (dropping non-printables is
    // vocabulary filtering, not data loss); this is the `ins` length below.
    std::string clean;
    clean.reserve(raw.size());
    for (unsigned char c : raw) {
        if (c >= 0x20 && c <= 0x7e) clean.push_back(static_cast<char>(c));
    }
    int cap = kMaxPendingChars;
    if (s.kind == Kind::BpmBracket)         cap = kMaxPendingCharsBpm;
    if (s.kind == Kind::SettingsAssignment) cap = kMaxPendingCharsSettings;
    if (s.kind == Kind::RenderCommit)       cap = kMaxPendingCharsRenderCommit;
    if (s.kind == Kind::FlagPayload && s.iter_grammar)
        cap = kMaxPendingCharsFlagIter;
    // Atomic cap: compute the result size BEFORE mutating anything. Refuse
    // exactly when the operation would push the pending past the cap AND grow
    // it — a non-growing edit (shorter replacement, empty insert from cut) is
    // always accepted so an over-cap pending seeded from a hand-edited file
    // can still be trimmed back.
    const int old_size = static_cast<int>(s.pending.size());
    const int sel      = has_selection(s) ? selection_end(s) - selection_start(s)
                                          : 0;
    const int ins      = static_cast<int>(clean.size());
    const int new_size = old_size - sel + ins;
    if (new_size > cap && new_size > old_size) {
        s.red = true;
        touch_blink(s);
        return;
    }
    if (has_selection(s)) erase_selection(s);
    s.pending.insert(static_cast<size_t>(s.cursor_pos), clean);
    s.cursor_pos += ins;
    s.red = false;
    touch_blink(s);
}

void deactivate(State& s) {
    s.target            = -1;
    s.kind              = Kind::FlagPayload;
    s.iter_grammar      = false;
    s.pending.clear();
    s.locked_prefix.clear();
    s.cursor_pos        = 0;
    s.selection_anchor  = -1;
    s.red               = false;
}

void enter(State& s, int target,
           std::string locked_prefix,
           std::string initial_pending,
           Kind kind,
           bool iter_grammar) {
    s.target            = target;
    s.kind              = kind;
    s.iter_grammar      = iter_grammar;
    s.locked_prefix     = std::move(locked_prefix);
    s.pending           = std::move(initial_pending);
    s.cursor_pos        = static_cast<int>(s.pending.size());
    s.selection_anchor  = -1;
    s.red               = false;
    touch_blink(s);
}

KeyClass classify_key(GuiKey key, GuiInputState mods) {
    // Precedence mirrors handle_key's branch order exactly (see the header):
    // session first, then the Ctrl chords, then motion (ctrl/shift, never alt),
    // then printable. Each test returns before the next, so a key satisfying two
    // predicates classifies as the branch handle_key would act on.
    //
    // NO ARM ADMITS ALT. Session is bare-exact, the chords are ctrl-exact,
    // motion takes ctrl/shift only, printable already excludes ctrl and alt — so
    // a press wearing a modifier the arm does not bind falls all the way to
    // NotEditorKey and is dropped by the keyboard-modal gate before it can reach
    // an editor at all. That is the whole enforcement: unbound is unbound, and
    // nothing downstream distinguishes it from any other key the editor does not
    // own.
    //
    // Session keys are BARE-EXACT (the strict-modifier rule): a modified
    // Escape / Return / KpEnter has no binding, so it must not cancel or commit
    // an edit — Ctrl+Enter in particular must not commit the settings, commit,
    // flag, or bpm editor, and the bpm commit dispatches a render batch. Escape
    // and Return produce sub-0x20 codepoints, so neither can reappear as a
    // printable below.
    if ((key == GuiKeys::Escape ||
         key == GuiKeys::Return || key == GuiKeys::KpEnter) &&
        !mods.ctrl && !mods.shift && !mods.alt)
        return KeyClass::SessionKey;
    // The chords are CTRL-EXACT (architect 2026-07-28, the same strict-modifier
    // rule): Ctrl+Shift+V must not paste and Ctrl+Alt+A must not select all.
    // The `mods.ctrl` term is load-bearing beyond strictness: without it a BARE
    // a/c/x/v would stop being a typed character, and bare `c` / `x` are global
    // bindings the editor deliberately shadows by typing them.
    // Ctrl+Left is not a chord here — the shape list is A/C/X/V only, so it is
    // motion below.
    if (mods.ctrl && !mods.shift && !mods.alt &&
        (key == GuiKeys::A || key == GuiKeys::C ||
         key == GuiKeys::X || key == GuiKeys::V))
        return KeyClass::ChordKey;
    // Motion / editing takes CTRL AND SHIFT IN EVERY COMBINATION but NEVER ALT
    // (architect 2026-07-28). The asymmetry is the point, so do not "finish the
    // job" by making these exact too:
    //   - Ctrl and Shift carry real meaning on all six keys — Shift extends the
    //     selection, Ctrl jumps / erases by WORD, Ctrl+Shift extends by word —
    //     and where a combination has no distinct behavior it degrades to the
    //     plain gesture exactly as an ordinary one-line text field does
    //     (Ctrl+Home still goes to the start, Shift+BackSpace still backspaces).
    //     Making them exact would break those.
    //   - ALT binds nothing anywhere in this family; no standard one-line field
    //     uses it. So an alt-carrying motion press is genuinely UNBOUND, and the
    //     strict-modifier rule makes it a no-op: it falls to NotEditorKey and the
    //     keyboard-modal gate drops it before any editor sees it.
    if (!mods.alt &&
        (key == GuiKeys::Left || key == GuiKeys::Right ||
         key == GuiKeys::Home || key == GuiKeys::End ||
         key == GuiKeys::BackSpace || key == GuiKeys::Delete))
        return KeyClass::MotionEditKey;
    // Printable insertion: no Ctrl/Alt, printable ASCII codepoint.
    if (!mods.ctrl && !mods.alt &&
        mods.codepoint >= 0x20 && mods.codepoint <= 0x7e)
        return KeyClass::PrintableKey;
    return KeyClass::NotEditorKey;
}

KeyAction handle_key(State& s, GuiKey key, GuiInputState mods) {
    if (!is_active(s)) return KeyAction::NotConsumed;

    // Membership gate: the classifier is the single truth of which
    // keys the editor owns. NotEditorKey falls through here; every other class
    // is covered by a branch below that returns a consumed action, so no
    // classified key can reach the function-end NotConsumed fallback — a key
    // added to a branch but not the classifier is dead (benign), and a key
    // classified as owned but missing from the branches would be an error the
    // coverage walk forbids.
    if (classify_key(key, mods) == KeyClass::NotEditorKey)
        return KeyAction::NotConsumed;

    // Only ctrl and shift are ever read below, and `mods.alt` is FALSE on every
    // press that reaches this line: no classifier arm admits alt. That is why no
    // branch tests it.
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;

    // The session and chord branches below need no modifier tests of their own:
    // the membership gate above already rejected every modified Escape / Return /
    // KpEnter and every Ctrl+Shift / Ctrl+Alt A/C/X/V as NotEditorKey, so only
    // the exactly-bound presses reach here. The classifier is the single owner
    // of those predicates.
    if (key == GuiKeys::Escape) {
        return KeyAction::CancelRequested;
    }
    if (key == GuiKeys::Return || key == GuiKeys::KpEnter) {
        return KeyAction::CommitRequested;
    }

    // Ctrl+A: select-all. Sits above the printable-detect path so the
    // `a` key doesn't fall through and insert a literal 'a'.
    if (ctrl && key == GuiKeys::A) {
        if (!s.pending.empty()) {
            s.selection_anchor = 0;
            s.cursor_pos = static_cast<int>(s.pending.size());
            touch_blink(s);
        }
        return KeyAction::Consumed;
    }

    // Ctrl+C / Ctrl+X / Ctrl+V: clipboard. These sit in the editor's owned
    // keymap (returning a handled action), so they never fall through to the
    // global dispatch; Ctrl+C/X/V are unbound globally, so no conflict. Copy
    // and cut are a no-op (plain Consumed) without a selection; paste always
    // requests — the input handler applies the action against the internal
    // session clipboard.
    if (ctrl && key == GuiKeys::C) {
        return has_selection(s) ? KeyAction::CopyRequested
                                : KeyAction::Consumed;
    }
    if (ctrl && key == GuiKeys::X) {
        return has_selection(s) ? KeyAction::CutRequested
                                : KeyAction::Consumed;
    }
    if (ctrl && key == GuiKeys::V) {
        return KeyAction::PasteRequested;
    }

    // Cursor motion. Shift extends a selection from an anchor; bare
    // motion collapses any existing selection to the corresponding edge.
    // These branches read ctrl and shift and NEVER alt, and that is a CONTRACT,
    // not an oversight: the membership gate above classifies an alt-carrying
    // motion press as NotEditorKey, so `mods.alt` is false on every press that
    // reaches here. Do not add an alt arm — alt binds nothing in this family,
    // and an alt press is dropped at the keyboard-modal gate long before this.
    if (key == GuiKeys::Left) {
        if (ctrl) {
            // Word-left: optionally extend the selection (Shift), else
            // collapse it, then jump the cursor to the previous boundary.
            const int b = prev_word_boundary(s.pending, s.cursor_pos);
            if (shift) {
                if (s.selection_anchor < 0) s.selection_anchor = s.cursor_pos;
            } else {
                s.selection_anchor = -1;
            }
            s.cursor_pos = b;
            touch_blink(s);
            return KeyAction::Consumed;
        }
        if (shift) {
            if (s.selection_anchor < 0) s.selection_anchor = s.cursor_pos;
            if (s.cursor_pos > 0) s.cursor_pos--;
        } else {
            if (has_selection(s)) {
                s.cursor_pos = selection_start(s);
            } else if (s.cursor_pos > 0) {
                s.cursor_pos--;
            }
            s.selection_anchor = -1;
        }
        touch_blink(s);
        return KeyAction::Consumed;
    }
    if (key == GuiKeys::Right) {
        if (ctrl) {
            const int b = next_word_boundary(s.pending, s.cursor_pos);
            if (shift) {
                if (s.selection_anchor < 0) s.selection_anchor = s.cursor_pos;
            } else {
                s.selection_anchor = -1;
            }
            s.cursor_pos = b;
            touch_blink(s);
            return KeyAction::Consumed;
        }
        if (shift) {
            if (s.selection_anchor < 0) s.selection_anchor = s.cursor_pos;
            if (s.cursor_pos < static_cast<int>(s.pending.size())) {
                s.cursor_pos++;
            }
        } else {
            if (has_selection(s)) {
                s.cursor_pos = selection_end(s);
            } else if (s.cursor_pos <
                       static_cast<int>(s.pending.size())) {
                s.cursor_pos++;
            }
            s.selection_anchor = -1;
        }
        touch_blink(s);
        return KeyAction::Consumed;
    }
    if (key == GuiKeys::Home) {
        if (shift) {
            if (s.selection_anchor < 0) s.selection_anchor = s.cursor_pos;
        } else {
            s.selection_anchor = -1;
        }
        s.cursor_pos = 0;
        touch_blink(s);
        return KeyAction::Consumed;
    }
    if (key == GuiKeys::End) {
        if (shift) {
            if (s.selection_anchor < 0) s.selection_anchor = s.cursor_pos;
        } else {
            s.selection_anchor = -1;
        }
        s.cursor_pos = static_cast<int>(s.pending.size());
        touch_blink(s);
        return KeyAction::Consumed;
    }

    // Editing.
    if (key == GuiKeys::BackSpace) {
        if (has_selection(s)) {
            erase_selection(s);
            s.red = false;
        } else {
            // Editing keys collapse degenerate anchors before cursor movement can resurrect phantom selections.
            s.selection_anchor = -1;
            if (ctrl) {
                const int b = prev_word_boundary(s.pending, s.cursor_pos);
                if (b < s.cursor_pos) {
                    s.pending.erase(static_cast<size_t>(b),
                                    static_cast<size_t>(s.cursor_pos - b));
                    s.cursor_pos = b;
                    s.red = false;
                }
            } else if (s.cursor_pos > 0) {
                s.pending.erase(static_cast<size_t>(s.cursor_pos - 1), 1);
                s.cursor_pos--;
                s.red = false;
            }
        }
        touch_blink(s);
        return KeyAction::Consumed;
    }
    if (key == GuiKeys::Delete) {
        if (has_selection(s)) {
            erase_selection(s);
            s.red = false;
        } else {
            s.selection_anchor = -1;
            if (ctrl) {
                const int e = next_word_boundary(s.pending, s.cursor_pos);
                if (e > s.cursor_pos) {
                    s.pending.erase(static_cast<size_t>(s.cursor_pos),
                                    static_cast<size_t>(e - s.cursor_pos));
                    s.red = false;
                }
            } else if (s.cursor_pos < static_cast<int>(s.pending.size())) {
                s.pending.erase(static_cast<size_t>(s.cursor_pos), 1);
                s.red = false;
            }
        }
        touch_blink(s);
        return KeyAction::Consumed;
    }

    // Printable insertion (length-capped). Each Kind's cap admits its
    // longest full-precision value form (see the kMaxPendingChars*
    // comments in text_editor.h).
    // Accept any printable character the keyboard produced. The platform
    // resolved the effective codepoint (shift / layout applied) via
    // xkbcommon; insert it when no Ctrl/Alt is held and it is a printable
    // ASCII character. Characters that are invalid for this field are NOT
    // filtered here — the commit-time validator rejects the value (red
    // flash) when Enter is pressed. This replaces the per-Kind
    // keysym_to_char vocabulary entirely; the only thing Kind still selects
    // is the length cap below.
    if (!ctrl && !mods.alt &&
        mods.codepoint >= 0x20 && mods.codepoint <= 0x7e) {
        const char ch = static_cast<char>(mods.codepoint);
        int cap = kMaxPendingChars;
        if (s.kind == Kind::BpmBracket)         cap = kMaxPendingCharsBpm;
        if (s.kind == Kind::SettingsAssignment) cap = kMaxPendingCharsSettings;
        if (s.kind == Kind::RenderCommit)       cap = kMaxPendingCharsRenderCommit;
        if (s.kind == Kind::FlagPayload && s.iter_grammar)
            cap = kMaxPendingCharsFlagIter;
        // Atomic cap: compute the result size BEFORE erasing the selection,
        // so a refusal leaves the buffer (selection included) untouched.
        // Refuse exactly when the insert would push past the cap AND grow the
        // pending; replacing a full selection with one char shrinks-then-grows
        // within the cap and is accepted.
        const int old_size = static_cast<int>(s.pending.size());
        const int sel      = has_selection(s)
                                 ? selection_end(s) - selection_start(s)
                                 : 0;
        const int new_size = old_size - sel + 1;
        if (new_size > cap && new_size > old_size) {
            s.red = true;
            touch_blink(s);
            return KeyAction::Consumed;
        }
        if (has_selection(s)) {
            erase_selection(s);
        }
        s.pending.insert(static_cast<size_t>(s.cursor_pos), 1, ch);
        s.cursor_pos++;
        s.red = false;
        touch_blink(s);
        return KeyAction::Consumed;
    }

    // The editor does not own this key. Report NotConsumed so the caller can
    // route it (cancel the edit and let the global command run).
    return KeyAction::NotConsumed;
}

bool cursor_visible_now(const State& s) {
    if (!is_active(s)) return false;
    using namespace std::chrono;
    const auto now = steady_clock::now();
    const auto ms  = duration_cast<milliseconds>(now - s.blink_epoch).count();
    // 1000ms period: visible for the first 500ms, hidden for the next 500ms.
    return ((ms / 500) % 2) == 0;
}

} // namespace text_editor
