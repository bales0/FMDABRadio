#include <assert.h>
#include <stdint.h>

#include "../src/mot_assembly_policy.h"

int main() {
  using namespace mot_assembly;

  assert(segmentInRange(0U, 256U));
  assert(segmentInRange(255U, 256U));
  assert(!segmentInRange(256U, 256U));
  assert(!segmentInRange(32767U, 256U));

  assert(totalSegmentsFromLast(0U, false) == 0U);
  assert(totalSegmentsFromLast(0U, true) == 1U);
  assert(totalSegmentsFromLast(255U, true) == 256U);

  assert(repeatedZeroCompletes(true, 0U, 0U, 3U, true));
  assert(!repeatedZeroCompletes(false, 0U, 0U, 3U, true));
  assert(!repeatedZeroCompletes(true, 1U, 0U, 3U, true));
  assert(!repeatedZeroCompletes(true, 0U, 4U, 3U, true));
  assert(!repeatedZeroCompletes(true, 0U, 0U, 3U, false));

  assert(objectPacketBelongs(false, 1U, 2U, 7U));
  assert(objectPacketBelongs(true, 1U, 1U, 7U));
  assert(objectPacketBelongs(true, 1U, 2U, 0U));
  assert(!objectPacketBelongs(true, 1U, 2U, 1U));

  // A published image is immutable until UI acknowledgement. Afterwards the
  // completed Transport ID is still rejected, while segment zero of a new ID
  // is allowed to take ownership of the single arena.
  assert(!canAcceptTransport(true, true, 7U, 8U));
  assert(!canAcceptTransport(false, true, 7U, 7U));
  assert(canAcceptTransport(false, true, 7U, 8U));
  assert(startsNewBodyOverCache(true, 0U));
  assert(!startsNewBodyOverCache(true, 1U));
  assert(!startsNewBodyOverCache(false, 0U));

  const uint32_t arena = 50UL * 1024UL;
  assert(canAppend(0U, 1U, arena));
  assert(canAppend(arena - 2048U, 2048U, arena));
  assert(!canAppend(arena - 2047U, 2048U, arena));
  assert(!canAppend(arena, 1U, arena));
  assert(!canAppend(arena + 1U, 1U, arena));
  assert(!canAppend(0U, 0U, arena));

  uint8_t headerCore[7] = {0};
  const uint32_t encodedBodySize = 0x12345UL;
  const uint16_t encodedHeaderSize = 7U;
  headerCore[0] = static_cast<uint8_t>(encodedBodySize >> 20);
  headerCore[1] = static_cast<uint8_t>(encodedBodySize >> 12);
  headerCore[2] = static_cast<uint8_t>(encodedBodySize >> 4);
  headerCore[3] = static_cast<uint8_t>((encodedBodySize << 4) |
      ((encodedHeaderSize >> 9) & 0x0FU));
  headerCore[4] = static_cast<uint8_t>(encodedHeaderSize >> 1);
  headerCore[5] = static_cast<uint8_t>((encodedHeaderSize & 1U) << 7);
  uint32_t decodedBodySize = 0;
  uint16_t decodedHeaderSize = 0;
  assert(decodeHeaderCore(headerCore, sizeof(headerCore), decodedBodySize,
                          decodedHeaderSize));
  assert(decodedBodySize == encodedBodySize);
  assert(decodedHeaderSize == encodedHeaderSize);
  assert(!decodeHeaderCore(headerCore, 6U, decodedBodySize,
                           decodedHeaderSize));
  return 0;
}
