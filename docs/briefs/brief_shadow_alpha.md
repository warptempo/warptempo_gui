# BRIEF: the cards' drop shadow is 0.4 at the edge, not 0.8 — the rings' alpha matched the mock's number, not the mock's look (architect 2026-10-02, on the glass: "way too dark, but in a good direction; small and tight, which is good")

Model: Opus, effort high. You are THE CODER: edit source only, build (`cmake --build build -j$(nproc)`) to self-check, read-only git only, no commits, no GUI launch, no CLAUDE.md edits. One constant and its comment.

## WHY 0.4
The S4 mock (tmp/palette/render_cards2.py) blurred a 0.8-alpha rect with a ~2 px gaussian: at the rect's EDGE a gaussian-blurred step reads HALF its interior value, so the mock's darkest visible pixel beside the card was ~0.4, decaying outward. The painted rings put the full 0.8 at the innermost ring, twice the mock's edge — hence "way too dark". 0.4 at the innermost ring, falling linearly to 0 across the spread, reproduces the mock's edge darkness with the same tight extent.

## WHAT
`kNotificationShadowAlpha` (render.h, the shadow block beside the playhead's alpha): 0.8 → 0.4. Retell its comment with the reasoning above in two sentences (the rings put the whole number at the edge where the mock's blur put half of it) and his words. If the comment or closed_questions.md's line names 0.8, update it. Nothing else changes.

## REPORT
The diff, the build's last lines verbatim.
