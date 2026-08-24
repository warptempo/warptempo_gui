// warptempo M2 spike -- the whole Android substrate on one surface.
//
// What it proves, in the order the brief asks for it:
//   1. cairo test card (colour swatches, 1px lines, two harfbuzz-shaped lines)
//   2. live touch echo, one labelled dot per finger, tracked by POINTER ID
//   3. a tap region that plays the bundled 44.1 kHz WAV through AAudio
//   4. a tap region that runs the OTG write probe and prints errno
//   5. a frame counter driven by the ALooper redraw
//
// NO PRODUCT SOURCE IS INVOLVED. Nothing here is a draft of the real platform
// layer; it is a throwaway that answers device questions the laptop cannot.
//
// RUN LOOP: research §3.4 Model 1 -- ALooper_pollOnce with a timeout, no
// AChoreographer. The spike must repaint continuously (the frame counter is
// itself an assertion that the loop turns), so the timeout is a flat 8 ms and
// ANativeWindow_unlockAndPost's blocking on buffer availability is what actually
// paces it. The product's own free-running ~8 ms tick maps onto exactly this.
//
// GLUE: the NDK r29 stock android_native_app_glue, UNPATCHED. Research §3.7 says
// to copy it in and fix process_input's one-event-per-callback ANR bug -- that bug
// is already fixed upstream in r29 (it drains with a while loop and `continue`),
// so there is nothing to patch and no fork to maintain.

#include <android/asset_manager.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <arm_neon.h>
#include <cairo/cairo.h>
#include <time.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "spike_audio.h"
#include "spike_log.h"
#include "spike_storage.h"
#include "spike_text.h"

namespace {

// ---------------------------------------------------------------- small helpers

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return std::string(buf);
}

double now_seconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(double px, double py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

void set_rgb(cairo_t* cr, uint32_t rgb) {
    cairo_set_source_rgb(cr, ((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0,
                         (rgb & 0xFF) / 255.0);
}

void fill_rect(cairo_t* cr, const Rect& r, uint32_t rgb) {
    set_rgb(cr, rgb);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_fill(cr);
}

// ------------------------------------------------------------------ the swizzle
//
// cairo's ARGB32 is a NATIVE-ENDIAN 0xAARRGGBB word, so on this little-endian
// target its bytes run B,G,R,A. Android's WINDOW_FORMAT_RGBA_8888/RGBX_8888 run
// R,G,B,A. R and B are therefore transposed and the copy has to fix it. The
// decision is made per frame off the format the window ACTUALLY handed back, not
// off the format that was requested.

void copy_swap_rb(uint32_t* dst, const uint32_t* src, int n) {
    int x = 0;
    for (; x + 16 <= n; x += 16) {
        uint8x16x4_t p = vld4q_u8(reinterpret_cast<const uint8_t*>(src + x));
        const uint8x16_t t = p.val[0];
        p.val[0] = p.val[2];
        p.val[2] = t;
        vst4q_u8(reinterpret_cast<uint8_t*>(dst + x), p);
    }
    for (; x < n; ++x) {
        const uint32_t v = src[x];
        dst[x] = (v & 0xFF00FF00u) | ((v >> 16) & 0xFFu) | ((v & 0xFFu) << 16);
    }
}

const char* window_format_name(int f) {
    switch (f) {
        case WINDOW_FORMAT_RGBA_8888: return "RGBA_8888";
        case WINDOW_FORMAT_RGBX_8888: return "RGBX_8888";
        case WINDOW_FORMAT_RGB_565: return "RGB_565";
        case 5: return "BGRA_8888 (HAL-only)";
        default: return "unknown";
    }
}

// ---------------------------------------------------------------- spike state

constexpr int kMaxTouches = 10;

struct Touch {
    bool active = false;
    int32_t id = -1;
    float x = 0, y = 0;
};

constexpr double kDemoPx = 48.0;
constexpr double kInfoPx = 26.0;

struct Spike {
    android_app* app = nullptr;
    ANativeWindow* window = nullptr;  // borrowed; NEVER _acquire()d, dropped at TERM

    cairo_surface_t* back = nullptr;
    cairo_t* cr = nullptr;
    int back_w = 0, back_h = 0;

    // The fonts are held by pointer for ONE reason: teardown order. Every FT_Face
    // inside them was created from `ft`, so all three must be gone before
    // FT_Done_FreeType runs, and a destructor body runs before its own members'.
    FT_Library ft = nullptr;
    AAsset* sans_asset = nullptr;
    AAsset* mono_asset = nullptr;
    std::unique_ptr<SpikeFont> sans = std::make_unique<SpikeFont>();
    std::unique_ptr<SpikeFont> mono = std::make_unique<SpikeFont>();
    std::unique_ptr<SpikeFont> info = std::make_unique<SpikeFont>();
    std::string font_error;

    SpikeAudio audio;
    SpikeStorageReport storage;

    Touch touches[kMaxTouches];

    Rect play_rect;
    Rect probe_rect;
    bool play_armed = false;
    bool probe_armed = false;

    long long frame = 0;
    double fps = 0.0;
    double fps_mark = 0.0;
    long long fps_frame_mark = 0;

    int last_format = -1;
    int last_stride = -1;
    int last_bufw = 0, last_bufh = 0;
    bool swizzled = false;
    int lock_failures = 0;
    int last_lock_error = 0;

    ~Spike() {
        if (cr) cairo_destroy(cr);
        if (back) cairo_surface_destroy(back);
        sans.reset();
        mono.reset();
        info.reset();
        if (ft) FT_Done_FreeType(ft);
        if (sans_asset) AAsset_close(sans_asset);
        if (mono_asset) AAsset_close(mono_asset);
    }
};

// ------------------------------------------------------------------ asset load

bool load_fonts(Spike& s) {
    AAssetManager* mgr = s.app->activity->assetManager;
    if (!mgr) { s.font_error = "no AAssetManager"; return false; }

    if (FT_Init_FreeType(&s.ft) != 0) { s.font_error = "FT_Init_FreeType failed"; return false; }

    auto open = [&](const char* name, AAsset** slot) -> const void* {
        *slot = AAssetManager_open(mgr, name, AASSET_MODE_BUFFER);
        if (!*slot) { s.font_error = std::string("missing asset ") + name; return nullptr; }
        return AAsset_getBuffer(*slot);
    };

    const void* sans_bytes = open("LiberationSans-Regular.ttf", &s.sans_asset);
    if (!sans_bytes) return false;
    const void* mono_bytes = open("LiberationMono-Regular.ttf", &s.mono_asset);
    if (!mono_bytes) return false;

    // The AAssets stay open for the process's life: FT_New_Memory_Face does not
    // copy, so the mapped bytes must outlive every face built on them.
    const size_t sans_len = static_cast<size_t>(AAsset_getLength(s.sans_asset));
    const size_t mono_len = static_cast<size_t>(AAsset_getLength(s.mono_asset));

    std::string err;
    if (!s.sans->init(s.ft, sans_bytes, sans_len, kDemoPx, err)) { s.font_error = "sans: " + err; return false; }
    if (!s.mono->init(s.ft, mono_bytes, mono_len, kDemoPx, err)) { s.font_error = "mono: " + err; return false; }
    if (!s.info->init(s.ft, mono_bytes, mono_len, kInfoPx, err)) { s.font_error = "info: " + err; return false; }
    return true;
}

void load_audio_asset(Spike& s) {
    AAssetManager* mgr = s.app->activity->assetManager;
    if (!mgr) return;
    AAsset* a = AAssetManager_open(mgr, "spike.wav", AASSET_MODE_BUFFER);
    if (!a) { SPIKE_LOGE("missing asset spike.wav"); return; }
    const void* bytes = AAsset_getBuffer(a);
    const size_t len = static_cast<size_t>(AAsset_getLength(a));
    s.audio.load_wav(bytes, len);   // parsed into floats, so the asset can close
    AAsset_close(a);
}

// ------------------------------------------------------------------ backbuffer

void ensure_backbuffer(Spike& s, int w, int h) {
    if (s.back && s.back_w == w && s.back_h == h) return;
    if (s.cr) { cairo_destroy(s.cr); s.cr = nullptr; }
    if (s.back) { cairo_surface_destroy(s.back); s.back = nullptr; }
    s.back = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    s.cr = cairo_create(s.back);
    s.back_w = w;
    s.back_h = h;
    SPIKE_LOGI("backbuffer %dx%d (stride %d bytes)", w, h, cairo_image_surface_get_stride(s.back));
}

// ---------------------------------------------------------------------- paint

void draw_button(Spike& s, const Rect& r, const char* label, bool armed, uint32_t fill) {
    fill_rect(s.cr, r, armed ? 0xF0C060u : fill);
    set_rgb(s.cr, 0xFFFFFFu);
    cairo_set_line_width(s.cr, 2.0);
    cairo_rectangle(s.cr, r.x + 1, r.y + 1, r.w - 2, r.h - 2);
    cairo_stroke(s.cr);
    set_rgb(s.cr, armed ? 0x000000u : 0xFFFFFFu);
    const double tw = s.info->advance(label);
    s.info->draw(s.cr, r.x + (r.w - tw) / 2.0, r.y + r.h / 2.0 + kInfoPx * 0.36, label);
}

void paint(Spike& s) {
    cairo_t* cr = s.cr;
    if (!cr) return;

    // Every paint is opaque: the alpha byte lands 0xFF, which is what lets the
    // window take the buffer as RGBX without a blend pass.
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    set_rgb(cr, 0x101418u);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    const double margin = 24.0;
    double y = margin + kDemoPx;

    set_rgb(cr, 0xFFFFFFu);
    s.sans->draw(cr, margin, y, "warptempo M2 spike \xE2\x80\x94 cairo / harfbuzz / AAudio / OTG");
    y += s.sans->line_height();

    if (!s.font_error.empty()) {
        set_rgb(cr, 0xFF4040u);
        s.info->draw(cr, margin, y, ("FONT ERROR: " + s.font_error).c_str());
        y += s.info->line_height();
    }

    // --- 1. colour swatches. Each is LABELLED with the colour it is supposed to
    // be, so a channel swap is legible at a glance rather than inferred.
    struct Swatch { const char* name; uint32_t rgb; };
    static const Swatch swatches[] = {
        {"RED", 0xFF0000u},   {"GREEN", 0x00FF00u}, {"BLUE", 0x0000FFu},
        {"CYAN", 0x00FFFFu},  {"MAGENTA", 0xFF00FFu}, {"YELLOW", 0xFFFF00u},
        {"WHITE", 0xFFFFFFu}, {"GREY 50%", 0x808080u},
    };
    const double sw_w = 150.0;
    const double sw_h = 84.0;
    double sx = margin;
    for (const Swatch& sw : swatches) {
        fill_rect(cr, Rect{sx, y, sw_w - 8, sw_h}, sw.rgb);
        set_rgb(cr, 0xC8C8C8u);
        s.info->draw(cr, sx, y + sw_h + kInfoPx, sw.name);
        sx += sw_w;
    }
    y += sw_h + kInfoPx + 16.0;

    // --- 1b. 1px lines: a comb of single-pixel rules at growing gaps. Any scaling
    // between the backbuffer and the panel shows up here as dropped or doubled
    // lines long before it shows up in the text.
    set_rgb(cr, 0xFFFFFFu);
    const double comb_y = y;
    double lx = margin;
    for (int gap = 1; gap <= 6; ++gap) {
        for (int i = 0; i < 8; ++i) {
            cairo_rectangle(cr, std::floor(lx), comb_y, 1.0, 40.0);
            lx += 1.0 + gap;
        }
        lx += 24.0;
    }
    for (int i = 0; i < 8; ++i) {
        cairo_rectangle(cr, margin, comb_y + 48.0 + i * 4.0, 420.0, 1.0);
    }
    cairo_fill(cr);
    set_rgb(cr, 0xC8C8C8u);
    s.info->draw(cr, lx + 16.0, comb_y + 28.0, "1px rules: gaps 1..6 px, then 8 horizontals");
    y = comb_y + 48.0 + 8 * 4.0 + 24.0;

    // --- 1c. the two shaped lines, at a FIXED pixel size.
    set_rgb(cr, 0xFFFFFFu);
    s.sans->draw(cr, margin, y + kDemoPx,
                "Sans 48px  AVATAR Wavy To 0123 \xE2\x80\x94 fi fl \xC2\xAB quoted \xC2\xBB");
    y += s.sans->line_height();
    s.mono->draw(cr, margin, y + kDemoPx,
                "Mono 48px  ||||  0O1lI  #{}[]()<>  \xE2\x80\x94  |");
    y += s.mono->line_height() + 12.0;

    // --- 5. the run-loop facts, including the frame counter.
    set_rgb(cr, 0x90E0FFu);
    std::vector<std::string> lines;
    lines.push_back(fmt("frame %lld   %.1f fps   loop ALooper_pollOnce(8ms), no Choreographer",
                        s.frame, s.fps));
    lines.push_back(fmt("window %dx%d   buffer %dx%d  stride %d px  format %d (%s)  copy %s",
                        s.back_w, s.back_h, s.last_bufw, s.last_bufh, s.last_stride,
                        s.last_format, window_format_name(s.last_format),
                        s.swizzled ? "R<->B swizzle (NEON)" : "straight memcpy"));
    if (s.lock_failures > 0) {
        lines.push_back(fmt("ANativeWindow_lock failures: %d (last %d)", s.lock_failures,
                            s.last_lock_error));
    }
    for (const std::string& l : lines) {
        s.info->draw(cr, margin, y + kInfoPx, l.c_str());
        y += s.info->line_height();
    }

    // --- 3. audio.
    y += 8.0;
    set_rgb(cr, s.audio.playing() ? 0x80FF80u : 0xE0E0E0u);
    for (const std::string& l : s.audio.status_lines()) {
        s.info->draw(cr, margin, y + kInfoPx, l.c_str());
        y += s.info->line_height();
    }

    // --- 4. storage.
    y += 8.0;
    set_rgb(cr, 0xFFD080u);
    if (!s.storage.ran) {
        s.info->draw(cr, margin, y + kInfoPx, "OTG probe not run yet -- tap OTG PROBE");
        y += s.info->line_height();
    } else {
        for (const std::string& l : s.storage.lines) {
            s.info->draw(cr, margin, y + kInfoPx, l.c_str());
            y += s.info->line_height();
        }
    }

    // --- the two tap regions, bottom-left.
    const double bh = 84.0;
    const double bw = 320.0;
    s.play_rect = Rect{margin, static_cast<double>(s.back_h) - margin - bh, bw, bh};
    s.probe_rect = Rect{margin + bw + 24.0, static_cast<double>(s.back_h) - margin - bh, bw, bh};
    draw_button(s, s.play_rect, s.audio.playing() ? "STOP WAV" : "PLAY WAV", s.play_armed, 0x204060u);
    draw_button(s, s.probe_rect, "OTG PROBE", s.probe_armed, 0x604020u);

    // --- 2. touch echo, painted last so it is never occluded.
    for (const Touch& t : s.touches) {
        if (!t.active) continue;
        set_rgb(cr, 0x40FF90u);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
        cairo_arc(cr, t.x, t.y, 44.0, 0.0, 2.0 * M_PI);
        cairo_set_line_width(cr, 4.0);
        cairo_stroke(cr);
        cairo_arc(cr, t.x, t.y, 6.0, 0.0, 2.0 * M_PI);
        cairo_fill(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        const std::string label = fmt("id %d  %.0f,%.0f", t.id, t.x, t.y);
        s.info->draw(cr, t.x + 52.0, t.y + kInfoPx * 0.4, label.c_str());
    }

    cairo_surface_flush(s.back);
}

// ----------------------------------------------------------------------- blit

void present(Spike& s) {
    if (!s.window || !s.back) return;

    ANativeWindow_Buffer buf;
    // NULL dirty rect = the whole buffer. The spike repaints everything every
    // frame, so there is no damage set to union with the rect lock would hand
    // back; the product's port is where that machinery earns its keep.
    const int r = ANativeWindow_lock(s.window, &buf, nullptr);
    if (r != 0) {
        s.lock_failures++;
        s.last_lock_error = r;
        return;
    }

    s.last_format = buf.format;
    s.last_stride = buf.stride;
    s.last_bufw = buf.width;
    s.last_bufh = buf.height;

    // RGBA/RGBX both need the swap; a BGRA window (HAL-only, not in the NDK enum)
    // would already match cairo and take the plain copy.
    const bool swap = (buf.format == WINDOW_FORMAT_RGBA_8888 || buf.format == WINDOW_FORMAT_RGBX_8888);
    s.swizzled = swap;

    const int src_stride_px = cairo_image_surface_get_stride(s.back) / 4;
    const uint32_t* src = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(s.back));
    uint32_t* dst = static_cast<uint32_t*>(buf.bits);

    const int h = buf.height < s.back_h ? buf.height : s.back_h;
    const int w = buf.width < s.back_w ? buf.width : s.back_w;
    for (int yy = 0; yy < h; ++yy) {
        const uint32_t* srow = src + static_cast<size_t>(yy) * src_stride_px;
        uint32_t* drow = dst + static_cast<size_t>(yy) * buf.stride;   // stride is in PIXELS
        if (swap) {
            copy_swap_rb(drow, srow, w);
        } else {
            std::memcpy(drow, srow, static_cast<size_t>(w) * 4);
        }
    }

    ANativeWindow_unlockAndPost(s.window);
}

// ---------------------------------------------------------------------- input

void clear_touches(Spike& s) {
    for (Touch& t : s.touches) t.active = false;
}

void set_touch(Spike& s, int32_t id, float x, float y) {
    for (Touch& t : s.touches) {
        if (t.active && t.id == id) { t.x = x; t.y = y; return; }
    }
    for (Touch& t : s.touches) {
        if (!t.active) { t.active = true; t.id = id; t.x = x; t.y = y; return; }
    }
}

void drop_touch(Spike& s, int32_t id) {
    for (Touch& t : s.touches) {
        if (t.active && t.id == id) { t.active = false; return; }
    }
}

void hit_regions(Spike& s, double x, double y) {
    if (s.play_rect.contains(x, y)) {
        s.play_armed = true;
        s.audio.toggle();
    } else if (s.probe_rect.contains(x, y)) {
        s.probe_armed = true;
        spike_storage_probe(s.app->activity, s.storage);
    }
}

int32_t on_input(android_app* app, AInputEvent* ev) {
    Spike& s = *static_cast<Spike*>(app->userData);
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) {
        // Keys are left to the system so BACK still leaves the app.
        return 0;
    }

    const int32_t action = AMotionEvent_getAction(ev);
    const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
    const int32_t index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                          AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    const size_t count = AMotionEvent_getPointerCount(ev);

    switch (masked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN: {
            const int32_t id = AMotionEvent_getPointerId(ev, static_cast<size_t>(index));
            const float x = AMotionEvent_getX(ev, static_cast<size_t>(index));
            const float y = AMotionEvent_getY(ev, static_cast<size_t>(index));
            set_touch(s, id, x, y);
            // The spike's regions act AT THE PRESS: a tap here can only mean one
            // thing, so there is nothing to defer to a lift.
            hit_regions(s, x, y);
            break;
        }
        case AMOTION_EVENT_ACTION_MOVE:
            for (size_t i = 0; i < count; ++i) {
                set_touch(s, AMotionEvent_getPointerId(ev, i), AMotionEvent_getX(ev, i),
                          AMotionEvent_getY(ev, i));
            }
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            drop_touch(s, AMotionEvent_getPointerId(ev, static_cast<size_t>(index)));
            s.play_armed = false;
            s.probe_armed = false;
            break;
        case AMOTION_EVENT_ACTION_CANCEL:
            clear_touches(s);
            s.play_armed = false;
            s.probe_armed = false;
            break;
        default:
            break;
    }
    return 1;
}

// ------------------------------------------------------------------ lifecycle

void on_cmd(android_app* app, int32_t cmd) {
    Spike& s = *static_cast<Spike*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                s.window = app->window;  // borrowed, never acquired
                ANativeWindow_setBuffersGeometry(s.window, 0, 0, WINDOW_FORMAT_RGBA_8888);
                ensure_backbuffer(s, ANativeWindow_getWidth(s.window),
                                  ANativeWindow_getHeight(s.window));
                SPIKE_LOGI("INIT_WINDOW %dx%d", s.back_w, s.back_h);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            SPIKE_LOGI("TERM_WINDOW");
            s.window = nullptr;
            s.audio.stop();
            clear_touches(s);
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            if (s.window) {
                ensure_backbuffer(s, ANativeWindow_getWidth(s.window),
                                  ANativeWindow_getHeight(s.window));
            }
            break;
        case APP_CMD_LOST_FOCUS:
            clear_touches(s);
            break;
        case APP_CMD_PAUSE:
            s.audio.stop();
            break;
        case APP_CMD_LOW_MEMORY:
            SPIKE_LOGW("APP_CMD_LOW_MEMORY");
            break;
        default:
            break;
    }
}

}  // namespace

void android_main(android_app* app) {
    // android_main RUNS AGAIN if the activity is destroyed and remade -- USB attach
    // is a documented trigger, which is exactly the scenario this port exists for.
    // The whole state is therefore a LOCAL: a second entry gets a fresh one and the
    // first one's cairo surfaces, FT faces and AAudio stream are already released.
    Spike s;
    s.app = app;
    app->userData = &s;
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;

    ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);

    if (!load_fonts(s)) SPIKE_LOGE("font setup failed: %s", s.font_error.c_str());
    load_audio_asset(s);

    s.fps_mark = now_seconds();

    bool leaving = false;
    while (!leaving) {
        // The FIRST poll of an iteration waits up to 8 ms; the rest drain what is
        // already pending with a zero timeout, so a busy event source can never
        // starve the repaint. ALooper_pollAll was removed in r27+; pollOnce is the
        // one spelling, and every return value must be treated as possibly a WAKE.
        int timeout = 8;
        int events = 0;
        android_poll_source* source = nullptr;
        while (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source != nullptr) source->process(app, source);
            source = nullptr;
            timeout = 0;
            if (app->destroyRequested != 0) { leaving = true; break; }
        }
        if (leaving) break;

        if (s.window && s.back) {
            paint(s);
            present(s);
            s.frame++;

            const double t = now_seconds();
            if (t - s.fps_mark >= 0.5) {
                s.fps = static_cast<double>(s.frame - s.fps_frame_mark) / (t - s.fps_mark);
                s.fps_mark = t;
                s.fps_frame_mark = s.frame;
            }
        }
    }

    // Drop every borrowed handle before the state dies, and unhook the callbacks so
    // the glue's own teardown cannot reach a destroyed Spike.
    s.audio.stop();
    s.window = nullptr;
    app->onAppCmd = nullptr;
    app->onInputEvent = nullptr;
    app->userData = nullptr;
}
