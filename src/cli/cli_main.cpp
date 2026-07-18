#include "warpmarkers_parse.h"          // WarpMarker, parse_warpmarkers_file
#include "phaseresetmarkers_parse.h"  // PhaseResetMarker, parse_phaseresetmarkers_file
#include "engine_settings.h"            // EngineSettings
#include "env_fingerprint.h"            // compute_render_env_hashes
#include "settings_file.h"              // SettingsFile, read_settings_file
#include "warp_frame_map_build.h"               // build_warp_frame_map,
                                        // resolve_warp_markers_for_render
#include "phase_reset_frame_map_build.h"  // build_phase_reset_source_frames
#include "map_output.h"                 // write_frame_map_pair
#include "marker_store_validate.h"      // first_past_eof_wall_defect
#include "time_format.h"                // format_timestamp
#include "engine/engine.h"              // EngineParams, run_warptempo_engine
#include "engine/engine_geometry.h"     // kN, kRs
#include "locale_check.h"
#include "trimmer.h"                    // plan_trim, finish_render,
                                        // validate_render_projection
#include "render_output_naming.h"       // render_output_directory,
                                        // render_output_stem,
                                        // compose_render_output_path,
                                        // render_staging_path
#include "source_audio_io.h"            // load_source_range_to_buffer

#include "audio_probe.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

void unlink_silent(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// Per-process cache directory for the framemap pair, resolved by the same
// convention the GUI's RenderCache uses — $XDG_CACHE_HOME (else $HOME/.cache)
// + "/warptempo_gui/<pid>/" — WITHOUT linking the GUI's RenderCache: this CLI
// only creates the directories and writes the pair. It never sweeps or
// removes the dir — its pid dir becomes a dead-PID orphan the GUI's
// next-launch sweep_orphans removes. Returns the pid-dir path, or an empty
// string when no cache home resolves or the directories cannot be created
// (the caller skips the write silently then).
std::string cache_framemap_dir() {
    std::string base;
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && xdg[0]) {
        base = xdg;
    } else if (const char* home = std::getenv("HOME"); home && home[0]) {
        base = std::string(home) + "/.cache";
    } else {
        return {};
    }
    const std::string dir = base + "/warptempo_gui/" +
        std::to_string(static_cast<long>(::getpid()));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return {};
    return dir;
}

void usage(const char* argv0) {
    std::fprintf(stderr, "usage: %s <source-audio>\n", argv0);
}

}  // namespace

int main(int argc, char** argv) {
    if (!verify_c_numeric_locale("warptempo_cli")) return 1;

    std::string source_path;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (!a.empty() && a[0] != '-' && source_path.empty()) source_path = a;
        else { usage(argv[0]); return 2; }
    }
    if (source_path.empty()) { usage(argv[0]); return 2; }

    std::filesystem::path src(source_path);
    std::filesystem::path parent = src.parent_path();
    if (parent.empty()) parent = std::filesystem::path(".");
    const std::string stem     = src.stem().string();
    const std::string wm_path  = (parent / (stem + ".warpmarkers")).string();
    const std::string pr_path  = (parent / (stem + ".phaseresetmarkers")).string();
    const std::string set_path = (parent / (stem + ".settings")).string();

    // --- settings: required, like the two marker sidecars below. The strict
    // whole-file reader refuses an unopenable file with its own could-not-open
    // diagnostic, which surfaces verbatim through the print below.
    // title and the applied trim come from it. ---
    //
    // Sidecar check order here is settings -> markers -> resets -> probe,
    // while the GUI load checks probe -> audio -> markers -> settings. On a
    // file set with multiple defects the two products therefore report a
    // DIFFERENT first error; the loadability verdict is identical (a set is
    // loadable in both products or neither), only the message order diverges.
    // Accepted.
    EngineSettings es;
    SettingsFile   sf;
    SettingsTrim   trim;
    {
        // The whole-file strict schema (settings_file.h), the same reader
        // the GUI runs at source load: a sidecar set is loadable in both
        // products or neither, GUI-kind keys included (an off-preset
        // playback_speed refuses here exactly as it refuses the GUI load).
        auto parsed = read_settings_file(set_path);
        if (!parsed) {
            std::fprintf(stderr,
                "warptempo_cli: settings rejected in '%s': %s\n",
                set_path.c_str(), parsed.error().c_str());
            return 1;
        }
        sf = std::move(*parsed);
        es = sf.engine;
        // Both tabs are kept for the past-EOF guard below; the render applies
        // the active tab's trim, matching the GUI, which renders the trim of
        // the tab persisted in active_tab_view.
        const char active_tab = sf.active_tab_view;
        trim = (active_tab == 'B') ? sf.tab_b.trim : sf.tab_a.trim;
    }

    // --- source-clobber guard, adversarial and load-fatal: a hand-edited
    // sidecar whose title composes a render output path onto the
    // source audio itself would overwrite the source. The GUI editor refuses
    // this at commit and the GUI loader refuses the hand-edited file at load;
    // refuse it here too, first-error and stderr-only, so a file set is
    // loadable in both products or neither. The shared predicate matches the
    // GUI's composition exactly. ---
    if (auto collision = render_output_source_collision(es, source_path)) {
        std::fprintf(stderr,
            "warptempo_cli: settings in '%s' would make the render output '%s' "
            "overwrite the source audio file\n",
            set_path.c_str(), collision->string().c_str());
        return 1;
    }

    // --- output path: the shared composer's wav path, title-named beside the
    // source — exactly where the GUI writes the same project's deliverable.
    // Wav is the only render product. ---
    const std::string out_path =
        compose_render_output_path(render_output_directory(source_path),
                                   render_output_stem(es))
            .string();

    // --- markers: required. parse_warpmarkers_file refuses an unopenable file
    // with its own cannot-open-file diagnostic, which surfaces verbatim through
    // the print below. ---
    std::vector<WarpMarker> markers;
    {
        auto wmp = parse_warpmarkers_file(wm_path);
        if (!wmp) {
            std::fprintf(stderr, "warptempo_cli: %s: %s\n",
                         wm_path.c_str(), wmp.error().c_str());
            return 1;
        }
        markers = std::move(*wmp);
    }

    // --- phase reset markers: required. parse_phaseresetmarkers_file refuses
    // an unopenable file with its own cannot-open-file diagnostic, which
    // surfaces verbatim through the print below; the empty file is the
    // no-resets form and parses to an empty list. ---
    std::vector<PhaseResetMarker> resets;
    {
        auto prp = parse_phaseresetmarkers_file(pr_path);
        if (!prp) {
            std::fprintf(stderr, "warptempo_cli: %s: %s\n",
                         pr_path.c_str(), prp.error().c_str());
            return 1;
        }
        resets = std::move(*prp);
    }

    // --- source sample rate / total frames ---
    // Print the probe owner's diagnostic verbatim in the unified shape. The
    // CLI is insurance-only; per the non-adversarial rubric it gets the owner
    // detail with NO convert-once friendliness hint (that hint is GUI-side).
    auto info = audio_probe(source_path);
    if (!info) {
        std::fprintf(stderr,
            "warptempo_cli: source open failed for '%s': %s\n",
            source_path.c_str(), info.error().c_str());
        return 1;
    }
    const long sample_rate  = info->sample_rate;
    const long total_frames = static_cast<long>(info->frames);

    // Rates below 44.1k are out of scope by ruling, and the whole-frame gesture
    // pixel guarantees assume the 44100 floor (higher rates only widen the margins).
    if (sample_rate < 44100) {
        std::fprintf(stderr,
            "warptempo_cli: source load failed for '%s': sample rate %ld is "
            "below the 44100 floor\n",
            source_path.c_str(), sample_rate);
        return 1;
    }

    // The corpus is stereo, and mono-for-sale is delivered as locked stereo, so
    // off-corpus channel counts are refused rather than supported (convert once
    // outside, e.g. with ffmpeg). The stereo invariant also makes every
    // product-written wav payload even (see WavWriter::close), so the RIFF
    // odd-payload pad byte stays unreachable. Same compare in both binaries: a
    // file set is loadable in both products or neither.
    if (info->channels != 2) {
        std::fprintf(stderr,
            "warptempo_cli: source load failed for '%s': %d channels (stereo "
            "sources only)\n",
            source_path.c_str(), info->channels);
        return 1;
    }

    // --- adversarial past-EOF guard: refuse the load like a corrupt audio
    // file when any marker or any tab's trim sits past its wall. Such a
    // position is uncommittable through the GUI (marker EOF walls, per-bound
    // trim walls) and a sidecar applies only to the audio it was authored
    // against, so a past-EOF position means the audio was swapped outside the
    // GUI. BOTH tabs' trim is checked even though this CLI renders only the
    // active tab's trim: a file set must be loadable or not, identically in
    // both binaries — first_past_eof_wall_defect (marker_store_validate.h)
    // is the one implementation the GUI load runs too. The downstream
    // render-boundary EOF refusals stay as breach backstops for hand-edited
    // maps. ---
    {
        const int64_t total = static_cast<int64_t>(total_frames);
        if (auto wall = first_past_eof_wall_defect(
                markers, resets, sf.tab_a.trim, sf.tab_b.trim,
                total, sample_rate)) {
            std::fprintf(stderr, "warptempo_cli: %s\n", wall->c_str());
            return 1;
        }
    }

    // Persisted viewport/playhead positions are display scratch, not authored
    // data, and the CLI never applies a viewport or playhead. They are not
    // guarded at load: the GUI's runtime clamps own any out-of-range value,
    // so there is nothing here to refuse.

    // --- locked engine geometry (shared with render_pipeline.cpp via
    // engine_geometry.h) ---
    const int     N_fft = kN;
    const int     R_s   = kRs;

    // --- full (untrimmed) frame map, do_render's full_warp_frame_map: the
    // parser knows nothing of trim; a trimmed render hands the engine the
    // prepost trimmer's translated maps derived from this one below. ---
    auto resolved = resolve_warp_markers_for_render(markers, sample_rate,
                                                    total_frames);
    auto r = build_warp_frame_map(resolved,
                                  es.scale, sample_rate, total_frames);
    if (!r) {
        std::fprintf(stderr,
            "warptempo_cli: map build failed: %s\n", r.error().c_str());
        return 1;
    }
    const std::vector<WarpFrameMapSegment> full_warp_frame_map =
        std::move(*r);

    // --- full deliverable-form phase reset derivation, built once beside
    // the full map exactly as do_render builds it. ---
    auto phase_reset_source_frames_r =
        build_phase_reset_source_frames(resets, sample_rate, total_frames);
    if (!phase_reset_source_frames_r) {
        std::fprintf(stderr, "warptempo_cli: %s\n",
                     phase_reset_source_frames_r.error().c_str());
        return 1;
    }
    const std::vector<double> full_phase_reset_frame_map =
        derive_phase_reset_frame_map(*phase_reset_source_frames_r,
                                     full_warp_frame_map);

    // --- Cache-dir framemap pair (future-proofing; 1:1 with the GUI's
    // archival disk-route write in do_render — this CLI is disk-only). Drop
    // the FULL untrimmed warp frame map and its phase-reset column, through
    // the shared write_frame_map_pair (the all-or-nothing write-or-clean home),
    // into the cache pid dir, named by the final wav's basename stem —
    // together exactly the engine's input for the FULL render
    // (deliverable-relative target column, absolute source frames). Written
    // here, once the final path and both full maps are in hand and before
    // synthesis, matching the GUI's placement. Non-fatal: a failure prints one
    // stderr line and the render proceeds; no staging/rename dance (cache
    // files, not deliverables). The CLI never removes the dir — it rests as a
    // dead-PID orphan until the GUI's next-launch sweep_orphans deletes it.
    // This is a single source-dir render (one source per invocation, no batch
    // folder), so the pair stays FLAT in the pid dir — no per-batch subdir, no
    // basename collision possible; the GUI's batch-folder namespacing does not
    // apply here. ---
    {
        const std::string pair_dir = cache_framemap_dir();
        if (!pair_dir.empty()) {
            const std::string pair_stem =
                std::filesystem::path(out_path).stem().string();
            if (auto w = write_frame_map_pair(
                    pair_dir, pair_stem,
                    full_warp_frame_map, full_phase_reset_frame_map); !w) {
                std::fprintf(stderr,
                    "warptempo_cli: cache framemap write skipped: %s\n",
                    w.error().c_str());
            }
        }
    }

    // --- trim plan. plan_trim validates the authored bounds first
    // (validate_trim_frames stays the sole author of the trim-validity
    // vocabulary). A refusal is NOT a render failure: ambiguous trim falls
    // back to the full, untrimmed deliverable with one stderr line —
    // trim_plan stays unset and every downstream stage keys on trim_plan,
    // so the fallback render is byte-identical to a no-trim render of the
    // same recipe (identical to do_render's wav-arm fallback). ---
    std::optional<TrimPlan> trim_plan;
    if (trim.has_begin || trim.has_end) {
        auto plan = plan_trim(full_warp_frame_map, full_phase_reset_frame_map,
                              trim.has_begin, trim.begin_frame,
                              trim.has_end, trim.end_frame,
                              static_cast<int64_t>(total_frames),
                              N_fft, R_s);
        if (!plan) {
            // plan.error() strings all begin with "trim ..." (the
            // vocabulary in trimmer.cpp), so no extra category word here.
            std::fprintf(stderr,
                "warptempo_cli: %s; rendering untrimmed\n",
                plan.error().c_str());
        } else {
            trim_plan = std::move(*plan);
        }
    }

    // --- load the full source; trimmed renders read an offset+length view
    // into it, no copy (the GUI's shared buffer supports the same view). ---
    std::vector<float> src_samples;
    int src_sr = 0, src_ch = 0;
    {
        if (auto lr = load_source_range_to_buffer(
                source_path, 0, static_cast<size_t>(total_frames),
                src_samples, src_sr, src_ch); !lr) {
            std::fprintf(stderr, "warptempo_cli: %s\n", lr.error().c_str());
            return 1;
        }
    }

    // --- engine params. The engine is buffer-out only; the shared
    // post-engine chain encodes to the staging path below. ---
    EngineParams ep;
    ep.source_audio_samples = src_samples.data();
    ep.source_audio_frames  =
        src_samples.size() / static_cast<size_t>(src_ch);
    ep.source_sample_rate   = src_sr;
    ep.source_channels      = src_ch;
    if (trim_plan) {
        ep.source_audio_samples +=
            static_cast<size_t>(trim_plan->pre.begin_frame) *
            static_cast<size_t>(src_ch);
        ep.source_audio_frames =
            static_cast<size_t>(trim_plan->pre.frames);
    }
    std::vector<float> render_buf;
    ep.output_buffer = &render_buf;

    // Trimmed renders hand the engine the trimmer's translated maps — the
    // engine renders them wholesale, ends at its map's last anchor, and
    // stays trim-ignorant. Untrimmed: the full pair verbatim. Identical to
    // do_render's wav branch.
    ep.warp_frame_map = trim_plan
        ? std::move(trim_plan->pre.warp_frame_map)
        : full_warp_frame_map;
    ep.phase_reset_frame_map = trim_plan
        ? std::move(trim_plan->pre.phase_reset_frame_map)
        : full_phase_reset_frame_map;
    // Trimmed renders adopt the schedule plan_trim derives from the full map
    // (identity by construction, not the translated map's re-interpolation);
    // full renders pass none and the engine generates its own. trim_plan
    // outlives the engine call below (finish_render reads it too), and the
    // pointer targets a member never moved out. Identical to do_render.
    ep.source_frame_schedule = trim_plan
        ? &trim_plan->pre.source_frame_schedule
        : nullptr;
    // limiter_ceiling_dbfs / tolerance stay at EngineParams defaults —
    // do_render inherits them too. The spectral limiter always runs.

    // Projection refusal, orchestrator-side before the engine allocates
    // (refuse-before-cost), same shape as do_render's wav branch.
    const int64_t engine_output_frames = static_cast<int64_t>(
        std::llrint(ep.warp_frame_map.back().tgt_frame));
    const int64_t encoded_frames =
        trim_plan ? trim_plan->post.samples : engine_output_frames;
    if (auto v = validate_render_projection(
            engine_output_frames, encoded_frames, src_ch,
            /*encode_to_disk=*/true); !v) {
        std::fprintf(stderr, "warptempo_cli: %s\n", v.error().c_str());
        return 1;
    }

    // --- render-environment advisory, placed AFTER the last input-validation
    // gate (the projection refusal above) and immediately before render
    // dispatch, so a malformed project produces ONLY its first load error —
    // matching the GUI, which runs this comparison after load_file() fully
    // succeeds. Compare the four STORED hashes against the running
    // environment's. Detection only, never a refusal — a mismatch render is
    // fully valid; the CLI never writes sidecars, so it prints ONE stderr line
    // and continues (the GUI owns the acknowledge-and-restamp prompt). ---
    {
        const RenderEnvHashes& cur = compute_render_env_hashes();
        std::string changed;
        auto note = [&changed](const char* name) {
            if (!changed.empty()) changed += ", ";
            changed += name;
        };
        if (sf.libm_hash          != cur.libm)          note("libm");
        if (sf.libmvec_hash       != cur.libmvec)       note("libmvec");
        if (sf.fftw3_hash         != cur.fftw3)         note("fftw3");
        if (sf.fftw3_threads_hash != cur.fftw3_threads) note("fftw3_threads");
        if (!changed.empty()) {
            std::fprintf(stderr,
                "warptempo_cli: render libraries changed since last save "
                "(%s; glibc %s, fftw %s); output may differ from previous "
                "renders\n",
                changed.c_str(),
                render_env_glibc_version().c_str(),
                render_env_fftw_version().c_str());
        }
    }

    // --- render into the buffer, then run the shared post-engine chain
    // (crop when trimmed -> peak limiter -> encode to the sibling staging
    // file); success publishes atomically via rename.
    const std::string staging_output_path = render_staging_path(out_path);
    const EngineResult er = run_warptempo_engine(ep);
    if (er != EngineResult::Success) {
        std::fprintf(stderr, "warptempo_cli: engine %s\n",
                     er == EngineResult::Cancelled ? "cancelled" : "failed");
        return 1;
    }
    if (auto fin = finish_render(render_buf, src_ch, src_sr,
                                 trim_plan ? &trim_plan->post : nullptr,
                                 staging_output_path); !fin) {
        unlink_silent(staging_output_path);
        std::fprintf(stderr, "warptempo_cli: %s\n", fin.error().c_str());
        return 1;
    }

    {
        std::error_code ec;
        std::filesystem::rename(staging_output_path, out_path, ec);
        if (ec) {
            unlink_silent(staging_output_path);
            std::fprintf(stderr,
                "warptempo_cli: could not publish '%s' to '%s': %s\n",
                staging_output_path.c_str(), out_path.c_str(),
                ec.message().c_str());
            return 1;
        }
    }

    std::fprintf(stderr, "warptempo_cli: wrote %s\n", out_path.c_str());
    return 0;
}
