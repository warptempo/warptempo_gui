#pragma once

// This is the live plain-text tier. `draw_line` is the plain
// text-display primitive consumed by the bottom strip's assembled row and
// the modal prompt / queue overlays (paint_handler.cpp).

#include "render.h"

#include <cairo/cairo.h>
#include <string>

namespace text_display {

// Plain text-display tier. Draws `content` left-anchored with
// its baseline at (x, baseline_y), in monospace at `font_size`, tinted
// `color`. No fill, no outline, no cursor — those live in the editor tier
// (render_editor_text_box). Returns the measured x_advance so flow callers
// (e.g. the modal prompt's response labels) can chain draws without
// re-measuring. Cairo state is saved/restored. A no-op returning 0 if
// `content` is empty.
double draw_line(cairo_t* cr,
                 double x,
                 double baseline_y,
                 const std::string& content,
                 GuiColor color,
                 double font_size);

} // namespace text_display
