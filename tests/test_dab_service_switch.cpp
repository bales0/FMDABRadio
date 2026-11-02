#include <assert.h>
#include <stdint.h>
#include <initializer_list>

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

  // Same-mux and cross-mux requests both stop existing streams first.
  assert(dispatchRequest(true, true, false, true) == State::StopData);
  assert(dispatchRequest(false, true, false, true) == State::StopAudio);
  assert(dispatchRequest(true, true, true, true) == State::StopData);
  assert(dispatchRequest(true, false, true, true) == State::StopData);
  assert(afterDataStop(true, true) == State::StopAudio);
  assert(afterDataStop(true, false) == State::ServiceSettle);
  assert(afterDataStop(false, false) == State::Ready);
  assert(afterDataStop(false, true) == State::Ready);

  // Successful STOP and device error 0x03 follow the same safe next step;
  // unrelated device errors and transport failures never confirm a STOP.
  assert(stopConfirmed(true, false, 0U));
  assert(stopConfirmed(false, true, 0x03U));
  assert(!stopConfirmed(false, true, 0x18U));
  assert(!stopConfirmed(false, false, 0x03U));

  for (Resume resume : {Resume::StopData, Resume::StopAudio}) {
    Controller stop;
    for (uint8_t attempt = 1U; attempt <= MAX_STOP_RETRIES; ++attempt) {
      assert(stop.backoff(resume, stop.stopRetries, MAX_STOP_RETRIES, 100U));
      assert(stop.state == State::Backoff && stop.resume == resume);
      assert(stop.stopRetries == attempt);
      assert(!stop.ready(100U + RETRY_BACKOFF_MS * attempt - 1U));
      assert(stop.ready(100U + RETRY_BACKOFF_MS * attempt));
    }
    assert(!stop.backoff(resume, stop.stopRetries, MAX_STOP_RETRIES, 100U));
    assert(stop.state == State::Backoff); // No implicit START on exhaustion.
  }

  // STOP settle and audio-to-data settle each last the full 200 ms.
  controller.settle(State::ServiceSettle, 1000U, STOP_SETTLE_MS);
  assert(!controller.ready(1199U));
  assert(controller.ready(1200U));
  controller.settle(State::AudioSettle, 2000U, AUDIO_TO_DATA_SETTLE_MS);
  assert(!controller.ready(2199U));
  assert(controller.ready(2200U));

  // If a controller request is superseded during STOP or settle, completions
  // bound to the previous request cannot belong to the new request.
  for (State state : {State::WaitDataStop, State::WaitAudioStop,
                      State::ServiceSettle}) {
    controller.requestSwitch();
    controller.state = state;
    controller.bindCommand();
    controller.requestSwitch();
    assert(!controller.commandIsCurrent());
    assert(controller.state == State::RequestSwitch);
    assert(controller.stopRetries == 0U);
    assert(controller.resume == Resume::None);
  }
  controller.requestId = 0xFFFFFFFFU;
  controller.requestSwitch();
  assert(controller.requestId == 1U);
  return 0;
}
