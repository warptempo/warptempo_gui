#include "score_viewer_file.h"

#include "app_state.h"
#include "settings_io.h"      // atomic_write_string_to_path

#include "frame_format.h"     // parse_authored_frame / format_authored_frame
#include "parse_text_util.h"  // warptempo_parse::prefix_line_error
#include "settings_file.h"    // warptempo_settings::scan_key_value_file, bad_value

#include <cstdio>
#include <fstream>
#include <system_error>

std::filesystem::path score_viewer_file_path(
        const std::filesystem::path& project_folder) {
    return project_folder / "score" / "viewer.txt";
}

std::filesystem::path score_viewer_project_folder(const AppState& app) {
    std::filesystem::path folder =
        std::filesystem::path(app.source_audio_path).parent_path();
    if (folder.empty()) folder = std::filesystem::path(".");
    return folder;
}

GuiScoreViewerFile score_viewer_file_defaults(
        const std::filesystem::path& project_folder) {
    GuiScoreViewerFile out;
    // THE MOVEMENT'S OWN FOLDER NEEDS NO TYPING: a pages.txt in
    // `<project>/score/` is the score (the tool's output place,
    // tools/score_extract.py's head). A failed stat reads as no score.
    std::error_code ec;
    if (std::filesystem::exists(project_folder / "score" / "pages.txt", ec))
        out.score_path = "./score";
    return out;
}

std::optional<std::expected<GuiScoreViewerValue, std::string>>
validate_score_viewer_value(const std::string& key, const std::string& value) {
    using R = std::expected<GuiScoreViewerValue, std::string>;
    GuiScoreViewerValue out;
    if (key == "score_path") {
        // Verbatim, the empty value included (no score): the lexer split at
        // the first '=' and a line holds no newline, which is the grammar.
        out.text = value;
        return R(out);
    }
    if (key == "score_first_page" || key == "score_last_page" ||
        key == "tab_a_score_position" || key == "tab_b_score_position") {
        if (!parse_authored_frame(value, out.i64))
            return R(std::unexpected(std::string("must be a non-negative integer")));
        return R(out);
    }
    return std::nullopt;
}

GuiScoreViewerFile read_score_viewer_file(
        const std::filesystem::path& project_folder) {
    const std::filesystem::path path = score_viewer_file_path(project_folder);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return score_viewer_file_defaults(project_folder);

    GuiScoreViewerFile out;
    const auto parsed = [&]() -> std::expected<void, std::string> {
        std::ifstream f(path, std::ios::binary);
        if (!f) return std::unexpected(std::string("could not open it"));
        auto scan = warptempo_settings::scan_key_value_file(
            f, [&out](int ln, const std::string& key, const std::string& value)
                      -> std::expected<void, std::string> {
            auto v = validate_score_viewer_value(key, value);
            if (!v) {
                return warptempo_parse::prefix_line_error(
                    ln, "unknown key '" + key + "'");
            }
            if (!*v)
                return warptempo_settings::bad_value(ln, key, value,
                                                     v->error());
            if (key == "score_path")            out.score_path     = (*v)->text;
            else if (key == "score_first_page") out.first_page     = (*v)->i64;
            else if (key == "score_last_page")  out.last_page      = (*v)->i64;
            else if (key == "tab_a_score_position")
                out.tab_a_position = (*v)->i64;
            else
                out.tab_b_position = (*v)->i64;
            return {};
        }, kScoreViewerFileKeys);
        if (!scan) return scan;
        if (score_range_crossed(out.first_page, out.last_page)) {
            return std::unexpected(
                "score_first_page " + std::to_string(out.first_page) +
                " is past score_last_page " + std::to_string(out.last_page));
        }
        return {};
    }();
    if (!parsed) {
        std::fprintf(stderr,
            "warptempo_gui: Ignored the score viewer's file '%s': %s; "
            "its defaults stand\n",
            path.string().c_str(), parsed.error().c_str());
        return score_viewer_file_defaults(project_folder);
    }
    return out;
}

std::optional<std::string> recall_score_viewer_value(const AppState&    app,
                                                     const std::string& key) {
    if (key == "score_path")           return app.score_path;
    if (key == "score_first_page")     return format_authored_frame(app.score_first_page);
    if (key == "score_last_page")      return format_authored_frame(app.score_last_page);
    if (key == "tab_a_score_position") return format_authored_frame(app.tab_a.score_position);
    if (key == "tab_b_score_position") return format_authored_frame(app.tab_b.score_position);
    return std::nullopt;
}

void write_score_viewer_file(const AppState& app) {
    const std::filesystem::path path =
        score_viewer_file_path(score_viewer_project_folder(app));
    std::string text;
    for (const char* key : kScoreViewerFileKeys) {
        text += key;
        text += '=';
        text += *recall_score_viewer_value(app, key);
        text += '\n';
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        std::fprintf(stderr,
            "warptempo_gui: Score viewer's file write failed: could not "
            "create '%s': %s\n",
            path.parent_path().string().c_str(), ec.message().c_str());
        return;
    }
    if (!atomic_write_string_to_path(path.string(), text)) {
        std::fprintf(stderr,
            "warptempo_gui: Score viewer's file write failed: could not "
            "write '%s'\n",
            path.string().c_str());
    }
}
