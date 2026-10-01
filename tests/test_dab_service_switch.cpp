#include <assert.h>
#include <stdint.h>

#include "../src/dab_service_switch_policy.h"

int main() {
  using namespace dab_switch;
  Controller controller;
  controller.requestSwitch();
  const uint32_t first = controller.requestId;
  controller.bindCommand();
  assert(controller.commandIsCurrent());
  controller.requestSwitch();
  assert(controller.requestId != first);
  assert(!controller.commandIsCurrent());

  controller.settle(State::AudioSettle, 0xFFFFFFF0U, 32U);
  assert(!controller.ready(0x00000008U));
  assert(controller.ready(0x00000010U));

  uint8_t retries = 0;
  assert(controller.backoff(Resume::StartAudio, retries, 3U, 100U));
  assert(retries == 1U && controller.notBeforeMs == 350U);
  assert(dispatchRequest(true, true, true, true) == State::StopData);
  assert(audioPadSlsContextValid(true, false));
  assert(!audioPadSlsContextValid(true, true));
  return 0;
}
