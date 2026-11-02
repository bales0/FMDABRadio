#include <assert.h>
#include <stdint.h>

#include "../src/mot_assembly_policy.h"

int main() {
  using namespace mot_assembly;

  assert(receptionActive(true, 0U, 0U));
  assert(receptionActive(true, 100U, 30099U));
  assert(!receptionActive(true, 100U, 30100U));
  assert(!receptionActive(true, 100U, 180100U));
  assert(!receptionActive(false, 100U, 101U));
  assert(receptionActive(true, 0xFFFFFF00U, 0xFFFFFF00U + 29999U));
  assert(!receptionActive(true, 0xFFFFFF00U, 0xFFFFFF00U + 30000U));

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
  assert(objectPacketBelongs(true, 1U, 2U, 1U));
  assert(!objectPacketBelongs(true, 1U, 2U, 1U, true));

  // Only an explicit render lease blocks writes. A notification is reusable,
  // and any valid segment can begin a new TID over the cached image.
  assert(!canAcceptTransport(true));
  assert(canAcceptTransport(false));
  assert(startsNewBodyOverCache(true, 0U));
  assert(startsNewBodyOverCache(true, 1U));
  assert(startsNewBodyOverCache(true, 31U));
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
  const uint8_t packed[] = {1U, 2U, 3U, 4U};
  const uint8_t same[] = {3U, 4U};
  const uint8_t conflict[] = {3U, 5U};
  assert(duplicateMatches(packed, 4U, 2U, 2U, same, 2U));
  assert(!duplicateMatches(packed, 4U, 2U, 2U, conflict, 2U));
  assert(!duplicateMatches(packed, 4U, 2U, 1U, same, 2U));
  assert(!duplicateMatches(packed, 4U, 3U, 2U, same, 2U));
  assert(!duplicateMatches(packed, 4U, UINT32_MAX, 2U, same, 2U));
  assert(!duplicateMatches(nullptr, 4U, 2U, 2U, same, 2U));
  assert(!duplicateMatches(packed, 4U, 2U, 2U, nullptr, 2U));
  assert(!duplicateMatches(packed, 4U, 2U, 0U, same, 0U));
  assert(lastBoundaryValid(3U, true, 0U, 3U));
  assert(!lastBoundaryValid(2U, true, 0U, 3U));
  assert(lastBoundaryValid(3U, true, 4U, 3U));
  assert(lastBoundaryValid(0U, false, 4U, 3U));
  assert(!lastBoundaryValid(4U, false, 4U, 3U));
  assert(!lastBoundaryValid(2U, true, 4U, 3U));
  assert(lastBoundaryValid(255U, true, 256U, 255U));
  // A second LAST cannot shorten or extend the known final segment count.
  assert(!lastBoundaryValid(254U, true, 256U, 255U));
  assert(!lastBoundaryValid(256U, true, 256U, 255U));
  // Safe packed insertion arithmetic when a missing interior segment arrives.
  assert(canAppend(19U * 32U, 32U, arena));
  assert(!duplicateMatches(packed, 4U, 0U, 4U, packed, 3U));
  return 0;
}
