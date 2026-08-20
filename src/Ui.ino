constexpr uint8_t UI_LIST_ROWS = 7;
constexpr uint8_t UI_TEXT_LINE_GLYPHS = 26;
constexpr uint8_t UI_TEXT_WINDOW_GLYPHS = UI_TEXT_LINE_GLYPHS * 2;
constexpr uint16_t UI_TEXT_SCROLL_STEP_MS = 220;
const uint16_t UI_DIM_TIMEOUT_SECONDS[] = {15, 30, 60, 120};
constexpr uint16_t UI_GRAY = 0x8410;
constexpr uint16_t UI_DARK_LINE = 0x3186;
constexpr uint16_t UI_ORANGE = 0xFD20;
const char* const UI_DAB_CHANNELS[DAB_FREQS] = {
    "5A", "5B", "5C", "5D", "6A", "6B", "6C", "6D",
    "7A", "7B", "7C", "7D", "8A", "8B", "8C", "8D",
    "9A", "9B", "9C", "9D", "10A", "10B", "10C", "10D",
    "11A", "11B", "11C", "11D", "12A", "12B", "12C", "12D",
    "13A", "13B", "13C", "13D", "13E", "13F"};

uint32_t nextUtf8Codepoint(const String& text, uint16_t& offset) {
  if (offset >= text.length()) return 0;
  const uint8_t first = static_cast<uint8_t>(text[offset++]);
  if (first < 0x80) return first;
  uint8_t remaining = 0;
  uint32_t codepoint = 0;
  if ((first & 0xE0) == 0xC0) {
    remaining = 1;
    codepoint = first & 0x1F;
  } else if ((first & 0xF0) == 0xE0) {
    remaining = 2;
    codepoint = first & 0x0F;
  } else if ((first & 0xF8) == 0xF0) {
    remaining = 3;
    codepoint = first & 0x07;
  } else {
    return '?';
  }
  while (remaining-- != 0) {
    if (offset >= text.length()) return '?';
    const uint8_t continuation = static_cast<uint8_t>(text[offset++]);
    if ((continuation & 0xC0) != 0x80) return '?';
    codepoint = (codepoint << 6) | (continuation & 0x3F);
  }
  return codepoint;
}

UiGlyph uiGlyph(uint32_t codepoint) {
  if (codepoint >= 32 && codepoint <= 126) {
    return {static_cast<char>(codepoint), UiAccent::None};
  }
  switch (codepoint) {
    case 0x00AB: return {0, UiAccent::None, UiSpecialGlyph::LeftGuillemet};
    case 0x00BB: return {0, UiAccent::None, UiSpecialGlyph::RightGuillemet};
    case 0x00A1: return {0, UiAccent::None, UiSpecialGlyph::InvertedExclamation};
    case 0x00BF: return {0, UiAccent::None, UiSpecialGlyph::InvertedQuestion};
    case 0x00A3: return {0, UiAccent::None, UiSpecialGlyph::Pound};
    case 0x20AC: return {0, UiAccent::None, UiSpecialGlyph::Euro};
    case 0x00A9: return {0, UiAccent::None, UiSpecialGlyph::Copyright};
    case 0x00B0: return {'o', UiAccent::Ring};
    case 0x00AF: return {0, UiAccent::None, UiSpecialGlyph::Macron};
    case 0x2015: return {0, UiAccent::None, UiSpecialGlyph::HorizontalBar};
    case 0x00C6: return {0, UiAccent::None, UiSpecialGlyph::AEUpper};
    case 0x00E6: return {0, UiAccent::None, UiSpecialGlyph::AELower};
    case 0x0152: return {0, UiAccent::None, UiSpecialGlyph::OEUpper};
    case 0x0153: return {0, UiAccent::None, UiSpecialGlyph::OELower};
    case 0x00D0: return {0, UiAccent::None, UiSpecialGlyph::EthUpper};
    case 0x00F0: return {0, UiAccent::None, UiSpecialGlyph::EthLower};
    case 0x00DE: return {0, UiAccent::None, UiSpecialGlyph::ThornUpper};
    case 0x00FE: return {0, UiAccent::None, UiSpecialGlyph::ThornLower};
    case 0x00DF: return {0, UiAccent::None, UiSpecialGlyph::SharpS};
    case 0x014A: return {0, UiAccent::None, UiSpecialGlyph::EngUpper};
    case 0x014B: return {0, UiAccent::None, UiSpecialGlyph::EngLower};
    case 0x0132: return {0, UiAccent::None, UiSpecialGlyph::IJUpper};
    case 0x0133: return {0, UiAccent::None, UiSpecialGlyph::IJLower};
    case 0x03B2: return {0, UiAccent::None, UiSpecialGlyph::Beta};

    case 0x00C0: return {'A', UiAccent::Grave};
    case 0x00C1: return {'A', UiAccent::Acute};
    case 0x00C2: return {'A', UiAccent::Circumflex};
    case 0x00C3: return {'A', UiAccent::Tilde};
    case 0x00C4: return {'A', UiAccent::Umlaut};
    case 0x00C5: return {'A', UiAccent::Ring};
    case 0x00C7: return {'C', UiAccent::Cedilla};
    case 0x00C8: return {'E', UiAccent::Grave};
    case 0x00C9: return {'E', UiAccent::Acute};
    case 0x00CA: return {'E', UiAccent::Circumflex};
    case 0x00CB: return {'E', UiAccent::Umlaut};
    case 0x00CC: return {'I', UiAccent::Grave};
    case 0x00CD: return {'I', UiAccent::Acute};
    case 0x00CE: return {'I', UiAccent::Circumflex};
    case 0x00CF: return {'I', UiAccent::Umlaut};
    case 0x00D1: return {'N', UiAccent::Tilde};
    case 0x00D2: return {'O', UiAccent::Grave};
    case 0x00D3: return {'O', UiAccent::Acute};
    case 0x00D4: return {'O', UiAccent::Circumflex};
    case 0x00D5: return {'O', UiAccent::Tilde};
    case 0x00D6: return {'O', UiAccent::Umlaut};
    case 0x00D8: return {'O', UiAccent::Stroke};
    case 0x00D9: return {'U', UiAccent::Grave};
    case 0x00DA: return {'U', UiAccent::Acute};
    case 0x00DB: return {'U', UiAccent::Circumflex};
    case 0x00DC: return {'U', UiAccent::Umlaut};
    case 0x00DD: return {'Y', UiAccent::Acute};
    case 0x00E0: return {'a', UiAccent::Grave};
    case 0x00E1: return {'a', UiAccent::Acute};
    case 0x00E2: return {'a', UiAccent::Circumflex};
    case 0x00E3: return {'a', UiAccent::Tilde};
    case 0x00E4: return {'a', UiAccent::Umlaut};
    case 0x00E5: return {'a', UiAccent::Ring};
    case 0x00E7: return {'c', UiAccent::Cedilla};
    case 0x00E8: return {'e', UiAccent::Grave};
    case 0x00E9: return {'e', UiAccent::Acute};
    case 0x00EA: return {'e', UiAccent::Circumflex};
    case 0x00EB: return {'e', UiAccent::Umlaut};
    case 0x00EC: return {'i', UiAccent::Grave};
    case 0x00ED: return {'i', UiAccent::Acute};
    case 0x00EE: return {'i', UiAccent::Circumflex};
    case 0x00EF: return {'i', UiAccent::Umlaut};
    case 0x00F1: return {'n', UiAccent::Tilde};
    case 0x00F2: return {'o', UiAccent::Grave};
    case 0x00F3: return {'o', UiAccent::Acute};
    case 0x00F4: return {'o', UiAccent::Circumflex};
    case 0x00F5: return {'o', UiAccent::Tilde};
    case 0x00F6: return {'o', UiAccent::Umlaut};
    case 0x00F8: return {'o', UiAccent::Stroke};
    case 0x00F9: return {'u', UiAccent::Grave};
    case 0x00FA: return {'u', UiAccent::Acute};
    case 0x00FB: return {'u', UiAccent::Circumflex};
    case 0x00FC: return {'u', UiAccent::Umlaut};
    case 0x00FD: return {'y', UiAccent::Acute};
    case 0x00FF: return {'y', UiAccent::Umlaut};

    case 0x0100: return {'A', UiAccent::Macron};
    case 0x0101: return {'a', UiAccent::Macron};
    case 0x0102: return {'A', UiAccent::Breve};
    case 0x0103: return {'a', UiAccent::Breve};
    case 0x0104: return {'A', UiAccent::Ogonek};
    case 0x0105: return {'a', UiAccent::Ogonek};
    case 0x0106: return {'C', UiAccent::Acute};
    case 0x0107: return {'c', UiAccent::Acute};
    case 0x010A: return {'C', UiAccent::DotAbove};
    case 0x010B: return {'c', UiAccent::DotAbove};
    case 0x010C: return {'C', UiAccent::Caron};
    case 0x010D: return {'c', UiAccent::Caron};
    case 0x010E: return {'D', UiAccent::Caron};
    case 0x010F: return {'d', UiAccent::Caron};
    case 0x0111: return {'d', UiAccent::Stroke};
    case 0x0112: return {'E', UiAccent::Macron};
    case 0x0113: return {'e', UiAccent::Macron};
    case 0x0116: return {'E', UiAccent::DotAbove};
    case 0x0117: return {'e', UiAccent::DotAbove};
    case 0x0118: return {'E', UiAccent::Ogonek};
    case 0x0119: return {'e', UiAccent::Ogonek};
    case 0x011A: return {'E', UiAccent::Caron};
    case 0x011B: return {'e', UiAccent::Caron};
    case 0x011E: return {'G', UiAccent::Breve};
    case 0x011F: return {'g', UiAccent::Breve};
    case 0x01E6: return {'G', UiAccent::Caron};
    case 0x01E7: return {'g', UiAccent::Caron};
    case 0x0120: return {'G', UiAccent::DotAbove};
    case 0x0121: return {'g', UiAccent::DotAbove};
    case 0x0122: return {'G', UiAccent::Cedilla};
    case 0x0123: return {'g', UiAccent::Cedilla};
    case 0x0126: return {'H', UiAccent::Stroke};
    case 0x0127: return {'h', UiAccent::Stroke};
    case 0x012A: return {'I', UiAccent::Macron};
    case 0x012B: return {'i', UiAccent::Macron};
    case 0x012E: return {'I', UiAccent::Ogonek};
    case 0x012F: return {'i', UiAccent::Ogonek};
    case 0x0130: return {'I', UiAccent::DotAbove};
    case 0x0131: return {'i', UiAccent::None};
    case 0x0136: return {'K', UiAccent::Cedilla};
    case 0x0137: return {'k', UiAccent::Cedilla};
    case 0x0139: return {'L', UiAccent::Acute};
    case 0x013A: return {'l', UiAccent::Acute};
    case 0x013B: return {'L', UiAccent::Cedilla};
    case 0x013C: return {'l', UiAccent::Cedilla};
    case 0x013D: return {'L', UiAccent::Caron};
    case 0x013E: return {'l', UiAccent::Caron};
    case 0x013F: return {'L', UiAccent::MiddleDot};
    case 0x0140: return {'l', UiAccent::MiddleDot};
    case 0x0141: return {'L', UiAccent::Stroke};
    case 0x0142: return {'l', UiAccent::Stroke};
    case 0x0143: return {'N', UiAccent::Acute};
    case 0x0144: return {'n', UiAccent::Acute};
    case 0x0145: return {'N', UiAccent::Cedilla};
    case 0x0146: return {'n', UiAccent::Cedilla};
    case 0x0147: return {'N', UiAccent::Caron};
    case 0x0148: return {'n', UiAccent::Caron};
    case 0x0150: return {'O', UiAccent::DoubleAcute};
    case 0x0151: return {'o', UiAccent::DoubleAcute};
    case 0x0154: return {'R', UiAccent::Acute};
    case 0x0155: return {'r', UiAccent::Acute};
    case 0x0158: return {'R', UiAccent::Caron};
    case 0x0159: return {'r', UiAccent::Caron};
    case 0x015A: return {'S', UiAccent::Acute};
    case 0x015B: return {'s', UiAccent::Acute};
    case 0x015E: case 0x0218: return {'S', UiAccent::Cedilla};
    case 0x015F: case 0x0219: return {'s', UiAccent::Cedilla};
    case 0x0160: return {'S', UiAccent::Caron};
    case 0x0161: return {'s', UiAccent::Caron};
    case 0x0164: return {'T', UiAccent::Caron};
    case 0x0165: return {'t', UiAccent::Caron};
    case 0x021A: return {'T', UiAccent::Cedilla};
    case 0x021B: return {'t', UiAccent::Cedilla};
    case 0x016A: return {'U', UiAccent::Macron};
    case 0x016B: return {'u', UiAccent::Macron};
    case 0x016E: return {'U', UiAccent::Ring};
    case 0x016F: return {'u', UiAccent::Ring};
    case 0x0170: return {'U', UiAccent::DoubleAcute};
    case 0x0171: return {'u', UiAccent::DoubleAcute};
    case 0x0172: return {'U', UiAccent::Ogonek};
    case 0x0173: return {'u', UiAccent::Ogonek};
    case 0x0175: return {'w', UiAccent::Circumflex};
    case 0x0177: return {'y', UiAccent::Circumflex};
    case 0x0178: return {'Y', UiAccent::Umlaut};
    case 0x0179: return {'Z', UiAccent::Acute};
    case 0x017A: return {'z', UiAccent::Acute};
    case 0x017B: return {'Z', UiAccent::DotAbove};
    case 0x017C: return {'z', UiAccent::DotAbove};
    case 0x017D: return {'Z', UiAccent::Caron};
    case 0x017E: return {'z', UiAccent::Caron};
    default: return {'?', UiAccent::None};
  }
}

void drawUiAccent(int16_t x, int16_t y, uint16_t color, uint8_t size,
                  UiAccent accent) {
  const int16_t p = size;
  switch (accent) {
    case UiAccent::Acute:
      tft.fillRect(x + 3 * p, y, p, p, color);
      tft.fillRect(x + 2 * p, y + p, p, p, color);
      break;
    case UiAccent::Grave:
      tft.fillRect(x + p, y, p, p, color);
      tft.fillRect(x + 2 * p, y + p, p, p, color);
      break;
    case UiAccent::Caron:
      tft.fillRect(x + p, y, p, p, color);
      tft.fillRect(x + 3 * p, y, p, p, color);
      tft.fillRect(x + 2 * p, y + p, p, p, color);
      break;
    case UiAccent::Umlaut:
      tft.fillRect(x + p, y, p, p, color);
      tft.fillRect(x + 3 * p, y, p, p, color);
      break;
    case UiAccent::Circumflex:
      tft.fillRect(x + 2 * p, y, p, p, color);
      tft.fillRect(x + p, y + p, p, p, color);
      tft.fillRect(x + 3 * p, y + p, p, p, color);
      break;
    case UiAccent::Ring:
      tft.drawRect(x + 2 * p, y, 2 * p, 2 * p, color);
      break;
    case UiAccent::Tilde:
      tft.fillRect(x + p, y + p, 2 * p, p, color);
      tft.fillRect(x + 3 * p, y, p, p, color);
      break;
    case UiAccent::Cedilla:
      tft.fillRect(x + 2 * p, y + 9 * p, p, p, color);
      break;
    case UiAccent::Macron:
      tft.fillRect(x + p, y, 3 * p, p, color);
      break;
    case UiAccent::Breve:
      tft.fillRect(x + p, y, p, p, color);
      tft.fillRect(x + 3 * p, y, p, p, color);
      tft.fillRect(x + 2 * p, y + p, p, p, color);
      break;
    case UiAccent::DotAbove:
      tft.fillRect(x + 2 * p, y, p, p, color);
      break;
    case UiAccent::DoubleAcute:
      tft.fillRect(x + p, y + p, p, p, color);
      tft.fillRect(x + 2 * p, y, p, p, color);
      tft.fillRect(x + 3 * p, y + p, p, p, color);
      tft.fillRect(x + 4 * p, y, p, p, color);
      break;
    case UiAccent::Ogonek:
      tft.fillRect(x + 3 * p, y + 9 * p, p, p, color);
      tft.fillRect(x + 2 * p, y + 10 * p, p, p, color);
      break;
    case UiAccent::Stroke:
      tft.fillRect(x + p, y + 5 * p, 4 * p, p, color);
      break;
    case UiAccent::MiddleDot:
      tft.fillRect(x + 4 * p, y + 5 * p, p, p, color);
      break;
    default:
      break;
  }
}

// Compact 5x7 glyphs for characters that cannot be composed faithfully from
// the Adafruit built-in Latin base glyph plus an accent. The repertoire is the
// union of the ETSI EBU table and the supplied dabreceiver conversion table.
const uint8_t UI_SPECIAL_BITMAPS[][7] PROGMEM = {
    {0x00, 0x05, 0x0A, 0x14, 0x0A, 0x05, 0x00},  // «
    {0x00, 0x14, 0x0A, 0x05, 0x0A, 0x14, 0x00},  // »
    {0x04, 0x00, 0x04, 0x04, 0x04, 0x04, 0x04},  // ¡
    {0x04, 0x00, 0x04, 0x02, 0x01, 0x11, 0x0E},  // ¿
    {0x06, 0x09, 0x08, 0x1E, 0x08, 0x08, 0x1F},  // £
    {0x06, 0x09, 0x08, 0x1E, 0x08, 0x09, 0x06},  // €
    {0x0E, 0x11, 0x17, 0x15, 0x17, 0x11, 0x0E},  // ©
    {0x0E, 0x15, 0x15, 0x1F, 0x15, 0x15, 0x17},  // Æ
    {0x00, 0x0E, 0x01, 0x0F, 0x11, 0x13, 0x0D},  // æ
    {0x0E, 0x15, 0x15, 0x15, 0x15, 0x15, 0x0E},  // Œ
    {0x00, 0x00, 0x0E, 0x15, 0x15, 0x15, 0x0E},  // œ
    {0x0E, 0x09, 0x09, 0x1D, 0x09, 0x09, 0x0E},  // Ð
    {0x02, 0x02, 0x0E, 0x12, 0x12, 0x12, 0x0F},  // ð
    {0x08, 0x08, 0x0E, 0x09, 0x09, 0x0E, 0x08},  // Þ
    {0x08, 0x08, 0x0E, 0x09, 0x09, 0x0E, 0x08},  // þ
    {0x0E, 0x11, 0x10, 0x1E, 0x11, 0x11, 0x1E},  // ß
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x01, 0x06},  // Ŋ
    {0x00, 0x00, 0x1E, 0x11, 0x11, 0x13, 0x1D},  // ŋ
    {0x15, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00},  // Ĳ
    {0x00, 0x00, 0x15, 0x04, 0x04, 0x0E, 0x00},  // ĳ
    {0x0C, 0x12, 0x12, 0x1C, 0x12, 0x12, 0x1C},  // β
    {0x00, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00},  // ¯
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},  // ―
};
static_assert(sizeof(UI_SPECIAL_BITMAPS) / sizeof(UI_SPECIAL_BITMAPS[0]) ==
                  static_cast<uint8_t>(UiSpecialGlyph::HorizontalBar),
              "UiSpecialGlyph table is incomplete");

void drawUiSpecialGlyph(int16_t x, int16_t y, uint16_t color, uint8_t size,
                        UiSpecialGlyph glyph) {
  if (glyph == UiSpecialGlyph::None) return;
  const uint8_t index = static_cast<uint8_t>(glyph) - 1;
  for (uint8_t row = 0; row < 7; ++row) {
    const uint8_t bits = pgm_read_byte(&UI_SPECIAL_BITMAPS[index][row]);
    for (uint8_t column = 0; column < 5; ++column) {
      if ((bits & (1U << (4 - column))) != 0) {
        tft.fillRect(x + column * size, y + (row + 2) * size,
                     size, size, color);
      }
    }
  }
}

uint16_t utf8CodepointCount(const String& text) {
  uint16_t offset = 0;
  uint16_t count = 0;
  while (offset < text.length()) {
    nextUtf8Codepoint(text, offset);
    ++count;
  }
  return count;
}

String utf8Slice(const String& text, uint16_t start, uint16_t count) {
  uint16_t byteOffset = 0;
  uint16_t codepoint = 0;
  uint16_t startByte = text.length();
  uint16_t endByte = text.length();
  while (byteOffset < text.length()) {
    const uint16_t before = byteOffset;
    nextUtf8Codepoint(text, byteOffset);
    if (codepoint == start) startByte = before;
    ++codepoint;
    if (codepoint == start + count) {
      endByte = byteOffset;
      break;
    }
  }
  if (start == codepoint && startByte == text.length()) startByte = byteOffset;
  if (startByte >= text.length()) return String();
  return text.substring(startByte, endByte);
}

uint32_t utf8CodepointAt(const String& text, uint16_t index) {
  uint16_t offset = 0;
  for (uint16_t current = 0; offset < text.length(); ++current) {
    const uint32_t codepoint = nextUtf8Codepoint(text, offset);
    if (current == index) return codepoint;
  }
  return 0;
}

bool uiWrappedLine(const String& text, uint16_t start, String& line,
                   uint16_t& nextStart) {
  const uint16_t total = utf8CodepointCount(text);
  while (start < total && utf8CodepointAt(text, start) == ' ') ++start;
  while (start < total && utf8CodepointAt(text, start) == '\n') ++start;
  if (start >= total) {
    line = "";
    nextStart = total;
    return false;
  }

  uint16_t end = min<uint16_t>(total, start + UI_TEXT_LINE_GLYPHS);
  uint16_t lastSpace = 0xFFFF;
  for (uint16_t index = start; index < end; ++index) {
    const uint32_t codepoint = utf8CodepointAt(text, index);
    if (codepoint == '\n') {
      end = index;
      nextStart = index + 1;
      line = utf8Slice(text, start, end - start);
      line.trim();
      return true;
    }
    if (codepoint == ' ') lastSpace = index;
  }

  if (end < total && lastSpace != 0xFFFF && lastSpace > start) {
    end = lastSpace;
    nextStart = lastSpace + 1;
  } else {
    nextStart = end;
  }
  while (nextStart < total && utf8CodepointAt(text, nextStart) == ' ') {
    ++nextStart;
  }
  line = utf8Slice(text, start, end - start);
  line.trim();
  return true;
}

uint8_t uiWrappedLineCount(const String& text) {
  uint16_t start = 0;
  uint8_t count = 0;
  String ignored;
  uint16_t next = 0;
  while (count < 16 && uiWrappedLine(text, start, ignored, next)) {
    ++count;
    if (next <= start) break;
    start = next;
  }
  return count;
}

String uiWrappedLineAt(const String& text, uint8_t requestedLine) {
  uint16_t start = 0;
  String line;
  uint16_t next = 0;
  for (uint8_t index = 0; index <= requestedLine; ++index) {
    if (!uiWrappedLine(text, start, line, next)) return String();
    if (index == requestedLine) return line;
    start = next;
  }
  return String();
}

uint16_t utf8TextWidth(const String& text, uint8_t size) {
  return utf8CodepointCount(text) * 6U * size;
}

void drawUtf8Text(const String& text, int16_t x, int16_t y, uint16_t color,
                  uint8_t size, uint16_t maxWidth = 160) {
  uint16_t offset = 0;
  const int16_t startX = x;
  tft.setTextSize(size);
  tft.setTextColor(color);
  while (offset < text.length() && x + 6 * size <= startX + maxWidth) {
    const UiGlyph glyph = uiGlyph(nextUtf8Codepoint(text, offset));
    if (glyph.special == UiSpecialGlyph::None) {
      tft.setCursor(x, y + 2 * size);
      tft.write(static_cast<uint8_t>(glyph.base));
      drawUiAccent(x, y, color, size, glyph.accent);
    } else {
      drawUiSpecialGlyph(x, y, color, size, glyph.special);
    }
    x += 6 * size;
  }
}

uint8_t currentUiStationCount() {
  return dabMode == 1 ? totalDABchannels : totalFMchannels;
}

void readUiStationLabel(uint8_t channel, char* label, size_t labelSize,
                        uint16_t& frequency, uint8_t& charset) {
  memset(label, 0, labelSize);
  frequency = 0;
  charset = DAB_CHARSET_EBU_LATIN;
  if (dabMode == 1) {
    extEEPROM.read(ADDR_DAB_CHANNEL + 28 * (channel - 1) + 11,
                   reinterpret_cast<uint8_t*>(label), 16);
    label[16] = 0;
    extEEPROM.get(ADDR_DAB_CHANNEL + 28 * (channel - 1) + 27, charset);
  } else {
    uint8_t record[12] = {0};
    extEEPROM.read(ADDR_FM_CHANNEL + 13 * (channel - 1) + 1,
                   record, sizeof(record));
    frequency = 100U * record[1] + 10U * record[0];
    memcpy(label, record + 3, min(labelSize - 1, static_cast<size_t>(8)));
    if (label[0] == 0 || strncmp(label, "unknown?", 8) == 0) {
      snprintf(label, labelSize, "%u.%1u MHz", frequency / 100,
               (frequency % 100) / 10);
    }
  }
}

void markUiDirty(uint8_t regions) {
  uiDirtyFlags |= regions;
}

struct UiStatusSnapshot {
  int8_t signalstrength;
  int8_t snr;
  uint8_t quality;
  uint16_t bitrate;
  uint16_t samplerate;
  uint16_t pi;
  uint8_t mode;
  uint8_t pty;
  uint8_t fmStereoBlend;
  uint32_t dsrvPackets;
  uint32_t dlsPackets;
  uint32_t motPackets;
  uint32_t irqCount;
  uint32_t commandErrors;
  uint32_t dsrvOverflows;
  bool valid;
  bool dabplus;
  bool fmPilot;
  bool rdsSync;
  bool tp;
  bool ta;
};

void noteUiStatusChanged() {
  static UiStatusSnapshot previous = {};
  static bool previousValid = false;
  UiStatusSnapshot current = {};
  current.signalstrength = Dab.signalstrength;
  current.snr = Dab.snr;
  current.quality = Dab.quality;
  current.bitrate = Dab.bitrate;
  current.samplerate = Dab.samplerate;
  current.pi = Dab.pi;
  current.mode = static_cast<uint8_t>(Dab.mode);
  current.pty = Dab.pty;
  current.fmStereoBlend = Dab.fmStereoBlend;
  current.dsrvPackets = Dab.dsrvPacketCount();
  current.dlsPackets = Dab.dlsPacketCount();
  current.motPackets = Dab.motPacketCount();
  current.irqCount = Dab.irqCount();
  current.commandErrors = Dab.commandErrorCount();
  current.dsrvOverflows = Dab.dsrvOverflowCount();
  current.valid = Dab.valid;
  current.dabplus = Dab.dabplus;
  current.fmPilot = Dab.fmPilot;
  current.rdsSync = Dab.rdsSync;
  current.tp = Dab.tp;
  current.ta = Dab.ta;
  const bool signalChanged = !previousValid ||
      current.signalstrength != previous.signalstrength ||
      current.snr != previous.snr;
  const bool dabTypeChanged = !previousValid ||
      current.dabplus != previous.dabplus;
  const bool statusChanged = !previousValid ||
      current.quality != previous.quality ||
      current.bitrate != previous.bitrate ||
      current.samplerate != previous.samplerate ||
      current.pi != previous.pi || current.mode != previous.mode ||
      current.pty != previous.pty ||
      current.fmStereoBlend != previous.fmStereoBlend ||
      current.valid != previous.valid || current.dabplus != previous.dabplus ||
      current.fmPilot != previous.fmPilot ||
      current.rdsSync != previous.rdsSync || current.tp != previous.tp ||
      current.ta != previous.ta;
  const bool diagnosticsChanged = !previousValid ||
      current.dsrvPackets != previous.dsrvPackets ||
      current.dlsPackets != previous.dlsPackets ||
      current.motPackets != previous.motPackets ||
      current.irqCount != previous.irqCount ||
      current.commandErrors != previous.commandErrors ||
      current.dsrvOverflows != previous.dsrvOverflows;
  previous = current;
  previousValid = true;
  if (signalChanged) markUiDirty(UI_DIRTY_SIGNAL);
  if (statusChanged) markUiDirty(UI_DIRTY_STATUS);
  if (dabMode == 1 && dabTypeChanged) markUiDirty(UI_DIRTY_HEADER);
  if (signalChanged || statusChanged || diagnosticsChanged) {
    markUiDirty(UI_DIRTY_TECH);
  }
}

void updateUiBroadcastText(const String& decoded) {
  String normalized = decoded;
  normalized.replace('\r', ' ');
  normalized.replace('\n', ' ');
  if (normalized == uiBroadcastText) return;
  uiBroadcastText = normalized;
  uiBroadcastTextGlyphs = utf8CodepointCount(normalized);
  uiBroadcastLoopText = "";
  uiBroadcastLoopGlyphs = 0;
  if (uiBroadcastTextGlyphs != 0) {
    const String unit = uiBroadcastText + "   ";
    const uint16_t unitGlyphs = uiBroadcastTextGlyphs + 3;
    do {
      uiBroadcastLoopText += unit;
      uiBroadcastLoopGlyphs += unitGlyphs;
    } while (uiBroadcastLoopGlyphs < UI_TEXT_WINDOW_GLYPHS);
  }
  uiTextScrollGlyph = 0;
  uiTextPageDeadlineMs = millis() + UI_TEXT_SCROLL_STEP_MS;
  markUiDirty(UI_DIRTY_TEXT);
}

String uiCircularTextSlice(uint16_t start, uint16_t count) {
  String result;
  if (uiBroadcastLoopGlyphs == 0 || count == 0) return result;
  result.reserve(count * 2U);
  start %= uiBroadcastLoopGlyphs;
  while (count != 0) {
    const uint16_t available = uiBroadcastLoopGlyphs - start;
    const uint16_t take = count < available ? count : available;
    result += utf8Slice(uiBroadcastLoopText, start, take);
    count -= take;
    start = 0;
  }
  return result;
}

void renderUiHeader() {
  if (uiView != UiView::Text) return;
  tft.fillRect(0, 0, screenWidth, 14, ST77XX_BLACK);
  const char* band = dabMode == 1
      ? (Dab.dabplus ? "DAB+" : "DAB") : "FM";
  drawUtf8Text(String(band), 2, 1, ST77XX_YELLOW, 1, 24);
  char detail[22];
  if (dabMode == 1) {
    const uint32_t frequency = ensemble < DAB_FREQS ? Dab.freq_khz(ensemble) : 0;
    snprintf(detail, sizeof(detail), "%s %lu.%03lu",
             ensemble < DAB_FREQS ? UI_DAB_CHANNELS[ensemble] : "--",
             static_cast<unsigned long>(frequency / 1000),
             static_cast<unsigned long>(frequency % 1000));
  } else {
    const uint16_t frequency = 100U * stationFM_h + 10U * stationFM_l;
    snprintf(detail, sizeof(detail), "%u.%1u MHz", frequency / 100,
             (frequency % 100) / 10);
  }
  drawUtf8Text(String(detail), dabMode == 1 ? 30 : 19, 1,
               ST77XX_WHITE, 1, dabMode == 1 ? 88 : 99);
  char volumeText[8];
  snprintf(volumeText, sizeof(volumeText), "VOL %u", vol);
  const uint16_t width = utf8TextWidth(String(volumeText), 1);
  drawUtf8Text(String(volumeText), screenWidth - width - 2, 1,
               ST77XX_GREEN, 1, width);
  tft.drawFastHLine(0, 14, screenWidth, UI_DARK_LINE);
}

String currentUiStationName() {
  String name = dabMode == 1
      ? decodeBroadcastText(reinterpret_cast<const uint8_t*>(dabName), 16,
                            dabCharset)
      : decodeRdsText(reinterpret_cast<const uint8_t*>(fmName), 8);
  name.trim();
  if (name.length() == 0 && dabMode == 0) {
    char fallback[16];
    snprintf(fallback, sizeof(fallback), "%u.%1u MHz", stationFM_h,
             stationFM_l);
    name = fallback;
  }
  return name;
}

void resetUiStationScroll() {
  uiStationScrollGlyph = 0;
  uiStationScrollEndHold = false;
  uiStationScrollDeadlineMs = millis() + 1200;
}

void renderUiStationName() {
  if (uiView != UiView::Text) return;
  tft.fillRect(0, 15, screenWidth, 24, ST77XX_BLACK);
  String name = currentUiStationName();
  constexpr uint16_t visibleGlyphs = 13;
  const uint16_t glyphCount = utf8CodepointCount(name);
  if (glyphCount > visibleGlyphs) {
    const uint16_t maximumOffset = glyphCount - visibleGlyphs;
    if (uiStationScrollGlyph > maximumOffset) {
      uiStationScrollGlyph = maximumOffset;
    }
    name = utf8Slice(name, uiStationScrollGlyph, visibleGlyphs);
  }
  const uint16_t width = utf8TextWidth(name, 2);
  const int16_t x = width < screenWidth ? (screenWidth - width) / 2 : 0;
  drawUtf8Text(name, x, 16, ST77XX_RED, 2, screenWidth);
  tft.drawFastHLine(0, 40, screenWidth, UI_DARK_LINE);
}

void renderUiTextArea() {
  if (uiView != UiView::Text || scanActive()) return;
  tft.fillRect(0, 42, screenWidth, 37, ST77XX_BLACK);
  if (uiBroadcastText.length() == 0) {
    drawUtf8Text("Čekám na vysílaný text", 2, 49, 0x7BEF, 1, 156);
    return;
  }
  const String upper = uiCircularTextSlice(uiTextScrollGlyph,
                                            UI_TEXT_LINE_GLYPHS);
  const String lower = uiCircularTextSlice(
      uiTextScrollGlyph + UI_TEXT_LINE_GLYPHS, UI_TEXT_LINE_GLYPHS);
  drawUtf8Text(upper, 2, 43, ST77XX_WHITE, 1, 156);
  drawUtf8Text(lower, 2, 58, ST77XX_WHITE, 1, 156);
}

uint16_t uiSignalColor(int8_t quality, bool dab) {
  const int8_t good = dab ? 12 : 20;
  const int8_t medium = dab ? 7 : 10;
  if (quality >= good) return ST77XX_GREEN;
  if (quality >= medium) return ST77XX_YELLOW;
  return ST77XX_RED;
}

uint16_t uiRssiColor(int8_t rssi, bool dab) {
  const int8_t good = 35;
  const int8_t medium = dab ? 25 : 20;
  if (rssi >= good) return ST77XX_GREEN;
  if (rssi >= medium) return ST77XX_YELLOW;
  return ST77XX_RED;
}

uint16_t uiFicColor(uint8_t quality) {
  if (quality >= 90) return ST77XX_GREEN;
  if (quality >= 70) return ST77XX_YELLOW;
  return ST77XX_RED;
}

void renderUiSignal() {
  if (scanActive() || uiView != UiView::Text) return;
  tft.fillRect(0, 96, screenWidth, 15, ST77XX_BLACK);
  char value[32];
  if (dabMode == 1) {
    drawUtf8Text("CNR", 2, 98, ST77XX_CYAN, 1, 24);
    snprintf(value, sizeof(value), "%d dB", Dab.snr);
    drawUtf8Text(String(value), 28, 98, uiSignalColor(Dab.snr, true), 1, 45);
    drawUtf8Text("RSSI", 78, 98, ST77XX_CYAN, 1, 30);
    snprintf(value, sizeof(value), "%d", Dab.signalstrength);
    drawUtf8Text(String(value), 110, 98,
                 uiRssiColor(Dab.signalstrength, true), 1, 48);

  } else {
    drawUtf8Text("SNR", 2, 98, ST77XX_CYAN, 1, 24);
    snprintf(value, sizeof(value), "%d dB", Dab.snr);
    drawUtf8Text(String(value), 28, 98, uiSignalColor(Dab.snr, false), 1, 45);
    drawUtf8Text("RSSI", 78, 98, ST77XX_CYAN, 1, 30);
    snprintf(value, sizeof(value), "%d", Dab.signalstrength);
    drawUtf8Text(String(value), 110, 98,
                 uiRssiColor(Dab.signalstrength, false), 1, 48);
  }
}

void renderUiStatusStatic() {
  if (scanActive() || uiView != UiView::Text) return;
  tft.fillRect(0, 81, screenWidth, 15, ST77XX_BLACK);
  tft.fillRect(0, 111, screenWidth, 17, ST77XX_BLACK);
  tft.drawFastHLine(0, 80, screenWidth, UI_DARK_LINE);
  char value[32];
  if (dabMode == 1) {
    static const char* const shortAudioMode[] = {"DUAL", "MONO", "ST", "J-ST"};
    snprintf(value, sizeof(value), "%s %ukbit %ukHz %s",
             Dab.dabplus ? "HE-AAC" : "MP2", Dab.bitrate,
             Dab.samplerate / 1000U,
             shortAudioMode[static_cast<uint8_t>(Dab.mode) & 0x03U]);
    drawUtf8Text(String(value), 2, 83, ST77XX_WHITE, 1, 156);
    drawUtf8Text("FIC", 2, 113, ST77XX_CYAN, 1, 18);
    snprintf(value, sizeof(value), "%u%%", Dab.quality);
    drawUtf8Text(String(value), 26, 113, uiFicColor(Dab.quality), 1, 36);
    drawUtf8Text("SLS", 68, 113,
                 Dab.slideshowAvailable() ? ST77XX_GREEN : UI_GRAY, 1, 18);
    drawUtf8Text("CH", 92, 113, ST77XX_CYAN, 1, 12);
    snprintf(value, sizeof(value), "%u/%u", currentDABchannel,
             totalDABchannels);
    drawUtf8Text(String(value), 110, 113, ST77XX_WHITE, 1, 48);
  } else {
    drawUtf8Text(Dab.fmPilot ? "STEREO" : "MONO", 2, 83,
                 Dab.fmPilot ? ST77XX_GREEN : ST77XX_YELLOW, 1, 44);
    drawUtf8Text("RDS", 52, 83,
                 Dab.rdsSync ? ST77XX_GREEN : ST77XX_RED, 1, 24);
    drawUtf8Text("TP", 86, 83, Dab.tp ? ST77XX_GREEN : UI_GRAY, 1, 18);
    drawUtf8Text("TA", 113, 83, Dab.ta ? UI_ORANGE : UI_GRAY, 1, 18);
    snprintf(value, sizeof(value), "PTY %u  PI %04X", Dab.pty, Dab.pi);
    drawUtf8Text(String(value), 2, 113, UI_GRAY, 1, 92);
    snprintf(value, sizeof(value), "%u/%u", currentFMchannel,
             totalFMchannels);
    drawUtf8Text(String(value), 128, 113, UI_GRAY, 1, 30);
  }
}

void renderUiStatus() {
  if (scanActive()) return;
  if (uiView == UiView::Slideshow) {
    renderSlideshowStatus();
  } else if (uiView == UiView::Tech) {
    renderTechScreen();
  } else if (uiView == UiView::Text) {
    renderUiStatusStatic();
    renderUiSignal();
  }
}

void renderListeningScreen() {
  uiView = UiView::Text;
  resetUiStationScroll();
  clearScreen();
  renderUiHeader();
  renderUiStationName();
  renderUiTextArea();
  renderUiStatus();
}

void renderSlideshowStatus() {
  if (uiView != UiView::Slideshow || uiSettings.slideshowLayout != 0 ||
      scanActive()) {
    return;
  }
  tft.fillRect(0, 113, screenWidth, 15, ST77XX_BLACK);
  tft.drawFastHLine(0, 112, screenWidth, UI_DARK_LINE);
  String station = currentUiStationName();
  if (utf8CodepointCount(station) > 13) station = utf8Slice(station, 0, 13);
  drawUtf8Text(station, 2, 115, ST77XX_RED, 1, 78);
  drawUtf8Text(ensemble < DAB_FREQS ? String(UI_DAB_CHANNELS[ensemble]) : "--",
               83, 115, ST77XX_WHITE, 1, 20);
  char value[8];
  snprintf(value, sizeof(value), "V%u", vol);
  drawUtf8Text(String(value), 106, 115, ST77XX_GREEN, 1, 27);
  snprintf(value, sizeof(value), "C%d", Dab.snr);
  drawUtf8Text(String(value), 135, 115, uiSignalColor(Dab.snr, true), 1, 24);
}

bool renderSlideshowScreen() {
  uiView = UiView::Slideshow;
  clearScreen();
  if (dabMode != 1 || uiSettings.slideshowMode == 0) {
    drawUtf8Text("Slideshow není k dispozici", 7, 51,
                 ST77XX_YELLOW, 1, 148);
    return false;
  }
  if (!Dab.slideshowAvailable()) {
    drawUtf8Text("Čekám na slideshow...", 17, 51,
                 ST77XX_CYAN, 1, 130);
    uiSlideshowDecodePending = true;
    renderSlideshowStatus();
    return true;
  }
  if (Dab.urgentDataPending()) {
    drawUtf8Text("Přijímám slideshow...", 17, 51,
                 ST77XX_CYAN, 1, 130);
    uiSlideshowDecodePending = true;
    return true;
  }

  const int16_t imageHeight = uiSettings.slideshowLayout ? screenHeight : 112;
  const bool decoded = renderRamSlideshow(
      tft, Dab.slideshowData(), Dab.slideshowLength(), 0, 0,
      screenWidth, imageHeight, &Serial);
  uiSlideshowDecodePending = false;
  if (!decoded) {
    clearScreen();
    drawUtf8Text("Slideshow nelze zobrazit", 5, 44,
                 ST77XX_RED, 1, 150);
    drawUtf8Text("JPEG/PNG bylo odmítnuto", 8, 61,
                 UI_GRAY, 1, 144);
    Dab.discardSlideshow();
    markUiDirty(UI_DIRTY_STATUS);
    return false;
  }
  renderSlideshowStatus();
  // Pixels are now stored in the display controller. Reuse the one compressed
  // MOT arena for the next object instead of allocating another image buffer.
  Dab.discardSlideshow();
  return true;
}

void renderCurrentUiView() {
  if (uiView == UiView::Tech && uiSettings.techEnabled) {
    renderTechScreen();
  } else if (uiView == UiView::Slideshow && dabMode == 1 &&
             uiSettings.slideshowMode != 0) {
    renderSlideshowScreen();
  } else {
    renderListeningScreen();
  }
}

void flushUiDirty() {
  if (uiDirtyFlags == UI_DIRTY_NONE || scanActive()) return;
  const uint8_t dirty = uiDirtyFlags;
  uiDirtyFlags = UI_DIRTY_NONE;
  if ((dirty & UI_DIRTY_FULL) != 0) {
    renderCurrentUiView();
    return;
  }
  if (uiView == UiView::Text) {
    if ((dirty & UI_DIRTY_HEADER) != 0) renderUiHeader();
    if ((dirty & UI_DIRTY_STATION) != 0) renderUiStationName();
    if ((dirty & UI_DIRTY_TEXT) != 0) renderUiTextArea();
    if ((dirty & UI_DIRTY_STATUS) != 0) renderUiStatusStatic();
    if ((dirty & UI_DIRTY_SIGNAL) != 0) renderUiSignal();
  } else if (uiView == UiView::Tech &&
             (dirty & (UI_DIRTY_HEADER | UI_DIRTY_STATUS |
                       UI_DIRTY_SIGNAL | UI_DIRTY_TECH)) != 0) {
    renderTechScreen();
  } else if (uiView == UiView::Slideshow &&
             (dirty & (UI_DIRTY_HEADER | UI_DIRTY_STATION |
                       UI_DIRTY_STATUS | UI_DIRTY_SIGNAL)) != 0) {
    renderSlideshowStatus();
  }
}

void renderTechScreen() {
  if (uiView != UiView::Tech || scanActive()) return;
  char line[34];
  tft.fillRect(0, 0, screenWidth, 14, ST77XX_BLACK);
  drawUtf8Text(dabMode == 1 ? "DAB" : "FM", 2, 1,
               ST77XX_YELLOW, 1, 24);
  drawUtf8Text("TECH", 61, 1, ST77XX_CYAN, 1, 30);
  snprintf(line, sizeof(line), "V%u", vol);
  drawUtf8Text(String(line), 136, 1, ST77XX_GREEN, 1, 22);
  tft.drawFastHLine(0, 14, screenWidth, UI_DARK_LINE);
  if (dabMode == 1) {
    const uint32_t frequency = ensemble < DAB_FREQS ? Dab.freq_khz(ensemble) : 0;
    snprintf(line, sizeof(line), "%s  %lu.%03lu MHz",
             ensemble < DAB_FREQS ? UI_DAB_CHANNELS[ensemble] : "--",
             static_cast<unsigned long>(frequency / 1000),
             static_cast<unsigned long>(frequency % 1000));
    tft.fillRect(0, 15, screenWidth, 15, ST77XX_BLACK);
    drawUtf8Text(String(line), 2, 17, ST77XX_WHITE, 1, 156);

    tft.fillRect(0, 30, screenWidth, 15, ST77XX_BLACK);
    drawUtf8Text("CNR", 2, 32, ST77XX_CYAN, 1, 24);
    snprintf(line, sizeof(line), "%d dB", Dab.snr);
    drawUtf8Text(String(line), 28, 32, uiSignalColor(Dab.snr, true), 1, 42);
    drawUtf8Text("RSSI", 78, 32, ST77XX_CYAN, 1, 30);
    snprintf(line, sizeof(line), "%d", Dab.signalstrength);
    drawUtf8Text(String(line), 110, 32,
                 uiRssiColor(Dab.signalstrength, true), 1, 46);

    snprintf(line, sizeof(line), "FIC %u%% %s %ukbit", Dab.quality,
             Dab.dabplus ? "DAB+" : "DAB", Dab.bitrate);
    tft.fillRect(0, 45, screenWidth, 15, ST77XX_BLACK);
    drawUtf8Text(String(line), 2, 47, ST77XX_GREEN, 1, 156);
    snprintf(line, sizeof(line), "Audio %u Hz  %s", Dab.samplerate,
             audiomode[Dab.mode]);
    tft.fillRect(0, 60, screenWidth, 15, ST77XX_BLACK);
    drawUtf8Text(String(line), 2, 62, ST77XX_WHITE, 1, 156);
    snprintf(line, sizeof(line), "SID %08lX", static_cast<unsigned long>(serviceid));
    tft.fillRect(0, 75, screenWidth, 15, ST77XX_BLACK);
    drawUtf8Text(String(line), 2, 77, UI_GRAY, 1, 156);
    snprintf(line, sizeof(line), "CID %08lX  %s", static_cast<unsigned long>(compid),
             broadcastCharsetName(Dab.ServiceDataCharset));
    tft.fillRect(0, 90, screenWidth, 15, ST77XX_BLACK);
    drawUtf8Text(String(line), 2, 92, UI_GRAY, 1, 156);
  } else {
    snprintf(line, sizeof(line), "%u.%1u MHz  %s %u%%", stationFM_h,
             stationFM_l, Dab.fmPilot ? "ST" : "MONO", Dab.fmStereoBlend);
    tft.fillRect(0, 15, screenWidth, 15, ST77XX_BLACK);
    drawUtf8Text(String(line), 2, 17, ST77XX_WHITE, 1, 156);

    tft.fillRect(0, 30, screenWidth, 15, ST77XX_BLACK);
    drawUtf8Text("SNR", 2, 32, ST77XX_CYAN, 1, 24);
    snprintf(line, sizeof(line), "%d dB", Dab.snr);
    drawUtf8Text(String(line), 28, 32, uiSignalColor(Dab.snr, false), 1, 42);
    drawUtf8Text("RSSI", 78, 32, ST77XX_CYAN, 1, 30);
    snprintf(line, sizeof(line), "%d", Dab.signalstrength);
    drawUtf8Text(String(line), 110, 32,
                 uiRssiColor(Dab.signalstrength, false), 1, 46);

    snprintf(line, sizeof(line), "PI %04X  PTY %u", Dab.pi, Dab.pty);
    tft.fillRect(0, 45, screenWidth, 15, ST77XX_BLACK);
    drawUtf8Text(String(line), 2, 47, ST77XX_WHITE, 1, 156);
    snprintf(line, sizeof(line), "RDS %s  TP %u  TA %u", Dab.rdsSync ? "LOCK" : "--",
             Dab.tp ? 1 : 0, Dab.ta ? 1 : 0);
    tft.fillRect(0, 60, screenWidth, 15, ST77XX_BLACK);
    drawUtf8Text(String(line), 2, 62,
                 Dab.rdsSync ? ST77XX_GREEN : ST77XX_RED, 1, 156);
    const String decodedPs = decodeRdsText(
        reinterpret_cast<const uint8_t*>(Dab.ps), 8);
    tft.fillRect(0, 75, screenWidth, 15, ST77XX_BLACK);
    drawUtf8Text(String("PS ") + decodedPs, 2, 77, ST77XX_CYAN, 1, 156);
    tft.fillRect(0, 90, screenWidth, 15, ST77XX_BLACK);
    drawUtf8Text("Charset Basic RDS", 2, 92, UI_GRAY, 1, 156);
  }
  tft.fillRect(0, 105, screenWidth, 23, ST77XX_BLACK);
  snprintf(line, sizeof(line), "DSRV:%lu DLS:%lu MOT:%lu",
           static_cast<unsigned long>(Dab.dsrvPacketCount()),
           static_cast<unsigned long>(Dab.dlsPacketCount()),
           static_cast<unsigned long>(Dab.motPacketCount()));
  drawUtf8Text(String(line), 2, 106, ST77XX_CYAN, 1, 156);
  snprintf(line, sizeof(line), "IRQ:%lu ERR:%lu OVF:%lu",
           static_cast<unsigned long>(Dab.irqCount()),
           static_cast<unsigned long>(Dab.commandErrorCount()),
           static_cast<unsigned long>(Dab.dsrvOverflowCount()));
  drawUtf8Text(String(line), 2, 117, UI_ORANGE, 1, 156);
}

void cycleUiScreen() {
  if (uiView == UiView::Text && dabMode == 1 &&
      uiSettings.slideshowMode != 0) {
    uiView = UiView::Slideshow;
  } else if (uiView != UiView::Tech && uiSettings.techEnabled) {
    uiView = UiView::Tech;
  } else {
    uiView = UiView::Text;
  }
  stationPreviewActive = false;
  renderCurrentUiView();
}

void renderStationList() {
  if (uiView != UiView::StationList) return;
  clearScreen();
  char header[28];
  snprintf(header, sizeof(header), "Seznam %s  V%u",
           dabMode == 1 ? "DAB" : "FM", vol);
  drawUtf8Text(String(header), 2, 1, ST77XX_CYAN, 1, 156);
  tft.drawFastHLine(0, 14, screenWidth, UI_DARK_LINE);
  const uint8_t total = currentUiStationCount();
  if (total == 0) {
    drawUtf8Text("Žádné stanice - podrž SCAN", 2, 36, ST77XX_RED, 1, 156);
    return;
  }
  if (stationListSelection < stationListTop) stationListTop = stationListSelection;
  if (stationListSelection >= stationListTop + UI_LIST_ROWS) {
    stationListTop = stationListSelection - UI_LIST_ROWS + 1;
  }
  for (uint8_t row = 0; row < UI_LIST_ROWS; ++row) {
    const uint8_t channel = stationListTop + row;
    if (channel > total) break;
    const int16_t y = 18 + row * 15;
    const bool selected = channel == stationListSelection;
    const uint16_t rowColor = selected ? 0x04B6
                                       : (row & 1U ? 0x1082 : ST77XX_BLACK);
    tft.fillRect(0, y, screenWidth, 14, rowColor);
    char label[18];
    uint16_t frequency;
    uint8_t charset;
    readUiStationLabel(channel, label, sizeof(label), frequency, charset);
    char prefix[6];
    snprintf(prefix, sizeof(prefix), "%3u ", channel);
    drawUtf8Text(String(prefix), 1, y, selected ? ST77XX_WHITE : 0x7BEF, 1, 28);
    const String decodedLabel = dabMode == 1
        ? decodeBroadcastText(reinterpret_cast<const uint8_t*>(label), 16,
                              charset)
        : decodeRdsText(reinterpret_cast<const uint8_t*>(label), 8);
    drawUtf8Text(decodedLabel, 30, y,
                 selected ? ST77XX_WHITE : ST77XX_CYAN, 1, 128);
  }
}

void openStationList() {
  uiViewBeforeModal = (uiView == UiView::Tech || uiView == UiView::Slideshow)
                          ? uiView : UiView::Text;
  uiView = UiView::StationList;
  stationListSelection = dabMode == 1 ? currentDABchannel : currentFMchannel;
  if (stationListSelection == 0) stationListSelection = 1;
  stationListTop = stationListSelection;
  renderStationList();
}

void moveStationList(int8_t direction) {
  const uint8_t total = currentUiStationCount();
  if (total == 0) return;
  if (direction > 0) {
    stationListSelection = stationListSelection < total
                               ? stationListSelection + 1 : 1;
  } else {
    stationListSelection = stationListSelection > 1
                               ? stationListSelection - 1 : total;
  }
  renderStationList();
}

void tuneStationListSelection() {
  if (currentUiStationCount() == 0 || !Dab.ready()) return;
  stationPreviewActive = false;
  uiView = UiView::Text;
  if (dabMode == 1) {
    currentDABchannel = stationListSelection;
    saveCurrentDABChannelToEEPROM(currentDABchannel);
    DAB_SetChannel();
  } else {
    currentFMchannel = stationListSelection;
    saveCurrentFMchannelToEEPROM(currentFMchannel);
    FMsetChannel(currentFMchannel, true);
  }
}

void renderMenu() {
  if (uiView != UiView::Menu) return;
  clearScreen();
  drawUtf8Text("Nastavení", 2, 1, ST77XX_CYAN, 1, 156);
  tft.drawFastHLine(0, 14, screenWidth, UI_DARK_LINE);
  constexpr uint8_t menuItems = 7;
  constexpr uint8_t visibleItems = 5;
  const char* labels[menuItems] = {"Jas", "Ztlumení", "Ztlumit po",
                                   "Slideshow", "SLS vzhled", "TECH",
                                   "Výchozí"};
  if (menuSelection < menuTop) menuTop = menuSelection;
  if (menuSelection >= menuTop + visibleItems) {
    menuTop = menuSelection - visibleItems + 1;
  }
  for (uint8_t row = 0; row < visibleItems; ++row) {
    const uint8_t item = menuTop + row;
    if (item >= menuItems) break;
    const int16_t y = 19 + row * 19;
    const bool selected = item == menuSelection;
    tft.fillRect(0, y, screenWidth, 18,
                 selected ? 0x04B6 : (item & 1U ? 0x1082 : ST77XX_BLACK));
    drawUtf8Text(String(labels[item]), 2, y,
                 selected ? ST77XX_WHITE : ST77XX_CYAN, 1, 92);
    char value[15];
    switch (item) {
      case 0: snprintf(value, sizeof(value), "%u%%", uiSettings.brightness); break;
      case 1: snprintf(value, sizeof(value), "%u%%", uiSettings.dimLevel); break;
      case 2: snprintf(value, sizeof(value), "%us",
                       UI_DIM_TIMEOUT_SECONDS[uiSettings.dimTimeoutIndex]); break;
      case 3: snprintf(value, sizeof(value), "%s",
                       uiSettings.slideshowMode == 0 ? "Off" :
                       (uiSettings.slideshowMode == 1 ? "Ručně" : "Auto")); break;
      case 4: snprintf(value, sizeof(value), "%s",
                       uiSettings.slideshowLayout ? "Celá" : "Info"); break;
      case 5: snprintf(value, sizeof(value), "%s",
                       uiSettings.techEnabled ? "On" : "Off"); break;
      default: snprintf(value, sizeof(value), "%s",
                        uiSettings.defaultView == 0 ? "TEXT" :
                        (uiSettings.defaultView == 1 ? "TECH" : "SLS")); break;
    }
    drawUtf8Text(String(value), 98, y, ST77XX_GREEN, 1, 60);
  }
  drawUtf8Text("Dlouhý SELECT = zpět", 2, 116, UI_GRAY, 1, 156);
}

void applyUiBacklightSettings() {
  backlight.configure(uiSettings.brightness, uiSettings.dimLevel,
                      static_cast<uint32_t>(
                          UI_DIM_TIMEOUT_SECONDS[uiSettings.dimTimeoutIndex]) * 1000UL);
  backlight.noteActivity(millis());
}

void adjustMenu(int8_t direction) {
  switch (menuSelection) {
    case 0:
      uiSettings.brightness = constrain(
          static_cast<int>(uiSettings.brightness) + direction * 10, 20, 100);
      if (uiSettings.dimLevel > uiSettings.brightness) {
        uiSettings.dimLevel = uiSettings.brightness;
      }
      break;
    case 1:
      uiSettings.dimLevel = constrain(
          static_cast<int>(uiSettings.dimLevel) + direction * 5, 5,
          uiSettings.brightness);
      break;
    case 2:
      uiSettings.dimTimeoutIndex = constrain(
          static_cast<int>(uiSettings.dimTimeoutIndex) + direction, 0, 3);
      break;
    case 3:
      uiSettings.slideshowMode = constrain(
          static_cast<int>(uiSettings.slideshowMode) + direction, 0, 2);
      if (!Dab.setSlideshowEnabled(uiSettings.slideshowMode != 0)) {
        uiSettings.slideshowMode = 0;
      }
      if (uiSettings.slideshowMode == 0 && uiSettings.defaultView == 2) {
        uiSettings.defaultView = 0;
      }
      uiDefaultSlideshowPending = dabMode == 1 &&
                                  uiSettings.defaultView == 2 &&
                                  uiSettings.slideshowMode != 0;
      break;
    case 4:
      uiSettings.slideshowLayout = !uiSettings.slideshowLayout;
      break;
    case 5:
      uiSettings.techEnabled = !uiSettings.techEnabled;
      if (!uiSettings.techEnabled && uiSettings.defaultView == 1) {
        uiSettings.defaultView = 0;
      }
      break;
    case 6: {
      uint8_t candidate = uiSettings.defaultView;
      for (uint8_t tries = 0; tries < 3; ++tries) {
        candidate = direction > 0 ? (candidate + 1) % 3
                                  : (candidate == 0 ? 2 : candidate - 1);
        if ((candidate != 1 || uiSettings.techEnabled) &&
            (candidate != 2 || uiSettings.slideshowMode != 0)) {
          uiSettings.defaultView = candidate;
          uiDefaultSlideshowPending = dabMode == 1 && candidate == 2 &&
                                      uiSettings.slideshowMode != 0;
          break;
        }
      }
      break;
    }
  }
  applyUiBacklightSettings();
  saveUiSettingsDelayed();
  renderMenu();
}

void openSettingsMenu() {
  uiViewBeforeModal = (uiView == UiView::Tech || uiView == UiView::Slideshow)
                          ? uiView : UiView::Text;
  uiView = UiView::Menu;
  menuSelection = 0;
  menuTop = 0;
  renderMenu();
}

void closeSettingsMenu() {
  saveUiSettingsDelayed();
  uiView = uiViewBeforeModal;
  renderCurrentUiView();
}

bool handleUiButtonEvent(const ButtonEvent& event) {
  const bool step = event.type == ButtonEventType::ShortPress ||
                    event.type == ButtonEventType::Repeat;
  if (uiView == UiView::Menu) {
    if (event.button == ButtonId::ChannelUp && step) {
      menuSelection = (menuSelection + 1) % 7;
      renderMenu();
    } else if (event.button == ButtonId::ChannelDown && step) {
      menuSelection = menuSelection == 0 ? 6 : menuSelection - 1;
      renderMenu();
    } else if (event.button == ButtonId::VolumeUp && step) {
      adjustMenu(1);
    } else if (event.button == ButtonId::VolumeDown && step) {
      adjustMenu(-1);
    } else if (event.button == ButtonId::Select &&
               event.type == ButtonEventType::ShortPress) {
      adjustMenu(1);
    } else if (event.button == ButtonId::Select &&
               event.type == ButtonEventType::LongPress) {
      closeSettingsMenu();
    }
    return true;
  }

  if (uiView == UiView::StationList) {
    if (event.button == ButtonId::ChannelUp && step) {
      moveStationList(1);
      return true;
    }
    if (event.button == ButtonId::ChannelDown && step) {
      moveStationList(-1);
      return true;
    }
    if (event.button == ButtonId::Select &&
        event.type == ButtonEventType::ShortPress) {
      tuneStationListSelection();
      return true;
    }
    if (event.button == ButtonId::Select &&
        event.type == ButtonEventType::LongPress) {
      openSettingsMenu();
      return true;
    }
    if (event.button == ButtonId::Scan &&
        event.type == ButtonEventType::ShortPress) {
      uiView = uiViewBeforeModal;
      renderCurrentUiView();
      return true;
    }
    if (event.button == ButtonId::Band &&
        event.type == ButtonEventType::ShortPress && Dab.ready()) {
      openListAfterBandReady = true;
      dabMode = !dabMode;
      saveModeToEEPROM(dabMode);
      clearScreen();
      TFT_aff(dabMode == 1 ? "Starting DAB" : "Starting FM", 40);
      Dab.beginAsync(dabMode == 1 ? 0 : 1);
      return true;
    }
  }
  return false;
}

void serviceUi(uint32_t now) {
  flushUiDirty();
  if (Dab.takeSlideshowUpdate()) {
    uiSlideshowDecodePending = true;
    markUiDirty(UI_DIRTY_STATUS);
  }

  if (!scanActive() && uiSlideshowDecodePending && dabMode == 1 &&
      uiSettings.slideshowMode != 0 && Dab.slideshowAvailable()) {
    const bool alreadyVisible = uiView == UiView::Slideshow;
    const bool canAutoOpen = uiSettings.slideshowMode == 2 &&
                             (uiView == UiView::Text || uiView == UiView::Tech);
    const bool canDefaultOpen = uiDefaultSlideshowPending &&
                                uiSettings.defaultView == 2 &&
                                (uiView == UiView::Text || uiView == UiView::Tech);
    if (alreadyVisible || canAutoOpen || canDefaultOpen) {
      if (!Dab.urgentDataPending()) {
        const UiView previousView = uiView;
        if (canDefaultOpen) uiDefaultSlideshowPending = false;
        if (canAutoOpen || canDefaultOpen) uiView = UiView::Slideshow;
        uiSlideshowDecodePending = false;
        const bool rendered = renderSlideshowScreen();
        if (!rendered && canAutoOpen) {
          uiView = previousView;
          renderCurrentUiView();
        }
      }
    } else {
      // Manual mode keeps the compressed image ready in RAM and decodes only
      // when the user actually opens the SLS screen.
      uiSlideshowDecodePending = false;
    }
  }

  if (scanActive() || uiView != UiView::Text) return;

  const String stationName = currentUiStationName();
  constexpr uint16_t visibleGlyphs = 13;
  const uint16_t stationGlyphs = utf8CodepointCount(stationName);
  if (stationGlyphs > visibleGlyphs &&
      static_cast<int32_t>(now - uiStationScrollDeadlineMs) >= 0) {
    const uint16_t maximumOffset = stationGlyphs - visibleGlyphs;
    if (uiStationScrollGlyph < maximumOffset) {
      ++uiStationScrollGlyph;
      uiStationScrollDeadlineMs = now + 170;
      markUiDirty(UI_DIRTY_STATION);
    } else if (!uiStationScrollEndHold) {
      uiStationScrollEndHold = true;
      uiStationScrollDeadlineMs = now + 1000;
    } else {
      uiStationScrollGlyph = 0;
      uiStationScrollEndHold = false;
      uiStationScrollDeadlineMs = now + 1200;
      markUiDirty(UI_DIRTY_STATION);
    }
  }

  if (uiBroadcastText.length() == 0 || uiBroadcastLoopGlyphs == 0 ||
      static_cast<int32_t>(now - uiTextPageDeadlineMs) < 0) {
    return;
  }
  uiTextScrollGlyph = (uiTextScrollGlyph + 1U) % uiBroadcastLoopGlyphs;
  uiTextPageDeadlineMs = now + UI_TEXT_SCROLL_STEP_MS;
  markUiDirty(UI_DIRTY_TEXT);
}
