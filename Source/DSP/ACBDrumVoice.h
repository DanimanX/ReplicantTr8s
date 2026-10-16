#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
#include <array>
#include <atomic>
#include <cmath>
#include "../Firmware/TR8SFirmwareData.h"

namespace ReplicantTR8S {

class ACBDrumVoice {
public:
    ACBDrumVoice();
    ~ACBDrumVoice() = default;

    void prepare(double newSampleRate);
    void reset();

    // Trigger voice with velocity (0.0 .. 1.0), accent (0.0 .. 1.0), and flam flag
    void trigger(float velocity, float accentAmount, bool isFlam = false);
    void choke(); // Fast hi-hat choke when CH chokes OH

    // Render single stereo sample frame
    void renderNextSample(float& outL, float& outR);

    // Parameter setters
    void setChannelIndex(int idx) { channelIndex = idx; }
    void setToneId(uint16_t id);
    uint16_t getToneId() const { return currentToneId; }

    void setTune(float normTune) { tuneParam = normTune; }       // 0..1 (0.5 = center)
    void setDecay(float normDecay) { decayParam = normDecay; }   // 0..1 (0.5 = nominal)
    void setCtrl(float normCtrl) { ctrlParam = normCtrl; }       // 0..1 (0.5 = nominal)
    void setLevel(float normLevel) { levelParam = normLevel; }   // 0..1
    void setPan(float normPan) { panParam = normPan; }           // 0..1 (0.5 = center)

    float getTune() const { return tuneParam; }
    float getDecay() const { return decayParam; }
    float getCtrl() const { return ctrlParam; }
    float getLevel() const { return levelParam; }
    float getPan() const { return panParam; }

    // Custom User Sample support
    bool loadCustomSample(const juce::File& file, juce::AudioFormatManager& formatManager);
    void setCustomSampleBuffer(const juce::AudioBuffer<float>& buffer, double sourceSampleRate, const juce::String& name, const juce::String& path = {});
    void clearCustomSample();
    bool hasCustomSample() const { return useCustomSample && customSampleLength > 0; }
    void setUseCustomSample(bool shouldUse) { if (customSampleLength > 0) useCustomSample = shouldUse; }
    void setSampleReverse(bool rev) { sampleReverse = rev; }
    bool isSampleReversed() const { return sampleReverse; }
    juce::String getCustomSampleName() const { return customSampleName; }
    juce::String getCustomSamplePath() const { return customSamplePath; }
    const std::vector<float>& getWaveformPreview() const { return waveformPreview; }

    bool isActive() const { return active; }
    float getRecentPeak() const { return recentPeak.load(std::memory_order_relaxed); }
    void decayRecentPeak(float factor = 0.92f) {
        recentPeak.store(recentPeak.load(std::memory_order_relaxed) * factor, std::memory_order_relaxed);
    }

    void updateSynthWaveformPreview();

private:
    float synthesizeSample();
    float render808Model(int cat);
    float render909Model(int cat);
    float render707Or626Model(int cat, bool is707);
    float render727Model(int cat);
    float render606Model(int cat);
    float renderCR78Model(int cat);
    float renderFMModel(int cat);
    float renderPCMOrSynthModel(int cat);
    float renderUserSample();

    float nextWhiteNoise();

    int channelIndex = 0;
    uint16_t currentToneId = 0;
    double sampleRate = 44100.0;

    // Normalized parameters (0.0 .. 1.0)
    float tuneParam = 0.5f;
    float decayParam = 0.5f;
    float ctrlParam = 0.5f;
    float levelParam = 0.85f;
    float panParam = 0.5f;

    // Voice runtime state
    bool active = false;
    float currentVelocity = 0.0f;
    float envTime = 0.0f;
    float phase1 = 0.0f;
    float phase2 = 0.0f;
    float phase3 = 0.0f;
    std::array<float, 6> metalPhases { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    // Filter state variables (SVF)
    float svfLp1 = 0.0f, svfBp1 = 0.0f;
    float svfLp2 = 0.0f, svfBp2 = 0.0f;
    float chokeGain = 1.0f;
    bool choking = false;

    // Flam retrigger
    bool flamPending = false;
    int flamCountdownSamples = 0;
    float flamVelocity = 0.0f;

    // PRNG state for analog noise
    uint32_t rngState = 0x12345678u;

    // Custom User Sample storage
    bool useCustomSample = false;
    bool sampleReverse = false;
    std::vector<float> customSampleL;
    std::vector<float> customSampleR;
    int customSampleLength = 0;
    double customSampleRate = 44100.0;
    double samplePlayhead = 0.0;
    juce::String customSampleName;
    juce::String customSamplePath;

    // Waveform preview (128 points for LCD display)
    std::vector<float> waveformPreview;
    std::atomic<float> recentPeak { 0.0f };
};

} // namespace ReplicantTR8S
