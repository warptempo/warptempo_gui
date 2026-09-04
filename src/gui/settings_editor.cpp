#include "settings_editor.h"

#include "input_handler.h"
#include "render_output_naming.h"
#include "device_config.h"
#include "settings_io.h"
#include "warp_frame_map_view.h"  // the target-view re-land's two translations
#include "target_render.h"
#include "text_editor.h"
#include "undo.h"

#include "settings_file.h"     // warptempo_settings::validate_gui_setting
#include "frame_format.h"      // parse_authored_frame (the gui_scale arm)

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

// The commit line's interactive latitude: strip surrounding whitespace from the
// typed key and value (the FILE boundary is byte-exact and trims nothing).
//
// IT CANNOT SPLIT A MULTI-BYTE CHARACTER, which matters now that values carry
// UTF-8. `std::isspace` is consulted in the "C" locale — nothing in this
// program calls setlocale (locale_check.h states and enforces that) — where it
// is true for exactly 0x09..0x0d and 0x20, all ASCII. Every byte of a UTF-8
// multi-byte sequence is >= 0x80, so no continuation byte can be mistaken for
// whitespace and the trim always lands on a codepoint boundary.
std::string trim_ws(const std::string& s) {
    size_t a = 0;
    while (a < s.size() &&
           std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a &&
           std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// The leading half of the trim above, for the path completer alone: commit()
// trims both sides, so a value typed with a leading space is a path prefix
// like any other and Tab should complete it. The trailing side is deliberately
// left on, because the completer appends at the LINE'S end — a value that does
// not end where the buffer ends has nothing to append to, and letting the
// trailing bytes stay in the tail is what makes such a Tab match no name and
// walk the ring instead of writing the completion past the gap.
std::string ltrim_ws(const std::string& s) {
    size_t a = 0;
    while (a < s.size() &&
           std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    return s.substr(a);
}

bool is_key_char(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_';
}

// THE TWO PATH KEYS — the device config's `projects_path` and `sync_path`,
// the keys whose Tab completion is the filesystem's (complete_path_value).
// `projects_repo` is deliberately not one: a host/path, not a folder here.
bool is_path_completed_key(const std::string& key) {
    return key == "projects_path" || key == "sync_path";
}

// The longest common prefix of the matches, cut back to a CODEPOINT BOUNDARY:
// two names sharing only the lead byte of different multi-byte characters
// would otherwise yield a lone lead byte, which the editor's incoming filter
// drops as malformed — the answer would be right (no advance) but by
// accident. The cut makes it right by construction: a UTF-8 lead byte says
// how many bytes its sequence has, and a prefix that ends inside one is
// shortened to the byte before it.
std::string common_prefix_on_codepoint_boundary(
        const std::vector<std::string>& names) {
    if (names.empty()) return {};
    std::string lcp = names.front();
    for (size_t i = 1; i < names.size(); ++i) {
        const std::string& n = names[i];
        size_t k = 0;
        while (k < lcp.size() && k < n.size() && lcp[k] == n[k]) ++k;
        lcp.resize(k);
    }
    // Walk back to the last lead byte (any byte that is not a continuation
    // byte) and drop the sequence it starts if that sequence is incomplete.
    size_t lead = lcp.size();
    while (lead > 0 &&
           (static_cast<unsigned char>(lcp[lead - 1]) & 0xc0) == 0x80) {
        --lead;
    }
    if (lead == 0) return {};
    const unsigned char b = static_cast<unsigned char>(lcp[lead - 1]);
    size_t expected = 1;
    if      ((b & 0xe0) == 0xc0) expected = 2;
    else if ((b & 0xf0) == 0xe0) expected = 3;
    else if ((b & 0xf8) == 0xf0) expected = 4;
    if (lcp.size() - (lead - 1) < expected) lcp.resize(lead - 1);
    return lcp;
}

} // namespace

void GuiSettingsEditor::open_prefilled(const char* key) {
    // THE DROPDOWN'S ITEM CLICK, and it is open() plus the recall
    // autocomplete_value already performs — no parallel opener and no second
    // serializer. Seed `<key>=`, then let autocomplete_value fill the value
    // side through format_engine_setting_value / recall_gui_setting_value,
    // which is byte-identical to what a Ctrl+S writes; it leaves the cursor at
    // the line end and no-ops on an unrecallable key, so the bare `<key>=` is
    // the honest fallback rather than an error. Its bool answer is the TAB
    // arm's question and means nothing here, so this caller drops it.
    //
    // It carries no gate of its own and needs none: it delegates to open()
    // whole, so whatever that one owner decides is what this route gets, and
    // the is_active test on the next line is the belt that turns an open which
    // did not take into this route's silent return — the seed never runs.
    // Since 2026-09-04 the opener refuses nothing, the read-only decision
    // having moved to the key's own commit arm (the account is at open()), so
    // a locked tab's menu click raises the editor like any other and the
    // commit says the lock's sentence where the key is the piece's.
    if (key == nullptr) return;
    open();
    if (!text_editor::is_active(app.settings_editor)) return;
    app.settings_editor.pending          = std::string(key) + "=";
    app.settings_editor.cursor_pos       =
        static_cast<int>(app.settings_editor.pending.size());
    app.settings_editor.selection_anchor = -1;
    (void)autocomplete_value();
    viewport.invalidate_modal_dialog_area();
}

// The one opener, and it carries no read-only decision at all: the lock
// governs the keys, not the surface (architect 2026-09-04).
//
// The gate this function held from 2026-08-07 to 2026-09-04 was reasoned for
// the engine keys — the surface authors the engine settings, which the lock
// protects by the ruling's own vocabulary (read_only_key_blocked,
// input_key_dispatch.cpp: read-only protects the authored musical content, the
// two marker stores and the engine settings, and nothing else). What sitting at
// the surface could not see is that the same editor authors the four per-device
// keys, which belong to no piece (device_config.h: no undo, no dirty, no
// Ctrl+S), so a locked tab killed the one road to `sync_path`,
// `projects_path`, `projects_repo` and the scale — and on the tablet the menu
// is that road. The decision sits at each key's own commit arm now: the
// engine-key path in commit() refuses under the lock with kTabReadOnlyCard and
// the red flash every other refusal there wears, while commit_device_setting's
// three keys and the `gui_scale` arm commit regardless. So every Settings
// dropdown row opens on a locked tab, and the four sidecar rows (Title, Notes,
// URL, Cover) say the lock's sentence when they commit.
//
// The typed road did not move and keeps its refusal one level up: bare `;` is
// off the read-only allowlist and dies at the keyboard gate, which names the
// press in its own voice ("; is not available on a read-only tab"). What the
// dropdown opens on a locked tab is the whole editor, though, and its field is
// free text — a row prefills a key, it does not restrict the buffer — so every
// GUI-kind key can now be typed from a locked ACTIVE tab, the typed
// `tab_X_trim_*=` relaxation of 2026-08-07 among them, and
// `tab_X_read_only=false` with them as a self-unlock beside bare `o` and the
// icon row's toggle. That is the ruling working rather than a hole, and it is
// why the commit arm below stays engine-only: the GUI-kind keys are band
// rather than authored content — view state, follow, centered,
// center_on_next_marker, the magnification level, the per-tab viewport, zoom, playhead, trim and the
// read_only bit itself — and have been read-only-legal since 2026-08-07, so
// none of them owes a gate here.
void GuiSettingsEditor::open() {
    // ALREADY OPEN IS SILENT: the editor is on screen, which is the whole
    // answer — a second `;` asks for what is already there.
    if (text_editor::is_active(app.settings_editor)) return;
    // THE MODAL PLAYBACK STOP IS THE OPENER'S, past the return above — the
    // `h` view's own load raise takes the same shape ("playback halts only
    // when the modal actually opens, so a refused open leaves a listening
    // session undisturbed"). It moved here from the two call sites in
    // 2026-08-07 with the read-only gate that has since left again: a
    // caller-side stop would have made a refused open kill a live audition and
    // open nothing.
    playback_lifecycle.stop_playback_for_modal_open();
    text_editor::enter(app.settings_editor,
                       /*target=*/0,
                       /*initial_pending=*/"",
                       text_editor::Kind::SettingsAssignment);
    // A MODAL OPEN damages the whole window: the modal's rect does not exist
    // before its first paint (the stash is the painter's), so the status-row
    // owner's modal rider cannot cover it here. Every later repaint of the
    // session rides that owner (viewport.cpp).
    viewport.invalidate_all();
}

void GuiSettingsEditor::exit_no_commit() {
    if (!text_editor::is_active(app.settings_editor)) return;
    viewport.invalidate_modal_dialog_area();
    text_editor::deactivate(app.settings_editor);
}

// GUI-kind key router. It hands (key, value) to the single grammar owner
// validate_gui_setting (src/parser/settings_file.cpp — the same check the load
// schema runs), red-flashes AND CARDS with the returned reason on any
// malformed or out-of-vocabulary value (2026-08-30: one composed sentence, the
// stderr line and the card its two readers), and otherwise applies the typed
// value through the
// key's own gesture chokepoint — no parallel state writer, no grammar spelled
// twice. ITS ONE ARM AHEAD OF THAT OWNER is `gui_scale`, which left the load
// schema 2026-08-27 for the per-device config and reaches its grammar owner
// there instead; the rule is unchanged, only the file the value lands in.
// GUI-kind commits touch no undo history and no dirty state (launch/view
// state); a same-value commit no-op-deactivates like the
// engine no-op gate. Playback is already stopped (the editor is modal), so the
// appliers need no playback special-casing. Returns false when `key` is not a
// GUI-kind key, so the caller falls through to the engine-key path.
bool GuiSettingsEditor::commit_gui_setting(const std::string& key,
                                           const std::string& value) {
    // ONE COMPOSER, TWO READERS (architect 2026-08-30): the refusal sentence
    // is built once and read by the stderr line and by the card, a red field
    // saying only THAT it refused.
    auto reject = [&](const std::string& reason) {
        app.settings_editor.red = true;
        viewport.invalidate_modal_dialog_area();
        const std::string refusal = "Settings edit rejected: " + reason;
        std::fprintf(stderr, "warptempo_gui: %s\n", refusal.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
    };
    auto applied = [&]() {
        std::fprintf(stderr,
            "warptempo_gui: Setting applied: %s=%s\n",
            key.c_str(), value.c_str());
        viewport.invalidate_modal_dialog_area();
        text_editor::deactivate(app.settings_editor);
    };
    auto unchanged = [&]() {
        std::fprintf(stderr,
            "warptempo_gui: Setting unchanged: %s=%s\n",
            key.c_str(), value.c_str());
        viewport.invalidate_modal_dialog_area();
        text_editor::deactivate(app.settings_editor);
    };

    // THE DEVICE CONFIG'S SCALE, ahead of the shared schema owner because it is
    // not in that schema any more (2026-08-27): `gui_scale` is a per-DEVICE
    // preference living in its own file, and this editor is still its authoring
    // surface. THE GRAMMAR IS SPELLED ONCE ANYWAY — the canonical integer
    // spelling through parse_authored_frame and the RANGE through
    // is_gui_scale_percent (device_config.h), which is the very predicate that
    // file's reader runs, so "loadable iff it commits" still holds across the
    // move. (The other three editable device keys — projects_repo and, since
    // 2026-09-02, projects_path and sync_path — have no chokepoint to reach
    // and take their one direct-set body in commit(), commit_device_setting,
    // ahead of this router.)
    if (key == "gui_scale") {
        int64_t v64 = 0;
        if (!parse_authored_frame(value, v64) || !is_gui_scale_percent(v64)) {
            reject("must be an integer in [50, 350] in canonical spelling");
            return true;
        }
        // History-less, and APPLIED LIVE: the store write is no longer the
        // whole commit — since
        // 2026-07-31 the redesigned rows size on this value, so the commit
        // pushes it to the renderer and re-lays-out immediately (apply_gui_scale
        // runs the resize-path rebuild), and a `gui_scale=200` is visible
        // without a restart. It marks nothing dirty and Ctrl+S does not carry
        // it: since 2026-08-27 the applier WRITES THE DEVICE CONFIG at the
        // commit, so the value is persisted the moment it is applied.
        const int v = static_cast<int>(v64);
        if (v == app.gui_scale) { unchanged(); return true; }
        input->apply_gui_scale(v);
        applied(); return true;
    }

    // The shared grammar/vocabulary owner. std::nullopt: `key` is not a
    // GUI-kind key — fall through to the engine path. An error: malformed or
    // out-of-vocabulary value — red-flash with the returned reason. Otherwise
    // route the typed value through the key's gesture chokepoint below. The
    // editor's state-dependent refusals (the trim walls; the read-only-tab trim
    // refusal that stood beside them was deleted 2026-08-07) stay here.
    auto g = warptempo_settings::validate_gui_setting(key, value);
    if (!g) return false;
    if (!*g) { reject((*g).error()); return true; }
    const warptempo_settings::GuiSettingValue& gv = **g;

    // -- non-tab GUI keys ------------------------------------------------
    if (key == "follow") {
        if (gv.b == app.follow_mode) { unchanged(); return true; }
        playback_lifecycle.set_follow_mode(gv.b);
        applied(); return true;
    }
    if (key == "centered") {
        // History-less, through the same chokepoint the bare-`y` toggle and
        // its icon-row button use (set_centered_mode, follow's shape): the
        // off->on edge recenters immediately, so a typed `centered=true` is
        // one more caller of the toggle's own body, never a parallel writer.
        if (gv.b == app.centered_mode) { unchanged(); return true; }
        playback_lifecycle.set_centered_mode(gv.b);
        applied(); return true;
    }
    if (key == "center_on_next_marker") {
        // History-less, through the same chokepoint bare `n` and its icon-row
        // button use (set_center_on_next_marker): a typed
        // `center_on_next_marker=false` is one more caller of the lamp's own
        // setter, never a parallel writer. Nothing moves at the commit — the
        // bit is read at the next Tab walk.
        if (gv.b == app.center_on_next_marker) { unchanged(); return true; }
        input->set_center_on_next_marker(gv.b);
        applied(); return true;
    }
    if (key == "waveform_magnification_level") {
        // History-less and APPLIED LIVE, through the SAME chokepoint the TWO
        // hotkeys (bare `=` and bare `-`), the TWO icon-row buttons that
        // synthesize them and the PLAIN WHEEL use — the third of each retired
        // 2026-08-27 with Ctrl+0 and the reset button, this editor line being
        // the reset road that replaced them. A typed
        // `waveform_magnification_level=4` is one more caller of
        // apply_waveform_magnification_level, never a parallel writer. The
        // RANGE was already enforced by validate_gui_setting above, so an
        // out-of-range value never reaches here: it red-flashes at the grammar
        // owner, which is why the applier clamps nothing. The value persists on
        // the next ordinary Ctrl+S and marks nothing dirty.
        const int v = static_cast<int>(gv.i64);
        if (v == app.waveform_magnification_level) { unchanged(); return true; }
        input->apply_waveform_magnification_level(v);
        applied(); return true;
    }
    if (key == "active_audio_view") {
        if (gv.c == app.active_audio_view) { unchanged(); return true; }
        // The bare-`t` route (no editor-state guard); it flips S<->T.
        input->handle_active_audio_view_toggle();
        applied(); return true;
    }
    if (key == "active_markers_view") {
        if (gv.c == app.active_markers_view) { unchanged(); return true; }
        // The bare-`p` route; it flips W<->P and repaints.
        active_views.toggle_active_markers_view();
        applied(); return true;
    }
    if (key == "active_tab_view") {
        if (gv.c == app.active_tab_view) { unchanged(); return true; }
        // Exactly the Ctrl+Tab pair.
        active_views.switch_active_tab_view_to(gv.c);
        target_render.trigger();
        applied(); return true;
    }

    // -- per-tab GUI keys ------------------------------------------------
    char tab_char = 0;
    std::string suffix;
    if (key.rfind("tab_a_", 0) == 0) { tab_char = 'A'; suffix = key.substr(6); }
    else if (key.rfind("tab_b_", 0) == 0) { tab_char = 'B'; suffix = key.substr(6); }
    if (tab_char == 0) return false;   // not a GUI-kind key

    const bool active = (tab_char == app.active_tab_view);
    // The tab's band. active_view_state(app) returns exactly this band when the
    // tab is active; read_only lives here (never mirrored to a live field), so
    // the band is authoritative for read_only in both tabs.
    ViewState& band = (tab_char == 'B') ? app.tab_b : app.tab_a;

    if (suffix == "viewport_start") {
        const int64_t v = gv.i64;
        if (active) {
            if (v == app.viewport_start_sample) { unchanged(); return true; }
            // Assign-then-clamp: the same idiom every viewport mutation uses;
            // the clamp owns out-of-range constructively.
            app.viewport_start_sample = v;
            clamp_viewport_start(app, audio);
            viewport.invalidate_waveform_area();
            viewport.kick_waveform_sync();
        } else {
            if (v == band.viewport_start_sample) { unchanged(); return true; }
            band.viewport_start_sample = v;   // the restore clamps at tab-in
        }
        applied(); return true;
    }
    if (suffix == "zoom") {
        // validate_gui_setting already accepted the one continuous
        // [kMinZoom, kMaxZoom] double vocabulary and red-flashed anything else
        // (0 and 0.5 included); gv.d carries the level.
        const double v = gv.d;
        if (active) {
            if (v == app.zoom_level) { unchanged(); return true; }
            viewport.apply_zoom_change(v);
        } else {
            if (v == band.zoom_level) { unchanged(); return true; }
            // Stored verbatim, unclamped against today's per-file effective
            // ceiling: the ceiling is width- and domain-dependent (a narrower
            // window raises it; target view computes it against the deformed
            // total), and this parked band activates later under whatever
            // width and view are live then, so a store-time clamp against the
            // current context could destroy a value honorable at activation.
            // Parked zoom is display-scratch like the persisted
            // viewport/playhead fields -- clamp_viewport_start's level-clamp
            // is the sole owner of honoring it, at tab-in.
            // AND THE PARKED BAND'S WHOLE-SONG STATE GOES WITH IT (2026-09-02,
            // R-17g): a typed level is a zoom write like any other, and
            // leaving the bit standing would have the tab-in clamp pin this
            // very value back to the ceiling (the inventory of the four clears
            // is at ViewState::whole_song_visible).
            band.zoom_level = v;
            band.whole_song_visible = false;
        }
        applied(); return true;
    }
    if (suffix == "playhead_cursor") {
        const int64_t v = gv.i64;
        if (active) {
            if (v == app.playhead_cursor_sample) { unchanged(); return true; }
            // THE NAVIGATION-JUMP CLASS (architect 2026-07-29, no exemptions):
            // this is a playhead jump to an arbitrary typed position — a
            // non-marker spot — so it LEAVES the marker lane exactly as Home/End
            // do, and it clears the SELECTION and HIDES THE OVERLAY the way they do
            // (the precedent and its rationale live at the Home/End arms in
            // input_key_dispatch.cpp: a flag left selected would go on claiming
            // to be the playhead at its own position, and the next bare arrow
            // would tow the playhead back onto the marker, silently discarding
            // the jump). Collapse is cheap; leaving the overlay standing across
            // an arbitrary jump is worth less than the confusion it buys, and
            // hiding it discards nothing. THE HIDE IS THE MOVEMENT OWNER'S since
            // 2026-08-19 — it rides move_playhead_to below, the rule at
            // clear_region_highlight (input_handler.h) — so this arm spells only
            // its selection clear. The INACTIVE arm
            // below writes the other tab's stored cursor, moves nothing live,
            // and stays out of this entirely (it reaches no owner, so it hides
            // nothing, which is exactly right).
            if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
                selection.clear_selection();
                viewport.invalidate_waveform_area();
            }
            // The live chokepoint; its clamp owns out-of-range constructively.
            viewport.move_playhead_to(v);
        } else {
            if (v == band.playhead_cursor_sample) { unchanged(); return true; }
            band.playhead_cursor_sample = v;
        }
        applied(); return true;
    }
    if (suffix == "read_only") {
        // Navigation-class (allowed even while the tab is read-only), and the
        // remote-unlock route for the OTHER tab. read_only lives in the band for
        // both tabs.
        // IT WAS THE REMOTE ROUTE ONLY UNTIL 2026-09-04, when the lock moved
        // from this surface to the keys (the account is at open()): the editor
        // did not raise on a locked ACTIVE tab, so the arm was reached only by
        // typing `tab_X_read_only=false` from an unlocked tab about the other
        // one. The Settings dropdown raises the editor on a locked tab now,
        // and its field is free text — the row is a prefill, not a
        // restriction — so a self-unlock can be typed there. That is the
        // ruling working rather than a hole: this key is band, not authored
        // content, navigation-class on the read-only allowlist's own reading,
        // and the ordinary unlock is bare `o` or the icon row's toggle
        // standing right there. Bare `;` still cannot open the editor on a
        // locked tab at all, being off that allowlist one level up.
        if (gv.b == band.read_only) { unchanged(); return true; }
        // The bit's one setter, shared with bare `o` (the contract is at
        // GuiInputHandler::set_tab_read_only, input_handler.h): it writes the
        // named band and, when that band is the active one, damages the top
        // strip so the icon row's Lock glyph stops showing the state the tab
        // just left. applied() below damages the modal area alone, so a typed
        // self-lock or self-unlock would otherwise close the editor onto a
        // stale padlock.
        input->set_tab_read_only(tab_char, gv.b);
        applied(); return true;
    }
    if (suffix == "trim_begin" || suffix == "trim_end") {
        const bool is_begin = (suffix == "trim_begin");
        // THE LIVE / PARKED SPLIT, which both value arms below branch on
        // (`active`). A tab_A key edited while tab A is active writes the LIVE
        // trim, and that commit is a trim SETTER: it DESELECTS like every other
        // route that commits the live window (architect 2026-07-30, "a typed
        // commit is a commit"). A tab_B
        // key edited from tab A writes only the PARKED band — no live trim,
        // nothing visible changed — so it deselects nothing; the entering tab
        // rests its pair bare either way (the Ctrl+Tab pull is an entry route,
        // not a setter).
        // NO READ-ONLY REFUSAL (architect 2026-08-07, deleting the one this arm
        // carried): read-only protects the AUTHORED MUSICAL CONTENT — the two
        // marker stores and the engine settings — and trim is BAND, sitting in
        // ViewState beside the viewport, the zoom, the playhead and the
        // read_only bit itself, all four of which were already allowed here.
        // The refusal existed to mirror the trim GESTURES' read-only returns,
        // and those are deleted the same day, so mirroring them now means
        // committing. It covered BOTH arms below (the guard read the NAMED tab's
        // band, not the active one), so a parked `tab_b_trim_*=` written from a
        // writable tab A while B is locked commits too — the same rule, and the
        // weaker case of it: that write moves nothing visible at all. The full
        // ruling is at read_only_key_blocked (input_key_dispatch.cpp).
        // THE `-1` UNSET ARM IS GONE (architect approval 2026-07-30): the trim
        // window is always set, so there is nothing to unset. A typed `-1` now
        // fails the SHARED validator (validate_gui_setting, settings_file.h —
        // the same grammar the whole-file load runs, so a spelling is loadable
        // iff it commits) and red-flashes like any other invalid value, with no
        // second spelling of the refusal here. Shift+[ is the maximizer.
        const int64_t v = gv.i64;
        // Per-bound walls, exactly the load guard's compare: both bounds
        // 0..EOF-1, the unified inclusive [0, total-1] authored domain.
        const int64_t total = audio.total_frames();
        const int64_t wall = total - 1;
        if (v < 0 || v > wall) {
            reject("trim bound is past its wall"); return true;
        }
        TrimState& t = active ? app.trim : band.trim;
        int64_t& fr = is_begin ? t.begin_frame : t.end_frame;
        if (fr == v) { unchanged(); return true; }
        fr = v;
        if (active) {
            // The same commit tail trim gestures use, through their one owner
            // (GuiInputHandler::commit_trim_mutation, input_trim.cpp): the
            // crossed-commit reset first — a bound committed onto/across its
            // partner resets the pair to the song edges, and says so on that
            // owner's own card since 2026-08-30 — then the
            // waveform + status-chain repaints and the target-render trigger.
            // History-less, like all trim. The timestamp invalidate also rides
            // applied() below; raising it twice costs nothing — the damage list
            // coalesces by containment, so the identical rect is dropped.
            // THE TAIL ALSO PARKS THE PLAYHEAD at the committed trim start
            // (architect 2026-08-05 — every trim write does, the membership
            // stated at the head of input_trim.cpp). IT DOES NOT HIDE THE TRIM
            // REGION OVERLAY: the trim writes are that inventory's one excluded
            // class since 2026-08-18, the region being the trim itself. It
            // moves the cursor from inside a modal editor, which is accepted
            // for uniformity: a typed commit is a commit, and the editor lives
            // on the bottom row while the move is out on the waveform. The
            // INACTIVE-band arm below stays out of the rule — it writes a
            // PARKED pair, not the live window, and moves nothing visible.
            input->commit_trim_mutation();
            // THE SETTER'S DESELECT after the bound commit + auto_clear
            // (architect 2026-07-30). Past every refusal (the
            // wall range check, the unchanged early return), so a refused commit
            // deselects nothing.
            selection.clear_selection();
        } else if (!trim_is_full_window(t, total) &&
                   t.end_frame <= t.begin_frame) {
            // Inactive band: the load convention — a crossed/equal resulting
            // pair RESETS both bounds to the song edges, one stderr line. The
            // full-window recognition runs first so a one-frame source's
            // canonical [0, 0] is not read as crossed (the same precedence
            // auto_clear_crossed_trim and the render orchestrators use).
            t = full_trim_window(total);
            std::fprintf(stderr,
                "warptempo_gui: tab_%c trim crossed (end <= begin); both "
                "bounds reset to the song edges\n",
                (tab_char == 'B') ? 'b' : 'a');
        }
        applied(); return true;
    }

    // A tab_a_/tab_b_ prefix with an unrecognized suffix is not a GUI-kind
    // key; fall through so the engine path reports "unknown engine key".
    return false;
}

void GuiSettingsEditor::commit() {
    if (!text_editor::is_active(app.settings_editor)) return;
    const std::string& pending = app.settings_editor.pending;

    // Shape: split on the first `=`. The key is everything before it
    // (validated whitespace-free below, so it never contains `=`); the
    // value is everything after and may itself contain `=`, so a free-text
    // value such as a url= with a `?v=` query parameter or a notes= line
    // commits intact. This matches the settings schema reader
    // (read_settings_file), which also splits on the first `=`.
    const size_t eq = pending.find('=');
    // The shape's twin above (commit_gui_setting's own): one composed
    // sentence, the stderr line and the card its two readers.
    auto reject = [&](const char* reason) {
        app.settings_editor.red = true;
        viewport.invalidate_modal_dialog_area();
        const std::string refusal =
            std::string("Settings edit rejected: ") + reason;
        std::fprintf(stderr, "warptempo_gui: %s\n", refusal.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
    };
    if (eq == std::string::npos) { reject("missing '='"); return; }
    const std::string key   = trim_ws(pending.substr(0, eq));
    const std::string value = trim_ws(pending.substr(eq + 1));
    if (key.empty()) { reject("empty key"); return; }
    for (char c : key) {
        if (!is_key_char(c)) { reject("invalid character in key"); return; }
    }

    // 2. The three gesture-less device keys — one body, ahead of the routers
    //    because none of them has a chokepoint to route into (the head's
    //    item 1; the body's own comment carries the rest).
    if (commit_device_setting(key, value)) return;

    // 3a. GUI-kind keys. Every key that can appear in a `.settings` file is
    // settable here: the router parses strictly and applies through the key's
    // own gesture chokepoint (no parallel writer). It returns true when it has
    // fully handled the commit (applied + deactivated, or red-flashed); false
    // when `key` is not a GUI-kind key, so we fall through to the engine path.
    if (commit_gui_setting(key, value)) return;

    // 3b. Canonical engine-key write. Reject any key that is not in the
    // canonical engine set; validate the value through the same helper
    // the file-load deserializer uses. Capture-before-mutate so the
    // snapshot on the undo stack reflects the pre-edit settings.
    if (!is_canonical_engine_key(key)) {
        app.settings_editor.red = true;
        viewport.invalidate_modal_dialog_area();
        const std::string refusal =
            "Settings edit rejected: unknown engine key '" + key + "'";
        std::fprintf(stderr, "warptempo_gui: %s\n", refusal.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
        return;
    }

    // The lock, and this is the whole of it for this surface (architect
    // 2026-09-04, moving the decision off GuiSettingsEditor::open — the
    // account is there). An engine key IS the piece: read-only protects the
    // authored musical content, the two marker stores and the engine settings
    // (read_only_key_blocked, input_key_dispatch.cpp), so the four sidecar
    // dropdown rows and every typed engine key refuse here while the device
    // keys and the GUI-kind band above commit on a locked tab. It sits past
    // is_canonical_engine_key so an unknown key still hears that it is
    // unknown, and ahead of the value validator because the lock outranks the
    // grammar: a locked tab refuses the key whatever the value would have
    // been. Red flash plus card, the shape every refusal on this surface
    // wears, and the sentence is the lock's own (kTabReadOnlyCard,
    // notifications.h) because this site knows its act and needs no chord in
    // it.
    if (active_view_state(app).read_only) {
        app.settings_editor.red = true;
        viewport.invalidate_modal_dialog_area();
        std::fprintf(stderr, "warptempo_gui: %s\n", kTabReadOnlyCard);
        notifications.notify(AppState::NotificationClass::Normal,
                             kTabReadOnlyCard);
        return;
    }

    EngineSettings candidate = app.engine_settings;
    std::string reason;
    if (!validate_engine_setting(key, value, candidate, reason)) {
        app.settings_editor.red = true;
        viewport.invalidate_modal_dialog_area();
        const std::string refusal =
            "Settings edit rejected: key '" + key + "' has invalid value '" +
            value + "': " + reason;
        std::fprintf(stderr, "warptempo_gui: %s\n", refusal.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
        return;
    }

    // Source-clobber guard. An edit that makes the title-named render output
    // resolve to the source file itself would overwrite the source on the next
    // render, and the shared predicate composes and checks that path. What is
    // still reachable now that the deliverable lands one folder down, in the
    // project's `render/`, is stated at the predicate itself
    // (render_output_source_collision, render_output_naming.h): not a
    // spelling, but a `render/` or a `render/<title>.wav` that is linked onto
    // the source. Refused here so the colliding value never reaches
    // app.engine_settings.
    if (render_output_source_collision(candidate, app.source_audio_path)) {
        app.settings_editor.red = true;
        viewport.invalidate_modal_dialog_area();
        // The path names its BASENAME on the card, the stack's own rule
        // (messaging.md), and the stderr line has always named the same.
        // ONE CLAUSE AND NO INSTRUCTION AFTER IT (2026-09-01, the
        // capitalization sweep's sentence shape): the reason IS the message,
        // the grid-iterations cap card's own rule. It closed with "; choose a
        // different title" until that day.
        const std::string refusal =
            "Settings edit rejected: this would make the render output "
            "overwrite the source file (" +
            std::filesystem::path(app.source_audio_path).filename().string() +
            ")";
        std::fprintf(stderr, "warptempo_gui: %s\n", refusal.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
        return;
    }

    // No-op gate. An undo entry represents a state change, not a gesture, so
    // committing the value already in effect deactivates the editor and
    // touches no history, dirty state, view, or render. The key has passed
    // is_canonical_engine_key, so both serializations are engaged; comparing
    // canonical serialized bytes makes every accepted spelling of the current
    // value a no-op (for example a scale written with extra trailing zeros).
    std::optional<std::string> cur_serialized =
        format_engine_setting_value(app.engine_settings, key);
    std::optional<std::string> new_serialized =
        format_engine_setting_value(candidate, key);
    if (cur_serialized == new_serialized) {
        std::fprintf(stderr,
            "warptempo_gui: Setting unchanged: %s=%s\n",
            key.c_str(), cur_serialized->c_str());
        viewport.invalidate_modal_dialog_area();
        text_editor::deactivate(app.settings_editor);
        return;
    }

    // THE PLAYHEAD'S OWN MUSICAL INSTANT, in SOURCE frames and read while the
    // OLD map still stands — the subject of this commit's target-view re-land
    // below, and the delete's own two lines in the warp family's shape (the
    // contract at the head of warpmarkers_ops.cpp). The engine scale is a
    // warp-map input, so a commit that moves it re-warps the target domain
    // under a resting cursor; like the delete, this act leaves NO FOCUS to
    // follow (the selection clears below), so the cursor's own instant is what
    // has to survive the rewrite. active_domain_to_source_frame is the
    // product's one inverse for a bare frame — the identity in source view,
    // the memoized target map's inverse in target view — so this costs two
    // compares off home and is read unconditionally.
    const int64_t playhead_source_frame =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);

    SettingsSnapshot pre = capture_current_settings(app);
    app.engine_settings = std::move(candidate);
    undo.push_settings_undo(std::move(pre));

    std::fprintf(stderr,
        "warptempo_gui: Setting applied: %s=%s\n",
        key.c_str(), value.c_str());

    viewport.invalidate_modal_dialog_area();
    text_editor::deactivate(app.settings_editor);
    // (THE ENGINE COMMIT'S WHOLESALE OVERLAY HIDE IS DELETED, 2026-08-19, with
    // the call-site inventory it belonged to. THE OVERLAY HIDES WHEN THE
    // PLAYHEAD'S POSITION IN THE MUSIC CHANGES, WHEN A MARKER IS TOUCHED AND
    // WHEN THE SWEEP ENDS —
    // the rule at clear_region_highlight, input_handler.h — and a typed engine
    // key does none of the three. It stood here from 2026-07-29 on the argument that the
    // scale is a warp-map input, so the commit rebuilds the target map
    // underneath a span measured against the OLD map; that argument died on
    // 2026-08-18 when the region became the trim, the span being DERIVED from
    // source-domain trim bounds every frame, so the re-warp below simply
    // re-derives the overlay's columns in the new domain with nothing to
    // maintain and nothing stale to put away. The trim WINDOW itself was never
    // touched either way — the trim bar and its endcaps go on showing it.)
    // THE SELECTION STILL GOES (architect 2026-07-29): an engine
    // commit rebuilds the map under every marker INDEX and IMAGE at once, so no
    // marker keeps the identity a focus named — the same
    // "ready to move on" act the trim setters make when a trim-bar click deselects.
    // This is the SYMMETRIC half of a pair: the settings-only ('S') undo/redo
    // restore clears the selection at its own restore (undo.cpp), and GUI-kind
    // keys are
    // history-less, so no other settings entry kind exists to cover. Together they
    // are what let the never-span-less ENFORCEMENT be deleted: this site was one of
    // its two remaining producers, and closing it here means no collapse protocol
    // is needed rather than a collapse being owed. It touches no region: a
    // selection mutator writes no region state at all, and the overlay's
    // visibility is not a selection concern.
    selection.clear_selection();
    // Full-area damage for the teardown, and it is the site's own: clear_selection
    // damages only on a stem/overlay SUBJECT CHANGE, so an already-empty selection
    // raises nothing there, while this commit rebuilds the map under the whole
    // plate either way (the kick below). A settings commit is a rare, discrete
    // command, so it takes the standing "rare command, full damage" answer.
    viewport.invalidate_waveform_area();
    // The engine scale is a warp-map input (build_warp_frame_map's slope
    // product), so an engine commit that moved it re-warps the target-view
    // plate. This is one of the target-view re-warp sites (the full inventory
    // lives at Viewport::kick_waveform_sync; warp placement edits author in source
    // view only under the home-view binding), so re-warp synchronously in target
    // view so displayed == live at this command boundary. A non-scale engine key (provenance) leaves the plate
    // unchanged, so the sync is a redundant bounded rebuild there — acceptable,
    // matching the unconditional trigger beside it.
    // AND THE PLAYHEAD RE-LANDS ON ITS OWN INSTANT (architect 2026-09-02, the
    // four-tier review's R-17d, superseding the "NO RE-LAND, and none is
    // possible" this site carried from 2026-07-29). That reading was the
    // pre-2026-08-24 FOCUS argument: with the selection cleared above there is
    // no focus whose image could be the subject, so the site concluded there
    // was no subject at all. The DELETE had already answered it — it clears the
    // selection too and re-lands the PLAYHEAD'S OWN MUSICAL INSTANT, captured
    // in source frames before the rewrite (warpmarkers_ops.cpp) — and a whole-
    // map rewrite is exactly the delete's class: keeping the cursor's NUMBER
    // across it moves the cursor in the music, which is what the family's
    // re-land exists to prevent, and left an edit-then-undo landing the cursor
    // where neither the edit nor the undo put it. THROUGH THE RESEAT, never a
    // movement owner: an image moving out from under a resting cursor is a
    // TRANSLATION, so the trim region overlay must stand (the rule at
    // clear_region_highlight, input_handler.h). Source view needs nothing —
    // the identity domain, where the inverse above and this forward map are
    // both identity and the write is the value the cursor already holds.
    if (app.active_audio_view == 'T') {
        viewport.kick_waveform_sync();
        viewport.reseat_playhead_to(
            source_frame_to_active_domain(app, audio, playhead_source_frame));
    }
    // The trigger is unconditional by ruling — rationale recorded at
    // GuiTargetRender::trigger. Under the full-recipe key every engine-settings
    // commit moves the fingerprint, provenance (title/bpm/notes/url/cover)
    // included, so the target-view re-preview renders fresh rather than reusing
    // the prior buffer. Playback is not at stake at this site: the editor is
    // modal and its open already stopped playback.
    target_render.trigger();
}

// THE THREE DEVICE KEYS' COMMIT (the head's item 1). projects_repo has taken
// a direct-set arm here since it was a `.settings` key — free text, no undo
// history, no dirty tracking, the settings editor its sole authoring surface
// — and since 2026-08-27 it is a DEVICE preference: ONE user has ONE
// repository, so the projects home is the device config's (device_config.h)
// and Ctrl+S does not carry it. projects_path and sync_path JOINED IT
// 2026-09-02 (architect, the four-tier review's R-22 — the Settings dropdown
// carries all three as rows), which is what made the arm a body: ONE SHAPE
// FOR THE THREE. The key's own grammar owner in device_config.h decides —
// is_projects_repo, is_projects_path, is_sync_path, never a second spelling
// — and a refusal is the red flash and the card with the grammar's own
// reason, through the composer every other key uses. A value byte-equal to
// the live one is the consumed no-op every routed GUI-kind key takes (the
// empty value included) and writes nothing. On success the LIVE STRUCT takes
// the value (AppState::device_config, the loop's one — so the other keys
// travel as they stand, the ownership rule at write_device_config) and THIS
// COMMIT IS THE PERSIST: the config is written right here. A failed write is
// advisory — the live value stands for the session — and since 2026-09-02 it
// is SAID: the writer composes the two clauses at its one failure point
// (GuiFailure, failure.h) and this site prints the diagnostic and cards the
// display, the strictness ruling's shape for a press whose result nothing
// paints.
//
// WHEN EACH IS IN FORCE. `projects_repo`: at once — every reader reads
// `app.projects_repo`, the live field, whose source moved 2026-08-27 and whose
// readers did not (an empty value simply never matches any remote, which
// disables the GitHub recheck). `sync_path`: at once — the Synchronize act
// reads the live struct at each press (synchronize_to_external_storage,
// input_key_dispatch.cpp). `projects_path`: FOR THE NEXT OPEN PROJECT AND THE
// NEXT LAUNCH, the open project staying open — the picker lists the folders
// under the live struct's path and gui_main's reopen resolves the chosen name
// under that same live value (main.cpp), so File → Open project already
// works against the new folder, while the open project's own paths are
// absolute and untouched. The card says where the change applies, because
// nothing on screen changes at the commit. `last_project` still names a
// folder under the OLD path until the next successful open rewrites it; the
// next launch's fallback (startup_source, project_model.cpp) is load-lenient
// on a name that is gone and takes the first valid folder under the new path.
// THE ONE RECORDED EDGE: the picker's same-project no-op compares NAMES
// (open_project_commit), so a folder under the new path carrying the open
// project's own name reads as "already open" until a relaunch or a different
// project is opened first — a rename-by-hand case, accepted.
bool GuiSettingsEditor::commit_device_setting(const std::string& key,
                                              const std::string& value) {
    std::string* live    = nullptr;
    bool (*grammar)(const std::string&) = nullptr;
    const char* reason   = nullptr;
    if (key == "projects_repo") {
        live = &app.device_config->projects_repo;
        grammar = &is_projects_repo;
        reason  = kProjectsRepoGrammarReason;
    } else if (key == "projects_path") {
        live = &app.device_config->projects_path;
        grammar = &is_projects_path;
        reason  = kProjectsPathGrammarReason;
    } else if (key == "sync_path") {
        live = &app.device_config->sync_path;
        grammar = &is_sync_path;
        reason  = kSyncPathGrammarReason;
    } else {
        return false;
    }

    // ONE COMPOSER, TWO READERS — commit_gui_setting's own shape, the
    // `gui_scale` arm's sentence exactly (the reason after the tag and no key
    // name, the field on screen already showing which key): the refusal
    // sentence is built once and read by the stderr line and by the card, a
    // red field saying only THAT it refused.
    if (!grammar(value)) {
        app.settings_editor.red = true;
        viewport.invalidate_modal_dialog_area();
        const std::string refusal =
            std::string("Settings edit rejected: ") + reason;
        std::fprintf(stderr, "warptempo_gui: %s\n", refusal.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
        return true;
    }
    if (value == *live) {
        std::fprintf(stderr,
            "warptempo_gui: Setting unchanged: %s=%s\n",
            key.c_str(), value.c_str());
        viewport.invalidate_modal_dialog_area();
        text_editor::deactivate(app.settings_editor);
        return true;
    }
    *live = value;
    // The live field every reader of the repository reads (the rationale at
    // AppState::projects_repo); the two path keys have no field beside the
    // struct's own.
    if (key == "projects_repo") app.projects_repo = value;
    // The write is advisory in the ruling's sense: it may fail, the live value
    // stands either way, and the failure is the press's answer on a card. The
    // verdict is kept because the applies-card below is a claim about the next
    // launch, which only a written file can make true — a failed write leaves
    // the old path on disk, so the two cards together would have said the
    // persist failed and then that it survives a relaunch, with the second one
    // topmost. On failure the device-config card is the whole answer.
    const auto failure = write_device_config(*app.device_config);
    if (failure) {
        std::fprintf(stderr, "warptempo_gui: %s\n",
                     failure->diagnostic.c_str());
        notifications.notify(AppState::NotificationClass::Normal,
                             failure->display);
    }
    std::fprintf(stderr,
        "warptempo_gui: Setting applied: %s=%s\n",
        key.c_str(), value.c_str());
    viewport.invalidate_modal_dialog_area();
    text_editor::deactivate(app.settings_editor);
    if (key == "projects_path" && !failure) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kProjectsPathAppliesCard);
    }
    return true;
}

bool GuiSettingsEditor::complete_path_value(const std::string& value) {
    // Split at the last `/`: the HEAD names the directory to list (the
    // separator kept, so `/` alone lists the root), the TAIL is what a name
    // must start with. A value with no `/` at all has no head, and a relative
    // head completes nothing — the grammar's own rule (is_config_path_value:
    // both keys take absolute paths), so the completer never offers a value
    // the commit would refuse.
    const size_t slash = value.rfind('/');
    if (slash == std::string::npos) return false;
    const std::string head = value.substr(0, slash + 1);
    const std::string tail = value.substr(slash + 1);
    const std::filesystem::path dir(head);
    if (!dir.is_absolute()) return false;

    // LISTED AFRESH AT EVERY TAB, with an error_code: a directory that is not
    // there, not readable or not a directory completes nothing, and a Tab that
    // completes nothing walks the ring — the one autocomplete model.
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) return false;
    std::vector<std::string> matches;
    for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (ec) return false;
        const std::filesystem::directory_entry& e = *it;
        // DIRECTORIES ONLY — both keys name folders. Through the link: a
        // symlinked folder is a folder to name, and the grammar follows
        // nothing either way.
        std::error_code dec;
        if (!e.is_directory(dec) || dec) continue;
        const std::string name = e.path().filename().string();
        // Dot-directories are offered only when they are asked for, the
        // shell's own convention: nearly every home directory holds a
        // `.config` and a `.cache`, and with an empty tail those join the
        // match set and drag the common prefix to nothing, so the Tab that
        // most naturally asks "what is here" would advance by zero characters
        // and walk the focus ring instead. A tail beginning with `.` says the
        // hidden names are the ones wanted and lets them all through.
        if (!name.empty() && name[0] == '.' &&
            (tail.empty() || tail[0] != '.')) continue;
        if (name.size() < tail.size() ||
            name.compare(0, tail.size(), tail) != 0) continue;
        matches.push_back(name);
    }
    if (matches.empty()) return false;

    std::string lcp = common_prefix_on_codepoint_boundary(matches);
    // The tail is a prefix of every match, so it is a prefix of the common
    // prefix — unless the boundary cut took it below the tail, in which case
    // the matches diverge inside one character and there is nothing to add.
    if (lcp.size() < tail.size()) return false;
    std::string append = lcp.substr(tail.size());
    if (matches.size() == 1) append += '/';
    if (append.empty()) return false;

    // The append lands at the LINE'S END whatever the cursor was doing, through
    // the one incoming filter, as the recall does; its over-capacity refusal is
    // silent for the recall's reason (the text is the product's own, not the
    // user's), and answers false below because the buffer stood still.
    const std::string before = app.settings_editor.pending;
    app.settings_editor.cursor_pos       =
        static_cast<int>(app.settings_editor.pending.size());
    app.settings_editor.selection_anchor = -1;
    (void)text_editor::replace_selection(app.settings_editor, append);
    viewport.invalidate_modal_dialog_area();
    return app.settings_editor.pending != before;
}

// Returns whether the buffer CHANGED — the one autocomplete model's question
// (the rule is stated at route_modal_editor_key's Tab arm,
// input_key_dispatch.cpp): true consumes the Tab that ran it, false lets that
// Tab walk the modal's focus ring. Every refusal below answers false.
bool GuiSettingsEditor::autocomplete_value() {
    if (!text_editor::is_active(app.settings_editor)) return false;
    const std::string pending = app.settings_editor.pending;

    const size_t eq = pending.find('=');
    // No `key=` yet; nothing to complete.
    if (eq == std::string::npos) return false;

    const std::string key = trim_ws(pending.substr(0, eq));
    // Only fill an empty value side, so an in-progress value is never
    // overwritten. Whitespace-only counts as empty. THIS is what makes a
    // SECOND Tab walk with no state to remember the first: the completion just
    // written is a non-empty value side. THE TWO PATH KEYS ARE THE EXCEPTION
    // (2026-09-02): a non-empty value side there is a path PREFIX, and Tab
    // completes it against the filesystem — the recall below still answers
    // the empty side, so the dropdown's prefill and a bare `sync_path=` Tab
    // behave as every other key's do, and the path road opens only once
    // something has been typed. The path completer's own no-advance cases
    // (an ambiguous prefix already at the common prefix, no match, a relative
    // or unlistable head) are Tabs that walk, exactly as here.
    if (!trim_ws(pending.substr(eq + 1)).empty()) {
        if (!is_path_completed_key(key)) return false;
        // Leading whitespace off, trailing whitespace kept — the reasons are
        // at ltrim_ws. The completer used to be handed the raw substring, so a
        // `sync_path= /run/media` never completed although commit() trims and
        // accepts exactly that value.
        return complete_path_value(ltrim_ws(pending.substr(eq + 1)));
    }

    // Recall the current live value for ANY settable key. Engine keys read
    // through format_engine_setting_value; GUI-kind keys (view state,
    // follow, centered, center_on_next_marker, gui_scale,
    // waveform_magnification_level,
    // projects_repo, projects_path, sync_path — the last four the device
    // config's — per-tab trim / read_only)
    // read through recall_gui_setting_value — which produces byte-identical
    // output to what a Ctrl+S would write, so recall and save never diverge.
    // A trim bound recalls as its actual frame (`tab_a_trim_begin=0`).
    // Only a truly unknown key is not recallable.
    //
    // THE RECALL IS BYTE-EXACT WITH NO EXCEPTION (architect 2026-08-02, the
    // UTF-8 relaxation). The seed below lands through
    // text_editor::replace_selection, whose filter now passes well-formed UTF-8
    // verbatim — and the free-text provenance values (title/bpm/notes/url/cover)
    // are exactly what the strict load takes verbatim
    // (engine_settings_io.cpp), so a value carrying non-ASCII text recalls
    // unchanged and commits back unchanged. The one-way hole this note used to
    // record — a hand-edited non-ASCII value recalling stripped, and committing
    // that stripped line — is CLOSED for TEXT, which is the whole of what the
    // exception was ever about. What the filter still drops is ASCII control
    // bytes and malformed byte sequences: not text, authorable by nothing in
    // the product (the typed path encodes real codepoints and the file writer
    // writes what it held), and reachable only by hand-editing a value into a
    // state the GUI cannot express.
    std::optional<std::string> cur =
        format_engine_setting_value(app.engine_settings, key);
    if (!cur) cur = recall_gui_setting_value(app, key);
    if (!cur) return false;  // unknown key: nothing to recall

    // Rebuild as `<prefix>=<current value>`, cap-aware, cursor at end. The
    // prefix is the typed text up to and including the first `=`, kept
    // verbatim; replace_selection fills the value at the cursor.
    app.settings_editor.pending          = pending.substr(0, eq + 1);
    app.settings_editor.cursor_pos       =
        static_cast<int>(app.settings_editor.pending.size());
    app.settings_editor.selection_anchor = -1;
    // THE RECALL'S OWN OVER-CAPACITY REFUSAL IS SILENT, and it is the one
    // replace_selection caller that says nothing (2026-08-30). The strictness
    // ruling's two capacity sentences answer what the USER just supplied — a
    // typed character, a pasted clipboard — while this text is the product's
    // own recall of a value it already holds, so a field too short for it is a
    // cap that needs widening, not a refusal to explain. It leaves the red the
    // filter set, and Tab still walks (the buffer advanced, the prefix having
    // been rewritten above).
    (void)text_editor::replace_selection(app.settings_editor, *cur);

    viewport.invalidate_modal_dialog_area();
    // The literal answer, compared against the buffer this call started from:
    // recalling an EMPTY value (a blank free-text key such as `projects_repo=`
    // or `sync_path=` on a device with no destination, or an empty `title=`)
    // onto an already-bare `key=` writes the same bytes back and is honestly
    // no advance, so its Tab walks.
    return app.settings_editor.pending != pending;
}
