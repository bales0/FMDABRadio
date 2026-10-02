#pragma once

#include <stdint.h>

namespace mot_assembly {

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
                                uint16_t segment) {
  return !collecting || currentObjectId == incomingObjectId || segment == 0U;
}

inline bool canAcceptTransport(bool publishedPending,
                               bool completedTransportValid,
                               uint32_t completedTransportId,
                               uint32_t incomingTransportId) {
  return !publishedPending &&
         (!completedTransportValid ||
          completedTransportId != incomingTransportId);
}

inline bool startsNewBodyOverCache(bool cachedImageAvailable,
                                   uint16_t segment) {
  return cachedImageAvailable && segment == 0U;
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

}  // namespace mot_assembly
