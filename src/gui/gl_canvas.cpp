#include "gl_canvas.h"

#include "canvas_sink.h"   // GuiCanvasDrawList, GuiCanvasPrimitive

#include <GLES3/gl3.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

// THE GL CANVAS'S OWNER (the class's head, gl_canvas.h): the programs, the
// textures and the frame's GL calls, in their own object (2026-10-14) the
// platform's present drives. The calls, their order and their arguments are
// the tablet's of 2026-10-13, the build the GL canvas's identity proof
// covered (docs/engineering/gpu_canvas_2026_10.md §5). One reduction since,
// pixel-neutral by its
// key: the plate's bounds texture is re-sent only when its key moved
// (upload_list, 2026-10-14). Two additions: the score viewer's page drawn by
// the GPU (2026-10-27, the page program and its textures; the design record's
// §3.13), and THE WAVEFORM'S ANTIALIASING (architect 2026-10-30): the plate's
// fragment rasterizes the column tips at exact coverage and blends
// source-over, the one blend the canvas does (kListFragmentShader's head;
// the rule at render_waveform_columns' head, render.h).

namespace {

// A SETUP FAILURE IS FATAL (the host's rule, GuiGlCanvasHost): the platform
// writes the line and never returns; the abort behind it states as much to
// the compiler.
[[noreturn]] void canvas_fatal(const GuiGlCanvasHost& host,
                               const char* message) {
    host.fatal(message);
    std::abort();
}

// ONE TRIANGLE OVER THE WHOLE VIEWPORT, its corners from gl_VertexID alone
// ((-1,-1), (3,-1), (-1,3)), so no vertex buffer and no attribute exists:
// the default vertex array draws it.
constexpr const char* kCanvasVertexShader =
    "#version 300 es\n"
    "void main() {\n"
    "    vec2 p = vec2(float((gl_VertexID & 1) << 2) - 1.0,\n"
    "                  float((gl_VertexID & 2) << 1) - 1.0);\n"
    "    gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

// THE TEXEL IS PLACED, NEVER FILTERED: texelFetch at the fragment's own
// integer pixel, the row flipped (cairo's row 0 is the top, GL's window row 0
// the bottom; u_height is the texture's height), and the cairo bytes B,G,R,A
// — uploaded as GL_RGBA, so the texel reads (B,G,R,A) — swizzled to R,G,B
// with alpha written 1 — the channel swap, in the shader. The sampler is highp so the fetched value carries the 8-bit
// UNORM texel exactly, and k/255 written to an RGBA8 framebuffer converts
// back to k.
// AN ALPHA-0 TEXEL IS DISCARDED (2026-10-13, the canvas sink's transparency
// invariant, canvas_sink.h): the backbuffer's canvas paint area is cleared
// to 0 where no overlay stands, so the canvas the list drew under it shows
// there, and every other texel is opaque and placed as above.
constexpr const char* kCanvasFragmentShader =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "uniform highp sampler2D u_tex;\n"
    "uniform int u_height;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    ivec2 p = ivec2(int(gl_FragCoord.x), u_height - 1 - int(gl_FragCoord.y));\n"
    "    vec4 t = texelFetch(u_tex, p, 0);\n"
    "    if (t.a == 0.0) discard;\n"
    "    o_color = vec4(t.bgr, 1.0);\n"
    "}\n";

// THE CANVAS LIST'S PROGRAM (2026-10-13; the records at GuiCanvasPrimitive,
// canvas_sink.h; the draw at present_frame). ONE INSTANCE A RECORD, in list
// order, one instanced draw of a four-vertex strip: the instance's rect, in
// surface px with row 0 the top, is the quad (its corners from gl_VertexID),
// and the fragment shader takes the record's integer rule at the fragment's own
// pixel — the column int(gl_FragCoord.x), the row H − 1 − int(gl_FragCoord.y)
// (u_height the surface's height, H) — every comparison and the modulo in highp
// int. A pixel outside the instance's rect is discarded (the quad's float edges
// never decide coverage); a RECT writes its word; a DOTTED RUN (kind 1 a
// column, 2 a row) writes it where (c − origin) % period = phase, c the row or
// the column, never negative (the sink moved the origin back to the run's
// first cell or before; GLSL ES 3.00 leaves % on a negative operand undefined);
// THE PLATE (kind 3) is THE WAVEFORM'S ANTIALIASING (architect 2026-10-30;
// the rule at render_waveform_columns' head, render.h), an INTEGER
// ALGORITHM over the bounds texture
// (an isampler2D over GL_RGBA16I: row `period` holds, per column of the
// record, the four QUARTER-ROW tips {o_top, o_bot, i_top, i_bot}, the inner
// absent where i_bot <= i_top) at the plate's row y (the pixel's row less
// `origin`), in SUBROWS (quarter rows, kWaveformSubcells per row) and
// SUBCOLUMNS (four per column, the pixel's own 4 x 4 subcells its coverage):
//   * THE WINDOW: a mask is 20 bits, bit b the subrow 4y − 8 + b, the
//     pixel's own subrows 4y .. 4y + 3 bits 8 .. 11 (kPixBits). Every mask
//     is honest where every bit it was computed from is: the base masks on
//     all 20 bits, the dilated ones (grown by up to 2) on bits 2 .. 17, the
//     eroded ones (shrunk by up to 4) on bits 6 .. 13, which hold the pixel's
//     with two bits to spare.
//   * THE SEVEN COLUMNS i − 3 .. i + 3 are texelFetched (clamped into the
//     record's width): the inner's sixteen subcolumns 4i − 6 .. 4i + 9 belong
//     to columns i − 2 .. i + 2, whose contours read their neighbors.
//   * THE BASE of subcolumn j of a column (base_bits): its bar's subrows
//     [T, B − 1] UNION the contour's — the contour's top at the subcolumn's
//     center interpolates this column's T and the NEARER neighbor's in
//     eighths (j = 0..3: 3:5, 1:7, 7:1, 5:3; the left neighbor for j < 2,
//     the right for j >= 2), the bottom likewise, and subrow k is in it iff
//     8Tc <= 8k + 4 <= 8Bc, i.e. (8Tc + 3) / 8 <= k <= (8Bc − 4) / 8 — both
//     operands non-negative (T >= 0, B >= 1 for a present bar), so / is
//     floor. A neighbor beyond the record's side edge is the column itself
//     (the contour runs flat to the edge), as is an absent inner neighbor; a
//     subcolumn beyond the edge, or of an absent bar, has an empty base.
//   * THE DILATION (grow 0.5 = the disc of radius kWaveformGrowSubcells = 2
//     subcells, 13 offsets): at a subcolumn, the union of the five bases
//     within two subcolumns, each grown vertically by 2, 1, 0 rows at
//     |dx| = 0, 1, 2 (dilate: grow2, grow1 as shifted ORs). mo the outer's
//     over the pixel's four subcolumns, mi the inner's over twelve (the
//     erosion reads four either side).
//   * THE EROSION (the outline's distance kWaveformSubcells ·
//     waveform_line_px() = 4 subcells, the disc's 49 offsets): the inner's
//     interior at a subcell holds iff the nine mi within four subcolumns,
//     each shrunk vertically by 4, 3, 3, 2, 0 rows at |dx| = 0..4 (erode:
//     shrink4 .. shrink2 as shifted ANDs), are all set. BEYOND THE RECORD'S
//     SIDE EDGES AND BEYOND THE BAND'S ROWS mi COUNTS AS SET (`outside`, the
//     all-ones mask): the waveform continues off screen, so no vertical line
//     at the area's sides and none at the band's edges.
//   * THE COVERAGE per pixel, in sixteenths (pixel_count, a 16-bit popcount
//     — bitCount is GLSL ES 3.10's, not 3.00's): n_o = |(mo ∪ mi) ∩ pixel|,
//     n_i = |mi ∩ pixel|, n_t = |interior ∩ pixel|; the outline's coverage
//     is (n_i − n_t) / 16, never negative (the interior is inside mi).
//   * THE COMPOSITE, source-over in byte space: the fragment writes the
//     PREMULTIPLIED source  ink · cov_o · (1 − cov_ol) + outline · cov_ol
//     with alpha 1 − (1 − cov_o)(1 − cov_ol), and the blend
//     glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA) (present_frame) lays it on
//     the pixel beneath, G: the result is lerp(lerp(G, ink, cov_o), outline,
//     cov_ol), rounded once to a byte. Where the core is (cov_o = 1, cov_ol 0
//     or 1) the factors are exactly 1 and 0 and the word lands exactly;
//     where nothing covers (n_o = 0) the fragment discards and the plate
//     stays transparent there.
//   * TWO EXACT FAST PATHS, each a provable subset of the rule above:
//     (a) DISCARD where no bar can reach the pixel — its subrows lie more
//     than two subrows outside the hull of the outer bars of columns
//     i − 1 .. i + 1 and outside the hull of their present inner bars (the
//     contour between two columns lies within their two extents, and the
//     dilation reaches two subrows); (b) THE CORE — the pixel's subrows
//     inside its own column's outer bar give cov_o = 1 (the base is inside
//     the dilated), and cov_ol = 0 when no inner is present among
//     i − 1 .. i + 1, or when the square of four subrows round the pixel lies
//     inside the own and both in-record neighbors' inner bars (the square
//     contains the erosion's disc; a neighbor beyond the edge counts as
//     set): the ink is written opaque.
// A word is cairo's 0xAARRGGBB with A = 255, written as (R, G, B, 1) at
// k / 255 per channel for every record but the plate's fringe, which the
// RGBA8 framebuffer stores as k — the texture triangle's own channel order.
// Primitives write in instance order (GL's primitive order within a draw), so a
// later record overwrites an earlier one, the paint order; under the blend an
// opaque record's word lands exactly (src · 1 + dst · 0).
constexpr const char* kListVertexShader =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "layout(location = 0) in ivec4 a_rect;\n"
    "layout(location = 1) in ivec4 a_param;\n"
    "layout(location = 2) in uvec2 a_words;\n"
    "uniform vec2 u_size;\n"
    "flat out ivec4 v_rect;\n"
    "flat out ivec4 v_param;\n"
    "flat out uvec2 v_words;\n"
    "void main() {\n"
    "    int x = a_rect.x + (((gl_VertexID & 1) != 0) ? a_rect.z : 0);\n"
    "    int y = a_rect.y + (((gl_VertexID & 2) != 0) ? a_rect.w : 0);\n"
    "    gl_Position = vec4(float(x) / u_size.x * 2.0 - 1.0,\n"
    "                       1.0 - float(y) / u_size.y * 2.0, 0.0, 1.0);\n"
    "    v_rect  = a_rect;\n"
    "    v_param = a_param;\n"
    "    v_words = a_words;\n"
    "}\n";

constexpr const char* kListFragmentShader =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "uniform highp isampler2D u_columns;\n"
    "uniform int u_height;\n"
    "flat in ivec4 v_rect;\n"
    "flat in ivec4 v_param;\n"
    "flat in uvec2 v_words;\n"
    "out vec4 o_color;\n"
    "vec3 word_rgb(uint w) {\n"
    "    return vec3(float((w >> 16u) & 255u), float((w >> 8u) & 255u),\n"
    "                float(w & 255u)) / 255.0;\n"
    "}\n"
    // The plate's helpers (the head's plate paragraph): a mask's bit b is the
    // subrow k0 + b, b in [0, 20); range_bits clips a subrow run to the window.
    "uint range_bits(int lo, int hi) {\n"
    "    int a = max(lo, 0);\n"
    "    int b = min(hi, 19);\n"
    "    if (b < a) return 0u;\n"
    "    return ((1u << (b - a + 1)) - 1u) << a;\n"
    "}\n"
    "uint base_bits(ivec2 own, ivec2 nb, int j, int k0) {\n"
    "    int wn = (j == 0 || j == 3) ? 3 : 1;\n"
    "    int wc = 8 - wn;\n"
    "    int t8 = wn * nb.x + wc * own.x;\n"
    "    int b8 = wn * nb.y + wc * own.y;\n"
    "    return range_bits((t8 + 3) / 8 - k0, (b8 - 4) / 8 - k0) |\n"
    "           range_bits(own.x - k0, own.y - 1 - k0);\n"
    "}\n"
    "uint grow1(uint m) { return m | (m << 1) | (m >> 1); }\n"
    "uint grow2(uint m) { return grow1(m) | (m << 2) | (m >> 2); }\n"
    "uint shrink2(uint m) { return m & (m << 1) & (m >> 1) & (m << 2) & (m >> 2); }\n"
    "uint shrink3(uint m) { return shrink2(m) & (m << 3) & (m >> 3); }\n"
    "uint shrink4(uint m) { return shrink3(m) & (m << 4) & (m >> 4); }\n"
    "uint dilate(uint l2, uint l1, uint c, uint r1, uint r2) {\n"
    "    return l2 | grow1(l1) | grow2(c) | grow1(r1) | r2;\n"
    "}\n"
    "uint erode(uint a4, uint a3, uint a2, uint a1, uint c,\n"
    "           uint b1, uint b2, uint b3, uint b4) {\n"
    "    return a4 & shrink2(a3) & shrink3(a2) & shrink3(a1) & shrink4(c) &\n"
    "           shrink3(b1) & shrink3(b2) & shrink2(b3) & b4;\n"
    "}\n"
    "int pop16(uint v) {\n"
    "    v = v - ((v >> 1) & 0x5555u);\n"
    "    v = (v & 0x3333u) + ((v >> 2) & 0x3333u);\n"
    "    v = (v + (v >> 4)) & 0x0F0Fu;\n"
    "    return int((v + (v >> 8)) & 0x1Fu);\n"
    "}\n"
    "int pixel_count(uint m0, uint m1, uint m2, uint m3) {\n"
    "    return pop16(((m0 >> 8) & 0xFu) | ((m1 >> 4) & 0xF0u) |\n"
    "                 (m2 & 0xF00u) | ((m3 << 4) & 0xF000u));\n"
    "}\n"
    "void main() {\n"
    "    int px = int(gl_FragCoord.x);\n"
    "    int py = u_height - 1 - int(gl_FragCoord.y);\n"
    "    if (px < v_rect.x || px >= v_rect.x + v_rect.z ||\n"
    "        py < v_rect.y || py >= v_rect.y + v_rect.w) discard;\n"
    "    int kind = v_param.x;\n"
    "    if (kind == 1 || kind == 2) {\n"
    "        int c = ((kind == 1) ? py : px) - v_param.z;\n"
    "        if (c % v_param.y != v_param.w) discard;\n"
    "    } else if (kind == 3) {\n"
    "        int W = v_rect.z;\n"
    "        int i = px - v_rect.x;\n"
    "        int y = py - v_param.z;\n"
    "        int k0 = 4 * y - 8;\n"
    "        ivec4 col[7];\n"
    "        bool inp[7];\n"
    "        bool lit[7];\n"
    "        for (int d = 0; d < 7; ++d) {\n"
    "            int c = i + d - 3;\n"
    "            inp[d] = c >= 0 && c < W;\n"
    "            col[d] = texelFetch(u_columns, ivec2(clamp(c, 0, W - 1), v_param.y), 0);\n"
    "            lit[d] = inp[d] && col[d].w > col[d].z;\n"
    "        }\n"
    "        int ylo = 4 * y;\n"
    "        int yhi = ylo + 3;\n"
    "        int tmin_o = min(col[2].x, min(col[3].x, col[4].x));\n"
    "        int bmax_o = max(col[2].y, max(col[3].y, col[4].y));\n"
    "        int tmin_i = 1 << 20;\n"
    "        int bmax_i = -(1 << 20);\n"
    "        for (int d = 2; d <= 4; ++d) {\n"
    "            if (lit[d]) {\n"
    "                tmin_i = min(tmin_i, col[d].z);\n"
    "                bmax_i = max(bmax_i, col[d].w);\n"
    "            }\n"
    "        }\n"
    "        bool reach_o = yhi >= tmin_o - 2 && ylo <= bmax_o + 1;\n"
    "        bool reach_i = yhi >= tmin_i - 2 && ylo <= bmax_i + 1;\n"
    "        if (!reach_o && !reach_i) discard;\n"
    "        if (col[3].x <= ylo && yhi <= col[3].y - 1) {\n"
    "            bool core = !(lit[2] || lit[3] || lit[4]);\n"
    "            if (!core) {\n"
    "                core = true;\n"
    "                for (int d = 2; d <= 4; ++d) {\n"
    "                    if (inp[d])\n"
    "                        core = core && lit[d] && col[d].z <= ylo - 4 &&\n"
    "                               yhi + 4 <= col[d].w - 1;\n"
    "                }\n"
    "            }\n"
    "            if (core) {\n"
    "                o_color = vec4(word_rgb(v_words.x), 1.0);\n"
    "                return;\n"
    "            }\n"
    "        }\n"
    "        uint bo[8];\n"
    "        for (int q = 0; q < 8; ++q) {\n"
    "            int sp = q + 2;\n"
    "            int d = 2 + sp / 4;\n"
    "            int j = sp & 3;\n"
    "            int e = d + ((j < 2) ? -1 : 1);\n"
    "            bo[q] = inp[d] ? base_bits(col[d].xy, inp[e] ? col[e].xy : col[d].xy, j, k0)\n"
    "                           : 0u;\n"
    "        }\n"
    "        uint bi[16];\n"
    "        for (int q = 0; q < 16; ++q) {\n"
    "            int sp = q + 2;\n"
    "            int d = 1 + sp / 4;\n"
    "            int j = sp & 3;\n"
    "            int e = d + ((j < 2) ? -1 : 1);\n"
    "            bi[q] = lit[d] ? base_bits(col[d].zw, lit[e] ? col[e].zw : col[d].zw, j, k0)\n"
    "                           : 0u;\n"
    "        }\n"
    "        int band_lo = 4 * (v_rect.y - v_param.z);\n"
    "        int band_hi = 4 * (v_rect.y + v_rect.w - v_param.z);\n"
    "        uint outside = range_bits(0, band_lo - 1 - k0) | range_bits(band_hi - k0, 19);\n"
    "        uint mo[4];\n"
    "        for (int m = 0; m < 4; ++m)\n"
    "            mo[m] = dilate(bo[m], bo[m + 1], bo[m + 2], bo[m + 3], bo[m + 4]);\n"
    "        uint mi[12];\n"
    "        for (int q = 0; q < 12; ++q) {\n"
    "            int s = 4 * i - 4 + q;\n"
    "            mi[q] = (s >= 0 && s < 4 * W)\n"
    "                ? (dilate(bi[q], bi[q + 1], bi[q + 2], bi[q + 3], bi[q + 4]) | outside)\n"
    "                : 0xFFFFFu;\n"
    "        }\n"
    "        uint mt[4];\n"
    "        for (int m = 0; m < 4; ++m)\n"
    "            mt[m] = erode(mi[m], mi[m + 1], mi[m + 2], mi[m + 3], mi[m + 4],\n"
    "                          mi[m + 5], mi[m + 6], mi[m + 7], mi[m + 8]);\n"
    "        int n_o = pixel_count(mo[0] | mi[4], mo[1] | mi[5], mo[2] | mi[6], mo[3] | mi[7]);\n"
    "        int n_i = pixel_count(mi[4], mi[5], mi[6], mi[7]);\n"
    "        int n_t = pixel_count(mt[0], mt[1], mt[2], mt[3]);\n"
    "        if (n_o == 0) discard;\n"
    "        float cov_o  = float(n_o) / 16.0;\n"
    "        float cov_ol = float(n_i - n_t) / 16.0;\n"
    "        o_color = vec4(word_rgb(v_words.x) * (cov_o * (1.0 - cov_ol)) +\n"
    "                       word_rgb(v_words.y) * cov_ol,\n"
    "                       1.0 - (1.0 - cov_o) * (1.0 - cov_ol));\n"
    "        return;\n"
    "    }\n"
    "    o_color = vec4(word_rgb(v_words.x), 1.0);\n"
    "}\n";

// THE SHADER'S LITERALS ARE render.h's CONSTANTS: the shader is a string, so
// this is the binding. kWaveformSubcells is the 4 of `4 * y`, the eighths'
// 3:5 / 1:7 weights (two subcells to the half), the four-bit pixel slices
// and the 16 of the coverage; kWaveformGrowSubcells is grow2's disc and its
// 2, 1, 0 rows at |dx| = 0, 1, 2; the erosion's disc is kWaveformSubcells ·
// waveform_line_px() = shrink4's 4 and its 4, 3, 3, 2, 0 rows at |dx| = 0..4
// (disc_rows: the largest g with g² + dx² <= r²).
constexpr int disc_rows(int r, int dx) {
    int g = 0;
    while ((g + 1) * (g + 1) + dx * dx <= r * r) ++g;
    return g;
}
constexpr int kWaveformErodeSubcells = kWaveformSubcells * waveform_line_px();
static_assert(kWaveformSubcells == 4,
              "kListFragmentShader spells the 4x grid as literals");
static_assert(kWaveformGrowSubcells == 2 && disc_rows(2, 0) == 2 &&
                  disc_rows(2, 1) == 1 && disc_rows(2, 2) == 0,
              "kListFragmentShader's dilate is the disc of radius 2");
static_assert(kWaveformErodeSubcells == 4 && disc_rows(4, 0) == 4 &&
                  disc_rows(4, 1) == 3 && disc_rows(4, 2) == 3 &&
                  disc_rows(4, 3) == 2 && disc_rows(4, 4) == 0,
              "kListFragmentShader's erode is the disc of radius 4");

// THE PAGE PROGRAM (architect 2026-10-27, "1 a"; GuiCanvasPage,
// canvas_sink.h): the texture triangle's vertex shader over the whole
// viewport under the scissor to the box's interior (the list's clip), and at
// each fragment's own integer pixel — the column int(gl_FragCoord.x), the row
// H − 1 − int(gl_FragCoord.y), u_height the surface's height — THE PAGE'S
// TEXEL where the pixel lies inside u_rect (x, y, w, h in surface px, row 0
// the top: the page's place, gui_page_place), fetched by integer coordinate
// from the R8 texture on unit 2 (its row 0 the page's top row, as uploaded)
// and written (g, g, g, 1) — an 8-bit UNORM texel round-trips to its byte,
// the texture triangle's own argument — and THE FIELD'S WORD elsewhere,
// written as the list's words are (word_color: k / 255 per channel, alpha
// 1). An empty u_rect (no page held) writes the field alone.
constexpr const char* kPageFragmentShader =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "uniform highp sampler2D u_page;\n"
    "uniform int u_height;\n"
    "uniform ivec4 u_rect;\n"
    "uniform uint u_field;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    ivec2 q = ivec2(int(gl_FragCoord.x), u_height - 1 - int(gl_FragCoord.y))\n"
    "            - u_rect.xy;\n"
    "    if (q.x >= 0 && q.y >= 0 && q.x < u_rect.z && q.y < u_rect.w) {\n"
    "        float g = texelFetch(u_page, q, 0).r;\n"
    "        o_color = vec4(g, g, g, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    o_color = vec4(float((u_field >> 16u) & 255u),\n"
    "                   float((u_field >> 8u) & 255u),\n"
    "                   float(u_field & 255u), 255.0) / 255.0;\n"
    "}\n";

// AT MOST ONE PAGE UPLOADED A PRESENT (the page textures' rule,
// gl_canvas.h): about 2.2 MB at the tablet's box.
constexpr int kPageUploadsPerPresent = 1;

// One list record as the instance attributes read it: the rect (location
// 0), {kind, period, origin, phase} (location 1), {word, word2} (location 2),
// every coordinate in surface px (the GUI's own px: the app paints no window
// frame since 2026-10-24, so the two are one).
struct CanvasInstance {
    GLint  rect[4];
    GLint  param[4];
    GLuint words[2];
};

GLuint compile_canvas_shader(const GuiGlCanvasHost& host, GLenum kind,
                             const char* source) {
    const GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char info[1024] = {};
        glGetShaderInfoLog(s, static_cast<GLsizei>(sizeof(info)), nullptr,
                           info);
        char line[1152];
        std::snprintf(line, sizeof(line),
                      "the GL canvas: the %s shader failed to compile: %s",
                      kind == GL_VERTEX_SHADER ? "vertex" : "fragment", info);
        canvas_fatal(host, line);
    }
    return s;
}

// Link a program from two shader sources; a failure is fatal, the canvas
// program's rule (both programs, ensure_programs).
GLuint link_canvas_program(const GuiGlCanvasHost& host, const char* vs_source,
                           const char* fs_source) {
    const GLuint vs = compile_canvas_shader(host, GL_VERTEX_SHADER, vs_source);
    const GLuint fs =
        compile_canvas_shader(host, GL_FRAGMENT_SHADER, fs_source);
    const GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        char info[1024] = {};
        glGetProgramInfoLog(prog, static_cast<GLsizei>(sizeof(info)), nullptr,
                            info);
        char line[1152];
        std::snprintf(line, sizeof(line),
                      "the GL canvas: a program failed to link: %s", info);
        canvas_fatal(host, line);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

} // namespace

// THE CONTEXT'S FIXED STATE, set at each make current (the platform's
// surface road calls it right after its make current): nothing dithers or
// tests, and THE BLEND IS OFF AT REST — its function set once here, the
// premultiplied source-over the plate's fringe needs (kListFragmentShader's
// composite), and enabled for the canvas list's draw alone (present_frame,
// 2026-10-30) — so a texel and an opaque list word reach the framebuffer as
// written.
void GuiGlCanvas::bind_fixed_state() {
    glDisable(GL_DITHER);
    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
}

// THE THREE PROGRAMS, built at the first make current and kept with the
// context.
void GuiGlCanvas::ensure_programs() {
    if (!program_) {
        const GLuint prog =
            link_canvas_program(host_, kCanvasVertexShader,
                                kCanvasFragmentShader);
        glUseProgram(prog);
        glUniform1i(glGetUniformLocation(prog, "u_tex"), 0);
        height_uniform_ = glGetUniformLocation(prog, "u_height");
        program_ = prog;
    }
    // THE CANVAS LIST'S OBJECTS (2026-10-13; kListVertexShader's head): the
    // program, its own vertex array — the instance attributes never touch
    // the default one the texture triangle draws from — over one instance
    // buffer, and the plate's bounds texture on unit 1 (GL_RGBA16I, NEAREST:
    // an integer texture with a filtering minifier is incomplete), seeded
    // 1 x 1 so the sampler is complete before the first plate.
    if (!list_program_) {
        const GLuint prog =
            link_canvas_program(host_, kListVertexShader, kListFragmentShader);
        glUseProgram(prog);
        glUniform1i(glGetUniformLocation(prog, "u_columns"), 1);
        list_size_uniform_   = glGetUniformLocation(prog, "u_size");
        list_height_uniform_ = glGetUniformLocation(prog, "u_height");
        list_program_ = prog;
        glGenVertexArrays(1, &list_vao_);
        glGenBuffers(1, &list_vbo_);
        glBindVertexArray(list_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, list_vbo_);
        const GLsizei stride = static_cast<GLsizei>(sizeof(CanvasInstance));
        glVertexAttribIPointer(0, 4, GL_INT, stride,
            reinterpret_cast<const void*>(offsetof(CanvasInstance, rect)));
        glVertexAttribIPointer(1, 4, GL_INT, stride,
            reinterpret_cast<const void*>(offsetof(CanvasInstance, param)));
        glVertexAttribIPointer(2, 2, GL_UNSIGNED_INT, stride,
            reinterpret_cast<const void*>(offsetof(CanvasInstance, words)));
        for (GLuint loc = 0; loc < 3; ++loc) {
            glEnableVertexAttribArray(loc);
            glVertexAttribDivisor(loc, 1);
        }
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glGenTextures(1, &columns_texture_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, columns_texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        const GLshort seed[4] = {};
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16I, 1, 1, 0, GL_RGBA_INTEGER,
                     GL_SHORT, seed);
        columns_w_ = 1;
        columns_h_ = 1;
        // The seed is no upload: the first plate packs whatever its key.
        columns_uploaded_ = false;
        glActiveTexture(GL_TEXTURE0);
    }
    // THE PAGE PROGRAM (2026-10-27; kPageFragmentShader's head): the texture
    // triangle's vertex shader, its sampler on unit 2.
    if (!page_program_) {
        const GLuint prog =
            link_canvas_program(host_, kCanvasVertexShader, kPageFragmentShader);
        glUseProgram(prog);
        glUniform1i(glGetUniformLocation(prog, "u_page"), 2);
        page_height_uniform_ = glGetUniformLocation(prog, "u_height");
        page_rect_uniform_   = glGetUniformLocation(prog, "u_rect");
        page_field_uniform_  = glGetUniformLocation(prog, "u_field");
        page_program_ = prog;
    }
}

// THE CANVAS TEXTURE FOLLOWS THE BACKBUFFER: one GL_RGBA8 texture of its
// size, immutable storage, one level, NEAREST both ways (texelFetch reads no
// filter; the parameters keep the texture plainly complete), remade when the
// size moves. Its content is the backbuffer's as uploaded by damage rect
// (present_frame); a remade texture is filled by the full damage every
// adoption declares, the adoption being the one road a size moves by. Asked
// with the context current on a surface (the platform's backbuffer, after
// its surface is made: ensure_backbuffer, platform_android.cpp).
void GuiGlCanvas::ensure_texture(int w, int h) {
    if (texture_ && texture_w_ == w && texture_h_ == h)
        return;
    if (texture_) glDeleteTextures(1, &texture_);
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    texture_w_ = w;
    texture_h_ = h;
    // A remade texture holds nothing: its first upload walk sends every
    // damage rect whole, the canvas's clip included (present_frame's upload
    // rule).
    upload_seeded_ = false;
}

// THE FRAME (2026-10-13; the design record is
// docs/engineering/gpu_canvas_2026_10.md), between the platform's surface
// check and its swap (GuiPlatform::present, platform_android.cpp). It reads
// THE PLATFORM'S DAMAGE LIST ITSELF, the rects its paint pass just painted,
// and is that list's one upload walk.
//
// THE UPLOAD: every rect of the damage, clamped to the backbuffer, goes into
// the texture at its own place, sourced in place from the cairo surface
// (GL_UNPACK_ROW_LENGTH = cairo's stride in pixels, the pointer at the
// rect's first word; GL_UNPACK_ALIGNMENT 4, cairo's stride being a
// multiple of 4) as GL_RGBA bytes — cairo's B,G,R,A, swizzled by the
// shader. The texture therefore holds the backbuffer whole: every pixel
// the backbuffer changes is in a painted rect, and a failed swap keeps the
// damage for the next frame (the platform's paint pass keeps it), so the
// rect goes up again.
// LESS THE CANVAS (2026-10-13, the canvas sink, canvas_sink.h): each
// damage rect goes up MINUS the frame's canvas clip (the list's clip,
// the GUI's waveform_area ∩ waveform_paint_area — or the modal box's
// interior while the score viewer's page stands, 2026-10-27 — the platform
// computes no GUI geometry), split into up to four rects, because the
// backbuffer holds nothing there but zeros the texture already holds. THE WHOLE
// RECT GOES UP instead while the list's floater bit stands or stood at
// the last walk (a floating surface's pixels over the canvas, and the
// zeros it leaves when it goes, ride containing rects), when the clip
// differs from the last walk's (a keyboard or a folder overlay came or
// went, and texels it uploaded now lie inside the clip), and at the
// first walk on a remade texture (no texel is known yet). So every texel
// inside the clip is the backbuffer's: 0 where no overlay stands.
//
// THE PAGE TEXTURES (2026-10-27; gl_canvas.h's rule, reconcile_pages): after
// the walk, the request's wanted list reconciled — the textures it no longer
// names deleted, and while the page stands the first one not yet held
// uploaded — and the report's held serials written.
//
// THE DRAW: the canvas list first, then the texture over it. THE LIST
// (kListVertexShader's head; GuiCanvasPrimitive, canvas_sink.h): its
// records as instances in list order, one instanced draw, the scissor
// the canvas clip, UNDER THE BLEND (GL_BLEND enabled for this draw alone
// and disabled after, its function the fixed state's premultiplied
// source-over, bind_fixed_state): the plate's antialiased fringe blends
// with the ground and the grid drawn before it, and every other record,
// alpha 1, lands exactly as it would unblended (2026-10-30) — its
// instances re-made only when the list's generation moved, and the plate's
// bounds texture, inside that, only when the plate's key moved
// (upload_list). THE TEXTURE: one full-screen
// triangle over the surface's own viewport, the fragment shader placing
// each texel at its pixel and DISCARDING an alpha-0 one, so the canvas
// shows wherever the backbuffer's interior is clear and every other
// pixel is the backbuffer's (kCanvasFragmentShader). The frame is
// cleared first, opaque black: with discards a pixel neither draw writes
// would otherwise hold a recycled buffer's bytes, though by the
// invariant none exists (the list's first record is the canvas's ground,
// over the whole clip, and every texel outside it is opaque).
// WHILE THE SCORE VIEWER'S PAGE STANDS the clip is the box's interior and
// THE PAGE draws there in the list's stead (draw_page), under the same
// scissor: the field's word and the held page's gray bytes, the window's
// bytes by GuiCanvasPage's rule.
// THE PICTURE IS THE BACKBUFFER'S BYTES EXACTLY outside the clip and
// under every overlay: an 8-bit UNORM texel
// fetched without a filter round-trips to the same 8-bit value in the
// RGBA8 framebuffer, dithering is off, the texture's draw is unblended, and
// the framebuffer is plain RGBA (the passthrough colorspace encodes nothing
// — never an sRGB framebuffer, which is what makes the plate's blend the
// byte-space blend ruled); inside the clip it is the list's words, each a
// chrome pixel's bytes for its color by the sink's construction, and on the
// plate's fringe their coverage blend (kListFragmentShader).
bool GuiGlCanvas::present_frame(cairo_surface_t* back,
                                std::span<const GuiGlRect> damage,
                                GuiCanvasDrawList* list,
                                int surface_w, int surface_h) {
    if (!texture_) return false;
    const int stride_px = cairo_image_surface_get_stride(back) / 4;
    const auto* src = reinterpret_cast<const uint32_t*>(
        cairo_image_surface_get_data(back));
    if (!src) return false;

    // THE FRAME'S CANVAS in surface px (the GUI's own: no window frame since
    // 2026-10-24): an empty clip — no list yet, or a paint area
    // the folder overlay emptied — leaves nothing out of any upload and
    // draws no list.
    int  clip[4] = {0, 0, 0, 0};
    bool floater = false;
    if (list && list->clip.w > 0 && list->clip.h > 0) {
        clip[0] = list->clip.x;
        clip[1] = list->clip.y;
        clip[2] = list->clip.w;
        clip[3] = list->clip.h;
        floater = list->floater;
    }
    const bool clip_moved = !upload_seeded_ ||
                            clip[0] != upload_clip_[0] ||
                            clip[1] != upload_clip_[1] ||
                            clip[2] != upload_clip_[2] ||
                            clip[3] != upload_clip_[3];
    const bool whole = clip_moved || floater || upload_floater_ ||
                       clip[2] <= 0;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride_px);
    const auto upload = [&](int x0, int y0, int x1, int y1) {
        if (x1 <= x0 || y1 <= y0) return;
        glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, x1 - x0, y1 - y0,
                        GL_RGBA, GL_UNSIGNED_BYTE,
                        src + static_cast<size_t>(y0) * stride_px + x0);
    };
    for (const GuiGlRect& d : damage) {
        const int x0 = std::max(d.x, 0);
        const int y0 = std::max(d.y, 0);
        const int x1 = std::min(d.x + d.w, texture_w_);
        const int y1 = std::min(d.y + d.h, texture_h_);
        if (x1 <= x0 || y1 <= y0) continue;
        const int ix0 = std::max(x0, clip[0]);
        const int iy0 = std::max(y0, clip[1]);
        const int ix1 = std::min(x1, clip[0] + clip[2]);
        const int iy1 = std::min(y1, clip[1] + clip[3]);
        if (whole || ix1 <= ix0 || iy1 <= iy0) {
            upload(x0, y0, x1, y1);
            continue;
        }
        upload(x0, y0, x1, iy0);     // the rows above the clip
        upload(x0, iy1, x1, y1);     // the rows below it
        upload(x0, iy0, ix0, iy1);   // beside it, left
        upload(ix1, iy0, x1, iy1);   // beside it, right
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    for (int i = 0; i < 4; ++i) upload_clip_[i] = clip[i];
    upload_floater_ = floater;
    upload_seeded_  = true;

    if (list) reconcile_pages(list->page);

    glViewport(0, 0, surface_w, surface_h);
    // THE CLEAR IS OPAQUE BLACK.
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (list && clip[2] > 0 && list->page.stands) {
        draw_page(list->page, clip);
    } else if (list && clip[2] > 0) {
        if (!list_uploaded_ || list->generation != list_generation_)
            upload_list(*list);
        if (list_instances_ > 0) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(clip[0], texture_h_ - (clip[1] + clip[3]),
                      clip[2], clip[3]);
            glUseProgram(list_program_);
            glUniform2f(list_size_uniform_, static_cast<float>(surface_w),
                        static_cast<float>(surface_h));
            glUniform1i(list_height_uniform_, texture_h_);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, columns_texture_);
            glBindVertexArray(list_vao_);
            glEnable(GL_BLEND);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                                  list_instances_);
            glDisable(GL_BLEND);
            glBindVertexArray(0);
            glActiveTexture(GL_TEXTURE0);
            glDisable(GL_SCISSOR_TEST);
        }
    }

    glUseProgram(program_);
    glUniform1i(height_uniform_, texture_h_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    return true;
}

// THE CONTEXT'S END: the platform has destroyed the context, and every
// object made in it with it; the names are forgotten so a later context
// would build its own.
void GuiGlCanvas::forget() {
    program_   = 0;
    height_uniform_ = -1;
    texture_   = 0;
    texture_w_ = 0;
    texture_h_ = 0;
    list_program_        = 0;
    list_size_uniform_   = -1;
    list_height_uniform_ = -1;
    list_vao_            = 0;
    list_vbo_            = 0;
    list_instances_      = 0;
    list_uploaded_       = false;
    columns_texture_     = 0;
    columns_w_           = 0;
    columns_h_           = 0;
    columns_uploaded_    = false;
    columns_revision_    = 0;
    columns_spans_.clear();
    page_program_        = 0;
    page_height_uniform_ = -1;
    page_rect_uniform_   = -1;
    page_field_uniform_  = -1;
    pages_.clear();
    upload_seeded_       = false;
}

// THE PAGE TEXTURES' RECONCILIATION (2026-10-27; the rule at gl_canvas.h's
// PageTexture block): every texture whose serial the wanted list no longer
// names deleted; while the page stands, the wanted pages not yet held
// uploaded in the list's order — the feed's, the shown page first — at most
// kPageUploadsPerPresent of them, each one immutable R8 level of its
// size, NEAREST both ways (texelFetch reads no filter), its rows tight
// (GL_UNPACK_ALIGNMENT 1: a row is the page's width in bytes); then the
// report's held serials, ascending. A serial names one immutable buffer for
// the process's life (GuiScoreGrayPage::serial), so a held texture is never
// stale.
void GuiGlCanvas::reconcile_pages(GuiCanvasPage& page) {
    const auto wanted = [&](uint64_t serial) {
        return std::any_of(page.want.begin(), page.want.end(),
                           [&](const GuiCanvasPageTexture& w) {
                               return w.serial == serial;
                           });
    };
    const auto held = [&](uint64_t serial) {
        return std::any_of(pages_.begin(), pages_.end(),
                           [&](const PageTexture& t) {
                               return t.serial == serial;
                           });
    };
    std::erase_if(pages_, [&](const PageTexture& t) {
        if (wanted(t.serial)) return false;
        glDeleteTextures(1, &t.name);
        return true;
    });
    if (page.stands) {
        int budget = kPageUploadsPerPresent;
        for (const GuiCanvasPageTexture& w : page.want) {
            if (budget <= 0) break;
            if (held(w.serial) || !w.bytes || w.w <= 0 || w.h <= 0) continue;
            assert(w.bytes->size() ==
                   static_cast<size_t>(w.w) * static_cast<size_t>(w.h));
            PageTexture t;
            t.serial = w.serial;
            t.page   = w.page;
            t.w      = w.w;
            t.h      = w.h;
            glGenTextures(1, &t.name);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, t.name);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_R8, w.w, w.h);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w.w, w.h, GL_RED,
                            GL_UNSIGNED_BYTE, w.bytes->data());
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glActiveTexture(GL_TEXTURE0);
            pages_.push_back(t);
            --budget;
        }
    }
    page.held.clear();
    for (const PageTexture& t : pages_) page.held.push_back(t.serial);
    std::sort(page.held.begin(), page.held.end());
}

// THE PAGE'S DRAW (2026-10-27; GuiCanvasPage's rule, canvas_sink.h): the
// page on screen chosen — the field alone for a shown page whose decode
// failed (it is the page on screen); else the shown page's texture where it
// is held; else the last painted page's where it is held; else the field
// alone — placed in the clip, the box's interior, by gui_page_place, and
// drawn by the page program under the scissor to that interior. The report
// takes the session and the page now on screen (-1: none).
void GuiGlCanvas::draw_page(GuiCanvasPage& page, const int clip[4]) {
    const auto find = [&](uint64_t serial) -> const PageTexture* {
        if (serial == 0) return nullptr;
        for (const PageTexture& t : pages_)
            if (t.serial == serial) return &t;
        return nullptr;
    };
    const PageTexture* t = nullptr;
    int on_screen = -1;
    if (page.shown_failed) {
        on_screen = page.shown_page;
    } else if ((t = find(page.shown_serial)) != nullptr) {
        on_screen = page.shown_page;
    } else if ((t = find(page.last_serial)) != nullptr) {
        on_screen = page.last_page;
    }
    GuiRect at{0, 0, 0, 0};
    if (t != nullptr)
        at = gui_page_place(GuiRect{clip[0], clip[1], clip[2], clip[3]}, t->w,
                            t->h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(clip[0], texture_h_ - (clip[1] + clip[3]), clip[2], clip[3]);
    glUseProgram(page_program_);
    glUniform1i(page_height_uniform_, texture_h_);
    glUniform4i(page_rect_uniform_, at.x, at.y, at.w, at.h);
    glUniform1ui(page_field_uniform_, page.field);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, t != nullptr ? t->name : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_SCISSOR_TEST);
    page.drawn_session = page.session;
    page.drawn_page    = on_screen;
}

// THE LIST'S UPLOAD (2026-10-13; present_frame's draw): every record one
// instance in list order, its coordinates the surface's px as the GUI wrote
// them (a rect, a run's origin, a plate's row 0 alike), the instance buffer rewritten
// whole — asked only when the list's generation moved, so at every rebuild of
// the records (the scanner's and the cursor's among them; the buffer is small).
// A plate record's `period` is re-pointed at its row of THE PLATE'S BOUNDS
// TEXTURE — ONE ROW PER PLATE RECORD (2026-10-30), the record's columns left
// to right, each texel the column's four quarter-row tips {o_top, o_bot,
// i_top, i_bot} as render_waveform_columns gathered them (GL_RGBA16I: a
// plate row times four of a canvas far under 8191 rows; the absent inner is
// 0, 0), zero past a record's width; rows = max(1, the plate records).
// THE BOUNDS TEXTURE HAS ITS OWN LIFETIME (2026-10-14): it is packed and sent
// only when its KEY moved — the list's plate revision, the plate records'
// spans in list order (each its offset into the list's columns and its
// width, so their count too), and the dimensions those spans give it — or
// when the texture holds no upload yet (the seed of ensure_programs, a
// context remade after forget()); remade at a size change, else rewritten
// whole. THE KEY IS COMPLETE: every texel is a function of the spans, the
// dimensions and the list's columns at those spans, and the list's columns
// are a function of the revision alone — the sink copies the live
// plate's stored bands into them once per revision, and the live plate's
// bounds change only at a publication, which takes a new revision
// (GuiPlateColumns::revision, render.h; GuiCanvasSink::plate). A list
// with no plate record leaves the texture and its key as they are: nothing
// samples it, and its texels still answer the key.
void GuiGlCanvas::upload_list(const GuiCanvasDrawList& list) {
    static std::vector<CanvasInstance>      instances;
    static std::vector<GLshort>             texels;
    static std::vector<std::pair<int, int>> spans;
    spans.clear();
    int max_w = 1;
    for (const GuiCanvasPrimitive& p : list.prims) {
        if (p.kind != GuiCanvasKind::Plate) continue;
        spans.emplace_back(p.period, p.r.w);
        max_w = std::max(max_w, p.r.w);
    }
    const int plates = static_cast<int>(spans.size());
    const int rows   = std::max(1, plates);
    const bool pack =
        plates > 0 &&
        !(columns_uploaded_ && list.plate_revision == columns_revision_ &&
          spans == columns_spans_ && max_w == columns_w_ &&
          rows == columns_h_);
    if (pack)
        texels.assign(static_cast<size_t>(max_w) * rows * 4, 0);
    const auto texel = [&](int row, int col) {
        return &texels[(static_cast<size_t>(row) * max_w + col) * 4];
    };
    const auto clamp16 = [](int v) {
        assert(v >= -32768 && v <= 32767);
        return static_cast<GLshort>(v);
    };
    instances.clear();
    int row = 0;
    for (const GuiCanvasPrimitive& p : list.prims) {
        CanvasInstance in{};
        in.rect[0]  = p.r.x;
        in.rect[1]  = p.r.y;
        in.rect[2]  = p.r.w;
        in.rect[3]  = p.r.h;
        in.param[0] = static_cast<GLint>(p.kind);
        in.param[1] = p.period;
        in.param[2] = p.origin;
        in.param[3] = p.phase;
        in.words[0] = p.word;
        in.words[1] = p.word2;
        switch (p.kind) {
        case GuiCanvasKind::Rect:
        case GuiCanvasKind::DotsColumn:
        case GuiCanvasKind::DotsRow:
            break;
        case GuiCanvasKind::Plate:
            if (pack) {
                for (int j = 0; j < p.r.w; ++j) {
                    const WaveformColumnRows& c =
                        list.columns[static_cast<size_t>(p.period + j)];
                    GLshort* a = texel(row, j);
                    a[0] = clamp16(c.o_top);
                    a[1] = clamp16(c.o_bot);
                    a[2] = clamp16(c.i_top);
                    a[3] = clamp16(c.i_bot);
                }
            }
            in.param[1] = row;
            row += 1;
            break;
        }
        instances.push_back(in);
    }
    if (!instances.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, list_vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(instances.size() *
                                             sizeof(CanvasInstance)),
                     instances.data(), GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    if (pack) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, columns_texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        if (max_w != columns_w_ || rows != columns_h_) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16I, max_w, rows, 0,
                         GL_RGBA_INTEGER, GL_SHORT, texels.data());
            columns_w_ = max_w;
            columns_h_ = rows;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, max_w, rows,
                            GL_RGBA_INTEGER, GL_SHORT, texels.data());
        }
        glActiveTexture(GL_TEXTURE0);
        columns_uploaded_ = true;
        columns_revision_ = list.plate_revision;
        columns_spans_    = spans;
    }
    list_instances_  = static_cast<int>(instances.size());
    list_generation_ = list.generation;
    list_uploaded_   = true;
}
