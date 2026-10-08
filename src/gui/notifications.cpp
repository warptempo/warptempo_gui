#include "notifications.h"

#include "folder_overlay.h"   // kPanelPadPx, the panel's air (one constant read)
#include "paint_handler.h"    // the lane table and relief_line_px

#include <algorithm>
#include <utility>

int notification_glyph_px() {
    return scaled_px(kNotificationGlyphPx);
}

int notification_glyph_box_px() {
    return scaled_px(kNotificationGlyphLeadPx) + notification_glyph_px() +
           scaled_px(kNotificationGlyphLeadPx);
}

int notification_card_h_px() {
    return scaled_px(kNotificationAirPx) + notification_glyph_box_px() +
           scaled_px(kNotificationAirPx);
}

int notification_pad_px() {
    // THE BOX'S OWN VERTICAL MARGIN, and so the card's every pad (the ruling
    // and the five distances at the declaration): the card's air around the
    // glyph's 22-px box in its 28-px line, three Windows px a side. An ODD
    // difference floors, putting the extra pixel below the box.
    return (notification_card_h_px() - notification_glyph_box_px()) / 2;
}

int notification_card_max_w_px(const AppState& a) {
    // THE CEILING IS AUTHORED AND SCALED, NOT A FRACTION OF THE WINDOW
    // (architect 2026-08-31, the reasoning and the number at the declaration,
    // kNotificationMaxWidthPx in notifications.h): the ceiling is what
    // `a.width / 3` gave on the 1920 px laptop, so the card the laptop already
    // had is now the card every device gets, in millimetres rather than in
    // device pixels.
    const int floor_w   = scaled_px(kNotificationMinWidthPx);
    const int ceiling_w = scaled_px(kNotificationMaxWidthPx);
    // THE WINDOW IS A SAFETY BELOW THE CEILING AND NOTHING ABOVE THE FLOOR:
    // the stack sits inside the panel's two margins, so a window that cannot
    // hold the ceiling gets what it has; a window that cannot hold the FLOOR
    // keeps the floor and lets the card overhang, because this number is also
    // the ROOM the painter clips and the damage owner erases (a bound under
    // the floor would strand pixels outside it).
    const int room_w = a.width - 2 * folder_overlay::pad_px();
    return std::max(floor_w, std::min(ceiling_w, room_w));
}

GuiRect notification_stack_bound(const AppState& a) {
    // THE STACK'S MARGINS: THE SCROLL BAR'S TOP ABOVE, kPanelPadPx AT THE RIGHT
    // AND THE FOOT. The right and the foot are the panel's own kPanelPadPx
    // (architect 2026-08-29, the foot joining 2026-08-30; the right margin
    // was the icon row's 8 px pad for the cards' first day, which read wider
    // than the air above them). THE TOP IS ONE RULE FOR EVERY VOCABULARY,
    // READ OFF THE LIVE LANE TABLE (architect 2026-10-08: "it looks like
    // absolute positioning where it should be relative to the elements";
    // the card "starting at the scroll bar"): THE TRIM LANE'S FIRST ROW LESS
    // ONE RELIEF LINE, so the card's one-line frame (relief_line_px, the
    // INFO face's, paint_popup_chrome) ends exactly where the scroll bar
    // begins and the card's ground starts on the bar's own first row. It
    // is anchored on the trim lane and not on the icon row, so whatever
    // the icon row puts below its cases — win2000's 4-W foot of ground,
    // clearlooks' toolbar shadow line (its foot 0, the trim lane flush
    // under it, so the frame lies on that line), gap 1 should it ever open
    // — the card meets the bar the same way under every chrome and at every
    // scale. No chrome spec field: no vocabulary needs another anchor (the
    // note among ChromeSpec's fields). The frame never reaches
    // the etched pair above the band (architect's glass 2026-10-06: a
    // card's frame laid over the pair's Hilight line reads as a fault).
    // The card's INTERNAL pad is a different number and a different concept —
    // notification_pad_px(), the chrome inside the box rather than the box's
    // placement.
    //
    // THE ANSWER IS THE ROOM, NOT A TIGHT BOUND (the whole reasoning at the
    // declaration): a wrapped card is taller than a line and no window
    // arithmetic can say how much taller, so this is the space the stack has
    // to grow into and the painter clips to it.
    const int pad     = folder_overlay::pad_px();
    const int w       = notification_card_max_w_px(a);
    const int x       = a.width - pad - w;
    const int y       = top_trim_row_area(a).y - relief_line_px();
    const int floor_y = bottom_row_area(a).y - pad;
    return GuiRect{x, y, w, std::max(0, floor_y - y)};
}

int notification_capacity(const AppState& a) {
    // ONE-LINE CARDS IN THE ROOM (the reasoning and the two measured numbers
    // at the declaration): n cards take n heights and n-1 gaps, so the room
    // holds floor((room + gap) / (card + gap)). Floored at one — a window too
    // short for a single card still shows the one it was given, clipped.
    const int card_h = notification_card_h_px();
    const int gap    = scaled_px(kNotificationGapPx, 1);
    const int n      = (notification_stack_bound(a).h + gap) / (card_h + gap);
    return n < 1 ? 1 : n;
}

bool notification_visible(const AppState& a, uint64_t id) {
    // IN THE STACK IS ON SCREEN since the queue retired (2026-08-30): the
    // second clause this carried — "and among the first kNotificationVisibleMax"
    // — had the queue as its whole producer, and there is no queue.
    if (id == 0) return false;
    for (const AppState::Notification& n : a.notifications.cards) {
        if (n.id == id) return true;
    }
    return false;
}

uint64_t notification_card_at(const AppState& a, int x, int y) {
    // A CARD UNDER A LIST POPUP OR THE COLOR PICKER'S CARD IS HIDDEN THERE
    // (floater_above_cards_at, app_state.h: the menu row's drop-down, the
    // settings choice editor's list, the color picker's element list, its
    // preset menu and its card — each painting above the cards), so the
    // press, the hover and the cursor reach the surface on top.
    if (floater_above_cards_at(a, x, y)) return 0;
    for (const AppState::NotificationPainted& p : a.notifications.painted) {
        if (!rect_contains(p.rect, x, y)) continue;
        // THE PUBLICATION SELECTS, THE LIVE STACK DECIDES: the rects were
        // painted, the card may since have expired or been BUMPED by a push,
        // and a stale rect must neither dismiss it nor consume the press
        // over it. The published rects do not overlap, so
        // a hit that fails this test is the whole answer — nothing beneath
        // it in the publication can be the card the user sees.
        return notification_visible(a, p.id) ? p.id : 0;
    }
    return 0;
}

// -- GuiNotifications ---------------------------------------------------------

AppState::Notification* GuiNotifications::find(uint64_t id) {
    for (AppState::Notification& n : app.notifications.cards) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

void GuiNotifications::notify(AppState::NotificationClass cls,
                              std::string text) {
    std::vector<AppState::Notification>& cards = app.notifications.cards;
    const int64_t now = monotonic_ms();
    // DELIBERATE PRESSES STACK THEIR DUPLICATES (architect 2026-09-01,
    // superseding the unconditional dedup of 2026-08-30): a PHYSICAL press
    // pushes its own card whatever stands, so hitting a wall three times
    // shows three cards — "I am very deliberate with my presses", and the
    // count IS the confirmation that each press was seen.
    //
    // THE ONE CARVE-OUT IS A HELD INPUT'S SYNTHESIZED REPEATS, which coalesce
    // exactly as everything used to: a burst at the compositor's cadence is
    // ONE gesture, not thirty presses, and letting it stack would empty the
    // room of every other card in half a second. The bit is the key event's,
    // set at on_key's head and dispatch-scoped
    // (AppState::Notifications::held_repeat_dispatch) — it covers the held
    // BUTTON as well as the held KEY, both of that bit's producers dispatching
    // through that one body, and it reads false on every other road into this
    // function (a worker's verdict, a pointer gesture), which is the
    // deliberate-press answer for them too.
    //
    // WHY THE REPEAT MOVES ITS CARD, and does not re-arm it in place: "in the
    // stack" and "on screen" are not the same thing. A stack that outgrows the
    // room paints on past its foot and the painter clips there, so a card can
    // be live and yet wholly invisible — and a re-arm in place would then
    // answer the act the user is this moment performing with nothing visible
    // at all. The top is the one place the answer is certain to be seen, and
    // it is also what a repeated event means to the reader: the top says the
    // last time it happened. ONE RULE FOR BOTH CLASSES — a critical duplicate
    // moves to the top too.
    if (app.notifications.held_repeat_dispatch) {
        for (size_t i = 0; i < cards.size(); ++i) {
            if (cards[i].cls != cls || cards[i].text != text) continue;
            if (app.notifications.hovered_id == cards[i].id) set_hover(0);
            cards.erase(cards.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    AppState::Notification card;
    card.id   = app.notifications.next_id++;
    card.cls  = cls;
    card.text = std::move(text);
    // THE CLOCK STARTS AT THE PUSH, here and nowhere else (2026-08-30, with
    // the queue's retirement): a card is on screen from this instant, so
    // there is no later surfacing for a clock to wait for. A critical card
    // keeps expiry_ms 0 — it has no clock at all.
    if (cls == AppState::NotificationClass::Normal)
        card.expiry_ms = now + kNotificationMs;
    cards.insert(cards.begin(), std::move(card));

    // THE BUMP (architect 2026-08-30): "cards bump each other off the screen
    // — newest on top, the oldest leaving; critical cards keep standing". The
    // stack is brought back inside the ROOM'S CAPACITY by removing the OLDEST
    // NORMAL card, walked from the back.
    //
    // TWO CARDS ARE NEVER THE VICTIM, and each for its own reason:
    //   - a CRITICAL one, by the ruling: it is never TIMED out and never
    //     BUMPED whatever the count, so the walk skips it and a stack of
    //     criticals alone simply keeps growing (what does take one down is a
    //     DELIBERATE dismissal — the lift of a press on it, or the whole
    //     stack's dismissal by bare Esc or a shifted or held lift — and
    //     none of those is this walk);
    //   - THE CARD JUST PUSHED, at index 0: it is the answer to the act the
    //     user has this moment performed, and bumping it would make that act
    //     silent — which is reachable, not theoretical (the capacity is 4 at
    //     a 1080 px window and 350 %, and a run of critical checkpoint
    //     failures fills it). So the walk stops above index 0.
    // THE MULTIPLES RULE MADE THIS LOAD-BEARING (2026-09-01): repeated presses
    // of one refusal now fill the room with copies of one sentence instead of
    // refreshing a single card, so the walk runs on ordinary use and not only
    // in a burst of different events. It needs nothing new for that — the
    // victims are still the oldest normal cards and the count is still the
    // room's — and what the user sees at the wall is the stack topping out at
    // the capacity with the newest press on top, which is the picture the
    // count is for.
    //
    // When neither leaves a victim the loop simply stops and THE OVERFLOW
    // PAINTS ON PAST THE ROOM, clipped at its foot by the painter. That clip
    // is also the answer to the other overflow the count cannot see: the
    // capacity counts ONE-LINE cards, so a burst of wrapped ones can pass the
    // room's foot while sitting inside the count. One rule, not two.
    const size_t cap = static_cast<size_t>(notification_capacity(app));
    while (cards.size() > cap) {
        size_t victim = 0;
        for (size_t i = cards.size(); i-- > 1;) {
            if (cards[i].cls == AppState::NotificationClass::Normal) {
                victim = i;
                break;
            }
        }
        if (victim == 0) break;
        // A HELD victim is bumped like any other (the hold pauses the clock;
        // it is not a victimhood exemption): the arm keeps naming a card
        // that is gone, and its lift asks the live stack and lands nothing.
        if (app.notifications.hovered_id == cards[victim].id) set_hover(0);
        cards.erase(cards.begin() + static_cast<std::ptrdiff_t>(victim));
    }
    viewport.invalidate_notification_stack();
}

void GuiNotifications::dismiss(uint64_t id) {
    // A CARD THAT HAS ALREADY GONE IS NOT DISMISSABLE — the same live test
    // the hit asks (notification_visible), asked here of the act's own
    // argument. The rect that named a card was painted a frame ago; the card
    // may have been bumped since (it cannot expire under a press that banked
    // its clock, but the lift's re-hit reads a paint-old publication too),
    // and those pixels belong to whatever took its place, so the act must
    // not reach past the screen.
    if (!notification_visible(app, id)) return;
    std::vector<AppState::Notification>& cards = app.notifications.cards;
    // The test above already found the card; this walk is here for the
    // erase's iterator and cannot come back empty.
    auto it = std::find_if(cards.begin(), cards.end(),
                           [id](const AppState::Notification& n) {
                               return n.id == id;
                           });
    if (it == cards.end()) return;
    if (app.notifications.hovered_id == id) set_hover(0);
    cards.erase(it);
    viewport.invalidate_notification_stack();
}

void GuiNotifications::dismiss_all() {
    // BARE ESC (architect 2026-09-01, "Esc should clear all notifications")
    // and, since 2026-10-01, THE SHIFTED OR HELD LIFT on any card ("shift
    // click or long press dismisses all"): every card dismissed at once,
    // CRITICALS INCLUDED. The clock never reaches a critical card and the
    // bump skips them, and that is deliberate: a deliberate press is a
    // decision, where a timeout would be an accident. THE HOVER
    // GOES WITH THEM, because the card it named is gone and a banked life on a
    // vanished card would be a leak. The arm took the OLDEST card alone from
    // 2026-08-31, and Ctrl+Esc carried this bulk act for one morning before
    // the key absorbed it and the chord retired.
    //
    // AN EMPTY STACK DAMAGES NOTHING AND SAYS NOTHING: the state asked for is
    // already true and visibly so (the stack is what the user is looking at),
    // which is the already-at-state silence the strictness ruling leaves
    // standing, and it is the arm's own silence at its dispatch site too.
    if (app.notifications.cards.empty()) return;
    if (app.notifications.hovered_id != 0) set_hover(0);
    app.notifications.cards.clear();
    viewport.invalidate_notification_stack();
}

void GuiNotifications::fire_if_due() {
    std::vector<AppState::Notification>& cards = app.notifications.cards;
    // One size test on an idle tick; the clock is read only past it.
    if (!cards.empty()) {
        const int64_t now = monotonic_ms();
        bool changed = false;
        for (size_t i = 0; i < cards.size();) {
            const AppState::Notification& n = cards[i];
            const bool due =
                n.cls == AppState::NotificationClass::Normal && !n.paused &&
                n.expiry_ms != 0 && n.expiry_ms <= now;
            if (!due) { ++i; continue; }
            if (app.notifications.hovered_id == n.id) set_hover(0);
            cards.erase(cards.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
        }
        if (changed) viewport.invalidate_notification_stack();
    }
    // THE HOVER IS RE-ANSWERED FROM THE REMEMBERED POINTER on every tick, so
    // a card that slid up under a motionless pointer starts pausing without
    // waiting for a motion; it reads the last paint's
    // publication, exactly as the motion handler does. Glass never sets the
    // in-window bit at rest (the touch translation's end delivers the leave),
    // so no finger RESTS on a card: a finger or the pen holds a card's clock
    // only while its contact presses that card, the bank's other reason
    // (rebank), and the contact's lift ends both.
    if (app.pointer_in_window) update_hover(app.last_mouse_x, app.last_mouse_y);
    else                       clear_hover();
}

void GuiNotifications::set_hover(uint64_t id) {
    AppState::Notifications& st = app.notifications;
    if (st.hovered_id == id) return;
    // THE HOVER IS THE BANK'S FIRST REASON AND PAINTS NOTHING (the card
    // wears no hover face since its X retired, 2026-10-01), so an edge owes
    // no damage: it moves the reason and re-answers the two cards it touched,
    // the one LEFT and the one ENTERED, each through the one bank writer.
    const uint64_t old = st.hovered_id;
    st.hovered_id = id;
    const int64_t now = monotonic_ms();
    rebank(old, now);
    rebank(id, now);
}

void GuiNotifications::rebank(uint64_t id, int64_t now) {
    AppState::Notification* n = find(id);
    if (n == nullptr) return;
    // THE TWO REASONS (architect 2026-10-01: "holding turns off the timer,
    // releasing re-enables"): the pointer resting on the card, and a press
    // holding it — AppState::chrome_press's Card arm, which is the hold's one
    // record, read here rather than mirrored into a second field. ONE BANK
    // FOR BOTH: the card stands still while either holds and its clock
    // resumes when neither does, so a press on a hovered card (the mouse, and
    // the touch translation, whose entry motion hovers the card before its
    // press) banks nothing twice, and a slide-away off a held card leaves the
    // bank where the hold put it until the lift.
    const AppState::ChromePress& arm = app.chrome_press;
    const bool held =
        arm.kind == AppState::ChromePress::Kind::Card && arm.card_id == id;
    const bool stands = app.notifications.hovered_id == id || held;
    if (!stands) {
        // THE LAST REASON ENDED: a banked life is re-armed from now.
        if (n->paused) {
            n->paused       = false;
            n->expiry_ms    = now + n->remaining_ms;
            n->remaining_ms = 0;
        }
        return;
    }
    // A REASON BEGAN: a normal card with a running clock banks what is left
    // of its life. A critical one has no clock to bank, a card already banked
    // stays banked, and a card a reason can name is on screen by construction
    // (the hover reads the painter's own publication, the press claim the
    // same).
    //
    // A CARD ALREADY DUE IS NOT BANKED. The pointer — or the press — can
    // arrive after the deadline has passed and before the tick that retires
    // it (the deadlines are polled, not scheduled), and banking a life of
    // zero would pause a card that has already earned its exit and hold it
    // there for as long as the reason stood. Left running, it leaves on the
    // next fire_if_due exactly as an unheld one would: A PAUSE STOPS A CLOCK,
    // IT DOES NOT RESURRECT ONE.
    if (n->cls != AppState::NotificationClass::Normal || n->paused ||
        n->expiry_ms == 0)
        return;
    const int64_t left = n->expiry_ms - now;
    if (left > 0) {
        n->paused       = true;
        n->remaining_ms = left;
        n->expiry_ms    = 0;
    }
}

void GuiNotifications::press_hold_edge(uint64_t id) {
    rebank(id, monotonic_ms());
}

void GuiNotifications::update_hover(int x, int y) {
    // The hit owner has already asked the live stack (a card that has left it
    // — by its expiry, by a dismissal or by the bump — answers 0 there, while
    // a live card is selected wherever it has a published rect), so this walk
    // owes nothing more.
    set_hover(notification_card_at(app, x, y));
}

void GuiNotifications::clear_hover() {
    set_hover(0);
}
