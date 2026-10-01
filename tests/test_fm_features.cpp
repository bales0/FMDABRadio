#include <assert.h>
#include <stdint.h>

#include "../src/FmRegion.h"
#include "../src/fm_af_policy.h"
#include "../src/fm_features.h"

int main() {
  using namespace fm_features;

  AfList af;
  af.clear(0x1234);
  assert(!af.addCode(225));
  assert(af.expected == 1);
  assert(af.addCode(1));
  assert(af.frequency10kHz[0] == 8760);
  assert(!af.addCode(1));

  ClockTime utc;
  // MJD 60310 (2024-01-01), 12:34 UTC, +1 hour local offset.
  assert(decodeClockTime(0x0001, 0xD72C, 0xC882, utc));
  assert(utc.hour <= 23 && utc.minute <= 59);

  ClockValidator validator;
  ClockTime confirmed;
  assert(validator.ingest(utc, 0x1234, 1000, confirmed) ==
         ClockSampleResult::Candidate);
  assert(validator.ingest(utc, 0x1234, 2000, confirmed) ==
         ClockSampleResult::Confirmed);

  ClockTime rollover;
  rollover.year = 2024;
  rollover.month = 12;
  rollover.day = 31;
  rollover.hour = 23;
  rollover.minute = 50;
  rollover.localOffsetHalfHours = 2;
  applyLocalOffset(rollover);
  assert(rollover.year == 2025 && rollover.month == 1 && rollover.day == 1);
  assert(rollover.hour == 0 && rollover.minute == 50);

  assert(fm_af::weakSignal(true, 15, 2, 20, 4));
  assert(fm_af::candidateIsBetter(true, 0x1234, 0x1234, 15, 5, 22, 4));
  assert(!fm_af::candidateIsBetter(true, 0x1234, 0x5678, 15, 5, 30, 9));

  assert(stepFmFrequency(10800, 10,
                         static_cast<uint8_t>(FmRegion::Europe)) == 8750);
  assert(nextDabChannel(37, true) == 0);
  assert(ptyName(1, false)[0] == 'N');
  return 0;
}
