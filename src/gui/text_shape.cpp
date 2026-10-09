#include "text_shape.h"

#include <cairo/cairo-ft.h>
#include <hb-ft.h>
#include <hb.h>

namespace text_shape {

namespace {

// HarfBuzz positions arrive in 26.6 fixed point here: hb-ft scales the font
// from the FT face's own size metrics, so every offset and advance is a
// pixel value times 64.
constexpr double k26Dot6 = 64.0;

// RAII over the FT face lock the shaping pass holds. The release is
// unconditional: the body allocates (the glyph vector), so no plain call at
// the end could be trusted to run, and the buffer (declared after it) goes
// before the unlock, which is what cairo requires.
//
// THE LOCK STAYS AROUND EVERY SHAPE, cached hb font or not: the body and the
// small share one FT face (the live set's regular file) at two sizes,
// cairo's lock sets this
// scaled font's size on it, and hb-ft reads the face's live size for every
// advance at shape time.
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

// THE ONE HB FONT PER SCALED FONT (architect 2026-10-07: the stutter at full
// zoom-out on the phase-reset axis, 435 flag labels shaped per paint, is fixed
// by caching it). Building an hb font through hb_ft_font_create also builds a
// fresh hb face, so HarfBuzz re-read cmap/GSUB/GPOS and rebuilt its shape plan
// and advance cache for EVERY run — 5 to 6.5 us a label on the laptop at 300,
// nearly the whole cost of a shape; cached, a label costs under 1 us
// (measured 2026-10-07). So the hb font is built ONCE PER
// cairo_scaled_font_t and hung on it as cairo USER DATA, destroyed by cairo
// with it: KEYED ON THE SCALED FONT'S IDENTITY AND INVALIDATED WITH IT by
// construction. The face owner's per-(face, percent) cache
// (gui_outline_scaled_font) rebuilds the scaled font when the percent moves,
// and the new one carries no hb font until its first shape, so the two can
// never disagree; a pointer-keyed mirror here could (a freed scaled font's
// address reused by its successor), and a second cache in the owner would
// pull HarfBuzz into the face owner for no gain. Built inside the caller's
// lock, so hb-ft reads its scale off the face at this scaled font's own
// size, exactly as the per-call build did, and the advances are the same
// bits (checked 2026-10-07 on every face at 300 and the body at 138, the
// widths and every glyph's offsets and advances compared before and after).
//
// THE HB FONT LOADS OUTLINES ONLY (gui_font_bundled.cpp's head, the strike
// rule's glyph-load road): hb-ft's own default, unhinted, plus
// FT_LOAD_NO_BITMAP, so no advance is ever read off an embedded strike.
// GUI thread only, like the scaled font it hangs on.
cairo_user_data_key_t g_hb_font_key;

void destroy_hb_font(void* font) {
    hb_font_destroy(static_cast<hb_font_t*>(font));
}

hb_font_t* hb_font_of(cairo_scaled_font_t* scaled, FT_Face locked_face) {
    if (void* cached = cairo_scaled_font_get_user_data(scaled, &g_hb_font_key))
        return static_cast<hb_font_t*>(cached);
    hb_font_t* font = hb_ft_font_create(locked_face, nullptr);
    hb_ft_font_set_load_flags(
        font, FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
    cairo_scaled_font_set_user_data(scaled, &g_hb_font_key, font,
                                    destroy_hb_font);
    return font;
}

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

// THE ONE ROAD: shape `utf8` whole with HarfBuzz on `font`'s scaled font's
// own FT face, through the hb font cached on that scaled font (hb_font_of),
// and append the glyphs to `run`; each glyph's cluster is its byte index into
// the string. EVERY GLYPH'S ADVANCE TAKES THE TRACKING (gui_tracking_px,
// gui_font.h) after the 26.6 conversion, the last included, and A LIFTED
// MARK'S Y OFFSET TAKES ITS LIFT where the live set lifts it
// (gui_sign_lift_px, gui_font.h — the four math signs onto the hyphen's
// axis in the FreeSans and Liberation sets, 2026-10-09, the latter's
// lift negative, added as it comes; the pipe onto the digits' band in
// every set, 2026-10-09 ~21:20: matched by glyph id after substitution,
// HarfBuzz's up-positive sense, the advance untouched).
void append_glyphs(const GuiFont& font, std::string_view utf8,
                   ShapedRun& run) {
    const double         tracking = gui_tracking_px(font);
    cairo_scaled_font_t* scaled   = gui_outline_scaled_font(font);
    ScaledFontFace       locked(scaled);
    hb_font_t*           hb_font  = hb_font_of(scaled, locked.face());
    HbBuffer             buffer;

    hb_buffer_add_utf8(buffer.get(), utf8.data(),
                       static_cast<int>(utf8.size()), 0,
                       static_cast<int>(utf8.size()));
    // Direction is ours (LTR horizontal runs only); script and language are
    // guessed from the text, which leaves the already-set direction alone.
    hb_buffer_set_direction(buffer.get(), HB_DIRECTION_LTR);
    hb_buffer_guess_segment_properties(buffer.get());

    hb_shape(hb_font, buffer.get(), nullptr, 0);

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
        glyph.y_offset_px  = positions[i].y_offset / k26Dot6 +
                             gui_sign_lift_px(font, glyph.glyph_index);
        glyph.x_advance_px = positions[i].x_advance / k26Dot6 + tracking;
        run.width_px += glyph.x_advance_px;
        run.glyphs.push_back(glyph);
    }
}

} // namespace

ShapedRun shape_text_run(const GuiFont& font, std::string_view utf8) {
    ShapedRun run;
    run.font = font;
    if (utf8.empty()) return run;
    append_glyphs(font, utf8, run);
    return run;
}

// THE RUN'S GLYPHS PLACED at the baseline origin (x, y) — the one placement
// the painter hands cairo and the ink measure below reads, so the two cannot
// part.
static std::vector<cairo_glyph_t> placed_glyphs_of(const ShapedRun& run,
                                                   double x, double y) {
    std::vector<cairo_glyph_t> placed_glyphs;
    placed_glyphs.reserve(run.glyphs.size());
    double pen_x = x;
    for (const ShapedGlyph& glyph : run.glyphs) {
        cairo_glyph_t placed;
        placed.index = glyph.glyph_index;
        placed.x     = pen_x + glyph.x_offset_px;
        // HarfBuzz y is up-positive, cairo's is down-positive.
        placed.y     = y - glyph.y_offset_px;
        placed_glyphs.push_back(placed);
        pen_x += glyph.x_advance_px;
    }
    return placed_glyphs;
}

void show_shaped_run(cairo_t* cr, const ShapedRun& run, double x, double y) {
    if (run.glyphs.empty()) return;
    const std::vector<cairo_glyph_t> placed_glyphs = placed_glyphs_of(run, x, y);
    cairo_save(cr);
    cairo_set_scaled_font(cr, gui_outline_scaled_font(run.font));
    cairo_show_glyphs(cr, placed_glyphs.data(),
                      static_cast<int>(placed_glyphs.size()));
    cairo_restore(cr);
}

double ink_left_px(const ShapedRun& run) {
    // The contract is at the declaration: the paint's own placement at pen
    // origin (0, 0), measured on the run's own font.
    if (run.glyphs.empty()) return 0.0;
    const std::vector<cairo_glyph_t> placed = placed_glyphs_of(run, 0.0, 0.0);
    cairo_text_extents_t ext;
    cairo_scaled_font_glyph_extents(gui_outline_scaled_font(run.font),
                                    placed.data(),
                                    static_cast<int>(placed.size()), &ext);
    return ext.x_bearing;
}

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
