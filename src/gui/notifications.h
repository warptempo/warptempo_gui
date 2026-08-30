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
//              failed", "History is unavailable". Leaves on its own
//              kNotificationMs after it
//              became visible (gui_input.h; the pointer resting on it pauses
//              the clock) or at its X.
//   CRITICAL — the four checkpoint outcomes and nothing else today. Stands
//              until its X; no clock.
//
// WHAT IS NOT A CARD, by ruling: a render's completion ("that would get
// annoying"); every deliberately silent refusal (the consumed no-ops — the
// strict-modifier no-ops, the off-home drops, `m` on a bad run, and bare `h`
// with a checkpoint publishing); the loader's fatal exits (adversarial class:
// stderr and exit 1); every QUESTION (the prompts and the dialog editors); and
// what is TRUE NOW rather than what happened — the render's progress line and
// the `h` walk's line, the two STATE strings, which live in ROW 8'S STATE CELL
// right of the clock. (The player's LOAD UNDER A RUNNING RENDER was on this
// list for the one day a status bar stood at the window's foot to explain it,
// and is a card since the fold: the state cell is row 8's, whose lane the
// player's own modal row takes whole, so that refusal has nothing beside it.)
//
// ONE PUSH CHOKEPOINT: GuiNotifications::notify. Every producer above calls
// it and nothing else writes a card. A text identical to a card already on
// screen in the same class does not stack a duplicate — it re-arms that
// card's clock (a critical duplicate is a no-op).
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
// THE X, AND ONLY THE X, DISMISSES A CARD, on both backends. A press on the
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

// The card's width is its content's, clamped to [this, a third of the
// window]. Authored px, scaled like every other length. On a window narrower
// than three of these the floor wins — a contrived window, not catered for.
inline constexpr double kNotificationMinWidthPx = 240.0;

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

// The card's largest possible width at this window and scale (the clamp's
// upper bound, or the floor where the window is narrower than three floors).
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

    // THE ONE PUSH. A duplicate of a card on screen (same class, same text)
    // re-arms that card instead of stacking; a critical duplicate is a no-op.
    // A new card goes on TOP and is visible at once, its clock started here;
    // then THE BUMP brings the stack back inside notification_capacity by
    // removing the oldest NORMAL card, never a critical one and never the
    // card just pushed (the full argument is at the site).
    void notify(AppState::NotificationClass cls, std::string text);

    // THE X's act: remove the card whatever its class and state, and drop the
    // hover if it was this card's.
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
