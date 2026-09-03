// on_key dispatch helpers. Each is a GuiInputHandler method declared in
// input_handler.h; on_key calls them in sequence (if (handle_X(...))
// return;). Grouped here to keep input_handler.cpp focused on the event
// entry points and the pointer / wheel paths.

#include "input_handler.h"

#include "file_loader.h"     // source_load_dry_run (the Open project picker's act)
#include "folder_overlay.h"  // the player's and the picker's key routers (the list walk)
#include "frame_format.h"    // format_authored_frame (the revert act's line)
#include "marker_store_validate.h"  // first_past_eof_wall_defect (the three
                                    // promote roads' shared wall guard)
#include "project_model.h"   // resolve_project / enumerate_project_names
#include "prompt.h"          // GuiCloseTarget (the Open project picker's reopen)
#include "render_output_naming.h"  // render_output_directory (the sync act's
                                   // deliverable folder)
#include "renders_dir.h"     // project_batch_root (the sync act's batch root)
#include "history_diff.h"
#include "phase_reset_clipboard.h"  // warp_marker_label_name / warp_marker_propagates
#include "phase_reset_propagate.h"  // format_domain_timestamp (the family's one register)
#include "paint_handler.h"
#include "render.h"
#include "render_pipeline.h"
#include "settings_io.h"
#include "text_editor.h"
#include "warp_frame_map_view.h"  // source_frame_to_active_domain (the diff-flag
                                  // cycle's playhead-anchored seed)
#include "warpmarkers.h"
#include "warp_frame_map_build.h"  // warp_coincident_collapse_members (the `m`
                                   // gate's coincident-owner refusal)

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

// (THE HISTORY-UNAVAILABLE SENTENCE moved to history_diff.h on 2026-08-30,
// beside the module that composes the reason it prefixes: the mode's ENTRY
// refusal became a card that day, which put a fourth reporter of the fact in
// this file and a producer of it in another translation unit.)

// THE CHECKPOINT-PUBLISHING SENTENCE, ONE LITERAL (2026-08-30). ONE MODE, ONE
// FACT, ONE WORDING, and it now has FOUR readers in this file: bare `h`'s
// entry refusal (which also prints it on stderr — one composer, two
// surfaces), the Open project picker's own open act, that picker's
// router's Ctrl+S arm, which meets the identical bit at the identical moment,
// and — since 2026-09-01 — THE COMMIT ACT'S OWN OPENER
// (open_history_commit_editor), which met the bit as an allowlist admission
// term until the architect ruled a gate's membership the chord's alone and the
// refusal became the act's to say.
// ONE CLAUSE (architect 2026-09-01, the capitalization sweep's sentence
// shape): the sentence is the instruction, exactly as its sibling
// kTargetPreviewNotReadyCard already was. It read "A checkpoint is still
// publishing; try again when it finishes" until that day.
constexpr const char* kCheckpointPublishing =
    "Wait for the checkpoint to finish publishing";

// Move `dir` to the DESKTOP TRASH with `gio trash`, the freedesktop trash
// spec's ordinary command-line front end. True iff the folder is gone from
// disk afterwards.
//
// THIS IS THE PRODUCT'S ONLY TRASHED DELETION (architect 2026-08-07), and the
// scope line is deliberate: the one caller is the `'` load-in-place's tmp/
// wipe far below, which takes every archival render in the folder with it —
// user-visible artifacts a sweep may have spent an evening producing, so a
// wiped batch stays restorable. EVERY OTHER deletion in the product stays a
// native remove: the atomic-write temporaries, the history mode's RAII scratch
// dir, the process-private render cache and a cancelled render's own partial
// artifacts are all ephemera the user never sees, and trashing them would
// pollute his trash instead of protecting anything.
//
// argv exec, NEVER a shell (the project's standing rule): a project folder's
// name carries spaces and reaches gio as one argv element with no quoting rules
// in between. `--` guards a hypothetical dash-leading path. The folder goes
// WHOLE, so a restore brings back one entry rather than a scatter of files.
//
// THE VERDICT IS AN OBSERVATION OF THE FILESYSTEM rather than the child's exit
// status, for the reason main()'s SIG_IGN SIGCHLD makes unavoidable: under that
// disposition waitpid returns ECHILD and the status is simply not obtainable
// (the git runners in history_diff.cpp are written to the same regime, and
// their comments own the reasoning). The wait still runs — it is what orders
// the observation after the child — and the status is HONOURED when it does
// arrive, a future session leaving the default disposition; what decides the
// ordinary case is that the directory is no longer there. That one witness
// covers every shape the caller's fallback exists for: gio absent (execvp
// returns and the child _exit's), gio refusing (an unsupported filesystem, no
// gvfs available), a fork that never happened.
//
// THE WITNESS ANSWERS ONLY A CONFIRMED ABSENCE, which is why the error_code is
// read rather than discarded: std::filesystem::exists returns false both when
// the status says not_found and when the status query itself FAILED (a
// permission or I/O error), and those two are opposite verdicts. A false with
// `ec` set is INDETERMINATE — the folder may well still be there — so it is not
// absence and this helper returns false for it, routing the caller onto the
// native fallback, whose own failure is loud. Reporting a successful trash on
// an unreadable status would suppress that fallback and let the caller announce
// a wipe that never happened.
//
// The child's stdout and stderr go to /dev/null: the caller's single fallback
// line is the whole diagnostic this act prints, and gio's own words would only
// double it.
bool trash_directory(const std::filesystem::path& dir) {
    const std::string path = dir.string();

    char* argv[] = {const_cast<char*>("gio"), const_cast<char*>("trash"),
                    const_cast<char*>("--"),  const_cast<char*>(path.c_str()),
                    nullptr};

    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        // Child: nothing between here and exec that is not async-signal-safe.
        const int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execvp("gio", argv);
        _exit(127);
    }

    int   status = 0;
    pid_t w      = 0;
    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR);
    if (w == pid && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) {
        return false;
    }

    std::error_code ec;
    const bool absent = !std::filesystem::exists(dir, ec);
    return absent && !ec;
}

}  // namespace

// The lane model's one predicate — see the declaration for the two readers and
// the rationale. A non-empty selection IS the marker lane: with a focus standing,
// the bare horizontal arrows move that MARKER and the cursor rides along. The
// expression lives at marker_selection_standing (app_state.h) since
// 2026-08-30, where the bottom row's faces can read it too.
bool GuiInputHandler::playhead_in_marker_lane() const {
    return marker_selection_standing(app);
}

// Source-view read-only allowlist. True when key+mods is not on the allowlist
// and should be dropped.
//
// WHAT READ-ONLY PROTECTS, in one sentence (architect 2026-08-07, RECLASSIFYING
// the old "persistent mutation" standard): read-only protects the AUTHORED
// MUSICAL CONTENT — the two marker stores and the engine settings — AND NOTHING
// ELSE. The per-tab BAND is not content: viewport, zoom, playhead, TRIM and the
// read_only bit itself are all read-only-LEGAL, and so are SAVE and RENDER,
// which author nothing — a save writes the state the tab already holds and a
// render reads it — and so, since 2026-08-28, is THE OPEN PROJECT PROMPT on
// Ctrl+O, which leaves this session for another project rather than writing
// into it.
// THE DRIVING CASE IS TRIM'S (the architect's own): a finished section's tab
// locked against accidental marker movement, while its trim window is moved
// freely to compare a passage against the other tab in target view, re-rendered,
// and saved. TRIM'S OWN DATA MODEL ALREADY SAID BAND RATHER THAN CONTENT — it
// lives in ViewState beside the viewport and the zoom, it has NO undo, and it
// never dirties the session — so the gate treating it as authoring was a
// classification leftover rather than a ruling.
// THE OLD STANDARD IS SUPERSEDED WHOLE, and with it the two clauses it produced:
// "read-only means no save" (Ctrl+S dropped here) and the structural drop of the
// ctrl+alt render chords (their modifier combination simply matched no
// predicate). ADMITTING Ctrl+S REMOVES AN INCONSISTENCY rather than creating
// one: the close prompt's Save answer has always saved from a locked tab —
// GuiPrompt::respond calls GuiSaveOps::save with no read-only check of its own,
// and the prompt block sits at the TOP of on_key, far above this gate — so the
// keyboard chord was the only save route the lock ever stopped.
// WHAT STAYS BLOCKED, the authoring vocabulary, dropped here at the gate rather
// than admitted for a deeper owner refusal: the marker drop / status-toggle /
// position-nudge / Delete chords, the flag, BPM and MEASURE editors' openers
// (bare `/` among them — its SHIFTED twin, the score-video jump, is admitted
// below, the one key on this gate whose two spellings answer differently),
// `;` (the settings editor, whose engine-key commits ARE authored content), `i`,
// undo/redo (Ctrl+Z / Ctrl+Shift+Z), every propagate command in BOTH families
// (the two COPIES, Ctrl+P and Ctrl+/, explicitly — a copy is non-mutating, but
// it arms a paste that is not, and the pair travels together; the three PASTES,
// Ctrl+Alt+P, Ctrl+Alt+Shift+P and Ctrl+Alt+/, structurally, their modifier
// combinations matching no allowlist predicate) — and `'` IN THE `h` VIEW,
// where it is the load-in-place and replaces the whole authored state. THAT
// ONE ENTRY READS A STATE (2026-09-01): outside the view the same chord opens
// the RENDER PLAYER, which is bare `l`'s own admitted act, so the gate admits
// it there and the entry below is the product's second state-dependent
// admission beside the horizontal arrows' lane term.
// The deeper owner refusals — do_undo / do_redo's per-entry target-tab check
// (undo.cpp), and the pointer AUTHORING refusals (input_pointer.cpp: the marker
// drag arm, the flag editor's double-click open, the empty-lane marker drop) —
// stay as backstops for the mouse and cross-tab paths, no longer the primary
// surface for these keyboard chords.
// THE POINTER TRIM REFUSALS ARE GONE WITH THIS RULING rather than merely
// bypassed: the plain trim-bar press's band gate (input_pointer.cpp),
// trim_bound_click_frame's first gate (input_trim.cpp) and the settings editor's
// typed trim arm (settings_editor.cpp) were all deleted the same day, so the
// whole trim family — keyboard, pointer and typed — is read-only-legal by ONE
// rule with no site left to disagree with it.
// THE LOCK HAS A FACE, AND THIS FUNCTION OWNS ITS MEMBERSHIP (architect
// 2026-08-15): the roster buttons wearing the disabled face while the active
// tab is locked — the four marker verbs (bare `s`, Delete, Ctrl+D, Ctrl+N),
// the Edit flag button and the Measure on the bottom row, the load-in-place
// in the icon row, and since 2026-08-30 THE FOUR CARDINAL ARROWS (planner
// decision 52: Up/Down, dropped outright here; Left/Right, dropped only in
// the marker lane through the is_playhead_step entry, whose lane term is the
// shared owner horizontal_arrow_step_lock_admits that the face reads too) —
// are chords this allowlist drops, so the
// toggle looks the way the `h` history view already looks. The drop's SHIFTED
// chord (Shift+S, 2026-08-28) rides that face rather than asking for a second
// one: the button is greyed by the same arm, and a greyed button's shift press
// is consumed with its plain one.
// THE MIRROR IS NOT AN EQUIVALENCE, and one direction has a member: the
// propagate copy/paste pair is BLOCKED here with no face left to grey (its two
// buttons left with the 2026-08-20 relocation). The other direction is EMPTY
// since the 2026-08-21 sunset: THE MARKER MEASURE was LIT on a locked tab for
// one day although bare `/` dropped here (architect 2026-08-20) — its SHIFT
// half was the score-video jump, admitted here then, and a chrome face cannot
// split — and the jump left the product whole, so the Measure greys under the
// lock with its four neighbours (its arm in redesign_button_enabled). THE MIRROR IS
// HAND-LISTED at
// redesign_button_enabled (app_state.h) rather than derived by walking the
// chord table through this predicate, and the two classes that walk gets wrong
// are recorded there — chords claimed ABOVE this gate, whose "blocked" here is
// vacuous, and the buttons the ruling deliberately leaves lit. SO A CHANGE TO
// THE ADMISSIONS BELOW NEEDS A HAND EDIT THERE; nothing else in the product
// reads this gate for a face — and the TWO STATE-DEPENDENT admissions a face
// mirrors are each read off their own owner rather than off this gate: the
// horizontal arrows' lane term (horizontal_arrow_step_lock_admits) and, since
// 2026-09-01, bare `'`, whose admission turns on the `h` view's own bit, which
// the Load in place button's arm composes for itself.
bool GuiInputHandler::read_only_key_blocked(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    const bool is_o =
        (key == GuiKeys::O && !ctrl && !shift && !alt);
    // THE OPEN PROJECT PICKER (architect 2026-08-28), Ctrl+O — the same letter
    // as the escape chord above and a wholly different act, which is why each
    // gets its own term rather than one loosened test. It is admitted on the
    // header's own standard, exactly as Ctrl+Q and Ctrl+S are: the picker
    // authors nothing — it lists the projects, and choosing one REOPENS the
    // program around another project, discarding this session rather than
    // writing into it, so there is no marker and no engine setting for the lock
    // to protect. Ctrl-exact, through the shared predicate the dispatch arm
    // reads, so the key and this gate cannot drift.
    const bool is_open_project = is_open_project_key(key, mods);
    // SYNCHRONIZE TO EXTERNAL STORAGE, bare `\` since 2026-08-31. It is
    // admitted on the header's own standard and was already read-only-legal
    // through its menu row, which carries no gate of this kind: the act
    // AUTHORS NOTHING — it reads two output folders and writes OUTSIDE the
    // project entirely, onto the folder `sync_path` names — so the lock has
    // nothing to protect from it. Bare-exact through the shared predicate the
    // dispatch arm reads, so the key and this gate cannot drift. NO FACE
    // FOLLOWS IT: the act's one button is a MENU ROW, and a menu item never
    // greys (kFilePopupItems).
    const bool is_sync_external = is_sync_external_key(key, mods);
    const bool is_play_pause = is_play_pause_key(key, mods);
    // THE A/B AUDITION (2026-08-26) is admitted on the header's own standard:
    // it plays and switches tabs — playback and navigation, both already on
    // this list — and writes no store, no setting and no trim. Shift-exact,
    // the dispatch arm's own spelling through the shared predicate.
    const bool is_ab_audition = is_ab_audition_key(key, mods);
    // The horizontal arrows step the PLAYHEAD by their own count of painted
    // columns (one bare, three shifted, ten with ctrl since 2026-08-31), and
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
    // NOT the audition scrub: that is the waveform's one-shot POINTER act
    // (scrub_act_at — the lower half's motionless-release click act, its one
    // entry), a different act on a different surface, untouched here
    // and the sole owner of the "scrub" name.
    // THE LANE TERM READS ITS ONE OWNER (horizontal_arrow_step_lock_admits,
    // app_state.h — planner decision 52, 2026-08-30): the Left / Right
    // buttons' disabled face reads the same owner, so the lock's refusal and
    // the grey are one decision. (It read playhead_in_marker_lane() until
    // then, the lane predicate whose body that owner now is.)
    // AND IT ADMITS ALL THREE MAGNITUDES since 2026-08-31 (R12, the step
    // ladder): Shift+Left / Right steps three painted columns and Ctrl+Left /
    // Right ten, and the modifier scales the step without changing WHOSE step
    // it is — so the lane decides for the shifted and ctrl forms exactly as it
    // decides for the bare one. CTRL+SHIFT stays out (it spells nothing, and
    // an unbound combination needs no admission) and ALT with it.
    const bool is_playhead_step =
        ((key == GuiKeys::Left || key == GuiKeys::Right) &&
         !alt && !(ctrl && shift) &&
         horizontal_arrow_step_lock_admits(app));
    // HOME / END IN BOTH FORMS — bare (the trim-bound jump) and CTRL (the
    // whole-piece jump, 2026-08-24). Both are pure navigation: they move the
    // cursor, stop an audition and clear a selection, and write no store at
    // all, so the ctrl shape is admitted on exactly the reason the bare one
    // always was. Shift and alt forms bind nothing and stay refused.
    const bool is_home_end =
        ((key == GuiKeys::Home || key == GuiKeys::End) &&
         !shift && !alt);
    const bool is_page_updown =
        ((key == GuiKeys::PageUp || key == GuiKeys::PageDown) &&
         !ctrl && !shift && !alt);
    // THE ZOOM STEP PAIR IS CTRL+`=` / CTRL+`-` SINCE 2026-08-27 (bare is
    // vertical, ctrl is horizontal — the magnification pair below holds the
    // bare forms now). Pure navigation either way, which is why the lock
    // admits it.
    const bool is_zoom_symbol =
        ((key == GuiKeys::Equal || key == GuiKeys::Minus) &&
         ctrl && !shift && !alt);
    // THE WAVEFORM MAGNIFICATION PAIR, bare `=` and bare `-` since 2026-08-27,
    // exactly as their dispatch arms spell them. It is a DISPLAY PREFERENCE —
    // the picture's own gain, keyed and persisted like gui_scale — so it
    // authors nothing the lock protects: no marker, no engine setting, no
    // sample. It is admitted on the header's own persistent-mutation standard,
    // exactly as the zoom pair above it is, and the two icon-row buttons
    // stay lit on a locked tab by the same answer. CTRL+0 LEFT THIS ALLOWLIST
    // 2026-08-27 with the reset chord itself: the chord is unbound, and an
    // unbound combination needs no admission.
    const bool is_waveform_magnify =
        ((key == GuiKeys::Equal || key == GuiKeys::Minus) &&
         !ctrl && !shift && !alt);
    const bool is_zero =
        (key == GuiKeys::Digit0 && !ctrl && !shift && !alt);
    const bool is_follow =
        (key == GuiKeys::F && !ctrl && !shift && !alt);
    // THE CENTERED PIN, bare `y` (2026-08-31, R11): follow's sibling and
    // admitted on follow's exact reasoning — a viewport preference is
    // navigation, not authored content. Its button stays lit on a locked tab
    // by the same answer.
    const bool is_centered =
        (key == GuiKeys::Y && !ctrl && !shift && !alt);
    const bool is_center =
        (key == GuiKeys::C && !ctrl && !shift && !alt);
    // Bare `t` (the S/T audio-view switch) IS PURE NAVIGATION AGAIN, and WRITES
    // NO STORE AT ALL since 2026-08-07. It used to write the warp store on one
    // edge — entering target view exited iteration mode through wipe_iter_state,
    // clearing every bracket and pushing an undo entry, admitted here because
    // iter brackets are session-only (never serialized, affects_persistence
    // false, excluded from the render recipe) so the write reached neither disk
    // nor a render. THAT WIPE IS DELETED with the ruling that iteration mode is
    // TARGET-LEGAL (the record is at switch_active_audio_view_to,
    // input_handler.cpp), so the admission now rests on nothing but the
    // persistent-mutation standard above. The former reasoning is kept because
    // TWO OTHER GATES leaned on it (the history mode's allowlist below, and the
    // local walk's frozen-stack premise) and both are re-derived by this
    // deletion.
    const bool is_sub_t =
        (key == GuiKeys::T && !ctrl && !shift && !alt);
    const bool is_sub_p =
        (key == GuiKeys::P && !ctrl && !shift && !alt);
    // Bare 1 / 2 / 3, the ABSOLUTE view selectors (S+W / T+P / T+W). They are
    // admitted for exactly the reason `t` and `p` are, and by exactly the same
    // argument: they RUN those two handlers and nothing else, so they reach no
    // store write at all (the S->T iter wipe that was the one exception is
    // deleted — see is_sub_t above). Nothing new to weigh.
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
    // Bare Escape only, and no modified Escape needs an admission here because
    // none of them binds anywhere (Ctrl+Esc, the one that did, retired on
    // 2026-09-01 when bare Esc took the notification stack whole), so a
    // modified Escape has nothing to be admitted FOR.
    // WHAT BARE Esc IS ADMITTED FOR: the RENDER /
    // BATCH CANCEL and, since 2026-08-31, THE NOTIFICATION STACK'S CLEAR at
    // the bare tail — the two of Esc's places that reach this
    // gate. Neither mutates anything
    // persistent, so both are read-only-safe like every one of Esc's bindings (the
    // authoritative enumeration is at its dispatch point in on_key,
    // input_handler.cpp; no count belongs here), and dropping Esc at this gate
    // would break it. THE REGION HIDE WAS THE OTHER ADMISSION UNTIL 2026-08-21
    // (a locked tab can raise the trim region overlay, bare `[` and its button
    // being read-only-legal on the trim band ruling, so Esc had to be able to
    // put it down again) — with the hide retired, `[` itself is that road both
    // ways and is admitted on its own arm.
    const bool is_esc =
        (key == GuiKeys::Escape && !ctrl && !shift && !alt);
    const bool is_ctrl_q =
        (ctrl && !shift && !alt && key == GuiKeys::Q);
    // THE SAVE (architect 2026-08-07). It writes the state the tab already
    // holds — it authors nothing — and the close prompt's Save answer already saved
    // from a locked tab through the very same owner (the header's inconsistency
    // note). Ctrl-exact, exactly the dispatch arm's own spelling. IT CARRIES THE
    // SAVE AND COMMIT ACT IN THE `h` VIEW since 2026-08-08 and needs no clause
    // for it: publishing a checkpoint of the state the tab already holds authors
    // nothing either, which is the same ruling that admitted the render chords.
    const bool is_save =
        (ctrl && !shift && !alt && key == GuiKeys::S);
    // THE RENDER CHORDS (architect 2026-08-07), both of them, spelled as their
    // dispatch arms are (handle_render_dispatch_keys). A render READS the
    // authored state and writes audio beside the source; it changes no marker
    // and no engine setting, so the lock has nothing to protect from it.
    // Ctrl+Alt+R is the single render, or — with the iteration bit set — the
    // sweep; Ctrl+Alt+Shift+R is the miscellaneous-render cell. THE SAVE AND
    // COMMIT ACT IS ADMITTED THROUGH CTRL+S INSTEAD since 2026-08-08 (the act
    // moved onto the save chord it begins with), which changes nothing about
    // this gate's answer: a save and a checkpoint publish the state the tab
    // already holds and author nothing, and the save entry above admits it.
    // ONE ADMITTED ROUTE WRITES A STORE, and it is RATIFIED rather than merely
    // tolerated (architect 2026-08-07, ruling on it as a named consequence of
    // the reclassification): the ITERATION SWEEP's success tail wipes every
    // marker's iter bracket (wipe_iter_state) and pushes an undo entry for it.
    // The bit is global rather than per-tab, so a tab locked while iteration
    // mode already stood can reach that write. THE ARCHITECT'S THREE REASONS:
    // (1) iteration brackets are NEVER SAVED — session-only, affects_persistence
    // false, excluded from the render recipe, so nothing the lock protects
    // reaches disk; (2) the wipe MUTATES NO MARKER PERMANENTLY, clearing
    // session-only fields and leaving every authored position, tempo and label
    // exactly as it found them; and (3) the sweep LEAVES ITS OWN TRACE in the
    // tmp/ folder — the act is self-documenting output, not silent
    // authoring, which is the property that makes it unlike everything the gate
    // blocks. A locked tab that renders a sweep ends it in the same bracketless
    // state a writable one does. `i` itself is NOT admitted, so the mode cannot
    // be entered or left by key in a locked tab.
    const bool is_render =
        (ctrl && alt && !shift && key == GuiKeys::R);
    const bool is_render_misc =
        (ctrl && alt && shift && key == GuiKeys::R);
    // THE TRIM GESTURES (architect 2026-08-07): Shift+[ maximizes the trim
    // window back to the full song, and bare `[` SHOWS AND HIDES THE TRIM
    // REGION OVERLAY — the other half of the same surface. Trim is BAND, not
    // content (the header), so both are admitted, and their internal behavior
    // is untouched: the maximizer's identity guard, the setter's deselect, the
    // playhead park and the trim-mutation playback stop are all the same code
    // taking the same decisions. The TOGGLE is the easier of the two to admit —
    // it writes no trim bound at all, only a session visibility bit and then
    // the viewport, which is strictly less than the write the band ruling was
    // argued over, the SET act it replaced on 2026-08-18 having written a
    // bound. THE PAIR MOVED ONTO THE BRACKET ON 2026-08-24 (the architect's
    // reason is at the dispatch arms, input_handler.cpp) and the terms below
    // are named for the ACTS rather than for a key, which is what kept this
    // move to a spelling change. The keys the pair left — bare `x`, Shift+X —
    // answer nothing here or anywhere, as Ctrl+Shift+X has not since
    // 2026-08-18: the strict-modifier rule makes an unbound combination a no-op
    // everywhere.
    const bool is_trim_region_toggle =
        (!ctrl && !shift && !alt && key == GuiKeys::BracketLeft);
    const bool is_trim_maximize =
        (!ctrl && shift && !alt && key == GuiKeys::BracketLeft);
    // ADD TO SELECTION (architect 2026-08-18), and it is admitted on the
    // header's own standard rather than a new one: the chord flips a session
    // bit that changes what a PLAIN FLAG CLICK means, and the click it enables
    // — the ctrl-branch membership toggle — is a SELECTION act, which this
    // gate has never blocked. A locked tab has always been able to select
    // markers by click, by Tab and by shift+click; this adds the one spelling
    // of that vocabulary a keyboardless rig had no way to reach. Nothing it
    // enables writes a store: membership, focus and the playhead are all
    // navigation. Bare-exact, exactly the dispatch arm's own spelling.
    const bool is_add_to_selection =
        (!ctrl && !shift && !alt && key == GuiKeys::K);
    // BARE `l` — THE RENDER PLAYER (2026-08-28) — is admitted on the header's
    // own standard: it plays a rendered wav through the engine and authors
    // nothing; the player's one authoring act, the Load in place button,
    // refuses on a locked tab inside the player (the lock's rule, at the act).
    const bool is_play_renders =
        (!ctrl && !shift && !alt && key == GuiKeys::L);
    // BARE `'` IS ADMITTED OUTSIDE THE `h` VIEW AND BLOCKED INSIDE IT
    // (architect 2026-09-01, with the Load in place button's move to the
    // history group): THE STATE SELECTS WHICH ACT THE CHORD IS, and the gate
    // has to ask the same question the dispatch does. Outside the view `'`
    // OPENS THE RENDER PLAYER — the very act bare `l` above already runs on a
    // locked tab, and the player's own Load in place answers the lock for
    // itself, by key and by button, with kTabReadOnlyCard. Inside the view it
    // is the LOAD, which rewrites the whole authored state, and stays blocked
    // exactly as it always was — the outermost refusal for that press, since
    // the mode's allowlist admits the chord while the walk carries a member.
    // The chord was blocked in BOTH states from the feature's birth, on its
    // NAME rather than on its act; the arrows' lane term is the same shape's
    // precedent (one chord, two acts, the state deciding which is admitted).
    // THE FACE FOLLOWS: the Play renders button left
    // redesign_button_enabled's read-only arm with `l`'s admission, and the
    // Load in place button left it with this one — its lock term rides the
    // history group's arm now, where it composes with the mode (app_state.h).
    const bool is_load_in_place_player =
        (!ctrl && !shift && !alt && key == GuiKeys::Apostrophe &&
         !app.history_mode.active);
    // THE VALUE PAIR — bare `j` and Shift+`j` (2026-08-29) — is admitted on
    // the header's own standard: neither authors anything. `j` composes the
    // focused marker's resolved value and hands it to the compositor's
    // clipboard; Shift+`j` switches the A/B tab, selects the marker that value
    // came from, lands the playhead on it and centres it — a tab switch, a
    // selection, a playhead and a camera, every one of them navigation this
    // gate has never blocked.
    // Shift-exact through the shared predicates, so these two entries and the
    // dispatch arms cannot drift. THE FACE FOLLOWS THE KEYS: the Copy resolved
    // value button is NOT in redesign_button_enabled's read-only arm, so a
    // locked
    // tab leaves it lit exactly as it leaves both chords live.
    const bool is_copy_value          = is_copy_value_key(key, mods);
    const bool is_jump_to_value_source = is_jump_to_value_source_key(key, mods);
    // SHIFT+S IS BLOCKED, and it needs no term of its own to be: it drops a
    // phase reset from any view (2026-08-28) — authored content, exactly what
    // bare `s` drops and exactly what this gate refuses — and the is_save
    // entry above is ctrl-exact, so the shifted spelling reaches no admission
    // and falls out at the default. THE FACE FOLLOWS THE KEY WITH NO SECOND
    // EDIT: the Drop marker button is already one of the four verbs the lock
    // greys, and its press arm consumes a SHIFT press on a disabled button
    // exactly as it consumes a plain one (arm_redesign_press, one predicate
    // for both routes), so the button's shift-click and its long press refuse
    // where the key refuses.
    // BARE `/` IS BLOCKED and is not admitted by any entry here: it opens the
    // measure EDITOR, and a measure is serialized content. (Shift+`/` was the
    // score-video jump's lock-legal admission from 2026-08-20 until the
    // 2026-08-21 sunset removed the jump whole; the chord is unbound and the
    // strict-modifier rule makes it a no-op everywhere, so no entry answers it.)
    // Ctrl+Z (undo) and Ctrl+Shift+Z (redo) — the whole family, alt binding
    // nothing on it — are NOT on the allowlist: both drop at this gate. The
    // old design admitted them because an undo entry
    // may target the OTHER (writable) tab, deferring the real decision to
    // do_undo / do_redo's per-entry target-tab peek. Under the gate-block,
    // undoing from a read-only tab first requires switching to the writable
    // tab (Ctrl+Tab) — accepted for gate legibility, so that authoring
    // mutations stop uniformly at the gate. The target-tab peek in undo.cpp
    // survives as a backstop for entries that outlive a mid-history lock.
    // Delete, `;`, `i` and the propagate copy/paste chords are likewise
    // absent (blocked here); `'` LEFT that list on 2026-09-01 for a
    // state-dependent entry of its own, blocked in the `h` view and admitted
    // outside it (is_load_in_place_player above). The trim gesture left it on
    // 2026-08-07 — see is_trim_region_toggle above.
    return !(is_o || is_open_project || is_sync_external ||
             is_play_pause || is_ab_audition ||
             is_playhead_step ||
             is_home_end || is_page_updown ||
             is_zoom_symbol || is_waveform_magnify || is_zero ||
             is_follow || is_centered || is_center || is_sub_t || is_sub_p ||
             is_view_selector ||
             is_tab_cycle || is_ctrl_tab || is_ctrl_shift_tab ||
             is_esc || is_ctrl_q ||
             is_save || is_render || is_render_misc ||
             is_trim_region_toggle || is_trim_maximize ||
             is_add_to_selection || is_play_renders ||
             is_load_in_place_player ||
             is_copy_value || is_jump_to_value_source);
}

// -- THE HISTORY MODE'S OWN KEYS AND ITS ONE KEYBOARD ALLOWLIST -------------
//
// The mode itself, what opens and closes it and why the frozen now side is safe
// are all stated ONCE, at AppState::HistoryMode (app_state.h). What lives here
// is the mechanism.

// Leave the mode, clearing it WHOLE — the commit walk with it, so the next entry
// re-inits and measures against the state at THAT moment. The one exit owner:
// every closer calls this rather than clearing fields themselves. Idempotent, so
// a closer may fire with the mode already down.
//
// THE CALLER INVENTORY, and the ONE authoritative site for it (re-derived by
// grep 2026-08-09, when the failed-scan closer arrived) — SEVEN callers, of
// which SIX can actually be running with a view up:
//   * handle_history_mode_key — bare `h`, the toggle's close arm;
//   * on_history_prefetch_ready — THE ONE CLOSER THAT IS NOT A USER ACT
//     (2026-08-09): a scan that finishes FAILED while the view stands ends the
//     visit, the view's premise being that this session knows the piece's
//     history. It is also the one closer that can fire with a POPUP up (below);
//   * run_history_commit — the Save-and-Commit act, once its save has landed;
//   * run_history_revert — bare `v`, which rewrites the state the now side was
//     measured against and so must not leave the lane describing it;
//   * load_history_commit_in_place and load_history_local_entry_in_place — the
//     mode's own `'`, one act per walk;
//   * load_render_entry_in_place — the RENDER PLAYER's load, which has ONE
//     caller (confirm_load_in_place's player arm) and therefore CANNOT run
//     with the view standing: the player's opener refuses in the mode, where
//     bare `'` raises the same prompt on a walk member and routes to one of
//     the two above.
//     Its call is the idempotent no-op this function's early return
//     exists for, placed at the mutator so the close travels with the act.
// There is no pointer closer, and no closer outside this file's two acts, its
// three loads and the prefetch arrival hook.
//
// WHAT IT DOES NOT DO ANY MORE IS TOUCH THE WINDOW (architect 2026-08-18, "this
// removes machinery"). From 2026-08-05 this was also the ONE site that put the
// EDITOR'S NAVIGATION BAND back — the entry stashed the live viewport, zoom and
// playhead and this exit restored them, on the reasoning that the view is a
// VIEWER and a review's pans and zooms are the review's. The architect reversed
// that whole ownership: the window is the USER'S throughout, entry and exit
// included, so the view leaves it exactly where the review left it and each A/B
// tab keeps whatever band it had. THE THREE LOAD-IN-PLACES ARE UNAFFECTED, and
// the asymmetry that made this worth stating is GONE: all three route through
// apply_recipe_in_place (2026-08-24), which writes the two marker columns and
// the engine block and NOTHING ELSE — no tab band, no view bits, no clamps — so
// the COMMIT load applies no band either now, and there is no restore under or
// over any of them. AppState::HistoryMode's field block owns the ruling and the
// record of what went.
//
// THIS OWNER DOES NOT CLOSE A DROPDOWN, and since 2026-08-09 that is a decision
// rather than an unreachable case. EVERY USER-ACT CLOSER STILL CANNOT FIRE WITH
// ONE UP, positionally: bare `h`, the Ctrl+S checkpoint act, the bare `v` revert
// and the loads behind `'` are all KEYBOARD routes dispatching BELOW on_key's
// popup gate, which swallows every chord but Ctrl+Q while a menu is up (Ctrl+Q
// closes the popup itself and then takes the close-window route, which ends the
// process rather than the view; the WM close is the same), and there is no
// pointer closer at all. THE PREFETCH ARRIVAL IS THE EXCEPTION — it runs off a
// poll, not a key, so the FILE menu, which opens inside the view (2026-08-13,
// on the architect's 2026-08-08 ruling that first opened the deleted Navigation
// menu in here), may be up when a failed scan ends the visit. A standing menu is
// LEFT STANDING there, deliberately: it is row 1's own surface, its item is a
// live command in the editor exactly as it was in the view, and nothing
// about it named the mode. Closing it would be a second dismissal riding an
// event the user did not cause.
void GuiInputHandler::close_history_mode() {
    if (!app.history_mode.active) return;
    // (THE EXIT'S OVERLAY HIDE IS DELETED, architect 2026-08-19, with the
    // walk step's and the reading switch's — THE MODE'S THREE OWN EDGES as one
    // class. THE OVERLAY'S VISIBILITY IS NOT A PLAYHEAD, SELECTION OR MUTATION
    // CONCERN: it is a view preference about whether the user is looking at the
    // trim, and this view neither touches the trim nor offers a way to raise or
    // lower the overlay — bare `[` is consumed in the mode and its button greys
    // — so an overlay shown before `h` must survive the visit intact or the
    // user gets it back only by pressing a key the mode has taken away. The
    // 2026-08-05 view-local reading it inherited died with the view-local span
    // itself on 2026-08-18. The mode's PLAYHEAD-MOVING and SELECTION-CHANGING
    // routes still hide, exactly as their live twins do.)
    // THE SESSION COUNTER SURVIVES THE RESET, alone among the fields, because it
    // counts VISITS rather than describing one: letting it fall back to zero
    // would let a close-then-open pair reissue a number the flag cache has
    // already seen, which is the very collision the counter exists to prevent
    // (an `h` off and an `h` on delivered in one dispatch batch reach the paint as
    // a single edge, with no intervening rebuild to notice `active` blinking).
    const unsigned long long generation = app.history_mode.generation;
    // (THE PARKED BAND'S RESTORE IS DELETED — architect 2026-08-18, "this
    // removes machinery". From 2026-08-05 this exit read the entry's stashed
    // viewport / zoom / playhead trio and its audio view out before the
    // whole-struct reset destroyed them and applied them after, translating the
    // two frame-shaped values through the warp map when a `t` inside the view
    // had flipped the domain and re-landing a surviving selection on its focus
    // over the top. THE VIEW OWNS NO NAVIGATION STATE now — the window is the
    // user's across both edges, which is what lets one A/B tab stand framed
    // wide while the other stands zoomed — so there is nothing to read out,
    // nothing to translate and nothing to put back. The field block at
    // AppState::HistoryMode owns the ruling.)
    app.history_mode = AppState::HistoryMode{};
    app.history_mode.generation = generation;
    drop_lane_stash_across_history_edge();

    // AND THE LIVE LANE IS REPUBLISHED IN THIS SAME PRESS (architect 2026-08-07,
    // the FOURTH REPUBLISHING EDGE — so the close is symmetric with the three
    // in-view ones, and the exit's own one-tick blank, recorded at the drop as
    // that fix's remainder for a few hours, is closed). THE MODE IS ALREADY DOWN at this point (the
    // whole-struct reset ran above), so the rebuild takes the flag cache's LIVE
    // arm and publishes the session's own markers, their hit rects and their
    // stems — exactly what the leaving view owes the editor.
    //
    // IT IS THE EXIT'S ONLY REBUILD since 2026-08-18: the parked-band restore
    // that used to sit above it ran a synchronous rebuild of its own whenever
    // it MOVED something (apply_zoom_to_start's), so this call had to be placed
    // strictly below it or be the one erased. With no restore left there is
    // nothing above it and nothing to order it against — the exit writes no
    // viewport at all now.
    //
    // THE LOAD-IN-PLACE CLOSERS PAY A REDUNDANT REBUILD HERE and are
    // deliberately not special-cased: each calls this closer on the first line
    // past its last refusal and then rebuilds again at its own tail, over the top
    // of this — the same "not wasted either" reasoning the restore above carries
    // (which is where the per-act membership and the band difference between them
    // live), at the price of one plate render on a keypress that is already
    // loading a state.
    republish_history_lane_now();

    // A DISCRETE COMMAND, so FULL-WINDOW DAMAGE (the CADENCE rule's discrete
    // class): the lane swaps its whole content, the stems in the waveform swap
    // with it, and row 8's state cell rewrites its `n/N shortsha` walk line.
    // Narrow damage would have to know all three, and none of them is worth a
    // rect. It
    // covers the restore's and the republication's own damage too, which is why
    // both above emit theirs and nothing here has to widen for them.
    viewport.invalidate_all();

    // (THE DEFERRED PREFETCH KICK'S FLUSH stood here from 2026-08-07 and is
    // DELETED 2026-08-29 with the bit it read: no route can park one — the
    // funnel's own comment carries the three kickers' proof — so this was a
    // flush of a bit nothing could ever set.)
}

// DROP THE LANE'S PUBLISHED CONTENT AT EVERY MODE EDGE — all THREE members of
// it, at the entry, the exit, each walk step and each SWITCH (the fourth edge,
// 2026-08-05: a different walk or a different reading is a different list, so a
// switch replaces the lane's content exactly as a step does — one edge whether
// it arrived from bare `g`, the walk radios' clicks or bare `u`, all of them
// going through set_history_reading): the pointer stash (flag_hit_rects), the stem
// painter's stash (marker_stems — paint-only since the stems-inert ruling,
// 2026-08-12, but its `marker_index` still changes domain across the edge and
// the playhead's stem-suppression decider reads it) and the diff-flag LIST
// their indices name.
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
// THE LIST ITSELF IS THE SAME ARGUMENT WIDENED (2026-08-05): app.history_mode.-
// flags is paint-cache OUTPUT, rebuilt only by that same once-per-tick pass, so
// between an edge and the next tick it still holds the LEAVING commit's flags —
// and the keyboard reaches it directly, with no stash in between. Key events
// arrive in BATCHES (the compositor's pending queue drains whole before the
// tick, the same batching the mode's session counter already argues from), so a
// `,` and a Tab delivered together would cycle the old commit's list: the
// playhead lands on a flag that is no longer shown, and the ordinal it stored
// then brightens whatever flag the NEW commit has at that ordinal once the
// rebuild lands — focus and playhead describing different flags, which is
// exactly what `focus` indexes the painted list to prevent.
//
// Clearing is the whole fix and it is the lane's own rule applied: nothing is
// clickable OR NAVIGABLE that is not drawn, and for as long as the lane stands
// empty the answer to every lane hit and every cycle step is "nothing", which is
// the correct cold answer rather than a wrong warm one. The cost is a frame's
// stems, absent instead of stale — and the edge's own full-window damage is
// already repainting. (That span is now a handful of LINES rather than a frame at
// every one of the four edges; the next paragraph is the whole of it.)
//
// A SYNCHRONOUS FLAG REBUILD AT EVERY EDGE IS EXACTLY WHAT THEY DO NOW
// (architect 2026-08-07), SUPERSEDING this site's own "heavier than a one-tick
// window warrants" weighing — the window is VISIBLE. Nothing else in a step or a
// reading switch republishes the lane inside the press: each painted a BLANK
// lane for a frame before the arriving commit's flags landed, and a blank frame
// between two contents is a flicker whatever removing it costs. (That was true
// even while those edges framed the viewport, the walk being read at full zoom
// out where the framing moved nothing; since 2026-08-08 they write no viewport
// at all — and since 2026-08-18 no edge does — so the call below is the only
// republication any of them has.) All four edges
// now call republish_history_lane_now (below) as their last act — the three
// in-view ones as the tail of the press, the EXIT after the whole-struct reset,
// where the mode is already down and the rebuild publishes the LIVE lane the
// editor is coming back to (that fourth call landed hours after the first three,
// closing the one window this comment recorded as the fix's remainder). So the
// lane's old content is replaced only once the new one is ready, atomically
// inside the press, with no stale-hit window on either side of the swap, and the
// ENTER and LEAVE edges are symmetric.
//
// THE DROP STAYS — as that rebuild's own pre-step, and as the COLD ANSWER for the
// frames the rebuild cannot serve: maybe_rebuild_flag_cache refuses while the
// audio is loading or absent and before the first plate has published, and an
// edge reached in any of those states leaves the emptied lane exactly as
// described. So what died is the ROUTINE one-tick window, not the empty answer
// itself: every reader above still has to answer "nothing" correctly, and still
// does.
//
// A VIEW SWITCH INSIDE THE MODE IS DELIBERATELY NOT ONE OF THESE EDGES
// (2026-08-04, when `t` / `p` / 1 / 2 / 3 joined the keyboard allowlist): it
// already ends in kick_waveform_sync, whose tail rebuilds the flag cache INLINE
// — the same republication every edge now reaches for, arrived at as part of
// the switch itself — so there is no frame to protect.
// Nor is there a domain change to protect against: on both sides of a view
// switch `marker_index` indexes app.history_mode.flags, the mode owning the lane
// throughout. Dropping anyway would be worse than redundant if it ran after the
// kick, erasing the geometry that kick had just published. (The LIVE lane leans
// on the very same synchronous rebuild across `p`, where its stash genuinely
// does change domain — warp store index to phase-reset store index — so the
// mode is asking no more of that route than the live columns already do.)
//
// AND THE A/B TAB SWITCH JOINS THAT CLASS RATHER THAN THIS ONE (2026-08-18,
// when Ctrl+Tab was admitted inside the view). BOTH HALVES OF THE VIEW
// SWITCH'S ARGUMENT HOLD FOR IT, checked rather than assumed: the mode still
// owns the lane on both sides, so `marker_index` indexes
// app.history_mode.flags throughout and no domain changes; and
// switch_active_tab_view_to ends in the same kick_waveform_sync, so the
// arriving band's flags are republished inside the press. THE LIST ITSELF DOES
// NOT CHANGE ACROSS A TAB SWITCH AT ALL, which is the stronger fact: the two
// tabs share both marker stores and the engine settings, so the delta a switch
// arrives at is the delta it left — only the WINDOW the flags are laid on
// moves.
void GuiInputHandler::drop_lane_stash_across_history_edge() {
    app.flag_hit_rects.clear();
    app.marker_stems.clear();
    app.history_mode.flags.clear();
}

// REPUBLISH THE LANE INSIDE THE PRESS — the last act of ALL FOUR mode edges
// (architect 2026-08-07, the step flicker's fix, extended to the exit the same
// day: the entry, the walk step, the reading switch and the close; the drop above
// carries the argument and the weighing this superseded).
//
// IT IS THE SAME CALL AT ALL FOUR, and WHICH lane it publishes falls out of where
// each caller puts it rather than out of an argument here: the three in-view
// edges call it with the mode standing, so the rebuild takes its history arm and
// publishes the arriving delta's flags; the exit calls it with the mode already
// down, so the rebuild takes a live-column arm and publishes the session's own
// markers. One function, one route, and the flag cache's own arm selection is the
// only thing that differs.
//
// IT IS THE VIEW SWITCH'S OWN ROUTE, unforked: Viewport::kick_waveform_sync
// renders the plate synchronously, publishes the displayed fingerprint and
// rebuilds the flag cache INLINE at its tail — and that tail is the whole point
// of this call, the mode's lane content (app.history_mode.flags plus the two
// pointer stashes over it) being that rebuild's history arm's output. There is
// deliberately NO flag-only reach: the `p` column toggle faced this exact choice
// on 2026-07-30 and joined the view-switch class rather than growing a second
// kick, on the reasoning that the redundant plate render is one discrete
// keypress's cost — and the mode is asking no more of that route than the live
// columns already do.
//
// NO EDGE PAYS A DOUBLE RENDER ANY MORE, and the cost is recorded because the
// shape it argued for is what stayed. An edge whose viewport write MOVED
// something had already kicked a plate render of its own, so the press rendered
// the plate twice (the flag cache did not rebuild twice — it is
// fingerprint-guarded, and the first kick had published the arriving lane).
// From 2026-08-08 that was two edges, the ENTRY's framing and the exit's
// parked-band restore, and on 2026-08-18 both writes were deleted with the
// view's navigation-state ownership — so no edge writes a viewport at all and
// this call is every edge's only render. ONE SHAPE AT EVERY EDGE is what was
// worth the redundancy, and skipping the call by testing whether a write moved
// would have made the fix depend on an inference about another function's
// internals. The load-in-place closers pay a redundant rebuild for a different
// reason, stated at the exit (which owns the closer inventory).
//
// THE ORDER IS FIXED at every caller, and it is ONE rule at all four now that
// the view writes no viewport anywhere: this call comes LAST of the acts that
// change what the lane should show. In view that is the state write (the walk
// index, or the source/reading pair), the focus clear, the stash drop and the
// region hide, THEN this; at the exit it is the whole-struct reset, the stash
// drop, THEN this. Everything the rebuild reads must already be true —
// `history_mode.active` decides which arm of the flag cache runs, and the
// index, focus and compare fields decide what that arm publishes, so a call
// placed above any of them publishes the state the press is leaving.
// THERE IS NO VIEWPORT PREREQUISITE LEFT (2026-08-18): the entry's framing and
// the exit's parked-band restore were the two writes this had to sit below
// (each ran a synchronous rebuild of its own, so a call placed above one was
// the one erased), and both are deleted with the view's navigation-state
// ownership. Only `viewport.invalidate_all()` follows this call at every
// caller, damage being the last thing every edge does.
void GuiInputHandler::republish_history_lane_now() {
    viewport.kick_waveform_sync();
}

// ENTER THE MODE ON A FRESH SESSION — the one entry owner, the mirror of
// close_history_mode. Everything about a visit is built here and nowhere else:
// a new commit walk, and a NOW SIDE CAPTURED AT THIS INSTANT, which is what
// makes the deltas describe the state the user is actually looking at.
//
// ONE CALLER since 2026-08-05 — bare `h`, the toggle's open arm. The second was
// the commit act, which re-entered the mode in place on the checkpoint it had
// just made; the architect's ruling that day made the act CLOSE the view
// instead, so the re-entry is gone. This stays a function rather than eight
// lines inside the toggle because it is the ENTRY OWNER: everything a visit is
// made of is here, so a future entry inherits the whole shape — walk, index,
// cleared focus, head delta, generation bump, lane-stash drop — instead of
// reproducing part of it.
//
// UNAVAILABLE IS A CONSUMED NO-OP: init() has already put its own one line on
// stderr naming the reason, and that is the whole story — no new UI surface, no
// red flash, and above all no half-open mode. The existing mode state, if any,
// is left untouched, because the fresh session is built beside it and only moved
// in once it is known good.
bool GuiInputHandler::open_history_mode_fresh() {
    // THE STALENESS KICK, ABOVE EVERYTHING (2026-08-07): the walk lives in the
    // prefetch store now, and a store describing another source, another
    // projects_repo or a branch tip that has moved would answer this visit out
    // of the wrong history. It runs BEFORE the bind below, so init() binds to
    // the fresh generation — and before `active` goes up, so the kick is not
    // the deferred one.
    kick_history_prefetch_if_stale();
    AppState::HistoryMode fresh;
    if (!fresh.session.init(app, history_prefetch)) {
        // THE REFUSED ENTRY SAYS SO (architect 2026-08-30). init() already
        // prints the line on stderr with the store's own reason appended;
        // the card is that same composed sentence on screen — one composer
        // (kHistoryUnavailable, history_diff.h), the stderr line unchanged —
        // so a `h` that opens nothing is not a dead key on the tablet, where
        // there is no terminal to read. THE CARD IS THIS OWNER'S, not
        // init()'s: the module has no notifications and this is the ONE
        // entry, so there is exactly one card per press by construction.
        // (The OTHER reporter of the same fact — the prefetch arrival that
        // closes a standing view — raises its own, an event the user was not
        // watching rather than an answer to a press.)
        notifications.notify(AppState::NotificationClass::Normal,
                             std::string(kHistoryUnavailable) + ": " +
                                 fresh.session.unavailable_reason());
        return false;
    }
    // THE SECOND WALK, BOUND TO THE SAME NOW SIDE (2026-08-07). It costs no git
    // and no formatting here — the undo stack's size, the settings writer's GUI
    // half and a copy of the three frozen strings — because every member text is
    // serialized on first ask. It is bound BELOW the session's init for the one
    // reason that matters: the now side it takes is the session's own capture,
    // so the two walks cannot come to disagree about what "now" is.
    //
    // IT RIDES THE MODE, IT DOES NOT CARRY IT: entry is gated on the COMMIT
    // side's HEADER resolving and on nothing else — which repository, which
    // piece, which source — never on how many members the walk turned out to
    // have. So a piece whose every checkpoint refuses the strict load opens
    // just as one with a hundred good ones does, and the local walk is read in
    // there beside a blank Remote lane (architect 2026-08-09; the empty walk's
    // ruling is at GuiHistoryDiff::init).
    fresh.local.init(app, fresh.session.now_side());
    // AND IT OPENS WHERE THE SESSION STANDS (architect 2026-08-08), which is the
    // one place the two walks' entry positions differ. The commit walk opens at
    // 0 because its newest member is where the session is; the LOCAL walk's 0 is
    // the FURTHEST FUTURE state — the far end of the redo stack — and the state
    // on screen is the LIVE member, at the captured redo count. With no redo
    // entries the two coincide, which is why this reads as "still the newest" on
    // an ordinary session. The whole-struct reset zeroes local_index, so this is
    // an assignment the entry makes rather than a default the initializer could
    // carry: only a bound walk knows where its live member is.
    fresh.local_index = fresh.local.live_index();
    // THE ENTRY STOPS A LIVE AUDITION (architect 2026-08-05, with playback's
    // removal from the view): the mode consumes bare Space and never runs a
    // scrub act, so a session still running from before `h` could not be stopped
    // from inside — it would play on under a view that offers no transport at
    // all. THE OWNER IS THIS ENTRY OWNER rather than the toggle's arm, for the
    // reason everything else about a visit lives here: a future second entry
    // inherits the whole shape. It is REFUSAL-GATED like every other stop in the
    // product (the rule at stop_playback_if_playing) — below init()'s
    // UNAVAILABLE return, so a mode that does not open silences nothing.
    playback_lifecycle.stop_playback_if_playing();
    fresh.active = true;
    fresh.index  = 0;      // the newest commit
    fresh.focus  = -1;
    // (THE HEAD DELTA IS MEASURED BELOW, once the session is moved in: it may
    // have nothing to measure yet — a visit can open before the prefetch has
    // delivered member 0 — so the measurement is its own owner now rather than
    // two lines here. measure_history_head_delta owns the rule.)
    // EVERY ENTRY IS A NEW GENERATION. The flag cache identifies the mode's
    // content by (active, index, focus, compare) plus this, and two sessions of
    // the same piece open in the same shape — index 0, focus -1, iterative,
    // `active` true — so a
    // close and a reopen the paint never sees between would otherwise be
    // indistinguishable from no change at all, leaving the previous session's
    // diff flags on screen. The bump is HERE rather than at the call site
    // because this is the one entry owner, so a second caller inherits it.
    fresh.generation = app.history_mode.generation + 1;
    // (THE ENTRY PARKS NOTHING AND FRAMES NOTHING — architect 2026-08-18, "this
    // removes machinery". From 2026-08-05 it stashed the live viewport, zoom,
    // playhead and audio view here for close_history_mode to put back, and then
    // opened the visit at FULL ZOOM OUT through a framing owner of its own —
    // the whole song in the window, the reading position a checkpoint review
    // starts from, and THE ONLY EDGE THAT FRAMED since 2026-08-08. Both are
    // deleted: the view owns no navigation state at all, so it opens over
    // whatever window the user is already in and leaves it there. That framer
    // outlived the entry by one caller and is deleted with it later the same
    // day, the trim bar's double-click having gone back to the ordinary span
    // framing.)
    app.history_mode = std::move(fresh);
    measure_history_head_delta();
    drop_lane_stash_across_history_edge();
    // AND THE LANE IS REPUBLISHED IN THIS PRESS (2026-08-07): the view opens
    // showing the newest checkpoint's flags rather than an empty lane that fills
    // on the next tick. The edges' one shape — its owner carries the reasoning.
    republish_history_lane_now();
    viewport.invalidate_all();
    return true;
}

// THE HEAD DELTA'S ONE MEASUREMENT SITE (architect 2026-08-07, generalizing the
// entry's own two lines). It answers "is there anything to checkpoint" — index
// 0, the newest checkpoint, against the now side init() froze — and the answer
// is static for the visit once made, both sides being fixed for the session's
// life (the field's own comment, AppState::HistoryMode::head_delta_empty, owns
// the full reasoning and the recorded asymmetry).
//
// WHAT THE STREAMING WALK ADDED is a window in which there is nothing to
// measure: a visit may open before the prefetch has delivered member 0 at all.
// The bit RESTS TRUE there — the conservative face, since the act is greyed and
// the chord refused while the answer is unknown — and this runs again at every
// prefetch arrival while the view stands, taking the measurement the first time
// member 0 exists. `head_delta_measured` is what makes that "the first time":
// after it, this is a no-op whatever else arrives.
//
// AND A FINISHED WALK WITH NO MEMBER AT ALL ANSWERS COMMIT-WORTHY (architect
// 2026-08-09, with the empty walk becoming a legal standing state): the window
// above closes when the run reports DONE, and if nothing arrived in it the
// answer is not unknown any more — with no eligible baseline to measure
// against there is BY DEFINITION everything to checkpoint, and the act must be
// live or the view has no way to create the first member. It is a measurement
// like any other, so the bit latches and the mid-scan greyed face stands until
// the run says it is done.
//
// CUMULATIVE, EXPLICITLY, and it is the one reader of a delta that names a
// compare mode rather than passing the session's bit: the act commits the LIVE
// state, so the question is live-vs-newest whatever reading the lane shows.
// Since the iterative reading turned FORWARD (architect 2026-08-05) the two
// coincide at index 0, so the name currently picks the same delta the bit would
// — kept explicit anyway, the coincidence being a property of the pairing and
// not of this question.
//
// A MISSING DELTA AT A NON-EMPTY WALK leaves the bit untouched and UNMEASURED,
// so a later arrival can still answer. It is not a reachable state (an available
// session's index 0 resolves whenever a member exists), and the resting TRUE is
// the same conservative face the empty window wears.
void GuiInputHandler::measure_history_head_delta() {
    if (!app.history_mode.active) return;
    if (app.history_mode.head_delta_measured) return;
    if (app.history_mode.session.commit_count() == 0) {
        if (!app.history_mode.session.walk_finished_empty()) return;
        app.history_mode.head_delta_empty    = false;
        app.history_mode.head_delta_measured = true;
        return;
    }
    const GuiHistoryCommitDelta* head = app.history_mode.session.delta_at(
        0, GuiHistoryCompare::Cumulative);
    if (!head) return;
    app.history_mode.head_delta_empty    = head->is_empty();
    app.history_mode.head_delta_measured = true;
}

// -- THE PREFETCH'S THREE EDGES (architect 2026-08-07) ----------------------

// START A FRESH SCAN — the ONE funnel, and the one place the deferral lives.
// Its three kickers, re-derived by grep on this name: main.cpp's startup load
// tail (once the source has settled), on_history_checkpoint_complete for every
// outcome that MAY have committed (four of the six — that site owns the
// derivation), and kick_history_prefetch_if_stale below.
//
// IT SUPERSEDES WHATEVER IS RUNNING, and no caller has to ask: the store's kick
// bumps the generation, clears the queue and replaces the pending run, so a scan
// begun against an older tip is abandoned between candidates and its queued
// members are dropped by tag. The freshness short-circuit that can DECLINE to
// kick lives one function down, and only the `h` entry goes through it.
//
// NO KICK CAN ARRIVE WHILE THE VIEW STANDS, and the three kickers are the whole
// proof (re-derived by grep 2026-08-29): the startup load tail runs once with
// the view down and no way to have opened it; the `h` ENTRY's own
// kick_history_prefetch_if_stale runs BEFORE `active` goes up, deliberately, so
// init binds to the fresh generation; and the checkpoint completion's re-warm
// runs after run_history_commit has already closed the view, which bare `h`
// then refuses to reopen while the bit stands. (A DEFERRAL BIT stood here from
// 2026-08-07 to 2026-08-29 for the case none of those three can produce — a
// kick parked rather than run because a visit is BOUND to the store's
// generation and a restart would clear the deque its indices name — and it was
// deleted with its flush at close_history_mode as producer-less, the project's
// rule for machinery no route reaches. The ARGUMENT it encoded still holds and
// is why the entry kicker's placement above `active` is load-bearing rather
// than incidental: a future fourth kicker that could fire under a standing
// visit has to answer this question again.)
void GuiInputHandler::kick_history_prefetch() {
    history_prefetch.kick(app.source_audio_path, app.projects_repo);
}

// THE STALENESS TEST, and the `h` entry's own kick. The store is FRESH for this
// visit when all three of its subject terms still hold: the same source, the
// same projects_repo, and the same branch tip it was built against. Anything
// else and the walk describes a repository this session is no longer asking
// about.
//
// A RUN STILL IN FLIGHT IS FRESH BY DEFINITION, which is what covers the window
// before its header (and with it the tip it read) has arrived: the run was
// kicked for THIS subject and started against a tip nobody has read yet, so
// re-kicking it would only restart the scan the entry is about to stream from.
// THE RUNNING SCAN IS ALWAYS THE NEWEST KICK'S, which is what keeps that
// shortcut honest across a checkpoint: the completion re-warms through the
// FUNNEL, which supersedes rather than asking this question, and a kick that
// arrived while a view stood is flushed at the exit before any later entry can
// reach this line.
//
// THE TIP READ IS TWO `rev-parse`s on this thread — one deriving the clone from
// the loaded source (2026-08-11: there is no compiled-in root to read against any
// more) and one for the tip itself — which is still the whole of what an ordinary
// entry pays in git, against the log plus a strict load per candidate it used to.
void GuiInputHandler::kick_history_prefetch_if_stale() {
    const bool same_subject =
        history_prefetch.subject_source_path() == app.source_audio_path &&
        history_prefetch.subject_projects_repo() == app.projects_repo;
    if (same_subject) {
        if (history_prefetch.running()) return;
        if (!history_prefetch.tip_sha().empty() &&
            history_prefetch.tip_sha() ==
                read_history_branch_tip_sha(app.source_audio_path)) {
            return;
        }
    }
    kick_history_prefetch();
}

// THE ARRIVAL HOOK — the platform's prefetch ready fd, once per POLLIN, whatever
// the counter said (the signal means "the queue has something", never how much).
//
// The DRAIN is unconditional: the store is the app's, not the view's, and it
// must stay current whether or not anyone is looking. What is conditional is the
// REACTION, and it is the whole of what a growing walk needs while the view
// stands:
//   * the head delta gets its one measurement the moment member 0 exists;
//   * the window is damaged, because `n/N` in the bottom corner and the diff
//     lane both read a count that just changed — the lane through the flag
//     cache's own fingerprint field, which is why an empty-walk entry's first
//     member repaints rather than sitting blank.
// FULL-WINDOW DAMAGE, the mode edges' own class: the corner and the lane are two
// surfaces and neither is worth a rect.
//
// TWO EDGES REACT, NOT ONE (2026-08-09): an APPEND, above, and the run BECOMING
// DONE, which is the other moment the head delta can be answered — a scan that
// finishes having delivered nothing turns the act from "unknown, so greyed" into
// "everything to checkpoint" (measure_history_head_delta owns that rule), and the
// Save button's face has to follow inside the same frame. A header arriving alone
// still costs nothing.
//
// AND A RUN THAT FINISHES FAILED ENDS THE VISIT (2026-08-09). The view's premise
// is that this session knows the piece's history; a `git log` capture that could
// not run means it does not, and the `0/0` blank lane would then be a LIE — it
// says "no checkpoints" where the truth is "unknown" — with the act greyed and
// no account anywhere of why. So the failure gets the refusal it would have got
// a moment earlier: init's own one-line stderr shape, printed here from the
// reason the store recorded, and the view CLOSED through the one exit owner, so
// the lane republishes exactly as any other close does. The next `h` meets init's stable refusal with the same line.
//
// IT IS NOT A MODAL and raises nothing: nothing asynchronous in this product
// does. It is a visit ending because what it was reading stopped existing.
//
// AND IT SETTLES ANY HELD POINTER GESTURE FIRST, which is what makes it safe to
// be the product's ONE ASYNCHRONOUS CLOSER. Every OTHER closer is a keyboard
// route below on_key's DRAG-MODAL GATE, so none of them can run with a gesture
// live — the whole reason close_history_mode ends none. This one arrives on a
// poll and bypasses that gate, and the view has gestures live in it (the
// region drag and the one nav drag, pan and ctrl-zoom phases alike): left held across the
// reset, the next motion would grow a VIEW-LOCAL region in the EDITOR from an
// anchor the view took, or pan on behalf of a view that is no longer up.
// finalize_active_drags is the existing force-end — the same release bodies the
// Ctrl+Q hatch and main.cpp's resize and WM-close callbacks run — so "any end
// commits" stays true, no cancel semantics appear, and by the time the mode
// resets the gate's invariant holds again exactly as it does for a key. It runs
// BEFORE the close so each gesture ends against the state it was made in, and
// the close's own region hide and band restore then land over the top.
//
// ONE PRODUCER, and it is narrow: a view opened mid-scan whose run then fails.
// A run that had already failed refuses at init and never opens a view at all.
//
// AND IT RETIRES THE VIEW'S OWN STANDING QUESTION BEFORE IT CLOSES (2026-08-29).
// TWO SURFACES CAN BE UP WHEN THIS FIRES, and the visit's end has to answer for
// both:
//   * THE LOAD CONFIRMATION raised by bare `'` on the viewed member
//     (history_load_in_place). close_history_mode's whole-struct reset clears
//     pending_load_member, so a question left painted would name a subject that
//     no longer exists and its OK would load nothing, silently. The prompt is
//     dropped through the answer's own Cancel body first
//     (GuiPrompt::cancel_load_confirmation). THE HISTORY SUBJECT IS THE TERM
//     and the player's cannot be standing here: the player and this view
//     exclude each other — bare `l` and the Play renders button are outside the
//     mode's allowlist, and route_render_player_key consumes bare `h` — so the
//     only LOAD_IN_PLACE_CONFIRM reachable under a standing visit is this one.
//   * THE COMMIT-TITLE EDITOR, which is left standing deliberately (a modal
//     editor is not a question about a member) and whose Enter then meets
//     run_history_commit's own !active arm, which says "History is unavailable"
//     on a notification card rather than returning in silence.
// The FILE MENU's standing-menu decision is above, and unchanged.
void GuiInputHandler::on_history_prefetch_ready() {
    const GuiHistoryPrefetch::DrainResult r = history_prefetch.drain();
    if (!app.history_mode.active) return;
    if (r.became_done && history_prefetch.run_failed()) {
        // A view closing under the user is an event he was not watching, so
        // the arrival's sentence is a notification card beside its stderr
        // line (2026-08-29), the store's own reason appended on both.
        std::fprintf(stderr, "warptempo_gui: %s: %s\n", kHistoryUnavailable,
                     history_prefetch.scan_failure_reason().c_str());
        notifications.notify(AppState::NotificationClass::Normal,
                             std::string(kHistoryUnavailable) + ": " +
                                 history_prefetch.scan_failure_reason());
        if (app.history_mode.pending_load_member)
            prompt.cancel_load_confirmation();
        finalize_active_drags();
        close_history_mode();
        return;
    }
    if (r.members_appended == 0 && !r.became_done) return;
    measure_history_head_delta();
    viewport.invalidate_all();
}

// SWITCH WHAT THE LANE SHOWS — the ONE owner (architect 2026-08-05 for the two
// compare readings, GENERALIZED 2026-08-07 to the (WALK SOURCE, READING) pair),
// and its callers, re-derived by grep 2026-08-18: TWO ARMS OF
// handle_history_mode_key and nothing else — bare `g`, which steps the WALK in
// row order, and bare `u`, which flips the READING. THE TWO WALK RADIOS AND THE
// CUMULATIVE TOGGLE REACH IT THROUGH THOSE SAME ARMS, dispatching their chords
// on_key like every other roster button, so there is no pointer call site to
// keep in step with a key. (There WAS one from 2026-08-05 to 2026-08-18: row
// 3's two repurposed tabs selected a walk directly from the tab row's band
// claim, with Ctrl+Tab / Ctrl+Shift+Tab as the keyboard's cycle over the same
// owner. The walk's buttons are ordinary chord buttons now.)
//
// SWITCHING SOURCE DOES NOT MOVE THE OTHER WALK'S POSITION — neither field is
// touched here at all, which is what makes the two walks two places a visit can
// come back to (AppState::HistoryMode's `source` owns that rule).
//
// A SWITCH IS A MODE EDGE, with the `,` / `.` step's shape exactly — the same
// acts in the same order, for the same reasons, because the same thing is true
// of it: the lane is about to show a DIFFERENT LIST.
//   * the mode focus AND ITS SELECTION clear, through the one clearer that
//     always takes the pair — both index the painted list, so carrying either
//     would light an unrelated flag; the playhead it landed stays where it is,
//     the step's own rule;
//   * the lane's published content is dropped — the two pointer stashes and the
//     diff-flag list they index describe the reading that is LEAVING — AND
//     REPUBLISHED IN THIS SAME PRESS (2026-08-07, republish_history_lane_now):
//     the arriving reading's list, rects and stems are standing before the press
//     returns, so the swap is atomic and shows no blank frame;
//   * THE TRIM REGION OVERLAY HIDES (2026-08-05, the view-local rule —
//     planner-included on the step's own edge argument, the architect having
//     named the exit and the step): the arriving reading is a different delta
//     from the one the reader was reading, which is the turn-to-other-work
//     every hide answers to. It discards nothing — this view writes no trim;
//   * THE VIEWPORT IS NOT TOUCHED — the step's own rule since 2026-08-08, and
//     it applies here for the step's own reason: the window is the USER'S while
//     the view stands, so every walk and reading shares one viewport and a
//     switch shows
//     the arriving reading through exactly the frame the leaving one was read
//     in, which is what makes the two readings of one member comparable at a
//     glance (architect, SUPERSEDING the 2026-08-05 reset to full zoom out);
//   * full-window damage, a discrete command.
//
// IDEMPOTENT AT THE TOP, which is where the walk radios' rule comes from: a
// switch to the walk already shown changes nothing and damages nothing, so it
// is a consumed nothing without the call site testing for it — and it is what
// makes bare `g`'s step safe to express as a plain "the next one" without a
// live-walk test of its own. (The radio flag on the two buttons kills the press
// one step earlier, at the claim, which is the roster's own shape for a lit
// radio; this is the owner's backstop under it.) The `!active` guard is the same defensive shape
// the two framers carry — the callers are gated by the mode already.
//
// IT WRITES THE PAIR ACROSS TWO HOMES since 2026-08-08, and the split is the
// reading's session scope rather than a second owner: the WALK SOURCE is
// per-visit state on HistoryMode, while the READING is the program-session
// preference AppState::history_cumulative (its contract is at that field). Both
// halves still arrive here and only here — the `g` step passes (the next walk,
// the current reading) and the `u` toggle passes (the current walk, the flipped
// reading).
void GuiInputHandler::set_history_reading(GuiHistoryWalkSource source,
                                          GuiHistoryCompare    compare) {
    if (!app.history_mode.active) return;
    const bool cumulative = (compare == GuiHistoryCompare::Cumulative);
    if (app.history_mode.source == source &&
        app.history_cumulative == cumulative) {
        return;
    }
    app.history_mode.source = source;
    app.history_cumulative  = cumulative;
    clear_history_mode_focus(app.history_mode);
    drop_lane_stash_across_history_edge();
    // (NO OVERLAY HIDE, architect 2026-08-19: a walk-or-reading switch is one of
    // THE MODE'S THREE OWN EDGES, and none of them hides any more — the
    // argument is at close_history_mode, the mode's exit.)
    republish_history_lane_now();
    viewport.invalidate_all();
}

// THE MODE'S OWN KEYBOARD SURFACE — the whole membership, re-derived from the
// arms below (2026-08-05):
//   * bare `h`             — the toggle, the ONE shape bound outside the mode;
//   * bare `u`             — the CUMULATIVE reading's toggle (2026-08-08);
//   * bare `g`             — the WALK's toggle (2026-08-18), the icon row's two
//     radio buttons' chord;
//   * bare `,` / `.`       — the walk;
//   * bare Tab / Shift+Tab / IsoLeftTab — the diff-flag cycle, shift-agnostic on
//     IsoLeftTab exactly as the live cycle is;
//   * Ctrl+Shift+Tab       — the PAIRED MARCH (2026-08-18), the diff-flag cycle
//     composed with the A/B switch: the mode's Tab act, the tab switch, the
//     mode's Tab act again;
//   * bare Home / End      — the ABSOLUTE ends of the song;
//   * bare `c`             — working zoom, centered on the mode's own focus.
// THE MARCH IS THE ONE CTRL SHAPE (2026-08-18). Ctrl+Tab was the walk cycle's
// forward direction from 2026-08-05 and left with the walk selector; the
// allowlist admits it as an ordinary A/B switch now. Returns true when the
// press was consumed.
//
// THE ENTRY GATES ARE POSITIONAL, NOT RE-TESTED, and that is the point: this is
// reached from on_key's main body, BELOW every gate that must refuse an entry,
// so each refusal is the existing gate's and there is no second copy to
// drift. In on_key's own order — the prompt swallow (returns unconditionally),
// the open dropdown (dropdown_key_blocked: every chord but Ctrl+Q is inert while
// a popup is up), loading-or-absent audio (returns), the editor text drag, the
// keyboard-modal editor gate (keyboard_modal_editor_active + modal_editor_key_-
// blocked, and a printable `h` is a PrintableKey, so it is not merely dropped
// but TYPED — the editor's own handler consumes it and returns above this
// point), and the drag-modal gate (any live pointer gesture swallows every key
// but Ctrl+Q). Every other key above inherits the identical list.
//
// ONLY `h` IS BOUND OUTSIDE THE MODE — the toggle arm sits ABOVE the `!active`
// check and every other arm below it, so with the mode down `u`, `g`, `,`, `.`,
// Tab, Ctrl+Shift+Tab, Home/End and `c` fall through to the ordinary
// dispatch and behave exactly as they always have. Bare `u` and bare `g` are
// bound NOWHERE there, so falling through is the unbound key's own consumed
// nothing, and Ctrl+Shift+Tab out there is the LIVE march — the same
// composition over live markers, reached through its own arm in
// handle_tab_switch_keys. A claim costs a shape outside the mode nothing: it is
// a membership test, and the arm below it is not reached with the mode down.
//
// THE SHAPE IS ITS OWN PREDICATE (history_mode_owns_key) because it has a SECOND
// reader: the redesign roster's mode-scoped disabled-face partition
// (history_mode_disables_button, input_pointer.cpp) asks "would this button's
// chord act in the mode", and the answer for the history button's own bare `h`
// is decided HERE — one line above the allowlist — rather than in it. Spelling
// the membership twice is exactly how that face would come to lie about the
// button that opens the view.
// MOST OF THESE SHAPES ARE ALSO BUTTON CHORDS, and the membership is
// RE-DERIVED BY READING kToolbarChords rather than remembered — which is what
// this paragraph got wrong twice, having twice claimed a shape "no roster
// entry carries" while the roster carried it. The current answer, re-greped:
//   * BARE `g` — the icon row's TWO WALK RADIOS (2026-08-18), which share the
//     one chord as a radio pair; this claim is what answers their face LIVE
//     inside the view, their resting grey outside it being their own arm's;
//   * BARE `,` / BARE `.` — the walk's own two buttons (2026-08-05), the
//     roster's first RESTING-DISABLED entries, and resting-disabled again
//     since 2026-08-18: the icon row hides nothing, so what keeps them from
//     dispatching outside the view is their ENABLED BIT
//     (redesign_button_enabled reads `history_mode.active` for them and states
//     the whole succession, including the plain `true` they answered from
//     2026-08-15 while the bottom row's cluster swap left them unpainted out
//     there);
//   * BARE `u` — the Cumulative toggle's own button (2026-08-08), which joined
//     that family on exactly the same terms and shares that arm;
//   * BARE Home / BARE End — the bottom row's two SKIP buttons since
//     2026-08-11, and this paragraph claimed the opposite until 2026-08-15;
//   * BARE `c` — the icon row's zoom-original button since the 2026-08-12
//     relayout, likewise;
//   * BARE Tab, SHIFT+TAB and CTRL+SHIFT+TAB — the bottom row's whole
//     MARKER-WALK GROUP since 2026-08-15, all three answered LIVE in the view by
//     this predicate: the first two step the mode's own diff-flag cycle, and the
//     third (2026-08-18) marches the pair over that same cycle, so none of the
//     three greys in the view.
// Which leaves BARE `h` — the history button's own chord, and the one shape
// here bound outside the mode at all.
bool history_mode_owns_key(GuiKey key, GuiInputState mods) {
    if (mods.alt) return false;
    // THE FIRST CTRL SHAPE (architect 2026-08-18): CTRL+SHIFT+TAB, THE PAIRED
    // MARCH, read here over the mode's own vocabulary. "Both tab and ctrl+tab
    // are available [in the view], and ctrl+shift+tab is just short for
    // 'tab, ctrl+tab, tab'" — so the chord means one thing everywhere and only
    // what "Tab" DENOTES changes with the context: live markers outside, the
    // viewed checkpoint's diff flags in here. Claimed rather than left to the
    // allowlist because that is how the marker-walk group's other two members
    // (bare Tab and Shift+Tab) already answer, both for the key and for the
    // roster face derived from this predicate.
    //
    // THE SPELLING IS THE LIVE MARCH'S EXACTLY — plain Tab with ctrl+shift, no
    // IsoLeftTab arm — because the live arm binds that one shape too; the mode
    // mirrors what it composes rather than widening it.
    if (mods.ctrl && mods.shift && key == GuiKeys::Tab) return true;
    // THE OTHER CTRL SHAPE (architect 2026-08-24): CTRL+HOME / CTRL+END, the
    // WHOLE-PIECE jump. It is claimed here so the chord means the SAME thing in
    // every state — in this view the bare pair already jumps to the piece's own
    // ends, so the ctrl form lands on exactly the same frames and the arm below
    // needs no shape of its own. Claimed rather than left to the allowlist
    // because that is how the bare pair already answers, both for the key and
    // for the roster face derived from this predicate.
    if (mods.ctrl && !mods.shift &&
        (key == GuiKeys::Home || key == GuiKeys::End)) return true;
    // EVERY OTHER CTRL SHAPE IS REFUSED, and the branch stays as a REFUSAL
    // rather than being deleted: every shape below it is BARE (or
    // shift-carrying, on Tab and the walk), so a ctrl press falling past this
    // line would reach the bare list and be read as its unmodified twin — a
    // Ctrl+H, which binds nothing anywhere since the revert act left it on
    // 2026-09-01 and must therefore be as silent as any unbound chord, would
    // come out as the mode's own `h` toggle and close the view. One line, and
    // strict modifier validation holds.
    //
    // (WHAT IT ALSO CLAIMED: from 2026-08-05 Ctrl+Tab stepped the WALK SOURCE
    // one tab right and, from 2026-08-07, Ctrl+Shift+Tab one tab left — both in
    // the plain Tab spelling and in the IsoLeftTab one xkb puts on the Tab key's
    // shift level. The walk is bare `g` below now, so Ctrl+Tab falls through to
    // the allowlist, which ADMITS it — an A/B tab switch works normally in the
    // view, the architect's own ruling — while Ctrl+Shift+Tab is claimed above
    // for the march itself rather than for a walk cycle.)
    if (mods.ctrl) return false;
    // THE CYCLE IS ONE OF TWO SHIFT-CARRYING SHAPES, and it is admitted in the
    // live cycle's own three spellings: bare Tab forward, Shift+Tab back, and
    // IsoLeftTab back shift-agnostically (the compositor delivers that keysym
    // for Shift+Tab on most layouts, and the live arm accepts it either way).
    if (key == GuiKeys::Tab || key == GuiKeys::IsoLeftTab) return true;
    // THE WALK IS THE OTHER (2026-08-07): bare `,` / `.` STEP and shift-exact
    // `,` / `.` JUMP TO THE WALLS — oldest and newest. Both shapes are the
    // walk's own vocabulary, so both are admitted here and the arm below reads
    // the bit; every other combination stays the consumed no-op strict modifier
    // validation makes it (ctrl and alt are already refused above).
    if (key == GuiKeys::Comma || key == GuiKeys::Period) return true;
    if (mods.shift) return false;
    // BARE `u` IS THE READING'S TOGGLE (2026-08-08) and BARE `g` THE WALK'S
    // (2026-08-18) — BARE ONLY, like `h` and the three below them: a modified
    // `u` or `g` binds nothing anywhere in the product, so strict modifier
    // validation leaves each the consumed nothing it already was and these
    // claims add no shape.
    return key == GuiKeys::H || key == GuiKeys::U || key == GuiKeys::G ||
           key == GuiKeys::Home || key == GuiKeys::End || key == GuiKeys::C;
}

// THE DIFF-FLAG CYCLE'S ONE ACT — the mode's Tab, factored out because the
// mode's Ctrl+Shift+Tab march composes it twice (2026-08-18). Two callers, both
// in handle_history_mode_key below: its Tab arm and its march arm. Every rule
// the cycle carries — the no-wrap walls, the empty-list nothing, the
// focus-replaces-selection rest, the landing and its damage — is stated at the
// arm that spells it, and this is the one body that runs them.
// EVERY REFUSAL HERE IS SILENT (architect 2026-08-31, retiring the 2026-08-30
// cards "There are no changed markers to step through" / "This is the last
// change" / "This is the first change"): a benign one-dimensional refusal
// already at its state says nothing — the diff lane is on screen, and one
// glance at it shows both an empty list and a focus resting at its end. The
// walls and the empty arm are the consumed no-ops they were; only the
// sentences left. (Which is also why the march's two compositions no longer
// need the stack's duplicate-text rule to answer once.)
void GuiInputHandler::cycle_history_diff_flag_focus(bool forward) {
    const int n = static_cast<int>(app.history_mode.flags.size());
    if (n == 0) return;
    const int here = app.history_mode.focus;
    int there = -1;
    if (here < 0 || here >= n) {
        // THE SEED IS THE PLAYHEAD ANCHOR (architect 2026-08-22), not the list's
        // first index: with no focus standing, a Tab lands the nearest flag
        // STRICTLY PAST the playhead and a Shift+Tab the nearest strictly before
        // it. That is the live cycle's own rule verbatim ("the playhead frame is
        // the sole cycle anchor", stated at Selection::cycle_selection), and the
        // index seed it replaces was this body's one deviation from the mirror
        // its landing comment below claims — it teleported a reader who had just
        // opened the view to the piece's first delta instead of continuing from
        // where the playhead sits.
        //
        // THE COMPARE IS IN THE ACTIVE DISPLAY DOMAIN, both sides: the flags
        // carry SOURCE frames (they land through land_playhead_on_source_frame),
        // and playhead_cursor_sample is a domain frame, so each candidate
        // forward-translates exactly as the live cycle's frame_of does. The mode
        // switches no audio view, so it stands in whichever of source (identity)
        // or target (the live map) the tab was in, and this handles both.
        //
        // FIRST/LAST HIT IS THE NEAREST HIT: rebuild_history_diff_flags leaves
        // the list sorted ASCENDING BY time_frame and the source->domain
        // translation is monotone, so the scan needs no minimum-search. Where
        // several flags share one frame (a changed/removed/added coincidence,
        // which the stable sort keeps grouped) the group's first member forward
        // and its last backward is the stop, and the index step below then walks
        // the rest of the group — every flag stays Tab-reachable.
        //
        // STRICT INEQUALITY IS THE LIVE FAMILY'S OWN, and it means a playhead
        // parked exactly on an unfocused flag steps PAST it rather than
        // re-landing where it already stands.
        const int64_t ph_f = app.playhead_cursor_sample;
        auto frame_of = [&](int i) -> int64_t {
            return source_frame_to_active_domain(
                app, audio,
                app.history_mode.flags[static_cast<std::size_t>(i)].time_frame);
        };
        if (forward) {
            for (int i = 0; i < n; ++i) {
                if (frame_of(i) > ph_f) { there = i; break; }
            }
        } else {
            for (int i = n - 1; i >= 0; --i) {
                if (frame_of(i) < ph_f) { there = i; break; }
            }
        }
        // NO CANDIDATE IS THE CONSUMED NO-OP the whole family already is — the
        // live cycle's "nothing ahead" return and the no-wrap walls two lines
        // below in one shape. Forward with the playhead at or past the last
        // flag, backward at or before the first, and the view rests untouched.
        // It is silent for the SAME reason the seated walls below are,
        // because it is the same wall reached from an unseated cursor:
        // nothing ahead of the playhead is nothing ahead.
        if (there < 0) return;
    } else if (forward) {
        if (here + 1 >= n) return;   // last already
        there = here + 1;
    } else {
        if (here == 0) return;       // first already
        there = here - 1;
    }
    playback_lifecycle.stop_playback_if_playing();
    // THE CYCLE REPLACES THE SELECTION WITH ITS STOP, the live cycle's own
    // shape: the set clears and the focus alone stands, which is the plain
    // click's rest too. Ordered so the clearer cannot undo the focus it
    // writes.
    clear_history_mode_focus(app.history_mode);
    app.history_mode.focus = there;
    // THE LAND HIDES THE TRIM REGION OVERLAY (2026-08-19 — it is one of the
    // rule's two movement owners; the rule is at clear_region_highlight,
    // input_handler.h), so the walk's own hide is deleted with the inventory
    // it belonged to. Every branch that reaches here lands, so nothing is lost.
    land_playhead_on_source_frame(
        app, audio, viewport,
        app.history_mode.flags[static_cast<std::size_t>(there)].time_frame);
    // THE LIVE FAMILY'S LANDING, MIRRORED: the live cycle recenters on its
    // stop AT THE CURRENT ZOOM, follow mode not gating it, and this does the
    // same over the diff-flag list, reading the flag it just landed on. The
    // working-zoom snap both families carried for one commit is REVERTED
    // (architect 2026-08-05, "no zoom on Tab", same day it landed) — a walk
    // must not re-frame the view under the reader, in here least of all,
    // where the whole delta is what is being read.
    viewport.center_viewport_on_playhead();
    // A DISCRETE COMMAND and the focus ALWAYS moved to get here (every branch
    // above either returned or picked a different index), so the full-window
    // damage the mode's focus click emits on a move is unconditional.
    viewport.invalidate_all();
}

bool GuiInputHandler::handle_history_mode_key(GuiKey key, GuiInputState mods) {
    if (!history_mode_owns_key(key, mods)) return false;

    if (key == GuiKeys::H) {
        if (app.history_mode.active) {
            close_history_mode();
            return true;
        }
        // NOT WHILE A CHECKPOINT IS PUBLISHING (2026-08-07). The walk, the now
        // side and the head delta are all measured against the repository at
        // init(), and the worker is mid-mutation on that repository: a view
        // opened now would show a commit list the act is about to add to, or
        // worse, catch it half-written. One consumed no-op and one line, the
        // mode's own unavailable shape — the wait is seconds and the bit falls
        // by itself. The CLOSE above is deliberately not gated: leaving a view
        // is always allowed.
        //
        // AND IT SAYS SO ON A CARD (architect 2026-08-30): the refusal was
        // the last of the ruled silences and left `h` reading as a dead key
        // for the seconds the worker takes. ONE COMPOSER FEEDS BOTH — the
        // stderr line stays, and the words are the picker's own
        // (kCheckpointPublishing, the head of this file), the same fact met
        // one act over.
        if (app.history_checkpoint_in_flight) {
            std::fprintf(stderr, "warptempo_gui: history: %s\n",
                         kCheckpointPublishing);
            notifications.notify(AppState::NotificationClass::Normal,
                                 kCheckpointPublishing);
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

    // BARE `g` — THE WALK'S TOGGLE (architect 2026-08-18), the keyboard half of
    // the icon row's two WALK RADIOS: it steps the WALK SOURCE in row order
    // with wrap — Git (the committed checkpoints), Session (this session's own
    // undo/redo timeline) — through the ONE switch owner, so the key and the
    // two buttons cannot diverge, and it passes the CURRENT reading through
    // untouched (`u` is the only thing that moves that bit).
    //
    // ONE CHORD FOR A PAIR IS THE ROSTER'S RADIO SHAPE, not a new one: with two
    // walks a toggle IS the direct select, exactly as bare `t` and bare `p`
    // serve the S/T and W/P pairs. The `radio` flag on both rows is what makes
    // a press on the lit half a consumed nothing instead of a switch away from
    // what the user just clicked.
    //
    // NOT REPEAT-ELIGIBLE (repeat_eligible below, which lists the bare shapes
    // that DO repeat and leaves this one out, exactly as it leaves `u` out): a
    // held toggle over two walks can only flap.
    //
    // (THE CHORD WAS CTRL+TAB FROM 2026-08-05 TO 2026-08-18, with
    // Ctrl+Shift+Tab as its reverse from 2026-08-07, because row 3's tabs were
    // the walk's surface and their own chord had to become the cycle. The walk
    // has buttons of its own now and Ctrl+Tab switches A/B tabs in here like
    // everywhere else.)
    //
    // THE ROW ORDER IS SPELLED ONCE, as a table read by this step and by
    // nothing else, and the arithmetic is left general so a third walk would
    // need no new shape — the two BUTTONS name their own walk at the face
    // (redesign_button_selected), which is what a radio pair does.
    if (key == GuiKeys::G) {
        static constexpr GuiHistoryWalkSource kRow[] = {
            GuiHistoryWalkSource::Commit,
            GuiHistoryWalkSource::Local,
        };
        constexpr std::size_t kCount = std::size(kRow);
        std::size_t here = 0;
        for (std::size_t i = 0; i < kCount; ++i) {
            if (kRow[i] == app.history_mode.source) {
                here = i;
                break;
            }
        }
        set_history_reading(kRow[(here + 1) % kCount], app.history_compare());
        return true;
    }

    // BARE `u` — THE CUMULATIVE READING'S TOGGLE (architect 2026-08-08), the
    // axis row 3 carried as two more tabs for one day. It flips the session's
    // own bit through the same switch owner the walk toggle above uses, so a
    // reading change is
    // the SAME MODE EDGE a walk change is — focus and selection cleared, lane
    // stash dropped, region hideed, lane republished synchronously, window
    // damaged — and the two can never come to do different amounts of work.
    //
    // ONE-SHOT, NOT REPEAT-ELIGIBLE (repeat_eligible below, which lists the bare
    // shapes that DO repeat and leaves this one out): a held toggle can only
    // flap, exactly as the ctrl cycle can.
    //
    // THE BIT IS THE SESSION'S, NOT THE VIEW'S: it lives on AppState so the mode
    // edges cannot reset it (its contract is at AppState::history_cumulative), so
    // this toggle is remembered until the program closes. Its BUTTON is row 4's
    // HistoryCumulative, which dispatches this same bare chord.
    if (key == GuiKeys::U) {
        set_history_reading(app.history_mode.source,
                            app.history_cumulative
                                ? GuiHistoryCompare::Iterative
                                : GuiHistoryCompare::Cumulative);
        return true;
    }

    // `,` steps OLDER (further back in the walk, index+1), `.` steps NEWER
    // (index-1). Each CLAMPS at its wall as a consumed no-op — the walk has
    // ends, and running off one must not wrap or refuse loudly.
    // SHIFT JUMPS TO THE WALL INSTEAD OF STEPPING TOWARD IT (architect
    // 2026-08-07): Shift+`,` goes to the OLDEST member and Shift+`.` to the
    // NEWEST, the same key naming the same direction. A jump is the STEP'S OWN
    // ACT — the same mode edge below, in the same order — so the two shapes
    // differ in one expression and in nothing else; and standing at the wall it
    // names, it is the step's own consumed nothing.
    // ONLY THE BARE SHAPES REPEAT (repeat_eligible, below): a held absolute jump
    // could only flap against the wall it is already on.
    // THE ICON ROW'S TWO ARROW BUTTONS ARE THESE KEYS (2026-08-05,
    // RedesignButton::HistoryOlder / HistoryNewer): they dispatch the
    // bare chords through on_key like every other button, so this is the one
    // body and a click at a wall is the same consumed nothing a key press is —
    // behind a DEAD FACE since 2026-08-30, the buttons greying at the wall
    // they stand on through the same two predicates (the truthful-buttons
    // ruling; redesign_button_enabled's companions arm) — and since
    // 2026-08-07 they are SHIFT-ADMITTING (redesign_button_shift_
    // admits, app_state.h), so a shift-click reaches the jump through that same
    // one route and their tooltips carry the shift line that names it; a
    // greyed arrow loses no jump, the jump onto the wall it stands on moving
    // nothing either.
    //
    // THE ACTIVE WALK'S POSITION, never a named one (2026-08-07): the step reads
    // walk_count / walk_index and writes through set_walk_index, so the same body
    // walks the committed history or the session's own undo/redo timeline
    // depending on which tab is lit, and each walk keeps the position
    // the other one left alone. On the Local tab `.` walks INTO FUTURE STATES
    // when redo entries exist, the walk opening at the live member rather than at
    // its newest one (GuiHistoryLocalWalk owns that model). A ONE-MEMBER walk —
    // a session that has authored nothing — clamps at both walls and every press
    // is the same consumed nothing a wall is, as does the commit side's empty
    // window.
    if (key == GuiKeys::Comma || key == GuiKeys::Period) {
        const bool older = (key == GuiKeys::Comma);
        // THE WALL IS ONE PREDICATE PER DIRECTION FOR BOTH SHAPES
        // (history_walk_older_actionable / history_walk_newer_actionable,
        // app_state.h — 2026-08-30, when the Older / Newer buttons' face began
        // reading the same walls under the truthful-buttons ruling): a step
        // that would run off the end and a jump made while already standing
        // at that end are the same consumed no-op, with no edge and nothing
        // moved, and the EMPTY walk — one address (0), stood at — answers
        // false in both directions with no case of its own. The walk has ends,
        // and reaching one must not wrap — it moves nothing. (Until 2026-08-30
        // this arm computed `there` and compared it to `here`; the predicate
        // is that compare, named once for the act and the face.)
        //
        // AND THE WALL IS SILENT (architect 2026-08-31, retiring the
        // 2026-08-30 pair "This is the oldest checkpoint" / "This is the
        // newest state"): a benign one-dimensional refusal already at its
        // state says nothing — the walk's own position is on screen and the
        // Older / Newer buttons grey on these very predicates, so the grey is
        // the cue and the unmoved walk is the answer.
        if (!(older ? history_walk_older_actionable(app.history_mode)
                    : history_walk_newer_actionable(app.history_mode))) {
            return true;
        }
        const std::size_t count  = app.history_mode.walk_count();
        const std::size_t here   = app.history_mode.walk_index();
        const std::size_t oldest = count - 1;   // count > 0 past the wall
        const std::size_t there =
            older ? (mods.shift ? oldest : here + 1)
                  : (mods.shift ? 0 : here - 1);
        app.history_mode.set_walk_index(there);
        // THE MODE FOCUS AND ITS SELECTION CLEAR ON EVERY STEP, through the one
        // clearer that always takes them together: both index into the list the
        // step is about to replace, so carrying either would light an unrelated
        // flag — and the playhead it landed stays where it is, which is the
        // navigation the click was.
        clear_history_mode_focus(app.history_mode);
        // The lane's published content — the diff-flag list and the two pointer
        // stashes over it — describes the commit that is LEAVING. Same domain on
        // both sides, so no index can be misread, but the FRAMES behind them are
        // the old commit's: dropping all three is what stops a lane press or a
        // Tab step landing the playhead on a flag that is no longer shown. The
        // refill is this press's own (republish_history_lane_now, at the tail),
        // so the emptied state lives only across the handful of lines between.
        drop_lane_stash_across_history_edge();
        // (NO OVERLAY HIDE, architect 2026-08-19: a `,` / `.` step is one of THE
        // MODE'S THREE OWN EDGES and none of them hides any more — a step
        // changes which checkpoint is being read and touches neither playhead
        // nor selection nor trim, so it has no business putting away a view
        // preference the mode itself offers no way to restore. The argument is
        // at close_history_mode, the mode's exit.)
        // THE VIEWPORT IS THE USER'S ACROSS A STEP (architect 2026-08-08,
        // SUPERSEDING the 2026-08-05 per-edge reset to full zoom out): he pans
        // and zooms once and reads the SAME WINDOW through every step of the
        // walk, so a checkpoint's flags are compared against the previous one's
        // at the magnification he chose rather than at an overview he has to
        // re-establish after each press. NO EDGE FRAMES AT ALL since
        // 2026-08-18, the entry's own framing having gone with the view's
        // navigation-state ownership: the trim bar's double-click is the view's
        // one framing gesture, and it frames the TRIM WINDOW in here exactly as
        // it does outside. So this edge writes NO viewport at all.
        //
        // THE ARRIVING COMMIT'S LANE, PUBLISHED IN THIS PRESS (2026-08-07, the
        // architect's reported flicker): the drop above emptied the lane and
        // nothing else in this press republishes it — the step moves no
        // viewport, so no kick rides along — and without this the step showed a
        // blank lane until the next tick.
        republish_history_lane_now();
        viewport.invalidate_all();
        return true;
    }

    // THE ARMS BELOW ARE THE MODE'S OWN RE-EXPRESSIONS of three live commands
    // (architect 2026-08-05). Each is the live arm's gesture read against the
    // mode's own data — the diff-flag list and the mode's own focus — never the
    // live-marker machinery, which navigates by markers the lane is not showing.
    // THEY KEEP THE LIVE ARMS' PLAYBACK AND REGION REGIMES: a keyboard command
    // that commits a new cursor position stops a live audition and HIDES the
    // trim region overlay (the keyboard stop rule at stop_playback_if_playing,
    // whose cursor-moving navigation class names Home/End and the Tab family;
    // the hide follows from the movement owners these arms write through, the
    // rule at clear_region_highlight). That is where they part from the mode's
    // diff-flag CLICK on the STOP alone — the click's own land hides just as
    // these do, and it is the stop it deliberately omits.
    // THE STOP HALF HAS NO REACHABLE PRODUCER IN HERE, and is kept anyway
    // (recorded at the arms 2026-08-06, where the docs had carried it alone):
    // the entry owner stops any session running before `h` and nothing in the
    // view can start one — bare Space is consumed and no scrub act exists in
    // here — so
    // these calls are formalities. They stay because the REGIME is what these
    // arms re-express: a mode-local command that commits a cursor position looks
    // exactly like its live twin, and a future route that could audition in here
    // would inherit the right shape instead of a missing call. The revert act's
    // own stop carries the same note at its site.

    // THE TAB FAMILY IN THE VIEW — the diff-flag cycle, and above it the march
    // that composes the cycle with the A/B switch.
    //
    // BARE TAB / SHIFT+TAB / IsoLeftTab — THE DIFF-FLAG CYCLE, mode-local. Tab
    // steps to the next flag, Shift+Tab and IsoLeftTab to the previous, in the
    // list's own order: rebuild_history_diff_flags leaves `flags` sorted
    // ASCENDING BY time_frame, so list order IS reading order and no second
    // ordering is derived here (the hit stash indexes this same list).
    // NO WRAP, mirroring the live cycle: it lands on the nearest stop in the
    // walk direction and does nothing at all with none ahead, so a Tab on the
    // last flag is a consumed no-op here too. With NO focus standing the
    // PLAYHEAD is the anchor (architect 2026-08-22): Tab takes the nearest flag
    // strictly past it and Shift+Tab the nearest strictly before it, the live
    // cycle's own seed, and a playhead with nothing ahead in the walk direction
    // lands nothing. An empty list is a consumed no-op, in all three of its
    // shapes: an empty delta, an active column whose half of the delta is empty,
    // and the DROPPED-AND-NOT-YET-REBUILT list, whose window every mode edge
    // closed on 2026-08-07 by republishing inside its own press — the rebuild's
    // own refusals (loading or absent audio, no plate yet) still leave it open, so
    // the cold answer stays this arm's, and it is what a step and a Tab arriving in
    // one key batch would have cycled the leaving commit's flags with
    // (drop_lane_stash_across_history_edge owns the whole argument).
    //
    // THE ACT IS ITS OWN MEMBER (cycle_history_diff_flag_focus) because the
    // march below composes it twice; this arm is its spelling, not its body.
    //
    // CTRL+SHIFT+TAB RANKS ABOVE IT and must: the test below reads the KEY
    // alone, so the march's own Tab would fall into a backward step here.
    if (mods.ctrl && mods.shift && key == GuiKeys::Tab) {
        // THE PAIRED MARCH, MODE-LOCAL (architect 2026-08-18): "ctrl+shift+tab
        // is just short for 'tab, ctrl+tab, tab'", so it is built as exactly
        // that composition over the view's own vocabulary — the mode's Tab act,
        // the A/B switch, the mode's Tab act again — the same three lines the
        // live march is over live markers.
        //
        // WHAT IT LEAVES BEHIND is the march's own shape: the mode's focus is
        // ONE index over one diff-flag list (the two tabs share both marker
        // stores, so the list is the same on either side), and each step lands
        // THE THEN-ACTIVE TAB's playhead and recenters THAT tab's viewport at
        // its own zoom. So the leaving tab is parked on one flag and the
        // arriving tab on the next, each in its own window — which is what
        // makes a march a march.
        //
        // THE SWITCH IS THE ALLOWLIST'S OWN Ctrl+Tab, spelled here rather than
        // dispatched: the same active_views call and the same target-render
        // trigger the live march ends on, so the two compositions differ in the
        // cycle they name and in nothing else.
        cycle_history_diff_flag_focus(true);
        active_views.switch_active_tab_view_to(app.active_tab_view == 'A' ? 'B' : 'A');
        cycle_history_diff_flag_focus(true);
        target_render.trigger();
        return true;
    }
    if (key == GuiKeys::Tab || key == GuiKeys::IsoLeftTab) {
        cycle_history_diff_flag_focus(key == GuiKeys::Tab && !mods.shift);
        return true;
    }

    // HOME / END — THE ABSOLUTE ENDS OF THE SONG, deliberately NOT the trim
    // bounds the live BARE arms jump to (architect 2026-08-05). The view reviews
    // the WHOLE piece: a checkpoint's delta is laid out across every authored
    // frame, trimmed window or not, so an End that stopped at a trim bound would
    // hide the flags past it. With a full trim window the two answers coincide
    // (trim_window_is_full), so the difference shows only under a set trim.
    // BOTH SHAPES LAND HERE, bare and CTRL (history_mode_owns_key claims the
    // ctrl pair since 2026-08-24): outside the view ctrl is what asks for the
    // piece's ends, and in here that is what a jump already means, so the chord
    // means one thing in every state and this arm needs no fork.
    if (key == GuiKeys::Home || key == GuiKeys::End) {
        // A JUMP THAT WOULD CHANGE NOTHING IS SILENT here too (architect
        // 2026-08-31, the live body's rule): a benign one-dimensional
        // refusal already at its state says nothing, the playhead's own
        // position being the tell. The actionability owner's mode arm reads
        // the diff-flag focus this body clears and the piece's-ends landing
        // its mode bit already selects, so the refusal is exactly "no write
        // below would move anything".
        if (!playhead_end_jump_actionable(app, audio, key == GuiKeys::End,
                                          /*whole_piece=*/false)) {
            return true;
        }
        playback_lifecycle.stop_playback_if_playing();
        // THE MODE ANALOG OF THE LIVE ARMS' SELECTION CLEAR: the playhead is
        // leaving the focused flag for a spot nothing marks, so the focus — and
        // the selection with it, through the one clearer — goes with it;
        // otherwise the flag would keep claiming to be the playhead at its own
        // position. Full-window damage for the face swap, as the mode's click
        // emits on a focus move.
        if (clear_history_mode_focus(app.history_mode)) {
            viewport.invalidate_all();
        }
        // The ACTIVE domain's own ends: live_total_frames is what the displayed
        // timeline runs to in either view, and it is the same total the
        // full-window trim range resolves to. THE ARITHMETIC LIVES AT THE
        // SHARED OWNER since 2026-08-15 (playhead_skip_landing_frame,
        // viewport.cpp), whose whole-piece arm this mode bit already selects —
        // which is why `whole_piece` is passed FALSE here and nothing is
        // duplicated: one spelling for every jump in the product. The bottom
        // row's two SKIP buttons grey in this view through
        // playhead_end_jump_actionable, whose `h` arm reads this same
        // landing and the mode's diff-flag clear (the succession is at their
        // case in redesign_button_enabled). The owner's clamp is idempotent on
        // a value move_playhead_to would clamp anyway, so this jump is
        // byte-identical to the hand-spelled one.
        viewport.move_playhead_to(
            playhead_skip_landing_frame(app, audio, key == GuiKeys::End,
                                        /*whole_piece=*/false));
        return true;
    }

    // BARE `c` — the live arm's recipe with the MODE's focus in place of the
    // live marker's, and the BODY IS NOT HERE: run_center_command owns both
    // recipes and picks between them on the mode bit (2026-08-05, when `0`'s
    // second arm became a third caller of the same command). This arm is the
    // CLAIM alone — `c` is the mode's own vocabulary, so it must return true
    // here rather than fall to the allowlist, which does not admit it.
    if (key == GuiKeys::C) {
        run_center_command();
        return true;
    }

    return false;
}

// THE MODE'S KEYBOARD ALLOWLIST — the shape read_only_key_blocked has, and for
// the same reason: one gate with a stated membership beats twenty scattered
// refusals. True when the press is NOT admitted and should be dropped as a
// consumed no-op.
//
// WHAT IS ADMITTED, the whole list. SPACE IS NOT ON IT (architect 2026-08-05):
// PLAYBACK IS REMOVED FROM THE VIEW WHOLE — it was slow, the mode's full-song
// trim forcing a full target preview render, and buggy besides — so bare Space
// is a consumed no-op in here, the lower half's pointer press is the mode's
// own navigation press rather than a scrub, and the one entry owner stops a session that was already running
// (open_history_mode_fresh), since a view that consumes Space could not otherwise
// stop one.
//   - = / - (bare)          → zoom in / out
//   - 0 (bare)              → the overview: full zoom out, or, once already
//                             there, THE MODE'S OWN `c` (run_center_command
//                             forks on the mode bit, so the second arm reads the
//                             diff-flag focus like every other mode-local
//                             re-expression — 2026-08-05). That arm is not a
//                             pure viewport move: it is `c`, region hide, stop
//                             and land included, admitted on exactly the reason
//                             `c` itself is claimed one line above the gate.
//   - PageUp/PageDown       → the paged viewport scroll
//     (bare)                  — the three above are NAVIGATION, which is the
//                             mode's whole vocabulary: the delta is laid out on
//                             the viewport, so panning and zooming it is reading
//                             it. Two of them are PURE viewport moves; `0`'s
//                             second arm reaches the mode's own `c` and lands
//                             the playhead, which the mode's diff-flag click and
//                             Tab cycle already do.
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
//                             own invariants. NONE OF THEM WRITES A STORE OR
//                             PUSHES AN UNDO ENTRY since 2026-08-07: the S->T
//                             entry's iteration wipe — the one that did, and
//                             the one producer both this gate and the LOCAL
//                             walk's frozen-stack premise had to reason about
//                             — is deleted with the ruling that iteration mode
//                             is target-legal (the record is at
//                             switch_active_audio_view_to,
//                             input_handler.cpp). `i` is not on this allowlist,
//                             so the bit cannot toggle in here either, and the
//                             view now has NO undo-stack producer at all.
//                             `p` clears the live
//                             selection and runs the coincidence auto-select;
//                             the mode neither reads nor paints that selection
//                             (the lane suppresses every live flag while it
//                             stands, and the mode's focus is its own field), so
//                             those land unseen and leave exactly the state the
//                             same press leaves outside the mode. The playhead
//                             moves `t` can make (its selection-gated re-land)
//                             are navigation, which is already this mode's
//                             vocabulary — its own diff-flag click lands the
//                             playhead too. The region hide is a visibility
//                             bit that discards nothing, the
//                             playback stop is running state, and the flag-
//                             editor teardown is unreachable here (no editor can
//                             be open while this gate is reached at all — see
//                             the modal note below). The S->T tail's target
//                             PREVIEW render is derived data, not authoring
//                             state, and the mode already tolerates one: a
//                             render live from before `h` runs on, and the
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
//                             through the chrome press's release half
//                             (finish_chrome_press_release), like every other
//                             redesigned button.
//   - ' (bare)              → THE LOAD CONFIRMATION on the VIEWED walk member,
//                             and the mode's one admitted
//                             MUTATOR (2026-08-04). It is admitted because in
//                             the mode it loads something else in place: the
//                             key names the member the walk stands on and its
//                             OK loads THAT MEMBER's state in place, which is
//                             the mode's own
//                             act rather than an authoring chord that would
//                             leave the frozen now side describing a state that
//                             no longer exists — and the load-in-place closes
//                             the mode
//                             as part of itself, so the mode never outlives the
//                             state it was measured against. The icon row's
//                             load button reaches it here, like every other
//                             redesigned button, by synthesizing this same bare
//                             chord.
//                             IT IS ADMITTED ON BOTH WALKS since 2026-08-08
//                             (architect, superseding the 2026-08-07 ruling that
//                             the Local walk consumes it): it opens the HISTORY
//                             PICKER over the viewed walk's members, and the
//                             open act on the REMOTE tab loads the commit's
//                             three sidecars (load_history_commit_in_place),
//                             on the LOCAL tab that state of the session's own
//                             timeline as a new undo entry
//                             (load_history_local_entry_in_place). It is a
//                             FOURTH session-conditional admission, on THE ACTIVE
//                             WALK CARRYING A MEMBER — one term for both walks
//                             since 2026-08-09, when the empty Remote walk became
//                             a legal standing state (the term at the predicate
//                             says why).
//   - Ctrl+S                → THE SAVE-AND-COMMIT ACT, the mode's second
//                             admitted mutator, and in here it is the ONLY
//                             meaning this chord has (architect 2026-08-08,
//                             moving the act off Ctrl+Alt+R, which is a
//                             consumed nothing in here now, its shifted
//                             twin with it). The act is
//                             save-FIRST by definition — it runs the ordinary
//                             Ctrl+S through its own owner and only then writes,
//                             commits and pushes — so the Save button is where
//                             it belongs, and the mode bit selects the command
//                             inside that button's own chord exactly as the
//                             iteration bit selects the sweep. The dispatch is
//                             the `s` arm in on_key (input_handler.cpp), which
//                             opens the COMMIT-TITLE EDITOR; nothing is
//                             dispatched from here. The Save button reaches it
//                             by synthesizing this same chord and wears the
//                             commit icon and the tooltip "Save and commit" while
//                             the mode stands.
//                             THE PLAIN DISK SAVE HAS NO HOTKEY IN THE VIEW,
//                             and that is the ruling rather than a gap: a
//                             settings-only drift — the one thing the act's own
//                             head-delta grey calls "nothing to checkpoint" — is
//                             saved by leaving the view first. (Ctrl+S inside
//                             the commit-title editor is still the plain save,
//                             through the ONE route_modal_editor_key contract
//                             — one contract whatever the editor count stands
//                             at — which this
//                             gate never sees: the keyboard-modal gate sits
//                             above it.)
//                             IT WAS THE FIRST OF THREE ADMISSIONS CONDITIONAL
//                             ON THE SESSION AND IS UNCONDITIONAL SINCE
//                             2026-09-01 (architect: a gate's membership is the
//                             chord's alone — the two terms were what made the
//                             gate answer this very chord with "not available
//                             in the history view"). They were the head delta
//                             (2026-08-05) and the in-flight bit (2026-08-07),
//                             both inherited from the chord this act moved off,
//                             and they live at the ACT now
//                             (open_history_commit_editor: the publishing card,
//                             then silence for an empty delta) with the SAVE
//                             button's grey reading the same two terms through
//                             history_checkpoint_actionable (app_state.h)
//                             instead of through this line. With the HEAD DELTA
//                             EMPTY — the newest checkpoint already carrying
//                             this session's authoring content — there is
//                             nothing to do, and that is what the face says.
//                             The delta bit
//                             is measured once and cannot change while the mode
//                             stands (AppState::HistoryMode::head_delta_empty
//                             owns it, the asymmetry included: "no changes" is
//                             the delta's vocabulary, the two marker columns
//                             plus `scale`, so a settings-only drift greys the
//                             act too). A PUSH-PENDING bit sat beside it for
//                             one day of 2026-08-09, admitting the chord as an
//                             in-app retry for a checkpoint that committed and
//                             failed to push; it went with the graded machinery
//                             — that fix is the terminal's now. The in-flight
//                             term is one checkpoint at a time; it is nearly
//                             unreachable in here (the act closes the view and
//                             `h` will not reopen one over a publishing
//                             repository), which is why the act answers it with
//                             the sentence three other sites already say rather
//                             than one of its own, while the GLOBAL save lockout
//                             that DOES show is GuiSaveOps::save's own term,
//                             mirrored by the "Committing..." face.
//   - Bare `v`              → THE REVERT ACT, the mode's THIRD admitted mutator
//                             (architect 2026-08-05, on Ctrl+H until
//                             2026-09-01) and admitted on the same
//                             reasoning as the two above: in the mode it is not
//                             an authoring chord that would leave the frozen now
//                             side describing a state that no longer exists, but
//                             the view's OWN act — it applies the SELECTED diff
//                             flags backwards into the live store and then
//                             CLOSES the view, so the mode never outlives the
//                             state it was measured against.
//                             IT IS THE SECOND ADMISSION CONDITIONAL ON THE
//                             SESSION: with no diff flag selected and none
//                             focused there is nothing to revert, so the chord
//                             drops here as a consumed no-op
//                             (history_revert_actionable, app_state.h — the
//                             subject AND the lock since planner decision 58,
//                             whose one code reader is this line). THE FACE
//                             MIRRORS IT AGAIN SINCE
//                             2026-08-30: the Revert button greys from this
//                             same line — one decision for the chord and the
//                             glyph, the cleanest shape the enabled predicate
//                             has — as it did from 2026-08-05 until the
//                             architect reversed the face half on 2026-08-15,
//                             because this bit MOVES DURING A VISIT (every
//                             click that selects or clears changes it) so the
//                             grey tracked the diff-flag SELECTION and blinked
//                             at interaction cadence, the argument that then
//                             took the four cardinal arrows always-on. The
//                             truthful-buttons ruling withdrew that argument
//                             ("Any time a button would be a no-op, grey it")
//                             and deleted the lift that kept the button lit;
//                             the full record is at the history companions'
//                             arm in redesign_button_enabled, app_state.h.
//                             IT IS NOT DISPATCHED FROM HERE, and not from a
//                             mode arm either: admitting it lets the press fall
//                             through to on_key's ordinary body, which is what
//                             puts it BELOW the read-only gate — a backstop
//                             since decision 58, this admission refusing a
//                             locked tab first (the lock
//                             means hands off the piece's authored state, and
//                             this act writes it; Save-and-Commit, which
//                             authors nothing, is admitted by that gate
//                             instead).
//   - Ctrl+Q                → the close routing.
//   - Esc (bare)            → ITS EXISTING BINDINGS, AND NOT ONE OF ITS OWN
//                             (architect 2026-08-04, closing the arc's recorded
//                             cost). Admitting it adds NO Esc place of its own:
//                             the bare-Esc inventory is the one enumerated at
//                             on_key's dispatch point (input_handler.cpp), and
//                             this line lets exactly the binding that sits BELOW
//                             it run — the RENDER / BATCH CANCEL (a render
//                             launched before `h`, whose progress line the mode's
//                             corner outranks). It sits BELOW this gate in
//                             on_key and mutates no authored state, so the
//                             frozen now side is untouched — the same argument
//                             the read-only allowlist admits Esc on. (The REGION
//                             HIDE was the other one the admission bought until
//                             2026-08-21, when it retired: an overlay carried in
//                             from before `h` now leaves by the rule or by bare
//                             `[` outside the view.)
//                             IT CANNOT CLOSE THE VIEW, structurally rather than
//                             by refusal: the toggle is handle_history_mode_key's,
//                             and that function's whole vocabulary
//                             (history_mode_owns_key, whose declaration below
//                             enumerates it) carries no Esc shape in any modifier
//                             combination, so no Esc reaches it. The
//                             view's exits are unchanged, and `h` is still the
//                             key that leaves. With no render running a bare
//                             Esc falls to the card dismissal (the whole
//                             notification stack, if any card is standing)
//                             exactly as it does everywhere else, and is a
//                             consumed nothing only where that too finds
//                             nothing to dismiss.
//
// WHILE THAT EDITOR IS OPEN THIS GATE IS NOT REACHED AT ALL: the keyboard-modal
// editor gate sits ABOVE the mode in on_key, so the editor owns every key its
// modality owns — `h`, `,` and `.` included, which TYPE into the buffer instead
// of stepping the walk underneath it (they are printable, so the editor consumes
// them and returns above this line), exactly as they do under any other editor.
//
// WHAT THE MODE CLAIMS ONE LINE ABOVE THIS GATE, and so never reaches it:
// handle_history_mode_key's own vocabulary — bare `h` (the toggle), bare `u`
// (the CUMULATIVE READING's toggle, 2026-08-08), bare `g` (the WALK's toggle,
// 2026-08-18), bare `,` and
// `.` (the walk), bare Tab / Shift+Tab / IsoLeftTab (the DIFF-FLAG CYCLE),
// bare Home / End (the ABSOLUTE ends of the song,
// not the trim bounds) and bare `c` (working zoom centered on the mode's own
// focus). Four of those families joined on 2026-08-05, and they are claimed
// rather than admitted for one reason: each is a MODE-LOCAL re-expression,
// reading the diff-flag list, the mode's focus or the reading bit instead of the
// live stores and the live tab band the ordinary arms would reach. That
// function's declaration comment carries the membership; this gate never sees
// any of it.
//
// WHAT IS DELIBERATELY OUT, beyond the obvious authoring chords: the PLAYHEAD
// steps (they move the cursor, and in the marker lane the very same press nudges
// a marker), `f` (a session-state toggle), `o`, and CTRL+SHIFT+TAB, the
// paired-tab march (out here from the start, claimed above as the walk cycle's
// reverse from 2026-08-07, and back on this list since 2026-08-18 — the march
// still never runs in here, by this gate dropping it again).
//
// AND BOTH RENDER CHORDS ARE OUT since 2026-08-08 (architect), Ctrl+Alt+R having
// joined its shifted twin here when the checkpoint act moved onto Ctrl+S: a
// render is an authoring-adjacent act with no meaning in a viewer, and with
// nothing left to select between, the chord has no in-mode meaning to admit. The
// consequence is the roster's, and it is the derived partition working: the
// Render button wears its ORDINARY label and icon in here over the mode's
// disabled face, joining Undo, Redo and the rest of the consumed roster with no
// hand entry anywhere.
//
// VIEWS AND A/B TAB SWITCHES ARE BOTH ADMITTED, the second since 2026-08-18
// (architect: "ctrl+tab should work as normal in history view"). A view switch
// re-reads THE SAME piece — the same three sidecar texts the now side was
// frozen from, the same delta, another column of it — and SO DOES A TAB SWITCH,
// which is what makes the admission cheap rather than a re-entry: the tabs hold
// a value-shaped band alone and share both marker stores and the engine
// settings, so nothing the delta is made of moves with them. THE OLD REFUSAL'S
// PREMISE was that a tab switch "swaps the per-tab band the session was
// measured with", and the band is not what the session is measured with; the
// architect admitted views on 2026-08-04 and left the tabs out, and both Tab
// chords then stopped reaching this gate at all (the mode took Ctrl+Tab for its
// walk cycle on 2026-08-05, Ctrl+Shift+Tab for that cycle's reverse on
// 2026-08-07) until the walk moved to its own buttons and gave the chord back.
// The BAND a switch moves is the user's own for the whole visit now, the view
// having stopped owning navigation state on 2026-08-18.
// The 2026-08-04 ratification also covered the BARE cycle, on the argument that
// Tab and `c` navigate by LIVE MARKERS; the architect SUPERSEDED that half on
// 2026-08-05 by giving the mode its own Tab and its own `c`, which navigate by
// the diff flags instead — so nothing walks a live marker in here and the
// argument's premise is gone, while the tab-band argument, which was never about
// markers, stands untouched.
//
// THE REDESIGNED BUTTONS AND THE COMMAND MENU'S ITEMS PASS THROUGH HERE
// UNCHANGED, which is why they need no rule of their own: both synthesize a
// chord and call on_key (finish_chrome_press_release and
// finish_dropdown_release, each at its own lift),
// so Save, Undo, Redo, Render and the view bar drop at this gate exactly as
// their keys do. AND A MENU'S ITEMS DO IT FOR REAL SINCE 2026-08-08, not
// merely in principle: a command menu OPENS inside the view (the architect
// narrowed toggle_dropdown's lockout to the Settings anchor, whose items reach
// the settings editor by a direct call and so have no gate of their own), and
// this predicate is what admits File's Ctrl+Q — and, since 2026-08-29, its
// Ctrl+O — in there. THE FILE MENU HAS NO DEAD ROW IN THE VIEW as of that day
// (architect, "admit both"): Open project rides this admission, Quit always
// did, and Synchronize's act carries no history-mode refusal at all — its own
// bare `\` riding this admission too since 2026-08-31, so the row and its
// chord answer the view alike.
// (It admitted the deleted Navigation menu's zoom, zoom-out and overview rows
// the same way, refusing nothing else on it — its remaining four were claimed
// one line above as the mode's own vocabulary — while the row whose chord means
// something ELSE in the view greyed at the item instead, which is the one thing
// a chord dispatch cannot express: the chord acts, it is just not the act the
// label names. That menu and that grey are deleted 2026-08-15.)
//
// AND SINCE 2026-08-04 THIS GATE IS ALSO READ BY THE FACES: a button whose chord
// this predicate blocks wears its row's DISABLED face while the mode stands and
// ignores the pointer, so the roster says what it will do rather than swallowing
// clicks silently. The partition is DERIVED from this function (and hand-answered
// for the four anchors alone, which have no chord to ask about: Settings,
// Edit since 2026-08-20 and Series since 2026-08-27 all dead on the
// toggle_dropdown lockout, File live
// since 2026-08-13 — another, Navigation, was live from 2026-08-08 until its
// 2026-08-15 deletion), never
// hand-listed —
// history_mode_disables_button, input_pointer.cpp, which carries the whole
// inventory.
//
// THE PREDICATE IS FREE, NOT A MEMBER, for exactly that second reader: it is
// pure, and the face derivation asks it about a table of chords with no press
// and no handler in hand. IT TAKES THE WHOLE AppState alongside key+mods because
// TWO admissions are conditional on session state (the revert act's, on a
// subject standing on a writable tab, and the load-in-place's, on the active
// walk carrying a member). THEY WERE FOUR UNTIL 2026-09-01, when the COMMIT
// ACT'S TWO — head_delta_empty and history_checkpoint_in_flight, Ctrl+S's since
// 2026-08-08 — moved to the act on the architect's ruling that a gate's
// membership is the chord's alone (a state term here makes the gate answer the
// view's own chord with "not available in the history view"); the two that
// remain say their own true sentence at the gate's call site instead of falling
// into that one. Both readers hand it
// the same `app` — each condition is decided HERE and restated at neither
// caller, which is what keeps the key that refuses and the face that greys one
// decision rather than two spellings of one. THE REVERT ACT'S ADMISSION HAD
// NO FACE READER from 2026-08-15 to 2026-08-30, a scope rather than a second
// spelling: its subject term still decided the KEY here while
// redesign_button_enabled lifted the four history companions over the derived
// partition, so Revert stayed lit on an empty subject — the architect's
// reversal of a grey that tracked the diff-flag selection and blinked at
// interaction cadence; the truthful-buttons ruling deleted that lift, so the
// face reads this admission through the partition again, recorded at the
// companions' arm in app_state.h. It took the HistoryMode struct
// alone until the in-flight bit arrived (that bit living on AppState because
// the act outlives the view it was launched from); the bit left again with the
// commit act's terms on 2026-09-01, and the whole AppState stays because the
// REVERT admission composes the active tab's lock, which is no more the mode's
// than the worker was.
bool history_mode_key_blocked(GuiKey key, GuiInputState mods,
                              const AppState& app) {
    const AppState::HistoryMode& mode = app.history_mode;
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    const bool bare  = !ctrl && !shift && !alt;
    // THE ZOOM STEP PAIR IS CTRL+`=` / CTRL+`-` SINCE 2026-08-27 (bare is
    // vertical, ctrl is horizontal — the magnification pair below holds the
    // bare forms now).
    const bool is_zoom_symbol =
        ((key == GuiKeys::Equal || key == GuiKeys::Minus) &&
         ctrl && !shift && !alt);
    // THE WAVEFORM MAGNIFICATION PAIR, bare `=` and bare `-` since 2026-08-27.
    // The view paints the SAME PLATE, so the gain is as live in here as the
    // zoom is — it changes the picture and nothing the mode is reading. Its
    // two buttons stay lit in the view for that reason, the derived partition
    // walking the chord table through this gate. CTRL+0 LEFT THIS ALLOWLIST
    // 2026-08-27 with the reset chord itself.
    const bool is_waveform_magnify =
        ((key == GuiKeys::Equal || key == GuiKeys::Minus) && bare);
    const bool is_zero  = (key == GuiKeys::Digit0 && bare);
    // THE CENTERED PIN, bare `y` (2026-08-31, R11) — a VIEWPORT preference,
    // admitted where FOLLOW is not: follow's chase is playback's and playback
    // is removed from the view whole, so admitting `f` would admit a lamp
    // with nothing to do, while the centered derivation reads the SAME
    // resting cursor the view's lanes read (the pre-paint hook's resting
    // half) and pins the camera in here exactly as outside. Its icon-row
    // button stays LIVE in the view through the derived partition on this
    // line.
    const bool is_centered = (key == GuiKeys::Y && bare);
    const bool is_page_updown =
        ((key == GuiKeys::PageUp || key == GuiKeys::PageDown) && bare);
    // THE LOAD-IN-PLACE IS EITHER WALK'S ACT (architect 2026-08-08, superseding
    // the 2026-08-07 "the Local walk consumes it": a local member is a STATE of
    // this session's timeline, and loading a state in place is exactly what the
    // act does). The two differ only in how the member is SPELLED in the
    // confirmation — a short SHA on the Remote tab, a NUMBER on the Local one
    // — and the routing lives at confirm_load_in_place, not here.
    //
    // THE TERM IS THE ACTIVE WALK'S, AND IT IS ONE TERM FOR BOTH SINCE
    // 2026-08-09: the walk must have a member. The act names THE VIEWED one,
    // so a walk with none has nothing to name and
    // nothing to load, and one decision refuses the key and greys the icon
    // row's load-in-place button — the head delta's own shape.
    //
    // IT WAS THE LOCAL ARM'S ALONE until the empty COMMIT walk became a legal
    // standing state: the Local walk carries U + R + 1 members and can only be
    // empty UNBOUND, while the Remote one now opens at `0/0` whenever a piece
    // has no eligible checkpoint. So the honest gate against the blank-lane
    // walk covers both, and the source fork this predicate carried for it is
    // gone (the vocabulary fork stays where it always was, at member_label's
    // spelling and at confirm_load_in_place).
    const bool is_load_in_place =
        (key == GuiKeys::Apostrophe && bare && mode.walk_count() > 0);
    // THE REVERT ACT (2026-08-05), the mode's THIRD admitted mutator and its
    // SECOND session-conditional admission: BARE `v` is admitted only while
    // there is a subject to revert — a selected diff flag, or the focused one
    // — so with nothing selected the key drops here as a consumed no-op. (It
    // was CTRL+H until 2026-09-01, when the architect moved the act onto a
    // bare letter freed by the render player's Stop; the succession is at the
    // dispatch arm, input_handler.cpp.) THE
    // REVERT BUTTON TAKES ITS ROW'S DISABLED FACE FROM THIS SAME LINE — one
    // decision for the chord and the glyph, through history_mode_disables_
    // button's walk of the chord — as it did until 2026-08-15, when the
    // architect reversed the face half alone (the grey tracked the diff-flag
    // SELECTION, so it blinked at interaction cadence, the same argument that
    // took the four cardinal arrows always-on) and redesign_button_enabled
    // lifted the companions over the partition; the 2026-08-30 truthful-
    // buttons ruling deleted that lift, so the face reads this term again.
    // THE TERM IS history_revert_actionable SINCE PLANNER DECISION 58 (the
    // same day): the subject AND the lock composed once, so a locked tab's
    // Revert greys from this line too — the admission used to ignore the lock
    // and lit a button whose chord the read-only gate below then dropped. So
    // on a locked tab this gate now refuses the key ahead of that gate, and
    // the card is this gate's (the view's), not the lock's. Unlike
    // the two mutators above it, this key is NOT dispatched from a mode arm:
    // it falls through to on_key's ordinary body, below the read-only gate,
    // which is a backstop for it now rather than its first refusal.
    const bool is_revert_act =
        (key == GuiKeys::V && bare && history_revert_actionable(app));
    // CTRL+S IS THE ACT IN HERE (architect 2026-08-08, moving it off Ctrl+Alt+R
    // — the act saves first, so it belongs on the save chord), AND IT IS
    // ADMITTED UNCONDITIONALLY SINCE 2026-09-01 (architect: a gate's
    // membership test is the CHORD'S ALONE — a chord the mode owns is admitted
    // as vocabulary, and its ACT answers whatever the session cannot do right
    // now). IT CARRIED THE ACT'S TWO SESSION TERMS UNTIL THEN — a non-empty
    // head delta (2026-08-05) and no checkpoint in flight (2026-08-07) — and
    // that is exactly the shape the architect ruled out: with either failing,
    // the press fell out of this list into the gate's GENERIC sentence, which
    // says "Ctrl+S is not available in the history view" about the one chord
    // this view exists to run. The act owns both refusals now
    // (open_history_commit_editor, below): a checkpoint in flight says the
    // publishing sentence three other sites already say, and an EMPTY HEAD
    // DELTA is silent under the one-dimensional rule (nothing to commit is a
    // state the greyed Save-and-Commit face is already showing).
    //
    // THE FACE IS THE ACT'S TOO, and it did not move an inch when the terms
    // did: the Save button greys inside the view through
    // history_checkpoint_actionable (app_state.h — the same two terms plus the
    // mode bit) read by redesign_button_enabled's own Save arm, instead of
    // through this line and the derived partition. One decision for the key
    // and the glyph, as before; only its home changed.
    //
    // THE QUESTION THE ACT ASKS IS UNCHANGED: IS THERE ANYTHING TO
    // CHECKPOINT? — the head delta, live against the newest commit. Static
    // once measured, and it rests TRUE (greying the act) in the window before
    // the prefetch has delivered member 0 to measure against (2026-08-07,
    // measure_history_head_delta owns that rule).
    //
    // A CLEAN-BUT-UNPUSHED SESSION IS GREY, AND THAT IS THE MODEL RATHER
    // THAN A GAP (2026-08-09): the act publishes what it commits, and a branch
    // already committed and merely unpushed is fixed in the TERMINAL, where the
    // user has git. A push-pending bit admitted the chord for an in-app retry
    // until this date; the retry family went with the graded machinery, and the
    // act's clean arm now says so on stderr in as many words.
    //
    // THE PLAIN DISK SAVE IS NOT SEPARATELY REACHABLE, deliberately: in the
    // view this chord has exactly one meaning, and a session with nothing to
    // checkpoint saves by leaving the view.
    const bool is_save =
        (ctrl && !shift && !alt && key == GuiKeys::S);
    const bool is_ctrl_q = (ctrl && !shift && !alt && key == GuiKeys::Q);
    // CTRL+O IS ADMITTED (architect 2026-08-29, "admit both"), and it is the
    // ONE OTHER ACT ON THE SESSION AS A WHOLE — Ctrl+Q's own family, sitting
    // beside it here as it sits beside it in on_key's dispatch. The reasoning
    // is the quit's exactly: an open TEARS THIS VIEW DOWN, which is not a
    // reason to refuse it but a description of what a reopen does to
    // everything, and the admitted Ctrl+Q already ends the view the same way.
    // The picker it raises stands OVER the mode — its router runs ahead of
    // this gate in on_key, its veil consumes the view's presses, and its band
    // is the waveform's lower half, clear of the diff lane — so a Cancel or an
    // Esc leaves the view exactly as it stood, this view owning no navigation
    // state to disturb. Until this date the chord fell through this list as a
    // consumed no-op, which made two of the File menu's three rows dead in
    // here while the third was live.
    //
    // IT IS THE SHARED PREDICATE'S SHAPE and not a second spelling of it
    // (is_open_project_key, gui_input.h — ctrl-exact, so Ctrl+Shift+O stays
    // the strict rule's consumed no-op here as everywhere), which is also what
    // keeps this admission and the read-only allowlist's answering the same
    // question the same way. THE FILE ANCHOR'S FACE READS THIS LINE by the
    // derived partition: with Quit, Open and — since 2026-08-31 — Synchronize
    // all admitted, every row of that menu is live in the view.
    const bool is_open_project = is_open_project_key(key, mods);
    // SYNCHRONIZE IS ADMITTED TOO, bare `\` since 2026-08-31, and it needs no
    // argument of its own: the ACT has run in the view since 2026-08-29
    // (architect, "admit both") through the File menu's row, whose road meets
    // no gate here at all, so refusing the key would make the row and its
    // chord disagree — the one thing an allowlist may not do. The act's own
    // reasoning is the easy one: it authors nothing, stops no playback, writes
    // outside the project entirely, and its sentences are notification cards,
    // which this mode cannot hide.
    const bool is_sync_external = is_sync_external_key(key, mods);
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
    // THE A/B TAB SWITCH (architect 2026-08-18): "ctrl+tab should work as
    // normal in history view — it becomes essentially another view but in
    // mostly readonly mode, with no playback since trim is not mutable". So
    // Ctrl+Tab is an ORDINARY tab switch in here — not a special case and not a
    // closer: the view stays up, the newly active tab's band goes live, and the
    // switch's own tail (kick_waveform_sync) republishes the lane over it.
    //
    // THE MODE RE-BINDS NOTHING, and that is why this is one allowlist entry
    // rather than a re-entry: the A/B tabs hold a VALUE-SHAPED BAND ALONE
    // (viewport, zoom, playhead, trim, read_only) and share the warp store, the
    // phase-reset store and the engine settings, while the displayed delta's
    // whole vocabulary is those two stores plus `scale` — so the two walks, the
    // frozen now side and the head delta describe the same piece on either tab.
    // The undo/redo stacks the LOCAL walk indexes are session-global for the
    // same reason. What the switch does move is the WINDOW, which the view has
    // not owned since the entry stopped framing and the exit stopped restoring.
    //
    // ITS SHIFTED TWIN NEVER REACHES THIS LIST — Ctrl+Shift+Tab is the mode's
    // OWN since 2026-08-18 (history_mode_owns_key claims it and
    // handle_history_mode_key consumes it one line above this gate), where it
    // is the paired march built over the diff-flag cycle: the same chord, the
    // same composition, and only what "Tab" denotes changes with the context.
    // It was the mode's REVERSE walk cycle from 2026-08-07 until the walk left
    // row 3 earlier that day, and a blocked no-op for the hours between.
    const bool is_ctrl_tab =
        (ctrl && !shift && !alt && key == GuiKeys::Tab);
    return !(is_zoom_symbol || is_waveform_magnify || is_zero || is_centered ||
             is_page_updown ||
             is_audio_view_switch || is_marker_view_switch ||
             is_view_selector || is_esc || is_ctrl_tab ||
             is_load_in_place || is_revert_act ||
             is_save || is_ctrl_q || is_open_project || is_sync_external);
}

// -- THE COMMIT ACT'S GUI HALF ----------------------------------------------
//
// The act itself is commit_history_checkpoint (history_diff.h): the three
// writes, the pathspec-scoped commit, the push, and every stderr line about
// them. What lives here is the QUESTION in front of it (the commit-title
// editor, 2026-08-07), THE SAVE in front of that (2026-08-04 — the act is "Save
// and Commit" now), the CLOSE behind the save (2026-08-05, re-partitioned
// 2026-08-07), and the DISPATCH onto the background worker with the report that
// comes back from it.

// ASK FOR THE MESSAGE. One caller: Ctrl+S's own arm while the mode stands
// (input_handler.cpp — it was Ctrl+Alt+R's until 2026-08-08, when the architect
// moved the act onto the chord its own first step already is).
//
// IT USED TO ASK FOR PERMISSION (the fourth prompt, `y` or Esc, whose text named
// a title the user could not change). The architect replaced it with this editor
// on 2026-08-07, superseding his own "the commit message is derived, not
// chosen": the act still pauses exactly once, and the pause now carries
// information. THE DEFAULT IS THE OLD DERIVATION — history_checkpoint_title,
// still the one owner of the `Update <id>` spelling — prefilled and
// open-selected, so the common case is a bare Enter and the uncommon one is
// typing over it.
//
// The structural guards are the act's preconditions restated as "there is
// something to ask about": no mode, or a session that never resolved a piece
// directory, and there is no commit to offer. Neither is reachable from the one
// call site (the chord is admitted only while the mode stands, and an available
// session always carries both strings), which is why they are silent.
//
// THE TWO SESSION REFUSALS ARE THIS ACT'S OWN SINCE 2026-09-01 (architect: a
// gate's membership is the chord's alone, so a chord the mode owns says its
// TRUE reason rather than falling into the allowlist's "not available in the
// history view"). They were the allowlist's admission terms from 2026-08-05
// and 2026-08-07, which dropped the press ABOVE the `s` arm and so never let it
// reach here at all:
//   - A CHECKPOINT ALREADY IN FLIGHT says the publishing sentence its three
//     other sites say (kCheckpointPublishing, the head of this file) — single
//     in flight, and the wait is seconds. It is a card rather than a silence
//     because the fact is a BACKGROUND act's, not a state the view is showing.
//   - AN EMPTY HEAD DELTA IS SILENT, the benign one-dimensional refusal already
//     at its state: the newest checkpoint already carries this session's
//     authoring content, nothing would change, and the Save-and-Commit face is
//     greyed on that very predicate (history_checkpoint_actionable, app_state.h
//     — the ONE composition of these two terms, which the face reads and this
//     body forks on) with its tooltip naming the act.
// The order is the face's own: the in-flight bit outranks the delta, because a
// worker mid-act is the fact the user is waiting on either way.
//
// PLAYBACK STOPS AS THE MODAL OPENS, through the shared owner and past every
// guard, exactly as the three editors before it do. It is a structural no-op in
// practice — the history view is silent by ruling, its entry having stopped any
// session running before `h` — and it is here because the rule is the modal's,
// not the surface's.
void GuiInputHandler::open_history_commit_editor() {
    if (!app.history_mode.active) return;
    if (text_editor::is_active(app.commit_title_editor)) return;
    if (app.history_checkpoint_in_flight) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kCheckpointPublishing);
        return;
    }
    if (app.history_mode.head_delta_empty) return;
    const std::string& dir = app.history_mode.session.project_directory();
    if (dir.empty() || app.history_mode.session.sidecar_base_name().empty()) {
        return;
    }
    playback_lifecycle.stop_playback_for_modal_open();
    text_editor::enter(app.commit_title_editor,
                       /*target=*/0,
                       history_checkpoint_title(dir),
                       text_editor::Kind::CommitTitle);
    // OPEN-SELECTED ON THE SEED, the prefilling opener's convention (the flag
    // editor's): the first keystroke replaces
    // the default wholesale, so writing your own title is one act rather than a
    // select-all first. The seed is never empty here — the title is built from a
    // directory name this function has already refused to proceed without.
    app.commit_title_editor.selection_anchor = 0;
    app.commit_title_editor.cursor_pos =
        static_cast<int>(app.commit_title_editor.pending.size());
    // A modal-dialog OPEN damages the whole window (the box's rect does not
    // exist before its first paint — the settings opener carries the rule).
    viewport.invalidate_all();
}

void GuiInputHandler::commit_title_editor_exit_no_commit() {
    if (!text_editor::is_active(app.commit_title_editor)) return;
    viewport.invalidate_modal_dialog_area();
    text_editor::deactivate(app.commit_title_editor);
}

// Enter: run the act under the typed title.
//
// A BLANK BUFFER IS A RED FLASH, not a commit: git would take an empty message
// only under --allow-empty-message, and a checkpoint nobody can name is not a
// thing this product writes. Whitespace-only counts as blank (ASCII whitespace
// in the "C"
// locale — the settings editor's own trim rule), and the flash leaves the
// editor open with the text in place to be corrected, which is every editor's
// refusal shape here.
//
// THE TITLE IS TAKEN VERBATIM OTHERWISE — free UTF-8 text through the one
// incoming filter (text_editor::replace_selection), leading and trailing
// whitespace included if the user typed it. There is no second grammar: what is
// in the buffer is what the commit carries.
//
// THE EDITOR CLOSES BEFORE THE ACT RUNS, the old prompt's own order and for its
// reason grown sharper: the act closes the view and dispatches a worker, so
// leaving a modal editor standing over it would paint a caret into a strip whose
// question has been answered.
void GuiInputHandler::commit_title_editor_commit() {
    if (!text_editor::is_active(app.commit_title_editor)) return;
    const std::string title = app.commit_title_editor.pending;
    const bool blank = title.find_first_not_of(" \t\r\n\f\v") ==
                       std::string::npos;
    if (blank) {
        app.commit_title_editor.red = true;
        viewport.invalidate_modal_dialog_area();
        // THE CARD IS THE WHOLE MESSAGE HERE (architect 2026-08-30): this
        // refusal never had a stderr line and gains none — the red field says
        // that it refused, the card says why.
        notifications.notify(AppState::NotificationClass::Normal,
                             "Enter a title for the checkpoint");
        return;
    }
    text_editor::deactivate(app.commit_title_editor);
    viewport.invalidate_modal_dialog_area();
    run_history_commit(title);
}

// Routes a key to the active commit-title editor through the shared modal
// route. NO autocomplete hook: a commit message has no vocabulary to complete
// against, so bare Tab walks the modal's focus ring here from the first press
// (the one autocomplete model is at route_modal_editor_key).
bool GuiInputHandler::handle_commit_title_editor_key(GuiKey        key,
                                                     GuiInputState mods) {
    return route_modal_editor_key(
        app.commit_title_editor, key, mods,
        /*autocomplete=*/nullptr,
        [this] { commit_title_editor_commit(); },
        [this] { commit_title_editor_exit_no_commit(); },
        [this] { viewport.invalidate_modal_dialog_area(); });
}

// ---------------------------------------------------------------------------
// THE MEASURE PROPAGATE (architect 2026-08-20). The phase reset propagate's
// shape for the marker MEASURE field: Ctrl+/ captures the selected run's
// measures, Ctrl+Alt+/ replays them onto a destination run matched BY LABEL,
// under a signed measure offset typed into a modal dialog. The contracts are at
// the declarations in input_handler.h; the clipboard's own header states why
// the feature is warp-column only.

// Ctrl+/. The gates are the caller's (handle_mode_keys' arm above): W-mode, a
// non-empty CONTIGUOUS selection.
//
// UNLABELED MARKERS NEVER PROPAGATE A MEASURE, and that is the RULING rather
// than an oversight (architect 2026-08-20): membership is
// `warp_marker_propagates` — labeled AND effectively enabled — the one
// predicate the phase propagate's own walks take, so a selected marker with no
// label contributes no entry here and its opposite number is skipped at the
// destination. The label is what the paste matches on; a marker with none has
// nothing to align, and admitting it on one side alone would open a lockstep
// gap. To propagate a measure, name the marker.
//
// A MARKER WITHOUT A MEASURE IS CAPTURED, NOT SKIPPED: the entry records
// has_measure = false, and the paste writes that "none" onto its match as a
// CLEAR. The clipboard is a picture of the run, so a hole in the source is a
// hole in the destination.
void GuiInputHandler::copy_measures_from_selection() {
    const auto& mv = app.warpmarkers.markers();
    if (app.selected_markers.empty()) return;
    const int n = static_cast<int>(mv.size());

    // std::set is ascending, so the entries come out in store order — which is
    // the order the destination walk produces and the order the lockstep pairs
    // them in.
    std::vector<MeasureClipboardEntry> entries;
    for (int i : app.selected_markers) {
        if (i < 0 || i >= n) continue;
        if (!warp_marker_propagates(mv, i)) continue;
        MeasureClipboardEntry e;
        e.label_name   = warp_marker_label_name(mv[static_cast<size_t>(i)]);
        e.measure_text = mv[static_cast<size_t>(i)].measure;
        e.has_measure  = !e.measure_text.empty();
        entries.push_back(std::move(e));
    }
    app.measure_clipboard.set(std::move(entries));
}

// Ctrl+Alt+/'s opener. The gates are the caller's (W-mode, a non-empty measure
// clipboard, exactly one selected warp marker); the bounds check here is the
// opener's own, the phase paste's arrangement.
//
// PLAYBACK STOPS AS THE MODAL OPENS, through the shared owner, exactly as the
// four dialog editors before it do.
void GuiInputHandler::open_measure_paste_editor() {
    if (app.measure_clipboard.empty()) return;
    if (text_editor::is_active(app.measure_offset_editor)) return;
    if (app.selected_markers.size() != 1) return;
    const int anchor = *app.selected_markers.begin();
    const int n = static_cast<int>(app.warpmarkers.markers().size());
    if (anchor < 0 || anchor >= n) return;

    playback_lifecycle.stop_playback_for_modal_open();
    // THE ANCHOR RIDES IN THE EDITOR'S OWN SUBJECT SLOT (the commit-title
    // editor parks a 0 there; the flag editor parks a marker index): the paste
    // has exactly one subject and exactly one modal, so the index lives for the
    // session and dies with it, and there is no AppState field beside the
    // clipboard to leave stale after a cancel.
    text_editor::enter(app.measure_offset_editor,
                       /*target=*/anchor,
                       "0",
                       text_editor::Kind::MeasureOffset);
    // OPEN-SELECTED ON THE SEED, the prefilling openers' convention: typing a
    // digit replaces the `0` wholesale, while a bare Enter over it pastes
    // unshifted — the repeat case, and the one this dialog is fastest at.
    app.measure_offset_editor.selection_anchor = 0;
    app.measure_offset_editor.cursor_pos =
        static_cast<int>(app.measure_offset_editor.pending.size());
    // A modal-dialog OPEN damages the whole window (the box's rect does not
    // exist before its first paint — the settings opener carries the rule).
    viewport.invalidate_all();
}

void GuiInputHandler::measure_offset_editor_exit_no_commit() {
    if (!text_editor::is_active(app.measure_offset_editor)) return;
    viewport.invalidate_modal_dialog_area();
    // The anchor dies with the session: deactivate clears the State, so the
    // subject slot cannot outlive the modal that seated it.
    text_editor::deactivate(app.measure_offset_editor);
}

// Enter: parse the offset, run the paste, close on success.
//
// THE FIELD GRAMMAR IS ONE CANONICAL SIGNED DECIMAL INTEGER and it is judged
// HERE rather than on the keyboard, the Kind carrying no grammar: `0`, `12`,
// `-3`. No `+` sign (the absence of a minus IS the positive spelling), no
// leading zeros, and `-0` refused — one spelling per value, the frame_format.h
// discipline every other serialized-adjacent number in this product takes. It
// is not a serialized value, but it is the number the pasted measures are
// computed from, and a field that accepts `007` accepts two spellings of one
// paste.
//
// A REFUSAL LEAVES THE EDITOR STANDING with the text in place to be corrected —
// the dialog editors' one refusal shape — AND SAYS WHY ON A CARD (architect
// 2026-08-30). There are TWO producers of it: this grammar, whose card is the
// only thing it says (there was never an stderr line here and none is added),
// and the paste's own refusals below, which return their sentence having
// written nothing. The second is the more interesting one and it is
// deliberately not a clamp: an offset that would carry a measure past the
// bracket is a mis-typed offset, and silently pinning a run of bar numbers to
// 99999 would be a confident wrong answer.
void GuiInputHandler::measure_offset_editor_commit() {
    if (!text_editor::is_active(app.measure_offset_editor)) return;
    const std::string& text = app.measure_offset_editor.pending;

    bool        ok      = !text.empty();
    bool        negative = false;
    std::string digits  = text;
    if (ok && text.front() == '-') {
        negative = true;
        digits   = text.substr(1);
    }
    // At most six digits, so the accumulation below cannot overflow and the
    // bracket check in the paste is the only bound that matters.
    if (digits.empty() || digits.size() > 6) ok = false;
    if (ok && digits.size() > 1 && digits.front() == '0') ok = false;
    int64_t magnitude = 0;
    if (ok) {
        for (const char c : digits) {
            if (c < '0' || c > '9') { ok = false; break; }
            magnitude = magnitude * 10 + (c - '0');
        }
    }
    // `-0` is the second spelling of zero and is refused with the leading-zero
    // family it belongs to.
    if (ok && negative && magnitude == 0) ok = false;
    if (!ok) {
        app.measure_offset_editor.red = true;
        viewport.invalidate_modal_dialog_area();
        // The grammar's own card (architect 2026-08-30). Like the commit
        // title's, this refusal never had a stderr line and gains none.
        notifications.notify(AppState::NotificationClass::Normal,
                             "Enter a whole number with no leading zeros");
        return;
    }

    // THE PASTE'S REASON TRAVELS OUT WITH ITS REFUSAL (GuiOpRefusal,
    // warpmarkers_ops.h): the facts it refuses on are the clipboard's and the
    // bracket's, so the sentence is composed where the fact lives and the card
    // is raised here, at the layer that knows a press happened.
    if (GuiOpRefusal refusal =
            apply_measure_paste(negative ? -magnitude : magnitude)) {
        app.measure_offset_editor.red = true;
        viewport.invalidate_modal_dialog_area();
        notifications.notify(AppState::NotificationClass::Normal,
                             std::move(*refusal));
        return;
    }
    text_editor::deactivate(app.measure_offset_editor);
    viewport.invalidate_modal_dialog_area();
}

// Routes a key to the active measure paste-offset editor through the shared
// modal route. NO autocomplete hook: an integer has no vocabulary to complete
// against, so bare Tab walks the modal's focus ring from the first press (the
// one autocomplete model is at route_modal_editor_key).
bool GuiInputHandler::handle_measure_offset_editor_key(GuiKey        key,
                                                       GuiInputState mods) {
    return route_modal_editor_key(
        app.measure_offset_editor, key, mods,
        /*autocomplete=*/nullptr,
        [this] { measure_offset_editor_commit(); },
        [this] { measure_offset_editor_exit_no_commit(); },
        [this] { viewport.invalidate_modal_dialog_area(); });
}

// THE PASTE ITSELF — the offset editor's Enter, and the only caller.
//
// Returns A REFUSAL SENTENCE having written absolutely nothing when the paste
// cannot be honored whole; std::nullopt on every path that completed, including
// the ones that wrote nothing because there was nothing to write (GuiOpRefusal,
// warpmarkers_ops.h). The sentence is composed HERE, where the fact is — the
// clipboard entry that will not parse, or the measure number the offset would
// carry out of the bracket — and the caller raises the card.
//
// TWO PASSES, AND THE SPLIT IS THE CONTRACT: pass one resolves every
// destination's new measure text and can REFUSE; pass two writes them. A
// half-applied paste would leave a run of bar numbers the user has to
// reconstruct by hand — trim's own "no undo, so no half-measures" reasoning
// applied to an act that DOES have undo, because the refusal is free here and
// the undo entry would otherwise cover a state nobody asked for.
//
// THE LOCKSTEP IS THE PHASE PASTE'S, term for term: destination members from
// the anchor forward, paired positionally with the clipboard, stopping WHOLE at
// the first label divergence and reporting it in the family's one register
// (format_domain_timestamp, phase_reset_propagate.h). One side running out is a
// clean partial walk and stays silent, likewise the phase rule.
//
// THREE KINDS OF CLIPBOARD ENTRY, and only the first sees the offset:
//   * a DIRECT measure — the offset is added to its WHOLE part and the value is
//     re-spelled canonically through marker_measure.h's own writer, so the
//     fraction rides unchanged and there is exactly one spelling on disk.
//   * an OFFSET (`+`) measure — copied VERBATIM. It is already relative to its
//     own predecessor, so it means the same thing wherever the run lands, and
//     adding an absolute measure count to it would say something else entirely.
//   * NO measure — CLEARS the destination's. It is what the copy captured, and
//     a clipboard that could not express a hole could not reproduce the run.
//     A CLEAR CAN ORPHAN A `+` CHAIN whose link sits on a marker OUTSIDE the
//     pasted run: that successor's offset now has no resolved predecessor and
//     becomes UNRESOLVED. That is accepted and not guarded — it is exactly the
//     load-lenient, act-strict answer the grammar is built on (marker_measure.h:
//     an unresolved `+` still commits, saves, loads and paints, and only the
//     CONSUMER declines to act on it), and the alternative would be a paste
//     that reads its own successors' text to decide what it is allowed to
//     erase.
//
// NO VIEW SWITCH, NO RENDER, NO MAP REBUILD, and the playhead and selection are
// untouched — this is commit_measure_edit's damage profile scaled to many
// markers, not the phase paste's. A measure reaches neither the engine nor the
// render fingerprint, so the flags are the only thing that moved; and there is
// nowhere to LAND the reader, a measure being editable wherever the flag paints
// (the scoping note is at land_paste_in_target_view). No overlay-hide owner is
// reached, because none is called.
GuiOpRefusal GuiInputHandler::apply_measure_paste(int64_t offset_measures) {
    const auto& mv = app.warpmarkers.markers();
    const int   n  = static_cast<int>(mv.size());
    const int   anchor = app.measure_offset_editor.target;
    // The subject may have gone out from under the modal (an undo while it
    // stood). Nothing to paste onto: report nothing and let the editor close,
    // exactly as the flag editor's commit drops an edit whose target vanished.
    if (anchor < 0 || anchor >= n) return std::nullopt;

    std::vector<int> dest;
    for (int i = anchor; i < n; ++i) {
        if (warp_marker_propagates(mv, i)) dest.push_back(i);
    }

    const auto&  clip       = app.measure_clipboard.entries();
    const size_t pair_count = std::min(clip.size(), dest.size());
    size_t       matched    = 0;
    for (; matched < pair_count; ++matched) {
        if (clip[matched].label_name !=
            warp_marker_label_name(mv[static_cast<size_t>(dest[matched])])) {
            break;
        }
    }

    // Divergence (the loop broke) versus one side running out (a clean partial
    // walk): only the first says anything, the phase paste's own distinction.
    std::string stop_message;
    if (matched < pair_count) {
        stop_message =
            "Stopped at " +
            format_domain_timestamp(
                static_cast<double>(
                    mv[static_cast<size_t>(dest[matched])].time_frame),
                app, audio) +
            " (label name diverged)";
    }

    // PASS ONE — resolve, and refuse whole if anything cannot be honored.
    std::vector<std::string> resolved(matched);
    for (size_t k = 0; k < matched; ++k) {
        const MeasureClipboardEntry& e = clip[k];
        if (!e.has_measure) continue;          // stays empty: a clear
        MarkerMeasureValue v;
        std::string        err;
        // A clipboard measure that does not parse is BREACH-ONLY — every route
        // into the store runs the same validator (the two file parsers, both
        // history extractors, the measure editor's commit, and this paste's own
        // canonical writer) — but it refuses here rather than copying the bytes
        // through, because a verbatim copy of an unparseable measure would
        // write a load-fatal file and say nothing.
        if (!parse_marker_measure(e.measure_text, v, err))
            return "Measure rejected: " + err;
        if (v.is_offset) {
            // Relative already: verbatim, and the offset must not reach it.
            resolved[k] = e.measure_text;
            continue;
        }
        // THE OFFSET SHIFTS THE MEASURE NUMBER AND NOTHING ELSE: `12` pasted
        // at +5 is `17`, the fraction riding through in the parsed value and
        // re-spelled by the one writer. The bracket below is a check on the
        // NUMBER — an out-of-bracket result refuses the paste whole. (The
        // retired section qualifier rode through this same shape until the
        // 2026-08-21 sunset removed it from the grammar.)
        const int64_t shifted = v.whole + offset_measures;
        if (shifted < 1 || shifted > kMeasureMaxWhole)
            return "That offset would put a measure outside the allowed range";
        v.whole     = shifted;
        resolved[k] = format_marker_measure(v);
    }

    // PASS TWO — write. The snapshot is taken here, after the last refusal
    // point, so a refused paste pushes no undo entry and copies no store.
    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    bool                       changed   = false;
    for (size_t k = 0; k < matched; ++k) {
        GuiWarpMarker* m = app.warpmarkers.marker_mut(dest[k]);
        if (!m) continue;
        if (m->measure == resolved[k]) continue;
        m->measure = resolved[k];
        changed    = true;
    }

    // AN UNDO ENTRY IS A STATE CHANGE, NOT A GESTURE: pasting a run onto itself
    // at offset 0 reproduces every measure byte-equal and pushes nothing, the
    // shape every no-op commit in the product takes. affects_persistence stays
    // TRUE by default — a measure is serialized content, so a real write
    // dirties the tab like any other authored change.
    if (changed) {
        undo.push_undo_warp(std::move(pre_state));
        undo.recompute_dirty();
    }
    viewport.invalidate_top_strip();
    // The divergence report is a notification card (2026-08-29); a clean or
    // empty walk says nothing.
    if (!stop_message.empty()) {
        notifications.notify(AppState::NotificationClass::Normal, std::move(stop_message));
    }
    return std::nullopt;
}

// THEN DO IT — the commit-title editor's Enter, and the only caller.
//
// THE BYTES ARE REBUILT FRESH, NEVER THE SESSION'S FROZEN NOW SIDE, and this is
// the one place in the mode where the difference between them is real. The
// frozen side is honest about AUTHORED state — the mode's gates refuse every
// route that could change a marker or an engine setting — but the settings file
// also carries the per-tab VIEW BAND, and both allowlists admit routes that move
// it (membership re-derived 2026-08-12 under pan-primary): zoom, the paged
// scroll, the alt+wheel stepped pan, the overview command,
// the one nav drag on the mode's whole navigation surface (its pan and its
// ctrl zoom phase alike), and the mode's own cursor-moving acts
// — the diff-flag click, the deferred click act and the keyboard's Tab cycle,
// Home/End and `c`, which `0` reaches too from full zoom out. Committing the
// frozen text
// would therefore write a checkpoint whose view band is a stale copy of one the
// user has since moved — invisible in the diff (which displays only `scale=`)
// and wrong on disk. Rebuilding costs one serialization and is exactly what a
// Ctrl+S at this instant would write.
//
// AND IT IS WHAT KEEPS THE CHECKPOINT HONEST: what lands is exactly what a
// Ctrl+S at this instant would write, view band and all, rather than a snapshot
// the user has since navigated away from — invisible in the diff (which displays
// only `scale=`) and wrong on disk.
//
// THE ACT CLOSES THE VIEW (architect 2026-08-05). The view asks one question —
// what differs between this session and a checkpoint — and an act that has just
// made the answer "nothing" has answered it; leaving the user inside an empty
// view to press `h` is ceremony.
//
// THE PARTITION IS THE SAVE'S SINCE 2026-08-07 (architect, superseding his own
// "the view closes iff the checkpoint ends up in the repository"): THE VIEW
// CLOSES IFF THE SAVE LANDED. That is the last thing this thread knows — the
// checkpoint's own verdict arrives seconds later, on a worker, and a view held
// open until then would be a modal wait dressed as a review. So a failed save
// leaves the view exactly as it was (every refusal's shape) and a successful one
// closes it, whatever the repository then says; the four failing verdicts
// report through a CRITICAL NOTIFICATION CARD (architect 2026-08-29; the tab
// row's permanent critical chip from 2026-08-09, an acknowledge notice before
// that) instead of through a view left standing.
//
// AND THE ACT IS ASYNCHRONOUS FROM THAT POINT (same ruling). The save is the
// user's own bytes and stays synchronous; the checkpoint is `git add`, `git
// commit` and a network push, which used to freeze the window for as long as the
// remote took. Everything the act needs is CAPTURED BY VALUE here, on the main
// thread — the two path strings, the projects_repo setting, the title, and the
// freshly rebuilt now side — and handed to GuiHistoryCommitWorker, so the user
// can edit, render, undo and even load in place while the checkpoint publishes,
// and what lands is what was on screen when he asked. The capture happens BEFORE
// the close, deliberately: the two strings are the closing session's.
//
// SINGLE IN FLIGHT: the in-flight bit goes up at the dispatch below and comes
// down at the completion, and while it stands the chord that reaches this
// function is not admitted at all (the Save-and-Commit button's grey derives
// from that same one decision — see the note at the dispatch for why that half
// is structural rather than visible) and bare `h` will not open a new view.
//
// AND SINCE 2026-08-08 THE BIT ALSO LOCKS OUT EVERY SAVE, globally, which is
// what makes the coincident-write paragraph below safe rather than merely
// unlucky: the worker writes the three sidecars into projects/<id>/ off the main
// thread, and in that workflow a concurrent Ctrl+S would write the very same
// paths through the same fixed temp name. The refusal lives at the one save
// owner (GuiSaveOps::save) and its face is the Save button's "Committing...".
// THE PRELUDE SAVE BELOW IS EXEMPT BY ORDERING ALONE — it runs before the bit
// goes up, three statements down — so the act's own save needs no flag and no
// second entry point.
//
// THE ACT SAVES FIRST (architect 2026-08-04): the checkpoint is what you see,
// SAVED and published, one sentence. Before this the act wrote and committed the
// repo copies while the session still claimed unsaved changes — incoherent in
// the architect's own workflow, where the loaded source LIVES in the matched
// projects/<id>/ and the act's repo write therefore IS the working sidecar set:
// the bytes on disk were exactly a save's and the title bar still carried the
// dirty dot. So the prelude below is the REAL Ctrl+S, through its one owner
// (GuiSaveOps::save — the same three atomic writes beside the source, the same
// per-write stderr, the same note_saved tail — the reference move, the
// coalescing stamp's clear and the dirty refold), never a second
// writer or a partial imitation of it.
//
// A FAILED SAVE REFUSES THE WHOLE ACT, and by construction rather than by
// discipline: the refusal returns ABOVE commit_history_checkpoint, the only
// remaining call in this body that writes bytes or runs git, so no
// checkpoint-side write happens and no git child is spawned. That is
// narrower than "nothing reaches the repository": the save's own three
// writes are sequential, not cross-file transactional (save_ops.cpp), so a
// save that fails partway through can leave earlier atomic renames on disk
// exactly as any ordinary Ctrl+S failure can — and in the coincident
// projects/<id>/ workflow those are repository working-tree paths. The
// save's own failure line has already named the path; this one names the
// act that declined because of it. The editor is already down (its Enter
// closes it before calling here), which is every other
// failure's shape in this act too.
//
// THE DOUBLE WRITE IS DELIBERATE AND HARMLESS in the coincident workflow. When
// the source lives inside the matched projects/<id>/, the save and the act's
// own write hit THE SAME THREE PATHS with BYTE-IDENTICAL CONTENT — the now side
// is built from the save writers' own string halves (build_history_now_side
// mirrors refresh_active_tab_view_from_app onto local copies, so a save running
// first changes none of its bytes), and both writers are the atomic tmp + fsync
// + rename, so the second rename simply replaces a file with its own contents.
// It is not deduped: recognizing the coincidence would mean canonicalizing three
// absolute paths against the repo root and then carrying a skip that only one
// user's layout ever takes, to save three renames of bytes we already hold. When
// the source lives ELSEWHERE (the older workflow), both writes are wanted and
// distinct — the save publishes beside the source, the act publishes the repo's
// copies — which is the same code doing the same thing for the same reason.
//
// AND THE SAVE BUTTON STAYS (architect's explicit reasoning): saving to disk is
// its own act and the common one; this act is a save that also PUBLISHES. Two
// buttons because one is to disk and one is to disk and the remote.
void GuiInputHandler::run_history_commit(const std::string& title) {
    // THE VIEW CAN HAVE GONE UNDER THE EDITOR, and the act says so rather than
    // returning in silence (2026-08-29): the commit-title editor is the one
    // surface the FAILED-SCAN ARRIVAL leaves standing when it ends the visit
    // off a poll (on_history_prefetch_ready owns that edge and names both
    // surfaces), so a user who has already typed a checkpoint name can press
    // Enter into a mode that is no longer there. A notification card carries
    // the arrival's own sentence (2026-08-29).
    if (!app.history_mode.active) {
        notifications.notify(AppState::NotificationClass::Normal, kHistoryUnavailable);
        return;
    }
    // A SECOND ACT CANNOT ARRIVE HERE — the chord is not admitted while one is in
    // flight — so this guard is unreachable, and it asks THE WORKER rather than
    // the AppState mirror the admission reads, because what it protects is that
    // worker's single-job slot. (The two answer the same question a hair apart:
    // the slot frees at the completion event, the bit one call later, inside the
    // callback that event runs.)
    if (history_commit_worker.is_busy()) return;
    GuiHistoryCommitJob job;
    job.repo_root         = app.history_mode.session.repo_root();
    job.project_directory = app.history_mode.session.project_directory();
    job.base_name         = app.history_mode.session.sidecar_base_name();
    if (job.repo_root.empty() || job.project_directory.empty() ||
        job.base_name.empty()) {
        return;
    }

    // THE SAVE SAYS WHY, THE ACT SAYS WHAT IT COST: the owner cards the write
    // it could not do (save_ops.cpp, 2026-09-02), so this prelude raises no
    // second card and keeps the stderr line that names what did NOT happen —
    // the commit — for the terminal.
    if (!save_ops.save()) {
        std::fprintf(stderr,
            "warptempo_gui: Save and commit refused: the save failed, so "
            "nothing was committed\n");
        return;
    }

    // THE REST OF THE CAPTURE, all by value and all on this thread: the setting
    // the guard and the push will read, the title the user wrote, and the bytes
    // — rebuilt AFTER the save (which changes none of them, the coincident-write
    // paragraph above) and BEFORE the close (which is a viewport act and touches
    // none of them either), so the checkpoint is exactly this instant's state.
    job.projects_repo = app.projects_repo;
    job.title         = title;
    job.bytes         = build_history_now_side(app);

    // THE VIEW CLOSES ON THE SAVE, and the session's three strings — the derived
    // clone, the project directory and the base name — are already captured
    // above, so the close cannot take them with it.
    close_history_mode();

    // THE BIT GOES UP AFTER THE SAVE, WHICH IS THE WHOLE EXEMPTION the act's own
    // prelude needs: from here on every save is refused at GuiSaveOps::save, and
    // the save three statements above ran while nothing was in flight.
    app.history_checkpoint_in_flight = true;
    // TWO FACES COME OFF THIS ONE BIT AND ONLY ONE OF THEM IS VISIBLE. The
    // SAVE-AND-COMMIT face greys on it through history_checkpoint_actionable
    // (app_state.h — the allowlist's Ctrl+S admission derived that grey until
    // 2026-09-01, when the membership was purified and the term moved to the
    // act's own predicate; the same one decision, one house over), and that
    // face exists only inside the view — the view has just closed and `h`
    // refuses to reopen one while the bit stands — so that half is structural,
    // kept because it is not a second decision. What the user actually sees is
    // the GLOBAL one: the same bit greys the Save button in every view and
    // puts "Committing the checkpoint" on its TOOLTIP — the "Committing..."
    // LABEL died with row 2's labeled faces at the 2026-08-12 relayout, and
    // this note named its deleted owner until a 2026-08-15 re-grep
    // (redesign_button_enabled / redesign_button_glyph_swapped / the stateful
    // tooltip overload are the live readers), mirroring the save lockout
    // above.
    history_commit_worker.dispatch(
        std::move(job),
        [this](GuiHistoryCommitOutcome outcome) {
            on_history_checkpoint_complete(outcome);
        });
}

// THE CHECKPOINT'S REPORT, back on the main thread (the platform's completion
// eventfd, main.cpp's wiring). The act has already said everything it has to say
// on stderr — every verdict prints its own line from the worker thread — so what
// this owns is the ONE thing a background act cannot do for itself: telling the
// user, at the window, when the checkpoint he asked for did not happen.
//
// IT RAISES A CRITICAL NOTIFICATION CARD (architect 2026-08-29; the tab
// row's permanent critical chip from 2026-08-09 until then, and an
// acknowledge modal from 2026-08-07 before that): a failed checkpoint is
// critical, so its card STANDS UNTIL ITS X IS PRESSED — no clock, and
// nothing that comes after takes it down, a later success included. The
// chip's one clearing route was a later established success, because the
// chip was a SLOT that held the repository's last answer; a card is an
// EVENT, and the user closes it once he has read it, whatever the next act
// answered (the architect: persistent, the X alone). GuiNotifications owns
// the card; this is the critical class's one producer.
//
// THE PARTITION, over the act's SIX verdicts (GuiHistoryCommitOutcome,
// history_diff.h, whose contract comment owns what each one establishes):
//   ESTABLISHED, AND THEY RAISE NOTHING — Committed (made and published, the
//   ordinary ending) and NothingToCommit (the newest checkpoint already carried
//   these bytes AND the remote already had it). Neither is a failure; each
//   says what it has to say on stderr.
//   THE FOUR FAILURES — WriteFailed (nothing reached the repository at all),
//   CommitFailed (git made no checkpoint the act can stand behind), Committed-
//   NotPushed (the checkpoint is in the local branch and the remote has not got
//   it) and Unconfirmed (the act could not establish its answer). WHAT EACH ONE
//   ESTABLISHES IS THE ENUM CONTRACT'S TO SAY, and it says it once
//   (GuiHistoryCommitOutcome, history_diff.h) — this end of the wire needs only
//   which class each falls in, so the arms are not re-enumerated here.
//   The texts differ exactly where the
//   user's next move does, which is why the act distinguishes them at all. They
//   are SHORT because a card is one line and the detail is already on stderr,
//   verbatim and unchanged by this arc.
//
// AN UNANSWERED QUESTION IS NOT A SUCCESS, which is the whole point of the sixth
// verdict: Unconfirmed came back as NothingToCommit until 2026-08-09, so an act
// that established neither content nor publication was reported as a clean
// ending. It raises its own card like the other three failures; what it must
// never do is claim to have established anything.
//
// THERE IS NO RETRY KEY AND NOTHING TO ACKNOWLEDGE, and since 2026-08-09 no
// in-app retry either: a checkpoint that committed and failed to push is pushed
// FROM THE TERMINAL, and the next checkpoint act notices — its clean arm reads
// the branch against its remote, so a hand-push is what clears this report.
// That is the strict model's whole shape: the act does the sanctioned thing or
// it throws, and the fixing happens where git lives.
//
// AND NOTHING ASYNCHRONOUS RAISES A MODAL WITHOUT CLEARING THE WAY FIRST, which
// is what retired a whole family of guards this function used to owe (the parked
// notice, its three park classes, the dropdown close and the release-owned scrap
// clears, all deleted 2026-08-09 as producer-less). One asynchronous modal
// opener remains, the compositor's WM close (main.cpp's set_on_close), and it
// owes those guards in its own body: it force-ends every live gesture, hides the
// hint and closes the popup before raising the unsaved-work prompt. What is gone
// is an async opener that had to park and clear on someone ELSE's behalf — a
// card can be pushed from any clock at all, because it takes nothing from
// anyone.
void GuiInputHandler::on_history_checkpoint_complete(
        GuiHistoryCommitOutcome outcome) {
    app.history_checkpoint_in_flight = false;

    // RE-WARM THE WALK FOR EVERY OUTCOME THAT MAY HAVE MOVED HEAD (2026-08-07,
    // membership re-derived 2026-08-09). The prefetch store describes the
    // repository as of one tip, so the next `h` must see a checkpoint this act
    // made — and "made" is not the same set as "succeeded".
    //   FOUR MAY HAVE COMMITTED: Committed and CommittedNotPushed obviously did;
    //   CommitFailed may have, because the act reports it on a hung
    //   `post-commit` hook whose commit had ALREADY landed (git moves HEAD
    //   before running the hook — the recorded accepted consequence); and
    //   Unconfirmed may have, because ONE of its arms is the push verify, which
    //   is reached only past a commit that DID move the tip. Its other arms are
    //   the CLEAN one's and commit nothing — an unreadable local tip, a branch
    //   observably behind its remote, an unanswerable containment read — but a
    //   kick cannot tell them apart from here and does not need to: the whole
    //   set is admitted on the one arm that can have moved HEAD.
    //   TWO PROVABLY DID NOT: WriteFailed never reaches git at all (a detached
    //   refusal or a failed write), and NothingToCommit is the clean arm's
    //   in-sync ending, which runs no add and no commit by construction.
    // Kicking the two extra costs one scan on a rare failure and buys the walk
    // being TRUE after it; the old membership left the next visit reading a
    // pre-commit repository.
    //
    // THE KICK SUPERSEDES AN IN-FLIGHT SCAN rather than being swallowed by one,
    // and that is the funnel's own idiom rather than anything spelled here:
    // GuiHistoryPrefetch::kick BUMPS THE GENERATION, clears the queue and
    // replaces the pending run, so a scan started against the pre-commit tip is
    // abandoned mid-walk and its remaining members are dropped by tag. (The
    // freshness SHORT-CIRCUIT that treats a running scan as fresh lives at
    // kick_history_prefetch_if_stale, the `h` ENTRY's path, and this route does
    // not go through it — which is what keeps a completion-time re-warm from
    // being answered by the very scan it is meant to replace.) The scan's git
    // READS may also have raced this act's mutations — the accepted overlap
    // recorded at GuiHistoryPrefetch — and this kick is what rebuilds whatever
    // did. The view is normally already closed by now, but the funnel defers
    // rather than assumes.
    if (outcome != GuiHistoryCommitOutcome::WriteFailed &&
        outcome != GuiHistoryCommitOutcome::NothingToCommit) {
        kick_history_prefetch();
    }

    switch (outcome) {
    case GuiHistoryCommitOutcome::Committed:
    case GuiHistoryCommitOutcome::NothingToCommit:
        // THE TWO ESTABLISHED ANSWERS raise nothing: the checkpoint is
        // committed AND the remote observably carries it, and a clean ending
        // is not an event the user needs a card for (a render's completion
        // is ruled the same way). A failure card standing from an earlier
        // act stands on — it is the user's to close.
        break;
    case GuiHistoryCommitOutcome::WriteFailed:
        notifications.notify(AppState::NotificationClass::Critical,
                             "Checkpoint failed: nothing was committed");
        break;
    case GuiHistoryCommitOutcome::CommitFailed:
        notifications.notify(
            AppState::NotificationClass::Critical,
            "Checkpoint failed: files written but not committed");
        break;
    case GuiHistoryCommitOutcome::Unconfirmed:
        // NOTHING WAS ESTABLISHED HERE, and the card says exactly that. (The
        // chip's fill-only-an-empty-slot condition is gone with the slot: a
        // standing "committed but not pushed" card is not overwritten by this
        // one, both stand, and the actionable text is not lost to the vaguer
        // one.)
        notifications.notify(AppState::NotificationClass::Critical, "Checkpoint could not be confirmed");
        break;
    case GuiHistoryCommitOutcome::CommittedNotPushed:
        // The commit landed and the push did not. The fix is `git push` in the
        // terminal; the card stands until the user closes it. ONE CLAUSE like
        // its three siblings (2026-09-01): it read "Checkpoint committed; push
        // failed", the one semicolon among the four verdicts.
        notifications.notify(AppState::NotificationClass::Critical, "Checkpoint committed but not pushed");
        break;
    }
}

// -- THE REVERT ACT --------------------------------------------------------
//
// BARE `v`, THE HISTORY VIEW'S THIRD MUTATOR (architect 2026-08-05 on Ctrl+H,
// moved onto the bare letter 2026-09-01): apply the
// SELECTED diff flags' INVERSES to the live store of the active column, then
// close the view. The one caller is on_key's own `v` arm, which is reached
// only while the mode stands, only past the read-only gate, and only with the
// allowlist having admitted the chord — which it does only while a subject
// stands on a WRITABLE tab (history_revert_actionable, app_state.h, whose
// ONE code reader is that key gate; the lock joined the subject there under
// planner decision 58). The Revert BUTTON'S grey reads the same decision
// through the derived `h` partition — again since 2026-08-30: the architect
// reversed the face half alone on 2026-08-15, the grey having tracked the
// diff-flag selection and blinked at interaction cadence (the argument that
// took the four cardinal arrows always-on), and the truthful-buttons ruling
// reversed that reversal, so the button greys with no subject and a click on
// it is the dead face's consumed nothing. The record is at the history companions'
// arm in redesign_button_enabled, app_state.h; the predicate keeps its one code
// reader, the face reaching it through the partition's walk.
//
// IT IS FULLY MANUAL AND IT ALWAYS FORCES — the architect's explicit ruling, and
// the reason there is not one coherence check in this body. The user may select
// any subset of any commit's delta, in either compare reading, and the act
// applies each member blindly: it may well produce a state that is not the
// checkpoint's and not the session's, and that is the tool working as ruled
// rather than a hole in it. The mitigation is the one every authoring act has —
// ONE undo entry for the whole act, so Ctrl+Z takes back the lot.
//
// THE PER-CLASS INVERSE, read off the flag's own two bits:
//   * ADDED ONLY (`[+]`, the newer side has this line and the older did not) →
//     DELETE the live marker at that exact frame. None there is NOTHING
//     HAPPENING for that flag — never a refusal, never a diagnostic.
//   * REMOVED ONLY (`[-]`, the older side had it and the newer dropped it) →
//     PUT THE THEN SIDE BACK at its frame, replacing a live occupant there or
//     inserting fresh when there is none.
//   * CHANGED (the double flag) → SET the live marker at that frame to the THEN
//     side. Which is the SAME primitive as the removed arm — insert-or-replace —
//     so the two share one body and the distinction never reaches the code.
//
// ONE FLAG IS ONE LINE, AND COINCIDENT LINES ARE LEGAL, so BOTH arms walk
// coincidence rather than always answering with the first marker at the frame: a
// subject carrying two flags at one frame must reach two DIFFERENT markers, or
// two restores collapse into one (the second replacing what the first inserted)
// and a delete eats a marker this very act put back. The rule is one sentence
// for both arms — every arm consumes the NEXT PRE-ACT OCCUPANT at its frame, and
// a marker this act inserted is never anyone's target — carried by one per-frame
// skip counter (`skip` below, whose declaration owns the arithmetic).
//
// A WRITE THAT CHANGES NOTHING IS NOT A CHANGE: the replace arm compares the
// occupant's CANONICAL LINE against the then side's and does nothing when they
// are equal, so `changed` means "the state differed" rather than "a store call
// happened". That case is reachable — the ITERATIVE reading's delta is between
// two commits and blind to the live store, so a flag can name a change the live
// state already carries — and it is exactly the case that must not push an undo
// entry whose restore does nothing (nor re-trigger the target render).
//
// THE COLUMN IS THE ACTIVE ONE BY CONSTRUCTION: the lane paints only the active
// column's half of a delta (rebuild_history_diff_flags), so every ordinal in the
// subject names a flag of that column and the store to write is decided once,
// here, rather than per flag.
//
// THE THEN SIDE TRAVELS AS TEXT AND IS JUDGED BY THE FROZEN PARSER. A warp
// flag's then value is the sidecar's own payload token, verbatim, so this body
// RECONSTITUTES THE LINE — `[#]<frame>|<token>` — and hands it to
// parse_single_canonical_line, the loader's own per-line entry point. That is
// what keeps pass markers, label definitions, label references and typed scales
// working with no vocabulary of their own here: whatever the file could hold,
// the line holds, and the ONE grammar that reads it is the parser's — a
// measure suffix included, since the token is rest-of-line. The phase reset
// column needs no such trip: frame, the disable bit and the measure IS its
// line, and all three travel typed on the flag.
void GuiInputHandler::run_history_revert() {
    if (!app.history_mode.active) return;

    // THE SUBJECT: the selected ordinals, else the focused one alone. Ordinals
    // out of the list's range are dropped — the list is paint-cache output and
    // a mode edge empties it, so "nothing there" is the standing cold answer
    // every reader of it takes.
    const std::vector<HistoryDiffFlag>& flags = app.history_mode.flags;
    const int n = static_cast<int>(flags.size());
    std::vector<int> subject;
    if (!app.history_mode.selection.empty()) {
        for (int idx : app.history_mode.selection) {
            if (idx >= 0 && idx < n) subject.push_back(idx);
        }
    } else if (app.history_mode.focus >= 0 && app.history_mode.focus < n) {
        subject.push_back(app.history_mode.focus);
    }
    if (subject.empty()) return;

    // THE COLUMN, hoisted above the wall guard below because both read it:
    // the active one by construction (the lane paints only that half of a
    // delta), so the store is chosen once for the whole act.
    const bool phase = (app.active_markers_view == 'P');

    // THE PAST-EOF WALL, ahead of everything (2026-08-29): the flags' THEN
    // side is a commit's sidecar text, authored against whatever audio stood
    // at that checkpoint, so a removed flag can carry a frame past this
    // session's `total - 1` exactly as a whole checkpoint load can. THE ACT
    // REFUSES WHOLE rather than skipping the offender: a revert applies a
    // delta, and a partially applied one leaves a state the user did not ask
    // for and cannot name. THE FRAME IS THE FLAG'S OWN — the phase arm copies
    // f.time_frame into the fresh marker and the warp arm re-spells it with
    // format_authored_frame, so the parsed line lands on exactly this value —
    // which is what lets the check run before the loop parses anything.
    // The refusal is a notification card beside its stderr line (2026-08-29;
    // stderr alone until then, the view's own line having outranked the
    // transient tier it would have written). It sits ABOVE the
    // stop below because a refusal is not the act; in this mode the stop is a
    // formality either way (its own comment says why).
    {
        std::vector<GuiWarpMarker>       restored_warp;
        std::vector<GuiPhaseResetMarker> restored_phase;
        for (int idx : subject) {
            const HistoryDiffFlag& f = flags[static_cast<std::size_t>(idx)];
            if (!f.removed) continue;   // an added flag DELETES; it lands none
            if (phase) {
                GuiPhaseResetMarker nm;
                nm.time_frame = f.time_frame;
                restored_phase.push_back(nm);
            } else {
                GuiWarpMarker nm;
                nm.time_frame = f.time_frame;
                restored_warp.push_back(nm);
            }
        }
        if (auto defect =
                in_place_load_wall_defect(restored_warp, restored_phase)) {
            // AN APPENDED REASON IS LOWERCASE (the rule and its one owner
            // lowercase_initial are at notifications.h): the wall defect is a
            // sentence at its frozen producer because two consumers use it
            // WHOLE, and this seam is one of the four that append it.
            const std::string reason = lowercase_initial(*defect);
            std::fprintf(stderr,
                "warptempo_gui: Revert refused: %s\n", reason.c_str());
            notifications.notify(AppState::NotificationClass::Normal, "Revert refused: " + reason);
            return;
        }
    }

    // THE MODE'S OWN STOP-UP-FRONT REGIME, unconditional and ahead of the loop —
    // the shape its Tab cycle, its Home/End and its `c` all take, and the
    // load-in-place's reason besides (a store rewrite under a live audition).
    // NOT gated on anything this act finds: the stop is refusal-gated at its own
    // owner, and in this mode it is a formality either way — the entry owner
    // stops a session that was running before `h` and nothing in the view can
    // start one (open_history_mode_fresh; bare Space is consumed and no scrub
    // act exists here). The doc says exactly this rather than folding the stop into
    // the "only when something changed" claim below, which covers the three
    // effects that do wait on a change.
    playback_lifecycle.stop_playback_if_playing();

    // ONE SNAPSHOT FOR THE WHOLE ACT, taken before the first write — the shape
    // every multi-marker single-store mutation in the product takes (the two
    // delete-selected bodies, the two status toggles). The undo push is at the
    // tail, after the last write, so a subject that changed nothing leaves no
    // entry behind.
    std::vector<GuiWarpMarker>       warp_pre =
        phase ? std::vector<GuiWarpMarker>{} : app.warpmarkers.markers();
    std::vector<GuiPhaseResetMarker> phase_pre =
        phase ? app.phaseresetmarkers.markers()
              : std::vector<GuiPhaseResetMarker>{};
    bool changed = false;

    // THE ACT'S COINCIDENCE MEMORY, one counter per frame: how many markers at
    // that frame the act must SKIP to reach the next PRE-ACT occupant. The store
    // keeps a frame's markers contiguous and ascending, so the group is a run and
    // the skip is an offset into it. Each arm's contribution follows from what it
    // does to that run:
    //   * INSERT lands at the group's FRONT (insert_marker's lower_bound), ahead
    //     of every pre-act occupant → +1, so the markers this act restores are
    //     never a later arm's target.
    //   * REPLACE writes over the occupant it consumed, which stays where it is →
    //     +1, so the next flag at that frame takes the NEXT occupant.
    //   * DELETE erases its occupant and the rest of the run slides down into the
    //     same index → +0.
    // A no-op replace (the identical-line case) still consumes its occupant: one
    // flag is one line, and the line is spoken for whether or not a byte moved.
    std::unordered_map<int64_t, int> skip;
    // The store index of the next pre-act occupant at `frame`, or -1 when the run
    // is exhausted (which is "insert fresh" for a restore and "nothing happens"
    // for a delete). Generic over the two marker types — the two columns' stores
    // are one template and this walk is the same walk in both.
    auto next_occupant = [](const auto& mv, int64_t frame, int skip_count) {
        const int count = static_cast<int>(mv.size());
        int i = 0;
        while (i < count &&
               mv[static_cast<std::size_t>(i)].time_frame < frame) {
            ++i;
        }
        i += skip_count;
        if (i < count &&
            mv[static_cast<std::size_t>(i)].time_frame == frame) {
            return i;
        }
        return -1;
    };

    for (int idx : subject) {
        const HistoryDiffFlag& f = flags[static_cast<std::size_t>(idx)];
        if (phase) {
            const auto& mv = app.phaseresetmarkers.markers();
            int&       sk  = skip[f.time_frame];
            const int  at  = next_occupant(mv, f.time_frame, sk);
            if (!f.removed) {
                // Added only: delete the occupant. ONE flag takes ONE marker
                // away, and the one it takes is the next PRE-ACT occupant at the
                // frame — a second coincident added flag in the same subject
                // takes the one after it, and neither can take a marker a
                // removed flag in the same subject just restored.
                if (at >= 0) {
                    app.phaseresetmarkers.remove_marker(at);
                    changed = true;
                }
                continue;
            }
            GuiPhaseResetMarker nm;
            nm.time_frame = f.time_frame;
            nm.disabled   = f.then_disabled;
            // The then side's measure travels with the flag and is restored
            // with the rest of the line. Dropping it would make the compare
            // below report every measured marker as changed and then
            // overwrite the measure away.
            //
            // THE THEN SIDE IS ALL THIS ACT EVER READS, unchanged by the
            // 2026-08-22 ruling that made the phase halves PAINT their measure
            // bytes: a revert restores what the commit had, so the added half's
            // measure — which now shows in that half's label — is not a thing
            // to restore and reaches no field here.
            nm.measure    = f.then_measure;
            if (at >= 0) {
                ++sk;
                // IDENTICAL IS NOT A CHANGE — the canonical line the occupant
                // would save as against the then side's, through the ONE
                // serializer, which is the same line vocabulary the delta itself
                // is computed in (history_diff.h). The frames are equal by
                // construction, so for this column the compare is the disable
                // bit and the measure; it is spelled as the line anyway,
                // symmetrically with the warp arm below, whose payload has no
                // such shortcut.
                const auto& live = mv[static_cast<std::size_t>(at)];
                if (format_phaseresetmarkers_text({live}) ==
                    format_phaseresetmarkers_text({nm})) {
                    continue;
                }
                GuiPhaseResetMarker* m = app.phaseresetmarkers.marker_mut(at);
                if (m) *m = nm;
            } else {
                app.phaseresetmarkers.insert_marker(nm);
                ++sk;
            }
            changed = true;
            continue;
        }

        const auto& mv = app.warpmarkers.markers();
        int&       sk  = skip[f.time_frame];
        const int  at  = next_occupant(mv, f.time_frame, sk);
        if (!f.removed) {
            if (at >= 0) {
                app.warpmarkers.remove_marker(at);
                changed = true;
            }
            continue;
        }
        // The sidecar line this flag's then side came off, rebuilt: the disable
        // prefix, the canonical frame spelling (format_authored_frame, the one
        // serializer), the '|' and the verbatim payload token. The token is
        // rest-of-line, so a ` //<measure>` suffix is already inside it and the
        // rebuilt line is the sidecar's line byte for byte — which is why the
        // parse accepts measures here; refusing them would fire the
        // "unreachable" arm below on every measured marker.
        std::string line;
        if (f.then_disabled) line += '#';
        line += format_authored_frame(f.time_frame);
        line += '|';
        line += f.then_token;
        auto parsed = warpmarkers_internal::parse_single_canonical_line(
            line, /*accept_measure=*/true);
        if (!parsed) {
            // UNREACHABLE BY CONSTRUCTION and stated loudly rather than
            // recovered from: every walk member is strict-load clean, so the
            // token was sliced out of a line this very parser accepted and the
            // frame re-spells canonically. One line, then on to the next flag —
            // there is nothing to repair and nothing partial to undo.
            std::fprintf(stderr,
                "warptempo_gui: Revert skipped a warp line the parser refused: "
                "'%s'\n", line.c_str());
            continue;
        }
        // A FRESH GuiWarpMarker, not a patch of the occupant: the then side is a
        // different marker, so it arrives with the session-only iteration and
        // bpm scratch at its defaults exactly as a load would deliver it. (The
        // at-most-one-bpm-owner invariant survives trivially — this never sets
        // the bit.)
        GuiWarpMarker nm;
        static_cast<WarpMarker&>(nm) = *parsed;
        if (at >= 0) {
            ++sk;
            // IDENTICAL IS NOT A CHANGE, the phase arm's rule in the column that
            // needs it: the occupant's canonical line against the then side's,
            // both through format_warpmarkers_text, so the compare reads exactly
            // the eight serialized fields — the measure included, which is
            // content — and IGNORES the session-only iter/bpm
            // scratch — which is also why a no-op replace leaves that scratch
            // standing instead of resetting it to a fresh marker's defaults.
            const auto& live = mv[static_cast<std::size_t>(at)];
            if (format_warpmarkers_text({live}) ==
                format_warpmarkers_text({nm})) {
                continue;
            }
            GuiWarpMarker* m = app.warpmarkers.marker_mut(at);
            if (m) *m = nm;
        } else {
            app.warpmarkers.insert_marker(std::move(nm));
            ++sk;
        }
        changed = true;
    }

    // THE THREE EFFECTS THAT WAIT ON A CHANGE, and `changed` is "the state
    // DIFFERED", not "a store call happened": a subject whose every member found
    // the live state already carrying its then side leaves no undo entry, no
    // dirty bit and no re-render behind, exactly as one that found nothing at all
    // does. (The stop above is the one effect that does not wait — its own
    // comment says why.)
    if (changed) {
        // THE LIVE SELECTION GOES, the wholesale-store-change convention (the
        // load-in-place's own line, and the deletes'): it is a set of STORE
        // indices, and this act inserts and removes under them.
        selection.clear_selection();
        if (phase) undo.push_undo_phase_reset(std::move(phase_pre));
        else       undo.push_undo_warp(std::move(warp_pre));
        undo.recompute_dirty();
        target_render.trigger();
    }

    // THEN THE VIEW CLOSES, and the order is the whole reasoning: this act has
    // just rewritten the very state the session's frozen now side was measured
    // against, so every flag in the lane now describes a state that no longer
    // exists — the load-in-place's own argument, applied to a narrower write.
    // Closing AFTER the mutations is what lets the exit owner's entry-band
    // restore run over the finished state, and it is why the act needs no damage
    // of its own: close_history_mode invalidates the window whole.
    //
    // IT CLOSES EVEN WHEN NOTHING CHANGED — a subject of added flags with no
    // live markers under them, the always-force rule's own quiet case. The act
    // ran and answered; leaving the view standing would say it had not.
    close_history_mode();
}

// THE OPEN DROPDOWN'S keyboard gate — ONE gate for EVERY menu, because there is
// one popup state and a dropdown is a dropdown (the Navigation menu joined
// 2026-08-02, the File one 2026-08-13 and the Edit one 2026-08-20, each needing
// nothing here, and the
// Navigation menu's 2026-08-15 deletion needed nothing either: the popup's bare
// Esc is ONE bare-Esc binding through all of it, never two). Returns true when the press is SWALLOWED (the
// popup consumed it, or it was inert); false only for Ctrl+Q, which closes the
// popup and then lets on_key run the close route.
//
// Bare-exact and ctrl-exact respectively, like every other modal predicate here:
// a modified Escape and a shifted Ctrl+Q carry no binding anywhere, so they fall
// into the swallow with everything else rather than dismissing. (CTRL+ESC, the
// notification stack's bulk clear since 2026-09-01, is the exception that never
// arrives: on_key claims it at its head, above this gate, so a popup neither
// swallows it nor closes on it.)
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

// The DIALOG-HOSTED modal editors: the settings editor, the bpm editor
// (top_flag_editor reused with Kind::BpmBracket), the history view's
// commit-title editor (2026-08-07) and the measure paste-offset editor
// (2026-08-20) — the four surfaces painting in THE BOTTOM ROW'S MODAL since
// 2026-08-13 (a centered modal dialog for the one day from 2026-08-12, and
// the bottom strip before that, whence the predicate's old
// modal_bottom_strip_editor_active name). The LOAD editor stood among them
// until 2026-08-28, when it became the field-less picker, which is a modal
// owner and not an editor — the membership is AppState::dialog_editor_session's
// and nothing here restates it. Plus the prompts, which own input through
// their own gates in on_key and the pointer handlers. Since the flag editor
// became keyboard-modal this is NO LONGER the keyboard gate's predicate (that
// is keyboard_modal_editor_active); what it names is the POINTER-facing
// behaviors the top-strip FlagPayload editor is deliberately transparent to —
// the caller roster (re-grepped 2026-08-29 — NINE calling functions over ten
// call sites, on_button_press's own veil joining that day when it stopped
// spelling the four is_active tests inline, and the modal-trap block that used
// to stand among them deleted)
// is
// the declaration's, in input_handler.h. The playback stop is
// NOT here: it has its own owner (stop_playback_for_modal_open) that the open
// sites call. Authoritative statement at the declaration in input_handler.h.
bool GuiInputHandler::modal_dialog_editor_active() const {
    // The four are NAMED at AppState::dialog_editor_session, which hands back
    // the live one's session id — one membership serving both questions.
    return app.dialog_editor_session() != 0;
}

// Any text editor consuming printable keys — the THREE single-State dialog
// editors (the settings prompt, the commit-title editor and the measure
// paste-offset editor) plus the top-strip flag editor in ANY of its three
// kinds (the FlagPayload editor takes typed letters too); the six Kinds are
// listed at text_editor::Kind. The platform layer's kLeftClickKey probe: while
// this is true that key types a normal letter rather than emulating the left
// button.
bool GuiInputHandler::any_text_editor_active() const {
    return text_editor::is_active(app.settings_editor) ||
           text_editor::is_active(app.commit_title_editor) ||
           text_editor::is_active(app.measure_offset_editor) ||
           text_editor::is_active(app.top_flag_editor);
}

// Keyboard modality — see the declaration for the readers and for why the
// wheel and playback-stop readers deliberately keep the dialog predicate.
// It is EXACTLY any_text_editor_active, and that identity is structural rather
// than coincidental: an editor that swallows printable letters MUST own the
// keyboard, or typing `f` into a flag would toggle follow mode. So this
// delegates instead of restating the membership — one expression, two names,
// and the two concepts can only ever be the same set.
bool GuiInputHandler::keyboard_modal_editor_active() const {
    return any_text_editor_active();
}

namespace {

// THE MODAL FOCUS RING'S TAB SHAPE — the ONE predicate four sites read (the
// keyboard-modal gate's admission, the ring's own walk, the completion arm
// that must fire on the FORWARD shape alone, and repeat_eligible's ring arm
// just below), so the spellings cannot drift apart the way hand-kept lists
// would.
//
// IT MIRRORS THE LIVE MARKER CYCLE'S SPELLINGS EXACTLY (`is_tab_cycle` and
// handle_tab_switch_keys' three arms, both in this file) rather than inventing
// a second convention for the same physical gesture: bare Tab forward,
// Shift+Tab back, and IsoLeftTab back SHIFT-AGNOSTICALLY — that keysym lives on
// the Tab key's shift level, so a layout may deliver the reverse either with or
// without the shift bit and the product reads both as the one shape everywhere
// it binds a reverse Tab (the `h` view's diff-flag cycle does the same). CTRL
// AND ALT ARE REFUSED on every arm, again as the live cycle refuses them: a
// ctrl-carrying Tab is the tab-cycle family's, not this ring's, and alt binds
// nothing in it.
enum class ModalRingTab { None, Forward, Reverse };

ModalRingTab modal_ring_tab_shape(GuiKey key, GuiInputState mods) {
    if (mods.ctrl || mods.alt) return ModalRingTab::None;
    if (key == GuiKeys::IsoLeftTab) return ModalRingTab::Reverse;
    if (key != GuiKeys::Tab) return ModalRingTab::None;
    return mods.shift ? ModalRingTab::Reverse : ModalRingTab::Forward;
}

// THE RING'S KEY NORMALIZATION — KEYPAD ENTER IS RETURN'S TWIN (2026-08-29),
// the one owner, read by the ring's walk at its head and by the release arm
// (GuiInputHandler::on_key_release) before its compare. The pair is one key
// everywhere else in the product — the editors' session keys
// (text_editor::classify_key), the flag editor's open chord, and the picker's
// and the player's list routers, which both spell `case Return: case KpEnter:`
// — and a ring that read Return alone made the difference depend on nothing
// but where the focus happened to be, letting a keypad Enter on a focused
// button reach the LIST instead. Normalizing at both ends is what keeps a
// press stored as Return answerable by the KpEnter release that ends it.
// Every other key passes through unchanged.
GuiKey modal_ring_press_key(GuiKey key) {
    return key == GuiKeys::KpEnter ? GuiKeys::Return : key;
}

}  // namespace

// Press-time key-repeat eligibility (see the declaration). Repeat serves
// held-step gestures and editor typing; edge-triggered commands never repeat.
// Eligibility is judged under the PRESS-TIME context, so a press that opens an
// editor (evaluated before the open) does not arm, while typing inside an
// already-open editor does.
bool GuiInputHandler::repeat_eligible(GuiKey key, GuiInputState mods) const {
    // A press a live pointer gesture would swallow must not arm: its owning
    // context rejected the press, and the gate lifting later must not
    // retroactively empower the hold (e.g. a chord held through a marker drag
    // must not repeat onto the just-committed marker once the mouse releases).
    if (any_pointer_gesture_active(app)) return false;
    // THE MODAL FOCUS RING'S TAB WALK REPEATS (architect 2026-08-13, at his
    // live test: "a held tab does not key repeat. It should"), and it is the
    // ONE thing that repeats on a modal surface. It is a continuous step
    // gesture in exactly the sense the marker cycle is — walk the stops
    // quickly — and its shapes are that cycle's own, read from the one
    // predicate the gate, the walk and the completion arm share rather than
    // re-spelled here.
    //
    // THE TWO SURFACES ARE THE TWO THAT HAVE A RING: a prompt (whose buttons
    // cycle) and a DIALOG editor (field plus buttons). The top-strip flag
    // editor publishes no dialog and so has no ring; its whole Tab family
    // drops at modal_editor_key_blocked before anything could act on it, and
    // it is deliberately absent here.
    //
    // A REPEATING WALK IS COHERENT WITH THE ONE AUTOCOMPLETE MODEL
    // (route_modal_editor_key): the first fire in a field with a live
    // completion completes and is consumed, and no later fire can complete
    // again — the buffer is already AT its completion, so the advancement test
    // answers false and every subsequent fire walks. The completion cannot
    // fire repeatedly under a hold for that reason alone; nothing here has to
    // suppress it.
    if ((app.prompt.active || modal_dialog_editor_active()) &&
        modal_ring_tab_shape(key, mods) != ModalRingTab::None)
        return true;
    // AND THE WALK IS THE ONLY THING THAT REPEATS ONCE THE FOCUS IS ON A BUTTON
    // (2026-08-13): from there no key reaches the field at all
    // (route_modal_editor_key's wall), so there is nothing left that a hold
    // could usefully continue — and the two that DO act there, bare Enter and
    // bare Space, are a press-and-hold whose act is at the physical release
    // (AppState::modal_dialog_key_pressed). A repeat could only re-press or
    // re-fire them, which is exactly what must not happen. One arm covers both
    // and everything else, derived from the wall rather than listing keys —
    // through the wall's own reading of the focus (modal_dialog_focus_live),
    // so the two cannot disagree about where the keyboard is.
    if ((app.prompt.active || modal_dialog_editor_active()) &&
        modal_dialog_focus_live() >= 0)
        return false;
    // THE RENDER PLAYER (2026-08-28): its continuous steps repeat — the
    // highlight walk (Up/Down) and the seeks (Left/Right) — AND NOTHING ELSE
    // DOES, on any modifier. Enter and Space are the modal's one-shot press or
    // the open/play acts; the closers and the load chord are
    // one-shot commands; HOME AND END ARE ABSOLUTE (architect 2026-08-31, and
    // that is why the two skips' keys are one-shot on BOTH shapes: the plain
    // pair lands the item's start or its end — or, inside Home's
    // previous-track window, a whole file back, which a hold would then walk
    // through the folder a file per repeat — and the SHIFTED pair lands the
    // folder's first and last wav, where a hold could only re-reach the wall
    // it just hit); and the ring's Tab repeats through this arm's own first
    // line exactly as every ring's does. The item's two NEIGHBOURS were
    // repeat-eligible on bare `,` / `.` with their shifted ends from
    // 2026-08-30 to 2026-08-31 and left the mode with them. A prompt over the
    // player is the prompt's own answer, which the arm above and the blanket
    // below already give.
    if (app.render_player.active && !app.prompt.active) {
        if (modal_ring_tab_shape(key, mods) != ModalRingTab::None) return true;
        if (mods.ctrl || mods.alt || mods.shift) return false;
        return key == GuiKeys::Left || key == GuiKeys::Right ||
               key == GuiKeys::Up || key == GuiKeys::Down;
    }
    // THE PICKER (2026-08-28), the player's shape with the player's reason:
    // the highlight walk (Up/Down) repeats — a continuous step that decides
    // nothing but where the band is — and the ring's Tab repeats as every
    // ring's does; Enter, Space and Esc are one-shot, the open act and the
    // close being acts and the button press being the modal's own
    // press-and-hold. A prompt over the picker is the prompt's own answer.
    if (app.picker.active && !app.prompt.active) {
        if (modal_ring_tab_shape(key, mods) != ModalRingTab::None) return true;
        return !mods.ctrl && !mods.shift && !mods.alt &&
               (key == GuiKeys::Up || key == GuiKeys::Down);
    }
    // EVERY OTHER KEY IS REFUSED OUTRIGHT WHILE A PROMPT STANDS, and that
    // blanket stays exactly as it was: a prompt's one-key answers must be
    // one-shot, because a held response key repeating is the destructive shape
    // the painted gate (PromptState::painted) exists to prevent — the second
    // fire would answer a question raised by the first. The ring's walk is
    // safe under a hold precisely because it decides nothing; it only moves
    // where the keyboard is.
    if (app.prompt.active) return false;
    if (any_text_editor_active()) {
        // Only the editor's motion/edit and printable-insert keys auto-repeat
        // while held; its session (bare Escape/Return) and chord (ctrl-exact
        // A/C/X/V) keys are one-shot, and NotEditorKey is not the editor's to
        // repeat — the keyboard-modal gate drops it before anything could act on
        // it anyway. An ALT-carrying motion press is in that last bucket and so
        // does not repeat, which falls out of the classifier rather than being
        // spelled here. This consumes the one editor-key owner.
        //
        // THE TAB FAMILY NEVER REACHES THIS ARM: it classifies NotEditorKey
        // (handle_key never consumes a Tab — the ring and the completions are
        // route_modal_editor_key's, above handle_key in that route, not the
        // editor's own keymap), and a DIALOG editor's ring already answered it
        // in the arm above this one. What falls through to here is a Tab under
        // the FLAG editor, which has no ring and does not repeat.
        const auto kc = text_editor::classify_key(key, mods);
        return kc == text_editor::KeyClass::MotionEditKey ||
               kc == text_editor::KeyClass::PrintableKey;
    }
    // Global dispatch: only the continuous step gestures repeat — the
    // ARROWS all four (Left/Right being the playhead step in the waveform lane
    // and the position nudge in the marker lane, Up/Down the
    // tempo cent step; the lane split is decided per fire at dispatch, so the
    // arrows repeat as one family — and since 2026-08-31 they repeat on their
    // shifted and ctrl spellings too, which the arm below this one owns),
    // bare PageUp/PageDown, bare Equal/Minus (the
    // WAVEFORM MAGNIFICATION step since 2026-08-27, the horizontal zoom's own
    // eligibility inherited whole when the two acts swapped modifiers),
    // THE WALK'S BARE COMMA/PERIOD (2026-08-07 — the `h` history view's
    // older/newer step, a continuous step gesture like the arrows and held for
    // the same reason, to walk quickly; in GLOBAL dispatch it is bound only
    // inside that view, so a repeat outside it fires into an unbound key
    // exactly as a held arrow with nothing to nudge fires into a refusal — the
    // render player bound the same pair to its own item walk for the one day
    // of 2026-08-30 and answered for it in the player arm above, which returns
    // before this one whatever that mode binds. Their SHIFT shapes — the
    // walk's absolute wall jumps — are excluded
    // by the no-shift term below and stay one-shot: a held jump could only
    // flap against the wall it just reached),
    // the marker-focus cycle (bare Tab / Shift+Tab / IsoLeftTab), and the FIVE
    // repeating Ctrl chords — the Ctrl+Shift+Tab march, Ctrl+Z / Ctrl+Shift+Z
    // (undo / redo), and Ctrl+= / Ctrl+- (the horizontal ZOOM step, which is
    // what that chord spells since 2026-08-27),
    // each a continuous step gesture like the cycle, not a
    // one-shot command. THE MARCH REPEATS IN EVERY STATE since 2026-08-18, its
    // mode scope having gone with the reason for it: the chord is the march
    // inside the `h` history view too, over the diff-flag cycle instead of the
    // live one, so a hold continues exactly the gesture it continues outside —
    // the same eligibility the bare cycle already inherits in there. (It was
    // the mode's reverse walk cycle from 2026-08-07, which a hold could only
    // flap, and this line excluded the view for that.) Ctrl+Tab stays one-shot
    // everywhere, in the view as out of it, a held A/B switch being able only
    // to flap. Every
    // letter, toggle, opener, other Ctrl / Ctrl+Alt chord, Space in BOTH of
    // its forms (bare, and Shift+Space the A/B audition — a held one would
    // only meet the running-sequence refusal, so no repeat is owed), Home/End
    // in BOTH of its forms (bare and the 2026-08-24 ctrl whole-piece jump),
    // and Delete is one-shot.
    if (!mods.ctrl && !mods.shift && !mods.alt &&
        (key == GuiKeys::PageUp || key == GuiKeys::PageDown ||
         key == GuiKeys::Equal || key == GuiKeys::Minus ||
         key == GuiKeys::Comma || key == GuiKeys::Period))
        return true;
    // THE FOUR ARROWS REPEAT IN ALL THREE MAGNITUDES (architect 2026-08-31,
    // R12): a HELD REPEAT CARRIES ITS MODIFIER, so a held Shift+Right walks
    // three columns a fire and a held Ctrl+Up ten cents a fire — the burst
    // continues the gesture the press began, which is the whole meaning of a
    // hold. They are one family however the press is spelled (the lane split
    // is decided per fire at dispatch, and the magnitude with it), so the arm
    // is their own rather than a term in the bare list above: CTRL+SHIFT is
    // excluded because it spells nothing to repeat, and ALT with it.
    if (!mods.alt && !(mods.ctrl && mods.shift) &&
        (key == GuiKeys::Left || key == GuiKeys::Right ||
         key == GuiKeys::Up || key == GuiKeys::Down))
        return true;
    // Marker-focus cycle keys auto-advance while held (fast marker walking):
    // bare Tab and Shift+Tab both cycle, and IsoLeftTab cycles shift-agnostic
    // (mirroring the dispatch arm), all requiring no ctrl/alt. The `h` history
    // mode's diff-flag cycle takes the same three shapes and inherits this line
    // unchanged, which is the eligibility it wants: fast walking of the flags
    // instead of the markers. CTRL+TAB is excluded by the
    // same no-ctrl term, which is the eligibility it wants in every state — a
    // held A/B switch would only flap — and the term catches ctrl+shift+
    // IsoLeftTab with it (the Ctrl+Shift+Tab spelling needs the arm below).
    if (!mods.ctrl && !mods.alt &&
        (key == GuiKeys::Tab || key == GuiKeys::IsoLeftTab))
        return true;
    // Ctrl+Shift+Tab exactly (the lockstep march) repeats too, IN EVERY STATE
    // since 2026-08-18: the mode-scoped term this line carried from 2026-08-07
    // is deleted with the thing it was about — the chord was the view's reverse
    // WALK cycle in there, which a hold could only flap, and it is the march
    // itself in there now, marching the diff-flag cycle instead of the live one.
    // Ctrl+Tab needs no term of its own — the no-ctrl arms above exclude it in
    // every state — and ctrl+shift+IsoLeftTab is excluded by those same arms'
    // no-ctrl term, the march binding the plain Tab spelling alone.
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
    // Ctrl+= / Ctrl+- (the horizontal ZOOM step since 2026-08-27) repeat
    // while held, for the reason the BARE pair on those same two keys does:
    // stepping a ladder is a continuous step gesture, and a held key walking
    // it is how a passage is brought to the span the eye wants in one
    // press-and-hold. Ctrl-exact, matching the dispatch arms; the shifted
    // shapes bind nothing and stay one-shot by this term. THE TWO ZOOM
    // ICON-ROW BUTTONS DO NOT REPEAT AT ALL and never asked this: they carry
    // no `repeats` in kToolbarChords (input_pointer.cpp), which is the chrome
    // hold's whole membership, so a held zoom button is one step at its lift
    // like every other non-repeating button. The MAGNIFICATION pair's buttons
    // DO carry it, and what they ask this predicate about is the BARE
    // spelling of these same two keys, answered by the arrows' term above —
    // so on that pair the key and the button walk at one speed by
    // construction.
    //
    // CTRL+0 IS DELIBERATELY ABSENT because it binds nothing at all since
    // 2026-08-27: the magnification reset that wore it for one day retired with
    // its button, and an unbound chord has no repeat to ask about.
    if (mods.ctrl && !mods.shift && !mods.alt &&
        (key == GuiKeys::Equal || key == GuiKeys::Minus))
        return true;
    return false;
}

// The KEYBOARD-MODAL editor key gate, the sibling of read_only_key_blocked's
// allowlist shape. True when key+mods is not on the allowlist and should be
// dropped. It serves ALL SIX editor kinds — the settings prompt,
// the commit-title editor (2026-08-07), the bpm bracket, the MARKER MEASURE
// editor (2026-08-19), the MEASURE PASTE-OFFSET editor (2026-08-20) and
// (architect 2026-07-28) the top-strip flag editor, which this ruling brought
// under the same contract. While one is open the user can
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
// modifier its arm does not bind (Ctrl+Enter, Ctrl+Shift+V,
// Ctrl+Alt+A, Alt+Left, Ctrl+Alt+BackSpace) is NotEditorKey like any other
// unbound chord and drops right here, so it cannot cancel, commit, paste, move
// the caret, or erase. The gate-level
// carve-outs below are NOT editor consumption — they are gate policy layered on
// top: bare Tab under any DIALOG editor (the focus ring's step and, where the
// editor has one, its autocomplete — the fork is
// route_modal_editor_key's, not this gate's), Ctrl+S (save), and
// Ctrl+Q (close routing). Admitted keys flow into the editor routing unchanged,
// so the only NotConsumed keys that can reach route_modal_editor_key's command
// tail are those last two chords.
// THE ADMITTED SET HAS GROWN TWICE since the dialog arc, both times by the
// TAB FAMILY: bare Tab, from "the settings and load editors only" to "any
// dialog editor", when the focus ring landed 2026-08-13; and THE RING'S
// REVERSE WALK the same day (architect: "also, shift+tab should cycle
// backward"), which admits Shift+Tab and the IsoLeftTab keysym in the live
// marker cycle's own spellings — one predicate, modal_ring_tab_shape above,
// read here and by the ring alike. (The Open project prompt's bare Up / Down
// were a third growth for one afternoon on 2026-08-28, scoped to that one
// editor's picker band, and left when the prompt lost its field: the picker
// is a modal owner with its own router now, not an editor this gate serves.)
// (WHAT A TAB DOES inside the set was re-ruled
// twice more that day and settled on the one autocomplete model — completion
// first, ring if it did not advance, stated at route_modal_editor_key — without
// moving the admission either time.) Nothing else about
// the contract moved — Left / Right /
// Enter were always admitted as editor keys
// and still are, and what the ring does with them is decided downstream, on a
// focus state that did not exist before.
bool GuiInputHandler::modal_editor_key_blocked(GuiKey key,
                                               GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    const bool is_editor_key =
        (text_editor::classify_key(key, mods) !=
         text_editor::KeyClass::NotEditorKey);
    // THE RING'S WHOLE TAB FAMILY, admitted while any DIALOG editor stands
    // (2026-08-13) — a superset of what this gate admitted before: the one
    // editor that HAS a completion, SETTINGS (its value recall), keeps the
    // FORWARD key as its first meaning, while the commit-title, BPM and
    // measure-offset editors let it walk the ring from the first press
    // (route_modal_editor_key owns which of the two a given forward Tab is,
    // under the one autocomplete model), and the REVERSE shapes walk
    // backwards for all four, completing nothing anywhere. The top-strip FLAG
    // editor is deliberately outside it: it is not a dialog, publishes no
    // buttons, and so has no ring for Tab to walk — its whole Tab family still
    // drops here while it stands.
    const bool is_dialog_focus_tab =
        (modal_dialog_editor_active() &&
         modal_ring_tab_shape(key, mods) != ModalRingTab::None);
    const bool is_save =
        (ctrl && !shift && !alt && key == GuiKeys::S);
    const bool is_ctrl_q =
        (ctrl && !shift && !alt && key == GuiKeys::Q);
    return !(is_editor_key || is_dialog_focus_tab || is_save || is_ctrl_q);
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
// `<source parent>/tmp/<N>_iterations/`, one cell per product point with
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
                // ONE COMPOSER, TWO SURFACES (architect 2026-08-30): the
                // stderr line keeps the marker's index, which is the
                // evidence; the card says the fact, which is the answer to
                // the press. The card names no index because the sweep's
                // whole dispatch refused, not one cell of it.
                std::fprintf(stderr,
                    "warptempo_gui: render-iterations refused: marker %d "
                    "iter bracket start exceeds end\n", i);
                notifications.notify(
                    AppState::NotificationClass::Normal,
                    "A marker's iteration bracket runs backwards");
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
        // AND IT SAYS SO (architect 2026-08-30): with iteration mode on,
        // Ctrl+Alt+R IS the sweep, so a chord that renders nothing at all
        // must not look like a chord that rendered silently. The Render
        // button wears the mode's own "Render grid iterations" face here and
        // dispatches this same chord, so the one card answers both roads.
        std::fprintf(stderr,
            "warptempo_gui: render-iterations: No iter ranges "
            "authored; nothing to render\n");
        notifications.notify(AppState::NotificationClass::Normal,
                             "No iteration ranges are authored");
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
        //
        // A NORMAL CARD (architect 2026-08-30): a refusal that answers an act
        // the user just gave is an EVENT, and events are cards
        // (messaging.md's split). It wore the dismiss-only ERROR_NOTICE modal
        // until the cards landed — the pre-split surface for a sentence the
        // user had to be shown — and that prompt kind retired whole with this
        // move, its other caller (the target-view entry gate) having become a
        // silent one. The sentence was unchanged by that move; the 2026-08-31
        // rebrand renamed its subject alone ("Iteration sweep" -> the menu
        // row's "Grid iterations", kSeriesPopupItems).
        //
        // ONE CLAUSE, ONE NUMBER, NO PERIOD (architect 2026-09-01, the
        // capitalization sweep's sentence-shape rule — messaging.md's card
        // section). It was the product's only TWO-SENTENCE card, said the cap
        // twice and closed with an instruction ("more than N cells (cap N).
        // Narrow the marker brackets and retry."), which was the retired
        // dismiss-only
        // modal's shape rather than a card's: the fix is implied by the reason
        // and HELP already tells the user to narrow the brackets.
        notifications.notify(
            AppState::NotificationClass::Normal,
            "Grid iterations refused: the marker brackets make more than " +
            std::to_string(kMaxIterSweepCells) + " cells");
        return;
    }

    const std::filesystem::path queue_root =
        project_batch_root(app.source_audio_path);

    // Resolve the next batch index: max+1 over `<digits>_<anything>`
    // entries (the shared batch-folder scan).
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
        // ONE COMPOSER FOR THE CARD, shared with the two other batch folders
        // a render chord can fail to create (render_folder_creation_card,
        // renders_dir.h — the basename rule is its own). The stderr line
        // keeps the WHOLE path, which is what a terminal is for.
        const std::string why = ec.message();
        std::fprintf(stderr,
            "warptempo_gui: render-iterations: Could not create "
            "'%s': %s\n",
            batch_folder.string().c_str(), why.c_str());
        notifications.notify(
            AppState::NotificationClass::Normal,
            render_folder_creation_card(batch_folder, why));
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

    // The batch's DISPLAY label — the progress line's counted noun and the
    // stderr summary's tag. "grid iterations" since the 2026-08-31 rebrand,
    // which is the menu row's own name for this mode (kSeriesPopupItems):
    // the GUI line reads "Rendering 3 of 8 grid iterations...", the label
    // naming what the two numerals count, and "render" fell out of it
    // because the sentence already leads with "Rendering". (It was plain
    // "iterations" from 2026-08-29, when the architect made the label the
    // counted noun, and "render" before that.) It falls mid-sentence in both
    // surfaces and so is lowercase by the sentence-case rule itself, no
    // exception to anything (the product's text rules are stated once at
    // paint_handler.cpp's menu-row block); the BPM batch's twin label
    // capitalizes its acronym and lowercases its noun for the same reason.
    if (async_renderer.is_busy()) {
        // A render dispatch kills the running render. Park the fully
        // built batch for the worker-idle pump.
        AppState::PendingArchivalCommand cmd;
        cmd.reqs        = std::move(reqs);
        cmd.batch_label = "grid iterations";
        kill_running_render_and_park(std::move(cmd));
    } else {
        start_render_batch(std::move(reqs), "grid iterations");
    }
    // The sweep is committed to run either way (dispatched, or parked
    // behind the killed render's drain): iteration mode turns off after
    // fire, and exiting the mode IS the bracket clear (wipe_iter_state,
    // the chokepoint every other iter-mode exit runs). Safe here: every
    // request above carries its own per-cell marker copies, so nothing
    // dispatched reads the live iter fields.
    //
    // IN TARGET VIEW THIS TAIL IS A GRANTED HOME-VIEW-BINDING EXCEPTION
    // (architect 2026-08-07, with the ruling that iteration mode is
    // TARGET-LEGAL — the sweep dispatches from either audio view now). The
    // wipe writes the WARP store and pushes an undo entry, which off warp's
    // home view the binding would otherwise refuse; it was what gated this
    // whole command to source view (bdf4336, 2026-07-22), and that gate is
    // gone. The exception is admitted on the CENT STEP'S OWN CLASS of
    // argument, and narrower: iter brackets are SESSION-ONLY fields — never
    // serialized (no sidecar key), excluded from the render recipe, and
    // pushed with affects_persistence=false — so the write can reach neither
    // disk nor a render, and the state it clears is the very state this
    // command just consumed. Every other route out of the mode already runs
    // this same clear, so nothing about the mode's lifecycle changed with the
    // view.
    flag_editor.wipe_iter_state();
    app.iteration_mode_enabled = false;
    viewport.invalidate_top_strip();
    // (A SECOND DAMAGE CALL STOOD HERE for the one day the STATUS BAR did,
    // 2026-08-29: the mode bit is one of the eligibility terms the resolved
    // READOUT read, so clearing it could bring that cell back. The readout
    // retired with the bar and nothing outside the top strip changes here.)
}

// Render-trigger chords. See the declaration for the chord list.
void GuiInputHandler::card_trim_fallback_if_any() {
    // THE ARCHIVAL HALF OF ITEM L (architect 2026-09-02). do_render prints the
    // "…; rendering untrimmed" line from the WORKER thread, which is the
    // engineering log and nothing the user sees: the deliverable comes out the
    // full length of the piece while the trim bar goes on painting the hairline
    // window that produced it. The strictness ruling's test is what shows, and
    // what shows here would mislead — so the press answers with a sentence.
    //
    // AT THE COMMAND, NOT AT THE DISPATCH. Both chords may PARK behind a
    // running render (kill_running_render_and_park) and reach
    // dispatch_single_archival_render only when the worker drains; carding at
    // the dispatcher would therefore fire once for a direct press and once
    // more for the parked one, and a park that is later replaced by a newer
    // command would card for a render that never ran. One press, one card, at
    // the site that reads the live trim to build the request.
    //
    // ONE CARD PER PRESS, NO EDGE — the deliberate-press rule (notifications.h):
    // a second Ctrl+Alt+R under the same tiny window is a second explicit
    // command producing a second untrimmed deliverable, and it gets its own
    // card. The PREVIEW's edge gate exists because its dispatches are not
    // presses at all (last_dispatch_trim_fallback_, target_render.h — the edge
    // scoped to the tab and trim pair that produced the verdict).
    //
    // THE TWO SWEEPS ARE NOT CARDED, and the asymmetry is deliberate rather
    // than an omission. A sweep cell rewrites the warp markers per cell, so the
    // map its plan_trim consults is NOT the live one this verdict reads: at
    // tempo x scale near the threshold some cells fall back and some do not,
    // and a card raised from the live state would be an assertion about cells
    // it cannot see — the truthfulness rule refuses that more firmly than it
    // asks for the card. The honest alternative is a per-cell verdict, which is
    // the worker's, and that is N cards for one act. The sweeps keep
    // do_render's per-cell stderr line; their output is disposable `tmp/` batch
    // material the architect auditions in the render player, where the length
    // of the cell is the answer.
    if (target_render.trim_would_fall_back()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kTrimFallbackCard);
    }
}

bool GuiInputHandler::handle_render_dispatch_keys(GuiKey key,
                                                  GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    // Ctrl+Alt+R: single render into the project's `render/` folder using
    // `title` from settings. Empty batch_folder/batch_basename selects the
    // deliverable naming convention inside do_render, which creates that
    // folder if it is missing. A successful deliverable publish emits
    // the .fingerprint sidecar, but not batch-only sidecars
    // (.warpmarkers / .phaseresetmarkers / .settings).
    // Title-not-set is a hard error surfaced from do_render.
    //
    // ONE MODE BIT RE-AIMS THIS CHORD, the iteration one. THE HISTORY MODE HAD
    // THE OUTER CLAIM FROM 2026-08-04 TO 2026-08-08 and no longer does: the
    // save-and-commit act moved onto Ctrl+S, the chord the act's own first step
    // already is (architect 2026-08-08), so this route is a render again in
    // every mode and the history view simply never reaches it — neither render
    // chord is on that view's allowlist, which is also where the Render button's
    // disabled face in there comes from.
    //
    // ITERATION MODE RE-AIMS IT (architect 2026-08-02): with that mode
    // on, Ctrl+Alt+R IS the iteration sweep — the same body, the same output
    // under tmp/, the same refusals — and there is no second chord for it.
    // The single render below is the mode-off meaning, unchanged. THE
    // SWEEP DISPATCHES FROM EITHER AUDIO VIEW since 2026-08-07 (the mode is
    // TARGET-LEGAL): the bit alone selects the command, and target view needs
    // no clause of its own for the opposite reason it needed none before — the
    // mode can now REST in target, and the arm below fires there. This is also
    // what keeps the Render button honest, its "Render grid iterations" face
    // following the same bit from either view. The sweep's own body carries no
    // view assumption (it builds per-cell marker copies off the warp store and
    // renders are view-independent); its success-tail wipe is the granted
    // home-view-binding exception recorded at run_iteration_sweep_render.
    if (ctrl && alt && !shift &&
        key == GuiKeys::R) {
        if (app.source_audio_path.empty()) return true;
        if (app.iteration_mode_enabled) {
            run_iteration_sweep_render();
            return true;
        }

        // Dispatch validates nothing: the render worker's own resolve->build
        // chain is the tripwire surface (the resolver normalizes ambiguous
        // marker arrangements to tempo 1.00, and trim never refuses — crossed
        // cannot rest, an ambiguous trim falls back to untrimmed inside
        // do_render, maps ignore trim), with its stderr as the backstop.

        // Empty batch_folder/basename selects the deliverable naming
        // convention inside do_render.
        RenderRequest req = build_render_request(
            app.source_audio_path, app.warpmarkers.markers(),
            app.phaseresetmarkers.markers(), app.engine_settings,
            app.trim.begin_frame, app.trim.end_frame);
        req.authoring = snapshot_current_authoring_state();
        attach_shared_render_resources(req);

        // The deliverable this press produces may be the WHOLE piece under a
        // hairline trim bar; say so once, here (definition above).
        card_trim_fallback_if_any();

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
    // under tmp/. This moved off its former `e`-based chord because `e` is
    // now the click key (kLeftClickKey), so an e-chord is swallowed at the
    // platform boundary and can never reach dispatch as a command.
    // This is Ctrl+Alt+R with an extra mkdir and a different output
    // location: no queue, no batch runner, one request through the same
    // single-dispatch path. Folder logic (in allocate_miscellaneous_cell):
    // look at the most-recent folder BY INDEX in tmp/; if it is a
    // `_miscellaneous` folder, append into it; otherwise (or tmp/
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
    // render this command kills can still publish into tmp/ during its
    // cancellation drain (after any command-time scan but before the cancel
    // flag lands, through do_render's reuse-rung renames), so a cell name
    // scanned at command time could be stolen and then overwritten — two
    // successful publications collapsing to one pathname. Allocating only
    // once the worker is confirmed idle makes the scan exact: idle drains the
    // whole CompletionPending interval, so worker publication is fully done
    // before the scan, and every other tmp/ mutation (batch-folder
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
    // WHILE THE HISTORY MODE STANDS THIS CHORD NEVER ARRIVES, and since
    // 2026-08-08 neither does its unshifted twin — the mode's keyboard allowlist
    // admits NEITHER render chord, the checkpoint act having moved onto Ctrl+S —
    // so the press is consumed a gate above and this arm is not reached from
    // either surface. The Render button is simply dead in there, hint and all,
    // which is the derived partition's own answer.
    if (ctrl && alt && shift &&
        key == GuiKeys::R) {
        if (app.source_audio_path.empty()) return true;
        if (app.iteration_mode_enabled) {
            // AND THE REFUSAL SAYS SO (architect 2026-08-30). It stays where
            // it is — inside the route, so the keyboard press and the Render
            // button's shift press are one refusal — and the card rides with
            // it for the same reason: one press, one answer, whichever
            // surface asked. The button's hint already drops its shift line
            // in this mode, so nothing advertises the press; what is left is
            // the user who pressed it anyway.
            notifications.notify(
                AppState::NotificationClass::Normal,
                "Turn off grid iterations to render one file");
            return true;
        }

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

        // The cell this press produces may be the WHOLE piece under a hairline
        // trim bar — the same recipe, so the same answer (definition above).
        card_trim_fallback_if_any();

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
    // in the BPM dialog editor after a successful commit; there is no
    // key-dispatch handler for it here.

    return false;
}

// The promote roads' past-EOF wall guard (contract at the declaration): the
// loader's own shared check, asked of a candidate marker pair against this
// session's audio. The live trim pair rides along because the guard's six
// checks are one call; only the two marker arms can answer here.
std::optional<std::string> GuiInputHandler::in_place_load_wall_defect(
        const std::vector<GuiWarpMarker>& warp,
        const std::vector<GuiPhaseResetMarker>& phase_resets) const {
    auto trim_of = [](const TrimState& t) {
        SettingsTrim s;
        s.begin_frame = t.begin_frame;
        s.end_frame   = t.end_frame;
        return s;
    };
    return first_past_eof_wall_defect(
        slice_to_warp_markers(warp), slice_to_phase_reset_markers(phase_resets),
        trim_of(app.tab_a.trim), trim_of(app.tab_b.trim),
        audio.total_frames(), audio.sample_rate());
}

// -- THE RECIPE APPLY, the two sidecar load-in-places' one body -------
//
// The RULE this body exists to state once — a load in place writes exactly what
// its one undo entry restores, and everything else stays live — is at the
// declaration (input_handler.h), with the whole-file apply it superseded on
// 2026-08-24. This comment carries only the sequence's own reasons.
void GuiInputHandler::apply_recipe_in_place(
        std::vector<GuiWarpMarker> warp,
        std::vector<GuiPhaseResetMarker> phase_resets,
        const EngineSettings& engine) {
    std::vector<GuiWarpMarker>       warp_pre = app.warpmarkers.markers();
    std::vector<GuiPhaseResetMarker> phase_reset_pre =
        app.phaseresetmarkers.markers();

    app.warpmarkers.markers_mut()       = std::move(warp);
    app.phaseresetmarkers.markers_mut() = std::move(phase_resets);
    // Wholesale authoring reset: the ONE selection goes, and there is nothing
    // else to reset — no per-tab per-mode slot holds a copy (the parked
    // selections died 2026-07-29, so a wholesale store replace no longer has to
    // hunt down stale index sets in either ViewState).
    selection.clear_selection();

    // ONE cross-file undo entry: the marker pair plus the OUTGOING engine
    // settings, which push_undo_both captures from `app` — so it must run
    // BEFORE the incoming block is applied below. It files under the LIVE tab
    // and the LIVE W/P column, which are the only ones this act touches now
    // that it performs no tab or column switch at all.
    undo.push_undo_both(std::move(warp_pre), std::move(phase_reset_pre),
                        app.active_markers_view);
    undo.recompute_dirty();

    // Wholesale authoring reset: clear every marker's session-only iteration
    // state and the bpm state, and turn off both sweep modes' visibility. This
    // is scratch ABOUT the markers that were just replaced, so it goes with
    // them; it is not view state and not a preference.
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

    // The engine block, VALUES ONLY — the third and last piece the undo entry
    // carries. The map rebuild and the target preview are the tail's, below,
    // exactly as they are an undo restore's.
    app.engine_settings = engine;

    // The store and the engine block that built the displayed target basis are
    // both gone, so the basis goes cold through its one owner
    // (reset_displayed_target_basis, app_state.h, which carries the membership
    // and names its two callers).
    reset_displayed_target_basis(app);

    // The LIVE camera stays exactly where the user left it, but the DOMAIN it
    // sits in may have moved: a target-view session's total is derived from the
    // map the markers and the engine block just replaced. So the playhead takes
    // the shared clamp (clamp_playhead_to_live_domain) and the viewport takes
    // its own chokepoint. BEFORE the auto-select below, so the coincidence scan
    // reads an in-domain cursor; kick_waveform_sync's own
    // clamp_display_state_to_live_domain then re-asks and finds nothing to do.
    // THE PARKED BAND IS NOT CLAMPED HERE and must not be: it is not a live
    // field, and the Ctrl+Tab restore clamps it on the way in (active_views.cpp)
    // — the inventory at clamp_playhead_to_live_domain's declaration is what
    // this follows.
    app.playhead_cursor_sample =
        clamp_playhead_to_live_domain(app.playhead_cursor_sample, app, audio);
    clamp_viewport_start(app, audio);

    // COINCIDENCE AUTO-SELECT, the load-in-place chokepoint (the rule, the
    // formula and the authoritative call-site inventory live at
    // auto_select_marker_at_playhead, input_pointer.cpp / input_handler.h). The
    // act replaced the store under a resting playhead, so the entry re-acquires
    // the selection the wholesale clear above dropped — the never-park rule's
    // entry half. PLACED HERE, at the tail: the stores, the engine block and the
    // domain clamps have all run, so the scan reads the final playhead against
    // the column the session is actually standing in. Nothing downstream writes
    // the selection, so the single-select it may make is what rests. Its narrow
    // damage is superseded by the kick below and by the caller's full-window
    // invalidate.
    auto_select_marker_at_playhead(app, audio, selection, viewport);
    viewport.kick_waveform_sync();
    viewport.invalidate_waveform_area();
    viewport.invalidate_clock_area();

    // The trigger owns the rebind for a session standing in TARGET view: it
    // marks the buffer stale and dispatches the loaded-in-place state's target
    // preview, which rebinds playback on completion.
    target_render.trigger();
}

// -- Standalone render-entry load-in-place (the render player's load) ------
//
// Load render entry `e`'s frozen sidecar recipe in place as the new authoring
// baseline, view-agnostic: callable from source OR target authoring view. It
// takes an explicit entry, and THE ACT ITSELF OWNS THE VISIBLE REFUSAL — the
// local `refuse` below, whose contract is stated further down; the caller
// says nothing at either end. ONE
// CALLER, re-greped: confirm_load_in_place's player arm, the Load in place
// button through its confirmation, which acts on the HIGHLIGHTED batch cell
// and so has no name to fail to resolve — its own refusals (the lock, a
// running render, a highlight that is not a batch cell) all run before the
// prompt is raised. The retired two-road sanction is recorded at the
// declaration.
//
// Reads-then-checks BEFORE any mutation: the entry wav must exist and all
// three sidecars (.settings, .warpmarkers, .phaseresetmarkers) must read and
// validate. On ANY failure — the running-batch self-guard, a missing wav, or a
// malformed / unreadable sidecar — return false with NO state mutation, so a
// failure leaves authoring untouched.
//
// THE ACT OWNS ITS REFUSALS, on BOTH surfaces (architect 2026-08-30, taking
// the caller's useless "Load refused" out): every arm names its cause once
// through the local `refuse` below, which writes the STDERR line (architect
// 2026-08-02 — with the offending path, the debugging surface) AND the
// NOTIFICATION CARD "Load in place refused: <reason>", which is the `h`
// view's own load acts' form, so the product's load-in-place roads answer
// alike. First-error-only holds by construction (each arm returns), and the
// card names the file the BASENAME RULE's way (messaging.md) — the cell's own
// id, or a sidecar through shown_project_path (device_config.h), the SAME
// folder-and-file composer the loaders and the dry run use, never a full path
// — SINGLE-QUOTED like every other name in a sentence. Until 2026-09-01 this
// family wrote the id bare and the sidecar as a bare `.filename()`, which was
// the product's second path form on a card and its one unquoted name beside
// quoted ones. Returns true after the
// recipe is applied and tmp/ wiped, and the caller then says nothing at all.
bool GuiInputHandler::load_render_entry_in_place(
        const AppState::RenderEntry& e) {
    // THE ONE REFUSAL OWNER for both surfaces: the sentence is composed once
    // and the full path rides along for stderr alone.
    auto refuse = [&](const std::string& reason,
                      const std::filesystem::path& full) {
        if (full.empty())
            std::fprintf(stderr, "warptempo_gui: Load in place refused: %s\n",
                         reason.c_str());
        else
            std::fprintf(stderr,
                         "warptempo_gui: Load in place refused: %s ('%s')\n",
                         reason.c_str(), full.string().c_str());
        notifications.notify(AppState::NotificationClass::Normal,
                             "Load in place refused: " + reason);
        return false;
    };

    // Self-guard on the standalone mutator: a successful load-in-place wipes
    // tmp/,
    // which must never race a batch publishing into it. The player's load act
    // already refuses on this same condition — with its own sentence — before
    // it raises the confirmation, so the one caller never reaches here; this
    // backstop protects any other caller and answers like every arm below it.
    // The condition is the one owner load_in_place_render_blocked
    // (app_state.h), the act's, the face's and this backstop's alike.
    if (load_in_place_render_blocked(app)) {
        return refuse("a render batch is running or an archival is armed", {});
    }

    // NOT a modal open, so NOT the modal-open owner's business
    // (stop_playback_for_modal_open belongs to the sites that open a surface):
    // this is the standalone mutator's own self-guard. The player's load act
    // already paused its transport when it raised the confirmation; stopping
    // again here keeps the mutator correct from any caller.
    playback_lifecycle.stop_playback_if_playing();

    // -- Read + validate every input BEFORE touching a store. --
    std::error_code ec;
    if (!std::filesystem::is_regular_file(e.wav_path, ec)) {
        return refuse("the wav for '" + render_entry_id(e) +
                          "' is missing or is not a regular file",
                      e.wav_path);
    }

    const std::filesystem::path sidecar = renders_dir.settings_path(e);
    const auto settings = read_settings_file(sidecar.string());
    if (!settings) {
        return refuse("invalid settings in '" +
                          shown_project_path(sidecar) + "': " +
                          settings.error(),
                      sidecar);
    }

    std::vector<GuiWarpMarker>       src_warp;
    std::vector<GuiPhaseResetMarker> src_phase_resets;
    {
        GuiWarpMarkers m;
        const std::filesystem::path wm =
            e.batch_folder / (e.basename + ".warpmarkers");
        auto r = m.load(wm.string());
        if (!r) {
            return refuse("invalid warp markers in '" +
                              shown_project_path(wm) + "': " + r.error(),
                          wm);
        }
        src_warp = m.markers();
    }
    {
        GuiPhaseResetMarkers t;
        const std::filesystem::path tm =
            e.batch_folder / (e.basename + ".phaseresetmarkers");
        auto r = t.load(tm.string());
        if (!r) {
            return refuse("invalid phase reset markers in '" +
                              shown_project_path(tm) + "': " + r.error(),
                          tm);
        }
        src_phase_resets = t.markers();
    }

    // THE PAST-EOF WALL, the loader's own adversarial guard asked of the
    // parsed pair before anything is installed (in_place_load_wall_defect
    // carries the whole reasoning). A cell authored against a longer take
    // would otherwise land markers past `total - 1` in the live store, Ctrl+S
    // would write them, and the next launch would refuse the file. The
    // refusal is WHOLE — not a dropped marker — and names its cause on both
    // surfaces like every other arm here.
    // The defect is a sentence at its frozen producer (two consumers use it
    // whole) and this is an APPENDING seam, so it lowercases through the one
    // owner lowercase_initial like its three siblings — notifications.h states
    // the rule, and every other reason handed to `refuse` is already lowercase.
    if (auto defect = in_place_load_wall_defect(src_warp, src_phase_resets)) {
        return refuse(lowercase_initial(*defect), {});
    }

    // Every input is in hand and valid; nothing below refuses. WHAT IS APPLIED
    // IS THE RECIPE AND NOTHING ELSE — the marker pair and the engine block —
    // through the shared owner apply_recipe_in_place, whose declaration
    // (input_handler.h) states the rule. The file's view keys, its two tab bands
    // and its session prefs are READ PAST: the entry sidecar carries them
    // because it is a whole standard-schema `.settings` a CLI or a plain source
    // load could read, not because this act wants them.

    // THE `h` HISTORY MODE ENDS HERE, on the first line past the last refusal
    // and before the first store write. It is the one route in the product that
    // replaces the authored state the mode's frozen now side was measured
    // against, so leaving the mode standing would leave every flag in the lane
    // describing a session that no longer exists. Placed in this act's own body
    // rather than at the `'` key because this function is what performs the
    // replacement, and the close belongs with the act rather than with one of its
    // callers. It stays here rather than moving into the shared recipe apply
    // below: the mode's other load closes it too, but only after reading the
    // session the close clears (see there), so the close is each act's own line.
    // IN PRACTICE IT IS AN IDEMPOTENT NO-OP: the mode ADMITS bare `'`, but in
    // the view that key raises the confirmation on the VIEWED WALK MEMBER, and
    // that prompt's OK forks at confirm_load_in_place to one of the mode's own
    // two loads — never to this act, whose subject is the render player's
    // parked entry and whose opener the mode refuses. So no renders-side load
    // ever runs with a visit standing (the closer inventory at
    // close_history_mode states it). The line stays for the same reason the
    // close is at the mutator at all.
    close_history_mode();

    apply_recipe_in_place(std::move(src_warp), std::move(src_phase_resets),
                          settings->engine);

    const std::filesystem::path batch_root =
        project_batch_root(app.source_audio_path);

    // Wipe tmp/ AFTER the successful load-in-place. The loaded render survives
    // through the render cache, not as a folder artifact.
    //
    // TO THE DESKTOP TRASH FIRST (architect 2026-08-07): this is the product's one
    // deletion of user-visible artifacts, so a wiped batch stays restorable. THE
    // FALLBACK IS THE DETECTION — no upfront probe, no setting, no capability
    // cache: trash_directory answers by observation, and its false lands on the
    // native remove_all below with ONE line saying the trash was unavailable. A
    // successful trash prints no diagnostic of its own; that silence is this
    // wipe's ordinary ending and always was, and the act's own tail line below is
    // what names where the batch went. The is_directory guard and the
    // ec-reported failure line are the fallback path's own, unchanged.
    //
    // THE GUARD READS ITS error_code for the same reason the trash witness does:
    // is_directory returns false both when the status says "not there" and when
    // the status QUERY ITSELF FAILED (a permission or I/O error), and those are
    // opposite verdicts. An absence is the ordinary silent ending — nothing to
    // dispose of — while a failed query is INDETERMINATE: the folder may well
    // still be there, nothing was trashed or deleted, and it gets its own line.
    //
    // THE TAIL LINE THEN NAMES THE DISPOSAL THAT ACTUALLY HAPPENED (architect
    // 2026-08-08), three wordings off one verdict: TRASHED says "moved tmp/
    // to the trash", because that batch is RESTORABLE and "wiped" would overstate
    // it — the whole point of the trash-first rule; WIPED says "wiped tmp/"
    // for the native fallback's own delete, which is not restorable; and NONE
    // drops the clause entirely on the two failing shapes (the failed query and
    // the fallback delete's error), since the load succeeded either way but the
    // disposal did not happen and each shape has already printed its own line.
    // THE ABSENT DIRECTORY KEEPS THE "WIPED" WORDING, unchanged from before the
    // split: the clause is a claim about the END STATE the act guarantees — there
    // is no tmp/ on disk and nothing of it left to restore — which is exactly
    // true with nothing there, and the split is about restorability, the one axis
    // an absence has no side of.
    enum class WipeVerdict { Trashed, Wiped, None };
    WipeVerdict verdict = WipeVerdict::Wiped;  // The absent case; see above.
    if (std::filesystem::is_directory(batch_root, ec)) {
        if (trash_directory(batch_root)) {
            verdict = WipeVerdict::Trashed;
        } else {
            std::fprintf(stderr,
                "warptempo_gui: load-in-place: Trash unavailable for '%s'; "
                "deleting it instead\n",
                batch_root.string().c_str());
            std::filesystem::remove_all(batch_root, ec);
            if (ec) {
                std::fprintf(stderr,
                    "warptempo_gui: load-in-place: Wipe failed for '%s': %s\n",
                    batch_root.string().c_str(), ec.message().c_str());
                verdict = WipeVerdict::None;
            }
        }
    } else if (ec) {
        std::fprintf(stderr,
            "warptempo_gui: load-in-place: Could not check '%s': %s\n",
            batch_root.string().c_str(), ec.message().c_str());
        verdict = WipeVerdict::None;
    }

    const char* disposal =
        (verdict == WipeVerdict::Trashed) ? " and moved tmp/ to the trash"
      : (verdict == WipeVerdict::Wiped)   ? " and wiped tmp/"
                                          : "";
    std::fprintf(stderr,
        "warptempo_gui: load-in-place: Loaded render in place%s\n", disposal);
    gui.invalidate_region(0, 0, app.width, app.height);
    return true;
}

// -- Load-in-place from a COMMIT (the `'` editor in the `h` history mode) --
//
// WHAT IT IS: the same act load_render_entry_in_place performs, with the committed
// history as its source instead of a render entry. `sha` is the full SHA the
// prefetch store holds for the VIEWED member — the one caller
// (confirm_load_in_place) hands it a walk member
// and nothing else; the typed spelling, and with it the "short SHA pasted out
// of GitHub's web UI" use case, retired with the load prompt's field
// (architect R23). ONE STATE IN, ONE STATE OUT: the three sidecars THAT
// commit carried become the live session, in memory, and the disk is never
// touched — not the corpus, not the working sidecars, not tmp/.
//
// WHAT GATES, all of it BEFORE any store is touched — the validate-before-mutate
// contract load_render_entry_in_place states and this path mirrors: ONE call,
// load_commit_sidecars_strict (history_diff.h), which is the resolution, the
// missing-sidecar refusals, the scratch staging and the three STRICT
// WHOLE-FILE LOADERS in one predicate — the same predicate that is WALK
// MEMBERSHIP since 2026-08-04, so a walk member's own SHA passes by
// construction and every refusal arm (an unresolvable spelling, a partial
// checkpoint, an ambiguous per-commit path resolution, a sidecar the loaders
// refuse) can fire only on a change in the clone between the scan and the
// act. That strictness is the point rather than a side effect — exactly the
// parse-gating the architect ruled, and the reason no second, looser grammar
// is written anywhere on this path. A refusal is one stderr line naming its
// cause with the committed path and the SHA (first error only — the gate's own
// contract) AND a notification card carrying the same sentence
// (2026-08-29; stderr alone until then, the view's own status line having
// outranked the transient tier a refusal would have written).
//
// THE WAV IS NOT COMPARED, and there is nothing to compare it to: the corpus
// stores the three sidecars and no audio at all, so the LOADED SOURCE IS THE
// SOURCE — this loads a recipe in place for the file already open, exactly as
// the mode's
// diff measures a commit against the session for that same file. The render-entry
// load-in-place's wav-existence check has no counterpart here.
//
// NO RUNNING-RENDER GUARD, deliberately. load_render_entry_in_place's self-guard
// protects ITS TAIL — the tmp/ wipe, which must never race a batch
// publishing into that directory — and this path has no tail to protect: it
// wipes nothing and reads no render entry. A render dispatched BEFORE the mode
// opened (the mode blocks the launchers, not a render already in flight) renders
// from the request snapshot it was built with, publishes into tmp/ and the
// render cache, and is untouched by and untouching of this act; its entries then
// describe the state from before the load-in-place exactly as they do after
// any other authoring
// edit.
//
// WHAT IS APPLIED is THE RECIPE — the commit's marker pair and its engine block,
// through the shared owner apply_recipe_in_place, which is also
// load_render_entry_in_place's body and whose declaration (input_handler.h)
// states the rule: a load in place writes exactly what its one undo entry
// restores. Everything else the commit's `.settings` carries is READ PAST, the
// three-sidecar set being a whole standard-schema state rather than a request:
// its tab bands (the checkpoint's trim included), its S/T, W/P and A/B keys, its
// camera, and its session prefs — `projects_repo` among them, so a commit whose
// settings named a different projects home no longer installs that answer, and
// the next `h` reads the live one.
//
// AND THE MODE CLOSES, at the first line past the last refusal — the placement
// load_render_entry_in_place states and for its reason: this is the other route that
// replaces the very state the frozen now side was measured against, so leaving
// the mode standing would leave every flag in the lane describing a session that
// no longer exists.
bool GuiInputHandler::load_history_commit_in_place(const std::string& sha) {
    // The mode is the route's precondition, not a courtesy: the sidecar base
    // name comes from the session (init() owns that derivation), and the close
    // below is part of the act.
    if (!app.history_mode.active) return false;
    const std::string base_name =
        app.history_mode.session.sidecar_base_name();
    // THE CLONE IS THE SESSION'S OWN, derived from the loaded source at init
    // (history_diff.h): the `'` act reads the same repository the lane was
    // built from, on the same one derivation.
    const std::string repo_root = app.history_mode.session.repo_root();

    // THE WHOLE VALIDATION IS THE ONE SHARED GATE, and the SHA is all it
    // takes now: the session's matched directory was a parameter here until
    // 2026-08-09, when the folder a commit is about became the folder that
    // COMMIT TOUCHED rather than a tie the session could break. So a commit
    // touching this base name in two directories refuses instead of resolving to
    // whichever one this session happens to sit in, and one touching it nowhere
    // — an ordinary non-piece commit, a merge, a read that did not answer —
    // refuses too. Nothing is settled by a guess any more, which is the point.
    GuiHistoryCommitLoad loaded;
    std::string          reason;
    if (!load_commit_sidecars_strict(repo_root, sha, base_name, loaded,
                                     reason)) {
        std::fprintf(stderr, "warptempo_gui: Load in place refused: %s\n",
                     reason.c_str());
        notifications.notify(AppState::NotificationClass::Normal, "Load in place refused: " + reason);
        return false;
    }
    const SettingsFile& settings = loaded.settings;
    std::vector<GuiWarpMarker>       src_warp = std::move(loaded.warp_markers);
    std::vector<GuiPhaseResetMarker> src_phase_resets =
        std::move(loaded.phase_reset_markers);

    // THE PAST-EOF WALL, the sibling's own line and for its reason: a
    // checkpoint's sidecars are state authored against whatever audio stood
    // when it was committed, and load_commit_sidecars_strict reads them
    // against no audio at all (in_place_load_wall_defect carries the whole
    // reasoning). The refusal is WHOLE and names its cause on stderr and on a
    // notification card like every other arm here.
    if (auto defect = in_place_load_wall_defect(src_warp, src_phase_resets)) {
        // Appended, so lowercase through the one owner (notifications.h).
        const std::string reason = lowercase_initial(*defect);
        std::fprintf(stderr,
            "warptempo_gui: Load in place refused: %s\n", reason.c_str());
        notifications.notify(AppState::NotificationClass::Normal, "Load in place refused: " + reason);
        return false;
    }

    // Every input is in hand and valid; nothing below refuses.

    // NOT a modal open, so NOT the modal-open owner's business — the standalone
    // mutator's own self-guard, exactly as the render-entry load-in-place
    // spells it. The `'` raise already froze playback through
    // that owner on the keyboard route; stopping again here keeps the mutator
    // correct from any caller.
    playback_lifecycle.stop_playback_if_playing();

    // THE MODE ENDS HERE, on the first line past the last refusal and before the
    // first store write — the render-entry load-in-place's placement and its
    // reason (see the
    // paragraph at the head of this function). It also clears the session this
    // function read its base name from, which is why that read is at the top.
    close_history_mode();

    apply_recipe_in_place(std::move(src_warp), std::move(src_phase_resets),
                          settings.engine);

    // NO tmp/ WIPE. That step is the render-entry load-in-place's cleanup
    // of the folder it
    // consumed an entry from; this path consumed a commit and tmp/ is none of
    // its business.
    std::fprintf(stderr,
        "warptempo_gui: load-in-place: Loaded the sidecar state of commit "
        "%s in place\n",
        loaded.sidecars.sha.c_str());
    gui.invalidate_region(0, 0, app.width, app.height);
    return true;
}

// -- Load-in-place from a LOCAL HISTORY MEMBER (the `'` confirmation on a
//    Local tab) --
//
// WHAT IT IS: the third of the load-in-place family (architect 2026-08-08,
// superseding his own "the Local walk consumes `'`"), with A STATE OF THIS
// SESSION'S OWN UNDO/REDO TIMELINE as its source. `number` is the highlighted
// VIEWED member's displayed NUMBER — the corner's own `n/N` vocabulary, which
// is the only name a local member has (the one caller,
// confirm_load_in_place, hands it that member; the typed number retired
// with the load prompt's field).
//
// IT IS NEVER A ROLLBACK. The member's state is applied ON TOP of the current
// one as ONE NEW UNDO ENTRY, exactly as loading a commit is, so Ctrl+Z
// afterwards returns to the state from immediately before the load — walking to
// the oldest member and loading it gives you the file-open state to audition,
// with one keystroke back out of it. push_undo_both CLEARS THE REDO STACK, which
// is correct and ruled: the load forks the timeline, and the walk it forked was
// already closed with the mode a few lines above.
//
// WHAT GATES, all of it BEFORE any store is touched — the family's
// validate-before-mutate contract: the number must name a member in [1, N],
// and the walk must hand back that member's state. Anything else is ONE
// stderr line and a false return, the caller keeping the picker open. There is no grammar and nothing to
// resolve: N is the walk's own member count, and the number is an index into
// it.
//
// WHAT IS APPLIED: an undo entry carries the two MARKER COLUMNS and the ENGINE
// BLOCK and nothing else (the carry-everywhere shape at UndoEntry), so that is
// exactly what this restores — the same three pieces the walk's delta vocabulary
// is built from, and, since 2026-08-24, exactly what BOTH SIBLINGS write too: a
// load in place writes what its undo entry restores, so the family's three acts
// differ only in where the three pieces come from. They differ in NOTHING ELSE
// since 2026-08-24, this body having joined the two on the one apply owner
// (GuiInputHandler::apply_recipe_in_place, input_handler.h, which carries the
// rule and the sequence): what is spelled here is the validation, the mode
// close and the stderr line, and the apply itself is read there. NO tab bands,
// NO playback_speed, NO gui_scale, NO trim, NO read_only, NO session prefs —
// here because a timeline state does not carry them at all, there because the
// act reads past the ones a sidecar set does. (The first two clauses are
// tombstones as of 2026-08-27: playback_speed retired whole and gui_scale left
// the sidecar for the per-device config, so neither is a thing a sidecar set
// could offer any more.)
//
// THE STATE IS TAKEN AS TYPED SNAPSHOTS, never through the member's three TEXTS:
// those are the DIFF's medium, and re-parsing them would put the strict loaders
// in a path with nothing to parse. It is COPIED before anything is written,
// which is what makes THE IDENTITY LOAD (loading the live member, the one the
// session is standing in) an ordinary case rather than a store assigned to
// itself. That load is deliberately NOT refused: it pushes an undo entry whose
// restore puts back what is already there, which is honest — the user asked for
// that member — and costs one Ctrl+Z to leave.
//
// AND THE MODE CLOSES, at the first line past the last refusal, for the reason
// both siblings state: this replaces the very state the frozen now side was
// measured against.
bool GuiInputHandler::load_history_local_entry_in_place(std::size_t number) {
    // The mode is the route's precondition — the walk lives on it, and the close
    // below is part of the act. The source test is the routing's own fact
    // restated defensively: confirm_load_in_place sends only Local-tab
    // members here.
    if (!app.history_mode.active) return false;
    if (app.history_mode.source != GuiHistoryWalkSource::Local) return false;

    // THE WHOLE VALIDATION: the number is a count position in [1, N].
    const std::size_t count  = app.history_mode.local.entry_count();
    const bool        in_range = number >= 1 && number <= count;

    // THE WALK'S OWN ANSWER IS THE SECOND HALF OF THE GATE. It is empty only for
    // an UNBOUND walk or a stack shorter than its capture (the blank-lane state,
    // which a live Local tab cannot reach — the mode's entry binds the walk and
    // the allowlist refuses the chord on an empty one), so this is the
    // unreachable arm stated rather than assumed, refusing in the same shape a
    // bad number does.
    std::optional<GuiHistoryLocalWalk::MemberState> state;
    if (in_range) state = app.history_mode.local.member_state(number - 1);
    if (!state) {
        std::fprintf(stderr,
            "warptempo_gui: Load in place refused: %zu is not a history entry "
            "number (1..%zu)\n", number, count);
        notifications.notify(
            AppState::NotificationClass::Normal,
            "Load in place refused: " + std::to_string(number) +
                " is not a history entry number (1.." +
                std::to_string(count) + ")");
        return false;
    }

    // COPIED BEFORE ANYTHING IS WRITTEN (see the header): the pointers name the
    // live stores themselves on the identity load, and the close below ends the
    // visit they are valid for.
    std::vector<GuiWarpMarker>       src_warp   = *state->warp_markers;
    std::vector<GuiPhaseResetMarker> src_phase_resets =
        *state->phase_reset_markers;
    EngineSettings                   src_engine = *state->engine_settings;

    // Every input is in hand and valid; nothing below refuses.

    // NOT a modal open, so NOT the modal-open owner's business — the standalone
    // mutator's own self-guard, exactly as both siblings spell it. The history
    // picker's open already froze playback through that owner on the keyboard
    // route; stopping again here keeps the mutator correct from any caller.
    playback_lifecycle.stop_playback_if_playing();

    // THE MODE ENDS HERE, on the first line past the last refusal and before the
    // first store write — the family's placement. It also drops the walk the
    // state was read out of, which is why the copies are above it, AND it is
    // what keeps this act inside the walk's frozen-timeline premise: the push
    // below happens with no visit standing, so the entry it adds is not one any
    // walk had captured.
    close_history_mode();

    // THE APPLY IS THE ONE OWNER'S (apply_recipe_in_place), the same body both
    // siblings hand their three pieces to: the outgoing snapshot, both store
    // replacements, the selection clear, the ONE cross-file undo entry, the
    // session-only marker scratch wipe, the engine block, the displayed target
    // basis reset, the live playhead and viewport clamps, and the auto-select /
    // sync / invalidate / trigger tail. What is left below is this act's own.
    //
    // THE TWO STEPS THIS BODY GAINED BY JOINING ARE CORRECT FOR IT, not a cost
    // of sharing: a wholesale store replace can move the ACTIVE domain's total
    // under a resting cursor whatever the three pieces came from, so the live
    // playhead's clamp and the cold displayed target basis belong to every
    // recipe apply — a timeline state's included.
    apply_recipe_in_place(std::move(src_warp), std::move(src_phase_resets),
                          src_engine);

    // NO tmp/ WIPE and NO DISK WRITE of any kind: this act moved state that
    // was already in memory from one place in memory to another.
    std::fprintf(stderr,
        "warptempo_gui: load-in-place: Loaded local history entry %zu of %zu "
        "in place\n",
        number, count);
    gui.invalidate_region(0, 0, app.width, app.height);
    return true;
}

// THE MODAL DIALOG'S KEYBOARD FOCUS RING (architect 2026-08-13, part D of the
// modal-button ruling: the FACE is kdenlive-sampled, the NAVIGATION his own
// derivation — kdenlive's dialogs have no text boxes, so there was nothing to
// copy for the field's half). ONE route for both modal surfaces, read by the
// prompt gate (input_handler.cpp) and by route_modal_editor_key below; the
// state, the two meanings of -1 and the reset rule are at
// AppState::modal_dialog_focus.
//
//   TAB      cycles every stop. On an EDITOR dialog the FIELD is a stop, so
//            one field and two buttons is three stops, wrapping; on a PROMPT
//            there is no field and the buttons alone cycle. AN EDITOR WITH AN
//            AUTOCOMPLETE GETS FIRST REFUSAL on the field's FORWARD Tab,
//            upstream of this route — a completion that ADVANCED consumes the
//            key and this route is never called, one that did not hands it
//            straight here. The whole model is stated once at
//            route_modal_editor_key's Tab arm; nothing about it is decided in
//            this body.
//   SHIFT+   the REVERSE walk (architect 2026-08-13: "also, shift+tab should
//   TAB      cycle backward"), the forward arm's exact inverse — the same
//            stops, the same wrap, the other direction — and it NEVER
//            COMPLETES: shift means "go back", never "complete", so no
//            autocomplete is offered on this shape at any site. Its spellings
//            are the live marker cycle's, one predicate away at
//            modal_ring_tab_shape.
//   LEFT /
//   RIGHT    move between BUTTONS ONLY and are INERT in an editor's field: the
//            arrows belong to the text there and the editors' own motion arm
//            owns them. They wrap like Tab: one ring, one rule.
//   ENTER /  PRESS THE FOCUSED BUTTON DOWN and commit it AT THE KEY'S RELEASE
//   SPACE    (architect 2026-08-13, from kdenlive: "pressing Enter when a
//            button has focus pushes down the button. It doesn't automatically
//            commit the action... we move the playhead when the user lifts up
//            the mouse key, so we should do that here as well"). The press arms
//            and paints; on_key_release runs the act through the one shared
//            dispatch. Both keys are BARE-EXACT, and both mean the button only
//            while the focus IS on a button: with the focus in an editor's
//            FIELD neither reaches this route at all, so Enter keeps its commit
//            and Space still types a space, byte-identical to before the ring.
//            A prompt has no field, so on a prompt they always mean the focused
//            button — which since the same day's passive-focus ruling is the
//            button the raise focused: the Escape sentinel everywhere but the
//            render player's load confirmation, which asks for its OK
//            (PromptState owns the supersession of "this prompt system has no
//            Enter answer", the two facts that make it safe and that one
//            exception's own reason).
//
// THE WALK ASSIGNS THE ACTIVE STRENGTH, and it is the strength's ONE producer:
// every landing on a button here is a deliberate keyboard step, which is the
// definition of active focus (AppState::modal_dialog_focus_active). Landing
// back on an editor's FIELD carries no strength and clears it.
//
// EVERY SHAPE IS BARE-EXACT WITH ONE DELIBERATE EXCEPTION, THE REVERSE WALK:
// the strict-modifier rule's own text (conventions.md) names THREE families
// left untightened BECAUSE the modifier is a real binding there — is_tab_cycle,
// Ctrl+Z's shift-selects-redo, and the editors' motion arm — and the ring's
// reverse Tab is exactly that shape, shift meaning "go back" here as it does in
// the marker cycle this ring's spellings mirror. So the modifier guard below
// admits the reverse shape and refuses every other modified key: Ctrl+Tab
// (the tab-cycle family's), Shift+Left (the field's selection extension) and
// the rest are still not this ring's to take.
// THE STASH'S IDENTITY GATES IT like both press claims — the stash is the
// painter's publication and may only SELECT; the surface that owns input
// DECIDES (modal_dialog_stash_current, the one comparison). A flag editor
// publishes no dialog at all, so its ring is empty by construction and every
// shape here declines.
bool GuiInputHandler::route_modal_dialog_focus_key(GuiKey key,
                                                   GuiInputState mods) {
    // KEYPAD ENTER IS RETURN, HERE AS EVERYWHERE (2026-08-29). The pair is one
    // key at every other surface in the product — the editors' session keys,
    // the flag editor's open chord, and the two LIST routers below, which map
    // KpEnter onto the highlight's own act — so a ring that knew only Return
    // let a keypad Enter fall PAST a focused button and open the highlighted
    // row instead: Tab onto the picker's Cancel and press it, and the picker
    // opened a project. NORMALIZED AT THE HEAD, so the press arm's compare and
    // the key it STORES for the release are both Return and a KpEnter press
    // released as KpEnter still completes (on_key_release normalizes the same
    // way, through the same owner). With the LIST focused (at < 0) both keys
    // still fall through to the list routers exactly as before.
    key = modal_ring_press_key(key);
    const ModalRingTab tab_shape = modal_ring_tab_shape(key, mods);
    if (tab_shape == ModalRingTab::None &&
        (mods.ctrl || mods.shift || mods.alt)) {
        return false;
    }
    const AppState::ModalDialogGeometry& dlg = app.modal_dialog;
    if (!dlg.valid || dlg.buttons.empty()) return false;
    if (!modal_dialog_stash_current()) return false;
    const bool prompt_up = app.prompt.active;
    // THE RENDER PLAYER'S RING (architect R9, 2026-08-28) is [LIST, buttons…]:
    // the folder overlay's list is one member, standing where an editor's
    // field stands (-1) with one difference — the player OPENS with the ring
    // NOWHERE (-1 and `list_focused` false), and the first Tab lands on the
    // list. LANDING THERE SHOWS: the highlighted row's outline takes the
    // ring's ACTIVE strength (the accent) where it wore the passive line while
    // the ring stood elsewhere, and the band repaints for it at the bit's
    // write below — the walk's own face, the same two lines a focused button
    // wears. What it does NOT change is the highlight itself: Up/Down walk it
    // either way, and what else the landing decides is what a bare Enter
    // means. Left/Right are NOT the ring's here: they are the seeks, the car's
    // rewind and fast-forward, on a button or off it. THE PICKER'S RING IS THE
    // SAME SHAPE, one member shorter (its row is Cancel alone since
    // 2026-08-29): [list, Cancel], opening nowhere, and Left/Right are not
    // its either — its router consumes them, the list's walk being Up/Down —
    // so the two list-bearing owners share one arm.
    const bool list_up = !prompt_up &&
                         (app.render_player.active || app.picker.active);
    const bool on_list = list_up && app.folder_overlay.list_focused;
    const int n = static_cast<int>(dlg.buttons.size());
    const int at = (app.modal_dialog_focus >= 0 &&
                    app.modal_dialog_focus < n) ? app.modal_dialog_focus : -1;

    int  next      = at;
    bool list_next = on_list;
    if (tab_shape == ModalRingTab::Forward) {
        // The field is a stop only on an editor dialog, which is the one
        // place -1 is a place to come back to — and the player's list is
        // that place for the player, reached first from nowhere.
        if (list_up && at < 0 && !on_list) { next = -1; list_next = true; }
        else if (at < 0)       { next = 0; list_next = false; }
        else if (at + 1 < n)   next = at + 1;
        else                   { next = prompt_up ? 0 : -1;
                                 list_next = list_up; }
    } else if (tab_shape == ModalRingTab::Reverse) {
        // The exact inverse of the forward arm — the same stops in the same
        // order, walked the other way, with the same wrap: on an editor
        // field -> last button -> ... -> first button -> field, on a prompt
        // the buttons alone, wrapping, on the player list -> last button ->
        // ... -> first button -> list. (The prompt's `at < 0` arm below is a
        // cold answer only: a standing prompt always has a focused button,
        // assigned at its raise.)
        if (at < 0)            { next = n - 1; list_next = false; }
        else if (at > 0)       next = at - 1;
        else                   { next = prompt_up ? n - 1 : -1;
                                 list_next = list_up; }
    } else if (key == GuiKeys::Right) {
        if (list_up) return false;                // the list owners' own
        if (at < 0 && !prompt_up) return false;   // the field's own arrows
        next = (at < 0) ? 0 : (at + 1) % n;
    } else if (key == GuiKeys::Left) {
        if (list_up) return false;                // the list owners' own
        if (at < 0 && !prompt_up) return false;   // the field's own arrows
        next = (at < 0) ? n - 1 : (at + n - 1) % n;
    } else if (key == GuiKeys::Return || key == GuiKeys::Space) {
        // THE BUTTON'S KEYBOARD PRESS — Return here NAMES BOTH ENTER KEYS, the
        // main one and the keypad's, normalized to this spelling at the head
        // (modal_ring_press_key); no other site restates that pairing. With no
        // button focused this is not the ring's key at all: an editor's field
        // keeps Enter's commit and Space's typed character, and both fall
        // through untouched.
        if (at < 0) return false;
        // A GREYED BUTTON'S KEYBOARD PRESS IS A CONSUMED NOTHING (architect
        // 2026-08-30) — the roster's rule on this surface: no arm, no
        // pressed face, no card (the grey is the message). Reachable only
        // when the face changed under a parked focus — the walk below skips
        // greyed stops — and the dispatch's live re-ask is the second wall.
        if (!dlg.buttons[static_cast<size_t>(at)].enabled) return true;
        // A SYNTHESIZED REPEAT IS CONSUMED AND CHANGES NOTHING — the act
        // happens once, at the physical release. repeat_eligible refuses to
        // ARM these two while the focus is on a button, so this arm should see
        // no repeats at all; it is here because the eligibility argument is
        // about a state that could in principle be entered with a hold already
        // armed, and "fires once" should be true by construction rather than
        // by that argument.
        if (mods.synthesized_repeat) return true;
        if (app.modal_dialog_key_pressed != at ||
            app.modal_dialog_key_pressed_key != key) {
            app.modal_dialog_key_pressed     = at;
            app.modal_dialog_key_pressed_key = key;
            viewport.invalidate_rect(dlg.box);
        }
        return true;
    } else {
        return false;
    }

    // THE RING SKIPS A GREYED BUTTON (architect 2026-08-30: the transport
    // keys are their own class; the router and the ring consume what the
    // face refuses — there is no roster Tab behaviour to mirror, the icon
    // rows taking no keyboard at all, so the modal ring's own rule is the
    // one stated here): a Tab walk that lands on a disabled button advances
    // again, in its own direction and by its own wrap rules, until an
    // enabled stop. The LIST stop and an editor's field are always stops,
    // and Close never greys, so the walk terminates; the guard bounds it
    // against the impossible all-disabled ring anyway. The bit read is the
    // STASH'S — the walk only SELECTS a focus, and the press it leads to
    // re-asks the live predicate at dispatch. Left / Right are not gated:
    // they walk the prompt ring alone, whose buttons carry no enabled
    // split.
    if (tab_shape != ModalRingTab::None) {
        int guard = 0;
        while (next >= 0 &&
               !dlg.buttons[static_cast<size_t>(next)].enabled &&
               guard++ <= n) {
            if (tab_shape == ModalRingTab::Forward) {
                if (next + 1 < n) ++next;
                else { next = prompt_up ? 0 : -1; list_next = list_up; }
            } else {
                if (next > 0) --next;
                else { next = prompt_up ? n - 1 : -1; list_next = list_up; }
            }
        }
    }

    // LEAVING THE BUTTON CANCELS A HELD ENTER OR SPACE (2026-08-14), the
    // rule's first site and the reason it exists: the arm names the button the
    // focus was on, and the user has visibly left it. Without this the release
    // still committed the OLD button — focus OK, hold Space, Tab onto Cancel,
    // release, and OK fired — and the pressed face and the ring pointed at
    // different buttons for the rest of the hold. The rule, the resulting
    // armed == focused invariant and how it differs from the pointer's FEINT
    // are at AppState::modal_dialog_key_pressed. It reads the INDEX alone: a
    // one-button ring whose walk lands back where it started has left nothing,
    // and only the focus's STRENGTH changed there.
    const bool left_the_button = next != app.modal_dialog_focus;
    if (left_the_button ||
        app.modal_dialog_focus_active != (next >= 0)) {
        if (left_the_button) clear_modal_dialog_key_press();
        app.modal_dialog_focus = next;
        // Landing on a button by a deliberate walk IS the active strength;
        // landing back on an editor's field carries none.
        app.modal_dialog_focus_active = next >= 0;
        viewport.invalidate_rect(dlg.box);
    }
    // THE LIST BIT, written on the same walk: the highlight's outline says
    // which strength the list has (the accent while the ring is on it, the
    // passive line otherwise), so the band repaints with it.
    if (list_up && app.folder_overlay.list_focused != list_next) {
        app.folder_overlay.list_focused = list_next;
        viewport.invalidate_rect(folder_overlay::surface_rect(app));
    }
    return true;
}

// THE KEYBOARD PRESS ARM'S RELEASE — the act, at the lift (the ruling and the
// arm are at AppState::modal_dialog_key_pressed). Called from on_key_release
// for every delivered key release, above every other gate: this arm belongs to
// a modal surface that owns input, so nothing may rank above it, and a release
// that matches nothing costs one integer compare.
//
// IT MATCHES ON THE KEY, so releasing the other of the two keys resolves
// nothing, and it re-asks the dialog's own gates through the SHARED DISPATCH
// (dispatch_modal_dialog_button) rather than a third copy of them: if the
// dialog changed under the hold — the owner moved, the prompt was replaced —
// the act does not fire. The painter has usually dropped the arm outright on
// those same edges; this is the second wall, exactly as the pointer's release
// is.
// MODIFIERS AT THE RELEASE ARE NOT RE-READ, the pointer release's own rule (it
// takes the platform's modifier state and names it unused): the PRESS is what
// is bare-exact, and a shift tapped mid-hold does not turn a committed press
// into something else.
void GuiInputHandler::on_key_release(GuiKey key) {
    // KEYPAD ENTER IS RETURN at both ends of the arm — the press stored
    // Return through the same owner, so the release has to ask the same
    // question or a KpEnter hold could never complete (modal_ring_press_key).
    key = modal_ring_press_key(key);
    const int armed = app.modal_dialog_key_pressed;
    if (armed < 0 || app.modal_dialog_key_pressed_key != key) return;
    app.modal_dialog_key_pressed     = -1;
    app.modal_dialog_key_pressed_key = 0;
    if (app.modal_dialog.valid)
        viewport.invalidate_rect(app.modal_dialog.box);
    dispatch_modal_dialog_button(armed);
}

// Shared key route for EVERY keyboard-modal editor — the settings prompt, the
// commit-title editor, the measure paste-offset editor, the bpm bracket
// editor, and the top-strip flag editor.
// All five spell ONE modal contract: the on_key gate (modal_editor_key_blocked)
// admits only the editor's own keys plus bare Esc, Ctrl+S, and Ctrl+Q, so a
// NotConsumed key here is one of the latter two chords. Ctrl+S saves with
// the editor left open (save is not an exit); Ctrl+Q returns false so on_key
// runs the close routing, WHICH IS WHAT CLOSES THE EDITOR (the close road's
// own step, close_modal_editors_no_commit — reached identically by the
// compositor's keyless WM close, which is why this route no longer carries a
// teardown hook of its own); anything else is swallowed as a backstop. THE COMMAND ADMISSION IS KEYBOARD-ONLY
// AGAIN (2026-08-13): it had a pointer-side mirror from 2026-08-11 — the
// modal-trap reach-through, which let a roster button whose chord is admitted
// here dispatch from a press while a dialog editor stood — and the architect
// retired it once every dialog grew real OK and Cancel buttons, so nothing
// outside this file restates this set and the veil swallows every roster
// press. Ctrl+S here is unchanged. `autocomplete` is the optional
// bare-Tab hook, PASSED BY THE SETTINGS EDITOR (the commit-title, measure
// offset, bpm and flag editors have no vocabulary to complete and pass an
// empty hook).
//
// THE ONE AUTOCOMPLETE MODEL — the authoritative statement, architect
// 2026-08-13: "how about first tab autocompletes, second tab (or tab on a
// non-matching string, i.e. a string that does not autocomplete) moves focus?
// And we revert the settings editor to that model also — we should use one
// model for all autocompletes."
//
// BARE TAB WITH THE FOCUS IN THE FIELD OFFERS THE COMPLETION FIRST, AND THE
// COMPLETION'S OWN ANSWER DECIDES: it reports whether it ADVANCED THE BUFFER —
// advanced, and the key is consumed; did not, and the key falls through to the
// focus ring. "Second Tab" and "Tab on a string that completes to nothing" ARE
// THE SAME CONDITION and need no state to tell apart: after a successful
// completion the next Tab cannot advance (the settings editor's value side is
// no longer empty), so it walks by that one test. THERE IS NO PRESS COUNTER, NO TIMER
// AND NO "last key was Tab" BIT anywhere in this model, deliberately — wanting
// one means the rule has been mis-derived.
//
// THE ORDERING, in full:
//   * Tab with the focus ON A BUTTON walks the ring — a button has no field to
//     complete, so the hook is not even offered (the test is
//     modal_dialog_focus < 0, and -1 IS the field on an editor dialog;
//     AppState::modal_dialog_focus owns that meaning).
//   * Tab with the focus IN THE FIELD offers the completion, then walks.
//   * Tab in a dialog editor with NO hook (commit-title, measure offset, BPM)
//     walks at once.
//   * SHIFT+TAB NEVER COMPLETES — it is the ring's REVERSE WALK and nothing
//     else (architect 2026-08-13: "also, shift+tab should cycle backward"),
//     so the completion is not offered on it at any site: this arm tests the
//     FORWARD shape alone, and shift means "go back", never "complete". Its
//     spellings are the live marker cycle's, one predicate away at
//     modal_ring_tab_shape (bare Tab forward, Shift+Tab and IsoLeftTab back,
//     the latter shift-agnostic because that keysym rides the Tab key's shift
//     level).
//   * The top-strip FLAG editor is not a dialog: it publishes no buttons, has
//     no ring, and its whole Tab family drops at that gate before this route
//     sees it.
// modal_editor_key_blocked's ADMITTED SET grew with the reverse walk and only
// with it — bare Tab was already admitted for every dialog editor when the ring
// landed, and the reverse spellings joined it at the same one term, read from
// the same predicate this arm reads.
//
// EVERYTHING ELSE THE RING TOOK is unchanged: Left / Right / Enter change
// meaning ONLY while the focus is on a BUTTON — a state that did not exist
// before it — so every key's behaviour with the focus in the field is
// byte-identical to what it has always been.
// `repaint` is the caller's text-change damage and is REQUIRED — unlike
// `autocomplete` it is called unconditionally, with no emptiness test: the four
// dialog surfaces pass invalidate_modal_dialog_area (the bottom row's lane,
// which IS the modal's surface, viewport.cpp), the top-strip flag editor
// invalidate_top_strip. Commit and cancel own their own invalidations.
bool GuiInputHandler::route_modal_editor_key(
        text_editor::State& ed, GuiKey key, GuiInputState mods,
        const std::function<bool()>& autocomplete,
        const std::function<void()>& commit,
        const std::function<void()>& cancel,
        const std::function<void()>& repaint) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    // THE FIELD'S FIRST REFUSAL ON THE FORWARD TAB (the one autocomplete model
    // above): offered only with the focus in the field, and consuming the key
    // only when it advanced the buffer. A false answer falls into the ring
    // below, which is where a second Tab, an unknown key and an ambiguous
    // prefix all land — as does every REVERSE shape, which is never offered
    // here at all.
    if (autocomplete &&
        modal_ring_tab_shape(key, mods) == ModalRingTab::Forward &&
        modal_dialog_focus_live() < 0 && autocomplete()) {
        return true;
    }
    if (route_modal_dialog_focus_key(key, mods)) {
        // THE CARET RESTARTS WHEN THE RING WALKS BACK ONTO THE FIELD: the blink
        // keeps its own clock, and the field's caret stops painting while the
        // focus is on a button, so a field re-focused mid-period could
        // otherwise show nothing for up to half a second. One site, with the
        // editor already in hand — the ring's other caller is the prompt gate,
        // which has no field at all.
        if (app.modal_dialog_focus < 0) text_editor::touch_blink(ed);
        return true;
    }
    // NO KEY REACHES THE FIELD WHILE THE FOCUS IS ON A BUTTON (architect
    // 2026-08-13, at his live test: "hit Tab twice more to reach Cancel, then
    // press Space. I expect Cancel to be pressed, but instead a space character
    // is added to the text field even though the text field has now lost
    // focus"). Not typing, not motion, not editing: the buffer and the
    // selection are preserved untouched and the keys simply do not arrive,
    // which is what focus MEANS on the other surfaces this product has.
    // Anything the ring above did not claim is a CONSUMED NO-OP here — EXCEPT
    // the modal contract's own three commands, which work from anywhere in the
    // dialog and keep working from a button exactly as they do from the field:
    // bare Esc abandons the edit, Ctrl+S saves with the editor open, Ctrl+Q
    // hands the close routing on. They are spelled here
    // as the tail below spells them, because this wall stands ABOVE the
    // editor's own keymap and the tail is unreachable from a focused button.
    // (The FLAG editor cannot be in this branch: it publishes no dialog, so its
    // ring is empty and the live focus is structurally -1 while it stands.)
    // IT READS THE LIVE FOCUS, never the raw index (modal_dialog_focus_live,
    // input_handler.h): in the one dispatch batch between an editor's open and
    // its first paint the raw index still names the PREVIOUS dialog's buttons,
    // and a wall keyed on it would swallow the new editor's first keystrokes
    // at a field the user is looking at and typing into.
    if (modal_dialog_focus_live() >= 0) {
        if (!ctrl && !shift && !alt && key == GuiKeys::Escape) {
            cancel();
            return true;
        }
        if (ctrl && !shift && !alt && key == GuiKeys::S) {
            save_ops.save();
            return true;
        }
        if (ctrl && !shift && !alt && key == GuiKeys::Q) {
            return false;  // let on_key run the close routing
        }
        return true;       // modal: swallow, and the field never sees it
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
    // THE FIELD HAD NO ROOM FOR THE CHARACTER (architect 2026-08-30): consumed
    // and repainted exactly as Consumed is — the editor has already set red and
    // left the buffer alone — plus the card the red field cannot say. This is
    // the ONE place every editor's keys pass through, which is why the sentence
    // sits here and not once per editor.
    if (action == text_editor::KeyAction::OverCapacity) {
        repaint();
        notifications.notify(AppState::NotificationClass::Normal,
                             "This field is full");
        return true;
    }
    if (ctrl && !shift && !alt && key == GuiKeys::S) {
        save_ops.save();
        return true;
    }
    // CTRL+Q: HAND THE CLOSE ROUTING ON, TEARING NOTHING DOWN HERE. The
    // editor is closed by the CLOSE ROAD itself (GuiPrompt::request_close,
    // through close_modal_editors_no_commit) — the road the compositor's own
    // WM close takes too, which is why the step cannot live on the keyboard's
    // side of it. The edit is abandoned uncommitted either way, exactly as Esc
    // abandons it.
    if (ctrl && !shift && !alt && key == GuiKeys::Q) {
        return false;  // let on_key run the close routing
    }
    return true;  // modal: swallow
}

// WHAT CLOSES BEFORE THE QUIT QUESTION, the editors' half — the contract and
// the one caller are at the declaration (input_handler.h).
//
// THE STEP LIVES ON THE CLOSE ROAD AND NOT ON THE KEYBOARD'S SIDE OF IT
// (2026-08-28): Ctrl+Q used to tear its own editor down through a per-editor
// hook on the shared modal route, and the COMPOSITOR'S CLOSE — which arrives
// with no key at all (main.cpp's set_on_close) — reached GuiPrompt::request_close
// without it, leaving a standing editor alive beneath the unsaved-work
// question and back on screen at Cancel. One road, one closer, the render
// player's own close's argument exactly.
//
// EACH EDITOR IS ABANDONED THROUGH ITS OWN EXIT BODY, never through a second
// spelling of what abandoning means: those bodies own their damage (the flag
// editor's red-flash waveform arm among it) and their is_active guards, so the
// chain is a no-op when nothing stands. THE PICKER IS NOT AN EDITOR and is
// not here: the close road takes it down through close_picker beside the
// player's line, one close body each.
void GuiInputHandler::close_modal_editors_no_commit() {
    // THE BPM MODE GOES WITH ITS EDITOR, and the kind is read BEFORE the exit
    // that clears it: the bracket session IS its editor, so mode-without-editor
    // stays unreachable (the mode's one off-chokepoint is exit_bpm_mode).
    const bool bpm_bracket =
        text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::BpmBracket;
    flag_editor.exit_top_flag_edit_no_commit();
    if (bpm_bracket) flag_editor.exit_bpm_mode();
    settings_editor.exit_no_commit();
    commit_title_editor_exit_no_commit();
    measure_offset_editor_exit_no_commit();
}

// -- THE PICKER (the contract is at the declaration) --------------------------
//
// Two openers, two list builders, two open acts, and the machinery both
// contents share: the highlight, the close and the key router.

// The Open project picker's opener. Every guard returns without touching
// playback (a refused open never interrupts a listening session); the shared
// modal stop runs only once the picker is definitely opening. TWO ROADS REACH
// IT and both are the same chord: Ctrl+O's own dispatch arm in on_key, and
// the File menu's Open project row, whose release dispatches that chord
// through on_key with the popup already closed. The guards below serve both —
// the `h` view's, which the key road also meets one gate earlier at
// history_mode_key_blocked, and the modal and loading refusals, which the key
// road meets earlier still.
void GuiInputHandler::open_project_picker() {
    // A PROMPT IS SILENT AND AN EDITOR IS NOT (architect 2026-08-30): a
    // prompt VEILS everything and is itself the answer on screen — its
    // question is what the press has to deal with — while an editor is
    // pointer-transparent in one of its five kinds (the top flag editor), so
    // the File menu's Open project row is reachable under it and owes a
    // sentence. The KEY road never reaches either arm: Ctrl+O under any
    // editor dies at on_key's editor gate, which says these very words with
    // the chord named, and under a prompt at the prompt's own claim above it.
    if (app.prompt.active) return;
    if (keyboard_modal_editor_active()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             "Close the editor first");
        return;
    }
    // (THE `h` HISTORY VIEW'S REFUSAL IS DELETED — architect 2026-08-29,
    // "admit both": the File menu's three rows are all live in the view now,
    // and Ctrl+O joined the mode's allowlist beside Ctrl+Q with it. The
    // premise was that "a reopen would tear the view down from under itself",
    // and the answer is that TEARING IT DOWN IS WHAT A REOPEN DOES to
    // everything — Ctrl+Q, which is admitted, ends the view the same way. The
    // picker stands OVER the view (its router runs ahead of the mode's gate in
    // on_key, its veil consumes the view's presses, and its band is the
    // waveform's lower half, clear of the diff lane), a Cancel or Esc leaves
    // the view exactly as it stood — this view owns no navigation state — and
    // a successful open reaches the reopen through the one close request,
    // whose teardown joins the prefetch and the commit worker with everything
    // else per project. What the act still refuses in here is what it refuses
    // everywhere: a publishing checkpoint, at open_project_commit.)
    // THE RENDER PLAYER IS CLOSED, NOT TOLD TO CLOSE (architect 2026-09-02,
    // with the File anchor staying lit under the panel): the row and the chord
    // are reachable in there now — the anchor is admitted through the player's
    // veil and route_render_player_key lets Ctrl+O fall through — so a face
    // that opens a menu whose row then refuses would be the face promising
    // more than the act delivers. The two modes never stand together, and
    // CLOSE-THEN-OPEN is what keeps that true: `close()` is the one close body
    // (it rebinds the VIEW's buffer before freeing the item's, so the reopen
    // this may lead to never tears a bound buffer out from under the engine),
    // and the picker then opens on the ordinary state. The refusal it replaces
    // — "Close the render player first", 2026-08-30 to 2026-09-02, the one of
    // the four gates here that ever spoke — is retired with the reason it
    // rested on.
    if (render_player_active()) render_player.close();
    // A STANDING PICKER IS STILL SILENT: the picker IS this act, so a second
    // Open project over one asks for what is already on screen — the settings
    // editor's own already-open silence, and a one-dimensional refusal at the
    // state it refuses on. Its key road answers earlier (route_picker_key
    // consumes Ctrl+O in its catch-all); what reaches here is the File menu's
    // row, raised over the picker's own band.
    if (picker_active()) return;
    if (app.loading) return;

    playback_lifecycle.stop_playback_for_modal_open();
    app.picker.active  = true;
    app.picker.session = text_editor::next_session_id();
    // THE BAND RISES WITH THE PICKER and IS its whole state: the rows and
    // the highlight, nothing beside them. On glass the band takes the
    // on-screen keyboard's place, so the picker shows THE LIST there and no
    // keyboard — the architect's R3, "neither use needs typing", made
    // literal by R22.
    build_project_picker_rows();
    // A modal OPEN damages the whole window (the row's rect does not exist
    // before its first paint — the settings opener carries the rule).
    viewport.invalidate_all();
}

void GuiInputHandler::build_project_picker_rows() {
    AppState::FolderOverlay& ov = app.folder_overlay;
    ov       = AppState::FolderOverlay{};
    ov.owner = AppState::FolderOverlay::Owner::ProjectPicker;
    // ONE LIST, AND THIS BODY IS ITS ONE OWNER: the folder names under
    // projects_path are enumerated here (in the model's own order,
    // project_model.h), filtered by the model (resolve_project), and the
    // survivors become the band's ROWS. A row that would refuse at the open
    // is not a row, so the band and the act AGREE BY CONSTRUCTION: an invalid
    // folder simply does not show up (architect R8). THE NAME GRAMMAR IS NOT
    // ASKED HERE: is_last_project_name (device_config.h) is the ENUMERATION's
    // own membership rule now — a folder the device config cannot name is not
    // a project on any opening road — so the walk this loop reads has already
    // dropped those names, and a second copy of the test here would be a
    // duplicate predicate with no producer of its own. The act's remaining
    // arms are not filters either: the same-project no-op is a legal answer
    // and the dry run reads the disk, which this walk does not re-do per row.
    // No `..` row and no folder inside a project: the picker's tree is one
    // level deep. BUILT AT THE OPEN AND NEVER KEPT FRESH — a project that
    // appears or vanishes while the picker stands shows at the next open.
    const std::filesystem::path root(app.device_config->projects_path);
    for (const std::string& name :
             enumerate_project_names(app.device_config->projects_path)) {
        const std::filesystem::path folder = root / name;
        if (!resolve_project(folder)) continue;
        AppState::FolderOverlayRow row;
        row.kind = AppState::FolderOverlayRow::Kind::Folder;
        row.name = name;
        row.path = folder;
        ov.rows.push_back(std::move(row));
    }
    // THE BAND OPENS ON THE CURRENT PROJECT — name to name on both sides, the
    // model's resolved folder name here and the same string assigned verbatim
    // by the field's one producer there (AppState::project_name). Row 0 when
    // the open project is somehow not listed (a folder renamed under a
    // running session), which the same-project no-op then simply never
    // takes.
    int current = 0;
    for (size_t i = 0; i < ov.rows.size(); ++i) {
        if (ov.rows[i].name == app.project_name) {
            current = static_cast<int>(i);
            break;
        }
    }
    folder_overlay::clamp_scroll(app);
    folder_overlay::set_highlight(app, current);
}

// THE OPEN ACT on row `index`. A publishing checkpoint refuses first, then
// validity through the project model, then the strict sidecar dry-run, each
// refusal a notification card with the picker still open; the project already
// open is a consumed no-op that closes it; and a project that passes reaches
// the reopen through the one close request, REOPEN-targeted.
// (THE CHECKPOINT SENTENCE lives at the head of this file since 2026-08-30 —
// bare `h`'s own entry refusal became its third reader that day.)

void GuiInputHandler::open_project_commit(int index) {
    if (!app.picker.active) return;
    if (app.folder_overlay.owner !=
        AppState::FolderOverlay::Owner::ProjectPicker) return;
    const int n = static_cast<int>(app.folder_overlay.rows.size());
    if (index < 0 || index >= n) return;
    const std::string name =
        app.folder_overlay.rows[static_cast<size_t>(index)].name;

    // THE THREE REFUSALS BELOW ARE NOTIFICATION CARDS (2026-08-29), visible
    // in the `h` view like anywhere else — they were the status chain's
    // transient tier for one day, invisible under the view's own line — and
    // the picker STAYS OPEN on every one of them, which is the answer the
    // user can act on: press another row, or Esc.
    auto refuse = [&](const std::string& reason) {
        notifications.notify(AppState::NotificationClass::Normal, reason);
    };

    // NOT WHILE A CHECKPOINT IS PUBLISHING (2026-08-29), bare `h`'s own
    // refusal one act over and in the same words. A reopen JOINS the commit
    // worker in the teardown (main.cpp's run_project) — and the teardown
    // forgets the completion fd first, so the worker's verdict never reaches
    // on_history_checkpoint_complete: a CommittedNotPushed or CommitFailed
    // would go to stderr alone, invisible on the tablet, with the critical
    // chip that verdict owes the user never painted. The wait is seconds and
    // the bit falls by itself. THE PICKER STAYS OPEN, like its other two
    // refusals, so the answer is one line and another Enter.
    if (app.history_checkpoint_in_flight) {
        refuse(kCheckpointPublishing);
        return;
    }

    // The row's name is ONE folder name by construction (the enumeration's own
    // membership rule, project_model.h), so the model is the first real test.
    const std::filesystem::path folder =
        std::filesystem::path(app.device_config->projects_path) / name;
    auto project = resolve_project(folder);
    if (!project) {
        refuse(project.error());
        return;
    }
    if (project->name == app.project_name) {
        // Choosing the project that is open: nothing to reopen, the picker
        // simply closes. NAME TO NAME on both sides — the model's resolved
        // folder name here, and the same string assigned verbatim by the
        // field's one producer there (AppState::project_name) — so the no-op
        // cannot be missed by a link in the way of either spelling.
        //
        // AND IT IS SILENT (architect 2026-08-31, retiring the one-day card
        // "That project is already open"): a benign one-dimensional refusal
        // already at its state says nothing — the window title names the
        // project the picker just closed over, which is one glance at one
        // place. The CLOSE is unchanged; only the sentence left.
        close_picker();
        return;
    }
    if (auto reason = source_load_dry_run(project->source)) {
        refuse(*reason);
        return;
    }

    // THE REOPEN. The picker closes; the chosen name is seated for gui_main's
    // loop; and the close request runs with the REOPEN target — the
    // unsaved-tab prompt exactly as Ctrl+Q's when the tab is dirty, an
    // immediate completion when it is clean. The prompt's Cancel leaves the
    // seated name behind harmlessly: the loop reads it only after run()
    // returns, and run() returns for a reopen only through this request's own
    // completion.
    // A RUNNING RENDER IS KILLED WHERE THE REOPEN IS CERTAIN, not here: this
    // act only ASKS to close, and the question can be answered Cancel — a kill
    // spelled at the click would have taken a running sweep down for a reopen
    // that never happened. The teardown's own worker join kills it (main.cpp's
    // run_project), on the reopen road and the exit road alike, so both
    // completions kill exactly once and nothing here needs a kill of its own.
    close_picker();
    app.reopen_project = project->name;
    prompt.request_close(GuiCloseTarget::Reopen);
}

// THE `h` VIEW'S LOAD IN PLACE — bare `'` there, and its road alone (architect
// 2026-08-28: "we can keep the single quote for load in place because that
// still works in the history view"). IT ACTS ON THE VIEWED MEMBER WITH NO LIST
// (architect 2026-08-29, retiring the one-day history picker as
// "overengineered; I don't really need it"): `,` and `.` already walk the
// members and the lane already shows the one the walk stands on, so the key
// raises THE CONFIRMATION straight away — the render player's own prompt body
// with the walk member as its subject, "Load '<member label>' in place?",
// OK / Cancel with Enter answering OK.
//
// THE MODE IS THE CALLER'S GATE rather than a term of this body: the one key
// caller is on_key's `'` arm under history_mode.active, which is where the
// fork between this act and the player lives. THE EMPTY WALK IS THE
// ALLOWLIST'S: the mode opens on an EMPTY commit walk, and the honest gate
// against a walk with no member is the `'` admission's own term
// (history_mode_key_blocked — the chord is refused and the icon row's load
// button greys from the same line), restated below as a guard so the body is
// correct from any caller. No-op with no source loaded. Stops playback only
// when the prompt actually opens (after every guard), so a refused raise
// leaves a listening session running.
void GuiInputHandler::history_load_in_place() {
    if (!app.history_mode.active) return;
    if (app.prompt.active || keyboard_modal_editor_active()) return;
    if (render_player_active() || picker_active()) return;
    if (app.source_audio_path.empty()) return;
    const std::size_t count = app.history_mode.walk_count();
    if (count == 0) return;
    const std::size_t member = app.history_mode.walk_index();
    if (member >= count) return;

    playback_lifecycle.stop_playback_for_modal_open();
    // THE SUBJECT IS PARKED, not the act: the prompt answers back through the
    // input-handler back-pointer and confirm_load_in_place forks on which of
    // the two subjects is standing (the player's entry, or this member).
    app.history_mode.pending_load_member = member;
    // The member in the ONE spelling both walks share (member_label — the
    // short SHA on the Remote tab, the displayed number on the Local one), so
    // the question names the member exactly as the mode's own corner does.
    // Cancel LAST, the escape sentinel every prompt derives its Esc from; the
    // FIRST button takes the passive focus, this prompt's own choice on both
    // its subjects (PromptInitialFocus).
    app.prompt.present(
        "Load '" + app.history_mode.member_label(member) + "' in place?",
        {'o', '\x1b'},
        {"OK", "Cancel"},
        DialogTrigger::LOAD_IN_PLACE_CONFIRM,
        PromptInitialFocus::FirstButton);
    viewport.invalidate_all();
}

// -- The picker's shared machinery --------------------------------------------

// ENTER ON THE LIST — the keyboard's own click on the highlight, through the
// one row-act fork (folder_overlay_open_row, input_pointer.cpp, which the row
// click's motionless lift reaches too). A -1 highlight is a consumed no-op.
// The row's own OK button went with the ruling that a click activates: the
// picker's row is Cancel alone.
void GuiInputHandler::picker_open_highlight() {
    if (!app.picker.active) return;
    const int highlight = app.folder_overlay.highlight_row;
    if (highlight < 0) {
        // NO HIGHLIGHT IS AN EMPTY BAND, and it says so (architect
        // 2026-08-30): the builder seats the highlight on the current
        // project, or on row 0, so the only way to reach here is a listing
        // with no rows at all — every folder under projects_path refused by
        // the model, or none there. Enter on an empty band would otherwise
        // read as a dead key over a panel that is plainly standing.
        notifications.notify(AppState::NotificationClass::Normal,
                             "Choose a project first");
        return;
    }
    folder_overlay_open_row(highlight);
}

// The widget's two highlight mechanics (folder_overlay.h owns the clamps and
// the scroll-into-view) with the picker's damage on top — the band as it
// stands, which is the whole of what a moved highlight touches.
void GuiInputHandler::picker_set_highlight(int index) {
    if (!app.picker.active) return;
    if (folder_overlay::set_highlight(app, index))
        viewport.invalidate_rect(folder_overlay::surface_rect(app));
}

void GuiInputHandler::picker_move_highlight(int delta) {
    if (!app.picker.active) return;
    if (folder_overlay::move_highlight(app, delta))
        viewport.invalidate_rect(folder_overlay::surface_rect(app));
}

// THE ONE CLOSE BODY (the caller inventory is at the
// declaration). THE OVERLAY LEAVES WITH THE PICKER: the reset restores
// Owner::None, which IS the band's standing predicate answering false. The
// damage is the whole window, as the open's is — the band's pixels sit over
// the waveform, the modal row goes back to its tenants and the roster's grey
// lifts with them.
void GuiInputHandler::close_picker() {
    if (!app.picker.active) return;
    app.picker         = AppState::Picker{};
    app.folder_overlay = AppState::FolderOverlay{};
    viewport.invalidate_all();
}

// THE PICKER'S KEY ROUTER — the whole plastic vocabulary while a picker
// stands, in route_render_player_key's shape (the contract is at the
// declaration).
bool GuiInputHandler::route_picker_key(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    const bool bare  = !ctrl && !shift && !alt;

    // CTRL+S SAVES WITH THE PICKER STANDING, through the one save owner — the
    // contract the typed prompt this picker replaced carried
    // (route_modal_editor_key's Ctrl+S arm). THE ONE REFUSAL THE SAVE OWNER
    // CAN MEET HERE SAYS SO (architect 2026-08-30, the strictness ruling): a
    // checkpoint in flight, asked ahead of the call so the sentence can be
    // raised — and it is the PICKER'S OWN sentence, the literal it shares
    // with open_project_commit's identical refusal. Elsewhere that same
    // refusal is silent because the Save button reads "Committing..." and is
    // the message; under the picker the whole roster is greyed and unreadable
    // as state, so nothing else answers the press. THE SAVE OWNER'S OTHER
    // REFUSALS SAY THEMSELVES since 2026-09-02: the three write arms and the
    // numeric-locale arm raise their own card from inside save() (save_ops.cpp
    // — one composer, every caller inheriting it), so this arm asks about
    // nothing but the bit it can answer better than the owner can, and the
    // picker composes no second sentence for a failure. The empty-sidecar-path
    // belt keeps its silence, having no producer.
    if (ctrl && !shift && !alt && key == GuiKeys::S) {
        if (app.history_checkpoint_in_flight) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kCheckpointPublishing);
            return true;
        }
        save_ops.save();
        return true;
    }
    // THE TWO FALL-THROUGHS: Ctrl+Q falls through to the ordinary quit road,
    // which takes the picker down at its head (GuiPrompt::request_close) —
    // the same step the compositor's close takes, stated once there rather
    // than here, so the two roads cannot drift; and BARE BACKSLASH since
    // 2026-09-02, the player's own admission one mode over — the File anchor
    // is lit under the panel, so Synchronize's row works in here and its key
    // must answer the same way. CTRL+O IS DELIBERATELY NOT ONE: it is this
    // very act, and a second Open project over a standing picker asks for what
    // is on screen, so it falls to the modified catch-all below and is
    // consumed SILENTLY — the one-dimensional refusal at the state it refuses
    // on, which is also what the File row answers with (open_project_picker's
    // picker arm returns without a word).
    if (ctrl && !shift && !alt && key == GuiKeys::Q) return false;
    if (is_sync_external_key(key, mods)) return false;

    // THE RING: Tab / Shift+Tab walk [list, Cancel] through the one modal
    // ring route, whose list arm the player and the picker share. A
    // RING-FOCUSED BUTTON takes Enter and Space as its own press
    // (press-at-press, commit-at-release — the route above already armed it
    // and returned true); reaching the switch below with the focus on a
    // button means the key was not one of the ring's.
    if (route_modal_dialog_focus_key(key, mods)) return true;

    // EVERY OTHER MODIFIED CHORD: CONSUMED, AND SILENTLY (architect
    // 2026-08-30, the unbound-keys ruling — "bound keys either show an effect
    // or a card, so an unbound key is identified by its silence"), the
    // player's own arm one mode over and for its reason: the router is the
    // whole vocabulary while the picker stands, and a chord it does not name
    // is a chord that does nothing here. It carded "<chord> is not bound in
    // the project list" for the one day of 2026-08-30.
    if (!bare) return true;

    switch (key) {
        case GuiKeys::Escape:
            close_picker();
            return true;
        case GuiKeys::Return:
        case GuiKeys::KpEnter:
            // With no button focused, Enter OPENS the highlight — the row
            // click's own act from the keyboard.
            if (mods.synthesized_repeat) return true;
            picker_open_highlight();
            return true;
        case GuiKeys::Up:
            picker_move_highlight(-1);
            return true;
        case GuiKeys::Down:
            picker_move_highlight(+1);
            return true;
        default:
            // Space with no button focused, Left / Right, and every other
            // bare key: consumed, and silent with the modified arm above. The
            // picker has no bare act for them.
            return true;
    }
}

// -- SYNCHRONIZE TO EXTERNAL STORAGE (the contract is at the declaration) ---

// The act's GUI half: the gates, the destination, the capture, the dispatch.
// TWO ROADS REACH IT and neither restates anything: the File menu's
// Synchronize row, whose release has already closed the popup and calls this
// directly, and BARE BACKSLASH on the ordinary dispatch since 2026-08-31
// (is_sync_external_key, gui_input.h). The gates below are what let the row
// have no chord of its own for four days and what the key needs none of its
// own beyond its place in on_key.
void GuiInputHandler::synchronize_to_external_storage() {
    // The Open project row's own gates, mirrored: a menu row's refusals belong
    // to the menu, not to the act. Each returns without touching playback —
    // and neither does the act itself, which is silent and changes no audio.
    // (THE `h` VIEW'S REFUSAL WENT WITH THE OPEN ROW'S — architect 2026-08-29,
    // "admit both", so the File menu has no dead row in the view. This act was
    // always the easier admission of the two: it is READ-ONLY-LEGAL already,
    // authors nothing, stops no playback and writes outside the project
    // entirely, so a viewer running it is a viewer copying files.)
    if (app.prompt.active || keyboard_modal_editor_active()) return;
    if (app.loading) return;
    // Nothing loaded is nothing to mirror.
    if (app.source_audio_path.empty() || app.project_name.empty()) return;

    // EVERY SENTENCE THIS ACT WRITES IS A NOTIFICATION CARD (2026-08-29) —
    // the unset-key refusal, the already-running answer and the worker's
    // FAILURES on the way back (on_external_sync_complete) — visible in the
    // `h` view like anywhere else; they were the status chain's transient
    // tier for one day, invisible under the view's own line. THE DISPATCH
    // NOTICE IS DROPPED with the move: "Synchronizing to …" was a process
    // line, state rather than an event, and the design gives the mirror no
    // bar cell. AND SINCE 2026-08-30 A SUCCESS SAYS NOTHING AT ALL (the
    // architect's ruling, the render's precedent), so the only sentences left
    // are refusals — which is why this act, when it works, is one menu press
    // and silence. THE WORKER'S FAILURES ARE STILL LOUD on stderr too —
    // run_external_sync's one refusal owner writes every failing line there
    // beside the verdict (logcat on the tablet).
    auto report = [&](std::string line) {
        notifications.notify(AppState::NotificationClass::Normal, std::move(line));
    };

    // SINGLE ACT IN FLIGHT, answered in words: the menu item never greys (the
    // standing rule at kFilePopupItems), so a second row press while one act
    // runs is a consumed no-op that says which one it was.
    if (external_sync_worker.is_busy()) {
        report("A synchronization is already running");
        return;
    }

    // THE DESTINATION IS THE DEVICE CONFIG'S, told and not found (architect
    // 2026-08-30): `sync_path`, the loop's one DeviceConfig, read here and
    // handed to the job whole — the act composes `<sync_path>/<project
    // name>/` and looks for nothing. IT WAS THE SEAM'S ANSWER for three days
    // (`GuiPlatform::removable_volume`, deleted with its two backends'
    // discoveries): the finding rule worked on the laptop and could not work
    // on the tablet at all, and where a machine's removable storage is
    // mounted is exactly the kind of per-device fact that file is for
    // (device_config.h).
    //
    // AN EMPTY KEY IS THE DEVICE SAYING IT HAS NO DESTINATION — "not set up
    // on this device", the first-run template's own value — and the card
    // names THE KEY BY ITS SPELLING (`sync_path is not set`): a config key is
    // named the way it is written in the file everywhere in the product, so
    // the sentence tells the reader exactly what to add. Nothing runs.
    const std::string& sync_path = app.device_config->sync_path;
    if (sync_path.empty()) {
        report("sync_path is not set");
        return;
    }

    // THE JOB, captured whole by value on this thread. TWO FOLDERS AND NO
    // TITLE (architect 2026-09-02): the job carries `render/` and `tmp/`
    // themselves — the deliverable folder through the parser's one owner, the
    // batch root through the GUI's — and the act LISTS them, so the set it
    // mirrors is the set the disk holds. The title is not a term of it: a
    // composed `render/<title>.wav` made the mirror's set and
    // prune_render_folder's definition (renders_dir.h) two rules that had to
    // agree, and between a retitle and the next render they did not — the disk
    // kept the old wav, the stick lost it and nothing was copied in its place.
    // The prune is still what keeps the folder to one pair; it simply runs on
    // its own trigger now, and the next Synchronize follows the folder there.
    GuiExternalSyncJob job;
    job.sync_root    = std::filesystem::path(sync_path);
    job.project_name = app.project_name;
    job.render_root  = render_output_directory(app.source_audio_path);
    job.batch_root   = project_batch_root(app.source_audio_path);
    // The project folder itself — the source's own parent, the project
    // model's rule — so the act can name every project-side path relative to
    // it (the basename rule at external_sync.h's rule 1).
    job.project_dir  =
        std::filesystem::path(app.source_audio_path).parent_path();

    external_sync_worker.dispatch(
        std::move(job),
        [this](GuiExternalSyncOutcome outcome) {
            on_external_sync_complete(std::move(outcome));
        });
}

// The verdict, back on the main thread (the platform's completion eventfd,
// main.cpp's wiring).
//
// A SUCCESS SAYS NOTHING (architect 2026-08-30, "if it succeeds, we don't
// necessarily need [a notice]") — the render's own precedent, a render served
// silently publishing silently — so only a FAILURE raises, and the worker
// composed that sentence. It is a NORMAL card and never the critical class: a
// failed mirror is retried by pressing the row again, while the critical
// class is the checkpoint act's alone (the reason at the declaration). A
// successful act carries an empty message by construction
// (GuiExternalSyncOutcome), so this fork and that emptiness are one statement
// and neither invents the other.
void GuiInputHandler::on_external_sync_complete(
        GuiExternalSyncOutcome outcome) {
    if (outcome.ok) return;
    notifications.notify(AppState::NotificationClass::Normal, std::move(outcome.message));
}

// P / I / M letter-key handlers, plus the measure propagate's two Ctrl+Slash
// chords. See the declaration for the chord list.
// THE CLIPBOARD FAMILY'S SHARED SENTENCES (architect 2026-08-30, the
// strictness ruling). Four chords — the phase-reset propagate's copy and its
// two pastes, and the measure propagate's copy and paste — take the SAME
// gates term for term, so where two of them refuse on the same fact they say
// the same words and the literal lives once. What is NOT shared is each
// chord's own first gate (what it copies from, what it pastes onto), which
// names its own payload and is spelled at its arm.
// kSelectOneRun HAS A FIFTH READER, the `m` sweep's contiguity arm below: the
// two gates are the same test on the same set for the same reason (the run
// must have one span meaning), so they answer in one sentence.
constexpr const char* kSelectOneRun =
    "Select one consecutive run of markers";
constexpr const char* kNothingCopiedYet = "Nothing has been copied yet";
constexpr const char* kSelectOneAnchor =
    "Select exactly one marker to paste onto";
constexpr const char* kPastePhaseOntoWarp =
    "Phase resets are pasted onto warp markers";
// THE TWO COPIES' SHARED "NOTHING WAS CAPTURED" SENTENCE (2026-08-30), the
// kNothingMatched twin on the copy side (phase_reset_propagate.cpp): both
// copies take the SAME membership, warp_marker_propagates — labeled AND
// effectively enabled — so a run of unlabeled or disabled markers passes every
// gate and captures nothing, leaving an EMPTY clipboard that the matching
// paste then refuses with "Nothing has been copied yet". One fact, one
// wording, and it names the membership rather than the label alone because a
// labeled but disabled marker propagates nothing either.
constexpr const char* kNothingToCopy =
    "No labeled, enabled markers are selected, so nothing was copied";

bool GuiInputHandler::handle_mode_keys(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // Ctrl+P: copy phase reset placements from the selected warp markers
    // into the session clipboard. Section-based (architect 2026-07-23):
    // each selected marker contributes the block it owns — its time to the next
    // EFFECTIVELY-ENABLED marker's time, or to the song end when none follows
    // (a disabled marker never reaches the warp map, so it bounds no section;
    // the extent rule is stated in full at section_end_index,
    // warpmarkers.h). The set
    // must be a CONTIGUOUS run: the paste walks every labeled
    // destination block from the anchor and matches the clipboard in strict
    // lockstep, so a disjoint clipboard (labeled blocks A, C with a labeled B
    // unselected in the gap) would diverge at the first gap and never paste C.
    // The clipboard carries no gap representation, so contiguity is what keeps
    // the two label sequences aligned — the SAME gate the `m` sweep takes
    // (extent == count). Unlabeled markers inside the run still contribute no
    // block, and the paste's destination walk skips unlabeled markers
    // identically, so the two label sequences stay aligned regardless.
    // W-mode only. EVERY REFUSAL IN THE FOUR CLIPBOARD CHORDS SAYS SO ON A
    // CARD since 2026-08-30 (architect, the strictness ruling), each naming
    // the gate it failed: the 2026-07-23 "gesture refusals are silent by
    // convention" ruling was withdrawn for this family with the rest.
    if (key == GuiKeys::P && ctrl && !shift && !alt) {
        if (app.active_markers_view != 'W') {
            notifications.notify(AppState::NotificationClass::Normal,
                                 "Phase resets are copied from warp markers");
            return true;
        }
        if (app.selected_markers.empty()) {
            notifications.notify(
                AppState::NotificationClass::Normal,
                "Select the markers whose phase resets to copy");
            return true;
        }
        // Contiguity gate, same spelling as the `m` sweep: std::set is
        // ascending, so a run [first .. last] is contiguous iff its extent
        // equals its count.
        if (*app.selected_markers.rbegin() - *app.selected_markers.begin() + 1
                != static_cast<int>(app.selected_markers.size())) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kSelectOneRun);
            return true;
        }
        phase_reset_propagate.copy_from_selection();
        // THE COPY SAYS SO, and says WHICH of the two things it did: a
        // clipboard write paints nothing (the reasoning is at bare `j`'s own
        // card, copy_focused_marker_value), and the write here can come out
        // EMPTY behind passed gates, which is a different fact and takes the
        // family's own sentence for it. The card is the ARM'S, beside the
        // gates it follows: both propagates' COPY bodies carry no sentence of
        // their own (unlike the pastes beside them), and the arm is where the
        // rest of this chord's answers already live.
        // THE MENU ROW INHERITS IT: Edit -> Copy phase resets dispatches this
        // chord.
        if (app.phase_reset_clipboard.empty()) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kNothingToCopy);
            return true;
        }
        notifications.notify(AppState::NotificationClass::Normal,
                             "Copied the selected markers' phase resets");
        return true;
    }

    // Ctrl+Alt+P: paste clipboard phase resets onto the destination
    // anchored at the single selected warp marker. W-mode only; the three
    // gates — the P view, an empty clipboard, and a selection that is not
    // exactly one marker — EACH SAY SO ON A CARD since 2026-08-30 (the
    // strictness ruling; the sentences are the family's shared literals, so
    // the four clipboard chords answer one state in one wording). They were
    // silent no-ops under the 2026-07-30 "every refusal in this family is
    // silent" ruling, withdrawn with the rest of that convention. STILL NO
    // GESTURE-CLASS STDERR: a wrong selection count is an ordinary "not ready
    // yet" state the user can see on screen, not a fault worth a terminal
    // line — the card is the whole answer.
    // Opens a confirmation prompt before any mutation.
    if (key == GuiKeys::P && ctrl && !shift && alt) {
        if (app.active_markers_view != 'W') {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kPastePhaseOntoWarp);
            return true;
        }
        if (app.phase_reset_clipboard.empty()) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kNothingCopiedYet);
            return true;
        }
        if (app.selected_markers.size() != 1) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kSelectOneAnchor);
            return true;
        }
        phase_reset_propagate.open_paste_confirmation();
        return true;
    }

    // Ctrl+Alt+Shift+P: propagate the enabled/disabled *state* of
    // clipboard placements onto the matching destination region's
    // phase resets, in order. Positions are not modified. W-mode only; its
    // three gates are its Ctrl+Alt+P sibling's term for term and CARD in that
    // sibling's own sentences since 2026-08-30, where the rationale is
    // stated. Unlike Ctrl+Alt+P, no confirmation prompt — applies
    // directly. Divergence/mismatch is reported as a notification card
    // rather than a modal dialog.
    if (key == GuiKeys::P && ctrl && shift && alt) {
        // THE THREE GATES ARE ITS SIBLING'S, TERM FOR TERM, so they answer in
        // its sentences (2026-08-30). The strictness inventory listed the
        // four other clipboard chords and not this one; it is the same family
        // and a silent arm beside three that speak is the drift the arc is
        // removing.
        if (app.active_markers_view != 'W') {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kPastePhaseOntoWarp);
            return true;
        }
        if (app.phase_reset_clipboard.empty()) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kNothingCopiedYet);
            return true;
        }
        if (app.selected_markers.size() != 1) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kSelectOneAnchor);
            return true;
        }
        phase_reset_propagate.paste_state_apply();
        return true;
    }

    // `p` (no modifiers) toggles phase reset view globally.
    if (key == GuiKeys::P && !ctrl && !shift && !alt) {
        active_views.toggle_active_markers_view();
        return true;
    }

    // Ctrl+/: copy the selected warp markers' MEASURES into the session
    // clipboard — the measure propagate's copy half (architect 2026-08-20), the
    // Ctrl+P arm above written for the other propagate. THE GATES ARE
    // IDENTICAL, deliberately and not by coincidence: W-mode, a non-empty
    // selection, and a CONTIGUOUS run, spelled here at the caller exactly as
    // Ctrl+P spells it (std::set is ascending, so a run is contiguous iff its
    // extent equals its count). Contiguity is load-bearing for the same reason:
    // the paste matches the two label sequences in strict lockstep, so a
    // disjoint clipboard would diverge at the first gap and never reach what
    // followed it.
    //
    // W-MODE ONLY, and the measure propagate has no phase-reset counterpart at
    // all — the ruling and its reasoning are at measure_clipboard.h.
    // Non-mutating: no undo entry, no dirty bit. Every refusal SAYS SO on a
    // card since 2026-08-30, its Ctrl+P twin's three sentences in this
    // clipboard's own words.
    if (key == GuiKeys::Slash && ctrl && !shift && !alt) {
        if (app.active_markers_view != 'W') {
            notifications.notify(AppState::NotificationClass::Normal,
                                 "Measures are copied from warp markers");
            return true;
        }
        if (app.selected_markers.empty()) {
            notifications.notify(
                AppState::NotificationClass::Normal,
                "Select the markers whose measures to copy");
            return true;
        }
        if (*app.selected_markers.rbegin() - *app.selected_markers.begin() + 1
                != static_cast<int>(app.selected_markers.size())) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kSelectOneRun);
            return true;
        }
        copy_measures_from_selection();
        // Its Ctrl+P twin's two answers in this clipboard's words, for the
        // twin's reasons (the card is the arm's; the empty capture is the
        // shared sentence). Edit -> Copy measures dispatches this chord and
        // inherits both.
        if (app.measure_clipboard.empty()) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kNothingToCopy);
            return true;
        }
        notifications.notify(AppState::NotificationClass::Normal,
                             "Copied the selected markers' measures");
        return true;
    }

    // Ctrl+Alt+/: paste the clipboard's measures onto the destination run
    // anchored at the single selected warp marker. W-mode only; the gates are
    // its Ctrl+Alt+P sibling's, term for term — the P view, an empty clipboard
    // and a selection that is not exactly one marker — and each CARDS in the
    // family's own sentences since 2026-08-30, the sibling owning the
    // reasoning.
    //
    // IT OPENS A MODAL EDITOR BEFORE ANY MUTATION, where the phase paste opens
    // a confirmation prompt: the offset dialog IS this act's confirmation, and
    // a bare Enter over its `0` seed is that prompt's `y`.
    if (key == GuiKeys::Slash && ctrl && !shift && alt) {
        if (app.active_markers_view != 'W') {
            notifications.notify(AppState::NotificationClass::Normal,
                                 "Measures are pasted onto warp markers");
            return true;
        }
        if (app.measure_clipboard.empty()) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kNothingCopiedYet);
            return true;
        }
        if (app.selected_markers.size() != 1) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kSelectOneAnchor);
            return true;
        }
        open_measure_paste_editor();
        return true;
    }

    // `i` (no modifiers) toggles iteration mode in warp. In phase reset view
    // it is a no-op that SAYS SO on a card (the arm below, 2026-08-30 —
    // phase reset flags carry no tempo to iterate). The editor-active branch
    // above already swallows any
    // keystroke while a popup edit is in flight, so this code only
    // runs with no active editor. Toggling repaints the top strip
    // so iteration popups appear or vanish in one frame.
    if (key == GuiKeys::I && !ctrl && !shift && !alt) {
        if (app.active_markers_view == 'W') {
            // THE GATE IS THE WARP COLUMN ALONE — both audio views (architect
            // 2026-08-07, iteration mode is TARGET-LEGAL; the deleted S->T
            // wipe's record is in switch_active_audio_view_to,
            // input_handler.cpp). It read active_column_authoring_allowed
            // until then, which pinned the toggle to warp's SOURCE home. The
            // relaxation is about MODE STATE rather than authoring, which is
            // why it is not a home-view-binding exception: the bit selects
            // what Ctrl+Alt+R means and what the flags show. (Bracket
            // AUTHORING was source-only at the flag editor's own gate until
            // 2026-08-24, when the editor became the binding's FIFTH ruled
            // exception and the grammar rode into target view with it.) The
            // two views behave IDENTICALLY here in every other respect —
            // read-only in particular, which refuses `i` from either view for
            // the same one reason (the key is not on read_only_key_blocked's
            // allowlist, a view-independent gate that runs above this
            // dispatch).
            const bool turning_on = !app.iteration_mode_enabled;
            if (!turning_on) {
                // Turning iteration mode OFF wipes every marker's
                // session-only iter bracket — exiting the mode is the
                // clear (wipe_iter_state, shared with enter_bpm_mode's
                // forced iter-off so the two exit routes cannot drift).
                // Runs before the flag flips.
                // IN TARGET VIEW THAT WIPE IS A WARP-STORE WRITE OFF WARP'S
                // HOME, admitted as the same granted home-view-binding
                // exception the sweep's success tail takes (architect
                // 2026-08-07, recorded at run_iteration_sweep_render's tail
                // where the class is argued in full): iter brackets are
                // session-only fields, never serialized and excluded from the
                // render recipe, so the write reaches neither disk nor a
                // render, and the entry it pushes carries
                // affects_persistence=false.
                flag_editor.wipe_iter_state();
            }
            app.iteration_mode_enabled = !app.iteration_mode_enabled;
            viewport.invalidate_top_strip();
            // (A SECOND DAMAGE CALL STOOD HERE for the one day the STATUS BAR
            // did, 2026-08-29: the mode bit is one of the eligibility terms
            // the resolved READOUT read, so the toggle hid or restored that
            // cell. The readout retired with the bar and nothing outside the
            // top strip changes here.)
        } else {
            // AND THE P VIEW SAYS SO (architect 2026-08-30, the strictness
            // ruling): iteration brackets are a WARP payload — a phase reset
            // carries no tempo to iterate — so the card names the COLUMN and
            // not a view, the bit being legal in both audio views of warp.
            // THE SUBJECT IS THE MENU ROW'S OWN NAME (2026-08-31 rebrand,
            // kSeriesPopupItems): this card and the BPM ladder's twin below
            // say "Grid iterations" / "BPM iterations", the words the user
            // pressed, rather than a "mode" noun that appears nowhere else.
            notifications.notify(AppState::NotificationClass::Normal,
                                 "Grid iterations work on warp markers");
        }
        return true;
    }

    // `m` (no modifiers): open the BPM editor on the FIRST of a contiguous
    // run of selected markers whose sections define the sweep span. Warp
    // view only, and the P column is answered on a card. Mutual exclusion with
    // iter mode is handled inside enter_bpm_mode. The section rule (architect
    // 2026-07-23, in its EFFECTIVE-PARTICIPATION form here since 2026-08-24):
    // a marker owns the section from itself to the next marker that
    // PARTICIPATES IN THE RENDER, and a marker trailed only by disabled ones
    // owns the section to the song end — so the selected run's LAST section is
    // INCLUDED, and it may run PAST disabled markers. section_end_index
    // (warpmarkers.h) is that rule's one owner, shared with the phase-reset
    // propagate. The gate requires a NON-EMPTY, CONTIGUOUS run of selected
    // markers with no effectively-enabled label_ref in
    // [owner .. boundary marker] inclusive and, since 2026-08-26, ONE TEMPO
    // across the run (each selected marker a pass or the owner's own value)
    // and an owner outside every coincident-collapse run; any other selection
    // is refused, and SAYS WHICH RULE IT BROKE since 2026-08-30 (the arm-by-arm
    // ruling is at the first test below). Under the contiguity rule every in-span marker up to the last
    // selected one IS selected, so a selected span-internal marker may be
    // disabled — and since 2026-08-29 the sweep cell LEAVES IT EXACTLY AS IT
    // IS rather than converting it to a plain pass, as it leaves the disabled
    // markers the extended boundary sweeps in past it (a disabled marker is
    // invisible to the act in the span and out of it, the ruling at
    // bpm_cell_warp_markers, input_handler.h); a disabled OWNER is rejected
    // (bpm_popup_eligible_marker excludes disabled — a disabled owner was a
    // render-inert rewrite).
    // There is no toggle-off branch: the bpm editor is a modal dialog
    // surface, so while it is open `m` never reaches this dispatch — it is
    // just a typed character the bracket grammar rejects — and bpm mode never
    // rests without its editor (the mode's only exits are the editor's own:
    // Esc, and Enter's dispatch tail).
    if (key == GuiKeys::M && !ctrl && !shift && !alt) {
        // EVERY ARM OF THIS GATE NAMES THE RULE IT BROKE (architect
        // 2026-08-30, the strictness ruling). The ladder is ten tests deep and
        // its whole answer used to be one indistinguishable non-response — the
        // state a card exists for. Each arm now returns on its own sentence, so
        // ONE press raises ONE card carrying the FIRST rule the selection
        // failed; the sweep's own later refusals (the commit gate's bracket
        // walls) belong to the editor and are untouched.
        if (app.active_markers_view != 'W') {
            notifications.notify(AppState::NotificationClass::Normal,
                                 "BPM iterations work on warp markers");
            return true;
        }
        // The bpm editor is DELIBERATELY OMITTED from the warp status/value
        // family admitted in W+target on 2026-08-24 (architect: "add the ones
        // we can, and omit the ones we must omit"; the inventory is at
        // active_column_authoring_allowed, app_state.h). Its reason is its
        // own rather than placement: it rewrites tempo through a DERIVATION
        // over a SPAN — the selected run's sections and their durations —
        // rather than editing one marker's value, so it keeps warp's home
        // (source) view; off home the card names that view.
        if (!active_column_authoring_allowed(app)) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 "BPM iterations work in source view");
            return true;
        }
        // Section-based span gate. A non-empty, contiguous run of selected
        // markers; the first owns, and the run covers the sections owned by
        // every selected marker (the last one's section included).
        if (app.selected_markers.empty()) {
            notifications.notify(
                AppState::NotificationClass::Normal,
                "Select the markers whose sections to sweep");
            return true;
        }
        const auto& mv = app.warpmarkers.markers();
        const int n = static_cast<int>(mv.size());
        const int owner    = *app.selected_markers.begin();
        const int last_sel = *app.selected_markers.rbegin();
        // A BELT against the selection layer's own invariant (indices in
        // range), carded like the rest rather than left mute: it is the one
        // arm whose sentence describes a state the user cannot read off the
        // screen, so silence here would be the worst of the ten.
        if (owner < 0 || last_sel >= n) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 "The selection no longer matches the markers");
            return true;
        }
        // Contiguity: the sweep writes ONE owner tempo over ONE contiguous
        // span, and the shift-range select produces exactly contiguous runs;
        // a disjoint set has no single-span meaning here. The COPY takes the
        // SAME contiguity gate (its paste walks labeled blocks in lockstep, so
        // a gap would misalign the two label sequences). std::set is ascending,
        // so a run [owner .. last_sel] is contiguous iff its extent equals its
        // count.
        if (last_sel - owner + 1 != static_cast<int>(app.selected_markers.size())) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kSelectOneRun);
            return true;
        }
        // The last selected marker's section ends at the next marker that
        // PARTICIPATES IN THE RENDER, not at the next store marker
        // (architect 2026-08-24): section_end_index (warpmarkers.h) is that
        // rule's one owner, shared with the phase-reset propagate. When
        // boundary < n the marker there is the closing boundary — effectively
        // enabled, owning the following section, outside the span; boundary
        // == n is the song end, no enabled marker following the selection, so
        // the last section runs to total_frames. A DISABLED marker is dropped
        // before the warp map is built, so bounding the span at one measured
        // the duration short by the whole remainder and mistuned every derived
        // cell tempo.
        const int boundary = section_end_index(mv, last_sel);
        // No EFFECTIVELY-ENABLED label_ref anywhere in
        // [owner .. min(boundary, n-1)] inclusive: every in-span marker
        // rewrites its tempo (a ref cannot take one), and the boundary marker
        // (when it exists) still cannot bound the span cleanly. At song end
        // there is no boundary marker, so the scan clamps to n-1. An
        // effectively-disabled ref does not refuse — it takes no part in the
        // render, so it neither receives a rewritten tempo nor bounds
        // anything, which is exactly how the boundary walk above reads it. The
        // scan now also covers the disabled markers between the last selected
        // one and the boundary, and passes over each for the same reason.
        const int scan_end = std::min(boundary, n - 1);
        for (int i = owner; i <= scan_end; ++i) {
            if (!mv[i].label_ref.empty() && !effective_disabled(mv, i)) {
                notifications.notify(
                    AppState::NotificationClass::Normal,
                    "A label reference lies inside or at the end of the span");
                return true;
            }
        }
        // Owner must satisfy the BPM-eligibility predicate (owning, no ref,
        // and — now — enabled).
        if (!bpm_popup_eligible_marker(mv[owner])) {
            notifications.notify(
                AppState::NotificationClass::Normal,
                "The first selected marker must own its tempo and be enabled");
            return true;
        }
        // A COINCIDENT OWNER CANNOT TAKE A TEMPO EITHER (codex 2026-08-26,
        // the ref refusal's own class): the resolver's stage 2 collapses an
        // exact-frame run with two or more effectively-enabled members to
        // ONE synthetic 1.00 owner, so a span owned from inside such a run
        // renders at 1.00 in every cell whatever the editor derived — and
        // when the coincident partner is the boundary itself the span's
        // duration is zero, the commit gate has nothing to measure and the
        // sweep derives nothing, closing the mode on an empty batch. The
        // verdict is the classifier's, warp_coincident_collapse_members
        // (warp_frame_map_build.h — the render's stage 2 and the lane's red
        // cue consult the same one), never re-spelled here.
        if (warp_coincident_collapse_members(
                slice_to_warp_markers(mv))[static_cast<size_t>(owner)]) {
            notifications.notify(
                AppState::NotificationClass::Normal,
                "The first selected marker shares its frame with another");
            return true;
        }
        // ONE TEMPO IN THE RUN (architect 2026-08-26): every selected marker
        // is a pass or carries the owner's own tempo — cents AND typed scale,
        // the effective value — else `m` refuses and says so, like the other
        // arrangement refusals above. The sweep rewrites the span to ONE
        // tempo and rescales everything outside it by THE OWNER'S change
        // (bpm_cell_warp_markers, input_handler.h), so a run whose members
        // disagreed would have its internal differences erased while the
        // rest of the map kept its shape — the very deformation the rescale
        // exists to prevent — and the rule is what makes the owner's change
        // the whole span's. Effectively-disabled members pass over for the
        // reason the ref scan passes over them: they contribute no tempo to
        // the render, and the cell never rewrites them (2026-08-29), so a
        // disagreeing disabled member has nothing to erase. The scan covers
        // the SELECTED run [owner .. last_sel];
        // the disabled markers the boundary walk sweeps in past it are
        // render-inert by construction and were never selected.
        for (int i = owner + 1; i <= last_sel; ++i) {
            if (mv[i].tempo_inherits)      continue;
            if (effective_disabled(mv, i)) continue;
            if (mv[i].tempo_cents != mv[owner].tempo_cents ||
                mv[i].tempo_scale != mv[owner].tempo_scale) {
                notifications.notify(AppState::NotificationClass::Normal,
                                     "Selected markers carry different tempos");
                return true;
            }
        }
        // enter_bpm_mode tags the owner and flips the mode flag; the span
        // endpoint is explicit, so record it on the owner and keep the whole
        // selected run highlighted as the span cue.
        flag_editor.enter_bpm_mode();
        // THE ONE SILENT ARM OF THE TEN, and unreachable (verified against
        // enter_bpm_mode's head 2026-08-30): that route's five bails are the
        // W column, an empty selection, an out-of-range owner and
        // bpm_popup_eligible_marker — every one of them re-asked from the
        // ladder above, which has already carded it — plus a bpm mode already
        // standing, which cannot be: the mode never rests without its editor,
        // and while that editor stands `m` is a typed character that never
        // reaches this dispatch. So there is no state in which this fires and
        // no sentence to invent for it; it stays a defensive belt.
        if (!app.bpm_mode_enabled) return true;
        {
            auto& mvw = app.warpmarkers.markers_mut();
            mvw[owner].bpm_endpoint = boundary;
        }
        const std::set<int> span_selection = app.selected_markers;
        // The bpm editor is a modal dialog surface, so its open takes the
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
        // it hides the trim region overlay as every point command does. The
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

    // `k` (no modifiers): toggle ADD TO SELECTION, the sticky ctrl (architect
    // 2026-08-18). It is `i`'s and `p`'s shape exactly — one bit, flipped both
    // ways by one key, with the bottom row's button dispatching this same
    // chord — and the ONE route that SETS the bit; every clear is the
    // Selection layer's (the whole contract, the clear list and the shift rule
    // are at AppState::add_to_selection).
    //
    // NO GATE OF ITS OWN, and each omission is deliberate: it is legal in both
    // columns and both audio views (a selection is not authored content, so
    // the home-view binding has nothing to say about it), legal on a LOCKED
    // tab (read_only_key_blocked admits it, where it drops the four marker
    // verbs), and unreachable in the `h` view, whose allowlist consumes it
    // above this dispatch. It stops no playback and hides no overlay: turning
    // the mode on IS NOT a selection act — the click that follows is, and that
    // click runs the marker act's own stop and region hide.
    //
    // THE REPAINT IS THE BOTTOM LANE'S — the button's lamp lives there, so
    // this takes the row's own damage fork (invalidate_rect on
    // bottom_row_area) where `i` and `p` take invalidate_top_strip. It is not
    // left to main.cpp's per-tick face comparator: that walk would catch the
    // drift on the NEXT tick, and a mode toggle must light in the frame it was
    // asked for.
    if (key == GuiKeys::K && !ctrl && !shift && !alt) {
        app.add_to_selection = !app.add_to_selection;
        viewport.invalidate_rect(bottom_row_area(app));
        return true;
    }

    // `l` (no modifiers): "Play renders" — THE RENDER PLAYER (architect
    // design 2026-08-28, retiring the external `audio_player` spawn whole):
    // the in-app player over the project's `tmp/` batch cells (it walked
    // `render/` too until 2026-09-01),
    // through the product's own engine on both devices. TWO PRODUCERS, ONE
    // ROUTE: this key and the icon row's Play renders button, which
    // synthesizes exactly this bare chord. The modal / editor / `h` /
    // loading gates in on_key run before this handler, so `l` is inert while
    // any of them owns the keyboard; inside the player the key is the mode's
    // own closer (route_render_player_key) and never reaches here. "Nothing
    // to play: no renders under tmp/" is the opener's one status
    // refusal.
    if (key == GuiKeys::L && !ctrl && !shift && !alt) {
        toggle_render_player();
        return true;
    }

    return false;
}

// -- THE RENDER PLAYER'S KEYBOARD HALF (2026-08-28) ----------------------------
//
// The contracts are at the declarations (input_handler.h); the acts are
// GuiRenderPlayer's (render_player.h). What lives here is the routing.

void GuiInputHandler::toggle_render_player() {
    if (app.render_player.active) {
        render_player.close();
        return;
    }
    (void)render_player.open();
}

bool GuiInputHandler::route_render_player_key(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    const bool bare  = !ctrl && !shift && !alt;

    // THE FOUR FALL-THROUGHS, WHICH ARE THE FILE MENU'S THREE ROWS PLUS THE
    // SAVE: Ctrl+S saves with the player standing (no transport is touched);
    // Ctrl+Q falls through to the ordinary quit road, which takes the player
    // down at its head (GuiPrompt::request_close) — the same step the
    // compositor's close takes, stated once there rather than here, so the two
    // roads cannot drift; and since 2026-09-02 CTRL+O and BARE BACKSLASH join
    // them, because the File anchor stays LIT under the panel (architect: "only
    // File should remain lit") and THE KEY AND THE ROW MUST AGREE — a row that
    // works from the menu and a chord the router eats would be one act with two
    // answers. Ctrl+O reaches open_project_picker, which CLOSES the player and
    // opens the picker (the two never stand together); `\` reaches
    // synchronize_to_external_storage, which is read-only-legal, runs on its own
    // worker and stops no playback — a playing item is a reader of files the
    // mirror only reads.
    if (ctrl && !shift && !alt && key == GuiKeys::S) return false;
    if (ctrl && !shift && !alt && key == GuiKeys::Q) return false;
    if (is_open_project_key(key, mods)) return false;
    if (is_sync_external_key(key, mods)) return false;

    // THE RING: Tab / Shift+Tab walk [list, buttons…] through the one modal
    // ring route, whose player arm owns the list's membership.
    if (route_modal_dialog_focus_key(key, mods)) return true;

    // A RING-FOCUSED BUTTON takes Enter and Space as its own press
    // (press-at-press, commit-at-release — the modal's own rule, the route
    // above already armed it and returned true). Reaching here with the
    // focus on a button means the key was not one of the ring's, so the
    // player's vocabulary below applies to it; Left/Right are the seeks even
    // then, deliberately — the player's ring has no Left/Right walk, since
    // those keys are the car's rewind and fast-forward.

    // THE ITEM FOLDER'S ENDS (architect 2026-08-28, R37; re-keyed 2026-08-31):
    // Shift+Home and Shift+End are the two skips' keys with the modifier — the
    // first and the last wav of the transport item's own folder, never a wrap
    // and a carded refusal with no item. Ahead of the blanket below because
    // that blanket is what consumes every OTHER shifted spelling; they are
    // ONE-SHOT with the plain pair (repeat_eligible's player arm — an absolute
    // landing a hold could only re-reach).
    //
    // THE PAIR FOLLOWS ITS PLAIN ACTS off `,` / `.` — the skips are Home and
    // End now, "just like the regular GUI", and a shifted twin lives on its
    // own button's key or it is not a twin.
    if (shift && !ctrl && !alt) {
        if (key == GuiKeys::Home) {
            render_player.first_in_item_folder();
            return true;
        }
        if (key == GuiKeys::End) {
            render_player.last_in_item_folder();
            return true;
        }
    }

    // EVERY OTHER MODIFIED CHORD: CONSUMED, AND SILENTLY (architect
    // 2026-08-30, the unbound-keys ruling) — the mode's router IS the whole
    // vocabulary while it stands, so a chord that means something outside it
    // means nothing in here, and a press that does nothing is answered by the
    // silence that identifies it. Both catch-alls, the modified one and the
    // bare one below, carded "<chord> is not bound in the render player" for
    // the one day of 2026-08-30.
    if (!bare) return true;

    const int highlight = app.folder_overlay.highlight_row;
    switch (key) {
        case GuiKeys::Escape:
        case GuiKeys::L:
            render_player.close();
            return true;
        // BARE `v` WAS STOP (R36, on this letter since 2026-08-30 — Audacious
        // and Winamp before it stop on V) AND IS UNBOUND HERE since 2026-09-01:
        // the player's Stop act retired whole with its button, leaving the row
        // the main window's own transport triple, so the letter falls to the
        // silent catch-all below like every other chord this router does not
        // name.
        case GuiKeys::Apostrophe:
            render_player_load_in_place();
            return true;
        case GuiKeys::R:
            // REPEAT ONE (architect 2026-08-28, R26), the Repeat one button's
            // own chord: one-shot like every other act key here, and bare-
            // exact like all of them — every modified spelling of `r` is
            // consumed above by the router's `if (!bare) return true`.
            render_player.toggle_repeat_one();
            return true;
        case GuiKeys::Return:
        case GuiKeys::KpEnter:
            // With no button focused, Enter OPENS the highlight.
            if (mods.synthesized_repeat) return true;
            render_player.open_row(highlight);
            return true;
        case GuiKeys::Space:
            // THE PLAY BUTTON'S ACT, which since R6 (2026-08-31) reads the
            // HIGHLIGHT first and the transport second (the fork at
            // play_button_act): a band standing on a folder row or on a
            // wav that is not the item goes THERE, and anywhere else it is
            // the transport's toggle. Enter one case above is the same act
            // through the row's own body, and the two differ only on the
            // transport's own item.
            if (mods.synthesized_repeat) return true;
            render_player.play_button_act();
            return true;
        case GuiKeys::Up:
            render_player.move_highlight(-1);
            return true;
        case GuiKeys::Down:
            render_player.move_highlight(+1);
            return true;
        case GuiKeys::Left:
            render_player.seek_by(-render_player.seek_step_frames());
            return true;
        case GuiKeys::Right:
            render_player.seek_by(+render_player.seek_step_frames());
            return true;
        case GuiKeys::Home:
            // THE LEFT SKIP'S KEY (architect 2026-08-31) — this track's start,
            // or THE PREVIOUS ENTRY inside the item's first three seconds (the
            // previous-track window at kPlayerPreviousThresholdMs, the act's
            // own fork). One-shot: both arms are absolute landings.
            render_player.home();
            return true;
        case GuiKeys::End:
            // THE TRACK'S OWN END (architect 2026-08-30), Home's twin and NOT
            // "next": it writes the position the scrub's right end writes and
            // nothing more — the item is unchanged and no folder is walked.
            // What happens after is the NATURAL END's, unaltered: a live
            // transport plays out the last frames and then advances to the
            // next entry where one exists, rests idle on the item at the
            // folder's last, or replays under a lit Repeat one. A PAUSED one just moves
            // its rest, which the resume arm reads as "at or past the end" and
            // replays from the start (toggle_pause), and an IDLE one meets
            // seek_to's own carded refusal — Home's twin here too, except
            // that HOME'S PREVIOUS-TRACK WINDOW can carry an idle press to
            // the previous entry before the seek is ever asked. End has no
            // window of its own: "next" at a track's end is what the natural
            // end already does.
            render_player.end();
            return true;
        case GuiKeys::BackSpace:
            render_player.up();
            return true;
        default:
            // The bare half of the catch-all above, silent with it: every
            // bare key the player binds is a case above this one — `,` and
            // `.` among the keys it does NOT, since 2026-08-31: they stepped
            // the item's folder for one day and fall here now, silent like
            // every other unbound chord (decision 72's deduction rule, not a
            // card), their acts having gone to the two skips as Home and End.
            // Bare `v` joined them on 2026-09-01 with the Stop's retirement.
            return true;   // consumed
    }
}

// -- THE VALUE PAIR: bare `j` copies, Shift+`j` jumps -----------------------
//
// (architect 2026-08-29, replacing the resolved readout and its Ctrl+C, which
// retired with the status bar the same day.) BOTH ACT ON THE SELECTION'S
// FOCUS, the subject Ctrl+C had, and both compose through the ONE parser
// composer resolved_marker_payload (warp_frame_map_build.h), which returns the
// pasteable value and — through its out-parameter — the marker that value came
// FROM: a pass's immediate prior owner, a ref's definition. One resolve, two
// answers, so the value copied and the marker jumped to can never name
// different markers.
//
// THE ELIGIBILITY IS THE ONE GATE, payload_eligible_marker (app_state.h):
// warp view, iteration mode off, an enabled pass or a ref to an enabled
// definition. An ineligible focus — an owner, a phase reset, iteration mode,
// the `P` column, nothing focused — and an empty payload alike SAY SO ON A
// CARD since 2026-08-30 (architect, the strictness ruling), and they say the
// SAME words: from the user's side both are "this marker has no resolved
// value", the difference between them being which layer discovered it. THE
// COPY VALUE BUTTON GREYS ON THE GATE'S ANSWER since the same day
// (redesign_button_enabled, the truthful-buttons ruling — for its first day
// it never greyed on the selection's state, the refusal changing at
// interaction cadence), so the gate's card is the KEY's; the empty payload
// alone stays behind a lit face, needing the composer run, and is the one
// refusal of the two a button lift can reach.

// THE COPY'S ONE SENTENCE, said by BOTH of its refusals (2026-08-30).
constexpr const char* kNoResolvedValueToCopy =
    "The focused marker has no resolved value to copy";

void GuiInputHandler::copy_focused_marker_value() {
    if (!payload_eligible_marker(app, app.last_selected_marker)) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kNoResolvedValueToCopy);
        return;
    }
    const std::string payload = resolved_marker_payload(
        slice_to_warp_markers(app.warpmarkers.markers()),
        app.last_selected_marker, audio.total_frames());
    // THE CLIPBOARD HAS ONE REPRESENTATION, the platform's: this composes the
    // string and hands it straight over, holding no copy of its own
    // (conventions.md's clipboard ruling).
    if (payload.empty()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kNoResolvedValueToCopy);
        return;
    }
    gui.clipboard_set_text(payload);
    // AND THE SUCCESS SAYS SO (architect 2026-08-30, the strictness ruling's
    // invariant that an accepted press shows something): A CLIPBOARD WRITE IS
    // THE ONE SUCCESS IN THE PRODUCT THAT PAINTS NOTHING — no surface displays
    // a clipboard, and since the resolved readout retired with the status bar
    // nothing displays a resolved value either — so this card is the whole of
    // what the press shows. The save, a render and a Synchronize stay silent
    // by ruling: each has its own visible answer (the title's dirty mark, the
    // file on disk). THE BUTTON INHERITS IT: the bottom row's Copy resolved
    // value dispatches this same bare `j` through on_key at its lift, so the
    // sentence lives once and both roads say it.
    notifications.notify(AppState::NotificationClass::Normal,
                         "Copied the resolved value");
}

// THE JUMP — Shift+`j`: stand the OTHER A/B tab on the marker this one's
// focused value came from, so a reference and its definition can be read side
// by side one Ctrl+Tab apart. FIVE ACTS IN THIS ORDER, each through its own
// chokepoint and none of them spelled twice — and the ORDER is the whole of
// what makes the act land where it says it does:
//   * run_center_command ON THE CURRENT TAB FIRST (architect 2026-08-29, "with
//     the c on the current tab first to ensure it gets done"): the working
//     zoom centred on THIS tab's focused marker — the reference or the pass
//     the value was read off — done BEFORE the tab is left, so the origin tab
//     is framed on its own subject whether or not the user ever comes back to
//     it. IT IS THE A/B AUDITION'S OWN SHAPE, each half of that act opening
//     with `c` on the tab it plays (ab_audition.h). AND IT LANDS THE PLAYHEAD:
//     `c` re-lands the cursor on the focused marker before it centres, so the
//     origin tab's playhead comes to rest on the reference — the audition's
//     behaviour too, and wanted here for the same reason: the tab you leave is
//     left standing on the thing you were reading;
//   * the TAB SWITCH through switch_active_tab_view_to, the Ctrl+Tab road,
//     which clears the selection as every tab switch does, re-lands the
//     window's own state and runs the COINCIDENCE AUTO-SELECT at the entering
//     tab's own parked cursor — which may seat a marker that has nothing to do
//     with this value, and is exactly why the two steps below come AFTER it;
//   * THE SINGLE-SELECT, Selection::set_single_selection on the returned
//     index: the plain marker click's own road (run_marker_click_act's plain
//     arm, input_pointer.cpp), so the source is the whole selection and the
//     focus. BY INDEX, never by frame — with markers sharing the source's
//     frame the selected member is the exact marker the value came from, which
//     a coincidence scan cannot promise — and it overwrites whatever the
//     switch's auto-select seated, so the act cannot end on the entering tab's
//     previously parked marker;
//   * land_playhead_on_marker on that same index — the landing owner the plain
//     click uses, the selection road above landing nothing of its own — and,
//     being one of the two MOVEMENT owners, it stops playback, ends a standing
//     audition and hides the trim region overlay, so this body writes none of
//     that;
//   * run_center_command AGAIN, now on the tab that landed, which puts the
//     working zoom on the FOCUSED marker — the source, by the single-select
//     above, so the camera and the cursor cannot come to name two different
//     markers. BOTH TABS END FRAMED, each on its own half of the pair.
// NO UNDO ENTRY: a tab switch, a selection and a playhead move record nothing
// anywhere in the product, so there is nothing here to push.
void GuiInputHandler::jump_to_value_source() {
    if (!payload_eligible_marker(app, app.last_selected_marker)) {
        // THE COPY'S OWN GATE, in the JUMP's words: there is no resolved
        // value here, so there is no marker the value came from either. It
        // says "value" and not "value to copy" because this chord copies
        // nothing (architect 2026-08-30).
        notifications.notify(AppState::NotificationClass::Normal,
                             "The focused marker has no resolved value");
        return;
    }
    // AN EMPTY PAYLOAD IS THE COPY'S OWN REFUSAL and the jump's too — an
    // unresolvable ref or a carve-out with no successor — and a payload that
    // names NO SOURCE is the value's own fallback (a first-marker pass, a walk
    // that ended on a ref, a synthetic prior, a normalized ref): there is
    // nothing to jump to, so the press is a consumed nothing.
    // AND THEY SAY SO SINCE 2026-08-30, all three in ONE sentence: whichever
    // of them answered, the fact the press needs is that this value names no
    // marker to stand on. (The third is a belt — a source index past the
    // store — and shares the sentence rather than earning one, since a
    // separate wording would describe an invariant breach to the user.)
    // THE THREE ARE ONE OWNER since 2026-09-01 — value_source_marker
    // (app_state.cpp), which wraps the composer call this body made inline
    // and which the Copy value button's shift line reads too, so the line
    // drops exactly where this cards.
    const int source = value_source_marker(app, audio.total_frames());
    if (source < 0) {
        notifications.notify(AppState::NotificationClass::Normal,
                             "There is no source marker to jump to");
        return;
    }
    run_center_command();
    active_views.switch_active_tab_view_to(
        app.active_tab_view == 'A' ? 'B' : 'A');
    selection.set_single_selection(source);
    land_playhead_on_marker(app, audio, viewport, source);
    run_center_command();
}

void GuiInputHandler::render_player_load_in_place() {
    if (!app.render_player.active) return;
    if (app.prompt.active) return;
    // THE LOCK REFUSES AND SAYS SO (architect 2026-08-30, ending the silence
    // it kept until then): a load in place writes the marker stores and the
    // engine block, exactly what the read-only tab protects. SINCE THE SAME
    // DAY THE BUTTON GREYS on this refusal and on a recipe-less highlight
    // (render_player_button_enabled — its lift is consumed, the grey being
    // the message), so this card and the highlight one below are the
    // KEYBOARD's, bare `'`'s own — and so is the running-render card since
    // 2026-09-01, when that refusal became a face term too (the roster's
    // shape: the grey is the button's message, the key keeps its card). It is
    // one of the THREE readers of kTabReadOnlyCard (notifications.h) — the
    // sites that KNOW THEIR ACT and so need no chord in the sentence. The
    // keyboard gate is not among them: it drops an unbound chord and a bound
    // authoring one alike, so it names the chord instead ("<chord> is not
    // available on a read-only tab"). One lock; the wording differs exactly
    // where what the site knows differs.
    if (active_view_state(app).read_only) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kTabReadOnlyCard);
        return;
    }
    // THE RUNNING-RENDER REFUSAL SAYS SO ON A CARD (architect 2026-08-29, the
    // status bar's fold into row 8): the load wipes tmp/, which must never
    // race a batch publishing into it, so the refusal itself stays — and it
    // is no longer silent. It was silent for one day on the ground that the
    // progress line was its explanation, and THAT LINE IS NOT ON SCREEN HERE:
    // the state cell is row 8's, the modal row takes that lane whole while
    // the player stands, so a refusal with nothing beside it would read as a
    // dead button (validation_topology.md's row, and the batch-cell refusal
    // one arm down is the same class). THE SENTENCE NAMES THE ACT IT
    // REFUSES (architect 2026-08-30, his own example): a bare "Render
    // running" was a fact with no verb, and a card answers a press. THE
    // CONDITION IS ONE OWNER (load_in_place_render_blocked, app_state.h —
    // 2026-09-01), read by the mutator's backstop and the button's face too.
    if (load_in_place_render_blocked(app)) {
        notifications.notify(AppState::NotificationClass::Normal,
                             "Cannot load in place while a render is running");
        return;
    }
    const AppState::RenderEntry* entry = render_player.highlighted_entry();
    if (entry == nullptr) {
        // Said on a card because the button and `'` are live on every row
        // the band can hold, so a silent nothing would read as a broken
        // button (validation_topology.md's row). THE SENTENCE NAMES THE
        // EXCLUDED SET (2026-08-30) AND NAMES ONLY WHAT IS REACHABLE
        // (2026-09-01, the gates-stop-lying rule — a refusal's wording, like a
        // gate's membership, must describe the state the user can actually be
        // in): it named the `..` row and the DELIVERABLE under render/ too,
        // and NEITHER CAN BE HIGHLIGHTED ANY MORE — the deliverable left the
        // listing when the player moved inside tmp/, and `..` became the Up
        // button on the modal row — so a FOLDER ROW is the whole of what can
        // reach this arm. ONE CLAUSE since 2026-09-01 (the sentence-shape
        // rule): it read "Only a render can be loaded in place; a folder
        // carries no recipe", one fact told twice across a semicolon.
        notifications.notify(
            AppState::NotificationClass::Normal,
            "A folder carries no recipe to load in place");
        return;
    }
    // A modal surface is opening over a possibly live transport: PAUSE it
    // through the player's own pause (which takes the one stop body through
    // the player's fork and keeps the resume point) — the recorded exception
    // at stop_playback_for_modal_open.
    if (app.render_player.transport == AppState::RenderPlayer::Transport::Live)
        render_player.toggle_pause();
    app.render_player.pending_load = *entry;
    // THE CONFIRMATION: the entry's id in its one spelling (render_entry_id,
    // renders_dir.h) and SINGLE-QUOTED like every other name in a sentence
    // (2026-09-01, the product's one quoting form on cards and prompts alike;
    // both raisers of this prompt body wore backticks until that day),
    // OK / Cancel — Cancel the Escape sentinel LAST like every
    // prompt's, so Esc's own answer is derived rather than declared; `o` is
    // OK's letter.
    // THE RAISE'S PASSIVE FOCUS IS THE FIRST BUTTON — the ONE LOAD PROMPT,
    // raised from its TWO subjects, and no other prompt in the product
    // (architect 2026-08-28): a bare ENTER here answers OK, because the load
    // is not a destructive answer — it lands ONE undo entry, which the
    // ordinary Ctrl+Z takes back — and it is this same prompt body that the
    // `h` view's `'` raises on its viewed member, so the two `'` load roads
    // answer alike by construction. Through
    // PromptState::present, the one raise route, so the painted gate holds.
    app.prompt.present("Load '" + render_entry_id(*entry) + "' in place?",
                       {'o', '\x1b'},
                       {"OK", "Cancel"},
                       DialogTrigger::LOAD_IN_PLACE_CONFIRM,
                       PromptInitialFocus::FirstButton);
    viewport.invalidate_all();
}

// THE LOAD CONFIRMATION'S OK — ONE PROMPT BODY, TWO SUBJECTS (architect
// 2026-08-29). The raise parks exactly one of them and this fork runs the act
// that subject names: the PLAYER's highlighted batch entry
// (AppState::RenderPlayer::pending_load, raised by its Load in place button or
// bare `'` inside it) or the `h` VIEW's viewed walk member
// (AppState::HistoryMode::pending_load_member, raised by bare `'` there). The
// two raises cannot coexist — the player's opener refuses in the view and the
// view's key never reaches the player — so the order between the arms is free,
// and each arm re-asks its own mode live because the prompt outlives neither.
void GuiInputHandler::confirm_load_in_place() {
    if (app.render_player.pending_load) {
        if (!app.render_player.active) return;
        // Copied before the act: its tail wipes tmp/, and the player's close
        // resets the slot.
        const AppState::RenderEntry entry = *app.render_player.pending_load;
        app.render_player.pending_load.reset();
        if (load_render_entry_in_place(entry)) {
            // SUCCESS CLOSES THE PLAYER: the close binds the source, then its
            // re-express fork hands the engine the VIEW's buffer, then frees
            // the item. In SOURCE view that fork rebinds source.wav; in TARGET
            // view the load's own trigger() has already dispatched (or
            // parked) the preview for the generation this load made, so the
            // fork's ensure_ready HONOURS that standing dispatch and its
            // completion rebinds — it no longer cancels and redispatches the
            // identical render (architect 2026-09-02, R-8; the guard and its
            // state table at GuiTargetRender).
            render_player.close();
            return;
        }
        // NOTHING IS SAID HERE (2026-08-30): the act names every refusal on
        // its own route, on a card and on stderr alike — the same rule the
        // `h` view's two arms below follow — so a sentence composed here
        // could only be a vaguer copy of one already on screen. It said
        // "Load refused" until that day, which told the user nothing the
        // dead player did not.
        return;
    }
    if (app.history_mode.pending_load_member) {
        const std::size_t member = *app.history_mode.pending_load_member;
        app.history_mode.pending_load_member.reset();
        if (!app.history_mode.active) return;
        if (member >= app.history_mode.walk_count()) return;
        // THE FORK ON THE WALK SOURCE, and the one site of it: the Remote tab
        // loads the store's SHA at that index through
        // load_history_commit_in_place, the Local tab the member's number
        // through load_history_local_entry_in_place. The SHA is COPIED out of
        // the store before the act, which closes the mode and drops the
        // session a reference would point into. Each act owns every refusal on
        // its own route and names it on stderr AND on a notification card
        // (2026-08-29; stderr alone until then, the view's own line having
        // outranked the transient tier), so nothing is said here.
        const std::string sha = app.history_mode.session.sha_at(member);
        (void)(app.history_mode.source == GuiHistoryWalkSource::Local
                   ? load_history_local_entry_in_place(member + 1)
                   : load_history_commit_in_place(sha));
    }
}

// THE CONFIRMATION'S CANCEL: both subjects dropped, whichever was parked —
// one body, so a subject cannot outlive the question that named it.
void GuiInputHandler::cancel_load_in_place() {
    app.render_player.pending_load.reset();
    app.history_mode.pending_load_member.reset();
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
// THE Home / End JUMP, one body for four arms — bare Home, bare End and their
// two CTRL forms — so the three unconditional acts are spelled once and the two
// landings differ only in the flags handed to the arithmetic owner.
//
// IT IS A ROUTE OUT OF THE MARKER LANE: the playhead is leaving the focused flag
// for a spot nothing marks, so the selection must go with it — the lane rule's
// second clause (a route that empties the selection leaves the playhead where it
// lands, for the cursor to paint again; the rule itself is stated at
// land_playhead_on_marker in input_pointer.cpp). UNLIKE the bare Left/Right
// arms, this clear does real MEMBERSHIP work: those reach their body only with
// an empty selection (the marker-lane branch in on_key claims them first), so
// theirs is a focus-only repair, while Home/End reach this with ANY selection.
// Without it the flag would keep claiming to be the playhead at its own
// position and the next bare arrow would tow the playhead back onto the marker,
// silently discarding the jump.
//
// THE LANDING FRAME COMES FROM THE SHARED OWNER since 2026-08-15
// (playhead_skip_landing_frame, viewport.cpp), whose two arms this body selects
// between with `whole_piece`: FALSE takes Viewport::trim_range's own bounds (the
// bare pair), TRUE forces the piece's ends whatever the trim is (the ctrl pair,
// architect 2026-08-24). Both come back pre-clamped, and the clamp is idempotent
// on what move_playhead_to would clamp anyway.
//
// THE THREE STEPS STILL RUN UNGATED past the head refusal: the audition stop,
// the lane exit's selection clear and the overlay hide are unconditional
// wherever the body runs, and the head refusal fires only when all three
// WOULD do nothing and the jump would not move — the actionability owner's
// terms are this body's own writes, so the two cannot disagree. The hide is
// unconditional AT ITS OWNER (move_playhead_to hides before it writes,
// whatever the write turns out to be — the rule at clear_region_highlight,
// input_handler.h); the trim it derives from is untouched by any of this.
// THE SUCCESSION: the skips did not grey from 2026-08-15 to 2026-08-30 (a
// landing-only grey promised less than this body delivers, and gating the
// three steps to make it honest would change behaviour); the truthful-buttons
// ruling greyed them again that morning, knowingly forgoing the side acts on
// the dead face; the twin rule that evening folded the side acts into
// playhead_end_jump_actionable, so the grey, this body's refusal and the body
// are one decision — the full record is at the skips' case in
// redesign_button_enabled. (The refusal CARDED for one day, 2026-08-30 to
// 2026-08-31, when the one-dimensional rule silenced it; the grey stayed.)
void GuiInputHandler::run_playhead_end_jump(bool forward, bool whole_piece) {
    // A FORM THAT WOULD CHANGE NOTHING IS SILENT (architect 2026-08-31,
    // superseding the 2026-08-30 card): a benign one-dimensional refusal
    // already at its state says nothing — the playhead is one mark in one
    // place, and the glance that asks "did it move?" is the same glance that
    // answers it; the greyed skip button is the standing cue. The owner's
    // terms are exactly this body's writes — the stop, the clear, the mover's
    // overlay hide and the landing — so past this return at least one act
    // below does real work or the jump moves.
    if (!playhead_end_jump_actionable(app, audio, forward, whole_piece)) {
        return;
    }
    playback_lifecycle.stop_playback_if_playing();
    if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
        selection.clear_selection();
        viewport.invalidate_waveform_area();
    }
    viewport.move_playhead_to(
        playhead_skip_landing_frame(app, audio, forward, whole_piece));
}

// THE WAVEFORM-LANE PLAYHEAD STEP, one act owner for all three magnitudes
// (architect 2026-08-31, R12 — the step ladder: bare one painted column,
// Shift three, Ctrl ten, resolved at the dispatch through
// arrow_step_magnitude, gui_input.h). TWO CALLERS, and they are two SITES of
// one act rather than two acts: handle_plain_bare_keys' Left / Right case
// below (the bare form, where the bare road has always ended) and on_key's
// modified-arrow arm (the shift and ctrl forms, which must be claimed above
// the bare dispatch because that dispatch is entered only with no modifier at
// all). It was written inline in that switch until the ladder landed; the
// extraction is what keeps the stop, the stale-focus clear and the step from
// being spelled twice.
//
// IT IS REACHED ONLY WITH AN EMPTY SELECTION, because on_key's marker-lane
// branch claims the press first and returns — in every magnitude, the two
// arms carrying the same lane fork.
void GuiInputHandler::run_waveform_lane_playhead_step(int step_columns) {
    // The membership half of the clear below is therefore already satisfied;
    // the FOCUS half is not — last_selected_marker survives an empty selection
    // (a ctrl-toggle that empties the set repairs the focus rather than
    // dropping it, and sanitize can leave one behind), and clearing it is what
    // stops a stale focus from re-entering the marker lane on the next
    // selection gesture.
    // The stop lives HERE, in this lane only: the marker-lane routes carry
    // their own playback regimes (the position nudges stop in their prologue,
    // while the W+target refusal stops nothing at all), which is
    // exactly why on_key routes before reaching this body.
    playback_lifecycle.stop_playback_if_playing();
    if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
        selection.clear_selection();
        viewport.invalidate_waveform_area();
    }
    // Navigation playhead step: the overlay hide is the MOVEMENT OWNER's,
    // reached through move_playhead_pixels -> move_playhead_to (the rule at
    // clear_region_highlight, input_handler.h). The playhead is leaving the
    // overlay, and hiding discards nothing. AT THE WALL the landing is the
    // cursor itself (playhead_pixel_step_landing, the owner the Left
    // button's face reads since planner decision 60): the key still runs
    // the stop, the clear and the hide, the greyed button none of them. THE
    // WALL IS THE SAME WALL AT EVERY MAGNITUDE — the landing owner clamps into
    // the live domain, so a ten-column press near the end lands exactly ON the
    // end and one already resting there moves nothing whatever the modifier,
    // which is why the buttons' face can read the BARE step and still answer
    // for all three (the twin rule's own resolution, at
    // horizontal_arrow_step_actionable).
    viewport.move_playhead_pixels(step_columns);
}

void GuiInputHandler::handle_plain_bare_keys(GuiKey key) {
    switch (key) {
    case GuiKeys::Escape:
        // ESC CLEARS THE WHOLE NOTIFICATION STACK (architect 2026-09-01), at
        // the tail of its own ranking and nowhere else: every one of Esc's
        // other places is EARLIER in on_key, so reaching this arm means
        // nothing modal is standing and no render is in flight, and the last
        // thing the press can be aimed at is the stack. It is the X's BULK
        // keyboard twin — the X pressed on every card at once — and it READS
        // NO CLASS: a critical card goes with the rest, exactly as the X takes
        // any class (the bump is the act that skips criticals, and it is not
        // this one), which makes this the one act that reaches a critical card
        // without a pointer. THE SUCCESSION: the arm was born 2026-08-31
        // dismissing the stack's OLDEST card one press at a time, and the
        // whole stack has been its act since 2026-09-01, when the bulk clear
        // it duplicated (Ctrl+Esc, bound at on_key's head for one morning)
        // retired — one act wants one chord, and the bare key is the one the
        // hand already reaches for.
        //
        // WITH NO CARDS THE ARM IS THE RULED SILENCE (architect 2026-08-30):
        // a press that finds nothing to dismiss means "never mind" and is
        // already answered by the surface that did not close — a card would
        // answer a retraction with a complaint. It is an arm of its own and
        // not the bare default's for exactly that reason.
        //
        // IT IS RANKED, NOT PRIVILEGED: the X's own claim sits above every
        // veil because a card must be dismissable under any modal, while this
        // key sits UNDER all of them — a standing prompt, editor, player or
        // picker takes the press for its own close, and the card waits for
        // its clock, its X, or an Esc once that surface is down.
        notifications.dismiss_all();
        break;
    case GuiKeys::Left:
        // THE BARE FORM OF THE WAVEFORM-LANE STEP — one painted column back,
        // through the act owner directly above this dispatch, which the two
        // MODIFIED forms reach from on_key's own arm (the ladder's contract is
        // at that owner). The whole body lived here until 2026-08-31.
        run_waveform_lane_playhead_step(-1);
        break;
    case GuiKeys::Right:
        run_waveform_lane_playhead_step(+1);
        break;
    case GuiKeys::F:
        // Toggle follow mode. The full body (off→on edge resync) lives in
        // GuiPlaybackLifecycle::set_follow_mode, shared with the settings
        // editor's `follow=` commit.
        playback_lifecycle.set_follow_mode(!app.follow_mode);
        break;
    case GuiKeys::Y:
        // Toggle the centered pin (2026-08-31, R11). The full body — the
        // off→on edge's immediate recenter through the one derivation body —
        // lives in GuiPlaybackLifecycle::set_centered_mode, shared with the
        // settings editor's `centered=` commit and the icon-row button's
        // synthesized chord. History-less, one-shot, follow's own shape.
        playback_lifecycle.set_centered_mode(!app.centered_mode);
        break;
    case GuiKeys::C:
        // The center command, whose recipe and whose history-mode twin both live
        // at its owner (run_center_command). This arm is unreachable while the
        // mode stands — handle_history_mode_key claims `c` above this dispatch —
        // so the owner's fork decides nothing for it; it is the third caller,
        // `0`'s already-full-out arm, that the fork exists for.
        run_center_command();
        break;
    case GuiKeys::Home:
        // The trim-begin jump. The body is shared with End and with the two
        // CTRL forms (run_playhead_end_jump, above this dispatch).
        run_playhead_end_jump(/*forward=*/false, /*whole_piece=*/false);
        break;
    case GuiKeys::End:
        // The owner's `forward` arm, which is trim_range's END minus one — the
        // last frame INSIDE the window.
        run_playhead_end_jump(/*forward=*/true, /*whole_piece=*/false);
        break;
    default:
        // THE UNBOUND BARE KEY IS SILENT (architect 2026-08-30, the
        // unbound-keys ruling), the strict-modifier tail's other half and its
        // identical answer — this dispatch is the end of the bare road, so a
        // key reaching here binds nothing at all in this state (bare `u`, `g`,
        // `v`, `,` and `.` outside the `h` view; the digits 4..9; a keysym with
        // no spelling), and saying nothing is what identifies it as unbound.
        // AND THE GATES AGREE WITH THIS ARM SINCE 2026-09-01 (U4): the
        // read-only lock, the two drag gates and the loading gate carded those
        // five (and the walk's two shifted spellings) for the one state term
        // they were missing, so the same press said "not available on a
        // read-only tab" a lock away from the silence it gets here.
        // chord_is_bound reads the mode bit now (gui_input.h), and every one of
        // them answers a mode-only chord outside its mode exactly as this
        // default does. The rule and its consequences for the gates are at the
        // tail this arm pairs with (the end of on_key, input_handler.cpp).
        break;
    }
}

// Top-flag editor key routing. See the declaration for the consumed/command
// contract. ALL THREE kinds take the shared modal route (architect 2026-07-28)
// and differ only in their commit / cancel bodies and their repaint area: the
// bpm bracket editor draws in the MODAL DIALOG on the bottom row (like the
// settings editor; its damage is that row's own lane owner) and commits into a
// render sweep, the FlagPayload editor draws in the TOP strip and commits the
// flag's own payload, and the MeasureText editor draws in the TOP strip too and
// commits the marker's measure. None passes a bare-Tab hook, having no
// vocabulary to complete: in the BPM editor, a DIALOG, bare Tab walks the
// modal's focus ring from the first press, while for the two top-strip kinds it
// never reaches this route at all — the on_key gate swallows it, a flag editor
// publishing no dialog and so no ring (route_modal_editor_key).
// For all three, Ctrl+S saves with the editor left open and Esc / Enter are the
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
                viewport.invalidate_modal_dialog_area();
            },
            [this] { viewport.invalidate_modal_dialog_area(); });
    }
    if (app.top_flag_editor.kind == text_editor::Kind::MeasureText) {
        // The MEASURE editor: the same modal route, the same top-strip repaint
        // as the payload editor's — and STILL no waveform red-flash edge, even
        // though this kind DOES have a commit-time refusal since 2026-08-20
        // (the measure grammar is judged at commit_measure_edit). The reason is
        // the PAINTER, not the absence of a producer: the stem flash is gated
        // on Kind::FlagPayload at paint_marker_stems, so a MeasureText `red`
        // reaches no waveform pixel at all — its whole surface is the box in
        // the strip, which this route's repaint already covers.
        return route_modal_editor_key(
            app.top_flag_editor, key, mods,
            /*autocomplete=*/nullptr,
            [this] { flag_editor.commit_measure_edit(); },
            [this] { flag_editor.exit_top_flag_edit_no_commit(); },
            [this] { viewport.invalidate_top_strip(); });
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
        [this] { viewport.invalidate_top_strip(); });
    if (app.top_flag_editor.red != was_red) viewport.invalidate_waveform_area();
    return consumed;
}

// Settings-prompt editor key routing, through the shared modal route.
//
// THE VALUE COMPLETION IS BARE TAB'S, under the one autocomplete model stated
// at route_modal_editor_key (architect 2026-08-13: "we should use one model for
// all autocompletes"). The TYPED `=` carried this completion for part of that
// day and the trigger is REVERTED with the ruling: an `=` is once more an
// ordinary character, and this route says nothing about it. Tab offers the
// recall first and walks the focus ring when it did not advance the buffer, so
// the completion's own refusals (no `=` yet, a non-empty value side — which is
// every second Tab — and an unknown key) are exactly the Tabs that step.
bool GuiInputHandler::handle_settings_editor_key(GuiKey key,
                                                 GuiInputState mods) {
    return route_modal_editor_key(
        app.settings_editor, key, mods,
        [this] { return settings_editor.autocomplete_value(); },
        [this] { settings_editor.commit(); },
        [this] { settings_editor.exit_no_commit(); },
        [this] { viewport.invalidate_modal_dialog_area(); });
}
