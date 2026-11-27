#include "render_player.h"

#include "car_transport.h"          // GuiCarTransport::kCarSeekSeconds (the car's one seek)
#include "failure.h"                // GuiFailure (the decode refusal's shape)
#include "folder_overlay.h"
#include "render_cache.h"           // read_fingerprint_sidecar (the item's limiter line)
#include "text_editor.h"            // next_session_id (the one modal counter)
#include "wav_io.h"                 // wav_probe, checked_audio_sample_count,
                                    // wav_read_full — called, never changed

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <system_error>
#include <utility>

using Folder    = AppState::RenderPlayer::Folder;
using Row       = AppState::FolderOverlayRow;
using Transport = AppState::RenderPlayer::Transport;

// THE REFUSED DEVICE'S CARD, one sentence for its two sayers: the tick's
// no-device arm (a drop under a sounding item) and a refused launch's rest
// (rest_if_launch_refused). ONE CLAUSE (2026-09-01): a card is one clause.
constexpr const char* kRenderPlayerNoDeviceCard =
    "No audio device to play the wav";

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

// THE DECODE'S REFUSAL, TWO CLAUSES (GuiFailure, failure.h — 2026-09-02):
// the card says the wav reader's own words
// alone — the subject is the highlighted row on screen, so
// the sentence needs no name (the basename rule's own reasoning,
// failure.h) — and the stderr line names the FULL
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
// (the load road's own refusal, 2026-08-28) — while `tmp/`'s batch cells are
// the player's whole subject.
//
// THE PRUNE HAS ONE TRIGGER, the deliverable's publish (the succession is
// at prune_render_folder, renders_dir.h): the archival render writes
// `render/<title>.wav` and prunes the folder at its completion. Only the
// PLAYER does not look at `render/`.

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

void GuiRenderPlayer::rebuild_rows(const std::filesystem::path& seat_folder) {
    AppState::FolderOverlay& ov = app.folder_overlay;
    AppState::RenderPlayer&  rp = app.render_player;
    ov.rows.clear();
    // A NEW LISTING (AppState::FolderOverlay::listing): the rows refilled
    // are new faces, none of the last listing's fades carried onto them.
    ov.listing = text_editor::next_session_id();

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

    // THE INITIAL HIGHLIGHT of every rebuild, THREE ARMS IN ORDER: the
    // transport's item's row when it is in this listing; else the row of
    // `seat_folder` when the caller named one and it is listed; else row 0 —
    // and -1 only for an empty listing.
    // THE SECOND ARM IS THE UP ACT'S ALONE (architect 2026-09-11): pressing
    // Up inside a batch folder is not a memory of where the band once was but
    // something happening right now — "I'm pressing Up on a specific folder,
    // and I expect that folder to be highlighted" — so up() names the folder
    // it is leaving and the root listing seats the band on that folder's row.
    // EVERY OTHER CALLER NAMES NONE, and ENTERING A FOLDER STAYS
    // MEMORY-LESS: the band takes the folder's top, or the item's row when
    // the item lives there, and a file played earlier that is no longer the
    // item is never sought out. A named folder that is not in the listing —
    // deleted while the player stood, the listing being rebuilt from disk at
    // every entry — falls through to row 0 like any other miss.
    // ROW 0 IS THE FIRST REAL ROW (no listing carries a `..` row), so the
    // seat is a batch folder or a wav and the highlight-driven Space
    // (2026-08-31) acts on content at every entry.
    ov.scroll_px     = 0.0;
    ov.press         = AppState::FolderOverlayPress{};
    ov.highlight_row = ov.rows.empty() ? -1 : 0;
    bool seated = false;
    if (!rp.item.empty()) {
        for (size_t i = 0; i < ov.rows.size(); ++i) {
            if (ov.rows[i].kind == Row::Kind::Wav &&
                ov.rows[i].path == rp.item) {
                ov.highlight_row = static_cast<int>(i);
                seated = true;
                break;
            }
        }
    }
    if (!seated && !seat_folder.empty()) {
        for (size_t i = 0; i < ov.rows.size(); ++i) {
            if (ov.rows[i].kind == Row::Kind::Folder &&
                ov.rows[i].path == seat_folder) {
                ov.highlight_row = static_cast<int>(i);
                break;
            }
        }
    }
    // THE SEATED ROW LANDS CENTERED (folder_overlay::scroll_row_centered,
    // architect 2026-10-26) on EVERY rebuild — the open's root listing, a
    // folder entered and the Up act's root entry — since each is a fresh
    // listing opening; the walks inside it keep the least move.
    folder_overlay::clamp_scroll(app);
    if (ov.highlight_row >= 0)
        folder_overlay::scroll_row_centered(app, ov.highlight_row);
    // THE BAND IS ONE RECT (2026-08-28): its height is the slot's ceiling
    // whatever this listing's length, so the rebuild's damage is the same band
    // every other damage takes. THE ROW REPAINTS WITH IT: the
    // rebuild reseats the highlight, which the Load in place and Delete
    // buttons' faces read (render_player_button_enabled).
    damage_band();
    damage_row();
    // THE HEAD UNIT WITH IT: a rebuild reseats the band, and with nothing
    // sounding the band IS the published title (the rule at
    // publish_media_state), so every entry is a display edge — the open's
    // root listing, a folder entered, and the Up act's root entry, which is
    // why neither open() nor up() carries a tail push of its own.
    // UNDER THE UP ACT THIS PUSH IS THE LAST WORD OVER THE STOP BODY'S FORK,
    // whose push still carries the item the unload ahead of this entry has
    // since dropped; it lands last because the entry is the last thing that
    // act does. The gate is the movers' own: a LIVE transport publishes its
    // item whatever listing is on the screen — a folder entered mid-play
    // changes nothing the head unit shows.
    if (rp.transport != Transport::Live) publish_media_state();
}

void GuiRenderPlayer::enter(Folder folder, const std::filesystem::path& dir,
                            const std::filesystem::path& seat_folder) {
    app.render_player.folder    = folder;
    app.render_player.batch_dir = dir;
    rebuild_rows(seat_folder);
}

// THE UP WALL'S ONE OWNER (the contract is at the declaration, app_state.h):
// the root is `tmp/` and there is nothing above it.
bool render_player_up_actionable(const AppState& a) {
    return a.render_player.folder != AppState::RenderPlayer::Folder::Root;
}

void GuiRenderPlayer::up() {
    // THE ROOT IS SILENT (architect 2026-08-31): a benign one-dimensional
    // refusal already at its state says nothing — THE UP BUTTON'S GREY IS
    // the glance that says it. The wall is asked through the predicate above, which the
    // button's face reads too, so the key and the button cannot disagree.
    if (!render_player_up_actionable(app)) return;
    // THE FOLDER BEING LEFT, read before the act runs: the root entry below
    // rewrites `batch_dir`, and the band's seat wants the folder this press
    // was made in (the rule is at rebuild_rows' seat block). The unload
    // between them leaves the field alone — it clears the ITEM's fields, not
    // the listing's place — so this copy is what survives both.
    const std::filesystem::path came_from = app.render_player.batch_dir;
    // UP UNLOADS THE ITEM (architect 2026-09-04, in his words: "The Up button
    // should simply stop audio and drop the selection, not pause it. It should
    // make it like when you first open the player: nothing is loaded"). WHY AN
    // UNLOAD RATHER THAN A PAUSE: the row carries no stop button and none is
    // wanted, and the car's dashboard has no stop either — so Up, the one act
    // that LEAVES the item's folder, is where stopping lives, and leaving the
    // folder means nothing is loaded. An unloaded transport is IDLE and the
    // root's listing is folder rows — the play/pause face reads the highlight ahead of the transport
    // (render_player_highlight_act_row), so it says "Open Folder" over a
    // transport that is doing exactly nothing, and no face can lie about the
    // sound.
    //
    // The ordering the engine's pointer demands is the shared body's (the
    // contract at unload_item's declaration), and the Up tail is what keeps
    // the mode bit standing through it — the player is not going anywhere.
    // What is UP'S OWN is the root entry — the listing rebuilt with no item
    // and SEATED ON THE FOLDER JUST LEFT (architect 2026-09-11: the press was
    // made in that folder and the band lands on it). THE HEAD UNIT'S PUSH
    // RIDES THAT
    // ENTRY and this act carries none of its own: the stop body's fork inside
    // the unload has already pushed a state carrying the very item this act is
    // dropping, and the entry's own push — the last thing to run here, with
    // the item's fields already cleared — is what supersedes it, naming the
    // folder the band just landed on. REPEAT ONE IS UNTOUCHED: the
    // lamp is session state that an open() resets and nothing else writes,
    // and going up a folder is not an open.
    unload_item(UnloadTail::Up);
    enter(Folder::Root, {}, came_from);
}

// THE ROOT'S BATCH-FOLDER COUNT (the contract is at the declaration,
// app_state.h): the listing's own Folder rows while it stands at the root.
int render_player_root_folder_count(const AppState& a) {
    if (a.render_player.folder != AppState::RenderPlayer::Folder::Root) return 0;
    int n = 0;
    for (const Row& r : a.folder_overlay.rows)
        if (r.kind == Row::Kind::Folder) ++n;
    return n;
}

// THE LISTED FOLDER'S RENDERS (the contract is at the declaration,
// app_state.h): the listing's own Wav rows while it stands inside a batch.
int render_player_batch_render_count(const AppState& a) {
    if (a.render_player.folder != AppState::RenderPlayer::Folder::Batch)
        return 0;
    int n = 0;
    for (const Row& r : a.folder_overlay.rows)
        if (r.kind == Row::Kind::Wav) ++n;
    return n;
}

// WOULD DELETE ACT (the contract at the declaration): the band on a row of the
// kind the list's plain press deletes — a batch folder at the root, a render
// inside a batch folder.
bool render_player_delete_actionable(const AppState& a) {
    const AppState::FolderOverlay& ov = a.folder_overlay;
    if (ov.highlight_row < 0 ||
        ov.highlight_row >= static_cast<int>(ov.rows.size()))
        return false;
    const Row::Kind kind = ov.rows[static_cast<size_t>(ov.highlight_row)].kind;
    switch (a.render_player.folder) {
        case Folder::Root:  return kind == Row::Kind::Folder;
        case Folder::Batch: return kind == Row::Kind::Wav;
    }
    return false;
}

// A FOLDER DELETE, ANSWERED (the contract is at the declaration).
void GuiRenderPlayer::delete_batch_folders(
        const std::vector<std::filesystem::path>& folders) {
    AppState::RenderPlayer& rp = app.render_player;
    if (!rp.active || folders.empty()) return;
    // A BOUND ITEM IN A FOLDER ABOUT TO GO IS UNLOADED FIRST (architect
    // 2026-11-25): from inside a folder the item, when one is bound, lives
    // there, and the question's rise has paused it; the Up tail is up()'s own
    // unload (the engine fenced and rebound off the item's buffer, the item's
    // fields cleared, the transport IDLE), so no transport, scrub, Repeat One
    // replay or car act reads a file that is gone. At the root nothing is
    // bound (up() unloads on the way there, open() binds none), and the
    // question's veil and the car's prompt guard keep the listing as it stood
    // until this answer.
    if (!rp.item.empty()) {
        for (const std::filesystem::path& f : folders) {
            if (rp.item.parent_path() == f) {
                unload_item(UnloadTail::Up);
                break;
            }
        }
    }
    // THE BOUNDS AND THE REMOVE are the one batch-folder deletion's
    // (remove_batch_folder, renders_dir.h, shared with the load in place's
    // tail): only a directory directly under this project's own `tmp/`,
    // named by the listing it came from, goes, and it goes for good. The
    // batch root is re-derived here from the source, the same owner the
    // listing's enumeration and every dispatcher name their folders by, so a
    // parked path that is anything else — `tmp/` itself, a path outside it, a
    // file — is refused whole.
    const std::filesystem::path batch_root =
        project_batch_root(app.source_audio_path);
    for (const std::filesystem::path& f : folders) {
        std::error_code ec;
        switch (remove_batch_folder(batch_root, f, ec)) {
            case BatchFolderRemoval::Removed:
                break;
            case BatchFolderRemoval::OutOfBounds:
                std::fprintf(stderr,
                             "warptempo_gui: delete refused: '%s' is not a "
                             "batch folder under '%s'\n",
                             f.string().c_str(), batch_root.string().c_str());
                break;
            case BatchFolderRemoval::Failed: {
                const GuiFailure fail = path_failure(
                    "Could not delete ", f, f.filename().string(),
                    ": " + ec.message());
                std::fprintf(stderr, "warptempo_gui: %s\n",
                             fail.diagnostic.c_str());
                notifications.notify(AppState::NotificationClass::Critical,
                                     fail.display);
                break;
            }
        }
    }
    // THE LISTING FROM DISK AGAIN, whatever failed: a partial removal shows
    // what is left, and the band takes the listing's top (the seat rule's
    // memory-less arm — the folder it stood on is gone).
    enter(Folder::Root, {}, {});
    // THE LAST FOLDER GONE CLOSES THE PLAYER (architect 2026-10-01): a root
    // listing with no folder left has nothing to play or open, so the player
    // takes itself down through its own close act — the PLAYER key's road,
    // the same unload and the same restore — rather than standing over an
    // empty band. The root lists folders and nothing else (rebuild_rows), so
    // an empty row list IS "no folder left".
    if (app.folder_overlay.rows.empty()) close();
}

// ONE RENDER'S DELETE, ANSWERED (the contract is at the declaration).
void GuiRenderPlayer::delete_render(const AppState::RenderEntry& entry) {
    AppState::RenderPlayer& rp = app.render_player;
    if (!rp.active) return;
    // THE FOLDER'S LAST RENDER IS THE FOLDER'S DELETE (architect 2026-11-25):
    // the emptied folder goes with it and the player goes up, through the one
    // folder delete, which unloads a bound item first.
    if (render_player_batch_render_count(app) <= 1) {
        delete_batch_folders({entry.batch_folder});
        return;
    }
    // The deleted row's place in the listing, read before the rebuild: the
    // band's seat after it (the question's veil kept the listing as it
    // stood since the press).
    const AppState::FolderOverlay& ov = app.folder_overlay;
    int place = -1;
    for (size_t i = 0; i < ov.rows.size(); ++i) {
        if (ov.rows[i].path == entry.wav_path) {
            place = static_cast<int>(i);
            break;
        }
    }
    // THE BOUND ITEM. This render: unloaded through up()'s own road (the Up
    // tail; the player stands on with nothing loaded), so no transport,
    // scrub, Repeat One replay or car act reads the file about to go. Another
    // render of this folder: it stays bound and paused as the question's
    // rise left it, and the removed render is struck from its item folder —
    // the list the next track, the folder's ends and the natural end's
    // advance walk — its index following, so nothing walks into the gone
    // file.
    if (rp.item == entry.wav_path) {
        unload_item(UnloadTail::Up);
    } else if (!rp.item.empty()) {
        for (size_t i = 0; i < rp.item_folder.size(); ++i) {
            if (rp.item_folder[i].path != entry.wav_path) continue;
            rp.item_folder.erase(rp.item_folder.begin() +
                                 static_cast<std::ptrdiff_t>(i));
            if (static_cast<int>(i) < rp.item_index) --rp.item_index;
            break;
        }
    }
    // THE RENDER'S WHOLE FILE SET, the cell's one enumeration
    // (batch_cell_file_set, renders_dir.h). A file already missing is the
    // ordinary case (the staging sibling, a sidecar never written) and is
    // silent; a removal that fails is one stderr line per file with its full
    // path, and ONE CRITICAL card for the render, named by its entry id
    // (render_entry_id, the question's own spelling) with the first failure's
    // words (failure.h's two clauses).
    bool carded = false;
    for (const std::filesystem::path& p :
         batch_cell_file_set(entry.batch_folder, entry.basename)) {
        std::error_code ec;
        std::filesystem::remove(p, ec);
        if (!ec) continue;
        const GuiFailure fail = path_failure(
            "Could not delete ", p, render_entry_id(entry),
            ": " + ec.message());
        std::fprintf(stderr, "warptempo_gui: %s\n", fail.diagnostic.c_str());
        if (!carded) {
            notifications.notify(AppState::NotificationClass::Critical,
                                 fail.display);
            carded = true;
        }
    }
    // THE LISTING FROM DISK AGAIN, whatever failed, and THE BAND ON THE ROW
    // THAT TOOK THE DELETED ONE'S PLACE (architect 2026-11-25) — the next
    // render, the new last row when the last went. The rebuild's own seat
    // rule would take the bound item's row; this reseat after it is the
    // delete's, the row centered as every rebuild's seat is, the band and the
    // row already damaged by the rebuild and the head unit told the new
    // title off a live transport (the question paused it).
    enter(Folder::Batch, entry.batch_folder, {});
    AppState::FolderOverlay& listed = app.folder_overlay;
    const int n = static_cast<int>(listed.rows.size());
    if (place >= 0 && n > 0) {
        const int seat = std::min(place, n - 1);
        if (seat != listed.highlight_row) {
            listed.highlight_row = seat;
            folder_overlay::scroll_row_centered(app, seat);
            if (rp.transport != Transport::Live) publish_media_state();
        }
    }
}

void GuiRenderPlayer::open_row(int index, bool tapped) {
    const AppState::FolderOverlay& ov = app.folder_overlay;
    if (index < 0 || index >= static_cast<int>(ov.rows.size())) return;
    // The row is copied: the act below rebuilds the listing under it.
    const Row row = ov.rows[static_cast<size_t>(index)];
    switch (row.kind) {
        case Row::Kind::Folder:
            switch (app.render_player.folder) {
                case Folder::Root:
                    // Entering names no folder to seat on: the band takes
                    // this listing's top, or the item's row if the item lives
                    // in here (the seat rule at rebuild_rows).
                    enter(Folder::Batch, row.path, {});
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
            play_wav(row.path, wavs, wi, tapped);
            return;
        }
        case Row::Kind::Plain:
        case Row::Kind::Disabled:
            return;   // the pickers' kinds; no player listing carries them
    }
}

// The three below are the WIDGET'S mechanics (folder_overlay.h owns where the
// band may sit and how far the offset may run, for every content alike); what
// this cluster adds is the player's own damage — THE ROW'S beside the band's
// on the two highlight movers, because the Load in place and Delete
// buttons' faces read the highlight (render_player_button_enabled) and a
// face edge must reach a repaint through the row's one damage owner.
//
// AND THE HEAD UNIT'S PUSH BESIDE IT (architect 2026-09-12, from the car):
// off a live transport the published title is THE SILENCE TRACK, which names
// the HIGHLIGHTED ROW, so a band move is a display edge like any other and
// each mover pushes AFTER its write and its damage. THE GATE IS
// `transport != Live`, because a live title is the item whatever the band is
// doing — walking the band under a playing item changes nothing the head unit
// shows, and a push per keystroke over a binder is not free. The two wrappers
// are the whole pointer-and-keyboard road here: the router's Up / Down, the
// row lift's first half (folder_overlay_highlight_row's player arm) and THE
// CAR'S OWN Previous / Next AT REST, which compose this body rather than
// walking a listing of their own and so reach the head unit's title through
// this very push. The third writer is
// rebuild_rows, which pushes in its own body, while play_wav's seat writes
// the widget direct and publishes at its tail with the item it just bound.
void GuiRenderPlayer::move_highlight(int delta) {
    if (folder_overlay::move_highlight(app, delta)) {
        damage_band();
        damage_row();
        abandon_paused_resume_if_band_left_item();
        if (app.render_player.transport != Transport::Live)
            publish_media_state();
    }
}

void GuiRenderPlayer::set_highlight(int index) {
    if (folder_overlay::seat_tapped_row(app, index)) {
        damage_band();
        damage_row();
        abandon_paused_resume_if_band_left_item();
        if (app.render_player.transport != Transport::Live)
            publish_media_state();
    }
}

// THE ABANDON: the contract is at the declaration. Silent unless a paused
// item is actually resting there, and a highlight ON NO ROW counts as left
// exactly as a highlight on a FOLDER row does — neither can be the item's own
// Wav row.
void GuiRenderPlayer::abandon_paused_resume_if_band_left_item() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.transport != Transport::Paused || rp.resume_frame == 0) return;
    const AppState::FolderOverlay& ov = app.folder_overlay;
    if (ov.highlight_row >= 0 &&
        ov.highlight_row < static_cast<int>(ov.rows.size()) &&
        ov.rows[static_cast<size_t>(ov.highlight_row)].path == rp.item)
        return;
    rp.resume_frame = 0;
    damage_row();
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

// THE ROW SPACE WOULD OPEN (architect 2026-08-31) — the contract and the
// three-reader inventory are at the declaration (app_state.h). The fork is the
// row's KIND plus one identity compare: a folder is always somewhere to go, a
// wav is somewhere to go unless it is what is already bound, and the
// transport's own item hands the press back to the transport. (Going up is
// the modal row's own button; no row navigates.)
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
        case Row::Kind::Plain:
        case Row::Kind::Disabled:
            return -1;   // the pickers' kinds; no player listing carries them
    }
    return -1;
}

// THE PLAY/PAUSE FACE — the contract and the readers are at the declaration
// (app_state.h). play_button_act's two forks in the act's own order, read
// without acting: the highlight's row first, then transport_toggle_act's
// three states on two faces (PAUSE while LIVE, PLAY otherwise).
PlayerPlayFace render_player_play_face(const AppState& a) {
    if (const int row = render_player_highlight_act_row(a); row >= 0) {
        const Row& r = a.folder_overlay.rows[static_cast<size_t>(row)];
        return r.kind == Row::Kind::Folder ? PlayerPlayFace::OpenFolder
                                           : PlayerPlayFace::Play;
    }
    switch (a.render_player.transport) {
        case Transport::Live:   return PlayerPlayFace::Pause;
        case Transport::Paused:
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
        // THE TWO SKIPS' FACES ARE HOME'S AND NEXT TRACK'S (2026-08-31 and
        // 2026-09-04), each the act's own leading refusals ORed with its
        // shifted twin's under the twin rule. NEITHER READS A POSITION: a LIVE
        // transport always acts (the reseek re-lands its window), and past
        // the no-item term the only other state is PAUSED (IDLE has no item,
        // the invariant at AppState::RenderPlayer::transport), whose position
        // IS `resume_frame` — away from 0 the reseek acts, and at 0 the
        // previous-track window's test collapses into "is there a previous
        // entry", which is also exactly when the shifted first-jump acts — and
        // so is read as that twin's own owner (2026-09-01).
        case AppState::PlayerButtonAct::Home:
            if (rp.item.empty() || rp.frames <= 0) return false;
            if (rp.transport == Transport::Live) return true;
            if (rp.resume_frame != 0) return true;
            return render_player_first_in_item_folder_actionable(a);
        case AppState::PlayerButtonAct::NextTrack:
            // ONE WALL CARRIES BOTH HALVES (2026-09-04, the plain act being
            // THE NEXT TRACK): the plain press plays the item folder's next
            // wav and the shifted twin its last, and each acts exactly where a
            // next entry exists — the plain act's predicate being that wall
            // plus the bound-item terms the wall already implies (item_index
            // is written with the item in play_wav and cleared with it in
            // unload_item, so a live index means a bound item). The OR the
            // twin rule asks for is therefore one term, and it is the act's
            // own owner rather than a copy of its conditions.
            return render_player_next_track_actionable(a);
        case AppState::PlayerButtonAct::PlayPause: {
            // THE HIGHLIGHT'S ARM FIRST, the act's own order (2026-08-31):
            // a row to open is an act in every transport state, so the button
            // is live even where the transport alone would have nothing to do.
            // AND THE DEVICE TERM SITS INSIDE IT, on the row's own KIND —
            // open_row's fork read without acting (2026-09-02): a FOLDER
            // row ENTERS a listing and sounds nothing, so it needs no device
            // and stays lit; the other arm is a play and takes the term. The
            // reasoning is at the declaration.
            if (const int row = render_player_highlight_act_row(a); row >= 0) {
                const Row& r = a.folder_overlay.rows[static_cast<size_t>(row)];
                if (r.kind == Row::Kind::Folder) return true;
                return !playback.device_absent();
            }
            // THE TRANSPORT TAIL, EVERY ACT OF WHICH SOUNDS
            // (transport_toggle_act: a live transport pauses, a paused one
            // RESUMES; an IDLE one has no item, the invariant at
            // AppState::RenderPlayer::transport, and does nothing) — so the
            // never-came-up device greys the whole tail. It is not
            // device_unavailable: the player's road reaches play(), which
            // reopens a dead stream at its head, so a route that dropped
            // mid-session is exactly what a press repairs.
            if (playback.device_absent()) return false;
            return rp.transport != Transport::Idle;
        }
        // THE HIGHLIGHT PAIR (architect 2026-10-10): the band walk's own
        // question, through the step's one owner, so the face and the bare
        // Up / Down keys cannot disagree about the wall.
        case AppState::PlayerButtonAct::HighlightDown:
            return folder_overlay::move_highlight_actionable(a, +1);
        case AppState::PlayerButtonAct::HighlightUp:
            return folder_overlay::move_highlight_actionable(a, -1);
        case AppState::PlayerButtonAct::Up:
            // THE ACT'S OWN WALL, through its one owner (2026-09-01): the
            // root is `tmp/` and there is nothing above it. No twin — the
            // button admits no modifier, so the plain form is the whole set.
            return render_player_up_actionable(a);
        // LOAD IN PLACE: the act's three leading refusals in the act's own
        // order — the lock, the running render (load_in_place_render_blocked,
        // a face term; the reasoning is at the declaration)
        // and the recipe-less highlight. THE LOCK IS BOTH LOCKS
        // (2026-09-10; authoring_locked, app_state.h): the load rewrites both
        // marker stores and the engine block and pushes an entry, so grid
        // iterations greys this button exactly as a read-only tab does, and
        // the KEY (bare `'` on the highlight) cards the forked sentence.
        case AppState::PlayerButtonAct::LoadInPlace:
            return !authoring_locked(a) &&
                   !load_in_place_render_blocked(a) &&
                   render_player_highlighted_entry(a) != nullptr;
        // DELETE (architect 2026-09-29; in both lists 2026-11-25): the act's
        // own question under the twin rule (render_player_delete_actionable —
        // the band on a batch folder at the root, on a render inside a batch
        // folder, which in each list is also every case the shifted press
        // has something to take). A RUNNING RENDER IS NO TERM: no run can
        // stand while the player does (open() refuses over one, and nothing
        // under the player starts one). THE LOCKS ARE NO TERM: a delete
        // writes no authoring.
        case AppState::PlayerButtonAct::Delete:
            return render_player_delete_actionable(a);
        case AppState::PlayerButtonAct::RepeatOne:
            return true;
        case AppState::PlayerButtonAct::None:
            return false;
    }
    return true;
}

// -- The transport --------------------------------------------------------------

int64_t GuiRenderPlayer::position() const {
    return render_player_position(app, playback);
}

double GuiRenderPlayer::scrub_x_of(int64_t frame) const {
    return render_player_scrub_x_of(app, frame);
}

int64_t GuiRenderPlayer::scrub_frame_at(double x) const {
    return render_player_scrub_frame_at(app, x);
}

bool GuiRenderPlayer::play_wav(const std::filesystem::path& path,
                               const std::vector<Row>& folder_wavs,
                               int index, bool tapped) {
    AppState::RenderPlayer& rp = app.render_player;

    // THE DECODE VOCABULARY: probe, the rate/channel equality against the
    // DEVICE (the engine was init()ed with the source's; nothing resamples),
    // the allocation owner on the probed shape, then the read — and THE SAME
    // EQUALITY AGAIN ON THE DECODED BUFFER'S OWN SHAPE, which is the one that
    // binds. Every refusal is its own words and the item does not change.
    // The probe, allocation and read arms are the frozen owners' own
    // adversarial and IO refusals, unduplicated; the equality is a breach
    // check at the engine's caller contract (a mismatched buffer plays at the
    // wrong speed rather than failing), reachable only by a hand-placed
    // foreign wav, and the same predicate asked at two points is no duplicate.
    //
    // THE PROBE'S ANSWER IS STALE BY CONSTRUCTION: wav_read_full REOPENS the
    // path, so the file object it decoded need not be the one probed here — a
    // wav republished between the two opens (every writer in this tree
    // publishes by rename: the render's own staging, warptempo_sync) is a different file at the same name. The probe is kept as the
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
    playback.rebind_buffer(GuiBoundAudio::float32(
        rp.buffer.data(), rp.frames, audio.sample_rate(), 0));
    // THE ITEM'S OWN LIMITER LIST (architect 2026-11-05): the bind has left the target buffer, so the lamp's
    // snapshot (AppState::bound_limiter_frames) is retaken from the item's
    // .fingerprint — its limiter line, read on the stat pair's identity
    // alone (read_fingerprint_sidecar: the player has no recipe to compare),
    // the item's wav frames being the list's domain and this bind's
    // domain_begin 0, so the frames are buffer-local as the tick wants them.
    // A record missing, malformed or of an earlier layout leaves the list
    // empty — the lamp dark for this item — with one stderr line; the item
    // is never re-rendered in place.
    app.bound_limiter_frames.clear();
    {
        FingerprintSidecarRecord record;
        if (read_fingerprint_sidecar(path.string(), record)) {
            app.bound_limiter_frames = std::move(record.limiter_frames);
        } else {
            std::fprintf(stderr,
                "warptempo_gui: Render player: no limiter list for '%s' "
                "(no .fingerprint of the current layout beside it)\n",
                path.string().c_str());
        }
    }

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
    // A REFUSED DEVICE LEAVES THE NEW ITEM BOUND AND PAUSED AT ITS START,
    // under the card (rest_if_launch_refused); the band, the damage and the
    // head unit below follow the item as at any change.
    (void)rest_if_launch_refused(0);
    // THE BAND FOLLOWS THE ITEM AT A CHANGE (2026-08-28, the contract at the
    // head of render_player.h): this is the ONE place the item changes, so
    // seating the highlight here covers every change the transport makes on its
    // own — Home's previous-track window, the folder's ends and the
    // auto-advance — with no membership list to keep. THE GATE IS
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
    // THE ROW'S TAP IS THE ONE PLAY THAT SCROLLS NOTHING (`tapped`, passed
    // down from the row's lift through open_row; THE PRESS NEVER SCROLLS,
    // architect 2026-11-21 — the ruling bans a scroll at a row's tap
    // and nowhere else): that lift has already seated the highlight on the
    // tapped row with the scroll untouched (folder_overlay::seat_tapped_row,
    // the row under the pen, partly cut or whole as the user left it), and a
    // keep-visible scroll here would move the list under the pen after the
    // tap. EVERY OTHER PLAY KEEPS THE KEEP-VISIBLE SEAT (set_highlight): Play
    // on the controls row (the highlighted row perhaps dragged out of view),
    // the transport's Next / Previous, the folder's ends, the auto-advance
    // and the car's console bring the playing item's row into view.
    if (item_changed && !tapped) {
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
    // The head unit: a new item, playing — or resting at its start after a
    // refused launch (the inventory at the declaration).
    publish_media_state();
    return true;
}

void GuiRenderPlayer::play_button_act() {
    // THE HIGHLIGHT LEADS (architect 2026-08-31 — PLAY IS HIGHLIGHT-
    // DRIVEN IN THE PLAYER). A folder or
    // a wav that is NOT the transport's item under the band is somewhere to
    // GO, and Play goes there whatever is playing; the transport's own item
    // and an empty band fall through to transport_toggle_act below, which
    // the IMPOSED INTERRUPTS reach WITHOUT this fork (the reasons are at that
    // body). THE CAR'S TOGGLE REACHES THIS WHOLE BODY past its LIVE arm
    // (architect 2026-09-07, from the car: the Accord's play does exactly
    // what the tablet's Play button does, band and all).
    //
    // THE BAND NEVER LAGS THE TRANSPORT: it FOLLOWS THE ITEM (2026-08-28) at
    // every item change the transport makes on its own, so a highlight that
    // is somewhere else is somewhere the user WALKED it, deliberately, and
    // going there is what he asked for — never a row the live button would
    // play instead of pausing what sounds.
    //
    // ONE ACTIVATION ROAD: open_row is the row tap's own body, so the three
    // row acts have one owner and this fork adds no second walk of the
    // listing. THE RESIDUAL DIFFERENCE BETWEEN THE ROW TAP AND PLAY is one
    // case and it is deliberate: on the TRANSPORT'S OWN ITEM with a session
    // standing, the tap restarts it from 0 and Play toggles it — everywhere
    // else the two agree exactly.
    if (const int row = render_player_highlight_act_row(app); row >= 0) {
        open_row(row, /*tapped=*/false);
        return;
    }
    transport_toggle_act();
}

void GuiRenderPlayer::transport_toggle_act() {
    const AppState::RenderPlayer& rp = app.render_player;
    // THE TRANSPORT'S OWN BUSINESS — play_button_act's tail, A BODY OF ITS
    // OWN (2026-08-31) because the IMPOSED INTERRUPTS need exactly this and
    // NOT the highlight fork above it: a focus loss that reached the
    // transport through the whole act would START a walked-to row (or open a
    // folder) instead of pausing what sounded. So the split is by NAME: the car's own toggle takes the WHOLE act off its
    // LIVE arm (a play past the band could start nothing at all with nothing
    // bound), while the two focus losses and Stop's leading pause call THIS
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
            // IDLE IS NO ITEM BOUND (the invariant at the field), so there is
            // nothing to toggle: a consumed no-op, SILENT because a player
            // resting with nothing bound shows that state on its own row, the
            // clock at zero and the slider at its left end (architect
            // 2026-08-31, the one-dimensional rule).
            return;
    }
}

void GuiRenderPlayer::toggle_repeat_one() {
    AppState::RenderPlayer& rp = app.render_player;
    rp.repeat_one = !rp.repeat_one;
    // The lamp lives on the modal row; nothing else on the screen says this.
    damage_row();
}

void GuiRenderPlayer::toggle_pause() {
    if (app.render_player.transport == Transport::Live) pause();
    else resume();
}

void GuiRenderPlayer::pause() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.item.empty() || rp.frames <= 0) return;
    if (rp.transport != Transport::Live) return;
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
    // arithmetic at playback_publish_play). The clock and the scrub paint from
    // the same call, so the picture does not step at the pause.
    // THE PROJECT'S OWN STOP PARKS NOTHING, verified:
    // stop_playback_if_playing leaves app.playhead_cursor_sample where
    // the user put it, so the player's two pauses are the whole
    // resting-write set — this arm and the tick's dead-device arm, which
    // reads the predictor's position exactly as this one does and writes
    // it before the stop body too — beside a refused launch's rest, which
    // writes the launch's own `from` before the same body
    // (rest_if_launch_refused; up() unloads the item and writes no
    // resume point at all).
    rp.resume_frame =
        std::clamp<int64_t>(playback.cursor(), 0, rp.frames);
    playback_lifecycle.stop_playback_if_playing();
}

void GuiRenderPlayer::resume() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.item.empty() || rp.frames <= 0) return;
    if (rp.transport == Transport::Live) return;
    if (rp.frames < 2) return;
    // RESUME: from the resume point. THE PLAY KEY AT THE END (architect
    // 2026-10-26, "next track if there is one, otherwise play from the
    // start"): a point in the item's last frame or at its end (the natural
    // end's rest, a paused seek to the end) CONTINUES where there is
    // something to continue to — with the REPEAT ONE lamp lit this item from
    // its start (the lamp's own answer at the end), else the next wav of the
    // item's folder through the shared forward step (whose refused decode
    // has raised its own card and is the press's answer: a rare non-silent
    // fault, no recovery) — and otherwise plays this item from its start in
    // the same press. A press always sounds; the automatic rewind is what
    // the ruling refused. THE RULE IS THE KEY'S ALONE: the scrub's lift
    // resumes through resume_at, which never runs it.
    int64_t from = rp.resume_frame;
    if (from >= rp.frames - 1) {
        if (!rp.repeat_one && render_player_next_track_actionable(app)) {
            advance_to_next_in_item_folder();
            return;
        }
        from = 0;
    }
    play_live_from(from);
}

void GuiRenderPlayer::resume_at(int64_t frame) {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.item.empty() || rp.frames <= 0) return;
    if (rp.transport == Transport::Live) return;
    if (rp.frames < 2) return;
    // THE SCRUB'S DROP (architect 2026-10-27, the drop decides through the
    // LIVE SEEK's end rule and never the PLAY key's): the item stays the one
    // grabbed, and a target in its last frame or at its end stops one short
    // of the last frame as seek_to's live arm does, so the natural end takes
    // over at once — the next track or the rest dead at the end, the one
    // rule's answer (on_natural_end).
    play_live_from(std::clamp<int64_t>(frame, 0, rp.frames - 2));
}

void GuiRenderPlayer::play_live_from(int64_t from) {
    AppState::RenderPlayer& rp = app.render_player;
    rp.painted_cursor = -1;
    playback.play(from, rp.frames);
    rp.transport = Transport::Live;
    if (rest_if_launch_refused(from)) return;
    damage_row();
    // The head unit: playing again (the inventory at the declaration).
    publish_media_state();
}

// A LAUNCH OVER A REFUSED DEVICE RESTS WHERE IT WAS TO PLAY FROM (architect
// 2026-11-26). The Play face greys only for a device that never came up
// (device_absent), so a press can still meet a refused reopen — or a refused
// start, or a drop seen at the publish's own re-check
// (GuiPlayback::launch_window) — and play() then returns with NOTHING
// SOUNDING. Left LIVE there, the transport met the tick's no-device arm,
// which parks the resume point at the ENGINE'S HELD CURSOR: a paused move
// was lost — the car's seek, Home, SeekTo (seek_to's PAUSED arm moves
// resume_frame, never the engine) and the scrub's drop (resume_at hands its
// target to play_live_from alone). So right after each launch's play() —
// play_live_from's (resume(), resume_at()) and play_wav's — this asks
// device_unavailable and, refused, takes THE PAUSE'S OWN SEATING at `from`:
// the resume point written first, then the one stop body, whose player fork
// parks the transport at PAUSED (its one writer of that state), damages the
// row and pushes the head unit's paused state; then the card said ONCE
// (play() itself cards nothing). The transport is never left LIVE, so the
// tick's no-device arm never runs for this launch and never cards it again;
// the next Play resumes from `from`. On a NEW item (play_wav) `from` is its
// start and the item stays bound, the band following it as at any change.
// THE READ IS THIS LAUNCH'S OWN VERDICT: every refusal inside play() leaves
// the stream null (or, the device never having come up, device_ready false),
// and both are written on this thread alone (a refused open sets nothing, a
// refused start and the publish's dead re-check close the stream), so a
// refused launch answers true and a good one false. The one other writer is
// the error callback's latch, on the device's thread: a drop landing between
// play()'s return and this read answers true for a launch that did start —
// a real drop, after which nothing sounds (the callback lowers the playing
// bit itself), so the rest is the tick's pause with the point at `from`,
// where the engine's held cursor all but stands.
// PRECONDITION: the transport is LIVE (each caller has just written it), so
// the fork takes it down. THE LIVE RESEEK (seek_to's LIVE arm) does not ask
// it: it would need the device to drop and be refused inside one frame,
// before the tick pauses.
bool GuiRenderPlayer::rest_if_launch_refused(int64_t from) {
    if (!playback.device_unavailable()) return false;
    app.render_player.resume_frame = from;
    playback_lifecycle.stop_playback_if_playing();
    status(kRenderPlayerNoDeviceCard);
    return true;
}

// THE TWO FOLDER WALKS SAY NOTHING AT ALL (architect 2026-08-31 — the
// one-dimensional rule): a benign refusal already at its state is silent.
// At the end, the highlighted row sitting at the listing's first or last line
// is the one glance that answers it; with no item — the state a freshly opened
// player rests in — the modal row is resting whole, its clock at zero and its
// slider at the left end, and that is the answer too. Each walk asks the
// two conditions in that order, so an empty transport never reaches the end's
// own arm.
//
// THE ITEM FOLDER'S ENDS (2026-08-28) — the play road with the index named
// outright instead of stepped. THE END ITSELF REFUSES: an item that is already
// the folder's first is already where "go to the first" would put it, and bare
// Home is what restarts a wav in place.
// BOTH WALLS ARE ONE OWNER EACH (2026-09-01): the no-item and the
// already-there returns are render_player_first_in_item_folder_actionable /
// render_player_last_in_item_folder_actionable, which the face and the hint's
// shift line read too — silent here, the rule above.
void GuiRenderPlayer::first_in_item_folder() {
    if (!render_player_first_in_item_folder_actionable(app)) return;
    const std::vector<Row> folder = app.render_player.item_folder;
    play_wav(folder.front().path, folder, 0, /*tapped=*/false);
}

void GuiRenderPlayer::last_in_item_folder() {
    if (!render_player_last_in_item_folder_actionable(app)) return;
    const std::vector<Row> folder = app.render_player.item_folder;
    const int last = static_cast<int>(folder.size()) - 1;
    play_wav(folder[static_cast<size_t>(last)].path, folder, last,
             /*tapped=*/false);
}

void GuiRenderPlayer::seek_by(int64_t delta_frames) {
    // EVERY REFUSAL IS seek_to's, THE ONE OWNER (2026-08-30, with the cards):
    // this body only composes the target for its one caller, the car's seek
    // (car_seek), and asks no guard of its own. position() answers 0 with
    // nothing bound, so the sum is well-formed on the road to the refusal.
    seek_to(position() + delta_frames);
}

void GuiRenderPlayer::seek_to(int64_t frame) {
    AppState::RenderPlayer& rp = app.render_player;
    // NO ITEM IS A SILENT CONSUME (architect 2026-08-31): a player with
    // nothing bound rests with its clock at zero and its slider at the left
    // end, so the row already says what a sentence would. IT IS THE IDLE
    // TRANSPORT'S WHOLE ANSWER, IDLE being no item bound (the invariant at
    // AppState::RenderPlayer::transport) — the dead slider (Audacious's own
    // stopped slider) is this guard. THE ONE OWNER: every
    // road that can move the point (the car's seek, Home, the car's absolute
    // SeekTo, its Stop, the scrub's lift) shares it, neither moving `resume_frame` nor damaging
    // anything; the scrub's press refuses to arm at its own gate, silent one
    // road over on the same term.
    if (rp.item.empty() || rp.frames <= 0) return;
    const int64_t target = std::clamp<int64_t>(frame, 0, rp.frames);
    if (rp.transport == Transport::Live) {
        // A LIVE RESEEK IS play() OVER THE LIVE SESSION (the main view's
        // placement stops instead, architect 2026-10-06,
        // stop_playback_if_playing; the score viewer's seek_to and
        // range_changed reseek its soundtrack the same way): play() on a
        // running session moves its resume point without tearing the session
        // down, so the seek costs no stop-and-restart glitch; the window stays
        // the item and only the resume point moves. A target inside the last
        // frame would be a one-frame impulse and a play() over an empty range
        // returns without lowering the session word's playing bit, so the end
        // of the item is reached by letting it play out — the seek stops one
        // frame short of the exclusive end, which is where the natural end
        // takes over (resume_at, the scrub's lift, clamps the same way).
        const int64_t live_target = std::min<int64_t>(target, rp.frames - 2);
        if (live_target < 0) return;
        rp.painted_cursor = -1;
        // THE LIMITER LAMP'S SPAN IS VOIDED (architect 2026-11-06): the
        // play stands across the jump, so the tick would test the
        // skipped frames as heard; -1 makes its next tick seed afresh.
        app.limiter_lamp_prev_frame = -1;
        playback.play(live_target, rp.frames);
        damage_row();
        // The head unit's clock, re-anchored on the seek (the inventory at
        // the declaration).
        publish_media_state();
        return;
    }
    // PAUSED (the only state left, an IDLE transport having no item and so
    // having returned above): a seek moves
    // the point, not the state — the transport stays paused at its new
    // point, where the next Play resumes it.
    if (rp.resume_frame != target) {
        rp.resume_frame = target;
        damage_row();
        publish_media_state();
    }
}

// THE PREVIOUS-TRACK WINDOW'S ONE OWNER (architect 2026-08-31,
// kPlayerPreviousThresholdMs; the contract is at the declaration): is the item
// inside its own first three seconds?
//
// THE WINDOW IS THE POSITION THE CLOCK AND THE SCRUB SHOW — position(), the
// one reader, which is the engine's cursor while live and the resume point at
// every rest — so the act reads exactly what the user sees, and a second
// press is "previous" at any press speed because the first one landed that
// position at 0. NOTHING HERE IS PRESS-TIMED: the constant's own declaration
// carries the reason (a slow press outruns any double-press window).
//
// ITS READER NEVER ASKS IT AT IDLE, which has no item (the invariant at
// AppState::RenderPlayer::transport): the file step's fork answers false on
// its own no-item term ahead of this. The folder's end rests PAUSED at the
// item's end (2026-10-26), outside the window, so Home there seeks the item's
// start and a second press inside the window steps back.
//
// ONE READER ASKS IT: render_player_home_takes_previous below, the file step's
// own fork (which home() and the Home button's hint share). The car's
// Previous asks no window: while an item sounds it is the car's seek
// (architect 2026-11-25, car_previous).
bool render_player_inside_previous_window(const AppState& a,
                                          const GuiPlayback& playback,
                                          const GuiAudio& audio) {
    // The window in frames at the DEVICE's rate — the item's own rate by
    // the decode's equality. A rate the engine cannot name closes the
    // window rather than opening it wide.
    const int64_t rate   = audio.sample_rate();
    const int64_t window = rate > 0 ? kPlayerPreviousThresholdMs * rate / 1000
                                    : 0;
    return render_player_position(a, playback) < window;
}

// HOME'S FORK — the contract is at the declaration: a previous entry to play
// AND the window above. THE FORK IS ONE OWNER (2026-09-01), which the Home
// button's hint reads too, so "Previous File" and "Go to Start" are said
// exactly where each is what the press does — and so does the hint's
// SHIFT line, which compares this arm's
// destination with the shifted twin's and drops where the folder's second item
// makes them one file.
bool render_player_home_takes_previous(const AppState& a,
                                       const GuiPlayback& playback,
                                       const GuiAudio& audio) {
    const AppState::RenderPlayer& rp = a.render_player;
    const bool has_previous = render_player_first_in_item_folder_actionable(a) &&
                              !rp.item.empty() && rp.frames > 0;
    if (!has_previous) return false;
    return render_player_inside_previous_window(a, playback, audio);
}

// THE LEFT SKIP'S ACT — bare Home and the Home button's plain press, and
// nothing else: the contract is at the declaration. TWO ARMS OVER ONE POSITION
// TEST — the previous-track window above, and everywhere else the seek to the
// item's own start with every refusal seek_to owns. IT NEVER LEAVES A FOLDER
// (architect 2026-09-12, from the car: "the car is a separate interface" —
// going up is the Up button, Backspace and the car's own Previous).
void GuiRenderPlayer::home() {
    if (render_player_home_takes_previous(app, playback, audio)) {
        const AppState::RenderPlayer& rp = app.render_player;
        const std::vector<Row> folder = rp.item_folder;
        const int i = rp.item_index - 1;
        play_wav(folder[static_cast<size_t>(i)].path, folder, i,
                 /*tapped=*/false);
        return;
    }
    seek_to(0);
}

// THE RIGHT SKIP'S ACT — bare End and the NextTrack button's plain press
// (architect 2026-09-04, from the car: "Next should always skip to the next
// song. The home/end analogy doesn't quite work — this isn't a playhead, this
// is audio playback"). The contract is at the declaration; the walk itself is
// the natural end's own, shared below.
//
// IT OUTRANKS REPEAT ONE, and that is this act's own sentence rather than an
// omission: the lamp governs what happens when a file REACHES ITS END, and a
// press is not a natural end. On the glass Next Track walks to the next file
// whatever the lamp says: honoring the lamp would turn the press into a
// replay of the same file, so the act reads the lamp nowhere.
void GuiRenderPlayer::next_track() {
    advance_to_next_in_item_folder();
}

// -- THE CAR'S OWN ACTS (architect 2026-09-12, from the car) -----------------
//
// THE CAR IS A SEPARATE INTERFACE: these bodies are the head unit's three
// buttons and the seek two of them share, reached from on_media_command
// alone, never from a key and never from the modal row. Their contracts are
// at the declarations.

// THE CONSOLE'S ONE PLAY/PAUSE BUTTON. The session publishes PLAYING whenever
// the player stands (publish_media_state's dummy display), so the console
// sends whichever verb its own picture believes — Pause or the undivided
// PlayPause, the two kinds that reach this body — and either of them means the
// same thing here: the other one. (A bare Play is NOT a press of that button
// but a head unit's autoplay at the connect, and it reaches the stream's
// reopen instead; the arm is at on_media_command.) A LIVE transport pauses and
// keeps its resume point — the silence track takes over the display at the
// pause's own push — and anything else takes the tablet's Play button WHOLE,
// the highlight arm included, so a highlighted folder opens, a highlighted
// other wav plays, and the item's own row resumes.
//
// NO STATE GATE AND NO RE-PUBLISH: with the published state a constant
// PLAYING there is nothing to drift from and no direction to contradict — the
// console's button is a toggle and this is the toggle it presses.
void GuiRenderPlayer::car_toggle() {
    if (app.render_player.transport == Transport::Live) {
        toggle_pause();
        return;
    }
    play_button_act();
}

// THE WHEEL'S REWIND. WHILE LIVE IT IS THE CAR'S SEEK BACK (architect
// 2026-11-25: "render player in car should also do ... skip back/forth when
// audio is playing", the step the project transport's 3 s): car_seek below.
// AT REST THE BAND IS THE PLAYLIST: a row above steps to it, and the FIRST row
// leaves the folder instead — the one direction of travel that does, because
// a listener knows which file starts a folder and never how many it holds,
// so this exit is always deliberate. At the ROOT's first row up() is its own
// silent wall. Changing track from the car is pause, step, play.
void GuiRenderPlayer::car_previous() {
    if (app.render_player.transport == Transport::Live) {
        car_seek(/*forward=*/false);
        return;
    }
    if (folder_overlay::walk_origin_row(app) > 0) move_highlight(-1);
    else                                          up();
}

// THE WHEEL'S FAST-FORWARD — Rewind's mirror image with ONE difference, and it
// is the ruling's own: this act NEVER LEAVES A FOLDER. While live, the car's
// seek forward; at rest, one row down and no further, the listing's last row
// a silent wall (move_highlight's own clamp), never an exit and never a wrap.
void GuiRenderPlayer::car_next() {
    if (app.render_player.transport == Transport::Live) {
        car_seek(/*forward=*/true);
        return;
    }
    move_highlight(+1);
}

// THE CAR'S ONE SEEK (architect 2026-11-25; the contract at the declaration):
// GuiCarTransport::kCarSeekSeconds at the device's rate, the item's own by
// the decode's equality, so the step is seconds of the item as it sounds, as
// the project transport's car seek is of its bound buffer (car_skip). A rate
// the engine cannot name seeks nothing, car_skip's own guard. The walls,
// the clamp and every refusal are seek_by's and seek_to's.
void GuiRenderPlayer::car_seek(bool forward) {
    const int64_t rate = audio.sample_rate();
    if (rate <= 0) return;
    const int64_t step = GuiCarTransport::kCarSeekSeconds * rate;
    seek_by(forward ? step : -step);
}

// THE FOLDER'S FORWARD STEP, ONE BODY FOR ITS THREE CALLERS (2026-09-04):
// the natural end's auto-advance, next_track
// (which the right skip's plain press runs), and resume() — THE PLAY KEY AT THE END, its end rule
// continuing to the next wav (architect 2026-10-26). Never across
// folders and never a wrap — the wall is the act's own owner above, so the
// button's face and every caller ask one question. Returns whether the
// next wav played; a refused decode has raised its own card and left the item
// where it was, which is what lets the natural end's Repeat One arm and its
// rest read this as "nothing happened".
bool GuiRenderPlayer::advance_to_next_in_item_folder() {
    if (!render_player_next_track_actionable(app)) return false;
    const AppState::RenderPlayer& rp = app.render_player;
    const std::vector<Row> folder = rp.item_folder;
    const int i = rp.item_index + 1;
    return play_wav(folder[static_cast<size_t>(i)].path, folder, i,
                    /*tapped=*/false);
}

void GuiRenderPlayer::on_natural_end() {
    AppState::RenderPlayer& rp = app.render_player;
    // THE REST IS AT THE ITEM'S END, written BEFORE the stop body so the
    // "paused" the body's fork publishes reads the end rather than a resume
    // point left over from an earlier pause (the resume point is dead state
    // while the transport is live, so nothing else reads this write).
    rp.resume_frame = rp.frames;
    // THE FENCE FIRST, through the one stop body: the session word's playing
    // bit is already down (the audio thread's terminal exchange lowered it)
    // but stop() is the quiescence proof the
    // next rebind requires, and the body's player fork takes the transport
    // down behind it to PAUSED — the tick's own natural-end shape. AT THE END
    // IT CONTINUES IF THERE IS A NEXT TRACK AND STOPS DEAD WHERE IT IS
    // OTHERWISE (architect 2026-10-26, "advance to the next one if
    // possible"; the score viewer's one rule): the two arms below play again
    // and take LIVE with them, and where neither does the transport stays
    // PAUSED AT THE ITEM'S END — the clock reading its length, the thumb at
    // the right, the scrub live, the key reading PLAY, whose press at that
    // rest is resume()'s end rule.
    playback_lifecycle.stop_playback_if_playing();
    // REPEAT ONE, ONE OF NOTHING LOOPS' TWO SANCTIONED EXCEPTIONS (architect
    // 2026-08-28; the other is the car's loop of the trim on the project
    // transport, 2026-09-17, which reaches no player audio): the lamp
    // replays THIS item from its start through the
    // player's own play road — the same road a user's Play takes, so the item
    // stays the transport's, the head unit is published at the edge as any
    // play publishes, and there is no second launch here. It outranks the
    // advance: repeat ONE means this wav and not the folder.
    // THE ARM RETURNS WHETHER THE REPLAY SOUNDED OR REFUSED, and that is what
    // "outranks" means: a replay that cannot decode (the item deleted or
    // republished in another shape while it played) has already put its own
    // words on a notification card and left the item untouched, and the rest
    // written above leaves the transport on that item at its end. Falling
    // through to the arms below would answer a refused REPEAT with the folder's
    // next wav. A lit lamp means this wav and nothing else, failure included.
    if (rp.repeat_one && !rp.item.empty() && rp.item_index >= 0 &&
        rp.item_index < static_cast<int>(rp.item_folder.size())) {
        const std::vector<Row> folder = rp.item_folder;
        const int i = rp.item_index;
        const bool replayed =
            play_wav(folder[static_cast<size_t>(i)].path, folder, i,
                     /*tapped=*/false);
        if (!replayed) {
            // The lamp's arm is terminal: nothing follows this return, and the
            // transport is left paused at the end of an item that will not
            // decode.
            damage_row();
        }
        return;
    }
    // AUTO-ADVANCE WITHIN THE ITEM'S FOLDER ONLY (2026-08-28), never across
    // folders and never a wrap: the next wav of the list the item was played
    // from, or the rest at the item's end. THE WALK IS ONE BODY
    // (2026-09-04), shared with the deliberate Next the right skip runs and
    // with resume()'s end rule, so the three cannot walk different folders.
    if (advance_to_next_in_item_folder()) return;
    // THE FOLDER IS AT ITS END (or the next wav refused to decode, its own
    // words on a card and the item unchanged) and the transport STOPS DEAD:
    // PAUSED at the item's end, so the next Play replays THIS item from its
    // start (resume()'s end rule, the folder's end having nothing next).
    // NOTHING DISTINGUISHES THIS REST from any other rest at an item's end
    // (architect 2026-08-31: "we simplify — play on last file means play
    // last file"): the next Play never starts the folder's first file.
    //
    // The player has finished and the head unit is looking at a session that
    // says so.
    damage_row();
}

void GuiRenderPlayer::tick() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.transport != Transport::Live) return;
    if (playback.device_unavailable()) {
        // NO DEVICE PAUSES, IT DOES NOT ADVANCE (the rule at on_natural_end's
        // declaration): an engine that cannot sound leaves the session word's
        // playing bit down exactly as a finished window does, so this arm
        // stands ABOVE the natural-end test, or a device that went away
        // mid-play (the AAudio latch) would read as a natural end and walk on
        // to the next wav. A LAUNCH THE DEVICE REFUSED NEVER REACHES THIS ARM
        // — a refused reopen, or the device that never came up (an init that
        // failed): the launch rests itself paused where it was to play from,
        // under the same card (rest_if_launch_refused, architect 2026-11-26).
        // It is the pause arm of toggle_pause with one line added — the
        // resume point is the engine's held cursor (a suspended device holds
        // it rather than extrapolating), read before the stop body, whose
        // fence returns at once on a dead or absent device and whose player
        // fork moves the transport to PAUSED and pushes the head unit's own
        // state — PAUSED AT THE HELD CURSOR EVEN WHERE THAT IS FRAME 0, which
        // is the whole reason the state is stored: the next Play resumes this
        // item at that cursor rather than restarting it. ONE LINE FOR EVERY
        // SHAPE (this arm's and the refused launch's): what the user needs to
        // know is that nothing will sound, not which way it will not. Nothing
        // here retries, and this fork is a READ (device_unavailable, never
        // the reopen): on Android the next play PRESS reopens the device —
        // the main window's launch gates through
        // GuiPlayback::ensure_device_available_for_play, this player's own
        // road through play()'s head check (the one reopen body, both).
        // The resume point is the predictor's position, as the pause above
        // reads it — a suspended device holds the integer cursor and
        // extrapolates nothing, so here that is simply where the last fill
        // left the read cursor.
        rp.resume_frame =
            std::clamp<int64_t>(playback.cursor(), 0, rp.frames);
        playback_lifecycle.stop_playback_if_playing();
        // Nothing follows this stop: the device is not touched here, and what
        // really closes a stream that went away is the reopen the next play
        // press takes.
        status(kRenderPlayerNoDeviceCard);
        return;
    }
    // THE NATURAL END IS THE FLAG'S DROP, the main window's tick's own shape
    // (2026-09-03): the render body lowers the playing bit when the fill reaches the item's
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
    // THE CLOCK'S LABEL MOVES ONCE A SECOND (whole seconds, floored;
    // architect 2026-11-25, paint_handler.cpp's THE TIME FIELDS): its slot is
    // damaged only when the label at the new cursor differs from the label
    // at the last one compared — the painter's own text (time_label_text
    // over render_player_position's clamp, the item's length and rate) — and
    // at every reset of the anchor (-1); the scrub at every cursor change.
    const int64_t rate = audio.sample_rate();
    const auto label_at = [&](int64_t c) {
        return time_label_text(std::clamp<int64_t>(c, 0, rp.frames),
                               rp.frames, rate);
    };
    const bool label_moved =
        rp.painted_cursor < 0 || label_at(cur) != label_at(rp.painted_cursor);
    rp.painted_cursor = cur;
    // The clock cell and the scrub track, the two cells the painter published
    // for exactly this damage; before the row's first paint both are zero and
    // the whole row is the honest widening.
    const GuiRect clock = app.modal_dialog.clock;
    const GuiRect scrub = app.modal_dialog.scrub;
    if (label_moved && clock.w > 0 && clock.h > 0)
        viewport.invalidate_rect(clock);
    if (scrub.w > 0 && scrub.h > 0) viewport.invalidate_rect(scrub);
    if ((clock.w <= 0 || clock.h <= 0) && (scrub.w <= 0 || scrub.h <= 0))
        damage_row();
}

// -- Enter and leave -------------------------------------------------------------

bool GuiRenderPlayer::open() {
    AppState::RenderPlayer& rp = app.render_player;
    if (rp.active) return false;
    if (app.source_audio_path.empty()) return false;
    // A RENDER RUN REFUSES THE OPEN (architect 2026-09-15): the player's close
    // and its Up re-express the view through unload_item, whose target-view
    // ensure_ready would kill the run — the reasoning is at the predicate,
    // app_state.h. ONE PLACE for every road (bare `l`, bare `'` outside the
    // `h` view, the Play renders button's plain lift), and ahead of the listing's walk, a run being the more
    // fundamental answer. THE CONSEQUENCE: nothing
    // starts a render while the player stands — its router consumes the
    // render chords, the top icon row is dead under it, and a parked archival
    // command cannot be armed at the open because this refusal reads it.
    if (load_in_place_render_blocked(app)) {
        status(kRenderPlayerWhileRenderingCard);
        return false;
    }
    if (!has_playable_render()) {
        // THE SENTENCE NAMES THE ONE FOLDER THE PLAYER LOOKS AT (2026-09-01).
        status("Nothing to play: no renders under tmp/");
        return false;
    }
    // THE OPEN ENTERS THE NEWEST BATCH FOLDER (architect 2026-09-15): a
    // sweep just finished, or the architect is back at a project he rendered
    // earlier, and the folder he wants is the one he last wrote into, not
    // tmp/'s own listing of every sweep this project has ever run.
    // "NEWEST" IS THE PRODUCT'S OWN DEFINITION of it — the highest leading
    // index, max_renders_batch_index's scan (renders_dir.h, whose comment
    // lists the allocators that read the same scan to name their NEXT folder)
    // — NEVER A FILESYSTEM TIMESTAMP, which a sync or a copy can disturb.
    // THE FALLBACK ASKS THAT FOLDER ALONE: a highest-index folder with no
    // PLAYABLE wav in it (every cell bracket-rejected, or the folder made
    // and nothing written into it yet) falls back to the root listing
    // whole, exactly as an empty tmp/ does — never the next-highest
    // playable folder, because the newest folder answering empty is the
    // honest answer to "what did I just render". has_playable_render()
    // above has already confirmed tmp/ holds SOMETHING playable; this asks
    // only whether it is the newest folder that does.
    Folder                open_folder = Folder::Root;
    std::filesystem::path open_batch_dir;
    const std::filesystem::path queue_root =
        project_batch_root(app.source_audio_path);
    const RendersBatchScan scan = max_renders_batch_index(queue_root);
    if (!scan.max_index_folder_name.empty()) {
        const std::filesystem::path candidate =
            queue_root / scan.max_index_folder_name;
        for (const AppState::RenderEntry& e :
             renders_dir.enumerate_render_entries()) {
            if (e.batch_folder == candidate) {
                open_folder    = Folder::Batch;
                open_batch_dir = candidate;
                break;
            }
        }
    }
    // A modal surface is opening: the shared modal stop
    // (stop_playback_for_modal_open — its declaration owns the decision table
    // and names this opener). Past every refusal above, as the rule demands.
    playback_lifecycle.stop_playback_for_modal_open();

    rp.active         = true;
    rp.session        = text_editor::next_session_id();
    rp.folder         = open_folder;
    rp.batch_dir      = open_batch_dir;
    rp.item.clear();
    rp.item_folder.clear();
    rp.item_index     = -1;
    rp.buffer.clear();
    rp.frames         = 0;
    rp.transport      = Transport::Idle;
    // REPEAT ONE IS SESSION-ONLY AND LIT AT EVERY OPEN (architect 2026-09-11
    // — the default is repeat the one item). The lamp is reset here and nowhere else, like the
    // mode's every other bit: it forgets at the close, survives the Up act's
    // unload, and bare `r` and the row's lamp flip it from lit.
    rp.repeat_one     = true;
    rp.resume_frame   = 0;
    rp.painted_cursor = -1;
    rp.scrub          = AppState::RenderPlayer::ScrubDrag{};
    rp.pending_load.reset();
    rp.pending_delete.clear();
    rp.pending_delete_render.reset();
    // THE BAND OPENS WITH THE PLAYER AS ITS OWNER — the tag is the panel's
    // standing predicate (folder_overlay_stands), so it is what raises the
    // overlay, and every other field of the panel is reset with it.
    app.folder_overlay       = AppState::FolderOverlay{};
    app.folder_overlay.owner = AppState::FolderOverlay::Owner::Player;
    // The open names no folder to seat on: ENTERING STAYS MEMORY-LESS
    // whichever listing rp.folder now names, so the band takes the newest
    // batch folder's top, or tmp/'s own top on the fallback. ONE REBUILD,
    // ONE PUSH: rp.folder / rp.batch_dir are already the folder decided
    // above, so this is the whole entry — no separate enter() call chases
    // it with a second rebuild and a second publish. THE HEAD UNIT IS
    // PUBLISHED BY THIS REBUILD and the open adds no push of its own: the
    // session goes ACTIVE with the mode (2026-08-28) and its title is the
    // silence track naming the fresh band's row — the newest batch folder's
    // first cell, or `tmp/`'s own top row (its first batch folder, or nothing
    // listed at all) on the fallback. Every field the push reads is written
    // above this line.
    rebuild_rows({});
    // A modal OPEN damages the whole window: the row's chrome greys, the band
    // appears over the waveform, and the modal row has no rect before its
    // first paint.
    viewport.invalidate_all();
    return true;
}

// THE UNLOAD, and THE ONE OWNER of the order the engine's pointer demands
// (the contract at the declaration). Its callers each name a tail:
// close(), which adds the panel teardown, and up(), which adds the root entry
// — so the player standing with nothing loaded and the player gone are the
// same act with different tails, and the ordering cannot drift between them;
// the two deletes (delete_batch_folders, delete_render) take up()'s tail
// ahead of their own listing rebuild.
// The mode bit is the one thing the tail itself moves, since the two roads
// need it in different places (the reasons are at UnloadTail).
void GuiRenderPlayer::unload_item(UnloadTail tail) {
    AppState::RenderPlayer& rp = app.render_player;
    // THE ORDER IS LOAD-BEARING: stop (the fence) → the mode bit where the
    // tail wants it → the VIEW's buffer rebound → only then the item's buffer
    // freed, the engine holding the pointer until the rebind.
    playback_lifecycle.stop_playback_if_playing();
    // IDLE OVER THE FORK'S PAUSED: the stop body's player fork parks every
    // LIVE transport at PAUSED, and an unloaded transport has nothing to
    // resume — so this write lands after that fork. It is one of IDLE's two
    // writers, open()'s reset the other, and like it clears the item below
    // (IDLE <=> no item bound, the invariant at the field, app_state.h).
    rp.transport      = Transport::Idle;
    rp.scrub          = AppState::RenderPlayer::ScrubDrag{};
    rp.pending_load.reset();
    // (The parked delete is NOT dropped here: the Up tail runs inside the
    // delete's own answer, after the prompt has consumed it. The close drops
    // both parked subjects with the rest of the mode, below.)
    if (tail == UnloadTail::Close) {
        rp.pending_delete.clear();
        rp.pending_delete_render.reset();
    }
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
        playback.rebind_buffer(GuiBoundAudio::float32(
            audio.samples_ptr(), audio.total_frames(), audio.sample_rate(), 0));
    }
    // The source is bound: the limiter lamp's snapshot is cleared with the
    // bind (AppState::bound_limiter_frames); the fork below re-takes it
    // where ensure_ready rebinds the target.
    app.bound_limiter_frames.clear();
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
    // THE CLOSE PUSHES NOTHING (architect 2026-09-17): with the car
    // transport standing behind this player, a push of the session INACTIVE
    // would hand the head unit back a dead session while the app went on
    // running. THE SESSION IS THE APP'S FOR ITS LIFE: the mode bit coming
    // down here is simply the wire changing owners, and GuiCarTransport::tick takes it
    // back on the FIRST TICK AFTER THIS CLOSE, its falling edge pushing the
    // project transport's own three lines (up to one tick of the player's
    // last picture on the console, accepted). No STOPPED state is pushed
    // anywhere while the app runs; the activity's onDestroy is what releases
    // the session.
}

// -- The car ---------------------------------------------------------------------

void GuiRenderPlayer::on_media_command(GuiMediaCommand cmd) {
    using Kind = GuiMediaCommand::Kind;
    const AppState::RenderPlayer& rp = app.render_player;
    // Belt: the hook's fork on this very bit is the partition, so a command
    // reaching here with the mode down was queued before the close drained.
    if (!rp.active) return;
    // PLAY IS THE ONE KIND ANSWERED AHEAD OF THE PROMPT GUARD, and the arm
    // stands here whole — the switch below carries a pointer back to it.
    //
    // PLAY MEANS REOPEN THE STREAM AND START NOTHING (architect 2026-09-18,
    // measured in the car; the car transport's table owns the measurement and
    // the accepted cost). The Accord sends a plain KEYCODE_MEDIA_PLAY about a
    // second after the Bluetooth link comes up — that IS its autoplay — while
    // every human press arrives as Pause, the dummy display leaving the
    // console nothing else to send; the connect kills the AAudio stream and
    // nothing but a press reopens it, so the arm reopens it and starts it
    // SILENT under the car's own fade-in, where the crackle is spent. No
    // transport starts, nothing is published, and the answer is ignored — a
    // console key is not a deliberate press at the glass, so a failed reopen
    // raises no card and the next real press cards at the launch gates.
    //
    // THE PROMPT HAS NOTHING TO PROTECT FROM IT, which is why it runs first:
    // the reopen writes no authored, transport, modal or published state, and
    // its whole purpose — the Bluetooth audio link coming up under the car's
    // fade-in — holds whatever stands on the screen, exactly as it does for
    // GuiCarTransport::on_media_command's own ungated arm, so both car roads
    // answer a connect alike whether or not a load confirmation happens to be
    // standing. The MODE BELT above it stays the player's own statement about
    // the state it needs, and EVERY OTHER KIND is still the prompt's to drop.
    if (cmd.kind == Kind::Play) {
        (void)playback.ensure_device_available_for_play();
        return;
    }
    if (app.prompt.active) return; // a question on the screen is answered there

    // EVERY COMMAND IS A DIRECT ACT ON THIS CLUSTER (architect 2026-09-12,
    // from the car: "the car is a separate interface"). The table and each
    // arm's reason are at the declaration; what stands here is the dispatch.
    //
    // NOTHING IS SYNTHESIZED: a direct act presses no key and no button, so
    // nothing on the screen can take a car command for its own press.
    //
    // AND A CAR COMMAND ACTS UNDER A POINTER DRAG, exactly as the modal row's
    // own button would: on_key's player block swallows every key while the
    // band's row press or the scrub's handle drag is armed, and that swallow
    // is the KEY road's — a direct act never enters it. The band's walk breaks
    // no armed press (a row lift re-hits the published rect), and the scrub's
    // release commits its seek against whatever item is live, clamped into it
    // by the mapping.
    switch (cmd.kind) {
        case Kind::Play:
            // Answered at the head of this body, ahead of the prompt guard,
            // and so never reached here; the arm and its reason are there.
            return;
        case Kind::Pause:
        case Kind::PlayPause:
            // ONE BUTTON, ONE BODY FOR THE TWO VERBS IT SENDS. The console's
            // display says PLAYING as long as the player stands, so whichever
            // of them it sends it means "the other one".
            car_toggle();
            return;
        case Kind::Next:
            car_next();
            return;
        case Kind::Previous:
            car_previous();
            return;
        case Kind::Stop:
            // THE HEAD UNIT'S STOP IS PAUSE AND THEN HOME (architect
            // 2026-09-01; the player has no Stop act of its own). AUDIBLY IT
            // IS A STOP: silence now, the top on the next Play. The state
            // left behind is PAUSED rather than IDLE, so the scrub stays live
            // under it and the next Play resumes a rest that happens to be
            // frame 0.
            //
            // The pause is the transport's own tail (transport_toggle_act —
            // the tablet's Play act reads the highlight first), gated on
            // LIVE; the seek is seek_to(0) DIRECT
            // AND NEVER the left skip's act, whose previous-track window would
            // step a head unit's Stop back a TRACK inside a file's first three
            // seconds. `rp` is a reference, so the second arm reads the state
            // the first one just wrote: LIVE pauses and then seeks, an already
            // PAUSED transport only seeks, and an IDLE one — no item bound,
            // the invariant at AppState::RenderPlayer::transport — meets
            // neither gate and does nothing at all.
            // THE ACCORD HAS NO STOP BUTTON; the arm stands for any console
            // that has one.
            if (rp.transport == Transport::Live) transport_toggle_act();
            if (rp.transport == Transport::Paused) seek_to(0);
            return;
        case Kind::FocusLost:
        case Kind::FocusLostTransient:
            // A focus loss pauses (Android's one imposed interrupt), and it
            // pauses ALWAYS: the gate admits exactly a live transport and the
            // act it reaches is the transport's own, so nothing the band is
            // doing can turn an imposed interrupt into a play — the split the
            // toggle above does not need and this one does. A "pause" said to
            // a resting transport must not start it: the gate.
            if (rp.transport == Transport::Live) transport_toggle_act();
            return;
        case Kind::FastForward:
            // THE CAR'S ONE SEEK, IN EVERY FOLDER (architect 2026-11-25: one
            // seek, one step): the Accord's wheel sends Previous / Next, whose
            // live arms are this same seek, and this pair is it wherever a
            // console sends these kinds — a paused item's resume point moving
            // as any paused seek's does, an idle player seek_to's silence.
            car_seek(/*forward=*/true);
            return;
        case Kind::Rewind:
            car_seek(/*forward=*/false);
            return;
        case Kind::SeekTo: {
            // Milliseconds to frames at the device's rate; seek_to clamps into
            // the item and refuses with no item.
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
    const AppState::RenderPlayer&  rp = app.render_player;
    const AppState::FolderOverlay& ov = app.folder_overlay;
    // NO REPEAT MODE IS PUBLISHED and no media command maps to one: the lamp
    // (2026-08-28) is the app's own state, the head unit shows what is playing
    // and its buttons are the transport's, and a repeat mode nothing can set
    // from the wheel is not worth a field on the wire.
    GuiMediaState st;
    // `session_active` SAYS PLAYING (architect 2026-09-12, from the car): the
    // consuming side's fork is "inactive -> STOPPED, else PLAYING" at speed
    // 1.0, because a console reads the still-streaming Bluetooth link as
    // playing and overrides a session that says PAUSED — so the session tells
    // it what it already believes and its one button becomes a plain toggle.
    // `playing` stays the TRUE transport bit and is read there for AUDIO FOCUS
    // ALONE. The rule and its reasons are at the declaration.
    // Both read the mode bit, which is TRUE on every road that reaches this
    // body (the belt below says why the arm is still here).
    st.session_active = rp.active;
    st.playing        = rp.active && rp.transport == Transport::Live;
    // THE ALBUM IS THE PROJECT'S NAME (architect 2026-09-12, from the car,
    // on the console's own layout): the Accord lays the album line ABOVE the
    // title, dim, and the artist line BELOW it, so the dim top line takes the
    // project — the least important of the three names on the head unit.
    st.album          = app.project_name;
    // THE MODE-DOWN ARM PUSHES NOTHING AND IS THE BELT (architect
    // 2026-09-17). The close pushes nothing at all and the car transport's
    // tick takes the wire back (the reasoning is at close()'s tail), so no
    // road reaches this body with the mode down — the nine call sites are
    // rebuild_rows, delete_render (its reseat after the rebuild),
    // move_highlight, set_highlight, play_wav, play_live_from (the resume's
    // road), seek_to (twice) and the stop body's player fork, and that fork
    // is itself gated on the mode bit. The arm stays because the fault it
    // guards is SILENT AND PERMANENT: a push with `session_active` false
    // calls setActive(false) on the sliver's session, and nothing in this
    // product would ever put it back — the car transport's comparator would
    // see no change of its own to push, so the head unit would stay dead for
    // the rest of the project's life. One branch against that.
    if (!rp.active) return;
    if (rp.transport == Transport::Live && !rp.item.empty() && rp.frames > 0) {
        // THE ITEM, with the clock at the engine's own cursor
        // (render_player_position) and its own length. THE ARTIST IS THE
        // ITEM'S OWN FOLDER, not the band's: the listener may have walked
        // into another folder while this one sounds, and what is sounding is
        // what the display is about.
        st.artist = rp.item.parent_path().filename().string();
        st.title  = rp.item.filename().string();
        const int64_t rate = audio.sample_rate();
        if (rate > 0) {
            st.duration_ms = rp.frames * 1000 / rate;
            st.position_ms = render_player_position(app, playback) * 1000 / rate;
        }
        gui.publish_media_state(st);
        return;
    }
    // THE SILENCE TRACK — what plays while the listener is at the top level
    // walking folders (his own framing). THE ARTIST IS THE FOLDER THE BAND IS
    // IN, `tmp` at the root and the batch folder's own name inside one, and
    // THE TITLE IS THE HIGHLIGHTED ROW'S OWN NAME, which is what the console's
    // button would start — a wav's or a folder's, bare, the artist under it
    // already saying where that name lives.
    //
    // POSITION 0 AND THE DURATION UNKNOWN (-1, which the consuming side turns
    // into "no duration key at all"): the state says PLAYING at speed 1.0, so
    // the console counts up from zero with no length to run into. A PAUSED
    // ITEM'S OWN CLOCK IS NOT PUBLISHED HERE — a real duration under a playing
    // state would have the console's clock run into the track's end and stop
    // there — and the tablet's own row is where that clock lives.
    st.duration_ms = -1;
    st.artist =
        (rp.folder == Folder::Root ? project_batch_root(app.source_audio_path)
                                   : rp.batch_dir)
            .filename()
            .string();
    const int row = ov.highlight_row;
    if (row >= 0 && row < static_cast<int>(ov.rows.size())) {
        st.title = ov.rows[static_cast<size_t>(row)].name;
    } else {
        // A listing with no rows, or one whose band has no seat — a batch
        // folder's cells deleted from outside while the player stood: THE
        // LISTED FOLDER NAMES ITSELF, the one road with nothing to highlight,
        // so the title is never empty while the session stands.
        st.title = st.artist;
    }
    gui.publish_media_state(st);
}
