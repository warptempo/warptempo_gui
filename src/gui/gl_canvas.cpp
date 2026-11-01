#include "gl_canvas.h"

#include "canvas_sink.h"   // GuiCanvasDrawList, GuiCanvasPrimitive
#include "device_config.h" // kWaveformGrowMaxQuarters, the key's lattice (the plate's asserts); the column width's range

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
// the rule at render_waveform_columns' head, render.h; the grow's key
// 2026-10-31, the 4x grid ruled for good that afternoon at render.h's
// kWaveformSubcells), an INTEGER ALGORITHM over the bounds texture
// (an isampler2D over GL_RGBA16I: row `period` holds, per column of the
// record, the four QUARTER-ROW tips {o_top, o_bot, i_top, i_bot}, the inner
// absent where i_bot <= i_top) at the plate's row y (the pixel's row less
// `origin`), in SUBROWS (quarter rows, kWaveformSubcells per row) and
// SUBCOLUMNS (four per column, the pixel's own 4 x 4 subcells its
// coverage), THE GROW A CONSTANT OF THE SOURCE — GROW, the `waveform_grow`
// device key's quarters of a device px, one subcell each, 0 .. 8
// (kWaveformGrowMaxQuarters; the key's seat is render.h's
// waveform_grow_quarters, the list carries it as
// GuiCanvasDrawList::plate_grow), AND THE COLUMN WIDTH A CONSTANT OF THE
// SOURCE TOO — COLPX, the `waveform_column_px` tuning key's device px, 1 .. 4
// (architect 2026-11-01; the seat render.h's waveform_column_px, the list's
// GuiCanvasDrawList::plate_column_px):
//   * THE SPECIALIZATION RULE (2026-10-31 afternoon; the second constant
//     2026-11-01): the grow and the column width are baked into the shader's
//     text as `#define GROW <r>` and `#define COLPX <n>`
//     (list_fragment_source: each literal's one substitution point) and THE
//     PROGRAM IS REBUILT WHEN EITHER KEY CHANGES — ensure_programs builds it
//     at the defaults, present_frame compares the list's plate_grow and
//     plate_column_px with the pair the standing program was compiled for
//     and rebuilds before the draw (build_list_program), once per commit of
//     a key, tens of ms, no frame-to-frame cost. `#if COLPX == 1` KEEPS THE
//     ONE-COLUMN ARM AS IT STOOD (the key's off value is today's picture byte
//     for byte by construction: the preprocessed source at COLPX 1 is the
//     one-column shader's of before the key token for token, checked at
//     every grow 2026-11-01), `#else` the BLOCK ARM (THE BLOCKS, below). WHY: every loop
//     bound below is then a constant
//     expression (GROW, 4, 4 + 2 · GROW, 12 + 2 · GROW), every disc
//     condition a comparison of constants and every array index a loop
//     variable, so after unrolling no array is indexed by a runtime value
//     and the masks are registers. The same algorithm with the grow as a
//     uniform (the 2026-10-31 morning road, 19657fb7, on the 8x grid:
//     runtime bounds, a while that walked g(j), a continue guard over
//     arrays of 56 and 24 wide words) spilled the masks to scratch memory
//     and measured ~300 ms of GPU time a plate frame on the tablet against
//     the 11 ms budget; this road replaced it the same day, and the grid
//     went back to 4x that afternoon (the measurements at
//     kWaveformSubcells, render.h: the specialized 8x shader still cost 16
//     .. 110 ms a frame).
//   * THE WINDOW is ONE WORD of 32 bits, a uint: bit b the subrow 4y −
//     PIX0 + b, the pixel's own subrows 4y .. 4y + 3 bits 14 .. 17 (PIX0 =
//     14). Every mask is honest where every bit it was computed from is:
//     the base masks on all 32 bits (range_bits clips a run to the word),
//     the dilated ones (grown by up to GROW) on bits GROW .. 31 − GROW, the
//     eroded ones (shrunk by up to 4 more) on bits GROW + 4 .. 27 − GROW —
//     at the key's top grow, 8, bits 12 .. 19, which hold the pixel's four
//     with two to spare (4 + 2 · (4 + 8) = 28 <= 32: one word is enough at
//     every grow of the key; the static_asserts after the source). The
//     shifts are by one (grow1, shrink1), every grow or shrink a count of
//     them.
//   * THE COLUMNS (the one-column arm; the block arm's are THE BLOCKS'): a
//     base at subcolumn t reads the columns of the
//     subcolumns t − 2 .. t + 2 (its own and the nearer neighbor's, whose
//     nearest subcolumn is within two), and the inner's bases span the
//     subcolumns 4i − 4 − GROW .. 4i + 7 + GROW (its erosion's reach), so
//     its road texelFetches the columns i − D .. i + D, D = (GROW + 9) / 4
//     = ceil((GROW + 6) / 4) (2 at GROW 0 .. 2, 3 at 3 .. 6, 4 at 7 and 8;
//     col[0 .. 8], the own column at 4, each clamped into the record's
//     width); every pixel fetches i − L .. i + L first (L below), which
//     serve the fast paths and the outer's bases whole, and the rest are
//     fetched only on a road that runs the inner.
//   * THE BLOCKS (the block arm, COLPX > 1; architect 2026-11-01, Ableton's
//     one min/max polyline through wider columns' centers; the tips' rule at
//     render_waveform_columns' head, render.h, THE BLOCKS): the COLPX = N
//     columns of a block hold the block's one pair of tips (the CPU wrote
//     them into every column), and THE CONTOUR RUNS BETWEEN BLOCK CENTERS.
//     φ = k0 mod N, the record's first column's offset in its block, rides
//     the record's `phase` (v_param.w, which the plate read nothing from
//     before; the dots' kinds 1 and 2 read their own), so record column c
//     sits at block offset p = (φ + c) mod N (the operand made non-negative
//     with 4N, c >= −4) and its subcolumn j at the block-local u = 4p + j in
//     [0, 4N). THE BASE (base_bits_n) at u: the column's own bar — the
//     block's — union the contour toward the NEARER NEIGHBOR BLOCK, the left
//     for u < 2N (its nearest column c − p − 1) and the right for u >= 2N (c
//     + N − p), the weight toward it w = |2u + 1 − 4N| in 1 / (8N) (the
//     subcolumn center's distance from the block center over the two
//     centers' 4N): X = (8N − w) · T + w · T_nb, Y likewise from the
//     bottoms, subrow k in it iff X <= 8N · k + 4N <= Y, i.e. (X + 4N − 1) /
//     (8N) <= k <= (Y − 4N) / (8N), every operand non-negative so / is floor;
//     at N = 1 it is base_bits exactly (u = j; w = 3, 1, 1, 3; (8T + 3) / 8
//     and (8B − 4) / 8). A NEIGHBOR BLOCK WITH NO COLUMN IN THE RECORD IS THE
//     OWN BLOCK, the contour flat to the edge, BY THE CLAMP ALONE: the
//     neighbor's nearest column lies outside [0, W) only when the own
//     block's edge column is the record's edge column, so clamp(·, 0, W − 1)
//     lands in the own block, whose texels are its tips; an absent inner
//     neighbor is the own, as ever. THE FETCHES (fetch_block_col): each
//     column the arm reads takes its own texel and, where one of its four
//     subcolumns needs it, the left block's (4p < 2N) and the right block's
//     (4p + 3 >= 2N) — one neighbor a column but the center column of an odd
//     N — each texelFetch at a clamped runtime coordinate, the arrays
//     indexed by loop variables alone. THE COLUMNS: the outer's bases (and
//     the inner's dilated at the pixel's subcolumns) read the own columns i −
//     R .. i + R, R = (GROW + 3) / 4, the first fetch i − LB .. i + LB, LB =
//     max(R, 1) for the square's i ± 1, the inner's road i − RI .. i + RI, RI
//     = (GROW + 7) / 4, its erosion halo, the neighbor blocks' texels beside
//     each. THE FAST PATHS STAND, re-derived for blocks (THE EXACT FAST
//     PATHS, below): the reach lemma holds with the nearer neighbor BLOCK (a
//     base's contour lies between its own bar and that block's bar, X and Y
//     being convex combinations), so the hull is taken over the own texels of
//     i − R .. i + R and their fetched neighbor blocks' (an unfetched one is
//     the own texel, so the hull stays every bar a base reads); the inner
//     square, the full outer and the core read the own columns' bars alone —
//     each column's bar is in every one of its subcolumns' bases whatever the
//     contour — so their certificates hold as written, on col[i − 1 .. i +
//     1]'s own texels, not the block neighbors'. The one-column arm's
//     window, dilation, erosion, coverage and composite are the block arm's
//     unchanged (the vertical reach is the grow's and the erosion's alone).
//     Modeled bit for bit in Python 2026-11-01 (tmp/colpx/model.py: the arm
//     as written against the full rule over the record, at every grow 0 .. 8
//     and every N 1 .. 4; the block base against a rational forward model
//     of the contour between block centers; the edge clamp's claim): no
//     mismatch over about 2.08 million pixels of random, waveform-shaped and
//     thin bars, about 58,000 a (grow, N).
//   * THE BASE of subcolumn j of a column (base_bits; the one-column arm —
//     the block arm's is THE BLOCKS' above): its bar's subrows
//     [T, B − 1] UNION the contour's — the contour's top at the subcolumn's
//     center interpolates this column's T and the NEARER neighbor's in
//     eighths (j = 0..3: 3:5, 1:7, 7:1, 5:3; the left neighbor for j < 2,
//     the right for j >= 2), the bottom likewise, and subrow k is in it iff
//     8Tc <= 8k + 4 <= 8Bc, i.e. (8Tc + 3) / 8 <= k <= (8Bc − 4) / 8 — both
//     operands non-negative (T >= 0, B >= 1 for a present bar), so / is
//     floor. A neighbor beyond the record's side edge is the column itself
//     (the contour runs flat to the edge), as is an absent inner neighbor; a
//     subcolumn beyond the edge, or of an absent bar, has an empty base. The
//     bases live in TWO ARRAYS SIZED TO THE REACH: the outer's bo over
//     4i − GROW .. 4i + 3 + GROW (4 + 2 · GROW words, subcolumn 4i − GROW +
//     t at index t), the inner's bi over 4i − 4 − GROW .. 4i + 7 + GROW
//     (12 + 2 · GROW words, subcolumn 4i − 4 − GROW + t at index t); every
//     word is written, and a dilation's reads stay inside.
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
//     computed. At the default, GROW 2, the scheme is grow2(c) | grow1(l1 |
//     r1) | l2 | r2, the 2026-10-30 shader's dilate. mo the outer's over the
//     pixel's four subcolumns (folded into the count at once, never kept),
//     mi the inner's over twelve (the erosion reads four either side).
//   * THE EROSION (the outline's distance kWaveformSubcells ·
//     waveform_line_px() = 4 subcells, erode): the inner's interior at a
//     subcell holds iff the nine mi within four subcolumns, each shrunk
//     vertically by e(|dx|) = floor(sqrt(16 − dx²)) = 4, 3, 3, 2, 0 rows at
//     |dx| = 0..4, are all set — the same Horner scheme by the level with ∩
//     and shrink (shrink_a ∘ shrink_b = shrink_{a+b}, shrink distributes
//     over ∩), the literal 4 its radius: shrink4(c) & shrink3(a1 & b1) &
//     shrink3(a2 & b2) & shrink2(a3 & b3) & a4 & b4, the 2026-10-30
//     shader's erode. BEYOND THE RECORD'S SIDE EDGES AND BEYOND THE BAND'S
//     ROWS mi COUNTS AS SET (`outside`, the all-ones word): the waveform
//     continues off screen, so no vertical line at the area's sides and
//     none at the band's edges.
//   * THE COVERAGE per pixel, in sixteenths (pop16 over the four
//     subcolumns' pixel nibbles packed into one 16-bit word — bitCount is
//     GLSL ES 3.10's, not 3.00's): n_o = |(mo ∪ mi) ∩ pixel|, n_i = |mi ∩
//     pixel|, n_t = |interior ∩ pixel|; the outline's coverage is (n_i −
//     n_t) / 16, never negative (the interior is inside mi).
//   * THE COMPOSITE, source-over in byte space: the fragment writes the
//     PREMULTIPLIED source  ink · cov_o · (1 − cov_ol) + outline · cov_ol
//     with alpha 1 − (1 − cov_o)(1 − cov_ol), and the blend
//     glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA) (present_frame) lays it on
//     the pixel beneath, G: the result is lerp(lerp(G, ink, cov_o), outline,
//     cov_ol), rounded once to a byte. Where the core is (cov_o = 1, cov_ol 0
//     or 1) the factors are exactly 1 and 0 and the word lands exactly;
//     where nothing covers (n_o = 0) the fragment discards and the plate
//     stays transparent there.
//   * THE EXACT FAST PATHS (2026-10-31 night: smooth coarse zoom and pan
//     with the magnify lamp lit outranks everything but the picture's
//     bytes, which these paths leave untouched — each is a provable subset
//     of the rule above, every pixel's word the rule's byte for byte at
//     every grow). Every pixel first fetches the columns i − L .. i + L, L
//     = (GROW + 5) / 4: the dilation at the pixel's four subcolumns reads
//     the bases within GROW, 4i − GROW .. 4i + 3 + GROW, and a base at
//     subcolumn t reads the columns of t − 2 .. t + 2, so the columns
//     reached are those of the subcolumns 4i − GROW − 2 .. 4i + 5 + GROW,
//     i − ceil((GROW + 2) / 4) .. i + floor((GROW + 5) / 4), the one L
//     both sides (1 at GROW 0 .. 2, 2 at 3 .. 6, 3 at 7 and 8). THE REACH
//     LEMMA under them all: a base lies within its own and its neighbor's
//     bars (the contour between two columns lies within their two
//     extents), a dilation grows it by at most GROW subrows and the window
//     only ever drops bits, so where the pixel's subrows lie more than GROW
//     subrows outside the hull of the outer bars of i − L .. i + L
//     (reach_o false) no dilated outer bit is at the pixel, and outside the
//     hull of their present inner bars (reach_i false; the hull empty, so
//     reach_i false, where no inner is present) no dilated inner bit is —
//     `outside` and the all-ones words never touch the pixel's own
//     subrows, which lie inside the band and the record — so n_i = n_t = 0.
//     In the arm's order:
//     (a) DISCARD when neither reaches: n_o = 0.
//     (b) THE INNER SQUARE ON THE GROWN BARS, the ink opaque (2026-10-31
//     night): the pixel's subrows ± 4 lie inside the inner bar grown by
//     GROW, T − GROW <= 4y − 4 and 4y + 7 <= B − 1 + GROW, of the own and
//     both in-record neighbors below GROW 4 (a neighbor beyond the edge
//     counts as set), of the own column alone from GROW 4 (SQ, a constant
//     of the source). Either form gives n_t = n_i = 16 and n_o = 16 (wo
//     takes mi's nibbles) — whether or not the own outer bar holds the
//     pixel (a column wholly on one side of zero has its outer pushed
//     outward with a gap, render.h's "THE OUTER IS A DILATION ABOUT THE
//     CENTRE ROW"). THREE COLUMNS: the disc's dx = 0 column stands g(0) =
//     GROW rows and the own bar is in every own subcolumn's base, so mi
//     holds subrows 4y − 4 .. 4y + 7 at all twelve subcolumns of i − 1 ..
//     i + 1 (an out-of-record word all ones), a square holding the
//     erosion's disc about each of the pixel's subcells. THE OWN COLUMN
//     ALONE, GROW >= 4: a pixel subcell (s, r) lies within GROW − 4 rows of
//     the own bar's cell (s, clamp(r, T, B − 1)), a base bit; each point of
//     the erosion's disc about (s, r) is within 4 + GROW − 4 = GROW of that
//     cell (the triangle inequality), so inside the dilation's disc about
//     it, and the dilated bit there is set. The rows read are 4y − 4 .. 4y
//     + 7, bits 10 .. 21, inside the dilated masks' honest bits at every
//     grow, and a base bit a shift brings there lies between it and the
//     bar. BELOW GROW 4 the own column alone does not serve (the reviewer's
//     witness: the own inner [140, 180), the neighbors' [200, 201), GROW 2,
//     the row 40 passes the own test, and the erosion's horizontal
//     neighbors are missing, n_i = 16, n_t = 0, the outline). At GROW 0
//     the test is the ungrown square.
//     (c) THE FULL OUTER: where the own outer bar grown vertically by GROW
//     holds the pixel's four subrows, the outer's sixteen bits are set, wo
//     = 0xFFFF with no base or dilation computed — the disc's dx = 0 column
//     stands g(0) = GROW rows, so each own subcolumn's dilation holds its
//     base grown by GROW, and the own bar's subrows are in every own
//     subcolumn's base (the outer is always present, bot >= top + 1).
//     (d) THE CORE, the ink opaque: the full outer with reach_i false,
//     cov_o = 1 by (c) and cov_ol = 0 by the lemma. It holds the dark
//     lamp's solid within the own bar grown by GROW (a pixel the contour's
//     part of a base covers beyond it takes the outer's road to the same
//     ink: the certificates are sufficient, not complete), and it exists
//     for the gap the 2026-10-31 night consult found: with the lamp lit
//     every solid pixel between the outer's edge and the inner's square —
//     on the magnified coarse picture a large area, the outer levelled and
//     expanded toward the lane edge, the inner at most half of it — took
//     the full path though its answer is the ink.
//     (e) THE ROADS past them: reach_i false, the outer's bases and
//     dilation alone, wi = wt = 0 (the lemma: the inner's words would be
//     zero) and the columns past L unfetched (they serve the inner's halo
//     alone); reach_o false, the inner alone with wo starting at 0 (the
//     lemma for the outer: the outer's word would be zero, and full_o
//     implies reach_o); both reaching, both, the outer's work replaced by
//     (c) where the full outer holds. Every road ends in the one composite,
//     and the ink written opaque is the composite's own result at cov_o =
//     1, cov_ol = 0 (x · 1.0 = x, x + 0.0 = x and 1 − 0 · 1 = 1 in IEEE).
//     A road is a branch on reach_o, reach_i and full_o, uniform across a
//     column's run of pixels in practice but no constant of the source: the
//     specialization rule holds inside every road.
//   The window and the level-indexed schemes are modeled bit for bit in
//   Python against the brief's forward model (tmp/grow/make_grow_mag.py's
//   mask / morph / down at SS = 4) at every grow 0 .. 8, 2026-10-31; the
//   fast paths, each a subset of the rule, change no result the model
//   computes. At GROW 2 the base rule, the disc shapes and the composite
//   are the 2026-10-30 shader's (2f26b882), so its picture is that build's
//   byte for byte.
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
    // THE TWO SUBSTITUTION POINTS (list_fragment_source): the grow's digits
    // and the column width's land here, so GROW and COLPX are preprocessor
    // constants of the whole source.
    "#define GROW @GROW@\n"
    "#define COLPX @COLPX@\n"
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
    "uint shrink1(uint m) { return m & (m << 1) & (m >> 1); }\n"
    "uint base_bits(ivec2 own, ivec2 nb, int j, int k0) {\n"
    "    int wn = (j == 0 || j == 3) ? 3 : 1;\n"
    "    int wc = 8 - wn;\n"
    "    int t8 = wn * nb.x + wc * own.x;\n"
    "    int b8 = wn * nb.y + wc * own.y;\n"
    "    return range_bits((t8 + 3) / 8 - k0, (b8 - 4) / 8 - k0) |\n"
    "           range_bits(own.x - k0, own.y - 1 - k0);\n"
    "}\n"
    // THE BLOCK ARM'S BASE (the head's blocks paragraph; COLPX > 1 alone): at
    // the block-local subcolumn u in [0, 4 · COLPX), the own block's bar
    // union the contour toward the nearer neighbor block `nb`, the weight
    // toward it w = |2u + 1 − 4 · COLPX| in 1 / (8 · COLPX) — base_bits at
    // COLPX = 1, u = j (w = 3, 1, 1, 3; the divisor 8 and the offsets 3 and
    // 4), every operand non-negative so / is floor.
    "#if COLPX != 1\n"
    "uint base_bits_n(ivec2 own, ivec2 nb, int u, int k0) {\n"
    "    int w = abs(2 * u + 1 - 4 * COLPX);\n"
    "    int wc = 8 * COLPX - w;\n"
    "    int t8 = w * nb.x + wc * own.x;\n"
    "    int b8 = w * nb.y + wc * own.y;\n"
    "    return range_bits((t8 + 4 * COLPX - 1) / (8 * COLPX) - k0,\n"
    "                      (b8 - 4 * COLPX) / (8 * COLPX) - k0) |\n"
    "           range_bits(own.x - k0, own.y - 1 - k0);\n"
    "}\n"
    // THE BLOCK ARM'S COLUMNS (window index d, the own column at 4): the own
    // texel bc, the nearer-neighbor blocks' texels bl (the left block's
    // nearest column c − p − 1) and br (the right's, c + COLPX − p), each
    // fetched only where a subcolumn of the column needs it and else the own
    // texel, the block offset bp = p, and whether the column is in the record
    // and its inner present. Globals, indexed by a loop variable of a
    // constant-bounded loop, as the masks are.
    "ivec4 bc[9];\n"
    "ivec4 bl[9];\n"
    "ivec4 br[9];\n"
    "int bp[9];\n"
    "bool binp[9];\n"
    "bool blit[9];\n"
    "void fetch_block_col(int d, int i, int W, int phi, int row) {\n"
    "    int c = i + d - 4;\n"
    "    binp[d] = c >= 0 && c < W;\n"
    "    bc[d] = texelFetch(u_columns, ivec2(clamp(c, 0, W - 1), row), 0);\n"
    "    blit[d] = binp[d] && bc[d].w > bc[d].z;\n"
    "    int p = (phi + c + 4 * COLPX) % COLPX;\n"
    "    bp[d] = p;\n"
    "    bl[d] = (binp[d] && 4 * p < 2 * COLPX)\n"
    "        ? texelFetch(u_columns, ivec2(clamp(c - p - 1, 0, W - 1), row), 0) : bc[d];\n"
    "    br[d] = (binp[d] && 4 * p + 3 >= 2 * COLPX)\n"
    "        ? texelFetch(u_columns, ivec2(clamp(c + COLPX - p, 0, W - 1), row), 0) : bc[d];\n"
    "}\n"
    "#endif\n"
    // THE MASK ARRAYS, SIZED TO THE REACH (the head's specialization rule):
    // the outer's bases bo over the subcolumns 4i − GROW .. 4i + 3 + GROW
    // (index t the subcolumn 4i − GROW + t), the inner's bi over 4i − 4 −
    // GROW .. 4i + 7 + GROW (index t the subcolumn 4i − 4 − GROW + t), the
    // dilated inner mi over 4i − 4 .. 4i + 7. Globals, so the disc
    // routines read them in place (GLSL passes an array by value); every
    // index a loop variable of a constant-bounded loop, so the unrolled
    // accesses are constant and the arrays are registers.
    "const int NO = 4 + 2 * GROW;\n"
    "const int NI = 12 + 2 * GROW;\n"
    "uint bo[NO];\n"
    "uint bi[NI];\n"
    "uint mi[12];\n"
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
    // THE DISC EROSION of radius 4 (one device px) at mi's index s: the same
    // scheme with the intersection and the shrink, its radius the literal 4.
    "uint erode(int s) {\n"
    "    uint acc = mi[s];\n"
    "    UNROLL for (int v = 4; v >= 0; --v) {\n"
    "        UNROLL for (int j = 1; j <= 4; ++j) {\n"
    "            if (j * j + v * v <= 16 && j * j + (v + 1) * (v + 1) > 16)\n"
    "                acc &= mi[s - j] & mi[s + j];\n"
    "        }\n"
    "        if (v > 0) acc = shrink1(acc);\n"
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
    "    if (kind == 1 || kind == 2) {\n"
    "        int c = ((kind == 1) ? py : px) - v_param.z;\n"
    "        if (c % v_param.y != v_param.w) discard;\n"
    "    } else if (kind == 3) {\n"
    // THE ONE-COLUMN ARM at COLPX 1 (the column width's off value, the
    // one-column picture byte for byte by construction: this text is that
    // arm unchanged); the BLOCK ARM after #else.
    "#if COLPX == 1\n"
    "        int W = v_rect.z;\n"
    "        int i = px - v_rect.x;\n"
    "        int y = py - v_param.z;\n"
    "        int k0 = 4 * y - PIX0;\n"
    // The columns' reach (the head's columns paragraph): the fast paths and
    // the outer read i − L .. i + L, the inner's road i − D .. i + D; col[4]
    // is the own.
    "        const int L = (GROW + 5) / 4;\n"
    "        const int D = (GROW + 9) / 4;\n"
    "        ivec4 col[9];\n"
    "        bool inp[9];\n"
    "        bool lit[9];\n"
    "        UNROLL for (int d = 4 - L; d <= 4 + L; ++d) {\n"
    "            int c = i + d - 4;\n"
    "            inp[d] = c >= 0 && c < W;\n"
    "            col[d] = texelFetch(u_columns, ivec2(clamp(c, 0, W - 1), v_param.y), 0);\n"
    "            lit[d] = inp[d] && col[d].w > col[d].z;\n"
    "        }\n"
    "        int ylo = 4 * y;\n"
    "        int yhi = ylo + 3;\n"
    "        int tmin_o = 1 << 20;\n"
    "        int bmax_o = -(1 << 20);\n"
    "        int tmin_i = 1 << 20;\n"
    "        int bmax_i = -(1 << 20);\n"
    "        UNROLL for (int d = 4 - L; d <= 4 + L; ++d) {\n"
    "            tmin_o = min(tmin_o, col[d].x);\n"
    "            bmax_o = max(bmax_o, col[d].y);\n"
    "            if (lit[d]) {\n"
    "                tmin_i = min(tmin_i, col[d].z);\n"
    "                bmax_i = max(bmax_i, col[d].w);\n"
    "            }\n"
    "        }\n"
    "        bool reach_o = yhi >= tmin_o - GROW && ylo <= bmax_o - 1 + GROW;\n"
    "        bool reach_i = yhi >= tmin_i - GROW && ylo <= bmax_i - 1 + GROW;\n"
    // The fast paths in the head's order: (a) the discard; (b) the inner
    // square and (d) the core, the ink opaque; (c) the full outer.
    "        if (!reach_o && !reach_i) discard;\n"
    // The square on the grown bars: the own and both neighbors below GROW
    // 4, the own alone from 4 (SQ a constant expression, so the loop's
    // bounds stay constants).
    "        const int SQ = (GROW >= 4) ? 0 : 1;\n"
    "        bool square = true;\n"
    "        UNROLL for (int d = 4 - SQ; d <= 4 + SQ; ++d) {\n"
    "            if (inp[d])\n"
    "                square = square && lit[d] && col[d].z - GROW <= ylo - 4 &&\n"
    "                         yhi + 4 <= col[d].w - 1 + GROW;\n"
    "        }\n"
    "        if (square) {\n"
    "            o_color = vec4(word_rgb(v_words.x), 1.0);\n"
    "            return;\n"
    "        }\n"
    "        bool full_o = col[4].x - GROW <= ylo && yhi <= col[4].y - 1 + GROW;\n"
    "        if (full_o && !reach_i) {\n"
    "            o_color = vec4(word_rgb(v_words.x), 1.0);\n"
    "            return;\n"
    "        }\n"
    // (e) The roads: the outer's work where it reaches and the full outer
    // does not hold (full_o implies reach_o), the inner's where it reaches;
    // every road ends in the one composite below.
    "        uint wo = full_o ? 0xFFFFu : 0u;\n"
    "        uint wi = 0u;\n"
    "        uint wt = 0u;\n"
    "        if (reach_o && !full_o) {\n"
    // The outer's bases over 4i − GROW .. 4i + 3 + GROW (sp the offset from
    // 4i; the column d and its subcolumn j of sp + 16, every operand
    // non-negative; every d and e within 4 − L .. 4 + L), its dilation at
    // the pixel's four subcolumns (bo's index m + GROW) folded at once into
    // the count word wo (four pixel nibbles).
    "            UNROLL for (int t = 0; t < NO; ++t) {\n"
    "                int sp = t - GROW;\n"
    "                int d = (sp + 16) / 4;\n"
    "                int j = (sp + 16) & 3;\n"
    "                int e = d + ((j < 2) ? -1 : 1);\n"
    "                bo[t] = inp[d] ? base_bits(col[d].xy, inp[e] ? col[e].xy : col[d].xy, j, k0)\n"
    "                               : 0u;\n"
    "            }\n"
    "            UNROLL for (int m = 0; m < 4; ++m)\n"
    "                wo |= pix4(dilate_o(m + GROW)) << (4 * m);\n"
    "        }\n"
    "        if (reach_i) {\n"
    // The columns past L, the inner's erosion halo: fetched on this road
    // alone.
    "            UNROLL for (int d = 4 - D; d <= 4 + D; ++d) {\n"
    "                if (d < 4 - L || d > 4 + L) {\n"
    "                    int c = i + d - 4;\n"
    "                    inp[d] = c >= 0 && c < W;\n"
    "                    col[d] = texelFetch(u_columns, ivec2(clamp(c, 0, W - 1), v_param.y), 0);\n"
    "                    lit[d] = inp[d] && col[d].w > col[d].z;\n"
    "                }\n"
    "            }\n"
    // The inner's bases over 4i − 4 − GROW .. 4i + 7 + GROW, dilated into
    // mi over 4i − 4 .. 4i + 7 (bi's index q + GROW), the outside set
    // beyond the band's rows and the record's side edges.
    "            UNROLL for (int t = 0; t < NI; ++t) {\n"
    "                int sp = t - 4 - GROW;\n"
    "                int d = (sp + 16) / 4;\n"
    "                int j = (sp + 16) & 3;\n"
    "                int e = d + ((j < 2) ? -1 : 1);\n"
    "                bi[t] = lit[d] ? base_bits(col[d].zw, lit[e] ? col[e].zw : col[d].zw, j, k0)\n"
    "                               : 0u;\n"
    "            }\n"
    "            int band_lo = 4 * (v_rect.y - v_param.z);\n"
    "            int band_hi = 4 * (v_rect.y + v_rect.w - v_param.z);\n"
    "            uint outside = range_bits(0, band_lo - 1 - k0) | range_bits(band_hi - k0, 31);\n"
    "            UNROLL for (int q = 0; q < 12; ++q) {\n"
    "                int s = 4 * i - 4 + q;\n"
    "                mi[q] = (s >= 0 && s < 4 * W) ? (dilate_i(q + GROW) | outside) : 0xFFFFFFFFu;\n"
    "            }\n"
    "            UNROLL for (int m = 0; m < 4; ++m) {\n"
    "                uint i4 = pix4(mi[4 + m]);\n"
    "                wo |= i4 << (4 * m);\n"
    "                wi |= i4 << (4 * m);\n"
    "                wt |= pix4(erode(4 + m)) << (4 * m);\n"
    "            }\n"
    "        }\n"
    "        int n_o = pop16(wo);\n"
    "        int n_i = pop16(wi);\n"
    "        int n_t = pop16(wt);\n"
    "        if (n_o == 0) discard;\n"
    "        float cov_o  = float(n_o) / 16.0;\n"
    "        float cov_ol = float(n_i - n_t) / 16.0;\n"
    "        o_color = vec4(word_rgb(v_words.x) * (cov_o * (1.0 - cov_ol)) +\n"
    "                       word_rgb(v_words.y) * cov_ol,\n"
    "                       1.0 - (1.0 - cov_o) * (1.0 - cov_ol));\n"
    "        return;\n"
    "#else\n"
    // THE BLOCK ARM (the head's blocks paragraph): the one-column arm's
    // rule and fast paths with the base between BLOCK centers. φ rides the
    // record's phase (v_param.w); record column c's block offset is (φ + c)
    // mod COLPX.
    "        int W = v_rect.z;\n"
    "        int i = px - v_rect.x;\n"
    "        int y = py - v_param.z;\n"
    "        int k0 = 4 * y - PIX0;\n"
    "        int phi = v_param.w;\n"
    // The columns' reach: the outer's bases (and the dilated inner at the
    // pixel's subcolumns) read the own columns i − R .. i + R and their
    // nearer-neighbor blocks' texels; the first fetch is i − LB .. i + LB,
    // LB = max(R, 1) for the inner square's i ± 1; the inner's road adds
    // i − RI .. i + RI.
    "        const int R = (GROW + 3) / 4;\n"
    "        const int LB = (R > 1) ? R : 1;\n"
    "        const int RI = (GROW + 7) / 4;\n"
    "        UNROLL for (int d = 4 - LB; d <= 4 + LB; ++d)\n"
    "            fetch_block_col(d, i, W, phi, v_param.y);\n"
    "        int ylo = 4 * y;\n"
    "        int yhi = ylo + 3;\n"
    "        int tmin_o = 1 << 20;\n"
    "        int bmax_o = -(1 << 20);\n"
    "        int tmin_i = 1 << 20;\n"
    "        int bmax_i = -(1 << 20);\n"
    // The hull: every bar a base of i − R .. i + R reads, the own and the
    // fetched neighbor blocks' (an unfetched neighbor is the own texel).
    "        UNROLL for (int d = 4 - R; d <= 4 + R; ++d) {\n"
    "            tmin_o = min(tmin_o, min(bc[d].x, min(bl[d].x, br[d].x)));\n"
    "            bmax_o = max(bmax_o, max(bc[d].y, max(bl[d].y, br[d].y)));\n"
    "            if (blit[d]) {\n"
    "                tmin_i = min(tmin_i, bc[d].z);\n"
    "                bmax_i = max(bmax_i, bc[d].w);\n"
    "            }\n"
    "            if (binp[d] && bl[d].w > bl[d].z) {\n"
    "                tmin_i = min(tmin_i, bl[d].z);\n"
    "                bmax_i = max(bmax_i, bl[d].w);\n"
    "            }\n"
    "            if (binp[d] && br[d].w > br[d].z) {\n"
    "                tmin_i = min(tmin_i, br[d].z);\n"
    "                bmax_i = max(bmax_i, br[d].w);\n"
    "            }\n"
    "        }\n"
    "        bool reach_o = yhi >= tmin_o - GROW && ylo <= bmax_o - 1 + GROW;\n"
    "        bool reach_i = yhi >= tmin_i - GROW && ylo <= bmax_i - 1 + GROW;\n"
    "        if (!reach_o && !reach_i) discard;\n"
    // The square on the grown bars and the full outer read the own columns'
    // bars alone (each column's bar is in every one of its subcolumns'
    // bases, the contour aside), so they stand as in the one-column arm.
    "        const int SQ = (GROW >= 4) ? 0 : 1;\n"
    "        bool square = true;\n"
    "        UNROLL for (int d = 4 - SQ; d <= 4 + SQ; ++d) {\n"
    "            if (binp[d])\n"
    "                square = square && blit[d] && bc[d].z - GROW <= ylo - 4 &&\n"
    "                         yhi + 4 <= bc[d].w - 1 + GROW;\n"
    "        }\n"
    "        if (square) {\n"
    "            o_color = vec4(word_rgb(v_words.x), 1.0);\n"
    "            return;\n"
    "        }\n"
    "        bool full_o = bc[4].x - GROW <= ylo && yhi <= bc[4].y - 1 + GROW;\n"
    "        if (full_o && !reach_i) {\n"
    "            o_color = vec4(word_rgb(v_words.x), 1.0);\n"
    "            return;\n"
    "        }\n"
    "        uint wo = full_o ? 0xFFFFu : 0u;\n"
    "        uint wi = 0u;\n"
    "        uint wt = 0u;\n"
    "        if (reach_o && !full_o) {\n"
    // The outer's bases over 4i − GROW .. 4i + 3 + GROW: the column d, its
    // subcolumn j, the block-local u = 4 · bp + j, the left block's texel
    // for u < 2 · COLPX and the right's past it.
    "            UNROLL for (int t = 0; t < NO; ++t) {\n"
    "                int sp = t - GROW;\n"
    "                int d = (sp + 16) / 4;\n"
    "                int j = (sp + 16) & 3;\n"
    "                int u = 4 * bp[d] + j;\n"
    "                ivec2 nb = (u < 2 * COLPX) ? bl[d].xy : br[d].xy;\n"
    "                bo[t] = binp[d] ? base_bits_n(bc[d].xy, nb, u, k0) : 0u;\n"
    "            }\n"
    "            UNROLL for (int m = 0; m < 4; ++m)\n"
    "                wo |= pix4(dilate_o(m + GROW)) << (4 * m);\n"
    "        }\n"
    "        if (reach_i) {\n"
    "            UNROLL for (int d = 4 - RI; d <= 4 + RI; ++d) {\n"
    "                if (d < 4 - LB || d > 4 + LB)\n"
    "                    fetch_block_col(d, i, W, phi, v_param.y);\n"
    "            }\n"
    // The inner's bases over 4i − 4 − GROW .. 4i + 7 + GROW, an absent
    // inner neighbor block the own (the contour flat), dilated into mi.
    "            UNROLL for (int t = 0; t < NI; ++t) {\n"
    "                int sp = t - 4 - GROW;\n"
    "                int d = (sp + 16) / 4;\n"
    "                int j = (sp + 16) & 3;\n"
    "                int u = 4 * bp[d] + j;\n"
    "                ivec4 nb = (u < 2 * COLPX) ? bl[d] : br[d];\n"
    "                bi[t] = blit[d] ? base_bits_n(bc[d].zw, (nb.w > nb.z) ? nb.zw : bc[d].zw, u, k0)\n"
    "                                : 0u;\n"
    "            }\n"
    "            int band_lo = 4 * (v_rect.y - v_param.z);\n"
    "            int band_hi = 4 * (v_rect.y + v_rect.w - v_param.z);\n"
    "            uint outside = range_bits(0, band_lo - 1 - k0) | range_bits(band_hi - k0, 31);\n"
    "            UNROLL for (int q = 0; q < 12; ++q) {\n"
    "                int s = 4 * i - 4 + q;\n"
    "                mi[q] = (s >= 0 && s < 4 * W) ? (dilate_i(q + GROW) | outside) : 0xFFFFFFFFu;\n"
    "            }\n"
    "            UNROLL for (int m = 0; m < 4; ++m) {\n"
    "                uint i4 = pix4(mi[4 + m]);\n"
    "                wo |= i4 << (4 * m);\n"
    "                wi |= i4 << (4 * m);\n"
    "                wt |= pix4(erode(4 + m)) << (4 * m);\n"
    "            }\n"
    "        }\n"
    "        int n_o = pop16(wo);\n"
    "        int n_i = pop16(wi);\n"
    "        int n_t = pop16(wt);\n"
    "        if (n_o == 0) discard;\n"
    "        float cov_o  = float(n_o) / 16.0;\n"
    "        float cov_ol = float(n_i - n_t) / 16.0;\n"
    "        o_color = vec4(word_rgb(v_words.x) * (cov_o * (1.0 - cov_ol)) +\n"
    "                       word_rgb(v_words.y) * cov_ol,\n"
    "                       1.0 - (1.0 - cov_o) * (1.0 - cov_ol));\n"
    "        return;\n"
    "#endif\n"
    "    }\n"
    "    o_color = vec4(word_rgb(v_words.x), 1.0);\n"
    "}\n";

// THE SOURCE AT A GROW AND A COLUMN WIDTH (the specialization rule, the
// head): the literal with the grow's digits at GROW's one substitution point
// and the column width's at COLPX's, each token occurring exactly once.
constexpr bool list_shader_has_one(std::string_view token) {
    constexpr std::string_view literal = kListFragmentShader;
    const std::size_t at = literal.find(token);
    return at != std::string_view::npos &&
           literal.find(token, at + token.size()) == std::string_view::npos;
}
static_assert(list_shader_has_one("@GROW@"),
              "kListFragmentShader has exactly one GROW substitution point");
static_assert(list_shader_has_one("@COLPX@"),
              "kListFragmentShader has exactly one COLPX substitution point");
std::string list_fragment_source(int grow, int colpx) {
    assert(grow >= 0 && grow <= kWaveformGrowMaxQuarters);
    assert(colpx >= kWaveformColumnPxMin && colpx <= kWaveformColumnPxMax);
    std::string source(kListFragmentShader);
    const auto put = [&](std::string_view token, int value) {
        const std::size_t at = source.find(token);
        source.replace(at, token.size(), std::to_string(value));
    };
    put("@GROW@", grow);
    put("@COLPX@", colpx);
    return source;
}

// THE SHADER'S LITERALS ARE render.h's CONSTANTS: the shader is a string, so
// this is the binding. kWaveformSubcells is the 4 of `4 * y`, the eighths'
// 3:5 / 1:7 weights (two subcells to the half), the pixel's nibble and the
// 16 of the coverage; the erosion's disc is kWaveformSubcells ·
// waveform_line_px() = erode's 4 and the 4, 3, 3, 2, 0 rows its levels
// select at |dx| = 0..4 (disc_rows: the largest g with g² + dx² <= r²);
// the grow's disc is GROW's, selected the same way, its top
// kWaveformGrowMaxQuarters = 8 what the one 32-bit window and the pixel's
// seat in it, PIX0 = 14, were sized for (the head's window paragraph: the
// eroded masks are honest on bits GROW + 4 .. 27 − GROW, which hold the
// pixel's bits 14 .. 17 at the top grow).
constexpr int disc_rows(int r, int dx) {
    int g = 0;
    while ((g + 1) * (g + 1) + dx * dx <= r * r) ++g;
    return g;
}
constexpr int kWaveformErodeSubcells = kWaveformSubcells * waveform_line_px();
constexpr int kPlatePixelBit = 14;
static_assert(kWaveformSubcells == 4,
              "kListFragmentShader spells the 4x grid as literals");
static_assert(kWaveformErodeSubcells == 4 && disc_rows(4, 0) == 4 &&
                  disc_rows(4, 1) == 3 && disc_rows(4, 2) == 3 &&
                  disc_rows(4, 3) == 2 && disc_rows(4, 4) == 0,
              "kListFragmentShader's erode is the disc of radius 4");
static_assert(kWaveformGrowMaxQuarters + kWaveformErodeSubcells <=
                      kPlatePixelBit &&
                  kPlatePixelBit + kWaveformSubcells +
                          kWaveformErodeSubcells + kWaveformGrowMaxQuarters <=
                      32,
              "kListFragmentShader's one-word window holds the pixel's "
              "subrows honestly at the key's top grow");
static_assert(disc_rows(kWaveformGrowMaxQuarters, kWaveformGrowMaxQuarters) == 0,
              "the dilation's Horner scheme ends with no final grow: g(r) = 0");

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

// THE LIST PROGRAM AT A GROW (the declaration's rule, gl_canvas.h): the
// specialized source linked, the sampler on unit 1, the two uniforms, and
// the grow kept beside the program as the key present_frame compares.
void GuiGlCanvas::build_list_program(int grow, int column_px) {
    const std::string source = list_fragment_source(grow, column_px);
    const GLuint prog =
        link_canvas_program(host_, kListVertexShader, source.c_str());
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "u_columns"), 1);
    list_size_uniform_   = glGetUniformLocation(prog, "u_size");
    list_height_uniform_ = glGetUniformLocation(prog, "u_height");
    list_program_        = prog;
    list_program_grow_   = grow;
    list_program_column_px_ = column_px;
}

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
    // program — at the plate's default grow; a list at another grow has it
    // rebuilt at its first draw (present_frame, build_list_program) — its
    // own vertex array — the instance attributes never touch
    // the default one the texture triangle draws from — over one instance
    // buffer, and the plate's bounds texture on unit 1 (GL_RGBA16I, NEAREST:
    // an integer texture with a filtering minifier is incomplete), seeded
    // 1 x 1 so the sampler is complete before the first plate.
    if (!list_program_) {
        build_list_program(kWaveformGrowDefaultQuarters,
                           kWaveformColumnPxDefault);
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
            // THE GROW AND THE COLUMN WIDTH RIDE THE LIST
            // (GuiCanvasDrawList::plate_grow, the `waveform_grow` key in
            // subcells; plate_column_px, the `waveform_column_px` key,
            // 2026-11-01) AND ARE CONSTANTS OF THE PROGRAM
            // (kListFragmentShader's specialization rule): a list at a grow
            // or a width the standing program was not compiled for has the
            // program rebuilt here, once — the sink stamps the keys' seats
            // on every list, so either moves only at a commit of its key and
            // the compare is false at every other frame — and a commit of
            // the grow is then the next frame's picture with no upload (the
            // tips do not move); a commit of the width arrives with the
            // plate re-rendered over its blocks (a new revision, uploaded).
            if (list->plate_grow != list_program_grow_ ||
                list->plate_column_px != list_program_column_px_) {
                glDeleteProgram(list_program_);
                build_list_program(list->plate_grow, list->plate_column_px);
            }
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
    list_program_grow_   = -1;
    list_program_column_px_ = -1;
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
// plate row times four of a canvas far under 8191 rows, the tablet's
// 1072 giving 4288; the absent inner is 0, 0), zero past a record's
// width; rows = max(1, the plate records).
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
