#pragma once
#include <stdint.h>

// Board-wide limits, not the unrelated Si4684 external-flash SPI interface.
// Overrides may only lower the clock; overclock builds are intentionally barred.
#ifndef FMDAB_TFT_SPI_HZ
#define FMDAB_TFT_SPI_HZ (1000000000UL / 66UL)
#endif
#ifndef FMDAB_RADIO_SPI_HZ
#define FMDAB_RADIO_SPI_HZ 10000000UL
#endif
#ifndef FMDAB_EEPROM_I2C_HZ
#define FMDAB_EEPROM_I2C_HZ 400000UL
#endif

namespace board_bus {
constexpr uint32_t TFT_SPI_MAX_HZ = 1000000000UL / 66UL; // ST7735R write: >=66ns
constexpr uint32_t RADIO_SPI_MAX_HZ = 10000000UL;        // Si4684-A10 Table 3
constexpr uint32_t EEPROM_I2C_MAX_HZ = 400000UL;         // classic ESP32 limit
constexpr uint32_t TFT_SPI_HZ = FMDAB_TFT_SPI_HZ;
constexpr uint32_t RADIO_SPI_HZ = FMDAB_RADIO_SPI_HZ;
constexpr uint32_t EEPROM_I2C_HZ = FMDAB_EEPROM_I2C_HZ;
static_assert(TFT_SPI_HZ > 0 && TFT_SPI_HZ <= TFT_SPI_MAX_HZ, "TFT overclock forbidden");
static_assert(RADIO_SPI_HZ > 0 && RADIO_SPI_HZ <= RADIO_SPI_MAX_HZ, "Radio overclock forbidden");
static_assert(EEPROM_I2C_HZ > 0 && EEPROM_I2C_HZ <= EEPROM_I2C_MAX_HZ, "ESP32 I2C overclock forbidden");
}  // namespace board_bus
