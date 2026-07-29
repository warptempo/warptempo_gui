#pragma once

#include "app_state.h"
#include "selection.h"
#include "undo.h"
#include "viewport.h"

class GuiAudio;
struct GuiTargetRender;

// Marker reposition drag — the single plain-flag-drag fine-tuning gesture
// (a plain flag press single-selects and arms it; motion past the shared
// threshold begins the move), shared by the warp and phase reset views and
// dispatched on app.active_markers_view (begin) and app.drag.drag_mode
// (commit). It
// lives in its own translation unit because it is the one cross-kind
// gesture: the per-kind authoring and selection-shift operations stay in
// GuiWarpMarkersOps and GuiPhaseResetMarkersOps. Under the frozen-coord
// regime, motion writes app.drag.moveable_times only — the live per-list
// stores stay untouched until commit_drag does the write-back.
struct MarkerDragOps {
    AppState&           app;
    const GuiAudio&     audio;
    Viewport&           viewport;
    Selection&          selection;
    Undo&               undo;
    GuiTargetRender&    target_render;

    MarkerDragOps(AppState&        app_,
                  const GuiAudio&  audio_,
                  Viewport&        viewport_,
                  Selection&       selection_,
                  Undo&            undo_,
                  GuiTargetRender& target_render_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          undo(undo_),
          target_render(target_render_) {}

    bool begin_drag(int hit, int mouse_x);
    void apply_drag_motion(double raw_delta);
    void commit_drag();

    // Target-view TEMPO drag (Ableton-style stretch) — the pointer half of the
    // home-view binding's tempo exception, warp-only and reachable only in W +
    // target view (the arm site in input_pointer.cpp owns the view/read-only
    // gating; TempoDragState in app_state.h carries the model). Unlike the
    // reposition drag above there is no overlay and no deferred commit: each
    // changed integer-cents candidate writes the predecessor's tempo into the
    // live store and re-warps synchronously, and the release only finalizes
    // (undo push + preview trigger on net change).
    //
    // tempo_drag_predecessor: store predicate + walk. Returns the GROUP's
    // predecessor store index for a drag on marker `hit`, or -1 when
    // ineligible. Coincident groups act as ONE item (architect 2026-07-22): the
    // predecessor is the nearest marker at a STRICTLY earlier frame (walk
    // backward past the equal-frame run of same-frame siblings), so dragging
    // ANY member of a stack drags the stack as one. That predecessor must be an
    // enabled tempo OWNER by its own authored payload and NOT itself a
    // coincident-collapse member (a surviving exact-frame group of 2+ enabled
    // markers resolves to one synthetic 1.00 owner, so its authored tempo is
    // render-inert — tested via the normalization-red set). The old zero-span
    // rejection is now structural: the walk lands a strictly-earlier frame, so
    // L_src >= 1 always. A marker FOLLOWING a stack still never arms — its
    // predecessor IS a collapse member, caught by the red-set test; and a
    // marker at the store's earliest frame (no strictly-earlier marker) returns
    // -1.
    int tempo_drag_predecessor(int hit) const;
    bool begin_tempo_drag(int hit);
    void apply_tempo_drag_motion(int mouse_x);
    // Release / lost button / any force-end (finalize_active_drags): FINALIZE.
    // There is no cancel_tempo_drag any more — pointer gestures have no cancel
    // (architect 2026-07-29, the rule at the drag-modal gate in
    // input_handler.cpp), so an interrupted drag keeps the cents it already wrote
    // and undo is the mitigation.
    void end_tempo_drag();

    // Deduped participant-predecessor seeding over the WHOLE selection — the
    // ONE owner of the group eligibility verdict, shared by begin_tempo_drag
    // (which consumes `walled` as the drag's arms-but-walls state) and the
    // keyboard step_tempo_image (which maps "would wall" to a silent refusal)
    // so the two surfaces cannot drift. `hit` is the grabbed/focused marker,
    // `pred` its own predecessor (tempo_drag_predecessor(hit), caller-checked
    // >= 0). participants is the deduped ascending predecessor set (never
    // empty — pred is inserted unconditionally); walled = any ineligible
    // selected member (tempo_drag_predecessor < 0: no strictly-earlier marker,
    // or a disabled/pass/ref/coincident-collapsed predecessor, or forward-label
    // coupling) OR the GROUP LABEL WALL (the multi-participant bisection's
    // monotonicity guarantee — full rationale at the definition).
    struct TempoGroupSeed {
        std::vector<int> participants;
        bool             walled = false;
    };
    TempoGroupSeed seed_tempo_group_participants(int hit, int pred) const;

    // The tempo solve CORE, factored out of apply_tempo_drag_motion
    // (byte-identical through the factor — the drag keeps its pointer->t_des
    // derivation and per-event commit tail around this call): given the desired
    // target-domain position `t_des` for marker `mi`'s image, mi's predecessor
    // `pi`, the participant set, and the walled pin, produce the
    // bracket-intersection-clamped group cents delta. Two paths by participant
    // count (closed-form absolute solve / exact monotone bisection — the full
    // rationale at apply_tempo_drag_motion). Returns false only on a
    // hypothetical-build failure inside the bisection (unreachable by
    // construction; the caller drops the event/press without committing).
    bool solve_tempo_group_delta(double t_des, int mi, int pi,
                                 const std::vector<int>& participants,
                                 bool walled, int64_t& out_delta) const;

    // W+target bare Left/Right: the tempo drag's KEYBOARD TWIN (architect
    // 2026-07-24 second pass — replacing the same-day warp position nudge's
    // target-view branch, which read as waveform truncation). One motion, two
    // routes: the drag is pointer-continuous, this steps the FOCUSED marker's
    // IMAGE by one painted column per press WHERE THE CENT GRID ALLOWS —
    // otherwise the minimum directional cent, whose travel can span several
    // columns (e.g. ~8 px on a 1 s span at working zoom; see step_tempo_image's
    // minimum-step rule) — both authoring the (deduped participant)
    // predecessors' tempo_cents through the same seeding and solve helpers
    // above. Dispatched from the bare Left/Right marker-lane route in
    // input_handler.cpp; a keyboard command like adjust_tempo_cents, living
    // here because the machinery is the drag's. `synthesized_repeat` is that key
    // event's platform repeat bit, read only by the undo-coalesce verdict (undo.h).
    void step_tempo_image(int direction, bool synthesized_repeat);
};
