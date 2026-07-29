#include "settings_editor.h"

#include "input_handler.h"
#include "render_output_naming.h"
#include "settings_io.h"
#include "target_render.h"
#include "text_editor.h"
#include "undo.h"

#include "settings_file.h"     // warptempo_settings::validate_gui_setting

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace {

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

void GuiSettingsEditor::open() {
    if (text_editor::is_active(app.settings_editor)) return;
    text_editor::enter(app.settings_editor,
                       /*target=*/0,
                       /*locked_prefix=*/"",
                       /*initial_pending=*/"",
                       text_editor::Kind::SettingsAssignment);
    viewport.invalidate_timestamp_area();
}

void GuiSettingsEditor::exit_no_commit() {
    if (!text_editor::is_active(app.settings_editor)) return;
    viewport.invalidate_timestamp_area();
    text_editor::deactivate(app.settings_editor);
}

// GUI-kind key router. It hands (key, value) to the single grammar owner
// validate_gui_setting (src/parser/settings_file.cpp — the same check the load
// schema runs), red-flashes with the returned reason on any malformed or
// out-of-vocabulary value, and otherwise applies the typed value through the
// key's own gesture chokepoint — no parallel state writer, no grammar spelled
// twice. GUI-kind commits touch no undo history and no dirty state (launch/view
// state, like audio_player) — the four *_hash keys included: their branch
// assigns the live quartet directly and persists on the next ordinary save,
// marking nothing dirty; a same-value commit no-op-deactivates like the
// engine no-op gate. Playback is already stopped (the editor is modal), so the
// appliers need no playback special-casing. Returns false when `key` is not a
// GUI-kind key, so the caller falls through to the engine-key path.
bool GuiSettingsEditor::commit_gui_setting(const std::string& key,
                                           const std::string& value) {
    auto reject = [&](const std::string& reason) {
        app.settings_editor.red = true;
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: %s\n", reason.c_str());
    };
    auto applied = [&]() {
        std::fprintf(stderr,
            "warptempo_gui: setting applied: %s=%s\n",
            key.c_str(), value.c_str());
        viewport.invalidate_timestamp_area();
        text_editor::deactivate(app.settings_editor);
    };
    auto unchanged = [&]() {
        std::fprintf(stderr,
            "warptempo_gui: setting unchanged: %s=%s\n",
            key.c_str(), value.c_str());
        viewport.invalidate_timestamp_area();
        text_editor::deactivate(app.settings_editor);
    };

    // The shared grammar/vocabulary owner. std::nullopt: `key` is not a
    // GUI-kind key — fall through to the engine path. An error: malformed or
    // out-of-vocabulary value — red-flash with the returned reason. Otherwise
    // route the typed value through the key's gesture chokepoint below. The
    // editor's state-dependent refusals (read-only tab, trim walls) stay here.
    auto g = warptempo_settings::validate_gui_setting(key, value);
    if (!g) return false;
    if (!*g) { reject((*g).error()); return true; }
    const warptempo_settings::GuiSettingValue& gv = **g;

    // -- non-tab GUI keys ------------------------------------------------
    if (key == "playback_speed") {
        if (gv.f == app.playback_speed) { unchanged(); return true; }
        // Stores always; silent-inaudible in target view (the ruled behavior,
        // not a rejection). The one path that writes app.playback_speed.
        playback_lifecycle.set_playback_speed(gv.f);
        applied(); return true;
    }
    if (key == "follow") {
        if (gv.b == app.follow_mode) { unchanged(); return true; }
        playback_lifecycle.set_follow_mode(gv.b);
        applied(); return true;
    }
    if (key == "font_size") {
        if (gv.d == app.font_size) { unchanged(); return true; }
        input->apply_font_size(gv.d);
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
    if (key == "libm_hash" || key == "libmvec_hash" ||
        key == "fftw3_hash" || key == "fftw3_threads_hash") {
        // Render-environment attestation: a trivial history-less chokepoint
        // like audio_player's (assign directly, no gesture exists) — an
        // ordinary GUI-kind commit that marks NOTHING dirty. The stored hashes
        // are persisted identity that rides the next ordinary Ctrl+S; this
        // commit pushes no undo entry and touches no dirty state. The grammar
        // (exactly 16 lowercase hex digits) was already enforced by
        // validate_gui_setting above; applied() prints the one stderr line and
        // deactivates.
        std::string& stored =
            (key == "libm_hash")    ? app.libm_hash :
            (key == "libmvec_hash") ? app.libmvec_hash :
            (key == "fftw3_hash")   ? app.fftw3_hash :
                                      app.fftw3_threads_hash;
        if (gv.text == stored) { unchanged(); return true; }
        stored = gv.text;
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
            // A pre-`:` marker hover popup would repaint stale over the shifted
            // viewport; clear it, mirroring the scroll/pan gesture path. (The
            // sibling zoom and playhead_cursor arms need no clear here: their
            // chokepoints -- apply_zoom_change / move_playhead_to -- carry the
            // hover tail internally, exactly as their gestures do.)
            viewport.clear_hover_popup();
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
            // do, and it clears the SELECTION and the REGION the way they do
            // (the precedent and its rationale live at the Home/End arms in
            // input_key_dispatch.cpp: a flag left selected would go on claiming
            // to be the playhead at its own position, and the next bare arrow
            // would tow the playhead back onto the marker, silently discarding
            // the jump). Collapse is cheap; carrying a span across an arbitrary
            // jump is worth less than the confusion it buys. The INACTIVE arm
            // below writes the other tab's stored cursor, moves nothing live,
            // and stays out of this entirely.
            if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
                selection.clear_selection();
                viewport.invalidate_waveform_area();
            }
            clear_region_highlight(app, viewport);
            // The live chokepoint; its clamp owns out-of-range constructively.
            viewport.move_playhead_to(v);
        } else {
            if (v == band.playhead_cursor_sample) { unchanged(); return true; }
            band.playhead_cursor_sample = v;
        }
        applied(); return true;
    }
    if (suffix == "read_only") {
        // Navigation-class (allowed even while the tab is read-only). The
        // editor cannot OPEN in a read-only tab (its `:` opener drops at the
        // read-only key gate), so this is also the remote-unlock route for the
        // OTHER tab. read_only lives in the band for both tabs.
        if (gv.b == band.read_only) { unchanged(); return true; }
        band.read_only = gv.b;
        applied(); return true;
    }
    if (suffix == "trim_begin" || suffix == "trim_end") {
        const bool is_begin = (suffix == "trim_begin");
        // Trim is an authoring mutation: its gestures refuse in a read-only
        // tab, so mirror that here (viewport / zoom / playhead / read_only
        // above are navigation-class and stay allowed).
        if (band.read_only) {
            reject("tab is read-only; trim is not settable here"); return true;
        }
        // A `-1` value clears that bound (the unset file form); a blank value
        // fails the validator and red-flashes, so it never reaches here.
        if (gv.trim_unset) {
            TrimState& t = active ? app.trim : band.trim;
            bool& has = is_begin ? t.has_begin : t.has_end;
            if (!has) { unchanged(); return true; }
            int64_t& fr = is_begin ? t.begin_frame : t.end_frame;
            has = false;
            fr = 0;
            if (active) {
                viewport.invalidate_waveform_area();
                target_render.trigger();
                // MAINTAIN (never create) a TrimWindow highlight: an active-tab
                // bound unset is a trim author, so if a TrimWindow region is up,
                // re-sync it — the no-window arm clears it now that a bound is
                // gone, so x can't resurrect the pair from a stale highlight and
                // no later tempo re-sync hits the no-window arm. A text commit is
                // NOT a pointing gesture — it only maintains an existing trim
                // highlight, so a Free / SelectionExtent / no region is left
                // untouched (no creation).
                if (app.region.active &&
                    app.region.provenance == RegionProvenance::TrimWindow)
                    input->sync_highlight_to_trim_window();
            }
            applied(); return true;
        }
        const int64_t v = gv.i64;
        // Per-bound walls, exactly the load guard's compare: both bounds
        // 0..EOF-1, the unified inclusive [0, total-1] authored domain.
        const int64_t total = audio.total_frames();
        const int64_t wall = total - 1;
        if (v < 0 || v > wall) {
            reject("trim bound is past its wall"); return true;
        }
        TrimState& t = active ? app.trim : band.trim;
        bool& has = is_begin ? t.has_begin : t.has_end;
        int64_t& fr = is_begin ? t.begin_frame : t.end_frame;
        if (has && fr == v) { unchanged(); return true; }
        has = true;
        fr = v;
        if (active) {
            // The same commit tail trim gestures use, including
            // auto_clear_crossed_trim (a bound committed onto/across its
            // partner destroys both, silently). History-less, like all trim.
            input->auto_clear_crossed_trim();
            viewport.invalidate_waveform_area();
            target_render.trigger();
            // MAINTAIN (never create) a TrimWindow highlight after the bound
            // commit + auto_clear: re-sync an already-active TrimWindow region to
            // the NEW pair (or clear it if auto_clear dissolved the pair). A text
            // commit is not a pointing gesture — a Free / SelectionExtent / no
            // region is left untouched (no creation).
            if (app.region.active &&
                app.region.provenance == RegionProvenance::TrimWindow)
                input->sync_highlight_to_trim_window();
        } else if (t.has_begin && t.has_end && t.end_frame <= t.begin_frame) {
            // Inactive band: the load convention — a crossed/equal resulting
            // pair clears both bounds, one stderr line.
            t.has_begin = false;
            t.has_end = false;
            t.begin_frame = 0;
            t.end_frame = 0;
            std::fprintf(stderr,
                "warptempo_gui: tab_%c trim crossed (end <= begin); both "
                "bounds cleared\n", (tab_char == 'B') ? 'b' : 'a');
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
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: %s\n", reason);
    };
    if (eq == std::string::npos) { reject("missing '='"); return; }
    const std::string key   = trim_ws(pending.substr(0, eq));
    const std::string value = trim_ws(pending.substr(eq + 1));
    if (key.empty()) { reject("empty key"); return; }
    for (char c : key) {
        if (!is_key_char(c)) { reject("invalid character in key"); return; }
    }

    // audio_player is the one GUI-kind key with no dedicated gesture at all —
    // a free-text launcher path, so the settings editor is its sole authoring
    // surface. Handled here rather than through commit_gui_setting (which
    // routes every OTHER GUI key into its gesture chokepoint). Set it directly
    // (an empty value means no external player — the writer always emits the
    // line as `audio_player=`, which re-loads as no-player). A launch
    // preference like font_size: no undo history, no dirty tracking, silently
    // persisted on Ctrl+S. A same-value commit no-op-deactivates like every
    // routed GUI-kind key (the empty value included).
    if (key == "audio_player") {
        if (value == app.audio_player) {
            std::fprintf(stderr,
                "warptempo_gui: setting unchanged: %s=%s\n",
                key.c_str(), value.c_str());
            viewport.invalidate_timestamp_area();
            text_editor::deactivate(app.settings_editor);
            return;
        }
        app.audio_player = value;
        std::fprintf(stderr, "warptempo_gui: audio_player set: '%s'\n",
            value.c_str());
        viewport.invalidate_timestamp_area();
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
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: unknown engine key "
            "'%s'\n", key.c_str());
        return;
    }

    EngineSettings candidate = app.engine_settings;
    std::string reason;
    if (!validate_engine_setting(key, value, candidate, reason)) {
        app.settings_editor.red = true;
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: key '%s' has invalid "
            "value '%s': %s\n",
            key.c_str(), value.c_str(), reason.c_str());
        return;
    }

    // Source-clobber guard. The single-render wav output lands beside the
    // source, named by title (render_output_stem); an edit that makes the
    // output path resolve to the source file itself would overwrite the
    // source on the next Ctrl+Alt+R. The shared predicate composes and checks
    // that path. Refuse it here so the colliding value never reaches
    // app.engine_settings.
    if (render_output_source_collision(candidate, app.source_audio_path)) {
        app.settings_editor.red = true;
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: this would make the "
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
            "warptempo_gui: setting unchanged: %s=%s\n",
            key.c_str(), cur_serialized->c_str());
        viewport.invalidate_timestamp_area();
        text_editor::deactivate(app.settings_editor);
        return;
    }

    SettingsSnapshot pre = capture_current_settings(app);
    app.engine_settings = std::move(candidate);
    undo.push_settings_undo(std::move(pre));

    std::fprintf(stderr,
        "warptempo_gui: setting applied: %s=%s\n",
        key.c_str(), value.c_str());

    viewport.invalidate_timestamp_area();
    text_editor::deactivate(app.settings_editor);
    // WHOLESALE REGION CLEAR at the engine-commit chokepoint (architect
    // 2026-07-29, the maximally greedy collapse): the scale is a warp-map input,
    // so this commit rebuilds the target map underneath any resting highlight,
    // and a span measured against the old map is exactly the stale assertion the
    // two-forms model collapses on sight. This tail is the ONE committed path for
    // every canonical engine key — the GUI-kind keys returned through
    // commit_gui_setting far above (the TRIM keys among them, which re-sync a
    // TrimWindow highlight to the edited bounds and are maintainers by ruling,
    // untouched here), and the unknown-key / invalid-value / source-collision /
    // unchanged arms all returned before this point — so one clear here covers
    // the map change with no arm left uncovered. It is UNCONDITIONAL for the same
    // reason the kick below is unconditional in target view: a provenance key
    // (title/bpm/notes/url/cover) moves no image and a SOURCE-view commit changes
    // no display domain at all, so the clear is greed rather than repair there,
    // and one rule beats a second view gate to maintain. The trim WINDOW itself
    // is untouched — a chip-row re-click brings its highlight back. The helper
    // owns its own waveform damage, which the source-view path would otherwise
    // not raise. A 2+ selection standing beside the cleared span collapses to its
    // FOCUS with it (the never-rest-2+-without-a-span invariant, stated at
    // clear_region_highlight's declaration): the commit re-derives no span, and
    // the focus flag is where the playhead already rests.
    clear_region_highlight(app, viewport);
    if (app.selected_markers.size() >= 2) selection.collapse_to_focused();
    // Full-area damage for the collapse. collapse_to_focused raises its own
    // NARROW stem/overlay damage, computed against the basis live at the call —
    // and this commit is about to rebuild the map under it (the kick below), so
    // that basis is the wrong one to trust here. A settings commit is a rare,
    // discrete command, so it takes the standing "rare command, full damage"
    // answer rather than a clever narrow recompute.
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
    // The trigger is unconditional by ruling — rationale recorded at
    // GuiTargetRender::trigger. Under the full-recipe key every engine-settings
    // commit moves the fingerprint, provenance (title/bpm/notes/url/cover)
    // included, so the target-view re-preview renders fresh rather than reusing
    // the prior buffer. Playback is not at stake at this site: the editor is
    // modal and its open already stopped playback.
    target_render.trigger();
}

void GuiSettingsEditor::autocomplete_value() {
    if (!text_editor::is_active(app.settings_editor)) return;
    const std::string pending = app.settings_editor.pending;

    const size_t eq = pending.find('=');
    if (eq == std::string::npos) return;  // no `key=` yet; nothing to complete

    // Only fill an empty value side, so an in-progress value is never
    // overwritten. Whitespace-only counts as empty.
    if (!trim_ws(pending.substr(eq + 1)).empty()) return;

    const std::string key = trim_ws(pending.substr(0, eq));
    // Recall the current live value for ANY settable key. Engine keys read
    // through format_engine_setting_value; GUI-kind keys (view state,
    // playback_speed, follow, font_size, audio_player, per-tab trim / read_only)
    // read through recall_gui_setting_value — which produces byte-identical
    // output to what a Ctrl+S would write, so recall and save never diverge.
    // An unset optional trim recalls as `-1` (`tab_a_trim_begin=-1`).
    // Only a truly unknown key is not recallable.
    std::optional<std::string> cur =
        format_engine_setting_value(app.engine_settings, key);
    if (!cur) cur = recall_gui_setting_value(app, key);
    if (!cur) return;  // unknown key: nothing to recall

    // Rebuild as `<prefix>=<current value>`, cap-aware, cursor at end. The
    // prefix is the typed text up to and including the first `=`, kept
    // verbatim; replace_selection fills the value at the cursor.
    app.settings_editor.pending          = pending.substr(0, eq + 1);
    app.settings_editor.cursor_pos       =
        static_cast<int>(app.settings_editor.pending.size());
    app.settings_editor.selection_anchor = -1;
    text_editor::replace_selection(app.settings_editor, *cur);

    viewport.invalidate_timestamp_area();
}
