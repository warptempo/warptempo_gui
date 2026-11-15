#pragma once
#include "canvas_sink.h"   // GuiCanvasPrimitive, WaveformColumnRows: the ahead kit's copy

#include <cairo/cairo.h>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

// THE GL CANVAS'S OWNER (2026-10-14; the design record is
// docs/engineering/gpu_canvas_2026_10.md): everything of the GPU canvas that
// is GLES 3.0 and neither EGL nor a window — the four programs and their
// shaders, the backbuffer's texture, the canvas list's instance buffer, the
// plate's bounds texture and the score viewer's page textures, THE FRAME
// TARGET (architect 2026-11-15, send L: the canvas keeps its own frame, an
// offscreen RGBA8 target holding the whole picture of the last frame
// composed), THE AHEAD TARGET and its kit (send M, 2026-11-15: the next
// page's picture composed ahead, a slice a frame, and copied at the turn),
// the upload walk, the frame's compose into the target and the
// copy out of it into the window's buffer — in ONE platform-neutral object
// the platform's present drives (the one canvas road, architect 2026-10-30).
// THE PLATFORM KEEPS
// WHAT IS ITS OWN: the display, the config, the context and the window's
// surface, the make current, the swap interval, the swap and its failure
// rule (GuiPlatform's EGL members; platform_android.cpp's ensure_gl_surface
// and present). Every call here is made with the platform's context current.
//
// THIS HEADER EXPOSES NO GL TYPE AND INCLUDES NO GL HEADER TO ITS CONSUMERS:
// GL object names are spelled `unsigned int` and uniform locations `int`
// (GLuint and GLint under the NDK's GLES 3.0). The GL RENDERING calls are
// gl_canvas.cpp's alone; the platform's translation unit includes
// <GLES3/gl3.h> itself for its one driver-identity line (glGetString), never
// for a render call.

// THE LOG SEAM: the owner names no platform's log. A failure of its setup —
// a shader that fails to compile, a program that fails to link, a frame
// target that is incomplete (ensure_texture, 2026-11-15) — is fatal
// (the platform's gl_fatal rule: no fallback road at run time), and `fatal`
// is handed the whole message, written as one line by the platform's own
// fatal log and never returning (the owner aborts behind it all the same).
struct GuiGlCanvasHost {
    void (*fatal)(const char* message) = nullptr;
};

// ONE DAMAGE RECT in surface px, the shape the platform's damage list holds
// (x, y, w, h), so a present passes its list in place.
struct GuiGlRect { int x, y, w, h; };

class GuiGlCanvas {
public:
    // Named once, before the first ensure_programs (the platform's first
    // context).
    void set_host(const GuiGlCanvasHost& host) { host_ = host; }

    // THE CONTEXT'S FIXED STATE, set at each make current: no dither, no
    // depth or scissor test, the blend off at rest with its function set
    // (the premultiplied source-over of the plate's fringe and the tint,
    // enabled for the canvas list's draw alone, 2026-10-30).
    void bind_fixed_state();
    // THE FOUR PROGRAMS, built at the first make current (each a no-op once
    // made): the texture triangle's, the canvas list's — at the plate's
    // default grow, the source being specialized per grow (build_list_program
    // below) — with its vertex array, its instance buffer and the plate's
    // bounds texture, the GPU page's (2026-10-27), and THE COPY's (architect
    // 2026-11-15, send L; kCopyFragmentShader's head), its sampler on unit 3;
    // made with the list's objects, THE AHEAD KIT's (send M, 2026-11-15) —
    // its vertex array, instance buffer and bounds texture — and THE
    // SUFFIX'S vertex array over the list's instance buffer.
    void ensure_programs();
    // THE CANVAS TEXTURE FOLLOWS THE BACKBUFFER, AND THE FRAME TARGET AND THE
    // AHEAD TARGET WITH IT (the definition): all three remade at a size of
    // w x h the texture does not have, by the one size test; the default
    // framebuffer bound at the return.
    void ensure_texture(int w, int h);
    // THE FRAME (the definition): the upload walk of `damage` out of `back`
    // into the texture, less the canvas clip; the page textures reconciled
    // with the list's request; THE COMPOSE into the frame target — the clear;
    // the list's draw, or the page's while it stands, under the scissor; the
    // texture's draw over it — then THE COPY of `repair` out of the target
    // into the window's buffer (architect 2026-11-15, send L).
    // `list` the GUI's canvas frame (its px the surface's: the app paints no
    // window frame since 2026-10-24), null before the first, its page's
    // REPORT written here (GuiCanvasPage, canvas_sink.h); `surface_w` x
    // `surface_h` the surface's size, the viewport — the platform hands its
    // BACKBUFFER'S size, which is the whole surface's (each present's comment
    // says why not EGL's own answer). FALSE, with no GL call made, when there
    // is no texture yet or the backbuffer has no data: the platform then
    // swaps nothing and keeps its damage. The platform swaps on true.
    // `compose` (THE COMPOSE at the definition): THIS FRAME'S OWN CHANGE —
    // empty composes the whole frame; otherwise only its rects, each
    // recomposed through the whole pass order under a scissor, the target's
    // other pixels being the last frame's. `repair` (THE COPY at the
    // definition): the region the window's buffer is missing (the platform's
    // buffer-age history) — empty copies the whole surface; otherwise only
    // its rects, the buffer's other pixels being an earlier frame's copy.
    // `ahead` THE AHEAD LIST (send M, 2026-11-15; gui_canvas_ahead,
    // canvas_sink.h), null before the first frame: THE VERDICT before the
    // compose, THE ADOPTION in the list draw's place where it holds, THE
    // AHEAD STEP after the copy (the definition's THE AHEAD TARGET).
    bool present_frame(cairo_surface_t* back, std::span<const GuiGlRect> damage,
                       std::span<const GuiGlRect> compose,
                       std::span<const GuiGlRect> repair,
                       GuiCanvasDrawList* list,
                       const GuiCanvasDrawList* ahead,
                       int surface_w, int surface_h);
    // Whether present_frame would draw at all (a texture, a backbuffer with
    // data): the platform asks before it names a frame's damage region to
    // EGL, so no region is set for a frame that draws nothing.
    bool ready(cairo_surface_t* back) const;
    // THE COMPOSE'S VERDICT (the definition; architect 2026-11-15, send L):
    // true when this frame's change is not described by its damage — no
    // walk yet on this texture, the canvas clip moved, the score viewer's
    // page stands or stood last frame, the grow's program is to be rebuilt —
    // or the frame target holds no whole frame yet, so it is composed whole.
    bool compose_must_be_whole(const GuiCanvasDrawList* list) const;
    // THE CONTEXT'S END (the platform's exit): every name forgotten, the
    // context's destruction having freed the objects themselves — no GL call
    // is made, so it is asked after the platform has released and destroyed
    // its context.
    void forget();

private:
    // THE LIST PROGRAM AT A GROW (2026-10-31; the specialization rule at
    // kListFragmentShader's head, gl_canvas.cpp): linked from the source
    // assembled with `grow` as its GROW constant, its sampler bound to unit
    // 1, its two uniforms fetched, and the grow recorded as
    // list_program_grow_. ensure_programs builds it once at the default
    // grow; present_frame rebuilds it — the old program deleted — before a
    // draw whose list carries another grow.
    void build_list_program(int grow);
    void upload_list(const GuiCanvasDrawList& list);
    // THE ADOPTION'S VERDICT and THE AHEAD STEP (send M, 2026-11-15; the
    // rules at present_frame's THE AHEAD TARGET, gl_canvas.cpp).
    bool adoption_verdict(const GuiCanvasDrawList& list, const int clip[4]);
    void ahead_step(const GuiCanvasDrawList& ahead, const int clip[4],
                    int surface_w, int surface_h);
    // The list program's instanced draw, one spelling for the frame's list,
    // the suffix and the ahead slice (the definition).
    void draw_instances(unsigned int vao, unsigned int columns_texture,
                        int count, int surface_w, int surface_h);
    void reconcile_pages(GuiCanvasPage& page);
    void draw_page(GuiCanvasPage& page, const int clip[4]);
    static void frame_clip(const GuiCanvasDrawList* list, int clip[4],
                           bool& floater);

    GuiGlCanvasHost host_{};
    // The texture triangle's program and its one uniform, and THE BACKBUFFER'S
    // TEXTURE with its size (ensure_texture).
    unsigned int program_        = 0;
    int          height_uniform_ = -1;
    unsigned int texture_        = 0;
    int          texture_w_      = 0;
    int          texture_h_      = 0;
    // THE CANVAS LIST'S OBJECTS (the draw at present_frame, the records at
    // canvas_sink.h): the program with its two uniforms (the size and the
    // height) and THE GROW IT WAS COMPILED FOR (list_program_grow_, −1 while
    // no program stands: the plate's grow is a constant of the shader's
    // source, never a uniform — the specialization rule at
    // kListFragmentShader's head — so a list whose plate_grow differs has
    // the program rebuilt at its draw, build_list_program), its own vertex
    // array over the instance buffer (independent of the program: the
    // attribute locations are the vertex shader's layout qualifiers, so a
    // rebuild keeps it), the plate's bounds texture and its CAPACITY
    // (columns_w_ x columns_h_, which only grows: upload_list's rule,
    // architect 2026-11-14, send G) — made with
    // the texture program, living in the context — and the instances' count
    // and the list generation they were made from.
    // THE BOUNDS TEXTURE'S KEY (2026-10-14, upload_list's rule): whether its
    // texels are an upload's at all (false while it holds the seed, after
    // forget()), and the plate revision and the plate records' spans —
    // each record's offset into the list's columns and its bar count, in
    // list order — that upload packed.
    unsigned int list_program_        = 0;
    int          list_size_uniform_   = -1;
    int          list_height_uniform_ = -1;
    int          list_program_grow_   = -1;
    unsigned int list_vao_            = 0;
    unsigned int list_vbo_            = 0;
    int          list_instances_      = 0;
    uint64_t     list_generation_     = 0;
    bool         list_uploaded_       = false;
    unsigned int columns_texture_     = 0;
    int          columns_w_           = 0;
    int          columns_h_           = 0;
    bool         columns_uploaded_    = false;
    uint64_t     columns_revision_    = 0;
    std::vector<std::pair<int, int>> columns_spans_;
    // THE GPU PAGE'S OBJECTS (architect 2026-10-27, "1 a"; the request and
    // the report at GuiCanvasPage, canvas_sink.h; the draw at present_frame):
    // the page program and its uniforms, made with the others, and
    // THE PAGE TEXTURES — one immutable R8 texture a wanted page, on unit 2,
    // named by the store entry's serial with its size. THEIR LIFETIME IS THE
    // REQUEST'S WANTED LIST: at every present a texture whose serial the
    // list no longer names is deleted, and while the page stands the first
    // wanted page not yet held is uploaded, AT MOST kPageUploadsPerPresent a
    // present (one page, ~2.2 MB at the tablet's box), so no frame pays a
    // burst and a scrub over a resident range uploads nothing. The list is
    // the gray store's, so the textures go with the store's pages — the
    // range, the box, the score folder, the project — and survive the
    // viewer's close as the store does. A HELD TEXTURE IS THE PAGE'S ONE
    // PICTURE (architect 2026-10-27, the memory fix): the store lets its RAM
    // bytes go once the report lists the serial, and the request then
    // carries the page with no bytes, so an upload's source lives only until
    // the upload. No upload runs while the page does
    // not stand (the waveform's frames never pay one). forget() forgets the
    // names; the context's destruction frees the textures.
    struct PageTexture {
        uint64_t     serial = 0;
        unsigned int name   = 0;
        int          page   = -1;
        int          w      = 0;
        int          h      = 0;
    };
    unsigned int             page_program_        = 0;
    int                      page_height_uniform_ = -1;
    int                      page_rect_uniform_   = -1;
    int                      page_field_uniform_  = -1;
    std::vector<PageTexture> pages_;
    // THE LAST UPLOAD WALK'S CANVAS (present_frame's upload rule): its clip
    // in surface px, its floater bit, and whether the texture has had a walk
    // since it was made (ensure_texture clears it).
    int          upload_clip_[4]      = {0, 0, 0, 0};
    bool         upload_floater_      = false;
    bool         upload_seeded_       = false;
    // Whether the last frame drew the score viewer's page (the compose's
    // verdict: the page's leaving is composed whole too).
    bool         page_stood_          = false;
    // THE FRAME TARGET (architect 2026-11-15, send L; the rule at
    // present_frame's THE FRAME TARGET, gl_canvas.cpp): the framebuffer
    // object and its one color attachment, an immutable RGBA8 texture of the
    // backbuffer texture's size (ensure_texture makes and remakes both with
    // texture_), bound to a texture unit — unit 3, its own — for the copy
    // alone; and whether the target holds a whole composed frame (false at
    // the make, the remake and forget(); set by a whole compose), the
    // compose's verdict reading it. THE COPY'S PROGRAM: kCanvasVertexShader
    // with kCopyFragmentShader, its sampler on unit 3 (ensure_programs).
    unsigned int frame_fbo_           = 0;
    unsigned int frame_texture_       = 0;
    bool         frame_seeded_        = false;
    unsigned int copy_program_        = 0;
    // THE AHEAD TARGET (send M, 2026-11-15; the rule at present_frame's THE
    // AHEAD TARGET, gl_canvas.cpp): a second framebuffer object over an
    // immutable RGBA8 texture of the surface's size, made, remade and
    // forgotten with the frame target (ensure_texture, forget) — its texture
    // bound to a unit, unit 3, during an adoption's copy alone.
    unsigned int ahead_fbo_           = 0;
    unsigned int ahead_texture_       = 0;
    // THE AHEAD KIT: the ahead list as the GPU reads it — its own vertex
    // array over its own instance buffer, its own bounds texture and that
    // texture's capacity (the list's packing, pack_list, one routine for
    // both) and its instances' count — and WHAT THE TARGET HOLDS, kept on
    // the CPU: whether the kit holds an upload at all, the records and the
    // bounds it was composed from (the ahead list's records and columns,
    // and the plate revision those columns are), the clip and the grow it
    // was composed under, and how many of the clip's device columns are
    // composed, from the left (ahead_composed_, 0 at every upload, remake
    // and forget).
    unsigned int ahead_vao_           = 0;
    unsigned int ahead_vbo_           = 0;
    int          ahead_instances_     = 0;
    unsigned int ahead_columns_texture_ = 0;
    int          ahead_columns_w_     = 0;
    int          ahead_columns_h_     = 0;
    bool         ahead_kit_           = false;
    std::vector<GuiCanvasPrimitive> ahead_prims_;
    std::vector<WaveformColumnRows> ahead_columns_;
    uint64_t     ahead_revision_      = 0;
    int          ahead_clip_[4]       = {0, 0, 0, 0};
    int          ahead_grow_          = -1;
    int          ahead_composed_      = 0;
    // THE VERDICT'S BOUNDS COMPARE, kept per pair of revisions (a revision
    // names one set of bounds): valid while the kit holds the same upload,
    // for the frame list's plate revision `verdict_revision_`.
    bool         verdict_cached_      = false;
    uint64_t     verdict_revision_    = 0;
    bool         verdict_bounds_equal_ = false;
    // THE SUFFIX'S VERTEX ARRAY: the list's attributes over the list's own
    // instance buffer, starting `suffix_base_` records in (−1: never
    // pointed), re-pointed when an adopting frame's prefix differs.
    unsigned int suffix_vao_          = 0;
    int          suffix_base_         = -1;
};
