#include "render_view.h"

#include "marker_store_validate.h"
#include "phaseresetmarkers.h"
#include "render_cache.h"
#include "settings_io.h"
#include "target_render.h"
#include "trimmer.h"
#include "warp_frame_map_build.h"
#include "warpmarkers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// Render-view cluster: batch-folder enumeration, entry load/refresh, the
// strict entry .settings read (validated, never rewritten — the entry's
// sidecars are frozen at dispatch), and the entry-audio
// decode/bind around render-view sessions. Reaches viewport,
// active_views, and selection through the struct's reference members
// (clamp_viewport_start and compute_trim_samples are free functions
// declared in app_state.h). The source GuiAudio object `audio` is ALWAYS the
// source (see the invariant at GuiRenderView's head comment); the displayed
// entry decodes into its own view-owned GuiAudio (app.render_view.entry_audio)
// through the standard peaks pipeline, and the plate paints from its samples.

// Enumerate the flat render-view list under <source_parent>/renders/.
// Returns an empty vector if no source path is set or if the renders root
// contains no valid entries.
std::vector<AppState::RenderViewEntry>
GuiRenderView::enumerate_render_view_list() {
    std::vector<AppState::RenderViewEntry> out;
    if (app.source_audio_path.empty()) return out;
    std::filesystem::path src(app.source_audio_path);
    std::filesystem::path src_parent = src.parent_path();
    if (src_parent.empty()) src_parent = std::filesystem::path(".");
    const std::filesystem::path renders_root = src_parent / "renders";
    std::error_code ec;
    if (!std::filesystem::is_directory(renders_root, ec)) return out;

    auto leading_int = [](const std::string& s, size_t& end_out) -> int {
        int v = 0;
        size_t i = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + (s[i] - '0');
            ++i;
        }
        end_out = i;
        return v;
    };

    struct BatchSlot { int idx; std::filesystem::path path; };
    std::vector<BatchSlot> batches;
    for (const auto& de :
         std::filesystem::directory_iterator(renders_root, ec)) {
        if (!de.is_directory()) continue;
        const std::string name = de.path().filename().string();
        size_t end = 0;
        const int v = leading_int(name, end);
        if (end == 0 || end >= name.size() || name[end] != '_') continue;
        batches.push_back({v, de.path()});
    }
    std::sort(batches.begin(), batches.end(),
              [](const BatchSlot& a, const BatchSlot& b) {
                  return a.idx < b.idx;
              });

    for (const auto& b : batches) {
        struct WavSlot {
            int idx;
            std::filesystem::path path;
            std::string basename;
        };
        std::vector<WavSlot> wavs;
        for (const auto& fe :
             std::filesystem::directory_iterator(b.path, ec)) {
            if (!fe.is_regular_file()) continue;
            if (fe.path().extension() != ".wav") continue;
            const std::string stem = fe.path().stem().string();
            size_t end = 0;
            const int v = leading_int(stem, end);
            if (end == 0) continue;
            if (end != stem.size() && stem[end] != '_') continue;
            wavs.push_back({v, fe.path(), stem});
        }
        std::sort(wavs.begin(), wavs.end(),
                  [](const WavSlot& a, const WavSlot& b) {
                      return a.idx < b.idx;
                  });
        for (auto& w : wavs) {
            AppState::RenderViewEntry e;
            e.batch_folder = b.path;
            e.basename     = std::move(w.basename);
            e.wav_path     = std::move(w.path);
            out.push_back(std::move(e));
        }
    }
    return out;
}

// -- <basename>.settings snapshot --------------------------------------
//
// A render entry's `.settings` snapshot is frozen at dispatch: the dispatch
// writer writes it once (beside the wav, seeding the queue/dispatch-moment
// tab/zoom/viewport/playhead/W-P) and NOTHING in render view ever touches it
// again. Render view is an audio player with no per-entry memory: the entry
// loader validates the file but does NOT apply its browse position — every
// display resets to fit-file/0/0 with an empty selection (load_render_view_at),
// and the user may zoom/scroll/click while auditioning, with the next display
// resetting again. The only transfer out of render view is Ctrl+Alt+C, which
// inherits the frozen file's position (the queue-moment position, not the
// browsed one).

std::filesystem::path GuiRenderView::settings_path(
        const AppState::RenderViewEntry& e) {
    return e.batch_folder / (e.basename + ".settings");
}

// Loads the render at app.render_view.list[index] into the view-owned
// entry audio — the GuiAudio object `audio` stays the source (the invariant
// at the struct's head comment). Entries are program-written snapshots, read
// STRICTLY: <basename>.warpmarkers / <basename>.phaseresetmarkers through
// the standard store loaders and <basename>.settings through
// read_settings_file. Any refusal — a decode failure, a read failure, a
// past-EOF wall defect, a snapshot whose map cannot rebuild, an
// out-of-domain persisted view position, an entry wav whose length
// contradicts the snapshot's rendered window, or a fingerprint mismatch —
// is adversarial: one stderr line, first error only, the entry refuses to
// display (return false, prior state preserved).
//
// The architect's ruling: render view displays the RENDERED ARTIFACT — the
// entry wav's own timeline, which IS the trimmed window when the recipe was
// trimmed. The waveform pixels come from the entry audio's own samples, and
// playback binds that buffer at domain offset 0. Markers paint at their
// positions on the WINDOW axis — the authored frame forward-mapped through
// the target-shifted snapshot map (built below), minus the window origin,
// which the shift folds in — and a marker whose image falls outside the
// rendered window is absent, like the audio it annotates. The display stores
// adopt the snapshot vectors WHOLESALE (disabled markers and label defs
// included; the membership rule culls out-of-window rows at each consumer).
// Stops playback before installing the entry buffer and rebinds the device
// to it.
//
// The entry's sidecar set is frozen at dispatch and never rewritten, so this
// loader never applies its browse position. Every display — render-view entry
// and entry-to-entry navigation alike — resets to a fixed fit-file zoom,
// viewport 0, playhead 0, and an empty selection (the per-display reset block
// below). The .settings is still strict-read and fully validated (the schema,
// the entry invariants, the fingerprint, the entry-length check), and its view
// keys are validated as schema integrity and as the position Ctrl+Alt+C will
// inherit — they are just no longer applied at browse time. The tab letter and
// W/P mode stay the authoring session's: the tab is frozen to the authoring
// session and W/P is global by ruling, so neither app.active_tab_view nor
// app.active_markers_view is touched here.
bool GuiRenderView::load_render_view_at(int index) {
    if (index < 0 ||
        index >= static_cast<int>(app.render_view.list.size())) {
        return false;
    }
    auto& e = app.render_view.list[index];
    // Decode the entry wav through the STANDARD peaks pipeline: a fresh
    // GuiAudio::load (decode + `<basename>.peaks` sidecar read beside the wav,
    // pyramid rebuild and sidecar write on a miss / stale / corrupt cache).
    // The render plate paints from THIS audio's own samples — the rendered
    // artifact's own timeline — so an entry carries its own pyramid like any
    // source, and a first display writes the entry's .peaks. Entries are
    // short, so the progress callback is empty. A load failure is adversarial:
    // GuiAudio::load already printed its own owner diagnostic, and this adds
    // one render-view line; prior state is preserved (nothing mutated until
    // the install block below).
    auto entry_audio = std::make_shared<GuiAudio>();
    if (!entry_audio->load(e.wav_path.string(), {})) {
        std::fprintf(stderr,
            "warptempo_gui: render-view: failed to load %s\n",
            e.wav_path.string().c_str());
        return false;
    }
    // The engine emits the source's sample rate and channel count, so a
    // mismatch is adversarial (a foreign wav dropped into renders/): refuse
    // — the playback device stays init'd against the source's rate/channels
    // and a rebind must match them.
    if (entry_audio->sample_rate() != audio.sample_rate() ||
        entry_audio->channels() != audio.channels()) {
        std::fprintf(stderr,
            "warptempo_gui: render-view: '%s' rate/channels (%d Hz, %d ch) "
            "do not match the source (%d Hz, %d ch); refusing entry\n",
            e.wav_path.string().c_str(), entry_audio->sample_rate(),
            entry_audio->channels(), audio.sample_rate(), audio.channels());
        return false;
    }
    const int64_t decoded_frames = entry_audio->total_frames();

    // Strict snapshot reads. The source-domain marker pair is the same set
    // Ctrl+Alt+C commit reloads when promoting a render into authoring
    // memory; the .settings snapshot carries the entry's engine recipe,
    // recipe trim (on the commit tab), and the queue-moment view keys that
    // Ctrl+Alt+C inherits (validated here, never applied at browse time).
    const std::filesystem::path wm_path =
        e.batch_folder / (e.basename + ".warpmarkers");
    const std::filesystem::path pm_path =
        e.batch_folder / (e.basename + ".phaseresetmarkers");
    const std::filesystem::path st_path = this->settings_path(e);

    std::vector<GuiWarpMarker> snapshot_warp;
    {
        GuiWarpMarkers m;
        auto r = m.load(wm_path.string());
        if (!r) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: load failed for '%s': %s\n",
                wm_path.string().c_str(), r.error().c_str());
            return false;
        }
        snapshot_warp = m.markers();
    }
    std::vector<GuiPhaseResetMarker> snapshot_phase_resets;
    {
        GuiPhaseResetMarkers t;
        auto r = t.load(pm_path.string());
        if (!r) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: load failed for '%s': %s\n",
                pm_path.string().c_str(), r.error().c_str());
            return false;
        }
        snapshot_phase_resets = t.markers();
    }
    const auto settings = read_settings_file(st_path.string());
    if (!settings) {
        std::fprintf(stderr,
            "warptempo_gui: render-view: load failed for '%s': %s\n",
            st_path.string().c_str(), settings.error().c_str());
        return false;
    }
    // Entry-semantic invariants of the dispatch writer: it emits an entry
    // .settings only for wav renders and always pins active_audio_view=T (the
    // render display axis IS the target axis, and commit lands in 'T'). Both
    // are checked here rather than widened into the standard source settings
    // vocabulary — the schema legitimately loads active_audio_view=S and every
    // output_format for authoring sources. A hand-edited entry violating
    // either is GUI-unproducible and therefore adversarial: refuse.
    // active_audio_view=S would otherwise display fine (the key rides outside
    // the render fingerprint) while commit still lands in T, and a non-wav
    // output_format contradicts the entry's own wav artifact.
    if (settings->active_audio_view != 'T') {
        std::fprintf(stderr,
            "warptempo_gui: render-view: snapshot rejected for '%s': entry "
            "settings must carry active_audio_view=T\n",
            st_path.string().c_str());
        return false;
    }
    if (settings->engine.output_format != "wav") {
        std::fprintf(stderr,
            "warptempo_gui: render-view: snapshot rejected for '%s': entry "
            "output_format must be wav\n",
            st_path.string().c_str());
        return false;
    }
    // The commit tab (named by active_tab_view) carries the recipe trim
    // that shaped this render.
    const SettingsFileTab& commit_tab =
        (settings->active_tab_view == 'B') ? settings->tab_b
                                           : settings->tab_a;
    const SettingsTrim& recipe_trim = commit_tab.trim;

    // Guard domain: the source's totals. The snapshot markers and trim are
    // source-domain, and the audio object is invariantly the source in
    // every view, so `audio` is always the guard domain — there is no
    // parked domain to choose.
    const int64_t source_total_frames = audio.total_frames();
    const long source_sample_rate =
        static_cast<long>(audio.sample_rate());

    // Adversarial guards, the render-invalid-cannot-display ruling: a
    // snapshot that cannot rebuild its map cannot be displayed. Past-EOF
    // walls first, one call per candidate file so the message names the
    // offending sidecar (the call order — warp markers, phase resets, trim
    // — is the validator's own internal order, so the first offender
    // reported matches the single-call composite).
    {
        const std::vector<WarpMarker> cand_warp =
            slice_to_warp_markers(snapshot_warp);
        const std::vector<PhaseResetMarker> cand_resets =
            slice_to_phase_reset_markers(snapshot_phase_resets);
        if (auto detail = first_past_eof_wall_defect(
                cand_warp, {}, SettingsTrim{}, SettingsTrim{},
                source_total_frames, source_sample_rate)) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: past-EOF wall defect for "
                "'%s': %s\n",
                wm_path.string().c_str(), detail->c_str());
            return false;
        }
        if (auto detail = first_past_eof_wall_defect(
                {}, cand_resets, SettingsTrim{}, SettingsTrim{},
                source_total_frames, source_sample_rate)) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: past-EOF wall defect for "
                "'%s': %s\n",
                pm_path.string().c_str(), detail->c_str());
            return false;
        }
        if (auto detail = first_past_eof_wall_defect(
                {}, {}, settings->tab_a.trim, settings->tab_b.trim,
                source_total_frames, source_sample_rate)) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: past-EOF wall defect for "
                "'%s': %s\n",
                st_path.string().c_str(), detail->c_str());
            return false;
        }
    }

    // Raw-store walk, the same enumerator warp_render_preflight runs ahead of
    // the owner-level build at every live dispatch site. The GUI coincidence
    // window (kCoincidenceWindowSeconds) is deliberately wider than the map
    // owner's sub-frame refusal, so a hand-edited entry with enabled markers a
    // couple dozen frames apart is raw-invalid yet still buildable — without
    // this walk it would display. Every GUI-dispatched entry already passed
    // warp_render_preflight, so a raw-store defect here is hand fabrication:
    // an adversarial artifact boundary that hard-fails to stderr, first defect
    // only, and must not open the live defect-resolution series. The past-EOF
    // walls above already ran (the enumerator's contract assumes in-wall
    // stores).
    {
        std::vector<MarkerDefect> defects = enumerate_marker_store_defects(
            slice_to_warp_markers(snapshot_warp),
            slice_to_phase_reset_markers(snapshot_phase_resets),
            source_sample_rate);
        if (!defects.empty()) {
            // Name the file that actually failed: the defect carries its
            // column ('W' warp, 'P' phase reset), and dangling-ref /
            // pass-after-ref defects are warp-only by construction, so a 'P'
            // defect is always a stacked-reset coincidence group in the
            // .phaseresetmarkers file. The column field is authoritative.
            const std::filesystem::path& defect_path =
                defects.front().column == 'P' ? pm_path : wm_path;
            std::fprintf(stderr,
                "warptempo_gui: render-view: snapshot rejected for '%s': %s\n",
                defect_path.string().c_str(), defects.front().message.c_str());
            return false;
        }
    }

    // Rebuild the FULL map the render was derived from: the same
    // resolve-then-build every render path runs, against the source's
    // totals and the entry's engine scale.
    auto resolved_warp_markers = resolve_warp_markers_for_render(
        slice_to_warp_markers(snapshot_warp), source_sample_rate);
    if (!resolved_warp_markers) {
        std::fprintf(stderr,
            "warptempo_gui: render-view: snapshot rejected for '%s': %s\n",
            wm_path.string().c_str(), resolved_warp_markers.error().c_str());
        return false;
    }
    auto full_map_r = build_warp_frame_map(
        *resolved_warp_markers, settings->engine.scale,
        source_sample_rate, static_cast<long>(source_total_frames));
    if (!full_map_r) {
        std::fprintf(stderr,
            "warptempo_gui: render-view: map build failed for '%s': %s\n",
            wm_path.string().c_str(), full_map_r.error().c_str());
        return false;
    }
    std::vector<WarpFrameMapSegment> full_warp_frame_map =
        std::move(*full_map_r);

    if (recipe_trim.has_begin || recipe_trim.has_end) {
        auto tv = validate_trim_frames(
            recipe_trim.has_begin, recipe_trim.begin_frame,
            recipe_trim.has_end, recipe_trim.end_frame,
            source_total_frames, full_warp_frame_map);
        if (!tv) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: trim validation failed for "
                "'%s': %s\n",
                st_path.string().c_str(), tv.error().c_str());
            return false;
        }
    }

    // Persisted view keys are validated like a source load: an out-of-domain
    // viewport/playhead in an entry's .settings is adversarial, exactly as it
    // is at a source load (the GUI's own dispatch writer only writes in-domain
    // values). The keys are validated as SCHEMA INTEGRITY — adversarial-input
    // strictness over program-written bytes — and as the position Ctrl+Alt+C
    // will inherit from the frozen file; render view itself does NOT apply them
    // (every display resets to fit-file/0/0 below). Same shared rule the source
    // load and the CLI run (first_view_range_defect, marker_store_validate.h).
    // The AXIS these keys live on is the FULL target axis (the entry's
    // persisted active_audio_view is 'T' always, checked above), so the domain
    // total is the UNSHIFTED full map's target total through
    // target_total_frames_for_map — the same 'T' arm the source load uses; the
    // map just built (before the window shift below) IS that map, so the source
    // load's cannot-build skip arm has no analogue here. This is the
    // queue-moment position Ctrl+Alt+C inherits — NOT applied at browse time —
    // and is DELIBERATELY the full target axis, not the browsed window: the
    // display total (snapshot_display_total, the entry frame count) no longer
    // equals it. Both tabs are checked, as at source load.
    {
        const int64_t display_total = target_total_frames_for_map(
            source_total_frames, full_warp_frame_map);
        if (auto detail = first_view_range_defect(
                settings->tab_a, settings->tab_b, display_total)) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: view-range defect for '%s': "
                "%s\n",
                st_path.string().c_str(), detail->c_str());
            return false;
        }
    }

    // Display follows the snapshot, and an entry whose artifact does not
    // attest to its snapshot REFUSES to display. Renders are transient by
    // design — cheap to recreate, never committed as artifacts — and
    // Ctrl+Alt+C commit overwrites the project from an entry, so an entry
    // is displayed spotless or not at all: any attestation failure is one
    // stderr line and a refusal, no repair attempted — there is no repair
    // path, the user re-renders from the live project. The snapshot
    // fingerprint is the same render_fingerprint a dispatch of this exact
    // recipe computes: source path + the source `audio`'s load identity
    // (the audio object is invariantly the source), the source sample
    // rate, the snapshot stores, the entry's engine block, and the recipe
    // trim; the compare is the same fingerprint_sidecar_matches rung
    // do_render's reuse path runs (a missing .fingerprint is a mismatch).
    // Placed after the adversarial guards and before any state mutation
    // (playback stop / buffer install / store writes), so a refused entry
    // preserves prior state.
    {
        RenderFileIdentity source_identity;
        source_identity.size  = audio.source_load_size();
        source_identity.mtime = audio.source_load_mtime();
        std::vector<uint8_t> snapshot_fingerprint = render_fingerprint(
            app.source_audio_path, source_identity,
            static_cast<int>(source_sample_rate),
            snapshot_warp, snapshot_phase_resets, settings->engine,
            recipe_trim.has_begin, recipe_trim.begin_frame,
            recipe_trim.has_end,   recipe_trim.end_frame);
        if (!fingerprint_sidecar_matches(e.wav_path.string(),
                                         snapshot_fingerprint)) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: fingerprint mismatch for "
                "'%s'; refusing entry\n",
                e.wav_path.string().c_str());
            return false;
        }
    }

    // Rendered-window geometry on the FULL (unshifted) snapshot map's target
    // axis, exact doubles. T_b / T_e are the trim bounds' target images (unset
    // sides default to the whole timeline: 0 and the source total's image —
    // the map's final anchor target). T_b (llrint) is the window origin: the
    // entry wav's frame 0 in full-target coordinates, 0 untrimmed. It is now a
    // LOAD-TIME LOCAL only — it sizes the shift that folds the window origin
    // into snapshot_warp_frame_map (so every display consumer reads window-axis
    // positions directly) and drives the entry-length check below; playback
    // binds the entry buffer at offset 0, so there is no stored bind anchor.
    const double t_begin = recipe_trim.has_begin
        ? map_source_to_target(
              static_cast<double>(recipe_trim.begin_frame),
              full_warp_frame_map)
        : 0.0;
    const double t_end = map_source_to_target(
        recipe_trim.has_end
            ? static_cast<double>(recipe_trim.end_frame)
            : static_cast<double>(source_total_frames),
        full_warp_frame_map);
    const int64_t window_origin_frame = std::llrint(t_begin);

    // Entry-length verification: the delivered wav covers exactly the
    // rendered window — llrint(T_e) - llrint(T_b) frames, the post_trim
    // crop (untrimmed, the engine's full emission extent). A mismatch is
    // adversarial (a foreign wav dropped over the entry's name): refuse,
    // prior state preserved. This equality is what lets snapshot_display_total
    // be the entry frame count (decoded_frames) and still equal the window
    // span the shifted map spans.
    {
        const int64_t expected_frames =
            std::llrint(t_end) - window_origin_frame;
        if (decoded_frames != expected_frames) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: '%s' holds %lld frames but "
                "the snapshot's rendered window is %lld frames; refusing "
                "entry\n",
                e.wav_path.string().c_str(),
                static_cast<long long>(decoded_frames),
                static_cast<long long>(expected_frames));
            return false;
        }
    }

    // The entry is now fully attested and will install. Only at this commit
    // boundary leave live target playback: cancelling before the strict reads
    // made a refused entry destroy an otherwise valid in-flight target preview,
    // contradicting the prior-state-preserved failure contract above. No-op on
    // render-to-render navigation, where no target update can be in flight.
    target_render.cancel_in_flight_update();
    playback.stop();
    app.playhead_scanner_active = false;
    app.playhead_scanner_restore_pending = false;
    app.playhead_scanner_endpoint_painted = false;
    app.playhead_scanner_sample = 0;
    viewport.clear_hover_popup();

    // Stash the authoring POSITION on the FIRST entry of this render-view
    // session (no entry audio resident yet). refresh_active_tab_view_from_app
    // snapshots the live authoring viewport/zoom/playhead and selection into
    // the active tab's slot; restore_source_view restores it on exit. This is
    // one-way POSITION memory (enter stashes, exit restores) with no tab or
    // mode component — the tab stays the authoring session's throughout render
    // view and W/P is global by ruling, so neither is stashed. The source audio
    // object itself is untouched — it stays the source.
    if (!app.render_view.entry_audio) {
        active_views.refresh_active_tab_view_from_app();
    }
    // Install the entry audio. Hold the OUTGOING entry (the previously browsed
    // entry, on a render-to-render navigation) alive in a local until playback
    // has been stopped (above) and rebound (below): playback borrows the entry
    // samples raw, and a waveform worker job may hold a raw pointer into the
    // old entry too — the job's own shared_ptr keepalive covers the async case,
    // and this local covers the synchronous stop/rebind window. It drops at
    // function scope end, after kick_waveform_sync has drained the worker.
    std::shared_ptr<GuiAudio> outgoing_entry =
        std::move(app.render_view.entry_audio);
    app.render_view.entry_audio = entry_audio;
    // The displayed domain changed (a different entry's axis): bump the
    // audio identity counter so the waveform / stem / flag caches — all
    // keyed on audio_generation — invalidate. The bump keys the render-view
    // identity flip (the plate's audio source and map change), so no new
    // fingerprint field is needed.
    app.audio_generation++;

    // The display stores adopt the snapshot vectors WHOLESALE — no
    // keep-filtering here: every authored row rides in, disabled markers
    // included (label defs ride in the same vector, so effective_disabled
    // works on the snapshot). The out-of-window membership rule
    // (render_view_position_in_window) culls rows whose window-axis image
    // falls outside the entry wav at each display consumer.
    app.render_view.warp_markers           = std::move(snapshot_warp);
    app.render_view.phase_resets        = std::move(snapshot_phase_resets);
    // Snapshot display geometry for the Render display context: the
    // TARGET-SHIFTED map (window axis) this entry's authored positions
    // translate through, and the entry frame count as the display total. Shift
    // every segment's tgt_frame by -T_b so map_source_to_target yields
    // window-axis positions directly; the display total is the decoded entry
    // frame count (verified above to equal the window span), stored rather than
    // re-derived from the shifted map. Built once here, immutable while
    // displayed; cleared beside the display stores at every clear site.
    for (auto& seg : full_warp_frame_map) {
        seg.tgt_frame -= static_cast<double>(window_origin_frame);
    }
    app.render_view.snapshot_display_total  = decoded_frames;
    app.render_view.snapshot_warp_frame_map = std::move(full_warp_frame_map);
    // The entry's dispatch tab (settings->active_tab_view, frozen at dispatch),
    // stashed for the Ctrl+Alt+C routing re-attestation (the key rides outside
    // the render fingerprint). app.active_tab_view is NOT set to it — the tab
    // stays the authoring session's throughout render view — so this is a
    // separate stash, not a mirror of app.active_tab_view.
    app.render_view.snapshot_commit_tab = settings->active_tab_view;
    app.render_view.index             = index;
    app.render_view.last_path         = e.wav_path.string();

    // Per-display reset: the entry's sidecars are frozen at dispatch and never
    // rewritten, so render view keeps NO per-entry browse memory. Every display
    // — render-view entry and entry-to-entry navigation alike — lands at a
    // fixed fit-file zoom over the displayed domain, viewport 0, playhead 0,
    // and an empty selection. app.active_tab_view and app.active_markers_view
    // are deliberately NOT touched: the tab is frozen to the authoring session
    // and W/P is global by ruling. commit_tab (bound above) still supplies
    // snapshot_commit_tab (the recipe trim it also carries stays a load-time
    // local — it sized the window shift and the length check, and is not
    // stored); its view keys were validated at this load
    // (first_view_range_defect) as the position Ctrl+Alt+C inherits, but they
    // are not applied here. The engine block, playback_speed, follow,
    // font_size, active_audio_view are likewise the commit payload only,
    // adopted by Ctrl+Alt+C, never at browse time.
    app.zoom_level             = kFitFileLevel;
    app.viewport_start_sample  = 0;
    app.playhead_cursor_sample = 0;
    clamp_viewport_start(app, audio);
    app.selected_markers.clear();
    app.last_selected_marker = -1;

    // A freshly loaded entry starts with NO trim bound focused. Render view
    // has no trim display surface and no trim Tab stops, so a trim-bound
    // selection carried in from source view (on the first entry) must be
    // dropped here — otherwise it would sit as stale focus behind the read-only
    // display and be restored oddly. restore_source_view brings the authoring
    // trim focus back from the tab slot stashed at entry, so clearing the live
    // fields here is safe. Mirrors clear_selection's trim reset.
    app.trim_begin_selected = false;
    app.trim_end_selected   = false;
    app.last_selected_trim  = 0;
    app.last_sel_group      = LastSelGroup::Markers;

    // Split-playhead invariant (move_playhead_to): the scanner mirror
    // tracks the cursor while the scanner is inactive. It was forced
    // inactive and zeroed beside playback.stop above, so re-mirror it to
    // the applied entry playhead.
    if (!app.playhead_scanner_active) {
        app.playhead_scanner_sample = app.playhead_cursor_sample;
    }

    // Rebind playback to the entry buffer at domain offset 0: render view
    // displays the entry wav's OWN timeline, so its frame 0 is display
    // position 0 and the whole [0, entry frames) buffer is the playable
    // domain. The device stays init'd against the source's rate/channels from
    // file load — the entry's match those by construction (verified above), so
    // a rebind suffices; playback was stopped above, satisfying rebind_buffer's
    // contract.
    playback.rebind_buffer(app.render_view.entry_audio->samples_ptr(),
                           app.render_view.entry_audio->total_frames(), 0);
    // One-shot discrete jump: the loaded render swapped the entry buffer and
    // the viewport / zoom, so render the plate synchronously and publish the
    // displayed fingerprint now. Otherwise the markers / playhead repaint
    // immediately from the full-window invalidate below while the plate is left
    // to the next tick's worker, jumping the overlays a frame ahead of the
    // waveform on render-view entry and on render-to-render navigation. Covers
    // the R-key toggle-on, render navigation, and auto-open paths, which all
    // route through here.
    viewport.kick_waveform_sync();
    gui.invalidate_region(0, 0, app.width, app.height);
    return true;
}

// Render-view exit cleanup: restores the authoring position stashed at
// render-view entry (the active tab slot's viewport/zoom/playhead and
// selection), rebinds playback to the source samples, and frees the entry
// buffer. The tab letter and W/P mode are NOT part of the stash — the tab
// stayed the authoring session's throughout render view, and W/P is global,
// so `p` flips made while auditioning persist across the exit. The audio
// object never moved (it is invariantly the source), so there is no buffer
// to restore, only the view position and the playback bind. No-op when no
// entry buffer is resident (render view never displayed an entry).
void GuiRenderView::restore_source_view() {
    // Leaving render view: clear the flag BEFORE the synchronous waveform
    // kick at the end of this function. kick_waveform_sync resolves to
    // force_synchronous_waveform_rebuild, whose compute_waveform_render_inputs
    // derives is_target as (active_audio_view == 'T') && !render_view.enabled.
    // If the flag is still set when the kick runs, is_target is false, the
    // plate is rendered with no warp_frame_map (an unwarped source plate) and that
    // non-target fingerprint is published; the next on_redraw then sees the
    // inputs differ and rebuilds the real target plate on the async worker,
    // which is the visible flash on the render -> target transition. Clearing
    // here, ahead of the entry-buffer guard, makes every leave path
    // correct without each call site having to order the flag itself.
    app.render_view.enabled = false;
    if (!app.render_view.entry_audio) return;
    playback.stop();
    app.playhead_scanner_active = false;
    app.playhead_scanner_restore_pending = false;
    app.playhead_scanner_endpoint_painted = false;
    app.playhead_scanner_sample = 0;
    viewport.clear_hover_popup();

    // The displayed domain flips back to the source axis: bump the audio
    // identity counter so the waveform / stem / flag caches invalidate
    // (the bump used to ride the GuiAudio move-back; the object no longer
    // moves, so it is explicit here).
    app.audio_generation++;

    // Render view keeps no per-entry browse memory; exit restores the authoring
    // POSITION stashed at entry. The tab letter never changed (render view
    // blocks the A/B chords and never touches app.active_tab_view), and W/P is
    // global by ruling — a `p` flip made while auditioning stays flipped here —
    // so neither is restored: only the tab slot's position and the current
    // mode's selection come back. app.active_tab_view still names the authoring
    // tab, so the slot reads below name the authoring session directly.
    // Restore the authoring tab's slot position: viewport/zoom/playhead
    // (unclamped playhead — the restore-site convention; move_playhead_to owns
    // the value at first use), then the current-mode selection and the
    // trim-selection fields below. The tab slots were seeded at render-view
    // entry (refresh_active_tab_view_from_app) and are untouched while
    // browsing, so this returns the authoring view exactly as it was left.
    const ViewState& t = (app.active_tab_view == 'B') ? app.tab_b : app.tab_a;
    app.viewport_start_sample  = t.viewport_start_sample;
    app.zoom_level             = t.zoom_level;
    app.playhead_cursor_sample = t.playhead_cursor_sample;
    // Load the matching-mode slot into the
    // live pair. Live pair held render-view selection while
    // render-view was active; restoring the authoring view requires
    // pulling the authoring tab's matching-mode slot back in.
    if (app.active_markers_view == 'P') {
        app.selected_markers     = t.phase_reset_selected;
        app.last_selected_marker = t.phase_reset_last_selected;
    } else {
        app.selected_markers     = t.warp_selected;
        app.last_selected_marker = t.warp_last_selected;
    }
    // Restore the trim-bound selection from the same tab slot the markers came
    // from. Entry display cleared the live trim-selection fields because render
    // view has no trim surface; pulling back the snapshot saved at render-view
    // entry (refresh_active_tab_view_from_app) keeps the authoring view's focused
    // bound intact across the round trip, symmetric with the marker restore
    // above.
    // app.trim itself is untouched in read-only render view, so only the
    // selection fields are restored (mirroring switch_active_tab_view_to's
    // tab-restore of the same fields).
    app.trim_begin_selected = t.trim_begin_selected;
    app.trim_end_selected   = t.trim_end_selected;
    app.last_selected_trim  = t.last_selected_trim;
    app.last_sel_group      = t.last_sel_group;
    clamp_viewport_start(app, audio);
    selection.prune_live_selection();

    // Rebind playback to the source samples at domain offset 0 (the source
    // is its own domain origin), then release the entry audio — rebind first,
    // so the device is never left bound to a freed buffer. The entry audio was
    // held alive by app.render_view.entry_audio through the stop/rebind window;
    // a waveform worker job still rendering it keeps its own shared_ptr
    // keepalive, so this reset cannot dangle the worker (the kick_waveform_sync
    // tail below drains it). The device was init'd against the source's
    // rate/channels at file load; playback is stopped above, satisfying
    // rebind_buffer's contract.
    playback.rebind_buffer(audio.samples_ptr(), audio.total_frames(), 0);
    app.render_view.entry_audio.reset();
    // Render-view exit lands playback bound to source.wav unconditionally
    // above. If the user is still in target view (R does not touch
    // active_audio_view), rebind to the target buffer — ensure_ready
    // short-circuits to a clean playback.rebind_buffer when the buffer is
    // current, otherwise it dispatches a fresh preview. Leaving render view
    // stops anything render-related (the user-driven exits run
    // cancel_archival_session before this restore), so the preview
    // re-derives against an idle or draining worker.
    if (app.active_audio_view == 'T') {
        target_render.ensure_ready();
    }
    // One-shot discrete jump: the authoring view is restored and the restored
    // tab's viewport / zoom / playhead are applied, so render the plate
    // synchronously and publish the displayed fingerprint now. Covers the
    // R-key toggle-off and exit_render_view_and_clear paths, which route
    // through here. The plate is built from the source audio (or, when
    // active_audio_view is Target, source audio plus the live
    // warp_frame_map), independent of the target render buffer ensure_ready
    // rebinds above.
    viewport.kick_waveform_sync();
    gui.invalidate_region(0, 0, app.width, app.height);
}

// Clear exactly the snapshot-context fields of the RenderViewContext. See
// the declaration: one clear site for the snapshot state so a new snapshot
// field is cleared everywhere by adding a single line here, and the
// app_state.h promise that every snapshot field is cleared beside the others
// at every clear site holds because all four exits call this.
void GuiRenderView::clear_snapshot_context() {
    app.render_view.warp_markers.clear();
    app.render_view.phase_resets.clear();
    app.render_view.snapshot_warp_frame_map.clear();
    app.render_view.snapshot_display_total = 0;
    app.render_view.snapshot_commit_tab = 'A';
}

// Re-enumerate the renders/ folder and follow the currently-viewed entry by
// wav_path into the refreshed list. Entries carry no per-entry persisted state
// to migrate — a render entry is just its three path fields — so this only
// re-enumerates, keeps the index on the displayed entry (clamping to the
// closest surviving position when it was deleted), and reports empty.
bool GuiRenderView::refresh_render_view_list() {
    std::vector<AppState::RenderViewEntry> fresh =
        this->enumerate_render_view_list();
    if (fresh.empty()) {
        app.render_view.list.clear();
        app.render_view.index = -1;
        return false;
    }

    const int prior_index = app.render_view.index;
    std::string current_wav_path;
    if (prior_index >= 0 &&
        prior_index < static_cast<int>(app.render_view.list.size())) {
        current_wav_path =
            app.render_view.list[prior_index].wav_path.string();
    }

    app.render_view.list = std::move(fresh);

    int new_index = -1;
    if (!current_wav_path.empty()) {
        for (size_t i = 0; i < app.render_view.list.size(); ++i) {
            if (app.render_view.list[i].wav_path.string() ==
                current_wav_path) {
                new_index = static_cast<int>(i);
                break;
            }
        }
    }
    if (new_index < 0) {
        const int n = static_cast<int>(app.render_view.list.size());
        int clamped = prior_index;
        if (clamped < 0)  clamped = 0;
        if (clamped >= n) clamped = n - 1;
        new_index = clamped;
    }
    app.render_view.index = new_index;
    return true;
}

// Show render-view at the first .wav of the just-finished batch: enumerate
// the renders/ folder, select the first entry whose batch_folder matches the
// passed-in argument, and enter render view at it — the R-key toggle-on entry
// sequence, substituting the batch_folder match for the last_path match.
// A completion beneath an OPEN render view is unreachable under the
// entry-gated contract: `r` refuses while archival work is running or
// parked, the render-view key allowlist admits no archival dispatch chord,
// and the view itself dispatches nothing — no render can run under an open
// render view at all. So this method only ever enters fresh.
void GuiRenderView::auto_open_batch_at_first_file(
        const std::filesystem::path& batch_folder) {
    if (app.source_audio_path.empty()) return;

    std::vector<AppState::RenderViewEntry> list =
        this->enumerate_render_view_list();
    if (list.empty()) {
        std::fprintf(stderr,
            "warptempo_gui: auto-open: enumerator returned empty list\n");
        return;
    }

    int target = -1;
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].batch_folder == batch_folder) {
            target = static_cast<int>(i);
            break;
        }
    }
    if (target < 0) {
        std::fprintf(stderr,
            "warptempo_gui: auto-open: batch folder '%s' not found "
            "in render-view list\n",
            batch_folder.string().c_str());
        return;
    }

    app.render_view.list      = std::move(list);
    // Iter/BPM modes persist across render-view enter/leave. The flags are
    // inert inside render view (input gate drops i/M; paint
    // gates on !render_view.enabled), so they're simply restored on exit.
    app.render_view.enabled    = true;
    // Batch auto-open is a render-view ENTRY: the first entry load stashes the
    // authoring position and resets the display to fit-file/0/0, the same entry
    // sequence as the r toggle-on.
    if (!this->load_render_view_at(target)) {
        app.render_view.enabled = false;
        app.render_view.list.clear();
    }
}

// Shared teardown for "render-view ends here" exits driven by the
// navigation handlers when the renders/ folder turns out to be empty
// after a refresh. Equivalent to the toggle-off branch of the R key,
// minus the live-state capture that the navigation handler already
// performed before calling refresh_render_view_list.
void GuiRenderView::exit_render_view_and_clear() {
    this->restore_source_view();
    this->clear_snapshot_context();
    app.render_view.index             = -1;
}
