#pragma once

// THE SCORE VIEWER'S PAGE DECODER (architect 2026-10-26 night: "it's a
// little laggy and slow when I turn pages, when I open, when I close. If
// there's going to be an additional visual lag on the page turning on top of
// the Bluetooth lag, that's not going to work") — ONE std::jthread, held by
// the viewer's state (AppState::ScoreViewer::decoder), that turns a page's
// JPEG (tools/score_extract.py's grayscale page, 2026-10-26) into the gray
// bytes the viewer's store holds (AppState::ScoreViewer::gray; architect
// 2026-10-26 ~22:10), the GL canvas's page textures' source (2026-10-27,
// score_viewer.h's head), so NO PAGE IS EVER DECODED OR SCALED ON THE MAIN
// THREAD, the open's included.
//
// A JOB is a page at a FIT BOX (the modal box's interior, its w x h,
// modal_box_interior_rect), read from the page's file. THE FILE'S ROAD: the
// decode at the page's own size through svg_icon.h's raster road
// (svg_icon::raster_info and raster_regions, which any thread may run:
// svg_icon.h's THREADS block), the scale to the page's FIT SIZE — the page
// scaled uniformly by the smaller of the box's two ratios, each side rounded
// at the element — by cairo's GOOD filter onto a surface this thread owns (a
// box filter on a reduction: the page is a picture of thin staff lines,
// which a plain bilinear tap drops), the edge extended so every pixel stays
// opaque; then THE GRAY, one byte a pixel taken from the fit-size ARGB —
// the low (blue) byte of each word: the page is gray already
// (tools/score_extract.py writes grayscale JPEG, 2026-10-26), so its three
// channels are one and any of them is the page, and every pixel is opaque,
// so premultiplication leaves it whole. A page already at the fit size, the
// tool's pre-sized page (its --fit, the box this viewer prints at the open:
// score_viewer.cpp's feed_decoder), skips the scale. NO CONVERSION PASS, BY
// PROOF (2026-10-26): display_transform's sRGB -> P3 (transform_word over
// kSrgbToP3, as p3_premultiplied runs it) maps every neutral word g g g to
// itself, all 256 checked (2026-10-27 again) — each matrix row sums to 1
// within 5e-16, so the linear value is decode(g) within 5 ulp, and the
// encode's decision levels stand at least 0.49 of a byte's interval from it
// on either side, a margin no rounding order can cross — so the gray byte is
// the window's byte under either conversion bit. EVERY RESULT is a new gray
// buffer. A page whose decode fails is a result with no bytes and ONE stderr
// line, the rare non-silent fault: the viewer marks it and never asks for it
// again while that box stands. A failed allocation of a buffer is
// std::bad_alloc, no recovery (architect 2026-10-26: the store has no memory
// backstop).
//
// THE TIMING LINE (architect 2026-10-26 ~22:10, the measurements the next
// ruling reads off logcat): one stderr line a job, steady clock, one
// decimal, naming the arms that ran — `warptempo_gui: score page <n>: decode
// <x> ms, scale <y> ms, gray <z> ms`; <n> the viewer's page number (the
// index plus one, score_pages.h).
//
// THE QUEUE IS THE MAIN THREAD'S ORDER, REPLACED WHOLE (request): the viewer's
// tick hands the worker the pages its store lacks, most wanted first, every
// time that list changes; the job in progress is never interrupted, and a
// job naming the work in progress, or a finished result not yet drained, is
// dropped from the new queue rather than done twice. THE RESULTS are drained
// on the main thread (drain, the viewer's tick), the receiver sharing each
// gray buffer. A RESET (the score folder changed: score_folder_changed,
// score_viewer.h) empties the queue and supersedes the job in progress by a
// generation: whatever an older generation delivers is let go at the drain,
// unseen.

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

// WHAT A DECODED PAGE IS FOR: the page index and the fit box. A store entry
// answers only under the box the interior is now (the feed's scope).
struct GuiScorePageKey {
    int  page    = -1;
    int  box_w   = 0;
    int  box_h   = 0;
    bool operator==(const GuiScorePageKey&) const = default;
};

// ONE PAGE OF THE GRAY STORE (the head; AppState::ScoreViewer::gray): the
// page index and the fit box it was made for — NOT the conversion bit: the
// bytes are the page as authored, sRGB, which is the window's byte under
// either bit (the head's proof) — and its fit-size pixels, one byte each, row-major, w x h, shared (immutable
// once made) so a job carries them to the worker without a copy. `serial`
// NAMES THE BYTES FOR THE GL CANVAS (2026-10-27, the GPU page: GuiCanvasPage,
// canvas_sink.h): minted when the entry enters the store
// (GuiScoreViewer::drain_decoder) from a counter the process never resets, so
// one serial is one immutable buffer for the process's life and the texture
// made from it is never stale; 0 before the store takes it.
// THREE STATES, by `bytes` and `released`:
//   * HELD IN RAM — bytes, not released: until the page's texture is held;
//   * RELEASED — no bytes, `released` (architect 2026-10-27, the memory
//     fix): the GL canvas's report listed the serial as
//     held, so the RAM copy, which then has no reader, was let go
//     (GuiScoreViewer::settle_gray_on_gpu). The entry still stands — the page
//     is had, w x h its size — and the texture is its picture; an entry whose
//     texture goes leaves the store with it, so a released entry is always a
//     held texture;
//   * FAILED — no bytes, not released: a decode that failed under this page
//     and box, never asked for again while they stand.
struct GuiScoreGrayPage {
    int page  = -1;
    int box_w = 0;
    int box_h = 0;
    int w     = 0;
    int h     = 0;
    std::shared_ptr<const std::vector<uint8_t>> bytes;
    uint64_t serial   = 0;
    bool     released = false;
    bool failed() const { return bytes == nullptr && !released; }
};

// WHAT A JOB DOES, its identity in the queue and in the viewer's change test:
// the page at its fit box (the key).
struct GuiScorePageWork {
    GuiScorePageKey key;
    bool operator==(const GuiScorePageWork&) const = default;
};

struct GuiScorePageJob {
    GuiScorePageWork      work;
    // The page's file (the file's road).
    std::filesystem::path file;
};

struct GuiScorePageResult {
    GuiScorePageWork work;
    // The gray bytes; no bytes where the decode failed (its stderr line
    // already printed).
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
    // end), every undrained result let go. Idempotent; the project's teardown
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
