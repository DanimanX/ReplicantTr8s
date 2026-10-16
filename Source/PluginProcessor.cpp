#include "PluginProcessor.h"
#include "PluginEditor.h"

ReplicantTR8SAudioProcessor::ReplicantTR8SAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "TR8S_PARAMETERS", createParameterLayout())
{
    engine.onKitLoaded = [this]() {
        syncParametersFromEngineKit();
    };
    syncParametersFromEngineKit();
}

ReplicantTR8SAudioProcessor::~ReplicantTR8SAudioProcessor() = default;

juce::String ReplicantTR8SAudioProcessor::getChannelParamId(int ch, const char* suffix) {
    return juce::String(TR8SFirmware::CHANNEL_NAMES[ch]) + "_" + suffix;
}

juce::AudioProcessorValueTreeState::ParameterLayout ReplicantTR8SAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (int ch = 0; ch < TR8SFirmware::NUM_CHANNELS; ++ch) {
        juce::String chName = TR8SFirmware::CHANNEL_NAMES[ch];
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ getChannelParamId(ch, "TUNE"), 1 },
            chName + " Tune",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
            0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ getChannelParamId(ch, "DECAY"), 1 },
            chName + " Decay",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
            0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ getChannelParamId(ch, "CTRL"), 1 },
            chName + " Ctrl",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
            0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ getChannelParamId(ch, "LEVEL"), 1 },
            chName + " Level",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
            0.85f));
    }

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "MASTER_VOLUME", 1 }, "Master Volume",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.85f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "ACCENT_LEVEL", 1 }, "Accent Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.65f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "REVERB_LEVEL", 1 }, "Reverb Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.25f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "DELAY_LEVEL", 1 }, "Delay Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.20f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "MASTER_FILTER", 1 }, "Master Filter (LPF/HPF)",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.50f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "MASTER_DRIVE", 1 }, "Master Drive",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.15f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "TEMPO_BPM", 1 }, "Tempo BPM",
        juce::NormalisableRange<float>(40.0f, 300.0f, 0.1f), 128.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "SHUFFLE", 1 }, "Shuffle",
        juce::NormalisableRange<float>(-0.5f, 0.5f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "SCATTER_DEPTH", 1 }, "Scatter Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    return { params.begin(), params.end() };
}

void ReplicantTR8SAudioProcessor::syncParametersFromEngineKit() {
    syncingFromKit = true;
    for (int ch = 0; ch < TR8SFirmware::NUM_CHANNELS; ++ch) {
        const auto& v = engine.getVoice(ch);
        if (auto* p = apvts.getParameter(getChannelParamId(ch, "TUNE")))
            p->setValueNotifyingHost(v.getTune());
        if (auto* p = apvts.getParameter(getChannelParamId(ch, "DECAY")))
            p->setValueNotifyingHost(v.getDecay());
        if (auto* p = apvts.getParameter(getChannelParamId(ch, "CTRL")))
            p->setValueNotifyingHost(v.getCtrl());
        if (auto* p = apvts.getParameter(getChannelParamId(ch, "LEVEL")))
            p->setValueNotifyingHost(v.getLevel());
    }
    if (auto* p = apvts.getParameter("TEMPO_BPM"))
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(engine.getTempoBpm())));
    syncingFromKit = false;
}

void ReplicantTR8SAudioProcessor::pushParametersToEngine() {
    if (syncingFromKit)
        return;

    for (int ch = 0; ch < TR8SFirmware::NUM_CHANNELS; ++ch) {
        auto& v = engine.getVoice(ch);
        v.setTune(apvts.getRawParameterValue(getChannelParamId(ch, "TUNE"))->load());
        v.setDecay(apvts.getRawParameterValue(getChannelParamId(ch, "DECAY"))->load());
        v.setCtrl(apvts.getRawParameterValue(getChannelParamId(ch, "CTRL"))->load());
        v.setLevel(apvts.getRawParameterValue(getChannelParamId(ch, "LEVEL"))->load());
    }

    engine.getMasterBus().setMasterVolume(apvts.getRawParameterValue("MASTER_VOLUME")->load());
    engine.setAccentLevel(apvts.getRawParameterValue("ACCENT_LEVEL")->load());
    engine.getMasterBus().setReverbLevel(apvts.getRawParameterValue("REVERB_LEVEL")->load());
    engine.getMasterBus().setDelayLevel(apvts.getRawParameterValue("DELAY_LEVEL")->load());
    engine.getMasterBus().setMasterFxFilter(apvts.getRawParameterValue("MASTER_FILTER")->load());
    engine.getMasterBus().setMasterDrive(apvts.getRawParameterValue("MASTER_DRIVE")->load());
    engine.setTempoBpm(static_cast<double>(apvts.getRawParameterValue("TEMPO_BPM")->load()));
    engine.setShuffle(apvts.getRawParameterValue("SHUFFLE")->load());
    engine.getMasterBus().setScatterDepth(apvts.getRawParameterValue("SCATTER_DEPTH")->load());
}

void ReplicantTR8SAudioProcessor::notifyParameterPopup(const juce::String& title, const juce::String& valueText, float normVal) {
    popupTitle = title;
    popupValueText = valueText;
    popupNormValue = juce::jlimit(0.0f, 1.0f, normVal);
    popupTimestampMs = juce::Time::getMillisecondCounter();
}

const juce::String ReplicantTR8SAudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool ReplicantTR8SAudioProcessor::acceptsMidi() const { return true; }
bool ReplicantTR8SAudioProcessor::producesMidi() const { return false; }
bool ReplicantTR8SAudioProcessor::isMidiEffect() const { return false; }
double ReplicantTR8SAudioProcessor::getTailLengthSeconds() const { return 2.5; }

int ReplicantTR8SAudioProcessor::getNumPrograms() {
    return TR8SFirmware::NUM_KITS;
}

int ReplicantTR8SAudioProcessor::getCurrentProgram() {
    return engine.getCurrentKitIndex();
}

void ReplicantTR8SAudioProcessor::setCurrentProgram(int index) {
    engine.loadFactoryKit(index);
}

const juce::String ReplicantTR8SAudioProcessor::getProgramName(int index) {
    int idx = juce::jlimit(0, TR8SFirmware::NUM_KITS - 1, index);
    return juce::String::formatted("%03d: %s", idx + 1, TR8SFirmware::FACTORY_KITS[idx].name);
}

void ReplicantTR8SAudioProcessor::changeProgramName(int, const juce::String&) {}

void ReplicantTR8SAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine.prepare(sampleRate, samplesPerBlock);
}

void ReplicantTR8SAudioProcessor::releaseResources() {
    engine.reset();
}

bool ReplicantTR8SAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

void ReplicantTR8SAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    pushParametersToEngine();

    bool hostIsPlaying = false;
    double hostBpm = engine.getTempoBpm();
    double hostPpq = 0.0;

    if (auto* currentPlayHead = getPlayHead()) {
        if (auto pos = currentPlayHead->getPosition()) {
            hostIsPlaying = pos->getIsPlaying();
            if (auto bpmOpt = pos->getBpm())
                hostBpm = *bpmOpt;
            if (auto ppqOpt = pos->getPpqPosition())
                hostPpq = *ppqOpt;
        }
    }

    engine.processBlock(buffer, midiMessages, hostIsPlaying, hostBpm, hostPpq);
}

bool ReplicantTR8SAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* ReplicantTR8SAudioProcessor::createEditor() {
    return new ReplicantTR8SAudioProcessorEditor(*this);
}

void ReplicantTR8SAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    state.setProperty("kitIndex", engine.getCurrentKitIndex(), nullptr);
    state.setProperty("patternIndex", engine.getCurrentPatternIndex(), nullptr);
    state.setProperty("variationIndex", engine.getCurrentVariation(), nullptr);
    state.setProperty("selectedChannel", engine.getSelectedChannel(), nullptr);

    for (int ch = 0; ch < TR8SFirmware::NUM_CHANNELS; ++ch) {
        const auto& v = engine.getVoice(ch);
        state.setProperty("tone_" + juce::String(ch), static_cast<int>(v.getToneId()), nullptr);
        state.setProperty("samplePath_" + juce::String(ch), v.getCustomSamplePath(), nullptr);
        state.setProperty("sampleRev_" + juce::String(ch), v.isSampleReversed(), nullptr);
    }

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void ReplicantTR8SAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType())) {
        auto vt = juce::ValueTree::fromXml(*xmlState);
        apvts.replaceState(vt);

        if (vt.hasProperty("patternIndex"))
            engine.loadFactoryPattern(static_cast<int>(vt.getProperty("patternIndex")), false);
        if (vt.hasProperty("kitIndex"))
            engine.loadFactoryKit(static_cast<int>(vt.getProperty("kitIndex")));
        if (vt.hasProperty("variationIndex"))
            engine.setCurrentVariation(static_cast<int>(vt.getProperty("variationIndex")));
        if (vt.hasProperty("selectedChannel"))
            engine.setSelectedChannel(static_cast<int>(vt.getProperty("selectedChannel")));

        for (int ch = 0; ch < TR8SFirmware::NUM_CHANNELS; ++ch) {
            auto& v = engine.getVoice(ch);
            if (vt.hasProperty("tone_" + juce::String(ch)))
                v.setToneId(static_cast<uint16_t>(static_cast<int>(vt.getProperty("tone_" + juce::String(ch)))));
            juce::String spath = vt.getProperty("samplePath_" + juce::String(ch)).toString();
            if (spath.isNotEmpty()) {
                juce::File f(spath);
                if (f.existsAsFile())
                    engine.loadSampleForChannel(ch, f);
            }
            if (vt.hasProperty("sampleRev_" + juce::String(ch)))
                v.setSampleReverse(static_cast<bool>(vt.getProperty("sampleRev_" + juce::String(ch))));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new ReplicantTR8SAudioProcessor();
}
