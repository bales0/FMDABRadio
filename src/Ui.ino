constexpr uint8_t UI_LIST_ROWS = 7;
constexpr uint8_t UI_TEXT_LINE_GLYPHS = 26;
constexpr uint8_t UI_TEXT_WINDOW_GLYPHS = UI_TEXT_LINE_GLYPHS;
constexpr uint16_t UI_TEXT_SCROLL_STEP_MS = 35;
const uint16_t UI_DIM_TIMEOUT_SECONDS[] = {15, 30, 60, 120};
constexpr uint16_t UI_GRAY = 0x8410;
constexpr uint16_t UI_DARK_LINE = 0x3186;
constexpr uint16_t UI_ORANGE = 0xFD20;
constexpr uint8_t UI_MENU_ITEMS = 10;
struct UiListRowCache {
  bool valid = false;
  uint16_t item = 0;
  bool selected = false;
  String label;
  String value;
};
UiListRowCache stationListRows[UI_LIST_ROWS];
UiListRowCache menuRows[5];
bool stationListPainted = false;
bool menuPainted = false;
bool stationListWasEmpty = false;
String stationListHeader;
uint8_t paintedMenuLanguage = 0;
uint8_t paintedMenuTheme = 0;
uint8_t menuFmRegionOnOpen = static_cast<uint8_t>(FmRegion::Europe);
uint32_t uiObservedMotPackets = 0;
uint32_t uiMotActivityUntilMs = 0;
uint32_t uiMotReadyUntilMs = 0;
bool uiObservedMotCollecting = false;
uint8_t uiObservedMotProgress = 0;
const char* const UI_DAB_CHANNELS[DAB_FREQS] = {
    "5A", "5B", "5C", "5D", "6A", "6B", "6C", "6D",
    "7A", "7B", "7C", "7D", "8A", "8B", "8C", "8D",
    "9A", "9B", "9C", "9D", "10A", "10B", "10C", "10D",
    "11A", "11B", "11C", "11D", "12A", "12B", "12C", "12D",
    "13A", "13B", "13C", "13D", "13E", "13F"};

const char* uiText(const char* english, const char* czech) {
  return uiSettings.language == 1 ? czech : english;
}

uint16_t uiAccentColor() {
  return uiSettings.theme == 1 ? UI_ORANGE
       : uiSettings.theme == 2 ? 0x5DDF : ST77XX_CYAN;
}

uint16_t uiStationColor() {
  return uiSettings.theme == 1 ? ST77XX_YELLOW
       : uiSettings.theme == 2 ? 0xB7FF : ST77XX_RED;
}

int16_t uiSignalValue(int8_t dbuv) {
  if (uiSettings.signalUnits == 1) return static_cast<int16_t>(dbuv) + 6;
  if (uiSettings.signalUnits == 2) return dbuv;
  return static_cast<int16_t>(dbuv) - 107;
}

const char* uiSignalUnitName() {
  return uiSettings.signalUnits == 1 ? "dBf"
       : uiSettings.signalUnits == 2 ? "dBuV" : "dBm";
}

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

bool readUiStationLabel(uint16_t channel, char* label, size_t labelSize,
                        uint16_t& frequency, uint8_t& charset) {
  memset(label, 0, labelSize);
  frequency = 0;
  charset = DAB_CHARSET_EBU_LATIN;
  const uint16_t maximum = dabMode == 1 ? MAX_DAB_STATIONS : MAX_FM_STATIONS;
  if (channel < 1 || channel > maximum) return false;
  if (dabMode == 1) {
    uint8_t record[28] = {0};
    const int result = extEEPROM.read(
        ADDR_DAB_CHANNEL + 28U * (channel - 1U), record, sizeof(record));
    if (result != 0 || record[0] != channel) return false;
    memcpy(label, record + 11, min(labelSize - 1, static_cast<size_t>(16)));
    charset = record[27];
  } else {
    uint8_t record[13] = {0};
    const int result = extEEPROM.read(
        ADDR_FM_CHANNEL + 13U * (channel - 1U), record, sizeof(record));
    if (result != 0 || record[0] != channel) return false;
    frequency = 100U * record[2] + 10U * record[1];
    memcpy(label, record + 4, min(labelSize - 1, static_cast<size_t>(8)));
    if (label[0] == 0 || strncmp(label, "unknown?", 8) == 0) {
      snprintf(label, labelSize, "%u.%1uMHz", frequency / 100,
               (frequency % 100) / 10);
    }
  }
  return true;
}

void markUiDirty(uint8_t regions) {
  uiDirtyFlags |= regions;
}

dab_scheduler::SignalDisplayFilter uiSignalFilter;
uint32_t uiSignalNextStepMs = 0;

int16_t uiRoundedTenths(int16_t value) {
  return value >= 0 ? (value + 5) / 10 : (value - 5) / 10;
}

int8_t uiDisplayedRssi() {
  return uiSignalFilter.valid
      ? static_cast<int8_t>(uiRoundedTenths(uiSignalFilter.displayedSignal10))
      : Dab.signalstrength;
}

int8_t uiDisplayedSnr() {
  return uiSignalFilter.valid
      ? static_cast<int8_t>(uiRoundedTenths(uiSignalFilter.displayedCnr10))
      : Dab.snr;
}

uint8_t uiDisplayedQuality() {
  return uiSignalFilter.valid
      ? static_cast<uint8_t>((uiSignalFilter.displayedQuality10 + 5U) / 10U)
      : Dab.quality;
}

void serviceUiSignalFilter(uint32_t now) {
  const bool fm = dabMode == 0;
  const bool newSample = uiSignalFilter.acceptSample(
      Dab.signalSampleGeneration(), fm,
      static_cast<int16_t>(Dab.signalstrength) * 10, Dab.snr, Dab.quality);
  const uint32_t interval = fm
      ? dab_scheduler::FM_SIGNAL_UI_INTERVAL_MS
      : dab_scheduler::DAB_SIGNAL_UI_INTERVAL_MS;
  if (newSample && uiSignalNextStepMs == 0U) uiSignalNextStepMs = now;
  if (uiSignalNextStepMs != 0U &&
      dab_scheduler::deadlineReached(now, uiSignalNextStepMs)) {
    uiSignalFilter.stepDisplay(false);
    uiSignalNextStepMs = now + interval;
    markUiDirty(UI_DIRTY_SIGNAL);
  }
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
  uint32_t fmClockGeneration;
  uint8_t motProgress;
  uint8_t afCount;
  bool valid;
  bool dabplus;
  bool fmPilot;
  bool rdsSync;
  bool tp;
  bool ta;
  bool motCollecting;
  bool motAvailable;
  bool afActive;
  char ps[9];
};

void noteUiStatusChanged() {
  static UiStatusSnapshot previous = {};
  static bool previousValid = false;
  UiStatusSnapshot current = {};
  current.signalstrength = uiDisplayedRssi();
  current.snr = uiDisplayedSnr();
  current.quality = uiDisplayedQuality();
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
  current.fmClockGeneration = Dab.fmClockGeneration();
  current.motProgress = Dab.slideshowProgress();
  current.afCount = Dab.fmAfList().count;
  current.valid = Dab.valid;
  current.dabplus = Dab.dabplus;
  current.fmPilot = Dab.fmPilot;
  current.rdsSync = Dab.rdsSync;
  current.tp = Dab.tp;
  current.ta = Dab.ta;
  current.motCollecting = Dab.slideshowCollecting();
  current.motAvailable = Dab.slideshowAvailable();
  current.afActive = fmAfActive();
  memcpy(current.ps, Dab.ps, sizeof(current.ps));
  const bool signalChanged = !previousValid ||
      current.signalstrength != previous.signalstrength ||
      current.snr != previous.snr;
  const bool dabTypeChanged = !previousValid ||
      current.dabplus != previous.dabplus;
  const bool fmHeaderChanged = !previousValid ||
      current.fmPilot != previous.fmPilot ||
      current.fmStereoBlend != previous.fmStereoBlend;
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
      current.ta != previous.ta ||
      current.motProgress != previous.motProgress ||
      current.motCollecting != previous.motCollecting ||
      current.motAvailable != previous.motAvailable ||
      current.afCount != previous.afCount ||
      current.afActive != previous.afActive ||
      current.fmClockGeneration != previous.fmClockGeneration ||
      memcmp(current.ps, previous.ps, sizeof(current.ps)) != 0;
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
  (void)fmHeaderChanged;
  if (diagnosticsChanged) markUiDirty(UI_DIRTY_TECH);
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
    snprintf(detail, sizeof(detail), "%s %lu.%03luMHz",
             ensemble < DAB_FREQS ? UI_DAB_CHANNELS[ensemble] : "--",
             static_cast<unsigned long>(frequency / 1000),
             static_cast<unsigned long>(frequency % 1000));
  } else {
    const uint16_t frequency = 100U * stationFM_h + 10U * stationFM_l;
    snprintf(detail, sizeof(detail), "%u.%1uMHz", frequency / 100,
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
  String name;
  if (dabMode == 1) {
    // dabName always represents the selected EEPROM record. A verified live
    // Service Info label is copied into it by refreshStoredDabLabel(). Reading
    // ActiveLabel directly here allowed stale scan metadata to hide the name
    // of the station being previewed or restored after autoscan.
    name = decodeBroadcastText(reinterpret_cast<const uint8_t*>(dabName), 16,
                               dabCharset);
  } else {
    name = decodeRdsText(reinterpret_cast<const uint8_t*>(fmName), 8);
  }
  name.trim();
  if (name.length() == 0 && dabMode == 1) {
    char fallback[17];
    if (serviceid != 0U) {
      snprintf(fallback, sizeof(fallback), "DAB service");
    } else {
      snprintf(fallback, sizeof(fallback), "DAB station");
    }
    name = fallback;
  } else if (name.length() == 0) {
    char fallback[16];
    snprintf(fallback, sizeof(fallback), "%u.%1uMHz", stationFM_h,
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
  drawUtf8Text(name, x, 16,
               stationPreviewActive ? UI_GRAY : uiStationColor(),
               2, screenWidth);
  tft.drawFastHLine(0, 40, screenWidth, UI_DARK_LINE);
}

void renderUiTextArea() {
  if (uiView != UiView::Text || scanActive()) return;
  tft.fillRect(0, 52, screenWidth, 13, ST77XX_BLACK);
  if (uiBroadcastText.length() == 0) {
    drawUtf8Text("Waiting for broadcast text", 2, 55, 0x7BEF, 1, 156);
    return;
  }
  const uint16_t glyphOffset = uiTextScrollGlyph / 6U;
  const uint8_t pixelOffset = uiTextScrollGlyph % 6U;
  const String line = uiCircularTextSlice(glyphOffset,
                                           UI_TEXT_LINE_GLYPHS + 2U);
  drawUtf8Text(line, 2 - pixelOffset, 55, ST77XX_WHITE, 1,
               158U + pixelOffset);
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
  const int8_t displayedSnr = uiDisplayedSnr();
  const int8_t displayedRssi = uiDisplayedRssi();
  if (dabMode == 1) {
    drawUtf8Text("CNR", 2, 98, ST77XX_CYAN, 1, 24);
    snprintf(value, sizeof(value), "%d dB", displayedSnr);
    drawUtf8Text(String(value), 28, 98, uiSignalColor(displayedSnr, true), 1, 45);
    drawUtf8Text("RSSI", 78, 98, ST77XX_CYAN, 1, 30);
    snprintf(value, sizeof(value), "%d%s", uiSignalValue(displayedRssi),
             uiSignalUnitName());
    drawUtf8Text(String(value), 110, 98,
                 uiRssiColor(displayedRssi, true), 1, 48);

  } else {
    drawUtf8Text("SNR", 2, 98, ST77XX_CYAN, 1, 24);
    snprintf(value, sizeof(value), "%d dB", displayedSnr);
    drawUtf8Text(String(value), 28, 98, uiSignalColor(displayedSnr, false), 1, 45);
    drawUtf8Text("RSSI", 78, 98, ST77XX_CYAN, 1, 30);
    snprintf(value, sizeof(value), "%d%s", uiSignalValue(displayedRssi),
             uiSignalUnitName());
    drawUtf8Text(String(value), 110, 98,
                 uiRssiColor(displayedRssi, false), 1, 48);
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
    if (Dab.bitrate == 0U || Dab.samplerate == 0U) {
      snprintf(value, sizeof(value), "Audio loading...");
    } else {
      snprintf(value, sizeof(value), "%s %ukbit %ukHz %s",
               Dab.dabplus ? "HE-AAC" : "MP2", Dab.bitrate,
               Dab.samplerate / 1000U,
               shortAudioMode[static_cast<uint8_t>(Dab.mode) & 0x03U]);
    }
    drawUtf8Text(String(value), 2, 83, ST77XX_WHITE, 1, 156);
    drawUtf8Text("RX", 2, 113, Dab.valid ? ST77XX_GREEN : ST77XX_RED, 1, 12);
    drawUtf8Text("FIC", 20, 113, ST77XX_CYAN, 1, 18);
    const uint8_t displayedQuality = uiDisplayedQuality();
    snprintf(value, sizeof(value), "%u%%", displayedQuality);
    drawUtf8Text(String(value), 44, 113, uiFicColor(displayedQuality), 1, 26);
    const uint32_t now = millis();
    const bool motLoading = Dab.slideshowCollecting();
    const bool motReady = Dab.slideshowAvailable() ||
        (uiMotReadyUntilMs != 0U &&
         static_cast<int32_t>(uiMotReadyUntilMs - now) > 0);
    const bool motPresent = motLoading || motReady ||
        (uiMotActivityUntilMs != 0U &&
         static_cast<int32_t>(uiMotActivityUntilMs - now) > 0);
    drawUtf8Text("MOT", 72, 113,
                 motReady ? ST77XX_GREEN
                          : (motLoading ? ST77XX_YELLOW
                                        : (motPresent ? ST77XX_CYAN : UI_GRAY)),
                 1, 18);
    if (motLoading && Dab.slideshowProgress() != 0U)
      snprintf(value, sizeof(value), "%u%%", Dab.slideshowProgress());
    else if (motLoading) snprintf(value, sizeof(value), "RX");
    else snprintf(value, sizeof(value), "%s", motReady ? "OK" :
                  (motPresent ? "RX" : "--"));
    drawUtf8Text(String(value), 94, 113,
                 motReady ? ST77XX_GREEN
                          : (motLoading ? ST77XX_YELLOW
                                        : (motPresent ? ST77XX_CYAN : UI_GRAY)),
                 1, 30);
    snprintf(value, sizeof(value), "%u/%u", currentDABchannel,
             totalDABchannels);
    drawUtf8Text(String(value), 128, 113, ST77XX_WHITE, 1, 30);
  } else {
    drawUtf8Text(Dab.fmPilot ? "STEREO" : "MONO", 2, 83,
                 Dab.fmPilot ? ST77XX_GREEN : ST77XX_YELLOW, 1, 44);
    const char* rdsLabel = fmRegionProfile(uiSettings.fmRegion).rbds ? "RBDS" : "RDS";
    drawUtf8Text(rdsLabel, 52, 83,
                 Dab.rdsSync ? ST77XX_GREEN : ST77XX_RED, 1, 24);
    drawUtf8Text("TP", 86, 83, Dab.tp ? ST77XX_GREEN : UI_GRAY, 1, 18);
    drawUtf8Text("TA", 113, 83, Dab.ta ? UI_ORANGE : UI_GRAY, 1, 18);
    uint16_t afColor = UI_GRAY;
    if (uiSettings.fmAfEnabled) {
      afColor = fmAfActive() ? UI_ORANGE
          : (Dab.fmAfList().count != 0 ? ST77XX_GREEN : ST77XX_CYAN);
    }
    drawUtf8Text("AF", 139, 83, afColor, 1, 18);
    snprintf(value, sizeof(value), "PTY %u  PI %04X", Dab.pty, Dab.pi);
    drawUtf8Text(String(value), 2, 113, UI_GRAY, 1, 92);
    snprintf(value, sizeof(value), "%u/%u", currentFMchannel,
             totalFMchannels);
    drawUtf8Text(String(value), 128, 113, UI_GRAY, 1, 30);
  }
}

void renderUiStatus() {
  if (scanActive()) return;
  if (uiView == UiView::Text) {
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

bool renderSlideshowScreen() {
  uiView = UiView::Slideshow;
  if (dabMode != 1 || uiSettings.slideshowMode == 0) {
    clearScreen();
    drawUtf8Text("Slideshow unavailable", 7, 51,
                 ST77XX_YELLOW, 1, 148);
    return false;
  }
  if (!Dab.slideshowAvailable()) {
    clearScreen();
    drawUtf8Text("Waiting for slideshow...", 17, 51,
                 ST77XX_CYAN, 1, 130);
    uiSlideshowDecodePending = true;
    return true;
  }
  const bool decoded = renderRamSlideshow(
      tft, Dab.slideshowData(), Dab.slideshowLength(), 0, 0,
      screenWidth, screenHeight, &serialMonitor);
  uiSlideshowDecodePending = false;
  if (!decoded) {
    // The decoder validates before its first pixel write. Keep the last valid
    // screen visible if this carousel object is malformed or unsupported.
    Dab.discardSlideshow();
    markUiDirty(UI_DIRTY_STATUS);
    return false;
  }
  // Keep the complete compressed object available so leaving and reopening
  // the slideshow redraws the same image immediately. acknowledgeSlideshow()
  // only releases collector ownership; the first segment of a genuinely new
  // Transport ID will invalidate and reuse the single arena.
  Dab.acknowledgeSlideshow();
  return true;
}

void renderCurrentUiView() {
  if (uiView == UiView::Slideshow && dabMode == 1 &&
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
  }
}

void renderTechHeader() {
  if (uiView != UiView::Tech || scanActive()) return;
  char line[34];
  tft.fillRect(0, 0, screenWidth, 30, ST77XX_BLACK);
  const char* band = dabMode == 1
      ? (Dab.dabplus ? "DAB+" : "DAB") : "FM";
  drawUtf8Text(band, 2, 1, ST77XX_YELLOW, 1, 24);
  drawUtf8Text("TECH", 61, 1, ST77XX_CYAN, 1, 30);
  snprintf(line, sizeof(line), "V%u", vol);
  drawUtf8Text(String(line), 136, 1, ST77XX_GREEN, 1, 22);
  tft.drawFastHLine(0, 14, screenWidth, UI_DARK_LINE);

  if (dabMode == 1) {
    const uint32_t frequency = ensemble < DAB_FREQS ? Dab.freq_khz(ensemble) : 0;
    snprintf(line, sizeof(line), "%s %lu.%03luMHz",
             ensemble < DAB_FREQS ? UI_DAB_CHANNELS[ensemble] : "--",
             static_cast<unsigned long>(frequency / 1000),
             static_cast<unsigned long>(frequency % 1000));
  } else {
    snprintf(line, sizeof(line), "%u.%1uMHz  %s %u%%", stationFM_h,
             stationFM_l, Dab.fmPilot ? "ST" : "MONO", Dab.fmStereoBlend);
  }
  drawUtf8Text(String(line), 2, 17, ST77XX_WHITE, 1, 156);
}

void renderTechSignal() {
  if (uiView != UiView::Tech || scanActive()) return;
  char line[16];
  const int8_t displayedSnr = uiDisplayedSnr();
  const int8_t displayedRssi = uiDisplayedRssi();
  tft.fillRect(0, 30, screenWidth, 15, ST77XX_BLACK);
  drawUtf8Text(dabMode == 1 ? "CNR" : "SNR", 2, 32, ST77XX_CYAN, 1, 24);
  snprintf(line, sizeof(line), "%d dB", displayedSnr);
  drawUtf8Text(String(line), 28, 32,
               uiSignalColor(displayedSnr, dabMode == 1), 1, 42);
  drawUtf8Text("RSSI", 78, 32, ST77XX_CYAN, 1, 30);
  snprintf(line, sizeof(line), "%d%s", uiSignalValue(displayedRssi),
           uiSignalUnitName());
  drawUtf8Text(String(line), 110, 32,
               uiRssiColor(displayedRssi, dabMode == 1), 1, 46);
}

void renderTechStatus() {
  if (uiView != UiView::Tech || scanActive()) return;
  char line[34];
  tft.fillRect(0, 45, screenWidth, 60, ST77XX_BLACK);

  if (dabMode == 1) {
    drawUtf8Text("FIC", 2, 47, ST77XX_CYAN, 1, 18);
    const uint8_t displayedQuality = uiDisplayedQuality();
    snprintf(line, sizeof(line), "%u%%", displayedQuality);
    drawUtf8Text(String(line), 26, 47, uiFicColor(displayedQuality), 1, 30);
    snprintf(line, sizeof(line), "%s %ukbit",
             Dab.dabplus ? "DAB+" : "DAB", Dab.bitrate);
    drawUtf8Text(String(line), 58, 47, ST77XX_WHITE, 1, 98);

    snprintf(line, sizeof(line), "Audio %u Hz  %s", Dab.samplerate,
             audiomode[Dab.mode]);
    drawUtf8Text(String(line), 2, 62, ST77XX_WHITE, 1, 156);
    snprintf(line, sizeof(line), "SID %08lX",
             static_cast<unsigned long>(serviceid));
    drawUtf8Text(String(line), 2, 77, UI_GRAY, 1, 156);
    snprintf(line, sizeof(line), "CID %08lX  %s",
             static_cast<unsigned long>(compid),
             broadcastCharsetName(Dab.ServiceDataCharset));
    drawUtf8Text(String(line), 2, 92, UI_GRAY, 1, 156);
  } else {
    snprintf(line, sizeof(line), "PI %04X  %s", Dab.pi,
             fm_features::ptyName(
                 Dab.pty, fmRegionProfile(uiSettings.fmRegion).rbds));
    drawUtf8Text(String(line), 2, 47, ST77XX_WHITE, 1, 156);

    const bool rbds = fmRegionProfile(uiSettings.fmRegion).rbds;
    snprintf(line, sizeof(line), "%s %s  TP %u  TA %u",
             rbds ? "RBDS" : "RDS", Dab.rdsSync ? "LOCK" : "--",
             Dab.tp ? 1 : 0, Dab.ta ? 1 : 0);
    drawUtf8Text(String(line), 2, 62,
                 Dab.rdsSync ? ST77XX_GREEN : ST77XX_RED, 1, 156);

    const String decodedPs = decodeRdsText(
        reinterpret_cast<const uint8_t*>(Dab.ps), 8);
    drawUtf8Text(String("PS ") + decodedPs, 2, 77, ST77XX_CYAN, 1, 156);
    if (Dab.fmClockValid) {
      snprintf(line, sizeof(line), "%02u:%02u %02u.%02u.%04u %+d/2h",
               Dab.Hours, Dab.Minutes, Dab.Days, Dab.Months, Dab.Year,
               Dab.fmLocalOffsetHalfHours);
      drawUtf8Text(String(line), 2, 92, UI_GRAY, 1, 156);
    } else {
      drawUtf8Text(rbds ? "Charset Basic RBDS" : "Charset Basic RDS",
                   2, 92, UI_GRAY, 1, 156);
    }
  }
}

void renderTechDiagnostics() {
  if (uiView != UiView::Tech || scanActive()) return;
  char line[34];
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

void renderTechScreen() {
  if (uiView != UiView::Tech || scanActive()) return;
  clearScreen();
  renderTechHeader();
  renderTechSignal();
  renderTechStatus();
  renderTechDiagnostics();
}

void renderTuneScreen() {
  if (uiView != UiView::Tune || scanActive()) return;
  clearScreen();
  drawUtf8Text(uiText("Manual tuning", "Rucni ladeni"), 2, 1,
               uiAccentColor(), 1, 156);
  tft.drawFastHLine(0, 14, screenWidth, UI_DARK_LINE);
  char line[34];
  if (dabMode == 0) {
    const uint16_t frequency = Dab.freq != 0 ? Dab.freq
        : static_cast<uint16_t>(stationFM_h * 100U + stationFM_l * 10U);
    snprintf(line, sizeof(line), "%u.%02u MHz", frequency / 100,
             frequency % 100);
    drawUtf8Text(String(line), 18, 25, uiStationColor(), 2, 140);
    snprintf(line, sizeof(line), "%s  PI %04X", Dab.valid ? "LOCK" : "----",
             Dab.pi);
    drawUtf8Text(String(line), 2, 55, Dab.valid ? ST77XX_GREEN : ST77XX_RED,
                 1, 156);
    snprintf(line, sizeof(line), "AF %s  %u/%u", uiSettings.fmAfEnabled ? "ON" : "OFF",
             Dab.fmAfList().count, Dab.fmAfList().expected);
    drawUtf8Text(String(line), 2, 72, uiAccentColor(), 1, 156);
    drawUtf8Text(uiText("CH+/- tune  SCAN seek", "CH+/- ladit SCAN hledat"),
                 2, 96, ST77XX_WHITE, 1, 156);
  } else {
    const uint32_t frequency = Dab.freq_khz(ensemble);
    snprintf(line, sizeof(line), "%s %lu.%03lu MHz",
             UI_DAB_CHANNELS[ensemble],
             static_cast<unsigned long>(frequency / 1000U),
             static_cast<unsigned long>(frequency % 1000U));
    drawUtf8Text(String(line), 2, 25, uiStationColor(), 1, 156);
    snprintf(line, sizeof(line), "%s  services %u",
             Dab.valid ? "LOCK" : "----", Dab.numberofservices);
    drawUtf8Text(String(line), 2, 48, Dab.valid ? ST77XX_GREEN : ST77XX_RED,
                 1, 156);
    drawUtf8Text(uiText("CH+/- multiplex", "CH+/- multiplex"), 2, 78,
                 ST77XX_WHITE, 1, 156);
  }
  drawUtf8Text(uiText("SELECT next screen", "SELECT dalsi obrazovka"), 2, 114,
               UI_GRAY, 1, 156);
}

void manualTuneStep(int8_t direction) {
  if (!Dab.ready() || scanActive()) return;
  stationPreviewActive = false;
  if (dabMode == 0) {
    manualDabSelectPending = false;
    const FmRegionProfile& profile = fmRegionProfile(uiSettings.fmRegion);
    uint16_t frequency = Dab.freq != 0 ? Dab.freq
        : static_cast<uint16_t>(stationFM_h * 100U + stationFM_l * 10U);
    frequency = stepFmFrequency(
        frequency, direction * static_cast<int16_t>(profile.seekSpacing10kHz),
        uiSettings.fmRegion);
    stationFM_h = frequency / 100U;
    stationFM_l = (frequency % 100U) / 10U;
    flag_sel = false;
    Dab.requestFmTune(frequency);
  } else {
    ensemble = fm_features::nextDabChannel(ensemble, direction > 0);
    manualDabSelectPending = Dab.requestDabTune(ensemble);
  }
  renderTuneScreen();
}

void cycleUiScreen() {
  if (uiView == UiView::Text && dabMode == 1 &&
      uiSettings.slideshowMode != 0) {
    uiView = UiView::Slideshow;
  } else {
    uiView = UiView::Text;
  }
  stationPreviewActive = false;
  renderCurrentUiView();
}

void renderStationList() {
  if (uiView != UiView::StationList) return;
  const uint16_t total = currentUiStationCount();
  const bool full = !stationListPainted || stationListWasEmpty != (total == 0);
  if (full) {
    clearScreen();
    for (uint8_t row = 0; row < UI_LIST_ROWS; ++row)
      stationListRows[row].valid = false;
    tft.drawFastHLine(0, 14, screenWidth, UI_DARK_LINE);
  }
  stationListPainted = true;
  stationListWasEmpty = total == 0;
  char header[28];
  snprintf(header, sizeof(header), "%s stations  V%u",
           dabMode == 1 ? "DAB" : "FM", vol);
  if (full || stationListHeader != header) {
    tft.fillRect(0, 0, screenWidth, 14, ST77XX_BLACK);
    drawUtf8Text(String(header), 2, 1, ST77XX_CYAN, 1, 156);
    stationListHeader = header;
  }
  if (total == 0) {
    if (full)
      drawUtf8Text("No stations - hold SCAN", 2, 36, ST77XX_RED, 1, 156);
    return;
  }
  if (stationListSelection < 1) stationListSelection = 1;
  if (stationListSelection > total) stationListSelection = total;
  const uint16_t lastTop = total > UI_LIST_ROWS
                               ? total - UI_LIST_ROWS + 1U : 1U;
  // Keep the selection on the middle row while there are enough entries
  // on both sides. Clamp the window at either end so the cursor reaches
  // the first/last row before the next navigation step wraps the selection.
  constexpr uint8_t middleRow = UI_LIST_ROWS / 2U;
  stationListTop = stationListSelection > middleRow
                       ? stationListSelection - middleRow : 1U;
  if (stationListTop > lastTop) stationListTop = lastTop;
  for (uint8_t row = 0; row < UI_LIST_ROWS; ++row) {
    const uint16_t channel = stationListTop + row;
    const int16_t y = 18 + row * 15;
    UiListRowCache& cached = stationListRows[row];
    if (channel > total) {
      if (cached.valid) tft.fillRect(0, y, screenWidth, 14, ST77XX_BLACK);
      cached.valid = false;
      continue;
    }
    const bool selected = channel == stationListSelection;
    const uint16_t rowColor = selected ? 0x04B6
                                       : (row & 1U ? 0x1082 : ST77XX_BLACK);
    char label[18];
    uint16_t frequency;
    uint8_t charset;
    const bool recordValid = readUiStationLabel(
        channel, label, sizeof(label), frequency, charset);
    char prefix[6];
    snprintf(prefix, sizeof(prefix), "%3u ", channel);
    const String decodedLabel = !recordValid ? String("<read error>") :
        (dabMode == 1
        ? decodeBroadcastText(reinterpret_cast<const uint8_t*>(label), 16,
                              charset)
        : decodeRdsText(reinterpret_cast<const uint8_t*>(label), 8));
    if (cached.valid && cached.item == channel &&
        cached.selected == selected && cached.label == decodedLabel) continue;
    tft.fillRect(0, y, screenWidth, 14, rowColor);
    drawUtf8Text(String(prefix), 1, y, selected ? ST77XX_WHITE : 0x7BEF, 1, 28);
    drawUtf8Text(decodedLabel, 30, y,
                 selected ? ST77XX_WHITE : ST77XX_CYAN, 1, 128);
    cached.valid = true;
    cached.item = channel;
    cached.selected = selected;
    cached.label = decodedLabel;
  }
}

void openStationList() {
  stationListPainted = false;
  uiViewBeforeModal = uiView == UiView::Slideshow ? uiView : UiView::Text;
  uiView = UiView::StationList;
  stationListSelection = dabMode == 1 ? currentDABchannel : currentFMchannel;
  const uint16_t total = currentUiStationCount();
  if (total == 0) {
    stationListSelection = stationListTop = 1;
  } else {
    if (stationListSelection < 1) stationListSelection = 1;
    if (stationListSelection > total) stationListSelection = total;
  }
  serialMonitor.printf("[UI] station list open: band=%s total=%u selected=%u free=%u min=%u largest=%u stackHwm=%u\n",
                dabMode == 1 ? "DAB" : "FM", total, stationListSelection,
                ESP.getFreeHeap(),
                heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  renderStationList();
}

void moveStationList(int8_t direction) {
  const uint16_t total = currentUiStationCount();
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
  const bool full = !menuPainted;
  const bool styleChanged = full || paintedMenuLanguage != uiSettings.language ||
      paintedMenuTheme != uiSettings.theme;
  if (full) {
    clearScreen();
    for (uint8_t row = 0; row < 5; ++row) menuRows[row].valid = false;
    tft.drawFastHLine(0, 14, screenWidth, UI_DARK_LINE);
  }
  if (styleChanged) {
    tft.fillRect(0, 0, screenWidth, 14, ST77XX_BLACK);
    drawUtf8Text(uiText("Settings", "Nastaveni"), 2, 1, uiAccentColor(), 1, 156);
    tft.fillRect(0, 115, screenWidth, 13, ST77XX_BLACK);
    drawUtf8Text(uiText("Hold SELECT = back", "Drzet SELECT = zpet"),
                 2, 116, UI_GRAY, 1, 156);
  }
  menuPainted = true;
  paintedMenuLanguage = uiSettings.language;
  paintedMenuTheme = uiSettings.theme;
  constexpr uint8_t visibleItems = 5;
  const char* labelsEn[UI_MENU_ITEMS] = {
      "Brightness", "Dim level", "Dim after", "Slideshow",
      "FM region", "FM AF", "Signal units", "Theme", "Language",
      "Serial control"};
  const char* labelsCs[UI_MENU_ITEMS] = {
      "Jas", "Jas v klidu", "Ztlumit za", "Slideshow",
      "FM oblast", "FM AF", "Jednotky", "Motiv", "Jazyk",
      "Seriove rizeni"};
  if (menuSelection < menuTop) menuTop = menuSelection;
  if (menuSelection >= menuTop + visibleItems) {
    menuTop = menuSelection - visibleItems + 1;
  }
  for (uint8_t row = 0; row < visibleItems; ++row) {
    const uint8_t item = menuTop + row;
    const int16_t y = 19 + row * 19;
    UiListRowCache& cached = menuRows[row];
    if (item >= UI_MENU_ITEMS) {
      if (cached.valid) tft.fillRect(0, y, screenWidth, 18, ST77XX_BLACK);
      cached.valid = false;
      continue;
    }
    const bool selected = item == menuSelection;
    const uint16_t background = selected ? 0x04B6
        : (item & 1U ? 0x1082 : ST77XX_BLACK);
    const String label = uiSettings.language == 1 ? labelsCs[item] : labelsEn[item];
    char value[15];
    switch (item) {
      case 0: snprintf(value, sizeof(value), "%u%%", uiSettings.brightness); break;
      case 1: snprintf(value, sizeof(value), "%u%%", uiSettings.dimLevel); break;
      case 2: snprintf(value, sizeof(value), "%us",
                       UI_DIM_TIMEOUT_SECONDS[uiSettings.dimTimeoutIndex]); break;
      case 3: snprintf(value, sizeof(value), "%s",
                       uiSettings.slideshowMode == 0 ? "Off" :
                       (uiSettings.slideshowMode == 1 ? "Manual" : "Auto")); break;
      case 4: snprintf(value, sizeof(value), "%s",
                       fmRegionProfile(uiSettings.fmRegion).menuName); break;
      case 5: snprintf(value, sizeof(value), "%s",
                       uiSettings.fmAfEnabled ? "On" : "Off"); break;
      case 6: snprintf(value, sizeof(value), "%s", uiSignalUnitName()); break;
      case 7: snprintf(value, sizeof(value), "%s",
                        uiSettings.theme == 0 ? "Classic" :
                        (uiSettings.theme == 1 ? "Amber" : "Ice")); break;
      case 8: snprintf(value, sizeof(value), "%s",
                        uiSettings.language == 0 ? "English" : "Cestina"); break;
      default: snprintf(value, sizeof(value), "%s",
                        uiSettings.serialControl ? "On" : "Off"); break;
    }
    const bool rowChanged = styleChanged || !cached.valid ||
        cached.item != item || cached.selected != selected || cached.label != label;
    if (rowChanged) {
      tft.fillRect(0, y, screenWidth, 18, background);
      drawUtf8Text(label, 2, y,
                   selected ? ST77XX_WHITE : uiAccentColor(), 1, 92);
    } else if (cached.value != value) {
      tft.fillRect(98, y, screenWidth - 98, 18, background);
    }
    if (rowChanged || cached.value != value)
      drawUtf8Text(String(value), 98, y, ST77XX_GREEN, 1, 60);
    cached.valid = true;
    cached.item = item;
    cached.selected = selected;
    cached.label = label;
    cached.value = value;
  }
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
      uiSettings.slideshowMode = direction > 0
          ? (uiSettings.slideshowMode + 1U) % 3U
          : (uiSettings.slideshowMode == 0 ? 2U
                                          : uiSettings.slideshowMode - 1U);
      if (!Dab.setSlideshowEnabled(uiSettings.slideshowMode != 0)) {
        uiSettings.slideshowMode = 0;
      }
      uiDefaultSlideshowPending = false;
      break;
    case 4:
      if (direction > 0) {
        uiSettings.fmRegion =
            static_cast<uint8_t>((sanitizeFmRegion(uiSettings.fmRegion) + 1U) %
                                 FM_REGION_COUNT);
      } else {
        const uint8_t region = sanitizeFmRegion(uiSettings.fmRegion);
        uiSettings.fmRegion = region == 0 ? FM_REGION_COUNT - 1 : region - 1;
      }
      break;
    case 5:
      uiSettings.fmAfEnabled = !uiSettings.fmAfEnabled;
      break;
    case 6:
      uiSettings.signalUnits = direction > 0
          ? (uiSettings.signalUnits + 1U) % 3U
          : (uiSettings.signalUnits == 0 ? 2 : uiSettings.signalUnits - 1U);
      break;
    case 7:
      uiSettings.theme = direction > 0
          ? (uiSettings.theme + 1U) % 3U
          : (uiSettings.theme == 0 ? 2 : uiSettings.theme - 1U);
      break;
    case 8:
      uiSettings.language = !uiSettings.language;
      break;
    case 9:
      uiSettings.serialControl = !uiSettings.serialControl;
      break;
  }
  applyUiBacklightSettings();
  saveUiSettingsDelayed();
  renderMenu();
}

void openSettingsMenu() {
  menuPainted = false;
  uiViewBeforeModal = uiView == UiView::Slideshow ? uiView : UiView::Text;
  uiView = UiView::Menu;
  menuSelection = 0;
  menuTop = 0;
  menuFmRegionOnOpen = sanitizeFmRegion(uiSettings.fmRegion);
  renderMenu();
}

void closeSettingsMenu() {
  const bool fmRegionChanged =
      sanitizeFmRegion(uiSettings.fmRegion) != menuFmRegionOnOpen;
  saveUiSettingsDelayed();
  uiView = uiViewBeforeModal;
  if (fmRegionChanged) {
    applyFmRegionSelection();
  } else {
    renderCurrentUiView();
  }
}

bool handleUiButtonEvent(const ButtonEvent& event) {
  const bool step = event.type == ButtonEventType::ShortPress ||
                    event.type == ButtonEventType::Repeat;
  if (uiView == UiView::Menu) {
    if (event.button == ButtonId::ChannelUp && step) {
      menuSelection = (menuSelection + 1) % UI_MENU_ITEMS;
      renderMenu();
    } else if (event.button == ButtonId::ChannelDown && step) {
      menuSelection = menuSelection == 0 ? UI_MENU_ITEMS - 1 : menuSelection - 1;
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
      startBandTransition();
      return true;
    }
  }
  return false;
}

void serviceUi(uint32_t now) {
  serviceUiSignalFilter(now);
  if (uiBandStarting) return;
  const uint32_t motPackets = Dab.motPacketCount();
  const bool motCollecting = Dab.slideshowCollecting();
  const uint8_t motProgress = Dab.slideshowProgress();
  if (motPackets != uiObservedMotPackets) {
    uiObservedMotPackets = motPackets;
    uiMotActivityUntilMs = now + 3000U;
    markUiDirty(UI_DIRTY_STATUS);
  }
  if (motCollecting != uiObservedMotCollecting ||
      motProgress != uiObservedMotProgress) {
    uiObservedMotCollecting = motCollecting;
    uiObservedMotProgress = motProgress;
    markUiDirty(UI_DIRTY_STATUS);
  }
  if (Dab.slideshowAvailable()) uiMotReadyUntilMs = now + 5000U;
  if (uiMotActivityUntilMs != 0U &&
      static_cast<int32_t>(now - uiMotActivityUntilMs) >= 0) {
    uiMotActivityUntilMs = 0;
    markUiDirty(UI_DIRTY_STATUS);
  }
  if (uiMotReadyUntilMs != 0U && !Dab.slideshowAvailable() &&
      static_cast<int32_t>(now - uiMotReadyUntilMs) >= 0) {
    uiMotReadyUntilMs = 0;
    markUiDirty(UI_DIRTY_STATUS);
  }
  flushUiDirty();
  if (Dab.takeSlideshowUpdate()) {
    uiSlideshowDecodePending = true;
    uiMotReadyUntilMs = now + 5000U;
    markUiDirty(UI_DIRTY_STATUS);
  }

  if (!scanActive() && uiSlideshowDecodePending && dabMode == 1 &&
      uiSettings.slideshowMode != 0 && Dab.slideshowAvailable()) {
    const bool alreadyVisible = uiView == UiView::Slideshow;
    const bool canAutoOpen = uiSettings.slideshowMode == 2 &&
                             uiView == UiView::Text;
    const bool canDefaultOpen = false;
    if (alreadyVisible || canAutoOpen || canDefaultOpen) {
      // A completed image owns the single MOT arena until renderSlideshowScreen()
      // calls discardSlideshow(). Continuous DSRV/INTB activity therefore
      // cannot corrupt it and must not starve the decoder waiting for an
      // artificial "quiet" period.
      const UiView previousView = uiView;
      if (canDefaultOpen) uiDefaultSlideshowPending = false;
      if (canAutoOpen || canDefaultOpen) uiView = UiView::Slideshow;
      uiSlideshowDecodePending = false;
      serialMonitor.printf("[SLS/UI] rendering %lu bytes\n",
                    static_cast<unsigned long>(Dab.slideshowLength()));
      const bool rendered = renderSlideshowScreen();
      if (!rendered && canAutoOpen) {
        uiView = previousView;
        renderCurrentUiView();
      }
    } else {
      // Manual mode keeps the compressed image ready in RAM and decodes only
      // when the user actually opens the SLS screen. Release exclusive UI
      // ownership while retaining the cached image for SELECT.
      Dab.acknowledgeSlideshow();
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
  uiTextScrollGlyph = (uiTextScrollGlyph + 1U) %
                      (static_cast<uint32_t>(uiBroadcastLoopGlyphs) * 6U);
  uiTextPageDeadlineMs = now + UI_TEXT_SCROLL_STEP_MS;
  markUiDirty(UI_DIRTY_TEXT);
}
