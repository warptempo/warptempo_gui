#pragma once

// THE SCORE VIEWER'S PAGE DECODER (architect 2026-10-26 night: "it's a
// little laggy and slow when I turn pages, when I open, when I close. If
// there's going to be an additional visual lag on the page turning on top of
// the Bluetooth lag, that's not going to work") — ONE std::jthread, held by
// the viewer's state (AppState::ScoreViewer::decoder), that turns a page's
// PNG into the surface the painter blits 1:1, so NO PAGE IS EVER DECODED OR
// SCALED ON THE MAIN THREAD, the open's included.
//
// A JOB is a page's file at a FIT BOX (the modal box's interior, its w x h,
// modal_box_interior_rect) and the window's conversion bit as the GUI thread
// read it (display_transform::active — the switch's two bits are the GUI
// thread's, display_transform.h's head, so the job carries the answer). THE
// RESULT is a NEW opaque ARGB32 surface at the page's FIT SIZE — the page
// scaled uniformly by the smaller of the box's two ratios, each side rounded
// at the element — made in three steps: the decode through the materials'
// road at k = 1 (svg_icon::raster_info and raster_regions, which any thread
// may run: svg_icon.h's THREADS block), the scale by cairo's GOOD filter onto
// a surface this thread owns (a box filter on a reduction: the page is a
// picture of thin staff lines, which a plain bilinear tap drops), the edge
// extended so every pixel stays opaque, then, with the bit set, every pixel
// through display_transform::p3_premultiplied (the page an authored sRGB
// picture like every raster). A page whose decode fails is a result with no
// surface and ONE stderr line, the rare non-silent fault: the viewer marks it
// and never asks for it again while that key stands.
//
// THE QUEUE IS THE MAIN THREAD'S ORDER, REPLACED WHOLE (request): the viewer's
// tick hands the worker the pages its cache lacks, most wanted first, every
// time that list changes; the job in progress is never interrupted, and a job
// naming the key in progress is dropped from the new queue rather than done
// twice. THE RESULTS are drained on the main thread (drain, the viewer's
// tick), the receiver owning each surface. A RESET (the score folder changed:
// score_folder_changed, score_viewer.h) empties the queue and supersedes the
// job in progress by a generation: whatever an older generation delivers is
// freed at the drain, unseen.

#include <cairo/cairo.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// WHAT A DECODED PAGE IS FOR: the page index, the fit box and the conversion
// bit. A cached surface answers a paint only under the key the paint would
// ask (the box the interior is now, the bit the window has now).
struct GuiScorePageKey {
    int  page    = -1;
    int  box_w   = 0;
    int  box_h   = 0;
    bool convert = false;
    bool operator==(const GuiScorePageKey&) const = default;
};

struct GuiScorePageJob {
    GuiScorePageKey       key;
    std::filesystem::path file;
};

struct GuiScorePageResult {
    GuiScorePageKey  key;
    // The fit-size page, OWNED BY THE RECEIVER (cairo_surface_destroy); null
    // where the decode failed (its stderr line already printed).
    cairo_surface_t* surface = nullptr;
};

class GuiScorePageWorker {
public:
    GuiScorePageWorker() = default;
    ~GuiScorePageWorker();
    GuiScorePageWorker(const GuiScorePageWorker&)            = delete;
    GuiScorePageWorker& operator=(const GuiScorePageWorker&) = delete;

    // THE ORDER (the head): the queue replaced by `jobs`, first taken first;
    // the thread is started at the first request.
    void request(std::vector<GuiScorePageJob> jobs);
    // THE RESET (the head): the queue emptied and every job before this call
    // superseded.
    void reset();
    // Every result of the current generation since the last drain, in the
    // order they finished.
    std::vector<GuiScorePageResult> drain();
    // The thread asked to stop and joined (a decode in flight runs to its
    // end), every undrained surface freed. Idempotent; the project's teardown
    // calls it (main.cpp) and the destructor again.
    void shutdown();

private:
    void run(std::stop_token stop);

    std::mutex                  mtx_;
    std::condition_variable_any cv_;
    // Under mtx_: the queue, the key in progress, the generation, the results
    // each tagged with the generation it was taken under.
    std::deque<GuiScorePageJob>                          queue_;
    std::optional<GuiScorePageKey>                       current_;
    uint64_t                                             generation_ = 0;
    std::vector<std::pair<uint64_t, GuiScorePageResult>> done_;
    std::jthread                                         thread_;
};
