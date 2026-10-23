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
// textures and the frame's GL calls, moved whole out of the Android backend
// (2026-10-14) so every platform's present drives the same ones. The calls,
// their order and their arguments are the tablet's of 2026-10-13: the GL
// canvas's identity proof (docs/engineering/gpu_canvas_2026_10.md §5) holds
// across the move by construction. One reduction since, pixel-neutral by its
// key: the plate's bounds texture is re-sent only when its key moved
// (upload_list, 2026-10-14).

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
// with alpha written 1, the cpu canvas's copy_swap_rb (platform_android.cpp)
// in the shader. The sampler is highp so the fetched value carries the 8-bit
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
// the column, never negative (the list sink moved the origin back to the run's
// first cell or before; GLSL ES 3.00 leaves % on a negative operand undefined);
// THE PLATE (kind 3) fetches its column's two texels from the bounds texture
// (an isampler2D over GL_RGBA16I: row `period` holds o0 o1 i0 i1, row `period`
// + 1 lo hi, the column the pixel's offset in the rect) and makes
// render_waveform's priority select on the plate's row (the pixel's row less
// `origin`): inside [i0, i1] the ink within [lo, hi] and the outline elsewhere,
// else inside [o0, o1] the ink, else nothing. A word is cairo's 0xAARRGGBB with
// A = 255, written as (R, G, B, 1) at k / 255 per channel, which the RGBA8
// framebuffer stores as k — the texture triangle's own channel order.
// Primitives write in instance order (GL's primitive order within a draw), so a
// later record overwrites an earlier one, the cairo road's paint order.
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
    "vec4 word_color(uint w) {\n"
    "    return vec4(float((w >> 16u) & 255u), float((w >> 8u) & 255u),\n"
    "                float(w & 255u), 255.0) / 255.0;\n"
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
    "        int i = px - v_rect.x;\n"
    "        ivec4 a = texelFetch(u_columns, ivec2(i, v_param.y), 0);\n"
    "        ivec4 b = texelFetch(u_columns, ivec2(i, v_param.y + 1), 0);\n"
    "        int y = py - v_param.z;\n"
    "        if (y >= a.z && y <= a.w) {\n"
    "            o_color = word_color((y >= b.x && y <= b.y) ? v_words.x\n"
    "                                                         : v_words.y);\n"
    "            return;\n"
    "        }\n"
    "        if (y < a.x || y > a.y) discard;\n"
    "    }\n"
    "    o_color = word_color(v_words.x);\n"
    "}\n";

// One list record as the instance attributes read it: the rect (location
// 0), {kind, period, origin, phase} (location 1), {word, word2} (location 2),
// every coordinate in surface px (the GUI's client px plus the frame).
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
// surface road calls it right after its make current): nothing dithers,
// blends or tests, so a texel and a list word reach the framebuffer as
// written.
void GuiGlCanvas::bind_fixed_state() {
    glDisable(GL_DITHER);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
}

// THE TWO PROGRAMS, built at the first make current and kept with the
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
    // 1 x 2 so the sampler is complete before the first plate.
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
        const GLshort seed[8] = {};
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16I, 1, 2, 0, GL_RGBA_INTEGER,
                     GL_SHORT, seed);
        columns_w_ = 1;
        columns_h_ = 2;
        // The seed is no upload: the first plate packs whatever its key.
        columns_uploaded_ = false;
        glActiveTexture(GL_TEXTURE0);
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
// the GUI's waveform_area ∩ waveform_paint_area — the platform computes
// no GUI geometry), split into up to four rects, because the backbuffer
// holds nothing there but zeros the texture already holds. THE WHOLE
// RECT GOES UP instead while the list's floater bit stands or stood at
// the last walk (a floating surface's pixels over the canvas, and the
// zeros it leaves when it goes, ride containing rects), when the clip
// differs from the last walk's (a keyboard or a folder overlay came or
// went, and texels it uploaded now lie inside the clip), and at the
// first walk on a remade texture (no texel is known yet). So every texel
// inside the clip is the backbuffer's: 0 where no overlay stands.
//
// THE DRAW: the canvas list first, then the texture over it. THE LIST
// (kListVertexShader's head; GuiCanvasPrimitive, canvas_sink.h): its
// records as instances in list order, one instanced draw, the scissor
// the canvas clip (the cpu road's own clip of every canvas pass) — its
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
// THE PICTURE IS THE BACKBUFFER'S BYTES EXACTLY outside the clip and
// under every overlay: an 8-bit UNORM texel
// fetched without a filter round-trips to the same 8-bit value in the
// RGBA8 framebuffer, dithering is off, nothing blends, and the framebuffer
// is plain RGBA (the passthrough colorspace encodes nothing — never an
// sRGB framebuffer); inside the clip it is the list's words, the cpu
// road's bytes by the sink's construction.
bool GuiGlCanvas::present_frame(cairo_surface_t* back,
                                std::span<const GuiGlRect> damage,
                                int frame_px, const GuiCanvasDrawList* list,
                                int surface_w, int surface_h) {
    if (!texture_) return false;
    const int stride_px = cairo_image_surface_get_stride(back) / 4;
    const auto* src = reinterpret_cast<const uint32_t*>(
        cairo_image_surface_get_data(back));
    if (!src) return false;

    // THE FRAME'S CANVAS in surface px (the GUI's client rect plus the
    // frame, frame_px): an empty clip — no list yet, or a paint area
    // the folder overlay emptied — leaves nothing out of any upload and
    // draws no list.
    int  clip[4] = {0, 0, 0, 0};
    bool floater = false;
    if (list && list->clip.w > 0 && list->clip.h > 0) {
        clip[0] = list->clip.x + frame_px;
        clip[1] = list->clip.y + frame_px;
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

    glViewport(0, 0, surface_w, surface_h);
    // THE CLEAR IS TRANSPARENT WHERE A SIZING BAND STANDS (the hi-fi skin's
    // window border, 2026-10-23, render.h's caption block): the band's
    // texels are cleared to alpha 0 (paint_window_sizing_frame) and discarded
    // by the shader, so the clear is what the compositor shows there — the
    // band invisible beyond its one light line. Without a band (the
    // maximized laptop, the tablet) the clear stays opaque black, as before.
    glClearColor(0.0f, 0.0f, 0.0f, frame_px > 0 ? 0.0f : 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (list && clip[2] > 0) {
        if (!list_uploaded_ || list->generation != list_generation_)
            upload_list(*list, frame_px);
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
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                                  list_instances_);
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
    upload_seeded_       = false;
}

// THE LIST'S UPLOAD (2026-10-13; present_frame's draw): every record one
// instance in list order, its coordinates moved by the frame into surface px (a
// rect, a run's origin, a plate's row 0 alike), the instance buffer rewritten
// whole — asked only when the list's generation moved, so at every rebuild of
// the records (the scanner's and the cursor's among them; the buffer is small).
// A plate record's `period` is re-pointed at its rows of THE PLATE'S BOUNDS
// TEXTURE — two rows per plate record, the record's columns left to right:
// row 2k {o0, o1, i0, i1}, row 2k + 1 {lo, hi, 0, 0}, the plate's rows as
// render_waveform_columns gathered them (the cairo road's empty sentinels,
// y_lo − 1 among them, are why the format is signed, GL_RGBA16I — every
// value a plate row of a canvas far under 32767 rows), zero past a record's
// width and below the last record's rows.
// THE BOUNDS TEXTURE HAS ITS OWN LIFETIME (2026-10-14): it is packed and sent
// only when its KEY moved — the list's plate revision, the plate records'
// spans in list order (each its offset into the list's columns and its
// width, so their count too), and the dimensions those spans give it — or
// when the texture holds no upload yet (the seed of ensure_programs, a
// context remade after forget()); remade at a size change, else rewritten
// whole. THE KEY IS COMPLETE: every texel is a function of the spans, the
// dimensions and the list's columns at those spans, and the list's columns
// are a function of the revision alone — the list sink copies the live
// plate's stored bands into them once per revision, and the live plate's
// bounds change only at a publication, which takes a new revision
// (GuiPlateColumns::revision, render.h; GuiListCanvasSink::plate). A list
// with no plate record leaves the texture and its key as they are: nothing
// samples it, and its texels still answer the key.
void GuiGlCanvas::upload_list(const GuiCanvasDrawList& list, int frame_px) {
    static std::vector<CanvasInstance>      instances;
    static std::vector<GLshort>             texels;
    static std::vector<std::pair<int, int>> spans;
    const int f = frame_px;
    spans.clear();
    int max_w = 1;
    for (const GuiCanvasPrimitive& p : list.prims) {
        if (p.kind != GuiCanvasKind::Plate) continue;
        spans.emplace_back(p.period, p.r.w);
        max_w = std::max(max_w, p.r.w);
    }
    const int plates = static_cast<int>(spans.size());
    const int rows   = std::max(2, 2 * plates);
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
        in.rect[0]  = p.r.x + f;
        in.rect[1]  = p.r.y + f;
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
            break;
        case GuiCanvasKind::DotsColumn:
            in.param[2] = p.origin + f;
            break;
        case GuiCanvasKind::DotsRow:
            in.param[2] = p.origin + f;
            break;
        case GuiCanvasKind::Plate:
            if (pack) {
                for (int j = 0; j < p.r.w; ++j) {
                    const WaveformColumnRows& c =
                        list.columns[static_cast<size_t>(p.period + j)];
                    GLshort* a = texel(row, j);
                    a[0] = clamp16(c.o0);
                    a[1] = clamp16(c.o1);
                    a[2] = clamp16(c.i0);
                    a[3] = clamp16(c.i1);
                    GLshort* b = texel(row + 1, j);
                    b[0] = clamp16(c.lo);
                    b[1] = clamp16(c.hi);
                }
            }
            in.param[1] = row;
            in.param[2] = p.origin + f;
            row += 2;
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
