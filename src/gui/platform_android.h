#pragma once
#include "device_config.h"
#include "gui_battery.h"
#include "gui_input.h"
#include "gui_media.h"
#include "input_core.h"
#include "gl_canvas.h"   // GuiGlCanvas, the GL canvas's owner
#include <cairo/cairo.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct GuiCanvasDrawList;   // canvas_sink.h — the GL canvas's frame

// GuiPlatform: the platform abstraction for the GUI's window, event loop and
// presentation — THE ONE BACKEND (architect 2026-10-30: the laptop GUI
// dropped, the tablet the one platform; platform.h is its one include). It
// runs inside a NativeActivity over the NDK's stock android_native_app_glue:
// the glue's thread owns an ALooper, this class adds its own periodic timerfd
// and the four worker eventfds to it, paints cairo into a persistent ARGB32
// backbuffer and presents it — uploaded by damage rect into one GLES texture
// drawn over an EGL window surface, the canvas drawn from the frame's draw
// list (the GL canvas, GuiGlCanvas, gl_canvas.h; the road at present) — (the
// window the GUI sees is the whole surface; the rule is at width_).
// No Android headers appear here on purpose; member pointer types are spelled
// `struct foo*` so the compiler treats them as forward declarations and the
// real interface headers stay private to platform_android.cpp.
//
// THE PUBLIC API IS THE SEAM the GUI's consumers compile against through
// platform.h (re-grepped 2026-10-08: main.cpp, viewport.cpp, icons.cpp,
// paint_handler.h, prompt.h, file_loader.h, input_handler.h,
// onscreen_keyboard.h, render_player.h, car_transport.h). A member with
// nothing behind it on glass — the window title — is an honest no-op whose
// declaration says why. EACH CONTRACT IS STATED ONCE: the input doors at
// their owner, GuiInputCore (input_core.h); the loop, the hold, the damage
// call and the canvas frame at platform.h's head; every other member's at
// its declaration below.
//
// THE INPUT POLICY IS NOT HERE. It lives in the ONE portable GuiInputCore
// this class holds (input_core.h);
// this class decodes AMotionEvent units and lifecycle commands on its own side
// and hands the core plain values.
//
// THE ONE GLASS HOST is the Galaxy Tab S10 FE (SM-X520): Android 16 / One UI
// 8.0.5, 2304x1440 at 280 dpi (exactly 1.75x), the panel pinned to 90 Hz
// (adopt_window) so the tick is 5 ms, PAGE_SIZE 4096 (the APK's 16 KB
// alignment is headroom), `/storage` 0711 (traversable, never listable), one
// USB-C port (cable and any OTG device exclusive; wireless adb for the rest),
// and `block_usb_lock` blocking USB while locked (it reads like Auto Blocker
// and is not). Every number this backend hard-codes was measured on it; the
// provisioning log is the architect's, outside the repo.

class GuiPlatform {
public:
    using RedrawCallback       = std::function<void(cairo_t*, int x, int y, int w, int h)>;
    using ResizeCallback       = std::function<void(int w, int h)>;
    // The input callbacks and probes are the core's types, re-exported so a
    // consumer that holds this class needs no second include (contracts at
    // GuiInputCore, input_core.h).
    using KeyCallback          = GuiInputCore::KeyCallback;
    using KeyReleaseCallback   = GuiInputCore::KeyReleaseCallback;
    using ButtonCallback       = GuiInputCore::ButtonCallback;
    using MotionCallback       = GuiInputCore::MotionCallback;
    using CloseCallback        = std::function<void()>;
    using TextEditorProbe      = GuiInputCore::TextEditorProbe;
    using RepeatEligibleProbe  = GuiInputCore::RepeatEligibleProbe;
    using TickCallback         = std::function<void()>;
    using PrePaintCallback     = std::function<void()>;

    GuiPlatform();
    ~GuiPlatform();

    // THE WINDOW ALREADY EXISTS when this runs: android_main pumps the glue
    // until APP_CMD_INIT_WINDOW has landed and only then calls gui_main, so
    // init() adopts a live ANativeWindow rather than waiting for one. The
    // width/height arguments are therefore ADVISORY ONLY — the panel's own
    // size wins, since a NativeActivity has no say in its window size — and
    // the title argument is unused (no system titlebar exists to carry it).
    bool init(int width, int height, const char* title);

    // THE DEVICE CONFIG'S FIRST-RUN TEMPLATE — the per-device preferences this
    // backend is born with, stamped into `$XDG_CONFIG_HOME/warptempo_gui/config`
    // on the first launch that finds no file there and never consulted again
    // (device_config.h owns the file, its schema and its keys,
    // kDeviceConfigKeys, every one required). A PLATFORM FACT, on the seam for
    // that reason: the scale a panel wants and where on this device the
    // projects live are answers only the backend has. STATIC because it is
    // asked before any window exists — gui_main resolves the config ahead of
    // init(). This backend answers 300 % and `<externalDataPath>/projects` as
    // the projects path. 300 is the scale the tablet runs at (architect
    // 2026-10-10; a 16-px glyph at 48 device px), the whole icon row fitting
    // (rationale at the definition, platform_android.cpp). The projects path is the
    // activity's own absolute path stamped literally — the `projects/` of the
    // tablet's projects clone, whose root is the external files dir — and it
    // is read off the backend's one file-scope pointer (g_android_app), which
    // android_main parks before
    // gui_main asks; the choice is stated at the definition. The
    // XDG_CONFIG_HOME the config resolves under is pointed at the app's
    // private internal directory by android_main before gui_main runs, beside
    // the cache home it has always set.
    static DeviceConfig device_config_defaults();

    // THE BUNDLED ICONS' FILES — the drawings of assets/icons/ (icons.h's
    // head), as a map from FILE NAME (`<Enumerator>.svg`, icons.h's
    // kIconNames; no folder) to the file's whole bytes, every `.svg` of the
    // bundle. Read by the launch's icon load alone (icons::load_set,
    // icons.h), IN PLACE: nothing is copied into the config folder (nobody
    // authors an icon on the device). The bundle is the APK's `icons/`
    // assets — the repository's `assets/icons/*.svg`, links dereferenced,
    // which build_apk.sh's asset step packs — listed and read through the
    // activity's AAssetManager, reached off the backend's one file-scope
    // pointer (g_android_app), which android_main parks before gui_main asks,
    // the road device_config_defaults takes. STATIC, asked before any window
    // exists, like the template above. THE ERROR ARM'S PRODUCER IS IO (an
    // asset that will not open): the system's words, which the load carries
    // to the launch's hard fail.
    static std::expected<std::map<std::string, std::string>, std::string>
        bundled_icon_files();
    // THE WINDOW TITLE HAS NO SURFACE: the activity is fullscreen and
    // landscape-only with no system titlebar, so this setter stores nothing
    // and paints nothing (the app paints no caption, 2026-10-24). It stays on
    // the API because the GUI calls it from its load path unconditionally,
    // with the source's parent folder basename (file_loader.cpp). THE DIRTY
    // INDICATOR IS THE SAVE BUTTON'S GREY (architect 2026-10-05,
    // plain_save_actionable), the product's one.
    void set_project_title(std::string project_name);

    void shutdown();
    // Pump until an exit or a run stop is requested (the loop contract at
    // platform.h). Re-entrant across the process: the stop bit is cleared at
    // the head, the exit bit never is.
    void run();
    void request_exit();
    // THE REOPEN'S HALF OF THE PAIR: make run() return with the window
    // standing and the exit bit untouched, the stop bit ending pump()'s loop,
    // so gui_main's loop can rebuild the object set and call run() again
    // (platform.h). Nothing else stops. It also ARMS THE OPEN'S PRESENTATION
    // HOLD AND ITS ONE FRAME'S DEBT (platform.h's head): a run stop is a
    // reopen, and the reopen holds the glass.
    void request_run_stop();
    // Has an exit been requested or forced? Read by gui_main after run()
    // returns to tell the two returns apart.
    bool exit_requested() const { return should_exit_; }
    // Fire on_resize with the window's current geometry, for a freshly
    // installed object set whose window will send no INIT_WINDOW of its own —
    // the reopen's case. A no-op with no window adopted. One call, one fire.
    void redeliver_geometry();
    void invalidate_region(int x, int y, int w, int h);
    void drain_events();
    // THE OPEN'S PRESENTATION HOLD (the rule at platform.h's head): whether
    // it stands, armed with its one frame's debt at construction and by
    // request_run_stop; the hold's ONE FRAME, which the GUI asks once per
    // arming at its load's entry — the whole surface damaged, painted past
    // the hold's gate and posted at once, outside the vsync gate, the hold
    // left standing; and the GUI's one release — refused under a pending run
    // stop; otherwise the whole surface damaged and left to the pass's vsync
    // post, the hold standing until that frame's post succeeds (the road out,
    // at the definition).
    bool presentation_held() const { return presentation_held_; }
    void present_held_frame();
    void release_presentation();

    // THE COPY STAYS IN THIS PROCESS AND THE PASTE TAKES THE NEWER
    // (architect 2026-10-29): Android 13 and later show a system "Copied"
    // chip on every write to the system clipboard, which no app can turn
    // off, and he does not want it. So clipboard_set_text NEVER WRITES THE
    // SYSTEM CLIPBOARD: it stores the text in clipboard_text_, this process's
    // one payload, with a wall-clock stamp (clipboard_stamp_ms_, ms since the
    // epoch — std::chrono::system_clock, the base Android's ClipDescription
    // timestamps use), and RETURNS NOTHING: the copy is pastable here at
    // once and no road refuses it, so the card "Copied the resolved value
    // '…'" is the one confirmation and the editor's cut always erases (no
    // refusal arm exists, the type rule: an error arm exists iff a producer
    // exists). clipboard_get_text answers WHICHEVER WAS COPIED LATER: the
    // process's payload when it is set and its stamp is >= the system clip's
    // own (MainActivity.clipboardTime), else the system clipboard's text
    // (MainActivity.clipboardGet), so text copied in another app — a URL in a
    // browser into a settings field — still pastes. An empty payload falls
    // to the system road. A copy made here never reaches another app.
    //
    // THE SYSTEM ROAD: ClipboardManager is a Java object with no NDK surface,
    // so the read goes up the MediaSession's own JNI road — the glue
    // thread's env, two method ids looked up once beside mediaState's (the
    // init block), a local frame per call, an exception described, cleared
    // and swallowed. THE PAYLOAD CROSSES AS BYTES, never as a jstring: JNI
    // strings are MODIFIED UTF-8 and the product's text is UTF-8 verbatim
    // (the Java side carries the whole reasoning). The bytes are not
    // filtered here: the editor's own incoming filter,
    // text_editor::replace_selection, is the boundary that validates UTF-8
    // well-formedness and drops control bytes.
    // WITH THE ROAD ABSENT (a failed attach or lookup, the line logged at
    // init) the paste reads the process's payload alone. `get` answers the
    // empty string where there is nothing to paste, the legal cold answer
    // every caller already handles, and a system clip past kClipboardMaxBytes
    // (gui_input.h, the seam's one bound, mirrored in MainActivity so the
    // bytes never cross) is one more of them. A clock set backwards between
    // a copy here and one elsewhere picks the wrong one of the two; nothing
    // guards it (no backstops for adversarial use).
    void        clipboard_set_text(const std::string& text);
    std::string clipboard_get_text();

    int width()  const;
    int height() const;
    bool has_initial_configure() const { return has_initial_configure_; }

    // THE DISPLAY'S HORIZONTAL RESOLUTION in device px (architect 2026-10-06),
    // its one reader the working column's per-process hold
    // (working_column_frames, app_state.h; read once at the first project's
    // load, file_loader.cpp): the zoom grid is the device's, never the
    // window's. It is the surface's width, width_ — the activity is full
    // screen and landscape-only, so the
    // surface IS the display (the rule at width_), 2304 on the tablet. Known
    // from init() on, since init adopts a live window, so it is never 0 by
    // the first load.
    int display_width_px() const;

    // THE WINDOW'S COLOR SPACE (architect 2026-10-08, display_transform.h's
    // head): true when the window is a Display-P3 layer, so the painters
    // convert every authored sRGB color at their entry — here once the EGL
    // surface's Display-P3 passthrough colorspace was accepted
    // (ensure_gl_surface), false if it was refused (the layer then stays sRGB
    // and nothing is converted). init() adopts the live window, so the answer
    // stands by the time gui_main reads it, once, right after init(); every
    // later adoption asks the same of the same panel, and a later surface
    // answering otherwise is fatal (ensure_gl_surface: gui_main reads the
    // first answer alone).
    bool window_is_display_p3() const { return window_display_p3_; }

    // NO WINDOW CHROME OF ITS OWN (architect 2026-10-24): the tablet's
    // full-screen window has no caption, no sizing frame
    // and no window verb on the seam; Android's own navigation minimizes and
    // Back is the close road.

    // THE PAINT TARGETS (2026-10-13): the buffers this backend paints into
    // and, inside the paint loop (paint_one_frame), the one this frame
    // paints — the index the GUI's as-painted records keep per target
    // (GuiUpperRowsRecord, app_state.h). ONE: the persistent backbuffer every
    // frame paints into.
    static constexpr int kPaintTargetCount = 1;
    int painting_target() const { return 0; }
    // THE GL CANVAS'S FRAME (2026-10-13; the seam's contract at platform.h's
    // head): present() reads the list it names — its records, or the score
    // viewer's page while it stands, drawn under a scissor to its clip, its
    // clip left out of every upload unless a floater stands — and writes its
    // page's report (GuiCanvasPage, canvas_sink.h), until the next call names
    // another.
    void set_canvas_frame(GuiCanvasDrawList* frame);

    void set_on_redraw(RedrawCallback cb);
    void set_on_resize(ResizeCallback cb);
    void set_on_key(KeyCallback cb);
    // Contract at GuiInputCore::set_on_key_release, input_core.h.
    void set_on_key_release(KeyReleaseCallback cb);
    void set_on_button_press(ButtonCallback cb);
    void set_on_button_release(ButtonCallback cb);
    void set_on_motion(MotionCallback cb);
    // THE CLOSE HOOK'S PRODUCER HERE IS THE BACK KEY (architect 2026-08-29):
    // AKEYCODE_BACK is consumed whole in on_input_event and fires this
    // callback on its ACTION_UP — a REQUEST the program answers
    // (GuiInputHandler::on_window_close) with the unsaved-work prompt when
    // the tab is dirty and with an immediate exit when it is clean, as it
    // answers Ctrl+Q.
    // WHAT IS STILL NOT THIS HOOK is APP_CMD_DESTROY / destroyRequested: that
    // is the system stating the activity is already going, with nobody left to
    // answer a prompt and nothing allowed to block, so the loop returns at
    // once and the teardown runs on the way out of gui_main. BACK asks; a
    // destroy cannot (a session killed from the task switcher goes unasked,
    // the accepted residual loss). PREDICTIVE BACK DOES NOT TAKE THIS ROAD
    // AWAY: the manifest declares no android:enableOnBackInvokedCallback and
    // targets SDK 34, so the framework dispatches the legacy KEYCODE_BACK to
    // the activity — measured on the tablet (docs/engineering/android_notes.md).
    void set_on_close(CloseCallback cb);
    void set_text_editor_active_probe(TextEditorProbe cb);
    void set_repeat_eligible_probe(RepeatEligibleProbe cb);

    // THE KEY-REPEAT INTERVAL in milliseconds. Contract at
    // GuiInputCore::key_repeat_period_ms, input_core.h. Android advertises no
    // repeat cadence of its own, so init() seeds the core with the hard-coded
    // pair (the ruling and the numbers are at that call, platform_android.cpp)
    // and this answers off it.
    int64_t key_repeat_period_ms() const;

    // Contract at GuiInputCore::set_pointer_left_hook, input_core.h.
    void set_pointer_left_hook(std::function<void(GuiPointerLeaveReason)> cb);

    // Fired ONLY on a CHANGE of the window's activation (keyboard focus),
    // from the focus commands (APP_CMD_GAINED_FOCUS / LOST_FOCUS). No face
    // takes an unfocused look, so the edge is the only consumer: main.cpp
    // wires it to end the tooltip, the same shape the pointer-leave hook
    // takes for the same reason (a platform edge with no other event to
    // carry its effect). Null-safe.
    void set_activation_changed_hook(std::function<void()> cb);

    // THE TOUCH NAVIGATION HOOKS. Contract at
    // GuiInputCore::set_touch_nav_hooks, input_core.h.
    void set_touch_nav_hooks(
        std::function<void(const GuiTouchNavFrame&)> update,
        std::function<void()> end,
        std::function<bool(int x, int y)> pan_zone,
        std::function<void(int x, int y)> region_begin,
        std::function<void(int x, int y)> region_update,
        std::function<void()> region_end,
        std::function<GuiTouchEditorField(int x, int y)> editor_field,
        std::function<void(int x, int y)> caret_begin,
        std::function<void(int x, int y)> caret_update,
        std::function<void()> caret_end);

    // TRUE WHILE ANY FINGER IS ON THE GLASS. Contract at
    // GuiInputCore::touch_contact_active, input_core.h.
    bool touch_contact_active() const;

    // THE PEN'S RETAINED ZOOM ANCHOR (architect 2026-09-27): a mouse's
    // Ctrl + left button held while it is lifted off the table and set down
    // again, carried onto the S Pen. The platform owns the pen facts (its
    // button, its plane, its contacts) and the GUI owns the seat
    // (TouchNavZoomState, app_state.h); these two members are the whole of
    // the traffic between them.
    //
    // THE QUERY — TRUE ONLY WHILE THE PEN'S OWN LIFT IS BEING DELIVERED WITH
    // ITS SIDE BUTTON HELD (the stroke's last sampled Ctrl bit, pen_ctrl_;
    // the lift itself is unsampled) and no finger landed during the stroke,
    // false at every other moment. GuiInputHandler::end_touch_nav asks it:
    // a one-finger seat standing at that end is RETAINED rather than cleared.
    bool pen_lift_keeps_zoom_anchor() const { return pen_lift_keeps_anchor_; }
    // THE RELEASE HOOK — fired (null-safe) where a retained anchor must die,
    // and this is the inventory (on_motion_event and on_app_cmd, by grep of
    // release_pen_zoom_anchor):
    //   (a) A FINGER'S CONTACT — every finger down, at the contact itself.
    //       A finger landing while the PEN owns a live stroke is an ignored
    //       contact: this still fires (taking a seat an earlier stroke
    //       retained, inert on the live stroke's own), and the stroke is
    //       marked so its lift keeps nothing;
    //   (b) THE PEN LANDING WITHOUT ITS BUTTON (its DOWN, sampled released);
    //   (c) THE FIRST IN-PLANE PEN REPORT SHOWING THE BUTTON UP, the plane
    //       being pen_report_in_plane's latch (a hover report or hovering
    //       button edge entering at or under kPenPlaneEnter and staying in
    //       until one rises past kPenPlaneExit, and every contact report).
    //       A button state reported ABOVE the plane, a HOVER_EXIT, or
    //       nothing at all (out of range) changes nothing;
    //   (d) THE HARD ENDS — a CANCEL and focus loss.
    // The GUI's body (GuiInputHandler::release_pen_zoom_anchor) clears only a
    // RETAINED seat, so a fire that meets a live gesture's seat is inert.
    void set_pen_zoom_anchor_release_hook(std::function<void()> cb);

    // THE PEN'S LIFT HOOK (architect 2026-10-07, after a pen lift the hot face
    // waits for motion; every hover-lit face since 2026-10-09): fired (null-safe), carrying nothing, at the pen's
    // lift off an EMPTY glass — the one lift the pointer survives
    // (pointer_focus_at; the rule at on_motion_event's hover arm) — AFTER
    // EVERYTHING the lift delivers (the release, the restore motion at the
    // lift, the Ctrl bit's drop; architect 2026-10-08), so the first motion
    // the GUI's latch meets is the pen's first hover report, its anchor
    // (GuiInputHandler::arm_pen_hot_latch, which owns the rule and its
    // clearing roads). Nothing else fires it: a finger never hovers, and a
    // pen lifting beside another contact is no hover.
    void set_pen_lift_hook(std::function<void()> cb);

    // THE TOUCH SLOP, in device pixels — the GUI's scaled press-becomes-drag
    // gate pushed down. Contract, uses, twin-gate invariant and the two-call-site
    // inventory are all at GuiInputCore::set_touch_slop_px, input_core.h.
    void set_touch_slop_px(double px);
    // THE TOUCH STILLNESS RADIUS, its sibling (GuiInputCore::
    // set_touch_stillness_px), pushed at the slop's two call sites.
    void set_touch_stillness_px(double px);

    void set_on_tick(TickCallback cb);
    void set_on_pre_paint(PrePaintCallback cb);

    // GuiAsyncRenderer integration. The renderer creates its own eventfd (it
    // owns the lifetime) and registers it here; the run loop's ALooper watch
    // set grows a source for it. On readiness the loop reads the 8-byte
    // counter to clear the fd and then invokes the registered callback.
    void set_worker_completion_fd(int fd, std::function<void()> on_event);

    // THE POINTER'S NOTIONAL POSITION (surface x, px). Contract at
    // GuiInputCore::notional_pointer_x, input_core.h: the finger's
    // translation and the pen's hover write it through the core.
    double notional_pointer_x() const;

    // The GuiWaveformWorker's completion eventfd; same shape as the async
    // renderer's above.
    void set_waveform_worker_completion_fd(int fd, std::function<void()> on_event);

    // And the GuiHistoryCommitWorker's completion eventfd.
    void set_history_worker_completion_fd(int fd, std::function<void()> on_event);

    // And the SIXTH, the history walk's prefetch worker — the one whose eventfd
    // is a READY signal rather than a completion (the callback DRAINS whatever
    // has queued; the loop's read of the counter is unchanged, the value being
    // the count of nothing the callback needs).
    void set_history_prefetch_completion_fd(int fd,
                                            std::function<void()> on_event);

    // -- THE ON-SCREEN KEYBOARD'S SEAM MEMBER -------------------------------
    //
    // THE PAINTED KEYBOARD IS THE ONE KEY PRODUCER (architect 2026-10-30: the
    // hardware keyboard is closed on the tablet), so the GUI paints it
    // wherever an editor owns the keyboard, with no platform term
    // (onscreen_keyboard::stands).
    //
    // A KEY EVENT FROM SOMETHING THAT IS NOT A PHYSICAL KEYBOARD. Hardware
    // keyboards are out of scope on this platform (on_input_event's key arm)
    // and this backend
    // translates none: AInputEvent key events are handed back to the system so
    // BACK still leaves the app. THIS IS THE ROAD THE OWNED ON-SCREEN KEYBOARD
    // TAKES INTO THE CORE'S KEY PATH — a painted surface emitting keysyms
    // directly (onscreen_keyboard.h; its press router is the one caller).
    //
    // `key` is a GuiKey (an X11 keysym, ASCII case-folded, standalone
    // modifiers and F1..F35 already dropped — the backend's contract, at
    // GuiInputCore::key_event). `stable_code` is the caller's own per-key
    // identity, which the repeat cancel compares against; it must be stable
    // for a given key and must not be the
    // keysym. `codepoint` is the UTF-32 value the key produces, or 0 for a key
    // that produces no character — it is REMEMBERED against stable_code so the
    // core's codepoint probe can re-answer for a synthesized repeat, which is
    // the whole reason this is a method rather than a bare forward.
    // `with_ctrl` is a ctrl the key carries itself — the painted Copy and
    // Paste keys' (2026-10-29), handed to the core's key_event as is (its
    // contract there): the S Pen's barrel stays the glass's tracked Ctrl.
    //
    // ITS ONE CALLER (2026-08-27) is the keyboard's press router, which
    // calls it through the seam like any other member.
    void synthesize_key(GuiKey key, uint32_t stable_code, bool pressed,
                        uint32_t codepoint, bool with_ctrl);

    // -- THE CAR'S TWO SEAM MEMBERS (architect design 2026-08-28 §3;
    // gui_media.h is the vocabulary) --------------------------------------
    //
    // A head unit's buttons reach the product over Bluetooth as media-button
    // events delivered to whichever app holds an ACTIVE media session, and
    // the head unit's display reads that session's metadata and playback
    // state back. Both are a PLATFORM SERVICE and not a GUI surface — the
    // session object, its callbacks, the audio focus and the thread they all
    // run on are the backend's — so the seam carries exactly two doors: a
    // hook the loop fires for each button the platform received, and a push
    // the GUI makes when what the display should show has changed. THE
    // CONSUMERS ARE TWO SINCE 2026-09-17, and main.cpp's hook forks the
    // commands between them on the render player's mode bit: the PLAYER
    // while it stands (GuiRenderPlayer::on_media_command /
    // publish_media_state) and the PROJECT TRANSPORT while it is closed
    // (GuiCarTransport::on_media_command / tick). Each holds the platform for
    // exactly these two calls, and exactly one of them is the live publisher
    // at any moment.
    //
    // THE HOOK IS FIRED ON THE LOOP'S OWN THREAD, one call per command, in
    // arrival order, from the same pass that dispatches the worker
    // completions and before that pass's paint — so a
    // button acts and its frame paints in one pass, and nothing the GUI holds
    // needs a lock. A command that arrives with no hook installed is DROPPED
    // by the platform's null test: main.cpp installs the hook per project and
    // clears it at the session tail, so a button pressed between two projects
    // reaches nothing. THE STATE PUSH is called from the loop thread only —
    // at the edges the render player inventories while it stands, and, with
    // it closed, from the car transport's per-tick comparator, which calls
    // only when a field changed, so neither owner pushes per tick (the
    // consuming side advances a playing position on its own clock from the
    // last push).
    //
    // THE PRODUCER: the Java sliver's MediaSession (MainActivity.java) receives
    // the head unit's buttons on the UI thread and hands each down through
    // JNI as one command, which the JNI entry queues and signals over this
    // backend's own per-process eventfd (media_command_fd_ below); pump()
    // drains the queue on the glue thread and fires the hook once per
    // command. The push goes the other way up the same bridge, into
    // MainActivity.mediaState, from the glue thread attached to the VM once at
    // init(). The whole mechanism — the queue, the wake, the attach, the
    // method lookup and the three drop rules — is stated at the definitions.
    // THE SESSION STANDS FOR THE APP'S LIFE (architect 2026-09-17): the push
    // carries `session_active` true from the first tick of the first project
    // onward — the render player's close stopped pushing an inactive state
    // that day, and the car transport's tick publishes the project's own
    // transport whenever the player is closed — so the sliver's
    // setActive(false) is reached only by the activity's onDestroy.
    void set_on_media_command(std::function<void(GuiMediaCommand)> cb);
    void publish_media_state(const GuiMediaState& state);

    // THE HOST'S BATTERY (architect 2026-10-24): what the host knows of its
    // battery NOW, for the logo lane's battery + clock legend (gui_battery.h
    // owns the type and the composer). CALLED ON EVERY TICK from the loop
    // thread — main.cpp's tick is the legend's one refresh site
    // (refresh_logo_legend), composing only when the wall-clock minute or
    // this answer changed — so it answers cheaply. IT IS THE SLIVER'S LAST
    // BROADCAST:
    // MainActivity registers for the sticky ACTION_BATTERY_CHANGED (no
    // permission; the registration hands back the sticky intent at once and
    // the receiver keeps it current) and pushes each one down through
    // nativeBatteryState — ONE JAVA->NATIVE CALL PER CHANGE, the car's road
    // (nativeMediaCommand) rather than a native->Java read at each refresh,
    // because the broadcast IS the change: the legend follows it within a
    // tick, with no binder call on the loop's thread. The push stores one
    // atomic word and needs no wake, the tick reading it on its own cadence
    // (the word and its packing are at g_battery_word, platform_android.cpp).
    // Before the first push it answers a battery whose level and plug state
    // are unknown — the honest cold answer, which the registration's sticky
    // intent replaces within the activity's first moments.
    GuiBattery battery_status();

private:
    // The glue's callback tables are C function pointers taking `android_app*`,
    // so dispatch lives in file-static functions that cast `app->userData` to
    // `GuiPlatform*` and call into private member functions. This struct is the
    // friend through which those statics reach the privates.
    friend struct AndroidCallbacks;

    // -- The glue and the window --
    // app_ is the glue's state, adopted at init() from the file-scope pointer
    // android_main parks there (the reason is at that pointer's definition).
    // window_ is BORROWED and never ANativeWindow_acquire()d — the glue owns
    // it between APP_CMD_INIT_WINDOW and APP_CMD_TERM_WINDOW, and a null here
    // means painting is suspended.
    struct android_app*   app_    = nullptr;
    struct ANativeWindow* window_ = nullptr;
    // The last adoption's DISPLAY_P3 tag accepted (window_is_display_p3).
    bool                  window_display_p3_ = false;

    // -- Damage accumulator --
    // Set by invalidate_region() at any time; consumed by the next paint pass
    // and cleared once the damaged pixels have been posted. The rect is the
    // GL owner's own (GuiGlRect: x, y, w, h), so present hands the list to it
    // in place.
    using DamageRect = GuiGlRect;
    std::vector<DamageRect> damage_;

    // -- The backbuffer (pattern: ONE persistent surface, not a pool) --
    // Cairo paints into this ARGB32 image surface and present uploads its
    // damaged rects into the GL canvas's texture. THE PERSISTENCE IS WHAT
    // MAKES PARTIAL DAMAGE CORRECT: a frame repaints only its damage, so the
    // surface holds the WHOLE last frame and every upload reads current
    // pixels.
    cairo_surface_t* back_   = nullptr;
    int              back_w_ = 0;
    int              back_h_ = 0;

    // -- THE GL CANVAS (2026-10-13; the one canvas road since the laptop
    // GUI's drop, architect 2026-10-30; the road at present) --
    // The window goes through EGL / GLES 3.0: the backbuffer above is
    // uploaded into ONE texture by damage rect and drawn whole every swap.
    // The EGL handles are spelled void* (each EGL type is one on this
    // platform) so no EGL header reaches this file. THE DISPLAY, CONFIG,
    // CONTEXT AND PROGRAMS ARE THE PROCESS'S, made at the first adoption and
    // kept until shutdown; THE SURFACE IS THE WINDOW'S (egl_window_ names
    // the window it was made on): made at an adoption of a new window,
    // destroyed at the window's loss. EVERYTHING GLES AND NEITHER EGL NOR
    // WINDOW — the programs, the texture that follows the backbuffer's size,
    // the canvas list's objects, the upload walk and the frame's draw — is
    // gl_canvas_'s (GuiGlCanvas, gl_canvas.h, 2026-10-14), the one owner
    // present drives; what stays here is the platform's.
    void*                 egl_display_ = nullptr;
    void*                 egl_config_  = nullptr;
    void*                 egl_context_ = nullptr;
    void*                 egl_surface_ = nullptr;
    struct ANativeWindow* egl_window_  = nullptr;
    // The one-time line naming the driver (adopt_window's road).
    bool                  gl_identity_logged_ = false;
    // THE FIRST SURFACE'S DISPLAY-P3 ANSWER, the one gui_main read; a later
    // surface answering otherwise is fatal (ensure_gl_surface, 2026-10-13).
    std::optional<bool>   gl_first_display_p3_;
    GuiGlCanvas           gl_canvas_;
    void ensure_gl_context();
    void ensure_gl_surface();
    void destroy_gl_surface();
    void destroy_gl();
    // The GL canvas's frame (set_canvas_frame): the GUI's one process-wide
    // list (gui_canvas_frame, canvas_sink.h), null until the first frame
    // names it.
    GuiCanvasDrawList* canvas_frame_ = nullptr;

    // -- Window state --
    // THE SURFACE IS THE WINDOW (architect 2026-10-01, with full screen):
    // width_/height_ are the size ANativeWindow reports, what the GUI is told
    // the window is and what every rect that crosses the seam is measured in.
    // An app window's frame is the whole display by construction on modern
    // Android, and with both system bars hidden (MainActivity.java's head)
    // nothing sits over any of it, so this backend reads no content rect and
    // subtracts no system inset — the GUI's coordinates are the surface's and
    // the panel's.
    int  width_  = 0;
    int  height_ = 0;
    // One rect of SURFACE damage, coalesced (invalidate_region's body; the
    // adoption's whole surface).
    void invalidate_surface_rect(int x, int y, int w, int h);
    bool should_exit_ = false;
    // The run-stop request, cleared at the head of every run() (platform.h).
    bool run_stop_requested_ = false;
    // THE OPEN'S PRESENTATION HOLD (platform.h's head): standing from
    // construction — the cold launch's open — and re-armed by every
    // request_run_stop; cleared at the post of the frame release_presentation
    // owes (paint_one_frame's tail), never at the release itself. Its ONE
    // FRAME'S DEBT is armed beside it at the same two points and paid by
    // present_held_frame alone. THE RELEASE OWED (2026-10-28, Sol's round):
    // set by release_presentation alone, the hold left standing; it opens
    // the paint's hold gate and the vsync post at pump's tail, and clears
    // with the hold, both bits together, only once that frame's post
    // succeeds — so every open_unseen() gate (main.cpp's hook block) drops
    // input until the finished picture is on the glass. request_run_stop
    // clears it (the next set's hold is its own to release).
    bool presentation_held_ = true;
    bool held_frame_owed_   = true;
    bool release_owed_      = false;
    bool has_initial_configure_ = false;
    // Latest focus reading (APP_CMD_GAINED_FOCUS / LOST_FOCUS): false until the
    // first GAINED_FOCUS, the honest cold answer; the launched activity is
    // focused, so the flag flips before any frame paints.
    bool window_activated_ = false;
    // THE S PEN IS HOVERING AS THE POINTER (2026-09-25): true between the
    // pen's translated HOVER_ENTER (the core's pointer_enter) — or the pen's
    // lift off an empty glass, which is that enter (pointer_focus_at; the
    // rule at on_motion_event's hover arm, architect 2026-10-07) — and the
    // edge that ends it — HOVER_EXIT, a hover report or hovering button edge
    // above the GUI's plane (pen_report_in_plane), ANY first contact down (the pen's tip or a
    // finger), or focus loss, each delivering the core's pointer_leave with
    // PenHoverEnd (end_pen_hover). What
    // makes the doors sequence sanely is that one owner: a hover never
    // overlaps a touch the core is translating (on_motion_event's hover and
    // down arms), so a first down's synthesized entry motion never meets a
    // pointer already "in".
    bool pen_hovering_ = false;
    // THE STANDING CTRL BIT WAS SET BY THE PEN'S BUTTON (set_pen_ctrl's
    // record): true from a sampled pen event reporting the button, while the
    // pen owns what it would steer, until the pen's lift, a cancel, a hover
    // exit or any pen report above the GUI's plane, a finger's first down or
    // focus loss clears it. The clears read
    // this record rather than the clearing event's own stylus metadata, so a
    // cancel that no longer enumerates the pen still drops the bit.
    bool pen_ctrl_ = false;
    // THE PEN IS A CONTACT ON THE GLASS: true from a down whose acting
    // pointer is the pen until its own lift, a cancel or focus loss. KEYED ON
    // THE ACTIONS, never on the distance axis (the driver writes distance 0
    // when the pen leaves range, so 0 is not "touching"); read by
    // pen_report_in_plane for the one action that can arrive either way, a
    // BUTTON_PRESS / BUTTON_RELEASE.
    bool pen_on_glass_ = false;
    // THE GUI'S PLANE'S LATCH — the pen is within the plane (architect
    // 2026-09-27, the plane's hysteresis). pen_report_in_plane owns it whole:
    // it reads and writes it once per pen-carrying event, the transitions
    // being listed at its definition (platform_android.cpp). Besides that
    // owner only the hard ends write it, OUT: a cancel and focus loss, which
    // clear it unconditionally as they clear pen_on_glass_.
    bool pen_in_plane_ = false;
    // A FINGER LANDED DURING THE PEN'S LIVE STROKE (release inventory (a) at
    // set_pen_zoom_anchor_release_hook): set at such a finger's down, reset
    // at the pen's first down; the stroke's lift then keeps no anchor.
    bool pen_stroke_shared_ = false;
    // pen_lift_keeps_zoom_anchor's answer: set immediately around the pen's
    // own touch_up and false everywhere else.
    bool pen_lift_keeps_anchor_ = false;
    std::function<void()> pen_zoom_anchor_release_hook_;
    // set_pen_lift_hook's callback; fired from the UP arm's pen_stays alone,
    // at the arm's end.
    std::function<void()> pen_lift_hook_;

    // THE FIRST on_resize_ FIRE IS OWED RATHER THAN MADE. init() adopts a
    // window that already exists (android_main waits for it), which is BEFORE
    // main.cpp has wired a single hook — so the geometry is taken at once and
    // the CALLBACK is deferred to whichever comes first of the first loop pass
    // and the first paint, by which time every hook is installed
    // (deliver_owed_resize is the one owner). Every later adoption (an
    // APP_CMD_INIT_WINDOW after a TERM) fires it on the spot and never sets
    // this.
    bool initial_resize_owed_ = false;

    // THE DEFERRABLE SOURCES — the timer, the four workers and the car's
    // media eventfd below — recorded by the drain and consumed by the
    // loop pass's tail. They are MEMBERS rather than locals because the drain
    // hands the looper's events back in readiness order while the ORDER THEY
    // ARE ACTED ON IN IS POLICY (stated at pump): a tick and a worker
    // completion that arrive mid-drain are recorded here and dispatched at the
    // pass's tail, after the window-system sources are empty.
    bool timer_fired_ = false;
    bool worker_fired_[4] = {false, false, false, false};
    // The media command source's own readiness flag, the same shape: recorded
    // by the drain, consumed by pump() after the worker completions.
    bool media_fired_ = false;

    // -- THE CAR'S BRIDGE (set_on_media_command / publish_media_state) --
    // DOWN: the JNI entry (Java_com_warptempo_gui_MainActivity_nativeMediaCommand,
    // platform_android.cpp) pushes onto media_queue_ under the file-scope
    // mutex that also guards the sink pointer it reaches this object through,
    // then writes media_command_fd_ — an eventfd this object CREATES in init()
    // and closes in shutdown(), watched on the looper under its own ident for
    // the process's life (unlike the four worker fds, which are per project).
    // pump() swaps the queue out under the same mutex and fires the hook per
    // command on the glue thread, outside the lock.
    int                          media_command_fd_ = -1;
    std::vector<GuiMediaCommand> media_queue_;
    std::function<void(GuiMediaCommand)> on_media_command_;
    // UP: the glue thread's JNIEnv (attached once in init(), detached in
    // shutdown() — a thread that exits attached is a VM abort) and the
    // method ids, looked up once. Null when the attach or the lookup failed,
    // and every push then drops with the line already logged. Spelled as
    // `struct` pointers so no JNI header reaches this file.
    // THREE IDS SHARE THE ONE LOOKUP BLOCK off the one class object, each
    // independent of the others: the car's mediaState (2026-08-28) and the
    // clipboard's read pair, clipboardGet (2026-09-03) and clipboardTime
    // (2026-10-29; the copy writes no system clipboard, the rule at
    // clipboard_set_text) — clipboard_road_open() is the one predicate the
    // paste's system road asks. (Activity's own moveTaskToBack, the
    // caption's Minimise, 2026-10-05 to 2026-10-24, went with the caption.)
    struct _JNIEnv*   jni_env_            = nullptr;
    bool              jni_attached_       = false;
    struct _jmethodID* media_state_method_   = nullptr;
    struct _jmethodID* clipboard_get_method_  = nullptr;
    struct _jmethodID* clipboard_time_method_ = nullptr;

    // -- Idle-tick timing --
    // The tick: a periodic timerfd (bionic has them), one of the loop's TWO
    // wake sources since 2026-10-14, the vsync
    // callback below the other (the 2026-08-27 "one wakeup" ruling reversed;
    // the reason at pump's tail). The number and its whole rationale are at
    // detect_refresh_rate_ms().
    int  playback_tick_ms_ = 5;
    int  timerfd_ = -1;

    // -- THE PAINT'S GATE, THE VSYNC CALLBACK (2026-10-14) --
    // The loop thread's AChoreographer, taken once in init(); the two bits
    // of the gate's state machine, whose whole rule is at pump's tail:
    // vsync_pending_ — a frame callback is posted and has not fired (set at
    // the post, cleared by the callback); vsync_due_ — a callback fired and
    // its frame is not yet painted (set by the callback, cleared at the
    // tail's paint, or there when no damage stands). The callback reaches
    // this object through a file-scope sink, never a `this` in the post
    // (g_vsync_sink, platform_android.cpp, has the reason).
    struct AChoreographer* choreographer_ = nullptr;
    bool vsync_pending_ = false;
    bool vsync_due_     = false;

    // -- Async renderer completion fd (owned by GuiAsyncRenderer; this is just
    // a watch handle, not a lifetime claim). -1 when none is registered.
    int  worker_completion_fd_ = -1;
    std::function<void()> on_worker_completion_;

    // Waveform-worker completion fd. Same lifetime story as above.
    int  waveform_worker_completion_fd_ = -1;
    std::function<void()> on_waveform_worker_completion_;

    // Checkpoint-worker completion fd. Same lifetime story again.
    int  history_worker_completion_fd_ = -1;
    std::function<void()> on_history_worker_completion_;

    // History-prefetch ready fd. Same lifetime story again.
    int  history_prefetch_completion_fd_ = -1;
    std::function<void()> on_history_prefetch_ready_;

    // -- The program's own copy (the rule at clipboard_set_text) --
    // Every copy's payload and its wall-clock stamp in ms since the epoch,
    // the base the system clip's ClipDescription timestamp uses; the paste
    // compares the two and takes the newer. Empty until the first copy.
    std::string clipboard_text_;
    int64_t     clipboard_stamp_ms_ = 0;

    // -- The synthesized keyboard's codepoint table --
    // stable_code -> codepoint, written by synthesize_key and read by the
    // core's codepoint probe when it re-fills a synthesized repeat. It is a
    // MAP rather than a single slot because the probe may be asked about a key
    // whose press is no longer the newest one; it never shrinks, and its size
    // is bounded by the painted keyboard's own key count.
    std::unordered_map<uint32_t, uint32_t> key_codepoints_;

    // -- THE PORTABLE INPUT POLICY (input_core.h) --
    // Every input event this class decodes is handed to this object in plain
    // units, and every input door on the public surface above forwards to it.
    GuiInputCore input_;

    // -- Callbacks --
    RedrawCallback       on_redraw_;
    ResizeCallback       on_resize_;
    CloseCallback        on_close_;
    // Fired at each window_activated_ EDGE (see set_activation_changed_hook).
    std::function<void()> activation_changed_hook_;
    TickCallback         on_tick_;
    PrePaintCallback     on_pre_paint_;

    // -- Internal helpers --
    // Whether the paste's Java road exists at all: the glue thread's env and
    // both read ids (clipboardGet, clipboardTime). False means a failed
    // attach or lookup, and the paste then reads clipboard_text_ alone (the
    // contract at clipboard_set_text).
    bool clipboard_road_open() const;
    // The system clip's own stamp, ms since the epoch, through
    // MainActivity.clipboardTime; 0 for no clip or a throw. Asked only with
    // clipboard_road_open() true, by the paste's newer-of-two test.
    int64_t clipboard_system_stamp_ms();

    // Take the glue's current ANativeWindow as ours: the surface geometry
    // (re-read every time — it is the window, width_'s rule), the backbuffer,
    // the core's surface width, has_initial_configure_, and full damage.
    // `fire_resize` is false for init()'s adoption alone (see
    // initial_resize_owed_) and true everywhere else.
    void adopt_window(bool fire_resize);
    void ensure_backbuffer(int w, int h);
    void destroy_backbuffer();
    // `held_frame` is the open's hold's one frame (present_held_frame), the
    // one call the hold's gate lets through (platform.h's head).
    void paint_one_frame(bool held_frame = false);
    // Upload every rect of damage_ into the GL canvas's texture and swap the
    // whole frame (the definition). Silent false with no window, no
    // backbuffer or no EGL surface. TRUE MEANS THE PIXELS REACHED THE WINDOW
    // — the caller clears its damage on that answer and on no other (the
    // rule is at the call site).
    bool present();
    // Deliver the owed first on_resize_ if one is owed. THE ONE OWNER, called
    // from pump()'s head and from paint_one_frame's (contract at its
    // definition).
    void deliver_owed_resize();
    int  detect_refresh_rate_ms();
    bool arm_playback_timer();
    // Add one fd to the glue's looper under `ident`; a negative fd is the
    // "nothing registered" answer and is a silent no-op. SEVEN REGISTRATIONS
    // GO THROUGH IT (re-grepped 2026-08-28): the timerfd and the car's media
    // eventfd, both once per PROCESS in init(), and the FOUR worker
    // completion fds, each re-registered per project by its own
    // set_*_worker_completion_fd setter. unwatch_fd is its counterpart at
    // shutdown (a looper holding a closed fd is the one thing teardown must
    // not leave behind).
    void watch_fd(int fd, int ident);
    void unwatch_fd(int fd);
    // Drain the looper into the flag members above. Window-system sources
    // (the glue's cmd pipe and input queue) are processed ON THE SPOT — their
    // process() bodies are the glue's own and deferring one would mean holding
    // an unfinished AInputEvent — while the timer, the four worker fds and the
    // car's media eventfd are only recorded. `timeout_ms` is -1 to block for the first event and 0 to take
    // whatever is already there.
    void drain_looper(int timeout_ms);
    // One pass of the loop body: the blocking drain, then the fixed dispatch
    // order and the paint at the vsync callback (its gate).
    void pump();

    // -- Lifecycle and input handlers (called from the file-static glue hooks) --
    void on_app_cmd(int32_t cmd);
    // Returns 1 when the event was consumed, 0 to let the system have it.
    int32_t on_input_event(struct AInputEvent* event);
    void on_motion_event(struct AInputEvent* event);
    // THE PEN'S SIDE BUTTON THROUGH THE CTRL DOOR — the ctrl bit set to
    // `held`; a no-op when the bit already stands, and a release is a no-op
    // unless the pen set the bit (pen_ctrl_ above). The door's one producer
    // (the contract at GuiInputCore::set_ctrl).
    void set_pen_ctrl(bool held);
    // END A PEN HOVER — the core's pointer_leave with the pen's own reason,
    // PenHoverEnd (the leave that darkens no lit row; GuiPointerLeaveReason,
    // input_core.h), and its frame, iff one stands (pen_hovering_ above).
    void end_pen_hover();
    // THE GUI'S PLANE — the one predicate both pen rules read (the hover
    // doors and the retained anchor's release), and the owner of the plane's
    // latch (pen_in_plane_): it UPDATES the latch as it answers, so it is
    // asked exactly once per pen-carrying event (on_motion_event's one
    // `pen_in_plane` const, its only caller). The two thresholds
    // (kPenPlaneEnter / kPenPlaneExit), the transitions and the measured
    // scale are at its definition, platform_android.cpp.
    bool pen_report_in_plane(const struct AInputEvent* event, int32_t masked,
                             size_t pen_index);
    // Fire the release hook (set_pen_zoom_anchor_release_hook), null-safe.
    void release_pen_zoom_anchor();
};
