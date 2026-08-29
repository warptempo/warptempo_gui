#include "renders_dir.h"

#include "directory_walk.h"         // the one non-throwing listing walk
#include "render_cache.h"           // kFingerprintSidecarExtension
#include "render_output_naming.h"   // render_output_directory / _stem

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// The batch folder's path composition and its enumeration: the flat list of
// valid render entries under <source parent>/tmp/, plus the per-entry
// .settings path helper. The render player's listings and its load road call
// the latter two. Beside them, the DELIVERABLE folder's prune (the contract is
// at its declaration).

// The project folder: the source's parent, or "." for a bare filename.
static std::filesystem::path project_folder_of(
        const std::string& source_audio_path) {
    std::filesystem::path parent =
        std::filesystem::path(source_audio_path).parent_path();
    if (parent.empty()) parent = std::filesystem::path(".");
    return parent;
}

std::filesystem::path project_batch_root(
        const std::string& source_audio_path) {
    return project_folder_of(source_audio_path) / kBatchFolderName;
}

// Enumerate the flat render-entry list under <source parent>/tmp/.
// Returns an empty vector if no source path is set or if the batch root
// contains no valid entries.
std::vector<AppState::RenderEntry>
GuiRendersDir::enumerate_render_entries() {
    std::vector<AppState::RenderEntry> out;
    if (app.source_audio_path.empty()) return out;
    const std::filesystem::path batch_root =
        project_batch_root(app.source_audio_path);
    std::error_code ec;
    if (!std::filesystem::is_directory(batch_root, ec)) return out;

    auto leading_int = [](const std::string& s, size_t& end_out) -> int {
        int v = 0;
        size_t i = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + (s[i] - '0');
            ++i;
        }
        end_out = i;
        return v;
    };

    // BOTH WALKS ARE NON-THROWING (directory_walk.h). A batch folder removed
    // by the trash road, or the whole batch root swept away, while the player
    // is listing or a dispatch is numbering answers WHAT IT SAW — a partial or
    // empty entry list, which every caller already treats as "no cells" —
    // instead of terminating the process out of a range-for's increment.
    struct BatchSlot { int idx; std::filesystem::path path; };
    std::vector<BatchSlot> batches;
    for_each_directory_entry(batch_root, ec, [&](
            const std::filesystem::directory_entry& de) {
        std::error_code entry_ec;
        if (!de.is_directory(entry_ec) || entry_ec) return;
        const std::string name = de.path().filename().string();
        size_t end = 0;
        const int v = leading_int(name, end);
        if (end == 0 || end >= name.size() || name[end] != '_') return;
        batches.push_back({v, de.path()});
    });
    std::sort(batches.begin(), batches.end(),
              [](const BatchSlot& a, const BatchSlot& b) {
                  return a.idx < b.idx;
              });

    for (const auto& b : batches) {
        struct WavSlot {
            int idx;
            std::filesystem::path path;
            std::string basename;
        };
        std::vector<WavSlot> wavs;
        for_each_directory_entry(b.path, ec, [&](
                const std::filesystem::directory_entry& fe) {
            std::error_code entry_ec;
            if (!fe.is_regular_file(entry_ec) || entry_ec) return;
            if (fe.path().extension() != ".wav") return;
            const std::string stem = fe.path().stem().string();
            size_t end = 0;
            const int v = leading_int(stem, end);
            if (end == 0) return;
            if (end != stem.size() && stem[end] != '_') return;
            wavs.push_back({v, fe.path(), stem});
        });
        std::sort(wavs.begin(), wavs.end(),
                  [](const WavSlot& a, const WavSlot& b) {
                      return a.idx < b.idx;
                  });
        for (auto& w : wavs) {
            AppState::RenderEntry e;
            e.batch_folder = b.path;
            e.basename     = std::move(w.basename);
            e.wav_path     = std::move(w.path);
            out.push_back(std::move(e));
        }
    }
    return out;
}

// The entry's <basename>.settings path, beside its wav. The .settings snapshot
// is frozen at dispatch (the dispatch writer writes it once, seeding the
// queue/dispatch-moment tab/zoom/viewport/playhead/W-P) and never rewritten;
// the `'` load-in-place reads it for the ENGINE BLOCK, which with the marker
// pair is the whole of what that act applies (the rule at
// GuiInputHandler::apply_recipe_in_place, input_handler.h) — the view keys
// beside it are there because the schema is whole, not because the load wants
// them.
std::filesystem::path GuiRendersDir::settings_path(
        const AppState::RenderEntry& e) {
    return e.batch_folder / (e.basename + ".settings");
}

// THE DELIVERABLE FOLDER'S PRUNE. The whole contract — the definition it
// serves, its two callers, the CLI asymmetry, the running-render case and the
// refusals — is at the declaration (renders_dir.h); what is stated here is
// only what the shape of the body is for.
void prune_render_folder(const std::string& source_audio_path,
                         const EngineSettings& es) {
    if (source_audio_path.empty()) return;
    // The naming owner's stem, never composed by hand. An empty one is the
    // breach backstop: it matches nothing, and pruning "everything that is not
    // the empty stem" would empty the folder.
    const std::string stem = render_output_stem(es);
    if (stem.empty()) return;

    const std::filesystem::path dir =
        render_output_directory(source_audio_path);
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;

    // CLASSIFY WHOLE, THEN REMOVE. A removal inside the walk would change the
    // directory a live directory_iterator is reading; the mirror's own
    // deletion pass is two passes for the same reason (external_sync.h rule 1).
    // The walk never throws (directory_walk.h) and stops at the first fault —
    // every entry it did reach is a positive identification, so the list below
    // is removed whatever `ec` ended up holding, and nothing past the fault is.
    std::vector<std::filesystem::path> doomed;
    for_each_directory_entry(dir, ec, [&](
            const std::filesystem::directory_entry& de) {
        std::error_code entry_ec;
        if (!de.is_regular_file(entry_ec) || entry_ec) return;   // dirs, faults
        const std::filesystem::path& p = de.path();
        const std::string ext = p.extension().string();
        if (ext != ".wav" && ext != kFingerprintSidecarExtension) return;
        if (p.stem().string() == stem) return;            // the title's own
        doomed.push_back(p);
    });

    for (const std::filesystem::path& p : doomed) {
        std::error_code rm_ec;
        std::filesystem::remove(p, rm_ec);
        if (rm_ec) {
            std::fprintf(stderr,
                "warptempo_gui: Could not remove '%s': %s\n",
                p.string().c_str(), rm_ec.message().c_str());
        }
    }
}
