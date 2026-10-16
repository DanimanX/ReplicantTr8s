#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/TR8SEngine.h"

class ReplicantTR8SAudioProcessor : public juce::AudioProcessor {
public:
    ReplicantTR8SAudioProcessor();
    ~ReplicantTR8SAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    ReplicantTR8S::TR8SEngine& getEngine() { return engine; }
    const ReplicantTR8S::TR8SEngine& getEngine() const { return engine; }

    void syncParametersFromEngineKit();
    void pushParametersToEngine();

    // LCD Display status notification for last-edited control
    void notifyParameterPopup(const juce::String& title, const juce::String& valueText, float normVal);
    juce::String getPopupTitle() const { return popupTitle; }
    juce::String getPopupValueText() const { return popupValueText; }
    float getPopupNormValue() const { return popupNormValue; }
    juce::uint32 getPopupTimestampMs() const { return popupTimestampMs; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static juce::String getChannelParamId(int ch, const char* suffix);

private:
    juce::AudioProcessorValueTreeState apvts;
    ReplicantTR8S::TR8SEngine engine;
    bool syncingFromKit = false;

    juce::String popupTitle;
    juce::String popupValueText;
    float popupNormValue = 0.5f;
    juce::uint32 popupTimestampMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReplicantTR8SAudioProcessor)
};
