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
// THE INTERPRETER HAS GROWN EXACTLY FOUR FEATURES past the plain filled path
// it started as, each with a committed producer and each taken so that the `d`
// string in the table stays VERBATIM rather than being flattened by hand:
//   - a per-path TRANSLATE (dialog-ok-apply's and dialog-cancel's transform);
//   - the SMOOTH CUBIC `s` (document-revert's arrow lobes, 2026-08-05);
//   - the STROKED PATH (boost's four open polylines, 2026-08-15 — RESTORED
//     rather than invented: the arm lived for part of 2026-08-11 for
//     distortionfx, went producer-less when the architect reglyphed that
//     button hours later, and came back with a real producer; the per-path
//     LINE CAP boost brought with it was deleted with boost on 2026-09-14,
//     and tool-rect-selection is the arm's one producer since);
//   - the STROKE'S OWN WIDTH AND DASH (tool-rect-selection, 2026-08-16), which
//     is a widening of the third rather than a fourth kind of thing: the pen
//     had been hard-coded to boost's implicit defaults, and the marching-ants
//     rectangle is the first file to state either attribute.
// Each is described where it is implemented (icons.cpp's table header and its
// `d`-interpreter header). A general per-path MATRIX was grown alongside the
// stroke for distortionfx and did NOT come back with it — no stroked file
// carried a transform — so that one is still git history alone.
//
// THREE FILES DEPART FROM THE VERBATIM RULE and they are stated here as well
// as at their table entries, because the rule is what this header promises:
// tool-rect-selection's geometry is a `<rect>` element, not a `<path>`, so
// there is no `d` in the file to copy and its row holds a four-number
// derivation instead, and since 2026-08-29 the two dialog glyphs' PLATES
// (dialog-information, dialog-error) are `<rect rx="2">` elements under a
// verbatim glyph path, their rows spelling the rounded rectangle SVG defines
// for that rx. A `<rect>` parser was declined for one file and stays
// declined for three.
//
// PROVENANCE: the SVGs the tables were transcribed from are committed under
// assets/icons/breeze/. They are the record of what this code draws; the
// d-strings here are copied from them VERBATIM, so a diff between the two is a
// transcription bug and nothing else — with the ONE `<rect>` file's derivation
// as the stated exception just above. They are read by no code at runtime — the
// product ships no icon files and reads none.
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
    // Row 4, the icon row. (ZoomOut / ZoomIn lived here 2026-08-01..08-02, for
    // the icon row's zoom pair; both went with those buttons.)
    //
    // (THE TWO VIEW LAMPS' FACES — document-import and chronometer-start,
    // architect-picked 2026-08-11 off a rendered candidate sheet for the four
    // radios these two buttons replaced — lived here from 2026-08-11 to
    // 2026-09-15, when the architect deleted both lamp buttons whole with
    // their view-lamp category: enumerators, table defs and assets together,
    // the edit-cut precedent, since neither glyph had a second consumer.
    // TARGET had worn document-import, the arrow ENTERING a document, its
    // Source partner document-export having left the same way on 2026-09-04
    // when the radios first collapsed to lamps; PHASE RESET had worn
    // chronometer-start, the stopwatch with the solid play triangle in its
    // dial — start the clock anew — picked over chronometer-reset and
    // view-refresh (indistinguishable from each other at row size, and
    // chronometer-reset's dial not surviving the rendering) and over the bare
    // chronometer, its Warp partner speedometer having left with document-export
    // on 2026-09-04 too. Before the 2026-08-11 glyphs, the S/T and W/P pairs
    // wore shaped LETTER GLYPHS from the row's first day — the row's only
    // non-icon buttons, and the reason the architect briefly ruled the radios
    // deleted altogether ("ugly letter blips") before reversing that the same
    // day; the painter's shaped-letter branch left with that reversal, well
    // before the buttons themselves went. All picks and runners-up stay
    // recorded here so none is re-proposed without a new ruling: document-export
    // was the arrow LEAVING a document against document-import's arrow
    // entering one; speedometer was the gauge with the needle, his first pick,
    // reversed to distortionfx (the spiral, "time bends") in the same breath
    // and restored at his second look that evening, leaving distortionfx and
    // player-time the runners-up.)
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
    // THE SHOW TRIM REGION BUTTON'S GLYPH (architect 2026-08-16), and the trim
    // group's ONE member since the scissors left on 2026-08-18 — the button
    // that inherited their chord hours later (bare `x` then, bare `[` since
    // 2026-08-24): Breeze's
    // TOOL-RECT-SELECTION, the marching-ants
    // rectangle — a dashed box says "a selected span" everywhere, and the
    // button's whole job is to put one on the waveform. Taken at 22 though the
    // architect named the 24px path: the two files hold the SAME rectangle (24
    // wraps it in a translate inside a 24 viewBox) and 22 is this set's
    // convention. It is the table's ONE `<rect>` file, so its `d` is a
    // four-number derivation rather than a verbatim copy — stated at the table
    // entry, which also records the two stroke attributes it brought (a
    // non-default width and the dash).
    ToolRectSelection,   // Toggle trim region (bare `[`)
    // THE ZOOM PAIR (architect-picked 2026-08-12, the grand relayout's
    // roster commit — the icon row's viewport-class group, behind the trim
    // region toggle). Breeze's own magnifier family: the fit frame (full zoom
    // out — bare `0`'s whole-song arm) and the 1:1 original (working-zoom
    // center, bare `c`). The plain zoom-in / zoom-out magnifiers that stood
    // beside them left with the Zoom In / Zoom Out buttons on 2026-09-14
    // (architect 2026-09-14) — enumerators, defs and assets together, no
    // other button wearing them.
    ZoomFitBest,         // Full zoom out / overview (bare `0`)
    ZoomOriginal,        // Working-zoom center (bare `c`)
    // ZOOM-OUT-Y, the vertical magnifier (the ruler on its dial beside a
    // minus): worn by the Ignore Waveform Magnification lamp between Center
    // and Follow (architect 2026-09-22), restored with its def and asset for the
    // third time — the lamp's stands of 2026-09-14 and 2026-09-17 wore it too.
    ZoomOutY,            // Toggle Ignore Waveform Magnification (bare `]`)
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
    // (/usr/share/icons/breeze/actions/22/merge.svg): every asset in this set
    // is 22 and every IconDef is {22.0, ...}, so the glyph is transcribed at
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
    GoJump,              // Follow mode
    // Breeze's timeline-lift (2026-09-04, the architect's pick): a clip's two
    // end brackets with a red cross between them — a stretch of timeline the
    // editor declines to travel. The lamp it wears refuses an undo whose
    // restore would carry the camera off the stretch on screen. A verbatim
    // 22px transcription, and the roster's fourth two-colour file.
    TimelineLift,        // Toggle restrict undo to viewport (`z`)
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
    // THE RENDER PLAYER'S PAUSE FACE (2026-08-28, architect R36): its row
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
    // arrows): the walk (Tab, and Shift+Tab on its shifted press). HIS OWN
    // REASONS, kept because they are about this row's crowding rather than
    // about the glyphs in isolation:
    //   bbox-prev / bbox-next are AN ARROW MEETING A BAR, which is the Tab
    //   key's own shape — and they share no silhouette with the chevrons two
    //   slots away (the cardinal arrows), the media-skip triangles at the
    //   row's left, or the keyframe dials the history walk wears in the same
    //   cluster inside the `h` view.
    // (BOOST, the two-arrow cycle the group's third button wore for walk both
    // tabs, is DELETED with that button on 2026-09-14 — its enumerator, its
    // def and its committed asset — no other button wearing it; the march is
    // the tab row's shifted press now. It was the file that brought the
    // interpreter's stroked arm back and the one producer of the per-path line
    // cap, which went with it.)
    // (BBOXPREV, the Previous marker button's glyph, is DELETED with that
    // button on 2026-09-22, when the walk pair merged into one button wearing
    // bboxnext — its enumerator, its def and its committed asset.)
    BboxNext,            // The walk (Tab; Shift+Tab on the shifted press)
    // THE HOLD-COLUMN NUDGES' GLYPHS (architect 2026-09-22), the walk group's
    // second and third: Breeze's go-previous-context / go-next-context, a
    // tag-shaped arrow carrying a pair of braces — the chevron's direction
    // with a mark that it is not the plain arrow two slots away.
    GoPreviousContext,   // Ctrl+Left, the held-column nudge left
    GoNextContext,       // Ctrl+Right, the held-column nudge right
    // THE EDIT FLAG BUTTON'S GLYPH (2026-08-27), the bottom row's verb group
    // after Toggle inherit: text-field, Breeze's own
    // TEXT CURSOR — a serif I-beam standing on a field's underline rule.
    //
    // WHY THIS ONE OF THE FAMILY'S FOUR. The architect asked for "an
    // I-beam-ish button", and Breeze offers four near neighbours: `insert-text`
    // is the same I-beam with a PLUS, which reads as ADD and would have
    // collided with the drop verb's list-add two boxes to its left;
    // `edit-select-text` is an `A` inside brackets, which names SELECTING text
    // rather than opening an editor over it; `edittext` is a pencil, a verb
    // this roster spells nowhere else. text-field says the one thing the act
    // is: put a caret in a field and type. Its silhouette is shared with
    // nothing on the row — the media triangles, the +/− pair, the crossed eye,
    // the chain, the staff, the marquee arrow, the tab arrows and the four
    // chevrons are all closed or diagonal shapes, and this is the row's only
    // upright bar.
    //
    // THE ENUMERATOR KEEPS THE BREEZE FILE NAME while the product act it faces
    // is EDIT FLAG: icon ids name their PROVENANCE, not the product verb (the
    // theme-provenance rule), so this is TextField and not IconEditFlag.
    TextField,           // Edit flag (Return)

    // THE FOLDER OVERLAY'S TWO ROW GLYPHS (2026-08-28, the render player):
    // places/22/folder, the Breeze dark folder every file picker on the
    // architect's desktop paints beside a folder row (pcmanfm-qt and
    // kdenlive's Open dialog alike), and mimetypes/22/audio-x-wav, the glyph
    // pcmanfm-qt paints beside a wav — a bracket-shaped double note in
    // Breeze's own #44aaeb (the file's literal fill; audio-x-generic stays
    // the program's logo alone, the architect's ruling). Both are painted by
    // folder_overlay rows (paint_handler.cpp) and by nothing else; the names
    // are their Breeze file names, the theme-provenance rule.
    Folder,              // a folder row (a batch folder at the player's root,
                         // a project row in the Open project picker)
    AudioXWav,           // a wav row

    // THE RENDER PLAYER'S REPEAT TOGGLE (2026-08-28, architect R30): Breeze's
    // actions/22/media-repeat-single — the loop with a "1" — worn in BOTH
    // states by the modal row's one lamp, "a plain toggle: off is the
    // unpressed face, on is the pressed/lit face", so the glyph never
    // changes and the LAMP carries the state (the trim region toggle's own
    // precedent; the text names the toggle since 2026-09-01, the rule at
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
    // the X at its right, three fresh verbatim transcriptions from the
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
    // WINDOW-CLOSE HAS A SECOND READER SINCE 2026-09-01 — the render player's
    // modal row wears it on CLOSE, the architect having ruled the row's last
    // two word buttons into glyphs ("Close should then get a glyph also, to
    // avoid being the odd one out: window-close.svg") — which needed no new
    // entry and no new transcription: a def is a GLYPH and several buttons are
    // free to wear one, GoUp's own precedent above. `kIconCount` is unmoved.
    DialogInformation,   // a NORMAL card's glyph
    DialogError,         // a CRITICAL card's glyph
    WindowClose,         // the X: a card's dismiss, and the player's Close

    // THE COPY VALUE BUTTON'S GLYPH (2026-08-29, the bottom row's verb group):
    // Breeze's actions/22/edit-copy, the two stacked sheets — the same file
    // this roster carried for the ICONCOPY button from 2026-08-12 until the
    // 2026-08-20 propagate relocation deleted that button, transcribed FRESH
    // here rather than recovered (the def and the asset had gone with the
    // consumer). It says "take this value with you", which is what bare `j`
    // does with the focused marker's resolved tempo.
    EditCopy,            // Copy resolved value (the bottom row's verb group)

};

// Roster size, for the once-per-icon diagnostic latch in draw(). Keep it equal
// to the enumerator count above; a mismatch only costs that icon its latch (the
// latch is bounds-checked), never correctness.
// 51 SINCE 2026-09-22's walk-group change, re-counted off the enumerators
// above: bboxprev left with the Previous marker button and go-previous-context
// / go-next-context joined with the two hold-column nudges. It was 50 earlier
// that day, re-counted rather than adjusted: zoom-out-y came back with the Ignore Waveform Magnification lamp
// that wears it (architect 2026-09-22), and merge had joined on 2026-09-19
// with the Flatten button without this number moving, so the count was 49
// against a stated 48 until the recount. It was 48 from 2026-09-17 evening,
// when waveform magnification became a function of the audio view and
// zoom-out-y left with the `]` lamp that wore it — enumerator, def and asset
// together — having been 49 for that one day. It was
// 48 from 2026-09-16, THE MEASURES FEATURE'S DELETION (architect
// 2026-09-16): minuet-scales left with the Marker Measure button that wore
// it — enumerator, def and asset together. It was 49 from 2026-09-15, the
// per-marker magnification's retirement, when zoom-in-y left with the
// Magnification button the same way. It was 50 earlier that day, after the
// two view lamps' deletion, when document-import and chronometer-start left
// with the two lamp buttons that wore them, the edit-cut precedent. The
// count's succession is in git history; a glyph joining or
// leaving restates this number.
inline constexpr int kIconCount = 51;

// Draw `icon` with its viewBox mapped onto the square (x, y, size_px, size_px),
// filling each of its paths in that path's OWN color (the colors are the SVGs'
// and are hard-coded per the redesign color ruling — see the table).
//
// Uniform scale, no aspect fitting: every icon here is square by construction
// (viewBox 0 0 22 22). Cairo state is saved and restored; the caller's source,
// path and matrix survive untouched.
//
// THE DISABLED FACE'S DIMMING rides the last two arguments, through the shared
// mix_color owner (render.h): each path's own color is RETAINED by `keep_own`
// and made up with `mixed_with`, so a greyed-out button's icon keeps its shape
// and loses its life. It is a MIX, not an alpha — the redesign composites
// nothing and every color that reaches cairo is opaque — and it is per-path,
// which is what lets a multi-colored icon (media-record's red beside a white
// glyph) dim as one object without either part being special-cased.
// keep_own == 1 (the default, which every enabled caller takes) leaves the
// table's colors bit-identical.
//
// A malformed `d` string is a PROGRAMMING ERROR, not a runtime state — the
// strings are in-tree constants — so a parse failure emits one stderr line and
// draws nothing. That is deliberately all the machinery there is: a silent
// fallback would hide a transcription typo forever.
void draw(cairo_t* cr, Icon icon, double x, double y, double size_px,
          double keep_own = 1.0,
          GuiColor mixed_with = GuiColor{0.0, 0.0, 0.0});

} // namespace icons
