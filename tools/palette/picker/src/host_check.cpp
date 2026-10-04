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
//     eye, its commit's picks.txt and state.json checked.

#include "colour.h"
#include "fonts.h"
#include "picker.h"
#include "scene.h"

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
    Picker p(scene, work);
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
    check(state_load(work + "/state.json", st, err) && st.count(p.scene().layers[p.scene().active].name) &&
              hex_of(st[p.scene().layers[p.scene().active].name]) == hex,
          "state.json holds the commit");
    p.press(300, 700);
    p.release(300, 700);
    check(p.open() && p.panel_on_right(), "a tap on the left half opens the panel on the right");
    p.paint(frame);
    png(frame, work + "/frame_open_right.png");
    std::vector<std::string> msg = {"warptempo picker: no scene", "manifest.json: missing or unreadable (in /x/scene)"};
    paint_message(frame, msg);
    png(frame, work + "/frame_message.png");
    cairo_surface_destroy(frame);
    std::printf("%s\n", g_fail ? "FAILED" : "all checks pass");
    return g_fail ? 1 : 0;
}
