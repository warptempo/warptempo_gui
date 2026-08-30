#pragma once

// THE NOTIFICATION CARDS (architect design 2026-08-29, docs/engineering/
// architecture/messaging.md) — the product's surface for EVENTS: something
// happened that answers an act, or that the user was not watching. A small
// dark card stacked top-right under row 1's view radios, newest on top, at
// most kNotificationVisibleMax visible, one line of the one sans, a Breeze
// glyph at the left naming the class, an X at the right. Two classes and
// nothing else — no remaining-time bar, no actions, no title/body split, no
// sound (the architect is "not a big fan of notifications": minimal):
//
//   NORMAL   — every refusal that has a sentence and every act's report:
//              the load-in-place refusals, "Only batch renders load in
//              place", "No renders to play", the render player's decode
//              refusals, the propagate pastes' "Stopped at …" reports, the
//              measure paste's, the picker's three refusals, Synchronize's
//              refusals and its count, "Target render failed", "History is
//              unavailable". Leaves on its own kNotificationMs after it
//              became visible (gui_input.h; the pointer resting on it pauses
//              the clock) or at its X.
//   CRITICAL — the four checkpoint outcomes and nothing else today. Stands
//              until its X; no clock.
//
// WHAT IS NOT A CARD, by ruling: a render's completion ("that would get
// annoying"); every deliberately silent refusal (the consumed no-ops — the
// strict-modifier no-ops, the off-home drops, `m` on a bad run, the player's
// load under a running render, whose explanation is the progress line
// itself); the loader's fatal exits (adversarial class: stderr and exit 1);
// every QUESTION (the prompts and the dialog editors); and what is TRUE NOW
// rather than what happened — the render's progress line, the `h` walk's
// line and the selected marker's readout, which are state and live on the
// STATUS BAR, the window's last row.
//
// ONE PUSH CHOKEPOINT: GuiNotifications::notify. Every producer above calls
// it and nothing else writes a card. A text identical to a VISIBLE card's of
// the same class does not stack a duplicate — it re-arms that card's clock (a
// critical duplicate is a no-op). A queued (fourth or later) card surfaces
// when a visible one leaves, and ITS CLOCK STARTS THEN, not at its push, so a
// burst of sentences is read one screenful at a time and none of them
// expires unseen.
//
// THE CLOCK RIDES THE RUN LOOP'S OWN DEADLINE TICK, polled: fire_if_due is
// called at the HEAD of main.cpp's on_tick, above every early return that
// body has — the startup load's, the render player's fork, the loading/blank
// guard and the playing-only guard — because a card is a message about
// something that has already happened, so nothing the tick does below can be
// a precondition for retiring one, while every one of those returns is a mode
// a card can stand over (the render player's own "No audio device" card, the
// blank window's). It reads monotonic_ms() — the one clock every software
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
// The owners below that need no text width — the damage bound, the card
// height — are pure functions of the window and the scale.

#include "app_state.h"
#include "viewport.h"

#include <cstdint>
#include <string>

// At most this many cards are visible; older ones wait in the queue. A stack
// that would reach the bottom strip is contrived and nothing clamps further
// ("we don't cater to that"); on the tablet's 1024 logical px it is cramped,
// as expected.
inline constexpr int kNotificationVisibleMax = 3;

// The card's width is its content's, clamped to [this, a third of the
// window]. Authored px, scaled like every other length. On a window narrower
// than three of these the floor wins — a contrived window, not catered for.
inline constexpr double kNotificationMinWidthPx = 240.0;

// -- Geometry the painter, the damage owner and the hit share ---------------

// A card's height: the icon row's content height (the 32 px button box plus
// its 7 px margins, one source) — the X and the glyph sit in that box at the
// row's own inset.
int notification_card_h_px();

// The card's largest possible width at this window and scale (the clamp's
// upper bound, or the floor where the window is narrower than three floors).
int notification_card_max_w_px(const AppState& a);

// THE STACK'S BOUND: the rect the visible stack can ever occupy — three
// cards of the maximum width, right-aligned at kPanelPadPx from the window's
// right edge, THE SAME kPanelPadPx of air below row 1 (architect 2026-08-29:
// the two margins are one number; the right one was the icon row's 8 px pad
// for the cards' first day). Every change to
// the stack damages this rect (Viewport::invalidate_notification_stack): the
// painted cards always lie inside it by construction, so it erases what
// stood and admits what comes without shaping a single glyph off the paint
// clock.
GuiRect notification_stack_bound(const AppState& a);

// Whether `id` names a card that is VISIBLE RIGHT NOW — present in the live
// stack and among its first kNotificationVisibleMax. THE ONE LIVE TEST every
// act on a published hit asks, and the one dismiss() asks of its argument.
bool notification_visible(const AppState& a, uint64_t id);

// The card under (x, y), or 0. PUBLISHED GEOMETRY MAY ONLY SELECT, LIVE STATE
// DECIDES (the owner-tag doctrine, recorded at ModalDialogGeometry): the walk
// picks an id out of the painter's rects and then answers 0 unless
// notification_visible still holds for it, so a card that expired or was
// pushed into the queue between the paint and the event is hit by nothing,
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

    // THE ONE PUSH. A duplicate of a visible card (same class, same text)
    // re-arms that card instead of stacking; a critical duplicate is a
    // no-op. A new card goes on TOP and is visible at once: the stack is
    // newest-first and the first kNotificationVisibleMax are visible, so a
    // push on a full stack demotes the third card into the queue, where its
    // clock forgets what had run and starts afresh when it surfaces again.
    void notify(AppState::NotificationClass cls, std::string text);

    // THE X's act: remove the card whatever its class and state, surface the
    // queue, drop the hover if it was this card's.
    void dismiss(uint64_t id);

    // THE CLOCK, on the run loop's deadline tick: retire every visible normal
    // card whose life has elapsed and is not paused, surface the queue, and
    // re-derive the hover from the remembered pointer (a card that slid up
    // under a motionless pointer pauses from this tick on).
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
    // A visible normal card whose clock has not started (a card that just
    // surfaced, or that was pushed straight into the visible set) takes its
    // full life from `now`.
    void start_visible_clocks(int64_t now_ms);
    void set_hover(uint64_t id, bool close);
    AppState::Notification* find(uint64_t id);
    bool is_visible_index(size_t index) const;
};
