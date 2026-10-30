#include "project_model.h"

#include "directory_walk.h"   // the one non-throwing listing walk
#include "settings_io.h"      // kSidecarExtensions, the one sidecar list

#include <algorithm>
#include <system_error>
#include <utility>

namespace {

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
    std::sort(names.begin(), names.end());
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
    // Then the FIRST VALID project in name order.
    for (const std::string& name : enumerate_project_names(projects_path)) {
        auto candidate = resolve_project(projects_path / name);
        if (candidate) return candidate;
    }
    return std::unexpected("No project under " + projects_path.string());
}
