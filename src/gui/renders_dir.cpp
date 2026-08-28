#include "renders_dir.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// The batch folder's path composition and its enumeration: the flat list of
// valid render entries under <source parent>/tmp/, plus the per-entry
// .settings path helper. The `l` launcher and the `'` load editor call the
// latter two.

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

    struct BatchSlot { int idx; std::filesystem::path path; };
    std::vector<BatchSlot> batches;
    for (const auto& de :
         std::filesystem::directory_iterator(batch_root, ec)) {
        if (!de.is_directory()) continue;
        const std::string name = de.path().filename().string();
        size_t end = 0;
        const int v = leading_int(name, end);
        if (end == 0 || end >= name.size() || name[end] != '_') continue;
        batches.push_back({v, de.path()});
    }
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
        for (const auto& fe :
             std::filesystem::directory_iterator(b.path, ec)) {
            if (!fe.is_regular_file()) continue;
            if (fe.path().extension() != ".wav") continue;
            const std::string stem = fe.path().stem().string();
            size_t end = 0;
            const int v = leading_int(stem, end);
            if (end == 0) continue;
            if (end != stem.size() && stem[end] != '_') continue;
            wavs.push_back({v, fe.path(), stem});
        }
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
