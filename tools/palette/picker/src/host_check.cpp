// tools/palette/picker — THE LAPTOP CHECK (no device): the picker's portable core driven on the host, with the
// laptop's cairo and FreeType, against the mock tool's own bytes. Built and run by build_picker.sh --check (never
// part of the APK):
//
//   host_check <fonts dir> <work dir> --export <scene dir> <expects.txt> [--export <dir> <expects.txt>]...
//              [--linmix <table>] [--today <dir>] [--late <dir>] [--models <models_ref.txt>] [--presets <presets.json>]
//              [--kept <dir>]
//
//   - every export loads, and every expects.txt line ("<scene> <ppm> [<key>=#RRGGBB ...]": the mock tool's render of
//     that scene with those elements moved from the manifest's colours) equals the picker's picture byte for byte,
//     both painted whole and reached by the live repaint from the manifest's colours (the road a pen drag takes);
//   - --linmix: a 65536-byte table from colour.py, lin_mix(a, b, 0.5) for every byte pair: the C++ port must agree;
//   - the blend (colour.h over_n_8) equals cairo's own compositing of a solid source through an A8 mask on every
//     (source, frame, coverage) byte triple, each channel; the chrome rule's scale_byte is half-to-even and capped;
//   - ColourState over the whole cube, the view numbers, the readouts (one decimal; the widest fits its field);
//   - THE MODELS: bytes -> HSL / LCh -> bytes the identity over the whole cube, every LCh inside the gamut, the C
//     track's range holding P3's most chromatic colour, white and black, the retention; --models: check_refs.py's
//     independent reference (HSL by colorsys, LCh by numpy over Display-P3) to 1e-9;
//   - the scripted sessions on the first export (check_refs.py's check theme: Chrome, Canvas and Ink over the scene
//     waveform, the Outline over the scene magnified (architect 2026-10-05), then the flag kinds (architect 2026-10-05) -- Warp Flag and Selected Warp Flag over the scene flags,
//     Phase Reset Flag and Selected Phase Reset Flag over the scene phase_reset, Added Flag, Selected Added Flag, Removed
//     Flag and Selected Removed Flag over the scene history -- Playhead Head and Playhead Stem over the scene playhead,
//     the Label over the scene label, Selection and Selected Text over the scene open_flag, then test elements filling
//     the chooser to nineteen entries, the last over the waveform scene) and its active element (the chrome): the
//     panel, the pick history, the HSV he dialled; then THE CHOOSER (open, its fit -- two columns of ten and nine
//     entries inside the panel and the screen, painted over the wheel and the slider rows it covers, the same picture
//     at two colours, the second column's empty cell holding nothing; the extents at sixteen and twenty-two printed --
//     a tap outside it, the no-op re-choice, the edited-then-switch commit, the switches of scene Ink -> Warp Flag ->
//     Selected Warp Flag -> Selection -> the last entry, at the second column's foot), every element's history, cursor
//     and view independent across a switch and a relaunch, and today's picks.txt (58 ink lines, the old form and the
//     new) and state.json loading unchanged; THE PLAYHEAD (architect 2026-10-04: the panel on the right, the head and
//     the stem each repainting alone, the head's outline the Label's, each committed by the switch or the close and
//     kept by a relaunch); THE LABEL (architect 2026-10-04: the chrome's text colour over the scene label, its role the
//     element, the flag labels fixed; its words repainting and their antialiased edges re-blending, committed by the
//     close, kept by a relaunch); THE OPEN FLAG (architect 2026-10-04: the selected pair over the scene open_flag, the
//     panel on the left, the band and the glyphs each repainting alone with the antialiased edges re-blending, the face
//     and the editor's black frame staying, each committed by the switch or the close and kept by a relaunch); THE
//     FLAG KINDS (architect 2026-10-05: the phase-reset pair over the scene phase_reset and the added and the removed
//     pairs over the scene history, the panel on the left, each face repainting with its stem where it has one, the
//     fixed labels' antialiased edges re-blending over it, every other face staying, each committed by the switch or
//     the close and kept by a relaunch; the names' fit, measured in the frame: each set clear of the button's head,
//     every chooser entry inside its cell); THE OUTLINE (architect 2026-10-05: an element starting at the old rule's
//     colour, repainting alone over the lit plate, independent of the ink and the canvas, committed and kept); COPY
//     AND PASTE (architect 2026-10-05: PASTE greyed before a copy, measured in the frame; a copy's colour and view
//     pasted onto another element as an edit, OLD reverting, the close committing one pick; both panel sides; nothing
//     copied after a relaunch); THE RENAMED KEYS (the flag elements' keys before 2026-10-05 read as the
//     elements that replaced them: colours, views, entries, histories in file order, the active element and a preset;
//     the files rewritten with the new keys alone);
//   - THE SWITCH'S PICTURE on the round's own export (the one labelled "scene"): launched with every element at the
//     colours of its "all moved" reference, the waveform scene and, after the chooser's switch Ink -> Warp Flag, the
//     flags scene equal the mock tool's renders byte for byte, the switch back the waveform's again, and the switch
//     Ink -> Playhead Head the playhead scene's, then the switch on to the Label the label scene's, on to the Selection
//     the open_flag scene's, on to the Phase Reset Flag the phase_reset scene's, on to the Added Flag the history
//     scene's and on to the Outline the magnified scene's;
//   - THE PRESETS (two saves, the edited colour committed by the pop-up's opening; a load committing each changed
//     element once and an unchanged one never, BACK returning; a relaunch; a scroll and a tap outside that act on
//     nothing; a preset saved before the flags round, three elements, loading and leaving every other element as it
//     is) and
//     THE THEME STRIP (opened from the pop-up, swatches adopted as edits into the chrome and, after a
//     switch, the canvas, OLD reverting, the strip surviving the switch and a relaunch, Windows 95 Standard's quartet
//     and a tinted theme's rule lines, its own scroll, the close control) on the first export;
//   - THE MODEL SWITCH on the first export: the list's tap rules, HSL and LCh dialled, committed and restored exactly
//     (relaunch, BACK / FORWARD, OLD, the leave, a preset over three models), the switch leaving views untouched, the
//     ring and the triangle in both, the gamut stop on a C drag and an h drag, the out-of-gamut stretch's neutral, the
//     new forms' refusals;
//   - --today <dir>: the tablet's picks.txt and state.json of 2026-10-04 (morning) load unchanged; --late <dir>: those
//     of the presets build's install (136 lines) too, every element but the first three at the manifest's colours with
//     empty histories, HSV shown; --presets: the repository's presets.json reads; --kept <dir>:
//     the repository's copy of the tablet's files (picks.txt, state.json, presets.json) loads over the round's own
//     export, every element they name at state.json's colour -- an old flag key's colour on each element that replaced
//     it, its picks.txt lines that element's history, read from the files' own text -- every element they never name
//     at the manifest's with an empty history, and each preset loaded sets the elements it names (a commit for each
//     whose colour or view changes, none for the rest) and leaves the elements it does not name as they were, with no
//     pick;
//   - THE PER-FRAME COST of a pen drag (R's track) on the ink, the chrome and the warp flag, and of LCh's h track
//     on the chrome: the live repaint and the frame;
//   - frames as PNGs in the work dir for the eye.

#include "colour.h"
#include "fonts.h"
#include "json.h"
#include "picker.h"
#include "scene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

void plog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::fprintf(stderr, "[warptempo_picker] ");
    std::vfprintf(stderr, fmt, ap);
    std::fprintf(stderr, "\n");
    va_end(ap);
}

namespace {

int g_fail = 0;
std::string g_theme;   // state_load's theme, where a check does not read it
Model g_model;         // and its model
void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void put(const std::string& path, const std::string& text) {
    FILE* f = std::fopen(path.c_str(), "w");
    std::fputs(text.c_str(), f);
    std::fclose(f);
}

// the picture's words against a binary P6 (the export's header shape): the pixels that differ, or -1
long diff_ppm(const std::vector<uint32_t>& pic, const std::string& ppm_path, int w, int h) {
    const std::string d = slurp(ppm_path);
    const std::string hdr = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    if (d.compare(0, hdr.size(), hdr) != 0 || d.size() != hdr.size() + size_t(w) * h * 3) return -1;
    const auto* p = reinterpret_cast<const uint8_t*>(d.data() + hdr.size());
    long n = 0;
    for (size_t k = 0; k < size_t(w) * h; ++k)
        if (pic[k] != word_of(Rgb{p[3 * k], p[3 * k + 1], p[3 * k + 2]})) ++n;
    return n;
}

size_t occurrences(const std::string& text, const std::string& what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
    return n;
}

size_t lines_of(const std::string& path) {
    const std::string t = slurp(path);
    return size_t(std::count(t.begin(), t.end(), '\n'));
}

void png(cairo_surface_t* surf, const std::string& path) {
    cairo_surface_write_to_png(surf, path.c_str());
    std::printf("     wrote %s\n", path.c_str());
}

int scene_of(const Export& ex, const std::string& name) {
    for (size_t k = 0; k < ex.scenes.size(); ++k)
        if (ex.scenes[k].name == name) return int(k);
    return -1;
}

// "key=#hex" overrides -> the export's elements moved; false on a bad token
bool apply_overrides(Export& ex, const std::vector<std::string>& toks, uint32_t& changed) {
    changed = 0;
    for (const std::string& t : toks) {
        const size_t eq = t.find('=');
        if (eq == std::string::npos) return false;
        const int e = element_of(ex, t.substr(0, eq));
        Rgb c;
        if (e < 0 || !parse_hex(t.substr(eq + 1), c)) return false;
        ex.elements[size_t(e)].colour = c;
        changed |= 1u << e;
    }
    return true;
}

// every expects.txt line: the picture painted whole and by the live repaint, each against the ppm
void check_expects(const Export& ex0, const std::string& path, const std::string& label) {
    std::istringstream in(slurp(path));
    std::string line;
    int lines = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string scene, ppm, t;
        ls >> scene >> ppm;
        std::vector<std::string> toks;
        while (ls >> t) toks.push_back(t);
        const int sc = scene_of(ex0, scene);
        Export ex = ex0;
        uint32_t changed = 0;
        if (sc < 0 || !apply_overrides(ex, toks, changed)) { check(false, label + ": a bad expects line: " + line); continue; }
        std::vector<uint32_t> words, w0, pic(size_t(ex.width) * ex.height), live(pic.size());
        role_words(ex, words);
        scene_paint(ex, sc, words, pic.data());
        role_words(ex0, w0);                                  // the live road: from the manifest's picture
        scene_paint(ex0, sc, w0, live.data());
        scene_repaint(ex, sc, w0, words, changed, live.data());
        const long a = diff_ppm(pic, ppm, ex.width, ex.height), b = diff_ppm(live, ppm, ex.width, ex.height);
        std::string what = label + " " + scene;
        for (const std::string& k : toks) what += " " + k;
        if (toks.empty()) what += " (the manifest's colours)";
        check(a == 0 && b == 0, what + " equals the mock tool's render (" + std::to_string(a) + " px differ painted whole, " +
                                    std::to_string(b) + " by the live repaint)");
        ++lines;
    }
    check(lines > 0, label + ": " + std::to_string(lines) + " references read");
}

// a solid pixel of element e's own role in the scene `sc`, right of min_x: its index, or -1
long element_pixel(const Export& ex, int sc, int e, int min_x) {
    for (size_t r = 0; r < ex.roles.size(); ++r) {
        const Role& R = ex.roles[r];
        if (R.kind != Role::Kind::Element || R.el != e) continue;
        for (const Run& run : ex.scenes[size_t(sc)].solid[r])
            for (uint32_t k = run.start; k < run.start + run.len; ++k)
                if (int(k % ex.width) > min_x) return long(k);
    }
    return -1;
}

uint32_t frame_word(cairo_surface_t* frame, long k, int width) {
    cairo_surface_flush(frame);
    const auto* w = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(frame));
    const int st = cairo_image_surface_get_stride(frame) / 4;
    return w[size_t(k / width) * st + k % width];
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // the check's lines in order with plog's stderr
    if (argc < 6) {
        std::fprintf(stderr, "usage: host_check <fonts dir> <work dir> --export <scene dir> <expects.txt> [--export ...] [--linmix <table>]\n");
        return 2;
    }
    const std::string fonts = argv[1], work = argv[2];
    const std::string sans = slurp(fonts + "/Roboto-Regular.ttf");
    if (!fonts_install(reinterpret_cast<const uint8_t*>(sans.data()), sans.size())) {
        std::fprintf(stderr, "fonts did not install\n");
        return 2;
    }
    std::string err;
    Export ex0;                                   // the first export: the sessions' and the timing's
    std::string today;                            // --today: the tablet's picks.txt and state.json of 2026-10-04
    std::string late;                             // --late: those of the presets build's install, the same day
    std::string models_ref;                       // --models: check_refs.py's models_ref.txt (HSL by colorsys, LCh by numpy)
    std::string repo_presets;                     // --presets: the repository's copy of the tablet's presets.json
    std::string kept;                             // --kept: the repository's copy of the tablet's files (presets/)
    Export round_ex;                              // the export labelled "scene" (the round's, as the tablet gets it)
    std::string round_expects;
    bool have = false;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--export" && i + 2 < argc) {
            const std::string dir = argv[++i], expects = argv[++i];
            Export ex;
            if (!export_load(dir, ex, err)) { check(false, dir + ": " + err); continue; }
            size_t stacks = 0, layers = 0, runs = 0;
            for (const Scene& sc : ex.scenes) {
                stacks += sc.stacks.size();
                layers += sc.layers.size();
                for (const auto& r : sc.solid) runs += r.size();
            }
            std::printf("export %s: %dx%d, %zu elements, %zu roles, %zu scenes, %zu antialiased px (%zu paints), %zu solid runs\n",
                        dir.c_str(), ex.width, ex.height, ex.elements.size(), ex.roles.size(), ex.scenes.size(), stacks,
                        layers, runs);
            const std::string label = dir.substr(dir.find_last_of('/') + 1);
            check_expects(ex, expects, label);
            if (label == "scene") { round_ex = ex; round_expects = expects; }
            if (!have) { ex0 = std::move(ex); have = true; }
        } else if (a == "--today" && i + 1 < argc) {
            today = argv[++i];
        } else if (a == "--models" && i + 1 < argc) {
            models_ref = argv[++i];
        } else if (a == "--presets" && i + 1 < argc) {
            repo_presets = argv[++i];
        } else if (a == "--late" && i + 1 < argc) {
            late = argv[++i];
        } else if (a == "--kept" && i + 1 < argc) {
            kept = argv[++i];
        } else if (a == "--linmix" && i + 1 < argc) {
            const std::string t = slurp(argv[++i]);
            size_t bad = 0;
            if (t.size() != 65536) bad = 65536;
            else
                for (int x = 0; x < 256; ++x)
                    for (int y = 0; y < 256; ++y)
                        if (lin_mix_byte(x, y, 0.5) != uint8_t(t[size_t(x) * 256 + y])) ++bad;
            check(bad == 0, "lin_mix(a, b, 0.5) equals colour.py on all 65536 byte pairs (" + std::to_string(bad) + " differ)");
        } else {
            std::fprintf(stderr, "host_check: unknown argument %s\n", a.c_str());
            return 2;
        }
    }
    if (!have) return 1;
    const Export& ex = ex0;

    // ---------------------------------------------------------------- the blend against cairo, every byte triple
    {
        // for each source byte s: a 256 x 256 frame whose row d holds the frame byte d, through an A8 mask whose
        // column m holds the coverage m; the channels run through bijections of s and d, so each sees every triple
        cairo_surface_t* dst = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 256, 256);
        cairo_surface_t* msk = cairo_image_surface_create(CAIRO_FORMAT_A8, 256, 256);
        {
            cairo_surface_flush(msk);
            uint8_t* m = cairo_image_surface_get_data(msk);
            const int ms = cairo_image_surface_get_stride(msk);
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x) m[y * ms + x] = uint8_t(x);
            cairo_surface_mark_dirty(msk);
        }
        auto drow = [](int d) { return Rgb{uint8_t(d), uint8_t((d * 5 + 1) & 255), uint8_t(255 - d)}; };
        long bad = 0;
        for (int s = 0; s < 256; ++s) {
            const Rgb src{uint8_t(s), uint8_t(255 - s), uint8_t((s * 7 + 3) & 255)};
            cairo_surface_flush(dst);
            auto* px = reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(dst));
            const int st = cairo_image_surface_get_stride(dst) / 4;
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x) px[y * st + x] = word_of(drow(y));
            cairo_surface_mark_dirty(dst);
            cairo_t* cr = cairo_create(dst);
            cairo_set_source_rgb(cr, src.r / 255.0, src.g / 255.0, src.b / 255.0);
            cairo_mask_surface(cr, msk, 0, 0);
            cairo_destroy(cr);
            cairo_surface_flush(dst);
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x)
                    if ((px[y * st + x] & 0xFFFFFF) != (over_n_8(word_of(src), uint8_t(x), word_of(drow(y))) & 0xFFFFFF)) ++bad;
        }
        cairo_surface_destroy(dst);
        cairo_surface_destroy(msk);
        check(bad == 0, "the blend (over_n_8) equals cairo's solid source through an A8 mask on every (source, frame, "
                        "coverage) byte triple, each channel (" + std::to_string(bad) + " of 16777216 px differ)");
        bool scale = true;
        for (int c = 0; c < 256; ++c)
            for (int num : {255, 223, 128}) {
                const double q = double(c) * num / 192;
                scale = scale && scale_byte(c, num, 192) == uint8_t(std::min(255.0, std::nearbyint(q)));
            }
        check(scale && scale_byte(32, 255, 192) == 42 && scale_byte(96, 223, 192) == 112 &&
                  scale_rgb(Rgb{0x19, 0x19, 0x19}, 255, 192) == (Rgb{0x21, 0x21, 0x21}) &&
                  scale_rgb(Rgb{0x19, 0x19, 0x19}, 223, 192) == (Rgb{0x1D, 0x1D, 0x1D}) &&
                  scale_rgb(Rgb{0x19, 0x19, 0x19}, 128, 192) == (Rgb{0x11, 0x11, 0x11}) &&
                  scale_rgb(Rgb{0xE6, 0xD2, 0xB4}, 255, 192) == (Rgb{0xFF, 0xFF, 0xEF}),
              "the chrome rule's scale_byte: half to even (32 x 255 / 192 -> 42, 96 x 223 / 192 -> 112), capped, #191919 -> "
              "#212121 / #1D1D1D / #111111");
    }

    // ---------------------------------------------------------------- ColourState: the bytes are the truth
    {
        ColourState cs;
        cs.set_rgb(Rgb{0x80, 0x40, 0x20});
        const double h0 = cs.h;
        cs.set_hsv(cs.h, 0, cs.v);
        check(cs.rgb.r == cs.rgb.g && cs.rgb.g == cs.rgb.b && cs.h == h0, "S to 0 gives a grey and keeps the hue");
        cs.set_hsv(cs.h, 0.75, cs.v);
        check(cs.h == h0 && cs.rgb.r > cs.rgb.b, "S back up restores the hue's colour");
        const double s0 = cs.s;
        cs.set_hsv(cs.h, cs.s, 0);
        check(cs.rgb == (Rgb{0, 0, 0}), "V to 0 is black");
        cs.set_rgb(cs.rgb);
        check(cs.h == h0 && cs.s == s0, "black set as bytes keeps the hue and the saturation");
        cs.set_rgb(Rgb{0x77, 0x77, 0x77});
        check(cs.h == h0 && cs.s == 0, "a grey set as bytes keeps the hue, saturation 0");
        // THE WHOLE CUBE, every model: bytes -> view -> bytes is the identity, the LCh inside the gamut (its bytes reached
        // by rounding alone), every re-derived view in its ranges
        long hsv_bad = 0, hsl_bad = 0, lch_bad = 0, lch_out = 0, range_bad = 0;
        double cmax = 0;
        Rgb cmax_at;
        for (int r = 0; r < 256; ++r)
            for (int g = 0; g < 256; ++g)
                for (int b = 0; b < 256; ++b) {
                    const Rgb c{uint8_t(r), uint8_t(g), uint8_t(b)};
                    ColourState t;
                    t.set_rgb(c);
                    if (!(rgb_of_hsv(t.h, t.s, t.v) == c)) ++hsv_bad;
                    if (!(rgb_of_hsl(t.hsl[0], t.hsl[1], t.hsl[2]) == c)) ++hsl_bad;
                    const Unit u = unit_of_lch(t.lch[0], t.lch[1], t.lch[2]);
                    if (!unit_in_gamut(u)) ++lch_out;
                    if (!(rgb_of_unit(u) == c)) ++lch_bad;
                    for (Model m : kAllModels)
                        for (int k = 0; k < 3; ++k) {
                            const double x = t.view(m)[size_t(k)];
                            if (!(x >= 0 && x <= axis_max(m, k))) ++range_bad;
                        }
                    if (t.lch[1] > cmax) { cmax = t.lch[1]; cmax_at = c; }
                }
        check(hsv_bad == 0, "bytes -> HSV -> bytes is the identity (the whole cube)");
        check(hsl_bad == 0, "bytes -> HSL -> bytes is the identity (the whole cube, " + std::to_string(hsl_bad) + " differ)");
        check(lch_bad == 0 && lch_out == 0 && range_bad == 0,
              "bytes -> LCh -> bytes is the identity and every view inside the gamut and its ranges (the whole cube; " +
                  std::to_string(lch_bad) + " differ, " + std::to_string(lch_out) + " outside the gamut, " +
                  std::to_string(range_bad) + " numbers outside their ranges)");
        char cm[200];
        std::snprintf(cm, sizeof cm, "the most chromatic byte triple is %s at C %.4f, inside the C track's 0..%.0f",
                      hex_of(cmax_at).c_str(), cmax, kChromaMax);
        check(cmax <= kChromaMax && cmax_at == (Rgb{0, 255, 0}), cm);
        bool trip = view_number(0.35) == "0.35" && view_number(227) == "227" && view_number(0) == "0";
        for (int k = 0; k <= 100000 && trip; ++k) {
            const double xs[3] = {k / 100000.0, k / 7.0 / 100000.0 * 360, std::nearbyint(k % 101) / 100};
            for (double x : xs) trip = trip && std::strtod(view_number(x).c_str(), nullptr) == x;
        }
        check(trip, "a stored view number reads back as the same double, in the shortest form (0.35, 227)");
        ColourState f;
        f.set_hsv(247.27, 0.4714, 0.99999);
        const bool tenths = row_readout(f, 0) == "247.3" && row_readout(f, 1) == "47.1" && row_readout(f, 2) == "100.0" &&
                            row_readout(f, 3) == std::to_string(f.rgb.r) && row_readout(f, 5) == std::to_string(f.rgb.b);
        f.set_hsv(360, 0, 1);
        const bool ends = row_readout(f, 0) == "360.0" && row_readout(f, 1) == "0.0" && row_readout(f, 2) == "100.0";
        f.set_hsv(0.04, 0.00051, 0.00049);
        const bool low = row_readout(f, 0) == "0.0" && row_readout(f, 1) == "0.1" && row_readout(f, 2) == "0.0";
        check(tenths && ends && low, "the H / S / V readouts show one decimal, rounded to nearest (247.27 -> 247.3, "
                                     "0.4714 -> 47.1, 0.99999 -> 100.0), R / G / B whole bytes");
        cairo_surface_t* ms = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 4, 4);
        cairo_t* mc = cairo_create(ms);
        fonts_select(mc, panel::kNumPx);
        double widest = 0;
        for (const char* t : {"360.0", "100.0", "255"}) {
            cairo_text_extents_t e;
            cairo_text_extents(mc, t, &e);
            widest = std::max(widest, e.x_advance);
        }
        cairo_destroy(mc);
        cairo_surface_destroy(ms);
        char fit[160];
        std::snprintf(fit, sizeof fit, "the widest readout (%.1f px) fits its field (%d px) with %.1f px of air left of it, "
                      "%d right", widest, panel::kFieldW, panel::kFieldW - panel::kReadoutInset - widest, panel::kReadoutInset);
        check(widest + 2 * panel::kReadoutInset <= panel::kFieldW, fit);
    }

    // ---------------------------------------------------------------- THE MODELS: HSL and LCh over the bytes
    {
        double w[3], k[3];
        lch_of_unit(Unit{1, 1, 1}, w);
        lch_of_unit(Unit{0, 0, 0}, k);
        char wk[200];
        std::snprintf(wk, sizeof wk, "white #FFFFFF is L %.12f C %g, black L %g C %g", w[0], w[1], k[0], k[1]);
        check(std::abs(w[0] - 100) < 1e-9 && w[1] == 0 && k[0] == 0 && k[1] == 0, wk);
        // the retention: HSL keeps its hue through grey and its saturation through black and white; LCh its hue
        ColourState g;
        g.show(Model::Hsl);
        g.set_rgb(Rgb{0x80, 0x40, 0x20});
        const double gh = g.hsl[0], gs = g.hsl[1], lh = g.lch[2];
        g.set_rgb(Rgb{0x77, 0x77, 0x77});
        const bool grey_ok = g.hsl[0] == gh && g.hsl[1] == 0 && g.lch[2] == lh && g.lch[1] == 0;
        g.set_rgb(Rgb{0x80, 0x40, 0x20});
        g.set_rgb(Rgb{0, 0, 0});
        const bool black_ok = g.hsl[0] == gh && g.hsl[1] == gs && g.hsl[2] == 0;
        g.set_rgb(Rgb{0x80, 0x40, 0x20});
        g.set_rgb(Rgb{255, 255, 255});
        check(grey_ok && black_ok && g.hsl[0] == gh && g.hsl[1] == gs && g.hsl[2] == 1 && g.lch[2] == lh,
              "HSL keeps its hue through grey and its saturation through black and white; LCh its hue through C 0");
        // against the independent reference: HSL by colorsys, LCh by numpy
        if (!models_ref.empty()) {
            std::istringstream in(slurp(models_ref));
            long n = 0, bad_hsl = 0, bad_lch = 0;
            double worst_hsl = 0, worst_lch = 0;
            std::map<std::string, std::string> named;
            int r, g2, b;
            double H, S, Lh, L, C, h;
            while (in >> r >> g2 >> b >> H >> S >> Lh >> L >> C >> h) {
                ++n;
                const Rgb c{uint8_t(r), uint8_t(g2), uint8_t(b)};
                ColourState t;
                t.set_rgb(c);
                const bool grey = r == g2 && g2 == b;
                double dh = grey ? 0 : std::abs(std::remainder(t.hsl[0] - H, 360.0));
                double e1 = std::max({dh, std::abs(t.hsl[1] - S), std::abs(t.hsl[2] - Lh)});
                double dl = std::abs(std::remainder(t.lch[2] - h, 360.0));
                double e2 = std::max({std::abs(t.lch[0] - L), std::abs(t.lch[1] - C), C > 1e-6 ? dl : 0.0});
                worst_hsl = std::max(worst_hsl, e1);
                worst_lch = std::max(worst_lch, e2);
                if (e1 > 1e-9) ++bad_hsl;
                if (e2 > 1e-9) ++bad_lch;
                char line[160];
                std::snprintf(line, sizeof line, "L %.4f C %.4f h %.4f (the reference: %.4f %.4f %.4f)", t.lch[0], t.lch[1],
                              t.lch[2], L, C, h);
                named[hex_of(c)] = line;
            }
            char what[240];
            std::snprintf(what, sizeof what, "HSL equals Python's colorsys and LCh numpy's (Display-P3, D65) on %ld byte "
                          "triples to 1e-9 (worst %.2g and %.2g)", n, worst_hsl, worst_lch);
            check(n > 20000 && bad_hsl == 0 && bad_lch == 0, what);
            for (const char* hx : {"#FF0000", "#00FF00", "#0000FF", "#00FFFF", "#FF00FF", "#FFFF00", "#FFFFFF"})
                std::printf("     P3 %s: %s\n", hx, named[hx].c_str());
        }
    }

    using namespace panel;
    const int W = ex.width;
    const std::string L = ex.elements[size_t(ex.active)].key;   // its key
    const int act0 = ex.active;   // the manifest's active element (the chrome): the panel's, history's and HSV sessions'
    const int ink = element_of(ex, "ink"), chrome = element_of(ex, "chrome"), canvas = element_of(ex, "canvas"),
              wflag = element_of(ex, "warp_flag"), wsel = element_of(ex, "warp_flag_selected"),
              pflag = element_of(ex, "phase_reset_flag"), psel = element_of(ex, "phase_reset_flag_selected"),
              aflag = element_of(ex, "added_flag"), asel = element_of(ex, "added_flag_selected"),
              rflag = element_of(ex, "removed_flag"), rsel = element_of(ex, "removed_flag_selected"),
              phead = element_of(ex, "playhead_head"), pstem = element_of(ex, "playhead_stem"),
              lbl = element_of(ex, "label"), selfill = element_of(ex, "selected_fill"),
              seltext = element_of(ex, "selected_text"), outl = element_of(ex, "waveform_outline");
    const int n_el = int(ex.elements.size());
    const int last = n_el - 1;   // the chooser's last entry (a check test element, at the second column's foot)
    auto launch = [&]() {
        Export e = ex;
        Launch l;
        if (!picker_load(work, e, l, err)) { std::printf("FAIL picker_load: %s\n", err.c_str()); ++g_fail; }
        return Picker(std::move(e), work, std::move(l));
    };
    auto load_fails = [&](std::string& why) {
        Export e = ex;
        Launch l;
        return !picker_load(work, e, l, why);
    };
    auto tap = [](Picker& q, double x, double y) { q.press(x, y); q.release(x, y); };
    auto count = [](const Picker& q) {
        return std::to_string(q.history().cursor + 1) + " of " + std::to_string(q.history().picks.size());
    };
    auto fresh = [&]() {
        std::remove((work + "/picks.txt").c_str());
        std::remove((work + "/state.json").c_str());
        std::remove((work + "/presets.json").c_str());
    };
    // the panel opens on the left (a tap at x 1700): its controls in window px
    const double ppx = kMargin, ppy = (ex.height - kH) / 2;
    const double back_x = ppx + kBackX + kBtn / 2.0, fwd_x = ppx + kFwdX + kBtn / 2.0, hist_y = ppy + kHistY + kBtn / 2.0;
    const double name_x = ppx + kColX + 100, name_y = ppy + kNameY + kNameH / 2.0;
    auto plus = [&](Picker& q, int row) { tap(q, ppx + kPlusX + 30, ppy + row_y(row) + 30); };
    auto minus = [&](Picker& q, int row) { tap(q, ppx + kMinusX + 30, ppy + row_y(row) + 30); };
    auto track = [&](Picker& q, int row, double f) { tap(q, ppx + kTrackX + f * (kTrackL - 1), ppy + row_y(row) + 30); };
    // the chooser's entry e, the middle of its cell, window px for the panel at x0 (the left one's by default)
    auto entry_x = [&](int e, double x0) { return x0 + chooser_x(e, n_el) + kChooserColW / 2.0; };
    auto entry_y = [&](int e) { return ppy + chooser_y(e, n_el) + kChooserRowH / 2.0; };
    auto row_tap = [&](Picker& q, int e) { tap(q, entry_x(e, ppx), entry_y(e)); };
    auto same_view = [](const ColourState& a, const ColourState& b) {
        return a.rgb == b.rgb && a.h == b.h && a.s == b.s && a.v == b.v;
    };
    cairo_surface_t* frame = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ex.width, ex.height);

    // ---------------------------------------------------------------- a scripted session over the active element
    {
        fresh();
        Picker p = launch();
        check(p.history().cursor == -1 && p.history().picks.empty() && p.active() == act0,
              "no state.json and no picks.txt: the manifest's active element and colour, an empty history");
        p.paint(frame);
        png(frame, work + "/frame_closed.png");
        tap(p, 1700, 700);
        check(p.open() && !p.panel_on_right(), "a tap on the right half opens the panel on the left");
        p.paint(frame);
        png(frame, work + "/frame_open_left.png");
        const Rgb start = p.colour().rgb;
        auto track_x = [&](int byte) { return ppx + kTrackX + byte / 255.0 * (kTrackL - 1); };
        p.press(track_x(200), ppy + row_y(3) + 30);
        p.move(track_x(230), ppy + row_y(3) + 40);
        p.release(track_x(230), ppy + row_y(3) + 40);
        check(p.colour().rgb.r == 230 && p.colour().rgb.g == start.g, "R's track sets the byte under the pen");
        plus(p, 5);
        check(p.colour().rgb.b == start.b + 1, "B's increment is one byte");
        p.press(ppx + kPad + kROut + 255, ppy + kPad + kROut);   // the ring at 0 degrees
        p.move(ppx + kPad + kROut, ppy + kPad + kROut - 255);    // to 90
        p.release(ppx + kPad + kROut, ppy + kPad + kROut - 255);
        check(std::abs(p.colour().h - 90) < 1e-9, "the ring sets the hue by the angle (90 at the top)");
        p.paint(frame);
        png(frame, work + "/frame_dragged.png");
        const long k = element_pixel(p.exp(), p.exp().elements[size_t(act0)].scene, act0, kMargin + kW + 10);
        check(k >= 0 && frame_word(frame, k, W) == word_of(p.colour().rgb), "the active element repaints live with the pick");
        tap(p, ppx + kColX + 50, ppy + kSwatchY0 + 50);
        check(p.colour().rgb == start, "a tap on OLD reverts");
        tap(p, ppx + kTrackX + 100, ppy + row_y(0) + 30);   // H's track, then close
        const std::string hex = hex_of(p.colour().rgb);
        tap(p, 1700, 700);
        check(!p.open(), "a tap outside the panel closes it");
        const std::string picks = slurp(work + "/picks.txt");
        check(picks.size() > 30 && picks.find(" " + L + " " + hex + " hsv ") != std::string::npos,
              "the commit appended to picks.txt: " + picks.substr(0, picks.size() - 1));
        std::map<std::string, Pick> st;
        std::map<std::string, int> ent;
        std::string act;
        check(state_load(work + "/state.json", st, ent, act, g_theme, g_model, err) && st.count(L) && hex_of(st[L].rgb) == hex &&
                  act == L && st.size() == p.exp().elements.size(),
              "state.json holds the commit, every element's colour and the active element");
        tap(p, 300, 700);
        check(p.open() && p.panel_on_right(), "a tap on the left half opens the panel on the right");
        p.paint(frame);
        png(frame, work + "/frame_open_right.png");
        tap(p, 300, 700);    // close again (the panel is on the right), unchanged: the history's last entry
        check(!p.open() && lines_of(work + "/picks.txt") == 1, "a close on the unedited colour appends nothing");
    }

    // ---------------------------------------------------------------- the pick history
    {
        fresh();
        Picker q = launch();
        const Rgb a0 = q.colour().rgb;
        tap(q, 1700, 700);
        check(q.open() && !q.back_enabled() && !q.forward_enabled() && count(q) == "0 of 0",
              "an empty history: both buttons disabled, 0 of 0");
        q.paint(frame);
        png(frame, work + "/frame_history_empty.png");
        tap(q, back_x, hist_y);
        tap(q, fwd_x, hist_y);
        check(q.colour().rgb == a0 && q.open(), "a disabled button does nothing");
        tap(q, 1700, 700);
        check(lines_of(work + "/picks.txt") == 1 && count(q) == "1 of 1",
              "a close with an empty history commits even an unchanged colour (as before): 1 of 1");
        tap(q, 1700, 700);
        plus(q, 3);                                   // R + 1
        const Rgb a1 = q.colour().rgb;
        tap(q, 1700, 700);
        tap(q, 1700, 700);
        plus(q, 4);                                   // G + 1
        const Rgb a2 = q.colour().rgb;
        tap(q, 1700, 700);
        check(lines_of(work + "/picks.txt") == 3 && count(q) == "3 of 3" && q.history().picks[1].rgb == a1 &&
                  q.history().picks[2].rgb == a2, "two edited closes commit: 3 of 3");
        tap(q, 1700, 700);
        check(q.back_enabled() && !q.forward_enabled(), "at the last entry: BACK enabled, FORWARD disabled");
        tap(q, back_x, hist_y);
        check(q.colour().rgb == a1 && count(q) == "2 of 3" && q.old() == a2, "BACK shows the entry before; OLD stays");
        q.paint(frame);
        const long k = element_pixel(q.exp(), q.exp().elements[size_t(act0)].scene, act0, kMargin + kW + 10);
        check(k >= 0 && frame_word(frame, k, W) == word_of(a1), "the active element repaints with the step");
        check(q.back_enabled() && q.forward_enabled(), "mid-history: both enabled");
        png(frame, work + "/frame_history_mid.png");
        tap(q, back_x, hist_y);
        check(q.colour().rgb == a0 && count(q) == "1 of 3" && !q.back_enabled() && q.forward_enabled(),
              "BACK to the first entry: BACK disabled");
        q.paint(frame);
        png(frame, work + "/frame_history_first.png");
        tap(q, back_x, hist_y);
        check(q.colour().rgb == a0 && count(q) == "1 of 3", "BACK at the first entry does nothing");
        tap(q, fwd_x, hist_y);
        check(q.colour().rgb == a1 && count(q) == "2 of 3", "FORWARD shows the entry after");
        tap(q, 1700, 700);
        check(lines_of(work + "/picks.txt") == 3, "a close on a stepped, unchanged colour appends nothing");
        q.paint(frame);
        png(frame, work + "/frame_history_closed.png");
        std::map<std::string, Pick> sc;
        std::map<std::string, int> se;
        std::string act;
        check(state_load(work + "/state.json", sc, se, act, g_theme, g_model, err) && sc[L].rgb == a1 && se[L] == 2,
              "state.json records the stepped colour and the cursor 2");

        Picker r = launch();
        check(r.colour().rgb == a1 && count(r) == "2 of 3", "a reload restores the colour and the cursor");
        tap(r, 1700, 700);
        plus(r, 5);                                   // B + 1: an unsaved edit
        check(r.edited() && r.back_enabled() && r.forward_enabled(), "an unsaved edit mid-history: both enabled");
        tap(r, back_x, hist_y);
        check(lines_of(work + "/picks.txt") == 3 && r.colour().rgb == a0 && count(r) == "1 of 3" && !r.edited(),
              "BACK from an unsaved edit discards it and steps from the cursor's entry");
        plus(r, 5);
        check(r.edited() && !r.back_enabled() && r.forward_enabled(), "an unsaved edit at the first entry: BACK disabled");
        tap(r, fwd_x, hist_y);
        check(lines_of(work + "/picks.txt") == 3 && r.colour().rgb == a1 && count(r) == "2 of 3",
              "FORWARD from an unsaved edit discards it too");
        tap(r, fwd_x, hist_y);                        // a2, 3 of 3
        plus(r, 3);                                   // an unsaved edit, then the app is left
        r.discard_if_open();
        check(!r.open() && lines_of(work + "/picks.txt") == 3 && r.colour().rgb == a1 && count(r) == "2 of 3",
              "leaving the app with an unsaved edit saves nothing: the colour and the cursor of the panel's opening");
        check(state_load(work + "/state.json", sc, se, act, g_theme, g_model, err) && sc[L].rgb == a1 && se[L] == 2, "and state.json is untouched");
        Picker r2 = launch();
        check(r2.colour().rgb == a1 && count(r2) == "2 of 3", "a relaunch after it comes back on the last saved state");
        tap(r, 1700, 700);
        plus(r, 3);
        const Rgb a4 = r.colour().rgb;
        tap(r, 1700, 700);
        check(lines_of(work + "/picks.txt") == 4 && count(r) == "4 of 4" && r.history().picks[3].rgb == a4,
              "a close on an edited colour from the middle commits it at the end: 4 of 4");
        // the first build's state.json: no entry; the newest entry equal to the colour, else the end
        put(work + "/state.json", "{\n \"" + L + "\": \"" + hex_of(a2) + "\"\n}\n");
        Picker o = launch();
        check(o.colour().rgb == a2 && count(o) == "3 of 4", "the first build's state.json: the newest equal entry");
        put(work + "/state.json", "{\n \"" + L + "\": \"#010203\"\n}\n");
        Picker e = launch();
        check(count(e) == "4 of 4" && e.edited(), "no entry equals the colour: the end, edited");
        tap(e, 1700, 700);
        tap(e, 1700, 700);
        check(lines_of(work + "/picks.txt") == 5, "and its close commits it");
        std::remove((work + "/picks.txt").c_str());
        Picker d = launch();
        check(count(d) == "0 of 0" && d.colour().rgb == (Rgb{1, 2, 3}), "picks.txt deleted: an empty history");
        put(work + "/picks.txt", "2026-10-04T05:00:00+00:00 " + L + " #12345\n");
        std::string why;
        const bool failed = load_fails(why);
        check(failed, "a malformed picks.txt fails the load: " + why);
    }

    // ---------------------------------------------------------------- the HSV he dialled is part of the pick
    {
        fresh();
        auto nums = [](const ColourState& c) {
            char b[160];
            std::snprintf(b, sizeof b, "H %d S %d V %d (exact %.6f %.6f %.6f) %s", int(std::nearbyint(c.h)),
                          int(std::nearbyint(c.s * 100)), int(std::nearbyint(c.v * 100)), c.h, c.s, c.v, hex_of(c.rgb).c_str());
            return std::string(b);
        };
        Picker q = launch();
        tap(q, 1700, 700);
        track(q, 0, 227.2 / 360); plus(q, 0); minus(q, 0);
        track(q, 1, 0.352); plus(q, 1); minus(q, 1);
        track(q, 2, 0.202); plus(q, 2); minus(q, 2);
        const ColourState d1 = q.colour();
        std::printf("     dialled: %s\n", nums(d1).c_str());
        tap(q, 1700, 700);
        tap(q, 1700, 700);
        plus(q, 2);
        const ColourState d2 = q.colour();
        tap(q, 1700, 700);
        tap(q, 1700, 700);
        tap(q, back_x, hist_y);
        const ColourState b1 = q.colour();
        tap(q, fwd_x, hist_y);
        const ColourState f2 = q.colour();
        check(same_view(b1, d1) && same_view(f2, d2), "BACK and FORWARD restore the HSV each pick was saved under");
        plus(q, 1);
        tap(q, ppx + kColX + 50, ppy + kSwatchY0 + 50);
        check(same_view(q.colour(), d2), "OLD restores the HSV the panel opened with: " + nums(q.colour()));
        tap(q, 1700, 700);
        Picker r = launch();
        check(same_view(r.colour(), d2), "a relaunch restores the HSV of the last close: " + nums(r.colour()));
        int moved = 0, handles = 0;
        for (int k = 0; k < 20; ++k) {
            Picker c = launch();
            const ColourState before = c.colour();
            tap(c, 1700, 700);
            plus(c, 2);
            tap(c, 1700, 700);
            Picker c2 = launch();
            tap(c2, 1700, 700);
            tap(c2, back_x, hist_y);
            tap(c2, fwd_x, hist_y);
            const ColourState after = c2.colour();
            tap(c2, 1700, 700);
            if (std::nearbyint(after.h) != std::nearbyint(before.h) || std::nearbyint(after.s * 100) != std::nearbyint(before.s * 100)) ++moved;
            if (after.h != before.h || after.s != before.s) ++handles;
        }
        check(moved == 0 && handles == 0, "20 saves of V + 1 (each relaunched, BACK, FORWARD): H and S never move (" +
                                              std::to_string(moved) + " cycles moved their numbers, " + std::to_string(handles) +
                                              " their exact values)");
        {
            Picker c = launch();
            const ColourState opened = c.colour();
            tap(c, 1700, 700);
            plus(c, 0);
            plus(c, 1);
            c.discard_if_open();
            check(same_view(c.colour(), opened), "leaving the app with an edit restores the opening's view: " + nums(c.colour()));
        }
        {
            Picker c = launch();
            tap(c, 1700, 700);
            track(c, 2, 0.022); plus(c, 2); minus(c, 2);
            tap(c, 1700, 700);
            const size_t n0 = lines_of(work + "/picks.txt");
            const ColourState s35 = c.colour();
            tap(c, 1700, 700);
            plus(c, 1);
            const ColourState s36 = c.colour();
            check(s36.rgb == s35.rgb && std::nearbyint(s36.s * 100) == 36 && c.edited(),
                  "S + 1 at V 2 keeps the bytes " + hex_of(s36.rgb) + " and is an edit: " + nums(s36));
            tap(c, 1700, 700);
            tap(c, 1700, 700);
            tap(c, back_x, hist_y);
            const ColourState b = c.colour();
            tap(c, fwd_x, hist_y);
            check(lines_of(work + "/picks.txt") == n0 + 1 && same_view(b, s35) && same_view(c.colour(), s36),
                  "its close commits it, and BACK / FORWARD show S 35 and S 36 over the same bytes");
            tap(c, 1700, 700);
        }
        auto refused = [&](const std::string& picks_text, const std::string& state_text, const std::string& what) {
            put(work + "/picks.txt", picks_text);
            if (state_text.empty()) std::remove((work + "/state.json").c_str());
            else put(work + "/state.json", state_text);
            std::string why;
            const bool failed = load_fails(why);
            check(failed, what + ": " + why);
        };
        refused("2026-10-04T05:00:00-04:00 " + L + " #212533 hsv 227 0.36 0.21\n", "", "a picks.txt view that does not give its hex fails the load");
        refused("2026-10-04T05:00:00-04:00 " + L + " #212533 hsv 587 0.35 0.2\n", "", "a picks.txt hue past 360 fails the load");
        refused("2026-10-04T05:00:00-04:00 " + L + " #212533 hsv 227 0.35\n", "", "a picks.txt view short of a number fails the load");
        refused("2026-10-04T05:00:00-04:00 chrome #212533 hsv 227 x 0.2\n", "", "a view number that is not one fails the load (any element's line)");
        refused("", "{\"colours\": {\"" + L + "\": \"#212533\"}, \"hsv\": {\"" + L + "\": [227, 0.36, 0.21]}, \"entry\": {}}",
                "a state.json view that does not give its colour fails the load");
        refused("", "{\"active\": 3, \"colours\": {}, \"entry\": {}}", "a state.json active that is not a key fails the load");
    }

    // ---------------------------------------------------------------- THE CHOOSER
    const auto scene_el = [&](int e) { return ex.elements[size_t(e)].scene; };
    const bool nine = chrome == act0 && chrome == 0 && canvas == 1 && ink == 2 && outl == 3 && wflag == 4 && wsel == 5 &&
                      pflag == 6 && psel == 7 && aflag == 8 && asel == 9 && rflag == 10 && rsel == 11 && phead == 12 &&
                      pstem == 13 && lbl == 14 && selfill == 15 && seltext == 16 && n_el % 2 == 1 && n_el >= 19 &&
                      scene_el(outl) != scene_el(chrome) && scene_el(outl) != scene_el(wflag) &&
                      scene_el(wflag) == scene_el(wsel) && scene_el(pflag) == scene_el(psel) &&
                      scene_el(aflag) == scene_el(asel) && scene_el(aflag) == scene_el(rflag) &&
                      scene_el(aflag) == scene_el(rsel) && scene_el(phead) == scene_el(pstem) &&
                      scene_el(selfill) == scene_el(seltext) && scene_el(wflag) != scene_el(chrome) &&
                      scene_el(pflag) != scene_el(chrome) && scene_el(pflag) != scene_el(wflag) &&
                      scene_el(aflag) != scene_el(chrome) && scene_el(aflag) != scene_el(wflag) &&
                      scene_el(aflag) != scene_el(pflag) && scene_el(aflag) != scene_el(phead) &&
                      scene_el(aflag) != scene_el(lbl) && scene_el(aflag) != scene_el(selfill) &&
                      scene_el(pflag) != scene_el(phead) && scene_el(pflag) != scene_el(lbl) &&
                      scene_el(pflag) != scene_el(selfill) && scene_el(phead) != scene_el(chrome) &&
                      scene_el(phead) != scene_el(wflag) && scene_el(lbl) != scene_el(chrome) &&
                      scene_el(lbl) != scene_el(wflag) && scene_el(lbl) != scene_el(phead) &&
                      scene_el(selfill) != scene_el(wflag) && scene_el(selfill) != scene_el(chrome) &&
                      scene_el(selfill) != scene_el(phead) && scene_el(selfill) != scene_el(lbl) &&
                      scene_el(last) != scene_el(selfill) && scene_el(last) != scene_el(wflag);
    if (!nine) {
        check(false, "the check export lists Chrome (active), Canvas, Ink over one scene, the Outline over its own, Warp "
                     "Flag and Selected Warp Flag over a second, Phase Reset Flag and Selected Phase Reset Flag over a third, Added Flag, Selected "
                     "Added Flag, Removed Flag and Selected Removed Flag over a fourth, Playhead Head and Playhead Stem over "
                     "a fifth, the Label over a sixth, Selection and Selected Text over a seventh, and test elements to an "
                     "odd count of at least nineteen entries, the last over another scene than the Selection's and the "
                     "warp flags'");
    } else {
        fresh();
        const Rgb chrome0 = ex.elements[size_t(chrome)].colour, ink0 = ex.elements[size_t(ink)].colour,
                  uf0 = ex.elements[size_t(wflag)].colour, sf0 = ex.elements[size_t(wsel)].colour;
        const Rgb tint{0x20, 0x60, 0x48};
        Picker p = launch();
        tap(p, 1700, 700);
        // THE CHOOSER'S FIT (architect 2026-10-05, two columns): the rectangle it covers, read from the frame with the
        // chooser closed and open, here and again at another colour below (the wheel, every readout, handle and swatch
        // moved): open, it is the same picture both times, so nothing under it shows through
        const int rows = chooser_rows(n_el), cx0 = kChooserX0, cx1 = kChooserX1, cy0 = kChooserY,
                  cy1 = kChooserY + rows * kChooserRowH;
        auto chooser_px = [&](Picker& q) {
            q.paint(frame);
            std::vector<uint32_t> v;
            for (int y = int(ppy) + cy0; y < int(ppy) + cy1; ++y)
                for (int x = int(ppx) + cx0; x < int(ppx) + cx1; ++x) v.push_back(frame_word(frame, long(y) * W + x, W));
            return v;
        };
        const std::vector<uint32_t> closed0 = chooser_px(p);
        tap(p, name_x, name_y);
        check(p.chooser_open() && p.open(), "a tap on the element's name opens the chooser (Chrome, Canvas, Ink, Warp "
                                            "Flag, Selected Warp Flag, ...)");
        const std::vector<uint32_t> open0 = chooser_px(p);
        png(frame, work + "/frame_chooser_open.png");
        {
            char fit[400];
            std::snprintf(fit, sizeof fit, "the chooser's %d entries fit in two columns of %d and %d rows: panel x %d..%d, "
                          "y %d..%d (window y %d..%d), inside the panel's pad (%d) and the screen (%d); %d entries would "
                          "fit (y ..%d)", n_el, rows, n_el - rows, cx0, cx1, cy0, cy1, int(ppy) + cy0, int(ppy) + cy1,
                          kH - kPad, ex.height, kChooserMax, kChooserY + chooser_rows(kChooserMax) * kChooserRowH);
            check(n_el <= kChooserMax && kChooserMax >= 22 && cy1 <= kH - kPad && int(ppy) + cy1 <= ex.height &&
                      chooser_x(last, n_el) == kChooserX0 + kChooserColW && chooser_y(last, n_el) == cy1 - 2 * kChooserRowH,
                  fit);
            std::printf("     the chooser at 16 entries (the round's): panel y %d..%d; at 22: %d..%d (the slider rows from "
                        "%d, the panel's pad at %d)\n", cy0, kChooserY + chooser_rows(16) * kChooserRowH, cy0,
                        kChooserY + chooser_rows(22) * kChooserRowH, row_y(0), kH - kPad);
        }
        tap(p, ppx + 200, ppy + 1000);                // inside the panel, outside the chooser
        check(!p.chooser_open() && p.open() && p.active() == chrome && !std::ifstream(work + "/state.json"),
              "a tap outside the chooser closes it and changes nothing (the panel open, no file written)");
        tap(p, name_x, name_y);
        tap(p, 2200, 1300);                           // outside the panel too: only the chooser closes
        check(!p.chooser_open() && p.open(), "a tap outside the panel while the chooser is open closes the chooser alone");
        tap(p, name_x, name_y);
        p.press(entry_x(ink, ppx), entry_y(ink));     // pressed on Ink, lifted on Warp Flag
        p.release(entry_x(wflag, ppx), entry_y(wflag));
        check(p.chooser_open() && p.active() == chrome, "a press on one entry lifted on another chooses nothing (the chooser stays)");
        tap(p, ppx + kChooserX0 + kChooserColW * 1.5, ppy + cy1 - kChooserRowH / 2.0);   // the second column's empty cell
        check(p.chooser_open() && p.active() == chrome && !std::ifstream(work + "/state.json"),
              "a tap on the second column's empty cell (an odd count) chooses nothing and leaves the chooser open");
        row_tap(p, chrome);
        check(!p.chooser_open() && p.active() == chrome && !std::ifstream(work + "/state.json") &&
                  !std::ifstream(work + "/picks.txt"), "choosing the active element again is a no-op");
        // the chrome at a tint, dialled by its R / G / B tracks: every line follows
        track(p, 3, tint.r / 255.0);
        track(p, 4, tint.g / 255.0);
        track(p, 5, tint.b / 255.0);
        check(p.colour().rgb == tint, "the chrome dialled to #206048 by its tracks");
        {
            const std::vector<uint32_t> closed1 = chooser_px(p);
            tap(p, name_x, name_y);
            const std::vector<uint32_t> open1 = chooser_px(p);
            size_t under = 0;
            for (size_t k = 0; k < closed0.size(); ++k) under += closed0[k] != closed1[k];
            check(p.chooser_open() && open1 == open0 && under > 0,
                  "the chooser is painted over everything it covers: at #206048 its " + std::to_string(open0.size()) +
                      " px are the same picture as before, while " + std::to_string(under) + " px under it (the wheel, the "
                      "hex, the swatch, the readouts, tracks and handles of the slider rows it runs over) changed");
            tap(p, ppx + 200, ppy + 1000);            // outside the chooser (a slider row's): it closes alone
        }
        p.paint(frame);
        png(frame, work + "/frame_chrome_tint_open.png");
        {
            std::vector<uint32_t> words;
            role_words(p.exp(), words);
            bool lines_ok = true;
            std::string said;
            for (size_t r = 0; r < p.exp().roles.size(); ++r) {
                const Role& R = p.exp().roles[r];
                if (R.kind != Role::Kind::Scale) continue;
                const Rgb want = scale_rgb(tint, R.num, R.den);
                lines_ok = lines_ok && words[r] == word_of(want);
                said += " " + R.name + " " + hex_of(want);
            }
            check(lines_ok, "every chrome line is the tint x its Windows 95 byte / 192:" + said);
        }
        // the switch to Ink commits the edited chrome
        tap(p, name_x, name_y);
        row_tap(p, ink);
        std::map<std::string, Pick> st;
        std::map<std::string, int> ent;
        std::string act;
        const bool st_ok = state_load(work + "/state.json", st, ent, act, g_theme, g_model, err);
        check(p.active() == ink && p.open() && !p.chooser_open() && lines_of(work + "/picks.txt") == 1 &&
                  slurp(work + "/picks.txt").find(" chrome #206048 hsv ") != std::string::npos && st_ok && act == "ink" &&
                  st["chrome"].rgb == tint && ent["chrome"] == 1 && p.element(chrome).hist.cursor == 0,
              "the edited chrome is committed by the switch (one picks.txt line, state.json), the panel on Ink");
        check(p.old() == ink0 && p.colour().rgb == ink0 && count(p) == "0 of 0",
              "OLD is the chosen element's current colour; its own history (0 of 0)");
        p.paint(frame);
        const int wsc = p.exp().elements[size_t(ink)].scene;
        const long ki = element_pixel(p.exp(), wsc, ink, kMargin + kW + 10), kg = element_pixel(p.exp(), wsc, chrome, 1300);
        check(ki >= 0 && kg >= 0 && frame_word(frame, ki, W) == word_of(ink0) && frame_word(frame, kg, W) == word_of(tint),
              "the chrome's tint stays live beside the ink");
        // an edited ink, then the switch to the Warp Flag: the flags scene
        plus(p, 3);
        const Rgb ink1 = p.colour().rgb;
        tap(p, name_x, name_y);
        row_tap(p, wflag);
        const int fsc = p.exp().elements[size_t(wflag)].scene;
        const long kf = element_pixel(p.exp(), fsc, wflag, kMargin + kW + 10), ks = element_pixel(p.exp(), fsc, wsel, -1),
                   kc = element_pixel(p.exp(), fsc, chrome, 1300), kn = element_pixel(p.exp(), fsc, ink, kMargin + kW + 10);
        check(p.active() == wflag && lines_of(work + "/picks.txt") == 2 &&
                  slurp(work + "/picks.txt").find(" ink " + hex_of(ink1) + " hsv ") != std::string::npos,
              "the edited ink is committed by the switch to the Warp Flag");
        check(kf >= 0 && ks >= 0 && kc >= 0 && kn >= 0 && p.picture()[size_t(kf)] == word_of(uf0) &&
                  p.picture()[size_t(ks)] == word_of(sf0) && p.picture()[size_t(kc)] == word_of(tint) &&
                  p.picture()[size_t(kn)] == word_of(ink1),
              "the switch shows the flags scene: both flag faces at their colours, the chrome's tint and the ink's new colour "
              "live in it");
        plus(p, 4);                                   // the face moves: its labels' antialiased edges re-blend over it
        const Rgb uf1 = p.colour().rgb;
        check(p.picture()[size_t(kf)] == word_of(uf1) && p.picture()[size_t(ks)] == word_of(sf0),
              "the warp face repaints live, its selected face stays");
        p.paint(frame);
        png(frame, work + "/frame_warp_flag_open.png");
        // the switch to the Selected Warp Flag: the same scene, the warp face's edit committed
        tap(p, name_x, name_y);
        row_tap(p, wsel);
        check(p.active() == wsel && lines_of(work + "/picks.txt") == 3 &&
                  slurp(work + "/picks.txt").find(" warp_flag " + hex_of(uf1) + " hsv ") != std::string::npos &&
                  p.colour().rgb == sf0 && count(p) == "0 of 0" && p.picture()[size_t(kf)] == word_of(uf1),
              "the edited warp face is committed by the switch to the Selected Warp Flag (#" + hex_of(sf0).substr(1) +
                  ", 0 of 0)");
        p.paint(frame);
        png(frame, work + "/frame_warp_flag_selected_open.png");
        // the switch to the Selection: a third scene (the open flag); the selected face's empty history commits it
        tap(p, name_x, name_y);
        row_tap(p, selfill);
        const int esc = p.exp().elements[size_t(selfill)].scene;
        const long ke = element_pixel(p.exp(), esc, selfill, -1);
        check(p.active() == selfill && lines_of(work + "/picks.txt") == 4 && ke >= 0 &&
                  p.picture()[size_t(ke)] == word_of(ex.elements[size_t(selfill)].colour),
              "the switch to the Selection (the selected face committed, as an empty history's close): its scene");
        plus(p, 4);                                   // the band moves: the selected text's edges re-blend over it
        p.paint(frame);
        png(frame, work + "/frame_selection_switch_open.png");
        // the chooser's LAST entry, over the slider rows: picked by a tap there, its scene of its own; the Selection
        // committed
        tap(p, name_x, name_y);
        row_tap(p, last);
        const long kt = element_pixel(p.exp(), scene_el(last), last, -1);
        check(p.active() == last && !p.chooser_open() && lines_of(work + "/picks.txt") == 5 && kt >= 0 &&
                  p.picture()[size_t(kt)] == word_of(ex.elements[size_t(last)].colour) &&
                  slurp(work + "/picks.txt").find(" selected_fill " + hex_of(p.element(selfill).cs.rgb) + " hsv ") != std::string::npos,
              "a tap on the chooser's last entry (" + ex.elements[size_t(last)].name + ", panel y " +
                  std::to_string(chooser_y(last, n_el)) + ".." + std::to_string(chooser_y(last, n_el) + kChooserRowH) +
                  ", the second column's foot, over the slider rows) picks it: its scene; the edited Selection committed");
        tap(p, name_x, name_y);
        row_tap(p, chrome);
        check(p.active() == chrome && lines_of(work + "/picks.txt") == 6 && count(p) == "1 of 1" && p.colour().rgb == tint,
              "the switch back to the chrome: its own history (1 of 1), its tint");
        tap(p, 2200, 1300);
        check(!p.open() && lines_of(work + "/picks.txt") == 6, "an unedited chrome's close appends nothing");
        p.paint(frame);
        png(frame, work + "/frame_chrome_tint.png");
        // a relaunch: the element he left, every element's colour, history and view
        Picker r = launch();
        bool hists = r.element(canvas).hist.cursor == -1;
        for (int e : {chrome, ink, wflag, wsel, selfill, last}) hists = hists && r.element(e).hist.picks.size() == 1;
        check(r.active() == chrome && r.colour().rgb == tint && r.element(ink).cs.rgb == ink1 &&
                  r.element(canvas).cs.rgb == ex.elements[size_t(canvas)].colour && r.element(wflag).cs.rgb == uf1 &&
                  r.element(wsel).cs.rgb == sf0 && r.element(selfill).cs.rgb == p.element(selfill).cs.rgb && hists,
              "a relaunch returns to the element he left, every element's colour and history its own");
        check(same_view(r.element(chrome).cs, p.element(chrome).cs) && same_view(r.element(ink).cs, p.element(ink).cs) &&
                  same_view(r.element(wflag).cs, p.element(wflag).cs) && same_view(r.element(wsel).cs, p.element(wsel).cs) &&
                  same_view(r.element(selfill).cs, p.element(selfill).cs), "and every element's view exactly as it was");
        // histories and cursors independent across a switch
        tap(r, 1700, 700);
        tap(r, name_x, name_y);
        row_tap(r, ink);
        plus(r, 0);
        tap(r, name_x, name_y);
        row_tap(r, chrome);                           // commits ink 2
        tap(r, name_x, name_y);
        row_tap(r, ink);
        tap(r, back_x, hist_y);
        check(r.active() == ink && count(r) == "1 of 2" && r.colour().rgb == ink1, "the ink's own history: BACK to 1 of 2");
        tap(r, name_x, name_y);
        row_tap(r, chrome);                           // a stepped ink is no edit: nothing appended
        check(count(r) == "1 of 1" && r.element(ink).hist.cursor == 0 && lines_of(work + "/picks.txt") == 7,
              "the chrome's cursor and the ink's are their own across the switch (a stepped ink switched away appends nothing)");
        Picker r2 = launch();
        check(r2.active() == chrome && r2.element(ink).hist.cursor == 0 && r2.element(ink).cs.rgb == ink1 &&
                  r2.element(chrome).hist.cursor == 0 && r2.element(wflag).hist.cursor == 0 &&
                  r2.element(wsel).hist.cursor == 0 && r2.element(selfill).hist.cursor == 0 &&
                  r2.element(last).hist.cursor == 0, "and across a relaunch");
        std::remove((work + "/state.json").c_str());
        Picker r3 = launch();
        check(r3.active() == chrome && r3.colour().rgb == chrome0 && r3.element(chrome).hist.cursor == 0 && r3.edited() &&
                  r3.element(ink).cs.rgb == ink0 && r3.element(ink).hist.cursor == 1 && r3.element(canvas).hist.cursor == -1 &&
                  r3.element(wflag).cs.rgb == uf0 && r3.element(wflag).hist.cursor == 0,
              "state.json deleted: the manifest's active element and colours, each history at its end");
    }

    // ---------------------------------------------------------------- THE PLAYHEAD (architect 2026-10-04)
    // its two elements over their own scene, the panel on the right (a tap on the left half), so the playhead at x 293
    // stays in view: the head and the stem each repaint alone, the head's one-LW outline the theme's label (the Label
    // element since the label round, at its starting #FFFFFF here); each committed by the switch away or the close, and
    // both kept by a relaunch
    if (nine) {
        fresh();
        const double rpx = W - kMargin - kW, rname_x = rpx + kColX + 100;
        auto rchoose = [&](Picker& q, int e) {
            tap(q, rname_x, name_y);
            tap(q, entry_x(e, rpx), entry_y(e));
        };
        auto rplus = [&](Picker& q, int row) { tap(q, rpx + kPlusX + 30, ppy + row_y(row) + 30); };
        const Rgb ph0 = ex.elements[size_t(phead)].colour, ps0 = ex.elements[size_t(pstem)].colour;
        const uint32_t white = word_of(Rgb{0xFF, 0xFF, 0xFF});
        Picker p = launch();
        tap(p, 600, 700);
        rchoose(p, phead);                            // the chrome left: its empty history commits it (one line)
        const int psc = scene_el(phead);
        // kh the head's first face pixel (its top face row, the outline's LW rows above it), kb the outline pixel over it
        const long kh = element_pixel(p.exp(), psc, phead, -1), ks = element_pixel(p.exp(), psc, pstem, -1),
                   kb = kh - W;
        p.paint(frame);
        check(p.open() && p.panel_on_right() && p.active() == phead && count(p) == "0 of 0" && kh >= 0 && ks >= 0 &&
                  kb >= 0 && kh % W < rpx && ks % W < rpx && p.picture()[size_t(kh)] == word_of(ph0) &&
                  p.picture()[size_t(ks)] == word_of(ps0) && p.picture()[size_t(kb)] == white &&
                  frame_word(frame, kh, W) == word_of(ph0) && frame_word(frame, kb, W) == white,
              "the switch to the Playhead Head shows the playhead scene, the playhead left of the panel: the head " +
                  hex_of(ph0) + ", its outline the Label's #FFFFFF, the stem " + hex_of(ps0));
        rplus(p, 4);                                  // the head moves: the stem and the outline stay
        const Rgb ph1 = p.colour().rgb;
        check(ph1 != ph0 && p.picture()[size_t(kh)] == word_of(ph1) && p.picture()[size_t(ks)] == word_of(ps0) &&
                  p.picture()[size_t(kb)] == white, "the head repaints live, the stem and the head's outline stay");
        p.paint(frame);
        png(frame, work + "/frame_playhead_head_open.png");
        rchoose(p, pstem);
        check(p.active() == pstem && lines_of(work + "/picks.txt") == 2 &&
                  slurp(work + "/picks.txt").find(" playhead_head " + hex_of(ph1) + " hsv ") != std::string::npos &&
                  p.colour().rgb == ps0 && count(p) == "0 of 0" && p.picture()[size_t(kh)] == word_of(ph1),
              "the edited head is committed by the switch to the Playhead Stem (" + hex_of(ps0) + ", 0 of 0)");
        rplus(p, 5);                                  // the stem moves: the head and the outline stay
        const Rgb ps1 = p.colour().rgb;
        check(ps1 != ps0 && p.picture()[size_t(ks)] == word_of(ps1) && p.picture()[size_t(kh)] == word_of(ph1) &&
                  p.picture()[size_t(kb)] == white, "the stem repaints live, the head and its outline stay");
        p.paint(frame);
        png(frame, work + "/frame_playhead_stem_open.png");
        tap(p, 300, 1300);                            // outside the panel: the close commits the stem
        check(!p.open() && lines_of(work + "/picks.txt") == 3 &&
                  slurp(work + "/picks.txt").find(" playhead_stem " + hex_of(ps1) + " hsv ") != std::string::npos,
              "the close commits the edited stem");
        Picker r = launch();
        bool others = true;
        for (int e = 0; e < int(r.exp().elements.size()); ++e)
            if (e != phead && e != pstem)
                others = others && r.element(e).cs.rgb == ex.elements[size_t(e)].colour &&
                         r.element(e).hist.picks.size() == (e == chrome ? 1u : 0u);
        check(r.active() == pstem && r.element(phead).cs.rgb == ph1 && r.element(pstem).cs.rgb == ps1 &&
                  r.element(phead).hist.picks.size() == 1 && r.element(pstem).hist.picks.size() == 1 && others &&
                  same_view(r.element(phead).cs, p.element(phead).cs) && same_view(r.element(pstem).cs, p.element(pstem).cs),
              "a relaunch keeps both, each with its own history (1 of 1) and view; every other element at the manifest's "
              "colour (the chrome's one pick its commit on the way)");
        fresh();
    }

    // ---------------------------------------------------------------- THE LABEL (architect 2026-10-04)
    // the chrome's text colour, one element over its own scene (the waveform scene with row 8's state line on), the
    // panel on the right so the menu, the clock and the state line stay in view. Every role the theme's label roots
    // follows it (the words, the clock, the ruler's timestamps, the icons' text ink, the scroll arrow, the playhead
    // head's outline); the flag labels are program keys of their own and stay fixed, the selected text the Selected
    // Text element's (the open-flag round). Its edit
    // repaints the solid word pixels and re-blends the antialiased edges over the chrome; committed by the close, kept
    // by a relaunch
    if (nine) {
        fresh();
        const double rpx = W - kMargin - kW, rname_x = rpx + kColX + 100;
        auto rminus = [&](Picker& q, int row) { tap(q, rpx + kMinusX + 30, ppy + row_y(row) + 30); };
        const Rgb lb0 = ex.elements[size_t(lbl)].colour;
        bool roles_ok = true;
        std::string follow;
        for (const Role& R : ex.roles) {
            if (R.name == "label") roles_ok = roles_ok && R.kind == Role::Kind::Element && R.el == lbl;
            if (R.name == "flag_label" || R.name == "flag_label_sel")
                roles_ok = roles_ok && R.kind == Role::Kind::Colour && R.rgb == (Rgb{0xFF, 0xFF, 0xFF});
            if (R.name == "selected_text") roles_ok = roles_ok && R.kind == Role::Kind::Element && R.el == seltext;
            if (R.kind == Role::Kind::Element && R.el == lbl) follow += " " + R.name;
        }
        check(roles_ok && lb0 == (Rgb{0xFF, 0xFF, 0xFF}),
              "the Label starts #FFFFFF and its role is the element (the export's role:" + follow + "); the flag labels "
              "stay fixed #FFFFFF, the selected text is the Selected Text's");
        Picker p = launch();
        tap(p, 600, 700);
        tap(p, rname_x, name_y);
        tap(p, entry_x(lbl, rpx), entry_y(lbl));      // the chrome left: one line
        const int lsc = scene_el(lbl);
        const long kl = element_pixel(p.exp(), lsc, lbl, -1);
        const Scene& sc = p.exp().scenes[size_t(lsc)];
        std::vector<std::pair<uint32_t, uint32_t>> aa;   // the antialiased edges reading the label, left of the panel
        for (const Stack& st : sc.stacks)
            if ((st.deps & (1u << lbl)) && int(st.index % uint32_t(W)) < rpx) aa.push_back({st.index, p.picture()[st.index]});
        check(p.open() && p.panel_on_right() && p.active() == lbl && count(p) == "0 of 0" && kl >= 0 && kl % W < rpx &&
                  !aa.empty() && p.picture()[size_t(kl)] == word_of(lb0),
              "the switch to the Label shows its scene, the panel on the right: a word's solid pixel at " + hex_of(lb0));
        rminus(p, 3);                                 // R one byte down: every word follows
        const Rgb lb1 = p.colour().rgb;
        size_t moved_aa = 0;
        for (const auto& [k, w] : aa) moved_aa += p.picture()[k] != w;
        check(lb1 == (Rgb{0xFE, 0xFF, 0xFF}) && p.picture()[size_t(kl)] == word_of(lb1) && moved_aa > 0,
              "the Label repaints live: the solid pixel " + hex_of(lb1) + ", " + std::to_string(moved_aa) + " of " +
                  std::to_string(aa.size()) + " antialiased edges re-blended (the rest round to the same byte)");
        p.paint(frame);
        png(frame, work + "/frame_label_open.png");
        tap(p, 300, 1300);                            // outside the panel: the close commits the Label
        check(!p.open() && lines_of(work + "/picks.txt") == 2 &&
                  slurp(work + "/picks.txt").find(" label " + hex_of(lb1) + " hsv ") != std::string::npos,
              "the close commits the edited Label");
        Picker r = launch();
        check(r.active() == lbl && r.element(lbl).cs.rgb == lb1 && r.element(lbl).hist.picks.size() == 1 &&
                  same_view(r.element(lbl).cs, p.element(lbl).cs),
              "a relaunch keeps the Label, its history (1 of 1) and view");
        tap(r, 600, 700);
        tap(r, rname_x, name_y);
        tap(r, entry_x(phead, rpx), entry_y(phead));
        const int psc = scene_el(phead);
        const long kh = element_pixel(r.exp(), psc, phead, -1);
        const long kf = element_pixel(r.exp(), psc, wflag, -1);
        bool flag_white = false;                      // a flag label's solid pixel in the playhead scene stays white
        for (size_t q = 0; q < r.exp().roles.size() && !flag_white; ++q)
            if (r.exp().roles[q].name == "flag_label")
                for (const Run& run : r.exp().scenes[size_t(psc)].solid[q])
                    if (run.len) { flag_white = r.picture()[run.start] == word_of(Rgb{0xFF, 0xFF, 0xFF}); break; }
        check(kh >= 0 && kf >= 0 && r.picture()[size_t(kh - W)] == word_of(lb1) && flag_white,
              "in the playhead scene the head's outline is the Label's " + hex_of(lb1) + "; the flag labels stay #FFFFFF");
        fresh();
    }

    // ---------------------------------------------------------------- THE OPEN FLAG (architect 2026-10-04)
    // the selected pair, Selection and Selected Text, over their own scene: the fourth flag's editor open, its whole
    // text selected (the editor opens so), the panel on the left so the open flag stays in view, the theme strip's
    // column too. The Selection repaints the band and re-blends the selected text's antialiased edges over it, the text
    // staying; the Selected Text repaints its glyphs and their edges, the band staying; the flag's face (the Selected
    // Flag's) and the editor's black frame stay; each committed by the switch away or the close, both kept by a relaunch
    if (nine) {
        fresh();
        const Rgb sl0 = ex.elements[size_t(selfill)].colour, st0 = ex.elements[size_t(seltext)].colour,
                  sf0 = ex.elements[size_t(wsel)].colour;
        bool roles_ok = true;
        for (const Role& R : ex.roles) {
            if (R.name == "selected_fill") roles_ok = roles_ok && R.kind == Role::Kind::Element && R.el == selfill;
            if (R.name == "selected_text") roles_ok = roles_ok && R.kind == Role::Kind::Element && R.el == seltext;
            if (R.name == "flag_border_edit") roles_ok = roles_ok && R.kind == Role::Kind::Colour && R.rgb == (Rgb{0, 0, 0});
        }
        check(roles_ok && sl0 == (Rgb{0x66, 0x66, 0x66}) && st0 == (Rgb{0xFF, 0xFF, 0xFF}),
              "the Selection starts #666666 and the Selected Text #FFFFFF, each role its element; the editor's frame "
              "fixed #000000");
        const int view_x = kMargin + kW + kStripGap + kStripW;   // right of the panel on the left and its strip
        Picker p = launch();
        tap(p, 1700, 700);
        tap(p, name_x, name_y);
        row_tap(p, selfill);                          // the chrome left: its empty history commits it (one line)
        const int osc = scene_el(selfill);
        const Scene& sc = p.exp().scenes[size_t(osc)];
        const long kb = element_pixel(p.exp(), osc, selfill, view_x), kt = element_pixel(p.exp(), osc, seltext, view_x),
                   kf = element_pixel(p.exp(), osc, wsel, view_x);
        long kframe = -1;                             // the editor's frame
        for (size_t q = 0; q < p.exp().roles.size() && kframe < 0; ++q)
            if (p.exp().roles[q].name == "flag_border_edit")
                for (const Run& run : sc.solid[q])
                    if (run.len && int(run.start % uint32_t(W)) > view_x) { kframe = long(run.start); break; }
        auto edges = [&](int e) {                     // the antialiased pixels reading e, in view
            std::vector<std::pair<uint32_t, uint32_t>> v;
            for (const Stack& st : sc.stacks)
                if ((st.deps & (1u << e)) && int(st.index % uint32_t(W)) > view_x) v.push_back({st.index, p.picture()[st.index]});
            return v;
        };
        const auto band_aa = edges(selfill), text_aa = edges(seltext);
        p.paint(frame);
        check(p.open() && !p.panel_on_right() && p.active() == selfill && count(p) == "0 of 0" && kb >= 0 && kt >= 0 &&
                  kf >= 0 && kframe >= 0 && !band_aa.empty() && !text_aa.empty() && p.picture()[size_t(kb)] == word_of(sl0) &&
                  p.picture()[size_t(kt)] == word_of(st0) && p.picture()[size_t(kf)] == word_of(sf0) &&
                  p.picture()[size_t(kframe)] == word_of(Rgb{0, 0, 0}) && frame_word(frame, kb, W) == word_of(sl0) &&
                  frame_word(frame, kt, W) == word_of(st0),
              "the switch to the Selection shows the open flag right of the panel and the strip's column (x > " +
                  std::to_string(view_x) + "): the band " + hex_of(sl0) + ", the text " + hex_of(st0) + " (" +
                  std::to_string(text_aa.size()) + " antialiased px over the band), the face " + hex_of(sf0) +
                  ", the frame black");
        plus(p, 4);                                   // the band moves: its text's edges re-blend over it
        const Rgb sl1 = p.colour().rgb;
        size_t moved = 0;
        for (const auto& [k, w] : band_aa) moved += p.picture()[k] != w;
        check(sl1 != sl0 && p.picture()[size_t(kb)] == word_of(sl1) && p.picture()[size_t(kt)] == word_of(st0) &&
                  p.picture()[size_t(kf)] == word_of(sf0) && p.picture()[size_t(kframe)] == word_of(Rgb{0, 0, 0}) && moved > 0,
              "the Selection repaints live: the band " + hex_of(sl1) + ", " + std::to_string(moved) + " of " +
                  std::to_string(band_aa.size()) + " antialiased edges re-blended; the text, the face and the frame stay");
        p.paint(frame);
        png(frame, work + "/frame_selection_open.png");
        tap(p, name_x, name_y);
        row_tap(p, seltext);
        check(p.active() == seltext && lines_of(work + "/picks.txt") == 2 &&
                  slurp(work + "/picks.txt").find(" selected_fill " + hex_of(sl1) + " hsv ") != std::string::npos &&
                  p.colour().rgb == st0 && count(p) == "0 of 0" && p.picture()[size_t(kb)] == word_of(sl1),
              "the edited Selection is committed by the switch to the Selected Text (" + hex_of(st0) + ", 0 of 0)");
        const auto text_aa1 = edges(seltext);
        minus(p, 3);                                  // R one byte down: the glyphs and their edges follow
        const Rgb st1 = p.colour().rgb;
        size_t tmoved = 0;
        for (const auto& [k, w] : text_aa1) tmoved += p.picture()[k] != w;
        check(st1 == (Rgb{0xFE, 0xFF, 0xFF}) && p.picture()[size_t(kt)] == word_of(st1) &&
                  p.picture()[size_t(kb)] == word_of(sl1) && p.picture()[size_t(kf)] == word_of(sf0) && tmoved > 0,
              "the Selected Text repaints live: the glyphs " + hex_of(st1) + ", " + std::to_string(tmoved) + " of " +
                  std::to_string(text_aa1.size()) + " antialiased edges re-blended (the rest round to the same byte); the "
                  "band and the face stay");
        p.paint(frame);
        png(frame, work + "/frame_selected_text_open.png");
        tap(p, 1700, 700);                            // outside the panel: the close commits the Selected Text
        check(!p.open() && lines_of(work + "/picks.txt") == 3 &&
                  slurp(work + "/picks.txt").find(" selected_text " + hex_of(st1) + " hsv ") != std::string::npos,
              "the close commits the edited Selected Text");
        Picker r = launch();
        bool others = true;
        for (int e = 0; e < int(r.exp().elements.size()); ++e)
            if (e != selfill && e != seltext)
                others = others && r.element(e).cs.rgb == ex.elements[size_t(e)].colour &&
                         r.element(e).hist.picks.size() == (e == chrome ? 1u : 0u);
        check(r.active() == seltext && r.element(selfill).cs.rgb == sl1 && r.element(seltext).cs.rgb == st1 &&
                  r.element(selfill).hist.picks.size() == 1 && r.element(seltext).hist.picks.size() == 1 && others &&
                  same_view(r.element(selfill).cs, p.element(selfill).cs) &&
                  same_view(r.element(seltext).cs, p.element(seltext).cs) && r.picture()[size_t(kb)] == word_of(sl1) &&
                  r.picture()[size_t(kt)] == word_of(st1),
              "a relaunch keeps both, each with its own history (1 of 1) and view, the open flag at them; every other "
              "element at the manifest's colour");
        fresh();
    }

    // ---------------------------------------------------------------- THE FLAG KINDS (architect 2026-10-05)
    // each kind's face and selected face over the scene that paints the kind as the app does: the phase-reset pair over
    // the scene phase_reset (the phase-reset column, every flag the lane token "reset", the fourth selected with its
    // payload addressed, so its stem is bright), the added and the removed pairs over the scene history (the `h` view's
    // diff flags, `[+]` / `[-]` and the token, the fourth a changed pair, the view's focus: both its halves the selected
    // faces, its stem the removed half's). The panel on the left (no strip open), so the faces right of it stay in view.
    // Each face repaints with its stem where it has one (a changed pair's added half has none: the stem is the face of
    // the half it leaves from), every other face of the scene staying; the labels stay fixed #FFFFFF -- `flag_label`
    // over a face, `flag_label_sel` over a selected one, the product's one label pair (render.cpp resolve_flag_face) --
    // their antialiased edges re-blending over the live face; each committed by the switch away or the close, both kept
    // by a relaunch. THE NAMES' FIT (with the phase-reset pair, whose selected name is the longest): the element button
    // sets each name to end clear of the chooser's head, and every chooser entry ends inside its cell (fit_px), measured
    // in the painted frame
    if (nine) {
        const Rgb white{0xFF, 0xFF, 0xFF};
        const uint32_t whitew = word_of(white);
        const int view_x = kMargin + kW;              // right of the panel on the left (no strip open)
        const int ox = int(ppx), oy = int(ppy);
        const uint32_t fieldw = word_of(Rgb{0x21, 0x21, 0x21}), edgew = word_of(Rgb{0, 0, 0});
        auto button_right = [&]() {                   // the name's rightmost pixel in the element button, the head's left
            const int mx = ox + kNameX1 - 36, my = oy + kNameY + kNameH / 2 - 6;
            int r = -1;
            for (int y = oy + kNameY + 1; y < oy + kNameY + kNameH - 1; ++y)
                for (int x = ox + kColX + 1; x < ox + kNameX1 - 1; ++x) {
                    const int i = my + 11 - y;
                    if (i >= 0 && i < 12 && x >= mx - 1 - i && x < mx + 1 + i) continue;   // the head
                    const uint32_t w = frame_word(frame, long(y) * W + x, W);
                    if (w != fieldw && w != edgew) r = std::max(r, x);
                }
            return std::make_pair(r, mx - 12);
        };
        // one pair's session: ue the face, se the selected face, each with its starting colour and whether it has a
        // stem in the scene; `others` the scene's other faces, which stay
        auto pair_session = [&](int ue, int se, Rgb u_start, Rgb s_start, bool u_stem, bool s_stem,
                                const std::vector<int>& others, bool names) {
            fresh();
            const Rgb ui0 = ex.elements[size_t(ue)].colour, si0 = ex.elements[size_t(se)].colour;
            const std::string un = ex.elements[size_t(ue)].name, sn = ex.elements[size_t(se)].name;
            bool roles_ok = true;
            int r_label = -1, r_slabel = -1;
            for (size_t q = 0; q < ex.roles.size(); ++q) {
                const Role& R = ex.roles[q];
                if (R.kind == Role::Kind::Element && (R.el == ue || R.el == se))
                    roles_ok = roles_ok && ex.scenes[size_t(scene_el(R.el))].solid[q].size() > 0;
                if (R.name == "flag_label") { r_label = int(q); roles_ok = roles_ok && R.kind == Role::Kind::Colour && R.rgb == white; }
                if (R.name == "flag_label_sel") { r_slabel = int(q); roles_ok = roles_ok && R.kind == Role::Kind::Colour && R.rgb == white; }
            }
            check(roles_ok && r_label >= 0 && r_slabel >= 0 && ui0 == u_start && si0 == s_start,
                  "the " + un + " starts " + hex_of(u_start) + " and the " + sn + " " + hex_of(s_start) + ", each role "
                  "its element; the flag label and the selected label fixed #FFFFFF");
            Picker p = launch();
            tap(p, 1700, 700);
            tap(p, name_x, name_y);
            row_tap(p, ue);                           // the chrome left: its empty history commits it (one line)
            const int ksc = scene_el(ue);
            const Scene& sc = p.exp().scenes[size_t(ksc)];
            auto stem_foot = [&](int e, long face) {  // e's last solid pixel within 8 columns of the face's: its stem's foot
                long k = -1;
                const int x0 = int(face % W);
                for (size_t r = 0; r < p.exp().roles.size(); ++r) {
                    const Role& R = p.exp().roles[r];
                    if (R.kind != Role::Kind::Element || R.el != e) continue;
                    for (const Run& run : sc.solid[r])
                        for (uint32_t j = run.start; j < run.start + run.len; ++j)
                            if (int(j % uint32_t(W)) >= x0 && int(j % uint32_t(W)) < x0 + 8) k = std::max(k, long(j));
                }
                return k;
            };
            auto label_px = [&](int r) {              // a label's first solid pixel in view
                for (const Run& run : sc.solid[size_t(r)])
                    for (uint32_t j = run.start; j < run.start + run.len; ++j)
                        if (int(j % uint32_t(W)) > view_x) return long(j);
                return -1L;
            };
            auto edges = [&](int e) {                 // the antialiased pixels reading e
                std::vector<std::pair<uint32_t, uint32_t>> v;
                for (const Stack& st : sc.stacks)
                    if (st.deps & (1u << e)) v.push_back({st.index, p.picture()[st.index]});
                return v;
            };
            auto stays = [&](const std::vector<std::pair<long, uint32_t>>& v) {
                bool ok = true;
                for (const auto& [k, w] : v) ok = ok && p.picture()[size_t(k)] == w;
                return ok;
            };
            const long ku = element_pixel(p.exp(), ksc, ue, view_x), ks = element_pixel(p.exp(), ksc, se, view_x),
                       kus = stem_foot(ue, ku), kss = stem_foot(se, ks), kl = label_px(r_label), ksl = label_px(r_slabel);
            std::vector<std::pair<long, uint32_t>> other_px;   // every other face of the scene, in view
            std::string other_said;
            for (int e : others) {
                const long k = element_pixel(p.exp(), ksc, e, view_x);
                other_px.push_back({k, k >= 0 ? p.picture()[size_t(k)] : 0});
                other_said += " " + p.exp().elements[size_t(e)].name + " " + hex_of(p.element(e).cs.rgb) + ",";
            }
            bool others_ok = true;
            for (const auto& [k, w] : other_px) others_ok = others_ok && k >= 0;
            const auto u_aa = edges(ue), s_aa = edges(se);
            p.paint(frame);
            // a stem runs from the face's box down the waveform (its foot rows under the face); none stays in the box
            auto stem_ok = [&](long face, long foot, bool has) {
                return face >= 0 && foot >= 0 && (has ? foot / W > face / W + 100 : foot / W < face / W + 60);
            };
            const bool stems = stem_ok(ku, kus, u_stem) && stem_ok(ks, kss, s_stem);
            check(p.open() && !p.panel_on_right() && p.active() == ue && count(p) == "0 of 0" && stems && others_ok &&
                      kl >= 0 && ksl >= 0 && !u_aa.empty() && !s_aa.empty() && p.picture()[size_t(ku)] == word_of(ui0) &&
                      p.picture()[size_t(kus)] == word_of(ui0) && p.picture()[size_t(ks)] == word_of(si0) &&
                      p.picture()[size_t(kss)] == word_of(si0) && p.picture()[size_t(kl)] == whitew &&
                      p.picture()[size_t(ksl)] == whitew && frame_word(frame, ku, W) == word_of(ui0) &&
                      frame_word(frame, ks, W) == word_of(si0),
                  "the switch to the " + un + " shows the " + p.exp().scenes[size_t(ksc)].name + " scene, both faces right "
                  "of the panel (x " + std::to_string(ku % W) + " and " + std::to_string(ks % W) + "): " + hex_of(ui0) +
                  " and " + hex_of(si0) + (u_stem ? ", the face's stem to row " + std::to_string(kus / W) : ", no stem") +
                  (s_stem ? ", the selected face's to row " + std::to_string(kss / W) : ", the selected face no stem of its "
                  "own") + "; both labels #FFFFFF (" + std::to_string(u_aa.size()) + " and " + std::to_string(s_aa.size()) +
                  " antialiased px over the faces); the scene's other faces:" + other_said);
            plus(p, 4);                               // the face moves: its stem with it, its labels' edges re-blend
            const Rgb ui1 = p.colour().rgb;
            size_t umoved = 0, ustill = 0;
            for (const auto& [k, w] : u_aa) umoved += p.picture()[k] != w;
            for (const auto& [k, w] : s_aa) ustill += p.picture()[k] != w;
            check(ui1 != ui0 && p.picture()[size_t(ku)] == word_of(ui1) && p.picture()[size_t(kus)] == word_of(ui1) &&
                      p.picture()[size_t(ks)] == word_of(si0) && p.picture()[size_t(kss)] == word_of(si0) &&
                      p.picture()[size_t(kl)] == whitew && stays(other_px) && umoved > 0 && ustill == 0,
                  "the " + un + " repaints live: the face" + (u_stem ? " and its stem " : " ") + hex_of(ui1) + ", " +
                      std::to_string(umoved) + " of " + std::to_string(u_aa.size()) + " antialiased edges of its labels "
                      "re-blended over it (the label #FFFFFF); the selected face, its edges and the other faces stay");
            p.paint(frame);
            png(frame, work + "/frame_" + ex.elements[size_t(ue)].key + "_open.png");
            {
                const auto [ub_r, head_x] = button_right();
                check(ub_r >= 0 && ub_r < head_x - 1,
                      "the element button's \"" + un + "\" ends at panel x " + std::to_string(ub_r - ox) + ", " +
                          std::to_string(head_x - 1 - ub_r) + " px clear of the chooser's head (" +
                          std::to_string(head_x - ox) + ")");
            }
            tap(p, name_x, name_y);
            if (names) {                              // every entry inside its cell, in the painted frame
                p.paint(frame);
                png(frame, work + "/frame_chooser_kinds_open.png");
                int worst = -1, worst_e = -1, worst_x1 = 0;
                bool rows_ok = p.chooser_open();
                for (int e = 0; e < n_el; ++e) {
                    const int cx = ox + chooser_x(e, n_el), ry = oy + chooser_y(e, n_el), x1 = cx + kChooserColW;
                    int r = -1;
                    for (int y = ry + 2; y < ry + kChooserRowH - 1; ++y)
                        for (int x = cx + kChooserNameDX; x < x1 - 1; ++x) {
                            const uint32_t w = frame_word(frame, long(y) * W + x, W);
                            if (w != fieldw && w != edgew) r = std::max(r, x);
                        }
                    rows_ok = rows_ok && r >= 0 && r < x1 - 2;
                    if (r - x1 > worst - worst_x1 || worst_e < 0) { worst = r; worst_e = e; worst_x1 = x1; }
                }
                check(rows_ok, "every chooser entry ends inside its cell: the nearest its edge, \"" +
                                   (worst_e >= 0 ? ex.elements[size_t(worst_e)].name : std::string()) + "\", " +
                                   std::to_string(worst_x1 - 1 - worst) + " px inside it (its name at " +
                                   std::to_string(kChooserNameW) + " px or set smaller)");
            }
            row_tap(p, se);
            check(p.active() == se && lines_of(work + "/picks.txt") == 2 &&
                      slurp(work + "/picks.txt").find(" " + ex.elements[size_t(ue)].key + " " + hex_of(ui1) + " hsv ") !=
                          std::string::npos &&
                      p.colour().rgb == si0 && count(p) == "0 of 0" && p.picture()[size_t(ku)] == word_of(ui1),
                  "the edited " + un + " is committed by the switch to the " + sn + " (" + hex_of(si0) + ", 0 of 0)");
            p.paint(frame);
            {
                const auto [sb_r, sh_x] = button_right();
                check(sb_r >= 0 && sb_r < sh_x - 1,
                      "the element button's \"" + sn + "\" ends at panel x " + std::to_string(sb_r - ox) + ", " +
                          std::to_string(sh_x - 1 - sb_r) + " px clear of the chooser's head");
            }
            const auto s_aa1 = edges(se), u_aa1 = edges(ue);
            plus(p, 4);                               // the selected face moves: its stem with it, its labels' edges re-blend
            const Rgb si1 = p.colour().rgb;
            size_t smoved = 0, sstill = 0;
            for (const auto& [k, w] : s_aa1) smoved += p.picture()[k] != w;
            for (const auto& [k, w] : u_aa1) sstill += p.picture()[k] != w;
            check(si1 != si0 && p.picture()[size_t(ks)] == word_of(si1) && p.picture()[size_t(kss)] == word_of(si1) &&
                      p.picture()[size_t(ku)] == word_of(ui1) && p.picture()[size_t(kus)] == word_of(ui1) &&
                      p.picture()[size_t(ksl)] == whitew && stays(other_px) && smoved > 0 && sstill == 0,
                  "the " + sn + " repaints live: the face" + (s_stem ? " and its stem " : " ") + hex_of(si1) + ", " +
                      std::to_string(smoved) + " of " + std::to_string(s_aa1.size()) + " antialiased edges of the "
                      "selected label re-blended over it (the label #FFFFFF); the " + un + ", its edges and the other "
                      "faces stay");
            p.paint(frame);
            png(frame, work + "/frame_" + ex.elements[size_t(se)].key + "_open.png");
            tap(p, 1700, 700);                        // outside the panel: the close commits the selected face
            check(!p.open() && lines_of(work + "/picks.txt") == 3 &&
                      slurp(work + "/picks.txt").find(" " + ex.elements[size_t(se)].key + " " + hex_of(si1) + " hsv ") !=
                          std::string::npos,
                  "the close commits the edited " + sn);
            Picker r = launch();
            bool rest = true;
            for (int e = 0; e < n_el; ++e)
                if (e != ue && e != se)
                    rest = rest && r.element(e).cs.rgb == ex.elements[size_t(e)].colour &&
                           r.element(e).hist.picks.size() == (e == chrome ? 1u : 0u);
            check(r.active() == se && r.element(ue).cs.rgb == ui1 && r.element(se).cs.rgb == si1 &&
                      r.element(ue).hist.picks.size() == 1 && r.element(se).hist.picks.size() == 1 && rest &&
                      same_view(r.element(ue).cs, p.element(ue).cs) && same_view(r.element(se).cs, p.element(se).cs) &&
                      r.picture()[size_t(ku)] == word_of(ui1) && r.picture()[size_t(ks)] == word_of(si1),
                  "a relaunch keeps both, each with its own history (1 of 1) and view, the flags at them; every other "
                  "element at the manifest's colour");
            fresh();
        };
        pair_session(pflag, psel, Rgb{0x66, 0x66, 0x99}, Rgb{0x99, 0x99, 0xCC}, true, true, {}, true);
        pair_session(aflag, asel, Rgb{0x80, 0x80, 0x00}, Rgb{0x00, 0x80, 0x00}, true, false, {rflag, rsel}, false);
        pair_session(rflag, rsel, Rgb{0x99, 0x33, 0x33}, Rgb{0xFF, 0x66, 0x66}, true, true, {aflag, asel}, false);
    }

    // ---------------------------------------------------------------- THE OUTLINE (architect 2026-10-05)
    // an element of its own, no inherited default: over the scene `magnified` (the waveform scene with the magnification
    // lamp lit, render.py magnified_masks: the only picture the product paints the outline in), starting at the colour
    // the earlier rule gave (the ink over the canvas, 50 % linear light), then independent of both. The panel on the
    // left; the outline repaints alone, the ink and the canvas move it no more; committed by the switch or the close,
    // kept by a relaunch
    if (nine) {
        fresh();
        const Rgb ink0 = ex.elements[size_t(ink)].colour, cv0 = ex.elements[size_t(canvas)].colour,
                  ol0 = ex.elements[size_t(outl)].colour;
        bool as_element = false, derived = false;
        for (const Role& R : ex.roles) {
            derived = derived || R.kind == Role::Kind::Derive;
            as_element = as_element || (R.kind == Role::Kind::Element && R.el == outl);
        }
        const int osc = scene_el(outl);
        size_t opx = 0;
        for (size_t r = 0; r < ex.roles.size(); ++r)
            if (ex.roles[r].kind == Role::Kind::Element && ex.roles[r].el == outl)
                for (const Run& run : ex.scenes[size_t(osc)].solid[r]) opx += run.len;
        check(as_element && !derived && ol0 == lin_mix(ink0, cv0, 0.5) && opx > 0 &&
                  ex.scenes[size_t(osc)].name == "magnified",
              "the Outline is an element over the scene magnified (" + std::to_string(opx) + " outline px), its role "
              "the element's and no role derived, starting at " + hex_of(ol0) + ", the ink " + hex_of(ink0) +
              " over the canvas " + hex_of(cv0) + " by the earlier rule");
        Picker p = launch();
        tap(p, 1700, 700);
        tap(p, name_x, name_y);
        row_tap(p, outl);                             // the chrome left: its empty history commits it
        const long ko = element_pixel(p.exp(), osc, outl, kMargin + kW + 10),
                   ki = element_pixel(p.exp(), osc, ink, kMargin + kW + 10),
                   kc = element_pixel(p.exp(), osc, canvas, kMargin + kW + 10);
        p.paint(frame);
        check(p.active() == outl && count(p) == "0 of 0" && ko >= 0 && ki >= 0 && kc >= 0 &&
                  p.picture()[size_t(ko)] == word_of(ol0) && frame_word(frame, ko, W) == word_of(ol0) &&
                  p.picture()[size_t(ki)] == word_of(ink0),
              "the switch to the Outline shows the lit plate, the outline right of the panel at " + hex_of(ol0));
        plus(p, 3);
        const Rgb ol1 = p.colour().rgb;
        check(ol1 != ol0 && p.picture()[size_t(ko)] == word_of(ol1) && p.picture()[size_t(ki)] == word_of(ink0) &&
                  p.picture()[size_t(kc)] == word_of(cv0), "the outline repaints live, the ink and the canvas stay");
        p.paint(frame);
        png(frame, work + "/frame_outline_open.png");
        tap(p, name_x, name_y);
        row_tap(p, ink);
        plus(p, 4);
        const Rgb ink1 = p.colour().rgb;
        tap(p, name_x, name_y);
        row_tap(p, canvas);
        plus(p, 5);
        const Rgb cv1 = p.colour().rgb;
        tap(p, name_x, name_y);
        row_tap(p, outl);
        check(p.element(outl).cs.rgb == ol1 && p.picture()[size_t(ko)] == word_of(ol1) &&
                  p.picture()[size_t(ki)] == word_of(ink1) && p.picture()[size_t(kc)] == word_of(cv1) &&
                  lin_mix(ink1, cv1, 0.5) != ol1 && lines_of(work + "/picks.txt") == 4 &&
                  slurp(work + "/picks.txt").find(" waveform_outline " + hex_of(ol1) + " hsv ") != std::string::npos,
              "the ink and the canvas moved (" + hex_of(ink1) + ", " + hex_of(cv1) + ", the old rule's outline " +
                  hex_of(lin_mix(ink1, cv1, 0.5)) + "): the outline stays " + hex_of(ol1) + ", committed by the switch");
        tap(p, 1700, 700);
        Picker r = launch();
        check(r.active() == outl && r.element(outl).cs.rgb == ol1 && r.element(outl).hist.picks.size() == 1 &&
                  same_view(r.element(outl).cs, p.element(outl).cs) && r.element(ink).cs.rgb == ink1,
              "a relaunch keeps the outline (1 of 1, its view) beside the moved ink");
        fresh();
    }

    // ---------------------------------------------------------------- COPY AND PASTE (architect 2026-10-05)
    // two buttons under OLD | NEW: COPY takes the active element's colour and view, PASTE sets the active element to it
    // as an edit (OLD reverting, the close the one save); PASTE greyed until a copy; the copied colour never saved
    if (nine) {
        fresh();
        // the brightest channel inside a button's field (its word's ink: white lit, the dim grey greyed), window px
        auto brightest = [&](double px0, int x0, int x1) {
            int m = 0;
            for (int y = int(ppy) + kClipY + 2; y < int(ppy) + kClipY + kClipH - 2; ++y)
                for (int x = int(px0) + x0 + 2; x < int(px0) + x1 - 2; ++x) {
                    const uint32_t w = frame_word(frame, long(y) * W + x, W);
                    m = std::max({m, int(w >> 16 & 255), int(w >> 8 & 255), int(w & 255)});
                }
            return m;
        };
        auto copy_at = [&](Picker& q, double px0) { tap(q, px0 + (kCopyX0 + kCopyX1) / 2.0, ppy + kClipY + kClipH / 2.0); };
        auto paste_at = [&](Picker& q, double px0) { tap(q, px0 + (kPasteX0 + kPasteX1) / 2.0, ppy + kClipY + kClipH / 2.0); };
        auto model_to = [&](Picker& q, double px0, int m) {
            tap(q, px0 + kModelX0 + 60, ppy + kModelY + kModelH / 2.0);
            tap(q, px0 + kModelX0 + 60, ppy + kModelListY + m * kChooserRowH + kChooserRowH / 2.0);
        };
        const Rgb sf0 = ex.elements[size_t(wsel)].colour;
        Picker p = launch();
        tap(p, 1700, 700);
        const Rgb c0 = p.colour().rgb;
        p.paint(frame);
        const int dim = brightest(ppx, kPasteX0, kPasteX1), lit_copy = brightest(ppx, kCopyX0, kCopyX1);
        png(frame, work + "/frame_copy_paste_greyed.png");
        paste_at(p, ppx);
        check(!p.paste_enabled() && dim == 0x55 && lit_copy == 0xFF && p.colour().rgb == c0 &&
                  !std::ifstream(work + "/state.json") && !std::ifstream(work + "/picks.txt"),
              "before any copy PASTE is greyed (its word's brightest channel " + std::to_string(dim) + ", COPY's " +
                  std::to_string(lit_copy) + ") and its lift does nothing");
        tap(p, name_x, name_y);
        row_tap(p, wflag);                            // the chrome's empty history commits it: one line
        model_to(p, ppx, 1);                          // HSL: the view he dials there is the one copied
        track(p, 0, 200.4 / 360);
        track(p, 2, 0.4137);
        const Pick copied = p.colour().pick();
        copy_at(p, ppx);
        p.paint(frame);
        const int lit = brightest(ppx, kPasteX0, kPasteX1);
        png(frame, work + "/frame_copy_paste_left.png");
        check(p.paste_enabled() && lit == 0xFF && p.clip().rgb == copied.rgb && p.clip().model == Model::Hsl &&
                  p.clip().x[0] == copied.x[0] && p.clip().x[1] == copied.x[1] && p.clip().x[2] == copied.x[2] &&
                  p.colour().rgb == copied.rgb && p.edited(),
              "COPY takes the Warp Flag's colour " + hex_of(copied.rgb) + " and its HSL view, the colour unchanged and "
              "still an unsaved edit; PASTE lit (" + std::to_string(lit) + ")");
        model_to(p, ppx, 0);                          // the panel in HSV: the paste re-derives HSV, keeps the HSL view
        tap(p, name_x, name_y);
        row_tap(p, wsel);                             // the warp face's edit committed: two lines
        paste_at(p, ppx);
        ColourState ref;
        ref.set_rgb(copied.rgb);
        const std::array<double, 3> hsl = p.colour().view(Model::Hsl);
        const Pick pasted = p.colour().pick();
        p.paint(frame);
        const long knew = long(int(ppy) + kSwatchY0 + 40) * W + int(ppx) + kNewX + 40,
                   kold = long(int(ppy) + kSwatchY0 + 40) * W + int(ppx) + kColX + 40,
                   ks = element_pixel(p.exp(), scene_el(wsel), wsel, -1);
        png(frame, work + "/frame_paste_open.png");
        check(p.active() == wsel && p.colour().rgb == copied.rgb && hsl[0] == copied.x[0] && hsl[1] == copied.x[1] &&
                  hsl[2] == copied.x[2] && pasted.model == Model::Hsl && p.colour().h == ref.h && p.colour().s == ref.s &&
                  p.colour().v == ref.v && p.edited() && frame_word(frame, knew, W) == word_of(copied.rgb) &&
                  frame_word(frame, kold, W) == word_of(sf0) && ks >= 0 && p.picture()[size_t(ks)] == word_of(copied.rgb) &&
                  lines_of(work + "/picks.txt") == 2,
              "PASTE on the Selected Warp Flag: the bytes and the HSL view arrive exactly (the HSV the panel shows "
              "re-derived from the bytes), an edit, NEW " + hex_of(copied.rgb) + " and the selected face live, OLD " +
                  hex_of(sf0) + ", nothing written");
        tap(p, ppx + kColX + 50, ppy + kSwatchY0 + 50);
        check(p.colour().rgb == sf0 && p.paste_enabled(), "OLD reverts the paste; the copy is still there");
        paste_at(p, ppx);
        tap(p, 1700, 700);                            // the close: one pick, the copied colour and view
        const std::string want = " warp_flag_selected " + hex_of(copied.rgb) + " hsl " + view_number(copied.x[0]) + " " +
                                 view_number(copied.x[1]) + " " + view_number(copied.x[2]) + "\n";
        const std::string picks = slurp(work + "/picks.txt");
        check(!p.open() && lines_of(work + "/picks.txt") == 3 && picks.size() > want.size() &&
                  picks.compare(picks.size() - want.size(), want.size(), want) == 0 && count(p) == "1 of 1",
              "the close commits the paste once:" + want.substr(0, want.size() - 1));
        tap(p, 1700, 700);
        model_to(p, ppx, 1);
        minus(p, 2);                                  // then just tweak the lightness
        const Rgb tweaked = p.colour().rgb;
        tap(p, 1700, 700);
        check(lines_of(work + "/picks.txt") == 4 && tweaked != copied.rgb && p.element(wsel).cs.hsl[0] == copied.x[0] &&
                  p.element(wsel).cs.hsl[1] == copied.x[1], "a lightness − on the pasted colour keeps its hue and "
                  "saturation exactly, " + hex_of(tweaked) + " committed by the close");
        // the panel on the right: the same two controls in the same place, pasted onto another element
        const double rpx = W - kMargin - kW;
        tap(p, 300, 700);
        tap(p, rpx + kColX + 100, name_y);
        tap(p, entry_x(pflag, rpx), entry_y(pflag));
        paste_at(p, rpx);
        p.paint(frame);
        const int rlit = brightest(rpx, kPasteX0, kPasteX1), rcopy = brightest(rpx, kCopyX0, kCopyX1);
        png(frame, work + "/frame_copy_paste_right.png");
        check(p.panel_on_right() && p.active() == pflag && p.colour().rgb == copied.rgb && rlit == 0xFF && rcopy == 0xFF,
              "the panel on the right: COPY and PASTE in the same place, the paste onto the Phase Reset Flag");
        {
            char ext[300];
            std::snprintf(ext, sizeof ext, "COPY at panel x %d..%d and PASTE at %d..%d, y %d..%d (window x %d..%d and "
                          "%d..%d with the panel on the left, %d..%d and %d..%d on the right): under the swatches (..%d), "
                          "right of the model switch (..%d), over the slider rows (%d..)", kCopyX0, kCopyX1, kPasteX0,
                          kPasteX1, kClipY, kClipY + kClipH, int(ppx) + kCopyX0, int(ppx) + kCopyX1, int(ppx) + kPasteX0,
                          int(ppx) + kPasteX1, int(rpx) + kCopyX0, int(rpx) + kCopyX1, int(rpx) + kPasteX0,
                          int(rpx) + kPasteX1, kSwatchY1, kModelX1, row_y(0));
            check(kClipY > kSwatchY1 && kClipY + kClipH + 12 < row_y(0) && kCopyX0 > kModelX1 && kCopyX1 < kPasteX0 &&
                      kPasteX1 <= kW - kPad && rpx + kPasteX1 <= W, ext);
        }
        tap(p, 300, 1300);
        Picker r = launch();
        tap(r, 1700, 700);
        check(!r.paste_enabled() && !std::ifstream(work + "/state.json").fail() &&
                  slurp(work + "/state.json").find("clip") == std::string::npos,
              "a relaunch starts with nothing copied (the copy is never saved): PASTE greyed");
        fresh();
    }

    // ---------------------------------------------------------------- THE RENAMED KEYS (architect 2026-10-05)
    // files written before the flag kinds name the flag elements' old keys (scene.h renamed_key): state.json's colours,
    // views, entries and active element, picks.txt's lines and a preset's colours read as the elements that replaced
    // them -- `unselected_flag` as the Warp Flag AND the Phase Reset Flag, `selected_flag` as both selected faces,
    // `unselected_invalid_flag` as the Removed Flag, `selected_invalid_flag` as the Selected Removed Flag -- each
    // history its lines in the file's order beside the new key's own; and every file the app writes then names the new
    // keys alone, picks.txt's old lines kept as they were
    if (nine) {
        fresh();
        auto hsl_of = [](Rgb c) {
            ColourState v;
            v.show(Model::Hsl);
            v.set_rgb(c);
            return view_number(v.hsl[0]) + " " + view_number(v.hsl[1]) + " " + view_number(v.hsl[2]);
        };
        auto hsv_of = [](Rgb c) {
            ColourState v;
            v.set_rgb(c);
            return "[" + view_number(v.h) + ", " + view_number(v.s) + ", " + view_number(v.v) + "]";
        };
        const Rgb uf{0x80, 0x80, 0xC0}, uf_old{0x7D, 0x81, 0xBF}, sf{0x9C, 0x9B, 0xD0}, ui{0xAA, 0x33, 0x33},
                  si{0xFF, 0x77, 0x77}, pr{0x60, 0x60, 0x90}, ps1{0x12, 0x34, 0x56}, ps2{0x65, 0x43, 0x21};
        const std::string picks0 =
            "2026-10-04T09:00:00-04:00 unselected_flag " + hex_of(uf_old) + " hsl " + hsl_of(uf_old) + "\n" +
            "2026-10-04T09:01:00-04:00 selected_flag " + hex_of(sf) + "\n" +
            "2026-10-04T09:02:00-04:00 unselected_invalid_flag " + hex_of(ui) + "\n" +
            "2026-10-04T09:03:00-04:00 unselected_flag " + hex_of(uf) + " hsl " + hsl_of(uf) + "\n" +
            "2026-10-05T09:00:00-04:00 phase_reset_flag " + hex_of(pr) + "\n";
        put(work + "/picks.txt", picks0);
        {
            ColourState v;
            v.show(Model::Hsl);
            v.set_rgb(uf);
            put(work + "/state.json",
                "{\"active\": \"selected_invalid_flag\", \"model\": \"hsl\", \"colours\": {\"unselected_flag\": \"" +
                    hex_of(uf) + "\", \"selected_flag\": \"" + hex_of(sf) + "\", \"unselected_invalid_flag\": \"" + hex_of(ui) +
                    "\", \"selected_invalid_flag\": \"" + hex_of(si) + "\"}, \"hsl\": {\"unselected_flag\": [" +
                    view_number(v.hsl[0]) + ", " + view_number(v.hsl[1]) + ", " + view_number(v.hsl[2]) +
                    "]}, \"entry\": {\"unselected_flag\": 2, \"selected_flag\": 1, \"unselected_invalid_flag\": 1}}\n");
        }
        put(work + "/presets.json", "{\"presets\": [{\"number\": 1, \"saved\": \"2026-10-04T09:05:00-04:00\", \"colours\": "
                                    "{\"selected_flag\": \"" + hex_of(ps1) + "\", \"selected_invalid_flag\": \"" +
                                    hex_of(ps2) + "\"}, \"hsv\": {\"selected_flag\": " + hsv_of(ps1) +
                                    ", \"selected_invalid_flag\": " + hsv_of(ps2) + "}}]}\n");
        Picker t = launch();
        auto at = [&](int e, Rgb c, size_t m, int cursor) {
            return t.element(e).cs.rgb == c && t.element(e).hist.picks.size() == m && t.element(e).hist.cursor == cursor;
        };
        const bool views = t.element(wflag).cs.dialled == Model::Hsl && t.element(pflag).cs.dialled == Model::Hsl &&
                           t.element(wflag).cs.view(Model::Hsl) == t.element(pflag).cs.view(Model::Hsl) &&
                           t.element(wflag).hist.picks[0].rgb == uf_old && t.element(pflag).hist.picks[2].rgb == pr;
        check(at(wflag, uf, 2, 1) && at(pflag, uf, 3, 1) && at(wsel, sf, 1, 0) && at(psel, sf, 1, 0) && at(rflag, ui, 1, 0) &&
                  at(rsel, si, 0, -1) && at(aflag, ex.elements[size_t(aflag)].colour, 0, -1) &&
                  at(asel, ex.elements[size_t(asel)].colour, 0, -1) && views && t.active() == rsel &&
                  t.presets().size() == 1 && t.presets()[0].colours.count("warp_flag_selected") &&
                  t.presets()[0].colours.count("phase_reset_flag_selected") &&
                  t.presets()[0].colours.count("removed_flag_selected") && t.presets()[0].colours.size() == 3,
              "the old keys read as the new elements: unselected_flag's colour, HSL view and entry on the Warp Flag (2 of 2) "
              "and the Phase Reset Flag (2 of 3, its own line after the old two), selected_flag on both selected faces, "
              "the invalid pair on the removed pair, the active selected_invalid_flag the Selected Removed Flag, the "
              "preset's two old keys its three new elements");
        tap(t, 1700, 700);
        tap(t, ppx + kPresetsX + 60, name_y);         // commits the Selected Removed Flag (its empty history)
        tap(t, ppx + kPopX0 + 200, ppy + kPopListY0 + kPopRowH / 2.0);   // Preset 1
        tap(t, ppx + kPresetsX + 60, name_y);
        tap(t, ppx + kPopX0 + 200, ppy + kPopY0 + kPopRowH / 2.0);       // Save as Preset 2
        tap(t, 1700, 700);
        const std::string picks1 = slurp(work + "/picks.txt"), state1 = slurp(work + "/state.json"),
                          presets1 = slurp(work + "/presets.json");
        bool no_old = true;
        for (const char* k : {"unselected_flag", "selected_flag", "unselected_invalid_flag", "selected_invalid_flag"}) {
            const std::string q = std::string("\"") + k + "\"", w = std::string(" ") + k + " ";
            no_old = no_old && state1.find(q) == std::string::npos && presets1.find(q) == std::string::npos &&
                     picks1.find(w, picks0.size()) == std::string::npos;
        }
        check(picks1.compare(0, picks0.size(), picks0) == 0 && no_old && t.element(wsel).cs.rgb == ps1 &&
                  t.element(psel).cs.rgb == ps1 && t.element(rsel).cs.rgb == ps2 &&
                  occurrences(picks1.substr(picks0.size()), " warp_flag_selected " + hex_of(ps1)) == 1 &&
                  occurrences(picks1.substr(picks0.size()), " phase_reset_flag_selected " + hex_of(ps1)) == 1 &&
                  occurrences(picks1.substr(picks0.size()), " removed_flag_selected " + hex_of(ps2)) == 1 &&
                  t.presets().size() == 2,
              "the old preset loads onto its new elements (one commit each, under the new keys), and the files the app "
              "writes then -- state.json, presets.json, picks.txt's new lines -- name the new keys alone, the old lines "
              "kept as they were");
        Picker u = launch();
        check(u.active() == rsel && u.element(wflag).hist.picks.size() == 2 && u.element(pflag).hist.picks.size() == 3 &&
                  u.element(wsel).hist.picks.size() == 2 && u.element(psel).hist.picks.size() == 2 &&
                  u.element(rsel).hist.picks.size() == 2 && u.element(wsel).hist.cursor == 1 && u.element(rsel).cs.rgb == ps2,
              "a relaunch over the rewritten files: each history its old lines and its new ones, every cursor at its "
              "entry");
        fresh();
    }

    // ---------------------------------------------------------------- THE PRESETS AND THE THEME STRIP
    if (nine) {
        fresh();
        const double pres_x = ppx + kPresetsX + 60, save_x = ppx + kPopX0 + 200, save_y = ppy + kPopY0 + kPopRowH / 2.0;
        auto presets_btn = [&](Picker& q) { tap(q, pres_x, name_y); };
        auto choose_el = [&](Picker& q, int e) { tap(q, name_x, name_y); row_tap(q, e); };
        // the pop-up's item k, scrolled into view by a drag (the drag acts on nothing), then its window y
        auto item_y = [&](Picker& q, int k) {
            const int want = std::max(0, k * kPopRowH - 400);
            const double delta = want - q.pop_scroll(), y0 = ppy + kPopListY0 + 500;
            if (delta != 0) {
                q.press(save_x, y0);
                q.move(save_x, y0 - delta - (delta > 0 ? kSlop : -kSlop));
                q.release(save_x, y0 - delta - (delta > 0 ? kSlop : -kSlop));
            }
            return ppy + kPopListY0 + k * kPopRowH - q.pop_scroll() + kPopRowH / 2.0;
        };
        auto theme_item = [&](const Picker& q, const std::string& key) {
            return int(q.presets().size()) + 1 + theme_of(q.exp(), key);
        };
        auto open_theme = [&](Picker& q, const std::string& key) {
            presets_btn(q);
            const int k = theme_item(q, key);
            const double y = item_y(q, k);
            tap(q, save_x, y);
        };
        auto strip_tap = [&](Picker& q, int i) { tap(q, q.strip_x() + 60, q.strip_row_y(i) + 20); };
        auto strip_close = [&](Picker& q) { tap(q, q.strip_x() + kStripW - kStripPad - kBtn / 2.0, ppy + kStripPad + kBtn / 2.0); };
        auto word = [&](const Picker& q, const std::string& role) {
            std::vector<uint32_t> w;
            role_words(q.exp(), w);
            for (size_t r = 0; r < q.exp().roles.size(); ++r)
                if (q.exp().roles[r].name == role) return hex_of(Rgb{uint8_t(w[r] >> 16), uint8_t(w[r] >> 8), uint8_t(w[r])});
            return std::string("(no role ") + role + ")";
        };
        auto quartet = [&](const Picker& q) {
            return word(q, "bevel_hilight") + " / " + word(q, "bevel_light") + " / " + word(q, "bevel_shadow") + " / " +
                   word(q, "bevel_dkshadow");
        };
        const int n_themes = int(ex.themes.size());
        size_t cmin = 1000, cmax = 0;
        for (const Theme& t : ex.themes) { cmin = std::min(cmin, t.colours.size()); cmax = std::max(cmax, t.colours.size()); }
        // the count is the data's (the export checks themes.json against the bundled theme files); the
        // program's own family closes the table, `warptempo` first, the architect's colour-picker presets
        // (`warptempo-preset-<n>`, 2026-10-04) after it
        const int wt = theme_of(ex, "warptempo");
        bool wt_tail = wt > 0 && ex.themes[size_t(wt - 1)].key.rfind("warptempo", 0) != 0;
        for (int t = wt + 1; t < n_themes; ++t)
            wt_tail = wt_tail && ex.themes[size_t(t)].key.rfind("warptempo-preset-", 0) == 0;
        check(n_themes > 0 && ex.themes[0].key == "windows-brick" && theme_of(ex, "windows-95-standard") >= 0 && wt_tail,
              "themes.json lists the product's " + std::to_string(n_themes) + " themes in the catalog's order, the "
                  "program's own family last, its presets after warptempo (" + std::to_string(cmin) + ".." +
                  std::to_string(cmax) + " colours each)");

        Picker p = launch();
        const Rgb ink0 = p.element(ink).cs.rgb;
        // every element the presets below never touch: its colour now, to stay so with an empty history
        auto untouched = [&](const Picker& q) {
            bool ok = true;
            for (int e = 0; e < int(q.exp().elements.size()); ++e)
                if (e != chrome && e != ink)
                    ok = ok && q.element(e).cs.rgb == ex.elements[size_t(e)].colour && q.element(e).hist.picks.empty();
            return ok;
        };
        tap(p, 1700, 700);
        plus(p, 3);                                   // the chrome edited
        const ColourState c1 = p.colour();
        presets_btn(p);
        check(p.presets_open() && lines_of(work + "/picks.txt") == 1 &&
                  slurp(work + "/picks.txt").find(" chrome " + hex_of(c1.rgb) + " hsv ") != std::string::npos &&
                  p.old() == c1.rgb && !p.edited() && p.save_label() == "Save as Preset 1",
              "opening the pop-up commits the edited chrome first (OLD now it); its first line Save as Preset 1");
        p.paint(frame);
        png(frame, work + "/frame_presets_open.png");
        tap(p, save_x, save_y);
        check(!p.presets_open() && p.open() && p.presets().size() == 1 && p.presets()[0].number == 1 &&
                  slurp(work + "/presets.json").find("\"number\": 1") != std::string::npos &&
                  p.presets()[0].colours.at("chrome").rgb == c1.rgb && p.presets()[0].colours.size() == ex.elements.size() &&
                  p.presets()[0].colours.count("warp_flag") && p.presets()[0].colours.count("removed_flag_selected"),
              "Save writes Preset 1 (every element's colour and view) and closes the pop-up; the panel stays open");
        choose_el(p, ink);
        plus(p, 3);
        const Rgb ink1 = p.colour().rgb;
        choose_el(p, chrome);                         // commits the ink
        plus(p, 4);
        const ColourState c2 = p.colour();
        presets_btn(p);
        check(p.save_label() == "Save as Preset 2" && lines_of(work + "/picks.txt") == 3, "the second save's line: Save as Preset 2");
        tap(p, save_x, save_y);
        check(p.presets().size() == 2 && p.presets()[1].number == 2 && p.presets()[1].colours.at("ink").rgb == ink1 &&
                  p.presets()[1].colours.at("chrome").rgb == c2.rgb, "Preset 2 holds the ink and chrome edited since");
        presets_btn(p);
        check(lines_of(work + "/picks.txt") == 3, "opening the pop-up on an unedited colour commits nothing");
        tap(p, save_x, item_y(p, 0));                 // Preset 1
        const std::string after_load = slurp(work + "/picks.txt");
        check(!p.presets_open() && p.open() && lines_of(work + "/picks.txt") == 5 && p.colour().rgb == c1.rgb &&
                  same_view(p.colour(), c1) && p.old() == c1.rgb && p.element(ink).cs.rgb == ink0 &&
                  after_load.find(" chrome " + hex_of(c1.rgb) + " hsv ", after_load.size() - 200) != std::string::npos &&
                  untouched(p),
              "loading Preset 1: the chrome and the ink each one committed pick (5 lines), every other element none; OLD "
              "the loaded chrome, its view exact");
        {
            std::map<std::string, Pick> st;
            std::map<std::string, int> ent;
            std::string act;
            check(state_load(work + "/state.json", st, ent, act, g_theme, g_model, err) && st["chrome"].rgb == c1.rgb &&
                      st["ink"].rgb == ink0 && ent["chrome"] == 3 && ent["ink"] == 2, "state.json follows the load");
        }
        tap(p, back_x, hist_y);
        check(p.colour().rgb == c2.rgb && same_view(p.colour(), c2), "BACK on the chrome returns to where he was (its Preset 2 colour)");
        choose_el(p, ink);
        tap(p, back_x, hist_y);
        check(p.colour().rgb == ink1, "and BACK on the ink to its colour before the load");
        tap(p, 1700, 700);
        Picker r = launch();
        check(r.presets().size() == 2 && r.presets()[0].number == 1 && r.presets()[1].number == 2 &&
                  r.save_label() == "Save as Preset 3" && r.presets()[1].colours.at("chrome").x[0] == c2.h &&
                  r.presets()[1].colours.at("chrome").x[1] == c2.s && r.presets()[1].colours.at("chrome").x[2] == c2.v,
              "a relaunch keeps the presets, their views exact; the next is Preset 3");
        // a scroll that acts on nothing, a tap outside
        tap(r, 1700, 700);
        presets_btn(r);
        const size_t lines0 = lines_of(work + "/picks.txt");
        const std::string pre0 = slurp(work + "/presets.json");
        const double y0 = item_y(r, 0);
        r.press(save_x, y0);
        r.move(save_x, y0 - 300);
        const int scrolled = r.pop_scroll();
        r.move(save_x, y0);
        r.release(save_x, y0);
        check(scrolled == 300 - kSlop && r.pop_scroll() == 0 && r.presets_open() && lines_of(work + "/picks.txt") == lines0,
              "a drag on Preset 1 scrolls the list (" + std::to_string(scrolled) + " px) and, lifted back on it, loads nothing");
        tap(r, 2200, 1300);
        check(!r.presets_open() && r.open() && lines_of(work + "/picks.txt") == lines0 && slurp(work + "/presets.json") == pre0,
              "a tap outside the pop-up closes it alone; nothing changes");
        // the pop-up scrolled into the themes, then a theme opened
        presets_btn(r);
        item_y(r, theme_item(r, "windows-95-standard"));
        r.paint(frame);
        png(frame, work + "/frame_presets_themes.png");
        tap(r, 2200, 1300);
        choose_el(r, chrome);                         // the relaunch came back on the ink, the element he left
        const Rgb chrome_r = r.element(chrome).cs.rgb;
        open_theme(r, "windows-95-standard");
        const int w95 = theme_of(r.exp(), "windows-95-standard");
        check(!r.presets_open() && r.theme_open() == w95 && r.colour().rgb == chrome_r &&
                  slurp(work + "/state.json").find("\"theme\": \"windows-95-standard\"") != std::string::npos,
              "a tap on Windows 95 Standard opens its strip (no colour changes; state.json names it)");
        r.paint(frame);
        png(frame, work + "/frame_strip_open.png");
        strip_tap(r, 0);                              // the ground, #C0C0C0
        check(r.colour().rgb == (Rgb{0xC0, 0xC0, 0xC0}) && r.edited() && r.old() == chrome_r &&
                  quartet(r) == "#FFFFFF / #DFDFDF / #808080 / #000000",
              "adopting its ground into the chrome is an edit, and gives Windows 95 Standard's own quartet: " + quartet(r));
        r.paint(frame);
        png(frame, work + "/frame_strip_adopted.png");
        tap(r, ppx + kColX + 50, ppy + kSwatchY0 + 50);
        check(r.colour().rgb == chrome_r, "OLD reverts the adoption");
        strip_tap(r, 0);
        const size_t lines1 = lines_of(work + "/picks.txt");
        choose_el(r, canvas);
        check(r.theme_open() == w95 && lines_of(work + "/picks.txt") == lines1 + 1 &&
                  slurp(work + "/picks.txt").find(" chrome #C0C0C0 hsv ") != std::string::npos,
              "the switch to the canvas commits the adopted chrome; the strip stays open");
        const Rgb adopt = r.exp().themes[size_t(w95)].colours[3].rgb;
        strip_tap(r, 3);
        check(r.colour().rgb == adopt && r.edited(), "a swatch adopted into the canvas: " + hex_of(adopt));
        tap(r, 1700, 700);
        Picker s = launch();
        check(s.theme_open() == w95 && s.active() == canvas && s.colour().rgb == adopt && s.element(chrome).cs.rgb == (Rgb{0xC0, 0xC0, 0xC0}),
              "a relaunch keeps the strip open on Windows 95 Standard, the canvas's adoption saved by the close");
        // a tinted theme: its ground's lines are the rule's, never its own recorded relief
        tap(s, 1700, 700);
        choose_el(s, chrome);
        open_theme(s, "windows-brick");
        strip_tap(s, 0);
        const Rgb brick{0xC2, 0xBF, 0xA5};
        check(s.colour().rgb == brick && word(s, "bevel_hilight") == hex_of(scale_rgb(brick, 255, 192)) &&
                  word(s, "bevel_light") == hex_of(scale_rgb(brick, 223, 192)) &&
                  word(s, "bevel_shadow") == hex_of(scale_rgb(brick, 128, 192)) && word(s, "bevel_hilight") != "#E1E0D2" &&
                  word(s, "bevel_shadow") != "#8D8961",
              "Brick's ground #C2BFA5 gives the rule's lines " + quartet(s) + ", not its own #E1E0D2 / #C2BFA5 / #8D8961 / #000000");
        // the strip's own scroll: a drag that adopts nothing
        open_theme(s, "cde-alpine");
        const Rgb before = s.colour().rgb;
        const double sy0 = s.strip_row_y(2) + 10;
        s.press(s.strip_x() + 60, sy0);
        for (int k = 1; k <= 20; ++k) s.move(s.strip_x() + 60, sy0 - 20 * k);
        s.release(s.strip_x() + 60, sy0 - 400);
        check(s.strip_scroll() == 400 - kSlop && s.colour().rgb == before,
              "CDE Alpine's " + std::to_string(s.exp().themes[size_t(theme_of(s.exp(), "cde-alpine"))].colours.size()) +
                  " colours: a drag scrolls the strip (" + std::to_string(s.strip_scroll()) + " px) and adopts nothing");
        s.paint(frame);
        png(frame, work + "/frame_strip_scrolled.png");
        check(slurp(work + "/picks.txt").find(" chrome #C2BFA5 hsv ") != std::string::npos,
              "(the Brick ground, adopted, was committed by the pop-up's opening, as any edit)");
        const Theme& alp = s.exp().themes[size_t(theme_of(s.exp(), "cde-alpine"))];
        int vis = 0;
        while (s.strip_row_y(vis) < s.strip_list_y0()) ++vis;
        strip_tap(s, vis);
        const Rgb alp_c = alp.colours[size_t(vis)].rgb;
        check(s.colour().rgb == alp_c && s.edited(), "a swatch tapped on the scrolled strip adopts its colour " + hex_of(alp_c));
        strip_close(s);
        {
            std::map<std::string, Pick> st;
            std::map<std::string, int> ent;
            std::string act;
            check(s.theme_open() == -1 && state_load(work + "/state.json", st, ent, act, g_theme, g_model, err) && g_theme.empty() &&
                      st["chrome"].rgb == brick && s.edited() && s.colour().rgb == alp_c,
                  "the close control closes the strip (state.json drops it, and holds the chrome's last saved colour, never "
                  "the unsaved adoption, which stays an edit)");
        }
        tap(s, 1700, 700);
        Picker u = launch();
        check(u.theme_open() == -1 && u.colour().rgb == alp_c, "a relaunch after the close: no strip; the adoption saved by the close");
        // a malformed presets.json fails the load
        put(work + "/presets.json", "{\"presets\": [{\"number\": 2, \"saved\": \"x\", \"colours\": {\"chrome\": \"#212533\"}, "
                                    "\"hsv\": {\"chrome\": [227, 0.36, 0.21]}}]}\n");
        std::string why;
        const bool refused_view = load_fails(why);
        check(refused_view, "a presets.json view that does not give its hex fails the load: " + why);
        // A PRESET SAVED BEFORE THE FLAGS ROUND (three elements) loads, and leaves every other element as it is
        fresh();
        put(work + "/presets.json",
            "{\n \"presets\": [\n  {\"number\": 1, \"saved\": \"2026-10-04T08:30:00-04:00\", \"colours\": {\"canvas\": "
            "\"#0D0D0D\", \"chrome\": \"#4C666D\", \"ink\": \"#D2E8DF\"},\n   \"hsv\": {\"canvas\": [176.03043599605004, "
            "1.7359654195688278e-16, 0.05], \"chrome\": [193.0393034140969, 0.3063294442914831, 0.4291970677312775], "
            "\"ink\": [155.00037250875818, 0.09285947824889867, 0.9082819956864905]}}\n ]\n}\n");
        Picker o = launch();
        tap(o, 1700, 700);
        choose_el(o, wflag);
        plus(o, 4);
        const Rgb ufo = o.colour().rgb;
        choose_el(o, wsel);                           // commits the warp face; the selected one stays the manifest's
        const size_t lines_o = lines_of(work + "/picks.txt");
        presets_btn(o);                               // commits the selected face (its empty history)
        const size_t lines_p = lines_of(work + "/picks.txt");
        check(o.presets().size() == 1 && o.presets()[0].colours.size() == 3 && o.save_label() == "Save as Preset 2",
              "a three-element presets.json (written before the flags round) loads: Preset 1, the next Preset 2");
        tap(o, save_x, item_y(o, 0));
        const std::string tail = slurp(work + "/picks.txt");
        bool rest = true;                             // every element the preset does not name and the session never touched
        for (int e = 0; e < n_el; ++e)
            if (e != chrome && e != canvas && e != ink && e != wflag && e != wsel)
                rest = rest && o.element(e).cs.rgb == ex.elements[size_t(e)].colour && o.element(e).hist.picks.empty() &&
                       occurrences(tail, " " + ex.elements[size_t(e)].key + " ") == 0;
        check(lines_of(work + "/picks.txt") == lines_p + 3 && lines_p == lines_o + 1 &&
                  o.element(chrome).cs.rgb == (Rgb{0x4C, 0x66, 0x6D}) && o.element(canvas).cs.rgb == (Rgb{0x0D, 0x0D, 0x0D}) &&
                  o.element(ink).cs.rgb == (Rgb{0xD2, 0xE8, 0xDF}) && o.element(ink).cs.v == 0.9082819956864905 &&
                  o.element(wflag).cs.rgb == ufo && o.element(wflag).hist.picks.size() == 1 &&
                  o.element(wsel).cs.rgb == ex.elements[size_t(wsel)].colour && o.element(wsel).hist.picks.size() == 1 &&
                  o.active() == wsel && o.colour().rgb == ex.elements[size_t(wsel)].colour &&
                  occurrences(tail, " warp_flag ") == 1 && occurrences(tail, " warp_flag_selected ") == 1 && rest,
              "loading it commits the chrome, the canvas and the ink (3 lines, their views exact) and leaves the warp "
              "flags, the other flag kinds, both playhead elements, the Label, the Selection and the Selected Text as "
              "they were, no pick for any");
        fresh();
    }

    // ---------------------------------------------------------------- THE MODEL SWITCH: HSL and LCh on the panel
    if (nine) {
        fresh();
        const double mb_x = ppx + kModelX0 + 60, mb_y = ppy + kModelY + kModelH / 2.0;
        auto model_tap = [&](Picker& q, Model m) {
            tap(q, mb_x, mb_y);
            tap(q, mb_x, ppy + kModelListY + int(m) * kChooserRowH + kChooserRowH / 2.0);
        };
        auto shown = [](const ColourState& c, double a, double b, double d) {
            const std::array<double, 3> x = c.view(c.model);
            return x[0] == a && x[1] == b && x[2] == d;
        };
        auto same_all = [](const ColourState& a, const ColourState& b) {
            return a.rgb == b.rgb && a.model == b.model && a.view(a.model) == b.view(b.model) && a.dialled == b.dialled &&
                   a.view(a.dialled) == b.view(b.dialled);
        };
        auto readouts = [](const ColourState& c) {
            return row_readout(c, 0) + " / " + row_readout(c, 1) + " / " + row_readout(c, 2);
        };
        auto last_line = [&]() {
            const std::string t = slurp(work + "/picks.txt");
            const size_t b = t.rfind('\n', t.size() - 2);
            return t.substr(b == std::string::npos ? 0 : b + 1, t.size() - (b == std::string::npos ? 0 : b + 1) - 1);
        };
        Picker p = launch();
        tap(p, 1700, 700);
        check(p.model() == Model::Hsv && !p.models_open(), "the model switch shows HSV at a fresh start");
        tap(p, mb_x, mb_y);
        check(p.models_open(), "a tap on the model switch opens its list (HSV, HSL, LCh)");
        p.paint(frame);
        png(frame, work + "/frame_model_list_open.png");
        tap(p, ppx + 600, ppy + 1000);
        check(!p.models_open() && p.open() && p.model() == Model::Hsv && !std::ifstream(work + "/state.json"),
              "a tap outside the list closes it and changes nothing (no file written)");
        tap(p, mb_x, mb_y);
        p.press(mb_x, ppy + kModelListY + kChooserRowH / 2.0 + kChooserRowH);       // pressed on HSL, lifted on LCh
        p.release(mb_x, ppy + kModelListY + kChooserRowH / 2.0 + 2 * kChooserRowH);
        check(p.models_open() && p.model() == Model::Hsv, "a press on one model lifted on another picks nothing (the list stays)");
        tap(p, mb_x, ppy + kModelListY + kChooserRowH / 2.0 + kChooserRowH);
        const Rgb before = p.colour().rgb;
        check(!p.models_open() && p.model() == Model::Hsl && p.colour().rgb == before && p.old() == before &&
                  slurp(work + "/state.json").find("\"model\": \"hsl\"") != std::string::npos &&
                  !std::ifstream(work + "/picks.txt"),
              "HSL chosen: the colour unchanged, state.json names the model, nothing committed");
        // HSL dialled by its tracks and − / +
        track(p, 0, 200.0 / 360); plus(p, 0); minus(p, 0);
        track(p, 1, 0.6); plus(p, 1); minus(p, 1);
        track(p, 2, 0.4); plus(p, 2);
        const ColourState h1 = p.colour();
        check(shown(h1, 200, 0.6, 0.41) && h1.rgb == rgb_of_hsl(200, 0.6, 0.41) && readouts(h1) == "200.0 / 60.0 / 41.0" &&
                  h1.dialled == Model::Hsl && h1.ring[0] == 200,
              "HSL's tracks and − / + set H 200, S 60, L 41 exactly (" + hex_of(h1.rgb) + "), the ring at its hue");
        p.paint(frame);
        png(frame, work + "/frame_model_hsl_open.png");
        tap(p, 1700, 700);
        check(last_line().find(" chrome " + hex_of(h1.rgb) + " hsl " + view_number(200) + " 0.6 0.41") != std::string::npos,
              "the close commits the HSL view: " + last_line());
        {
            Picker r = launch();
            check(r.model() == Model::Hsl && same_all(r.colour(), h1) && readouts(r.colour()) == "200.0 / 60.0 / 41.0",
                  "a relaunch shows HSL and the view exactly as dialled");
        }
        tap(p, 1700, 700);
        plus(p, 2);
        const ColourState h2 = p.colour();
        tap(p, 1700, 700);
        tap(p, 1700, 700);
        tap(p, back_x, hist_y);
        const ColourState b1 = p.colour();
        tap(p, fwd_x, hist_y);
        check(same_all(b1, h1) && same_all(p.colour(), h2) && shown(h2, 200, 0.6, 0.42),
              "BACK and FORWARD restore each HSL view exactly");
        // the switch to HSV re-derives HSV from the bytes; back to HSL, unchanged, the dialled view
        model_tap(p, Model::Hsv);
        const ColourState hv = p.colour();
        model_tap(p, Model::Hsl);
        check(hv.model == Model::Hsv && hv.rgb == h2.rgb && rgb_of_hsv(hv.h, hv.s, hv.v) == h2.rgb && !p.edited() &&
                  same_all(p.colour(), h2),
              "HSV shows the bytes' own view (" + readouts(hv) + "); back to HSL the dialled view, no edit");
        // the ring and the triangle in HSL: the triangle keeps the hue exactly, the ring sets it
        p.press(ppx + kPad + kROut, ppy + kPad + kROut);
        p.move(ppx + kPad + kROut + 30, ppy + kPad + kROut + 20);
        p.release(ppx + kPad + kROut + 30, ppy + kPad + kROut + 20);
        const ColourState tr = p.colour();
        p.press(ppx + kPad + kROut + 255, ppy + kPad + kROut);
        p.move(ppx + kPad + kROut, ppy + kPad + kROut - 255);
        p.release(ppx + kPad + kROut, ppy + kPad + kROut - 255);
        check(tr.hsl[0] == 200 && tr.rgb == rgb_of_hsl(tr.hsl[0], tr.hsl[1], tr.hsl[2]) && tr.dialled == Model::Hsl &&
                  std::abs(p.colour().hsl[0] - 90) < 1e-9 && p.colour().ring[0] == p.colour().hsl[0],
              "in HSL the triangle keeps the hue 200 exactly (" + readouts(tr) + "), the ring sets it (90)");
        track(p, 1, 0);
        const ColourState grey = p.colour();
        track(p, 1, 0.5);
        check(grey.rgb.r == grey.rgb.g && grey.rgb.g == grey.rgb.b && grey.hsl[0] == p.colour().hsl[0] &&
                  std::abs(p.colour().hsl[0] - 90) < 1e-9, "HSL's S to 0 gives a grey and keeps the hue");
        tap(p, ppx + kColX + 50, ppy + kSwatchY0 + 50);   // OLD
        check(same_all(p.colour(), h2), "OLD restores the HSL view the panel opened with");

        // LCh: the tracks, the gamut stop
        model_tap(p, Model::Lch);
        check(p.model() == Model::Lch && p.colour().rgb == h2.rgb && p.colour().lch[1] > 0, "LCh chosen: " + readouts(p.colour()));
        track(p, 1, 0);
        track(p, 0, 0.6); plus(p, 0); minus(p, 0);
        track(p, 2, 250.0 / 360); plus(p, 2); minus(p, 2);
        track(p, 1, 20.0 / kChromaMax); plus(p, 1); minus(p, 1);
        check(shown(p.colour(), 60, 20, 250) && p.colour().dialled == Model::Lch, "LCh's tracks set L 60, C 20, h 250 exactly");
        {
            const double x0 = ppx + kTrackX + 20.0 / kChromaMax * (kTrackL - 1), y = ppy + row_y(1) + 30;
            p.press(x0, y);
            for (int k = 1; k <= 40; ++k) p.move(x0 + k * (kTrackL - (x0 - ppx - kTrackX)) / 40.0, y);
            p.release(ppx + kTrackX + kTrackL + 10, y);
        }
        const ColourState cs = p.colour();
        const double cstop = cs.lch[1];
        const Unit u = unit_of_lch(60, cstop, 250);
        char stop[240];
        std::snprintf(stop, sizeof stop, "a drag on C to the track's end stops at the gamut, C %.9f (%s), inside; C + 1e-8 is "
                      "outside; the bytes reached by rounding alone", cstop, hex_of(cs.rgb).c_str());
        check(cstop > 20 && cstop < kChromaMax && lch_in_gamut(60, cstop, 250) && !lch_in_gamut(60, cstop + 1e-8, 250) &&
                  unit_in_gamut(u) && rgb_of_unit(u) == cs.rgb && cs.lch[0] == 60 && cs.lch[2] == 250,
              stop);
        plus(p, 1);
        check(same_all(p.colour(), cs), "C + at the gamut's edge moves nothing");
        p.paint(frame);
        png(frame, work + "/frame_model_lch_open.png");
        {   // the C track's out-of-gamut stretch: the neutral, its in-gamut part the colour
            const int ty = int(ppy) + row_y(1) + kRowH / 2;
            auto at = [&](double c) {
                const long k = long(ty) * W + long(ppx + kTrackX + std::lround(c / kChromaMax * (kTrackL - 1)));
                return frame_word(frame, k, W);
            };
            const Rgb lch_g = rgb_of_unit(unit_of_lch(60, 0, 250));
            check(at(155) == word_of(Rgb{0x19, 0x19, 0x19}) && at(cstop + 5) == word_of(Rgb{0x19, 0x19, 0x19}) &&
                      at(0) == word_of(lch_g) && at(cstop - 5) != word_of(Rgb{0x19, 0x19, 0x19}),
                  "the C track paints its out-of-gamut stretch (C > " + std::to_string(int(cstop)) +
                      ") in the flat neutral #191919, the rest in colour (C 0 the grey " + hex_of(lch_g) + ")");
        }
        minus(p, 1);
        const double cm = std::nearbyint(cstop) - 1;
        check(shown(p.colour(), 60, cm, 250), "C − from the edge: C " + std::to_string(int(cm)) + " exactly");
        {   // the h track dragged across the gamut's edges (C toward 70): every stop inside, at the edge or at the pen
            track(p, 1, 70.0 / kChromaMax);
            const double y = ppy + row_y(2) + 30;
            bool ok = true;
            int stops = 0;
            p.press(ppx + kTrackX + p.colour().lch[2] / 360 * (kTrackL - 1), y);
            for (int k = 0; k <= 72 && ok; ++k) {
                const double target = (k % 2 ? 360 - k * 5 : k * 5);
                p.move(ppx + kTrackX + target / 360 * (kTrackL - 1), y);
                const ColourState& c = p.colour();
                const double hh = c.lch[2];
                const double t = std::max(0.0, std::min(360.0, (ppx + kTrackX + target / 360 * (kTrackL - 1) - (ppx + kTrackX)) /
                                                                    (kTrackL - 1) * 360));
                ok = lch_in_gamut(c.lch[0], c.lch[1], hh) && c.rgb == rgb_of_unit(unit_of_lch(c.lch[0], c.lch[1], hh));
                if (hh != t) {
                    ++stops;
                    const double dir = t > hh ? 1 : -1;
                    ok = ok && !lch_in_gamut(c.lch[0], c.lch[1], hh + dir * 1e-6);
                }
            }
            p.release(ppx + kTrackX, y);
            check(ok && stops > 0, "a drag on h across the gamut at C " + row_readout(p.colour(), 1) +
                                   " (C 70 stopped there): every frame inside, the bytes unclipped, " + std::to_string(stops) +
                                   " frames stopped at an edge");
        }
        // LCh's hue through C 0
        track(p, 1, 0);
        track(p, 0, 0.6); plus(p, 0); minus(p, 0);
        track(p, 2, 250.0 / 360); plus(p, 2); minus(p, 2);
        track(p, 1, 30.0 / kChromaMax); plus(p, 1); minus(p, 1);
        const ColourState l1 = p.colour();
        track(p, 1, 0);
        const bool keeps = p.colour().lch[1] == 0 && p.colour().lch[2] == 250 && p.colour().rgb.r == p.colour().rgb.g &&
                           p.colour().rgb.g == p.colour().rgb.b;
        track(p, 1, 30.0 / kChromaMax); plus(p, 1); minus(p, 1);
        check(keeps && same_all(p.colour(), l1), "LCh's C to 0 is a grey keeping h 250; C back to 30 the colour again exactly");
        tap(p, 1700, 700);
        check(last_line().find(" chrome " + hex_of(l1.rgb) + " lch " + view_number(60) + " " + view_number(30) + " " + view_number(250)) != std::string::npos,
              "the close commits the LCh view: " + last_line());
        {
            Picker r = launch();
            check(r.model() == Model::Lch && same_all(r.colour(), l1), "a relaunch shows LCh and the view exactly as dialled");
            tap(r, 1700, 700);
            tap(r, back_x, hist_y);   // the HSL pick h2, shown in LCh: re-derived
            const ColourState rb = r.colour();
            model_tap(r, Model::Hsl);
            check(rb.model == Model::Lch && rb.rgb == h2.rgb && same_all(r.colour(), h2) && !r.edited(),
                  "BACK in LCh to the HSL pick shows its bytes' LCh (" + readouts(rb) + "); HSL then shows its view exactly");
            model_tap(r, Model::Lch);
            tap(r, fwd_x, hist_y);
            check(same_all(r.colour(), l1), "FORWARD restores the LCh view exactly");
            tap(r, 1700, 700);
        }
        // the ring and the triangle in LCh: the pen's HSV gives the bytes, LCh reads them
        {
            Picker r = launch();
            tap(r, 1700, 700);
            r.press(ppx + kPad + kROut, ppy + kPad + kROut);
            r.move(ppx + kPad + kROut - 40, ppy + kPad + kROut + 10);
            r.release(ppx + kPad + kROut - 40, ppy + kPad + kROut + 10);
            const ColourState& c = r.colour();
            double x[3];
            lch_of_unit(Unit{c.rgb.r / 255.0, c.rgb.g / 255.0, c.rgb.b / 255.0}, x);
            check(c.rgb == rgb_of_hsv(c.ring[0], c.ring[1], c.ring[2]) && c.lch[0] == x[0] && c.lch[1] == x[1] &&
                      c.dialled == Model::Lch,
                  "in LCh the triangle's HSV gives the bytes and LCh reads them (" + readouts(c) + ")");
            r.discard_if_open();
            check(same_all(r.colour(), l1), "leaving the app restores the LCh view of the panel's opening");
        }
        // a preset over three models
        {
            Picker r = launch();
            tap(r, 1700, 700);
            tap(r, name_x, name_y);
            row_tap(r, ink);
            model_tap(r, Model::Hsl);
            track(r, 0, 30.0 / 360); plus(r, 0); minus(r, 0);
            const ColourState ink_l = r.colour();
            tap(r, name_x, name_y);
            row_tap(r, canvas);
            model_tap(r, Model::Hsv);
            plus(r, 2);
            const ColourState can_v = r.colour();
            tap(r, ppx + kPresetsX + 60, name_y);
            tap(r, ppx + kPopX0 + 200, ppy + kPopY0 + kPopRowH / 2.0);
            const std::string pj = slurp(work + "/presets.json");
            check(pj.find("\"lch\": {\"chrome\": [" + view_number(60) + ", " + view_number(30) + ", " + view_number(250) + "]") != std::string::npos &&
                      pj.find("\"hsl\": {\"ink\": [" + view_number(30) + ", ") != std::string::npos && pj.find("\"hsv\": {") != std::string::npos &&
                      pj.find("\"canvas\": [", pj.find("\"hsv\": {")) < pj.find("}", pj.find("\"hsv\": {")),
                  "a preset keeps each element's view in its model's map (chrome lch, ink hsl, canvas hsv)");
            tap(r, ppx + kColX + 50, ppy + kSwatchY0 + 50);
            plus(r, 0);                                   // the canvas moved
            tap(r, name_x, name_y);
            row_tap(r, chrome);                           // the canvas committed; the chrome
            plus(r, 3);                                   // moved by its R
            tap(r, 1700, 700);
            Picker s = launch();
            check(s.presets().size() == 1 && s.presets()[0].colours.at("chrome").model == Model::Lch &&
                      s.presets()[0].colours.at("ink").model == Model::Hsl, "a relaunch reads the preset's three models");
            tap(s, 1700, 700);
            tap(s, ppx + kPresetsX + 60, name_y);
            tap(s, ppx + kPopX0 + 200, ppy + kPopListY0 + kPopRowH / 2.0);
            check(s.element(chrome).cs.rgb == l1.rgb && s.element(chrome).cs.view(Model::Lch) == l1.view(Model::Lch) &&
                      s.element(chrome).cs.dialled == Model::Lch && s.element(ink).cs.rgb == ink_l.rgb &&
                      s.element(ink).cs.view(Model::Hsl) == ink_l.view(Model::Hsl) && s.element(canvas).cs.rgb == can_v.rgb &&
                      s.element(canvas).cs.view(Model::Hsv) == can_v.view(Model::Hsv) && s.model() == Model::Hsv,
                  "loading it restores every element's view in its own model exactly, the panel's model kept (HSV)");
            tap(s, 1700, 700);
        }
        // a grey stored in HSV, shown in HSL: the hue it was saved under, not 0
        put(work + "/picks.txt", "2026-10-04T12:00:00-04:00 chrome #808080 hsv 123 0 " + view_number(128 / 255.0) + "\n");
        put(work + "/state.json", "{\"active\": \"chrome\", \"model\": \"hsl\", \"colours\": {\"chrome\": \"#808080\"}, "
                                  "\"hsv\": {\"chrome\": [123, 0, " + view_number(128 / 255.0) + "]}, \"entry\": {\"chrome\": 1}}\n");
        {
            Picker r = launch();
            check(r.model() == Model::Hsl && r.colour().hsl[0] == 123 && r.colour().hsl[1] == 0 && !r.edited() &&
                      r.colour().dialled == Model::Hsv,
                  "a grey saved in HSV and shown in HSL keeps the hue it was saved under (123)");
        }
        // the formats' refusals
        auto refused = [&](const std::string& picks_text, const std::string& state_text, const std::string& what) {
            put(work + "/picks.txt", picks_text);
            if (state_text.empty()) std::remove((work + "/state.json").c_str());
            else put(work + "/state.json", state_text);
            std::string why;
            const bool failed = load_fails(why);
            check(failed, what + ": " + why);
        };
        refused("2026-10-04T05:00:00-04:00 chrome #808080 lch 50 170 0\n", "", "a picks.txt LCh C past 160 fails the load");
        refused("2026-10-04T05:00:00-04:00 chrome #FF0000 lch 54.966557096508478 140 45.205497846029594\n", "",
                "a picks.txt LCh view outside the gamut fails the load");
        refused("2026-10-04T05:00:00-04:00 chrome #FF0000 hsl 0 1 0.6\n", "", "a picks.txt HSL view not giving its hex fails the load");
        refused("", "{\"model\": \"rgb\", \"colours\": {}, \"entry\": {}}", "a state.json model that is none fails the load");
        refused("", "{\"colours\": {\"chrome\": \"#FF0000\"}, \"hsv\": {\"chrome\": [0, 1, 1]}, \"hsl\": {\"chrome\": [0, 1, 0.5]}, "
                    "\"entry\": {}}", "a state.json element with two views fails the load");
        fresh();
    }

    // ---------------------------------------------------------------- the tablet's files of 2026-10-04 load unchanged
    // the elements of the rounds after the ink's (the flags', the playhead's, the label's, the open flag's, 2026-10-04,
    // and the flag kinds', 2026-10-05), absent from every file the tablet wrote before them: the manifest's colours, no
    // history
    auto flags_fresh = [&](const Picker& q) {   // every element but the chrome, the canvas and the ink
        bool ok = n_el > 3;
        for (int e = 3; e < n_el; ++e)
            ok = ok && q.element(e).cs.rgb == ex.elements[size_t(e)].colour && q.element(e).hist.cursor == -1;
        return ok;
    };
    if (!today.empty() && chrome == 0 && canvas == 1 && ink == 2) {
        fresh();
        const std::string picks0 = slurp(today + "/picks.txt"), state0 = slurp(today + "/state.json");
        put(work + "/picks.txt", picks0);
        put(work + "/state.json", state0);
        Picker t = launch();
        const auto& C = t.element(chrome).cs;
        const auto& V = t.element(canvas).cs;
        const auto& I = t.element(ink).cs;
        check(std::count(picks0.begin(), picks0.end(), '\n') == 110 && t.active() == canvas &&
                  C.rgb == (Rgb{0x63, 0x63, 0x9C}) && C.h == 240 && C.s == 0.36538461538461536 && C.v == 0.611764705882353 &&
                  V.rgb == (Rgb{0x63, 0x63, 0x9C}) && I.rgb == (Rgb{0xBB, 0xCE, 0xF2}) && I.h == 218.50048182819384 &&
                  I.s == 0.23 && I.v == 0.95 && t.element(chrome).hist.picks.size() == 11 &&
                  t.element(chrome).hist.cursor == 8 && t.element(canvas).hist.picks.size() == 22 &&
                  t.element(canvas).hist.cursor == 21 && t.element(ink).hist.picks.size() == 77 &&
                  t.element(ink).hist.cursor == 76 && t.theme_open() == -1 && t.presets().empty() && flags_fresh(t) &&
                  t.model() == Model::Hsv,
              "the tablet's picks.txt (110 lines) and state.json load unchanged: the canvas active, chrome 9 of 11, canvas "
              "22 of 22, ink 77 of 77, every view exact; every other element at the manifest's colours, 0 of "
              "0; HSV shown (no model)");
        tap(t, 1700, 700);
        tap(t, ppx + kPresetsX + 60, ppy + kNameY + kNameH / 2.0);
        tap(t, ppx + kPopX0 + 200, ppy + kPopY0 + kPopRowH / 2.0);
        tap(t, ppx + kPresetsX + 60, ppy + kNameY + kNameH / 2.0);
        tap(t, ppx + kPopX0 + 200, ppy + kPopListY0 + kPopRowH / 2.0);   // Preset 1, at the top of the list
        tap(t, 1700, 700);
        check(slurp(work + "/picks.txt") == picks0 && t.presets().size() == 1,
              "a preset saved and loaded over them appends nothing (every element unchanged): picks.txt byte for byte");
        fresh();
    }

    // ---------------------------------------------------------------- the tablet's files at the presets build's install
    if (!late.empty() && chrome == 0 && canvas == 1 && ink == 2) {
        fresh();
        const std::string picks0 = slurp(late + "/picks.txt"), state0 = slurp(late + "/state.json");
        put(work + "/picks.txt", picks0);
        put(work + "/state.json", state0);
        Picker t = launch();
        const auto& C = t.element(chrome).cs;
        const auto& V = t.element(canvas).cs;
        const auto& I = t.element(ink).cs;
        check(std::count(picks0.begin(), picks0.end(), '\n') == 136 && t.active() == ink &&
                  C.rgb == (Rgb{0x4C, 0x66, 0x6D}) && C.h == 193.0393034140969 && C.s == 0.3063294442914831 &&
                  C.v == 0.4291970677312775 && V.rgb == (Rgb{0x0D, 0x0D, 0x0D}) && V.h == 176.03043599605004 &&
                  V.s == 1.7359654195688278e-16 && V.v == 0.05 && I.rgb == (Rgb{0xD2, 0xE8, 0xDF}) &&
                  I.h == 155.00037250875818 && I.s == 0.09285947824889867 && I.v == 0.9082819956864905 &&
                  t.element(chrome).hist.picks.size() == 12 && t.element(chrome).hist.cursor == 11 &&
                  t.element(canvas).hist.picks.size() == 34 && t.element(canvas).hist.cursor == 33 &&
                  t.element(ink).hist.picks.size() == 90 && t.element(ink).hist.cursor == 89 && t.theme_open() == -1 &&
                  t.presets().empty() && flags_fresh(t) && t.model() == Model::Hsv,
              "the tablet's picks.txt (136 lines) and state.json at the presets build's install load unchanged: the ink "
              "active, chrome 12 of 12, canvas 34 of 34, ink 90 of 90, every view exact; every other element at the "
              "manifest's colours, 0 of 0; HSV shown (no model)");
        tap(t, 1700, 700);
        tap(t, 1700, 700);
        check(slurp(work + "/picks.txt") == picks0, "an open and a close on the unedited ink append nothing");
        fresh();
    }

    // ---------------------------------------------------------------- the repository's presets.json reads unchanged
    if (!repo_presets.empty()) {
        std::vector<Preset> ps;
        const bool ok = presets_load(repo_presets, ps, err);
        // any model's view (each element's is saved in its own, the model switch's): a later copy of his file keeps it
        bool viewed = ok && !ps.empty();
        for (const Preset& q : ps)
            for (const auto& kv : q.colours) viewed = viewed && kv.second.has_view && view_holds(kv.second);
        check(viewed, "the repository's presets.json (the tablet's, " + std::to_string(ps.size()) +
                          " presets) reads, every colour with a view that gives its hex");
    }

    // ---------------------------------------------------------------- the repository's copy of the tablet's files
    // presets/'s picks.txt, state.json and presets.json, as the tablet holds them, over the round's own export (the
    // playhead round's install, architect 2026-10-04): they load; every element state.json names at its colour; every
    // element the files never name at the manifest's colour with an empty history; and each preset loaded sets the
    // elements it names (a commit for each whose colour or view changes, none for the rest) and leaves every element it
    // does not name at the colour it had, with no pick. Every expectation is read from the files (which elements they
    // name, the colours and histories they give, picks.txt before and after each load), never a count or an element
    // list, so a later copy of his files keeps the check
    if (!kept.empty() && !round_ex.elements.empty()) {
        fresh();
        const std::string picks0 = slurp(kept + "/picks.txt");
        put(work + "/picks.txt", picks0);
        put(work + "/state.json", slurp(kept + "/state.json"));
        put(work + "/presets.json", slurp(kept + "/presets.json"));
        std::map<std::string, Pick> st;
        std::map<std::string, int> ent;
        std::map<std::string, std::vector<Pick>> hist;
        std::vector<Preset> ps;
        std::string act, thm;
        Model mdl = Model::Hsv;
        const bool read = state_load(kept + "/state.json", st, ent, act, thm, mdl, err) &&
                          picks_load(kept + "/picks.txt", hist, err) && presets_load(kept + "/presets.json", ps, err);
        Export e = round_ex;
        Launch l;
        if (!read || !picker_load(work, e, l, err)) {
            check(false, "the repository's copy of the tablet's files loads over the round's export: " + err);
        } else {
            Picker q(std::move(e), work, std::move(l));
            const Export& rx = q.exp();
            bool named = true, unnamed = true;
            std::string fresh_keys;
            for (int k = 0; k < int(rx.elements.size()); ++k) {
                const std::string& key = rx.elements[size_t(k)].key;
                if (st.count(key)) named = named && q.element(k).cs.rgb == st[key].rgb;
                if (st.count(key) || hist.count(key)) continue;
                unnamed = unnamed && q.element(k).cs.rgb == rx.elements[size_t(k)].colour && q.element(k).hist.picks.empty();
                fresh_keys += " " + key;
            }
            check(named && unnamed && q.model() == mdl && (act.empty() || rx.elements[size_t(q.active())].key == act) &&
                      q.presets().size() == ps.size() && (thm.empty() || q.theme_open() == theme_of(rx, thm)),
                  "the repository's copy of the tablet's files (" +
                      std::to_string(std::count(picks0.begin(), picks0.end(), '\n')) + " picks.txt lines, " +
                      std::to_string(ps.size()) + " presets) loads: every element at state.json's colour, its model, "
                      "active element and theme; the elements they never name at the manifest's colours, 0 of 0:" + fresh_keys);
            // THE RENAMED KEYS IN HIS FILES (architect 2026-10-05), read from their own text rather than through the
            // readers: each colour state.json's text gives an old flag key is on every element that replaced it (unless
            // the file names that element itself), and each element's history is exactly the picks.txt lines under its
            // own key or an old key it replaced, so his old flag picks are both new faces' histories
            {
                Json raw;
                std::string jerr;
                const bool parsed = json_parse(slurp(kept + "/state.json"), raw, jerr) && raw.is_object();
                const Json* rc = parsed ? raw.get("colours") : nullptr;
                bool alias_ok = rc && rc->is_object();
                std::string said;
                for (const auto& kv : alias_ok ? rc->obj : std::vector<std::pair<std::string, Json>>{}) {
                    const std::vector<std::string>* to = renamed_key(kv.first);
                    if (!to) continue;
                    Rgb c;
                    alias_ok = alias_ok && parse_hex(kv.second.str, c);
                    for (const std::string& k : *to) {
                        const int el = element_of(rx, k);
                        alias_ok = alias_ok && el >= 0 && (rc->get(k) || q.element(el).cs.rgb == c);
                        said += " " + kv.first + " -> " + k + " " + hex_of(c) + ";";
                    }
                }
                std::map<std::string, size_t> lines_for;   // element key -> the picks.txt lines its history must hold
                std::istringstream pin(picks0);
                for (std::string pl; std::getline(pin, pl);) {
                    std::istringstream ls(pl);
                    std::string ts, key;
                    ls >> ts >> key;
                    if (const std::vector<std::string>* to = renamed_key(key))
                        for (const std::string& k : *to) ++lines_for[k];
                    else
                        ++lines_for[key];
                }
                for (int k = 0; k < int(rx.elements.size()); ++k)
                    alias_ok = alias_ok && q.element(k).hist.picks.size() == lines_for[rx.elements[size_t(k)].key];
                check(alias_ok, "his files' old flag keys, read from their text, are the new elements' colours and "
                                "histories:" + (said.empty() ? std::string(" (none named)") : said));
            }
            // each load against the state it found (his files' state, then the previous load's): every element the
            // preset names at its colour, one pick if its colour or view changed and none if not; every other element
            // at the colour it had, its history unchanged; picks.txt gaining exactly those commits, each element's
            // once with the preset's hex and model, and nothing else
            bool kept_ok = true;
            size_t committed = 0;
            for (size_t k = 0; k < ps.size(); ++k) {
                std::vector<ColourState> before;
                std::vector<size_t> lines;
                for (int x = 0; x < int(rx.elements.size()); ++x) {
                    before.push_back(q.element(x).cs);
                    lines.push_back(q.element(x).hist.picks.size());
                }
                const std::string picks_before = slurp(work + "/picks.txt");
                tap(q, 1700, 700);                    // the panel on the left, the strip (if open) right of it
                tap(q, ppx + kPresetsX + 60, name_y);
                const int want = std::max(0, int(k) * kPopRowH - 400);   // scrolled into view by a drag (acting on nothing)
                const double y0 = ppy + kPopListY0 + 500, delta = want - q.pop_scroll();
                if (delta != 0) {
                    q.press(ppx + kPopX0 + 200, y0);
                    q.move(ppx + kPopX0 + 200, y0 - delta - (delta > 0 ? kSlop : -kSlop));
                    q.release(ppx + kPopX0 + 200, y0 - delta - (delta > 0 ? kSlop : -kSlop));
                }
                tap(q, ppx + kPopX0 + 200, ppy + kPopListY0 + int(k) * kPopRowH - q.pop_scroll() + kPopRowH / 2.0);
                tap(q, 1700, 700);
                std::map<std::string, std::string> want_line;   // key -> " <hex> <model>" of its one expected commit
                for (int x = 0; x < int(rx.elements.size()); ++x) {
                    const std::string& key = rx.elements[size_t(x)].key;
                    const auto c = ps[k].colours.find(key);
                    if (c != ps[k].colours.end()) {
                        const bool changes = !before[size_t(x)].shows(c->second);
                        kept_ok = kept_ok && q.element(x).cs.shows(c->second) &&
                                  q.element(x).hist.picks.size() == lines[size_t(x)] + (changes ? 1 : 0);
                        if (changes) want_line[key] = " " + hex_of(c->second.rgb) + " " + model_word(c->second.model);
                    } else {
                        kept_ok = kept_ok && q.element(x).cs.rgb == before[size_t(x)].rgb &&
                                  q.element(x).hist.picks.size() == lines[size_t(x)];
                    }
                }
                const std::string picks_after = slurp(work + "/picks.txt");
                kept_ok = kept_ok && picks_after.compare(0, picks_before.size(), picks_before) == 0;
                std::istringstream added(picks_after.size() >= picks_before.size() ? picks_after.substr(picks_before.size())
                                                                                   : std::string());
                size_t n_added = 0;
                for (std::string line; std::getline(added, line); ++n_added) {
                    std::istringstream ls(line);
                    std::string ts, key, hex, model;
                    ls >> ts >> key >> hex >> model;
                    const auto w = want_line.find(key);
                    kept_ok = kept_ok && w != want_line.end() && w->second == " " + hex + " " + model;
                    if (w != want_line.end()) want_line.erase(w);   // each element's commit once
                }
                kept_ok = kept_ok && want_line.empty();
                committed += n_added;
            }
            check(kept_ok && !q.open(),
                  "each of his presets loaded over them sets the elements it names, a pick for each whose colour or view "
                  "changes and none for the rest, and leaves every other element as it was with no pick (" +
                      std::to_string(committed) + " picks.txt lines appended over " + std::to_string(ps.size()) +
                      " loads, each a named element's commit)");
        }
        fresh();
    }

    // ---------------------------------------------------------------- THE SWITCH'S PICTURE, byte for byte
    // the round's own export at the colours of its "all moved" references: the waveform scene, then after the chooser's
    // switch Ink -> Warp Flag the flags scene, then back, then after the switch Ink -> Playhead Head the playhead scene,
    // then after the switch Playhead Head -> Label the label scene, then after the switch Label -> Selection the
    // open_flag scene, then after the switch Selection -> Phase Reset Flag the phase_reset scene, then after the switch
    // Phase Reset Flag -> Added Flag the history scene, then after the switch Added Flag -> Outline the magnified scene,
    // each the mock tool's render
    if (!round_expects.empty()) {
        std::istringstream in(slurp(round_expects));
        std::string line;
        std::map<std::string, std::string> ppm_of;   // scene -> its all-moved reference
        std::vector<std::string> moved;
        size_t most = 0;
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string sc, ppm, t;
            std::vector<std::string> toks;
            ls >> sc >> ppm;
            while (ls >> t) toks.push_back(t);
            if (toks.size() < most) continue;
            if (toks.size() > most) {
                most = toks.size();
                ppm_of.clear();
                moved = toks;
            }
            if (toks == moved) ppm_of[sc] = ppm;
        }
        const Export& rx = round_ex;
        const int rn = int(rx.elements.size());
        const int ri = element_of(rx, "ink"), rw = element_of(rx, "warp_flag"), rh = element_of(rx, "playhead_head"),
                  rl = element_of(rx, "label"), rs = element_of(rx, "selected_fill"), rp = element_of(rx, "phase_reset_flag"),
                  ra = element_of(rx, "added_flag"), ro = element_of(rx, "waveform_outline");
        const char* scenes[] = {"waveform", "flags", "playhead", "label", "open_flag", "phase_reset", "history", "magnified"};
        bool all = moved.size() == rx.elements.size() && ri >= 0 && rw >= 0 && rh >= 0 && rl >= 0 && rs >= 0 && rp >= 0 &&
                   ra >= 0 && ro >= 0;
        for (const char* sc : scenes) all = all && ppm_of.count(sc);
        if (!all) {
            check(false, "the round's export has an all-moved reference of its eight scenes, an ink, an outline, a warp "
                         "flag, a playhead head, a label, a selection, a phase reset flag and an added flag");
        } else {
            fresh();
            std::string cols;
            for (const std::string& t : moved) {
                const size_t eq = t.find('=');
                cols += std::string(cols.empty() ? "" : ", ") + "\"" + t.substr(0, eq) + "\": \"" + t.substr(eq + 1) + "\"";
            }
            put(work + "/state.json", "{\"active\": \"ink\", \"colours\": {" + cols + "}, \"entry\": {}}\n");
            Export e = rx;
            Launch l;
            if (!picker_load(work, e, l, err)) check(false, "picker_load on the round's export: " + err);
            Picker q(std::move(e), work, std::move(l));
            std::string said;
            for (const std::string& t : moved) said += " " + t;
            auto to = [&](int el) {                   // the chooser's switch to el, the round's export's cells
                tap(q, name_x, name_y);
                tap(q, ppx + chooser_x(el, rn) + kChooserColW / 2.0, ppy + chooser_y(el, rn) + kChooserRowH / 2.0);
                return q.active() == el;
            };
            auto diff = [&](const char* sc) { return diff_ppm(q.picture(), ppm_of[sc], rx.width, rx.height); };
            const long d0 = diff("waveform");
            tap(q, 1700, 700);
            bool on = to(rw);
            const long d1 = diff("flags");
            q.paint(frame);
            png(frame, work + "/frame_switch_flags_moved.png");
            on = to(ri) && on;
            const long d2 = diff("waveform");
            on = to(rh) && on;
            const long d3 = diff("playhead");
            on = to(rl) && on;
            const long d4 = diff("label");
            on = to(rs) && on;
            const long d5 = diff("open_flag");
            q.paint(frame);
            png(frame, work + "/frame_switch_open_flag_moved.png");
            on = to(rp) && on;
            const long d6 = diff("phase_reset");
            q.paint(frame);
            png(frame, work + "/frame_switch_phase_reset_moved.png");
            on = to(ra) && on;
            const long d7 = diff("history");
            q.paint(frame);
            png(frame, work + "/frame_switch_history_moved.png");
            on = to(ro) && on;
            const long d8 = diff("magnified");
            q.paint(frame);
            png(frame, work + "/frame_switch_magnified_moved.png");
            check(on && d0 == 0 && d1 == 0 && d2 == 0 && d3 == 0 && d4 == 0 && d5 == 0 && d6 == 0 && d7 == 0 && d8 == 0,
                  "the round's export at" + said + ": the waveform scene, the switch Ink -> Warp Flag and back, the "
                  "switch Ink -> Playhead Head, the switch on to the Label, on to the Selection, on to the Phase Reset "
                  "Flag, on to the Added Flag and on to the Outline each equal the mock tool's render (" +
                  std::to_string(d0) + ", " + std::to_string(d1) + ", " + std::to_string(d2) + ", " + std::to_string(d3) +
                  ", " + std::to_string(d4) + ", " + std::to_string(d5) + ", " + std::to_string(d6) + ", " +
                  std::to_string(d7) + ", " + std::to_string(d8) + " px differ)");
            fresh();
        }
    }

    // ---------------------------------------------------------------- today's files load unchanged
    if (ink >= 0 && chrome >= 0) {
        // the 15 lines of the first build (the old form, kept verbatim from the tablet's file) and 43 of the HSV build,
        // the last #A6B9DE; the state.json the brief names
        const char* olds[15] = {"2026-10-04T05:05:02-04:00 ink #A979A3", "2026-10-04T05:07:58-04:00 ink #6E6EA5",
                                "2026-10-04T05:08:03-04:00 ink #6E6EA5", "2026-10-04T05:08:11-04:00 ink #9D9DEC",
                                "2026-10-04T05:09:15-04:00 ink #9D9DEC", "2026-10-04T05:10:12-04:00 ink #999DEC",
                                "2026-10-04T05:10:16-04:00 ink #969DEC", "2026-10-04T05:10:50-04:00 ink #969FEC",
                                "2026-10-04T05:18:07-04:00 ink #9E9FFF", "2026-10-04T05:18:09-04:00 ink #9E9FFF",
                                "2026-10-04T05:18:13-04:00 ink #9494EE", "2026-10-04T05:18:59-04:00 ink #95A7EC",
                                "2026-10-04T05:20:03-04:00 ink #9AABEA", "2026-10-04T05:20:32-04:00 ink #9AABEA",
                                "2026-10-04T05:24:07-04:00 ink #9AABEA"};
        std::string text;
        for (const char* l : olds) text += std::string(l) + "\n";
        std::vector<ColourState> views;
        for (int k = 0; k < 43; ++k) {
            ColourState c;
            if (k == 42) c.set_rgb(Rgb{0xA6, 0xB9, 0xDE});
            else c.set_hsv(218 + k * 0.25, 0.2 + k * 0.003, 0.7 + k * 0.004);
            views.push_back(c);
            char t[40];
            std::snprintf(t, sizeof t, "2026-10-04T06:%02d:00-04:00", k);
            text += std::string(t) + " ink " + hex_of(c.rgb) + " hsv " + view_number(c.h) + " " + view_number(c.s) + " " +
                    view_number(c.v) + "\n";
        }
        put(work + "/picks.txt", text);
        put(work + "/state.json", "{\"colours\": {\"ink\": \"#A6B9DE\"}, \"entry\": {\"ink\": 58}}");
        Picker t = launch();
        const History& h = t.element(ink).hist;
        bool same = h.picks.size() == 58;
        for (int k = 0; same && k < 15; ++k) same = !h.picks[size_t(k)].has_view;
        for (int k = 0; same && k < 43; ++k) same = h.picks[size_t(15 + k)].has_view && h.picks[size_t(15 + k)].x[0] == views[size_t(k)].h;
        check(same && h.cursor == 57 && t.element(ink).cs.rgb == (Rgb{0xA6, 0xB9, 0xDE}) && t.active() == act0 &&
                  t.element(chrome).hist.cursor == -1 && t.element(chrome).cs.rgb == ex.elements[size_t(chrome)].colour,
              "today's picks.txt (58 ink lines, 15 old and 43 new) and state.json load unchanged: the ink 58 of 58 "
              "#A6B9DE, the other elements at the manifest's colours, the manifest's active element (no active in the file)");
        tap(t, 1700, 700);
        tap(t, name_x, name_y);
        row_tap(t, ink);
        tap(t, back_x, hist_y);
        check(count(t) == "57 of 58" && t.colour().rgb == views[41].rgb, "and the ink steps as before");
        tap(t, name_x, name_y);
        row_tap(t, chrome);                           // a stepped ink: nothing appended
        plus(t, 2);
        tap(t, 2200, 1300);
        const std::string after = slurp(work + "/picks.txt");
        check(after.compare(0, text.size(), text) == 0 && lines_of(work + "/picks.txt") == 60 &&
                  after.find(" chrome ", text.size()) == text.size() + 25,
              "the chrome's lines append after the 58, which stay as they were (its start, committed by the switch from "
              "its empty history, and its edit)");
        Picker u = launch();
        check(u.active() == chrome && u.element(ink).hist.picks.size() == 58 && u.element(ink).hist.cursor == 56 &&
                  u.element(chrome).hist.picks.size() == 2, "the new state.json reads back: chrome active, the ink at 57 of 58");
    }

    // ---------------------------------------------------------------- the per-frame cost of a pen drag (R's track)
    {
        fresh();
        for (int run = 0; run < 4; ++run) {   // R's track on the ink, the chrome, the warp flag; LCh's h on the chrome
            const int e = run == 0 ? ink : run == 2 ? wflag : chrome;
            const bool lch = run == 3;
            if (e < 0) continue;
            Picker p = launch();
            tap(p, 1700, 700);
            if (e != p.active()) { tap(p, name_x, name_y); row_tap(p, e); }
            if (lch) {   // LCh at C 60, its three tracks painted per frame, h swept (the gamut stop on its edges)
                tap(p, ppx + kModelX0 + 60, ppy + kModelY + kModelH / 2.0);
                tap(p, ppx + kModelX0 + 60, ppy + kModelListY + 2 * kChooserRowH + kChooserRowH / 2.0);
                track(p, 1, 60.0 / kChromaMax);
            }
            const double ty = ppy + row_y(lch ? 2 : 3) + 30;
            p.press(ppx + kTrackX, ty);
            const int frames = 240;
            double t_move = 0, t_paint = 0;
            for (int f = 1; f <= frames; ++f) {   // R's track swept: every frame a new byte, the scene repainted
                const auto t0 = std::chrono::steady_clock::now();
                p.move(ppx + kTrackX + (f % 2 ? f : frames - f) * (kTrackL - 1.0) / frames, ty);
                const auto t1 = std::chrono::steady_clock::now();
                p.paint(frame);
                const auto t2 = std::chrono::steady_clock::now();
                t_move += std::chrono::duration<double, std::milli>(t1 - t0).count();
                t_paint += std::chrono::duration<double, std::milli>(t2 - t1).count();
            }
            p.release(ppx + kTrackX, ty);
            const Export& x = p.exp();
            const Scene& sc = x.scenes[size_t(x.elements[size_t(e)].scene)];
            size_t solid = 0, stacks = 0;
            for (size_t r = 0; r < x.roles.size(); ++r)
                if (x.roles[r].deps & (1u << e))
                    for (const Run& run : sc.solid[r]) solid += run.len;
            for (const Stack& s : sc.stacks)
                if (s.deps & (1u << e)) ++stacks;
            std::printf("     the pen drag on %s%s: %.3f ms the live repaint (%zu solid px refilled, %zu antialiased px "
                        "re-blended) + %.3f ms the frame (the picture copied, the panel painted) = %.3f ms per frame, "
                        "%d frames\n", x.elements[size_t(e)].key.c_str(), lch ? " (LCh, h's track)" : "", t_move / frames,
                        solid, stacks, t_paint / frames, (t_move + t_paint) / frames, frames);
            check(true, "the per-frame cost of a pen drag on " + x.elements[size_t(e)].key + (lch ? " in LCh" : "") + " measured");
        }
        fresh();
    }

    std::vector<std::string> msg = {"warptempo picker: no scene", "manifest.json: missing or unreadable (in /x/scene)"};
    paint_message(frame, msg);
    png(frame, work + "/frame_message.png");
    cairo_surface_destroy(frame);
    std::printf("%s\n", g_fail ? "FAILED" : "all checks pass");
    return g_fail ? 1 : 0;
}
