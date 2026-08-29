#include "notifications.h"

#include "folder_overlay.h"   // kPanelPadPx, the panel's air (one constant read)
#include "paint_handler.h"    // icon_row_pad_x

#include <algorithm>
#include <utility>

int notification_card_h_px() {
    return icon_row_content_h_px();
}

int notification_card_max_w_px(const AppState& a) {
    const int floor_w = scaled_px(kNotificationMinWidthPx);
    return std::max(floor_w, a.width / 3);
}

GuiRect notification_stack_bound(const AppState& a) {
    const GuiRect menu = top_menu_row_area(a);
    const int pad_x = icon_row_pad_x();
    const int w     = notification_card_max_w_px(a);
    const int h     = notification_card_h_px();
    const int gap   = scaled_px(kIconBtnGapPx, 1);
    const int x     = a.width - pad_x - w;
    const int y     = menu.y + menu.h + folder_overlay::pad_px();
    return GuiRect{x, y,
                   w, kNotificationVisibleMax * h +
                          (kNotificationVisibleMax - 1) * gap};
}

uint64_t notification_card_at(const AppState& a, int x, int y) {
    if (a.dropdown.open() && rect_contains(a.dropdown.rect, x, y)) return 0;
    for (const AppState::NotificationPainted& p : a.notifications.painted) {
        if (rect_contains(p.rect, x, y)) return p.id;
    }
    return 0;
}

bool notification_close_at(const AppState& a, uint64_t id, int x, int y) {
    for (const AppState::NotificationPainted& p : a.notifications.painted) {
        if (p.id == id) return rect_contains(p.close, x, y);
    }
    return false;
}

// -- GuiNotifications ---------------------------------------------------------

AppState::Notification* GuiNotifications::find(uint64_t id) {
    for (AppState::Notification& n : app.notifications.cards) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

bool GuiNotifications::is_visible_index(size_t index) const {
    return index < static_cast<size_t>(kNotificationVisibleMax);
}

void GuiNotifications::start_visible_clocks(int64_t now_ms) {
    std::vector<AppState::Notification>& cards = app.notifications.cards;
    for (size_t i = 0; i < cards.size() && is_visible_index(i); ++i) {
        AppState::Notification& n = cards[i];
        if (n.cls != AppState::NotificationClass::Normal) continue;
        if (n.paused || n.expiry_ms != 0) continue;
        n.expiry_ms = now_ms + kNotificationMs;
    }
}

void GuiNotifications::notify(AppState::NotificationClass cls,
                              std::string text) {
    std::vector<AppState::Notification>& cards = app.notifications.cards;
    const int64_t now = monotonic_ms();
    // THE DEDUP: the same sentence already on screen in the same class is
    // that card again, not a second one. A normal duplicate takes a fresh
    // full life — banked if the pointer is resting on it, armed otherwise —
    // and a critical duplicate changes nothing.
    for (size_t i = 0; i < cards.size() && is_visible_index(i); ++i) {
        AppState::Notification& n = cards[i];
        if (n.cls != cls || n.text != text) continue;
        if (cls == AppState::NotificationClass::Normal) {
            if (n.paused) n.remaining_ms = kNotificationMs;
            else          n.expiry_ms    = now + kNotificationMs;
        }
        return;
    }
    // A push demotes whatever stood at the visible set's foot into the
    // queue, and a queued card's clock has not started: a demoted normal
    // card that was running or paused forgets its clock and takes a full
    // life when it surfaces again (the bank is the pointer's, and the
    // pointer is not on a queued card).
    if (cards.size() >= static_cast<size_t>(kNotificationVisibleMax)) {
        AppState::Notification& demoted =
            cards[static_cast<size_t>(kNotificationVisibleMax) - 1];
        demoted.expiry_ms    = 0;
        demoted.paused       = false;
        demoted.remaining_ms = 0;
        if (app.notifications.hovered_id == demoted.id) set_hover(0, false);
    }
    AppState::Notification card;
    card.id   = app.notifications.next_id++;
    card.cls  = cls;
    card.text = std::move(text);
    cards.insert(cards.begin(), std::move(card));
    start_visible_clocks(now);
    viewport.invalidate_notification_stack();
}

void GuiNotifications::dismiss(uint64_t id) {
    std::vector<AppState::Notification>& cards = app.notifications.cards;
    auto it = std::find_if(cards.begin(), cards.end(),
                           [id](const AppState::Notification& n) {
                               return n.id == id;
                           });
    if (it == cards.end()) return;
    if (app.notifications.hovered_id == id) set_hover(0, false);
    cards.erase(it);
    start_visible_clocks(monotonic_ms());
    viewport.invalidate_notification_stack();
}

void GuiNotifications::fire_if_due() {
    std::vector<AppState::Notification>& cards = app.notifications.cards;
    // One size test on an idle tick; the clock is read only past it.
    if (!cards.empty()) {
        const int64_t now = monotonic_ms();
        bool changed = false;
        for (size_t i = 0; i < cards.size() && is_visible_index(i);) {
            const AppState::Notification& n = cards[i];
            const bool due =
                n.cls == AppState::NotificationClass::Normal && !n.paused &&
                n.expiry_ms != 0 && n.expiry_ms <= now;
            if (!due) { ++i; continue; }
            if (app.notifications.hovered_id == n.id) set_hover(0, false);
            cards.erase(cards.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
        }
        if (changed) {
            start_visible_clocks(now);
            viewport.invalidate_notification_stack();
        }
    }
    // THE HOVER IS RE-ANSWERED FROM THE REMEMBERED POINTER on every tick, so
    // a card that surfaced or slid up under a motionless pointer starts
    // pausing without waiting for a motion; it reads the last paint's
    // publication, exactly as the motion handler does. Glass never sets the
    // in-window bit at rest (the touch translation's end delivers the leave),
    // so no finger ever holds a clock.
    if (app.pointer_in_window) update_hover(app.last_mouse_x, app.last_mouse_y);
    else                       clear_hover();
}

void GuiNotifications::set_hover(uint64_t id, bool close) {
    AppState::Notifications& st = app.notifications;
    if (st.hovered_id == id && st.close_hovered == close) return;
    const int64_t now = monotonic_ms();
    // LEAVE the old card: a banked life is re-armed from now.
    if (st.hovered_id != id) {
        if (AppState::Notification* old = find(st.hovered_id)) {
            if (old->paused) {
                old->paused    = false;
                old->expiry_ms = now + old->remaining_ms;
                old->remaining_ms = 0;
            }
        }
        // ENTER the new one: a visible normal card with a running clock
        // banks what is left of its life. A queued card cannot be hovered
        // (it is not painted) and a critical one has no clock to bank.
        if (AppState::Notification* n = find(id)) {
            if (n->cls == AppState::NotificationClass::Normal &&
                !n->paused && n->expiry_ms != 0) {
                n->paused       = true;
                n->remaining_ms = std::max<int64_t>(0, n->expiry_ms - now);
                n->expiry_ms    = 0;
            }
        }
    }
    // THE X's FACE is the one thing hover paints; the card's body wears
    // none, so the damage is the two X boxes and nothing wider.
    for (const AppState::NotificationPainted& p : st.painted) {
        if (p.id == st.hovered_id || p.id == id) {
            viewport.invalidate_rect(p.close);
        }
    }
    st.hovered_id    = id;
    st.close_hovered = close;
}

void GuiNotifications::update_hover(int x, int y) {
    const uint64_t id = notification_card_at(app, x, y);
    // The publication may name a card that has since left; the live stack
    // decides, and a departed id hovers nothing.
    if (id != 0 && find(id) == nullptr) { set_hover(0, false); return; }
    set_hover(id, id != 0 && notification_close_at(app, id, x, y));
}

void GuiNotifications::clear_hover() {
    set_hover(0, false);
}
