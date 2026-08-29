#include "project_model.h"

#include "directory_walk.h"   // the one non-throwing listing walk

#include <algorithm>
#include <system_error>
#include <utility>

namespace {

// The three sidecar extensions, as the product writes them. One list, read by
// the walk below and nowhere else.
constexpr const char* kSidecarExtensions[] = {
    ".warpmarkers", ".phaseresetmarkers", ".settings",
};

bool is_sidecar_extension(const std::string& ext) {
    for (const char* e : kSidecarExtensions) {
        if (ext == e) return true;
    }
    return false;
}

}  // namespace

std::expected<GuiProjectSource, std::string> resolve_project(
        const std::filesystem::path& folder) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        return std::unexpected("'" + folder.string() + "' is not a folder" +
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
        return std::unexpected("Cannot read '" + folder.string() + "': " +
                               ec.message());
    }
    const std::filesystem::directory_iterator walk_end;
    while (it != walk_end) {
        const std::filesystem::path entry = it->path();
        std::error_code entry_ec;
        const bool regular = it->is_regular_file(entry_ec);
        if (entry_ec) {
            return std::unexpected("Cannot read '" +
                                   entry.filename().string() + "' in '" +
                                   folder.string() + "': " +
                                   entry_ec.message());
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
            return std::unexpected("Cannot read '" + folder.string() +
                                   "': " + ec.message());
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

    const std::string name = folder.filename().string();
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
                return std::unexpected("Move `" + stem + ".wav` into render/");
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
        const DeviceConfig& config, const char* argument) {
    const std::filesystem::path projects_path(config.projects_path);

    if (argument == nullptr) {
        // THE REMEMBERED PROJECT FIRST, silently falling through when the
        // folder it names is gone or invalid (the load-lenient class — the
        // program wrote the name; what happened to the folder since is
        // ordinary use, not a hand edit). A name that fails last_project's own
        // grammar never reaches here: the config reader refuses it at startup.
        if (!config.last_project.empty()) {
            auto remembered = resolve_project(projects_path /
                                              config.last_project);
            if (remembered) return remembered;
        }
        // Then the FIRST VALID project in name order.
        for (const std::string& name : enumerate_project_names(projects_path)) {
            auto candidate = resolve_project(projects_path / name);
            if (candidate) return candidate;
        }
        return std::unexpected("No project under " + projects_path.string());
    }

    // WITH AN ARGUMENT: the argument's folder must sit directly under the
    // projects path, and the argument must BE that folder's resolved source.
    //
    // BOTH QUESTIONS ARE ASKED OF THE SPELLING THE USER GAVE, and the second
    // is asked BY FILESYSTEM IDENTITY. The folder is the ARGUMENT'S OWN
    // parent — made absolute against the working directory and lexically
    // normalized, which is spelling work only and follows no link — so
    // "directly under the projects path" is a fact about where the user says
    // the file is, not about where a symlink at that name happens to point.
    // Then the source compare is std::filesystem::equivalent rather than a
    // path compare, because the walk that resolved the folder followed links
    // too (is_regular_file, above): a project's source may legitimately BE a
    // symlink, and a canonicalized argument would then name the link's target,
    // sit outside the projects path, and be refused as not a project source —
    // the same file the picker opens without complaint. equivalent asks the
    // one question that matters, whether the two spellings name the same file,
    // and it refuses on its own when the argument names nothing at all.
    // "Directly under" stays equivalent for its own reason: a trailing slash
    // or a symlink in the CONFIG's spelling must not fail a folder that really
    // is there.
    std::error_code ec;
    const std::filesystem::path spelled =
        std::filesystem::absolute(std::filesystem::path(argument), ec)
            .lexically_normal();
    auto refuse = [&]() {
        return std::unexpected(std::string(argument) +
                               " is not a project source under " +
                               projects_path.string());
    };
    if (ec) return refuse();
    const std::filesystem::path folder = spelled.parent_path();
    if (!std::filesystem::equivalent(folder.parent_path(), projects_path, ec) ||
        ec) {
        return refuse();
    }
    // THE NAME MUST BE ONE THE DEVICE CONFIG CAN CARRY — the enumeration's
    // own membership rule, asked here because this road never walks the
    // enumeration. The open would write this name into `last_project`, so a
    // folder the grammar refuses is not a project source on this road either,
    // and it refuses with the road's own sentence rather than a second
    // vocabulary.
    if (!is_last_project_name(folder.filename().string())) return refuse();
    auto resolved = resolve_project(folder);
    if (!resolved) return std::unexpected(std::move(resolved.error()));
    if (!std::filesystem::equivalent(spelled, resolved->source, ec) || ec) {
        return refuse();
    }
    return resolved;
}
