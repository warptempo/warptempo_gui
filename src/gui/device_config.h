#pragma once

#include "failure.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// THE DEVICE CONFIG — the preferences that describe the MACHINE rather than the
// piece (architect 2026-08-27). Thirteen keys live here and nowhere else,
// EVERY ONE REQUIRED (architect 2026-10-12, his "yes" 2026-10-14; the
// strictness paragraph below):
//
//   gui_scale=<percent>      the GUI's one scale axis, an integer [50, 1000]
//   projects_repo=<host/path> the repository that is the PROJECTS HOME — the
//                            GitHub recheck's corpus; free text, may be blank
//   projects_path=<path>     the ABSOLUTE folder whose subfolders are the
//                            projects (project_model.h owns the model)
//   last_project=<name>      the folder NAME opened last, written at every
//                            successful open; blank until the first
//   waveform=<name>          THE WAVEFORM PRESET the program's colors are
//                            painted in — the waveform, its canvas, the
//                            grid and center lines, the cues and the
//                            playhead (palette_file.h's head; its Face
//                            unread under the materials since 2026-10-21):
//                            a built-in, one of the twenty presets'
//                            keys alone
//                            (`default`, `xp-blue`, ...; no prefix since
//                            2026-10-27; is_builtin_palette_name,
//                            palette_file.h; a
//                            preset file's name until 2026-10-14), chosen by
//                            the Settings Waveform row; REQUIRED (architect
//                            2026-10-14; absent meant the default
//                            2026-10-07..14), the default SPELLED: both
//                            first-run templates write
//                            `waveform=default`, "Default" (architect
//                            2026-10-26; kGuiDefaultPaletteKey,
//                            palette_file.h). Named
//                            `palette` until 2026-10-13 and `chrome` until
//                            2026-10-21 (the key `chrome` of 2026-10-07 to
//                            2026-10-10 was another thing, retired); an old
//                            `palette=` or `chrome=` line is the ordinary
//                            unknown-key hard fail. (THE `caption` KEY — the
//                            title bar's preset, `scheme` until 2026-10-13,
//                            one of 42 built-in Windows and Plus! schemes or
//                            the product's own `default` — stood before it
//                            2026-10-08..10-21 and left with the captions:
//                            the app paints no caption since 2026-10-24,
//                            render.h's caption note; an old
//                            `caption=` line is the ordinary unknown-key
//                            hard fail.)
//   true_colors=<on|off>     THE sRGB -> DISPLAY-P3 CONVERSION'S SWITCH
//                            (architect 2026-10-21, the Settings menu's
//                            "True Colors" command row made a key with the
//                            popup's retirement; the ruling at
//                            display_transform.h's head): `on` or `off`, the
//                            two lowercase words and nothing else, any other
//                            word the launch's hard fail; REQUIRED, read at
//                            launch to seed the switch and applied LIVE at
//                            the settings editor's OK and at the settings
//                            page's value list's tap (the apply shape,
//                            GuiInputHandler::apply_true_colors); both
//                            templates and both installs `on`. (The row's
//                            toggle, 2026-10-08..10-21, persisted nothing:
//                            the switch was true at every launch.)
//   noise=<name>             THE BACKGROUND SOUND'S FOLDER (architect
//                            2026-10-28; noise.h owns the sound): the name of
//                            one folder under `noise/` beside this file,
//                            exactly one non-empty path component
//                            (is_noise_folder_name below); live at the
//                            settings editor's OK and at the settings
//                            page's value list's tap, the lamp darkening and
//                            the resident recordings freed
//                            (GuiNoise::change_folder); the list shows and
//                            Tab completes the folders there. PERMANENT and
//                            REQUIRED; both templates spell `rain`. A name
//                            with no folder LOADS and greys the NOISE key
//                            (the absent-folder rule, noise.h) — only the
//                            shape is the grammar's
//   noise_level=<dB>         THE BACKGROUND SOUND'S LEVEL (architect
//                            2026-10-28; noise.h owns the sound): an integer
//                            in [-30, 0] in canonical spelling, dB applied
//                            to the files as they are — one level, never
//                            ducked, live at the settings editor's OK and
//                            at the settings page's value list's tap;
//                            PERMANENT and REQUIRED, device-level (he retunes
//                            it by hand per piece); both templates spell -9
//                            (his "the rain is a masker, the default should
//                            be higher than -18")
//   noise_crossfade_s=<s>    THE NOISE'S CROSSFADE between two segments, in
//                            whole SECONDS (architect 2026-10-28): an integer
//                            in [1, 30] in canonical spelling, live at the
//                            settings editor's OK and at the settings
//                            page's value list's tap (read at the next
//                            transition); PERMANENT and REQUIRED — the
//                            crossfade is a recording's relationship to
//                            itself, so a later sound may want another
//                            value; both templates spell 4
//   key_height=<px>          THE KEY'S HEIGHT (architect 2026-10-29, on five
//                            mock sheets varying only the keys' height: the
//                            case "around 33" device px at 300 %, its width
//                            unchanged): the program's key case's height in
//                            DEVICE PX AT 300 % (skin_px's axis, the logo
//                            lane's unit), an integer in [21, 99] in
//                            canonical spelling (parse_key_height below),
//                            live at the settings editor's OK, the window
//                            re-laid-out at once
//                            (GuiInputHandler::apply_key_height); A TUNING
//                            KEY with a Settings row, REQUIRED like every
//                            key while it stands, which settles into a
//                            constexpr once he rules the number (render.h's
//                            icon_case_h_px), its key, grammar and row going
//                            then; both templates spell 33
//   osk_key_w=<px>           THE ON-SCREEN KEYBOARD'S KEY BOX (architect
//   osk_key_h=<px>           2026-10-29, the compact keyboard: "small,
//                            centered, like a phone … chiclets, taller than
//                            wide", the FLAGS sheet's 48 x 60): the painted
//                            keyboard's one-unit key's width and every key's
//                            height in DEVICE PX AT 300 % (skin_px's axis),
//                            each an integer in [24, 160] in canonical
//                            spelling (parse_osk_key_size below), no
//                            taller-than-wide constraint (his to tune),
//                            live at the settings editor's OK, the window
//                            re-laid-out at once
//                            (GuiInputHandler::apply_osk_key_size); TWO
//                            TUNING KEYS with Settings rows, REQUIRED like
//                            every key while they stand, which settle into
//                            constexprs once he rules the numbers
//                            (onscreen_keyboard.h's geometry), their keys,
//                            grammar and rows going then; the template
//                            spells 48 and 60
//   osk_font_px=<px>         THE ON-SCREEN KEYBOARD'S CAP SIZE (architect
//                            2026-10-29 night, "font size osk config tmp
//                            key"): the keys face's em (gui_font.h's
//                            GuiFace::Keys) in DEVICE PX AT 300 %, scaled
//                            with gui_scale like every table size, an
//                            integer in [12, 48] in canonical spelling
//                            (parse_osk_font_px below), live at the settings
//                            editor's OK, the window repainted at once
//                            (GuiInputHandler::apply_osk_font_px); A TUNING
//                            KEY with a Settings row, REQUIRED like every key
//                            while it stands, which settles into the face
//                            table's constexpr once he rules the number
//                            (kGuiFaceSizes), its key, grammar and row going
//                            then; the template spells 24
//   (role=<author|mirror>, THE DEVICE'S PART IN THE PROJECTS REPOSITORY,
//                            a required key 2026-10-10 to 2026-10-30,
//                            RETIRED with the laptop GUI (architect
//                            2026-10-30): one device runs the program, so
//                            there is no mirror, and the one behavior is the
//                            one that commits; an old `role=` line is the
//                            ordinary unknown-key hard fail, the planner
//                            editing the tablet's config at the install and
//                            the sync script's setup removing it)
//   (noise_crossfade_ms=<ms>, THE CROSSFADE IN MS, [100, 30000], a tuning
//                            key 2026-10-28, RETIRED that day for the
//                            seconds key above; an old line is the ordinary
//                            unknown-key hard fail, the planner editing both
//                            devices' configs at the install and the sync
//                            script's setup removing it on the tablet)
//   (rain_level=<dB>, rain_crossfade_ms=<ms>, THE TWO KEYS ABOVE UNDER
//                            THEIR FIRST NAMES, 2026-10-28, RENAMED
//                            `noise_level` and `noise_crossfade_ms` the same
//                            day with the sound's folder made a choice; an
//                            old line is the ordinary unknown-key hard fail,
//                            the planner editing both devices' configs at
//                            the install and the sync script's setup
//                            removing them on the tablet)
//   (rain_fade_in_ms=<ms>, rain_fade_out_ms=<ms>, THE NOISE'S TWO FADES,
//                            tuning keys with Settings rows for one day,
//                            2026-10-28, SETTLED that day on the glass at
//                            200 and 40 ms — noise.h's kNoiseFadeInMs and
//                            kNoiseFadeOutMs; an old line is the ordinary
//                            unknown-key hard fail, the planner editing both
//                            devices' configs at the install)
//   (bevel_light=<percent>, bevel_dark=<percent>, THE FORTEPIANO CHROME'S
//                            two bevel weights, tuning keys with Settings
//                            rows 2026-10-21 to 2026-10-23, RETIRED with that
//                            chrome — the hi-fi skin's rings are the two ring
//                            tones off the ground's mean, materials.h; an old
//                            line is the ordinary unknown-key hard fail, the
//                            planner editing both devices' configs at the
//                            install)
//   (strip_pad=<W>, THE STRIP PAD, a tuning key with a Settings row
//                            2026-10-22 to 2026-10-23, SETTLED AT 13 W —
//                            program_spec.h's strip_pad_px, the ruling
//                            there; an old `strip_pad=` line is the ordinary
//                            unknown-key hard fail, the planner editing both
//                            devices' configs at the install and the sync
//                            script's setup removing it on the tablet)
//   (THE LIT LED'S GLOW in device px, an optional tuning key with a
//                            Settings row 2026-10-23 to 2026-10-26, SETTLED
//                            AT 4 — materials.cpp's kLedGlowPx (architect
//                            2026-10-26: "it stays on"); an old line is the
//                            ordinary unknown-key hard fail, the sync
//                            script's setup removing it on the tablet)
//   (text_shadow=<px>, THE WORDS' DROP SHADOW in device px, a tuning key
//                            with a Settings row 2026-10-21 to 2026-10-22,
//                            RETIRED 2026-10-22 for ONE SCALED WINDOWS PX on
//                            every word, materials.h's INKS — architect: "I
//                            would think that everything should be one
//                            Windows pixel"; an old `text_shadow=` line is
//                            the ordinary unknown-key hard fail)
//
// THAT IS THE WRITER'S ORDER and it is the architect's own (2026-08-30;
// the tuning phases' keys stood at the end from 2026-09-23 until the last of
// them left 2026-09-27, below; `theme` APPENDED after last_project,
// 2026-10-03, the colour keys that stood after it for a day gone, below;
// `palette` APPENDED after `theme` 2026-10-07, the program's colors' own
// file type; `theme` gone 2026-10-08, below; `scheme` placed before `palette`
// 2026-10-08 ~18:15, the chrome's colors before the program's; `icons` and
// `font` stood 2026-10-09 to 2026-10-12;
// `scheme` (`caption` since 2026-10-13) gone and `palette` (`chrome` since
// 2026-10-13) renamed `waveform` in its place 2026-10-21;
// three tuning keys APPENDED at the end 2026-10-21, a tuning phase's
// keys at the end as the 2026-09 phases' stood (`text_shadow` gone
// 2026-10-22, `bevel_light` and `bevel_dark` 2026-10-23); `strip_pad`
// appended 2026-10-22 and gone 2026-10-23, the LED glow's appended
// 2026-10-23 and gone 2026-10-26, the cues' tuning keys after it 2026-10-26 and gone
// that day — the
// label's three in the morning, settled at 26, 14 and on, then four (the
// triangle's rows and antialiasing, the label's size and gap past the
// triangle) settled ~13:05 at 8 rows authored at 300 %, the staircase, 22
// and a gap of 8 (render.h's cue_triangle_rows and cue_label_lead_px,
// paint_ce_cue_triangle, gui_font.h's Program size; an old line is the
// ordinary unknown-key hard fail) — the tuning keys keeping the end; `true_colors` placed after `waveform` 2026-10-21, the two display
// keys together; the level APPENDED after the permanent keys 2026-10-28,
// and the crossfade after it — the two fade tuning keys after it
// for part of that day, gone the same day; the two renamed `noise_level` and
// `noise_crossfade_ms` the same day and `noise` placed before them, so the
// noise's three follow `true_colors` (`role` stood between them 2026-10-10
// to 2026-10-30, the head's list); the crossfade made `noise_crossfade_s`, in seconds and permanent,
// that night, in its place at the end; the tuning key `key_height`
// APPENDED after it 2026-10-29, the tuning keys keeping the end, and the
// keyboard's two, `osk_key_w` and `osk_key_h`, after it the same
// evening, and the keyboard's cap size `osk_font_px` after them that
// night);
// the list above is this file's telling of it and
// kDeviceConfigKeys (device_config.cpp) is the one the program emits from.
//
// WHY IT EXISTS. `gui_scale` was a `.settings` key until 2026-08-27, which
// made it a fact about the PIECE: the same project opened on the laptop and
// on the tablet wanted different scales — 138 and 300 until the laptop GUI's
// drop of 2026-10-30 (100 and 225 in the laptop-pixel unit of 2026-08-27).
// Carrying it in the sidecar meant every
// sync of a project between the two devices had to rewrite it on the way over
// and put it back on the way home. It is the panel's business, so it
// follows the panel. (`audio_player`, the `l` command's external player, made
// the same move beside it and RETIRED WHOLE 2026-08-28 with the in-app render
// player — `l` now plays a render through the product's own playback
// engine, so there is no spawnable player to name; a config still
// carrying the key is unknown-key fatal, no migration, the architect's
// ruling.) THE OTHER THREE JOINED 2026-08-27 with the project model:
// where the projects live is a fact about the device (the app's external
// files dir's `projects/` on the tablet);
// which repository is the projects home is ONE user's ONE repo, not a
// property of each piece, so `projects_repo` left the sidecar too (architect
// approval 2026-08-27, the fifth grant on settings_file.{h,cpp}); and what
// was opened last is what lets the tablet, which has no command line, open
// the right piece without a picker
// at startup. THE WAVEFORM'S HEIGHT CAP CAME AND WENT 2026-09-13..10-07
// (architect): a key in authored px right after gui_scale, with a Settings
// row of its own, until the architect ruled the waveform's height THE
// LANES' LEFTOVER, NOTHING ELSE (2026-10-07 evening). NO TOLERANCE FOR A
// RETIRED KEY (the same ruling, closed_questions.md): the two installs'
// configs are hand-edited at the install — by the planner, the program not
// running — so a config still carrying a retired key meets the ordinary
// unknown-key first-error refusal naming its line, with no migration and no
// special arm.
// THE WAVEFORM PICTURE'S KEYS came and went (architect): the leveler's
// `waveform_gain_*` joined 2026-09-23 and the expander's `waveform_expander_*`
// 2026-09-24 for a tuning phase, eleven at the end, and all eleven LEFT
// 2026-09-24 with the values hard-coded in waveform_gain.cpp so every project
// shares one frame of reference; a config still carrying any of them is
// unknown-key fatal, no migration, the architect hand-editing his two.
// THE WAVEFORM COLOUR KEYS CAME AND WENT 2026-09-25 (architect), one day's
// tuning phase: `waveform_ink`, `waveform_magnified_ink` and
// `waveform_ghost_ink` carried the plate's three inks until the architect
// closed the phase by eye, and the chosen values went back to render.h as
// constants (the lit plate's inks settled after their own tuning phase,
// below). A config still carrying `waveform_magnified_ink` or
// `waveform_ghost_ink` is unknown-key fatal, no migration; `waveform_ink` was
// a key again for a day, 2026-10-03..04 (THE COLOUR KEYS, below).
// THE LIT PLATE'S INK KEYS CAME AND WENT 2026-09-25..27 (architect), A SHORT
// STRUCK RECORD: `fg_color` (the inner's one flat ink), then `fg_blend_loud`
// / `fg_blend_quiet` (the core shade by the bar gap, struck: "it doesn't
// work"), then `fg_color` / `bg_color` (the two bars' flat inks, closed
// 2026-09-26), then `fg_color` / `fg_border_color` / `bg_color` /
// `bg_border_color` (each bar's fill and one-pixel outline), closed by eye
// 2026-09-27 on both fills in the plate's ink and the inner's outline its
// linear-light blend over the canvas, constants in render.h until the
// `waveform_ink` and `waveform_outline` keys of 2026-10-03, theme roles
// 2026-10-04..07 and palette roles since (palette_file.h). A config still carrying any of them is
// unknown-key fatal, no migration.
// `waveform_widening` CAME AND WENT 2026-09-27 (architect), its own tuning
// phase: k columns on each side of a plate column's peak read, closed by eye
// on 0.00, so the read is the column's own span and the key is struck with
// its branch; a config still carrying it is unknown-key fatal, no migration.
// THE TWO LEVEL KEYS CAME AND WENT 2026-09-25 (architect):
// `waveform_magnification_foreground_db` and
// `waveform_magnification_background_db` (earlier the same day
// `waveform_magnified_gain`, `waveform_ghost_reduction`,
// `waveform_magnified_gain_db` and `waveform_ghost_gain_db`) set the lit
// plate's two bars' flat levels, `-inf` leaving a bar out; the inner
// compressor put both bars at 0 dB by construction and they are struck. A
// config still carrying any of them is unknown-key fatal, no migration.
// THE COMPRESSOR'S TWO KEYS CAME AND WENT TWICE, 2026-09-25 and 2026-09-27
// (architect): `waveform_compressor_threshold_db` and
// `waveform_compressor_ratio` carried the lit plate's inner-bar compressor
// for a tuning phase closed by eye the same day on -24.00 and 2.00, and came
// back 2026-09-27 for a second phase, the inner now an outline, with
// `waveform_foreground_gain_db` (the inner bar's flat gain) beside them. The
// architect closed it by eye the same day on -24.00, 2.00 and -6.02 dB taken
// as the exact half, now constexpr in waveform_gain.cpp. A config still
// carrying any of the three is unknown-key fatal, no migration.
// THE PEN PLANE'S KEYS CAME AND WENT 2026-09-27 (architect): the S Pen's
// plane cutoff, landed that day as a constexpr, became the single key
// `pen_plane_distance` for a tuning phase, then the hysteresis pair
// `pen_plane_enter` / `pen_plane_exit` (one cutoff flickered under a pen held
// still at it); the architect closed the phase on the glass the same day on
// 85 and 100 raw AXIS_DISTANCE counts, now constexpr beside the latch
// (kPenPlaneEnter / kPenPlaneExit, platform_android.cpp). A config still
// carrying any of the three is unknown-key fatal, no migration.
// `sync_path` CAME AND WENT 2026-08-30..09-27 (architect): the destination
// folder of the Synchronize act, which mirrored a project's renders onto a
// USB stick for the car. The architect struck the act and the key together
// 2026-09-27, the tablet now being the car's source; a config still carrying
// the key is unknown-key fatal, no migration.
// THE HOLD DELAY'S KEY CAME AND WENT 2026-09-29 (architect), a one-day
// tuning phase: the two holds (the chrome shift long press and the touch
// region hold) read it, placed after the scale with a
// Settings row of its own, until the architect closed the phase on the
// glass the same day on 300 ms, now constexpr (kHoldDelayMs, gui_input.h);
// the key was `hold_delay_ms`, [100, 2000] ms. A config still carrying it is
// unknown-key fatal, no migration.
// THE PALETTE'S TWO KEYS CAME AND WENT 2026-10-02 (architect), a same-day
// tuning phase: `palette_passes` and `waveform_passes` put the chrome's and
// the waveform's colours through extra sRGB -> Display-P3 passes, each with a
// Settings row; the architect settled on the two-pass package, frozen as
// P3 bytes the same day. A config still carrying either is unknown-key
// fatal, no migration.
// THE COLOUR KEYS (architect): `theme` JOINED 2026-10-03 (and left
// 2026-10-08, below), naming THE THEME every color was painted in
// (render.h's palette block owns what each role paints). Beside it from 2026-10-03
// to 2026-10-04 stood `theme_level` (light | dark, the generated table's two
// levels) and the twelve program colour keys (`waveform_ink`,
// `waveform_canvas`, `waveform_outline`, `flag_face`, `flag_face_selected`,
// `flag_label`, `flag_label_selected`, `invalid_face`,
// `invalid_face_selected`, `invalid_label`, `playhead_head`,
// `playhead_stem`), each with a Settings row; they LEFT 2026-10-04 when every
// colour became a role of a theme file and the dark level was dropped (a dark
// look is a theme of its own). A config still carrying any of the thirteen is
// unknown-key fatal, no migration, the fix deleting those lines.
// `theme` ITSELF CAME AND WENT 2026-10-03..10-08 (architect 2026-10-08:
// "okay to retire the color theme catalog"; "we just need hard-coded
// chromes"): it named the built-in `windows-2000-standard` or a theme file
// read at launch from a themes folder beside this file (the imported
// catalog shipped as bundled files, copied in at every launch), absent the
// chrome's own, with a Settings row; every chrome's colors are compiled in
// since (theme_file.h), and THE WAVEFORM PRESET (palette_file.h) chooses
// among compiled-in presets. A config still carrying `theme=` is unknown-key fatal, no
// migration (neither install's carried one).
// The sidecar schema keeps everything that is about the music
// (settings_file.h, where the retired-key record lives).
//
// WHERE IT LIVES: `$XDG_CONFIG_HOME/warptempo_gui/config`, falling back to
// `$HOME/.config/warptempo_gui/config` — the same resolution shape the render
// cache uses for XDG_CACHE_HOME (render_cache.cpp), and ONE resolver with no
// `#ifdef` in it: the Android backend points XDG_CONFIG_HOME at the app's
// private internal directory before anything reads it (android_main,
// platform_android.cpp), exactly as it already does for the cache home.
//
// THE STRICTNESS POSTURE IS THE SIDECAR'S, DELIBERATELY. The file is
// program-written — the first run stamps it from the backend's own template and
// every later commit rewrites it — so any violation is a hand edit, which the
// two-category rule makes ADVERSARIAL: whole-file schema, EXACTLY the thirteen
// keys and each at most once, EVERY KEY REQUIRED (architect 2026-10-12,
// deferred, his "yes" 2026-10-14: "if one of these lines is not in place, it
// means something's wrong with the config or the setup" — the two color
// keys were the two that might be absent 2026-10-07..14, the absence
// spelling the default; the default is SPELLED since, the writer emitting
// every line and the first-run templates naming
// `waveform=default`, so an absent line is the shared scanner's
// missing-required-key refusal like any other, with no migration and no
// launch-time repair — the planner edited the configs at the install),
// one canonical spelling per
// value, and the FIRST error is fatal at startup with a blunt terminal line
// naming the path and the offending line. No repair, no partial apply, no
// silent fallback to defaults — a fallback would silently discard a value the
// user typed. The lexing itself is not respelled here: the shared scanner
// (warptempo_settings::scan_key_value_file, settings_file.h) owns "split at the
// first '=', no blank/comment/whitespace tolerance, no duplicate key, every
// required key present", and this file owns only the per-key grammar over it.
//
// ORDER IS THE WRITER'S, NOT THE READER'S — the sidecar's own posture again.
// The key list in device_config.cpp is the EMITTED order (the head's list)
// and it is what every file this program writes carries; the shared scanner checks
// MEMBERSHIP, duplicates and presence and never position, so a hand-reordered
// file still loads. That costs nothing and buys the sidecar's symmetry: there,
// kSettingsOrder owns the write order and the reader has always been
// order-insensitive. A position rule would be a second guard for a state the
// writer cannot produce.
//
// NO UNDO, NO DIRTY, NO Ctrl+S. Every key is history-less, committed through
// its own chokepoint, and each chokepoint writes this file immediately, so the
// file is always the last committed state and a save carries none of them.
// The chokepoints are the callers inventory at write_device_config below.
//
// EVERY EDITABLE KEY HAS AN IN-APP ROAD SINCE 2026-09-02 (architect): THE
// SETTINGS EDITOR (settings_editor.h), its typed `key=value` line, Tab
// completing the key names — `gui_scale`, `projects_repo`, `projects_path`,
// `waveform`, `true_colors`, since 2026-10-28 `noise`, `noise_level`
// and `noise_crossfade_s` and since 2026-10-29 the tuning keys `key_height`,
// `osk_key_w` and `osk_key_h` and that night `osk_font_px` among them
// (kSettingsEditorKeys) — then a key's current value, then a closed value's
// words, and committing each through this file's writer under the key's own
// grammar below. (The Settings popup's rows opened it prefilled, one per key,
// 2026-09-02..10-21: `GUI Scale`, `Projects Repository`, `Projects Path`,
// `Waveform` (`Chrome` 2026-10-14..10-21, beside `Caption`), the tuning rows
// and the "True Colors" command row; `Chrome` the vocabulary 2026-10-07..10,
// `Icons` and `Font` 2026-10-09..12, `Theme` 2026-10-03..08; the color
// picker's preset menu wrote the two color keys 2026-10-07..14.) Until
// 2026-09-02 the path keys
// were hand-edited only, and HELP told the user to edit one without
// telling him to quit first — which mattered, because A HAND EDIT UNDER A
// RUNNING APP IS CLOBBERED BY THE NEXT IN-APP COMMIT: the live struct is
// the truth and every commit rewrites the whole file from it. That stays so,
// and it is ordinary Linux behaviour for a program-written config; the in-app
// road is the sanctioned one now and the hand edit is the quit-first
// alternative. `last_project` is the one key with no editor that the program
// writes — it is the program's own.

// The whole file, typed. The member defaults are CONSTRUCTION STATE, not load
// fallbacks, for every key: each is required, so a successful read always
// assigns them. `waveform` is EMPTY as construction state, never a read's
// (an empty value is refused); the backend's template
// (GuiPlatform::device_config_defaults) SPELLS THE DEFAULT into it (architect
// 2026-10-14: every key required) — kGuiDefaultPaletteKey (palette_file.h),
// `default` — so the first-run file names its
// waveform preset. EVERY KEY WRITES ITS LINE, so a first run stamps the
// whole file and its read-back loads. (The `icons` and `font` keys left
// 2026-10-12: the icons and Liberation Sans are hard-coded.)
//
// ONE OF THEM MEANS SOMETHING BY BEING EMPTY, saying so in its own grammar
// below: `last_project` empty is "nothing opened yet". (`projects_repo` also
// ADMITS empty, being free text, but empty is just a value there — one that
// never matches a remote.)
// The key is still REQUIRED in every case: the file admits no absent state,
// only an empty VALUE.
//
// The members are in the writer's order.
//
// THE WAVEFORM PRESET IS ITS KEY VERBATIM (2026-10-07); the install
// resolves it (install_palette, render.h, through palette_record,
// palette_file.h) and derives the chrome's colors by Cool Edit's tones
// (live_chrome_words, chrome_derive.h).
struct DeviceConfig {
    int         gui_scale = 138;
    std::string projects_repo;
    std::string projects_path;
    std::string last_project;
    // THE WAVEFORM PRESET, the `waveform` key's built-in name (architect
    // 2026-10-07; `palette` the member's name until 2026-10-21): required
    // since 2026-10-14, so empty only before a read; the templates spell the
    // default, `default` (kGuiDefaultPaletteKey). (`scheme`, the
    // `caption` key's, stood before it 2026-10-08..10-21.)
    std::string waveform;
    // TRUE COLORS (2026-10-21, the head's list): required; `on` the
    // templates' and the construction state's.
    bool        true_colors = true;
    // THE NOISE'S FOLDER NAME, its level in dB and its crossfade in seconds
    // (2026-10-28, the head's list): required; the construction state is the
    // templates' values (`rain`, -9, 4).
    std::string noise             = "rain";
    int         noise_level       = -9;
    int         noise_crossfade_s = 4;
    // THE KEY'S HEIGHT in device px at 300 % (2026-10-29, the head's list; a
    // tuning key): required; the construction state is the templates' 33
    // (kKeyHeightDefaultAt300 below).
    int         key_height        = 33;
    // THE ON-SCREEN KEYBOARD'S KEY BOX in device px at 300 % (2026-10-29,
    // the head's list; two tuning keys): required; the construction state is
    // the templates' 48 x 60 (kOskKeyWDefaultAt300, kOskKeyHDefaultAt300
    // below).
    int         osk_key_w         = 48;
    int         osk_key_h         = 60;
    // THE ON-SCREEN KEYBOARD'S CAP SIZE in device px at 300 % (2026-10-29
    // night, the head's list; a tuning key): required; the construction
    // state is the templates' 24 (kOskFontPxDefaultAt300 below).
    int         osk_font_px       = 24;
};

// The repository a device is STAMPED WITH when it has never named one — the
// first-run template's `projects_repo=` value. It moved here
// from settings_file.h with the key (architect approval 2026-08-27): the
// sidecar no longer references it, and AppState's own field reads it so a
// session that has not read the config yet names the same repository the
// template stamps. It is NOT a load fallback — the key is required, so a
// config either names a repository (blank included) or refuses. THE PROJECTS
// REPOSITORY IS ITS OWN, SEPARATE AND PUBLIC (architect 2026-09-27): the
// pieces' sidecar history lives in warptempo_projects, laid out as
// `projects/<piece>/` inside it, and this program's own repository ignores
// `projects/` (the walk's match, is_piece_sidecar_path, history_diff.h).
inline constexpr const char* kDefaultProjectsRepo =
    "github.com/warptempo/warptempo_projects";

// THE gui_scale RANGE — the ONE owner, moved here from the `.settings` schema
// 2026-08-27 with the key (architect approval 2026-08-27). Both askers call it
// and neither respells the bracket: this file's reader, and the settings
// editor's `gui_scale=` refused commit ("loadable iff it commits", the
// standing rule).
//
// THE PERCENT IS DEVICE PX PER WINDOWS-95 PX since 2026-10-02 (architect; the
// unit's statement is at render.h's scaled_px): the tablet runs 300 since
// 2026-10-06 (a 16-W glyph at 48 device px, 16 · 3); 100 is Windows' own
// 96-dpi size; 1000 is the ceiling; 50 is the half-size floor,
// which is where every structural dimension in render.h's scaled_px
// accessors still has a floor holding it above zero.
//
// THE CEILING IS 1000 (architect 2026-10-05, raising it from the 350 that
// stood from 2026-08-29 so 400 can be tried on the tablet; 400 stood from
// 2026-08-26 before that). It is a vocabulary, not a fit: at the Windows
// pixel the lanes above the waveform — the program's band, its logo lane
// 72 device px at 300 % over the case and the strip pad, its canvas
// column's 36 W and the canvas's top frame row, one device px — and below
// it — the canvas's bottom frame row, one device px, and row 8's 49 W —
// stand 385 device rows above the waveform and 197 below at 400 %, 582 in
// all, leaving the tablet's 1440 rows 858 of waveform (re-derived
// 2026-10-28 with the frame rows' hairlines, main.cpp's stack record);
// they pass 1440 near 990 %, past which the
// waveform is the zero height waveform_area's floor answers (the arithmetic and the guard at
// waveform_area, main.cpp).
//
// THE LAYOUT IS NOT WIDENED WITH THE CEILING, deliberately: below about 679
// Windows px of window at 100 % (472 in the `h` view; re-derived 2026-10-29,
// the Restrict lamp behind Follow)
// the icon row — the
// toolbar band, its twenty-seven standing members in three left runs and one
// right run of the program's 23-W case (eighteen in the view, its history
// stand-ins and the view's hide — architect 2026-10-05 and 2026-10-07; the
// runs the simple layout's, 2026-10-22) — no longer fits, and its
// right-anchored run covers the runs to its left (the overflow rule at
// kIconRowViewPairs, paint_handler.cpp). The tablet's 2304-px panel holds
// the row whole in both states up to about 340 % — the left runs on device
// x 23..1630 and the right run on 1936..2280 at 300 % outside the view (the
// arithmetic is at paint_icon_row, paint_handler.cpp, its one statement,
// re-derived 2026-10-29 with the Restrict lamp) — and past
// that ceiling the overflow rule answers. The
// redesign carries no
// collision rule anywhere: the crop-at-the-floor allowance recorded at
// kMinWindowWidthPx (render.h) is the standing answer for a narrow window. A
// scale is a VOCABULARY; which of its values lays out well is the
// architect's call on his own panel, not a validator's.
inline constexpr bool is_gui_scale_percent(int64_t v) {
    return v >= 50 && v <= 1000;
}

// The scale's reason, spelled once for its two readers (the config reader's
// `bad_value` line and the settings editor's card).
inline constexpr const char* kGuiScaleGrammarReason =
    "must be an integer in [50, 1000] in canonical spelling";

// THE ASCII WHITESPACE SET this file's grammars refuse at a value's edges —
// all six of it, spelled as a byte set rather than asked of the locale, which
// no other grammar surface in the product consults either. ONE owner, TWO
// askers below: the path value grammar and the project name's.
inline constexpr bool is_config_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\v' || c == '\f' || c == '\r';
}

// THE PATH VALUE GRAMMAR — the ONE owner for the path key (`projects_path`,
// below): non-empty, ABSOLUTE, carrying NO LINE
// SEPARATOR anywhere (`\n` or `\r`) and NO ASCII WHITESPACE at either edge,
// and taken verbatim otherwise. A path may legitimately hold a space in the
// middle of a component, so only the edges are refused.
//
// THE TWO LEXICAL CLAUSES ARE `last_project`'s, for its reasons said of a path
// (the full argument is at is_last_project_name): the file is line-based
// `key=value` with NO ESCAPING, so a value carrying a line separator cannot be
// serialized at all, and an edge space is serializable but not canonical — and
// it would compose a DIFFERENT folder than the one the hand meant to name,
// silently. `\r` HAS A REAL PRODUCER:
// std::getline strips the `\n` of a CRLF line and leaves the `\r` on the
// value, so a config saved by a Windows editor refuses out loud here instead
// of aiming an act at `<path>\r`.
//
// EXISTENCE IS DELIBERATELY NOT A GRAMMAR QUESTION: a `projects_path` that
// names no folder is startup's own "No project under <projects_path>" answer
// (project_model.h) — the config is right about WHERE, and the thing may
// simply not be there yet.
inline bool is_config_path_value(const std::string& v) {
    if (v.empty()) return false;
    if (v.find('\n') != std::string::npos) return false;
    if (v.find('\r') != std::string::npos) return false;
    if (is_config_whitespace(v.front()) || is_config_whitespace(v.back()))
        return false;
    return std::filesystem::path(v).is_absolute();
}

// THE projects_path GRAMMAR — the path value grammar exactly, this key having
// no empty form: the app always has a projects path.
inline bool is_projects_path(const std::string& v) {
    return is_config_path_value(v);
}

// THE GRAMMARS' REASONS, spelled once beside the predicates they explain:
// the config reader's `bad_value` line and the settings editor's refusal
// card are the two readers of each, so a refusal says the same words on the
// terminal at startup and on a card at the commit. THE PATH REASONS NAME THE
// EDGE RULE because that is the fault a hand actually makes: an absolute path
// with a trailing space (or the `\r` a CRLF-saved file leaves on every value)
// is refused, and "must be an absolute path" alone would read as a lie
// against a value that plainly is one.
inline constexpr const char* kProjectsPathGrammarReason =
    "must be an absolute path with no whitespace at either edge";
inline constexpr const char* kProjectsRepoGrammarReason =
    "must not carry a line separator";

// THE projects_repo GRAMMAR — free text, verbatim, EMPTY INCLUDED (a blank
// repository never matches a remote and so disables the GitHub recheck), with
// ONE refusal: a line separator anywhere (`\n` or `\r`), the one thing this
// line-based `key=value` file with no escaping cannot carry — the writer would
// emit more than one physical line and the shared scanner would refuse the
// remainder on the next read. No host/path grammar is enforced here: the
// recheck normalizes the value against the local clone's own `origin` and
// refuses the mismatch there, which is a far better place to judge it than a
// reader that cannot see the clone. The `\r` clause has the same real
// producer the path key's has (std::getline on a CRLF-saved file) and joined
// 2026-09-02 when the key gained this predicate — before it the reader took
// the value with no test at all, and the editor's own filter had always
// dropped control bytes, so the only value the clause newly refuses is a
// hand-edited one.
inline bool is_projects_repo(const std::string& v) {
    if (v.find('\n') != std::string::npos) return false;
    if (v.find('\r') != std::string::npos) return false;
    return true;
}

// THE true_colors GRAMMAR (2026-10-21) — the two lowercase words, exactly;
// the one owner the reader, the writer, the settings editor's commit and its
// recall read (format_true_colors, parse_true_colors), and the words its Tab
// completes (kTrueColorsWords).
inline constexpr const char* kTrueColorsOn  = "on";
inline constexpr const char* kTrueColorsOff = "off";
inline constexpr const char* kTrueColorsWords[] = {kTrueColorsOn,
                                                   kTrueColorsOff};
inline constexpr const char* kTrueColorsGrammarReason = "must be on or off";
inline const char* format_true_colors(bool on) {
    return on ? kTrueColorsOn : kTrueColorsOff;
}
inline std::optional<bool> parse_true_colors(std::string_view v) {
    if (v == kTrueColorsOn)  return true;
    if (v == kTrueColorsOff) return false;
    return std::nullopt;
}

// A CANONICAL SIGNED INTEGER (2026-10-28, the two noise integers' shared
// lexis, and since 2026-10-29 the key's height's and the keyboard's key
// box's): an optional `-`, then digits with no leading zero, and no `-0` —
// exactly what `%d` prints, so the reader admits back only the writer's
// spelling. At most nine digits, which every range below sits far inside, so
// the accumulation cannot overflow. The one lexer the four integer grammars
// read; each owns its own range.
inline std::optional<int> parse_canonical_config_int(std::string_view v) {
    bool neg = false;
    if (!v.empty() && v.front() == '-') {
        neg = true;
        v.remove_prefix(1);
    }
    if (v.empty() || v.size() > 9) return std::nullopt;
    if (v.size() > 1 && v.front() == '0') return std::nullopt;
    if (neg && v == "0") return std::nullopt;
    int n = 0;
    for (const char c : v) {
        if (c < '0' || c > '9') return std::nullopt;
        n = n * 10 + (c - '0');
    }
    return neg ? -n : n;
}

// THE noise_level GRAMMAR (architect 2026-10-28) — an integer in [-30, 0] dB
// in canonical spelling; the one owner the reader, the writer, the settings
// editor's commit and its recall, and the settings page's value list read
// (format_noise_level, parse_noise_level; the list's 31 rows are this range
// in 1 dB steps, architect 2026-10-28 night: "1 dB, 0 to -30"). 0 is the
// files as they are, -30 a faint bed; the level is applied to the segments
// as they lie, no equalizing (noise.h's head).
inline constexpr int kNoiseLevelMinDb = -30;
inline constexpr int kNoiseLevelMaxDb = 0;
inline constexpr const char* kNoiseLevelGrammarReason =
    "must be an integer in [-30, 0] in canonical spelling";
std::string format_noise_level(int db);
inline std::optional<int> parse_noise_level(std::string_view v) {
    const std::optional<int> n = parse_canonical_config_int(v);
    if (!n || *n < kNoiseLevelMinDb || *n > kNoiseLevelMaxDb)
        return std::nullopt;
    return n;
}

// THE noise_crossfade_s GRAMMAR (architect 2026-10-28) — an integer in
// [1, 30] SECONDS in canonical spelling, the one owner the same five read
// (format_noise_crossfade_s, parse_noise_crossfade_s; the value list's 30
// rows are this range in whole seconds). Seconds because the crossfade is a
// recording's relationship to itself, chosen per sound and never finer than
// a second; the voice converts at its one setter (GuiNoise::set_crossfade_s,
// noise.h).
inline constexpr int kNoiseCrossfadeMinS = 1;
inline constexpr int kNoiseCrossfadeMaxS = 30;
inline constexpr const char* kNoiseCrossfadeGrammarReason =
    "must be an integer in [1, 30] in canonical spelling";
std::string format_noise_crossfade_s(int s);
inline std::optional<int> parse_noise_crossfade_s(std::string_view v) {
    const std::optional<int> n = parse_canonical_config_int(v);
    if (!n || *n < kNoiseCrossfadeMinS || *n > kNoiseCrossfadeMaxS)
        return std::nullopt;
    return n;
}

// THE key_height GRAMMAR (architect 2026-10-29) — an integer in [21, 99]
// DEVICE PX AT 300 % in canonical spelling, the one owner the reader, the
// writer, the settings editor's commit and its recall read
// (format_key_height, parse_key_height). The range brackets the mocks'
// series round his "around 33" with room both ways: 21 a third of the
// square case's 69, 99 past it. The settings page opens the LINE EDITOR on
// it, not a value list (79 values is no short domain, kSettingsValueListKeys'
// rule). A TUNING KEY: when he rules the number, the key, these functions
// and its Settings row go, and the value becomes a constexpr behind
// render.h's icon_case_h_px.
inline constexpr int kKeyHeightMinAt300     = 21;
inline constexpr int kKeyHeightMaxAt300     = 99;
inline constexpr int kKeyHeightDefaultAt300 = 33;
inline constexpr const char* kKeyHeightGrammarReason =
    "must be an integer in [21, 99] in canonical spelling";
std::string format_key_height(int px);
inline std::optional<int> parse_key_height(std::string_view v) {
    const std::optional<int> n = parse_canonical_config_int(v);
    if (!n || *n < kKeyHeightMinAt300 || *n > kKeyHeightMaxAt300)
        return std::nullopt;
    return n;
}

// THE osk_key_w AND osk_key_h GRAMMAR (architect 2026-10-29), ONE FOR THE
// TWO — an integer in [24, 160] DEVICE PX AT 300 % in canonical spelling, the
// one owner the reader, the writer, the settings editor's commit and its
// recall read (format_osk_key_size, parse_osk_key_size). The defaults are
// the FLAGS sheet's key box, 48 wide and 60 tall (tmp/kbmock's generator);
// the range runs from half the default width to well past the height, wide
// enough for him to try a key wider than tall, and its top keeps the widest
// row (thirteen units) inside the tablet's 2304 (13 x (160 + 16) - 16 =
// 2272). The settings page opens the LINE EDITOR on each, not a value list
// (137 values is no short domain, kSettingsValueListKeys' rule). TWO TUNING
// KEYS: when he rules the numbers, the keys, these functions and their
// Settings rows go, and the values become constexprs behind
// onscreen_keyboard.h's key_w_px and key_h_px.
inline constexpr int kOskKeySizeMinAt300  = 24;
inline constexpr int kOskKeySizeMaxAt300  = 160;
inline constexpr int kOskKeyWDefaultAt300 = 48;
inline constexpr int kOskKeyHDefaultAt300 = 60;
inline constexpr const char* kOskKeySizeGrammarReason =
    "must be an integer in [24, 160] in canonical spelling";
std::string format_osk_key_size(int px);
inline std::optional<int> parse_osk_key_size(std::string_view v) {
    const std::optional<int> n = parse_canonical_config_int(v);
    if (!n || *n < kOskKeySizeMinAt300 || *n > kOskKeySizeMaxAt300)
        return std::nullopt;
    return n;
}

// THE osk_font_px GRAMMAR (architect 2026-10-29 night) — an integer in
// [12, 48] DEVICE PX AT 300 % in canonical spelling, the keys face's em
// (gui_font.h's GuiFace::Keys, scaled with gui_scale like every table size),
// the one owner the reader, the writer, the settings editor's commit and its
// recall read (format_osk_font_px, parse_osk_font_px). The default is the
// face table's 24 (architect 2026-10-24, "C24"). The range: 12 is the
// smallest em at which a cap stays legible at 300 % (the em 150 % paints
// the default at); 48 is twice the default, the em by which
// every function word but "Bksp" and "Space" already overhangs its key at
// the default key box ("Esc", the last of them, from em 44; "Bksp", 107 px
// at em 48 in the 2.5u key's 127-px face, not until 58), so a larger cap
// would only overhang more (onscreen_keyboard.h's EVERY WORD FITS ITS KEY
// paragraph, cap_word; architect 2026-10-29 night). The settings page opens the LINE
// EDITOR on it, not a value list (37 values is no short domain,
// kSettingsValueListKeys' rule). A TUNING KEY: when he rules the number, the
// key, these functions and its Settings row go, and the value settles into
// kGuiFaceSizes' Keys entry.
inline constexpr int kOskFontPxMinAt300     = 12;
inline constexpr int kOskFontPxMaxAt300     = 48;
inline constexpr int kOskFontPxDefaultAt300 = 24;
inline constexpr const char* kOskFontPxGrammarReason =
    "must be an integer in [12, 48] in canonical spelling";
std::string format_osk_font_px(int px);
inline std::optional<int> parse_osk_font_px(std::string_view v) {
    const std::optional<int> n = parse_canonical_config_int(v);
    if (!n || *n < kOskFontPxMinAt300 || *n > kOskFontPxMaxAt300)
        return std::nullopt;
    return n;
}

// (THE waveform GRAMMAR is palette_file.h's is_builtin_palette_name and
// kPaletteGrammarReason — a built-in's key alone since 2026-10-14; the
// `caption` key's, is_builtin_scheme_name, left with the key 2026-10-21. The
// `theme_level` grammar and the program color grammar of the twelve keys
// left with those keys 2026-10-04; the `theme` key's own grammar left with
// the key 2026-10-08.)

// HOW A PATH UNDER THE PROJECTS PATH IS NAMED IN A SENTENCE — the basename
// rule's composer for everything the project model and the loaders say
// (failure.h). A sentence that carries a path names THE PROJECT FOLDER
// AND THE FILE — `550 - 1/07 - Menuetto.settings` — and never the projects
// path itself, because every one of those sentences is one line on a
// notification card that CLIPS, and the leading `/home/.../projects/` is the
// one part of it the reader already knows: it is this file's own key, the
// same for every project on the device. A path with no parent falls back to
// its filename. Lexical, following no link.
//
// IT LIVES BESIDE THE KEY IT REFUSES TO SPELL rather than in the project
// model, so the sidecar readers (settings_io.cpp) can compose the same
// sentence without depending on the model; the model's own refusals name the
// FOLDER NAME alone, which is `folder.filename()` and needs nothing from here.
inline std::string shown_project_path(const std::filesystem::path& p) {
    const std::string parent = p.parent_path().filename().string();
    if (parent.empty()) return p.filename().string();
    return parent + "/" + p.filename().string();
}

// THE last_project GRAMMAR — the ONE owner: EMPTY, or exactly ONE path
// component — no `/`, not `.`, not `..`, no LINE SEPARATOR anywhere in it
// (`\n` or `\r`), and no leading or trailing ASCII WHITESPACE (all six of it,
// through the byte-set owner is_config_whitespace above). The name is
// otherwise verbatim, since a project folder may legitimately end in any
// other character. A separator or a
// dot-name is the ADVERSARIAL class and refuses at load like every other schema
// violation: the program writes this key with one folder NAME, so a `/` or a
// `..` is a state its one producer cannot make — and a name carrying a
// separator would compose a path that LEAVES the folder it claims to name
// (`../other` resolves outside the projects path entirely), after which the
// program would open and EDIT a piece that was never a project here. So the
// composition happens only after the name is known to be a single component,
// and anything else refuses the whole config out loud.
//
// THE LINE SEPARATOR IS REFUSED ANYWHERE, not just at the edges, because the
// file is line-based `key=value` with NO ESCAPING: a name carrying a `\n` or a
// `\r` cannot be serialized at all — the writer would emit more than one
// physical line and the shared scanner would refuse the remainder as "not a
// key=value line" on the next read. The edge-whitespace rule stays exactly as
// it is (a trailing space is serializable but not canonical); this clause is
// about what the FORMAT can carry.
//
// THE GRAMMAR IS ALSO A MEMBERSHIP TEST, not only a reader's: a folder whose
// name this predicate refuses is not a project on any road, because every
// successful open writes that name into this key (enumerate_project_names,
// project_model.{h,cpp}, is the other caller).
inline bool is_last_project_name(const std::string& v) {
    if (v.empty()) return true;
    if (v == "." || v == "..") return false;
    if (v.find('/') != std::string::npos) return false;
    if (v.find('\n') != std::string::npos) return false;
    if (v.find('\r') != std::string::npos) return false;
    if (is_config_whitespace(v.front()) || is_config_whitespace(v.back()))
        return false;
    return true;
}

// THE noise GRAMMAR (architect 2026-10-28) — the ONE owner the reader, the
// settings editor's commit, the settings page's value list (its rows the
// folders under `noise/` this admits) and the sync script's mirror (its own
// respelling, scripts/warptempo_sync's mirror_noise) read: exactly ONE path
// component and
// NON-EMPTY — is_last_project_name's clauses (no `/`, not `.` or `..`, no
// `\n` or `\r` anywhere, no leading or trailing config whitespace) with the
// empty arm REFUSED: the key names a sound, the templates spell `rain`, and a
// blank line is a state the program never writes. THE COMPOSITION ARGUMENT IS
// is_last_project_name's: a `/` or a `..` would compose a path outside
// `noise/` (GuiNoise::probe composes the folder only from a name this admits),
// and a line separator cannot be serialized. THE ABSENT FOLDER IS NOT THE
// GRAMMAR'S: a well-shaped name whose folder does not exist LOADS, and the
// NOISE key greys (noise.h's head). CLOSED (closed_questions.md, this its
// owner): an empty `noise=` line. THE CHOICE OF SOUND FROM INSIDE THE APP is
// the settings page's `noise` row (architect 2026-10-28 night): a value list
// of the folders there, a tap committing the name through the typed line's
// own commit; Tab still completes the typed name.
inline constexpr const char* kNoiseFolderGrammarReason =
    "must be one folder name under noise/, not empty and not a path";
inline bool is_noise_folder_name(const std::string& v) {
    return !v.empty() && is_last_project_name(v);
}

// The canonical on-disk spelling of a scale percent: plain digits, exactly the
// `%d` output the reader's parse_authored_frame arm accepts back. THE ONE
// SERIALIZER for the value — this file's writer and the settings editor's Tab
// autocomplete recall (recall_gui_setting_value, settings_io.h) both call it,
// so recall and file can never diverge.
std::string format_gui_scale_percent(int percent);

// The resolved config path, or an EMPTY path when neither XDG_CONFIG_HOME nor
// HOME is set (the loader turns that into its own fatal line; the writer
// reports the failure and writes nothing).
std::filesystem::path device_config_path();

// The file's exact bytes for this value set — the string half write_device_config
// hands to the atomic writer, and the first-run template's bytes too, so the
// stamped file and a committed one are one format by construction.
std::string format_device_config_text(const DeviceConfig& cfg);

// Parse and validate the whole config file at `path` under the schema at the
// head of this file. Errors carry a "line N: " prefix where a line is at fault;
// the caller adds the path context.
std::expected<DeviceConfig, std::string> read_device_config(
    const std::filesystem::path& path);

// Write the live values atomically (temp + fsync + rename, through the shared
// atomic writer), creating the parent directory if it is missing. Answers
// nothing on success and THE FAILURE ON ANY I/O FAILURE — both clauses
// composed here at the one failure point (GuiFailure, failure.h: the
// diagnostic with the full path and the system's words where there are any,
// the display naming the file by its basename, `config`), this writer
// printing nothing itself; each caller prints the diagnostic and, where it
// has a card surface, raises the display. A failed persist is advisory, never
// fatal: the live value stands for this session and the next launch reads
// whatever the file last held.
//
// The card stands, and it is a ruling rather than a leftover (architect
// 2026-09-04, blessing what landed 2026-09-02 in the two-clause shape): a
// 2026-09-02 review had classed a failed persist as adversarial reach and
// asked for no message at all, but each of these writes is a deliberate press
// whose result nothing paints, and the deliberate-press rule (notifications.h)
// is what decides such a case — so the failure says its sentence, on a
// NORMAL card, where the caller has a card surface.
//
// THE LIVE CONFIG HAS ONE OWNER, gui_main's loop (main.cpp): the ONE
// DeviceConfig it reads at startup outlives every project the process opens,
// and AppState reaches it through `AppState::device_config` (a pointer, seated
// at each AppState's construction). That is what lets a value typed in one
// project survive the reopen that tears that AppState down — a failed persist
// is advisory, so re-reading the FILE at each reopen could lose a value the
// user committed in the session — and it is why the callers below write the
// struct they were handed rather than composing one from AppState's fields.
//
// THREE CALL SITES CARRY THE KEY COMMITS, and this is their inventory
// (re-grepped 2026-10-21):
// the scale's chokepoint GuiInputHandler::apply_gui_scale (input_handler.cpp);
// the settings editor's ONE device-key body, which serves eleven keys —
// `projects_repo=`, `projects_path=`, since 2026-10-14 `waveform=` (`chrome=`
// until 2026-10-21, beside `caption=` until then), since 2026-10-21
// `true_colors=`, and since 2026-10-28 the noise's three, `noise=`,
// `noise_level=` and `noise_crossfade_s=`, and since 2026-10-29 the tuning
// keys `key_height=` (its live act GuiInputHandler::apply_key_height) and
// `osk_key_w=` and `osk_key_h=` (theirs GuiInputHandler::apply_osk_key_size)
// and that night `osk_font_px=` (its GuiInputHandler::apply_osk_font_px)
// (the
// noise's latter two `rain_level=` and
// `rain_crossfade_ms=` earlier that day and the crossfade
// `noise_crossfade_ms=` until that night, `rain_fade_in_ms=` and
// `rain_fade_out_ms=` beside them; `bevel_light=` and `bevel_dark=`
// 2026-10-21 to
// 2026-10-23, `text_shadow=` until 2026-10-22, `strip_pad=` 2026-10-22 to
// 2026-10-23, the LED glow's 2026-10-23 to 2026-10-26)
// (GuiSettingsEditor::commit_device_setting, settings_editor.cpp;
// the path arm joined 2026-09-02;
// the theme's stood 2026-10-03..10-08, the vocabulary `chrome`'s
// 2026-10-07..10-10; the color picker's preset menu wrote the two color
// keys 2026-10-07..14); and
// gui_main's `last_project` write on the success path
// of every open (main.cpp). A same-value commit never reaches any of them —
// each gates the no-op ahead of the write — so a file rewrite means a value
// actually moved. `last_project` is the one key with no editor: it is the
// program's own.
std::optional<GuiFailure> write_device_config(const DeviceConfig& cfg);

// STARTUP: the config, created from `first_run_template` if the file does not
// exist yet and then read back like any other. The template is the running
// BACKEND's answer (GuiPlatform::device_config_defaults — a platform fact, not
// a GUI one: 300 % and the external files dir's `projects/`;
// it stamps `waveform=default` (the default spelled, every key
// required since 2026-10-14), `true_colors=on` (the struct's own value),
// `noise=rain`, `noise_level=-9` and `noise_crossfade_s=4` (2026-10-28),
// `key_height=33`, `osk_key_w=48`, `osk_key_h=60` and `osk_font_px=24`
// (2026-10-29, kKeyHeightDefaultAt300, kOskKeyWDefaultAt300,
// kOskKeyHDefaultAt300, kOskFontPxDefaultAt300), kDefaultProjectsRepo and
// a blank
// last_project),
// so a first run lands a
// file that is
// already right for it and the user edits
// from there rather than from a wrong guess. A missing parent directory is
// created.
//
// The error is the whole failure vocabulary of this file: an unresolvable
// config home, a create that failed, an unopenable file, or the reader's first
// schema refusal. Every one of them is FATAL at the caller (gui_main) — the
// blunt terminal line and no window.
std::expected<DeviceConfig, std::string> load_device_config(
    const DeviceConfig& first_run_template);
