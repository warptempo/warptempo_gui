// tools/palette/picker — THE LAPTOP CHECK (no device): the picker's portable core driven on the host, with the
// laptop's cairo and FreeType, against the mock tool's own bytes. Built and run by build_picker.sh --check (never
// part of the APK):
//
//   host_check <fonts dir> <scene dir> <work dir> [--linmix <table>] [--expect <#rrggbb> <ppm>]...
//
//   - the scene loads and its picture (background + layers in the manifest's colours) is the background byte for byte
//     (the export wrote the render as the background);
//   - --linmix: a 65536-byte table from colour.py, lin_mix(a, b, 0.5) for every byte pair: the C++ port must agree
//     on every one;
//   - --expect: the active layer set to the hex, the picture must equal the ppm (the mock tool's render of the theme
//     with that colour) byte for byte -- the derived layers following by their rule;
//   - a scripted session (open, drag, steps, revert, close) on the work dir, its frames written as PNGs there for the
//     eye, its commit's picks.txt and state.json checked;
//   - a scripted HISTORY session (picker.h's pick history): the empty history's disabled buttons, commits, BACK and
//     FORWARD, the disabled ends, a no-op close after a step, an edit-then-step discard, leaving the app with an
//     unsaved edit, reloads restoring the cursor (this build's state.json and the previous build's),
//     frame_history_*.png for the eye.

#include "colour.h"
#include "fonts.h"
#include "picker.h"
#include "scene.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
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

// the picture's words against a binary P6 (the export's header shape)
size_t diff_ppm(const std::vector<uint32_t>& pic, const std::string& ppm_path, int w, int h) {
    const std::string d = slurp(ppm_path);
    const std::string hdr = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    if (d.compare(0, hdr.size(), hdr) != 0 || d.size() != hdr.size() + size_t(w) * h * 3) return size_t(-1);
    const auto* p = reinterpret_cast<const uint8_t*>(d.data() + hdr.size());
    size_t n = 0;
    for (size_t k = 0; k < size_t(w) * h; ++k)
        if (pic[k] != word_of(Rgb{p[3 * k], p[3 * k + 1], p[3 * k + 2]})) ++n;
    return n;
}

size_t lines_of(const std::string& path) {
    const std::string t = slurp(path);
    return size_t(std::count(t.begin(), t.end(), '\n'));
}

std::vector<uint32_t> picture_of(const Scene& s) {
    std::vector<uint32_t> pic(size_t(s.width) * s.height);
    scene_paint(s, pic.data());
    return pic;
}

void png(cairo_surface_t* surf, const std::string& path) {
    cairo_surface_write_to_png(surf, path.c_str());
    std::printf("     wrote %s\n", path.c_str());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: host_check <fonts dir> <scene dir> <work dir> [--linmix <table>] [--expect <#rrggbb> <ppm>]...\n");
        return 2;
    }
    const std::string fonts = argv[1], scene_dir = argv[2], work = argv[3];
    const std::string sans = slurp(fonts + "/Roboto-Regular.ttf"), mono = slurp(fonts + "/RobotoMono-Regular.ttf");
    if (!fonts_install(reinterpret_cast<const uint8_t*>(sans.data()), sans.size(),
                       reinterpret_cast<const uint8_t*>(mono.data()), mono.size())) {
        std::fprintf(stderr, "fonts did not install\n");
        return 2;
    }
    Scene scene;
    std::string err;
    if (!scene_load(scene_dir, scene, err)) {
        std::fprintf(stderr, "scene: %s\n", err.c_str());
        return 1;
    }
    std::printf("scene %dx%d, %zu layers, active %s %s\n", scene.width, scene.height, scene.layers.size(),
                scene.layers[scene.active].name.c_str(), hex_of(scene.layers[scene.active].colour).c_str());
    check(diff_ppm(picture_of(scene), scene_dir + "/background.ppm", scene.width, scene.height) == 0,
          "the picture in the manifest's colours is the background byte for byte");

    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--linmix" && i + 1 < argc) {
            const std::string t = slurp(argv[++i]);
            size_t bad = 0;
            if (t.size() != 65536) bad = 65536;
            else
                for (int x = 0; x < 256; ++x)
                    for (int y = 0; y < 256; ++y)
                        if (lin_mix_byte(x, y, 0.5) != uint8_t(t[size_t(x) * 256 + y])) ++bad;
            check(bad == 0, "lin_mix(a, b, 0.5) equals colour.py on all 65536 byte pairs (" + std::to_string(bad) + " differ)");
        } else if (a == "--expect" && i + 2 < argc) {
            const std::string hex = argv[++i], ppm = argv[++i];
            Scene s2 = scene;
            Rgb c;
            if (!parse_hex(hex, c)) { std::fprintf(stderr, "bad hex %s\n", hex.c_str()); return 2; }
            s2.layers[s2.active].colour = c;
            scene_derive(s2);
            const size_t n = diff_ppm(picture_of(s2), ppm, s2.width, s2.height);
            check(n == 0, "the picture with " + s2.layers[s2.active].name + " " + hex + " equals " + ppm +
                              " (" + std::to_string(n == size_t(-1) ? -1L : long(n)) + " px differ)");
        }
    }

    // ColourState: the bytes are the truth, the hue survives grey and black
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
        for (int r = 0; r < 256 && all; r += 5)
            for (int g = 0; g < 256 && all; g += 7)
                for (int b = 0; b < 256 && all; b += 3) {
                    ColourState t;
                    t.set_rgb(Rgb{uint8_t(r), uint8_t(g), uint8_t(b)});
                    all = rgb_of_hsv(t.h, t.s, t.v) == t.rgb;
                }
        check(all, "bytes -> HSV -> bytes is the identity (a sampled cube)");
    }

    // a scripted session over the scene, on its own copy, frames written for the eye
    std::remove((work + "/picks.txt").c_str());
    std::remove((work + "/state.json").c_str());
    History hist0;
    std::string note;
    Scene scene0 = scene;
    const bool loaded0 = picker_load(work, scene0, hist0, note, err);
    check(loaded0 && hist0.cursor == -1 && hist0.picks.empty(),
          "no state.json and no picks.txt: the manifest's colour, an empty history (" + note + ")");
    Picker p(scene0, work, hist0);
    cairo_surface_t* frame = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, scene.width, scene.height);
    p.paint(frame);
    png(frame, work + "/frame_closed.png");
    p.press(1700, 700);
    p.release(1700, 700);
    check(p.open() && !p.panel_on_right(), "a tap on the right half opens the panel on the left");
    p.paint(frame);
    png(frame, work + "/frame_open_left.png");
    const Rgb start = p.colour().rgb;
    // R's track: press at the byte 200's column, drag to 230's
    using namespace panel;
    const double px = kMargin, py = (scene.height - kH) / 2;
    auto track_x = [&](int byte) { return px + kTrackX + byte / 255.0 * (kTrackL - 1); };
    p.press(track_x(200), py + row_y(3) + 30);
    p.move(track_x(230), py + row_y(3) + 40);
    p.release(track_x(230), py + row_y(3) + 40);
    check(p.colour().rgb.r == 230 && p.colour().rgb.g == start.g, "R's track sets the byte under the pen");
    p.press(px + kPlusX + 30, py + row_y(5) + 30);
    p.release(px + kPlusX + 30, py + row_y(5) + 30);
    check(p.colour().rgb.b == start.b + 1, "B's increment is one byte");
    p.press(px + kPad + kROut + 255, py + kPad + kROut);   // the ring at 0 degrees
    p.move(px + kPad + kROut, py + kPad + kROut - 255);    // to 90
    p.release(px + kPad + kROut, py + kPad + kROut - 255);
    check(std::abs(p.colour().h - 90) < 1e-9, "the ring sets the hue by the angle (90 at the top)");
    p.paint(frame);
    png(frame, work + "/frame_dragged.png");
    {   // the active layer on the picture is the new colour
        cairo_surface_flush(frame);
        const auto* w = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(frame));
        uint32_t k = 0;      // a pixel of the active layer right of the panel (open on the left)
        for (uint32_t q : p.scene().layers[p.scene().active].pixels)
            if (int(q % scene.width) > kMargin + kW + 10) { k = q; break; }
        const int st = cairo_image_surface_get_stride(frame) / 4;
        check(w[size_t(k / scene.width) * st + k % scene.width] == word_of(p.colour().rgb),
              "the active layer repaints live with the pick");
    }
    p.press(px + kColX + 50, py + kSwatchY0 + 50);
    p.release(px + kColX + 50, py + kSwatchY0 + 50);
    check(p.colour().rgb == start, "a tap on OLD reverts");
    p.press(px + kTrackX + 100, py + row_y(0) + 30);   // H's track, then close
    p.release(px + kTrackX + 100, py + row_y(0) + 30);
    const std::string hex = hex_of(p.colour().rgb);
    p.press(1700, 700);
    p.release(1700, 700);
    check(!p.open(), "a tap outside the panel closes it");
    const std::string picks = slurp(work + "/picks.txt");
    check(picks.size() > 30 && picks.find(" " + p.scene().layers[p.scene().active].name + " " + hex + "\n") != std::string::npos,
          "the commit appended to picks.txt: " + picks.substr(0, picks.size() - 1));
    std::map<std::string, Rgb> st;
    std::map<std::string, int> ent;
    check(state_load(work + "/state.json", st, ent, err) && st.count(p.scene().layers[p.scene().active].name) &&
              hex_of(st[p.scene().layers[p.scene().active].name]) == hex,
          "state.json holds the commit");
    p.press(300, 700);
    p.release(300, 700);
    check(p.open() && p.panel_on_right(), "a tap on the left half opens the panel on the right");
    p.paint(frame);
    png(frame, work + "/frame_open_right.png");
    p.press(300, 700);    // close again (the panel is on the right), unchanged: the history's last entry
    p.release(300, 700);
    check(!p.open() && lines_of(work + "/picks.txt") == 1, "a close on the unedited colour appends nothing");

    // ---------------------------------------------------------------- the pick history
    {
        std::remove((work + "/picks.txt").c_str());
        std::remove((work + "/state.json").c_str());
        const std::string L = scene.layers[scene.active].name;
        // a Picker as the device builds it at launch, over the work dir as it stands
        auto launch = [&](std::string& why) {
            Scene sc = scene;
            History h;
            if (!picker_load(work, sc, h, why, err)) { std::printf("FAIL picker_load: %s\n", err.c_str()); ++g_fail; }
            return Picker(sc, work, h);
        };
        auto tap = [](Picker& q, double x, double y) { q.press(x, y); q.release(x, y); };
        auto count = [](const Picker& q) {
            return std::to_string(q.history().cursor + 1) + " of " + std::to_string(q.history().picks.size());
        };
        // the panel opens on the left (a tap at x 1700): its buttons in window px
        const double ppx = kMargin, ppy = (scene.height - kH) / 2;
        const double back_x = ppx + kBackX + kBtn / 2.0, fwd_x = ppx + kFwdX + kBtn / 2.0, hist_y = ppy + kHistY + kBtn / 2.0;
        auto plus = [&](Picker& q, int row) { tap(q, ppx + kPlusX + 30, ppy + row_y(row) + 30); };

        Picker q = launch(note);
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
        check(lines_of(work + "/picks.txt") == 3 && count(q) == "3 of 3" && q.history().picks[1] == a1 &&
                  q.history().picks[2] == a2, "two edited closes commit: 3 of 3");

        tap(q, 1700, 700);
        check(q.back_enabled() && !q.forward_enabled(), "at the last entry: BACK enabled, FORWARD disabled");
        tap(q, back_x, hist_y);
        check(q.colour().rgb == a1 && count(q) == "2 of 3" && q.old() == a2, "BACK shows the entry before; OLD stays");
        {   // the active layer repaints live with the step
            q.paint(frame);
            cairo_surface_flush(frame);
            const auto* w = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(frame));
            uint32_t k = 0;
            for (uint32_t i : q.scene().layers[q.scene().active].pixels)
                if (int(i % scene.width) > kMargin + kW + 10) { k = i; break; }
            const int stw = cairo_image_surface_get_stride(frame) / 4;
            check(w[size_t(k / scene.width) * stw + k % scene.width] == word_of(a1), "the active layer repaints with the step");
        }
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
        std::map<std::string, Rgb> sc;
        std::map<std::string, int> se;
        check(state_load(work + "/state.json", sc, se, err) && sc[L] == a1 && se[L] == 2,
              "state.json records the stepped colour and the cursor 2");

        Picker r = launch(note);
        check(r.colour().rgb == a1 && count(r) == "2 of 3", "a reload restores the colour and the cursor (" + note + ")");
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
        check(state_load(work + "/state.json", sc, se, err) && sc[L] == a1 && se[L] == 2, "and state.json is untouched");
        Picker r2 = launch(note);
        check(r2.colour().rgb == a1 && count(r2) == "2 of 3", "a relaunch after it comes back on the last saved state");
        tap(r, 1700, 700);
        plus(r, 3);
        const Rgb a4 = r.colour().rgb;
        tap(r, 1700, 700);
        check(lines_of(work + "/picks.txt") == 4 && count(r) == "4 of 4" && r.history().picks[3] == a4,
              "a close on an edited colour from the middle commits it at the end: 4 of 4");

        // the previous build's state.json: no entry; the newest entry equal to the colour, else the end
        {
            FILE* f = std::fopen((work + "/state.json").c_str(), "w");
            std::fprintf(f, "{\n \"%s\": \"%s\"\n}\n", L.c_str(), hex_of(a2).c_str());
            std::fclose(f);
        }
        Picker o = launch(note);
        check(o.colour().rgb == a2 && count(o) == "3 of 4", "the previous build's state.json: the newest equal entry (" + note + ")");
        {
            FILE* f = std::fopen((work + "/state.json").c_str(), "w");
            std::fprintf(f, "{\n \"%s\": \"#010203\"\n}\n", L.c_str());
            std::fclose(f);
        }
        Picker e = launch(note);
        check(count(e) == "4 of 4" && e.edited(), "no entry equals the colour: the end, edited (" + note + ")");
        tap(e, 1700, 700);
        tap(e, 1700, 700);
        check(lines_of(work + "/picks.txt") == 5, "and its close commits it");
        // picks.txt deleted beside a kept state.json: the entry is out of range
        std::remove((work + "/picks.txt").c_str());
        Picker d = launch(note);
        check(count(d) == "0 of 0" && d.colour().rgb == (Rgb{1, 2, 3}), "picks.txt deleted: an empty history (" + note + ")");
        // a malformed picks.txt is a hard fail
        {
            FILE* f = std::fopen((work + "/picks.txt").c_str(), "w");
            std::fprintf(f, "2026-10-04T05:00:00+00:00 %s #12345\n", L.c_str());
            std::fclose(f);
            Scene sx = scene;
            History hx;
            std::string ex;
            const bool loaded = picker_load(work, sx, hx, note, ex);
            check(!loaded, "a malformed picks.txt fails the load: " + ex);
        }
    }

    std::vector<std::string> msg = {"warptempo picker: no scene", "manifest.json: missing or unreadable (in /x/scene)"};
    paint_message(frame, msg);
    png(frame, work + "/frame_message.png");
    cairo_surface_destroy(frame);
    std::printf("%s\n", g_fail ? "FAILED" : "all checks pass");
    return g_fail ? 1 : 0;
}
