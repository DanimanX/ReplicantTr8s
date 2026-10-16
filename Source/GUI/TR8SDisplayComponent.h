#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

namespace ReplicantTR8S {

class TR8SDisplayComponent : public juce::Component {
public:
    explicit TR8SDisplayComponent(ReplicantTR8SAudioProcessor& proc);
    ~TR8SDisplayComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void rebuildCatalogMenus();
    void refreshSelectorsFromEngine();
    void openSampleFileChooserForChannel(int ch);

    std::function<void()> onStateChanged;

private:
    ReplicantTR8SAudioProcessor& processor;

    juce::ComboBox kitCombo;
    juce::ComboBox patternCombo;
    juce::ComboBox toneCombo;

    // Quick Kit Bank Buttons (808, 909, 707, 727, 606, CR-78, FM)
    struct QuickKitBtn {
        juce::TextButton button;
        int kitIndex0Based;
    };
    std::array<QuickKitBtn, 7> quickKitButtons;

    juce::TextButton loadSampleBtn { "LOAD SMP" };
    juce::TextButton reverseSampleBtn { "REV" };
    juce::TextButton clearSampleBtn { "CLR" };

    std::unique_ptr<juce::FileChooser> fileChooser;
};

} // namespace ReplicantTR8S
