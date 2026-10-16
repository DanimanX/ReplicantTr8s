#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "GUI/TR8SLookAndFeel.h"
#include "GUI/TR8SDisplayComponent.h"

class ReplicantTR8SAudioProcessorEditor : public juce::AudioProcessorEditor,
                                          public juce::FileDragAndDropTarget,
                                          private juce::Timer {
public:
    explicit ReplicantTR8SAudioProcessorEditor(ReplicantTR8SAudioProcessor&);
    ~ReplicantTR8SAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // FileDragAndDropTarget for dropping WAV/AIFF/FLAC/MP3 onto any channel or LCD
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void handleStepPadClick(int stepIdx, const juce::ModifierKeys& mods);

    ReplicantTR8SAudioProcessor& audioProcessor;
    ReplicantTR8S::TR8SLookAndFeel lookAndFeel;
    ReplicantTR8S::TR8SDisplayComponent lcdDisplay;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct ChannelStripControls {
        juce::Slider tuneKnob;
        juce::Slider decayKnob;
        juce::Slider ctrlKnob;
        juce::Slider levelFader;

        std::unique_ptr<SliderAttachment> tuneAtt;
        std::unique_ptr<SliderAttachment> decayAtt;
        std::unique_ptr<SliderAttachment> ctrlAtt;
        std::unique_ptr<SliderAttachment> levelAtt;

        juce::TextButton muteBtn { "M" };
        juce::TextButton soloBtn { "S" };
        juce::TextButton sampleBtn { "SMP" };
        juce::TextButton instPadBtn;
        juce::Rectangle<int> stripBounds;
    };

    std::array<ChannelStripControls, TR8SFirmware::NUM_CHANNELS> channelStrips;

    // Master & Performance Knobs
    juce::Slider tempoKnob;
    juce::Slider shuffleKnob;
    juce::Slider accentKnob;
    juce::Slider scatterDepthKnob;
    juce::Slider reverbKnob;
    juce::Slider delayKnob;
    juce::Slider masterFilterKnob;
    juce::Slider masterDriveKnob;
    juce::Slider masterVolKnob;

    std::unique_ptr<SliderAttachment> tempoAtt;
    std::unique_ptr<SliderAttachment> shuffleAtt;
    std::unique_ptr<SliderAttachment> accentAtt;
    std::unique_ptr<SliderAttachment> scatterDepthAtt;
    std::unique_ptr<SliderAttachment> reverbAtt;
    std::unique_ptr<SliderAttachment> delayAtt;
    std::unique_ptr<SliderAttachment> masterFilterAtt;
    std::unique_ptr<SliderAttachment> masterDriveAtt;
    std::unique_ptr<SliderAttachment> masterVolAtt;

    // Scatter controls
    juce::TextButton scatterOnBtn { "SCATTER ON" };
    std::array<juce::TextButton, 8> scatterTypeBtns;

    // Transport & Variations
    juce::TextButton startStopBtn { "START / STOP" };
    std::array<juce::TextButton, 8> variationBtns;
    juce::TextButton fillInBtn { "FILL IN" };
    juce::TextButton editAccentBtn { "ACCENT STEP" };
    juce::TextButton clearVarBtn { "CLEAR" };
    bool editingAccentTrack = false;

    // 16 Step Sequencer Rectangles (drawn procedurally for crisp RGB pad lighting)
    std::array<juce::Rectangle<int>, 16> stepPadRects;
    juce::Rectangle<float> ledTempoRect;

    // Runtime Firmware Loader UI (prompts user before starting if firmware is not yet loaded)
    void openFirmwareFileChooser();
    bool tryLoadFirmwareFromFile(const juce::File& file);

    juce::TextButton headerFirmwareBtn { "FW: LOAD..." };
    juce::TextButton donateCoffeeBtn { "DONATE A COFFEE" };
    juce::String firmwareErrorMsg;
    std::unique_ptr<juce::FileChooser> firmwareChooser;

    class FirmwareStartupOverlay : public juce::Component {
    public:
        explicit FirmwareStartupOverlay(ReplicantTR8SAudioProcessorEditor& ownerEditor);
        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        ReplicantTR8SAudioProcessorEditor& editor;
        juce::TextButton selectFirmwareBtn { "SELECT FIRMWARE FILE (tr8s_sys_v300.zip / TR8S_UP.bin)" };
    };

    FirmwareStartupOverlay firmwareOverlay { *this };

    void mouseDown(const juce::MouseEvent& e) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReplicantTR8SAudioProcessorEditor)
};
