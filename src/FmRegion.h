#ifndef FMDABRADIO_FMREGION_H
#define FMDABRADIO_FMREGION_H

#include <Arduino.h>

enum class FmRegion : uint8_t {
  Europe = 0,
  NorthAmerica = 1,
  Japan = 2
};

constexpr uint8_t FM_REGION_COUNT = 3;

struct FmRegionProfile {
  const char* menuName;
  uint16_t minFrequency10kHz;
  uint16_t maxFrequency10kHz;
  uint8_t seekSpacing10kHz;
  uint8_t deEmphasis;
  bool rbds;
};

// DAB is intentionally not part of this profile and is never changed here.
// 100/200 kHz spacing also preserves the existing 0.1 MHz FM EEPROM format.
constexpr FmRegionProfile FM_REGION_PROFILES[FM_REGION_COUNT] = {
    {"Europe",    8750, 10800, 10, 1, false}, // 87.5-108.0 MHz, 50 us
    {"N.America", 8790, 10790, 20, 0, true }, // 87.9-107.9 MHz, 75 us / RBDS
    {"Japan",     7600,  9500, 10, 1, false}  // 76.0-95.0 MHz, 50 us
};

inline uint8_t sanitizeFmRegion(uint8_t value) {
  return value < FM_REGION_COUNT ? value
                                 : static_cast<uint8_t>(FmRegion::Europe);
}

inline const FmRegionProfile& fmRegionProfile(uint8_t value) {
  return FM_REGION_PROFILES[sanitizeFmRegion(value)];
}

#endif
