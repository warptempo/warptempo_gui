warptempo_gui is a custom phase vocoder application for time-warping
classical orchestral recordings toward target tempos. The engine is
Laroche-Dolson identity phase-locking (defaults: N=4096, R_s=1024,
fs=44100) wrapped in a Cairo/Wayland GUI for marker authoring, tempo
specification, and audition.

This project was built because no existing tool covered the niche.
Commercial DAWs assume a metronome-driven timebase with a click track;
classical orchestral recording is free-tempo with no underlying grid.
Existing time-stretch libraries automate transient-smear mitigation,
which is the right tradeoff for material with strong percussive
attacks but introduces coloration and artifacts on orchestral material
where transient density is high and irregular. warptempo_gui takes the
manual route: hand-authored phase reset markers placed where the user
decides phase realignment is wanted, with no automatic transient
detection in the rendering path. The warp marker model supports
cross-referenced section tempos via a label cascade so recapitulated
material across a sonata-form movement can be tempo-locked to its
exposition counterpart by a single edit.

warptempo_gui supports other time-stretch engines via timemap and
tempomap outputs. Export output_format=timemap to drive any
time-stretch library that accepts a sample-domain warp map — Rubber
Band reads timemap files directly, and the adapters/ directory holds
wrappers that feed the same timemap into Signalsmith Stretch,
SoundTouch, and Bungee. Export output_format=tempomap to drive
professional algorithms inside a DAW host — the tempomap exports as
MIDI tempo events readable by Ableton Live, REAPER, etc. (and by
extension zplane Élastique, Avid X-Form, and similar host-provided
engines).

The examples directory contains the working corpus: the 1972 Krips /
Royal Concertgebouw Mozart symphony recordings (Decca Eloquence 2024
remaster) warped toward Hummel-published metronome marks. The Symphony
No. 40 mvt I directory (examples/550 - 1/) is the reference example —
it exercises every form and syntax used in the project (label
cascade, phase reset markers, two-decimal base_tempo with six-decimal
scale fine-tune).

Linux + Wayland (JACK audio). Build instructions, conceptual model,
hotkey reference, file formats, and troubleshooting are in
docs/HELP.md.

Licensed GPL v3. See LICENSE.
