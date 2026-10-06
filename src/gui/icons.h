#pragma once

// THE IN-TREE BREEZE ICON RENDERER — the redesign's icon path, and the reason
// no SVG library enters this tree.
//
// The kdenlive rows draw Breeze icons, and a Breeze icon is a handful of paths
// in a square viewBox: no gradients, no references. That is small enough to
// interpret directly, and interpreting it keeps the icons as SOURCE (a `d`
// string beside its color) rather than as pixels baked at one scale — so an
// icon is crisp at every gui_scale, exactly like every other redesigned
// dimension.
//
// THE INTERPRETER HAS GROWN EXACTLY TWO FEATURES past the plain filled path
// it started as, each with a committed producer and each taken so that the `d`
// string in the table stays VERBATIM rather than being flattened by hand:
//   - a per-path TRANSLATE (dialog-ok-apply's and dialog-cancel's transform);
//   - the SMOOTH CUBIC `s` (document-revert's arrow lobes, 2026-08-05).
// Each is described where it is implemented (icons.cpp's table header and its
// `d`-interpreter header). THE STROKED PATH, with its per-path width and dash,
// was a third and left on 2026-09-22 with tool-rect-selection, its last
// producer (the Show trim region button's glyph, deleted with the button); it
// had lived for distortionfx and for boost before that, and a general per-path
// MATRIX grown beside it for distortionfx is git history too.
//
// TWO FILES DEPART FROM THE VERBATIM RULE and they are stated here as well
// as at their table entries, because the rule is what this header promises:
// since 2026-08-29 the two dialog glyphs' PLATES (dialog-information,
// dialog-error) are `<rect rx="2">` elements under a verbatim glyph path, so
// there is no `d` in the file to copy for the plate and each row spells the
// rounded rectangle SVG defines for that rx. A `<rect>` parser was declined
// for those files (and for tool-rect-selection before them) and stays
// declined.
//
// PROVENANCE: the SVGs the tables were transcribed from are committed under
// assets/icons/breeze/. They are the record of what this code draws; the
// d-strings here are copied from them VERBATIM, so a diff between the two is a
// transcription bug and nothing else — with the two `<rect>` plates'
// derivations as the stated exception just above. They are read by no code at runtime — the
// product reads no icon files. The one committed SVG with no table row is
// audio-x-generic.svg (the 64 px mimetype rendition), which lends the launcher
// icon on both devices its sheet grey and its note's ink (white at .75). The
// launcher's glyph is music-note-16th's outer contour, which IS transcribed
// here for the roster (MusicNote16th): its d, truncated before the two
// subpaths that cut the head and the flag hollow, is a verbatim prefix of the
// file's in android/app/ic_launcher_foreground.svg, rendered once to the PNG
// layers under android/app/res/, and in packaging/warptempo_gui.svg, the
// .desktop's Icon=; the launchers read those files, and THE CAPTION'S ICON
// (AppIcon, 2026-10-05) is packaging/warptempo_gui.svg transcribed whole —
// its plate and its note, the note under the file's own transform.
//
// BREEZE IS THE RULED GLYPH SOURCE AND AN AUTHORING-TIME DEPENDENCY ONLY
// (architect 2026-08-08: the one theme addressing Qt and GTK both, with a
// commitment to stay so). A glyph is transcribed from the installed
// breeze-dark theme's actions/22 files — breeze-dark because it resolves
// `.ColorScheme-Text` to the #fcfcfc every row carries, the `d` geometry
// being the light theme's own — and consulted exactly then: building and
// running need no icon theme installed on any machine, the glyphs being
// compiled-in geometry, so there is no broken-icon state to degrade to. A
// glyph of our own authoring is ruled out in favour of Breeze's own file
// (2026-08-08, an authored three-bars glyph reverted the day it landed). The
// one runtime theme consultation in the product is the cursor set (below).
//
// EVERY ENTRY IS A ROW'S. The icons here are painted by the redesigned rows and
// nowhere else — the pointer cursor is not one of them: every cursor the product
// shows is a NAMED CURSOR FROM THE USER'S OWN XCURSOR THEME (architect
// 2026-08-03), so the platform ships no cursor art and draws none.
//
// GUI-ONLY, like text_shape: icons exist only where pixels do,
// and warptempo_cli must never carry this TU.

#include "render.h"        // GuiColor

#include <cairo/cairo.h>

#include <cstddef>
#include <cstdint>

namespace icons {

// The icon set, one entry per committed SVG.
enum class Icon {
    // The toolbar four — Save / Undo / Redo / Render. Transcribed for row 2
    // (the labeled toolbar row, 2026-07-31) and SURVIVING ITS DELETION whole
    // (2026-08-12, the grand relayout's roster commit): the four buttons moved
    // into the icon row as its first group, wearing these same glyphs at the
    // row's 22px box. MediaRecord serves BOTH the plain render and the
    // iteration sweep by the architect's same-day ruling ("the context makes
    // it clear" — the tooltip alone forks); the mid-render Cancel face is
    // DialogCancel below.
    DocumentSave,        // Save
    EditUndo,            // Undo
    EditRedo,            // Redo
    MediaRecord,         // Render
    // SAVE's OTHER face: while the history mode stands, Ctrl+S saves and then
    // commits the checkpoint, so the button wears the commit icon — and keeps
    // it while the checkpoint publishes (the swap's owner is
    // redesign_button_icon, its hint the tooltip override's "Save and Commit";
    // the publishing hint "Committing the checkpoint" went on 2026-09-12 with
    // the state-and-reason class, the grey saying it now; the mode is
    // AppState::HistoryMode). It
    // was RENDER's second face until 2026-08-08, when the act moved onto the
    // save chord; the WORDS moved off the button whole when row 2's labeled
    // faces died at the 2026-08-12 relayout — the glyph swap says it now.
    VcsCommit,           // Save, in the history view and while publishing
    // SAVE'S THIRD face (architect 2026-09-27): in the history view while the
    // GitHub status reads Behind, Ctrl+S PULLS, and the button wears Breeze's
    // actions/22/vcs-pull — the arrow down onto the bar (the swap's owner is
    // redesign_button_icon, its hint "Pull (Ctrl+S)").
    VcsPull,             // Save, in the history view while GitHub is ahead
    // Row 4, the icon row.
    //
    // THE VIEW GROUP'S THREE FACES (architect 2026-10-01): the icon row's
    // flush-right view group — Source+Warp, Target+Warp, Target+Phase, the
    // three absolute view selectors on bare 1 / 2 / 3 — wears the glyphs he
    // picked on 2026-08-11 for the view radios, RESTORED VERBATIM from git
    // (enumerators, table defs and assets together, the edit-cut precedent in
    // reverse) when the row-1 view bar's three labelled buttons were deleted
    // and their acts came down to the icon row. SOURCE is document-export, the
    // arrow LEAVING a document — his own metaphor, the source being where the
    // audio comes FROM; TARGET is document-import, the arrow ENTERING one;
    // PHASE RESET is chronometer-start, the stopwatch with the solid play
    // triangle in its dial — start the clock anew. A WARP view wears its
    // audio's glyph alone (warp is the column every audio view has; the one
    // phase-reset view is the one that needs its own face).
    // PROVENANCE OF THE PICKS (2026-08-11, off a rendered candidate sheet, for
    // the four S/T and W/P radios that then stood in this row): the radios
    // wore shaped LETTER GLYPHS from the row's first day until that pick —
    // the row's only non-icon buttons, and the reason the architect briefly
    // ruled them deleted altogether ("ugly letter blips") before reversing
    // that the same day. chronometer-start was picked over chronometer-reset
    // and view-refresh (indistinguishable from each other at row size, and
    // chronometer-reset's dial not surviving the rendering) and over the bare
    // chronometer. Warp's own glyph was speedometer, the gauge with the
    // needle, his first pick, reversed to distortionfx (the spiral, "time
    // bends") in the same breath and restored at his second look that
    // evening, leaving distortionfx and player-time the runners-up; it left
    // with the radios' collapse to lamps on 2026-09-04 and is not restored.
    // All picks and runners-up stay recorded here so none is re-proposed
    // without a new ruling.
    DocumentExport,      // Source+Warp (bare 1)
    DocumentImport,      // Target+Warp (bare 2)
    ChronometerStart,    // Target+Phase (bare 3)
    // (THE TRIM SCISSORS' EDIT-CUT IS DELETED with its button, 2026-08-18: the
    // architect retired the "set trim from region" BUTTON in the roster
    // relayout — and the ACT went with it later the same day, when the region
    // became the trim and its chord `x` was repointed onto the button below —
    // which left this glyph with
    // no consumer at all, so the enumerator, the table row and
    // assets/icons/breeze/edit-cut.svg went together rather than resting
    // unpainted. It served the trim button from 2026-08-11 and was the
    // architect's own pick from the rendered candidate sheet, over the first
    // cut's planner-picked transform-crop; both are git history now.)
    // (TOOL-RECT-SELECTION, the marching-ants rectangle, was the Show trim
    // region button's glyph from 2026-08-16 and left with that button on
    // 2026-09-22 — enumerator, def and asset together, and the interpreter's
    // stroked arm with it, the file having been that arm's last producer.)
    // THE ZOOM PAIR (architect-picked 2026-08-12, the grand relayout's
    // roster commit). Breeze's own magnifier family: the fit frame (full zoom
    // out — bare `0`'s whole-song arm, leading the icon row's viewport-class
    // group) and the 1:1 original (working-zoom center, bare `c`, on the
    // bottom row's walk group since 2026-09-29). The plus and minus magnifiers left with the Zoom In / Zoom
    // Out buttons on 2026-09-25 (architect: zoom is on every surface) —
    // enumerators, defs and assets together.
    ZoomFitBest,         // Full zoom out / overview (bare `0`)
    ZoomOriginal,        // Working-zoom center (bare `c`)
    // ZOOM-IN-Y, the vertical magnifier (the ruler on its dial beside a
    // plus): worn by the Waveform Magnification lamp between Full zoom out
    // and Follow since the lamp's reversal (architect 2026-09-24), in place of
    // its twin zoom-out-y.
    ZoomInY,             // Toggle Waveform Magnification (bare `)
    // THE SINGLE-MARKER VERBS' FOUR (architect-picked 2026-08-12, the same
    // sheets): list-add for the drop (bare `s`), Breeze's RED list-remove for
    // the delete (`Delete` — the resolved-color entry, like media-record's
    // red), view-hidden for the disable toggle (`Ctrl+D` — the crossed-out
    // eye), and insert-link for the inherit/collapse (`Ctrl+N` — a pass
    // marker LINKS its tempo to its neighbor). view-hidden is transcribed
    // VERBATIM, its degenerate artifact subpath included (the architect ruled
    // the artifact fine; the record is at the table entry).
    ListAdd,             // Drop marker (bare `s`)
    ListRemove,          // Delete markers (`Delete`)
    ViewHidden,          // Toggle disabled (`Ctrl+D`)
    InsertLink,          // Inherit tempo (`Ctrl+N`)
    // THE FLATTEN BUTTON'S GLYPH (architect 2026-09-19): Breeze's MERGE, "one
    // path, three offset boxes reading as two merging into one" — which is
    // what the act does to a tempo's deviation terms, several of them
    // collapsing to one or to none. His own pick, with "I might change it
    // later" attached to it.
    //
    // THE 22px FILE IS HIS OWN PICK AND THE SET'S SIZE
    // (/usr/share/icons/breeze/actions/22/merge.svg): every transcribed asset
    // in this set is 22 and every IconDef is {22.0, ...}, so the glyph is transcribed at
    // the size the rest of the roster already wears. NO SWAP IS PENDING.
    Merge,               // Flatten tempo deviations (`Ctrl+F`)
    // (EDITCOPY AND EDITPASTE ARE DELETED — 2026-08-20, with their buttons:
    // the architect's propagate relocation gave the propagate commands the
    // EDIT MENU as their one pointer home, and neither glyph had a second
    // consumer. Breeze's edit-copy and edit-paste, the two-sheets and the
    // clipboard, transcribed from the shipped SVGs; the provenance files went
    // with them, the trim scissors' own precedent. A menu ROW carries text and
    // an accelerator, never a glyph, so nothing replaced them.)
    // (MUSIC-NOTE-16TH AND MATHMODE SPENT 2026-08-27 TO 2026-09-04 OUT OF
    // THIS ENUM. The Series relocation gave the BPM opener and iteration mode
    // a menu row each and deleted their buttons, and a menu ROW carries text
    // and an accelerator, never a glyph, so both files left with their one
    // consumer — enumerators, defs and assets together, edit-copy and
    // edit-paste's own precedent seven days earlier. The architect deleted
    // that menu on 2026-09-04 and both buttons came back, so both glyphs did;
    // their entries are above, at the icon row's own order.)
    // THE SUMMATION SIGMA, ON THE CUMULATIVE READING SINCE 2026-08-18
    // (architect): a cumulative delta is a SUM over the walk's members, read
    // against the iterative reading's one step at a time — the Σ says exactly
    // that, which deep-history's swept clock only implied. It dressed
    // ITERATION MODE from 2026-08-01 until this move (an iteration sweep is
    // also a sum over cells), and that slot took mathmode, which wears it
    // still — no two buttons ever wore one math symbol, which is what the move
    // bought.
    BlackSum,            // The cumulative reading (`u`)
    // GO-JUMP, the chevron with its destination dot (2026-08-01), dressing the
    // FOLLOW lamp — out for the hours of 2026-09-23 the lamp was deleted and
    // back with it that evening, asset and all.
    GoJump,              // Follow (`f`)
    // Breeze's timeline-lift (2026-09-04, the architect's pick): a clip's two
    // end brackets with a red cross between them — a stretch of timeline the
    // editor declines to travel. The lamp it wears refuses an undo whose
    // restore would switch the view on screen (2026-09-22). A verbatim
    // 22px transcription, and the roster's fourth two-colour file.
    TimelineLift,        // Toggle restrict undo to current view (`z`)
    // THE ITERATION GROUP'S TWO GLYPHS, BACK WITH THEIR BUTTONS (architect
    // 2026-09-04): the row had room again, so the Iterations dropdown was
    // deleted and the BPM opener (bare `m` then, Ctrl+B since 2026-09-15) and
    // grid iteration mode (bare `i`)
    // returned to the icon row in a group of their own. Both files are the
    // ones the 2026-08-27 relocation deleted, re-transcribed verbatim from the
    // shipped SVGs and re-committed under assets/icons/breeze/ — Breeze's
    // music-note-16th, the flagged quaver, for the BPM opener, and mathmode,
    // an italic f beside a multiplication cross reading as f(x), for the mode
    // lamp. Neither picture moved while it was away and neither is a fresh
    // pick, so the metaphors below are the 2026-08-01 and 2026-08-18 rulings'
    // own, unchanged.
    //
    // MATHMODE'S SLOT KEEPS A MATH SYMBOL and f(x) names the OPERATION — a
    // render as a function of a variable swept across a bracket, which is what
    // a grid iteration sweep is — where the SUMMATION SIGMA it yielded to on
    // 2026-08-18 says summing, which is the reading the history walk wanted
    // (BlackSum below carries that succession).
    MusicNote16th,       // BPM iterations (Ctrl+B)
    Mathmode,            // Toggle grid iterations (bare `i`)
    PreviewRenderOn,     // Listen to a render
    // THE CHECKMARK HAS TWO READERS SINCE 2026-09-01, one per surface that
    // runs a load in place: the ICON ROW's button (the `h` view's load, its
    // group changed that day) and the RENDER PLAYER's modal row, which took
    // this same glyph when its last two word buttons became glyphs — "the
    // media player button should get the checkmark glyph then", the architect
    // naming the icon-row button's own face for the act one surface over.
    DialogOkApply,       // Load a state in place as the baseline
    VcsDiff,             // The history mode (`h`)
    // Breeze's shallow-history: a clock face with NO sweep arrow — the
    // session's own undo/redo timeline, which reaches back no further than this
    // run. It arrived 2026-08-18 for the history view's Session walk radio and
    // is THE WALK LAMP'S WHOLE FACE since the 2026-09-04 collapse, the lamp
    // lighting in Session and so wearing the lit state's glyph.
    //
    // ITS DEEP SIBLING WENT WITH THE GIT HALF: deep-history, the same dial with
    // a curl-back arrow sweeping around it, was the set's one TWO-CLASS icon
    // (dial .ColorScheme-Text, arrow .ColorScheme-Accent). It dressed the
    // CUMULATIVE reading's toggle from 2026-08-09 until that toggle took the
    // summation sigma on 2026-08-18, when it was freed for the Git walk radio —
    // a committed history being what a deep clock sweeps. Its candidate
    // succession is kept at the shallow-history entry in icons.cpp so none of
    // it is re-proposed without a new ruling.
    ShallowHistory,            // The walk lamp, lit in Session (bare `g`)
    // THE ADD-TO-SELECTION ACT'S GLYPH (2026-08-18): Breeze's edit-select, the
    // pointer arrow over a marquee corner — picking one more thing up.
    EditSelect,          // Add to selection
    // THE WALK'S TWO ARROWS (2026-08-05 as go-previous / go-next, REGLYPHED
    // 2026-08-11): the checkpoint walk's older (`,`) and newer (`.`) steps wear
    // Breeze's keyframe-previous / keyframe-next — a stopwatch dial with a
    // solid triangle pointing into the past or the future. The architect's
    // reason is their neighbour: Deep-History is a CLOCK, so the steps beside it
    // read as clock steps. The chevrons they yielded are row 8's left and right
    // arrows now, and the succession with its runners-up is recorded at the
    // table entry.
    KeyframePrevious,    // Older checkpoint (`,`)
    KeyframeNext,        // Newer checkpoint (`.`)
    // The chevron pair, go-jump's own construction minus its destination dot:
    // ONE closed outline per file whose limbs are one viewBox unit thick, so
    // the weight rides the icon's scale like every other geometry here and
    // there is no stroke to set. Row 8's horizontal arrows are their whole
    // consumer list since 2026-08-11 (they served the walk 2026-08-05..11, and
    // both buttons at once for the few hours row 8 shared them).
    GoPrevious,          // The left arrow (bare Left)
    GoNext,              // The right arrow (bare Right)
    // THE REVERT ACT'S GLYPH (2026-08-05), the history group's third button: the
    // selected diff flags applied backwards into the live state (bare `v`,
    // `Ctrl+H` until 2026-09-01).
    // Breeze's own document-revert — a page with an arrow curving back into it,
    // which is the act — and the FIRST committed file whose `d` uses the smooth
    // cubic (`s`), the one command the interpreter grew for it.
    DocumentRevert,      // Revert the selected differences (bare `v`)
    // Row 8, the transport row (2026-08-11, the touch arc's first surface;
    // a tenant of the unified bottom row since 2026-08-12):
    // SEVEN new glyphs for the eight buttons IT THEN HAD — the four cardinal
    // arrows took
    // GoPrevious / GoNext for left and right, SHARED with the walk pair at
    // first (an Icon is a GLYPH, not a button: VcsCommit already serves two
    // faces) and theirs alone since the walk reglyphed to the keyframe pair
    // later that day — with GoUp / GoDown below completing the chevron family,
    // so all four arrows are the
    // same Breeze construction — one closed outline whose limbs are one
    // viewBox unit thick, no stroke to set. The FOUR media-* glyphs are
    // Breeze's own set (the universal transport vocabulary; media-playback-
    // pause was the considered runner-up for the stop slot and lost — Space
    // STOPS, it does not pause, and the face must not promise a resume). They
    // dress THREE buttons since 2026-08-15, play and stop having collapsed
    // into one; the glyph count did not move with the button count.
    // dialog-cancel (the circle-slash) is Breeze's one "cancel" glyph, no
    // runner-up considered — transcribed for the row's short-lived Esc button
    // (deleted the same day at the architect's live pass) and kept as the
    // RENDER button's mid-render Cancel face (redesign_button_icon).
    MediaSkipBackward,   // Go to start (bare Home)
    // PLAY AND STOP ARE ONE BUTTON'S TWO FACES since 2026-08-15 (architect,
    // collapsing the pair): bare Space is one toggle, so the roster carries
    // ONE member — RedesignButton::TransportPlayStop — wearing whichever of
    // these two the live audition bit selects, Save's and Render's own
    // stateful-glyph shape (redesign_button_icon, paint_handler.cpp). They
    // were TWO buttons over the one chord from the row's first day until then,
    // most recently as a radio pair; an Icon is a GLYPH rather than a button
    // and neither entry moved.
    MediaPlaybackStart,  // Play (bare Space, the face while stopped)
    MediaPlaybackStop,   // Stop (bare Space, the face while an audition runs
                         // — the ROSTER'S own, and its one reader since the
                         // render player's Stop button retired 2026-09-01)
    // THE RENDER PLAYER'S PAUSE FACE (architect 2026-08-28): its row
    // carried Play/Pause AND Stop as two buttons — "one button that's either
    // play or pause, and the other one is stop" — so the two-faced button
    // needed a pause glyph of its own rather than the stop square it wore
    // while live. Breeze actions/22/media-playback-pause, one fresh verbatim
    // transcription. THE PAUSE STAYS WITHOUT THE STOP (2026-09-01): the
    // player's transport parks a resumable rest whatever the row's shape, so
    // the live face is a pause there. The ROSTER'S transport button is
    // untouched: bare Space
    // there is one toggle over the project's audio with no pause state, so it
    // keeps Play/Stop.
    MediaPlaybackPause,  // Pause (the player's row, the face while live)
    MediaSkipForward,    // Go to end (bare End)
    DialogCancel,        // Render's mid-render Cancel face (row 2)
    GoDown,              // The down arrow (bare Down)
    GoUp,                // The up arrow (bare Up)
    // THE READ-ONLY TOGGLE'S TWO STATES — the icon row's last group since
    // 2026-08-14 (row 3's tab lock slots from 2026-08-01 until then, the same
    // two glyphs): the closed padlock while the ACTIVE tab is read-only and
    // the OPEN one while it is writable, swapped by redesign_button_icon.
    Lock,                // Locked: closed padlock, full color
    Unlock,              // Unlocked: open padlock, drawn dimmed by the caller
    // THE BOTTOM ROW'S MARKER-WALK GROUP (architect-picked 2026-08-15 from a
    // rendered candidate sheet, the row's right cluster ahead of the four
    // arrows): Previous Marker (Shift+Tab) and Next Marker — the walk — (Tab,
    // and Shift+Tab on its shifted press; its landing's camera the audio
    // view's since 2026-09-23). HIS OWN REASONS,
    // kept because they are about this row's crowding rather than about the
    // glyphs in isolation:
    //   bbox-prev / bbox-next are AN ARROW MEETING A BAR, which is the Tab
    //   key's own shape — and they share no silhouette with the chevrons two
    //   slots away (the cardinal arrows), the media-skip triangles at the
    //   row's left, or the keyframe dials the history walk wears in the same
    //   cluster inside the `h` view.
    // (BOOST, the two-arrow cycle the group's third button wore for walk both
    // tabs, is DELETED with that button on 2026-09-14 — its enumerator, its
    // def and its committed asset — no other button wearing it; the march is
    // Switch Tab's shifted press now. It was the file that brought the
    // interpreter's stroked arm back and the one producer of the per-path line
    // cap, which went with it.)
    // BBOXPREV IS BACK (architect 2026-09-29) with the Previous Marker
    // button: it left on 2026-09-22 when the walk pair merged into one button
    // wearing bboxnext, and returned byte-verbatim (its committed asset
    // restored from git history) when the walk group grew a dedicated
    // Previous Marker again.
    // (SNAP-ORTHOGONAL and SNAP-NODE, Breeze's node-on-a-dotted-cross and
    // node-at-the-end-of-a-dotted-run, stood here from 2026-09-22 as the
    // centring walk's and the least-movement walk's glyphs, bboxnext deleted
    // for that day; both were DELETED 2026-09-23 with the least-movement walk
    // button — enumerators, defs and committed assets — and bboxnext came
    // back byte-verbatim as the one walk's glyph.)
    BboxPrev,            // Previous Marker (Shift+Tab; Ctrl for the march)
    BboxNext,            // Next Marker (Tab; Shift+Tab on the shifted press)
    // THE SWITCH TAB BUTTON'S GLYPH (architect 2026-09-29), the walk group's
    // last: Breeze Dark's actions/22/tab-detach — a tabbed folder whose tab
    // stands apart from the body, the other tab being the act. One fresh
    // verbatim transcription.
    TabDetach,           // Switch Tab (Ctrl+Tab; Shift for the march)
    // THE SETTINGS BUTTON'S GLYPH (architect 2026-09-29), the icon row's
    // render-entry group, after the padlock: Breeze Dark's
    // actions/22/settings-configure — a symlink in the theme onto
    // configure.svg, copied resolved (the dialog glyphs' precedent), two
    // sliders with their knobs. The enumerator keeps the name the architect
    // picked it by (the theme-provenance rule).
    SettingsConfigure,   // Settings (bare `;`)
    // (THE HOLD-COLUMN NUDGES' GLYPHS stood here: Breeze's go-previous-context
    // / go-next-context from 2026-09-22, then for the hours of 2026-09-23
    // snap-nodes-midpoint TURNED A QUARTER left and right, the product's
    // first icon modification. All four enumerators, their defs and the
    // committed assets are DELETED — the last two 2026-09-23 with the two
    // buttons, the architect having made the held column a posture of the
    // bare arrows. The rotation's record is at icons.cpp's IconTransform.)
    // (TEXT-FIELD, Breeze's own text cursor — a serif I-beam on a field's
    // underline rule — was the EDIT FLAG button's glyph from 2026-08-27 until
    // the architect deleted the button 2026-09-29; the enumerator, its def and
    // its asset left with it. Its three rejected neighbours stay recorded for
    // any future editor-opening button: insert-text reads as ADD,
    // edit-select-text names SELECTING text, edittext is a pencil.)

    // THE FOLDER OVERLAY'S TWO ROW GLYPHS (2026-08-28, the render player):
    // places/22/folder, the Breeze dark folder every file picker on the
    // architect's desktop paints beside a folder row (pcmanfm-qt and
    // kdenlive's Open dialog alike), and mimetypes/22/audio-x-wav, the glyph
    // pcmanfm-qt paints beside a wav — a bracket-shaped double note, the
    // file's literal Breeze blue (kIconWav, icons.cpp;
    // audio-x-generic is never a row glyph, the architect's ruling). Both are painted by
    // folder_overlay rows (paint_handler.cpp) and by nothing else; the names
    // are their Breeze file names, the theme-provenance rule.
    Folder,              // a folder row (a batch folder at the player's root,
                         // a project row in the Open project picker)
    AudioXWav,           // a wav row

    // THE RENDER PLAYER'S REPEAT TOGGLE (architect 2026-08-28): Breeze's
    // actions/22/media-repeat-single — the loop with a "1" — worn in BOTH
    // states by the modal row's one lamp, "a plain toggle: off is the
    // unpressed face, on is the pressed/lit face", so the glyph never
    // changes and the LAMP carries the state (the since-deleted trim region
    // toggle's precedent; the text names the toggle since 2026-09-01, the rule at
    // redesign_button_tooltip's head). Breeze spells "repeat one" as media-repeat-single /
    // media-playlist-repeat-song, two names for one artwork; there is no
    // media-repeat-one.
    MediaRepeatSingle,   // Repeat one (the player's modal row)
    // THE PLAYER ROW'S UP BUTTON (architect 2026-09-01): Breeze's
    // actions/22/go-parent-folder — an open folder with an arrow rising out of
    // it. The button landed earlier the same day wearing the roster's `go-up`
    // chevron, and the chevron says "up" about a NUMBER while this says it
    // about a DIRECTORY, which is the act: leave this batch folder for the one
    // above it. One fresh verbatim transcription; the ROSTER's bare-Up
    // transport button keeps GoUp, an Icon being a glyph rather than a button.
    // The enumerator keeps the Breeze file name (the theme-provenance rule),
    // so it is GoParentFolder and not IconPlayerUp.
    GoParentFolder,      // Up, out of a batch folder (the player's modal row)
    // THE NOTIFICATION CARDS' THREE (2026-08-29, the messaging redesign's
    // card half — notifications.h): the two CLASS glyphs at a card's left and
    // the X that stood at its right until 2026-10-01, three fresh verbatim
    // transcriptions from the
    // architect's own Breeze Dark (status/22/dialog-information,
    // status/22/dialog-error — both symlinks in the theme, onto
    // data-information and data-error, copied resolved — and
    // actions/22/window-close). The two dialog files are TWO-COLOUR icons in
    // media-record's shape, a coloured plate under a white glyph: the plate
    // is the file's own scheme class (`.ColorScheme-Accent` for information,
    // `.ColorScheme-NegativeText` for error, the roster's existing
    // kIconAccent and kIconNegativeText) and the glyph is the file's literal
    // #fff, so the glyph is what tells the classes apart at a glance and no
    // caller colours it — the roster paints each path in the table's own ink,
    // as it always has. window-close is the ordinary `.ColorScheme-Text` X.
    // WINDOW-CLOSE TOOK A SECOND READER ON 2026-09-01 — the render player's
    // modal row wears it on CLOSE, the architect having ruled the row's last
    // two word buttons into glyphs ("Close should then get a glyph also, to
    // avoid being the odd one out: window-close.svg") — which needed no new
    // entry and no new transcription: a def is a GLYPH and several buttons are
    // free to wear one, GoUp's own precedent above. THAT IS ITS ONE READER
    // SINCE 2026-10-01, when the card's X retired ("whole card dismisses, X
    // gone"). `kIconCount` is unmoved.
    DialogInformation,   // a NORMAL card's glyph
    DialogError,         // a CRITICAL card's glyph
    WindowClose,         // the X: the render player's Close

    // THE COPY VALUE BUTTON'S GLYPH (2026-08-29; the bottom row's verb group
    // until 2026-09-29, the icon row's Undo group since, between Redo and
    // Render):
    // Breeze's actions/22/edit-copy, the two stacked sheets — the same file
    // this roster carried for the ICONCOPY button from 2026-08-12 until the
    // 2026-08-20 propagate relocation deleted that button, transcribed FRESH
    // here rather than recovered (the def and the asset had gone with the
    // consumer). It says "take this value with you", which is what Ctrl+C
    // does with the focused marker's resolved tempo.
    EditCopy,            // Copy resolved value (the icon row, after Redo)

    // THE ENABLE TOOLTIPS LAMP'S GLYPH (architect 2026-09-29), the icon
    // row's render-entry group, its last, behind Settings: Breeze
    // Dark's actions/22/help-whatsthis — the "What's This?" ring with its
    // pointer arrow, the desktop's own glyph for asking a control its name.
    HelpWhatsthis,       // Toggle Tooltips (bare backslash)
    // THE JUMP TO DEFINING MARKER BUTTON'S GLYPH (architect 2026-09-29), the
    // bottom row's verb group after Toggle Inherit: Breeze Dark's
    // actions/22/go-jump-declaration — a flag on its staff beside a return
    // arrow, the declaration a reference goes back to.
    GoJumpDeclaration,   // Jump to Defining Marker (Ctrl+J)
    // THE RENDER PLAYER'S DELETE GLYPH (architect 2026-09-29), worn by the
    // player's row at its root listing in Load in Place's slot: Breeze Dark's
    // actions/22/edit-delete, the waste bin in `.ColorScheme-NegativeText`,
    // the table's red (kIconNegativeText).
    EditDelete,          // Delete a batch folder (the player's modal row)
    // THE APP'S OWN ICON (architect 2026-10-05: the caption wears "the app's
    // EXISTING icon"), worn by the caption at 16 x 16 (render.h's caption
    // block): the desktop launcher's, packaging/warptempo_gui.svg, the
    // tablet's launcher made whole — the sheet-grey plate under the filled
    // quaver. Not a Breeze glyph and no roster button's: the one icon of the
    // product's own, transcribed from its own file. AT A BITMAP gui_scale
    // (architect 2026-10-05, the icon pass's own caption pick) it instead
    // wears Chicago95's status/audio-volume-high.png (icons.h's
    // kChicago95Files, draw_bitmap's plain blit at the spot the caption
    // already paints it — paint_caption_row), the vector form above standing
    // at every other scale.
    AppIcon,             // the caption's icon
};

// Roster size, for the once-per-icon diagnostic latch in draw(). Keep it equal
// to the enumerator count above; a mismatch only costs that icon its latch (the
// latch is bounds-checked), never correctness.
// 57 SINCE THE VIEW GROUP'S ARRIVAL (architect 2026-10-01): document-export,
// document-import and chronometer-start came back with the icon row's three
// view buttons — enumerators, defs and assets together. The count's
// succession is in git history; a glyph joining or leaving restates this
// number. 58 SINCE THE CAPTION'S ICON (AppIcon, 2026-10-05).
inline constexpr int kIconCount = 58;

// Draw `icon` with its viewBox mapped onto the square (x, y, size_px, size_px),
// filling each of its paths in that path's OWN color (the table's: a path the
// SVG inks in the scheme's text class takes the theme's LABEL, every other
// path its hard-coded ink — the ink block at icons.cpp).
//
// Uniform scale, no aspect fitting: every icon here is square by construction
// (viewBox 0 0 22 22). Cairo state is saved and restored; the caller's source,
// path and matrix survive untouched.
//
//
// A malformed `d` string is a PROGRAMMING ERROR, not a runtime state — the
// strings are in-tree constants — so a parse failure emits one stderr line and
// draws nothing. That is deliberately all the machinery there is: a silent
// fallback would hide a transcription typo forever.
void draw(cairo_t* cr, Icon icon, double x, double y, double size_px);

// THE GLYPH IN ONE INK — every path of `icon` in `ink`, the same square,
// validation and stderr rule as draw. Its one caller is the folder overlay's
// lit row, which inks its glyph in the theme's selected text as it inks the
// row's name (architect 2026-10-03: text over a fill is the fill's recorded
// pair). (It replaced draw's mix_color recolouring the same day, the mix
// retired with the luminance rule.)
void draw_in_ink(cairo_t* cr, Icon icon, double x, double y, double size_px,
                 GuiColor ink);

// THE ENGRAVED GLYPH — a disabled toolbar button's face (architect
// 2026-10-02, the AB set; Windows' DrawState DSS_DISABLED): the glyph's whole
// shape, every path in one ink, painted TWICE — first in the theme's HILIGHT
// `offset_px` right and down (the caller passes one Windows px,
// relief_line_px), then in its Shadow at (x, y) — so the dead glyph reads
// as cut into the face. THE GLYPH HALF OF THE DISABLED EMBOSS (architect
// 2026-10-03; the word half is show_embossed_run, render.h, the rule at the
// palette block). Same square, same validation and the same one-stderr rule
// as draw above.
void draw_engraved(cairo_t* cr, Icon icon, double x, double y, double size_px,
                   double offset_px);

// THE CHICAGO95 BITMAP PASS (architect 2026-10-05): AT A BITMAP gui_scale (a
// whole multiple of 100 — gui_font.h's gui_scale_is_bitmap, read here on
// gui_scale_percent() so the icons turn bitmap in the same breath as the
// text) every ROSTER BUTTON'S glyph this product paints through draw_cased
// below comes from its period Chicago95 16-px picture
// (assets/icons/chicago95/16/, one pick per glyph recorded at
// assets/icons/chicago95/mapping.md) instead of its Breeze vector: decoded
// once at launch (install_chicago95_bitmaps), each icon pixel a k x k block
// of device px (k = gui_scale/100) blitted with cairo's own nearest-neighbour
// scaling (CAIRO_FILTER_NEAREST on an integer scale leaves every block's
// edges on whole device px, so no hand block loop is needed). AT EVERY OTHER
// SCALE, or for a glyph the table does not carry, this is draw() unchanged
// (AppIcon is never cased — it wears its Chicago95 picture through
// draw_bitmap instead, render.h's caption block).
//
// PLACEMENT (ruling 2): draw() seats a 16-px bitmap toolbar icon at the
// case's (3, 3) as Windows always does; THIS CALL THEN RE-CENTRES ON THE
// INK — the icon's own opaque pixels, alpha >= 128, read off the decoded
// surface ONCE AT INSTALL (chicago95_ink_box below, "one function, no hand
// table": no L/T/R/B is authored here, mapping.md's columns are the
// documentation of what that function already computes, not its source) —
// so the ink box sits as centred in the WHOLE 23 x 22 CASE as whole Windows
// px allow, a half-pixel tie resolving UP and LEFT (the optical centre, not
// the arithmetic one). `case_x`/`case_y` are the case's own top-left corner
// (NOT the (3, 3)-offset glyph origin draw() takes) and `button_shift_px` is
// the button's own pressed/checked shift (paint_button_box's `ButtonBoxFace
// ::shift` — ruling 4: the glyph keeps its existing +1,+1 Windows px move,
// applied on top of the ink-centred placement precisely as it is applied to
// draw()'s (3, 3) placement today).
void draw_cased(cairo_t* cr, Icon icon, int case_x, int case_y,
                double size_px, int button_shift_px);

// THE DISABLED CASED GLYPH (architect 2026-10-05, ruling 3): draw_cased's
// own road for a DEAD roster button — same case corner, same ink-centred
// placement and the same pressed/checked `button_shift_px` — but instead of
// the Chicago95 picture's own colours it draws WINDOWS' DSS_DISABLED EMBOSS
// of a MONO MASK of it: the icon's fully opaque pixels (alpha 255; a
// partially-transparent edge pixel is never masked, Chicago95 having no such
// pixels at 16 px) whose own colour is neither white (#FFFFFF) nor Windows'
// button-face silver (#C0C0C0) — the toolbar's own ground and the metal the
// WordPad reference shows surviving a greyed glyph — read off the alpha and
// colour bytes ONCE AT INSTALL (chicago95_for's Chicago95Icon::mask_rows, a
// row of 16 bits, the mask's one source — mapping.md's columns describe the
// ink box; this predicate has no table of its own for the same reason ruling
// 2's ink box does not). Each mask pixel is a k x k device-px block, painted
// TWICE exactly as draw_engraved paints a vector glyph's whole shape: the
// theme's Hilight at `offset_px` (one Windows px, the caller's
// relief_line_px) right and down, then the theme's Shadow at the glyph's own
// place — so a dead glyph is cut into the face in the colours every other
// disabled face already wears, the Chicago95 artwork itself never appearing
// (render.h's palette block: the one further exception there). At every
// other scale, or for a glyph the mapping does not carry, this is
// draw_engraved at draw()'s own (3, 3)-in-the-case vector placement,
// unchanged — the Breeze glyph's own disabled face, draw_cased's own
// fallback precedent.
void draw_cased_disabled(cairo_t* cr, Icon icon, int case_x, int case_y,
                         double size_px, int button_shift_px,
                         double offset_px);

// THE BARE BITMAP GLYPH, NO CASE (architect 2026-10-05, ruling 1's four
// non-button glyphs: the folder overlay's folder and wav row glyphs, and a
// notification card's NORMAL / CRITICAL glyph): at a bitmap gui_scale the
// whole 16 x 16 Chicago95 picture is blitted nearest-neighbour into
// (x, y, size_px, size_px) exactly, IN ITS OWN COLOURS — none of these sites
// owns a Windows CASE to ink-centre inside the way a toolbar button does
// (there is no (23, 22) box around a list row's or a card's glyph slot), so
// there is nothing to re-centre against and the site's own existing (x, y)
// placement stands unchanged. At every other scale, or for a glyph the
// mapping does not carry, this is draw() unchanged.
void draw_bitmap(cairo_t* cr, Icon icon, double x, double y, double size_px);

// draw_bitmap's sibling for a site that recolours a vector glyph when its
// row is lit (icons::draw_in_ink — the folder overlay's selected row, text
// over a fill taking the fill's own pair): AT A BITMAP SCALE `ink` IS
// IGNORED, the Chicago95 picture being unrecolourable period artwork (render
// .h's palette block, the one further exception) blitted exactly as
// draw_bitmap above; at every other scale this is draw_in_ink(ink)
// unchanged, so a lit row's vector glyph still takes the selected text as it
// always has.
void draw_bitmap_in_ink(cairo_t* cr, Icon icon, double x, double y,
                        double size_px, GuiColor ink);

// THE FIVE-FILES INSTALL, Chicago95's own: one PNG per distinct picture the
// mapping table names (assets/icons/chicago95/16/*.png — the directory IS
// the inventory; kChicago95Files lists its basenames, no extension, THE
// LAPTOP'S CONFIGURE-TIME GLOB'S OWN ORDER, same contract as
// kGuiFontFiles — see gui_font.h's head for why the order is shared between
// the two backends' install calls).
inline constexpr std::size_t kChicago95FileCount = 52;
extern const char* const kChicago95Files[kChicago95FileCount];

// A DECODED ICON, both backends' common currency (architect 2026-10-05,
// after the APK build caught the gap: Android's cairo is built with
// -Dpng=disabled, android/deps/50_cairo.sh's own choice, so cairo's PNG
// stream reader cannot appear in any TU the Android target compiles, and
// icons.cpp IS one — WARPTEMPO_GUI_SOURCES is shared). Sixteen Windows px
// square, cairo's own ARGB32 layout: PREMULTIPLIED, 32-bit words, native-
// endian (0xAARRGGBB — on this little-endian target, bytes B, G, R, A in
// memory order), no padding (stride exactly 16 * 4). Each backend's own
// decoder is responsible for landing in exactly this shape — a source PNG
// with no alpha channel normalizes to it fully opaque (alpha 255 at every
// pixel), which is music-player.png's case and why its ink box is the whole
// 16 x 16 square (mapping.md's own recorded row).
struct Chicago95Pixels {
    const uint8_t* argb32 = nullptr; // kChicago95PixelBytes bytes
};
inline constexpr std::size_t kChicago95PixelBytes = 16 * 16 * 4;

// THE ONE SHARED INSTALL, decoded pixels in: builds each icon's OWN cairo
// ARGB32 surface (the bytes above COPIED into it — the fonts' own lifetime
// contract, the caller may free its buffer the moment this returns) and
// computes its ink box FROM THE ALPHA CHANNEL ALONE (ruling 2's "one
// function, no hand table"). BREACH-ONLY, the fonts' own contract
// (gui_font_install_bundled): the files are the repository's own, so a
// decode failure upstream of this call is a build defect, and this call's
// own failure (no opaque pixel) is one too.
bool install_chicago95_bitmaps(const Chicago95Pixels (&files)[kChicago95FileCount]);

// THE FIVE-FILES' RAW BYTES, Chicago95's own: one PNG per distinct picture,
// as committed (assets/icons/chicago95/16/<name>.png), UNDECODED — each
// backend decodes its own copy, below. The bytes are COPIED, like the
// fonts': the caller may free or unmap them the moment the decode call
// returns.
struct Chicago95Bytes {
    const uint8_t* data = nullptr;
    size_t         len  = 0;
};

// THE LINUX BINARY'S COPY, in kChicago95Files' order, defined by
// icons_chicago95_embedded.cpp (generated at configure time, CMakeLists.txt
// — the fonts' own mechanism, gui_font_embedded.cpp's). The APK carries the
// same files as assets instead (android/app/build_apk.sh), decoded by
// platform_android.cpp's own road (AImageDecoder, below).
extern const Chicago95Bytes chicago95_embedded_files[kChicago95FileCount];

// THE LINUX DECODE, cairo's own PNG stream reader — THE ONE CALLER OF IT IN
// THIS PRODUCT, and the reason it lives in icons_chicago95_decode_linux.cpp
// rather than icons.cpp: that reader is unavailable on Android's cairo
// build (its header comment, above), so no TU the Android target compiles
// may name it, and icons.cpp is compiled into both. Each PNG decodes to
// whatever format cairo's loader picks (ARGB32 for one with an alpha
// channel, RGB24 — no alpha at all — for music-player.png) and is then
// NORMALIZED to ARGB32 by painting it onto a fresh transparent ARGB32
// canvas (an opaque RGB24 source paints OVER at alpha 255 throughout), so
// install_chicago95_bitmaps above is handed exactly one pixel shape
// regardless of which format a source PNG triggers. Declared here (a
// platform-neutral header) but DEFINED ONLY IN THE LINUX TARGET; the one
// caller is platform_wayland.cpp's GuiPlatform::init, which is itself
// Linux-only, so there is no Android link-time reference to resolve.
bool install_chicago95_bitmaps_from_png_linux(
    const Chicago95Bytes (&files)[kChicago95FileCount]);

} // namespace icons
