# Validation topology (architect-blessed 2026-07-17, HEAD at landing)

Every guard in the tree, classified. Produced by the round-8 foundations sweep (three parallel classification agents over parser/prepost, engine/audio_io/cli, and the GUI boundaries), blessed with the R8 rulings below. New guards must classify under this doctrine at introduction; a guard that fits no class is a design smell to resolve, not a row to force.

## The doctrine

Four failure classes, each with ONE owner:

1. **ADVERSARIAL input** (hand-edited/corrupt files, absurd values) → the **LOAD boundary**. First-error hardfail, loud, identical in both binaries ("loadable in both products or neither"). The product contract.
2. **AMBIGUOUS authored state** → the **RESOLVER** (and its phase-reset sibling). Normalizes, never refuses; one stderr line per timestamp per resolve is the signal (directive #11).
3. **LEGAL-BUT-UNHONORABLE state** → the owning stage refuses **CONSTRUCTIVELY** (plan_trim: refusal = render untrimmed, never a failed render).
4. **PROGRAM BUGS (breach)** → the **ENGINE INIT tripwires**: the last point before silently-wrong bytes, validating the actual consumed artifact in both binaries. Breach loudness elsewhere (abort / terminate / stderr+Failed) is justified only where the engine cannot see the breach.
5. **ADVISORY signals**: stderr-and-continue, owning no failure — the fftw-threads single-thread fallback, the framemap-pair write failure (inert future-proofing), the render-env hash mismatch (detection, not prevention). Never fatal.

A predicate may exist at TWO boundaries only when:

- **(a)** the downstream owner cannot catch it (sub-frame gap — the engine checks ascent, not spacing; dangling label ref — the engine never consumes refs; apply_post_trim's crop contract — only it holds both buffer and window; the worker's inode-level clobber check — batch paths and write-time races only it can see);
- **(b)** the upstream site has message vocabulary the downstream lacks (the sweep tempo<=0 backstop names the tempo; the engine could only say "not strictly ascending");
- **(c)** an explicit architect symmetry ruling covers it (the past-EOF column pair; the GUI/CLI shared-implementation load checks).

**Type rule**: an error arm (std::expected / optional / bool-false) exists iff a producer exists — reachable, breach-only, or IO, and the classification names which. Total normalizers return plain values (resolve_warp_markers_for_render); partial compilers keep expected (build_warp_frame_map).

**Out-of-topology labels** (things a guard-shaped grep hits that are not guards): LIVE FUNCTIONALITY (cancel checks, capability-loss release tails, the limiter's pass cap), DOMAIN INVARIANT (the clamp chokepoints — constructive, never refuse), DISPLAY-ONLY (marker_effective/hover safe-returns — no bytes, no stderr), INTERACTIVE VOCABULARY (editor red-flash — the same validators the load runs, UI surfacing only: "loadable iff it commits"), ENVIRONMENT PRECONDITION (the CLI's locale/arg checks — guard the launch, not the data).

## Sweep verdict

ZERO incoherent guards. Every duplicate carries an (a)/(b)/(c) justification; every error arm has a producer proof. Notable adjudications, all KEEP:

- The trim-wall predicate at both marker_store_validate and validate_trim_frames: two failure classes over one predicate keyed on provenance — persisted = adversarial load-fatal, live = constructive render-untrimmed; neither owner can serve the other's reachability set.
- apply_post_trim's refusal: correctly-placed (a)-class — the only site holding both the buffer and the crop window; the engine is trim-ignorant.
- RenderFileIdentity vs ArtifactStatIdentity: two honest trust boundaries (persisted SOURCE recipe-key vs ephemeral OUTPUT TOCTOU race token; never interconverted) — cross-referenced at the structs.
- compute_buffer_start_frame_for's span re-derivation: honest minimal coupling — two of its three callers structurally cannot see a TrimPlan (the synchronous reuse rungs never run plan_trim; the dispatch stamp precedes the async plan).
- Message vocabulary: every boundary prints its owner's .error() verbatim; the two by-design exceptions are the trim-fallback literal reproduced at target_render's reuse rungs (they return before plan_trim runs — see R8-4) and render_output_source_collision returning a PATH (each boundary authors its own register; GUI-load and CLI-load deliberately identical).

## The R8 rulings (architect 2026-07-17)

- R8-1 taxonomy amendments (class 5 + out-of-topology labels): BLESSED, incorporated above.
- R8-2 the NDEBUG-stripped synthesis assert: PROMOTED to an always-on loud breach check (stderr + abort — synthesize_full has no error channel and a silent early-return would truncate emission).
- R8-3 render_output_naming.h's phantom fourth-caller claim: FIXED (three real callers + the worker's own inode-level backstop; Shift+. adopt needs no check — entry sidecars are trusted).
- R8-4 the trim-fallback literal duplicated at target_render's reuse rungs: ACCEPTED with the drift risk recorded at the sites; a shared constant is the fix WHEN the trimmer next opens (do not reopen for this alone).
- R8-5 marker_effective's ~50-line label-ref computational tail duplicated between the projection and carve-out walks: freeze-reopen candidate (extract a shared readout helper when the parser next opens). The WALKS themselves are honestly split — different domains by the authored-display-split ruling; do NOT unify them.
- R8-6 source_audio_io's end<=begin arg guard: KEEP — not a pure duplicate (wav_read_range accepts equal bounds as empty success; the guard refuses).
- R8-7 pcm24_code_from_float's NaN clamp: KEEP — dead in the write path (WavWriter's isfinite refusal runs first) but live via the GUI quantize route.
- R8-8 identity structs: keep-as-is + the cross-reference comments (landed).
- R8-9 the span re-derivation: KEEP (minimal coupling; see verdict above).
- R8-10 message vocabulary: declared as-is.

## Classification tables

The three cluster tables below are the sweep's row-level output, reproduced as delivered (2026-07-17, HEAD a0586b2). Line numbers drift with edits; sites are anchored by symbol names. Legend: class 1-5 per the doctrine; dup-(a/b/c); producer proof = reachable / breach-only / IO / none.

### Table A — engine, audio_io, cli

engine.cpp: output_buffer null check = class 4 (crash-prevention breach; breach-only — both orchestrators set it). validate_warp_frame_map_strictly_ascending: empty-map, per-entry finiteness, strict src ascent, strict tgt ascent — all class 4 (empty → .back() UB; NaN/non-monotone → silent misinterpolation), breach-only behind the resolver's collapse/seed and the builder's emission ordering. validate_phase_reset_frame_map_strictly_ascending: finiteness + strict ascent = class 4 (silent skip by the forward cursor), breach-only behind the collapse + constant-shift derivation. Source-buffer param checks = class 4 breach (crash-prevention). emit_sample_cap <= 0 = class 4 (cap 0 would read as uncapped → full STFT tail, silent-wrong), breach-only (trimmer closing anchor >= 1). Schedule size/order checks = class 4 (wrong-length/order schedule → silent-wrong warp), breach-only (plan_trim counts with the same loop as generate_source_frame_positions; integer-cut translation preserves order). cancel_pending sites = live functionality. init_fftw_threads else-branch = class 5 advisory. synthesis.cpp emission-size check = class 4, ALWAYS-ON since R8-2 (was a Release-stripped assert). synthesis abort ring = live functionality. limiter kCidSelfCheck = compile-gated diagnostic; MAX_PEAK_RESOLVE_PASSES = live functionality (ruled outside the trim null contract).

audio_io parse_wav_layout: all twenty-plus container refusals (magic, RIFF-size-vs-physical, chunk-past-EOF, duplicate fmt/data, fmt sizes, cbSize overrun, non-PCM subformat, fmt sanity, block_align/byte_rate consistency, trailing-bytes — proven non-redundant with the RIFF-size check — missing fmt/data, alignment, zero-frame) = class 1, the ruled adversarial-load contract, each reachable-adversarial. checked_audio_sample_count + read_range overflow/alloc bounds = class 1 (the 8 GiB implausible-alloc arm reachable-adversarial; the rest breach-only on a 64-bit host). read_range invalid-range = class 4 caller-contract, breach-only (plus the source_audio_io equal-bounds refusal above it, R8-6). Truncated-read/seek arms = class 1 IO. audio_probe arms = class 1 (unknown-magic carries the GUI-side convert-once hint — vocabulary (b)). pcm24 NaN clamp = R8-7. WavWriter: state/arg checks = class 4 breach; RIFF-limit checks = class 3 backstop, dup-(a) with validate_render_projection's refuse-before-cost; non-finite sample refusal = class 4 (a NaN would quantize into the lattice silently), breach-only; IO arms = class 1 IO.

cli_main: locale + arg checks = environment precondition. All load-path refusals (settings schema, collision, marker parses, probe, rate floor, stereo, first_past_eof_wall_defect) = class 1, dup-(c) shared-implementation with the GUI load, surfacing each owner's vocabulary verbatim. build_warp_frame_map / build_phase_reset_source_frames failures = class 4 breach backstops surfaced at load (the CLI has no sweep, so even the tempo arm is breach-only there). write_frame_map_pair failure + env-hash mismatch = class 5 advisory. plan_trim refusal = class 3 constructive fallback. validate_render_projection = class 3 refuse-before-cost, dup-(a) partner of the WavWriter RIFF backstop. Engine/finish_render/rename failures = owner-surfacing / IO.

### Table B — parser, prepost

Resolver + phase-reset sibling: coincident-collapse, pass-inherits-ref, dangling/extreme-ref normalizations and the reset collapse + render-end drop = class 2 owners (stderr is the signal; the render-end drop is silent by design). build_warp_frame_map: metadata check = class 4-flavored breach (breach-only); past-EOF = breach backstop dup-(a/c) with marker_store_validate (owner) and the reset wall (column symmetry); sub-frame gap = dup-(a), the one map defect the engine cannot catch — silent-wrong class; tempo <= 0 = the one REACHABLE refusal (sweep cells), kept at this site for (b) message vocabulary; dangling-ref pass-2 = dup-(a), raw-UB loudness. marker_effective/hover safe-returns = display-only. All marker/settings grammar arms (warpmarkers_parse, phaseresetmarkers_parse, settings_file, engine_settings_io, value_format/frame_format/parse_text_util primitives) = class 1 owners, reachable-adversarial; validate_gui_setting and validate_engine_setting serve load AND editor-commit as one implementation (not a duplicate — interactive vocabulary label). marker_store_validate = the class-1 past-EOF/trim-wall OWNER (the one implementation both products run). source_audio_io: equal-bounds refusal (R8-6); error forwarding = class 1 owner-surfacing.

prepost: validate_trim_frames = class 3 OWNER (sole trim-validity vocabulary; the two wall arms are dup-(a) with marker_store_validate — different classes over one predicate keyed on provenance); plan_trim propagates it. apply_post_trim = class 4-flavored internal-breach, (a)-justified (only holder of buffer + crop window), breach-only. validate_render_projection = class 3 refuse-before-cost (RIFF arm reachable for >4 GB renders; overflow arm breach-only). finish_render buffer-contract checks incl. the non-finite scan = class 4 breach at the shared GUI/CLI chain, dup-(a/b) with the WavWriter refusal (covers the buffer route too, names the sample index); WavWriter IO arms = class 1 IO; cancelled returns = live functionality. peak_limiter hardclip = constructive silent-wrong clamp (KEEP class), outside the refusal topology. map_output write arms = class 5 advisory (inert future-proofing pair; caller treats failure as one non-fatal line).

### Table C — GUI boundaries

file_loader: .peaks-path refusal = class 1, GUI-only surface (the launch loader has no blank state). Probe/marker/settings/collision/rate/stereo/past-EOF refusals = class 1, owner-surfacing (%s verbatim) with dup-(c) shared-implementation where the CLI runs the same code; the collision line is GUI-authored from the predicate's PATH return (deliberately identical wording to the CLI's). Crossed-trim clear at load = class 2 normalization, dup-(c) with the gesture-side auto_clear_crossed_trim (same exact integer compare — cannot drift); deliberately ordered AFTER the past-EOF hardfail. validate_target_view_entry (load restore + t-toggle) = class 2 (resolve-then-build; failure falls back to S silently), dup-(c) same predicate both entries. Editors (settings/flag/bpm/commit) = interactive vocabulary over the same parser validators (red-flash; "loadable iff it commits"); the flag editor's iter-bracket gate is editor-owned (session-only state never reaching the parser; backstops = the build refusal and the strict promote parse). adopt_render_entry = validate-all-then-mutate returning false untouched (constructive; every false arm is a parser failure — no new validity judgment; entry sidecars trusted). Render worker source-clobber check = class 4 breach backstop, dup-(a): inode-level, composes batch paths, sees write-time races the load-time predicate cannot. Trim gesture walls + auto_clear = class 2/constructive owners. Platform capability-loss/keyboard-leave release tails = live functionality. clamp_playhead_to_live_domain / clamp_viewport_start / snap_authored_frame = domain invariants.
