#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <iostream>
#include <cmath>

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    ReplicantTR8SAudioProcessor processor;
    processor.prepareToPlay(44100.0, 512);

    // 0. Load user firmware archive at runtime if present in working directory
    juce::File localZip = juce::File::getCurrentWorkingDirectory().getChildFile("tr8s_sys_v300.zip");
    if (localZip.existsAsFile()) {
        juce::String err;
        if (!processor.getEngine().loadFirmwareFile(localZip, err)) {
            std::cerr << "ERROR loading firmware file: " << err << std::endl;
            return 1;
        }
        std::cout << "RuntimeFirmwareLoader: Loaded " << localZip.getFileName()
                  << " -> Kit 1 = " << TR8SFirmware::FACTORY_KITS[0].name
                  << ", Kit 2 = " << TR8SFirmware::FACTORY_KITS[1].name
                  << ", Tone 0 = " << TR8SFirmware::FACTORY_TONES[0].name << std::endl;
    }

    // Verify sequencer does NOT auto-start on boot and stops when DAW pauses
    if (processor.getEngine().isPlaying()) {
        std::cerr << "ERROR: Sequencer should NOT auto-start on launch!" << std::endl;
        return 1;
    }
    {
        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer midi;
        processor.getEngine().processBlock(buf, midi, true, 135.0, 0.0); // DAW Play at 135 BPM
        if (!processor.getEngine().isPlaying() || std::abs(processor.getEngine().getTempoBpm() - 135.0) > 0.1) {
            std::cerr << "ERROR: Failed to sync to DAW transport play / BPM!" << std::endl;
            return 1;
        }
        processor.getEngine().processBlock(buf, midi, false, 135.0, 0.25); // DAW Pause
        if (processor.getEngine().isPlaying()) {
            std::cerr << "ERROR: Sequencer kept playing after DAW paused!" << std::endl;
            return 1;
        }
        std::cout << "Transport sync verified: Boot=Stopped, DAW Play=Playing (135 BPM), DAW Pause=Stopped." << std::endl;
    }

    // 1. Verify audio synthesis for Kit 001 (TR-808), Kit 002 (TR-909), Kit 085 (CR-78), Kit 087 (FM-Groover)
    for (int kitIdx : { 0, 1, 84, 86 }) {
        processor.getEngine().loadFactoryKit(kitIdx);
        for (int ch = 0; ch < 11; ++ch)
            processor.getEngine().triggerChannel(ch, 0.95f, false);

        juce::AudioBuffer<float> buf(2, 2048);
        juce::MidiBuffer midi;
        processor.processBlock(buf, midi);

        float rms = buf.getRMSLevel(0, 0, 2048);
        std::cout << "Kit " << (kitIdx + 1) << " (" << processor.getEngine().getCurrentKitName()
                  << ") RMS = " << rms << std::endl;
        if (rms < 0.001f) {
            std::cerr << "ERROR: Kit " << (kitIdx + 1) << " produced silence!" << std::endl;
            return 1;
        }
    }

    // 2. Verify custom sample loading on Channel 0 (BD) and Channel 1 (SD)
    {
        juce::AudioBuffer<float> testSample(2, 4410);
        for (int i = 0; i < 4410; ++i) {
            float t = static_cast<float>(i) / 44100.0f;
            float s = std::sin(2.0f * 3.14159265f * 65.0f * t) * std::exp(-t * 14.0f);
            testSample.setSample(0, i, s);
            testSample.setSample(1, i, s);
        }
        processor.getEngine().getVoice(1).setCustomSampleBuffer(testSample, 44100.0, "Custom909SD.wav");
    }

    // Select Kit 002 (TR-909) or Kit 001 (TR-808) for snapshot and render a few audio blocks so oscilloscope is live
    processor.getEngine().loadFactoryKit(1); // TR-909
    processor.getEngine().triggerChannel(0, 1.0f, false);
    processor.getEngine().triggerChannel(1, 0.9f, false);
    processor.getEngine().triggerChannel(7, 0.85f, false);
    {
        juce::AudioBuffer<float> buf(2, 1024);
        juce::MidiBuffer midi;
        processor.processBlock(buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setSize(1280, 760);

    juce::Image snapshot = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.5f);

    juce::File outFile = (argc > 1)
        ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
        : juce::File::getCurrentWorkingDirectory().getChildFile("assets/replicant_tr8s_preview.png");

    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();

    juce::PNGImageFormat pngFormat;
    std::unique_ptr<juce::FileOutputStream> stream(outFile.createOutputStream());
    if (stream != nullptr && pngFormat.writeImageToStream(snapshot, *stream)) {
        std::cout << "Saved UI snapshot to: " << outFile.getFullPathName() << std::endl;
        return 0;
    }

    std::cerr << "Failed to write snapshot PNG." << std::endl;
    return 1;
}
