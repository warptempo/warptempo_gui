#include "frame_map_build.h"
#include "time_format.h"  // format_timestamp

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

double effective_tempo(const MarkerForRender& m) {
    double v = m.tempo_base;
    if (!m.tempo_scale.empty()) {
        try {
            v *= std::stod(m.tempo_scale);
        } catch (...) {
            return 0.0;
        }
    }
    return v;
}

struct LabelCacheEntry {
    double      delta_tgt   = 0.0;
    double      tempo_base  = 1.0;
    std::string tempo_scale;
};

}  // namespace

std::vector<MarkerForRender> resolve_markers_for_render(
    const std::vector<WarpMarker>& src) {

    // First pass: collect disabled label names.
    std::vector<std::string> disabled;
    for (const auto& m : src) {
        if (!m.label_def.empty() && m.disabled) disabled.push_back(m.label_def);
    }
    auto is_disabled_ref = [&](const std::string& label) {
        for (const auto& d : disabled) if (d == label) return true;
        return false;
    };

    // Inherited-tempo resolution for pass markers is the canonical
    // resolve_inherited_tempo / resolve_inherited_tempo_scale (defined below,
    // declared in frame_map_build.h) — the same walk the hover popup uses. Called
    // directly at the tempo_inherits branch.

    std::vector<MarkerForRender> out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        const auto& g = src[i];
        const bool is_disabled_label_ref_cascade =
            !g.disabled && !g.label_ref.empty()
            && is_disabled_ref(g.label_ref);
        // `disabled` is allowed on any marker; whatever
        // its kind, a disabled marker's tempo is silenced. The label_ref
        // cascade is a separate path (the ref itself is not disabled but
        // its target is). With trim moved to settings, a disabled marker
        // has no reason to survive into the resolved list.
        const bool is_effectively_disabled =
            g.disabled || is_disabled_label_ref_cascade;

        if (is_effectively_disabled) continue;

        MarkerForRender m;
        m.time_seconds  = g.time_seconds;
        m.label_def     = g.label_def;
        m.label_ref     = g.label_ref;

        if (!g.label_ref.empty()) {
            m.tempo_base = 0.0;
            m.tempo_scale.clear();
        } else if (g.tempo_inherits) {
            const int gi = static_cast<int>(i);
            m.tempo_base  = resolve_inherited_tempo(src, gi);
            m.tempo_scale = resolve_inherited_tempo_scale(src, gi);
        } else {
            m.tempo_base  = g.tempo_base;
            m.tempo_scale = g.tempo_scale;
        }
        out.push_back(std::move(m));
    }
    return out;
}

double resolve_inherited_tempo(const std::vector<WarpMarker>& markers, int index) {
    for (int i = index - 1; i >= 0; --i) {
        const WarpMarker& m = markers[i];
        if (!m.tempo_inherits && m.label_ref.empty() && !m.disabled) {
            return m.tempo_base;
        }
    }
    return 1.0;
}

std::string resolve_inherited_tempo_scale(
    const std::vector<WarpMarker>& markers, int index) {
    for (int i = index - 1; i >= 0; --i) {
        const WarpMarker& m = markers[i];
        if (!m.tempo_inherits && m.label_ref.empty() && !m.disabled) {
            return m.tempo_scale;
        }
    }
    return {};
}

MarkerEffective marker_effective(
    const std::vector<WarpMarker>& mv, int idx, int sample_rate) {
    MarkerEffective r;
    if (idx < 0 || idx >= static_cast<int>(mv.size())) return r;
    const WarpMarker& m = mv[idx];

    if (m.tempo_inherits) {
        // resolve_inherited_tempo walks backward from `walk-1`. Starting at
        // idx+1 lets it return idx's resolved tempo if idx is the only
        // inheriting marker in front of an owning origin.
        const int walk = idx + 1;
        r.base  = resolve_inherited_tempo(mv, walk);
        r.scale = resolve_inherited_tempo_scale(mv, walk);
        // source_idx is the immediate prior non-disabled marker — the
        // visible source of the inherited value, not necessarily the
        // owning marker if there's a chain of passes.
        for (int i = idx - 1; i >= 0; --i) {
            if (!mv[i].disabled) {
                r.source_idx = i;
                break;
            }
        }
        return r;
    }

    if (!m.label_ref.empty()) {
        int def_idx = -1;
        for (int i = 0; i < static_cast<int>(mv.size()); ++i) {
            if (mv[i].label_def == m.label_ref) {
                def_idx = i;
                break;
            }
        }
        if (def_idx < 0) return r;
        if (def_idx + 1 >= static_cast<int>(mv.size())) return r;
        if (idx     + 1 >= static_cast<int>(mv.size())) return r;
        const double sr_d = static_cast<double>(sample_rate);
        if (sr_d <= 0.0) return r;

        const double lr_src_dist =
            (mv[idx + 1].time_seconds - mv[idx].time_seconds) * sr_d;
        const double def_src_dist =
            (mv[def_idx + 1].time_seconds - mv[def_idx].time_seconds) * sr_d;
        if (def_src_dist <= 0.0 || lr_src_dist <= 0.0) return r;

        const WarpMarker& def = mv[def_idx];
        double      def_base;
        std::string def_scale_str;
        bool        def_has_typed_scale;
        if (def.tempo_inherits) {
            def_base = resolve_inherited_tempo(mv, def_idx);
            def_scale_str = "";
            def_has_typed_scale = false;
        } else {
            def_base = def.tempo_base;
            def_scale_str = def.tempo_scale;
            def_has_typed_scale = !def_scale_str.empty();
        }
        double def_scale_val = 1.0;
        if (def_has_typed_scale) {
            try { def_scale_val = std::stod(def_scale_str); }
            catch (...) { def_scale_val = 1.0; }
        }
        const double def_eff_tempo = def_base * def_scale_val;
        if (def_base == 0.0 || def_eff_tempo == 0.0) return r;

        // settings.scale cancels in the engine's multiplier expression:
        //   multiplier = (lr_src_dist * def_eff_tempo)
        //              / (def_base * def_src_dist)
        const double multiplier =
            (lr_src_dist * def_eff_tempo) / (def_base * def_src_dist);
        const double combined_scale = def_has_typed_scale
            ? (def_scale_val * multiplier)
            : multiplier;

        char scale_buf[32];
        std::snprintf(scale_buf, sizeof(scale_buf), "%.4f", combined_scale);
        r.base       = def_base;
        r.scale      = scale_buf;
        r.source_idx = def_idx;
        return r;
    }

    // Owner: resolves to its own tempo_base / tempo_scale.
    r.base       = m.tempo_base;
    r.scale      = m.tempo_scale;
    r.source_idx = idx;
    return r;
}

std::string compute_hover_popup_text(
    const std::vector<WarpMarker>& mv, int idx, int sample_rate) {
    if (idx < 0 || idx >= static_cast<int>(mv.size())) return "";
    const WarpMarker& m = mv[idx];

    if (m.tempo_inherits) {
        const MarkerEffective eff = marker_effective(mv, idx, sample_rate);
        if (eff.base == 0.0) return "";

        char tbuf[32];
        std::snprintf(tbuf, sizeof(tbuf), "%.2f", eff.base);
        std::string out = "= ";
        out += tbuf;
        if (!eff.scale.empty()) {
            out += "*";
            out += eff.scale;
        }

        // Provenance: the immediate prior marker's own displayed tempo
        // (its base, or base*scale if it carries a typed scale) and its
        // time_seconds.
        const WarpMarker& src = mv[eff.source_idx];
        char sbuf[32];
        std::snprintf(sbuf, sizeof(sbuf), "%.2f", src.tempo_base);
        std::string descriptor = sbuf;
        if (!src.tempo_scale.empty()) {
            descriptor += "*";
            descriptor += src.tempo_scale;
        }
        out += " (from ";
        out += descriptor;
        out += " @ ";
        out += format_timestamp(src.time_seconds);
        out += ")";
        return out;
    }

    if (!m.label_ref.empty()) {
        const MarkerEffective eff = marker_effective(mv, idx, sample_rate);
        if (eff.base == 0.0) return "";

        char base_buf[32];
        std::snprintf(base_buf, sizeof(base_buf), "%.2f", eff.base);
        std::string out = "~= ";
        out += base_buf;
        out += "*";
        out += eff.scale;

        // Provenance: "<def_base>:<label>" and the def marker's time_seconds.
        const WarpMarker& def = mv[eff.source_idx];
        out += " (from ";
        out += base_buf;
        out += ":";
        out += m.label_ref;
        out += " @ ";
        out += format_timestamp(def.time_seconds);
        out += ")";
        return out;
    }

    return "";
}

std::expected<MapBuildResult, std::string> build_maps(
    const MapBuildInput& in) {
    MapBuildResult out;

    const auto&  markers      = in.markers;
    const double scale        = in.scale;
    const long   sample_rate  = in.sample_rate;
    const long   total_frames = in.total_frames;

    if (sample_rate <= 0 || total_frames <= 0) {
        return std::unexpected("invalid source audio metadata");
    }

    // Trim range comes from .settings (MapBuildInput::trim_*), no
    // longer from per-marker flags. The post-pass below filters frame_map
    // segments by source frame against this range.
    // A begin of <= 0 means "start at the start", identical to no begin
    // trim, so normalize it away here rather than treating it as an error.
    const bool   has_begin = in.has_trim_begin && in.trim_begin_sec > 0.0;
    const bool   has_end   = in.has_trim_end;
    const double begin_sec = in.trim_begin_sec;
    const double end_sec   = in.trim_end_sec;

    // Pass 1: accumulate per-label deltas so forward-declared references
    // receive the correct duration when encountered in Pass 2.
    std::map<std::string, LabelCacheEntry> label_cache;

    double src_f_prev = 0.0;
    double tgt_f_prev = 0.0;

    for (size_t i = 0; i < markers.size(); ++i) {
        double src_frame = (i + 1 < markers.size())
            ? markers[i + 1].time_seconds * static_cast<double>(sample_rate)
            : static_cast<double>(total_frames);

        if (src_frame > static_cast<double>(total_frames)) {
            return std::unexpected("marker time exceeds source length at marker "
                                   + std::to_string(i));
        }
        if (src_frame - src_f_prev < 1.0) {
            return std::unexpected("marker segment < 1 frame at marker "
                                   + std::to_string(i));
        }

        const auto& m = markers[i];
        double current_tgt = tgt_f_prev;
        const bool is_numeric   = m.label_ref.empty();
        const bool is_label_ref = !m.label_ref.empty();

        if (is_numeric) {
            double tempo_val = effective_tempo(m);
            if (tempo_val > 9.99) {
                return std::unexpected("tempo " + std::to_string(tempo_val)
                                       + " exceeds 9.99 at marker "
                                       + std::to_string(i));
            }
            if (tempo_val <= 0.0) {
                return std::unexpected("tempo " + std::to_string(tempo_val)
                                       + " <= 0 at marker " + std::to_string(i));
            }

            double delta_src = src_frame - src_f_prev;
            double delta_tgt = delta_src / (tempo_val * scale);
            current_tgt += delta_tgt;

            if (!m.label_def.empty()) {
                if (label_cache.count(m.label_def)) {
                    return std::unexpected("duplicate label definition: "
                                           + m.label_def);
                }
                LabelCacheEntry e;
                e.delta_tgt   = delta_tgt;
                e.tempo_base  = m.tempo_base;
                e.tempo_scale = m.tempo_scale;
                label_cache[m.label_def] = e;
            }
            tgt_f_prev = current_tgt;
        }
        (void)is_label_ref;  // computed only for Pass 2

        src_f_prev = src_frame;
    }

    // Pass 2: emit frame_map segments + tempo_map entries.
    out.frame_map.push_back({0, 0});

    src_f_prev = 0.0;
    tgt_f_prev = 0.0;
    double last_valid_multiplier = 1.0;

    for (size_t i = 0; i < markers.size(); ++i) {
        double src_frame = (i + 1 < markers.size())
            ? markers[i + 1].time_seconds * static_cast<double>(sample_rate)
            : static_cast<double>(total_frames);

        const auto& m = markers[i];
        double target_frame = 0.0;

        if (!m.label_ref.empty()) {
            auto it = label_cache.find(m.label_ref);
            if (it == label_cache.end()) {
                return std::unexpected("undefined label reference: "
                                       + m.label_ref);
            }
            const LabelCacheEntry& lbl = it->second;
            target_frame = tgt_f_prev + lbl.delta_tgt;

            double base_val = lbl.tempo_base;
            double unadj    = tgt_f_prev + ((src_frame - src_f_prev) / (base_val * scale));
            double multiplier = (unadj - tgt_f_prev) / (target_frame - tgt_f_prev);

            double final_multiplier = multiplier;
            if (!lbl.tempo_scale.empty()) {
                // The parser validates every scale to the strict N.NNNN
                // syntax, so std::stod cannot throw on a well-formed load; a
                // throw here means a malformed scale slipped past parse.
                double s_val = 0.0;
                try { s_val = std::stod(lbl.tempo_scale); }
                catch (...) {
                    return std::unexpected("malformed tempo scale at marker "
                                           + std::to_string(i) + " (label: "
                                           + m.label_ref + "): "
                                           + lbl.tempo_scale);
                }
                final_multiplier = s_val * multiplier;
            }
            if (final_multiplier > 9.9999) {
                return std::unexpected("label final multiplier > 9.9999 at marker "
                                       + std::to_string(i) + " (label: "
                                       + m.label_ref + ")");
            }
        } else {
            double tempo_val = effective_tempo(m);
            double delta_src = src_frame - src_f_prev;
            target_frame = tgt_f_prev + (delta_src / (tempo_val * scale));
        }

        double seg_src_dur = src_frame - src_f_prev;
        double seg_tgt_dur = target_frame - tgt_f_prev;
        if (seg_tgt_dur > 0.000001) {
            double effective_multiplier = seg_src_dur / seg_tgt_dur;
            last_valid_multiplier = effective_multiplier;
            double seg_start_time = tgt_f_prev / static_cast<double>(sample_rate);
            out.tempo_map.push_back({seg_start_time, effective_multiplier});
        }

        out.frame_map.push_back({src_frame, target_frame});

        src_f_prev = src_frame;
        tgt_f_prev = target_frame;
    }

    double final_tgt_sec = tgt_f_prev / static_cast<double>(sample_rate);
    out.tempo_map.push_back({final_tgt_sec, last_valid_multiplier});

    // Trim post-pass. Shifts both frame_map and tempo_map vectors to begin at
    // the trim start; drops entries outside [begin_frame, end_frame]. Injects
    // synthetic boundary anchors into frame_map (and, on the end side,
    // tempo_map) when the trim boundaries do not align with real warp markers, so the
    // engine reads target_total_frames = anchor + N rather than truncating
    // at the last in-range marker.
    if (has_begin || has_end) {
        long begin_frame = has_begin
            ? static_cast<long>(std::nearbyint(begin_sec * sample_rate))
            : 0;
        long end_frame   = has_end
            ? static_cast<long>(std::nearbyint(end_sec   * sample_rate))
            : total_frames;

        // Snapshot the pre-shift frame_map for boundary interpolation; the
        // shift loop below moves into out.frame_map. The engine helper
        // map_source_to_target takes std::vector<FrameMapSegment> (engine
        // struct), so build a one-shot element-wise copy.
        std::vector<FrameMapSegment> pre_shift_frame_map = out.frame_map;

        // Boundary tgt values (unrounded). Aligning to a real marker
        // degenerates to that marker's tgt (interpolation at a node returns the
        // node); off-alignment yields the interpolated tgt at the trim boundary
        // so the shift offset preserves strict monotonicity when the synthetic
        // anchor is later prepended/appended. Source offset is the integer trim
        // cut (begin_frame); only the target offset is fractional.
        const double begin_tgt = has_begin
            ? map_source_to_target(static_cast<double>(begin_frame),
                                   pre_shift_frame_map)
            : 0.0;
        const double end_tgt = has_end
            ? map_source_to_target(static_cast<double>(end_frame),
                                   pre_shift_frame_map)
            : (pre_shift_frame_map.empty()
                  ? 0.0
                  : pre_shift_frame_map.back().tgt_frame);

        std::vector<FrameMapSegment> frame_map_shifted;
        for (const auto& seg : pre_shift_frame_map) {
            const long sf = static_cast<long>(std::llrint(seg.src_frame));
            if (sf >= begin_frame && sf <= end_frame) {
                frame_map_shifted.push_back({
                    seg.src_frame - static_cast<double>(begin_frame),
                    seg.tgt_frame - begin_tgt
                });
            }
        }
        out.frame_map = std::move(frame_map_shifted);

        // Begin anchor: prepend (0, 0) when no surviving entry shifted to
        // src_frame == 0. The interpolated begin_tgt above guarantees
        // strict monotonicity against the first real entry.
        if (has_begin &&
            (out.frame_map.empty() ||
             std::llrint(out.frame_map.front().src_frame) != 0)) {
            out.frame_map.insert(out.frame_map.begin(), FrameMapSegment{0.0, 0.0});
            out.has_trim_begin_anchor = true;
        }

        // End anchor: append (end-begin, end_tgt-begin_tgt) when no
        // surviving entry sits at the trim_end boundary.
        if (has_end) {
            const long end_src_shifted = end_frame - begin_frame;
            if (out.frame_map.empty() ||
                std::llrint(out.frame_map.back().src_frame) != end_src_shifted) {
                out.frame_map.push_back(FrameMapSegment{
                    static_cast<double>(end_frame - begin_frame),
                    end_tgt - begin_tgt
                });
                out.has_trim_end_anchor = true;
            }
        }

        {
            double begin_tgt_sec =
                static_cast<double>(std::llrint(begin_tgt)) / sample_rate;
            double end_tgt_sec   =
                static_cast<double>(std::llrint(end_tgt))   / sample_rate;

            std::vector<TempoMapEntry> tempo_map_shifted;
            double active_multiplier = 1.0;
            bool   start_point_written = false;
            for (const auto& e : out.tempo_map) {
                if (e.target_time_sec < begin_tgt_sec) {
                    active_multiplier = e.multiplier;
                } else if (e.target_time_sec <= end_tgt_sec) {
                    if (!start_point_written) {
                        if (e.target_time_sec > begin_tgt_sec) {
                            tempo_map_shifted.push_back({0.0, active_multiplier});
                        }
                        start_point_written = true;
                    }
                    tempo_map_shifted.push_back({e.target_time_sec - begin_tgt_sec, e.multiplier});
                    active_multiplier = e.multiplier;
                }
            }
            if (!start_point_written) {
                tempo_map_shifted.push_back({0.0, active_multiplier});
            }
            // End anchor for tempo_map: a final tempo event at the trim_end
            // timestamp carrying the multiplier active just before the
            // boundary — a no-op tempo change that gives DAWs the correct
            // track length. Mirrors the frame_map end anchor's presence.
            if (out.has_trim_end_anchor) {
                tempo_map_shifted.push_back(
                    {end_tgt_sec - begin_tgt_sec, active_multiplier});
            }
            out.tempo_map = std::move(tempo_map_shifted);
        }

        out.trimmed          = true;
        out.trim_begin_frame = static_cast<size_t>(begin_frame);
        out.trim_end_frame   = static_cast<size_t>(end_frame);
    }

    return out;
}

FrameMapRealRange real_segments(const MapBuildResult& r) {
    auto b = r.frame_map.begin() + (r.has_trim_begin_anchor ? 1 : 0);
    auto e = r.frame_map.end()   - (r.has_trim_end_anchor   ? 1 : 0);
    return {b, e};
}

std::vector<int64_t> phase_reset_source_frames(
    const std::vector<PhaseResetMarker>& markers, long sample_rate) {
    std::vector<int64_t> out;
    out.reserve(markers.size());
    for (const auto& m : markers) {
        if (m.disabled) continue;
        out.push_back(static_cast<int64_t>(
            std::nearbyint(m.time_seconds *
                           static_cast<double>(sample_rate))));
    }
    return out;
}

WindowedFrameMap slice_frame_map_to_trim_window(
    const std::vector<FrameMapSegment>& full_map,
    int64_t trim_begin_src, int64_t trim_end_src,
    int N, int R_s) {
    WindowedFrameMap out;
    if (full_map.empty()) { out.frame_map = full_map; return out; }

    // Dense synthesis-frame source schedule over the FULL map. Identical to the
    // engine's generate_source_frame_positions(): frame m at output m*R_s reads
    // source map_target_to_source(m*R_s) - N/2, banker's-rounded to int64
    // (llrint).
    const int64_t target_total =
        static_cast<int64_t>(std::llrint(full_map.back().tgt_frame)) + N;
    std::vector<int64_t> dense;
    for (int64_t t_s = 0; t_s < target_total; t_s += R_s) {
        const double src =
            map_target_to_source(static_cast<double>(t_s), full_map)
            - static_cast<double>(N) / 2.0;
        dense.push_back(static_cast<int64_t>(std::llrint(src)));
    }
    const int num_frames = static_cast<int>(dense.size());
    if (num_frames == 0) { out.frame_map = full_map; return out; }

    // Window [wbegin, wend): identical selection to the engine's removed block.
    auto bit = std::upper_bound(dense.begin(), dense.end(), trim_begin_src);
    int wbegin = (bit == dense.begin()) ? 0
               : static_cast<int>((bit - dense.begin()) - 1);
    if (wbegin > num_frames - 1) wbegin = num_frames - 1;
    auto eit = std::upper_bound(dense.begin(), dense.end(), trim_end_src);
    int wend = static_cast<int>(eit - dense.begin());
    if (wend > num_frames) wend = num_frames;
    if (wend < wbegin + 1) wend = wbegin + 1;

    const int64_t offset =
        static_cast<int64_t>(wbegin) * static_cast<int64_t>(R_s);
    out.window_offset_samples = offset;

    // Edge sources/targets on the full map's exact piecewise lines.
    const int64_t end_tgt_full = static_cast<int64_t>(std::llrint(
        map_source_to_target(static_cast<double>(trim_end_src), full_map)));
    const double start_src_precise =
        map_target_to_source(static_cast<double>(offset), full_map);

    std::vector<FrameMapSegment>& sm = out.frame_map;
    // Start anchor at output 0.
    sm.push_back(FrameMapSegment{
        start_src_precise < 0.0 ? 0.0 : start_src_precise, 0.0});
    // Interior real segments strictly inside the window's output span, target
    // shifted by -offset (rigid integer translation -> lines preserved exactly),
    // source absolute. Skip any that would collide with the start anchor's
    // source or land at/under target 0 (strict-monotonic guard).
    for (const auto& s : full_map) {
        const int64_t tf = static_cast<int64_t>(std::llrint(s.tgt_frame));
        if (tf <= offset || tf >= end_tgt_full) continue;
        const int64_t sf = static_cast<int64_t>(std::llrint(s.src_frame));
        if (sf <= static_cast<int64_t>(std::llrint(sm.back().src_frame))) continue;  // src strict
        const int64_t st = tf - offset;
        if (st <= static_cast<int64_t>(std::llrint(sm.back().tgt_frame))) continue;  // tgt strict
        sm.push_back(FrameMapSegment{s.src_frame,
                                     s.tgt_frame - static_cast<double>(offset)});
    }
    // End on the first full-map anchor at or past trim_end_src (the anchor that
    // closes the segment containing trim_end_src), target-shifted by -offset.
    // Using a real anchor keeps the final segment on the full map's exact line,
    // so source reads up to the trim boundary match a full render. The engine
    // truncates at emit_sample_cap (below), so the span between trim_end_src and
    // this anchor is synthesized into the discarded tail only. trim_end_src is
    // bounded by the source length, so a closing anchor always exists (at worst
    // full_map.back()).
    for (const auto& s : full_map) {
        if (static_cast<int64_t>(std::llrint(s.src_frame)) < trim_end_src) continue;
        const int64_t sf = static_cast<int64_t>(std::llrint(s.src_frame));
        const int64_t st = static_cast<int64_t>(std::llrint(s.tgt_frame)) - offset;
        if (sf > static_cast<int64_t>(std::llrint(sm.back().src_frame)) &&
            st > static_cast<int64_t>(std::llrint(sm.back().tgt_frame))) {
            sm.push_back(FrameMapSegment{s.src_frame,
                                         s.tgt_frame - static_cast<double>(offset)});
        }
        break;
    }

    // Output-sample cap: the trim-end target on the full map, re-anchored. The
    // engine emits up to this and no further, so the render ends exactly at the
    // trim boundary even though the sub-map's last anchor sits past it.
    const int64_t cap = end_tgt_full - offset;
    out.emit_sample_cap = cap < 0 ? 0 : cap;
    return out;
}
