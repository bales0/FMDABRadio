#ifndef FM_FEATURES_H
#define FM_FEATURES_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace fm_features {

static constexpr uint8_t MAX_AF_COUNT = 25;

struct AfList {
  uint16_t pi = 0;
  uint8_t expected = 0;
  uint8_t count = 0;
  uint16_t frequency10kHz[MAX_AF_COUNT] = {};

  void clear(uint16_t newPi = 0) {
    pi = newPi;
    expected = count = 0;
    memset(frequency10kHz, 0, sizeof(frequency10kHz));
  }

  bool addCode(uint8_t code) {
    if (code >= 224U && code <= 249U) {
      expected = static_cast<uint8_t>(code - 224U);
      return false;
    }
    if (code < 1U || code > 204U) return false;
    const uint16_t frequency = static_cast<uint16_t>(8750U + code * 10U);
    for (uint8_t i = 0; i < count; ++i)
      if (frequency10kHz[i] == frequency) return false;
    if (count >= MAX_AF_COUNT) return false;
    frequency10kHz[count++] = frequency;
    return true;
  }
};

struct ClockTime {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  int8_t localOffsetHalfHours = 0;
};

enum class ClockSampleResult : uint8_t { Candidate, Confirmed, Rejected };

inline bool leapYear(uint16_t year) {
  return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

inline uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30,
                                 31, 31, 30, 31, 30, 31};
  if (month < 1U || month > 12U) return 0;
  return month == 2U && leapYear(year) ? 29U : days[month - 1U];
}

inline bool mjdToDate(uint32_t mjd, uint16_t& year, uint8_t& month,
                      uint8_t& day) {
  if (mjd < 15079U || mjd > 88068U) return false;
  const int32_t j = static_cast<int32_t>(mjd) + 2400001 + 68569;
  int32_t n = (4 * j) / 146097;
  int32_t l = j - (146097 * n + 3) / 4;
  int32_t i = (4000 * (l + 1)) / 1461001;
  l = l - (1461 * i) / 4 + 31;
  int32_t k = (80 * l) / 2447;
  const int32_t d = l - (2447 * k) / 80;
  l = k / 11;
  const int32_t m = k + 2 - 12 * l;
  const int32_t y = 100 * (n - 49) + i + l;
  if (y < 1900 || y > 2099 || m < 1 || m > 12 || d < 1 ||
      d > daysInMonth(static_cast<uint16_t>(y), static_cast<uint8_t>(m)))
    return false;
  year = static_cast<uint16_t>(y);
  month = static_cast<uint8_t>(m);
  day = static_cast<uint8_t>(d);
  return true;
}

inline bool decodeClockTime(uint16_t blockB, uint16_t blockC, uint16_t blockD,
                            ClockTime& output) {
  const uint32_t mjd = (static_cast<uint32_t>(blockB & 0x0003U) << 15) |
                       (static_cast<uint32_t>(blockC) >> 1);
  const uint8_t hour = static_cast<uint8_t>(((blockC & 1U) << 4) |
                                            ((blockD >> 12) & 0x0FU));
  const uint8_t minute = static_cast<uint8_t>((blockD >> 6) & 0x3FU);
  const uint8_t magnitude = static_cast<uint8_t>(blockD & 0x1FU);
  if (hour > 23U || minute > 59U || magnitude > 24U) return false;
  ClockTime parsed;
  if (!mjdToDate(mjd, parsed.year, parsed.month, parsed.day)) return false;
  parsed.hour = hour;
  parsed.minute = minute;
  parsed.localOffsetHalfHours = static_cast<int8_t>(magnitude);
  if ((blockD & 0x0020U) != 0) parsed.localOffsetHalfHours *= -1;
  output = parsed;
  return true;
}

inline bool clockTimeToMinutes(const ClockTime& value, int32_t& minutes) {
  if (value.year < 1900U || value.year > 2099U || value.month < 1U ||
      value.month > 12U || value.day < 1U ||
      value.day > daysInMonth(value.year, value.month) || value.hour > 23U ||
      value.minute > 59U || value.localOffsetHalfHours < -24 ||
      value.localOffsetHalfHours > 24)
    return false;
  int32_t days = 0;
  for (uint16_t year = 1900U; year < value.year; ++year)
    days += leapYear(year) ? 366 : 365;
  for (uint8_t month = 1U; month < value.month; ++month)
    days += daysInMonth(value.year, month);
  days += static_cast<int32_t>(value.day) - 1;
  minutes = days * 1440L + static_cast<int32_t>(value.hour) * 60L +
            value.minute;
  return true;
}

inline bool clockSamplesConsistent(const ClockTime& previous,
                                   const ClockTime& current,
                                   uint32_t elapsedMs,
                                   uint8_t maxAdvanceMinutes = 5U) {
  if (previous.localOffsetHalfHours != current.localOffsetHalfHours ||
      elapsedMs > 10UL * 60UL * 1000UL)
    return false;
  int32_t previousMinutes = 0;
  int32_t currentMinutes = 0;
  if (!clockTimeToMinutes(previous, previousMinutes) ||
      !clockTimeToMinutes(current, currentMinutes))
    return false;
  const int32_t advance = currentMinutes - previousMinutes;
  const uint32_t elapsedMinutes = elapsedMs / 60000UL;
  const uint32_t allowed =
      (elapsedMinutes + 1U) < maxAdvanceMinutes ? elapsedMinutes + 1U
                                                : maxAdvanceMinutes;
  return advance >= 0 && static_cast<uint32_t>(advance) <= allowed;
}

inline void applyLocalOffset(ClockTime& value) {
  int16_t minutes = static_cast<int16_t>(value.hour) * 60 + value.minute +
                    static_cast<int16_t>(value.localOffsetHalfHours) * 30;
  while (minutes < 0) {
    minutes += 1440;
    if (value.day > 1U) {
      --value.day;
    } else {
      if (value.month > 1U) --value.month;
      else {
        value.month = 12U;
        --value.year;
      }
      value.day = daysInMonth(value.year, value.month);
    }
  }
  while (minutes >= 1440) {
    minutes -= 1440;
    const uint8_t lastDay = daysInMonth(value.year, value.month);
    if (value.day < lastDay) ++value.day;
    else {
      value.day = 1U;
      if (value.month < 12U) ++value.month;
      else {
        value.month = 1U;
        ++value.year;
      }
    }
  }
  value.hour = static_cast<uint8_t>(minutes / 60);
  value.minute = static_cast<uint8_t>(minutes % 60);
}

struct ClockValidator {
  bool candidateValid = false;
  uint16_t candidatePi = 0U;
  uint32_t candidateTimestampMs = 0U;
  ClockTime candidate;

  void reset() {
    candidateValid = false;
    candidatePi = 0U;
    candidateTimestampMs = 0U;
    candidate = ClockTime{};
  }

  ClockSampleResult ingest(const ClockTime& decoded, uint16_t pi, uint32_t now,
                           ClockTime& confirmed) {
    if (pi == 0U) {
      reset();
      return ClockSampleResult::Rejected;
    }
    if (!candidateValid || candidatePi != pi) {
      candidate = decoded;
      candidatePi = pi;
      candidateTimestampMs = now;
      candidateValid = true;
      return ClockSampleResult::Candidate;
    }
    const uint32_t elapsed = now - candidateTimestampMs;
    if (!clockSamplesConsistent(candidate, decoded, elapsed)) {
      candidate = decoded;
      candidateTimestampMs = now;
      return ClockSampleResult::Rejected;
    }
    candidate = decoded;
    candidateTimestampMs = now;
    confirmed = decoded;
    return ClockSampleResult::Confirmed;
  }
};

inline const char* ptyName(uint8_t pty, bool rbds) {
  static const char* const rds[32] = {
      "None", "News", "Current Affairs", "Information", "Sport", "Education",
      "Drama", "Culture", "Science", "Varied", "Pop Music", "Rock Music",
      "Easy Listening", "Light Classics", "Serious Classics", "Other Music",
      "Weather", "Finance", "Children", "Social Affairs", "Religion",
      "Phone In", "Travel", "Leisure", "Jazz", "Country", "National Music",
      "Oldies", "Folk Music", "Documentary", "Alarm Test", "Alarm"};
  static const char* const rbdsNames[32] = {
      "None", "News", "Information", "Sports", "Talk", "Rock", "Classic Rock",
      "Adult Hits", "Soft Rock", "Top 40", "Country", "Oldies", "Soft",
      "Nostalgia", "Jazz", "Classical", "Rhythm and Blues", "Soft R&B",
      "Foreign Language", "Religious Music", "Religious Talk", "Personality",
      "Public", "College", "Spanish Talk", "Spanish Music", "Hip Hop",
      "Unassigned", "Unassigned", "Weather", "Emergency Test", "Emergency"};
  return pty < 32U ? (rbds ? rbdsNames[pty] : rds[pty]) : "";
}

inline uint8_t nextDabChannel(uint8_t current, bool forward) {
  current = static_cast<uint8_t>(current % 38U);
  return forward ? static_cast<uint8_t>((current + 1U) % 38U)
                 : static_cast<uint8_t>(current == 0U ? 37U : current - 1U);
}

}  // namespace fm_features

#endif
