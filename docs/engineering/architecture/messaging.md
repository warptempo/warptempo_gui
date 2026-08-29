# Messaging — the notification cards (architect design 2026-08-29)

This file is the ruling for the CARD half of the messaging redesign; the STATUS BAR half (the window's last row, which takes the status chain's three STATE strings) is the redesign's second part and is recorded here when it lands. Until then the state strings — the `h` walk's line, the render's progress line, the selected marker's readout — stay on the tab row's status chain (`paint_status_chain`, the 2026-08-13 record at the end of `kdenlive-redesign.md`), which ranks THREE tiers since 2026-08-29 and paints no chip. The design record with his words is `tmp/notifications_design.md`; the mockups he picked from are `tmp/previous/messaging_mockups/` (`cards_AB.png`'s look 1 is the card).

## The split: state and events

Every message the product emits is one of two things. STATE is what is true right now, replaced as it changes, never timed out — the progress line, the walk line, the readout; it lives on the chain (the bar, in part 2). An EVENT is something that happened: an act was answered with a sentence, or something the user was not watching finished badly. Events are NOTIFICATION CARDS. The architect is "not a big fan of notifications": the cards are minimal, and the inventory below is closed.

## The two classes

- NORMAL — a small card that leaves on its own `kNotificationMs` (5000 ms, `gui_input.h`, beside `kHoldBeatMs` and deliberately NOT derived from it: a card's life is neither a hold nor a second tap, and Plasma's own default is what it takes; a duration, so it rides no scale) after it became VISIBLE, or at its X. The pointer resting on it PAUSES the clock (the remaining life is banked at hover-enter and re-armed at hover-leave; the pointer-left hook ends a hover too; glass has no hover and no finger ever holds a clock).
- CRITICAL — stands until its X is pressed. No clock, and nothing that comes after takes it down: a later success raises nothing and clears nothing. The four checkpoint outcomes (`Checkpoint failed: nothing was committed` / `Checkpoint failed: files written but not committed` / `Checkpoint could not be confirmed` / `Checkpoint committed; push failed`) are its only producers.

No remaining-time bar, no actions, no title/body split, no sound. The glyph is what tells the classes apart at a glance: `dialog-information` on a normal card, `dialog-error` on a critical one.

## The look and the placement

The stack sits top-right, `kPanelPadPx`-class air (the folder overlay's `pad_px`) below ROW 1 (the menu row, whose right-floating view bar carries the S+W / T+P / T+W radios), right-aligned at `icon_row_pad_x()` from the window's edge, newest on top, growing DOWN over whatever lies there (the tab row's right stretch, the icon row's empty right, the thin lanes, the waveform). At most `kNotificationVisibleMax` (3) cards are visible; older ones QUEUE and surface as visible ones leave, a queued normal card's clock starting when it surfaces, not at its push. A stack that would reach the bottom strip is contrived ("we don't cater to that"): three is the cap and nothing clamps further; on the tablet's 1024 logical px it is cramped, as expected.

Every constant a card reads, none of its own but two (`notifications.h`):
- height = `icon_row_content_h_px()` — the icon row's 46: the 32 px button box plus its 7 px margins;
- the gap between cards = `kIconBtnGapPx` (2);
- the horizontal pad = `icon_row_pad_x()` (8); the glyph and the X each sit in a `kIconBtnPx` (32) box at that pad, the `kIconGlyphPx` (22) glyph at the box's own `(32-22)/2` inset; the gap between the glyph's box and the text, and the text and the X's box, is the folder overlay's icon-to-name gap `kRowIconGapPx` (8);
- width = the content's (two pads, two boxes, two gaps, the shaped text) clamped to [`kNotificationMinWidthPx` 240 authored px, a third of the window]; on a window narrower than three floors the floor wins;
- ground `kModalFieldGround` #141618 (the player's and the picker's band ground — "that colour stands out"; the dropdown's ground was rejected as "what's already in use throughout") under a 1 px `kRedesignTabLine` #4c4e51 outline (the popup's own border), through the one popup box painter `paint_popup_chrome` at the popup's radius;
- ink `kRedesignLabel`, one line of the one sans at the redesign's size, vertically centred through `redesign_baseline`; a sentence longer than the text's room CLIPS at the run's right edge with a cairo clip — no wrap, no ellipsis (the folder overlay rows' precedent);
- the X wears the icon button's hover face — the 1 px `kRedesignAccent` outline through `redesign_face_box`, the icon row's own — while the pointer is in its box; the card's body wears no hover face.

The three glyphs are fresh verbatim transcriptions into the roster (`icons.h`, `kIconCount` 54): `status/22/dialog-information` and `status/22/dialog-error` (symlinks in Breeze Dark onto `data-information` / `data-error`, copied resolved into `assets/icons/breeze/`) and `actions/22/window-close`. The two dialog files are TWO-COLOUR — a rounded plate in a scheme class (`.ColorScheme-Accent` #3daee9, `.ColorScheme-NegativeText` #da4453: the roster's existing `kIconAccent` and `kIconNegativeText`) under a glyph in the literal `#fff` — and the roster paints each path in the table's own ink, so the card's painter colours nothing: the plate is Breeze's own blue or red. Their plates are `<rect rx="2">` elements and take the table's four-number derivation (the `<rect>` precedent tool-rect-selection set), the glyph paths verbatim.

## The layer

Cards paint in `on_redraw`'s step 13, AFTER the flag editor's box and BEFORE the dropdown — above every lane, the waveform, the keyboard slot and the folder overlay's band, BELOW the three surfaces that stay topmost: the dropdown and the tooltip (the two pointer-transient floaters) and the modal row. One painter, `paint_notifications`, unconditional on every frame for the floating surfaces' reason: it PUBLISHES each visible card's rect and X box (`AppState::Notifications::painted`), and a skipped run would strand a stale publication.

## The hit

THE X, AND ONLY THE X, DISMISSES A CARD (architect 2026-08-29, superseding the design's tap-anywhere). A press anywhere on a visible card is consumed whole — arms nothing, moves nothing, lands no playhead, opens no drag, reaches nothing underneath — and the LEFT press on the card's X box dismisses it; any other button over a card is consumed in the veil's own manner. ONE RULE ON BOTH BACKENDS: the router cannot fork on tap versus click (no origin bit rides a press — `GuiInputState` carries modifiers alone), so the rule must be one, and the X's box is the icon row's 32 px button box, already the product's glass target, which is why no finger-fattened body target exists.

The claim sits at `on_button_press`'s head, after the tooltip hide and the menu row's disarm (a press anywhere still does both) and ABOVE EVERY VEIL — the on-screen keyboard's claim, the prompt's, the folder overlay's, the player's, the picker's, the dialog editors' — because a card must be dismissable under any modal. It is not an exception to the veil: a card is not a reach into the veiled surface, it is the message about the act the veil stands over (the record at the retired reach-through's site, `input_pointer.cpp`). The press is the act (content acts the moment its identity is certain; a card press can mean one thing), so the release owes nothing and no arm is left standing.

The geometry a card is HIT by is the geometry it was PAINTED with: the claim, the cursor map and the hover walk read the publication and then ask the live stack whether the id still stands (published geometry may only SELECT; live state decides — `ModalDialogGeometry`'s doctrine), so a card that expired between paint and press lands nothing and is never dismissed twice. A card under the open dropdown's box yields to it (the one pointer-owning surface that paints above the cards), in the one owner `notification_card_at`.

- CURSOR: the zone map's arm 0 — the Arrow over a card, whatever lies under it (the card is opaque to the pointer).
- MOTION: the hover is re-derived at the head of the motion handler, above every branch, like the menu row's exit; the X's face is the only thing it paints.
- TOUCH: the one-finger translation delivers the press only once the disambiguation window has resolved to the pointer (a tap, or an off-zone hold or crossing), which is what keeps a two-finger landing off it; and the pan zone answers FALSE on a card (`touch_point_in_pan_zone`), so a finger landing on one resolves to the pointer translation and reaches the claim rather than becoming the phone-model pan.

## The clock and the damage

The clock rides the run loop's existing deadline tick, polled: `GuiNotifications::fire_if_due` in `main.cpp`'s `on_tick`, beside the A/B audition's rest sampler and ABOVE the playing-only guard (an idle window still retires an expired card). The tick's software deadlines are FOUR — key repeat and the touch disambiguation window (`GuiInputCore::tick`), the audition's rests, the cards — and none is scheduled; all read `monotonic_ms()`. The same call re-answers the hover from the remembered pointer, so a card that slid up under a resting pointer starts pausing without a motion.

Damage has one owner, `Viewport::invalidate_notification_stack`: every stack change (a push, the X, an expiry) damages the stack's BOUND — `notification_stack_bound`, the rect three cards of the maximum width can ever occupy — which erases what stood and admits what comes without shaping a glyph off the paint clock; the hover face takes `invalidate_rect` on the X box alone.

## The chokepoint, the dedup, the queue

`GuiNotifications::notify` is the ONE writer of a card; every producer below calls it and nothing else pushes. A text identical to a VISIBLE card's of the same class does not stack a duplicate: a normal duplicate re-arms that card's clock (banked if the pointer rests on it), a critical duplicate is a no-op. A push on a full stack demotes the third card into the queue, where its clock forgets what had run and starts afresh when it surfaces. The stack is a member of the per-project `AppState`, so a reopen starts with an empty stack by construction — the chip's own per-project scope, and no clear site exists because none is needed.

## What is notified, and what is not

NORMAL cards, every one a sentence answering an act or reporting a background act's outcome (the `notify` callers, re-grepped 2026-08-29):
- the three phase-reset pastes' `Stopped at …` reports (`phase_reset_propagate.cpp`) and the measure paste's;
- `History is unavailable` — the commit-title editor's Enter into a mode that closed, and the failed-scan arrival that closes the view under the user (with the store's reason appended);
- the `h` view's `'` load refusals, `Load in place refused: …` (the strict sidecar load, the past-EOF wall, the Local walk's number) and `Revert refused: …` — stderr-only until 2026-08-29;
- the Open project picker's three refusals (`A checkpoint is still publishing; try again when it finishes`, the project model's sentence, the dry run's reason), the picker staying open;
- Synchronize's refusals (`No removable volume mounted`, the several-volumes list, `Synchronization already running`), its worker's every failure and its `Synchronized N files to …` count — the `Synchronizing to …` dispatch notice is DROPPED (a process line is state, and the verdict follows within seconds);
- the render player's `No renders to play`, `Only batch renders load in place`, `Load refused`, `Not the source's rate and channel count`, `Empty wav`, `No audio device` and the frozen reader's own words (`GuiRenderPlayer::status`, the thin road);
- `Target render failed` (the preview's failure is an event the user was not watching; a cancelled preview is the product's own doing and says nothing).

CRITICAL cards: the four checkpoint outcomes and nothing else.

NOT a card, by ruling:
- a render's completion ("that would get annoying");
- every deliberately silent refusal — the consumed no-ops: the strict-modifier no-ops, the off-home drops, `m` on a bad run, bare `h` with a checkpoint publishing, and the player's load under a running render (`render_player_load_in_place`; re-decided 2026-08-29 on its own merits: the progress line is the explanation and a card would restate what the screen shows — `validation_topology.md`'s row);
- the loader's fatal exits (the adversarial class: stderr and exit 1);
- every QUESTION — the prompts and the dialog editors;
- the peaks-cache rebuild lines (`audio.cpp`, stderr; they self-heal);
- what is TRUE NOW — the three state strings.

## The basename rule

A message that carries a filesystem path names the FILE — its basename, or the batch cell's `N_tag/NN` id — never a full path: the subject is the highlighted row on screen, and the clip is the backstop. Synchronize's sentences name their path RELATIVE TO THE MIRROR'S TWO ROOTS (`<volume name>/<path under the volume>` on the stick, the path under the project folder in the project — the one composer `shown`, `external_sync.cpp`; the full path stays on stderr), which is `external_sync.h`'s rule 1's naming clause. The picker's reasons and the load refusals already name folder names and repo-relative sidecar paths.

## What retired, and why

- `AppState::transient_status_message`, the TRANSIENT class — hidden under a progress line, revealed stale when it cleared, wiped by the next key press, and invisible altogether under the `h` view's top-tier line; its clear at `on_key`'s head, its tier in `paint_status_chain`, its "no writer may stamp under a progress line" discipline and every `invalidate_status_chain_area` call that served it alone. A card cannot be hidden, so "it would be invisible" is no longer a reason for silence, and the gates that refused silently on that ground were re-decided each on its own merits (the picker's three and Synchronize's reports in the `h` view: cards; the player's load under a running render: still silent, for the progress line).
- `AppState::critical_error_message`, the PERMANENT CRITICAL CHIP — the tab row's paint-only red box at the chain's left (2026-08-09 to 2026-08-29), its painter, its re-derived box, its one clearing route (a later established success) and its fill-only-an-empty-slot condition. The chip was a SLOT holding the repository's last answer; a card is an EVENT the user closes once read.
- `status_chain_pad_x`'s second use (the chip-to-text gap); `paint_status_chain` ranks three.
