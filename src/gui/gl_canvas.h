#pragma once
#include <cairo/cairo.h>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

struct GuiCanvasDrawList;

// THE GL CANVAS'S OWNER (2026-10-14; the design record is
// docs/engineering/gpu_canvas_2026_10.md): everything of the GPU canvas that
// is GLES 3.0 and neither EGL nor a window — the two programs and their
// shaders, the backbuffer's texture, the canvas list's instance buffer and
// the plate's bounds texture, the upload walk and the frame's draw — in ONE
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
    // THE TWO PROGRAMS, built at the first make current (each a no-op once
    // made): the texture triangle's, and the canvas list's with its vertex
    // array, its instance buffer and the plate's bounds texture.
    void ensure_programs();
    // THE CANVAS TEXTURE FOLLOWS THE BACKBUFFER (the definition): remade at
    // a size of w x h it does not have.
    void ensure_texture(int w, int h);
    // THE FRAME (the definition): the upload walk of `damage` out of `back`
    // into the texture, less the canvas clip; the clear; the list's draw
    // under the scissor; the texture's draw over it. `frame_px` the window's
    // frame (the GUI's client px plus it are surface px); `list` the GUI's
    // canvas frame, null before the first; `surface_w` x `surface_h` the
    // surface's size, the viewport. FALSE, with no GL call made, when there
    // is no texture yet or the backbuffer has no data: the platform then
    // swaps nothing and keeps its damage. The platform swaps on true.
    bool present_frame(cairo_surface_t* back, std::span<const GuiGlRect> damage,
                       int frame_px, const GuiCanvasDrawList* list,
                       int surface_w, int surface_h);
    // THE CONTEXT'S END (the platform's exit): every name forgotten, the
    // context's destruction having freed the objects themselves — no GL call
    // is made, so it is asked after the platform has released and destroyed
    // its context.
    void forget();

private:
    void upload_list(const GuiCanvasDrawList& list, int frame_px);

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
    // THE LAST UPLOAD WALK'S CANVAS (present_frame's upload rule): its clip
    // in surface px, its floater bit, and whether the texture has had a walk
    // since it was made (ensure_texture clears it).
    int          upload_clip_[4]      = {0, 0, 0, 0};
    bool         upload_floater_      = false;
    bool         upload_seeded_       = false;
};
