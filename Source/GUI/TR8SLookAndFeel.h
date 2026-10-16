#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ReplicantTR8S {

class TR8SLookAndFeel : public juce::LookAndFeel_V4 {
public:
    TR8SLookAndFeel();
    ~TR8SLookAndFeel() override = default;

    static juce::Colour getChannelColour(int ch);

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    static void drawSevenSegmentNumber(juce::Graphics& g, juce::Rectangle<float> bounds,
                                       double value, juce::Colour ledColour);
};

} // namespace ReplicantTR8S
