#!/usr/bin/env python3
# tools/score_extract.py — a "sheet music" video, or a PDF of a printed score, -> THE SCORE FOLDER the built-in page
# viewer reads: one grayscale JPEG per page, pre-sized to the viewer's box, pages.txt (the pages' frame ranges, tiling
# the whole video, or the PDF's slices at rate 1) and the video's soundtrack (architect 2026-10-26; THE PDF ROAD
# 2026-10-27).
#
# Standalone: no product target builds or links it (the convention of tools/CMakeLists.txt's head). python3 with
# numpy and Pillow (PIL); for a video ffmpeg and ffprobe on PATH, for a PDF poppler's pdftoppm and pdfinfo (a PDF
# without them is refused in one line).
#
#   score_extract.py <video or PDF> <outdir> [--fit W:H] [--rate R] [--url LINK]
#
# THE INPUT IS A PDF when its first bytes are `%PDF-` (PDF_MAGIC), and anything else is a video: the PDF takes THE PDF
# ROAD (its block below), the video every other block of this head.
#
# <outdir> is the score/ folder, created if absent. A run writes into it:
#   page001.jpg, page002.jpg, ...  the pages (1-based, three digits, four from page 1000 on), grabbed at the fit size
#                                  (--fit), GRAYSCALE JPEG AT QUALITY 92 (PAGE_JPEG_QUALITY); a PDF's are its slices;
#   pages.txt                      the pages' ranges (THE GRAMMAR OF pages.txt);
#   audio.wav                      the video's soundtrack, pcm_s16le STEREO (`-ac 2`: a mono stream is doubled) at
#                                  the stream's own rate (AUDIO_WAV): the score's audio is the video's, the score
#                                  viewer's player plays it (architect 2026-10-26 night); a video with no audio
#                                  stream writes none and removes one already in the folder (architect 2026-10-27),
#                                  and so does a PDF;
#   scan.npy, scan.meta            a video's scan cache (SCAN_NPY); a PDF writes none.
# A rerun overwrites them, every page grabbed again whether or not the scan is cached (SCAN_META), and removes, by
# name, every page<NNN>.png in the folder (the spelling this tool no longer writes) and every page<NNN>.jpg numbered
# past the new count or misspelled (stale_pages). A refusal (one line, exit 1) writes nothing. Nothing about the pages
# is the user's to set: what a page is and where its range runs are read off the video (WHAT A PAGE IS).
#   --fit W:H   THE VIEWER'S BOX in device px (default PAGE_FIT, the tablet's): each page is scaled, down or up, to
#               fit inside it keeping the video's aspect (fit_size), always exactly the viewer's fit size: 1975x1111
#               from any 16:9 video at the default; a PDF's slice is the box's height and its page's width at THE
#               DOCUMENT SCALE. The viewer prints its box at its open, `warptempo_gui: score page box WxH` (the
#               laptop's terminal, the tablet's logcat warptempo:I): that pair, as W:H, pre-sizes the pages for
#               another screen or gui_scale.
#   --rate R    the unit of pages.txt's frames (default the video's audio sample_rate; required when the video has no
#               audio stream, which then writes no audio.wav and removes a previous one). REFUSED WITH A PDF: a PDF's
#               frames are its pages, at rate 1.
#   --url LINK  recorded in the header (one token, no whitespace). Nothing reads it.
#
# WHAT A PAGE IS, AND WHICH FRAMES ARE ITS (architect 2026-10-26 evening, the fill rule confirmed that night). The scan
# (scan_series, detect) cuts the video into static runs and the transitions between them. A LIT STATIC RUN IS A PAGE
# IFF IT CARRIES BAR LINES (ridge_mask and best_shear on its midpoint frame, at least MIN_BARLINES): a cover, a title
# card or a white flash carries none and is reported, never numbered, wherever in the video it stands; a black run is
# never tested. Length is no discriminator: the Jupiter rip's 1.8 s volta page carries bar lines of its own.
# Consecutive pages whose bar lines match (same_page) are ONE page, every frame between them included: a fade's brief
# settle splits a page's run without changing the page. THE RANGES TILE THE VIDEO (tile_gap): page 1 begins at frame
# 0, the last page ends at the video's end (the scan's frame count), and page i's end is page i+1's begin. Between two
# pages' runs the boundary is THE FIRST FRAME THAT SHOWS THE NEXT PAGE (shows >= SAME_MIN) AT LEAST AS FULLY AS IT
# SHOWS THE PREVIOUS ONE: every frame before it stays with the previous page — its fade-out while its bar lines still
# match, then the FILL that shows no page (black, a cover, a title card, the deep end of a fade), the reader's own
# rule that a page stays until the next is shown — and the frames before the first page's run are the first page's
# (a cover is backfilled by page 1). So a fade counts as its unfaded page's wherever its bar lines match, a fade-in's
# frames go to the page after it from the frame its bar lines appear, and a crossfade turns at the first frame whose
# bar lines show the next page whole while the previous page's show no more fully.
#
# THE GRAMMAR OF pages.txt, spelled here once (the product's loader cites this block). LF line ends, ASCII. First the
# header, every line opening with '#', in this order:
#
#     # src <the video's basename>
#     # url <link>                         (only with --url)
#     # rate <R>                           (a positive integer: the frames' unit, in frames per second)
#     # generated by tools/score_extract.py; frames at `rate`, begin inclusive, end exclusive
#
# then one line per page, at least one:
#
#     <page>|<begin>|<end>
#
# <page> is the page's number, 1, 2, 3, ... with no gap, in line order, equal to its file's number (page<NNN>.jpg in
# the same folder). <begin> and <end> are non-negative decimal integers, audio frames at <rate> measured from the
# video's start (round(seconds * rate)), begin inclusive and end exclusive, begin < end; the ranges ascend and never
# overlap (a page's begin >= the previous page's end). The ranges tile the video: the first begins at 0, each begins
# where the previous ends, and the last ends at the video's end, so every frame is inside exactly one range; a
# transition's or a non-page's frames belong to the page WHAT A PAGE IS gives them. The frames are a position's unit
# and the pages' order: nothing aligns them to any recording but the video's own soundtrack, audio.wav, whose frames
# they are when <rate> is the stream's own (the default); the product plays audio.wav only at pages.txt's rate. FROM A
# PDF (THE PDF ROAD) the header is the same with the PDF's basename and `# rate 1`, and a frame IS a page: page i's
# line is `i|i-1|i`, unit ranges tiling 0 .. the page count, and no audio.wav stands beside them.
#
# THE TWO INSTANCES (the only ones; measured 2026-10-26).
# - THE K.550 RIP, youtube BfcXoB9y4rc, "2017-09-09 Symphony No. 40 in G minor, K.550 (Mozart) - Sheet Music
#   [i:BfcXoB9y4rc].webm": 3840x2160 VP9 at 30/1 fps (a keyframe every 5.333 s), opus 48000 Hz stereo, 1381.068 s,
#   408 MB, the whole symphony in one file. The scan finds 141 transitions and every one is a ONE-FRAME CUT at the
#   source's 30 fps (a frame-to-frame threshold as low as 0.5 finds the same 141); there is no fade, no crossfade, no
#   black frame and no cover (the darkest page's mean is 215.2). Its one non-page is a 0.2 s white flash at 13:11.700
#   (mean 254.0, no bar lines), the 6-frame fill of page 071 before it. Its run: 141 pages, page 001 the
#   first page of music (Molto Allegro, bars 1-5) from frame 0, page 045 the Andante's first at 6:00.333, page 141 the
#   finale's last (bars 303-308) to the video's end, 1381.033 s (the soundtrack runs 8 ms longer); bar lines 4 to 13 a
#   page; audio.wav 265 MB. Its pages at the default fit are 1975x1111.
# - THE JUPITER RIP, youtube gAmw8ATln68, "Wolfgang Amadeus Mozart: Symphony No. 41 in C major "Jupiter" K. 551
#   (1788) [gAmw8ATln68].webm": 1920x1080 VP9 at 25/1 fps, opus 48000 Hz stereo, 2384.4 s, 183 MB, the score
#   pillarboxed (217 px of black either side). The scan finds 199 cuts, one crossfade (one frame, 38:34.200), and
#   eight fades: from black to a cover (the recording's CD cover, a dark photograph, 0:02.320..0:08.120, mean 20.6, with
#   a 0.4 s settle in its fade-in) and back to black, a fade-in to the first page of music, two dips through black
#   (11:44 and 28:04), and the fade to black after the last page. Its non-pages are the cover and that settle, no bar
#   lines either; the two 0.08 s lit runs at 11:42.800 and 11:47.600 (means 211.5 and 202.8) are settles inside the
#   dip's fades and carry their pages' lines whole, so they merge into pages 053 and 054. The bar lines hold deep into
#   every fade: the last frame that still shows the page, or the first, sits at a mean of 8.2 for page 001's fade-in
#   (of its 174.5), 5.8 and 5.6 at 11:44 (of 213.3 and 204.7), 5.9 and 5.8 at 28:04 (of 212.1 and 213.1) and 4.4 for
#   page 200's fade-out (of 207.2), 2-5 % of the page; the crossfade's one frame already shows page 195 whole and
#   page 194 not at all, so it is 195's. Its run: 200 pages, page 001 the first page of music from frame 0
#   (the cover and both its fades its fill), page 173 the 1.8 s volta page (35:38.440), page 200 the finale's last to
#   the video's end, 2384.360 s (the soundtrack runs 16 ms longer); bar lines 4 to 16 a page; audio.wav 458 MB.
#   Its pages at the default fit are 1975x1111, enlarged about 3 %.
#
# THE MEASURED COSTS on this laptop, 2026-10-26 night, the grabs then PNG at 2304 px wide. The Jupiter rip: 2 min 54 s wall from nothing, of which the
# 1080p scan is about 1 min 27 s; 1 min 28 s on its cached scan (the page test's 204 midpoint decodes with their
# shear searches, the five gaps' 540 frames, the 200 page grabs and 4 s of soundtrack). K.550: 1 min 41 s on its
# cached scan (142 midpoint decodes at 4K, the flash's 6 frames, the 141 grabs at about 0.29 s a page at
# GRAB_WORKERS, the soundtrack); its 4K scan adds about 2 min 20 s (the decode alone is 5.2 s per 60 s of video, on
# six cores). A bar-line reading costs about 0.2 s a frame with its shear search and 0.06 s at a page's known shear.
#
# THE PDF ROAD (architect 2026-10-27, after a prototype on IMSLP #00072, K.550; THE EXTENT, THE STAFF TEST and THE
# AVERAGE ROWS the same day: "I should not need to input any information for the tool. ok if certain pages like recits
# and misc are spliced and show occasionally"). A PDF of a printed score is cut into SLICES, one JPEG each, the viewer's
# pages: pages.txt numbers the slices 1 .. N at `# rate 1` (THE GRAMMAR OF pages.txt), no audio.wav is written and a
# previous one is removed (the silent video's rule), and no scan cache is kept. The viewer shows such a folder on its
# page road (src/gui/score_viewer.h's head, "TWO FACES OF THE CONTROLS ROW"). Every pixel constant of the video road
# keeps its meaning: the analysis render is THE PAGE AT CANON_H ROWS, its width to the page's aspect (canonical_size),
# and every length below is in those CANONICAL ROWS.
# - THE BLOCKS AND THE LEFT LINE (systems_of): ridge_mask, the page's best_shear (a scan sits up to a degree off-axis),
#   and the healed vertical runs at that shear (close_vertical); a row is COVERED when some column's run of at least
#   MIN_BARLINE_RUN (a bar line's, the video road's floor) passes through it (covered_rows), and a maximal run of
#   covered rows is a BLOCK. An edition whose bar lines stop between instrument groups (the AMA's K.299 and Requiem,
#   Breitkopf's K.466 and K.620) covers each group alone, so A PAGE'S BLOCKS ARE JOINED THROUGH EVERY GAP ITS LEFT LINE
#   CROSSES: the LEFT LINE is the page's leftmost bar line (left_line), the system's opening line that runs through all
#   its groups, and it crosses a gap when some column of it, widened RIDGE_SIDE either side, is INK in every row of the
#   gap and has paper to its left in at least one — ink a pixel darker than the midpoint of the page's ink and paper
#   levels (ink_and_paper), that mask sheared and healed as the bar lines are (close_vertical heals a tear's 3-row break
#   in K.299 page 51's line). The line is ink but no ridge there: a bracket's or a brace's end beside it darkens the
#   ridge test's side, so its ridge runs stop at the groups' ends, under the run floor. The paper-to-its-left clause is
#   the scan's dark border: on 9 pages of the Cosi (59, 103, 119, ...) the leftmost bar line stands within RIDGE_SIDE of
#   the page's black edge, which runs through every row of the gap between two systems. Measured 2026-10-27 over the
#   eleven PDFs' 1576 gaps: crossed, K.299's 57 of 108 (all its within-system gaps, 10 to 106 rows, a harp's bass staff
#   inside some, the line within a few columns of the leftmost bar line), the Requiem's 10 of 60, K.466's 10 of 69,
#   K.620's 49 of 249, Figaro's 14 of 249, the Cosi's 14 of 296, Don Giovanni's 3 of 319, and none of K.550's 49,
#   K.388's 41, K.525's 78 or K.622's 58; every crossed gap looked at lies inside one printed system, or inside a
#   non-music page. NO BLANK-ROW TEST ("one system unless some row of the gap is blank"): ink stands in every row of
#   many gaps between two printed systems — a dynamic meeting the next system's high note (K.388 pages 4 and 7, 2 dark
#   pixels in the emptiest row), the Requiem's figured bass and tempo words (17 gaps, up to 42 pixels on page 62) — and
#   a merged pair, twice a system's height, would shrink the whole document's scale; the left line never crosses those.
# - THE EXTENT: A SYSTEM'S EXTENT IS WHAT ITS OPENING LINE SPANS (opening_extent, 2026-10-27). A system's OPENING LINE
#   is its leftmost column that is ink in every one of its rows (and the columns within MERGE_GAP of it that are too: a
#   double line, a bracket's stroke beside it), on the ink mask sheared and healed as the bar lines are, without THE
#   BORDER (unbordered: the ink running in unbroken from either edge of the page along its row, the scan's dark edge or
#   the scanner's bed beyond a torn page — the right edge too: K.299 PDF page 52's bed stands at its right edge in every
#   row and joined its two systems into one of 974 rows) and healed again (the Cosi's page 23: its dark edge touches the
#   line in single rows and takes them with the border). Every healed run of the line meeting the system's rows joins
#   it, and systems that then meet are one. So a group barred apart from the long bar lines but spanned by the opening
#   line is inside its system: K.550 PDF page 46's first system takes its bass staff, its bottom moving from row 461 to
#   520. Elsewhere on K.550 the line's ink stands a row or two past its ridge run's ends: 26 of its 98 systems' tops
#   move up 1 or 2 rows (those 26 slices change by those rows), 69 bottoms down 1 to 8, the scale and pages.txt not at
#   all. The line's own columns, never the ridge's side: widened RIDGE_SIDE either side, the line took a bracket's curl
#   and moved K.550's systems 3 to 7 rows, its tallest from 451 to 458. Where the opening line itself breaks between
#   groups the group stays out of the system, and THE RISE (THE WINDOW) or THE AVERAGE ROWS show it.
# - THE STAFF TEST (staves_of, 2026-10-27): A SYSTEM IS SURE WHEN IT CARRIES A STAFF, five equally spaced long
#   horizontal lines, its middle row inside the system's rows. A block of bar lines with no staff (a catalog's column
#   rules, a title page's frame, a cover's weave, a library card) is no system and never sets the scale, and A PDF PAGE
#   WITH NO STAFF IS NOT A PAGE: dropped, one report line ("not a page: PDF page 3, no staff"), never numbered ("a whole
#   page will show up as a break in the pattern when flipping — drop"). A LINE is a run of rows whose staff-line ridge
#   (staff_ridges: ridge_mask turned a quarter) covers STAFF_SHARE of the page's central half, at the rows' own shear
#   (searched apart from the bar lines': a photographed page's staff lines and bar lines lean apart — the Zauberfloete's
#   PDF page 223 reads its bar lines at -0.75 degrees and its staff lines at 0, K.388's page 10 its bar lines at 0 and
#   its staff lines at -0.8, where unstraightened rows give 5 of its 21 staves). A STAFF is five consecutive lines whose
#   spacings lie within STAFF_SPACING and agree within STAFF_TOL, standing alone (STAFF_ALONE). Measured over the eleven
#   PDFs at CANON_H: 32883 staves, their spacing 3.90 rows (Don Giovanni) to 5.87 (Figaro), the medians 4.31 (the
#   Requiem) to 5.63 (the Eulenburg K.525); the densest pages, the Cosi's PDF page 167 and the Zauberfloete's 223 (17
#   staves each), read 17 of 17, so the test runs on the analysis render and no taller one.
# - THE DOCUMENT SCALE, ONE FOR THE WHOLE PDF ("we want the pages to look more or less similar when flipped — we don't
#   want a page with large notes followed by one with smaller notes"), in device px per PDF point: the box's height over
#   THE TALLEST SURE SYSTEM of the document plus PAD above and below, or the box's width over a page's width where that
#   is tighter, the smaller of all of them. A page's canonical row is its height / CANON_H in points, so pages of
#   different sizes (the Internet Archive's scans are cropped page by page) are measured in one unit: every staff of the
#   document is then the edition's own rastral at one size on the glass, and a 20-stave tutti, taller in print too, sets
#   the scale honestly. Pages of different sizes are reported in one advisory line and each slice is its own page's
#   width at the one scale; none is refused.
# - THE WINDOW (pdf_windows), over a page's sure systems: every slice is the box's height (box_h device px) and its
#   page's FULL WIDTH at the document scale (no horizontal trim: the viewer centers the slice). It is TOP-ANCHORED: its
#   top stands PAD rows over a system's top, so the first staff of every slice lands at one height on the glass and the
#   continuation is always the first staff. It holds that system and as many following ones as fit whole (each one's
#   bottom plus PAD inside the window), greedy and consecutive; the next slice starts at the first system that did not
#   fit. It shows whatever the page has there: the gap, and the top of the next system cut by the box's edge (a preview
#   at the bottom, never a repeat at the top). The cost, accepted: the running header and the printed page number stand
#   above the pad and never show, and a first page's title block shows only its bottom. THE RISE (rise_window,
#   2026-10-27): a window RISES over the staff standing nearest above its first system when the gap between them is no
#   wider than the widest gap between two consecutive staves of its own systems and its last system still fits, its top
#   then PAD over that staff, and on over the next staff up by the same test; the first staff still lands at one height.
#   It shows the group an edition bars apart and its opening line leaves out (the Zauberfloete's first flute over its
#   full-page systems, the Cosi's top staves, K.299's flute and harp over the strings), which the average rows otherwise
#   show as a second slice repeating part of the system, or miss. Measured over the eleven PDFs: 168 windows rise, over
#   805 staves; the staves in no window fall from 82 to 34, the average windows on sure pages from 100 to 53, the slices
#   from 2499 to 2452, and no scale changes (the window rises only where its system still fits). The average rows are
#   taken from the windows before they rise.
# - THE AVERAGE ROWS (plan_pdf, snap_cut, 2026-10-27): the canonical tops of the windows of the sure pages holding the
#   modal count of windows, averaged per slice index (K.550's 71 and 542; an opera's one window a page, at 68 to 99). A
#   PAGE WITH STAVES AND NO SURE SYSTEM (a secco recitative's page, whose two-stave systems' bar lines stand under
#   MIN_BARLINE_RUN; a layout the rule cannot read) is shown by the average rows, and a page whose sure windows leave a
#   staff unshown takes the average windows that show it: an average window is kept where it shows whole a staff no
#   earlier window shows. Each one's top is SNAPPED to a blank row of the page (no ink across the staff lines' columns,
#   THE BORDER aside) within SNAP_TOL rows, so a short system is not cut through, THE NEAREST AT OR ABOVE IT FIRST and
#   the nearest below only where none stands above (2026-10-27): a snap down steps over whatever stands between, and no
#   window may show it then, while a snap up only shows more. The window is the document's window, the paper filling
#   past the page's edge. A staff no window shows whole is reported on the page's last slice ("1 staff in no window").
# - THE FILL (edge_tones, cut_slice): where the window runs past the page's edge (a short last system, a thin margin)
#   the rows are THE PAGE'S OWN PAPER TONE, the median gray of its edge rows on the slice's own render (the page's first
#   PAD rows above it, its last PAD below; the page's median where an edge band reads as ink; FILL_PCT's block;
#   2026-10-27), NEVER BLACK: a black block would stand as a rectangle on the box's ground, while the margin's own paper
#   reads as the page running on.
# - THE SLICE FILE is exactly (round(page width x scale), box_h) device px — the viewer's identity blit (fit_size's
#   rule: a slice IS the fit size on the height) — gray JPEG at PAGE_JPEG_QUALITY, optimized.
# - TWO RENDERS PER PAGE (render_pdf_page), the analysis one for every page first (the scale needs the tallest sure
#   system and the average rows every sure page's windows before any slice is cut): poppler's pdftoppm renders the page
#   gray at PDF_SUPERSAMPLE times the size wanted and Pillow box-averages it down, so both the analysis render and the
#   slice's are area-scaled as the video road's frames are.
#
# THE PDF ROAD'S INSTANCES (measured 2026-10-27 at the default box, PAD 26, the staff test, the extent, the rise and the
# average rows; "rows" are canonical, "systems" the rule's sure ones, "slices" the pages written; "sure", "average" and
# "dropped" count the PDF pages by outcome). K.550 is the one the rule was ruled on; the ten others are the test set of
# tmp/research/mozart_pdfs/, and where their rule's systems are not the printed ones is listed below.
# - K.550, IMSLP #00072 (Breitkopf, 1-bit CCITT at 320 dpi, 595 x 792 pt, every page): 49 pages, all sure, 2 systems
#   each, the tallest 451 rows (PDF page 7); scale 3.012 px/pt (216.9 dpi); slices 1792 x 1111, 98 of them, one system
#   each; 13.8 s.
# - K.388, AMA (CCITT 600 dpi, 708.48 x 926.88 pt): 22 pages, all sure, 2-3 systems; tallest 345 (PDF page 1); the box's
#   WIDTH bounds the scale, 3.190 px/pt (229.7 dpi), slices 2260 x 1111, 65; 11.2 s.
# - K.626 Requiem, AMA (CCITT 600 dpi): 100 pages, 95 sure, 5 dropped (1-4, 100: the title pages, the publisher's
#   subscription notice), 1-2 systems; tallest 868 (PDF page 23, a full-page system); 1.407 px/pt (101.3 dpi), slices
#   997 x 1111, 142; 23.4 s.
# - K.299, AMA (JPEG 2000, 4 page sizes): 52 pages, all sure, 1-2 systems; tallest 453 (PDF page 22); 2.444 px/pt (176.0
#   dpi), slices 1655..1913 x 1111, 107; 72.1 s.
# - K.466, Breitkopf Partiturbibliothek (JPEG 2000, 18 sizes): 72 pages, 56 sure, 16 dropped (1-8, 65-72: the cloth
#   covers, the scanner's blanks, the publisher's catalogs, the library card), 1-3 systems; tallest 542 (PDF page 41);
#   2.482 px/pt (178.7 dpi), slices 1491..1536 x 1111, 128; 66.4 s.
# - K.622, Breitkopf (JPEG 2000, 8 sizes): 72 pages, 58 sure, 14 dropped (1-8, 67-72, as K.466's), 2 systems each;
#   tallest 464 (PDF page 42); 2.846 px/pt (204.9 dpi), slices 1780..1812 x 1111, 116; 78.2 s.
# - K.525, Eulenburg miniature No. 218 (JPEG 2000, 18 sizes): 44 pages, 24 sure, 20 dropped (1-10, 35-44: covers,
#   blanks, title pages, the contents, the preface, the catalogs), 3-4 systems; tallest 222 (PDF page 11); the box's
#   width bounds the scale, 4.838 px/pt (348.4 dpi), slices 2174..2260 x 1111, 95, one system each; 50.8 s.
# - K.620 Zauberfloete, Breitkopf (JPEG 2000, 4 sizes): 240 pages, 223 sure, 1 average (97), 16 dropped (1-10, 235-240),
#   1-4 systems; tallest 938 (PDF page 223, a full-page system, the Queen, her three ladies and Monostatos over the
#   orchestra); 1.397 px/pt (100.6 dpi), slices 886..898 x 1111, 319; 193.9 s.
# - K.588 Cosi, Breitkopf 1810 (JPEG 2000, 17 sizes): 296 pages, 259 sure, 7 average (75, 116, 173, 191, 213, 229, 236:
#   recitatives), 30 dropped (1-18, 170-172, 288-296: covers, title pages, the libretto, blanks), 1-5 systems; tallest
#   1080 (PDF page 40, two systems merged, below; the music's tallest 1021, page 167); 0.999 px/pt (71.9 dpi), slices
#   775..798 x 1111, 277; 239.3 s.
# - K.527 Don Giovanni, Leuckart 1868 (JPEG 2000, 4 sizes): 512 pages, 438 sure, 28 average (recitatives, and two music
#   pages of the preface, 17 and 24, the overture's changed ending among them), 46 dropped (1-16, 18-23, 25-28: covers,
#   title pages, the preface; 257-258, the second act's title; 495-512, blanks), 1-4 systems; tallest 910 (PDF page 215,
#   a full-page system); 1.448 px/pt (104.3 dpi), slices 901 x 1111, 547; 399.5 s.
# - K.492 Figaro, Paris 1808 (JPEG 2000, 5 sizes): 578 pages, 504 sure, 52 average (recitatives, between PDF pages 46
#   and 494), 22 dropped (1-11, 574-578: covers, the title, the contents, the cast; 140, 327-328, 445-447: blank or
#   show-through leaves), 1-3 systems; tallest 924 (PDF page 99); 1.194 px/pt (86.0 dpi), slices 880..906 x 1111, 558;
#   476.7 s.
# Every dropped page, 169 of the 2037, was looked at: none holds music; and every kept page beside a dropped one, and
# every average page looked at, is music. WHERE THE RULE'S SYSTEMS ARE NOT THE PRINTED ONES, seen on the slices and the
# analysis renders:
# - A GROUP OVER ITS SYSTEM'S FIRST BLOCK, BEYOND THE OPENING LINE: where the opening line breaks between groups and the
#   upper group carries no long bar line (the AMA's K.299, its flute and harp over the strings; the Zauberfloete's first
#   flute staff over its full-page systems; the Cosi's top staves; Breitkopf's K.466) the sure window would start PAD
#   over the strings. THE RISE shows the group with its system where its gap is a system's own (K.299 pages 24 and 34,
#   the Zauberfloete's 13, the Cosi's 23, K.466's 26, all whole). Where the gap is wider than any inside the system the
#   window does not rise and the average rows show the group, a second slice repeating part of its system (K.299 pages
#   27 and 31: the system's flute and harp with its strings, then the strings again), or nothing shows it where it
#   stands over the average top too: 34 staves on 30 pages in no window (the Zauberfloete's 10 on 9 pages, 46, 69, 89,
#   92, 99, 120, 190, 200, 220; the Cosi's 20 on 17; Don Giovanni's 2, pages 17 and 401; K.466's 1, page 61; the
#   Requiem's 1, page 57), most of them a page's first staff.
# - A PAGE MOSTLY UNREAD: where one system of a page is sure and the others' bar lines are all short (Don Giovanni page
#   401, an aria's four five-stave systems, its voice barred apart; K.466 pages 22 and 30) the average rows and the sure
#   window overlap, and a slice can open inside a system.
# - THE AVERAGE ROWS' CUTS: of the 141 average windows (88 on the average pages, 53 on sure pages), 13 open through a
#   staff, no blank row within SNAP_TOL (the Cosi's 5, pages 77, 191, 229, 245 and 259; K.466's 4, pages 23, 30 twice
#   and 61; the Requiem's 2, pages 57 and 63; the Zauberfloete's 200; K.299's page 1); 6 snap down, no blank row within
#   SNAP_TOL above them, and 3 of those leave a staff in no window (the Cosi's 75, Don Giovanni's 17 and 401).
# - MERGED: a page whose opening line runs on through the gap between its systems (the Cosi's page 40, its line reaching
#   both page edges) is one system, here the whole page, 1080 rows: it sets the Cosi's scale at 0.999 px/pt where its
#   music's own tallest would give 1.050.
# - THE RECITATIVES: a secco recitative's two-staff system is 60-80 rows (its bar lines' runs 40-80), under
#   MIN_BARLINE_RUN, so a page of recitative has no sure system and is shown by the average rows (the operas' one window
#   a page, from row 68 to 99 over nearly the whole page; Figaro's 52 such pages and Don Giovanni's 28 lose no staff),
#   and a page mixing recitative with a sure system takes average windows for the staves its window leaves.
#
# THE PDF ROAD'S COSTS on this laptop (2026-10-27; the measuring shell's nproc 2, GRAB_WORKERS 4): the analysis of a
# page about 0.2 s of compute (its bar lines, the extent and the ink 0.15 s, the staff test 0.06 s); a 1-bit CCITT page
# about 0.25 s in all (K.550's 49 pages in 13.8 s, the Requiem's 100 in 23.4 s); a JPEG 2000 page of the Internet
# Archive about 0.8 s (Figaro's 578 in 476.7 s, Don Giovanni's 512 in 399.5 s), the decode dominating, twice per page
# (the analysis render, then the slice's), and the slice's render the larger the larger the scale (K.525's 44 pages at
# 348 dpi in 50.8 s).
import math
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from fractions import Fraction

import numpy as np
from PIL import Image

# MOTION, in two thresholds on the 0-255 scale over the DIFF_W x DIFF_H thumbnail, both mean absolute differences:
# A FRAME STARTS MOTION when it differs from the FIRST FRAME OF THE CURRENT STATIC RUN by more than DRIFT_THR, and
# MOTION GOES ON through every frame that differs from ITS PREDECESSOR by more than SETTLE_THR. Measuring against the
# run's first frame rather than the previous one is what catches a slow transition: a 0.5 s crossfade between two
# pages at 30 fps moves each frame by about 2.5 and a 2 s fade by under 4, so a frame-to-frame threshold anywhere near
# a cut's size (the sunset tool's 5.0) sees neither, while the drift from the page passes DRIFT_THR within a frame or
# two whatever the speed. The settle threshold keeps a slow transition one event instead of a chain of one-frame
# static runs. Measured on the K.550 rip at 30 fps (VP9's own noise on a held page): a static page differs from its
# predecessor by at most 0.158 and from its run's first frame by at most 0.194, over runs up to 27 s; the smallest cut
# is 23.9. So DRIFT_THR = 1.0 sits 5x above the drift noise and 24x under the smallest cut, and a page keeps a
# crossfade's or a fade's frames only while they are within 1.0 of it (a blend under 2.5 % of a typical page-to-page
# difference of 40, the first 0.5 % of a fade from a 215 page); SETTLE_THR = 0.5 sits 3x above the frame-to-frame
# noise and holds in motion a crossfade of 40 lasting up to 80 frames (2.7 s), a fade from 215 to black up to 14 s.
# Proven on a synthetic 30 fps video (2026-10-26) carrying a cut, a 0.5 s crossfade, a 1.5 s fade-out, a 2 s black
# gap, a 1 s fade-in, a 1 s dip through black, a 0.2 s white flash and a 3 s fade-out: every range came out exact to
# the frame.
DRIFT_THR = 1.0
SETTLE_THR = 0.5
# THE BLACK CEILING: a static run whose mean luminance stays under this is BLACK, the gap after a fade-out, never a
# page and never tested for one. Black is about 0-3; a printed page is about 150-230 (the K.550 rip's darkest is
# 215.2). 16 is 5x above black's top and 13x under that page, and still under any score page's ground. Black is the
# report's word (and the transitions' classes'); no range depends on it.
FADE_FLOOR = 16.0
# THE SCAN'S THUMBNAIL, the size the motion thresholds were measured at. `flags=area` is the box average a downscale
# this deep wants, and it is the cheap scaler: bicubic adds 2.3 s per 60 s of 4K video at 30 fps, area 0.7 s.
DIFF_W, DIFF_H = 480, 270
# THE DEFAULT FIT BOX (--fit's default): THE TABLET'S SCORE-PAGE BOX, the score viewer's fit box
# modal_box_interior_rect(app, 0) (src/gui/modal_box.h) on the Galaxy Tab S10 FE's 2304 x 1440 at gui_scale 300,
# derived from the program's layout (2026-10-26) and printed by the viewer itself at its open (score_viewer.cpp's
# feed_decoder), the line the pair is checked against:
#   ACROSS (re-derived 2026-10-28, the canvas's frame column one hairline): the canvas interior (waveform_area,
#   src/gui/main.cpp) stands column_inner_x_px in from each side, the column margin's 6 W plus the frame column's
#   one device px, 18 + 1 = 19 px; the room 2304 - 38 = 2266 floors to the 16-px grid step, 2256, its slack of 10
#   split, so x = 24; the ring's outer columns (program_ring_columns) one frame column outside it, 23 and 2281, the
#   box's x and width 2258; the interior one hairline_px (1 at every scale) in from each side: x 24, WIDTH 2256 —
#   the canvas's own columns.
#   DOWN: the box runs from the view bar's top row (top_trim_row_area) to the controls row's top
#   (modal_controls_row_rect, the bottom row's lane). The band above the view bar is the logo lane's 72, the case's
#   69 (1 + 20 + 1 + 1 W) and the strip pad's 39 (13 W), 180 rows, the flexible gap 0 (the waveform takes the
#   whole leftover); the bottom row is the strip, 39 + 69 + 39 = 147 rows on the window's foot from row
#   1440 - 147 = 1293. So the box is rows 180..1292, 1113 rows, and its interior one hairline in: y 181, HEIGHT 1111.
PAGE_FIT = (2256, 1111)
# THE PAGE FILE: GRAYSCALE JPEG at this quality, Huffman-optimized (architect 2026-10-26: "grayscale ok", quality 92).
# A score page is black ink on paper, so one channel loses nothing a reader sees; ffmpeg's mjpeg encoder writes no
# gray JPEG (its pixel formats are yuvj420p, 422p and 444p alone), so the grab pipes ffmpeg's gray rawvideo to Pillow,
# which writes the one-channel file (grab_page). The viewer decodes it through resvg's JPEG road (zune-jpeg, its
# grayscale expanded to RGBA).
PAGE_JPEG_QUALITY = 92
# THE PAGE GRABS are one ffmpeg per page seeking to the page's midpoint frame, GRAB_WORKERS at a time. Measured on a
# 2-minute slice of the K.550 rip (15 pages): one decode pass selecting every midpoint took 9.56 s (the whole video
# would take about 110 s), per-page seeks 0.58 s a page one at a time and 0.34 s at 4 at a time (about 48 s for the
# whole video): each seek decodes from the keyframe before it, at most 5.3 s of video here, far less than the pass.
# The analysis decodes (the page test's midpoints, the gaps) run GRAB_WORKERS at a time too.
GRAB_WORKERS = 4
# THE SCAN CACHE lives beside the outputs: the three series per scan frame (SCAN_NPY, a 3 x n float64 array, the rows
# SERIES_ROWS) and the stamp of the input it was taken from (SCAN_META). The stamp is a RESOLVED IDENTITY plus a CHANGE
# WITNESS — the realpath, device, inode, size and mtime — THE EXACT FILTER STRING the scan handed ffmpeg and the two
# motion thresholds (the drift row's anchors depend on them), so a different video, the same path re-encoded in
# place, or a changed scan compares unequal and rescans. Deliberately NOT a content digest: a 400 MB rip would cost
# more to digest than to decode. A scan that did not complete acquires no cache. THE GRAB'S FIT IS NOT IN IT: the
# pages are grabbed on every run, so a rerun at another --fit reuses the cached scan and regrabs every page at the new
# size.
SCAN_NPY = "scan.npy"
SCAN_META = "scan.meta"
# The diff to the previous frame (0 for the first), the mean luminance, the diff to the current static run's first
# frame (motion_at's anchor; the diff to the previous frame while in motion).
SERIES_ROWS = ("diff", "mean", "drift")
PAGES_TXT = "pages.txt"
AUDIO_WAV = "audio.wav"
# THE PAGE TEST AND THE SIGNATURE: BAR LINES, read off a gray frame at THE CANONICAL ANALYSIS HEIGHT, CANON_H lines
# (the video scaled with flags=area, its width kept to its aspect, by canonical_size), so every pixel constant below
# has one meaning on every rip. The geometry is the sunset sheet-map extractor's (git a6ccf9a3^, tools/
# extract_sheet_map.py), which COUNTED bar lines well enough to validate K.550's four movements against kern ground
# truth; its fixed BINARIZE = 170 and its scipy deskew are replaced here so the test holds on what a scanned score
# carries (architect 2026-10-26 night: "bulletproof as far as can be: scanned pages, pages slightly off-axis").
CANON_H = 1080
# A BAR LINE IS A THIN DARK RIDGE (ridge_mask): a pixel is a ridge pixel when it is darker than BOTH of the pixels
# RIDGE_SIDE px to its left and its right by RIDGE_DEPTH of the frame's own ink-to-paper contrast, taken as a share
# of that local paper (the darker of the two sides). So the test is local and relative: a grey or uneven ground, a
# lowered contrast and a fade toward black (the page scaled down whole) keep their bar lines, while a fixed threshold
# loses the lines in a shadow or sees the whole frame as ink once the paper sinks under it; and a long dark mass is
# never a line: the Jupiter rip's pillarbox (217 px of black either side of the page) and its cover (a dark
# photograph, mean 20) carry none. The contrast is the frame's PAPER_PCT percentile against its INK_PCT percentile,
# both over the frame's central half (a pillarbox or a letterbox would make its black the ink level). A side at
# RIDGE_SIDE = 5 px clears a bar line's own width (2-3 columns at CANON_H, 4-5 under a one-px blur); RIDGE_DEPTH = 1/3
# is the sunset tool's 170 of 255 on a clean page, and passes a 2:1 page (paper 180, ink 90) at a sixth of the local
# paper, while a bleed-through at a quarter of the ink stays out. MIN_DROP gray levels at least, so the quantization
# noise of a near-black frame is no ridge; and the ground either side is paper, at least MIN_GROUND of the frame's
# paper level: the Jupiter rip's black pillarbox carries the encoder's faint full-height seams (a 2-level drop, a
# 1000 px run at x = 1788 and 1907 on two pages' edge frames), while an uneven ground keeps 0.6 of its paper.
INK_PCT, PAPER_PCT = 1, 90
RIDGE_SIDE = 5
RIDGE_DEPTH = 1 / 3
MIN_DROP = 2
MIN_GROUND = 1 / 2
# THE VERTICAL SMOOTHING before the ridge test: each pixel is the mean of SMOOTH_ROWS rows of its column. A bar line
# is vertical and keeps its depth; pixel noise falls by the square root of SMOOTH_ROWS, which keeps a noisy frame's
# speckle (the cover under Gaussian noise of sigma 20 made 3 one-column "lines" without it) from chaining into runs.
SMOOTH_ROWS = 5
# THE SHEAR (best_shear): a page off-axis by a degree turns a 700 px bar line into 12 columns of short runs, none
# reaching the run floor. Each row is shifted by its distance from the frame's middle row times tan(angle) — a shear
# that straightens a small rotation well enough for column runs, and leaves a line's center where the middle row
# crosses it — at SHEAR_COARSE_DEG, then at SHEAR_FINE_DEG either side of the best, keeping the one whose runs over
# the floor are longest (the sum of their squares). A page's shear is found once, at its run's midpoint; the frames of
# its fades are read at that shear (tile_gap's caller).
SHEAR_COARSE_DEG = (-1.0, -0.5, 0.0, 0.5, 1.0)
SHEAR_FINE_DEG = 0.25
# The run geometry, the sunset tool's at CANON_H: a vertical closing of CLOSE_GAP px heals the breaks (the staff
# lines crossing, a broken or scanned line), a column is a bar line's when its longest healed run reaches
# MIN_BARLINE_RUN px, and columns within MERGE_GAP px are one line (a double bar, a repeat sign). A one-column line
# counts (a thin line, or a 2 px line narrowed by a scan): the sunset tool's two-column floor kept stems and slur ends
# out of a COUNT, which the ridge depth and the smoothing now do. Measured runs: K.550's 168-447 px, the Jupiter
# rip's up to 984; notation debris under 100.
CLOSE_GAP = 6
MIN_BARLINE_RUN = 160
MERGE_GAP = 30
# A PAGE CARRIES AT LEAST MIN_BARLINES bar lines: a system carries one at each end at least, while a title card's
# ornament or a photograph's edge could carry one thin vertical rule. Every page of both rips carries 4 or more; the
# cover, the flash and the Jupiter rip's 0.4 s settle in the cover's fade-in carry none.
MIN_BARLINES = 2
# THE SIGNATURE of a frame is its bar lines' x-centers (canonical px, ascending). Two signatures SHARE a line when
# their centers lie within MATCH_TOL px (share); two frames show THE SAME PAGE when the shared lines are at least
# SAME_MIN of the longer signature (same_page), and a frame SHOWS a page to the degree of the page's lines it carries
# (shows: shared / the page's count, so a crossfade frame carrying both pages' lines can show each whole).
#
# THE MARGINS, measured 2026-10-26 on three frames of every static run of both rips (its second, middle and
# second-to-last) and on every page against its neighbors. HELD NOISE (one page across its own run, VP9 noise): a
# line's center moves at most 1 px on the Jupiter rip and 0.5 px on K.550, and every held pair shares 1.0.
# DISTINCT NEIGHBORS: at MATCH_TOL = 3 the closest pair shares 0.40 (K.550 9:51.800 and 10:02.800, 2 of 5 lines), the
# Jupiter rip's 0.25, and the volta page shares 0 and 0.0625 with its two neighbors. Two K.550 neighbors (19:52.767
# and 19:58.200) carry the same seven bars shifted 1-6 px, so the tolerance has a ceiling: they share 0.571 at 5 px,
# 0.714 at 6 (a merge) and 1.0 at 8. So MATCH_TOL = 3 sits 2 px over the held jitter and 3 px under that pair's merge,
# and SAME_MIN = 0.65 sits 0.35 under the held floor and 0.25 over the closest distinct pair.
#
# THE SYNTHETIC PROOF (2026-10-26 night, numpy + Pillow off the tool's path): two Jupiter pages (2:18.5, the volta
# page), two K.550 pages (0:03, 13:20), the cover and a black frame, each put through a rotation of +0.5, +1.0 and
# -1.0 degrees (bilinear, white fill), Gaussian noise of sigma 10 and 20, a one-px box blur, a 2:1 contrast (paper
# 180, ink 90), a grey ground (paper 200), an uneven ground (1.0 falling to 0.6 across the page), a bleed-through (the
# page's mirror at a quarter of its ink), 5-row breaks every 37 rows, every stroke one px thinner (plain and at +0.5
# degrees), and two composites of a scan (rotated 0.7 or -1.0 degrees, ink 70 on paper 200, uneven, blurred, noise of
# sigma 8). Every variant of every page is a page whose signature shares at least 0.86 with its clean original
# (K.550 13:20 at -1.0 degrees, 6 of 7); the other 63 of the 64 share 1.0; the cover and the black frame carry no bar
# line under any.
MATCH_TOL = 3
SAME_MIN = 0.65
# THE DECODE'S ALIGNMENT TRIPWIRE (decode_gray): an analysis frame's mean against the scan's mean of the same frame.
# The two scales' box averages differ by at most 0.84 (the Jupiter rip) and 0.16 (K.550); a frame one index off
# differs by up to 18 in the Jupiter rip's fades, and by nothing on a held page, where it changes nothing.
ALIGN_TOL = 2.0
# THE PDF ROAD's constants (its block in the head). THE INPUT IS A PDF when its first bytes are this magic (is_pdf).
PDF_MAGIC = b"%PDF-"
# THE RENDERS' SUPERSAMPLING (render_pdf_page): pdftoppm renders the page PDF_SUPERSAMPLE times the size wanted on each
# side and Pillow's reduce box-averages it, deterministically. Splash, poppler's rasterizer, all but SAMPLES a 1-bit
# scan on a reduction: K.550's CCITT page 1 (2287 px wide at 320 dpi) rendered straight at about its slice size (1775 x
# 2363) came out 96.4 % pure black or white and 3.1 % one mid gray (127) — a thresholded page, its staff lines of uneven
# weight — while the box average of a k-times render gives a binary picture k * k + 1 coverage levels (5 at 2x, 10 at
# 3x, 17 at 4x; 87.9, 86.4 and 85.7 % of the page then pure paper or solid ink). 3 is the antialiased gray the slice
# wants at about four times the straight render's cost on a 1-bit scan (that page: 0.13 s straight, 0.28 s at 2x, 0.55 s
# at 3x, 0.72 s at 4x); on the Internet Archive's JPEG 2000 scans the decode dominates whatever the size. The analysis
# render takes the same road, so it is area-scaled as the video road's analysis frame is (flags=area), never a sampling
# that could step over a thin bar line of a 600 dpi scan.
PDF_SUPERSAMPLE = 3
# THE PAD, in canonical rows (THE WINDOW): the page above a slice's first system and below its last. Measured on K.550
# (2026-10-27), the tempo words' tops over their system's top staff line: "Allegro molto." (page 1) 14 rows,
# "Andante." (page 19) 15, the Menuetto's "Allegretto." (page 30) 17 and its title "MENUETTO." above it 31, the
# "Trio." (page 32) 17, the Finale's "Allegro assai." (page 33) 25. Its 49 gaps between two systems of a page run 34
# to 61 rows (40 to 48 for most), the system above hanging ink (a dynamic, a low note) 0 to 17 rows into the gap
# but one of 32. So a pad of 26 holds every tempo word whole with a row of paper above the Finale's, is about half a
# gap, and shows the system above in the top pad of 2 of those 49 slices (at 28: 5; at 32: 21); a pad of 24 cuts the
# Finale's word. The Menuetto's title line is cut (a heading, like the operas' scene headings, may be cut).
PAD = 26
# THE FILL TONE (edge_tones, cut_slice; 2026-10-27): the median gray of THE PAGE'S OWN EDGE ROWS, its first PAD
# canonical rows for the fill above the page and its last PAD for the fill below, over its central half, so the fill
# continues the margin it meets. The page's median stood a visible step off it where a scan is lit unevenly: on K.299's
# PDF page 27 (slice 056, its last system's window run past the page's bottom) the page's median is 170 under a bottom
# margin of 181, its edge band 181; K.525's page 13 205 against its bands' 201 and 200; Figaro's page 99 176 against 193
# and 191 (its dark central half); the Cosi's page 23 171 against a top band of 158 (its systems start under a thin top
# margin). AN EDGE BAND THAT READS AS INK (its median under the midpoint of the page's ink and paper levels) is no
# margin: K.299's page 52 ends in the scanner's black bed under a torn edge (median 5), and a fill at it would stand as
# the black block THE FILL forbids, so such an edge takes the page's median over its central half (FILL_PCT), the level
# most of a page's pixels sit at. Not ridge_mask's PAPER_PCT there: on a gray scan the 90th percentile is the paper's
# brightest grain, lighter than the paper the eye averages (it stood as a pale band under K.525's slice 010).
FILL_PCT = 50
# THE STAFF TEST (staves_of; its block in the head), measured 2026-10-27 over the eleven PDFs at CANON_H, where a
# staff's lines stand 3.90 to 5.87 rows apart. A STAFF-LINE RIDGE PIXEL is darker than both pixels STAFF_SIDE rows
# above and below it by STAFF_DEPTH of the page's contrast (ridge_mask's test turned a quarter, MIN_DROP and
# MIN_GROUND as there): a side of 2 lands between two lines at the narrowest spacing, where ridge_mask's RIDGE_SIDE of
# 5 lands on the next line. RIDGE_DEPTH's third finds none of the 17 staves of the Zauberfloete's PDF page 223 (a
# photographed page, its staff lines a pale gray) and 0.15 all 17; the depth admits noise, but a line must cover
# STAFF_SHARE of the central half, which scattered noise never does. A row's cover counts a ridge of the row below
# too: a line straddling two rows splits its pixels between them (K.550's page 1, 0.33 and 0.36 of the half).
STAFF_SIDE = 2
STAFF_DEPTH = 0.15
STAFF_SHARE = 0.4
# A STAFF is five consecutive lines whose four spacings lie within STAFF_SPACING rows and agree within STAFF_TOL of
# their mean (a row at least: a line's center moves by a row's half where the line straddles two), and that stand
# ALONE: the lines before and after them lie more than STAFF_ALONE spacings away. The floor and the ceiling hold the
# staves' 3.90 to 5.87 with room either side, and keep out a cover's cloth weave (a series of lines 2.9 to 3.1 rows
# apart on K.466's PDF pages 1 and 72) and a catalog's dotted leaders (13.1 and 13.2 rows apart on K.525's pages 6 and
# 39, the page whose false staff set the scale at 997 rows under a ceiling of 15); alone keeps out a weave's longer
# series (one at 5.9 rows on K.466's page 1). Alone costs a dense page a staff where a line of its neighbor's text or
# beam stands close (K.550 pages 4, 9, 12, 18, 22, 24 and 27, one or two of their 20 to 22 staves), never a system.
STAFF_SPACING = (3.5, 10.0)
STAFF_TOL = 0.25
STAFF_ALONE = 1.5
# THE ROWS' SHEAR (staves_of) is searched apart from the bar lines' (THE SHEAR): at STAFF_SKEW_COARSE_DEG, a quarter
# degree apart over a degree and a half either side (the kept pages' staff lines lean up to 1.4 degrees, K.525's PDF
# page 24; 1.05 on the Requiem's page 57 and Figaro's page 47), then STAFF_SKEW_FINE_STEPS steps of STAFF_SKEW_FINE_DEG
# either side of the best, the score the sum of the rows' squared cover. Across the central half's 400 columns a
# twentieth of a degree moves a line's ends by a sixth of a row.
STAFF_SKEW_COARSE_DEG = tuple(q / 4 for q in range(-6, 7))
STAFF_SKEW_FINE_DEG = 0.05
STAFF_SKEW_FINE_STEPS = 4
# THE AVERAGE ROWS' SNAP (snap_cut): a cut moves to a blank row within SNAP_TOL rows, PAD's own reach (the average top
# stands PAD above an average system's top, so a blank row within it lies in the gap over that system), the nearest at
# or above it first, the nearest below where none stands above. Measured 2026-10-27, nearest either way: of the 192
# average windows 58 stood on a blank row, 75 snapped 1 to 26 rows (spread evenly), 59 found none (the Cosi's dense
# pages and K.466's, ink in every row) and kept their row; 5 of the snaps went down past a staff no window then showed
# (K.466 pages 26 and 39, the Cosi's 75, Don Giovanni's 17 and 401). Above first, with THE RISE: of the 141 average
# windows 52 stand on a blank row, 51 snap up, 6 snap down (no blank row within SNAP_TOL above them: K.466 pages 31 and
# 39, the Zauberfloete's 97, the Cosi's 75, Don Giovanni's 17 and 401; the last three lose a staff), 32 find none.
SNAP_TOL = 26


class OptionError(Exception):
    """One bad option, carrying the line to print: every option fault comes out as the tool's REFUSE line and exit 1,
    never a traceback."""


class Refusal(Exception):
    """A refusal after the options: one line, exit 1, nothing written."""


# THE OPTION PROTOCOL is a whole-tail parse: every flag with its exact arity, each at most once, a flag name never a
# value; anything unclaimed is named and refused. So a mistyped flag cannot pass silently.
KNOWN_OPTIONS = {
    "--fit":  1,
    "--rate": 1,
    "--url":  1,
}


def parse_argv_tail(tail):
    """`tail` -> {flag: [values]}, or an OptionError naming the first fault."""
    claimed = {}
    i = 0
    while i < len(tail):
        flag = tail[i]
        if flag not in KNOWN_OPTIONS:
            raise OptionError(f"unknown argument {flag!r}")
        if flag in claimed:
            raise OptionError(f"{flag} given more than once")
        arity = KNOWN_OPTIONS[flag]
        values = tail[i + 1:i + 1 + arity]
        if len(values) != arity:
            raise OptionError(f"{flag} takes {arity} argument{'' if arity == 1 else 's'}")
        for value in values:
            if value in KNOWN_OPTIONS:
                raise OptionError(f"{flag} is missing a value ({value!r} is an option)")
        claimed[flag] = values
        i += 1 + arity
    return claimed


def option_int(claimed, flag, default, low):
    if flag not in claimed:
        return default
    (raw,) = claimed[flag]
    try:
        value = int(raw)
    except ValueError:
        raise OptionError(f"{flag} takes a whole number, not {raw!r}") from None
    if value < low:
        raise OptionError(f"{flag} must be at least {low}")
    return value


def option_fit(claimed):
    """--fit W:H as (W, H), two whole numbers of at least 1 joined by one colon; PAGE_FIT without it."""
    if "--fit" not in claimed:
        return PAGE_FIT
    (raw,) = claimed["--fit"]
    w, sep, h = raw.partition(":")
    if not (sep and w.isdigit() and h.isdigit() and w.isascii() and h.isascii()):
        raise OptionError(f"--fit takes W:H, two whole numbers, not {raw!r}")
    if int(w) < 1 or int(h) < 1:
        raise OptionError("--fit's two sides must be at least 1")
    return int(w), int(h)


def read_options(tail):
    """Every command-line decision, made before the scan touches anything."""
    claimed = parse_argv_tail(tail)
    opts = {
        "fit":  option_fit(claimed),
        "rate": option_int(claimed, "--rate", None, 1),
        "url":  claimed["--url"][0] if "--url" in claimed else None,
    }
    # One unambiguous token on one header line, recorded verbatim: what a link may look like is not this tool's
    # business.
    if opts["url"] is not None and (not opts["url"] or any(c.isspace() for c in opts["url"])):
        raise OptionError("--url takes one link with no whitespace in it")
    return opts


def probe_stream(video, selector, entries):
    """ffprobe's entries for the first stream matching `selector`, as a dict; empty when there is none."""
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", selector, "-show_entries",
                          f"stream={entries}", "-of", "default=nw=1", video],
                         capture_output=True, text=True)
    if out.returncode != 0:
        raise Refusal(f"ffprobe cannot read {video!r}")
    fields = {}
    for line in out.stdout.splitlines():
        key, _, value = line.partition("=")
        fields.setdefault(key, value)
    return fields


def video_stream(video):
    """(the frame rate as ffprobe spells it, the same as a Fraction, (width, height)). The spelling goes to ffmpeg's fps
    filter as is, and frame i of the scan is at i / fps seconds, which holds because the stream starts at 0."""
    fields = probe_stream(video, "v:0", "avg_frame_rate,start_time,width,height")
    if not fields:
        raise Refusal("the file has no video stream")
    text = fields.get("avg_frame_rate", "")
    num, _, den = text.partition("/")
    if not (num.isdigit() and den.isdigit() and int(num) > 0 and int(den) > 0):
        raise Refusal(f"the video's frame rate is unreadable ({text!r})")
    try:
        start = float(fields.get("start_time", "0"))
    except ValueError:
        start = math.nan
    if start != 0.0:
        raise Refusal(f"the video stream starts at {fields.get('start_time')!r}, not 0")
    w, h = fields.get("width", ""), fields.get("height", "")
    if not (w.isdigit() and h.isdigit() and int(w) > 0 and int(h) > 0):
        raise Refusal(f"the video's size is unreadable ({w!r} x {h!r})")
    return text, Fraction(int(num), int(den)), (int(w), int(h))


def audio_rate(video):
    """The audio stream's sample rate, or None when the video has none."""
    fields = probe_stream(video, "a:0", "sample_rate")
    rate = fields.get("sample_rate", "")
    return int(rate) if rate.isdigit() and int(rate) > 0 else None


def motion_at(in_motion, diff, drift):
    """Whether a frame moves, given whether its predecessor did: the one rule the scan's anchors and detect read."""
    return drift > DRIFT_THR or (in_motion and diff > SETTLE_THR)


def scan_series(video, vf):
    """The SERIES_ROWS per scan frame, as a 3 x n array (float64, so detect's rerun of motion_at over the cached rows
    decides every frame exactly as the scan did), or a refusal. The drift row's anchor is the first frame of the
    current static run: every moving frame becomes the anchor, so the frame after a transition's last anchors the run
    that follows it. A HALF-DECODED stream looks exactly like a shorter video whose tail pages simply vanish, so an
    ffmpeg failure or a short final frame (a non-empty short read on a pipe is a stream cut mid-frame) refuses whole."""
    proc = subprocess.Popen(["ffmpeg", "-v", "error", "-i", video, "-vf", vf,
                             "-pix_fmt", "gray", "-f", "rawvideo", "-"], stdout=subprocess.PIPE)
    nbytes = DIFF_W * DIFF_H
    prev = anchor = None
    rows = []
    in_motion = False
    truncated = False
    while True:
        buf = proc.stdout.read(nbytes)
        if len(buf) < nbytes:
            truncated = len(buf) > 0
            break
        cur = np.frombuffer(buf, dtype=np.uint8).astype(np.int16)
        if prev is None:
            diff = drift = 0.0
            anchor = cur
        else:
            diff = float(np.abs(cur - prev).mean())
            drift = float(np.abs(cur - anchor).mean())
            in_motion = motion_at(in_motion, diff, drift)
            if in_motion:
                anchor = cur
        rows.append((diff, float(cur.mean()), drift))
        prev = cur
    status = proc.wait()
    if status != 0 or truncated or not rows:
        raise Refusal(f"the frame scan did not complete (ffmpeg exit {status}, {len(rows)} frames"
                      f"{', a truncated final frame' if truncated else ''})")
    return np.array(rows, dtype=np.float64).T


def load_or_scan(video, outdir, vf):
    """(series, stamp, cached). The cache is read here and written only once the run is known to emit."""
    st = os.stat(video)
    stamp = (f"{os.path.realpath(video)}|{st.st_dev}|{st.st_ino}|{st.st_size}|{st.st_mtime_ns}|"
             f"{DIFF_W}x{DIFF_H}|{vf}|{DRIFT_THR!r}|{SETTLE_THR!r}\n")
    npy, meta = os.path.join(outdir, SCAN_NPY), os.path.join(outdir, SCAN_META)
    if os.path.isfile(npy) and os.path.isfile(meta):
        with open(meta) as f:
            if f.read() == stamp:
                return np.load(npy), stamp, True
    return scan_series(video, vf), stamp, False


def detect(series):
    """The series -> (static runs, their black flags, the transitions by class, the advisories). Frame k moves by
    motion_at (the scan's own rule over its own rows). A TRANSITION is a maximal run [a, b] of moving frames; the
    STATIC RUNS are what lies between: frames [s, e] with no moving frame in s+1..e, so they alternate with the
    transitions, the first starting at frame 0 and the last ending at the last frame. A cut is a transition of one
    frame: the static run before it ends at a - 1, the one after starts at a. A longer transition's frames a..b-1 are
    in motion and belong to no static run; frame b, the first after which nothing moves, starts the next one.

    A static run whose mean luminance stays under FADE_FLOOR is BLACK. Each transition is classed by its neighbors:
    lit to black a FADE-OUT, black to lit a FADE-IN, lit to lit a CUT (one frame) or a CROSSFADE (more), and lit to
    lit through black inside the run (a dip with no static black) both a fade-out and a fade-in. The classes are the
    report's: the ranges need none, since the bar lines decide which frames are whose (WHAT A PAGE IS)."""
    diffs, means, drifts = series
    n = len(means)
    moving = np.zeros(n, dtype=bool)
    for k in range(1, n):
        moving[k] = motion_at(moving[k - 1], diffs[k], drifts[k])
    transitions, statics = [], []
    s, k = 0, 1
    while k < n:
        if moving[k]:
            a = k
            while k + 1 < n and moving[k + 1]:
                k += 1
            statics.append((s, a - 1))
            transitions.append((a, k))
            s = k
        k += 1
    statics.append((s, n - 1))
    black = [bool(means[a:b + 1].max() < FADE_FLOOR) for a, b in statics]
    classes = {"cut": [], "crossfade": [], "fade-out": [], "fade-in": [], "within black": []}
    nonmonotonic = []
    for i, (a, b) in enumerate(transitions):
        before, after = black[i], black[i + 1]
        lum = means[a - 1:b + 1]
        if before and after:
            classes["within black"].append((a, b))
        elif after:
            classes["fade-out"].append((a, b))
            # Tolerating noise: a rise past the settle threshold is no fade's.
            if (np.diff(lum) > SETTLE_THR).any():
                nonmonotonic.append(("fade-out", a))
        elif before:
            classes["fade-in"].append((a, b))
            if (np.diff(lum) < -SETTLE_THR).any():
                nonmonotonic.append(("fade-in", a))
        elif lum.min() < FADE_FLOOR:
            classes["fade-out"].append((a, b))
            classes["fade-in"].append((a, b))
        elif a == b:
            classes["cut"].append((a, b))
        else:
            classes["crossfade"].append((a, b))
    return statics, black, classes, nonmonotonic




def canonical_size(width, height):
    """The analysis frame's (width, height): CANON_H lines, the width kept to the video's aspect and rounded to even."""
    return max(2, 2 * round(width * CANON_H / height / 2)), CANON_H


def seek_time(fps, frame):
    """THE SEEK TIME IS HALF A FRAME BEFORE THE FRAME: an input -ss drops every frame whose pts is under it, and WebM
    pts are whole milliseconds truncated (frame 2693 at 30 fps is 89.766, under 89.7667), so the frame's own time can
    land on its successor. Every seek here goes through it: the page grabs and the analysis decodes."""
    return f"{float(max(Fraction(0), (Fraction(frame) - Fraction(1, 2)) / fps)):.6f}"


def decode_gray(video, fps, first, count, size, means):
    """The frames first .. first + count - 1 at the canonical `size`, gray, one ffmpeg; yielded one at a time (a
    cover's window is hundreds of frames). `-fps_mode passthrough` is load-bearing: rawvideo is a constant-rate muxer,
    and without it ffmpeg lays the half-frame-late first frame (seek_time) on a timeline from the seek and DOUBLES it,
    every later frame landing one index late; the setpts lays the frames out at the video's own rate from 0, so the
    muxer never sees two equal timestamps (passthrough alone carries the whole-millisecond pts, which collide on the
    muxer's time base, and ffmpeg prints a line per collision). Each frame's mean is checked against the scan's
    (`means`, the series' row) within ALIGN_TOL, so a decode that is not the scan's frame for frame refuses instead of
    tiling wrong; fewer frames than asked is a refusal, as a short scan is."""
    w, h = size
    proc = subprocess.Popen(["ffmpeg", "-v", "error", "-ss", seek_time(fps, first), "-i", video,
                             "-frames:v", str(count), "-fps_mode", "passthrough",
                             "-vf", f"setpts=N/({fps.numerator}/{fps.denominator})/TB,scale={w}:{h}:flags=area",
                             "-pix_fmt", "gray", "-f", "rawvideo", "-"], stdout=subprocess.PIPE)
    got = 0
    try:
        while got < count:
            buf = proc.stdout.read(w * h)
            if len(buf) < w * h:
                break
            gray = np.frombuffer(buf, dtype=np.uint8).reshape(h, w)
            if abs(gray.mean() - means[first + got]) > ALIGN_TOL:
                raise Refusal(f"the decode at frame {first + got} is not the scan's (mean {gray.mean():.2f}, the "
                              f"scan's {means[first + got]:.2f})")
            got += 1
            yield gray
    finally:
        proc.stdout.close()
        status = proc.wait()
    if got < count or status != 0:
        raise Refusal(f"the decode at frame {first} did not complete (ffmpeg exit {status}, {got} of {count} frames)")


def central_levels(gray, percents):
    """The frame's gray levels at `percents` (percentiles, 0-100) over its central half, as numpy's own integers."""
    h, w = gray.shape
    cum = np.cumsum(np.bincount(gray[h // 4:3 * h // 4, w // 4:3 * w // 4].ravel(), minlength=256))
    return np.searchsorted(cum, [cum[-1] * p / 100 for p in percents])


def ink_and_paper(gray):
    """The frame's (ink, paper) levels: its INK_PCT and PAPER_PCT percentiles over its central half (the ridge's
    contrast, ridge_mask; the PDF road's ink test, systems_of)."""
    # numpy's own integers, NOT Python's: ridge_mask's depth is then a float64 scalar, and the float32 smoothed frame
    # times it is computed in float64 (NumPy 2's promotion), as the video road's measurements were taken; a Python
    # float would keep the product in float32 and move a pixel or two of the mask.
    ink, paper = central_levels(gray, (INK_PCT, PAPER_PCT))
    return ink, paper


def ridge_mask(gray):
    """The frame's ridge pixels (A BAR LINE IS A THIN DARK RIDGE), on the frame smoothed over SMOOTH_ROWS rows."""
    ink, paper = ink_and_paper(gray)
    depth = RIDGE_DEPTH * (paper - ink) / paper if paper > 0 else 1.0
    k = SMOOTH_ROWS
    rows = np.cumsum(np.pad(gray.astype(np.int32), ((k // 2 + 1, k - k // 2 - 1), (0, 0)), mode="edge"), axis=0)
    smooth = ((rows[k:] - rows[:-k]) / k).astype(np.float32)
    s = RIDGE_SIDE
    side = np.minimum(smooth[:, :-2 * s], smooth[:, 2 * s:])
    drop = side - smooth[:, s:-s]
    mask = np.zeros(gray.shape, dtype=bool)
    mask[:, s:-s] = (drop > depth * side) & (drop >= MIN_DROP) & (side >= MIN_GROUND * paper)
    return mask


def close_vertical(mask):
    """The binary closing with a (CLOSE_GAP + 1) x 1 structure, centered: a dilation then an erosion, each the OR or
    the AND of the shifted rows. The erosion counts the frame's outside as set, so a run touching the edge keeps its
    end."""
    r = CLOSE_GAP // 2
    grown = mask.copy()
    for o in range(1, r + 1):
        grown[o:] |= mask[:-o]
        grown[:-o] |= mask[o:]
    closed = grown.copy()
    for o in range(1, r + 1):
        closed[o:] &= grown[:-o]
        closed[:-o] &= grown[o:]
    return closed


def longest_runs(mask):
    """Per column, the longest vertical run of set pixels."""
    rows = np.arange(mask.shape[0], dtype=np.int32)[:, None]
    last_unset = np.maximum.accumulate(np.where(mask, np.int32(-1), rows), axis=0)
    return np.where(mask, rows - last_unset, 0).max(axis=0)


def shear_mask(mask, slope):
    """The ridge mask sheared by `slope` (THE SHEAR): row y shifted by (y - h / 2) * slope columns, rounded."""
    if not slope:
        return mask
    h, w = mask.shape
    shift = np.rint((np.arange(h) - h / 2) * slope).astype(np.int64)
    cols = np.arange(w)[None, :] + shift[:, None]
    return np.where((cols >= 0) & (cols < w), mask[np.arange(h)[:, None], np.clip(cols, 0, w - 1)], False)


def sheared_runs(mask, slope):
    """(the shear's score, the per-column longest healed runs) of the ridge mask sheared by `slope` (THE SHEAR)."""
    runs = longest_runs(close_vertical(shear_mask(mask, slope)))
    long = runs[runs >= MIN_BARLINE_RUN].astype(np.float64)
    return float((long * long).sum()), runs


def lines_of(runs):
    """The SIGNATURE out of the per-column runs: the bar lines' x-centers in canonical px, ascending."""
    cols = np.flatnonzero(runs >= MIN_BARLINE_RUN)
    if len(cols) == 0:
        return []
    clusters, start = [], cols[0]
    for prev, c in zip(cols, cols[1:]):
        if c - prev > MERGE_GAP:
            clusters.append((start, prev))
            start = c
    clusters.append((start, cols[-1]))
    return [(int(a) + int(b)) / 2 for a, b in clusters]


def best_shear(mask):
    """(the signature, the slope) at the best of THE SHEAR's candidates."""
    def at(deg):
        slope = math.tan(math.radians(deg))
        return (*sheared_runs(mask, slope), slope, deg)

    best = max((at(deg) for deg in SHEAR_COARSE_DEG), key=lambda c: c[0])
    for deg in (best[3] - SHEAR_FINE_DEG, best[3] + SHEAR_FINE_DEG):
        cand = at(deg)
        if cand[0] > best[0]:
            best = cand
    return lines_of(best[1]), best[2]


def share(a, b):
    """How many lines two ascending signatures share within MATCH_TOL, paired in order (bar lines stand at least
    MERGE_GAP apart, ten times the tolerance, so the in-order pairing is the only one)."""
    i = j = shared = 0
    while i < len(a) and j < len(b):
        if abs(a[i] - b[j]) <= MATCH_TOL:
            shared, i, j = shared + 1, i + 1, j + 1
        elif a[i] < b[j]:
            i += 1
        else:
            j += 1
    return shared


def same_page(a, b):
    return bool(a) and bool(b) and share(a, b) >= SAME_MIN * max(len(a), len(b))


def shows(page, frame):
    """How fully a frame's signature shows a page's: the share of the page's lines it carries."""
    return share(page, frame) / len(page)


def tile_gap(frames, before, after):
    """THE BOUNDARY between two pages' runs (WHAT A PAGE IS): `frames` are, per frame between them, its signature at
    the previous page's shear and at the next page's, `before` and `after` the two pages' signatures (None before the
    first page and after the last). Returns (the boundary's offset into the gap, the frames of the previous page's
    matched fade-out, of fill, of the next page's matched fade-in): the previous page keeps offsets 0 .. boundary - 1,
    the next takes boundary on. The matched fade-out ends at the first frame that fails to show the previous page;
    before the first page the boundary is 0, and the fade-in counts from the first frame that shows the page; after
    the last page it is the gap's end."""
    count = len(frames)
    s_before = [shows(before, f) if before else 0.0 for f, _ in frames]
    s_after = [shows(after, f) if after else 0.0 for _, f in frames]
    if before is None:
        first = next((k for k in range(count) if s_after[k] >= SAME_MIN), count)
        return 0, 0, first, count - first
    if after is None:
        boundary = count
    else:
        boundary = next((k for k in range(count) if s_after[k] >= SAME_MIN and s_after[k] >= s_before[k]), count)
    fade_out = next((k for k in range(boundary) if s_before[k] < SAME_MIN), boundary)
    return boundary, fade_out, boundary - fade_out, count - boundary


def spell_time(seconds):
    minutes, rest = divmod(float(seconds), 60.0)
    return f"{int(minutes)}:{rest:06.3f}"


def page_name(number, count):
    return f"page{number:0{max(3, len(str(count)))}d}.jpg"


def stale_pages(outdir, count):
    """By name, the page files in `outdir` this run does not write: every page<NNN>.png (the spelling this tool no
    longer writes), and every page<NNN>.jpg numbered past `count` or carrying another padding than page_name's."""
    names = []
    for name in sorted(os.listdir(outdir)):
        stem = name[4:-4] if name.startswith("page") and name.endswith((".png", ".jpg")) else ""
        if not (stem.isdigit() and stem.isascii()):
            continue
        if name.endswith(".png") or int(stem) > count or name != page_name(int(stem), count):
            names.append(name)
    return names


def fit_size(width, height, box):
    """The page's (width, height) in the box: THE VIEWER'S OWN FIT (score_page_worker.cpp's decode_fit_page) — the
    smaller of the box's two ratios, each side rounded at the element (Python's round and the worker's std::nearbyint
    both round half to even, on the same doubles) — ONE RULE, DOWN OR UP (planner's ruling 2026-10-26: a page is a
    picture to fit the box, nothing more; a narrower rip enlarged by a few percent loses nothing). The tool computes
    the pair itself rather than leave it to ffmpeg's force_original_aspect_ratio (whose av_rescale rounds half away
    from zero), because the gray pipe needs the size, and so that EVERY page made for a box is exactly the size the
    viewer would scale it to there: its scale pass then never runs (the worker's identity short-circuit)."""
    k = min(box[0] / width, box[1] / height)
    return round(width * k), round(height * k)


class GrabFailure(Exception):
    """A page grab that did not complete, carrying its words."""


def grab_page(video, fps, frame, size, out):
    """One page: ffmpeg decodes the frame, scales it to `size` with the box filter (flags=area, the reduction of
    thin staff lines DIFF_W's note gives it for; an enlargement it interpolates) and pipes it as one gray rawvideo frame; Pillow writes the grayscale
    JPEG (PAGE_JPEG_QUALITY)."""
    w, h = size
    proc = subprocess.run(["ffmpeg", "-v", "error", "-ss", seek_time(fps, frame), "-i", video, "-frames:v", "1",
                           "-vf", f"scale={w}:{h}:flags=area,format=gray", "-pix_fmt", "gray", "-f", "rawvideo", "-"],
                          stdout=subprocess.PIPE)
    if proc.returncode != 0 or len(proc.stdout) != w * h:
        raise GrabFailure(f"ffmpeg could not grab a page (exit {proc.returncode}, {len(proc.stdout)} of {w * h} "
                          "bytes)")
    Image.frombytes("L", (w, h), proc.stdout).save(out, "JPEG", quality=PAGE_JPEG_QUALITY, optimize=True)


def group_pages(lit, signatures):
    """The lit static runs and their (signature, shear) -> (the pages, the non-pages). A page is a list of runs, its
    first, last and PRINCIPAL run (the longest, the first of equals) and the principal's signature and shear; a run
    joins the page before it when the two show the same page (same_page against that page's last run), whatever lies
    between them."""
    pages, non_pages = [], []
    for run, (sig, slope) in zip(lit, signatures):
        if len(sig) < MIN_BARLINES:
            non_pages.append(run)
        elif pages and same_page(pages[-1]["runs"][-1][1], sig):
            pages[-1]["runs"].append((run, sig, slope))
        else:
            pages.append({"runs": [(run, sig, slope)]})
    for page in pages:
        page["first"], page["last"] = page["runs"][0][0][0], page["runs"][-1][0][1]
        page["principal"], page["sig"], page["slope"] = max(page["runs"], key=lambda r: r[0][1] - r[0][0])
    return pages, non_pages


def is_pdf(path):
    """Whether the input is a PDF: the `%PDF-` magic in its first bytes (PDF_MAGIC). Anything else is a video."""
    with open(path, "rb") as f:
        return f.read(len(PDF_MAGIC)) == PDF_MAGIC


def pdf_page_boxes(pdf):
    """Per PDF page, its (width, height) in points as pdftoppm `-cropbox` renders it: the CropBox's sides, swapped
    where the page is rotated a quarter turn. pdfinfo's `-box` spells the CropBox outright (its plain `size` line is
    not the box pdftoppm renders by default, and the Internet Archive's scans carry a CropBox inside the MediaBox)."""
    out = subprocess.run(["pdfinfo", pdf], capture_output=True, text=True, errors="replace")
    pages = next((line.split()[1] for line in out.stdout.splitlines() if line.startswith("Pages:")), "")
    if out.returncode != 0 or not (pages.isdigit() and int(pages) > 0):
        raise Refusal(f"pdfinfo cannot read {pdf!r}")
    count = int(pages)
    out = subprocess.run(["pdfinfo", "-box", "-f", "1", "-l", str(count), pdf], capture_output=True, text=True,
                         errors="replace")
    boxes, rots = {}, {}
    for line in out.stdout.splitlines():
        words = line.split()
        if len(words) >= 4 and words[0] == "Page" and words[1].isdigit():
            if words[2] == "CropBox:" and len(words) == 7:
                x0, y0, x1, y1 = (float(v) for v in words[3:])
                boxes[int(words[1])] = (abs(x1 - x0), abs(y1 - y0))
            elif words[2] == "rot:" and words[3].lstrip("-").isdigit():
                rots[int(words[1])] = int(words[3])
    if out.returncode != 0 or sorted(boxes) != list(range(1, count + 1)):
        raise Refusal(f"pdfinfo cannot read the page boxes of {pdf!r}")
    sizes = []
    for page in range(1, count + 1):
        w, h = boxes[page]
        if w <= 0 or h <= 0:
            raise Refusal(f"PDF page {page} has an empty CropBox")
        sizes.append((h, w) if rots.get(page, 0) % 180 == 90 else (w, h))
    return sizes


def render_pdf_page(pdf, page, size):
    """PDF page `page` (1-based) rendered gray at exactly `size` (width, height) px, as a uint8 array: pdftoppm
    renders it PDF_SUPERSAMPLE times larger on each side (-scale-to-x and -scale-to-y together set the size exactly,
    whatever the page's aspect) and Pillow's reduce box-averages each PDF_SUPERSAMPLE-square block to one pixel. None
    when pdftoppm fails or its picture is not the size asked for."""
    w, h = size
    k = PDF_SUPERSAMPLE
    proc = subprocess.run(["pdftoppm", "-q", "-gray", "-cropbox", "-f", str(page), "-l", str(page), "-singlefile",
                           "-scale-to-x", str(w * k), "-scale-to-y", str(h * k), pdf], capture_output=True)
    # THE PGM HEADER pdftoppm writes, byte for byte: "P5", the size, the maxval, each ended by one newline. The
    # pixels follow that last newline and may start with a whitespace byte, so the header is matched, never split.
    head = f"P5\n{w * k} {h * k}\n255\n".encode()
    if proc.returncode != 0 or not proc.stdout.startswith(head) or len(proc.stdout) != len(head) + w * k * h * k:
        return None
    big = Image.frombytes("L", (w * k, h * k), proc.stdout[len(head):])
    return np.asarray(big.reduce(k) if k > 1 else big)


def covered_rows(closed):
    """Per row of a healed (close_vertical) mask, whether some column's vertical run of at least MIN_BARLINE_RUN set
    pixels (a bar line's, the video road's floor) passes through it: each set pixel's run length is the rows since
    its run's first plus the rows to its run's last, less one."""
    h = closed.shape[0]
    rows = np.arange(h, dtype=np.int32)[:, None]
    up = np.where(closed, rows - np.maximum.accumulate(np.where(closed, np.int32(-1), rows), axis=0), 0)
    flip = closed[::-1]
    down = np.where(flip, rows - np.maximum.accumulate(np.where(flip, np.int32(-1), rows), axis=0), 0)[::-1]
    return ((up + down - 1) * closed >= MIN_BARLINE_RUN).any(axis=1)


def left_line(runs):
    """The page's LEFT LINE (THE BLOCKS AND THE LEFT LINE): the columns (first, last) of its leftmost bar line, the
    columns whose healed runs reach MIN_BARLINE_RUN within MERGE_GAP of the leftmost one (lines_of's clustering).
    None on a page without bar lines."""
    cols = np.flatnonzero(runs >= MIN_BARLINE_RUN)
    if len(cols) == 0:
        return None
    last = cols[0]
    for c in cols[1:]:
        if c - last > MERGE_GAP:
            break
        last = c
    return int(cols[0]), int(last)


def opening_extent(line_ink, top, bottom, cols):
    """THE EXTENT (the PDF ROAD): the rows (top, bottom) of the system [top, bottom] joined with every run of
    `line_ink` in the columns `cols` (the system's opening line) that meets the system's rows."""
    for c in cols:
        v = line_ink[:, c]
        if not v[top:bottom + 1].any():
            continue
        edges = np.flatnonzero(np.diff(np.concatenate(([False], v, [False])).astype(np.int8)))
        for a, b in zip(edges[::2], edges[1::2] - 1):
            if a <= bottom and b >= top:
                top, bottom = min(top, int(a)), max(bottom, int(b))
    return top, bottom


def unbordered(inked):
    """An ink mask without THE BORDER: the ink running in unbroken from either edge of the page along its row (a
    scan's dark edge, the scanner bed beyond a torn or cropped page)."""
    return (inked & ~np.logical_and.accumulate(inked, axis=1)
            & ~np.logical_and.accumulate(inked[:, ::-1], axis=1)[:, ::-1])


def systems_of(gray):
    """A PAGE'S SYSTEMS (the PDF ROAD's BLOCKS AND LEFT LINE, then its EXTENT) off its analysis render: (the systems
    as (top, bottom) canonical rows, both inclusive, top to bottom; the page's shear in degrees; its ink mask, gray
    under the midpoint of its ink and paper levels, THE BORDER removed). The covered blocks first; each gap the left
    line's ink crosses joins the blocks either side of it into one system; then each system takes the rows its opening
    line spans, and systems that then meet are one."""
    mask = ridge_mask(gray)
    _, slope = best_shear(mask)
    closed = close_vertical(shear_mask(mask, slope))
    covered = covered_rows(closed)
    blocks, row, h = [], 0, len(covered)
    while row < h:
        if covered[row]:
            top = row
            while row + 1 < h and covered[row + 1]:
                row += 1
            blocks.append((top, row))
        row += 1
    ink, paper = ink_and_paper(gray)
    inked_page = gray < (ink + paper) / 2
    systems = blocks[:1]
    if not blocks:
        return systems, math.degrees(math.atan(slope)), unbordered(inked_page)
    inked = close_vertical(shear_mask(inked_page, slope))
    if len(blocks) > 1:
        first, last = left_line(longest_runs(closed))
        x0, x1 = max(first - RIDGE_SIDE, 0), last + RIDGE_SIDE + 1
        for top, bottom in blocks[1:]:
            gap = inked[systems[-1][1] + 1:top]
            # A column crosses when it is ink in every row of the gap and paper stands left of it in some row: ink
            # running in from the page's edge in every row is the scan's border, never the line.
            border = np.logical_and.accumulate(gap, axis=1).all(axis=0)
            if (gap.all(axis=0) & ~border)[x0:x1].any():
                systems[-1] = (systems[-1][0], bottom)
            else:
                systems.append((top, bottom))
    # THE EXTENT: each system's opening line, its leftmost column inked through all its rows (and the columns within
    # MERGE_GAP of it that are too), on the ink without THE BORDER healed again (a row where the scan's dark edge
    # touches the line takes the line with the border); every run of it meeting the system joins the system.
    line_ink = close_vertical(unbordered(inked))
    extended = []
    for top, bottom in systems:
        whole = np.flatnonzero(line_ink[top:bottom + 1].all(axis=0))
        cols = ([int(c) for c in whole[:np.searchsorted(whole, whole[0] + MERGE_GAP, side="right")]]
                if len(whole) else [])
        extended.append(opening_extent(line_ink, top, bottom, cols))
    systems = extended[:1]
    for top, bottom in extended[1:]:
        if top <= systems[-1][1] + 1:
            systems[-1] = (systems[-1][0], max(systems[-1][1], bottom))
        else:
            systems.append((top, bottom))
    return systems, math.degrees(math.atan(slope)), unbordered(inked_page)


def staff_ridges(gray):
    """THE STAFF LINES' RIDGE (THE STAFF TEST): ridge_mask turned a quarter: a pixel darker than both pixels
    STAFF_SIDE rows above and below it by STAFF_DEPTH of the frame's contrast, on the frame smoothed over SMOOTH_ROWS
    columns."""
    ink, paper = ink_and_paper(gray)
    depth = STAFF_DEPTH * (paper - ink) / paper if paper > 0 else 1.0
    k = SMOOTH_ROWS
    cols = np.cumsum(np.pad(gray.astype(np.int32), ((0, 0), (k // 2 + 1, k - k // 2 - 1)), mode="edge"), axis=1)
    smooth = ((cols[:, k:] - cols[:, :-k]) / k).astype(np.float32)
    s = STAFF_SIDE
    side = np.minimum(smooth[:-2 * s], smooth[2 * s:])
    drop = side - smooth[s:-s]
    mask = np.zeros(gray.shape, dtype=bool)
    mask[s:-s] = (drop > depth * side) & (drop >= MIN_DROP) & (side >= MIN_GROUND * paper)
    return mask


def row_shear(mask, slope, middle=None):
    """The mask with its rows straightened for a page whose horizontal lines climb `slope` rows per column: column x
    shifted by (x - middle) * slope rows, rounded (THE SHEAR's turn for the rows), `middle` the page's middle column
    in the mask's columns (the mask's own middle by default)."""
    if not slope:
        return mask
    h, w = mask.shape
    shift = np.rint((np.arange(w) - (w / 2 if middle is None else middle)) * slope).astype(np.int64)
    rows = np.arange(h)[:, None] + shift[None, :]
    return np.where((rows >= 0) & (rows < h), mask[np.clip(rows, 0, h - 1), np.arange(w)[None, :]], False)


def staff_cover(ridges, slope):
    """Per row, the share of the page's central half that is a staff-line ridge at the row shear `slope`, a ridge
    of the row below counting (a line straddling two rows splits its pixels between them)."""
    w = ridges.shape[1]
    m = row_shear(ridges[:, w // 4:3 * w // 4], slope, w / 2 - w // 4)
    pair = m.copy()
    pair[:-1] |= m[1:]
    return pair.mean(axis=1)


def staves_of(gray):
    """THE STAFF TEST off a page's analysis render: (its staves as (top line, bottom line, spacing), canonical rows of
    the rows straightened about the middle column; the row shear, rows per column). The row shear is searched as
    THE SHEAR's (STAFF_SKEW_COARSE_DEG, then STAFF_SKEW_FINE_DEG steps either side), the sum of the rows' squared
    cover the score; a LINE is a run of rows whose cover reaches STAFF_SHARE (its center the cover-weighted row); A
    STAFF is five consecutive lines whose four spacings lie within STAFF_SPACING and agree within STAFF_TOL, standing
    alone: the lines before and after it lie more than STAFF_ALONE spacings away."""
    ridges = staff_ridges(gray)

    def at(deg):
        slope = math.tan(math.radians(deg))
        cover = staff_cover(ridges, slope)
        return float((cover * cover).sum()), deg, slope, cover

    best = max((at(deg) for deg in STAFF_SKEW_COARSE_DEG), key=lambda c: c[0])
    for step in range(1, STAFF_SKEW_FINE_STEPS + 1):
        for deg in (best[1] - step * STAFF_SKEW_FINE_DEG, best[1] + step * STAFF_SKEW_FINE_DEG):
            cand = at(deg)
            if cand[0] > best[0]:
                best = cand
    _, _, slope, cover = best
    rows = np.flatnonzero(cover >= STAFF_SHARE)
    lines, i = [], 0
    while i < len(rows):
        j = i
        while j + 1 < len(rows) and rows[j + 1] == rows[j] + 1:
            j += 1
        run = np.arange(rows[i], rows[j] + 1)
        lines.append(float((run * cover[run]).sum() / cover[run].sum()))
        i = j + 1
    staves, i = [], 0
    while i + 4 < len(lines):
        gaps = np.diff(lines[i:i + 5])
        mean = float(gaps.mean())
        alone = ((i == 0 or lines[i] - lines[i - 1] > STAFF_ALONE * mean)
                 and (i + 5 == len(lines) or lines[i + 5] - lines[i + 4] > STAFF_ALONE * mean))
        if (alone and STAFF_SPACING[0] <= gaps.min() and gaps.max() <= STAFF_SPACING[1]
                and gaps.max() - gaps.min() <= max(1.0, STAFF_TOL * mean)):
            staves.append((lines[i], lines[i + 4], mean))
            i += 5
        else:
            i += 1
    return staves, slope, ridges


def read_pdf_page(gray):
    """A PDF page's analysis render read whole: a dict of its systems (canonical rows, top to bottom), whether each
    is SURE (a staff's middle row inside it), its staves and their rows on the page (each staff's lines' span widened
    by the row shear across the staff lines' columns), the BLANK ROWS (no ink across those columns: THE AVERAGE
    ROWS' snap) and the shears."""
    systems, deg, inked = systems_of(gray)
    staves, slope, ridges = staves_of(gray)
    h, w = gray.shape
    page = {"systems": systems, "deg": deg, "staves": staves, "row_slope": slope}
    page["sure"] = [any(top <= (a + b) / 2 <= bottom for a, b, _ in staves) for top, bottom in systems]
    page["staff_rows"], page["blank"], page["ink_cols"] = [], None, None
    if staves:
        # THE INK COLUMNS: where the staff lines run, the columns at least half the lines' rows hold a ridge in,
        # less RIDGE_SIDE either end (a line's ragged end, a bracket's stroke); the central half where none is.
        straight = row_shear(ridges, slope)
        held = np.zeros(w, dtype=np.int32)
        for a, b, spacing in staves:
            for line in range(5):
                y = int(round(a + line * spacing))
                held += straight[max(y - 1, 0):y + 2].any(axis=0)
        cols = np.flatnonzero(held * 2 >= 5 * len(staves))
        x0, x1 = (int(cols[0]) + RIDGE_SIDE, int(cols[-1]) - RIDGE_SIDE) if len(cols) else (w // 4, 3 * w // 4)
        x0, x1 = (x0, x1) if x1 > x0 else (w // 4, 3 * w // 4)
        lean = abs(slope) * max(abs(x0 - w / 2), abs(x1 - w / 2))
        page["staff_rows"] = [(a - lean, b + lean) for a, b, _ in staves]
        page["blank"] = ~inked[:, x0:x1 + 1].any(axis=1)
        page["ink_cols"] = (x0, x1)
    return page


def pdf_windows(systems, rows_per_canon, box_h):
    """THE WINDOW over one page's systems: per slice (its top row in the page's device render, the first system's
    index, the count of systems it holds). The top stands PAD canonical rows over its first system's top; each
    following system joins while its bottom plus PAD lies inside the box's height, greedy; the first that does not
    starts the next slice. A slice always holds its first system whole (the document scale saw to it)."""
    windows, k = [], 0
    while k < len(systems):
        top = round((systems[k][0] - PAD) * rows_per_canon)
        held = 1
        while (k + held < len(systems)
               and round((systems[k + held][1] + 1 + PAD) * rows_per_canon) <= top + box_h):
            held += 1
        windows.append((top, k, held))
        k += held
    return windows


def snap_cut(blank, row):
    """THE AVERAGE ROWS' SNAP: (the nearest blank row at or above `row` within SNAP_TOL rows, else the nearest below
    within SNAP_TOL; a row outside the page counts blank, the fill; the signed move, up negative) or (row, None)
    where none is."""
    h = len(blank)
    for step in (-1, 1):
        for d in range(SNAP_TOL + 1):
            r = row + step * d
            if r < 0 or r >= h or blank[r]:
                return r, step * d
    return row, None


def rise_window(top, first, last, staff_rows, rows_per_canon, box_h):
    """THE RISE over a sure window (THE WINDOW): (its top in device rows, the staves it rose over). The staff standing
    nearest above the window's first system (canonical rows `first`) joins the window when the gap between them is no
    wider than the widest gap between two consecutive staves inside the window's systems ([first, last]) and the last
    system's bottom plus PAD still lies inside the box: the top then stands PAD over that staff, and the next staff up
    is tried. A window over fewer than two staves never rises. Its reason and its measurements are THE RISE's, in the
    head's WINDOW: the group an edition bars apart above the opening line's reach shows with its system, and no scale
    changes, since the window rises only where its last system still fits."""
    inside = sorted(rows for rows in staff_rows if first <= (rows[0] + rows[1]) / 2 <= last)
    if len(inside) < 2:
        return top, 0
    widest = max(below[0] - above[1] for above, below in zip(inside, inside[1:]))
    upper, staves = first, 0
    while True:
        above = [rows for rows in staff_rows if rows[1] < upper]
        if not above:
            return top, staves
        rows = max(above, key=lambda r: r[1])
        risen = round((rows[0] - PAD) * rows_per_canon)
        if upper - rows[1] > widest or (last + 1 + PAD) * rows_per_canon > risen + box_h:
            return top, staves
        top, upper, staves = risen, rows[0], staves + 1


def plan_pdf(sizes, read, box):
    """THE PDF ROAD's plan off every page's reading: (the scale in device px per point, each page's render size,
    the slices as (PDF page, window top in its render, the slice's words), the tallest sure system (rows, PDF page),
    the average rows (canonical tops), the per-page outcome words). A page with a sure system takes its windows over
    its sure systems; THE AVERAGE ROWS then show every staff they leave, and a page with staves and no sure system is
    shown by the average rows alone; a page with no staff is no page."""
    box_w, box_h = box

    def pt_per_row(page):
        return sizes[page - 1][1] / CANON_H

    sure_pages = [page for page in range(1, len(sizes) + 1) if any(read[page - 1]["sure"])]
    staff_pages = [page for page in range(1, len(sizes) + 1) if read[page - 1]["staves"]]
    if not sure_pages:
        raise Refusal(f"no page: none of the {len(sizes)} PDF pages carries a system with a staff")
    sure_systems = [(page, a, b) for page in sure_pages
                    for (a, b), sure in zip(read[page - 1]["systems"], read[page - 1]["sure"]) if sure]
    tall_rows, tall_page = max(((b - a + 1 + 2 * PAD) * pt_per_row(page), b - a + 1, page)
                               for page, a, b in sure_systems)[1:]
    scale = min(min(box_w / sizes[page - 1][0] for page in staff_pages),
                min(box_h / ((b - a + 1 + 2 * PAD) * pt_per_row(page)) for page, a, b in sure_systems))
    render_size = {page: (round(sizes[page - 1][0] * scale), round(sizes[page - 1][1] * scale))
                   for page in staff_pages}

    def rpc(page):
        return render_size[page][1] / CANON_H

    sure_windows = {page: pdf_windows([s for s, sure in zip(read[page - 1]["systems"], read[page - 1]["sure"])
                                       if sure], rpc(page), box_h) for page in sure_pages}
    # THE AVERAGE ROWS: the windows' canonical tops of the sure pages holding the modal count of windows, averaged
    # per slice index.
    counts = [len(sure_windows[page]) for page in sure_pages]
    mode = max(sorted(set(counts)), key=counts.count)
    modal = [page for page in sure_pages if len(sure_windows[page]) == mode]
    average = [round(sum(sure_windows[page][i][0] / rpc(page) for page in modal) / len(modal)) for i in range(mode)]

    slices, outcome = [], {}
    for page in range(1, len(sizes) + 1):
        reading = read[page - 1]
        if not reading["staves"]:
            outcome[page] = ("dropped", f"not a page: PDF page {page}, no staff"
                             + (f" ({len(reading['systems'])} block{'' if len(reading['systems']) == 1 else 's'} of "
                                "bar lines)" if reading["systems"] else ""))
            continue
        k_rows = rpc(page)
        windows = []                             # (top, words)
        systems = [s for s, sure in zip(reading["systems"], reading["sure"]) if sure]
        for top, k, held in sure_windows.get(page, []):
            risen, staves = rise_window(top, systems[k][0], systems[k + held - 1][1], reading["staff_rows"], k_rows,
                                        box_h)
            windows.append((risen, f"system {k + 1} of {len(systems)}, rows {systems[k][0]}..{systems[k + held - 1][1]}"
                                   f" (+ the pad), {held} system{'' if held == 1 else 's'}"
                                   + (f", risen over {staves} {'staff' if staves == 1 else 'staves'} above it"
                                      if staves else "")))

        def shows(top, rows):
            return top <= rows[0] * k_rows and (rows[1] + 1) * k_rows <= top + box_h

        unshown = [rows for rows in reading["staff_rows"] if not any(shows(top, rows) for top, _ in windows)]
        added = []
        for cut in average:
            snapped, moved = snap_cut(reading["blank"], cut)
            top = round(snapped * k_rows)
            held = [rows for rows in unshown if shows(top, rows)]
            if held:
                unshown = [rows for rows in unshown if rows not in held]
                added.append((top, snapped, moved, len(held)))
        for top, snapped, moved, held in added:
            windows.append((top, f"the average rows, cut at {snapped} "
                                 + (f"(snapped {moved:+d})" if moved is not None else "(no blank row near)")
                                 + f", {held} {'staff' if held == 1 else 'staves'} the sure windows leave"
                                 if systems else
                                 f"no sure system: the average rows, cut at {snapped} "
                                 + (f"(snapped {moved:+d})" if moved is not None else "(no blank row near)")
                                 + f", {held} {'staff' if held == 1 else 'staves'}"))
        lost = f"; {len(unshown)} {'staff' if len(unshown) == 1 else 'staves'} in no window" if unshown else ""
        if not windows:
            outcome[page] = ("dropped", f"not a page: PDF page {page}, its {len(reading['staves'])} "
                             f"{'staff' if len(reading['staves']) == 1 else 'staves'} in no window of the average rows")
            continue
        outcome[page] = ("sure" if systems else "average", lost)
        windows.sort(key=lambda w: w[0])
        for top, words in windows:
            slices.append((page, top, words + (lost if (top, words) == windows[-1] else "")))
    return scale, render_size, slices, (tall_rows, tall_page), average, outcome


def edge_tones(render):
    """THE FILL's tones, (above the page, below it): the median gray of the page's own edge rows, its first and last
    PAD canonical rows over its central half; an edge whose median reads as ink (under the midpoint of the page's ink
    and paper levels: the scanner's bed beyond a torn edge) takes the page's median over its central half
    (FILL_PCT)."""
    h, w = render.shape
    band = max(1, round(PAD * h / CANON_H))
    ink, paper = ink_and_paper(render)
    page, = central_levels(render, (FILL_PCT,))
    tones = []
    for rows in (render[:band, w // 4:3 * w // 4], render[h - band:, w // 4:3 * w // 4]):
        tone = int(np.median(rows))
        tones.append(tone if tone >= (int(ink) + int(paper)) / 2 else int(page))
    return tuple(tones)


def cut_slice(render, top, box_h, tones):
    """The window's rows [top, top + box_h) of a page's device render, the rows past the page's edge THE FILL: the
    page's own edge tone above its top and below its bottom (edge_tones)."""
    h, w = render.shape
    out = np.empty((box_h, w), dtype=np.uint8)
    a, b = max(top, 0), min(top + box_h, h)
    out[:max(a - top, 0)] = tones[0]
    out[max(b - top, 0):] = tones[1]
    if b > a:
        out[a - top:b - top] = render[a:b]
    return out


def pdf_main(pdf, outdir, opts):
    """THE PDF ROAD, from the options to the last line."""
    if opts["rate"] is not None:
        print("REFUSE: a PDF's frames are its pages, at rate 1: --rate is for a video")
        return 1
    missing = [tool for tool in ("pdftoppm", "pdfinfo") if shutil.which(tool) is None]
    if missing:
        print(f"REFUSE: a PDF needs poppler's pdftoppm and pdfinfo on the PATH ({' and '.join(missing)} missing)")
        return 1
    box_w, box_h = opts["fit"]
    try:
        sizes = pdf_page_boxes(pdf)

        # THE ANALYSIS, every page before any slice: the document scale needs the tallest sure system, the average
        # rows every sure page's windows.
        def analyse(page):
            w, h = sizes[page - 1]
            gray = render_pdf_page(pdf, page, canonical_size(w, h))
            if gray is None:
                raise Refusal(f"pdftoppm could not render PDF page {page}")
            return read_pdf_page(gray)

        with ThreadPoolExecutor(GRAB_WORKERS) as pool:
            read = list(pool.map(analyse, range(1, len(sizes) + 1)))
        scale, render_size, slices, (tall_rows, tall_page), average, outcome = plan_pdf(sizes, read, (box_w, box_h))
    except Refusal as err:
        print(f"REFUSE: {err}")
        return 1
    count = len(slices)
    shown = sorted({page for page, *_ in slices})

    widths = sorted({size[0] for size in sizes})
    shape = (f"{sizes[0][0]:g}x{sizes[0][1]:g} pt" if len(set(sizes)) == 1
             else f"{len(set(sizes))} page sizes, {widths[0]:g} to {widths[-1]:g} pt wide")
    slice_w = sorted({render_size[page][0] for page in shown})
    kinds = [kind for kind, _ in outcome.values()]
    print(f"{len(sizes)} PDF pages, {shape}; tallest sure system {tall_rows} rows (PDF page {tall_page}); scale "
          f"{scale:.3f} px/pt ({scale * 72:.1f} dpi); slices "
          + (f"{slice_w[0]}x{box_h}" if len(slice_w) == 1 else f"{slice_w[0]}..{slice_w[-1]}x{box_h}")
          + f"; {kinds.count('sure')} sure pages, {kinds.count('average')} average-rows pages, "
          f"{kinds.count('dropped')} dropped; the average rows at " + ", ".join(str(top) for top in average))
    if len(set(sizes)) > 1:
        print("advisory: the PDF's pages differ in size: each slice is its own page's width at the one scale")
    for page in range(1, len(sizes) + 1):
        if outcome[page][0] == "dropped":
            print(outcome[page][1])
    for number, (page, top, words) in enumerate(slices, start=1):
        print(f"page {number:03d}  PDF page {page}, {words}")

    os.makedirs(outdir, exist_ok=True)
    # pages.txt goes first and returns last, as the video road's.
    txt = os.path.join(outdir, PAGES_TXT)
    if os.path.exists(txt):
        os.remove(txt)
    first_slice = {}
    for number, (page, *_rest) in enumerate(slices, start=1):
        first_slice.setdefault(page, number)

    def cut_page(page):
        render = render_pdf_page(pdf, page, render_size[page])
        if render is None:
            raise GrabFailure(f"pdftoppm could not render PDF page {page}")
        tones = edge_tones(render)
        for number in range(first_slice[page], count + 1):
            if slices[number - 1][0] != page:
                break
            out = cut_slice(render, slices[number - 1][1], box_h, tones)
            Image.fromarray(out, "L").save(os.path.join(outdir, page_name(number, count)), "JPEG",
                                           quality=PAGE_JPEG_QUALITY, optimize=True)

    try:
        with ThreadPoolExecutor(GRAB_WORKERS) as pool:
            list(pool.map(cut_page, shown))
    except GrabFailure as err:
        print(f"FAIL: {err}; {PAGES_TXT} not written")
        return 1
    for name in stale_pages(outdir, count):
        os.remove(os.path.join(outdir, name))
    lines = [f"# src {os.path.basename(pdf)}"]
    if opts["url"] is not None:
        lines.append(f"# url {opts['url']}")
    lines.append("# rate 1")
    lines.append("# generated by tools/score_extract.py; frames at `rate`, begin inclusive, end exclusive")
    lines += [f"{i}|{i - 1}|{i}" for i in range(1, count + 1)]
    with open(txt + ".part", "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    os.replace(txt + ".part", txt)
    wav = os.path.join(outdir, AUDIO_WAV)
    removed = os.path.exists(wav)
    if removed:
        os.remove(wav)
    print(f"wrote {count} pages and {PAGES_TXT} to {outdir}"
          + (f"; removed the previous {AUDIO_WAV} (a PDF has no audio)" if removed else ""))
    return 0


def main():
    if len(sys.argv) < 3:
        print("REFUSE: usage: score_extract.py <video or PDF> <outdir> [--fit W:H] [--rate R] [--url LINK]")
        return 1
    video, outdir = sys.argv[1], sys.argv[2]
    try:
        opts = read_options(sys.argv[3:])
        if not os.path.isfile(video):
            raise Refusal(f"no video or PDF at {video!r}")
        if is_pdf(video):
            return pdf_main(video, outdir, opts)
        fps_text, fps, (vid_w, vid_h) = video_stream(video)
        page_size = fit_size(vid_w, vid_h, opts["fit"])
        stream_rate = audio_rate(video)
        has_audio = stream_rate is not None
        rate = opts["rate"] if opts["rate"] is not None else stream_rate
        if rate is None:
            raise Refusal("the video has no audio stream: --rate is required")
        # THE SCAN RATE IS THE VIDEO'S OWN (ffprobe's avg_frame_rate, verbatim into the fps filter, which keeps the
        # index -> time mapping honest on a variable-rate stream too): a page's edges are then the very frames a cut
        # falls between, and the analysis decodes' frame indices are the scan's. The decode dominates either way: the
        # whole K.550 scan took 2 min 0 s at 10 fps and about 2 min 20 s at 30.
        vf = f"fps={fps_text},scale={DIFF_W}:{DIFF_H}:flags=area"
        series, stamp, cached = load_or_scan(video, outdir, vf)
        statics, black, classes, nonmonotonic = detect(series)
        n = series.shape[1]
        size = canonical_size(vid_w, vid_h)
        lit = [(s, e) for (s, e), b in zip(statics, black) if not b]
        # THE PAGE TEST: one decode of each lit run's midpoint frame, the frame its page file is grabbed at.

        def midpoint_signature(run):
            (gray,) = decode_gray(video, fps, run[0] + (run[1] - run[0]) // 2, 1, size, series[1])
            return best_shear(ridge_mask(gray))

        with ThreadPoolExecutor(GRAB_WORKERS) as pool:
            signatures = list(pool.map(midpoint_signature, lit))
        pages, non_pages = group_pages(lit, signatures)
        if not pages:
            raise Refusal(f"no page: none of the {len(lit)} lit static runs carries bar lines")
        # THE GAPS: the frames between one page's last run and the next page's first, plus the frames before the first
        # page and after the last, each decoded once and read frame by frame (tile_gap).
        windows = [(i, pages[i - 1]["last"] + 1 if i > 0 else 0, pages[i]["first"] if i < len(pages) else n)
                   for i in range(len(pages) + 1)]
        gaps = [(i, a, b) for i, a, b in windows if b > a]

        def read_gap(gap):
            i, a, b = gap
            before, after = pages[i - 1] if i > 0 else None, pages[i] if i < len(pages) else None
            slopes = [p["slope"] for p in (before, after) if p is not None]
            frames = []
            for gray in decode_gray(video, fps, a, b - a, size, series[1]):
                mask = ridge_mask(gray)
                read = {slope: lines_of(sheared_runs(mask, slope)[1]) for slope in set(slopes)}
                frames.append((read[slopes[0]], read[slopes[-1]]))
            return tile_gap(frames, before["sig"] if before else None, after["sig"] if after else None)

        with ThreadPoolExecutor(GRAB_WORKERS) as pool:
            tiled = dict(zip([g[0] for g in gaps], pool.map(read_gap, gaps)))
    except (OptionError, Refusal) as err:
        print(f"REFUSE: {err}")
        return 1

    means = series[1]
    bounds = []                                  # each page's first frame, then the video's end
    taken = [[0, 0, 0, 0] for _ in pages]        # per page: left fill, left fade-in, right fade-out, right fill
    for i, a, _ in windows:
        offset, fade_out, fill, fade_in = tiled.get(i, (0, 0, 0, 0))
        if i < len(pages):
            bounds.append(a + offset)
            taken[i][1] = fade_in
        if i == 0:
            taken[0][0] = fill
        else:
            taken[i - 1][2:] = [fade_out, fill]
    bounds.append(n)
    assert bounds[0] == 0 and all(x < y for x, y in zip(bounds, bounds[1:])), "the ranges do not tile the video"

    def secs(frame):
        return Fraction(frame) / fps

    def span(s, e):
        return f"{spell_time(secs(s))}..{spell_time(secs(e + 1))}"

    print(f"scan: {n} frames at {fps_text} fps ({float(secs(n)):.3f} s){', cached' if cached else ''}; rate {rate}; "
          f"{len(lit)} lit runs, analysis at {size[0]}x{size[1]}, pages at {page_size[0]}x{page_size[1]}")
    for name in ("cut", "crossfade", "fade-out", "fade-in"):
        print(f"{name}s: {len(classes[name])}")
    if classes["within black"]:
        print(f"transitions within black: {len(classes['within black'])}")
    blacks = [(s, e) for (s, e), b in zip(statics, black) if b]
    print(f"black runs: {len(blacks)} ({float(sum(secs(e + 1 - s) for s, e in blacks)):.3f} s)"
          + "".join(f" {span(s, e)}" for s, e in blacks))
    for kind, a in nonmonotonic:
        print(f"advisory: the {kind} at {spell_time(secs(a))} is not monotonic")
    for s, e in non_pages:
        print(f"not a page: {span(s, e)} ({float(secs(e + 1 - s)):.3f} s, mean {means[s:e + 1].mean():.1f}), "
              "no bar lines")
    for i, page in enumerate(pages, start=1):
        for (s, e), sig, _ in page["runs"]:
            if (s, e) != page["principal"]:
                print(f"merged into page {i:03d}: {span(s, e)} ({float(secs(e + 1 - s)):.3f} s, mean "
                      f"{means[s:e + 1].mean():.1f}), shares {share(sig, page['sig'])} of {len(page['sig'])} lines")
    # THE GAPS' LINES: each fade's match depth is the scan's mean at its last (fade-out) or first (fade-in) frame
    # that still shows the page.
    for i, a, b in gaps:
        _, fade_out, fill, fade_in = tiled[i]
        parts = []
        if i > 0:
            parts.append(f"page {i:03d} fades out {fade_out}" + (f" to mean {means[a + fade_out - 1]:.1f}"
                                                                   if fade_out else ""))
        parts.append(f"fill {fill}")
        if i < len(pages):
            parts.append(f"page {i + 1:03d} fades in {fade_in}" + (f" from mean {means[b - fade_in]:.1f}"
                                                                    if fade_in else ""))
        print(f"gap {span(a, b - 1)} ({b - a} frame{'' if b - a == 1 else 's'}): " + ", ".join(parts))

    def audio_frame(frame):
        return round(secs(frame) * rate)

    ranges = [(audio_frame(x), audio_frame(y)) for x, y in zip(bounds, bounds[1:])]
    for i, (page, (b, x)) in enumerate(zip(pages, ranges), start=1):
        s, e = page["principal"]
        fill_in, fade_in, fade_out, fill_out = taken[i - 1]
        runs = len(page["runs"])
        print(f"page {i:03d}  {b}..{x}  run {span(s, e)} ({float(secs(e + 1 - s)):.3f} s"
              + (f", {runs} runs {span(page['first'], page['last'])}" if runs > 1 else "")
              + f"); left fill {fill_in} fade {fade_in}, right fade {fade_out} fill {fill_out}")

    os.makedirs(outdir, exist_ok=True)
    if not cached:
        np.save(os.path.join(outdir, SCAN_NPY), series)
        with open(os.path.join(outdir, SCAN_META), "w") as f:
            f.write(stamp)
    # pages.txt goes first and returns last, so a run that fails mid-grab leaves no list naming its half-written pages.
    txt = os.path.join(outdir, PAGES_TXT)
    if os.path.exists(txt):
        os.remove(txt)
    count = len(pages)
    # THE PAGE FILE is grabbed at the midpoint of the page's PRINCIPAL run (group_pages), the page test's own frame:
    # a frame of a static run, never of a transition, whatever the page's range takes around it.
    jobs = [(s + (e - s) // 2, os.path.join(outdir, page_name(i, count)))
            for i, (s, e) in enumerate((page["principal"] for page in pages), start=1)]
    try:
        with ThreadPoolExecutor(GRAB_WORKERS) as pool:
            list(pool.map(lambda job: grab_page(video, fps, job[0], page_size, job[1]), jobs))
    except GrabFailure as err:
        print(f"FAIL: {err}; {PAGES_TXT} not written")
        return 1
    for name in stale_pages(outdir, count):
        os.remove(os.path.join(outdir, name))
    lines = [f"# src {os.path.basename(video)}"]
    if opts["url"] is not None:
        lines.append(f"# url {opts['url']}")
    lines.append(f"# rate {rate}")
    lines.append("# generated by tools/score_extract.py; frames at `rate`, begin inclusive, end exclusive")
    lines += [f"{i}|{b}|{e}" for i, (b, e) in enumerate(ranges, start=1)]
    with open(txt + ".part", "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    os.replace(txt + ".part", txt)

    # THE SOUNDTRACK goes last, on every run: the score's audio is the video's (architect 2026-10-26 night). A video
    # with no audio stream (the --rate road) has none to write, AND A PREVIOUS VIDEO'S audio.wav IN THE FOLDER IS
    # REMOVED (architect 2026-10-27): the viewer would otherwise play another video's music against these pages.
    wav = os.path.join(outdir, AUDIO_WAV)
    removed = False
    if has_audio:
        status = subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", video, "-vn", "-map", "0:a:0",
                                 "-c:a", "pcm_s16le", "-ac", "2", "-f", "wav", wav]).returncode
        if status != 0:
            print(f"FAIL: ffmpeg could not extract the audio (exit {status}); the pages are written")
            return 1
    elif os.path.exists(wav):
        os.remove(wav)
        removed = True
    print(f"wrote {count} pages, {PAGES_TXT}" + (f" and {AUDIO_WAV}" if has_audio else "") + f" to {outdir}"
          + (f"; removed the previous {AUDIO_WAV} (this video has no audio)" if removed else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
