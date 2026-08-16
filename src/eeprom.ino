// ------ EEPROM -------------
// 

void startEEPROM(){
   Wire.begin();
   Wire.setClock(400000);
   if (extEEPROM.begin() == false) 
  {
    Message_red("No memory detected",97);
    while (1);// freezing !
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
  
  saveVolumeToEEPROM(45);
  saveModeToEEPROM(0);                                          // FM mode
  saveCurrentFMchannelToEEPROM(1);   
  saveTotalFMchannelToEEPROM(0);
  saveCurrentDABChannelToEEPROM(1);
  saveTotalDABchannelToEEPROM(0);  
  for (i=1; i <= MAX_FM_STATIONS; i++)                          // Clear FM infos
  {
     saveFMchannelToEEPROM(i, (stationFM%100)/10, stationFM/100, 0, FMNAME);
  }   
  for ( i=1; i <= MAX_DAB_STATIONS; i++)                        // Clear DAB infos
  {
     saveDABchannelToEEPROM(i, 0, 0, 0, 0, DABNAME);
  }    
  clearScreen();
  TFT_aff("Release sel", 50);    
}

void cleanChannels(byte number) {
  countSort = 0;
  char empty[17]="                ";
  for ( byte i; i<number; i++){
     addStation(empty, 0, 0, 0, 0, 0);
  }   
}

void lastEEPROM(){                                               // read back : vol, dabMode, currentDABchannel, totalDABchannels
    extEEPROM.get(ADDR_VOLUME,vol);
    extEEPROM.get(ADDR_MODE,dabMode);
    extEEPROM.get(ADDR_CURRENT_DAB_CHANNEL, currentDABchannel);
    extEEPROM.get(ADDR_TOTAL_DAB_CHANNEL, totalDABchannels);
    extEEPROM.get(ADDR_CURRENT_FM_CHANNEL, currentFMchannel);  
    extEEPROM.get(ADDR_TOTAL_FM_CHANNEL, totalFMchannels); 
    //Serial.println("-> Read vol, dabMode, current channel, total channel, from EEPROM");   
}

void DABreadEEPROM(byte channel){
    extEEPROM.get(ADDR_DAB_CHANNEL + 28*(channel-1), currentDABchannel);
    extEEPROM.get(ADDR_DAB_CHANNEL + 28*(channel-1) + 1, ensemble);
    extEEPROM.get(ADDR_DAB_CHANNEL + 28*(channel-1) + 2, service);
    extEEPROM.get(ADDR_DAB_CHANNEL + 28*(channel-1) + 3, serviceid);
    extEEPROM.get(ADDR_DAB_CHANNEL + 28*(channel-1) + 7, compid);
    extEEPROM.get(ADDR_DAB_CHANNEL + 28*(channel-1) + 11, dabName);
    //Serial.println("-> Read current channel, ensemble, service, serviceid, compid, dabName, from EEPROM");   
}

void FMreadEEPROM(byte channel){
    extEEPROM.get(ADDR_FM_CHANNEL + 13*(channel-1), currentFMchannel);
    extEEPROM.get(ADDR_FM_CHANNEL + 13*(channel-1) + 1 , stationFM_l);
    extEEPROM.get(ADDR_FM_CHANNEL + 13*(channel-1) + 2, stationFM_h);
    extEEPROM.get(ADDR_FM_CHANNEL + 13*(channel-1) + 3, flag_name_FM);
    extEEPROM.get(ADDR_FM_CHANNEL + 13*(channel-1) + 4, fmName);
    //Serial.println("-> Read currentFMchannel, stationFM,fmName from EEPROM");   
}

// Last volume backup
// ------------------------
void saveVolumeToEEPROM(byte volume){
    extEEPROM.put(ADDR_VOLUME, volume);
    //Serial.println("-> volume has been stored in EEPROM");
}

// Save mode (DAB/FM)
// ------------------------
void saveModeToEEPROM(byte dabMode){
    extEEPROM.put(ADDR_MODE, dabMode);
    //Serial.println("-> mode has been stored in EEPROM");
}

// Save DAB total channels
// ------------------------
void saveTotalDABchannelToEEPROM(byte total){
    extEEPROM.put(ADDR_TOTAL_DAB_CHANNEL, total);
    //Serial.println("-> Total Channels has been stored in EEPROM");
}

// Save FM total channels
// ------------------------
void saveTotalFMchannelToEEPROM(byte total){
    extEEPROM.put(ADDR_TOTAL_FM_CHANNEL, total);
    //Serial.println("-> Total Channels has been stored in EEPROM");
}

// Save FM Current channel
// ------------------------
void saveCurrentFMchannelToEEPROM(byte current){
    extEEPROM.put(ADDR_CURRENT_FM_CHANNEL, current);
    //Serial.println("-> Current channel has been stored in EEPROM");
}

// Save DAB Current channel
// ------------------------
void saveCurrentDABChannelToEEPROM(byte current){
    extEEPROM.put(ADDR_CURRENT_DAB_CHANNEL, current);
    //Serial.println("-> Current channel has been stored in EEPROM");
}

// Save channel number , ensemble, service
void saveDABchannelToEEPROM(byte channel, byte ensemble, byte service, uint32_t serviceid, uint32_t compid, char charName[17]){ 
    extEEPROM.put(ADDR_DAB_CHANNEL + 28*(channel-1), channel);
    extEEPROM.put(ADDR_DAB_CHANNEL  + 28*(channel-1)+1, ensemble);
    extEEPROM.put(ADDR_DAB_CHANNEL  + 28*(channel-1)+ 2, service);
    extEEPROM.put(ADDR_DAB_CHANNEL  + 28*(channel-1)+ 3, serviceid);
    extEEPROM.put(ADDR_DAB_CHANNEL  + 28*(channel-1)+ 7, compid);
    for (int i=0; i < 16; i++){
      extEEPROM.put(ADDR_DAB_CHANNEL  + 28*(channel-1)+ 11+i, charName[i]);
    }
    charName[16] = 0;  //terminator
    extEEPROM.put(ADDR_DAB_CHANNEL  + 28*(channel-1)+ 11 + 16, charName[16]);
    //Serial.print("-> Channel has been stored in EEPROM");
}


// Save channel number , ensemble, service
void saveFMchannelToEEPROM(byte channel, byte stationFM_l, byte stationFM_h, bool flagDisplay, char charName[9]){ 
    extEEPROM.put(ADDR_FM_CHANNEL + 13*(channel-1), channel);
    extEEPROM.put(ADDR_FM_CHANNEL  + 13*(channel-1)+ 1, stationFM_l);
    extEEPROM.put(ADDR_FM_CHANNEL  + 13*(channel-1)+ 2, stationFM_h);
    extEEPROM.put(ADDR_FM_CHANNEL  + 13*(channel-1)+ 3, flagDisplay);
    for (int i=0; i < 8; i++){
      extEEPROM.put(ADDR_FM_CHANNEL  + 13*(channel-1)+ 4 + i, charName[i]);
    }
    charName[8] = 0;  //terminator
    extEEPROM.put(ADDR_FM_CHANNEL  + 13*(channel-1)+ 4 + 8, charName[8]);
    //Serial.println("-> Channel has been stored in EEPROM");
}

// Save flagDisplayName_RDS and RDS name
void saveFMnameToEEPROM(byte channel, bool flagDisplay, char charName[9]){
    extEEPROM.put(ADDR_FM_CHANNEL  + 13*(channel-1)+ 3, flagDisplay);
    for (int i=0; i < 8; i++){
      extEEPROM.put(ADDR_FM_CHANNEL  + 13*(channel-1)+ 4 + i, charName[i]);
    }
    charName[8] = 0;  //terminator
    extEEPROM.put(ADDR_FM_CHANNEL  + 13*(channel-1)+ 4 + 8, charName[8]);
    //Serial.println("-> FlagDisplay and RDS name have been stored in EEPROM");
}
