# The icon pass: each button -> its Chicago95 16-px icon (architect picks, 2026-10-05)

Ink box (w x h) and its margins in icon px (L, T, R, B); dx, dy = the ink centre's offset from the cell centre (+ = right / down). Off-centre = |dx| or |dy| >= 1.

| button | tooltip | place | Breeze today | Chicago95 | ink | L | T | R | B | dx | dy | flag |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Save | Save | icon row (toolbar, 1st) | document-save | actions/document-save | 14x14 | 1 | 1 | 1 | 1 | 0.0 | 0.0 |  |
| Save (commit face) | Save and Commit | icon row, Save while `h` history stands | vcs-commit | actions/document-send | 16x11 | 0 | 2 | 0 | 3 | 0.0 | -0.5 |  |
| Save (pull face) | Pull | icon row, Save while GitHub is ahead | vcs-pull | actions/go-bottom | 13x16 | 1 | 0 | 2 | 0 | -0.5 | 0.0 |  |
| Undo | Undo | icon row (toolbar) | edit-undo | actions/edit-undo | 12x7 | 2 | 4 | 2 | 5 | 0.0 | -0.5 |  |
| Redo | Redo | icon row (toolbar) | edit-redo | actions/edit-redo | 12x7 | 2 | 4 | 2 | 5 | 0.0 | -0.5 |  |
| IconCopyValue | Copy Resolved Value | icon row (after Redo) | edit-copy | actions/edit-copy | 15x13 | 1 | 2 | 0 | 1 | 0.5 | 0.5 |  |
| Render | Render | icon row (own group) | media-record | actions/media-record | 9x9 | 3 | 4 | 4 | 3 | -0.5 | 0.5 |  |
| Render (cancel face) | Cancel | icon row, Render mid-render | dialog-cancel | actions/dialog-cancel | 12x10 | 2 | 3 | 2 | 3 | 0.0 | 0.0 |  |
| IconZoomFitBest | Full Zoom Out | icon row (zoom group) | zoom-fit-best | actions/zoom-fit-best | 15x15 | 0 | 0 | 1 | 1 | -0.5 | -0.5 |  |
| IconWaveformMagnification | Toggle Waveform Magnification | icon row (zoom group) | zoom-in-y | actions/zoom-in | 15x15 | 0 | 0 | 1 | 1 | -0.5 | -0.5 |  |
| IconFollow | Toggle Follow | icon row (zoom group) | go-jump | actions/view-refresh | 13x16 | 1 | 0 | 2 | 0 | -0.5 | 0.0 |  |
| IconRestrictUndo | Toggle Restrict Undo to Current View | icon row (zoom group) | timeline-lift | actions/view-pin | 11x7 | 2 | 4 | 3 | 5 | -0.5 | -0.5 |  |
| IconBpm | BPM Iterations | icon row (iteration group) | music-note-16th | devices/music-player | 16x16 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |  |
| IconIter | Toggle Grid Iterations | icon row (iteration group) | mathmode | actions/view-grid | 14x12 | 1 | 2 | 1 | 2 | 0.0 | 0.0 |  |
| IconFlatten | Flatten | icon row (own group) | merge | actions/object-group | 14x14 | 1 | 1 | 1 | 1 | 0.0 | 0.0 |  |
| IconListen | Play Renders | icon row (own group) | preview-render-on | actions/media-playback-start | 9x9 | 3 | 3 | 4 | 4 | -0.5 | -0.5 |  |
| IconReadOnly (locked) | Toggle Read-Only | icon row (own group) | lock | actions/lock | 12x15 | 2 | 0 | 2 | 1 | 0.0 | -0.5 |  |
| IconReadOnly (unlocked face) | Toggle Read-Only | icon row, lock released | unlock | status/stock_lock-open | 14x15 | 1 | 0 | 1 | 1 | 0.0 | -0.5 |  |
| IconSettings | Settings | icon row (settings pair) | settings-configure | emblems/emblem-system | 16x16 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |  |
| IconTooltips | Toggle Tooltips | icon row (settings pair) | help-whatsthis | actions/help-hint | 16x14 | 0 | 1 | 0 | 1 | 0.0 | 0.0 |  |
| IconHistory | Toggle History View | icon row (own group) | vcs-diff | actions/document-open-recent | 15x13 | 0 | 1 | 1 | 2 | -0.5 | -0.5 |  |
| HistoryOlder | Older | icon row, history step group (stands in for Undo group) | keyframe-previous | actions/media-skip-backward | 12x9 | 2 | 3 | 2 | 4 | 0.0 | -0.5 |  |
| HistoryNewer | Newer | icon row, history step group | keyframe-next | actions/media-skip-forward | 12x9 | 2 | 3 | 2 | 4 | 0.0 | -0.5 |  |
| HistoryRevert | Revert | icon row, history step group | document-revert | actions/document-revert | 13x15 | 1 | 0 | 2 | 1 | -0.5 | -0.5 |  |
| IconLoadInPlace | Load in Place | icon row, history step group | dialog-ok-apply | actions/dialog-ok-apply | 12x12 | 2 | 2 | 2 | 2 | 0.0 | 0.0 |  |
| HistoryWalk | Toggle History Walk | icon row, history reading group (stands in for BPM/Iter) | shallow-history | actions/view-dual | 14x12 | 1 | 2 | 1 | 2 | 0.0 | 0.0 |  |
| HistoryCumulative | Toggle Cumulative | icon row, history reading group | black_sum | actions/view-sort-ascending | 16x14 | 0 | 1 | 0 | 1 | 0.0 | 0.0 | 2026-10-05: object-group went to Flatten (candidate 2; object-merge has no 16-px art), so Cumulative took this fresh pick ("one picture per act, pick something that even remotely resembles cumulative"); a placeholder, a Fable redraw pending |
| ViewSW | Source+Warp | icon row, view group (flush right) | document-export | actions/document-export | 13x15 | 1 | 0 | 2 | 1 | -0.5 | -0.5 |  |
| ViewTW | Target+Warp | icon row, view group | document-import | actions/document-import | 13x15 | 1 | 0 | 2 | 1 | -0.5 | -0.5 |  |
| ViewTP | Target+Phase | icon row, view group | chronometer-start | apps/clock | 16x16 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |  |
| TransportSkipBack | Go to Start | row 8, transport group | media-skip-backward | actions/media-skip-backward | 12x9 | 2 | 3 | 2 | 4 | 0.0 | -0.5 |  |
| TransportPlayStop (play face) | Play | row 8, transport group | media-playback-start | actions/media-playback-start | 9x9 | 3 | 3 | 4 | 4 | -0.5 | -0.5 |  |
| TransportPlayStop (stop face) | Stop (while an audition runs) | row 8, transport group | media-playback-stop | actions/media-playback-stop | 8x8 | 4 | 4 | 4 | 4 | 0.0 | 0.0 |  |
| TransportSkipForward | Go to End | row 8, transport group | media-skip-forward | actions/media-skip-forward | 12x9 | 2 | 3 | 2 | 4 | 0.0 | -0.5 |  |
| IconMarkerDrop | Drop Marker | row 8, marker verb group | list-add | actions/list-add | 10x10 | 3 | 3 | 3 | 3 | 0.0 | 0.0 |  |
| IconMarkerDelete | Delete Markers | row 8, marker verb group | list-remove | actions/list-remove | 10x2 | 3 | 7 | 3 | 7 | 0.0 | 0.0 |  |
| IconMarkerDisable | Toggle Disabled | row 8, marker verb group | view-hidden | actions/action-unavailable | 14x14 | 1 | 1 | 1 | 1 | 0.0 | 0.0 |  |
| IconMarkerInherit | Toggle Inherit | row 8, marker verb group | insert-link | actions/insert-link | 16x16 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |  |
| IconJumpToDefiningMarker | Jump to Defining Marker | row 8, marker verb group | go-jump-declaration | actions/go-jump | 14x11 | 1 | 3 | 1 | 2 | 0.0 | 0.5 |  |
| IconAddToSelection | Toggle Add to Selection | row 8, marker verb group | edit-select | actions/edit-select | 9x16 | 3 | 0 | 4 | 0 | -0.5 | 0.0 |  |
| TransportWalkPrevious | Previous Marker | row 8, walk group | bboxprev | actions/go-previous | 15x13 | 1 | 2 | 0 | 1 | 0.5 | 0.5 |  |
| TransportWalk | Next Marker | row 8, walk group | bboxnext | actions/go-next | 15x13 | 0 | 2 | 1 | 1 | -0.5 | 0.5 |  |
| IconZoomOriginal | Center on Focus | row 8, walk group | zoom-original | actions/zoom-original | 15x15 | 0 | 0 | 1 | 1 | -0.5 | -0.5 |  |
| TransportSwitchTab | Switch Tab | row 8, walk group | tab-detach | actions/view-paged | 14x16 | 1 | 0 | 1 | 0 | 0.0 | 0.0 |  |
| TransportDown | Down | row 8, arrow group | go-down | actions/go-down | 13x15 | 2 | 0 | 1 | 1 | 0.5 | -0.5 |  |
| TransportUp | Up | row 8, arrow group | go-up | actions/go-up | 13x15 | 2 | 1 | 1 | 0 | 0.5 | 0.5 |  |
| TransportLeft | Left | row 8, arrow group | go-previous | actions/go-previous | 15x13 | 1 | 2 | 0 | 1 | 0.5 | 0.5 |  |
| TransportRight | Right | row 8, arrow group | go-next | actions/go-next | 15x13 | 0 | 2 | 1 | 1 | -0.5 | 0.5 |  |
| PlayerButtonAct::Home | Go to Start / Previous File | render player, transport | media-skip-backward | actions/media-skip-backward | 12x9 | 2 | 3 | 2 | 4 | 0.0 | -0.5 |  |
| PlayerButtonAct::PlayPause (pause face) | Pause | render player, transport | media-playback-pause | actions/media-playback-pause | 6x8 | 5 | 4 | 5 | 4 | 0.0 | 0.0 |  |
| PlayerButtonAct::NextTrack | Next Track | render player, transport | media-skip-forward | actions/media-skip-forward | 12x9 | 2 | 3 | 2 | 4 | 0.0 | -0.5 |  |
| PlayerButtonAct::RepeatOne | Toggle Repeat One | render player, modal row | media-repeat-single | status/media-playlist-repeat | 12x9 | 2 | 4 | 2 | 3 | 0.0 | 0.5 |  |
| PlayerButtonAct::Up | Up a Folder | render player, modal row | go-parent-folder | actions/go-up | 13x15 | 2 | 1 | 1 | 0 | 0.5 | 0.5 |  |
| PlayerButtonAct::Delete | Delete Folder | render player, modal row (at the root) | edit-delete | actions/edit-delete | 13x13 | 2 | 1 | 1 | 2 | 0.5 | -0.5 |  |
| PlayerButtonAct::LoadInPlace | Load in Place | render player, modal row (inside a batch folder) | dialog-ok-apply | actions/dialog-ok-apply | 12x12 | 2 | 2 | 2 | 2 | 0.0 | 0.0 |  |
| PlayerButtonAct::Close | Close | render player, modal row (last) | window-close | actions/window-close | 12x10 | 2 | 3 | 2 | 3 | 0.0 | 0.0 |  |
| [non-button] folder row | a folder (folder overlay / player root) | folder overlay list row | folder | places/folder | 16x14 | 0 | 1 | 0 | 1 | 0.0 | 0.0 |  |
| [non-button] wav row | a wav (folder overlay / player) | folder overlay list row | audio-x-wav | mimes/audio-x-wav | 16x16 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |  |
| [non-button] NORMAL card | card glyph | notification card | dialog-information | status/dialog-information | 16x16 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |  |
| [non-button] CRITICAL card | card glyph | notification card | dialog-error | status/dialog-error | 16x16 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |  |
