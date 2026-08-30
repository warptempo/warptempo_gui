#pragma once

#include "app_state.h"
#include "playback_lifecycle.h"
#include "selection.h"
#include "undo.h"
#include "viewport.h"

#include <optional>
#include <string>
#include <vector>

class GuiAudio;
struct GuiWarpMarker;
struct GuiTargetRender;

// Index of the nearest non-disabled marker strictly before `time_frame`,
// or -1 if none. See the definition in warpmarkers_ops.cpp for the full
// doc comment (matches the resolver's walk).
int find_immediate_prior(const std::vector<GuiWarpMarker>& mv,
                          double time_frame);

// Warp-authoring cluster. Covers the basic authoring operations (drop /
// delete / toggle / adjust) and the pixel-column-anchored nudge — and, in
// adjust_tempo_cents + adjust_tempo_cents_group, THE WHOLE TEMPO SURFACE since
// 2026-07-29 (the pointer tempo drag and the bare Left/Right tempo-image step were
// deleted; the list is at the head of marker_drag.h).
// stop_playback_if_playing is reached through playback_lifecycle. The
// reposition drag is no longer here: it is the one cross-kind gesture and
// lives in MarkerDragOps in marker_drag.{h,cpp}.
// THE REFUSAL REASON, and why an authoring op returns a STRING (architect
// 2026-08-30, the strictness ruling "a card for every silent refusal"): the
// facts these acts refuse on are THEIRS — a marker that owns no tempo, a
// coincident stack, a bracket edge, a wall — so the sentence is composed
// where the fact lives, and the CARD is raised by the dispatcher, which is
// the layer that knows a press happened. That split is what keeps this
// freeze-adjacent cluster free of GuiNotifications while still saying what it
// refused. std::nullopt means NOTHING TO SAY: the act ran, or it refused on a
// fact an OUTER gate has already carded (one card per press, raised at the
// outermost site that has the reason) or on a belt against an invariant the
// selection layer keeps.
using GuiOpRefusal = std::optional<std::string>;

struct GuiWarpMarkersOps {
    AppState&             app;
    const GuiAudio&       audio;
    Viewport&             viewport;
    Selection&            selection;
    Undo&                 undo;
    GuiPlaybackLifecycle& playback_lifecycle;
    GuiTargetRender&   target_render;

    GuiWarpMarkersOps(AppState&             app_,
                      const GuiAudio&       audio_,
                      Viewport&             viewport_,
                      Selection&            selection_,
                      Undo&                 undo_,
                      GuiPlaybackLifecycle& playback_lifecycle_,
                      GuiTargetRender&   target_render_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          undo(undo_),
          playback_lifecycle(playback_lifecycle_),
          target_render(target_render_) {}

    void drop_marker(double time_frame, bool inherit,
                      int64_t tempo_cents, std::optional<double> scale);
    void drop_copy_previous_at_playhead();
    void delete_selected_marker();
    void toggle_inherits();
    void toggle_disabled();
    // Steps the focused marker's tempo by `delta_cents` integer cents (one
    // cent per keypress, signed by direction of travel). With a 2+ selection it
    // dispatches to the all-or-nothing group step below. `synthesized_repeat` is
    // the dispatching key event's platform repeat bit, read only by the
    // undo-coalesce verdict (undo.h).
    // NEITHER ARM STOPS PLAYBACK, and that is the ruling rather than an omission:
    // the cent step is a GROUP-PRESERVING VALUE STEP, the class that plays on under
    // the edit (the keyboard stop rule, at stop_playback_if_playing's declaration in
    // playback_lifecycle.h).
    GuiOpRefusal adjust_tempo_cents(int64_t delta_cents,
                                    bool synthesized_repeat);
    GuiOpRefusal nudge_selected_markers(int direction,
                                        bool synthesized_repeat);

   private:
    // Group tempo step (architect 2026-07-23, 2+ selection): all-or-nothing.
    // Every selected marker must be a steppable OWNER not at the bracket edge in
    // the step direction, or the whole press refuses silently; then each steps
    // its own tempo_cents by delta_cents. See the definition for the wall set.
    GuiOpRefusal adjust_tempo_cents_group(int64_t delta_cents,
                                          bool synthesized_repeat);
};
