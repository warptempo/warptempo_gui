#!/usr/bin/env python3
"""Icon survey: index candidate retro sets, match the warptempo roster, render
contact sheets. Report-only scratch; lives in tmp/icon_survey/."""
import glob, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SETS_DIR = os.path.join(HERE, "sets")
WORK = os.path.join(HERE, "work")
os.makedirs(WORK, exist_ok=True)

# ---------------------------------------------------------------- the roster
# (enumerator, breeze file, act, class G=generic freedesktop / E=esoteric)
ROSTER = [
 ("DocumentSave","document-save","Save (Ctrl+S)","G"),
 ("EditUndo","edit-undo","Undo","G"),
 ("EditRedo","edit-redo","Redo","G"),
 ("MediaRecord","media-record","Render / iteration sweep","G"),
 ("VcsCommit","vcs-commit","Save and Commit (history view)","E"),
 ("VcsPull","vcs-pull","Pull (history view, GitHub ahead)","E"),
 ("ZoomFitBest","zoom-fit-best","Full zoom out (0)","G"),
 ("ZoomOriginal","zoom-original","Working-zoom center (c)","G"),
 ("ZoomInY","zoom-in-y","Toggle Waveform Magnification (`)","E"),
 ("ListAdd","list-add","Drop marker (s)","G"),
 ("ListRemove","list-remove","Delete markers (Delete)","G"),
 ("ViewHidden","view-hidden","Toggle disabled (Ctrl+D)","E"),
 ("InsertLink","insert-link","Inherit tempo (Ctrl+N)","G"),
 ("Merge","merge","Flatten tempo deviations (Ctrl+F)","E"),
 ("BlackSum","black_sum","Cumulative reading (u)","E"),
 ("GoJump","go-jump","Follow (f)","E"),
 ("TimelineLift","timeline-lift","Restrict undo to current view (z)","E"),
 ("MusicNote16th","music-note-16th","BPM iterations (Ctrl+B)","E"),
 ("Mathmode","mathmode","Toggle grid iterations (i)","E"),
 ("PreviewRenderOn","preview-render-on","Listen to a render","E"),
 ("DialogOkApply","dialog-ok-apply","Load in place as baseline","G"),
 ("VcsDiff","vcs-diff","History mode (h)","E"),
 ("ShallowHistory","shallow-history","Walk lamp, lit in Session (g)","E"),
 ("EditSelect","edit-select","Add to selection","E"),
 ("KeyframePrevious","keyframe-previous","Older checkpoint (,)","E"),
 ("KeyframeNext","keyframe-next","Newer checkpoint (.)","E"),
 ("GoPrevious","go-previous","Left arrow (Left)","G"),
 ("GoNext","go-next","Right arrow (Right)","G"),
 ("DocumentRevert","document-revert","Revert selected differences (v)","G"),
 ("MediaSkipBackward","media-skip-backward","Go to start (Home)","G"),
 ("MediaPlaybackStart","media-playback-start","Play (Space)","G"),
 ("MediaPlaybackStop","media-playback-stop","Stop (Space)","G"),
 ("MediaPlaybackPause","media-playback-pause","Pause (render player)","G"),
 ("MediaSkipForward","media-skip-forward","Go to end (End)","G"),
 ("DialogCancel","dialog-cancel","Cancel a render","G"),
 ("GoDown","go-down","Down arrow (Down)","G"),
 ("GoUp","go-up","Up arrow (Up)","G"),
 ("Lock","lock","Read-only: locked","G"),
 ("Unlock","unlock","Read-only: unlocked","G"),
 ("BboxPrev","bboxprev","Previous Marker (Shift+Tab)","E"),
 ("BboxNext","bboxnext","Next Marker (Tab)","E"),
 ("TabDetach","tab-detach","Switch Tab (Ctrl+Tab)","E"),
 ("SettingsConfigure","settings-configure","Settings (;)","G"),
 ("Folder","folder","Folder row (player, picker)","G"),
 ("AudioXWav","audio-x-wav","WAV row (player)","G"),
 ("MediaRepeatSingle","media-repeat-single","Repeat one (player)","G"),
 ("GoParentFolder","go-parent-folder","Up out of a batch folder (player)","G"),
 ("DialogInformation","dialog-information","Normal card glyph","G"),
 ("DialogError","dialog-error","Critical card glyph","G"),
 ("WindowClose","window-close","Card dismiss / player Close","G"),
 ("EditCopy","edit-copy","Copy resolved value (Ctrl+C)","G"),
 ("HelpWhatsthis","help-whatsthis","Toggle Tooltips (\\)","G"),
 ("GoJumpDeclaration","go-jump-declaration","Jump to Defining Marker (Ctrl+J)","E"),
 ("EditDelete","edit-delete","Delete a batch folder (player)","G"),
]

# NATIVE name candidates per role: the same freedesktop name, the KDE 1-3 /
# GNOME 1-2 / CDE legacy name for the same act, or an obvious same-meaning icon.
NATIVE = {
 "DocumentSave": ["document-save","filesave","stock_save","gtk-save"],
 "EditUndo": ["edit-undo","undo","stock_undo","gtk-undo"],
 "EditRedo": ["edit-redo","redo","stock_redo","gtk-redo"],
 "MediaRecord": ["media-record","player_record","krec_record","stock_media-rec","gtk-media-record"],
 "VcsCommit": ["vcs-commit","git-commit","cervisia_commit"],
 "VcsPull": ["vcs-pull","vcs-update","cervisia_update"],
 "ZoomFitBest": ["zoom-fit-best","viewmagfit","view_fit_window","stock_zoom-optimal","stock_zoom-page","gtk-zoom-fit","zoom-best-fit"],
 "ZoomOriginal": ["zoom-original","viewmag1","stock_zoom-1","gtk-zoom-100","zoom-100"],
 "ZoomInY": ["zoom-in-y","zoom-fit-height","view_fit_height","stock_zoom-page-height"],
 "ListAdd": ["list-add","add","edit_add","stock_add","gtk-add","Dtplus"],
 "ListRemove": ["list-remove","remove","edit_remove","stock_remove","gtk-remove","Dtminus"],
 "ViewHidden": ["view-hidden","hidden","object-hidden","stock_hide","14_layer_novisible"],
 "InsertLink": ["insert-link","link","stock_link","Dtlink"],
 "Merge": ["merge","vcs-merge","object-merge","stock_merge","mergecell"],
 "BlackSum": ["black_sum","math_sum","sum","stock_sum","stock_insert-formula-sum"],
 "GoJump": ["go-jump","goto","gtk-jump-to","stock_jump-to","dbgjumpto"],
 "TimelineLift": ["timeline-lift"],
 "MusicNote16th": ["music-note-16th","music_sixteenthnote"],
 "Mathmode": ["mathmode","funct","frame_formula","stock_insert-formula","formula"],
 "PreviewRenderOn": ["preview-render-on"],
 "DialogOkApply": ["dialog-ok-apply","dialog-ok","apply","button_ok","ok","gtk-apply","gtk-ok","stock_ok"],
 "VcsDiff": ["vcs-diff","diff","kompare"],
 "ShallowHistory": ["shallow-history","history","Dthist"],
 "EditSelect": ["edit-select","select","tool-pointer","stock_draw-selection"],
 "KeyframePrevious": ["keyframe-previous"],
 "KeyframeNext": ["keyframe-next"],
 "GoPrevious": ["go-previous","back","1leftarrow","previous","stock_left","gtk-go-back-ltr"],
 "GoNext": ["go-next","forward","1rightarrow","next","stock_right","gtk-go-forward-ltr"],
 "DocumentRevert": ["document-revert","revert","filerevert","stock_revert","gtk-revert-to-saved"],
 "MediaSkipBackward": ["media-skip-backward","player_start","stock_media-prev","gtk-media-previous-ltr","gtk-media-previous"],
 "MediaPlaybackStart": ["media-playback-start","player_play","stock_media-play","gtk-media-play-ltr","gtk-media-play"],
 "MediaPlaybackStop": ["media-playback-stop","player_stop","stock_media-stop","gtk-media-stop"],
 "MediaPlaybackPause": ["media-playback-pause","player_pause","stock_media-pause","gtk-media-pause"],
 "MediaSkipForward": ["media-skip-forward","player_end","stock_media-next","gtk-media-next-ltr","gtk-media-next"],
 "DialogCancel": ["dialog-cancel","button_cancel","cancel","gtk-cancel","stock_cancel"],
 "GoDown": ["go-down","1downarrow","down","stock_down","gtk-go-down","Fpdown"],
 "GoUp": ["go-up","1uparrow","up","stock_up","gtk-go-up","Fpup"],
 "Lock": ["lock","object-locked","locked","encrypted","stock_lock","system-lock-screen"],
 "Unlock": ["unlock","object-unlocked","unlocked","decrypted","stock_lock-open"],
 "BboxPrev": ["bboxprev","go-first","2leftarrow","stock_first","gtk-goto-first-ltr"],
 "BboxNext": ["bboxnext","go-last","2rightarrow","stock_last","gtk-goto-last-ltr"],
 "TabDetach": ["tab-detach","tab_breakoff"],
 "SettingsConfigure": ["settings-configure","configure","preferences-system","preferences-other","gtk-preferences","stock_properties","document-properties"],
 "Folder": ["folder","i-directory","DtdirB","folder_closed","gnome-fs-directory"],
 "AudioXWav": ["audio-x-wav","gnome-audio-x-wav","audio-x-generic","sound","i-music","Dtaudio","gnome-mime-audio"],
 "MediaRepeatSingle": ["media-repeat-single","media-playlist-repeat-song","noatunloopsong","media-playlist-repeat","stock_repeat","gtk-media-repeat"],
 "GoParentFolder": ["go-parent-folder","folder_up","folder-up","Dtdirup","stock_up-folder"],
 "DialogInformation": ["dialog-information","messagebox_info","info","gtk-dialog-info","stock_dialog-info"],
 "DialogError": ["dialog-error","messagebox_critical","gtk-dialog-error","stock_dialog-error","error"],
 "WindowClose": ["window-close","fileclose","gtk-close","stock_close","close"],
 "EditCopy": ["edit-copy","editcopy","stock_copy","gtk-copy"],
 "HelpWhatsthis": ["help-whatsthis","contexthelp","whatsthis","help-contextual"],
 "GoJumpDeclaration": ["go-jump-declaration"],
 "EditDelete": ["edit-delete","editdelete","user-trash","edittrash","stock_delete","gtk-delete","Dttrsh"],
}

# COMPOSABLE recipes: every group must resolve to a set icon (first hit per
# group); the note says how the pieces combine in the set's own language.
A_L = ["go-previous","back","1leftarrow","stock_left","Dthistprev"]
A_R = ["go-next","forward","1rightarrow","stock_right","Dthistnext"]
A_U = ["go-up","1uparrow","up","stock_up","Fpup"]
A_D = ["go-down","1downarrow","down","stock_down","Fpdown"]
PLAY = ["media-playback-start","player_play","stock_media-play","gtk-media-play-ltr"]
STOP = ["media-playback-stop","player_stop","stock_media-stop"]
CLOCK = ["clock","kclock","xclock","appointment-soon","player_time","smallclock","alarm-clock","chronometer","clock","stock_timer","Dtclock","Fpclock","x-office-calendar-clock","document-open-recent","appointment"]
DOC = ["text-x-generic","document-new","filenew","empty","stock_new","gtk-new","Dtdata"]
REDX = ["process-stop","dialog-error","no","stock_stop","gtk-stop","edit-delete","dialog-cancel","window-close"]
MAGN = ["zoom-in","viewmag+","viewmag","stock_zoom-in","gtk-zoom-in","system-search","edit-find"]
FOLDER = ["folder","i-directory","DtdirB"]
EYE = ["view-visible","eye","image-red-eye","mate-eyes-applet","xeyes","14_layer_visible","stock_show","visible","show"]
POINTER = ["pointer","tool-pointer","selecttool","14_select","stock_draw-selection","input-mouse","mouse","stock_draw-selection","DtMouse"]
FLAG = ["flag","edit-flag","bookmark","emblem-important","bookmark-new","DtFlag","stock_mark"]
NOTE = ["music_eightnote","music_quarternote","audio-x-generic","sound","i-music","Dtaudio","audio-x-wav","gnome-audio"]
HELP = ["help-contents","help","help-browser","gtk-help","stock_help","Dthelp"]
TWODOC = ["edit-copy","window_duplicate","tab_duplicate","stock_copy"]
PLUS = ["list-add","add","edit_add","stock_add","Dtplus"]
MINUS = ["list-remove","remove","stock_remove","Dtminus"]
CHECK = ["dialog-ok-apply","dialog-ok","apply","ok","button_ok","gtk-apply"]
GEAR = ["gear","configure","preferences-system","gtk-preferences","stock_properties","Dtstyle"]
MONITOR = ["video-display","display","computer","monitor","Dtdisplay","preferences-desktop-display"]
RECORD = ["media-record","player_record","krec_record","stock_media-rec"]
UNDO = ["edit-undo","undo","stock_undo"]
REDO = ["edit-redo","redo","stock_redo"]
TAB = ["tab-new","tab_new","window-new","window_new","stock_new-window"]
MATHX = ["math_frac","frac","math_matrix","stock_insert-formula","sqrt","math_sqrt"]
LOCKS = ["lock","object-locked","encrypted","locked","system-lock-screen"]
UNLOCKS = ["unlock","object-unlocked","decrypted","unlocked"]

RECIPE = {
 "DocumentSave": ([DOC], "a document/disk from the set's document glyph"),
 "EditUndo": ([A_L], "curved form of the set's arrow"),
 "EditRedo": ([UNDO], "the set's undo arrow mirrored"),
 "MediaRecord": ([PLAY], "a red disc in the set's transport style"),
 "VcsCommit": ([DOC, CHECK], "the set's document + its check mark (commit = sign off the state)"),
 "VcsPull": ([A_D, DOC], "the set's down arrow onto its document (take in what GitHub has)"),
 "ZoomFitBest": ([MAGN], "the set's magnifier with a frame/four arrows"),
 "ZoomOriginal": ([MAGN], "the set's magnifier with '1:1'"),
 "ZoomInY": ([MAGN, A_U], "the set's magnifier + its up/down arrow pair (vertical)"),
 "ListAdd": ([MINUS], "the set's minus with the vertical bar added"),
 "ListRemove": ([PLUS], "the set's plus without its vertical bar"),
 "ViewHidden": ([EYE, REDX], "the set's eye + its red cross / slash"),
 "InsertLink": ([], ""),
 "Merge": ([TWODOC, A_R], "the set's two sheets + its arrow converging to one"),
 "BlackSum": ([MATHX], "a Sigma in the set's math-glyph style"),
 "GoJump": ([REDO], "the set's curved arrow landing on a dot"),
 "TimelineLift": ([REDX], "two bracket strokes around the set's red cross"),
 "MusicNote16th": ([NOTE], "the note cropped/redrawn from the set's audio glyph"),
 "Mathmode": ([MATHX], "an italic f(x) in the set's math-glyph style"),
 "PreviewRenderOn": ([MONITOR, RECORD], "the set's monitor with its red record disc as the 'on' pip"),
 "DialogOkApply": ([], ""),
 "VcsDiff": ([TWODOC, PLUS], "the set's two sheets + its plus/minus"),
 "ShallowHistory": ([CLOCK], "the set's clock face"),
 "EditSelect": ([POINTER], "the set's pointer over a dotted marquee corner"),
 "KeyframePrevious": ([CLOCK, PLAY], "the set's clock + its transport triangle pointing left"),
 "KeyframeNext": ([CLOCK, PLAY], "the set's clock + its transport triangle pointing right"),
 "GoPrevious": ([A_R], "the set's right arrow mirrored"),
 "GoNext": ([A_L], "the set's left arrow mirrored"),
 "DocumentRevert": ([DOC, UNDO], "the set's document + its undo arrow"),
 "MediaSkipBackward": ([PLAY], "the set's play triangle mirrored + a bar"),
 "MediaPlaybackStart": ([STOP], "a triangle in the set's transport style"),
 "MediaPlaybackStop": ([PLAY], "a square in the set's transport style"),
 "MediaPlaybackPause": ([STOP], "two bars cut from the set's stop square"),
 "MediaSkipForward": ([PLAY], "the set's play triangle + a bar"),
 "DialogCancel": ([REDX], "the set's red cross / stop sign"),
 "GoDown": ([A_U], "the set's up arrow flipped"),
 "GoUp": ([A_D], "the set's down arrow flipped"),
 "Lock": ([UNLOCKS], "the set's open padlock with its shackle closed"),
 "Unlock": ([LOCKS], "the set's padlock with its shackle opened"),
 "BboxPrev": ([A_L], "the set's left arrow meeting a bar"),
 "BboxNext": ([A_R], "the set's right arrow meeting a bar"),
 "TabDetach": ([TAB], "the set's new-tab/window glyph with the tab lifted apart"),
 "SettingsConfigure": ([GEAR], "the set's gear/tool"),
 "Folder": ([], ""),
 "AudioXWav": ([NOTE], "the set's audio document"),
 "MediaRepeatSingle": ([REDO], "the set's curved arrow closed into a loop + a '1'"),
 "GoParentFolder": ([FOLDER, A_U], "the set's folder + its up arrow"),
 "DialogInformation": ([HELP], "an 'i' in the set's help-balloon style"),
 "DialogError": ([REDX], "the set's red stop/cross"),
 "WindowClose": ([REDX], "the set's cross without its red plate"),
 "EditCopy": ([DOC], "two of the set's documents stacked"),
 "HelpWhatsthis": ([HELP, POINTER], "the set's help '?' + its pointer arrow"),
 "GoJumpDeclaration": ([FLAG, A_L], "the set's flag + its back arrow"),
 "EditDelete": ([], ""),
}

SIZE_PREF = [16, 22, 24, 20, 32, 48]


def add(idx, name, size, path):
    idx.setdefault(name, []).append((size, path))


def index_freedesktop(root, pattern_dirs):
    """root/<ctx>/<size>/name.ext or root/<NxN>/<ctx>/name.ext."""
    idx = {}
    for f in glob.glob(os.path.join(root, "**", "*.*"), recursive=True):
        if not re.search(r"\.(png|svg|svgz|xpm)$", f):
            continue
        if "@2x" in f:
            continue
        parts = f[len(root)+1:].split(os.sep)
        size = None
        for p in parts[:-1]:
            m = re.fullmatch(r"(\d+)(x\d+)?", p)
            if m:
                size = int(m.group(1))
            elif p == "scalable":
                size = 999
        if size is None:
            continue
        name = re.sub(r"\.(png|svg|svgz|xpm)$", "", parts[-1])
        add(idx, name, size, f)
    return idx


def index_crystal(root):
    idx = {}
    for f in glob.glob(os.path.join(root, "cr*-*-*.*")):
        m = re.match(r"cr(\d+|sc)-([a-z]+)-(.*)\.(png|svgz)$", os.path.basename(f))
        if not m:
            continue
        size = 999 if m.group(1) == "sc" else int(m.group(1))
        add(idx, m.group(3), size, f)
    return idx


def index_cde(root):
    idx = {}
    sz = {"t": 16, "s": 24, "m": 32, "l": 48}
    for f in glob.glob(os.path.join(root, "*.pm")):
        m = re.match(r"(.*)\.([tsml])\.pm$", os.path.basename(f))
        if m:
            add(idx, m.group(1), sz[m.group(2)], f)
        else:
            add(idx, os.path.basename(f)[:-3], 32, f)
    return idx


def merge_idx(*idxs):
    out = {}
    for i in idxs:
        for k, v in i.items():
            out.setdefault(k, []).extend(v)
    return out


def pick(entries):
    by = {}
    for s, p in entries:
        by.setdefault(s, p)
    for s in SIZE_PREF:
        if s in by:
            return s, by[s]
    if 999 in by:
        return 999, by[999]
    s = min(by)
    return s, by[s]


def lookup(idx, names):
    for n in names:
        if n in idx:
            s, p = pick(idx[n])
            return n, s, p
    return None


def build_sets():
    S = SETS_DIR
    sets = {}
    sets["chicago95"] = index_freedesktop(os.path.join(S, "chicago95/Icons/Chicago95"), None)
    sets["se98"] = index_freedesktop(os.path.join(S, "win98se/SE98"), None)
    bc = index_freedesktop(os.path.join(S, "bluecurve/icons/icon-set/Bluecurve"), None)
    # Bluecurve ships its freedesktop names as install-time symlinks declared in
    # icons/icon-xml/*.icontheme (<alias name= target=>); fold them in.
    for x in glob.glob(os.path.join(S, "bluecurve/icons/icon-xml/*.icontheme")):
        for m in re.finditer(r'<alias\s+name="([^"]+)"\s+target="([^"]+)"', open(x).read()):
            n, t = m.group(1), m.group(2)
            if t in bc and n not in bc:
                bc[n] = list(bc[t])
    sets["bluecurve"] = bc
    sets["crystalsvg"] = index_crystal(os.path.join(S, "tdelibs/pics/crystalsvg"))
    kc = index_freedesktop(os.path.join(S, "tdeartwork/IconThemes/kdeclassic"), None)
    sets["kdeclassic"] = kc
    lo = index_freedesktop(os.path.join(S, "tdeartwork/IconThemes/locolor"), None)
    sets["locolor"] = lo  # own files only; it Inherits=kdeclassic at runtime
    sets["cde"] = index_cde(os.path.join(S, "cde/cde/programs/icons"))
    return sets


def classify(sets, overrides):
    result = {}
    for sname, idx in sets.items():
        rows = {}
        for enum, bfile, act, cls in ROSTER:
            ov = overrides.get(sname, {}).get(enum)
            hit = lookup(idx, NATIVE[enum])
            if ov and ov.get("status") == "NATIVE" and ov.get("name"):
                h2 = lookup(idx, [ov["name"]])
                if h2:
                    hit = h2
            if ov and ov.get("status") in ("COMPOSABLE", "GAP"):
                hit = None
            if hit:
                rows[enum] = {"status": "NATIVE", "name": hit[0], "size": hit[1], "path": hit[2],
                              "note": (ov or {}).get("note", "")}
                continue
            groups, note = RECIPE[enum]
            prims = []
            ok = bool(groups)
            for g in groups:
                h = lookup(idx, g)
                if not h:
                    ok = False
                    break
                prims.append(h)
            if ov and ov.get("status") == "GAP":
                ok = False
            if ov and ov.get("prims"):
                prims = [lookup(idx, [n]) for n in ov["prims"]]
                ok = all(prims)
            if ok:
                rows[enum] = {"status": "COMPOSABLE", "prims": prims,
                              "note": (ov or {}).get("note", note)}
            else:
                rows[enum] = {"status": "GAP", "note": (ov or {}).get("note", "")}
        result[sname] = rows
    return result


if __name__ == "__main__":
    sets = build_sets()
    ov_path = os.path.join(HERE, "overrides.json")
    overrides = json.load(open(ov_path)) if os.path.exists(ov_path) else {}
    # hand-mapped sets (ReactOS, Nautilus 1.0) are injected as ready-made rows
    res = classify(sets, overrides)
    for extra in ("reactos", "nautilus1"):
        p = os.path.join(HERE, f"hand_{extra}.json")
        if os.path.exists(p):
            res[extra] = json.load(open(p))
    json.dump(res, open(os.path.join(HERE, "coverage.json"), "w"), indent=1)
    for s, rows in res.items():
        c = {"NATIVE": 0, "COMPOSABLE": 0, "GAP": 0}
        for r in rows.values():
            c[r["status"]] += 1
        print(s, c, "index size", len(sets.get(s, {})))
