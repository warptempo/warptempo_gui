# tools/mockup — sheets onto the tablet's Gallery

`push.sh` puts design sheets where the architect judges them: the tablet's `/sdcard/Download`, which Samsung Gallery
shows byte for byte when a PNG carries the Display-P3 iCCP chunk and nothing else (the app's Android window is a
Display-P3 layer, so a sheet's bytes are what the glass shows). The planner runs it; the coder never does.

```
tools/mockup/push.sh <png...>            # each file in name order: pushed, touched, announced to the media
                                         # scanner, two seconds apart, so Gallery lists them in their names' order
tools/mockup/push.sh --delete <name...>  # the superseded sheets (base names) off the tablet, the scanner told
```

The mock-up renderer that lived here retired on 2026-10-07 with the palette mock tool it was built on (both stay in
the git history); the sheets since the Clearlooks arc are ad-hoc numpy scripts in `tmp/`, drawn over his tablet
captures.
