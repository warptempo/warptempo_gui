// tools/palette/picker — THE LAPTOP CHECK (no device): the picker's portable core driven on the host, with the
// laptop's cairo and FreeType, against the mock tool's own bytes. Built and run by build_picker.sh --check (never
// part of the APK):
//
//   host_check <fonts dir> <work dir> --export <scene dir> <expects.txt> [--export <dir> <expects.txt>]...
//              [--linmix <table>] [--today <dir>]
//
//   - every export loads, and every expects.txt line ("<scene> <ppm> [<key>=#RRGGBB ...]": the mock tool's render of
//     that scene with those elements moved from the manifest's colours) equals the picker's picture byte for byte,
//     both painted whole and reached by the live repaint from the manifest's colours (the road a pen drag takes);
//   - --linmix: a 65536-byte table from colour.py, lin_mix(a, b, 0.5) for every byte pair: the C++ port must agree;
//   - the blend (colour.h over_n_8) equals cairo's own compositing of a solid source through an A8 mask on every
//     (source, frame, coverage) byte triple, each channel; the chrome rule's scale_byte is half-to-even and capped;
//   - ColourState over the whole cube, the view numbers, the readouts (one decimal; the widest fits its field);
//   - the scripted sessions on the first export (check_refs.py's check theme: Chrome, Canvas, Ink and a Flag Test over
//     a second scene) and its active element (the chrome): the panel, the pick history, the HSV he dialled; then THE
//     CHOOSER (open, a tap outside it, the no-op re-choice, the edited-then-switch commit, the switch of scene),
//     every element's history, cursor and view independent across a switch and a relaunch, and today's picks.txt
//     (58 ink lines, the old form and the new) and state.json loading unchanged;
//   - THE PRESETS (two saves, the edited colour committed by the pop-up's opening; a load committing each changed
//     element once and an unchanged one never, BACK returning; a relaunch; a scroll and a tap outside that act on
//     nothing) and THE THEME STRIP (opened from the pop-up, swatches adopted as edits into the chrome and, after a
//     switch, the canvas, OLD reverting, the strip surviving the switch and a relaunch, Windows 95 Standard's quartet
//     and a tinted theme's rule lines, its own scroll, the close control) on the first export;
//   - --today <dir>: the tablet's picks.txt and state.json of 2026-10-04 load unchanged;
//   - THE PER-FRAME COST of a pen drag (R's track) on the ink and on the chrome: the live repaint and the frame;
//   - frames as PNGs in the work dir for the eye.

#include "colour.h"
#include "fonts.h"
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
    const std::string sans = slurp(fonts + "/Roboto-Regular.ttf"), mono = slurp(fonts + "/RobotoMono-Regular.ttf");
    if (!fonts_install(reinterpret_cast<const uint8_t*>(sans.data()), sans.size(),
                       reinterpret_cast<const uint8_t*>(mono.data()), mono.size())) {
        std::fprintf(stderr, "fonts did not install\n");
        return 2;
    }
    std::string err;
    Export ex0;                                   // the first export: the sessions' and the timing's
    std::string today;                            // --today: the tablet's picks.txt and state.json of 2026-10-04
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
            check_expects(ex, expects, dir.substr(dir.find_last_of('/') + 1));
            if (!have) { ex0 = std::move(ex); have = true; }
        } else if (a == "--today" && i + 1 < argc) {
            today = argv[++i];
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
        bool all = true;
        for (int r = 0; r < 256 && all; ++r)
            for (int g = 0; g < 256 && all; ++g)
                for (int b = 0; b < 256 && all; ++b) {
                    ColourState t;
                    t.set_rgb(Rgb{uint8_t(r), uint8_t(g), uint8_t(b)});
                    all = rgb_of_hsv(t.h, t.s, t.v) == t.rgb;
                }
        check(all, "bytes -> HSV -> bytes is the identity (the whole cube)");
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
        fonts_select(mc, true, panel::kNumPx);
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

    using namespace panel;
    const int W = ex.width;
    const std::string L = ex.elements[size_t(ex.active)].key;   // its key
    const int act0 = ex.active;   // the manifest's active element (the chrome): the panel's, history's and HSV sessions'
    const int ink = element_of(ex, "ink"), chrome = element_of(ex, "chrome"), canvas = element_of(ex, "canvas"),
              flag = element_of(ex, "flag_test");
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
    auto row_tap = [&](Picker& q, int e) { tap(q, name_x, ppy + kChooserY + e * kChooserRowH + kChooserRowH / 2.0); };
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
        check(state_load(work + "/state.json", st, ent, act, g_theme, err) && st.count(L) && hex_of(st[L].rgb) == hex &&
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
        check(state_load(work + "/state.json", sc, se, act, g_theme, err) && sc[L].rgb == a1 && se[L] == 2,
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
        check(state_load(work + "/state.json", sc, se, act, g_theme, err) && sc[L].rgb == a1 && se[L] == 2, "and state.json is untouched");
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
    if (chrome != act0 || ink < 0 || canvas < 0 || flag < 0 || chrome != 0 || canvas != 1 || ink != 2 ||
        ex.elements[size_t(flag)].scene == ex.elements[size_t(chrome)].scene) {
        check(false, "the check export lists Chrome (active), Canvas, Ink and a Flag Test over a second scene");
    } else {
        fresh();
        const Rgb chrome0 = ex.elements[size_t(chrome)].colour, ink0 = ex.elements[size_t(ink)].colour;
        const Rgb tint{0x20, 0x60, 0x48};
        Picker p = launch();
        tap(p, 1700, 700);
        tap(p, name_x, name_y);
        check(p.chooser_open() && p.open(), "a tap on the element's name opens the chooser (Chrome, Canvas, Ink, ...)");
        p.paint(frame);
        png(frame, work + "/frame_chooser_open.png");
        tap(p, ppx + 200, ppy + 900);                 // inside the panel, outside the chooser
        check(!p.chooser_open() && p.open() && p.active() == chrome && !std::ifstream(work + "/state.json"),
              "a tap outside the chooser closes it and changes nothing (the panel open, no file written)");
        tap(p, name_x, name_y);
        tap(p, 2200, 1300);                           // outside the panel too: only the chooser closes
        check(!p.chooser_open() && p.open(), "a tap outside the panel while the chooser is open closes the chooser alone");
        tap(p, name_x, name_y);
        p.press(name_x, ppy + kChooserY + ink * kChooserRowH + 30);   // pressed on Ink, lifted on Flag Test
        p.release(name_x, ppy + kChooserY + flag * kChooserRowH + 30);
        check(p.chooser_open() && p.active() == chrome, "a press on one entry lifted on another chooses nothing (the chooser stays)");
        row_tap(p, chrome);
        check(!p.chooser_open() && p.active() == chrome && !std::ifstream(work + "/state.json") &&
                  !std::ifstream(work + "/picks.txt"), "choosing the active element again is a no-op");
        // the chrome at a tint, dialled by its R / G / B tracks: every line follows
        track(p, 3, tint.r / 255.0);
        track(p, 4, tint.g / 255.0);
        track(p, 5, tint.b / 255.0);
        check(p.colour().rgb == tint, "the chrome dialled to #206048 by its tracks");
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
        const bool st_ok = state_load(work + "/state.json", st, ent, act, g_theme, err);
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
        // an edited ink, then the switch to the Flag Test: another scene
        plus(p, 3);
        const Rgb ink1 = p.colour().rgb;
        tap(p, name_x, name_y);
        row_tap(p, flag);
        const int fsc = p.exp().elements[size_t(flag)].scene;
        const long kf = element_pixel(p.exp(), fsc, flag, -1), kc = element_pixel(p.exp(), fsc, chrome, 1300),
                   kn = element_pixel(p.exp(), fsc, ink, kMargin + kW + 10);
        check(p.active() == flag && lines_of(work + "/picks.txt") == 2 &&
                  slurp(work + "/picks.txt").find(" ink " + hex_of(ink1) + " hsv ") != std::string::npos,
              "the edited ink is committed by the switch to the Flag Test");
        check(kf >= 0 && kc >= 0 && kn >= 0 && p.picture()[size_t(kf)] == word_of(ex.elements[size_t(flag)].colour) &&
                  p.picture()[size_t(kc)] == word_of(tint) && p.picture()[size_t(kn)] == word_of(ink1),
              "the switch shows the Flag Test's scene, the chrome's tint and the ink's new colour live in it");
        plus(p, 4);                                   // the flag face moves: its labels' antialiased edges re-blend over it
        p.paint(frame);
        png(frame, work + "/frame_flag_open.png");
        // the Flag Test's history is empty: switching away commits it, as a close with an empty history does
        tap(p, name_x, name_y);
        row_tap(p, chrome);
        check(p.active() == chrome && lines_of(work + "/picks.txt") == 3 && count(p) == "1 of 1" && p.colour().rgb == tint,
              "the switch back to the chrome: its own history (1 of 1), its tint");
        tap(p, 2200, 1300);
        check(!p.open() && lines_of(work + "/picks.txt") == 3, "an unedited chrome's close appends nothing");
        p.paint(frame);
        png(frame, work + "/frame_chrome_tint.png");
        // a relaunch: the element he left, every element's colour, history and view
        Picker r = launch();
        check(r.active() == chrome && r.colour().rgb == tint && r.element(ink).cs.rgb == ink1 &&
                  r.element(canvas).cs.rgb == ex.elements[size_t(canvas)].colour && r.element(flag).cs.rgb == p.element(flag).cs.rgb &&
                  r.element(ink).hist.picks.size() == 1 && r.element(chrome).hist.picks.size() == 1 &&
                  r.element(flag).hist.picks.size() == 1 && r.element(canvas).hist.cursor == -1,
              "a relaunch returns to the element he left, every element's colour and history its own");
        check(same_view(r.element(chrome).cs, p.element(chrome).cs) && same_view(r.element(ink).cs, p.element(ink).cs) &&
                  same_view(r.element(flag).cs, p.element(flag).cs), "and every element's view exactly as it was");
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
        check(count(r) == "1 of 1" && r.element(ink).hist.cursor == 0 && lines_of(work + "/picks.txt") == 4,
              "the chrome's cursor and the ink's are their own across the switch (a stepped ink switched away appends nothing)");
        Picker r2 = launch();
        check(r2.active() == chrome && r2.element(ink).hist.cursor == 0 && r2.element(ink).cs.rgb == ink1 &&
                  r2.element(chrome).hist.cursor == 0 && r2.element(flag).hist.cursor == 0, "and across a relaunch");
        std::remove((work + "/state.json").c_str());
        Picker r3 = launch();
        check(r3.active() == chrome && r3.colour().rgb == chrome0 && r3.element(chrome).hist.cursor == 0 && r3.edited() &&
                  r3.element(ink).cs.rgb == ink0 && r3.element(ink).hist.cursor == 1 && r3.element(canvas).hist.cursor == -1,
              "state.json deleted: the manifest's active element and colours, each history at its end");
    }

    // ---------------------------------------------------------------- THE PRESETS AND THE THEME STRIP
    if (chrome == 0 && canvas == 1 && ink == 2 && flag >= 0) {
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
        check(n_themes == 98 && ex.themes[0].key == "windows-brick" && theme_of(ex, "windows-95-standard") >= 0 &&
                  theme_of(ex, "warptempo") == n_themes - 1,
              "themes.json lists the product's 98 themes in the table's order (" + std::to_string(cmin) + ".." +
                  std::to_string(cmax) + " colours each)");

        Picker p = launch();
        const Rgb ink0 = p.element(ink).cs.rgb, cv0 = p.element(canvas).cs.rgb, fl0 = p.element(flag).cs.rgb;
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
                  p.presets()[0].colours.at("chrome").rgb == c1.rgb && p.presets()[0].colours.size() == 4,
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
                  p.element(canvas).cs.rgb == cv0 && p.element(canvas).hist.picks.empty() &&
                  p.element(flag).cs.rgb == fl0 && p.element(flag).hist.picks.empty(),
              "loading Preset 1: the chrome and the ink each one committed pick (5 lines), the canvas and the flag none; "
              "OLD the loaded chrome, its view exact");
        {
            std::map<std::string, Pick> st;
            std::map<std::string, int> ent;
            std::string act;
            check(state_load(work + "/state.json", st, ent, act, g_theme, err) && st["chrome"].rgb == c1.rgb &&
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
                  r.save_label() == "Save as Preset 3" && r.presets()[1].colours.at("chrome").h == c2.h &&
                  r.presets()[1].colours.at("chrome").s == c2.s && r.presets()[1].colours.at("chrome").v == c2.v,
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
            check(s.theme_open() == -1 && state_load(work + "/state.json", st, ent, act, g_theme, err) && g_theme.empty() &&
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
        check(load_fails(why), "a presets.json view that does not give its hex fails the load: " + why);
        fresh();
    }

    // ---------------------------------------------------------------- the tablet's files of 2026-10-04 load unchanged
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
                  t.element(ink).hist.cursor == 76 && t.theme_open() == -1 && t.presets().empty(),
              "the tablet's picks.txt (110 lines) and state.json load unchanged: the canvas active, chrome 9 of 11, canvas "
              "22 of 22, ink 77 of 77, every view exact");
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
        for (int k = 0; same && k < 15; ++k) same = !h.picks[size_t(k)].has_hsv;
        for (int k = 0; same && k < 43; ++k) same = h.picks[size_t(15 + k)].has_hsv && h.picks[size_t(15 + k)].h == views[size_t(k)].h;
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
        for (int e : {ink, chrome}) {
            if (e < 0) continue;
            Picker p = launch();
            tap(p, 1700, 700);
            if (e != p.active()) { tap(p, name_x, name_y); row_tap(p, e); }
            const double ty = ppy + row_y(3) + 30;
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
            std::printf("     the pen drag on %s: %.3f ms the live repaint (%zu solid px refilled, %zu antialiased px "
                        "re-blended) + %.3f ms the frame (the picture copied, the panel painted) = %.3f ms per frame, "
                        "%d frames\n", x.elements[size_t(e)].key.c_str(), t_move / frames, solid, stacks, t_paint / frames,
                        (t_move + t_paint) / frames, frames);
            check(true, "the per-frame cost of a pen drag on " + x.elements[size_t(e)].key + " measured");
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
