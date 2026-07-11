#include "warp_frame_map_view.h"

#include "app_state.h"
#include "audio.h"
#include "gui_display_context.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// GUI target-view warp_frame_map helpers, split out of warp_frame_map_build.cpp so the
// build core (resolve_warp_markers_for_render / build_warp_frame_map / the
// phase-reset assembly) carries no AppState dependency and lives in
// the parser sources. These helpers stay GUI-side: they read the
// live AppState marker store and the active-view selector, and own the
// memoized target-view cache. Declarations remain in warp_frame_map_view.h.

std::vector<WarpFrameMapSegment> build_target_view_warp_frame_map(
    const std::vector<GuiWarpMarker>& markers,
    double scale,
    int sample_rate,
    long total_frames,
    std::string* error_out) {
    // Trim is render-time, not view-time — target view paints the WHOLE song,
    // and build_warp_frame_map builds the whole-song map, matching the
    // paint_handler construction so hit-test math and waveform paint walk the
    // same segment list. This overload always builds directly and is the entry
    // point for hypothetical (non-live) marker lists; live-state consumers go
    // through target_view_warp_frame_map_cached.
    // A resolve failure (first-marker render grammar, tie, dangling label
    // ref) or a build failure returns the empty map and reports the message
    // through error_out. The empty map still paints as identity for the
    // frame or two it stays displayed, but it is no longer a silent
    // dead-end: the cache records the error and the target-view validity
    // gate (GuiInputHandler::enforce_target_view_validity) kicks back to
    // source view on the next tick and opens the defect-resolution series
    // (or the error-notice popup for the non-modeled class).
    if (error_out) error_out->clear();
    auto resolved = resolve_warp_markers_for_render(
        slice_to_warp_markers(markers), sample_rate);
    if (!resolved) {
        if (error_out) *error_out = std::move(resolved.error());
        return {};
    }
    auto r = build_warp_frame_map(
        *resolved, scale, sample_rate, total_frames);
    if (!r) {
        if (error_out) *error_out = std::move(r.error());
        return {};
    }
    return std::move(*r);
}

const TargetWarpFrameMapCache& target_view_warp_frame_map_cached(
    const AppState& app, int sample_rate, long total_frames) {
    TargetWarpFrameMapCache& c = app.target_warp_frame_map_cache;
    const long long gen = app.warpmarkers.generation();
    const double scale  = app.engine_settings.scale;
    if (c.valid && c.markers_gen == gen && c.scale == scale &&
        c.sample_rate == sample_rate && c.total_frames == total_frames) {
        return c;
    }
    c.warp_frame_map = build_target_view_warp_frame_map(
        app.warpmarkers.markers(), scale, sample_rate, total_frames,
        &c.build_error);
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const auto& s : c.warp_frame_map) {
        h ^= std::bit_cast<uint64_t>(s.src_frame);
        h *= 0x100000001b3ULL;
        h ^= std::bit_cast<uint64_t>(s.tgt_frame);
        h *= 0x100000001b3ULL;
    }
    c.hash         = c.warp_frame_map.empty() ? 0 : h;
    c.tgt_total_frames = target_total_frames_for_map(
        static_cast<int64_t>(total_frames), c.warp_frame_map);
    c.markers_gen  = gen;
    c.scale        = scale;
    c.sample_rate  = sample_rate;
    c.total_frames = total_frames;
    c.valid        = true;
    return c;
}

int64_t to_source_frame(const AppState& app, int64_t domain_frame,
                        const std::vector<WarpFrameMapSegment>& warp_frame_map) {
    if (app.active_audio_view == 'S') return domain_frame;
    const size_t q = (domain_frame < 0)
        ? static_cast<size_t>(0)
        : static_cast<size_t>(domain_frame);
    return static_cast<int64_t>(
        std::nearbyint(map_target_to_source(q, warp_frame_map)));
}

int64_t to_domain_frame(const AppState& app, int64_t source_frame,
                        const std::vector<WarpFrameMapSegment>& warp_frame_map) {
    if (app.active_audio_view == 'S') return source_frame;
    const size_t q = (source_frame < 0)
        ? static_cast<size_t>(0)
        : static_cast<size_t>(source_frame);
    return static_cast<int64_t>(
        std::nearbyint(map_source_to_target(q, warp_frame_map)));
}

namespace {
// The stem painters' samples-per-pixel: the visible span quantized to
// whole samples (the vp_end the waveform-cache inputs carry is
// vp_start + nearbyint(spp * area.w)) divided back over the strip width.
// Both pixel-anchoring helpers below use this same value in both
// directions, so a committed column time round-trips to its own column.
// Returns 0.0 on degenerate geometry (no strip width / no zoom).
double painter_samples_per_pixel(const AppState& app, const GuiAudio& audio,
                                 const GuiRect& area) {
    if (area.w <= 0) return 0.0;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return 0.0;
    return std::nearbyint(spp * static_cast<double>(area.w)) /
           static_cast<double>(area.w);
}
}  // namespace

int painted_column_of_source_frame(
    const AppState& app, const GuiAudio& audio, double source_frame,
    const std::vector<WarpFrameMapSegment>& warp_frame_map) {
    const GuiRect area = waveform_area(app);
    const double spp = painter_samples_per_pixel(app, audio, area);
    if (spp <= 0.0) return 0;
    // The painters' exact shape (frame_to_paint_sample in render.cpp):
    // nearbyint the source frame; in target view forward-map and
    // nearbyint the map output; then std::round the fractional column.
    double ms = std::nearbyint(source_frame);
    if (app.active_audio_view != 'S' && !warp_frame_map.empty()) {
        ms = std::nearbyint(map_source_to_target(ms, warp_frame_map));
    }
    const double x_raw =
        (ms - static_cast<double>(app.viewport_start_sample)) / spp;
    return static_cast<int>(std::round(x_raw));
}

int64_t authored_frame_at_column(
    const AppState& app, const GuiAudio& audio, int col,
    const std::vector<WarpFrameMapSegment>& warp_frame_map) {
    const GuiRect area = waveform_area(app);
    const double spp = painter_samples_per_pixel(app, audio, area);
    if (spp <= 0.0) return 0;
    const double t_active =
        static_cast<double>(app.viewport_start_sample) +
        static_cast<double>(col) * spp;
    if (app.active_audio_view != 'S' && !warp_frame_map.empty()) {
        // Quantize the column's target-domain time to an integer target
        // frame (floored at 0), then inverse-map at full precision; the
        // map is monotone increasing, so the target-domain direction is
        // the source-domain direction.
        const double q = (t_active < 0.0)
            ? 0.0
            : static_cast<double>(std::llrint(t_active));
        return snap_authored_frame(map_target_to_source(q, warp_frame_map));
    }
    return snap_authored_frame(t_active);
}

// The single reader of app.active_audio_view / app.render_view.enabled
// for DOMAIN QUERIES (see gui_display_context.h for the ruling and the
// mode-logic carve-out). The three arms are the composite view rule.
const GuiDisplayContext& active_display_context(const AppState& app,
                                                const GuiAudio& audio) {
    // Function-local static storage, refreshed on every call. The GUI
    // loop is single-threaded, and warp_frame_map aliases the app-owned
    // cache exactly as the direct cache callers always did — valid until
    // a changed-key rebuild.
    static GuiDisplayContext ctx;
    static const std::vector<WarpFrameMapSegment> kIdentityMap;
    if (app.render_view.enabled) {
        // Render view translates IDENTITY, whatever active_audio_view
        // says: render view displays a flat rendered wav whose positions
        // (the render_view display stores) are already in the displayed
        // domain, while the target map describes the AUTHORING source's
        // deformation — the flag persists as 'T' through render view, so
        // the TargetLive arm below is reached only when render view is
        // off. Translating here would also rebuild the target-map cache
        // against the render audio's totals, which is never meaningful.
        // A dedicated Render domain kind lands when render view rebuilds
        // onto an authored-domain snapshot context; this arm is where it
        // will plug in.
        ctx.domain = GuiDisplayDomain::Source;
        ctx.warp_frame_map = &kIdentityMap;
        ctx.domain_total_frames = audio.total_frames();
    } else if (app.active_audio_view == 'T') {
        // Live target view: the memoized target-view map is the
        // translation, and the displayed total is the built map's target
        // total — source total when the map cannot build (the cache's
        // tgt_total_frames is only > 0 for a built map).
        const TargetWarpFrameMapCache& c = target_view_warp_frame_map_cached(
            app, audio.sample_rate(),
            static_cast<long>(audio.total_frames()));
        ctx.domain = GuiDisplayDomain::TargetLive;
        ctx.warp_frame_map = &c.warp_frame_map;
        ctx.domain_total_frames = (c.tgt_total_frames > 0)
            ? c.tgt_total_frames
            : audio.total_frames();
    } else {
        // Source view: identity, source total.
        ctx.domain = GuiDisplayDomain::Source;
        ctx.warp_frame_map = &kIdentityMap;
        ctx.domain_total_frames = audio.total_frames();
    }
    ctx.sample_rate = audio.sample_rate();
    return ctx;
}

// Translate through the active display context. A Source-domain context
// is identity outright; a TargetLive context routes through
// to_domain_frame / to_source_frame, whose empty-map path (the
// unbuildable-target fallthrough) is itself identity.
int64_t source_frame_to_active_domain(const AppState& app, const GuiAudio& audio,
                                      int64_t source_frame) {
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    if (ctx.domain == GuiDisplayDomain::Source) return source_frame;
    return to_domain_frame(app, source_frame, *ctx.warp_frame_map);
}

int64_t active_domain_to_source_frame(const AppState& app, const GuiAudio& audio,
                                      int64_t domain_frame) {
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    if (ctx.domain == GuiDisplayDomain::Source) return domain_frame;
    return to_source_frame(app, domain_frame, *ctx.warp_frame_map);
}
