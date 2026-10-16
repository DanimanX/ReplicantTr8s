#pragma once
// Clean-Room Runtime Firmware Loader & Data Structures for ReplicantTr8s
// Does NOT embed proprietary firmware data in the source or binary.
// Parses tr8s_sys_v300.zip / TR8S_UP.bin / rpg42_init_param.bin at runtime when provided by the user.

#include <juce_core/juce_core.h>
#include <cstdint>
#include <array>
#include <string>

namespace TR8SFirmware {

static constexpr int NUM_CHANNELS = 11;
static constexpr int NUM_STEPS = 16;
static constexpr int NUM_VARIATIONS = 8;
static constexpr int NUM_KITS = 128;
static constexpr int NUM_PATTERNS = 128;
static constexpr int NUM_TONES = 522;

static constexpr const char* CHANNEL_NAMES[NUM_CHANNELS] = {
    "BD", "SD", "LT", "MT", "HT", "RS", "HC", "CH", "OH", "CC", "RC"
};

static constexpr const char* CHANNEL_FULL_NAMES[NUM_CHANNELS] = {
    "BASS DRUM", "SNARE DRUM", "LOW TOM", "MID TOM", "HIGH TOM",
    "RIM SHOT", "HAND CLAP", "CLOSED HI-HAT", "OPEN HI-HAT", "CRASH CYMBAL", "RIDE CYMBAL"
};

struct FirmwareTone {
    uint16_t id = 0;
    char name[24] = "Unloaded";
    uint8_t category = 1;
    uint8_t engineType = 1; // 1 = ACB, 2 = PCM/Sample, 3 = FM
};

struct FirmwareChannelPreset {
    uint16_t toneId = 0;
    uint8_t tune = 128;     // 0..255 (128 = center)
    uint8_t decay = 128;    // 0..255 (128 = nominal)
    uint8_t level = 220;    // 0..255
    uint8_t gain = 81;      // 0..255 (81 = nominal)
    uint8_t pan = 128;      // 0..255 (128 = center)
    uint8_t revSend = 0;    // 0..255
    uint8_t dlySend = 0;    // 0..255
    uint8_t ctrl = 128;     // 0..255 (128 = center)
    uint8_t fxType = 0;
};

struct FirmwareKit {
    uint8_t number = 1; // 1..128
    char name[20] = "NO FIRMWARE";
    FirmwareChannelPreset channels[NUM_CHANNELS];
};

struct FirmwareVariation {
    uint8_t velocity[NUM_CHANNELS + 1][NUM_STEPS] {}; // 0..10 = BD..RC, 11 = ACCENT
    uint8_t flags[NUM_CHANNELS + 1][NUM_STEPS] {};    // bit7 = ghost/accent, low nibble = substep/flam
};

struct FirmwarePattern {
    uint8_t number = 1; // 1..128
    char code[8] = "1-01";
    char name[20] = "NO FIRMWARE";
    uint16_t tempoTenths = 1280; // e.g. 1280 = 128.0 BPM
    uint8_t kitNumber = 1;       // 1..128
    FirmwareVariation variations[NUM_VARIATIONS];
};

// Runtime-populated tables (populated when user loads tr8s_sys_v300.zip / TR8S_UP.bin)
extern FirmwareTone FACTORY_TONES[NUM_TONES];
extern FirmwareKit FACTORY_KITS[NUM_KITS];
extern FirmwarePattern FACTORY_PATTERNS[NUM_PATTERNS];

class RuntimeFirmwareLoader {
public:
    static void initializeCleanDefaults();
    static bool isFirmwareLoaded();
    static juce::String getLoadedFirmwareName();
    static juce::File getSavedFirmwarePathFile();

    // Loads tr8s_sys_v300.zip, TR8S_UP.bin, or rpg42_init_param.bin at runtime
    static bool loadFromFile(const juce::File& file, juce::String& errorMsg, bool savePreference = true);
    static bool tryAutoLoadSavedFirmware();

private:
    static bool parseTarArchive(const void* data, size_t size, juce::String& errorMsg);
    static bool parseInitParamBin(const uint8_t* data, size_t size, juce::String& errorMsg);
    static std::vector<uint8_t> decompressLZSS(const uint8_t* src, size_t srcLen, size_t uncompLen);
};

} // namespace TR8SFirmware
