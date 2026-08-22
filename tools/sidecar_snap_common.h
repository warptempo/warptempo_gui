#pragma once

// sidecar_snap_common — the two canonical authored LATTICES a sidecar position
// lands on, plus the sibling, read and backup/write machinery the sidecar tools
// share. Header-only and TOOL-LOCAL: the tools are standalone binaries that
// each compile the in-tree sources they need (object-code duplication accepted,
// the project's own build convention), and nothing under src/ includes this.
//
// THE LATTICES ARE VIEWPORT-INDEPENDENT, which is the whole reason a tool can
// reproduce them. The GUI lands every authored position on the pixel column
// grid of the view it was authored in, anchored at frame 0 of that display
// domain rather than at the current viewport start
// (displayed_grid_position_at_column, src/gui/warp_frame_map_view.h): a
// column's time is (m + col) * q with m the viewport's own column index, so the
// reachable set at a given zoom is exactly {round(k*q)} over integer k, whatever
// the camera did on the way there. These tools mirror that set at zoom level 2
// — the working zoom the `c` command parks on.
//
//   WARP — the SOURCE view's lattice (the warp column's home view):
//       q       = sample_rate * 1.25 / 1000    (55.125 frames/px at 44.1 kHz)
//       landing = nearbyint(nearbyint(s / q) * q)
//
//   PHASE RESET — the TARGET view's lattice (the phase reset column's home
//   view), defined by the live warp map:
//       s_int   = nearbyint(s)
//       T       = nearbyint(map_source_to_target(s_int, map))
//       k       = nearbyint(T / q)             (same q: ms/px is domain-blind)
//       Tq      = llrint(k * q), floored at 0
//       landing = nearbyint(map_target_to_source(Tq, map)), clamped to
//                 [0, total_frames - 1]
//   The first two roundings are the PAINTER'S, not tidiness: k must be the
//   column the product actually paints the position at (the shape is stated
//   in full at snap_to_target_lattice below).
//
// The target lattice needs the LIVE WARP MAP, which is why every tool path that
// touches the phase reset column needs the source WAV and the sidecar's warp
// and settings siblings: the map is built through the exact chain the GUI target
// view and the CLI run — resolve_warp_markers_for_render, then
// build_warp_frame_map at the settings sidecar's `scale`. NO kN/2 OFFSET
// ANYWHERE: the phase reset drop's lead-in offset is a seed convenience at
// authoring time, not the resting lattice a position occupies.

#include "../src/parser/frame_format.h"
#include "../src/parser/value_format.h"
#include "../src/parser/warp_frame_map_build.h"
#include "../src/parser/warpmarkers_parse.h"
#include "../src/warp_frame_map.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace sidecar_snap {

// --- the lattices -----------------------------------------------------------

// GUI zoom level 2, the working zoom. ms-per-pixel is the continuous function
// 0.625 * 2^(level - 1) (samples_per_pixel_at, src/gui/main.cpp), so level 2 is
// 1.25 ms/px — in EITHER display domain, since ms-per-pixel is a screen
// quantity and knows nothing of source or target frames.
inline constexpr double kLatticeMsPerPx = 1.25;

// Frames per pixel column at the lattice zoom, for a domain clocked at
// `sample_rate` (55.125 at 44.1 kHz). The target domain runs on the same clock
// as the source — a warp changes how many frames a passage occupies, never the
// rate — so one q serves both lattices.
inline double lattice_frames_per_px(double sample_rate) {
    return sample_rate * kLatticeMsPerPx / 1000.0;
}

// THE WARP LATTICE: the nearest source-view column's frame, anchored at frame 0.
// `exact_source` is the position's exact source-sample value (for an already
// migrated file that is simply the stored frame — the exact position IS the
// frame).
inline int64_t snap_to_source_lattice(double exact_source, double sample_rate) {
    const double q = lattice_frames_per_px(sample_rate);
    return static_cast<int64_t>(
        std::nearbyint(std::nearbyint(exact_source / q) * q));
}

// Everything the target lattice needs: the built warp map plus the source
// audio's rate and length. Built once per file by build_target_lattice below.
struct TargetLattice {
    std::vector<WarpFrameMapSegment> map;
    double  sample_rate  = 0.0;
    int64_t total_frames = 0;
};

// THE PHASE RESET LATTICE: where a target-view nudge, drop or drag commit would
// leave a reset whose exact source position is `exact_source` — take the column
// the product PAINTS the position at, quantize that column's time to a whole
// target frame (the target arm's own llrint, floored at 0), then inverse-map at
// full precision and round once into the authored source domain.
//
// THE COLUMN CHOICE IS frame_to_paint_sample's SHAPE EXACTLY (render.cpp), at
// the frame-0 basis: nearbyint the source frame, forward-map it, nearbyint the
// MAPPED TARGET FRAME, then nearbyint the fractional column. That middle
// rounding is the load-bearing one — the painters place a marker from the
// rounded target frame, so choosing k from the full-precision map output picks
// the ADJACENT column whenever the mapped target falls within half a frame of a
// half-column boundary, and the tool would then land the position somewhere the
// GUI's own painted-column -> authored_frame_at_column round trip never puts it.
// With the paint shape mirrored, the landing IS the product's own
// authored_frame_at_column of the column it paints the marker in.
//
// PAST-EOF IS A REFUSAL, NOT A CLAMP. All authored positions share the product's
// inclusive [0, total-1] domain, and a reset outside it is load-fatal in both
// binaries (first_past_eof_wall_defect), so a tool must never quietly pull such
// a position into range and publish it as a migration: an input that reaches
// here past the end is reported to the caller instead. The clamp on the RESULT
// is a different thing — the walls win over the grid everywhere in the product,
// so the last column's landing rests at the wall.
inline std::expected<int64_t, std::string> snap_to_target_lattice(
    double exact_source, const TargetLattice& lattice) {
    if (!(exact_source >= 0.0))
        return std::unexpected(std::string("negative source position"));
    const double hi = static_cast<double>(lattice.total_frames - 1);
    if (!(exact_source <= hi)) {
        return std::unexpected(
            "position past the end of the source (" +
            format_authored_frame(lattice.total_frames) + " frames)");
    }
    const double q = lattice_frames_per_px(lattice.sample_rate);
    const double source_frame = std::nearbyint(exact_source);
    const double target =
        std::nearbyint(map_source_to_target(source_frame, lattice.map));
    const double column_time = std::nearbyint(target / q) * q;
    const double target_frame = (column_time < 0.0)
        ? 0.0
        : static_cast<double>(std::llrint(column_time));
    double landed = std::nearbyint(
        map_target_to_source(target_frame, lattice.map));
    // Ordered so a NaN (unreachable from a built map, whose emission contract
    // refuses non-finite anchors) would take the wall rather than the cast's
    // undefined behavior.
    if (!(landed <= hi)) landed = hi;
    if (!(landed >= 0.0)) landed = 0.0;
    return static_cast<int64_t>(landed);
}

// --- text helpers -----------------------------------------------------------

inline bool ends_with(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() &&
           s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

inline std::string trim_ws_copy(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// The sibling sidecar beside `path`: same directory, same stem, a different
// suffix. The stem is whatever remains once the KNOWN suffix comes off — never
// std::filesystem::stem, because sidecar stems carry dots of their own ("05 -
// ... K550 - I. Molto allegro"). The caller has already classified `path` by
// `ext`, so the suffix is guaranteed present; a path that does not carry it
// comes back unchanged rather than mangled.
inline std::string sibling_sidecar_path(const std::string& path,
                                        const std::string& ext,
                                        const std::string& sibling_ext) {
    if (!ends_with(path, ext)) return path;
    return path.substr(0, path.size() - ext.size()) + sibling_ext;
}

// Split raw file bytes into lines, remembering per line whether a '\n'
// terminated it, so reassembly reproduces the exact terminator structure
// (including a missing final newline). '\r' bytes, if any, stay inside the line
// content and copy through verbatim. The split matches the product parsers'
// std::getline walk one for one, which is what lets a parsed marker index name
// a line number.
struct Line {
    std::string content;   // without the trailing '\n'
    bool had_newline;
};

inline std::vector<Line> split_lines(const std::string& data) {
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

inline std::string join_lines(const std::vector<Line>& lines) {
    std::string out;
    for (const Line& l : lines) {
        out += l.content;
        if (l.had_newline) out += '\n';
    }
    return out;
}

// Rewrite ONLY the leading `[#]<digits>` position token of a canonical marker
// line, leaving every later byte alone — the warp payload, the ` //<measure>`
// suffix, any trailing bytes. The caller has parsed the line with the product
// parser, so the token is exactly an optional '#' followed by a digit run.
inline std::string replace_leading_frame_token(const std::string& content,
                                               const std::string& frame_text) {
    size_t i = (!content.empty() && content[0] == '#') ? 1 : 0;
    size_t j = i;
    while (j < content.size() && content[j] >= '0' && content[j] <= '9') ++j;
    return content.substr(0, i) + frame_text + content.substr(j);
}

// --- siblings ---------------------------------------------------------------

// The `scale=` value from a .settings sidecar, read as ONE line and nothing
// else. DELIBERATELY NOT the whole-file settings loader
// (warptempo_settings::load): a sidecar being migrated may still hold legacy
// MM:SS.mmm trim timestamps, and a tool that needs one number has no business
// refusing a file over keys it does not read.
//
// THE ONE FIELD IT DOES READ CARRIES THE PRODUCT'S WHOLE VOCABULARY, because
// this number defines the lattice every phase reset then lands on: a scale the
// product would refuse must never build a map and author positions from it.
// So the KEY MATCH IS BYTE-EXACT — the line begins exactly `scale=`, with no
// whitespace trimmed off either side, matching the product's byte-exact
// settings lexer (a product-written file carries no padding) — and the VALUE
// takes the product scale grammar expression for expression
// (engine_settings_io.cpp): strict parse, strictly positive, inside
// [kScaleMin, kScaleMax], and pinned to the writer's ONE canonical spelling, so
// "1" and "100" refuse where "1.0000" loads.
inline std::expected<double, std::string> read_settings_scale(
    const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        return std::unexpected("cannot open settings sibling '" + path + "'");
    static const std::string kKey = "scale=";
    std::string line;
    std::string value;
    int found = 0;
    while (std::getline(f, line)) {
        if (line.compare(0, kKey.size(), kKey) != 0) continue;
        ++found;
        value = line.substr(kKey.size());
    }
    if (f.bad())
        return std::unexpected("read error in settings sibling '" + path + "'");
    if (found == 0) {
        return std::unexpected("no 'scale=' line in settings sibling '" +
                               path + "'");
    }
    // A duplicate key is load-fatal in both products; ambiguity about which
    // scale the map was authored under is not something to guess at.
    if (found > 1) {
        return std::unexpected("duplicate 'scale=' lines in settings sibling '" +
                               path + "'");
    }
    double v = 0.0;
    if (!parse_value_double(value, v) || !(v > 0.0) ||
        v < kScaleMin || v > kScaleMax ||
        format_value_double(v, 4) != value) {
        return std::unexpected(
            "scale value '" + value + "' in settings sibling '" + path +
            "' is not a finite double within [" +
            format_value_double(kScaleMin, 4) + ", " +
            format_value_double(kScaleMax, 4) + "] in canonical spelling");
    }
    return v;
}

// Build the target lattice for a .phaseresetmarkers file from its siblings.
// BOTH ARE REQUIRED and neither has a default: the map is the lattice, and a
// guessed identity map would silently author positions the GUI can never reach.
//
// The warp sibling parses with the PRODUCT parser, which is also the ordering
// contract's enforcement: a legacy MM:SS.mmm warp file refuses here with the
// parser's own diagnostic, so within a project the warp column is migrated (and
// snapped) BEFORE the phase reset column, never after.
//
// The chain is the product's own — resolve_warp_markers_for_render then
// build_warp_frame_map — so the lattice is the one the GUI target view paints
// and the CLI renders. The resolver's normalization lines go to stderr as they
// do everywhere else; they are the product's loudness, not this tool's noise.
inline std::expected<TargetLattice, std::string> build_target_lattice(
    const std::string& warp_path, const std::string& settings_path,
    long sample_rate, int64_t total_frames) {
    for (const std::string* p : {&warp_path, &settings_path}) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(*p, ec))
            return std::unexpected("missing required sibling '" + *p + "'");
    }
    auto markers = parse_warpmarkers_file(warp_path);
    if (!markers) {
        return std::unexpected("cannot read warp sibling '" + warp_path +
                               "': " + markers.error());
    }
    auto scale = read_settings_scale(settings_path);
    if (!scale) return std::unexpected(scale.error());

    auto built = build_warp_frame_map(
        resolve_warp_markers_for_render(*markers, sample_rate,
                                        static_cast<long>(total_frames)),
        *scale, sample_rate, static_cast<long>(total_frames));
    if (!built) {
        return std::unexpected("cannot build the warp map from '" + warp_path +
                               "': " + built.error());
    }
    TargetLattice lattice;
    lattice.map          = std::move(*built);
    lattice.sample_rate  = static_cast<double>(sample_rate);
    lattice.total_frames = total_frames;
    return lattice;
}

// --- the manufactured-coincidence refusal -----------------------------------

// One converted position: the line it came from, its exact position BEFORE the
// snap (a source-sample value), and the whole frame it landed on.
struct SnapRecord {
    int     line_number = 0;
    double  before      = 0.0;
    int64_t after       = 0;
};

// A SNAP MUST NEVER MANUFACTURE A COINCIDENCE. Two positions that were distinct
// before the snap and share a frame after it are a musical change, not a
// rounding detail: on the warp column an exact-frame group of 2+ survivors
// collapses at the render boundary into ONE synthetic 1.00 owner, dropping both
// authored tempos; on the phase reset column such a group collapses into one
// event. Positions that were ALREADY coincident stay each other's business —
// they were authored that way and the product already has its verdict for them.
//
// Returns the first offending pair's detail, or nullopt when the snap collided
// nothing new.
inline std::optional<std::string> first_manufactured_coincidence(
    const std::vector<SnapRecord>& records) {
    std::map<int64_t, const SnapRecord*> first_at;
    for (const SnapRecord& r : records) {
        auto [it, inserted] = first_at.emplace(r.after, &r);
        if (inserted) continue;
        if (it->second->before == r.before) continue;   // already coincident
        return "lines " + std::to_string(it->second->line_number) + " and " +
               std::to_string(r.line_number) +
               " are distinct before the snap and both land on frame " +
               format_authored_frame(r.after);
    }
    return std::nullopt;
}

// --- the read / backup / write contract -------------------------------------

// A failed file operation, in the shape the tools' one-line diagnostics take:
// "<prog>: <action> failed for '<path>': <detail>".
struct SidecarIoError {
    std::string action;
    std::string detail;
};

// Read the whole file as bytes — COMPLETELY OR NOT AT ALL. A short or failed
// read must refuse here, before any conversion and long before the destructive
// rename below: accepting a prefix would publish it as the finished file and
// move the real input aside as the backup.
//
// THE COMPLETENESS VERDICT IS A LENGTH COMPARE, not a stream flag, because the
// stream flags cannot carry it. `ss << f.rdbuf()` never touches `f`'s state at
// all, and it sets failbit on `ss` whenever ZERO characters were inserted —
// which an empty file and an unreadable one produce alike (a DIRECTORY with a
// recognized suffix is the concrete case: the read-only open succeeds, the
// buffer read yields nothing, and neither badbit nor eofbit is raised
// anywhere). So the input is first required to be a REGULAR FILE, and the bytes
// actually read are then compared against the size the filesystem reports.
// badbit is still checked — it is the one genuine hard-error signal the idiom
// does raise — but the length compare is what makes the read all-or-nothing. A
// file changing size underneath a one-shot rewrite refuses too, which is the
// right answer.
inline std::expected<std::string, SidecarIoError> read_file_whole(
    const std::string& path) {
    std::error_code fec;
    const std::filesystem::file_status st = std::filesystem::status(path, fec);
    if (fec)
        return std::unexpected(SidecarIoError{"Read",
                                              "cannot stat file: " + fec.message()});
    if (!std::filesystem::is_regular_file(st))
        return std::unexpected(SidecarIoError{"Read", "not a regular file"});
    const std::uintmax_t size = std::filesystem::file_size(path, fec);
    if (fec)
        return std::unexpected(SidecarIoError{
            "Read", "cannot read file size: " + fec.message()});
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return std::unexpected(SidecarIoError{"Read", "cannot open file"});
    std::ostringstream ss;
    ss << f.rdbuf();
    if (f.bad() || ss.bad())
        return std::unexpected(SidecarIoError{"Read", "cannot read file"});
    std::string data = ss.str();
    if (static_cast<std::uintmax_t>(data.size()) != size) {
        return std::unexpected(SidecarIoError{
            "Read", "short read (" + std::to_string(data.size()) + " of " +
                        std::to_string(size) + " bytes)"});
    }
    return data;
}

// The original moves aside as the backup, then the new text is written fresh at
// the original path. This rename IS the tools' recovery contract: the GUI
// serializers' atomic temp+fsync+rename writer is deliberately NOT imported
// here (a one-shot rewrite does not need it), and the original bytes surviving
// under '<path>.bk' is what makes a failed or torn write below recoverable
// rather than fatal.
//
// An existing backup is a refusal, never an overwrite: it holds an earlier
// run's input, the exact copy this contract exists to protect. Nothing has been
// written or moved at that point, so the refusal leaves the original in place.
//
// THE REFUSAL IS THE SYSCALL'S, IN ONE FILESYSTEM OPERATION. There is no
// preflight existence check, deliberately: POSIX rename REPLACES its
// destination, so a check-then-rename pair leaves a window in which a .bk
// appearing after the check — a second invocation on the same file is enough —
// is silently destroyed, which is exactly the loss the recovery contract exists
// to prevent. renameat2's RENAME_NOREPLACE makes the "only if the backup does
// not exist" condition part of the move itself and reports EEXIST when it does;
// that errno IS the refusal arm. This is a Linux-only primitive, and these
// Linux-only tools are entitled to it (the product's binaries are
// -march=native for one Arch host).
//
// Both write failures name the backup: it is by then the only copy of the
// input, and saying where it went is the whole point of taking it. There is no
// confirmation prompt; the backup is the answer the prompt used to ask for.
//
// `refusal_action` is the caller's own verb for the existing-backup refusal —
// the one arm that refuses the WHOLE act rather than reporting a filesystem
// failure, so it reads as the tool's verb ("Migrate", "Snap") where the others
// read as the operation's.
inline std::optional<SidecarIoError> backup_and_write(
    const std::string& path, const std::string& contents,
    const std::string& refusal_action) {
    const std::string backup_path = path + ".bk";
    if (::renameat2(AT_FDCWD, path.c_str(), AT_FDCWD, backup_path.c_str(),
                    RENAME_NOREPLACE) != 0) {
        if (errno == EEXIST) {
            return SidecarIoError{
                refusal_action, "backup '" + backup_path + "' already exists"};
        }
        return SidecarIoError{"Backup", "cannot rename to '" + backup_path +
                                            "': " + std::strerror(errno)};
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return SidecarIoError{
            "Write", "cannot open file for writing (the original is at '" +
                         backup_path + "')"};
    }
    out << contents;
    out.flush();
    if (!out) {
        return SidecarIoError{"Write", "write error (the original is at '" +
                                           backup_path + "')"};
    }
    return std::nullopt;
}

}  // namespace sidecar_snap
