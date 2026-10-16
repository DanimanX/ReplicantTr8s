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
    phase1 = phase2 = phase3 = 0.0f;
    metalPhases.fill(0.0f);
    svfLp1 = svfBp1 = svfLp2 = svfBp2 = 0.0f;
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
    svfLp1 = svfBp1 = svfLp2 = svfBp2 = 0.0f;
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
        chokeGain *= 0.965f; // ~3ms smooth analog choke
        mono *= chokeGain;
        if (chokeGain < 0.001f) {
            active = false;
            choking = false;
        }
    }

    mono *= currentVelocity * levelParam;
    // Soft analog saturation on hot levels
    mono = std::tanh(mono * 1.15f);

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

    // Dispatch based on exact firmware tone ranges
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
// TR-808 ACB CIRCUIT MODELS
// ==============================================================================
float ACBDrumVoice::render808Model(int cat) {
    float tuneRatio = std::pow(2.0f, (tuneParam - 0.5f) * 1.5f);
    float decayScale = 0.25f + decayParam * 2.2f;
    float dt = 1.0f / static_cast<float>(sampleRate);

    // 1. 808 BASS DRUM (Bridged-T network with sub decay & click impulse)
    if (cat == 1 || currentToneId == 158) {
        bool isLong = (currentToneId == 2 || currentToneId == 3 || currentToneId == 158);
        float baseFreq = (currentToneId == 158 ? 55.0f : 48.0f) * tuneRatio;
        float decayTime = (isLong ? 0.95f : 0.42f) * decayScale;
        float pitchEnv = std::exp(-envTime * 68.0f);
        float subDrift = std::exp(-envTime * 4.5f);
        float freq = baseFreq * (0.94f + 0.12f * subDrift + (1.6f + ctrlParam * 2.2f) * pitchEnv);

        phase1 += freq * dt;
        if (phase1 >= 1.0f) phase1 -= 1.0f;

        float ampEnv = std::exp(-envTime / std::max(0.04f, decayTime));
        if (ampEnv < 0.0005f) { active = false; return 0.0f; }

        // Bridged-T slight 2nd harmonic asymmetry + initial pulse click
        float body = std::sin(phase1 * TWO_PI) + 0.08f * std::sin(phase1 * TWO_PI * 2.0f);
        float click = std::exp(-envTime * 420.0f) * (0.25f + ctrlParam * 0.65f);
        return (body * ampEnv + click) * 1.15f;
    }

    // 2. 808 SNARE DRUM (Dual Bridged-T 180Hz/330Hz + High-Pass Snappy Noise)
    if (cat == 2) {
        float f1 = 180.0f * tuneRatio * (1.0f + 0.45f * std::exp(-envTime * 95.0f));
        float f2 = 330.0f * tuneRatio * (1.0f + 0.35f * std::exp(-envTime * 95.0f));
        phase1 += f1 * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += f2 * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;

        float toneEnv = std::exp(-envTime * (15.0f / decayScale));
        float noiseEnv = std::exp(-envTime * (9.5f / decayScale));
        if (toneEnv < 0.0005f && noiseEnv < 0.0005f) { active = false; return 0.0f; }

        float shells = (std::sin(phase1 * TWO_PI) * 0.62f + std::sin(phase2 * TWO_PI) * 0.38f) * toneEnv;
        float rawNoise = nextWhiteNoise();
        // High-pass @ 2.4kHz for crisp 808 snare wires
        float cutoff = std::min(0.45f, 2400.0f / static_cast<float>(sampleRate));
        svfLp1 += cutoff * rawNoise - cutoff * svfLp1;
        float hpNoise = (rawNoise - svfLp1) * noiseEnv * (0.25f + ctrlParam * 1.15f);
        return (shells * 0.75f + hpNoise * 0.7f);
    }

    // 3. 808 TOMS & CONGAS (cat 3 = Toms, cat 8 = Congas)
    if (cat == 3 || cat == 8) {
        float baseFreq = 110.0f;
        if (channelIndex == 2) baseFreq = (cat == 8) ? 165.0f : 98.0f;
        else if (channelIndex == 3) baseFreq = (cat == 8) ? 245.0f : 142.0f;
        else if (channelIndex == 4) baseFreq = (cat == 8) ? 355.0f : 205.0f;

        baseFreq *= tuneRatio;
        float pitchSweep = 1.0f + (cat == 8 ? 0.25f : 0.55f) * std::exp(-envTime * 32.0f);
        phase1 += (baseFreq * pitchSweep) * dt;
        if (phase1 >= 1.0f) phase1 -= 1.0f;

        float decayRate = (cat == 8 ? 9.5f : 6.5f) / decayScale;
        float ampEnv = std::exp(-envTime * decayRate);
        if (ampEnv < 0.0005f) { active = false; return 0.0f; }

        float pinkNoise = nextWhiteNoise() * std::exp(-envTime * 55.0f) * (0.1f + ctrlParam * 0.35f);
        return (std::sin(phase1 * TWO_PI) * ampEnv + pinkNoise) * 0.95f;
    }

    // 4. 808 RIM SHOT & CLAVES (cat 4 = RS, cat 9 = Claves)
    if (cat == 4 || cat == 9) {
        if (cat == 9 || currentToneId == 16) {
            // 808 Claves: Pure 2500Hz resonant sine
            float f = 2480.0f * tuneRatio;
            phase1 += f * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
            float env = std::exp(-envTime * (38.0f / decayScale));
            if (env < 0.0005f) { active = false; return 0.0f; }
            return std::sin(phase1 * TWO_PI) * env * 0.9f;
        }
        // 808 Rim Shot: Dual resonant modes (470 Hz + 1720 Hz) with sharp transient clip
        phase1 += 470.0f * tuneRatio * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += 1720.0f * tuneRatio * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
        float env1 = std::exp(-envTime * (55.0f / decayScale));
        float env2 = std::exp(-envTime * (85.0f / decayScale));
        if (env1 < 0.0005f) { active = false; return 0.0f; }
        float sig = std::sin(phase1 * TWO_PI) * env1 * 0.6f + std::sin(phase2 * TWO_PI) * env2 * 0.85f;
        return std::tanh(sig * (1.8f + ctrlParam * 1.5f));
    }

    // 5. 808 HAND CLAP & MARACAS (cat 5 = Clap, cat 10 = Maracas)
    if (cat == 5 || cat == 10) {
        if (cat == 10 || currentToneId == 18) {
            // 808 Maracas: Fast attack-decay filtered high-frequency noise
            float attack = std::min(1.0f, envTime * 140.0f);
            float env = attack * std::exp(-envTime * (36.0f / decayScale));
            if (envTime > 0.02f && env < 0.0005f) { active = false; return 0.0f; }
            float n = nextWhiteNoise();
            svfLp1 += 0.45f * (n - svfLp1);
            return (n - svfLp1) * env * 0.85f;
        }
        // 808 Hand Clap: 3 micro-bursts spaced by 11ms + reverberant noise tail through 1.1kHz bandpass
        float env = 0.0f;
        if (envTime < 0.033f) {
            float burstPhase = std::fmod(envTime, 0.011f) / 0.011f;
            env = std::exp(-burstPhase * 4.2f);
        } else {
            env = 0.65f * std::exp(-(envTime - 0.033f) * (7.5f / decayScale));
        }
        if (envTime > 0.04f && env < 0.0005f) { active = false; return 0.0f; }

        float n = nextWhiteNoise();
        float f = 2.0f * std::sin(PI * std::min(0.35f, (1150.0f * tuneRatio) / static_cast<float>(sampleRate)));
        float q = 0.22f;
        float hp = n - svfLp1 - q * svfBp1;
        svfBp1 += f * hp;
        svfLp1 += f * svfBp1;
        return svfBp1 * env * (1.5f + ctrlParam * 0.9f);
    }

    // 6. 808 COWBELL (cat 11: 540Hz + 800Hz square oscillators through resonant bandpass)
    if (cat == 11 || currentToneId == 25) {
        phase1 += 540.0f * tuneRatio * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += 800.0f * tuneRatio * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
        float sq1 = (phase1 < 0.5f) ? 1.0f : -1.0f;
        float sq2 = (phase2 < 0.5f) ? 1.0f : -1.0f;
        float env = 0.65f * std::exp(-envTime * 35.0f) + 0.35f * std::exp(-envTime * (5.5f / decayScale));
        if (env < 0.0005f) { active = false; return 0.0f; }
        float raw = (sq1 + sq2) * 0.5f;
        float f = 2.0f * std::sin(PI * std::min(0.4f, (880.0f * tuneRatio) / static_cast<float>(sampleRate)));
        float hp = raw - svfLp1 - 0.35f * svfBp1;
        svfBp1 += f * hp;
        svfLp1 += f * svfBp1;
        return svfBp1 * env * 1.4f;
    }

    // 7. 808 HI-HATS (cat 6) & CYMBALS (cat 7) - 6-Oscillator Non-Harmonic Metallic Bank
    static constexpr float freqs808[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
    float metalSum = 0.0f;
    for (size_t i = 0; i < 6; ++i) {
        metalPhases[i] += (freqs808[i] * tuneRatio * 3.8f) * dt;
        if (metalPhases[i] >= 1.0f) metalPhases[i] -= 1.0f;
        metalSum += (metalPhases[i] < 0.5f) ? 1.0f : -1.0f;
    }
    metalSum *= (1.0f / 6.0f);

    bool isOpen = (channelIndex == 8 || currentToneId == 22);
    bool isCymbal = (cat == 7 || channelIndex >= 9);
    float decayRate = isCymbal ? (1.8f / decayScale) : (isOpen ? (4.2f / decayScale) : (26.0f / decayScale));
    float env = std::exp(-envTime * decayRate);
    if (env < 0.0005f) { active = false; return 0.0f; }

    // High-pass metallic bank @ ~6.5kHz
    float f = 2.0f * std::sin(PI * std::min(0.45f, (6200.0f * tuneRatio) / static_cast<float>(sampleRate)));
    float hp = metalSum - svfLp1 - 0.5f * svfBp1;
    svfBp1 += f * hp;
    svfLp1 += f * svfBp1;
    return (hp * 0.7f + svfBp1 * 0.5f) * env * (0.9f + ctrlParam * 0.5f);
}

// ==============================================================================
// TR-909 ACB CIRCUIT MODELS
// ==============================================================================
float ACBDrumVoice::render909Model(int cat) {
    float tuneRatio = std::pow(2.0f, (tuneParam - 0.5f) * 1.5f);
    float decayScale = 0.25f + decayParam * 2.1f;
    float dt = 1.0f / static_cast<float>(sampleRate);

    // 1. 909 BASS DRUM (Triangle-to-Sine Waveshaper + Dual-Stage Pitch Env + Attack Click)
    if (cat == 1 || currentToneId == 157) {
        float baseFreq = 52.0f * tuneRatio;
        float fastEnv = std::exp(-envTime * 95.0f);
        float medEnv  = std::exp(-envTime * 19.0f);
        float freq = baseFreq + (210.0f * fastEnv + 58.0f * medEnv) * tuneRatio;

        phase1 += freq * dt;
        if (phase1 >= 1.0f) phase1 -= 1.0f;

        // Triangle core shaped through diode sine shaper
        float tri = 4.0f * std::abs(phase1 - 0.5f) - 1.0f;
        float shapedSine = std::sin(tri * (PI * 0.5f));

        float ampEnv = std::exp(-envTime * (2.6f / decayScale));
        if (ampEnv < 0.0005f) { active = false; return 0.0f; }

        // 909 Attack transient (pulse + filtered noise controlled by CTRL)
        float clickEnv = std::exp(-envTime * 340.0f);
        float click = (clickEnv * 0.85f + nextWhiteNoise() * std::exp(-envTime * 480.0f) * 0.55f) * (0.2f + ctrlParam * 1.25f);
        return std::tanh((shapedSine * ampEnv + click) * 1.45f);
    }

    // 2. 909 SNARE DRUM (Dual Triangle VCOs + Dual-Envelope Snappy Noise)
    if (cat == 2) {
        float sweep = 1.0f + 0.65f * std::exp(-envTime * 85.0f);
        phase1 += 185.0f * tuneRatio * sweep * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += 342.0f * tuneRatio * sweep * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;

        float bodyEnv = std::exp(-envTime * (16.0f / decayScale));
        float snapEnv = 0.6f * std::exp(-envTime * (22.0f / decayScale)) + 0.4f * std::exp(-envTime * (7.5f / decayScale));
        if (bodyEnv < 0.0005f && snapEnv < 0.0005f) { active = false; return 0.0f; }

        float body = (std::sin(phase1 * TWO_PI) * 0.65f + std::sin(phase2 * TWO_PI) * 0.35f) * bodyEnv;
        float n = nextWhiteNoise();
        svfLp1 += 0.25f * (n - svfLp1);
        float snappy = (n - svfLp1 * 0.6f) * snapEnv * (0.25f + ctrlParam * 1.35f);
        return std::tanh((body * 0.85f + snappy * 0.75f) * 1.25f);
    }

    // 3. 909 TOMS (LT, MT, HT)
    if (cat == 3) {
        float baseFreq = (channelIndex == 2) ? 92.0f : (channelIndex == 3 ? 132.0f : 185.0f);
        baseFreq *= tuneRatio;
        float freq = baseFreq * (1.0f + 0.85f * std::exp(-envTime * 28.0f));
        phase1 += freq * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        float ampEnv = std::exp(-envTime * (5.8f / decayScale));
        if (ampEnv < 0.0005f) { active = false; return 0.0f; }
        float click = nextWhiteNoise() * std::exp(-envTime * 160.0f) * (0.15f + ctrlParam * 0.45f);
        return (std::sin(phase1 * TWO_PI) * ampEnv + click) * 1.05f;
    }

    // 4. 909 RIM SHOT (3-Oscillator Modal Pulse Circuit)
    if (cat == 4) {
        phase1 += 250.0f * tuneRatio * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += 495.0f * tuneRatio * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
        phase3 += 1580.0f * tuneRatio * dt; if (phase3 >= 1.0f) phase3 -= 1.0f;
        float env = std::exp(-envTime * (65.0f / decayScale));
        if (env < 0.0005f) { active = false; return 0.0f; }
        float mix = std::sin(phase1 * TWO_PI) * 0.4f + std::sin(phase2 * TWO_PI) * 0.6f + std::sin(phase3 * TWO_PI) * 0.9f;
        return std::tanh(mix * env * (2.2f + ctrlParam * 1.4f));
    }

    // 5. 909 HAND CLAP
    if (cat == 5) {
        float env = 0.0f;
        if (envTime < 0.030f) {
            float b = std::fmod(envTime, 0.010f) / 0.010f;
            env = std::exp(-b * 3.8f);
        } else {
            env = 0.72f * std::exp(-(envTime - 0.030f) * (6.8f / decayScale));
        }
        if (envTime > 0.04f && env < 0.0005f) { active = false; return 0.0f; }
        float n = nextWhiteNoise();
        float f = 2.0f * std::sin(PI * std::min(0.4f, (1320.0f * tuneRatio) / static_cast<float>(sampleRate)));
        float hp = n - svfLp1 - 0.28f * svfBp1;
        svfBp1 += f * hp;
        svfLp1 += f * svfBp1;
        return std::tanh(svfBp1 * env * (1.8f + ctrlParam * 1.0f));
    }

    // 6. 909 6-BIT LO-FI METALLIC HI-HATS (cat 6) & CRASH/RIDE CYMBALS (cat 7)
    static constexpr float freqs909[6] = { 317.0f, 466.0f, 628.0f, 795.0f, 1045.0f, 1412.0f };
    float s = 0.0f;
    for (size_t i = 0; i < 6; ++i) {
        metalPhases[i] += (freqs909[i] * tuneRatio * 4.2f) * dt;
        if (metalPhases[i] >= 1.0f) metalPhases[i] -= 1.0f;
        s += std::sin(metalPhases[i] * TWO_PI + ((i & 1u) ? 0.4f : -0.4f));
    }
    // Mix metallic partials + high-frequency shimmer noise, quantized to 6-bit (64 steps) like 909 EPROM
    float raw = (s * 0.14f + nextWhiteNoise() * 0.42f);
    float quantized6Bit = std::round(raw * 32.0f) * (1.0f / 32.0f);

    bool isOpen = (channelIndex == 8 || currentToneId == 39);
    bool isCrash = (currentToneId == 40 || channelIndex == 9);
    bool isRide = (currentToneId == 41 || channelIndex == 10);
    float decayRate = isCrash ? (1.4f / decayScale) : (isRide ? (1.1f / decayScale) : (isOpen ? (3.8f / decayScale) : (24.0f / decayScale)));
    float env = std::exp(-envTime * decayRate);
    if (env < 0.0005f) { active = false; return 0.0f; }

    float f = 2.0f * std::sin(PI * std::min(0.45f, (isRide ? 5200.0f : 7400.0f) * tuneRatio / static_cast<float>(sampleRate)));
    float hp = quantized6Bit - svfLp1 - 0.4f * svfBp1;
    svfBp1 += f * hp;
    svfLp1 += f * svfBp1;

    float bell = 0.0f;
    if (isRide) {
        phase1 += 2840.0f * tuneRatio * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        bell = std::sin(phase1 * TWO_PI) * std::exp(-envTime * (5.5f / decayScale)) * 0.35f;
    }
    return (hp * 0.65f + svfBp1 * 0.55f + bell) * env * (0.95f + ctrlParam * 0.45f);
}

// ==============================================================================
// TR-707 & TR-626 VINTAGE 12-BIT PCM MODELS
// ==============================================================================
float ACBDrumVoice::render707Or626Model(int cat, bool is707) {
    // 707/626 use punchy 12-bit/8-bit companded PCM drum timbres
    float raw = is707 ? render909Model(cat) : render808Model(cat);
    // Apply 25kHz sample-and-hold & 8-bit mu-law-style quantization crunch
    float levels = is707 ? 64.0f : 48.0f;
    float crushed = std::round(raw * levels) / levels;
    return std::tanh(crushed * (1.2f + ctrlParam * 0.6f));
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
            // 727 Samba Whistle (FM LFO vibrato at 2.8kHz + breath noise)
            float dur = (currentToneId == 69) ? (0.55f * decayScale) : (0.18f * decayScale);
            if (envTime > dur) { active = false; return 0.0f; }
            float vib = std::sin(envTime * TWO_PI * 28.0f) * 0.035f;
            phase1 += (2780.0f * tuneRatio * (1.0f + vib)) * dt;
            if (phase1 >= 1.0f) phase1 -= 1.0f;
            float env = std::min(1.0f, envTime * 80.0f) * std::exp(-(envTime / dur) * 2.2f);
            return (std::sin(phase1 * TWO_PI) * 0.75f + nextWhiteNoise() * 0.15f) * env;
        }
        if (currentToneId == 71) {
            // 727 Star Chime (Sweeping high metallic chimes)
            float env = std::exp(-envTime * (1.8f / decayScale));
            if (env < 0.0005f) { active = false; return 0.0f; }
            float chime = 0.0f;
            for (size_t i = 0; i < 6; ++i) {
                float f = (2200.0f + static_cast<float>(i) * 670.0f + std::fmod(envTime * 900.0f, 1200.0f)) * tuneRatio;
                metalPhases[i] += f * dt;
                if (metalPhases[i] >= 1.0f) metalPhases[i] -= 1.0f;
                chime += std::sin(metalPhases[i] * TWO_PI);
            }
            return (chime * 0.16f) * env;
        }
        // Quijada (Rattle)
        float pulse = (std::fmod(envTime, 0.018f) < 0.004f) ? 1.0f : 0.15f;
        float env = std::exp(-envTime * (4.5f / decayScale));
        if (env < 0.0005f) { active = false; return 0.0f; }
        phase1 += 1650.0f * tuneRatio * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        return std::sin(phase1 * TWO_PI) * pulse * env;
    }

    // Agogo bells (cat 11)
    if (cat == 11) {
        float f1 = ((currentToneId == 64) ? 880.0f : 660.0f) * tuneRatio;
        phase1 += f1 * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += (f1 * 1.414f) * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;
        float env = std::exp(-envTime * (11.0f / decayScale));
        if (env < 0.0005f) { active = false; return 0.0f; }
        return (std::sin(phase1 * TWO_PI) * 0.7f + std::sin(phase2 * TWO_PI) * 0.35f) * env;
    }

    // Bongos, Congas, Timbales (cat 8)
    float baseFreq = 220.0f;
    if (currentToneId == 57) baseFreq = 390.0f; // Hi Bongo
    else if (currentToneId == 58) baseFreq = 260.0f; // Low Bongo
    else if (currentToneId == 59) baseFreq = 310.0f; // Mute Hi Conga
    else if (currentToneId == 60) baseFreq = 295.0f; // Open Hi Conga
    else if (currentToneId == 61) baseFreq = 195.0f; // Low Conga
    else if (currentToneId == 62) baseFreq = 250.0f; // Hi Timbale
    else if (currentToneId == 63) baseFreq = 175.0f; // Low Timbale

    baseFreq *= tuneRatio;
    float pitchBend = 1.0f + 0.35f * std::exp(-envTime * 45.0f);
    phase1 += baseFreq * pitchBend * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
    phase2 += baseFreq * 1.52f * dt; if (phase2 >= 1.0f) phase2 -= 1.0f;

    float decayRate = (currentToneId == 59 ? 24.0f : 8.5f) / decayScale;
    float env = std::exp(-envTime * decayRate);
    if (env < 0.0005f) { active = false; return 0.0f; }
    float slap = nextWhiteNoise() * std::exp(-envTime * 110.0f) * (0.25f + ctrlParam * 0.45f);
    return (std::sin(phase1 * TWO_PI) * 0.8f + std::sin(phase2 * TWO_PI) * 0.25f) * env + slap;
}

// ==============================================================================
// TR-606 TRANSISTOR RHYTHM MODELS
// ==============================================================================
float ACBDrumVoice::render606Model(int cat) {
    float tuneRatio = std::pow(2.0f, (tuneParam - 0.5f) * 1.5f);
    float decayScale = 0.25f + decayParam * 2.0f;
    float dt = 1.0f / static_cast<float>(sampleRate);

    if (cat == 1) {
        // 606 Punchy Transistor Kick
        float f = 58.0f * tuneRatio * (1.0f + 1.4f * std::exp(-envTime * 55.0f));
        phase1 += f * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
        float env = std::exp(-envTime * (6.2f / decayScale));
        if (env < 0.0005f) { active = false; return 0.0f; }
        float click = std::exp(-envTime * 350.0f) * 0.45f;
        return std::tanh((std::sin(phase1 * TWO_PI) * env + click) * 1.3f);
    }
    return render808Model(cat);
}

// ==============================================================================
// CR-78 COMPURHYTHM (v3.00 FIRMWARE) MODELS
// ==============================================================================
float ACBDrumVoice::renderCR78Model(int cat) {
    float tuneRatio = std::pow(2.0f, (tuneParam - 0.5f) * 1.5f);
    float decayScale = 0.25f + decayParam * 2.0f;
    float dt = 1.0f / static_cast<float>(sampleRate);

    // CR-78 Guiro (171)
    if (currentToneId == 171) {
        float dur = 0.38f * decayScale;
        if (envTime > dur) { active = false; return 0.0f; }
        float ratchet = 0.5f + 0.5f * std::sin(envTime * TWO_PI * 72.0f);
        phase1 += (1450.0f + envTime * 1100.0f) * tuneRatio * dt;
        if (phase1 >= 1.0f) phase1 -= 1.0f;
        return std::sin(phase1 * TWO_PI) * ratchet * (1.0f - envTime / dur) * 0.85f;
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
        float env = std::exp(-envTime * (12.0f / decayScale));
        if (env < 0.0005f) { active = false; return 0.0f; }
        return sum * 0.22f * env;
    }

    // Warm LC-Tank CR-78 Bass/Snare/Bongo/HiHat
    return render808Model(cat) * 0.92f;
}

// ==============================================================================
// 2-OPERATOR FM DRUM ENGINE (v2.00 FIRMWARE: TONES 86..156)
// ==============================================================================
float ACBDrumVoice::renderFMModel(int cat) {
    float tuneRatio = std::pow(2.0f, (tuneParam - 0.5f) * 2.0f);
    float decayScale = 0.2f + decayParam * 2.2f;
    float dt = 1.0f / static_cast<float>(sampleRate);

    // Carrier & Modulator ratios determined by category and tone ID
    float baseCarrier = 110.0f;
    float modRatio = 1.414f + (currentToneId % 7) * 0.37f;
    float modIndexBase = 1.8f + ctrlParam * 6.5f; // CTRL = FM Morph!
    float pitchSweepAmt = 0.5f;
    float decayRate = 8.0f / decayScale;

    if (cat == 1) { // FM Kick
        baseCarrier = 50.0f;
        modRatio = 1.0f + (currentToneId % 4) * 0.5f;
        pitchSweepAmt = 3.2f;
        decayRate = 4.2f / decayScale;
    } else if (cat == 2) { // FM Snare
        baseCarrier = 175.0f;
        modRatio = 2.37f + (currentToneId % 5) * 0.41f;
        pitchSweepAmt = 1.1f;
        decayRate = 10.0f / decayScale;
    } else if (cat == 3 || cat == 8) { // FM Tom / Perc
        baseCarrier = (channelIndex == 2) ? 95.0f : (channelIndex == 3 ? 140.0f : 195.0f);
        modRatio = 1.73f;
        pitchSweepAmt = 1.4f;
        decayRate = 6.5f / decayScale;
    } else if (cat == 6 || cat == 7) { // FM Hi-Hat / Cymbal
        baseCarrier = 680.0f;
        modRatio = 3.14159f;
        modIndexBase = 4.5f + ctrlParam * 8.0f;
        decayRate = (channelIndex == 7) ? (22.0f / decayScale) : (3.5f / decayScale);
    }

    float pitchEnv = 1.0f + pitchSweepAmt * std::exp(-envTime * 55.0f);
    float cFreq = baseCarrier * tuneRatio * pitchEnv;
    float mFreq = cFreq * modRatio;

    phase2 += mFreq * dt;
    if (phase2 >= 1.0f) phase2 -= 1.0f;

    float modEnv = std::exp(-envTime * (decayRate * 1.8f));
    float modSig = std::sin(phase2 * TWO_PI) * modIndexBase * modEnv;

    phase1 += cFreq * dt;
    if (phase1 >= 1.0f) phase1 -= 1.0f;

    float ampEnv = std::exp(-envTime * decayRate);
    if (ampEnv < 0.0005f) { active = false; return 0.0f; }

    float fmOut = std::sin(phase1 * TWO_PI + modSig) * ampEnv;
    if (cat == 2 || cat == 5) {
        fmOut += nextWhiteNoise() * modEnv * 0.35f;
    }
    return std::tanh(fmOut * 1.25f);
}

// ==============================================================================
// PRESET SYNTH / BASS / CHORD / FX MODELS (TONES 355..521)
// ==============================================================================
float ACBDrumVoice::renderPCMOrSynthModel(int cat) {
    float tuneRatio = std::pow(2.0f, (tuneParam - 0.5f) * 2.0f);
    float decayScale = 0.25f + decayParam * 2.2f;
    float dt = 1.0f / static_cast<float>(sampleRate);

    // Chords (cat 19), Leads/Pads (cat 15), Basses (cat 17), Mallets (cat 18)
    float baseFreq = (cat == 17) ? 65.41f : 220.0f;
    baseFreq *= tuneRatio;

    phase1 += baseFreq * dt; if (phase1 >= 1.0f) phase1 -= 1.0f;
    phase2 += (baseFreq * 1.1892f) * dt; if (phase2 >= 1.0f) phase2 -= 1.0f; // Minor 3rd
    phase3 += (baseFreq * 1.4983f) * dt; if (phase3 >= 1.0f) phase3 -= 1.0f; // Perfect 5th

    float env = std::exp(-envTime * (3.8f / decayScale));
    if (env < 0.0005f) { active = false; return 0.0f; }

    float saw1 = 2.0f * phase1 - 1.0f;
    float saw2 = 2.0f * phase2 - 1.0f;
    float saw3 = 2.0f * phase3 - 1.0f;
    float raw = (cat == 19) ? ((saw1 + saw2 + saw3) * 0.33f) : (std::sin(phase1 * TWO_PI) * 0.7f + saw1 * 0.3f);

    float cutoff = std::min(0.45f, (400.0f + ctrlParam * 4200.0f * env) / static_cast<float>(sampleRate));
    svfLp1 += cutoff * (raw - svfLp1);
    return std::tanh(svfLp1 * env * 1.35f);
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

    // CTRL knob controls warm drive / bit-crunch on user samples
    float driven = interp * ampEnv;
    if (ctrlParam > 0.55f) {
        float drive = 1.0f + (ctrlParam - 0.55f) * 6.0f;
        driven = std::tanh(driven * drive);
    } else if (ctrlParam < 0.45f) {
        // Low-pass filter sweep when CTRL < 0.45
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
    previewVoice.sampleRate = 16000.0;
    previewVoice.tuneParam = tuneParam;
    previewVoice.decayParam = decayParam;
    previewVoice.ctrlParam = ctrlParam;
    previewVoice.levelParam = 1.0f;
    previewVoice.trigger(1.0f, 0.0f, false);

    waveformPreview.assign(128, 0.0f);
    constexpr int spb = 32; // 128 * 32 = 4096 samples (~256ms window)
    for (int b = 0; b < 128; ++b) {
        float maxVal = 0.0f;
        for (int s = 0; s < spb; ++s) {
            float l = 0.0f, r = 0.0f;
            previewVoice.renderNextSample(l, r);
            if (std::abs(l) > std::abs(maxVal))
                maxVal = l;
        }
        waveformPreview[static_cast<size_t>(b)] = juce::jlimit(-1.0f, 1.0f, maxVal);
    }
}

} // namespace ReplicantTR8S
