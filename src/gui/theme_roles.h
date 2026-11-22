#pragma once

#include "gui_theme.h"   // GuiThemeTones

#include <expected>
#include <string>
#include <string_view>

// THE THEME FILE AND THE DERIVATION (architect 2026-11-05, the theme arc;
// 2026-11-06, session 75: this file the derivation's one owner; architect
// 2026-11-11, session 110, THE THEME FILES: OUR OWN KEYED FORMAT; ONE THEME
// since 2026-11-21, send GG: "i am committed" — Arc-Dark alone, the themes'
// list and the `theme` key gone with every other theme). The theme is one
// file of LIVE'S RAW ROLE VALUES — the roles THE DERIVATION below reads,
// under Live's own role names, never a derived tone — PLUS FIFTEEN ROLES OF
// OUR OWN (THE FORMAT, below), and the derivation, this file's, turns it
// into the row of GuiThemeTones at the launch's read.
//
// THE FORMAT (its one owner here). The theme file is ASCII text (UTF-8 in
// a comment), a line ending `\n` — a `\r` before it is stripped, so a file
// saved with CRLF reads the same — and the last line's `\n` optional. A
// line is EMPTY, A COMMENT (its first byte `#`, the rest anything), or A
// ROLE LINE `Name=HEX` exactly: no space anywhere, Name one of kThemeRoles
// (theme_roles.cpp; Live's role names, the one nested role flattened to the
// key `StandardVuMeter/Maximum` — Live's own meter clip box — AND OUR OWN
// FIFTEEN, WindowBackground, TitleForeground and SidebarForeground, the
// three fills TooltipBackground, CardBackground and CardStandingBackground,
// ScaleTrough (send AA's stage D2), ScaleSliderBorder (send AA's stage D3),
// the two hover tones ButtonHoverBackground and ScrollbarSliderHover (send
// CC) and the five of the waveform's hover PlayheadLine, HoverLine,
// CueHover, CueDisabledHover and CueDisabledLabelHover (send FF),
// kOwnThemeRoles: no Live role names them), HEX 6 or 8 hex digits,
// case-blind on read, an 8-digit word's last byte its straight alpha — kept
// raw, the derivation compositing it (the COMPOSITED block) or, for the
// three fills, CARRYING IT TO THE PAINTER (the CARRIED block). Every role of
// the table exactly once, in any order.
//
// THE FILE. The one shipped file is the repository's
// `themes/system/arc_dark.theme` (kGuiDefaultThemeSourceSlug, gui_theme.h),
// WRITTEN BY HAND from the arc-theme sources, no .ask behind it, its header
// the record of every role (architect 2026-11-17, session 140), packed by
// apk/build_apk.sh as the APK's `themes/system/` asset and read in place at
// launch (GuiPlatform::bundled_theme_file). READ ONCE AT LAUNCH
// (load_gui_theme, gui_main, after the config and before the palette's first
// install).
//
// THE FILE IS THE PROGRAM'S OWN, so its fault is A BUILD DEFECT, the
// validation doctrine's class (1) as icons::load_set's: the file missing or
// unreadable in the APK (the platform's read), a malformed line, a role name
// the table lacks, a role twice or a role missing (the parse, its line
// number where it has one) makes the launch print one line and open no
// window.
//
// THE DERIVATION — our Default tone = Live's role, each Default number Live's
// Default Dark Neutral Medium's (the record of the derivation's Live
// origin; arc_dark's own number named beside it where the telling needs
// it). A ROLE-NAMED role is read FLAT, its alpha byte dropped: each is
// 6-digit in the shipped file. A role found carrying alpha is COMPOSITED
// over the surface it paints on, never stripped (architect 2026-11-06,
// session 81, his "ok to implement"): the COMPOSITED block.
//
//   ROLE-NAMED (one Live role each):
//     ground                 242424 = SurfaceArea (ruling 2, 2026-11-05: the
//                                     arrangement's floor, his "#56" on Classic
//                                     Medium Light over the panels' DADADA)
//     lane                   2A2A2A = Desktop (the marker lane)
//     key face, magnify core
//                            363636 = SurfaceBackground (the core below)
//     cue outline            111111 = ControlContrastFrame — THE SELECTION
//                                     STEM on every selected chip and the
//                                     lead-in ring (no chip and no ruler
//                                     head wears an outline since
//                                     2026-11-19, send AA)
//     text                   B5B5B5 = ControlForeground (the keys' glyphs, the
//                                     clocks, the status bar)
//     dim text               757575 = TextDisabled
//     inactive cue           5D5D5D = BackgroundClip
//     cue, cue fill          FFAD56 = ViewCheckControlEnabledOn, THE
//                                     ACCENT: A LIT OR PRESSED KEY'S FACE
//                                     (Arc's `:checked` and `:active`, one
//                                     look since send Z, 2026-11-19), every
//                                     checked key's — the lamps', the seven
//                                     doors', the four exclusive pairs' (the
//                                     radio keys' ScaleAwareness retired that
//                                     day) and the on-screen keyboard's two
//                                     lit keys alike (architect 2026-11-11,
//                                     session 106: "we should go back to where
//                                     the accent color is the background of
//                                     the lit lamp" — Live's "on";
//                                     materials.h's THE KEY) — ARC'S ENTRY
//                                     PROGRESS on NOISE (send Z) — AND EVERY
//                                     ENABLED MARKER'S
//                                     CHIP, the `h` view's added and changed
//                                     items too: `cue_fill`, its own member,
//                                     the same word (architect 2026-11-13,
//                                     session 119: "we revert the rule so that
//                                     markers get accent color instead of icon
//                                     glyph color, and we simply tune the
//                                     accent itself ... so that the accent
//                                     shows up everywhere as slightly darker
//                                     for the dark themes"; filled beside the
//                                     lit key's derived tones, the chip the
//                                     accent's own word). immaterial_mod's
//                                     file carries its shaded gold here, its
//                                     header the recipe
//     (radio lit fill        = ScaleAwareness, the radio keys' purple of
//                                     2026-11-11, session 107, RETIRED with
//                                     the class and the role 2026-11-19, send
//                                     Z's stage C: "we can let go of the
//                                     green")
//     playhead head          03C3D5 = RangeDefault (arc_dark A5ABB5, the
//                                     stem's color, PlayheadLine's number:
//                                     "the triangle for the playhead should
//                                     probably become the same color as the
//                                     stem", architect 2026-11-21, send GG)
//     selection fill         637E86 = StandbySelectionBackground
//     selection text, text on lit, card ink
//                            070707 = StandbySelectionForeground — THE
//                                     SELECTION'S TEXT, Live's own partner of
//                                     the bar (architect 2026-11-06, session
//                                     75); THE PRINT ON A LIT KEY'S
//                                     FACE too, its glyph and the on-screen
//                                     keyboard's lit cap, and A STANDING
//                                     CARD'S WORDS AND GLYPH (Arc's white on
//                                     its destructive red, send AA's stage
//                                     D1, 2026-11-19; every card's from
//                                     2026-11-17, session 140, D4, "white
//                                     card words" on the warning orange, a
//                                     normal card's `sidebar_text` since D1;
//                                     Arc prints its $selected_fg_color white
//                                     on its checked blue too; on
//                                     Live's own files the partner is
//                                     near-black, so the print stays dark on
//                                     the accent; key_ink, materials.h)
//     limiter lamp           E76942 = Alert, THE LIMITER LAMP's lit face
//                                     (architect 2026-11-05: "orange for
//                                     limiter and red for clipper" — a
//                                     warning; Arc's warning orange), its
//                                     own tone (the cards wear their own
//                                     fills, the CARRIED block)
//     clip lamp              FF0A0A = StandardVuMeter/Maximum, the one NESTED
//                                     role: Live's own meter clip box — THE
//                                     CLIP LAMP's lit face (architect
//                                     2026-11-05, his d6 "yes": a fault).
//                                     A key's lit look REFUSED for it:
//                                     "clip indicator is not a button"
//   OUR OWN (no Live role; the Arc page, architect 2026-11-18 / 2026-11-19,
//   send Z — the design's THE TONES). Live's themes paint one
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
//                                     TONE and the launcher icon's ground; the palette's
//                                     `face` is this tone since the send's
//                                     stage B, and the lit-but-inert pairs
//                                     below stand on it
//     title text             B5B5B5 = TitleForeground, xfwm's title text on
//                                     the title bar (arc_dark AFB8C5)
//     sidebar text           B5B5B5 = SidebarForeground, Arc's
//                                     $dark_sidebar_fg (arc_dark BAC3CF): the
//                                     marker labels' ink since the send's
//                                     stage B, the tooltip's words and a
//                                     normal card's words and glyph since
//                                     send AA's stage D1
//   AND ONE MORE OF OUR OWN FOR THE PROGRESS BAR (send AA's stage D2,
//   2026-11-19: GTK's progress bar, the progress page's), each file but
//   Arc's its own nearest value, so no Live theme sees it:
//     scale trough           242424 = ScaleTrough over the ground (arc_dark
//                                     2D303B, Arc's progress and scale
//                                     trough): the progress page's bar and
//                                     the scrub's trough; a Live theme's
//                                     ScrollbarInnerTrack, so its trim
//                                     trough's tone
//   AND ONE MORE FOR THE SCRUB (send AA's stage D3, 2026-11-19: Arc's scale,
//   render.h's scrub block), the same way:
//     scale slider border    2A2A2A = ScaleSliderBorder over the ground
//                                     (arc_dark 262933, Arc's slider border):
//                                     the scrub's knob's rim at rest; a Live
//                                     theme's ClipBorder, so its well's
//                                     lines' tone
//   AND TWO MORE FOR ARC'S HOVER (architect 2026-11-18, send Y: "lets go arc
//   truthful re: hoevers"; 2026-11-20, send CC: "If they're from Canonical,
//   from Arc, I'll just accept them"; the hover's rule and its one owner
//   AppState::FaceFades, app_state.h):
//     key hover face         363636 = ButtonHoverBackground, read flat as the
//                                     key face's role is (arc_dark 505666,
//                                     Arc's button:hover, gtk3 CSS line 136,
//                                     measured on his desktop): A KEY'S, A
//                                     WORD BUTTON'S AND AN ON-SCREEN KEYBOARD
//                                     KEY'S FACE AND THE SCRUB'S KNOB'S FILL
//                                     UNDER THE HOVERING PEN, the far end of
//                                     the hover's mix (materials.h's THE
//                                     HOVER); a Live theme's SurfaceBackground
//     trim span hover face   696969 = ScrollbarSliderHover over Desktop, as
//                                     the pill's own role is composited
//                                     (arc_dark 676B78, Thunar's scrollbar
//                                     slider under the pointer, measured on
//                                     his desktop): THE TRIM BAR'S PART UNDER
//                                     THE HOVERING PEN, the pill's body or an
//                                     end's cap (render.h's trim_bar_tones;
//                                     the walk
//                                     GuiInputHandler::walk_trim_bar_hover);
//                                     a Live theme's ScrollbarInnerHandle
//   AND FIVE MORE FOR THE WAVEFORM'S HOVER AND THE PLAYHEAD'S STEM
//   (architect 2026-11-21, send FF: "just make sure they're all colors
//   from Arc, dark"; the brightness order on the waveform his — the
//   scanner and the zoom anchor's stem FFFFFF, "the scanner should be this
//   brightest one", then the playhead's stem, then the hover line, "an even
//   more dark and faint version of the playhead stem"), arc_dark's file
//   carrying Arc's numbers (gtk3.0-arc-dark.css, its header the record),
//   each file but Arc's the word of the tone it stands beside, so on a
//   Live theme the playhead's line keeps its look and NO MARKER HOVER
//   SHOWS (kOwnThemeRoles' source and read). Each is read flat as its
//   neighbor is, the hover line alone composited over the canvas it is
//   drawn on:
//     playhead line          81E1EA = PlayheadLine (arc_dark A5ABB5, Arc's
//                                     `.h4` / `.category-label`; "the stem is
//                                     too bright" at the font color, send FF;
//                                     the head wears it too since send GG):
//                                     THE PLAYHEAD'S STEM ON THE
//                                     WAVEFORM, its one static tone
//                                     (render_playhead). A Live theme's WHITE
//                                     HALF WAY TO ITS RangeDefault,
//                                     mix(white, head, 0.5), rounded half to
//                                     even (architect 2026-11-12, session
//                                     113: Live's measured fade FFFFFF <->
//                                     03C3D5 is linear in sRGB bytes, its
//                                     true mean the half) — written into its
//                                     file, kOwnThemeRoles' WhiteHalfWay read,
//                                     so it is the tone the derivation drew
//                                     there until send FF
//     hover line             696969 = HoverLine over the canvas (arc_dark
//                                     767B87, Arc's scrollbar slider): THE
//                                     WAVEFORM'S HOVER LINE AND ITS TRIANGLE
//                                     IN THE RULER (architect 2026-11-21,
//                                     send GG); a Live theme's
//                                     ScrollbarInnerHandle, its trim bar's
//                                     pill, a modest line there
//     cue hover              FFAD56 = CueHover (arc_dark 639FE5,
//                                     `.documents-entry-tag:hover`, Arc's
//                                     hover of a blue thing): AN ENABLED
//                                     MARKER'S CHIP UNDER THE HOVERING PEN;
//                                     a Live theme's ViewCheckControlEnabledOn,
//                                     the chip's own tone
//     inactive cue hover     5D5D5D = CueDisabledHover (arc_dark 90939B, Arc's
//                                     `wm_icon_bg`, the titlebar glyph awake;
//                                     its resting 666A74 `wm_icon_unfocused_
//                                     bg`, the same glyph of an unfocused,
//                                     fully clickable window): A DISABLED
//                                     MARKER'S CHIP UNDER THE HOVERING PEN —
//                                     a disabled marker hovers, as "our
//                                     disabled marker means that it's ignored
//                                     by the parser, but otherwise fully
//                                     clickable and actionable"; a Live
//                                     theme's BackgroundClip, the inert
//                                     chip's own tone
//     inactive cue label hover
//                            757575 = CueDisabledLabelHover (arc_dark 9DA3AD,
//                                     `.gedit-document-panel row:hover:not(
//                                     :selected) button`, a dim button under
//                                     its row's hover): THAT MARKER'S LABEL
//                                     INK UNDER THE HOVERING PEN, its rest the
//                                     inert label's `text_dim`; a Live
//                                     theme's TextDisabled, that tone itself
//   CARRIED (our own three fills, send AA's stage D1, architect 2026-11-19 —
//   DESIGN §6 / §9, his "good to build" on the mock Z_07 and "if we
//   can do the transparency, we might as well"): a fill PAINTED OVER
//   WHATEVER LIES UNDER IT — the chrome, the lanes, the canvas — so no
//   surface is known to composite it over at the derivation: ITS ALPHA IS
//   CARRIED TO THE PAINTER, the role's three bytes the row word's low 24
//   bits and its alpha byte the word's TOP BYTE (FF for a 6-digit word,
//   opaque; packed_carried, theme_roles.cpp), read back by tone_alpha
//   (render.h) and painted through set_palette_source_alpha (cairo over the
//   chrome; the GPU's blend inside the floaters' hulls over the canvas,
//   canvas_sink.h's THE TRANSPARENCY INVARIANT). Every other row word's top byte is 0. EVERY
//   FILE BUT ARC'S CARRIES ITS OWN NEAREST EXISTING VALUE, OPAQUE
//   (kOwnThemeRoles' source), so a Live theme's tooltip and cards are
//   today's tones:
//     tooltip fill        FF363636 = TooltipBackground (arc_dark 4B5162F2,
//                                     Arc's $osd_bg_color at .95 — the byte
//                                     F2, .949; a Live theme's
//                                     SurfaceBackground, the pane it wore):
//                                     THE TOOLTIP'S BOX (paint_shift_tooltip)
//     card fill           FFE76942 = CardBackground (arc_dark 353945F2,
//                                     xfce4-notifyd's bubble under Arc at .95;
//                                     a Live theme's Alert, the orange it
//                                     wore): A NORMAL (TIMED) CARD'S BOX, a
//                                     surface, notifyd's grey at Arc's own
//                                     number
//     card standing fill  FFE76942 = CardStandingBackground (arc_dark
//                                     F04A50F2, Arc's destructive red,
//                                     button.destructive-action G3:216, at
//                                     .95; a Live theme's Alert): A STANDING
//                                     (CRITICAL) CARD'S BOX
//   (notifications.h's THE LOOK, render.h's THE FLOATING SURFACES.)
//   ROLE-NAMED, continued:
//     core                   363636 = SurfaceBackground, THE KEY FACE, on
//                                     every theme (architect 2026-11-07,
//                                     session 83, his pick i4 of the mocks on
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
//     (level fill            = RangeEditField over SurfaceBackground, the
//                                     NOISE case's volume-colored fill of
//                                     2026-11-11, RETIRED with the role
//                                     2026-11-19, send Z's stage C: NOISE is
//                                     Arc's entry, its level Arc's entry
//                                     progress in the accent)
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
//   Arc page's three (the ruler's digits, the disabled key's face and
//   border) and the list row's hover are ONE BODY
//   (fill_dependent_tones, theme_roles.cpp) reading the row's own words —
//   the inert prints among them, over the inert faces:
//     ruler text             7D7D7D = THE TEXT AT 55 % OVER THE RULER FACE,
//                                     mix(ruler face, text, 0.55): THE RULER'S
//                                     DIGITS, Arc's `.dim-label` (`opacity:
//                                     0.55`, G3:35) of the font color over
//                                     the ruler's ground (arc_dark D3DAE3
//                                     over 404552 -> 9197A2: under the
//                                     triangle A5ABB5, over the hover line
//                                     767B87; architect 2026-11-21, send GG)
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
//     list hover face        393939 = ARC'S ROW HOVER, 3 % WHITE OVER THE
//                                     LIST'S VIEW, rgba(255, 255, 255, 0.03)
//                                     over the ruler face, per channel c +
//                                     0.03 · (255 − c) rounded (derive(),
//                                     beside the three above): A LIST ROW
//                                     UNDER THE HOVERING
//                                     PEN (architect 2026-11-21: "I'd
//                                     like to add hover"; its tone Arc's own
//                                     `row.activatable:hover`, gtk3.0-arc-
//                                     dark.css line 1258, the same day: "I
//                                     prefer to go more truthful. It just
//                                     needs to be very subtle."; paint_
//                                     handler.cpp's THE LIST VIEW). arc_dark
//                                     404552 -> 464B57, the stylesheet's own
//                                     number computed exactly; the desktop's
//                                     8-bit compositing (the alpha as 7 / 255)
//                                     may read one level under it on two
//                                     channels, 454A57 (reasoned, not
//                                     measured) — the formula taken. (The
//                                     arithmetic still asks whether the key
//                                     hover face differs from the key face,
//                                     derive(); on arc_dark it does, 505666
//                                     against 444A58.)
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
//                                     242424 -> C88B4A -> 86623B; Arc
//                                     5294E2 over 383C4A -> 4C7EBC ->
//                                     44648E), the glyph DIMMED
//                                     WITH THE BUTTON, as GTK's `opacity`
//                                     dims the label (DESIGN §3; send Z's
//                                     stage B, 2026-11-19, correcting the
//                                     kit's brighter reading, which the
//                                     approved picture Z_02 drew): (THE PRINT
//                                     ON A LIT FACE AT 80 % OVER THE
//                                     ACCENT-AT-75 % FACE) AT 60 % OVER THE
//                                     BODY TONE, mix(body, mix(mix(body, cue,
//                                     0.75), text on lit, 0.8), 0.6) (Default
//                                     070707 -> 2A221A; Arc FFFFFF -> 9AA1AF),
//                                     per channel, rounded half to even and
//                                     clamped. One rule on every theme,
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
//                                     a unit"; paint_linked_group and
//                                     key_ink, materials.h)
//     (the radio keys' press, inert face and inert print, the purple by the
//     same rules, 2026-11-11 / 2026-11-19, RETIRED with their class
//     2026-11-19, send Z's stage C: "we can let go of the green")
//
// THE SCANNER IS NEITHER A ROLE NOR A DERIVED TONE: FFFFFF on every theme,
// CANONICAL — Live itself paints its scanner white on every theme tested,
// Default and the built-in themes included, so no theme's file carries it
// (architect 2026-11-07, session 82: "it comes from all live themes tested -
// including the default and builtin"; 2026-11-06: "a look online shows it's
// canonically full white", his Live 12 reference shot). It is never proposed
// as a role or a mix. THE WAVEFORM'S BRIGHTNESS ORDER — the scanner, the
// playhead's stem, the hover line (render.h's render_hover_line) — IS THE
// THEME'S OWN NUMBERS' ORDER (architect 2026-11-21, send FF): on Arc-Dark
// FFFFFF, A5ABB5, 767B87.
//
// Where a theme makes the lane, the trim bar's trough, the well's lines or
// the dim text coincide with the ground, that is Live's own number —
// accepted, not a bug, no backstop (architect 2026-11-05; the trough
// 2026-11-17: Live's transparent ScrollbarInnerTrack).
//
// The derivation is total. ITS ARITHMETIC IS
// PYTHON'S, BIT FOR BIT (the derivation was first written in Python, and the
// proof of 2026-11-06 diffed every row on the tablet against its numbers):
// doubles in its operation order, Python's round() as nearbyint under the
// default rounding mode (half to even; theme_roles.cpp).

// THE ONE THEME'S ROW off the shipped file's whole text (the head's THE
// FILE; GuiPlatform::bundled_theme_file, or any host's reading of the
// repository's `themes/system/arc_dark.theme`): the file parsed (THE
// FORMAT) and derived (THE DERIVATION), at its own numbers — or THE ERROR
// ARM, the head's build defect, the parse's first error with its line number
// where it has one. Pure: no state is kept; gui_main installs the row
// (install_palette, render.h).
std::expected<GuiThemeTones, std::string> load_gui_theme(std::string_view text);
