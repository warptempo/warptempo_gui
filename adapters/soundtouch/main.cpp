#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>

#include <sndfile.h>
#include <soundtouch/SoundTouch.h>

using namespace soundtouch;
using namespace std;

const int BUFFER_SIZE = 2048;

struct FrameMapPoint {
    double src_frame;
    double tgt_frame;
};

// Loader for .warpframemap breakpoints (read as double; parser emits precise doubles)
vector<FrameMapPoint> loadFrameMap(const string& filename) {
    vector<FrameMapPoint> map;
    ifstream infile(filename);
    if (!infile.is_open()) {
        cerr << "Error: Could not open frame map: " << filename << endl;
        exit(1);
    }

    // Note: Implicit push of {0,0} removed because parser now guarantees it in the file.

    double src, tgt;
    // Read breakpoints as double
    while (infile >> src >> tgt) {
        map.push_back({src, tgt});
    }
    
    // Safety check: Ensure map has at least 2 points to process a segment
    if (map.size() < 2) {
        cerr << "Warning: Frame map too short, adding default end point." << endl;
        if (map.empty() || (map.back().src_frame == 0 && map.back().tgt_frame == 0)) {
             // Fallback if file was empty or only had 0 0
             map.push_back({0, 0});
             // We can't guess the length here without the file info, 
             // but strictly this prevents index out of bounds in main loop.
        }
    }
    
    return map;
}

int main(int argc, char* argv[]) {
    // Usage: adapter <input.wav> <map.warpframemap> <output.wav>
    if (argc < 4) {
        cerr << "Usage: " << argv[0] << " <input.wav> <map.warpframemap> <output.wav>" << endl;
        return 1;
    }

    string inputPath = argv[1];
    string mapPath = argv[2];
    string outputPath = argv[3];

    SF_INFO sfInfoInput;
    memset(&sfInfoInput, 0, sizeof(sfInfoInput)); 
    SNDFILE* infile = sf_open(inputPath.c_str(), SFM_READ, &sfInfoInput);
    if (!infile) {
        cerr << "Error opening input file: " << inputPath << endl;
        return 1;
    }

    SF_INFO sfInfoOutput = sfInfoInput;
    // Force 32-bit float output to match input precision
    sfInfoOutput.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    
    SNDFILE* outfile = sf_open(outputPath.c_str(), SFM_WRITE, &sfInfoOutput);
    if (!outfile) return 1;

    SoundTouch soundTouch;
    soundTouch.setSampleRate(sfInfoInput.samplerate);
    soundTouch.setChannels(sfInfoInput.channels);
    
    // Standard Quality Settings
    soundTouch.setSetting(SETTING_USE_AA_FILTER, 1);
    soundTouch.setSetting(SETTING_USE_QUICKSEEK, 0);
    
    // Defaults (confirmed in TDStretch.h) are used automatically:
    // Sequence: 0 (Auto)
    // Seek: 0 (Auto)
    // Overlap: 8 ms

    vector<FrameMapPoint> frameMap = loadFrameMap(mapPath);
    vector<float> inputBuffer(BUFFER_SIZE * sfInfoInput.channels);
    vector<float> outputBuffer(BUFFER_SIZE * sfInfoInput.channels);

    long current_src_pos = 0;
    
    for (size_t i = 1; i < frameMap.size(); ++i) {
        const double src_delta = frameMap[i].src_frame - frameMap[i-1].src_frame;
        double tgt_delta = frameMap[i].tgt_frame - frameMap[i-1].tgt_frame;
        if (tgt_delta <= 0.0) tgt_delta = 1.0;

        soundTouch.setTempo(src_delta / tgt_delta);

        // Whole-frame pull boundary: the segment's source endpoint rounded to
        // an integer sample. The tempo ratio above stays exact-double; only
        // this read boundary rounds, since a frame read is whole-frame.
        const long target_src = std::llround(frameMap[i].src_frame);
        long frames_remaining = target_src - current_src_pos;
        while (frames_remaining > 0) {
            int read_size = (frames_remaining > BUFFER_SIZE) ? BUFFER_SIZE : frames_remaining;
            sf_count_t count = sf_readf_float(infile, inputBuffer.data(), read_size);
            if (count == 0) break;

            soundTouch.putSamples(inputBuffer.data(), count);
            uint nSamples;
            do {
                nSamples = soundTouch.receiveSamples(outputBuffer.data(), BUFFER_SIZE);
                if (nSamples > 0) {
                    sf_writef_float(outfile, outputBuffer.data(), nSamples);
                }
            } while (nSamples != 0);

            frames_remaining -= count;
            current_src_pos += count;
        }
    }

    // Flush remaining samples
    soundTouch.flush();
    uint nSamples;
    do {
        nSamples = soundTouch.receiveSamples(outputBuffer.data(), BUFFER_SIZE);
        if (nSamples > 0) sf_writef_float(outfile, outputBuffer.data(), nSamples);
    } while (nSamples != 0);

    sf_close(infile);
    sf_close(outfile);
    return 0;
}
