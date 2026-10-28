#pragma once
#include <cairo/cairo.h>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

struct GuiCanvasDrawList;
struct GuiCanvasPage;

// THE GL CANVAS'S OWNER (2026-10-14; the design record is
// docs/engineering/gpu_canvas_2026_10.md): everything of the GPU canvas that
// is GLES 3.0 and neither EGL nor a window — the three programs and their
// shaders, the backbuffer's texture, the canvas list's instance buffer, the
// plate's bounds texture and the score viewer's page textures, the upload
// walk and the frame's draw — in ONE
// platform-neutral object each platform's present drives. THE PLATFORM KEEPS
// WHAT IS ITS OWN: the display, the config, the context and the window's
// surface, the make current, the swap interval, the swap and its failure
// rule (GuiPlatform's EGL members; platform_android.cpp's ensure_gl_surface
// and present). Every call here is made with the platform's context current.
//
// THIS HEADER EXPOSES NO GL TYPE AND INCLUDES NO GL HEADER TO ITS CONSUMERS:
// GL object names are spelled `unsigned int` and uniform locations `int`
// (GLuint and GLint on every platform this builds for). The GL RENDERING
// calls are gl_canvas.cpp's alone; each platform's translation unit includes
// <GLES3/gl3.h> itself under the GL fence for its one driver-identity line
// (glGetString), never for a render call.

// THE LOG SEAM: the owner names no platform's log. A failure of its setup —
// a shader that fails to compile, a program that fails to link — is fatal
// (the platform's gl_fatal rule: no fallback road at run time), and `fatal`
// is handed the whole message, written as one line by the platform's own
// fatal log and never returning (the owner aborts behind it all the same).
struct GuiGlCanvasHost {
    void (*fatal)(const char* message) = nullptr;
};

// ONE DAMAGE RECT in surface px, the shape both platforms' damage lists hold
// (x, y, w, h), so a present passes its list in place.
struct GuiGlRect { int x, y, w, h; };

class GuiGlCanvas {
public:
    // Named once, before the first ensure_programs (the platform's first
    // context).
    void set_host(const GuiGlCanvasHost& host) { host_ = host; }

    // THE CONTEXT'S FIXED STATE, set at each make current: no dither, no
    // blend, no depth or scissor test.
    void bind_fixed_state();
    // THE THREE PROGRAMS, built at the first make current (each a no-op once
    // made): the texture triangle's, the canvas list's with its vertex
    // array, its instance buffer and the plate's bounds texture, and the GPU
    // page's (2026-10-27).
    void ensure_programs();
    // THE CANVAS TEXTURE FOLLOWS THE BACKBUFFER (the definition): remade at
    // a size of w x h it does not have.
    void ensure_texture(int w, int h);
    // THE FRAME (the definition): the upload walk of `damage` out of `back`
    // into the texture, less the canvas clip; the page textures reconciled
    // with the list's request; the clear; the list's draw — or the page's,
    // while it stands — under the scissor; the texture's draw over it.
    // `list` the GUI's canvas frame (its px the surface's: the app paints no
    // window frame since 2026-10-24), null before the first, its page's
    // REPORT written here (GuiCanvasPage, canvas_sink.h); `surface_w` x
    // `surface_h` the surface's size, the viewport — the platform hands its
    // BACKBUFFER'S size, which is the whole surface's (each present's comment
    // says why not EGL's own answer). FALSE, with no GL call made, when there
    // is no texture yet or the backbuffer has no data: the platform then
    // swaps nothing and keeps its damage. The platform swaps on true.
    bool present_frame(cairo_surface_t* back, std::span<const GuiGlRect> damage,
                       GuiCanvasDrawList* list,
                       int surface_w, int surface_h);
    // THE CONTEXT'S END (the platform's exit): every name forgotten, the
    // context's destruction having freed the objects themselves — no GL call
    // is made, so it is asked after the platform has released and destroyed
    // its context.
    void forget();

private:
    void upload_list(const GuiCanvasDrawList& list);
    void reconcile_pages(GuiCanvasPage& page);
    void draw_page(GuiCanvasPage& page, const int clip[4]);

    GuiGlCanvasHost host_{};
    // The texture triangle's program and its one uniform, and THE BACKBUFFER'S
    // TEXTURE with its size (ensure_texture).
    unsigned int program_        = 0;
    int          height_uniform_ = -1;
    unsigned int texture_        = 0;
    int          texture_w_      = 0;
    int          texture_h_      = 0;
    // THE CANVAS LIST'S OBJECTS (the draw at present_frame, the records at
    // canvas_sink.h): the program and its two uniforms, its own vertex array
    // over the instance buffer, the plate's bounds texture and its size —
    // made with the texture program, living in the context — and the
    // instances' count and the list generation they were made from.
    // THE BOUNDS TEXTURE'S KEY (2026-10-14, upload_list's rule): whether its
    // texels are an upload's at all (false while it holds the seed, after
    // forget()), and the plate revision and the plate records' spans —
    // each record's offset into the list's columns and its width, in list
    // order — that upload packed.
    unsigned int list_program_        = 0;
    int          list_size_uniform_   = -1;
    int          list_height_uniform_ = -1;
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
    // the page program and its uniforms, made with the other two, and
    // THE PAGE TEXTURES — one immutable R8 texture a wanted page, on unit 2,
    // named by the store entry's serial with its size. THEIR LIFETIME IS THE
    // REQUEST'S WANTED LIST: at every present a texture whose serial the
    // list no longer names is deleted, and while the page stands the first
    // wanted page not yet held is uploaded, AT MOST kPageUploadsPerPresent a
    // present (one page, ~2.2 MB at the tablet's box), so no frame pays a
    // burst and a scrub over a resident range uploads nothing. The list is
    // the gray store's, so the textures go with the store's pages — the
    // range, the box, the score folder, the project — and survive the
    // viewer's close as the store does. No upload runs while the page does
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
};
