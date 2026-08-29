#include "render_player.h"

#include "folder_overlay.h"
#include "input_handler.h"          // the ring clear's one owner
                                    // (clear_modal_dialog_key_press)
#include "render_output_naming.h"   // the deliverable's directory and stem
#include "text_editor.h"            // next_session_id (the one modal counter)
#include "wav_io.h"                 // wav_probe, checked_audio_sample_count,
                                    // wav_read_full — called, never changed

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

using Folder    = AppState::RenderPlayer::Folder;
using Row       = AppState::FolderOverlayRow;
using Transport = AppState::RenderPlayer::Transport;

// -- Damage and status --------------------------------------------------------

void GuiRenderPlayer::damage_band() {
    viewport.invalidate_rect(folder_overlay::surface_rect(app));
}

void GuiRenderPlayer::damage_row() {
    viewport.invalidate_modal_dialog_area();
}

void GuiRenderPlayer::status(const std::string& line) {
    notifications.notify(AppState::NotificationClass::Normal, line);
}

// -- The folders ----------------------------------------------------------------

std::optional<std::filesystem::path>
GuiRenderPlayer::deliverable_wav() const {
    if (app.source_audio_path.empty()) return std::nullopt;
    // THE PRUNE IS THE LISTING'S FIRST ACT (architect 2026-08-29): `render/`
    // holds the current title's deliverable and nothing else, so the folder is
    // brought to that definition before it is read rather than filtered on the
    // way out. Its whole contract — the two callers, the CLI asymmetry, the
    // running render, the refusals — is at prune_render_folder (renders_dir.h).
    prune_render_folder(app.source_audio_path, app.engine_settings);
    // The one path, composed by the parser's owners exactly as a render
    // composes it — never a directory scan, because there is nothing to
    // search for.
    const std::filesystem::path p = compose_render_output_path(
        render_output_directory(app.source_audio_path),
        render_output_stem(app.engine_settings));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec) || ec) return std::nullopt;
    return p;
}

bool GuiRenderPlayer::has_playable_render() const {
    if (deliverable_wav()) return true;
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
            // THE ROOT SHOWS `render` IFF THE CURRENT TITLE'S WAV IS THERE AND
            // `tmp` IFF A CELL EXISTS — no `..` at the root (R13). Both are
            // asked fresh here: a listing is built when its folder is entered
            // (R4), and the deliverable question prunes the folder as it asks.
            if (deliverable_wav()) {
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
            // ONE ROW AT MOST, after the `..`: the folder is the current
            // title's deliverable alone. Play order, Previous / Next and the
            // two folder ends read this list like any other and are simply
            // degenerate over it — no arm of their own.
            if (const std::optional<std::filesystem::path> p =
                    deliverable_wav()) {
                Row r;
                r.kind = Row::Kind::Wav;
                r.name = p->filename().string();
                r.path = *p;
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
    // (A REBUILT LISTING RENAMES EVERY ROW, which is why this body also
    // cleared the double-click candidate while the rows had one: its target
    // was a row INDEX into the listing being replaced. The clear went with
    // DoubleClickSurface::FolderRow on 2026-08-29, when a click became the
    // open act and the second press lost its meaning.)
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
    // THE BAND IS ONE RECT (R35): its height is the slot's ceiling whatever
    // this listing's length, so the rebuild's damage is the same band every
    // other damage takes.
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
    // AN OPEN ENDS THE FOLDER-END REST (R27's bit): the user has named where
    // to go — a wav to play, a folder to walk into — and the next Play must
    // answer THAT rather than restarting the folder the transport finished.
    // Set here, above the fork, so all three row kinds are one rule (the wav
    // arm's own play clears it again a call later, harmlessly).
    app.render_player.ended_at_folder_end = false;
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
// band may sit and how far the offset may run, for every content alike); what
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

    // THE SEAT BELOW IS GATED ON THIS, read before the item is overwritten
    // (the fence above touches no field of the item): the band follows the
    // transport at an item CHANGE, and a play on the item already resting
    // here is not one.
    const bool item_changed = rp.item != path;

    rp.item           = path;
    rp.item_folder    = folder_wavs;
    rp.item_index     = index;
    rp.resume_frame   = 0;
    rp.painted_cursor = -1;
    // THE FOLDER IS NOT AT ITS END ANY MORE — a wav is playing (R27's bit,
    // whose whole membership is "every play, resume, seek, row open, open and
    // close"; this is the play).
    rp.ended_at_folder_end = false;
    // THE SECOND LAUNCH BODY (the contract at the head of render_player.h):
    // the item's domain is [0, frames), the scanner never runs, and the
    // project's playhead does not move.
    playback.play(0, rp.frames);
    rp.transport = Transport::Live;
    // THE BAND FOLLOWS THE ITEM AT A CHANGE (R38, the contract at the head of
    // render_player.h): this is the ONE place the item changes, so seating the
    // highlight here covers every change the transport makes on its own —
    // Previous, Next, the folder's ends, the auto-advance and the folder-end
    // restart — with no membership list to keep. THE GATE IS WHAT KEEPS THE
    // OTHER HALF OF THE RULE: a user's own highlight moves are untouched, and
    // the road that would otherwise fight the band back onto the item is the
    // REPEAT ONE REPLAY, which re-enters this body on the item already
    // resting here at every natural end — the band a user has walked
    // elsewhere under a lit lamp must stay where he put it. IT MOVES ONLY
    // WHERE THE ITEM IS LISTED: a user who has walked into another folder
    // keeps his place, and a user's own play finds the band already on the
    // row. The listing is walked rather than asked for a row index because
    // item_index names the ITEM FOLDER's wav list, which is not this listing
    // when the two differ.
    if (item_changed) {
        for (size_t i = 0; i < app.folder_overlay.rows.size(); ++i) {
            const Row& r = app.folder_overlay.rows[i];
            if (r.kind == Row::Kind::Wav && r.path == rp.item) {
                folder_overlay::set_highlight(app, static_cast<int>(i));
                break;
            }
        }
    }
    damage_band();
    damage_row();
    // The head unit: a new item, playing (the inventory at the declaration).
    publish_media_state();
    return true;
}

void GuiRenderPlayer::play_button_act() {
    const AppState::RenderPlayer& rp = app.render_player;
    // THE HIGHLIGHT IS NOT READ HERE AT ALL (architect 2026-08-29; the table
    // is at the declaration): PLAY WHEN IDLE PLAYS THE CURRENT TRACK, which is
    // Audacious's own answer, and the row acts belong to the rows now — a
    // click on a folder opens it, a click on a wav plays it. THE STATE IS THE
    // STORED FIELD (AppState::RenderPlayer::transport), so a transport parked
    // at frame 0 — the no-device pause on a wav that never sounded — answers
    // PAUSED here and resumes ITS item.
    switch (rp.transport) {
        case Transport::Live:
        case Transport::Paused:
            toggle_pause();
            return;
        case Transport::Idle:
            break;
    }
    // IDLE. THE FOLDER-END ARM IS R27 (architect 2026-08-28): PLAY AFTER THE
    // FOLDER FINISHED STARTS THE FOLDER'S FIRST FILE, the car's Play at the
    // end of a playlist. The bit is set only by the natural end at the item
    // folder's last wav with the lamp off, so reaching here with it standing
    // means the transport is resting exactly there; the car's own Play arrives
    // as the Space that runs this act, and the restart moves the band onto the
    // first file like every other item change the transport makes for itself
    // (R38).
    if (rp.ended_at_folder_end && !rp.item_folder.empty()) {
        const std::vector<Row> folder = rp.item_folder;
        play_wav(folder[0].path, folder, 0);
        return;
    }
    // ELSE THE ITEM FROM ITS START, ALWAYS (architect 2026-08-29 ~01:40:
    // "Play when idle should start literally"): `resume_frame` is 0 at every
    // idle rest by construction — seek_to's own head refuses an idle seek, so
    // nothing can have moved it since the last Stop, natural end or fresh
    // bind — and toggle_pause's resume arm reads exactly that field, a
    // consumed no-op with no item.
    toggle_pause();
}

void GuiRenderPlayer::toggle_repeat_one() {
    AppState::RenderPlayer& rp = app.render_player;
    rp.repeat_one = !rp.repeat_one;
    // The lamp lives on the modal row; nothing else on the screen says this.
    damage_row();
}

void GuiRenderPlayer::toggle_pause() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.item.empty() || rp.frames <= 0) return;
    if (rp.transport == Transport::Live) {
        // PAUSE: the resume point is the engine's own position, read BEFORE
        // the stop body (whose fence is the one stop); the body's player fork
        // moves the transport to PAUSED — at whatever frame the cursor holds,
        // the item's start included — and damages the row.
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
    // A RESUME IS A PLAY: the folder-end rest is over (R27's bit; its
    // membership is at the field). Nothing here reaches this arm at the
    // folder's end today — play_button_act takes R27's own road first — but
    // the bit says "the transport is resting at the folder's end" and this
    // line is what keeps that true rather than a claim about callers.
    rp.ended_at_folder_end = false;
    playback.play(from, rp.frames);
    rp.transport = Transport::Live;
    damage_row();
    // The head unit: playing again (the inventory at the declaration).
    publish_media_state();
}

void GuiRenderPlayer::stop() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.item.empty() || rp.frames <= 0) return;
    // ALREADY RESTING AT THE START IS A CONSUMED NO-OP (R36): there is nothing
    // for a stop to do to an item already where a stop leaves it, and the test
    // is the three things this body writes. THE STATE IS PART OF IT: a
    // transport PAUSED at frame 0 (the no-device arm's own rest) is not where
    // a stop leaves one, its next Play resuming from a state a Play would not
    // resume from — and the folder-end rest is not that state either, its bit
    // being what the next Play reads.
    if (rp.transport == Transport::Idle && rp.resume_frame == 0 &&
        !rp.ended_at_folder_end)
        return;
    // THE REST IS AT THE ITEM'S START, written BEFORE the stop body so the
    // "paused" that body's player fork publishes reads position 0 — the
    // natural end's own ordering, and for the same reason.
    rp.resume_frame        = 0;
    rp.painted_cursor      = -1;
    // A STOP ENDS THE FOLDER-END REST TOO: the user has named this item's
    // start as where the transport is, so the next Play replays THIS item
    // rather than starting the folder over (R27's bit, whose membership is
    // every play, resume, seek, row open, open, close — and now this stop).
    rp.ended_at_folder_end = false;
    if (rp.transport == Transport::Live) {
        // THE FENCE, the row's damage and the head unit's push are the one
        // stop body's player fork, which leaves the transport PAUSED; the
        // state a STOP means is written over it here. No second push: the
        // fork's own already carried "not playing" at the rest this body
        // wrote above, and the wire does not distinguish idle from paused.
        playback_lifecycle.stop_playback_if_playing();
        rp.transport = Transport::Idle;
        return;
    }
    // A TRANSPORT THAT IS NOT SOUNDING has already passed that fork — paused,
    // or idle with a seeked rest or the folder-end bit standing — so what is
    // left is the state and the rest moving to 0, which the clock, the scrub
    // and the head unit read.
    rp.transport = Transport::Idle;
    damage_row();
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

// THE ITEM FOLDER'S ENDS (R37) — the neighbours' own road with the index
// named outright instead of stepped. THE END ITSELF REFUSES, as Previous and
// Next refuse at the ends: an item that is already the folder's first is
// already where "go to the first" would put it, and bare Home is what restarts
// a wav in place.
void GuiRenderPlayer::first_in_item_folder() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.item_index <= 0 ||
        rp.item_index >= static_cast<int>(rp.item_folder.size()))
        return;
    const std::vector<Row> folder = rp.item_folder;
    play_wav(folder.front().path, folder, 0);
}

void GuiRenderPlayer::last_in_item_folder() {
    AppState::RenderPlayer& rp = app.render_player;
    const int last = static_cast<int>(rp.item_folder.size()) - 1;
    if (last < 0 || rp.item_index < 0 || rp.item_index >= last) return;
    const std::vector<Row> folder = rp.item_folder;
    play_wav(folder[static_cast<size_t>(last)].path, folder, last);
}

void GuiRenderPlayer::seek_by(int64_t delta_frames) {
    const AppState::RenderPlayer& rp = app.render_player;
    if (rp.item.empty() || rp.frames <= 0) return;
    seek_to(position() + delta_frames);
}

void GuiRenderPlayer::seek_to(int64_t frame) {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.item.empty() || rp.frames <= 0) return;
    // A SEEK WHILE IDLE IS A CONSUMED NO-OP (architect 2026-08-29 ~01:40,
    // Audacious's own stopped slider): Play when idle starts the item from
    // its start, so an idle transport must not be nudgeable to some other
    // rest first. THE ONE OWNER — every road that can move the point (Left /
    // Right, Home, the car's absolute SeekTo; the scrub's press already
    // refuses to arm at its own gate for the same reason) shares this
    // refusal, neither moving `resume_frame` nor damaging anything. LIVE and
    // PAUSED are unchanged below, and this is why `resume_frame` is always 0
    // at an idle rest, by construction (the field's own comment,
    // app_state.h).
    if (rp.transport == Transport::Idle) return;
    // A SEEK MOVES THE REST, so the folder-end rest is over (R27's bit): the
    // user has named a place in this item, and the next Play resumes there
    // rather than starting the folder over.
    rp.ended_at_folder_end = false;
    const int64_t target = std::clamp<int64_t>(frame, 0, rp.frames);
    if (rp.transport == Transport::Live) {
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
    // PAUSED (the only state left, Idle having returned above): a seek moves
    // the point, not the state — the transport stays paused at its new
    // point, where the next Play resumes it.
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
    // next rebind requires, and the body's player fork takes the transport
    // down behind it — the tick's own natural-end shape.
    playback_lifecycle.stop_playback_if_playing();
    // THE REST IS IDLE, NOT PAUSED: the item rests AT ITS START, and there is
    // nothing there to resume that a Play from the start does not do
    // identically — so the next Play replays this item, or, at the folder's
    // end, starts the folder over (R27's bit). Written over the PAUSED the
    // stop body's fork leaves on
    // every live transport, and ahead of the two arms below, which play again
    // and take LIVE with them.
    rp.transport = Transport::Idle;
    // REPEAT ONE, THE ONE SANCTIONED EXCEPTION TO NOTHING LOOPS (architect
    // 2026-08-28, R26): the lamp replays THIS item from its start through the
    // player's own play road — the same road a user's Play takes, so the item
    // stays the transport's, the head unit is published at the edge as any
    // play publishes, and there is no second launch here. It outranks the
    // advance: repeat ONE means this wav and not the folder.
    // THE ARM RETURNS WHETHER THE REPLAY SOUNDED OR REFUSED, and that is what
    // "outranks" means: a replay that cannot decode (the item deleted or
    // republished in another shape while it played) has already put its own
    // words on a notification card and left the item untouched, and the rest
    // written above leaves the transport on that item at its start. Falling
    // through to the arms below would answer a refused REPEAT with the folder's
    // next wav — or, on the last wav, with ended_at_folder_end standing under a
    // lit lamp, so the next Play started the folder over. A lit lamp means this
    // wav and nothing else, failure included.
    if (rp.repeat_one && !rp.item.empty() && rp.item_index >= 0 &&
        rp.item_index < static_cast<int>(rp.item_folder.size())) {
        const std::vector<Row> folder = rp.item_folder;
        const int i = rp.item_index;
        const bool replayed =
            play_wav(folder[static_cast<size_t>(i)].path, folder, i);
        if (!replayed) damage_row();
        return;
    }
    // AUTO-ADVANCE WITHIN THE ITEM'S FOLDER ONLY (R2), never across folders
    // and never a wrap: the next wav of the list the item was played from, or
    // the rest at the item's start.
    const bool has_next =
        rp.item_index >= 0 &&
        rp.item_index + 1 < static_cast<int>(rp.item_folder.size());
    if (has_next) {
        const std::vector<Row> folder = rp.item_folder;
        const int i = rp.item_index + 1;
        if (play_wav(folder[static_cast<size_t>(i)].path, folder, i)) return;
    }
    // THE FOLDER IS AT ITS END and the transport rests at the item's start.
    // THE BIT IS THIS ARM'S ONE WRITER (R27): the next Play starts the item
    // folder's FIRST wav instead of replaying this last one. IT NAMES THE END
    // AND NOT A FAILURE — a next wav that REFUSED to decode (its own status
    // line, the item unchanged) leaves the transport resting mid-folder, so
    // that arm does not set it and the next Play means what it always meant
    // there.
    if (!has_next) rp.ended_at_folder_end = true;
    damage_row();
}

void GuiRenderPlayer::tick() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.transport != Transport::Live) return;
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
        // once on a dead or absent device and whose player fork moves the
        // transport to PAUSED and publishes the head unit's "paused" —
        // PAUSED AT THE HELD CURSOR EVEN WHERE THAT IS FRAME 0, which is the
        // whole reason the state is stored: the next Play resumes this item at
        // that cursor rather than restarting it. ONE LINE FOR BOTH SHAPES:
        // what the user needs to know is that nothing will sound, not which
        // way it will not. Nothing here retries; on Android
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
    rp.transport      = Transport::Idle;
    // REPEAT ONE IS SESSION-ONLY AND OFF AT EVERY OPEN (R26), like the mode's
    // every other bit; the folder-end rest cannot outlive an open either.
    rp.repeat_one     = false;
    rp.ended_at_folder_end = false;
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
    rp.transport      = Transport::Idle;
    rp.ended_at_folder_end = false;
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
    // THE S/T FLIP'S OWN TAIL FORK, verbatim (switch_active_audio_view_to,
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
            // transport is already true and must not toggle it off. AT THE
            // FOLDER'S END the transport is down, so this Play passes the gate
            // and reaches play_button_act's transport arm — which is where
            // R27 starts the folder's first file, the car's own case.
            if (rp.transport != Transport::Live) press(GuiKeys::Space);
            return;
        case Kind::Pause:
        case Kind::FocusLost:
        case Kind::FocusLostTransient:
            // A focus loss pauses (Android's one imposed interrupt). A
            // "pause" said to a resting transport must not start it.
            if (rp.transport == Transport::Live) press(GuiKeys::Space);
            return;
        case Kind::Stop:
            // THE HEAD UNIT'S STOP IS THE PLAYER'S STOP since R36, where it
            // was a pause before the row had a stop of its own. IT TAKES NO
            // STATE GATE — the gate above exists only because Space is a
            // TOGGLE and Play and Pause name a direction, while this key names
            // an act that says the same thing whatever the transport is doing;
            // a stop said to a resting transport is the act's own consumed
            // no-op.
            press(GuiKeys::S);
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
    // NO REPEAT MODE IS PUBLISHED and no media command maps to one: the lamp
    // (R26) is the app's own state, the head unit shows what is playing and
    // its buttons are the transport's, and a repeat mode nothing can set from
    // the wheel is not worth a field on the wire.
    GuiMediaState st;
    st.session_active = rp.active;
    st.playing        = rp.active && rp.transport == Transport::Live;
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
