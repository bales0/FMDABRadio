#include "../src/BusConfig.h"
#include <cassert>
#include <cstdio>

static uint32_t attainable(uint32_t requested) {
  // Independent exhaustive oracle for the classic ESP32 80MHz APB divider.
  // Not a simulation of signal integrity; verifies no clock above the request.
  uint32_t best=0;
  for (uint32_t pre=1;pre<=8192;++pre) for (uint32_t n=2;n<=64;++n) {
    const uint32_t actual=80000000U/(pre*n);
    if (actual<=requested && actual>best) best=actual;
  }
  return best;
}
int main() {
  using namespace board_bus;
  assert(uint64_t(TFT_SPI_HZ)*66U<=1000000000ULL);
  assert(RADIO_SPI_HZ<=10000000U && EEPROM_I2C_HZ<=400000U);
  const auto tft=attainable(TFT_SPI_HZ),radio=attainable(RADIO_SPI_HZ);
  assert(tft<=TFT_SPI_MAX_HZ && radio<=RADIO_SPI_MAX_HZ);
  if (TFT_SPI_HZ==TFT_SPI_MAX_HZ) assert(tft==13333333U);
  if (RADIO_SPI_HZ==RADIO_SPI_MAX_HZ) assert(radio==10000000U);
  printf("Bus configuration PASS: requested TFT=%u effective@80MHz=%u radio=%u I2C=%u\n",
         TFT_SPI_HZ,tft,radio,EEPROM_I2C_HZ);
}
