// on_key dispatch helpers. Each is a GuiInputHandler method declared in
// input_handler.h; on_key calls them in sequence (if (handle_X(...))
// return;). Grouped here to keep input_handler.cpp focused on the event
// entry points and the pointer / wheel paths.

#include "input_handler.h"

#include "file_loader.h"     // apply_settings_engine_and_prefs (shared with load)
#include "frame_format.h"    // format_authored_frame (the revert act's line)
#include "history_diff.h"
#include "paint_handler.h"
#include "render.h"
#include "render_pipeline.h"
#include "settings_io.h"
#include "text_editor.h"
#include "warpmarkers.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
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

// Move `dir` to the DESKTOP TRASH with `gio trash`, the freedesktop trash
// spec's ordinary command-line front end. True iff the folder is gone from
// disk afterwards.
//
// THIS IS THE PRODUCT'S ONLY TRASHED DELETION (architect 2026-08-07), and the
// scope line is deliberate: the one caller is the `'` load-in-place's renders/
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
// the bare horizontal arrows move that MARKER and the cursor rides along.
bool GuiInputHandler::playhead_in_marker_lane() const {
    return !app.selected_markers.empty();
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
// render reads it.
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
// position-nudge / Delete chords, the flag and BPM editors' openers, `;` (the
// settings editor, whose engine-key commits ARE authored content), `i`,
// undo/redo (Ctrl+Z / Ctrl+Shift+Z), every propagate command — the copy (Ctrl+P)
// explicitly, the paste pair (Ctrl+Alt+P and Ctrl+Alt+Shift+P) structurally,
// their modifier combinations matching no allowlist predicate — and `'`, the
// load-in-place, which replaces the whole authored state.
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
// THE LOCK NOW HAS A FACE, AND THIS FUNCTION OWNS ITS MEMBERSHIP (architect
// 2026-08-15): ten roster buttons wear the disabled face while the active
// tab is locked — six in the icon row and, since the 2026-08-18 relayout moved
// them, four on the BOTTOM one — so the toggle looks the way the `h` history
// view already looks
// — the four marker verbs (bare `s`, Delete, Ctrl+D, Ctrl+N), the propagate
// copy/paste pair, the BPM and iteration openers, listen and the load-in-place,
// which is exactly what this allowlist drops. THE MIRROR IS HAND-LISTED at
// redesign_button_enabled (app_state.h) rather than derived by walking the
// chord table through this predicate, and the two classes that walk gets wrong
// are recorded there — chords claimed ABOVE this gate, whose "blocked" here is
// vacuous, and the buttons the ruling deliberately leaves lit. SO A CHANGE TO
// THE ADMISSIONS BELOW NEEDS A HAND EDIT THERE; nothing else in the product
// reads this gate for a face.
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
    // NOT the audition scrub: that is the waveform's one-shot POINTER act
    // (scrub_act_at — the lower half's motionless-release click act, its one
    // entry), a different act on a different surface, untouched here
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
    // Bare `t` (the S/T audio-view switch) IS PURE NAVIGATION AGAIN, and WRITES
    // NO STORE AT ALL since 2026-08-07. It used to write the warp store on one
    // edge — entering target view exited iteration mode through wipe_iter_state,
    // clearing every bracket and pushing an undo entry, admitted here because
    // iter brackets are session-only (never serialized, affects_persistence
    // false, excluded from the render recipe) so the write reached neither disk
    // nor a render. THAT WIPE IS DELETED with the ruling that iteration mode is
    // TARGET-LEGAL (the record is at handle_active_audio_view_toggle,
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
    // renders/ folder — the act is self-documenting output, not silent
    // authoring, which is the property that makes it unlike everything the gate
    // blocks. A locked tab that renders a sweep ends it in the same bracketless
    // state a writable one does. `i` itself is NOT admitted, so the mode cannot
    // be entered or left by key in a locked tab.
    const bool is_render =
        (ctrl && alt && !shift && key == GuiKeys::R);
    const bool is_render_misc =
        (ctrl && alt && shift && key == GuiKeys::R);
    // THE TRIM GESTURES (architect 2026-08-07): Shift+X maximizes the trim
    // window back to the full song, and bare `x` — which SET the trim from a
    // region until 2026-08-18 and SHOWS AND HIDES THE TRIM REGION OVERLAY since
    // — is the other half of the same surface. Trim is BAND, not content (the
    // header), so both are admitted, and their internal behavior is untouched:
    // Shift+X's identity guard, the setter's deselect, the playhead park and
    // the trim-mutation playback stop are all the same code taking the same
    // decisions. The repointing only made `x` EASIER to admit — it now writes
    // no trim bound at all, only a session visibility bit and then the
    // viewport, which is strictly less than the write the band ruling was
    // argued over. (Ctrl+Shift+X carried the show act from 2026-08-16 to
    // 2026-08-18 and is UNBOUND again; nothing here answers it, the
    // strict-modifier rule making an unbound combination a no-op everywhere.)
    const bool is_trim_x =
        (!ctrl && !shift && !alt && key == GuiKeys::X);
    const bool is_trim_shift_x =
        (!ctrl && shift && !alt && key == GuiKeys::X);
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
    // Ctrl+Z (undo) and Ctrl+Shift+Z (redo) — the whole family, alt binding
    // nothing on it — are NOT on the allowlist: both drop at this gate. The
    // old design admitted them because an undo entry
    // may target the OTHER (writable) tab, deferring the real decision to
    // do_undo / do_redo's per-entry target-tab peek. Under the gate-block,
    // undoing from a read-only tab first requires switching to the writable
    // tab (Ctrl+Tab) — accepted for gate legibility, so that authoring
    // mutations stop uniformly at the gate. The target-tab peek in undo.cpp
    // survives as a backstop for entries that outlive a mid-history lock.
    // Delete, `;`, `i`, `'` and the propagate copy/paste chords are likewise
    // absent (blocked here). The trim gesture LEFT that list on 2026-08-07 —
    // see is_trim_x above.
    return !(is_o || is_play_pause || is_playhead_step ||
             is_home_end || is_page_updown ||
             is_zoom_symbol || is_zero ||
             is_follow || is_center || is_sub_t || is_sub_p ||
             is_view_selector ||
             is_tab_cycle || is_ctrl_tab || is_ctrl_shift_tab ||
             is_esc || is_ctrl_q ||
             is_save || is_render || is_render_misc ||
             is_trim_x || is_trim_shift_x ||
             is_add_to_selection);
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
//   * run_history_revert — Ctrl+H, which rewrites the state the now side was
//     measured against and so must not leave the lane describing it;
//   * load_history_commit_in_place and load_history_local_entry_in_place — the
//     mode's own `'`, one act per walk;
//   * load_render_entry_in_place — the renders-side `'`, which has ONE caller
//     (load_editor_commit's non-mode branch) and therefore CANNOT run with the
//     view standing: in the mode that editor's Enter routes to one of the two
//     above. Its call is the idempotent no-op this function's early return
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
// tab keeps whatever band it had. THE THREE LOAD-IN-PLACES ARE UNAFFECTED and
// their asymmetry simply stops mattering — the COMMIT load applies the loaded
// file's own band some seventy lines past its call to this closer (the tab_a /
// tab_b replace, the live-band pull, the clamps), the LOCAL load applies none
// at all (a timeline state carries none), and neither now has a restore under
// or over it. AppState::HistoryMode's field block owns the ruling and the
// record of what went.
//
// THIS OWNER DOES NOT CLOSE A DROPDOWN, and since 2026-08-09 that is a decision
// rather than an unreachable case. EVERY USER-ACT CLOSER STILL CANNOT FIRE WITH
// ONE UP, positionally: bare `h`, the Ctrl+S checkpoint act, the Ctrl+H revert
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
    // THE VIEW'S REGIONS ARE VIEW-LOCAL (architect 2026-08-05): a span drawn in
    // here marks a passage of the checkpoint being read, so it leaves with the
    // view — the same rule the `,` / `.` step and the compare switch apply to
    // their own edges. It is also what keeps "a region rests only beside an
    // EMPTY selection" true for the EDITOR while the view's own press deselects
    // nothing: nothing formed in here can reach the editor at all. The clear is
    // unconditional and takes a span formed BEFORE `h` with it — accepted, and
    // the honest half of a view-local rule, the two being indistinguishable once
    // inside. Its damage is covered by this function's own full-window
    // invalidate below.
    clear_region_highlight(app, viewport);
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
    // with it, and the tab row's status chain rewrites its `n/N shortsha`.
    // Narrow damage would have to know all three, and none of them is worth a
    // rect. It
    // covers the restore's and the republication's own damage too, which is why
    // both above emit theirs and nothing here has to widen for them.
    viewport.invalidate_all();

    // THE DEFERRED PREFETCH KICK, FLUSHED (2026-08-07): a re-warm that arrived
    // while this visit stood was parked rather than run, the visit being bound
    // to the store's generation, and this is the first moment nothing is reading
    // it. STILL LAST, which is the point of its position: it starts a background
    // scan, so it must not interleave with the restore or the synchronous
    // republication above it — those two own this press's frame, and the kick owns
    // nothing but a worker. Through the one funnel, which reads the live source
    // and setting rather than anything the parked bit carried.
    if (deferred_history_prefetch_kick_) kick_history_prefetch();
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
// region clear, THEN this; at the exit it is the whole-struct reset, the stash
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
    if (!fresh.session.init(app, history_prefetch)) return false;
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
    // opened the visit at FULL ZOOM OUT through frame_history_view_whole_song —
    // the whole song in the window, the reading position a checkpoint review
    // starts from, and THE ONLY EDGE THAT FRAMED since 2026-08-08. Both are
    // deleted: the view owns no navigation state at all, so it opens over
    // whatever window the user is already in and leaves it there. The framer
    // survives with its OTHER caller, the diff-span framing's empty-delta arm.)
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
// A KICK WHILE THE VIEW STANDS IS DEFERRED, never dropped and never run: the
// visit is BOUND to the store's current generation, and a restart would clear
// the deque its indices name out from under it — `n/N`, the walls and the lane
// would all change subject mid-read. The bit is flushed at the exit owner, which
// is the first moment nothing is reading. (It is one bit rather than a queue
// because a kick carries no payload but the live source and setting, which the
// flush reads fresh.)
void GuiInputHandler::kick_history_prefetch() {
    if (app.history_mode.active) {
        deferred_history_prefetch_kick_ = true;
        return;
    }
    deferred_history_prefetch_kick_ = false;
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
// the close's own region clear and band restore then land over the top.
//
// ONE PRODUCER, and it is narrow: a view opened mid-scan whose run then fails.
// A run that had already failed refuses at init and never opens a view at all.
void GuiInputHandler::on_history_prefetch_ready() {
    const GuiHistoryPrefetch::DrainResult r = history_prefetch.drain();
    if (!app.history_mode.active) return;
    if (r.became_done && history_prefetch.run_failed()) {
        std::fprintf(stderr, "warptempo_gui: History is unavailable: %s\n",
                     history_prefetch.scan_failure_reason().c_str());
        finalize_active_drags();
        close_history_mode();
        return;
    }
    if (r.members_appended == 0 && !r.became_done) return;
    measure_history_head_delta();
    viewport.invalidate_all();
}

// FRAME THE VIEWED COMMIT'S DIFF SPAN — AN ON-DEMAND ACT, not an edge effect
// (architect 2026-08-05, superseding his own per-diff framing of earlier that
// day). The mode's edges move no viewport at all — the internal ones since
// 2026-08-08 and the ENTRY since 2026-08-18, the window being the user's from
// before `h` until after it; this is what the user asks for when he
// wants the differences filling the window, and its ONE caller is THE TRIM BAR'S
// PLAIN DOUBLE-CLICK — the regular views' span-framing gesture exactly, on the
// same band and through the same machinery, with this act as
// its command; since 2026-08-17 that means the mode router's own consume in
// handle_history_mode_press, which acts AT THE SECOND PRESS (each press site
// runs its own command, so no mode fork
// survives anywhere) (architect 2026-08-05, superseding the single click this shipped
// with earlier that day: the band behaves like a read-only tab's, where the trim
// drags refuse and the framing double-click still navigates). The bar is showing
// the diff span while the view stands (paint_trim's display-only substitution),
// so the gesture reads as "zoom to what the bar shows".
//
// IT IS IDEMPOTENT, which is what makes repeated clicking harmless: the framing
// ends in apply_zoom_to_start, whose current-vs-target compare no-ops when
// nothing moves.
//
// THE SPAN IS THE DISPLAYED DELTA'S, through the one accessor that forks on the
// (source, reading) pair, so it follows WHAT THE LANE IS SHOWING like every
// other reader — the walk from the lit tab, the reading from the session's own
// bit: two readings of one member generally differ in extent, and so do the two
// walks, so a click that framed some other pair's span would be showing one
// answer at another's magnification.
//
// THE RECIPE IS THE TRIM-BAR DOUBLE-CLICK'S, through its own framing core
// frame_span_into_view: the deterministic zoom-TO-span with the 2.5%-per-side
// margin, NOT the undo/redo restore's scroll-if-it-fits-else-frame variant (that
// one lives in undo.cpp's group tail and only ever zooms out as a fallback). The
// EMPTY-DELTA arm falls through to the whole-song owner below, which is the
// double-click's own whole-song arm — and is where the edges already are, so
// the click is harmless there rather than special.
//
// THE SPAN IS THE WHOLE DELTA'S, in source frames, from the delta itself
// (GuiHistoryCommitDelta::frame_span owns both of those choices) and converted
// into the ACTIVE domain through source_frame_to_active_domain — the wrapper
// whose target-view arm is nearbyint(map_source_to_target(...)), which is
// frame_to_paint_sample's own formula, so the framed span is exactly the span
// the lane paints its diff flags across in either view.
//
// IT MOVES THE VIEWPORT AND NOTHING ELSE: no playhead, no focus, no selection.
// The click leaves the playhead where the user put it, and framing is a reading
// act, not a navigation one.
//
// A ONE-FRAME SPAN (a single changed marker — the common case) is not a special
// case: the framer clamps its degenerate zero-width span to 1.0 before the log2
// and the fit level saturates at kMinZoom, so it rests at the deepest zoom
// centred on that frame.
void GuiInputHandler::frame_viewed_commit_diff_span() {
    if (!app.history_mode.active) return;
    if (audio.total_frames() <= 0) return;

    const GuiHistoryCommitDelta* d =
        app.history_mode.displayed_delta(app.history_compare());
    int64_t lo = 0, hi = 0;
    if (!d || !d->frame_span(lo, hi)) {
        frame_history_view_whole_song();
        return;
    }
    frame_span_into_view(app, audio, viewport,
                         source_frame_to_active_domain(app, audio, lo),
                         source_frame_to_active_domain(app, audio, hi),
                         /*margin=*/true);
}

// THE WHOLE SONG IN THE WINDOW — the FULL ZOOM OUT the diff-span framing falls
// back to when the viewed delta has no frames to frame.
//
// ONE CALLER SINCE 2026-08-18, re-derived by grep on this name:
// frame_viewed_commit_diff_span above, which falls through to it as its
// EMPTY-DELTA arm (the double-click's own whole-song answer, an on-demand act
// rather than an edge). NO MODE EDGE FRAMES AT ALL any more, and the
// succession is worth the two dates: each `,` / `.` step and each reading-or-
// walk switch stopped calling this on 2026-08-08 (architect, SUPERSEDING his
// own 2026-08-05 "three edges land at full zoom out"), so a pan and a zoom made
// once are read through every walk step, both walks and both readings — the
// viewport is UNIFIED across them — and THE ENTRY stopped calling it on
// 2026-08-18, when the view gave up owning navigation state altogether (the
// exit's restore went with it). Until then a visit opened over the whole song,
// which is the reading position a checkpoint review starts from: the delta's
// flags are laid out across the piece, and where they SIT is as much of the
// answer as what they say. The trim bar's double-click is how that overview is
// asked for now, on demand and at the user's own moment.
//
// FULL ZOOM OUT IS SPELLED AS THE SPAN FRAMER'S WHOLE-SONG ARM, [0, total] with
// NO margin, which the framer's centering plus the wall clamp degenerate to the
// per-file effective ceiling at start 0 — the same rest bare `0` reaches through
// effective_max_zoom_level, arrived at through the mode's one framing route
// rather than a second recipe.
//
// IT MOVES THE VIEWPORT AND NOTHING ELSE, exactly as the span framing does: the
// playhead stays where it is, and the entry's own focus clear and stash drop are
// its caller's.
//
// The `!active` guard is the defensive shape the framing above carries, for the
// same reason: every caller is inside the mode already.
void GuiInputHandler::frame_history_view_whole_song() {
    if (!app.history_mode.active) return;
    if (audio.total_frames() <= 0) return;
    frame_span_into_view(app, audio, viewport, 0,
                         live_total_frames(app, audio), /*margin=*/false);
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
//   * a resting REGION clears (2026-08-05, the view-local rule — planner-included
//     on the step's own edge argument, the architect having named the exit and
//     the step): a span drawn in here marks a passage of the delta being read,
//     and the arriving reading is a different delta;
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
    clear_region_highlight(app, viewport);
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
//   * bare Home / End      — the ABSOLUTE ends of the song;
//   * bare `c`             — working zoom, centered on the mode's own focus.
// THERE IS NO CTRL SHAPE HERE SINCE 2026-08-18: Ctrl+Tab and Ctrl+Shift+Tab
// were the walk cycle's two directions from 2026-08-05 / 07 and left with the
// walk selector. Returns true when the press was consumed.
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
// Tab, Home/End and `c` fall through to the ordinary
// dispatch and behave exactly as they always have. Bare `u` and bare `g` are
// bound NOWHERE there, so falling through is the unbound key's own consumed
// nothing. NOTHING CTRL-CARRYING IS CLAIMED AT ALL since 2026-08-18: this
// predicate claimed Ctrl+Tab from 2026-08-05 and Ctrl+Shift+Tab from
// 2026-08-07 in both states, which cost neither anything (the claim is a
// membership test and the arm below it was not reached with the mode down), and
// both are gone with the walk cycle they served.
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
//   * BARE Tab and SHIFT+TAB — two of the bottom row's MARKER-WALK GROUP since
//     2026-08-15, answered LIVE in the view by this predicate and doing the
//     mode's own diff-flag cycle. THE GROUP'S THIRD, Ctrl+Shift+Tab, IS NOT
//     THIS PREDICATE'S ANY MORE (2026-08-18): the reverse walk cycle it ran in
//     here died with the walk selector, so the chord falls to the allowlist,
//     which blocks it — and that button GREYS in the view, honestly, its live
//     act being the paired marker march this view has no use for.
// Which leaves BARE `h` — the history button's own chord, and the one shape
// here bound outside the mode at all.
bool history_mode_owns_key(GuiKey key, GuiInputState mods) {
    if (mods.alt) return false;
    // CTRL CLAIMS NOTHING AT ALL SINCE 2026-08-18, and the branch stays as a
    // REFUSAL rather than being deleted: every shape below it is BARE (or
    // shift-carrying, on Tab and the walk), so a ctrl press falling past this
    // line would reach the bare list and be read as its unmodified twin — a
    // Ctrl+H would come out as the mode's own `h` toggle and close the view
    // instead of reverting into it. One line, and strict modifier validation
    // holds.
    //
    // (WHAT IT CLAIMED: from 2026-08-05 Ctrl+Tab stepped the WALK SOURCE one
    // tab right and, from 2026-08-07, Ctrl+Shift+Tab one tab left — both in the
    // plain Tab spelling and in the IsoLeftTab one xkb puts on the Tab key's
    // shift level. The walk is bare `g` below now, so both fall through to the
    // allowlist, where they part company: Ctrl+Tab is ADMITTED — an A/B tab
    // switch works normally in the view, the architect's own ruling — while
    // Ctrl+Shift+Tab, the paired marker march, is BLOCKED, which is exactly the
    // consumed no-op it was before 2026-08-07 and for the reason the bare cycle
    // stopped being live: nothing in here navigates by live markers.)
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
        if (app.history_checkpoint_in_flight) {
            std::fprintf(stderr,
                "warptempo_gui: history: A checkpoint is still publishing; "
                "try again when it finishes\n");
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
    // stash dropped, region cleared, lane republished synchronously, window
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
    // and since 2026-08-07 they are SHIFT-ADMITTING (redesign_button_shift_
    // admits, app_state.h), so a shift-click reaches the jump through that same
    // one route and their tooltips carry the shift line that names it.
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
        const std::size_t count = app.history_mode.walk_count();
        const std::size_t here  = app.history_mode.walk_index();
        // THE OLDEST INDEX, and the empty walk's answer with it: an empty walk
        // has one address (0) and stands at it, so both keys resolve to `here`
        // and fall out of the wall check below with nothing to do — no case of
        // its own.
        const std::size_t oldest = count == 0 ? 0 : count - 1;
        std::size_t there;
        if (key == GuiKeys::Comma)
            there = mods.shift ? oldest : std::min(here + 1, oldest);
        else
            there = mods.shift ? 0 : (here == 0 ? 0 : here - 1);
        // THE WALL IS ONE CHECK FOR BOTH SHAPES: a step that would run off the
        // end and a jump made while already standing at that end are the same
        // consumed no-op, with no edge and nothing moved. The walk has ends, and
        // reaching one must not wrap or refuse loudly.
        if (there == here) return true;
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
        // AND THE REGION GOES WITH THE COMMIT (architect 2026-08-05, the
        // view-local rule): a span drawn in here marks a passage of the
        // checkpoint it was drawn against, and the step is leaving that
        // checkpoint. Same reasoning as the focus clear above, on the same edge.
        clear_region_highlight(app, viewport);
        // THE VIEWPORT IS THE USER'S ACROSS A STEP (architect 2026-08-08,
        // SUPERSEDING the 2026-08-05 per-edge reset to full zoom out): he pans
        // and zooms once and reads the SAME WINDOW through every step of the
        // walk, so a checkpoint's flags are compared against the previous one's
        // at the magnification he chose rather than at an overview he has to
        // re-establish after each press. NO EDGE FRAMES AT ALL since
        // 2026-08-18, the entry's own framing having gone with the view's
        // navigation-state ownership: the trim bar's double-click is the view's
        // one framing gesture, the on-demand "show me this delta's span". So
        // this edge writes NO viewport at all.
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
    // that commits a new cursor position stops a live audition and dissolves a
    // resting region (the keyboard stop rule at stop_playback_if_playing, whose
    // cursor-moving navigation class names Home/End and the Tab family; the
    // clear-site set at clear_region_highlight). That is where they part from
    // the mode's diff-flag CLICK, which deliberately touches neither — a pointer
    // route with its own recorded regime.
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

    // BARE TAB / SHIFT+TAB / IsoLeftTab — THE DIFF-FLAG CYCLE, mode-local. Tab
    // steps to the next flag, Shift+Tab and IsoLeftTab to the previous, in the
    // list's own order: rebuild_history_diff_flags leaves `flags` sorted
    // ASCENDING BY time_frame, so list order IS reading order and no second
    // ordering is derived here (the hit stash indexes this same list).
    // NO WRAP, mirroring the live cycle: it lands on the nearest stop in the
    // walk direction and does nothing at all with none ahead, so a Tab on the
    // last flag is a consumed no-op here too. With NO focus standing Tab takes
    // the FIRST flag and Shift+Tab the LAST, which is the same rule read from
    // outside the list. An empty list is a consumed no-op, in all three of its
    // shapes: an empty delta, an active column whose half of the delta is empty,
    // and the DROPPED-AND-NOT-YET-REBUILT list, whose window every mode edge
    // closed on 2026-08-07 by republishing inside its own press — the rebuild's
    // own refusals (loading or absent audio, no plate yet) still leave it open, so
    // the cold answer stays this arm's, and it is what a step and a Tab arriving in
    // one key batch would have cycled the leaving commit's flags with
    // (drop_lane_stash_across_history_edge owns the whole argument).
    if (key == GuiKeys::Tab || key == GuiKeys::IsoLeftTab) {
        const int n = static_cast<int>(app.history_mode.flags.size());
        if (n == 0) return true;
        const bool forward = (key == GuiKeys::Tab && !mods.shift);
        const int here = app.history_mode.focus;
        int there = -1;
        if (here < 0 || here >= n) {
            there = forward ? 0 : n - 1;
        } else if (forward) {
            if (here + 1 >= n) return true;   // last already
            there = here + 1;
        } else {
            if (here == 0) return true;       // first already
            there = here - 1;
        }
        playback_lifecycle.stop_playback_if_playing();
        // THE CYCLE REPLACES THE SELECTION WITH ITS STOP, the live cycle's own
        // shape: the set clears and the focus alone stands, which is the plain
        // click's rest too. Ordered so the clearer cannot undo the focus it
        // writes.
        clear_history_mode_focus(app.history_mode);
        app.history_mode.focus = there;
        clear_region_highlight(app, viewport);
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
        // A DISCRETE COMMAND and the focus ALWAYS moved to get here (every arm
        // above either returned or picked a different index), so the full-window
        // damage the mode's focus click emits on a move is unconditional.
        viewport.invalidate_all();
        return true;
    }

    // BARE HOME / END — THE ABSOLUTE ENDS OF THE SONG, deliberately NOT the trim
    // bounds the live arms jump to (architect 2026-08-05). The view reviews the
    // WHOLE piece: a checkpoint's delta is laid out across every authored frame,
    // trimmed window or not, so an End that stopped at a trim bound would hide
    // the flags past it. With a full trim window the two answers coincide
    // (trim_window_is_full), so the difference shows only under a set trim.
    if (key == GuiKeys::Home || key == GuiKeys::End) {
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
        clear_region_highlight(app, viewport);
        // The ACTIVE domain's own ends: live_total_frames is what the displayed
        // timeline runs to in either view, and it is the same total the
        // full-window trim range resolves to. THE ARITHMETIC LIVES AT THE
        // SHARED OWNER since 2026-08-15 (playhead_skip_landing_frame,
        // viewport.cpp), which forks on this very mode bit — one spelling for
        // the three arms that jump, and for nothing else: the bottom row's two
        // SKIP buttons read it for one revision and no longer do (they are lit
        // unconditionally, the ruling and its reason at their case in
        // redesign_button_enabled). The owner's clamp is idempotent on a value
        // move_playhead_to would clamp anyway, so this jump is byte-identical
        // to the hand-spelled one.
        viewport.move_playhead_to(
            playhead_skip_landing_frame(app, audio, key == GuiKeys::End));
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
//                             pure viewport move: it is `c`, region clear, stop
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
//                             handle_active_audio_view_toggle,
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
//                             playhead too. The region clear is scratch, the
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
//   - ' (bare)              → THE LOAD EDITOR, and the mode's one admitted
//                             MUTATOR (2026-08-04). It is admitted because in
//                             the mode it loads something else in place: the editor
//                             opens prefilled with the viewed member and loads
//                             THAT MEMBER's state in place, which is the
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
//                             IT IS ADMITTED ON BOTH WALKS since 2026-08-08
//                             (architect, superseding the 2026-08-07 ruling that
//                             the Local walk consumes it): the REMOTE tab takes
//                             a COMMIT SPELLING and loads its three sidecars
//                             (load_history_commit_in_place), the LOCAL tab takes
//                             a member NUMBER and loads that state of the
//                             session's own timeline as a new undo entry
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
//                             commit icon and the label "Save and Commit" while
//                             the mode stands.
//                             THE PLAIN DISK SAVE HAS NO HOTKEY IN THE VIEW,
//                             and that is the ruling rather than a gap: a
//                             settings-only drift — the one thing the act's own
//                             head-delta grey calls "nothing to checkpoint" — is
//                             saved by leaving the view first. (Ctrl+S inside
//                             the commit-title editor is still the plain save,
//                             through the five-editor modal contract, which this
//                             gate never sees: the keyboard-modal gate sits
//                             above it.)
//                             IT IS ONE OF THE THREE ADMISSIONS CONDITIONAL ON
//                             THE SESSION, and the first of them
//                             (architect 2026-08-05 for the head delta,
//                             2026-08-07 for the in-flight bit, both inherited
//                             from the chord this act moved off): with the HEAD
//                             DELTA EMPTY — the newest checkpoint already
//                             carrying this session's authoring content — AND NO
//                             PUSH STILL OWED, there
//                             is nothing to do, so the chord drops here
//                             as a consumed no-op and the SAVE button takes its
//                             row's disabled face from this same line. The delta
//                             bit
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
//                             term is the same one
//                             decision for one checkpoint at a time; it is
//                             structural rather than visible in here (the act
//                             closes the view and `h` will not reopen one over a
//                             publishing repository), while the GLOBAL save
//                             lockout that DOES show is GuiSaveOps::save's own
//                             term, mirrored by the "Committing..." face.
//   - Ctrl+H (no shift/alt) → THE REVERT ACT, the mode's THIRD admitted mutator
//                             (architect 2026-08-05) and admitted on the same
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
//                             (history_mode_revert_subject_standing, app_state.h
//                             — the KEY GATE, and since 2026-08-15 its ONE
//                             reader; it must not gain a face reader again).
//                             THE FACE NO LONGER MIRRORS IT: the Revert button
//                             greyed from this same line — one decision for the
//                             chord and the glyph, the cleanest shape the
//                             enabled predicate had — and the architect
//                             reversed that, because this bit MOVES DURING A
//                             VISIT (every click that selects or clears changes
//                             it) so the grey tracked the diff-flag SELECTION
//                             and blinked at interaction cadence, which is the
//                             same argument that took the four cardinal arrows
//                             always-on. The button stays lit with an empty
//                             subject and a click on it is a consumed no-op,
//                             the roster's standing shape for a refusal; the
//                             full record is at the four history companions'
//                             arm in redesign_button_enabled, app_state.h.
//                             IT IS NOT DISPATCHED FROM HERE, and not from a
//                             mode arm either: admitting it lets the press fall
//                             through to on_key's ordinary body, which is what
//                             puts it BELOW the read-only gate — a locked tab
//                             refuses it exactly as it refuses `'` (the lock
//                             means hands off the piece's authored state, and
//                             this act writes it; Save-and-Commit, which
//                             authors nothing, is admitted by that gate
//                             instead).
//   - Ctrl+Q                → the close routing.
//   - Esc (bare)            → ITS EXISTING BINDINGS, AND NOT ONE OF ITS OWN
//                             (architect 2026-08-04, closing the arc's recorded
//                             cost). Admitting it adds NO seventh Esc place: the
//                             bare-Esc inventory is still the six enumerated at
//                             on_key's dispatch point (input_handler.cpp), and
//                             this line lets exactly the two that sit BELOW it
//                             run — the REGION CLEAR (a span formed
//                             before `h`, or one formed INSIDE the view by its
//                             own placement press and drag: the clear is
//                             reachable from within, which is fine — the region
//                             is scratch and its clear has no side effects) and
//                             the RENDER / BATCH CANCEL (a render
//                             launched before `h`, whose progress line the mode's
//                             corner outranks). Both sit BELOW this gate in
//                             on_key and neither mutates authored state, so the
//                             frozen now side is untouched — the same argument
//                             the read-only allowlist admits Esc on.
//                             IT CANNOT CLOSE THE VIEW, structurally rather than
//                             by refusal: the toggle is handle_history_mode_key's,
//                             and that function's whole vocabulary
//                             (history_mode_owns_key, whose declaration below
//                             enumerates it) carries no Esc shape in any modifier
//                             combination, so no Esc reaches it. The
//                             view's exits are unchanged, and `h` is still the
//                             key that leaves. With no region resting and no
//                             render running a bare Esc is a consumed nothing,
//                             which is what it is everywhere else too.
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
// this predicate is what admits File's Ctrl+Q in there.
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
// for the two anchors alone, which have no chord to ask about: Settings dead
// on the toggle_dropdown lockout, File live since 2026-08-13 — a third,
// Navigation, was live from 2026-08-08 until its 2026-08-15 deletion), never
// hand-listed —
// history_mode_disables_button, input_pointer.cpp, which carries the whole
// inventory.
//
// THE PREDICATE IS FREE, NOT A MEMBER, for exactly that second reader: it is
// pure, and the face derivation asks it about a table of chords with no press
// and no handler in hand. IT TAKES THE WHOLE AppState alongside key+mods because
// FOUR admissions are conditional on session state (the commit act's — Ctrl+S
// since 2026-08-08 — on head_delta_empty AND on no checkpoint already being in
// flight, the revert act's, on a subject standing, and the load-in-place's, on
// the active walk carrying a member), and both readers hand it
// the same `app` — each condition is decided HERE and restated at neither
// caller, which is what keeps the key that refuses and the face that greys one
// decision rather than two spellings of one. ONE ADMISSION HAS NO FACE READER
// SINCE 2026-08-15 and that is a scope, not a second spelling: the revert act's
// subject term still decides the KEY here, but redesign_button_enabled lifts
// the four history companions over the derived partition entirely, so Revert
// stays lit on an empty subject and its click is a consumed no-op — the
// architect's reversal of a grey that tracked the diff-flag selection and
// blinked at interaction cadence, recorded at that arm in app_state.h. It took
// the HistoryMode struct
// alone until the in-flight bit arrived, which lives on AppState because the act
// outlives the view it was launched from.
bool history_mode_key_blocked(GuiKey key, GuiInputState mods,
                              const AppState& app) {
    const AppState::HistoryMode& mode = app.history_mode;
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    const bool bare  = !ctrl && !shift && !alt;
    const bool is_zoom_symbol =
        ((key == GuiKeys::Equal || key == GuiKeys::Minus) && bare);
    const bool is_zero  = (key == GuiKeys::Digit0 && bare);
    const bool is_page_updown =
        ((key == GuiKeys::PageUp || key == GuiKeys::PageDown) && bare);
    // THE LOAD-IN-PLACE IS EITHER WALK'S ACT (architect 2026-08-08, superseding
    // the 2026-08-07 "the Local walk consumes it": a local member is a STATE of
    // this session's timeline, and loading a state in place is exactly what the
    // act does). The two differ only in what the editor asks for — a commit
    // spelling on the Remote tab, a member NUMBER on the Local one — and the
    // routing lives at load_editor_commit, not here.
    //
    // THE TERM IS THE ACTIVE WALK'S, AND IT IS ONE TERM FOR BOTH SINCE
    // 2026-08-09: the walk must have a member. The act loads THE VIEWED ONE, so
    // a walk with none has nothing to prefill and nothing to load, and one
    // decision refuses the key and greys the icon row's load-in-place button —
    // the head delta's own shape.
    //
    // IT WAS THE LOCAL ARM'S ALONE until the empty COMMIT walk became a legal
    // standing state: the Local walk carries U + R + 1 members and can only be
    // empty UNBOUND, while the Remote one now opens at `0/0` whenever a piece
    // has no eligible checkpoint. So the honest gate against the blank-lane
    // walk covers both, and the source fork this predicate carried for it is
    // gone (the vocabulary fork stays where it always was, at the opener and at
    // load_editor_commit).
    const bool is_load_in_place =
        (key == GuiKeys::Apostrophe && bare && mode.walk_count() > 0);
    // THE REVERT ACT (2026-08-05), the mode's THIRD admitted mutator and its
    // SECOND session-conditional admission: Ctrl+H is admitted only while there
    // is a subject to revert — a selected diff flag, or the focused one — so
    // with nothing selected the chord drops here as a consumed no-op. THE
    // REVERT BUTTON TOOK ITS ROW'S DISABLED FACE FROM THIS SAME LINE UNTIL
    // 2026-08-15 — one decision for the chord and the glyph — and the architect
    // reversed the face half alone: the grey tracked the diff-flag SELECTION,
    // so it blinked at interaction cadence, the same argument that took the
    // four cardinal arrows always-on. The button is lit with an empty subject
    // and the click is a consumed no-op; this term is the KEY GATE only, and
    // history_mode_revert_subject_standing has exactly one reader — this line.
    // Unlike
    // the two mutators above it, this chord is NOT dispatched from a mode arm:
    // it falls through to on_key's ordinary body, BELOW the read-only gate, so a
    // locked tab refuses it exactly as it refuses `'`.
    const bool is_revert_act =
        (ctrl && !shift && !alt && key == GuiKeys::H &&
         history_mode_revert_subject_standing(mode));
    // CTRL+S IS THE ACT IN HERE (architect 2026-08-08, moving it off Ctrl+Alt+R
    // — the act saves first, so it belongs on the save chord). It is admitted
    // while there is something TO DO and no checkpoint is already in flight
    // (2026-08-07, single-in-flight): with either condition failing the chord is
    // not admitted at all, which is both the key's refusal and the Save button's
    // grey.
    //
    // IT ASKS ONE QUESTION: IS THERE ANYTHING TO CHECKPOINT? — the head delta,
    // live against the newest commit. Static once measured, and it rests TRUE
    // (greying the act) in the window before the prefetch has delivered member 0
    // to measure against (2026-08-07, measure_history_head_delta owns that rule).
    //
    // A CLEAN-BUT-UNPUSHED SESSION IS GREY HERE, AND THAT IS THE MODEL RATHER
    // THAN A GAP (2026-08-09): the act publishes what it commits, and a branch
    // already committed and merely unpushed is fixed in the TERMINAL, where the
    // user has git. A push-pending bit admitted the chord for an in-app retry
    // until this date; the retry family went with the graded machinery, and the
    // act's clean arm now says so on stderr in as many words.
    //
    // THE PLAIN DISK SAVE IS NOT SEPARATELY ADMITTED, deliberately: in the view
    // this chord has exactly one meaning, and a session with nothing to
    // checkpoint saves by leaving the view.
    const bool is_save =
        (ctrl && !shift && !alt && key == GuiKeys::S &&
         !mode.head_delta_empty &&
         !app.history_checkpoint_in_flight);
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
    // ITS SHIFTED TWIN STAYS OUT — Ctrl+Shift+Tab, the paired marker march,
    // which is exactly where it stood before 2026-08-07 and for the bare
    // cycle's own reason: it navigates by LIVE MARKERS, and the lane in here
    // shows diff flags. It was the mode's REVERSE walk cycle from 2026-08-07
    // until the walk left row 3 on 2026-08-18; with no cycle to run it is a
    // consumed no-op again, and the bottom row's third walk button greys
    // through the derived partition to say so.
    const bool is_ctrl_tab =
        (ctrl && !shift && !alt && key == GuiKeys::Tab);
    return !(is_zoom_symbol || is_zero || is_page_updown ||
             is_audio_view_switch || is_marker_view_switch ||
             is_view_selector || is_esc || is_ctrl_tab ||
             is_load_in_place || is_revert_act ||
             is_save || is_ctrl_q);
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
// The guards are the act's preconditions restated as "there is something to ask
// about": no mode, or a session that never resolved a piece directory, and there
// is no commit to offer. Neither is reachable from the one call site (the chord
// is admitted only while the mode stands, and an available session always
// carries both strings), which is why they are silent. The allowlist narrows it
// further without moving that reachability — "nothing to do" (an empty head
// delta, 2026-08-05) and a checkpoint
// already in flight (2026-08-07) both drop the chord ABOVE the `s` arm, so
// nothing can raise this editor over a session with nothing to do or a worker
// mid-act, and neither refusal has to be spelled here.
//
// PLAYBACK STOPS AS THE MODAL OPENS, through the shared owner and past every
// guard, exactly as the three editors before it do. It is a structural no-op in
// practice — the history view is silent by ruling, its entry having stopped any
// session running before `h` — and it is here because the rule is the modal's,
// not the surface's.
void GuiInputHandler::open_history_commit_editor() {
    if (!app.history_mode.active) return;
    if (text_editor::is_active(app.commit_title_editor)) return;
    const std::string& dir = app.history_mode.session.project_directory();
    if (dir.empty() || app.history_mode.session.sidecar_base_name().empty()) {
        return;
    }
    playback_lifecycle.stop_playback_for_modal_open();
    text_editor::enter(app.commit_title_editor,
                       /*target=*/0,
                       /*locked_prefix=*/"",
                       history_checkpoint_title(dir),
                       text_editor::Kind::CommitTitle);
    // OPEN-SELECTED ON THE SEED, the prefilling openers' convention (the flag
    // editor's, and the load editor's in the mode): the first keystroke replaces
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
        [this] { commit_title_editor_exit_no_commit(); },
        [this] { viewport.invalidate_modal_dialog_area(); });
}

// THEN DO IT — the commit-title editor's Enter, and the only caller.
//
// THE BYTES ARE REBUILT FRESH, NEVER THE SESSION'S FROZEN NOW SIDE, and this is
// the one place in the mode where the difference between them is real. The
// frozen side is honest about AUTHORED state — the mode's gates refuse every
// route that could change a marker or an engine setting — but the settings file
// also carries the per-tab VIEW BAND, and both allowlists admit routes that move
// it (membership re-derived 2026-08-12 under pan-primary): zoom, the paged
// scroll, the plain wheel's stepped pan, the overview command,
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
// report through the BOTTOM ROW'S CRITICAL SLOT (architect 2026-08-09,
// superseding the acknowledge notice they reported through until then) instead
// of through a view left standing.
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
// per-write stderr, the same mark_saved + recompute_dirty tail), never a second
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
    if (!app.history_mode.active) return;
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
    // allowlist's Ctrl+S refusal derives a grey on the SAVE-AND-COMMIT face,
    // which exists only inside the view — the view has just closed and `h`
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
// IT WRITES THE CRITICAL SLOT (architect 2026-08-09, REPLACING the acknowledge
// modal this raised from 2026-08-07): a failed checkpoint is critical, so its
// report is PERMANENT and PAINT-ONLY — the bottom row's leftmost cell, in the
// product's one invalid red, standing until a later checkpoint succeeds or the
// program closes. AppState::critical_error_message owns the contract; this is
// its one producer.
//
// THE PARTITION, over the act's SIX verdicts (GuiHistoryCommitOutcome,
// history_diff.h, whose contract comment owns what each one establishes):
//   ESTABLISHED, AND THEY CLEAR THE SLOT — Committed (made and published, the
//   ordinary ending) and NothingToCommit (the newest checkpoint already carried
//   these bytes AND the remote already had it). Neither is a failure, and either
//   one supersedes a failure the slot is still showing: the message describes
//   the repository's last answer, and this is a newer one.
//   THE FOUR FAILURES — WriteFailed (nothing reached the repository at all),
//   CommitFailed (git made no checkpoint the act can stand behind), Committed-
//   NotPushed (the checkpoint is in the local branch and the remote has not got
//   it) and Unconfirmed (the act could not establish its answer). WHAT EACH ONE
//   ESTABLISHES IS THE ENUM CONTRACT'S TO SAY, and it says it once
//   (GuiHistoryCommitOutcome, history_diff.h) — this end of the wire needs only
//   which class each falls in, so the arms are not re-enumerated here.
//   The texts differ exactly where the
//   user's next move does, which is why the act distinguishes them at all. They
//   are SHORT because the row is one line and the detail is already on stderr,
//   verbatim and unchanged by this arc.
//
// AN UNANSWERED QUESTION IS NOT A SUCCESS, which is the whole point of the sixth
// verdict: Unconfirmed came back as NothingToCommit until 2026-08-09, so an act
// that established neither content nor publication CLEARED a standing critical
// report — the one thing this slot must never do on anything but a newer,
// better answer.
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
// paint-only slot can be written from any clock at all, because it takes nothing
// from anyone.
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
        // THE TWO ESTABLISHED ANSWERS, and the only two that clear the slot:
        // the checkpoint is committed AND the remote observably carries it, so
        // a failure the slot was showing is superseded — including one the user
        // fixed in the terminal, which is exactly what the act's clean arm
        // re-observes.
        app.critical_error_message.clear();
        break;
    case GuiHistoryCommitOutcome::WriteFailed:
        app.critical_error_message = "Checkpoint failed: nothing was committed";
        break;
    case GuiHistoryCommitOutcome::CommitFailed:
        app.critical_error_message =
            "Checkpoint failed: files written but not committed";
        break;
    case GuiHistoryCommitOutcome::Unconfirmed:
        // NOTHING WAS ESTABLISHED HERE, so nothing is DISPLACED here: it does
        // not overwrite a report already standing. THE OVERWRITE IS THE POINT OF
        // THE CONDITION — an act run over a session that already reads
        // "committed; push failed" and comes back merely unconfirmed would
        // replace the actionable text with a vaguer one, losing the thing the
        // user could act on to an answer that added nothing. So it FILLS AN
        // EMPTY SLOT and otherwise leaves the standing report alone, which is
        // what "a standing report stands" has to mean for an outcome that
        // establishes nothing.
        if (app.critical_error_message.empty()) {
            app.critical_error_message = "Checkpoint could not be confirmed";
        }
        break;
    case GuiHistoryCommitOutcome::CommittedNotPushed:
        // The commit landed and the push did not. The fix is `git push` in the
        // terminal; the next checkpoint act observes the branch against its
        // remote and takes this report down.
        app.critical_error_message = "Checkpoint committed; push failed";
        break;
    }

    // THE ROW'S OWN DAMAGE, UNCONDITIONAL, and it is the DAMAGE that is
    // unconditional rather than the write: five arms always write the slot (the
    // two established ones by clearing it, which erases a cell that was painting
    // a moment ago and is exactly as much a change as setting one), while
    // Unconfirmed writes only into an empty slot and may well leave the row
    // untouched. Damaging anyway costs one repaint of one row on a keypress-rare
    // event and needs no arm to remember it. This is the chip's one
    // invalidation (invalidate_status_chain_area — the TAB ROW's lane, where
    // the chip has been the chain's leftmost member since 2026-08-13), the
    // same call every transient-status writer makes.
    viewport.invalidate_status_chain_area();
}

// -- THE REVERT ACT --------------------------------------------------------
//
// CTRL+H, THE HISTORY VIEW'S THIRD MUTATOR (architect 2026-08-05): apply the
// SELECTED diff flags' INVERSES to the live store of the active column, then
// close the view. The one caller is on_key's own Ctrl+H arm, which is reached
// only while the mode stands, only past the read-only gate, and only with the
// allowlist having admitted the chord — which it does only while a subject
// stands (history_mode_revert_subject_standing, app_state.h, whose ONE reader
// is that key gate). The Revert BUTTON'S grey read the same decision until
// 2026-08-15 and no longer does: the architect reversed the face half alone,
// the grey having tracked the diff-flag selection and blinked at interaction
// cadence — the argument that took the four cardinal arrows always-on — so the
// button is lit with no subject and the click is a consumed no-op, the roster's
// standing shape for a refusal. The record is at the four history companions'
// arm in redesign_button_enabled, app_state.h; that predicate must not gain a
// face reader here again.
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
// the line holds, and the ONE grammar that reads it is the parser's. The phase
// reset column needs no such trip — frame plus the disable bit IS its line.
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

    const bool phase = (app.active_markers_view == 'P');
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
            if (at >= 0) {
                ++sk;
                // IDENTICAL IS NOT A CHANGE — the canonical line the occupant
                // would save as against the then side's, through the ONE
                // serializer, which is the same line vocabulary the delta itself
                // is computed in (history_diff.h). The frames are equal by
                // construction, so for this column the compare is the disable
                // bit; it is spelled as the line anyway, symmetrically with the
                // warp arm below, whose payload has no such shortcut.
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
        // serializer), the '|' and the verbatim payload token.
        std::string line;
        if (f.then_disabled) line += '#';
        line += format_authored_frame(f.time_frame);
        line += '|';
        line += f.then_token;
        auto parsed = warpmarkers_internal::parse_single_canonical_line(line);
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
            // the seven serialized fields and IGNORES the session-only iter/bpm
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

// THE OPEN DROPDOWN'S keyboard gate — ONE gate for BOTH menus, because there is
// one popup state and a dropdown is a dropdown (the Navigation menu joined
// 2026-08-02 and the File one 2026-08-13, each needing nothing here, and the
// Navigation menu's 2026-08-15 deletion needed nothing either: bare Esc is the
// SIXTH bare-Esc binding through all of it, never a seventh and never a fifth). Returns true when the press is SWALLOWED (the
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

// The DIALOG-HOSTED modal editors: the settings editor, the load
// editor, the bpm editor (top_flag_editor reused with Kind::BpmBracket) and,
// since 2026-08-07, the history view's commit-title editor — the four
// surfaces painting in the centered modal dialog since 2026-08-12 (they
// lived on the bottom strip before, whence the predicate's old
// modal_bottom_strip_editor_active name; the membership is unchanged) —
// plus the prompts, which own input through
// their own gates in on_key and the pointer handlers. Since the flag editor
// became keyboard-modal this is NO LONGER the keyboard gate's predicate (that
// is keyboard_modal_editor_active); what it names is the POINTER-facing
// behaviors the top-strip FlagPayload editor is deliberately transparent to —
// the caller roster (re-derived by grep at the reach-through's retirement,
// 2026-08-13 — ten calling functions plus the dialog painter's fork by proxy)
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
// editors (the settings prompt, the load prompt and the commit-title editor)
// plus the top-strip flag editor in EITHER kind (the FlagPayload editor takes
// typed letters too). The platform layer's kLeftClickKey probe: while this is
// true that key types a normal letter rather than emulating the left button.
bool GuiInputHandler::any_text_editor_active() const {
    return text_editor::is_active(app.settings_editor) ||
           text_editor::is_active(app.load_editor) ||
           text_editor::is_active(app.commit_title_editor) ||
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
    // Global dispatch: only the continuous step gestures repeat — the bare
    // ARROWS all four (Left/Right being the playhead step in the waveform lane
    // and the position nudge in the marker lane, Up/Down the
    // tempo cent step; the lane split is decided per fire at dispatch, so the
    // arrows repeat as one family), bare PageUp/PageDown, bare Equal/Minus zoom,
    // THE WALK'S BARE COMMA/PERIOD (2026-08-07 — the `h` history view's
    // older/newer step, a continuous step gesture like the arrows and held for
    // the same reason, to walk quickly; it is bound only inside that view, so a
    // repeat outside it fires into an unbound key exactly as a held arrow with
    // nothing to nudge fires into a refusal. Their SHIFT shapes — the walk's
    // absolute wall jumps — are excluded by the no-shift term below and stay
    // one-shot: a held jump could only flap against the wall it just reached),
    // the marker-focus cycle (bare Tab / Shift+Tab / IsoLeftTab), and the THREE
    // repeating Ctrl chords — the Ctrl+Shift+Tab march plus Ctrl+Z / Ctrl+Shift+Z
    // (undo / redo), each a continuous step gesture like the cycle, not a
    // one-shot command. The march is the one of the three that is MODE-SCOPED
    // (2026-08-07): inside the `h` history view it is not the march at all —
    // the mode's reverse walk cycle from 2026-08-07 to 2026-08-18, and a
    // consumed no-op at the allowlist since — so there is nothing in there for
    // a hold to continue. Ctrl+Tab stays one-shot everywhere, in the view as
    // out of it, a held A/B switch being able only to flap. Every
    // letter, toggle, opener, other Ctrl / Ctrl+Alt chord, Space, Home/End,
    // and Delete is one-shot. No MODIFIED arrow repeats at all: the arrows carry
    // no modified binding to repeat.
    if (!mods.ctrl && !mods.shift && !mods.alt &&
        (key == GuiKeys::Left || key == GuiKeys::Right ||
         key == GuiKeys::Up || key == GuiKeys::Down ||
         key == GuiKeys::PageUp || key == GuiKeys::PageDown ||
         key == GuiKeys::Equal || key == GuiKeys::Minus ||
         key == GuiKeys::Comma || key == GuiKeys::Period))
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
    // Ctrl+Shift+Tab exactly (the lockstep marker march) repeats too — OUTSIDE
    // THE `h` HISTORY VIEW ONLY (2026-08-07). The mode-scoped term stands and
    // its reason has only got plainer: the chord WAS the mode's reverse walk
    // cycle in there and is a consumed no-op at the allowlist since 2026-08-18,
    // so a hold has nothing to continue either way. Ctrl+Tab needs no term of
    // its own — the no-ctrl arms above exclude it in every state — and
    // ctrl+shift+IsoLeftTab is excluded by those same arms' no-ctrl term. The
    // march outside the view is untouched by this line.
    if (mods.ctrl && mods.shift && !mods.alt && key == GuiKeys::Tab &&
        !app.history_mode.active)
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
// dropped. It serves ALL FIVE editors — the settings and load prompts,
// the commit-title editor (2026-08-07), the bpm bracket, and (architect
// 2026-07-28) the top-strip flag editor, which
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
// top: bare Tab under any DIALOG editor (the focus ring's step and, where the
// editor has one, its autocomplete — the fork is
// route_modal_editor_key's, not this gate's), Ctrl+S (save), and
// Ctrl+Q (close routing). Admitted keys flow into the editor routing unchanged,
// so the only NotConsumed keys that can reach route_modal_editor_key's command
// tail are those last two chords.
// THE ADMITTED SET HAS GROWN TWICE since the dialog arc, both times by the TAB
// FAMILY AND NOTHING ELSE: bare Tab, from "the settings and load editors only"
// to "any dialog editor", when the focus ring landed 2026-08-13; and THE
// RING'S REVERSE WALK the same day (architect: "also, shift+tab should cycle
// backward"), which admits Shift+Tab and the IsoLeftTab keysym in the live
// marker cycle's own spellings — one predicate, modal_ring_tab_shape above,
// read here and by the ring alike. (WHAT A TAB DOES inside the set was re-ruled
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
    // (2026-08-13) — a superset of what this gate admitted before: the LOAD
    // editor's entry-name autocomplete keeps the FORWARD key as its first
    // meaning, as the SETTINGS editor does, while the commit-title and BPM
    // editors let it walk the ring from the first press (route_modal_editor_key
    // owns which of the two a given forward Tab is, under the one autocomplete
    // model), and the REVERSE shapes walk backwards for all four, completing
    // nothing anywhere. The top-strip FLAG
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
    // under renders/, the same refusals — and there is no second chord for it.
    // The single render below is the mode-off meaning, unchanged. THE
    // SWEEP DISPATCHES FROM EITHER AUDIO VIEW since 2026-08-07 (the mode is
    // TARGET-LEGAL): the bit alone selects the command, and target view needs
    // no clause of its own for the opposite reason it needed none before — the
    // mode can now REST in target, and the arm below fires there. This is also
    // what keeps the Render button honest, its "Render Iterations" face
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
    // WHILE THE HISTORY MODE STANDS THIS CHORD NEVER ARRIVES, and since
    // 2026-08-08 neither does its unshifted twin — the mode's keyboard allowlist
    // admits NEITHER render chord, the checkpoint act having moved onto Ctrl+S —
    // so the press is consumed a gate above and this arm is not reached from
    // either surface. The Render button is simply dead in there, hint and all,
    // which is the derived partition's own answer.
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
    // in the BPM dialog editor after a successful commit; there is no
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

    // THE `h` HISTORY MODE ENDS HERE, on the first line past the last refusal
    // and before the first store write. It is the one route in the product that
    // replaces the authored state the mode's frozen now side was measured
    // against, so leaving the mode standing would leave every flag in the lane
    // describing a session that no longer exists. Placed at the MUTATOR rather
    // than at the `'` key because this function is what performs the replacement,
    // and the close belongs with the act rather than with one of its callers.
    // IN PRACTICE IT IS AN IDEMPOTENT NO-OP: the mode ADMITS bare `'`, but in the
    // view that editor's Enter routes to one of the mode's own two loads, so no
    // renders-side load ever runs with a visit standing (the closer inventory at
    // close_history_mode states it). The line stays for the same reason the
    // close is at the mutator at all.
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
    apply_settings_engine_and_prefs(app, viewport, *settings);

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
    viewport.invalidate_status_chain_area();
    viewport.invalidate_clock_area();

    // The tail's trigger owns the rebind for a 'T' landing: it marks the
    // buffer stale and dispatches the loaded-in-place state's target preview,
    // which rebinds
    // playback on completion.
    target_render.trigger();

    // Wipe renders/ AFTER the successful load-in-place. The loaded render survives
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
    // 2026-08-08), three wordings off one verdict: TRASHED says "moved renders/
    // to the trash", because that batch is RESTORABLE and "wiped" would overstate
    // it — the whole point of the trash-first rule; WIPED says "wiped renders/"
    // for the native fallback's own delete, which is not restorable; and NONE
    // drops the clause entirely on the two failing shapes (the failed query and
    // the fallback delete's error), since the load succeeded either way but the
    // disposal did not happen and each shape has already printed its own line.
    // THE ABSENT DIRECTORY KEEPS THE "WIPED" WORDING, unchanged from before the
    // split: the clause is a claim about the END STATE the act guarantees — there
    // is no renders/ on disk and nothing of it left to restore — which is exactly
    // true with nothing there, and the split is about restorability, the one axis
    // an absence has no side of.
    enum class WipeVerdict { Trashed, Wiped, None };
    WipeVerdict verdict = WipeVerdict::Wiped;  // The absent case; see above.
    if (std::filesystem::is_directory(renders_root, ec)) {
        if (trash_directory(renders_root)) {
            verdict = WipeVerdict::Trashed;
        } else {
            std::fprintf(stderr,
                "warptempo_gui: load-in-place: Trash unavailable for '%s'; "
                "deleting it instead\n",
                renders_root.string().c_str());
            std::filesystem::remove_all(renders_root, ec);
            if (ec) {
                std::fprintf(stderr,
                    "warptempo_gui: load-in-place: Wipe failed for '%s': %s\n",
                    renders_root.string().c_str(), ec.message().c_str());
                verdict = WipeVerdict::None;
            }
        }
    } else if (ec) {
        std::fprintf(stderr,
            "warptempo_gui: load-in-place: Could not check '%s': %s\n",
            renders_root.string().c_str(), ec.message().c_str());
        verdict = WipeVerdict::None;
    }

    const char* disposal =
        (verdict == WipeVerdict::Trashed) ? " and moved renders/ to the trash"
      : (verdict == WipeVerdict::Wiped)   ? " and wiped renders/"
                                          : "";
    std::fprintf(stderr,
        "warptempo_gui: load-in-place: Loaded render in place%s\n", disposal);
    gui.invalidate_region(0, 0, app.width, app.height);
    return true;
}

// -- Load-in-place from a COMMIT (the `'` editor in the `h` history mode) --
//
// WHAT IT IS: the same act load_render_entry_in_place performs, with the committed
// history as its source instead of a render entry. `spelling` is whatever the
// user left in the load editor — the viewed commit's SHA the opener prefilled,
// or any other spelling git can resolve (a short SHA pasted out of GitHub's web
// UI is the ruled use case). ONE STATE IN, ONE STATE OUT: the three sidecars
// THAT commit carried become the live session, in memory, and the disk is never
// touched — not the corpus, not the working sidecars, not renders/.
//
// WHAT GATES, all of it BEFORE any store is touched — the validate-before-mutate
// contract load_render_entry_in_place states and this path mirrors: ONE call,
// load_commit_sidecars_strict (history_diff.h), which is the resolution, the
// missing-sidecar refusals, the scratch staging and the three STRICT
// WHOLE-FILE LOADERS in one predicate — the same predicate that is WALK
// MEMBERSHIP since 2026-08-04, so the prefilled spelling (a walk member's own
// SHA) always passes and a refusal can only come from a PASTED spelling naming
// a commit outside the walk: an unresolvable spelling, a partial checkpoint,
// an ambiguous per-commit path resolution, or a sidecar the loaders refuse (a
// commit from the legacy MM:SS.mmm era, a settings key this build no longer
// knows). That strictness is the point rather than a side effect — exactly the
// parse-gating the architect ruled, and the reason no second, looser grammar
// is written anywhere on this path. A refusal is one stderr line naming its
// cause with the committed path and the SHA (first error only — the gate's own
// contract), and the caller keeps the editor open with its red flash.
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
// projects home installs that answer too, and the next `h` reads it.
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
    // THE CLONE IS THE SESSION'S OWN, derived from the loaded source at init
    // (history_diff.h): the `'` act reads the same repository the lane was
    // built from, on the same one derivation.
    const std::string repo_root = app.history_mode.session.repo_root();

    // THE WHOLE VALIDATION IS THE ONE SHARED GATE, and the spelling is all it
    // takes now: the session's matched directory was a parameter here until
    // 2026-08-09, when the folder a commit is about became the folder that
    // COMMIT TOUCHED rather than a tie the session could break. So a commit
    // touching this base name in two directories refuses instead of resolving to
    // whichever one this session happens to sit in, and one touching it nowhere
    // — an ordinary non-piece commit, a merge, a read that did not answer —
    // refuses too. Nothing is settled by a guess any more, which is the point.
    GuiHistoryCommitLoad loaded;
    std::string          reason;
    if (!load_commit_sidecars_strict(repo_root, spelling, base_name, loaded,
                                     reason)) {
        std::fprintf(stderr, "warptempo_gui: Load in place refused: %s\n",
                     reason.c_str());
        return false;
    }
    const std::string sha = loaded.sidecars.sha;
    const SettingsFile& settings = loaded.settings;
    std::vector<GuiWarpMarker>       src_warp = std::move(loaded.warp_markers);
    std::vector<GuiPhaseResetMarker> src_phase_resets =
        std::move(loaded.phase_reset_markers);

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

    const char load_tab = settings.active_tab_view;

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
    app.tab_a = view_state_from_settings_tab(settings.tab_a);
    app.tab_b = view_state_from_settings_tab(settings.tab_b);
    apply_settings_engine_and_prefs(app, viewport, settings);

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
    viewport.invalidate_status_chain_area();
    viewport.invalidate_clock_area();

    // The tail's trigger owns the rebind for a 'T' landing.
    target_render.trigger();

    // NO renders/ WIPE. That step is the render-entry load-in-place's cleanup
    // of the folder it
    // consumed an entry from; this path consumed a commit and renders/ is none of
    // its business.
    std::fprintf(stderr,
        "warptempo_gui: load-in-place: Loaded the sidecar state of commit "
        "%s in place\n",
        sha.c_str());
    gui.invalidate_region(0, 0, app.width, app.height);
    return true;
}

// -- Load-in-place from a LOCAL HISTORY MEMBER (the `'` editor on a Local tab) -
//
// WHAT IT IS: the third of the load-in-place family (architect 2026-08-08,
// superseding his own "the Local walk consumes `'`"), with A STATE OF THIS
// SESSION'S OWN UNDO/REDO TIMELINE as its source. `text` is whatever the user
// left in the load editor: the viewed member's displayed NUMBER, which the
// opener prefilled, or any other member number he typed over it — the corner's
// own `n/N` vocabulary, which is the only name a local member has.
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
// validate-before-mutate contract: the text must be ASCII DIGITS naming a member
// in [1, N], and the walk must hand back that member's state. Anything else is
// ONE stderr line and a false return, the caller keeping the editor open with
// its red flash and the typed text in it to correct. There is no second grammar
// and nothing to resolve: N is the walk's own member count, and the number is an
// index into it.
//
// WHAT IS APPLIED, AND WHAT DELIBERATELY IS NOT: an undo entry carries the two
// MARKER COLUMNS and the ENGINE BLOCK and nothing else (the carry-everywhere
// shape at UndoEntry), so that is exactly what this restores — the same three
// pieces the walk's delta vocabulary is built from. NO tab bands, NO
// playback_speed, NO gui_scale, NO trim, NO read_only, NO session prefs: the
// sibling loads those because a SIDECAR SET carries them, and a timeline state
// simply does not. The engine block is applied the way a restore applies one
// (restore_history_entry) — the values into app.engine_settings, with the
// synchronous plate rebuild and the target-preview trigger in the tail covering
// the map it changes.
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
bool GuiInputHandler::load_history_local_entry_in_place(
        const std::string& text) {
    // The mode is the route's precondition — the walk lives on it, and the close
    // below is part of the act. The source test is the routing's own fact
    // restated defensively: load_editor_commit sends only Local-tab pendings
    // here.
    if (!app.history_mode.active) return false;
    if (app.history_mode.source != GuiHistoryWalkSource::Local) return false;

    // THE WHOLE VALIDATION. ASCII digits only — the number is a count position,
    // not text — parsed with the range check folded INTO the accumulation, which
    // is what keeps a pasted forty-digit string from overflowing on its way to
    // being refused (every further digit only grows the value, so the first one
    // past the count settles it).
    const std::size_t count = app.history_mode.local.entry_count();
    std::size_t       number = 0;
    bool              parsed = !text.empty();
    for (const char c : text) {
        if (c < '0' || c > '9') { parsed = false; break; }
        number = number * 10 + static_cast<std::size_t>(c - '0');
        if (number > count) { parsed = false; break; }
    }
    if (parsed && number == 0) parsed = false;

    // THE WALK'S OWN ANSWER IS THE SECOND HALF OF THE GATE. It is empty only for
    // an UNBOUND walk or a stack shorter than its capture (the blank-lane state,
    // which a live Local tab cannot reach — the mode's entry binds the walk and
    // the allowlist refuses the chord on an empty one), so this is the
    // unreachable arm stated rather than assumed, refusing in the same shape a
    // bad number does.
    std::optional<GuiHistoryLocalWalk::MemberState> state;
    if (parsed) state = app.history_mode.local.member_state(number - 1);
    if (!state) {
        std::fprintf(stderr,
            "warptempo_gui: Load in place refused: '%s' is not a history entry "
            "number (1..%zu)\n", text.c_str(), count);
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
    // mutator's own self-guard, exactly as both siblings spell it. The `'`
    // editor's open already froze playback through that owner on the keyboard
    // route; stopping again here keeps the mutator correct from any caller.
    playback_lifecycle.stop_playback_if_playing();

    // THE MODE ENDS HERE, on the first line past the last refusal and before the
    // first store write — the family's placement. It also drops the walk the
    // state was read out of, which is why the copies are above it, AND it is
    // what keeps this act inside the walk's frozen-timeline premise: the push
    // below happens with no visit standing, so the entry it adds is not one any
    // walk had captured.
    close_history_mode();

    std::vector<GuiWarpMarker>       warp_pre = app.warpmarkers.markers();
    std::vector<GuiPhaseResetMarker> phase_reset_pre =
        app.phaseresetmarkers.markers();

    app.warpmarkers.markers_mut()       = std::move(src_warp);
    app.phaseresetmarkers.markers_mut() = std::move(src_phase_resets);
    // Wholesale authoring reset: the ONE selection goes, and there is nothing
    // else to reset — no per-tab per-mode slot holds a copy.
    selection.clear_selection();

    // ONE cross-file undo entry: the marker pair plus the OUTGOING engine
    // settings, which push_undo_both captures from `app` — so this must run
    // BEFORE the incoming block is applied below. NO TAB OVERRIDE: the load
    // lands on the ACTIVE tab, a timeline state carrying no tab band of its own.
    undo.push_undo_both(std::move(warp_pre), std::move(phase_reset_pre),
                        app.active_markers_view, 0);
    undo.recompute_dirty();

    // Wholesale authoring reset: clear every marker's session-only iteration
    // state and the bpm state, and turn off both sweep modes' visibility —
    // exactly as loading a commit would, the two acts replacing the authored
    // state the same way.
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

    // The engine block, applied as a restore applies one (restore_history_entry)
    // — the values, and nothing beside them: the map rebuild and the target
    // preview are the tail's, below, exactly as they are a restore's.
    app.engine_settings = std::move(src_engine);

    clamp_viewport_start(app, audio);
    // COINCIDENCE AUTO-SELECT at the load-in-place chokepoint (rule and inventory
    // at auto_select_marker_at_playhead), at the tail for the siblings' reason:
    // everything the scan reads has landed.
    auto_select_marker_at_playhead(app, audio, selection, viewport);
    viewport.kick_waveform_sync();
    viewport.invalidate_waveform_area();
    viewport.invalidate_status_chain_area();
    // HARMLESS OVER-DAMAGE, not a cursor land: a timeline state carries no
    // band, so this tail never writes the live playhead (the auto-select above
    // fires only where the land is a provable no-op) — the clock inventory
    // (viewport.h) records the class beside the zoom appliers'.
    viewport.invalidate_clock_area();

    // The tail's trigger owns the rebind for a 'T' landing.
    target_render.trigger();

    // NO renders/ WIPE and NO DISK WRITE of any kind: this act moved state that
    // was already in memory from one place in memory to another.
    std::fprintf(stderr,
        "warptempo_gui: load-in-place: Loaded local history entry %zu of %zu "
        "in place\n",
        number, count);
    gui.invalidate_region(0, 0, app.width, app.height);
    return true;
}

// Open the `'` load prompt. No-op with no source loaded. An empty
// renders/ reports a one-line bottom-strip status and does not open. Stops
// playback only when the modal actually opens (after every guard), so a
// refused open leaves a listening session running.
//
// THE `h` HISTORY MODE CHANGES WHAT THIS EDITOR IS FOR, and the whole change is
// this one branch plus the routing at load_editor_commit: in the mode the
// editor takes THE VIEWED WALK'S OWN VOCABULARY and opens PREFILLED with the
// viewed member — a COMMIT SPELLING seeded with the full 40-char SHA on the
// Remote tab (load_history_commit_in_place), a MEMBER NUMBER seeded with the
// corner's own displayed `n` on the Local tab since 2026-08-08
// (load_history_local_entry_in_place). Both of the renders-side guards
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
        // THE SEED IS THE VIEWED MEMBER, IN THE ACTIVE WALK'S OWN SPELLING
        // (2026-08-08, when the Local walk got the act): the corner's displayed
        // NUMBER on the local timeline, the commit's full 40-char SHA on the
        // committed one. The fork is here rather than in an accessor for the
        // reason the corner's own SHA token is spelled out: the two walks name
        // their members in different vocabularies, and the editor asks for the
        // one the user is looking at.
        //
        // NEITHER ARM CAN BE COLD, and since 2026-08-09 that is the ALLOWLIST's
        // doing rather than the walk's: the mode opens on an EMPTY commit walk
        // now, so the honest gate against a walk with no viewed member is the
        // `'` admission's own term (history_mode_key_blocked — the chord is
        // refused and the icon row's load button greys), and this opener is
        // reached only with a member to name. Within a bound walk every step
        // clamps, so the index is always in range.
        if (app.history_mode.source == GuiHistoryWalkSource::Local) {
            prefill = std::to_string(app.history_mode.local_index + 1);
        } else {
            prefill = app.history_mode.session.sha_at(app.history_mode.index);
        }
    } else {
        // Running-render guard: the load-in-place wipes renders/, which would race a
        // background sweep writing into it. Refuse, don't cancel — a running
        // batch may be irreplaceable queued work; Esc is the explicit cancel.
        if (app.queue_running || app.pending_archival.armed) {
            app.transient_status_message = "Render running; Esc cancels it";
            viewport.invalidate_status_chain_area();
            return;
        }
        std::vector<AppState::RenderEntry> list =
            renders_dir.enumerate_render_entries();
        if (list.empty()) {
            app.transient_status_message = "No renders to load in place";
            viewport.invalidate_status_chain_area();
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
    // A modal-dialog OPEN damages the whole window (the box's rect does not
    // exist before its first paint — the settings opener carries the rule).
    viewport.invalidate_all();
}

void GuiInputHandler::load_editor_exit_no_commit() {
    if (!text_editor::is_active(app.load_editor)) return;
    viewport.invalidate_modal_dialog_area();
    text_editor::deactivate(app.load_editor);
}

// Tab handler: extend the pending to the longest common prefix of the entry
// identifiers that start with it. No-op when nothing matches or when the
// common prefix does not advance past what is already typed (mirrors the
// settings editor's no-op-on-ambiguity Tab). A unique matching candidate
// completes fully — its whole string is the common prefix of the singleton.
//
// IT RETURNS WHETHER THE BUFFER ADVANCED, the one autocomplete model's whole
// question (the rule is stated at route_modal_editor_key's Tab arm below): true
// consumes the Tab, false lets it walk the modal's focus ring. Every early
// return below is a false — an inactive editor, the `h` mode's foreign
// vocabulary, no candidate carrying this prefix, a common prefix that does not
// advance, and a UTF-8 back-off that leaves nothing to add. THAT IS WHAT MAKES
// A SECOND TAB WALK with no state to remember the first: the buffer is already
// AT the longest common prefix, so the advancement test below refuses it. And
// it is what gives the `h` HISTORY MODE a working ring here — a commit spelling
// or a member number has nothing to complete against, so every Tab in there
// answers false and steps, a case the pre-ruling model could not reach.
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
bool GuiInputHandler::load_editor_autocomplete() {
    if (!text_editor::is_active(app.load_editor)) return false;
    // IN THE `h` HISTORY MODE THERE IS NOTHING HERE TO COMPLETE AGAINST: the
    // pending is a commit spelling or a member number, and the entry
    // identifiers below name renders this route never reads. Answering false
    // rather than completing out of the wrong vocabulary is what hands the key
    // to the ring.
    if (app.history_mode.active) return false;
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
    if (!have) return false;                    // no candidate has this prefix
    // Back the prefix off to a codepoint boundary (see the note above). Runs
    // BEFORE the advancement test, so a prefix that reached only into the
    // middle of a character correctly collapses to a no-op.
    while (lcp.size() < first.size() &&
           text_editor::is_utf8_continuation_byte(
               static_cast<unsigned char>(first[lcp.size()])))
        lcp.pop_back();
    if (lcp.size() <= pending.size()) return false;  // prefix does not advance

    app.load_editor.pending          = std::move(lcp);
    app.load_editor.cursor_pos       =
        static_cast<int>(app.load_editor.pending.size());
    app.load_editor.selection_anchor = -1;
    app.load_editor.red              = false;
    viewport.invalidate_modal_dialog_area();
    return true;
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
// IN THE `h` HISTORY MODE THE SUBJECT IS A HISTORY MEMBER, not a render entry,
// and WHICH KIND is the viewed walk's: a COMMIT SPELLING on the Remote tab
// (load_history_commit_in_place), a MEMBER NUMBER on the Local tab since
// 2026-08-08 (load_history_local_entry_in_place). Each owns every refusal on its
// own route and names each one on stderr. All three routes share this function's
// SHAPE exactly — a true result closes the editor, a false one red-flashes and
// stays open — so a failed resolve leaves the typed text in place to be
// corrected.
void GuiInputHandler::load_editor_commit() {
    if (!text_editor::is_active(app.load_editor)) return;
    const std::string pending = app.load_editor.pending;

    auto reject = [&]() {
        app.load_editor.red = true;
        viewport.invalidate_modal_dialog_area();
    };

    if (app.history_mode.active) {
        const bool loaded =
            app.history_mode.source == GuiHistoryWalkSource::Local
                ? load_history_local_entry_in_place(pending)
                : load_history_commit_in_place(pending);
        if (loaded) {
            viewport.invalidate_modal_dialog_area();
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
        viewport.invalidate_modal_dialog_area();
        text_editor::deactivate(app.load_editor);
    } else {
        reject();
    }
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
//            Escape sentinel at every raise (PromptState owns the supersession
//            of "this prompt system has no Enter answer" and the two facts that
//            make it safe).
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
    const ModalRingTab tab_shape = modal_ring_tab_shape(key, mods);
    if (tab_shape == ModalRingTab::None &&
        (mods.ctrl || mods.shift || mods.alt)) {
        return false;
    }
    const AppState::ModalDialogGeometry& dlg = app.modal_dialog;
    if (!dlg.valid || dlg.buttons.empty()) return false;
    if (!modal_dialog_stash_current()) return false;
    const bool prompt_up = app.prompt.active;
    const int n = static_cast<int>(dlg.buttons.size());
    const int at = (app.modal_dialog_focus >= 0 &&
                    app.modal_dialog_focus < n) ? app.modal_dialog_focus : -1;

    int next = at;
    if (tab_shape == ModalRingTab::Forward) {
        // The field is a stop only on an editor dialog, which is the one
        // place -1 is a place to come back to.
        if (at < 0)            next = 0;
        else if (at + 1 < n)   next = at + 1;
        else                   next = prompt_up ? 0 : -1;
    } else if (tab_shape == ModalRingTab::Reverse) {
        // The exact inverse of the forward arm — the same stops in the same
        // order, walked the other way, with the same wrap: on an editor
        // field -> last button -> ... -> first button -> field, on a prompt
        // the buttons alone, wrapping. (The prompt's `at < 0` arm below is a
        // cold answer only: a standing prompt always has a focused button,
        // assigned at its raise.)
        if (at < 0)            next = n - 1;
        else if (at > 0)       next = at - 1;
        else                   next = prompt_up ? n - 1 : -1;
    } else if (key == GuiKeys::Right) {
        if (at < 0 && !prompt_up) return false;   // the field's own arrows
        next = (at < 0) ? 0 : (at + 1) % n;
    } else if (key == GuiKeys::Left) {
        if (at < 0 && !prompt_up) return false;   // the field's own arrows
        next = (at < 0) ? n - 1 : (at + n - 1) % n;
    } else if (key == GuiKeys::Return || key == GuiKeys::Space) {
        // THE BUTTON'S KEYBOARD PRESS. With no button focused this is not the
        // ring's key at all: an editor's field keeps Enter's commit and Space's
        // typed character, and both fall through untouched.
        if (at < 0) return false;
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
    const int armed = app.modal_dialog_key_pressed;
    if (armed < 0 || app.modal_dialog_key_pressed_key != key) return;
    app.modal_dialog_key_pressed     = -1;
    app.modal_dialog_key_pressed_key = 0;
    if (app.modal_dialog.valid)
        viewport.invalidate_rect(app.modal_dialog.box);
    dispatch_modal_dialog_button(armed);
}

// Shared key route for EVERY keyboard-modal editor — the settings prompt, the
// load prompt, the commit-title editor, the bpm bracket editor, and the
// top-strip flag editor.
// All five spell ONE modal contract: the on_key gate (modal_editor_key_blocked)
// admits only the editor's own keys plus bare Esc, Ctrl+S, and Ctrl+Q, so a
// NotConsumed key here is one of the latter two chords. Ctrl+S saves with
// the editor left open (save is not an exit); Ctrl+Q runs the caller's
// teardown and returns false so on_key runs the close routing; anything
// else is swallowed as a backstop. THE COMMAND ADMISSION IS KEYBOARD-ONLY
// AGAIN (2026-08-13): it had a pointer-side mirror from 2026-08-11 — the
// modal-trap reach-through, which let a roster button whose chord is admitted
// here dispatch from a press while a dialog editor stood — and the architect
// retired it once every dialog grew real OK and Cancel buttons, so nothing
// outside this file restates this set and the veil swallows every roster
// press. Ctrl+S here is unchanged. `autocomplete` is the optional
// bare-Tab hook, PASSED BY THE SETTINGS AND LOAD EDITORS (the commit-title,
// bpm and flag editors have no vocabulary to complete and pass an empty hook).
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
// no longer empty, the load editor's buffer is already at the longest common
// prefix), so it walks by that one test. THERE IS NO PRESS COUNTER, NO TIMER
// AND NO "last key was Tab" BIT anywhere in this model, deliberately — wanting
// one means the rule has been mis-derived.
//
// THE ORDERING, in full:
//   * Tab with the focus ON A BUTTON walks the ring — a button has no field to
//     complete, so the hook is not even offered (the test is
//     modal_dialog_focus < 0, and -1 IS the field on an editor dialog;
//     AppState::modal_dialog_focus owns that meaning).
//   * Tab with the focus IN THE FIELD offers the completion, then walks.
//   * Tab in a dialog editor with NO hook (commit-title, BPM) walks at once.
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
        const std::function<void()>& ctrl_q_teardown,
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
    // runs the teardown and hands the close routing on. They are spelled here
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
            ctrl_q_teardown();
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

// Routes a key to the active load editor through the shared modal route; bare
// Tab in the field offers the entry-identifier completion and walks the focus
// ring when it did not advance (the one autocomplete model is stated at
// route_modal_editor_key).
bool GuiInputHandler::handle_load_editor_key(GuiKey key,
                                               GuiInputState mods) {
    return route_modal_editor_key(
        app.load_editor, key, mods,
        [this] { return load_editor_autocomplete(); },
        [this] { load_editor_commit(); },
        [this] { load_editor_exit_no_commit(); },
        [this] { load_editor_exit_no_commit(); },
        [this] { viewport.invalidate_modal_dialog_area(); });
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
            // THE GATE IS THE WARP COLUMN ALONE — both audio views (architect
            // 2026-08-07, iteration mode is TARGET-LEGAL; the deleted S->T
            // wipe's record is in handle_active_audio_view_toggle,
            // input_handler.cpp). It read active_column_authoring_allowed
            // until then, which pinned the toggle to warp's SOURCE home. The
            // relaxation is about MODE STATE rather than authoring, which is
            // why it is not a home-view-binding exception: the bit selects
            // what Ctrl+Alt+R means and what the flags show, while bracket
            // AUTHORING stays source-only at the flag editor's own gate. The
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
    // There is no toggle-off branch: the bpm editor is a modal dialog
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
    // above this dispatch. It stops no playback and clears no region: turning
    // the mode on IS NOT a selection act — the click that follows is, and that
    // click runs the marker act's own stop and region clear.
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
            viewport.invalidate_status_chain_area();
            return true;
        }
        std::vector<AppState::RenderEntry> list =
            renders_dir.enumerate_render_entries();
        if (list.empty()) {
            app.transient_status_message = "No renders to play";
            viewport.invalidate_status_chain_area();
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
        // The center command, whose recipe and whose history-mode twin both live
        // at its owner (run_center_command). This arm is unreachable while the
        // mode stands — handle_history_mode_key claims `c` above this dispatch —
        // so the owner's fork decides nothing for it; it is the third caller,
        // `0`'s already-full-out arm, that the fork exists for.
        run_center_command();
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
        //
        // THE LANDING FRAME COMES FROM THE SHARED OWNER since 2026-08-15
        // (playhead_skip_landing_frame, viewport.cpp — Viewport::trim_range's
        // begin, pre-clamped), which the three arms that jump share so the
        // bound is spelled once. The clamp is idempotent on what
        // move_playhead_to would clamp anyway, so nothing about this jump
        // changed. THE THREE STEPS ABOVE RUN ON A NO-OP JUMP TOO and are
        // deliberately not gated on it: the audition stop, the lane exit's
        // selection clear and the region dissolve are unconditional, so this
        // key ACTS even when the cursor already rests on the landing frame.
        // THAT IS WHY THE BOTTOM ROW'S SKIP BUTTONS DO NOT GREY THERE (architect
        // 2026-08-15, taking back the face that did): a grey would promise less
        // than this arm delivers, and gating the three steps to make the grey
        // honest would change behaviour. The full record is at the skips' case
        // in redesign_button_enabled.
        playback_lifecycle.stop_playback_if_playing();
        if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
            selection.clear_selection();
            viewport.invalidate_waveform_area();
        }
        // Navigation jump to the trim-begin bound: dissolve a
        // resting region (its span is stale now the playhead jumps).
        clear_region_highlight(app, viewport);
        viewport.move_playhead_to(playhead_skip_landing_frame(app, audio,
                                                              false));
        break;
    case GuiKeys::End:
        playback_lifecycle.stop_playback_if_playing();
        if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
            selection.clear_selection();
            viewport.invalidate_waveform_area();
        }
        clear_region_highlight(app, viewport);
        // The owner's `forward` arm, which is trim_range's END minus one — the
        // last frame INSIDE the window (see the Home arm above).
        viewport.move_playhead_to(playhead_skip_landing_frame(app, audio,
                                                              true));
        break;
    default: break;
    }
}

// Top-flag editor key routing. See the declaration for the consumed/command
// contract. BOTH kinds take the shared modal route (architect 2026-07-28) and
// differ only in their commit / cancel bodies and their repaint area: the bpm
// bracket editor draws in the MODAL DIALOG on the bottom row (like the settings
// editor; its damage is that row's own lane owner) and
// commits into a render sweep, the FlagPayload editor draws in the TOP strip
// and commits the flag's own payload. Neither passes a bare-Tab hook, having no
// vocabulary to complete: in the BPM editor, a DIALOG, bare Tab walks the
// modal's focus ring from the first press, while for the top-strip FLAG editor
// it never reaches this route at all — the on_key gate swallows it, a flag
// editor publishing no dialog and so no ring (route_modal_editor_key).
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
                viewport.invalidate_modal_dialog_area();
            },
            [this] {
                // Ctrl+Q tears the editor and the mode down together
                // (mode-without-editor stays unreachable).
                flag_editor.exit_top_flag_edit_no_commit();
                flag_editor.exit_bpm_mode();
            },
            [this] { viewport.invalidate_modal_dialog_area(); });
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
        [this] { settings_editor.exit_no_commit(); },
        [this] { viewport.invalidate_modal_dialog_area(); });
}
