#include "project_model.h"

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
    // test can hide it.
    std::vector<std::string> wav_stems;
    std::string              sidecar_stem;
    bool                     sidecar_stem_conflict = false;
    std::string              sidecar_stem_other;

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
                if (sidecar_stem.empty()) {
                    sidecar_stem = stem;
                } else if (stem != sidecar_stem && !sidecar_stem_conflict) {
                    sidecar_stem_conflict = true;
                    sidecar_stem_other    = stem;
                }
            }
        }
        it.increment(ec);
        if (ec) {
            return std::unexpected("Cannot read '" + folder.string() +
                                   "': " + ec.message());
        }
    }
    // The walk's order is unspecified, so the names are sorted before any
    // message names one: the same folder always refuses with the same words.
    std::sort(wav_stems.begin(), wav_stems.end());

    const std::string name = folder.filename().string();
    auto make = [&](const std::string& stem) {
        GuiProjectSource out;
        out.name   = name;
        out.folder = folder;
        out.source = folder / (stem + ".wav");
        return out;
    };

    if (sidecar_stem_conflict) {
        // Two sidecar stems: two pieces claim one folder, and there is no
        // honest way to choose between them.
        const std::string a = std::min(sidecar_stem, sidecar_stem_other);
        const std::string b = std::max(sidecar_stem, sidecar_stem_other);
        return std::unexpected("Two sidecar stems in '" + name + "': '" + a +
                               "' and '" + b + "'");
    }
    if (!sidecar_stem.empty()) {
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
    std::vector<std::string> names;
    std::error_code ec;
    std::filesystem::directory_iterator it(projects_path, ec);
    if (ec) return names;   // missing or unreadable: empty, the caller's line
    const std::filesystem::directory_iterator walk_end;
    while (it != walk_end) {
        std::error_code entry_ec;
        const bool dir = it->is_directory(entry_ec);
        if (!entry_ec && dir) names.push_back(it->path().filename().string());
        it.increment(ec);
        if (ec) break;      // a walk that fails mid-way answers what it saw
    }
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
    // projects path, and the argument must be that folder's resolved source.
    // The argument is taken weakly canonical so a symlinked or relative
    // spelling still passes, and "directly under" is asked of the filesystem
    // (std::filesystem::equivalent on the folder's parent and the projects
    // path), so a trailing slash or a symlink in the config's spelling cannot
    // fail a folder that really is there.
    std::error_code ec;
    const std::filesystem::path given(argument);
    const std::filesystem::path given_canon =
        std::filesystem::weakly_canonical(given, ec);
    auto refuse = [&]() {
        return std::unexpected(std::string(argument) +
                               " is not a project source under " +
                               projects_path.string());
    };
    if (ec) return refuse();
    const std::filesystem::path folder = given_canon.parent_path();
    if (!std::filesystem::equivalent(folder.parent_path(), projects_path, ec) ||
        ec) {
        return refuse();
    }
    auto resolved = resolve_project(folder);
    if (!resolved) return std::unexpected(std::move(resolved.error()));
    if (resolved->source != given_canon) return refuse();
    return resolved;
}
