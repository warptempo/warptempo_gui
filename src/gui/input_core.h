#pragma once
#include "gui_input.h"
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>

// GuiInputCore: the product's PLATFORM-NEUTRAL INPUT POLICY, in one object.
// It owns the touch translation state machine, the key-repeat synthesis, the
// ctrl bit, the logical pointer (position, focus and the touch translation's
// left hold), and the one fractional->pixel conversion the whole product converts through (containing_pixel below).
//
// THE PEN AND THE FINGER ARE THE WHOLE INPUT (architect 2026-10-30: the
// hardware keyboard and the mouse are closed on the tablet, the one platform —
// determinism is per display width, so consistency across sessions commits him
// to this one panel, and a keyboard or mouse at the tablet would duplicate
// what the pen and the finger do and lose the car form factor). So this layer
// carries no mouse door, no wheel, no pointer capture and no cursor: its
// producers are the finger's and the pen's contacts, the pen's hover as the
// pointer, the pen's side button as Ctrl, and the painted keyboard's keys.
//
// WHAT "DETERMINISM PER DISPLAY WIDTH" PROMISES (Astra's determinism audit,
// 2026-11-06): the same state and the same DELIVERED events give the same
// coordinate arithmetic and the same authored result on this panel — the
// width fixes the grid's pitch. IT DOES NOT PROMISE THE SAME RESULT FOR THE
// SAME PHYSICAL GESTURE, because the event sampling and the tick schedule
// are not the gesture: a held arrow's repeats fire on due ticks scheduled
// from now, dropping catch-up (tick_chrome_press_repeat, input_pointer.cpp),
// so the same hold can count one step more or less; the value drag's travel
// counts from the first delivered sample past the grab gate, not from the
// gate's own line (the crossing's origin, input_pointer.cpp), so the same
// stroke can land a step apart. Both are stated local policies, not faults;
// a physical replay would need the same samples and tick decisions too.
//
// WHAT IT CONSUMES: primitive input events in PLAIN UNITS from exactly ONE
// backend — window-pixel doubles, bools, ids, an already-translated GuiKey
// with a stable per-key identity beside it. No protocol type, no fixed-point
// value and no keymap object reaches this layer; a backend decodes on its own
// side of the call and hands over values. It reads no window system.
//
// WHAT IT EMITS: the consumer hooks the GUI installs — the key, key-release,
// button and motion callbacks, the pointer-leave hook and the eleven touch
// hooks (the seven navigation ones — the sweep-armed query among them since
// 2026-11-07 — the editor-field query and the caret-drag trio)
// — plus the two probes it asks the GUI (text-editor-active,
// repeat-eligible) and the ONE probe it asks the backend (the codepoint
// refill, at set_codepoint_probe).
//
// GuiPlatform HOLDS ONE OF THESE AND FORWARDS TO IT. The GUI's consumers
// (every file that includes platform.h) hold GuiPlatform, and every setter
// and query on that class forwards to the body living here. COMMENTS HERE
// SPEAK THE CORE'S EVENT NAMES (touch_frame, touch_cancel, pointer_leave,
// set_ctrl, set_repeat_info); a provenance sentence (a behaviour measured
// under the laptop's compositor while its backend stood, until the laptop
// GUI's drop of 2026-10-30; labwc's repeat delay) keeps its words.

// THE POINTER LEAVE'S THREE FIRES — one hook, no reason handed (the reason
// enum, GuiPointerLeaveReason, 2026-08-08 to 2026-11-07, RETIRED when its
// last reader, the hover tooltip's contact-lift keep, went with the
// always-show model, AppState::RedesignTooltip: a road nothing reads goes).
// Every consumer reads every fire alike:
//   * THE ORDINARY LEAVE, the pointer focus dropping: pointer_leave, and a
//     touch POINTER TRANSLATION's ABNORMAL end WITH NO POINTER FOCUSED — the
//     window system took the contact and no pen hovers in the window, so
//     the pointer is gone (delivered after the lost-button motion; the
//     fires are touch_cancel and touch-capability loss — the edge inventory
//     at the touch state block). A translation ending with the pointer
//     FOCUSED fires this hook NOT AT ALL — it delivers a restore MOTION at
//     the pointer's own position instead (the focus fork; the one statement
//     is at deliver_touch_translation_end's definition).
//   * THE CONTACT'S LIFT, the same translation's CLEAN end (touch_up), with
//     no pointer focused — delivered after the release, the fork being the
//     same one. (The hover tooltip kept its button and the button's spent
//     state across this one, Windows' rule after a click, 2026-10-06 to
//     2026-11-07.) The S Pen's lift off an empty glass takes no leave at
//     all since 2026-10-07: the Android backend focuses the pointer at the
//     lift, so the fork's restore arm runs — pointer_focus_at.
//   * THE PEN'S HOVER END, the Android backend's S Pen hover ending
//     (GuiPlatform::end_pen_hover, through pointer_leave: a HOVER_EXIT —
//     which the platform sends just before every tip DOWN — a report above
//     the GUI's plane, any first down, focus loss; architect 2026-09-27,
//     every pen hover effect acts only within the plane). Its one
//     difference, while it stood (no face it would spare is left since the
//     Settings popup retired 2026-10-21: the record follows): THE PEN'S
//     HOVER END DARKENED NOTHING (architect 2026-10-10, his S Pen
//     touching down on the open Settings menu's row: "the word Pick Colors
//     blinks" — the exit's leave put the lit row out for the frame before the
//     DOWN's own walk re-lit it): every hover-lit face keeps what was painted
//     (pointer_leave_keeps_lit_rows, retired with the popup), the pen latch's own principle
//     (GuiInputHandler::pen_hot_latch_), and the next pointer event re-derives
//     the faces as ever — a DOWN on the same row changes nothing, a hover
//     re-entry elsewhere moves the row by its walk, a pen withdrawn for good
//     leaves the row lit as Windows leaves it under a resting pointer.
//     TRACED ON HIS GLASS 2026-10-10 (three taps on the open Settings menu's
//     Pick Colors row, three on the Font row's drop-down): the stylus
//     HOVER_EXIT and its DOWN arrive in SEPARATE deliveries within one
//     millisecond (steady-clock ms 92518249 → 92518250, 92524919 → 92524919,
//     92528433 → 92528434), the exit's leave running before the DOWN's own
//     motion and press, so a frame could paint between them — the darkening
//     showed whenever one did, which is why the blink came and went on most
//     rows (the frame boundary falling in that gap or not, the tip's speed
//     moving it); with the hold, no face changed on any of the six taps.
//     Everything else the leave does it still does: the tooltip goes at once
//     as on leaving any tool (architect 2026-10-06), the in-window bit drops,
//     the press arms, the drags, the cards' hover and the pen latch clear
//     (the popup list's scroll holds beside them until 2026-10-14).
// (The lit rows' clear, pointer_leave_keeps_lit_rows, which the pen's hover
// end skipped, went with the Settings popup, 2026-10-21.) Every clear the
// hook performs (main.cpp's body) is unconditional.

// THE PRODUCT'S ONE FRACTIONAL COORDINATE -> PIXEL CONVERSION (architect
// 2026-08-25). A SCREEN PIXEL x COVERS [x, x+1), so a surface coordinate names
// THE PIXEL THAT CONTAINS IT — floor, not rounding. Every fractional position
// this layer turns into a window pixel goes through here: the pen's hover
// enter and motion, and every one of the touch machine's deliveries and
// surface queries.
//
// NEAREST-POINT ROUNDING STAYS THE RULE FOR POINTS ON A GRID and is untouched
// by this: authored frames, lattice columns and sample indices are POINTS,
// where the nearest one is the right answer and banker's rounding
// (std::nearbyint) is the project's tie-break (CLAUDE.md "Rounding",
// snap_authored_frame) — save THE FRAME->COLUMN PLACEMENT, whose exact tie
// goes right so a moving mark steps one column per column of travel
// (lattice_index_ties_right, displayed_column_at, warp_frame_map_view.h;
// architect 2026-11-06, session 69). A CELL is not a
// point — asking which pixel a coordinate is IN has one answer and no tie to
// break — so the containment is the one answer for every device that hands
// this layer a fractional position.
inline int containing_pixel(double v) {
    return static_cast<int>(std::floor(v));
}

// THE TOUCH SLOP AT gui_scale 100, in device pixels — the value a freshly
// built core carries until the GUI pushes its own (set_touch_slop_px). It is
// the AUTHORED length (9 W, the drag gate's, kDragMovedThresholdPx in
// app_state.h); this layer never resolves it, because this layer never
// learns the scale.
inline constexpr double kDefaultTouchSlopPx = 9.0;

// THE MONOTONIC CLOCK EVERY DEADLINE IN THIS LAYER IS MEASURED AGAINST, in
// microseconds. One owner for the whole GUI: the key-repeat deadline and the
// backend's own bounded clipboard read both read it, so no second reading of
// the same clock can drift from this one.
uint64_t gui_monotonic_us();

class GuiInputCore {
public:
    using KeyCallback          = std::function<void(GuiKey key, GuiInputState mods)>;
    // A KEY RELEASE, carrying the key alone (2026-08-13). Key releases were
    // platform-internal until the modal dialog's buttons grew a keyboard
    // press-and-hold whose act is at the LIFT; the application needs the edge
    // and needs nothing else from it, so this callback deliberately hands over
    // no modifier state — the PRESS is what is modifier-exact and a release's
    // modifiers never select the act, as on_button_release's `mods` says of
    // the pointer (its one reader is the tooltip's held bit).
    using KeyReleaseCallback   = std::function<void(GuiKey key)>;
    using ButtonCallback       = std::function<void(int x, int y, GuiInputState mods)>;
    using MotionCallback       = std::function<void(int x, int y, GuiInputState mods)>;
    using CloseCallback        = std::function<void()>;
    // Predicate installed by main.cpp: true when a text editor is consuming
    // printable keys. Consulted at PRESS time and at every repeat fire: the
    // arm records it and a fire whose answer no longer matches disarms
    // (layer (2) of the repeat contract, at maybe_fire_repeat).
    using TextEditorProbe      = std::function<bool()>;
    // Predicate installed by main.cpp: true when the pressed key+mods should
    // arm key repeat. Consulted at PRESS time (after the codepoint is filled)
    // so a press that opens an editor — evaluated before the open — does not
    // arm, while typing inside an already-open editor does.
    using RepeatEligibleProbe  = std::function<bool(GuiKey, GuiInputState)>;

    // -- The backend's side of the seam ------------------------------------
    // Every method in this block is called by ONE backend with plain values;
    // none of them names a protocol type. The backend decodes units, protocol
    // enums and keymap state on its own side and calls in.

    // ONE KEY EVENT, already translated. `key` is the GuiKey the backend's own
    // keymap translation produced — an X11 keysym, ASCII case-folded, with
    // standalone modifier keysyms and F1..F35 already dropped — or 0 when this
    // GUI has no use for the key at all, which is the same thing as "there is
    // nothing to deliver". `stable_code` is the backend's PER-KEY IDENTITY, the
    // value the repeat cancel compares against
    // (the painted keyboard's own per-key code, synthesize_key): deliberately
    // NOT the keysym, since a layout
    // change mid-hold would break the match. `codepoint` is the UTF-32 value
    // the key resolves to under the live keyboard state, filled at the press
    // and re-filled per synthesized repeat through the codepoint probe.
    // `carried` ARE THE MODIFIERS THE KEY CARRIES ITSELF (GuiCarriedMods,
    // gui_input.h; the on-screen keyboard the one producer: Copy's and
    // Paste's ctrl, architect 2026-10-29, and LEFT's and RIGHT's latched
    // shift, 2026-11-08): each bit set ORs into the GuiInputState this press
    // is delivered with — and into every synthesized repeat armed off it, so
    // a held chord repeats as the chord, the way a physical Ctrl+key or
    // Shift+key hold does — whatever the tracked modifiers say; a release
    // ignores it, as a release carries no modifier state at all. It is a
    // property of the KEY EVENT, never of the tracked state: set_ctrl is not
    // touched, so no ctrl edge is announced and nothing else reads a
    // modifier that outlives the press.
    void key_event(GuiKey key, uint32_t stable_code, bool pressed,
                   uint32_t codepoint, GuiCarriedMods carried);

    // THE CTRL EDGE — the one tracked modifier, the only bit GuiInputState's
    // ctrl carries from this layer's tracked state (shift and alt are never
    // tracked here: the roster's long press raises shift on the chord itself,
    // downstream of this layer, a key's own carried shift rides its key
    // event alone, and nothing binds alt on glass). ITS ONE PRODUCER IS THE S
    // PEN'S SIDE BUTTON (architect 2026-09-25: the pen's button IS the Ctrl
    // bit — the glass's only Ctrl modifier, bare `k`'s sticky ctrl being a
    // lamp the flag press reads and not this bit, and the painted Copy and
    // Paste keys' ctrl riding their own key events, key_event's `carried`,
    // not this bit either): the backend calls this with ctrl = the button
    // held — for what the pen owns alone, its hover or a gesture
    // touch_owner_tool() reports as the pen's (an ignored pen passes
    // released) — BEFORE it delivers the touch or hover event that reported
    // the button state, so the press and the motions read the bit that event
    // carried; the pen's own lift is not sampled (its last leg keeps the
    // stroke's bit), and the bit drops after that lift, at a cancel, a hover
    // exit (or any pen report above the GUI's plane, 2026-09-27), a finger's
    // first down and focus loss. It owes the touch machine ONE thing of its
    // own: a ctrl edge under a live single-finger nav is announced to the GUI
    // as a nav frame (the exemption at set_touch_nav_hooks' update contract),
    // so the one-finger zoom's pivot seats and clears at the edge itself.
    void set_ctrl(bool ctrl);

    // THE KEY-REPEAT CADENCE, as the platform gives it (the backend's
    // hard-coded pair, platform_android.cpp's init): `rate` in Hz, positive,
    // and `delay` in milliseconds.
    void set_repeat_info(int32_t rate, int32_t delay);

    void pointer_enter(double x, double y);
    // THE CONTACT'S INSTRUMENT STAYS AS THE POINTER (architect 2026-10-07,
    // the S Pen's lift): pointer_enter's focus and position with NO motion
    // delivered, called by a backend immediately before the touch_up of the
    // LAST contact when that contact's instrument goes on hovering where it
    // lifted — the Android pen, whose lift is no leave (the rule is at the
    // Android backend's hover arm). Nothing is delivered because the
    // translation is still live (a motion now would carry its held button).
    // The lift's own translation end then reads the pointer as focused and
    // takes the focus fork's restore arm — the release, then an ordinary
    // MOTION at this position instead of the leave
    // (deliver_touch_translation_end) — so the hot face under the pen
    // survives the lift; an end that delivers nothing (a nav gesture, the
    // region, the caret) leaves the pointer resting here for the backend's
    // next hover report, a motion.
    void pointer_focus_at(double x, double y);
    // The pointer leaving — on the tablet the pen's hover ending
    // (end_pen_hover, 2026-10-10); the contact's lift is
    // deliver_touch_translation_end's own fire and does not come through
    // here (THE POINTER LEAVE'S THREE FIRES, above the class).
    void pointer_leave();
    // The pen's hover motion (the Android backend's hover arm), delivered at
    // once: the hover carries no button, so there is nothing to coalesce.
    void pointer_motion(double x, double y);

    // A CONTACT'S DOWN. `tool` is the backend's answer to "what touched"
    // (GuiTouchTool, gui_input.h — the pen's three amendments are there):
    // Finger unless the backend can tell a pen, which only the Android backend
    // can (a STYLUS or ERASER tool type). The core remembers the FIRST
    // contact's tool beside its id and reads it in two places: the
    // second-down arms, where a pen on either side of the pair opens no
    // two-finger navigation (the pen is never a pinch member), and
    // touch_owner_tool() below, the backend's question of who owns the
    // gesture its pen button would steer. Every other
    // answer of the machine — the zone, the window, the armed sweep, the
    // caret drag, the single-finger pan and its ctrl fork — is the finger's.
    void touch_down(int32_t id, double x, double y,
                    GuiTouchTool tool = GuiTouchTool::Finger);
    void touch_up(int32_t id);
    void touch_motion(int32_t id, double x, double y);
    void touch_frame();
    void touch_cancel();
    // THE TOUCH CAPABILITY GOING AWAY — the hard-end contract, shared whole
    // with touch_cancel (hard_end_touch_stream). Named rather than left to the
    // backend: the contract must not be droppable by replacing a backend body.
    void touch_capability_lost();

    // THE ONE SOFTWARE DEADLINE, SAMPLED: the key repeat's. The backend
    // calls this once per periodic tick, immediately after its own tick hook.
    // The deadline is never SCHEDULED — it is compared against a free-running
    // periodic wakeup — so its granularity is one tick period. (The touch
    // window's deadline, the region hold's beat, retired 2026-11-07: the
    // window is decided by geometry alone, input_core.cpp's constants block.)
    void tick();

    // THE ONE DOWNWARD PROBE. A synthesized key repeat must not reuse the
    // press's codepoint — the live keyboard state may have moved under the held
    // key — so each fire asks the backend to resolve the held key's stable code
    // afresh. Answering 0 (no keymap, no character) is legal and is what a
    // key that produces no character already means.
    void set_codepoint_probe(std::function<uint32_t(uint32_t stable_code)> probe);

    // -- The consumer's side of the seam -----------------------------------

    void set_on_key(KeyCallback cb);
    // THE KEY RELEASE HOOK (2026-08-13). It fires for the SAME key identity the
    // press delivered — the same keysym lookup, the same ASCII case-fold — and
    // carries no modifier state: a release binds nothing on its own, it can
    // only RESOLVE an arm an already-delivered press created. Null-safe.
    void set_on_key_release(KeyReleaseCallback cb);
    void set_on_button_press(ButtonCallback cb);
    void set_on_button_release(ButtonCallback cb);
    void set_on_motion(MotionCallback cb);
    void set_text_editor_active_probe(TextEditorProbe cb);
    void set_repeat_eligible_probe(RepeatEligibleProbe cb);

    // THE PLATFORM'S ADVERTISED KEY-REPEAT INTERVAL, in milliseconds (the
    // field's contract is at repeat_period_us_). A platform truth handed out
    // as a query: the GUI holds this class by reference and reads it
    // directly, no hook.
    //
    // ITS ONE APPLICATION CONSUMER is the CHROME BUTTON HOLD-REPEAT
    // (tick_chrome_press_repeat, input_pointer.cpp), which paces a held
    // repeating BUTTON at the rate its held KEY runs at rather than at a
    // constant of its own — so the two holds cannot drift apart if the
    // platform's repeat cadence changes. It is READ PER FIRE and never cached.
    //
    // The advertised value is a RATE in Hz, so the period is exact only for
    // divisors of 1000; the microsecond field is rounded to the nearest
    // millisecond here and floored at 1.
    int64_t key_repeat_period_ms() const {
        const int64_t ms =
            static_cast<int64_t>((repeat_period_us_ + 500ull) / 1000ull);
        return ms < 1 ? 1 : ms;
    }

    // Fired when the pointer LEAVES (pointer_leave — the pen's hover ending)
    // and at a touch pointer translation's end on its NO-FOCUS arm only
    // (after the lift's release, or after an abnormal end's lost-button
    // motion: the contact is gone and no pen hovers in the window; a FOCUSED
    // pointer gets a restore motion instead and this hook stays silent — the
    // focus fork, stated at deliver_touch_translation_end). The edges drop
    // pointer focus. IT CARRIES NO ARGUMENT since 2026-11-07: every consumer
    // reads the three fires alike (THE POINTER LEAVE'S THREE FIRES, above the
    // class).
    // The one owner of the drop-what-the-pointer-was-naming behavior. What
    // main.cpp wires it to is enumerated THERE, at the hook body, which is the
    // authoritative list — this contract deliberately does not keep a second
    // copy — but the shape is: every face and claim derived from where the
    // pointer is, so a pointer that has gone cannot leave a stranded pressed
    // interior or a hanging tooltip behind. Null-safe.
    void set_pointer_left_hook(std::function<void()> cb);

    // THE TOUCH NAVIGATION HOOKS (touch phase 1, 2026-08-11; SIX members
    // from PAN-PRIMARY's touch half, the eighth glass ruling 2026-08-12 —
    // update, end, the pan-zone query, and the REGION trio
    // begin/update/end, the dead trim-move members' exact pattern reborn for
    // the region former — TEN since 2026-09-05, when the EDITOR-FIELD
    // query and the CARET-DRAG trio joined for the third ruled divergence,
    // and ELEVEN since 2026-11-07, the SWEEP-ARMED query joining for the
    // TRIM arm lamp; the retired shapes are closed_questions.md's touch
    // section). ONE finger on
    // the glass IS the pointer — translated whole inside this class, so the
    // GUI sees ordinary pointer deliveries and cannot tell which contact
    // produced them (the translation
    // contract lives at the touch state block below) — with THREE ruled
    // exceptions, two born of the pan zone (the pan_zone query below) and
    // one of the open editor's field (the editor_field query below): a
    // one-finger DRAG whose DOWN POINT lies on the zone is
    // SINGLE-FINGER NAVIGATION — the finger drags the pan, the phone model —
    // delivered through
    // the SAME update hook with the finger as the centroid and dist_ratio
    // pinned at 1.0 (one finger has no distance, so no zoom) — EXCEPT WHILE
    // THE GUI'S TRIM LAMP IS LIT (the sweep_armed query below), when the
    // same drag with no ctrl is THE ARMED SWEEP: the crossing drives the
    // GUI's region former through the region trio from the down point
    // (architect 2026-11-07, session 87, retiring the hold-then-drag region
    // hold of 2026-08-12: "if I turn it on, it's deliberate and for a
    // sweep") — and a one-finger DRAG whose down point lies in an
    // OPEN EDITOR'S FIELD is THE CARET DRAG: the caret follows the finger
    // through the caret trio and nothing selects (architect 2026-09-05, the
    // phone's text vocabulary — a divergence from the pointer road's drag
    // there, the selection sweep, which is why it lives here). TWO fingers
    // are the ZOOM gesture: this layer measures both the
    // centroid delta and the distance ratio and delivers both, and the GUI
    // discards the centroid delta on a two-finger frame — two fingers zoom
    // and never pan since 2026-08-14 (the model at apply_touch_nav_update's
    // declaration, input_handler.h), which
    // is a GESTURE POLICY and therefore the GUI's, not this layer's. None of
    // the four delivers pointer events.
    // All are handed to the GUI through these hooks (main.cpp wires them
    // to the input handler's touch-nav body, which drives the strip-drag
    // family's viewport chokepoint, to the region former's own bodies and
    // to the editor's caret-seat bodies).
    //
    //   * update(frame): fired at most once per touch_frame while a
    //     navigation gesture is live and past its latch. The payload is a
    //     GuiTouchNavFrame (gui_input.h, the field contracts there): the
    //     CURRENT centroid, the centroid's horizontal delta and the
    //     finger-distance ratio against the previous DELIVERED frame, the
    //     finger count and the live ctrl bit — four measurements and the two
    //     bits the GUI forks on, every field read. The latch is the platform's: nothing is
    //     delivered until the centroid has travelled the touch slop (Chebyshev)
    //     from the gesture's start OR the finger distance has changed by that
    //     same slop, and the crossing update folds the whole accumulated
    //     delta — the strip drag's own press-becomes-drag model, so a
    //     two-finger tap navigates nothing (a single-finger nav is born past
    //     its latch: it exists only by crossing the window's slop, the
    //     same distance in the same Chebyshev metric, measured from the same
    //     down point).
    //     A NO-OP FRAME IS NOT DELIVERED — zero centroid delta and ratio 1.0
    //     say nothing — WITH TWO EXEMPTIONS, each a change of the gesture's
    //     MEANING under a finger that may be standing still, and each one
    //     single-finger frame carrying the finger's own centroid with both
    //     deltas at their no-op values:
    //       - THE DOWNGRADE: the two-to-one transition delivers one at the
    //         transition itself. The GUI's model reads a not-two-finger
    //         ARRIVAL as the end of the pinch (it is where the seated pivot is
    //         cleared and its stem erased), and a survivor that stands still
    //         would otherwise never produce one.
    //       - THE CTRL EDGE (2026-09-25, the S Pen's button): set_ctrl's
    //         edge under a live, latched single-finger nav delivers one
    //         carrying the NEW bit — after delivering, under the OLD bit, any
    //         frame already staged (motion made under one meaning is delivered
    //         under it). The GUI seats the one-finger zoom's pivot on a
    //         ctrl-down arrival and clears it on a ctrl-up one, so the stem
    //         appears and vanishes at the edge rather than at the next motion.
    //   * end(): the gesture ended — its LAST nav finger lifted (any end
    //     commits; one finger of TWO lifting is the DOWNGRADE, a transform
    //     the END hook never sees — the update hook announces it with the
    //     transition frame above; the edge inventory below),
    //     touch_cancel, or touch-capability loss. Fired ONLY if at least
    //     one update was delivered, so a sub-latch two-finger touch costs the
    //     GUI nothing.
    //   * pan_zone(x, y): THE ZONE QUERY — does this point lie on the
    //     one-finger PAN SURFACE? Asked ONCE per touch stream, at the FIRST
    //     finger's down; the answer is captured beside the down point and
    //     DECIDES WHETHER A WINDOW OPENS AT ALL (2026-09-25): ON the zone a
    //     window opens with no deadline, its slop crossing the pan (or the
    //     armed sweep), its lift the tap, a second finger inside it the
    //     pinch; OFF it (and outside an open editor's
    //     field) the down resolves to the POINTER ON CONTACT — no wait, no
    //     pinch — the touch state block below. SURFACE GEOMETRY ONLY — the
    //     GUI answers the navigation surface (the pan-primary vocabulary's
    //     one plain-drag surface, the waveform alone) less the surfaces
    //     floating over it, and nothing modal SAVE AN OPEN MENU, which
    //     yields the whole zone so the first touch outside it is the press
    //     that closes it (architect 2026-10-01; the carve-outs are the GUI's,
    //     at touch_point_in_pan_zone); every other refusal (prompt, editors,
    //     loading/empty audio, live pointer gesture) stays downstream — at the
    //     update body's per-frame state refusal for the nav gestures, in the
    //     region begin body for the armed sweep — so a refused pan freezes
    //     and a refused sweep is a dead stream rather than a fallback.
    //     THIS ANSWER IS THE GESTURE'S ONE SURFACE ANSWER (architect
    //     2026-10-16): a nav gesture born of the zone's window navigates
    //     wherever its point later travels — chrome, a card, the keys —
    //     until its end; nothing here or in the GUI re-asks the place.
    //     Null — or answering false — means no pan surface: the pointer on
    //     contact everywhere outside an editor's field.
    //   * sweep_armed(): THE SWEEP-ARMED QUERY (architect 2026-11-07,
    //     session 87) — is the GUI's TRIM arm lamp lit for this drag? Asked
    //     at a pan-zone window's SLOP CROSSING, and only when no ctrl stands
    //     (the barrel-held drag stays the one-finger zoom, lamp or not): true
    //     resolves the crossing to the region trio below instead of the
    //     single-finger pan. A STATE ANSWER, not geometry — the lamp is the
    //     GUI's (GuiInputHandler::touch_sweep_armed, dark in the `h` view);
    //     the begin's own refusals stay the begin body's. Null — or
    //     answering false — means the pan.
    //   * region_begin(x, y): the armed sweep resolved on the pan zone at the
    //     slop crossing — the GUI arms its region former at the DOWN point
    //     (the former's own
    //     press half: deselect, playhead seat, the drag arm), or refuses
    //     inside the body (prompt, the editors, the two overlay modes,
    //     loading/empty audio, live pointer gesture); a refused begin makes
    //     the two hooks below no-ops through the drag's own !active guard, so
    //     the refused stream is dead. NOTHING pointer-shaped starts — no
    //     press, no hold bit, no release, no translation end (the
    //     single-finger nav's model).
    //   * region_update(x, y): at most once per touch_frame while the
    //     region gesture is live — the finger's current position, absolute
    //     (the drag's one motion path converts to columns itself).
    //   * region_end(): the gesture ended — finger up, touch_cancel or
    //     touch-capability loss. Any end COMMITS (the former's own release
    //     regime: the moved drag has already written the trim per motion
    //     event, so its end runs the sweep's commit tail and the GUI puts the
    //     lamp out). Fired UNCONDITIONALLY once
    //     the gesture began — the GUI-side drag holds the drag-modal gate
    //     open and its release path is owed — the refused-begin stream
    //     covered by the same !active guard.
    //   * editor_field(x, y): THE EDITOR-FIELD QUERY (2026-09-05) — does this
    //     point lie in the ACTIVE EDITOR'S FIELD, and does a double-click
    //     candidate on that editor's text stand here (GuiTouchEditorField,
    //     gui_input.h — the answer's contract)? Asked ONCE, at the FIRST
    //     finger's down, beside the pan-zone query, captured beside the
    //     down point and cleared with them. It reads the field rect the
    //     GUI's own press claim reads — the marker lane's unrolled flag box
    //     or the dialog editor's edit control, the modal family's box's
    //     interior (2026-10-14), whichever editor is open — so the platform
    //     keeps no memory of taps and no second spelling of the field. It
    //     forks TWO things. (1) The window's SLOP CROSSING resolves to the
    //     CARET DRAG (below) — the field is off the pan zone, which is the
    //     waveform alone on the GUI's answer (the marker lane's box lies
    //     outside it, and the box empties it while a dialog editor stands),
    //     so no pan and no sweep can be meant there. (2) A Field answer
    //     OPENS A WINDOW where the pointer would otherwise land on contact, a
    //     window with no deadline (input_core.cpp's constants block): the
    //     architect's field vocabulary is a tap and a drag (2026-11-08,
    //     session 95, the double tap retired) and carries NO hold meaning,
    //     so nothing there may be decided by a duration — a motionless
    //     contact stays Pending however long it rests and its LIFT is the
    //     ordinary tap burst, the press seating the caret, while a contact
    //     that rests and then drags still moves the caret through the
    //     crossing.
    //     Null — or answering Outside — means no field: off the pan zone,
    //     the pointer on contact.
    //   * caret_begin(x, y): the window resolved to the caret drag at the
    //     SLOP CROSSING (its one road — no window expires)
    //     and this is the DOWN point: the
    //     GUI seats the caret under it through its one pointer seat body,
    //     drops any selection, clears the double-click seed (a caret drag is
    //     motion, and motion between two taps breaks a candidate), and
    //     damages the editor's surface. NOTHING pointer-shaped starts — no
    //     press, no hold bit, no release, no translation end (the region
    //     trio's model). A begin with no editor open is a no-op, the body's
    //     own guard, so the stream is dead rather than a fallback.
    //   * caret_update(x, y): at most once per touch_frame while the caret
    //     drag is live — the finger's current position, absolute; the GUI
    //     seats the caret under it, nothing selects.
    //   * caret_end(): the drag ended — finger up, touch_cancel or
    //     touch-capability loss. THE DRAIN RULE: the caret stays where it
    //     was last seated, nothing selects, nothing commits, nothing seeds a
    //     double-click, and the editor stays open with Enter its one commit
    //     route; the GUI restarts the blink so the resting caret is lit the
    //     instant the finger leaves. Fired UNCONDITIONALLY once the drag
    //     began (the region end's rule).
    //
    // The hooks carry ONE modifier bit, the nav frame's ctrl (the one-finger
    // zoom's fork, read on single-finger frames only); the GUI bodies read
    // nothing else modal from them, and their refusal answers are their own
    // (the update body's per-frame state refusal; the region begin's gate
    // list; the caret bodies' own no-editor guard). Null-safe.
    void set_touch_nav_hooks(
        std::function<void(const GuiTouchNavFrame&)> update,
        std::function<void()> end,
        std::function<bool(int x, int y)> pan_zone,
        std::function<bool()> sweep_armed,
        std::function<void(int x, int y)> region_begin,
        std::function<void(int x, int y)> region_update,
        std::function<void()> region_end,
        std::function<GuiTouchEditorField(int x, int y)> editor_field,
        std::function<void(int x, int y)> caret_begin,
        std::function<void(int x, int y)> caret_update,
        std::function<void()> caret_end);

    // THE TOUCH SLOP'S ONE DOOR — the travel, in DEVICE pixels, that this layer
    // measures every finger against. TWO USES, both Chebyshev and both `>=`:
    // the window's resolve by motion (the resolution forks on the down
    // point's answers — single-finger nav or the armed sweep on the pan
    // surface, the caret drag in an open editor's field, the only two places
    // a window opens), and the navigation
    // gestures' LATCH (centroid travel or finger-distance change past it
    // starts navigating, so a two-finger tap navigates nothing and a
    // single-finger nav is born past it by construction). (A third, the live
    // translation's MOVED latch that a second finger forked on, went with the
    // motionless-hold upgrade on 2026-09-25: a second finger landing on a
    // live pointer translation is ignored whatever the first has done.)
    //
    // IT IS A LENGTH, SO IT RIDES gui_scale — AND THE CORE NEVER LEARNS THE
    // SCALE. The authored length (8 laptop px then, 6 Windows px from
    // 2026-10-02, 9 W since the rebase of 2026-11-05) was 1.7 mm on the
    // retired road rig's 1024x600 panel at 100 %
    // and 0.8 mm on the tablet's 249 PPI one, which a fingertip's roll crosses
    // during a relaxed double tap; the GUI resolves it and pushes it here,
    // so this layer keeps measuring raw device pixels and knows nothing about
    // percent. The default is kDefaultTouchSlopPx, which is the authored value
    // and therefore exactly right at gui_scale 100 — so a core nobody ever
    // pushes to behaves as it always did.
    //
    // THE TWIN-GATE INVARIANT, and why this is a setter rather than a second
    // constant: the slop DELIBERATELY EQUALS the GUI's one generic
    // press-becomes-drag gate, so a finger's travel means one distance
    // whichever side decides it: on the pan zone this layer's crossing turns
    // the window into the pan, and off it — where the finger is the pointer
    // on contact since 2026-09-25 and its motion reaches the GUI raw — the
    // GUI's own gate turns the press into its drag at the same travel. (The
    // GUI's GRAB gate, twice this, 2026-09-29, has no twin here and needs
    // none: its surfaces, a flag and the trim bar, lie off the pan zone, so
    // this layer's crossing never delivers one of their presses —
    // grab_moved_threshold_px, app_state.h.) ONE NUMBER FLOWS TO BOTH: the
    // pushed value IS
    // drag_moved_threshold_px() (app_state.h), so the two cannot drift at any
    // scale, where before they were two literals kept equal by comment.
    //
    // THE PUSH HAS TWO CALL SITES, one per gui_scale application point, and
    // this is their inventory (there is no third; set_gui_scale_percent,
    // render.h, has exactly these two callers):
    //   * gui_main's startup, beside its set_gui_scale_percent push and BEFORE
    //     GuiPlatform::init — main.cpp. This is the INIT road:
    //     the scale arrives from the per-device config (device_config.h), which
    //     is read before a window exists, so no frame is ever painted and no
    //     finger is ever delivered at a scale other than the user's. IT MOVED
    //     HERE 2026-08-27, from the source load's tail, when the scale stopped
    //     being a `.settings` key and became a device fact — and the window that
    //     used to run at the authored default (a mapped surface with no source
    //     in it) closed with the move.
    //   * the settings editor's `gui_scale=` commit —
    //     GuiInputHandler::apply_gui_scale, input_handler.cpp. This is the LIVE
    //     road, and the new slop is in force from that commit onward.
    // Reached through GuiPlatform, which re-exports it like every other door of
    // this class.
    void set_touch_slop_px(double px) { touch_slop_px_ = px; }

    // TRUE WHILE ANY FINGER IS ON THE GLASS — the phase machine simply not
    // Idle. The full rationale (and why Drain's inclusion is harmless) is at
    // the touch state block below, beside the phases it reads; the ONE
    // consumer is main.cpp's pre-paint follow chase, which must not page the
    // song out from under a finger that is aiming or gesturing.
    bool touch_contact_active() const {
        return touch_phase_ != TouchPhase::Idle;
    }

    // THE TOOL OF THE CONTACT THAT OWNS THE LIVE GESTURE — the first
    // contact's (touch_down's door) while a gesture has an owner (Pending,
    // Pointer, Nav, Region, Caret); nullopt when none does: Idle, and Drain,
    // where the owner has lifted and only ignored contacts remain. Portable
    // (a fact of the machine, no platform word in it); the ONE consumer is
    // the Android backend's pen-button sampling (on_motion_event,
    // platform_android.cpp): THE PEN'S BARREL BUTTON IS THE CTRL BIT ONLY FOR
    // A GESTURE THE PEN OWNS, or for its hover with nothing on the glass, so
    // an ignored pen beside a finger never converts that finger's pan or
    // pending window (architect 2026-09-25; GuiTouchTool, gui_input.h).
    std::optional<GuiTouchTool> touch_owner_tool() const {
        if (touch_phase_ == TouchPhase::Idle ||
            touch_phase_ == TouchPhase::Drain)
            return std::nullopt;
        return touch_owner_tool_;
    }

    // THE LIVE MODIFIER TRUTH, on demand — the same GuiInputState every pointer
    // callback is built from: the tracked ctrl bit and the touch translation's
    // left hold; shift and alt are never raised here (set_ctrl's contract).
    // Read by the core's own deliveries and by the backend's pen-button
    // sampling (set_pen_ctrl, platform_android.cpp), which asks the standing
    // bit before it announces an edge.
    // THE STANDING SPLIT, which is why this is read live and never stashed: the
    // POSITION must be remembered (only pointer events carry it) and the
    // MODIFIERS must not (they are live here). A remembered copy of the
    // modifiers would be a second representation of a fact this object owns,
    // with its own staleness invariant to keep.
    GuiInputState current_mods() const;

private:
    // The tracked ctrl bit, written by set_ctrl alone and read into every
    // GuiInputState this layer builds.
    bool mod_ctrl_  = false;

    // Key repeat (last-key-wins, timerfd-tick-piggyback).
    // repeat_key_ is the GuiKey currently repeating (0 = none).
    // repeat_keycode_ is that key's stable per-key code, used so the key release event can match-and-cancel and so each
    // synthesized repeat recomputes its codepoint live through the codepoint
    // probe.
    // repeat_due_us_ is the next-fire monotonic time in microseconds; when
    // the playback tick fires and current_monotonic_us >= repeat_due_us_,
    // the on_key_ callback fires and repeat_due_us_ is advanced by the
    // repeat interval.
    // repeat_delay_us_ is the platform-advertised initial delay before
    // the first repeat (from set_repeat_info), in microseconds.
    // repeat_period_us_ is the platform-advertised inter-repeat interval
    // (1_000_000 / rate), in microseconds.
    // repeat_editor_ctx_ is the arm-time editor-active flag (from
    // text_editor_active_probe_); each fire re-checks that the live flag still
    // matches it. This boolean is sufficient because the editor target/session
    // can only change via a key or pointer-button event, and both kill the hold
    // outright — a key press re-arms or disarms via press-time eligibility, and
    // the touch translation's press clears repeat_key_ at its delivery — so no
    // armed hold can straddle a target change.
    // repeat_carried_ is the armed press's own carried modifiers (key_event's
    // contract), ORed into each fire's modifiers before the eligibility
    // re-check, so a held carried chord repeats as itself.
    GuiKey         repeat_key_       = 0;
    uint32_t       repeat_keycode_   = 0;
    bool           repeat_editor_ctx_ = false;
    GuiCarriedMods repeat_carried_{};
    uint64_t      repeat_due_us_    = 0;
    uint64_t      repeat_delay_us_  = 600'000;   // sensible defaults if the
    uint64_t      repeat_period_us_ = 33'000;    // platform is mute (600ms/30Hz)
    // THE BACKEND'S CODEPOINT REFILL (contract at set_codepoint_probe): asked
    // once per synthesized repeat, with the held key's stable code.
    std::function<uint32_t(uint32_t)> codepoint_probe_;

    // THE HOVERING PEN'S FOCUS AND LAST-KNOWN POSITION. The backend hands over
    // the pen's surface-local hover position as a fractional DOUBLE and
    // containing_pixel names the pixel it lands in; pointer_x_/pointer_y_ hold
    // the most recent such pixel, which the translation end's focus fork
    // restores the pointer to (deliver_touch_translation_end).
    bool pointer_focused_ = false;
    int  pointer_x_ = 0;
    int  pointer_y_ = 0;

    // -- Touch (the finger as the pointer; touch phase 1, 2026-08-11; the
    //    WINDOWED MODEL, restored at the sixth glass ruling, 2026-08-12) --
    //
    // THE UNIFIED-MODE PREMISE, made literal at this boundary: ONE finger IS
    // the pointer, translated whole here so the GUI sees ordinary pointer
    // deliveries — enter-motion, left press, motion, release — and cannot tell
    // which contact produced an event (no touch mode, no flag, no GUI-side
    // branch). TWO fingers are the
    // navigation gesture, delivered through the touch-nav hooks and NEVER as
    // pointer events. The WINDOW is what buys that vocabulary ON THE PAN ZONE
    // (a real pinch's two fingers land tens of milliseconds apart, so a press
    // delivered at the first contact jumps the playhead before it can zoom —
    // the timer-free model died of exactly that on the glass, 2026-08-12:
    // "every time I pinch, it jumps"), and the pan zone is the only surface
    // with a pinch, so off it the finger is the pointer on contact
    // (2026-09-25). A PEN is a finger here (GuiTouchTool, gui_input.h): the
    // backend tells the core which tool touched, and the core reads it only
    // to keep the pen out of every pinch.
    //
    // THE PHASES:
    //   * Idle    — no touch points.
    //   * Pending — the WINDOW: the first contact is down and NOTHING has
    //     been delivered. IT OPENS IN TWO PLACES ONLY (2026-09-25, architect:
    //     "zoom is not useful outside waveform", and the second finger's only
    //     use is the pinch): ON THE PAN ZONE and IN AN OPEN EDITOR'S FIELD,
    //     and in both it runs to NO DEADLINE (architect 2026-11-07 on the
    //     zone, the region hold retired; 2026-09-05 in the field, whose
    //     vocabulary has no hold meaning), so a resting contact simply stays
    //     Pending. Anywhere else the down
    //     resolves to Pointer ON CONTACT (the edge inventory's off-zone down),
    //     so a press on a flag, a button or the trim bar lands at the down
    //     with no wait. It resolves to Pointer on the contact lifting inside
    //     it (a TAP, however long the contact rested — the whole burst
    //     delivers at the lift, and on the
    //     navigation surface that burst's motionless press-release IS the
    //     deferred click act — the playhead placement or the lower half's
    //     scrub, the pointer road's own machinery, the TRIM lamp lit or not);
    //     on MOTION beyond
    //     the touch slop to SINGLE-FINGER Nav on the zone (the PHONE MODEL,
    //     second glass session 2026-08-11: the finger drags the pan, or
    //     under the ctrl bit the one-finger zoom; no press was ever delivered
    //     — nothing to unwind), or with the TRIM lamp lit and no ctrl to THE
    //     ARMED SWEEP (-> Region below; the sweep_armed query), and to the
    //     CARET DRAG in the field (2026-09-05,
    //     the editor_field query; -> Caret below), where the TAP is the
    //     field's one road to the pointer. A second FINGER landing inside the ZONE'S
    //     window resolves to two-finger Nav — the jump-free pinch, the
    //     window's whole point there; in the field's window, or where either
    //     contact is a PEN, it is ignored.
    //   * Pointer — the translation is live: the owning contact's motion is
    //     pointer motion (coalesced to the touch_frame boundary), its lift is
    //     the release AND THE TRANSLATION END ON THAT SAME EDGE — an ordinary
    //     leave, or a restore motion at the pointer when it has focus (the
    //     pen's lift, pointer_focus_at; the finger left the glass, which is
    //     what keeps hover faces from resting lit where a finger last was). A
    //     SECOND contact landing here is IGNORED whole, moved or not: a pinch
    //     is admitted only from the zone's window (2026-09-25), and a live
    //     translation lies off the zone by construction — the zone's only
    //     road here is the tap, which ends in the same call.
    //   * Nav     — ONE or TWO fingers drive the touch-nav hooks (contract at
    //     set_touch_nav_hooks; touch_nav_single_ splits them). Single-finger
    //     Nav is the phone model's pan: the finger is the centroid, the
    //     distance stays degenerate so every frame carries dist_ratio 1.0. A
    //     second finger landing during SINGLE-finger nav UPGRADES it to the
    //     two-finger gesture IN PLACE — same hook stream, the delta bases
    //     REBASED to the join (folding the centroid's jump to the pair
    //     midpoint would pan by half the finger gap), so pan is continuous
    //     and zoom is relative to the join. THE REVERSE IS THE DOWNGRADE
    //     since 2026-08-14 (the one-model ruling — the second finger is the
    //     zoom modifier, droppable at any time): one finger lifting from
    //     TWO-finger nav CONTINUES the gesture as the single-finger pan on
    //     the survivor, the delta bases rebased to the survivor's own
    //     position (the upgrade's rebase in reverse — folding the centroid's
    //     jump off the pair midpoint would pan by half the finger gap) and
    //     the latch state carried, so 1↔2 transitions repeat freely within
    //     one contact stream with no jump at any edge — and ANNOUNCED to the
    //     GUI by a single-finger frame delivered at the transition, so a
    //     survivor that never moves still ends the pinch (the no-op
    //     exemption at set_touch_nav_hooks' update contract). The gesture
    //     ends when its LAST nav finger lifts. A third finger is ignored,
    //     across upgrades and downgrades alike.
    //   * Region  — THE ARMED SWEEP (architect 2026-11-07, session 87; the
    //     REGION HOLD of 2026-08-12, the zone window's expiry, retired): the
    //     zone window's slop crossing with the TRIM lamp lit and no ctrl, and
    //     the owning contact now drives the REGION former through the region
    //     trio — begin fired at entry with the DOWN point (the GUI runs the
    //     former's own press half and arms the drag, or refuses), the
    //     crossing position staged as the first frame, motion
    //     staged to the frame boundary and delivered as region_update(x, y),
    //     the lift delivering the staged final frame then region_end (any
    //     end commits — the former's own release regime). NOTHING
    //     pointer-shaped ever starts: no press, no hold bit, no release, no
    //     translation end — the single-finger Nav model, not the Pointer
    //     one. A second finger landing here is IGNORED whole (a committed
    //     gesture — the moved-drag rule's family).
    //   * Caret   — THE CARET DRAG (2026-09-05, the third ruled divergence):
    //     the window's slop crossing with the down point in an
    //     OPEN EDITOR'S FIELD, and the owning finger now drives the editor's
    //     caret through the caret trio — begin fired at entry with the DOWN
    //     point (the GUI seats the caret there and drops any selection),
    //     motion staged to the frame boundary and delivered as
    //     caret_update(x, y) (a seat under the finger, nothing selecting),
    //     the lift delivering the staged final frame then caret_end (the
    //     caret stays where last seated; nothing selects, commits or seeds).
    //     NOTHING pointer-shaped ever starts: no press, no hold bit, no
    //     release, no translation end — the Region model. A second finger
    //     landing here is IGNORED whole (a committed gesture — the armed
    //     sweep's own rule; the field has no pinch at all, its window
    //     ignoring a second finger too).
    //   * Drain   — waiting for every remaining (ignored) finger to lift;
    //     nothing is delivered. Entered from Pointer, Nav, Region and Caret
    //     ends that leave ignored fingers on the glass.
    //
    // THE EDGE INVENTORY — every route that ends or transforms touch state,
    // authoritative here (the one-authoritative-site rule; each fire site
    // states only its own clause):
    //   * touch UP, owner, Pending  — a TAP, HOWEVER LONG THE CONTACT RESTED
    //     (no window has a deadline: in the editor field a slow tap is still
    //     a tap, seating the caret; on the zone a resting contact is never a
    //     hold, 2026-11-07): resolve
    //     (enter-motion + press at
    //     the down point, any queued motion), then the release, then the
    //     translation end on the release's own edge; -> the Pointer arm's own
    //     end, Drain if an ignored contact is still down or Idle if not (a
    //     Pending window can hold one since 2026-09-25 — a finger beside a
    //     pen's zone window, a pen beside a finger's, a second finger in the
    //     field's window; that contact's own up is the id-mismatch break
    //     above, not this clause). This window opens ONLY on the pan zone
    //     and in the field since 2026-09-25 (the placement lanes below never
    //     reach it), so ON THE PAN ZONE the burst's motionless press-release
    //     runs the GUI's DEFERRED CLICK ACT at its release — the placement at
    //     the tap point (the lower half's scrub), the TRIM lamp lit or not.
    //   * first DOWN OFF THE PAN ZONE AND OUTSIDE AN OPEN EDITOR'S FIELD
    //     (2026-09-25, every tool alike) — Idle -> Pointer AT ONCE, the
    //     window opened and resolved in the same call: the entry motion and the press at
    //     the down point, nothing queued, and every motion from here is
    //     Pointer motion. The press's identity is certain at the down there —
    //     no pan, no sweep and no pinch can be meant off the waveform — so
    //     nothing waits: a flag selects, a trim endcap grabs and a chrome
    //     button arms at the contact (the chrome's act and its shift long
    //     press stay the pointer road's own, at the lift and on the press's
    //     clock). THE PLACEMENT LANES (the ruler, the marker lane's empty
    //     stretches) are this same road with no window and no touch-side
    //     machinery of their own: a tap's ordinary press+release burst
    //     places exactly as a pen click does, and on the marker lane's
    //     empty stretch that release also seeds the EmptyLane double-click
    //     candidate, so TWO TAPS there reach the marker CREATE (the second
    //     tap's press consumes the candidate).
    //   * (NO EXPIRY EDGE: no window carries a deadline — the zone's since
    //     2026-11-07, the region hold and its stillness radius retired; the
    //     field's since 2026-09-05. A resting contact stays Pending at any age
    //     and leaves only by the slop crossing, its lift or, on the zone, a
    //     second down.)
    //   * motion beyond the touch slop inside the window — FORK on the down
    //     point's captured answers (the phone model, and the editor field
    //     since 2026-09-05): pan surface -> SINGLE-FINGER Nav (the nav seed
    //     measures its latch from the DOWN point, so the first delivered
    //     frame folds the whole accumulated delta exactly as the two-finger
    //     latch folds, and the frame's ctrl bit picks the pan or the zoom),
    //     or, the sweep_armed query answering true with no ctrl standing,
    //     -> Region, THE ARMED SWEEP (2026-11-07: the begin at the down point,
    //     the crossing position staged as the first frame);
    //     the editor's field -> Caret (the begin at the down point, the
    //     crossing position staged as the first frame, so the caret is seated
    //     where the finger landed and then under it in one burst).
    //   * second DOWN inside the ZONE'S window — Pending -> two-finger Nav,
    //     nothing delivered, whatever the owner's dwell, UNLESS either contact
    //     is a PEN (2026-09-25: the pen is never a pinch member), when the
    //     second is ignored — recorded (the point count), not routed.
    //   * second DOWN inside the FIELD'S window — ignored the same way: a
    //     pinch is admitted only from the zone's window.
    //   * second DOWN during Pointer — IGNORED whole, recorded (the point
    //     count), not routed, whether the translation has moved or not (the
    //     Pointer clause above: the translation lies off the zone).
    //   * second DOWN during SINGLE-finger Nav — the UPGRADE (a transform,
    //     not an end; the end hook is not owed): touch_nav_single_ drops,
    //     the second finger is recorded, and the delta bases REBASE to the
    //     join; the latch state carries (a live pan does not re-latch). An
    //     undelivered staged single-finger frame is DROPPED at the join — a
    //     sub-frame sliver the rebase supersedes (recorded at the site). A
    //     PEN on either side refuses it: the second contact is ignored and
    //     the single-finger gesture runs on.
    //   * second DOWN during Region — IGNORED whole: recorded (the point
    //     count), not routed — a committed gesture, the moved-drag rule's
    //     family (the armed sweep is born of a moved drag; there is no
    //     motionless sub-state to upgrade from). The pinch stays the
    //     window's: a second down BEFORE the crossing zooms, the lamp lit.
    //   * second DOWN during Caret — IGNORED whole, the Region clause's own
    //     terms: the finger becomes nothing, not a pinch pivot inside the
    //     field.
    //   * touch UP, owner, Pointer  — staged motion flushed, the release at
    //     the last position, THE TRANSLATION END ON THAT SAME EDGE (one
    //     owner, deliver_touch_translation_end), which FORKS ON POINTER
    //     FOCUS: the leave (the contact's lift) when no pen hovers in the window, an
    //     ordinary restore MOTION at pointer_x_/pointer_y_ when one does (the
    //     pen's own lift off an empty glass, pointer_focus_at) — the motion
    //     re-derives hover from truth;
    //     -> Drain if ignored fingers remain, else Idle.
    //   * touch UP, one of TWO nav fingers — THE DOWNGRADE (2026-08-14, the
    //     one-model ruling; the site's comment carries the full record and
    //     the no-re-join-window ruling): a transform, not an end — the end
    //     hook is not owed, exactly as at the upgrade. A staged PAIR frame
    //     DELIVERS FIRST — the two directions' ONE deliberate difference,
    //     since that frame is the pair's own completed motion and nothing
    //     supersedes it (the upgrade's single-finger sliver IS superseded by
    //     the join, and both fingers lifting in one touch_frame batch is
    //     what makes the difference load-bearing; the argument is at the
    //     site). THEN the survivor becomes the owner, the delta bases REBASE
    //     to its current position — which is also why the delivered frame
    //     cannot be applied twice — the distance basis drops to the
    //     single-finger degenerate 0.0, and the latch state CARRIES (an
    //     unlatched pair re-seats its latch reference at the survivor).
    //     AND THE TRANSITION IS THEN ANNOUNCED: one single-finger frame
    //     delivers off the rebased bases (dx 0.0, ratio 1.0 — the no-op
    //     exemption at update() above), iff a frame was ever delivered for
    //     this gesture, so the GUI hears the pinch end on the frame it ends
    //     rather than on the survivor's first real motion, which a
    //     stationary survivor never produces.
    //     An ignored third finger stays ignored.
    //   * touch UP, the LAST nav finger (single-finger nav: the owner) — the
    //     staged dirty frame DELIVERS
    //     first (the up comes before the touch_frame that closes its batch,
    //     and that motion is the user's own final leg — the
    //     any-end-commits family; the end split at end_touch_nav_gesture),
    //     then the nav end (commits through the end hook iff an update was
    //     delivered). A nav gesture never held the logical button, so no
    //     release and no translation end fire — no nav entry ever delivered
    //     a press; -> Drain / Idle (Drain iff IGNORED contacts remain — a
    //     third finger, or a pen or finger the no-pinch rules ignored; the
    //     two-finger survivor is the DOWNGRADE above, not a drain producer).
    //   * touch UP, owner, Region — the staged dirty frame DELIVERS first
    //     (the user's own final leg, the nav finger-up's model), then the
    //     region_end hook (any end commits — the former's release regime:
    //     the moved drag's trim writes are already in and its end runs the
    //     sweep's commit tail, the GUI putting the TRIM lamp out). No
    //     release and no translation end: nothing pointer-shaped ever
    //     started; -> Drain / Idle.
    //   * touch UP, owner, Caret — the Region clause's shape: the staged
    //     dirty frame DELIVERS first (the finger's own final leg — the caret
    //     lands where the finger lifted), then caret_end, which always fires
    //     (the drain rule at the contract: the caret stays, nothing selects,
    //     commits or seeds, the editor stays open). No release and no
    //     translation end; -> Drain / Idle.
    //   * touch UP, ignored finger  — bookkeeping only.
    //   * third DOWN during two-finger Nav / any DOWN during Drain — ignored:
    //     recorded (the point count), not routed — the mid-gesture rule
    //     above.
    //   * touch_cancel — the window system claims the touches. One contract
    //     with capability loss: a live Pointer translation takes THE ABNORMAL
    //     END (2026-08-29 — a touch the system takes away is not a click
    //     unless it moved), a lost-button MOTION delivered at the last
    //     position (an end, not a vanish: a moved drag finalizes, an unmoved
    //     press commits nothing, and the drag-modal gate un-latches either
    //     way) then the
    //     translation end on that delivery's own edge (the UP clause's
    //     focus fork applies here identically — one owner); a live Nav
    //     gesture (single- or two-finger — one arm) DROPS its staged dirty
    //     frame
    //     (the window system's claim means that motion retroactively was not
    //     ours — the recorded asymmetry with the Pointer translation, whose
    //     edge MUST deliver whatever it delivers; the end split at
    //     end_touch_nav_gesture) and ends through
    //     its end path (commits iff an update was delivered); a live REGION
    //     gesture takes the Nav shape — staged frame DROPPED, then
    //     region_end (the end itself ALWAYS fires: the GUI-side region drag
    //     holds the drag-modal gate open and its release path is owed; only
    //     the final MOTION is dropped, and dropped motion wedges nothing);
    //     a live CARET drag takes that same shape — staged frame DROPPED
    //     (no seat), then caret_end (no selection, no seed; the caret stays
    //     where the last delivered frame seated it and the editor stays
    //     open — a back-swipe mid-drag leaves the edit standing);
    //     an unresolved
    //     Pending window is dropped silently (nothing was delivered, so
    //     there is nothing to end); all touch state forgotten either way.
    //   * TOUCH capability loss — the cancel contract above, then the backend
    //     releases its touch object. Shutdown releases that object with no
    //     deliveries (the backend's teardown is not an input edge — it
    //     delivers nothing).
    //
    // TOUCH NEVER WRITES pointer_x_/pointer_y_ (a deliberate split): those
    // fields are the hovering pen's own, and every touch delivery carries its
    // own explicit coordinates, so the unified "one pointer" view lives in the
    // DELIVERIES. Touch gates NO DELIVERY on pointer_focused_ (touch delivers
    // only to the touched surface). The split has ONE deliberate reader:
    // deliver_touch_translation_end reads pointer_focused_ — and
    // pointer_x_/pointer_y_ on the focused arm — because the translation's END
    // asks where the pointer now is: the backend's pointer_focus_at at the
    // pen's lift is a pointer door, the pen going on as the hovering pointer,
    // not a touch write (architect 2026-10-07).
    //
    // ANY CONTACT DOWN PAUSES THE FOLLOW CHASE — the one thing outside this
    // layer that reads the phase machine, through touch_contact_active()
    // above. THE HONEST PHONE-MODEL STATEMENT: a finger on the glass means the
    // user is aiming or gesturing, and the autopager must not move the song
    // under it. It is not a nicety — the window's whole design defers
    // conversion: a TAP delivers its press+release burst AT THE LIFT carrying
    // the DOWN point's coordinates (any time later, the window having no
    // deadline) and THE ARMED SWEEP converts the down point at the slop
    // crossing, so a chase that paged in between would
    // land the act on whatever frame had slid under that screen column instead
    // of the frame aimed at. Nothing GUI-side is armed during Pending, so
    // any_pointer_gesture_active cannot see that window — this query is what
    // covers it, along with the live translation, Nav, Region and Drain. DRAIN
    // IS IN BY CONSTRUCTION rather than by need (nothing converts during a
    // drain): the rule is "any contact down", which needs no phase list to
    // stay true as phases are added.
    enum class TouchPhase { Idle, Pending, Pointer, Nav, Region, Caret, Drain };
    TouchPhase touch_phase_       = TouchPhase::Idle;
    // ALL live touch points, the ignored ones included — the Drain exit test.
    int        touch_point_count_ = 0;
    // Pending/Pointer: the translating finger. Positions are surface-local
    // doubles (touch positions are fractional on real panels); deliveries
    // resolve through containing_pixel, the product's one fractional
    // coordinate -> pixel conversion.
    int32_t    touch_owner_id_    = 0;
    // The travel every finger is measured against, in DEVICE pixels — the
    // three uses, the gui_scale story and the twin-gate invariant are at
    // set_touch_slop_px. Born at the authored 100 % value so a core nobody
    // pushes to is the core this file always had.
    double     touch_slop_px_     = kDefaultTouchSlopPx;
    double     touch_down_x_      = 0.0;
    double     touch_down_y_      = 0.0;
    double     touch_last_x_      = 0.0;
    double     touch_last_y_      = 0.0;
    // Pending only: whether any sub-slop motion arrived inside the window
    // (the queued motion the resolution replays).
    bool       touch_window_moved_       = false;
    // Pending: the down point's PAN-ZONE answer, captured ONCE at the first
    // contact's down (the pan_zone query at set_touch_nav_hooks). It decides
    // whether a window opens at all (off the zone and outside a field the
    // down is the pointer on contact), forks the slop crossing to the pan or
    // the armed sweep, and admits the pinch at the second-down arm.
    bool       touch_down_in_pan_zone_   = false;
    // The down point's EDITOR-FIELD answer (2026-09-05), captured ONCE beside
    // the pan-zone bit above at the first finger's down (the editor_field query
    // at set_touch_nav_hooks) and cleared with them in forget_touch_state.
    // Field opens a window (deadline-free like every window, so the tap
    // survives any dwell) and forks its crossing to the caret drag. Outside
    // leaves the answer to the pan-zone bit.
    GuiTouchEditorField touch_down_in_editor_field_ = GuiTouchEditorField::Outside;
    // Region: a finger position staged for the touch_frame boundary
    // (the Nav dirty-frame cadence; delivered as region_update(x, y)).
    bool       touch_region_frame_dirty_ = false;
    // Caret: the same staging for the caret trio (delivered as
    // caret_update(x, y)).
    bool       touch_caret_frame_dirty_  = false;
    // The FIRST contact's tool (touch_down's door), captured beside its id
    // and read at the second-down arms (a pen on either side of a pair
    // opens no two-finger navigation) and by touch_owner_tool().
    GuiTouchTool touch_owner_tool_ = GuiTouchTool::Finger;
    // THE LOGICAL LEFT BUTTON: true while the touch translation holds it
    // (raised at the resolution's press, dropped at the translation's end),
    // read by current_mods() into GuiInputState::primary_button_held. The
    // translation is its one source.
    bool       touch_left_held_          = false;
    // Pointer: a motion staged for the touch_frame boundary (flushed ahead
    // of the release).
    bool       touch_frame_motion_pending_ = false;
    // Nav: ONE finger (the phone model's pan — the finger is the centroid and
    // the distance fields stay degenerate) vs two. touch_nav_id2_ and the
    // second finger's positions are meaningful only while this is false.
    bool       touch_nav_single_ = false;
    // Nav: the second finger and the per-finger latest positions.
    int32_t    touch_nav_id2_   = 0;
    double     touch_nav_x1_    = 0.0;
    double     touch_nav_y1_    = 0.0;
    double     touch_nav_x2_    = 0.0;
    double     touch_nav_y2_    = 0.0;
    // Nav: the gesture START (the latch reference — the two-finger seed, the
    // single-finger down point, or an upgrade's join) and the last DELIVERED
    // centroid/distance (the per-frame delta basis). Single-finger nav keeps
    // the distance fields at 0.0.
    double     touch_nav_start_cx_   = 0.0;
    double     touch_nav_start_cy_   = 0.0;
    double     touch_nav_start_dist_ = 0.0;
    double     touch_nav_last_cx_    = 0.0;
    double     touch_nav_last_dist_  = 0.0;
    bool       touch_nav_latched_    = false;
    // An update hook fired — the end hook is owed a commit.
    bool       touch_nav_delivered_  = false;
    // Positions moved this touch frame (compute + deliver at the frame).
    bool       touch_nav_frame_dirty_ = false;

    // -- Callbacks --
    KeyCallback          on_key_;
    KeyReleaseCallback   on_key_release_;
    ButtonCallback       on_button_press_;
    ButtonCallback       on_button_release_;
    MotionCallback       on_motion_;
    TextEditorProbe      text_editor_active_probe_;
    RepeatEligibleProbe  repeat_eligible_probe_;
    // The one owner of the pointer-leave drop: fired at pointer_leave and at a
    // touch translation end's NO-FOCUS arm (the focus fork at
    // deliver_touch_translation_end — a focused pointer gets a restore motion,
    // not this hook). Wired to everything derived from where the pointer is,
    // main.cpp's hook body holding the authoritative list. It carries no
    // reason (THE POINTER LEAVE'S THREE FIRES, above the class: every
    // consumer reads them alike since 2026-11-07). Null-safe.
    std::function<void()> pointer_left_hook_;
    // The touch navigation hooks (see set_touch_nav_hooks). Null-safe at
    // each fire site.
    std::function<void(const GuiTouchNavFrame&)>  touch_nav_update_hook_;
    std::function<void()>                         touch_nav_end_hook_;
    // The pan-zone query (asked once, at the first finger's down; surface
    // geometry only — the contract at set_touch_nav_hooks).
    std::function<bool(int, int)>                 touch_pan_zone_hook_;
    // The sweep-armed query (asked at a pan-zone window's slop crossing with
    // no ctrl standing; the contract at set_touch_nav_hooks).
    std::function<bool()>                         touch_sweep_armed_hook_;
    // The region trio the Region phase drives (contracts at
    // set_touch_nav_hooks). Null-safe at each fire site.
    std::function<void(int, int)>                 touch_region_begin_hook_;
    std::function<void(int, int)>                 touch_region_update_hook_;
    std::function<void()>                         touch_region_end_hook_;
    // The editor-field query (asked once, at the first finger's down, beside
    // the pan-zone query; the contract at set_touch_nav_hooks) and the
    // caret trio the Caret phase drives. Null-safe at each fire site.
    std::function<GuiTouchEditorField(int, int)>  touch_editor_field_hook_;
    std::function<void(int, int)>                 touch_caret_begin_hook_;
    std::function<void(int, int)>                 touch_caret_update_hook_;
    std::function<void()>                         touch_caret_end_hook_;

    // -- Internal helpers --
    void deliver_key(GuiKey key, GuiInputState mods);
    void maybe_fire_repeat();

    // DELIVER A MOTION THAT CARRIES A REAL POINTER POSITION — the one funnel
    // for every such delivery, each built with current_mods(). Its callers
    // are pointer_enter, the pen's hover pointer_motion, and the FOUR
    // touch-translation deliveries (the resolution's entry motion and its
    // queued motion, the touch frame flush, and the translation end's
    // re-delivery at the hovering pen's spot) — touch is the pointer by
    // ruling, so its deliveries are the pen's hover's kind.
    void deliver_motion(int x, int y);

    // -- The touch machine's internals (the phase machine and the edge
    // inventory are at the touch state block above) --
    // Resolve the Pending window to the POINTER translation: deliver the
    // synthesized enter-motion at the ORIGINAL down point, the left press
    // there, then any queued motion inside the slop (the sliver's rule at the
    // definition). Shared
    // by all three pointer resolutions — the OFF-ZONE DOWN (on contact,
    // 2026-09-25), the editor field's DOUBLE PRESS (on contact), and the tap
    // (whose caller then delivers the release + the translation end itself,
    // through deliver_touch_translation_end).
    void resolve_touch_window_to_pointer();
    // Resolve the Pending window to SINGLE-FINGER NAV instead (the phone
    // model's slop-crossing arm, taken when the captured down point lay in
    // the pan zone): seed the Nav state from the DOWN point, unlatched, so
    // the first delivered frame runs the ordinary latch test and folds the
    // whole accumulated delta — "latched and folded exactly as the
    // two-finger latch is", literally the same code path. No press was ever
    // delivered and the touch hold is never raised.
    void resolve_touch_window_to_single_nav();
    // Resolve the Pending window to THE ARMED SWEEP (the on-zone slop
    // crossing with the sweep_armed query true and no ctrl — 2026-11-07):
    // fire the region begin at the DOWN point and stage the crossing
    // position as the gesture's first frame. Nothing pointer-shaped starts —
    // no press, no hold bit — the single-nav model.
    void resolve_touch_window_to_region();
    // Resolve the Pending window to the CARET DRAG (2026-09-05 — the slop
    // crossing with the down point in an open editor's field):
    // fire the caret begin at the DOWN point and stage any window travel —
    // the crossing position, or sub-slop drift — as the drag's first frame.
    // Nothing pointer-shaped starts — no press, no hold bit — the region
    // model.
    void resolve_touch_window_to_caret();
    // Deliver the staged Pointer-phase motion (touch_frame coalescing) —
    // called at the frame boundary and UNCONDITIONALLY at every end of the
    // touch hold (the flush-vs-clear invariant is at the definition).
    void flush_touch_frame_motion();
    // End the touch translation's hold on the logical left: flush the staged
    // TOUCH motion while the bit still reads held (so the flushed motion takes
    // the live-drag path, not the button-lost teardown), clear the bit, then
    // deliver at the owner's last position.
    // clean_release picks WHAT that edge delivers: true =
    // the left RELEASE, the press ending as the click it was; false = THE
    // ABNORMAL END, a lost-button MOTION instead (the hold bit still drops,
    // so nothing sticks) — the product's own button-lost spelling, under
    // which a moved drag finalizes and an unmoved press commits nothing.
    // THE FINGER'S OWN LIFT IS THE ONLY CLEAN CALLER since 2026-08-29: the
    // two HARD ENDS are the ends where no finger left, and they take the
    // abnormal one (architect: a touch the system takes away is not a click
    // unless it moved).
    // RETURNS whether the end was delivered — the translation end's own
    // edge: every end site goes through deliver_touch_translation_end below,
    // which acts iff the end fired (no hold standing, or no consumer
    // installed, delivers nothing).
    bool end_touch_left_hold(bool clean_release);
    // The one owner of the translation's END, called by the touch-up site
    // and the hard-end contract: end the touch hold, and on the delivering
    // edge FORK ON POINTER FOCUS — a pen hovering in the window (its own
    // lift off an empty glass, pointer_focus_at) gets an ordinary restore
    // MOTION at its last platform-tracked position instead of the leave, no
    // focus gets the leave. clean_release is passed
    // through to end_touch_left_hold (the release or the lost-button motion;
    // the leave carries no reason since 2026-11-07): the
    // finger's OWN LIFT passes true, and the ends where no finger left — the
    // HARD ENDS (2026-08-29) — pass false. Rationale, edges and
    // the resting-anchor judgment at the definition.
    void deliver_touch_translation_end(bool clean_release);
    // Compute + deliver the Nav frame's centroid/distance update through the
    // update hook (latch, fold-at-crossing, per-frame deltas — the contract
    // is at set_touch_nav_hooks). Single-finger nav forks only the three
    // input reads: the finger is the centroid and the distance stays 0.0, so
    // the pinch latch arm is structurally false and the ratio guard delivers
    // 1.0 — everything downstream is shared verbatim.
    // A DELIVERED FRAME IS NEITHER A KEY-REPEAT DISARM EDGE NOR A TRANSPORT
    // HOLD EDGE, by judgment rather than omission: those are ruled event-edge
    // enumerations (a key press, a pointer press) and a navigation frame is
    // neither.
    // deliver_even_if_no_op exempts THIS call from the body's exact-no-op
    // return (dx 0.0 and ratio 1.0 deliver nothing), and it has TWO callers,
    // each a change of meaning under a finger that may be standing still (the
    // exemption at set_touch_nav_hooks' update contract): the DOWNGRADE —
    // the two-to-one transition owes the GUI a not-two-finger frame, that
    // arrival being the only thing that tells it the pinch is over and clears
    // its seated pivot — and the CTRL EDGE under a live single-finger nav
    // (set_ctrl), whose frame carries the new bit so the one-finger
    // zoom's pivot seats or clears at the edge. It does not exempt the LATCH,
    // so a sub-latch pair still navigates nothing. Every other call passes
    // false.
    void deliver_touch_nav_frame(bool deliver_even_if_no_op);
    // End a live Nav gesture. deliver_final_frame selects the END SPLIT
    // (recorded at the definition): the finger's own lift delivers the staged
    // dirty frame first (true — the user's final motion, the any-end-commits
    // family), the hard ends drop it (false — the window system's claim means
    // that motion retroactively was not ours). Either way the end hook fires
    // iff an update was delivered.
    void end_touch_nav_gesture(bool deliver_final_frame);
    // End a live Region gesture (the armed sweep) — the Nav end's own
    // split, restated for the region trio (the finger's lift delivers the
    // staged frame first, the hard ends drop it) with ONE deliberate
    // difference: region_end fires
    // UNCONDITIONALLY, the GUI-side drag holding the drag-modal gate open
    // (the trim-move precedent; rationale at the definition).
    void end_touch_region_gesture(bool deliver_final_frame);
    // End a live Caret drag — the Region end restated for the caret trio:
    // the finger's lift delivers the staged frame first (the caret lands
    // where the finger lifted), the hard ends drop it (no seat), and
    // caret_end fires UNCONDITIONALLY either way.
    void end_touch_caret_gesture(bool deliver_final_frame);
    // THE HARD-END CONTRACT, shared verbatim by touch_cancel and
    // touch-capability loss (the edge inventory names it): commit-and-END a
    // live Pointer translation (the release, then the focus-forked translation
    // end through deliver_touch_translation_end), end a live Nav, Region or
    // Caret gesture (staged frame dropped), drop a Pending window silently,
    // forget all touch state.
    void hard_end_touch_stream();
    // Reset every touch field to its rest value (the one forget, so a new
    // edge cannot land with one field remembered).
    void forget_touch_state();
};
