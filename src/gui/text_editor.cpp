#include "text_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace text_editor {

void touch_blink(State& s) {
    s.blink_epoch = std::chrono::steady_clock::now();
}

namespace {

// ---------------------------------------------------------------------------
// UTF-8, the editor's unit of movement (the contract is at the head of
// text_editor.h). Indices stay BYTE indices — the shaped run addresses by byte
// and so does `pending` — but every step that a user perceives as "one
// character" moves a whole codepoint, so a multi-byte character can never be
// half-deleted or caretted into.
// ---------------------------------------------------------------------------

// THE UTF-8 PRIMITIVE every boundary walk below is built from: a continuation
// byte is 10xxxxxx, and every other byte STARTS a codepoint. That single test
// needs no sequence length and degrades safely on malformed bytes. It is the
// module's own since 2026-08-28 — its one outside caller, the Open prompt's
// prefix completion, went with that prompt's text field.
bool is_utf8_continuation_byte(unsigned char b) { return (b & 0xc0) == 0x80; }

// The codepoint boundary strictly LEFT of `pos` (0 when there is none).
int prev_codepoint_boundary(const std::string& s, int pos) {
    if (pos <= 0) return 0;
    int i = std::min(pos, static_cast<int>(s.size())) - 1;
    while (i > 0 && is_utf8_continuation_byte(
                        static_cast<unsigned char>(s[static_cast<size_t>(i)])))
        --i;
    return i;
}

// The codepoint boundary strictly RIGHT of `pos` (the end when there is none).
int next_codepoint_boundary(const std::string& s, int pos) {
    const int n = static_cast<int>(s.size());
    if (pos >= n) return n;
    int i = std::max(pos, 0) + 1;
    while (i < n && is_utf8_continuation_byte(
                        static_cast<unsigned char>(s[static_cast<size_t>(i)])))
        ++i;
    return i;
}

// The insertable codepoints for the typed path: everything from U+0020 up
// EXCEPT the C0 controls, DEL, the surrogate range, and anything past the
// Unicode maximum. ASCII controls are dropped as they always were (Return and
// Escape produce sub-0x20 codepoints and are session keys, not characters);
// the rest of the exclusions describe values xkb_state_key_get_utf32 does not
// produce and exist so `encode_utf8` below has a stated domain rather than an
// unreachable failure arm. C1 (U+0080..U+009F) is deliberately NOT excluded —
// no keyboard produces one, and adding a rule for a non-producer is the kind of
// guard the validation topology forbids.
bool is_insertable_codepoint(uint32_t cp) {
    return cp >= 0x20 && cp != 0x7f && cp <= 0x10ffff &&
           !(cp >= 0xd800 && cp <= 0xdfff);
}

// Encode ONE insertable codepoint as UTF-8. Precondition:
// is_insertable_codepoint(cp) — the two callers gate on it (handle_key's
// printable branch, which classify_key admitted on the same predicate).
std::string encode_utf8(uint32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
    return out;
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

// The CURSOR-MOTION word class (Ctrl+Left/Right and the word erases), distinct
// from the double-click selector's three-class `classify` below. Any byte >=
// 0x80 counts as a word character, which is what makes the two byte walks below
// codepoint-safe without carrying a decoder: a multi-byte character's bytes are
// ALL >= 0x80, so a run of them is one uniform class and a boundary can never
// land inside one. It also gives the behavior a user expects — a word of
// accented Latin walks as one word.
bool is_word_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u >= 0x80) return true;
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
// byte >= 0x80 (UTF-8 lead and continuation bytes both classify as word, so a
// multi-byte character is one uniform run and a selection edge can never land
// inside one); WHITESPACE is ' ' and '\t'; PUNCTUATION is every other byte.
// Distinct from the cursor-motion word class above, which excludes '_' and
// treats every non-word byte as a separator to skip.
//
// THE PROBE IS EXACT, not a degradation: `pos` comes from
// byte_index_from_shaped_x over the shaped run's OWN per-byte pen offsets, so
// non-ASCII earlier in the line shifts nothing (the pre-shaping monospace
// mapping, which divided x by one advance, is what could be shifted — it died
// with the face).
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

// Nearest boundary, ties to the lower index — the contract is at the
// declaration. Linear over at most kMaxPendingCharsFlagIter + 1 entries, run
// once per click; a binary search would be the same answer with more code.
int byte_index_from_shaped_x(double click_x, double text_origin_x,
                             const std::vector<double>& boundaries) {
    if (boundaries.empty()) return 0;
    const double target = click_x - text_origin_x;
    int    best     = 0;
    double best_gap = std::abs(target - boundaries[0]);
    for (size_t i = 1; i < boundaries.size(); ++i) {
        const double gap = std::abs(target - boundaries[i]);
        if (gap < best_gap) {   // strict: a tie keeps the lower index
            best_gap = gap;
            best     = static_cast<int>(i);
        }
    }
    return best;
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
    // THE ONE INCOMING-TEXT FILTER (the acceptance rule is at the declaration):
    // printable ASCII and WELL-FORMED multi-byte UTF-8 pass through verbatim;
    // ASCII controls (0x00..0x1f and DEL) and MALFORMED bytes are dropped, the
    // rest of the string kept. Dropping is vocabulary filtering, not data loss —
    // an editor buffer must never hold a byte the shaper and the walks below
    // cannot address.
    //
    // Recovery on a malformed byte is the standard one: drop that ONE byte and
    // re-scan from the next, so a single bad byte never eats the good text after
    // it. THE MALFORMED ARM HAS A LIVE PRODUCER since the system clipboard
    // landed (2026-08-02): a Ctrl+V pastes another application's bytes in here
    // unexamined — the platform reads the pipe and hands the payload over
    // whole, deliberately, because this is the one boundary that judges
    // incoming text. The typed path still produces nothing malformed (xkb emits
    // only well-formed UTF-8), and neither does our own copy.
    std::string  clean;
    clean.reserve(raw.size());
    const size_t n = raw.size();
    size_t       i = 0;
    while (i < n) {
        const unsigned char b0 = static_cast<unsigned char>(raw[i]);
        if (b0 < 0x80) {                       // ASCII: printable only
            if (b0 >= 0x20 && b0 != 0x7f) clean.push_back(static_cast<char>(b0));
            ++i;
            continue;
        }
        // A multi-byte sequence: length from the lead byte, then every
        // continuation byte checked, then the decoded value range-checked so an
        // OVERLONG form (a codepoint spelled in more bytes than it needs) and a
        // surrogate or out-of-range value are refused like any other malformed
        // input. A continuation byte appearing alone, or an 0xf8..0xff byte, has
        // no length at all and takes the same single-byte drop.
        size_t   len = 0;
        uint32_t cp  = 0;
        if      ((b0 & 0xe0) == 0xc0) { len = 2; cp = b0 & 0x1fu; }
        else if ((b0 & 0xf0) == 0xe0) { len = 3; cp = b0 & 0x0fu; }
        else if ((b0 & 0xf8) == 0xf0) { len = 4; cp = b0 & 0x07u; }
        else                          { ++i; continue; }
        if (i + len > n) { ++i; continue; }    // truncated at the string's end
        bool ok = true;
        for (size_t k = 1; k < len; ++k) {
            const unsigned char bk = static_cast<unsigned char>(raw[i + k]);
            if (!is_utf8_continuation_byte(bk)) { ok = false; break; }
            cp = (cp << 6) | (bk & 0x3fu);
        }
        if (!ok) { ++i; continue; }
        static constexpr uint32_t kShortest[5] = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < kShortest[len] || !is_insertable_codepoint(cp)) {
            ++i;
            continue;
        }
        clean.append(raw, i, len);
        i += len;
    }
    int cap = kMaxPendingChars;
    if (s.kind == Kind::BpmBracket)         cap = kMaxPendingCharsBpm;
    if (s.kind == Kind::SettingsAssignment) cap = kMaxPendingCharsSettings;
    if (s.kind == Kind::CommitTitle)       cap = kMaxPendingCharsCommitTitle;
    if (s.kind == Kind::MeasureText)       cap = kMaxPendingCharsMeasure;
    if (s.kind == Kind::MeasureOffset)     cap = kMaxPendingCharsMeasureOffset;
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
    s.cursor_pos        = 0;
    s.selection_anchor  = -1;
    s.red               = false;
    s.view_offset_px    = 0.0;
}

// The modal session id source (contract at the declaration, text_editor.h).
// The counter is function-local so it has exactly one home and no order-of-
// initialization to reason about; it starts at 1 so 0 is reliably "no session".
uint64_t next_session_id() {
    static uint64_t next = 0;
    return ++next;
}

void enter(State& s, int target,
           std::string initial_pending,
           Kind kind,
           bool iter_grammar) {
    // EVERY ACTIVATION IS A NEW SESSION, including a retarget of a live editor
    // (the flag editor's) and a reopen of the same editor a keystroke after it
    // closed: the published geometry of the old session must never be able to
    // name the new one.
    s.session           = next_session_id();
    s.target            = target;
    s.kind              = kind;
    s.iter_grammar      = iter_grammar;
    s.pending           = std::move(initial_pending);
    s.cursor_pos        = static_cast<int>(s.pending.size());
    s.selection_anchor  = -1;
    s.red               = false;
    // A fresh session starts unscrolled; the flag editor's painter travels it on
    // the first frame if the seeded cursor (at end of text) is already past the
    // box. The dialog editors leave it at zero for their whole session.
    s.view_offset_px    = 0.0;
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
    // an edit — Ctrl+Enter in particular must not commit the settings, load,
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
    // Printable insertion: no Ctrl/Alt, and a character-bearing codepoint —
    // ASCII or otherwise (is_insertable_codepoint owns the range, and
    // handle_key's branch tests the SAME predicate so membership and effect
    // cannot drift). The platform hands over a full Unicode codepoint
    // (xkb_state_key_get_utf32), so a compose or dead-key sequence classifies
    // here exactly as a letter does.
    if (!mods.ctrl && !mods.alt && is_insertable_codepoint(mods.codepoint))
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
    // requests — the input handler applies the action against the SYSTEM
    // clipboard, and answers a request with nothing on it by doing nothing.
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
        // One CODEPOINT left, not one byte: `pending` carries UTF-8 and a caret
        // must never rest inside a character (the contract is at the head of
        // text_editor.h). The selection endpoints inherit the property, since
        // both are cursor positions.
        if (shift) {
            if (s.selection_anchor < 0) s.selection_anchor = s.cursor_pos;
            s.cursor_pos = prev_codepoint_boundary(s.pending, s.cursor_pos);
        } else {
            if (has_selection(s)) {
                s.cursor_pos = selection_start(s);
            } else {
                s.cursor_pos = prev_codepoint_boundary(s.pending, s.cursor_pos);
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
        // One CODEPOINT right — the mirror of the Left branch above.
        if (shift) {
            if (s.selection_anchor < 0) s.selection_anchor = s.cursor_pos;
            s.cursor_pos = next_codepoint_boundary(s.pending, s.cursor_pos);
        } else {
            if (has_selection(s)) {
                s.cursor_pos = selection_end(s);
            } else {
                s.cursor_pos = next_codepoint_boundary(s.pending, s.cursor_pos);
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
                // A WHOLE CODEPOINT, not a byte — deleting one byte of a
                // multi-byte character would leave the buffer malformed.
                const int b =
                    prev_codepoint_boundary(s.pending, s.cursor_pos);
                s.pending.erase(static_cast<size_t>(b),
                                static_cast<size_t>(s.cursor_pos - b));
                s.cursor_pos = b;
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
                // A WHOLE CODEPOINT forward — the mirror of BackSpace's.
                const int e =
                    next_codepoint_boundary(s.pending, s.cursor_pos);
                s.pending.erase(static_cast<size_t>(s.cursor_pos),
                                static_cast<size_t>(e - s.cursor_pos));
                s.red = false;
            }
        }
        touch_blink(s);
        return KeyAction::Consumed;
    }

    // Printable insertion (length-capped in BYTES). Each Kind's cap admits its
    // longest full-precision value form (see the kMaxPendingChars*
    // comments in text_editor.h).
    // Accept any character-bearing key the keyboard produced. The platform
    // resolved the effective codepoint (shift / layout / compose applied) via
    // xkb_state_key_get_utf32 — a FULL Unicode codepoint, not a byte — so a
    // compose or dead-key sequence arrives here whole and is UTF-8 encoded
    // below, landing as one insertion of one to four bytes. Characters that are
    // invalid for this field are NOT filtered here — the commit-time validator
    // rejects the value (red flash) when Enter is pressed. This replaces the
    // per-Kind keysym_to_char vocabulary entirely; the only thing Kind still
    // selects is the length cap below.
    if (!ctrl && !mods.alt && is_insertable_codepoint(mods.codepoint)) {
        const std::string ch = encode_utf8(mods.codepoint);
        int cap = kMaxPendingChars;
        if (s.kind == Kind::BpmBracket)         cap = kMaxPendingCharsBpm;
        if (s.kind == Kind::SettingsAssignment) cap = kMaxPendingCharsSettings;
        if (s.kind == Kind::CommitTitle)       cap = kMaxPendingCharsCommitTitle;
        if (s.kind == Kind::MeasureText)       cap = kMaxPendingCharsMeasure;
        if (s.kind == Kind::MeasureOffset)     cap = kMaxPendingCharsMeasureOffset;
        if (s.kind == Kind::FlagPayload && s.iter_grammar)
            cap = kMaxPendingCharsFlagIter;
        // Atomic cap: compute the result size BEFORE erasing the selection,
        // so a refusal leaves the buffer (selection included) untouched.
        // Refuse exactly when the insert would push past the cap AND grow the
        // pending; replacing a full selection with one char shrinks-then-grows
        // within the cap and is accepted. The insert's length is the ENCODED
        // byte count, so a multi-byte character is weighed as what it costs.
        const int old_size = static_cast<int>(s.pending.size());
        const int sel      = has_selection(s)
                                 ? selection_end(s) - selection_start(s)
                                 : 0;
        const int ins      = static_cast<int>(ch.size());
        const int new_size = old_size - sel + ins;
        if (new_size > cap && new_size > old_size) {
            s.red = true;
            touch_blink(s);
            return KeyAction::Consumed;
        }
        if (has_selection(s)) {
            erase_selection(s);
        }
        s.pending.insert(static_cast<size_t>(s.cursor_pos), ch);
        s.cursor_pos += ins;
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
