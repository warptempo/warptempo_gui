#pragma once
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

// THE HOST'S BATTERY ACROSS THE SEAM, AND THE LOGO LANE'S LEGEND COMPOSED
// FROM IT AND THE WALL CLOCK (architect 2026-10-24; the legend's first design
// stood in the menu row 2026-10-01 to 2026-10-05). The type is the seam's —
// declared here so the two GuiPlatform headers and the GUI share ONE
// spelling, gui_media.h's shape — and it is a plain value:
// GuiPlatform::battery_status fills it from what the host knows (the
// contracts are at the two declarations), and the tick composes the legend
// from it and the wall clock (main.cpp's tick, refresh_logo_legend, the
// legend's one refresh site; render.h's logo lane block and
// paint_handler.cpp's paint_logo_lane paint it).

struct GuiBattery {
    // WHETHER A POWER SOURCE IS ATTACHED: Plugged covers charging, full and
    // "not charging" while on power alike (any source: AC, USB, wireless,
    // dock); Unknown is no status at all, or one the host cannot read.
    enum class Plugged : uint8_t { Plugged, Unplugged, Unknown };

    // FALSE ON A HOST WITH NO BATTERY (a desktop on mains).
    bool    has_battery = false;
    // 0..100, or -1 when the host cannot read the level.
    int     percent     = -1;
    Plugged plugged     = Plugged::Unknown;

    bool operator==(const GuiBattery&) const = default;
};

// THE LEGEND'S GLYPH, ALWAYS ONE OF THE TWO (architect 2026-10-24: "I prefer
// not to use nothing … the glyph always there taking up space so that the
// clock doesn't move"): THE BOLT while any power source is attached, 100 %
// INCLUDED ("the charging one on even at 100 %"), and on a host with no
// battery (power is attached); THE BATTERY while discharging — and while
// the plug state is UNKNOWN (the cold answer before the tablet's first
// broadcast, an unreadable sysfs node): the bolt claims power, so a state
// that cannot claim it takes the battery. The drawings are render.h's logo
// lane block.
enum class GuiLogoGlyph : uint8_t { Bolt, Battery };

// THE LEGEND — "92% <glyph> | 2:12 PM" — AS THREE PARTS, laid right to left
// by the painter (render.h's logo lane block): THE CLOCK RUN with its pipe,
// the glyph, and the PERCENT RUN.
struct GuiLogoLegend {
    std::string  percent;   // "92%", or empty: no battery, or no level read
    GuiLogoGlyph glyph = GuiLogoGlyph::Bolt;
    std::string  clock;     // "| 2:12 PM"
    bool operator==(const GuiLogoLegend&) const = default;
};

// THE ONE COMPOSER (architect 2026-10-24):
//   * THE PERCENT: the level and "%", NO LEADING ZERO ("100%" grows
//     leftward, away from the clock — the painter's right-to-left walk); an
//     unreadable level, or no battery, drops it and keeps the glyph.
//   * THE GLYPH: GuiLogoGlyph's rule above.
//   * THE CLOCK: "| " and the wall clock in 12-HOUR LOCAL time with AM / PM
//     and NO LEADING ZERO on the hour ("2:12 PM", "12:05 AM"), from the C
//     library on both hosts (localtime_r). The AM / PM words are spelled
//     here rather than taken from strftime's %p: the product's text is its
//     own, and %p is the locale's word. The pipe is the body face's own
//     glyph, never a drawn one (architect 2026-10-24: "I don't think we
//     should draw our own pipe"), lifted onto the digits' band like every
//     pipe the face shows (gui_font.h's kGuiLiftedSigns).
// WI-FI IS NOT PART OF THIS (architect 2026-10-01: "maybe later").
inline GuiLogoLegend compose_logo_legend(const GuiBattery& battery,
                                         std::time_t now) {
    std::tm local{};
    localtime_r(&now, &local);
    int hour = local.tm_hour % 12;
    if (hour == 0) hour = 12;
    char clock[24];
    std::snprintf(clock, sizeof(clock), "| %d:%02d %s", hour, local.tm_min,
                  local.tm_hour < 12 ? "AM" : "PM");

    GuiLogoLegend legend;
    legend.clock = clock;
    if (battery.has_battery && battery.percent >= 0)
        legend.percent = std::to_string(battery.percent) + "%";
    legend.glyph = (!battery.has_battery ||
                    battery.plugged == GuiBattery::Plugged::Plugged)
                       ? GuiLogoGlyph::Bolt
                       : GuiLogoGlyph::Battery;
    return legend;
}
