#include "render_player.h"

#include "folder_overlay.h"
#include "render_output_naming.h"   // render_output_directory (the deliverable)
#include "text_editor.h"            // next_session_id (the one modal counter)
#include "wav_io.h"                 // wav_probe, checked_audio_sample_count,
                                    // wav_read_full — called, never changed

#include <algorithm>
#include <cmath>
#include <string>
#include <system_error>
#include <utility>

using Folder = AppState::RenderPlayer::Folder;
using Row    = AppState::FolderOverlayRow;

// -- Damage and status --------------------------------------------------------

void GuiRenderPlayer::damage_band() {
    viewport.invalidate_rect(folder_overlay::surface_rect(app));
}

void GuiRenderPlayer::damage_row() {
    viewport.invalidate_modal_dialog_area();
}

void GuiRenderPlayer::status(const std::string& line) {
    app.transient_status_message = line;
    viewport.invalidate_status_chain_area();
}

// -- The folders ----------------------------------------------------------------

std::vector<std::filesystem::path> GuiRenderPlayer::deliverable_wavs() const {
    std::vector<std::filesystem::path> out;
    if (app.source_audio_path.empty()) return out;
    const std::filesystem::path dir =
        render_output_directory(app.source_audio_path);
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return out;
    for (const auto& de : std::filesystem::directory_iterator(dir, ec)) {
        if (!de.is_regular_file()) continue;
        if (de.path().extension() != ".wav") continue;
        out.push_back(de.path());
    }
    // BYTE ORDER of the file names — the listing's one order, and the folder's
    // play order with it.
    std::sort(out.begin(), out.end(),
              [](const std::filesystem::path& a,
                 const std::filesystem::path& b) {
                  return a.filename().string() < b.filename().string();
              });
    return out;
}

bool GuiRenderPlayer::has_playable_render() const {
    if (!deliverable_wavs().empty()) return true;
    return !renders_dir.enumerate_render_entries().empty();
}

std::vector<Row> GuiRenderPlayer::listing_wavs() const {
    std::vector<Row> wavs;
    for (const Row& r : app.folder_overlay.rows) {
        if (r.kind == Row::Kind::Wav) wavs.push_back(r);
    }
    return wavs;
}

void GuiRenderPlayer::rebuild_rows() {
    AppState::FolderOverlay& ov = app.folder_overlay;
    AppState::RenderPlayer&  rp = app.render_player;
    ov.rows.clear();

    auto folder_row = [](const std::string& name,
                         const std::filesystem::path& path) {
        Row r;
        r.kind = Row::Kind::Folder;
        r.name = name;
        r.path = path;
        return r;
    };
    auto up_row = []() {
        Row r;
        r.kind = Row::Kind::Up;
        r.name = "..";
        return r;
    };

    switch (rp.folder) {
        case Folder::Root: {
            // THE ROOT SHOWS `render` IFF IT HOLDS A WAV AND `tmp` IFF A CELL
            // EXISTS — no `..` at the root (R13). Both are asked fresh here:
            // a listing is built when its folder is entered (R4).
            if (!deliverable_wavs().empty()) {
                ov.rows.push_back(folder_row(
                    kDeliverableFolderName,
                    render_output_directory(app.source_audio_path)));
            }
            if (!renders_dir.enumerate_render_entries().empty()) {
                ov.rows.push_back(folder_row(
                    kBatchFolderName,
                    project_batch_root(app.source_audio_path)));
            }
            break;
        }
        case Folder::Deliverable: {
            ov.rows.push_back(up_row());
            for (const std::filesystem::path& p : deliverable_wavs()) {
                Row r;
                r.kind = Row::Kind::Wav;
                r.name = p.filename().string();
                r.path = p;
                // NO ENTRY: the deliverable carries no render-entry sidecars,
                // so it is not load-capable (R15) — the load road's refusal.
                ov.rows.push_back(std::move(r));
            }
            break;
        }
        case Folder::Batches: {
            ov.rows.push_back(up_row());
            // The batch folders in the enumeration's own order (the leading
            // integer), each once.
            std::filesystem::path last;
            for (const AppState::RenderEntry& e :
                 renders_dir.enumerate_render_entries()) {
                if (e.batch_folder == last) continue;
                last = e.batch_folder;
                ov.rows.push_back(folder_row(
                    e.batch_folder.filename().string(), e.batch_folder));
            }
            break;
        }
        case Folder::Batch: {
            ov.rows.push_back(up_row());
            // The cells of this batch in the enumeration's order — the order
            // `'` walks, reused as the play order.
            for (const AppState::RenderEntry& e :
                 renders_dir.enumerate_render_entries()) {
                if (e.batch_folder != rp.batch_dir) continue;
                Row r;
                r.kind  = Row::Kind::Wav;
                r.name  = e.wav_path.filename().string();
                r.path  = e.wav_path;
                r.entry = e;
                ov.rows.push_back(std::move(r));
            }
            break;
        }
    }

    // THE INITIAL HIGHLIGHT of every rebuild: the transport's item's row when
    // it is in this listing, else row 0; -1 only for an empty listing.
    ov.scroll_px     = 0;
    ov.hovered_row   = -1;
    ov.press         = AppState::FolderOverlayPress{};
    ov.highlight_row = ov.rows.empty() ? -1 : 0;
    if (!rp.item.empty()) {
        for (size_t i = 0; i < ov.rows.size(); ++i) {
            if (ov.rows[i].kind == Row::Kind::Wav &&
                ov.rows[i].path == rp.item) {
                ov.highlight_row = static_cast<int>(i);
                break;
            }
        }
    }
    folder_overlay::clamp_scroll(app);
    if (ov.highlight_row >= 0)
        folder_overlay::scroll_row_into_view(app, ov.highlight_row);
    damage_band();
}

void GuiRenderPlayer::enter(Folder folder, const std::filesystem::path& dir) {
    app.render_player.folder    = folder;
    app.render_player.batch_dir = dir;
    rebuild_rows();
}

void GuiRenderPlayer::up() {
    switch (app.render_player.folder) {
        case Folder::Root:        return;   // consumed at the root
        case Folder::Deliverable: enter(Folder::Root, {});    return;
        case Folder::Batches:     enter(Folder::Root, {});    return;
        case Folder::Batch:       enter(Folder::Batches, {}); return;
    }
}

void GuiRenderPlayer::open_row(int index) {
    const AppState::FolderOverlay& ov = app.folder_overlay;
    if (index < 0 || index >= static_cast<int>(ov.rows.size())) return;
    // The row is copied: the act below rebuilds the listing under it.
    const Row row = ov.rows[static_cast<size_t>(index)];
    switch (row.kind) {
        case Row::Kind::Up:
            up();
            return;
        case Row::Kind::Folder:
            switch (app.render_player.folder) {
                case Folder::Root:
                    if (row.name == kDeliverableFolderName)
                        enter(Folder::Deliverable, {});
                    else
                        enter(Folder::Batches, {});
                    return;
                case Folder::Batches:
                    enter(Folder::Batch, row.path);
                    return;
                case Folder::Deliverable:
                case Folder::Batch:
                    return;   // these listings carry no folder rows
            }
            return;
        case Row::Kind::Wav: {
            const std::vector<Row> wavs = listing_wavs();
            int wi = -1;
            for (size_t i = 0; i < wavs.size(); ++i) {
                if (wavs[i].path == row.path) { wi = static_cast<int>(i); break; }
            }
            play_wav(row.path, wavs, wi);
            return;
        }
    }
}

void GuiRenderPlayer::move_highlight(int delta) {
    AppState::FolderOverlay& ov = app.folder_overlay;
    const int n = static_cast<int>(ov.rows.size());
    if (n <= 0) return;
    const int from = ov.highlight_row < 0 ? 0 : ov.highlight_row;
    set_highlight(std::clamp(from + delta, 0, n - 1));
}

void GuiRenderPlayer::set_highlight(int index) {
    AppState::FolderOverlay& ov = app.folder_overlay;
    const int n = static_cast<int>(ov.rows.size());
    const int to = n <= 0 ? -1 : std::clamp(index, 0, n - 1);
    if (to != ov.highlight_row) {
        ov.highlight_row = to;
        damage_band();
    }
    if (to >= 0) {
        const int before = ov.scroll_px;
        folder_overlay::scroll_row_into_view(app, to);
        if (ov.scroll_px != before) damage_band();
    }
}

void GuiRenderPlayer::scroll_rows(int rows) {
    AppState::FolderOverlay& ov = app.folder_overlay;
    const int before = ov.scroll_px;
    ov.scroll_px += rows * (folder_overlay::row_height_px() +
                            folder_overlay::row_gap_px());
    folder_overlay::clamp_scroll(app);
    if (ov.scroll_px != before) damage_band();
}

const AppState::RenderEntry* GuiRenderPlayer::highlighted_entry() const {
    const AppState::FolderOverlay& ov = app.folder_overlay;
    if (ov.highlight_row < 0 ||
        ov.highlight_row >= static_cast<int>(ov.rows.size()))
        return nullptr;
    const Row& r = ov.rows[static_cast<size_t>(ov.highlight_row)];
    if (r.kind != Row::Kind::Wav || !r.entry) return nullptr;
    return &*r.entry;
}

// -- The transport --------------------------------------------------------------

int64_t GuiRenderPlayer::seek_step_frames() const {
    return static_cast<int64_t>(5) * static_cast<int64_t>(audio.sample_rate());
}

int64_t GuiRenderPlayer::position() const {
    return render_player_position(app, playback);
}

int GuiRenderPlayer::scrub_x_of(int64_t frame) const {
    return render_player_scrub_x_of(app, frame);
}

int64_t GuiRenderPlayer::scrub_frame_at(int x) const {
    return render_player_scrub_frame_at(app, x);
}

bool GuiRenderPlayer::play_wav(const std::filesystem::path& path,
                               const std::vector<Row>& folder_wavs,
                               int index) {
    AppState::RenderPlayer& rp = app.render_player;

    // THE DECODE VOCABULARY: probe, the rate/channel equality against the
    // DEVICE (the engine was init()ed with the source's; nothing resamples),
    // the allocation owner on the probed shape, then the read. Every refusal
    // is its own words and the item does not change.
    auto info = wav_probe(path.string());
    if (!info) {
        status(info.error());
        return false;
    }
    if (info->channels != audio.channels() ||
        info->sample_rate != audio.sample_rate()) {
        status("Not the source's rate and channel count");
        return false;
    }
    if (auto n = checked_audio_sample_count(info->frames, info->channels);
        !n) {
        status(n.error());
        return false;
    }
    WavInfo read_info;
    auto samples = wav_read_full(path.string(), &read_info);
    if (!samples) {
        status(samples.error());
        return false;
    }
    const int64_t frames =
        read_info.channels > 0
            ? static_cast<int64_t>(samples->size() /
                                   static_cast<size_t>(read_info.channels))
            : 0;
    if (frames <= 0) {
        status("Empty wav");
        return false;
    }

    // THE FENCE, then the bind. The stop body takes GuiPlayback::stop() —
    // the quiescence proof rebind_buffer requires — and clears the transport
    // bit through the player's fork inside it; the old buffer is released
    // only after that, and the new one is bound before anything reads it.
    playback_lifecycle.stop_playback_if_playing();
    rp.buffer = std::move(*samples);
    rp.frames = frames;
    playback.rebind_buffer(rp.buffer.data(), rp.frames, 0);

    rp.item           = path;
    rp.item_folder    = folder_wavs;
    rp.item_index     = index;
    rp.resume_frame   = 0;
    rp.painted_cursor = -1;
    // THE SECOND LAUNCH BODY (the contract at the head of render_player.h):
    // the item's domain is [0, frames), the scanner never runs, and the
    // project's playhead does not move.
    playback.play(0, rp.frames);
    rp.transport_live = true;
    damage_band();
    damage_row();
    return true;
}

void GuiRenderPlayer::play_button_act() {
    const AppState::FolderOverlay& ov = app.folder_overlay;
    const AppState::RenderPlayer&  rp = app.render_player;
    const int h = ov.highlight_row;
    if (h < 0 || h >= static_cast<int>(ov.rows.size())) {
        toggle_pause();
        return;
    }
    const Row& row = ov.rows[static_cast<size_t>(h)];
    if (row.kind != Row::Kind::Wav) {
        open_row(h);
        return;
    }
    if (row.path != rp.item) {
        open_row(h);
        return;
    }
    toggle_pause();
}

void GuiRenderPlayer::toggle_pause() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.item.empty() || rp.frames <= 0) return;
    if (rp.transport_live) {
        // PAUSE: the resume point is the engine's own position, read BEFORE
        // the stop body (whose fence is the one stop); the body clears the
        // transport bit and damages the row.
        rp.resume_frame = std::clamp<int64_t>(playback.cursor(), 0, rp.frames);
        playback_lifecycle.stop_playback_if_playing();
        return;
    }
    // RESUME: from the resume point; a point at or past the item's end (the
    // rest after a natural end, or a seek to the end while paused) replays
    // from the start — a user act, not a loop.
    int64_t from = rp.resume_frame;
    if (from >= rp.frames - 1) from = 0;
    if (rp.frames < 2) return;
    rp.painted_cursor = -1;
    playback.play(from, rp.frames);
    rp.transport_live = true;
    damage_row();
}

void GuiRenderPlayer::previous() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.item_index <= 0 ||
        rp.item_index >= static_cast<int>(rp.item_folder.size()))
        return;
    const std::vector<Row> folder = rp.item_folder;
    const int i = rp.item_index - 1;
    play_wav(folder[static_cast<size_t>(i)].path, folder, i);
}

void GuiRenderPlayer::next() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.item_index < 0 ||
        rp.item_index + 1 >= static_cast<int>(rp.item_folder.size()))
        return;
    const std::vector<Row> folder = rp.item_folder;
    const int i = rp.item_index + 1;
    play_wav(folder[static_cast<size_t>(i)].path, folder, i);
}

void GuiRenderPlayer::seek_by(int64_t delta_frames) {
    const AppState::RenderPlayer& rp = app.render_player;
    if (rp.item.empty() || rp.frames <= 0) return;
    seek_to(position() + delta_frames);
}

void GuiRenderPlayer::seek_to(int64_t frame) {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.item.empty() || rp.frames <= 0) return;
    const int64_t target = std::clamp<int64_t>(frame, 0, rp.frames);
    if (rp.transport_live) {
        // A LIVE RESEEK is the engine's own keep-alive shape (play() over a
        // live session, the reseek body's precedent): the window stays the
        // item and only the resume point moves. A target inside the last
        // frame would be a one-frame impulse and a play() over an empty range
        // returns without clearing the playing flag, so the end of the item
        // is reached by letting it play out — the seek stops one frame short
        // of the exclusive end, which is where the natural end takes over.
        const int64_t live_target = std::min<int64_t>(target, rp.frames - 2);
        if (live_target < 0) return;
        rp.painted_cursor = -1;
        playback.play(live_target, rp.frames);
        damage_row();
        return;
    }
    if (rp.resume_frame != target) {
        rp.resume_frame = target;
        damage_row();
    }
}

void GuiRenderPlayer::home() {
    seek_to(0);
}

void GuiRenderPlayer::on_natural_end() {
    AppState::RenderPlayer& rp = app.render_player;
    // THE FENCE FIRST, through the one stop body: `playing` is already false
    // (the audio thread published it) but stop() is the quiescence proof the
    // next rebind requires, and the body's player fork clears the transport
    // bit behind it — the tick's own natural-end shape.
    playback_lifecycle.stop_playback_if_playing();
    // AUTO-ADVANCE WITHIN THE ITEM'S FOLDER ONLY (R2), never across folders
    // and never a wrap: the next wav of the list the item was played from, or
    // the rest at the item's start.
    if (rp.item_index >= 0 &&
        rp.item_index + 1 < static_cast<int>(rp.item_folder.size())) {
        const std::vector<Row> folder = rp.item_folder;
        const int i = rp.item_index + 1;
        if (play_wav(folder[static_cast<size_t>(i)].path, folder, i)) return;
    }
    rp.resume_frame = 0;
    damage_row();
}

void GuiRenderPlayer::tick() {
    AppState::RenderPlayer& rp = app.render_player;
    if (!rp.transport_live) return;
    if (!playback.is_playing()) {
        on_natural_end();
        return;
    }
    const int64_t cur = playback.cursor();
    if (cur == rp.painted_cursor) return;
    rp.painted_cursor = cur;
    // The clock cell and the scrub track, the two cells the painter published
    // for exactly this damage; before the row's first paint both are zero and
    // the whole row is the honest widening.
    const GuiRect clock = app.modal_dialog.clock;
    const GuiRect scrub = app.modal_dialog.scrub;
    if (clock.w > 0 && clock.h > 0) viewport.invalidate_rect(clock);
    if (scrub.w > 0 && scrub.h > 0) viewport.invalidate_rect(scrub);
    if ((clock.w <= 0 || clock.h <= 0) && (scrub.w <= 0 || scrub.h <= 0))
        damage_row();
}

// -- Enter and leave -------------------------------------------------------------

bool GuiRenderPlayer::open() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.active) return false;
    if (app.source_audio_path.empty()) return false;
    if (!has_playable_render()) {
        status("No renders to play");
        return false;
    }
    // A modal surface is opening: the shared modal stop
    // (stop_playback_for_modal_open — its declaration owns the decision table
    // and names this opener). Past every refusal above, as the rule demands.
    playback_lifecycle.stop_playback_for_modal_open();

    rp.active         = true;
    rp.session        = text_editor::next_session_id();
    rp.folder         = Folder::Root;
    rp.batch_dir.clear();
    rp.item.clear();
    rp.item_folder.clear();
    rp.item_index     = -1;
    rp.buffer.clear();
    rp.frames         = 0;
    rp.transport_live = false;
    rp.resume_frame   = 0;
    rp.painted_cursor = -1;
    rp.scrub          = AppState::RenderPlayer::ScrubDrag{};
    rp.pending_load.reset();
    app.folder_overlay = AppState::FolderOverlay{};
    rebuild_rows();
    // A modal OPEN damages the whole window: the row's chrome greys, the band
    // appears over the waveform, and the modal row has no rect before its
    // first paint.
    viewport.invalidate_all();
    return true;
}

void GuiRenderPlayer::close() {
    AppState::RenderPlayer& rp = app.render_player;
    if (!rp.active) return;
    // THE ORDER IS LOAD-BEARING: stop (the fence) → the mode down → the
    // VIEW's buffer rebound → only then the item's buffer freed, the engine
    // holding the pointer until the rebind.
    playback_lifecycle.stop_playback_if_playing();
    rp.active         = false;
    rp.transport_live = false;
    rp.scrub          = AppState::RenderPlayer::ScrubDrag{};
    rp.pending_load.reset();
    // THE ENGINE LEAVES THE ITEM'S BUFFER BEFORE THAT BUFFER DIES: bind the
    // source (valid memory for the whole session, offset 0) ahead of the fork
    // below, because the fork's target arm may DISPATCH rather than rebind —
    // a dirty or empty preview goes to trigger(), whose completion rebinds
    // later — and a stopped engine left pointing at the freed item would be
    // one Space away from reading it in the meantime. The fence was taken by
    // the stop body above, so this bind is admitted.
    if (audio.total_frames() > 0) {
        playback.rebind_buffer(audio.samples_ptr(), audio.total_frames(), 0);
    }
    // THE S/T FLIP'S OWN TAIL FORK, verbatim (handle_active_audio_view_toggle,
    // input_handler.cpp): target view rebinds the current preview or
    // dispatches a fresh one, source view rebinds source.wav.
    if (app.active_audio_view == 'T') {
        target_render.ensure_ready();
    } else {
        target_render.rebind_to_source();
    }
    rp.buffer.clear();
    rp.buffer.shrink_to_fit();
    rp.frames = 0;
    rp.item.clear();
    rp.item_folder.clear();
    rp.item_index = -1;
    app.folder_overlay = AppState::FolderOverlay{};
    viewport.invalidate_all();
}
