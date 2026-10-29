#include "trim_presets_file.h"

#include "device_config.h"    // shown_project_path
#include "settings_io.h"      // atomic_write_string_to_path
#include "text_editor.h"      // the one incoming filter, the name's alphabet

#include "frame_format.h"     // parse_authored_frame / format_authored_frame

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <expected>
#include <fstream>
#include <iterator>
#include <system_error>

std::filesystem::path trim_presets_file_path(
        const std::filesystem::path& project_folder) {
    return project_folder / "trim" / "presets.txt";
}

std::optional<std::string> trim_preset_name_refusal(
        std::string_view name, std::span<const GuiTrimPreset> presets) {
    if (name.empty()) return std::string("the name is empty");
    if (name.front() == ' ' || name.back() == ' ')
        return std::string("the name begins or ends with a space");
    // THE NAME'S ALPHABET IS THE ONE INCOMING FILTER'S (text_editor.h): the
    // name passes whole through a fresh name-editor buffer or it is a name
    // the editor could never hold. The editor's own commit always passes
    // here (its buffer came through the same filter); the reader's line may
    // not.
    text_editor::State probe;
    probe.kind = text_editor::Kind::TrimPresetName;
    const std::string whole(name);
    if (!text_editor::replace_selection(probe, whole) || probe.pending != whole)
        return std::string("the name holds a character a name cannot");
    for (const GuiTrimPreset& p : presets)
        if (p.name == name)
            return "a preset named '" + whole + "' exists";
    return std::nullopt;
}

std::vector<GuiTrimPreset> read_trim_presets_file(
        const std::filesystem::path& project_folder, int64_t total_frames) {
    const std::filesystem::path path = trim_presets_file_path(project_folder);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return {};

    std::vector<GuiTrimPreset> out;
    const auto parsed = [&]() -> std::expected<void, std::string> {
        std::ifstream f(path, std::ios::binary);
        if (!f) return std::unexpected(std::string("could not open it"));
        const std::string text((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
        if (f.bad()) return std::unexpected(std::string("could not read it"));
        if (!text.empty() && text.back() != '\n')
            return std::unexpected(std::string("its last line has no LF"));
        const int64_t wall = total_frames - 1;
        std::size_t at = 0;
        int ln = 0;
        while (at < text.size()) {
            ++ln;
            const std::size_t eol = text.find('\n', at);
            const std::string_view line(text.data() + at, eol - at);
            at = eol + 1;
            const auto fault = [ln](const std::string& why) {
                return std::unexpected("line " + std::to_string(ln) + ": " +
                                       why);
            };
            const std::size_t s1 = line.find(' ');
            const std::size_t s2 =
                s1 == std::string_view::npos ? s1 : line.find(' ', s1 + 1);
            if (s2 == std::string_view::npos)
                return fault("not '<begin> <end> <name>'");
            GuiTrimPreset p;
            if (!parse_authored_frame(line.substr(0, s1), p.begin) ||
                !parse_authored_frame(line.substr(s1 + 1, s2 - s1 - 1), p.end))
                return fault("a bound is not a frame");
            if (p.end > wall)
                return fault("a bound is past the source's last frame");
            if (p.begin >= p.end)
                return fault("the begin is not before the end");
            const std::string_view name = line.substr(s2 + 1);
            if (auto why = trim_preset_name_refusal(name, out))
                return fault(*why);
            p.name = std::string(name);
            out.push_back(std::move(p));
        }
        return {};
    }();
    if (!parsed) {
        std::fprintf(stderr,
            "warptempo_gui: Ignored the trim presets file '%s': %s; "
            "the list reads empty\n",
            path.string().c_str(), parsed.error().c_str());
        return {};
    }
    std::ranges::sort(out, trim_preset_before);
    return out;
}

std::optional<GuiFailure> write_trim_presets_file(
        const std::filesystem::path& project_folder,
        std::span<const GuiTrimPreset> presets) {
    static constexpr std::string_view kTag =
        "Trim presets write failed: could not write ";
    assert(std::ranges::is_sorted(presets, trim_preset_before));
    const std::filesystem::path path = trim_presets_file_path(project_folder);
    std::string text;
    for (const GuiTrimPreset& p : presets) {
        text += format_authored_frame(p.begin);
        text += ' ';
        text += format_authored_frame(p.end);
        text += ' ';
        text += p.name;
        text += '\n';
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return path_failure(kTag, path, shown_project_path(path),
                            ": " + ec.message());
    }
    if (!atomic_write_string_to_path(path.string(), text))
        return path_failure(kTag, path, shown_project_path(path), "");
    return std::nullopt;
}
