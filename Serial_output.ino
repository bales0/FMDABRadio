void listStations(String choice) {
  uint8_t i;
  countSort--;
  Serial.println();
  if (choice == "before") Serial.println("Before Sorting :");
  if (choice == "after") Serial.println("After Sorting :");
  for (i = 0; i <= countSort; i++)
  {
    if (dabMode == 0) {                 // FM mode displays freq_h , freq_l and flagName
      foundChannelFM *s = &channelsFM[i];
      Serial.print(i+1);
      Serial.print(">");
      Serial.print("\t");
      Serial.print(s->param1);
      Serial.print("\t");
      Serial.print(s->param3);
      Serial.print("\t");
      Serial.print(s->param2);
      Serial.print("\t");
      Serial.print(s->param4);
      Serial.print("\t");
      Serial.print(s->name);  
      Serial.print("\n");  
    }else{                              // DAB mode displays ensemble, service, serviceid, compid and name
      foundChannel *s = &channels[i];
      Serial.print(i+1);
      Serial.print(">");
      Serial.print("\t");
      Serial.print(s->param1);
      Serial.print("\t");
      Serial.print(s->param2);
      Serial.print("\t");
      Serial.print(s->param3);
      Serial.print("\t");
      Serial.print(s->param4);
      Serial.print("\t");
      Serial.print(s->name);  
      Serial.print("\n");
    }
  }
  //Serial.print("\n");
}

void ListChannels(void)
{
    Serial.println("---------------------------------- FM Channels ----------------------------------");
    Serial.println();
    int channel;
    for (uint8_t i = 1; i <= totalFMchannels; i++){
        channel = i;
        extEEPROM.get(ADDR_FM_CHANNEL + 13*(channel-1) + 1 , stationFM_l);
        extEEPROM.get(ADDR_FM_CHANNEL + 13*(channel-1) + 2 , stationFM_h);
        extEEPROM.get(ADDR_FM_CHANNEL + 13*(channel-1) + 3 , flag_name_FM);
        extEEPROM.get(ADDR_FM_CHANNEL + 13*(channel-1) + 4 , fmName);
        Serial.print("Memory:");
        Serial.print(i);
        Serial.print( " \tfmName:");
        Serial.print(fmName);
        Serial.print("  \tFrequency:");
        float stationFM = 100*stationFM_h + stationFM_l*10;
        Serial.print(stationFM / 100);
        Serial.print("  \tflag_name:");
        Serial.println(flag_name_FM);        
    }
    
    Serial.println();
    Serial.println("---------------------------------- DAB Channels ----------------------------------");
    Serial.println();
    for (uint8_t i=1 ;i <=totalDABchannels; i++){
        channel = i;
        extEEPROM.get(ADDR_DAB_CHANNEL + 28*(channel-1) + 1 , ensemble);
        extEEPROM.get(ADDR_DAB_CHANNEL + 28*(channel-1) + 2 , service);
        extEEPROM.get(ADDR_DAB_CHANNEL + 28*(channel-1) + 3 , serviceid);
        extEEPROM.get(ADDR_DAB_CHANNEL + 28*(channel-1) + 7 , compid);
        extEEPROM.get(ADDR_DAB_CHANNEL + 28*(channel-1) + 11 , dabName);
        Serial.print("Memory:");
        Serial.print(i);
        Serial.print( " \tdabName:");
        Serial.print(dabName);
        Serial.print("  \tEnsemble:");
        Serial.print(ensemble);
        Serial.print("  \tService:");
        Serial.print(service);
        Serial.print("  \tServiceID:");
        Serial.print(serviceid);
        Serial.print("  \tCompID:");
        Serial.println(compid);
   }
}


void displayLast(void)
{
  Serial.println();
  Serial.println("--------------------- Recall last choice ------------------");
  Serial.println();
  Serial.print(" Mode: ");
  Serial.println (dabMode);
  Serial.print(" FM channels: ");
  Serial.println (totalFMchannels);
  Serial.print(" FM current channel: ");
  Serial.println (currentFMchannel);
  Serial.print(" DAB channels: ");
  Serial.println (totalDABchannels);  
  Serial.print(" DAB current channel: ");
  Serial.println (currentDABchannel);
  Serial.print(" Volume: ");
  Serial.println (vol);
  Serial.println();
}


void debugEEPROM(int total)
{
  byte val;
  int j=0;
  for (int i=total; i < total+60 ; i++){
      extEEPROM.get(i,val);
      Serial.print(i);
      Serial.print(">");
      Serial.print(val);
      Serial.print(" \t");
      j++;
      if (j == 8) {
        j = 0;
        Serial.println();
      }
  }
}
