#include "ACBDrumVoice.h"
#include <algorithm>
#include <cmath>

namespace ReplicantTR8S {

static constexpr float TWO_PI = 6.28318530717958647692f;
static constexpr float PI     = 3.14159265358979323846f;

ACBDrumVoice::ACBDrumVoice() {
    waveformPreview.resize(128, 0.0f);
}

void ACBDrumVoice::prepare(double newSampleRate) {
    sampleRate = std::max(8000.0, newSampleRate);
    reset();
    updateSynthWaveformPreview();
}

void ACBDrumVoice::reset() {
    active = false;
    envTime = 0.0f;
    phase1 = phase2 = phase3 = phase4 = 0.0f;
    metalPhases.fill(0.0f);
    svfLp1 = svfBp1 = svfLp2 = svfBp2 = svfLp3 = svfBp3 = 0.0f;
    snhPhase = snhHeld = 0.0f;
    dcBlockX = dcBlockY = 0.0f;
    chokeGain = 1.0f;
    choking = false;
    flamPending = false;
}

void ACBDrumVoice::setToneId(uint16_t id) {
    uint16_t clamped = std::min<uint16_t>(id, TR8SFirmware::NUM_TONES - 1);
    if (currentToneId != clamped) {
        currentToneId = clamped;
        if (!useCustomSample)
            updateSynthWaveformPreview();
    }
}

float ACBDrumVoice::nextWhiteNoise() {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return static_cast<float>(static_cast<int32_t>(rngState)) * (1.0f / 2147483648.0f);
}

void ACBDrumVoice::trigger(float velocity, float accentAmount, bool isFlam) {
    float boostedVel = juce::jlimit(0.05f, 1.45f, velocity * (1.0f + accentAmount * 0.45f));
    currentVelocity = boostedVel;
    envTime = 0.0f;
    phase1 = 0.0f;
    phase2 = 0.0f;
    phase3 = 0.0f;
    phase4 = 0.0f;
    svfLp1 = svfBp1 = svfLp2 = svfBp2 = svfLp3 = svfBp3 = 0.0f;
    snhPhase = 1.0f;
    snhHeld = 0.0f;
    chokeGain = 1.0f;
    choking = false;
    active = true;

    if (useCustomSample && customSampleLength > 0) {
        samplePlayhead = sampleReverse ? static_cast<double>(customSampleLength - 1) : 0.0;
    }

    if (isFlam) {
        flamPending = true;
        flamCountdownSamples = static_cast<int>(sampleRate * 0.024); // 24ms classic TR flam
        flamVelocity = std::min(1.45f, boostedVel * 1.12f);
    } else {
        flamPending = false;
    }

    recentPeak.store(std::min(1.0f, boostedVel), std::memory_order_relaxed);
}

void ACBDrumVoice::choke() {
    if (active) {
        choking = true;
    }
}

void ACBDrumVoice::renderNextSample(float& outL, float& outR) {
    if (flamPending) {
        if (--flamCountdownSamples <= 0) {
            flamPending = false;
            trigger(flamVelocity, 0.0f, false);
        }
    }

    if (!active) {
        outL = 0.0f;
        outR = 0.0f;
        return;
    }

    float mono = 0.0f;
    if (useCustomSample && customSampleLength > 0) {
        mono = renderUserSample();
    } else {
        mono = synthesizeSample();
    }

    if (choking) {
        chokeGain *= 0.962f; // ~2.8ms smooth analog VCA choke
        mono *= chokeGain;
        if (chokeGain < 0.0008f) {
            active = false;
            choking = false;
        }
    }

    // 18Hz High-Pass DC blocker to keep sub transients symmetric and clean
    float dcCoeff = std::max(0.985f, 1.0f - (113.0f / static_cast<float>(sampleRate)));
    float dcOut = mono - dcBlockX + dcCoeff * dcBlockY;
    dcBlockX = mono;
    dcBlockY = dcOut;
    mono = dcOut;

    mono *= currentVelocity * (levelParam * MAX_MIXER_GAIN);
    // Warm analog output transformer saturation
    mono = std::tanh(mono * 1.18f);

    float panAngle = juce::jlimit(0.0f, 1.0f, panParam) * (PI * 0.5f);
    outL = mono * std::cos(panAngle);
    outR = mono * std::sin(panAngle);

    envTime += 1.0f / static_cast<float>(sampleRate);
    if (envTime > 4.5f) {
        active = false;
    }
}

float ACBDrumVoice::synthesizeSample() {
    const auto& tone = TR8SFirmware::FACTORY_TONES[currentToneId];
    int cat = tone.category;
    uint16_t id = tone.id;

    // Dispatch based on exact TR-8S v3.00 firmware tone ranges
    if (id <= 25 || (id >= 81 && id <= 83) || id == 158) {
        return render808Model(cat);
    } else if ((id >= 26 && id <= 41) || id == 84 || id == 85 || id == 157) {
        return render909Model(cat);
    } else if (id >= 42 && id <= 56) {
        return render707Or626Model(cat, true);
    } else if (id >= 57 && id <= 71) {
        return render727Model(cat);
    } else if (id >= 72 && id <= 80) {
        return render606Model(cat);
    } else if (id >= 86 && id <= 156) {
        return renderFMModel(cat);
    } else if (id >= 159 && id <= 173) {
        return renderCR78Model(cat);
    } else if (id >= 181 && id <= 354) {
        return render707Or626Model(cat, false);
    }
    return renderPCMOrSynthModel(cat);
}

// ==============================================================================
// TR-808 ACB CIRCUIT MODELS (CCRMA / SERVICE MANUAL COMPONENT MODELING)
// ==============================================================================
float ACBDrumVoice::render808Model(int cat) {
    float tuneRatio = std::pow(2.0f, (tuneParam - 0.5f) * 1.4f);
    float decayScale = 0.22f + decayParam * 2.25f;
    float dt = 1.0f / static_cast<float>(sampleRate);

    // 1. 808 BASS DRUM (Bridged-T network with pulse shaper, dual pitch drop & sub sigh)
    if (cat == 1 || currentToneId == 158) {
        bool isLong = (currentToneId == 2 || currentToneId == 3 || currentToneId == 158);
        bool isBass2 = (currentToneId == 1 || currentToneId == 3);
        float baseFreq = (currentToneId == 158 ? 55.0f : (isBass2 ? 49.0f : 46.5f)) * tuneRatio;
        float decayTime = (isLong ? 1.25f : 0.48f) * decayScale;

        // Bridged-T dual pitch envelope: fast head flex (4.5ms) + membrane punch (30ms) + sub sigh
        float fastEnv = std::exp(-envTime * 215.0f);
        float punchEnv = std::exp(-envTime * 34.0f);
        float sighDrift = std::exp(-envTime * 3.2f);
        float attackDepth = (isBass2 ? 2.1f : 1.45f) + ctrlParam * 2.2f;
        float freq = baseFreq * (0.95f + 0.08f * sighDrift + 0.42f * punchEnv + attackDepth * fastEnv);

        phase1 += freq * dt;
        if (phase1 >= 1.0f) phase1 -= 1.0f;

        float ampEnv = std::exp(-envTime / std::max(0.045f, decayTime));
        if (ampEnv < 0.0004f) { active = false; return 0.0f; }

        // Bridged-T op-amp saturation asymmetry (subtle 2nd & 3rd harmonics during initial punch)
        float harmEnv = 0.07f + 0.12f * punchEnv;
        float body = std::sin(phase1 * TWO_PI)
                   + harmEnv * std::sin(phase1 * TWO_PI * 2.0f)
                   - 0.025f * std::cos(phase1 * TWO_PI * 3.0f);

        // Bipolar trigger pulse click + low-passed wood knock (no unipolar DC spike)
        float pulseClick = std::sin(envTime * TWO_PI * 1180.0f) * std::exp(-envTime * 680.0f);
        float woodNoise = nextWhiteNoise() * std::exp(-envTime * 520.0f);
        svfLp1 += 0.14f * (woodNoise - svfLp1);
        float click = (pulseClick * 0.48f + svfLp1 * 0.55f) * (0.22f + ctrlParam * 0.95f);

        return std::tanh((body * ampEnv + click) * 1.28f);
    }

    // 2. 808 SNARE DRUM (Dual Bridged-T 182Hz/332Hz shells + 2.6kHz High-Pass Snappy Wires)
    if (cat == 2) {
        bool isSnare2 = (currentToneId == 5);
        float headSweep = 1.0f + 0.36f * std::exp(-envTime * 135.0f);
        float f1 = (isSnare2 ? 188.0f : 180.0f) * tuneRatio * headSweep;
        float f2 = (isSnare2 ? 342.0f : 330.0f) * tuneRatio * headSweep;
        phase1 += f1 * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += f2 * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;

        // Lower shell rings slightly longer than upper shell, matching 808 circuit
        float toneEnv1 = std::exp(-envTime * (16.5f / decayScale));
        float toneEnv2 = std::exp(-envTime * (23.0f / decayScale));
        float wireEnv  = 0.35f * std::exp(-envTime * 52.0f) + 0.65f * std::exp(-envTime * (10.2f / decayScale));
        if (toneEnv1 < 0.0004f && wireEnv < 0.0004f) { active = false; return 0.0f; }

        float shells = std::sin(phase1 * TWO_PI) * toneEnv1 * 0.64f
                     + std::sin(phase2 * TWO_PI) * toneEnv2 * 0.42f;

        // High-pass + resonant bandpass @ 2.7kHz for crisp 808 snare wires without low-mid boxiness
        float rawNoise = nextWhiteNoise();
        float fSvf = 2.0f * std::sin(PI * std::min(0.22f, (2650.0f * tuneRatio) / static_cast<float>(sampleRate)));
        float hp = rawNoise - svfLp1 - 0.42f * svfBp1;
        svfBp1 += fSvf * hp;
        svfLp1 += fSvf * svfBp1;

        float snappy = (hp * 0.68f + svfBp1 * 0.52f) * wireEnv * (0.22f + ctrlParam * 1.25f);
        float stickClick = std::sin(envTime * TWO_PI * 920.0f) * std::exp(-envTime * 580.0f) * 0.22f;
        return std::tanh((shells * 0.88f + snappy * 0.78f + stickClick) * 1.22f);
    }

    // 3. 808 TOMS (id 6..8, 12..14) & 808 CONGAS (id 9..11, 81..83)
    if (cat == 3 || cat == 8) {
        bool isConga = (cat == 8 || (currentToneId >= 9 && currentToneId <= 11) || (currentToneId >= 81 && currentToneId <= 83));
        bool isNoiseTom = (currentToneId >= 12 && currentToneId <= 14);

        float baseFreq = 138.0f;
        if (currentToneId == 6 || currentToneId == 12) baseFreq = 94.0f;
        else if (currentToneId == 7 || currentToneId == 13) baseFreq = 140.0f;
        else if (currentToneId == 8 || currentToneId == 14) baseFreq = 202.0f;
        else if (currentToneId == 9 || currentToneId == 81) baseFreq = 168.0f;
        else if (currentToneId == 10 || currentToneId == 82) baseFreq = 248.0f;
        else if (currentToneId == 11 || currentToneId == 83) baseFreq = 362.0f;
        else if (channelIndex == 2) baseFreq = isConga ? 168.0f : 94.0f;
        else if (channelIndex == 3) baseFreq = isConga ? 248.0f : 140.0f;
        else if (channelIndex == 4) baseFreq = isConga ? 362.0f : 202.0f;

        baseFreq *= tuneRatio;
        float pitchSweep = isConga
            ? (1.0f + 0.32f * std::exp(-envTime * 54.0f))
            : (1.0f + 0.54f * std::exp(-envTime * 31.0f));
        phase1 += (baseFreq * pitchSweep) * dt;
        if (phase1 >= 1.0f) phase1 -= 1.0f;

        float decayRate = (isConga ? 9.8f : 6.2f) / decayScale;
        float ampEnv = std::exp(-envTime * decayRate);
        if (ampEnv < 0.0004f) { active = false; return 0.0f; }

        float body = std::sin(phase1 * TWO_PI) + (isConga ? 0.11f : 0.05f) * std::sin(phase1 * TWO_PI * 2.0f);

        // Filtered pink/membrane noise (LPF @ 580Hz) for 808 Tom thud instead of raw white noise
        float rawN = nextWhiteNoise();
        float cut = std::min(0.25f, 580.0f / static_cast<float>(sampleRate));
        svfLp1 += cut * (rawN - svfLp1);
        float noiseAmt = isConga ? (0.06f + ctrlParam * 0.15f) : ((isNoiseTom ? 0.35f : 0.14f) + ctrlParam * 0.38f);
        float membraneThud = svfLp1 * std::exp(-envTime * (isNoiseTom ? 26.0f : 58.0f)) * noiseAmt;

        return std::tanh((body * ampEnv + membraneThud) * 1.15f);
    }

    // 4. 808 RIM SHOT (cat 4, id 15) & 808 CLAVES (cat 9, id 16)
    if (cat == 4 || cat == 9) {
        if (cat == 9 || currentToneId == 16) {
            // 808 Claves: Dual coupled Bridged-T (2480Hz primary + 1180Hz hollow rosewood cavity)
            phase1 += 2480.0f * tuneRatio * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
            phase2 += 1180.0f * tuneRatio * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
            float env1 = std::exp(-envTime * (42.0f / decayScale));
            float env2 = std::exp(-envTime * 115.0f);
            if (env1 < 0.0004f) { active = false; return 0.0f; }
            float clave = std::sin(phase1 * TWO_PI) * env1 * 0.85f + std::sin(phase2 * TWO_PI) * env2 * 0.28f;
            return std::tanh(clave * 1.25f);
        }
        // 808 Rim Shot: Dual Bridged-T resonators (485Hz wood body + 1690Hz rim clack) with fast transient clip
        float snap = 1.0f + 0.22f * std::exp(-envTime * 450.0f);
        phase1 += 485.0f * tuneRatio * snap * dt;  if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += 1690.0f * tuneRatio * snap * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
        float envBody = std::exp(-envTime * (82.0f / decayScale));
        float envRim  = std::exp(-envTime * (155.0f / decayScale));
        if (envBody < 0.0004f) { active = false; return 0.0f; }
        float click = std::sin(envTime * TWO_PI * 2200.0f) * std::exp(-envTime * 750.0f) * 0.45f;
        float sig = std::sin(phase1 * TWO_PI) * envBody * 0.68f
                  + std::sin(phase2 * TWO_PI) * envRim * 0.92f
                  + click;
        return std::tanh(sig * (1.9f + ctrlParam * 1.4f));
    }

    // 5. 808 HAND CLAP (cat 5, id 17, 19, 20) & 808 MARACAS (cat 10, id 18)
    if (cat == 5 || cat == 10) {
        if (cat == 10 || currentToneId == 18) {
            // 808 Maracas: Brisk 4ms shaker attack + 5.5kHz high-passed white noise
            float attack = 1.0f - std::exp(-envTime * 280.0f);
            float env = attack * std::exp(-envTime * (38.0f / decayScale));
            if (envTime > 0.015f && env < 0.0004f) { active = false; return 0.0f; }
            float n = nextWhiteNoise();
            float f = 2.0f * std::sin(PI * std::min(0.22f, (5500.0f * tuneRatio) / static_cast<float>(sampleRate)));
            float hp = n - svfLp1 - 0.5f * svfBp1;
            svfBp1 += f * hp;
            svfLp1 += f * svfBp1;
            return hp * env * 0.95f;
        }
        if (currentToneId == 19) {
            // 808 Finger Snap: Crisp single resonant pop (1.15kHz + 2.35kHz)
            float snapHit = (envTime < 0.002f) ? 0.4f : 1.0f;
            phase1 += 1150.0f * tuneRatio * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
            phase2 += 2350.0f * tuneRatio * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
            float env = std::exp(-envTime * (68.0f / decayScale));
            if (env < 0.0004f) { active = false; return 0.0f; }
            float n = nextWhiteNoise() * std::exp(-envTime * 190.0f) * 0.45f;
            return std::tanh(((std::sin(phase1 * TWO_PI) * 0.65f + std::sin(phase2 * TWO_PI) * 0.45f) * env + n) * snapHit * 1.4f);
        }

        // 808 Hand Clap: 4 sawtooth-exponential micro-bursts (10.8ms apart) + independent room tail VCA
        constexpr float burstSpacing = 0.0108f;
        constexpr float burstTotal   = burstSpacing * 4.0f; // 43.2ms (4 distinct claps)
        float strikeEnv = 0.0f;
        if (envTime < burstTotal) {
            float toothPhase = std::fmod(envTime, burstSpacing) / burstSpacing;
            strikeEnv = std::exp(-toothPhase * 4.3f);
        } else {
            strikeEnv = std::exp(-(envTime - burstTotal) * 28.0f);
        }
        float tailEnv = (0.36f + ctrlParam * 0.34f) * std::exp(-envTime * (6.2f / decayScale));
        float totalEnv = std::max(strikeEnv, tailEnv);
        if (envTime > burstTotal && totalEnv < 0.0004f) { active = false; return 0.0f; }

        float n = nextWhiteNoise();
        // Filter 1: 1080Hz resonant palm-cavity bandpass (Q ~ 5.5)
        float f1 = 2.0f * std::sin(PI * std::min(0.20f, (1080.0f * tuneRatio) / static_cast<float>(sampleRate)));
        float hp1 = n - svfLp1 - 0.18f * svfBp1;
        svfBp1 += f1 * hp1;
        svfLp1 += f1 * svfBp1;

        // Filter 2: 3100Hz crisp skin-snap bandpass weighted on the 4 strikes
        float f2 = 2.0f * std::sin(PI * std::min(0.22f, (3100.0f * tuneRatio) / static_cast<float>(sampleRate)));
        float hp2 = n - svfLp2 - 0.32f * svfBp2;
        svfBp2 += f2 * hp2;
        svfLp2 += f2 * svfBp2;

        float clapOut = svfBp1 * (strikeEnv * 1.15f + tailEnv * 0.95f)
                      + svfBp2 * (strikeEnv * 0.72f + tailEnv * 0.32f);
        return std::tanh(clapOut * 1.65f);
    }

    // 6. 808 COWBELL (cat 11, id 25: 540Hz + 800Hz Schmitt-trigger pulse oscillators + 680Hz resonant BPF)
    if (cat == 11 || currentToneId == 25) {
        phase1 += 540.0f * tuneRatio * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += 800.0f * tuneRatio * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
        // 48% duty cycle pulse waves (exact HD14584 Schmitt trigger behavior)
        float p1 = (phase1 < 0.48f) ? 1.0f : -0.92f;
        float p2 = (phase2 < 0.48f) ? 1.0f : -0.92f;

        // Two-stage VCA envelope: sharp initial stick strike + metallic cowbell ring
        float envStrike = 0.72f * std::exp(-envTime * 42.0f);
        float envRing   = 0.34f * std::exp(-envTime * (5.2f / decayScale));
        float env = envStrike + envRing;
        if (env < 0.0004f) { active = false; return 0.0f; }

        float raw = p1 * 0.58f + p2 * 0.42f;
        float f = 2.0f * std::sin(PI * std::min(0.20f, (680.0f * tuneRatio) / static_cast<float>(sampleRate)));
        float hp = raw - svfLp1 - 0.26f * svfBp1;
        svfBp1 += f * hp;
        svfLp1 += f * svfBp1;

        float out = (svfBp1 * 1.15f + raw * 0.22f) * env;
        return std::tanh(out * (1.35f + ctrlParam * 0.45f));
    }

    // 7. 808 HI-HATS (cat 6: id 21 CH, id 22 OH) & 808 CYMBAL (cat 7: id 23, 24)
    // Exact CCRMA 6-Oscillator Schmitt-Trigger Bank (205.3, 304.4, 369.6, 522.7, 540.0, 800.0 Hz)
    static constexpr float freqs808[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
    float metalSum = 0.0f;
    for (size_t i = 0; i < 6; ++i) {
        metalPhases[i] += (freqs808[i] * tuneRatio) * dt;
        if (metalPhases[i] >= 1.0f) metalPhases[i] -= 1.0f;
        // 48% duty cycle square wave produces both odd and even metallic partials
        metalSum += (metalPhases[i] < 0.48f) ? 1.0f : -0.92f;
    }
    // Non-linear diode intermodulation across the 6 square waves creates dense 3kHz..12kHz metallic cluster
    float intermod = std::tanh(metalSum * 0.65f) - metalSum * 0.25f;

    // Bandpass 1 @ 3440Hz (CCRMA lower cymbal/hat resonator)
    float fBp1 = 2.0f * std::sin(PI * std::min(0.21f, (3440.0f * tuneRatio) / static_cast<float>(sampleRate)));
    float hp1 = intermod - svfLp1 - 0.28f * svfBp1;
    svfBp1 += fBp1 * hp1;
    svfLp1 += fBp1 * svfBp1;

    // Bandpass 2 / Highpass @ 7100Hz (CCRMA upper sizzle network)
    float fBp2 = 2.0f * std::sin(PI * std::min(0.22f, (7100.0f * tuneRatio) / static_cast<float>(sampleRate)));
    float hp2 = hp1 - svfLp2 - 0.32f * svfBp2;
    svfBp2 += fBp2 * hp2;
    svfLp2 += fBp2 * svfBp2;

    bool isCymbal = (cat == 7 || currentToneId == 23 || currentToneId == 24 || channelIndex >= 9);
    bool isOpen   = (!isCymbal) && (channelIndex == 8 || currentToneId == 22);

    if (isCymbal) {
        // 808 Cymbal: Parallel dual-VCA branches (fast 3.44kHz crash wash + swelling 7.1kHz shimmer tail)
        float envLow  = std::exp(-envTime * (6.2f / decayScale));
        float swell   = 1.0f - std::exp(-envTime * 95.0f);
        float tailRate = (currentToneId == 24 ? 3.6f : 1.45f) / decayScale;
        float envHigh = swell * std::exp(-envTime * tailRate);
        if (envLow < 0.0004f && envHigh < 0.0004f) { active = false; return 0.0f; }

        float toneBal = 0.35f + ctrlParam * 0.65f;
        float cym = svfBp1 * envLow * (1.15f - toneBal * 0.45f)
                  + (hp2 * 0.85f + svfBp2 * 0.65f) * envHigh * (0.85f + toneBal * 0.65f);
        return std::tanh(cym * 1.45f);
    }

    // 808 Closed / Open Hi-Hat
    float decayRate = isOpen ? (4.2f / decayScale) : (28.0f / decayScale);
    float env = std::exp(-envTime * decayRate);
    if (env < 0.0004f) { active = false; return 0.0f; }

    float hh = (hp2 * 0.92f + svfBp2 * 0.68f + svfBp1 * 0.25f) * env * (0.95f + ctrlParam * 0.45f);
    return std::tanh(hh * 1.55f);
}

// ==============================================================================
// TR-909 ACB CIRCUIT & 6-BIT 32kHz EPROM MODELS
// ==============================================================================
float ACBDrumVoice::render909Model(int cat) {
    float tuneRatio = std::pow(2.0f, (tuneParam - 0.5f) * 1.4f);
    float decayScale = 0.22f + decayParam * 2.15f;
    float dt = 1.0f / static_cast<float>(sampleRate);

    // 1. 909 BASS DRUM (Triangle-to-Diode-Sine Shaper + Dual Pitch Sweep + Bipolar Attack Click)
    if (cat == 1 || currentToneId == 157) {
        bool isLowBass = (currentToneId == 29);
        bool isAtkBass = (currentToneId == 28);
        bool isBass2   = (currentToneId == 27 || isAtkBass);

        float baseFreq = (currentToneId == 157 ? 55.0f : (isLowBass ? 46.0f : 51.5f)) * tuneRatio;
        // Fast upper-bass chest punch (9.5ms) + mid-bass body sweep (42ms)
        float fastEnv = std::exp(-envTime * 108.0f);
        float medEnv  = std::exp(-envTime * 24.5f);
        float freq = baseFreq + ((isBass2 ? 225.0f : 180.0f) * fastEnv + 52.0f * medEnv) * tuneRatio;

        phase1 += freq * dt;
        if (phase1 >= 1.0f) phase1 -= 1.0f;

        // Triangle VCO core shaped through diode sine shaper (richer odd harmonics during initial transient)
        float tri = 4.0f * std::abs(phase1 - 0.5f) - 1.0f;
        float diodeDrive = 1.5707963f * (1.0f + 0.35f * fastEnv);
        float shapedSine = std::sin(juce::jlimit(-1.5707963f, 1.5707963f, tri * diodeDrive));

        float ampEnv = std::exp(-envTime * (2.65f / decayScale));
        if (ampEnv < 0.0004f) { active = false; return 0.0f; }

        // 909 Attack Circuit: Bipolar 1.8ms click pulse + 1.6kHz low-passed LFSR noise burst (controlled by CTRL)
        float atkScale = (isAtkBass ? 1.38f : 1.0f) * (0.18f + ctrlParam * 1.25f);
        float pulseClick = std::sin(envTime * TWO_PI * 1380.0f) * std::exp(-envTime * 560.0f);
        float rawNoise = nextWhiteNoise() * std::exp(-envTime * 380.0f);
        float cut = std::min(0.25f, 1650.0f / static_cast<float>(sampleRate));
        svfLp1 += cut * (rawNoise - svfLp1);
        float click = (pulseClick * 0.65f + svfLp1 * 0.72f) * atkScale;

        return std::tanh((shapedSine * ampEnv + click) * 1.48f);
    }

    // 2. 909 SNARE DRUM (Dual Triangle-Shaped VCOs + Dual-Slope High-Passed Snappy LFSR Noise)
    if (cat == 2) {
        bool isAtkSnare = (currentToneId == 32);
        bool isSnare2   = (currentToneId == 31 || isAtkSnare);

        // 909 Snare head pitch drop (+85% over 12ms gives the fat 200-400Hz analog pop)
        float sweep = 1.0f + (isAtkSnare ? 0.95f : 0.78f) * std::exp(-envTime * 84.0f);
        phase1 += 186.0f * tuneRatio * sweep * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += 344.0f * tuneRatio * sweep * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;

        float bodyEnv1 = std::exp(-envTime * (15.5f / decayScale));
        float bodyEnv2 = std::exp(-envTime * (21.0f / decayScale));
        float snapEnv  = 0.55f * std::exp(-envTime * 26.0f)
                       + 0.45f * std::exp(-envTime * (7.2f / decayScale));
        if (bodyEnv1 < 0.0004f && snapEnv < 0.0004f) { active = false; return 0.0f; }

        // Diode-shaped triangle VCOs for warm 909 shell body
        float tri1 = 4.0f * std::abs(phase1 - 0.5f) - 1.0f;
        float tri2 = 4.0f * std::abs(phase2 - 0.5f) - 1.0f;
        float body = std::sin(tri1 * (PI * 0.5f)) * bodyEnv1 * 0.68f
                   + std::sin(tri2 * (PI * 0.5f)) * bodyEnv2 * (isSnare2 ? 0.46f : 0.36f);

        // High-pass (>1.5kHz) + presence peak (3.4kHz) for crisp, non-boxy 909 snare wires
        float n = nextWhiteNoise();
        float fSvf = 2.0f * std::sin(PI * std::min(0.21f, (2850.0f * tuneRatio) / static_cast<float>(sampleRate)));
        float hp = n - svfLp1 - 0.45f * svfBp1;
        svfBp1 += fSvf * hp;
        svfLp1 += fSvf * svfBp1;

        float snappy = (hp * 0.72f + svfBp1 * 0.58f) * snapEnv * (0.24f + ctrlParam * 1.38f);
        float stick = std::sin(envTime * TWO_PI * 1120.0f) * std::exp(-envTime * 520.0f) * (isAtkSnare ? 0.42f : 0.25f);
        return std::tanh((body * 0.92f + snappy * 0.82f + stick) * 1.32f);
    }

    // 3. 909 TOMS (id 33 Low Tom, id 34 Mid Tom, id 35 High Tom: Dual-Mode Coupled Resonators)
    if (cat == 3) {
        float baseFreq = 132.0f;
        if (currentToneId == 33) baseFreq = 90.0f;
        else if (currentToneId == 34) baseFreq = 132.0f;
        else if (currentToneId == 35) baseFreq = 186.0f;
        else if (channelIndex == 2) baseFreq = 90.0f;
        else if (channelIndex == 3) baseFreq = 132.0f;
        else if (channelIndex == 4) baseFreq = 186.0f;

        baseFreq *= tuneRatio;
        // 909 Tom dual-slope pitch envelope + 1.56x secondary drum-shell mode
        float sweep = 1.0f + 0.82f * std::exp(-envTime * 36.0f) + 0.20f * std::exp(-envTime * 11.0f);
        phase1 += (baseFreq * sweep) * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += (baseFreq * 1.56f * (1.0f + 0.35f * std::exp(-envTime * 48.0f))) * dt;
        if (phase2 >= 1.0f) phase2 -= 1.0f;

        float ampEnv   = std::exp(-envTime * (5.6f / decayScale));
        float shellEnv = std::exp(-envTime * (14.5f / decayScale));
        if (ampEnv < 0.0004f) { active = false; return 0.0f; }

        float tri = 4.0f * std::abs(phase1 - 0.5f) - 1.0f;
        float mainHead = std::sin(tri * (PI * 0.5f)) * ampEnv;
        float shellMode = std::sin(phase2 * TWO_PI) * shellEnv * 0.22f;

        // Low-passed membrane stick impact (@ 920Hz) instead of raw white noise
        float rawClick = nextWhiteNoise() * std::exp(-envTime * 210.0f);
        float cut = std::min(0.24f, 920.0f / static_cast<float>(sampleRate));
        svfLp1 += cut * (rawClick - svfLp1);
        float impact = (svfLp1 * 0.65f + std::sin(envTime * TWO_PI * 680.0f) * std::exp(-envTime * 420.0f) * 0.28f)
                     * (0.18f + ctrlParam * 0.55f);

        return std::tanh((mainHead + shellMode + impact) * 1.22f);
    }

    // 4. 909 RIM SHOT (cat 4, id 36, 84: 3-Mode Bridged-T Pulse Circuit 265Hz / 520Hz / 1640Hz)
    if (cat == 4) {
        float snap = 1.0f + 0.25f * std::exp(-envTime * 480.0f);
        phase1 += 265.0f * tuneRatio * snap * dt;  if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += 520.0f * tuneRatio * snap * dt;  if (phase2 >= 1.0f) phase2 -= 1.0f;
        phase3 += 1640.0f * tuneRatio * snap * dt; if (phase3 >= 1.0f) phase3 -= 1.0f;

        float env1 = std::exp(-envTime * (75.0f / decayScale));
        float env2 = std::exp(-envTime * (105.0f / decayScale));
        float env3 = std::exp(-envTime * (175.0f / decayScale));
        if (env1 < 0.0004f) { active = false; return 0.0f; }

        float click = std::sin(envTime * TWO_PI * 2400.0f) * std::exp(-envTime * 800.0f) * 0.45f;
        float mix = std::sin(phase1 * TWO_PI) * env1 * 0.48f
                  + std::sin(phase2 * TWO_PI) * env2 * 0.68f
                  + std::sin(phase3 * TWO_PI) * env3 * 0.95f
                  + click;
        return std::tanh(mix * (2.2f + ctrlParam * 1.4f));
    }

    // 5. 909 HAND CLAP (cat 5, id 37, 85: 4 Rapid 9.6ms Pulse Bursts + Dual-Band Filter + Room Tail)
    if (cat == 5) {
        constexpr float burstSpacing = 0.0096f;
        constexpr float burstTotal   = burstSpacing * 4.0f; // 38.4ms (4 crisp claps)
        float strikeEnv = 0.0f;
        if (envTime < burstTotal) {
            float b = std::fmod(envTime, burstSpacing) / burstSpacing;
            strikeEnv = std::exp(-b * 3.9f);
        } else {
            strikeEnv = std::exp(-(envTime - burstTotal) * 30.0f);
        }
        float tailEnv = (0.42f + ctrlParam * 0.38f) * std::exp(-envTime * (6.6f / decayScale));
        if (envTime > burstTotal && std::max(strikeEnv, tailEnv) < 0.0004f) { active = false; return 0.0f; }

        float n = nextWhiteNoise();
        // Band 1: 1220Hz resonant palm-slap body
        float f1 = 2.0f * std::sin(PI * std::min(0.20f, (1220.0f * tuneRatio) / static_cast<float>(sampleRate)));
        float hp1 = n - svfLp1 - 0.20f * svfBp1;
        svfBp1 += f1 * hp1;
        svfLp1 += f1 * svfBp1;

        // Band 2: 3500Hz bright 909 clap snap
        float f2 = 2.0f * std::sin(PI * std::min(0.22f, (3500.0f * tuneRatio) / static_cast<float>(sampleRate)));
        float hp2 = n - svfLp2 - 0.32f * svfBp2;
        svfBp2 += f2 * hp2;
        svfLp2 += f2 * svfBp2;

        float clap = svfBp1 * (strikeEnv * 1.2f + tailEnv * 0.95f)
                   + svfBp2 * (strikeEnv * 0.85f + tailEnv * 0.38f);
        return std::tanh(clap * 1.75f);
    }

    // 6. 909 6-BIT 32kHz PAISTE METALLIC HI-HATS (cat 6: id 38 CH, id 39 OH) & CYMBALS (cat 7: id 40 CC, id 41 RC)
    bool isCrash = (currentToneId == 40 || (cat == 7 && channelIndex == 9));
    bool isRide  = (currentToneId == 41 || (cat == 7 && channelIndex == 10));
    bool isOpen  = (!isCrash && !isRide) && (channelIndex == 8 || currentToneId == 39);

    // Paiste 602 inharmonic metallic modal ratios with cross-phase FM modulation
    static constexpr float freqs909[6] = { 412.0f, 587.0f, 811.0f, 1108.0f, 1492.0f, 1978.0f };
    float metal = 0.0f;
    float prevMod = 0.0f;
    for (size_t i = 0; i < 6; ++i) {
        metalPhases[i] += (freqs909[i] * tuneRatio * (isCrash ? 2.65f : (isRide ? 2.85f : 3.25f))) * dt;
        if (metalPhases[i] >= 1.0f) metalPhases[i] -= 1.0f;
        float modSig = std::sin(metalPhases[i] * TWO_PI + prevMod * 1.35f);
        prevMod = modSig;
        metal += modSig;
    }
    metal *= (1.0f / 6.0f);

    // Combine dense metallic FM partials with high-frequency cymbal air
    float n = nextWhiteNoise();
    float rawCymbal = metal * 0.72f + n * (isCrash ? 0.36f : 0.26f);

    // Authentic 909 32kHz EPROM clock Sample-and-Hold + 6-bit (64-step) D/A quantizer
    float epromRate = 32000.0f * tuneRatio;
    snhPhase += epromRate * dt;
    if (snhPhase >= 1.0f) {
        snhPhase -= std::floor(snhPhase);
        snhHeld = std::round(juce::jlimit(-1.0f, 1.0f, rawCymbal) * 32.0f) * (1.0f / 32.0f);
    }

    float decayRate = isCrash ? (1.35f / decayScale)
                    : (isRide ? (1.15f / decayScale)
                    : (isOpen ? (3.6f / decayScale) : (25.0f / decayScale)));
    float env = std::exp(-envTime * decayRate);
    if (env < 0.0004f) { active = false; return 0.0f; }

    // Bandpass 1: Mid-high metallic body (2.4kHz for Crash/Ride, 4.2kHz for Hats)
    float bodyHz = (isCrash ? 2250.0f : (isRide ? 2650.0f : 4200.0f)) * tuneRatio;
    float f1 = 2.0f * std::sin(PI * std::min(0.21f, bodyHz / static_cast<float>(sampleRate)));
    float hp1 = snhHeld - svfLp1 - 0.30f * svfBp1;
    svfBp1 += f1 * hp1;
    svfLp1 += f1 * svfBp1;

    // Bandpass 2 / Highpass: Crisp Paiste sizzle (6.5kHz..8.5kHz)
    float sizzleHz = (isRide ? 5800.0f : 7200.0f) * tuneRatio;
    float f2 = 2.0f * std::sin(PI * std::min(0.22f, sizzleHz / static_cast<float>(sampleRate)));
    float hp2 = hp1 - svfLp2 - 0.35f * svfBp2;
    svfBp2 += f2 * hp2;
    svfLp2 += f2 * svfBp2;

    if (isRide) {
        // 909 Ride Cymbal: Pingy dual-mode cymbal bell (2420Hz + 3780Hz) + metallic bow wash
        phase1 += 2420.0f * tuneRatio * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += 3780.0f * tuneRatio * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
        float bellEnv = std::exp(-envTime * (9.5f / decayScale));
        float pingEnv = std::exp(-envTime * 48.0f);
        float bell = (std::sin(phase1 * TWO_PI) * 0.65f + std::sin(phase2 * TWO_PI) * 0.45f) * bellEnv * 0.38f
                   + svfBp2 * pingEnv * 0.35f;
        float rideOut = (svfBp1 * 0.48f + hp2 * 0.62f + svfBp2 * 0.45f) * env + bell;
        return std::tanh(rideOut * (1.25f + ctrlParam * 0.45f));
    }

    if (isCrash) {
        // 909 Crash Cymbal: Explosive initial splash + wide metallic wash
        float splashEnv = std::exp(-envTime * (5.2f / decayScale));
        float crashOut = svfBp1 * splashEnv * 0.72f + (hp2 * 0.78f + svfBp2 * 0.58f) * env;
        return std::tanh(crashOut * (1.35f + ctrlParam * 0.45f));
    }

    // 909 Closed / Open Hi-Hat with crisp initial Paiste stick chick (6ms)
    float chickEnv = std::exp(-envTime * 165.0f);
    float hhOut = (hp2 * 0.85f + svfBp2 * 0.65f + svfBp1 * (0.32f + chickEnv * 0.45f)) * env;
    return std::tanh(hhOut * (1.38f + ctrlParam * 0.45f));
}

// ==============================================================================
// TR-707 & TR-626 / PCM VINTAGE 12-BIT DRUM MODELS
// ==============================================================================
float ACBDrumVoice::render707Or626Model(int cat, bool is707) {
    float tuneRatio = std::pow(2.0f, (tuneParam - 0.5f) * 1.5f);
    float decayScale = 0.22f + decayParam * 2.15f;
    float dt = 1.0f / static_cast<float>(sampleRate);

    float raw = 0.0f;
    // Special 707 / 626 / PCM percussion categories (Cowbell, Tambourine, Shaker, Agogo, Triangle)
    if (cat == 11) {
        // 707 Cowbell (id 50) / 626 Cowbell, Agogo, Triangle (id 348..354)
        bool isTriangle = (currentToneId == 353 || currentToneId == 354);
        float f1 = (isTriangle ? 3120.0f : 565.0f) * tuneRatio;
        float f2 = (isTriangle ? 4780.0f : 845.0f) * tuneRatio;
        phase1 += f1 * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += f2 * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
        float env = std::exp(-envTime * ((isTriangle ? 3.8f : 11.5f) / decayScale));
        if (env < 0.0004f) { active = false; return 0.0f; }
        raw = (std::sin(phase1 * TWO_PI) * 0.65f + std::sin(phase2 * TWO_PI + 0.4f) * 0.5f) * env;
    } else if (cat == 10) {
        // 707 Tambourine (id 52) / 626 Shaker & Tambourine (id 341..347)
        bool isShaker = (currentToneId >= 341 && currentToneId <= 343);
        float env = isShaker
            ? ((1.0f - std::exp(-envTime * 220.0f)) * std::exp(-envTime * (32.0f / decayScale)))
            : ((0.55f * std::exp(-envTime * 65.0f) + 0.65f * std::exp(-envTime * (12.5f / decayScale))));
        if (envTime > 0.02f && env < 0.0004f) { active = false; return 0.0f; }
        phase1 += 5400.0f * tuneRatio * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += 7850.0f * tuneRatio * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
        float jingle = isShaker ? 0.0f : (std::sin(phase1 * TWO_PI) + std::sin(phase2 * TWO_PI)) * 0.25f;
        float n = nextWhiteNoise();
        float f = 2.0f * std::sin(PI * std::min(0.22f, 5200.0f / static_cast<float>(sampleRate)));
        float hp = n - svfLp1 - 0.4f * svfBp1;
        svfBp1 += f * hp;
        svfLp1 += f * svfBp1;
        raw = (hp * 0.75f + jingle) * env;
    } else {
        // Use 909 acoustic-hybrid core for 707/EDM/Studio PCM and 808/analog core for HipHop/808/Analog PCM
        bool useAnalogCore = (!is707) && (currentToneId == 184 || currentToneId == 185 || currentToneId == 187
                                       || (currentToneId >= 194 && currentToneId <= 197)
                                       || (currentToneId >= 230 && currentToneId <= 234)
                                       || (currentToneId >= 278 && currentToneId <= 280)
                                       || (currentToneId >= 312 && currentToneId <= 315)
                                       || (currentToneId >= 323 && currentToneId <= 340));
        raw = useAnalogCore ? render808Model(cat) : render909Model(cat);
    }

    // Authentic 25kHz (TR-707) / 28kHz (TR-626) PCM Sample-and-Hold + 8-bit/12-bit companding DAC
    float dacClock = (is707 ? 25000.0f : 28000.0f) * tuneRatio;
    snhPhase += dacClock * dt;
    if (snhPhase >= 1.0f) {
        snhPhase -= std::floor(snhPhase);
        float levels = is707 ? 96.0f : 64.0f;
        snhHeld = std::round(raw * levels) / levels;
    }
    return std::tanh(snhHeld * (1.15f + ctrlParam * 0.55f));
}

// ==============================================================================
// TR-727 LATIN PERCUSSION MODELS
// ==============================================================================
float ACBDrumVoice::render727Model(int cat) {
    float tuneRatio = std::pow(2.0f, (tuneParam - 0.5f) * 1.5f);
    float decayScale = 0.25f + decayParam * 2.0f;
    float dt = 1.0f / static_cast<float>(sampleRate);

    // Whistle / Quijada / Star Chime (cat 12)
    if (cat == 12) {
        if (currentToneId == 68 || currentToneId == 69) {
            // 727 Samba Whistle (pea-ball LFO vibrato at 2.8kHz + airy breath turbulence)
            float dur = (currentToneId == 69) ? (0.55f * decayScale) : (0.18f * decayScale);
            if (envTime > dur) { active = false; return 0.0f; }
            float vib = std::sin(envTime * TWO_PI * 29.0f) * 0.038f;
            phase1 += (2780.0f * tuneRatio * (1.0f + vib)) * dt;
            if (phase1 >= 1.0f) phase1 -= 1.0f;
            float env = std::min(1.0f, envTime * 90.0f) * std::exp(-(envTime / dur) * 2.1f);
            float breath = nextWhiteNoise();
            svfLp1 += 0.25f * (breath - svfLp1);
            return (std::sin(phase1 * TWO_PI) * 0.78f + (breath - svfLp1) * 0.16f) * env;
        }
        if (currentToneId == 71) {
            // 727 Star Chime (Cascading bar chimes 2.2kHz..8.5kHz)
            float env = std::exp(-envTime * (1.75f / decayScale));
            if (env < 0.0004f) { active = false; return 0.0f; }
            float chime = 0.0f;
            for (size_t i = 0; i < 6; ++i) {
                float f = (2250.0f + static_cast<float>(i) * 710.0f + std::fmod(envTime * 950.0f, 1350.0f)) * tuneRatio;
                metalPhases[i] += f * dt;
                if (metalPhases[i] >= 1.0f) metalPhases[i] -= 1.0f;
                chime += std::sin(metalPhases[i] * TWO_PI);
            }
            return (chime * 0.17f) * env;
        }
        // 727 Quijada (Jawbone rattle)
        float pulse = (std::fmod(envTime, 0.017f) < 0.0045f) ? 1.0f : 0.15f;
        float env = std::exp(-envTime * (4.5f / decayScale));
        if (env < 0.0004f) { active = false; return 0.0f; }
        phase1 += 1680.0f * tuneRatio * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += 2540.0f * tuneRatio * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
        return (std::sin(phase1 * TWO_PI) * 0.7f + std::sin(phase2 * TWO_PI) * 0.35f) * pulse * env;
    }

    // 727 Cabasa (66) & Maracas (67) (cat 10)
    if (cat == 10 || currentToneId == 66 || currentToneId == 67) {
        bool isCabasa = (currentToneId == 66);
        float attack = 1.0f - std::exp(-envTime * (isCabasa ? 190.0f : 280.0f));
        float env = attack * std::exp(-envTime * ((isCabasa ? 26.0f : 36.0f) / decayScale));
        if (envTime > 0.02f && env < 0.0004f) { active = false; return 0.0f; }
        float n = nextWhiteNoise();
        float f = 2.0f * std::sin(PI * std::min(0.22f, (isCabasa ? 3800.0f : 5600.0f) * tuneRatio / static_cast<float>(sampleRate)));
        float hp = n - svfLp1 - 0.32f * svfBp1;
        svfBp1 += f * hp;
        svfLp1 += f * svfBp1;
        return (isCabasa ? (svfBp1 * 0.8f + hp * 0.5f) : hp) * env * 1.15f;
    }

    // 727 Agogo Bells (cat 11: id 64 High Agogo, id 65 Low Agogo)
    if (cat == 11) {
        float f1 = ((currentToneId == 64) ? 885.0f : 665.0f) * tuneRatio;
        phase1 += f1 * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += (f1 * 1.46f) * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
        phase3 += (f1 * 2.18f) * dt; if (phase3 >= 1.0f) phase3 -= 1.0f;
        float env = std::exp(-envTime * (10.5f / decayScale));
        float clinkEnv = std::exp(-envTime * 85.0f);
        if (env < 0.0004f) { active = false; return 0.0f; }
        return (std::sin(phase1 * TWO_PI) * 0.72f
              + std::sin(phase2 * TWO_PI) * 0.38f
              + std::sin(phase3 * TWO_PI) * clinkEnv * 0.28f) * env;
    }

    // 727 Bongos (57, 58), Congas (59, 60, 61), Timbales (62, 63) (cat 8)
    float baseFreq = 220.0f;
    bool isTimbale = (currentToneId == 62 || currentToneId == 63);
    if (currentToneId == 57) baseFreq = 405.0f;      // Hi Bongo
    else if (currentToneId == 58) baseFreq = 270.0f; // Low Bongo
    else if (currentToneId == 59) baseFreq = 318.0f; // Mute Hi Conga
    else if (currentToneId == 60) baseFreq = 295.0f; // Open Hi Conga
    else if (currentToneId == 61) baseFreq = 196.0f; // Low Conga
    else if (currentToneId == 62) baseFreq = 256.0f; // Hi Timbale
    else if (currentToneId == 63) baseFreq = 182.0f; // Low Timbale

    baseFreq *= tuneRatio;
    float pitchBend = 1.0f + (isTimbale ? 0.42f : 0.32f) * std::exp(-envTime * 48.0f);
    phase1 += baseFreq * pitchBend * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
    phase2 += baseFreq * (isTimbale ? 1.48f : 1.54f) * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
    phase3 += baseFreq * 2.14f * dt; if (phase3 >= 1.0f) phase3 -= 1.0f;

    float decayRate = (currentToneId == 59 ? 25.0f : (isTimbale ? 7.2f : 8.8f)) / decayScale;
    float env = std::exp(-envTime * decayRate);
    if (env < 0.0004f) { active = false; return 0.0f; }

    float rawSlap = nextWhiteNoise() * std::exp(-envTime * (isTimbale ? 95.0f : 130.0f));
    svfLp1 += 0.35f * (rawSlap - svfLp1);
    float slap = (isTimbale ? rawSlap : svfLp1) * (0.24f + ctrlParam * 0.45f);
    float shell = std::sin(phase1 * TWO_PI) * 0.82f
                + std::sin(phase2 * TWO_PI) * (isTimbale ? 0.38f : 0.22f)
                + (isTimbale ? std::sin(phase3 * TWO_PI) * std::exp(-envTime * 28.0f) * 0.24f : 0.0f);
    return std::tanh(shell * env + slap);
}

// ==============================================================================
// TR-606 DRUMATIX TRANSISTOR RHYTHM MODELS (id 72..80)
// ==============================================================================
float ACBDrumVoice::render606Model(int cat) {
    float tuneRatio = std::pow(2.0f, (tuneParam - 0.5f) * 1.5f);
    float decayScale = 0.25f + decayParam * 2.0f;
    float dt = 1.0f / static_cast<float>(sampleRate);

    // 1. 606 Bass Drum (id 72: Compact 58Hz Bridged-T + fast 18ms pitch chirp + transistor pulse)
    if (cat == 1) {
        float f = 58.0f * tuneRatio * (1.0f + 1.55f * std::exp(-envTime * 58.0f));
        phase1 += f * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        float env = std::exp(-envTime * (6.2f / decayScale));
        if (env < 0.0004f) { active = false; return 0.0f; }
        float click = std::sin(envTime * TWO_PI * 1250.0f) * std::exp(-envTime * 480.0f) * (0.32f + ctrlParam * 0.55f);
        return std::tanh((std::sin(phase1 * TWO_PI) * env + click) * 1.35f);
    }

    // 2. 606 Snare Drum (id 73: Dual 238Hz/445Hz resonators + crisp 3.2kHz high-passed wire burst)
    if (cat == 2) {
        float sweep = 1.0f + 0.35f * std::exp(-envTime * 110.0f);
        phase1 += 238.0f * tuneRatio * sweep * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += 445.0f * tuneRatio * sweep * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
        float toneEnv = std::exp(-envTime * (21.0f / decayScale));
        float wireEnv = std::exp(-envTime * (12.5f / decayScale));
        if (toneEnv < 0.0004f && wireEnv < 0.0004f) { active = false; return 0.0f; }

        float body = (std::sin(phase1 * TWO_PI) * 0.62f + std::sin(phase2 * TWO_PI) * 0.38f) * toneEnv;
        float n = nextWhiteNoise();
        float f = 2.0f * std::sin(PI * std::min(0.22f, (3200.0f * tuneRatio) / static_cast<float>(sampleRate)));
        float hp = n - svfLp1 - 0.45f * svfBp1;
        svfBp1 += f * hp;
        svfLp1 += f * svfBp1;
        return std::tanh((body * 0.8f + (hp * 0.7f + svfBp1 * 0.5f) * wireEnv * (0.35f + ctrlParam * 1.15f)) * 1.28f);
    }

    // 3. 606 Hi-Hats (id 77 CH, id 78 OH) & 606 Cymbal (id 79, 80)
    if (cat == 6 || cat == 7) {
        static constexpr float freqs606[6] = { 245.0f, 306.0f, 365.0f, 415.0f, 540.0f, 630.0f };
        float sum = 0.0f;
        for (size_t i = 0; i < 6; ++i) {
            metalPhases[i] += (freqs606[i] * tuneRatio * 1.15f) * dt;
            if (metalPhases[i] >= 1.0f) metalPhases[i] -= 1.0f;
            sum += (metalPhases[i] < 0.48f) ? 1.0f : -0.92f;
        }
        float intermod = std::tanh(sum * 0.7f);
        float f1 = 2.0f * std::sin(PI * std::min(0.21f, (4800.0f * tuneRatio) / static_cast<float>(sampleRate)));
        float hp1 = intermod - svfLp1 - 0.25f * svfBp1;
        svfBp1 += f1 * hp1;
        svfLp1 += f1 * svfBp1;

        bool isCym = (cat == 7 || currentToneId >= 79);
        bool isOpen = (!isCym) && (currentToneId == 78 || channelIndex == 8);
        float decayRate = isCym ? ((currentToneId == 80 ? 3.8f : 1.7f) / decayScale)
                                : (isOpen ? (4.5f / decayScale) : (28.0f / decayScale));
        float env = std::exp(-envTime * decayRate);
        if (env < 0.0004f) { active = false; return 0.0f; }
        return std::tanh((hp1 * 0.85f + svfBp1 * 0.75f) * env * 1.35f);
    }

    return render808Model(cat);
}

// ==============================================================================
// CR-78 COMPURHYTHM (v3.00 FIRMWARE: id 159..173) MODELS
// ==============================================================================
float ACBDrumVoice::renderCR78Model(int cat) {
    float tuneRatio = std::pow(2.0f, (tuneParam - 0.5f) * 1.5f);
    float decayScale = 0.25f + decayParam * 2.0f;
    float dt = 1.0f / static_cast<float>(sampleRate);

    // CR-78 Guiro (171)
    if (currentToneId == 171) {
        float dur = 0.38f * decayScale;
        if (envTime > dur) { active = false; return 0.0f; }
        float ratchet = 0.5f + 0.5f * std::sin(envTime * TWO_PI * 74.0f);
        phase1 += (1450.0f + envTime * 1150.0f) * tuneRatio * dt;
        if (phase1 >= 1.0f) phase1 -= 1.0f;
        return std::sin(phase1 * TWO_PI) * ratchet * (1.0f - envTime / dur) * 0.88f;
    }

    // CR-78 Metal Beat (172) / Tambourine (170) / Cowbell (169)
    if (currentToneId >= 169 && currentToneId <= 172) {
        static constexpr float crMetal[4] = { 1120.0f, 1680.0f, 2490.0f, 3310.0f };
        float sum = 0.0f;
        for (size_t i = 0; i < 4; ++i) {
            metalPhases[i] += crMetal[i] * tuneRatio * dt;
            if (metalPhases[i] >= 1.0f) metalPhases[i] -= 1.0f;
            sum += (metalPhases[i] < 0.5f) ? 1.0f : -1.0f;
        }
        float f = 2.0f * std::sin(PI * std::min(0.22f, (2650.0f * tuneRatio) / static_cast<float>(sampleRate)));
        float hp = sum * 0.25f - svfLp1 - 0.3f * svfBp1;
        svfBp1 += f * hp;
        svfLp1 += f * svfBp1;
        float env = std::exp(-envTime * (12.5f / decayScale));
        if (env < 0.0004f) { active = false; return 0.0f; }
        return (svfBp1 * 0.9f + hp * 0.4f) * env * 1.25f;
    }

    // Warm LC-Tank CR-78 Bass/Snare/Bongo/HiHat
    return render808Model(cat) * 0.94f;
}

// ==============================================================================
// 2-OPERATOR FM DRUM ENGINE WITH MORPH MACRO (v2.00 FIRMWARE: TONES 86..156)
// ==============================================================================
float ACBDrumVoice::renderFMModel(int cat) {
    float tuneRatio = std::pow(2.0f, (tuneParam - 0.5f) * 2.0f);
    float decayScale = 0.2f + decayParam * 2.2f;
    float dt = 1.0f / static_cast<float>(sampleRate);

    float baseCarrier = 110.0f;
    float modRatio = 1.414f + (currentToneId % 7) * 0.37f;
    float modIndexBase = 1.8f + ctrlParam * 6.5f; // CTRL = FM Morph macro
    float pitchSweepAmt = 0.5f;
    float decayRate = 8.0f / decayScale;

    if (cat == 1) { // FM Kicks (86..96)
        baseCarrier = (currentToneId == 91 || currentToneId == 93) ? 45.0f : 52.0f;
        modRatio = 1.0f + (currentToneId % 4) * 0.5f;
        pitchSweepAmt = (currentToneId == 90 || currentToneId == 95) ? 4.2f : 3.2f;
        decayRate = (currentToneId == 91 ? 2.8f : 4.2f) / decayScale;
    } else if (cat == 2) { // FM Snares (97..107)
        baseCarrier = 180.0f;
        modRatio = 2.37f + (currentToneId % 5) * 0.41f;
        pitchSweepAmt = 1.25f;
        decayRate = 10.5f / decayScale;
    } else if (cat == 3 || cat == 8) { // FM Toms / Perc (108..112, 131..134)
        baseCarrier = (channelIndex == 2) ? 95.0f : (channelIndex == 3 ? 140.0f : 195.0f);
        modRatio = 1.732f;
        pitchSweepAmt = 1.45f;
        decayRate = 6.5f / decayScale;
    } else if (cat == 6 || cat == 7) { // FM Hi-Hats / Cymbals (118..130)
        baseCarrier = 720.0f;
        modRatio = 3.14159f;
        modIndexBase = 4.8f + ctrlParam * 8.5f;
        decayRate = (channelIndex == 7 || currentToneId <= 119) ? (24.0f / decayScale) : (3.2f / decayScale);
    } else if (cat == 13) { // FM Riser / Laser / Sci-Fi FX (148..155)
        baseCarrier = (currentToneId == 149) ? (180.0f + envTime * 950.0f) : 420.0f;
        modRatio = 2.618f;
        pitchSweepAmt = (currentToneId == 150) ? 5.5f : -0.6f;
        decayRate = 3.5f / decayScale;
    }

    float pitchEnv = std::max(0.15f, 1.0f + pitchSweepAmt * std::exp(-envTime * 58.0f));
    float cFreq = baseCarrier * tuneRatio * pitchEnv;
    float mFreq = cFreq * modRatio;

    phase2 += mFreq * dt;
    if (phase2 >= 1.0f) phase2 -= 1.0f;

    float modEnv = std::exp(-envTime * (decayRate * 1.75f));
    float modSig = std::sin(phase2 * TWO_PI) * modIndexBase * modEnv;

    phase1 += cFreq * dt;
    if (phase1 >= 1.0f) phase1 -= 1.0f;

    float ampEnv = std::exp(-envTime * decayRate);
    if (ampEnv < 0.0004f) { active = false; return 0.0f; }

    float fmOut = std::sin(phase1 * TWO_PI + modSig) * ampEnv;
    if (cat == 2 || cat == 5) {
        fmOut += nextWhiteNoise() * modEnv * 0.35f;
    }
    float drive = (currentToneId == 95) ? (2.4f + ctrlParam * 2.5f) : 1.32f; // Gabber FM Kick overdrive
    return std::tanh(fmOut * drive);
}

// ==============================================================================
// PRESET SYNTH / BASS / VOICE / OSC / CHORD / FX MODELS (TONES 355..521)
// ==============================================================================
float ACBDrumVoice::renderPCMOrSynthModel(int cat) {
    float tuneRatio = std::pow(2.0f, (tuneParam - 0.5f) * 2.0f);
    float decayScale = 0.25f + decayParam * 2.2f;
    float dt = 1.0f / static_cast<float>(sampleRate);

    // 1. Voice / Formant / Vocoder Tones (cat 14: id 416..434)
    if (cat == 14) {
        float f0 = 130.8f * tuneRatio;
        phase1 += f0 * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        float glottal = (2.0f * phase1 - 1.0f) + 0.5f * std::sin(phase1 * TWO_PI * 2.0f);
        float formant1 = 720.0f + (currentToneId % 5) * 280.0f;
        float formant2 = 1250.0f + (currentToneId % 5) * 420.0f;
        float f1 = 2.0f * std::sin(PI * std::min(0.20f, formant1 / static_cast<float>(sampleRate)));
        float f2 = 2.0f * std::sin(PI * std::min(0.22f, formant2 / static_cast<float>(sampleRate)));
        svfBp1 += f1 * (glottal - svfLp1 - 0.18f * svfBp1); svfLp1 += f1 * svfBp1;
        svfBp2 += f2 * (glottal - svfLp2 - 0.22f * svfBp2); svfLp2 += f2 * svfBp2;
        float env = std::exp(-envTime * (4.8f / decayScale));
        if (env < 0.0004f) { active = false; return 0.0f; }
        return std::tanh((svfBp1 * 0.7f + svfBp2 * 0.5f) * env * 1.4f);
    }

    // 2. Raw Oscillator Waveforms (cat 16: id 451..475 Sine, Tri, Square, Pulse, SuperSaw, White Noise)
    if (cat == 16) {
        float env = std::exp(-envTime * (4.2f / decayScale));
        if (env < 0.0004f) { active = false; return 0.0f; }
        if (currentToneId >= 473) {
            return nextWhiteNoise() * env * 0.75f;
        }
        bool isHigh = ((currentToneId & 1) == 0);
        float f0 = (isHigh ? 220.0f : 110.0f) * tuneRatio;
        phase1 += f0 * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += (f0 * 1.012f) * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
        phase3 += (f0 * 1.4983f) * dt; if (phase3 >= 1.0f) phase3 -= 1.0f;

        float sig = 0.0f;
        if (currentToneId <= 454) sig = std::sin(phase1 * TWO_PI);
        else if (currentToneId <= 458) sig = 4.0f * std::abs(phase1 - 0.5f) - 1.0f;
        else if (currentToneId <= 464) sig = (phase1 < (0.25f + ctrlParam * 0.5f)) ? 0.85f : -0.85f;
        else sig = ((2.0f * phase1 - 1.0f) + (2.0f * phase2 - 1.0f) + (2.0f * phase3 - 1.0f)) * 0.35f;
        return std::tanh(sig * env * 1.2f);
    }

    // 3. Chords (cat 19), Leads/Pads (cat 15), Basses (cat 17), Mallets/Pianos (cat 18), FX (cat 12, 13)
    float baseFreq = (cat == 17) ? 55.0f : (cat == 18 ? 261.63f : 220.0f);
    baseFreq *= tuneRatio;

    bool isMajor = (currentToneId == 506 || currentToneId == 508 || currentToneId == 509 || currentToneId == 517 || currentToneId == 520);
    float thirdRatio = isMajor ? 1.2599f : 1.1892f;
    phase1 += baseFreq * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
    phase2 += (baseFreq * thirdRatio) * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
    phase3 += (baseFreq * 1.4983f) * dt; if (phase3 >= 1.0f) phase3 -= 1.0f;
    phase4 += (baseFreq * 1.7818f) * dt; if (phase4 >= 1.0f) phase4 -= 1.0f; // 7th

    float env = std::exp(-envTime * ((cat == 17 ? 3.2f : 4.2f) / decayScale));
    if (env < 0.0004f) { active = false; return 0.0f; }

    float saw1 = 2.0f * phase1 - 1.0f;
    float saw2 = 2.0f * phase2 - 1.0f;
    float saw3 = 2.0f * phase3 - 1.0f;
    float saw4 = 2.0f * phase4 - 1.0f;
    float raw = (cat == 19)
        ? ((saw1 + saw2 + saw3 + saw4 * 0.7f) * 0.28f)
        : (std::sin(phase1 * TWO_PI) * 0.72f + saw1 * 0.32f);

    float cutoff = std::min(0.42f, (380.0f + ctrlParam * 4400.0f * env) / static_cast<float>(sampleRate));
    svfLp1 += cutoff * (raw - svfLp1);
    return std::tanh(svfLp1 * env * 1.38f);
}

// ==============================================================================
// CUSTOM USER SAMPLE ENGINE (WAV / AIFF / FLAC / MP3 / OGG)
// ==============================================================================
bool ACBDrumVoice::loadCustomSample(const juce::File& file, juce::AudioFormatManager& formatManager) {
    if (!file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    int maxSamples = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, static_cast<juce::int64>(reader->sampleRate * 12.0)));
    juce::AudioBuffer<float> tempBuffer(static_cast<int>(std::min<unsigned int>(2u, reader->numChannels)), maxSamples);
    reader->read(&tempBuffer, 0, maxSamples, 0, true, true);

    setCustomSampleBuffer(tempBuffer, reader->sampleRate, file.getFileNameWithoutExtension(), file.getFullPathName());
    return true;
}

void ACBDrumVoice::setCustomSampleBuffer(const juce::AudioBuffer<float>& buffer, double sourceSampleRate, const juce::String& name, const juce::String& path) {
    int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    customSampleL.resize(static_cast<size_t>(numSamples));
    customSampleR.resize(static_cast<size_t>(numSamples));

    const float* srcL = buffer.getReadPointer(0);
    const float* srcR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : srcL;

    float peak = 0.001f;
    for (int i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::max(std::abs(srcL[i]), std::abs(srcR[i])));
    }
    float normGain = (peak > 0.01f) ? (0.92f / peak) : 1.0f;

    for (int i = 0; i < numSamples; ++i) {
        customSampleL[static_cast<size_t>(i)] = srcL[i] * normGain;
        customSampleR[static_cast<size_t>(i)] = srcR[i] * normGain;
    }

    customSampleLength = numSamples;
    customSampleRate = std::max(8000.0, sourceSampleRate);
    customSampleName = name.substring(0, 16);
    customSamplePath = path;
    useCustomSample = true;

    // Populate 128-point waveform preview for the LCD display
    waveformPreview.assign(128, 0.0f);
    for (int b = 0; b < 128; ++b) {
        int startIdx = (b * numSamples) / 128;
        int endIdx = std::max(startIdx + 1, ((b + 1) * numSamples) / 128);
        float maxVal = 0.0f;
        for (int i = startIdx; i < endIdx && i < numSamples; ++i) {
            float v = 0.5f * (customSampleL[static_cast<size_t>(i)] + customSampleR[static_cast<size_t>(i)]);
            if (std::abs(v) > std::abs(maxVal))
                maxVal = v;
        }
        waveformPreview[static_cast<size_t>(b)] = maxVal;
    }
}

void ACBDrumVoice::clearCustomSample() {
    useCustomSample = false;
    customSampleLength = 0;
    customSampleL.clear();
    customSampleR.clear();
    customSampleName.clear();
    customSamplePath.clear();
    updateSynthWaveformPreview();
}

float ACBDrumVoice::renderUserSample() {
    if (customSampleLength <= 1) {
        active = false;
        return 0.0f;
    }

    int idx0 = static_cast<int>(samplePlayhead);
    if (idx0 < 0 || idx0 >= customSampleLength - 1) {
        active = false;
        return 0.0f;
    }

    float frac = static_cast<float>(samplePlayhead - static_cast<double>(idx0));
    float s0 = 0.5f * (customSampleL[static_cast<size_t>(idx0)] + customSampleR[static_cast<size_t>(idx0)]);
    float s1 = 0.5f * (customSampleL[static_cast<size_t>(idx0 + 1)] + customSampleR[static_cast<size_t>(idx0 + 1)]);
    float interp = s0 + frac * (s1 - s0);

    // Pitch ratio from TUNE knob (-24 to +24 semitones)
    double pitchRatio = std::pow(2.0, (static_cast<double>(tuneParam) - 0.5) * 4.0) * (customSampleRate / sampleRate);
    if (sampleReverse)
        samplePlayhead -= pitchRatio;
    else
        samplePlayhead += pitchRatio;

    // Amplitude decay envelope from DECAY knob
    float decayDuration = 0.08f + decayParam * 3.5f;
    float ampEnv = std::exp(-envTime / decayDuration);
    if (ampEnv < 0.0005f) {
        active = false;
        return 0.0f;
    }

    // CTRL knob controls warm drive / low-pass filter on user samples
    float driven = interp * ampEnv;
    if (ctrlParam > 0.55f) {
        float drive = 1.0f + (ctrlParam - 0.55f) * 6.0f;
        driven = std::tanh(driven * drive);
    } else if (ctrlParam < 0.45f) {
        float cut = 0.05f + (ctrlParam / 0.45f) * 0.85f;
        svfLp1 += cut * (driven - svfLp1);
        driven = svfLp1;
    }
    return driven;
}

void ACBDrumVoice::updateSynthWaveformPreview() {
    if (useCustomSample && customSampleLength > 0)
        return;

    ACBDrumVoice previewVoice;
    previewVoice.channelIndex = channelIndex;
    previewVoice.currentToneId = currentToneId;
    previewVoice.sampleRate = 44100.0;
    previewVoice.tuneParam = tuneParam;
    previewVoice.decayParam = decayParam;
    previewVoice.ctrlParam = ctrlParam;
    previewVoice.levelParam = 1.0f;
    previewVoice.trigger(1.0f, 0.0f, false);

    waveformPreview.assign(128, 0.0f);
    constexpr int spb = 88; // 128 * 88 = 11264 samples (~255ms window at 44.1kHz)
    for (int b = 0; b < 128; ++b) {
        float maxVal = 0.0f;
        for (int s = 0; s < spb; ++s) {
            float l = 0.0f, r = 0.0f;
            previewVoice.renderNextSample(l, r);
            if (std::abs(l) > std::abs(maxVal))
                maxVal = l;
        }
        waveformPreview[static_cast<size_t>(b)] = maxVal;
    }
}

} // namespace ReplicantTR8S
