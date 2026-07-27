#pragma once

// The startup color config: ~/.config/warptempo_gui/colors.conf, the one route
// by which the architect retunes the GUI palette without a rebuild.
//
// WHOLE FILE OR NOTHING, in the settings-sidecar spirit. The grammar is
// maximally strict: exactly one line per palette key, in exactly the canonical
// order the key table in color_config.cpp states, each line exactly
// `key = #rrggbb` (single spaces, lowercase hex, six digits) and terminated by
// a newline. No comments, no blank lines, no trailing content. ANY deviation —
// an unknown key, a key out of order, a duplicate, a malformed or uppercase
// value, a missing final newline, extra bytes — prints ONE stderr line naming
// the file, the 1-based line number and the reason, then REJECTS THE WHOLE
// FILE and keeps every compiled default. Never a partial adoption, never an
// exit: a bad config is a cosmetic problem and the product still runs.
//
// A MISSING FILE IS SILENT — the normal state on a fresh machine, and the
// reason nothing here ever writes the file.
//
// GUI ONLY. Colors exist only in the GUI, so the CLI never reads this (its
// source list does not carry the translation unit at all).
//
// Called ONCE from main(), before the window exists and therefore before the
// first paint and before anything derives a value from the palette. Nothing
// writes the palette afterwards, so no surface needs invalidating and the
// waveform worker's read of the ink color needs no synchronization — a retune
// is a restart, by ruling.
void load_color_config();
