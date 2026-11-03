#pragma once
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

// THE HOST'S BATTERY ACROSS THE SEAM, AND THE LOGO LANE'S LEGEND COMPOSED
// FROM IT AND THE WALL CLOCK (architect 2026-10-24; the legend's first design
// stood in the menu row 2026-10-01 to 2026-10-05). The type is the seam's —
// declared here so the GuiPlatform header and the GUI share ONE
// spelling, gui_media.h's shape — and it is a plain value:
// GuiPlatform::battery_status fills it from what the host knows (the
// contract is at its declaration), and the tick composes the legend
// from it and the wall clock (main.cpp's tick, refresh_logo_legend, the
// legend's one refresh site; render.h's logo lane block and
// paint_handler.cpp's paint_logo_lane paint it).

struct GuiBattery {
    // WHETHER A POWER SOURCE IS ATTACHED: Plugged covers charging, full and
    // "not charging" while on power alike (any source: AC, USB, wireless,
    // dock); Unknown is no status at all, or one the host cannot read.
    enum class Plugged : uint8_t { Plugged, Unplugged, Unknown };

    // FALSE WHEN THE HOST REPORTS NO BATTERY (the broadcast's EXTRA_PRESENT).
    bool    has_battery = false;
    // 0..100, or -1 when the host cannot read the level.
    int     percent     = -1;
    Plugged plugged     = Plugged::Unknown;

    bool operator==(const GuiBattery&) const = default;
};

// THE LEGEND'S GLYPH, ALWAYS ONE OF THE TWO (architect 2026-10-24: "I prefer
// not to use nothing … the glyph always there taking up space so that the
// clock doesn't move"): CHARGING while any power source is attached, 100 %
// INCLUDED ("the charging one on even at 100 %"), and on a host with no
// battery (power is attached); DISCHARGING while discharging — and while
// the plug state is UNKNOWN (the cold answer before the tablet's first
// broadcast, an unreadable sysfs node): the charging glyph claims power, so
// a state that cannot claim it takes the other. The drawings are Breeze's
// battery outline with and without its plug (Battery000Charging /
// Battery000, 2026-11-02; the drawn bolt and battery of 2026-10-24 retired),
// render.h's logo lane block.
enum class GuiLogoGlyph : uint8_t { Charging, Discharging };

// THE LEGEND — "92% <glyph> | 2:12 PM" — AS THREE COMPOSED PARTS, laid right
// to left by the painter (render.h's logo lane block): THE CLOCK RUN, the
// glyph, and the PERCENT RUN. THE PIPE between the glyph and the clock is
// NOT COMPOSED: it never changes, and it is the layout's own seated item
// (kLogoLegendPipe, render.h; architect 2026-11-03, on mock dec0b: every
// item of the legend is centered on the logo's band by its own ink, and the
// pipe's ink is not the clock's).
struct GuiLogoLegend {
    std::string  percent;   // "92%", or empty: no battery, or no level read
    GuiLogoGlyph glyph = GuiLogoGlyph::Charging;
    std::string  clock;     // "2:12 PM"
    bool operator==(const GuiLogoLegend&) const = default;
};

// THE ONE COMPOSER (architect 2026-10-24):
//   * THE PERCENT: the level and "%", NO LEADING ZERO ("100%" grows
//     leftward, away from the clock — the painter's right-to-left walk); an
//     unreadable level, or no battery, drops it and keeps the glyph.
//   * THE GLYPH: GuiLogoGlyph's rule above.
//   * THE CLOCK: the wall clock in 12-HOUR LOCAL time with AM / PM and NO
//     LEADING ZERO on the hour ("2:12 PM", "12:05 AM"), from the C library
//     (localtime_r). The AM / PM words are spelled here rather than taken
//     from strftime's %p: the product's text is its own, and %p is the
//     locale's word. ITS ALPHABET IS kLogoLegendInkSet's (render.h: the
//     digits, "%", ":", the space and "AMP", the percent's too), which seats
//     the runs, so a character composed here and missing there would ink
//     off the seat. The pipe before it is the layout's (the struct above).
// WI-FI IS NOT PART OF THIS (architect 2026-10-01: "maybe later").
inline GuiLogoLegend compose_logo_legend(const GuiBattery& battery,
                                         std::time_t now) {
    std::tm local{};
    localtime_r(&now, &local);
    int hour = local.tm_hour % 12;
    if (hour == 0) hour = 12;
    char clock[24];
    std::snprintf(clock, sizeof(clock), "%d:%02d %s", hour, local.tm_min,
                  local.tm_hour < 12 ? "AM" : "PM");

    GuiLogoLegend legend;
    legend.clock = clock;
    if (battery.has_battery && battery.percent >= 0)
        legend.percent = std::to_string(battery.percent) + "%";
    legend.glyph = (!battery.has_battery ||
                    battery.plugged == GuiBattery::Plugged::Plugged)
                       ? GuiLogoGlyph::Charging
                       : GuiLogoGlyph::Discharging;
    return legend;
}
