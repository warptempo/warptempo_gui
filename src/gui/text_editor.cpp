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

} // namespace

int byte_index_from_click_x(double click_x, double text_left_x,
                            double advance, int pending_size) {
    const double offset = click_x - text_left_x;
    int idx = static_cast<int>(std::nearbyint(offset / advance));
    return std::clamp(idx, 0, pending_size);
}

std::string selected_text(const State& s) {
    if (!has_selection(s)) return std::string();
    const int a = selection_start(s);
    const int b = selection_end(s);
    return s.pending.substr(static_cast<size_t>(a),
                            static_cast<size_t>(b - a));
}

void replace_selection(State& s, const std::string& raw) {
    if (has_selection(s)) erase_selection(s);
    std::string clean;
    clean.reserve(raw.size());
    for (unsigned char c : raw) {
        if (c >= 0x20 && c <= 0x7e) clean.push_back(static_cast<char>(c));
    }
    int cap = kMaxPendingChars;
    if (s.kind == Kind::BpmBracket)         cap = kMaxPendingCharsBpm;
    if (s.kind == Kind::SettingsAssignment) cap = kMaxPendingCharsSettings;
    if (s.kind == Kind::FlagPayload && s.iter_grammar)
        cap = kMaxPendingCharsFlagIter;
    const int room = cap - static_cast<int>(s.pending.size());
    if (room > 0 && !clean.empty()) {
        if (static_cast<int>(clean.size()) > room)
            clean.resize(static_cast<size_t>(room));
        s.pending.insert(static_cast<size_t>(s.cursor_pos), clean);
        s.cursor_pos += static_cast<int>(clean.size());
    }
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

    // Ctrl+C / Ctrl+X / Ctrl+V: clipboard. These sit in the editor's owned
    // keymap (returning a handled action), so they never fall through to the
    // global dispatch; Ctrl+C/X/V are unbound globally, so no conflict. Copy
    // and cut are a no-op (plain Consumed) without a selection; paste always
    // requests — the input handler performs the platform I/O.
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
        } else if (ctrl) {
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
        touch_blink(s);
        return KeyAction::Consumed;
    }
    if (key == GuiKeys::Delete) {
        if (has_selection(s)) {
            erase_selection(s);
            s.red = false;
        } else if (ctrl) {
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
        touch_blink(s);
        return KeyAction::Consumed;
    }

    // Printable insertion (length-capped). BpmBracket gets a tighter cap
    // than the default (brief X.2): the strict format `<beats>@[<lo>,<hi>]`
    // tops out at 12 chars, so 13 leaves one char of typo slack.
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

    // The editor does not own this key. Report NotConsumed so the caller can
    // route it (Brief 2: cancel the edit and let the global command run).
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

bool blink_period_milliseconds(int& out_ms) {
    out_ms = 500;
    return true;
}

} // namespace text_editor
