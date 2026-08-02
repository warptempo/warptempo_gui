// migrate_sidecar_to_frames — convert one legacy MM:SS.mmm sidecar to the
// whole-source-frame authored domain, at the original path, keeping the legacy
// bytes as '<original-path>.bk'.
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
// format_authored_frame (plain integer text). EXACTLY ONE COLUMN SNAPS: WARP
// marker positions (.warpmarkers) are additionally snapped to the GUI
// zoom-level-2 frame grid — the nearest pixel column at sample_rate * 1.25 /
// 1000 frames per pixel, anchored at frame 0 (55.125 frames at 44.1 kHz).
// .phaseresetmarkers positions and .settings trim values are NOT snapped —
// both keep plain whole-frame rounding.
//
// Migrated WARP positions therefore land on the SAME frame-0 zoom-2 grid the
// GUI's own source-view authoring gestures land on, and match GUI zoom-2
// authoring EXACTLY (not merely from the file start). The GUI viewport itself
// is snapped to this grid (clamp_viewport_start / painter_samples_per_pixel),
// and the source-view commit rounds once (source_grid_position_at_column), so
// under the multiple-of-16 effective-width / standard-rate contract the
// painter samples-per-pixel equals the logical spp and the recovered-viewport-
// column basis makes GUI zoom-2 authoring agree with this tool bit-for-bit at
// ANY configured width. The tool cannot know the runtime width, so it uses the
// logical spp: the canonical frame-0 grid the GUI now agrees with. The snap
// still widens the shift from the exact product (up to about half a pixel
// column, ~28 frames / 0.6 ms at 44.1 kHz), so a render from a migrated file
// is NOT byte-identical to a pre-migration render of the same authoring, by
// design, within the accepted migration tolerance (migrated renders do not
// cmp-null against pre-migration renders).
//
// PHASE RESETS CLAIM NO GRID AT ALL, DELIBERATELY. They author in the TARGET
// view only (the home-view binding), on the target-frame column lattice the
// LIVE WARP MAP defines — which this tool, seeing one sidecar and a sample
// rate, cannot know. The source-grid snap this column used to take was a
// category error: it reproduced the GUI's authorable set only under an
// identity warp map, and drifted arbitrarily far from it under any real tempo
// authoring. So a migrated phase reset is a plain rounded whole frame: a
// deliberate BALLPARK, not a snapped position. Such a file loads and renders
// normally (grid membership is nowhere validated); the position simply sits
// wherever the millisecond landed, to be nudged into place by hand afterwards
// — which phase resets, being a rough guide, get anyway.
//
// Strictness note: every field that SHOULD convert must parse as a valid
// timestamp (is_valid_timestamp_format); anything else is a hard error that
// writes nothing. A file whose time fields already hold frame positions
// ("44100") does not match the MM:SS.mmm grammar, so a second run refuses
// cleanly rather than double-converting — the tool is not idempotent by
// re-parsing but is safe against re-running.
//
// Recovery: the conversion runs fully in memory, then the ORIGINAL FILE IS
// RENAMED to '<original-path>.bk' and the converted text is written fresh at
// the original path. The .bk is the whole recovery contract — a failed or torn
// write leaves the legacy bytes intact beside it — so an already-existing .bk
// is a refusal, never an overwrite: it is the only copy of an earlier run's
// legacy input. That refusal is the RENAME ITSELF, not a check before it
// (renameat2 with RENAME_NOREPLACE — the Linux-only primitive this Linux-only
// tool is entitled to): plain rename REPLACES its destination, so a check-then-
// rename pair could still overwrite a .bk that appeared in between. There is no
// confirmation prompt; the backup is the answer the prompt used to ask for.
//
// THE TIMESTAMP PARSE HELPERS ARE TOOL-LOCAL (architect 2026-07-30), joining
// this file's other local mirrors (the zoom-2 pixel scale below): they had no
// caller anywhere in src/, and holding them in the shared src/time_format.h
// pulled <regex> into every product consumer of that header — the frozen
// src/parser consumers included, compiled into both binaries — for a legacy read
// path only this tool has. The shared header keeps what the product actually uses
// — format_timestamp, the display-only WRITE direction — and records the derived
// consumer inventory. format_authored_frame is still shared, from
// src/parser/frame_format.h; it is the live serialization the product writes too.
//
// Ad hoc compile (from tools/):
//     g++ -std=c++23 -O2 -o migrate_sidecar_to_frames migrate_sidecar_to_frames.cpp

#include "../src/parser/frame_format.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
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
    std::cerr << kProg << " <sample_rate> <file>\n";
}

// A valid MM:SS.mmm timestamp is exactly nine characters. Both marker columns
// carry the timestamp as the leading nine bytes (after an optional '#'); the
// settings trim values carry it as the whole value token.
constexpr size_t kTimestampLen = 9;

// The GUI zoom pixel scale that migrated WARP marker frames snap to (the warp
// column alone — see the head comment). The live source of truth is
// samples_per_pixel_at (src/gui/main.cpp): zoom is a continuous exponent and
// ms-per-pixel is the function 0.625 * 2^(level - 1), which at level 2 gives
// this same 1.25 ms/px. The old kZoomMsPerPixel[2] table that value used to be
// read from no longer exists; the number it held for zoom 2 is unchanged. At
// 44.1 kHz this is a 55.125-frame grid (sample_rate * 1.25 / 1000 frames per
// pixel).
constexpr double kMarkerSnapMsPerPx = 1.25;   // GUI zoom level 2

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

// Snap an exact source-sample position to the frame grid that GUI authoring
// produces at a given zoom pixel scale, viewport anchored at frame 0: the
// nearest pixel column's frame. snap_ms_per_px <= 0 means "no snap" (plain
// nearbyint of the sample), used for settings trim values and for phase reset
// positions, which claim no grid.
int64_t snap_frame_to_grid(double exact_sample, double sample_rate,
                           double snap_ms_per_px) {
    if (snap_ms_per_px <= 0.0)
        return static_cast<int64_t>(std::nearbyint(exact_sample));
    const double spp = sample_rate * snap_ms_per_px / 1000.0;
    const double col = std::nearbyint(exact_sample / spp);
    return static_cast<int64_t>(std::nearbyint(col * spp));
}

// Convert one legacy time field to its whole-frame integer text. The field is a
// nine-character MM:SS.mmm timestamp optionally followed by a signed seconds
// offset that spans exactly to the field end; effective seconds =
// parse_timestamp(ts) + offset. The effective sample position is then routed
// through snap_frame_to_grid: with snap_ms_per_px > 0 the result is snapped to
// that GUI zoom grid (nearest pixel column's frame, anchored at frame 0), and
// with snap_ms_per_px <= 0 it is plain-rounded to the nearest whole frame
// (banker's rounding, matching every other fractional-to-integer-domain
// conversion in the project). Shared by the marker and settings converters.
// Returns true and sets `frame_text` on success; on a hard error returns false
// and sets `reason` to a short parenthetical detail (no timestamp, a
// malformed/over-long offset, an offset whose magnitude is beyond double, an
// effective time whose frame position would not fit int64_t, or a negative
// effective time — the last refused because parse_authored_frame treats
// negative positions as malformed at load, so the tool must never write one).
bool convert_time_field(const std::string& field, double sample_rate,
                        double snap_ms_per_px, std::string& frame_text,
                        std::string& reason) {
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
    // int64_t cast in snap_frame_to_grid — undefined behavior writing a garbage
    // frame, which is the promised first-error path bypassed by a second route.
    // ONE loose bound closes it: half of int64_t's range is still ~3.3 million
    // years of source at 44.1 kHz, so nothing reachable is refused, and the
    // slack absorbs the snap's rounding-up at the very edge. The negated
    // compare also refuses a non-finite value, though the throw above makes one
    // unreachable here.
    const double exact_sample = seconds * sample_rate;
    constexpr double kMaxExactSample =
        static_cast<double>(std::numeric_limits<int64_t>::max() / 2);
    if (!(exact_sample <= kMaxExactSample)) {
        reason = "effective time out of range";
        return false;
    }
    frame_text = format_authored_frame(
        snap_frame_to_grid(exact_sample, sample_rate, snap_ms_per_px));
    return true;
}

// Split raw file bytes into lines, remembering per line whether a '\n'
// terminated it, so reassembly reproduces the exact terminator structure
// (including a missing final newline). '\r' bytes, if any, stay inside the line
// content and copy through verbatim.
struct Line {
    std::string content;   // without the trailing '\n'
    bool had_newline;
};

std::vector<Line> split_lines(const std::string& data) {
    std::vector<Line> lines;
    std::string cur;
    for (char c : data) {
        if (c == '\n') {
            lines.push_back({std::move(cur), true});
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    // Trailing bytes with no closing newline form a final, newline-less line.
    if (!cur.empty()) lines.push_back({std::move(cur), false});
    return lines;
}

std::string join_lines(const std::vector<Line>& lines) {
    std::string out;
    for (const Line& l : lines) {
        out += l.content;
        if (l.had_newline) out += '\n';
    }
    return out;
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
bool convert_marker_line(const Line& in, double sample_rate,
                         double snap_ms_per_px, int line_number, Line& out,
                         std::string& error_detail) {
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

    std::string frame_text;
    std::string reason;
    if (!convert_time_field(field, sample_rate, snap_ms_per_px, frame_text,
                            reason)) {
        error_detail = "line " + std::to_string(line_number) + ": " + s +
                       " (" + reason + ")";
        return false;
    }
    out.content = (disabled ? "#" : "") + frame_text + suffix;
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

std::string trim_ws_copy(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool convert_settings_line(const Line& in, double sample_rate,
                           double snap_ms_per_px, int line_number, Line& out,
                           std::string& error_detail) {
    const std::string& s = in.content;
    out.had_newline = in.had_newline;

    const size_t eq = s.find('=');
    if (eq == std::string::npos) {   // not a key=value line: copy through
        out.content = s;
        return true;
    }
    const std::string key = trim_ws_copy(s.substr(0, eq));
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
    std::string frame_text;
    std::string reason;
    if (!convert_time_field(token, sample_rate, snap_ms_per_px, frame_text,
                            reason)) {
        error_detail = "line " + std::to_string(line_number) + ": " + s +
                       " (" + reason + ")";
        return false;
    }
    const std::string lead = right.substr(0, tok_b);
    const std::string tail = right.substr(tok_e + 1);
    out.content = s.substr(0, eq + 1) + lead + frame_text + tail;
    return true;
}

enum class Kind { Warp, PhaseReset, Settings };

bool ends_with(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() &&
           s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
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
    if (ends_with(path, ".warpmarkers")) {
        kind = Kind::Warp;
    } else if (ends_with(path, ".phaseresetmarkers")) {
        kind = Kind::PhaseReset;
    } else if (ends_with(path, ".settings")) {
        kind = Kind::Settings;
    } else {
        diag("Migrate", path,
             "unrecognized extension (expected .warpmarkers, "
             ".phaseresetmarkers, or .settings)");
        return 1;
    }

    // Read the whole file as bytes — COMPLETELY OR NOT AT ALL. A short or
    // failed read must refuse here, before the conversion and long before the
    // destructive rename below: accepting a prefix would publish it as the
    // finished migration and move the real input aside as the backup.
    //
    // THE COMPLETENESS VERDICT IS A LENGTH COMPARE, not a stream flag, because
    // the stream flags cannot carry it. `ss << f.rdbuf()` never touches `f`'s
    // state at all, and it sets failbit on `ss` whenever ZERO characters were
    // inserted — which an empty file and an unreadable one produce alike (a
    // DIRECTORY with a recognized suffix is the concrete case: the read-only
    // open succeeds, the buffer read yields nothing, and neither badbit nor
    // eofbit is raised anywhere). So the input is first required to be a
    // REGULAR FILE, and the bytes actually read are then compared against the
    // size the filesystem reports. badbit is still checked — it is the one
    // genuine hard-error signal the idiom does raise — but the length compare
    // is what makes the read all-or-nothing. A file changing size underneath a
    // one-shot conversion refuses too, which is the right answer.
    std::string data;
    {
        std::error_code fec;
        const std::filesystem::file_status st =
            std::filesystem::status(path, fec);
        if (fec) {
            diag("Read", path, "cannot stat file: " + fec.message());
            return 1;
        }
        if (!std::filesystem::is_regular_file(st)) {
            diag("Read", path, "not a regular file");
            return 1;
        }
        const std::uintmax_t size = std::filesystem::file_size(path, fec);
        if (fec) {
            diag("Read", path, "cannot read file size: " + fec.message());
            return 1;
        }
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            diag("Read", path, "cannot open file");
            return 1;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        if (f.bad() || ss.bad()) {
            diag("Read", path, "cannot read file");
            return 1;
        }
        data = ss.str();
        if (static_cast<std::uintmax_t>(data.size()) != size) {
            diag("Read", path,
                 "short read (" + std::to_string(data.size()) + " of " +
                     std::to_string(size) + " bytes)");
            return 1;
        }
    }

    // Convert fully in memory first, so a conversion error refuses before
    // anything on disk moves: no backup rename, no partial file.
    std::vector<Line> lines = split_lines(data);
    std::vector<Line> out_lines;
    out_lines.reserve(lines.size());
    // The WARP column alone snaps to the GUI zoom-level-2 grid. Phase resets
    // author on the target grid this tool cannot know, and settings trim values
    // never snapped; both take plain whole-frame rounding (0.0 disables the
    // snap in convert_time_field).
    const double snap_ms_per_px =
        (kind == Kind::Warp) ? kMarkerSnapMsPerPx : 0.0;
    for (size_t i = 0; i < lines.size(); ++i) {
        const int line_number = static_cast<int>(i + 1);
        Line out_line;
        std::string detail;
        bool ok;
        if (kind == Kind::Settings) {
            ok = convert_settings_line(lines[i], sample_rate, snap_ms_per_px,
                                       line_number, out_line, detail);
        } else {
            ok = convert_marker_line(lines[i], sample_rate, snap_ms_per_px,
                                     line_number, out_line, detail);
        }
        if (!ok) {
            diag("Convert", path, detail);
            return 1;
        }
        out_lines.push_back(std::move(out_line));
    }
    const std::string converted = join_lines(out_lines);

    // The original moves aside as the backup, then the converted text is
    // written fresh at the original path. This rename IS the tool's recovery
    // contract: the GUI serializers' atomic temp+fsync+rename writer is
    // deliberately NOT imported here (a one-shot conversion does not need it),
    // and the legacy bytes surviving under '<path>.bk' is what makes a failed
    // or torn write below recoverable rather than fatal.
    //
    // An existing backup is a refusal, never an overwrite: it holds an earlier
    // run's legacy input, the exact copy this tool exists to protect. Nothing
    // has been written or moved at this point, so the refusal leaves the
    // original in place.
    //
    // THE REFUSAL IS THE SYSCALL'S, IN ONE FILESYSTEM OPERATION. There is no
    // preflight existence check, deliberately: POSIX rename REPLACES its
    // destination, so a check-then-rename pair leaves a window in which a .bk
    // appearing after the check — a second invocation on the same file is
    // enough — is silently destroyed, which is exactly the loss the recovery
    // contract above exists to prevent. renameat2's RENAME_NOREPLACE makes the
    // "only if the backup does not exist" condition part of the move itself and
    // reports EEXIST when it does; that errno IS the refusal arm. This is a
    // Linux-only primitive, and this Linux-only tool is entitled to it (the
    // product's binaries are -march=native for one Arch host).
    const std::string backup_path = path + ".bk";
    if (::renameat2(AT_FDCWD, path.c_str(), AT_FDCWD, backup_path.c_str(),
                    RENAME_NOREPLACE) != 0) {
        if (errno == EEXIST) {
            diag("Migrate", path,
                 "backup '" + backup_path + "' already exists");
            return 1;
        }
        diag("Backup", path,
             "cannot rename to '" + backup_path + "': " +
                 std::strerror(errno));
        return 1;
    }

    // Plain truncate-and-write at the now-vacant original path; the .bk above
    // is what a failure here falls back on.
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        // Both write failures name the backup: it is now the only copy of the
        // legacy input, and saying where it went is the whole point of taking
        // it.
        diag("Write", path,
             "cannot open file for writing (the original is at '" +
                 backup_path + "')");
        return 1;
    }
    out << converted;
    out.flush();
    if (!out) {
        diag("Write", path,
             "write error (the original is at '" + backup_path + "')");
        return 1;
    }
    return 0;
}
