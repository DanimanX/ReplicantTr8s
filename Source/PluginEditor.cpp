#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
void configureRotaryKnob(juce::Slider& s, juce::Colour accentCol) {
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s.setColour(juce::Slider::rotarySliderFillColourId, accentCol);
}
} // namespace

ReplicantTR8SAudioProcessorEditor::ReplicantTR8SAudioProcessorEditor(ReplicantTR8SAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      lcdDisplay(p)
{
    setLookAndFeel(&lookAndFeel);

    lcdDisplay.onStateChanged = [this]() {
        repaint();
    };
    addAndMakeVisible(lcdDisplay);

    auto& apvts = audioProcessor.getAPVTS();
    auto& engine = audioProcessor.getEngine();

    // 1. Setup 11 Channel Strips (BD..RC)
    for (int ch = 0; ch < TR8SFirmware::NUM_CHANNELS; ++ch) {
        auto& strip = channelStrips[static_cast<size_t>(ch)];
        juce::Colour chCol = ReplicantTR8S::TR8SLookAndFeel::getChannelColour(ch);
        juce::String chName = TR8SFirmware::CHANNEL_NAMES[ch];

        configureRotaryKnob(strip.tuneKnob, chCol);
        configureRotaryKnob(strip.decayKnob, chCol);
        configureRotaryKnob(strip.reverbKnob, juce::Colour(0xff1ae5ff).interpolatedWith(chCol, 0.35f));
        configureRotaryKnob(strip.delayKnob, juce::Colour(0xff38ff9c).interpolatedWith(chCol, 0.35f));
        configureRotaryKnob(strip.ctrlKnob, chCol);

        strip.levelFader.setSliderStyle(juce::Slider::LinearVertical);
        strip.levelFader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        strip.levelFader.setColour(juce::Slider::trackColourId, chCol);

        addAndMakeVisible(strip.tuneKnob);
        addAndMakeVisible(strip.decayKnob);
        addAndMakeVisible(strip.reverbKnob);
        addAndMakeVisible(strip.delayKnob);
        addAndMakeVisible(strip.ctrlKnob);
        addAndMakeVisible(strip.levelFader);

        strip.tuneAtt = std::make_unique<SliderAttachment>(apvts, ReplicantTR8SAudioProcessor::getChannelParamId(ch, "TUNE"), strip.tuneKnob);
        strip.decayAtt = std::make_unique<SliderAttachment>(apvts, ReplicantTR8SAudioProcessor::getChannelParamId(ch, "DECAY"), strip.decayKnob);
        strip.reverbAtt = std::make_unique<SliderAttachment>(apvts, ReplicantTR8SAudioProcessor::getChannelParamId(ch, "REVERB"), strip.reverbKnob);
        strip.delayAtt = std::make_unique<SliderAttachment>(apvts, ReplicantTR8SAudioProcessor::getChannelParamId(ch, "DELAY"), strip.delayKnob);
        strip.ctrlAtt = std::make_unique<SliderAttachment>(apvts, ReplicantTR8SAudioProcessor::getChannelParamId(ch, "CTRL"), strip.ctrlKnob);
        strip.levelAtt = std::make_unique<SliderAttachment>(apvts, ReplicantTR8SAudioProcessor::getChannelParamId(ch, "LEVEL"), strip.levelFader);

        strip.tuneKnob.onValueChange = [this, ch, chName]() {
            float v = static_cast<float>(channelStrips[static_cast<size_t>(ch)].tuneKnob.getValue());
            audioProcessor.getEngine().getVoice(ch).updateSynthWaveformPreview();
            audioProcessor.notifyParameterPopup(chName + " TUNE", juce::String::formatted("%+.1f st", (v - 0.5f) * 24.0f), v);
        };
        strip.decayKnob.onValueChange = [this, ch, chName]() {
            float v = static_cast<float>(channelStrips[static_cast<size_t>(ch)].decayKnob.getValue());
            audioProcessor.getEngine().getVoice(ch).updateSynthWaveformPreview();
            audioProcessor.notifyParameterPopup(chName + " DECAY", juce::String(static_cast<int>(v * 127.0f)), v);
        };
        strip.reverbKnob.onValueChange = [this, ch, chName]() {
            float v = static_cast<float>(channelStrips[static_cast<size_t>(ch)].reverbKnob.getValue());
            audioProcessor.getEngine().setSelectedChannel(ch);
            audioProcessor.notifyParameterPopup(chName + " REVERB", juce::String(static_cast<int>(std::round(v * 100.0f))) + "%", v);
        };
        strip.delayKnob.onValueChange = [this, ch, chName]() {
            float v = static_cast<float>(channelStrips[static_cast<size_t>(ch)].delayKnob.getValue());
            audioProcessor.getEngine().setSelectedChannel(ch);
            audioProcessor.notifyParameterPopup(chName + " DELAY", juce::String(static_cast<int>(std::round(v * 100.0f))) + "%", v);
        };
        strip.ctrlKnob.onValueChange = [this, ch, chName]() {
            float v = static_cast<float>(channelStrips[static_cast<size_t>(ch)].ctrlKnob.getValue());
            audioProcessor.getEngine().getVoice(ch).updateSynthWaveformPreview();
            audioProcessor.notifyParameterPopup(chName + " CTRL", juce::String(static_cast<int>(v * 127.0f)), v);
        };
        strip.levelFader.onValueChange = [this, ch, chName]() {
            float v = static_cast<float>(channelStrips[static_cast<size_t>(ch)].levelFader.getValue());
            int level150 = juce::jlimit(0, ReplicantTR8S::ACBDrumVoice::MAX_MIXER_LEVEL,
                                        static_cast<int>(std::round(v * static_cast<float>(ReplicantTR8S::ACBDrumVoice::MAX_MIXER_LEVEL))));
            audioProcessor.notifyParameterPopup(chName + " LEVEL", juce::String(level150) + " / 150", v);
        };

        // Mute Button
        strip.muteBtn.onClick = [this, ch]() {
            auto& eng = audioProcessor.getEngine();
            eng.setChannelMute(ch, !eng.isChannelMuted(ch));
            repaint();
        };
        addAndMakeVisible(strip.muteBtn);

        // Solo Button
        strip.soloBtn.onClick = [this, ch]() {
            auto& eng = audioProcessor.getEngine();
            eng.setChannelSolo(ch, !eng.isChannelSoloed(ch));
            repaint();
        };
        addAndMakeVisible(strip.soloBtn);

        // Sample Load Button per channel
        strip.sampleBtn.onClick = [this, ch]() {
            audioProcessor.getEngine().setSelectedChannel(ch);
            lcdDisplay.refreshSelectorsFromEngine();
            lcdDisplay.openSampleFileChooserForChannel(ch);
        };
        addAndMakeVisible(strip.sampleBtn);

        // Instrument Select + Audition Pad Button
        strip.instPadBtn.setButtonText(chName);
        strip.instPadBtn.onClick = [this, ch]() {
            auto& eng = audioProcessor.getEngine();
            eng.setSelectedChannel(ch);
            eng.triggerChannel(ch, 0.92f, false);
            lcdDisplay.refreshSelectorsFromEngine();
            repaint();
        };
        addAndMakeVisible(strip.instPadBtn);
    }

    // 2. Setup Master & Performance Knobs
    configureRotaryKnob(tempoKnob, juce::Colour(0xffff2b3a));
    configureRotaryKnob(shuffleKnob, juce::Colour(0xffff9a1a));
    configureRotaryKnob(accentKnob, juce::Colour(0xffffd21a));
    configureRotaryKnob(scatterDepthKnob, juce::Colour(0xff38ff9c));
    configureRotaryKnob(reverbKnob, juce::Colour(0xff1ae5ff));
    configureRotaryKnob(delayKnob, juce::Colour(0xff1a9cff));
    configureRotaryKnob(masterFilterKnob, juce::Colour(0xffc93bff));
    configureRotaryKnob(masterDriveKnob, juce::Colour(0xffff3ba8));
    configureRotaryKnob(masterVolKnob, juce::Colour(0xffffffff));

    addAndMakeVisible(tempoKnob);
    addAndMakeVisible(shuffleKnob);
    addAndMakeVisible(accentKnob);
    addAndMakeVisible(scatterDepthKnob);
    addAndMakeVisible(reverbKnob);
    addAndMakeVisible(delayKnob);
    addAndMakeVisible(masterFilterKnob);
    addAndMakeVisible(masterDriveKnob);
    addAndMakeVisible(masterVolKnob);

    tempoAtt = std::make_unique<SliderAttachment>(apvts, "TEMPO_BPM", tempoKnob);
    shuffleAtt = std::make_unique<SliderAttachment>(apvts, "SHUFFLE", shuffleKnob);
    accentAtt = std::make_unique<SliderAttachment>(apvts, "ACCENT_LEVEL", accentKnob);
    scatterDepthAtt = std::make_unique<SliderAttachment>(apvts, "SCATTER_DEPTH", scatterDepthKnob);
    reverbAtt = std::make_unique<SliderAttachment>(apvts, "REVERB_LEVEL", reverbKnob);
    delayAtt = std::make_unique<SliderAttachment>(apvts, "DELAY_LEVEL", delayKnob);
    masterFilterAtt = std::make_unique<SliderAttachment>(apvts, "MASTER_FILTER", masterFilterKnob);
    masterDriveAtt = std::make_unique<SliderAttachment>(apvts, "MASTER_DRIVE", masterDriveKnob);
    masterVolAtt = std::make_unique<SliderAttachment>(apvts, "MASTER_VOLUME", masterVolKnob);

    reverbKnob.onValueChange = [this]() {
        float v = static_cast<float>(reverbKnob.getValue());
        audioProcessor.notifyParameterPopup("MASTER REVERB", juce::String(static_cast<int>(v * 100.0f)) + "%", v);
    };
    delayKnob.onValueChange = [this]() {
        float v = static_cast<float>(delayKnob.getValue());
        audioProcessor.notifyParameterPopup("MASTER DELAY", juce::String(static_cast<int>(v * 100.0f)) + "%", v);
    };
    masterFilterKnob.onValueChange = [this]() {
        float v = static_cast<float>(masterFilterKnob.getValue());
        juce::String mode = std::abs(v - 0.5f) < 0.03f ? "BYPASS" : (v < 0.5f ? "LPF SWEEP" : "HPF SWEEP");
        audioProcessor.notifyParameterPopup("MASTER FILTER", mode, v);
    };
    masterDriveKnob.onValueChange = [this]() {
        float v = static_cast<float>(masterDriveKnob.getValue());
        audioProcessor.notifyParameterPopup("MASTER DRIVE", juce::String(static_cast<int>(v * 100.0f)) + "%", v);
    };

    // 3. Scatter Buttons
    scatterOnBtn.onClick = [this]() {
        auto& mb = audioProcessor.getEngine().getMasterBus();
        mb.setScatterActive(!mb.isScatterActive());
        repaint();
    };
    addAndMakeVisible(scatterOnBtn);

    static const char* kScatterNames[8] = { "REV", "GAT", "RP1", "RP2", "HLF", "CHP", "BIT", "PAN" };
    for (int i = 0; i < 8; ++i) {
        scatterTypeBtns[static_cast<size_t>(i)].setButtonText(kScatterNames[i]);
        int stype = i + 1;
        scatterTypeBtns[static_cast<size_t>(i)].onClick = [this, stype]() {
            audioProcessor.getEngine().getMasterBus().setScatterType(stype);
            audioProcessor.notifyParameterPopup("SCATTER TYPE", juce::String(stype) + ": " + kScatterNames[stype - 1], stype / 8.0f);
            repaint();
        };
        addAndMakeVisible(scatterTypeBtns[static_cast<size_t>(i)]);
    }

    // 4. Transport & Variation A..H Controls
    startStopBtn.onClick = [this]() {
        auto& eng = audioProcessor.getEngine();
        eng.setPlaying(!eng.isPlaying());
        repaint();
    };
    addAndMakeVisible(startStopBtn);

    for (int v = 0; v < 8; ++v) {
        variationBtns[static_cast<size_t>(v)].setButtonText(juce::String::charToString('A' + v));
        variationBtns[static_cast<size_t>(v)].onClick = [this, v]() {
            audioProcessor.getEngine().setCurrentVariation(v);
            repaint();
        };
        addAndMakeVisible(variationBtns[static_cast<size_t>(v)]);
    }

    fillInBtn.onClick = [this]() {
        audioProcessor.getEngine().triggerFillIn();
        repaint();
    };
    addAndMakeVisible(fillInBtn);

    editAccentBtn.onClick = [this]() {
        editingAccentTrack = !editingAccentTrack;
        repaint();
    };
    addAndMakeVisible(editAccentBtn);

    clearVarBtn.onClick = [this]() {
        auto& eng = audioProcessor.getEngine();
        eng.clearVariation(eng.getCurrentVariation());
        repaint();
    };
    addAndMakeVisible(clearVarBtn);

    headerFirmwareBtn.onClick = [this]() {
        openFirmwareFileChooser();
    };
    addAndMakeVisible(headerFirmwareBtn);

    donateCoffeeBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2c2010));
    donateCoffeeBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffffc83b));
    donateCoffeeBtn.onClick = []() {
        juce::URL("https://paypal.me/DanimanX?locale.x=en_US&country.x=CL").launchInDefaultBrowser();
    };
    addAndMakeVisible(donateCoffeeBtn);

    // Add Startup Firmware Modal Overlay on top of all controls when firmware is not yet loaded
    addChildComponent(firmwareOverlay);
    firmwareOverlay.setVisible(!engine.isFirmwareLoaded());

    setSize(1280, 760);
    startTimerHz(30);
}

ReplicantTR8SAudioProcessorEditor::~ReplicantTR8SAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

ReplicantTR8SAudioProcessorEditor::FirmwareStartupOverlay::FirmwareStartupOverlay(ReplicantTR8SAudioProcessorEditor& ownerEditor)
    : editor(ownerEditor)
{
    selectFirmwareBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff00e65b));
    selectFirmwareBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    selectFirmwareBtn.onClick = [this]() {
        editor.openFirmwareFileChooser();
    };
    addAndMakeVisible(selectFirmwareBtn);
}

void ReplicantTR8SAudioProcessorEditor::FirmwareStartupOverlay::resized() {
    auto b = getLocalBounds();
    selectFirmwareBtn.setBounds(b.getCentreX() - 240, b.getCentreY() + 72, 480, 46);
}

void ReplicantTR8SAudioProcessorEditor::FirmwareStartupOverlay::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xe8080b0f));

    auto box = getLocalBounds().withSizeKeepingCentre(740, 360).toFloat();
    juce::ColourGradient boxGrad(juce::Colour(0xff161d26), box.getX(), box.getY(),
                                 juce::Colour(0xff0e1319), box.getX(), box.getBottom(), false);
    g.setGradientFill(boxGrad);
    g.fillRoundedRectangle(box, 10.0f);

    g.setColour(juce::Colour(0xff00e65b));
    g.drawRoundedRectangle(box, 10.0f, 2.2f);

    int cx = static_cast<int>(box.getX()) + 32;
    int cw = static_cast<int>(box.getWidth()) - 64;
    int cy = static_cast<int>(box.getY()) + 28;

    g.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    g.setColour(juce::Colour(0xff00e65b));
    g.drawText("FIRMWARE FILE REQUIRED BEFORE STARTING", cx, cy, cw, 28, juce::Justification::centred, false);

    g.setFont(juce::Font(juce::FontOptions(13.5f, juce::Font::plain)));
    g.setColour(juce::Colour(0xffd0dce8));
    g.drawText("For copyright compliance, Replicant TR-8S does not bundle proprietary firmware ROM data.",
               cx, cy + 38, cw, 22, juce::Justification::centred, false);

    g.setColour(juce::Colour(0xff84f5ff));
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText("1. Obtain the official TR-8S System Program v3.00 archive (tr8s_sys_v300.zip or TR8S_UP.bin).",
               cx, cy + 78, cw, 22, juce::Justification::centred, false);
    g.drawText("2. Click the button below or Drag & Drop the file anywhere onto this window.",
               cx, cy + 104, cw, 22, juce::Justification::centred, false);
    g.drawText("3. All 128 Factory Kits, 128 Patterns, and 522 Tones will be extracted in memory.",
               cx, cy + 130, cw, 22, juce::Justification::centred, false);

    if (editor.firmwareErrorMsg.isNotEmpty()) {
        g.setColour(juce::Colour(0xffff425b));
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText("ERROR: " + editor.firmwareErrorMsg, cx, cy + 166, cw, 22, juce::Justification::centred, false);
    } else {
        g.setColour(juce::Colour(0xff8898a8));
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::plain)));
        g.drawText("Your firmware path will be remembered automatically for future sessions.",
                   cx, cy + 166, cw, 22, juce::Justification::centred, false);
    }
}

bool ReplicantTR8SAudioProcessorEditor::tryLoadFirmwareFromFile(const juce::File& file) {
    juce::String err;
    if (audioProcessor.getEngine().loadFirmwareFile(file, err)) {
        firmwareErrorMsg.clear();
        lcdDisplay.rebuildCatalogMenus();
        firmwareOverlay.setVisible(false);
        audioProcessor.notifyParameterPopup("FIRMWARE v3.00 LOADED", file.getFileName(), 1.0f);
        repaint();
        return true;
    }
    firmwareErrorMsg = err;
    firmwareOverlay.repaint();
    repaint();
    return false;
}

void ReplicantTR8SAudioProcessorEditor::openFirmwareFileChooser() {
    firmwareChooser = std::make_unique<juce::FileChooser>(
        "Select TR-8S Firmware v3.00 (tr8s_sys_v300.zip or TR8S_UP.bin)",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.zip;*.bin");

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    firmwareChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
        auto result = fc.getResult();
        if (result.existsAsFile()) {
            tryLoadFirmwareFromFile(result);
        }
    });
}

bool ReplicantTR8SAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files) {
        if (f.endsWithIgnoreCase(".zip") || f.endsWithIgnoreCase(".bin")
            || f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".aif")
            || f.endsWithIgnoreCase(".flac") || f.endsWithIgnoreCase(".mp3") || f.endsWithIgnoreCase(".ogg"))
            return true;
    }
    return false;
}

void ReplicantTR8SAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    if (files.isEmpty())
        return;

    juce::File f(files[0]);
    if (f.hasFileExtension("zip;bin") || !audioProcessor.getEngine().isFirmwareLoaded()) {
        if (f.hasFileExtension("zip;bin")) {
            tryLoadFirmwareFromFile(f);
            return;
        }
        if (!audioProcessor.getEngine().isFirmwareLoaded()) {
            firmwareErrorMsg = "Please drop tr8s_sys_v300.zip or TR8S_UP.bin first.";
            firmwareOverlay.repaint();
            return;
        }
    }

    int targetChannel = audioProcessor.getEngine().getSelectedChannel();
    for (int ch = 0; ch < TR8SFirmware::NUM_CHANNELS; ++ch) {
        if (channelStrips[static_cast<size_t>(ch)].stripBounds.contains(x, y)) {
            targetChannel = ch;
            break;
        }
    }

    if (audioProcessor.getEngine().loadSampleForChannel(targetChannel, f)) {
        audioProcessor.getEngine().setSelectedChannel(targetChannel);
        audioProcessor.getEngine().triggerChannel(targetChannel, 0.95f, false);
        audioProcessor.notifyParameterPopup(
            juce::String(TR8SFirmware::CHANNEL_NAMES[targetChannel]) + " SAMPLE DROPPED",
            f.getFileName(), 1.0f);
        lcdDisplay.refreshSelectorsFromEngine();
        repaint();
    }
}

void ReplicantTR8SAudioProcessorEditor::timerCallback() {
    auto& eng = audioProcessor.getEngine();
    bool fwLoaded = eng.isFirmwareLoaded();
    if (firmwareOverlay.isVisible() == fwLoaded) {
        firmwareOverlay.setVisible(!fwLoaded);
        if (fwLoaded)
            lcdDisplay.rebuildCatalogMenus();
    }
    headerFirmwareBtn.setButtonText(fwLoaded ? "FW v3.00: READY (CHANGE)" : "FW: LOAD FILE...");
    headerFirmwareBtn.setColour(juce::TextButton::buttonColourId, fwLoaded ? juce::Colour(0xff142c20) : juce::Colour(0xff521c24));
    headerFirmwareBtn.setColour(juce::TextButton::textColourOffId, fwLoaded ? juce::Colour(0xff38ff9c) : juce::Colour(0xffff8a9a));

    // Update dynamic button colors for MUTE, SOLO, SMP, INST, VARIATION, SCATTER, START/STOP
    for (int ch = 0; ch < TR8SFirmware::NUM_CHANNELS; ++ch) {
        auto& strip = channelStrips[static_cast<size_t>(ch)];
        auto& voice = eng.getVoice(ch);
        voice.decayRecentPeak(0.86f);

        bool muted = eng.isChannelMuted(ch);
        bool soloed = eng.isChannelSoloed(ch);
        bool hasSmp = voice.hasCustomSample();
        bool selected = (eng.getSelectedChannel() == ch);
        juce::Colour chCol = ReplicantTR8S::TR8SLookAndFeel::getChannelColour(ch);

        strip.muteBtn.setColour(juce::TextButton::buttonColourId, muted ? juce::Colour(0xff00e65b) : juce::Colour(0xff18221d));
        strip.muteBtn.setColour(juce::TextButton::textColourOffId, muted ? juce::Colours::black : juce::Colour(0xff68a880));

        strip.soloBtn.setColour(juce::TextButton::buttonColourId, soloed ? juce::Colour(0xff00d4ff) : juce::Colour(0xff162029));
        strip.soloBtn.setColour(juce::TextButton::textColourOffId, soloed ? juce::Colours::black : juce::Colour(0xff6aa8c4));

        strip.sampleBtn.setColour(juce::TextButton::buttonColourId, hasSmp ? juce::Colour(0xffffaa1a) : juce::Colour(0xff1c2128));
        strip.sampleBtn.setColour(juce::TextButton::textColourOffId, hasSmp ? juce::Colours::black : juce::Colour(0xff9aa4b0));

        strip.instPadBtn.setColour(juce::TextButton::buttonColourId, selected ? chCol : chCol.withAlpha(0.24f));
        strip.instPadBtn.setColour(juce::TextButton::textColourOffId, selected ? juce::Colours::black : juce::Colours::white);
    }

    for (int v = 0; v < 8; ++v) {
        bool activeVar = (eng.getCurrentVariation() == v);
        variationBtns[static_cast<size_t>(v)].setColour(
            juce::TextButton::buttonColourId,
            activeVar ? juce::Colour(0xff00e5ff) : juce::Colour(0xff1b222a));
        variationBtns[static_cast<size_t>(v)].setColour(
            juce::TextButton::textColourOffId,
            activeVar ? juce::Colours::black : juce::Colour(0xffb8c8d8));
    }

    bool scOn = eng.getMasterBus().isScatterActive();
    scatterOnBtn.setColour(juce::TextButton::buttonColourId, scOn ? juce::Colour(0xffff2b55) : juce::Colour(0xff241820));
    scatterOnBtn.setColour(juce::TextButton::textColourOffId, scOn ? juce::Colours::white : juce::Colour(0xffff8aa0));

    for (int i = 0; i < 8; ++i) {
        bool selType = (eng.getMasterBus().getScatterType() == (i + 1));
        scatterTypeBtns[static_cast<size_t>(i)].setColour(
            juce::TextButton::buttonColourId,
            selType ? juce::Colour(0xff38ff9c) : juce::Colour(0xff182228));
        scatterTypeBtns[static_cast<size_t>(i)].setColour(
            juce::TextButton::textColourOffId,
            selType ? juce::Colours::black : juce::Colour(0xff8ad8b8));
    }

    bool playing = eng.isPlaying();
    startStopBtn.setColour(juce::TextButton::buttonColourId, playing ? juce::Colour(0xff24ff68) : juce::Colour(0xff28322c));
    startStopBtn.setColour(juce::TextButton::textColourOffId, playing ? juce::Colours::black : juce::Colours::white);

    fillInBtn.setColour(juce::TextButton::buttonColourId, eng.isFillInActive() ? juce::Colour(0xffffbe1a) : juce::Colour(0xff282218));
    fillInBtn.setColour(juce::TextButton::textColourOffId, eng.isFillInActive() ? juce::Colours::black : juce::Colour(0xffffd478));

    editAccentBtn.setColour(juce::TextButton::buttonColourId, editingAccentTrack ? juce::Colour(0xffff3b5c) : juce::Colour(0xff261b20));
    editAccentBtn.setColour(juce::TextButton::textColourOffId, editingAccentTrack ? juce::Colours::white : juce::Colour(0xffff90a4));

    clearVarBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff241c1c));
    clearVarBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffff8a8a));

    lcdDisplay.repaint();
    repaint();
}

void ReplicantTR8SAudioProcessorEditor::handleStepPadClick(int stepIdx, const juce::ModifierKeys& mods) {
    auto& eng = audioProcessor.getEngine();
    int targetTrack = editingAccentTrack ? TR8SFirmware::NUM_CHANNELS : eng.getSelectedChannel();
    bool withFlam = mods.isRightButtonDown() || mods.isShiftDown();
    bool weakStep = mods.isAltDown();
    eng.toggleStep(targetTrack, stepIdx, withFlam, weakStep);
    repaint();
}

void ReplicantTR8SAudioProcessorEditor::mouseDown(const juce::MouseEvent& e) {
    for (int s = 0; s < 16; ++s) {
        if (stepPadRects[static_cast<size_t>(s)].contains(e.getPosition())) {
            handleStepPadClick(s, e.mods);
            return;
        }
    }
}

void ReplicantTR8SAudioProcessorEditor::resized() {
    // 1. Top-Left LCD Display Module
    lcdDisplay.setBounds(18, 34, 560, 168);

    // 2. Top-Center Tempo + Scatter + Accent Section (x = 588..895)
    ledTempoRect = juce::Rectangle<float>(598.0f, 40.0f, 114.0f, 42.0f);
    tempoKnob.setBounds(718, 34, 54, 54);
    shuffleKnob.setBounds(776, 34, 54, 54);
    accentKnob.setBounds(834, 34, 54, 54);

    // Scatter controls row (y = 104..196)
    scatterOnBtn.setBounds(598, 108, 102, 34);
    scatterDepthKnob.setBounds(706, 98, 52, 52);

    int scX = 598;
    int scY = 152;
    for (int i = 0; i < 8; ++i) {
        scatterTypeBtns[static_cast<size_t>(i)].setBounds(scX + i * 36, scY, 34, 24);
    }

    // 3. Top-Right Master FX & Bus Section (x = 905..1262)
    masterFilterKnob.setBounds(916, 42, 64, 64);
    masterDriveKnob.setBounds(986, 42, 64, 64);
    reverbKnob.setBounds(1056, 42, 64, 64);
    delayKnob.setBounds(1126, 42, 64, 64);
    masterVolKnob.setBounds(1196, 42, 60, 64);

    // 4. Middle Section: 11 Hardware Channel Strips (x = 18..1262, y = 212..588)
    int startX = 18;
    int totalW = 1244;
    int stripW = totalW / TR8SFirmware::NUM_CHANNELS; // 113 px each

    for (int ch = 0; ch < TR8SFirmware::NUM_CHANNELS; ++ch) {
        int sx = startX + ch * stripW;
        auto& strip = channelStrips[static_cast<size_t>(ch)];
        strip.stripBounds = juce::Rectangle<int>(sx, 212, stripW - 4, 374);

        int cx = sx + (stripW - 4) / 2;
        strip.tuneKnob.setBounds(cx - 22, 230, 44, 44);
        strip.decayKnob.setBounds(cx - 22, 283, 44, 44);

        // Per-channel REVERB and DELAY controls positioned between DECAY and CTRL
        strip.reverbKnob.setBounds(sx + 9, 336, 40, 40);
        strip.delayKnob.setBounds(sx + 58, 336, 40, 40);

        strip.ctrlKnob.setBounds(cx - 22, 388, 44, 44);

        // Left column inside strip: MUTE, SOLO, SMP buttons
        int btnX = sx + 8;
        strip.muteBtn.setBounds(btnX, 445, 36, 23);
        strip.soloBtn.setBounds(btnX, 472, 36, 23);
        strip.sampleBtn.setBounds(btnX, 499, 36, 23);

        // Right column inside strip: Illuminated Vertical Level Fader (max 150)
        strip.levelFader.setBounds(sx + 48, 442, 54, 84);

        // Bottom of strip: Instrument Select & Trigger Pad
        strip.instPadBtn.setBounds(sx + 8, 540, stripW - 20, 38);
    }

    // 5. Bottom Section: Transport, Variations A..H, and 16 TR-REC RGB Step Pads (y = 594..750)
    startStopBtn.setBounds(18, 600, 134, 36);

    int varStartX = 166;
    for (int v = 0; v < 8; ++v) {
        variationBtns[static_cast<size_t>(v)].setBounds(varStartX + v * 46, 600, 42, 36);
    }

    fillInBtn.setBounds(546, 600, 92, 36);
    editAccentBtn.setBounds(648, 600, 112, 36);
    clearVarBtn.setBounds(770, 600, 78, 36);

    // 16 RGB Step Sequencer Pads across bottom (y = 648..744)
    int padStartX = 18;
    int padStepW = 1244 / 16; // 77 px each
    for (int s = 0; s < 16; ++s) {
        stepPadRects[static_cast<size_t>(s)] = juce::Rectangle<int>(padStartX + s * padStepW, 648, padStepW - 6, 94);
    }

    headerFirmwareBtn.setBounds(594, 12, 166, 19);
    donateCoffeeBtn.setBounds(766, 12, 130, 19);
    firmwareOverlay.setBounds(getLocalBounds());
}

void ReplicantTR8SAudioProcessorEditor::paint(juce::Graphics& g) {
    // 1. Brushed Anodized Black Aluminum Chassis Background
    juce::ColourGradient bgGrad(juce::Colour(0xff1c2026), 0.0f, 0.0f,
                                juce::Colour(0xff101317), 0.0f, static_cast<float>(getHeight()), false);
    g.setGradientFill(bgGrad);
    g.fillAll();

    // Brushed metal horizontal micro-lines
    g.setColour(juce::Colours::white.withAlpha(0.015f));
    for (int y = 0; y < getHeight(); y += 2) {
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
    }

    // 2. Top Green / Gold Trim Line & Hardware Header
    g.setColour(juce::Colour(0xff00e65b));
    g.fillRect(18, 8, 1244, 3);

    g.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
    g.setColour(juce::Colours::white);
    g.drawText("REPLICANT  //  TR-8S", 18, 12, 310, 20, juce::Justification::centredLeft, false);

    g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    g.setColour(juce::Colour(0xff00e65b));
    g.drawText("RHYTHM PERFORMER  |  ACB + FM + SAMPLE ENGINE  |  FIRMWARE 3.00 (1:1)",
               310, 13, 520, 18, juce::Justification::centredLeft, false);

    // 3. Top-Center Tempo + Scatter Panel Box
    juce::Rectangle<float> centerBox(588.0f, 34.0f, 308.0f, 168.0f);
    g.setColour(juce::Colour(0xff14181e));
    g.fillRoundedRectangle(centerBox, 5.0f);
    g.setColour(juce::Colour(0xff2b3440));
    g.drawRoundedRectangle(centerBox, 5.0f, 1.2f);

    ReplicantTR8S::TR8SLookAndFeel::drawSevenSegmentNumber(
        g, ledTempoRect, audioProcessor.getEngine().getTempoBpm(), juce::Colour(0xffff2438));

    g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
    g.setColour(juce::Colour(0xffa8b4c2));
    g.drawText("TEMPO BPM", 598, 83, 114, 12, juce::Justification::centred, false);
    g.drawText("FINE", 718, 86, 54, 12, juce::Justification::centred, false);
    g.drawText("SHUFFLE", 776, 86, 54, 12, juce::Justification::centred, false);
    g.drawText("ACCENT", 834, 86, 54, 12, juce::Justification::centred, false);
    g.drawText("DEPTH", 760, 118, 48, 12, juce::Justification::centredLeft, false);
    g.drawText("SCATTER MODE (1-8)", 598, 180, 288, 14, juce::Justification::centred, false);

    // 4. Top-Right Master FX & Reverb/Delay Panel Box
    juce::Rectangle<float> masterBox(905.0f, 34.0f, 357.0f, 168.0f);
    g.setColour(juce::Colour(0xff14181e));
    g.fillRoundedRectangle(masterBox, 5.0f);
    g.setColour(juce::Colour(0xff2b3440));
    g.drawRoundedRectangle(masterBox, 5.0f, 1.2f);

    g.setColour(juce::Colour(0xff00e5ff));
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText("MASTER FX  |  REVERB  |  DELAY  |  BUS", 916, 18, 340, 14, juce::Justification::centredLeft, false);

    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.setColour(juce::Colour(0xffc0cad6));
    g.drawText("LPF/HPF", 916, 108, 64, 13, juce::Justification::centred, false);
    g.drawText("DRIVE", 986, 108, 64, 13, juce::Justification::centred, false);
    g.drawText("REVERB", 1056, 108, 64, 13, juce::Justification::centred, false);
    g.drawText("DELAY", 1126, 108, 64, 13, juce::Justification::centred, false);
    g.drawText("MASTER", 1196, 108, 60, 13, juce::Justification::centred, false);

    // Mini status legend inside Master box
    g.setColour(juce::Colour(0xff0d1217));
    g.fillRoundedRectangle(918.0f, 132.0f, 332.0f, 58.0f, 4.0f);
    g.setColour(juce::Colour(0xff243240));
    g.drawRoundedRectangle(918.0f, 132.0f, 332.0f, 58.0f, 4.0f, 1.0f);

    g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::plain)));
    g.setColour(juce::Colour(0xff84f5ff));
    g.drawText("> Drag & Drop WAV/AIFF/FLAC/MP3 onto any channel or LCD", 928, 138, 316, 16, juce::Justification::centredLeft, false);
    g.drawText("> Per-Channel REVERB & DELAY + Max 150 Anti-Clip Fader", 928, 154, 316, 16, juce::Justification::centredLeft, false);
    g.drawText("> Step Pads: Click = On/Off | Right-Click/Shift = Flam", 928, 170, 316, 16, juce::Justification::centredLeft, false);

    // 5. Draw 11 Channel Strips Framing & Labels
    const auto& eng = audioProcessor.getEngine();
    for (int ch = 0; ch < TR8SFirmware::NUM_CHANNELS; ++ch) {
        const auto& strip = channelStrips[static_cast<size_t>(ch)];
        auto sb = strip.stripBounds.toFloat();
        bool isSel = (eng.getSelectedChannel() == ch);
        juce::Colour chCol = ReplicantTR8S::TR8SLookAndFeel::getChannelColour(ch);

        g.setColour(isSel ? juce::Colour(0xff1b232c) : juce::Colour(0xff14181e));
        g.fillRoundedRectangle(sb, 4.5f);

        g.setColour(isSel ? chCol : juce::Colour(0xff28303a));
        g.drawRoundedRectangle(sb, 4.5f, isSel ? 1.8f : 1.0f);

        // Top channel color banner + mixer level readout (0..150) + activity LED
        g.setColour(chCol.withAlpha(0.85f));
        g.fillRoundedRectangle(sb.getX() + 4.0f, sb.getY() + 4.0f, sb.getWidth() - 8.0f, 16.0f, 2.5f);

        const auto& voice = eng.getVoice(ch);
        int chLevel150 = juce::jlimit(0, ReplicantTR8S::ACBDrumVoice::MAX_MIXER_LEVEL,
                                      static_cast<int>(std::round(voice.getLevel() * static_cast<float>(ReplicantTR8S::ACBDrumVoice::MAX_MIXER_LEVEL))));

        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.setColour(juce::Colours::black);
        g.drawText(TR8SFirmware::CHANNEL_NAMES[ch],
                   static_cast<int>(sb.getX() + 8), static_cast<int>(sb.getY() + 4),
                   30, 16, juce::Justification::centredLeft, false);

        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.setColour(juce::Colours::black.withAlpha(0.82f));
        g.drawText("LVL " + juce::String(chLevel150),
                   static_cast<int>(sb.getX() + 34), static_cast<int>(sb.getY() + 4),
                   52, 16, juce::Justification::centred, false);

        // Activity peak LED on channel header
        float peak = voice.getRecentPeak();
        juce::Rectangle<float> ledDot(sb.getRight() - 16.0f, sb.getY() + 7.0f, 9.0f, 9.0f);
        g.setColour(peak > 0.05f ? juce::Colours::white.interpolatedWith(chCol, 0.3f) : juce::Colour(0xff222830));
        g.fillEllipse(ledDot);

        // Knob labels inside strip (TUNE -> DECAY -> REVERB / DELAY -> CTRL)
        g.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
        g.setColour(juce::Colour(0xff9aa6b4));
        g.drawText("TUNE", static_cast<int>(sb.getX()), 272, static_cast<int>(sb.getWidth()), 11, juce::Justification::centred, false);
        g.drawText("DECAY", static_cast<int>(sb.getX()), 325, static_cast<int>(sb.getWidth()), 11, juce::Justification::centred, false);

        g.setColour(juce::Colour(0xff7eeeff));
        g.drawText("REVERB", static_cast<int>(sb.getX() + 4), 375, 50, 11, juce::Justification::centred, false);
        g.setColour(juce::Colour(0xff68ffb8));
        g.drawText("DELAY", static_cast<int>(sb.getX() + 53), 375, 50, 11, juce::Justification::centred, false);

        g.setColour(juce::Colour(0xff9aa6b4));
        g.drawText("CTRL", static_cast<int>(sb.getX()), 430, static_cast<int>(sb.getWidth()), 11, juce::Justification::centred, false);

        // Tone/Sample short label above trigger pad
        juce::String shortTone = voice.hasCustomSample()
            ? ("[U] " + voice.getCustomSampleName())
            : juce::String(TR8SFirmware::FACTORY_TONES[voice.getToneId()].name);
        g.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::plain)));
        g.setColour(voice.hasCustomSample() ? juce::Colour(0xffffc842) : juce::Colour(0xff8cd8f0));
        g.drawText(shortTone, static_cast<int>(sb.getX() + 4), 526, static_cast<int>(sb.getWidth() - 8), 12, juce::Justification::centred, false);
    }

    // 6. Sequencer Mode Status Banner (right side of Variation row)
    g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
    g.setColour(juce::Colour(0xff7ff7ff));
    int selCh = eng.getSelectedChannel();
    juce::String seqHeader = editingAccentTrack
        ? "TR-REC TARGET: [ACCENT TRACK]  |  16-STEP SEQUENCER"
        : juce::String::formatted("TR-REC TARGET: [%s - %s]  |  VAR [%c]",
            TR8SFirmware::CHANNEL_NAMES[selCh],
            TR8SFirmware::CHANNEL_FULL_NAMES[selCh],
            'A' + eng.getCurrentVariation());
    g.drawText(seqHeader, 860, 600, 402, 36, juce::Justification::centredRight, false);

    // 7. Draw 16 RGB TR-REC Step Sequencer Pads (1..16)
    int curVar = eng.getCurrentVariation();
    int curPlayStep = eng.getCurrentStep();
    bool isRunning = eng.isPlaying();
    int targetTrack = editingAccentTrack ? TR8SFirmware::NUM_CHANNELS : selCh;

    static const juce::Colour kGroupColours[4] = {
        juce::Colour(0xffff2b38), // Steps 1..4: Classic TR Red
        juce::Colour(0xffff7e1a), // Steps 5..8: Classic TR Orange
        juce::Colour(0xffffd41a), // Steps 9..12: Classic TR Yellow
        juce::Colour(0xffeef4fa)  // Steps 13..16: Classic TR White
    };

    for (int s = 0; s < 16; ++s) {
        auto r = stepPadRects[static_cast<size_t>(s)].toFloat();
        juce::Colour grpCol = kGroupColours[s / 4];
        auto cell = eng.getStep(curVar, targetTrack, s);
        bool stepActive = (cell.velocity > 0);
        bool isPlayhead = isRunning && (curPlayStep == s);
        bool hasFlam = (cell.flags & 0x0F) != 0;

        // Outer pad housing
        g.setColour(juce::Colour(0xff0b0e12));
        g.fillRoundedRectangle(r, 5.0f);

        // Translucent illuminated silicone pad surface
        juce::Colour padFill = stepActive
            ? grpCol.withAlpha(cell.velocity > 90 ? 0.95f : (cell.velocity < 65 ? 0.48f : 0.80f))
            : grpCol.withAlpha(0.12f);

        if (isPlayhead) {
            padFill = padFill.interpolatedWith(juce::Colours::white, 0.55f);
        }

        auto innerPad = r.reduced(3.0f, 3.0f);
        g.setColour(padFill);
        g.fillRoundedRectangle(innerPad, 4.0f);

        // Top LED indicator jewel inside step pad
        juce::Rectangle<float> ledBar(innerPad.getX() + 10.0f, innerPad.getY() + 6.0f, innerPad.getWidth() - 20.0f, 7.0f);
        g.setColour(isPlayhead ? juce::Colour(0xff38ff9c) : (stepActive ? juce::Colours::white : juce::Colour(0xff262c34)));
        g.fillRoundedRectangle(ledBar, 2.5f);

        // Step Number (1..16)
        g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
        g.setColour((stepActive || isPlayhead) ? juce::Colours::black : grpCol.withAlpha(0.85f));
        g.drawText(juce::String(s + 1), innerPad.reduced(0.0f, 16.0f), juce::Justification::centredTop, false);

        // Velocity / Substep badge at bottom of pad
        if (stepActive) {
            juce::String tag = hasFlam ? "FLAM" : (cell.velocity > 95 ? "ACC" : (cell.velocity < 65 ? "WEAK" : "ON"));
            g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
            g.setColour(juce::Colours::black.withAlpha(0.85f));
            g.drawText(tag, innerPad.reduced(2.0f, 5.0f), juce::Justification::centredBottom, false);
        }

        g.setColour(isPlayhead ? juce::Colours::white : grpCol.withAlpha(stepActive ? 1.0f : 0.35f));
        g.drawRoundedRectangle(innerPad, 4.0f, isPlayhead ? 2.2f : 1.2f);
    }
}
