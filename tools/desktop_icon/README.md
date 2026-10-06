# Desktop icon

`gen_desktop_icon.py` writes `packaging/warptempo_gui.svg`, the Linux desktop icon (the `.desktop`'s `Icon=`), from `assets/icons/chicago95/16/audio-volume-high.png`: one rect per opaque pixel in a 16 x 16 viewBox under `shape-rendering="crispEdges"` (`python3 tools/desktop_icon/gen_desktop_icon.py`; regenerate, never hand-edit). Pure Python; a standalone utility with no link path from any product target.
