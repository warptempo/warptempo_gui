// snap_sidecar_to_grid — land every authored position in one already-migrated
// marker sidecar on its canonical lattice, at the original path, keeping the
// pre-snap bytes as '<original-path>.bak'.
//
// Authored positions are deterministic against canonical, viewport-independent
// lattices: the warp column against the source view's zoom-2 column grid, the
// phase reset column against the zoom-2 TARGET column lattice the live warp map
// defines. Both lattices, why a tool can reproduce them at all, and the
// PRECONDITION that reproduction rests on are stated once at the head of
// sidecar_snap_common.h. A file whose positions came from somewhere else — an
// older migration that rounded rather than snapped, a hand edit — is snapped
// onto the lattice here, so the GUI's own gestures can reach every position it
// holds.
//
// Usage:
//     snap_sidecar_to_grid [--check] <source.wav> <sidecar>
//
// where <sidecar> is a .warpmarkers or .phaseresetmarkers file in the FRAMES
// format. A .settings file is not a subject: its trim bounds are a render
// window, not an authored musical instant, and nothing snaps them. --check
// reports what would change and writes nothing at all.
//
// THE SOURCE WAV IS THE LATTICE'S CLOCK: sample rate and total frames both come
// from audio_probe, and a probe failure is reported verbatim — the audio owner's
// diagnostic is the one worth reading. THE PROBED RATE IS ALSO A GATE: a rate
// not divisible by 50 has no width-free canonical lattice behind it (the GUI's
// painted grid is width-quantized there) and refuses on both kinds before
// anything is read or snapped — rate_has_canonical_lattice carries the whole
// reasoning, including the short-source boundary the tools cannot detect.
//
// STRICT PARSE FIRST, THROUGH THE PRODUCT PARSER. The file must load exactly as
// the GUI and the CLI would load it; anything else refuses with the parser's own
// line-tagged diagnostic. That is also what refuses an UN-MIGRATED file cleanly:
// a legacy MM:SS.mmm position is not a canonical frame position and never
// reaches the snap. The rewrite that follows is TEXTUAL and replaces only each
// changed line's leading [#]<digits> token — the warp payload, the ` //<measure>`
// suffix and the terminator structure ride through byte-identically, so a snap
// touches positions and nothing else.
//
// TWO REFUSALS BEYOND THE PARSE, both of which write nothing:
//   - a post-snap DUPLICATE frame that was not already a duplicate. A snap must
//     never manufacture a coincidence: an exact-frame group collapses at the
//     render boundary (warp into one synthetic 1.00 owner, phase resets into one
//     event), which is a musical change no tool may silently cause.
//   - a position PAST THE SOURCE TOTAL, on either column. Past-EOF is
//     load-fatal in the product (first_past_eof_wall_defect), so the tool must
//     neither write such a position nor launder one that ARRIVED that way back
//     into range: both columns refuse a stored position past total-1 before
//     snapping it — the marker parsers are grammar and ordering only, so an
//     illegal position parses fine and reaches the snap. The warp column then
//     also refuses a snapped RESULT past the wall, its lattice having no clamp
//     of its own; the target lattice instead CLAMPS its landing, which is a
//     different thing (walls win over the grid, so the last column rests at
//     the wall).
//
// The write contract is the migration tool's, shared verbatim from
// sidecar_snap_common.h: convert fully in memory, move the original aside as
// '<path>.bak' through renameat2 RENAME_NOREPLACE (an existing .bak is a
// refusal), then write fresh at the original path. A run that changes nothing
// writes nothing and takes no backup, in either mode. The suffix is the one
// the repository's .gitignore already covers, so a backup taken beside a
// tracked sidecar never shows up as clutter or rides a checkpoint commit.
//
// Build (from the project root):
//     cmake -B build-tools -S tools && cmake --build build-tools

#include "sidecar_snap_common.h"

#include "../src/audio_io/audio_probe.h"
#include "../src/parser/phaseresetmarkers_parse.h"
#include "../src/parser/warpmarkers_parse.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

const char* kProg = "snap_sidecar_to_grid";

// Unified diagnostic shape for every error but the usage line. Single-space
// joined, no double spaces.
void diag(const std::string& action, const std::string& path,
          const std::string& detail) {
    std::cerr << kProg << ": " << action << " failed for '" << path
              << "': " << detail << '\n';
}

void usage() {
    std::cerr << kProg << " [--check] <source.wav> <sidecar>\n";
}

enum class Kind { Warp, PhaseReset };

constexpr const char* kWarpExt   = ".warpmarkers";
constexpr const char* kResetExt  = ".phaseresetmarkers";
constexpr const char* kSettingsExt = ".settings";

// One line's verdict, in file order. `content` is the original line text; the
// rewrite below replaces only its leading position token when `changed`.
struct Snapped {
    int     line_number = 0;
    int64_t before      = 0;
    int64_t after       = 0;
    bool    changed     = false;
};

// Report the changed positions to stdout, one line each. The signed delta is
// what tells a rounding-residue snap (a few dozen frames) apart from a position
// that was never on the lattice at all.
void report(const std::vector<Snapped>& snapped, size_t total_markers) {
    size_t changed = 0;
    for (const Snapped& s : snapped) {
        if (!s.changed) continue;
        ++changed;
        const int64_t delta = s.after - s.before;
        std::cout << "line " << s.line_number << ": "
                  << format_authored_frame(s.before) << " -> "
                  << format_authored_frame(s.after) << " ("
                  << (delta >= 0 ? "+" : "") << delta << ")\n";
    }
    if (changed == 0) {
        std::cout << "already snapped (" << total_markers << " markers)\n";
    } else {
        std::cout << changed << " of " << total_markers
                  << " markers changed\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    bool check_only = false;
    int arg = 1;
    if (argc > 1 && std::string(argv[1]) == "--check") {
        check_only = true;
        arg = 2;
    }
    if (argc - arg != 2) {
        usage();
        return 1;
    }
    const std::string wav_path = argv[arg];
    const std::string path     = argv[arg + 1];

    Kind kind;
    if (sidecar_snap::ends_with(path, kWarpExt)) {
        kind = Kind::Warp;
    } else if (sidecar_snap::ends_with(path, kResetExt)) {
        kind = Kind::PhaseReset;
    } else if (sidecar_snap::ends_with(path, kSettingsExt)) {
        // Named apart from the generic refusal: a .settings file is a plausible
        // thing to hand this tool, and "wrong extension" would read as a typo
        // rather than as the ruling it is.
        diag("Snap", path,
             "a .settings file holds no snapped position (trim bounds are a "
             "render window, not an authored instant)");
        return 1;
    } else {
        diag("Snap", path,
             "unrecognized extension (expected .warpmarkers or "
             ".phaseresetmarkers)");
        return 1;
    }

    // The source audio is the lattice's clock. Its diagnostic passes through
    // verbatim — the audio owner's message is the informative one.
    const auto probed = audio_probe(wav_path);
    if (!probed) {
        diag("Probe", wav_path, probed.error());
        return 1;
    }
    const long    sample_rate  = probed->sample_rate;
    const int64_t total_frames = probed->frames;
    if (sample_rate <= 0 || total_frames <= 0) {
        diag("Probe", wav_path, "source has no usable rate or length");
        return 1;
    }
    // BOTH LATTICES ARE THE SAME CLOCK'S, so both kinds take this gate, and it
    // stands before the file is even read: at a rate the canonical lattice does
    // not cover there is nothing to snap TO, and a snap run would relabel every
    // position against a grid the product does not paint.
    if (!sidecar_snap::rate_has_canonical_lattice(sample_rate)) {
        diag("Snap", path, sidecar_snap::lattice_rate_refusal(sample_rate));
        return 1;
    }

    // The bytes come in COMPLETELY OR NOT AT ALL, before anything else looks at
    // the file (the contract at read_file_whole).
    auto data = sidecar_snap::read_file_whole(path);
    if (!data) {
        diag(data.error().action, path, data.error().detail);
        return 1;
    }
    std::vector<sidecar_snap::Line> lines =
        sidecar_snap::split_lines(*data);

    // The STRICT product parse: the file must load exactly as both binaries
    // would load it. This is also the un-migrated refusal — a legacy timestamp
    // is not a canonical frame position — and the source of the positions the
    // snap works from.
    std::vector<int64_t> positions;
    if (kind == Kind::Warp) {
        auto markers = parse_warpmarkers_file(path);
        if (!markers) {
            diag("Parse", path, markers.error());
            return 1;
        }
        for (const WarpMarker& m : *markers) positions.push_back(m.time_frame);
    } else {
        auto resets = parse_phaseresetmarkers_file(path);
        if (!resets) {
            diag("Parse", path, resets.error());
            return 1;
        }
        for (const PhaseResetMarker& m : *resets)
            positions.push_back(m.time_frame);
    }

    // Both parsers walk the file with std::getline and refuse every line that
    // is not a marker (a blank line included), so marker i IS line i. The
    // compare is a tripwire on that equivalence, not a recovery path: if it
    // ever fired, the textual rewrite below would be writing positions onto the
    // wrong lines. It is also the only thing standing between this tool and a
    // file rewritten between the two reads — the product parsers take a PATH,
    // so the bytes above and the markers here come from two opens.
    if (positions.size() != lines.size()) {
        diag("Snap", path,
             "internal line/marker mismatch (" +
                 std::to_string(lines.size()) + " lines, " +
                 std::to_string(positions.size()) + " markers)");
        return 1;
    }
    if (positions.empty()) {
        report({}, 0);
        return 0;
    }

    // The phase reset column's lattice is the target view's, so it needs the
    // live warp map: the sidecar's own .warpmarkers and .settings siblings,
    // both required (build_target_lattice's contract).
    sidecar_snap::TargetLattice lattice;
    if (kind == Kind::PhaseReset) {
        auto built = sidecar_snap::build_target_lattice(
            sidecar_snap::sibling_sidecar_path(path, kResetExt, kWarpExt),
            sidecar_snap::sibling_sidecar_path(path, kResetExt, kSettingsExt),
            sample_rate, total_frames);
        if (!built) {
            diag("Snap", path, built.error());
            return 1;
        }
        lattice = std::move(*built);
    }

    // Snap every position, disabled markers included — a disabled marker is one
    // enable away from authoring, and its position is authored data now.
    std::vector<Snapped> snapped;
    snapped.reserve(positions.size());
    std::vector<sidecar_snap::SnapRecord> records;
    records.reserve(positions.size());
    for (size_t i = 0; i < positions.size(); ++i) {
        const int     line_number = static_cast<int>(i + 1);
        const int64_t before      = positions[i];
        // An already-migrated position is a whole frame, so its exact source
        // position IS that frame.
        const double exact = static_cast<double>(before);
        int64_t after = 0;
        if (kind == Kind::Warp) {
            // PAST-EOF IS A REFUSAL IN BOTH DIRECTIONS, and the INPUT side
            // comes first. The marker parser is the grammar and ordering
            // parser only — the product's own EOF verdict is the later
            // first_past_eof_wall_defect pass — so a position already past
            // total-1 reaches here parsed but load-fatal, and snapping it
            // would quietly pull adversarial data back into the legal range
            // and publish it. The RESULT side below is the different thing:
            // a legal position whose half-column move at the very end of the
            // source would carry it past the wall.
            if (before > total_frames - 1) {
                diag("Snap", path,
                     "line " + std::to_string(line_number) + ": " +
                         format_authored_frame(before) +
                         " is past the end of the source (" +
                         format_authored_frame(total_frames) + " frames)");
                return 1;
            }
            after = sidecar_snap::snap_to_source_lattice(
                exact, static_cast<double>(sample_rate));
            if (after > total_frames - 1) {
                diag("Snap", path,
                     "line " + std::to_string(line_number) + ": " +
                         format_authored_frame(before) + " snaps to " +
                         format_authored_frame(after) +
                         ", past the end of the source (" +
                         format_authored_frame(total_frames) + " frames)");
                return 1;
            }
        } else {
            auto landed = sidecar_snap::snap_to_target_lattice(exact, lattice);
            if (!landed) {
                diag("Snap", path,
                     "line " + std::to_string(line_number) + ": " +
                         format_authored_frame(before) + ": " + landed.error());
                return 1;
            }
            after = *landed;
        }
        snapped.push_back({line_number, before, after, after != before});
        records.push_back({line_number, exact, after});
    }

    if (auto collision = sidecar_snap::first_manufactured_coincidence(records)) {
        diag("Snap", path, *collision);
        return 1;
    }

    report(snapped, snapped.size());

    size_t changed = 0;
    for (const Snapped& s : snapped) changed += s.changed ? 1 : 0;
    // A run that changes nothing writes nothing and takes no backup — in either
    // mode. The .bak is the recovery copy of bytes that were replaced, and here
    // none were.
    if (changed == 0 || check_only) return 0;

    for (const Snapped& s : snapped) {
        if (!s.changed) continue;
        sidecar_snap::Line& line = lines[static_cast<size_t>(s.line_number - 1)];
        line.content = sidecar_snap::replace_leading_frame_token(
            line.content, format_authored_frame(s.after));
    }
    const std::string rewritten = sidecar_snap::join_lines(lines);

    if (auto err = sidecar_snap::backup_and_write(path, rewritten, "Snap")) {
        diag(err->action, path, err->detail);
        return 1;
    }
    return 0;
}
