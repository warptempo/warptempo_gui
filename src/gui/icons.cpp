#include "icons.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace icons {
namespace {

// -- The icon table ---------------------------------------------------------
//
// ONE ROW PER FILE under assets/icons/warptempo/ (the product's own set,
// architect 2026-10-06; icons.h's head), each holding that file's `<path>`
// elements IN FILE ORDER: `d` copied VERBATIM and `ink` the path's own
// `fill`, so a diff between this table and the file is a transcription bug
// and nothing else. Every file is viewBox 0 0 16 16 (kIconViewBox), fills
// only, no transform, no stroke, no group: a row is its paths and nothing
// more. THE FIVE SHARED DRAWINGS (the README's list) are one row each, worn
// by both their enumerators through icon_def below: the two files of a pair
// are byte-identical.
//
// A PATH IS FILLED WHOLE with cairo's default NONZERO winding rule — the SVG
// default too — so a subpath wound against its outline is a hole (the
// author's rings, DocumentSave's frame), and the paths are LAYERED: each is
// filled over the ones before it, a silhouette first and its insets on top,
// which is how the drawings are built.
inline constexpr double kIconViewBox = 16.0;

// THE INKS (architect 2026-10-06): EVERY PATH WEARS ITS OWN FILL, black
// included — none takes a theme role, so a glyph is the same period pixel
// art on every theme, as Windows' own toolbar bitmaps were (a disabled glyph
// is the emboss, draw_engraved, which is where the theme's roles enter). The
// fills are Windows' twenty always-solid colours (theme_file.h's
// kNamedThemeColours, the same names), the thirteen of them the set uses;
// a literal here is a file's value, never a judgment. WHITE AND SILVER are
// the disabled mask's two background inks (Windows' white and button face;
// draw_engraved): every other ink is the mask's ink.
constexpr GuiColor kIconBlack  = hex(0x000000);
constexpr GuiColor kIconMaroon = hex(0x800000);
constexpr GuiColor kIconGreen  = hex(0x008000);
constexpr GuiColor kIconOlive  = hex(0x808000);
constexpr GuiColor kIconNavy   = hex(0x000080);
constexpr GuiColor kIconTeal   = hex(0x008080);
constexpr GuiColor kIconSilver = hex(0xC0C0C0);
constexpr GuiColor kIconGray   = hex(0x808080);
constexpr GuiColor kIconRed    = hex(0xFF0000);
constexpr GuiColor kIconYellow = hex(0xFFFF00);
constexpr GuiColor kIconBlue   = hex(0x0000FF);
constexpr GuiColor kIconAqua   = hex(0x00FFFF);
constexpr GuiColor kIconWhite  = hex(0xFFFFFF);

struct IconPath {
    GuiColor    ink;   // the file's fill
    const char* d;     // the file's d, verbatim
};

struct IconDef {
    const IconPath* paths      = nullptr;
    int             path_count = 0;
};

template <std::size_t N>
constexpr IconDef icon_def_of(const IconPath (&paths)[N]) {
    return IconDef{paths, static_cast<int>(N)};
}

// Save — MICROSOFT.
constexpr IconPath kDocumentSavePaths[] = {
    {kIconBlack, "M1,1 H15 V15 H2 L1,14 Z M2,2 V13.5 L2.5,14 H14 V2 Z"},
    {kIconOlive, "M2,2 H3 V9 H13 V4 H14 V14 H13 V10 H4 V14 H2.5 L2,13.5 Z"},
    {kIconBlack, "M3,2 H4 V8 H12 V2 H13 V9 H3 Z"},
    {kIconBlack, "M13,3 H14 V4 H13 Z"},
    {kIconBlack, "M4,10 H13 V14 H12 V11 H10 V14 H4 Z"},
};

// Undo — MICROSOFT.
constexpr IconPath kEditUndoPaths[] = {
    {kIconNavy, "M2,4.5 V9.5 H7 Z"},
    {kIconNavy,
     "M2.9,7.958 C4.795,6.26 7.089,4.541 9.659,4.09 C11.199,3.82 12.93,4.19 "
     "13.658,5.716 C14.441,7.358 13.79,9.352 12.991,10.855 L12.109,10.385 "
     "C12.749,9.181 13.393,7.484 12.755,6.146 C12.227,5.039 10.92,4.884 "
     "9.832,5.075 C7.452,5.493 5.318,7.134 3.567,8.703 Z"},
};

// Redo — MICROSOFT.
constexpr IconPath kEditRedoPaths[] = {
    {kIconNavy, "M 14,4.5 V 9.5 H 9 Z"},
    {kIconNavy,
     "M 13.1,7.958 C 11.205,6.26 8.911,4.541 6.341,4.09 C 4.801,3.82 "
     "3.07,4.19 2.342,5.716 C 1.559,7.358 2.21,9.352 3.009,10.855 L "
     "3.891,10.385 C 3.251,9.181 2.607,7.484 3.245,6.146 C 3.773,5.039 "
     "5.08,4.884 6.168,5.075 C 8.548,5.493 10.682,7.134 12.433,8.703 Z"},
};

// Render — MICROSOFT.
constexpr IconPath kMediaRecordPaths[] = {
    {kIconMaroon, "M8,3 A5,5 0 0 1 8,13 A5,5 0 0 1 8,3 Z"},
};

// Save and Commit (Save's face in the history view) — ORIGINAL.
constexpr IconPath kVcsCommitPaths[] = {
    {kIconBlack,
     "M7.5,0 H8.5 V16 H7.5 Z M8,4.5 A3.5,3.5 0 0 1 8,11.5 A3.5,3.5 0 0 1 "
     "8,4.5 Z M8,5.5 A2.5,2.5 0 0 0 8,10.5 A2.5,2.5 0 0 0 8,5.5 Z"},
    {kIconWhite, "M8,5.5 A2.5,2.5 0 0 1 8,10.5 A2.5,2.5 0 0 1 8,5.5 Z"},
};

// Pull (Save's face while GitHub is ahead) — CHICAGO95.
constexpr IconPath kVcsPullPaths[] = {
    {kIconBlack, "M5,0 H10 V6 H14 V6.5 L7.5,13 L1,6.5 V6 H5 Z"},
    {kIconAqua, "M6,1 H9 V7 H12 L7.5,11.5 L3,7 H6 Z"},
    {kIconBlack, "M1,13 H14 V16 H1 Z"},
    {kIconAqua, "M2,14 H13 V15 H2 Z"},
};

// Source+Warp — CHICAGO95.
constexpr IconPath kDocumentExportPaths[] = {
    {kIconBlack, "M1,0 H14 V15 H1 Z"},
    {kIconWhite, "M2,1 H13 V14 H2 Z"},
    {kIconNavy, "M3,3 H12 V4 H3 Z"},
    {kIconMaroon, "M5,7 H9 V8 H5 Z M5,5 H6 V8 H5 Z M12,7.5 L9,5 L9,10 Z"},
};

// Target+Warp — CHICAGO95.
constexpr IconPath kDocumentImportPaths[] = {
    {kIconBlack, "M1,0 H14 V15 H1 Z"},
    {kIconWhite, "M2,1 H13 V14 H2 Z"},
    {kIconNavy, "M3,3 H12 V4 H3 Z"},
    {kIconMaroon, "M8,6 H12 V7 H8 Z M11,6 H12 V9 H11 Z M5,6.5 L8,4 L8,9 Z"},
};

// Target+Phase — ORIGINAL.
constexpr IconPath kChronometerStartPaths[] = {
    {kIconBlack, "M1,0 H14 V15 H1 Z"},
    {kIconWhite, "M2,1 H13 V14 H2 Z"},
    {kIconNavy, "M3,3 H12 V4 H3 Z"},
    {kIconMaroon,
     "M4,6 H5 V12 H4 Z M8,8.5 H12 V9.5 H8 Z M5,9 L8,6.5 L8,11.5 Z"},
};

// Full Zoom Out — CHICAGO95.
constexpr IconPath kZoomFitBestPaths[] = {
    {kIconNavy, "M8.939,11.061 L13.539,15.661 L15.661,13.539 L11.061,8.939 Z"},
    {kIconBlack, "M9.646,10.354 L14.246,14.954 L14.954,14.246 L10.354,9.646 Z"},
    {kIconBlack,
     "M6,0 A6,6 0 0 1 6,12 A6,6 0 0 1 6,0 Z M6,1 A5,5 0 0 0 6,11 A5,5 0 0 0 "
     "6,1 Z"},
    {kIconNavy,
     "M3,3 H5 V4 H3 Z M3,3 H4 V5 H3 Z M7,3 H9 V4 H7 Z M8,3 H9 V5 H8 Z M3,8 "
     "H5 V9 H3 Z M3,7 H4 V9 H3 Z M7,8 H9 V9 H7 Z M8,7 H9 V9 H8 Z"},
};

// Center on Focus — CHICAGO95.
constexpr IconPath kZoomOriginalPaths[] = {
    {kIconNavy, "M8.939,11.061 L13.539,15.661 L15.661,13.539 L11.061,8.939 Z"},
    {kIconBlack, "M9.646,10.354 L14.246,14.954 L14.954,14.246 L10.354,9.646 Z"},
    {kIconBlack,
     "M6,0 A6,6 0 0 1 6,12 A6,6 0 0 1 6,0 Z M6,1 A5,5 0 0 0 6,11 A5,5 0 0 0 "
     "6,1 Z"},
    {kIconNavy, "M5,3 H7 V9 H5 Z M4,4 H5 V5 H4 Z M4,8 H8 V9 H4 Z"},
};

// Toggle Waveform Magnification — CHICAGO95.
constexpr IconPath kZoomInYPaths[] = {
    {kIconNavy, "M8.939,11.061 L13.539,15.661 L15.661,13.539 L11.061,8.939 Z"},
    {kIconBlack, "M9.646,10.354 L14.246,14.954 L14.954,14.246 L10.354,9.646 Z"},
    {kIconBlack,
     "M6,0 A6,6 0 0 1 6,12 A6,6 0 0 1 6,0 Z M6,1 A5,5 0 0 0 6,11 A5,5 0 0 0 "
     "6,1 Z"},
    {kIconNavy, "M5,3 H7 V9 H5 Z M3,5 H9 V7 H3 Z"},
};

// Drop Marker — CHICAGO95.
constexpr IconPath kListAddPaths[] = {
    {kIconBlack, "M7,3 H9 V13 H7 Z M3,7 H13 V9 H3 Z"},
};

// Delete Markers — CHICAGO95.
constexpr IconPath kListRemovePaths[] = {
    {kIconBlack, "M3,7 H13 V9 H3 Z"},
};

// Toggle Disabled — CHICAGO95.
constexpr IconPath kViewHiddenPaths[] = {
    {kIconBlack,
     "M8,1 A7,7 0 0 1 8,15 A7,7 0 0 1 8,1 Z M8,2 A6,6 0 0 0 8,14 A6,6 0 0 0 "
     "8,2 Z"},
    {kIconRed, "M8,2 A6,6 0 0 1 8,14 A6,6 0 0 1 8,2 Z"},
    {kIconWhite,
     "M4.422,5.978 L10.022,11.578 L11.578,10.022 L5.978,4.422 Z "
     "M10.022,4.422 L4.422,10.022 L5.978,11.578 L11.578,5.978 Z"},
};

// Toggle Inherit — CHICAGO95.
constexpr IconPath kInsertLinkPaths[] = {
    {kIconBlack,
     "M0,0 H4 A4,4 0 0 1 8,4 V6 A4,4 0 0 1 4,10 H0 Z M0,1 V9 H4 A3,3 0 0 0 "
     "7,6 V4 A3,3 0 0 0 4,1 Z"},
    {kIconSilver,
     "M0,1 H4 A3,3 0 0 1 7,4 V6 A3,3 0 0 1 4,9 H0 Z M0,2 V8 H4 A2,2 0 0 0 "
     "6,6 V4 A2,2 0 0 0 4,2 Z"},
    {kIconBlack,
     "M0,2 H4 A2,2 0 0 1 6,4 V6 A2,2 0 0 1 4,8 H0 Z M0,3 V7 H4 A1,1 0 0 0 "
     "5,6 V4 A1,1 0 0 0 4,3 Z"},
    {kIconBlack,
     "M16,10 H12 A4,4 0 0 1 8,6 V4 A4,4 0 0 1 12,0 H16 Z M16,9 V1 H12 A3,3 "
     "0 0 0 9,4 V6 A3,3 0 0 0 12,9 Z"},
    {kIconSilver,
     "M16,9 H12 A3,3 0 0 1 9,6 V4 A3,3 0 0 1 12,1 H16 Z M16,8 V2 H12 A2,2 0 "
     "0 0 10,4 V6 A2,2 0 0 0 12,8 Z"},
    {kIconBlack,
     "M16,8 H12 A2,2 0 0 1 10,6 V4 A2,2 0 0 1 12,2 H16 Z M16,7 V3 H12 A1,1 "
     "0 0 0 11,4 V6 A1,1 0 0 0 12,7 Z"},
    {kIconBlack,
     "M6,2 H13 A3,3 0 0 1 16,5 V5 A3,3 0 0 1 13,8 H6 A3,3 0 0 1 3,5 V5 A3,3 "
     "0 0 1 6,2 Z M6,3 A2,2 0 0 0 4,5 V5 A2,2 0 0 0 6,7 H13 A2,2 0 0 0 15,5 "
     "V5 A2,2 0 0 0 13,3 Z"},
    {kIconSilver,
     "M6,3 H13 A2,2 0 0 1 15,5 V5 A2,2 0 0 1 13,7 H6 A2,2 0 0 1 4,5 V5 A2,2 "
     "0 0 1 6,3 Z M6,4 A1,1 0 0 0 5,5 V5 A1,1 0 0 0 6,6 H13 A1,1 0 0 0 14,5 "
     "V5 A1,1 0 0 0 13,4 Z"},
    {kIconBlack,
     "M6,4 H13 A1,1 0 0 1 14,5 V5 A1,1 0 0 1 13,6 H6 A1,1 0 0 1 5,5 V5 A1,1 "
     "0 0 1 6,4 Z M6.4,4.6 A0.4,0.4 0 0 0 6,5 V5 A0.4,0.4 0 0 0 6.4,5.4 "
     "H12.6 A0.4,0.4 0 0 0 13,5 V5 A0.4,0.4 0 0 0 12.6,4.6 Z"},
    {kIconMaroon, "M12,10 H13 V13 H12 Z M10,13 L15,13 L12.5,16 Z"},
};

// Flatten — ORIGINAL.
constexpr IconPath kMergePaths[] = {
    {kIconBlack,
     "M1,3.5 L7.5,3.5 L7.5,12.5 L1,12.5 L1,11.5 L6.5,11.5 L6.5,4.5 L1,4.5 Z "
     "M7,7.5 L15,7.5 L15,8.5 L7,8.5 Z M11,5 L15,8 L11,11 Z"},
};

// Toggle Cumulative — ORIGINAL.
constexpr IconPath kBlackSumPaths[] = {
    {kIconBlack,
     "M3,2 L13,2 L13,3 L3,3 Z M3,12 L13,12 L13,13 L3,13 Z M3.743,2.236 "
     "L9.329,7.5 L3.743,12.764 L3.057,12.036 L7.871,7.5 L3.057,2.964 Z"},
};

// Toggle Follow — ORIGINAL.
constexpr IconPath kGoJumpPaths[] = {
    {kIconBlack, "M1,0 H10 L13,3 V16 H1 Z"},
    {kIconWhite, "M2,1 H9 V4 H12 V15 H2 Z"},
    {kIconWhite, "M10,1.5 L11.5,3 H10 Z"},
    {kIconGreen, "M3,7 H8 V9 H3 Z M8,4.5 L12,8 L8,11.5 Z"},
};

// Toggle Restrict Undo — CHICAGO95.
constexpr IconPath kTimelineLiftPaths[] = {
    {kIconGray, "M2,7 H3 V8 H2 Z"},
    {kIconBlack, "M3,7 H6 V8 H3 Z"},
    {kIconBlack, "M9,5 H12 A1,1 0 0 1 13,6 V9 A1,1 0 0 1 12,10 H9 Z"},
    {kIconTeal, "M9,6 H12 V9 H9 Z"},
    {kIconAqua, "M10,6 H11 V7 H10 Z"},
    {kIconBlack, "M7.5,4 A2.5,3.5 0 0 1 7.5,11 A2.5,3.5 0 0 1 7.5,4 Z"},
    {kIconTeal, "M7.5,5 A1.5,2.5 0 0 1 7.5,10 A1.5,2.5 0 0 1 7.5,5 Z"},
    {kIconAqua,
     "M7.5,5 A1.5,2.5 0 0 0 6,7.5 L7.5,7.5 Z M7.5,5 A1.5,2.5 0 0 1 8.56,6.0 "
     "L7.5,7.5 Z"},
    {kIconBlack,
     "M8.4,5.6 A1.5,2.5 0 0 1 8.4,9.4 L7.9,8.9 A0.9,1.9 0 0 0 7.9,6.1 Z"},
};

// BPM Iterations — CHICAGO95.
constexpr IconPath kMusicNote16thPaths[] = {
    {kIconBlack, "M0,0 H16 V16 H0 Z"},
    {kIconTeal, "M0,0 H15 V15 H0 Z"},
    {kIconWhite, "M0,0 H15 V1 H0 Z M0,0 H1 V15 H0 Z"},
    {kIconBlack, "M3,3 H13 V10 H3 Z"},
    {kIconNavy, "M4,4 H12 V9 H4 Z"},
    {kIconWhite, "M4,6 H12 V7 H4 Z"},
    {kIconSilver, "M5,7 H6 V8 H5 Z M8,7 H9 V8 H8 Z M10,5 H11 V6 H10 Z"},
    {kIconWhite, "M3,11 H4 V13 H3 Z M7,11 H8 V13 H7 Z M10,11 H11 V13 H10 Z"},
    {kIconBlack, "M4,11 H5 V13 H4 Z M8,11 H9 V13 H8 Z M11,11 H12 V13 H11 Z"},
};

// Toggle Grid Iterations — CHICAGO95.
constexpr IconPath kMathmodePaths[] = {
    {kIconNavy, "M1,2 H15 V14 H1 Z M2,4 V13 H14 V4 Z"},
    {kIconWhite, "M2,4 H14 V13 H2 Z"},
    {kIconBlack,
     "M3,5 H5 V7 H3 Z M7,5 H9 V7 H7 Z M11,5 H13 V7 H11 Z M3,9 H5 V11 H3 Z "
     "M7,9 H9 V11 H7 Z M11,9 H13 V11 H11 Z"},
};

// Play Renders — ORIGINAL.
constexpr IconPath kPreviewRenderOnPaths[] = {
    {kIconNavy, "M0,1 H16 V15 H0 Z M1,4 V14 H15 V4 Z"},
    {kIconWhite, "M1,4 H15 V14 H1 Z"},
    {kIconBlack, "M5,5.5 L12,9 L5,12.5 Z"},
};

// Load in Place — MICROSOFT.
constexpr IconPath kDialogOkApplyPaths[] = {
    {kIconBlack,
     "M1.785,9.069 L4.773,14.049 L14.251,3.887 L12.349,2.113 L5.227,9.751 "
     "L4.015,7.731 Z"},
};

// Toggle History View — CHICAGO95.
constexpr IconPath kVcsDiffPaths[] = {
    {kIconBlack, "M1,13 H15 V14 H1 Z M14,4 H15 V14 H14 Z"},
    {kIconGray, "M2,0 L7,0 L8,1 L8,3 L14,3 L14,13 L0,13 L0,3 L1,3 L1,1 Z"},
    {kIconYellow, "M2,1 H7 V4 H13 V12 H1 V4 H2 Z"},
    {kIconWhite, "M1,4 H13 V5 H1 Z M1,4 H2 V12 H1 Z"},
    {kIconBlack,
     "M7.5,5 A3.5,3.5 0 0 1 7.5,12 A3.5,3.5 0 0 1 7.5,5 Z M7.5,6 A2.5,2.5 0 "
     "0 0 7.5,11 A2.5,2.5 0 0 0 7.5,6 Z"},
    {kIconWhite, "M7.5,6 A2.5,2.5 0 0 1 7.5,11 A2.5,2.5 0 0 1 7.5,6 Z"},
    {kIconBlack, "M7.5,8.5 L7.5,4.6 L11.4,8.5 Z"},
    {kIconOlive, "M8.2,8 L8.2,6.3 L9.9,8 Z"},
};

// Toggle History Walk — ORIGINAL.
constexpr IconPath kShallowHistoryPaths[] = {
    {kIconBlack,
     "M8,1.5 A6.5,6.5 0 0 1 8,14.5 A6.5,6.5 0 0 1 8,1.5 Z M8,2.5 A5.5,5.5 0 "
     "0 0 8,13.5 A5.5,5.5 0 0 0 8,2.5 Z"},
    {kIconWhite, "M8,2.5 A5.5,5.5 0 0 1 8,13.5 A5.5,5.5 0 0 1 8,2.5 Z"},
    {kIconBlack, "M7.5,4 H8.5 V8.5 H7.5 Z M7.5,7.5 H11 V8.5 H7.5 Z"},
};

// Toggle Add to Selection — CHICAGO95.
constexpr IconPath kEditSelectPaths[] = {
    {kIconBlack, "M0,1 L8,9 H4.7 L7,14 L5,15 L2.6,10.2 L0,12.6 Z"},
    {kIconBlack, "M11,8 H13 V14 H11 Z M9,10 H15 V12 H9 Z"},
};

// Left (row 8's arrow) and Previous Marker — CHICAGO95.
constexpr IconPath kGoPreviousPaths[] = {
    {kIconBlack, "M1,8 L8,1 V5 H15 V11 H8 V15 Z"},
    {kIconAqua, "M2.414,8 L7,3.414 V6 H14 V10 H7 V12.586 Z"},
};

// Right (row 8's arrow) and Next Marker — CHICAGO95.
constexpr IconPath kGoNextPaths[] = {
    {kIconBlack, "M 15,8 L 8,1 V 5 H 1 V 11 H 8 V 15 Z"},
    {kIconAqua, "M 13.586,8 L 9,3.414 V 6 H 2 V 10 H 9 V 12.586 Z"},
};

// Revert — CHICAGO95.
constexpr IconPath kDocumentRevertPaths[] = {
    {kIconBlack, "M1,0 H14 V15 H1 Z"},
    {kIconWhite, "M2,1 H13 V14 H2 Z"},
    {kIconNavy, "M3,3 H12 V4 H3 Z"},
    {kIconMaroon, "M7,7 H12 V8 H7 Z M11,5 H12 V8 H11 Z M5,7.5 L8,5 L8,10 Z"},
};

// Go to Start and Older (the history step) — MICROSOFT.
constexpr IconPath kMediaSkipBackwardPaths[] = {
    {kIconBlack, "M2,3 H4 V12 H2 Z M9,3 L4,7.5 L9,12 Z M14,3 L9,7.5 L14,12 Z"},
};

// Play — MICROSOFT.
constexpr IconPath kMediaPlaybackStartPaths[] = {
    {kIconBlack, "M3,3 L12,7.5 L3,12 Z"},
};

// Stop — MICROSOFT.
constexpr IconPath kMediaPlaybackStopPaths[] = {
    {kIconBlack, "M4,4 H12 V12 H4 Z"},
};

// Pause (the render player) — MICROSOFT.
constexpr IconPath kMediaPlaybackPausePaths[] = {
    {kIconBlack, "M5,4 H7 V12 H5 Z M9,4 H11 V12 H9 Z"},
};

// Go to End and Newer (the history step) — MICROSOFT.
constexpr IconPath kMediaSkipForwardPaths[] = {
    {kIconBlack,
     "M 14,3 H 12 V 12 H 14 Z M 7,3 L 12,7.5 L 7,12 Z M 2,3 L 7,7.5 L 2,12 Z"},
};

// Down (row 8's arrow) — CHICAGO95.
constexpr IconPath kGoDownPaths[] = {
    {kIconBlack, "M8,15 L1,8 H5 V1 H11 V8 H15 Z"},
    {kIconAqua, "M8,13.586 L3.414,9 H6 V2 H10 V9 H12.586 Z"},
};

// Up (row 8's arrow) — CHICAGO95.
constexpr IconPath kGoUpPaths[] = {
    {kIconBlack, "M8,1 L15,8 H11 V15 H5 V8 H1 Z"},
    {kIconAqua, "M8,2.414 L12.586,7 H10 V14 H6 V7 H3.414 Z"},
};

// Toggle Read-Only (locked) — CHICAGO95.
constexpr IconPath kLockPaths[] = {
    {kIconOlive,
     "M4,6.5 V4 A4,4 0 0 1 12,4 V6.5 H10.5 V4 A2.5,2.5 0 0 0 5.5,4 V6.5 Z"},
    {kIconBlack, "M8,0 A4,4 0 0 1 12,4 V6.5 H11 V4 A3,3 0 0 0 8,1 Z"},
    {kIconBlack, "M2,6 H14 V15 H2 Z"},
    {kIconOlive, "M2,6 H13 V14 H2 Z"},
    {kIconWhite, "M3,7 H12 V8 H3 Z M3,7 H4 V13 H3 Z"},
    {kIconYellow, "M4,8 H12 V13 H4 Z"},
    {kIconOlive, "M4,9 H11 V10 H4 Z M4,11 H11 V12 H4 Z"},
};

// Toggle Read-Only (unlocked) — CHICAGO95.
constexpr IconPath kUnlockPaths[] = {
    {kIconOlive, "M1,6 V4 A4,4 0 0 1 9,4 V6 H7.5 V4 A2.5,2.5 0 0 0 2.5,4 V6 Z"},
    {kIconBlack, "M5,0 A4,4 0 0 1 9,4 V6 H8 V4 A3,3 0 0 0 5,1 Z"},
    {kIconBlack, "M3,6 H15 V15 H3 Z"},
    {kIconOlive, "M3,6 H14 V14 H3 Z"},
    {kIconWhite, "M4,7 H13 V8 H4 Z M4,7 H5 V13 H4 Z"},
    {kIconYellow, "M5,8 H13 V13 H5 Z"},
    {kIconOlive, "M5,9 H12 V10 H5 Z M5,11 H12 V12 H5 Z"},
};

// Switch Tab — CHICAGO95.
constexpr IconPath kTabDetachPaths[] = {
    {kIconBlack, "M1,0 H7 L10,3 V11 H1 Z"},
    {kIconWhite, "M2,1 H6 V4 H9 V10 H2 Z"},
    {kIconWhite, "M7,1.5 L8.5,3 H7 Z"},
    {kIconBlack, "M6,5 H12 L15,8 V16 H6 Z"},
    {kIconWhite, "M7,6 H11 V9 H14 V15 H7 Z"},
    {kIconWhite, "M12,6.5 L13.5,8 H12 Z"},
};

// Settings — MICROSOFT.
constexpr IconPath kSettingsConfigurePaths[] = {
    {kIconBlack, "M0,4 H12 V15 H0 Z"},
    {kIconWhite, "M1,5 H11 V14 H1 Z"},
    {kIconBlack,
     "M2,10 H4 V11 H2 Z M5,10 H10 V11 H5 Z M2,12 H4 V13 H2 Z M5,12 H10 V13 "
     "H5 Z"},
    {kIconBlack,
     "M6.5,1 H7.5 A1.5,1.5 0 0 1 9,2.5 V7.5 A1.5,1.5 0 0 1 7.5,9 H6.5 "
     "A1.5,1.5 0 0 1 5,7.5 V2.5 A1.5,1.5 0 0 1 6.5,1 Z M4.5,6 H10.5 "
     "A1.5,1.5 0 0 1 12,7.5 V8.5 A1.5,1.5 0 0 1 10.5,10 H4.5 A1.5,1.5 0 0 1 "
     "3,8.5 V7.5 A1.5,1.5 0 0 1 4.5,6 Z"},
    {kIconWhite,
     "M6.9,2 H7.1 A0.9,0.9 0 0 1 8,2.9 V7.1 A0.9,0.9 0 0 1 7.1,8 H6.9 "
     "A0.9,0.9 0 0 1 6,7.1 V2.9 A0.9,0.9 0 0 1 6.9,2 Z M4.9,7 H10.1 "
     "A0.9,0.9 0 0 1 11,7.9 V8.1 A0.9,0.9 0 0 1 10.1,9 H4.9 A0.9,0.9 0 0 1 "
     "4,8.1 V7.9 A0.9,0.9 0 0 1 4.9,7 Z"},
};

// a folder row (the folder overlay, the project picker) — CHICAGO95.
constexpr IconPath kFolderPaths[] = {
    {kIconBlack, "M1,14 H16 V15 H1 Z M15,4 H16 V15 H15 Z"},
    {kIconGray, "M2,0 L7,0 L8,1 L8,3 L15,3 L15,14 L0,14 L0,3 L1,3 L1,1 Z"},
    {kIconYellow, "M2,1 H7 V4 H14 V13 H1 V4 H2 Z"},
    {kIconOlive, "M14,4 H15 V14 H14 Z M1,13 H15 V14 H1 Z"},
    {kIconWhite, "M1,4 H14 V5 H1 Z M1,4 H2 V13 H1 Z"},
};

// a wav row — CHICAGO95.
constexpr IconPath kAudioXWavPaths[] = {
    {kIconGray, "M4,0 L13,0 L16,3 L16,16 L4,16 Z"},
    {kIconWhite, "M5,1 H12 V4 H5 Z M5,4 H14 V14 H5 Z"},
    {kIconSilver, "M14,4 H15 V15 H14 Z M5,14 H15 V15 H5 Z"},
    {kIconSilver, "M12,1 H13 V3 H12 Z"},
    {kIconWhite, "M15,3 L13,3 L13,1 Z"},
    {kIconBlack, "M12,3 H16 V4 H12 Z M15,3 H16 V16 H15 Z M4,15 H16 V16 H4 Z"},
    {kIconOlive, "M7,3 L7,13 L6,13 L4,11 L0,9 L0,7 L4,5 L6,3 Z"},
    {kIconBlack, "M4,10 L6,12 L7,12 L7,13 L6,13 L4,11 L0,9 L0,8 Z"},
    {kIconYellow, "M7,4 L7,11 L6,11 L4,9 L1,9 L1,7 L4,6 L6,4 Z"},
    {kIconWhite, "M7,5 L4,7 L2,8 L2,7 L4,6 L7,4 Z M4,7 H5 V9 H4 Z"},
    {kIconWhite, "M1,7 H2 V8 H1 Z"},
    {kIconSilver, "M2,7 H3 V8 H2 Z"},
    {kIconGray, "M2,8 H3 V9 H2 Z"},
    {kIconGray, "M5,5 H6 V6 H5 Z"},
    {kIconBlack, "M5,6 H6 V11 H5 Z"},
    {kIconGray,
     "M7,3 H8 A1,1 0 0 1 9,4 V12 A1,1 0 0 1 8,13 H7 A1,1 0 0 1 6,12 V4 A1,1 "
     "0 0 1 7,3 Z"},
    {kIconBlack, "M6.5,4 A1,1 0 0 1 8.5,4 Z"},
    {kIconBlack, "M6.5,12 A1,1 0 0 0 8.5,12 Z"},
    {kIconSilver, "M6,4 H7 V12 H6 Z"},
    {kIconWhite, "M7,4 H8 V12 H7 Z"},
    {kIconGray,
     "M10,6 L11,5 L12,5 L12,6 L11,7 L10,7 Z M10,8 H13 V9 H10 Z M10,10 "
     "L11,10 L12,11 L12,12 L11,12 L10,11 Z"},
};

// Toggle Repeat One (the render player) — CHICAGO95.
constexpr IconPath kMediaRepeatSinglePaths[] = {
    {kIconNavy,
     "M4.402,9.99 L3.54,9.819 L2.725,9.275 L2.181,8.46 L2,7.549 L2,5.451 "
     "L2.181,4.54 L2.725,3.725 L3.54,3.181 L4.451,3 L11.549,3 L12.46,3.181 "
     "L13.275,3.725 L13.819,4.54 L14,5.451 L14,7.549 L13.819,8.46 "
     "L13.275,9.275 L12.46,9.819 L11.549,10 L10,10 L10,9 L11.451,9 "
     "L12.07,8.877 L12.554,8.554 L12.877,8.07 L13,7.451 L13,5.549 "
     "L12.877,4.93 L12.554,4.446 L12.07,4.123 L11.451,4 L4.549,4 "
     "L3.93,4.123 L3.446,4.446 L3.123,4.93 L3,5.549 L3,7.451 L3.123,8.07 "
     "L3.446,8.554 L3.93,8.877 L4.598,9.01 Z M7,9.5 L10,7 L10,12 Z"},
};

// Up a Folder (the render player) — MICROSOFT.
constexpr IconPath kGoParentFolderPaths[] = {
    {kIconBlack, "M2,0 L7,0 L8,1 L8,3 L15,3 L15,14 L0,14 L0,3 L1,3 L1,1 Z"},
    {kIconYellow, "M2,1 H7 V4 H14 V13 H1 V4 H2 Z"},
    {kIconBlack, "M5.5,5 L8,8 L6,8 L6,10 L11,10 L11,11 L5,11 L5,8 L3,8 Z"},
};

// a NORMAL card's glyph — CHICAGO95.
constexpr IconPath kDialogInformationPaths[] = {
    {kIconGray,
     "M8.5,1 A6.5,6.5 0 0 1 8.5,14 A6.5,6.5 0 0 1 8.5,1 Z M6.5,12.5 "
     "L11.5,12.5 L9.5,16.5 Z"},
    {kIconBlack,
     "M7.5,0 A6.5,6.5 0 0 1 7.5,13 A6.5,6.5 0 0 1 7.5,0 Z M5.5,11.5 "
     "L10.5,11.5 L8.5,15.5 Z"},
    {kIconGray,
     "M6.9,-0.6 A6.5,6.5 0 0 1 6.9,12.4 A6.5,6.5 0 0 1 6.9,-0.6 Z M4.9,10.9 "
     "L9.9,10.9 L7.9,14.9 Z"},
    {kIconWhite,
     "M7.5,1 A5.5,5.5 0 0 1 7.5,12 A5.5,5.5 0 0 1 7.5,1 Z M6,11.2 L10,11.2 "
     "L8.5,14.2 Z"},
    {kIconBlue, "M6,2 H9 V4 H6 Z M6,5 H9 V9 H6 Z M5,9 H10 V10 H5 Z"},
};

// a CRITICAL card's glyph — CHICAGO95.
constexpr IconPath kDialogErrorPaths[] = {
    {kIconGray, "M8.5,1 A7.5,7.5 0 0 1 8.5,16 A7.5,7.5 0 0 1 8.5,1 Z"},
    {kIconMaroon,
     "M7.5,0 A7.5,7.5 0 0 1 7.5,15 A7.5,7.5 0 0 1 7.5,0 Z M7.5,1 A6.5,6.5 0 "
     "0 0 7.5,14 A6.5,6.5 0 0 0 7.5,1 Z"},
    {kIconRed, "M7.5,1 A6.5,6.5 0 0 1 7.5,14 A6.5,6.5 0 0 1 7.5,1 Z"},
    {kIconWhite,
     "M4.649,2.951 L12.049,10.351 L10.351,12.049 L2.951,4.649 Z "
     "M12.049,4.649 L4.649,12.049 L2.951,10.351 L10.351,2.951 Z"},
};

// Close (the render player) and Cancel (Render's mid-render face) — MICROSOFT.
constexpr IconPath kWindowClosePaths[] = {
    {kIconBlack,
     "M2.5,3 L4.5,3 L13.5,12 L11.5,12 Z M4.5,12 L2.5,12 L11.5,3 L13.5,3 Z"},
};

// Copy Resolved Value — MICROSOFT.
constexpr IconPath kEditCopyPaths[] = {
    {kIconBlack, "M0,1 H6 L8,3 V11 H0 Z"},
    {kIconWhite, "M1,2 H5 V5 H7 V10 H1 Z"},
    {kIconWhite, "M6,2.5 L7.5,4 H6 Z"},
    {kIconBlack, "M2,4 H4 V5 H2 Z M2,6 H6 V7 H2 Z M2,8 H6 V9 H2 Z"},
    {kIconNavy, "M6,4 H12 L15,7 V14 H6 Z"},
    {kIconWhite, "M7,5 H11 V8 H14 V13 H7 Z"},
    {kIconWhite, "M12,5.5 L13.5,7 H12 Z"},
    {kIconBlack, "M8,7 H10 V8 H8 Z M8,9 H13 V10 H8 Z M8,11 H13 V12 H8 Z"},
};

// Toggle Tooltips — MICROSOFT.
constexpr IconPath kHelpWhatsthisPaths[] = {
    {kIconBlack, "M0,1 L8,9 H4.7 L7,14 L5,15 L2.6,10.2 L0,12.6 Z"},
    {kIconNavy,
     "M7.019,4.761 L7.185,3.736 L7.776,2.6 L8.695,1.707 L9.847,1.149 "
     "L11.117,0.982 L12.375,1.223 L13.493,1.847 L14.358,2.791 L14.882,3.96 "
     "L15.013,5.234 L14.735,6.484 L14.19,7.398 L13,9.242 L13,10 L10,10 "
     "L10,8.358 L11.641,5.816 L11.911,5.362 L11.979,5.057 L11.947,4.746 "
     "L11.819,4.461 L11.608,4.231 L11.335,4.079 L11.029,4.02 L10.719,4.061 "
     "L10.438,4.197 L10.213,4.414 L10.069,4.692 L9.981,5.239 Z M10,11 H13 "
     "V13 H10 Z"},
};

// Jump to Defining Marker — ORIGINAL.
constexpr IconPath kGoJumpDeclarationPaths[] = {
    {kIconBlack,
     "M2.407,8.906 L2.667,7.267 L3.463,5.704 L4.704,4.463 L6.267,3.667 "
     "L8,3.393 L9.733,3.667 L11.296,4.463 L12.537,5.704 L13.333,7.267 "
     "L13.593,8.906 L12.407,9.094 L12.178,7.643 L11.554,6.418 L10.582,5.446 "
     "L9.357,4.822 L8,4.607 L6.643,4.822 L5.418,5.446 L4.446,6.418 "
     "L3.822,7.643 L3.593,9.094 Z M10.5,8.5 L15.5,8.5 L13,12.5 Z M2.4,9 "
     "H3.6 V13 H2.4 Z"},
};

// Delete Folder (the render player) — MICROSOFT.
constexpr IconPath kEditDeletePaths[] = {
    {kIconBlack,
     "M4.342,0.879 L14.521,12.885 L13.879,13.515 L2.058,3.121 Z "
     "M2.095,10.843 L13.889,1.174 L14.511,1.826 L4.305,13.157 Z"},
};

// the caption's icon — CHICAGO95.
constexpr IconPath kAppIconPaths[] = {
    {kIconOlive, "M8,1 L8,15 L7,15 L3,11 L1,11 L0,10 L0,6 L1,5 L3,5 L7,1 Z"},
    {kIconBlack,
     "M1,10 L3,10 L7,14 L8,14 L8,15 L7,15 L3,11 L1,11 L0,10 L0,9 Z"},
    {kIconYellow, "M8,2 L8,13 L7,13 L3,9 L1,9 L1,6 L3,6 L7,2 Z"},
    {kIconWhite, "M8,4 L3,9 L3,8 L8,3 Z"},
    {kIconWhite, "M1,6 H2 V7 H1 Z"},
    {kIconSilver, "M2,6 H3 V9 H2 Z"},
    {kIconGray, "M2,9 H3 V10 H2 Z"},
    {kIconGray, "M6,3 H7 V4 H6 Z M6,12 H7 V13 H6 Z"},
    {kIconBlack, "M6,4 H7 V12 H6 Z"},
    {kIconGray,
     "M9,0 A2,2 0 0 1 11,2 V14 A2,2 0 0 1 9,16 A2,2 0 0 1 7,14 V2 A2,2 0 0 "
     "1 9,0 Z"},
    {kIconBlack, "M7.5,2 A1.5,1.5 0 0 1 10.5,2 Z"},
    {kIconBlack, "M7.5,14 A1.5,1.5 0 0 0 10.5,14 Z"},
    {kIconSilver, "M7,2 H11 V3 H7 Z M7,13 H11 V14 H7 Z M7,3 H8 V13 H7 Z"},
    {kIconWhite, "M8,3 H10 V13 H8 Z"},
    {kIconBlack,
     "M7.098,5.91 L7.807,6.051 L8.492,6.508 L8.949,7.193 L9.11,8 "
     "L8.949,8.807 L8.492,9.492 L7.807,9.949 L7.098,10.09 L6.902,9.11 "
     "L7.417,9.007 L7.771,8.771 L8.007,8.417 L8.09,8 L8.007,7.583 "
     "L7.771,7.229 L7.417,6.993 L6.902,6.89 Z"},
    {kIconGray,
     "M12,5 L14,3 L15,3 L15,4 L13,6 L12,6 Z M12,8 H16 V9 H12 Z M12,11 "
     "L13,11 L15,13 L15,14 L14,14 L12,12 Z"},
};

// EACH ENUMERATOR'S ROW — its own file's, the five shared drawings' second
// wearers (DialogCancel, KeyframePrevious, KeyframeNext, BboxPrev, BboxNext)
// on the row of their twin, whose file is byte-identical.
IconDef icon_def(Icon icon) {
    switch (icon) {
        case Icon::DocumentSave: return icon_def_of(kDocumentSavePaths);
        case Icon::EditUndo: return icon_def_of(kEditUndoPaths);
        case Icon::EditRedo: return icon_def_of(kEditRedoPaths);
        case Icon::MediaRecord: return icon_def_of(kMediaRecordPaths);
        case Icon::VcsCommit: return icon_def_of(kVcsCommitPaths);
        case Icon::VcsPull: return icon_def_of(kVcsPullPaths);
        case Icon::DocumentExport: return icon_def_of(kDocumentExportPaths);
        case Icon::DocumentImport: return icon_def_of(kDocumentImportPaths);
        case Icon::ChronometerStart: return icon_def_of(kChronometerStartPaths);
        case Icon::ZoomFitBest: return icon_def_of(kZoomFitBestPaths);
        case Icon::ZoomOriginal: return icon_def_of(kZoomOriginalPaths);
        case Icon::ZoomInY: return icon_def_of(kZoomInYPaths);
        case Icon::ListAdd: return icon_def_of(kListAddPaths);
        case Icon::ListRemove: return icon_def_of(kListRemovePaths);
        case Icon::ViewHidden: return icon_def_of(kViewHiddenPaths);
        case Icon::InsertLink: return icon_def_of(kInsertLinkPaths);
        case Icon::Merge: return icon_def_of(kMergePaths);
        case Icon::BlackSum: return icon_def_of(kBlackSumPaths);
        case Icon::GoJump: return icon_def_of(kGoJumpPaths);
        case Icon::TimelineLift: return icon_def_of(kTimelineLiftPaths);
        case Icon::MusicNote16th: return icon_def_of(kMusicNote16thPaths);
        case Icon::Mathmode: return icon_def_of(kMathmodePaths);
        case Icon::PreviewRenderOn: return icon_def_of(kPreviewRenderOnPaths);
        case Icon::DialogOkApply: return icon_def_of(kDialogOkApplyPaths);
        case Icon::VcsDiff: return icon_def_of(kVcsDiffPaths);
        case Icon::ShallowHistory: return icon_def_of(kShallowHistoryPaths);
        case Icon::EditSelect: return icon_def_of(kEditSelectPaths);
        case Icon::KeyframePrevious:
            return icon_def_of(kMediaSkipBackwardPaths);
        case Icon::KeyframeNext: return icon_def_of(kMediaSkipForwardPaths);
        case Icon::GoPrevious: return icon_def_of(kGoPreviousPaths);
        case Icon::GoNext: return icon_def_of(kGoNextPaths);
        case Icon::DocumentRevert: return icon_def_of(kDocumentRevertPaths);
        case Icon::MediaSkipBackward:
            return icon_def_of(kMediaSkipBackwardPaths);
        case Icon::MediaPlaybackStart:
            return icon_def_of(kMediaPlaybackStartPaths);
        case Icon::MediaPlaybackStop:
            return icon_def_of(kMediaPlaybackStopPaths);
        case Icon::MediaPlaybackPause:
            return icon_def_of(kMediaPlaybackPausePaths);
        case Icon::MediaSkipForward: return icon_def_of(kMediaSkipForwardPaths);
        case Icon::DialogCancel: return icon_def_of(kWindowClosePaths);
        case Icon::GoDown: return icon_def_of(kGoDownPaths);
        case Icon::GoUp: return icon_def_of(kGoUpPaths);
        case Icon::Lock: return icon_def_of(kLockPaths);
        case Icon::Unlock: return icon_def_of(kUnlockPaths);
        case Icon::BboxPrev: return icon_def_of(kGoPreviousPaths);
        case Icon::BboxNext: return icon_def_of(kGoNextPaths);
        case Icon::TabDetach: return icon_def_of(kTabDetachPaths);
        case Icon::SettingsConfigure:
            return icon_def_of(kSettingsConfigurePaths);
        case Icon::Folder: return icon_def_of(kFolderPaths);
        case Icon::AudioXWav: return icon_def_of(kAudioXWavPaths);
        case Icon::MediaRepeatSingle:
            return icon_def_of(kMediaRepeatSinglePaths);
        case Icon::GoParentFolder: return icon_def_of(kGoParentFolderPaths);
        case Icon::DialogInformation:
            return icon_def_of(kDialogInformationPaths);
        case Icon::DialogError: return icon_def_of(kDialogErrorPaths);
        case Icon::WindowClose: return icon_def_of(kWindowClosePaths);
        case Icon::EditCopy: return icon_def_of(kEditCopyPaths);
        case Icon::HelpWhatsthis: return icon_def_of(kHelpWhatsthisPaths);
        case Icon::GoJumpDeclaration:
            return icon_def_of(kGoJumpDeclarationPaths);
        case Icon::EditDelete: return icon_def_of(kEditDeletePaths);
        case Icon::AppIcon: return icon_def_of(kAppIconPaths);
    }
    return IconDef{};
}

// -- The `d` interpreter ----------------------------------------------------
//
// THE SUBSET: M/m, L/l, H/h, V/v, C/c, S/s, A/a, Z/z, implicit command
// repetition (a bare argument set repeats the previous command; after M/m the
// repeat is L/l, per SVG), comma-or-whitespace separation with both optional,
// negative numbers as their own separator ("5-5"), leading-dot decimals
// chained without separators (".207031.207031" is two numbers — a second '.'
// ends the first) and the exponent (parse_number). THE COMMITTED SET SPELLS
// ONLY THE ABSOLUTE M, L, H, V, C, A AND Z (assets/icons/warptempo/, read
// 2026-10-06); the relative forms, the smooth cubic and the exponent each
// joined with a producer an earlier set carried, and the subset grows with a
// producer and is not shrunk when one leaves. No Q/q, T/t: never spelled by
// any committed file, so the parser refuses them loudly rather than
// guessing. Elliptical `A` is implemented GENERALLY (endpoint->center
// conversion plus a quarter-arc bezier split) — the set's discs, rings,
// ellipses and rounded corners are all arcs.
//
// THE SUBSET IS THE `d` GRAMMAR AND NOTHING ELSE: a path element carries its
// fill and its `d` and no transform (no file in the set has one).
struct PathCursor {
    const char* p;
    const char* end;
};

bool at_end(const PathCursor& c) { return c.p >= c.end; }

void skip_separators(PathCursor& c) {
    while (!at_end(c)) {
        const char ch = *c.p;
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == ',')
            ++c.p;
        else
            break;
    }
}

// One SVG number: optional sign, digits, at most ONE decimal point, and an
// optional EXPONENT. The single-point rule is what splits ".207031.207031" into
// two numbers.
//
// THE EXPONENT JOINED 2026-08-20 WITH ITS FIRST PRODUCER, a file whose
// author's editor wrote small offsets as `8e-3` (a grammar feature enters this
// subset when a committed file spells it, never ahead of one: re-spelling the
// numbers in the table would break the verbatim rule at the table's head). No
// committed file spells an exponent today; the scanner stays, the subset
// growing with a producer and not shrinking when one leaves.
//
// IT IS SCANNED STRICTLY: the `e` is consumed only when an optional sign and at
// least ONE digit follow it, so a trailing `e` ends the number instead of
// swallowing the next command letter. std::from_chars reads the whole span
// either way — its default `general` format already includes exponents — so
// only the SPAN scanner below had to learn the syntax.
bool parse_number(PathCursor& c, double& out) {
    skip_separators(c);
    const char* start = c.p;
    if (!at_end(c) && (*c.p == '-' || *c.p == '+')) ++c.p;
    bool saw_digit = false;
    bool saw_dot   = false;
    while (!at_end(c)) {
        const char ch = *c.p;
        if (ch >= '0' && ch <= '9') { saw_digit = true; ++c.p; continue; }
        if (ch == '.' && !saw_dot)  { saw_dot   = true; ++c.p; continue; }
        break;
    }
    if (!saw_digit) { c.p = start; return false; }
    if (!at_end(c) && (*c.p == 'e' || *c.p == 'E')) {
        const char* mantissa_end = c.p;
        ++c.p;
        if (!at_end(c) && (*c.p == '-' || *c.p == '+')) ++c.p;
        bool saw_exp_digit = false;
        while (!at_end(c) && *c.p >= '0' && *c.p <= '9') {
            saw_exp_digit = true;
            ++c.p;
        }
        // No digits after the `e`: it was not an exponent at all. Give the
        // whole tail back and keep the mantissa as the number.
        if (!saw_exp_digit) c.p = mantissa_end;
    }
    // std::from_chars: no locale, no allocation, and it consumes exactly the
    // span already delimited above. A leading '+' is not part of its grammar,
    // so skip it.
    const char* num_begin = (*start == '+') ? start + 1 : start;
    double v = 0.0;
    const std::from_chars_result r = std::from_chars(num_begin, c.p, v);
    if (r.ec != std::errc{} || r.ptr != c.p) { c.p = start; return false; }
    out = v;
    return true;
}

// An arc flag is a single '0' or '1' and may be glued to its neighbours.
bool parse_flag(PathCursor& c, bool& out) {
    skip_separators(c);
    if (at_end(c)) return false;
    if (*c.p == '0') { out = false; ++c.p; return true; }
    if (*c.p == '1') { out = true;  ++c.p; return true; }
    return false;
}

// Elliptical arc from the current point to (x1, y1), appended as cubic beziers.
// Standard endpoint->center parameterization (SVG implementation notes F.6.5)
// followed by a split into <=90-degree segments, each approximated by the
// classic (4/3)tan(delta/4) control-point rule.
void arc_to(cairo_t* cr, double x0, double y0, double rx, double ry,
            double phi_deg, bool large_arc, bool sweep, double x1, double y1) {
    constexpr double kPi = 3.14159265358979323846;
    if (rx == 0.0 || ry == 0.0 || (x0 == x1 && y0 == y1)) {
        cairo_line_to(cr, x1, y1);
        return;
    }
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    const double phi = phi_deg * kPi / 180.0;
    const double cos_phi = std::cos(phi), sin_phi = std::sin(phi);

    const double dx2 = (x0 - x1) * 0.5, dy2 = (y0 - y1) * 0.5;
    const double x1p =  cos_phi * dx2 + sin_phi * dy2;
    const double y1p = -sin_phi * dx2 + cos_phi * dy2;

    // Enlarge the radii if they cannot span the chord (SVG F.6.6).
    const double lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.0) {
        const double s = std::sqrt(lambda);
        rx *= s;
        ry *= s;
    }

    const double rx2 = rx * rx, ry2 = ry * ry;
    const double num = rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p;
    const double den = rx2 * y1p * y1p + ry2 * x1p * x1p;
    double coef = 0.0;
    if (den > 0.0 && num > 0.0)
        coef = std::sqrt(num / den);
    if (large_arc == sweep) coef = -coef;
    const double cxp =  coef * rx * y1p / ry;
    const double cyp = -coef * ry * x1p / rx;
    const double cx = cos_phi * cxp - sin_phi * cyp + (x0 + x1) * 0.5;
    const double cy = sin_phi * cxp + cos_phi * cyp + (y0 + y1) * 0.5;

    const double ux = (x1p - cxp) / rx, uy = (y1p - cyp) / ry;
    const double vx = (-x1p - cxp) / rx, vy = (-y1p - cyp) / ry;
    const double theta1 = std::atan2(uy, ux);
    double dtheta = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!sweep && dtheta > 0.0) dtheta -= 2.0 * kPi;
    if (sweep  && dtheta < 0.0) dtheta += 2.0 * kPi;

    const int segments =
        static_cast<int>(std::ceil(std::fabs(dtheta) / (kPi * 0.5)));
    const double delta = dtheta / static_cast<double>(segments <= 0 ? 1
                                                                   : segments);
    const double alpha = (4.0 / 3.0) * std::tan(delta * 0.25);
    double t = theta1;
    for (int i = 0; i < segments; ++i) {
        const double t2 = t + delta;
        const double cos_t = std::cos(t),  sin_t = std::sin(t);
        const double cos_2 = std::cos(t2), sin_2 = std::sin(t2);
        // Point and derivative of the parameterized ellipse.
        const double px  = cx + rx * cos_t * cos_phi - ry * sin_t * sin_phi;
        const double py  = cy + rx * cos_t * sin_phi + ry * sin_t * cos_phi;
        const double dpx = -rx * sin_t * cos_phi - ry * cos_t * sin_phi;
        const double dpy = -rx * sin_t * sin_phi + ry * cos_t * cos_phi;
        const double qx  = cx + rx * cos_2 * cos_phi - ry * sin_2 * sin_phi;
        const double qy  = cy + rx * cos_2 * sin_phi + ry * sin_2 * cos_phi;
        const double dqx = -rx * sin_2 * cos_phi - ry * cos_2 * sin_phi;
        const double dqy = -rx * sin_2 * sin_phi + ry * cos_2 * cos_phi;
        cairo_curve_to(cr, px + alpha * dpx, py + alpha * dpy,
                       qx - alpha * dqx, qy - alpha * dqy, qx, qy);
        t = t2;
    }
}

// Walk one `d` string, appending to cr's current path. Returns false on the
// first thing the subset does not cover (the caller then draws nothing).
bool append_path(cairo_t* cr, const char* d) {
    PathCursor c{d, d + std::strlen(d)};
    char   cmd       = 0;     // the command in force (for implicit repetition)
    double cur_x = 0.0, cur_y = 0.0;      // current point
    double start_x = 0.0, start_y = 0.0;  // current subpath's start (for Z)
    bool   have_start = false;
    // THE SMOOTH CUBIC'S MEMORY, in ABSOLUTE coordinates: S/s takes its first
    // control point by REFLECTING the previous cubic's second control point
    // about the current point, and takes the current point itself when the
    // previous command was not a cubic (SVG 8.3.6). Both halves need this pair,
    // so every non-cubic arm below clears the flag rather than only the cubic
    // arms setting it.
    double last_c2x = 0.0, last_c2y = 0.0;
    bool   prev_cubic = false;

    for (;;) {
        skip_separators(c);
        if (at_end(c)) break;
        const char ch = *c.p;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
            cmd = ch;
            ++c.p;
        } else if (cmd == 0) {
            return false;             // arguments before any command
        } else if (cmd == 'Z' || cmd == 'z') {
            return false;             // Z takes no arguments: this is garbage
        } else if (cmd == 'M') {
            cmd = 'L';                // SVG: an M's extra pairs are lineto
        } else if (cmd == 'm') {
            cmd = 'l';
        }

        const bool rel = (cmd >= 'a' && cmd <= 'z');
        double a = 0.0, b = 0.0, c1x = 0.0, c1y = 0.0, c2x = 0.0, c2y = 0.0;
        switch (cmd) {
            case 'M': case 'm':
                if (!parse_number(c, a) || !parse_number(c, b)) return false;
                cur_x = rel ? cur_x + a : a;
                cur_y = rel ? cur_y + b : b;
                cairo_move_to(cr, cur_x, cur_y);
                start_x = cur_x; start_y = cur_y; have_start = true;
                prev_cubic = false;
                break;
            case 'L': case 'l':
                if (!parse_number(c, a) || !parse_number(c, b)) return false;
                cur_x = rel ? cur_x + a : a;
                cur_y = rel ? cur_y + b : b;
                cairo_line_to(cr, cur_x, cur_y);
                prev_cubic = false;
                break;
            case 'H': case 'h':
                if (!parse_number(c, a)) return false;
                cur_x = rel ? cur_x + a : a;
                cairo_line_to(cr, cur_x, cur_y);
                prev_cubic = false;
                break;
            case 'V': case 'v':
                if (!parse_number(c, a)) return false;
                cur_y = rel ? cur_y + a : a;
                cairo_line_to(cr, cur_x, cur_y);
                prev_cubic = false;
                break;
            case 'C': case 'c': {
                double x2 = 0.0, y2 = 0.0;
                if (!parse_number(c, c1x) || !parse_number(c, c1y) ||
                    !parse_number(c, c2x) || !parse_number(c, c2y) ||
                    !parse_number(c, x2)  || !parse_number(c, y2))
                    return false;
                const double bx = rel ? cur_x : 0.0;
                const double by = rel ? cur_y : 0.0;
                const double ex = bx + x2, ey = by + y2;
                cairo_curve_to(cr, bx + c1x, by + c1y, bx + c2x, by + c2y,
                               ex, ey);
                cur_x = ex; cur_y = ey;
                last_c2x = bx + c2x; last_c2y = by + c2y;
                prev_cubic = true;
                break;
            }
            case 'S': case 's': {
                // FOUR arguments: the SECOND control point and the endpoint.
                // The first control point is the reflection of the previous
                // cubic's second about the current point — and the current
                // point itself when no cubic precedes, which is the same thing
                // as a curve that starts straight.
                double x2 = 0.0, y2 = 0.0;
                if (!parse_number(c, c2x) || !parse_number(c, c2y) ||
                    !parse_number(c, x2)  || !parse_number(c, y2))
                    return false;
                const double bx = rel ? cur_x : 0.0;
                const double by = rel ? cur_y : 0.0;
                const double r1x = prev_cubic ? 2.0 * cur_x - last_c2x : cur_x;
                const double r1y = prev_cubic ? 2.0 * cur_y - last_c2y : cur_y;
                const double ex = bx + x2, ey = by + y2;
                cairo_curve_to(cr, r1x, r1y, bx + c2x, by + c2y, ex, ey);
                cur_x = ex; cur_y = ey;
                last_c2x = bx + c2x; last_c2y = by + c2y;
                prev_cubic = true;
                break;
            }
            case 'A': case 'a': {
                double rx = 0.0, ry = 0.0, rot = 0.0, ex = 0.0, ey = 0.0;
                bool large = false, sweep = false;
                if (!parse_number(c, rx) || !parse_number(c, ry) ||
                    !parse_number(c, rot) ||
                    !parse_flag(c, large) || !parse_flag(c, sweep) ||
                    !parse_number(c, ex) || !parse_number(c, ey))
                    return false;
                const double x1 = rel ? cur_x + ex : ex;
                const double y1 = rel ? cur_y + ey : ey;
                arc_to(cr, cur_x, cur_y, rx, ry, rot, large, sweep, x1, y1);
                cur_x = x1; cur_y = y1;
                prev_cubic = false;
                break;
            }
            case 'Z': case 'z':
                cairo_close_path(cr);
                if (have_start) { cur_x = start_x; cur_y = start_y; }
                prev_cubic = false;
                break;
            default:
                return false;         // outside the subset
        }
        // IMPLICIT REPETITION needs no code of its own: `cmd` stays in force,
        // so the next pass re-enters the same case when the next token is a
        // number and re-binds it when the token is a letter. The M->L rewrite
        // at the top of the loop is the one SVG rule that is not automatic.
    }
    return true;
}

// ONE STDERR, DRAW NOTHING — and both halves are now literally true.
//
// VALIDATE EVERY PATH BEFORE FILLING ANY. The old loop parsed and filled
// path by path, so a malformed LATER path of a multi-path icon left the
// EARLIER ones already on the surface: a partial glyph, which is exactly
// the "placeholder that lets the typo ship" the contract refuses. The
// dry-run below parses each `d` into a scratch context and bails as a whole
// before a single pixel is committed.
//
// AND SAY IT ONCE. `reported` latches per icon, so a transcription error is
// one line at the first paint rather than one line per repaint forever —
// a tripwire that floods is a tripwire nobody reads. Function-local static:
// the GUI is single-threaded at every draw site.
//
// AND PROVE IT ONCE. The `d` strings are in-tree constexpr data, so the
// verdict cannot change between calls — `validated` latches a PASSED probe
// per icon, and later draws of that icon skip the scratch surface and the
// dry-run parse entirely (the fill loop below re-walks the same constant
// strings, which is the byte-identity the two-walk contract rests on). A
// FAILED probe deliberately does not latch anything but its one stderr
// line: the icon re-probes, re-fails and draws nothing on every call,
// exactly as before. An out-of-range idx (a kIconCount mismatch) never
// latches either — that icon simply pays the probe per draw, the same
// "costs that icon its latch" degradation the header records for
// `reported`. Both draws (draw and draw_engraved) ask it first.
bool icon_paths_valid(Icon icon, const IconDef& def) {
    static bool validated[kIconCount] = {};
    const int idx = static_cast<int>(icon);
    const bool latch_ok =
        idx >= 0 && idx < static_cast<int>(std::size(validated));
    if (latch_ok && validated[idx]) return true;
    cairo_surface_t* probe_surf =
        cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* probe = cairo_create(probe_surf);
    bool ok = true;
    for (int i = 0; i < def.path_count && ok; ++i) {
        cairo_new_path(probe);
        ok = append_path(probe, def.paths[i].d);
    }
    cairo_destroy(probe);
    cairo_surface_destroy(probe_surf);
    if (!ok) {
        static bool reported[kIconCount] = {};
        if (latch_ok && !reported[idx]) {
            reported[idx] = true;
            std::fprintf(stderr,
                         "icons: Malformed path data, icon %d not drawn\n",
                         idx);
        }
        return false;
    }
    if (latch_ok) validated[idx] = true;
    return true;
}

// THE ONE FILL WALK every draw shares: the 16-unit viewBox mapped onto the
// box (x, y, w_px, h_px) — a square for every wearer but the chrome's
// (draw_in_ink) — each path filled after `paint_of(cr, path)` sets its paint
// (a source, or the disabled mask's operator), in table order (the
// layering).
//
// CAIRO'S DEFAULT ANTIALIAS STAYS (architect 2026-10-06): the curves need it
// (the discs, the swoops, the rounded corners), and every straight edge sits
// on a whole unit, so at a whole-multiple gui_scale — a unit then a whole
// number of device px (4 at 400 %) — those edges land on device-pixel
// boundaries and antialias nothing; at a fractional scale they cover pixels
// partially, like every other scaled length's edge.
template <typename PaintOf>
void fill_icon_paths(cairo_t* cr, const IconDef& def, double x, double y,
                     double w_px, double h_px, PaintOf paint_of) {
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, w_px / kIconViewBox, h_px / kIconViewBox);
    for (int i = 0; i < def.path_count; ++i) {
        const IconPath& p = def.paths[i];
        cairo_new_path(cr);
        // Cannot fail: the dry run proved every path in this icon parses (on
        // this call, or on the earlier call whose pass `validated` latched),
        // and the strings are compile-time constants that cannot change between
        // the walks.
        append_path(cr, p.d);
        paint_of(cr, p);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

// A background ink of the disabled mask (draw_engraved): White or Silver.
bool disabled_mask_background(GuiColor ink) {
    const auto same = [](GuiColor a, GuiColor b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    };
    return same(ink, kIconWhite) || same(ink, kIconSilver);
}

// ONE PASS OF THE MASK — the emboss's, and the chrome wear's one pass
// (draw_in_ink): the disabled mask built at (x, y) in an alpha group
// — each ink path filled opaque, each White or Silver path CLEARED, in table
// order, so an inset carved out of a silhouette stays a hole and a later ink
// path drawn over an inset is ink again — then `ink` painted through it.
// Each pass builds its own group at its own place, so the light copy's
// offset never shifts a mask cut at the group's clip.
void emboss_through_disabled_mask(cairo_t* cr, const IconDef& def, double x,
                                  double y, double w_px, double h_px,
                                  GuiColor ink) {
    cairo_push_group_with_content(cr, CAIRO_CONTENT_ALPHA);
    fill_icon_paths(cr, def, x, y, w_px, h_px,
                    [](cairo_t* c, const IconPath& p) {
                        if (disabled_mask_background(p.ink)) {
                            cairo_set_operator(c, CAIRO_OPERATOR_CLEAR);
                        } else {
                            cairo_set_operator(c, CAIRO_OPERATOR_OVER);
                            // Opaque, so the mask's alpha is whole; the
                            // colour itself is never seen.
                            set_palette_source(c, kIconBlack);
                        }
                    });
    cairo_pattern_t* mask = cairo_pop_group(cr);
    cairo_save(cr);
    set_palette_source(cr, ink);
    cairo_mask(cr, mask);
    cairo_restore(cr);
    cairo_pattern_destroy(mask);
}

} // namespace

void draw(cairo_t* cr, Icon icon, double x, double y, double size_px) {
    if (size_px <= 0.0) return;
    const IconDef def = icon_def(icon);
    if (!icon_paths_valid(icon, def)) return;
    fill_icon_paths(cr, def, x, y, size_px, size_px,
                    [](cairo_t* c, const IconPath& p) {
                        set_palette_source(c, p.ink);
                    });
}

void draw_engraved(cairo_t* cr, Icon icon, double x, double y, double size_px,
                   double offset_px) {
    draw_engraved_in_box(cr, icon, x, y, size_px, size_px, offset_px);
}

void draw_engraved_in_box(cairo_t* cr, Icon icon, double x, double y,
                          double w_px, double h_px, double offset_px) {
    if (w_px <= 0.0 || h_px <= 0.0) return;
    const IconDef def = icon_def(icon);
    if (!icon_paths_valid(icon, def)) return;
    // THE DISABLED MASK TWICE (icons.h): the emboss's light copy, the
    // theme's Hilight, one offset right and down beneath, then Shadow at the
    // glyph's own place.
    emboss_through_disabled_mask(cr, def, x + offset_px, y + offset_px, w_px,
                                 h_px, palette().hilight);
    emboss_through_disabled_mask(cr, def, x, y, w_px, h_px, palette().shadow);
}

void draw_in_ink(cairo_t* cr, Icon icon, double x, double y, double w_px,
                 double h_px, GuiColor ink) {
    if (w_px <= 0.0 || h_px <= 0.0) return;
    const IconDef def = icon_def(icon);
    if (!icon_paths_valid(icon, def)) return;
    emboss_through_disabled_mask(cr, def, x, y, w_px, h_px, ink);
}

InkBox ink_box(Icon icon) {
    const IconDef def = icon_def(icon);
    if (!icon_paths_valid(icon, def)) return InkBox{};
    // The union of the mask's ink paths' own extents, in units, on a scratch
    // context at the identity — the drawing's numbers as the table holds
    // them, never a second copy.
    cairo_surface_t* probe_surf =
        cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* probe = cairo_create(probe_surf);
    bool any = false;
    InkBox box;
    for (int i = 0; i < def.path_count; ++i) {
        if (disabled_mask_background(def.paths[i].ink)) continue;
        cairo_new_path(probe);
        append_path(probe, def.paths[i].d);
        double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
        cairo_path_extents(probe, &x0, &y0, &x1, &y1);
        if (!any) {
            box = InkBox{x0, y0, x1, y1};
            any = true;
        } else {
            box = InkBox{std::min(box.x0, x0), std::min(box.y0, y0),
                         std::max(box.x1, x1), std::max(box.y1, y1)};
        }
    }
    cairo_destroy(probe);
    cairo_surface_destroy(probe_surf);
    return box;
}

void draw_cased(cairo_t* cr, Icon icon, int case_x, int case_y,
                double size_px, int button_shift_px) {
    const double x = case_x + icon_case_lead_px() + button_shift_px;
    const double y = case_y + icon_case_lead_px() + button_shift_px;
    draw(cr, icon, x, y, size_px);
}

void draw_cased_disabled(cairo_t* cr, Icon icon, int case_x, int case_y,
                         double size_px, int button_shift_px,
                         double offset_px) {
    const double x = case_x + icon_case_lead_px() + button_shift_px;
    const double y = case_y + icon_case_lead_px() + button_shift_px;
    draw_engraved(cr, icon, x, y, size_px, offset_px);
}

} // namespace icons
