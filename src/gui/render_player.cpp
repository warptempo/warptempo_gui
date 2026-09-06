#include "render_player.h"

#include "failure.h"                // GuiFailure (the decode refusal's shape)
#include "folder_overlay.h"
#include "input_handler.h"          // the ring clear's one owner
                                    // (clear_modal_dialog_key_press)
#include "text_editor.h"            // next_session_id (the one modal counter)
#include "wav_io.h"                 // wav_probe, checked_audio_sample_count,
                                    // wav_read_full — called, never changed

#include <algorithm>
#include <cmath>
#include <cstdio>
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

// THE DECODE'S REFUSAL, TWO CLAUSES (GuiFailure, failure.h — 2026-09-02, the
// four-tier review's R-11): the card keeps saying the wav reader's own words
// alone, as it always did — the subject is the highlighted row on screen, so
// the sentence needs no name (the basename rule's own reasoning,
// messaging.md) — and the stderr line, new with the shape, names the FULL
// path beside those words, which is what a terminal is for. Composed here
// from the path and the words, never by parsing either.
void GuiRenderPlayer::refuse_decode(const std::filesystem::path& path,
                                    const std::string&           words) {
    GuiFailure f;
    f.diagnostic = "Cannot play '" + path.string() + "': " + words;
    f.display    = words;
    std::fprintf(stderr, "warptempo_gui: %s\n", f.diagnostic.c_str());
    status(f.display);
}

// -- The folders ----------------------------------------------------------------
//
// THE PLAYER LIVES INSIDE `tmp/` (architect 2026-09-01): it never lists
// `render/` and never rises above `tmp/`, so every question below is asked of
// the batch enumeration alone. HIS RATIONALE, recorded here because this is
// where the listing is built: the deliverable in `render/` is a
// NAMING-FOR-SHARING CONVENIENCE OUTSIDE THE GUI'S WORKFLOW — the tablet's
// engine differs from the laptop's by ULPs, so the deliverable is never driven
// from the glass, and it carries no sidecars, so it cannot be loaded in place
// (the load road's own refusal, R15) — while `tmp/`'s batch cells are the
// player's whole subject on both hosts.
//
// WHAT WENT WITH THE RULING: GuiRenderPlayer::deliverable_wav (the one-file
// folder's question, which PRUNED before it answered), the `Folder::Root` that
// listed `render` and `tmp` as two rows, the `Folder::Deliverable` listing
// under it, and the `..` row every non-root listing carried. THE PRUNE ITSELF
// STAYS AND HAS ONE TRIGGER NOW, the deliverable's publish (the succession is
// at prune_render_folder, renders_dir.h). TWO THINGS ARE DELIBERATELY
// UNTOUCHED: the deliverable's PUBLISH ROAD — the archival render still writes
// `render/<title>.wav` and prunes the folder at its completion — and THE
// SYNCHRONIZE MIRROR, which still ships `render/`'s CONTENTS beside every
// `tmp/` batch folder — every regular `.wav` it holds, listed rather than
// composed from the title since 2026-09-02 (external_sync.h). Only the PLAYER
// stops looking at `render/`.

bool GuiRenderPlayer::has_playable_render() const {
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
    switch (rp.folder) {
        case Folder::Root: {
            // THE ROOT IS `tmp/` (architect 2026-09-01, the ruling above): its
            // batch folders in the enumeration's own order (the leading
            // integer), each once, and NO `..` — there is nothing above it.
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
            // The cells of this batch in the enumeration's order — the order
            // `'` walks, reused as the play order. The way OUT is the modal
            // row's Up button (and Backspace), not a row.
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
    // ROW 0 IS THE FIRST REAL ROW since 2026-09-01, and that DISSOLVES R6'S
    // ONE SURFACED EDGE structurally: while every non-root listing opened with
    // a `..` row, entering a folder that does not hold the playing item seated
    // the band on it, so the highlight-driven Space (R6) went UP a folder
    // instead of playing something — the one case where a highlight the user
    // had not consciously placed drove the key. With the `..` row gone the
    // seat is a batch folder or a wav, and Space at every entry acts on
    // content.
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
    // other damage takes. THE ROW REPAINTS WITH IT since 2026-08-30: the
    // rebuild reseats the highlight, which the Load in place button's face
    // reads (render_player_button_enabled).
    damage_band();
    damage_row();
}

void GuiRenderPlayer::enter(Folder folder, const std::filesystem::path& dir) {
    app.render_player.folder    = folder;
    app.render_player.batch_dir = dir;
    rebuild_rows();
}

// THE UP WALL'S ONE OWNER (the contract is at the declaration, app_state.h):
// the root is `tmp/` and there is nothing above it.
bool render_player_up_actionable(const AppState& a) {
    return a.render_player.folder != AppState::RenderPlayer::Folder::Root;
}

void GuiRenderPlayer::up() {
    // THE ROOT IS SILENT (architect 2026-08-31, retiring the 2026-08-30 card
    // "This is the top of the render folders"): a benign one-dimensional
    // refusal already at its state says nothing — and since 2026-09-01 THE UP
    // BUTTON'S GREY IS the glance that says it, the missing `..` row having
    // said it before. The wall is asked through the predicate above, which the
    // button's face reads too, so the key and the button cannot disagree.
    if (!render_player_up_actionable(app)) return;
    // UP UNLOADS THE ITEM (architect 2026-09-04, in his words: "The Up button
    // should simply stop audio and drop the selection, not pause it. It should
    // make it like when you first open the player: nothing is loaded"). WHY AN
    // UNLOAD RATHER THAN A PAUSE: the row carries no stop button and none is
    // wanted, and the car's dashboard has no stop either — so Up, the one act
    // that LEAVES the item's folder, is where stopping lives, and leaving the
    // folder means nothing is loaded. THE PAUSE FORM LIVED FOR ONE EVENING
    // (a7b132ec, hours earlier) and was superseded the same day: it wrote the
    // resume point, took the stop body and kept the item, so walking back in
    // resumed it. What that form was built for survives unchanged here,
    // because an unloaded transport is IDLE and the root's listing is folder
    // rows — the play/pause face reads the highlight ahead of the transport
    // (render_player_highlight_act_row), so it says "Open Folder" over a
    // transport that is doing exactly nothing, and no face can lie about the
    // sound.
    //
    // The ordering the engine's pointer demands is the shared body's (the
    // contract at unload_item's declaration), and the Up tail is what keeps
    // the mode bit standing through it — the player is not going anywhere.
    // What is UP'S OWN is the root entry — the listing rebuilt with no item,
    // so the band seats on row 0 as at an open — and the head unit's push,
    // which must be THE LAST WORD: the stop body's fork inside the unload has
    // already published a paused state carrying the very item this act is
    // dropping. REPEAT ONE IS UNTOUCHED: the lamp is session state, off at
    // every open() and at no other time, and going up a folder is not an open.
    unload_item(UnloadTail::Up);
    enter(Folder::Root, {});
    publish_media_state();
}

void GuiRenderPlayer::open_row(int index) {
    const AppState::FolderOverlay& ov = app.folder_overlay;
    if (index < 0 || index >= static_cast<int>(ov.rows.size())) return;
    // The row is copied: the act below rebuilds the listing under it.
    const Row row = ov.rows[static_cast<size_t>(index)];
    switch (row.kind) {
        case Row::Kind::Folder:
            switch (app.render_player.folder) {
                case Folder::Root:
                    enter(Folder::Batch, row.path);
                    return;
                case Folder::Batch:
                    return;   // a batch listing carries no folder rows
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
        case Row::Kind::Text:
            // THE PLAYER PRODUCES NO TEXT ROW (rebuild_rows builds Folder and
            // Wav rows and nothing else): the kind belongs to the AV sync
            // stats panel, a different content of the same widget, and this
            // body is only ever reached under the PLAYER's own owner tag
            // (folder_overlay_open_row's fork). The arm exists because the
            // switch is exhaustive with no `default` — a kind that is not
            // this content's is stated and refused, never inherited.
            return;
    }
}

// The three below are the WIDGET'S mechanics (folder_overlay.h owns where the
// band may sit and how far the offset may run, for every content alike); what
// this cluster adds is the player's own damage — THE ROW'S beside the band's
// on the two highlight movers since 2026-08-30, because the Load in place
// button's face reads the highlight (render_player_button_enabled) and a
// face edge must reach a repaint through the row's one damage owner.
void GuiRenderPlayer::move_highlight(int delta) {
    if (folder_overlay::move_highlight(app, delta)) {
        damage_band();
        damage_row();
    }
}

void GuiRenderPlayer::set_highlight(int index) {
    if (folder_overlay::set_highlight(app, index)) {
        damage_band();
        damage_row();
    }
}

void GuiRenderPlayer::scroll_rows(int rows) {
    if (folder_overlay::scroll_rows(app, rows)) damage_band();
}

const AppState::RenderEntry* render_player_highlighted_entry(const AppState& a) {
    const AppState::FolderOverlay& ov = a.folder_overlay;
    if (ov.highlight_row < 0 ||
        ov.highlight_row >= static_cast<int>(ov.rows.size()))
        return nullptr;
    const Row& r = ov.rows[static_cast<size_t>(ov.highlight_row)];
    if (r.kind != Row::Kind::Wav || !r.entry) return nullptr;
    return &*r.entry;
}

const AppState::RenderEntry* GuiRenderPlayer::highlighted_entry() const {
    return render_player_highlighted_entry(app);
}

// THE ROW SPACE WOULD OPEN (architect 2026-08-31, R6) — the contract and the
// three-reader inventory are at the declaration (app_state.h). The fork is the
// row's KIND plus one identity compare: a folder is always somewhere to go, a
// wav is somewhere to go unless it is what is already bound, and the
// transport's own item hands the press back to the transport. (The `..` row
// was a third arm until 2026-09-01; going up is the modal row's own button
// now, and no row navigates.)
int render_player_highlight_act_row(const AppState& a) {
    const AppState::FolderOverlay& ov = a.folder_overlay;
    if (ov.highlight_row < 0 ||
        ov.highlight_row >= static_cast<int>(ov.rows.size()))
        return -1;
    const Row& r = ov.rows[static_cast<size_t>(ov.highlight_row)];
    switch (r.kind) {
        case Row::Kind::Folder:
            // A FOLDER ROW IN A LISTING THAT CARRIES NONE cannot exist: only
            // the root — `tmp/` itself — builds folder rows (rebuild_rows),
            // which is also why open_row's own batch arm returns doing
            // nothing — no producer, so this arm claims no dead press.
            return ov.highlight_row;
        case Row::Kind::Wav:
            return r.path == a.render_player.item ? -1 : ov.highlight_row;
        case Row::Kind::Text:
            // Not this content's kind (the record is at open_row above): the
            // player never builds one, and a row it did not build opens
            // nothing.
            return -1;
    }
    return -1;
}

// THE PLAY/PAUSE FACE — the contract and the readers are at the declaration
// (app_state.h). play_button_act's two forks in the act's own order, read
// without acting: the highlight's row first, then transport_toggle_act's
// three states.
PlayerPlayFace render_player_play_face(const AppState& a) {
    if (const int row = render_player_highlight_act_row(a); row >= 0) {
        const Row& r = a.folder_overlay.rows[static_cast<size_t>(row)];
        return r.kind == Row::Kind::Folder ? PlayerPlayFace::OpenFolder
                                           : PlayerPlayFace::Play;
    }
    switch (a.render_player.transport) {
        case Transport::Live:   return PlayerPlayFace::Pause;
        case Transport::Paused: return PlayerPlayFace::Resume;
        case Transport::Idle:   break;
    }
    return PlayerPlayFace::Play;
}

// THE TWO SHIFTED TWINS' WALLS — the contract and the three readers each are
// at the declaration (app_state.h): no item in a folder, or the item already
// at the end the jump names.
bool render_player_first_in_item_folder_actionable(const AppState& a) {
    const AppState::RenderPlayer& rp = a.render_player;
    const int n = static_cast<int>(rp.item_folder.size());
    return rp.item_index >= 0 && rp.item_index < n && rp.item_index > 0;
}

bool render_player_last_in_item_folder_actionable(const AppState& a) {
    const AppState::RenderPlayer& rp = a.render_player;
    const int n = static_cast<int>(rp.item_folder.size());
    return rp.item_index >= 0 && rp.item_index < n && rp.item_index + 1 < n;
}

// THE NEXT TRACK'S WALL (the contract at the declaration, app_state.h): an
// item must be bound, and it must not be the last of its folder — nothing
// loops, so the folder's end is where the act stops. The two terms are the
// act's own leading refusals in its own order, and the second is the shifted
// twin's own owner above, read rather than restated.
bool render_player_next_track_actionable(const AppState& a) {
    const AppState::RenderPlayer& rp = a.render_player;
    if (rp.item.empty() || rp.frames <= 0) return false;
    return render_player_last_in_item_folder_actionable(a);
}

// THE MODAL ROW'S DISABLED FACE — the contract, the per-act arms' rationale
// and the reader inventory are at the declaration (app_state.h). Each arm
// below is the act's own leading refusals in the act's own order.
bool render_player_button_enabled(const AppState& a,
                                  const GuiPlayback& playback,
                                  AppState::PlayerButtonAct act) {
    const AppState::RenderPlayer& rp = a.render_player;
    using Transport = AppState::RenderPlayer::Transport;
    switch (act) {
        // THE TWO SKIPS' FACES ARE HOME'S AND END'S (2026-08-31), each the
        // act's own leading refusals ORed with its shifted twin's under the
        // twin rule. NEITHER READS A POSITION: a LIVE transport always acts
        // (the reseek re-lands its window), and off LIVE the position IS
        // `resume_frame` — 0 at every idle rest by construction — so the
        // previous-track window's test collapses into "is there a previous
        // entry", which is also exactly when the shifted first-jump acts —
        // and so is read as that twin's own owner (2026-09-01).
        case AppState::PlayerButtonAct::Home:
            if (rp.item.empty() || rp.frames <= 0) return false;
            if (rp.transport == Transport::Live) return true;
            if (rp.transport == Transport::Paused && rp.resume_frame != 0)
                return true;
            return render_player_first_in_item_folder_actionable(a);
        case AppState::PlayerButtonAct::NextTrack:
            // ONE WALL CARRIES BOTH HALVES (2026-09-04, the plain act become
            // THE NEXT TRACK): the plain press plays the item folder's next
            // wav and the shifted twin its last, and each acts exactly where a
            // next entry exists — the plain act's predicate being that wall
            // plus the bound-item terms the wall already implies (item_index
            // is written with the item in play_wav and cleared with it in
            // unload_item, so a live index means a bound item). The OR the
            // twin rule asks for is therefore one term, and it is the act's
            // own owner rather than a copy of its conditions.
            // (The seek to `frames` was the plain act until that day and this
            // arm carried its live and paused positions; a next track is a
            // folder walk and reads no position at all.)
            return render_player_next_track_actionable(a);
        case AppState::PlayerButtonAct::PlayPause: {
            // THE HIGHLIGHT'S ARM FIRST, the act's own order since R6: a row
            // to open is an act in every transport state, so the button is
            // live even where the transport alone would have nothing to do.
            // AND THE DEVICE TERM SITS INSIDE IT, on the row's own KIND —
            // open_row's fork read without acting (2026-09-02, R-17a): a
            // FOLDER row ENTERS a listing and sounds nothing, so it needs no
            // device and stays lit; the other arm is a play and takes the
            // term. The reasoning is at the declaration.
            if (const int row = render_player_highlight_act_row(a); row >= 0) {
                const Row& r = a.folder_overlay.rows[static_cast<size_t>(row)];
                if (r.kind == Row::Kind::Folder) return true;
                return !playback.device_absent();
            }
            // THE TRANSPORT TAIL, EVERY ACT OF WHICH SOUNDS
            // (transport_toggle_act: a live transport pauses, a paused one
            // RESUMES, an idle one with an item PLAYS it) — so the
            // never-came-up device greys the whole tail. It is not
            // device_unavailable: the player's road reaches play(), which
            // reopens a dead stream at its head, so a route that dropped
            // mid-session is exactly what a press repairs.
            if (playback.device_absent()) return false;
            if (rp.transport != Transport::Idle) return true;
            return !rp.item.empty();
        }
        case AppState::PlayerButtonAct::Up:
            // THE ACT'S OWN WALL, through its one owner (2026-09-01): the
            // root is `tmp/` and there is nothing above it. No twin — the
            // button admits no modifier, so the plain form is the whole set.
            return render_player_up_actionable(a);
        // (STOP's arm stood here — the no-item belt and R36's already-resting
        // return — and went with the button on 2026-09-01.)
        // LOAD IN PLACE: the act's three leading refusals in the act's own
        // order — the lock, the running render (load_in_place_render_blocked,
        // a face term since 2026-09-01; the reasoning is at the declaration)
        // and the recipe-less highlight.
        case AppState::PlayerButtonAct::LoadInPlace:
            return !active_view_state(a).read_only &&
                   !load_in_place_render_blocked(a) &&
                   render_player_highlighted_entry(a) != nullptr;
        case AppState::PlayerButtonAct::RepeatOne:
        case AppState::PlayerButtonAct::Close:
            return true;
        case AppState::PlayerButtonAct::None:
            return false;
    }
    return true;
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
        refuse_decode(path, info.error());
        return false;
    }
    if (info->channels != audio.channels() ||
        info->sample_rate != audio.sample_rate()) {
        refuse_decode(path,
                      "This wav does not have the source's sample rate and "
                      "channel count");
        return false;
    }
    if (auto n = checked_audio_sample_count(info->frames, info->channels);
        !n) {
        refuse_decode(path, n.error());
        return false;
    }
    WavInfo read_info;
    auto samples = wav_read_full(path.string(), &read_info);
    if (!samples) {
        refuse_decode(path, samples.error());
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
        refuse_decode(path,
                      "This wav does not have the source's sample rate and "
                      "channel count");
        return false;
    }
    const int64_t frames =
        read_info.channels > 0
            ? static_cast<int64_t>(samples->size() /
                                   static_cast<size_t>(read_info.channels))
            : 0;
    if (frames <= 0) {
        refuse_decode(path, "This wav holds no samples");
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
    // THE SECOND LAUNCH BODY (the contract at the head of render_player.h):
    // the item's domain is [0, frames), the scanner never runs, and the
    // project's playhead does not move.
    playback.play(0, rp.frames);
    rp.transport = Transport::Live;
    // THE BAND FOLLOWS THE ITEM AT A CHANGE (R38, the contract at the head of
    // render_player.h): this is the ONE place the item changes, so seating the
    // highlight here covers every change the transport makes on its own —
    // Home's previous-track window, the folder's ends and the auto-advance —
    // with no membership list to keep. THE GATE IS
    // WHAT KEEPS THE
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
    // THE HIGHLIGHT LEADS (architect 2026-08-31, R6 — SPACE IS HIGHLIGHT-
    // DRIVEN IN THE PLAYER, narrowing R40's "the Play button never reads the
    // highlight, in any state" of two days before). A folder or
    // a wav that is NOT the transport's item under the band is somewhere to
    // GO, and Space goes there whatever is playing; the transport's own item
    // and an empty band fall through to transport_toggle_act below — the tail
    // this body used to hold inline, which the car's direction-named commands
    // now reach WITHOUT this fork (the reasons are at that body).
    //
    // R40'S BUG CANNOT COME BACK, which is what makes the narrowing safe: it
    // was a band left BEHIND the transport (a Next advanced the item and the
    // highlight stayed on the row he had double-clicked, so the live button
    // played that row instead of pausing what sounded), and THE BAND FOLLOWS
    // THE ITEM since R38 — at every item change the transport makes on its own
    // — so a highlight that is somewhere else is somewhere the user WALKED it,
    // deliberately, and going there is what he asked for.
    //
    // ONE ACTIVATION ROAD: open_row is the row click's and Enter's own body,
    // so the three row acts have one owner and this fork adds no second walk
    // of the listing. THE RESIDUAL DIFFERENCE BETWEEN ENTER AND SPACE is one
    // case and it is deliberate: on the TRANSPORT'S OWN ITEM with a session
    // standing, Enter (the click act) restarts it from 0 and Space toggles it
    // — everywhere else the two keys agree exactly.
    if (const int row = render_player_highlight_act_row(app); row >= 0) {
        open_row(row);
        return;
    }
    transport_toggle_act();
}

void GuiRenderPlayer::transport_toggle_act() {
    const AppState::RenderPlayer& rp = app.render_player;
    // THE TRANSPORT'S OWN BUSINESS — play_button_act's tail, LIFTED INTO A BODY
    // OF ITS OWN (2026-08-31, the round-B conversion) because the car's
    // DIRECTION-NAMED commands need exactly this and NOT the highlight fork
    // above it. R6 made Space highlight-driven, and a Pause or a focus loss
    // that reached the transport by synthesizing Space would then have STARTED
    // a walked-to row (or opened a folder) instead of pausing what sounded —
    // the very shape of R40's bug, arriving from the car's side. So the split
    // is by NAME: PlayPause, the undivided toggle, still synthesizes Space and
    // takes the whole act; Play, Pause and the two focus losses call THIS
    // (on_media_command's table carries the reasoning at each arm).
    //
    // THE STATE IS THE STORED FIELD (AppState::RenderPlayer::transport), so a
    // transport parked at frame 0 — the no-device pause on a wav that never
    // sounded — answers PAUSED here and resumes ITS item.
    switch (rp.transport) {
        case Transport::Live:
        case Transport::Paused:
            toggle_pause();
            return;
        case Transport::Idle:
            break;
    }
    // IDLE. THE FOLDER-END RESTART IS RETIRED (architect 2026-08-31, R7 — "we
    // simplify — play on last file means play last file"), and this is where
    // its arm stood: from 2026-08-28 a Play at a rest the natural end had left
    // on the item folder's LAST wav started the folder's FIRST file instead
    // (R27, the car's Play at the end of a playlist), off the one bit
    // `ended_at_folder_end`. The bit, its writer at the natural end and its
    // seven clears are deleted with the arm; a folder-end rest is now an
    // ordinary idle rest on the last item, and the car's Play there replays
    // that last track — which is also where the band is resting.
    //
    // ELSE THE ITEM FROM ITS START, ALWAYS (architect 2026-08-29 ~01:40:
    // "Play when idle should start literally"): `resume_frame` is 0 at every
    // idle rest by construction — seek_to's own head refuses an idle seek, so
    // nothing can have moved it since the last natural end, fresh bind or
    // unload — and toggle_pause's resume arm reads exactly that field.
    //
    // WITH NO ITEM THE ANSWER IS toggle_pause'S OWN SILENT GUARD (architect
    // 2026-08-31, R5 — the one-dimensional rule): a player resting with
    // nothing bound shows that state on its own row, the clock at zero and the
    // slider at its left end, so a sentence only repeats what is painted. This
    // body carried a carded guard of its own from 2026-08-30 as the OUTERMOST
    // site with the reason — the button, bare Space and the car's Play all
    // arrive here — and with the card gone it was a second copy of the
    // predicate behind it, so it is deleted rather than silenced: the resume
    // below is the consumed no-op, on `item.empty()` and on the `frames <= 0`
    // an item cannot reach alike.
    toggle_pause();
}

void GuiRenderPlayer::toggle_repeat_one() {
    AppState::RenderPlayer& rp = app.render_player;
    rp.repeat_one = !rp.repeat_one;
    // The lamp lives on the modal row; nothing else on the screen says this.
    damage_row();
}

// The device leaves its running state where the player has come to rest, and
// nowhere else (the five callers and the reasoning are at the declaration).
// Deliberately thin: the mechanism is GuiPlayback::suspend_stream's and the
// JACK backend answers it with nothing, so what this body exists for is to be
// a name the five rest roads call and the live-to-live transitions do not — a
// distinction the stop body's fence, taken by both classes alike, could never
// draw. The caller has just returned from that fence, which is the quiescence
// proof the suspension needs and does not take for itself.
void GuiRenderPlayer::rest_stream() {
    playback.suspend_stream();
}

void GuiRenderPlayer::toggle_pause() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.item.empty() || rp.frames <= 0) return;
    if (rp.transport == Transport::Live) {
        // PAUSE: the resume point is the engine's own position, read BEFORE
        // the stop body (whose fence is the one stop); the body's player fork
        // moves the transport to PAUSED — at whatever frame the cursor holds,
        // the item's start included — and damages the row.
        // THE RESUME POINT IS THE PREDICTOR'S POSITION, the one the picture
        // was drawn from — there is no second reading of the line to choose
        // between (architect 2026-09-03). The predictor is uncompensated, so
        // that position is AHEAD of the ear and a resume skips the run
        // between the two. HOW FAR AHEAD DEPENDS ON THE ANCHOR: the player
        // never resyncs — `resync_predictor` fires on the waveform camera's
        // own events and the player displays no waveform — so its anchor is
        // always the LAUNCH's, the publish instant, and the position leads
        // the ear by the launch's pickup phase (0 to one callback period,
        // re-rolled per launch) PLUS the device's output latency; the
        // phase-sized part of what a resume skips had not reached the port at
        // all when the press landed. After a resync the anchor is a real port
        // instant (the cycle stamp) and that phase term is gone, leaving the
        // latency alone. That is the same lead every other surface carries
        // and it is the ruling (playback.h's design note, the launch
        // arithmetic at playback_publish_play); a lead-free second face
        // existed for a day to park this write behind the ear and went with
        // the leads. The clock and the scrub paint from
        // the same call, so the picture does not step at the pause.
        // THE PROJECT'S OWN STOP PARKS NOTHING, verified:
        // stop_playback_if_playing leaves app.playhead_cursor_sample where
        // the user put it, so the player's two pauses are the whole
        // resting-write set — this arm and the tick's dead-device arm, which
        // reads the predictor's position exactly as this one does and writes
        // it before the stop body too. (up() was a third for the evening of
        // 2026-09-04; it unloads the item now and writes no resume point at
        // all — the record is at that body.)
        rp.resume_frame =
            std::clamp<int64_t>(playback.cursor(), 0, rp.frames);
        playback_lifecycle.stop_playback_if_playing();
        // The pause is the rest this whole mechanism exists for: the device
        // comes out of its running state behind the fence, so a head unit
        // reading the Bluetooth link stops seeing an active player under a
        // session that says paused (rest_stream's declaration owns the
        // membership; the ruling is at the head of playback_aaudio.cpp).
        rest_stream();
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
    rp.transport = Transport::Live;
    damage_row();
    // The head unit: playing again (the inventory at the declaration).
    publish_media_state();
}

// THE STOP BODY STOOD HERE AND IS RETIRED WHOLE (architect 2026-09-01,
// reversing R8's keep of the day before once the row's symmetry with the main
// window's transport became the goal — the record and the reasoning are at the
// head of render_player.h). It was R36's act: the rest written to frame 0 and
// the transport moved to IDLE, over the one stop body's player fork where the
// transport was sounding, a consumed no-op with no item and on an item already
// resting there. ITS THREE ROADS WERE ALL USER ACTS AND ALL DELETED WITH IT —
// the modal row's Stop button, bare `v` in route_render_player_key, and the
// head unit's Stop, which composes a pause with a seek to the top at
// on_media_command's own arm now. Nothing else called it, so nothing had to be
// kept: THE ONE STOP BODY (GuiPlaybackLifecycle::stop_playback_if_playing) is
// untouched, and every close, pause, natural end and rebind still passes
// through it exactly as before.

// THE TWO FOLDER WALKS SAY NOTHING AT ALL (architect 2026-08-31, R5 — the
// one-dimensional rule, which took the END'S OWN pair that morning and the
// NO-ITEM arm that evening): a benign refusal already at its state is silent.
// At the end, the highlighted row sitting at the listing's first or last line
// is the one glance that answers it; with no item — the state a freshly opened
// player rests in — the modal row is resting whole, its clock at zero and its
// slider at the left end, and that is the answer too. The three sentences the
// pair used to raise (kFirstInFolder / kLastInFolder and the shared
// kNoPlayerItem) are all deleted with their raises. Each walk still asks the
// two conditions in that order, so an empty transport never reaches the end's
// own arm. (They were FOUR walks until 2026-08-31 — the item's two neighbours
// took the same sentences on bare `,` / `.`; the step back lives inside home()
// now.)
//
// THE ITEM FOLDER'S ENDS (R37) — the play road with the index named outright
// instead of stepped. THE END ITSELF REFUSES: an item that is already the
// folder's first is already where "go to the first" would put it, and bare
// Home is what restarts a wav in place.
// BOTH WALLS ARE ONE OWNER EACH (2026-09-01): the no-item and the
// already-there returns are render_player_first_in_item_folder_actionable /
// render_player_last_in_item_folder_actionable, which the face and the hint's
// shift line read too — silent here, the rule above.
void GuiRenderPlayer::first_in_item_folder() {
    if (!render_player_first_in_item_folder_actionable(app)) return;
    const std::vector<Row> folder = app.render_player.item_folder;
    play_wav(folder.front().path, folder, 0);
}

void GuiRenderPlayer::last_in_item_folder() {
    if (!render_player_last_in_item_folder_actionable(app)) return;
    const std::vector<Row> folder = app.render_player.item_folder;
    const int last = static_cast<int>(folder.size()) - 1;
    play_wav(folder[static_cast<size_t>(last)].path, folder, last);
}

void GuiRenderPlayer::seek_by(int64_t delta_frames) {
    // EVERY REFUSAL IS seek_to's, THE ONE OWNER (2026-08-30, with the cards):
    // this body only composes the target, and its own copy of that guard
    // would have made Left / Right silent where bare Home already spoke.
    // position() answers 0 with nothing bound, so the sum is well-formed on
    // the road to the refusal.
    seek_to(position() + delta_frames);
}

void GuiRenderPlayer::seek_to(int64_t frame) {
    AppState::RenderPlayer& rp = app.render_player;
    // NO ITEM IS A SILENT CONSUME (architect 2026-08-31, R5, with the idle arm
    // below): a player with nothing bound rests with its clock at zero and its
    // slider at the left end, so the row already says what a sentence would.
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
    // AND IT SAYS NOTHING (architect 2026-08-31, R5, reversing 2026-08-30's
    // "and it says so"): the refusal is unchanged and only the card leaves —
    // an idle transport is a one-dimensional state the row itself shows, the
    // slider resting and the clock at zero, so the sentence repeated what was
    // already painted. The scrub's own gate is silent one road over for the
    // same reason.
    if (rp.transport == Transport::Idle) return;
    const int64_t target = std::clamp<int64_t>(frame, 0, rp.frames);
    if (rp.transport == Transport::Live) {
        // A LIVE RESEEK is the engine's own keep-alive shape (play() over a
        // live session, the reseek body's precedent): the window stays the
        // item and only the resume point moves. A target inside the last
        // frame would be a one-frame impulse and a play() over an empty range
        // returns without lowering the session word's playing bit, so the end of the item
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

// HOME — the contract is at the declaration. TWO ARMS OVER ONE POSITION TEST:
// THE PREVIOUS-TRACK WINDOW (architect 2026-08-31, kPlayerPreviousThresholdMs)
// and, everywhere else, the seek to the item's own start with every refusal
// seek_to owns.
//
// THE WINDOW IS THE POSITION THE CLOCK AND THE SCRUB SHOW — position(), the
// one reader, which is the engine's cursor while live and the resume point at
// every rest — so the act reads exactly what the user sees, and a second Home
// is "previous" at any press speed because the first one landed that position
// at 0. NOTHING HERE IS PRESS-TIMED: the constant's own declaration carries
// the reason (a head unit's buttons are slower than any double-press window).
//
// AN IDLE TRANSPORT TAKES THE WINDOW LIKE ANY OTHER, and that is deliberate
// rather than an oversight of the idle rule: `resume_frame` is 0 at every idle
// rest by construction, so an idle Home with a previous entry steps back and
// PLAYS it — the car's own act at a rest the folder's end left — while the
// seek arm
// underneath keeps R41's carded refusal for the first entry, where there is
// nothing to step back to. The idle rule is about not NUDGING a resting
// transport to some other point in the item it would not resume from; a track
// change is not a nudge.
//
// THE FORK IS ONE OWNER since 2026-09-01 (render_player_home_takes_previous
// below), which the Home button's hint reads too, so "Previous file" and
// "Go to start" are said exactly where each is what the press does — and,
// since codex round A the same day, so does the hint's SHIFT line, which
// compares this arm's destination with the shifted twin's and drops where the
// folder's second item makes them one file.
bool render_player_home_takes_previous(const AppState& a,
                                       const GuiPlayback& playback,
                                       const GuiAudio& audio) {
    const AppState::RenderPlayer& rp = a.render_player;
    const bool has_previous = render_player_first_in_item_folder_actionable(a) &&
                              !rp.item.empty() && rp.frames > 0;
    if (!has_previous) return false;
    // The window in frames at the DEVICE's rate — the item's own rate by
    // the decode's equality. A rate the engine cannot name closes the
    // window rather than opening it wide.
    const int64_t rate   = audio.sample_rate();
    const int64_t window = rate > 0 ? kPlayerPreviousThresholdMs * rate / 1000
                                    : 0;
    return render_player_position(a, playback) < window;
}

void GuiRenderPlayer::home() {
    if (render_player_home_takes_previous(app, playback, audio)) {
        const AppState::RenderPlayer& rp = app.render_player;
        const std::vector<Row> folder = rp.item_folder;
        const int i = rp.item_index - 1;
        play_wav(folder[static_cast<size_t>(i)].path, folder, i);
        return;
    }
    seek_to(0);
}

// THE NEXT TRACK (architect 2026-09-04, from the car: "Next should always skip
// to the next song. The home/end analogy doesn't quite work — this isn't a
// playhead, this is audio playback"). The contract is at the declaration; the
// walk itself is the natural end's own, shared below.
//
// IT OUTRANKS REPEAT ONE, and that is this act's own sentence rather than an
// omission: the lamp governs what happens when a file REACHES ITS END, and a
// press is not a natural end. Under a lit lamp the old act seeked to the
// item's end and the replay took it straight back to the same file, so Next
// did nothing at all in the car — this is the fix, and the act reads the lamp
// nowhere.
void GuiRenderPlayer::next_track() {
    advance_to_next_in_item_folder();
}

// THE FOLDER'S FORWARD STEP, ONE BODY FOR ITS TWO CALLERS (2026-09-04): the
// natural end's auto-advance and the deliberate Next of the right skip. Never
// across folders and never a wrap — the wall is the act's own owner above, so
// the button's face and both callers ask one question. Returns whether the
// next wav played; a refused decode has raised its own card and left the item
// where it was, which is what lets the natural end's Repeat One arm and its
// rest read this as "nothing happened".
bool GuiRenderPlayer::advance_to_next_in_item_folder() {
    if (!render_player_next_track_actionable(app)) return false;
    const AppState::RenderPlayer& rp = app.render_player;
    const std::vector<Row> folder = rp.item_folder;
    const int i = rp.item_index + 1;
    return play_wav(folder[static_cast<size_t>(i)].path, folder, i);
}

void GuiRenderPlayer::on_natural_end() {
    AppState::RenderPlayer& rp = app.render_player;
    // THE REST IS AT THE ITEM'S START, written BEFORE the stop body so the
    // "paused" the body's fork publishes reads position 0 rather than a
    // resume point left over from an earlier pause (the resume point is dead
    // state while the transport is live, so nothing else reads this write).
    rp.resume_frame = 0;
    // THE FENCE FIRST, through the one stop body: the session word's playing
    // bit is already down (the audio thread's terminal exchange lowered it)
    // but stop() is the quiescence proof the
    // next rebind requires, and the body's player fork takes the transport
    // down behind it — the tick's own natural-end shape.
    playback_lifecycle.stop_playback_if_playing();
    // THE REST IS IDLE, NOT PAUSED: the item rests AT ITS START, and there is
    // nothing there to resume that a Play from the start does not do
    // identically — so the next Play replays this item, at the folder's end
    // included (R7). Written over the PAUSED the
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
    // next wav. A lit lamp means this wav and nothing else, failure included.
    if (rp.repeat_one && !rp.item.empty() && rp.item_index >= 0 &&
        rp.item_index < static_cast<int>(rp.item_folder.size())) {
        const std::vector<Row> folder = rp.item_folder;
        const int i = rp.item_index;
        const bool replayed =
            play_wav(folder[static_cast<size_t>(i)].path, folder, i);
        if (!replayed) {
            // A refused replay is a rest like any other, so the device rests
            // under it: the lamp's arm is terminal, nothing follows this
            // return, and the transport is left idle on an item that will not
            // decode. The fence is this body's own, taken above — play_wav's
            // decode refusals all return ahead of the fence it would take, so
            // the last stop is still the one at the head of this function.
            rest_stream();
            damage_row();
        }
        return;
    }
    // AUTO-ADVANCE WITHIN THE ITEM'S FOLDER ONLY (R2), never across folders
    // and never a wrap: the next wav of the list the item was played from, or
    // the rest at the item's start. THE WALK IS ONE BODY SINCE 2026-09-04,
    // shared with the deliberate Next the right skip runs, so the two cannot
    // walk different folders.
    if (advance_to_next_in_item_folder()) return;
    // THE FOLDER IS AT ITS END (or the next wav refused to decode, its own
    // words on a card and the item unchanged) and the transport rests at the
    // item's start, IDLE — so the next Play replays THIS item. This rest,
    // open()'s and the Up act's unload are the state's whole production while
    // the mode stands (close() writes it on the way down), and UP IS THE ONE
    // USER ACT AMONG THEM since 2026-09-04 — no press reached the state
    // between the player's Stop retiring (2026-09-01) and that evening.
    //
    // THE FOLDER-END RESTART STOOD HERE (architect 2026-08-28, R27) and IS
    // RETIRED (architect 2026-08-31, R7 — "we simplify — play on last file
    // means play last file"): this arm was `ended_at_folder_end`'s one writer,
    // setting the bit at the item folder's last wav with the lamp off so that
    // the next Play — the car's Play at the end of a playlist above all —
    // started the folder's FIRST file instead. The bit is deleted whole with
    // its reader in play_button_act and its clears; nothing distinguishes this
    // rest from any other idle rest at an item's start now, which is the
    // simplification itself.
    //
    // This is the natural end's terminal rest (rest_stream's declaration owns
    // the membership): a replay and an advance that sound both sound again
    // within microseconds and must not stop the stream between the two items,
    // while here the player has finished and the head unit is looking at a
    // session that says so. It is one of this body's two rests, the other
    // being the refused Repeat One replay above — a rest is a rest, so that
    // arm takes the act too, and no road out of this function leaves an idle
    // transport over a running stream.
    rest_stream();
    damage_row();
}

void GuiRenderPlayer::tick() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.transport != Transport::Live) return;
    if (playback.device_unavailable()) {
        // NO DEVICE PAUSES, IT DOES NOT ADVANCE (the rule at on_natural_end's
        // declaration): an engine that cannot sound leaves the session word's
        // playing bit down exactly as a finished window does, so this arm
        // stands ABOVE the
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
        // way it will not. Nothing here retries, and this fork is a READ
        // (device_unavailable, never the reopen): on Android the next play
        // PRESS reopens the device — the main window's launch gates through
        // GuiPlayback::ensure_device_available_for_play, this player's own
        // road through play()'s head check (the one reopen body, both).
        // The resume point is the predictor's position, as the pause above
        // reads it — a suspended device holds the integer cursor and
        // extrapolates nothing, so here that is simply where the last fill
        // left the read cursor.
        rp.resume_frame =
            std::clamp<int64_t>(playback.cursor(), 0, rp.frames);
        playback_lifecycle.stop_playback_if_playing();
        // The rest act, as the pause arm this one copies takes it: nothing
        // follows this stop, so the device rests behind the fence. The two
        // shapes this arm covers meet it differently and both are right: a
        // device that never came up has no stream and the suspension returns
        // at once, while one that went away under a started stream is asked to
        // stop like any other rest — a refusal there is the suspension's own
        // stderr line and changes nothing, the reopen the next press takes
        // being what really closes that stream.
        rest_stream();
        // ONE CLAUSE (2026-09-01, the capitalization sweep's sentence
        // shape): it read "No audio device; the wav cannot be played".
        status("No audio device to play the wav");
        return;
    }
    // THE NATURAL END IS THE FLAG'S DROP, the main window's tick's own shape
    // (a deferred end that waited for the last queued frame to be heard stood
    // here for two days and went with the playback leads, 2026-09-03): the
    // render body lowers the playing bit when the fill reaches the item's
    // end, and the walk to the next item starts there — ahead of the last
    // sound leaving the speaker by the device's output latency PLUS that
    // fill's own valid prefix (the arithmetic at the session word,
    // playback_common.h), the near end of the lead every other surface
    // carries. Every OTHER road out of LIVE (the pause, the
    // dead-device arm above, the close, the rebind ahead of the next item)
    // takes the stop body at once.
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
        // THE SENTENCE NAMES THE ONE FOLDER THE PLAYER LOOKS AT (2026-09-01):
        // it read "no renders under render/ or tmp/" while the deliverable was
        // listed too, and the shape is unchanged.
        status("Nothing to play: no renders under tmp/");
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
    // every other bit.
    rp.repeat_one     = false;
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

// THE UNLOAD, and THE ONE OWNER of the order the engine's pointer demands
// (the contract at the declaration). Two callers, each naming its tail:
// close(), which adds the panel teardown, and up(), which adds the root entry
// — so the player standing with nothing loaded and the player gone are the
// same act with different tails, and the ordering cannot drift between them.
// The mode bit is the one thing the tail itself moves, since the two roads
// need it in different places (the reasons are at UnloadTail).
void GuiRenderPlayer::unload_item(UnloadTail tail) {
    AppState::RenderPlayer& rp = app.render_player;
    // THE ORDER IS LOAD-BEARING: stop (the fence) → the rest act → the mode
    // bit where the tail wants it → the VIEW's buffer rebound → only then the
    // item's buffer freed, the engine holding the pointer until the rebind.
    playback_lifecycle.stop_playback_if_playing();
    // The transport has come to rest and nothing follows it, on either tail,
    // so the device rests with it (the caller inventory and the reasons are at
    // rest_stream's declaration). Past the fence, which is what the suspension
    // requires and does not take.
    rest_stream();
    // IDLE OVER THE FORK'S PAUSED: the stop body's player fork parks every
    // LIVE transport at PAUSED, and an unloaded transport has nothing to
    // resume — so this write lands after that fork, exactly as open()'s reset
    // and the natural end's rest do (the state's whole writer set is at the
    // field, app_state.h).
    rp.transport      = Transport::Idle;
    rp.scrub          = AppState::RenderPlayer::ScrubDrag{};
    rp.pending_load.reset();
    // The mode bit goes where the tail puts it (the two roads' reasons are at
    // UnloadTail, render_player.h): the fence above was taken with the player
    // active on both roads, and the close's re-express below must run with the
    // player already down, so that a target completion resolving synchronously
    // on a reuse rung rebinds the engine instead of deferring to a re-express
    // that will never come. The Up tail writes the bit nowhere: the player goes
    // on standing.
    if (tail == UnloadTail::Close) rp.active = false;
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
    // UNCONDITIONAL, with an item or without one: a preview that completed
    // while the player stood did not rebind (GuiTargetRender::
    // complete_successful_buffer's guard), so re-expressing the view is what
    // this body owes whatever the transport was doing. The two tails meet that
    // guard from opposite sides: under Up the player is still standing, so a
    // preview dispatched here waits for the close's own re-express when it
    // completes and the engine rests on the source meanwhile, which is where
    // an unloaded player leaves it; under Close the bit is already down, so a
    // completion rebinds as it would with no player at all — the synchronous
    // one a cache or artifact rung resolves inside this very call, and an
    // asynchronous one when it lands.
    if (app.active_audio_view == 'T') {
        target_render.ensure_ready();
    } else {
        target_render.rebind_to_source();
    }
    // THE ITEM'S FIELDS TO THEIR open() VALUES, the buffer's memory with them.
    // The resume point and the painted cursor are cleared here too, which the
    // close never bothered to do — dead state either way, since open() rewrites
    // both — so that the fields the item owns go down together and up() has no
    // clears of its own.
    rp.buffer.clear();
    rp.buffer.shrink_to_fit();
    rp.frames         = 0;
    rp.item.clear();
    rp.item_folder.clear();
    rp.item_index     = -1;
    rp.resume_frame   = 0;
    rp.painted_cursor = -1;
}

void GuiRenderPlayer::close() {
    AppState::RenderPlayer& rp = app.render_player;
    if (!rp.active) return;
    // The unload carries the mode bit down, in the middle of its own body: the
    // stop body's player fork is what fences the engine out of the item's
    // buffer and that fork asks `active`, while the view's re-express behind
    // the fence must find the player already gone or a synchronous target
    // completion refuses to rebind — so the bit belongs between them and the
    // tail is what says so (UnloadTail::Close; the reasons are at the enum).
    // What is the CLOSE'S OWN is the panel's teardown.
    unload_item(UnloadTail::Close);
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

    // THE SPLIT (2026-08-31, the round-B conversion — the rule this table now
    // obeys): AN UNDIVIDED COMMAND TAKES THE WHOLE ACT, A DIRECTION-NAMED ONE
    // TAKES THE TRANSPORT ALONE. PlayPause says "the other one", which is
    // exactly what Space means in the player — the highlight fork included
    // (R6) — so it stays on the synthesis road and inherits every act the key
    // has. Play, Pause and the two focus losses name a DIRECTION, and a
    // direction is a claim about the TRANSPORT and about nothing else: since
    // R6 a synthesized Space would have read the band first, so a focus loss
    // with the highlight walked to another row would START that row instead of
    // pausing what sounds (R40's own bug, arriving from the car's side). They
    // call transport_toggle_act — play_button_act's tail past the highlight —
    // directly, which is a DIRECT ACT and not a second dispatch road for keys:
    // it joins SeekTo below — and, since 2026-09-01, Stop, which composes that
    // same toggle with a seek to the top now that the player has no stop key
    // to press — as a road with no keysym behind it, and like them it clears
    // no modal ring, the ring clear belonging to the
    // press lambda's membership ("every kind that synthesizes a key, and no
    // other") because a synthesized Space is what could press a ring-focused
    // button. THE GATES ARE UNCHANGED and still compose, each being a pure
    // test of the stored transport that decides whether the act runs at all.
    using Kind = GuiMediaCommand::Kind;
    switch (cmd.kind) {
        case Kind::PlayPause:
            // THE UNDIVIDED TOGGLE KEY TAKES NO STATE GATE: it says "the
            // other one", and Space in the player is exactly that act
            // (play_button_act's own fork, the highlight arm included). The
            // gates below exist only because Play and Pause name a direction.
            press(GuiKeys::Space);
            return;
        case Kind::Play:
            // A "play" said to a LIVE transport is already true and must not
            // toggle it off; anything else resumes or starts the transport's
            // own item. AT THE FOLDER'S END the transport is down, so this
            // Play passes the gate and replays the last track since R7 (R27's
            // restart to the folder's first file was the car's own case and is
            // retired). A WHEEL WALKS NO BAND, and this act reads none: the
            // head unit's Play means the transport's Play, whatever row the
            // band happens to rest on.
            if (rp.transport != Transport::Live) {
                transport_toggle_act();
                // AND A PLAY THE ACT COULD NOT START ANSWERS AS THE GATE'S
                // OWN REFUSAL DOES (the record is at the re-publish below).
                // The gate passes with the transport down, but the act
                // beneath it has refusals of its own — a player resting with
                // NOTHING BOUND is the live one, which is where `Up` and a
                // fresh `open()` leave it — and those went out silently, so
                // a head unit whose display had drifted to "paused" pressed
                // Play here forever and was never told the truth. THE TEST
                // IS THE RESULT, NOT A COPY OF THE ACT'S CONDITIONS (`rp` is
                // a reference, so it reads what the act just wrote): a
                // started transport is LIVE and has already published from
                // the play road itself, and anything else means nothing
                // moved and the unchanged truth is owed. The GUI side is
                // untouched — the refusal stays silent there, a player
                // resting with nothing bound showing that state on its own
                // row (the one-dimensional rule at transport_toggle_act).
                if (rp.transport != Transport::Live) publish_media_state();
                return;
            }
            // A REFUSED DIRECTION RE-PUBLISHES (architect 2026-09-04, after
            // the car's stuck pause). A head unit's toggle button sends the
            // direction ITS OWN DISPLAY believes — AVRCP has no play/pause
            // opcode, only PLAY and PAUSE — so a display that has drifted out
            // of step sends the verb that is already true, forever, and a
            // gate that drops it silently leaves the unit believing what it
            // believed. Telling it the truth at the press is what breaks the
            // loop: the state is unchanged, so this push is the same one the
            // last edge made, and the next press comes back with the other
            // direction. The act is still refused — a "play" said to a live
            // transport must not toggle it off, which is the gate.
            publish_media_state();
            return;
        case Kind::Pause:
            // Pause's own arm since 2026-09-04, lifted out of the focus
            // losses' below so that a refused PAUSE can answer as a refused
            // PLAY does (the record is at the Play arm): the gate is
            // unchanged — a "pause" said to a resting transport must not
            // start it — and the refusal now re-publishes instead of going
            // silent. This is the arm the car's stuck pause was pressing.
            if (rp.transport == Transport::Live) {
                transport_toggle_act();
                return;
            }
            publish_media_state();
            return;
        case Kind::FocusLost:
        case Kind::FocusLostTransient:
            // A focus loss pauses (Android's one imposed interrupt), and now
            // it pauses ALWAYS: the gate admits exactly a live transport and
            // the act it reaches is the transport's own, so nothing the band
            // is doing can turn an imposed interrupt into a play. A "pause"
            // said to a resting transport must not start it — the gate.
            // THESE TWO STAY SILENT WHERE THE GATE REFUSES: an imposed
            // interrupt is not a press, and there is no display belief behind
            // it to correct.
            if (rp.transport == Transport::Live) transport_toggle_act();
            return;
        case Kind::Stop:
            // THE HEAD UNIT'S STOP IS PAUSE AND THEN HOME (architect
            // 2026-09-01, with the player's own Stop act retired — it was that
            // act's key from R36, and a plain pause before R36 gave the row a
            // stop). AUDIBLY IT IS THE OLD STOP: silence now, the top on the
            // next Play. What differs is the state left behind — the CLASS IS
            // PAUSED rather than IDLE, so the scrub stays live under it and
            // the next Play resumes a rest that happens to be frame 0.
            //
            // TWO DIRECT ACTS IN ORDER, no key pressed and so no ring cleared
            // (the membership rule at the declaration). The pause is the
            // DIRECTIONAL tail (transport_toggle_act, R6's conversion — a
            // synthesized Space would have read the highlight first), gated on
            // LIVE exactly as Kind::Pause is; the seek is seek_to(0) DIRECT
            // AND NEVER home(), whose previous-track window would step a head
            // unit's Stop back a TRACK inside a file's first three seconds.
            // `rp` is a reference, so the second arm reads the state the first
            // one just wrote: LIVE pauses and then seeks, an already PAUSED
            // transport only seeks, and an IDLE one or a player with no item
            // does nothing at all (seek_to's own two silent refusals).
            if (rp.transport == Transport::Live) transport_toggle_act();
            if (rp.transport == Transport::Paused) seek_to(0);
            return;
        case Kind::Next:
            // THE WHEEL'S TWO SKIPS ARE THE PLAYER'S TWO SKIPS (architect
            // 2026-08-31): End and Home, the keys the row's buttons carry and
            // the main window's transport carries before them. The road is
            // the one synthesis, so the head unit inherits whatever those
            // keys mean — SINCE 2026-09-04 THE NEXT SONG on End ("Next should
            // always skip to the next song") and HOME'S PREVIOUS-TRACK WINDOW
            // unchanged, which is what gives the wheel a real previous-TRACK
            // act: inside the item's first kPlayerPreviousThresholdMs a
            // Previous steps back a file and past them it restarts the file,
            // the behaviour of the architect's own car. (Period / Comma from
            // 2026-08-30, Page Down / Page Up before that.) NOTHING ON A
            // WHEEL ASKS FOR THE FOLDER'S ENDS, so no command carries the
            // shift.
            //
            // AT THE FOLDER'S LAST FILE THE PRESS IS A WALLED NO-OP AND THE
            // UNIT IS TOLD SO, the directional arms' own rule read once more
            // (the record is at the Play arm above): nothing loops, so there
            // is nothing to walk to, and a re-publish is what keeps a head
            // unit's picture of the track from drifting on a press that
            // changes nothing. The wall is the act's own owner, asked here
            // rather than restated — the key this arm presses asks the very
            // same predicate inside the act.
            if (!render_player_next_track_actionable(app)) {
                publish_media_state();
                return;
            }
            press(GuiKeys::End);
            return;
        case Kind::Previous:
            press(GuiKeys::Home);
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
        // folder (the source's own parent): `tmp/<batch>/NN.wav`, a batch cell
        // being the only item the player can bind since it moved inside `tmp/`
        // (`render/<title>.wav` was the deliverable's spelling until
        // 2026-09-01) — lexically, no filesystem call, and in generic form so
        // the separator is `/` by construction.
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
