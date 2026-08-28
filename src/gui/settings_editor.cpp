#include "settings_editor.h"

#include "input_handler.h"
#include "render_output_naming.h"
#include "device_config.h"
#include "settings_io.h"
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
#include <utility>

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

bool is_key_char(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_';
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
    // IT CARRIES NO GATE OF ITS OWN and needs none: it delegates to open()
    // WHOLE, so the read-only refusal (and the modal stop past it) are that one
    // owner's, and the is_active test on the next line is what turns a refused
    // open into this route's silent return — the seed never runs, so a locked
    // tab's menu click changes nothing at all.
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

// THE ONE OPENER, and the ONE read-only decision for this whole surface.
//
// THE EDITOR IS DISABLED ON A READ-ONLY ACTIVE TAB (architect 2026-08-07): the
// surface authors the ENGINE SETTINGS, which the lock protects by the ruling's
// own vocabulary (read_only_key_blocked, input_key_dispatch.cpp — read-only
// protects the authored musical content, the two marker stores and the engine
// settings). The gate belongs HERE because this is the single chokepoint every
// open passes: the bare `;` key (which the keyboard allowlist already refuses
// one level up, and keeps refusing — this is not its defense) and the SETTINGS
// DROPDOWN's six item clicks, which call open_prefilled and reach no other gate
// at all. That second route was the hole: the menu opened the editor from a
// locked tab and every engine key was then settable in it.
//
// IT IS A SILENT CONSUMED NO-OP, NOT A GREYED MENU ITEM, and that is a ruling
// rather than an omission: the dropdown items' NEVER-GREY rule is the
// architect's own standing one (they dispatch and their commands' own refusals
// answer), so a face here would contradict it. A silent refusal is the
// product's ordinary shape for a gesture that cannot act.
//
// WHAT THIS MAKES UNREACHABLE, recorded because it is a live rule and not a
// dead one: the typed `tab_X_trim_*=` commits lost their read-only refusal the
// same day (trim is band, not authored content) — on a locked ACTIVE tab that
// relaxation is now unreachable, the SURFACE being gone rather than the rule.
// The arm's deleted guard stays deleted: this opener is the one owner, and a
// second check there would be both unreachable and wrong. Typing
// `tab_B_trim_begin=` from an UNLOCKED active tab while B is locked stays legal
// and is the case the relaxation still serves.
void GuiSettingsEditor::open() {
    if (active_view_state(app).read_only) return;
    if (text_editor::is_active(app.settings_editor)) return;
    // THE MODAL PLAYBACK STOP IS THE OPENER'S, past every refusal above — the
    // open_load_editor precedent exactly ("playback halts only when the modal
    // actually opens, so a refused open leaves a listening session
    // undisturbed"). It moved here from the two call sites with the gate: a
    // caller-side stop would have made the dropdown's refusal kill a live
    // audition and open nothing, which is not the consumed no-op this is.
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
// schema runs), red-flashes with the returned reason on any malformed or
// out-of-vocabulary value, and otherwise applies the typed value through the
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
    auto reject = [&](const std::string& reason) {
        app.settings_editor.red = true;
        viewport.invalidate_modal_dialog_area();
        std::fprintf(stderr,
            "warptempo_gui: Settings edit rejected: %s\n", reason.c_str());
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
    // move. (audio_player and projects_repo, the other two editable device
    // keys, have no vocabulary at all and take their free-text arms in
    // commit(), exactly as they did when all three were `.settings` keys.)
    if (key == "gui_scale") {
        int64_t v64 = 0;
        if (!parse_authored_frame(value, v64) || !is_gui_scale_percent(v64)) {
            reject("must be an integer in [50, 400] in canonical spelling");
            return true;
        }
        // History-less, and APPLIED LIVE: the store write is no longer the
        // whole commit — since
        // 2026-07-31 the redesigned rows size on this value, so the commit
        // pushes it to the renderer and re-lays-out immediately (apply_gui_scale
        // runs the resize-path rebuild), and a `:gui_scale=200` is visible
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
    if (key == "waveform_magnification_level") {
        // History-less and APPLIED LIVE, through the SAME chokepoint the three
        // hotkeys and the three icon-row buttons use — a typed
        // `:waveform_magnification_level=4` is one more caller of
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
            band.zoom_level = v;
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
        // IT IS THE REMOTE ROUTE ONLY: there is no SELF-unlock through this
        // surface, because no route raises the editor on a locked ACTIVE tab at
        // all — bare `;` is off the read-only allowlist one level up, and the
        // settings dropdown's item clicks go through open_prefilled, which
        // delegates to open() whole and takes its refusal (the reasoning lives
        // at that opener). So this arm is reached by typing
        // `tab_X_read_only=false` from an UNLOCKED active tab about the other
        // one; a locked ACTIVE tab is unlocked by bare `o` or the icon row's
        // read-only toggle, which is that key's button.
        if (gv.b == band.read_only) { unchanged(); return true; }
        band.read_only = gv.b;
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
            // partner resets the pair to the song edges, silently — then the
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
    auto reject = [&](const char* reason) {
        app.settings_editor.red = true;
        viewport.invalidate_modal_dialog_area();
        std::fprintf(stderr,
            "warptempo_gui: Settings edit rejected: %s\n", reason);
    };
    if (eq == std::string::npos) { reject("missing '='"); return; }
    const std::string key   = trim_ws(pending.substr(0, eq));
    const std::string value = trim_ws(pending.substr(eq + 1));
    if (key.empty()) { reject("empty key"); return; }
    for (char c : key) {
        if (!is_key_char(c)) { reject("invalid character in key"); return; }
    }

    // audio_player is the one preference with no dedicated gesture at all —
    // a free-text launcher path, so the settings editor is its sole authoring
    // surface. Handled here rather than through commit_gui_setting (which
    // routes every OTHER GUI key into its gesture chokepoint). Set it directly
    // (an empty value means no external player — the device config always emits
    // the line as `audio_player=`, which re-loads as no-player). A launch
    // preference: no undo history, no dirty tracking. IT IS A DEVICE
    // PREFERENCE since 2026-08-27, not a sidecar key, so Ctrl+S does not carry
    // it: THIS COMMIT IS ITS PERSIST — the device config is written right here,
    // which is what makes the arm the whole act. A same-value commit
    // no-op-deactivates like every routed GUI-kind key (the empty value
    // included) and writes nothing.
    // projects_repo is audio_player's twin in shape — free text, no gesture, no
    // undo history, no dirty tracking — so it takes the same direct-set arm
    // rather than a chokepoint route that does not exist for it, and since
    // 2026-08-27 its twin in provenance too: ONE user has ONE repository, so
    // the projects home is the DEVICE config's (device_config.h), Ctrl+S does
    // not carry it, and THIS COMMIT IS ITS PERSIST — the config is written
    // right here through the live struct, exactly as the audio_player arm
    // below writes it. An empty value simply never matches any remote, which
    // disables the GitHub recheck. A same-value commit no-op-deactivates and
    // writes nothing.
    if (key == "projects_repo") {
        if (value == app.projects_repo) {
            std::fprintf(stderr,
                "warptempo_gui: Setting unchanged: %s=%s\n",
                key.c_str(), value.c_str());
            viewport.invalidate_modal_dialog_area();
            text_editor::deactivate(app.settings_editor);
            return;
        }
        app.projects_repo = value;
        app.device_config->projects_repo = value;
        (void)write_device_config(*app.device_config);
        std::fprintf(stderr, "warptempo_gui: projects_repo set: '%s'\n",
            value.c_str());
        viewport.invalidate_modal_dialog_area();
        text_editor::deactivate(app.settings_editor);
        return;
    }

    if (key == "audio_player") {
        if (value == app.audio_player) {
            std::fprintf(stderr,
                "warptempo_gui: Setting unchanged: %s=%s\n",
                key.c_str(), value.c_str());
            viewport.invalidate_modal_dialog_area();
            text_editor::deactivate(app.settings_editor);
            return;
        }
        app.audio_player = value;
        // THE PERSIST IS THE COMMIT (2026-08-27): the value's home is the
        // device config, and nothing else writes it — no Ctrl+S, no load. A
        // failed write is advisory and prints its own line there; the live
        // value stands for this session either way. It writes THE LIVE STRUCT
        // (AppState::device_config, the loop's one), so the other four keys
        // travel as they stand (the ownership rule is at write_device_config).
        app.device_config->audio_player = value;
        (void)write_device_config(*app.device_config);
        std::fprintf(stderr, "warptempo_gui: audio_player set: '%s'\n",
            value.c_str());
        viewport.invalidate_modal_dialog_area();
        text_editor::deactivate(app.settings_editor);
        return;
    }

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
        std::fprintf(stderr,
            "warptempo_gui: Settings edit rejected: unknown engine key "
            "'%s'\n", key.c_str());
        return;
    }

    EngineSettings candidate = app.engine_settings;
    std::string reason;
    if (!validate_engine_setting(key, value, candidate, reason)) {
        app.settings_editor.red = true;
        viewport.invalidate_modal_dialog_area();
        std::fprintf(stderr,
            "warptempo_gui: Settings edit rejected: key '%s' has invalid "
            "value '%s': %s\n",
            key.c_str(), value.c_str(), reason.c_str());
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
        std::fprintf(stderr,
            "warptempo_gui: Settings edit rejected: this would make the "
            "render output overwrite the source file (%s); choose a "
            "different title\n",
            std::filesystem::path(app.source_audio_path)
                .filename().string().c_str());
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
    if (app.active_audio_view == 'T') viewport.kick_waveform_sync();
    // NO RE-LAND, and none is possible: the map-change re-land that used to sit
    // here (target view only, onto a surviving selection's focus, because the
    // re-warp moved that focus's image out from under the cursor) died with the
    // selection clear above — architect 2026-07-29. There is no lane and
    // no focus after this commit, so the resting cursor is the whole playhead and
    // it stays exactly where the user left it. The 'S' undo/redo restore's twin
    // re-land died the same way (undo.cpp).
    // The trigger is unconditional by ruling — rationale recorded at
    // GuiTargetRender::trigger. Under the full-recipe key every engine-settings
    // commit moves the fingerprint, provenance (title/bpm/notes/url/cover)
    // included, so the target-view re-preview renders fresh rather than reusing
    // the prior buffer. Playback is not at stake at this site: the editor is
    // modal and its open already stopped playback.
    target_render.trigger();
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

    // Only fill an empty value side, so an in-progress value is never
    // overwritten. Whitespace-only counts as empty. THIS is what makes a
    // SECOND Tab walk with no state to remember the first: the completion just
    // written is a non-empty value side.
    if (!trim_ws(pending.substr(eq + 1)).empty()) return false;

    const std::string key = trim_ws(pending.substr(0, eq));
    // Recall the current live value for ANY settable key. Engine keys read
    // through format_engine_setting_value; GUI-kind keys (view state,
    // follow, gui_scale, waveform_magnification_level,
    // audio_player, projects_repo — the last three the device config's —
    // per-tab trim / read_only)
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
    text_editor::replace_selection(app.settings_editor, *cur);

    viewport.invalidate_modal_dialog_area();
    // The literal answer, compared against the buffer this call started from:
    // recalling an EMPTY value (a blank free-text key such as `audio_player=`)
    // onto an already-bare `key=` writes the same bytes back and is honestly no
    // advance, so its Tab walks.
    return app.settings_editor.pending != pending;
}
