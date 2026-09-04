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
//     button hours later, and comes back with a real producer plus the one
//     thing distortionfx never needed, a per-path LINE CAP);
//   - the STROKE'S OWN WIDTH AND DASH (tool-rect-selection, 2026-08-16), which
//     is a widening of the third rather than a fourth kind of thing: the pen
//     had been hard-coded to boost's implicit defaults, and the marching-ants
//     rectangle is the first file to state either attribute.
// Each is described where it is implemented (icons.cpp's table header and its
// `d`-interpreter header). A general per-path MATRIX was grown alongside the
// stroke for distortionfx and did NOT come back with it — neither stroked file
// carries a transform — so that one is still git history alone.
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
    // redesign_button_icon, its hint the tooltip override's "Save and Commit"
    // / "Committing the checkpoint"; the mode is AppState::HistoryMode). It
    // was RENDER's second face until 2026-08-08, when the act moved onto the
    // save chord; the WORDS moved off the button whole when row 2's labeled
    // faces died at the 2026-08-12 relayout — the glyph swap says it now.
    VcsCommit,           // Save, in the history view and while publishing
    // Row 4, the icon row. (ZoomOut / ZoomIn lived here 2026-08-01..08-02, for
    // the icon row's zoom pair; both went with those buttons.)
    //
    // THE TWO VIEW LAMPS' FACES (architect-picked 2026-08-11 off a rendered
    // candidate sheet, for the four radios these two buttons replaced): the S/T
    // audio pair and the W/P marker pair wore shaped LETTER GLYPHS from the
    // row's first day until then — the row's only non-icon buttons, and the
    // reason the architect briefly ruled the radios deleted altogether ("ugly
    // letter blips"); he reversed that the same day and gave them real glyphs
    // instead, which is what killed the letter arm (no producer left; the
    // painter's shaped-letter branch went with it).
    //   TARGET is document-import, the arrow ENTERING a document. Its Source
    //   partner was document-export, the arrow LEAVING one — his own metaphor,
    //   the source being where the audio comes FROM.
    //   PHASE RESET is chronometer-start, the stopwatch with the solid play
    //   triangle in its dial: start the clock anew. Picked over
    //   chronometer-reset and view-refresh — indistinguishable from each other
    //   at row size, and chronometer-reset's dial does not survive the
    //   rendering — and over the bare chronometer. Its Warp partner was
    //   speedometer, the gauge with the needle: warping IS a speed change, and
    //   "change speed" is kdenlive's own word for it. That was his FIRST pick,
    //   reversed to distortionfx (the spiral, "time bends") in the same breath
    //   and RESTORED at his second look later the same day, which leaves
    //   distortionfx the recorded runner-up beside player-time.
    // TWO OF THE FOUR LEFT ON 2026-09-04, when the architect collapsed the
    // row's three radio pairs into three lamps: a lamp wears the LIT state's
    // glyph, so document-export and speedometer went with the Source and Warp
    // halves — enumerators, defs and assets together, the edit-cut precedent.
    // Their picks and runners-up stay recorded above so none is re-proposed
    // without a new ruling.
    DocumentImport,      // The audio-view lamp, lit in Target (bare `t`)
    ChronometerStart,    // The marker-column lamp, lit in Phase Reset (bare `p`)
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
    // THE ZOOM GROUP'S FOUR (architect-picked 2026-08-12, the grand relayout's
    // roster commit — the icon row's zoom group after the trim group).
    // Breeze's own magnifier family, one construction four ways: the bare
    // magnifier with a plus (zoom in), a minus (zoom out), the fit frame
    // (full zoom out — bare `0`'s whole-song arm), and the 1:1 original
    // (working-zoom center, bare `c`). The 2026-08-01 ZoomOut/ZoomIn pair was
    // deleted with its buttons under the no-duplicate-commands ruling of
    // 2026-08-02; the architect's 2026-08-12 relayout ruling supersedes that
    // for these four (the roster's record at kIconRowButtons), so the two
    // magnifiers return as fresh transcriptions beside two new siblings.
    ZoomIn,              // Zoom in (Ctrl+`=`)
    ZoomOut,             // Zoom out (Ctrl+`-`)
    ZoomFitBest,         // Full zoom out / overview (bare `0`)
    ZoomOriginal,        // Working-zoom center (bare `c`)
    // THE WAVEFORM MAGNIFICATION'S TWO FACES (2026-08-26), the zoom group's
    // last buttons: Breeze's zoom-in-y / zoom-out-y — the
    // SAME magnifier construction as the four above, each carrying a Y-AXIS
    // MARK: a ruler of tick marks down the left of the lens with the plus or
    // the minus in the dial. The
    // axis marks are the whole reason these are the right pick: the act IS a
    // zoom, of the amplitude axis rather than the time axis, and the mark is
    // what tells the pair apart from their four horizontal neighbours at row
    // size. Both are single `.ColorScheme-Text` paths in the family's own
    // idiom, so the interpreter was asked for nothing new.
    //
    // ONE OF THE TWO IS A SYMLINK IN THE INSTALLED THEME — zoom-in-y.svg
    // points at y-zoom-in.svg, byte-identical — and the committed asset is the
    // resolved bytes, exactly as edit-comment's was before it; the other is a
    // real file. The names here are the PROVENANCE names (the
    // theme-provenance rule), while the product act they face is the waveform
    // magnification.
    //
    // (ZOOMFITHEIGHT WAS A THIRD FACE FOR ONE DAY — breeze's zoom-fit-height,
    // the full-length handle with a bracketed pair of rules across the lens,
    // worn by the MAGNIFICATION RESET from 2026-08-26 to 2026-08-27. Its def
    // and its asset are deleted with the button, no button being left that
    // wears them — edit-comment's own precedent.)
    ZoomInY,             // Magnify waveform (bare `=`)
    ZoomOutY,            // Reduce waveform (bare `-`)
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
    // (EDITCOPY AND EDITPASTE ARE DELETED — 2026-08-20, with their buttons:
    // the architect's propagate relocation gave all five propagate commands the
    // EDIT MENU as their one pointer home, and neither glyph had a second
    // consumer. Breeze's edit-copy and edit-paste, the two-sheets and the
    // clipboard, transcribed from the shipped SVGs; the provenance files went
    // with them, the trim scissors' own precedent. A menu ROW carries text and
    // an accelerator, never a glyph, so nothing replaced them.)
    // (MUSIC-NOTE-16TH AND MATHMODE ARE DELETED — 2026-08-27, with their
    // buttons: the architect's Series relocation gave the BPM opener (bare
    // `m`) and iteration mode (bare `i`) the new SERIES MENU as their one
    // pointer home, and neither glyph had a second consumer. Breeze's
    // music-note-16th (the flagged quaver) and mathmode (an italic f beside a
    // multiplication cross, f(x)), transcribed from the shipped SVGs; the
    // provenance files went with them, edit-copy and edit-paste's own
    // precedent seven days earlier. A menu ROW carries text and an
    // accelerator, never a glyph, so nothing replaced them. Mathmode had held
    // iteration mode's slot only since 2026-08-18, when the SUMMATION SIGMA
    // below left it for the cumulative reading.)
    // THE SUMMATION SIGMA, ON THE CUMULATIVE READING SINCE 2026-08-18
    // (architect): a cumulative delta is a SUM over the walk's members, read
    // against the iterative reading's one step at a time — the Σ says exactly
    // that, which deep-history's swept clock only implied. It dressed
    // ITERATION MODE from 2026-08-01 until this move (an iteration sweep is
    // also a sum over cells), and that slot took mathmode, which is deleted
    // with the button on 2026-08-27.
    BlackSum,            // The cumulative reading (`u`)
    GoJump,              // Follow mode
    // Breeze's align-horizontal-center (2026-08-31, R11): two boxes riding
    // one vertical center line — exactly a viewport holding its subject at
    // the center column. A fresh verbatim transcription for the `y` centered
    // lamp beside Follow.
    AlignHorizontalCenter,  // Toggle centered viewport (`y`)
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
    // arrows): previous marker (Shift+Tab), next marker (Tab) and walk both
    // tabs (Ctrl+Shift+Tab). HIS OWN REASONS, kept because they are about this
    // row's crowding rather than about the glyphs in isolation:
    //   bbox-prev / bbox-next are AN ARROW MEETING A BAR, which is the Tab
    //   key's own shape — and they share no silhouette with the chevrons two
    //   slots away (the cardinal arrows), the media-skip triangles at the
    //   row's left, or the keyframe dials the history walk wears in the same
    //   cluster inside the `h` view.
    //   boost is A TWO-ARROW CYCLE, which is literally what walking both tabs
    //   is: step one, step the other, come back round.
    // boost IS SINGLE-COLOUR (checked at the transcription, because its
    // sibling boost-boosted is not): all four of its paths are
    // `.ColorScheme-Text` = #fcfcfc, and the green `.ColorScheme-PositiveText`
    // tick belongs to boost-boosted ALONE — so deep-history is still the
    // set's one two-colour glyph. It is, however, the set's ONE STROKED file
    // (fill="none" stroke="currentColor" on its group), which is what brought
    // the interpreter's stroked arm back; the record is at the table entry.
    BboxPrev,            // Previous marker (Shift+Tab)
    BboxNext,            // Next marker (Tab)
    Boost,               // Walk both tabs (Ctrl+Shift+Tab)
    // THE MARKER MEASURE BUTTON'S GLYPH, the bottom row's verb group between
    // the Edit flag button and Add to selection (between Toggle inherit and Add
    // to Selection until 2026-08-27): minuet-scales, KDE Minuet's own
    // icon — three note heads climbing a five-line staff.
    //
    // IT REPLACED edit-comment ON 2026-08-20 (architect), and the swap is about
    // what the field IS rather than about taste: the balloon was picked on
    // 2026-08-19 for a FREE-TEXT COMMENT and stopped being true the next day,
    // when the field was rebranded into a strict measure grammar. A button that
    // names a place in the SCORE wants staff lines, not speech. edit-comment's
    // enumerator, its def and its committed asset all went with the swap, no
    // button being left that wore it.
    //
    // THE ENUMERATOR KEEPS THE BREEZE FILE NAME while the product act it faces
    // is the MARKER MEASURE: icon ids name their PROVENANCE, not the product
    // verb (the theme-provenance rule), so this is MinuetScales and not
    // IconMeasure. Unlike its predecessor the file is a REAL one rather than a
    // symlink, so the committed asset is the installed breeze-dark bytes
    // verbatim — the table entry carries that record.
    MinuetScales,        // Measure (bare `/`)
    // THE EDIT FLAG BUTTON'S GLYPH (2026-08-27), the bottom row's verb group
    // between Toggle inherit and the Measure: text-field, Breeze's own
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
    // theme-provenance rule), so this is TextField and not IconEditFlag —
    // MinuetScales' own precedent, one entry above.
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
// 51 SINCE 2026-08-28, THE PLAYER ROW'S PAUSE FACE: 50 + media-playback-pause,
// one fresh transcription for R36's Play/Pause + Stop pair — two `m` subpaths
// of m/v/h/z in one `d`, the arms media-playback-stop and the two skips
// already cover.
// 50 SINCE 2026-08-28, THE PLAYER ROW'S REPEAT TOGGLE: 49 + media-repeat-
// single, one fresh transcription — three `<path>` elements with verbatim
// relative `d` strings, nothing new for the interpreter, and the file's one
// `fill-rule="evenodd"` needs no field (the entry says why).
// 49 SINCE 2026-08-28, THE FOLDER OVERLAY: 47 + folder and audio-x-wav, the
// render player's two row glyphs — two fresh transcriptions; audio-x-wav is
// the table's first path whose file wraps it in a group transform and its
// own inverse (a net identity, recorded at the entry), and its arcs take the
// interpreter's existing `A` arm.
// 47 SINCE 2026-08-27, THE EDIT FLAG BUTTON: 46 + text-field, one fresh
// transcription with no departure beside it — the bottom row's new verb needed
// a glyph and no existing one said "open the editor over this text".
// 46 SINCE 2026-08-27, THE KEYBOARD'S CAPS BECOMING WORDS: 51 − the on-screen
// keyboard's five (keyboard-caps-disabled / keyboard-caps-enabled /
// keyboard-enter / keyboard-spacebar / edit-clear-locationbar-rtl). The
// architect drove the painted keyboard on glass and the Breeze glyphs read
// OVERSIZED beside the letter caps, so every function key says its word on the
// one sans face instead — Shift, Backspace, Space, Cancel, Enter — and all
// five glyphs lost their only consumer in one act. Their defs and their
// committed assets went with them; DialogCancel is untouched, Render's
// mid-render face being its own reader. They lived one day.
// 51 since 2026-08-27, THE SERIES RELOCATION (the day's third move here):
// 53 − music-note-16th and mathmode, deleted with the two icon-row buttons the
// SERIES MENU replaced. Both commands survive whole on their bare keys and as
// menu rows; the glyphs had no other consumer, exactly as edit-copy and
// edit-paste had none when the EDIT MENU took their buttons.
// 53 since 2026-08-27: 48 + the ON-SCREEN KEYBOARD's five (keyboard-caps-
// disabled / keyboard-caps-enabled / keyboard-enter / keyboard-spacebar /
// edit-clear-locationbar-rtl). Five fresh transcriptions, no departures beside
// them — every one a `<path>` with a verbatim `d`, and four of the five are
// single M/L/H/V/Z outlines the interpreter's oldest arms already cover.
// 48 since 2026-08-27: zoom-fit-height is deleted with the MAGNIFICATION
// RESET button, the third zoom-group member the architect retired when the
// magnification pair moved onto the bare keys — 49 − 1, the glyph having had
// no second consumer.
// 49 since 2026-08-26 (later the same day): zoom-fit-height joined for the
// MAGNIFICATION RESET, the third zoom-group member the ladder's 2026-08-26
// retune brought with it — 48 + 1, one more fresh transcription in the same
// family.
// 48 since 2026-08-26: zoom-in-y and zoom-out-y joined for the waveform
// magnification stepping pair, the icon row's two new zoom-group members —
// 46 + 2, two fresh transcriptions with no departures beside them.
// 46 STILL, LATER ON 2026-08-20: the Marker Measure button's glyph changed from
// edit-comment to minuet-scales (the architect's pick, once the field became a
// measure grammar) — one out, one in, so the count is unmoved. edit-comment's
// def and asset are deleted with it, no button being left that wears them.
// 46 since 2026-08-20, the propagate relocation: 48 − edit-copy and edit-paste,
// deleted with the two icon-row buttons the EDIT MENU replaced. The five
// commands survive whole on their chords and as menu rows; the glyphs had no
// other consumer, exactly as edit-cut had none when the scissors went.
// 48 since 2026-08-19: 47 + edit-comment, the Marker Measure button's speech
// balloon (the bottom row's verb group).
// 47 since 2026-08-18, the roster relayout: 45 − edit-cut (the trim scissors,
// deleted with its button — the chord survives, the glyph had no other
// consumer) + shallow-history, mathmode and edit-select. TWO OF THE THREE
// ARRIVE AHEAD OF THEIR BUTTONS (shallow-history's walk radio and
// edit-select's add-to-selection button land in the following commits) and
// deep-history is left painted by nothing for the same span — an unreferenced
// enumerator costs a latch slot and nothing else, and one asset commit beats
// three.
// 45 since 2026-08-16: 44 + tool-rect-selection, the Show trim region
// button's marching-ants rectangle (the icon row's trim group, beside the
// scissors).
// 44 since 2026-08-15 (the bottom row's marker-walk group): 41 + bboxprev,
// bboxnext and boost. THE PLAY/STOP COLLAPSE OF THE SAME DAY COST NOTHING
// HERE — one button wearing two glyphs needs both of them, and an Icon has
// never been a button (VcsCommit is the precedent). 41 was 2026-08-12 (the
// grand relayout's roster commit): 33 + the zoom group's four (zoom-in /
// zoom-out / zoom-fit-best / zoom-original) + the single-marker verbs' four
// (list-add / list-remove / view-hidden / insert-link). 33 was 32 + edit-cut
// (2026-08-11, the trim surface arc).
// 55 SINCE 2026-08-29, THE COPY VALUE BUTTON: 54 + edit-copy, one fresh
// verbatim transcription with no departure beside it — absolute M/L/Z over
// three subpaths whose windings alternate, so cairo's default nonzero fill
// (SVG's own default, the file naming no fill-rule) cuts the two sheets'
// interiors out exactly as the file draws them. It is the same Breeze file
// the deleted IconCopy button wore from 2026-08-12 to 2026-08-20, back for a
// different act.
// 54 since 2026-08-29, THE NOTIFICATION CARDS: 51 + dialog-information,
// dialog-error and window-close — the card's two class glyphs and its X,
// three fresh verbatim transcriptions with no departures beside them (the
// two dialog files are two-colour, media-record's shape).
// 54 SINCE 2026-09-04, THE RADIO-PAIR COLLAPSE: 57 − document-export,
// speedometer and deep-history, deleted with the six buttons the architect
// collapsed into three lamps. Each lamp wears the LIT state's glyph, so
// document-import, chronometer-start and shallow-history stay and the three
// home-state faces go — enumerators, defs and assets together, which is
// edit-cut's precedent and the largest single departure this roster has taken.
// The picks, the metaphors and every runner-up stay recorded at the surviving
// entries so none is re-proposed without a new ruling.
// 57 SINCE 2026-09-01, THE PLAYER ROW'S UP BUTTON: 56 + go-parent-folder, one
// fresh verbatim transcription with no departure beside it — absolute M/L over
// two `z`-closed subpaths with an explicit command letter before every pair,
// the interpreter's oldest arm. The button had worn the roster's `go-up` since
// it landed hours earlier the same day; GoUp stays for the roster's own bare-Up
// transport button, so this is an addition rather than a swap.
// 56 was ALREADY TRUE ON 2026-08-31 and this constant said 55: the CENTERED
// LAMP's align-horizontal-center added its enumerator without bumping the
// number, so the roster's last entry (edit-copy) ran latch-less for a day. The
// count above is a re-COUNT of the enumerators rather than an increment of the
// stale value, per the inventory-retell rule; the drift cost exactly what this
// header says it costs — one icon's once-per-icon diagnostic latch, the latch
// being bounds-checked — and nothing else.
inline constexpr int kIconCount = 54;

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
