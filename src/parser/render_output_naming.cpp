#include "render_output_naming.h"

#include <system_error>

// The project folder, then the deliverable's folder beside the source
// (architect approval 2026-08-28).
std::filesystem::path render_output_directory(
    const std::string& source_audio_path) {
    std::filesystem::path dir =
        std::filesystem::path(source_audio_path).parent_path();
    if (dir.empty()) dir = std::filesystem::path(".");
    return dir / kDeliverableFolderName;
}

std::string render_output_stem(const EngineSettings& es) {
    // The wav deliverable is title-named.
    return es.title;
}

std::filesystem::path compose_render_output_path(
    const std::filesystem::path& dir,
    const std::string& stem) {
    return dir / (stem + ".wav");
}

std::string render_staging_path(const std::string& final_path) {
    return final_path + ".tmp";
}

std::optional<std::filesystem::path> render_output_source_collision(
    const EngineSettings& es,
    const std::string& source_audio_path) {
    if (source_audio_path.empty()) return std::nullopt;
    const std::filesystem::path src(source_audio_path);
    auto collides = [&](const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::equivalent(path, src, ec)
            || path.lexically_normal() == src.lexically_normal();
    };
    const std::filesystem::path out = compose_render_output_path(
        render_output_directory(source_audio_path),
        render_output_stem(es));
    if (collides(out)) return out;
    // The staging name is opened truncating before the render completes, so a
    // staging collision destroys the source too; return the staging path
    // itself so diagnostics name the true collider.
    const std::filesystem::path staging(render_staging_path(out.string()));
    if (collides(staging)) return staging;
    return std::nullopt;
}
