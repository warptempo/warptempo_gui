// tools/palette/picker — the Android side: the glue's lifecycle, the window, the touch, the blit. A design tool
// beside the product (README.md); nothing here links to or from it, and its package is its own,
// com.warptempo.picker.
//
// THE WINDOW IS SET UP AS THE PRODUCT'S (src/gui/platform_android.cpp, GuiPlatform::adopt_window, and that file's
// head on the P3 measurement): a WINDOW_FORMAT_RGBA_8888 buffer (ANativeWindow_setBuffersGeometry), tagged
// ADATASPACE_DISPLAY_P3 (ANativeWindow_setBuffersDataSpace) under the manifest's colorMode="wideColorGamut", the
// panel pinned at 90 Hz, full screen with both system bars hidden (PickerActivity.java). So a hex shown here is the
// same bytes on the same panel as the product's: NO COLOUR CONVERSION ANYWHERE -- the frame is cairo's ARGB32
// (native-endian 0xAARRGGBB words, bytes B,G,R,A) and the blit swaps R and B into the buffer's R,G,B,A and nothing
// else. Each call's result is logged as the product logs it.

#include "fonts.h"
#include "picker.h"
#include "scene.h"

#include <android/asset_manager.h>
#include <android/data_space.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/window.h>
#include <android_native_app_glue.h>

#include <cairo.h>

#include <cstdarg>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

constexpr const char* kTag = "warptempo_picker";

struct App {
    android_app* glue = nullptr;
    ANativeWindow* window = nullptr;
    cairo_surface_t* frame = nullptr;     // the scene's size (the panel's), ARGB32
    std::unique_ptr<Picker> picker;
    std::vector<std::string> message;     // non-empty: the scene failed; the message is the whole screen
    bool repaint = true;
    bool logged_format = false;
};

// the frame into the window's buffer: R and B swapped (the frame's bytes run B,G,R,A, the buffer's R,G,B,A), alpha
// opaque; the format read back at the lock must be one of the two 32-bit RGBA layouts, as the product insists
void blit(App& a) {
    if (!a.window || !a.frame) return;
    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(a.window, &buf, nullptr) != 0) {
        __android_log_print(ANDROID_LOG_WARN, kTag, "picker: ANativeWindow_lock failed");
        return;
    }
    if (!a.logged_format) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "picker: buffer %dx%d stride %d format %d", buf.width, buf.height,
                            buf.stride, buf.format);
        a.logged_format = true;
    }
    if (buf.format != WINDOW_FORMAT_RGBA_8888 && buf.format != WINDOW_FORMAT_RGBX_8888) {
        __android_log_print(ANDROID_LOG_FATAL, kTag, "picker: buffer format %d is neither RGBA_8888 nor RGBX_8888", buf.format);
        abort();
    }
    cairo_surface_flush(a.frame);
    const int fw = cairo_image_surface_get_width(a.frame), fh = cairo_image_surface_get_height(a.frame);
    const int fs = cairo_image_surface_get_stride(a.frame) / 4;
    const auto* src = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(a.frame));
    auto* dst = static_cast<uint32_t*>(buf.bits);
    const int w = std::min(fw, int(buf.width)), h = std::min(fh, int(buf.height));
    for (int y = 0; y < h; ++y) {
        const uint32_t* s = src + size_t(y) * fs;
        uint32_t* d = dst + size_t(y) * buf.stride;
        for (int x = 0; x < w; ++x) {
            const uint32_t v = s[x];
            d[x] = 0xFF000000u | (v & 0x0000FF00u) | ((v >> 16) & 0xFFu) | ((v & 0xFFu) << 16);
        }
    }
    ANativeWindow_unlockAndPost(a.window);
}

void paint(App& a) {
    if (!a.window) return;
    if (!a.frame) {
        const int w = a.picker ? a.picker->scene().width : ANativeWindow_getWidth(a.window);
        const int h = a.picker ? a.picker->scene().height : ANativeWindow_getHeight(a.window);
        a.frame = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    }
    if (a.message.empty()) a.picker->paint(a.frame);
    else paint_message(a.frame, a.message);
    blit(a);
    a.repaint = false;
}

void fail_screen(App& a, const std::vector<std::string>& lines) {
    a.message = lines;
    for (const std::string& l : lines) __android_log_print(ANDROID_LOG_ERROR, kTag, "picker: %s", l.c_str());
    a.picker.reset();
    a.repaint = true;
}

// THE WINDOW, AS THE PRODUCT ADOPTS IT (GuiPlatform::adopt_window): RGBA_8888, DISPLAY_P3, 90 Hz; every result logged
void adopt_window(App& a) {
    a.window = a.glue->window;
    if (!a.window) return;
    const int32_t geom = ANativeWindow_setBuffersGeometry(a.window, 0, 0, WINDOW_FORMAT_RGBA_8888);
    __android_log_print(ANDROID_LOG_INFO, kTag, "picker: ANativeWindow_setBuffersGeometry(RGBA_8888) -> %d", int(geom));
    const int32_t space = ANativeWindow_setBuffersDataSpace(a.window, ADATASPACE_DISPLAY_P3);
    __android_log_print(ANDROID_LOG_INFO, kTag, "picker: ANativeWindow_setBuffersDataSpace(DISPLAY_P3) -> %d", int(space));
    const int32_t rate = ANativeWindow_setFrameRate(a.window, 90.0f, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
    __android_log_print(ANDROID_LOG_INFO, kTag, "picker: ANativeWindow_setFrameRate(90, FIXED_SOURCE) -> %d", int(rate));
    const int w = ANativeWindow_getWidth(a.window), h = ANativeWindow_getHeight(a.window);
    __android_log_print(ANDROID_LOG_INFO, kTag, "picker: window %dx%d", w, h);
    if (a.picker && (w != a.picker->scene().width || h != a.picker->scene().height))
        fail_screen(a, {"warptempo picker: the window is " + std::to_string(w) + "x" + std::to_string(h) +
                            ", the scene " + std::to_string(a.picker->scene().width) + "x" +
                            std::to_string(a.picker->scene().height),
                        "manifest.json: width and height must be the screen's"});
    if (!a.picker && a.frame &&
        (cairo_image_surface_get_width(a.frame) != w || cairo_image_surface_get_height(a.frame) != h)) {
        cairo_surface_destroy(a.frame);
        a.frame = nullptr;
    }
    a.repaint = true;
}

void on_cmd(android_app* glue, int32_t cmd) {
    App& a = *static_cast<App*>(glue->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW: adopt_window(a); break;
        case APP_CMD_TERM_WINDOW: a.window = nullptr; break;
        case APP_CMD_WINDOW_REDRAW_NEEDED:
        case APP_CMD_CONFIG_CHANGED:
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_GAINED_FOCUS: a.repaint = true; break;
        case APP_CMD_PAUSE:   // leaving the app saves nothing: the open panel's unsaved colour is discarded
            if (a.picker) {
                a.picker->discard_if_open();
                a.repaint = true;
            }
            break;
        default: break;
    }
}

// ONE POINTER: the first finger or the pen; hover, a second pointer and keys are not ours (BACK goes to the system)
int32_t on_input(android_app* glue, AInputEvent* ev) {
    App& a = *static_cast<App*>(glue->userData);
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION || !a.picker) return 0;
    const int32_t act = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
    const double x = AMotionEvent_getX(ev, 0), y = AMotionEvent_getY(ev, 0);
    switch (act) {
        case AMOTION_EVENT_ACTION_DOWN: a.picker->press(x, y); break;
        case AMOTION_EVENT_ACTION_MOVE: a.picker->move(x, y); break;
        case AMOTION_EVENT_ACTION_UP: a.picker->release(x, y); break;
        case AMOTION_EVENT_ACTION_CANCEL: a.picker->cancel(); break;
        default: return 1;
    }
    if (a.picker->needs_paint()) a.repaint = true;
    return 1;
}

bool load_fonts(android_app* glue) {
    AAssetManager* mgr = glue->activity->assetManager;
    AAsset* s = AAssetManager_open(mgr, "Roboto-Regular.ttf", AASSET_MODE_BUFFER);
    AAsset* m = AAssetManager_open(mgr, "RobotoMono-Regular.ttf", AASSET_MODE_BUFFER);
    const bool ok = s && m &&
                    fonts_install(static_cast<const uint8_t*>(AAsset_getBuffer(s)), size_t(AAsset_getLength(s)),
                                  static_cast<const uint8_t*>(AAsset_getBuffer(m)), size_t(AAsset_getLength(m)));
    if (s) AAsset_close(s);
    if (m) AAsset_close(m);
    return ok;
}

} // namespace

void plog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, kTag, fmt, ap);
    va_end(ap);
}

void android_main(android_app* glue) {
    App a;
    a.glue = glue;
    glue->userData = &a;
    glue->onAppCmd = on_cmd;
    glue->onInputEvent = on_input;
    ANativeActivity_setWindowFlags(glue->activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);

    if (!load_fonts(glue)) {
        __android_log_print(ANDROID_LOG_FATAL, kTag, "picker: the bundled fonts did not install");
        abort();
    }
    // THE DATA DIR: the external files dir (adb reads and writes it); the scene under scene/, the picks beside it
    const char* ext = glue->activity->externalDataPath;
    const std::string data = ext ? ext : "";
    std::string err, note;
    Scene scene;
    History hist;
    if (data.empty()) fail_screen(a, {"warptempo picker: no external files dir (externalDataPath)"});
    else if (!scene_load(data + "/scene", scene, err))
        fail_screen(a, {"warptempo picker: the scene in " + data + "/scene", err});
    else if (!picker_load(data, scene, hist, note, err))
        fail_screen(a, {"warptempo picker: the state in " + data, err});
    else {
        const Layer& act = scene.layers[scene.active];
        __android_log_print(ANDROID_LOG_INFO, kTag, "picker: scene %dx%d, %zu layers, active %s %s (%s), %d of %zu",
                            scene.width, scene.height, scene.layers.size(), act.name.c_str(), hex_of(act.colour).c_str(),
                            note.c_str(), hist.cursor + 1, hist.picks.size());
        a.picker = std::make_unique<Picker>(std::move(scene), data, std::move(hist));
    }

    for (;;) {
        int events = 0;
        android_poll_source* source = nullptr;
        // block while there is nothing to paint; once something is owed, drain what is pending and paint once
        int timeout = (a.repaint && a.window) ? 0 : -1;
        while (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(glue, source);
            if (glue->destroyRequested) {
                if (a.frame) cairo_surface_destroy(a.frame);
                return;
            }
            timeout = 0;
        }
        if (a.repaint && a.window) paint(a);
    }
}
