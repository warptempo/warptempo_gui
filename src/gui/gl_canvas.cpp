#include "gl_canvas.h"

#include "canvas_sink.h"   // GuiCanvasDrawList, GuiCanvasPrimitive
#include "device_config.h" // kWaveformGrowMaxQuarters, the key's lattice (the plate's asserts)

#include <GLES3/gl3.h>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
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
// §3.13), THE WAVEFORM'S ANTIALIASING (architect 2026-10-30): the plate's
// fragment rasterizes the bars' tips at exact coverage — the bars stretched
// at their pitch from a fourth, float instance attribute (architect
// 2026-11-14, send G), and in the fade band before a level switch merged
// pair by pair by that attribute's weight (send H) — and blends
// source-over (kListFragmentShader's head; the rule at
// render_waveform_columns' head, render.h), and THE TINT (architect
// 2026-11-12, the sweep highlight), the canvas's second blend, a rect
// blended source-over toward its word under the same blend function — A
// RECT WITH REAL EDGES (architect 2026-11-14, send J), each pixel's alpha
// scaled by the exact area of it the rect covers, the canvas's lines at
// their exact places its records at 100 percent. And THE FRAME TARGET
// (architect 2026-11-15, "Send L."): every pass above composes into the
// canvas's own offscreen target over this frame's change alone, and each
// window buffer is brought up to date by a copy out of it, the window's
// bytes the target's, exactly (present_frame's THE FRAME TARGET). And THE
// AHEAD TARGET (send M, 2026-11-15): during a paging play the next page's
// own picture is composed into a second target a slice a frame, and a frame
// whose leading records equal it copies it in their place, the plain road's
// bytes (present_frame's THE AHEAD TARGET and THE ADOPTION).

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

// THE COPY (architect 2026-11-15, send L; the rule at present_frame's THE
// FRAME TARGET): with kCanvasVertexShader's triangle, the frame target's
// texel at the fragment's own integer pixel, written as it is stored — all
// four channels, no row flip (the target and the window are both the
// framebuffer's own lower-left coordinates), no swizzle, no discard. The
// sampler is highp on unit 3 and the target is RGBA8, so the fetched value
// is the 8-bit UNORM texel exactly and k/255 written to the window's RGBA8
// buffer, unblended and undithered, converts back to k
// (kCanvasFragmentShader's argument).
constexpr const char* kCopyFragmentShader =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "uniform highp sampler2D u_frame;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    o_color = texelFetch(u_frame, ivec2(gl_FragCoord.xy), 0);\n"
    "}\n";

// THE CANVAS LIST'S PROGRAM (2026-10-13; the records at GuiCanvasPrimitive,
// canvas_sink.h; the draw at present_frame). ONE INSTANCE A RECORD, in list
// order, one instanced draw of a four-vertex strip: the instance's rect, in
// surface px with row 0 the top, is the quad (its corners from gl_VertexID),
// and the fragment shader takes the record's integer rule at the fragment's own
// pixel — the column int(gl_FragCoord.x), the row H − 1 − int(gl_FragCoord.y)
// (u_height the surface's height, H) — every comparison in highp int. A pixel
// outside the instance's rect is discarded (the quad's float edges never
// decide coverage); a RECT (kind 0) writes its word; THE TINT (kind 2,
// architect 2026-11-12, the sweep highlight; its real edges architect
// 2026-11-14, send J, the canvas's lines at their exact places) writes its
// word premultiplied by its alpha a with alpha a, (word · a, a), so the
// blend glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA) lays G · (1 − a) +
// word · a on the pixel beneath, G — whatever the earlier records drew
// there, the plate's ink and core included — rounded once to a byte, the
// canvas's second blend and byte space like the first (the framebuffer is
// plain RGBA, never sRGB). THE TINT'S ALPHA IS ITS PERCENT TIMES THE
// PIXEL'S EXACT COVERAGE: the record's rect is the cells its real rect
// touches and the fourth instance attribute's floats are that real rect's
// edges e = {x0, y0, x1, y1} in device px from the rect's corner
// (GuiCanvasSink::blended_rect, canvas_sink.cpp), and at the pixel (px,
// py), every operation one highp float operation in this order,
//     lx = float(px − rect.x), ly = float(py − rect.y)          (exact)
//     cx = clamp(min(lx + 1, e.z) − max(lx, e.x), 0, 1)
//     cy = clamp(min(ly + 1, e.w) − max(ly, e.y), 0, 1)
//     a  = (float(word2) / 100) · (cx · cy)
// — cx and cy the lengths of the pixel's column and row inside the edges,
// their product the area. WHOLE EDGES GIVE cx = cy = 1 EXACTLY on every
// pixel of the rect, so a tint on whole cells is word2 / 100 itself on
// every one of them, and at 100 percent the word lands as an opaque rect's
// does (src · 1 + dst · 0); THE PLATE (kind 1) is
// THE WAVEFORM'S ANTIALIASING (architect 2026-10-30;
// the rule at render_waveform_columns' head, render.h; the grow's key
// 2026-10-31, the 4x grid ruled for good that afternoon at render.h's
// kWaveformSubcells; THE CORE a solid fill of the grown inner since
// 2026-11-04; THE BARS ANCHORED TO THE AUDIO AND DRAWN STRETCHED at their
// pitch, architect 2026-11-14, send G, render.h's THE PLATE'S BARS; THE
// FADE BAND'S ROW DRAWN MERGED, send H, the merged arm below), an
// INTEGER ALGORITHM over the bounds texture
// (an isampler2D over GL_RGBA16I: row `period` holds, per BAR of the
// record's row, the four QUARTER-ROW tips {o_top, o_bot, i_top, i_bot}, the
// inner absent where i_bot <= i_top) at the plate's row y (the pixel's row less
// `origin`), in SUBROWS (quarter rows, kWaveformSubcells per row) and
// SCREEN SUBCOLUMNS (four per device px, the pixel's own 4 x 4 subcells its
// coverage; only a bar's place among them is float), THE GROW A CONSTANT OF
// THE SOURCE — GROW, the `waveform_grow`
// device key's quarters of a device px, one subcell each, 0 .. 8
// (kWaveformGrowMaxQuarters; the key's seat is render.h's
// waveform_grow_quarters, the list carries it as
// GuiCanvasDrawList::plate_grow):
//   * THE SPECIALIZATION RULE (2026-10-31 afternoon): the grow is baked into
//     the shader's text as `#define GROW <r>` (list_fragment_source: the
//     literal's one substitution point) and THE PROGRAM IS REBUILT WHEN THE
//     KEY CHANGES — ensure_programs builds it at the default grow,
//     present_frame compares the list's plate_grow with the grow the
//     standing program was compiled for and rebuilds before the draw
//     (build_list_program), once per commit of the key, tens of ms, no
//     frame-to-frame cost. WHY: every loop bound below is then a constant
//     expression (GROW, 4, NO = 4 + 2 · GROW, NU, NM), every disc condition a
//     comparison of constants and every array index a loop variable, so
//     after unrolling no array is indexed by a runtime value and the masks
//     are registers. A BAR IS A SCALAR FOR THE SAME REASON (architect
//     2026-11-14, send G): which bar a subcolumn reads is a runtime value —
//     it follows the draw transform — so a bar is an ivec4 fetched at a
//     runtime TEXTURE coordinate into a scalar, never an element of a local
//     array. The same algorithm with the grow as a uniform (the
//     2026-10-31 morning road, 19657fb7, on the 8x grid: runtime bounds, a
//     while that walked g(j), a continue guard over arrays of 56 and 24
//     wide words) spilled the masks to scratch memory and measured ~300 ms
//     of GPU time a plate frame on the tablet against the 11 ms budget of
//     the panel then pinned at 90 Hz; this
//     road replaced it the same day, and the grid went back to 4x that
//     afternoon (the measurements at kWaveformSubcells, render.h: the
//     specialized 8x shader still cost 16 .. 110 ms a frame).
//   * THE WINDOW is ONE WORD of 32 bits, a uint: bit b the subrow 4y −
//     PIX0 + b, the pixel's own subrows 4y .. 4y + 3 bits 14 .. 17 (PIX0 =
//     14). Every mask is honest where every bit it was computed from is:
//     the base masks on all 32 bits (range_bits clips a run to the word),
//     the dilated ones (grown by up to GROW) on bits GROW .. 31 − GROW — at
//     the key's top grow, 8, bits 8 .. 23, which hold the pixel's four with
//     six to spare. THE WINDOW HOLDS THE GROW'S 2 PX ALONE (2026-11-04: the
//     core takes the grown inner whole, so no erosion reads past it; 4 + 2 ·
//     8 = 20 <= 32, one word at every grow of the key; the static_asserts
//     after the source). The shifts are by one (grow1), every grow a count
//     of them. The argument reads nothing of where a base came from: a base
//     is a union of runs range_bits clipped to the word, whichever bars and
//     weights gave the runs.
//   * THE BARS AND A SUBCOLUMN'S BRACKETS (architect 2026-11-14, send G;
//     render.h's THE PLATE'S BARS owns the geometry): the record's rect is
//     the band's DEVICE-PX rect and its row holds BARS — the first n =
//     v_bars.z texels, bar j's centre X_j = x0 + j · p device px from the
//     rect's left edge (THE DRAW TRANSFORM: x0 = v_bars.x, the first bar's
//     centre, and p = v_bars.y, the pitch, the fourth instance attribute's
//     floats). THE ROW'S WEIGHT is that attribute's fourth float, v_bars.w
//     (architect 2026-11-14, send H; render.h's THE FADE BAND), AND IT PICKS
//     THE ARM. Exactly 1 is A UNIFORM ROW, drawn by THE UNIFORM ARM: this
//     paragraph and every one below through THE COST are that arm's, each
//     claim said of a uniform row, WHOSE BARS STAND A PX OR MORE APART
//     (architect 2026-11-14, send I): the switch width is a fixed 2 px, so a
//     rung's own pitch p is 1 or more — up to 2, and past it under the
//     ladder's floor (render.h's waveform_bar_frames) — no row under a px
//     has a producer, and the arm's bounds are proved for p >= 1 alone (the
//     static_assert after the source). Under 1 the row is the fade band's
//     and THE MERGED ARM draws it (its paragraph after THE COST).
//     THE INSTANCE'S RECIPROCALS (architect 2026-11-14, send I): NO FRAGMENT
//     DIVIDES TO PLACE A SUBCOLUMN. The vertex shader takes the divisions a
//     row needs from the instance's own floats and hands them on flat,
//     v_inv: for a uniform row {1 / p, 1, 1}; for the fade band's row
//     {1 / P, 1 / (2h), 1 / (1 − 2h)}, P = 2p the pairs' pitch and h = w / 4
//     — THE MERGED ARM's invP, invI and invO, the second 1 where 2h is 0
//     (the band's start has no gap inside a pair, nothing reads it there,
//     and no 1 / 0 is formed), the products by 2 and by 0.25 exact; zero on
//     every record but a plate's — a rect's floats are zero and a tint's
//     are its edges, and no reciprocal of either is evaluated.
//     THE PICTURE IS MADE IN SCREEN SUBCOLUMNS: s = 4i +
//     m of the pixel i = px − rect.x (m = 0 .. 3; a neighbor's s is
//     negative or past the rect at the plate's sides), its centre xs = (s +
//     0.5) / 4 px. ITS PLACE AMONG THE BARS is u = (xs − x0) · (1 / p),
//     EVALUATED IN HIGHP FLOAT AS ((float(s) + 0.5) * 0.25 − x0) * v_inv.x —
//     the centre's product exact, then ONE subtraction and ONE multiply by
//     the instance's reciprocal, each rounded once (bar_u, the one
//     expression every site calls: the two terms are never scaled apart and
//     u is never stepped by addition) — with j0 = floor(u) and f = u − j0
//     in [0, 1] (that subtraction is exact but where −1 < u < 0, left of the
//     first bar's centre, where it is rounded once and a u a hair under
//     zero gives 1): THE BRACKETS are bars j0 and j0 + 1, the two whose
//     centres stand either side of the subcolumn's. A
//     BAR EXISTS iff 0 <= j < n, and in the inner family iff its i_bot >
//     i_top; bar_at answers the zero texel for a bar outside the row — an
//     existing outer always has bot > top (the never-fade floor), so zero is
//     "absent" in each family's own test — and clamps its coordinate, so no
//     texel outside [0, n) is ever fetched (the row's allocation is a
//     capacity, upload_list). Left of bar 0 nothing exists; past the song's
//     end the bars exist and are silent (the producer's floor bars), and the
//     row ends at the last bar the transform can ask at any grow.
//     NO SITE NEEDS ANOTHER'S u TO THE ULP, AND THE ROW'S TWO ENDS NEED NO
//     GUARD BAR — ONE PROPERTY: the base below is continuous across a bar's
//     centre. As f reaches 0 or 1 the contour is that bar's own tips and its
//     core covers, whichever of the two brackets it is called, so a u that
//     floors one bar off (a float rounding at a whole u) names the same
//     base. At the row's first or last bar the bracket it then asks beyond
//     the row is absent, and against an absent farther bracket the nearer
//     bar stands flat — the same tips. Between sites (the support's bars,
//     the pixel's own subcolumns, the walk) a floor that differed would
//     cost only a certificate: a lower bracket outside {jF, jF + 1}
//     certifies nothing, and the walk's pair, seeded at jA, serves such a
//     subcolumn through its right bar (f at 1, the left's weight nothing)
//     and rolls onto the true brackets at its next step.
//   * THE BASE of a subcolumn for one family (base_bits; a and b its
//     brackets j0 and j0 + 1, T and B a bar's top and bottom tips): THE
//     CONTOUR UNION THE CORES.
//     THE CONTOUR joins the two bars' centres. THE NEARER bracket is a where
//     f < 0.5, else b (a tie, f == 0.5, is b's). The nearer absent: no
//     contour. The nearer existing and the farther absent: the nearer's own
//     subrows [T, B − 1], flat. Both existing: the top Tc = (1 − f) · T_a +
//     f · T_b, the bottom Bc likewise, and subrow k is in it iff Tc <= k +
//     0.5 <= Bc. f IS QUANTIZED ONCE, fq = floor(f · 256 + 0.5) in [0, 256],
//     and everything vertical is integer: t256 = (256 − fq) · T_a + fq ·
//     T_b, b256 likewise, k in the contour iff t256 <= 256k + 128 <= b256,
//     i.e. (t256 + 127) / 256 <= k <= (b256 − 128) / 256 — both operands
//     non-negative (T >= 0, B >= 1 for an existing bar), so / is floor. The
//     flat case is the same sums over the nearer bar twice, 256 T and 256 B:
//     T <= k <= B − 1. THE CONTOUR OF TWO EXISTING BARS IS NEVER EMPTY AND
//     LIES BETWEEN THEM (the fast paths' two facts): t256 and b256 are
//     convex sums, so its first subrow is at least min(T_a, T_b) and at most
//     max(T_a, T_b), its last at least min(B_a, B_b) − 1 and at most
//     max(B_a, B_b) − 1; and b256 − t256 >= 256 (each bar has B − T >= 1)
//     leaves a subrow between them.
//     THE CORE is a bar's own full-height line, ONE DEVICE PX wide and
//     centred on the bar's centre (architect 2026-11-14, send G): a bracket
//     that exists covers the subcolumn iff −0.5 <= xs − X_j < 0.5,
//     half-open — a iff f · p < 0.5, b iff (1 − f) · p <= 0.5, on the
//     unquantized f, in float — and a covering bar gives its own subrows
//     [T, B − 1]. BOTH brackets' cores join where both cover — a gap under
//     a px, which a uniform row never has and the fade band's row does — and
//     in a uniform row no third bar's core reaches a subcolumn (the bars a
//     px or more apart). So a lone spike stands
//     flat-topped at its own height under its core at every pitch and
//     offset: the contour, which is sampled at the subcolumns' centres and
//     never at the bar's, cannot shave it.
//     AT p = 1 ON A WHOLE OFFSET (x0 ≡ 1/2 mod 1, a landing on a whole
//     level: every bar exactly on a pixel) THE BASES ARE ONE BAR A PIXEL'S,
//     BIT FOR BIT — the own bar whole in its pixel's four subcolumns and the
//     contour to the nearer neighbor in eighths, own : neighbor 5:3, 7:1,
//     7:1, 5:3, subrow k in it iff (t8 + 3) / 8 <= k <= (b8 − 4) / 8, the
//     plate's silhouette of 2026-10-30, so a landing keeps that picture's
//     bytes. xs − x0 is then a multiple of 1/8, exact in float, THE
//     INSTANCE'S RECIPROCAL IS EXACTLY 1 (1 / p at p = 1) and the multiply
//     returns its operand, so u is that difference itself:
//     f = 5/8, 7/8, 1/8, 3/8 for m = 0 .. 3 and fq = 160, 224, 32, 96. The
//     nearer bracket is the pixel's own bar (b for m = 0, 1; a for m = 2,
//     3), and its core alone covers ((1 − f) · p = 3/8, 1/8 <= 1/2 and f · p
//     = 1/8, 3/8 < 1/2, the farther's product 5/8 or 7/8). With t8 the
//     eighths' sum, t256 = 32 · t8: (32 t8 + 127) / 256 and (t8 + 3) / 8 =
//     (32 t8 + 96) / 256 step at the same t8 (32 t8 mod 256 is a multiple of
//     32, and none lies in 129 .. 159), and (32 b8 − 128) / 256 is (b8 − 4)
//     / 8 outright; an absent neighbor is the own bar flat in both rules,
//     an absent own bar nothing in both.
//     The bases live in TWO ARRAYS SIZED TO THE REACH, the outer's bo and
//     the inner's bi, each over 4i − GROW .. 4i + 3 + GROW (NO = 4 + 2 ·
//     GROW words, subcolumn 4i − GROW + t at index t); every word of a
//     family that is dilated is written, and a dilation's reads stay inside.
//   * THE DILATION (the disc of radius GROW subcells, dilate_o / dilate_i):
//     at a subcolumn, the union over dx in [−GROW, GROW] of the base at s +
//     dx grown vertically by g(|dx|) = floor(sqrt(GROW² − dx²)) rows (the
//     disc's column at dx; g nonincreasing in |dx|, g(0) = GROW, g(GROW) = 0
//     for GROW >= 1). TAKEN AS A HORNER SCHEME, exact and O(GROW) shifts:
//     with H_j the union of the two bases at s − j and s + j, the expansion
//     is ∪_j grow_{g(j)}(H_j), since grow_a ∘ grow_b = grow_{a+b} for the
//     vertical interval and grow distributes over ∪, and the scheme joins
//     each H_j once and grows the accumulator once per row: INDEXED BY THE
//     LEVEL v from GROW down to 0, at each level the H_j with g(j) = v
//     (v² + j² <= GROW² < (v + 1)² + j², both constants once unrolled) join,
//     then the accumulator grows by one for v > 0 — H_j is joined after
//     GROW − g(j) grows and grown g(j) times after, the j-walk's own order
//     (A_j = grow_{g(j−1) − g(j)}(A_{j−1}) ∪ H_j) with its g(j) never
//     computed. At GROW 2 the scheme is grow2(c) | grow1(l1 |
//     r1) | l2 | r2, the 2026-10-30 shader's dilate. Each bar's dilation is
//     taken at the pixel's four subcolumns alone and folded into the count
//     at once, never kept.
//   * THE COVERAGE per pixel, in sixteenths (pop16 over the four
//     subcolumns' pixel nibbles packed into one 16-bit word — bitCount is
//     GLSL ES 3.10's, not 3.00's): n_o = |(outer ∪ inner) ∩ pixel|, the
//     grown bars' union, and n_i = |inner ∩ pixel|, THE GROWN INNER's.
//   * THE COMPOSITE, THE CORE OVER THE INK (architect 2026-11-04, mock f1:
//     the inner a solid `waveform_core` fill over the outer's
//     `waveform_ink`), source-over in byte space, THE AREA-CORRECT
//     COMPOSITE OF THE TWO OPAQUE SHAPES on the subcell grid (architect
//     2026-11-07, session 83): the ink over the union less the inner, the
//     core over the inner, the alpha the union's coverage — wi ⊆ wo by the
//     OR, so the fragment writes the PREMULTIPLIED source
//     ink · (cov_o − cov_i) + core · cov_i  with alpha cov_o, and the blend
//     glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA) (present_frame) lays it on
//     the pixel beneath, G: the result is G · (1 − cov_o) + ink · (cov_o −
//     cov_i) + core · cov_i, rounded once to a byte. THE TWO ARMS: that
//     expression where the inner reaches a pixel the union does not fill
//     (n_i > 0, n_o < 16); elsewhere the former nested expression, ink ·
//     cov_o · (1 − cov_i) + core · cov_i with alpha 1 − (1 − cov_o)(1 −
//     cov_i), KEPT LITERALLY — there it is algebraically the same (cov_i = 0
//     or cov_o = 1), and the former source is retained literally to preserve
//     its existing evaluation; the bytes outside the fringe were verified
//     unchanged by two-build tablet captures (Default and Immaterial,
//     2026-11-07, session 84). The nested form counts the inner's subcells
//     twice where 0 < cov_i and cov_o < 1: it differs from the exact result
//     by cov_i · (1 − cov_o) · (ink − G) per channel, at most |ink − G| / 4
//     (cov_i ≤ cov_o) — a dark fringe at the core's edges of up to 8 levels
//     on Default's canvas (393939 − 191919 = 32, over 4) before byte
//     rounding, hence the first arm. Where a bar covers whole
//     (cov_o = 1, cov_i 0 or 1) the factors are exactly 1 and 0 and the
//     word lands exactly — the ink, or the core; where nothing covers (n_o
//     = 0) the fragment discards and the plate stays transparent there.
//     With the MAGNIFY lamp dark no inner is present (n_i = 0), and the
//     picture is the grown outer in the ink alone.
//   * THE EXACT FAST PATHS (2026-10-31 night: smooth coarse zoom and pan
//     with the magnify lamp lit outranks everything but the picture's
//     bytes, which these paths leave untouched — each is a SUFFICIENT
//     certificate, a provable subset of the rule above, every pixel's word
//     the rule's byte for byte at every grow, pitch and offset; the
//     certificates are send G's, architect 2026-11-14: a bar stands in a
//     pixel's subcolumns only where its core or the contour puts it, so
//     neither lemma leans on "the own bar").
//     THE SUPPORT'S BARS: the dilation at the pixel's four subcolumns reads
//     the bases of 4i − GROW .. 4i + 3 + GROW, NO of them, and each base
//     its two brackets, so every bar the pixel's word depends on lies in jA
//     .. jB — jA the first subcolumn's j0, jB the last's j0 + 1 (u grows
//     with s: bar_u's subtraction and its multiply by a positive reciprocal
//     each keep the order). THEY ARE AT MOST NU = (NO − 1) / 4 + 3
//     (architect 2026-11-14, send I): the support's centres span (NO − 1) /
//     4 px, so at a px or more a bar the last u exceeds the first by (NO −
//     1) / (4p) <= (NO − 1) / 4, the floors of two numbers d apart differ
//     by at most floor(d) + 1, so with both lower brackets and the last
//     one's upper bracket the support's bars are at most floor((NO − 1) /
//     4) + 3; NO − 1 = 3 + 2 · GROW is odd, so (NO − 1) / 4 is a quarter or
//     three quarters off a whole number — a quarter bar of slack, far past
//     any float's rounding of the subtraction and the multiply. ONE
//     BRANCH-FREE LOOP FETCHES THEM, bar min(jA + d, jB) for d < NU, into
//     scalars — a repeat of jB changes no hull — and keeps the three from
//     jF, the pixel's first subcolumn's j0, for the hold lemma (own0 ..
//     own2; one the loop does not reach stays the zero texel, absent), and
//     its first two, jA and jA + 1 (NU >= 3 at every grow), for the roads'
//     walk.
//     THE REACH LEMMA: every base lies within the hull of its two brackets
//     (THE BASE: the contour between them, a core within its own bar), a
//     dilation grows it by at most GROW subrows and the window only ever
//     drops bits, so where the pixel's subrows lie more than GROW subrows
//     outside the hull of the existing outer bars of jA .. jB (reach_o
//     false) no dilated outer bit is at the pixel, and outside the hull of
//     their present inners (reach_i false; the hull empty, so reach_i
//     false, where none is present) no dilated inner bit is, so n_i = 0.
//     THE HOLD LEMMA: a bar HOLDS the pixel where it exists and its subrows
//     grown by GROW contain the pixel's four, T − GROW <= 4y and 4y + 3 <=
//     B − 1 + GROW (holds). One of the pixel's own subcolumns is CERTAINLY
//     FULL — its four pixel bits set after the dilation, whose dx = 0
//     column stands g(0) = GROW rows over the subcolumn's own base — where
//     (i) both its brackets hold: the contour is a non-empty run from at
//     most max(T) to at least min(B) − 1 (THE BASE's two facts), so its
//     grow contains the pixel's four; or (ii) a bracket that holds is the
//     nearer with the farther absent: the contour is that bar, flat; or
//     (iii) a bracket that holds covers the subcolumn with its core: the
//     base contains the bar (sub_full). THE PIXEL IS FULL in a family where
//     all four of its subcolumns are. Their brackets are among the three
//     bars from jF: the four subcolumns span 3/4 px, 3 / (4p) <= 3/4 of a
//     bar, so j0 steps once at most, a quarter bar clear of any rounding
//     (the certificates still refuse a lower bracket outside {jF, jF + 1}:
//     it certifies nothing and takes the roads). At p = 1 on a whole
//     offset the own bar's core covers all four subcolumns and its neighbor
//     is each one's other bracket, so the pixel is full exactly where the
//     own bar holds it. In the arm's order:
//     (a) DISCARD when neither reaches: n_o = 0.
//     (b) THE OPAQUE INTERSECTION (architect 2026-11-14, send I), BY THE
//     OWN BARS ALONE, before any own subcolumn is placed: where the inner
//     of all three bars from jF holds the pixel, the core's word; where no
//     inner reaches and the outer of all three holds it, the ink's word.
//     Every own subcolumn's two brackets are among those three bars, so
//     where all three hold, both brackets of each subcolumn hold and the
//     pixel is full in that family by (i) — the words are (c)'s and (e)'s.
//     A slot the support's loop did not write is the zero texel, absent: it
//     fails the test, never passes it. The interior of a solid passage ends
//     here; a pixel it does not catch goes on to (c).
//     THE OWN SUBCOLUMNS' CERTIFICATES ARE REACH-GATED (architect
//     2026-11-14, send I): a family that does not reach the pixel cannot be
//     full there — a bar that holds the pixel is in the hull — so each
//     family's flag starts at its reach and its sub_full is evaluated only
//     while the flag stands.
//     (c) THE FULL INNER, the core opaque: the pixel full in the inner
//     family, so n_i = n_o = 16 (the union holds the inner) and the
//     composite is the core's word, whether or not an outer bar holds the
//     pixel (a bar wholly on one side of zero has its outer pushed outward
//     with a gap, render.h's "THE OUTER IS A DILATION ABOUT THE CENTRE
//     ROW").
//     (d) THE FULL OUTER: the pixel full in the outer family, wo = 0xFFFF
//     with no base or dilation computed.
//     (e) THE INK, opaque: the full outer with reach_i false, cov_o = 1 by
//     (d) and cov_i = 0 by the reach lemma. (A pixel the rule fills another
//     way — a steep contour between two bars that do not both hold it, a
//     neighbor subcolumn's disc — takes the roads to the same word: the
//     certificates are sufficient, not complete.)
//     (f) THE ROADS past them: reach_i false, the outer's bases and
//     dilation alone, wi = 0 (the reach lemma: the inner's word would be
//     zero); reach_o false, the inner alone with wo starting at 0 (the
//     lemma for the outer: the outer's word would be zero, and full_o
//     implies reach_o); both
//     reaching, both, the outer's work replaced by (d) where the full outer
//     holds. Every road ends in the one composite, and the
//     word written opaque is the composite's own result at cov_o = 1 and
//     cov_i 0 or 1, the former expression's arm (x · 1.0 = x, x · 0.0 = 0,
//     x + 0.0 = x and 1 − 0 · 1 = 1 in IEEE). A road is a branch on
//     reach_o, reach_i and full_o, uniform across a run of pixels in
//     practice but no constant of the source: the specialization rule holds
//     inside every road.
//   THE COST, a fragment of a uniform row, by where it ends (architect
//   2026-11-14, send I; neither arm divides by a row's float; a place is one
//   bar_u, in the merged arm one pair_bracket): AT THE DISCARD OR THE OPAQUE
//   INTERSECTION, the support's bars — NU fetches in the one branch-free
//   loop, the hulls, the three own bars and the walk's two seeds out of it —
//   and 3 places (jA, jB and jF); AT AN OPAQUE WORD BY THE CERTIFICATES, the
//   four own subcolumns' places beside, 7 in all, and the gated sub_fulls;
//   ON A ROAD, past the fast paths, the walk's NO places beside the 7, in
//   ONE WALK OF THE SUPPORT'S NO SUBCOLUMNS FOR BOTH FAMILIES (a
//   texel carries both), its brackets A ROLLING PAIR of scalars — between
//   two subcolumns u moves 1 / (4p) <= 1/4 of a bar, so j0 keeps or steps
//   by one, and on a step the right bar becomes the left and one bar is
//   fetched: THE PAIR STARTS FROM THE SUPPORT'S LOOP, its first two bars
//   kept in the pair's own scalars (jA and jA + 1, the first subcolumn's
//   brackets), so the walk fetches one bar a step and nothing else — NU
//   fetches a fragment, and on a road the steps of j0 across the support
//   more, floor((NO − 1) / (4p)) or one over it — then the dilations of the
//   family or families that reach. No erosion. At GROW 4 (NO 12): 3, 7 and
//   19 places, NU 5.
//   Whether the frame holds its 16.7 ms (the 60 Hz pin) on the tablet is
//   measured there (SurfaceFlinger's timestats), not argued here. The
//   window and the level-indexed dilation schemes are modeled bit for bit
//   in Python against the brief's forward model
//   (tmp/grow/make_grow_mag.py's mask / morph / down at SS = 4) at every
//   grow 0 .. 8, 2026-10-31, and are that text still; the fast paths, each
//   a subset of the rule, change no result.
//   * THE MERGED ARM (architect 2026-11-14, send H: the fade at the level
//     switch, one pass; send K: the fade over the whole doubling; render.h's
//     THE FADE BAND owns the band, the weight and the row). A row of weight
//     w < 1 holds n fine bars, n even — bar 2K and bar 2K + 1 the LEFT and
//     RIGHT halves of pair K, their tips mixed toward their parent's on the
//     CPU — and the instance's floats are {xc, p, n, w}: xc = v_bars.x THE
//     FIRST PAIR'S PARENT CENTRE in device px from the rect's left edge, p
//     the fine bars' pitch, in [1/2, 1] — half the main rung's pitch, which
//     is over the key's start there and under the switch, so the pairs
//     stand 2p >= 1 px apart (a double a hair over 1/2 or under 1 may round
//     to the float 0.5 or 1.0, both inside the domain).
//     THE VERTICES: in PAIR COORDINATES U = (xs − xc) / P, P = 2p, pair K's
//     parent centre is U = K, and with h = w / 4 its left half stands at K −
//     h and its right at K + h — in px, the parent's centre ∓ w · p / 2: at
//     w = 0 both on the parent's centre, toward w = 1 on their own. The
//     gaps alternate w · p inside a pair and (2 − w) · p across pairs. THE
//     PICTURE IS THE UNIFORM ARM'S RULE OVER THOSE VERTICES: the contour
//     straight from vertex to vertex, each vertex's one-px core, then the
//     dilation, the coverage and the composite, unchanged.
//     THE BRACKET OF A SUBCOLUMN (pair_bracket, THE ONE FUNCTION every site
//     of the arm calls; the picture is this arithmetic, ties included, every
//     operation a highp float's, each rounded once; architect 2026-11-14,
//     send I: no division in it): P = 2.0 · p, h = 0.25 · w, h2 = 2.0 · h
//     (all exact), D = 1.0 − h2 and THE TWO GAPS in device px, GI = h2 · P
//     inside a pair and GO = D · P across pairs; the vertex shader hands it
//     invP = 1 / P, invI = 1 / h2 (1 where h2 is 0) and invO = 1 / D (THE
//     INSTANCE'S RECIPROCALS, above). xs = (float(s) + 0.5) · 0.25 (exact);
//     U = (xs − xc) · invP, ONE subtraction then ONE multiply; K =
//     floor(U), g = U − floor(U) in [0, 1]. THREE CASES, in this order, both
//     tests strict, each giving a and z, f's NUMERATOR:
//       LOW    g < h:        a = 2K,     z = g + h
//       HIGH   (1 − g) < h:  a = 2K + 2, z = h − (1 − g)
//       ACROSS otherwise:    a = 2K + 1, z = g − h
//     Then an even a — LOW and HIGH, also below zero — is a pair's inner
//     gap, f = z · invI and G = GI; an odd a is the gap across pairs, f = z
//     · invO and G = GO. The two terms of U are never scaled apart, U is
//     never stepped by addition, the reciprocals are never multiplied
//     together, and no test is taken on another quantity than these.
//     THE BRACKETS are bars a and a + 1, f in [0, 1] the subcolumn's place
//     between their two vertices and G that gap in device px. LOW is the
//     right half of pair K's inner gap, HIGH the left half of pair K + 1's,
//     ACROSS the whole gap from pair K's right half to pair K + 1's left; a
//     is the last vertex at or left of the subcolumn, so it never steps back
//     as s grows (each float step is monotone, and h < 1/4 keeps LOW and
//     HIGH apart). At w = 0 (h = 0) only ACROSS fires — both strict tests
//     fail even where g rounds to 1 — with invO exactly 1, so f = g and G =
//     P exactly: the uniform arm's bracket of the parents at pitch P.
//     THE WEIGHT'S DOMAIN is the producer's (render.h's waveform_fade_band):
//     exactly 0, or an ordinary positive float under 1 — the least about
//     3.2e-16 (log2(1 + 2^-52) at F = 1), whose reciprocal is far inside a
//     float. A subnormal weight has no producer and no path here.
//     THE BASE is base_bits over bars a and a + 1 with f and G in the place
//     of the uniform arm's f and p: the nearer rule (a where f < 0.5), the
//     absent rules, the quantum fq = floor(f · 256 + 0.5), and THE
//     BRACKETS' CORES ALONE — a's covers iff f · G < 0.5, a + 1's iff (1 −
//     f) · G <= 0.5. NO OTHER BAR'S CORE IS JOINED, though a sibling just
//     beyond a bracket stands within half a px while w · p < 1/2. WHAT THAT
//     CUTS: while a pair is still parting, the taller half's body ends at
//     its sibling's vertex on that side and the contour takes over there.
//     Ruled cheap and simple inside the band (architect 2026-11-14: "The
//     untruthfulness of the trough is accepted ... Everything else just
//     needs to be almost overview level granularity"); both ends stay exact
//     — at w = 0 the sibling is the same vertex with the same tips, toward w
//     = 1 the halves stand on their own centres. NO THIRD CORE REACHES FROM
//     ANOTHER PAIR, at any w: the gap across pairs is (2 − w) · p, and the
//     weight never runs ahead of log2 P (w = (x − f) / (1 − f) <= x for
//     x = log2 P in [0, 1] and f = log2 F in [0, 1)), so that gap is at
//     least (2 − log2 P) · P / 2 >= 1 px over P in [1, 2] — no vertex of
//     another pair stands within half a px of a subcolumn in a pair's own
//     gap, and the one bar the rule leaves out is a bracket's sibling.
//     AT THE TWO ENDS OF THE BAND: at w = 0 a pair's two entries hold the
//     parent's tips, the brackets (2K + 1, 2K + 2) are the parents K and K +
//     1 — existing exactly where those parents would in a row of n / 2 —
//     and every float is the uniform arm's over the parents at x0 = xc and
//     pitch 2p — 2 · float(p) is float(P), so the vertex shader forms the
//     same reciprocal, and the subtraction, the multiply and U are the
//     same: the main rung's picture as the uniform arm draws it, byte for
//     byte, every pixel. At the largest w under 1 the vertices are the fine
//     bars' own centres to a part in 2^24: the fine rung's picture — the
//     one the frame past the switch draws with the fine rung as the main
//     rung — but for a decision (a floor, a case, the nearer, a core, a
//     quantum step) that sits near its boundary.
//     THE ROW'S ENDS AND THE SITES. bar_at is the row's whole end rule: a
//     bar outside [0, n) is the zero texel, absent in each family's test,
//     and nothing outside the row is fetched; the base's absent cases then
//     say what stands there (left of bar 0's vertex that bar alone, flat,
//     where it is the nearer). UNLIKE THE UNIFORM ARM'S, THIS BASE IS NOT
//     CONTINUOUS ACROSS A VERTEX whose sibling stands within half a px (the
//     sibling's core is in on the pair's side and out beyond it), so no
//     site may stand in for another: every site calls pair_bracket for ITS
//     OWN subcolumn and reads only what it says — the support's ends are
//     the walk's first and last subcolumns, aF the own loop's first, the
//     own loop's four the walk's — and the walk fetches by the index the
//     function returns, never by a count of steps.
//     THE SUPPORT'S BARS: every bar the pixel's word depends on lies in aA
//     .. aB, aA the first subcolumn's a and aB the last's a + 1 (a never
//     steps back). THEY ARE AT MOST NM = 2 · ((NO − 1) / 4) + 4 (architect
//     2026-11-14, send K): the bars between are the vertices in the span of
//     the NO subcolumns' centres, (NO − 1) / 4 px, a span of L = (NO − 1) /
//     (8p) <= (NO − 1) / 4 pairs (p >= 1/2), which holds at most floor(L) +
//     1 left halves and as many right, and one bar stands beyond each end;
//     NO − 1 = 3 + 2 · GROW is odd, so (NO − 1) / 4 is a quarter pair or
//     more off a whole number, far past any float's rounding. The bound is
//     reached at a pitch near 1/2 px with the pairs barely parted (a float32
//     sweep of every grow 0 .. 8 over p in [1/2, 1], w from 0 to the float
//     under 1 and pixels to 4130, 2026-11-15, met it and never passed it);
//     where p >= 3/4 the support is at most 2 · ((NO − 1) / 6) + 4 bars, so
//     there the loop's further steps repeat aB. One branch-free loop fetches
//     bar min(aA + d, aB) for d < NM into the hulls — a repeat of aB changes
//     no hull and no kept slot — keeps the four from aF for the pixel's own
//     subcolumns and its first two for the walk.
//     THE CERTIFICATES are the uniform arm's predicates over this arm's
//     brackets, each a statement about base_bits' own answer for the SAME
//     subcolumn and the same {a, f, G}, so none leans on a gap being
//     uniform. REACH: a base lies within the hull of its two brackets (the
//     contour between them, a core within its own bar — the brackets'
//     alone), and the dilation adds GROW subrows at most, so a pixel more
//     than GROW outside the hull of aA .. aB's existing bars has no bit:
//     the discard, and n_i = 0 past the inners' hull. HOLD and sub_full: a
//     subcolumn is certainly full where both its brackets hold the pixel
//     (the contour is a non-empty run from at most max(T) to at least
//     min(B) − 1), or one that holds is the nearer with the farther absent
//     (the contour is that bar), or one that holds covers it with its core
//     (the base contains the bar); the dilation's dx = 0 column grows that
//     base GROW rows, over the pixel's four. THE PIXEL IS FULL where its
//     four subcolumns are; their lower brackets are aF .. aF + 2 (the four
//     span 3/4 px, 3 / (8p) <= 3/4 pair: under one whole pair, a quarter
//     pair clear of any rounding, so at most one left and one right half
//     crossed), so their bars are the four from aF, and a lower
//     bracket outside aF .. aF + 2 certifies nothing. THE OPAQUE
//     INTERSECTION is the uniform arm's (b) over THE FOUR bars from aF —
//     every own subcolumn's two brackets are among them, so where all four
//     hold the pixel it is full in that family, and a slot the support's
//     loop did not write is absent and fails the test — and the own
//     subcolumns' certificates are reach-gated as there (architect
//     2026-11-14, send I). The opaque words are
//     the composite's own at cov_o = 1 and cov_i 0 or 1, as in the uniform
//     arm. THE INTERIOR OF A SOLID PASSAGE IS FULL at every w: there every
//     bar holds the pixel, whichever two a subcolumn's brackets are, and
//     the intersection returns before any own subcolumn is placed. WHAT
//     THE CORES RULE COSTS THE FAST PATHS: beside a half that holds the
//     pixel whose sibling does not, the subcolumns past the sibling's
//     vertex are not certified by the tall half's core as a uniform row's
//     would be — the rule itself does not fill them — so those fringe
//     pixels, on the rows between a pair's two heights while w · p < 1/2,
//     take the roads.
//     THE ROLL: between two subcolumns U moves 1 / (8p) <= 1/4 of a pair,
//     so a keeps, steps ONE bar, or steps TWO — a whole pair crossed in one
//     quarter px, its inner gap w · p under 1/4 — and never three (three
//     consecutive vertices span a gap across pairs, 1 px or more, above).
//     The walk's pair of scalars rolls on a one-step (the right becomes the
//     left, one fetch) and is fetched whole on any other change, so it
//     fetches exactly the bars aA + 2 .. aB, one a vertex crossed.
//     THE COST: the places are the uniform arm's counts, 3, 7 and 7 + NO by
//     where a fragment ends, a place here one pair_bracket (the three-case
//     bracket); in fetches a fragment, NM on every fragment (the support's
//     loop; the fast paths end there), and on a road one more a vertex the
//     support's span crosses, aB − aA − 1 <= NM − 2. At GROW 4 NM is 8
//     against the uniform arm's NU of 5, and where the key is at 1 the
//     merged arm draws every zoom between two whole levels, the uniform arm
//     the landings alone. Whether the band's frames hold 16.7 ms (the 60 Hz
//     pin, kPanelRefreshHz) on the tablet is measured there.
//   * WHAT IS PROMISED (architect 2026-11-14, send I). TRUTH IS REQUIRED AT
//     THE WORKING ZOOM ALONE — his words: "I'm not really looking for
//     truthfulness in anything beyond working Zoom level, so long as it's
//     reasonable ... I just want the overall topography ... Only at working
//     Zoom do I require that level of detail ... whatever shortcuts or
//     whatever optimizations can be done at the cost of truthfulness in
//     exchange for performance are welcome and should be applied." WHAT
//     THIS SHADER SPENDS OF THAT IS ONE THING: a subcolumn is placed by a
//     multiply with the instance's reciprocal where a divide would be the
//     exact quotient. A LANDING ON A WHOLE LEVEL — p = 1 on a whole offset:
//     the working zoom reached by a landing, and every whole level alike
//     (render.h's DETERMINISM HAS ONE SEAT) — IS BYTE-IDENTICAL TO THE
//     QUOTIENT'S PICTURE, the whole plate: the reciprocal is exactly 1 and
//     every u is the difference itself (THE BASE's landing paragraph).
//     EVERYWHERE ELSE THE PICTURE IS THIS ARITHMETIC AS
//     WRITTEN, a function of the viewport alone — no time, no state, the
//     same bytes for the same viewport — and where a decision (a floor, a
//     case of the merged bracket, the nearer, a core, a quantum step) sits
//     near its boundary it may fall the other way than a divide's would:
//     that one subcolumn's base, a quarter px of one edge, nothing of the
//     shape. THE FAST PATHS AND THE BOUNDS SPEND NOTHING: each gives the
//     full rule's bytes — every support subcolumn's base, the dilation, the
//     count — at every pixel, ties, row ends and absent bars included. THE
//     BAND'S START is exact against the uniform arm and its end meets the
//     fine rung's picture but for such decisions (THE MERGED ARM, at the
//     two ends). EACH PLACE IS TAKEN WHERE IT IS NEEDED: the staged reuse
//     of the support's brackets — each subcolumn located once, six held at
//     constant indices and finished only where its f is wanted — was built
//     (send I, patch 03) and measured SLOWER on the tablet's compiler (the
//     lit fade pan 10.2 ms against 9.3 ms a frame, post-to-acquire,
//     2026-11-14), so each bracket is located where it is needed, as here.
// A word is cairo's 0xAARRGGBB with A = 255, written as (R, G, B, 1) at
// k / 255 per channel for every record but the plate's fringe and the tint,
// which the RGBA8 framebuffer stores as k — the texture triangle's own
// channel order. Primitives write in instance order (GL's primitive order
// within a draw), so a later record overwrites an earlier one, or blends
// over it (the plate's fringe, the tint), the paint order; under the blend
// a word written at alpha 1 lands exactly (src · 1 + dst · 0).
constexpr const char* kListVertexShader =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "layout(location = 0) in ivec4 a_rect;\n"
    "layout(location = 1) in ivec4 a_param;\n"
    "layout(location = 2) in uvec2 a_words;\n"
    "layout(location = 3) in vec4 a_bars;\n"
    "uniform vec2 u_size;\n"
    "flat out ivec4 v_rect;\n"
    "flat out ivec4 v_param;\n"
    "flat out uvec2 v_words;\n"
    "flat out vec4 v_bars;\n"
    "flat out vec3 v_inv;\n"
    "void main() {\n"
    "    int x = a_rect.x + (((gl_VertexID & 1) != 0) ? a_rect.z : 0);\n"
    "    int y = a_rect.y + (((gl_VertexID & 2) != 0) ? a_rect.w : 0);\n"
    "    gl_Position = vec4(float(x) / u_size.x * 2.0 - 1.0,\n"
    "                       1.0 - float(y) / u_size.y * 2.0, 0.0, 1.0);\n"
    "    v_rect  = a_rect;\n"
    "    v_param = a_param;\n"
    "    v_words = a_words;\n"
    "    v_bars  = a_bars;\n"
    // THE INSTANCE'S RECIPROCALS (architect 2026-11-14, send I; the head's
    // bars paragraph owns the rule): the divisions a plate's row needs,
    // taken here from the instance's own floats and handed on flat — zero
    // on every record but a plate's; a uniform row (weight exactly 1) {1 /
    // p, 1, 1}; the fade band's row {1 / P, 1 / (2h), 1 / (1 − 2h)}, the
    // second 1 where 2h is 0 (safeI: no 1 / 0 is formed).
    "    v_inv = vec3(0.0);\n"
    "    if (a_param.x == 1) {\n"
    "        if (a_bars.w == 1.0) {\n"
    "            v_inv = vec3(1.0 / a_bars.y, 1.0, 1.0);\n"
    "        } else {\n"
    "            float P = 2.0 * a_bars.y;\n"
    "            float h = 0.25 * a_bars.w;\n"
    "            float h2 = 2.0 * h;\n"
    "            float D = 1.0 - h2;\n"
    "            float safeI = (h2 > 0.0) ? h2 : 1.0;\n"
    "            v_inv = vec3(1.0 / P, 1.0 / safeI, 1.0 / D);\n"
    "        }\n"
    "    }\n"
    "}\n";

constexpr const char* kListFragmentShader =
    "#version 300 es\n"
    // THE ONE SUBSTITUTION POINT (list_fragment_source): the grow's digits
    // land here, so GROW is a preprocessor constant of the whole source.
    "#define GROW @GROW@\n"
    // THE UNROLL HINT: where the driver knows GL_EXT_control_flow_attributes
    // (the macro is predefined iff it does) every loop below carries
    // [[unroll]]; elsewhere UNROLL is empty and the text is plain ES 3.00.
    // The loops' bounds are constants either way (the specialization rule).
    "#ifdef GL_EXT_control_flow_attributes\n"
    "#extension GL_EXT_control_flow_attributes : enable\n"
    "#define UNROLL [[unroll]]\n"
    "#else\n"
    "#define UNROLL\n"
    "#endif\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "uniform highp isampler2D u_columns;\n"
    "uniform int u_height;\n"
    "flat in ivec4 v_rect;\n"
    "flat in ivec4 v_param;\n"
    "flat in uvec2 v_words;\n"
    "flat in vec4 v_bars;\n"
    "flat in vec3 v_inv;\n"
    "out vec4 o_color;\n"
    "vec3 word_rgb(uint w) {\n"
    "    return vec3(float((w >> 16u) & 255u), float((w >> 8u) & 255u),\n"
    "                float(w & 255u)) / 255.0;\n"
    "}\n"
    // THE WINDOW'S WORD (the head's plate paragraph): one uint of 32 bits,
    // bit b the subrow k0 + b, the pixel's four subrows bits PIX0 .. PIX0 +
    // 3. range_bits is the run [lo, hi] clipped to [0, 31] (the full word by
    // a right shift of the all-ones, never a shift by 32); pix4 the pixel's
    // nibble.
    "const int PIX0 = 14;\n"
    "uint range_bits(int lo, int hi) {\n"
    "    int a = max(lo, 0);\n"
    "    int b = min(hi, 31);\n"
    "    if (b < a) return 0u;\n"
    "    return (0xFFFFFFFFu >> (31 - (b - a))) << a;\n"
    "}\n"
    "uint pix4(uint m) { return (m >> PIX0) & 15u; }\n"
    "uint grow1(uint m) { return m | (m << 1) | (m >> 1); }\n"
    // A SUBCOLUMN'S PLACE AMONG THE BARS (the head's bars paragraph): u for
    // the screen subcolumn s of the rect, x0 the first bar's centre and inv
    // the instance's reciprocal pitch (v_inv.x, the vertex shader's 1 / p) —
    // the one expression every site of the uniform arm evaluates. THE
    // ARITHMETIC IS THE PICTURE'S (architect 2026-11-14, send I): the
    // centre's product exact, then ONE subtraction and ONE multiply, each
    // rounded once — never the two terms scaled apart, never a u stepped by
    // addition; at a landing inv is exactly 1 and u is the difference.
    "float bar_u(int s, float x0, float inv) {\n"
    "    return ((float(s) + 0.5) * 0.25 - x0) * inv;\n"
    "}\n"
    // BAR j OF THE RECORD'S ROW, the zero texel where it does not exist
    // (absent in both families' own test, bot > top); the coordinate is
    // clamped, so no texel outside [0, n) is fetched.
    "ivec4 bar_at(int j, int n, int row) {\n"
    "    ivec4 c = texelFetch(u_columns, ivec2(clamp(j, 0, n - 1), row), 0);\n"
    "    return (j >= 0 && j < n) ? c : ivec4(0);\n"
    "}\n"
    // A SUBCOLUMN'S BRACKET IN THE FADE BAND'S ROW (the head's merged arm):
    // for the screen subcolumn s of the rect and the instance's floats
    // {xc, p, n, w} it answers a, the lower bracket's bar (the upper is a +
    // 1), and writes f, the subcolumn's place between the two vertices, and
    // G, their gap in device px — the one evaluation every site of the
    // merged arm calls, so every site holds the same three for a subcolumn.
    // In pair coordinates U (pair K's parent centre at U = K, its halves at
    // K − h and K + h, h = w / 4) the three cases, in this order, both tests
    // strict: LOW, between the centre and the right half; HIGH, between the
    // next pair's left half and its centre; ACROSS, the gap between pairs.
    // NO DIVISION IS TAKEN HERE (architect 2026-11-14, send I): inv is the
    // instance's reciprocals out of the vertex shader, {invP, invI, invO} =
    // {1 / P, 1 / (2h), 1 / (1 − 2h)}. U is ONE rounded subtraction then
    // ONE rounded multiply by invP; the case gives a and f's NUMERATOR z
    // (g + h, h − (1 − g), g − h); then f = z · invI in a pair's inner gap —
    // an even a, also below zero — and z · invO across pairs, and G is the
    // gap's own product, h2 · P or (1 − h2) · P. The reciprocals are never
    // multiplied together and no test is taken on another quantity. At h =
    // 0 ACROSS alone fires with invO exactly 1: f = g and G = P.
    "int pair_bracket(int s, vec4 bars, vec3 inv, out float f, out float G) {\n"
    "    float P = 2.0 * bars.y;\n"
    "    float h = 0.25 * bars.w;\n"
    "    float h2 = 2.0 * h;\n"
    "    float D = 1.0 - h2;\n"
    "    float GI = h2 * P;\n"
    "    float GO = D * P;\n"
    "    float xs = (float(s) + 0.5) * 0.25;\n"
    "    float U = (xs - bars.x) * inv.x;\n"
    "    float Kf = floor(U);\n"
    "    int K = int(Kf);\n"
    "    float g = U - Kf;\n"
    "    float omg = 1.0 - g;\n"
    "    int a;\n"
    "    float z;\n"
    "    if (g < h) {\n"
    "        a = 2 * K;\n"
    "        z = g + h;\n"
    "    } else if (omg < h) {\n"
    "        a = 2 * K + 2;\n"
    "        z = h - omg;\n"
    "    } else {\n"
    "        a = 2 * K + 1;\n"
    "        z = g - h;\n"
    "    }\n"
    "    bool inside = (a & 1) == 0;\n"
    "    f = z * (inside ? inv.y : inv.z);\n"
    "    G = inside ? GI : GO;\n"
    "    return a;\n"
    "}\n"
    // THE BASE of one subcolumn for one family (the head's base paragraph):
    // a and b the brackets' {top, bottom} tips, fq f's quantum in 256ths,
    // near_a whether a is the nearer, core_a / core_b whether each bracket's
    // one-px core covers the subcolumn.
    "uint base_bits(ivec2 a, ivec2 b, int fq, bool near_a, bool core_a,\n"
    "               bool core_b, int k0) {\n"
    "    bool ea = a.y > a.x;\n"
    "    bool eb = b.y > b.x;\n"
    "    uint m = 0u;\n"
    "    if (near_a ? ea : eb) {\n"
    // Both brackets existing, the contour between them; the farther absent,
    // the nearer bar flat (the same sums over the nearer twice: 256 T, 256 B).
    "        ivec2 nr = near_a ? a : b;\n"
    "        ivec2 ca = (ea && eb) ? a : nr;\n"
    "        ivec2 cb = (ea && eb) ? b : nr;\n"
    "        int t256 = (256 - fq) * ca.x + fq * cb.x;\n"
    "        int b256 = (256 - fq) * ca.y + fq * cb.y;\n"
    "        m = range_bits((t256 + 127) / 256 - k0, (b256 - 128) / 256 - k0);\n"
    "    }\n"
    "    if (core_a && ea) m |= range_bits(a.x - k0, a.y - 1 - k0);\n"
    "    if (core_b && eb) m |= range_bits(b.x - k0, b.y - 1 - k0);\n"
    "    return m;\n"
    "}\n"
    // THE HOLD LEMMA's two predicates (the head's fast paths): a bar holds
    // the pixel where it exists and its subrows grown by GROW contain the
    // pixel's four (ylo .. ylo + 3); a subcolumn is certainly full where
    // both its brackets hold, or one that holds is the flat contour's bar or
    // covers it with its core.
    "bool holds(ivec2 c, int ylo) {\n"
    "    return c.y > c.x && c.x - GROW <= ylo && ylo + 3 <= c.y - 1 + GROW;\n"
    "}\n"
    "bool sub_full(ivec2 a, ivec2 b, bool near_a, bool core_a, bool core_b,\n"
    "              int ylo) {\n"
    "    bool ha = holds(a, ylo);\n"
    "    bool hb = holds(b, ylo);\n"
    "    return (ha && hb) ||\n"
    "           (ha && (core_a || (near_a && !(b.y > b.x)))) ||\n"
    "           (hb && (core_b || (!near_a && !(a.y > a.x))));\n"
    "}\n"
    // THE MASK ARRAYS, SIZED TO THE REACH (the head's specialization rule):
    // the outer's bases bo and the inner's bi over the subcolumns 4i − GROW
    // .. 4i + 3 + GROW (index t the subcolumn 4i − GROW + t). Globals, so
    // the disc routines read them in place (GLSL passes an array by value);
    // every index a loop variable of a constant-bounded loop, so the
    // unrolled accesses are constant and the arrays are registers.
    "const int NO = 4 + 2 * GROW;\n"
    // THE UNIFORM ARM'S SUPPORT AT MOST (architect 2026-11-14, send I): a
    // uniform row's bars stand A PX OR MORE apart — the switch width is a
    // fixed 2 px, so a rung's own pitch is 1 or more (render.h's
    // waveform_bar_frames; the static_assert after the source) — so the NO
    // subcolumns' centres, (NO − 1) / 4 px end to end, span at most (NO −
    // 1) / 4 bars: the first and the last lower bracket's indices differ by
    // at most floor((NO − 1) / 4) + 1, so with both of them and the last
    // one's upper bracket the support's bars are at most floor((NO − 1) /
    // 4) + 3, NU. NO − 1 = 3 + 2 · GROW
    // is odd, so that span is a quarter bar or more short of another bar,
    // far past any float's rounding. NU >= 3 at every grow.
    "const int NU = (NO - 1) / 4 + 3;\n"
    // THE MERGED ARM'S SUPPORT AT MOST (the head's merged arm; architect
    // 2026-11-14, send K, the fade over the whole doubling): the NO
    // subcolumns' centres span (NO − 1) / 4 px, and the pairs stand a px or
    // more apart — the fade band's fine bars 1/2 px or more — so the span
    // is (NO − 1) / 4 pairs at most, holding at most floor of that plus one
    // left halves and as many right, with one bar beyond each end. NO − 1 =
    // 3 + 2 · GROW is odd, so that span is a quarter pair or more short of
    // another vertex, far past any float's rounding; the bound is reached
    // at a pitch near 1/2 px with the pairs barely parted.
    "const int NM = 2 * ((NO - 1) / 4) + 4;\n"
    "uint bo[NO];\n"
    "uint bi[NO];\n"
    // THE DISC DILATION of radius GROW (the Horner scheme of the head,
    // indexed by the grow level): v walks the level down from GROW to 0; at
    // each level the pairs s ± j whose disc column stands exactly v rows
    // tall (v² + j² <= GROW² < (v + 1)² + j², i.e. g(j) = v) join the
    // accumulator, which then grows by one row per level above 0. Every
    // condition compares constants once the loops unroll.
    "uint dilate_o(int s) {\n"
    "    uint acc = bo[s];\n"
    "    UNROLL for (int v = GROW; v >= 0; --v) {\n"
    "        UNROLL for (int j = 1; j <= GROW; ++j) {\n"
    "            if (j * j + v * v <= GROW * GROW &&\n"
    "                j * j + (v + 1) * (v + 1) > GROW * GROW)\n"
    "                acc |= bo[s - j] | bo[s + j];\n"
    "        }\n"
    "        if (v > 0) acc = grow1(acc);\n"
    "    }\n"
    "    return acc;\n"
    "}\n"
    "uint dilate_i(int s) {\n"
    "    uint acc = bi[s];\n"
    "    UNROLL for (int v = GROW; v >= 0; --v) {\n"
    "        UNROLL for (int j = 1; j <= GROW; ++j) {\n"
    "            if (j * j + v * v <= GROW * GROW &&\n"
    "                j * j + (v + 1) * (v + 1) > GROW * GROW)\n"
    "                acc |= bi[s - j] | bi[s + j];\n"
    "        }\n"
    "        if (v > 0) acc = grow1(acc);\n"
    "    }\n"
    "    return acc;\n"
    "}\n"
    "int pop16(uint v) {\n"
    "    v = v - ((v >> 1) & 0x5555u);\n"
    "    v = (v & 0x3333u) + ((v >> 2) & 0x3333u);\n"
    "    v = (v + (v >> 4)) & 0x0F0Fu;\n"
    "    return int((v + (v >> 8)) & 0x1Fu);\n"
    "}\n"
    "void main() {\n"
    "    int px = int(gl_FragCoord.x);\n"
    "    int py = u_height - 1 - int(gl_FragCoord.y);\n"
    "    if (px < v_rect.x || px >= v_rect.x + v_rect.z ||\n"
    "        py < v_rect.y || py >= v_rect.y + v_rect.w) discard;\n"
    "    int kind = v_param.x;\n"
    // THE MERGED ARM (the head's merged arm paragraph): a plate row of
    // weight under 1, the fade band's. The uniform arm below, taken at
    // exactly 1, shares none of this text but the functions.
    "    if (kind == 1 && v_bars.w != 1.0) {\n"
    "        int n = int(v_bars.z);\n"
    "        int row = v_param.y;\n"
    "        int i = px - v_rect.x;\n"
    "        int y = py - v_param.z;\n"
    "        int k0 = 4 * y - PIX0;\n"
    "        int ylo = 4 * y;\n"
    "        int yhi = ylo + 3;\n"
    // THE SUPPORT'S BARS: aA .. aB bracket the subcolumns 4i − GROW .. 4i +
    // 3 + GROW, and aF is the pixel's first subcolumn's lower bracket. One
    // branch-free loop fetches them, each into the hulls, the four from aF
    // kept for the pixel's own subcolumns and the first two, aA and aA + 1
    // (aB >= aA + 1: the bracket never steps back), for the walk's seeds.
    "        int s0 = 4 * i - GROW;\n"
    "        float f;\n"
    "        float G;\n"
    "        int aA = pair_bracket(s0, v_bars, v_inv, f, G);\n"
    "        int aB = pair_bracket(s0 + NO - 1, v_bars, v_inv, f, G) + 1;\n"
    "        int aF = pair_bracket(4 * i, v_bars, v_inv, f, G);\n"
    "        ivec4 own0 = ivec4(0);\n"
    "        ivec4 own1 = ivec4(0);\n"
    "        ivec4 own2 = ivec4(0);\n"
    "        ivec4 own3 = ivec4(0);\n"
    "        ivec4 bl = ivec4(0);\n"
    "        ivec4 br = ivec4(0);\n"
    "        int tmin_o = 1 << 20;\n"
    "        int bmax_o = -(1 << 20);\n"
    "        int tmin_i = 1 << 20;\n"
    "        int bmax_i = -(1 << 20);\n"
    "        UNROLL for (int d = 0; d < NM; ++d) {\n"
    "            int j = min(aA + d, aB);\n"
    "            ivec4 c = bar_at(j, n, row);\n"
    "            if (c.y > c.x) {\n"
    "                tmin_o = min(tmin_o, c.x);\n"
    "                bmax_o = max(bmax_o, c.y);\n"
    "            }\n"
    "            if (c.w > c.z) {\n"
    "                tmin_i = min(tmin_i, c.z);\n"
    "                bmax_i = max(bmax_i, c.w);\n"
    "            }\n"
    "            if (j == aF) own0 = c;\n"
    "            if (j == aF + 1) own1 = c;\n"
    "            if (j == aF + 2) own2 = c;\n"
    "            if (j == aF + 3) own3 = c;\n"
    "            if (d == 0) bl = c;\n"
    "            if (d == 1) br = c;\n"
    "        }\n"
    "        bool reach_o = yhi >= tmin_o - GROW && ylo <= bmax_o - 1 + GROW;\n"
    "        bool reach_i = yhi >= tmin_i - GROW && ylo <= bmax_i - 1 + GROW;\n"
    // The fast paths in the uniform arm's order: the discard; the full
    // inner, the core opaque; the full outer; the ink opaque — the two
    // opaque words first by the own bars alone, then by the own subcolumns.
    "        if (!reach_o && !reach_i) discard;\n"
    // THE OPAQUE INTERSECTION (architect 2026-11-14, send I; an exact early
    // return that stands on its own — without it the certificates below
    // give the same words): where the inner of ALL FOUR bars from aF holds
    // the pixel it is full in the inner family, the core's word; where no
    // inner reaches and the outer of all four holds it, the ink's word —
    // before any own subcolumn is placed. EXACT: each own subcolumn's two
    // brackets are among those four bars (the four subcolumns span three
    // quarters of a pair at most: lower brackets aF .. aF + 2), and where
    // both brackets hold the subcolumn is full (sub_full's first case). A
    // slot the loop above did not write is the zero texel, absent: it fails
    // the test, never passes it.
    "        if (reach_i && holds(own0.zw, ylo) && holds(own1.zw, ylo) &&\n"
    "            holds(own2.zw, ylo) && holds(own3.zw, ylo)) {\n"
    "            o_color = vec4(word_rgb(v_words.y), 1.0);\n"
    "            return;\n"
    "        }\n"
    "        if (!reach_i && reach_o && holds(own0.xy, ylo) &&\n"
    "            holds(own1.xy, ylo) && holds(own2.xy, ylo) &&\n"
    "            holds(own3.xy, ylo)) {\n"
    "            o_color = vec4(word_rgb(v_words.x), 1.0);\n"
    "            return;\n"
    "        }\n"
    // The pixel's own four subcolumns, each by its own bracket: e the lower
    // bracket from aF, 0 .. 2 (the four span three quarters of a pair at
    // most) — another certifies nothing. THE CERTIFICATES ARE REACH-GATED
    // (architect 2026-11-14, send I): a family that does not reach the pixel
    // cannot be full there (a bar that holds the pixel is in the hull), so
    // each flag starts at its reach and a family's sub_full is evaluated
    // only while its flag stands.
    "        bool full_o = reach_o;\n"
    "        bool full_i = reach_i;\n"
    "        UNROLL for (int m = 0; m < 4; ++m) {\n"
    "            int e = pair_bracket(4 * i + m, v_bars, v_inv, f, G) - aF;\n"
    "            ivec4 a = (e == 0) ? own0 : ((e == 1) ? own1 : own2);\n"
    "            ivec4 b = (e == 0) ? own1 : ((e == 1) ? own2 : own3);\n"
    "            bool ok = e >= 0 && e <= 2;\n"
    "            bool near_a = f < 0.5;\n"
    "            bool core_a = f * G < 0.5;\n"
    "            bool core_b = (1.0 - f) * G <= 0.5;\n"
    "            if (full_o)\n"
    "                full_o = ok &&\n"
    "                         sub_full(a.xy, b.xy, near_a, core_a, core_b, ylo);\n"
    "            if (full_i)\n"
    "                full_i = ok &&\n"
    "                         sub_full(a.zw, b.zw, near_a, core_a, core_b, ylo);\n"
    "        }\n"
    "        if (full_i) {\n"
    "            o_color = vec4(word_rgb(v_words.y), 1.0);\n"
    "            return;\n"
    "        }\n"
    "        if (full_o && !reach_i) {\n"
    "            o_color = vec4(word_rgb(v_words.x), 1.0);\n"
    "            return;\n"
    "        }\n"
    "        uint wo = full_o ? 0xFFFFu : 0u;\n"
    "        uint wi = 0u;\n"
    "        bool need_o = reach_o && !full_o;\n"
    // THE BASES over 4i − GROW .. 4i + 3 + GROW, ONE WALK FOR BOTH FAMILIES:
    // the brackets a rolling pair of scalars, bl and br, seeded with bars aA
    // and aA + 1 out of the loop above; al the left's index. The bracket
    // keeps, steps one bar (the right becomes the left, one fetch) or steps
    // two, a whole pair crossed in one quarter px (both fetched); any other
    // difference fetches both too, so the pair is the bracket's whatever
    // the step.
    "        int al = aA;\n"
    "        UNROLL for (int t = 0; t < NO; ++t) {\n"
    "            int a = pair_bracket(s0 + t, v_bars, v_inv, f, G);\n"
    "            if (a == al + 1) {\n"
    "                bl = br;\n"
    "                br = bar_at(a + 1, n, row);\n"
    "            } else if (a != al) {\n"
    "                bl = bar_at(a, n, row);\n"
    "                br = bar_at(a + 1, n, row);\n"
    "            }\n"
    "            al = a;\n"
    "            int fq = int(floor(f * 256.0 + 0.5));\n"
    "            bool near_a = f < 0.5;\n"
    "            bool core_a = f * G < 0.5;\n"
    "            bool core_b = (1.0 - f) * G <= 0.5;\n"
    "            if (need_o)\n"
    "                bo[t] = base_bits(bl.xy, br.xy, fq, near_a, core_a, core_b, k0);\n"
    "            if (reach_i)\n"
    "                bi[t] = base_bits(bl.zw, br.zw, fq, near_a, core_a, core_b, k0);\n"
    "        }\n"
    // The dilations, the coverage and the composite: the uniform arm's,
    // literally.
    "        if (need_o) {\n"
    "            UNROLL for (int m = 0; m < 4; ++m)\n"
    "                wo |= pix4(dilate_o(m + GROW)) << (4 * m);\n"
    "        }\n"
    "        if (reach_i) {\n"
    "            UNROLL for (int m = 0; m < 4; ++m) {\n"
    "                uint i4 = pix4(dilate_i(m + GROW));\n"
    "                wo |= i4 << (4 * m);\n"
    "                wi |= i4 << (4 * m);\n"
    "            }\n"
    "        }\n"
    "        int n_o = pop16(wo);\n"
    "        int n_i = pop16(wi);\n"
    "        if (n_o == 0) discard;\n"
    "        float cov_o = float(n_o) / 16.0;\n"
    "        float cov_i = float(n_i) / 16.0;\n"
    "        if (n_i > 0 && n_o < 16) {\n"
    "            o_color = vec4(word_rgb(v_words.x) * (cov_o - cov_i) +\n"
    "                           word_rgb(v_words.y) * cov_i,\n"
    "                           cov_o);\n"
    "        } else {\n"
    "        o_color = vec4(word_rgb(v_words.x) * (cov_o * (1.0 - cov_i)) +\n"
    "                       word_rgb(v_words.y) * cov_i,\n"
    "                       1.0 - (1.0 - cov_o) * (1.0 - cov_i));\n"
    "        }\n"
    "        return;\n"
    "    }\n"
    "    if (kind == 1) {\n"
    "        int n = int(v_bars.z);\n"
    "        int row = v_param.y;\n"
    "        int i = px - v_rect.x;\n"
    "        int y = py - v_param.z;\n"
    "        int k0 = 4 * y - PIX0;\n"
    "        int ylo = 4 * y;\n"
    "        int yhi = ylo + 3;\n"
    // THE SUPPORT'S BARS (the head's fast paths): jA .. jB bracket the
    // subcolumns 4i − GROW .. 4i + 3 + GROW, and jF is the pixel's first
    // subcolumn's lower bracket. One branch-free loop fetches them, each
    // into the hulls, the three from jF kept for the pixel's own
    // subcolumns; a repeat of jB changes nothing.
    "        int s0 = 4 * i - GROW;\n"
    "        int jA = int(floor(bar_u(s0, v_bars.x, v_inv.x)));\n"
    "        int jB = int(floor(bar_u(s0 + NO - 1, v_bars.x, v_inv.x))) + 1;\n"
    "        int jF = int(floor(bar_u(4 * i, v_bars.x, v_inv.x)));\n"
    "        ivec4 own0 = ivec4(0);\n"
    "        ivec4 own1 = ivec4(0);\n"
    "        ivec4 own2 = ivec4(0);\n"
    // THE WALK'S SEEDS, bars jA and jA + 1: the loop's d = 0 and d = 1 fetch
    // exactly them — jB >= jA + 1 always (the last subcolumn's u is at
    // least the first's, so its j0 + 1 is at least jA + 1), so the clamp
    // leaves both, and NU >= 3 at every grow, so both are written.
    "        ivec4 bl = ivec4(0);\n"
    "        ivec4 br = ivec4(0);\n"
    "        int tmin_o = 1 << 20;\n"
    "        int bmax_o = -(1 << 20);\n"
    "        int tmin_i = 1 << 20;\n"
    "        int bmax_i = -(1 << 20);\n"
    "        UNROLL for (int d = 0; d < NU; ++d) {\n"
    "            int j = min(jA + d, jB);\n"
    "            ivec4 c = bar_at(j, n, row);\n"
    "            if (c.y > c.x) {\n"
    "                tmin_o = min(tmin_o, c.x);\n"
    "                bmax_o = max(bmax_o, c.y);\n"
    "            }\n"
    "            if (c.w > c.z) {\n"
    "                tmin_i = min(tmin_i, c.z);\n"
    "                bmax_i = max(bmax_i, c.w);\n"
    "            }\n"
    "            if (j == jF) own0 = c;\n"
    "            if (j == jF + 1) own1 = c;\n"
    "            if (j == jF + 2) own2 = c;\n"
    "            if (d == 0) bl = c;\n"
    "            if (d == 1) br = c;\n"
    "        }\n"
    "        bool reach_o = yhi >= tmin_o - GROW && ylo <= bmax_o - 1 + GROW;\n"
    "        bool reach_i = yhi >= tmin_i - GROW && ylo <= bmax_i - 1 + GROW;\n"
    // The fast paths in the head's order: (a) the discard; (b) the opaque
    // intersection, by the own bars alone; then by the own subcolumns (c)
    // the full inner, the core opaque, (d) the full outer and (e) the ink
    // opaque.
    "        if (!reach_o && !reach_i) discard;\n"
    // THE OPAQUE INTERSECTION (architect 2026-11-14, send I; an exact early
    // return that stands on its own — without it the certificates below
    // give the same words): where the inner of ALL THREE bars from jF holds
    // the pixel it is full in the inner family, the core's word; where no
    // inner reaches and the outer of all three holds it, the ink's word —
    // before any own subcolumn is placed. EXACT: each own subcolumn's two
    // brackets are among those three bars (at a px or more a bar the four
    // subcolumns span 3/4 of a bar at most: lower brackets jF or jF + 1, a
    // quarter bar clear of any rounding), and where both brackets hold the
    // subcolumn is full (sub_full's first case). A slot the loop above did
    // not write is the zero texel, absent: it fails the test, never passes
    // it.
    "        if (reach_i && holds(own0.zw, ylo) && holds(own1.zw, ylo) &&\n"
    "            holds(own2.zw, ylo)) {\n"
    "            o_color = vec4(word_rgb(v_words.y), 1.0);\n"
    "            return;\n"
    "        }\n"
    "        if (!reach_i && reach_o && holds(own0.xy, ylo) &&\n"
    "            holds(own1.xy, ylo) && holds(own2.xy, ylo)) {\n"
    "            o_color = vec4(word_rgb(v_words.x), 1.0);\n"
    "            return;\n"
    "        }\n"
    // The pixel's own four subcolumns: full in a family where each is
    // (sub_full); e the subcolumn's lower bracket from jF, 0 or 1 at a px
    // or more a bar — another certifies nothing and takes the roads. THE
    // CERTIFICATES ARE REACH-GATED (architect 2026-11-14, send I): a family
    // that does not reach the pixel cannot be full there (a bar that holds
    // the pixel is in the hull), so each flag starts at its reach and a
    // family's sub_full is evaluated only while its flag stands.
    "        bool full_o = reach_o;\n"
    "        bool full_i = reach_i;\n"
    "        UNROLL for (int m = 0; m < 4; ++m) {\n"
    "            float u = bar_u(4 * i + m, v_bars.x, v_inv.x);\n"
    "            float fl = floor(u);\n"
    "            float f = u - fl;\n"
    "            int e = int(fl) - jF;\n"
    "            ivec4 a = (e == 0) ? own0 : own1;\n"
    "            ivec4 b = (e == 0) ? own1 : own2;\n"
    "            bool ok = e == 0 || e == 1;\n"
    "            bool near_a = f < 0.5;\n"
    "            bool core_a = f * v_bars.y < 0.5;\n"
    "            bool core_b = (1.0 - f) * v_bars.y <= 0.5;\n"
    "            if (full_o)\n"
    "                full_o = ok &&\n"
    "                         sub_full(a.xy, b.xy, near_a, core_a, core_b, ylo);\n"
    "            if (full_i)\n"
    "                full_i = ok &&\n"
    "                         sub_full(a.zw, b.zw, near_a, core_a, core_b, ylo);\n"
    "        }\n"
    "        if (full_i) {\n"
    "            o_color = vec4(word_rgb(v_words.y), 1.0);\n"
    "            return;\n"
    "        }\n"
    "        if (full_o && !reach_i) {\n"
    "            o_color = vec4(word_rgb(v_words.x), 1.0);\n"
    "            return;\n"
    "        }\n"
    // (f) The roads: the outer's work where it reaches and the full outer
    // does not hold (full_o implies reach_o), the inner's where it reaches;
    // every road ends in the one composite below.
    "        uint wo = full_o ? 0xFFFFu : 0u;\n"
    "        uint wi = 0u;\n"
    "        bool need_o = reach_o && !full_o;\n"
    // THE BASES over 4i − GROW .. 4i + 3 + GROW, ONE WALK FOR BOTH FAMILIES
    // (the head's cost paragraph): the brackets a rolling pair of scalars,
    // bl and br — seeded with bars jA and jA + 1 out of the support's loop
    // above, no fetch of their own; jl the left's index, on a step the right
    // becomes the left and one bar is fetched — each subcolumn's base
    // written at its constant index.
    "        int jl = jA;\n"
    "        UNROLL for (int t = 0; t < NO; ++t) {\n"
    "            float u = bar_u(s0 + t, v_bars.x, v_inv.x);\n"
    "            float fl = floor(u);\n"
    "            int j0 = int(fl);\n"
    "            if (j0 != jl) {\n"
    "                bl = br;\n"
    "                br = bar_at(j0 + 1, n, row);\n"
    "                jl = j0;\n"
    "            }\n"
    "            float f = u - fl;\n"
    "            int fq = int(floor(f * 256.0 + 0.5));\n"
    "            bool near_a = f < 0.5;\n"
    "            bool core_a = f * v_bars.y < 0.5;\n"
    "            bool core_b = (1.0 - f) * v_bars.y <= 0.5;\n"
    "            if (need_o)\n"
    "                bo[t] = base_bits(bl.xy, br.xy, fq, near_a, core_a, core_b, k0);\n"
    "            if (reach_i)\n"
    "                bi[t] = base_bits(bl.zw, br.zw, fq, near_a, core_a, core_b, k0);\n"
    "        }\n"
    // The outer's dilation at the pixel's four subcolumns (bo's index m +
    // GROW) folded at once into the count word wo (four pixel nibbles).
    "        if (need_o) {\n"
    "            UNROLL for (int m = 0; m < 4; ++m)\n"
    "                wo |= pix4(dilate_o(m + GROW)) << (4 * m);\n"
    "        }\n"
    // The inner's, into wi — and into wo, the composite's shape being the
    // union of the two grown bars.
    "        if (reach_i) {\n"
    "            UNROLL for (int m = 0; m < 4; ++m) {\n"
    "                uint i4 = pix4(dilate_i(m + GROW));\n"
    "                wo |= i4 << (4 * m);\n"
    "                wi |= i4 << (4 * m);\n"
    "            }\n"
    "        }\n"
    "        int n_o = pop16(wo);\n"
    "        int n_i = pop16(wi);\n"
    "        if (n_o == 0) discard;\n"
    "        float cov_o = float(n_o) / 16.0;\n"
    "        float cov_i = float(n_i) / 16.0;\n"
    // The head's two arms: the exact composite where the inner reaches a
    // pixel the union does not fill, the former expression literally
    // elsewhere (the same value there, its evaluation preserved; the bytes
    // verified unchanged on the tablet, the head).
    "        if (n_i > 0 && n_o < 16) {\n"
    "            o_color = vec4(word_rgb(v_words.x) * (cov_o - cov_i) +\n"
    "                           word_rgb(v_words.y) * cov_i,\n"
    "                           cov_o);\n"
    "        } else {\n"
    "        o_color = vec4(word_rgb(v_words.x) * (cov_o * (1.0 - cov_i)) +\n"
    "                       word_rgb(v_words.y) * cov_i,\n"
    "                       1.0 - (1.0 - cov_o) * (1.0 - cov_i));\n"
    "        }\n"
    "        return;\n"
    "    }\n"
    // THE TINT (the head's tint paragraph): the word at the whole-percent
    // alpha times the pixel's exact coverage by the record's real edges
    // (v_bars, from the rect's corner), premultiplied, for the blend to lay
    // over what lies beneath. Each statement is one operation of the head's
    // list, in its order.
    "    if (kind == 2) {\n"
    "        float lx = float(px - v_rect.x);\n"
    "        float ly = float(py - v_rect.y);\n"
    "        float cx = clamp(min(lx + 1.0, v_bars.z) - max(lx, v_bars.x),\n"
    "                         0.0, 1.0);\n"
    "        float cy = clamp(min(ly + 1.0, v_bars.w) - max(ly, v_bars.y),\n"
    "                         0.0, 1.0);\n"
    "        float a = (float(v_words.y) / 100.0) * (cx * cy);\n"
    "        o_color = vec4(word_rgb(v_words.x) * a, a);\n"
    "        return;\n"
    "    }\n"
    "    o_color = vec4(word_rgb(v_words.x), 1.0);\n"
    "}\n";

// THE SOURCE AT A GROW (the specialization rule, the head): the literal
// with the grow's digits at its one substitution point, GROW's definition.
std::string list_fragment_source(int grow) {
    constexpr std::string_view token = "@GROW@";
    constexpr std::string_view literal = kListFragmentShader;
    constexpr std::size_t at = literal.find(token);
    static_assert(at != std::string_view::npos &&
                      literal.find(token, at + token.size()) ==
                          std::string_view::npos,
                  "kListFragmentShader has exactly one substitution point");
    assert(grow >= 0 && grow <= kWaveformGrowMaxQuarters);
    std::string source(literal.substr(0, at));
    source += std::to_string(grow);
    source += literal.substr(at + token.size());
    return source;
}

// THE SHADER'S LITERALS ARE render.h's CONSTANTS: the shader is a string, so
// this is the binding. kWaveformSubcells is the 4 of `4 * y` and `4 * i`,
// the 0.25 and the 0.5 of a subcolumn's centre (bar_u), the pixel's nibble
// and the 16 of the coverage; EACH ARM'S PITCH FLOOR IS ITS OWN (architect
// 2026-11-14, send I). A UNIFORM ROW'S, 1 px — half the switch width
// (kWaveformSwitchPx, render.h's waveform_bar_frames: a rung's own bars
// stand a px or more apart) — is what makes NU's (NO − 1) / 4 the support's
// span in bars (NO − 1 quarter px at a px or more a bar), keeps the pixel's
// own subcolumns' brackets within the three bars from jF, and lets the
// uniform arm's brackets roll one bar a step. THE FADE BAND'S FINE ROW'S,
// 1/2 px — half the `waveform_fade_px` key's least
// (kWaveformFadeMinHundredths, device_config.h: the fine row is drawn at
// half the main rung's pitch, which is over the key's value there,
// render.h's waveform_fade_band; architect 2026-11-14, send K) — is the 4
// of NM's (NO − 1) / 4, the merged arm's pairs standing twice it apart, a
// px, and what keeps the pixel's own subcolumns within the four bars from
// aF and bounds its roll at two bars a step; the grow's disc is GROW's
// (disc_rows: the largest g with g² + dx² <= r², the level the Horner
// scheme selects), its top kWaveformGrowMaxQuarters = 8 what the one
// 32-bit window and the pixel's seat in it, PIX0 = 14, were sized for —
// THE WINDOW HOLDS THE GROW'S 2 PX ALONE (2026-11-04, the core: no erosion
// reads past the grown inner): the dilated masks are honest on bits GROW
// .. 31 − GROW, which hold the pixel's bits 14 .. 17 at the top grow.
constexpr int disc_rows(int r, int dx) {
    int g = 0;
    while ((g + 1) * (g + 1) + dx * dx <= r * r) ++g;
    return g;
}
constexpr int kPlatePixelBit = 14;
static_assert(kWaveformSubcells == 4,
              "kListFragmentShader spells the 4x grid as literals");
static_assert(kWaveformGrowMaxQuarters <= kPlatePixelBit &&
                  kPlatePixelBit + kWaveformSubcells +
                          kWaveformGrowMaxQuarters <=
                      32,
              "kListFragmentShader's one-word window holds the pixel's "
              "subrows honestly at the key's top grow");
static_assert(disc_rows(kWaveformGrowMaxQuarters, kWaveformGrowMaxQuarters) == 0,
              "the dilation's Horner scheme ends with no final grow: g(r) = 0");
static_assert(kWaveformSwitchPx == 2,
              "kListFragmentShader's NU, the uniform arm's three own bars "
              "and its rolling brackets spell a uniform row's least pitch, "
              "half the plate's switch width, as 1 px");
static_assert(2 * kWaveformFadeMinHundredths == 200,
              "kListFragmentShader's NM, the merged arm's four own bars and "
              "its rolling brackets spell the fade band's least pitch, half "
              "the waveform_fade_px key's floor, as 1/2 px");
// THE RECORDS' KINDS ARE THE SHADERS' LITERALS (GuiCanvasKind,
// canvas_sink.h): kListVertexShader takes a plate's reciprocals at `a_param.x
// == 1`, kListFragmentShader its plate arms at `kind == 1` and the tint's at
// `kind == 2`, a rect's the fall-through — and the tint's four edges fill
// the one float attribute the plate's bars ride (CanvasInstance below).
static_assert(static_cast<int>(GuiCanvasKind::Rect) == 0 &&
                  static_cast<int>(GuiCanvasKind::Plate) == 1 &&
                  static_cast<int>(GuiCanvasKind::Tint) == 2,
              "the list shaders spell the records' kinds as literals");
static_assert(sizeof(GuiCanvasPrimitive::edges) == 4 * sizeof(float),
              "a tint's edges are the four floats of the instance's bars "
              "attribute");

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
// 0), {kind, period, origin, 0} (location 1), {word, word2} (location 2),
// every coordinate in surface px (the GUI's own px: the app paints no window
// frame since 2026-10-24, so the two are one), and THE PLATE'S BARS
// (location 3, floats; architect 2026-11-14, sends G and H): {x, pitch,
// bars, weight} — the bars' pitch in device px, the row's bar count and the
// row's weight, and x in device px from the rect's left edge: the centre of
// the row's first bar where the weight is 1, a uniform row, and the first
// pair's parent centre where it is under 1, the fade band's
// (GuiCanvasPrimitive's bars_x0, bars_pitch, bars and bars_weight) — on A
// TINT THE SAME FOUR FLOATS ARE ITS REAL EDGES {x0, y0, x1, y1} in device px
// from the rect's corner (architect 2026-11-14, send J;
// GuiCanvasPrimitive's edges), and zero on a rect.
struct CanvasInstance {
    GLint   rect[4];
    GLint   param[4];
    GLuint  words[2];
    GLfloat bars[4];
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
// program's rule (every program, ensure_programs).
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

// THE LIST'S ATTRIBUTES (the instance fields at CanvasInstance, one rule at
// one site): on the bound vertex array, over the instance buffer `vbo`,
// starting `base` records in — the list's and the ahead kit's arrays at 0,
// the suffix's at the frame's prefix (send M, 2026-11-15) — one instance a
// record. The buffer's NAME and the offset are what the array keeps, so a
// glBufferData on the same name later leaves it pointing at the new storage.
void bind_list_attributes(GLuint vbo, int base) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const GLsizei stride = static_cast<GLsizei>(sizeof(CanvasInstance));
    const std::size_t at =
        static_cast<std::size_t>(base) * sizeof(CanvasInstance);
    glVertexAttribIPointer(0, 4, GL_INT, stride,
        reinterpret_cast<const void*>(at + offsetof(CanvasInstance, rect)));
    glVertexAttribIPointer(1, 4, GL_INT, stride,
        reinterpret_cast<const void*>(at + offsetof(CanvasInstance, param)));
    glVertexAttribIPointer(2, 2, GL_UNSIGNED_INT, stride,
        reinterpret_cast<const void*>(at + offsetof(CanvasInstance, words)));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(at + offsetof(CanvasInstance, bars)));
    for (GLuint loc = 0; loc < 4; ++loc) {
        glEnableVertexAttribArray(loc);
        glVertexAttribDivisor(loc, 1);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// A PLATE'S BOUNDS TEXTURE (the list's and the ahead kit's, one rule): on
// unit 1, GL_RGBA16I, NEAREST (an integer texture with a filtering
// minifier is incomplete), seeded 1 x 1 so the sampler is complete before
// the first plate, its capacity grown by the first pack (pack_list); unit 0
// active at the return.
void make_bounds_texture(GLuint& name, int& w, int& h) {
    glGenTextures(1, &name);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    const GLshort seed[4] = {};
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16I, 1, 1, 0, GL_RGBA_INTEGER,
                 GL_SHORT, seed);
    w = 1;
    h = 1;
    glActiveTexture(GL_TEXTURE0);
}

// A COMPOSE TARGET (the frame target's and the ahead target's, one rule;
// the rule at ensure_texture): a framebuffer object whose one color
// attachment is an immutable GL_RGBA8 texture of w x h, made on unit 3 and
// unbound from it at once; an incomplete target is fatal. The default
// framebuffer bound and unit 0 active at the return.
void make_compose_target(const GuiGlCanvasHost& host, const char* what,
                         GLuint& fbo, GLuint& texture, int w, int h) {
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (texture) glDeleteTextures(1, &texture);
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        char line[160];
        std::snprintf(line, sizeof(line),
                      "the GL canvas: the %s %dx%d is incomplete (0x%x)",
                      what, w, h, static_cast<unsigned>(status));
        canvas_fatal(host, line);
    }
}

} // namespace

// THE LIST PROGRAM AT A GROW (the declaration's rule, gl_canvas.h): the
// specialized source linked, the sampler on unit 1, the two uniforms, and
// the grow kept beside the program as the key present_frame compares.
void GuiGlCanvas::build_list_program(int grow) {
    const std::string source = list_fragment_source(grow);
    const GLuint prog =
        link_canvas_program(host_, kListVertexShader, source.c_str());
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "u_columns"), 1);
    list_size_uniform_   = glGetUniformLocation(prog, "u_size");
    list_height_uniform_ = glGetUniformLocation(prog, "u_height");
    list_program_        = prog;
    list_program_grow_   = grow;
}

// THE CONTEXT'S FIXED STATE, set at each make current (the platform's
// surface road calls it right after its make current): nothing dithers or
// tests, and THE BLEND IS OFF AT REST — its function set once here, the
// premultiplied source-over the plate's fringe and the tint need
// (kListFragmentShader's composite and tint arm), and enabled for the canvas list's draw alone (present_frame,
// 2026-10-30) — so a texel, an opaque list word and the copy's texel
// (architect 2026-11-15, send L) reach the framebuffer as written.
void GuiGlCanvas::bind_fixed_state() {
    glDisable(GL_DITHER);
    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
}

// THE FOUR PROGRAMS, built at the first make current and kept with the
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
    // program — at the plate's construction grow
    // (kWaveformGrowDefaultQuarters); a list at another grow has it
    // rebuilt at its first draw (present_frame, build_list_program) — its
    // own vertex array — the instance attributes never touch
    // the default one the texture triangle draws from — over one instance
    // buffer (three integer attributes and a float one, the plate's bars or
    // a tint's edges, CanvasInstance), and the plate's bounds texture on
    // unit 1 (make_bounds_texture: GL_RGBA16I,
    // NEAREST: an integer texture with a filtering minifier is incomplete),
    // seeded 1 x 1 so the sampler is complete before the first plate, its
    // capacity grown by the first pack (upload_list).
    // WITH THEM (send M, 2026-11-15; present_frame's THE AHEAD TARGET): THE
    // AHEAD KIT's vertex array over its own instance buffer and its own
    // bounds texture, made by the same two routines, and THE SUFFIX'S vertex
    // array over the list's instance buffer, pointed at the adopting frame's
    // prefix when one first adopts (suffix_base_ −1 until then).
    if (!list_program_) {
        build_list_program(kWaveformGrowDefaultQuarters);
        glGenVertexArrays(1, &list_vao_);
        glGenBuffers(1, &list_vbo_);
        glBindVertexArray(list_vao_);
        bind_list_attributes(list_vbo_, 0);
        glBindVertexArray(0);
        make_bounds_texture(columns_texture_, columns_w_, columns_h_);
        // The seed is no upload: the first plate packs whatever its key.
        columns_uploaded_ = false;

        glGenVertexArrays(1, &ahead_vao_);
        glGenBuffers(1, &ahead_vbo_);
        glBindVertexArray(ahead_vao_);
        bind_list_attributes(ahead_vbo_, 0);
        glBindVertexArray(0);
        make_bounds_texture(ahead_columns_texture_, ahead_columns_w_,
                            ahead_columns_h_);
        ahead_kit_ = false;

        glGenVertexArrays(1, &suffix_vao_);
        suffix_base_ = -1;
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
    // THE COPY'S PROGRAM (architect 2026-11-15, send L; kCopyFragmentShader's
    // head): the texture triangle's vertex shader, its sampler on unit 3,
    // the unit the frame target's texture is bound to for the copy alone.
    if (!copy_program_) {
        const GLuint prog =
            link_canvas_program(host_, kCanvasVertexShader, kCopyFragmentShader);
        glUseProgram(prog);
        glUniform1i(glGetUniformLocation(prog, "u_frame"), 3);
        copy_program_ = prog;
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
// THE FRAME TARGET FOLLOWS THE TEXTURE (architect 2026-11-15, send L; the
// rule at present_frame's THE FRAME TARGET): made and remade here, with
// texture_ and by its size test — one rule, one site — a framebuffer object
// whose one color attachment is an immutable GL_RGBA8 texture of the same
// size, one level, NEAREST both ways (the copy's texelFetch reads no
// filter), no depth, no stencil, never sRGB, never multisampled; its
// texture made on unit 3 and unbound from it at once (it is bound to a unit
// for the copy alone), so unit 0 holds texture_ at the return as it always
// has. Its completeness is checked once, at the make: an incomplete target
// is fatal by the host's rule (canvas_fatal; no fallback road). The new
// target holds no frame (frame_seeded_ false), so the compose's verdict
// draws the next frame whole. THE AHEAD TARGET (send M, 2026-11-15) is made
// and remade beside it by the same routine (make_compose_target), holding
// nothing composed after the make. THE DEFAULT FRAMEBUFFER IS BOUND AT THE
// RETURN, unit 0 active, no texture of either target bound to any unit —
// the rest state present_frame keeps too.
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

    make_compose_target(host_, "frame target", frame_fbo_, frame_texture_, w,
                        h);
    frame_seeded_ = false;
    // THE AHEAD TARGET (send M, 2026-11-15; the rule at present_frame's THE
    // AHEAD TARGET): made and remade with the frame target, by the same
    // routine and the same size test, its completeness fatal alike; a
    // remade target holds nothing, so no column of it counts as composed.
    make_compose_target(host_, "ahead target", ahead_fbo_, ahead_texture_, w,
                        h);
    ahead_composed_ = 0;
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
// THE DRAW, INTO THE FRAME TARGET (its paragraph below): the canvas list
// first, then the texture over it. THE LIST
// (kListVertexShader's head; GuiCanvasPrimitive, canvas_sink.h): its
// records as instances in list order, one instanced draw, the scissor
// the canvas clip, UNDER THE BLEND (GL_BLEND enabled for this draw alone
// and disabled after, its function the fixed state's premultiplied
// source-over, bind_fixed_state): the plate's antialiased fringe blends
// with the ground and the grid drawn before it, a tint blends over what
// lies beneath by its percent and each pixel's coverage (a line at its
// exact place, the sweep highlight; architect 2026-11-14, send J), and
// every pixel written at alpha 1 — an opaque rect's, a tint's at 100
// percent where it covers the pixel whole — lands exactly as it would
// unblended (2026-10-30) — its
// instances re-made only when the list's generation moved, and the plate's
// bounds texture, inside that, only when the plate's key moved
// (upload_list). THE TEXTURE: one full-screen
// triangle over the surface's own viewport, the fragment shader placing
// each texel at its pixel and DISCARDING an alpha-0 one, so the canvas
// shows wherever the backbuffer's interior is clear and every other
// pixel is the backbuffer's (kCanvasFragmentShader). The frame is
// cleared first, opaque black: with discards a pixel neither draw writes
// would otherwise keep the target's last frame's bytes, though by the
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
// the framebuffer is plain RGBA — the frame target a GL_RGBA8 texture, the
// window's buffer, which the copy writes, in the passthrough colorspace
// that encodes nothing — never an sRGB framebuffer, which is what makes the
// plate's blend the byte-space blend ruled; inside the clip it is the
// list's words, each a chrome pixel's bytes for its color by the sink's
// construction, on the
// plate's fringe their coverage blend, and under a tint the blend toward
// its word (kListFragmentShader).
//
// THE FRAME TARGET (architect 2026-11-15, "Send L."; the platform's half,
// the buffer-age history and the measurement, at GuiPlatform::present):
// THE CANVAS KEEPS ITS OWN FRAME. The draw above runs into one persistent
// offscreen target (ensure_texture's: an RGBA8 texture of the surface's
// size behind a framebuffer object), which holds THE WHOLE PICTURE OF THE
// LAST FRAME COMPOSED, all four channels — the bytes a whole draw into the
// target leaves — and the window's buffer is brought up to date by
// COPYING out of it, never by drawing the passes again. (Measured on the
// tablet 2026-11-15: those bytes are within one or two levels of a draw
// straight into the window buffer on partly covered pixels alone, the
// tablet's ANGLE rounding a blend into the target the other way there;
// opaque pixels never differ. The architect accepted it 2026-11-15, the
// new picture the reference since.) Two regions, both
// in surface px, top-left:
//   * THE COMPOSE, over `compose`, THIS FRAME'S OWN CHANGE (the platform's:
//     its damage, or the whole surface where the damage does not describe
//     it). Empty: the whole frame — the unscissored clear, the list or the
//     page under the clip's scissor, the texture over the whole surface —
//     after which the target holds a whole frame (frame_seeded_). Otherwise
//     each of its rects is RECOMPOSED from its own clear through the whole
//     pass order — the clear, the list under the rect ∩ the clip, the
//     texture — under a scissor to the rect, one rect whole before the
//     next; every pixel outside the region is the target's own, the last
//     frame's on the same passes. THE TARGET'S BYTES ARE THE WHOLE DRAW'S:
//     every pass is deterministic from the clear at the same absolute
//     coordinates on the same state, the plate's shader reads its column
//     data and never the framebuffer, the blends (the plate's fringe, the
//     tint) read the framebuffer only through the GL blend over the records
//     this same pass drew there since the clear, and a scissor changes no
//     fetch, coverage or blend; an overlap of two rects is cleared again
//     before it is drawn again, so nothing blends twice. A partial compose
//     needs the target to hold the frame before it: the verdict
//     (compose_must_be_whole) composes whole until a whole compose has
//     seeded it, and a remade target is unseeded.
//   * THE COPY, over `repair`, the region the platform's buffer-age history
//     says the acquired window buffer is missing (this frame's change and
//     the changes of the frames since the buffer was last written): with
//     the default framebuffer bound, the copy program (kCopyFragmentShader)
//     draws the texture triangle over it — one triangle with no scissor for
//     an empty `repair`, else one scissored triangle a rect, each clamped to
//     the surface — the target's texture bound on unit 3 for the copy alone
//     and unbound after, no clear (every pixel of the region is written),
//     the blend off. THE WINDOW'S BYTES ARE THE TARGET'S: each copied pixel
//     is the target's texel fetched without a filter and written unblended
//     and undithered to an RGBA8 buffer of the same format, and every other
//     pixel of the buffer is an earlier frame's copy of the same target
//     pixel, unchanged since (the history's rule).
// THE ALPHA the copy carries is known: the clear writes 1, the list's blend
// (GL_ONE, GL_ONE_MINUS_SRC_ALPHA on all four channels) over that 1 leaves
// a + 1 · (1 − a) = 1 for every record's alpha a — an opaque rect's 1, a
// plate fragment's coverage, a tint's percent times its coverage — the
// texture's triangle and the page write 1 unblended, and a discard leaves
// the 1 beneath; so the target's alpha, like a whole draw's, is 255, and no
// RGB blend reads the destination's alpha at all.
// THE AHEAD TARGET (send M, 2026-11-15; architect 2026-11-15: "do the next
// page, draw and ahead. Even if it's only as insurance for a later future
// iteration of the project on a more powerful tablet, where I may try to
// squeeze even more performance out of it"): while a paging play stands
// the GUI hands the next page's PREFIX — the four passes over the next
// page's plate, the ahead list (gui_canvas_ahead, canvas_sink.h) — beside
// the frame's list, and its picture is composed AHEAD into a second
// target, ensure_texture's twin of the frame target, so the turn copies
// it. THE KIT (gl_canvas.h) is the ahead list as the GPU reads it, packed
// by the frame list's own routine (pack_list), with a CPU copy of WHAT THE
// TARGET HOLDS: the records and bounds it was composed from, the clip, the
// grow, and how many of the clip's columns are composed. THE AHEAD STEP
// (ahead_step), after the copy, at every frame whose list draws: an empty
// ahead list does nothing; an ahead list whose content differs from the
// copy is uploaded into the kit and composes from nothing; then, the
// standing program at the kit's grow, one slice of the clip a frame
// (kAheadSlices) is cleared and drawn into the ahead target, under its
// scissor, from the kit, with the frame's own program, uniforms, viewport
// and blend (draw_instances).
// THE ADOPTION: once a frame, before the compose, THE VERDICT
// (adoption_verdict) — the list draws, its prefix stands and is the kit's
// record count, the target is composed whole over the kit's clip and that
// clip and grow are the frame's, the kit's first record is a rect over the
// clip, the prefix's records equal the kit's field for field, and the
// bounds they read equal the kit's bar for bar. In a frame that adopts,
// wherever the compose draws the list under a scissor c (the whole
// compose's clip, a partial compose's rect ∩ clip), IN ITS PLACE: the ahead
// target's texels COPIED over c (the copy program, the blend off, the ahead
// target's texture on unit 3 for the copy alone), then THE SUFFIX — the
// list's records from the prefix on — drawn over them under c with the
// blend, the list program and the frame's bounds texture, in list order,
// through a vertex array over the frame's instance buffer whose attributes
// start the prefix's count of records in (suffix_vao_). The clear before
// it and the texture pass after it are the compose's own.
// WHY THE BYTES ARE THE PLAIN ROAD'S, clause by clause: the prefix's first
// record is the canvas's ground, an opaque rect over the clip (the verdict
// checks it), written at alpha 1 under the blend (src · 1 + dst · 0), so
// every pixel of c after the prefix is a function of the prefix's records
// and bounds alone, never of the clear or of what the target held; the
// ahead target's pixels over the clip are that same function — drawn by the
// same program object (a grow change re-uploads the kit and the kit's slices
// wait for the program at its grow, so every slice and the frame draw with
// the program standing since its last rebuild), the same uniforms (the
// surface's size, the texture's height: the two targets are the texture's
// size) and viewport, at the same absolute coordinates, into an RGBA8
// target of the same format, from records equal field for field and bounds
// equal bar for bar in rows of the same index (the prefix's plate records
// are both lists' first, each one row of its own bounds texture in list
// order); NO INSTANCE READS ITS INDEX and NO FETCH DEPENDS ON THE BOUNDS
// TEXTURE'S SIZE (the list's two shaders name no gl_InstanceID and no
// textureSize; a plate's fetch is clamped to its own record's bar count),
// so the kit's other capacity and the suffix's offset change nothing; a
// scissor changes no fragment; the copy moves each texel exactly (texelFetch
// of an RGBA8 texel into an RGBA8 target, unblended, undithered; its alpha,
// like the frame target's, 255); and the suffix then blends over the same
// bytes in the same order the plain road's suffix does.
// NO FEEDBACK: no draw samples the texture attached to its own draw
// target. Unit 3 holds the ahead target's texture only during an
// adoption's copy, the frame target the draw target, and the frame
// target's texture only during the copy into the window, the default
// framebuffer the draw target; unit 3 is empty during every instanced list
// draw (the frame's list, the suffix, an ahead slice) and during the ahead
// target's composition. THE REST STATE at every return of present_frame
// and ensure_texture: the default framebuffer bound, unit 0 active, the
// scissor test and the blend off.
// The texture's upload is still THIS frame's damage alone (the texture is
// persistent, so every older pixel is already its own). IT STANDS ON THE
// DAMAGE CONTRACT: a canvas pixel the list changes outside every declared
// damage rect is not recomposed into the target, so every GPU-visible
// change of the list owes damage on its old and new pixels (the audit of
// 2026-11-04 in tmp/s33/done2.md; the owners' damage at each pass's
// comment). A change the damage cannot describe is composed whole by
// compose_must_be_whole's verdict, and its copy is whole (the platform).
// A FRAME COSTS the passes over its own change, once, into the target, and
// one fetch a pixel over the repair region: a page turn is one heavy frame
// and the next buffers' copies; a frame at rest draws nothing. A TURN THAT
// ADOPTS (send M) costs a fetch a pixel over the clip and the suffix's few
// lines in the plate's place, its heavy GPU work spread as eight slices
// over the page before it; outside a paging play the ahead list is empty
// and the verdict and the ahead step cost a handful of compares.
// THE FRAME'S CANVAS in surface px (the GUI's own: no window frame since
// 2026-10-24): an empty clip — no list yet, or a paint area the folder
// overlay emptied — leaves nothing out of any upload and draws no list.
// present_frame's and compose_must_be_whole's one reading.
void GuiGlCanvas::frame_clip(const GuiCanvasDrawList* list, int clip[4],
                             bool& floater) {
    clip[0] = clip[1] = clip[2] = clip[3] = 0;
    floater = false;
    if (list && list->clip.w > 0 && list->clip.h > 0) {
        clip[0] = list->clip.x;
        clip[1] = list->clip.y;
        clip[2] = list->clip.w;
        clip[3] = list->clip.h;
        floater = list->floater;
    }
}

bool GuiGlCanvas::ready(cairo_surface_t* back) const {
    return texture_ != 0 && back != nullptr &&
           cairo_image_surface_get_data(back) != nullptr;
}

// THE COMPOSE'S VERDICT (architect 2026-11-15, send L; the rule at
// present_frame's THE FRAME TARGET): the frame must be composed whole when
// the change it shows is not described by the damage — the texture has had
// no walk since it was made (a remade texture, the first frame), the canvas
// clip differs from the last walk's (a keyboard, a folder overlay, a modal
// box or the score viewer came or went: every pixel the clip gained or lost
// changes its pass), the score viewer's page stands or stood at the last
// frame (its textures arrive across presents with no damage of their own),
// or the list's grow differs from the standing program's (the plate is
// recompiled, so every fringe moves) — OR WHEN THE FRAME TARGET HOLDS NO
// WHOLE FRAME YET (made or remade since the last whole compose), since a
// partial compose keeps the target's other pixels as the frame before it.
// The platform's one caller (GuiPlatform::present) then names the whole
// surface as the frame's change, so the copy is whole too.
bool GuiGlCanvas::compose_must_be_whole(const GuiCanvasDrawList* list) const {
    if (!frame_seeded_) return true;
    if (!upload_seeded_) return true;
    int  clip[4];
    bool floater = false;
    frame_clip(list, clip, floater);
    for (int i = 0; i < 4; ++i)
        if (clip[i] != upload_clip_[i]) return true;
    if (page_stood_) return true;
    if (list && clip[2] > 0 && list->page.stands) return true;
    if (list && clip[2] > 0 && list->plate_grow != list_program_grow_)
        return true;
    return false;
}

bool GuiGlCanvas::present_frame(cairo_surface_t* back,
                                std::span<const GuiGlRect> damage,
                                std::span<const GuiGlRect> compose,
                                std::span<const GuiGlRect> repair,
                                GuiCanvasDrawList* list,
                                const GuiCanvasDrawList* ahead,
                                int surface_w, int surface_h) {
    if (!texture_) return false;
    const int stride_px = cairo_image_surface_get_stride(back) / 4;
    const auto* src = reinterpret_cast<const uint32_t*>(
        cairo_image_surface_get_data(back));
    if (!src) return false;

    int  clip[4];
    bool floater = false;
    frame_clip(list, clip, floater);
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

    const bool page_stands = list && clip[2] > 0 && list->page.stands;
    const bool list_draws  = list && clip[2] > 0 && !page_stands;
    if (list_draws) {
        if (!list_uploaded_ || list->generation != list_generation_)
            upload_list(*list);
        // THE GROW RIDES THE LIST (GuiCanvasDrawList::plate_grow, the
        // `waveform_grow` key in subcells) AND IS A CONSTANT OF THE
        // PROGRAM (kListFragmentShader's specialization rule): a list at
        // a grow the standing program was not compiled for has the
        // program rebuilt here, once — the sink stamps the key's seat on
        // every list, so the grow moves only at a commit of the key and
        // the compare is false at every other frame — and a commit of the
        // key is then the next frame's picture with no upload (the tips
        // do not move), composed whole (compose_must_be_whole).
        if (list_instances_ > 0 && list->plate_grow != list_program_grow_) {
            glDeleteProgram(list_program_);
            build_list_program(list->plate_grow);
        }
    }
    // THE VERDICT (send M, 2026-11-15; the rule at THE ADOPTION above):
    // once, before the compose, from what the kit's copy says the ahead
    // target holds now — the ahead step that may replace that copy runs
    // after the compose. An adopting frame points the suffix's vertex array
    // at its prefix.
    const bool adopt = list_draws && adoption_verdict(*list, clip);
    if (adopt && suffix_base_ != list->prefix) {
        glBindVertexArray(suffix_vao_);
        bind_list_attributes(list_vbo_, list->prefix);
        glBindVertexArray(0);
        suffix_base_ = list->prefix;
    }
    // THE LIST'S DRAW under the scissor (x, y, w, h) in surface px, top-left
    // — the canvas clip, or a compose rect's part of it. IN AN ADOPTING
    // FRAME, IN ITS PLACE (THE ADOPTION above): the ahead target's texels
    // copied over the same scissor — the copy program, the blend off, the
    // ahead target's texture on unit 3 for the copy alone — and then THE
    // SUFFIX, the list's records from the prefix on, drawn over them as the
    // list is, through the suffix's vertex array.
    const auto draw_list = [&](int x, int y, int w, int h) {
        if (w <= 0 || h <= 0 || list_instances_ <= 0) return;
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, texture_h_ - (y + h), w, h);
        if (adopt) {
            glUseProgram(copy_program_);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, ahead_texture_);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0);
            if (list_instances_ > list->prefix)
                draw_instances(suffix_vao_, columns_texture_,
                               list_instances_ - list->prefix, surface_w,
                               surface_h);
        } else {
            draw_instances(list_vao_, columns_texture_, list_instances_,
                           surface_w, surface_h);
        }
        glDisable(GL_SCISSOR_TEST);
    };
    const auto draw_texture = [&]() {
        glUseProgram(program_);
        glUniform1i(height_uniform_, texture_h_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };

    // THE COMPOSE, INTO THE FRAME TARGET (the rule at THE FRAME TARGET
    // above; architect 2026-11-15, send L): every pass below draws into the
    // target, over this frame's own change.
    glBindFramebuffer(GL_FRAMEBUFFER, frame_fbo_);
    if (compose.empty()) {
        // THE WHOLE COMPOSE: the clear, the list or the page under the
        // clip's scissor, the texture over the whole surface; the target
        // then holds a whole frame.
        glClear(GL_COLOR_BUFFER_BIT);
        if (page_stands)
            draw_page(list->page, clip);
        else if (list_draws)
            draw_list(clip[0], clip[1], clip[2], clip[3]);
        draw_texture();
        frame_seeded_ = true;
    } else {
        // THE PARTIAL COMPOSE (the rule at THE FRAME TARGET above): every
        // compose rect recomposed from its own clear through the same
        // passes in the same order — the clear, the list under the rect ∩
        // the clip, the texture — each rect whole before the next, so a
        // pixel two rects share is cleared again before it is drawn again
        // and never blends twice. Never with the page standing, and never
        // on a target that holds no whole frame (compose_must_be_whole).
        for (const GuiGlRect& r : compose) {
            const int rx0 = std::max(r.x, 0);
            const int ry0 = std::max(r.y, 0);
            const int rx1 = std::min(r.x + r.w, surface_w);
            const int ry1 = std::min(r.y + r.h, surface_h);
            if (rx1 <= rx0 || ry1 <= ry0) continue;
            glEnable(GL_SCISSOR_TEST);
            glScissor(rx0, texture_h_ - ry1, rx1 - rx0, ry1 - ry0);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
            if (list_draws) {
                const int cx0 = std::max(rx0, clip[0]);
                const int cy0 = std::max(ry0, clip[1]);
                const int cx1 = std::min(rx1, clip[0] + clip[2]);
                const int cy1 = std::min(ry1, clip[1] + clip[3]);
                draw_list(cx0, cy0, cx1 - cx0, cy1 - cy0);
            }
            glEnable(GL_SCISSOR_TEST);
            glScissor(rx0, texture_h_ - ry1, rx1 - rx0, ry1 - ry0);
            draw_texture();
            glDisable(GL_SCISSOR_TEST);
        }
    }
    page_stood_ = page_stands;

    // THE COPY, INTO THE WINDOW'S BUFFER (the rule at THE FRAME TARGET
    // above): the default framebuffer bound again — the rest state — and
    // `repair` copied out of the target, the target's texture on unit 3 for
    // the copy alone; one triangle for the whole surface, else one under
    // each rect's scissor, clamped to the surface as the compose's rects
    // are. No clear: every pixel of the region is written.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(copy_program_);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, frame_texture_);
    if (repair.empty()) {
        glDrawArrays(GL_TRIANGLES, 0, 3);
    } else {
        for (const GuiGlRect& r : repair) {
            const int rx0 = std::max(r.x, 0);
            const int ry0 = std::max(r.y, 0);
            const int rx1 = std::min(r.x + r.w, surface_w);
            const int ry1 = std::min(r.y + r.h, surface_h);
            if (rx1 <= rx0 || ry1 <= ry0) continue;
            glEnable(GL_SCISSOR_TEST);
            glScissor(rx0, texture_h_ - ry1, rx1 - rx0, ry1 - ry0);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glDisable(GL_SCISSOR_TEST);
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    // THE AHEAD STEP (send M, 2026-11-15; the rule at THE AHEAD TARGET
    // above): after the copy, at every frame whose list draws.
    if (list_draws && ahead) ahead_step(*ahead, clip, surface_w, surface_h);
    return true;
}

// THE LIST PROGRAM'S INSTANCED DRAW (the one spelling of the frame's list
// draw, the suffix's and the ahead slice's): `count` instances of the
// vertex array `vao` under the caller's scissor into the bound framebuffer,
// with the list program, its two uniforms (the surface's size, the
// texture's height), the bounds texture `columns_texture` on unit 1 and the
// blend enabled for the draw alone; unit 0 active and the vertex array
// unbound after.
void GuiGlCanvas::draw_instances(unsigned int vao,
                                 unsigned int columns_texture, int count,
                                 int surface_w, int surface_h) {
    glUseProgram(list_program_);
    glUniform2f(list_size_uniform_, static_cast<float>(surface_w),
                static_cast<float>(surface_h));
    glUniform1i(list_height_uniform_, texture_h_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, columns_texture);
    glBindVertexArray(vao);
    glEnable(GL_BLEND);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
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
    list_program_grow_   = -1;
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
    // The frame target and the copy's program (architect 2026-11-15, send
    // L): their names forgotten with the rest, the target unseeded.
    frame_fbo_           = 0;
    frame_texture_       = 0;
    frame_seeded_        = false;
    copy_program_        = 0;
    // The ahead target and its kit, and the suffix's array (send M,
    // 2026-11-15): forgotten with the frame target, the kit's copy emptied,
    // nothing composed.
    ahead_fbo_             = 0;
    ahead_texture_         = 0;
    ahead_vao_             = 0;
    ahead_vbo_             = 0;
    ahead_instances_       = 0;
    ahead_columns_texture_ = 0;
    ahead_columns_w_       = 0;
    ahead_columns_h_       = 0;
    ahead_kit_             = false;
    ahead_prims_.clear();
    ahead_columns_.clear();
    ahead_revision_        = 0;
    for (int& c : ahead_clip_) c = 0;
    ahead_grow_            = -1;
    ahead_composed_        = 0;
    verdict_cached_        = false;
    suffix_vao_            = 0;
    suffix_base_           = -1;
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
// them (a rect, a plate's row 0 alike), a plate's draw transform beside them
// as floats ({x, pitch, bars, weight}), a tint's real edges in the same four
// (zero on a rect), the instance
// buffer rewritten whole — asked only when the list's generation moved, so
// at every rebuild of the records (the scanner's and the cursor's among
// them; the buffer is small).
// A plate record's `period` is re-pointed at its row of THE PLATE'S BOUNDS
// TEXTURE — ONE ROW PER PLATE RECORD (2026-10-30), THE RECORD'S BARS left to
// right from texel 0, `bars` of them (architect 2026-11-14, send G: the row
// is the band's stored bars, more or fewer than its rect's device columns),
// each texel the bar's four quarter-row tips {o_top, o_bot, i_top, i_bot} as
// render_waveform_columns gathered them (GL_RGBA16I: a plate row times four
// of a canvas far under 8191 rows, the tablet's 1072 giving 4288; the absent
// inner is 0, 0).
// THE TEXTURE IS CAPACITY-SIZED AND ONLY GROWS: a row's bar count moves with
// the zoom on most gesture frames (the bars' pitch runs 1/2 .. 2 device px),
// so a texture sized to the count would be reallocated every frame. Its
// width is the widest row any pack has asked, rounded up to
// kColumnsCapacityStep texels, and its height the most rows any has (the
// tablet's plate, at most 2 · (2256 + 6) = 4524 bars in the fade band's row
// just past a landing, settles at 5120 x 2; architect 2026-11-14, send K);
// a growth reallocates with no data (glTexImage2D, a handful of times in a
// context's life) inside the pack that asked it, and every pack then
// writes, by one glTexSubImage2D, its rows by its own widest row, zero past
// a shorter row's bars. A texel past a row's bar count — a zero, an earlier
// pack's bar, or never written since a growth — is never read: the shader
// fetches inside [0, bars) alone.
// THE BOUNDS TEXTURE HAS ITS OWN LIFETIME (2026-10-14): it is packed and sent
// only when its KEY moved — the list's plate revision and the plate records'
// spans in list order (each its offset into the list's columns and its bar
// count, so their number too) — or when the texture holds no upload yet (the
// seed of ensure_programs, a context remade after forget()). THE KEY IS
// COMPLETE: every texel a record reads is a function of the spans and the
// list's columns at those spans, and the list's columns are a function of
// the revision alone — the sink copies the live plate's stored bands into
// them once per revision, and the live plate's bounds change only at a
// publication, which takes a new revision (GuiPlateColumns::revision,
// render.h; GuiCanvasSink::plate). THE CAPACITY IS NO PART OF THE KEY: it
// never shrinks, and it grows only inside a pack — a row wider than the
// capacity, or one row more, is a moved span — which rewrites every row, so
// an unmoved key always finds the texels its pack wrote. THE DRAW TRANSFORM
// IS NO PART OF IT EITHER: it rides the instances, rewritten whole at every
// generation. A list with no plate record leaves the texture and its key as
// they are: nothing samples it, and its texels still answer the key.
namespace {
constexpr int kColumnsCapacityStep = 1024;

// THE LIST'S PACKING (send M, 2026-11-15: ONE ROUTINE for the frame's list
// and the ahead kit, so the instance fields and the texel layout are one
// rule at one site; the rule is upload_list's head above): every record one
// instance in list order into the instance buffer `vbo` (rewritten whole,
// nothing sent for an empty list), and where `pack` and the list has plate
// records, their bounds into `columns_texture` — one row a plate record,
// grown to the capacity `columns_w` x `columns_h` when a row is wider or
// there is one row more. Each plate record's `period` is its row. Answers
// the instance count. Unit 0 active at the return.
int pack_list(const GuiCanvasDrawList& list, bool pack, GLuint vbo,
              GLuint columns_texture, int& columns_w, int& columns_h) {
    static std::vector<CanvasInstance> instances;
    static std::vector<GLshort>        texels;
    int max_n  = 1;
    int plates = 0;
    for (const GuiCanvasPrimitive& p : list.prims) {
        if (p.kind != GuiCanvasKind::Plate) continue;
        ++plates;
        max_n = std::max(max_n, p.bars);
    }
    const int rows = std::max(1, plates);
    pack = pack && plates > 0;
    if (pack)
        texels.assign(static_cast<size_t>(max_n) * rows * 4, 0);
    const auto texel = [&](int row, int col) {
        return &texels[(static_cast<size_t>(row) * max_n + col) * 4];
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
        in.param[3] = 0;
        in.words[0] = p.word;
        in.words[1] = p.word2;
        switch (p.kind) {
        case GuiCanvasKind::Rect:
            break;
        case GuiCanvasKind::Tint:
            // The tint's real edges, from the rect's corner (the head's
            // tint paragraph).
            in.bars[0] = p.edges[0];
            in.bars[1] = p.edges[1];
            in.bars[2] = p.edges[2];
            in.bars[3] = p.edges[3];
            break;
        case GuiCanvasKind::Plate:
            if (pack) {
                for (int j = 0; j < p.bars; ++j) {
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
            in.bars[0]  = p.bars_x0;
            in.bars[1]  = p.bars_pitch;
            in.bars[2]  = static_cast<GLfloat>(p.bars);
            in.bars[3]  = p.bars_weight;
            row += 1;
            break;
        }
        instances.push_back(in);
    }
    if (!instances.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(instances.size() *
                                             sizeof(CanvasInstance)),
                     instances.data(), GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    if (pack) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, columns_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        if (max_n > columns_w || rows > columns_h) {
            const int grown_w =
                (max_n + kColumnsCapacityStep - 1) / kColumnsCapacityStep *
                kColumnsCapacityStep;
            columns_w = std::max(columns_w, grown_w);
            columns_h = std::max(columns_h, rows);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16I, columns_w, columns_h,
                         0, GL_RGBA_INTEGER, GL_SHORT, nullptr);
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, max_n, rows,
                        GL_RGBA_INTEGER, GL_SHORT, texels.data());
        glActiveTexture(GL_TEXTURE0);
    }
    return static_cast<int>(instances.size());
}
} // namespace

void GuiGlCanvas::upload_list(const GuiCanvasDrawList& list) {
    static std::vector<std::pair<int, int>> spans;
    spans.clear();
    for (const GuiCanvasPrimitive& p : list.prims)
        if (p.kind == GuiCanvasKind::Plate) spans.emplace_back(p.period, p.bars);
    const bool pack =
        !spans.empty() &&
        !(columns_uploaded_ && list.plate_revision == columns_revision_ &&
          spans == columns_spans_);
    list_instances_ = pack_list(list, pack, list_vbo_, columns_texture_,
                                columns_w_, columns_h_);
    if (pack) {
        columns_uploaded_ = true;
        columns_revision_ = list.plate_revision;
        columns_spans_    = spans;
    }
    list_generation_ = list.generation;
    list_uploaded_   = true;
}

// -- THE NEXT PAGE DRAWN AHEAD (send M, 2026-11-15) -------------------------
//
// The rules are present_frame's THE AHEAD TARGET and THE ADOPTION; these are
// their two bodies.
namespace {
// ONE SLICE A FRAME: the ahead target is composed over ceil(clip.w /
// kAheadSlices) of the clip's device columns a frame. THE SLICE'S COST IS AN
// ESTIMATE, UNMEASURED: a lit page's whole plate measured about 14 ms of
// GPU at a turn (his local planner's page-turn runs, send L's return,
// 2026-11-15); an eighth of the clip a frame is that fragment work divided
// by eight, plus what a third render pass costs on the tablet's GPU (the
// ahead target's load and store under a scissored clear), which is
// unmeasured — the tablet's page-turn run measures it. The target is whole
// eight frames after its list arrives, early in a page that plays for about
// two seconds at the working zoom.
constexpr int kAheadSlices = 8;

// TWO RECORDS EQUAL FIELD FOR FIELD: every member, the floats by their bits
// (no tolerance and no −0 == +0: equal bits are equal shader inputs).
bool same_record(const GuiCanvasPrimitive& a, const GuiCanvasPrimitive& b) {
    const auto bits = [](float f) { return std::bit_cast<uint32_t>(f); };
    if (a.kind != b.kind || !(a.r == b.r) || a.period != b.period ||
        a.origin != b.origin || a.word != b.word || a.word2 != b.word2 ||
        a.bars != b.bars || bits(a.bars_x0) != bits(b.bars_x0) ||
        bits(a.bars_pitch) != bits(b.bars_pitch) ||
        bits(a.bars_weight) != bits(b.bars_weight))
        return false;
    for (int i = 0; i < 4; ++i)
        if (bits(a.edges[i]) != bits(b.edges[i])) return false;
    return true;
}

// Two bars' bounds equal, the four tips the texel packs.
bool same_bar(const WaveformColumnRows& a, const WaveformColumnRows& b) {
    return a.o_top == b.o_top && a.o_bot == b.o_bot && a.i_top == b.i_top &&
           a.i_bot == b.i_bot;
}
} // namespace

// THE VERDICT (THE ADOPTION's terms, in that order, the cheap first): the
// list's prefix stands and is the kit's record count; the kit holds an
// upload composed whole over its clip — the count the clip's width — and
// that clip and grow are the frame's; the kit's first record is a rect
// over the whole clip (the ground the byte argument stands on); the
// prefix's records equal the kit's field for field; and the bounds those
// records read equal the kit's, bar for bar — that last compare kept per
// pair of revisions (the frame list's plate revision against the kit's
// upload; the cache is dropped at every upload of the kit).
bool GuiGlCanvas::adoption_verdict(const GuiCanvasDrawList& list,
                                   const int clip[4]) {
    if (list.prefix <= 0 || !ahead_kit_) return false;
    if (static_cast<std::size_t>(list.prefix) != ahead_prims_.size())
        return false;
    for (int i = 0; i < 4; ++i)
        if (clip[i] != ahead_clip_[i]) return false;
    if (ahead_composed_ < ahead_clip_[2]) return false;
    if (list.plate_grow != ahead_grow_) return false;
    const GuiCanvasPrimitive& ground = ahead_prims_.front();
    if (ground.kind != GuiCanvasKind::Rect || ground.r.x > clip[0] ||
        ground.r.y > clip[1] || ground.r.x + ground.r.w < clip[0] + clip[2] ||
        ground.r.y + ground.r.h < clip[1] + clip[3])
        return false;
    for (std::size_t i = 0; i < ahead_prims_.size(); ++i)
        if (!same_record(list.prims[i], ahead_prims_[i])) return false;
    if (!verdict_cached_ || verdict_revision_ != list.plate_revision) {
        bool equal = true;
        for (const GuiCanvasPrimitive& p : ahead_prims_) {
            if (p.kind != GuiCanvasKind::Plate) continue;
            // The spans are the records', equal in both lists, and each
            // list's sink wrote its columns to cover its plate records'
            // spans (GuiCanvasSink::plate's offsets).
            const std::size_t lo = static_cast<std::size_t>(p.period);
            const std::size_t hi = lo + static_cast<std::size_t>(p.bars);
            assert(hi <= list.columns.size() && hi <= ahead_columns_.size());
            for (std::size_t j = lo; j < hi && equal; ++j)
                equal = same_bar(list.columns[j], ahead_columns_[j]);
            if (!equal) break;
        }
        verdict_cached_       = true;
        verdict_revision_     = list.plate_revision;
        verdict_bounds_equal_ = equal;
    }
    return verdict_bounds_equal_;
}

// THE AHEAD STEP (THE AHEAD TARGET's three cases, after the copy): an empty
// ahead list leaves the kit and the target as they are; an ahead list whose
// content — its records, field for field, its plate revision (a revision
// names one set of bounds, the records holding the spans), the frame's clip
// and its grow — differs from the kit's copy is uploaded into the kit
// (pack_list, the frame list's own packing), the copy replaced, nothing
// composed; then, with the standing list program at the kit's grow and the
// clip not composed whole, ONE SLICE — the next ceil(clip.w / kAheadSlices)
// columns by the clip's rows — cleared and drawn into the ahead target
// under its scissor from the kit, the frame's own program, uniforms,
// viewport and blend (draw_instances). The default framebuffer bound again,
// unit 0 active, the scissor and the blend off at the return; the ahead
// target's texture bound to no unit throughout.
void GuiGlCanvas::ahead_step(const GuiCanvasDrawList& ahead, const int clip[4],
                             int surface_w, int surface_h) {
    if (ahead.prims.empty()) return;
    bool differs = !ahead_kit_ || ahead.prims.size() != ahead_prims_.size() ||
                   ahead.plate_revision != ahead_revision_ ||
                   ahead.plate_grow != ahead_grow_;
    for (int i = 0; i < 4 && !differs; ++i)
        differs = clip[i] != ahead_clip_[i];
    for (std::size_t i = 0; i < ahead.prims.size() && !differs; ++i)
        differs = !same_record(ahead.prims[i], ahead_prims_[i]);
    if (differs) {
        ahead_instances_ = pack_list(ahead, /*pack=*/true, ahead_vbo_,
                                     ahead_columns_texture_, ahead_columns_w_,
                                     ahead_columns_h_);
        ahead_prims_   = ahead.prims;
        ahead_columns_ = ahead.columns;
        ahead_revision_ = ahead.plate_revision;
        for (int i = 0; i < 4; ++i) ahead_clip_[i] = clip[i];
        ahead_grow_     = ahead.plate_grow;
        ahead_composed_ = 0;
        ahead_kit_      = true;
        verdict_cached_ = false;
    }
    if (ahead_grow_ != list_program_grow_) return;
    if (ahead_composed_ >= ahead_clip_[2] || ahead_instances_ <= 0) return;
    const int slice = (ahead_clip_[2] + kAheadSlices - 1) / kAheadSlices;
    const int x = ahead_clip_[0] + ahead_composed_;
    const int w = std::min(slice, ahead_clip_[2] - ahead_composed_);
    glBindFramebuffer(GL_FRAMEBUFFER, ahead_fbo_);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, texture_h_ - (ahead_clip_[1] + ahead_clip_[3]), w,
              ahead_clip_[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_instances(ahead_vao_, ahead_columns_texture_, ahead_instances_,
                   surface_w, surface_h);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ahead_composed_ += w;
}
