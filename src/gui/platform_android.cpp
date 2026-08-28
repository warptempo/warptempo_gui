#include "platform_android.h"

#include "gui_font.h"
#include "gui_main.h"
#include "render.h"   // kRedesignContentGround, the band fill

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/window.h>
#include <android_native_app_glue.h>

#include <arm_neon.h>

#include <fcntl.h>
#include <pthread.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// THE ANDROID BACKEND. What is here is class A of the seam — the mechanics:
// the glue's lifecycle, the ALooper run loop, the cairo -> ANativeWindow blit,
// the AMotionEvent decode and this platform's stubs. THE WINDOW IT PRESENTS TO
// THE GUI IS THE CONTENT RECT, not the surface: the surface is the whole panel
// on this platform and the band inside the system bars arrives separately, so
// this file adds the origin at the blit and subtracts it at the touch decode
// and nothing above the seam knows either exists (the rule is at origin_x_,
// platform_android.h). The POLICY (the touch
// state machine, the key-repeat synthesis, the logical pointer, the notional-x
// bookkeeping, the containment conversion) is in GuiInputCore and is shared
// verbatim with the Wayland backend; every input event decoded below is handed
// to that object in plain window-pixel doubles.
//
// android_main lives at the bottom of this file: it is this platform's entry
// point and calls the one portable GUI body, gui_main (gui_main.h).

// ---------------------------------------------------------------------------
// The logcat sink for the GUI's own diagnostics
// ---------------------------------------------------------------------------

// EVERY DIAGNOSTIC THIS PROGRAM WRITES GOES TO stderr, in ~200 fprintf sites
// spread across the GUI, the parser and the engine — none of which knows what
// platform it is on, and none of which should. On Android a bare stderr write
// goes to /dev/null, so the whole diagnostic surface would be silently gone.
// The fix is at the FILE DESCRIPTOR, not at the call sites: stdout and stderr
// are redirected onto a pipe and one detached thread turns each line into an
// __android_log_write. That is the smallest thing that keeps every existing
// message, and it costs one thread that spends its life blocked in read().
namespace {

constexpr const char* kLogTag = "warptempo";

void* log_pump(void* arg) {
    const int read_fd = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    std::string line;
    char        buf[512];
    for (;;) {
        const ssize_t n = read(read_fd, buf, sizeof(buf));
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                __android_log_write(ANDROID_LOG_INFO, kLogTag, line.c_str());
                line.clear();
            } else if (line.size() < 4096) {
                line.push_back(buf[i]);
            }
        }
    }
    if (!line.empty()) {
        __android_log_write(ANDROID_LOG_INFO, kLogTag, line.c_str());
    }
    // EOF means every writer is gone and this pump is finished; the read end
    // is this thread's to close.
    close(read_fd);
    return nullptr;
}

// THE SINK IS THE PROCESS'S, NOT THE ACTIVITY'S. android_main runs again if
// the activity is destroyed and remade, and a second pipe would strand the
// first: replacing stdout and stderr closes the old pipe's last writers, the
// old pump reaches EOF and exits, and one more read fd is gone for each
// recreation. One sink installed once outlives every entry — the second
// android_main finds stdout and stderr already pointing at a pipe whose thread
// is still blocked in read(), and there is nothing left to do.
void route_stdio_to_logcat() {
    static bool installed = false;
    if (installed) return;

    int fds[2];
    if (pipe(fds) != 0) return;
    // Line-buffer both streams so a message reaches the pump at its newline
    // rather than when a 4 KB block fills.
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IOLBF, 0);
    // The originals are kept only so the failure arm below can put them back.
    const int saved_out = dup(STDOUT_FILENO);
    const int saved_err = dup(STDERR_FILENO);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    pthread_t t;
    if (pthread_create(&t, nullptr, log_pump,
                       reinterpret_cast<void*>(
                           static_cast<intptr_t>(fds[0]))) != 0) {
        // NO READER MEANS THE REDIRECTION MUST COME BACK OUT. Left as it is,
        // every diagnostic in the program would write into a pipe nobody
        // drains, and the GUI thread would block for good on the fprintf that
        // fills the last of the pipe's buffer. Restoring the descriptors costs
        // the logcat sink and keeps the program running.
        if (saved_out >= 0) dup2(saved_out, STDOUT_FILENO);
        if (saved_err >= 0) dup2(saved_err, STDERR_FILENO);
        close(fds[0]);
        if (saved_out >= 0) close(saved_out);
        if (saved_err >= 0) close(saved_err);
        return;
    }
    pthread_detach(t);
    if (saved_out >= 0) close(saved_out);
    if (saved_err >= 0) close(saved_err);
    installed = true;
}

// ---------------------------------------------------------------------------
// The glue state, parked for the GuiPlatform the GUI body constructs
// ---------------------------------------------------------------------------

// THE ONE FILE-SCOPE POINTER, and why it has to exist: gui_main constructs its
// GuiPlatform as an ordinary local, with the same signature on both platforms,
// so there is no argument through which android_main could hand it the glue's
// `android_app*`. android_main parks it here BEFORE calling gui_main and
// init() adopts it; nothing else reads or writes it. The lifetime is trivially
// safe — android_main's own frame outlives gui_main's — and a second entry
// (android_main runs again if the activity is destroyed and remade) overwrites
// it before the second gui_main runs.
android_app* g_android_app = nullptr;

// ---------------------------------------------------------------------------
// Damage coalescing — the Wayland backend's rule, spelled the same way
// ---------------------------------------------------------------------------

template <typename Rect>
bool contains_rect(const Rect& outer, const Rect& inner) {
    return outer.x <= inner.x &&
           outer.y <= inner.y &&
           outer.x + outer.w >= inner.x + inner.w &&
           outer.y + outer.h >= inner.y + inner.h;
}

template <typename Rect>
bool append_coalesced_rect(std::vector<Rect>& rects, const Rect& nr) {
    for (const Rect& e : rects) {
        if (contains_rect(e, nr)) return false;
    }
    rects.push_back(nr);
    rects.erase(
        std::remove_if(rects.begin(), rects.end() - 1,
                       [&](const Rect& e) { return contains_rect(nr, e); }),
        rects.end() - 1);
    return true;
}

// ---------------------------------------------------------------------------
// The blit's channel swap
// ---------------------------------------------------------------------------

// cairo's ARGB32 is a NATIVE-ENDIAN 0xAARRGGBB word, so on this little-endian
// target its bytes run B,G,R,A, while WINDOW_FORMAT_RGBA_8888 / RGBX_8888 run
// R,G,B,A. R and B are transposed and the copy fixes it. Every buffer this
// backend accepts is one of those two — the format the window ACTUALLY hands
// back is checked at each lock (present), never assumed from what was
// requested — so the swap is on every row of every frame.
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

// ---------------------------------------------------------------------------
// The bands outside the content rect
// ---------------------------------------------------------------------------

// THE BAND WORDS, in the WINDOW's own byte order. The bands are whatever the
// surface holds outside the content rect — above it and below it, each existing
// only insofar as the framework's rect leaves room for it (on the Tab S10 FE's
// 2026-08-27 measurement the rect started at y=53, under the status bar, and
// ran to the panel's bottom edge, so only the top one had any rows; the air
// below widens that top band by kStatusBarAirPx). The rows under a system
// window are covered by it, so nothing of ours is meant to be seen there — but
// a translucent bar over a buffer nobody wrote would show a stale frame, so
// they are filled; the air's own rows are meant to be seen. These pixels never
// pass through the backbuffer, so they never meet copy_swap_rb's swap either:
// the word is built R,G,B,A directly, from the palette constants rather than
// from a second spelling of their hex (render.h is the one color owner; a
// retune follows).
constexpr uint32_t window_word(GuiColor c) {
    const auto ch = [](double v) {
        return static_cast<uint32_t>(v * 255.0 + 0.5) & 0xFFu;
    };
    return 0xFF000000u | (ch(c.b) << 16) | (ch(c.g) << 8) | ch(c.r);
}

// TWO WORDS, and which one a band row takes is decided at the row (present()).
// The TOP band is the title strip's ground; every other band pixel — a bottom
// band, or a side band beside content rows — is the content's own.
constexpr uint32_t kBandWord    = window_word(kRedesignContentGround);
constexpr uint32_t kTopBandWord = window_word(kRedesignRowGround);

void fill_band(uint32_t* dst, int n, uint32_t word) {
    for (int x = 0; x < n; ++x) dst[x] = word;
}

// THE AIR UNDER THE STATUS BAR (architect 2026-08-27, on glass, measured on a
// screencap at his Screen zoom = override density 320): One UI does NOT
// centre the status bar's content in the inset it reports (inset 60 device
// px; the tallest element, the clock, occupies rows 23-52 — 23 above it, 7
// below). It sets the distance from the screen's top edge to the TOPMOST
// element (the G icon, row 21) and MIRRORS that distance below: the gap from
// the BOTTOMMOST element's last row (52) to our window's first row equals it
// too, so `inset + air - 53 = 21` -> air 14 (the 53 being the content rect's
// own top, before the air is added). The number is density-dependent — a
// different Screen zoom re-measures it; THE RETUNE KNOB IS THIS NUMBER and
// nothing else. DEVICE pixels, deliberately: the air pairs with the status
// bar's own density-scaled geometry, not with anything gui_scale sizes.
//
// IT PAINTS kTopBandWord, not the content ground. The status bar above it is
// kRedesignRowGround (the Java sliver sets it, from the labwc title bar the
// architect names as the color he expects) and the MENU ROW directly beneath it
// is the same ground, so a content-ground band between two identical grounds
// would read as a darker stripe — a defect, not air. Filled with the row
// ground, the status bar, the air and the menu row read as ONE title strip: the
// clock at its top, the menus beneath it, which is kdenlive's own arrangement.
constexpr int kStatusBarAirPx = 14;

// ---------------------------------------------------------------------------
// The looper identifiers this backend adds
// ---------------------------------------------------------------------------

// LOOPER_ID_MAIN (1) and LOOPER_ID_INPUT (2) are the glue's; everything from
// LOOPER_ID_USER (3) up is ours. The four worker slots are contiguous so the
// dispatch can index them, and the ORDER they are dispatched in is the Wayland
// loop's order (async renderer, waveform, checkpoint, prefetch).
constexpr int kIdentTimer   = LOOPER_ID_USER;       // 3
constexpr int kIdentWorker0 = LOOPER_ID_USER + 1;   // 4..7
constexpr int kWorkerCount  = 4;

} // namespace

// ---------------------------------------------------------------------------
// The glue's C callback table
// ---------------------------------------------------------------------------

struct AndroidCallbacks {
    static void app_cmd(android_app* app, int32_t cmd) {
        auto* self = static_cast<GuiPlatform*>(app->userData);
        if (self) self->on_app_cmd(cmd);
    }
    static int32_t input_event(android_app* app, AInputEvent* event) {
        auto* self = static_cast<GuiPlatform*>(app->userData);
        return self ? self->on_input_event(event) : 0;
    }
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

GuiPlatform::GuiPlatform() {}

GuiPlatform::~GuiPlatform() {
    shutdown();
}

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------

// The tablet's device-config template (contract at the declaration, rationale
// at this backend's own). 225 % IS THE ARCHITECT'S OWN ANSWER, settled on the
// glass 2026-08-27: it is the scale that reproduces the retired rig's 1024
// logical pixels on this 249 PPI panel (2304/2.25 = 1024), which is the layout
// the whole redesign was drawn against — every icon in the row fits, where the
// fit ceiling is 249 % and anything past it crops the rightmost history icons.
// 250 was tried for one afternoon that same day for the finger's sake — a
// marker flag has to be tappable without the second tap of a double-tap landing
// on the waveform instead — and stepped back that evening: it was one step too
// far for a ~3 authored px crop, and with the press-road thresholds now scaling
// with gui_scale (and the double-click window on the product's one beat, same
// evening) the double-tap holds together at 225 anyway. The BLANK player
// beside it is the real answer for a device with nothing spawnable on it rather
// than a placeholder.
DeviceConfig GuiPlatform::device_config_defaults() {
    DeviceConfig cfg;
    cfg.gui_scale    = 225;
    cfg.audio_player = "";
    return cfg;
}

bool GuiPlatform::init(int width, int height, const char* /*title*/) {
    app_ = g_android_app;
    if (!app_) {
        std::fprintf(stderr,
                     "warptempo_gui: no android_app; android_main did not run\n");
        return false;
    }
    app_->userData     = this;
    app_->onAppCmd     = AndroidCallbacks::app_cmd;
    app_->onInputEvent = AndroidCallbacks::input_event;

    // The panel's own size is the truth; the caller's advisory pair is the
    // cold answer until a window is adopted below.
    width_  = width;
    height_ = height;
    input_.set_surface_width(width_);

    // THE KEY-REPEAT CADENCE IS HARD-CODED HERE (architect ruling 2026-08-23).
    // Android advertises no repeat rate to a native activity — there is no
    // wl_keyboard.repeat_info counterpart and no system setting a native app
    // can read — so the numbers are labwc's own, taken BY CONVENTION: the
    // delay is kHoldBeatMs (575, gui_input.h, the same constant every product
    // hold threshold reads) and the rate is 30 Hz, a 33 ms period. They pace
    // the chrome buttons' hold-repeat through key_repeat_period_ms(), and
    // whatever on-screen keyboard this backend comes to own.
    input_.set_repeat_info(30, kHoldBeatMs);

    // THE CORE'S CODEPOINT REFILL, the one probe pointing DOWNWARD across the
    // seam (contract at GuiInputCore::set_codepoint_probe). The Wayland
    // backend re-resolves the held key against the live xkb state; this one
    // answers from the table synthesize_key fills, since a synthesized key
    // carries its own character and nothing under it can move. An unknown code
    // answers 0, which is what a key that produces no character already means.
    input_.set_codepoint_probe([this](uint32_t stable_code) -> uint32_t {
        const auto it = key_codepoints_.find(stable_code);
        return it == key_codepoints_.end() ? 0u : it->second;
    });

    timerfd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerfd_ < 0) {
        std::fprintf(stderr, "warptempo_gui: timerfd_create failed: %s\n",
                     std::strerror(errno));
        return false;
    }
    if (!arm_playback_timer()) return false;
    watch_fd(timerfd_, kIdentTimer);

    // The screen stays lit: this is a kiosk instrument on a stand, and the
    // whole implementation of that is one flag.
    ANativeActivity_setWindowFlags(app_->activity,
                                   AWINDOW_FLAG_KEEP_SCREEN_ON, 0);

    // ADOPT THE WINDOW THAT IS ALREADY THERE. android_main pumps the glue
    // until APP_CMD_INIT_WINDOW has landed, so this is the ordinary path and
    // the on_app_cmd arm is the one that runs after a TERM/INIT cycle. The
    // resize CALLBACK is owed rather than made (initial_resize_owed_).
    if (app_->window) adopt_window(/*fire_resize=*/false);

    return true;
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

void GuiPlatform::adopt_window(bool fire_resize) {
    window_ = app_->window;
    if (!window_) return;

    // 0,0 for the geometry means "the window's own size"; the FORMAT is the
    // one thing asked for. A REFUSAL IS REPORTED AND NOT FATAL HERE, because
    // it is not yet the question that matters: the window may still hand back
    // a 32-bit buffer of its own choosing. The gate sits where the truth is,
    // at the lock that reads the format back (present).
    const int32_t geom_rc =
        ANativeWindow_setBuffersGeometry(window_, 0, 0, WINDOW_FORMAT_RGBA_8888);
    if (geom_rc != 0) {
        std::fprintf(stderr,
                     "warptempo_gui: ANativeWindow_setBuffersGeometry"
                     "(RGBA_8888) refused (%d)\n", static_cast<int>(geom_rc));
    }

    // PIN THE PANEL TO 90 Hz (architect 2026-08-27). One device, one rate: the
    // Tab S10 FE's panel is variable-refresh and the system otherwise picks a
    // rate from what it sees the app doing, which for a program that paints
    // only on damage reads as "hardly ever" and lands on 60. The playhead and
    // the waveform under a drag are the surfaces that show the difference.
    //
    // FIXED_SOURCE is the honest compatibility: this is not a video player with
    // frames arriving at a fixed cadence that the panel should divide into — it
    // is a program that would like this rate — but FIXED_SOURCE is the value
    // that asks the display to run AT the number rather than at some multiple
    // it finds convenient, which is what the pin is for.
    //
    // THE RESULT IS LOGGED AND NOTHING ELSE: it is a REQUEST, the system is
    // free to refuse it (a battery saver, a thermal cap, another window), and
    // a refusal costs frames rather than correctness — so there is no error arm
    // and no fallback, exactly as the buffers-geometry refusal above has none.
    // API 30 is minSdk, and this symbol landed in 30, so no availability guard
    // is owed.
    {
        const int32_t rate_rc = ANativeWindow_setFrameRate(
            window_, 90.0f, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
        std::fprintf(stderr,
                     "warptempo_gui: ANativeWindow_setFrameRate(90, "
                     "FIXED_SOURCE) -> %d\n", static_cast<int>(rate_rc));
    }

    // THE SURFACE, which is not the window the GUI sees. On this platform the
    // app window's frame is the whole display and the band inside the system
    // bars arrives separately, as the content rect (the rule is at origin_x_,
    // platform_android.h).
    const int surf_w = ANativeWindow_getWidth(window_);
    const int surf_h = ANativeWindow_getHeight(window_);
    if (surf_w <= 0 || surf_h <= 0) {
        std::fprintf(stderr,
                     "warptempo_gui: ANativeWindow reported %dx%d; "
                     "painting suspended\n", surf_w, surf_h);
        window_ = nullptr;
        return;
    }

    // EVERY ADOPTION RE-READS THE RECT, which is what makes one function
    // answer for all four commands that reach it: INIT_WINDOW, WINDOW_RESIZED,
    // CONFIG_CHANGED and CONTENT_RECT_CHANGED. A bar coming or going moves the
    // rect without moving the surface, and a rotation would move both.
    int ox = 0, oy = 0, cw = surf_w, ch = surf_h;
    resolve_content_rect(surf_w, surf_h, ox, oy, cw, ch);

    // THE GEOMETRY EDGE, which is not the same thing as an adoption: the glue
    // posts INIT_WINDOW, WINDOW_RESIZED and CONFIG_CHANGED for a
    // landscape-locked activity whose surface never changes size, so most
    // adoptions move nothing. A moved ORIGIN counts as a move even at an
    // unchanged size: every pixel the GUI paints lands somewhere else.
    // Only a real move owes the resize callback (which force-ends every
    // pointer gesture, closes the dropdown and rebuilds the layout) and the
    // one startup line.
    const bool moved = (cw != width_ || ch != height_ ||
                        ox != origin_x_ || oy != origin_y_ ||
                        !has_initial_configure_);

    surface_w_ = surf_w;
    surface_h_ = surf_h;
    origin_x_  = ox;
    origin_y_  = oy;
    width_     = cw;
    height_    = ch;
    ensure_backbuffer(width_, height_);
    input_.set_surface_width(width_);
    has_initial_configure_ = true;

    if (moved) {
        if (fire_resize) {
            if (on_resize_) on_resize_(width_, height_);
        } else {
            initial_resize_owed_ = true;
        }
        std::fprintf(stderr,
                     "warptempo_gui: window %dx%d at (%d,%d) of surface "
                     "%dx%d, tick %d ms\n",
                     width_, height_, origin_x_, origin_y_,
                     surface_w_, surface_h_, playback_tick_ms_);
    }
    // FULL DAMAGE ON EVERY ADOPTION, moved or not: the window hands back
    // buffers whose content is a lost frame's, so the first post after any
    // adoption has to carry the whole picture — and the bands with it, which
    // is what the owed full-surface post below is for.
    surface_bands_owed_ = true;
    invalidate_region(0, 0, width_, height_);
}

/*
 * THE CONTENT RECT, resolved against the surface. The glue keeps
 * android_app::contentRect current (it writes it under its own mutex on the UI
 * thread and posts APP_CMD_CONTENT_RECT_CHANGED; reading the latest value here
 * is the documented use) and ZERO-INITIALISES it, so before the first callback
 * there is no rect at all — the fallback is the whole surface, which is
 * exactly what this backend did before the rect was read.
 *
 * EVERY DEGENERATE ANSWER TAKES THE SAME FALLBACK rather than an error arm:
 * an inverted or empty rect, or one that does not intersect the surface, would
 * otherwise hand the GUI a window of nothing. There is no producer for those
 * on this device; the fallback exists because the alternative is a black app,
 * not because a fault is expected.
 *
 * THE ONE THING THIS FUNCTION ADDS TO THE FRAMEWORK'S ANSWER is the air under
 * the status bar (kStatusBarAirPx, at the end of the body), and it is added
 * here so that origin, size, damage and every touch coordinate follow from it
 * for free.
 */
void GuiPlatform::resolve_content_rect(int surf_w, int surf_h,
                                       int& ox, int& oy,
                                       int& cw, int& ch) const {
    ox = 0;
    oy = 0;
    cw = surf_w;
    ch = surf_h;
    if (!app_) return;

    const ARect r = app_->contentRect;
    const int left   = std::max(0, std::min(static_cast<int>(r.left),   surf_w));
    const int top    = std::max(0, std::min(static_cast<int>(r.top),    surf_h));
    const int right  = std::max(0, std::min(static_cast<int>(r.right),  surf_w));
    const int bottom = std::max(0, std::min(static_cast<int>(r.bottom), surf_h));
    if (right <= left || bottom <= top) return;

    ox = left;
    oy = top;
    cw = right - left;
    ch = bottom - top;

    // THE AIR, ADDED TO THE TOP INSET AND ONLY THERE (kStatusBarAirPx, with the
    // reasoning and the color at its declaration). A rect that starts at y == 0
    // has no status bar over it — a fullscreen future — and gets no air: a
    // blank band at the top of the panel would be nothing but lost picture. The
    // rows given up become the top band, and because origin, size, damage and
    // every touch coordinate follow from this one function, nothing else in the
    // backend knows the air exists. A rect too short to give the air up keeps
    // its whole height, the same fallback every degenerate answer above takes.
    if (top > 0 && top + kStatusBarAirPx < bottom) {
        oy = top + kStatusBarAirPx;
        ch = bottom - oy;
    }
}

/*
 * THE OWED FIRST on_resize_, delivered by whoever gets there first. init()
 * adopts a window before main.cpp has wired a single hook, so the geometry is
 * taken then and the CALLBACK is owed (initial_resize_owed_). Every path that
 * can paint calls this before it paints — pump()'s head, and paint_one_frame,
 * which is the funnel paint_now() and drain_events() both go through — so the
 * first buffer this process posts can never carry the layout computed from
 * AppState's cold default size.
 */
void GuiPlatform::deliver_owed_resize() {
    if (!initial_resize_owed_) return;
    initial_resize_owed_ = false;
    if (on_resize_) on_resize_(width_, height_);
}

void GuiPlatform::ensure_backbuffer(int w, int h) {
    if (back_ && back_w_ == w && back_h_ == h) return;
    destroy_backbuffer();
    back_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(back_) != CAIRO_STATUS_SUCCESS) {
        std::fprintf(stderr,
                     "warptempo_gui: backbuffer %dx%d could not be created\n",
                     w, h);
        cairo_surface_destroy(back_);
        back_ = nullptr;
        return;
    }
    back_w_ = w;
    back_h_ = h;
}

void GuiPlatform::destroy_backbuffer() {
    if (back_) cairo_surface_destroy(back_);
    back_   = nullptr;
    back_w_ = 0;
    back_h_ = 0;
}

int GuiPlatform::width()  const { return width_; }
int GuiPlatform::height() const { return height_; }

// ---------------------------------------------------------------------------
// The title — no surface on this platform
// ---------------------------------------------------------------------------

// A NativeActivity under Theme.NoTitleBar has no titlebar and no
// task-switcher string a native app can rewrite, so the classic application
// form the Wayland backend composes ("K551 * - warptempo_gui") has nowhere to
// go. Both setters are therefore silent no-ops. They are not deleted because
// the GUI calls them unconditionally from the load path and from every
// dirty-state transition, and the seam's promise is that a consumer compiles
// against either backend unchanged.
void GuiPlatform::set_project_title(std::string /*project_name*/) {}
void GuiPlatform::set_title_dirty(bool /*dirty*/) {}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

void GuiPlatform::shutdown() {
    // Unhook first so the glue's own teardown cannot reach a dismantled
    // object, then drop everything this class owns. Idempotent: the
    // destructor calls it after gui_main's explicit call.
    if (app_ && app_->userData == this) {
        app_->onAppCmd     = nullptr;
        app_->onInputEvent = nullptr;
        app_->userData     = nullptr;
    }
    if (timerfd_ >= 0) {
        unwatch_fd(timerfd_);
        close(timerfd_);
        timerfd_ = -1;
    }
    unwatch_fd(worker_completion_fd_);
    unwatch_fd(waveform_worker_completion_fd_);
    unwatch_fd(history_worker_completion_fd_);
    unwatch_fd(history_prefetch_completion_fd_);
    worker_completion_fd_           = -1;
    waveform_worker_completion_fd_  = -1;
    history_worker_completion_fd_   = -1;
    history_prefetch_completion_fd_ = -1;
    destroy_backbuffer();
    window_ = nullptr;
    app_    = nullptr;
}

void GuiPlatform::request_exit() {
    should_exit_ = true;
    // ALSO ASK THE ACTIVITY TO GO. run() returning is only half of a quit
    // here: android_main returns into the glue, which would sit with a live
    // activity and no loop. finish() makes the system tear the activity down,
    // which is what the user asked for when they pressed Quit.
    if (app_ && app_->activity) ANativeActivity_finish(app_->activity);
}

// ---------------------------------------------------------------------------
// Damage and painting
// ---------------------------------------------------------------------------

void GuiPlatform::invalidate_region(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;

    // Never called from inside the paint loop (the hazard and the supported
    // pre-paint route are stated at that loop, paint_one_frame).

    // Each surviving rect costs one on_redraw call downstream, so the damage
    // signal uses the same containment coalescing the Wayland backend uses.
    // THERE IS NO PER-BUFFER LIST HERE: the backbuffer is persistent, so the
    // one accumulator is the whole story and a rect never has to be replayed
    // for a second buffer that missed it.
    const DamageRect nr{x, y, w, h};
    append_coalesced_rect(damage_, nr);

    // Nothing is scheduled: the loop's own settled tail paints whatever has
    // accumulated, and the periodic tick guarantees a pass within one tick
    // period. (The Wayland backend must arm a frame callback here because its
    // paint is the compositor's to invite.)
}

/*
 * Paint every damaged rect into the persistent backbuffer, then post their
 * bounding box. THE POST IS ONE RECT rather than the list because
 * ANativeWindow_lock takes a single dirty region and is free to widen it; the
 * backbuffer holds the whole frame, so the copy is correct for whatever it
 * hands back.
 */
void GuiPlatform::paint_one_frame() {
    // BEFORE ANY PIXEL: the owed first resize, so the layout the frame is
    // painted from is the panel's and not the cold default. It runs ahead of
    // the guards below because the hook it fires declares its own damage.
    deliver_owed_resize();

    if (!has_initial_configure_ || !window_ || !back_) return;
    if (damage_.empty()) return;

    // Pre-paint hook: gives the application a chance to update model state
    // (e.g. re-read the playback predictor) and declare additional damage
    // based on the freshly-updated state. It runs BEFORE the damage list is
    // read precisely so it may add to it; invalidate_region needs no
    // suppression here (unlike the Wayland twin's, it schedules nothing).
    // (No in_pre_paint_ guard, unlike the Wayland twin's: there is nothing to
    // suppress. That flag exists there to stop invalidate_region arming a
    // second frame callback mid-pre-paint, and this backend's
    // invalidate_region arms nothing at all.)
    if (on_pre_paint_) on_pre_paint_();

    // THE PAINT LOOP MUST NOT DECLARE DAMAGE. It range-fors over damage_ — the
    // same vector invalidate_region appends to — so an invalidate_region call
    // from inside on_redraw would push_back into the vector being iterated and
    // invalidate the loop. No paint pass does: painting is pure pixel
    // production, and anything that needs to declare damage around a frame
    // does it BEFORE this loop through the pre-paint hook above, or from
    // ordinary event/tick code after the frame.
    int lo_x = width_, lo_y = height_, hi_x = 0, hi_y = 0;
    cairo_t* cr = cairo_create(back_);
    for (const DamageRect& d : damage_) {
        cairo_save(cr);
        cairo_rectangle(cr, d.x, d.y, d.w, d.h);
        cairo_clip(cr);
        if (on_redraw_) on_redraw_(cr, d.x, d.y, d.w, d.h);
        cairo_restore(cr);
        lo_x = std::min(lo_x, d.x);
        lo_y = std::min(lo_y, d.y);
        hi_x = std::max(hi_x, d.x + d.w);
        hi_y = std::max(hi_y, d.y + d.h);
    }
    cairo_destroy(cr);
    cairo_surface_flush(back_);

    // THE DAMAGE LIVES UNTIL THE POST SUCCEEDS. The pixels are in the
    // backbuffer either way, but the damage list is the only record of what
    // the WINDOW has not yet been shown: clearing it on a failed lock or a
    // failed post would leave the displayed buffer stale with nothing left to
    // re-post it, and a later, disjoint invalidation would never copy the lost
    // rectangle. Kept, it simply goes out again with the next frame.
    if (present(lo_x, lo_y, hi_x - lo_x, hi_y - lo_y)) damage_.clear();
}

bool GuiPlatform::present(int x, int y, int w, int h) {
    if (!window_ || !back_ || w <= 0 || h <= 0) return false;

    // THE ORIGIN IS ADDED HERE AND NOWHERE ELSE ON THE WAY OUT. Everything
    // above this line — the damage list, the backbuffer, every rect the GUI
    // ever named — is in CONTENT coordinates; the window wants SURFACE ones.
    //
    // AND THE OWED POST IS THE WHOLE SURFACE, once per adoption, so the two
    // bands outside the content rect are written at least once with the
    // product's ground instead of holding whatever the buffer arrived with.
    // After that they stay right for free: lock() widens the dirty rect it is
    // given whenever the buffer it hands back is older than the last post, and
    // the row loop below fills whatever band rows that widening names — the
    // same mechanism the content's own partial damage already depends on.
    int sx0 = x + origin_x_;
    int sy0 = y + origin_y_;
    int sx1 = sx0 + w;
    int sy1 = sy0 + h;
    if (surface_bands_owed_) {
        sx0 = 0;
        sy0 = 0;
        sx1 = surface_w_;
        sy1 = surface_h_;
    }

    ARect dirty{sx0, sy0, sx1, sy1};
    ANativeWindow_Buffer buf;
    const int rc = ANativeWindow_lock(window_, &buf, &dirty);
    if (rc != 0) {
        // A failed lock loses this frame's post and nothing else: the
        // backbuffer still holds the pixels and the caller keeps the damage,
        // so the next paint re-posts the same rectangle. One loud line, no
        // retry machinery.
        std::fprintf(stderr, "warptempo_gui: ANativeWindow_lock failed (%d)\n",
                     rc);
        return false;
    }

    // THE FORMAT IS 32-BIT OR THE PROGRAM STOPS. The copy below casts
    // buf.bits to uint32_t* and applies buf.stride in 32-BIT UNITS, so a
    // 16-bit buffer — WINDOW_FORMAT_RGB_565 is a legal NDK window format, and
    // a refused geometry request above can leave one in place — would take
    // four bytes per pixel into two and run off the end of every row. That is
    // memory corruption, not a wrong picture. THE ACCEPTED SET IS EXACTLY
    // RGBA_8888 AND RGBX_8888, the two the swizzle below is written for, and
    // there is no conversion arm because there is no producer for one to
    // serve: inventing a 565 path would be untestable machinery for a fault
    // nothing on this product's panels produces.
    if (buf.format != WINDOW_FORMAT_RGBA_8888 &&
        buf.format != WINDOW_FORMAT_RGBX_8888) {
        __android_log_print(ANDROID_LOG_FATAL, kLogTag,
                            "ANativeWindow buffer format %d is neither "
                            "RGBA_8888 nor RGBX_8888; refusing to blit 32-bit "
                            "pixels into it",
                            static_cast<int>(buf.format));
        abort();
    }

    // lock() may WIDEN the rect it was asked for (a buffer whose content is
    // older than the last post), and the widened region is what the caller
    // must fill. The backbuffer holds the whole frame, so honoring it is a
    // straight clamp-and-copy — clamped to the BUFFER alone now, because the
    // widened rect may reach outside the content rect and those pixels are
    // this loop's to fill too.
    const int cx0 = std::max(0, std::min(dirty.left,   buf.width));
    const int cy0 = std::max(0, std::min(dirty.top,    buf.height));
    const int cx1 = std::max(cx0, std::min(dirty.right,  buf.width));
    const int cy1 = std::max(cy0, std::min(dirty.bottom, buf.height));

    // The content's span inside that rect, in SURFACE coordinates: [ix0, ix1)
    // horizontally, and vertically whichever rows map into the backbuffer.
    // Everything else is band.
    const int ix0 = std::max(cx0, origin_x_);
    const int ix1 = std::min(cx1, origin_x_ + back_w_);

    // Both strides are in PIXELS: cairo's is bytes and is divided here,
    // ANativeWindow_Buffer::stride is documented as pixels already.
    const int src_stride_px = cairo_image_surface_get_stride(back_) / 4;
    const auto* src = reinterpret_cast<const uint32_t*>(
        cairo_image_surface_get_data(back_));
    auto* dst = static_cast<uint32_t*>(buf.bits);
    if (src && dst && cx1 > cx0) {
        for (int row = cy0; row < cy1; ++row) {
            uint32_t* drow = dst + static_cast<size_t>(row) * buf.stride;
            const int brow = row - origin_y_;
            // WHICH GROUND THIS ROW'S BAND TAKES (the two words at kBandWord):
            // above the content rect is the title strip — the status bar and
            // the air under it, one ground with the menu row below — and
            // everything else is the content's own.
            const uint32_t band_word = (brow < 0) ? kTopBandWord : kBandWord;
            if (brow < 0 || brow >= back_h_ || ix1 <= ix0) {
                // A band row (above or below the content rect), or a rect that
                // misses the content horizontally: all ground.
                fill_band(drow + cx0, cx1 - cx0, band_word);
                continue;
            }
            if (ix0 > cx0) fill_band(drow + cx0, ix0 - cx0, band_word);
            if (cx1 > ix1) fill_band(drow + ix1, cx1 - ix1, band_word);
            const uint32_t* srow =
                src + static_cast<size_t>(brow) * src_stride_px +
                (ix0 - origin_x_);
            // Both accepted formats need the R<->B swap (the gate above is
            // what makes that unconditional). The band fill above does not:
            // its word was built in the window's order to begin with.
            copy_swap_rb(drow + ix0, srow, ix1 - ix0);
        }
    }

    const int post_rc = ANativeWindow_unlockAndPost(window_);
    if (post_rc != 0) {
        std::fprintf(stderr,
                     "warptempo_gui: ANativeWindow_unlockAndPost failed (%d)\n",
                     post_rc);
        return false;
    }
    // The owed full-surface post is spent only on a post that reached the
    // window, for the same reason the caller keeps its damage until then.
    surface_bands_owed_ = false;
    return true;
}

void GuiPlatform::paint_now() {
    // Synchronous paint + post for the one case that cannot wait for run()'s
    // next pass: a blocking load. paint_one_frame no-ops before the first
    // window and on empty damage, and the post is immediate, so there is no
    // flush counterpart to call.
    paint_one_frame();
}

// ---------------------------------------------------------------------------
// The tick
// ---------------------------------------------------------------------------

int GuiPlatform::detect_refresh_rate_ms() {
    // 5 ms (architect 2026-08-27), and stated as a CHOICE rather than a
    // reading: the NDK exposes no display refresh rate to a native activity
    // below the Choreographer's own vsync callbacks, and standing up a
    // Choreographer to time two frames would buy one number at the cost of a
    // second cadence in a loop whose whole design is ONE wakeup.
    //
    // IT IS THE PINNED 90 Hz PANEL'S HALF-PERIOD, ROUNDED DOWN. adopt_window
    // asks the window for 90 Hz outright (the ruling and the call are there),
    // so this loop's cadence and the panel's are two halves of one decision
    // rather than a guess about the other: 90 Hz is an 11.1 ms period and 5 ms
    // is under half of it. There is ONE DEVICE and it is pinned, which is what
    // makes a hard-coded number honest here.
    //
    // THE DIRECTION THAT COSTS NOTHING is still downward: the tick only POLLS
    // deadlines (key repeat, the touch window, the audition's rests) and drives
    // the playback scanner, so a tick faster than half the refresh is wasted
    // work and never a wrong frame, while a slower one would visibly step the
    // playhead. It was 8 ms — 60 Hz's half-period — until the pin.
    return 5;
}

bool GuiPlatform::arm_playback_timer() {
    playback_tick_ms_ = detect_refresh_rate_ms();
    if (timerfd_ < 0) return true;

    struct itimerspec its{};
    its.it_value.tv_sec     = playback_tick_ms_ / 1000;
    its.it_value.tv_nsec    = (playback_tick_ms_ % 1000) * 1000000L;
    its.it_interval.tv_sec  = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;
    if (timerfd_settime(timerfd_, 0, &its, nullptr) < 0) {
        std::fprintf(stderr, "warptempo_gui: timerfd_settime failed: %s\n",
                     std::strerror(errno));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The run loop
// ---------------------------------------------------------------------------

void GuiPlatform::watch_fd(int fd, int ident) {
    if (!app_ || !app_->looper || fd < 0) return;
    ALooper_addFd(app_->looper, fd, ident, ALOOPER_EVENT_INPUT, nullptr,
                  nullptr);
}

void GuiPlatform::unwatch_fd(int fd) {
    if (!app_ || !app_->looper || fd < 0) return;
    ALooper_removeFd(app_->looper, fd);
}

/*
 * THE DRAIN. The window-system sources (the glue's cmd pipe and its input
 * queue) are processed ON THE SPOT — their process() bodies are the glue's own,
 * and deferring one would mean holding an unfinished AInputEvent — while the
 * timer and the four worker fds are only RECORDED, because the looper hands
 * events back in readiness order and the order they are acted on in is policy
 * (see pump). Drained to empty at timeout 0 after the first poll, so a busy
 * source can never starve the repaint.
 */
void GuiPlatform::drain_looper(int timeout_ms) {
    if (!app_) return;   // after shutdown there is nothing left to drain

    int timeout = timeout_ms;
    for (;;) {
        int   events = 0;
        void* data   = nullptr;
        const int ident =
            ALooper_pollOnce(timeout, nullptr, &events, &data);
        if (ident < 0) break;   // WAKE, TIMEOUT, CALLBACK or ERROR: nothing owed
        timeout = 0;            // the rest of the drain never waits

        if (ident == LOOPER_ID_MAIN || ident == LOOPER_ID_INPUT) {
            auto* source = static_cast<android_poll_source*>(data);
            if (source) source->process(app_, source);
        } else if (ident == kIdentTimer) {
            uint64_t expirations = 0;
            (void)read(timerfd_, &expirations, sizeof(expirations));
            timer_fired_ = true;
        } else if (ident >= kIdentWorker0 &&
                   ident < kIdentWorker0 + kWorkerCount) {
            const int slot = ident - kIdentWorker0;
            const int fds[kWorkerCount] = {worker_completion_fd_,
                                           waveform_worker_completion_fd_,
                                           history_worker_completion_fd_,
                                           history_prefetch_completion_fd_};
            if (fds[slot] >= 0) {
                uint64_t cnt = 0;
                (void)read(fds[slot], &cnt, sizeof(cnt));
                worker_fired_[slot] = true;
            }
        }

        if (app_ && app_->destroyRequested != 0) {
            // The system is tearing the activity down and android_main must
            // return. This is the CloseCallback's edge too — the consumer's
            // quit path — but nothing may block here, so the hook is not
            // fired: the GUI's own teardown runs on the way out of gui_main.
            should_exit_ = true;
            break;
        }
    }
}

/*
 * ONE PASS of the loop body, and the ONE place the dispatch order lives.
 *
 * The order is the Wayland loop's, member for member, because the arbitration
 * it encodes is the product's and not the protocol's:
 *
 *   1. the window-system sources, drained to empty (drain_looper);
 *   2. the TICK: on_tick_ then input_.tick(), in that order — the core's own
 *      fixed order (key repeat, then the touch window) is inside tick();
 *   3. the four worker completions, in registration order;
 *   4. the loop-settled hook, at the tail, so it observes the pass's FULLY
 *      settled state;
 *   5. paint-if-dirty.
 *
 * WHEN THE SETTLED HOOK DOES NOT FIRE: once should_exit_ is set. The pass is
 * the last one — the objects the consumer reads are still alive, main.cpp's
 * outlive run() — but the frame it would compute for is never presented. The
 * Wayland twin additionally skips it on its EINTR and connection-loss paths;
 * neither exists here (ALooper surfaces no EINTR and there is no connection to
 * lose), so the exit test is the whole condition.
 */
void GuiPlatform::pump() {
    if (!app_) return;

    // The first on_resize_ of the process is owed here rather than at the
    // adoption that took the geometry, and it lands BEFORE the drain so the
    // first event of the process acts on the panel's own layout (one owner,
    // deliver_owed_resize).
    deliver_owed_resize();

    drain_looper(/*timeout_ms=*/-1);

    if (timer_fired_) {
        timer_fired_ = false;
        if (on_tick_) on_tick_();
        // Both software deadlines, sampled in the core's own fixed order
        // (key repeat, then the touch window) — the arbitration is stated at
        // GuiInputCore::tick.
        input_.tick();
    }

    for (int i = 0; i < kWorkerCount; ++i) {
        if (!worker_fired_[i]) continue;
        worker_fired_[i] = false;
        switch (i) {
            case 0: if (on_worker_completion_)          on_worker_completion_();          break;
            case 1: if (on_waveform_worker_completion_) on_waveform_worker_completion_(); break;
            case 2: if (on_history_worker_completion_)  on_history_worker_completion_();  break;
            default: if (on_history_prefetch_ready_)    on_history_prefetch_ready_();     break;
        }
    }

    if (!should_exit_) loop_settled_hook_(input_.current_mods());

    paint_one_frame();
}

void GuiPlatform::run() {
    // THE LOOP'S ONE WAKEUP is the periodic timerfd, so the blocking poll
    // never waits longer than one tick period and damage declared with no
    // event under it still paints within that period.
    while (!should_exit_) pump();
}

void GuiPlatform::drain_events() {
    // IT PAINTS, AND IT DOES NOTHING ELSE.
    //
    // ITS LIVE CALLER IS A BLOCKING LOAD'S PROGRESS CALLBACK (file_loader.cpp),
    // which calls it many times per load so the window keeps showing progress
    // while the model is half-built. What the caller was written against is
    // the Wayland backend's wl_display_dispatch_pending: that call dispatches
    // events ALREADY READ and reads no socket, so a load observes no new
    // input, no focus change and no close — only the pending frame-done
    // callback, which paints. This backend has no equivalent split: the
    // looper's window-system sources hold the glue's own process() bodies,
    // and stepping one would deliver a touch or a lifecycle command straight
    // into a GUI whose AppState is explicitly half-built. So no source is
    // processed at all — neither LOOPER_ID_MAIN nor LOOPER_ID_INPUT — and the
    // periodic tick and the worker completions likewise wait for the next
    // pump(), exactly as their Wayland counterparts wait outside
    // dispatch_pending's reach.
    //
    // THE ACCEPTED COST is Android's ANR watchdog, which fires after ~5 s of
    // an input event nobody services. The product's loads measure ~0.5 s for a
    // 101 MB source, so a touch has to land inside a load an order of
    // magnitude longer than that to be at risk — and a touch during a load
    // that long is the user interrupting his own load, which is his to answer.
    //
    // paint_now()'s contract is unchanged and is the other half of this pair:
    // that one is the synchronous paint asked for outright, this one is the
    // progress paint a load takes on its way through.
    paint_one_frame();
}

// ---------------------------------------------------------------------------
// Lifecycle commands
// ---------------------------------------------------------------------------

void GuiPlatform::on_app_cmd(int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            // A window arriving after init()'s adoption — the TERM/INIT cycle
            // a task switch produces. The resize fires on the spot: every hook
            // is wired by now.
            if (app_->window) adopt_window(/*fire_resize=*/true);
            break;

        case APP_CMD_TERM_WINDOW:
            // PAINTING SUSPENDS AND NOTHING ELSE HAPPENS. The window is
            // borrowed, so it is dropped rather than released; the backbuffer
            // survives, holding the last frame, so the next INIT_WINDOW at the
            // same size re-posts without a repaint. Input state is untouched:
            // a lost window is not a lost touch stream (the hard end has its
            // own edge, APP_CMD_LOST_FOCUS below and touch_cancel).
            window_ = nullptr;
            break;

        case APP_CMD_CONTENT_RECT_CHANGED:
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            // The activity is landscape-locked and resizeableActivity="false",
            // so the SURFACE cannot move here — but the window the GUI sees is
            // the content rect inside the system bars, and a bar coming or
            // going moves that without touching the surface. CONTENT_RECT_-
            // CHANGED is the command that carries it (the glue has already
            // stored the new rect by the time this runs); the other two adopt
            // for the same reason they always did. One handler for all three:
            // adopt_window re-reads the surface AND the rect, and both
            // ensure_backbuffer and the resize hook are cheap no-ops when
            // nothing moved.
            if (app_->window) adopt_window(/*fire_resize=*/true);
            break;

        case APP_CMD_GAINED_FOCUS:
        case APP_CMD_LOST_FOCUS: {
            const bool active = (cmd == APP_CMD_GAINED_FOCUS);
            if (active == window_activated_) break;
            window_activated_ = active;
            // The rows that darken on focus loss are the caller's business;
            // this hook is the EDGE, the same shape the Wayland backend's
            // configure-driven one takes for the same reason.
            if (activation_changed_hook_) activation_changed_hook_();
            if (!active) {
                // FOCUS LEAVING IS A HARD END FOR TOUCH: the window system has
                // taken the contacts (a notification shade pull, a task
                // switch), and no further event on them can arrive. The
                // contract is the core's, shared whole with touch_cancel.
                input_.touch_capability_lost();
            }
            break;
        }

        case APP_CMD_DESTROY:
            // The activity is going. run() leaves at the next pass and
            // gui_main's teardown follows.
            should_exit_ = true;
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

int32_t GuiPlatform::on_input_event(AInputEvent* event) {
    const int32_t type = AInputEvent_getType(event);

    if (type == AINPUT_EVENT_TYPE_KEY) {
        // HARDWARE KEYBOARDS ARE OUT OF SCOPE ON THIS PLATFORM (touch.md), and
        // this backend translates no KeyEvent: returning 0 hands the event
        // back to the system, which is what keeps BACK leaving the app instead
        // of being swallowed by a GUI that has no use for it. The road INTO
        // the core's key path exists and is public — synthesize_key — for an
        // on-screen keyboard this backend owns.
        return 0;
    }

    if (type != AINPUT_EVENT_TYPE_MOTION) return 0;

    // A MOUSE IS OUT OF SCOPE AND IS CONSUMED. A USB mouse or a trackpad
    // delivers motion on AINPUT_SOURCE_MOUSE/TOUCHPAD, and routing it into the
    // core's POINTER doors would put a second, untested vocabulary on a glass
    // product whose gestures were all taken on the finger. Consuming rather
    // than returning it is deliberate: a mouse click that fell through to the
    // system would act on whatever is behind the activity.
    const int32_t source = AInputEvent_getSource(event);
    if ((source & AINPUT_SOURCE_TOUCHSCREEN) != AINPUT_SOURCE_TOUCHSCREEN) {
        return 1;
    }

    on_motion_event(event);
    return 1;
}

void GuiPlatform::on_motion_event(AInputEvent* event) {
    const int32_t action = AMotionEvent_getAction(event);
    const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
    const size_t  index  =
        static_cast<size_t>((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                            AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    const size_t  count  = AMotionEvent_getPointerCount(event);

    // COORDINATES ARE CONTENT PIXELS AS DOUBLES, unscaled. A surface pixel IS
    // a panel pixel (setBuffersGeometry(0,0) takes the window's own size), so
    // there is no factor to apply — but an AMotionEvent is measured from the
    // WINDOW's own corner, and the window is the whole panel while the GUI's
    // is the content rect inside the system bars. THE ORIGIN IS SUBTRACTED
    // HERE AND NOWHERE ELSE ON THE WAY IN: these two lambdas are every
    // coordinate this backend hands the core (the key path carries none, the
    // hover/scroll actions are dropped below, and the capture doors take GUI
    // coordinates that never reach the window at all), which is what keeps
    // GuiInputCore identical to the Wayland build's.
    //
    // A TOUCH IN A BAND IS DELIVERED, NOT CLAMPED AND NOT DROPPED: translated,
    // it is simply outside the window — a negative y above the rect, y >=
    // height_ below it — which is exactly the shape a Wayland pointer
    // drag past an edge takes under labwc's implicit grab, and the GUI's own
    // hit tests are what answer it there (the case is written out at
    // containing_pixel, input_core.h). Clamping would invent a second policy
    // for a coordinate the shared core already has one for. There is no
    // producer either way: the bars' own windows take those touches.
    //
    // They are handed over FRACTIONAL because that is what the panel reports
    // and what the core's one containment conversion expects
    // (GuiInputCore::containing_pixel).
    auto px = [&](size_t i) {
        return static_cast<double>(AMotionEvent_getX(event, i)) -
               static_cast<double>(origin_x_);
    };
    auto py = [&](size_t i) {
        return static_cast<double>(AMotionEvent_getY(event, i)) -
               static_cast<double>(origin_y_);
    };

    switch (masked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            if (index < count) {
                // EVERY AMotionEvent CARRIES EVERY LIVE POINTER'S CURRENT
                // POSITION, not only the one its action names, and the fingers
                // already down have moved since the last MOVE was delivered.
                // Their fresh positions go FIRST, before the new finger's
                // down: the core's two-finger upgrade seats the pinch's pan
                // and distance bases on the positions it holds AT THE JOIN,
                // so a stale first finger would start the pinch from a place
                // the hand had already left and jump on its first frame. The
                // new pointer takes touch_down alone — a down IS its position.
                for (size_t i = 0; i < count; ++i) {
                    if (i == index) continue;
                    input_.touch_motion(AMotionEvent_getPointerId(event, i),
                                        px(i), py(i));
                }
                input_.touch_down(AMotionEvent_getPointerId(event, index),
                                  px(index), py(index));
            }
            break;

        case AMOTION_EVENT_ACTION_MOVE:
            // EVERY POINTER'S CURRENT POSITION IS DELIVERED before the frame
            // that closes the batch — the same shape wl_touch's
            // motion-then-frame grouping has, and what lets the core's
            // two-finger machine read a consistent pair. (The down and up arms
            // deliver the same snapshot for the same reason; this is the arm
            // that carries nothing else.)
            //
            // HISTORY SAMPLES ARE NOT REPLAYED. AMotionEvent carries the
            // positions the sensor produced between deliveries
            // (getHistoricalX/Y), and every one of them would be a separate
            // core motion. They are dropped deliberately: the core coalesces
            // motion to the frame boundary anyway (one delivery per logical
            // frame, whatever the sensor rate), so replaying them would cost
            // work to produce the same single delivery.
            for (size_t i = 0; i < count; ++i) {
                input_.touch_motion(AMotionEvent_getPointerId(event, i),
                                    px(i), py(i));
            }
            break;

        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            if (index < count) {
                // WHY THE UP NEEDS THE MOTION: the core's touch_up takes no
                // coordinates. It delivers whatever motion is STAGED for this
                // frame — the nav pair's completed leg, the region former's
                // final leg — and then ends the gesture, so a finger's LAST
                // MOTION IS ITS LIFT POSITION. That is Wayland's shape
                // (wl_touch.up carries no coordinates either), while an
                // AMotionEvent up carries the whole current snapshot, and a
                // short gesture can have its only travel present in the up
                // event alone. The snapshot is therefore delivered for every
                // pointer INCLUDING the lifting one, and only then the up.
                for (size_t i = 0; i < count; ++i) {
                    input_.touch_motion(AMotionEvent_getPointerId(event, i),
                                        px(i), py(i));
                }
                input_.touch_up(AMotionEvent_getPointerId(event, index));
            }
            break;

        case AMOTION_EVENT_ACTION_CANCEL:
            // The window system claims the touches. One contract with
            // capability loss, and the core owns it whole.
            input_.touch_cancel();
            return;   // a cancel closes its own batch; no frame is owed

        default:
            return;   // hovers, scrolls and the rest: nothing to translate
    }

    // THE BATCH BOUNDARY. One AMotionEvent is one logical touch frame, exactly
    // as one wl_touch.frame closes one Wayland batch, and the core's per-frame
    // drain (one motion delivery, one nav update or one region update) hangs
    // off it. It follows every translated action, not just MOVE: a down and an
    // up each close their own batch too.
    input_.touch_frame();
}

// THE ON-SCREEN KEYBOARD'S OTHER SEAM MEMBER (contract at the declarations;
// the platform-question ruling is at platform_wayland.h's twin). YES,
// unconditionally: this backend translates no hardware key event, so the
// painted surface is the only key producer the platform has.
bool GuiPlatform::wants_onscreen_keyboard() const {
    return true;
}

void GuiPlatform::synthesize_key(GuiKey key, uint32_t stable_code, bool pressed,
                                 uint32_t codepoint) {
    // Remember the character against the stable code BEFORE delivering, so the
    // core's codepoint probe can answer for a repeat synthesized off this
    // press. A release carries no character and must not erase the press's:
    // the repeat that reads it is already gone by then, but a stale entry
    // costs nothing and an erased one would be a hole with no producer.
    //
    // THIS IS ALSO WHY A HELD SHIFTED LETTER REPEATS IN UPPER CASE, which is
    // the ruling and not a leak: the PRESS'S CODEPOINT IS THE KEY EVENT'S
    // IDENTITY, so a `Q` typed off the painted keyboard's one-shot arm repeats
    // `Q` for the whole hold exactly as a physical Shift+Q hold does. The arm
    // itself cleared at that press (the rule and its whole statement are at the
    // press router, input_pointer.cpp), and nothing here re-derives a repeat's
    // character against the live lamp.
    if (pressed) key_codepoints_[stable_code] = codepoint;
    input_.key_event(key, stable_code, pressed, codepoint);
}

// ---------------------------------------------------------------------------
// The stubs — what the Wayland twin does, and why this platform has no twin
// ---------------------------------------------------------------------------

// THE CLIPBOARD IS ONE STORED STRING (contract at the declaration). The
// Wayland twin claims the CLIPBOARD selection through wl_data_device, answers
// the compositor's later `send` from the same store, and reads a foreign
// selection back through a bounded pipe read. Android's system clipboard is a
// Java object (ClipboardManager) with no NDK surface at all, so there is
// nothing to claim and nothing to read: copy and paste work inside this
// process and stop at its edge.
void GuiPlatform::clipboard_set_text(const std::string& text) {
    clipboard_text_ = text;
}

std::string GuiPlatform::clipboard_get_text() {
    return clipboard_text_;
}

// POINTER CAPTURE IS A NO-OP ON GLASS (contract at the declaration). The
// Wayland twin hides the cursor, creates a zwp_locked_pointer_v1 and switches
// the gesture onto the relative-motion stream; there is no cursor to hide, no
// pointer to lock and no relative stream here. The core's capture state is
// left alone on purpose — seeding it would tell the GUI its coordinates had
// gone virtual when they had not.
void GuiPlatform::begin_pointer_capture(GuiCursorKind /*restore_kind*/) {}
void GuiPlatform::end_pointer_capture() {}

// THE CURSOR IMAGE HAS NO SURFACE HERE (contract at the declaration). The
// Wayland twin swaps a themed wl_surface under the pointer; a touch panel has
// no pointer image at all. The POLICY still runs — the drop of a kind named
// for a position the pointer does not occupy, and the remember-only-on-change
// — because it is the core's and because notional_pointer_x() is live on this
// platform too; only the protocol request at the end is missing.
void GuiPlatform::set_cursor_kind(GuiCursorKind kind) {
    if (input_.pointer_position_unknown()) return;
    if (kind == input_.cursor_kind()) return;
    input_.remember_cursor_kind(kind);
}

// ---------------------------------------------------------------------------
// Callback registration and the forwards to the core
// ---------------------------------------------------------------------------

void GuiPlatform::set_on_redraw(RedrawCallback cb)              { on_redraw_ = std::move(cb); }
void GuiPlatform::set_on_resize(ResizeCallback cb)              { on_resize_ = std::move(cb); }
void GuiPlatform::set_on_close(CloseCallback cb)                { on_close_ = std::move(cb); }
void GuiPlatform::set_activation_changed_hook(std::function<void()> cb) { activation_changed_hook_ = std::move(cb); }
void GuiPlatform::set_loop_settled_hook(std::function<void(GuiInputState)> cb) { loop_settled_hook_ = std::move(cb); }
void GuiPlatform::set_on_tick(TickCallback cb)                  { on_tick_ = std::move(cb); }
void GuiPlatform::set_on_pre_paint(PrePaintCallback cb)         { on_pre_paint_ = std::move(cb); }

void GuiPlatform::set_worker_completion_fd(int fd, std::function<void()> on_event) {
    worker_completion_fd_ = fd;
    on_worker_completion_ = std::move(on_event);
    watch_fd(fd, kIdentWorker0 + 0);
}
void GuiPlatform::set_waveform_worker_completion_fd(int fd, std::function<void()> on_event) {
    waveform_worker_completion_fd_ = fd;
    on_waveform_worker_completion_ = std::move(on_event);
    watch_fd(fd, kIdentWorker0 + 1);
}
void GuiPlatform::set_history_worker_completion_fd(int fd, std::function<void()> on_event) {
    history_worker_completion_fd_ = fd;
    on_history_worker_completion_ = std::move(on_event);
    watch_fd(fd, kIdentWorker0 + 2);
}
void GuiPlatform::set_history_prefetch_completion_fd(int fd, std::function<void()> on_event) {
    history_prefetch_completion_fd_ = fd;
    on_history_prefetch_ready_ = std::move(on_event);
    watch_fd(fd, kIdentWorker0 + 3);
}

// -- The input doors: every one of them is the core's, forwarded --
void GuiPlatform::set_on_key(KeyCallback cb)                    { input_.set_on_key(std::move(cb)); }
void GuiPlatform::set_on_key_release(KeyReleaseCallback cb)     { input_.set_on_key_release(std::move(cb)); }
void GuiPlatform::set_on_button_press(ButtonCallback cb)        { input_.set_on_button_press(std::move(cb)); }
void GuiPlatform::set_on_button_release(ButtonCallback cb)      { input_.set_on_button_release(std::move(cb)); }
void GuiPlatform::set_on_wheel(WheelCallback cb)                { input_.set_on_wheel(std::move(cb)); }
void GuiPlatform::set_on_motion(MotionCallback cb)              { input_.set_on_motion(std::move(cb)); }
void GuiPlatform::set_wheel_context_probe(WheelContextProbe cb)     { input_.set_wheel_context_probe(std::move(cb)); }
void GuiPlatform::set_text_editor_active_probe(TextEditorProbe cb)  { input_.set_text_editor_active_probe(std::move(cb)); }
void GuiPlatform::set_repeat_eligible_probe(RepeatEligibleProbe cb) { input_.set_repeat_eligible_probe(std::move(cb)); }
int64_t GuiPlatform::key_repeat_period_ms() const { return input_.key_repeat_period_ms(); }
void GuiPlatform::set_pointer_left_hook(std::function<void(GuiPointerLeaveReason)> cb) { input_.set_pointer_left_hook(std::move(cb)); }
void GuiPlatform::set_keyboard_intent_cancel_hook(std::function<void()> cb) { input_.set_keyboard_intent_cancel_hook(std::move(cb)); }
void GuiPlatform::set_touch_nav_hooks(
    std::function<void(const GuiTouchNavFrame&)> update,
    std::function<void()> end,
    std::function<bool(int x, int y)> pan_zone,
    std::function<bool(int x, int y)> thin_lane,
    std::function<void(int x, int y)> region_begin,
    std::function<void(int x, int y)> region_update,
    std::function<void()> region_end) {
    input_.set_touch_nav_hooks(std::move(update), std::move(end),
                               std::move(pan_zone), std::move(thin_lane),
                               std::move(region_begin), std::move(region_update),
                               std::move(region_end));
}
bool GuiPlatform::touch_contact_active() const { return input_.touch_contact_active(); }
void GuiPlatform::set_touch_slop_px(double px)              { input_.set_touch_slop_px(px); }
void GuiPlatform::set_capture_restore_x(double surface_x)   { input_.set_capture_restore_x(surface_x); }
void GuiPlatform::clear_capture_restore_x()                 { input_.clear_capture_restore_x(); }
void GuiPlatform::set_capture_restore_kind(GuiCursorKind kind) { input_.set_capture_restore_kind(kind); }
void GuiPlatform::set_notional_x_frozen(bool frozen)        { input_.set_notional_x_frozen(frozen); }
void GuiPlatform::set_notional_pointer_x(double surface_x)  { input_.set_notional_pointer_x(surface_x); }
void GuiPlatform::set_capture_wrap_span(double lo, double hi) { input_.set_capture_wrap_span(lo, hi); }
double GuiPlatform::notional_pointer_x() const { return input_.notional_pointer_x(); }

// ---------------------------------------------------------------------------
// android_main — this platform's entry point
// ---------------------------------------------------------------------------

namespace {

// THE SOURCE PATH — THE APP OPENS THE CURRENT PROJECT AND NOTHING ELSE. The GUI
// takes exactly one audio file and derives its three sidecars from the stem, so
// a path is all this owes; what has to be decided here is WHICH path, and there
// is no file picker on this platform to ask.
//
// THE CONVENTION lives under <externalDataPath> = /sdcard/Android/data/<pkg>/
// files — THE APP'S EXTERNAL FILES DIR, which adb can push into with no
// permission granted and the app reads and writes without
// MANAGE_EXTERNAL_STORAGE:
//
//     current              a one-line file holding a project folder's name
//     projects/<name>/     the laptop's own projects/<name>/ mirrored: the
//                          source .wav, the three sidecars sharing its stem,
//                          and renders/ beside them
//
// THE SOURCE IS THE ONE .wav IN THAT FOLDER WHOSE STEM HAS A .warpmarkers
// SIBLING. That is the sync script's own rule for "the source", spelled here the
// same way so the two ends cannot disagree about which file a project is about;
// a finished render sitting beside it carries the published title and no
// sidecars of its own, so it never matches and needs no exclusion.
//
// EVERY FAILURE HERE IS FATAL, deliberately. The sync script is the ONE producer
// of this tree, it writes the project folder and then `current` on every run, and
// a laptop is what puts a project on the device at all — so a fallback arm would
// have no producer: nothing could ever place files where it looked. Each failure
// therefore names itself in logcat at FATAL and aborts, rather than opening the
// wrong piece or a path that does not exist — and the directory refusal carries
// the system's own words, because one of them is "Permission denied" and means
// something specific: a directory `adb push` creates below `files/` belongs to
// `shell` with mode 770, which this app's uid cannot walk into. The sync script
// chmods what it pushes for exactly that reason (android/NOTES.md §10.1).
[[noreturn]] void source_fatal(const std::string& why) {
    __android_log_write(ANDROID_LOG_FATAL, kLogTag, why.c_str());
    abort();
}

std::string resolve_source_path(ANativeActivity* activity) {
    const char* dir = activity ? activity->externalDataPath : nullptr;
    if (!dir || !*dir) {
        source_fatal("no externalDataPath: the whole sync convention lives "
                     "there and nothing else puts a project on this device");
    }
    const std::filesystem::path files(dir);

    // `current`. std::getline takes the line without its terminator; the \r trim
    // beside it costs nothing and the name is otherwise taken VERBATIM, since a
    // project folder may legitimately end in any character but a newline.
    //
    // ITS PAYLOAD IS ONE RELATIVE PATH COMPONENT AND NOTHING ELSE, and the four
    // refusals below are the ADVERSARIAL-LOAD CLASS, not a fallback: the sync
    // script writes this file with one folder NAME on one line, so a `/`, a `.`
    // or `..`, an absolute path, or a second line of payload are states its ONE
    // producer cannot make. A name carrying a separator would compose a path
    // that leaves the folder it claims to name — `../other` resolves outside
    // `files/projects` entirely — and the app would then open and EDIT a piece
    // the laptop never put here. So the composition happens only after the name
    // is known to be a single component, and anything else names itself and
    // aborts, exactly as every other failure in this function does.
    std::string name;
    bool        extra_payload = false;
    {
        std::ifstream in(files / "current", std::ios::binary);
        if (!in) {
            source_fatal("no `current` file in " + files.string() +
                         ": run `wts tp` from the laptop");
        }
        std::getline(in, name);
        // Trailing blank lines are what a text editor's final newline leaves
        // behind and say nothing; any other second line is payload.
        std::string rest;
        while (std::getline(in, rest)) {
            while (!rest.empty() && (rest.back() == '\r' || rest.back() == '\n')) {
                rest.pop_back();
            }
            if (!rest.empty()) {
                extra_payload = true;
                break;
            }
        }
    }
    while (!name.empty() && (name.back() == '\r' || name.back() == '\n')) {
        name.pop_back();
    }
    if (name.empty()) {
        source_fatal("`current` names no project folder");
    }
    if (extra_payload) {
        source_fatal("`current` holds more than one line: it must name one "
                     "folder under projects/ and nothing else");
    }
    if (name.find('/') != std::string::npos || name == "." || name == "..") {
        source_fatal("`current` names '" + name + "': it must name one folder "
                     "under projects/, not a path");
    }

    std::error_code             ec;
    const std::filesystem::path project = files / "projects" / name;
    if (!std::filesystem::is_directory(project, ec)) {
        // The parenthetical is the SYSTEM'S own words and only appears when the
        // system had some: a plainly absent folder sets no error code, and
        // "(Success)" after a refusal message would read as a contradiction.
        source_fatal("`current` names '" + name + "', which is not a folder "
                     "under " + (files / "projects").string() +
                     (ec ? " (" + ec.message() + ")" : ""));
    }

    // THE WALK REFUSES OUT LOUD AND CANNOT THROW. Every step takes the
    // non-throwing overload and every error code is read, because a directory
    // that passes is_directory can still refuse to be enumerated (an ACL or a
    // mode this uid cannot read) — and that refusal is a DIFFERENT fact from
    // "no source in there", which is what an empty walk would otherwise report.
    // The system's own words are the whole diagnosis in those cases, exactly as
    // the directory refusal above carries them.
    std::filesystem::path source;
    std::filesystem::directory_iterator it(project, ec);
    if (ec) {
        source_fatal("cannot read '" + name + "': " + ec.message());
    }
    // The step is the loop's TAIL rather than its increment expression so the
    // code it sets is read before the end test can hide it: an increment that
    // fails is free to land on the end iterator, and a for-loop's condition
    // would then leave the walk quietly short.
    const std::filesystem::directory_iterator walk_end;
    while (it != walk_end) {
        // NESTED RATHER THAN `continue`d: the step below is the loop's one
        // advance, so every not-the-source answer has to fall through to it.
        const std::filesystem::path entry = it->path();
        std::error_code             entry_ec;
        const bool                  regular = it->is_regular_file(entry_ec);
        if (entry_ec) {
            source_fatal("cannot read '" + entry.filename().string() +
                         "' in '" + name + "': " + entry_ec.message());
        }
        if (regular && entry.extension() == ".wav") {
            std::filesystem::path markers = entry;
            markers.replace_extension(".warpmarkers");
            // An ABSENT sibling is not an error and clears the code (that is
            // what makes this the ordinary "not the source" path); a real
            // refusal sets one and is fatal like every other.
            const bool has_markers =
                std::filesystem::is_regular_file(markers, entry_ec);
            if (entry_ec) {
                source_fatal("cannot read '" + markers.filename().string() +
                             "' in '" + name + "': " + entry_ec.message());
            }
            if (has_markers) {
                if (!source.empty()) {
                    // TWO SOURCES IN ONE FOLDER is not a state the sync script
                    // can produce, and directory_iterator's order is
                    // unspecified, so there is no honest way to choose between
                    // them. It says which two.
                    source_fatal("two sources in '" + name + "': " +
                                 source.filename().string() + " and " +
                                 entry.filename().string());
                }
                source = entry;
            }
        }
        it.increment(ec);
        if (ec) {
            source_fatal("cannot read '" + name + "': " + ec.message());
        }
    }
    if (source.empty()) {
        source_fatal("no source in '" + name +
                     "': no .wav there has a .warpmarkers sibling");
    }
    return source.string();
}

// LOAD THE TWO BUNDLED FACES OUT OF THE APK, or die. A missing or unreadable
// asset is a BUILD defect — the packaging step puts both files in and there is
// no runtime state that removes them — so there is no error arm to design: the
// library logs and leaves the face unset, and painting would then silently use
// cairo's default, which is worse than not starting. This ABORTS instead, and
// the abort is the CALLER'S: gui_font_bundled.cpp keeps its own error arms
// exactly as they are.
//
// The assets stay open for the process's life is NOT needed here — unlike the
// spike, gui_font_install_bundled COPIES the bytes (its LIFETIME comment says
// so), so both AAssets are closed as soon as it returns.
void install_fonts_or_die(android_app* app) {
    AAssetManager* mgr = app->activity ? app->activity->assetManager : nullptr;
    if (!mgr) {
        __android_log_write(ANDROID_LOG_FATAL, kLogTag,
                            "no AAssetManager; cannot load the bundled fonts");
        abort();
    }

    struct Slot { const char* name; AAsset* asset; const uint8_t* bytes; size_t len; };
    Slot slots[2] = {
        {"LiberationSans-Regular.ttf", nullptr, nullptr, 0},
        {"LiberationMono-Regular.ttf", nullptr, nullptr, 0},
    };
    for (Slot& s : slots) {
        s.asset = AAssetManager_open(mgr, s.name, AASSET_MODE_BUFFER);
        s.bytes = s.asset ? static_cast<const uint8_t*>(AAsset_getBuffer(s.asset))
                          : nullptr;
        s.len   = s.asset ? static_cast<size_t>(AAsset_getLength(s.asset)) : 0;
        if (!s.bytes || s.len == 0) {
            __android_log_print(ANDROID_LOG_FATAL, kLogTag,
                                "bundled font asset %s is missing or empty",
                                s.name);
            abort();
        }
    }

    gui_font_install_bundled(slots[0].bytes, slots[0].len,
                             slots[1].bytes, slots[1].len);
    for (Slot& s : slots) AAsset_close(s.asset);

    // THE INSTALL IS OBSERVED, not assumed. gui_font_install_bundled reports
    // nothing (its failures are log-and-leave-unset, and that library arm
    // stays as it is), so the caller asks the only question that matters: does
    // selecting a family actually put an FT-BACKED face on a context? That is
    // both what the install produces and what text_shape requires, and a
    // context whose face is still cairo's toy default answers no.
    cairo_surface_t* probe_surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* probe = cairo_create(probe_surface);
    bool ft_backed = true;
    for (GuiFontFamily family : {GuiFontFamily::Sans, GuiFontFamily::Mono}) {
        gui_select_font_face(probe, family);
        if (cairo_font_face_get_type(cairo_get_font_face(probe)) !=
            CAIRO_FONT_TYPE_FT) {
            ft_backed = false;
        }
    }
    cairo_destroy(probe);
    cairo_surface_destroy(probe_surface);
    if (!ft_backed) {
        __android_log_write(ANDROID_LOG_FATAL, kLogTag,
                            "bundled fonts did not install; refusing to paint "
                            "with cairo's default face");
        abort();
    }
}

} // namespace

void android_main(android_app* app) {
    // android_main RUNS AGAIN if the activity is destroyed and remade. Nothing
    // survives between entries: gui_main builds and tears down the whole GUI
    // inside this frame, and the one file-scope pointer is re-parked below.
    route_stdio_to_logcat();

    // THE ENVIRONMENT, BEFORE ANYTHING READS IT. The render cache is
    // XDG_CACHE_HOME's tenant (render_cache.cpp) and several std::filesystem
    // paths fall back to HOME; on Android neither variable is set at all, so
    // they are pointed at the app's private directory here — the one place
    // that is guaranteed writable and is wiped with the app. This must precede
    // gui_main, which builds the cache directory during construction.
    // THE NUMERIC LOCALE, MADE `C` BEFORE THE GUARD ASKS. On Linux a process
    // starts in `C` and verify_c_numeric_locale (locale_check.h) is a pure
    // TRIPWIRE for a linked library moving it; on bionic the process starts in
    // `C.UTF-8`, so the same guard refuses to run at all — observed on the
    // device, and the first thing this port hit. The two locales are the same
    // locale numerically (bionic supports only C, POSIX and C.UTF-8, and its
    // decimal point is `.` in every one), so this is not a relaxation of the
    // invariant but the platform's way of SPELLING it: setlocale(LC_ALL, "C")
    // makes bionic's own name for the current locale "C" and the guard then
    // passes on truth rather than on a widened test. It runs HERE, before
    // gui_main, for the same reason the environment below does — the backend
    // owns whatever its platform does not hand the GUI already true — and it
    // leaves the tripwire's real job intact: a library that moves the locale
    // AFTER this line is still caught, since the guard runs later.
    std::setlocale(LC_ALL, "C");

    const char* internal = app->activity ? app->activity->internalDataPath : nullptr;
    if (internal && *internal) {
        setenv("XDG_CACHE_HOME", internal, 1);
        // XDG_CONFIG_HOME joined 2026-08-27 with the device config
        // (device_config.h), for the same reason and by the same rule: the GUI
        // has ONE resolver for that file — XDG first, then HOME/.config — and
        // pointing the variable here is what keeps it free of an Android arm.
        // The HOME below would already carry it, but naming the variable is
        // what makes the resolver's first branch the one that answers on both
        // platforms.
        setenv("XDG_CONFIG_HOME", internal, 1);
        setenv("HOME", internal, 1);
        setenv("TMPDIR", internal, 1);
    }

    g_android_app = app;

    install_fonts_or_die(app);

    // WAIT FOR THE WINDOW before handing over. gui_main constructs its
    // GuiPlatform and expects init() to have geometry — the whole GUI layout
    // is computed from it — so the glue is pumped here until APP_CMD_INIT_WINDOW
    // has landed. The bootstrap command handler is the glue's default (none):
    // app->window is set by the glue itself before it posts the command, so
    // simply draining until it is non-null is the whole wait.
    while (app->window == nullptr && app->destroyRequested == 0) {
        int   events = 0;
        void* data   = nullptr;
        const int ident = ALooper_pollOnce(-1, nullptr, &events, &data);
        if (ident == LOOPER_ID_MAIN || ident == LOOPER_ID_INPUT) {
            auto* source = static_cast<android_poll_source*>(data);
            if (source) source->process(app, source);
        }
    }
    if (app->destroyRequested != 0) {
        g_android_app = nullptr;
        return;
    }

    const std::string source = resolve_source_path(app->activity);
    std::fprintf(stderr, "warptempo_gui: source %s\n", source.c_str());

    gui_main(source.c_str());

    // The GUI is gone; unhook whatever it left and let the glue finish.
    app->onAppCmd     = nullptr;
    app->onInputEvent = nullptr;
    app->userData     = nullptr;
    g_android_app     = nullptr;
}
