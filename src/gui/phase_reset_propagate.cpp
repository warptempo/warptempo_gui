#include "phase_reset_propagate.h"

#include "phase_reset_clipboard.h"
#include "phase_reset_markers.h"
#include "target_render.h"
#include "time_format.h"
#include "warpmarkers.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

// Boundary guard for paste_state_apply's near-end bucketing, in
// seconds. A phase reset within this distance before a section end (or
// before a section start) is re-homed into the next chronological
// labeled section by shifting every block's membership window backward
// by this amount. This is an AUTHORING tolerance — the largest the user
// ever nudges a destination phase reset off its true section boundary
// (~2-10 ms typically, never more than ~92 ms) — deliberately NOT tied
// to the engine N/window size, so a render-setting change cannot shift
// which section a marker counts toward. Sole consumer is
// paste_state_apply.
constexpr double kPhaseResetBoundaryGuardSeconds = 0.100;

// One named block resolved from a warp-marker walk. `label` is the
// owning marker's label name (empty markers don't produce entries);
// `start` and `end` are absolute seconds from the marker and its
// immediate successor.
struct DestBlock {
    std::string label;
    double      start;
    double      end;
};

// Walk the warp marker list across [from_idx, to_idx_exclusive),
// returning the named blocks in order. A block's extent runs from
// its owning marker to the next warp marker in the list; the final
// marker in the warp list has no successor and contributes no block.
// Markers without a label name also contribute no entry.
std::vector<DestBlock> walk_named_blocks(
    const std::vector<GuiWarpMarker>& mv,
    int from_idx, int to_idx_exclusive) {
    std::vector<DestBlock> out;
    const int n = static_cast<int>(mv.size());
    if (from_idx < 0)        from_idx = 0;
    if (to_idx_exclusive > n) to_idx_exclusive = n;
    for (int i = from_idx; i < to_idx_exclusive; ++i) {
        if (i + 1 >= n) break;  // no next marker → no extent
        const std::string& name = warp_marker_label_name(mv[i]);
        if (name.empty()) continue;
        const double start = mv[i].time_seconds;
        const double end   = mv[i + 1].time_seconds;
        out.push_back(DestBlock{name, start, end});
    }
    return out;
}

}  // namespace

void PhaseResetPropagate::copy_from_selection() {
    const auto& mv = app.warpmarkers.markers();
    const auto& tv = app.phase_reset_markers.markers();
    if (app.selected_markers.size() != 2) return;

    auto it = app.selected_markers.begin();
    const int first_idx = *it++;
    const int last_idx  = *it;
    if (first_idx < 0 || last_idx < 0) return;
    const int n = static_cast<int>(mv.size());
    if (first_idx >= n || last_idx >= n) return;
    if (first_idx >= last_idx) return;

    // The closing boundary is the time of the last selected marker —
    // its block is excluded. The walk inspects markers in
    // [first_idx, last_idx) and uses each marker's next-time as the
    // block's end. The last_idx marker's time is the end-extent for
    // a named block at index last_idx - 1 (provided by walk_named_blocks's
    // i+1 indexing into mv).
    std::vector<DestBlock> src_blocks =
        walk_named_blocks(mv, first_idx, last_idx);

    std::vector<ClipboardBlock> clipboard_blocks;
    clipboard_blocks.reserve(src_blocks.size());
    for (const auto& b : src_blocks) {
        ClipboardBlock cb;
        cb.label_name   = b.label;
        cb.source_start = b.start;
        cb.source_end   = b.end;
        const double duration = b.end - b.start;
        if (duration <= 0.0) {
            clipboard_blocks.push_back(std::move(cb));
            continue;
        }
        for (const auto& t : tv) {
            const double t_time = t.time_seconds;
            if (t_time < b.start) continue;
            if (t_time >= b.end)  continue;
            ClipboardPlacement p;
            p.fractional_position = (t_time - b.start) / duration;
            p.source_time         = t_time;
            p.disabled            = t.disabled;
            cb.placements.push_back(p);
        }
        clipboard_blocks.push_back(std::move(cb));
    }

    app.phase_reset_clipboard.set(std::move(clipboard_blocks));
}

void PhaseResetPropagate::open_paste_confirmation() {
    if (app.phase_reset_clipboard.empty()) return;
    if (app.selected_markers.size() != 1) return;
    const int anchor = *app.selected_markers.begin();
    const int n = static_cast<int>(app.warpmarkers.markers().size());
    if (anchor < 0 || anchor >= n) return;

    app.pending_paste_anchor   = anchor;
    app.prompt.active          = true;
    app.prompt.text            =
        "Paste phase_resets into matching blocks? "
        "Existing phase_resets in matched ranges will be cleared.";
    app.prompt.response_keys   = {'y', '\x1b'};
    app.prompt.response_labels = {"[Y]es", "[Esc]"};
    app.prompt.trigger         = DialogTrigger::PASTE_CONFIRM;
    viewport.invalidate_all();
}

void PhaseResetPropagate::paste_apply() {
    const int anchor = app.pending_paste_anchor;
    app.pending_paste_anchor = -1;
    if (app.phase_reset_clipboard.empty()) return;
    const auto& mv = app.warpmarkers.markers();
    const int n = static_cast<int>(mv.size());
    if (anchor < 0 || anchor >= n) return;

    std::vector<DestBlock> dest_blocks =
        walk_named_blocks(mv, anchor, n);

    const auto& clip_blocks = app.phase_reset_clipboard.blocks();

    // Lockstep walk; stop on the first name divergence.
    const size_t pair_count = std::min(clip_blocks.size(), dest_blocks.size());
    size_t matched = 0;
    for (; matched < pair_count; ++matched) {
        if (clip_blocks[matched].label_name != dest_blocks[matched].label) break;
    }
    if (matched == 0) return;

    std::vector<GuiPhaseResetMarker> pre_state =
        app.phase_reset_markers.markers();
    const int hint_last = app.last_selected_marker;

    auto& out = app.phase_reset_markers.markers_mut();

    // Per-block clear of destination phase resets inside [start, end).
    for (size_t i = 0; i < matched; ++i) {
        const double start = dest_blocks[i].start;
        const double end   = dest_blocks[i].end;
        out.erase(std::remove_if(out.begin(), out.end(),
            [start, end](const GuiPhaseResetMarker& m) {
                return m.time_seconds >= start && m.time_seconds < end;
            }), out.end());
    }

    // Per-block materialization. Insert via the monotonic-insertion
    // path so list ordering and any defensive dedup behave identically
    // to manual authoring.
    for (size_t i = 0; i < matched; ++i) {
        const double dst_start = dest_blocks[i].start;
        const double dst_dur   = dest_blocks[i].end - dst_start;
        if (dst_dur <= 0.0) continue;
        for (const auto& p : clip_blocks[i].placements) {
            GuiPhaseResetMarker nm;
            nm.time_seconds = dst_start + p.fractional_position * dst_dur;
            nm.disabled     = p.disabled;
            app.phase_reset_markers.insert_marker(std::move(nm));
        }
    }

    undo.push_undo_phase_reset(std::move(pre_state), OpKind::Other, hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

void PhaseResetPropagate::paste_state_apply() {
    if (app.phase_reset_clipboard.empty()) return;
    if (app.selected_markers.size() != 1) return;
    const int anchor = *app.selected_markers.begin();
    const auto& mv = app.warpmarkers.markers();
    const int n = static_cast<int>(mv.size());
    if (anchor < 0 || anchor >= n) return;

    const std::vector<DestBlock> dest_blocks =
        walk_named_blocks(mv, anchor, n);
    const auto& clip_blocks = app.phase_reset_clipboard.blocks();

    // Boundary guard in seconds (phase reset time_seconds live in the
    // source domain on both sides). Fixed authoring tolerance, sample-
    // rate independent.
    const double n_seconds = kPhaseResetBoundaryGuardSeconds;

    // Flat list of every clipboard placement so we can bucket the
    // clipboard side by absolute source_time against block windows,
    // mirroring the destination-side bucketing. Capture produces
    // block-ordered, within-block-time-ordered placements, so a flat
    // concatenation is already source_time-ordered; sort defensively.
    std::vector<const ClipboardPlacement*> all_placements;
    for (const auto& cb : clip_blocks)
        for (const auto& p : cb.placements)
            all_placements.push_back(&p);
    std::sort(all_placements.begin(), all_placements.end(),
        [](const ClipboardPlacement* a, const ClipboardPlacement* b) {
            return a->source_time < b->source_time;
        });

    // Snapshot pre-state up front; we commit a single undo entry only
    // if at least one flag actually changes.
    std::vector<GuiPhaseResetMarker> pre_state =
        app.phase_reset_markers.markers();
    const int hint_last = app.last_selected_marker;

    auto& out = app.phase_reset_markers.markers_mut();

    bool any_change = false;
    std::string stop_message;

    const size_t pair_count = std::min(clip_blocks.size(), dest_blocks.size());
    for (size_t i = 0; i < pair_count; ++i) {
        // Label first, then count — mirrors paste_apply's
        // stop-on-divergence order.
        if (clip_blocks[i].label_name != dest_blocks[i].label) {
            stop_message = "stopped at " +
                format_timestamp(dest_blocks[i].start) +
                " (label name diverged)";
            break;
        }

        // Shifted membership window [start - N, end - N) on both sides.
        // A near-end marker of the previous interval (within N before
        // this block's start) migrates into this block; a near-end
        // marker of this block (within N before its end) migrates out
        // into the next interval. If the next interval is unlabeled or
        // past the compared range, the marker falls off — symmetrically
        // on both sides. Clamp hi >= lo so a pathologically tiny block
        // produces an empty window (count 0), not an inverted one.
        const double dst_lo = dest_blocks[i].start - n_seconds;
        const double dst_hi = std::max(dst_lo, dest_blocks[i].end - n_seconds);
        const double src_lo = clip_blocks[i].source_start - n_seconds;
        const double src_hi = std::max(src_lo, clip_blocks[i].source_end - n_seconds);

        // Windowed clipboard placements (migration applied). Globally
        // bucketed by source_time so a near-end placement originally
        // captured under block i-1 lands in block i's window when i-1
        // and i are adjacent labeled blocks.
        std::vector<const ClipboardPlacement*> windowed_clip;
        for (const auto* p : all_placements) {
            const double t = p->source_time;
            if (t < src_lo)  continue;
            if (t >= src_hi) continue;
            windowed_clip.push_back(p);
        }

        // Windowed destination markers (markers_ is time-ordered).
        std::vector<int> dest_indices;
        dest_indices.reserve(windowed_clip.size());
        for (size_t k = 0; k < out.size(); ++k) {
            const double t = out[k].time_seconds;
            if (t < dst_lo)  continue;
            if (t >= dst_hi) continue;
            dest_indices.push_back(static_cast<int>(k));
        }
        if (dest_indices.size() != windowed_clip.size()) {
            stop_message = "stopped at " +
                format_timestamp(dest_blocks[i].start) +
                " (marker count mismatch)";
            break;
        }
        for (size_t j = 0; j < dest_indices.size(); ++j) {
            const bool want = windowed_clip[j]->disabled;
            auto& m = out[dest_indices[j]];
            if (m.disabled != want) {
                m.disabled = want;
                any_change = true;
            }
        }
    }

    if (any_change) {
        undo.push_undo_phase_reset(std::move(pre_state), OpKind::Other,
                                   hint_last);
        undo.recompute_dirty();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    }

    if (!stop_message.empty()) {
        app.transient_status_message = std::move(stop_message);
        viewport.invalidate_timestamp_area();
    }
}
