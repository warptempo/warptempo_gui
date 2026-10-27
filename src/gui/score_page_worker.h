#pragma once

// THE SCORE VIEWER'S PAGE DECODER (architect 2026-10-26 night: "it's a
// little laggy and slow when I turn pages, when I open, when I close. If
// there's going to be an additional visual lag on the page turning on top of
// the Bluetooth lag, that's not going to work") — ONE std::jthread, held by
// the viewer's state (AppState::ScoreViewer::decoder), that turns a page's
// JPEG (tools/score_extract.py's grayscale page, 2026-10-26) into the
// surface the painter blits 1:1 and the gray bytes the viewer's store holds,
// so NO PAGE IS EVER DECODED, SCALED OR EXPANDED ON THE MAIN THREAD, the
// open's included.
//
// A JOB is a page at a FIT BOX (the modal box's interior, its w x h,
// modal_box_interior_rect) and the window's conversion bit as the GUI thread
// read it (display_transform::active — the switch's two bits are the GUI
// thread's, display_transform.h's head, so the job carries the answer), and
// what it is to yield: THE PAINTED SURFACE (the viewer's cache), THE GRAY
// BYTES (the viewer's gray store, AppState::ScoreViewer::gray; architect
// 2026-10-26 ~22:10), or both. THREE KINDS, the viewer's feed choosing:
//   * FROM THE FILE, BOTH — a page the cache wants and the store lacks;
//   * FROM THE FILE, GRAY ALONE — a page of the viewing range the store
//     lacks and the cache does not want;
//   * AN EXPANSION — a page the cache wants and the store holds: the store's
//     bytes carried to this thread by their shared owner, never copied, and
//     no file read.
// THE FILE'S ROAD: the decode through the materials' road at k = 1
// (svg_icon::raster_info and raster_regions, which any thread may run:
// svg_icon.h's THREADS block), the scale to the page's FIT SIZE — the page
// scaled uniformly by the smaller of the box's two ratios, each side rounded
// at the element — by cairo's GOOD filter onto a surface this thread owns (a
// box filter on a reduction: the page is a picture of thin staff lines,
// which a plain bilinear tap drops), the edge extended so every pixel stays
// opaque; then THE GRAY, one byte a pixel taken from the fit-size ARGB
// before any conversion — the low (blue) byte of each word: the page is gray
// already (tools/score_extract.py writes grayscale JPEG, 2026-10-26), so its
// three channels are one and any of them is the page, and every pixel is
// opaque, so premultiplication leaves it whole; then, with the bit set,
// every pixel of the surface through display_transform::p3_premultiplied
// (the page an authored sRGB picture like every raster). A page already at
// the fit size, the tool's pre-sized page (its --fit, the box this viewer
// prints at the open: score_viewer.cpp's feed_decoder), skips the scale: the
// decode's own surface is the result. THE EXPANSION'S ROAD: each gray byte g
// into the opaque word FF g g g, which is the file road's surface exactly for
// a gray page, and NO CONVERSION PASS (expand_gray's proof: sRGB -> P3 is
// the identity on every neutral word). EVERY RESULT is a NEW opaque ARGB32
// surface at the fit size and / or a new gray buffer. A page whose decode
// fails is a result with neither and ONE stderr line, the rare non-silent
// fault: the viewer marks it and never asks for it again while that key
// stands. A failed allocation of a buffer is std::bad_alloc, no recovery
// (architect 2026-10-26: the store has no memory backstop).
//
// THE TIMING LINE (architect 2026-10-26 ~22:10, the measurements the next
// ruling reads off logcat): one stderr line a job, steady clock, one
// decimal, naming the arms that ran — `warptempo_gui: score page <n>: decode
// <x> ms, scale <y> ms, gray <z> ms, convert <w> ms` for the file's road,
// `expand <z> ms` for an expansion; <n> the viewer's page number (the index
// plus one, score_pages.h).
//
// THE QUEUE IS THE MAIN THREAD'S ORDER, REPLACED WHOLE (request): the viewer's
// tick hands the worker the work its cache and its store lack, most wanted
// first, every time that list changes; the job in progress is never
// interrupted, and a job naming the work in progress, or a finished result
// not yet drained, is dropped from the new queue rather than done twice.
// THE RESULTS are drained on the main thread (drain, the viewer's tick), the
// receiver owning each surface and sharing each gray buffer. A RESET (the
// score folder changed: score_folder_changed, score_viewer.h) empties the
// queue and supersedes the job in progress by a generation: whatever an
// older generation delivers is freed at the drain, unseen.

#include <cairo/cairo.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
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

// ONE PAGE OF THE GRAY STORE (the head; AppState::ScoreViewer::gray): the
// page index and the fit box it was made for — NOT the conversion bit: the
// bytes are the page as authored, sRGB, the window's form being the cache's — and
// its fit-size pixels, one byte each, row-major, w x h, shared (immutable
// once made) so a job carries them to the worker without a copy. No bytes is
// a decode that failed under this page and box, never asked for again while
// they stand.
struct GuiScoreGrayPage {
    int page  = -1;
    int box_w = 0;
    int box_h = 0;
    int w     = 0;
    int h     = 0;
    std::shared_ptr<const std::vector<uint8_t>> bytes;
    bool failed() const { return bytes == nullptr; }
};

// WHAT A JOB DOES (the head's THREE KINDS), its identity in the queue and in
// the viewer's change test: the key (a gray-alone job's `convert` is false,
// unread), whether it yields the surface and the gray bytes, and whether it
// expands the store's bytes instead of reading the file.
struct GuiScorePageWork {
    GuiScorePageKey key;
    bool            surface = true;
    bool            gray    = false;
    bool            expand  = false;
    bool operator==(const GuiScorePageWork&) const = default;
};

struct GuiScorePageJob {
    GuiScorePageWork      work;
    // The file's road: the page's file. An expansion: the store's page.
    std::filesystem::path file;
    GuiScoreGrayPage      from;
};

struct GuiScorePageResult {
    GuiScorePageWork work;
    // The fit-size page when the work asked for it, OWNED BY THE RECEIVER
    // (cairo_surface_destroy); null where the decode failed (its stderr line
    // already printed).
    cairo_surface_t* surface = nullptr;
    // The gray bytes when the work asked for them (made from the file); no
    // bytes where the decode failed.
    GuiScoreGrayPage gray;
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
    // Under mtx_: the queue, the work in progress, the generation, the
    // results each tagged with the generation it was taken under.
    std::deque<GuiScorePageJob>                          queue_;
    std::optional<GuiScorePageWork>                      current_;
    uint64_t                                             generation_ = 0;
    std::vector<std::pair<uint64_t, GuiScorePageResult>> done_;
    std::jthread                                         thread_;
};
