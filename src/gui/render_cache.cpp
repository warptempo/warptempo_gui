#include "render_cache.h"

#include "directory_walk.h"   // the one non-throwing listing walk
#include "trimmer.h"          // TrimPlan: the plan arm's engine inputs
#include "wav_io.h"

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>

#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

inline void put_bytes(std::vector<uint8_t>& v, const void* p, size_t n) {
    // resize-then-memcpy rather than v.insert(end, b, b + n): byte-identical
    // output, but this shape avoids a GCC -O3 -Wstringop-overflow false
    // positive through the vector reallocation-move inlined from the insert path.
    const size_t old = v.size();
    v.resize(old + n);
    std::memcpy(v.data() + old, p, n);
}
inline void put_u32(std::vector<uint8_t>& v, uint32_t x) { put_bytes(v, &x, sizeof x); }
inline void put_u64(std::vector<uint8_t>& v, uint64_t x) { put_bytes(v, &x, sizeof x); }
inline void put_i32(std::vector<uint8_t>& v, int32_t  x) { put_bytes(v, &x, sizeof x); }
inline void put_i64(std::vector<uint8_t>& v, int64_t  x) { put_bytes(v, &x, sizeof x); }
inline void put_f64(std::vector<uint8_t>& v, double   x) { put_bytes(v, &x, sizeof x); }
inline void put_u8 (std::vector<uint8_t>& v, uint8_t  x) { v.push_back(x); }
// Unsigned LEB128: seven bits a byte, low group first, the high bit set on
// every byte but the last. Self-delimiting and one spelling per value, so a
// run of them stays injective with nothing between (the plan arm's schedule
// deltas, architect 2026-11-24).
inline void put_uleb128(std::vector<uint8_t>& v, uint64_t x) {
    while (x >= 0x80u) {
        v.push_back(static_cast<uint8_t>((x & 0x7fu) | 0x80u));
        x >>= 7;
    }
    v.push_back(static_cast<uint8_t>(x));
}
inline void put_str(std::vector<uint8_t>& v, const std::string& s) {
    // Full uint64_t length prefix, no narrowing: the encoding stays injective
    // over the whole accepted in-memory string domain, so no oversized
    // free-text value can wrap its prefix and boundary-splice two distinct
    // field partitions into the same byte stream.
    put_u64(v, static_cast<uint64_t>(s.size()));
    put_bytes(v, s.data(), s.size());
}

// FNV-1a, 64-bit. Used only to name files and bucket the in-memory maps; a
// collision degrades to a miss via the exact fingerprint-blob compare, never
// to a wrong hit.
uint64_t fnv1a64(const std::vector<uint8_t>& d) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : d) { h ^= b; h *= 1099511628211ull; }
    return h;
}

std::string hex16(uint64_t h) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return std::string(buf);
}

char hex_digit(uint8_t x) {
    return static_cast<char>(x < 10 ? ('0' + x) : ('a' + (x - 10)));
}

std::string hex_encode(const std::vector<uint8_t>& bytes) {
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(hex_digit(static_cast<uint8_t>(b >> 4)));
        out.push_back(hex_digit(static_cast<uint8_t>(b & 0x0f)));
    }
    return out;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hex_decode(const std::string& text, std::vector<uint8_t>& out) {
    if ((text.size() & 1u) != 0) return false;
    std::vector<uint8_t> decoded;
    decoded.reserve(text.size() / 2);
    for (size_t i = 0; i < text.size(); i += 2) {
        const int hi = hex_value(text[i]);
        const int lo = hex_value(text[i + 1]);
        if (hi < 0 || lo < 0) return false;
        decoded.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    out = std::move(decoded);
    return true;
}

bool parse_u64_exact(const std::string& text, uint64_t& out) {
    if (text.empty()) return false;
    uint64_t value = 0;
    const char* first = text.data();
    const char* last = first + text.size();
    const auto r = std::from_chars(first, last, value);
    if (r.ec != std::errc{} || r.ptr != last) return false;
    out = value;
    return true;
}

bool parse_i64_exact(const std::string& text, int64_t& out) {
    if (text.empty()) return false;
    int64_t value = 0;
    const char* first = text.data();
    const char* last = first + text.size();
    const auto r = std::from_chars(first, last, value);
    if (r.ec != std::errc{} || r.ptr != last) return false;
    out = value;
    return true;
}

bool parse_prefixed_u64(const std::string& line, const char* prefix,
                        uint64_t& out) {
    const std::string_view p(prefix);
    if (!line.starts_with(p)) return false;
    return parse_u64_exact(line.substr(p.size()), out);
}

bool parse_prefixed_i64(const std::string& line, const char* prefix,
                        int64_t& out) {
    const std::string_view p(prefix);
    if (!line.starts_with(p)) return false;
    return parse_i64_exact(line.substr(p.size()), out);
}

// Canonical RENDER-IDENTITY fingerprint: the FULL recipe — "would a fresh
// render of this recipe produce these bytes". Serializes, in order: the
// content version; the source path plus its load-time source identity; the
// sample rate; EVERY EngineSettings field — the five naming/provenance
// fields (title, bpm, notes, url, cover) included
// by ruling (architect 2026-07-17: they change about once per movement, so a
// provenance edit forcing a fresh render is accepted; the payoff is that no
// inert-field classification exists anywhere, and a re-render refreshes the
// artifact's attested .settings provenance); the trim bounds; and the marker
// part, THE ENGINE'S INPUT FOR THIS RENDER (architect 2026-11-24; the rule and
// its two arms at render_cache.h): untrimmed, resolve_warp_markers_for_render's
// survivors and the collapsed enabled reset positions; under a surviving plan,
// what plan_trim hands the engine and finish_render — the source view, the
// emit cap, the schedule, the crop and the translated, range-filtered resets —
// so two states normalization or the trim window proves render-identical share
// a key (under unconditional triggers this is what turns an inert marker
// edit's forced re-derive, or an edit outside what a trimmed render reads,
// into a cache hit). The key is a conservative over-approximation of byte identity,
// and that direction is the point: a match guarantees byte-identical output;
// a mismatch at worst re-renders redundantly. THE KEY NAMES AUTHORED STATE AND
// RECIPE ONLY — no library-environment term (2026-08-09, the reason at the
// version block below), so the guarantee holds WITHIN ONE LIBRARY EPOCH and a reuse
// that crosses a glibc or FFTW upgrade may differ at the accepted inaudible
// class rather than byte-exactly.
// CALLERS OWN THE RESOLVE AND THE PLAN: render_fingerprint is pure
// serialization and picks its arm from the plan pointer it is handed; each
// call site either threads an already-resolved product and its own trim_plan
// through (do_render) or runs its own resolve and its own plan_trim and
// accepts the resolver's per-resolve stderr lines
// (compute_live_render_fingerprint). GUI-only marker session scratch
// (iteration / BPM authoring) never reaches the resolver, so it is excluded
// by construction. The result is hashed to name a cache file and stored
// verbatim for an exact-compare confirm on lookup.
//
// THE FIRST BUMP RULE — THE KEY'S BYTES: ANY change to the key's byte SHAPE,
// or to the DOMAIN of a field it serializes, forces a fresh namespace, because
// a pre-change key and a post-change key naming different input could
// otherwise byte-collide. The trim-aware phase-reset component is the case
// that shows why a DOMAIN change counts (2026-08-01): a TRIMMED render
// serializes each SURVIVING reset's TRANSLATED query, the source-side value
// minus plan_trim's cut_begin. Had it been serialized in the source domain
// under the same spelling, a translated query P would byte-match an older
// artifact whose UNSHIFTED query was P (a late positive cut_begin: that reset
// sat before the window and never reached its engine at all), with the
// version byte, the trimmed flag and both bounds identical on either side.
// The persistent .fingerprint compare (fingerprint_sidecar_matches, below)
// and both reuse rungs — the target-view cache/artifact rungs and
// do_render's up-to-date rung — would then serve that old audio for a
// genuinely DIFFERENT reset schedule. That is the one unsafe direction, and a
// fresh namespace is exactly what the version byte is for.
// WHY THE trim_window_is_full PRECEDENT DOES NOT REACH HERE: that one keeps
// the OLD BYTES for an UNCHANGED meaning, so its reuse is byte-identical by
// construction. A same-spelling-different-domain change is the opposite
// arrangement: reasoning only over new keys ("the key drops information only
// where that information cannot reach the engine") is true of two new keys
// and silent about old sidecars written under the other domain.
// ACCEPTED COST of every bump: every pre-bump cache entry and .fingerprint
// sidecar goes unreachable, full-window ones included — a one-time cold
// cache, aged out by the LRU budget on the cache side, warning-only on the
// sidecar side (single user, tmp/ transient).
//
// NO LIBRARY-ENVIRONMENT TERM (architect 2026-08-09): the key names authored
// state and recipe only, so a library upgrade does not move it and a reused
// preview may cross one. With -ffp-contract=off in the build, library drift
// is measured and bounded at inaudible classes (the -38 dBFS / two-samples
// limiter class), and anything material fails loudly through the limiter's
// knife-edge rather than silently — the argument that keeps attestation keys
// out of the .settings file (settings_file.cpp's retired-key record) keeps
// this term out identically. Byte-comparison across a library epoch is the
// user's own discipline: re-render what you keep comparing.
//
// THE SECOND BUMP RULE — DSP IDENTITY (architect 2026-09-02; stated here as the
// rule's authoritative home, its origin being perf_campaign_2026_07.md's
// "Reference-byte changes are legal and ride kFingerprintVersion bumps" and the
// §15 line "kFingerprintVersion encodes DSP recipe identity, NOT host
// identity"). The rule above is about the key's byte SHAPE; this one is about
// the DSP behind it. ANY REFERENCE-BYTE CHANGE BELOW THE KEY — a change that
// alters what a given recipe renders, in src/engine/ or src/prepost/, and
// equally in the parser's authored -> engine compilation, which the untrimmed
// arm does not name (it carries the resolved markers and SOURCE-domain reset
// frames; the map and the S - N/2 emission are the parser's) — RIDES A
// kFingerprintVersion BUMP. The key is the RECIPE and nothing about the code
// that turned it into samples, so a post-fix key byte-equals the pre-fix key
// of the same recipe. (The plan arm carries compiled values — the schedule,
// the emit cap — so a compilation change may move it on its own; the bump is
// owed all the same, for the untrimmed arm and for the engine below both.)
// In-process reuse is harmless there: RenderCache lives in a per-PID directory
// created at init and removed at shutdown (orphans swept at launch), so no
// cache entry outlives the binary that wrote it. THE .fingerprint SIDECAR ON
// DISK SURVIVES A REBUILD, and three rungs trust it — do_render's UP-TO-DATE
// rung (the final_output_path compare that returns "reused_up_to_date" having
// rendered nothing), do_render's PROJECT-ARTIFACT rung (the deliverable byte
// copy) and GuiTargetRender's ARTIFACT rung (the deliverable read straight into
// the target-view preview buffer), all three through
// fingerprint_sidecar_matches below. So without the bump an engine fix serves
// the PRE-FIX deliverable as "up to date" AND paints it as the target-view
// preview: the fix looks like it never landed. The bump is the whole remedy —
// a fresh namespace, every pre-bump sidecar unreachable, the same accepted
// cost the entries above already take.
// RECORDED RESIDUAL: the reuse is TOOLCHAIN-BLIND (no library-environment
// term, above). A sidecar written before a
// `pacman -Syu` that moves libm or FFTW can hand back the previous toolchain's
// bytes — the accepted inaudible class, and the reason the byte-reproducibility
// claim in perf_campaign_2026_07.md §15 is per toolchain and per host (that
// section's own gap: an artifact migrated from another machine poses as a
// current recipe; re-render rather than migrate).
constexpr uint32_t kFingerprintVersion = 21;
constexpr char     kSidecarMagic[]     = "WARPTEMPO_RENDER_FINGERPRINT";
// The sidecar_layout line versions the on-disk TEXT CONTAINER of the sidecar
// file itself — its line count and their grammar — never the recipe: the
// fingerprint content version is serialized inside the fingerprint payload by
// render_fingerprint. LAYOUT 2 (architect 2026-11-05) JOINED THE SIXTH LINE,
// `limiter=`, the spectral limiter's engaged frames (render_cache.h's THE
// SIDECAR'S SIX LINES); the strict reader wants exactly six lines under this
// number, so every layout-1 record is a miss at the deliverable's three reuse
// rungs and re-renders once, rewriting wav and sidecar together — accepted,
// nothing having been rendered for the archive yet.
constexpr uint32_t kSidecarVersion     = 2;
constexpr char     kSidecarLimiterPrefix[] = "limiter=";

// The limiter line's text after its prefix: the frames as decimal, single
// spaces between, nothing at all for an empty list (the grammar at
// render_cache.h's THE SIDECAR'S SIX LINES).
std::string format_limiter_frames(const std::vector<int64_t>& frames) {
    std::string out;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (i) out += ' ';
        out += std::to_string(frames[i]);
    }
    return out;
}

// The limiter line's strict reader: the prefix, then either nothing or
// single-space-separated non-negative int64 frames in STRICTLY ascending
// order (the list is a sorted set of places; a repeat or a step back is a
// shape the writer never produces). Any other shape — a leading, trailing or
// doubled space, a sign, a non-digit — is false with out untouched.
bool parse_limiter_line(const std::string& line, std::vector<int64_t>& out) {
    const std::string_view prefix(kSidecarLimiterPrefix);
    if (!line.starts_with(prefix)) return false;
    const std::string_view text(line.data() + prefix.size(),
                                line.size() - prefix.size());
    std::vector<int64_t> frames;
    if (text.empty()) {
        out = std::move(frames);
        return true;
    }
    const char* cur = text.data();
    const char* const end = cur + text.size();
    int64_t prev = -1;
    while (true) {
        // Digits only: from_chars would admit a leading '-' ("-0" reads as
        // 0), a sign the writer never produces.
        if (*cur < '0' || *cur > '9') return false;
        int64_t value = 0;
        const auto r = std::from_chars(cur, end, value);
        if (r.ec != std::errc{} || r.ptr == cur) return false;
        if (value <= prev) return false;   // strictly ascending
        frames.push_back(value);
        prev = value;
        cur = r.ptr;
        if (cur == end) break;
        if (*cur != ' ') return false;
        ++cur;
        if (cur == end) return false;      // a trailing space
    }
    out = std::move(frames);
    return true;
}

bool write_bytes_to_path(const std::string& path, const std::vector<char>& bytes) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    bool ok = false;
    do {
        if (!bytes.empty() &&
            std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) {
            break;
        }
        if (std::fflush(f) != 0) break;
        if (::fsync(::fileno(f)) != 0) break;
        ok = true;
    } while (false);

    if (std::fclose(f) != 0) ok = false;
    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    return ok;
}

} // namespace

bool stat_artifact_identity(const std::string& path,
                            ArtifactStatIdentity& out) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return false;
    if (st.st_size < 0) return false;
    constexpr int64_t kNsecPerSec = 1000000000ll;
    const int64_t sec = static_cast<int64_t>(st.st_mtim.tv_sec);
    if (sec > std::numeric_limits<int64_t>::max() / kNsecPerSec ||
        sec < std::numeric_limits<int64_t>::min() / kNsecPerSec) {
        return false;
    }
    const int64_t nsec = static_cast<int64_t>(st.st_mtim.tv_nsec);
    const int64_t base = sec * kNsecPerSec;
    if (nsec < 0 || nsec >= kNsecPerSec ||
        base > std::numeric_limits<int64_t>::max() - nsec) {
        return false;
    }
    out.dev      = static_cast<uint64_t>(st.st_dev);
    out.inode    = static_cast<uint64_t>(st.st_ino);
    out.size     = static_cast<uint64_t>(st.st_size);
    out.mtime_ns = base + nsec;
    return true;
}

bool stat_file_identity(const std::string& path, RenderFileIdentity& out) {
    // The fingerprint's source trust boundary is size+mtime only (header
    // ruling); the full-identity stat above is the one stat implementation.
    ArtifactStatIdentity full;
    if (!stat_artifact_identity(path, full)) return false;
    out.size = full.size;
    out.mtime = full.mtime_ns;
    return true;
}

std::vector<uint8_t> render_fingerprint(
        const std::string& source_audio_path,
        const RenderFileIdentity& source_identity,
        int sample_rate,
        const std::vector<MarkerForRender>& resolved_warp_markers,
        const std::vector<double>& phase_reset_source_frames,
        const EngineSettings& s,
        bool trimmed, int64_t trim_begin_frame, int64_t trim_end_frame,
        const TrimPlan* trim_plan) {
    std::vector<uint8_t> fp;
    fp.reserve(trim_plan
        ? 256 + trim_plan->pre.source_frame_schedule.size() * 2
        : 256 + resolved_warp_markers.size() * 64);

    put_u32(fp, kFingerprintVersion);

    put_str(fp, source_audio_path);
    put_bytes(fp, &source_identity.size, sizeof source_identity.size);
    put_bytes(fp, &source_identity.mtime, sizeof source_identity.mtime);
    put_i32(fp, static_cast<int32_t>(sample_rate));

    // Engine settings: EVERY field serializes — the full-recipe key (ruling
    // above), so no field carries an inert/live classification. The
    // exhaustive switch still forces a human decision here when the schema
    // grows: it has no default (a new EngineField enumerator draws -Wswitch),
    // and the static_assert below fails the build the moment EngineSettings
    // gains a field (the canonical-key addition recipe touches both), so a new
    // field's encoding and order must be chosen at this switch.
    // THE PROVENANCE FIELDS ARE KEY MEMBERS, AND THAT COST IS ACCEPTED
    // (recorded 2026-09-02). Of the six,
    // only `scale` reaches the engine; title, bpm, notes, url and cover are
    // informational (engine_settings.h says so at each field), so editing one
    // of them invalidates every cached artifact and the next render spends the
    // full synthesis to produce byte-identical audio. It is the price of the
    // full-recipe key ruled above and it is deliberately not bought off with a
    // per-field live/inert classification: such a split would have to be
    // re-proved at every schema change, and this exhaustive switch exists
    // precisely so no field is ever quietly assumed inert.
    static_assert(sizeof(EngineSettings) ==
                      5 * sizeof(std::string) + sizeof(double),
                  "EngineSettings changed: decide the new field's render-byte "
                  "role in render_fingerprint's per-field switch, then update "
                  "this size expression");
    constexpr EngineField kEngineFieldKeyOrder[] = {
        EngineField::Title, EngineField::Scale, EngineField::Bpm,
        EngineField::Notes, EngineField::Url,   EngineField::Cover,
    };
    for (const EngineField field : kEngineFieldKeyOrder) {
        switch (field) {  // no default: a new enumerator must be decided here
            case EngineField::Title: put_str(fp, s.title); break;
            case EngineField::Scale: put_f64(fp, s.scale); break;
            case EngineField::Bpm:   put_str(fp, s.bpm);   break;
            case EngineField::Notes: put_str(fp, s.notes); break;
            case EngineField::Url:   put_str(fp, s.url);   break;
            case EngineField::Cover: put_str(fp, s.cover); break;
        }
    }

    // Trim (architect 2026-07-30): a NOT-trimmed render — the FULL window
    // [0, total-1], the caller's trim_window_is_full verdict arriving here as
    // `trimmed == false` — renders untrimmed and writes the UNSET TRIM BYTES
    // (has-byte 0 + f64 0.0 on both sides), so every spelling of "render the
    // whole thing" hashes the same. A proper SUB-WINDOW writes the two
    // authored frames: two has-bytes of 1 and
    // the int64 bounds widened to the f64 encoding (exact — whole frames sit far
    // below 2^53). The sub-window bounds serialize verbatim even when plan_trim
    // will refuse them and the render falls back to untrimmed — accepted
    // conservatism, recorded at do_render's trim-plan block.
    put_u8 (fp, trimmed ? 1 : 0);
    put_f64(fp, trimmed ? static_cast<double>(trim_begin_frame) : 0.0);
    put_u8 (fp, trimmed ? 1 : 0);
    put_f64(fp, trimmed ? static_cast<double>(trim_end_frame) : 0.0);

    // THE ARM BYTE (architect 2026-11-24): 0 = the untrimmed arm (the
    // resolved list), 1 = the plan's engine inputs, ahead of the warp part in
    // both. Everything before it is fixed-width or length-prefixed, so it
    // stands at the same offset in any two keys whose prefixes agree, and two
    // keys of different arms differ there. The trimmed flag above cannot do
    // this job: a refused sub-window is trimmed = 1 under the untrimmed arm.
    put_u8(fp, trim_plan ? 1 : 0);
    if (!trim_plan) {
        // Warp markers, THE UNTRIMMED ARM: the RESOLVED render list — exactly
        // the MarkerForRender fields build_warp_frame_map reads (frame,
        // resolved owning tempo cents, typed scale, label def/ref), after
        // resolve_warp_markers_for_render's
        // filter/coincidence/seed/materialize/normalize pipeline. Raw disabled
        // markers, a coincident member's authored tempo and labels (its plain
        // 1.00 owner reaches the list instead, those on one exact frame as
        // one; architect 2026-11-17 — so a change to the coincidence rule
        // moves this arm through this list alone, no version bump), and fields
        // materialization discards never reach this list, so edits that
        // cannot change engine input cannot move the key. All survivors are
        // enabled by the resolver's output invariants — no disabled flag
        // exists here.
        // THE LABEL STRINGS ARE ENGINE INPUT IN THIS ARM, AND A CONSISTENT
        // RENAME BUYS NOTHING (recorded 2026-09-02, item Q, accepted): a
        // `label_ref` takes its `label_def`'s segment duration in
        // build_warp_frame_map, so the names do select the map and belong in
        // the key — but renaming BOTH sides of a pair yields the identical map
        // and identical output bytes while moving this key, so the next render
        // redoes the whole synthesis to land exactly where it already was. A
        // cost, not a defect, on the same reasoning as the provenance fields
        // above. (The plan arm below carries no names: there a consistent
        // rename moves nothing the engine reads and reuses.)
        put_u32(fp, static_cast<uint32_t>(resolved_warp_markers.size()));
        for (const auto& m : resolved_warp_markers) {
            put_i64(fp, m.time_frame);
            put_i64(fp, m.tempo_cents);
            // Optional typed scale: presence flag then the value (0.0 filler
            // when absent, so an absent scale and a hypothetical 0.0 cannot
            // collide — 0.0 is unparseable as a typed scale anyway).
            put_u8 (fp, m.tempo_scale.has_value() ? 1 : 0);
            put_f64(fp, m.tempo_scale.value_or(0.0));
            put_str(fp, m.label_def);
            put_str(fp, m.label_ref);
        }
    } else {
        // THE PLAN ARM (architect 2026-11-24): what the trimmed render reads
        // in place of the marker list, in this order — the source view
        // [pre.begin_frame, pre.begin_frame + pre.frames); the emit cap,
        // llrint of the translated map's last anchor target, the ONE value
        // the engine reads from the map under a supplied schedule (its
        // target_total_frames and emit_sample_cap, engine.cpp), keyed as the
        // rounded number the engine reads rather than the double, since a
        // marker just past the cut's end can move the double by an ulp
        // without moving a byte; the source frame schedule; and the post crop
        // finish_render applies (post.begin_sample, post.samples). The map's
        // other anchors feed only the engine's strict-ascent validator, which
        // refuses and never shapes a byte.
        // THE SCHEDULE GOES IN WHOLE, LOSSLESSLY — the key is stored verbatim
        // and exact-compared (the cache's confirm, the sidecar's hex line), so
        // no digest stands in for it: the entry count as u64, the first entry
        // as i64, then each later entry as the unsigned LEB128 of its step
        // from the one before. The schedule is nondecreasing (the engine's own
        // shape check), so a step is about R_s (1024) source frames, two bytes
        // — some 90 bytes a second of trimmed audio. The step is taken in
        // uint64_t, wrapping, so the encoding stays injective (the first entry
        // and the wrapped steps recover every entry exactly) even for a step
        // the schedule never makes, and no signed overflow is possible.
        const PreTrim& pre = trim_plan->pre;
        put_i64(fp, pre.begin_frame);
        put_i64(fp, pre.frames);
        put_i64(fp, static_cast<int64_t>(
            std::llrint(pre.warp_frame_map.back().tgt_frame)));
        const std::vector<int64_t>& schedule = pre.source_frame_schedule;
        put_u64(fp, static_cast<uint64_t>(schedule.size()));
        if (!schedule.empty()) {
            put_i64(fp, schedule.front());
            for (size_t m = 1; m < schedule.size(); ++m) {
                put_uleb128(fp, static_cast<uint64_t>(schedule[m]) -
                                    static_cast<uint64_t>(schedule[m - 1]));
            }
        }
        put_i64(fp, trim_plan->post.begin_sample);
        put_i64(fp, trim_plan->post.samples);
    }

    // Phase resets: the positions THE ENGINE RECEIVES for this render, by the
    // same arm. Untrimmed that is the resolved authored intermediate
    // build_phase_reset_source_frames emits, with disabled resets and
    // collapsed equal-frame duplicates already gone; under a surviving trim
    // plan it is plan_trim's translated, range-filtered list, so out-of-window
    // resets — which the trimmer keeps out of the engine entirely — cannot
    // move the key.
    // THE INT64 ENCODING STAYS EXACT ACROSS BOTH SPELLINGS: the authored form
    // is whole source frames (int64 positions widened exactly), and the
    // trimmed form is S - N/2 - cut_begin — a whole source frame minus the
    // integer 2048 (kN is even) minus the trimmer's integer source cut — so
    // it is whole too, negative near the head and still exact. No rounding
    // occurs in either arm. The two arms serialize different domains, and the
    // arm byte above keeps them apart.
    const std::vector<double>& phase_reset_engine_frames =
        trim_plan ? trim_plan->pre.phase_reset_frame_map
                  : phase_reset_source_frames;
    put_u32(fp, static_cast<uint32_t>(phase_reset_engine_frames.size()));
    for (const double p : phase_reset_engine_frames) {
        put_i64(fp, static_cast<int64_t>(p));
    }

    return fp;
}

std::string fingerprint_sidecar_path(const std::string& wav_path) {
    std::filesystem::path p(wav_path);
    p.replace_extension(kFingerprintSidecarExtension);
    return p.string();
}

bool write_fingerprint_sidecar(const std::string& wav_path,
                               const std::vector<uint8_t>& fingerprint,
                               const std::vector<int64_t>& limiter_frames) {
    RenderFileIdentity wav_identity;
    if (!stat_file_identity(wav_path, wav_identity)) return false;

    const std::string sidecar_path = fingerprint_sidecar_path(wav_path);
    const std::string tmp_path = sidecar_path + ".tmp";
    std::string data;
    data.reserve(128 + fingerprint.size() * 2 + limiter_frames.size() * 12);
    data += kSidecarMagic;
    data += '\n';
    data += "sidecar_layout=";
    data += std::to_string(kSidecarVersion);
    data += '\n';
    data += "size=";
    data += std::to_string(wav_identity.size);
    data += '\n';
    data += "mtime=";
    data += std::to_string(wav_identity.mtime);
    data += '\n';
    data += "fingerprint=";
    data += hex_encode(fingerprint);
    data += '\n';
    // The sixth line, last (layout 2): the limiter's list.
    data += kSidecarLimiterPrefix;
    data += format_limiter_frames(limiter_frames);
    data += '\n';

    std::FILE* f = std::fopen(tmp_path.c_str(), "wb");
    if (!f) return false;

    bool ok = false;
    do {
        if (!data.empty() &&
            std::fwrite(data.data(), 1, data.size(), f) != data.size())
            break;
        if (std::fflush(f) != 0) break;
        if (::fsync(::fileno(f)) != 0) break;
        ok = true;
    } while (false);

    if (std::fclose(f) != 0) ok = false;
    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, sidecar_path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }
    return true;
}

bool read_fingerprint_sidecar(const std::string& wav_path,
                              FingerprintSidecarRecord& out) {
    // One stat serves both the sidecar's size/mtime validation and the
    // TOCTOU capture handed back in the record's identity: with concurrent
    // GUI/CLI processes publishing the same title, the wav can be atomically
    // replaced between this validation and the caller's later read/copy
    // while the not-yet-replaced sidecar still validates — atomic
    // publication of each file does not make the wav/sidecar PAIR atomic.
    // The consumer therefore re-stats AFTER its read/copy and compares all
    // four fields against this capture; a mismatch discards the result as a
    // miss (no locks, no retries — a mismatch means concurrent publication,
    // and one wasted read falling back to synthesis is the whole remedy).
    ArtifactStatIdentity full_identity;
    if (!stat_artifact_identity(wav_path, full_identity)) return false;
    RenderFileIdentity wav_identity;
    wav_identity.size = full_identity.size;
    wav_identity.mtime = full_identity.mtime_ns;

    std::ifstream in(fingerprint_sidecar_path(wav_path), std::ios::binary);
    if (!in) return false;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(std::move(line));
    }
    if (!in.eof()) return false;
    if (lines.size() != 6) return false;
    if (lines[0] != kSidecarMagic) return false;
    // Composed from kSidecarVersion exactly as the writer composes it, so a
    // future layout bump moves reader and writer together instead of silently
    // accepting a file the writer does not produce (a hardcoded spelling
    // here would drift).
    if (lines[1] != "sidecar_layout=" + std::to_string(kSidecarVersion))
        return false;

    uint64_t recorded_size = 0;
    int64_t recorded_mtime = 0;
    if (!parse_prefixed_u64(lines[2], "size=", recorded_size)) return false;
    if (!parse_prefixed_i64(lines[3], "mtime=", recorded_mtime)) return false;
    if (recorded_size != wav_identity.size ||
        recorded_mtime != wav_identity.mtime) {
        return false;
    }

    constexpr std::string_view fp_prefix = "fingerprint=";
    if (!lines[4].starts_with(fp_prefix)) return false;
    FingerprintSidecarRecord record;
    if (!hex_decode(lines[4].substr(fp_prefix.size()), record.fingerprint)) {
        return false;
    }
    if (!parse_limiter_line(lines[5], record.limiter_frames)) return false;
    record.identity = full_identity;
    out = std::move(record);
    return true;
}

bool fingerprint_sidecar_matches(const std::string& wav_path,
                                 const std::vector<uint8_t>& fingerprint,
                                 ArtifactStatIdentity* out_identity,
                                 std::vector<int64_t>* out_limiter_frames) {
    FingerprintSidecarRecord record;
    if (!read_fingerprint_sidecar(wav_path, record)) return false;
    if (record.fingerprint != fingerprint) return false;
    if (out_identity) *out_identity = record.identity;
    if (out_limiter_frames) *out_limiter_frames = std::move(record.limiter_frames);
    return true;
}

void RenderCache::init() {
    enabled_ = false;

    // THE CACHE HOME IS XDG_CACHE_HOME AND NOTHING ELSE (architect
    // 2026-10-30: the tablet is the one platform): android_main points it at
    // the app's private directory before gui_main runs, so its absence is a
    // program fault in this binary — one line and a hard fail, no fallback
    // (no backstops for a state the product cannot produce).
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    if (!xdg || !xdg[0]) {
        std::fprintf(stderr,
                     "warptempo_gui: XDG_CACHE_HOME is not set; android_main "
                     "sets it before the GUI starts\n");
        std::abort();
    }
    const std::string base = xdg;

    parent_ = base + "/warptempo_gui";
    std::error_code ec;
    std::filesystem::create_directories(parent_, ec);
    if (ec) return;

    sweep_orphans();

    dir_ = parent_ + "/" + std::to_string(static_cast<long>(::getpid()));
    std::filesystem::create_directories(dir_, ec);
    if (ec) return;

    enabled_ = true;
}

void RenderCache::sweep_orphans() {
    // NON-THROWING (directory_walk.h): an unreadable or vanishing cache parent
    // sweeps nothing rather than terminating the process, which is what a
    // best-effort sweep at startup owes.
    std::error_code ec;
    const long self = static_cast<long>(::getpid());
    for_each_directory_entry(parent_, ec, [self](
            const std::filesystem::directory_entry& de) {
        std::error_code entry_ec;
        if (!de.is_directory(entry_ec) || entry_ec) return;

        const std::string name = de.path().filename().string();
        if (name.empty() ||
            name.find_first_not_of("0123456789") != std::string::npos) {
            return; // not a PID directory
        }
        long pid = 0;
        try { pid = std::stol(name); } catch (...) { return; }
        if (pid == self) return;

        // kill(pid, 0): 0 or EPERM means the process is alive (leave it);
        // ESRCH means it is gone (sweep its directory).
        if (::kill(static_cast<pid_t>(pid), 0) == 0) return;
        if (errno != ESRCH) return;

        std::error_code rmec;
        std::filesystem::remove_all(de.path(), rmec);
    });
}

void RenderCache::shutdown() {
    join_writer();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        disk_index_.clear();
        disk_bytes_ = 0;
    }
    if (!dir_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    enabled_ = false;
}

std::string RenderCache::process_dir() const {
    return enabled_ ? dir_ : std::string();
}

void RenderCache::join_writer() {
    std::thread local;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        local = std::move(writer_);
    }
    if (local.joinable()) local.join();
}

bool RenderCache::lookup(const std::vector<uint8_t>& fp,
                         int channels, int sample_rate,
                         std::vector<float>& out,
                         std::vector<int64_t>* out_limiter_frames) {
    if (!enabled_) {
        return false;
    }
    const uint64_t h = fnv1a64(fp);

    DiskEntry candidate;
    bool have_candidate = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = disk_index_.find(h); it != disk_index_.end() &&
            it->second.fingerprint == fp) {
            candidate = it->second;
            have_candidate = true;
        }
    }

    if (have_candidate) {
        const std::string path = dir_ + "/" + candidate.filename;
        // The potentially large wav read happens outside mutex_. If
        // eviction or failed validation removes the pair mid-read, the reader
        // reports a miss and the caller re-renders.
        if (read_file(path, fp, channels, sample_rate, out,
                      out_limiter_frames)) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = disk_index_.find(h); it != disk_index_.end() &&
                it->second.filename == candidate.filename &&
                it->second.fingerprint == fp) {
                it->second.seq = ++lru_seq_;
            }
            return true;
        }

        bool drop_pair = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Match the success-path re-check before mutating the disk index.
            if (auto it = disk_index_.find(h); it != disk_index_.end() &&
                it->second.filename == candidate.filename &&
                it->second.fingerprint == fp) {
                disk_bytes_ -= std::min(disk_bytes_, it->second.size_bytes);
                disk_index_.erase(it);
                drop_pair = true;
            }
        }
        if (drop_pair) remove_disk_pair(path);
    }

    return false;
}

bool RenderCache::publish_wav(const std::vector<uint8_t>& fp,
                              const std::string& staging_path,
                              std::vector<int64_t>* out_limiter_frames) {
    // The disk-tier publish keys on the fingerprint alone: it confirms via the
    // .fingerprint sidecar (the fingerprint already binds sample rate), then
    // byte-copies the entry — no format check.
    if (!enabled_) return false;
    const uint64_t h = fnv1a64(fp);

    DiskEntry candidate;
    bool have_candidate = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = disk_index_.find(h); it != disk_index_.end() &&
            it->second.fingerprint == fp) {
            candidate = it->second;
            have_candidate = true;
        }
    }
    if (!have_candidate) return false;

    const std::string path = dir_ + "/" + candidate.filename;
    bool copied = false;
    ArtifactStatIdentity candidate_identity{};
    std::vector<int64_t> candidate_limiter_frames;
    if (fingerprint_sidecar_matches(path, fp, &candidate_identity,
                                    &candidate_limiter_frames)) {
        std::error_code ec;
        std::filesystem::copy_file(
            path, staging_path,
            std::filesystem::copy_options::overwrite_existing, ec);
        // Identity bind (TOCTOU): the copy read the candidate wav, so re-stat
        // it AFTER the copy completes and require the exact object the sidecar
        // validation saw (dev, inode, size, mtime_ns — the capture at
        // fingerprint_sidecar_matches). The racing party is this process's own
        // cache writer thread: under a 64-bit FNV name collision it can
        // remove/replace this hash-named wav for a distinct fingerprint
        // between validation and copy. A mismatch keeps copied false and
        // discards the staged copy exactly as a miss (the fall-through below
        // removes the staging file; the next rung / synthesis is the whole
        // remedy — no locks, no retries).
        if (!ec) {
            ArtifactStatIdentity post_copy{};
            copied = stat_artifact_identity(path, post_copy) &&
                     post_copy == candidate_identity;
        }
    }
    if (copied) {
        // The list goes out with the bytes it was written beside (the
        // validated pair's own line), never on a discarded copy.
        if (out_limiter_frames) {
            *out_limiter_frames = std::move(candidate_limiter_frames);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = disk_index_.find(h); it != disk_index_.end() &&
            it->second.filename == candidate.filename &&
            it->second.fingerprint == fp) {
            it->second.seq = ++lru_seq_;
        }
        return true;
    }

    bool drop_pair = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Match the success-path re-check before mutating the disk index.
        if (auto it = disk_index_.find(h); it != disk_index_.end() &&
            it->second.filename == candidate.filename &&
            it->second.fingerprint == fp) {
            disk_bytes_ -= std::min(disk_bytes_, it->second.size_bytes);
            disk_index_.erase(it);
            drop_pair = true;
        }
    }
    if (drop_pair) remove_disk_pair(path);
    std::error_code ec;
    std::filesystem::remove(staging_path, ec);
    return false;
}

struct RenderCache::WriterJob {
    enum class Kind {
        WriteBlobToDisk,
        EncodeMasterThenRoute,
    };

    Kind kind = Kind::WriteBlobToDisk;
    uint64_t h = 0;
    std::vector<uint8_t> fp;
    std::string fname;
    std::string path;
    std::vector<char> blob;
    std::vector<float> samples;
    int channels = 0;
    int sample_rate = 0;
    int64_t frame_count = 0;
    // The limiter's engaged frames in the blob's own domain, written as the
    // pair's sidecar line (the contract at RenderCache::insert): the list
    // travels with the blob, so a dropped job drops both.
    std::vector<int64_t> limiter_frames;
    // EncodeMasterThenRoute only: the dispatching render's per-dispatch
    // session cancel token (never reset after creation, so a load of it
    // names exactly that session). Null for WriteBlobToDisk jobs.
    std::shared_ptr<const std::atomic<bool>> cancel_token;
};

void RenderCache::insert(const std::vector<uint8_t>& fp,
                         const std::vector<char>& blob,
                         int channels, int sample_rate, int64_t frame_count,
                         const std::vector<int64_t>& limiter_frames) {
    if (!enabled_) {
        return;
    }
    if (frame_count <= 0 || blob.empty() || channels <= 0 || sample_rate <= 0) {
        return;
    }

    const uint64_t h = fnv1a64(fp);
    insert_disk(h, fp, blob, frame_count, limiter_frames);
}

void RenderCache::insert_master_floats(
        const std::vector<uint8_t>& fp,
        const std::vector<float>& samples,
        int channels, int sample_rate,
        int64_t frame_count,
        std::shared_ptr<const std::atomic<bool>> cancel_token,
        const std::vector<int64_t>& limiter_frames) {
    if (!enabled_) {
        return;
    }
    if (frame_count <= 0 || samples.empty() ||
        channels <= 0 || sample_rate <= 0) {
        return;
    }
    const size_t ch = static_cast<size_t>(channels);
    if (samples.size() % ch != 0) return;

    const uint64_t h = fnv1a64(fp);
    WriterJob job;
    job.kind = WriterJob::Kind::EncodeMasterThenRoute;
    job.h = h;
    job.fp = fp;
    job.fname = hex16(h) + ".wav";
    job.path = dir_ + "/" + job.fname;
    job.samples = samples;
    job.channels = channels;
    job.sample_rate = sample_rate;
    job.frame_count = frame_count;
    job.limiter_frames = limiter_frames;
    job.cancel_token = std::move(cancel_token);
    start_writer_job(std::move(job));
}

bool RenderCache::insert_disk(uint64_t h, const std::vector<uint8_t>& fp,
                              const std::vector<char>& blob,
                              int64_t frame_count,
                              const std::vector<int64_t>& limiter_frames) {
    WriterJob job;
    job.kind = WriterJob::Kind::WriteBlobToDisk;
    job.h = h;
    job.fp = fp;
    job.fname = hex16(h) + ".wav";
    job.path = dir_ + "/" + job.fname;
    job.blob = blob;
    job.frame_count = frame_count;
    job.limiter_frames = limiter_frames;
    start_writer_job(std::move(job));
    return true;
}

void RenderCache::start_writer_job(WriterJob job) {
    join_writer();

    // Drop the job if the dispatching render was cancelled while we waited
    // on the previous writer. The check sits after the potentially long
    // join and before anything becomes externally observable — the disk
    // index mutation below and the writer thread launch. A cancel can still
    // land between this load and the thread start; the writer thread's
    // post-encode re-check of the same job token catches that window, so
    // the two checks together keep a killed session from publishing cache
    // state.
    if (job.cancel_token && job.cancel_token->load()) {
        return;
    }

    if (job.kind == WriterJob::Kind::WriteBlobToDisk) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = disk_index_.find(job.h); it != disk_index_.end()) {
                disk_bytes_ -= std::min(disk_bytes_, it->second.size_bytes);
                disk_index_.erase(it);
            }
        }
        remove_disk_pair(job.path);
    }

    std::thread new_writer([this, job = std::move(job)]() mutable {
        auto write_disk_blob = [this, &job]() {
            uint64_t written = 0;
            if (!write_file(job.path, job.fp, job.blob, job.frame_count,
                            job.limiter_frames, written)) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                DiskEntry e;
                e.fingerprint = job.fp;
                e.filename    = job.fname;
                e.size_bytes  = written;
                e.seq         = ++lru_seq_;
                disk_index_[job.h] = std::move(e);
                disk_bytes_   += written;
                evict_disk_until(kDiskBudgetBytes);
            }
        };

        if (job.kind == WriterJob::Kind::EncodeMasterThenRoute) {
            std::vector<char> encoded;
            if (!encode_pcm24_wav_blob(job.samples, job.channels,
                                       job.sample_rate, encoded)) {
                std::fprintf(stderr,
                    "warptempo_gui: render-cache insert dropped: Failed to "
                    "encode target samples as canonical PCM_24 WAV\n");
                return;
            }
            job.samples.clear();
            job.samples.shrink_to_fit();
            job.blob = std::move(encoded);

            // Post-encode cancellation re-check, before anything publishes:
            // the disk-index mutation and the pair write all come after this
            // point. The token is created per dispatch and never reset, so
            // this load names exactly the dispatching session; a cancel
            // landing anywhere before this point —
            // including the window between the handoff's pre-launch check
            // and this thread's start — drops the entry silently
            // (consistent with the pre-launch drop), and a cancel landing
            // after it completes the write, which is the accepted
            // late-cancel tail, now bounded by this check.
            if (job.cancel_token && job.cancel_token->load()) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (auto it = disk_index_.find(job.h);
                    it != disk_index_.end()) {
                    disk_bytes_ -= std::min(disk_bytes_, it->second.size_bytes);
                    disk_index_.erase(it);
                }
            }
            remove_disk_pair(job.path);
            write_disk_blob();
            return;
        }

        write_disk_blob();
    });
    {
        std::lock_guard<std::mutex> lock(mutex_);
        writer_ = std::move(new_writer);
    }
}

void RenderCache::evict_disk_until(uint64_t target_max) {
    while (disk_bytes_ > target_max && disk_index_.size() > 1) {
        auto victim = disk_index_.begin();
        for (auto it = disk_index_.begin(); it != disk_index_.end(); ++it)
            if (it->second.seq < victim->second.seq) victim = it;
        remove_disk_pair(dir_ + "/" + victim->second.filename);
        disk_bytes_ -= victim->second.size_bytes;
        disk_index_.erase(victim);
    }
}

void RenderCache::remove_disk_pair(const std::string& wav_path) {
    std::error_code ec;
    std::filesystem::remove(wav_path, ec);
    std::filesystem::remove(wav_path + ".tmp", ec);
    const std::string sidecar = fingerprint_sidecar_path(wav_path);
    std::filesystem::remove(sidecar, ec);
    std::filesystem::remove(sidecar + ".tmp", ec);
}

bool RenderCache::read_file(const std::string& path,
                            const std::vector<uint8_t>& want_fp,
                            int channels, int sample_rate,
                            std::vector<float>& out,
                            std::vector<int64_t>* out_limiter_frames) {
    ArtifactStatIdentity candidate_identity{};
    std::vector<int64_t> candidate_limiter_frames;
    if (!fingerprint_sidecar_matches(path, want_fp, &candidate_identity,
                                     &candidate_limiter_frames)) {
        return false;
    }
    if (read_wav_to_float(path, channels, sample_rate, out)) {
        // Identity bind (TOCTOU): re-stat AFTER the decode and require the
        // exact wav object the sidecar validation saw (dev, inode, size,
        // mtime_ns — the capture at fingerprint_sidecar_matches). The racing
        // party is this process's own cache writer thread: under a 64-bit FNV
        // name collision it can remove/replace this hash-named wav for a
        // distinct fingerprint between validation and decode. A mismatch
        // discards the decoded buffer exactly as a miss (silent — the caller
        // re-renders; no locks, no retries).
        ArtifactStatIdentity post_read{};
        if (stat_artifact_identity(path, post_read) &&
            post_read == candidate_identity) {
            // The list goes out with the samples it was written beside
            // (the validated pair's own line), never on a discarded read.
            if (out_limiter_frames) {
                *out_limiter_frames = std::move(candidate_limiter_frames);
            }
            return true;
        }
    }
    out.clear();
    return false;
}

bool read_wav_to_float(const std::string& path,
                       int expected_channels, int expected_sample_rate,
                       std::vector<float>& out) {
    if (expected_channels <= 0 || expected_sample_rate <= 0) return false;
    auto info = wav_probe(path);
    if (!info) return false;
    if (info->channels != expected_channels ||
        info->sample_rate != expected_sample_rate) {
        return false;
    }
    auto tmp = wav_read_full(path);
    if (!tmp) return false;
    out = std::move(*tmp);
    return true;
}

bool read_file_bytes(const std::string& path, std::vector<char>& out) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamoff end = in.tellg();
    if (end < 0) return false;
    if (static_cast<uint64_t>(end) > std::numeric_limits<size_t>::max()) {
        return false;
    }
    std::vector<char> tmp(static_cast<size_t>(end));
    in.seekg(0, std::ios::beg);
    if (!tmp.empty() && !in.read(tmp.data(), static_cast<std::streamsize>(tmp.size()))) {
        return false;
    }
    out = std::move(tmp);
    return true;
}

bool encode_pcm24_wav_blob(const std::vector<float>& samples,
                           int channels, int sample_rate,
                           std::vector<char>& out_blob) {
    if (channels <= 0 || sample_rate <= 0) return false;
    const size_t ch = static_cast<size_t>(channels);
    if (samples.size() % ch != 0) return false;
    const int64_t frame_count = static_cast<int64_t>(samples.size() / ch);

    std::vector<char> blob;
    auto writer = WavWriter::open_memory(blob, channels, sample_rate);
    if (!writer) return false;
    auto ok = writer->write_frames(samples.data(), frame_count);
    if (!ok) return false;
    ok = writer->close();
    if (!ok) return false;
    out_blob = std::move(blob);
    return true;
}

bool RenderCache::write_file(const std::string& path,
                             const std::vector<uint8_t>& fp,
                             const std::vector<char>& blob,
                             int64_t frame_count,
                             const std::vector<int64_t>& limiter_frames,
                             uint64_t& out_bytes) {
    if (frame_count <= 0 || blob.empty()) return false;

    const std::string tmp = path + ".tmp";
    remove_disk_pair(path);

    if (!write_bytes_to_path(tmp, blob)) {
        remove_disk_pair(path);
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        remove_disk_pair(path);
        return false;
    }
    if (!write_fingerprint_sidecar(path, fp, limiter_frames)) {
        remove_disk_pair(path);
        return false;
    }

    const uint64_t wav_size = std::filesystem::file_size(path, ec);
    if (ec) {
        remove_disk_pair(path);
        return false;
    }
    const uint64_t sidecar_size =
        std::filesystem::file_size(fingerprint_sidecar_path(path), ec);
    if (ec) {
        remove_disk_pair(path);
        return false;
    }
    out_bytes = wav_size + sidecar_size;
    return true;
}
