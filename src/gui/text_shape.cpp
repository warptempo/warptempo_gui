#include "text_shape.h"

#include <cairo/cairo-ft.h>
#include <hb-ft.h>
#include <hb.h>

#include <cmath>

namespace text_shape {

namespace {

// HarfBuzz positions arrive in 26.6 fixed point here: hb-ft scales the font
// from the FT face's own size metrics, so every offset and advance is a
// pixel value times 64.
constexpr double k26Dot6 = 64.0;

// RAII over the three handles the shaping pass borrows. Each release is
// unconditional and ordered by declaration: the buffer and the hb font go
// before the face unlock, which is what cairo requires. The body allocates
// (the glyph vector), so no plain call at the end could be trusted to run.
class ScaledFontFace {
public:
    explicit ScaledFontFace(cairo_scaled_font_t* font)
        : font_(font), face_(cairo_ft_scaled_font_lock_face(font)) {}
    ~ScaledFontFace() { cairo_ft_scaled_font_unlock_face(font_); }
    ScaledFontFace(const ScaledFontFace&) = delete;
    ScaledFontFace& operator=(const ScaledFontFace&) = delete;
    FT_Face face() const { return face_; }

private:
    cairo_scaled_font_t* font_;
    FT_Face              face_;
};

class HbFont {
public:
    explicit HbFont(FT_Face face) : font_(hb_ft_font_create(face, nullptr)) {}
    ~HbFont() { hb_font_destroy(font_); }
    HbFont(const HbFont&) = delete;
    HbFont& operator=(const HbFont&) = delete;
    hb_font_t* get() const { return font_; }

private:
    hb_font_t* font_;
};

class HbBuffer {
public:
    HbBuffer() : buffer_(hb_buffer_create()) {}
    ~HbBuffer() { hb_buffer_destroy(buffer_); }
    HbBuffer(const HbBuffer&) = delete;
    HbBuffer& operator=(const HbBuffer&) = delete;
    hb_buffer_t* get() const { return buffer_; }

private:
    hb_buffer_t* buffer_;
};

// THE OUTLINE ROAD: shape `utf8`'s bytes [offset, offset + length) with
// HarfBuzz on `font`'s own FT face and append the glyphs to `run`. The
// buffer is handed the WHOLE string with the item window, so each glyph's
// cluster is its byte index into the whole string — the fallback shapes the
// whole run in one window, the bitmap mode each stretch the strike lacks.
//
// The hb font is built per call, and stays that way deliberately: the
// build is a face wrap plus a scale read, and a cache would have to be
// keyed on the scaled font's identity and invalidated with it.
void append_outline_glyphs(cairo_scaled_font_t* font, std::string_view utf8,
                           size_t offset, size_t length, ShapedRun& run) {
    ScaledFontFace locked(font);
    HbFont         hb_font(locked.face());
    HbBuffer       buffer;

    hb_buffer_add_utf8(buffer.get(), utf8.data(),
                       static_cast<int>(utf8.size()),
                       static_cast<unsigned>(offset),
                       static_cast<int>(length));
    // Direction is ours (LTR horizontal runs only); script and language are
    // guessed from the text, which leaves the already-set direction alone.
    hb_buffer_set_direction(buffer.get(), HB_DIRECTION_LTR);
    hb_buffer_guess_segment_properties(buffer.get());

    hb_shape(hb_font.get(), buffer.get(), nullptr, 0);

    unsigned            count = 0;
    const hb_glyph_info_t*     infos =
        hb_buffer_get_glyph_infos(buffer.get(), &count);
    const hb_glyph_position_t* positions =
        hb_buffer_get_glyph_positions(buffer.get(), &count);

    for (unsigned i = 0; i < count; ++i) {
        ShapedGlyph glyph;
        // After shaping, `codepoint` holds the substituted GLYPH ID.
        glyph.glyph_index  = infos[i].codepoint;
        // The byte index this glyph's cluster starts at (see ShapedGlyph).
        glyph.cluster      = infos[i].cluster;
        glyph.x_offset_px  = positions[i].x_offset / k26Dot6;
        glyph.y_offset_px  = positions[i].y_offset / k26Dot6;
        glyph.x_advance_px = positions[i].x_advance / k26Dot6;
        run.width_px += glyph.x_advance_px;
        run.glyphs.push_back(glyph);
    }
}

// One UTF-8 codepoint at `pos`: its value and its byte length. A malformed
// lead or a truncated sequence answers U+FFFD for one byte (the header's
// precondition).
char32_t decode_utf8(std::string_view s, size_t pos, size_t& len) {
    const unsigned char c0 = static_cast<unsigned char>(s[pos]);
    int need = 0;
    char32_t cp = 0;
    if (c0 < 0x80)              { len = 1; return c0; }
    else if ((c0 & 0xE0) == 0xC0) { need = 1; cp = c0 & 0x1F; }
    else if ((c0 & 0xF0) == 0xE0) { need = 2; cp = c0 & 0x0F; }
    else if ((c0 & 0xF8) == 0xF0) { need = 3; cp = c0 & 0x07; }
    else                          { len = 1; return 0xFFFD; }
    if (pos + static_cast<size_t>(need) >= s.size()) {
        len = 1;
        return 0xFFFD;
    }
    for (int i = 1; i <= need; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[pos + i]);
        if ((c & 0xC0) != 0x80) { len = 1; return 0xFFFD; }
        cp = (cp << 6) | (c & 0x3F);
    }
    len = static_cast<size_t>(need) + 1;
    return cp;
}

} // namespace

ShapedRun shape_text_run(const GuiFont& font, std::string_view utf8) {
    ShapedRun run;
    run.font = font;
    if (utf8.empty()) return run;

    if (!gui_font_is_bitmap(font)) {
        append_outline_glyphs(gui_outline_scaled_font(font), utf8, 0,
                              utf8.size(), run);
        return run;
    }

    // THE BITMAP MODE: the strike's glyph per codepoint at the strike's
    // advance times k; each stretch of codepoints the strike lacks goes to
    // the outline road whole, so its marks and ligatures still shape.
    const int k = font.percent / 100;
    size_t pos = 0;
    while (pos < utf8.size()) {
        size_t len = 0;
        const char32_t cp = decode_utf8(utf8, pos, len);
        const GuiStrikeGlyph* g = gui_strike_glyph(font.face, cp);
        if (g != nullptr) {
            ShapedGlyph glyph;
            glyph.strike       = g;
            glyph.cluster      = static_cast<unsigned>(pos);
            glyph.x_advance_px = static_cast<double>(g->advance * k);
            run.width_px += glyph.x_advance_px;
            run.glyphs.push_back(glyph);
            pos += len;
            continue;
        }
        size_t end = pos + len;
        while (end < utf8.size()) {
            size_t next_len = 0;
            const char32_t next = decode_utf8(utf8, end, next_len);
            if (gui_strike_glyph(font.face, next) != nullptr) break;
            end += next_len;
        }
        append_outline_glyphs(gui_outline_scaled_font(font), utf8, pos,
                              end - pos, run);
        pos = end;
    }
    return run;
}

void show_shaped_run(cairo_t* cr, const ShapedRun& run, double x, double y) {
    if (run.glyphs.empty()) return;

    const bool bitmap = gui_font_is_bitmap(run.font);
    // THE BITMAP MODE'S ORIGIN IS A WHOLE DEVICE PX, and each strike glyph's
    // pen is rounded to one too (a stretch of outline glyphs before it may
    // leave the pen fractional), so every block lands on the pixel grid.
    const double ox = bitmap ? std::nearbyint(x) : x;
    const double oy = bitmap ? std::nearbyint(y) : y;
    const int    k  = run.font.percent / 100;

    std::vector<cairo_glyph_t> outline;
    outline.reserve(run.glyphs.size());
    cairo_path_t* saved_path = nullptr;
    bool any_strike = false;
    double pen_x = ox;
    for (const ShapedGlyph& glyph : run.glyphs) {
        if (glyph.strike != nullptr) {
            if (!any_strike) {
                saved_path = cairo_copy_path(cr);
                cairo_new_path(cr);
                any_strike = true;
            }
            const GuiStrikeGlyph& g = *glyph.strike;
            const double gx = std::nearbyint(pen_x) +
                              static_cast<double>(g.left * k);
            const double gy = oy - static_cast<double>(g.top * k);
            // One rectangle per horizontal stretch of lit pixels in a row.
            for (int r = 0; r < g.height; ++r) {
                const uint8_t* row = g.lit.data() + r * g.width;
                for (int c = 0; c < g.width;) {
                    if (!row[c]) { ++c; continue; }
                    int e = c;
                    while (e < g.width && row[e]) ++e;
                    cairo_rectangle(cr, gx + c * k, gy + r * k, (e - c) * k, k);
                    c = e;
                }
            }
        } else {
            cairo_glyph_t placed;
            placed.index = glyph.glyph_index;
            placed.x     = pen_x + glyph.x_offset_px;
            // HarfBuzz y is up-positive, cairo's is down-positive.
            placed.y     = oy - glyph.y_offset_px;
            outline.push_back(placed);
        }
        pen_x += glyph.x_advance_px;
    }
    cairo_save(cr);
    if (any_strike) {
        // THE BLIT: hard-edged blocks, no coverage at their edges.
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_fill(cr);
        cairo_append_path(cr, saved_path);
        cairo_path_destroy(saved_path);
    }
    if (!outline.empty()) {
        cairo_set_scaled_font(cr, gui_outline_scaled_font(run.font));
        cairo_show_glyphs(cr, outline.data(), static_cast<int>(outline.size()));
    }
    cairo_restore(cr);
}

// (ink_extents_px retired 2026-08-31 with its one consumer, row 8's sans state
// cell — the record is at the declaration's old place in text_shape.h.)

std::vector<double> byte_offsets_px(const ShapedRun& run, size_t byte_count) {
    // byte_count + 1 boundaries; the contract (including the
    // every-byte-of-a-cluster-reports-its-cluster's-START rule) is at the
    // declaration.
    //
    // THE WALK IS BY CLUSTER, NOT BY GLYPH, and that distinction is the whole
    // correctness argument. A first draft filled boundaries up to each glyph's
    // cluster as it arrived, which quietly gave every INTERIOR byte of a cluster
    // the pen of where that cluster ENDS — so an `fi` ligature's byte 1 reported
    // the ligature's right edge, and a click just past the rendered ligature
    // tied against that interior boundary and put the caret between the `f` and
    // the `i`. Here the fill for a cluster happens only once its SUCCESSOR is
    // known, using the pen recorded at the cluster's FIRST glyph, so every byte
    // the cluster covers reports that one start.
    std::vector<double> out(byte_count + 1, 0.0);
    double pen         = 0.0;   // running pen across the whole run
    double cluster_pen = 0.0;   // pen at the current cluster's first glyph
    size_t cluster_lo  = 0;     // that cluster's first byte
    bool   open        = false; // a cluster is being accumulated
    for (const ShapedGlyph& glyph : run.glyphs) {
        const size_t cluster = static_cast<size_t>(glyph.cluster);
        // Several glyphs may share one cluster (a decomposed character); only
        // the first of them sets the pen the cluster's bytes will report.
        if (!open || cluster != cluster_lo) {
            if (open) {
                for (size_t b = cluster_lo; b < cluster && b <= byte_count; ++b)
                    out[b] = cluster_pen;
            }
            cluster_lo  = cluster;
            cluster_pen = pen;
            open        = true;
        }
        pen += glyph.x_advance_px;
    }
    // The LAST cluster's bytes, by the same rule — its successor is the end of
    // the string rather than another cluster, which is the arm the first draft
    // also got wrong (a run ending in a ligature reported its end pen for its
    // interior bytes).
    if (open) {
        for (size_t b = cluster_lo; b <= byte_count; ++b) out[b] = cluster_pen;
    }
    // THE TRAILING BOUNDARY IS ALWAYS THE RUN'S END — it is the caret position
    // after the last cluster, not a byte inside one, so it is written last and
    // unconditionally. `pen` is that width by construction (run.width_px is the
    // same sum), so this is not a second derivation.
    out[byte_count] = pen;
    // Bytes BEFORE the first cluster (which LTR shaping does not produce — the
    // first glyph's cluster is 0) keep their zero initialisation, the correct
    // answer for a boundary at the run's origin.
    return out;
}

} // namespace text_shape
