

// ESP32 FM/DAB receiver with Si4684, ST7735 TFT and external 24C256 EEPROM.
// Buttons are active-low. GPIO34 (SCAN) and GPIO35 (BAND) do not provide
// internal pull-ups on ESP32 and therefore require external pull-ups to 3.3 V.

#include <SPI.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "DABShield.h"
#include "FmRegion.h"                // Si468x core + ESP32 board/application adapter
#include "Controls.h"
#include "Backlight.h"
#include "TextCodec.h"
#include "Slideshow.h"
#include <Adafruit_GFX.h>             // Core graphics library
#include <Adafruit_ST7735.h>          // Hardware-specific library for ST7735
#include <Wire.h>
#include "SparkFun_External_EEPROM.h" // Click here to get the library: http://librarymanager/All#SparkFun_External_EEPROM
String decodeDABString(const char* text);
uint16_t utf8TextWidth(const String& text, uint8_t size);
void drawUtf8Text(const String& text, int16_t x, int16_t y, uint16_t color,
                  uint8_t size, uint16_t maxWidth);
ExternalEEPROM extEEPROM;

//---------------- Screen connection and definition -----------------------
//
constexpr uint8_t TFT_CS = 12;         // Display chip select
constexpr int8_t TFT_RST = -1;         // Display reset (use of EN from ESP32)
constexpr uint8_t TFT_DC = 25;         // Display data/command select

uint8_t screenWidth  = 160;
uint8_t screenHeight = 128;

Adafruit_ST7735 tft  = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// --------- Keyboard I/O assignation ------
//
constexpr uint8_t vol_up = 4;
constexpr uint8_t vol_down = 5;
constexpr uint8_t scan_sw = 34;
constexpr uint8_t mode_sw = 35;
constexpr uint8_t sel = 32;
constexpr uint8_t ch_down = 16;
constexpr uint8_t ch_up = 17;

// ----------- DABShield I/O assignation -----
//
constexpr uint8_t slaveSelectPin = 13;  // Si4684 SSB/CS
constexpr uint8_t SCKPin = 18;
constexpr uint8_t MISOPin = 19;
constexpr uint8_t MOSIPin = 23;
constexpr uint8_t pwen = 2;
constexpr uint8_t resetPin = 14;
constexpr uint8_t interruptPin = 26;
constexpr uint8_t backlightPin = 15;
constexpr uint8_t amplifierG0Pin = 27;
constexpr uint8_t amplifierG1Pin = 33;

const char* const audiomode[] = {"DUAL", "MONO", "STEREO", "Joint ST"};

unsigned long lastStatus = 0;             // the last time the output pin was toggled
unsigned long statusDelay = 500;          // poll RSSI/SNR/quality twice per second
unsigned long lastDiagnostic = 0;
constexpr unsigned long diagnosticDelay = 5000;

// ------------- Defines MAX FM and DAB channels ---------------------------
//
constexpr byte MAX_DAB_STATIONS = 250;    // max number of DAB channels
constexpr byte MAX_FM_STATIONS  = 250;    // max number of FM channels

// ----- Definition of the memory slots to be reserved in the EEPROM --------
//
constexpr byte ADDR_VOLUME                = 0;  
constexpr byte ADDR_MODE                  = 1;  
constexpr byte ADDR_CURRENT_DAB_CHANNEL   = 2;
constexpr byte ADDR_TOTAL_DAB_CHANNEL     = 3;        
constexpr byte ADDR_CURRENT_FM_CHANNEL    = 4;
constexpr byte ADDR_TOTAL_FM_CHANNEL      = 5; 
// ----------------------------- ---------- free from 6 to 9 ------------------------------------------------ 
constexpr byte ADDR_FM_CHANNEL    = 10 ;                                                                 //FM 1st adress for channel 1 - channel 2 = ADDR_CHANNEL +13 .....
constexpr int  ADDR_DAB_CHANNEL   = MAX_FM_STATIONS * 13 + ADDR_FM_CHANNEL  ;                            //DAB 1st adress for channel 1 - channel 2 = ADDR_CHANNEL +21 .....
constexpr int  EEPROM_SIZE        = ADDR_FM_CHANNEL + MAX_FM_STATIONS * 13 + MAX_DAB_STATIONS * 28; 


// ------------------------------------ Sort Buffer definition ----------------------------------------------
//
uint8_t countSort = 0;                      // will be incremented writing to struct channels

struct foundChannel {
    static constexpr uint8_t NAME_MAX_LEN = 16;
    char name[NAME_MAX_LEN + 1];
    uint8_t  param1;
    uint8_t  param2;  
    uint32_t param3;
    uint32_t param4;
    uint8_t charset;
};
foundChannel channels[MAX_DAB_STATIONS];

struct foundChannelFM {
    static constexpr uint8_t NAME_MAX_LEN = 8;
    char name[NAME_MAX_LEN + 1];
    uint8_t param2;  
    uint8_t param3;
    bool    param4;    
};
foundChannelFM channelsFM[MAX_FM_STATIONS];

// -------------------- DAB initialization ---------------
//
DAB Dab;
Controls controls;
Backlight backlight;

// --------------------- Global variables ----------------
//
uint8_t  vol;
uint8_t  service;
uint32_t serviceid;
uint32_t compid;
uint8_t  ensemble;
uint16_t stationFM;                 // stationFM = 100*stationFM_h + stationFM_l
bool     flag_sel;
char     newFMname[9];
constexpr uint32_t FM_PS_STORE_STABILITY_MS = 20000;
constexpr uint32_t FM_SCAN_PS_STABILITY_MS = 4000;
char fmPsCandidate[9] = {0};
uint32_t fmPsCandidateSinceMs = 0;
bool fmPsCandidateActive = false;
char scanFmPsCandidate[9] = {0};
uint32_t scanFmPsCandidateSinceMs = 0;
bool scanFmPsCandidateActive = false;
bool scanFmPsDynamic = false;
//--------------------- Stored in EEPROM ------------------
byte     totalDABchannels;   
byte     currentDABchannel;  
byte     totalFMchannels;   
byte     currentFMchannel;  
byte     stationFM_l;        
byte     stationFM_h;       
byte     flag_name_FM;
byte     dabMode;
byte     dabCharset;
char     fmName[9];
char     dabName[17];

// ------------------------ Setup -------------------------------
//
enum class ScanState : uint8_t {
  Idle,
  MuteWait,
  DabTuneStart,
  DabTuneWait,
  DabServiceListWait,
  FmTuneStart,
  FmTuneWait,
  FmSeekStart,
  FmSeekWait,
  FmRdsTuneStart,
  FmRdsTuneWait,
  FmRdsWait,
  Commit,
  Summary
};

ScanState scanState = ScanState::Idle;
bool scanCancelRequested = false;
bool scanBandSwitchPending = false;
uint8_t scanDabIndex = 0;
uint8_t scanCommitIndex = 0;
uint8_t scanRdsIndex = 0;
uint32_t scanStateDeadlineMs = 0;
uint16_t scanLastFmFrequency = 0;
uint16_t scanPreviousFmFrequency = 0;
uint32_t scanPreviousDabServiceId = 0;
uint32_t scanPreviousDabComponentId = 0;

enum class UiView : uint8_t {
  Text,
  Slideshow,
  Tech,
  StationList,
  Menu
};

enum UiDirtyRegion : uint8_t {
  UI_DIRTY_NONE = 0,
  UI_DIRTY_HEADER = 1U << 0,
  UI_DIRTY_STATION = 1U << 1,
  UI_DIRTY_TEXT = 1U << 2,
  UI_DIRTY_STATUS = 1U << 3,
  UI_DIRTY_SIGNAL = 1U << 4,
  UI_DIRTY_TECH = 1U << 5,
  UI_DIRTY_FULL = 1U << 7
};

struct UiSettings {
  uint8_t brightness = 100;
  uint8_t dimLevel = 20;
  uint8_t dimTimeoutIndex = 1;  // 15, 30, 60 or 120 seconds.
  uint8_t techEnabled = 1;
  uint8_t defaultView = 0;
  uint8_t slideshowMode = 1;    // 0=off, 1=manual screen, 2=auto.
  uint8_t slideshowLayout = 0;  // 0=status strip, 1=full screen.
  uint8_t fmRegion = static_cast<uint8_t>(FmRegion::Europe);
};

enum class UiAccent : uint8_t {
  None,
  Acute,
  Grave,
  Caron,
  Umlaut,
  Circumflex,
  Ring,
  Tilde,
  Cedilla,
  Macron,
  Breve,
  DotAbove,
  DoubleAcute,
  Ogonek,
  Stroke,
  MiddleDot
};

enum class UiSpecialGlyph : uint8_t {
  None,
  LeftGuillemet,
  RightGuillemet,
  InvertedExclamation,
  InvertedQuestion,
  Pound,
  Euro,
  Copyright,
  AEUpper,
  AELower,
  OEUpper,
  OELower,
  EthUpper,
  EthLower,
  ThornUpper,
  ThornLower,
  SharpS,
  EngUpper,
  EngLower,
  IJUpper,
  IJLower,
  Beta,
  Macron,
  HorizontalBar
};

struct UiGlyph {
  char base;
  UiAccent accent;
  UiSpecialGlyph special;

  UiGlyph(char baseValue, UiAccent accentValue,
          UiSpecialGlyph specialValue = UiSpecialGlyph::None)
      : base(baseValue), accent(accentValue), special(specialValue) {}
};

UiSettings uiSettings;
UiView uiView = UiView::Text;
uint8_t uiDirtyFlags = UI_DIRTY_NONE;
UiView uiViewBeforeModal = UiView::Text;
bool stationPreviewActive = false;
bool openListAfterBandReady = false;
uint16_t stationListSelection = 1;
uint16_t stationListTop = 1;
uint8_t menuSelection = 0;
uint8_t menuTop = 0;
String uiBroadcastText;
uint16_t uiBroadcastTextGlyphs = 0;
String uiBroadcastLoopText;
uint16_t uiBroadcastLoopGlyphs = 0;
uint16_t uiTextScrollGlyph = 0;
uint32_t uiTextPageDeadlineMs = 0;
uint16_t uiStationScrollGlyph = 0;
uint32_t uiStationScrollDeadlineMs = 0;
bool uiStationScrollEndHold = false;
bool uiSlideshowDecodePending = false;
bool uiDefaultSlideshowPending = false;
bool uiBandStarting = false;
bool uiBandTunePending = false;
byte fmDatabaseRegion = 0xFF;

void clearUiBroadcastState() {
  uiBroadcastText = "";
  uiBroadcastTextGlyphs = 0;
  uiBroadcastLoopText = "";
  uiBroadcastLoopGlyphs = 0;
  uiTextScrollGlyph = 0;
  uiTextPageDeadlineMs = 0;
}

void applyActiveFmRegion() {
  uiSettings.fmRegion = sanitizeFmRegion(uiSettings.fmRegion);
  const FmRegionProfile& profile = fmRegionProfile(uiSettings.fmRegion);
  Dab.configureFmBand(profile.minFrequency10kHz, profile.maxFrequency10kHz,
                      profile.seekSpacing10kHz, profile.deEmphasis);
}

void showNoStationsForCurrentBand() {
  uiBandStarting = false;
  uiBandTunePending = false;
  uiDirtyFlags = UI_DIRTY_NONE;
  clearScreen();
  TFT_aff("No stations", 40);
  tft.setTextColor(ST77XX_YELLOW);
  Message("Hold SCAN to scan", 66);
}

void finishBandTransition(bool tuneSucceeded) {
  uiBandStarting = false;
  uiBandTunePending = false;
  uiDirtyFlags = UI_DIRTY_NONE;
  uiView = uiSettings.defaultView == 1 && uiSettings.techEnabled
               ? UiView::Tech : UiView::Text;
  uiDefaultSlideshowPending = dabMode == 1 &&
                              uiSettings.defaultView == 2 &&
                              uiSettings.slideshowMode != 0;
  renderCurrentUiView();
  if (!tuneSucceeded) {
    Serial.println("[APP][WARN] initial station tune failed; UI released to no-signal state");
  }
}

bool startBandTransition() {
  clearUiBroadcastState();
  resetUiStationScroll();
  uiSlideshowDecodePending = false;
  uiDefaultSlideshowPending = false;
  Dab.discardSlideshow();
  uiDirtyFlags = UI_DIRTY_NONE;
  uiBandStarting = true;
  uiBandTunePending = false;
  stationPreviewActive = false;
  uiView = uiSettings.defaultView == 1 && uiSettings.techEnabled
               ? UiView::Tech : UiView::Text;
  applyActiveFmRegion();
  clearScreen();
  TFT_aff(dabMode == 1 ? "Starting DAB" : "Starting FM", 40);
  if (!Dab.beginAsync(dabMode == 1 ? 0 : 1)) {
    uiBandStarting = false;
    clearScreen();
    Message_red("Radio start failed", 55);
    return false;
  }
  return true;
}

void applyFmRegionSelection() {
  applyActiveFmRegion();
  refreshFmDatabaseForRegion();
  if (dabMode == 0 && Dab.ready()) {
    startBandTransition();
  } else {
    renderCurrentUiView();
  }
}

void setup() {
  backlight.begin(backlightPin, millis());
  Dab.configureAudioPins(amplifierG0Pin, amplifierG1Pin);

  Serial.begin(115200);
  const bool slideshowRendererReady = initializeSlideshowRenderer(&Serial);
  // Allocate the long-lived broadcast strings once at startup. The capacity
  // covers the worst-case UTF-8 expansion of one ServiceData payload and
  // prevents repeated heap growth as DLS/Radiotext changes.
  constexpr unsigned int broadcastTextCapacity =
      DAB_MAX_SERVICEDATA_LEN * 3U + 4U;
  uiBroadcastText.reserve(broadcastTextCapacity);
  uiBroadcastLoopText.reserve(broadcastTextCapacity);
  const esp_reset_reason_t resetReason = esp_reset_reason();
  const char* resetReasonLabel = "unknown";
  switch (resetReason) {
    case ESP_RST_POWERON: resetReasonLabel = "power-on"; break;
    case ESP_RST_EXT: resetReasonLabel = "external"; break;
    case ESP_RST_SW: resetReasonLabel = "software"; break;
    case ESP_RST_PANIC: resetReasonLabel = "panic"; break;
    case ESP_RST_INT_WDT: resetReasonLabel = "interrupt-watchdog"; break;
    case ESP_RST_TASK_WDT: resetReasonLabel = "task-watchdog"; break;
    case ESP_RST_WDT: resetReasonLabel = "watchdog"; break;
    case ESP_RST_DEEPSLEEP: resetReasonLabel = "deep-sleep"; break;
    case ESP_RST_BROWNOUT: resetReasonLabel = "brownout"; break;
    case ESP_RST_SDIO: resetReasonLabel = "sdio"; break;
    default: break;
  }
  Serial.printf("[APP] reset reason=%d (%s)\n",
                static_cast<int>(resetReason), resetReasonLabel);
  SPI.begin(SCKPin, MISOPin, MOSIPin);
  controls.begin(vol_up, vol_down, scan_sw, mode_sw, sel, ch_down, ch_up);
  if (digitalRead(scan_sw) == LOW || digitalRead(mode_sw) == LOW) {
    Serial.printf("[KEY][WARN] SCAN=%u BAND=%u at startup; GPIO34/35 require external pull-ups to 3.3 V\n",
                  digitalRead(scan_sw), digitalRead(mode_sw));
  }

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(3);
  tft.setTextWrap(false);
  clearScreen();
  TFT_aff("Hello DAB+ !", 25);
  tft.setTextColor(ST77XX_WHITE);
  Message("(c)2026* Y.Bourdon", 60);
  Message("Si468x responsive core", 75);

  Serial.println("[APP] FMDABRadio responsive startup");
  Serial.println("EEPROM FM first byte     : " + String(ADDR_FM_CHANNEL));
  Serial.println("EEPROM FM last byte      : " + String(MAX_FM_STATIONS * 13 + ADDR_FM_CHANNEL - 1));
  Serial.println("EEPROM DAB first byte    : " + String(ADDR_DAB_CHANNEL));
  Serial.println("EEPROM DAB last byte     : " + String(MAX_DAB_STATIONS * 28 + ADDR_DAB_CHANNEL - 1));
  Serial.println("Minimum EEPROM size      : " + String(EEPROM_SIZE));

  tft.setTextColor(ST77XX_GREEN);
  startEEPROM();
  Message("I2C EEPROM connected", 97);

  if (!digitalRead(sel)) {
    cleanEEPROM();
    totalFMchannels = 0;
    totalDABchannels = 0;
    while (!digitalRead(sel)) {
      delay(1);
    }
  }

  lastEEPROM();
  loadUiSettings();
  loadFmDatabaseRegion();
  applyActiveFmRegion();
  static const uint16_t dimTimeoutSeconds[] = {15, 30, 60, 120};
  backlight.configure(uiSettings.brightness, uiSettings.dimLevel,
                      static_cast<uint32_t>(
                          dimTimeoutSeconds[uiSettings.dimTimeoutIndex]) * 1000UL);
  uiView = uiSettings.defaultView == 1 && uiSettings.techEnabled
               ? UiView::Tech
               : UiView::Text;
  uiDefaultSlideshowPending = dabMode == 1 &&
                              uiSettings.defaultView == 2 &&
                              uiSettings.slideshowMode != 0;
  if (vol > 75) {
    vol = 57;
    saveVolumeToEEPROM(vol);
  }
  if (dabMode > 1) {
    dabMode = 0;
    saveModeToEEPROM(dabMode);
  }
  Serial.printf("[APP] restored mode=%s DAB=%u/%u FM=%u/%u volume=%u\n",
                dabMode == 1 ? "DAB" : "FM", currentDABchannel,
                totalDABchannels, currentFMchannel, totalFMchannels, vol);

  Dab.configurePins(slaveSelectPin, interruptPin, resetPin, pwen);
  Dab.setDiagnostics(&Serial);
  Dab.setCallback(ServiceData);
  if (!slideshowRendererReady ||
      !Dab.setSlideshowEnabled(uiSettings.slideshowMode != 0)) {
    uiSettings.slideshowMode = 0;
    Serial.println("[SLS][WARN] slideshow disabled because RAM allocation failed");
  }

  startBandTransition();
}
void loop() {
  Dab.task();

  const uint32_t now = millis();
  backlight.service(now);
  commitDirtySettingsIfDue();
  controls.poll(now);
  ButtonEvent event;
  while (controls.pop(event)) {
    handleButtonEvent(event);
  }

  handleRadioEvents();
  serviceScan(now);
  serviceUi(now);

  if (!scanActive() && Dab.ready() && (now - lastStatus) > statusDelay) {
    if (dabMode == 1) {
      DAB_status();
    } else {
      FM_status();
    }
    lastStatus = now;
    if (now - lastDiagnostic >= diagnosticDelay) {
      Serial.printf("[DIAG] state=%s IRQ=%lu cmdErrors=%lu dsrvOverflow=%lu droppedKeys=%lu\n",
                    Dab.stateName(), static_cast<unsigned long>(Dab.irqCount()),
                    static_cast<unsigned long>(Dab.commandErrorCount()),
                    static_cast<unsigned long>(Dab.dsrvOverflowCount()),
                    static_cast<unsigned long>(controls.droppedEvents()));
      lastDiagnostic = now;
    }
  }

  if (!scanActive()) processFmNameUpdate();
}

void handleButtonEvent(const ButtonEvent& event) {
  const uint32_t now = millis();
  backlight.noteActivity(now);
  if (uiBandStarting) return;

  const bool stepEvent = event.type == ButtonEventType::ShortPress ||
                         event.type == ButtonEventType::Repeat;

  if (handleUiButtonEvent(event)) return;

  if (event.button == ButtonId::VolumeUp && stepEvent) {
    if (vol < 75) {
      ++vol;
      Volume();
      if (!scanActive()) Dab.requestVolume(vol);
      saveVolumeToEEPROM(vol);
      Serial.printf("[KEY] volume=%u/75\n", vol);
    }
    return;
  }

  if (event.button == ButtonId::VolumeDown && stepEvent) {
    if (vol > 0) {
      --vol;
      Volume();
      if (!scanActive()) Dab.requestVolume(vol);
      saveVolumeToEEPROM(vol);
      Serial.printf("[KEY] volume=%u/75\n", vol);
    }
    return;
  }

  if (event.button == ButtonId::ChannelUp && stepEvent) {
    if (scanActive()) return;
    previewStation(1);
    return;
  }

  if (event.button == ButtonId::ChannelDown && stepEvent) {
    if (scanActive()) return;
    previewStation(-1);
    return;
  }

  if (event.button == ButtonId::Band &&
      event.type == ButtonEventType::ShortPress) {
    if (scanActive()) {
      cancelFullScan(true);
      return;
    }
    if (!Dab.ready()) {
      Serial.println("[KEY] band switch ignored while radio is busy");
      return;
    }
    dabMode = !dabMode;
    saveModeToEEPROM(dabMode);
    startBandTransition();
    return;
  }

  if (event.button == ButtonId::Scan) {
    if (scanActive()) {
      if (event.type == ButtonEventType::ShortPress) cancelFullScan(false);
      return;
    }
    if (event.type == ButtonEventType::LongPress) {
      Serial.println("[KEY] full scan threshold reached; starting now");
      beginFullScan();
    } else if (event.type == ButtonEventType::ShortPress) {
      openStationList();
    }
    return;
  }

  if (event.button == ButtonId::Select) {
    if (scanActive()) return;
    if (event.type == ButtonEventType::LongPress) {
      openSettingsMenu();
      return;
    }
    if (event.type != ButtonEventType::ShortPress || !Dab.ready()) return;
    if (!stationPreviewActive) {
      cycleUiScreen();
    } else if (dabMode == 1 && totalDABchannels != 0) {
      stationPreviewActive = false;
      saveCurrentDABChannelToEEPROM(currentDABchannel);
      DAB_SetChannel();
    } else if (dabMode == 0 && totalFMchannels != 0) {
      stationPreviewActive = false;
      flag_sel = true;
      saveCurrentFMchannelToEEPROM(currentFMchannel);
      FMsetChannel(currentFMchannel, true);
    }
  }
}

void previewStation(int8_t direction) {
  if (dabMode == 1) {
    if (totalDABchannels == 0) return;
    const byte previousChannel = currentDABchannel;
    if (direction > 0 && currentDABchannel < totalDABchannels) {
      ++currentDABchannel;
    } else if (direction < 0 && currentDABchannel > 1) {
      --currentDABchannel;
    } else {
      return;
    }
    stationPreviewActive = true;
    uiView = UiView::Text;
    if (!DABreadEEPROM(currentDABchannel)) {
      currentDABchannel = previousChannel;
      return;
    }
    renderListeningScreen();
  } else {
    if (totalFMchannels == 0) return;
    const byte previousChannel = currentFMchannel;
    if (direction > 0 && currentFMchannel < totalFMchannels) {
      ++currentFMchannel;
    } else if (direction < 0 && currentFMchannel > 1) {
      --currentFMchannel;
    } else {
      return;
    }
    stationPreviewActive = true;
    uiView = UiView::Text;
    if (!FMreadEEPROM(currentFMchannel)) {
      currentFMchannel = previousChannel;
      return;
    }
    renderListeningScreen();
    flag_sel = false;
  }
}

void handleRadioEvents() {
  if (Dab.takeBandReady()) {
    Serial.printf("[APP] %s image ready\n", dabMode == 1 ? "DAB" : "FM");

    if (openListAfterBandReady) {
      openListAfterBandReady = false;
      uiBandStarting = false;
      uiBandTunePending = false;
      uiDirtyFlags = UI_DIRTY_NONE;
      openStationList();
    } else if (dabMode == 1) {
      if (totalDABchannels != 0) {
        uiBandTunePending = DAB_SetChannel();
        if (!uiBandTunePending) finishBandTransition(false);
      } else {
        showNoStationsForCurrentBand();
      }
    } else {
      if (totalFMchannels != 0) {
        uiBandTunePending = FMsetChannel(currentFMchannel, true);
        if (!uiBandTunePending) finishBandTransition(false);
      } else {
        showNoStationsForCurrentBand();
      }
    }
  }

  RadioOperation operation;
  bool success;
  while (Dab.takeOperationResult(operation, success)) {
    Serial.printf("[APP] radio operation=%u result=%s\n",
                  static_cast<unsigned>(operation), success ? "ok" : "failed");
    if (handleScanRadioResult(operation, success)) continue;

    if (uiBandStarting) {
      if (operation == RadioOperation::BandBoot && !success) {
        uiBandStarting = false;
        uiBandTunePending = false;
        clearScreen();
        Message_red("Radio start failed", 55);
        continue;
      }
      const bool initialTuneResult =
          uiBandTunePending &&
          ((dabMode == 1 && operation == RadioOperation::DabService) ||
           (dabMode == 0 && operation == RadioOperation::FmTune));
      if (initialTuneResult) {
        // A completed tune may legitimately have Dab.valid=false. The UI
        // transition ends on operation completion, not on RF validity.
        finishBandTransition(success);
        continue;
      }
    }

    if (!success && operation == RadioOperation::BandBoot) {
      clearScreen();
      Message_red("Radio start failed", 55);
    } else if (!success) {
      Serial.println("[APP][WARN] recoverable radio operation failed; display kept intact");
    }
  }
}

void normalizeFmPs(const char* source, char destination[9]) {
  uint8_t first = 0;
  while (first < 8 && source[first] == ' ') ++first;
  uint8_t last = 8;
  while (last > first && (source[last - 1] == ' ' || source[last - 1] == 0)) {
    --last;
  }
  memset(destination, ' ', 8);
  if (last > first) memcpy(destination, source + first, last - first);
  destination[8] = 0;
}

bool fmPsIsBlank(const char value[9]) {
  for (uint8_t index = 0; index < 8; ++index) {
    if (value[index] != 0 && value[index] != ' ') return false;
  }
  return true;
}

void resetFmPsCandidate() {
  memset(fmPsCandidate, 0, sizeof(fmPsCandidate));
  fmPsCandidateSinceMs = 0;
  fmPsCandidateActive = false;
}

void resetScanFmPsCandidate() {
  memset(scanFmPsCandidate, 0, sizeof(scanFmPsCandidate));
  scanFmPsCandidateSinceMs = 0;
  scanFmPsCandidateActive = false;
  scanFmPsDynamic = false;
}

void processFmNameUpdate() {
  if (dabMode != 0 || !flag_sel || strlen(Dab.ps) == 0) return;

  normalizeFmPs(Dab.ps, newFMname);
  if (fmPsIsBlank(newFMname)) return;
  const uint32_t now = millis();
  if (!fmPsCandidateActive) {
    memcpy(fmPsCandidate, newFMname, sizeof(fmPsCandidate));
    fmPsCandidateSinceMs = now;
    fmPsCandidateActive = true;
    Serial.printf("[RDS] PS candidate for FM channel %u: %.8s; observing for %lus\n",
                  currentFMchannel, fmPsCandidate,
                  static_cast<unsigned long>(FM_PS_STORE_STABILITY_MS / 1000U));
    return;
  }
  if (memcmp(fmPsCandidate, newFMname, 8) != 0) {
    Serial.printf("[RDS][WARN] dynamic PS detected on FM channel %u: %.8s -> %.8s; EEPROM name protected\n",
                  currentFMchannel, fmPsCandidate, newFMname);
    flag_sel = false;
    resetFmPsCandidate();
    return;
  }
  if (static_cast<uint32_t>(now - fmPsCandidateSinceMs) <
      FM_PS_STORE_STABILITY_MS) {
    return;
  }

  flag_name_FM = 1;
  saveFMnameToEEPROM(currentFMchannel, true, fmPsCandidate);
  memcpy(fmName, fmPsCandidate, sizeof(fmName));
  Serial.printf("[RDS] stable PS stored for previously unnamed FM channel %u: %.8s\n",
                currentFMchannel, fmName);
  if (uiView == UiView::Text) {
    resetUiStationScroll();
    markUiDirty(UI_DIRTY_STATION);
  } else if (uiView == UiView::StationList) {
    renderStationList();
  }
  flag_sel = false;
  resetFmPsCandidate();
}


bool scanActive() {
  return scanState != ScanState::Idle;
}

void drawScanProgress(uint8_t percent) {
  if (percent > 100) percent = 100;
  constexpr uint8_t x = 8;
  constexpr uint8_t y = 116;
  constexpr uint8_t width = 144;
  tft.drawRect(x, y, width, 7, ST77XX_WHITE);
  const uint8_t filled = static_cast<uint8_t>((width - 2) * percent / 100);
  tft.fillRect(x + 1, y + 1, width - 2, 5, ST77XX_BLACK);
  if (filled != 0) tft.fillRect(x + 1, y + 1, filled, 5, ST77XX_GREEN);
}

void renderDabScanFrequency() {
  char text[30];
  const uint32_t frequency = Dab.freq_khz(scanDabIndex);
  snprintf(text, sizeof(text), "DAB %u/%u  %lu.%03luMHz",
           scanDabIndex + 1, DAB_FREQS,
           static_cast<unsigned long>(frequency / 1000),
           static_cast<unsigned long>(frequency % 1000));
  tft.setTextColor(ST77XX_GREEN);
  Aff_Scan_Freq(text, 50);
  drawScanProgress(static_cast<uint8_t>(scanDabIndex * 100U /
                                        (DAB_FREQS - 1)));
}

void renderFmScanFrequency(uint16_t frequency) {
  char text[24];
  snprintf(text, sizeof(text), "FM %u.%1uMHz", frequency / 100,
           (frequency % 100) / 10);
  tft.setTextColor(ST77XX_GREEN);
  Aff_Scan_Freq(text, 55);

  const FmRegionProfile& profile = fmRegionProfile(uiSettings.fmRegion);
  uint8_t progress = 0;
  if (frequency > profile.minFrequency10kHz &&
      profile.maxFrequency10kHz > profile.minFrequency10kHz) {
    progress = static_cast<uint8_t>(
        (static_cast<uint32_t>(frequency - profile.minFrequency10kHz) * 100U) /
        (profile.maxFrequency10kHz - profile.minFrequency10kHz));
  }
  drawScanProgress(progress);
}

void beginFullScan() {
  if (scanActive() || !Dab.ready()) {
    Serial.println("[SCAN][WARN] scan start rejected while radio is busy");
    return;
  }

  scanCancelRequested = false;
  scanBandSwitchPending = false;
  scanDabIndex = 0;
  scanCommitIndex = 0;
  scanRdsIndex = 0;
  scanLastFmFrequency = 0;
  scanPreviousFmFrequency = 0;
  scanPreviousDabServiceId = 0;
  scanPreviousDabComponentId = 0;
  countSort = 0;

  if (dabMode == 1 && totalDABchannels != 0 &&
      currentDABchannel >= 1 && currentDABchannel <= totalDABchannels) {
    const byte previousChannel = currentDABchannel;
    if (DABreadEEPROM(previousChannel)) {
      scanPreviousDabServiceId = serviceid;
      scanPreviousDabComponentId = compid;
    }
    currentDABchannel = previousChannel;
  } else if (dabMode == 0 && totalFMchannels != 0 &&
             currentFMchannel >= 1 && currentFMchannel <= totalFMchannels) {
    const byte previousChannel = currentFMchannel;
    const bool previousRead = FMreadEEPROM(previousChannel);
    currentFMchannel = previousChannel;
    if (previousRead) {
      scanPreviousFmFrequency = 100U * stationFM_h + 10U * stationFM_l;
    }
  }

  clearScreen();
  TFT_aff(dabMode == 1 ? "DAB scanning" : "FM scanning", 12);
  tft.setTextColor(ST77XX_YELLOW);
  Message("Short SCAN cancels", 32);
  Aff_Scan_Service(0, 85);
  drawScanProgress(0);
  Dab.requestVolume(0);
  scanState = ScanState::MuteWait;
  scanStateDeadlineMs = millis() + 1;
  Serial.printf("[SCAN] %s full scan started; old database retained until commit\n",
                dabMode == 1 ? "DAB" : "FM");
}

void cancelFullScan(bool switchBand) {
  if (!scanActive()) return;
  scanBandSwitchPending |= switchBand;
  if (scanState == ScanState::Commit) {
    Serial.println("[SCAN] acquisition complete; band switch queued until database commit");
    return;
  }
  if (scanState == ScanState::Summary) {
    scanStateDeadlineMs = millis();
    return;
  }
  scanCancelRequested = true;
  tft.setTextColor(ST77XX_YELLOW);
  Message("Cancelling...", 100);
  Serial.println("[SCAN] cancellation requested");
}

void normalizeDabScanLabel(const char* source, char destination[17]) {
  uint8_t first = 0;
  while (first < 16 && source[first] == ' ') ++first;
  uint8_t last = 16;
  while (last > first && (source[last - 1] == ' ' || source[last - 1] == 0)) {
    --last;
  }
  memset(destination, ' ', 16);
  const uint8_t length = last - first;
  if (length != 0) {
    memcpy(destination, source + first, length);
  } else {
    memcpy(destination, "unknown?", 8);
  }
  destination[16] = 0;
}

void collectCurrentDabServices() {
  tft.setTextColor(ST77XX_GREEN);
  Aff_Scan_Freq(Dab.Ensemble, 65);
  for (uint8_t i = 0;
       i < Dab.numberofservices && countSort < MAX_DAB_STATIONS; ++i) {
    if (Dab.service[i].Type == SERVICE_DATA) continue;
    char name[17];
    normalizeDabScanLabel(Dab.service[i].Label, name);
    addStation(name, scanDabIndex, i, Dab.service[i].ServiceID,
               Dab.service[i].CompID, Dab.service[i].Charset);
    tft.setTextColor(ST77XX_WHITE);
    Aff_Scan_Name(decodeBroadcastText(
                      reinterpret_cast<const uint8_t*>(name), 16,
                      Dab.service[i].Charset),
                  100);
    Aff_Scan_Service(countSort, 85);
    const String decodedName = decodeBroadcastText(
        reinterpret_cast<const uint8_t*>(name), 16,
        Dab.service[i].Charset);
    Serial.printf("[SCAN][DAB] %u: freqIndex=%u SID=0x%08lX CID=0x%08lX charset=%u name=%s\n",
                  countSort, scanDabIndex,
                  static_cast<unsigned long>(Dab.service[i].ServiceID),
                  static_cast<unsigned long>(Dab.service[i].CompID),
                  Dab.service[i].Charset, decodedName.c_str());
  }
}

void startScanCommit() {
  scanCommitIndex = 0;
  scanState = ScanState::Commit;
  clearScreen();
  TFT_aff("Saving stations", 35);
  Aff_Scan_Service(countSort, 58);
  drawScanProgress(0);
  Serial.printf("[SCAN] acquisition complete; committing %u records\n", countSort);
}

void beginFmRdsPhase() {
  scanRdsIndex = 0;
  scanState = ScanState::FmRdsTuneStart;
  clearScreen();
  TFT_aff("Reading RDS", 12);
  tft.setTextColor(ST77XX_YELLOW);
  Message("Short SCAN cancels", 32);
  drawScanProgress(0);
  Serial.printf("[SCAN][RDS] starting PS detection for %u stations\n", countSort);
}

void advanceFmRdsStation() {
  ++scanRdsIndex;
  if (scanRdsIndex >= countSort) {
    startScanCommit();
  } else {
    scanState = ScanState::FmRdsTuneStart;
  }
}

void captureCurrentFmRdsName(const char source[9]) {
  foundChannelFM* station = &channelsFM[scanRdsIndex];
  memcpy(station->name, source, sizeof(station->name));
  station->param4 = station->name[0] != 0 && station->name[0] != ' ';
  if (!station->param4) memcpy(station->name, "unknown?", 9);
  tft.setTextColor(ST77XX_WHITE);
  Aff_Scan_Name(decodeRdsText(
                    reinterpret_cast<const uint8_t*>(station->name), 8),
                85);
  Serial.printf("[SCAN][RDS] %u/%u name=%s\n", scanRdsIndex + 1,
                countSort, station->name);
}

void completeScanAcquisition() {
  if (countSort == 0) {
    clearScreen();
    TFT_aff("No stations found", 42);
    tft.setTextColor(ST77XX_YELLOW);
    Message("Old list preserved", 62);
    scanState = ScanState::Summary;
    scanStateDeadlineMs = millis() + 1400;
    Serial.println("[SCAN] no stations found; old database preserved");
    return;
  }

  if (dabMode == 0) {
    beginFmRdsPhase();
  } else {
    startScanCommit();
  }
}

void showCancelledScan() {
  clearScreen();
  TFT_aff("Scan cancelled", 42);
  tft.setTextColor(ST77XX_YELLOW);
  Message("Old list preserved", 62);
  scanState = ScanState::Summary;
  scanStateDeadlineMs = millis() + (scanBandSwitchPending ? 0 : 1000);
  Serial.println("[SCAN] cancelled; old database preserved");
}

void advanceDabScan() {
  ++scanDabIndex;
  if (scanDabIndex >= DAB_FREQS || countSort >= MAX_DAB_STATIONS) {
    completeScanAcquisition();
  } else {
    scanState = ScanState::DabTuneStart;
  }
}

void captureCurrentFmStation() {
  const uint16_t frequency = Dab.freq;
  if (frequency <= scanLastFmFrequency || countSort >= MAX_FM_STATIONS) return;
  char name[9] = "unknown?";
  addStationFM(name, (frequency % 100) / 10, frequency / 100, false);
  scanLastFmFrequency = frequency;
  Aff_Scan_Service(countSort, 85);
  renderFmScanFrequency(frequency);
  Serial.printf("[SCAN][FM] %u: %u.%02u MHz RSSI=%d SNR=%d\n", countSort,
                frequency / 100, frequency % 100, Dab.signalstrength, Dab.snr);
}

bool handleScanRadioResult(RadioOperation operation, bool success) {
  if (!scanActive()) return false;

  if (operation == RadioOperation::DabTune &&
      scanState == ScanState::DabTuneWait) {
    if (scanCancelRequested) {
      showCancelledScan();
    } else if (success && Dab.valid) {
      if (Dab.numberofservices != 0) {
        collectCurrentDabServices();
        advanceDabScan();
      } else {
        scanState = ScanState::DabServiceListWait;
        scanStateDeadlineMs = millis() + 4500;
      }
    } else {
      advanceDabScan();
    }
    return true;
  }

  if (operation == RadioOperation::FmTune &&
      scanState == ScanState::FmTuneWait) {
    if (scanCancelRequested) {
      showCancelledScan();
    } else if (success) {
      const FmRegionProfile& profile = fmRegionProfile(uiSettings.fmRegion);
      scanLastFmFrequency = 0;
      if (Dab.valid) {
        captureCurrentFmStation();
      } else {
        scanLastFmFrequency = Dab.freq;
      }
      if (Dab.freq >= profile.maxFrequency10kHz ||
          countSort >= MAX_FM_STATIONS) {
        completeScanAcquisition();
      } else {
        scanState = ScanState::FmSeekStart;
      }
    } else {
      completeScanAcquisition();
    }
    return true;
  }

  if (operation == RadioOperation::FmTune &&
      scanState == ScanState::FmRdsTuneWait) {
    if (scanCancelRequested) {
      showCancelledScan();
    } else if (success && Dab.valid) {
      scanState = ScanState::FmRdsWait;
      scanStateDeadlineMs = millis() + 8000;
    } else {
      Serial.printf("[SCAN][RDS] %u/%u tune failed; keeping unknown name\n",
                    scanRdsIndex + 1, countSort);
      advanceFmRdsStation();
    }
    return true;
  }

  if (operation == RadioOperation::FmSeek &&
      scanState == ScanState::FmSeekWait) {
    if (scanCancelRequested) {
      showCancelledScan();
    } else if (success && Dab.valid && Dab.freq > scanLastFmFrequency) {
      const FmRegionProfile& profile = fmRegionProfile(uiSettings.fmRegion);
      if (Dab.freq <= profile.maxFrequency10kHz) {
        captureCurrentFmStation();
      }
      if (Dab.freq >= profile.maxFrequency10kHz ||
          countSort >= MAX_FM_STATIONS) {
        completeScanAcquisition();
      } else {
        scanState = ScanState::FmSeekStart;
      }
    } else {
      completeScanAcquisition();
    }
    return true;
  }
  return false;
}

void commitNextScanRecord() {
  if (scanCommitIndex < countSort) {
    if (dabMode == 1) {
      foundChannel* station = &channels[scanCommitIndex];
      saveDABchannelToEEPROM(scanCommitIndex + 1, station->param1,
                              station->param2, station->param3,
                              station->param4, station->name,
                              station->charset);
    } else {
      foundChannelFM* station = &channelsFM[scanCommitIndex];
      saveFMchannelToEEPROM(scanCommitIndex + 1, station->param2,
                            station->param3, station->param4, station->name);
    }
    ++scanCommitIndex;
    drawScanProgress(static_cast<uint8_t>(
        static_cast<uint16_t>(scanCommitIndex) * 100U / countSort));
    return;
  }

  if (dabMode == 1) {
    totalDABchannels = countSort;
    currentDABchannel = 1;
    for (uint8_t i = 0; i < countSort; ++i) {
      if (channels[i].param3 == scanPreviousDabServiceId &&
          channels[i].param4 == scanPreviousDabComponentId) {
        currentDABchannel = i + 1;
        break;
      }
    }
    saveTotalDABchannelToEEPROM(totalDABchannels);
    saveCurrentDABChannelToEEPROM(currentDABchannel);
  } else {
    totalFMchannels = countSort;
    currentFMchannel = 1;
    for (uint8_t i = 0; i < countSort; ++i) {
      const uint16_t frequency =
          100U * channelsFM[i].param3 + 10U * channelsFM[i].param2;
      if (frequency == scanPreviousFmFrequency) {
        currentFMchannel = i + 1;
        break;
      }
    }
    saveTotalFMchannelToEEPROM(totalFMchannels);
    saveCurrentFMchannelToEEPROM(currentFMchannel);
    saveFmDatabaseRegion(uiSettings.fmRegion);
  }

  clearScreen();
  TFT_aff("Scan complete", 42);
  Aff_Scan_Service(countSort, 62);
  scanState = ScanState::Summary;
  scanStateDeadlineMs = millis() + (scanBandSwitchPending ? 0 : 1400);
  Serial.printf("[SCAN] database commit complete: %u stations\n", countSort);
}

void finishScanSession() {
  const bool switchBand = scanBandSwitchPending;
  scanState = ScanState::Idle;
  scanCancelRequested = false;
  scanBandSwitchPending = false;
  Dab.requestVolume(vol);

  if (switchBand) {
    dabMode = !dabMode;
    saveModeToEEPROM(dabMode);
    startBandTransition();
    return;
  }

  clearScreen();
  if (dabMode == 1) {
    if (totalDABchannels != 0) {
      DAB_SetChannel();
    } else {
      TFT_aff("Please Scan!", 50);
    }
  } else if (totalFMchannels != 0) {
    FMsetChannel(currentFMchannel, true);
  } else {
    TFT_aff("Please Scan!", 50);
  }
}

void serviceScan(uint32_t now) {
  if (!scanActive()) return;

  if (scanCancelRequested && Dab.ready() &&
      scanState != ScanState::Commit && scanState != ScanState::Summary) {
    showCancelledScan();
    return;
  }

  switch (scanState) {
    case ScanState::MuteWait:
      if (static_cast<int32_t>(now - scanStateDeadlineMs) >= 0 && Dab.ready()) {
        scanState = dabMode == 1 ? ScanState::DabTuneStart
                                 : ScanState::FmTuneStart;
      }
      break;

    case ScanState::DabTuneStart:
      renderDabScanFrequency();
      if (Dab.requestDabTune(scanDabIndex)) {
        scanState = ScanState::DabTuneWait;
      }
      break;

    case ScanState::DabServiceListWait:
      if (Dab.numberofservices != 0) {
        collectCurrentDabServices();
        advanceDabScan();
      } else if (static_cast<int32_t>(now - scanStateDeadlineMs) >= 0) {
        Serial.printf("[SCAN][DAB] service-list timeout at index=%u\n",
                      scanDabIndex);
        advanceDabScan();
      }
      break;

    case ScanState::FmTuneStart: {
      const FmRegionProfile& profile = fmRegionProfile(uiSettings.fmRegion);
      renderFmScanFrequency(profile.minFrequency10kHz);
      if (Dab.requestFmTune(profile.minFrequency10kHz)) {
        scanState = ScanState::FmTuneWait;
      }
      break;
    }

    case ScanState::FmSeekStart:
      if (Dab.requestFmSeek(true, false)) scanState = ScanState::FmSeekWait;
      break;

    case ScanState::FmRdsTuneStart: {
      foundChannelFM* station = &channelsFM[scanRdsIndex];
      const uint16_t frequency =
          100U * station->param3 + 10U * station->param2;
      char statusText[24];
      renderFmScanFrequency(frequency);
      snprintf(statusText, sizeof(statusText), "RDS %u/%u",
               scanRdsIndex + 1, countSort);
      tft.setTextColor(ST77XX_YELLOW);
      Aff_Scan_Freq(statusText, 70);
      Aff_Scan_Name("Waiting for RDS...", 85);
      resetScanFmPsCandidate();
      drawScanProgress(static_cast<uint8_t>(
          static_cast<uint16_t>(scanRdsIndex) * 100U / countSort));
      if (Dab.requestFmTune(frequency)) {
        scanState = ScanState::FmRdsTuneWait;
      }
      break;
    }

    case ScanState::FmRdsWait:
      if (strlen(Dab.ps) != 0) {
        char candidate[9];
        normalizeFmPs(Dab.ps, candidate);
        if (!fmPsIsBlank(candidate)) {
          if (!scanFmPsCandidateActive) {
            memcpy(scanFmPsCandidate, candidate, sizeof(scanFmPsCandidate));
            scanFmPsCandidateSinceMs = now;
            scanFmPsCandidateActive = true;
            Aff_Scan_Name(decodeRdsText(
                              reinterpret_cast<const uint8_t*>(candidate), 8),
                          85);
          } else if (!scanFmPsDynamic &&
                     memcmp(scanFmPsCandidate, candidate, 8) != 0) {
            scanFmPsDynamic = true;
            Serial.printf("[SCAN][RDS][WARN] dynamic PS %u/%u: %.8s -> %.8s; name not stored\n",
                          scanRdsIndex + 1, countSort,
                          scanFmPsCandidate, candidate);
            Aff_Scan_Name("Dynamic PS ignored", 85);
          }
        }
      }
      if (scanFmPsCandidateActive && !scanFmPsDynamic &&
          static_cast<uint32_t>(now - scanFmPsCandidateSinceMs) >=
              FM_SCAN_PS_STABILITY_MS) {
        captureCurrentFmRdsName(scanFmPsCandidate);
        advanceFmRdsStation();
      } else if (static_cast<int32_t>(now - scanStateDeadlineMs) >= 0) {
        Aff_Scan_Name(scanFmPsDynamic ? "Dynamic PS ignored" : "unknown?", 85);
        Serial.printf("[SCAN][RDS] %u/%u %s; keeping unknown name\n",
                      scanRdsIndex + 1, countSort,
                      scanFmPsDynamic ? "dynamic PS" : "timeout");
        advanceFmRdsStation();
      }
      break;

    case ScanState::Commit:
      commitNextScanRecord();
      break;

    case ScanState::Summary:
      if (static_cast<int32_t>(now - scanStateDeadlineMs) >= 0) {
        finishScanSession();
      }
      break;

    default:
      break;
  }
}

bool DAB_SetChannel(void)
{
  if (!DABreadEEPROM(currentDABchannel)) return false;
  clearUiBroadcastState();
  if (!uiBandStarting) {
    if (uiView == UiView::Tech && uiSettings.techEnabled) renderTechScreen();
    else renderListeningScreen();
  }
  Dab.requestVolume(vol);
  const bool accepted = Dab.requestDabService(ensemble, serviceid, compid);
  if (!accepted) {
    Serial.println("[APP][WARN] DAB tune request rejected while radio is busy");
  }
  const String decodedName = decodeBroadcastText(
      reinterpret_cast<const uint8_t*>(dabName), 16, dabCharset);
  Serial.printf("DAB channel: %d  DAB name: %s  charset=%u (%s)\n",
                currentDABchannel, decodedName.c_str(), dabCharset,
                broadcastCharsetName(dabCharset));
  return accepted;
}

bool FMsetChannel(uint8_t FMchannel, bool flag)
{
  if (!FMreadEEPROM(FMchannel)) return false;
  resetFmPsCandidate();
  // A name accepted during a scan or a previous protected observation is
  // immutable during normal listening. A later dynamic PS therefore cannot
  // replace it; rescanning remains the explicit way to rebuild station names.
  flag_sel = flag && flag_name_FM == 0;
  if (flag && flag_name_FM != 0) {
    Serial.printf("[RDS] stored FM channel %u name locked against dynamic PS: %.8s\n",
                  FMchannel, fmName);
  }
  clearUiBroadcastState();
  if (!uiBandStarting) {
    if (uiView == UiView::Tech && uiSettings.techEnabled) renderTechScreen();
    else renderListeningScreen();
  }
  Dab.requestVolume(vol);
  stationFM = 100 * stationFM_h + stationFM_l * 10;
  const bool accepted = Dab.requestFmTune(stationFM);
  if (!accepted) {
    Serial.println("[APP][WARN] FM tune request rejected while radio is busy");
  }
  Serial.printf("FM channel: %d  FM name: %s\tfrequency: %3d.%1dMHz\n",
                FMchannel, fmName, stationFM_h, stationFM_l);
  return accepted;
}

void addStation(char * name, uint8_t param1, uint8_t param2, uint32_t param3,
                uint32_t param4, uint8_t charset) {
    if (countSort >= MAX_DAB_STATIONS) return;
    foundChannel *s   = &channels[countSort];
    memset(s->name, 0, sizeof(s->name));
    memcpy(s->name, name, foundChannel::NAME_MAX_LEN);
    s->param1 = param1;
    s->param2 = param2;
    s->param3 = param3;
    s->param4 = param4;
    s->charset = charset;
    countSort++;
}

void addStationFM(char * name, uint8_t param2, uint8_t param3, bool param4) {
    if (countSort >= MAX_FM_STATIONS) return;
    foundChannelFM *s   = &channelsFM[countSort];
    memset(s->name, 0, sizeof(s->name));
    memcpy(s->name, name, foundChannelFM::NAME_MAX_LEN);
    s->param2 = param2;
    s->param3 = param3;
    s->param4 = param4;
    countSort++;
}
