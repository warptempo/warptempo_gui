# warptempo_gui

`warptempo_gui` is a phase-vocoder application for transparent time-warping of recorded music under full manual control. The engine is a faithful, centered implementation of Prusa–Holighaus phase-gradient heap integration (PGHI — "Phase Vocoder Done Right"), wrapped in a Cairo/Wayland GUI for placing warp markers, specifying tempos, and auditioning the result over JACK.

It was built for one job: warping classical orchestral recordings toward historically informed tempos. Commercial DAWs assume a metronome-driven timebase with a click track; classical recording is free-tempo, with no underlying grid. Existing time-stretch libraries automate transient handling; here both concerns stay in the operator's hands. The stretch is applied locally through warp markers to follow a continuously varying tempo, and phase-reset markers are placed one by one, so the operator chooses the tradeoff between transient impact and the discontinuity each reset introduces. Because the stretch is local and section-scoped, sections can be tempo-locked to one another by name through a label cascade — recapitulated material across a sonata-form movement stays tied to its exposition counterpart through every subsequent edit.

The simplest use — one constant stretch ratio for a whole file — needs no marker work at all: the ratio is set through the `scale` setting (a full-precision double in `[0.5, 2]`), and warp markers enter the picture only when you want phrase-level tempo variation.

Renders are finished 24-bit PCM WAV deliverables.

## Building and running

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
./build/warptempo_gui "path/to/source.wav"
```

The GUI targets Linux with a Wayland compositor and JACK audio. A headless render CLI (`-DWARPTEMPO_BUILD_CLI=ON`) builds and runs without Wayland or JACK, including under WSL2, and renders byte-identically to the GUI.

Dependencies, build options, the conceptual model, the file formats, and the complete hotkey reference are all in [`docs/HELP.md`](docs/HELP.md) — the guided tour, and the only guide you need.

## Examples

The `examples/` directory contains the working corpus: the 1972 Krips / Royal Concertgebouw Mozart symphony recordings (Christopher Bernauer's 2024 remaster for Decca Eloquence), warped toward the metronome marks Hummel published. The Symphony No. 40 first movement (`examples/550 - 1/`) is the reference project — it exercises every form and syntax the project uses, including the label cascade, phase-reset markers, and two-decimal tempos fine-tuned with the full-precision scale.

Example output, in lossy audio format:

[Symphony No. 40](https://www.youtube.com/playlist?list=PLm5sJJQZOLT1OkUITQ4vX2l20qzGylkqI)

## License

GPL v3. See `LICENSE`.
