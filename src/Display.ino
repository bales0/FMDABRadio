
String decodeDABString(const char* text)
{
    if (!text) return String();
    return decodeBroadcastText(reinterpret_cast<const uint8_t*>(text),
                               strlen(text), DAB_CHARSET_EBU_LATIN);
}


void DAB_status(void)
{
  if(totalDABchannels != 0) {
      if (Dab.status()) noteUiStatusChanged();
  }
}

void FM_status(void)
{
  if(totalFMchannels != 0) {
     if (Dab.status()) noteUiStatusChanged();
  }
}

void TFT_aff(String dabName, uint8_t posx)
{
  dabName = decodeDABString(dabName.c_str());
  dabName.trim();
  const uint16_t width = utf8TextWidth(dabName, 2);
  const int16_t x = width < screenWidth ? (screenWidth - width) / 2 : 0;
  tft.fillRect(0,posx,screenWidth,20,0);
  drawUtf8Text(dabName, x, posx, ST77XX_RED, 2, screenWidth);
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
   if (uiView == UiView::StationList) renderStationList();
   else if (uiView == UiView::Slideshow) markUiDirty(UI_DIRTY_STATUS);
   else markUiDirty(UI_DIRTY_HEADER);
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
  name.trim();
  const uint16_t width = utf8TextWidth(name, 1);
  const int16_t x = width < screenWidth ? (screenWidth - width) / 2 : 0;
  tft.fillRect(0,posx,screenWidth,11,ST77XX_BLACK);
  drawUtf8Text(name, x, posx, ST77XX_WHITE, 1, screenWidth);
}

void Aff_Scan_Service(uint8_t channel, uint8_t posx)
{
  char channelstring[20];
  uint8_t x;
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  sprintf(channelstring,"Found %d channels",channel);
  uint8_t l = String(channelstring).length();
  x = (screenWidth - l*6)/2;                                                       //character length = 5+1
  tft.fillRect(0,posx,screenWidth,8,0);                                            //x,y,width,height,color
  tft.setCursor(x,posx);
  tft.println(channelstring); 
}

void ServiceData(void)
{
   // During a scan the decoder keeps collecting PS/RadioText, but the scan
   // screen owns the display regions.
   if (scanActive() || uiBandStarting) return;
   // DAB UCS-2BE contains zero bytes inside valid characters, therefore the
   // byte length must come from the DLS/RDS assembler rather than strlen().
   const size_t length = min<size_t>(Dab.ServiceDataLength,
                                     DAB_MAX_SERVICEDATA_LEN - 1);
   const String decoded = dabMode == 1
       ? decodeBroadcastText(
             reinterpret_cast<const uint8_t*>(Dab.ServiceData), length,
             Dab.ServiceDataCharset)
       : decodeRdsText(reinterpret_cast<const uint8_t*>(Dab.ServiceData),
                       length);
   updateUiBroadcastText(decoded);
}

void clearScreen(void) 
{
  tft.fillScreen(ST77XX_BLACK);
}
