#include "TR8SFirmwareData.h"
#include <cstring>
#include <algorithm>
#include <vector>

namespace TR8SFirmware {

FirmwareTone FACTORY_TONES[NUM_TONES];
FirmwareKit FACTORY_KITS[NUM_KITS];
FirmwarePattern FACTORY_PATTERNS[NUM_PATTERNS];

static bool gFirmwareLoaded = false;
static bool gDefaultsInitialized = false;
static juce::String gLoadedFirmwareName = "None (Awaiting TR8S_UP.bin / tr8s_sys_v300.zip)";

namespace {

struct FMOrCR78ToneMeta {
    uint16_t id;
    const char* label;
    uint8_t cat;
    uint8_t engineType;
};

static const FMOrCR78ToneMeta kSupplementalToneMeta[] = {
    {81, "808 Conga L Morph", 8, 1}, {82, "808 Conga M Morph", 8, 1}, {83, "808 Conga H Morph", 8, 1},
    {84, "909 Rim Morph", 4, 1},     {85, "909 Clap Morph", 5, 1},    {86, "FM Kick 1", 1, 3},
    {87, "FM Kick 2", 1, 3},         {88, "FM Kick 3", 1, 3},         {89, "FM Kick 4", 1, 3},
    {90, "FM Attack Kick", 1, 3},    {91, "FM Sub Kick", 1, 3},       {92, "FM Punch Kick", 1, 3},
    {93, "FM Deep Kick", 1, 3},      {94, "FM Elektro Kick", 1, 3},   {95, "FM Gabber Kick", 1, 3},
    {96, "FM Wood Kick", 1, 3},      {97, "FM Snare 1", 2, 3},        {98, "FM Snare 2", 2, 3},
    {99, "FM Snare 3", 2, 3},        {100, "FM Tight Snare", 2, 3},   {101, "FM Hollow Snare", 2, 3},
    {102, "FM Metal Snare", 2, 3},   {103, "FM Zap Snare", 2, 3},     {104, "FM Snap Snare", 2, 3},
    {105, "FM Crisp Snare", 2, 3},   {106, "FM Elektro Snare", 2, 3}, {107, "FM Noise Snare", 2, 3},
    {108, "FM Tom 1", 3, 3},         {109, "FM Tom 2", 3, 3},         {110, "FM Groover Tom", 3, 3},
    {111, "FM Sphere Tom", 3, 3},    {112, "FM Space Tom", 3, 3},     {113, "FM Rim Shot", 4, 3},
    {114, "FM Hand Clap 1", 5, 3},   {115, "FM Hand Clap 2", 5, 3},   {116, "FM Glitch Clap", 5, 3},
    {117, "FM Slap Clap", 5, 3},     {118, "FM Closed HH 1", 6, 3},   {119, "FM Closed HH 2", 6, 3},
    {120, "FM Open HH 1", 6, 3},     {121, "FM Open HH 2", 6, 3},     {122, "FM Crash Cym 1", 7, 3},
    {123, "FM Crash Cym 2", 7, 3},   {124, "FM Stick Perc", 4, 3},    {125, "FM Click Perc", 9, 3},
    {126, "FM Metal Rim", 4, 3},     {127, "FM Ride Cym 1", 7, 3},    {128, "FM Ride Cym 2", 7, 3},
    {129, "FM Bell Cym", 7, 3},      {130, "FM Gong Cym", 7, 3},      {131, "FM Bongo", 8, 3},
    {132, "FM Agogo", 11, 3},        {133, "FM Conga", 8, 3},         {134, "FM Timbale", 8, 3},
    {135, "FM Cowbell 1", 11, 3},    {136, "FM Cowbell 2", 11, 3},    {137, "FM Claves", 9, 3},
    {138, "FM Metal Perc", 11, 3},   {139, "FM Triangle", 11, 3},     {140, "FM Woodblock", 9, 3},
    {141, "FM Marimba", 18, 3},      {142, "FM Vibraphone", 18, 3},   {143, "FM Bass 1", 17, 3},
    {144, "FM Bass 2", 17, 3},       {145, "FM Bell Synth", 15, 3},   {146, "FM Pluck Synth", 15, 3},
    {147, "FM Chord Synth", 19, 3},  {148, "FM Tube Perc", 13, 3},    {149, "FM Riser FX", 13, 3},
    {150, "FM Laser FX", 13, 3},     {151, "FM Drop FX", 13, 3},      {152, "FMBubble FX", 13, 3},
    {153, "FM Sci-Fi FX", 13, 3},    {154, "FM Warp FX", 13, 3},      {155, "FM Glitch FX", 13, 3},
    {156, "FM Bleep Synth", 15, 3},  {157, "909 Chromatic Bs", 17, 1},{158, "808 Chromatic Bs", 17, 1},
    {159, "CR-78 Bass", 1, 1},       {160, "CR-78 Snare", 2, 1},      {161, "CR-78 Rim Shot", 4, 1},
    {162, "CR-78 Closed HH", 6, 1},  {163, "CR-78 Open HH", 6, 1},    {164, "CR-78 Cymbal", 7, 1},
    {165, "CR-78 Low Bongo", 8, 1},  {166, "CR-78 High Bongo", 8, 1}, {167, "CR-78 Low Conga", 8, 1},
    {168, "CR-78 Maracas", 10, 1},   {169, "CR-78 Cowbell", 11, 1},   {170, "CR-78 Tambourine", 10, 1},
    {171, "CR-78 Guiro", 12, 1},     {172, "CR-78 Metal Beat", 11, 1}, {173, "CR-78 Claves", 9, 1}
};

uint32_t readLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t readLE16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

void copyTrimmedAscii(char* dst, size_t dstCap, const uint8_t* src, size_t srcLen) {
    size_t n = std::min(dstCap - 1, srcLen);
    std::memcpy(dst, src, n);
    dst[n] = '\0';
    for (size_t i = 0; i < n; ++i) {
        if (static_cast<unsigned char>(dst[i]) < 0x20 || static_cast<unsigned char>(dst[i]) > 0x7e)
            dst[i] = ' ';
    }
    int end = static_cast<int>(n) - 1;
    while (end >= 0 && (dst[end] == ' ' || dst[end] == '\0')) {
        dst[end] = '\0';
        --end;
    }
}

} // namespace

void RuntimeFirmwareLoader::initializeCleanDefaults() {
    if (gDefaultsInitialized)
        return;
    gDefaultsInitialized = true;

    for (int i = 0; i < NUM_TONES; ++i) {
        FACTORY_TONES[i].id = static_cast<uint16_t>(i);
        std::snprintf(FACTORY_TONES[i].name, sizeof(FACTORY_TONES[i].name), "Tone %03d (Load FW)", i);
        FACTORY_TONES[i].category = static_cast<uint8_t>((i % 11) + 1);
        FACTORY_TONES[i].engineType = 1;
    }

    static const uint16_t kDefaultChTones[NUM_CHANNELS] = { 1, 5, 6, 7, 8, 15, 17, 21, 22, 23, 25 };
    for (int k = 0; k < NUM_KITS; ++k) {
        FACTORY_KITS[k].number = static_cast<uint8_t>(k + 1);
        std::snprintf(FACTORY_KITS[k].name, sizeof(FACTORY_KITS[k].name), "INIT KIT %03d", k + 1);
        for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
            auto& cp = FACTORY_KITS[k].channels[ch];
            cp.toneId = kDefaultChTones[ch];
            cp.tune = 128;
            cp.decay = 128;
            cp.level = 230;
            cp.gain = 81;
            cp.pan = 128;
            cp.revSend = 20;
            cp.dlySend = 10;
            cp.ctrl = 128;
            cp.fxType = 0;
        }
    }

    for (int p = 0; p < NUM_PATTERNS; ++p) {
        auto& ptn = FACTORY_PATTERNS[p];
        ptn.number = static_cast<uint8_t>(p + 1);
        int bank = (p / 16) + 1;
        int slot = (p % 16) + 1;
        std::snprintf(ptn.code, sizeof(ptn.code), "%d-%02d", bank, slot);
        std::snprintf(ptn.name, sizeof(ptn.name), "INIT PTN %d-%02d", bank, slot);
        ptn.tempoTenths = 1280;
        ptn.kitNumber = 1;
        for (int v = 0; v < NUM_VARIATIONS; ++v)
            ptn.variations[v] = FirmwareVariation {};
    }
}

bool RuntimeFirmwareLoader::isFirmwareLoaded() {
    return gFirmwareLoaded;
}

juce::String RuntimeFirmwareLoader::getLoadedFirmwareName() {
    return gLoadedFirmwareName;
}

juce::File RuntimeFirmwareLoader::getSavedFirmwarePathFile() {
    auto appDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("ReplicantTr8s");
    appDir.createDirectory();
    return appDir.getChildFile("firmware_location.txt");
}

bool RuntimeFirmwareLoader::tryAutoLoadSavedFirmware() {
    initializeCleanDefaults();
    if (gFirmwareLoaded)
        return true;

    auto prefFile = getSavedFirmwarePathFile();
    if (prefFile.existsAsFile()) {
        juce::String savedPath = prefFile.loadFileAsString().trim();
        if (savedPath.isNotEmpty()) {
            juce::File fwFile(savedPath);
            juce::String err;
            if (fwFile.existsAsFile() && loadFromFile(fwFile, err, false))
                return true;
        }
    }
    return false;
}

std::vector<uint8_t> RuntimeFirmwareLoader::decompressLZSS(const uint8_t* src, size_t srcLen, size_t uncompLen) {
    constexpr size_t N = 4096;
    constexpr size_t F = 18;
    constexpr size_t THRESHOLD = 2;

    std::array<uint8_t, N> ring {};
    size_t r = N - F;
    std::vector<uint8_t> out;
    out.reserve(uncompLen);

    size_t i = 0;
    uint32_t flags = 0;
    while (i < srcLen && out.size() < uncompLen) {
        flags >>= 1;
        if ((flags & 0x100u) == 0) {
            if (i >= srcLen) break;
            flags = static_cast<uint32_t>(src[i++]) | 0xFF00u;
        }
        if (flags & 1u) {
            if (i >= srcLen) break;
            uint8_t c = src[i++];
            out.push_back(c);
            ring[r] = c;
            r = (r + 1) & (N - 1);
        } else {
            if (i + 1 >= srcLen) break;
            uint8_t b0 = src[i++];
            uint8_t b1 = src[i++];
            size_t offset = static_cast<size_t>(b0) | (static_cast<size_t>(b1 & 0xF0u) << 4);
            size_t length = static_cast<size_t>(b1 & 0x0Fu) + THRESHOLD + 1;
            for (size_t k = 0; k < length && out.size() < uncompLen; ++k) {
                uint8_t c = ring[(offset + k) & (N - 1)];
                out.push_back(c);
                ring[r] = c;
                r = (r + 1) & (N - 1);
            }
        }
    }
    return out;
}

bool RuntimeFirmwareLoader::parseInitParamBin(const uint8_t* data, size_t size, juce::String& errorMsg) {
    if (size < 0x40 || std::memcmp(data, "INIT", 4) != 0) {
        errorMsg = "Invalid rpg42_init_param.bin header (missing INIT signature).";
        return false;
    }

    uint32_t compSize = readLE32(data + 8);
    uint32_t uncompSize = readLE32(data + 20);
    if (compSize == 0 || 0x30 + compSize > size || uncompSize < 0x330000) {
        errorMsg = "Corrupted LZSS size fields in firmware parameter binary.";
        return false;
    }

    auto unpacked = decompressLZSS(data + 0x30, compSize, uncompSize);
    if (unpacked.size() < 0x330000 || std::memcmp(unpacked.data(), "TR8S", 4) != 0) {
        errorMsg = "LZSS decompression failed or TR8S magic missing.";
        return false;
    }

    // 1. Parse TONE chunk (0x326f90 + 32)
    constexpr size_t tonePos = 0x326F90 + 32;
    for (int i = 0; i < NUM_TONES; ++i) {
        const uint8_t* t = unpacked.data() + tonePos + static_cast<size_t>(i) * 36;
        FACTORY_TONES[i].id = static_cast<uint16_t>(i);
        copyTrimmedAscii(FACTORY_TONES[i].name, sizeof(FACTORY_TONES[i].name), t, 16);
        FACTORY_TONES[i].category = t[16];
        FACTORY_TONES[i].engineType = t[17];

        if (FACTORY_TONES[i].name[0] == '\0') {
            bool foundMeta = false;
            for (const auto& meta : kSupplementalToneMeta) {
                if (meta.id == i) {
                    std::snprintf(FACTORY_TONES[i].name, sizeof(FACTORY_TONES[i].name), "%s", meta.label);
                    FACTORY_TONES[i].category = meta.cat;
                    FACTORY_TONES[i].engineType = meta.engineType;
                    foundMeta = true;
                    break;
                }
            }
            if (!foundMeta) {
                std::snprintf(FACTORY_TONES[i].name, sizeof(FACTORY_TONES[i].name), "Tone %03d", i);
            }
        }
    }

    // 2. Parse KIT chunk (0x2fdf70 + 32, 128 kits * 0x520 bytes)
    constexpr size_t kitPos = 0x2FDF70 + 32;
    for (int kidx = 0; kidx < NUM_KITS; ++kidx) {
        const uint8_t* k = unpacked.data() + kitPos + static_cast<size_t>(kidx) * 0x520;
        auto& dstKit = FACTORY_KITS[kidx];
        dstKit.number = static_cast<uint8_t>(kidx + 1);
        copyTrimmedAscii(dstKit.name, sizeof(dstKit.name), k, 16);
        if (dstKit.name[0] == '\0')
            std::snprintf(dstKit.name, sizeof(dstKit.name), "KIT %03d", kidx + 1);

        for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
            const uint8_t* slot = k + 0x184 + static_cast<size_t>(ch) * 0x34;
            const uint8_t* fx   = k + 0x3C0 + static_cast<size_t>(ch) * 0x20;
            auto& cp = dstKit.channels[ch];
            uint16_t tid = readLE16(slot);
            cp.toneId  = (tid < NUM_TONES) ? tid : 0;
            cp.tune    = slot[2];
            cp.decay   = slot[3];
            cp.level   = slot[4];
            cp.gain    = slot[5];
            cp.pan     = slot[6];
            cp.revSend = slot[7];
            cp.dlySend = slot[8];
            cp.ctrl    = slot[28];
            cp.fxType  = fx[0];
        }
    }

    // 3. Parse PTN chunk (0x000350 + 32, 128 patterns * 0x5fb8 bytes)
    constexpr size_t ptnPos = 0x000350 + 32;
    for (int pidx = 0; pidx < NUM_PATTERNS; ++pidx) {
        const uint8_t* p = unpacked.data() + ptnPos + static_cast<size_t>(pidx) * 0x5FB8;
        auto& dstPtn = FACTORY_PATTERNS[pidx];
        dstPtn.number = static_cast<uint8_t>(pidx + 1);
        int bank = (pidx / 16) + 1;
        int slot = (pidx % 16) + 1;
        std::snprintf(dstPtn.code, sizeof(dstPtn.code), "%d-%02d", bank, slot);
        copyTrimmedAscii(dstPtn.name, sizeof(dstPtn.name), p, 16);
        if (dstPtn.name[0] == '\0')
            std::snprintf(dstPtn.name, sizeof(dstPtn.name), "PTN %d-%02d", bank, slot);

        uint16_t tempoRaw = readLE16(p + 16);
        dstPtn.tempoTenths = (tempoRaw >= 400 && tempoRaw <= 3000) ? tempoRaw : 1280;
        uint8_t kitNo = p[18];
        dstPtn.kitNumber = (kitNo >= 1 && kitNo <= NUM_KITS) ? kitNo : 1;

        for (int v = 0; v < NUM_VARIATIONS; ++v) {
            const uint8_t* var = p + 0x90 + static_cast<size_t>(v) * 0x984;
            for (int trk = 0; trk <= NUM_CHANNELS; ++trk) {
                for (int s = 0; s < NUM_STEPS; ++s) {
                    size_t stepOff = 4 + static_cast<size_t>(trk) * 64 + static_cast<size_t>(s) * 4;
                    dstPtn.variations[v].velocity[trk][s] = var[stepOff];
                    dstPtn.variations[v].flags[trk][s]    = var[stepOff + 1];
                }
            }
        }
    }

    gFirmwareLoaded = true;
    return true;
}

bool RuntimeFirmwareLoader::parseTarArchive(const void* data, size_t size, juce::String& errorMsg) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    size_t pos = 0;
    while (pos + 512 <= size) {
        const uint8_t* hdr = bytes + pos;
        if (hdr[0] == '\0')
            break;

        char nameBuf[101] {};
        std::memcpy(nameBuf, hdr, 100);
        juce::String entryName(nameBuf);

        // Parse octal size at offset 124 (12 bytes)
        char sizeOctal[13] {};
        std::memcpy(sizeOctal, hdr + 124, 12);
        size_t fileSize = static_cast<size_t>(std::strtoull(sizeOctal, nullptr, 8));

        pos += 512;
        if (pos + fileSize > size)
            break;

        if (entryName.containsIgnoreCase("init_param.bin") || (fileSize >= 0x40 && std::memcmp(bytes + pos, "INIT", 4) == 0)) {
            return parseInitParamBin(bytes + pos, fileSize, errorMsg);
        }

        size_t paddedSize = ((fileSize + 511) / 512) * 512;
        pos += paddedSize;
    }

    errorMsg = "Could not find rpg42_init_param.bin inside TAR archive (TR8S_UP.bin).";
    return false;
}

bool RuntimeFirmwareLoader::loadFromFile(const juce::File& file, juce::String& errorMsg, bool savePreference) {
    initializeCleanDefaults();

    if (!file.existsAsFile()) {
        errorMsg = "Selected file does not exist: " + file.getFullPathName();
        return false;
    }

    bool ok = false;
    if (file.hasFileExtension(".zip")) {
        juce::ZipFile zip(file);
        for (int i = 0; i < zip.getNumEntries(); ++i) {
            const auto* entry = zip.getEntry(i);
            if (entry != nullptr && (entry->filename.endsWithIgnoreCase("TR8S_UP.bin") || entry->filename.endsWithIgnoreCase(".bin"))) {
                std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(i));
                if (stream != nullptr) {
                    juce::MemoryBlock mb;
                    stream->readIntoMemoryBlock(mb);
                    if (mb.getSize() >= 4 && std::memcmp(mb.getData(), "INIT", 4) == 0)
                        ok = parseInitParamBin(static_cast<const uint8_t*>(mb.getData()), mb.getSize(), errorMsg);
                    else
                        ok = parseTarArchive(mb.getData(), mb.getSize(), errorMsg);
                    if (ok) break;
                }
            }
        }
        if (!ok && errorMsg.isEmpty())
            errorMsg = "No valid TR8S_UP.bin found inside the selected .zip archive.";
    } else {
        juce::MemoryBlock mb;
        if (!file.loadFileAsData(mb) || mb.getSize() < 64) {
            errorMsg = "Unable to read firmware binary file.";
            return false;
        }
        if (std::memcmp(mb.getData(), "INIT", 4) == 0)
            ok = parseInitParamBin(static_cast<const uint8_t*>(mb.getData()), mb.getSize(), errorMsg);
        else
            ok = parseTarArchive(mb.getData(), mb.getSize(), errorMsg);
    }

    if (ok) {
        gLoadedFirmwareName = file.getFileName();
        if (savePreference) {
            getSavedFirmwarePathFile().replaceWithText(file.getFullPathName());
        }
    }
    return ok;
}

} // namespace TR8SFirmware
