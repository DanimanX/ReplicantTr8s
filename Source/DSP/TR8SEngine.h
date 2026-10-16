#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <functional>
#include "ACBDrumVoice.h"
#include "MasterBusProcessor.h"
#include "../Firmware/TR8SFirmwareData.h"

namespace ReplicantTR8S {

struct StepCell {
    uint8_t velocity = 0; // 0 = off, 50 = ghost/weak, 80 = normal, 115 = accent
    uint8_t flags = 0;    // bit 0..3 = substep/flam
};

class TR8SEngine {
public:
    static constexpr int NUM_CHANNELS = TR8SFirmware::NUM_CHANNELS;     // 11
    static constexpr int NUM_STEPS = TR8SFirmware::NUM_STEPS;           // 16
    static constexpr int NUM_VARIATIONS = TR8SFirmware::NUM_VARIATIONS; // 8

    TR8SEngine();
    ~TR8SEngine() = default;

    void prepare(double newSampleRate, int maxBlockSize);
    void reset();

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages,
                      bool hostIsPlaying, double hostBpm, double hostPpqPosition);

    // Runtime Firmware Loading (tr8s_sys_v300.zip / TR8S_UP.bin)
    bool loadFirmwareFile(const juce::File& file, juce::String& errorMsg, bool savePreference = true);
    bool isFirmwareLoaded() const { return TR8SFirmware::RuntimeFirmwareLoader::isFirmwareLoaded(); }

    // Kit & Pattern Management (1-based index 1..128 or 0-based index 0..127)
    void loadFactoryKit(int kitIndex0Based);
    void loadFactoryPattern(int patternIndex0Based, bool alsoLoadKit = true);

    int getCurrentKitIndex() const { return currentKitIndex; }
    int getCurrentPatternIndex() const { return currentPatternIndex; }
    juce::String getCurrentKitName() const;
    juce::String getCurrentPatternName() const;
    juce::String getCurrentPatternCode() const;

    // Variations A..H (0..7)
    void setCurrentVariation(int varIdx);
    int getCurrentVariation() const { return currentVariation; }
    void copyVariation(int srcVar, int dstVar);
    void clearVariation(int varIdx);

    // Selected Channel (0..10: BD..RC)
    void setSelectedChannel(int ch) { selectedChannel = juce::jlimit(0, NUM_CHANNELS - 1, ch); }
    int getSelectedChannel() const { return selectedChannel; }

    // Sequencer Step Access
    StepCell getStep(int variation, int channelOrAccent, int step) const;
    void setStep(int variation, int channelOrAccent, int step, uint8_t velocity, uint8_t flags = 0);
    void toggleStep(int channelOrAccent, int step, bool withFlam = false, bool weakStep = false);

    // Transport
    void setPlaying(bool play);
    bool isPlaying() const { return internalPlaying.load(std::memory_order_relaxed) || hostTransportPlaying.load(std::memory_order_relaxed); }
    void setTempoBpm(double bpm);
    double getTempoBpm() const { return tempoBpm; }
    void setShuffle(float normShuffle) { shuffleParam = juce::jlimit(-0.5f, 0.5f, normShuffle); }
    float getShuffle() const { return shuffleParam; }
    void setAccentLevel(float normAccent) { accentLevel = juce::jlimit(0.0f, 1.0f, normAccent); }
    float getAccentLevel() const { return accentLevel; }

    int getCurrentStep() const { return playheadStep.load(std::memory_order_relaxed); }

    // Channel Voice & Parameters
    ACBDrumVoice& getVoice(int ch) { return voices[static_cast<size_t>(juce::jlimit(0, NUM_CHANNELS - 1, ch))]; }
    const ACBDrumVoice& getVoice(int ch) const { return voices[static_cast<size_t>(juce::jlimit(0, NUM_CHANNELS - 1, ch))]; }

    void triggerChannel(int ch, float velocity = 0.9f, bool isFlam = false);

    void setChannelMute(int ch, bool muted) { channelMuted[static_cast<size_t>(ch)] = muted; }
    bool isChannelMuted(int ch) const { return channelMuted[static_cast<size_t>(ch)]; }
    void setChannelSolo(int ch, bool soloed) { channelSoloed[static_cast<size_t>(ch)] = soloed; }
    bool isChannelSoloed(int ch) const { return channelSoloed[static_cast<size_t>(ch)]; }

    void setChannelReverbSend(int ch, float send) { channelRevSend[static_cast<size_t>(ch)] = send; }
    float getChannelReverbSend(int ch) const { return channelRevSend[static_cast<size_t>(ch)]; }
    void setChannelDelaySend(int ch, float send) { channelDlySend[static_cast<size_t>(ch)] = send; }
    float getChannelDelaySend(int ch) const { return channelDlySend[static_cast<size_t>(ch)]; }

    // Fill-In Trigger
    void triggerFillIn() { fillInActive = !fillInActive; }
    bool isFillInActive() const { return fillInActive; }

    // Custom User Sample Loading
    bool loadSampleForChannel(int ch, const juce::File& file);
    juce::AudioFormatManager& getFormatManager() { return formatManager; }

    // Master Bus
    MasterBusProcessor& getMasterBus() { return masterBus; }
    const MasterBusProcessor& getMasterBus() const { return masterBus; }

    // Oscilloscope buffer for LCD
    void getScopeSnapshot(std::array<float, 128>& dest) const;

    // Callback when kit loads so processor can sync APVTS sliders
    std::function<void()> onKitLoaded;

private:
    void advanceSequencerStep(int stepIdx);
    bool isChannelAudible(int ch) const;

    double sampleRate = 44100.0;
    double tempoBpm = 128.0;
    float shuffleParam = 0.0f;
    float accentLevel = 0.65f;

    std::atomic<bool> internalPlaying { false };
    std::atomic<bool> hostTransportPlaying { false };
    bool wasHostPlaying = false;
    std::atomic<int> playheadStep { 0 };
    double sampleCounterInStep = 0.0;

    int currentKitIndex = 0;     // 0 = Kit 001 (TR-808), 1 = Kit 002 (TR-909)
    int currentPatternIndex = 103; // Default to PTN 104 (Straight 909) or 0
    int currentVariation = 0;    // 0..7 (A..H)
    int selectedChannel = 0;     // 0..10 (BD..RC)
    bool fillInActive = false;

    std::array<ACBDrumVoice, NUM_CHANNELS> voices;
    std::array<bool, NUM_CHANNELS> channelMuted {};
    std::array<bool, NUM_CHANNELS> channelSoloed {};
    std::array<float, NUM_CHANNELS> channelRevSend {};
    std::array<float, NUM_CHANNELS> channelDlySend {};

    // Active Pattern Grid: 8 Variations x 12 Tracks (0..10 = BD..RC, 11 = ACCENT) x 16 Steps
    StepCell patternGrid[NUM_VARIATIONS][NUM_CHANNELS + 1][NUM_STEPS];

    MasterBusProcessor masterBus;
    juce::AudioFormatManager formatManager;

    std::array<float, 128> scopeRing {};
    std::atomic<int> scopeWriteIdx { 0 };
};

} // namespace ReplicantTR8S
