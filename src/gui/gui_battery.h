#pragma once
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

// THE HOST'S BATTERY ACROSS THE SEAM, AND THE MENU ROW'S LEGEND COMPOSED FROM
// IT (architect 2026-10-01). The type is the seam's — declared here so the two
// GuiPlatform headers and the GUI share ONE spelling, gui_media.h's shape — and
// it is a plain value: GuiPlatform::battery_status fills it from what the host
// knows (the contracts are at the two declarations), and the GUI composes the
// legend from it and the wall clock (refresh_menu_legend, main.cpp: seeded
// once before a session's first paint, then refreshed by the tick;
// paint_menu_row paints the composed text).

struct GuiBattery {
    // WHETHER PLUGGED IN — the legend's GLYPH, and THERE IS ALWAYS ONE
    // (architect 2026-10-01, the legend's owner below): Plugged covers
    // charging, full and "not charging" while on power alike; Unknown is no
    // status at all, or one the host cannot read.
    enum class Plugged : uint8_t { Plugged, Unplugged, Unknown };

    // FALSE ON A HOST WITH NO BATTERY: the legend is the clock alone.
    bool    has_battery = false;
    // 0..100, or -1 when the host cannot read the level.
    int     percent     = -1;
    Plugged plugged     = Plugged::Unknown;

    bool operator==(const GuiBattery&) const = default;
};

// THE LEGEND'S TEXT — "92% ↓ | 2:12 PM" — THE ONE COMPOSER (architect
// 2026-10-01). A LABEL, not a button: the menu row paints it flush right and
// nothing hits it, hovers it or tooltips it (paint_menu_row).
//
//   * THE BATTERY: the percentage and "%", a space, then THE GLYPH, which
//     says whether the device is PLUGGED IN and is always there: U+2191 ↑
//     plugged, U+2193 ↓ unplugged, U+00D7 × unknown — "a sign that tells me
//     there's something wrong". The glyphs are LIBERATION SANS'S at every
//     scale (gui_font.h, architect 2026-10-05): Cronyx carries none of the
//     three, so in the bitmap mode they are the outline drawn at its own
//     advance beside the strike's digits — no drawn glyph. The two arrows
//     share one advance, as he asked (architect 2026-10-02: "let's stick
//     with the arrow from the font"). An unreadable level drops the
//     percentage and keeps the glyph.
//   * " | " between the battery and the clock — and on a host with NO
//     battery the clock alone, no pipe.
//   * THE CLOCK: the wall clock in 12-HOUR LOCAL time with AM / PM and no
//     leading zero on the hour ("2:12 PM", "12:05 AM"), from the C library on
//     both hosts (localtime_r). The AM / PM words are spelled here rather than
//     taken from strftime's %p: the product's text is its own, and %p is the
//     locale's word.
//
// WI-FI IS NOT PART OF THIS (architect 2026-10-01: "maybe later").
inline std::string compose_menu_legend(const GuiBattery& battery,
                                       std::time_t now) {
    std::tm local{};
    localtime_r(&now, &local);
    int hour = local.tm_hour % 12;
    if (hour == 0) hour = 12;
    char clock[16];
    std::snprintf(clock, sizeof(clock), "%d:%02d %s", hour, local.tm_min,
                  local.tm_hour < 12 ? "AM" : "PM");
    if (!battery.has_battery) return clock;

    std::string s;
    if (battery.percent >= 0) {
        s += std::to_string(battery.percent);
        s += "% ";
    }
    switch (battery.plugged) {
        case GuiBattery::Plugged::Plugged:   s += "\xE2\x86\x91"; break;  // ↑
        case GuiBattery::Plugged::Unplugged: s += "\xE2\x86\x93"; break;  // ↓
        case GuiBattery::Plugged::Unknown:   s += "\xC3\x97";     break;  // ×
    }
    s += " | ";
    s += clock;
    return s;
}
