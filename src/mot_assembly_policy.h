#pragma once

#include <stdint.h>
#include <string.h>

namespace mot_assembly {

// Activity indication only: expiry must never discard a partial carousel.
constexpr uint32_t RECEPTION_ACTIVITY_MS = 30000U;
inline bool receptionActive(bool collecting, uint32_t lastAcceptedMs,
                             uint32_t now) {
  return collecting && static_cast<uint32_t>(now - lastAcceptedMs) <
                           RECEPTION_ACTIVITY_MS;
}

inline bool segmentInRange(uint16_t segment, uint16_t maxSegments) {
  return segment < maxSegments;
}

inline bool canAppend(uint32_t receivedBytes, uint16_t dataLength,
                      uint32_t arenaBytes) {
  return dataLength != 0U && receivedBytes <= arenaBytes &&
         static_cast<uint32_t>(dataLength) <= arenaBytes - receivedBytes;
}

inline bool decodeHeaderCore(const uint8_t* data, uint16_t length,
                             uint32_t& bodySize, uint16_t& headerSize) {
  if (data == nullptr || length < 7U) return false;
  bodySize = (static_cast<uint32_t>(data[0]) << 20) |
             (static_cast<uint32_t>(data[1]) << 12) |
             (static_cast<uint32_t>(data[2]) << 4) |
             (static_cast<uint32_t>(data[3]) >> 4);
  headerSize = static_cast<uint16_t>(
      (static_cast<uint16_t>(data[3] & 0x0FU) << 9) |
      (static_cast<uint16_t>(data[4]) << 1) |
      (static_cast<uint16_t>(data[5]) >> 7));
  return bodySize != 0U && headerSize >= 7U;
}

inline bool objectPacketBelongs(bool collecting, uint32_t currentObjectId,
                                uint32_t incomingObjectId,
                                uint16_t segment, bool retired=false) {
  (void)segment;
  return !collecting || currentObjectId == incomingObjectId || !retired;
}

inline bool canAcceptTransport(bool renderOwned) {
  return !renderOwned;
}

inline bool startsNewBodyOverCache(bool cachedImageAvailable,
                                   uint16_t segment) {
  (void)segment;
  return cachedImageAvailable;
}

inline uint16_t totalSegmentsFromLast(uint16_t segment, bool last) {
  return last ? static_cast<uint16_t>(segment + 1U) : 0U;
}

inline bool repeatedZeroCompletes(bool duplicate, uint16_t segment,
                                  uint16_t knownTotal,
                                  uint16_t highestSegment,
                                  bool precedingSegmentsComplete) {
  return duplicate && segment == 0U && knownTotal == 0U &&
         highestSegment > 0U && precedingSegmentsComplete;
}

// Compare a retransmitted segment with the packed bytes before treating it as
// harmless repetition. Adapted from reference acceptSlideshowDuplicate(); the
// caller validates the bitmap index before computing this packed offset.
inline bool duplicateMatches(const uint8_t* arena, uint32_t receivedBytes,
                              uint32_t offset, uint16_t storedLength,
                              const uint8_t* incoming, uint16_t length) {
  return arena && incoming && length != 0U && length == storedLength &&
         offset <= receivedBytes && length <= receivedBytes - offset &&
         memcmp(arena + offset, incoming, length) == 0;
}

inline bool lastBoundaryValid(uint16_t segment, bool last,
                              uint16_t knownTotal, uint16_t highestSegment) {
  if (knownTotal != 0U)
    return segment < knownTotal && (!last || segment + 1U == knownTotal);
  return !last || segment >= highestSegment;
}

}  // namespace mot_assembly
