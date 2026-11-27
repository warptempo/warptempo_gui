#include "project_model.h"

#include "directory_walk.h"   // the one non-throwing listing walk
#include "settings_io.h"      // kSidecarExtensions, the one sidecar list

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

// THE PROJECT NAMES' NATURAL ORDER (architect 2026-11-27, Thunar's way:
// names in natural, case-insensitive order; his caution: "Just make sure
// that this isn't going to pose problems where, for example, a 2-digit
// number is deemed to come after a 3-digit number ... Case blind should be
// fine because I don't plan to use letters, at least not in the first part
// of a project name. It'll be the KV number."). THE RULE, its one statement:
// the two names are walked together; A RUN OF ASCII DIGITS COMPARES BY ITS
// NUMBER — leading zeros skipped, then the longer run the greater, then
// digit by digit (no integer parse: a run of any length compares, with no
// overflow); ANY OTHER BYTE compares with the ASCII letters folded to lower
// case, a byte >= 0x80 as it is; a digit run met by a non-digit byte
// compares as bytes (its first digit against that byte); a proper prefix
// sorts first. So 9 < 10 < 64 < 100 < 550, "K64" < "K550", "a" < "B". No
// locale, no collation library. NAMES EQUAL UNDER THAT RULE ("064" and "64")
// FALL TO PLAIN BYTE ORDER (project_name_before), so the order is total and
// the same at every walk.
// A STRICT WEAK ORDER, as std::sort needs: each name maps to a sequence of
// tokens — a digit run's number, or one folded byte — compared
// lexicographically, a prefix first; the tokens are totally preordered (the
// numbers by value among themselves and, since no non-digit byte lies inside
// '0' .. '9', all as one block at the digits' place among the bytes), so the
// lexicographic compare is a total preorder, and the byte tie-break, itself a
// total order, refines it into a total order.
constexpr bool is_ascii_digit(unsigned char c) { return c >= '0' && c <= '9'; }
constexpr unsigned char folded_byte(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a')
                                  : c;
}
// Negative, zero or positive as `a` sorts before, with or after `b` under
// THE RULE above (the byte tie-break not applied).
constexpr int natural_name_compare(std::string_view a, std::string_view b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[j]);
        if (is_ascii_digit(ca) && is_ascii_digit(cb)) {
            std::size_t ie = i, je = j;
            while (ie < a.size() &&
                   is_ascii_digit(static_cast<unsigned char>(a[ie])))
                ++ie;
            while (je < b.size() &&
                   is_ascii_digit(static_cast<unsigned char>(b[je])))
                ++je;
            std::size_t iz = i, jz = j;
            while (iz < ie && a[iz] == '0') ++iz;
            while (jz < je && b[jz] == '0') ++jz;
            if (ie - iz != je - jz) return ie - iz < je - jz ? -1 : 1;
            for (std::size_t k = 0; k < ie - iz; ++k)
                if (a[iz + k] != b[jz + k])
                    return a[iz + k] < b[jz + k] ? -1 : 1;
            i = ie;
            j = je;
            continue;
        }
        const unsigned char fa = folded_byte(ca), fb = folded_byte(cb);
        if (fa != fb) return fa < fb ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}
// THE MEMBERSHIP LIST'S COMPARATOR: the natural order, its ties in plain
// byte order (THE RULE above).
constexpr bool project_name_before(std::string_view a, std::string_view b) {
    const int c = natural_name_compare(a, b);
    return c < 0 || (c == 0 && a < b);
}
// THE RULE's cases, stated at compile time.
static_assert(project_name_before("9", "10"));
static_assert(project_name_before("64_x", "550_x"));
static_assert(!project_name_before("550_x", "64_x"));
static_assert(project_name_before("K64", "K550"));
static_assert(project_name_before("a", "B"));
static_assert(project_name_before("A", "b"));
static_assert(natural_name_compare("064", "64") == 0);
static_assert(project_name_before("064", "64"));
static_assert(!project_name_before("64", "064"));
static_assert(project_name_before("K64", "K64_x"));
static_assert(!project_name_before("550 - 1", "550 - 1"));

// A sidecar extension is one of the product's three (kSidecarExtensions,
// sidecar_set.h — the one list), read by the walk below.
bool is_sidecar_extension(const std::string& ext) {
    for (const char* e : kSidecarExtensions) {
        if (ext == e) return true;
    }
    return false;
}

}  // namespace

std::expected<GuiProjectSource, std::string> resolve_project(
        const std::filesystem::path& folder) {
    // EVERY REFUSAL BELOW NAMES THE FOLDER BY ITS NAME AND A FILE BY ITS
    // BASENAME, never the projects path (the basename rule, failure.h):
    // these sentences are the Open project picker's second refusal, one line
    // on a notification card that CLIPS, and the leading
    // `/home/.../projects/` is the one part of them the reader already knows —
    // it is the device config's own key, the same for every project on the
    // machine. ONE SENTENCE PER REASON, on every road: the FATAL road
    // (gui_main's reopen, stderr + exit 1) prints the same words, and
    // startup's own zero case is what names the projects path out loud ("No
    // project under <projects_path>").
    const std::string name = folder.filename().string();
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        return std::unexpected("'" + name + "' is not a folder" +
                               (ec ? " (" + ec.message() + ")" : ""));
    }

    // ONE WALK, regular files only, extensions compared exactly. The walk
    // refuses out loud: a folder that passes is_directory can still refuse
    // to be enumerated (a mode this uid cannot read), and that is a different
    // fact from "no wav in there", so every error code is read and the
    // system's own words are the diagnosis. The step is the loop's tail rather
    // than the increment expression so the code it sets is read before the end
    // test can hide it. IT SPELLS ITS OWN increment rather than routing through
    // the walk owner (for_each_directory_entry, directory_walk.h) for exactly
    // that reason: this walk refuses MID-WALK with the offending entry's own
    // sentence, which a void callback cannot return.
    std::vector<std::string> wav_stems;
    std::vector<std::string> sidecar_stems;

    std::filesystem::directory_iterator it(folder, ec);
    if (ec) {
        return std::unexpected("Cannot read '" + name + "': " + ec.message());
    }
    const std::filesystem::directory_iterator walk_end;
    while (it != walk_end) {
        const std::filesystem::path entry = it->path();
        std::error_code entry_ec;
        const bool regular = it->is_regular_file(entry_ec);
        if (entry_ec) {
            return std::unexpected("Cannot read '" +
                                   entry.filename().string() + "' in '" +
                                   name + "': " + entry_ec.message());
        }
        if (regular) {
            const std::string ext  = entry.extension().string();
            const std::string stem = entry.stem().string();
            if (ext == ".wav") {
                wav_stems.push_back(stem);
            } else if (is_sidecar_extension(ext)) {
                sidecar_stems.push_back(stem);
            }
        }
        it.increment(ec);
        if (ec) {
            return std::unexpected("Cannot read '" + name + "': " +
                                   ec.message());
        }
    }
    // The walk's order is unspecified, so both name sets are sorted before any
    // message names one — and the sidecar stems are sorted WHOLE and deduped
    // rather than winnowed to a pair while walking, because a pair chosen by
    // the walk's order and sorted afterwards would still be the walk's pair:
    // the same folder always refuses with the same words, at any number of
    // stems. (A folder holds three sidecar names per piece, so both vectors are
    // a handful of strings and the sort is free.)
    std::sort(wav_stems.begin(), wav_stems.end());
    std::sort(sidecar_stems.begin(), sidecar_stems.end());
    sidecar_stems.erase(
        std::unique(sidecar_stems.begin(), sidecar_stems.end()),
        sidecar_stems.end());

    auto make = [&](const std::string& stem) {
        GuiProjectSource out;
        out.name   = name;
        out.folder = folder;
        out.source = folder / (stem + ".wav");
        return out;
    };

    if (sidecar_stems.size() > 1) {
        // Several sidecar stems: more than one piece claims one folder, and
        // there is no honest way to choose between them. The whole sorted set
        // is named — one spelling at every count above one, so the refusal
        // reads the same on any filesystem.
        std::string list;
        for (size_t i = 0; i < sidecar_stems.size(); ++i) {
            if (i) list += ", ";
            list += "'" + sidecar_stems[i] + "'";
        }
        return std::unexpected("Several sidecar stems in '" + name + "': " +
                               list);
    }
    if (!sidecar_stems.empty()) {
        const std::string& sidecar_stem = sidecar_stems.front();
        // THE SIDECAR NAMES THE SOURCE. The wav must exist, and every OTHER
        // wav in the root is the legacy layout: an output that has not moved
        // into render/ yet.
        const bool have_source =
            std::find(wav_stems.begin(), wav_stems.end(), sidecar_stem) !=
            wav_stems.end();
        if (!have_source) {
            return std::unexpected("No '" + sidecar_stem + ".wav' in '" +
                                   name + "' for its sidecars");
        }
        for (const std::string& stem : wav_stems) {
            if (stem != sidecar_stem) {
                // Single-quoted like its five sibling refusals in this
                // function (2026-09-01, the product's one quoting form); the
                // IMPERATIVE stays — this refusal IS the instruction.
                return std::unexpected("Move '" + stem + ".wav' into render/");
            }
        }
        return make(sidecar_stem);
    }
    // NO SIDECAR: a NEW project iff exactly one wav.
    if (wav_stems.empty()) {
        return std::unexpected("No wav in '" + name + "'");
    }
    if (wav_stems.size() > 1) {
        return std::unexpected("Several wavs and no sidecar in '" + name +
                               "'");
    }
    return make(wav_stems.front());
}

std::vector<std::string> enumerate_project_names(
        const std::filesystem::path& projects_path) {
    // A MISSING OR UNREADABLE projects_path ANSWERS EMPTY, and a walk that
    // fails mid-way answers what it saw — both the caller's "No project under
    // <projects_path>" line, neither of them a throw (directory_walk.h owns
    // the non-throwing walk; the entry query takes its error_code overload
    // here for the same reason).
    std::vector<std::string> names;
    std::error_code ec;
    for_each_directory_entry(projects_path, ec, [&names](
            const std::filesystem::directory_entry& de) {
        std::error_code entry_ec;
        if (!de.is_directory(entry_ec) || entry_ec) return;
        // A FOLDER THE DEVICE CONFIG CANNOT NAME IS NOT A PROJECT, whatever
        // its shape, because every successful open writes that name into
        // `last_project` (main.cpp) — so the ONE membership list every opening
        // road walks yields only names the config can carry. The grammar's own
        // owner answers (is_last_project_name, device_config.h); this walk
        // spells no rule of its own.
        std::string name = de.path().filename().string();
        if (is_last_project_name(name)) names.push_back(std::move(name));
    });
    // THE NATURAL ORDER (project_name_before, THE RULE at its head).
    std::sort(names.begin(), names.end(), [](const std::string& x,
                                             const std::string& y) {
        return project_name_before(x, y);
    });
    return names;
}

std::expected<GuiProjectSource, std::string> startup_source(
        const DeviceConfig& config) {
    const std::filesystem::path projects_path(config.projects_path);

    // THE REMEMBERED PROJECT FIRST, silently falling through when the folder
    // it names is gone or invalid (the load-lenient class — the program wrote
    // the name; what happened to the folder since is ordinary use, not a hand
    // edit). A name that fails last_project's own grammar never reaches here:
    // the config reader refuses it at startup.
    if (!config.last_project.empty()) {
        auto remembered = resolve_project(projects_path / config.last_project);
        if (remembered) return remembered;
    }
    // Then the FIRST VALID project in the membership list's natural order.
    for (const std::string& name : enumerate_project_names(projects_path)) {
        auto candidate = resolve_project(projects_path / name);
        if (candidate) return candidate;
    }
    return std::unexpected("No project under " + projects_path.string());
}
