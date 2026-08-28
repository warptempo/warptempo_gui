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
// ONE FILE DEPARTS FROM THE VERBATIM RULE and it is stated here as well as at
// its table entry, because the rule is what this header promises:
// tool-rect-selection's geometry is a `<rect>` element, not a `<path>`, so
// there is no `d` in the file to copy and its row holds a four-number
// derivation instead. A `<rect>` parser was declined for one file.
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
    // THE FOUR VIEW RADIOS' FACES (architect-picked 2026-08-11 off a rendered
    // candidate sheet): the S/T audio pair and the W/P marker pair wore shaped
    // LETTER GLYPHS from the row's first day until then — the row's only
    // non-icon buttons, and the reason the architect briefly ruled the radios
    // deleted altogether ("ugly letter blips"); he reversed that the same day
    // and gave them real glyphs instead, which is what killed the letter arm
    // (no producer left; the painter's shaped-letter branch went with it).
    //   SOURCE / TARGET are document-export / document-import, the arrow
    //   LEAVING a document and ENTERING one — his own metaphor, the source
    //   being where the audio comes FROM.
    //   WARP is speedometer, the gauge with the needle: warping IS a speed
    //   change, and "change speed" is kdenlive's own word for it. It was his
    //   FIRST pick, reversed to distortionfx (the spiral, "time bends") in the
    //   same breath, and RESTORED at his second look later the same day, which
    //   leaves distortionfx the recorded runner-up beside player-time.
    //   PHASE RESET is chronometer-start, the stopwatch with the solid play
    //   triangle in its dial: start the clock anew. Picked over
    //   chronometer-reset and view-refresh — indistinguishable from each other
    //   at row size, and chronometer-reset's dial does not survive the
    //   rendering — and over the bare chronometer.
    DocumentExport,      // Source audio view (bare `t`)
    DocumentImport,      // Target audio view (bare `t`)
    Speedometer,         // Warp markers (bare `p`)
    ChronometerStart,    // Phase reset markers (bare `p`)
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
    ToolRectSelection,   // Show trim region (bare `[`)
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
    ViewHidden,          // Disable markers (`Ctrl+D`)
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
    PreviewRenderOn,     // Listen to a render
    DialogOkApply,       // Load a render in place as the baseline
    VcsDiff,             // The history mode (`h`)
    // Breeze's deep-history — a clock face with a curl-back arrow sweeping
    // around it. THE SET'S ONE TWO-COLOR ICON, knowingly: the dial is
    // .ColorScheme-Text and the arrow .ColorScheme-Accent, so the arrow reads
    // as the act over what it sweeps. The table's entry records both resolved
    // literals, the command-coverage check and the four glyphs tried before it.
    //
    // IT DRESSED THE CUMULATIVE READING'S TOGGLE from 2026-08-09 (architect's
    // pick then) until 2026-08-18, when that toggle took the summation sigma
    // and this glyph was FREED for the history view's Git walk radio — a
    // committed history is what the deep clock sweeps. Nothing paints it in
    // the meantime, which is expected and is why the entry stays.
    DeepHistory,               // The Git (committed) walk
    // ITS SHALLOW SIBLING (2026-08-18), for the history view's Session walk
    // radio: the same clock face with NO sweep arrow — the session's own
    // undo/redo timeline, which reaches back no further than this run.
    ShallowHistory,            // The Session (local) walk
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
    // selected diff flags applied backwards into the live state (`Ctrl+H`).
    // Breeze's own document-revert — a page with an arrow curving back into it,
    // which is the act — and the FIRST committed file whose `d` uses the smooth
    // cubic (`s`), the one command the interpreter grew for it.
    DocumentRevert,      // Revert the selected differences (`Ctrl+H`)
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
    MediaPlaybackStop,   // Stop (bare Space, the face while an audition runs)
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
    // the Edit flag button and Add to Selection (between Toggle inherit and Add
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
    TextField,           // Edit flag (Enter)

    // THE FOLDER OVERLAY'S TWO ROW GLYPHS (2026-08-28, the render player):
    // places/22/folder, the Breeze dark folder every file picker on the
    // architect's desktop paints beside a folder row (pcmanfm-qt and
    // kdenlive's Open dialog alike), and mimetypes/22/audio-x-wav, the glyph
    // pcmanfm-qt paints beside a wav — a bracket-shaped double note in
    // Breeze's own #44aaeb (the file's literal fill; audio-x-generic stays
    // the program's logo alone, the architect's ruling). Both are painted by
    // folder_overlay rows (paint_handler.cpp) and by nothing else; the names
    // are their Breeze file names, the theme-provenance rule.
    Folder,              // a folder row (render / tmp / a batch / `..`)
    AudioXWav,           // a wav row
};

// Roster size, for the once-per-icon diagnostic latch in draw(). Keep it equal
// to the enumerator count above; a mismatch only costs that icon its latch (the
// latch is bounds-checked), never correctness.
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
inline constexpr int kIconCount = 49;

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
