#include "score_page_worker.h"

#include "svg_icon.h"            // the raster road: raster_info, raster_regions

#include <cairo/cairo.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <span>

namespace {

using Clock = std::chrono::steady_clock;

double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// THE FILE'S ROAD, the head's steps; a failure prints its one line and
// answers no gray. `r` carries the work.
void decode_fit_page(const GuiScorePageJob& job, GuiScorePageResult& r) {
    const auto fail = [&](const std::string& words) {
        std::fprintf(stderr,
                     "warptempo_gui: Score page '%s' did not decode: %s\n",
                     job.file.string().c_str(), words.c_str());
    };
    const Clock::time_point t0 = Clock::now();
    std::ifstream f(job.file, std::ios::binary);
    if (!f) return fail("could not open it");
    const std::string bytes{std::istreambuf_iterator<char>(f),
                            std::istreambuf_iterator<char>()};
    if (f.bad()) return fail("could not read it whole");
    const auto info = svg_icon::raster_info(bytes);
    if (!info) return fail(info.error());
    const svg_icon::RasterRegion whole{0, 0, static_cast<int>(info->w),
                                       static_cast<int>(info->h)};
    auto decoded =
        svg_icon::raster_regions(bytes, *info, std::span(&whole, 1));
    if (!decoded) return fail(decoded.error());
    if (decoded->empty() || (*decoded)[0] == nullptr) return fail("no pixels");
    cairo_surface_t* src = (*decoded)[0];
    const Clock::time_point t_decode = Clock::now();

    // THE FIT: the smaller of the two ratios, each side rounded at the
    // element.
    const GuiScorePageKey& key = job.work.key;
    const int pw = static_cast<int>(info->w);
    const int ph = static_cast<int>(info->h);
    const double k = std::min(static_cast<double>(key.box_w) / pw,
                              static_cast<double>(key.box_h) / ph);
    const int dw = static_cast<int>(std::nearbyint(pw * k));
    const int dh = static_cast<int>(std::nearbyint(ph * k));
    if (dw <= 0 || dh <= 0) {
        cairo_surface_destroy(src);
        return fail("the box has no room for it");
    }
    cairo_surface_t* out    = nullptr;
    bool             scaled = false;
    if (dw == pw && dh == ph) {
        // THE IDENTITY (2026-10-26): a page already at the fit size — the
        // tool's pre-sized page, tools/score_extract.py's --fit — runs no
        // scale pass; the decode's surface, a new opaque ARGB32 surface this
        // thread owns (raster_regions' contract), is the result.
        out = src;
    } else {
        out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
        if (cairo_surface_status(out) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(out);
            cairo_surface_destroy(src);
            return fail("no surface for it");
        }
        cairo_t* cr = cairo_create(out);
        cairo_scale(cr, static_cast<double>(dw) / pw,
                    static_cast<double>(dh) / ph);
        cairo_set_source_surface(cr, src, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(src);
        scaled = true;
    }
    cairo_surface_flush(out);
    const Clock::time_point t_scale = Clock::now();
    unsigned char* data   = cairo_image_surface_get_data(out);
    const int      stride = cairo_image_surface_get_stride(out);

    // THE GRAY (the head): the low byte of each fit-size word.
    {
        auto g = std::make_shared<std::vector<uint8_t>>(
            static_cast<std::size_t>(dw) * static_cast<std::size_t>(dh));
        for (int y = 0; y < dh; ++y) {
            const auto* row =
                reinterpret_cast<const uint32_t*>(data + y * stride);
            uint8_t* to = g->data() + static_cast<std::size_t>(y) *
                                          static_cast<std::size_t>(dw);
            for (int x = 0; x < dw; ++x)
                to[x] = static_cast<uint8_t>(row[x] & 0xFFu);
        }
        r.gray = GuiScoreGrayPage{key.page, key.box_w, key.box_h, dw, dh,
                                  std::move(g)};
    }
    const Clock::time_point t_gray = Clock::now();
    cairo_surface_destroy(out);

    // THE TIMING LINE (the head), the arms that ran.
    char line[160];
    int  n = std::snprintf(line, sizeof line, "decode %.1f ms",
                           ms_between(t0, t_decode));
    const auto arm = [&](const char* name, Clock::time_point a,
                         Clock::time_point b) {
        if (n > 0 && static_cast<std::size_t>(n) < sizeof line)
            n += std::snprintf(line + n, sizeof line - static_cast<std::size_t>(n),
                               ", %s %.1f ms", name, ms_between(a, b));
    };
    if (scaled) arm("scale", t_decode, t_scale);
    arm("gray", t_scale, t_gray);
    std::fprintf(stderr, "warptempo_gui: score page %d: %s\n", key.page + 1,
                 line);
}

GuiScorePageResult run_job(const GuiScorePageJob& job) {
    GuiScorePageResult r;
    r.work = job.work;
    decode_fit_page(job, r);
    return r;
}

}  // namespace

GuiScorePageWorker::~GuiScorePageWorker() { shutdown(); }

void GuiScorePageWorker::request(std::vector<GuiScorePageJob> jobs) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        queue_.clear();
        for (GuiScorePageJob& j : jobs) {
            if (current_ && *current_ == j.work) continue;
            if (std::any_of(done_.begin(), done_.end(), [&](const auto& d) {
                    return d.first == generation_ && d.second.work == j.work;
                }))
                continue;
            queue_.push_back(std::move(j));
        }
        if (!thread_.joinable())
            thread_ = std::jthread([this](std::stop_token st) { run(st); });
    }
    cv_.notify_one();
}

void GuiScorePageWorker::reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    ++generation_;
    queue_.clear();
}

std::vector<GuiScorePageResult> GuiScorePageWorker::drain() {
    std::vector<std::pair<uint64_t, GuiScorePageResult>> taken;
    uint64_t gen = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (done_.empty()) return {};
        taken.swap(done_);
        gen = generation_;
    }
    std::vector<GuiScorePageResult> out;
    for (auto& [g, r] : taken)
        if (g == gen) out.push_back(std::move(r));
    return out;
}

void GuiScorePageWorker::shutdown() {
    if (thread_.joinable()) {
        thread_.request_stop();
        cv_.notify_one();
        thread_.join();
    }
    thread_ = std::jthread{};
    std::lock_guard<std::mutex> lk(mtx_);
    queue_.clear();
    current_.reset();
    done_.clear();
}

void GuiScorePageWorker::run(std::stop_token stop) {
    for (;;) {
        GuiScorePageJob job;
        uint64_t        gen = 0;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            if (!cv_.wait(lk, stop, [&] { return !queue_.empty(); })) return;
            job = std::move(queue_.front());
            queue_.pop_front();
            current_ = job.work;
            gen      = generation_;
        }
        GuiScorePageResult r = run_job(job);
        std::lock_guard<std::mutex> lk(mtx_);
        current_.reset();
        done_.emplace_back(gen, std::move(r));
    }
}
