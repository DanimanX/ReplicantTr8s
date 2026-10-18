#include "TR8SDisplayComponent.h"
#include "TR8SLookAndFeel.h"

namespace ReplicantTR8S {

TR8SDisplayComponent::TR8SDisplayComponent(ReplicantTR8SAudioProcessor& proc)
    : processor(proc)
{
    auto& engine = processor.getEngine();

    kitCombo.onChange = [this]() {
        int id = kitCombo.getSelectedId();
        if (id >= 1 && id <= TR8SFirmware::NUM_KITS) {
            processor.getEngine().loadFactoryKit(id - 1);
            refreshSelectorsFromEngine();
            if (onStateChanged) onStateChanged();
        }
    };
    addAndMakeVisible(kitCombo);

    patternCombo.onChange = [this]() {
        int id = patternCombo.getSelectedId();
        if (id >= 1 && id <= TR8SFirmware::NUM_PATTERNS) {
            processor.getEngine().loadFactoryPattern(id - 1, false);
            refreshSelectorsFromEngine();
            if (onStateChanged) onStateChanged();
        }
    };
    addAndMakeVisible(patternCombo);

    toneCombo.onChange = [this]() {
        int id = toneCombo.getSelectedId();
        if (id >= 1 && id <= TR8SFirmware::NUM_TONES) {
            int ch = processor.getEngine().getSelectedChannel();
            auto& v = processor.getEngine().getVoice(ch);
            v.clearCustomSample();
            v.setToneId(static_cast<uint16_t>(id - 1));
            v.updateSynthWaveformPreview();
            processor.getEngine().triggerChannel(ch, 0.9f, false);
            if (onStateChanged) onStateChanged();
        }
    };
    addAndMakeVisible(toneCombo);

    // Quick Kit Bank Buttons: 808 (Kit 1), 909 (Kit 2), 707 (Kit 3), 727 (Kit 4), 606 (Kit 5), CR-78 (Kit 85), FM (Kit 87)
    static const char* kQuickLabels[7] = { "808", "909", "707", "727", "606", "CR-78", "FM" };
    static const int   kQuickKits[7]   = { 0,     1,     2,     3,     4,     84,      86   };

    for (int i = 0; i < 7; ++i) {
        quickKitButtons[static_cast<size_t>(i)].button.setButtonText(kQuickLabels[i]);
        quickKitButtons[static_cast<size_t>(i)].kitIndex0Based = kQuickKits[i];
        int targetKit = kQuickKits[i];
        quickKitButtons[static_cast<size_t>(i)].button.onClick = [this, targetKit]() {
            processor.getEngine().loadFactoryKit(targetKit);
            refreshSelectorsFromEngine();
            if (onStateChanged) onStateChanged();
        };
        quickKitButtons[static_cast<size_t>(i)].button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff14222e));
        quickKitButtons[static_cast<size_t>(i)].button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff7ff7ff));
        addAndMakeVisible(quickKitButtons[static_cast<size_t>(i)].button);
    }

    // Sample Loader Controls
    loadSampleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff123c4a));
    loadSampleBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff42ffd4));
    loadSampleBtn.onClick = [this]() {
        openSampleFileChooserForChannel(processor.getEngine().getSelectedChannel());
    };
    addAndMakeVisible(loadSampleBtn);

    reverseSampleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a2634));
    reverseSampleBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff9ce6ff));
    reverseSampleBtn.onClick = [this]() {
        int ch = processor.getEngine().getSelectedChannel();
        auto& v = processor.getEngine().getVoice(ch);
        v.setSampleReverse(!v.isSampleReversed());
        processor.getEngine().triggerChannel(ch, 0.9f, false);
        repaint();
    };
    addAndMakeVisible(reverseSampleBtn);

    clearSampleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2e1820));
    clearSampleBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffff8a9a));
    clearSampleBtn.onClick = [this]() {
        int ch = processor.getEngine().getSelectedChannel();
        processor.getEngine().getVoice(ch).clearCustomSample();
        refreshSelectorsFromEngine();
        repaint();
    };
    addAndMakeVisible(clearSampleBtn);

    rebuildCatalogMenus();
    juce::ignoreUnused(engine);
}

void TR8SDisplayComponent::rebuildCatalogMenus() {
    kitCombo.clear(juce::dontSendNotification);
    for (int i = 0; i < TR8SFirmware::NUM_KITS; ++i) {
        const auto& k = TR8SFirmware::FACTORY_KITS[i];
        kitCombo.addItem(juce::String::formatted("KIT %03d: %s", k.number, k.name), i + 1);
    }

    patternCombo.clear(juce::dontSendNotification);
    for (int i = 0; i < TR8SFirmware::NUM_PATTERNS; ++i) {
        const auto& p = TR8SFirmware::FACTORY_PATTERNS[i];
        patternCombo.addItem(juce::String::formatted("PTN %s: %s", p.code, p.name), i + 1);
    }

    toneCombo.clear(juce::dontSendNotification);
    for (int i = 0; i < TR8SFirmware::NUM_TONES; ++i) {
        const auto& t = TR8SFirmware::FACTORY_TONES[i];
        const char* engTag = (t.engineType == 1) ? "ACB" : (t.engineType == 3 ? "FM" : "PCM");
        toneCombo.addItem(juce::String::formatted("[%03d %s] %s", t.id, engTag, t.name), i + 1);
    }

    refreshSelectorsFromEngine();
}

void TR8SDisplayComponent::refreshSelectorsFromEngine() {
    const auto& engine = processor.getEngine();
    kitCombo.setSelectedId(engine.getCurrentKitIndex() + 1, juce::dontSendNotification);
    patternCombo.setSelectedId(engine.getCurrentPatternIndex() + 1, juce::dontSendNotification);
    int ch = engine.getSelectedChannel();
    toneCombo.setSelectedId(static_cast<int>(engine.getVoice(ch).getToneId()) + 1, juce::dontSendNotification);
    repaint();
}

void TR8SDisplayComponent::openSampleFileChooserForChannel(int ch) {
    processor.getEngine().setSelectedChannel(ch);
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Audio Sample for Channel " + juce::String(TR8SFirmware::CHANNEL_NAMES[ch]),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg");

    auto browserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(browserFlags, [this, ch](const juce::FileChooser& fc) {
        auto result = fc.getResult();
        if (result.existsAsFile()) {
            if (processor.getEngine().loadSampleForChannel(ch, result)) {
                processor.getEngine().triggerChannel(ch, 0.95f, false);
                processor.notifyParameterPopup(
                    juce::String(TR8SFirmware::CHANNEL_NAMES[ch]) + " SAMPLE LOADED",
                    result.getFileName(), 1.0f);
                refreshSelectorsFromEngine();
                if (onStateChanged) onStateChanged();
            }
        }
    });
}

void TR8SDisplayComponent::resized() {
    auto b = getLocalBounds().reduced(8, 6);
    int bottomCtrlH = 46;
    auto ctrlArea = b.removeFromBottom(bottomCtrlH);

    // Row 1 of bottom controls: KIT combo + Quick Kit buttons (808, 909, 707, 727, 606, CR-78, FM)
    auto row1 = ctrlArea.removeFromTop(21);
    kitCombo.setBounds(row1.removeFromLeft(175));
    row1.removeFromLeft(4);
    int qBtnW = (row1.getWidth()) / 7;
    for (int i = 0; i < 7; ++i) {
        quickKitButtons[static_cast<size_t>(i)].button.setBounds(row1.removeFromLeft(qBtnW).reduced(1, 0));
    }

    ctrlArea.removeFromTop(4);
    // Row 2 of bottom controls: PTN combo + TONE combo + Sample buttons
    auto row2 = ctrlArea;
    patternCombo.setBounds(row2.removeFromLeft(160));
    row2.removeFromLeft(4);
    clearSampleBtn.setBounds(row2.removeFromRight(36));
    row2.removeFromRight(2);
    reverseSampleBtn.setBounds(row2.removeFromRight(36));
    row2.removeFromRight(2);
    loadSampleBtn.setBounds(row2.removeFromRight(68));
    row2.removeFromRight(4);
    toneCombo.setBounds(row2);
}

void TR8SDisplayComponent::paint(juce::Graphics& g) {
    auto fullBounds = getLocalBounds().toFloat();

    // 1. Recessed glossy black bezel around LCD
    g.setColour(juce::Colour(0xff06090c));
    g.fillRoundedRectangle(fullBounds, 6.0f);
    g.setColour(juce::Colour(0xff2c3844));
    g.drawRoundedRectangle(fullBounds.reduced(0.5f), 6.0f, 1.5f);

    // Inner backlit LCD screen area (above the combo/button controls)
    auto screenRect = fullBounds.reduced(8.0f, 6.0f);
    screenRect.removeFromBottom(50.0f);

    juce::ColourGradient lcdGrad(juce::Colour(0xff071821), screenRect.getX(), screenRect.getY(),
                                 juce::Colour(0xff040f16), screenRect.getX(), screenRect.getBottom(), false);
    g.setGradientFill(lcdGrad);
    g.fillRoundedRectangle(screenRect, 4.0f);

    // Subtle LCD scanline grid
    g.setColour(juce::Colour(0xff0d2a38).withAlpha(0.35f));
    for (float sy = screenRect.getY() + 2.0f; sy < screenRect.getBottom(); sy += 3.0f) {
        g.drawHorizontalLine(static_cast<int>(sy), screenRect.getX() + 2.0f, screenRect.getRight() - 2.0f);
    }

    g.setColour(juce::Colour(0xff1be0ff).withAlpha(0.45f));
    g.drawRoundedRectangle(screenRect, 4.0f, 1.2f);

    const auto& engine = processor.getEngine();
    int selCh = engine.getSelectedChannel();
    const auto& voice = engine.getVoice(selCh);
    const auto& tone = TR8SFirmware::FACTORY_TONES[voice.getToneId()];
    juce::Colour chCol = TR8SLookAndFeel::getChannelColour(selCh);

    // 2. Top Status Ribbon inside LCD
    auto topBar = screenRect.reduced(6.0f, 4.0f).removeFromTop(18.0f);
    g.setColour(juce::Colour(0xff103647));
    g.fillRoundedRectangle(topBar, 2.5f);

    g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
    g.setColour(juce::Colour(0xff84f5ff));
    juce::String statusLeft = juce::String::formatted("PTN %s %-15s | KIT %03d %-14s",
        engine.getCurrentPatternCode().toRawUTF8(),
        engine.getCurrentPatternName().toRawUTF8(),
        engine.getCurrentKitIndex() + 1,
        engine.getCurrentKitName().toRawUTF8());
    g.drawText(statusLeft, topBar.reduced(6.0f, 0.0f), juce::Justification::centredLeft, false);

    juce::String statusRight = juce::String::formatted("VAR [%c]  STEP %02d/16  FW 3.00",
        'A' + engine.getCurrentVariation(),
        engine.getCurrentStep() + 1);
    g.setColour(juce::Colour(0xff38ff9c));
    g.drawText(statusRight, topBar.reduced(6.0f, 0.0f), juce::Justification::centredRight, false);

    // 3. Split Main LCD into Left Info/Params Pane & Right Waveform/Scope Pane
    auto bodyArea = screenRect.reduced(8.0f, 4.0f);
    bodyArea.removeFromTop(21.0f);
    auto leftPane = bodyArea.removeFromLeft(bodyArea.getWidth() * 0.48f);
    bodyArea.removeFromLeft(6.0f);
    auto rightPane = bodyArea;

    // Left Pane: Selected Channel & Tone / Sample Name
    g.setColour(chCol);
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText(juce::String::formatted("CH %02d [%s] - %s",
               selCh + 1,
               TR8SFirmware::CHANNEL_NAMES[selCh],
               TR8SFirmware::CHANNEL_FULL_NAMES[selCh]),
               leftPane.removeFromTop(16.0f), juce::Justification::centredLeft, false);

    // Engine / Sample Tag + Tone Name
    juce::String srcBadge;
    juce::String instName;
    if (voice.hasCustomSample()) {
        srcBadge = voice.isSampleReversed() ? "USER SMP [REV]" : "USER SAMPLE";
        instName = voice.getCustomSampleName();
    } else {
        srcBadge = (tone.engineType == 1) ? "ACB CIRCUIT" : (tone.engineType == 3 ? "FM 2-OP" : "PCM SYNTH");
        instName = juce::String::formatted("%03d: %s", tone.id, tone.name);
    }

    auto toneRow = leftPane.removeFromTop(18.0f);
    g.setColour(voice.hasCustomSample() ? juce::Colour(0xffffb82b) : juce::Colour(0xff42ffd4));
    g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
    g.drawText(srcBadge + "  >>  " + instName, toneRow, juce::Justification::centredLeft, false);

    leftPane.removeFromTop(3.0f);

    // Parameter Bar Graphs (TUNE, DECAY, REV, DLY, CTRL, LEVEL 0..150)
    struct ParamBar { const char* name; float val; juce::String text; };
    float semi = (voice.getTune() - 0.5f) * 24.0f;
    float revVal = engine.getChannelReverbSend(selCh);
    float dlyVal = engine.getChannelDelaySend(selCh);
    int lvl150 = juce::jlimit(0, ACBDrumVoice::MAX_MIXER_LEVEL,
                              static_cast<int>(std::round(voice.getLevel() * static_cast<float>(ACBDrumVoice::MAX_MIXER_LEVEL))));
    std::array<ParamBar, 6> bars = {{
        { "TUNE",  voice.getTune(),  juce::String::formatted("%+.1f", semi) },
        { "DECAY", voice.getDecay(), juce::String::formatted("%d", static_cast<int>(std::round(voice.getDecay() * 127.0f))) },
        { "REV",   revVal,           juce::String::formatted("%d%%", static_cast<int>(std::round(revVal * 100.0f))) },
        { "DLY",   dlyVal,           juce::String::formatted("%d%%", static_cast<int>(std::round(dlyVal * 100.0f))) },
        { "CTRL",  voice.getCtrl(),  juce::String::formatted("%d", static_cast<int>(std::round(voice.getCtrl() * 127.0f))) },
        { "LEVEL", voice.getLevel(), juce::String::formatted("%d", lvl150) }
    }};

    float barW = leftPane.getWidth() / static_cast<float>(bars.size());
    for (size_t i = 0; i < bars.size(); ++i) {
        auto slot = leftPane.removeFromLeft(barW).reduced(1.5f, 0.0f);
        g.setColour((i == 2 || i == 3) ? juce::Colour(0xff68ffb8) : juce::Colour(0xff7ec8e3));
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText(bars[i].name, slot.removeFromTop(11.0f), juce::Justification::centred, false);

        auto valLabel = slot.removeFromBottom(11.0f);
        g.setColour(juce::Colour(0xffd8faff));
        g.drawText(bars[i].text, valLabel, juce::Justification::centred, false);

        auto meterRect = slot.reduced(5.0f, 1.0f);
        g.setColour(juce::Colour(0xff0b2330));
        g.fillRoundedRectangle(meterRect, 2.0f);
        float fh = meterRect.getHeight() * juce::jlimit(0.0f, 1.0f, bars[i].val);
        g.setColour(chCol.withAlpha(0.85f));
        g.fillRoundedRectangle(meterRect.getX(), meterRect.getBottom() - fh, meterRect.getWidth(), fh, 2.0f);
    }

    // 4. Right Pane: Real-time Waveform & Envelope Graph + Drag & Drop Sample Zone
    g.setColour(juce::Colour(0xff06131c));
    g.fillRoundedRectangle(rightPane, 3.0f);
    g.setColour(juce::Colour(0xff194961));
    g.drawRoundedRectangle(rightPane, 3.0f, 1.0f);

    // Center zero-line
    float midY = rightPane.getCentreY();
    g.setColour(juce::Colour(0xff143b4f));
    g.drawHorizontalLine(static_cast<int>(midY), rightPane.getX() + 2.0f, rightPane.getRight() - 2.0f);

    // Draw selected channel's 128-point waveform
    const auto& wf = voice.getWaveformPreview();
    if (!wf.empty()) {
        juce::Path wavePath;
        float w = rightPane.getWidth() - 8.0f;
        float h = rightPane.getHeight() * 0.42f;
        float startX = rightPane.getX() + 4.0f;
        for (size_t i = 0; i < wf.size(); ++i) {
            float px = startX + (static_cast<float>(i) / static_cast<float>(wf.size() - 1)) * w;
            float envAtten = std::exp(-static_cast<float>(i) / (25.0f + voice.getDecay() * 110.0f));
            float py = midY - wf[i] * envAtten * h;
            if (i == 0) wavePath.startNewSubPath(px, midY);
            wavePath.lineTo(px, py);
        }
        wavePath.lineTo(startX + w, midY);

        g.setColour(chCol.withAlpha(0.22f));
        g.fillPath(wavePath);
        g.setColour(chCol);
        g.strokePath(wavePath, juce::PathStrokeType(1.4f));
    }

    // Overlay live master output oscilloscope in cyan
    std::array<float, 128> scope {};
    engine.getScopeSnapshot(scope);
    juce::Path scopePath;
    float sw = rightPane.getWidth() - 8.0f;
    float sh = rightPane.getHeight() * 0.35f;
    float sx0 = rightPane.getX() + 4.0f;
    for (size_t i = 0; i < scope.size(); ++i) {
        float px = sx0 + (static_cast<float>(i) / 127.0f) * sw;
        float py = midY - juce::jlimit(-1.0f, 1.0f, scope[i]) * sh;
        if (i == 0) scopePath.startNewSubPath(px, py);
        else scopePath.lineTo(px, py);
    }
    g.setColour(juce::Colour(0xff42ffd4).withAlpha(0.65f));
    g.strokePath(scopePath, juce::PathStrokeType(1.1f));

    // Top-right label inside waveform box
    g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
    g.setColour(juce::Colour(0xff84f5ff).withAlpha(0.8f));
    g.drawText("WAVEFORM / DRAG & DROP WAV HERE", rightPane.reduced(5.0f, 3.0f), juce::Justification::topRight, false);

    // 5. Recent Parameter Edit Popup Overlay (shown for 1.8 seconds when a knob/fader is moved)
    juce::uint32 nowMs = juce::Time::getMillisecondCounter();
    if (processor.getPopupTitle().isNotEmpty() && (nowMs - processor.getPopupTimestampMs()) < 1800u) {
        auto popRect = screenRect.withSizeKeepingCentre(230.0f, 44.0f);
        g.setColour(juce::Colour(0xf2071b26));
        g.fillRoundedRectangle(popRect, 4.0f);
        g.setColour(juce::Colour(0xff38ff9c));
        g.drawRoundedRectangle(popRect, 4.0f, 1.5f);

        g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
        g.setColour(juce::Colours::white);
        g.drawText(processor.getPopupTitle() + ": " + processor.getPopupValueText(),
                   popRect.reduced(8.0f, 5.0f).removeFromTop(18.0f),
                   juce::Justification::centred, false);

        auto barArea = popRect.reduced(14.0f, 7.0f).removeFromBottom(10.0f);
        g.setColour(juce::Colour(0xff103647));
        g.fillRoundedRectangle(barArea, 3.0f);
        g.setColour(juce::Colour(0xff38ff9c));
        g.fillRoundedRectangle(barArea.getX(), barArea.getY(),
                               barArea.getWidth() * processor.getPopupNormValue(), barArea.getHeight(), 3.0f);
    }
}

} // namespace ReplicantTR8S
