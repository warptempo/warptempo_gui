#include "render_output_naming.h"

#include <system_error>

std::vector<std::string> render_output_extensions(
    const std::string& output_format) {
    if (output_format == "warptempo_maps")
        return {".warpframemap", ".phaseresetframemap"};
    if (output_format == "generic_map")
        return {".warpframemap"};
    if (output_format == "midi_map")
        return {".miditempomap"};
    return {".wav"};
}

std::filesystem::path render_output_directory(
    const std::string& source_audio_path) {
    std::filesystem::path dir =
        std::filesystem::path(source_audio_path).parent_path();
    if (dir.empty()) dir = std::filesystem::path(".");
    return dir;
}

std::string render_output_stem(const EngineSettings& es,
                               const std::string& source_stem) {
    // Map artifacts are project files named by the source stem, like the
    // authoring sidecars beside the source. Only the wav deliverable is
    // title-named.
    if (es.output_format != "wav") return source_stem;
    return es.title;
}

std::vector<std::filesystem::path> compose_render_output_paths(
    const std::filesystem::path& dir,
    const std::string& stem,
    const std::string& output_format) {
    std::vector<std::filesystem::path> paths;
    for (const std::string& ext : render_output_extensions(output_format))
        paths.push_back(dir / (stem + ext));
    return paths;
}

std::string render_staging_path(const std::string& final_path) {
    return final_path + ".tmp";
}

std::optional<std::filesystem::path> render_output_source_collision(
    const EngineSettings& es,
    const std::string& source_audio_path) {
    if (source_audio_path.empty()) return std::nullopt;
    const std::filesystem::path src(source_audio_path);
    const std::string source_stem =
        std::filesystem::path(source_audio_path).stem().string();
    auto collides = [&](const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::equivalent(path, src, ec)
            || path.lexically_normal() == src.lexically_normal();
    };
    for (const std::filesystem::path& out : compose_render_output_paths(
             render_output_directory(source_audio_path),
             render_output_stem(es, source_stem),
             es.output_format)) {
        if (collides(out)) return out;
        // The staging name is opened truncating before the render completes,
        // so a staging collision destroys the source too; return the staging
        // path itself so diagnostics name the true collider.
        const std::filesystem::path staging(
            render_staging_path(out.string()));
        if (collides(staging)) return staging;
    }
    return std::nullopt;
}
