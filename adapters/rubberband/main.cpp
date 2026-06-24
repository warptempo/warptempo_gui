#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>

#include <sndfile.hh>
#include <rubberband/RubberBandStretcher.h>

using RubberBand::RubberBandStretcher;

// De-interleave an interleaved buffer into one float array per channel.
static std::vector<std::vector<float>> interleavedToPlanar(
        const std::vector<float>& interleaved, int channels, size_t frames) {
    std::vector<std::vector<float>> planar(channels, std::vector<float>(frames));
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
            planar[c][i] = interleaved[i * channels + c];
    return planar;
}

// Re-interleave one-array-per-channel planar audio for writing.
static std::vector<float> planarToInterleaved(
        const std::vector<std::vector<float>>& planar, int channels, size_t frames) {
    std::vector<float> interleaved(frames * channels);
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
            interleaved[i * channels + c] = planar[c][i];
    return interleaved;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <input> <map.warpframemap> <output.wav>" << std::endl;
        return 1;
    }
    const std::string inputPath  = argv[1];
    const std::string mapPath    = argv[2];
    const std::string outputPath = argv[3];

    // 1. Load input audio (de-interleaved to planar).
    SndfileHandle inputFile(inputPath);
    if (inputFile.error()) {
        std::cerr << "Error opening input: " << inputFile.strError() << std::endl;
        return 1;
    }
    const size_t inputFrames = static_cast<size_t>(inputFile.frames());
    const int    channels    = inputFile.channels();
    const int    sampleRate  = inputFile.samplerate();

    std::vector<float> inputBuffer(inputFrames * channels);
    inputFile.read(inputBuffer.data(),
                   static_cast<sf_count_t>(inputFrames * channels));
    const auto inputPlanar = interleavedToPlanar(inputBuffer, channels, inputFrames);

    // 2. Load the frame map. Breakpoints are parsed as double (the parser
    // emits precise doubles); they are rounded to whole frames only when the
    // integer key-frame map is built below, since setKeyFrameMap takes
    // integer sample frames.
    std::ifstream mapFile(mapPath);
    struct Pt { double source; double target; };
    std::vector<Pt> mapPoints;
    std::string line;
    while (std::getline(mapFile, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        double s, t;
        if (ss >> s >> t) mapPoints.push_back({s, t});
    }
    if (mapPoints.size() < 2) {
        std::cerr << "Error: frame map too short." << std::endl;
        return 1;
    }

    // Rubber Band is the one adapter that must NOT receive the leading 0,0
    // anchor: setKeyFrameMap skews / errors on a 0->0 key, and offline mode
    // anchors the start itself via padding. Strip it if present.
    if (mapPoints.front().source == 0.0 && mapPoints.front().target == 0.0)
        mapPoints.erase(mapPoints.begin());
    if (mapPoints.empty()) {
        std::cerr << "Error: frame map has no non-zero breakpoints." << std::endl;
        return 1;
    }

    // 3. Overall stretch ratio from the final breakpoint (target / source).
    // The key-frame map below fixes only interior anchors; the overall output
    // duration comes from this ratio.
    const double srcEnd = mapPoints.back().source;
    const double tgtEnd = mapPoints.back().target;
    if (srcEnd <= 0.0) {
        std::cerr << "Error: degenerate frame map (source end <= 0)." << std::endl;
        return 1;
    }
    const double timeRatio = tgtEnd / srcEnd;

    // 4. Build the integer key-frame map (source frame -> target frame).
    std::map<size_t, size_t> keyFrameMap;
    for (const auto& p : mapPoints) {
        if (p.source < 0.0 || p.target < 0.0) continue;
        keyFrameMap[static_cast<size_t>(std::llround(p.source))] =
            static_cast<size_t>(std::llround(p.target));
    }

    // 5. Configure the R3 (Finer) engine in offline mode.
    RubberBandStretcher stretcher(
        static_cast<size_t>(sampleRate),
        static_cast<size_t>(channels),
        RubberBandStretcher::OptionProcessOffline |
        RubberBandStretcher::OptionEngineFiner,
        timeRatio,
        1.0);   // pitch scale 1.0: pure time stretch, no pitch change

    stretcher.setTimeRatio(timeRatio);
    stretcher.setPitchScale(1.0);
    // setKeyFrameMap must follow the ratio set and precede the first study /
    // process call.
    stretcher.setKeyFrameMap(keyFrameMap);

    // Clipping: the rubberband CLI's --ignore-clipping disables a CLI-only
    // corrective gain pass. The library performs no gain automation and this
    // adapter adds none - the stretched output is written as produced. That
    // is the intended --ignore-clipping behaviour (no gain automation).

    const size_t BLOCK = 16384;
    stretcher.setMaxProcessSize(BLOCK);

    auto chPtrs = [&](size_t pos) {
        std::vector<const float*> ptrs(channels);
        for (int c = 0; c < channels; ++c) ptrs[c] = &inputPlanar[c][pos];
        return ptrs;
    };

    // 6. Offline study pass: feed the whole input, final on the last block.
    for (size_t pos = 0; pos < inputFrames; ) {
        const size_t block = std::min(BLOCK, inputFrames - pos);
        const bool   last  = (pos + block >= inputFrames);
        auto ptrs = chPtrs(pos);
        stretcher.study(ptrs.data(), block, last);
        pos += block;
    }

    // 7. Offline process pass + drain. retrieve() pulls produced output frames
    // as they become available, keeping internal buffering bounded.
    std::vector<std::vector<float>> outputPlanar(channels);
    std::vector<std::vector<float>> retrieveBuf(channels, std::vector<float>(BLOCK));
    std::vector<float*> retrievePtrs(channels);
    for (int c = 0; c < channels; ++c) retrievePtrs[c] = retrieveBuf[c].data();

    auto drain = [&]() {
        int avail;
        while ((avail = stretcher.available()) > 0) {
            const size_t got = stretcher.retrieve(
                retrievePtrs.data(),
                std::min(static_cast<size_t>(avail), BLOCK));
            for (int c = 0; c < channels; ++c)
                outputPlanar[c].insert(outputPlanar[c].end(),
                                       retrieveBuf[c].begin(),
                                       retrieveBuf[c].begin() +
                                           static_cast<long>(got));
        }
    };

    for (size_t pos = 0; pos < inputFrames; ) {
        const size_t block = std::min(BLOCK, inputFrames - pos);
        const bool   last  = (pos + block >= inputFrames);
        auto ptrs = chPtrs(pos);
        stretcher.process(ptrs.data(), block, last);
        pos += block;
        drain();
    }
    drain();   // pull any tail produced after the final process block

    if (outputPlanar[0].empty()) {
        std::cerr << "Error: no output produced." << std::endl;
        return 1;
    }

    // 8. Write 32-bit float WAV.
    SndfileHandle outputFile(outputPath, SFM_WRITE,
                             SF_FORMAT_WAV | SF_FORMAT_FLOAT,
                             channels, sampleRate);
    const size_t outFrames = outputPlanar[0].size();
    const auto interleaved = planarToInterleaved(outputPlanar, channels, outFrames);
    outputFile.write(interleaved.data(),
                     static_cast<sf_count_t>(interleaved.size()));
    return 0;
}
