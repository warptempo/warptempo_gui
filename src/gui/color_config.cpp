#include "color_config.h"

#include "render.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

namespace {

// THE ONE AUTHORITATIVE TABLE: every configurable color, keyed by its config
// name, in the file's CANONICAL ORDER. It is the single enumeration of the
// palette anywhere in the product — the parser walks it in order, the expected
// line count is its size, and render.h's declaration order mirrors it for
// reading only. Adding a color is one row here plus its global in render.h;
// there is deliberately no second list to keep in step, and no line-count
// constant to update.
struct ColorKey {
    const char* name;
    GuiColor*   slot;
};

const ColorKey kColorKeys[] = {
    {"background",              &kBackground},
    {"canvas",                  &kCanvas},
    {"waveform_ink",            &kWaveform},
    {"text",                    &kText},
    {"text_disabled",           &kTextDisabled},
    {"line",                    &kLine},
    {"strip_anchor_stem",       &kStripAnchorStem},
    {"playhead_cursor",         &kPlayheadCursor},
    {"playhead_scanner",        &kPlayheadScanner},
    {"selected",                &kSelected},
    {"selected_outline",        &kSelectedOutline},
    {"marker",                  &kMarker},
    {"marker_outline",          &kMarkerOutline},
    {"marker_disabled",         &kMarkerDisabled},
    {"marker_disabled_outline", &kMarkerDisabledOutline},
    {"accent_red",              &kAccent},
    {"accent_red_outline",      &kAccentOutline},
    {"region_canvas",           &kRegionCanvas},
    {"overlay_canvas",          &kOverlayCanvas},
    {"overlay_outline",         &kOverlayOutline},
    {"trim_bar",                &kTrimBar},
    {"trim_bar_outline",        &kTrimBarOutline},
    {"trim_chip",               &kTrimChip},
    {"trim_chip_outline",       &kTrimChipOutline},
    {"trim_stem",               &kTrimStem},
};
constexpr size_t kColorKeyCount = std::size(kColorKeys);

// The one rejection site: a single lowercase stderr line naming the file, the
// 1-based line number and the reason. Every caller RETURNS immediately after
// it, which is what makes the "first error only, whole file dropped" rule
// structural rather than a discipline.
void reject(const std::string& path, size_t line, const std::string& reason) {
    std::fprintf(stderr,
                 "warptempo_gui: %s: line %zu: %s; keeping compiled colors\n",
                 path.c_str(), line, reason.c_str());
}

// One lowercase hex digit, or -1. Uppercase is deliberately NOT accepted: the
// file has exactly one canonical spelling per value, so `#FCFCFC` is a
// deviation like any other.
int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

} // namespace

void load_color_config() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return;
    const std::string path =
        std::string(home) + "/.config/warptempo_gui/colors.conf";

    // Binary, so the byte-exact grammar below sees the file as written (no
    // newline translation of any kind). A file that will not open is the
    // MISSING case and stays silent — the fresh-machine state.
    std::ifstream f(path, std::ios::binary);
    if (!f) return;
    std::ostringstream buf;
    buf << f.rdbuf();
    const std::string text = buf.str();

    // Parse into a SCRATCH set first. Adoption is all-or-nothing, so no global
    // is written until every line has validated — a file that goes wrong on its
    // last line leaves the palette exactly as compiled, with no half-applied
    // scheme to explain.
    GuiColor parsed[kColorKeyCount] = {};

    size_t pos = 0;
    for (size_t i = 0; i < kColorKeyCount; ++i) {
        const size_t line_no = i + 1;
        const std::string_view name(kColorKeys[i].name);

        const size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            // Covers both a truncated file and a last line written without its
            // terminator: every line ends in a newline, the final one included.
            reject(path, line_no, "missing trailing newline");
            return;
        }
        const std::string_view line(text.data() + pos, nl - pos);
        pos = nl + 1;

        // `<key> = ` exactly: single spaces around the one separator. A
        // mismatch here is every key-side deviation at once — an unknown key, a
        // duplicate, a key out of the canonical order, a missing or padded
        // separator — so the message names what was EXPECTED rather than
        // echoing the file's own bytes back (which are not ours and need not be
        // lowercase).
        const size_t head = name.size() + 3;
        if (line.size() < head ||
            line.compare(0, name.size(), name) != 0 ||
            line.compare(name.size(), 3, " = ") != 0) {
            reject(path, line_no, "expected key " + std::string(name));
            return;
        }

        // `#rrggbb` and nothing after it.
        const std::string value_error =
            "bad color value for key " + std::string(name);
        if (line.size() != head + 7 || line[head] != '#') {
            reject(path, line_no, value_error);
            return;
        }
        uint32_t rgb = 0;
        for (size_t d = 0; d < 6; ++d) {
            const int v = hex_digit(line[head + 1 + d]);
            if (v < 0) {
                reject(path, line_no, value_error);
                return;
            }
            rgb = (rgb << 4) | static_cast<uint32_t>(v);
        }
        parsed[i] = hex(rgb);
    }

    // Nothing may follow the last key's line — not a blank line, not a comment,
    // not a stray byte. The line number reported is the first line past the
    // table, which is where the offending content begins.
    if (pos != text.size()) {
        reject(path, kColorKeyCount + 1, "extra content after the last key");
        return;
    }

    for (size_t i = 0; i < kColorKeyCount; ++i)
        *kColorKeys[i].slot = parsed[i];
}
