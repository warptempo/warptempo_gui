#!/usr/bin/env python3
"""Hand maps for the two sets with no freedesktop naming: ReactOS (Win32
resource bitmaps/icons) and Nautilus 1.0 (Eazel). Crops land in work/."""
import json, os, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.join(HERE, "sets/reactos")
N = os.path.join(HERE, "sets/nautilus1/nautilus-1.0.6/icons")
CROP = os.path.join(HERE, "work/reactos/crops")
os.makedirs(CROP, exist_ok=True)

def sh(*a):
    subprocess.run(a, check=True)

STD = os.path.join(R, "dll/win32/comctl32/idb_std_small.bmp")
HIST = os.path.join(R, "dll/win32/comctl32/idb_hist_small.bmp")
VIEW = os.path.join(R, "dll/win32/comctl32/idb_view_small.bmp")
MP = os.path.join(R, "base/applications/mplay32/resources")
SH = os.path.join(R, "dll/win32/shell32/res/icons")

def strip(src, i, name):
    out = os.path.join(CROP, name + ".png")
    sh("magick", src, "-crop", f"16x16+{16*i}+0", "+repage", out)
    return out

def keyed(src, name):
    # Win32 toolbar bitmaps are keyed on their top-left pixel (LR_LOADTRANSPARENT)
    out = os.path.join(CROP, name + ".png")
    sh("magick", src, "-alpha", "set", "-fuzz", "0%", "-fill", "none",
       "-draw", "color 0,0 replace", out)
    return out

def ico16(num, name):
    out = os.path.join(CROP, name + ".png")
    f = os.path.join(SH, f"{num}.ico")
    ident = subprocess.run(["magick", "identify", f], capture_output=True, text=True).stdout.splitlines()
    idx = [i for i, l in enumerate(ident) if " 16x16 " in l][-1]
    sh("magick", f"{f}[{idx}]", out)
    return out

def N_(name, path, size=16): return {"status": "NATIVE", "name": name, "size": size, "path": path}
def C_(prims, note): return {"status": "COMPOSABLE", "prims": prims, "note": note}
def G_(note=""): return {"status": "GAP", "note": note}
def P(name, path, size=16): return [name, size, path]

s = {k: strip(STD, i, "std_" + k) for i, k in enumerate(
    "cut copy paste undo redo delete new open save printpre properties help find replace print".split())}
h = {k: strip(HIST, i, "hist_" + k) for i, k in enumerate("back forward favorites addfav viewtree".split())}
v = {k: strip(VIEW, i, "view_" + k) for i, k in enumerate(
    "large small list details sortname sortsize sortdate sorttype parent netconn netdisc newfolder".split())}
m = {k: keyed(os.path.join(MP, k + ".bmp"), "mplay_" + k) for k in
     "backward seekback play pause stop seekforw forward eject".split()}
info = ico16(1001, "sh_1001_info"); noentry = ico16(200, "sh_200_noentry")
check = ico16(253, "sh_253_check"); folder = ico16(4, "sh_4_folder")
note = ico16(225, "sh_225_audio"); lockmon = ico16(48, "sh_48_lockedmonitor")
redx = ico16(54, "sh_54_redx_doc")

reactos = {
 "DocumentSave": N_("idb_std_small[8] save", s["save"]),
 "EditUndo": N_("idb_std_small[3] undo", s["undo"]),
 "EditRedo": N_("idb_std_small[4] redo", s["redo"]),
 "MediaRecord": C_([P("mplay32 play.bmp", m["play"])], "a disc in mplay32's grey outline language (sndrec32's but_rec.bmp is a 59x29 bevelled button, not a glyph)"),
 "VcsCommit": C_([P("std new", s["new"]), P("shell32 253 check", check)], "document + check"),
 "VcsPull": C_([P("std new", s["new"]), P("view parent (up arrow, flipped)", v["parent"])], "down arrow onto document"),
 "ZoomFitBest": C_([P("std find", s["find"])], "the find magnifier with a frame"),
 "ZoomOriginal": C_([P("std find", s["find"])], "the find magnifier with 1:1"),
 "ZoomInY": C_([P("std find", s["find"]), P("view parent", v["parent"])], "magnifier + vertical arrow"),
 "ListAdd": G_("no plus glyph in the set"), "ListRemove": G_("no minus glyph in the set"),
 "ViewHidden": G_("no eye"), "InsertLink": G_("no chain"), "Merge": C_([P("std copy", s["copy"])], "two sheets converging"),
 "BlackSum": G_(), "GoJump": C_([P("std redo", s["redo"])], "curved arrow onto a dot"),
 "TimelineLift": C_([P("shell32 54 red-x doc", redx)], "brackets around the red x"),
 "MusicNote16th": C_([P("shell32 225 audio", note)], "the note cropped from the audio file icon"),
 "Mathmode": G_(), "PreviewRenderOn": G_(),
 "DialogOkApply": N_("shell32 253 check", check),
 "VcsDiff": G_(), "ShallowHistory": C_([P("view sortdate (clock)", v["sortdate"])], "the sort-by-date clock"),
 "EditSelect": C_([P("std help (pointer)", s["help"])], "the pointer from What's This"),
 "KeyframePrevious": C_([P("view sortdate", v["sortdate"]), P("mplay seekback", m["seekback"])], "clock + transport triangle"),
 "KeyframeNext": C_([P("view sortdate", v["sortdate"]), P("mplay seekforw", m["seekforw"])], "clock + transport triangle"),
 "GoPrevious": N_("idb_hist_small[0] back", h["back"]),
 "GoNext": N_("idb_hist_small[1] forward", h["forward"]),
 "DocumentRevert": C_([P("std new", s["new"]), P("std undo", s["undo"])], "document + undo arrow"),
 "MediaSkipBackward": N_("mplay32 backward.bmp", m["backward"]),
 "MediaPlaybackStart": N_("mplay32 play.bmp", m["play"]),
 "MediaPlaybackStop": N_("mplay32 stop.bmp", m["stop"]),
 "MediaPlaybackPause": N_("mplay32 pause.bmp", m["pause"]),
 "MediaSkipForward": N_("mplay32 forward.bmp", m["forward"]),
 "DialogCancel": N_("shell32 200 no-entry", noentry),
 "GoDown": C_([P("view parent (flipped)", v["parent"])], "the green up arrow flipped"),
 "GoUp": N_("idb_view_small[8] parent-folder arrow", v["parent"]),
 "Lock": C_([P("shell32 48 locked monitor", lockmon)], "the padlock cropped from the locked-monitor icon"),
 "Unlock": C_([P("shell32 48 locked monitor", lockmon)], "that padlock opened"),
 "BboxPrev": C_([P("mplay backward", m["backward"])], "arrow meeting a bar is the skip glyph's half"),
 "BboxNext": C_([P("mplay forward", m["forward"])], "arrow meeting a bar is the skip glyph's half"),
 "TabDetach": G_(), "SettingsConfigure": N_("idb_std_small[10] properties", s["properties"]),
 "Folder": N_("shell32 4 folder", folder), "AudioXWav": N_("shell32 225 audio file", note),
 "MediaRepeatSingle": G_(), "GoParentFolder": C_([P("shell32 4 folder", folder), P("view parent", v["parent"])], "folder + up arrow"),
 "DialogInformation": N_("shell32 1001 info", info),
 "DialogError": N_("shell32 200 no-entry", noentry),
 "WindowClose": C_([P("shell32 54 red-x doc", redx)], "the x without its sheet"),
 "EditCopy": N_("idb_std_small[1] copy", s["copy"]),
 "HelpWhatsthis": N_("idb_std_small[11] help (arrow + ?)", s["help"]),
 "GoJumpDeclaration": G_(), "EditDelete": N_("idb_std_small[5] delete", s["delete"]),
}

def n(f): return os.path.join(N, f)
naut = {
 "DocumentSave": G_(), "EditUndo": C_([P("Refresh.png", n("Refresh.png"), 20)], "one lobe of the refresh arrows"),
 "EditRedo": C_([P("Refresh.png", n("Refresh.png"), 20)], "one lobe of the refresh arrows"),
 "MediaRecord": G_(), "VcsCommit": G_(), "VcsPull": C_([P("Up.png (flipped)", n("Up.png"), 19)], "the up arrow flipped onto a bar"),
 "ZoomFitBest": C_([P("Search.png", n("Search.png"), 17)], "the search magnifier + frame"),
 "ZoomOriginal": C_([P("Search.png", n("Search.png"), 17)], "the search magnifier + 1:1"),
 "ZoomInY": C_([P("Search.png", n("Search.png"), 17), P("uparrow.png", n("uparrow.png"), 18)], "magnifier + chevron"),
 "ListAdd": N_("increment.png", n("increment.png"), 26), "ListRemove": N_("decrement.png", n("decrement.png"), 26),
 "ViewHidden": G_(), "InsertLink": G_(), "Merge": G_(), "BlackSum": G_(),
 "GoJump": C_([P("Forward.png", n("Forward.png"), 19)], "the forward arrow onto a dot"),
 "TimelineLift": C_([P("Stop.png", n("Stop.png"), 18)], "brackets around the stop x"),
 "MusicNote16th": C_([P("i-music-24.png", n("i-music-24.png"), 26)], "the note cropped from the music document"),
 "Mathmode": G_(), "PreviewRenderOn": G_(),
 "DialogOkApply": C_([P("emblem-OK.svg", n("emblem-OK.svg"), 40)], "the OK emblem (40 px) redrawn small"),
 "VcsDiff": G_(), "ShallowHistory": G_("no clock"), "EditSelect": G_(),
 "KeyframePrevious": G_(), "KeyframeNext": G_(),
 "GoPrevious": N_("Back.png", n("Back.png"), 19), "GoNext": N_("Forward.png", n("Forward.png"), 19),
 "DocumentRevert": G_(),
 "MediaSkipBackward": G_(), "MediaPlaybackStart": C_([P("rightarrow.png", n("rightarrow.png"), 16)], "chevron filled into a triangle"),
 "MediaPlaybackStop": G_(), "MediaPlaybackPause": G_(), "MediaSkipForward": G_(),
 "DialogCancel": N_("Stop.png", n("Stop.png"), 18),
 "GoDown": C_([P("Up.png (flipped)", n("Up.png"), 19)], "the up arrow flipped"),
 "GoUp": N_("Up.png", n("Up.png"), 19),
 "Lock": G_(), "Unlock": G_(),
 "BboxPrev": C_([P("leftarrow.png", n("leftarrow.png"), 18)], "chevron meeting a bar"),
 "BboxNext": C_([P("rightarrow.png", n("rightarrow.png"), 16)], "chevron meeting a bar"),
 "TabDetach": G_(), "SettingsConfigure": G_(),
 "Folder": N_("i-directory-24.png", n("i-directory-24.png"), 24), "AudioXWav": N_("i-music-24.png", n("i-music-24.png"), 24),
 "MediaRepeatSingle": C_([P("Refresh.png", n("Refresh.png"), 20)], "the refresh loop + a '1'"),
 "GoParentFolder": C_([P("i-directory-24.png", n("i-directory-24.png"), 24), P("Up.png", n("Up.png"), 19)], "folder + up arrow"),
 "DialogInformation": G_(), "DialogError": C_([P("tiny-alert.png", n("tiny-alert.png"), 16)], "the alert triangle"),
 "WindowClose": C_([P("Stop.png", n("Stop.png"), 18)], "the stop x without its plate"),
 "EditCopy": G_(), "HelpWhatsthis": G_(), "GoJumpDeclaration": G_(),
 "EditDelete": N_("trash-empty.png (58x44)", n("trash-empty.png"), 44),
}

json.dump(reactos, open(os.path.join(HERE, "hand_reactos.json"), "w"), indent=1)
json.dump(naut, open(os.path.join(HERE, "hand_nautilus1.json"), "w"), indent=1)
print("ok")
