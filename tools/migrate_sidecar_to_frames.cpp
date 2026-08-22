// migrate_sidecar_to_frames — convert one legacy MM:SS.mmm sidecar to the
// whole-source-frame authored domain, at the original path, keeping the legacy
// bytes as '<original-path>.bak'.
//
// The authored time domain moved from MM:SS.mmm timestamps to whole source
// frames; there is no legacy read path in the GUI, parser, or CLI, so old
// sidecars fail loudly at load. This standalone tool is the sole conversion
// route. It rewrites ONLY the time fields of a single file, preserving every
// other byte. A legacy time field is an MM:SS.mmm timestamp optionally
// followed by a signed seconds offset (+S.mmm or -S.mmm, exactly three
// decimals), applied in seconds before the rounding conversion: effective
// seconds = parse_timestamp(ts) + offset, then to whole source frames via
// banker's rounding (std::nearbyint) — the same rounding rule the GUI uses
// everywhere fractional values meet an integer domain — serialized via
// format_authored_frame (plain integer text). BOTH MARKER COLUMNS SNAP, each to
// its own canonical lattice (both stated once at the head of
// sidecar_snap_common.h): WARP marker positions (.warpmarkers) land on the GUI
// zoom-level-2 SOURCE grid — the nearest pixel column at sample_rate * 1.25 /
// 1000 frames per pixel, anchored at frame 0 (55.125 frames at 44.1 kHz) —
// while .phaseresetmarkers positions land on the zoom-level-2 TARGET lattice
// the live warp map defines, which is why that kind takes the source WAV as a
// third argument. .settings trim values are NOT snapped: a trim bound is a
// render window, not an authored musical instant, so it keeps plain whole-frame
// rounding.
//
// Migrated WARP positions therefore land on the SAME frame-0 zoom-2 grid the
// GUI's own source-view authoring gestures land on, and match GUI zoom-2
// authoring EXACTLY (not merely from the file start) WHERE THE LATTICE'S
// PRECONDITION HOLDS. The GUI viewport itself is snapped to this grid
// (clamp_viewport_start / painter_samples_per_pixel), and the source-view
// commit rounds once (displayed_grid_position_at_column), so under the
// multiple-of-16 effective-width floor AND a sample rate divisible by 50 — the
// 44.1 kHz / 48 kHz family and every other standard rate, though not the
// product's whole accepted rate vocabulary — the painter samples-per-pixel
// equals the logical spp and the recovered-viewport-column basis makes GUI
// zoom-2 authoring agree with this tool bit-for-bit at ANY configured width.
// A rate outside that family REFUSES here (rate_has_canonical_lattice, which
// also records the short-source boundary no tool can detect). The tool cannot
// know the runtime width, so it uses the logical spp: the canonical frame-0
// grid the GUI agrees with under that precondition. The snap
// still widens the shift from the exact product (up to about half a pixel
// column, ~28 frames / 0.6 ms at 44.1 kHz), so a render from a migrated file
// is NOT byte-identical to a pre-migration render of the same authoring, by
// design, within the accepted migration tolerance (migrated renders do not
// cmp-null against pre-migration renders).
//
// PHASE RESETS LAND ON THE TARGET LATTICE, AND THE TOOL KNOWS IT. They author
// in the TARGET view only (the home-view binding), on the target-frame column
// lattice the LIVE WARP MAP defines. The source-grid snap this column once took
// was a category error, and that history is the justification for the lattice
// it takes now: a source-grid snap reproduces the GUI's authorable set only
// under an identity warp map, and drifts arbitrarily far from it under any real
// tempo authoring — the two domains are different clocks past the first tempo
// change. For one day after that error was removed the column claimed no grid
// at all and took a plain rounded whole frame, a deliberate BALLPARK to be
// nudged into place by hand; the 2026-08-22 determinism ruling retires the
// ballpark. Authored positions are deterministic against canonical,
// viewport-independent lattices, and this tool CAN know the target one: the
// migrated .warpmarkers sibling is on disk beside the file being migrated, and
// the .settings sibling carries the scale, so the map builds through the exact
// chain the GUI target view and the CLI run. Hence the required <source.wav>
// argument (the map needs the rate and the source total) and the two sibling
// reads.
//
// THE ORDERING CONTRACT — warp before phase resets within a project — IS
// ENFORCED, not documented: the warp sibling is read with the PRODUCT parser, so
// a still-legacy .warpmarkers refuses with that parser's own diagnostic rather
// than yielding a map built from nothing. No second pass over the phase reset
// column is ever needed afterwards; the positions land where the GUI's own
// target-view gestures would put them.
//
// TWO REFUSALS RIDE THAT PATH, each writing nothing: a position AT OR PAST the
// source total (past-EOF is load-fatal in the product, and the tool must not
// write what the product refuses — the landing's own clamp at the last column is
// a different thing, the walls winning over the grid), and a post-conversion
// DUPLICATE frame that was not already a duplicate, which would collapse two
// authored resets into one event at the render boundary. THE WARP COLUMN IS
// UNTOUCHED BY THIS ARC, grid and refusals alike: its conversion is byte-for-
// byte the one that migrated this project's corpus, and a warp file's own
// collision refusal lives in the snapping tool beside it.
//
// Strictness note: every field that SHOULD convert must parse as a valid
// timestamp (is_valid_timestamp_format); anything else is a hard error that
// writes nothing. A file whose time fields already hold frame positions
// ("44100") does not match the MM:SS.mmm grammar, so a second run refuses
// cleanly rather than double-converting — the tool is not idempotent by
// re-parsing but is safe against re-running.
//
// Recovery: the conversion runs fully in memory, then the ORIGINAL FILE MOVES
// ASIDE as '<original-path>.bak' and the converted text is written fresh at the
// original path. That .bak is the whole recovery contract — a failed or torn
// write leaves the legacy bytes intact beside it — and an already-existing one
// is a refusal, never an overwrite. The contract, its refusal (why the
// existence test is renameat2's RENAME_NOREPLACE rather than a check-then-rename
// pair) and why the suffix is the .gitignore'd '.bak' are stated once, at
// sidecar_snap::backup_and_write, which this tool and the snapping tool share.
// There is no confirmation prompt; the backup is the answer the prompt used to
// ask for.
//
// THE TIMESTAMP PARSE HELPERS ARE TOOL-LOCAL (architect 2026-07-30), and stay
// local to this FILE rather than moving to the shared tool header: the legacy
// grammar is this one tool's business, and the header holds what BOTH tools use.
// They had no
// caller anywhere in src/, and holding them in the shared src/time_format.h
// pulled <regex> into every product consumer of that header — the frozen
// src/parser consumers included, compiled into both binaries — for a legacy read
// path only this tool has. The shared header keeps what the product actually uses
// — format_timestamp, the display-only WRITE direction — and records the derived
// consumer inventory. format_authored_frame is still shared, from
// src/parser/frame_format.h; it is the live serialization the product writes too.
//
// Build (from the project root). The single-translation-unit g++ line this
// header used to carry is gone with the target lattice: the tool now links the
// in-tree parser and audio-probe sources, so the standalone CMake build is the
// whole build story.
//     cmake -B build-tools -S tools && cmake --build build-tools

#include "sidecar_snap_common.h"

#include "../src/audio_io/audio_probe.h"
#include "../src/parser/frame_format.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const char* kProg = "migrate_sidecar_to_frames";

// Unified diagnostic shape for every error but the usage line. Single-space
// joined, no double spaces.
void diag(const std::string& action, const std::string& path,
          const std::string& detail) {
    std::cerr << kProg << ": " << action << " failed for '" << path
              << "': " << detail << '\n';
}

void usage() {
    std::cerr << kProg << " <sample_rate> <file> [<source.wav>]\n";
}

constexpr const char* kWarpExt     = ".warpmarkers";
constexpr const char* kResetExt    = ".phaseresetmarkers";
constexpr const char* kSettingsExt = ".settings";

// A valid MM:SS.mmm timestamp is exactly nine characters. Both marker columns
// carry the timestamp as the leading nine bytes (after an optional '#'); the
// settings trim values carry it as the whole value token.
constexpr size_t kTimestampLen = 9;

// Validate "MM:SS.mmm" (minutes and seconds 00-59, three-digit milliseconds).
// TOOL-LOCAL: the legacy sidecar grammar is this tool's business alone (see the
// head comment) — the product has no timestamp read path at all.
bool is_valid_timestamp_format(const std::string& s) {
    static const std::regex re("^([0-5][0-9]):([0-5][0-9])\\.[0-9]{3}$");
    return std::regex_match(s, re);
}

// Parse "MM:SS.mmm" to seconds. Caller validates format first (the one caller,
// convert_time_field below, does). Tool-local for the same reason.
double parse_timestamp(const std::string& s) {
    const int min    = std::stoi(s.substr(0, 2));
    const double sec = std::stod(s.substr(3));
    return min * 60.0 + sec;
}

// A time-field offset is a single token — sign, one or more digits, a dot, and
// exactly three digits — matching [+-][0-9]+[.][0-9]{3} in full. It follows the
// nine-character timestamp and must span exactly to the field end, so a partial
// or over-long form (e.g. "+0.0365") is rejected rather than silently prefix-
// matched.
bool is_valid_offset_token(const std::string& s) {
    static const std::regex re("^[+-][0-9]+\\.[0-9]{3}$");
    return std::regex_match(s, re);
}

// Where a converted position lands. The legacy parse above and its refusals are
// shared by all three kinds; only the final lattice differs, and the two
// lattices themselves live in sidecar_snap_common.h with the tool that also
// applies them to already-migrated files.
struct SnapMode {
    enum class Lattice {
        PlainRound,   // .settings trim: a render window, never a lattice point
        Source,       // .warpmarkers: the source view's zoom-2 column grid
        Target,       // .phaseresetmarkers: the target lattice under the map
    };
    Lattice lattice = Lattice::PlainRound;
    // Set iff lattice == Target; owned by main, alive for the whole conversion.
    const sidecar_snap::TargetLattice* target = nullptr;
};

// Land an exact source-sample position on the kind's lattice. Returns false and
// sets `reason` only on the Target arm, whose past-EOF refusal is real input a
// legacy file can carry (the position the millisecond named sits past the audio
// this sidecar was authored against).
bool land_on_lattice(double exact_sample, double sample_rate,
                     const SnapMode& snap, int64_t& frame,
                     std::string& reason) {
    switch (snap.lattice) {
        case SnapMode::Lattice::PlainRound:
            frame = static_cast<int64_t>(std::nearbyint(exact_sample));
            return true;
        case SnapMode::Lattice::Source:
            frame = sidecar_snap::snap_to_source_lattice(exact_sample,
                                                         sample_rate);
            return true;
        case SnapMode::Lattice::Target: {
            auto landed =
                sidecar_snap::snap_to_target_lattice(exact_sample, *snap.target);
            if (!landed) {
                reason = landed.error();
                return false;
            }
            frame = *landed;
            return true;
        }
    }
    return true;
}

// Convert one legacy time field to its whole-frame integer text. The field is a
// nine-character MM:SS.mmm timestamp optionally followed by a signed seconds
// offset that spans exactly to the field end; effective seconds =
// parse_timestamp(ts) + offset. The effective sample position — the EXACT
// position the legacy field named, reported back through `exact_sample` for the
// coincidence check — is then routed through land_on_lattice above, which picks
// the kind's lattice (or plain banker's rounding for the settings trim values,
// matching every other fractional-to-integer-domain conversion in the project).
// Shared by the marker and settings converters.
// Returns true and sets `exact_sample`/`frame` on success; on a hard error
// returns false and sets `reason` to a short parenthetical detail (no timestamp,
// a malformed/over-long offset, an offset whose magnitude is beyond double, an
// effective time whose frame position would not fit int64_t, a negative
// effective time — refused because parse_authored_frame treats negative
// positions as malformed at load, so the tool must never write one — or, on the
// target lattice alone, a position past the end of the source).
bool convert_time_field(const std::string& field, double sample_rate,
                        const SnapMode& snap, double& exact_sample,
                        int64_t& frame, std::string& reason) {
    // is_valid_timestamp_format matches only exactly nine characters, so a pass
    // here guarantees field.size() >= 9 and the offset substr below is in range.
    const std::string ts = field.substr(0, kTimestampLen);
    if (!is_valid_timestamp_format(ts)) {
        reason = "malformed timestamp";
        return false;
    }
    double seconds = parse_timestamp(ts);
    const std::string offset = field.substr(kTimestampLen);
    if (!offset.empty()) {
        if (!is_valid_offset_token(offset)) {
            reason = "malformed offset";
            return false;
        }
        // is_valid_offset_token admits any number of leading digits, so a
        // well-formed but enormous run ("+99999...999.000") overflows double
        // and std::stod throws out_of_range. Caught here so the failure lands
        // in the same first-error/write-nothing path every other malformed
        // field takes, rather than terminating the tool past its promise.
        // invalid_argument cannot fire: the token already matched the grammar.
        try {
            seconds += std::stod(offset);
        } catch (const std::out_of_range&) {
            reason = "offset out of range";
            return false;
        }
    }
    if (seconds < 0.0) {
        reason = "negative effective time";
        return false;
    }
    // The NON-THROWING half of the same escape the catch above closes. An
    // offset digit run that is enormous but still inside double (~1e20) parses
    // without throwing, and the effective sample position then overflows the
    // int64_t cast in land_on_lattice — undefined behavior writing a garbage
    // frame, which is the promised first-error path bypassed by a second route.
    // ONE loose bound closes it: half of int64_t's range is still ~3.3 million
    // years of source at 44.1 kHz, so nothing reachable is refused, and the
    // slack absorbs the snap's rounding-up at the very edge. The negated
    // compare also refuses a non-finite value, though the throw above makes one
    // unreachable here.
    exact_sample = seconds * sample_rate;
    constexpr double kMaxExactSample =
        static_cast<double>(std::numeric_limits<int64_t>::max() / 2);
    if (!(exact_sample <= kMaxExactSample)) {
        reason = "effective time out of range";
        return false;
    }
    return land_on_lattice(exact_sample, sample_rate, snap, frame, reason);
}

// --- marker columns ---------------------------------------------------------
//
// A warpmarkers line is [#]MM:SS.mmm[offset]|payload; a phaseresetmarkers line
// is [#]MM:SS.mmm[offset]. Both columns share this converter. The time field
// runs from the optional '#' to the first '|' (warp's payload separator) or to
// end of line (phase reset), and carries the timestamp plus an optional offset;
// warp's '|payload' suffix rides through verbatim.
//
// Rules:
//   - an empty line copies through;
//   - a first-byte-'#' line whose following nine characters are NOT a valid
//     timestamp is a comment line and copies through unchanged;
//   - a first-byte-'#' line whose following nine characters ARE a valid
//     timestamp is a disabled marker: convert, keep the '#' and the suffix;
//   - any other non-empty line must convert its time field; a leading field
//     that is not a valid timestamp, or trailing junk between the timestamp and
//     the field end that is not a valid offset, is a hard error.
//
// Returns true on success (converted or copied), false on a hard error, in
// which case `error_detail` carries the "line <n>: <content>" detail (with a
// short single-space parenthetical reason when the field itself is malformed).
// `record_out`, when non-null, receives the converted position on the lines
// that CARRY one — the coincidence check's input; lines that copy through leave
// it untouched.
bool convert_marker_line(const sidecar_snap::Line& in, double sample_rate,
                         const SnapMode& snap, int line_number,
                         sidecar_snap::Line& out, std::string& error_detail,
                         sidecar_snap::SnapRecord* record_out = nullptr) {
    const std::string& s = in.content;
    out.had_newline = in.had_newline;

    if (s.empty()) {           // empty line copies through
        out.content = s;
        return true;
    }

    const bool disabled = s[0] == '#';
    const size_t ts_start = disabled ? 1 : 0;
    const std::string ts = s.substr(ts_start, kTimestampLen);

    if (!is_valid_timestamp_format(ts)) {
        if (disabled) {        // '#' + non-timestamp = comment line
            out.content = s;
            return true;
        }
        // A non-'#' line that should carry a leading timestamp but does not is
        // a hard error — this is also what refuses a re-run over already
        // frame-converted content.
        error_detail = "line " + std::to_string(line_number) + ": " + s;
        return false;
    }

    // Valid leading timestamp: the time field ends at the first '|' (warp's
    // payload separator) or at end of line (phase reset). Convert the field
    // (timestamp plus optional offset) and keep the optional '#' and the
    // suffix verbatim.
    const size_t sep = s.find('|', ts_start);
    const size_t field_end = (sep == std::string::npos) ? s.size() : sep;
    const std::string field = s.substr(ts_start, field_end - ts_start);
    const std::string suffix = (sep == std::string::npos) ? "" : s.substr(sep);

    double  exact_sample = 0.0;
    int64_t frame        = 0;
    std::string reason;
    if (!convert_time_field(field, sample_rate, snap, exact_sample, frame,
                            reason)) {
        error_detail = "line " + std::to_string(line_number) + ": " + s +
                       " (" + reason + ")";
        return false;
    }
    if (record_out) *record_out = {line_number, exact_sample, frame};
    out.content = (disabled ? "#" : "") + format_authored_frame(frame) + suffix;
    return true;
}

// --- settings ---------------------------------------------------------------
//
// Only the four trim keys carry authored times; every other line copies
// through byte-identically. A trim key whose value is not a valid time field
// (a timestamp plus an optional offset) is a hard error. The key/value spelling
// and all surrounding whitespace are preserved: only the value token itself is
// rewritten.
bool is_trim_key(const std::string& key) {
    return key == "tab_a_trim_begin" || key == "tab_a_trim_end" ||
           key == "tab_b_trim_begin" || key == "tab_b_trim_end";
}

bool convert_settings_line(const sidecar_snap::Line& in, double sample_rate,
                           const SnapMode& snap, int line_number,
                           sidecar_snap::Line& out,
                           std::string& error_detail) {
    const std::string& s = in.content;
    out.had_newline = in.had_newline;

    const size_t eq = s.find('=');
    if (eq == std::string::npos) {   // not a key=value line: copy through
        out.content = s;
        return true;
    }
    const std::string key = sidecar_snap::trim_ws_copy(s.substr(0, eq));
    if (!is_trim_key(key)) {         // any non-trim key: copy through
        out.content = s;
        return true;
    }

    // Trim-key value: preserve leading/trailing whitespace around the token,
    // rewrite only the token. Find the value token within the right side.
    const std::string right = s.substr(eq + 1);
    const size_t tok_b = right.find_first_not_of(" \t\r");
    if (tok_b == std::string::npos) {
        error_detail = "line " + std::to_string(line_number) + ": " + s;
        return false;
    }
    const size_t tok_e = right.find_last_not_of(" \t\r");
    const std::string token = right.substr(tok_b, tok_e - tok_b + 1);
    double  exact_sample = 0.0;
    int64_t frame        = 0;
    std::string reason;
    if (!convert_time_field(token, sample_rate, snap, exact_sample, frame,
                            reason)) {
        error_detail = "line " + std::to_string(line_number) + ": " + s +
                       " (" + reason + ")";
        return false;
    }
    const std::string lead = right.substr(0, tok_b);
    const std::string tail = right.substr(tok_e + 1);
    out.content =
        s.substr(0, eq + 1) + lead + format_authored_frame(frame) + tail;
    return true;
}

enum class Kind { Warp, PhaseReset, Settings };

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        usage();
        return 1;
    }

    // sample_rate: a positive integer, whole token consumed.
    const std::string rate_arg = argv[1];
    errno = 0;
    char* end = nullptr;
    const long rate_l = std::strtol(rate_arg.c_str(), &end, 10);
    if (errno != 0 || end == rate_arg.c_str() || *end != '\0' || rate_l <= 0) {
        usage();
        return 1;
    }
    const double sample_rate = static_cast<double>(rate_l);

    const std::string path = argv[2];

    Kind kind;
    if (sidecar_snap::ends_with(path, kWarpExt)) {
        kind = Kind::Warp;
    } else if (sidecar_snap::ends_with(path, kResetExt)) {
        kind = Kind::PhaseReset;
    } else if (sidecar_snap::ends_with(path, kSettingsExt)) {
        kind = Kind::Settings;
    } else {
        diag("Migrate", path,
             "unrecognized extension (expected .warpmarkers, "
             ".phaseresetmarkers, or .settings)");
        return 1;
    }

    // THE RATE VOCABULARY GATE, ON THE <sample_rate> ARGUMENT ITSELF — which is
    // the authoritative rate for every kind here, the phase reset kind's probe
    // being required to AGREE with it below. The lattices only exist for a rate
    // divisible by 50 (rate_has_canonical_lattice carries the whole reasoning),
    // so a rate outside that family refuses before any conversion.
    //
    // ALL THREE KINDS TAKE THE GATE, the .settings kind included even though
    // its trim values take plain rounding and touch no lattice: a project is
    // migrated as a set under one rate, and a rate whose marker passes must
    // refuse is not a rate to half-convert a project under.
    if (!sidecar_snap::rate_has_canonical_lattice(rate_l)) {
        diag("Migrate", path, sidecar_snap::lattice_rate_refusal(rate_l));
        return 1;
    }

    // THE SOURCE WAV BELONGS TO EXACTLY ONE KIND, and an argument that cannot
    // participate is an error rather than decoration: the phase reset column
    // needs it (its lattice is the target one, which needs the warp map and the
    // source total), and the other two kinds convert from the rate argument
    // alone exactly as they always have.
    const std::string wav_path = (argc == 4) ? argv[3] : "";
    if (kind == Kind::PhaseReset && wav_path.empty()) {
        diag("Migrate", path,
             "a <source.wav> argument is required for .phaseresetmarkers (the "
             "target lattice needs the warp map and the source total)");
        return 1;
    }
    if (kind != Kind::PhaseReset && !wav_path.empty()) {
        diag("Migrate", path,
             "a <source.wav> argument is accepted only for "
             ".phaseresetmarkers");
        return 1;
    }

    // The phase reset lattice, built once from the source audio and the
    // sidecar's own siblings. THE SIBLINGS ARE THE OUTPUT FILE'S: the
    // .warpmarkers beside it must ALREADY be in the frames format, which is the
    // ordering contract — warp migrates first — enforced by the product parser
    // inside build_target_lattice rather than merely asked for here.
    sidecar_snap::TargetLattice lattice;
    if (kind == Kind::PhaseReset) {
        const auto probed = audio_probe(wav_path);
        if (!probed) {
            diag("Probe", wav_path, probed.error());
            return 1;
        }
        // The <sample_rate> argument stays authoritative for every kind, so it
        // must AGREE with the audio here rather than being quietly overridden:
        // a disagreement means one of the two describes a different source, and
        // the conversion would be wrong whichever way it were resolved.
        if (probed->sample_rate != rate_l) {
            diag("Probe", wav_path,
                 "sample rate " + std::to_string(probed->sample_rate) +
                     " disagrees with the <sample_rate> argument " +
                     rate_arg);
            return 1;
        }
        if (probed->frames <= 0) {
            diag("Probe", wav_path, "source has no frames");
            return 1;
        }
        auto built = sidecar_snap::build_target_lattice(
            sidecar_snap::sibling_sidecar_path(path, kResetExt, kWarpExt),
            sidecar_snap::sibling_sidecar_path(path, kResetExt, kSettingsExt),
            rate_l, probed->frames);
        if (!built) {
            diag("Migrate", path, built.error());
            return 1;
        }
        lattice = std::move(*built);
    }

    // The bytes come in COMPLETELY OR NOT AT ALL, before the conversion and
    // long before the destructive rename below (the contract at
    // sidecar_snap::read_file_whole).
    auto data = sidecar_snap::read_file_whole(path);
    if (!data) {
        diag(data.error().action, path, data.error().detail);
        return 1;
    }

    // Convert fully in memory first, so a conversion error refuses before
    // anything on disk moves: no backup rename, no partial file.
    std::vector<sidecar_snap::Line> lines = sidecar_snap::split_lines(*data);
    std::vector<sidecar_snap::Line> out_lines;
    out_lines.reserve(lines.size());
    // Each marker column lands on its own canonical lattice; settings trim
    // values are a render window rather than an authored instant and take plain
    // whole-frame rounding.
    SnapMode snap;
    if (kind == Kind::Warp) {
        snap.lattice = SnapMode::Lattice::Source;
    } else if (kind == Kind::PhaseReset) {
        snap.lattice = SnapMode::Lattice::Target;
        snap.target  = &lattice;
    }
    std::vector<sidecar_snap::SnapRecord> records;
    for (size_t i = 0; i < lines.size(); ++i) {
        const int line_number = static_cast<int>(i + 1);
        sidecar_snap::Line out_line;
        std::string detail;
        bool ok;
        if (kind == Kind::Settings) {
            ok = convert_settings_line(lines[i], sample_rate, snap, line_number,
                                       out_line, detail);
        } else {
            // The coincidence check below rides the PHASE RESET column alone.
            // The warp column's conversion — grid and refusals — is byte-for-
            // byte the one that already migrated this project's corpus, and it
            // stays that way; the snapping tool is where a warp file's
            // collision refusal lives.
            sidecar_snap::SnapRecord record;
            ok = convert_marker_line(
                lines[i], sample_rate, snap, line_number, out_line, detail,
                kind == Kind::PhaseReset ? &record : nullptr);
            if (ok && record.line_number != 0) records.push_back(record);
        }
        if (!ok) {
            diag("Convert", path, detail);
            return 1;
        }
        out_lines.push_back(std::move(out_line));
    }

    // A CONVERSION MUST NEVER MANUFACTURE A COINCIDENCE (the rule at
    // sidecar_snap::first_manufactured_coincidence). Two legacy timestamps that
    // named different instants and land on one frame would collapse at the
    // render boundary into a single event, dropping an authored tempo or a
    // reset the file still spells out.
    if (auto collision = sidecar_snap::first_manufactured_coincidence(records)) {
        diag("Convert", path, *collision);
        return 1;
    }
    const std::string converted = sidecar_snap::join_lines(out_lines);

    // The legacy bytes move aside as '<path>.bak' and the converted text is
    // written fresh at the original path — the recovery contract stated once at
    // sidecar_snap::backup_and_write, including why an existing backup refuses
    // through the rename itself.
    if (auto err = sidecar_snap::backup_and_write(path, converted, "Migrate")) {
        diag(err->action, path, err->detail);
        return 1;
    }
    return 0;
}
