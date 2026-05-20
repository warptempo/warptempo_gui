warptempo_gui is a custom phase vocoder application for time-warping
classical orchestral recordings toward target tempos. The engine is
Laroche-Dolson identity phase-locking (defaults: N=4096, R_s=1024)
wrapped in a Cairo/Wayland GUI for marker authoring, tempo
specification, and audition.

Commercial DAWs assume a metronome-driven timebase with a click track;
classical orchestral recording is free-tempo with no underlying
grid. Existing time-stretch libraries automate transient-smear
mitigation, which is the right compromise between convenience
and accuracy for material with regularly-spaced, easily detectable
percussive attacks but which tends to either under- or over-emphasize
transients in orchestral material where transient localization is too
complex for any single existing algorithm to address. warptempo_gui
addresses both problems: time-stretch is applied locally to accommodate
continuously varying tempo, and phase reset markers are placed
individually so the user can precisely control the tradeoff between
transient impact and the discontinuity each reset introduces. Because
the stretch is local and section-scoped, sections can be tempo-locked
to one another by name through a label cascade — recapitulated
material across a sonata-form movement can be tied to its exposition
counterpart by a single edit.

warptempo_gui supports other time-stretch engines via timemap and
tempomap outputs. Export output_format=timemap to drive any time-stretch
library that accepts a sample-domain warp map — Rubber Band reads
timemap files directly, and the adapters/ directory holds wrappers
that feed the same timemap into Signalsmith Stretch, SoundTouch, and
Bungee. Export output_format=tempomap to drive professional algorithms
inside a DAW host — the tempomap is exported as MIDI tempo events
readable by Ableton Live, Logic Pro, REAPER, etc., driving the host's
bundled stretch engines (zplane Élastique, Zynaptiq ZTX) and VST
stretch plugins that follow the host's tempo automation (Serato Sample /
Pitch 'n Time). Note: REAPER works but its tempo-map engine has lower
timing resolution than is needed for precise tempo manipulation.

The examples directory contains the working corpus: the 1972 Krips
/ Royal Concertgebouw Mozart symphony recordings (Decca Eloquence
2024 remaster) warped toward Hummel-published metronome marks. The
Symphony No. 40 mvt I directory (examples/550 - 1/) is the reference
example — it exercises every form and syntax used in the project
(label cascade, phase reset markers, two-decimal base_tempo with
six-decimal scale fine-tune).

Linux + Wayland (JACK audio). Build instructions, conceptual model,
hotkey reference, file formats, and troubleshooting are in docs/HELP.md.

Licensed GPL v3. See LICENSE.
