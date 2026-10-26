#include "score_viewer.h"

#include "modal_box.h"     // modal_area_rect: the viewer's damage
#include "notifications.h"
#include "svg_icon.h"      // the materials' road: raster_info, raster_regions
#include "text_editor.h"   // next_session_id (the one modal counter)

#include <cstdio>
#include <expected>
#include <fstream>
#include <iterator>
#include <string>

namespace {

// THE ONE CARD (bare `m` on a piece whose score_path names no loaded score;
// the key itself is grey there).
constexpr const char* kNoScoreCard = "This piece has no score";

// Is `index` a page the shown page's neighborhood holds?
bool in_neighborhood(const AppState& a, int index) {
    const ScoreViewerRange r = score_viewer_range(a);
    const int p = a.score_viewer.page;
    return index >= r.first && index <= r.last && index >= p - 1 &&
           index <= p + 1;
}

bool cached(const AppState& a, int index) {
    for (const AppState::ScoreViewer::Decoded& d : a.score_viewer.cache)
        if (d.page == index) return true;
    return false;
}

}  // namespace

bool GuiScoreViewer::open() {
    AppState::ScoreViewer& sv = app.score_viewer;
    if (sv.active) return false;
    if (app.prompt.active) return false;
    if (!app.score) {
        notifications.notify(AppState::NotificationClass::Normal, kNoScoreCard);
        return false;
    }
    playback_lifecycle.stop_playback_for_modal_open();
    sv.active  = true;
    sv.session = text_editor::next_session_id();
    sv.painted_page = -1;
    sv.scrub   = AppState::ScoreViewer::ScrubDrag{};
    for (AppState::ScoreViewer::Decoded& d : sv.cache) d.reset();
    // THE OPEN LANDS, IT DOES NOT CHANGE A PAGE: the remembered position is
    // read and left as it stands (a position inside a page names that page
    // already), so the open writes nothing.
    sv.page = score_page_for_position(app, active_view_state(app).score_position);
    decode_into_cache(sv.page);
    viewport.invalidate_all();
    return true;
}

void GuiScoreViewer::close() {
    AppState::ScoreViewer& sv = app.score_viewer;
    if (!sv.active) return;
    sv.active = false;
    sv.scrub  = AppState::ScoreViewer::ScrubDrag{};
    for (AppState::ScoreViewer::Decoded& d : sv.cache) d.reset();
    viewport.invalidate_all();
}

void GuiScoreViewer::show_page(int index) {
    AppState::ScoreViewer& sv = app.score_viewer;
    if (!sv.active || !app.score) return;
    const ScoreViewerRange r = score_viewer_range(app);
    index = std::clamp(index, r.first, r.last);
    if (index == sv.page) return;
    sv.page = index;
    // EVERY PAGE CHANGE WRITES THE POSITION (the head): the active tab's
    // band, the page's own begin — display scratch, nothing dirtied.
    active_view_state(app).score_position =
        app.score->pages[static_cast<std::size_t>(index)].begin;
    evict_outside_neighborhood();
    if (!cached(app, index)) decode_into_cache(index);
    damage();
}

void GuiScoreViewer::step(int delta) {
    show_page(app.score_viewer.page + delta);
}

void GuiScoreViewer::first_page() {
    if (!app.score_viewer.active || !app.score) return;
    show_page(score_viewer_range(app).first);
}

void GuiScoreViewer::last_page() {
    if (!app.score_viewer.active || !app.score) return;
    show_page(score_viewer_range(app).last);
}

void GuiScoreViewer::set_start() {
    if (!score_viewer_button_enabled(app, AppState::ScoreButtonAct::SetStart))
        return;
    app.score_first_page = app.score_viewer.page + 1;
    evict_outside_neighborhood();
    damage();
}

void GuiScoreViewer::set_end() {
    if (!score_viewer_button_enabled(app, AppState::ScoreButtonAct::SetEnd))
        return;
    app.score_last_page = app.score_viewer.page + 1;
    evict_outside_neighborhood();
    damage();
}

void GuiScoreViewer::raise_reset() {
    if (app.prompt.active) return;
    if (!score_viewer_button_enabled(app, AppState::ScoreButtonAct::Reset))
        return;
    app.prompt.present("Reset the page range to every page?",
                       kPromptOkCancel,
                       DialogTrigger::SCORE_RESET_CONFIRM,
                       PromptInitialFocus::LastButton);
    viewport.invalidate_all();
}

void GuiScoreViewer::confirm_reset() {
    if (!app.score_viewer.active) return;
    app.score_first_page = 0;
    app.score_last_page  = 0;
    damage();
}

void GuiScoreViewer::tick() {
    const AppState::ScoreViewer& sv = app.score_viewer;
    if (!sv.active || !app.score) return;
    // NOT BEFORE THE SHOWN PAGE IS ON SCREEN (the head): the turn's own frame
    // paints first, the neighbors after it.
    if (sv.painted_page != sv.page) return;
    // The next page first, then the previous: the page forward is the likelier
    // turn. (The shown page is decoded at its change, show_page.) A neighbor
    // paints nothing until it is shown, so it owes no damage.
    for (const int index : {sv.page + 1, sv.page - 1}) {
        if (!in_neighborhood(app, index) || cached(app, index)) continue;
        decode_into_cache(index);
        return;
    }
}

void GuiScoreViewer::decode_into_cache(int index) {
    AppState::ScoreViewer& sv = app.score_viewer;
    AppState::ScoreViewer::Decoded* slot = nullptr;
    for (AppState::ScoreViewer::Decoded& d : sv.cache)
        if (d.page < 0) { slot = &d; break; }
    if (slot == nullptr) {
        // Every slot holds a page: the shown page's neighborhood is at most
        // three, so one of them is outside it.
        evict_outside_neighborhood();
        for (AppState::ScoreViewer::Decoded& d : sv.cache)
            if (d.page < 0) { slot = &d; break; }
        if (slot == nullptr) return;
    }
    slot->page = index;
    const std::filesystem::path file = app.score->page_file(index);
    const auto fail = [&](const std::string& words) {
        std::fprintf(stderr, "warptempo_gui: Score page '%s' did not decode: %s\n",
                     file.string().c_str(), words.c_str());
    };
    std::ifstream f(file, std::ios::binary);
    if (!f) { fail("could not open it"); return; }
    const std::string bytes{std::istreambuf_iterator<char>(f),
                            std::istreambuf_iterator<char>()};
    if (f.bad()) { fail("could not read it whole"); return; }
    const auto info = svg_icon::raster_info(bytes);
    if (!info) { fail(info.error()); return; }
    const svg_icon::RasterRegion whole{0, 0, static_cast<int>(info->w),
                                       static_cast<int>(info->h)};
    auto out = svg_icon::raster_regions(bytes, *info, 1.0,
                                        std::span(&whole, 1));
    if (!out) { fail(out.error()); return; }
    if (out->empty() || (*out)[0] == nullptr) { fail("no pixels"); return; }
    cairo_surface_t* surface = (*out)[0];
    svg_icon::convert_to_display(surface);
    slot->surface = surface;
}

void GuiScoreViewer::evict_outside_neighborhood() {
    for (AppState::ScoreViewer::Decoded& d : app.score_viewer.cache)
        if (d.page >= 0 && !in_neighborhood(app, d.page)) d.reset();
}

void GuiScoreViewer::damage() {
    viewport.invalidate_rect(modal_area_rect(app, /*keyboard_h=*/0));
}
