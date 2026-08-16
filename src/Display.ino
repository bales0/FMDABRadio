
char decodeDAB(uint8_t c)
{
    switch (c)
    {
        case 0xC0: return 'A';
        case 0xC2: return 'E';
        case 0xC4: return 'I';
        case 0xC6: return 'O';
        case 0xC8: return 'U';
        case 0xCA: return 'R';
        case 0xCB: return 'C';
        case 0xCC: return 'S';
        case 0xCD: return 'Z';

        case 0x80: return 'a';
        case 0x82: return 'e';
        case 0x84: return 'i';
        case 0x86: return 'o';
        case 0x88: return 'u';
        case 0xA5: return 'e';
        case 0xA6: return 'n';
        case 0xDA: return 'r';
        case 0xDB: return 'c';
        case 0xDC: return 's';
        case 0xDD: return 'z';
        case 0xFE: return 't';
        case 0xA1: return 'N';

        default:
            if (c >= 32 && c <= 126)
                return (char)c;

            return ' ';
    }
}

String decodeDABString(const char* text)
{
    String out = "";

    for (int i = 0; text[i] != 0; i++)
    {
        out += decodeDAB((uint8_t)text[i]);
    }

    return out;
}


void DAB_time(void)
{
  if(totalDABchannels != 0) {
     char timestring[24];
     Dab.time(&dabtime);
     sprintf(timestring,"%02d/%02d/%02d %02d:%02d", dabtime.Days,dabtime.Months,dabtime.Year,dabtime.Hours,dabtime.Minutes);
     tft.setTextColor(ST77XX_MAGENTA);
     tft.setTextSize(1);
     uint8_t x = (screenWidth - String(timestring).length()*6)/2;//character length = 5+1
     tft.fillRect(0,33,screenWidth-x,8,0);//x,y,with,heigh - clear all line 
     tft.setCursor(x,33);
     tft.println(timestring);  
  }
}

void FM_time(void)
{
    if(totalFMchannels != 0) {
      char timestring[24];
      sprintf(timestring,"%02d/%02d/%02d %02d:%02d", Dab.Days,Dab.Months,Dab.Year,Dab.Hours,Dab.Minutes);
      tft.setTextColor(ST77XX_MAGENTA);
      tft.setTextSize(1);
      uint8_t x = (screenWidth - String(timestring).length()*6)/2;//character length = 5+1
      tft.fillRect(0,33,screenWidth-x,8,0);//x,y,with,heigh - clear all line 
      tft.setCursor(x,33);
      tft.println(timestring);  
    }
}

void DAB_status(void)
{
  char dabstring[48];
  uint8_t x;
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  if(totalDABchannels != 0) {
      Dab.status();                                                                            //Data recover  
      sprintf(dabstring,"Rate:%dkHz Sample:%dkHz", Dab.bitrate, Dab.samplerate/1000);
      x = (screenWidth - String(dabstring).length()*6)/2;                                      //character length = 5+1
      tft.fillRect(0,47,screenWidth,8,0);//x,y,width,height,color
      tft.setCursor(x,47);
      tft.println(dabstring); 
    
      sprintf(dabstring,"RSSI:-%d SNR:%d Qual:%d%%", Dab.signalstrength, Dab.snr, Dab.quality);
      x = (screenWidth - String(dabstring).length()*6)/2;                                      //character length = 5+1
      tft.fillRect(0,57,screenWidth,8,0);                                                      //x,y,width,height,color
      tft.setCursor(x,57);
      tft.println(dabstring); 
    
      sprintf(dabstring,"PTY:%s  %s", pty[Dab.pty],audiomode[Dab.mode]);
      x = (screenWidth - String(dabstring).length()*6)/2;                                      //character length = 5+1
      tft.fillRect(0,67,screenWidth,8,0);                                                      //x,y,width,height,color
      tft.setCursor(x,67);
      tft.println(dabstring); 
  }
}

void FM_status(void)
{
  char freqstring[32];
  uint8_t x;
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE); 

  if(totalFMchannels != 0) {
     Dab.status();                                                                            //Data recover  
     sprintf(freqstring, "Freq : %3d.%1d MHz",  stationFM / 100, (stationFM % 100)/10);
     x = (screenWidth - String(freqstring).length()*6)/2;                                     //character length = 5+1
     tft.fillRect(0,52,screenWidth,8,0);//x,y,width,height,color
     tft.setCursor(x,52);
     tft.println(freqstring); 
  
     sprintf(freqstring,"RSSI:-%ddB  SNR:%ddB", Dab.signalstrength, Dab.snr);
     x = (screenWidth - String(freqstring).length()*6)/2;                                     //character length = 5+1
     tft.fillRect(0,65,screenWidth,8,0);                                                      //x,y,width,height,color
     tft.setCursor(x,65);
     tft.println(freqstring); 
  }
}

void Aff_FM_freq(void)
{
  tft.setTextColor(ST77XX_WHITE); 
  char freqstring[20];
  sprintf(freqstring, "Freq : %3d.%1d MHz",  stationFM_h, stationFM_l);
  uint8_t x = (screenWidth - String(freqstring).length()*6)/2;                              //character length = 5+1
  tft.fillRect(0,52,screenWidth,8,0);                                                       //x,y,width,height,color
  tft.setCursor(x,52);
  tft.println(freqstring); 
}

void TFT_aff(String dabName, uint8_t posx)
{
  uint8_t x;
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(2);
  dabName = decodeDABString(dabName.c_str());
  dabName.trim();
  uint8_t l = dabName.length();
  if (l > 14){                                                                             //14 caracters max with this display mode
    x = 0;
  }else{
    x = (screenWidth - l*11)/2;                                                            //character length = 5+1
  }
  tft.fillRect(0,posx,screenWidth,17,0);                                                   //x,y,width,height,color
  tft.setCursor(x,posx);
  tft.println(dabName.substring(0, 14));                                                   //14 caracters max
}

void FM_name(char fmName[9], uint8_t posx)
{
  uint8_t x;
  String affName;
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(2);
  affName = String(fmName);
  affName = decodeDABString(affName.c_str());
  affName.trim();
  uint8_t l = affName.length();
  x = (screenWidth - l*11)/2;                                                             //character length = 5+1
  tft.fillRect(0,posx,screenWidth,17,0);                                                  //x,y,width,height,color
  tft.setCursor(x,posx);
  tft.println(affName); 
}

void Message_red(String message, uint8_t posx)
{
  uint8_t x;
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(1);
  message.trim();
  uint8_t l = message.length();
  x = (screenWidth - l*6)/2;                                                             //character length = 5+1
  tft.setCursor(x,posx);
  tft.println(message); 
}

void Message(String message, uint8_t posx)
{
  uint8_t x;
  tft.setTextSize(1);
  message.trim();
  uint8_t l = message.length();
  x = (screenWidth - l*6)/2;                                                            //character length = 5+1
  tft.setCursor(x,posx);
  tft.println(message); 
}

void Volume(void)
{
   char volumestring[10];
   uint8_t x;
   sprintf(volumestring,"Volume:%d",vol);
   x = (screenWidth - String(volumestring).length()*6)/2;                              //character length = 5+1
   tft.setTextSize(1);
   tft.setTextColor(ST77XX_GREEN);
   tft.fillRect(0,84,screenWidth/2-16,8,0);                                            //x,y,width,height,color
   tft.setCursor(10,84);
   tft.println(volumestring); 
}

void FM_affNum(void)
{
  if(totalFMchannels != 0){
    uint8_t x;
    char channelstring[20];                                             
    sprintf(channelstring,"Channel:%d/%d",currentFMchannel,totalFMchannels);
    x = (screenWidth - String(channelstring).length()*6)/2;                           //character length = 5+1
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_GREEN);
    tft.fillRect(screenWidth/2-2,84,screenWidth,8,0);                                 //x,y,width,height,color
    tft.setCursor(screenWidth/2-2,84);
    tft.println(channelstring); 
  }
}

void DAB_affNum(void)
{
  if(totalDABchannels != 0){
    uint8_t x;
    char channelstring[20];  
    sprintf(channelstring,"Channel:%d/%d",currentDABchannel,totalDABchannels);   
    x = (screenWidth - String(channelstring).length()*6)/2;                          //character length = 5+1
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_GREEN);
    tft.fillRect(screenWidth/2-2,84,screenWidth,8,0);                                //x,y,width,height,color
    tft.setCursor(screenWidth/2-2,84);
    tft.println(channelstring); 
  }
}

void Aff_Scan_Freq(char freqstring[17], uint8_t posx)
{
  uint8_t x;
  tft.setTextSize(1);
  uint8_t l = String(freqstring).length();
  x = (screenWidth - l*6)/2;//character length = 5+1
  tft.fillRect(0,posx,screenWidth,8,0);                                              //x,y,width,height,color
  tft.setCursor(x,posx);
  tft.println(freqstring); 
}

void Aff_Scan_Name(String name, uint8_t posx)
{
  uint8_t x;
  tft.setTextSize(1);
  name = decodeDABString(name.c_str());
  name.trim();
  uint8_t l = name.length();
  x = (screenWidth - l*6)/2;                                                        //character length = 5+1
  tft.fillRect(0,posx,screenWidth,8,0);                                             //x,y,width,height,color
  tft.setCursor(x,posx);
  tft.println(name); 
}

void Aff_Scan_Service(uint8_t channel, uint8_t posx)
{
  char channelstring[20];
  uint8_t x;
  tft.setTextSize(1);
  sprintf(channelstring,"Found %d channels",channel);
  uint8_t l = String(channelstring).length();
  x = (screenWidth - l*6)/2;                                                       //character length = 5+1
  tft.fillRect(0,posx,screenWidth,8,0);                                            //x,y,width,height,color
  tft.setCursor(x,posx);
  tft.println(channelstring); 
}

void ServiceData(void)
{
   //char statusstring[72];
   char statusstring[150];
   String line1;
   String line2;
   uint8_t x;
   if (dabMode == 1)
   {
      tft.setTextSize(1);
      tft.setTextColor(ST77XX_YELLOW);
      sprintf(statusstring, PSTR("%s"),Dab.ServiceData);
      String txt = decodeDABString(statusstring);
      line1 = txt.substring(0,26);
      line2 = txt.substring(26,txt.length());
      tft.fillRect(0,102,screenWidth,27,0);                                       //x,y,width,height,volume
      x = (screenWidth - line1.length()*6)/2;                                     //character length = 5+1
      tft.setCursor(0,102);
      tft.println(line1);
      x = (screenWidth - line2.length()*6)/2;                                     //character length = 5+1
      tft.setCursor(0,115); 
      tft.println(line2);
     }else{
      tft.setTextSize(1);
      tft.setTextColor(ST77XX_YELLOW);
      sprintf(statusstring, PSTR("%s"),Dab.ServiceData);
      String txt = decodeDABString(statusstring);
      line1 = txt.substring(0,26);
      line2 = txt.substring(26,txt.length());
      tft.fillRect(0,102,screenWidth,27,0);                                      //x,y,width,height,volume
      x = (screenWidth - line1.length()*6)/2;                                    //character length = 5+1
      tft.setCursor(0,102);
      tft.println(line1);
      x = (screenWidth - line2.length()*6)/2;                                    //character length = 5+1
      tft.setCursor(0,115); 
      tft.println(line2);
   }
}

void clearScreen(void) 
{
  tft.fillScreen(ST77XX_BLACK);
}
