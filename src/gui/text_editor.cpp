#include "text_editor.h"

#include <algorithm>
#include <cmath>

namespace text_editor {

namespace {

// Vocabulary closed set, no whitespace, no pipe (pipe is in the locked
// prefix). Letters lowercase only — uppercase swallowed (the platform
// boundary already case-folds, so only the lowercase form arrives). Map
// a GuiKey (paired with mods) to the literal char to insert; returns 0
// for "not a printable in this vocabulary". `kind` selects between the
// flag-payload, iteration-bracket, and BPM-bracket alphabets.
char keysym_to_char(GuiKey key, GuiInputState mods, Kind kind,
                    bool iter_grammar) {
    const bool shift = mods.shift;

    // Digits are common to all kinds.
    if (key >= GuiKeys::Digit0 && key <= GuiKeys::Digit9 && !shift) {
        // Brief X.2: Shift+2 → '@' for BpmBracket only; the digit branch
        // already filters out shift, so the BpmBracket-specific block
        // below is what handles the shifted form.
        return static_cast<char>('0' + (key - GuiKeys::Digit0));
    }
    // Decimal point is accepted by FlagPayload, IterationBracket, and
    // SettingsAssignment but NOT BpmBracket (strict integer-only).
    if (key == GuiKeys::Period && !shift && kind != Kind::BpmBracket) return '.';

    if (kind == Kind::SettingsAssignment) {
        // Letters: lowercase bare, uppercase with Shift. Capitals are
        // accepted for every settings line so `title=` can carry a
        // human-readable value (e.g. "Symphony No. 40 (K. 550)"); keys
        // that require lowercase (N, output_format, ...) are still guarded
        // by their per-key validators in settings_io.cpp, which reject the
        // bad value at commit. The title validator accepts any non-empty,
        // newline-free string, so the full printable set below round-trips.
        if (key >= GuiKeys::A && key <= GuiKeys::Z) {
            return shift
                ? static_cast<char>('A' + (key - GuiKeys::A))
                : static_cast<char>('a' + (key - GuiKeys::A));
        }
        // Identifier / value punctuation. Settings lines have exactly one
        // '='; both halves can contain identifier-shaped characters.
        // Minus carries negative numbers; colon is needed for the
        // MM:SS.mmm shape of trim values. Underscore arrives as
        // Shift+Minus on US.
        if (key == GuiKeys::Equal     && !shift) return '=';
        if (key == GuiKeys::Minus     && !shift) return '-';
        if (key == GuiKeys::Minus     &&  shift) return '_';
        if (key == GuiKeys::Colon                ) return ':';
        if (key == GuiKeys::Semicolon &&  shift) return ':';
        // Title punctuation. Space, parentheses, comma, apostrophe,
        // ampersand — the characters a recording title needs. Period is
        // handled by the common branch at the top of this function.
        // US-layout shifted-digit forms match the convention used by the
        // other Kind branches (e.g. Shift+2 → @ in BpmBracket).
        if (key == GuiKeys::Space)                 return ' ';
        if (key == GuiKeys::Comma      && !shift) return ',';
        if (key == GuiKeys::Apostrophe && !shift) return '\'';
        if (key == GuiKeys::Digit7     &&  shift) return '&';
        if (key == GuiKeys::Digit9     &&  shift) return '(';
        if (key == GuiKeys::Digit0     &&  shift) return ')';
        return 0;
    }

    if (kind == Kind::IterationBracket) {
        // Brackets, comma, signed-number prefixes. No letters, no `*`,
        // no `:` — those would be syntactically invalid in the
        // iteration popup payload.
        if (key == GuiKeys::BracketLeft  && !shift) return '[';
        if (key == GuiKeys::BracketRight && !shift) return ']';
        if (key == GuiKeys::Comma        && !shift) return ',';
        if (key == GuiKeys::Minus        && !shift) return '-';
        if (key == GuiKeys::Plus)                   return '+';
        // US layout: Shift+= produces +.
        if (key == GuiKeys::Equal && shift)         return '+';
        return 0;
    }

    if (kind == Kind::BpmBracket) {
        // Brief X.2: digits, `@`, `,`, `[`, `]`. No letters, no signs,
        // no decimals.
        if (key == GuiKeys::BracketLeft  && !shift) return '[';
        if (key == GuiKeys::BracketRight && !shift) return ']';
        if (key == GuiKeys::Comma        && !shift) return ',';
        if (key == GuiKeys::At)                     return '@';
        // US layout: Shift+2 produces @.
        if (key == GuiKeys::Digit2 && shift)        return '@';
        return 0;
    }

    // FlagPayload kind.
    if (key >= GuiKeys::A && key <= GuiKeys::Z && !shift) {
        return static_cast<char>('a' + (key - GuiKeys::A));
    }
    if (key == GuiKeys::Asterisk)            return '*';
    if (key == GuiKeys::Colon)               return ':';
    // US layout: Shift+; gives :. Treat as colon.
    if (key == GuiKeys::Semicolon && shift)  return ':';
    // Shift+8 also produces asterisk on US layouts. Let it through.
    if (key == GuiKeys::Digit8 && shift)     return '*';
    // Brief D: iteration-mode FlagPayload editing widens the vocabulary
    // to admit the inline bracket characters. The bracket's space after
    // the comma is part of the seeded display but is not typeable (and
    // the commit-side parser tolerates its absence).
    if (iter_grammar) {
        if (key == GuiKeys::BracketLeft  && !shift) return '[';
        if (key == GuiKeys::BracketRight && !shift) return ']';
        if (key == GuiKeys::Comma        && !shift) return ',';
        if (key == GuiKeys::Minus        && !shift) return '-';
        if (key == GuiKeys::Plus)                   return '+';
        // US layout: Shift+= produces +.
        if (key == GuiKeys::Equal && shift)         return '+';
    }
    return 0;
}

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

} // namespace

int byte_index_from_click_x(double click_x, double text_left_x,
                            double advance, int pending_size) {
    const double offset = click_x - text_left_x;
    int idx = static_cast<int>(std::nearbyint(offset / advance));
    return std::clamp(idx, 0, pending_size);
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

KeyAction handle_key(State& s, GuiKey key, GuiInputState mods) {
    if (!is_active(s)) return KeyAction::NotConsumed;

    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;

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

    // Cursor motion. Shift extends a selection from an anchor; bare
    // motion collapses any existing selection to the corresponding edge.
    if (key == GuiKeys::Left) {
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
        } else if (s.cursor_pos > 0) {
            s.pending.erase(static_cast<size_t>(s.cursor_pos - 1), 1);
            s.cursor_pos--;
            s.red = false;
        }
        touch_blink(s);
        return KeyAction::Consumed;
    }
    if (key == GuiKeys::Delete) {
        if (has_selection(s)) {
            erase_selection(s);
            s.red = false;
        } else if (s.cursor_pos < static_cast<int>(s.pending.size())) {
            s.pending.erase(static_cast<size_t>(s.cursor_pos), 1);
            s.red = false;
        }
        touch_blink(s);
        return KeyAction::Consumed;
    }

    // Printable insertion (length-capped). BpmBracket gets a tighter cap
    // than the default (brief X.2): the strict format `<beats>@[<lo>,<hi>]`
    // tops out at 12 chars, so 13 leaves one char of typo slack.
    const char ch = keysym_to_char(key, mods, s.kind, s.iter_grammar);
    if (ch != 0) {
        int cap = kMaxPendingChars;
        if (s.kind == Kind::BpmBracket)        cap = kMaxPendingCharsBpm;
        if (s.kind == Kind::SettingsAssignment) cap = kMaxPendingCharsSettings;
        if (s.kind == Kind::FlagPayload && s.iter_grammar)
            cap = kMaxPendingCharsFlagIter;
        // Replace-on-type: erase before the cap check so the typed
        // char can land inside the cap when a max-length pending is
        // entirely selected.
        if (has_selection(s)) {
            erase_selection(s);
        }
        if (static_cast<int>(s.pending.size()) >= cap) {
            // Silent swallow at cap.
            return KeyAction::Consumed;
        }
        s.pending.insert(static_cast<size_t>(s.cursor_pos), 1, ch);
        s.cursor_pos++;
        s.red = false;
        touch_blink(s);
        return KeyAction::Consumed;
    }

    // Any other key while editing: swallow (the editor owns the keyboard).
    return KeyAction::Consumed;
}

bool cursor_visible_now(const State& s) {
    if (!is_active(s)) return false;
    using namespace std::chrono;
    const auto now = steady_clock::now();
    const auto ms  = duration_cast<milliseconds>(now - s.blink_epoch).count();
    // 1000ms period: visible for the first 500ms, hidden for the next 500ms.
    return ((ms / 500) % 2) == 0;
}

bool blink_period_milliseconds(int& out_ms) {
    out_ms = 500;
    return true;
}

} // namespace text_editor
