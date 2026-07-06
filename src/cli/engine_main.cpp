#include "engine/engine.h"   // EngineParams, run_warptempo_engine, EngineResult
#include "engine/engine_geometry.h"   // kN, kRs
#include "warp_frame_map.h"           // WarpFrameMapSegment, read_warp_frame_map
#include "phase_reset_frame_map.h"    // read_phase_reset_frame_map
#include "locale_check.h"

#include "audio_reader.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

void unlink_silent(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s <source-audio> [--no-limiter]\n"
        "  Runs the PGHI engine on the prebuilt map pair warptempo_parser\n"
        "  writes beside the source for the same project in warptempo_maps\n"
        "  mode: the sibling <source-stem>.warpframemap and\n"
        "  <source-stem>.phaseresetframemap (both required; the\n"
        "  phaseresetframemap is the engine query-domain list warptempo_parser\n"
        "  computes against the same warpframemap, consumed as-is, with an\n"
        "  empty file as the valid no-resets form). Writes the warped wav\n"
        "  beside the source as <source-stem>-rendered.wav — with\n"
        "  --no-limiter, as limiter=false;<source-stem>-rendered.wav, the\n"
        "  GUI's clean-float naming. Limiter is on unless --no-limiter is\n"
        "  given. N is fixed at 4096. Trim is not an engine concern: the\n"
        "  supplied map is rendered wholesale.\n",
        argv0);
}

}  // namespace

int main(int argc, char** argv) {
    if (!verify_c_numeric_locale("warptempo_engine")) return 1;

    std::string source_path;
    bool no_limiter = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-limiter")                    no_limiter = true;
        else if (!a.empty() && a[0] != '-' && source_path.empty()) source_path = a;
        else { usage(argv[0]); return 2; }
    }
    if (source_path.empty()) { usage(argv[0]); return 2; }

    const std::string stem =
        std::filesystem::path(source_path).stem().string();
    std::filesystem::path dir =
        std::filesystem::path(source_path).parent_path();
    if (dir.empty()) dir = std::filesystem::path(".");

    // These literals restate the driver's command-line contract (also stated
    // in the usage text) and mirror the naming rules owned parser-side in
    // render_output_naming.h — the default render title (source stem plus
    // "-rendered"), the clean-float "limiter=false;" prefix, and the pair
    // extensions. The driver deliberately restates them inline instead of
    // linking the parser archive, keeping its link line engine and audio_io
    // alone. The output is <source-stem>-rendered.wav beside the source,
    // prefixed "limiter=false;" under --no-limiter (the clean-float wav
    // render). The map inputs are the <source-stem> siblings warptempo_parser
    // writes for the same project in warptempo_maps mode — both the
    // .warpframemap and the .phaseresetframemap are required, with an empty
    // phaseresetframemap as the valid no-resets file; a naming drift between
    // these inline literals and the parser-side composer hardfails on a
    // missing input on either column rather than corrupting output. A
    // presence check cannot detect a stale pair, though: a later generic_map
    // render rewrites the warp column alone, leaving a reset sibling that was
    // derived against an older warpframemap. The pair contract — the reset
    // list is derived against the exact warpframemap shipped beside it — is
    // therefore the operator's regeneration discipline, not something this
    // driver guards; the engine validates only strict ascent on the list.
    const std::string out_path =
        (dir / ((no_limiter ? "limiter=false;" : "") + stem + "-rendered.wav"))
            .string();

    const std::string warpframemap_path =
        (dir / (stem + ".warpframemap")).string();
    const std::string phaseresetframemap_path =
        (dir / (stem + ".phaseresetframemap")).string();

    // Hard refusal: never write over the source audio itself. equivalent() is
    // an inode match and only succeeds when both paths exist.
    {
        std::error_code ec;
        if (std::filesystem::exists(out_path, ec) &&
            std::filesystem::equivalent(out_path, source_path, ec)) {
            std::fprintf(stderr,
                "warptempo_engine: output '%s' resolves to the source audio "
                "file; refusing to overwrite the source\n",
                out_path.c_str());
            return 1;
        }
    }

    // --- warpframemap (required) ---
    std::optional<std::vector<WarpFrameMapSegment>> fm =
        read_warp_frame_map(warpframemap_path);
    if (!fm) {
        std::fprintf(stderr,
            "warptempo_engine: could not read warpframemap '%s' "
            "(missing, unreadable, or malformed)\n", warpframemap_path.c_str());
        return 1;
    }
    if (fm->empty()) {
        std::fprintf(stderr,
            "warptempo_engine: warpframemap '%s' is empty; nothing to render\n",
            warpframemap_path.c_str());
        return 1;
    }

    // --- phaseresetframemap (required). Engine query-domain doubles,
    // anticipation and drops already applied by warptempo_parser against the
    // same warpframemap read above; consumed as-is with no conversion. An
    // empty file is the valid no-resets form; absence is a refusal. ---
    std::optional<std::vector<double>> rm =
        read_phase_reset_frame_map(phaseresetframemap_path);
    if (!rm) {
        std::fprintf(stderr,
            "warptempo_engine: could not read phaseresetframemap '%s' "
            "(missing, unreadable, or malformed)\n",
            phaseresetframemap_path.c_str());
        return 1;
    }
    std::vector<double> reset_frames = std::move(*rm);

    // --- source audio, full file, interleaved float. ---
    std::vector<float> src_samples;
    int src_sr = 0, src_ch = 0;
    {
        AudioFileInfo info;
        auto full = audio_read_full(source_path, &info);
        if (!full) {
            std::fprintf(stderr,
                "warptempo_engine: could not read source '%s': %s\n",
                source_path.c_str(), full.error().c_str());
            return 1;
        }
        src_samples = std::move(*full);
        src_sr = info.sample_rate;
        src_ch = info.channels;
    }

    // --- locked engine geometry ---
    const int     N_fft = kN;
    const int     R_s   = kRs;

    // --- engine params ---
    EngineParams ep;
    ep.source_audio_samples = src_samples.data();
    ep.source_audio_frames  =
        src_samples.size() / static_cast<size_t>(src_ch);
    ep.source_sample_rate   = src_sr;
    ep.source_channels      = src_ch;
    const std::string staging_output_path = out_path + ".tmp";
    ep.output_audio_path    = staging_output_path;

    // The supplied map is rendered wholesale; the engine derives its output
    // length from the map's last anchor.
    ep.warp_frame_map = *fm;

    ep.N            = N_fft;
    ep.limiter      = !no_limiter;   // on by default; --no-limiter clears it
    // limiter_ceiling_dbfs / peak_* stay at EngineParams defaults, matching
    // render_pipeline / cli_main.

    ep.phase_reset_frame_map = std::move(reset_frames);

    // --- render: engine writes a sibling staging file and success publishes it
    // atomically via rename. ---
    std::fprintf(stderr,
        "warptempo_engine: rendering N=%d R_s=%d -> %s\n",
        N_fft, R_s, out_path.c_str());
    const EngineResult er = run_warptempo_engine(ep);
    if (er != EngineResult::Success) {
        unlink_silent(staging_output_path);
        std::fprintf(stderr, "warptempo_engine: engine %s\n",
                     er == EngineResult::Cancelled ? "cancelled" : "failed");
        return 1;
    }

    {
        std::error_code ec;
        std::filesystem::rename(staging_output_path, out_path, ec);
        if (ec) {
            unlink_silent(staging_output_path);
            std::fprintf(stderr,
                "warptempo_engine: could not publish '%s' to '%s': %s\n",
                staging_output_path.c_str(), out_path.c_str(),
                ec.message().c_str());
            return 1;
        }
    }

    std::fprintf(stderr, "warptempo_engine: wrote %s\n", out_path.c_str());
    return 0;
}
