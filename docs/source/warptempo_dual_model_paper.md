# A Dual-Model Phase Vocoder for Transparent Time-Warping under Manual Control

**warptempo**

*Engineering note: the perceptual findings are one expert listener's judgments, reported as such. Claims are stated at the scope where they hold.*

---

## Abstract

We describe a phase vocoder for high-ratio time-warping of recorded music under manual control, in which audible artifacts are minimized and contained rather than absent in principle. The central engineering result is that **neither classical Laroche-Dolson identity phase-locking nor Prusa-Holighaus phase-gradient heap integration is, on its own, sufficient** — each has a distinct failure mode the other handles well — but that the two, made selectable per segment and seeded identically at shared phase-reset points, together produce output more transparent than any general-purpose time-stretching tool we evaluated. The complementary failure modes are a property of the two models rather than of any particular material; the demonstrations here use the 2024 Christopher Bernauer / Decca Eloquence remasters of the 1972 Krips Mozart symphony cycle, warped toward historically-informed tempos. Orchestral material is chosen because it is the most demanding masking environment for this class of algorithm — exposed lines, natural-acoustic recording, large hall — and a tool that handles it transparently has headroom for genres with denser masking. The source is best-in-class recording quality, which sharpens rather than weakens the transparency test: there is detail worth preserving, and the criterion is whether it survives the warp. We also report two transcription hazards encountered in moving the gradient-integration method from its published phase convention into a standard un-fftshifted STFT, and the single symmetry principle that explains and prevents both. The perceptual findings are one expert listener's judgments accumulated over many iterations, not a formal listening study.

---

## 1. Motivation

The task is high-ratio time-warping of recorded music in which the audible artifacts introduced by the algorithm are minimal and perceptually contained — the output should differ from the source only in tempo, to the extent that the constraints of the method permit. Phase resets are themselves artifacts by construction (the synthesis phase is discontinuously re-seated to the analysis phase at each marker); the discipline is to place them where the surrounding material masks them. The work was developed on the 2024 Christopher Bernauer remasters (Decca Eloquence) of the 1972 Krips Mozart symphony cycle, warped toward historically-informed tempos. That material is used for the demonstrations below; the source is widely regarded as exemplary recording quality, and the 2024 remasters are best-in-class restorations of it. We name this because the transparency criterion is more demanding on high-quality source: there is more detail to preserve, and the warp has less room to hide.

The choice of orchestral material is not arbitrary: it is the most demanding masking environment for phase-vocoder time-warping. The size of a symphony orchestra and the natural-acoustic recording technique typical of the genre — sparse microphone counts, large hall acoustics, minimal close-miking — preserve substantial silence and air around individual instrumental lines and capture extended exposed solo passages, both of which give algorithmic artifacts maximal room to be heard. Genres with denser sustained spectra and more pervasive percussive activity (rock, pop, electronic dance music) provide stronger masking; the author's two decades of producer practice on such material support the expectation that the technique generalizes favorably to those genres. We make this expectation explicit here and present it as such — practitioner judgment, not formal evaluation — because the asymmetry runs in the direction that strengthens the claim: a tool that handles the most artifact-revealing case transparently has headroom for material that masks more, not less. The technique is therefore not specific to orchestral material — the complementary-failure-mode result in Section 6 is a property of the two phase-correction models, not of the repertoire. What *is* specific is the workflow: the operator authors phase-reset markers by hand, which is manual work (less of it than one might expect — see Section 2 — but manual). The method is therefore offline and operator-driven rather than real-time and automatic, and that is the only constraint on where it applies.

This separates it from general-purpose time-stretchers — the open-source and commercial tools surveyed in Section 6 — which are engineered for real-time operation, arbitrary input, formant-preserving vocals, low latency, and zero-configuration robustness on material the developer has never heard. Those properties cost transparency. A tool that spends that budget on transparency instead, accepting offline operation and manual authoring, is the natural design when the goal is fidelity rather than throughput.

The classical phase vocoder's difficulty is well known and is laid out in detail by Laroche and Dolson [1]: time-scaling by using different analysis and synthesis hops requires correcting the STFT phase, and naive correction destroys *vertical* phase coherence — the phase relationship across frequency bins within a single frame — producing the family of artifacts collectively called phasiness (reverberation, loss of presence, transient smearing). Laroche-Dolson's identity phase-locking restores vertical coherence by picking spectral peaks and locking each peak's region of influence to the peak's phase. Prusa and Holighaus [2] take a different route: they estimate *both* partial derivatives of the STFT phase — in time and in frequency — and integrate them adaptively, enforcing horizontal and vertical coherence without any peak-picking or transient detection.

Both methods work. The problem this paper addresses is that on our material, each one works *in a different place*, and the difference is large enough to hear.

## 2. Architecture

The engine is an in-process C++ Laroche-Dolson identity phase-locking phase vocoder. The signal path is: analysis STFT → per-frame phase correction → overlap-add resynthesis → native inline peak limiter.

**Fixed synthesis hop, variable analysis hop.** The synthesis hop is fixed at $R_s = N/4$. The analysis hop is computed per-frame from the local stretch ratio, $R_a = R_s / \alpha$, so the time grid the operator authors is realized by *varying where we read* rather than where we write. This is the right way around for a warp: the output is laid down on a uniform grid, and tempo variation lives entirely in the analysis-side sampling. The phase-derivative estimates accommodate the variable hop directly (Section 3); a tempo change across a warp marker needs no special-casing, because each finite difference is normalized by the actual local hop on its own side.

**FFT size.** `N = 4096`. The dual-model split is what makes this size viable. Under Laroche-Dolson alone, a window this large smears transients and vertical structure, and the engine had to run at a smaller `N = 2560` to hold attacks and high-frequency detail together. Once the heap model carries the dense material — where the larger window's finer frequency resolution is an asset rather than a liability — and the peak model is retained per-segment for the exposed and transient material, the larger global `N` becomes the better default. The size choice and the model split are not independent decisions; the split is the precondition for the size.

**Two orthogonal marker collections.** The operator authors two independent layers.

*Warp markers* shape the timemap — they set and inherit tempo, lock tempo across recapitulated material via labels, and define the entire stretch trajectory. This layer is where the musical expertise lives when the work calls for non-constant tempo, and it is deliberately not the subject of this paper. The classical phase-vocoder use case — one constant stretch ratio for an entire file — requires no warp-marker interaction at all: a single mandatory warp marker is present at the file's first frame with tempo $1.00$, and the global stretch ratio is set entirely through a settings field (`scale`) with six-decimal precision. The marker need not be touched. Warp markers become an authoring surface only when the operator wants phrase-level tempo variation; in the classical-PV sense, the timemap demands zero expertise. The expertise the tool demands, when it demands any, is concentrated in this warp layer on the question of *where the tempo should go*.

*Phase-reset markers* are the subject of this paper. Each is anchored to a transient and carries three fields: a time, a disabled flag, and a **mode**. At a phase-reset marker the synthesis phase is re-seated to the analysis phase ($\theta = \varphi$), discarding accumulated phase history — a phase-only operation that never alters the stretch, which remains the timemap's job everywhere. The mode field selects, for the *segment* of audio governed by that marker, which phase-propagation model runs:

- **peak** — Laroche-Dolson identity phase-locking. The historical engine and the global default.
- **heap** — Prusa-Holighaus phase-gradient heap integration (PGHI).
- **pass** — inherit the mode of the previous mode-owning marker (mirroring the way warp markers inherit tempo), resolving by a backward walk to a concrete peak/heap before the engine runs; peak when nothing precedes.

This layer demands far less expertise than the timemap. Phase-reset placement is locally scoped and transient-anchored — the markers go roughly where the onsets are — and under the heap model it tolerates *sparser* placement than peak did, because PGHI enforces vertical coherence without needing a reset to re-establish it as often. The tool pushes the operator's effort toward the musical decision and absorbs the signal-processing decision into the engine.

## 3. The two derivative axes and the symmetry principle

This is the conceptual core, and it is a single idea applied twice.

### 3.1 Notation

Following Prusa-Holighaus [2], write the discrete STFT phase as $\varphi(m, n)$ for frequency bin $m$ and frame $n$, and the principal-argument (phase-unwrapping) operator as

$$[x]_{2\pi} = x - 2\pi \cdot \operatorname{round}\!\left(\frac{x}{2\pi}\right)$$

i.e. the wrap of $x$ into $(-\pi, \pi]$. Let $R_a$ be the analysis hop and $R_s$ the synthesis hop. Both papers build the corrected synthesis phase by *differentiating* the analysis phase along an axis, unwrapping the difference, and *integrating* the result on the synthesis side.

### 3.2 The time axis (Laroche-Dolson, and the time half of PGHI)

The classical phase-vocoder time derivative — the instantaneous-frequency estimate — is, per Laroche-Dolson [1] and in the centered form preferred by Prusa-Holighaus, computed by removing the *expected* linear phase advance of bin $m$ before unwrapping, then adding it back:

$$(\Delta_t \varphi)(m, n) = \omega_m + \frac{\big[\, \varphi(m, n) - \varphi(m, n{-}1) - \omega_m R_a \,\big]_{2\pi}}{R_a}, \qquad \omega_m = \frac{2\pi m}{N}$$

The term $\omega_m R_a$ is the phase a stationary sinusoid at bin $m$'s center frequency would accrue over one analysis hop. Subtracting it before $[\,\cdot\,]_{2\pi}$ is what makes the unwrap meaningful — it removes the bulk rotation so the principal argument captures only the small deviation from the bin's nominal frequency — and adding $\omega_m$ back afterward restores the true instantaneous frequency. **Subtract the expected progression to unwrap; re-add it to reconstruct.** In the engine, the centered estimate normalizes each half-difference by its own actual hop ($R_a$ backward, $R_a$ forward), which is exactly what lets a stretch change across a warp marker pass through with no special handling.

### 3.3 The frequency axis (the PGHI addition)

The classical PV discards the frequency-direction derivative entirely, which is why it fails on anything but stationary sinusoids: a chirp or an impulse has real, non-negligible vertical phase structure that the time axis alone cannot see. PGHI restores it. The frequency derivative is the structurally identical operation rotated ninety degrees — difference across bins, unwrap, with the expected per-bin progression removed first:

$$(\Delta_f \varphi)(m, n) = \frac{\big[\, \varphi(m{+}1, n) - \varphi(m, n) - \texttt{expected\_f} \,\big]_{2\pi}}{b_a}$$

The subtlety, and the source of both hazards in Section 4, is the value of `expected_f`. In Prusa-Holighaus's phase convention the analysis window is centered such that this term vanishes, and a literal reading of the published frequency-difference formula has no expected-progression term at all. But a standard STFT implementation that applies the window *un-shifted* over $[0, N)$ — i.e. centered near sample $n_c = (N-1)/2$ rather than at the origin — carries a linear-in-bin group-delay term on the analysis phase of exactly

$$\texttt{expected\_f} = \frac{2\pi\, n_c}{N} = \frac{2\pi}{N}\cdot\frac{N-1}{2}$$

per one-bin step. This is the precise frequency-axis analogue of $\omega_m R_a$ on the time axis. It must be subtracted before $[\,\cdot\,]_{2\pi}$ (to unwrap) and re-added after (to reconstruct), for the same reason and with the same structure.

### 3.4 The symmetry principle

> **Both phase-derivative axes must subtract their expected progression before the principal argument, and re-add it after.** Time subtracts $\omega_m R_a$ and re-adds $\omega_m$; frequency subtracts `expected_f` and re-adds `expected_f`.

Breaking the symmetry on the time axis gives nonsense — nobody does it, because the classical PV makes the time-axis handling explicit and unavoidable. Breaking it on the frequency axis is easy to do by accident, precisely because the published convention hides the term, and it produces the two hazards below. The principle is the thing to preserve if anyone ever touches the derivative code: each axis is a demodulate-unwrap-remodulate sandwich, and the expected progression is the bread on both sides.

### 3.5 Integration: the heap

Given both derivatives, PGHI integrates them into a synthesis phase by a magnitude-prioritized flood fill. A max-heap holds bins with already-assigned phase as candidates to propagate *from*. The loudest available bin is popped; if it is a previous-frame bin, its phase steps forward in *time* to the current frame (trapezoidal rule on $\Delta_t$); if it is a current-frame bin, its phase steps in *frequency* to its still-unassigned significant neighbors (trapezoidal rule on $\Delta_f$). Each bin's phase is computed exactly once, and the order — loudest first — means coherence radiates outward from the spectral features that matter most. Significant bins (magnitude above a relative tolerance) are assigned via the heap; quiet bins below tolerance are *time-propagated on their own instantaneous frequency*, never randomized and never copied, so that reverb and decay tails do not develop spectral holes. Time-stretching rescales only the time grid; the frequency grid is unchanged, so the vertical integration uses the analysis gradient as-is ($b_s = b_a$) — a point validated empirically against the trusted peak path, where the alternative scalings lose or inflate energy under stretch and this one is stretch-invariant.

At a phase-reset marker, and at the file's first frame, both models do the identical thing: seed $\theta = \varphi$ and skip integration. The models differ only in how they propagate *between* resets.

## 4. The two transcription hazards

Moving the frequency axis from the published convention to the engine's un-fftshifted STFT produced, in sequence, two distinct and individually diagnosable failures. Both are violations of the symmetry principle on the frequency axis, and they are distinguishable by ear.

| | Hazard 1 — Leslie rotating speaker cabinet | Hazard 2 — tap-delay / comb filter |
|---|---|---|
| **Symmetry break** | `expected_f` not subtracted before $[\cdot]_{2\pi}$ | `expected_f` subtracted for unwrap but not re-added for reconstruction |
| **What the offset does** | A constant per-bin offset survives the unwrap and the heap **accumulates** it across each region as a phase ramp in frequency | The deviation integrates correctly but a **fixed** per-step offset remains in the reconstructed phase |
| **Audible symptom** | Continuously rotating, sweeping phasiness — the perceptual character of audio passed through a Leslie rotating speaker cabinet | Static comb filtering: broadband inter-bin cancellation, lost vertical phase coherence, reduced level; reset frames ($\theta = \varphi$) are conspicuously louder than the comb-filtered material between them |
| **Distinguishing tell** | The offset accumulates, so the artifact **moves** | The offset is fixed, so the artifact is **stationary** |
| **Fix** | Subtract `expected_f` before $[\cdot]_{2\pi}$ | Re-add `expected_f` in the synthesis step: `step = expected_f + deviation` |

The diagnostic value of the sequence is that each single-variable fix moved the symptom *predictably*: missing-subtract gave the accumulating sweep; adding the subtract but forgetting the re-add gave the fixed comb filter; doing both gave clean output. Because each change was made one at a time against an audible signal, the symptom progression itself confirmed the diagnosis. The first hazard rotates because an accumulating offset is a ramp; the second comb-filters because a fixed offset is a constant phase tilt that causes adjacent bins to cancel on summation. Reset frames stand out in the second case specifically because the seed ($\theta = \varphi$) is *correct* and is perceptually conspicuous against surrounding frames that have lost their vertical phase coherence.

Neither of these is a defect in the published method. They are hazards of *transcription* — of carrying a formula across a change of phase convention without carrying the convention's hidden assumptions with it. The contribution here is the diagnosis and the symmetry principle that prevents recurrence, not a correction to the source.

## 5. Methodology: instrument, do not speculate

One concern about the heap algorithm is theoretical and has no obvious audible signature: a bin that becomes significant at a hard onset, with no already-assigned significant neighbor to propagate from, could in principle be left unassigned ("orphaned"). Rather than write a speculative guard for a failure with no observable signature, we instrumented it: a debug-gated counter that tallied every orphaned bin and every frame containing one, run over the full 455-second first movement — approximately 39,000 heap frames. The result was **zero orphan frames and zero total orphans.** Real orchestral onsets carry sufficient frame-to-frame spectral continuity that the gap is never triggered. The counter was then reverted; the question was answered, and a permanent guard for a condition that does not occur would be unwarranted complexity. We record the orphan possibility as a documented caveat, not a fixed defect. We offer the episode as the general methodology used in this work: when a suspected fault has no audible signature, add a read-only counter, render representative material, and decide on the measurement.

## 6. Findings

### 6.1 Each model's failure mode

The headline result, stated as one expert listener's judgment over many iterations on the demonstration material:

**Peak (Laroche-Dolson) is a solid baseline.** Without phase resets, peak alone produces output that is acceptable but dull. With phase resets, it substantially recovers vertical phase coherence at each reset point — but the inter-reset propagation still smears mid- and high-frequency content, attenuating perceptual brightness and apparent transient sharpness. Peak's failure mode is therefore not a transient artifact but a *steady-state perceptual loss*: stable dullness, reduced impact, reduced clarity in the upper bands. The output remains coherent and listenable; it lacks the perceptual presence of the source.

**Heap (PGHI) is the opposite failure mode.** Heap preserves the spectral life that peak smears — the upper-band detail, the sense of impact at climaxes — but introduces a warbly, vibratory instability whenever the signal lacks dense articulation to mask it. On exposed sustained legato the instability is audible as phase wobble; on exposed tremolo it is sharper, because tremolo is rapid re-articulation (closely-spaced repeating near-onsets with spectral content in constant flux) and the gradient-integration anchors flicker frame-to-frame. Heap as the sole mode is therefore unacceptable for any material in this evaluation except contrived signals. We note that the authors of the PGHI method [2] would likely disagree with this characterization, as their listening study reports heap-alone as competitive with commercial tools; we return to this divergence in Section 6.6.

### 6.2 Asymmetric necessity

The single most practically important consequence of the above is asymmetric: peak and heap are *not* symmetric alternatives whose roles depend on local taste. Their necessity is one-directional.

**No quiet passage benefits from heap.** Quiet material has neither the dynamic envelope nor the articulated onset density to mask heap's instability, and peak's dullness is least audible precisely where the music is least demanding. Quiet material is peak-only.

**Many loud passages still require peak.** Loud does not imply heap. Loud legato, loud tremolo, and any loud writing lacking punctuated staccato-style gaps fail to mask heap's vibratory instability, and peak — even with its smearing — is the only acceptable choice. Loud material requires per-passage classification on articulation, not on dynamic.

**Without heap, peak is insufficient at climaxes.** The third asymmetry is the reason the dual-model architecture is required at all: peak alone produces a dynamically and spectrally flattened reading of the climactic, dense, harmonically-rich tutti passages that carry the dramatic weight of the music. A peak-only render is internally consistent — the dullness is uniform across the work — but consequently the passages that should be perceptually most prominent are rendered with the same reduced presence as everything else. Heap applied per-segment to those passages restores their perceptual prominence.

Heap is therefore not an enhancement to a working peak baseline, and peak is not a fallback for material on which heap fails. Each is the unique acceptable choice in regions where the other is unacceptable, and the per-segment dispatch is the mechanism that permits both to be applied where each belongs.

### 6.3 The authoring map

Because the choice is per-segment, the operator tags each section with the model that suits its texture. For the demonstration movement, the working map is: heap on the first prolonged tutti and on the exposition close (from the post-quiet-interlude crescendo through the end of the exposition); peak on the quiet and legato material, on all tremolo, and on the intermittently explosive prelude with its deceptive quiet interlude — because that alternation would otherwise put heap on exactly the exposed-quiet content it handles worst. The same assignments replicate across the exposition repeat and the recapitulation, where the material recurs. This map is visible directly in the authored marker files: phase-reset markers carry `heap` tags at the tutti entries and `peak` (or inherited `pass`) elsewhere, exactly tracking the texture.

### 6.4 The model-handoff seam

The single remaining concern in the design is the seam where an outgoing heap segment meets an incoming peak segment, or the reverse. Both models re-seat $\theta = \varphi$ at the reset, so the handoff is not a discontinuity in the strict sense — but the overlap-add window blends two models' phase trajectories across the boundary, and the seam is in principle audible. In practice it is not audibly problematic on the demonstration material, for a structural reason: the authoring map (Section 6.3) places every mode switch on a *section boundary* — quiet-to-tutti, the edges of the exposition close — which are points of strong dynamic and textural change and natural phase-reset locations independent of the mode-switch concern. Masking at section boundaries is correspondingly strong. A seam-crossfade refinement was designed in preliminary form and is held in reserve; the authoring placement to date has rendered it unnecessary. We classify it as not currently required rather than as outstanding work.

### 6.5 Operating envelope

The validated stretch envelope is modest by design. The nominal operating point is approximately a $1.5\times$ speed-up; the largest *sustained* stretch on the demonstration movement is on the order of $1.3\times$. These ratios are gentle, and are not a sampling artifact concealing a wider claim: the objective is transparency, and transparency is a function of both material and ratio. Larger ratios are mechanically possible — the control surface admits arbitrary ratios — but the envelope reported here is the one we have evaluated and judged transparent on the demonstration material. Performance at larger ratios is unevaluated and no claim is made about it.

### 6.6 Comparison to evaluated tools

We have, over the course of this work, compared the engine against a range of general-purpose time-stretchers and judged it preferable for transparent time-warping. Among open-source tools: Rubberband, Bungee, Signalsmith Stretch, and SoundTouch. Among commercial tools: zplane élastique (as deployed in Ableton Live and Reaper), Serato Pitch 'n Time (via Serato Sample), and Digital Performer's ZTX.

The claim, stated at its scope:

> *For transparent, high-ratio time-warping under manual control, this dual-model approach produced results we judged more transparent than every general-purpose tool we evaluated.*

The tools above are general-purpose: optimized for real-time use, formant-preserving vocals, low latency, and zero-configuration robustness on arbitrary input. The engine here is offline, hand-authored per-segment, and manual. The claim is that under those constraints transparency exceeds what we obtained from any general-purpose tool we evaluated; we do not claim it is a better general-purpose tool, and the constraints are stated as plainly as the comparison.

**Divergence from Průša & Holighaus [2].** The PGHI authors' own listening study reports heap-alone as competitive with the commercial tools they evaluated, which is at variance with this paper's finding that heap-alone is unacceptable as the sole mode on the demonstration material (Section 6.1). We attribute the divergence to three factors that are not in conflict: their listening study used a different acceptance criterion (relative comparison category rating against the proposed algorithm as reference), a different repertoire — drum, voice, and mixed pop excerpts — and a different stretch factor regime ($\alpha \in \{1.5, 2.0\}$ at the larger end). The repertoire factor is the salient one. Drum and pop material provide dense and pervasive masking that conceals heap's vibratory instability; orchestral material does not, and is one of the most artifact-revealing test environments in the genre space. The divergence in our finding is therefore the expected direction: we listened to heap-alone on harder material and heard a failure mode the PGHI study's material would not have surfaced. We regard PGHI as a method this paper *integrates and depends on*, not one it supersedes — heap is half of the result here, and the dual-model architecture only exists because PGHI gave us a model whose failure mode is complementary to Laroche-Dolson's. We would welcome the PGHI authors' independent evaluation of this work and of the demonstration material.

## 7. Limitations

- **Evaluation breadth.** The formal demonstrations are on one body of material (the 2024 Bernauer remasters of the 1972 Krips Mozart cycle) by one expert listener over many iterations, cross-checked informally with a small number of additional listeners. Orchestral material is the most artifact-revealing case for this class of algorithm; the author's two decades of producer practice on rock, pop, and electronic dance music support the expectation that the technique generalizes favorably to those genres, whose denser masking gives algorithmic artifacts less audible room. No formal cross-genre evaluation is presented here, and this is not a formal listening study.
- **Offline and manual.** The method requires hand-authored phase-reset markers and is not real-time. The authoring burden is modest — fewer markers than one might assume, and far less than the timemap demands — but it is manual, and that precludes drop-in or real-time use.
- **Heap failure modes.** Exposed sustained legato and exposed tremolo are where heap degrades; these are handled by tagging those segments peak, but they are genuine limits of the gradient-integration model, not solved problems.
- **Automated marker placement: declined, not infeasible.** Automated phase-reset placement is conceivable in principle and is a tractable engineering task — it would require classification of loud versus quiet material and, within loud material, articulated (staccato, punctuated) versus sustained (legato, tremolo) articulation, both of which are within reach of established audio analysis methods. We have not pursued it. The judgment is that manual marker authoring produces measurably higher transparency than any automated placement we would expect to achieve, and that the per-passage classification a competent operator performs by ear includes contextual information (phrase boundary, score knowledge, intended musical emphasis) not available to a signal-only classifier. We note the path here for completeness rather than as ongoing work.

## 8. Conclusion

The central result of this work is that **neither Laroche-Dolson identity phase-locking nor Prusa-Holighaus phase-gradient heap integration is sufficient on its own; their per-segment combination is.** The two models exhibit complementary failure modes: peak (Laroche-Dolson) produces a steady-state perceptual loss — stable dullness and reduced upper-band clarity — across all material, with phase resets recovering vertical phase coherence only at the reset points themselves; heap (PGHI) preserves the spectral life that peak smears but introduces vibratory instability whenever the signal lacks dense articulation to mask it. The per-segment dispatch — peak by default, heap selectively on passages whose articulation supports it, seeded identically at each reset — allows each model to be applied only where the other is unacceptable. The two phase-convention transcription hazards described in Section 4, and the single symmetry principle that explains both, are reported as a cautionary note for any implementation carrying gradient-integration phase correction across a change of STFT convention. The cost of the approach is manual marker authoring and offline operation; in our evaluation, transparency on the demonstration material exceeds that of the general-purpose tools listed in Section 6.6.

---

## References

[1] J. Laroche and M. Dolson, "Improved Phase Vocoder Time-Scale Modification of Audio," *IEEE Transactions on Speech and Audio Processing*, vol. 7, no. 3, pp. 323–332, May 1999. <https://ieeexplore.ieee.org/document/759041>

[2] Z. Prusa and N. Holighaus, "Phase Vocoder Done Right," in *Proc. 25th European Signal Processing Conference (EUSIPCO)*, 2017. <https://ltfat.org/notes/ltfatnote050.pdf> (Real-time phase gradient heap integration; building on Prusa & Sondergaard, DAFx-16, and Prusa, Balazs & Sondergaard, IEEE/ACM TASLP 25(5), 2017.)

*Both methods are cited as integrated, not extended. The contribution of this work is the per-segment dual-model architecture, the diagnosis of the two transcription hazards and the symmetry principle, and the per-section authoring findings on the target repertoire — not any modification to the underlying phase-correction methods themselves.*
