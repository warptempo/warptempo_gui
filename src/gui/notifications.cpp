#include "notifications.h"

#include "folder_overlay.h"   // pad_px, the panel's air (one accessor read)
#include "paint_handler.h"    // the lane table
#include "warp_frame_map_view.h"   // warp_red_flag_set_cached, the strike
#include "warpmarkers.h"      // format_warpmarkers_text, the authored bytes

#include <algorithm>
#include <cmath>
#include <utility>

int notification_gap_px() {
    // notifyd's 6 W (the ruling at the declaration), floored at one px.
    return scaled_px(kNotificationGapW, 1);
}

int notification_line_px() {
    // The body face's line box in W, whole (the declaration): 14 W, 28
    // device px on the tablet.
    return scaled_px(gui_font_line_box_w(gui_font(GuiFace::Body)).line(), 1);
}

int notification_card_h_px() {
    // top pad + one line box + bottom pad, each its own rounded part (the
    // composite rule, scaled_px's head): 28 + 28 + 28 = 84 device rows on
    // the tablet at 200 % (DESIGN §9's 42 W).
    return scaled_px(kNotificationPadTopW, 1) + notification_line_px() +
           scaled_px(kNotificationPadBottomW, 1);
}

int notification_card_max_w_px(const AppState& a) {
    // THE CEILING IS AUTHORED AND SCALED, NOT A FRACTION OF THE WINDOW
    // (architect 2026-08-31, the reasoning and the number at the declaration,
    // kNotificationMaxWidthPx in notifications.h): the ceiling is what
    // `a.width / 3` gave on the 1920 px laptop, where the number was taken,
    // kept as an authored length rather than a fraction of the window.
    const int floor_w   = scaled_px(kNotificationMinWidthPx);
    const int ceiling_w = scaled_px(kNotificationMaxWidthPx);
    // THE WINDOW IS A SAFETY BELOW THE CEILING AND NOTHING ABOVE THE FLOOR:
    // the stack sits inside the panel's columns (well_columns, THE EDGE
    // RULE; notification_stack_bound), so a window that cannot hold the
    // ceiling gets what it has; a window that cannot hold the FLOOR
    // keeps the floor and lets the card overhang, because this number is also
    // the ROOM the painter clips to and publishes within (a bound under
    // the floor would strand pixels outside it).
    const WellColumns well = well_columns(a);
    const int room_w = well.end - well.x - 2 * scaled_px(kNotificationSeatW);
    return std::max(floor_w, std::min(ceiling_w, room_w));
}

GuiRect notification_stack_bound(const AppState& a) {
    // THE STACK'S MARGINS: NOTIFYD'S SEAT ABOVE AND AT THE RIGHT, THE PANEL'S
    // PAD AT THE FOOT (architect 2026-11-19, send Z, DESIGN §9: the first
    // card's top 16 W under the top icon row's line, its right edge 16 W
    // inside the panel — rows from 176 and columns to 2271 on the tablet at
    // key_height 28; flush with the glass on the trim trough's first row
    // until send AA's stage D1). THE TOP IS READ OFF THE LIVE LANE TABLE
    // (architect 2026-10-08: "it looks like absolute positioning where it
    // should be relative to the elements"): the trim lane's first row is the
    // top icon row's line (one W, 142..143 on the tablet), so the row under
    // it is the trim lane's top plus one hairline, and the seat
    // kNotificationSeatW under that — whatever stands between the band's
    // keys and the trim lane, the card meets the lanes the same way at every
    // scale. The foot is the panel's own pad, folder_overlay::pad_px
    // (architect 2026-08-30; one W). The card's INTERNAL pads are a
    // different concept — notifyd's, the box's own (notifications.h).
    //
    // THE ANSWER IS THE ROOM, NOT A TIGHT BOUND (the whole reasoning at the
    // declaration): a wrapped card is taller than a line and no window
    // arithmetic can say how much taller, so this is the space the stack has
    // to grow into and the painter clips to it.
    const int pad     = folder_overlay::pad_px();
    const int seat    = scaled_px(kNotificationSeatW);
    const int w       = notification_card_max_w_px(a);
    const int x       = well_columns(a).end - seat - w;
    const int y       = top_trim_row_area(a).y + hairline_px() + seat;
    const int floor_y = bottom_row_area(a).y - pad;
    return GuiRect{x, y, w, std::max(0, floor_y - y)};
}

int notification_capacity(const AppState& a) {
    // ONE-LINE CARDS IN THE ROOM (the reasoning and the two measured numbers
    // at the declaration): n cards take n heights and n-1 gaps, so the room
    // holds floor((room + gap) / (card + gap)). Floored at one — a window too
    // short for a single card still shows the one it was given, clipped.
    const int card_h = notification_card_h_px();
    const int gap    = notification_gap_px();
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
    // NOTHING THAT TAKES A PRESS PAINTS ABOVE THE CARDS since the Settings
    // drop-down retired (2026-10-21; it hid a card under its box until then),
    // so the published rects are the whole answer.
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
    // exactly as everything used to: a burst at the platform's repeat cadence is
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
    // resumes when neither does, so a press on a hovered card (the pen's
    // hover, and the touch translation, whose entry motion hovers the card
    // before its press) banks nothing twice, and a slide-away off a held card leaves the
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

void GuiNotifications::sync_struck_card(int sample_rate, long total_frames) {
    // THE ONE DERIVATION (notifications.h's THE STRUCK CARD, architect
    // 2026-11-19). THE STORE'S GENERATION IS THE TRIGGER: every write to the
    // warp markers bumps it (marker_store.h), so a tick whose generation has
    // not moved owes nothing — and a card standing then already says the
    // count this store holds, which only a write could change. (The card's
    // own dismissal moves no generation: tapped away, it stays away until
    // the store moves.)
    AppState::Notifications& st = app.notifications;
    const long long gen = app.warpmarkers.generation();
    if (gen == st.struck_seen_gen) return;
    const bool first_look = st.struck_seen_gen < 0;
    st.struck_seen_gen = gen;
    // AN EDIT IS A CHANGE OF THE AUTHORED MARKERS — their `.warpmarkers`
    // bytes, exactly what a save would write (format_warpmarkers_text) —
    // not every bump of the generation: the store's mutable accessors bump
    // it on any call (marker_store.h, "a spurious bump"), and a session's
    // iteration bracket rides the marker unsaved, so a bump that leaves the
    // file's bytes as they were is no edit and does not bring a tapped-away
    // card back. The piece's first look (the project's open) counts as one.
    std::string text = format_warpmarkers_text(app.warpmarkers.markers());
    const bool edited = first_look || text != st.struck_seen_text;
    st.struck_seen_text = std::move(text);
    const int count = static_cast<int>(
        warp_red_flag_set_cached(app, sample_rate, total_frames)
            .collapsed.size());
    AppState::Notification* card =
        st.struck_card_id != 0 ? find(st.struck_card_id) : nullptr;
    // CLEARED BY ITSELF: no warp marker coincides, so the state the card
    // said is no longer true (an undo, a delete, a move apart).
    if (count == 0) {
        if (card != nullptr) {
            const uint64_t id = card->id;
            if (st.hovered_id == id) set_hover(0);
            std::erase_if(st.cards, [id](const AppState::Notification& n) {
                return n.id == id;
            });
            viewport.invalidate_notification_stack();
        }
        st.struck_card_id = 0;
        return;
    }
    // THE WORDS (the head's, agreed 2026-11-19): a coincident run holds two
    // or more members, so the count is 2 or more and the plural holds.
    std::string words = std::to_string(count) + " warp markers coincide";
    if (card != nullptr) {
        // RE-WORDED IN PLACE: the same card, its seat in the stack kept. ITS
        // OWN PAINTED RECT IS DAMAGED (new words on an unmoved rect — the
        // width floor holds every short card at one width, so "2" and "3"
        // lay out alike — are a change the stack's rect compare cannot
        // see), and the stack's owner damages what a new width moved.
        if (card->text != words) {
            card->text = std::move(words);
            for (const AppState::NotificationPainted& p : st.painted)
                if (p.id == card->id) viewport.invalidate_rect(p.rect);
            viewport.invalidate_notification_stack();
        }
        return;
    }
    // TAPPED AWAY (or never raised): it stands again only after an edit (or
    // at the piece's first look) that leaves a marker struck.
    if (!edited) return;
    notify(AppState::NotificationClass::Critical, std::move(words));
    st.struck_card_id = st.cards.front().id;
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
