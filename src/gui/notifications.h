#pragma once

// THE NOTIFICATION CARDS (architect design 2026-08-29, docs/engineering/
// architecture/messaging.md) — the product's surface for EVENTS: something
// happened that answers an act, or that the user was not watching. A small
// dark card stacked top-right under row 1's view radios, newest on top,
// EVERY CARD IN THE STACK VISIBLE, UP TO kNotificationMaxLines lines of
// the one sans, a Breeze glyph at the left naming the class, an X at the
// right, and ONE PAD around all three (notification_pad_px below — the
// card's chrome reads one number on all six of its distances). A sentence
// too long for one line WRAPS DOWNWARD UNDER THE TWO ICONS (architect
// 2026-08-30): the card grows taller, the glyph and the X stay at the first
// line's height, and nothing reflows beside them. Two classes and nothing
// else — no remaining-time bar, no actions, no title/body split, no sound
// (the architect is "not a big fan of notifications": minimal):
//
//   NORMAL   — every refusal that has a sentence and every act's report
//              (the inventory is messaging.md's, re-greped there): the load
//              act's own refusals and the player's two before them, the
//              player's opener and decode refusals, the propagate pastes'
//              "Stopped at …" reports, the measure paste's, the picker's
//              three refusals, Synchronize's refusals, "Target render
//              failed", "History is unavailable", and — since 2026-08-30 —
//              THE GATES' OWN CARDS, the swallowed press answered by the
//              state that swallowed it (the editor gate, the FOUR drag gates
//              — the editor text drag, the pointer gestures' drag-modal gate
//              and the player's and the picker's own arms above it — the
//              loading gate, the `h` allowlist and the read-only lock). THREE
//              OF THEM NAME THE CHORD through the one speller spell_chord
//              (gui_input.h), and every gate ON THE MAIN DISPATCH speaks only
//              for a chord this product BINDS (chord_is_bound, gui_input.h —
//              the unbound-keys ruling below; the player's and the picker's
//              own drag arms are the two that do not ask, each standing
//              inside a mode whose router is its own vocabulary). And THE
//              ACTS' OWN REFUSALS beside them — the
//              home-view binding's four sentences, the marker verbs' subject
//              refusals, the walls and the value facts, the four clipboard
//              chords, the value pair, undo and redo, the ten arms of the
//              BPM gate, the marker walk's wall, the `h` walk's and the diff
//              cycle's ends, the trim family's four, playback's and the
//              audition's, and the render chords' — and, beside them, EVERY
//              RED FLASH'S REASON (2026-08-30): the seven commit refusals of
//              the flag-editor cluster, the settings editor's five, the
//              commit title's blank, the offset editor's grammar, the measure
//              paste's two and the text editor's two capacity refusals. Each
//              is ONE SENTENCE WITH TWO READERS, the stderr line the site
//              already printed and the card; the two that never had an stderr
//              line have the card alone. Leaves on its own
//              kNotificationMs after it
//              became visible (gui_input.h; the pointer resting on it pauses
//              the clock), at its X, or at a bare Esc that reaches the stack.
//   CRITICAL — the four checkpoint outcomes and nothing else today. Stands
//              until its X or that same Esc; no clock.
//
// WHAT IS NOT A CARD, by ruling. ALMOST EVERY SUCCESS: a render's completion
// ("that would get annoying"), a Synchronize that mirrored the project, a
// propagate walk that pasted what it had, and THE SAVE — an act that did what
// was asked says nothing, its result being on screen, the save's being the
// dirty mark leaving the window title (architect 2026-08-30: "the disc
// writes — there is something that paints, the dirty dot goes away").
// THE ONE EXCEPTION IS THE CLIPBOARD WRITE (architect 2026-08-30, the
// invariant that an accepted press shows something): NOTHING PAINTS A
// CLIPBOARD, and since the resolved readout retired nothing paints a resolved
// value either, so the THREE COPIES — bare `j`'s resolved value, Ctrl+P's
// phase resets and Ctrl+/'s measures, each with its button or menu row
// inheriting the chord — say so on a normal card, which is the whole of what
// those presses show. THE EDITORS' OWN Ctrl+C IS NOT ONE OF THE THREE: an
// editor is its own world with that world's conventions, and its copy stays
// silent (its Ctrl+V over an empty clipboard with it).
// And THE SILENCES THE STRICTNESS RULING LEFT
// STANDING, which are these and no others (2026-08-30, re-greped; the
// off-home DROPS, `m` on a bad run and bare `h` with a checkpoint publishing
// left this list that day, and every one of them speaks now):
//   * A CHORD THIS PRODUCT BINDS NOWHERE, wherever it is pressed and whatever
//     swallowed it (architect 2026-08-30, the day's last ruling): "bound keys
//     either show an effect or a card, so an unbound key is identified by its
//     silence". THE DEDUCTION IS THE POINT — every other refusal answers now,
//     so a press with no card and no visible change can only be a press with
//     no binding — and it is why the "<chord> is not bound" class retired
//     whole that evening (the strict-modifier tail, the unbound bare default,
//     the render player's and the picker's two catch-alls each, the folder
//     overlay's modified press on a row, and the pointer's own
//     "<modifier>+click is not bound here"), and why every gate ON THE MAIN
//     DISPATCH asks chord_is_bound (gui_input.h) before it speaks. THE
//     UNBOUND POINTER PRESS goes with it: a modified press the waveform, the
//     top strip or the overlay's band binds nothing for says nothing.
//   * TOP-LEVEL BARE ESC WITH AN EMPTY STACK — a retraction with nothing to
//     dismiss. That arm's other half is an ACT since 2026-08-31: Esc dismisses
//     the OLDEST card when one stands (the key is the X's keyboard twin, the
//     hit section below), and the silence is what is left when there is none.
//     Esc inside a
//     gate is NOT this case: Esc is a BOUND chord, so the drag gates card it
//     with every other bound key.
//   * THE RENDER PLAYER'S MODIFIED PRESS ON THE SCRUB TRACK (the folder
//     overlay's band, pad, gaps and rows joined the unbound-gesture silence
//     above).
//   * THE T+W POINTER AUTHORING PAIR — the FLAG DRAG and the EMPTY-LANE
//     DOUBLE-CLICK DROP (re-greped 2026-08-30; the WARP column's alone since
//     the P column opened to both audio views that day, the four P-column
//     cards retiring with their refusals) — because a pointer gesture that
//     never begins is its own
//     answer: the flag does not move and no marker appears. The double-click
//     drop's READ-ONLY arm is silent on the same ground, the keyboard's own
//     lock speaking for the chord.
//   * A GREYED BUTTON'S LIFT — the grey IS the message (the truthful-buttons
//     ruling's division: the roster answers the pointer, the card answers the
//     keyboard), so the press dies at arm_redesign_press's disabled line.
//   * THE TARGET-VIEW ENTRY GATE — the preview must never receive a map its
//     builder refuses, and its refusals are unreachable from program-written
//     input, so it prints one stderr line and shows nothing, exactly as the
//     load road's own fallback always did.
//   * THE NO-PRODUCER BELTS, where an error arm would exist without a
//     producer (validation_topology.md): the drop's last-frame wall (both
//     columns — every drop road authors at the playhead, which rests inside
//     the domain), the position nudge's leading state guards (each either the
//     loading gate's card one level up or a belt against a state the
//     selection layer cannot be in), and
//     enter_bpm_mode's five-bail recheck, whose every arm the `m` gate has
//     already carded one level up.
// Also not a card: the loader's fatal exits (adversarial class:
// stderr and exit 1); every QUESTION (the prompts and the dialog editors); and
// what is TRUE NOW rather than what happened — the render's progress line and
// the `h` walk's line, the two STATE strings, which live in ROW 8'S STATE CELL
// right of the clock. (The player's LOAD UNDER A RUNNING RENDER was on this
// list for the one day a status bar stood at the window's foot to explain it,
// and is a card since the fold: the state cell is row 8's, whose lane the
// player's own modal row takes whole, so that refusal has nothing beside it.)
//
// ONE PUSH CHOKEPOINT: GuiNotifications::notify. Every producer above calls
// it and nothing else writes a card. A text identical to a card already in
// the stack in the same class does not stack a duplicate — the stack keeps
// ONE card for that event and THAT CARD IS RE-PUSHED AT THE TOP with a fresh
// clock, in both classes alike (architect 2026-08-30). It is not re-armed
// where it stands, because "in the stack" is not "on screen": an overflowing
// stack is clipped at the room's foot, so a live card can be wholly
// invisible, and the answer to the act the user has just performed must be
// visible. The top is also what a repeat means — the last time it happened.
//
// THE STACK IS UNCAPPED AND THE QUEUE IS RETIRED (architect 2026-08-30,
// "cards bump each other off the screen — newest on top, the oldest leaving;
// critical cards keep standing"). EVERY CARD IS VISIBLE FROM THE MOMENT IT IS
// PUSHED until it expires or is BUMPED — nothing waits unseen, so a normal
// card's clock always starts at its push and never at some later surfacing,
// and `notification_visible` is simply "the id is in the stack". What limits
// the stack is the ROOM, counted in one-line cards (notification_capacity):
// a push past that count removes THE OLDEST NORMAL CARD, walking from the
// back and SKIPPING every critical one, which is what "critical cards keep
// standing" means. A stack of criticals alone therefore keeps growing, and a
// burst of MULTILINE cards can exceed the room in pixels while sitting inside
// it in count; both overflow past the room's foot and the painter's clip is
// the answer, not a second rule.
//
// THE CLOCK RIDES THE RUN LOOP'S OWN DEADLINE TICK, polled: fire_if_due is
// called at the HEAD of main.cpp's on_tick, above every early return that
// body has — the startup load's, the render player's fork, the loading/blank
// guard and the playing-only guard — because a card is a message about
// something that has already happened, so nothing the tick does below can be
// a precondition for retiring one, while every one of those returns is a mode
// a card can stand over (the render player's own "No audio device; the wav
// cannot be played" card, the blank window's). It reads monotonic_ms() — the one clock every software
// deadline in the product is stamped on — and nothing here schedules anything.
//
// THE HIT (architect 2026-08-29, superseding the design's tap-anywhere):
// THE X, AND ONLY THE X, DISMISSES A CARD ON THE POINTER — and since
// 2026-08-31 THAT X HAS A KEYBOARD TWIN, bare Esc at the tail of its own
// ranking, which dismisses the stack's OLDEST card. The succession is exact
// and the 2026-08-29 ruling is untouched: it says where a PRESS may land on a
// card (the X's box and nothing else, one rule for finger and mouse), and the
// key lands on no card at all. THE TWO DIFFER IN RANK, deliberately: the X's
// claim sits ABOVE EVERY VEIL because a card must be dismissable under any
// modal, while Esc sits UNDER all of them — every other Esc place is earlier
// in the dispatch, so the key reaches the stack only when nothing modal
// stands and no render is in flight (the EIGHT places are enumerated at
// on_key, input_handler.cpp; the arm itself is handle_plain_bare_keys').
// OLDEST, NOT NEWEST, in his own reasoning: clearing the top would let the
// bottom card stick around preferentially, so the key empties the stack from
// the back the way the clock does. It reads no class — a critical card is
// dismissed like any other, exactly as the X takes any class.
//
// The pointer's own rule, unchanged: on both backends, a press on the
// card's BODY is consumed whole — arms nothing, moves nothing, lands no
// playhead, reaches nothing underneath — and dismisses nothing. The router
// cannot fork on tap versus click (no origin bit rides a press;
// GuiInputState carries modifiers alone), so the rule is one for both hosts,
// and the X's box is the icon row's 32 px button box, already the product's
// glass target, which is why no finger-fattened body target exists. The
// claim ranks ABOVE EVERY VEIL (the prompt's, the player's, the picker's, the
// dialog editors') because a card is not a reach into the veiled surface: it
// is the message about the act the veil stands over, and it must be
// dismissable under any of them. The press is the act (content acts the
// moment its identity is certain); the release owes nothing.
//
// THE GEOMETRY A CARD IS HIT BY IS THE GEOMETRY IT WAS PAINTED WITH: the
// painter publishes each visible card's rect and X box into
// AppState::Notifications::painted, and the router, the cursor map and the
// hover walk read that publication and then ask the live stack whether the
// id still stands (published geometry may only SELECT; live state decides).
// The owners below are pure functions of the window and the scale — the
// damage ROOM and the ONE-LINE card height — and neither knows a sentence:
// a card's real height needs its line count, the line count needs a shaped
// run, and shaping needs the paint's own font, so the painter is the only
// place that can answer it and the room is what the damage owner uses.

#include "app_state.h"
#include "viewport.h"

#include <cstdint>
#include <string>

// -- SENTENCES MORE THAN ONE TRANSLATION UNIT RAISES -------------------------
//
// A card's words live at the site that raises them. These two do not, because
// SEVERAL sites raise each and they must not drift: a sentence spelled twice
// is two sentences the moment one of them is edited. Everything else stays a
// literal where it fires (a sentence with ONE producer has nothing to agree
// with), and a family whose several sites share ONE translation unit keeps its
// constant there (kKeysDuringDrag, kCheckpointPublishing; the two mode
// routers' catch-all tails were a third until their catch-alls went silent
// with the unbound-keys ruling).
//
// THE LOCK'S SENTENCE — the read-only tab, said by the three sites that KNOW
// THEIR ACT and so need no chord in it: the Settings dropdown's own opener
// (the menu's items never grey, so their commands owe the answer themselves),
// the render player's Load in place, and the `h` view's Ctrl+H, whose
// admission composes the subject with this same lock. THE KEYBOARD GATE IS
// NOT A READER (since 2026-08-30): it says "<chord> is not available on a
// read-only tab" through the speller instead, naming what was pressed, which
// is what a user who just pressed it is looking for. Its predicate is the
// complement of an allowlist and so drops an unbound chord with a bound
// authoring one, which is why that gate asks chord_is_bound first and answers
// the unbound half with silence. The full reasoning is at that gate
// (input_handler.cpp).
inline constexpr const char* kTabReadOnlyCard = "This tab is read-only";

// THE TARGET PREVIEW'S SENTENCE — a launch asked for in target view before
// the preview it would play exists. Its two sites are Space's own play edge
// (which reads GuiTargetRender::preview_ready) and the waveform scrub's
// (which reads is_updating): two predicates, one fact to the user, so one
// sentence.
inline constexpr const char* kTargetPreviewNotReadyCard =
    "Wait for the target preview to finish rendering";

// The card's width is its content's, clamped to [this, kNotificationMaxWidthPx
// below]. Authored px, scaled like every other length.
inline constexpr double kNotificationMinWidthPx = 240.0;

// THE CARD'S CEILING IS THE LAPTOP'S OWN WIDTH, AUTHORED AND SCALED (architect
// 2026-08-31): 640 authored px is what the retired `window / 3` gave on the
// 1920 px laptop, so this constant is that width made CANONICAL — the same
// card at every window and, through `scaled_px`, the same card in millimetres
// at every gui_scale. The window fraction was DEVICE PIXELS and so starved the
// tablet: at 225 % its 1440 px panel gave a 480 px card for text shaped half
// again as large, three words to a line. IT MAY NOW COVER BUTTONS ON THE
// TABLET and that is accepted in his own words — "it's okay if it covers up
// some buttons": a card is a sentence to read and it leaves on its own.
// A length, so it scales; the life beside it (kNotificationMs) is a duration
// and does not.
inline constexpr double kNotificationMaxWidthPx = 640.0;

// HOW MANY LINES A SENTENCE MAY TAKE (architect 2026-08-30, "a few"): a card
// whose sentence does not fit its room GROWS DOWNWARD to this many lines and
// no further — the last permitted line takes the WHOLE remainder as one run
// and clips at the right edge, so no word is ever silently dropped, only cut
// where the eye can see the cut. A COUNT, not a length: it does not scale.
inline constexpr int kNotificationMaxLines = 3;

// -- Geometry the painter, the damage owner and the hit share ---------------

// A ONE-LINE card's height, and the height every card's FIRST line occupies:
// the icon row's content height (the 32 px button box plus its 7 px margins,
// one source) — the glyph and the X sit in that box at the row's own inset,
// AT THE FIRST LINE'S HEIGHT WHATEVER THE LINE COUNT (architect 2026-08-30:
// the text grows downward under the two icons, nothing reflows beside them).
// A card of `lines` lines is this plus (lines - 1) line spacings, which only
// the painter can know — a line count needs a shaped run, and shaping needs
// the paint's own font — so no pure function of the window states a card's
// real height and none is offered here.
int notification_card_h_px();

// THE CARD'S ONE PAD (architect 2026-08-30): the padding around the glyph,
// the text and the X is ONE NUMBER, the box's own vertical margin — the
// centering the card's height already derives from the icon row (46 = 32 +
// 2 x 7). It is read for ALL SIX of the card's distances: left edge -> glyph
// box, glyph box -> text, text -> X box, X box -> right edge, top -> boxes,
// boxes -> bottom. Nothing is authored here: the number IS
// (notification_card_h_px() - the button box) / 2, so a retune of either
// moves all six together, and the painter reads no foreign constant (the
// icon row's lane pad and the folder overlay's icon-to-name gap both left it
// that day — the overlay's rows keep their gap, that being their surface).
// PARITY: where card_h - btn is odd the integer floor puts the extra pixel
// BELOW the boxes, exactly as the icon row's own centering does for its
// buttons — the same floor, not a second rule.
int notification_pad_px();

// The card's largest possible width at this window and scale: the scaled
// ceiling above, and — where the window cannot even hold that — the window
// itself less the stack's two side margins. THE WINDOW TERM IS A SAFETY AND
// NOT A DESIGN (architect 2026-08-31, retiring `window / 3`): the ceiling is
// what the design says a card is wide, and the clamp only keeps a card inside
// a window too narrow for it (the tablet's portrait panel, a contrived
// window). IT NEVER ANSWERS BELOW THE FLOOR: the painter clamps a card's
// content width into [floor, this] and THE ROOM BELOW IS THIS SAME NUMBER, so
// a bound under the floor would put painted pixels outside the rect that
// damages and publishes them. A window narrower than the floor itself
// therefore keeps the floor and lets a card overhang, exactly as the retired
// fraction did — the contrived window this file declines to cater for.
int notification_card_max_w_px(const AppState& a);

// THE STACK'S ROOM: the rect the stack may occupy — the maximum card width,
// right-aligned at kPanelPadPx from the window's right edge, from that same
// kPanelPadPx of air below row 1 (architect 2026-08-29: the two margins are
// one number; the right one was the icon row's 8 px pad for the cards' first
// day) DOWN TO THE SAME AIR ABOVE THE BOTTOM ROW'S LANE.
//
// IT IS THE ROOM AND NO LONGER A TIGHT BOUND (2026-08-30, with the wrap): it
// was "three cards of one line each", which a wrapped card outgrows, and no
// pure function of the window can know a card's line count — that needs a
// shaped run. So the answer is the whole space the stack has to grow into,
// and the painter CLIPS to it: a stack that outgrows the room paints on down
// and is cut at the room's foot, so no card ever paints over the bottom row,
// and the painter publishes its rects CLIPPED TO THE ROOM TOO, so nothing
// under that foot is ever claimed by a card. (A stack that tall is the
// contrived case messaging.md already declines to cater for.)
//
// Every change to the stack damages this rect
// (Viewport::invalidate_notification_stack): the painted cards lie inside it
// by construction, so it erases what stood and admits what comes without
// shaping a single glyph off the paint clock. A window with no room between
// row 1 and the bottom row answers a zero height and paints nothing — a
// window with no waveform at all, in the contrived class.
GuiRect notification_stack_bound(const AppState& a);

// HOW MANY CARDS THE ROOM HOLDS, and so what a push bumps past (architect
// 2026-08-30, the uncapped stack): the number of ONE-LINE cards that fit the
// room above, `floor((room + gap) / (card + gap))`, at least one.
//
// IT IS A COUNT OF ONE-LINE CARDS AND NOT A PIXEL BUDGET, deliberately: no
// pure function of the window can know a card's line count — that needs a
// shaped run, and the model layer this serves has no font to shape with (the
// same reason the damage owner takes the room rather than a tight bound). So
// the cap counts the cards the room WOULD hold at one line each; a stack of
// wrapped cards can pass the room's foot while inside this count, and the
// painter's clip is what answers that, exactly as it answers a stack of
// criticals that will not be bumped.
//
// A pure function of the window and the scale, like the room. At a 1080 px
// window and 100 % it is 20 (a 998 px room over 46 + 2); on the tablet's
// 1440 px panel at 225 % it is 11 (1256 over 104 + 4). Every window this
// product runs in holds more cards than the architect will ever stack.
int notification_capacity(const AppState& a);

// Whether `id` names a card that is IN THE LIVE STACK — which is the same as
// "on screen" since the queue retired (2026-08-30): a push makes a card
// visible at once and only an expiry, an X or a bump removes it. THE ONE LIVE
// TEST every act on a published hit asks, and the one dismiss() asks of its
// argument — the publication is a paint old, and a card can expire or be
// bumped between that paint and the press.
bool notification_visible(const AppState& a, uint64_t id);

// The card under (x, y), or 0. PUBLISHED GEOMETRY MAY ONLY SELECT, LIVE STATE
// DECIDES (the owner-tag doctrine, recorded at ModalDialogGeometry): the walk
// picks an id out of the painter's rects and then answers 0 unless
// notification_visible still holds for it, so a card that expired or was
// bumped between the paint and the event is hit by nothing,
// and a press on the stale rect falls through to whatever the NEXT paint will
// put there, which is what the user is about to see. A card under the OPEN
// DROPDOWN's box yields to it — the dropdown is the one pointer-owning
// surface that paints above the cards — so every reader agrees on the z-order
// in one place too.
//
// THE READERS ARE THE CARD'S OPACITY, re-greped at this declaration: the
// press claim (claim_notification_press), the cursor map
// (pointer_cursor_kind), the card hover walk (GuiNotifications::update_hover),
// the WHEEL's routing predicate (wheel_context, which swallows a detent over
// a card), the touch pan zone (touch_point_in_pan_zone) and the two hover
// walks a card can stand over — the roster's
// (recompute_redesign_button_hover) and the folder overlay band's
// (update_folder_overlay_hover), each answering "nothing under the pointer"
// so no surface beneath a card wears a face or promises a press.
uint64_t notification_card_at(const AppState& a, int x, int y);

// Whether (x, y) lies in the published X box of the card `id`.
bool notification_close_at(const AppState& a, uint64_t id, int x, int y);

// -- The operations ---------------------------------------------------------

struct GuiNotifications {
    AppState& app;
    Viewport& viewport;

    GuiNotifications(AppState& app_, Viewport& viewport_)
        : app(app_), viewport(viewport_) {}

    // THE ONE PUSH. A duplicate of a card in the stack (same class, same
    // text) removes that card and pushes it again at the top with a fresh
    // clock, in both classes alike, instead of stacking a second one or
    // re-arming it where it stands (the argument is at the site).
    // A card goes on TOP and is visible at once, its clock started here;
    // then THE BUMP brings the stack back inside notification_capacity by
    // removing the oldest NORMAL card, never a critical one and never the
    // card just pushed (the full argument is at the site).
    void notify(AppState::NotificationClass cls, std::string text);

    // THE X's ACT, AND BARE ESC's ON THE OLDEST CARD (2026-08-31): remove the
    // card whatever its class and state, and drop the hover if it was this
    // card's. The two roads differ only in how they name the card — the X by
    // the published rect under the pointer, Esc by the stack's back — and the
    // live test below is asked of the argument either way.
    void dismiss(uint64_t id);

    // THE CLOCK, on the run loop's deadline tick: retire every normal card
    // whose life has elapsed and is not paused, and re-derive the hover from
    // the remembered pointer (a card that slid up under a motionless pointer
    // pauses from this tick on).
    void fire_if_due();

    // The hover walk, from the motion handler and the tick: which card the
    // pointer rests on and whether it is inside the X box. Entering a visible
    // normal card banks its remaining life; leaving re-arms it. A card whose
    // deadline has ALREADY passed is not banked — hover pauses a clock, it
    // does not resurrect one — so it retires on the next fire_if_due as an
    // unhovered one would.
    void update_hover(int x, int y);
    // The pointer-left hook's half: no card is hovered, every bank re-armed.
    void clear_hover();

private:
    // (start_visible_clocks and is_visible_index went with the QUEUE on
    // 2026-08-30: no card waits unseen any more, so the only clock a push
    // starts is its own card's, written where that card is built, and no
    // index is "visible" or not.)
    void set_hover(uint64_t id, bool close);
    AppState::Notification* find(uint64_t id);
};
