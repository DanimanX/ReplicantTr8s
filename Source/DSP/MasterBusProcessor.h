#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

namespace ReplicantTR8S {

class MasterBusProcessor {
public:
    MasterBusProcessor();
    ~MasterBusProcessor() = default;

    void prepare(double newSampleRate, int maxBlockSize);
    void reset();

    void setReverbLevel(float norm) { reverbLevel = norm; }
    void setDelayLevel(float norm) { delayLevel = norm; }
    void setMasterFxFilter(float norm) { masterFilter = norm; } // 0..0.5 LPF, 0.5..1.0 HPF
    void setMasterDrive(float norm) { masterDrive = norm; }
    void setMasterVolume(float norm) { masterVolume = norm; }

    void setScatterActive(bool active) { scatterActive = active; }
    bool isScatterActive() const { return scatterActive; }
    void setScatterType(int type) { scatterType = juce::jlimit(1, 8, type); }
    int getScatterType() const { return scatterType; }
    void setScatterDepth(float depth) { scatterDepth = juce::jlimit(0.0f, 1.0f, depth); }
    float getScatterDepth() const { return scatterDepth; }

    void setTempoBpm(double bpm);

    void processFrame(float& mainL, float& mainR, float revSendL, float revSendR, float dlySendL, float dlySendR, int currentStep);

private:
    double sampleRate = 44100.0;
    double currentBpm = 128.0;

    float reverbLevel = 0.25f;
    float delayLevel = 0.20f;
    float masterFilter = 0.5f;
    float masterDrive = 0.15f;
    float masterVolume = 0.85f;

    bool scatterActive = false;
    int scatterType = 1; // 1..8
    float scatterDepth = 0.5f;

    // Stereo Delay line (up to 2 seconds)
    std::vector<float> delayBufL, delayBufR;
    int delayWritePos = 0;
    int delaySamples = 16537;

    // Freeverb-style Stereo Reverb via juce::Reverb
    juce::Reverb reverb;

    // Master SVF Filter (LPF / HPF)
    float svfLpL = 0.0f, svfBpL = 0.0f;
    float svfLpR = 0.0f, svfBpR = 0.0f;

    // Scatter circular capture buffer (1 bar max)
    std::vector<float> scatterBufL, scatterBufR;
    int scatterWritePos = 0;
    double scatterReadPhase = 0.0;
};

} // namespace ReplicantTR8S
