# Warptempo engine performance campaign — engineering record (2026-07-16)

The canonical engineering record of the campaign: the quantitative and
methodological history that the code-comment rules (current behavior only)
deliberately keep out of source. Per-technique mechanism/rationale
documentation lives in the code comments at the sites listed in §9; a white
paper, if ever written, derives from this document. The
campaign opened and closed on 2026-07-16: eleven commits
(`33616a4..5bd70ee`), full-render synthesis wall 13.33s → 5.28s (−60%),
zero production defects across two external (codex) audits.

## 1. Setting

- Engine: custom C++23 in-process PGHI phase vocoder (Průša/Balazs/
  Søndergaard, ltfatnote050 — noniterative phase reconstruction from STFT
  magnitude). N = 4096, synthesis hop R_s = N/4 = 1024, FFT length
  M = 2N = 8192, half-spectrum K = 4097 bins. Two channels, each a
  producer (analysis FFT + PGHI prep) / consumer (phase integration +
  synthesis) thread pair over a FIFO ring.
- Host: fixed single laptop, Arch Linux, GCC 16.1.1, glibc 2.43, AVX2+FMA
  (no AVX-512), hybrid P/E cores, `-O3 -march=native`, binaries
  deliberately non-portable.
- Reference workload: Mozart Symphony 40/I (1972 recording, 2024 remaster
  — tape hiss throughout, no digital silence), 25,448,052 frames 44.1 kHz
  stereo, 221 warp markers, 352 phase-reset markers (230 placed on the
  full render), target 455.75 s. A trimmed variant (6.8M source frames →
  120.78 s target) served as the fast iteration case.
- Corpus property that shaped everything: 0.0% quiet bins — essentially
  all 4097 bins exceed the PGHI significance tolerance every frame
  (broadband tape hiss), so the propagation walk runs at full width.

## 2. Acceptance criterion (and the insight behind it)

Initial framing: renders could relax from byte-exact to "run-to-run
phase-invert null below −120 dBFS." The pre-campaign audit's key insight,
confirmed throughout: **−120 dB is a verification ceiling, not a spendable
budget.** Two amplifiers make any tolerance non-local:

1. The PGHI priority walk makes threshold/order decisions; a one-ulp
   upstream perturbation can flip which bin wins a comparison, selecting a
   different propagation path with effects far above −120 dB. Quiet-bin
   phase is inter-frame state (it can time-seed a bin that becomes
   significant next frame), so nothing upstream of the walk is bounded by
   the significance threshold.
2. The spectral limiter's per-peak thresholded decisions bifurcate under
   ulp-scale input differences (empirically confirmed, §7).

Settled contract: two fresh renders under one fixed binary must be
byte-identical (`cmp` null); the −120 dB figure survives only as the
measurement floor when comparing across *reference* changes.
Reference-byte changes are legal and ride `kFingerprintVersion` bumps
(v9 → v14 over the campaign) so cached artifacts re-render. Every
candidate had to be deterministic; all "spend the tolerance" options
(per-render FFTW planning, entropy-seeded quiet phase, relaxed concurrent
accumulation) were rejected at the outset.

Practical consequence: the relaxation's real value was **freedom from
old-reference byte compatibility** (enabling vector math, tie-order
changes, threshold-predicate changes), never freedom from determinism.

## 3. Method

- **Temporary instrumentation first** (`33616a4`): env-gated
  (`WARPTEMPO_PROFILE=1`) per-stage wall clocks and counters — per channel:
  PGHI (quiet/sort/drain split added mid-campaign), spectrum population,
  inverse FFT, OLA, ring wait; selection counters (total, prev-done-skips,
  current-noop-pops); limiter walls and bypass predicates. Strictly
  byte-neutral (counters and guarded clocks only); fully removed at close
  (`5bd70ee`).
- **Measure before touching**: every structural change was preceded by
  either the profiler's stage split or a scratchpad microbenchmark at
  exact production scale (K = 4097 nodes/frame, frame-to-frame-correlated
  log-normal magnitudes). Microbench predictions held within ~1.3x of
  production outcomes; production results twice exceeded prediction
  (§5.4) and once fell short due to memory contention (§6.1).
- **Verification ladder**, strongest available oracle per step:
  1. bit-exact claim → `cmp` against the pre-change render;
  2. order-identity claim → `cmp` against the previous fingerprint
     generation (valid because the corpus is tie-free — see §5.4);
  3. reference change → run-to-run `cmp` of two fresh renders
     (digital zero required) + phase-invert statistics + ear test.
  The GUI's render cache serves fingerprint-identical dispatches from
  cache/artifact, so fresh synthesis had to be forced for every timing.
- **External review**: two codex audit rounds (after step 6 and at close),
  report-only, with findings converted to fixes. Round 1 found two
  profiler reporting defects and one comment-precision issue, no
  production defects; round 2 found no production defects and one
  evidentiary qualification (§7).

## 4. Baseline profile (full render, instrumented)

| stage (per channel) | time |
|---|---|
| PGHI walk (heap) | 10.88 / 11.44 s |
| quiet-bin RNG assignment | 0.06 s |
| spectrum population (scalar sincos ×4097/frame) | 1.34 / 1.37 s |
| inverse FFT | 0.42 / 0.43 s |
| OLA + emit | 0.07 s |
| ring wait (analysis producer) | 0.017 s |
| **synthesis wall (channels concurrent)** | **13.33 s** |
| spectral limiter (engaged: raw max 1.181 > 0.966 ceiling) | 1.21 s |
| final peak limiter | 0.30 s |

Heap counters: 158.9M pops/channel (= 2 per significant bin per frame:
one prev-frame node, one current-frame node), inert tail 0.8–0.9%,
19,632 frames, 231 seed (reset) frames. The walk was ~82% of the wall.

## 5. What landed

### 5.1 PGHI early stop (`184a6f4`, bit-exact, no bump)

Algorithm 1 runs while the undone set I is nonempty; the implementation
drained the heap completely, executing provably inert pops after the last
assignment. Fix: an unconditional `remaining` countdown as the loop
condition. Verified `cmp`-null. Measured ~1% (the corpus's inert tail was
0.8%; the audit had ranked this "highest potential" — the measurement
deflated it immediately, which is the point of measuring).

### 5.2 Limiter bypasses (`4d45cbc`, fingerprint v10)

Peak limiter: one linear scan; an all-finite buffer at/below the ceiling
returns untouched (bit-exact when it fires — unity gain through a double
round-trip). Spectral limiter: the same predicate scan *before* any
allocation (formerly it built windows, band tables, FFTW plans, and a
full cached spectrum — ~85 MB/stereo-minute — before discovering
compliance). Byte-change only in the raw-compliant/identity-overshoot
edge → bump. Dormant on the (hot-mastered) reference: measured zero
there; pays off on quiet material.

### 5.3 Vectorized synthesis trig (`9d2c91a`, fingerprint v11)

Feasibility findings: glibc's libmvec declarations are gated on
`__FAST_MATH__`; GCC 16 does **not** auto-vectorize the two-output
`sincos` form even under fast-math, but vectorizes split `sin`/`cos`
calls to `_ZGVdN4v_sin/cos` (AVX2 four-double). Microbench: true scalar
6.5 ns/element at small phases, **12.0 ns at ±3e5 rad** (glibc
large-argument reduction); vector **1.7 ns flat** at every magnitude
(3.9–6.9x), max diff ~3e-16. The flat-cost reduction also retired the
separate "keep phase bounded mod 2π" candidate — its entire benefit was
avoiding the scalar large-angle reducer.

Shape: one new TU (`synth_spectrum_trig.cpp`), the tree's only
`-ffast-math` source (CMake source property; later also pinned
`-fno-lto` so LTO can never re-form the boundary). Interior loop only;
the strict-FP Hermitian endpoint correction overwrites the two endpoint
bins after. Verified: emitted object references `_ZGVdN4v_*`; run-to-run
digital zero (sox: every stat −inf over 5.32M samples). Production:
populate 0.36 → 0.05 s/channel trimmed (7x), ~9% wall.

### 5.4 Split-stream walk (`1eeae1f`, v12) and 8-byte nodes (`7a97bb9`, v13)

Microbench of five heap variants at production scale (per-frame walk,
all-significant bins):

| variant | vs current | assignment order |
|---|---|---|
| make_heap seed | 1.03x | identical (distinct keys) |
| 8-byte float-key binary heap | 1.15x | 153/8.2M reorders |
| naive 4-ary heap | **0.75x (slower)** | identical |
| split streams, double keys (F) | 1.20x | identical (distinct keys) |
| split + 8-byte nodes (G) | 1.25x | 162/8.2M reorders |

The split-stream insight: the combined max-heap mixes two populations
with different lifecycles — ALL previous-frame candidates are known up
front (keyed on the previous frame's magnitudes), while current-frame
candidates arrive dynamically. Splitting them makes the prev population
one descending sort walked by index (done entries skipped — exactly the
old discard pops) and leaves a heap only for current nodes; each
selection is max(stream head, heap top), and the max over a partition
equals the global max, so the assignment sequence is **provably
identical for distinct keys**.

Verification design: the corpus's tape hiss means no two double
magnitudes are ever bit-equal, so order-identity ⇒ byte-identity, and
the restructure could be accepted by `cmp` against the previous
generation — a far stronger oracle than run-to-run (a deterministic
logic bug nulls run-to-run perfectly; it cannot null against the old
implementation). This drove the decision to stage through exact double
keys (F) before flipping to float keys (G/v13) as a minimal,
separately-verified delta. Both cmp-nulled in production: v12 against a
v11 render, v13 against v12 (no float near-tie affected any contested
propagation source on this material — selection counters bit-equal).

Production: heap 2.90 → 2.19 s/channel trimmed (1.33x, better than the
1.25x prediction), trimmed synthesis wall 3.42 → 2.41 s. The 4b node
packing also removed the role flag entirely — the split structure
encodes it positionally.

### 5.5 Producer-side stream construction (`e30aada`, no bump)

The mid-campaign sort/drain split showed the prev-stream fill+sort was
3.25 s of the 8.47 s per-channel PGHI time, while the analysis producer
sat ~99% idle relative to the consumer (`analysis-wait` ≈ 0.013 s). The
producer already owns the previous frame's magnitudes and the quiet
mask, so it builds the sorted stream into the ring slot; the consumer
receives it pre-sorted. Byte-identical by construction (same fill order,
same comparator, same `std::sort`); verified `cmp`-null.

**Contention finding** (the campaign's main negative surprise): hiding
the sort cost the consumer real speed — with two producers doing heavy
work concurrently, drain rose 5.15 → 5.4–6.3 s, populate/ifft roughly
doubled, and channels went asymmetric on the hybrid cores. Net wall
9.19 → 7.98 s (−13%, against a −30% naive projection). Lesson: on a
bandwidth-bound workload, "free" overlap is taxed by shared cache/memory.

### 5.6 Ranked active-frontier bitset (`253c799`, fingerprint v14)

Every current-node key (`float(mag_cur[bin])`) is known before the walk,
so the heap is replaceable by rank indexing: sort the frame's full K
magnitudes once under an explicit TOTAL order (descending float
magnitude, ties ascending bin), and represent the productive current
bins — done ∧ significant ∧ has-undone-neighbor — as a two-level bitset
indexed by rank (65 leaf words + 2 summary words at K = 4097).
Selection = max(prev head, min-active-rank); a spent bin deactivates via
neighbor updates and is never selected, deleting the old no-op pops
(36.9M/channel, 23% of all selections) *structurally*. The one full-K
producer sort serves twice (carry-forward): frame f's rank order is,
filtered by frame f+1's quiet mask, frame f+1's prev stream — so
producer sort count is unchanged.

Microbench: 2.78x on the drain. Production: drain 5.4–6.3 → 4.06 s,
wall 7.98 → 6.88 s; `current-noop-pops = 0` (a designed tripwire: any
nonzero indicates a frontier-update defect). Equivalence to Algorithm 1:
a current coordinate with no neighbor in I cannot execute the spread
lines and is semantically inert; deleting it early changes no
assignment (audited independently by codex, including the seven update
sites and bitset maintenance).

This was the one step where the *expected*-null cmp against v13 did not
hold — the explicit tie order fired (§7).

### 5.7 Radix magnitude sort (`4a029f9`, no bump)

The producer's `std::sort` (grown to 4.19 s/channel under contention)
became a stable LSD radix over inverted float bits: for nonnegative
finite IEEE floats the bit pattern is monotone with value, so ascending
order on complemented bits is descending magnitude, and pass stability
plus ascending-bin fill order reproduces the explicit total order
exactly. Pack `(~float_bits << 32) | bin`; four LSB-first byte passes
over the key half; unpack by complement + memcpy. Pure integer work.

Microbench: 7.38x (184.8 → 25.0 µs/frame), output order verified
identical to the comparator on 4000 frames. Production: sort 4.19 →
1.03 s, and the freed memory bandwidth deflated the contention tax
everywhere (drain 4.06 → 3.29–3.44 s, populate 0.78 → 0.35–0.44 s,
ifft 1.13 → 0.64–0.75 s). Wall 6.88 → **5.28 s**. Verified `cmp`-null
against a v14 render (order-identity restored the strong oracle).
Hand-rolled-sort risk was mitigated by the microbench order proof, the
cmp oracle, and a dedicated external audit (stability, monotonicity
incl. subnormals and +0.0f, scratch aliasing, width/overflow — clean).

## 6. Rejected with data (the negative results)

| candidate | verdict | evidence |
|---|---|---|
| make_heap heap seeding | dud | 1.03x microbench |
| naive 4-ary heap | worse | 0.75x — std::push/pop_heap's optimized sift beats a simple wider node |
| ordered inverse-FFT/OLA backend thread | demoted, dropped | only ~0.5 s/channel hideable after the trig fix; medium concurrency risk |
| pinned FFTW wisdom / measured plans | dead | analysis producer fully hidden (wait ≈ 0); ifft 0.42 s |
| phase bounded mod 2π | dead | libmvec's vector reduction is flat-cost across magnitude (§5.3); resets reseat phase every ~2 s anyway |
| quiet-RNG replacement | dead | 0.0% quiet bins on real material |
| spectral-limiter queue direction | dead | sub-operation of an FFT/rescan-dominated stage; < 1% even after the stage rose to ~18% of engine time |
| PGO | skipped | external estimate 2–5% (7% ceiling) vs. profile-lifecycle + fingerprint complexity |
| LTO | **measured slower** | interleaved 3× CLI A/B, best-vs-best: 7.578 s vs 7.808 s (LTO ~3% slower, cmp-null). Hot templates already same-TU; adoption bar was cmp-null AND ≥2% faster |
| raising kPghiTol / OpenMP | pre-ruled | listening tests (1e-6 optimal); precalc cost exceeded savings (pre-campaign rounds) |

## 7. The knife-edge limiter observation

Cross-reference phase-invert tests (any pair spanning a reference
change) on this recipe repeatedly produced the **same** difference
signature: a 2-sample peak at −38.37 dBFS over a −78.2 dB RMS floor,
with per-stat equality to every displayed digit across *different*
underlying causes (the v9→v11 trig ulps; the v13→v14 tie flips whose
whole upstream footprint was a net ±15 change in 44M selection
counters). Reading, as qualified by the external review: the residual is
*consistent with at least one* knife-edge spectral-limiter decision — a
peak balanced exactly at a threshold — that flips under any ulp-scale
upstream perturbation, quantizing unrelated tiny causes into the same
discrete attenuation-map difference; the aggregate statistics cannot
prove it is a single decision, and direct phase/queue/envelope
contributions remain possible.

Practical consequences: (a) cross-reference sox statistics cannot
adjudicate small upstream changes on threshold-engaged material — only
run-to-run `cmp` and listening can; (b) this is the empirical
demonstration of why the −120 dB criterion could never be a budget
(§2); (c) equal-magnitude tie order in PGHI is heuristically arbitrary
(the algorithm specifies loudest-first, not the coin flip), so tie-order
changes are quality-neutral — confirmed by ear.

## 8. Results summary

Full reference render, synthesis wall (channels concurrent):

| after | wall | step delta |
|---|---|---|
| baseline (instrumented) | 13.33 s | — |
| early stop + bypasses | 13.33 s | ~0 on this corpus (by design) |
| vector trig | ~12.2 s | −9% |
| split-stream + 8B nodes | 9.19 s | −25% |
| producer-side sort | 7.98 s | −13% (vs −30% projected; contention) |
| frontier bitset | 6.88 s | −14% |
| radix producer sort | **5.28 s** | −23% (incl. contention relief) |

Engine totals (synthesis + spectral limiter + peak limiter):
~15.1 s → ~7.0 s. Trimmed render synthesis: 3.42 → ~2.4 s class.
External CLI wall (load + render + encode + write): ~7.6 s at close.
Every landed step verified at the strongest available oracle; fingerprint
walked v9 → v14 with each reference change documented in the
`render_cache.cpp` version ledger.

## 9. Where the mechanism documentation lives (code comments)

- `src/engine/synth_spectrum_trig.cpp` / `.h`, `CMakeLists.txt` — the
  fast-math TU boundary, libmvec lowering, why sincos is split, ulp and
  determinism reasoning, the `-fno-lto` pin.
- `src/engine/stft_container.h` (`pghi_integrate`) — the early-stop set-I
  argument; the frontier's equivalence argument, activation predicate
  (quiet exclusion is load-bearing), two-level bitset shape, both tie
  rules (cross-partition prev-first; within-population explicit total
  order).
- `src/engine/synthesis.cpp` — the producer/consumer slot alignment
  invariant, the two-deep order rotation and carry-forward, the radix
  algorithm (monotonicity, stability-as-tie-break, key-half vs payload,
  domain invariant), ring publication ordering.
- `src/engine/limiter.cpp`, `src/prepost/peak_limiter.cpp` — the bypass
  predicates and their cancellation semantics.
- `src/gui/render_cache.cpp` — the fingerprint version ledger
  (v10 spectral-bypass edge; v11 vector trig; v12 split-stream tie rule;
  v13 float keys; v14 frontier total order).

## 10. Process notes

- Planner/coder/architect separation: all source edits by implementation
  subagents against written briefs; the architect (human) owned every
  render, cmp, phase-invert, and ear verification; external codex audits
  after each round. The frozen-engine discipline (each reopen explicitly
  scoped and approved) held for all eleven commits.
- Scratchpad microbenchmarks at true production scale were the decisive
  cheap tool: they killed two plausible-sounding candidates (make_heap,
  4-ary) before any source risk, sized three winners correctly, and their
  order-capture harness doubled as the equivalence proof for the
  restructures.
- Verification-oracle design was treated as part of each change's design:
  staging the split-stream restructure on exact double keys purely so it
  could be cmp-checked against the old walk (then flipping key type as a
  separately-verified minimal delta) is the pattern worth writing up.

## 11. Build specificity and portability

The build is host-specific by charter (`-O3 -march=native`, rebuild on the
target host); the campaign added dependencies worth an explicit inventory.

**Same-OS hardware migration (newer x86-64 Arch laptop):** the standard
commands work unchanged; `-march=native` adopts the new ISA (AVX-512, if
present, will auto-widen the trig TU's libmvec dispatch). But EVERY HOST
IS ITS OWN REFERENCE: a different ISA, glibc/libmvec version, or
compiler's FMA-contraction choices shifts output by ulps, so renders do
not cmp across machines (nor across a glibc upgrade on one machine).
Run-to-run determinism holds per host+binary, exactly as the criterion is
written. Deliberate gap to remember: `kFingerprintVersion` encodes DSP
recipe identity, NOT host identity — a `renders/` entry migrated from
another machine can pose as a current recipe; re-render instead of
migrating artifacts. The producer/consumer contention balance (§5.5,
§5.7) is a property of this laptop's core topology and bandwidth; nothing
is hardcoded, but the measured equilibria are host-specific.

**WSL (Windows):** WSL2 is genuine x86-64 glibc Linux; the CLI builds
with `-DWARPTEMPO_BUILD_CLI=ON -DWARPTEMPO_BUILD_GUI=OFF` (fftw3 the sole
dependency) and libmvec is present, so even the vector trig runs at full
speed. No expected issues.

**macOS:** correctness ports; part of the performance does not.

- The campaign net-IMPROVED source portability: the GNU `sincos`
  extension is gone (split std::sin/std::cos), and the radix, frontier,
  and bitset code are plain integer/IEEE-754 operations — endian-safe and
  ARM-clean; `__builtin_ctzll` is supported by clang.
- Build-command tweaks: Apple Silicon clang may reject `-march=native`
  (use `-mcpu=native`); `std::expected` needs Xcode 15+ / LLVM 16+;
  fftw from Homebrew.
- The vector-trig win evaporates: no glibc means no libmvec, and Apple
  provides no vector libm that auto-vectorization targets — the fast-math
  TU compiles and runs correctly but falls back to scalar sin/cos calls
  (~10% whole-render regression vs the Linux build). A vendored vector
  math library would be a separate decision outside the current charter.

**Latent trap (all platforms):** `-ffast-math` must remain a
COMPILE-scoped option on `synth_spectrum_trig.cpp` only. On the GCC LINK
line it would pull in `crtfastmath.o`, which sets FTZ/DAZ process-wide at
startup — flushing denormals in the entire engine: a silent global
reference change, not a scoped one. The `-fno-lto` pin on the same TU
guards the boundary against whole-program optimization for the same
reason.

## 12. Postscript: the static reference (option landed post-campaign, same day)

Following §11's observation that the dynamic reference surface drifts under
system updates, the architect ruled to pin it (`WARPTEMPO_STATIC_DSP`,
fingerprint v15). Findings from the implementation:

- glibc 2.43 cannot statically link libm/libmvec into a PIE: their ifunc
  objects reference the loader-private `_dl_x86_cpu_features` (DT_TEXTREL
  error in PIE; undefined symbol under -no-pie). Full `-static` is the only
  complete-pinning shape, available to the CLI alone (the GUI needs dynamic
  wayland/jack).
- glibc's libm.a/libmvec.a are numerically IDENTICAL to their shared
  counterparts on this host — verified by byte-comparing renders of a fully
  static CLI against a static-fftw-only CLI. This is what makes the split
  shape coherent: the GUI (pinned fftw + dynamic glibc math) and the fully
  static CLI render byte-identically within one glibc epoch.
- A static fftw 3.3.11 built with Arch's documented double-precision
  codelet set (--enable-sse2 --enable-avx) and generic -march=x86-64 -O2
  flags does NOT numerically match Arch's shipped shared fftw (first
  divergence one cascade ~15 s into the reference render) — their
  additional hardening/LTO flags or build path differ. Chasing byte parity
  with the outgoing dynamic epoch was declined as decoration: the goal was
  future stability, so the static build became the canonical reference via
  a one-time fingerprint bump (v15) instead.
- Resulting model: the fully static CLI is the archival truth, immune to
  every system update; the GUI matches it from each rebuild until the next
  glibc update, then may drift ulp-class until the next chosen rebuild
  (epoch semantics, documented in HELP's Reproducibility section).

## 13. Postscript 2: the vendored-math attempt (implemented, measured, ROLLED BACK)

To retire the remaining GUI drift surface (dynamic glibc libm/libmvec) on
Arch, a full vendored-math epoch was implemented and landed briefly: an
OpenLibm v0.8.7 subset (23 sources + header closure, byte-verbatim,
SHA-256-manifested) plus sqrt/llrint instruction shims, ld --wrap bridges
to hidden prefixed symbols, -static-libstdc++/-static-libgcc, a
set-theoretic post-link linkage audit, and a fingerprint bump with the
manifest id serialized into the payload. It was CORRECT — post-link
audits clean, run-to-run and GUI-vs-CLI cmp null — and 2.4x SLOWER:
full-render CLI wall ~7 s -> 16.7 s (user CPU 20.6 s -> 41.1 s). Cause:
OpenLibm carries the classic fdlibm/msun algorithms, while glibc 2.43
ships modern, far faster implementations; at ~160M atan2 (analysis) and
~160M sincos (synthesis) calls per render, the per-call gap dominates.
The architect rolled it back (hard reset to the v15 state) rather than
escalate to vendoring modern implementations (CORE-MATH/SLEEF class) —
"too clever by half." Lessons recorded: (a) any future vendoring attempt
must benchmark the vendored per-call cost at render volume FIRST; (b) the
correctness machinery (wrap bridges, closure discovery incl. the
round/lround and libstdc++ fenv imports, the post-link audit design)
worked and is reusable; (c) byte-stability for the GUI on a rolling
distro is now pursued environmentally (package pinning / LTS / upgrade
discipline), not in-tree. Also verified in passing: libm.so.6 imports six
GLIBC_PRIVATE symbols, so pinning an old libm.so under a newer libc has
no ABI guarantee (a snapshot-preload pin can break loudly at any glibc
update).

## 14. Postscript 3: static reference retired; stability moves to package pinning

With the vendored-math rollback, the v15 split (static-fftw GUI + fully
static CLI) lost its coherence: at any glibc epoch the static CLI would
hold while the dynamic GUI moved, breaking CLI == GUI — and the architect
ruled that identity non-negotiable. The static machinery was removed
wholesale (CMakeLists byte-identical to its pre-option state; fingerprint
ledger back at 14, which is exactly what the dynamic build's bytes are).
Byte-stability is now purely environmental: pacman-pinned glibc + fftw
(`IgnorePkg`), everything else rolling, with the boundary protocol at
each deliberate unpin — re-render a kept reference with the existing
binaries and cmp (a null means the upgrade was never an epoch); on a real
epoch: rebuild, GUI-vs-CLI cmp, one ear pass, at a project boundary only.
The static fftw prefix (~/.warptempo/fftw-3.3.11-static) is inert and
removable. Net lesson of postscripts 1-3: for this project the correct
stability boundary is the PACKAGE MANAGER, not the linker.

## 15. Addendum 2026-08-09: FMA contraction off (`-ffp-contract=off`)

The campaign's closed status is unchanged; its own reopening criterion —
new measured data — landed exactly one flag. `-ffp-contract=off` joins
`-O3 -march=native` on every target, and the reason is reproducibility,
not speed.

**The measurement.** Reference corpus 550-1 K550-I (the §1 workload:
25,448,052 source frames, 40,198,472 output samples, 24-bit). Three CLI
builds over the repo's own source lists: the shipped `-O3 -march=native`
(GCC's default is `-ffp-contract=fast`), the same with
`-ffp-contract=off`, and a generic x86-64 baseline — which carries no FMA
instruction to contract into, so it is a non-contracting reference by
construction.

| pair | result |
|---|---|
| shipped (contracting) vs baseline | 14.235% of output samples differ; max 378 LSB (−87 dBFS), RMS 14.019 LSB (−115.5 dBFS) |
| `-ffp-contract=off` `-march=native` vs baseline | **byte-identical** |
| baseline vs Clang/WASM (emscripten 6.0.6, scalar) | 67 of 40,198,472 samples differ, all ±1 LSB |

All four builds' parser framemap outputs are byte-identical — both GCC
codegens, contract-off, and the Clang/WASM build (each pair cmp'd
directly): every divergence in the table is engine FP arithmetic and
nothing else.
Contraction is therefore the WHOLE of it — turning it off makes the
vectorized `-march=native` build reproduce the generic build exactly.

The third row is a WEAKER result than the other two and is to be quoted
as such. It is ONE pair with everything varied at once — compiler,
target architecture, C library (a musl-class libm) and FFTW version
(3.3.10 vendored into the WASM build against the system's 3.3.11) — so
nothing in it is attributed to any single term. It is the CROSS-TOOLCHAIN
residue, toolchain and libraries together, not an isolated libm
measurement. That the residue is 67 samples at ±1 LSB rather than a
codegen-class divergence is consistent with the strict-IEEE reasoning —
with contraction off the arithmetic has no freedom left, so only library
rounding remains to differ, which would put it in the env-fingerprint
modal's own domain — but that reading is a hypothesis this measurement
does not separate.

**It costs nothing.** Interleaved A/B wall times on the target host
(i7-1255U, two P-cores, GCC 16.1.1) are statistically indistinguishable:

| build | interleaved runs |
|---|---|
| shipped (contracting) | 8.15 / 9.86 / 9.91 s |
| `-ffp-contract=off` | 8.10 / 9.96 / 10.11 s |

The ~5% the baseline gives up is attributable to VECTORIZATION, which
`-march=native` keeps and which changes no output bit; contraction buys
no measurable time on top of it. So with the flag on, `-O3` and
`-march=native` become pure speed knobs with no reference consequence.

Control: the scoped `-ffast-math` on `synth_spectrum_trig.cpp` (§5.3)
changes no output byte on this host — verified against a build without
it — so it stays, and the TU repeats `-ffp-contract=off` after it so the
ban is explicit rather than resting on GCC's option interaction.

**The ruling** (architect, 2026-08-09): contraction is off for this
engine everywhere, forever. Every build, every target, and any future
port — clang contracts by default too, so a port carries the flag
unchanged.

**Accepted consequence**, one time only: renders made after the switch do
not byte-match renders made before it. The envelope is the table's first
row — −87 dBFS peak, −115.5 dBFS RMS, inaudible, and of the same
knife-edge class §7 describes. What is bought, stated to the
measurement's own scope: renders are byte-reproducible across rebuilds
and across `-march` choices, under an unchanged toolchain, unchanged math
libraries and one architecture. Extending that to another machine is a
DERIVATION rather than a second measurement — the same compiler over the
same libraries on the same ISA leaves strict IEEE arithmetic no freedom —
and no cross-COMPILER claim is made at all, GCC-vs-Clang byte identity
being untested here.

This supersedes exactly one clause of §11: "compiler's FMA-contraction
choices" is no longer among the reasons renders do not cmp across
machines. That sentence's ISA and glibc/libmvec terms STAND — the
`-march` result narrows nothing beyond x86-64 codegen choices, and a
genuinely different architecture is outside everything measured here.

**Provenance.** Hand-built control binaries over the repo's own source
lists, driven by a local-only harness under gitignored `tmp/` (not
checked in, in the scratchpad-microbenchmark tradition of §10); timing by
interleaved runs on the target host. No Clang toolchain is installed on
this host, so the GCC-vs-Clang control was not run and no claim rests on
one; the WASM figure comes from an emscripten build of the same sources,
which is why it varies four things at once.
