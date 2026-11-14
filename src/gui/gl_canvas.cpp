#include "gl_canvas.h"

#include "canvas_sink.h"   // GuiCanvasDrawList, GuiCanvasPrimitive
#include "device_config.h" // kWaveformGrowMaxQuarters, the key's lattice (the plate's asserts)

#include <GLES3/gl3.h>

#include <algorithm>
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
// 2026-11-14, send G) — and blends
// source-over (kListFragmentShader's head; the rule at
// render_waveform_columns' head, render.h), and THE TINT (architect
// 2026-11-12, the sweep highlight), the canvas's second blend, a rect
// blended source-over toward its word under the same blend function.

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
// (u_height the surface's height, H) — every comparison in highp int. A pixel
// outside the instance's rect is discarded (the quad's float edges never
// decide coverage); a RECT (kind 0) writes its word; THE TINT (kind 2,
// architect 2026-11-12, the sweep highlight) writes its word premultiplied
// by its alpha a = word2 / 100 with alpha a, (word · a, a), so the blend
// glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA) lays G · (1 − a) + word · a
// on the pixel beneath, G — whatever the earlier records drew there, the
// plate's ink and core included — rounded once to a byte, the canvas's
// second blend and byte space like the first (the framebuffer is plain
// RGBA, never sRGB); THE PLATE (kind 1) is
// THE WAVEFORM'S ANTIALIASING (architect 2026-10-30;
// the rule at render_waveform_columns' head, render.h; the grow's key
// 2026-10-31, the 4x grid ruled for good that afternoon at render.h's
// kWaveformSubcells; THE CORE a solid fill of the grown inner since
// 2026-11-04; THE BARS ANCHORED TO THE AUDIO AND DRAWN STRETCHED at their
// pitch, architect 2026-11-14, send G, render.h's THE PLATE'S BARS), an
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
//     expression (GROW, 4, NO = 4 + 2 · GROW, NB), every disc condition a
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
//     of GPU time a plate frame on the tablet against the 11 ms budget; this
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
//     floats; p >= 3/4, the switch width's floor halved — the static_assert
//     after the source). THE PICTURE IS MADE IN SCREEN SUBCOLUMNS: s = 4i +
//     m of the pixel i = px − rect.x (m = 0 .. 3; a neighbor's s is
//     negative or past the rect at the plate's sides), its centre xs = (s +
//     0.5) / 4 px. ITS PLACE AMONG THE BARS is u = (xs − x0) / p, EVALUATED
//     IN HIGHP FLOAT AS ((float(s) + 0.5) * 0.25 − x0) / p — the product
//     exact, then one subtraction and one division (bar_u, the one function
//     every site calls, so every site holds the same u) — with j0 = floor(u)
//     and f = u − floor(u) in [0, 1] (that subtraction is exact but for a u
//     a hair under zero, where it rounds to 1): THE BRACKETS are bars j0 and
//     j0 + 1, the two whose centres stand either side of the subcolumn's. A
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
//     [T, B − 1]. BOTH brackets' cores join where both cover (p < 1); no
//     third bar's core reaches a subcolumn (p > 1/2). So a lone spike stands
//     flat-topped at its own height under its core at every pitch and
//     offset: the contour, which is sampled at the subcolumns' centres and
//     never at the bar's, cannot shave it.
//     AT p = 1 ON A WHOLE OFFSET (x0 ≡ 1/2 mod 1, a landing on a whole
//     level: every bar exactly on a pixel) THE BASES ARE ONE BAR A PIXEL'S,
//     BIT FOR BIT — the own bar whole in its pixel's four subcolumns and the
//     contour to the nearer neighbor in eighths, own : neighbor 5:3, 7:1,
//     7:1, 5:3, subrow k in it iff (t8 + 3) / 8 <= k <= (b8 − 4) / 8, the
//     plate's silhouette of 2026-10-30, so a landing keeps that picture's
//     bytes. xs − x0 is then a multiple of 1/8, exact in float, and u is it:
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
//     with s, and each float step of bar_u keeps the order). THEY ARE AT
//     MOST NB = (NO − 1) / 3 + 3: the last u exceeds the first by (NO − 1) /
//     (4p) <= (NO − 1) / 3 at p >= 3/4, the floors of two numbers d apart
//     differ by at most floor(d) + 1, and the upper bracket adds one (a
//     float's rounding cannot add a bar: (NO − 1) / 3 is a whole number or a
//     third off one, never a rounding under the next). ONE BRANCH-FREE LOOP
//     FETCHES THEM, bar min(jA + d, jB) for d < NB, into scalars — a repeat
//     of jB changes no hull — and keeps the three from jF, the pixel's first
//     subcolumn's j0, for the hold lemma, and its first two, jA and jA + 1,
//     for the roads' walk.
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
//     bars from jF (the four subcolumns span 3 / (4p) <= 1 bar, one step of
//     j0 at most; a second, which only a rounding at the floor's pitch can
//     make, certifies nothing and takes the roads). At p = 1 on a whole
//     offset the own bar's core covers all four subcolumns and its neighbor
//     is each one's other bracket, so the pixel is full exactly where the
//     own bar holds it. In the arm's order:
//     (a) DISCARD when neither reaches: n_o = 0.
//     (b) THE FULL INNER, the core opaque: the pixel full in the inner
//     family, so n_i = n_o = 16 (the union holds the inner) and the
//     composite is the core's word, whether or not an outer bar holds the
//     pixel (a bar wholly on one side of zero has its outer pushed outward
//     with a gap, render.h's "THE OUTER IS A DILATION ABOUT THE CENTRE
//     ROW").
//     (c) THE FULL OUTER: the pixel full in the outer family, wo = 0xFFFF
//     with no base or dilation computed.
//     (d) THE INK, opaque: the full outer with reach_i false, cov_o = 1 by
//     (c) and cov_i = 0 by the reach lemma. (A pixel the rule fills another
//     way — a steep contour between two bars that do not both hold it, a
//     neighbor subcolumn's disc — takes the roads to the same word: the
//     certificates are sufficient, not complete.)
//     (e) THE ROADS past them: reach_i false, the outer's bases and
//     dilation alone, wi = 0 (the reach lemma: the inner's word would be
//     zero); reach_o false, the inner alone with wo starting at 0 (the
//     lemma for the outer: the outer's word would be zero, and full_o
//     implies reach_o — a bar that holds the pixel is in the hull); both
//     reaching, both, the outer's work replaced by (c) where the full outer
//     holds. Every road ends in the one composite, and the
//     word written opaque is the composite's own result at cov_o = 1 and
//     cov_i 0 or 1, the former expression's arm (x · 1.0 = x, x · 0.0 = 0,
//     x + 0.0 = x and 1 − 0 · 1 = 1 in IEEE). A road is a branch on
//     reach_o, reach_i and full_o, uniform across a run of pixels in
//     practice but no constant of the source: the specialization rule holds
//     inside every road.
//   THE COST: on every plate fragment, the support's bars — NB fetches in
//   the one branch-free loop, the hulls, the three own bars and the walk's
//   two seeds out of it — and the four own subcolumns' certificates; on the
//   roads, past the fast paths,
//   ONE WALK OF THE SUPPORT'S NO SUBCOLUMNS FOR BOTH FAMILIES (a
//   texel carries both), its brackets A ROLLING PAIR of scalars — between
//   two subcolumns u moves 1 / (4p) <= 1/3 of a bar, so j0 keeps or steps
//   by one, and on a step the right bar becomes the left and one bar is
//   fetched: THE PAIR STARTS FROM THE SUPPORT'S LOOP, its first two bars
//   kept in the pair's own scalars (jA and jA + 1, the first subcolumn's
//   brackets), so the walk fetches one bar a step and nothing else — NB
//   fetches a fragment, and on a road the steps of j0 across the support
//   more, floor((NO − 1) / (4p)) or one over it — then the dilations of the
//   family or families that reach. No erosion.
//   Whether the frame holds its 11 ms on the tablet is measured there
//   (SurfaceFlinger's timestats), not argued here. The window and the
//   level-indexed dilation schemes are modeled bit for bit in Python
//   against the brief's forward model (tmp/grow/make_grow_mag.py's mask /
//   morph / down at SS = 4) at every grow 0 .. 8, 2026-10-31, and are that
//   text still; the fast paths, each a subset of the rule, change no
//   result.
// A word is cairo's 0xAARRGGBB with A = 255, written as (R, G, B, 1) at
// k / 255 per channel for every record but the plate's fringe and the tint,
// which the RGBA8 framebuffer stores as k — the texture triangle's own
// channel order. Primitives write in instance order (GL's primitive order
// within a draw), so a later record overwrites an earlier one, or blends
// over it (the plate's fringe, the tint), the paint order; under the blend
// an opaque record's word lands exactly (src · 1 + dst · 0).
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
    "void main() {\n"
    "    int x = a_rect.x + (((gl_VertexID & 1) != 0) ? a_rect.z : 0);\n"
    "    int y = a_rect.y + (((gl_VertexID & 2) != 0) ? a_rect.w : 0);\n"
    "    gl_Position = vec4(float(x) / u_size.x * 2.0 - 1.0,\n"
    "                       1.0 - float(y) / u_size.y * 2.0, 0.0, 1.0);\n"
    "    v_rect  = a_rect;\n"
    "    v_param = a_param;\n"
    "    v_words = a_words;\n"
    "    v_bars  = a_bars;\n"
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
    // the screen subcolumn s of the rect, xp the draw transform {x0, p} —
    // the one expression every site evaluates.
    "float bar_u(int s, vec2 xp) {\n"
    "    return ((float(s) + 0.5) * 0.25 - xp.x) / xp.y;\n"
    "}\n"
    // BAR j OF THE RECORD'S ROW, the zero texel where it does not exist
    // (absent in both families' own test, bot > top); the coordinate is
    // clamped, so no texel outside [0, n) is fetched.
    "ivec4 bar_at(int j, int n, int row) {\n"
    "    ivec4 c = texelFetch(u_columns, ivec2(clamp(j, 0, n - 1), row), 0);\n"
    "    return (j >= 0 && j < n) ? c : ivec4(0);\n"
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
    // THE SUPPORT'S BARS AT MOST (the head's fast paths: the brackets of NO
    // subcolumns at a pitch of 3/4 px or more).
    "const int NB = (NO - 1) / 3 + 3;\n"
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
    "        int jA = int(floor(bar_u(s0, v_bars.xy)));\n"
    "        int jB = int(floor(bar_u(s0 + NO - 1, v_bars.xy))) + 1;\n"
    "        int jF = int(floor(bar_u(4 * i, v_bars.xy)));\n"
    "        ivec4 own0 = ivec4(0);\n"
    "        ivec4 own1 = ivec4(0);\n"
    "        ivec4 own2 = ivec4(0);\n"
    // THE WALK'S SEEDS, bars jA and jA + 1: the loop's d = 0 and d = 1 fetch
    // exactly them — jB >= jA + 1 always (the last subcolumn's u is at
    // least the first's, so its j0 + 1 is at least jA + 1), so the clamp
    // leaves both, and NB >= 4 at every grow, so both are written.
    "        ivec4 bl = ivec4(0);\n"
    "        ivec4 br = ivec4(0);\n"
    "        int tmin_o = 1 << 20;\n"
    "        int bmax_o = -(1 << 20);\n"
    "        int tmin_i = 1 << 20;\n"
    "        int bmax_i = -(1 << 20);\n"
    "        UNROLL for (int d = 0; d < NB; ++d) {\n"
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
    // The fast paths in the head's order: (a) the discard; (b) the full
    // inner, the core opaque; (c) the full outer; (d) the ink opaque.
    "        if (!reach_o && !reach_i) discard;\n"
    // The pixel's own four subcolumns: full in a family where each is
    // (sub_full); e the subcolumn's lower bracket from jF, 0 or 1 at any
    // pitch of the key — another certifies nothing.
    "        bool full_o = true;\n"
    "        bool full_i = true;\n"
    "        UNROLL for (int m = 0; m < 4; ++m) {\n"
    "            float u = bar_u(4 * i + m, v_bars.xy);\n"
    "            float fl = floor(u);\n"
    "            float f = u - fl;\n"
    "            int e = int(fl) - jF;\n"
    "            ivec4 a = (e == 0) ? own0 : own1;\n"
    "            ivec4 b = (e == 0) ? own1 : own2;\n"
    "            bool ok = e == 0 || e == 1;\n"
    "            bool near_a = f < 0.5;\n"
    "            bool core_a = f * v_bars.y < 0.5;\n"
    "            bool core_b = (1.0 - f) * v_bars.y <= 0.5;\n"
    "            full_o = full_o && ok &&\n"
    "                     sub_full(a.xy, b.xy, near_a, core_a, core_b, ylo);\n"
    "            full_i = full_i && ok &&\n"
    "                     sub_full(a.zw, b.zw, near_a, core_a, core_b, ylo);\n"
    "        }\n"
    "        if (full_i) {\n"
    "            o_color = vec4(word_rgb(v_words.y), 1.0);\n"
    "            return;\n"
    "        }\n"
    "        if (full_o && !reach_i) {\n"
    "            o_color = vec4(word_rgb(v_words.x), 1.0);\n"
    "            return;\n"
    "        }\n"
    // (e) The roads: the outer's work where it reaches and the full outer
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
    "            float u = bar_u(s0 + t, v_bars.xy);\n"
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
    // alpha, premultiplied, for the blend to lay over what lies beneath.
    "    if (kind == 2) {\n"
    "        float a = float(v_words.y) / 100.0;\n"
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
// and the 16 of the coverage; THE BARS' PITCH FLOOR, 3/4 px — half the
// switch width's least (kWaveformSwitchMinHundredths, device_config.h; the
// rung keeps p = b / q at T/2 or more, render.h's waveform_bar_frames) — is
// the 3 of NB's (NO − 1) / 3 and what lets the brackets roll one bar a
// step; the grow's disc is GROW's (disc_rows: the largest g
// with g² + dx² <= r², the level the Horner scheme selects), its top
// kWaveformGrowMaxQuarters = 8 what the one 32-bit window and the pixel's
// seat in it, PIX0 = 14, were sized for — THE WINDOW HOLDS THE GROW'S 2 PX
// ALONE (2026-11-04, the core: no erosion reads past the grown inner): the
// dilated masks are honest on bits GROW .. 31 − GROW, which hold the
// pixel's bits 14 .. 17 at the top grow.
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
static_assert(4 * kWaveformSwitchMinHundredths == 3 * 200,
              "kListFragmentShader's NB and its rolling brackets spell the "
              "bars' least pitch, half the switch width's floor, as 3/4 px");

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
// (location 3, floats; architect 2026-11-14, send G): {x0, pitch, bars, 0}
// — the centre of the row's first bar in device px from the rect's left
// edge, the bars' pitch in device px and the row's bar count
// (GuiCanvasPrimitive's bars_x0, bars_pitch and bars) — zero on every
// record but a plate's.
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
    // program — at the plate's construction grow
    // (kWaveformGrowDefaultQuarters); a list at another grow has it
    // rebuilt at its first draw (present_frame, build_list_program) — its
    // own vertex array — the instance attributes never touch
    // the default one the texture triangle draws from — over one instance
    // buffer (three integer attributes and the plate's bars, a float one,
    // CanvasInstance), and the plate's bounds texture on unit 1 (GL_RGBA16I,
    // NEAREST: an integer texture with a filtering minifier is incomplete),
    // seeded 1 x 1 so the sampler is complete before the first plate, its
    // capacity grown by the first pack (upload_list).
    if (!list_program_) {
        build_list_program(kWaveformGrowDefaultQuarters);
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
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<const void*>(offsetof(CanvasInstance, bars)));
        for (GLuint loc = 0; loc < 4; ++loc) {
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
// chrome pixel's bytes for its color by the sink's construction, on the
// plate's fringe their coverage blend, and under a tint the blend toward
// its word (kListFragmentShader).
//
// THE REPAIR (2026-11-04, session 33; the Astra consult's (b),
// tmp/codex_review.md; the platform's half at GuiPlatform::present): THE
// FRAME IS DRAWN WHOLE OR ONLY ITS REPAIR REGION. `repair` empty is the
// whole frame, as above. Otherwise `repair` is the region the platform's
// buffer-age history says the acquired buffer is missing (this frame's
// damage and the damage of the frames since the buffer was last drawn), in
// surface px, top-left, and each of its rects is RECOMPOSED from its own
// clear through the whole pass order — the clear, the list under the rect
// ∩ the clip, the texture — under a scissor to the rect, one rect whole
// before the next; every pixel outside the region is the buffer's own,
// drawn by an earlier frame on the same passes. THE BYTES ARE THE WHOLE
// DRAW'S: every pass is deterministic from the clear at the same absolute
// coordinates on the same state, the plate's shader reads its column data
// and never the framebuffer, the blends (the plate's fringe, the tint) read
// the framebuffer only through the GL blend over the records this same
// pass drew there since the clear, and a scissor changes no fetch,
// coverage or blend; an overlap of two rects is cleared again before it is
// drawn again, so nothing blends twice. The texture's upload is still THIS frame's
// damage alone (the texture is persistent, so every older repaired pixel is
// already its own). IT STANDS ON THE DAMAGE CONTRACT: a canvas pixel the
// list changes outside every declared damage rect is not redrawn, so every
// GPU-visible change of the list owes damage on its old and new pixels
// (the audit of 2026-11-04 in tmp/s33/done2.md; the owners' damage at each
// pass's comment). A change the damage cannot describe is drawn whole by
// repair_must_be_full's verdict.
// THE FRAME'S CANVAS in surface px (the GUI's own: no window frame since
// 2026-10-24): an empty clip — no list yet, or a paint area the folder
// overlay emptied — leaves nothing out of any upload and draws no list.
// present_frame's and repair_must_be_full's one reading.
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

// THE REPAIR'S OWN VERDICT (2026-11-04, session 33; the rule at
// present_frame's THE REPAIR): the frame must be drawn whole when the
// change it shows is not described by the damage — the texture has had no
// walk since it was made (a remade texture, the first frame), the canvas
// clip differs from the last walk's (a keyboard, a folder overlay, a modal
// box or the score viewer came or went: every pixel the clip gained or lost
// changes its pass), the score viewer's page stands or stood at the last
// frame (its textures arrive across presents with no damage of their own),
// or the list's grow differs from the standing program's (the plate is
// recompiled, so every fringe moves).
bool GuiGlCanvas::repair_must_be_full(const GuiCanvasDrawList* list) const {
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
                                std::span<const GuiGlRect> repair,
                                GuiCanvasDrawList* list,
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
        // do not move), drawn whole (repair_must_be_full).
        if (list_instances_ > 0 && list->plate_grow != list_program_grow_) {
            glDeleteProgram(list_program_);
            build_list_program(list->plate_grow);
        }
    }
    // THE LIST'S DRAW under the scissor (x, y, w, h) in surface px, top-left
    // — the canvas clip, or a repair rect's part of it.
    const auto draw_list = [&](int x, int y, int w, int h) {
        if (w <= 0 || h <= 0 || list_instances_ <= 0) return;
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, texture_h_ - (y + h), w, h);
        glUseProgram(list_program_);
        glUniform2f(list_size_uniform_, static_cast<float>(surface_w),
                    static_cast<float>(surface_h));
        glUniform1i(list_height_uniform_, texture_h_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, columns_texture_);
        glBindVertexArray(list_vao_);
        glEnable(GL_BLEND);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, list_instances_);
        glDisable(GL_BLEND);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
        glDisable(GL_SCISSOR_TEST);
    };
    const auto draw_texture = [&]() {
        glUseProgram(program_);
        glUniform1i(height_uniform_, texture_h_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };

    if (repair.empty()) {
        // THE FULL REPAIR: the clear, the list or the page under the clip's
        // scissor, the texture over the whole surface.
        glClear(GL_COLOR_BUFFER_BIT);
        if (page_stands)
            draw_page(list->page, clip);
        else if (list_draws)
            draw_list(clip[0], clip[1], clip[2], clip[3]);
        draw_texture();
    } else {
        // THE PARTIAL REPAIR (the rule at THE REPAIR above): every repair
        // rect recomposed from its own clear through the same passes in the
        // same order — the clear, the list under the rect ∩ the clip, the
        // texture — each rect whole before the next, so a pixel two rects
        // share is cleared again before it is drawn again and never blends
        // twice. Never with the page standing (repair_must_be_full).
        for (const GuiGlRect& r : repair) {
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
// as floats ({x0, pitch, bars, 0}; zero on every other record), the instance
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
// the zoom on most gesture frames (the bars' pitch runs T/2 .. T device px),
// so a texture sized to the count would be reallocated every frame. Its
// width is the widest row any pack has asked, rounded up to
// kColumnsCapacityStep texels, and its height the most rows any has (the
// tablet's plate, at most about 3020 bars at the switch width's floor,
// settles at 3072 x 2); a growth reallocates with no data (glTexImage2D, a
// handful of times in a context's life) inside the pack that asked it, and
// every pack then writes, by one glTexSubImage2D, its rows by its own
// widest row, zero past a shorter row's bars. A texel past a row's bar
// count — a zero, an earlier pack's bar, or never written since a growth —
// is never read: the shader fetches inside [0, bars) alone.
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
constexpr int kColumnsCapacityStep = 1024;
void GuiGlCanvas::upload_list(const GuiCanvasDrawList& list) {
    static std::vector<CanvasInstance>      instances;
    static std::vector<GLshort>             texels;
    static std::vector<std::pair<int, int>> spans;
    spans.clear();
    int max_n = 1;
    for (const GuiCanvasPrimitive& p : list.prims) {
        if (p.kind != GuiCanvasKind::Plate) continue;
        spans.emplace_back(p.period, p.bars);
        max_n = std::max(max_n, p.bars);
    }
    const int plates = static_cast<int>(spans.size());
    const int rows   = std::max(1, plates);
    const bool pack =
        plates > 0 &&
        !(columns_uploaded_ && list.plate_revision == columns_revision_ &&
          spans == columns_spans_);
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
        case GuiCanvasKind::Tint:
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
            in.bars[3]  = 0.0f;
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
        if (max_n > columns_w_ || rows > columns_h_) {
            const int grown_w =
                (max_n + kColumnsCapacityStep - 1) / kColumnsCapacityStep *
                kColumnsCapacityStep;
            columns_w_ = std::max(columns_w_, grown_w);
            columns_h_ = std::max(columns_h_, rows);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16I, columns_w_, columns_h_,
                         0, GL_RGBA_INTEGER, GL_SHORT, nullptr);
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, max_n, rows,
                        GL_RGBA_INTEGER, GL_SHORT, texels.data());
        glActiveTexture(GL_TEXTURE0);
        columns_uploaded_ = true;
        columns_revision_ = list.plate_revision;
        columns_spans_    = spans;
    }
    list_instances_  = static_cast<int>(instances.size());
    list_generation_ = list.generation;
    list_uploaded_   = true;
}
