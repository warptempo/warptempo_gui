#include "score_page_worker.h"

#include "display_transform.h"   // p3_premultiplied, the job's bit's transform
#include "svg_icon.h"            // the materials' road: raster_info, raster_regions

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <span>

namespace {

// THE DECODE, the head's three steps; a failure prints its one line and
// answers no surface.
cairo_surface_t* decode_fit_page(const GuiScorePageJob& job) {
    const auto fail = [&](const std::string& words) -> cairo_surface_t* {
        std::fprintf(stderr,
                     "warptempo_gui: Score page '%s' did not decode: %s\n",
                     job.file.string().c_str(), words.c_str());
        return nullptr;
    };
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
        svg_icon::raster_regions(bytes, *info, 1.0, std::span(&whole, 1));
    if (!decoded) return fail(decoded.error());
    if (decoded->empty() || (*decoded)[0] == nullptr) return fail("no pixels");
    cairo_surface_t* src = (*decoded)[0];

    // THE FIT: the smaller of the two ratios, each side rounded at the
    // element.
    const int pw = static_cast<int>(info->w);
    const int ph = static_cast<int>(info->h);
    const double k = std::min(static_cast<double>(job.key.box_w) / pw,
                              static_cast<double>(job.key.box_h) / ph);
    const int dw = static_cast<int>(std::nearbyint(pw * k));
    const int dh = static_cast<int>(std::nearbyint(ph * k));
    if (dw <= 0 || dh <= 0) {
        cairo_surface_destroy(src);
        return fail("the box has no room for it");
    }
    cairo_surface_t* out =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
    if (cairo_surface_status(out) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(out);
        cairo_surface_destroy(src);
        return fail("no surface for it");
    }
    cairo_t* cr = cairo_create(out);
    cairo_scale(cr, static_cast<double>(dw) / pw, static_cast<double>(dh) / ph);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(src);
    cairo_surface_flush(out);

    if (job.key.convert) {
        // THE WINDOW'S COPY, pixel by pixel, memoised on the last word: a page
        // is runs of one paper and one ink, so most pixels repeat the one
        // before them.
        unsigned char* data   = cairo_image_surface_get_data(out);
        const int      stride = cairo_image_surface_get_stride(out);
        uint32_t last_in = 0, last_out = 0;
        bool     have    = false;
        for (int y = 0; y < dh; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(data + y * stride);
            for (int x = 0; x < dw; ++x) {
                if (!have || row[x] != last_in) {
                    last_in  = row[x];
                    last_out = display_transform::p3_premultiplied(last_in);
                    have     = true;
                }
                row[x] = last_out;
            }
        }
        cairo_surface_mark_dirty(out);
    }
    return out;
}

}  // namespace

GuiScorePageWorker::~GuiScorePageWorker() { shutdown(); }

void GuiScorePageWorker::request(std::vector<GuiScorePageJob> jobs) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        queue_.clear();
        for (GuiScorePageJob& j : jobs) {
            if (current_ && *current_ == j.key) continue;
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
    for (auto& [g, r] : taken) {
        if (g == gen) {
            out.push_back(r);
        } else if (r.surface != nullptr) {
            cairo_surface_destroy(r.surface);
        }
    }
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
    for (auto& [g, r] : done_)
        if (r.surface != nullptr) cairo_surface_destroy(r.surface);
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
            current_ = job.key;
            gen      = generation_;
        }
        GuiScorePageResult r;
        r.key     = job.key;
        r.surface = decode_fit_page(job);
        std::lock_guard<std::mutex> lk(mtx_);
        current_.reset();
        done_.emplace_back(gen, r);
    }
}
