#pragma once

#include "gui_theme.h"   // GuiTheme, GuiThemeTones, the reference row, the slug rule

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// THE THEMES' LIST, THE THEME FILE AND THE DERIVATION (architect 2026-11-05,
// the theme arc; 2026-11-06, session 75: this file the derivation's one
// owner; architect 2026-11-11, session 110, THE THEME FILES: OUR OWN KEYED
// FORMAT, the files in the repository and the APK). A theme is one file of
// LIVE'S RAW ROLE VALUES — the roles THE DERIVATION below reads, under Live's
// own role names, never a derived tone — PLUS THREE ROLES OF OUR OWN (THE
// FORMAT, below), and the derivation, this file's, turns each into a row of
// GuiThemeTones at the launch's read.
//
// THE FORMAT (its one owner here). A theme file is `<slug>.theme`, ASCII
// text (UTF-8 in a comment), a line ending `\n` — a `\r` before it is
// stripped, so a file saved with CRLF reads the same — and the last line's
// `\n` optional. A line is EMPTY, A COMMENT (its first byte `#`, the rest
// anything), or A ROLE LINE `Name=HEX` exactly: no space anywhere, Name one
// of kThemeRoles (theme_roles.cpp; Live's role names, the one nested role
// flattened to the key `StandardVuMeter/Maximum` — Live's own meter clip box,
// the first `Maximum` inside the first `<StandardVuMeter>` block of an .ask —
// AND OUR OWN THREE, WindowBackground, TitleForeground and SidebarForeground,
// kOwnThemeRoles: no Live role names them),
// HEX 6 or 8 hex digits, case-blind on read (the converter writes uppercase),
// an 8-digit word's last byte its straight alpha — kept raw, the derivation
// compositing it (the COMPOSITED block). Every role of the table exactly
// once, in any order (the converter writes the table's). THE SLUG is the
// file's stem through THE SLUG RULE (theme_slug_of_stem, gui_theme.h), and a
// shipped file is named by its own slug (`default_dark_cool_medium.theme`,
// `sb_dash_mod.theme`): the slugs are the ones the `theme` key spelled when
// the app read Live's .ask files itself (2026-11-06 .. 11-11), so every
// config line keeps working.
//
// A FILE IS LEFT OUT, with one stderr line naming its path (its name, for a
// bundled file) and the first error, its line number where it has one, for:
// unreadable; a stem that spells no slug (no letter or digit); a malformed
// line; a role name the table lacks; a role twice; a role missing.
//
// THE FILES. The SHIPPED files are the repository's `themes/system/` and
// `themes/extra/`, packed by apk/build_apk.sh as the APK's `themes/system/`
// and `themes/extra/` assets and read in place at launch
// (GuiPlatform::bundled_theme_files) — each converted ONCE on the laptop
// from Live's or the pack's `.ask` (Ableton's theme XML) by
// tools/themes/ask_to_theme.py, never by hand but for the `_mod` files'
// recorded edits, the two roles of 2026-11-17 (ScrollbarInnerTrack and
// ClipBorder, each file's from its own .ask, its header line the record)
// and our own three of 2026-11-19 (each file's own SurfaceArea and
// ControlForeground copied, the ROLE-NAMED block's OUR OWN, its header line
// the record; the converter writes them the same way) —
// AND `arc_dark`, WRITTEN BY HAND from the arc-theme sources, no .ask behind
// it, its header the record of every role (architect 2026-11-17, session
// 140); NO .ASK FILE IS IN THE REPOSITORY OR THE APK, and the app reads
// none. THE USER'S files are `themes/*.theme` FLAT beside the device
// config (device_config_path()'s parent / "themes", the noise's
// composition): each regular file directly there whose name ends `.theme`;
// a subfolder, or any other file, is ignored silently; a MISSING OR
// UNLISTABLE folder is the ordinary state and silent (empty on his tablet).
// READ ONCE AT LAUNCH (load_gui_themes, gui_main, after the config and
// before the palette's first install): a file dropped while the app runs
// shows at the next launch, and the list is immutable for the process after
// the load.
//
// THE LIST — the value list's order, under THREE GROUPS, the headings exact
// (gui_theme.h's kGuiThemeGroup*):
//   SYSTEM        `default` FIRST — ARC-DARK (architect 2026-11-18, send
//                 Y: "arc_dark and 75 will be my default" — "let the remote
//                 session make that the default"): a copy of `arc_dark`'s
//                 row under the slug `default`, the opening theme and the
//                 fallback below — then BY SLUG every file of
//                 `themes/system/` (Live's stock themes; `arc_dark`, the
//                 default's source, so after `angst_robot`; `immaterial_mod`,
//                 D5: alphabetical, so right after `immaterial`; and
//                 `sb_gunmetal_mod`, a SYSTEM file since it was the
//                 default's source, 2026-11-12, so after `riparian`) AND THE
//                 COMPILED REFERENCE ROW as `default_dark_neutral_medium`
//                 (kGuiReferenceThemeTones, gui_theme.h: Live's Default Dark
//                 Neutral Medium, compiled, no file shipping it). Arc-Dark
//                 is listed twice, as `default` and under its own slug.
//   EXTRA         every file of `themes/extra/` by slug: the Sonic Bloom
//                 pack as he curated it (2026-11-07) less Gunmetal, the five
//                 `_mod` files his hand edits, each recorded in its header
//                 comment — the pack alone.
//   USER-DEFINED  his own files beside the config, by slug (then by file
//                 name). A user file whose slug a shipped row bears
//                 (`default`, `default_dark_neutral_medium` and every SYSTEM
//                 and EXTRA slug) or an earlier user file bears is LEFT OUT
//                 with its line (D9: the earlier group wins).
// A heading shows only over rows (settings_editor.h's value_list_groups).
//
// A BUNDLED FILE IS THE PROGRAM'S OWN, so its fault is A BUILD DEFECT, the
// validation doctrine's class (1) as icons::load_set's: a shipped file left
// out by the rules above, a shipped file not named by its own slug, two
// shipped files of one slug, a shipped file slugging to `default` or
// `default_dark_neutral_medium`, or no `arc_dark` (the default's source,
// kGuiDefaultThemeSourceSlug) among SYSTEM's files makes the load answer
// the error, and gui_main prints one line and opens no window.
//
// A CONFIG NAMING A SLUG NO ROW BEARS (a user file removed) OPENS IN
// `default` with one stderr line, the config's line untouched until he picks
// another (gui_main; the reader judges the shape alone, device_config.h's
// THE theme GRAMMAR).
//
// THE DERIVATION — our Default tone = Live's role, each Default number the
// reference row's (Live's Default Dark Neutral Medium; palette_file.h's
// static_asserts). A ROLE-NAMED role is read FLAT, its alpha byte dropped:
// each is 6-digit in every shipped file (audited 2026-11-06, session 81). A
// role found carrying alpha is COMPOSITED over the surface it paints on,
// never stripped (architect 2026-11-06, session 81, his "ok to implement"):
// the COMPOSITED block.
//
//   ROLE-NAMED (one Live role each):
//     ground                 242424 = SurfaceArea (ruling 2, 2026-11-05: the
//                                     arrangement's floor, his "#56" on Classic
//                                     Medium Light over the panels' DADADA)
//     lane                   2A2A2A = Desktop (the marker lane)
//     key press face         2A2A2A = Desktop (the press arc, 2026-11-05: his
//                                     pick p2 over ControlBackground 1E1E1E —
//                                     "press in skeumorphic is darker")
//     key face, pane, time field, magnify core
//                            363636 = SurfaceBackground (the core below)
//     cue outline            111111 = ControlContrastFrame — THE ENABLED
//                                     CHIP'S OUTLINE (a disabled chip's is
//                                     the DERIVED block's inactive cue
//                                     outline), THE SELECTION STEM on every
//                                     selected chip, the rings, the ruler
//                                     head's outline
//     text                   B5B5B5 = ControlForeground (the keys' glyphs, the
//                                     clocks, the status bar)
//     dim text, well frame   757575 = TextDisabled (the scrub channel and the
//                                     tooltip border)
//     inactive cue           5D5D5D = BackgroundClip
//     cue, cue fill          FFAD56 = ViewCheckControlEnabledOn, THE
//                                     ACCENT: A LIT KEY'S WHOLE FACE, the
//                                     lamps', the seven doors' and the
//                                     on-screen keyboard's two lit keys alike
//                                     (architect 2026-11-11, session 106: "we
//                                     should go back to where the accent color
//                                     is the background of the lit lamp" —
//                                     Live's "on"; materials.h's THE KEY) —
//                                     every lit key but the radio keys'
//                                     (below) — AND EVERY ENABLED MARKER'S
//                                     CHIP, the `h` view's added and changed
//                                     items too: `cue_fill`, its own member,
//                                     the same word (architect 2026-11-13,
//                                     session 119: "we revert the rule so that
//                                     markers get accent color instead of icon
//                                     glyph color, and we simply tune the
//                                     accent itself ... so that the accent
//                                     shows up everywhere as slightly darker
//                                     for the dark themes" — the tuning THE
//                                     BRIGHT MIX below; filled beside the lit
//                                     key's derived tones, so the install's
//                                     chip is the mixed accent). immaterial_mod's
//                                     file carries its shaded gold here, its
//                                     header the recipe
//     radio lit fill         B595FC = ScaleAwareness, THE PURPLE: A LIT RADIO
//                                     KEY'S WHOLE FACE, the eight of
//                                     redesign_button_is_radio — SOURCE /
//                                     TARGET, WARP / PHASE, FOLLOW / RESTR,
//                                     TRIM / ADD SEL (architect 2026-11-11,
//                                     session 107: two always-lit keys in the
//                                     accent read too bright at the top right
//                                     of his field of vision, "the purple is
//                                     perfectly fine in my peripheral field of
//                                     vision"; materials.h's THE RADIO KEYS).
//                                     Every theme's own role, immaterial_mod's
//                                     Immaterial's (its file changes the
//                                     accent alone)
//     playhead head          03C3D5 = RangeDefault
//     selection fill         637E86 = StandbySelectionBackground
//     selection text, text on lit, card ink
//                            070707 = StandbySelectionForeground — THE
//                                     SELECTION'S TEXT, Live's own partner of
//                                     the bar (architect 2026-11-06, session
//                                     75); THE PRINT ON A LIT KEY'S
//                                     FACE too, its glyph and the on-screen
//                                     keyboard's lit cap, and THE CARDS'
//                                     WORDS AND GLYPH (architect 2026-11-17,
//                                     session 140: Arc prints its
//                                     $selected_fg_color white on its checked
//                                     blue, and D4, "white card words", its
//                                     warning text on the warning orange; on
//                                     Live's own files the partner is
//                                     near-black, so the print stays dark on
//                                     the accent; key_ink, materials.h)
//     list press fill        B0DDEB = SelectionBackground (the press arc,
//                                     2026-11-05: "lighter for list
//                                     (highlight)")
//     list press text        070707 = SelectionForeground
//     card face              E76942 = Alert (the cards; THE LIMITER LAMP too,
//                                     architect 2026-11-05: "orange for limiter
//                                     and red for clipper" — a warning)
//     clip lamp              FF0A0A = StandardVuMeter/Maximum, the one NESTED
//                                     role: Live's own meter clip box — THE
//                                     CLIP LAMP's lit face (architect
//                                     2026-11-05, his d6 "yes": a fault).
//                                     A key's lit look REFUSED for it:
//                                     "clip indicator is not a button"
//   OUR OWN (no Live role; the Arc page, architect 2026-11-18 / 2026-11-19,
//   send Z — tmp/sendZ/DESIGN.md's THE TONES). Live's themes paint one
//   ground and one text tone where Arc parts the title bar from the body
//   under it and prints the title and the sidebar in tones of their own, so
//   no Live role can be read for them: these three are ours, named for what
//   they are. arc_dark's file carries Arc's numbers; EVERY OTHER FILE
//   CARRIES ITS OWN NEAREST EXISTING VALUE (kOwnThemeRoles' source: the body
//   tone its SurfaceArea, both texts its ControlForeground), so on a Live
//   theme they change no look:
//     body face              242424 = WindowBackground, THE BODY TONE (Arc's
//                                     $bg_color, arc_dark 383C4A): the ground
//                                     of everything under the title bar — his
//                                     "the darkest color would only apply to
//                                     the top line, the title bar" (2026-11-18).
//                                     THE GROUND (SurfaceArea) STAYS THE TITLE
//                                     TONE, THE BRIGHT MIX'S TARGET and the
//                                     launcher icon's ground; the palette's
//                                     `face` is this tone since the send's
//                                     stage B, and the lit-but-inert pairs
//                                     below stand on it
//     title text             B5B5B5 = TitleForeground, xfwm's title text on
//                                     the title bar (arc_dark AFB8C5)
//     sidebar text           B5B5B5 = SidebarForeground, Arc's
//                                     $dark_sidebar_fg (arc_dark BAC3CF): the
//                                     marker labels' ink since the send's
//                                     stage B, the tooltip's and the cards'
//                                     in its stage D
//   ROLE-NAMED, continued:
//     core                   363636 = SurfaceBackground, THE KEY FACE, on
//                                     every theme (architect 2026-11-07,
//                                     session 83, his pick i4 of tmp/s83 on
//                                     real MAGNIFY-lit captures, Default and
//                                     Immaterial judged: a lighter core from a
//                                     tone the palette already holds, no AA
//                                     halo) — the MAGNIFY-lit inner bar's solid
//                                     fill, one step under Default's 393939
//                                     canvas, so the center line (the ink, over
//                                     the plate) reads hard across it, ruled
//                                     fine (his "yes"). Before it
//                                     ControlBackground (1E1E1E on Default,
//                                     session 80, 2026-11-06; the record at
//                                     render.h's THE CORE)
//   COMPOSITED (Live's role over the surface it paints on, straight alpha
//   aa/255, a 6-digit word opaque and its bytes kept exactly; rounded half to
//   even, clamped):
//     ink                    191919 = WaveformColor over DetailViewBackground
//     canvas                 393939 = DetailGridTiles over DetailViewBackground
//     level fill             007383 = RangeEditField over SurfaceBackground
//                                     (Live's volume color) — THE NOISE LEVEL
//                                     CASE'S FILL on the key face
//                                     (paint_level_case; architect 2026-11-11:
//                                     "give noise the volume color from the
//                                     ask ... so at 0 it wears the full vol
//                                     color, and at -inf it wears the button
//                                     case color"); a file's 8-digit word
//                                     composited over the face, never stripped
//                                     (architect 2026-11-06, session 81)
//     center                 = ink (a role is what it paints: the zero line IS
//                                   the ink, palette_file.h)
//     score pane             = ink (modal_box.h's score pane: "from ableton
//                                   palette directly")
//     grid                   303030 = TakeLaneTrackNotHighlighted over the
//                                     canvas (the quarter lines)
//     key dim ink            5D5D5D = ViewCheckControlDisabledOff over
//                                     SurfaceBackground (the key face; 8-digit
//                                     D3D3D340 on SB Cielo / Onyx) — a
//                                     disabled key's glyph, the
//                                     struck clip lamp's square
//     trim span              696969 = ScrollbarInnerHandle over Desktop —
//                                     THE TRIM BAR'S PILL at rest (an opaque
//                                     word in every shipped file, so the
//                                     surface under it changes no byte)
//     trim bar (the trough)  242424 = ScrollbarInnerTrack over the ground
//                                     (SurfaceArea): THE TRIM BAR'S 12-W
//                                     TROUGH under the pill, Live's scrollbar
//                                     track, the handle's partner (architect
//                                     2026-11-17, session 140, ruling 4: "the
//                                     scroll bar and the trough should be
//                                     accurate to whatever Arc uses" — Arc's
//                                     $_scrollbar_bg_color in arc_dark's
//                                     file). Live's own files carry it
//                                     00000000, transparent, so on them the
//                                     trough is the ground itself
//     well border            2A2A2A = ClipBorder over the ground: THE WELL'S
//                                     FRAME — its one-W border round trim,
//                                     ruler, marker lane and canvas, the
//                                     three lane lines inside it and the
//                                     stereo divider (architect 2026-11-17,
//                                     session 140, D2: "lets use the thunar
//                                     border" — Arc's $borders_color, Live's
//                                     clip frame; render.h's THE WELL'S
//                                     FRAME)
//     ruler face             393939 = THE CANVAS (the composite above):
//                                     THE RULER WEARS THE CANVAS'S TONE
//                                     (architect 2026-11-17, session 140,
//                                     ruling 3: "otherwise it looks too
//                                     bright"; "the ruler lane is light, the
//                                     marker lane is dark, and then the canvas
//                                     is lighter. That makes sense, and I
//                                     prefer to use that")
//     ruler tick             919191 = DetailViewRulerMarkings over the ruler
//                                     face, the canvas (Live's light ticks;
//                                     Arc's scale marks, D3DAE366 -> 7B818C,
//                                     session 140)
//   The grid, the key dim ink, the trim span and the ruler tick were each a
//   Default mix of two roles; the role nearest it in hex on Default is the
//   faithful reading, so every theme now reads Live's own number (architect
//   2026-11-07, session 82). Default's key dim ink 5D5D5D equals inactive
//   cue's BackgroundClip — Live's own number, accepted. Every composited role
//   is present in every shipped file and in Default (the trough's and the
//   frame's added by hand 2026-11-17, each file's from its own .ask).
//   DERIVED (the app's own tones, no Live role near them; every mix a + t ·
//   (b − a) per channel, rounded half to even and clamped; "X at t over Y"
//   is mix(Y, X, t)). The cue fill above and every tone below but the
//   inactive cue outline and the Arc page's three (the ruler's digits, the
//   disabled key's face and border: no bright tone under them) are ONE BODY
//   (fill_dependent_tones, theme_roles.cpp) reading the row's own words, so
//   the install recomputes them off the mixed bright tones (THE BRIGHT MIX,
//   below) — the inert prints among them, over the mixed inert faces, the
//   print itself and the body tone never mixed:
//     ruler text             9C9C9C = THE TEXT AT 80 % OVER THE RULER FACE,
//                                     mix(ruler face, text, 0.8): THE RULER'S
//                                     DIGITS on the Arc page, Arc's column
//                                     header (Thunar's; arc_dark D3DAE3 over
//                                     404552 -> B6BCC6; architect 2026-11-18,
//                                     send Z: "we can go as very truthful,
//                                     which I think is what we've done here")
//     key dim face           2E2E2E = THE KEY FACE AT 55 % OVER THE BODY TONE,
//                                     mix(body, key face, 0.55): A DISABLED
//                                     KEY'S FACE on the Arc page, Arc's
//                                     button:disabled background (G3:148,
//                                     rgba($button_bg, 0.55); arc_dark 444A58
//                                     over 383C4A -> 3F4452, one level off on
//                                     two channels from the 3E4451 measured
//                                     on his desktop — the formula taken, as
//                                     for the border)
//     key dim border         272727 = THE WELL'S BORDER AT 55 % OVER THE BODY
//                                     TONE, mix(body, well border, 0.55): THAT
//                                     KEY'S BORDER, Arc's button:disabled
//                                     border (G3:148, rgba($borders_color,
//                                     0.55); arc_dark 2B2E39 over 383C4A ->
//                                     313441, the measured 313440 one level
//                                     off — the formula taken). Its drawing is
//                                     the key dim ink (COMPOSITED)
//     playhead line          81E1EA = FFFFFF + 0.5 · (head − FFFFFF), white
//                                     EXACTLY HALF WAY to the head on every
//                                     theme (architect 2026-11-12, session
//                                     113): Live's measured fade FFFFFF <->
//                                     03C3D5 is linear in sRGB bytes, its true
//                                     mean the half; a channel whose distance
//                                     from 255 is odd lands on a .5, which
//                                     rounds to the even byte like every mix
//                                     here. HALF WAY TO THE MIXED HEAD at the
//                                     install (architect 2026-11-13, session
//                                     119: "that rule should stay the same")
//     inactive cue outline   242424 = ControlContrastFrame + 0.75 ·
//                                     (Desktop − ControlContrastFrame),
//                                     THE DISABLED CHIP'S OUTLINE, the `h`
//                                     view's removed items' too: the enabled
//                                     chip's outline moved 75 % OF THE WAY
//                                     TOWARD DESKTOP, THE MARKER LANE'S
//                                     FLOOR the chip stands on, so a disabled
//                                     chip's edge recedes as if blended into
//                                     the lane (architect 2026-11-12, session
//                                     114: "the disable outline, I think,
//                                     should be a different rule in the code,
//                                     so no change to the themes needed"; 75 %
//                                     his pick on the tablet over 50 %;
//                                     2026-11-13, session 121: "this will be
//                                     marker lane ground ... this is supposed
//                                     to be a sort of alpha effect, even
//                                     though it's not truly an alpha" — SB
//                                     Gunmetal_mod 0B0D11 toward 24262D ->
//                                     1E2026). Its 0.75 is its own number, not
//                                     the bright mix's, and THE BRIGHT MIX
//                                     NEVER TOUCHES THIS TONE: it is read off
//                                     the row's own roles, never a bright
//                                     word, and is no bright tone itself (his
//                                     "the disabled color wasn't brightness
//                                     modulated by the ground"). The
//                                     selection stem of a selected disabled
//                                     chip, the ruler
//                                     head's outline and the lead-in ring stay
//                                     the cue outline (resolve_flag_face,
//                                     render.cpp)
//     lit press fill         C68743 = THE ACCENT DARKENED BY THE PRESS RATIO:
//                                     the accent (cue) · (mean(press face) /
//                                     mean(key face)) per channel, the means
//                                     over the three sRGB bytes, rounded half
//                                     to even and clamped (42 / 54 = 0.778 on
//                                     Default: FFAD56 -> C68743) — A CHECKED
//                                     KEY'S FACE UNDER A STANDING PRESS
//                                     (architect 2026-11-11, session 106: "if
//                                     it's a lit lamp, it gets darkened based
//                                     on the starting point of the accent
//                                     color"; the press arc's ruling of
//                                     2026-11-05, his "ratio" on mocks d1 / i1
//                                     of tmp/s45). THE RATIO, NOT THE
//                                     DIFFERENCE: the ratio keeps the theme's
//                                     lit hue and scales with its own press
//                                     step. A theme whose key face is black
//                                     (no divisor) keeps the accent whole,
//                                     ratio 1; where the press face is lighter
//                                     than the key face the lit press comes
//                                     out lighter — Live's numbers, no rule
//     lit dim fill, text on lit dim
//                            86623B, 2A221A = THE LIT-BUT-INERT PAIR ON ARC'S
//                                     OWN NUMBERS (architect 2026-11-19, send
//                                     Z: "let's do arcs numbers"; Arc's
//                                     button:disabled:checked, G3:152-154: the
//                                     face rgba($selected_bg_color, 0.75)
//                                     under opacity 0.6, the print
//                                     rgba($selected_fg_color, 0.8) — over the
//                                     body tone a button stands on): the face
//                                     (THE ACCENT AT 75 % OVER THE BODY TONE)
//                                     AT 60 % OVER THE BODY TONE, mix(body,
//                                     mix(body, cue, 0.75), 0.6), the inner
//                                     word rounded (Default: FFAD56 over
//                                     242424 -> C88B4A -> 86623B; Arc at 100
//                                     5294E2 over 383C4A -> 4C7EBC -> 44648E,
//                                     at the default 75 the mixed 497CB9 ->
//                                     456C9D -> 40597C), the glyph DIMMED
//                                     WITH THE BUTTON, as GTK's `opacity`
//                                     dims the label (DESIGN §3; send Z's
//                                     stage B, 2026-11-19, correcting the
//                                     kit's brighter reading, which the
//                                     approved picture Z_02 drew): (THE PRINT
//                                     ON A LIT FACE AT 80 % OVER THE
//                                     ACCENT-AT-75 % FACE) AT 60 % OVER THE
//                                     BODY TONE, mix(body, mix(mix(body, cue,
//                                     0.75), text on lit, 0.8), 0.6) (Default
//                                     070707 -> 2A221A; Arc FFFFFF -> 9AA1AF,
//                                     at 75 99A0AB), per channel, rounded half
//                                     to even and clamped. The body tone is
//                                     the theme's own, never mixed; the accent
//                                     the installed one, so the pair follows
//                                     THE BRIGHT MIX. One rule on every theme,
//                                     a light one too (the Live light themes
//                                     set aside, ruling 7 of 2026-11-17).
//                                     Arc's insensitive-active of 2026-11-17,
//                                     session 140, D5 (the color at 75 % over
//                                     the ground, no opacity), stood until
//                                     this rule. The face and the
//                                     glyph of A CHECKED-BUT-DISABLED KEY (a
//                                     lamp on a locked tab or under grid
//                                     iterations, a lit key under an overlay,
//                                     2026-11-11, session 107) — the state
//                                     reads true and dead as one unit
//                                     (architect 2026-11-11, session 106: "dim
//                                     the accent and the glyph so both read as
//                                     a unit"; paint_key and key_ink,
//                                     materials.h)
//     radio lit press fill   8D74C4 = the radio lit fill · THE SAME PRESS
//                                     RATIO, mean(press face) / mean(key
//                                     face), per channel, one code path with
//                                     the lit press fill (42 / 54 on Default:
//                                     B595FC -> 8D74C4) — a checked radio
//                                     key's face under a standing press
//                                     (architect 2026-11-11, session 107),
//                                     the black key face's ratio 1 as above
//     radio lit dim fill, radio text on lit dim
//                            655785, 232029 = THE RADIO KEY'S LIT-BUT-INERT
//                                     PAIR, the same rule on the purple
//                                     (2026-11-19): the face (the radio lit
//                                     fill at 75 % over the body tone) at 60 %
//                                     over the body tone (Default: B595FC ->
//                                     9179C6 -> 655785; Arc at 100 73D216 ->
//                                     527F33), the glyph by the accent's rule
//                                     on the purple (Default 232029; Arc at
//                                     100 9DA79C) — a tone of its own, since
//                                     the print is over its own face;
//                                     key_ink parts it by the key's lit class.
//                                     The radio class's own tones retire in
//                                     the send's stage C ("we can let go of
//                                     the green")
//
// THE BRIGHT MIX (architect 2026-11-13, session 119, on real tablet captures
// of Riparian and Gunmetal: "the whole point of it is that it dims ... the
// supposedly low contrast Ableton themes are still very high contrast with
// the buttons. Now, buttons in Ableton are tiny. Here they have to be a lot
// larger for the pen tab"; "I like all 75"; "a universal rule of 75 should
// work"). THE LIST'S ROWS ARE EACH THEME'S OWN NUMBERS — the blocks above,
// the compiled reference row Live's measured ones, the files untouched ("we
// don't have to modify the Ableton imports. We just make the rule") — and
// THE INSTALL PAINTS EACH ROW MIXED (install_palette, render.h): EVERY
// BRIGHT TONE moved toward THE THEME'S GROUND, SurfaceArea — the window
// ground, NOT the lane's Desktop ("towards ground, actual ground, because
// the buttons are the largest ones, and they're the ones that are the
// bigger problem. The marker chips are small, and they should follow to
// match the buttons") — KEEPING k % OF ITSELF: tone + (1 − k / 100) ·
// (ground − tone) per channel, rounded half to even and clamped like every
// mix here, k THE `bright_mix` DEVICE KEY (device_config.h: whole percent
// in [0, 100], 100 the theme's own numbers, 0 every bright tone the ground
// itself; ITS DEFAULT 75 since 2026-11-18, architect, send Y: "arc_dark
// and 75 will be my default" — the key stays, a ruled number keeping its
// key). ONE NUMBER FOR EVERY THEME, the light ones included, where the
// mix lifts the tones toward a light ground (his "we can deal with that
// when the time comes by just adjusting" the key; no per-theme value: "I
// don't think it's worth redoing the presets so that each preset gets its
// own value"). THE BRIGHT TONES, his list (kBrightTones, theme_roles.cpp):
// the accent `cue`, the radio lit fill (ScaleAwareness), the playhead head
// (RangeDefault), the level fill (the composite, mixed as it is painted),
// the card face (Alert, the cards and the limiter lamp), the clip lamp
// (StandardVuMeter/Maximum), the selection fill
// (StandbySelectionBackground) and the list press fill
// (SelectionBackground) — the two selection fills together ("standby
// selection should get the rule. Otherwise, it's going to look odd next to
// the selection background getting the rule"). EVERY TONE DERIVED FROM A
// BRIGHT ONE DERIVES FROM ITS MIXED WORD (fill_dependent_tones, the one
// body): the cue fill — the markers wear the mixed accent — the lit press
// and lit dim fills, the radio press and dim fills, the two inert prints
// over the mixed inert faces, and the playhead line, white half way to the
// mixed head. NEVER MIXED: the text tones ("never the glyph color itself":
// the text, the dim text, the key dim ink, the title, sidebar and ruler
// texts — the keys' glyphs, the markers' labels), the print on a lit face, the selection and list
// press texts, the cards' words, the faces, the lanes (the trim
// bar's trough and the well's frame among them, 2026-11-17), the Arc page's
// tones (2026-11-19: the body tone, the title bar's text, the sidebar text,
// the ruler's digits, the disabled key's face and border), the waveform's
// tones, the cue outline, the inactive cue and its outline
// (dark: "those weren't bright to begin with") and the scanner (below). At
// the default 75 on the reference row: the accent FFAD56 -> C88B4A, the
// purple B595FC -> 9179C6, the head 03C3D5 -> 0B9BA9 and its line 81E1EA
// -> 85CDD4 (bright_mixed_tones, the code); on Arc-Dark the accent 5294E2 ->
// 497CB9, the head D3DAE3 -> AAB0BA; at 100 every row is painted at its own
// numbers.
//
// THE SCANNER IS NEITHER A ROLE NOR A DERIVED TONE: FFFFFF on every theme,
// CANONICAL — Live itself paints its scanner white on every theme tested,
// Default and the built-in themes included, so no theme's file carries it
// (architect 2026-11-07, session 82: "it comes from all live themes tested -
// including the default and builtin"; 2026-11-06: "a look online shows it's
// canonically full white", his Live 12 reference shot). It is never proposed
// as a role or a mix.
//
// Where a theme makes the lane, the trim bar's trough, the well's frame or
// the dim text coincide with the ground, that is Live's own number —
// accepted, not a bug, no backstop (architect 2026-11-05; the trough
// 2026-11-17: Live's transparent ScrollbarInnerTrack).
//
// THE SONIC BLOOM FILES' UNDEFINED ROLES: a pack file may carry a role it does
// not use as 000000 (ScrollbarInnerHandle black on 12 SB files,
// DetailViewRulerMarkings on 4; of the shipped files, ScrollbarInnerTrack an
// opaque 000000 on SB Cielo, Dash, Elegance, Onyx, Rosequartz and Viridian,
// ClipBorder on SB Dash, Elegance, Rosequartz and Viridian). NO RULE CORRECTS IT: he edits the individual
// theme by hand, a `_mod` file of the shipped themes (architect 2026-11-07,
// session 82: "i'd rather manually fix individual sb themes than make rules
// around their missing colors").
//
// The derivation is total and leaves no file out. ITS ARITHMETIC IS
// PYTHON'S, BIT FOR BIT (the derivation was first written in Python, and the
// proof of 2026-11-06 diffed every row on the tablet against its numbers):
// doubles in its operation order, Python's round() as nearbyint under the
// default rounding mode (half to even; theme_roles.cpp).

// Build THE LIST (the head's groups and order), once per process, before
// the palette's first install: `system_files` and `extra_files` the shipped
// files, each a map from FILE NAME (`<slug>.theme`, no folder) to the file's
// whole text — the APK's `themes/system/` and `themes/extra/` assets
// (GuiPlatform::bundled_theme_files), or any host's reading of the
// repository's folders — and `user_dir` the user's flat folder (an empty
// path is the absent folder: no config home). THE ERROR ARM is the head's
// build defect (a shipped file's fault), the message naming the file;
// nothing is listed then. A user file's fault is its stderr line alone.
std::optional<std::string> load_gui_themes(
    const std::map<std::string, std::string>& system_files,
    const std::map<std::string, std::string>& extra_files,
    const std::filesystem::path& user_dir);

// THE LIST in the value list's order, `default` first. Empty before the
// load.
const std::vector<GuiTheme>& gui_themes();

// THE ROW WHOSE SLUG IS `slug`, or nullptr when no row bears it: the
// settings editor's `theme` commit refuses such a slug (only a typed line
// can name one), the launch opens in `default` (the head).
const GuiTheme* find_gui_theme(std::string_view slug);

// The `default` row, Arc-Dark's copy (the list's first; the
// load must have answered no error).
const GuiTheme& default_gui_theme();

// THE ROW AS INSTALLED (the head's THE BRIGHT MIX): `own`, a row of the list
// at its own numbers, with its bright tones mixed toward its ground keeping
// `percent` of themselves (the `bright_mix` key, an integer in [0, 100]) and
// every tone derived from them recomputed. At 100 it returns `own` byte for
// byte. Pure; install_palette (render.cpp) is its one product caller.
GuiThemeTones bright_mixed_tones(const GuiThemeTones& own, int percent);
