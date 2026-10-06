#include "icons.h"

// THE LINUX BINARY CARRIES ITS OWN CHICAGO95 BITMAPS (architect 2026-10-05,
// the icon pass), gui_font_embedded.cpp's own mechanism exactly: the 52 PNGs
// under assets/icons/chicago95/16/ (icons.h's kChicago95Files, in its order)
// are compiled into the executable here, so the laptop depends on no
// installed icon theme. The Wayland backend hands these to
// icons::install_chicago95_bitmaps once, at the head of GuiPlatform::init,
// beside gui_font_install_bundled. THIS FILE IS NOT IN THE ANDROID TARGET
// (the APK ships the same files as assets, read by its backend out of the
// package) nor in warptempo_cli, which paints nothing.
//
// THE MECHANISM: each `.inc` is the PNG's bytes as a comma-separated list of
// hex literals, written into the build tree at CONFIGURE time by the Linux
// target's icon step (CMakeLists.txt) with CMake's own file(READ … HEX), the
// configure re-running whenever a PNG changes — the font step's own road,
// picked there over C++26's #embed and over `ld -r -b binary` for the same
// reasons (gui_font_embedded.cpp's head).
//
// THE ARRAYS ARE THE FILES, BYTE FOR BYTE: cairo's PNG reader is handed an
// explicit length, so nothing is appended, and install_chicago95_bitmaps
// copies the bytes it keeps.

namespace icons {
namespace {

const uint8_t k_action_unavailable[] = {
#include "action-unavailable.png.inc"
};
const uint8_t k_audio_volume_high[] = {
#include "audio-volume-high.png.inc"
};
const uint8_t k_audio_x_wav[] = {
#include "audio-x-wav.png.inc"
};
const uint8_t k_clock[] = {
#include "clock.png.inc"
};
const uint8_t k_dialog_cancel[] = {
#include "dialog-cancel.png.inc"
};
const uint8_t k_dialog_error[] = {
#include "dialog-error.png.inc"
};
const uint8_t k_dialog_information[] = {
#include "dialog-information.png.inc"
};
const uint8_t k_dialog_ok_apply[] = {
#include "dialog-ok-apply.png.inc"
};
const uint8_t k_document_export[] = {
#include "document-export.png.inc"
};
const uint8_t k_document_import[] = {
#include "document-import.png.inc"
};
const uint8_t k_document_open_recent[] = {
#include "document-open-recent.png.inc"
};
const uint8_t k_document_revert[] = {
#include "document-revert.png.inc"
};
const uint8_t k_document_save[] = {
#include "document-save.png.inc"
};
const uint8_t k_document_send[] = {
#include "document-send.png.inc"
};
const uint8_t k_edit_copy[] = {
#include "edit-copy.png.inc"
};
const uint8_t k_edit_delete[] = {
#include "edit-delete.png.inc"
};
const uint8_t k_edit_redo[] = {
#include "edit-redo.png.inc"
};
const uint8_t k_edit_select[] = {
#include "edit-select.png.inc"
};
const uint8_t k_edit_undo[] = {
#include "edit-undo.png.inc"
};
const uint8_t k_emblem_system[] = {
#include "emblem-system.png.inc"
};
const uint8_t k_folder[] = {
#include "folder.png.inc"
};
const uint8_t k_go_bottom[] = {
#include "go-bottom.png.inc"
};
const uint8_t k_go_down[] = {
#include "go-down.png.inc"
};
const uint8_t k_go_jump[] = {
#include "go-jump.png.inc"
};
const uint8_t k_go_next[] = {
#include "go-next.png.inc"
};
const uint8_t k_go_previous[] = {
#include "go-previous.png.inc"
};
const uint8_t k_go_up[] = {
#include "go-up.png.inc"
};
const uint8_t k_help_hint[] = {
#include "help-hint.png.inc"
};
const uint8_t k_insert_link[] = {
#include "insert-link.png.inc"
};
const uint8_t k_list_add[] = {
#include "list-add.png.inc"
};
const uint8_t k_list_remove[] = {
#include "list-remove.png.inc"
};
const uint8_t k_lock[] = {
#include "lock.png.inc"
};
const uint8_t k_media_playback_pause[] = {
#include "media-playback-pause.png.inc"
};
const uint8_t k_media_playback_start[] = {
#include "media-playback-start.png.inc"
};
const uint8_t k_media_playback_stop[] = {
#include "media-playback-stop.png.inc"
};
const uint8_t k_media_playlist_repeat[] = {
#include "media-playlist-repeat.png.inc"
};
const uint8_t k_media_record[] = {
#include "media-record.png.inc"
};
const uint8_t k_media_skip_backward[] = {
#include "media-skip-backward.png.inc"
};
const uint8_t k_media_skip_forward[] = {
#include "media-skip-forward.png.inc"
};
const uint8_t k_music_player[] = {
#include "music-player.png.inc"
};
const uint8_t k_object_group[] = {
#include "object-group.png.inc"
};
const uint8_t k_stock_lock_open[] = {
#include "stock_lock-open.png.inc"
};
const uint8_t k_view_dual[] = {
#include "view-dual.png.inc"
};
const uint8_t k_view_grid[] = {
#include "view-grid.png.inc"
};
const uint8_t k_view_paged[] = {
#include "view-paged.png.inc"
};
const uint8_t k_view_pin[] = {
#include "view-pin.png.inc"
};
const uint8_t k_view_refresh[] = {
#include "view-refresh.png.inc"
};
const uint8_t k_view_sort_ascending[] = {
#include "view-sort-ascending.png.inc"
};
const uint8_t k_window_close[] = {
#include "window-close.png.inc"
};
const uint8_t k_zoom_fit_best[] = {
#include "zoom-fit-best.png.inc"
};
const uint8_t k_zoom_in[] = {
#include "zoom-in.png.inc"
};
const uint8_t k_zoom_original[] = {
#include "zoom-original.png.inc"
};

} // namespace

const Chicago95Bytes chicago95_embedded_files[kChicago95FileCount] = {
    {k_action_unavailable, sizeof(k_action_unavailable)},
    {k_audio_volume_high, sizeof(k_audio_volume_high)},
    {k_audio_x_wav, sizeof(k_audio_x_wav)},
    {k_clock, sizeof(k_clock)},
    {k_dialog_cancel, sizeof(k_dialog_cancel)},
    {k_dialog_error, sizeof(k_dialog_error)},
    {k_dialog_information, sizeof(k_dialog_information)},
    {k_dialog_ok_apply, sizeof(k_dialog_ok_apply)},
    {k_document_export, sizeof(k_document_export)},
    {k_document_import, sizeof(k_document_import)},
    {k_document_open_recent, sizeof(k_document_open_recent)},
    {k_document_revert, sizeof(k_document_revert)},
    {k_document_save, sizeof(k_document_save)},
    {k_document_send, sizeof(k_document_send)},
    {k_edit_copy, sizeof(k_edit_copy)},
    {k_edit_delete, sizeof(k_edit_delete)},
    {k_edit_redo, sizeof(k_edit_redo)},
    {k_edit_select, sizeof(k_edit_select)},
    {k_edit_undo, sizeof(k_edit_undo)},
    {k_emblem_system, sizeof(k_emblem_system)},
    {k_folder, sizeof(k_folder)},
    {k_go_bottom, sizeof(k_go_bottom)},
    {k_go_down, sizeof(k_go_down)},
    {k_go_jump, sizeof(k_go_jump)},
    {k_go_next, sizeof(k_go_next)},
    {k_go_previous, sizeof(k_go_previous)},
    {k_go_up, sizeof(k_go_up)},
    {k_help_hint, sizeof(k_help_hint)},
    {k_insert_link, sizeof(k_insert_link)},
    {k_list_add, sizeof(k_list_add)},
    {k_list_remove, sizeof(k_list_remove)},
    {k_lock, sizeof(k_lock)},
    {k_media_playback_pause, sizeof(k_media_playback_pause)},
    {k_media_playback_start, sizeof(k_media_playback_start)},
    {k_media_playback_stop, sizeof(k_media_playback_stop)},
    {k_media_playlist_repeat, sizeof(k_media_playlist_repeat)},
    {k_media_record, sizeof(k_media_record)},
    {k_media_skip_backward, sizeof(k_media_skip_backward)},
    {k_media_skip_forward, sizeof(k_media_skip_forward)},
    {k_music_player, sizeof(k_music_player)},
    {k_object_group, sizeof(k_object_group)},
    {k_stock_lock_open, sizeof(k_stock_lock_open)},
    {k_view_dual, sizeof(k_view_dual)},
    {k_view_grid, sizeof(k_view_grid)},
    {k_view_paged, sizeof(k_view_paged)},
    {k_view_pin, sizeof(k_view_pin)},
    {k_view_refresh, sizeof(k_view_refresh)},
    {k_view_sort_ascending, sizeof(k_view_sort_ascending)},
    {k_window_close, sizeof(k_window_close)},
    {k_zoom_fit_best, sizeof(k_zoom_fit_best)},
    {k_zoom_in, sizeof(k_zoom_in)},
    {k_zoom_original, sizeof(k_zoom_original)},
};

} // namespace icons
