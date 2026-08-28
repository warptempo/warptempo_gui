#include "render_player.h"

#include "folder_overlay.h"
#include "input_handler.h"          // the ring clear's one owner
                                    // (clear_modal_dialog_key_press)
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

void GuiRenderPlayer::damage_band_full() {
    viewport.invalidate_rect(folder_overlay::band_damage_rect(app));
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
    // A REBUILT LISTING RENAMES EVERY ROW, so no double-click candidate may
    // cross one: the FolderRow seed's target is a row INDEX, and index 0 of
    // the listing this call installs is not the row the seed was taken on.
    // The clear is the MUTATOR'S (the file load's own shape — the candidate
    // is one field, so the whole of it goes), and it is the rule stated at
    // DoubleClickSurface::FolderRow. Every OTHER road into a rebuild happens
    // to pass a chokepoint that clears — on_key's, on_wheel's, or
    // on_button_press's top-of-frame clear — but that is the callers'
    // accident and not a contract the listing may rest on.
    app.double_click = DoubleClickCandidate{};
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
    // THE BAND'S OWN HEIGHT MOVED WITH THE LISTING, so the damage is the band
    // at its ceiling rather than as it now stands (the reason is at the two
    // helpers' declaration): a folder with fewer rows than the last one
    // leaves its predecessor's rows painted above the shorter band.
    damage_band_full();
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

// The three below are the WIDGET'S mechanics (folder_overlay.h owns where the
// band may sit and how far the offset may run, for both contents alike); what
// this cluster adds is the player's own damage.
void GuiRenderPlayer::move_highlight(int delta) {
    if (folder_overlay::move_highlight(app, delta)) damage_band();
}

void GuiRenderPlayer::set_highlight(int index) {
    if (folder_overlay::set_highlight(app, index)) damage_band();
}

void GuiRenderPlayer::scroll_rows(int rows) {
    if (folder_overlay::scroll_rows(app, rows)) damage_band();
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
    // the allocation owner on the probed shape, then the read — and THE SAME
    // EQUALITY AGAIN ON THE DECODED BUFFER'S OWN SHAPE, which is the one that
    // binds. Every refusal is its own words and the item does not change.
    //
    // THE PROBE'S ANSWER IS STALE BY CONSTRUCTION: wav_read_full REOPENS the
    // path, so the file object it decoded need not be the one probed here — a
    // wav republished between the two opens (every writer in this tree
    // publishes by rename: the render's own staging, the Synchronize act,
    // wts) is a different file at the same name. The probe is kept as the
    // CHEAP EARLY REFUSAL — it is what lets the allocation policy
    // (checked_audio_sample_count) answer on a header before any payload is
    // read — and the post-decode check below is what the bind rests on,
    // because a mono or other-rate buffer under the engine's stride would be
    // read past its end (the engine reads by ITS channel count, playback
    // engine's mixer) or played at the wrong speed.
    //
    // IT IS A SHAPE CHECK, NOT AN IDENTITY ONE, unlike the render cache's
    // post-decode re-stat (RenderCache::read_file): that road holds a
    // fingerprint's own stat capture and must prove the bytes are THAT
    // artifact's; this one holds no identity — any wav at the path is a
    // legitimate item — and needs only that what it binds fits the device.
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
    // THE DECODED BUFFER'S OWN SHAPE, in the probe's words: read_info is the
    // layout the returned samples were decoded under (wav_read_range fills it
    // from the very open it read), so this is the buffer describing itself.
    // Its LENGTH needs no arm of its own — that read allocates
    // frames * channels through the same allocation owner and returns exactly
    // it, so the buffer is a whole number of frames at this channel count by
    // construction, which is what the frame count below divides by.
    if (read_info.channels != audio.channels() ||
        read_info.sample_rate != audio.sample_rate()) {
        status("Not the source's rate and channel count");
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
    // The head unit: a new item, playing (the inventory at the declaration).
    publish_media_state();
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
    // The head unit: playing again (the inventory at the declaration).
    publish_media_state();
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
        // The head unit's clock, re-anchored on the seek (the inventory at
        // the declaration).
        publish_media_state();
        return;
    }
    if (rp.resume_frame != target) {
        rp.resume_frame = target;
        damage_row();
        publish_media_state();
    }
}

void GuiRenderPlayer::home() {
    seek_to(0);
}

void GuiRenderPlayer::on_natural_end() {
    AppState::RenderPlayer& rp = app.render_player;
    // THE REST IS AT THE ITEM'S START, written BEFORE the stop body so the
    // "paused" the body's fork publishes reads position 0 rather than a
    // resume point left over from an earlier pause (the resume point is dead
    // state while the transport is live, so nothing else reads this write).
    rp.resume_frame = 0;
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
    damage_row();
}

void GuiRenderPlayer::tick() {
    AppState::RenderPlayer& rp = app.render_player;
    if (!rp.transport_live) return;
    if (playback.device_unavailable()) {
        // NO DEVICE PAUSES, IT DOES NOT ADVANCE (the rule at on_natural_end's
        // declaration): an engine that cannot sound leaves `playing` false
        // exactly as a finished window does, so this arm stands ABOVE the
        // natural-end test — and it covers BOTH shapes, the device that went
        // away mid-play (the AAudio latch) and the device that never came up
        // (an init that failed, the laptop without pipewire-jack), which
        // otherwise raced through the folder a wav per tick. It is the pause
        // arm of toggle_pause with one line added — the resume point is the
        // engine's held cursor (a suspended device holds it rather than
        // extrapolating), read before the stop body, whose fence returns at
        // once on a dead or absent device and whose player fork clears the
        // transport bit and publishes the head unit's "paused". ONE LINE FOR
        // BOTH SHAPES: what the user needs to know is that nothing will
        // sound, not which way it will not. Nothing here retries; on Android
        // the next Space reopens the device by the backend's own rule.
        rp.resume_frame = std::clamp<int64_t>(playback.cursor(), 0, rp.frames);
        playback_lifecycle.stop_playback_if_playing();
        status("No audio device");
        return;
    }
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
    // THE BAND OPENS WITH THE PLAYER AS ITS OWNER — the tag is the panel's
    // standing predicate (folder_overlay_stands), so it is what raises the
    // overlay, and every other field of the panel is reset with it.
    app.folder_overlay       = AppState::FolderOverlay{};
    app.folder_overlay.owner = AppState::FolderOverlay::Owner::Player;
    rebuild_rows();
    // A modal OPEN damages the whole window: the row's chrome greys, the band
    // appears over the waveform, and the modal row has no rect before its
    // first paint.
    viewport.invalidate_all();
    // The head unit: the session goes ACTIVE with the mode (R7) — no item,
    // stopped (the inventory at the declaration).
    publish_media_state();
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
    // The panel comes down with the mode: the reset restores Owner::None,
    // which IS the band's standing predicate answering false.
    app.folder_overlay = AppState::FolderOverlay{};
    viewport.invalidate_all();
    // The head unit: the session goes INACTIVE with the mode, and the audio
    // focus is abandoned on the consuming side (the inventory at the
    // declaration).
    publish_media_state();
}

// -- The car ---------------------------------------------------------------------

void GuiRenderPlayer::on_media_command(GuiMediaCommand cmd) {
    const AppState::RenderPlayer& rp = app.render_player;
    if (!rp.active) return;       // the session is inactive; belt and braces
    if (app.prompt.active) return; // a question on the screen is answered there

    // ONE KEY, PRESS AND RELEASE, through the seam's synthesis road (the
    // contract at the declaration). The stable code is the key's own value
    // off the car base, the codepoint 0: none of these keys produces a
    // character.
    //
    // THE RING CLEAR RIDES THIS LAMBDA, which is what gives it the membership
    // the rule asks for — every kind that synthesizes a key, and no other: a
    // car button is not a keyboard walking the modal row's ring, and a bare
    // Space or Enter on a ring-focused button is that button's press. The
    // three writes are dispatch_modal_dialog_editor_act's, the same focus
    // move made for the same reason (the full rule is at the declaration).
    const auto press = [&](GuiKey key) {
        if (app.modal_dialog_focus >= 0) {
            if (input != nullptr) input->clear_modal_dialog_key_press();
            app.modal_dialog_focus        = -1;
            app.modal_dialog_focus_active = false;
            if (app.modal_dialog.valid)
                viewport.invalidate_rect(app.modal_dialog.box);
        }
        const uint32_t code = kCarStableCodeBase + key;
        gui.synthesize_key(key, code, /*pressed=*/true,  /*codepoint=*/0);
        gui.synthesize_key(key, code, /*pressed=*/false, /*codepoint=*/0);
    };

    using Kind = GuiMediaCommand::Kind;
    switch (cmd.kind) {
        case Kind::PlayPause:
            // THE UNDIVIDED TOGGLE KEY TAKES NO STATE GATE: it says "the
            // other one", and Space in the player is exactly that act
            // (play_button_act's own fork). The gate below exists only
            // because Play and Pause name a direction.
            press(GuiKeys::Space);
            return;
        case Kind::Play:
            // The state gate (the declaration): a "play" said to a live
            // transport is already true and must not toggle it off.
            if (!rp.transport_live) press(GuiKeys::Space);
            return;
        case Kind::Pause:
        case Kind::Stop:
        case Kind::FocusLost:
        case Kind::FocusLostTransient:
            // Stop is a pause by ruling; a focus loss pauses. A "pause" said
            // to a resting transport must not start it.
            if (rp.transport_live) press(GuiKeys::Space);
            return;
        case Kind::Next:
            press(GuiKeys::PageDown);
            return;
        case Kind::Previous:
            press(GuiKeys::PageUp);
            return;
        case Kind::FastForward:
            press(GuiKeys::Right);
            return;
        case Kind::Rewind:
            press(GuiKeys::Left);
            return;
        case Kind::SeekTo: {
            // THE ROAD'S ONE DIRECT ACT (the declaration): no keysym carries
            // an absolute position. Milliseconds to frames at the device's
            // rate; seek_to clamps into the item and refuses with no item.
            //
            // THE CLAMP IS ON THE MILLISECONDS, BEFORE THE CONVERSION, and
            // that ordering is the rule: the position comes from a head unit
            // over binder and is any int64 at all, so `ms * rate` would
            // overflow — signed overflow being undefined — long before
            // seek_to's own clamp on the frame could see it. A negative or
            // absurd target lands at 0 or at the item's end instead.
            const int64_t rate = audio.sample_rate();
            if (rate <= 0) return;
            const int64_t duration_ms =
                rp.frames > 0 ? rp.frames * 1000 / rate : 0;
            const int64_t ms =
                std::clamp<int64_t>(cmd.position_ms, 0, duration_ms);
            seek_to(ms * rate / 1000);
            return;
        }
        case Kind::FocusGained:
            // NOTHING RECOVERS BY ITSELF: no auto-resume.
            return;
    }
}

void GuiRenderPlayer::publish_media_state() {
    const AppState::RenderPlayer& rp = app.render_player;
    GuiMediaState st;
    st.session_active = rp.active;
    st.playing        = rp.active && rp.transport_live;
    st.artist         = app.project_name;
    if (rp.active && !rp.item.empty() && rp.frames > 0) {
        // THE ITEM'S SPELLING WITH ITS FOLDER, relative to the project
        // folder (the source's own parent): `tmp/<batch>/NN.wav` for a cell,
        // `render/<title>.wav` for the deliverable — lexically, no
        // filesystem call, and in generic form so the separator is `/` by
        // construction.
        st.title = rp.item
                       .lexically_relative(
                           std::filesystem::path(app.source_audio_path)
                               .parent_path())
                       .generic_string();
        const int64_t rate = audio.sample_rate();
        if (rate > 0) {
            st.duration_ms = rp.frames * 1000 / rate;
            st.position_ms = render_player_position(app, playback) * 1000 / rate;
        }
    }
    gui.publish_media_state(st);
}
