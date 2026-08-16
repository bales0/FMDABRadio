

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
// - <scan> : starts scanning of all available DAB or FM Channels (storage in the non volatile flash EEPROM)
// - After scanning FM channels, defaut name is set to "unknown?" then a process search for valid RDS names and store them in EEPROM
//   at the end a sort is done and channel 1 is selected.
//   this process can be stopped just by pushing Upper Right switch. 
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
#include "DABShield.h"                // Thank you AVIT Research and Adrian for this nice library !!!!
#include <Adafruit_GFX.h>             // Core graphics library
#include <Adafruit_ST7735.h>          // Hardware-specific library for ST7735
#include <Wire.h>
#include "SparkFun_External_EEPROM.h" // Click here to get the library: http://librarymanager/All#SparkFun_External_EEPROM
String decodeDABString(const char* text);
ExternalEEPROM extEEPROM;

//---------------- Screen connection and definition -----------------------
//
#define TFT_CS        12         // Display chip select
#define TFT_RST       -1         // Display reset (use of EN from ESP32)
#define TFT_DC        25         // Display data/command select

uint8_t screenWidth  = 160;
uint8_t screenHeight = 128;

Adafruit_ST7735 tft  = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// --------- Keyboard I/O assignation ------
//
#define vol_up        4    // Up
#define vol_down      5    // Down
#define scan_sw       34   // Upright
#define mode_sw       35   // Upleft
#define sel           32   // Enter
#ifdef ESP32_PICO            // Schematic
#define ch_down       37    // Left      
#define ch_up         38    // Right    
#endif 
#ifdef ESP32_DEVKIT          // My prototype
#define ch_down       16     // Left
#define ch_up         17     // Right
#endif               

// ----------- DABShield I/O assignation -----
//
uint8_t slaveSelectPin = 13;    // CSSB (IO13) on schematic
uint8_t SCKPin         = 18;
uint8_t MISOPin        = 19;
uint8_t MOSIPin        = 23;
uint8_t pwen           = 2;
#define resetPin         14

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
unsigned long lastScan = 0;               // the last time the output pin was toggled
unsigned long scanDelay = 200;            // the scan time; increase if the output flickers
unsigned long lastStatus = 0;             // the last time the output pin was toggled
unsigned long statusDelay = 5000;         // update status display every 5s
unsigned long lastRDS = 0;                // the last time the RDS name search was launched
unsigned long timeOut = 20000;            // timeout for searching RDS name

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
    static int compareByName(const void *s1, const void *s2) {
        return strcmp(((foundChannel*)s1)->name, ((foundChannel*)s2)->name);
    }
    char name[NAME_MAX_LEN + 1];
    uint8_t  param1;
    uint8_t  param2;  
    uint32_t param3;
    uint32_t param4;
    bool     param5;    
    foundChannel(const char * const name, const uint8_t param1, const uint8_t param2, const uint32_t param3, const uint32_t param4, const bool param5): param1(param1), param2(param2), param3(param3), param4(param4), param5(param5)
    {
        uint8_t len = strlen(name);
        strncpy(this->name, name, len < NAME_MAX_LEN ? len : NAME_MAX_LEN);
        this->name[len] = 0;
    }
    foundChannel() : foundChannel("", 0, 0, 0, 0, 0) {}
};
foundChannel channels[MAX_DAB_STATIONS];

struct foundChannelFM {
    static constexpr uint8_t NAME_MAX_LEN = 16;
    static int compareByName(const void *s1, const void *s2) {
        return strcmp(((foundChannelFM*)s1)->name, ((foundChannelFM*)s2)->name);
    }
    char name[NAME_MAX_LEN + 1];
    uint8_t param1;
    uint8_t param2;  
    uint8_t param3;
    bool    param4;    
    foundChannelFM(const char * const name, const uint8_t param1, const uint8_t param2, const uint8_t param3, const bool param4): param1(param1), param2(param2), param3(param3), param4(param4)
    {
        uint8_t len = strlen(name);
        strncpy(this->name, name, len < NAME_MAX_LEN ? len : NAME_MAX_LEN);
        this->name[len] = 0;
    }
    foundChannelFM() : foundChannelFM("", 0, 0, 0, 0) {}
};
foundChannelFM channelsFM[MAX_DAB_STATIONS];

// -------------------- DAB initialization ---------------
//
   DAB Dab;
   DABTime dabtime;

// --------------------- Global variables ----------------
//
uint8_t  vol;
uint8_t  service;
uint32_t serviceid;
uint32_t compid;
uint8_t  ensemble;
uint8_t  freq = 0;
uint8_t  newChannel;
uint16_t stationFM;                 // stationFM = 100*stationFM_h + stationFM_l
bool     flag_sel;
String   before = "before";         // used in serial prints
String   after = "after";
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
char     fmName[9];
char     dabName[17];

// ------------------------ Setup -------------------------------
//
void setup() {
  pinMode(pwen, OUTPUT);                     // DABshield I/O
  digitalWrite(pwen, LOW);                   // Power OFF DABshield

  pinMode(slaveSelectPin, OUTPUT);
  // digitalWrite(slaveSelectPin, HIGH);
  pinMode(resetPin, OUTPUT);
  // digitalWrite(resetPin, LOW);
  
  pinMode(vol_up, INPUT_PULLUP);                    // keyboard inputs
  pinMode(vol_down, INPUT_PULLUP);
  pinMode(ch_down, INPUT_PULLUP);
  pinMode(ch_up, INPUT_PULLUP);
  pinMode(sel, INPUT_PULLUP);
  pinMode(mode_sw, INPUT);
  pinMode(scan_sw, INPUT);

  pinMode(15, OUTPUT);                       // display background LED
  digitalWrite(15, HIGH);

  pinMode(27, OUTPUT);                       // G0 headphone amplifier gain
  digitalWrite(27, HIGH);
  pinMode(33, OUTPUT);                       // G1
  digitalWrite(33, HIGH);

  Serial.begin(115200);                      // Initialise the terminal
  while(!Serial);
   
  SPI.begin();

  tft.initR(INITR_BLACKTAB);                 // Initialize ST7735R screen
  tft.setRotation(3);
  tft.setTextWrap(0);
  clearScreen();

  TFT_aff("Hello DAB+ !",25);
  tft.setTextColor(ST77XX_WHITE);
  Message("(c)2026* Y.Bourdon",60);
  Message("Version 1.71 06.06.2026",75);

  Serial.println("EEPROM FM first byte     : " + String(ADDR_FM_CHANNEL));
  Serial.println("EEPROM FM last byte      : " + String(MAX_FM_STATIONS * 13 + ADDR_FM_CHANNEL - 1));
  Serial.println("EEPROM DAB first byte    : " + String(ADDR_DAB_CHANNEL));
  Serial.println("EEPROM DAB last byte     : " + String(MAX_DAB_STATIONS * 28+ ADDR_DAB_CHANNEL - 1));
  Serial.println("Minimum EEPROM size      : " + String(EEPROM_SIZE));
  
  tft.setTextColor(ST77XX_GREEN);

  startEEPROM();
  if(extEEPROM.begin() == false)
  {
    Message_red("No EEPROM detected ",110);
    while(true);                                // no EEPROM freezing .....
  }
  else  
  {
    tft.setTextColor(ST77XX_GREEN);
    Message("I2C EEPROM connected",97);  
    extEEPROM.setMemorySize(256000 / 8); //AT24C256 (256k bit)
    Serial.println("EEPROM size in bytes     : " + String(extEEPROM.length()) + "\n");
  }

  Dab.setCallback(ServiceData);                 // DAB Setup
  Dab.begin();
  if(Dab.error != 0)
  {
    Message_red("Si4684:check connections",110);
    while(true);                                // no DABShield freezing .....
  }
  else  
  {
    tft.setTextColor(ST77XX_GREEN);
    Message("Si4684 connected",110); 
  }
  delay(2000);

  clearScreen();
  if(!digitalRead(sel)) {                      // init EEPROM if <sel> at startup                                    
       cleanEEPROM();                   
       cleanChannels(MAX_DAB_STATIONS);
       totalFMchannels = MAX_FM_STATIONS;
       totalDABchannels = MAX_DAB_STATIONS;
       ListChannels();
  }   
  do {
  } while (!digitalRead(sel));                  
 
  lastEEPROM();                                // read back : vol, dabMode, currentDABchannel, totalDABchannels, currentFMchannel, totalFMchannels
  displayLast();
  ListChannels();                              // list of FM and DAB channels on serial port

  clearScreen();
  if (dabMode == 1){                           // DAB mode
     TFT_aff("DAB mode", 40);
     Dab.begin(0);
     if(totalDABchannels != 0){
        delay(500);
        clearScreen();
        DAB_SetChannel();                      // select current channel
     }else{
        TFT_aff("Please Scan!",65);
     }
  }else{
     TFT_aff("FM mode", 40);
     Dab.begin(1);
     if(totalFMchannels != 0){
        delay(500);
        clearScreen();
        FMsetChannel(currentFMchannel, 1);    // select currentFMchannel and displays name
     }else{
        TFT_aff("Please Scan!",65);
     }       
  }  
}


// ------------------------------------ Loop ---------------------------------------------------
//
void loop() {
  
 Dab.task();
  
 if ((millis() - lastScan) > scanDelay) {                              // scan every 200mS
    lastScan = millis();
    if (!digitalRead(vol_up)) { //volume up
         if (vol < 63)
            {
              vol++;
              Volume();
              Dab.vol(vol);
              saveVolumeToEEPROM(vol);
              Serial.printf("Volume set to: %d/63\n", vol);
            }
            
    }else if (!digitalRead(vol_down)) {                                // volume down 
          if (vol > 0) 
          {
            vol--;
            Volume();
            Dab.vol(vol);
            saveVolumeToEEPROM(vol);
            Serial.printf("Volume set to: %d/63\n", vol);
          }
     
    }else if(!digitalRead(scan_sw)) {                                  // scanning channels
        clearScreen();
        TFT_aff("Scanning ...", 20);
        if (dabMode == 1){
          DAB_scan();                                                  // scan DAB channels
          if (totalDABchannels !=0) DAB_SetChannel();                  // select current channel
        }else{
          FM_scan();                                                   // scan FM chanels
          if (totalFMchannels !=0) FMsetChannel(currentFMchannel, 1);  // select currentFMchannel and displays name
        }
  
    }else if(!digitalRead(ch_up)) {
       if (dabMode == 1){                                              // DAB mode
         if(totalDABchannels != 0) {
           if (currentDABchannel < totalDABchannels){
             currentDABchannel++;
             DABreadEEPROM(currentDABchannel);
             DAB_affNum();
             TFT_aff(dabName, 8);
           }
         }
       }else{
         if(totalFMchannels != 0) {
            if (currentFMchannel < totalFMchannels){
               currentFMchannel++;
               FMreadEEPROM(currentFMchannel);
               FM_affNum();
               Aff_FM_freq();
               FM_name(fmName, 8);
               flag_sel = 0;
            }
         }
       }
  
    }else if(!digitalRead(ch_down)) {
       if (dabMode == 1){ 
        if(totalDABchannels != 0) {
          if (currentDABchannel >1){
             currentDABchannel--;
             DABreadEEPROM(currentDABchannel);
             DAB_affNum();
             TFT_aff(dabName, 8);
          }
        }
       }else{
          if(totalFMchannels != 0) {
            if (currentFMchannel >1){
               currentFMchannel--;
               FMreadEEPROM(currentFMchannel);
               FM_affNum();
               Aff_FM_freq();
               FM_name(fmName, 8);
               flag_sel = 0;
            }
          }
       }
       
    }else if(!digitalRead(sel)) {                                         // select currentChannel
       if (dabMode == 1){                                                 // DAB mode
          if(totalDABchannels != 0) {
             clearScreen();
             saveCurrentDABChannelToEEPROM(currentDABchannel);  
             DAB_SetChannel();   
          }
       }else{
          if(totalFMchannels != 0) {
             clearScreen();
             flag_sel = 1;
             saveCurrentFMchannelToEEPROM(currentFMchannel); 
             FMsetChannel(currentFMchannel, 1);                          // select currentFMchannel and displays name     
          }   
       }             

    }else if(!digitalRead(mode_sw)) {                                    // select mode DAB/FM
          clearScreen();
          dabMode = !dabMode;
          saveModeToEEPROM(dabMode);    
          lastEEPROM();
          if (dabMode == 1){                                             // DAB mode
             TFT_aff("DAB mode", 50);
             delay(500);
             Dab.begin(0);
             clearScreen();            
             if(totalDABchannels != 0){
                DAB_SetChannel();                                       // select current channel
             }else{
                TFT_aff("Please Scan!",50);
             }
         }else{
             TFT_aff("FM mode", 50);
             delay(500);
             Dab.begin(1);
             clearScreen();
             if(totalFMchannels != 0){
                FMsetChannel(currentFMchannel, 1);                     // select currentFMchannel and displays name
             }else{
               TFT_aff("Please Scan!",50);
             }                  
         }
    }
         
 }else if ((millis() - lastTime) > timeDelay) {                       // updates Time and Date and Status every 30 seconds
    if (dabMode == 1){                                                // DAB mode
       DAB_time();
    }else{
       FM_time();
    }
    lastTime = millis();
    
 }else if ((millis() - lastStatus) > statusDelay) {                   // updates  Status every 1 second
    if (dabMode == 1){                                                // DAB mode
            DAB_status();
         }else{
            FM_status();
            ServiceData();                                            // check for service data and displays
    }
    lastStatus = millis();
 }
       

 if (dabMode == 0){                                                  // only for FM mode
    if(flag_sel) {                                                   // sel switch was pressed
      if (String(Dab.ps).length() != 0) {
           String tmp = decodeDABString(Dab.ps);
           tmp.trim();
           while(tmp.length() < 8){ tmp += " "; }
           tmp.substring(0,8).toCharArray(newFMname,9);
           FM_name(newFMname, 8);                                       // displays FM name once
           flag_name_FM = 1;
           String nameString = decodeDABString(Dab.ps);                       // name can be "   FIP    " or "FIP     " or "      FIP"....
           nameString.trim();                                        // trim it
           uint8_t l = nameString.length();
           if (l < 8){
              for (uint8_t j=l; j<8; j++){
                 nameString = nameString + " ";
              }
           }
           nameString.toCharArray(newFMname,9);          
           if (String(fmName) != nameString){                                // EEPROM RDS name is different from updated RDS name
              saveFMnameToEEPROM(currentFMchannel,flag_name_FM, newFMname);  // save data with formated RDS name
              clearScreen();
              TFT_aff(newFMname, 8);
              TFT_aff("Sorting ...", 40);
              sortFMchannels();                                              // RDS name list has changed so sort it
              listStations(after);
              clearScreen();
              findFMstation();                                               // search new channel number
              Serial.println("new Channel: " + String(newChannel));
              currentFMchannel = newChannel;                                 // set to new channel number (after sorting)
              saveCurrentFMchannelToEEPROM(currentFMchannel);                
              FMsetChannel(currentFMchannel, 1);                             // select currentFMchannel and displays name 
           }
           flag_sel = 0;                                                     // updates only once
     }       
    }
 }  
             
}                                                                            // end of void loop


void findFMstation(void) {
  //countSort--;
  for (int i = 0; i <= countSort; i++)
  {
    foundChannelFM *s = &channelsFM[i]; 
    if(strcmp(s->name , newFMname) == 0 ) {
       newChannel = i+1;
       break;
    }
  }
}


void DAB_scan(void)
{
  uint8_t freq_index;
  char freqstring[32];
  char nameString[17];  
  totalDABchannels = 0;
  countSort = 0;
  Serial.println("\nSearching DAB channels ...");
  for (freq_index = 0; freq_index < DAB_FREQS; freq_index++)
  {  
    sprintf(freqstring, "Freq %02d/37   %03d.%03d MHz", freq_index, (uint16_t)(Dab.freq_khz(freq_index) / 1000), (uint16_t)(Dab.freq_khz(freq_index) % 1000));
    tft.setTextColor(ST77XX_GREEN);                                                     // green display
    Aff_Scan_Freq(freqstring,50);
    Dab.tune(freq_index);
    Serial.print("Ensemble: ");
    Serial.println(freq_index);
    if(Dab.servicevalid() == 1)
    {
       tft.setTextColor(ST77XX_GREEN);                                                  // green display
       Aff_Scan_Freq(Dab.Ensemble,65);   
       for (uint8_t i = 0; i < Dab.numberofservices; i++)
       {
          serviceid = Dab.service[i].ServiceID;                                         // identifier for station
          compid = Dab.service[i].CompID;                                               // identifier for station, not sure if needed at al
          String nameString = String(Dab.service[i].Label);                             // name can be "FIP    " or "  FIP  "
          nameString.trim();
          uint8_t l = nameString.length();
          if (l < 16){
              for (uint8_t j = l; j <= 16; j++){
                 nameString = nameString + " ";
              }
          } 
          if (l == 0) nameString = "unknown?        ";  
          nameString.toCharArray(dabName,17);
          Serial.print(countSort+1);
          Serial.print(">\t");
          Serial.print(dabName);  
          Serial.print("\t ServiceID: ");
          Serial.print(serviceid);
          Serial.print("\t CompID: ");
          Serial.println(compid);
          tft.setTextColor(ST77XX_WHITE);                                                             // white display
          Aff_Scan_Name(dabName,100);
          totalDABchannels++;                                                                         // total number of received channels from 1 to n                                                                                  
          if (countSort < MAX_DAB_STATIONS) addStation(dabName,freq_index,i, serviceid, compid, 0);   // from 0 to n-1                                                                                    
          Aff_Scan_Service(totalDABchannels, 85);           
       }
    }
  }
  saveTotalDABchannelToEEPROM(totalDABchannels);                                                      // end of scan
  clearScreen();

  if(totalDABchannels != 0) {
      TFT_aff("Channels", 8);
      TFT_aff("Sorting ...", 40);
      sortStations();                                                                                // sort channels array
      listStations(after);
      for (uint8_t i = 0; i < totalDABchannels; i++)
      {
        foundChannel *s = &channels[i];
        saveDABchannelToEEPROM(i+1, s->param1, s->param2, s->param3, s->param4, s->name);            // save to EEPROM
        delay(10);
      }
      cleanChannels(totalDABchannels);                               
      currentDABchannel = 1;                                                                         // we use the first available service
      saveCurrentDABChannelToEEPROM(currentDABchannel);  
      clearScreen();
  }else{
      clearScreen();
      TFT_aff("No channel !",55);
      currentDABchannel = 0;                                                                        // no available service
      saveCurrentDABChannelToEEPROM(currentDABchannel);  
  }
}

void FM_scan(void)                                                                            // 1st step : searching and storing all avalable FM channels (RSSi and SNR are good enough)
{
  uint16_t startfreq = Dab.freq; 
  uint8_t m;  
  uint8_t i ,j, k;
  bool flagTimeout = 0;
  bool n;
  Serial.println("\nSearching FM channels ...");
  Dab.vol(0);
  Dab.tune((uint16_t)8750);
  totalFMchannels = 0;
  char FMname[9] = "unknown?";
  while(Dab.seek(1, 0) == 1)
  {
    totalFMchannels++;
    countSort = totalFMchannels - 1;
    saveFMchannelToEEPROM(totalFMchannels, (Dab.freq%100)/10, Dab.freq/100, 0, FMname);        // save channel, freq_l, freq_h, flag=0 (no RDS name), "unknown?"
    addStationFM(FMname,totalFMchannels, (Dab.freq%100)/10, Dab.freq/100, 0);                  // save data in channels buffer
    char freqstring[24];  
    sprintf(freqstring, "Freq : %3d.%1d MHz",  Dab.freq / 100, (Dab.freq %100)/10);
    Serial.print(totalFMchannels);
    Serial.print(">\t");
    Serial.println(freqstring);
    tft.setTextColor(ST77XX_GREEN);                                                            // green display
    Aff_Scan_Freq(freqstring, 60);
    Aff_Scan_Service(totalFMchannels, 80);    
    if(Dab.freq == 10790){
      break;
    }
  }
  saveTotalFMchannelToEEPROM(totalFMchannels);                                                 // end of scan 
  Serial.println();
  clearScreen();

  if (totalFMchannels != 0) {
      TFT_aff("Search RDS...", 20);
      countSort = 0;                                                                           // 2nd step : looking for a valid RDS name - update EEPROM with the valid RDS or leave unknown? if not found
      for (i = 1 ; i<= totalFMchannels; i++){        
         lastRDS = millis();
         flagTimeout = 0;                                                                      // reset flag                      
         FMsetChannel(i , 0);                                                                  // select FM channel and doesn't update name on display
         FMreadEEPROM(i);                                                                      // read info from EEPROM
         do {
              Dab.task();
              m = strlen(Dab.ps);
              if (! digitalRead(scan_sw)){                                                     // read keyboord (scan)
                 i = totalFMchannels;                                                          // force la sortie
                 flagTimeout = 1;                                                              // emulates Timeout
                 clearScreen();                                                                // clear screen
                 delay(scanDelay);                                                             // wait for end of scan
                 break;
              }
              if((millis()-lastRDS) > timeOut){
                flagTimeout = 1;
              }         
         } while ((m == 0) && !flagTimeout ) ;                                                 // wait for a valid RDS name or scan key pressed or timeOut=1  
              
         if (!flagTimeout) {                                                                   // timeOut then next station
             flag_name_FM = 1;
             for(j = 0; j < 8; j++) {                                                          // blank newName
                newFMname[j] = ' ';
             }
             newFMname[8] = 0;                                                                 // terminator
             j = 0;                                                                            // name can be "   FIP    " or "FIP     " or "      FIP"....
             bool flag = 0;
             for (k = 0; k <8; k++) {
                if ( (Dab.ps[k] != ' ') && (flag == 0) ){
                   flag = 1;
                }
                if (flag == 1) {
                   newFMname[j] = Dab.ps[k];                                                   // left justify RDS name
                   j++;
                }
             }
             m = strlen(newFMname);                                                            // right fill DAB name with blanks
             if (m < 8){
                for (j = m; j<8; j++){
                   newFMname[j] = ' ';
                }
                newFMname[8] = 0;                                                              // terminator
             }
        
             FM_name(newFMname, 52);                                                           // displays FM name 
             Serial.print(i);                                                                  // RDS name found or timeout or force exit
             Serial.print(">\t");
             Serial.print("RDS name: " + String(Dab.ps));        
             Serial.println("\tNew Name: "+ String(newFMname));     
             addStationFM(newFMname,currentFMchannel, stationFM_l, stationFM_h, flag_name_FM); //save data with formated RDS name                                       
         }else{
             Serial.print(i);                                                                  // RDS name found or timeout or force exit
             Serial.print(">\ttimeout : ");
             FM_name(fmName, 52);                                                              // displays FM name  
             Serial.println(fmName);
             addStationFM(fmName,currentFMchannel, stationFM_l, stationFM_h, flag_name_FM);    // no change       
         }
      }
      clearScreen();
      TFT_aff("Channels", 8);
      TFT_aff("Sorting ...", 40); 
      sortNewFMlist();                                                                         // RDS name list has changed so sort it   
      listStations(after);
      currentFMchannel = 1;                                                                    // we use the first available service
      saveCurrentFMchannelToEEPROM(currentFMchannel);  
      clearScreen();
      FMsetChannel(currentFMchannel, 1);                                                       // select currentFMchannel and displays name 
    }else{
      clearScreen();
      TFT_aff("No channel !",55);
      currentFMchannel = 0;                                                                    // we use the first available service
      saveCurrentFMchannelToEEPROM(currentFMchannel); 
    }
}          

void DAB_SetChannel(void)
{
  DABreadEEPROM(currentDABchannel);
  TFT_aff(dabName, 8);                          // display DAB name
  DAB_affNum();                                 // display currentChannel/totalDABchannelss
  Volume();                                     // display volume set
  Dab.vol(vol);                                 // set volume
  Dab.tuneservice(ensemble, serviceid, compid);
  Serial.printf("DAB channel: %d  ", currentDABchannel);
  Serial.printf("DAB name: %s\n", dabName);
  //Serial.printf("ensemble: %d", ensemble);    // debug
  //Serial.printf("\tservice: %d", service);
  //Serial.printf("\tserviceid: %d", serviceid);
  //Serial.printf("\tcompid: %d\n", compid);
}

void FMsetChannel(uint8_t FMchannel, bool flag)
{
  FMreadEEPROM(FMchannel);
  if (flag) FM_name(fmName, 8);                 // display FM name
  FM_affNum();                                  // display channel number/total channels
  Volume();                                     // display volume set
  Dab.vol(vol);                                 // set volume
  stationFM = 100*stationFM_h + stationFM_l*10;
  Dab.tune(stationFM);
  Serial.printf("FM channel: %d  ", FMchannel);
  Serial.printf("FM name: %s", fmName);
  Serial.printf("\tfrequency: %3d.%1d MHz\n",stationFM_h,stationFM_l);
}

void sortNewFMlist(void)
{
uint8_t i;
char charName[9];
  sortStationsFM();
  for (i = 1; i <= totalFMchannels; i++)
  {
    foundChannelFM *s = &channelsFM[i-1];
    for (int j=0; j < 8; j++){
      charName[j] = s->name[j];                                             // RDS name has only 8 characters
    }
    charName[8] = 0;                                                        // terminator
    saveFMchannelToEEPROM(i, s->param2, s->param3, s->param4, charName);    // save to EEPROM after sorting
  }
}

void sortFMchannels(void)
{
uint8_t i;
char charName[9];
countSort = 0;
for (i=0 ; i < totalFMchannels ; i++){
  FMreadEEPROM(i+1);
  countSort = i;
  addStationFM(fmName,currentFMchannel, stationFM_l, stationFM_h, flag_name_FM);
}
  sortStationsFM();
  for (i = 1; i <= totalFMchannels; i++)
  {
    foundChannelFM *s = &channelsFM[i-1];
     for (int j=0; j < 8; j++){
      charName[j] = s->name[j]; //RDS name has only 8 characters
    }
    charName[8] = 0;                                                        // terminator
    saveFMchannelToEEPROM(i, s->param2, s->param3, s->param4, charName);    // save to EEPROM after sorting
  }
}

void addStation(char * name, uint8_t param1, uint8_t param2, uint32_t param3, uint32_t param4, bool param5) {
    uint8_t len = strlen(name);
    foundChannel *s   = &channels[countSort];
    strncpy(s->name, name, len < foundChannel::NAME_MAX_LEN ? len : foundChannel::NAME_MAX_LEN);
    s->param1 = param1;
    s->param2 = param2;
    s->param3 = param3;
    s->param4 = param4;
    s->param5 = param5;
    countSort++;
}

void addStationFM(char * name, uint8_t param1, uint8_t param2, uint8_t param3, bool param4) {
    uint8_t len = strlen(name);
    foundChannelFM *s   = &channelsFM[countSort];
    strncpy(s->name, name, len < foundChannelFM::NAME_MAX_LEN ? len : foundChannelFM::NAME_MAX_LEN);
    s->param1 = param1;
    s->param2 = param2;
    s->param3 = param3;
    s->param4 = param4;
    countSort++;
}

void sortStations(void) {
    qsort(channels, countSort, sizeof(foundChannel), foundChannel::compareByName);
}

void sortStationsFM(void) {
    qsort(channelsFM, countSort, sizeof(foundChannelFM), foundChannelFM::compareByName);
}

void DABSpiMsg(unsigned char *data, uint32_t len)
{
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));    //2MHz for starters...
  digitalWrite (slaveSelectPin, LOW);
  SPI.transfer(data, len);
  digitalWrite (slaveSelectPin, HIGH);
  SPI.endTransaction();
}
