#pragma once

#include <cstddef>
#include <cstdint>

namespace KisaConfig {
// iType -> raw image geometry, source DB (selects preprocessing) and preferred model.
struct Sensor {
    int type;
    int width; // [px]
    int height; // [px]
    const char* database;
    const char* model;
};
inline constexpr Sensor Sensors[] = {
    {1, 448, 478, "FVC2000_DB3_B", "fvc2000_db3"},
    {2, 300, 300, "FVC2002_DB3_B", "fvc2002_db3"},
    {3, 640, 480, "FVC2004_DB1_B", "fvc2004_db1"},
};

// Searched next to the DLL (models/ first, then the DLL folder), in this order, for each sensor:
// <model>.bin, then combined.bin. Production (30-output) weights from FingerprintTrain only;
// the pretrained encoder cannot create templates.
inline constexpr const char* ModelFolder = "models";
inline constexpr const char* CombinedModel = "combined";
inline constexpr const char* WeightExtension = ".bin";

// Template layout: 16-byte header + the encoder's own versioned template.
inline constexpr char Magic[4] = {'D', 'P', 'K', '2'};
inline constexpr std::size_t HeaderSize = 16; // magic, iType, payload bytes, reserved [uint32 each]
inline constexpr std::size_t MaxPayloadSize = 16384; // [bytes] Encoder template limit.
inline constexpr int MaxTemplateSize = 1000000; // [bytes] Spec limit.

// Error codes reported through piErrorCode.
enum Error : int {
    Ok = 0,
    InvalidArgument = 1,
    UnsupportedType = 2,
    ModelUnavailable = 3,
    PreprocessFailed = 4,
    EncodeFailed = 5,
    InvalidTemplate = 6,
    TypeMismatch = 7,
    CompareFailed = 8,
    Internal = 9,
};
}
