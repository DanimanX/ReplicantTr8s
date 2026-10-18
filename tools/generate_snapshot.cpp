#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <iostream>
#include <cmath>

static bool saveEditorSnapshot(ReplicantTR8SAudioProcessorEditor& editor, const juce::File& outFile) {
    juce::Image snapshot = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.5f);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    juce::PNGImageFormat pngFormat;
    std::unique_ptr<juce::FileOutputStream> stream(outFile.createOutputStream());
    return (stream != nullptr && pngFormat.writeImageToStream(snapshot, *stream));
}

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    ReplicantTR8SAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);

    // 0. Load user firmware archive at runtime if present, or activate built-in preview mode
    juce::File localZip = juce::File::getCurrentWorkingDirectory().getChildFile("tr8s_sys_v300.zip");
    if (localZip.existsAsFile()) {
        juce::String err;
        processor.getEngine().loadFirmwareFile(localZip, err);
    }
    if (!processor.getEngine().isFirmwareLoaded()) {
        TR8SFirmware::RuntimeFirmwareLoader::activateStandalonePreviewMode();
        processor.getEngine().loadFactoryPattern(103, false);
        processor.getEngine().loadFactoryKit(1);
    }

    // Program classic groove on BD (steps 1, 4, 9, 15) & full techno pattern
    auto& eng = processor.getEngine();
    eng.setTempoBpm(135.0);
    for (int s = 0; s < 16; ++s) {
        eng.setStep(0, 0, s, (s == 0 || s == 3 || s == 8 || s == 14) ? 90 : 0, 0); // BD
        eng.setStep(0, 1, s, (s == 4 || s == 12) ? 105 : ((s == 15) ? 75 : 0), (s == 15) ? 2 : 0); // SD
        eng.setStep(0, 2, s, (s == 3 || s == 11) ? 85 : 0, 0); // LT
        eng.setStep(0, 5, s, (s == 6 || s == 14) ? 90 : 0, 0); // RS
        eng.setStep(0, 6, s, (s == 4 || s == 12) ? 100 : 0, 0); // HC
        eng.setStep(0, 7, s, (s % 2 == 0) ? 80 : 65, 0); // CH
        eng.setStep(0, 8, s, (s % 4 == 2) ? 105 : 0, 0); // OH
        eng.setStep(0, 10, s, (s % 2 == 1) ? 75 : 0, 0); // RC
        eng.setStep(0, 11, s, (s % 4 == 0 || s == 14) ? 110 : 0, 0); // ACCENT
    }

    // 1. Load TR-909 kit & custom sample on SD before creating editor so LCD menus are 100% synced
    eng.loadFactoryKit(1);
    eng.setSelectedChannel(0); // BD selected for main preview
    eng.setChannelReverbSend(0, 0.25f);
    eng.setChannelDelaySend(0, 0.18f);
    eng.setChannelReverbSend(1, 0.68f);
    eng.setChannelDelaySend(1, 0.42f);
    processor.syncParametersFromEngineKit();

    {
        juce::AudioBuffer<float> testSample(2, 4410);
        for (int i = 0; i < 4410; ++i) {
            float t = static_cast<float>(i) / 44100.0f;
            float s = std::sin(2.0f * 3.14159265f * 185.0f * t) * std::exp(-t * 14.0f);
            testSample.setSample(0, i, s);
            testSample.setSample(1, i, s);
        }
        eng.getVoice(1).setCustomSampleBuffer(testSample, 44100.0, "Custom909SD.wav");
    }

    // Trigger voices so oscilloscope and channel peak LEDs are live
    eng.triggerChannel(0, 1.0f, false);
    eng.triggerChannel(1, 0.95f, false);
    eng.triggerChannel(7, 0.85f, false);
    {
        juce::AudioBuffer<float> buf(2, 1024);
        juce::MidiBuffer midi;
        processor.processBlock(buf, midi);
    }

    // Clear any popup so the main README preview shows the full unobstructed LCD with REV, DLY, and LEVEL 150 bars
    processor.notifyParameterPopup({}, {}, 0.0f);

    std::unique_ptr<juce::AudioProcessorEditor> rawEd(processor.createEditor());
    auto* editor = dynamic_cast<ReplicantTR8SAudioProcessorEditor*>(rawEd.get());
    editor->setSize(1280, 760);

    juce::File outMain = juce::File::getCurrentWorkingDirectory().getChildFile("assets/replicant_tr8s_preview.png");
    saveEditorSnapshot(*editor, outMain);
    std::cout << "Saved UI snapshot: " << outMain.getFullPathName() << std::endl;

    // Snapshot 2: FM Hard Techno Kit with Scatter ON & Delay popup active
    eng.loadFactoryKit(86);
    eng.setSelectedChannel(5); // RS
    eng.setChannelReverbSend(5, 0.55f);
    eng.setChannelDelaySend(5, 0.74f);
    eng.getMasterBus().setScatterActive(true);
    eng.getMasterBus().setScatterType(3);
    processor.syncParametersFromEngineKit();
    eng.triggerChannel(0, 1.0f, false);
    eng.triggerChannel(5, 0.95f, false);
    {
        juce::AudioBuffer<float> buf(2, 1024);
        juce::MidiBuffer midi;
        processor.processBlock(buf, midi);
    }
    processor.notifyParameterPopup("RS DELAY", "74%", 0.74f);

    juce::File outFM = juce::File::getCurrentWorkingDirectory().getChildFile("assets/replicant_tr8s_fm.png");
    saveEditorSnapshot(*editor, outFM);
    std::cout << "Saved FM UI snapshot: " << outFM.getFullPathName() << std::endl;

    // Snapshot 3: TR-808 Classic Kit with LEVEL 150 / 150 popup active
    eng.getMasterBus().setScatterActive(false);
    eng.loadFactoryKit(0);
    eng.setSelectedChannel(0); // BD
    processor.syncParametersFromEngineKit();
    eng.triggerChannel(0, 1.0f, false);
    {
        juce::AudioBuffer<float> buf(2, 1024);
        juce::MidiBuffer midi;
        processor.processBlock(buf, midi);
    }
    processor.notifyParameterPopup("BD LEVEL", "150 / 150", 1.0f);

    juce::File out808 = juce::File::getCurrentWorkingDirectory().getChildFile("assets/replicant_tr8s_808.png");
    saveEditorSnapshot(*editor, out808);
    std::cout << "Saved 808 UI snapshot: " << out808.getFullPathName() << std::endl;

    // Render 64s Hard Techno Drum Stem WAV from actual TR8SEngine DSP
    constexpr double kSampleRate = 48000.0;
    constexpr int kBlockSize = 512;
    constexpr double kDuration = 65.0;
    const int numBlocks = static_cast<int>(std::ceil(kDuration * kSampleRate / kBlockSize));
    juce::AudioBuffer<float> song(2, numBlocks * kBlockSize);
    song.clear();

    eng.loadFactoryKit(1); // Start with TR-909 Studio
    eng.getVoice(1).clearCustomSample();
    eng.setTempoBpm(138.0);
    eng.setPlaying(true);

    juce::AudioBuffer<float> block(2, kBlockSize);
    int curSection = -1;

    for (int n = 0; n < numBlocks; ++n) {
        double t = (n * kBlockSize) / kSampleRate;
        int sec = (t < 14.0) ? 0 : (t < 28.0 ? 1 : (t < 42.0 ? 2 : (t < 54.0 ? 3 : 4)));
        if (sec != curSection) {
            curSection = sec;
            if (sec == 0) eng.loadFactoryKit(0);       // 808 intro
            else if (sec == 1) eng.loadFactoryKit(1);  // 909 main groove
            else if (sec == 2) eng.loadFactoryKit(86); // FM Hard Techno
            else if (sec == 3) eng.loadFactoryKit(1);  // 909 + Scatter / FX climax
            processor.syncParametersFromEngineKit();
        }

        // Dynamic per-channel Reverb & Delay send modulation
        float lfo1 = 0.5f + 0.5f * std::sin(t * 0.8f);
        float lfo2 = 0.5f + 0.5f * std::cos(t * 1.1f);
        eng.setChannelReverbSend(1, 0.25f + 0.45f * lfo1); // Snare Reverb
        eng.setChannelReverbSend(6, 0.30f + 0.50f * lfo2); // Clap Reverb
        eng.setChannelDelaySend(5, 0.35f + 0.45f * lfo2);  // Rim Delay
        eng.setChannelDelaySend(7, 0.20f + 0.35f * lfo1);  // Closed HH Delay
        eng.getMasterBus().setScatterActive(t >= 38.0 && t < 40.0);
        eng.getMasterBus().setScatterType(2);

        block.clear();
        juce::MidiBuffer midi;
        eng.processBlock(block, midi, true, 138.0, (t * 138.0) / 60.0);
        for (int ch = 0; ch < 2; ++ch)
            song.copyFrom(ch, n * kBlockSize, block, ch, 0, kBlockSize);
    }

    juce::ignoreUnused(argc, argv);
    juce::File wavOut = juce::File::getCurrentWorkingDirectory().getChildFile("assets/replicant_tr8s_hardtechno_demo.wav");
    wavOut.deleteFile();
    juce::WavAudioFormat wav;
    if (std::unique_ptr<juce::OutputStream> ws = wavOut.createOutputStream()) {
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(ws.get(), kSampleRate, 2, 24, {}, 0));
        if (writer != nullptr) {
            ws.release();
            writer->writeFromAudioSampleBuffer(song, 0, numBlocks * kBlockSize);
            std::cout << "Saved DSP audio stem: " << wavOut.getFullPathName() << std::endl;
        }
    }

    return 0;
}

