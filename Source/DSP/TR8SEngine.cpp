#include "TR8SEngine.h"
#include <algorithm>
#include <cmath>

namespace ReplicantTR8S {

TR8SEngine::TR8SEngine() {
    formatManager.registerBasicFormats();
    TR8SFirmware::RuntimeFirmwareLoader::tryAutoLoadSavedFirmware();

    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
        voices[static_cast<size_t>(ch)].setChannelIndex(ch);
        channelMuted[static_cast<size_t>(ch)] = false;
        channelSoloed[static_cast<size_t>(ch)] = false;
        channelRevSend[static_cast<size_t>(ch)] = (ch == 1 || ch == 5 || ch == 6 || ch >= 8) ? 0.32f : 0.05f;
        channelDlySend[static_cast<size_t>(ch)] = (ch == 5 || ch == 6 || ch == 7) ? 0.22f : 0.02f;
    }

    loadFactoryPattern(103, false);
    loadFactoryKit(0);
}

bool TR8SEngine::loadFirmwareFile(const juce::File& file, juce::String& errorMsg, bool savePreference) {
    if (!TR8SFirmware::RuntimeFirmwareLoader::loadFromFile(file, errorMsg, savePreference))
        return false;

    loadFactoryPattern(currentPatternIndex, false);
    loadFactoryKit(currentKitIndex);
    return true;
}

void TR8SEngine::prepare(double newSampleRate, int maxBlockSize) {
    sampleRate = std::max(8000.0, newSampleRate);
    for (auto& v : voices)
        v.prepare(sampleRate);
    masterBus.prepare(sampleRate, maxBlockSize);
    masterBus.setTempoBpm(tempoBpm);
}

void TR8SEngine::reset() {
    for (auto& v : voices)
        v.reset();
    masterBus.reset();
    sampleCounterInStep = 0.0;
}

void TR8SEngine::loadFactoryKit(int kitIndex0Based) {
    currentKitIndex = juce::jlimit(0, TR8SFirmware::NUM_KITS - 1, kitIndex0Based);
    const auto& kit = TR8SFirmware::FACTORY_KITS[currentKitIndex];

    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
        const auto& preset = kit.channels[ch];
        auto& v = voices[static_cast<size_t>(ch)];
        v.setToneId(preset.toneId);
        v.setTune(static_cast<float>(preset.tune) / 255.0f);
        v.setDecay(static_cast<float>(preset.decay) / 255.0f);
        v.setLevel(juce::jlimit(0.15f, 1.0f, static_cast<float>(preset.level) / 255.0f));
        v.setPan(static_cast<float>(preset.pan) / 255.0f);
        v.setCtrl(static_cast<float>(preset.ctrl) / 255.0f);
        v.updateSynthWaveformPreview();

        channelRevSend[static_cast<size_t>(ch)] = static_cast<float>(preset.revSend) / 255.0f * 0.5f;
        channelDlySend[static_cast<size_t>(ch)] = static_cast<float>(preset.dlySend) / 255.0f * 0.4f;
    }

    if (onKitLoaded)
        onKitLoaded();
}

void TR8SEngine::loadFactoryPattern(int patternIndex0Based, bool alsoLoadKit) {
    currentPatternIndex = juce::jlimit(0, TR8SFirmware::NUM_PATTERNS - 1, patternIndex0Based);
    const auto& ptn = TR8SFirmware::FACTORY_PATTERNS[currentPatternIndex];

    setTempoBpm(static_cast<double>(ptn.tempoTenths) / 10.0);

    for (int v = 0; v < NUM_VARIATIONS; ++v) {
        for (int trk = 0; trk <= NUM_CHANNELS; ++trk) {
            for (int s = 0; s < NUM_STEPS; ++s) {
                patternGrid[v][trk][s].velocity = ptn.variations[v].velocity[trk][s];
                patternGrid[v][trk][s].flags    = ptn.variations[v].flags[trk][s];
            }
        }
    }

    // Ensure Variation A has a musical groove if the firmware pattern's Var A is sparse
    bool hasAnyActive = false;
    for (int trk = 0; trk < NUM_CHANNELS; ++trk)
        for (int s = 0; s < NUM_STEPS; ++s)
            if (patternGrid[0][trk][s].velocity > 0)
                hasAnyActive = true;

    if (!hasAnyActive) {
        // Fallback classic TR four-on-the-floor + offbeat hat + 2/4 snare & clap
        for (int s = 0; s < NUM_STEPS; ++s) {
            if (s % 4 == 0) patternGrid[0][0][s].velocity = 95; // BD
            if (s == 4 || s == 12) {
                patternGrid[0][1][s].velocity = 90; // SD
                patternGrid[0][6][s].velocity = 85; // HC
            }
            if (s % 2 == 0) patternGrid[0][7][s].velocity = 75; // CH
            if (s % 4 == 2) patternGrid[0][8][s].velocity = 85; // OH
        }
    }

    if (alsoLoadKit && ptn.kitNumber >= 1 && ptn.kitNumber <= TR8SFirmware::NUM_KITS) {
        loadFactoryKit(ptn.kitNumber - 1);
    }
}

juce::String TR8SEngine::getCurrentKitName() const {
    return TR8SFirmware::FACTORY_KITS[currentKitIndex].name;
}

juce::String TR8SEngine::getCurrentPatternName() const {
    return TR8SFirmware::FACTORY_PATTERNS[currentPatternIndex].name;
}

juce::String TR8SEngine::getCurrentPatternCode() const {
    return TR8SFirmware::FACTORY_PATTERNS[currentPatternIndex].code;
}

void TR8SEngine::setCurrentVariation(int varIdx) {
    currentVariation = juce::jlimit(0, NUM_VARIATIONS - 1, varIdx);
}

void TR8SEngine::copyVariation(int srcVar, int dstVar) {
    srcVar = juce::jlimit(0, NUM_VARIATIONS - 1, srcVar);
    dstVar = juce::jlimit(0, NUM_VARIATIONS - 1, dstVar);
    if (srcVar == dstVar) return;
    for (int t = 0; t <= NUM_CHANNELS; ++t)
        for (int s = 0; s < NUM_STEPS; ++s)
            patternGrid[dstVar][t][s] = patternGrid[srcVar][t][s];
}

void TR8SEngine::clearVariation(int varIdx) {
    varIdx = juce::jlimit(0, NUM_VARIATIONS - 1, varIdx);
    for (int t = 0; t <= NUM_CHANNELS; ++t)
        for (int s = 0; s < NUM_STEPS; ++s)
            patternGrid[varIdx][t][s] = StepCell{ 0, 0 };
}

StepCell TR8SEngine::getStep(int variation, int channelOrAccent, int step) const {
    int v = juce::jlimit(0, NUM_VARIATIONS - 1, variation);
    int c = juce::jlimit(0, NUM_CHANNELS, channelOrAccent);
    int s = juce::jlimit(0, NUM_STEPS - 1, step);
    return patternGrid[v][c][s];
}

void TR8SEngine::setStep(int variation, int channelOrAccent, int step, uint8_t velocity, uint8_t flags) {
    int v = juce::jlimit(0, NUM_VARIATIONS - 1, variation);
    int c = juce::jlimit(0, NUM_CHANNELS, channelOrAccent);
    int s = juce::jlimit(0, NUM_STEPS - 1, step);
    patternGrid[v][c][s] = StepCell{ velocity, flags };
}

void TR8SEngine::toggleStep(int channelOrAccent, int step, bool withFlam, bool weakStep) {
    int c = juce::jlimit(0, NUM_CHANNELS, channelOrAccent);
    int s = juce::jlimit(0, NUM_STEPS - 1, step);
    auto& cell = patternGrid[currentVariation][c][s];

    if (withFlam) {
        if (cell.velocity == 0) cell.velocity = 85;
        cell.flags = (cell.flags & 0x0F) ? 0 : 2; // Toggle flam substep
        return;
    }
    if (weakStep) {
        cell.velocity = (cell.velocity == 50) ? 115 : 50; // Cycle ghost <-> accented
        return;
    }

    if (cell.velocity > 0) {
        cell.velocity = 0;
        cell.flags = 0;
    } else {
        cell.velocity = 85;
        cell.flags = 0;
    }
}

void TR8SEngine::setPlaying(bool play) {
    bool wasPlaying = internalPlaying.exchange(play, std::memory_order_relaxed);
    if (play && !wasPlaying) {
        playheadStep.store(0, std::memory_order_relaxed);
        sampleCounterInStep = 0.0;
        advanceSequencerStep(0);
    }
}

void TR8SEngine::setTempoBpm(double bpm) {
    tempoBpm = juce::jlimit(40.0, 300.0, bpm);
    masterBus.setTempoBpm(tempoBpm);
}

bool TR8SEngine::isChannelAudible(int ch) const {
    bool anySolo = false;
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        if (channelSoloed[static_cast<size_t>(i)]) {
            anySolo = true;
            break;
        }
    }
    if (anySolo)
        return channelSoloed[static_cast<size_t>(ch)];
    return !channelMuted[static_cast<size_t>(ch)];
}

void TR8SEngine::triggerChannel(int ch, float velocity, bool isFlam) {
    if (ch < 0 || ch >= NUM_CHANNELS)
        return;

    // Closed Hi-Hat (7) chokes Open Hi-Hat (8)
    if (ch == 7) {
        voices[8].choke();
    }

    voices[static_cast<size_t>(ch)].trigger(velocity, accentLevel * 0.5f, isFlam);
}

bool TR8SEngine::loadSampleForChannel(int ch, const juce::File& file) {
    if (ch < 0 || ch >= NUM_CHANNELS)
        return false;
    return voices[static_cast<size_t>(ch)].loadCustomSample(file, formatManager);
}

void TR8SEngine::advanceSequencerStep(int stepIdx) {
    int v = fillInActive ? 7 : currentVariation; // Fill-In uses Variation H or dynamic roll
    const auto& accCell = patternGrid[v][NUM_CHANNELS][stepIdx];
    float stepAccent = (accCell.velocity > 85) ? accentLevel : (accCell.velocity > 0 ? accentLevel * 0.35f : 0.0f);

    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
        if (!isChannelAudible(ch))
            continue;

        const auto& cell = patternGrid[v][ch][stepIdx];
        uint8_t velByte = cell.velocity;

        // If Fill-In is active on the last 4 steps, add a classic 909/808 snare/tom fill roll
        if (fillInActive && stepIdx >= 12 && (ch == 1 || ch == 2 || ch == 3 || ch == 4)) {
            if ((stepIdx == 12 && ch == 2) || (stepIdx == 13 && ch == 3) || (stepIdx >= 14 && ch == 1))
                velByte = 105;
        }

        if (velByte > 0) {
            if (ch == 7) {
                voices[8].choke(); // Closed HH chokes Open HH
            }
            float normVel = juce::jlimit(0.2f, 1.2f, static_cast<float>(velByte) / 95.0f);
            bool isFlam = ((cell.flags & 0x0F) != 0) || (fillInActive && stepIdx == 15 && ch == 1);
            voices[static_cast<size_t>(ch)].trigger(normVel, stepAccent, isFlam);
        }
    }

    if (fillInActive && stepIdx == 15) {
        fillInActive = false; // Auto-reset fill at end of bar
    }
}

void TR8SEngine::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages,
                              bool hostIsPlaying, double hostBpm, double /*hostPpqPosition*/) {
    buffer.clear();
    if (hostBpm >= 40.0 && hostBpm <= 300.0 && hostIsPlaying) {
        setTempoBpm(hostBpm);
    }

    // Process incoming MIDI notes (GM Drum Map + Chromatic C1..B1)
    for (const auto metadata : midiMessages) {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn()) {
            int note = msg.getNoteNumber();
            float vel = msg.getFloatVelocity();
            int ch = -1;
            switch (note) {
                case 35: case 36: ch = 0; break; // BD
                case 38: case 40: ch = 1; break; // SD
                case 41: case 43: ch = 2; break; // LT
                case 45: case 47: ch = 3; break; // MT
                case 48: case 50: ch = 4; break; // HT
                case 37: case 75: ch = 5; break; // RS
                case 39:          ch = 6; break; // HC
                case 42: case 44: ch = 7; break; // CH
                case 46:          ch = 8; break; // OH
                case 49: case 57: ch = 9; break; // CC
                case 51: case 56: ch = 10; break; // RC
                default:
                    if (note >= 60 && note < 60 + NUM_CHANNELS)
                        ch = note - 60;
                    break;
            }
            if (ch >= 0 && ch < NUM_CHANNELS && isChannelAudible(ch)) {
                triggerChannel(ch, vel, false);
            }
        }
    }

    int numSamples = buffer.getNumSamples();
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outL;

    bool running = internalPlaying.load(std::memory_order_relaxed) || hostIsPlaying;
    double baseSamplesPerStep = (60.0 / tempoBpm) * 0.25 * sampleRate;

    int sWrite = scopeWriteIdx.load(std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i) {
        if (running) {
            int curStep = playheadStep.load(std::memory_order_relaxed);
            double swingFactor = (curStep & 1) ? (1.0 - shuffleParam * 0.45) : (1.0 + shuffleParam * 0.45);
            double stepLen = std::max(256.0, baseSamplesPerStep * swingFactor);

            sampleCounterInStep += 1.0;
            if (sampleCounterInStep >= stepLen) {
                sampleCounterInStep -= stepLen;
                curStep = (curStep + 1) % NUM_STEPS;
                playheadStep.store(curStep, std::memory_order_relaxed);
                advanceSequencerStep(curStep);
            }
        }

        float mixL = 0.0f, mixR = 0.0f;
        float revL = 0.0f, revR = 0.0f;
        float dlyL = 0.0f, dlyR = 0.0f;

        for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
            if (!voices[static_cast<size_t>(ch)].isActive())
                continue;

            float chL = 0.0f, chR = 0.0f;
            voices[static_cast<size_t>(ch)].renderNextSample(chL, chR);

            if (isChannelAudible(ch)) {
                mixL += chL;
                mixR += chR;
                revL += chL * channelRevSend[static_cast<size_t>(ch)];
                revR += chR * channelRevSend[static_cast<size_t>(ch)];
                dlyL += chL * channelDlySend[static_cast<size_t>(ch)];
                dlyR += chR * channelDlySend[static_cast<size_t>(ch)];
            }
        }

        int curStep = playheadStep.load(std::memory_order_relaxed);
        masterBus.processFrame(mixL, mixR, revL, revR, dlyL, dlyR, curStep);

        outL[i] = mixL;
        if (buffer.getNumChannels() > 1)
            outR[i] = mixR;

        if ((i & 15) == 0) {
            scopeRing[static_cast<size_t>(sWrite)] = 0.5f * (mixL + mixR);
            sWrite = (sWrite + 1) & 127;
        }
    }

    scopeWriteIdx.store(sWrite, std::memory_order_relaxed);
}

void TR8SEngine::getScopeSnapshot(std::array<float, 128>& dest) const {
    int w = scopeWriteIdx.load(std::memory_order_relaxed);
    for (int i = 0; i < 128; ++i) {
        dest[static_cast<size_t>(i)] = scopeRing[static_cast<size_t>((w + i) & 127)];
    }
}

} // namespace ReplicantTR8S
