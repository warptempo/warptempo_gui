// migrate_sidecar_to_frames — convert one legacy MM:SS.mmm sidecar to the
// source-frame-double authored domain, in place.
//
// The authored time domain moved from MM:SS.mmm timestamps to source-frame
// doubles; there is no legacy read path in the GUI, parser, or CLI, so old
// sidecars fail loudly at load. This standalone tool is the sole conversion
// route. It rewrites ONLY the time fields of a single file, preserving every
// other byte. A legacy time field is an MM:SS.mmm timestamp optionally
// followed by a signed seconds offset (+S.mmm or -S.mmm, exactly three
// decimals), applied in seconds before the rounding conversion: effective
// seconds = parse_timestamp(ts) + offset, frames = nearbyint(effective *
// sample_rate) — banker's rounding to the nearest whole frame, the same
// rounding rule the GUI uses everywhere fractional values meet an integer
// domain — serialized via format_frame_double (to_chars shortest). Migrated
// positions therefore match the integer frames the GUI's own gestures author.
// The shift from the exact product is at most half a frame (about 11
// microseconds at 44.1 kHz), so a render from a migrated file is NOT
// byte-identical to a pre-migration render of the same authoring, by design.
//
// Strictness note: every field that SHOULD convert must parse as a valid
// timestamp (is_valid_timestamp_format); anything else is a hard error that
// writes nothing. A file whose time fields already hold frame doubles ("44100")
// does not match the MM:SS.mmm grammar, so a second run refuses cleanly rather
// than double-converting — the tool is not idempotent by re-parsing but is
// safe against re-running.
//
// The two conversion helpers are shared with the rest of the tree, never
// duplicated: parse_timestamp / is_valid_timestamp_format live in
// src/time_format.h, format_frame_double in src/parser/frame_format.h.
//
// Ad hoc compile (from tools/):
//     g++ -std=c++23 -O2 -o migrate_sidecar_to_frames migrate_sidecar_to_frames.cpp

#include "../src/time_format.h"
#include "../src/parser/frame_format.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* kProg = "migrate_sidecar_to_frames";

// Unified diagnostic shape for every error but the usage line and the y/N
// prompt. Single-space joined, no double spaces.
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

// A time-field offset is a single token — sign, one or more digits, a dot, and
// exactly three digits — matching [+-][0-9]+[.][0-9]{3} in full. It follows the
// nine-character timestamp and must span exactly to the field end, so a partial
// or over-long form (e.g. "+0.0365") is rejected rather than silently prefix-
// matched.
bool is_valid_offset_token(const std::string& s) {
    static const std::regex re("^[+-][0-9]+\\.[0-9]{3}$");
    return std::regex_match(s, re);
}

// Convert one legacy time field to its frame-double text, rounded to the
// nearest whole frame (banker's rounding, matching every other fractional-to-
// integer-domain conversion in the project). The field is a nine-character
// MM:SS.mmm timestamp optionally followed by a signed seconds offset that spans
// exactly to the field end; effective seconds = parse_timestamp(ts) + offset.
// Shared by the marker and settings converters. Returns true and sets
// `frame_text` on success; on a hard error returns false and sets `reason` to a
// short parenthetical detail (no timestamp, a malformed/over-long offset, or a
// negative effective time — the last refused because parse_frame_double treats
// negative positions as malformed at load, so the tool must never write one).
bool convert_time_field(const std::string& field, double sample_rate,
                        std::string& frame_text, std::string& reason) {
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
        seconds += std::stod(offset);
    }
    if (seconds < 0.0) {
        reason = "negative effective time";
        return false;
    }
    frame_text = format_frame_double(std::nearbyint(seconds * sample_rate));
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
                         int line_number, Line& out, std::string& error_detail) {
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
        // frame-double content.
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
    if (!convert_time_field(field, sample_rate, frame_text, reason)) {
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

bool convert_settings_line(const Line& in, double sample_rate, int line_number,
                           Line& out, std::string& error_detail) {
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
    if (!convert_time_field(token, sample_rate, frame_text, reason)) {
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
        diag("migrate", path,
             "unrecognized extension (expected .warpmarkers, "
             ".phaseresetmarkers, or .settings)");
        return 1;
    }

    // Read the whole file as bytes.
    std::string data;
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            diag("read", path, "cannot open file");
            return 1;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        data = ss.str();
    }

    // Convert fully in memory first, so a refused confirmation or a conversion
    // error never leaves a partial file.
    std::vector<Line> lines = split_lines(data);
    std::vector<Line> out_lines;
    out_lines.reserve(lines.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        const int line_number = static_cast<int>(i + 1);
        Line out_line;
        std::string detail;
        bool ok;
        if (kind == Kind::Settings) {
            ok = convert_settings_line(lines[i], sample_rate, line_number,
                                       out_line, detail);
        } else {
            ok = convert_marker_line(lines[i], sample_rate, line_number,
                                     out_line, detail);
        }
        if (!ok) {
            diag("convert", path, detail);
            return 1;
        }
        out_lines.push_back(std::move(out_line));
    }
    const std::string converted = join_lines(out_lines);

    // Overwrite confirmation. The user creates their own backups.
    std::cerr << kProg << ": will overwrite '" << path
              << "' in place; proceed? [y/N]\n";
    std::string reply;
    std::getline(std::cin, reply);
    if (reply != "y" && reply != "Y") {
        return 0;  // refused: leave the file untouched
    }

    // Simple truncate-and-write, matching the GUI serializers.
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        diag("write", path, "cannot open file for writing");
        return 1;
    }
    out << converted;
    out.flush();
    if (!out) {
        diag("write", path, "write error");
        return 1;
    }
    return 0;
}
