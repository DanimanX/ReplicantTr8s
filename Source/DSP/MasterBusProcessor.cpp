#include "MasterBusProcessor.h"
#include <algorithm>

namespace ReplicantTR8S {

MasterBusProcessor::MasterBusProcessor() = default;

void MasterBusProcessor::prepare(double newSampleRate, int /*maxBlockSize*/) {
    sampleRate = std::max(8000.0, newSampleRate);
    int maxDelayLen = static_cast<int>(sampleRate * 2.5) + 1024;
    delayBufL.assign(static_cast<size_t>(maxDelayLen), 0.0f);
    delayBufR.assign(static_cast<size_t>(maxDelayLen), 0.0f);

    int maxScatterLen = static_cast<int>(sampleRate * 3.0) + 1024;
    scatterBufL.assign(static_cast<size_t>(maxScatterLen), 0.0f);
    scatterBufR.assign(static_cast<size_t>(maxScatterLen), 0.0f);

    reverb.setSampleRate(sampleRate);
    juce::Reverb::Parameters revParams;
    revParams.roomSize = 0.62f;
    revParams.damping = 0.42f;
    revParams.wetLevel = 1.0f;
    revParams.dryLevel = 0.0f;
    revParams.width = 0.92f;
    reverb.setParameters(revParams);

    setTempoBpm(currentBpm);
    reset();
}

void MasterBusProcessor::reset() {
    std::fill(delayBufL.begin(), delayBufL.end(), 0.0f);
    std::fill(delayBufR.begin(), delayBufR.end(), 0.0f);
    std::fill(scatterBufL.begin(), scatterBufL.end(), 0.0f);
    std::fill(scatterBufR.begin(), scatterBufR.end(), 0.0f);
    delayWritePos = 0;
    scatterWritePos = 0;
    scatterReadPhase = 0.0;
    svfLpL = svfBpL = svfLpR = svfBpR = 0.0f;
    reverb.reset();
}

void MasterBusProcessor::setTempoBpm(double bpm) {
    currentBpm = juce::jlimit(40.0, 300.0, bpm);
    // Dotted 8th note tempo-synced delay
    double dottedEighthSec = (60.0 / currentBpm) * 0.75;
    delaySamples = juce::jlimit(256, static_cast<int>(delayBufL.size()) - 4, static_cast<int>(dottedEighthSec * sampleRate));
}

void MasterBusProcessor::processFrame(float& mainL, float& mainR,
                                      float revSendL, float revSendR,
                                      float dlySendL, float dlySendR,
                                      int currentStep) {
    // 1. Stereo Tempo-Synced Delay
    if (!delayBufL.empty()) {
        int bufSize = static_cast<int>(delayBufL.size());
        int readPosL = (delayWritePos - delaySamples + bufSize) % bufSize;
        int readPosR = (delayWritePos - (delaySamples * 2 / 3) + bufSize) % bufSize; // Ping-pong offset

        float dlyL = delayBufL[static_cast<size_t>(readPosL)];
        float dlyR = delayBufR[static_cast<size_t>(readPosR)];

        float feedback = 0.38f + delayLevel * 0.25f;
        delayBufL[static_cast<size_t>(delayWritePos)] = std::tanh(dlySendL + dlyR * feedback);
        delayBufR[static_cast<size_t>(delayWritePos)] = std::tanh(dlySendR + dlyL * feedback);
        delayWritePos = (delayWritePos + 1) % bufSize;

        mainL += dlyL * delayLevel * 0.85f;
        mainR += dlyR * delayLevel * 0.85f;
    }

    // 2. Stereo Reverb
    if (reverbLevel > 0.005f) {
        float rl = revSendL;
        float rr = revSendR;
        reverb.processStereo(&rl, &rr, 1);
        mainL += rl * reverbLevel * 0.9f;
        mainR += rr * reverbLevel * 0.9f;
    }

    // 3. AIRA SCATTER Performance Effect
    if (!scatterBufL.empty()) {
        int sSize = static_cast<int>(scatterBufL.size());
        scatterBufL[static_cast<size_t>(scatterWritePos)] = mainL;
        scatterBufR[static_cast<size_t>(scatterWritePos)] = mainR;

        if (scatterActive) {
            double stepSamples = std::max(512.0, (60.0 / currentBpm) * 0.25 * sampleRate);
            double loopWindow = stepSamples * (scatterDepth > 0.65f ? 0.5 : 1.0);
            scatterReadPhase += 1.0;
            if (scatterReadPhase >= loopWindow)
                scatterReadPhase = 0.0;

            if (scatterType == 1) {
                // Type 1: Reverse grain per step
                int rIdx = (scatterWritePos - static_cast<int>(scatterReadPhase) + sSize) % sSize;
                mainL = scatterBufL[static_cast<size_t>(rIdx)];
                mainR = scatterBufR[static_cast<size_t>(rIdx)];
            } else if (scatterType == 2) {
                // Type 2: Trance Gate / Chop
                bool gateOn = (std::fmod(scatterReadPhase, loopWindow * 0.5) < loopWindow * 0.25);
                float g = gateOn ? 1.0f : (1.0f - scatterDepth);
                mainL *= g;
                mainR *= g;
            } else if (scatterType == 3 || scatterType == 4) {
                // Type 3/4: Stutter / Repeat 32nd/64th
                int rIdx = (scatterWritePos - static_cast<int>( loopWindow - scatterReadPhase ) + sSize) % sSize;
                mainL = scatterBufL[static_cast<size_t>(rIdx)];
                mainR = scatterBufR[static_cast<size_t>(rIdx)];
            } else if (scatterType == 5) {
                // Type 5: Tape Stop / Half-Speed
                int rIdx = (scatterWritePos - static_cast<int>(scatterReadPhase * 0.5) + sSize) % sSize;
                mainL = scatterBufL[static_cast<size_t>(rIdx)];
                mainR = scatterBufR[static_cast<size_t>(rIdx)];
            } else {
                // Type 6..8: Bit-Stutter + Alternating Pan Chop
                float panMod = (currentStep & 1) ? 0.2f : 0.8f;
                float q = 4.0f + (1.0f - scatterDepth) * 12.0f;
                mainL = (std::round(mainL * q) / q) * (1.2f - panMod);
                mainR = (std::round(mainR * q) / q) * (0.2f + panMod);
            }
        } else {
            scatterReadPhase = 0.0;
        }
        scatterWritePos = (scatterWritePos + 1) % sSize;
    }

    // 4. Master DJ Filter (0.0..0.48 = LPF, 0.52..1.0 = HPF, 0.48..0.52 = Bypass)
    if (std::abs(masterFilter - 0.5f) > 0.02f) {
        bool isHpf = (masterFilter > 0.5f);
        float normFreq = isHpf
            ? ((masterFilter - 0.52f) / 0.48f)
            : (masterFilter / 0.48f);
        normFreq = juce::jlimit(0.0f, 1.0f, normFreq);
        float cutoffHz = isHpf
            ? (30.0f * std::pow(180.0f, normFreq))
            : (80.0f * std::pow(220.0f, normFreq));
        float f = 2.0f * std::sin(3.14159265f * std::min(0.44f, cutoffHz / static_cast<float>(sampleRate)));
        float q = 0.52f;

        float hpL = mainL - svfLpL - q * svfBpL;
        svfBpL += f * hpL;
        svfLpL += f * svfBpL;

        float hpR = mainR - svfLpR - q * svfBpR;
        svfBpR += f * hpR;
        svfLpR += f * svfBpR;

        mainL = isHpf ? hpL : svfLpL;
        mainR = isHpf ? hpR : svfLpR;
    }

    // 5. Master Analog Bus Drive & Limiter
    float driveGain = 1.0f + masterDrive * 2.2f;
    mainL = std::tanh(mainL * driveGain) * masterVolume;
    mainR = std::tanh(mainR * driveGain) * masterVolume;
}

} // namespace ReplicantTR8S
