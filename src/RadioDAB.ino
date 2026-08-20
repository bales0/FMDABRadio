

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// RADIO DAB + Elektor
// 
// v1.0  01/05/2022 - initial release
// v1.1  09/05/2022 - add FM mode
// v1.2  09/05/2022 - add Sort DAB channels
// v1.3  16/05/2022 - add Sort FM channels
// v1.31 17/05/2022 - correct minor bugs
// v1.4  19/05/2022 - add auto store RDS names after scan
// v1.41 22/05/2022 - Memory up to 150 DAB channels and 80 FM channels - Selection of ESP32 Dev kit4 ou ESP32 Pico V4
// v1.42 23/05/2022 - add cleaning EEPROM at startup if <sel> pressed for at least 5 seconds
// v1.43 09/06/2022 - change <mode> IO port to IO35, bug corrections
// v1.50 14/06/2022 - crashes if scanned channels = 0, add tests, now corrected. Max DAB channels=100 and Max FM channels=40
// v1.51 21/06/2022 - in case of new FM name, select new channel after sorting
// v1.52 22/06/2022 - change variable allocation - Max DAB channels=150 and Max FM channels=80 (EEPROM SIZE is 4050 byte)
// v1.60 23/06/2022 - use of an external I2C EEPROM (24C256 32K)- Max DAB channels 250 and max FM channels 250 (8,2Ko used)
// v1.61 17/07/2022 - review code and comments. Correct some minor bugs
// v1.62 28/07/2022 - correct some minor bugs
// v1.61 17/07/2022 - freezing at startup if no EEPROM detected or Si484 connections error
// v1.62 03/08/2022 - change variable name - correct Unknown? instead of Unknow??
// v1.70 06/04/2023 - In some regions and for some ensembles, the order of stations changes at each tuning, i.e. the station
//                    gets a different index each time, and the index stored in the EEPROM at time of scanning is no longer valid.
//                    In this case, tuning by using Dab.tune(ensemble) and Dab.set_service(service) does not work correctly,
//                    but tuning has to be done via the ServiceID using Dab.tuneservice(ensemble, serviceid, compid). 
//                    For this, ServiceID & CompID are now also stored in the EEPROM (uint32_t, i.e. 4 bytes each).
//
//                    !!!! Thank you Michael for being able to fix this problem !!!!
// v1.71 06/06/2026 - DAB/RDS character conversion
// v2.0  20/08/2026 - responsive Si468x core, event-driven scan and safe settings/audio control
//
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//-------------------------------- Hardware ------------------------------------------------
// For my prototype I have used:
// - 1 ESPRESSIF ESP32 DEVKIT V4 (Use "ESP32 Dev Module" in IDE tools)
// - You can also  use an ESP32-PICO-KIT-V4 (please uncomment your choice (line69-70)
// - 1,8" TFT color screen with SPI interface - 3V3 compatible
// - 7 switches with pull-up resistors to +VCC (3V3)
// - 1 DABShield made by AVIT Research : https://www.ebay.fr/itm/302516551661
// - Power supply is supplied via the USB port from ESP32 module or via an external (5V/3A) power supply
// - New schematic (v3.0) tu use with this software (from 1.60)
//
// ------------------------------- Command -------------------------------------------------
// - At first start keep <sel> pressed while powering the board : an initialization process will start - release the <sel> switch.
// - <mode> is used to switch from DAB to FM
// - Hold <scan> for one second to scan DAB or FM; short <scan> cancels without replacing the old list.
// - New station lists remain in frequency/ensemble order. An FM RDS name updates only its own record.
// - You have to choose a channel using <ch_down> and <ch_up> then <sel> to validate.
// - Wait a few seconds, then the actual RDS name (if different from the stored name) will be permanently stored in the EEPROM for futur recall and sort will be updated.
// - <vol+> and <vol-> to adjust the volume setting.
// - After Power up, set to last mode (FM or DAB) and the last channel and volume adjustement is recalled from EEPROM.
//
// ------------------------------------------- Wiring -------------------------------------------------------------------------------------------
//
// ESP32Pic/Dev   Signal  Wired to Screen     Wired to DABShield     Wired to keyboard                  Wired to VS1838    Wired to AT24C256
// ------------   ------  ---------------     ------------------     -----------------------------      ---------------    ----------------------
// GPIO04         -       -                   -                      Vol+ (pull up to +3V3)
// GPIO05         -       -                   -                      Vol- (pull up to +3V3)
// GPIO12                 pin 3 CS            -                      -                                                                                 
// GPIO13                 -                   Slave Select           -
// GPIO14                 -                   RST                    -
// GPIO15                                                                                               pin 1 OUT
// GPIO33                                                                                             
// GPIO18         SCK     pin 7 CLK or SCK    SCK                    -                                
// GPIO19         MISO    -                   MISO                   -                                
// GPÏO21                                                                                                                  SCA (pull up to +3V3)
// GPIO22                                                                                                                  SCL (pull up to +3V3)
// GPIO23         MOSI    pin 6 DIN or SDA    MOSI                   -                               
// GPIO25                 pin 5 D/C or A0     -                      -
// GPIO26         -       -                   INT                    
// GPIO27         -       -                   PWREN        
// GPIO32         -       -                   -                      Sel  (pull up to +3V3)                                                                                                                              
// GPIO34         -       -                   -                      Scan (pull up to +3V3)
// GPIO35         -       -                   -                      Mode (pull up to +3V3)                                                       
// GPIO37/16      -       -                   -                      Ch-  (pull up to +3V3)                                                            
// GPIO38/17      -       -                   -                      Ch+  (pull up to +3V3)                                                           
// -----------    ------  ---------------     -------------------   ------------------------------     ---------------     ----------------------
// GND            -       pin 2 GND           GND                                                      pin 2 GND           pin 1-2-4 GND
// VCC 3.3V       -       pin 8-1 BL-VCC      IOREF                                                    pin 3 VCC           pin 8 VCC
// VCC 5V         -       -                   5V                                                      
// EN             -       pin 4 RST           -                                                      
//

//#define ESP32_PICO
#define ESP32_DEVKIT                  //for my prototype I use ESP32-DEVKIT-V4 wich affects some GPIOs definitions

#include <SPI.h>
#include <esp_system.h>
#include "DABShield.h"                // Si468x core + ESP32 board/application adapter
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

// --------- Pty and audio mode of DAB channel ------
//
char pty_0[]   =  "None";
char pty_1[]   =  "News";
char pty_2[]   =  "Current affairs";
char pty_3[]   =  "Information";
char pty_4[]   =  "Sport";
char pty_5[]   =  "Education";
char pty_6[]   =  "Drama";
char pty_7[]   =  "Culture";
char pty_8[]   =  "Science";
char pty_9[]   =  "Varied";
char pty_10[]  =  "Pop music";
char pty_11[]  =  "Rock music";
char pty_12[]  =  "Easy listening music";
char pty_13[]  =  "Light classical";
char pty_14[]  =  "Serious classical";
char pty_15[]  =  "Other music";
char pty_16[]  =  "Weather";
char pty_17[]  =  "Finance";
char pty_18[]  =  "Childrens programmes";
char pty_19[]  =  "Social Affairs";
char pty_20[]  =  "Religion";
char pty_21[]  =  "Phone In";
char pty_22[]  =  "Travel";
char pty_23[]  =  "Leisure";
char pty_24[]  =  "Jazz music";
char pty_25[]  =  "Country music";
char pty_26[]  =  "National music";
char pty_27[]  =  "Oldies music";
char pty_28[]  =  "Folk music";
char pty_29[]  =  "Documentary";
char pty_30[]  =  "Alarm test";
char pty_31[]  =  "Alarm";
char *pty[]  = {pty_0,pty_1,pty_2,pty_3,pty_4,pty_5,pty_6,pty_7,pty_8,pty_9,pty_10,pty_11,pty_12,pty_13,pty_14,pty_15,pty_16,pty_17,pty_18,pty_19,pty_20,pty_21,pty_22,pty_23,pty_24,pty_25,pty_26,pty_27,pty_28,pty_29,pty_30,pty_31};

char mode_0[]  = "DUAL";
char mode_1[]  = "MONO";
char mode_2[]  = "STEREO";
char mode_3[]  = "Joint ST";
char *audiomode[]  = {mode_0,mode_1,mode_2,mode_3};

unsigned long lastTime = 28000;           // the last time the date time was displayed
unsigned long timeDelay = 30000;          // update Time and Date every 30 seconds
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
foundChannelFM channelsFM[MAX_DAB_STATIONS];

// -------------------- DAB initialization ---------------
//
DAB Dab;
DABTime dabtime;
Controls controls;
Backlight backlight;

// --------------------- Global variables ----------------
//
uint8_t  vol;
uint8_t  service;
uint32_t serviceid;
uint32_t compid;
uint8_t  ensemble;
uint8_t  freq = 0;
uint16_t stationFM;                 // stationFM = 100*stationFM_h + stationFM_l
bool     flag_sel;
char     newFMname[9];
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
bool scanStartPending = false;

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
uint8_t stationListSelection = 1;
uint8_t stationListTop = 1;
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

void setup() {
  backlight.begin(backlightPin, millis());
  Dab.configureAudioPins(amplifierG0Pin, amplifierG1Pin);

  Serial.begin(115200);
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
  displayLast();
  ListChannels();

  Dab.configurePins(slaveSelectPin, interruptPin, resetPin, pwen);
  Dab.setDiagnostics(&Serial);
  Dab.setCallback(ServiceData);
  if (!Dab.setSlideshowEnabled(uiSettings.slideshowMode != 0)) {
    uiSettings.slideshowMode = 0;
    Serial.println("[SLS][WARN] slideshow disabled because RAM allocation failed");
  }

  clearScreen();
  TFT_aff(dabMode == 1 ? "Starting DAB" : "Starting FM", 40);
  if (!Dab.beginAsync(dabMode == 1 ? 0 : 1)) {
    Message_red("Radio start failed", 65);
  }
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

  if (scanStartPending && digitalRead(scan_sw) == HIGH) {
    scanStartPending = false;
    beginFullScan();
  }

  handleRadioEvents();
  serviceScan(now);
  serviceUi(now);

  if (!scanActive() && Dab.ready() && (now - lastTime) > timeDelay) {
    if (dabMode == 1) {
      DAB_time();
    } else {
      FM_time();
    }
    lastTime = now;
  }

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
    uiSlideshowDecodePending = false;
    uiDefaultSlideshowPending = dabMode == 1 &&
                                uiSettings.defaultView == 2 &&
                                uiSettings.slideshowMode != 0;
    uiView = uiSettings.defaultView == 1 && uiSettings.techEnabled
                 ? UiView::Tech : UiView::Text;
    stationPreviewActive = false;
    saveModeToEEPROM(dabMode);
    clearScreen();
    TFT_aff(dabMode == 1 ? "Starting DAB" : "Starting FM", 40);
    Dab.beginAsync(dabMode == 1 ? 0 : 1);
    return;
  }

  if (event.button == ButtonId::Scan) {
    if (scanActive()) {
      if (event.type == ButtonEventType::ShortPress) cancelFullScan(false);
      return;
    }
    if (event.type == ButtonEventType::LongPress) {
      scanStartPending = true;
      Serial.println("[KEY] full scan armed; starts on key release");
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
    if (direction > 0 && currentDABchannel < totalDABchannels) {
      ++currentDABchannel;
    } else if (direction < 0 && currentDABchannel > 1) {
      --currentDABchannel;
    } else {
      return;
    }
    stationPreviewActive = true;
    uiView = UiView::Text;
    DABreadEEPROM(currentDABchannel);
    renderListeningScreen();
  } else {
    if (totalFMchannels == 0) return;
    if (direction > 0 && currentFMchannel < totalFMchannels) {
      ++currentFMchannel;
    } else if (direction < 0 && currentFMchannel > 1) {
      --currentFMchannel;
    } else {
      return;
    }
    stationPreviewActive = true;
    uiView = UiView::Text;
    FMreadEEPROM(currentFMchannel);
    renderListeningScreen();
    flag_sel = false;
  }
}

void handleRadioEvents() {
  if (Dab.takeBandReady()) {
    clearScreen();
    Serial.printf("[APP] %s image ready\n", dabMode == 1 ? "DAB" : "FM");
    if (dabMode == 1) {
      if (totalDABchannels != 0) {
        DAB_SetChannel();
      } else {
        TFT_aff("Please Scan!", 50);
      }
    } else {
      if (totalFMchannels != 0) {
        FMsetChannel(currentFMchannel, true);
      } else {
        TFT_aff("Please Scan!", 50);
      }
    }
    if (openListAfterBandReady) {
      openListAfterBandReady = false;
      openStationList();
    }
  }

  RadioOperation operation;
  bool success;
  while (Dab.takeOperationResult(operation, success)) {
    Serial.printf("[APP] radio operation=%u result=%s\n",
                  static_cast<unsigned>(operation), success ? "ok" : "failed");
    if (handleScanRadioResult(operation, success)) continue;
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

void processFmNameUpdate() {
  if (dabMode != 0 || !flag_sel || strlen(Dab.ps) == 0) return;

  normalizeFmPs(Dab.ps, newFMname);
  const bool nameChanged = memcmp(fmName, newFMname, 8) != 0;
  const bool nameWasStored = flag_name_FM != 0;
  flag_name_FM = 1;

  if (nameChanged || !nameWasStored) {
    saveFMnameToEEPROM(currentFMchannel, flag_name_FM, newFMname);
    Serial.printf("[RDS] PS stored for FM channel %u without reordering database\n",
                  currentFMchannel);
  }
  memcpy(fmName, newFMname, sizeof(fmName));
  if (uiView == UiView::Text) {
    resetUiStationScroll();
    markUiDirty(UI_DIRTY_STATION);
  } else if (uiView == UiView::StationList) {
    renderStationList();
  }
  flag_sel = false;
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
  char text[28];
  const uint32_t frequency = Dab.freq_khz(scanDabIndex);
  snprintf(text, sizeof(text), "DAB %u/%u  %lu.%03lu",
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
  snprintf(text, sizeof(text), "FM %u.%02u MHz", frequency / 100,
           frequency % 100);
  tft.setTextColor(ST77XX_GREEN);
  Aff_Scan_Freq(text, 55);
  uint8_t progress = 0;
  if (frequency > 8750) {
    progress = static_cast<uint8_t>(
        (static_cast<uint32_t>(frequency - 8750) * 100U) / (10790 - 8750));
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
    DABreadEEPROM(previousChannel);
    currentDABchannel = previousChannel;
    scanPreviousDabServiceId = serviceid;
    scanPreviousDabComponentId = compid;
  } else if (dabMode == 0 && totalFMchannels != 0 &&
             currentFMchannel >= 1 && currentFMchannel <= totalFMchannels) {
    const byte previousChannel = currentFMchannel;
    FMreadEEPROM(previousChannel);
    currentFMchannel = previousChannel;
    scanPreviousFmFrequency = 100U * stationFM_h + 10U * stationFM_l;
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
  TFT_aff("Reading RDS names", 12);
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

void captureCurrentFmRdsName() {
  foundChannelFM* station = &channelsFM[scanRdsIndex];
  normalizeFmPs(Dab.ps, station->name);
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
      scanLastFmFrequency = Dab.freq;
      scanState = ScanState::FmSeekStart;
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
    } else if (success && Dab.valid && Dab.freq > scanLastFmFrequency &&
               Dab.freq <= 10790) {
      captureCurrentFmStation();
      if (Dab.freq >= 10790 || countSort >= MAX_FM_STATIONS) {
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
    clearScreen();
    TFT_aff(dabMode == 1 ? "Starting DAB" : "Starting FM", 40);
    Dab.beginAsync(dabMode == 1 ? 0 : 1);
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

    case ScanState::FmTuneStart:
      renderFmScanFrequency(8750);
      if (Dab.requestFmTune(8750)) scanState = ScanState::FmTuneWait;
      break;

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
      Aff_Scan_Name("Čekám na RDS...", 85);
      drawScanProgress(static_cast<uint8_t>(
          static_cast<uint16_t>(scanRdsIndex) * 100U / countSort));
      if (Dab.requestFmTune(frequency)) {
        scanState = ScanState::FmRdsTuneWait;
      }
      break;
    }

    case ScanState::FmRdsWait:
      if (strlen(Dab.ps) != 0) {
        captureCurrentFmRdsName();
        advanceFmRdsStation();
      } else if (static_cast<int32_t>(now - scanStateDeadlineMs) >= 0) {
        Aff_Scan_Name("unknown?", 85);
        Serial.printf("[SCAN][RDS] %u/%u timeout; keeping unknown name\n",
                      scanRdsIndex + 1, countSort);
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

void DAB_SetChannel(void)
{
  DABreadEEPROM(currentDABchannel);
  uiBroadcastText = "";
  if (uiView == UiView::Tech && uiSettings.techEnabled) renderTechScreen();
  else renderListeningScreen();
  Dab.requestVolume(vol);
  if (!Dab.requestDabService(ensemble, serviceid, compid)) {
    Serial.println("[APP][WARN] DAB tune request rejected while radio is busy");
  }
  const String decodedName = decodeBroadcastText(
      reinterpret_cast<const uint8_t*>(dabName), 16, dabCharset);
  Serial.printf("DAB channel: %d  DAB name: %s  charset=%u (%s)\n",
                currentDABchannel, decodedName.c_str(), dabCharset,
                broadcastCharsetName(dabCharset));
  //Serial.printf("ensemble: %d", ensemble);    // debug
  //Serial.printf("\tservice: %d", service);
  //Serial.printf("\tserviceid: %d", serviceid);
  //Serial.printf("\tcompid: %d\n", compid);
}

void FMsetChannel(uint8_t FMchannel, bool flag)
{
  FMreadEEPROM(FMchannel);
  flag_sel = flag;
  uiBroadcastText = "";
  if (uiView == UiView::Tech && uiSettings.techEnabled) renderTechScreen();
  else renderListeningScreen();
  Dab.requestVolume(vol);
  stationFM = 100*stationFM_h + stationFM_l*10;
  if (!Dab.requestFmTune(stationFM)) {
    Serial.println("[APP][WARN] FM tune request rejected while radio is busy");
  }
  Serial.printf("FM channel: %d  ", FMchannel);
  Serial.printf("FM name: %s", fmName);
  Serial.printf("\tfrequency: %3d.%1d MHz\n",stationFM_h,stationFM_l);
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
