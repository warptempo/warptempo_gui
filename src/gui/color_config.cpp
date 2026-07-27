#include "color_config.h"

#include "render.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
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
    {"selected_stem",           &kSelectedStem},
    {"marker",                  &kMarker},
    {"marker_outline",          &kMarkerOutline},
    {"marker_disabled",         &kMarkerDisabled},
    {"marker_disabled_outline", &kMarkerDisabledOutline},
    {"accent_red",              &kAccent},
    {"accent_red_outline",      &kAccentOutline},
    {"region_canvas",           &kRegionCanvas},
    {"overlay_outline",         &kOverlayOutline},
    {"trim_bar",                &kTrimBar},
    {"trim_bar_outline",        &kTrimBarOutline},
    {"trim_chip",               &kTrimChip},
    {"trim_chip_outline",       &kTrimChipOutline},
    {"trim_stem",               &kTrimStem},
};
constexpr size_t kColorKeyCount = std::size(kColorKeys);

// The two rejection sites, one per fault class. Both print a single lowercase
// stderr line and every caller RETURNS immediately after, which is what makes
// the "first error only, whole file dropped" rule structural rather than a
// discipline.
//
// reject_line is the GRAMMAR fault: the file was read, and one 1-based line of
// it deviates. reject_file is the FILE fault: the bytes never arrived, so there
// is no line to name. Only genuine ABSENCE is silent (see load_color_config) —
// a file that exists but cannot be delivered is a fault the architect must see,
// since the alternative is a retuned scheme silently not taking effect.
void reject_line(const std::string& path, size_t line,
                 const std::string& reason) {
    std::fprintf(stderr,
                 "warptempo_gui: %s: line %zu: %s; keeping compiled colors\n",
                 path.c_str(), line, reason.c_str());
}

void reject_file(const std::string& path, const char* reason) {
    std::fprintf(stderr, "warptempo_gui: %s: %s; keeping compiled colors\n",
                 path.c_str(), reason);
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
    // newline translation of any kind).
    //
    // ABSENCE IS THE ONLY SILENT FAILURE. No file at all is the fresh-machine
    // state — the normal way to run on compiled defaults — so ENOENT returns
    // without a word. Every OTHER open failure (a directory in the way, a
    // permission bit, an i/o error) means a config the architect wrote is not
    // being applied, which must be said out loud. errno is cleared first so the
    // test reads THIS open's result rather than some earlier call's residue.
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (errno != ENOENT) reject_file(path, "cannot open");
        return;
    }

    // THE READ MUST HAVE COMPLETED before a single line is parsed: a truncated
    // delivery whose prefix happens to be a run of valid lines would otherwise
    // be adopted as if it were the whole file. Chunked istream::read rather than
    // a streambuf slurp precisely so a failure lands in THIS stream's state —
    // an extraction straight from f.rdbuf() bypasses f and leaves it reading
    // "good" after a short read. The loop appends each full chunk; the read that
    // ends it (eof or error) leaves its tail count in gcount(), which the final
    // append takes.
    std::string text;
    {
        char chunk[4096];
        while (f.read(chunk, sizeof chunk))
            text.append(chunk, static_cast<size_t>(f.gcount()));
        text.append(chunk, static_cast<size_t>(f.gcount()));
    }
    // badbit is the i/o failure; ending at eof (eofbit + failbit, no badbit) is
    // the normal exit. An EMPTY file reads cleanly to zero bytes and falls
    // through to the parser, where it is a grammar fault — its first key's line
    // is simply missing.
    if (f.bad()) {
        reject_file(path, "cannot read");
        return;
    }

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
            // Two different faults share this arm, and naming them apart is the
            // difference between a useful message and a misleading one. Nothing
            // left at all (an empty file included) means the key's LINE IS
            // ABSENT — the file simply ran out before it. Bytes left but no
            // newline among them means the line is there and merely
            // UNTERMINATED. Every line ends in a newline, the final one
            // included, so both are fatal.
            if (pos == text.size())
                reject_line(path, line_no,
                            "missing line for key " + std::string(name));
            else
                reject_line(path, line_no, "missing trailing newline");
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
            reject_line(path, line_no,
                        "expected key " + std::string(name));
            return;
        }

        // `#rrggbb` and nothing after it.
        const std::string value_error =
            "bad color value for key " + std::string(name);
        if (line.size() != head + 7 || line[head] != '#') {
            reject_line(path, line_no, value_error);
            return;
        }
        uint32_t rgb = 0;
        for (size_t d = 0; d < 6; ++d) {
            const int v = hex_digit(line[head + 1 + d]);
            if (v < 0) {
                reject_line(path, line_no, value_error);
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
        reject_line(path, kColorKeyCount + 1,
                    "extra content after the last key");
        return;
    }

    for (size_t i = 0; i < kColorKeyCount; ++i)
        *kColorKeys[i].slot = parsed[i];
}
