// on_key dispatch helpers. Each is a GuiInputHandler method declared in
// input_handler.h; on_key calls them in sequence (if (handle_X(...))
// return;). Grouped here to keep input_handler.cpp focused on the event
// entry points and the pointer / wheel paths.

#include "input_handler.h"

#include "file_loader.h"     // apply_settings_engine_and_prefs (shared with load)
#include "history_diff.h"
#include "paint_handler.h"
#include "render.h"
#include "render_pipeline.h"
#include "settings_io.h"
#include "text_editor.h"
#include "warpmarkers.h"

#include <signal.h>
#include <spawn.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// The child's environment for the external audio-player spawn below. POSIX
// exposes the process environment through this global; passing it as
// posix_spawnp's envp gives the launched player the GUI's own environment.
extern char** environ;

namespace {

// Launch `player` DETACHED with `wavs` as its arguments, searching $PATH so a
// bare binary name (e.g. `audacious`) works and an absolute path works too.
// Fire-and-forget: the GUI neither tracks nor waits on the child (SIGCHLD is
// SIG_IGN from startup, so it auto-reaps). The child, however, is spawned with
// SIGCHLD AND SIGPIPE RESET TO DEFAULT (SETSIGDEF), because an ignored
// disposition is the one signal state that SURVIVES exec: the parent's
// SIG_IGN on SIGCHLD would give a player that waitpid()s its own
// helper/decoder an ECHILD, breaking its sequencing, and the parent's SIG_IGN
// on SIGPIPE (added for the clipboard write, see main.cpp) would leave the
// player's own pipelines returning EPIPE where the ordinary tool contract is
// death by SIGPIPE. Both are the GUI's private arrangements and neither is
// the child's business. posix_spawnp wants a NULL-terminated
// char* const argv[]; the backing std::strings (player and the wavs vector)
// stay alive across the call, so const_cast'ing their c_str() pointers is safe
// — POSIX does not modify them. Returns true iff the spawn started.
bool spawn_audio_player(const std::string& player,
                        const std::vector<std::string>& wavs) {
    std::vector<char*> argv;
    argv.reserve(wavs.size() + 2);
    argv.push_back(const_cast<char*>(player.c_str()));
    for (const std::string& w : wavs) {
        argv.push_back(const_cast<char*>(w.c_str()));
    }
    argv.push_back(nullptr);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t def;
    sigemptyset(&def);
    sigaddset(&def, SIGCHLD);
    sigaddset(&def, SIGPIPE);
    posix_spawnattr_setsigdefault(&attr, &def);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF);

    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, player.c_str(), nullptr, &attr,
                                argv.data(), environ);
    posix_spawnattr_destroy(&attr);
    return rc == 0;
}

// Removes its directory tree when it falls out of scope, on EVERY exit — the
// refusals, the success, and a throw the allocations below could raise. Its one
// user is the load-in-place-from-a-commit path's session scratch
// (load_history_commit_in_place),
// where the same guarantee written by hand would be one `remove_all` per refusal
// arm and a leaked directory the first time an arm was added without one.
struct ScratchDirGuard {
    std::filesystem::path dir;
    explicit ScratchDirGuard(std::filesystem::path d) : dir(std::move(d)) {}
    ScratchDirGuard(const ScratchDirGuard&)            = delete;
    ScratchDirGuard& operator=(const ScratchDirGuard&) = delete;
    ~ScratchDirGuard() {
        if (dir.empty()) return;
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

}  // namespace

// The lane model's one predicate — see the declaration for the two readers and
// the rationale. A non-empty selection IS the marker lane: with a focus standing,
// the bare horizontal arrows move that MARKER and the cursor rides along.
bool GuiInputHandler::playhead_in_marker_lane() const {
    return !app.selected_markers.empty();
}

// Source-view read-only allowlist. True when key+mods is not on the allowlist
// and should be dropped.
// WHAT READ-ONLY MEANS, in one sentence: the gate blocks PERSISTENT MUTATION —
// anything that can reach DISK or a RENDER — not every write to a store. That is
// why navigation, playback, zoom, view switches and the close prompt are all
// admitted even though several of them write app state, and it is the standard
// an allowlist entry is judged against.
// Authoring-mutation chords are blocked here at the gate, not admitted for a
// deeper owner refusal: undo/redo (Ctrl+Z / Ctrl+Shift+Z), the trim gesture
// (x), Delete, and every propagate command all drop at this gate.
// Ctrl+S (save) is likewise NOT on the allowlist: read-only means no save, so
// it drops here like the authoring chords. Gesture-owned state changed in a
// locked tab (the read-only flag, trim, view state, playback speed)
// reaches disk only after unlocking (bare o) or via Ctrl+S from the writable
// tab — never by saving from the locked tab itself.
// ALL propagate commands are read-only-blocked: the copy (Ctrl+P) explicitly,
// the paste pair (Ctrl+Alt+P and Ctrl+Alt+Shift+P) structurally — their
// ctrl+alt modifier combinations match no allowlist predicate. The deeper
// owner refusals — do_undo / do_redo's per-entry target-tab check
// (undo.cpp), and the read-only drag refusals (input_pointer.cpp) — stay as
// backstops for the mouse and cross-tab paths, no longer the primary surface
// for these keyboard chords.
bool GuiInputHandler::read_only_key_blocked(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    const bool is_o =
        (key == GuiKeys::O && !ctrl && !shift && !alt);
    const bool is_play_pause = is_play_pause_key(key, mods);
    // The bare horizontal arrows step the PLAYHEAD by one painted column, and
    // they are admitted ONLY in the waveform lane, where that step is pure
    // navigation. In the MARKER lane (a non-empty selection) the very same press
    // steps the playhead AND the marker under it — a position nudge in either
    // column — which is authoring, and this
    // gate is the sole read-only defense on BOTH routes (TWO since 2026-07-29, the
    // W+target tempo-image step having been deleted with the tempo-image family —
    // see marker_drag.h; that combination is a consumed refusal now):
    // position_nudge_prologue carries
    // no internal read-only check of its own. So a locked tab holding a selection REFUSES the
    // arrows outright (a consumed no-op); it does not fall back to the
    // waveform-lane step — this project has no gesture fallbacks. Leaving the
    // marker lane is any DESELECTING route (the lane model at
    // playhead_in_marker_lane); Esc is not one of them, being unbound outside the
    // editors, the prompts, the drag swallow and the render cancel since
    // 2026-07-29.
    // NOT the audition scrub: that is the waveform one-shot POINTER press
    // (scrub_act_at — the lower-half left entry and the bare right one), a
    // different gesture on a different surface, untouched here
    // and the sole owner of the "scrub" name.
    const bool is_playhead_step =
        ((key == GuiKeys::Left || key == GuiKeys::Right) &&
         !ctrl && !shift && !alt && !playhead_in_marker_lane());
    const bool is_home_end =
        ((key == GuiKeys::Home || key == GuiKeys::End) &&
         !ctrl && !shift && !alt);
    const bool is_page_updown =
        ((key == GuiKeys::PageUp || key == GuiKeys::PageDown) &&
         !ctrl && !shift && !alt);
    const bool is_zoom_symbol =
        ((key == GuiKeys::Equal || key == GuiKeys::Minus) &&
         !ctrl && !shift && !alt);
    const bool is_zero =
        (key == GuiKeys::Digit0 && !ctrl && !shift && !alt);
    const bool is_follow =
        (key == GuiKeys::F && !ctrl && !shift && !alt);
    const bool is_center =
        (key == GuiKeys::C && !ctrl && !shift && !alt);
    // Bare `t` (the S/T audio-view switch) WRITES THE WARP STORE on one edge and
    // is still navigation-class under the persistent-mutation standard above.
    // Entering target view exits iteration mode through wipe_iter_state, which
    // clears every marker's iter bracket and pushes an undo entry — a real store
    // write in a locked tab. It conforms because iter brackets are SESSION-ONLY
    // in every direction that matters: they are never serialized (no sidecar
    // field), the push carries affects_persistence=false so it cannot dirty the
    // document, and the engine's render recipe excludes them, so the wipe can
    // reach neither disk nor a render. The gate is this route's only defense, so
    // the fact is recorded here rather than left to be re-derived.
    const bool is_sub_t =
        (key == GuiKeys::T && !ctrl && !shift && !alt);
    const bool is_sub_p =
        (key == GuiKeys::P && !ctrl && !shift && !alt);
    // Bare 1 / 2 / 3, the ABSOLUTE view selectors (S+W / T+P / T+W). They are
    // admitted for exactly the reason `t` and `p` are, and by exactly the same
    // argument: they RUN those two handlers and nothing else, so the only store
    // write they can reach is the S->T iter wipe recorded at is_sub_t above,
    // session-only in every direction that matters. Nothing new to weigh.
    const bool is_view_selector =
        ((key == GuiKeys::Digit1 || key == GuiKeys::Digit2 ||
          key == GuiKeys::Digit3) && !ctrl && !shift && !alt);
    const bool is_tab_cycle =
        (!ctrl && !alt && key == GuiKeys::Tab) ||
        (!ctrl && !alt && key == GuiKeys::IsoLeftTab);
    const bool is_ctrl_tab =
        (ctrl && !shift && !alt && key == GuiKeys::Tab);
    const bool is_ctrl_shift_tab =
        (ctrl && shift && !alt && key == GuiKeys::Tab);
    // Bare Escape only: a modified Escape carries no binding anywhere, so it has
    // nothing to be admitted FOR. WHAT BARE Esc IS ADMITTED FOR: the REGION CLEAR
    // (architect 2026-07-30 — the region is transient display scratch, and a
    // locked tab may form one by plain drag, so it must be able to drop one) and
    // the RENDER / BATCH CANCEL. Neither mutates anything persistent, so both are
    // read-only-safe like every one of Esc's bindings (the authoritative
    // enumeration is at its dispatch point in on_key, input_handler.cpp; no count
    // belongs here), and dropping Esc at this gate would break both.
    const bool is_esc =
        (key == GuiKeys::Escape && !ctrl && !shift && !alt);
    const bool is_ctrl_q =
        (ctrl && !shift && !alt && key == GuiKeys::Q);
    // Ctrl+Z (undo) and Ctrl+Shift+Z (redo) are NOT on the allowlist: they
    // drop at this gate. The old design admitted them because an undo entry
    // may target the OTHER (writable) tab, deferring the real decision to
    // do_undo / do_redo's per-entry target-tab peek. Under the gate-block,
    // undoing from a read-only tab first requires switching to the writable
    // tab (Ctrl+Tab) — accepted for gate legibility, so that authoring
    // mutations stop uniformly at the gate. The target-tab peek in undo.cpp
    // survives as a backstop for entries that outlive a mid-history lock.
    // The trim gesture (x), Delete, and the propagate copy/paste
    // chords are likewise absent (blocked here).
    return !(is_o || is_play_pause || is_playhead_step ||
             is_home_end || is_page_updown ||
             is_zoom_symbol || is_zero ||
             is_follow || is_center || is_sub_t || is_sub_p ||
             is_view_selector ||
             is_tab_cycle || is_ctrl_tab || is_ctrl_shift_tab ||
             is_esc || is_ctrl_q);
}

// -- THE HISTORY MODE'S THREE KEYS AND ITS ONE KEYBOARD ALLOWLIST -----------
//
// The mode itself, what opens and closes it and why the frozen now side is safe
// are all stated ONCE, at AppState::HistoryMode (app_state.h). What lives here
// is the mechanism.

// Leave the mode, clearing it WHOLE — the commit walk with it, so the next entry
// re-inits and measures against the state at THAT moment. The one exit owner:
// bare `/`, the load-in-place, and any future closer call this rather than clearing
// fields themselves. Idempotent, so a closer may fire with the mode already down.
void GuiInputHandler::close_history_mode() {
    if (!app.history_mode.active) return;
    // THE SESSION COUNTER SURVIVES THE RESET, alone among the fields, because it
    // counts VISITS rather than describing one: letting it fall back to zero
    // would let a close-then-open pair reissue a number the flag cache has
    // already seen, which is the very collision the counter exists to prevent
    // (a `/` off and a `/` on delivered in one dispatch batch reach the paint as
    // a single edge, with no intervening rebuild to notice `active` blinking).
    const unsigned long long generation = app.history_mode.generation;
    app.history_mode = AppState::HistoryMode{};
    app.history_mode.generation = generation;
    drop_lane_stash_across_history_edge();
    // A DISCRETE COMMAND, so FULL-WINDOW DAMAGE (the CADENCE rule's discrete
    // class): the lane swaps its whole content, the stems in the waveform swap
    // with it, and the bottom strip's modal span gives its line back. Narrow
    // damage would have to know all three, and none of them is worth a rect.
    viewport.invalidate_all();
}

// DROP THE LANE'S PUBLISHED GEOMETRY AT EVERY MODE EDGE — both stashes, at the
// entry, the exit and each commit step.
//
// The stashes (app.flag_hit_rects, app.marker_stems) are produced ONCE PER TICK
// by the flag cache's rebuild, so they legitimately run one frame behind a
// press; the product accepts that lag deliberately and documents it at the
// producer. What it cannot accept is what a MODE EDGE would otherwise do to it:
// across this one edge the entries change DOMAIN, `marker_index` meaning a store
// index on one side and an index into app.history_mode.flags on the other. A
// press landing in the frame between the edge and the next tick would read the
// old side's indices under the new side's rules — after an exit, selecting or
// landing on whatever store marker happens to share a diff flag's ordinal, or
// none at all.
//
// Clearing is the whole fix and it is the lane's own rule applied: nothing is
// clickable that is not drawn, and for that one frame the answer to every lane
// hit is "nothing", which is the correct cold answer rather than a wrong warm
// one. The cost is the same frame's stems, absent instead of stale — and the
// edge's own full-window damage is already repainting.
//
// A VIEW SWITCH INSIDE THE MODE IS DELIBERATELY NOT ONE OF THESE EDGES
// (2026-08-04, when `t` / `p` / 1 / 2 / 3 joined the keyboard allowlist), and
// the reason is the CADENCE the three edges above are fighting: each of them
// only DAMAGES (invalidate_all) and leaves republication to the next tick, which
// is what opens the frame this function closes. A view switch ends in
// kick_waveform_sync, and that route rebuilds the flag cache INLINE at its tail
// — the same call that republishes both stashes — so the new view's rects are
// already standing when the press returns and there is no frame to protect.
// Nor is there a domain change to protect against: on both sides of a view
// switch `marker_index` indexes app.history_mode.flags, the mode owning the lane
// throughout. Dropping anyway would be worse than redundant if it ran after the
// kick, erasing the geometry that kick had just published. (The LIVE lane leans
// on the very same synchronous rebuild across `p`, where its stash genuinely
// does change domain — warp store index to phase-reset store index — so the
// mode is asking no more of that route than the live columns already do.)
void GuiInputHandler::drop_lane_stash_across_history_edge() {
    app.flag_hit_rects.clear();
    app.marker_stems.clear();
}

// ENTER THE MODE ON A FRESH SESSION — the one entry owner, the mirror of
// close_history_mode. Everything about a visit is built here and nowhere else:
// a new commit walk, and a NOW SIDE CAPTURED AT THIS INSTANT, which is what
// makes the deltas describe the state the user is actually looking at.
//
// TWO CALLERS, and the second is why this is a function rather than eight lines
// inside `/`: the commit act re-enters the mode on the checkpoint it has just
// made (the mode stays open across the act by the architect's ruling), and it
// must re-enter it in EXACTLY the shape a keypress does — same walk, same index,
// same cleared focus, same lane-stash drop across the edge — or the two entries
// would be two subtly different modes.
//
// UNAVAILABLE IS A CONSUMED NO-OP: init() has already put its own one line on
// stderr naming the reason, and that is the whole story — no new UI surface, no
// red flash, and above all no half-open mode. The existing mode state, if any,
// is left untouched, because the fresh session is built beside it and only moved
// in once it is known good.
bool GuiInputHandler::open_history_mode_fresh() {
    AppState::HistoryMode fresh;
    if (!fresh.session.init(app)) return false;
    fresh.active = true;
    fresh.index  = 0;      // the newest commit
    fresh.focus  = -1;
    // EVERY ENTRY IS A NEW GENERATION, including the in-place one the commit act
    // makes. The flag cache identifies the mode's content by (active, index,
    // focus) plus this, and without it a re-entry that lands on the same index
    // with the same focus — which the commit act's re-entry always does, and
    // which `active` never going false cannot betray — is indistinguishable from
    // no change at all, leaving the previous session's diff flags on screen.
    // The bump is HERE rather than at the two call sites because this is the one
    // entry owner, so a third caller inherits it.
    fresh.generation = app.history_mode.generation + 1;
    app.history_mode = std::move(fresh);
    drop_lane_stash_across_history_edge();
    viewport.invalidate_all();
    return true;
}

// `/`, `,` and `.` — the mode's whole keyboard surface, all three BARE-EXACT.
// Returns true when the press was consumed.
//
// THE ENTRY GATES ARE POSITIONAL, NOT RE-TESTED, and that is the point: this is
// reached from on_key's main body, BELOW every gate that must refuse an entry,
// so each refusal is the existing gate's and there is no second copy to
// drift. In on_key's own order — the prompt swallow (returns unconditionally),
// the open dropdown (dropdown_key_blocked: every chord but Ctrl+Q is inert while
// a popup is up), loading-or-absent audio (returns), the editor text drag, the
// keyboard-modal editor gate (keyboard_modal_editor_active + modal_editor_key_-
// blocked, and a printable `/` is a PrintableKey, so it is not merely dropped
// but TYPED — the editor's own handler consumes it and returns above this
// point), and the drag-modal gate (any live pointer gesture swallows every key
// but Ctrl+Q). `,` and `.` inherit the identical list.
//
// ONLY `/` IS BOUND OUTSIDE THE MODE. `,` and `.` fall through to the ordinary
// dispatch when it is down, where they remain the unbound no-ops they have
// always been.
//
// THE THREE KEYS' SHAPE IS ITS OWN PREDICATE (history_mode_owns_key) because it
// has a SECOND reader: the redesign roster's mode-scoped disabled-face partition
// (history_mode_disables_button, input_pointer.cpp) asks "would this button's
// chord act in the mode", and the answer for the history button's own bare `/`
// is decided HERE — one line above the allowlist — rather than in it. Spelling
// the membership twice is exactly how that face would come to lie about the
// button that opens the view.
bool history_mode_owns_key(GuiKey key, GuiInputState mods) {
    if (mods.ctrl || mods.shift || mods.alt) return false;
    return key == GuiKeys::Slash || key == GuiKeys::Comma ||
           key == GuiKeys::Period;
}

bool GuiInputHandler::handle_history_mode_key(GuiKey key, GuiInputState mods) {
    if (!history_mode_owns_key(key, mods)) return false;

    if (key == GuiKeys::Slash) {
        if (app.history_mode.active) {
            close_history_mode();
            return true;
        }
        // ENTRY RE-INITS, always: the diff's now side is frozen at init(), so
        // the visit must measure against the state the user is looking at.
        // UNAVAILABLE IS A CONSUMED NO-OP — the entry owner reports it and this
        // arm has nothing to add.
        open_history_mode_fresh();
        return true;
    }

    if (!app.history_mode.active) return false;

    // `,` steps OLDER (further back in the walk, index+1), `.` steps NEWER
    // (index-1). Each CLAMPS at its wall as a consumed no-op — the walk has
    // ends, and running off one must not wrap or refuse loudly.
    if (key == GuiKeys::Comma || key == GuiKeys::Period) {
        const std::size_t count = app.history_mode.session.commit_count();
        const std::size_t here  = app.history_mode.index;
        std::size_t there = here;
        if (key == GuiKeys::Comma) {
            if (here + 1 >= count) return true;   // oldest already
            there = here + 1;
        } else {
            if (here == 0) return true;           // newest already
            there = here - 1;
        }
        app.history_mode.index = there;
        // THE MODE FOCUS CLEARS ON EVERY STEP: it indexes into the list the
        // step is about to replace, so carrying it would light an unrelated
        // flag — and the playhead it landed stays where it is, which is the
        // navigation the click was.
        app.history_mode.focus = -1;
        // The lane's published geometry describes the commit that is leaving —
        // same domain as the one arriving, so no index can be misread, but the
        // FRAMES behind those rects are the old commit's until the next tick
        // republishes. Dropping it makes the intervening frame answer "nothing"
        // instead of landing the playhead on a flag that is no longer shown.
        drop_lane_stash_across_history_edge();
        viewport.invalidate_all();
        return true;
    }

    return false;
}

// THE MODE'S KEYBOARD ALLOWLIST — the shape read_only_key_blocked has, and for
// the same reason: one gate with a stated membership beats twenty scattered
// refusals. True when the press is NOT admitted and should be dropped as a
// consumed no-op.
//
// WHAT IS ADMITTED, the whole list:
//   - Space (bare)          → the audition. Playback is RUNNING state, not
//                             authored state; the frozen now side cannot see it.
//   - = / - (bare)          → zoom in / out
//   - 0 (bare)              → the overview toggle
//   - PageUp/PageDown       → the paged viewport scroll
//     (bare)                  — the three above are PURE VIEWPORT MOVES, which
//                             is the mode's navigation vocabulary: the delta is
//                             laid out on the viewport, so panning and zooming
//                             it is reading it.
//   - t / p / 1 / 2 / 3     → THE VIEW SWITCHES (architect 2026-08-04, from his
//     (bare)                  first real session with the mode). THE DELTA IS
//                             VIEW-INDEPENDENT AND THE PAINTED SUBSET IS NOT,
//                             which is the whole reason they belong here: a
//                             commit's typed delta is a LINE diff over the three
//                             sidecar texts (history_diff.h), computed once and
//                             identical in every view, while the lane paints
//                             only the ACTIVE COLUMN's half of it
//                             (rebuild_history_diff_flags picks warp or
//                             phase-reset entries by active_markers_view). So
//                             switching views is how both halves of a
//                             checkpoint's delta get read, and it is a REPAINT
//                             rather than a re-init: no session work, no new
//                             walk, no re-measured now side.
//                             THEY RUN THEIR ORDINARY HANDLERS WHOLE, side
//                             effects and all, each weighed against the mode's
//                             own invariants. The S->T entry's iteration wipe
//                             writes the warp store and pushes an undo entry,
//                             and it is admitted on the READ-ONLY gate's own
//                             argument (stated at is_sub_t in
//                             read_only_key_blocked): iter brackets are
//                             session-only, serialized nowhere, so the frozen
//                             now side — which is the three sidecar TEXTS —
//                             cannot see the write at all. `p` clears the live
//                             selection and runs the coincidence auto-select;
//                             the mode neither reads nor paints that selection
//                             (the lane suppresses every live flag while it
//                             stands, and the mode's focus is its own field), so
//                             those land unseen and leave exactly the state the
//                             same press leaves outside the mode. The playhead
//                             moves `t` can make (its selection-gated re-land)
//                             are navigation, which is already this mode's
//                             vocabulary — its own diff-flag click lands the
//                             playhead too. The region clear is scratch, the
//                             playback stop is running state, and the flag-
//                             editor teardown is unreachable here (no editor can
//                             be open while this gate is reached at all — see
//                             the modal note below). The S->T tail's target
//                             PREVIEW render is derived data, not authoring
//                             state, and the mode already tolerates one: a
//                             render live from before `/` runs on, and the
//                             load-in-place's own tail triggers one from inside the
//                             mode. The synchronous plate
//                             rebuild each switch ends in is load-bearing rather
//                             than incidental: it is what republishes the lane's
//                             hit rects in the same press (drop_lane_stash_-
//                             across_history_edge states why that means a view
//                             switch is NOT one of the stash-dropping edges).
//                             WHAT THIS COSTS THE FROZEN SIDE is one more
//                             producer of SETTINGS-file drift — active_audio_-
//                             view= and active_markers_view= are persisted keys
//                             — which the commit act already answers by
//                             rebuilding the now side fresh (the drift inventory
//                             is at AppState::HistoryMode).
//                             THE VIEW BAR AND THE S/T + W/P RADIOS need no rule
//                             of their own: they synthesize these very chords
//                             through dispatch_redesign_chord, like every other
//                             redesigned button.
//   - ' (bare)              → THE LOAD EDITOR, and the mode's one admitted
//                             MUTATOR (2026-08-04). It is admitted because in
//                             the mode it loads something else in place: the editor
//                             opens prefilled with the viewed commit's SHA and
//                             loads THAT COMMIT's three sidecars in place
//                             (load_history_commit_in_place), which is the
//                             mode's own
//                             act rather than an authoring chord that would
//                             leave the frozen now side describing a state that
//                             no longer exists — and the load-in-place closes
//                             the mode
//                             as part of itself, so the mode never outlives the
//                             state it was measured against. The icon row's
//                             load button reaches it here, like every other
//                             redesigned button, by synthesizing this same bare
//                             chord.
//   - Ctrl+Alt+R (no shift) → THE COMMIT ACT, the mode's second admitted mutator
//                             (2026-08-04) and admitted on the same reasoning as
//                             `'`: in the mode that chord is not a render at all
//                             but the act of committing the live state into the
//                             projects repository as a checkpoint. THE MODE BIT
//                             SELECTS THE COMMAND — the iteration bit's own
//                             precedent — so there is ONE route and the
//                             selection sits inside it, at the chord's arm in
//                             handle_render_dispatch_keys; nothing is dispatched
//                             from here. The Render button reaches it by
//                             synthesizing this same chord, and wears the commit
//                             icon and label while the mode stands.
//                             Ctrl+Alt+SHIFT+R IS DELIBERATELY NOT ADMITTED: a
//                             miscellaneous render is an authoring act with no
//                             meaning in this mode, so it stays a consumed
//                             nothing here and the button's hint drops its shift
//                             line to match.
//   - Ctrl+S                → the save. It writes the LIVE state, which is
//                             exactly the now side the diff is measured against,
//                             so it cannot make the display disagree with disk.
//   - Ctrl+Q                → the close routing.
//   - Esc (bare)            → ITS EXISTING BINDINGS, AND NOT ONE OF ITS OWN
//                             (architect 2026-08-04, closing the arc's recorded
//                             cost). Admitting it adds NO seventh Esc place: the
//                             bare-Esc inventory is still the six enumerated at
//                             on_key's dispatch point (input_handler.cpp), and
//                             this line only lets the two that can be live in
//                             this mode run — the REGION CLEAR (a span formed
//                             before `/`; the mode's pointer allowlist admits no
//                             region former, so nothing in here can make a new
//                             one) and the RENDER / BATCH CANCEL (a render
//                             launched before `/`, whose progress line the mode's
//                             corner outranks). Both sit BELOW this gate in
//                             on_key and neither mutates authored state, so the
//                             frozen now side is untouched — the same argument
//                             the read-only allowlist admits Esc on.
//                             IT CANNOT CLOSE THE VIEW, structurally rather than
//                             by refusal: the toggle is handle_history_mode_key's
//                             and that function owns `/`, `,` and `.` alone
//                             (history_mode_owns_key), so no Esc reaches it. The
//                             view's exits are unchanged, and `/` is still the
//                             key that leaves. With no region resting and no
//                             render running a bare Esc is a consumed nothing,
//                             which is what it is everywhere else too.
//
// WHILE THAT EDITOR IS OPEN THIS GATE IS NOT REACHED AT ALL: the keyboard-modal
// editor gate sits ABOVE the mode in on_key, so the editor owns every key its
// modality owns — `/`, `,` and `.` included, which TYPE into the buffer instead
// of stepping the walk underneath it (they are printable, so the editor consumes
// them and returns above this line), exactly as they do under any other editor.
// `/`, `,` and `.` never reach here — handle_history_mode_key consumes them one
// line above.
//
// WHAT IS DELIBERATELY OUT, beyond the obvious authoring chords: the PLAYHEAD
// steps and Home/End (they move the cursor, and in the marker lane the very same
// press nudges a marker), `c` and `f` (a jump onto a live marker's focus, and a
// session-state toggle), BOTH TAB CYCLES and the A/B tab switches (Tab,
// Shift+Tab, Ctrl+Tab, Ctrl+Shift+Tab), and `o`.
//
// VIEWS ARE ADMITTED, TABS ARE NOT, and the line between them is not arbitrary:
// a view switch re-reads THE SAME piece — the same three sidecar texts the now
// side was frozen from, the same delta, another column of it — while an A/B tab
// switch swaps the per-tab band (viewport, zoom, playhead, trim, read_only) the
// session was measured with, and `c` and Tab both navigate by LIVE MARKERS,
// which the mode is not showing. The architect admitted views on 2026-08-04 and
// nothing else with them. THE TAB CYCLES STAY CONSUMED and that is ratified
// rather than pending (same day): a tab switch swaps the very per-tab band the
// session was measured with, and the tabs now WEAR the refusal — see the face
// paragraph below.
//
// THE REDESIGNED BUTTONS AND THE NAVIGATION MENU'S ITEMS PASS THROUGH HERE
// UNCHANGED, which is why they need no rule of their own: both synthesize a
// chord and call on_key (dispatch_redesign_chord and finish_dropdown_release),
// so Save, Undo, Redo, Render and the view bar drop at this gate exactly as
// their keys do. The one non-chord route out of that row — the two dropdown
// anchors — is shut at toggle_dropdown instead.
//
// AND SINCE 2026-08-04 THIS GATE IS ALSO READ BY THE FACES: a button whose chord
// this predicate blocks wears its row's DISABLED face while the mode stands and
// ignores the pointer, so the roster says what it will do rather than swallowing
// clicks silently. The partition is DERIVED from this function (and from the
// toggle_dropdown lockout for the two anchors), never hand-listed —
// history_mode_disables_button, input_pointer.cpp, which carries the whole
// inventory.
//
// THE PREDICATE IS FREE, NOT A MEMBER, for exactly that second reader: it is a
// pure function of key+mods (it always was), and the face derivation asks it
// about a table of chords with no press in hand.
bool history_mode_key_blocked(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    const bool bare  = !ctrl && !shift && !alt;
    const bool is_play_pause = is_play_pause_key(key, mods);
    const bool is_zoom_symbol =
        ((key == GuiKeys::Equal || key == GuiKeys::Minus) && bare);
    const bool is_zero  = (key == GuiKeys::Digit0 && bare);
    const bool is_page_updown =
        ((key == GuiKeys::PageUp || key == GuiKeys::PageDown) && bare);
    const bool is_load_in_place = (key == GuiKeys::Apostrophe && bare);
    const bool is_commit_act = (ctrl && alt && !shift && key == GuiKeys::R);
    const bool is_save   = (ctrl && !shift && !alt && key == GuiKeys::S);
    const bool is_ctrl_q = (ctrl && !shift && !alt && key == GuiKeys::Q);
    // THE VIEW SWITCHES, in EXACTLY the shapes the ordinary dispatch requires —
    // all three bare-exact, read off their own arms in on_key (the `t` toggle,
    // the `p` toggle, and the 1/2/3 absolute selectors, which compose those two
    // handlers and add no third route). Admitting a shape the dispatch does not
    // bind would admit a press that then does nothing, which is the allowlist
    // telling a lie about itself.
    const bool is_audio_view_switch  = (key == GuiKeys::T && bare);
    const bool is_marker_view_switch = (key == GuiKeys::P && bare);
    const bool is_view_selector =
        ((key == GuiKeys::Digit1 || key == GuiKeys::Digit2 ||
          key == GuiKeys::Digit3) && bare);
    const bool is_esc = (key == GuiKeys::Escape && bare);
    return !(is_play_pause || is_zoom_symbol || is_zero || is_page_updown ||
             is_audio_view_switch || is_marker_view_switch ||
             is_view_selector || is_esc ||
             is_load_in_place || is_commit_act || is_save || is_ctrl_q);
}

// -- THE COMMIT ACT'S GUI HALF ----------------------------------------------
//
// The act itself is commit_history_checkpoint (history_diff.h): the three
// writes, the pathspec-scoped commit, the push, and every stderr line about
// them. What lives here is the question in front of it and the re-entry behind
// it.

// ASK FIRST. One caller: Ctrl+Alt+R's own arm while the mode stands.
//
// THE QUESTION NAMES THE DEED, not a rephrasing of it — the commit message comes
// from the act's own owner, so the prompt cannot advertise a title the commit
// does not write. The message is not editable and is not asked about (the
// architect's ruling: it is derived from the piece, not chosen), so this is a
// confirmation and its answers are `y` and Esc.
//
// The guards are the act's preconditions restated as "there is something to ask
// about": no mode, or a session that never resolved a piece directory, and there
// is no commit to offer. Neither is reachable from the one call site (the chord
// is admitted only while the mode stands, and an available session always
// carries both strings), which is why they are silent.
void GuiInputHandler::open_history_commit_confirmation() {
    if (!app.history_mode.active) return;
    const std::string& dir = app.history_mode.session.project_directory();
    if (dir.empty() || app.history_mode.session.sidecar_base_name().empty()) {
        return;
    }
    prompt.open_history_commit_confirm(history_checkpoint_title(dir),
                                       app.projects_repo);
}

// THEN DO IT — the prompt's `y`, and the only caller.
//
// THE BYTES ARE REBUILT FRESH, NEVER THE SESSION'S FROZEN NOW SIDE, and this is
// the one place in the mode where the difference between them is real. The
// frozen side is honest about AUTHORED state — the mode's gates refuse every
// route that could change a marker or an engine setting — but the settings file
// also carries the per-tab VIEW BAND, and both allowlists admit routes that move
// it: zoom, the paged scroll, the overview toggle and playback's follow chase,
// the pointer's pan / strip / ruler drags, and the mode's own diff-flag click,
// which lands the playhead. Committing the frozen text
// would therefore write a checkpoint whose view band is a stale copy of one the
// user has since moved — invisible in the diff (which displays only `scale=`)
// and wrong on disk. Rebuilding costs one serialization and is exactly what a
// Ctrl+S at this instant would write.
//
// AND IT IS WHAT MAKES THE CONFIRMATION TRUE: the re-init below measures the new
// checkpoint against a now side built from the same unchanged state one moment
// later, so the lane comes back EMPTY. Committing the frozen bytes after a zoom
// would have left the fresh now side disagreeing with them, and the empty diff —
// the whole visual point of staying in the mode — would show a settings delta
// instead.
//
// THE MODE STAYS OPEN (architect's ruling) and re-enters through the entry
// owner, so the walk re-heads at the new commit and the index returns to 0. A
// re-entry that finds no history is not a state this can produce — the commit
// just landed on the branch the walk reads — so its only honest answer is to
// close the mode, which init() has already explained on stderr.
void GuiInputHandler::run_history_commit() {
    if (!app.history_mode.active) return;
    const std::string dir  = app.history_mode.session.project_directory();
    const std::string base = app.history_mode.session.sidecar_base_name();
    if (dir.empty() || base.empty()) return;

    const GuiHistoryCommitOutcome outcome = commit_history_checkpoint(
        dir, base, app.projects_repo, build_history_now_side(app));

    // A CHECKPOINT EXISTS IN BOTH SURVIVING ARMS — pushed or not — and that is
    // exactly why the walk reads the local branch: an unpushed checkpoint is
    // still history, and the user must be able to see that it landed. Every
    // other outcome leaves the walk as it was, so the mode is left alone. (Both
    // arms also cover the act's RETRY shape, where the checkpoint was already
    // committed by an earlier attempt whose transport died over it and only the
    // push was outstanding; re-heading the walk at a commit it already heads at
    // is a no-op the empty diff confirms exactly as it does a fresh one.)
    if (outcome != GuiHistoryCommitOutcome::Committed &&
        outcome != GuiHistoryCommitOutcome::CommittedNotPushed) {
        return;
    }
    if (!open_history_mode_fresh()) close_history_mode();
}

// THE OPEN DROPDOWN'S keyboard gate — ONE gate for BOTH menus, because there is
// one popup state and a dropdown is a dropdown (the Navigation menu joined
// 2026-08-02 and needed nothing here: bare Esc stays the SIXTH bare-Esc binding
// rather than becoming a seventh). Returns true when the press is SWALLOWED (the
// popup consumed it, or it was inert); false only for Ctrl+Q, which closes the
// popup and then lets on_key run the close route.
//
// Bare-exact and ctrl-exact respectively, like every other modal predicate here:
// a modified Escape and a shifted Ctrl+Q carry no binding anywhere, so they fall
// into the swallow with everything else rather than dismissing.
bool GuiInputHandler::dropdown_key_blocked(GuiKey key, GuiInputState mods) {
    const bool bare = !mods.ctrl && !mods.shift && !mods.alt;
    if (key == GuiKeys::Escape && bare) {
        close_dropdown();
        return true;
    }
    if (key == GuiKeys::Q && mods.ctrl && !mods.shift && !mods.alt) {
        close_dropdown();
        return false;   // fall through to the close route
    }
    return true;        // every other chord is inert while the popup is up
}

// The BOTTOM-STRIP modal surfaces: the settings editor, the load
// editor, and the bpm editor (top_flag_editor reused with Kind::BpmBracket,
// painted in the bottom strip) — plus the prompts, which own input through
// their own gates in on_key and the pointer handlers. Since the flag editor
// became keyboard-modal this is NO LONGER the keyboard gate's predicate (that
// is keyboard_modal_editor_active); what it still names is ONE behavior the
// top-strip FlagPayload editor is deliberately transparent to — the wheel
// swallow in wheel_context, this predicate's ONLY caller. The playback stop is
// NOT here: it has its own owner (stop_playback_for_modal_open) that the open
// sites call. Authoritative statement at the declaration in input_handler.h.
bool GuiInputHandler::modal_bottom_strip_editor_active() const {
    return text_editor::is_active(app.settings_editor) ||
           text_editor::is_active(app.load_editor) ||
           (text_editor::is_active(app.top_flag_editor) &&
            app.top_flag_editor.kind == text_editor::Kind::BpmBracket);
}

// Any text editor consuming printable keys — the two bottom-strip editors
// plus the top-strip flag editor in EITHER kind (the FlagPayload editor takes
// typed letters too). The platform layer's kLeftClickKey probe: while this is
// true that key types a normal letter rather than emulating the left button.
bool GuiInputHandler::any_text_editor_active() const {
    return text_editor::is_active(app.settings_editor) ||
           text_editor::is_active(app.load_editor) ||
           text_editor::is_active(app.top_flag_editor);
}

// Keyboard modality — see the declaration for the readers and for why the
// wheel and playback-stop readers deliberately keep the bottom-strip predicate.
// It is EXACTLY any_text_editor_active, and that identity is structural rather
// than coincidental: an editor that swallows printable letters MUST own the
// keyboard, or typing `f` into a flag would toggle follow mode. So this
// delegates instead of restating the membership — one expression, two names,
// and the two concepts can only ever be the same set.
bool GuiInputHandler::keyboard_modal_editor_active() const {
    return any_text_editor_active();
}

// Press-time key-repeat eligibility (see the declaration). Repeat serves
// held-step gestures and editor typing; edge-triggered commands never repeat.
// Eligibility is judged under the PRESS-TIME context, so a press that opens an
// editor (evaluated before the open) does not arm, while typing inside an
// already-open editor does.
bool GuiInputHandler::repeat_eligible(GuiKey key, GuiInputState mods) const {
    // A press the prompt or a live pointer gesture would swallow must not arm:
    // its owning context rejected the press, and the gate lifting later must not
    // retroactively empower the hold (e.g. a chord held through a marker drag
    // must not repeat onto the just-committed marker once the mouse releases).
    if (app.prompt.active) return false;
    if (any_pointer_gesture_active(app)) return false;
    if (any_text_editor_active()) {
        // Only the editor's motion/edit and printable-insert keys auto-repeat
        // while held; its session (bare Escape/Return) and chord (ctrl-exact
        // A/C/X/V) keys are one-shot, and NotEditorKey is not the editor's to
        // repeat — the keyboard-modal gate drops it before anything could act on
        // it anyway. An ALT-carrying motion press is in that last bucket and so
        // does not repeat, which falls out of the classifier rather than being
        // spelled here. This consumes the one editor-key owner:
        // Tab/IsoLeftTab classify as NotEditorKey (handle_key never consumes Tab
        // — the autocompletes are intercepted at the gate before handle_key) and
        // so do not repeat here, matching the prior explicit one-shot exclusion.
        const auto kc = text_editor::classify_key(key, mods);
        return kc == text_editor::KeyClass::MotionEditKey ||
               kc == text_editor::KeyClass::PrintableKey;
    }
    // Global dispatch: only the continuous step gestures repeat — the bare
    // ARROWS all four (Left/Right being the playhead step in the waveform lane
    // and the position nudge in the marker lane, Up/Down the
    // tempo cent step; the lane split is decided per fire at dispatch, so the
    // arrows repeat as one family), bare PageUp/PageDown, bare Equal/Minus zoom,
    // the marker-focus cycle (bare Tab / Shift+Tab / IsoLeftTab), and the THREE
    // repeating Ctrl chords — the Ctrl+Shift+Tab march plus Ctrl+Z / Ctrl+Shift+Z
    // (undo / redo), each a continuous step gesture like the cycle, not a
    // one-shot command (Ctrl+Tab, the A/B switch, stays one-shot). Every
    // letter, toggle, opener, other Ctrl / Ctrl+Alt chord, Space, Home/End,
    // and Delete is one-shot. No MODIFIED arrow repeats at all: the arrows carry
    // no modified binding to repeat.
    if (!mods.ctrl && !mods.shift && !mods.alt &&
        (key == GuiKeys::Left || key == GuiKeys::Right ||
         key == GuiKeys::Up || key == GuiKeys::Down ||
         key == GuiKeys::PageUp || key == GuiKeys::PageDown ||
         key == GuiKeys::Equal || key == GuiKeys::Minus))
        return true;
    // Marker-focus cycle keys auto-advance while held (fast marker walking):
    // bare Tab and Shift+Tab both cycle, and IsoLeftTab cycles shift-agnostic
    // (mirroring the dispatch arm), all requiring no ctrl/alt.
    if (!mods.ctrl && !mods.alt &&
        (key == GuiKeys::Tab || key == GuiKeys::IsoLeftTab))
        return true;
    // Ctrl+Shift+Tab exactly (the lockstep marker march) repeats too.
    if (mods.ctrl && mods.shift && !mods.alt && key == GuiKeys::Tab)
        return true;
    // Ctrl+Z / Ctrl+Shift+Z (undo / redo) repeat while held (architect
    // 2026-07-23): stepping through history is a continuous step gesture
    // like the marker march — each fire is a full command (touched-set
    // selection, offscreen-only recenter, the sync re-warp where the entry
    // demands it), costs bounded like the tempo step. The single condition
    // covers both: shift distinguishes undo from redo, and repeat recomputes
    // modifiers live, so a shift pressed mid-hold flips to redo — consistent
    // with the platform's live-modifier rule.
    if (mods.ctrl && !mods.alt && key == GuiKeys::Z)
        return true;
    return false;
}

// The KEYBOARD-MODAL editor key gate, the sibling of read_only_key_blocked's
// allowlist shape. True when key+mods is not on the allowlist and should be
// dropped. It serves ALL FOUR editors — the settings and load prompts,
// the bpm bracket, and (architect 2026-07-28) the top-strip flag editor, which
// this ruling brought under the same contract. While one is open the user can
// reach the editor itself, bare Esc (exit), Ctrl+S (save; the editor stays
// open), and Ctrl+Q (close routing) — nothing else: Space-as-playback, zoom,
// mode toggles, tab switches, undo/redo, the marker / trim chords, and the
// Ctrl+Alt render chords all drop here. "The editor
// itself" CONSUMES the one editor-key owner:
// text_editor::classify_key — the gate admits exactly the non-NotEditorKey
// set (BARE Escape/Enter, CTRL-EXACT A/C/X/V, the cursor/editing keys Left /
// Right / Home / End / BackSpace / Delete under ctrl/shift but never alt, and
// printable insertion; Space lands in the buffer as a typed character, not as
// playback). NO admitted key carries alt, on any arm.
// The strict-modifier rule therefore holds by ONE route: a press wearing a
// modifier its arm does not bind (Ctrl+Escape, Ctrl+Enter, Ctrl+Shift+V,
// Ctrl+Alt+A, Alt+Left, Ctrl+Alt+BackSpace) is NotEditorKey like any other
// unbound chord and drops right here, so it cannot cancel, commit, paste, move
// the caret, or erase. The gate-level
// carve-outs below are NOT editor consumption — they are gate policy layered on
// top: the settings/load editors' own bare-Tab value autocomplete (their
// handle_*_editor_key intercepts it before handle_key; the bpm and flag editors
// have no Tab route, so bare Tab drops while either is open), Ctrl+S (save), and
// Ctrl+Q (close routing). Admitted keys flow into the editor routing unchanged,
// so the only NotConsumed keys that can reach route_modal_editor_key's command
// tail are those last two chords.
bool GuiInputHandler::modal_editor_key_blocked(GuiKey key,
                                               GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    const bool is_editor_key =
        (text_editor::classify_key(key, mods) !=
         text_editor::KeyClass::NotEditorKey);
    const bool is_settings_autocomplete =
        (text_editor::is_active(app.settings_editor) &&
         key == GuiKeys::Tab && !ctrl && !shift && !alt);
    // The load editor's bare-Tab entry-name autocomplete
    // (handle_load_editor_key intercepts it before handle_key), the sibling
    // of the settings editor's value autocomplete.
    const bool is_load_editor_autocomplete =
        (text_editor::is_active(app.load_editor) &&
         key == GuiKeys::Tab && !ctrl && !shift && !alt);
    const bool is_save =
        (ctrl && !shift && !alt && key == GuiKeys::S);
    const bool is_ctrl_q =
        (ctrl && !shift && !alt && key == GuiKeys::Q);
    return !(is_editor_key ||
             is_settings_autocomplete || is_load_editor_autocomplete ||
             is_save || is_ctrl_q);
}

// The Esc-cancel semantics as a callable body, used by the Esc key
// handler below. Requesting cancellation has two effects:
//   1. async_renderer.request_cancel() sets the worker's cancel flag,
//      which do_render passes through to the engine.
//   2. app.queue_cancel_requested = true so that on_batch_entry_complete
//      finalizes the batch instead of dispatching the next entry.
// Both are needed: (1) interrupts the current render mid-stream;
// (2) stops the batch state machine from advancing after the
// cancelled render's on_done fires.
// Cancel also disarms both parked slots that the worker-idle pump would
// otherwise resurrect: the parked archival command (app.pending_archival)
// and the parked target preview (GuiTargetRender::pending_, cleared via
// cancel_in_flight_update).
bool GuiInputHandler::cancel_archival_session() {
    if (async_renderer.is_busy()) {
        async_renderer.request_cancel();
        app.queue_cancel_requested = true;
        // Cancel means stop rendering: a parked archival command (a
        // dispatch that killed this render and is waiting out its drain)
        // AND a parked target preview are both disarmed, or the
        // worker-idle pump (finalize_render_run to maybe_dispatch_pending)
        // would resurrect a render the moment the cancel lands. The
        // preview slot is cleared through cancel_in_flight_update, which
        // also covers the updating... progress text and the case where
        // the busy render is the preview's own.
        app.pending_archival = {};
        target_render.cancel_in_flight_update();
        return true;
    }
    if (app.queue_running) {
        // Render-state housekeeping flag survives a frame past the
        // worker's actual completion (worker_state_ transitions
        // Running -> CompletionPending while is_busy() still returns
        // true; once on_completion_event fires it goes Idle). The
        // is_busy() branch above covers that window. This branch is
        // the rare case where queue_running is set but the worker has
        // already cleared — defensive, mirrors the prior behavior.
        // Both parked slots are disarmed here too, same pump-resurrection
        // reason as the is_busy() branch.
        app.queue_cancel_requested = true;
        app.pending_archival = {};
        target_render.cancel_in_flight_update();
        return true;
    }
    return false;
}

// Esc-cancel handlers for in-flight operations. See the declaration in
// input_handler.h for routing order. This is one of the surviving bare-Esc
// bindings (the full enumeration is at its dispatch point in on_key,
// input_handler.cpp — no count belongs here) — it
// SURVIVES the 2026-07-29 Esc unbinding as its own binding class, render-work
// cancel rather than a ladder rung — ARCHITECT-CONFIRMED 2026-07-29 ("esc should
// cancel render"), no longer a planner interpretation. The whole Esc story is stated at the on_key site where the deleted
// selection/region ladder used to be dispatched.
bool GuiInputHandler::handle_escape_cancels(GuiKey key, GuiInputState mods) {
    if (key != GuiKeys::Escape) return false;
    if (mods.ctrl || mods.shift || mods.alt) return false;
    return cancel_archival_session();
}

// THE ITERATION SWEEP — the Cartesian product of the per-marker iter ranges
// authored in iteration mode. Output lands in
// `<source_parent>/renders/<N>_iterations/`, one cell per product point with
// basename `<seq>_<delta_csv>`; each cell renders one `.wav`. The CSV holds the
// swept markers' deltas in timeline order, formatted `%+0.2f`; markers with no
// iter range authored are excluded from the CSV and contribute one fixed value
// (their authored tempo_cents) to the product. Per-cell progress and Esc
// cancellation are handled by the batch runner (start_render_batch and the
// ActiveBatch lifecycle).
//
// ONE CALLER, Ctrl+Alt+R's iteration arm (architect 2026-08-02: with the mode on
// that chord IS the sweep, and the former Ctrl+Alt+I is retired). The caller has
// already established both of this body's outer facts — a non-empty
// source_audio_path and iteration mode ON — so they are not re-tested here; the
// refusals below (no brackets authored, the inverted-bracket breach, the cell
// cap) are the SWEEP'S OWN and are stated where they fire.
void GuiInputHandler::run_iteration_sweep_render() {
    // Dispatch validates nothing: the render worker's own resolve->build
    // chain is the tripwire surface (marker arrangements normalize to
    // tempo 1.00, trim never refuses). The per-cell tempo_cents mutations
    // below need no validation either — they are in-bracket by
    // construction now that the bracket rides its base (the retroactive
    // clamp, warpmarkers.h), so nothing here leans on the async stderr
    // backstop.

    // Snapshot markers in timeline order (the GuiWarpMarkers store is
    // sorted by time_frame, with ties legal). For each owning marker
    // build its per-cell delta list in integer cents: a single 0 when
    // no iter range is authored, otherwise the cents enumeration from
    // iter_start_cents to iter_end_cents inclusive. Deltas and tempos
    // share the one integer-cents domain, so the per-cell base + delta
    // below is plain integer addition — no conversion anywhere.
    const std::vector<GuiWarpMarker> base_warp_markers =
        app.warpmarkers.markers();
    std::vector<int>                  eligible_indices;
    std::vector<std::vector<int64_t>> per_marker_delta_cents;
    std::vector<bool>                 is_swept;
    for (int i = 0; i < static_cast<int>(base_warp_markers.size()); ++i) {
        const GuiWarpMarker& m = base_warp_markers[i];
        if (!iter_popup_eligible_marker(m)) continue;
        eligible_indices.push_back(i);
        const bool swept =
            m.iter_start_cents.has_value() && m.iter_end_cents.has_value();
        is_swept.push_back(swept);
        std::vector<int64_t> delta_cents;
        if (swept) {
            const int64_t start_cents = *m.iter_start_cents;
            const int64_t end_cents   = *m.iter_end_cents;
            // The editor commit enforces start <= end and the bracket is
            // session-only (wiped on mode exit), so an inverted bracket
            // here is an internal breach — refuse the dispatch loudly and
            // enqueue nothing, repairing no iter state (the state is
            // evidence; the bracket lifecycle owns wiping). Pre-mutation:
            // nothing above has touched app state or the queue.
            if (start_cents > end_cents) {
                std::fprintf(stderr,
                    "warptempo_gui: render-iterations refused: marker %d "
                    "iter bracket start exceeds end\n", i);
                return;
            }
            for (int64_t c = start_cents; c <= end_cents; ++c) {
                delta_cents.push_back(c);
            }
        } else {
            delta_cents.push_back(0);
        }
        per_marker_delta_cents.push_back(std::move(delta_cents));
    }

    bool any_swept = false;
    for (bool s : is_swept) {
        if (s) { any_swept = true; break; }
    }
    if (!any_swept) {
        std::fprintf(stderr,
            "warptempo_gui: render-iterations: No iter ranges "
            "authored; nothing to render\n");
        return;
    }

    // Cap the Cartesian product before it can overflow or exhaust
    // memory: each cell is a full archival render, so a real sweep is
    // tens to hundreds of cells. The per-axis brackets don't bound the
    // product — a handful of markers each with a wide bracket multiply
    // into billions of cells, which narrows to a negative int at the
    // reserve/enumeration site (std::length_error) or exhausts memory
    // materializing RenderRequests. Accumulate with a CHECKED product
    // that refuses the instant the running total exceeds the cap, so no
    // overflow can occur (the cap sits far below any integer boundary).
    // kMaxIterSweepCells is the architect-ruled cap.
    constexpr size_t kMaxIterSweepCells = 1000;
    size_t total_cells = 1;
    bool over_cap = false;
    for (const auto& d : per_marker_delta_cents) {
        total_cells *= d.size();
        if (total_cells > kMaxIterSweepCells) { over_cap = true; break; }
    }
    if (total_cells == 0) return;
    if (over_cap) {
        // Refuse before any allocation, batch-folder creation, request
        // materialization, or render kill/park. `total_cells` here is an
        // accurate lower bound on the true product (the running product
        // already exceeded the cap before every axis was folded in), so
        // report "more than <cap>" rather than computing the full
        // product. Iteration mode and the brackets survive for
        // correction — the wipe-and-exit tail below does not run.
        prompt.open_error_notice(
            "Iteration sweep refused: more than " +
            std::to_string(kMaxIterSweepCells) +
            " cells (cap " + std::to_string(kMaxIterSweepCells) +
            "). Narrow the marker brackets and retry.");
        return;
    }

    std::filesystem::path src(app.source_audio_path);
    std::filesystem::path src_parent = src.parent_path();
    if (src_parent.empty()) src_parent = std::filesystem::path(".");
    const std::filesystem::path queue_root = src_parent / "renders";

    // Resolve the next batch index: max+1 over `<digits>_<anything>`
    // entries (the shared renders/ batch scan).
    std::error_code ec;
    const int next_index =
        max_renders_batch_index(queue_root).max_index + 1;

    const std::string command_tag = "iterations";
    const std::filesystem::path batch_folder =
        queue_root /
        (std::to_string(next_index) + "_" + command_tag);
    // The batch folder is created BEFORE requests are built here, the
    // reverse of the bpm sweep (which creates AFTER building): the
    // iteration sweep's delta enumeration is total, so no cell can be
    // rejected and the folder can never end up empty. The bpm sweep's
    // cells can be bracket-rejected, so it creates after building to
    // avoid leaving an empty folder behind.
    std::filesystem::create_directories(batch_folder, ec);
    if (ec) {
        std::fprintf(stderr,
            "warptempo_gui: render-iterations: Could not create "
            "'%s': %s\n",
            batch_folder.string().c_str(), ec.message().c_str());
        return;
    }

    // The cap check above bounds total_cells at kMaxIterSweepCells
    // (<= 1000), so this narrowing to int is exact — no truncation and
    // no negative wrap can reach the reserve/enumeration below.
    const int total = static_cast<int>(total_cells);
    int pad_width = 1;
    for (int n = total; n >= 10; n /= 10) ++pad_width;
    if (pad_width > 9) pad_width = 9;

    // Snapshot phase resets once — every cell shares the same
    // phase reset configuration, only marker tempo_cents values
    // differ across cells.
    const std::vector<GuiPhaseResetMarker> base_phase_resets =
        app.phaseresetmarkers.markers();

    // Cartesian product enumeration. `indices[k]` holds the
    // current cell coordinate along the k-th eligible marker
    // (timeline order). Rightmost dimension increments fastest:
    // consecutive cells differ in the last marker's delta first.
    const size_t num_dims = per_marker_delta_cents.size();
    std::vector<size_t> indices(num_dims, 0);

    std::vector<RenderRequest> reqs;
    reqs.reserve(total);
    for (int cell = 0; cell < total; ++cell) {
        std::string delta_csv;
        for (size_t k = 0; k < num_dims; ++k) {
            if (!is_swept[k]) continue;
            // Signed two-decimal text straight from cents — no double
            // round-trip (format_signed_delta_cents, warpmarkers.h).
            if (!delta_csv.empty()) delta_csv += ',';
            delta_csv += format_signed_delta_cents(
                per_marker_delta_cents[k][indices[k]]);
        }

        char num_buf[16];
        std::snprintf(num_buf, sizeof(num_buf),
                      "%0*d", pad_width, cell + 1);
        std::string basename = num_buf;
        basename += '_';
        basename += delta_csv;

        std::vector<GuiWarpMarker> cell_warp_markers = base_warp_markers;
        for (size_t k = 0; k < num_dims; ++k) {
            const int mi = eligible_indices[k];
            // Per-cell tempo is a computed value, not an authored one, and
            // it needs no bracket gate HERE because it cannot leave the
            // bracket: the bracket rides its base (architect 2026-08-02 —
            // clamp_iter_bracket_to_tempo_bracket, warpmarkers.h, called
            // by both base-tempo authoring surfaces), so both endpoints
            // rest inside [kTempoMinCents - base, kTempoMaxCents - base]
            // and every cell between them lands in the tempo bracket. No
            // downstream backstop is load-bearing for this sum. Base and
            // delta live in the one integer-cents domain, so the sum is
            // plain integer addition and the cell sidecar's N.NN spelling
            // re-parses to exactly this value — render-entry promotion
            // (the `'` load-in-place) stays closed under the grammar by type AND
            // by VOCABULARY: the cell values a sweep can write are exactly
            // the values the strict sidecar parse accepts.
            cell_warp_markers[mi].tempo_cents =
                base_warp_markers[mi].tempo_cents +
                per_marker_delta_cents[k][indices[k]];
            // The engine doesn't consume iter values; clear them
            // so the request is quiet.
            cell_warp_markers[mi].iter_start_cents.reset();
            cell_warp_markers[mi].iter_end_cents.reset();
        }

        RenderRequest req = build_render_request(
            app.source_audio_path, std::move(cell_warp_markers), base_phase_resets,
            app.engine_settings,
            app.trim.begin_frame, app.trim.end_frame,
            batch_folder.string(), std::move(basename));
        req.authoring = snapshot_current_authoring_state();
        attach_shared_render_resources(req);
        reqs.push_back(std::move(req));

        // Increment rightmost dimension; carry left on overflow.
        // The last cell leaves indices in an overflowed state but
        // the loop exits before that's read.
        for (int k = static_cast<int>(num_dims) - 1; k >= 0; --k) {
            ++indices[k];
            if (indices[k] < per_marker_delta_cents[k].size()) break;
            indices[k] = 0;
        }
    }

    // The batch's DISPLAY label — the progress parenthetical and the
    // stderr summary. It stays LOWERCASE because it is a shared
    // ROUTING/CATEGORY LABEL rather than sentence-initial prose in either
    // surface: it sits inside "Rendering N of M (...)..." in the GUI, and
    // in the summary it fills the tag slot ahead of the message proper,
    // whose own first word takes the capital ("warptempo_gui: render
    // iterations: Rendered 3 of 8 entries"). Its position after the
    // "warptempo_gui: " prefix is NOT the reason — the 2026-08-02
    // terminal pass looks past the program-name prefix when it locates
    // that first prose word. Contrast the BPM batch's label, which
    // capitalizes as an acronym everywhere.
    if (async_renderer.is_busy()) {
        // A render dispatch kills the running render. Park the fully
        // built batch for the worker-idle pump.
        AppState::PendingArchivalCommand cmd;
        cmd.reqs        = std::move(reqs);
        cmd.batch_label = "render iterations";
        kill_running_render_and_park(std::move(cmd));
    } else {
        start_render_batch(std::move(reqs), "render iterations");
    }
    // The sweep is committed to run either way (dispatched, or parked
    // behind the killed render's drain): iteration mode turns off after
    // fire, and exiting the mode IS the bracket clear (wipe_iter_state,
    // the chokepoint every other iter-mode exit runs). Safe here: every
    // request above carries its own per-cell marker copies, so nothing
    // dispatched reads the live iter fields.
    flag_editor.wipe_iter_state();
    app.iteration_mode_enabled = false;
    viewport.invalidate_top_strip();
}

// Render-trigger chords. See the declaration for the chord list.
bool GuiInputHandler::handle_render_dispatch_keys(GuiKey key,
                                                  GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    // Ctrl+Alt+R: single render into the source directory using `title`
    // from settings. Empty batch_folder/batch_basename selects the
    // source-directory naming convention inside do_render. A successful
    // sibling wav publish emits
    // the .fingerprint sidecar, but not batch-only sidecars
    // (.warpmarkers / .phaseresetmarkers / .settings).
    // Title-not-set is a hard error surfaced from do_render.
    //
    // TWO MODE BITS RE-AIM THIS CHORD, and they are the same idea twice: the
    // command is selected by a mode, inside this one route, so no surface that
    // reaches the chord needs to know which command it currently is.
    //
    // THE HISTORY MODE COMES FIRST (architect 2026-08-04) because it is the
    // outer mode: while it stands, Ctrl+Alt+R is THE COMMIT ACT — write the live
    // state into the projects repository as a checkpoint — and the mode's own
    // keyboard allowlist is what admits the chord here at all. It outranks the
    // iteration bit unconditionally, including in the state where the mode was
    // opened with iteration mode already on (nothing can toggle that bit while
    // the mode stands, `i` not being on the allowlist). The act confirms through
    // a prompt before it touches anything; run_history_commit owns the sequence.
    //
    // ITERATION MODE RE-AIMS IT OTHERWISE (architect 2026-08-02): with that mode
    // on, Ctrl+Alt+R IS the iteration sweep — the same body, the same output
    // under renders/, the same refusals — and there is no second chord for it.
    // The single render below is the both-modes-off meaning, unchanged. Target
    // view needs no clause of its own: mode-off-in-target is an invariant (the
    // S->T toggle wipes iteration mode through wipe_iter_state), so a
    // target-view press always takes the single-render arm exactly as it always
    // did.
    if (ctrl && alt && !shift &&
        key == GuiKeys::R) {
        if (app.source_audio_path.empty()) return true;
        if (app.history_mode.active) {
            open_history_commit_confirmation();
            return true;
        }
        if (app.iteration_mode_enabled) {
            run_iteration_sweep_render();
            return true;
        }

        // Dispatch validates nothing: the render worker's own resolve->build
        // chain is the tripwire surface (the resolver normalizes ambiguous
        // marker arrangements to tempo 1.00, and trim never refuses — crossed
        // cannot rest, an ambiguous trim falls back to untrimmed inside
        // do_render, maps ignore trim), with its stderr as the backstop.

        // Empty batch_folder/basename selects the source-dir naming
        // convention inside do_render.
        RenderRequest req = build_render_request(
            app.source_audio_path, app.warpmarkers.markers(),
            app.phaseresetmarkers.markers(), app.engine_settings,
            app.trim.begin_frame, app.trim.end_frame);
        req.authoring = snapshot_current_authoring_state();
        attach_shared_render_resources(req);

        if (async_renderer.is_busy()) {
            // A render dispatch kills the running render. Park this command;
            // the worker-idle pump dispatches it once the cancellation drains.
            AppState::PendingArchivalCommand cmd;
            cmd.single      = true;
            cmd.reqs.push_back(std::move(req));
            kill_running_render_and_park(std::move(cmd));
            return true;
        }

        // The dispatch hands the request to the worker thread; on_done
        // fires on the GUI thread when the render finishes (success,
        // failure, or cancel).
        dispatch_single_archival_render(std::move(req));
        return true;
    }

    // Ctrl+Alt+Shift+R (miscellaneous render): render the current authoring
    // state — the SAME recipe Ctrl+Alt+R captures (live stores + the active
    // tab's trim) — into a numbered cell inside a `_miscellaneous` batch folder
    // under renders/. This moved off its former `e`-based chord because `e` is
    // now the click key (kLeftClickKey), so an e-chord is swallowed at the
    // platform boundary and can never reach dispatch as a command.
    // This is Ctrl+Alt+R with an extra mkdir and a different output
    // location: no queue, no batch runner, one request through the same
    // single-dispatch path. Folder logic (in allocate_miscellaneous_cell):
    // look at the most-recent folder BY INDEX in renders/; if it is a
    // `_miscellaneous` folder, append into it; otherwise (or renders/
    // empty/missing) create `<max+1>_miscellaneous`. The cell is the next
    // `<N>.wav` inside that folder. Because the target is a batch folder,
    // do_render writes the FULL entry sidecar set (.warpmarkers /
    // .phaseresetmarkers / .settings / .fingerprint), so each misc cell is a
    // first-class `l`-auditionable entry that `'` loads in place. Repeat presses
    // with unchanged state are DELIBERATE — each is an explicit command that
    // produces one more cell; identical bytes come cheap from do_render's reuse
    // rungs (render_cache, then the on-disk artifact against its .fingerprint).
    //
    // The AUTHORING recipe (markers, settings, trim, snapshot, resources) is
    // frozen here at command time; only the OUTPUT naming (batch_folder /
    // batch_basename) is late-bound, at dispatch-to-worker time, on BOTH
    // routes. Late binding is load-bearing on the busy route: the running
    // render this command kills can still publish into renders/ during its
    // cancellation drain (after any command-time scan but before the cancel
    // flag lands, through do_render's reuse-rung renames), so a cell name
    // scanned at command time could be stolen and then overwritten — two
    // successful publications collapsing to one pathname. Allocating only
    // once the worker is confirmed idle makes the scan exact: idle drains the
    // whole CompletionPending interval, so worker publication is fully done
    // before the scan, and every other renders/ mutation (batch-folder
    // creation, the load-in-place wipe) runs on this same GUI thread, so none can
    // interleave with it. The idle route allocates here inline for the same
    // one implementation.
    //
    // ARCHIVAL IS A PLAIN-MODE ACT (architect 2026-08-02): while iteration mode
    // is on this chord is a CONSUMED NO-OP. The refusal lives here, inside the
    // route, rather than at any of the surfaces that reach it — so the keyboard
    // press and the Render button's shift press are one refusal, not two. The
    // button's face follows the same bit (its hint drops the shift line in
    // iteration mode; redesign_button_tooltip, app_state.h), so nothing
    // advertises a press this arm swallows.
    //
    // WHILE THE HISTORY MODE STANDS THIS CHORD NEVER ARRIVES — the mode's
    // keyboard allowlist admits Ctrl+Alt+R and not its shifted twin, so the
    // press is consumed a gate above and this arm is not reached from either
    // surface. The Render button's hint drops its shift line there too, by the
    // same rule and at the same table.
    if (ctrl && alt && shift &&
        key == GuiKeys::R) {
        if (app.source_audio_path.empty()) return true;
        if (app.iteration_mode_enabled) return true;

        // Dispatch validates nothing (same as Ctrl+Alt+R): the render worker's
        // own resolve->build chain is the tripwire surface.

        // Build EXACTLY the Ctrl+Alt+R request; batch_folder/basename stay
        // empty here and are assigned at dispatch-to-worker time.
        RenderRequest req = build_render_request(
            app.source_audio_path, app.warpmarkers.markers(),
            app.phaseresetmarkers.markers(), app.engine_settings,
            app.trim.begin_frame, app.trim.end_frame);
        req.authoring = snapshot_current_authoring_state();
        attach_shared_render_resources(req);

        // Same single-dispatch path as Ctrl+Alt+R: kill the running render and
        // park (newest-wins) when busy, else dispatch now.
        if (async_renderer.is_busy()) {
            AppState::PendingArchivalCommand cmd;
            cmd.single        = true;
            cmd.miscellaneous = true;   // late-bind the cell at the pump
            cmd.reqs.push_back(std::move(req));
            kill_running_render_and_park(std::move(cmd));
            return true;
        }
        std::string folder, basename;
        if (!allocate_miscellaneous_cell(folder, basename)) {
            // Folder creation failed; the stderr line is already printed.
            return true;
        }
        req.batch_folder   = std::move(folder);
        req.batch_basename = std::move(basename);
        dispatch_single_archival_render(std::move(req));
        return true;
    }

    // The BPM sweep render fires from render_bpm_sweep(), triggered by Enter
    // in the bottom-strip BPM editor after a successful commit; there is no
    // key-dispatch handler for it here.

    return false;
}

// Identify a render entry by its path relative to renders/ —
// `<batch_dir>/<basename>.wav` — always folder-qualified. One path per file,
// so the id is unique by filesystem construction; Tab autocomplete then
// discriminates on the short leading batch-folder name instead of deep value
// decimals inside near-identical cell basenames, and the painted
// `Load: ./renders/<id>` line is the entry's real on-disk path. The `'`
// load editor resolves the typed identifier against these strings.
static std::string render_entry_id(const AppState::RenderEntry& e) {
    return e.batch_folder.filename().string() + "/" + e.basename + ".wav";
}

// -- Standalone render-entry load-in-place (the `'` load editor) ------
//
// Load render entry `e`'s frozen sidecar recipe in place as the new authoring
// baseline, view-agnostic: callable from source OR target authoring view. It
// takes an explicit entry, and the caller owns the visible refusal (the `'`
// editor red-flashes).
//
// Reads-then-checks BEFORE any mutation: the entry wav must exist and all
// three sidecars (.settings, .warpmarkers, .phaseresetmarkers) must read and
// validate. On ANY failure — the running-batch self-guard, a missing wav, or a
// malformed / unreadable sidecar — return false with NO state mutation, so a
// failure leaves authoring untouched. THE GENUINE-FAILURE ARMS NAME THEIR CAUSE
// ON STDERR (architect 2026-08-02), one line each with the offending path, since
// a trusted sidecar failing to read is a real fault the user cannot diagnose
// from a flash; first-error-only holds by construction (each arm returns). The
// caller's own unknown-id refusal — a typed identifier matching no entry — stays
// SILENT: a typo is not a fault, and the flash is the whole answer. Returns true
// after the recipe is applied and renders/ wiped.
bool GuiInputHandler::load_render_entry_in_place(
        const AppState::RenderEntry& e) {
    // Self-guard on the standalone mutator: a successful load-in-place wipes
    // renders/,
    // which must never race a batch publishing into it. The `'` opener
    // already refuses on this same condition, so the keyboard route never
    // reaches here; this backstop protects any other caller.
    if (app.queue_running || app.pending_archival.armed) {
        std::fprintf(stderr,
            "warptempo_gui: Load in place refused: a render batch is running or an "
            "archival is armed\n");
        return false;
    }

    // NOT a modal open, so NOT the modal-open owner's business
    // (stop_playback_for_modal_open belongs to the sites that open a surface):
    // this is the standalone mutator's own self-guard. The `'` editor's open
    // already froze playback through that owner on the keyboard route; stopping
    // again here keeps the mutator correct from any caller.
    playback_lifecycle.stop_playback_if_playing();

    // -- Read + validate every input BEFORE touching a store. --
    std::error_code ec;
    if (!std::filesystem::is_regular_file(e.wav_path, ec)) {
        std::fprintf(stderr,
            "warptempo_gui: Load in place refused: entry WAV missing or not "
            "a regular file: '%s'\n",
            e.wav_path.string().c_str());
        return false;
    }

    const std::filesystem::path sidecar = renders_dir.settings_path(e);
    const auto settings = read_settings_file(sidecar.string());
    if (!settings) {
        std::fprintf(stderr,
            "warptempo_gui: Load in place refused: invalid settings in '%s': %s\n",
            sidecar.string().c_str(), settings.error().c_str());
        return false;
    }

    std::vector<GuiWarpMarker>       src_warp;
    std::vector<GuiPhaseResetMarker> src_phase_resets;
    {
        GuiWarpMarkers m;
        const std::filesystem::path wm =
            e.batch_folder / (e.basename + ".warpmarkers");
        auto r = m.load(wm.string());
        if (!r) {
            std::fprintf(stderr,
                "warptempo_gui: Load in place refused: invalid warp markers in "
                "'%s': %s\n",
                wm.string().c_str(), r.error().c_str());
            return false;
        }
        src_warp = m.markers();
    }
    {
        GuiPhaseResetMarkers t;
        const std::filesystem::path tm =
            e.batch_folder / (e.basename + ".phaseresetmarkers");
        auto r = t.load(tm.string());
        if (!r) {
            std::fprintf(stderr,
                "warptempo_gui: Load in place refused: invalid phase reset "
                "markers in '%s': %s\n",
                tm.string().c_str(), r.error().c_str());
            return false;
        }
        src_phase_resets = t.markers();
    }

    // Every input is in hand and valid. Apply the recipe wholesale. The commit
    // tab is the tab the entry was dispatched from; its view-state band carries
    // the recipe trim that shaped this render.

    // THE `/` HISTORY MODE ENDS HERE, on the first line past the last refusal
    // and before the first store write. It is the one route in the product that
    // replaces the authored state the mode's frozen now side was measured
    // against, so leaving the mode standing would leave every flag in the lane
    // describing a session that no longer exists. Placed at the MUTATOR rather
    // than at the `'` key because this function is what performs the replacement
    // — the opener is blocked by the mode's keyboard allowlist today, so the
    // keyboard route cannot reach here at all, and the close belongs with the
    // act rather than with one of its callers.
    close_history_mode();

    const char load_tab = settings->active_tab_view;

    std::vector<GuiWarpMarker>       warp_pre  = app.warpmarkers.markers();
    std::vector<GuiPhaseResetMarker> phase_reset_pre =
        app.phaseresetmarkers.markers();

    app.warpmarkers.markers_mut()       = std::move(src_warp);
    app.phaseresetmarkers.markers_mut() = std::move(src_phase_resets);
    // Wholesale authoring reset: the ONE selection goes, and there is nothing
    // else to reset — no per-tab per-mode slot holds a copy (the parked
    // selections died 2026-07-29, so a wholesale store replace no longer has to
    // hunt down stale index sets in either ViewState).
    selection.clear_selection();

    // One cross-file undo entry: the marker pair plus the outgoing engine
    // settings (captured inside push_undo_both). The inherited prefs and view
    // state ride OUTSIDE undo — the same convention that keeps view state and
    // trim out of history.
    const char load_marker_mode = app.active_markers_view;
    undo.push_undo_both(std::move(warp_pre), std::move(phase_reset_pre),
                        load_marker_mode, load_tab);
    undo.recompute_dirty();

    const std::filesystem::path src(app.source_audio_path);
    std::filesystem::path src_parent = src.parent_path();
    if (src_parent.empty()) src_parent = std::filesystem::path(".");
    const std::filesystem::path renders_root = src_parent / "renders";

    // Wholesale authoring reset: clear every marker's session-only iteration
    // state and the bpm state, and turn off both sweep modes' visibility.
    {
        auto& mv = app.warpmarkers.markers_mut();
        for (auto& m : mv) {
            m.iter_start_cents.reset();
            m.iter_end_cents.reset();
        }
    }
    flag_editor.wipe_bpm_state();
    app.iteration_mode_enabled = false;
    app.bpm_mode_enabled       = false;

    // Both tab bands from the file (view_state_from_settings_tab: viewport /
    // zoom / playhead, read_only, and the trim pair — the whole of a band, since
    // a ViewState parks nothing index-shaped). This clean
    // whole-band replace is equivalent to a source load's per-key apply plus
    // trim plus read_only for an all-keys render-entry sidecar.
    app.tab_a = view_state_from_settings_tab(settings->tab_a);
    app.tab_b = view_state_from_settings_tab(settings->tab_b);
    // Engine block plus the scalar session prefs, VALUES ONLY, through the one
    // routine a source load also calls — so the load-in-place applies
    // engine_settings,
    // follow, active_audio_view, active_markers_view, active_tab_view,
    // playback_speed, gui_scale, audio_player and projects_repo 1:1 with
    // load.
    // There is NO
    // W/P carve-out: active_markers_view is now applied from the file like
    // every other key. The one selection was cleared above, so landing on the
    // file's marker mode carries an empty selection, exactly as a fresh load's
    // empty-selection state.
    //
    // This replaces the four LIVE env hashes with the entry's. The hashes are
    // history-less, no-dirty GUI-kind state (like the other loaded-in-place view
    // prefs), so this replacement marks nothing dirty on its own; the
    // load-in-place is dirty via its cross-file history push regardless, and
    // the loaded-in-place hashes
    // ride the next ordinary Ctrl+S.
    apply_settings_engine_and_prefs(app, *settings);

    // Clamp both loaded-in-place tab bands' playheads into the live domain (the
    // shared chokepoint, clamp_playhead_to_live_domain), mirroring the source
    // load's tab-snapshot clamp at the same point in the sequence: the
    // loaded-in-place S/T domain is computable here (active_audio_view and the
    // markers/engine settings the target total derives from are all applied
    // above; one global domain, one total clamps both). Entry sidecars are
    // trusted (written once at dispatch from an in-domain live state), so
    // this is a no-op there — it keeps the load-in-place 1:1 with a source load of
    // the same sidecars, which clamps at this point too.
    app.tab_a.playhead_cursor_sample = clamp_playhead_to_live_domain(
        app.tab_a.playhead_cursor_sample, app, audio);
    app.tab_b.playhead_cursor_sample = clamp_playhead_to_live_domain(
        app.tab_b.playhead_cursor_sample, app, audio);

    // Activate the file's tab band. active_tab_view was just set by the shared
    // routine (== load_tab) and both bands are already the file's, so pull
    // the live fields straight from the active band with no double-apply (NOT
    // switch_active_tab_view_to).
    {
        const ViewState& band = (app.active_tab_view == 'B')
                                ? app.tab_b : app.tab_a;
        app.viewport_start_sample  = band.viewport_start_sample;
        app.zoom_level             = band.zoom_level;
        // Already clamped into the live domain by the band clamp above, so
        // the live copy is in [0, total - 1] by construction.
        app.playhead_cursor_sample = band.playhead_cursor_sample;
        app.trim                = band.trim;
    }

    // Caller-side side effects the shared routine deliberately omits, run after
    // the live band is in place — the same order and the same point a source
    // load runs them: push the speed to the engine and the gui scale to the
    // renderer (the one scale axis the lane table reads), then the
    // geometry-and-cache rebuild on_resize performs.
    playback.set_speed(app.playback_speed);
    set_gui_scale_percent(app.gui_scale);
    paint_handler.on_resize(app.width, app.height);

    clamp_viewport_start(app, audio);
    // COINCIDENCE AUTO-SELECT, the load-in-place chokepoint (the rule, the
    // formula and the
    // authoritative call-site inventory live at auto_select_marker_at_playhead,
    // input_pointer.cpp / input_handler.h). The load-in-place
    // is specified 1:1 with a source load and a load runs this, so it runs here too
    // (architect 2026-07-30, closing the one entry route that honored a stored
    // playhead and withheld the recovery). PLACED HERE, at the tail: the wholesale
    // store replacement, the engine/prefs apply that sets active_audio_view and
    // active_markers_view, both band clamps and the live-band pull have all run, so
    // the scan reads the column the session actually lands in and the playhead's
    // final value — the load chokepoint's placement mirrored. Nothing downstream
    // writes the selection (the wholesale clear is far above), so the single-select
    // it may make is what rests. Its narrow damage is superseded by the kick below
    // and by the tail's full-window invalidate.
    auto_select_marker_at_playhead(app, audio, selection, viewport);
    viewport.kick_waveform_sync();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();

    // The tail's trigger owns the rebind for a 'T' landing: it marks the
    // buffer stale and dispatches the loaded-in-place state's target preview,
    // which rebinds
    // playback on completion.
    target_render.trigger();

    // Wipe renders/ AFTER the successful load-in-place. The loaded render survives
    // through the render cache, not as a folder artifact.
    if (std::filesystem::is_directory(renders_root, ec)) {
        std::filesystem::remove_all(renders_root, ec);
        if (ec) {
            std::fprintf(stderr,
                "warptempo_gui: load-in-place: Wipe failed for '%s': %s\n",
                renders_root.string().c_str(), ec.message().c_str());
        }
    }

    std::fprintf(stderr,
        "warptempo_gui: load-in-place: Loaded render in place and wiped renders/\n");
    gui.invalidate_region(0, 0, app.width, app.height);
    return true;
}

// -- Load-in-place from a COMMIT (the `'` editor in the `/` history mode) --
//
// WHAT IT IS: the same act load_render_entry_in_place performs, with the committed
// history as its source instead of a render entry. `spelling` is whatever the
// user left in the load editor — the viewed commit's SHA the opener prefilled,
// or any other spelling git can resolve (a short SHA pasted out of GitHub's web
// UI is the ruled use case). ONE STATE IN, ONE STATE OUT: the three sidecars
// THAT commit carried become the live session, in memory, and the disk is never
// touched — not the corpus, not the working sidecars, not renders/.
//
// WHAT RESOLVES: read_commit_sidecars (history_diff.h) runs the rev-parse and
// reads the three blobs out of that commit's own tree by base name, so a commit
// from before a corpus rename reads with no era knowledge. The base name is the
// mode's session's, which is why this route is gated on the mode standing.
//
// WHAT GATES, all of it BEFORE any store is touched — the validate-before-mutate
// contract load_render_entry_in_place states and this path mirrors: an unresolvable
// spelling, a commit missing ANY of the three sidecars, and a sidecar that the
// STRICT WHOLE-FILE LOADERS refuse. That last one is the point rather than a
// side effect: a commit from the legacy MM:SS.mmm era, or one carrying a
// settings key this build no longer knows, fails HERE and changes nothing —
// exactly the parse-gating the architect ruled, and the reason no second, looser
// grammar is written anywhere on this path. Each refusal is one stderr line
// naming its cause with the committed path and the SHA, first-error-only by
// construction (every arm returns), and the caller keeps the editor open with
// its red flash.
//
// THE WAV IS NOT COMPARED, and there is nothing to compare it to: the corpus
// stores the three sidecars and no audio at all, so the LOADED SOURCE IS THE
// SOURCE — this loads a recipe in place for the file already open, exactly as
// the mode's
// diff measures a commit against the session for that same file. The render-entry
// load-in-place's wav-existence check has no counterpart here.
//
// NO RUNNING-RENDER GUARD, deliberately. load_render_entry_in_place's self-guard
// protects ITS TAIL — the renders/ wipe, which must never race a batch
// publishing into that directory — and this path has no tail to protect: it
// wipes nothing and reads no render entry. A render dispatched BEFORE the mode
// opened (the mode blocks the launchers, not a render already in flight) renders
// from the request snapshot it was built with, publishes into renders/ and the
// render cache, and is untouched by and untouching of this act; its entries then
// describe the state from before the load-in-place exactly as they do after
// any other authoring
// edit.
//
// WHAT IS APPLIED is load_render_entry_in_place's own sequence, whole: the wholesale
// store replace with its selection clear, ONE cross-file undo entry with dirty
// set (auditioning the loaded-in-place state and Ctrl+Z-ing back out is the
// point of the
// feature), the iteration/bpm session reset, both tab bands, the values-only
// engine-and-prefs apply a source load shares, the two band clamps, the live
// band pull, the three caller-side side effects, the coincidence auto-select and
// the target preview trigger. Note that the prefs apply includes
// `projects_repo`, 1:1 with a load: a commit whose settings named a different
// projects home installs that answer too, and the next `/` reads it.
//
// AND THE MODE CLOSES, at the first line past the last refusal — the placement
// load_render_entry_in_place states and for its reason: this is the other route that
// replaces the very state the frozen now side was measured against, so leaving
// the mode standing would leave every flag in the lane describing a session that
// no longer exists.
bool GuiInputHandler::load_history_commit_in_place(const std::string& spelling) {
    // The mode is the route's precondition, not a courtesy: the sidecar base
    // name comes from the session (init() owns that derivation), and the close
    // below is part of the act.
    if (!app.history_mode.active) return false;
    const std::string base_name =
        app.history_mode.session.sidecar_base_name();

    GuiHistoryCommitSidecars snap;
    std::string              reason;
    // The session's matched directory goes with the spelling: it is what settles
    // a commit whose tree carries this base name in more than one place (an older
    // era's copy of another piece), and an unsettleable one refuses rather than
    // acting on a guess.
    if (!read_commit_sidecars(spelling, base_name,
                              app.history_mode.session.project_directory(),
                              snap, reason)) {
        std::fprintf(stderr, "warptempo_gui: Load in place refused: %s\n",
                     reason.c_str());
        return false;
    }

    // A PARTIAL COMMIT IS A REFUSAL. The mode's DISPLAY treats a sidecar the
    // commit lacks as "everything added" — the natural line-diff answer — but a
    // load-in-place is a whole-state replace, and inheriting two files from
    // the commit
    // and the third from nowhere would compose a state no checkpoint ever was.
    auto missing = [&](const char* ext) {
        std::fprintf(stderr,
            "warptempo_gui: Load in place refused: commit %s carries no '%s%s'\n",
            snap.sha.c_str(), base_name.c_str(), ext);
        return false;
    };
    if (snap.warpmarkers.path.empty())       return missing(".warpmarkers");
    if (snap.phaseresetmarkers.path.empty()) return missing(".phaseresetmarkers");
    if (snap.settings.path.empty())          return missing(".settings");

    // THE COMMITTED BYTES REACH THE LOADERS THROUGH A SCRATCH DIRECTORY, because
    // all three whole-file entry points take a PATH and open the file themselves
    // (read_settings_file, GuiWarpMarkers::load, GuiPhaseResetMarkers::load) and
    // all three live in the FROZEN parser, so there is no string-shaped entry to
    // hand a blob to. The alternative — a GUI-side scanner over the strings —
    // would be a SECOND GRAMMAR beside the strict one, which is precisely what
    // this gate exists to avoid; staging the bytes is the cheap way to keep the
    // loaders themselves as the only judges.
    //
    // THE DIRECTORY IS THE SESSION'S OWN SCRATCH: the system temp dir, one
    // per-process per-commit subdirectory, removed on every exit by the guard.
    // NEVER the repository (this feature only ever reads it) and NEVER beside the
    // source (the working sidecars are the user's, and a read must not write
    // near them).
    std::error_code   ec;
    const std::string leaf = "warptempo_gui-load-in-place-" +
                             std::to_string(static_cast<long>(::getpid())) +
                             "-" + snap.sha.substr(0, 7);
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path(ec) / leaf;
    if (ec) {
        std::fprintf(stderr,
            "warptempo_gui: Load in place refused: no temporary directory "
            "available: %s\n", ec.message().c_str());
        return false;
    }
    ScratchDirGuard guard(scratch);
    std::filesystem::create_directories(scratch, ec);
    if (ec) {
        std::fprintf(stderr,
            "warptempo_gui: Load in place refused: could not create '%s': %s\n",
            scratch.string().c_str(), ec.message().c_str());
        return false;
    }

    // Staged under the sidecar's own leaf name, so the loaders see exactly the
    // filename shape they see beside a source. The stderr on failure names the
    // COMMITTED path, never the scratch one: the scratch is an implementation
    // detail of this call and nothing the user can act on.
    auto stage = [&](const GuiHistorySidecarBlob& blob, const char* ext,
                     std::filesystem::path& out_path) {
        out_path = scratch / (base_name + ext);
        if (atomic_write_string_to_path(out_path.string(), blob.text)) {
            return true;
        }
        std::fprintf(stderr,
            "warptempo_gui: Load in place refused: could not stage '%s' "
            "from commit %s\n", blob.path.c_str(), snap.sha.c_str());
        return false;
    };
    std::filesystem::path settings_file, warp_file, phase_reset_file;
    if (!stage(snap.settings, ".settings", settings_file))       return false;
    if (!stage(snap.warpmarkers, ".warpmarkers", warp_file))     return false;
    if (!stage(snap.phaseresetmarkers, ".phaseresetmarkers",
               phase_reset_file))                                return false;

    // -- Read + validate every input BEFORE touching a store. The three loaders
    //    and their order are the render-entry load-in-place's, and the
    //    refusal text carries
    //    the SHA the render-entry load-in-place has no need of.
    const auto settings = read_settings_file(settings_file.string());
    if (!settings) {
        std::fprintf(stderr,
            "warptempo_gui: Load in place refused: invalid settings in '%s' "
            "at commit %s: %s\n",
            snap.settings.path.c_str(), snap.sha.c_str(),
            settings.error().c_str());
        return false;
    }

    std::vector<GuiWarpMarker>       src_warp;
    std::vector<GuiPhaseResetMarker> src_phase_resets;
    {
        GuiWarpMarkers m;
        auto r = m.load(warp_file.string());
        if (!r) {
            std::fprintf(stderr,
                "warptempo_gui: Load in place refused: invalid warp markers "
                "in '%s' at commit %s: %s\n",
                snap.warpmarkers.path.c_str(), snap.sha.c_str(),
                r.error().c_str());
            return false;
        }
        src_warp = m.markers();
    }
    {
        GuiPhaseResetMarkers t;
        auto r = t.load(phase_reset_file.string());
        if (!r) {
            std::fprintf(stderr,
                "warptempo_gui: Load in place refused: invalid phase reset "
                "markers in '%s' at commit %s: %s\n",
                snap.phaseresetmarkers.path.c_str(), snap.sha.c_str(),
                r.error().c_str());
            return false;
        }
        src_phase_resets = t.markers();
    }

    // Every input is in hand and valid; nothing below refuses.

    // NOT a modal open, so NOT the modal-open owner's business — the standalone
    // mutator's own self-guard, exactly as the render-entry load-in-place
    // spells it. The `'`
    // editor's open already froze playback through that owner on the keyboard
    // route; stopping again here keeps the mutator correct from any caller.
    playback_lifecycle.stop_playback_if_playing();

    // THE MODE ENDS HERE, on the first line past the last refusal and before the
    // first store write — the render-entry load-in-place's placement and its
    // reason (see the
    // paragraph at the head of this function). It also clears the session this
    // function read its base name from, which is why that read is at the top.
    close_history_mode();

    const char load_tab = settings->active_tab_view;

    std::vector<GuiWarpMarker>       warp_pre = app.warpmarkers.markers();
    std::vector<GuiPhaseResetMarker> phase_reset_pre =
        app.phaseresetmarkers.markers();

    app.warpmarkers.markers_mut()       = std::move(src_warp);
    app.phaseresetmarkers.markers_mut() = std::move(src_phase_resets);
    // Wholesale authoring reset: the ONE selection goes, and there is nothing
    // else to reset — no per-tab per-mode slot holds a copy.
    selection.clear_selection();

    // One cross-file undo entry: the marker pair plus the outgoing engine
    // settings (captured inside push_undo_both). The inherited prefs and view
    // state ride OUTSIDE undo — the same convention that keeps view state and
    // trim out of history.
    const char load_marker_mode = app.active_markers_view;
    undo.push_undo_both(std::move(warp_pre), std::move(phase_reset_pre),
                        load_marker_mode, load_tab);
    undo.recompute_dirty();

    // Wholesale authoring reset: clear every marker's session-only iteration
    // state and the bpm state, and turn off both sweep modes' visibility.
    {
        auto& mv = app.warpmarkers.markers_mut();
        for (auto& m : mv) {
            m.iter_start_cents.reset();
            m.iter_end_cents.reset();
        }
    }
    flag_editor.wipe_bpm_state();
    app.iteration_mode_enabled = false;
    app.bpm_mode_enabled       = false;

    // Both tab bands from the file, then the engine block and the scalar session
    // prefs VALUES ONLY through the one routine a source load also calls — the
    // render-entry load-in-place's two steps, unchanged, so a commit loads in
    // place 1:1 with a load of the same three files.
    app.tab_a = view_state_from_settings_tab(settings->tab_a);
    app.tab_b = view_state_from_settings_tab(settings->tab_b);
    apply_settings_engine_and_prefs(app, *settings);

    // Clamp both loaded-in-place bands' playheads into the live domain (the shared
    // chokepoint), mirroring the source load's tab-snapshot clamp at the same
    // point in the sequence. Unlike an entry sidecar — written once at dispatch
    // from an in-domain live state — a COMMITTED band is only as in-domain as
    // the checkpoint that wrote it, so this clamp has a live producer here: a
    // commit made against a different cut of the source lands its stored
    // playhead inside this source's domain instead of past its end.
    app.tab_a.playhead_cursor_sample = clamp_playhead_to_live_domain(
        app.tab_a.playhead_cursor_sample, app, audio);
    app.tab_b.playhead_cursor_sample = clamp_playhead_to_live_domain(
        app.tab_b.playhead_cursor_sample, app, audio);

    // Activate the file's tab band with no double-apply (NOT
    // switch_active_tab_view_to): active_tab_view was just set by the shared
    // routine and both bands are already the file's.
    {
        const ViewState& band = (app.active_tab_view == 'B')
                                ? app.tab_b : app.tab_a;
        app.viewport_start_sample  = band.viewport_start_sample;
        app.zoom_level             = band.zoom_level;
        app.playhead_cursor_sample = band.playhead_cursor_sample;
        app.trim                   = band.trim;
    }

    // Caller-side side effects the shared routine deliberately omits, in the
    // source load's own order and at its own point: the speed to the engine, the
    // gui scale to the renderer, then the geometry-and-cache rebuild.
    playback.set_speed(app.playback_speed);
    set_gui_scale_percent(app.gui_scale);
    paint_handler.on_resize(app.width, app.height);

    clamp_viewport_start(app, audio);
    // COINCIDENCE AUTO-SELECT at the load-in-place chokepoint (rule and inventory at
    // auto_select_marker_at_playhead), at the tail for the reason the render-entry
    // load-in-place states: everything the scan reads has landed.
    auto_select_marker_at_playhead(app, audio, selection, viewport);
    viewport.kick_waveform_sync();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();

    // The tail's trigger owns the rebind for a 'T' landing.
    target_render.trigger();

    // NO renders/ WIPE. That step is the render-entry load-in-place's cleanup
    // of the folder it
    // consumed an entry from; this path consumed a commit and renders/ is none of
    // its business.
    std::fprintf(stderr,
        "warptempo_gui: load-in-place: Loaded the sidecar state of commit "
        "%s in place\n",
        snap.sha.c_str());
    gui.invalidate_region(0, 0, app.width, app.height);
    return true;
}

// Open the `'` load prompt. No-op with no source loaded. An empty
// renders/ reports a one-line bottom-strip status and does not open. Stops
// playback only when the modal actually opens (after every guard), so a
// refused open leaves a listening session running.
//
// THE `/` HISTORY MODE CHANGES WHAT THIS EDITOR IS FOR, and the whole change is
// this one branch plus the routing at load_editor_commit: in the mode the
// editor takes a COMMIT SPELLING and opens PREFILLED with the viewed commit's
// full SHA (load_history_commit_in_place). Both of the renders-side guards
// drop with the
// renders-side subject — an empty renders/ is no obstacle to loading a commit
// in place,
// and the running-render refusal exists for the wipe at the render-entry
// load-in-place's tail, which the from-commit load-in-place does not have
// (the reasoning is at that function).
void GuiInputHandler::open_load_editor() {
    if (text_editor::is_active(app.load_editor)) return;
    if (app.source_audio_path.empty()) return;

    std::string prefill;
    if (app.history_mode.active) {
        // The viewed commit's full 40-char SHA. An out-of-range index answers
        // with the empty string, which opens an empty editor the user can paste
        // into — the honest cold answer, and unreachable in practice since the
        // mode only opens with a non-empty walk and every step clamps.
        prefill = app.history_mode.session.sha_at(app.history_mode.index);
    } else {
        // Running-render guard: the load-in-place wipes renders/, which would race a
        // background sweep writing into it. Refuse, don't cancel — a running
        // batch may be irreplaceable queued work; Esc is the explicit cancel.
        if (app.queue_running || app.pending_archival.armed) {
            app.transient_status_message = "Render running; Esc cancels it";
            viewport.invalidate_timestamp_area();
            return;
        }
        std::vector<AppState::RenderEntry> list =
            renders_dir.enumerate_render_entries();
        if (list.empty()) {
            app.transient_status_message = "No renders to load in place";
            viewport.invalidate_timestamp_area();
            return;
        }
    }
    // Stop playback only now that the modal is definitely opening — the shared
    // modal stop (stop_playback_for_modal_open), whose refusal-gating rule this
    // site is the sharpest instance of: each guard above (no source,
    // running/parked render, empty renders/) returns without touching playback,
    // so a refused open never interrupts a listening session. Space is inside the
    // modal blocked set, so once open, playback cannot restart until the editor
    // closes.
    playback_lifecycle.stop_playback_for_modal_open();
    text_editor::enter(app.load_editor,
                       /*target=*/0,
                       /*locked_prefix=*/"",
                       std::move(prefill),
                       text_editor::Kind::LoadInPlace);
    // OPEN-SELECTED ON A SEEDED BUFFER, the flag editor's convention (the
    // product's only other prefilling opener) and for its stated reason: the
    // first keystroke replaces the seed wholesale, so pasting another SHA over
    // the prefilled one is one act rather than a select-all first. An EMPTY seed
    // — every renders-side open, which is byte-identical to before — takes
    // enter()'s own caret-at-end-of-nothing rest state and selects nothing.
    if (!app.load_editor.pending.empty()) {
        app.load_editor.selection_anchor = 0;
        app.load_editor.cursor_pos =
            static_cast<int>(app.load_editor.pending.size());
    }
    viewport.invalidate_timestamp_area();
}

void GuiInputHandler::load_editor_exit_no_commit() {
    if (!text_editor::is_active(app.load_editor)) return;
    viewport.invalidate_timestamp_area();
    text_editor::deactivate(app.load_editor);
}

// Tab handler: extend the pending to the longest common prefix of the entry
// identifiers that start with it. No-op when nothing matches or when the
// common prefix does not advance past what is already typed (mirrors the
// settings editor's no-op-on-ambiguity Tab). A unique matching candidate
// completes fully — its whole string is the common prefix of the singleton.
//
// THE COMPARISON IS BY BYTE, DELIBERATELY: an entry identifier is a FILESYSTEM
// PATH, and a path is bytes — it is not text the UTF-8 relaxation covers, and
// the resolve below matches it byte-exactly against the same strings. Program-
// written batch folders and cell basenames are ASCII, so the byte compare is
// the whole story for every name this product writes.
//
// WHAT THE PREFIX WRITES IS TEXT, THOUGH, and that is the one place the two
// domains meet: the result becomes an editor `pending`, which must end on a
// UTF-8 codepoint boundary (the invariant is at the head of text_editor.h).
// Two hand-placed files in renders/ whose stems pass the numeric-prefix filter
// and share a PARTIAL multi-byte character would otherwise cut the prefix
// mid-character, seeding a buffer the caret walks and the shaper draws as
// .notdef. So the prefix is backed off to a boundary before it is published.
// The back-off reads the SEED CANDIDATE rather than the prefix itself, which is
// what makes it exact: `lcp` is a prefix of `first` throughout (every step only
// truncates), so the cut is mid-character exactly when the byte `first` carries
// AT the cut is a continuation byte — a question the prefix alone cannot answer,
// since a COMPLETE trailing sequence also ends in continuation bytes. It is a
// provable no-op for ASCII identifiers: no ASCII byte is a continuation byte, so
// the loop never takes a step for any name the product writes.
void GuiInputHandler::load_editor_autocomplete() {
    if (!text_editor::is_active(app.load_editor)) return;
    // IN THE `/` HISTORY MODE THERE IS NOTHING HERE TO COMPLETE AGAINST: the
    // pending is a commit spelling, and the entry identifiers below name renders
    // this route never reads. A bare Tab is a consumed no-op there rather than a
    // completion out of the wrong vocabulary.
    if (app.history_mode.active) return;
    const std::string pending = app.load_editor.pending;

    std::vector<AppState::RenderEntry> list =
        renders_dir.enumerate_render_entries();

    std::string lcp;
    std::string first;
    bool have = false;
    for (const auto& e : list) {
        const std::string c = render_entry_id(e);
        if (c.size() < pending.size() ||
            c.compare(0, pending.size(), pending) != 0) continue;
        if (!have) { lcp = c; first = c; have = true; }
        else {
            const size_t n = std::min(lcp.size(), c.size());
            size_t i = 0;
            while (i < n && lcp[i] == c[i]) ++i;
            lcp.resize(i);
        }
    }
    if (!have) return;                          // no candidate has this prefix
    // Back the prefix off to a codepoint boundary (see the note above). Runs
    // BEFORE the advancement test, so a prefix that reached only into the
    // middle of a character correctly collapses to a no-op.
    while (lcp.size() < first.size() &&
           text_editor::is_utf8_continuation_byte(
               static_cast<unsigned char>(first[lcp.size()])))
        lcp.pop_back();
    if (lcp.size() <= pending.size()) return;   // common prefix does not advance

    app.load_editor.pending          = std::move(lcp);
    app.load_editor.cursor_pos       =
        static_cast<int>(app.load_editor.pending.size());
    app.load_editor.selection_anchor = -1;
    app.load_editor.red              = false;
    viewport.invalidate_timestamp_area();
}

// Enter handler: resolve the pending to exactly one entry and load it in place.
// Resolution accepts exactly the entry's canonical id
// (`<batch_dir>/<basename>.wav`); ids are unique by filesystem construction
// (one path per file), so the first match resolves. On a resolve,
// load_render_entry_in_place runs; a true result closes the editor, a false result
// (bad sidecar / missing wav) red-flashes and stays open — the mutator having
// named the cause on stderr. Zero matches red-flash and stay open SILENTLY: an
// identifier matching nothing is a typo, not a fault, and the flash is the whole
// answer (architect 2026-08-02).
//
// IN THE `/` HISTORY MODE THE SUBJECT IS A COMMIT, not a render entry: the
// pending goes to load_history_commit_in_place, which owns every refusal on
// that route
// and names each one on stderr. The two routes share this function's SHAPE
// exactly — a true result closes the editor, a false one red-flashes and stays
// open — so a failed resolve leaves the typed spelling in place to be corrected.
void GuiInputHandler::load_editor_commit() {
    if (!text_editor::is_active(app.load_editor)) return;
    const std::string pending = app.load_editor.pending;

    auto reject = [&]() {
        app.load_editor.red = true;
        viewport.invalidate_timestamp_area();
    };

    if (app.history_mode.active) {
        if (load_history_commit_in_place(pending)) {
            viewport.invalidate_timestamp_area();
            text_editor::deactivate(app.load_editor);
        } else {
            reject();
        }
        return;
    }

    std::vector<AppState::RenderEntry> list =
        renders_dir.enumerate_render_entries();

    const AppState::RenderEntry* found = nullptr;
    for (const auto& e : list) {
        if (render_entry_id(e) == pending) {
            found = &e;
            break;
        }
    }
    if (!found) { reject(); return; }

    // Copy the entry before the load-in-place: its tail wipes renders/, and
    // the copy is self-contained (paths + basename), so it stays valid.
    const AppState::RenderEntry entry = *found;
    if (load_render_entry_in_place(entry)) {
        viewport.invalidate_timestamp_area();
        text_editor::deactivate(app.load_editor);
    } else {
        reject();
    }
}

// Shared key route for EVERY keyboard-modal editor — the settings prompt, the
// load prompt, the bpm bracket editor, and the top-strip flag editor.
// All four spell ONE modal contract: the on_key gate (modal_editor_key_blocked)
// admits only the editor's own keys plus bare Esc, Ctrl+S, and Ctrl+Q, so a
// NotConsumed key here is one of the latter two chords. Ctrl+S saves with
// the editor left open (save is not an exit); Ctrl+Q runs the caller's
// teardown and returns false so on_key runs the close routing; anything
// else is swallowed as a backstop. `autocomplete` is the optional
// bare-Tab hook — only an unmodified Tab is intercepted (Shift / Ctrl /
// Alt + Tab fall through to handle_key unchanged); the bpm and flag editors
// pass an empty hook, but bare Tab never reaches this route for them at all —
// the on_key gate swallows it first.
// `repaint` is the caller's text-change damage and is REQUIRED — unlike
// `autocomplete` it is called unconditionally, with no emptiness test: the three
// bottom-strip surfaces pass invalidate_timestamp_area, the top-strip flag editor
// invalidate_top_strip. Commit and cancel own their own invalidations.
bool GuiInputHandler::route_modal_editor_key(
        text_editor::State& ed, GuiKey key, GuiInputState mods,
        const std::function<void()>& autocomplete,
        const std::function<void()>& commit,
        const std::function<void()>& cancel,
        const std::function<void()>& ctrl_q_teardown,
        const std::function<void()>& repaint) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    if (autocomplete && key == GuiKeys::Tab && !ctrl && !shift && !alt) {
        autocomplete();
        return true;
    }
    const auto action = text_editor::handle_key(ed, key, mods);
    if (action == text_editor::KeyAction::CommitRequested) {
        commit();
        return true;
    }
    if (action == text_editor::KeyAction::CancelRequested) {
        cancel();
        return true;
    }
    if (apply_editor_clipboard(action, ed)) {
        // Same repaint as the Consumed branch — text may have changed
        // (cut / paste); copy repaints harmlessly.
        repaint();
        return true;
    }
    if (action == text_editor::KeyAction::Consumed) {
        repaint();
        return true;
    }
    if (ctrl && !shift && !alt && key == GuiKeys::S) {
        save_ops.save();
        return true;
    }
    if (ctrl && !shift && !alt && key == GuiKeys::Q) {
        ctrl_q_teardown();
        return false;  // let on_key run the close routing
    }
    return true;  // modal: swallow
}

// Routes a key to the active load editor through the shared modal
// route; bare Tab autocompletes the entry identifier.
bool GuiInputHandler::handle_load_editor_key(GuiKey key,
                                               GuiInputState mods) {
    return route_modal_editor_key(
        app.load_editor, key, mods,
        [this] { load_editor_autocomplete(); },
        [this] { load_editor_commit(); },
        [this] { load_editor_exit_no_commit(); },
        [this] { load_editor_exit_no_commit(); },
        [this] { viewport.invalidate_timestamp_area(); });
}

// P / I / M letter-key handlers. See the declaration for the chord list.
bool GuiInputHandler::handle_mode_keys(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // Ctrl+P: copy phase reset placements from the selected warp markers
    // into the session clipboard. Section-based (architect 2026-07-23):
    // each selected marker contributes the block it owns — its time to the next
    // EFFECTIVELY-ENABLED marker's time, or to the song end when none follows
    // (a disabled marker never reaches the warp map, so it bounds no section;
    // the extent rule is stated in full at section_end_frame,
    // phase_reset_propagate.cpp). The set
    // must be a CONTIGUOUS run: the paste walks every labeled
    // destination block from the anchor and matches the clipboard in strict
    // lockstep, so a disjoint clipboard (labeled blocks A, C with a labeled B
    // unselected in the gap) would diverge at the first gap and never paste C.
    // The clipboard carries no gap representation, so contiguity is what keeps
    // the two label sequences aligned — the SAME gate the `m` sweep takes
    // (extent == count). Unlabeled markers inside the run still contribute no
    // block, and the paste's destination walk skips unlabeled markers
    // identically, so the two label sequences stay aligned regardless.
    // W-mode only; phase reset mode is a silent no-op. An empty or
    // non-contiguous selection refuses SILENTLY (architect 2026-07-23 —
    // gesture refusals are silent by convention, the read-only/home-view
    // class; stderr stays the render/load signal channel).
    if (key == GuiKeys::P && ctrl && !shift && !alt) {
        if (app.active_markers_view != 'W') return true;
        if (app.selected_markers.empty()) return true;
        // Contiguity gate, same spelling as the `m` sweep: std::set is
        // ascending, so a run [first .. last] is contiguous iff its extent
        // equals its count.
        if (*app.selected_markers.rbegin() - *app.selected_markers.begin() + 1
                != static_cast<int>(app.selected_markers.size())) {
            return true;
        }
        phase_reset_propagate.copy_from_selection();
        return true;
    }

    // Ctrl+Alt+P: paste clipboard phase resets onto the destination
    // anchored at the single selected warp marker. W-mode only; phase
    // reset mode is a silent no-op. Empty clipboard is a silent no-op, and so
    // is a selection that is not exactly one marker (architect 2026-07-30):
    // EVERY refusal in this family is silent, with no gesture-class stderr
    // anywhere in the GUI. A wrong selection count is an ordinary "not ready
    // yet" state the user can see on screen, not a fault worth a terminal line.
    // Opens a confirmation prompt before any mutation.
    if (key == GuiKeys::P && ctrl && !shift && alt) {
        if (app.active_markers_view != 'W') return true;
        if (app.phase_reset_clipboard.empty()) return true;
        if (app.selected_markers.size() != 1) return true;
        phase_reset_propagate.open_paste_confirmation();
        return true;
    }

    // Ctrl+Alt+Shift+P: propagate the enabled/disabled *state* of
    // clipboard placements onto the matching destination region's
    // phase resets, in order. Positions are not modified. W-mode only;
    // phase reset mode is a silent no-op. Empty clipboard is a silent
    // no-op, and so is a selection that is not exactly one marker — the same
    // all-refusals-are-silent rule as its Ctrl+Alt+P sibling above, where the
    // rationale is stated. Unlike Ctrl+Alt+P, no confirmation prompt — applies
    // directly. Divergence/mismatch is reported via the bottom-strip
    // transient status message rather than a modal dialog.
    if (key == GuiKeys::P && ctrl && shift && alt) {
        if (app.active_markers_view != 'W') return true;
        if (app.phase_reset_clipboard.empty()) return true;
        if (app.selected_markers.size() != 1) return true;
        phase_reset_propagate.paste_state_apply();
        return true;
    }

    // `p` (no modifiers) toggles phase reset view globally.
    if (key == GuiKeys::P && !ctrl && !shift && !alt) {
        active_views.toggle_active_markers_view();
        return true;
    }

    // `i` (no modifiers) toggles iteration mode in warp. Silent
    // no-op in phase reset view (phase reset flags carry no tempo to
    // iterate). The editor-active branch above already swallows any
    // keystroke while a popup edit is in flight, so this code only
    // runs with no active editor. Toggling repaints the top strip
    // so iteration popups appear or vanish in one frame.
    if (key == GuiKeys::I && !ctrl && !shift && !alt) {
        if (app.active_markers_view == 'W') {
            // Iteration mode drives the warp flag editor's bracket authoring,
            // so it toggles only in warp's home (source) view — both
            // directions refuse silently off home (consumed no-op). The S->T
            // toggle exits iteration mode through wipe_iter_state
            // (handle_active_audio_view_toggle), so the mode never rests in
            // target view.
            if (!active_column_authoring_allowed(app)) return true;
            const bool turning_on = !app.iteration_mode_enabled;
            if (!turning_on) {
                // Turning iteration mode OFF wipes every marker's
                // session-only iter bracket — exiting the mode is the
                // clear (wipe_iter_state, shared with enter_bpm_mode's
                // forced iter-off so the two exit routes cannot drift).
                // Runs before the flag flips.
                flag_editor.wipe_iter_state();
            }
            app.iteration_mode_enabled = !app.iteration_mode_enabled;
            viewport.invalidate_top_strip();
        }
        return true;
    }

    // `m` (no modifiers): open the BPM editor on the FIRST of a contiguous
    // run of selected markers whose sections define the sweep span. Warp
    // view only; silent no-op in phase reset view. Mutual exclusion with
    // iter mode is handled inside enter_bpm_mode. The section rule (architect
    // 2026-07-23): a marker owns the section from itself to the next store
    // marker, and the store-final marker's section runs to the song end — so
    // the selected run's LAST section is INCLUDED. The gate requires a
    // NON-EMPTY, CONTIGUOUS run of selected markers with no label_ref in
    // [owner .. boundary marker] inclusive; any other selection is a silent
    // no-op. Under the contiguity rule every in-span marker IS selected, so a
    // selected span-internal marker may be disabled and is still converted to a
    // plain pass per sweep cell; a disabled OWNER is rejected
    // (bpm_popup_eligible_marker now excludes disabled — a disabled owner was a
    // render-inert rewrite).
    // There is no toggle-off branch: the bpm editor is a modal bottom-strip
    // surface, so while it is open `m` never reaches this dispatch — it is
    // just a typed character the bracket grammar rejects — and bpm mode never
    // rests without its editor (the mode's only exits are the editor's own:
    // Esc, and Enter's dispatch tail).
    if (key == GuiKeys::M && !ctrl && !shift && !alt) {
        if (app.active_markers_view != 'W') return true;
        // The bpm editor rewrites tempo through the derivation (not a ruled
        // target-view exception), so it opens only in warp's home (source)
        // view; off home is a consumed no-op.
        if (!active_column_authoring_allowed(app)) return true;
        // Section-based span gate. A non-empty, contiguous run of selected
        // markers; the first owns, and the run covers the sections owned by
        // every selected marker (the last one's section included).
        if (app.selected_markers.empty()) return true;
        const auto& mv = app.warpmarkers.markers();
        const int n = static_cast<int>(mv.size());
        const int owner    = *app.selected_markers.begin();
        const int last_sel = *app.selected_markers.rbegin();
        if (owner < 0 || last_sel >= n) return true;
        // Contiguity: the sweep writes ONE owner tempo over ONE contiguous
        // span, and the shift-range select produces exactly contiguous runs;
        // a disjoint set has no single-span meaning here. The COPY takes the
        // SAME contiguity gate (its paste walks labeled blocks in lockstep, so
        // a gap would misalign the two label sequences). std::set is ascending,
        // so a run [owner .. last_sel] is contiguous iff its extent equals its
        // count.
        if (last_sel - owner + 1 != static_cast<int>(app.selected_markers.size()))
            return true;
        // boundary == last_sel + 1: one past the last selected marker. When
        // boundary < n the marker there is the closing boundary (owns the
        // following section, outside the span); boundary == n is the song end
        // (last_sel is the store-final marker, its section runs to the end).
        const int boundary = last_sel + 1;
        // No label_ref anywhere in [owner .. min(boundary, n-1)] inclusive:
        // every in-span marker rewrites its tempo (a ref cannot take one),
        // and the boundary marker (when it exists) still cannot bound the
        // span cleanly. At song end there is no boundary marker, so the scan
        // clamps to n-1.
        const int scan_end = std::min(boundary, n - 1);
        for (int i = owner; i <= scan_end; ++i) {
            if (!mv[i].label_ref.empty()) return true;   // silent no-op
        }
        // Owner must satisfy the BPM-eligibility predicate (owning, no ref,
        // and — now — enabled).
        if (!bpm_popup_eligible_marker(mv[owner])) return true;
        // enter_bpm_mode tags the owner and flips the mode flag; the span
        // endpoint is explicit, so record it on the owner and keep the whole
        // selected run highlighted as the span cue.
        flag_editor.enter_bpm_mode();
        if (!app.bpm_mode_enabled) return true;   // gate inside bailed
        {
            auto& mvw = app.warpmarkers.markers_mut();
            mvw[owner].bpm_endpoint = boundary;
        }
        const std::set<int> span_selection = app.selected_markers;
        // The bpm editor is a modal bottom-strip surface, so its open takes the
        // shared modal stop (stop_playback_for_modal_open). Position is
        // load-bearing: every refusal in the guard ladder above — the authoring
        // gate, the span's contiguity / label_ref / eligibility tests, and
        // enter_bpm_mode's own bail — returns before reaching here, so a refused
        // `m` leaves a listening session running. Space is inside the modal
        // blocked set, so playback cannot restart until the editor closes.
        playback_lifecycle.stop_playback_for_modal_open();
        flag_editor.enter_bpm_edit(owner);
        // The playhead land rides enter_text_edit (the shared open chokepoint):
        // it lands on `owner`, the EARLIEST selected, while the focus that built
        // this span sits wherever the multi-select click left it (those clicks
        // land on their focus — land_playhead_on_marker, input_pointer.cpp), and
        // it clears any resting scratch region as every point command does. The
        // open also collapsed the selection to {owner} on the way through. The
        // re-insert below restores the MEMBERSHIP only — std::set::insert leaves
        // last_selected_marker alone — so the focus stays `owner`. NOTHING
        // re-derives a region here any more (the extent re-derive died with the
        // SPAN FORM, architect 2026-07-30): the group's cue is its members'
        // brightened flags plus the visible cursor on `owner`. No second land.
        bool restored = false;
        for (int s : span_selection) {
            if (app.selected_markers.insert(s).second) restored = true;
        }
        if (restored) viewport.invalidate_top_strip();
        return true;
    }

    // `l` (no modifiers): "Listen to renders" — launch the external audio
    // player (the audio_player setting, default "audacious") with every
    // rendered wav under <source_parent>/renders/, in the numeric order
    // enumerate_render_entries returns them. Fire-and-forget; the GUI's own
    // playback is unaffected. The modal / editor / read-only gates in on_key
    // run before this handler, so `l` is inert while any of them owns the
    // keyboard (like p/i/m). An explicitly-blank player (the deliberate
    // opt-out) or an empty render set reports a one-line bottom-strip status
    // and does nothing.
    if (key == GuiKeys::L && !ctrl && !shift && !alt) {
        if (app.audio_player.empty()) {
            app.transient_status_message = "No audio_player set";
            viewport.invalidate_timestamp_area();
            return true;
        }
        std::vector<AppState::RenderEntry> list =
            renders_dir.enumerate_render_entries();
        if (list.empty()) {
            app.transient_status_message = "No renders to play";
            viewport.invalidate_timestamp_area();
            return true;
        }
        std::vector<std::string> wavs;
        wavs.reserve(list.size());
        for (const auto& e : list) wavs.push_back(e.wav_path.string());
        if (!spawn_audio_player(app.audio_player, wavs)) {
            std::fprintf(stderr,
                "warptempo_gui: Could not launch audio_player '%s'\n",
                app.audio_player.c_str());
        }
        return true;
    }

    return false;
}

// Tab-key family. See the declaration for the chord list.
bool GuiInputHandler::handle_tab_switch_keys(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // Ctrl+Tab toggles A/B navigational tabs. Stops playback, saves
    // current viewport/zoom/playhead to the leaving tab, restores the
    // target tab. Does not mark the document dirty. Alt-strict: an Alt
    // held alongside makes the chord an unbound no-op, never this binding.
    if (ctrl && !shift && !alt && key == GuiKeys::Tab) {
        active_views.switch_active_tab_view_to(app.active_tab_view == 'A' ? 'B' : 'A');
        target_render.trigger();
        return true;
    }

    // Ctrl+Shift+Tab: advance both tabs' marker focus and end on the
    // opposite tab. Composes bare Tab and Ctrl+Tab so the user can
    // march paired tabs forward in lockstep with one chord.
    if (ctrl && shift && !alt && key == GuiKeys::Tab) {
        cycle_marker_focus(true);
        active_views.switch_active_tab_view_to(app.active_tab_view == 'A' ? 'B' : 'A');
        cycle_marker_focus(true);
        target_render.trigger();
        return true;
    }

    // Bare Tab / Shift+Tab / IsoLeftTab: cycle focus onto the next/prev
    // marker, moving the playhead to it and recentering on it at the current
    // zoom (always — follow mode does not gate it). The Ctrl+Tab branch above runs first and
    // returns, so Ctrl+Tab is consumed before reaching here; the explicit
    // !ctrl guards below ensure Ctrl+Shift+Tab does not slip into the
    // cycle path either. Alt-strict everywhere: an Alt held makes the chord
    // an unbound no-op rather than falling into the cycle.
    if (!ctrl && !alt && key == GuiKeys::Tab && !shift) {
        cycle_marker_focus(true);  return true;
    }
    if (!ctrl && !alt && key == GuiKeys::Tab && shift)  {
        cycle_marker_focus(false); return true;
    }
    if (!ctrl && !alt && key == GuiKeys::IsoLeftTab)    {
        cycle_marker_focus(false); return true;
    }

    return false;
}

// Bare-key (no-modifier) dispatch. See the declaration for the binding list;
// the caller gates on no modifiers held.
void GuiInputHandler::handle_plain_bare_keys(GuiKey key) {
    switch (key) {
    case GuiKeys::Escape: /* top-level Escape is a no-op */ break;
    case GuiKeys::Left:
        // WAVEFORM-LANE playhead step: reached only with an EMPTY selection,
        // because the
        // marker-lane branch in on_key claims the press first and returns. The
        // membership half of the clear below is therefore already satisfied; the
        // FOCUS half is not — last_selected_marker survives an empty selection
        // (a ctrl-toggle that empties the set repairs the focus rather than
        // dropping it, and sanitize can leave one behind), and
        // clearing it is what stops a stale focus from re-entering the marker
        // lane on the next selection gesture.
        // The stop lives HERE, in this lane only: the marker-lane routes carry
        // their own playback regimes (the position nudges stop in their prologue,
        // while the W+target refusal stops nothing at all), which is
        // exactly why on_key routes before reaching this body.
        playback_lifecycle.stop_playback_if_playing();
        if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
            selection.clear_selection();
            viewport.invalidate_waveform_area();
        }
        // Navigation playhead step: dissolve a resting region (the playhead is
        // leaving its span). This dispatch site fires once per press;
        // move_playhead_pixels is its only caller, so the clear cannot leak to a
        // non-navigation path.
        clear_region_highlight(app, viewport);
        viewport.move_playhead_pixels(-1);
        break;
    case GuiKeys::Right:
        playback_lifecycle.stop_playback_if_playing();
        if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
            selection.clear_selection();
            viewport.invalidate_waveform_area();
        }
        clear_region_highlight(app, viewport);
        viewport.move_playhead_pixels(+1);
        break;
    case GuiKeys::F:
        // Toggle follow mode. The full body (off→on edge resync) lives in
        // GuiPlaybackLifecycle::set_follow_mode, shared with the settings
        // editor's `follow=` commit.
        playback_lifecycle.set_follow_mode(!app.follow_mode);
        break;
    case GuiKeys::C:
        // Jump to the working zoom (kWorkingZoomLevel, the ideal warp-authoring
        // zoom). When a marker is focused, first jump the playhead exactly onto
        // it — the same jump the Tab family runs, after the same last-selected
        // repair — then set the working zoom and center on it; with no focused
        // marker, keep the plain working-zoom-and-center-on-playhead behavior.
        // Clear the region here, unconditionally and up front: the no-focus arm
        // never reaches jump_playhead_to_focused_marker's clear tail (that
        // function early-returns with nothing focused), and a region drag clears
        // the marker selection, so region-drag-then-`c` is exactly the no-focus
        // path. HELP lists `c` in the clear set unconditionally. The focused arm
        // then double-clears via the jump tail — a no-op, since the helper's
        // !active guard returns immediately on the already-cleared region.
        // A GROUP CARRIES (architect 2026-07-30, with the SPAN FORM retired): the
        // collapse-to-focus that stood here is deleted — it existed only to keep a
        // group from resting SPANLESS, a state that no longer exists now the
        // region is trim scratch rather than a group's playhead form. The jump
        // below is a jump TO THE FOCUS and accepts a group's focus as-is; the
        // always-visible cursor lands there, the other members keeping their
        // brightened flags.
        clear_region_highlight(app, viewport);
        selection.repair_last_selected();
        jump_playhead_to_focused_marker();
        viewport.apply_zoom_change(kWorkingZoomLevel);
        viewport.center_viewport_on_playhead();
        break;
    case GuiKeys::Home:
        // Trim-bound jump, and a route OUT of the marker lane: the playhead is
        // leaving the focused flag for a spot nothing marks, so the selection
        // must go with it — the lane rule's second clause (a route that empties
        // the selection leaves the playhead where it lands, for the cursor to
        // paint again; the rule itself is stated at land_playhead_on_marker in
        // input_pointer.cpp). UNLIKE the bare Left/Right arms above, this clear
        // does real MEMBERSHIP work: those reach this body only with an empty
        // selection (the marker-lane branch in on_key claims them first), so
        // theirs is a focus-only repair, while Home/End reach it with ANY
        // selection. Without it the flag would keep claiming to be the playhead
        // at its own position and the next bare arrow would tow the playhead
        // back onto the marker, silently discarding the jump.
        playback_lifecycle.stop_playback_if_playing();
        if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
            selection.clear_selection();
            viewport.invalidate_waveform_area();
        }
        // Navigation jump to the trim-begin bound: dissolve a
        // resting region (its span is stale now the playhead jumps).
        clear_region_highlight(app, viewport);
        viewport.move_playhead_to(viewport.trim_begin_sample());
        break;
    case GuiKeys::End:
        playback_lifecycle.stop_playback_if_playing();
        if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
            selection.clear_selection();
            viewport.invalidate_waveform_area();
        }
        clear_region_highlight(app, viewport);
        viewport.move_playhead_to(viewport.trim_end_sample() - 1);
        break;
    default: break;
    }
}

// Top-flag editor key routing. See the declaration for the consumed/command
// contract. BOTH kinds take the shared modal route (architect 2026-07-28) and
// differ only in their commit / cancel bodies and their repaint area: the bpm
// bracket editor draws in the BOTTOM strip (like the settings editor) and
// commits into a render sweep, the FlagPayload editor draws in the TOP strip
// and commits the flag's own payload. Neither passes a bare-Tab hook, and bare
// Tab never reaches this route for either — the on_key gate swallows it first.
// For both, Ctrl+S saves with the editor left open and Esc / Enter are the
// session's only exits.
bool GuiInputHandler::handle_top_flag_editor_key(GuiKey key,
                                                 GuiInputState mods) {
    if (app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        return route_modal_editor_key(
            app.top_flag_editor, key, mods,
            /*autocomplete=*/nullptr,
            [this] {
                // Enter commits + renders + closes in one action.
                // A successful commit stores the values on the owner and
                // closes the editor; only then does the BPM sweep fire. A
                // parse failure leaves the editor open (red) and renders
                // nothing.
                if (flag_editor.commit_bpm_edit()) {
                    // render_bpm_sweep owns the mode teardown on its success
                    // path: after the batch is built and accepted (dispatched,
                    // or parked behind a killed render's drain — a busy worker
                    // no longer bails) it wipes the session-only bpm state and
                    // exits bpm mode, so an accepted sweep leaves no marker
                    // carrying bpm state and the next M on this marker seeds
                    // []. A guard-bail (return false) is an environmental
                    // backstop — batch-folder creation failure, no valid
                    // cells; the stale-endpoint class is
                    // unreachable because the modal bpm session freezes the
                    // store between mode entry and this dispatch. The commit
                    // already closed the editor, and bpm mode is exactly its
                    // editor session, so a bail exits the mode here —
                    // mode-without-editor stays unreachable.
                    if (!render_bpm_sweep()) {
                        flag_editor.exit_bpm_mode();
                    }
                } else if (!text_editor::is_active(app.top_flag_editor)) {
                    // commit_bpm_edit closed the editor without committing
                    // (the invalid-target backstop): take the mode down with
                    // it. A red-flash refusal leaves the editor open and
                    // deliberately does not land here.
                    flag_editor.exit_bpm_mode();
                }
            },
            [this] {
                flag_editor.exit_top_flag_edit_no_commit();
                flag_editor.exit_bpm_mode();
                viewport.invalidate_timestamp_area();
            },
            [this] {
                // Ctrl+Q tears the editor and the mode down together
                // (mode-without-editor stays unreachable).
                flag_editor.exit_top_flag_edit_no_commit();
                flag_editor.exit_bpm_mode();
            },
            [this] { viewport.invalidate_timestamp_area(); });
    }
    // FlagPayload: the same modal route, top-strip repaint — plus the WAVEFORM
    // on a red-flash EDGE. The invalid-commit flash reaches the marker's STEM
    // now (GuiPaintHandler::paint_marker_stems), and a stem is a waveform pixel
    // while the box the flash was designed for is a strip one, so the route's
    // top-strip repaint no longer covers the whole flash. Compared as an EDGE
    // rather than damaged unconditionally: typing inside the editor is a
    // key-repeat-cadence event and the flash flips at most once per commit
    // attempt. ONE site covers every keyboard flip in both directions — the
    // commit's own refusals and the pending-cap refusal SET it (both inside the
    // route), the next mutating keystroke CLEARS it, and Esc / Ctrl+Q / a
    // successful commit clear it through deactivate. The POINTER close is the
    // one flip that does not pass here; it pays the same damage at its own
    // chokepoint (exit_top_flag_edit_no_commit).
    const bool was_red = app.top_flag_editor.red;
    const bool consumed = route_modal_editor_key(
        app.top_flag_editor, key, mods,
        /*autocomplete=*/nullptr,
        [this] {
            // Iteration editing is a widened-grammar FlagPayload
            // commit (commit_top_flag_edit), not a separate bracket editor.
            flag_editor.commit_top_flag_edit();
        },
        [this] { flag_editor.exit_top_flag_edit_no_commit(); },
        [this] {
            // Ctrl+Q discards the edit, then on_key runs the close routing —
            // the same abandon Esc performs, since the edit is uncommitted.
            flag_editor.exit_top_flag_edit_no_commit();
        },
        [this] { viewport.invalidate_top_strip(); });
    if (app.top_flag_editor.red != was_red) viewport.invalidate_waveform_area();
    return consumed;
}

// Settings-prompt editor key routing, through the shared modal route.
// Bare Tab autocompletes the value side of `key=` with the key's current
// stored value — every settings key, engine and GUI-kind alike
// (recall_gui_setting_value / format_nonengine_value in settings_io) —
// for recall and editing.
bool GuiInputHandler::handle_settings_editor_key(GuiKey key,
                                                 GuiInputState mods) {
    return route_modal_editor_key(
        app.settings_editor, key, mods,
        [this] { settings_editor.autocomplete_value(); },
        [this] { settings_editor.commit(); },
        [this] { settings_editor.exit_no_commit(); },
        [this] { settings_editor.exit_no_commit(); },
        [this] { viewport.invalidate_timestamp_area(); });
}
