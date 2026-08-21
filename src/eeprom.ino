// ------ EEPROM -------------
// 

constexpr byte ADDR_SETTINGS_MAGIC_0 = 6;
constexpr byte ADDR_SETTINGS_MAGIC_1 = 7;
constexpr byte ADDR_SETTINGS_SCHEMA = 8;
constexpr byte ADDR_SETTINGS_MARKER = 9;
constexpr byte SETTINGS_MAGIC_0 = 0x46;  // 'F'
constexpr byte SETTINGS_MAGIC_1 = 0x52;  // 'R'
constexpr byte SETTINGS_SCHEMA = 2;
constexpr byte SETTINGS_MARKER = 0xA5;
constexpr uint32_t SETTINGS_COMMIT_DELAY_MS = 5000;
constexpr int ADDR_FM_DATABASE_MAGIC = EEPROM_SIZE + 12;
constexpr int ADDR_FM_DATABASE_REGION = EEPROM_SIZE + 13;
constexpr byte FM_DATABASE_MAGIC = 0xB7;

enum SettingsDirty : uint8_t {
  DIRTY_VOLUME = 1U << 0,
  DIRTY_MODE = 1U << 1,
  DIRTY_DAB_CHANNEL = 1U << 2,
  DIRTY_FM_CHANNEL = 1U << 3,
  DIRTY_UI = 1U << 4
};

uint8_t settingsDirty = 0;
uint32_t settingsChangedAtMs = 0;
byte persistedVolume = 0;
byte persistedMode = 0;
byte persistedDabChannel = 0;
byte persistedFmChannel = 0;
byte pendingVolume = 0;
byte pendingMode = 0;
byte pendingDabChannel = 0;
byte pendingFmChannel = 0;
UiSettings persistedUiSettings;
UiSettings pendingUiSettings;

void markSettingsDirty(uint8_t flag) {
  settingsDirty |= flag;
  settingsChangedAtMs = millis();
}

void writeSettingsSchema() {
  extEEPROM.put(ADDR_SETTINGS_MAGIC_0, SETTINGS_MAGIC_0);
  extEEPROM.put(ADDR_SETTINGS_MAGIC_1, SETTINGS_MAGIC_1);
  extEEPROM.put(ADDR_SETTINGS_SCHEMA, SETTINGS_SCHEMA);
  extEEPROM.put(ADDR_SETTINGS_MARKER, SETTINGS_MARKER);
}
void startEEPROM(){
   Wire.begin();
   Wire.setClock(400000);
   Wire.setTimeOut(50);
   if (extEEPROM.begin() == false) 
  {
    Message_red("No memory detected",97);
    Serial.println("[EEPROM][FATAL] external EEPROM not detected; radio halted");
    while (true) delay(1000);
  }
  extEEPROM.setMemorySize(256000 / 8); //EEPROM is the 24256C (256k bit)
  Serial.print("Mem size in bytes: ");
  Serial.println(extEEPROM.length());
  Serial.println();
}

void cleanEEPROM() {
  byte i;
  char FMNAME[9]   = "01234567"; 
  char DABNAME[17] = "0123456789ABCDEF";
  uint16_t stationFM = 8750;                                    // Set all FM frequencies to 87.5 MHz
  TFT_aff("Init EEPROM", 50);
  extEEPROM.erase();
  
  const byte factoryVolume = 57;
  const byte factoryMode = 0;
  const byte factoryChannel = 1;
  const byte factoryTotal = 0;
  extEEPROM.put(ADDR_VOLUME, factoryVolume);
  extEEPROM.put(ADDR_MODE, factoryMode);                         // FM mode
  extEEPROM.put(ADDR_CURRENT_FM_CHANNEL, factoryChannel);
  extEEPROM.put(ADDR_TOTAL_FM_CHANNEL, factoryTotal);
  extEEPROM.put(ADDR_CURRENT_DAB_CHANNEL, factoryChannel);
  extEEPROM.put(ADDR_TOTAL_DAB_CHANNEL, factoryTotal);
  writeSettingsSchema();
  extEEPROM.put(ADDR_FM_DATABASE_MAGIC, FM_DATABASE_MAGIC);
  extEEPROM.put(ADDR_FM_DATABASE_REGION,
                static_cast<byte>(FmRegion::Europe));
  fmDatabaseRegion = static_cast<byte>(FmRegion::Europe);
  persistedVolume = pendingVolume = factoryVolume;
  persistedMode = pendingMode = factoryMode;
  persistedFmChannel = pendingFmChannel = factoryChannel;
  persistedDabChannel = pendingDabChannel = factoryChannel;
  settingsDirty = 0;
  for (i=1; i <= MAX_FM_STATIONS; i++)                          // Clear FM infos
  {
     saveFMchannelToEEPROM(i, (stationFM%100)/10, stationFM/100, 0, FMNAME);
  }   
  for ( i=1; i <= MAX_DAB_STATIONS; i++)                        // Clear DAB infos
  {
     saveDABchannelToEEPROM(i, 0, 0, 0, 0, DABNAME, 0);
  }    
  clearScreen();
  TFT_aff("Release sel", 50);    
}

void lastEEPROM(){                                               // read back : vol, dabMode, currentDABchannel, totalDABchannels
    extEEPROM.get(ADDR_VOLUME,vol);
    extEEPROM.get(ADDR_MODE,dabMode);
    extEEPROM.get(ADDR_CURRENT_DAB_CHANNEL, currentDABchannel);
    extEEPROM.get(ADDR_TOTAL_DAB_CHANNEL, totalDABchannels);
    extEEPROM.get(ADDR_CURRENT_FM_CHANNEL, currentFMchannel);  
    extEEPROM.get(ADDR_TOTAL_FM_CHANNEL, totalFMchannels); 

    if (totalDABchannels > MAX_DAB_STATIONS) {
      Serial.printf("[EEPROM][WARN] invalid DAB station count %u; list disabled\n",
                    totalDABchannels);
      totalDABchannels = 0;
    }
    if (totalFMchannels > MAX_FM_STATIONS) {
      Serial.printf("[EEPROM][WARN] invalid FM station count %u; list disabled\n",
                    totalFMchannels);
      totalFMchannels = 0;
    }
    if (totalDABchannels == 0 || currentDABchannel < 1 ||
        currentDABchannel > totalDABchannels) {
      currentDABchannel = 1;
    }
    if (totalFMchannels == 0 || currentFMchannel < 1 ||
        currentFMchannel > totalFMchannels) {
      currentFMchannel = 1;
    }

    byte magic0, magic1, schema, marker;
    extEEPROM.get(ADDR_SETTINGS_MAGIC_0, magic0);
    extEEPROM.get(ADDR_SETTINGS_MAGIC_1, magic1);
    extEEPROM.get(ADDR_SETTINGS_SCHEMA, schema);
    extEEPROM.get(ADDR_SETTINGS_MARKER, marker);
    const bool currentSchema = magic0 == SETTINGS_MAGIC_0 &&
                               magic1 == SETTINGS_MAGIC_1 &&
                               schema == SETTINGS_SCHEMA &&
                               marker == SETTINGS_MARKER;
    if (!currentSchema) {
      const byte legacyVolume = vol;
      if (legacyVolume == 0) {
        vol = 0;
      } else if (legacyVolume <= 63) {
        vol = legacyVolume + 12;
      } else {
        vol = 57;
      }
      // Bytes 10 and above contain the station tables and are deliberately
      // untouched by this one-time settings migration.
      extEEPROM.put(ADDR_VOLUME, vol);
      writeSettingsSchema();
      Serial.printf("[EEPROM] migrated volume %u -> V%u; station tables preserved\n",
                    legacyVolume, vol);
    }

    persistedVolume = pendingVolume = vol;
    persistedMode = pendingMode = dabMode;
    persistedDabChannel = pendingDabChannel = currentDABchannel;
    persistedFmChannel = pendingFmChannel = currentFMchannel;
    settingsDirty = 0;
}

void loadUiSettings() {
  constexpr uint8_t recordSize = 12;
  uint8_t record[recordSize] = {0};
  extEEPROM.read(EEPROM_SIZE, record, sizeof(record));
  const bool validV1 = record[0] == 'U' && record[1] == 'I' &&
                       record[2] == 1 && record[8] == 0xA5;
  const bool validV2 = record[0] == 'U' && record[1] == 'I' &&
                       record[2] == 2 && record[10] == 0xA5;
  const bool validV3 = record[0] == 'U' && record[1] == 'I' &&
                       record[2] == 3 && record[11] == 0xA5;
  if (validV1 || validV2 || validV3) {
    uiSettings.brightness = constrain(record[3], 20, 100);
    uiSettings.dimLevel = constrain(record[4], 5, uiSettings.brightness);
    uiSettings.dimTimeoutIndex = record[5] <= 3 ? record[5] : 1;
    uiSettings.techEnabled = record[6] ? 1 : 0;
    uiSettings.defaultView = record[7] <= 2 ? record[7] : 0;
    if (validV2 || validV3) {
      uiSettings.slideshowMode = record[8] <= 2 ? record[8] : 1;
      uiSettings.slideshowLayout = record[9] ? 1 : 0;
    }
    if (validV3) {
      uiSettings.fmRegion = sanitizeFmRegion(record[10]);
    }
  }
  uiSettings.fmRegion = sanitizeFmRegion(uiSettings.fmRegion);
  persistedUiSettings = pendingUiSettings = uiSettings;
}

void loadFmDatabaseRegion() {
  byte marker = 0xFF;
  byte stored = 0xFF;
  extEEPROM.get(ADDR_FM_DATABASE_MAGIC, marker);
  extEEPROM.get(ADDR_FM_DATABASE_REGION, stored);
  if (marker != FM_DATABASE_MAGIC || stored >= FM_REGION_COUNT) {
    // Legacy FM tables were created by the original European configuration.
    marker = FM_DATABASE_MAGIC;
    stored = static_cast<byte>(FmRegion::Europe);
    extEEPROM.put(ADDR_FM_DATABASE_MAGIC, marker);
    extEEPROM.put(ADDR_FM_DATABASE_REGION, stored);
  }
  fmDatabaseRegion = stored;
  refreshFmDatabaseForRegion();
}

void refreshFmDatabaseForRegion() {
  if (fmDatabaseRegion == sanitizeFmRegion(uiSettings.fmRegion)) {
    extEEPROM.get(ADDR_TOTAL_FM_CHANNEL, totalFMchannels);
    extEEPROM.get(ADDR_CURRENT_FM_CHANNEL, currentFMchannel);
    if (totalFMchannels == 0) {
      currentFMchannel = 1;
    } else if (currentFMchannel < 1 || currentFMchannel > totalFMchannels) {
      currentFMchannel = 1;
    }
  } else {
    // Preserve the physical table. It becomes visible again if the matching
    // region is re-selected before another successful FM scan replaces it.
    totalFMchannels = 0;
    currentFMchannel = 1;
  }
}

void saveFmDatabaseRegion(byte region) {
  region = sanitizeFmRegion(region);
  if (fmDatabaseRegion != region) {
    extEEPROM.put(ADDR_FM_DATABASE_REGION, region);
    fmDatabaseRegion = region;
  }
}

void saveUiSettingsDelayed() {
  pendingUiSettings = uiSettings;
  markSettingsDirty(DIRTY_UI);
}

bool DABreadEEPROM(byte channel){
    if (channel < 1 || channel > MAX_DAB_STATIONS) return false;
    uint8_t record[28] = {0};
    const int result = extEEPROM.read(
        ADDR_DAB_CHANNEL + 28U * (channel - 1U), record, sizeof(record));
    if (result != 0 || record[0] != channel) {
      Serial.printf("[EEPROM][WARN] DAB record %u read failed: result=%d id=%u\n",
                    channel, result, record[0]);
      return false;
    }
    currentDABchannel = record[0];
    ensemble = record[1];
    service = record[2];
    memcpy(&serviceid, record + 3, sizeof(serviceid));
    memcpy(&compid, record + 7, sizeof(compid));
    memcpy(dabName, record + 11, 16);
    dabName[16] = 0;
    dabCharset = record[27];
    return true;
}

bool FMreadEEPROM(byte channel){
    if (channel < 1 || channel > MAX_FM_STATIONS) return false;
    uint8_t record[13] = {0};
    const int result = extEEPROM.read(
        ADDR_FM_CHANNEL + 13U * (channel - 1U), record, sizeof(record));
    if (result != 0 || record[0] != channel) {
      Serial.printf("[EEPROM][WARN] FM record %u read failed: result=%d id=%u\n",
                    channel, result, record[0]);
      return false;
    }
    currentFMchannel = record[0];
    stationFM_l = record[1];
    stationFM_h = record[2];
    flag_name_FM = record[3];
    memcpy(fmName, record + 4, 8);
    fmName[8] = 0;
    return true;
}

// Last volume backup
// ------------------------
void saveVolumeToEEPROM(byte volume){
    pendingVolume = volume;
    markSettingsDirty(DIRTY_VOLUME);
}

// Save mode (DAB/FM)
// ------------------------
void saveModeToEEPROM(byte dabMode){
    pendingMode = dabMode;
    markSettingsDirty(DIRTY_MODE);
}

// Save DAB total channels
// ------------------------
void saveTotalDABchannelToEEPROM(byte total){
    byte stored;
    extEEPROM.get(ADDR_TOTAL_DAB_CHANNEL, stored);
    if (stored != total) extEEPROM.put(ADDR_TOTAL_DAB_CHANNEL, total);
}

// Save FM total channels
// ------------------------
void saveTotalFMchannelToEEPROM(byte total){
    byte stored;
    extEEPROM.get(ADDR_TOTAL_FM_CHANNEL, stored);
    if (stored != total) extEEPROM.put(ADDR_TOTAL_FM_CHANNEL, total);
}

// Save FM Current channel
// ------------------------
void saveCurrentFMchannelToEEPROM(byte current){
    pendingFmChannel = current;
    markSettingsDirty(DIRTY_FM_CHANNEL);
}

// Save DAB Current channel
// ------------------------
void saveCurrentDABChannelToEEPROM(byte current){
    pendingDabChannel = current;
    markSettingsDirty(DIRTY_DAB_CHANNEL);
}

void commitDirtySettingsIfDue() {
  if (settingsDirty == 0 ||
      static_cast<uint32_t>(millis() - settingsChangedAtMs) <
          SETTINGS_COMMIT_DELAY_MS) {
    return;
  }

  uint8_t written = 0;
  if ((settingsDirty & DIRTY_VOLUME) && pendingVolume != persistedVolume) {
    extEEPROM.put(ADDR_VOLUME, pendingVolume);
    persistedVolume = pendingVolume;
    ++written;
  }
  if ((settingsDirty & DIRTY_MODE) && pendingMode != persistedMode) {
    extEEPROM.put(ADDR_MODE, pendingMode);
    persistedMode = pendingMode;
    ++written;
  }
  if ((settingsDirty & DIRTY_DAB_CHANNEL) &&
      pendingDabChannel != persistedDabChannel) {
    extEEPROM.put(ADDR_CURRENT_DAB_CHANNEL, pendingDabChannel);
    persistedDabChannel = pendingDabChannel;
    ++written;
  }
  if ((settingsDirty & DIRTY_FM_CHANNEL) &&
      pendingFmChannel != persistedFmChannel) {
    extEEPROM.put(ADDR_CURRENT_FM_CHANNEL, pendingFmChannel);
    persistedFmChannel = pendingFmChannel;
    ++written;
  }
  if ((settingsDirty & DIRTY_UI) &&
      memcmp(&pendingUiSettings, &persistedUiSettings,
             sizeof(UiSettings)) != 0) {
    uint8_t record[12] = {
        'U', 'I', 3,
        pendingUiSettings.brightness,
        pendingUiSettings.dimLevel,
        pendingUiSettings.dimTimeoutIndex,
        pendingUiSettings.techEnabled,
        pendingUiSettings.defaultView,
        pendingUiSettings.slideshowMode,
        pendingUiSettings.slideshowLayout,
        sanitizeFmRegion(pendingUiSettings.fmRegion),
        0xA5};
    extEEPROM.write(EEPROM_SIZE, record, sizeof(record));
    persistedUiSettings = pendingUiSettings;
    written += sizeof(record);
  }
  settingsDirty = 0;
  Serial.printf("[EEPROM] settings committed after 5 s (%u byte%s written)\n",
                written, written == 1 ? "" : "s");
}

// Save channel number , ensemble, service
void saveDABchannelToEEPROM(byte channel, byte ensemble, byte service,
                            uint32_t serviceid, uint32_t compid,
                            char charName[17], byte charset){
    uint8_t record[28] = {0};
    record[0] = channel;
    record[1] = ensemble;
    record[2] = service;
    memcpy(record + 3, &serviceid, sizeof(serviceid));
    memcpy(record + 7, &compid, sizeof(compid));
    memcpy(record + 11, charName, 16);
    record[27] = charset;
    extEEPROM.write(ADDR_DAB_CHANNEL + 28*(channel-1), record, sizeof(record));
}


// Save channel number , ensemble, service
void saveFMchannelToEEPROM(byte channel, byte stationFM_l, byte stationFM_h, bool flagDisplay, char charName[9]){ 
    uint8_t record[13] = {0};
    record[0] = channel;
    record[1] = stationFM_l;
    record[2] = stationFM_h;
    record[3] = flagDisplay ? 1 : 0;
    memcpy(record + 4, charName, 8);
    extEEPROM.write(ADDR_FM_CHANNEL + 13*(channel-1), record, sizeof(record));
}

// Save flagDisplayName_RDS and RDS name
void saveFMnameToEEPROM(byte channel, bool flagDisplay, char charName[9]){
    uint8_t record[10] = {0};
    record[0] = flagDisplay ? 1 : 0;
    memcpy(record + 1, charName, 8);
    extEEPROM.write(ADDR_FM_CHANNEL + 13*(channel-1) + 3,
                    record, sizeof(record));
}
