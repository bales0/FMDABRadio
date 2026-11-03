// Live information views for ST7735 160x128. Nine bounded text rows, no framebuffer.
bool isInfoView(UiView v) {
  return v==UiView::EpgNow || v==UiView::EpgList || v==UiView::EpgDetail ||
      v==UiView::FmRds || v==UiView::FmInfo;
}

ui_paint::Cell<160> uiInfoRows[9];
ui_paint::Cell<4> uiEpgBadge;
uint32_t uiInfoPoll=0;
uint8_t uiInfoPaintBudget=1;
bool uiInfoPaintPending=false;
void invalidateEpgBadge(){uiEpgBadge.invalidate();}

void logEpgMemory(const char* phase) {
#if EPG_ENABLE
  const unsigned bytes=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
  const unsigned words=MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT|MALLOC_CAP_EXEC;
  serialMonitor.printf("[EPG/RAM] %s core=%u buffers=%u i8=%u/%u min8=%u i32exec=%u/%u stack=%u OOM=%u\n",
    phase,unsigned(sizeof(epg::Core)),unsigned(epg::Core::bufferBytes(false)),
    heap_caps_get_free_size(bytes),heap_caps_get_largest_free_block(bytes),heap_caps_get_minimum_free_size(bytes),
    heap_caps_get_free_size(words),heap_caps_get_largest_free_block(words),uxTaskGetStackHighWaterMark(nullptr),epg_live::oom());
#else
  (void)phase;
#endif
}

void infoLine(uint8_t row,const String& text,uint16_t color) {
  if(row>=9)return;
  if(!uiInfoPaintBudget){uiInfoPaintPending=true;return;}
  if(!uiInfoRows[row].update(text.c_str(),color,2,3+row*13,1,156))return;
  --uiInfoPaintBudget;
  prepareTftSpi();
  // First/last strips cover the screen margins too. No full clear is needed.
  tft.fillRect(0,row==0?0:3+row*13,160,row==0?16:row==8?21:13,ST77XX_BLACK);
  drawUtf8Text(text,2,3+row*13,color,1,156);
}

void invalidateInfoRows() {
  for(auto& row:uiInfoRows)row.invalidate();
}

String epgTimeText(uint32_t utc,int16_t offset) {
  const int64_t local=int64_t(utc)+int32_t(offset)*60;
  if(!utc || local<=0)return "--:--";
  char line[8];snprintf(line,sizeof(line),"%02u:%02u",unsigned(local/3600%24),unsigned(local/60%60));
  return String(line);
}

void renderInfoView() {
  if(!isInfoView(uiView))return;
  uiInfoPaintBudget=1;uiInfoPaintPending=false;
  String lines[9];uint16_t colors[9];for(auto& color:colors)color=ST77XX_WHITE;
  colors[0]=uiAccentColor();colors[8]=0x8410;
  lines[8]=uiText("BAND back  SELECT detail","BAND zpet SELECT detail");
  if(uiView==UiView::FmRds || uiView==UiView::FmInfo){
    const bool live=Dab.valid && Dab.rdsSync;
    lines[0]=uiView==UiView::FmRds?"FM RDS":"FM info";
    char line[40];
    if(uiView==UiView::FmRds){
      lines[1]=live?decodeRdsText(reinterpret_cast<const uint8_t*>(Dab.ps),8):uiText("No live RDS","Bez zivych RDS");
      snprintf(line,sizeof(line),"PI %04X PTY %u",Dab.pi,Dab.pty);lines[2]=live?String(line):"PI --  PTY --";
      String text=live?decodeRdsText(reinterpret_cast<const uint8_t*>(Dab.ServiceData),Dab.ServiceDataLength):String("");
      for(unsigned i=0;i<5;++i)lines[3+i]=utf8Slice(text,i*26,26);
    }else{
      snprintf(line,sizeof(line),"%u.%02u MHz",Dab.freq/100,Dab.freq%100);lines[1]=line;
      snprintf(line,sizeof(line),"RSSI %d SNR %d",Dab.signalstrength,Dab.snr);lines[2]=line;
      lines[3]=Dab.fmPilot?"Stereo":"Mono";
      snprintf(line,sizeof(line),"AF %s  entries %u",uiSettings.fmAfEnabled?"ON":"OFF",Dab.fmAfList().count);lines[4]=line;
    }
  }else{
    lines[0]=uiView==UiView::EpgNow?"EPG Now / Next":uiView==UiView::EpgList?uiText("EPG programmes","EPG porady"):uiText("EPG detail","EPG detail");
    lines[1]=currentUiStationName();
#if EPG_ENABLE
    auto* c=epg_live::core();
    const auto state=epg_live::indicator();
    if(!c || !c->selected() || c->ambiguous() || !c->fresh() || !c->count()){
      lines[2]=uiView==UiView::EpgNow?uiText("Current and next programme","Aktualni a dalsi porad"):
        uiView==UiView::EpgList?uiText("Programme list is empty","Seznam poradu je prazdny"):uiText("Programme unavailable","Porad neni dostupny");
      lines[8]=uiView==UiView::EpgNow?uiText("SELECT = list; BAND back","SELECT seznam; BAND zpet"):
        uiText("CH select; BAND back","CH vyber; BAND zpet");
      lines[3]=state==epg_live::Indicator::Grey?uiText("EPG unavailable","EPG neni dostupne"):uiText("Waiting for programmes","Cekam na program");
      lines[5]=epg_live::oom()?uiText("EPG memory unavailable","Nedostatek pameti EPG"):
        state==epg_live::Indicator::Grey?uiText("No SPI signal","Bez SPI signalizace"):
        !c?uiText("EPG unavailable","EPG neni dostupne"):
        c->ambiguous()?uiText("Station ambiguous","Nejednoznacna stanice"):
        !c->clockTrusted()?uiText("Broadcast time unknown","Neznamy cas vysilani"):
        c->count()?uiText("Programme data stale","Stara data programu"):
        c->entryCount()?uiText("Receiving programmes","Prijimam porady"):
        uiText("Waiting for directory","Cekam na adresar");
    }else if(uiView==UiView::EpgNow){
      if(!c->clockTrusted())lines[2]=uiText("Broadcast time unknown","Neznamy cas vysilani");
      else {
        const int now=c->current(),next=c->next();
        if(now>=0){const auto& p=c->at(now);lines[2]=epgTimeText(p.start,c->offset())+" - "+epgTimeText(p.start+p.duration,c->offset());const String title=c->title(now);lines[3]=utf8Slice(title,0,26);lines[4]=utf8Slice(title,26,26);}
        else lines[3]=uiText("NOW unavailable","Aktualni porad neznamy");
        if(next>=0){lines[5]=epgTimeText(c->at(next).start,c->offset());const String title=c->title(next);lines[6]=utf8Slice(title,0,26);lines[7]=utf8Slice(title,26,26);}
        else lines[6]=uiText("NEXT unavailable","Dalsi porad neznamy");
      }
      lines[8]=uiText("SELECT = list; BAND back","SELECT seznam; BAND zpet");
    }else if(uiView==UiView::EpgList){
      if(uiEpgSelection>=c->count())uiEpgSelection=c->count()-1;
      const unsigned top=uiEpgSelection>2?uiEpgSelection-2:0;
      for(unsigned row=0;row<5 && top+row<c->count();++row){
        const unsigned i=top+row;lines[2+row]=String(i==uiEpgSelection?">":" ")+epgTimeText(c->at(i).start,c->clockTrusted()?c->offset():0)+" "+String(c->title(i));
        if(i==uiEpgSelection)colors[2+row]=ST77XX_GREEN;
      }
      if(!c->clockTrusted())lines[7]=uiText("Times UTC; clock unknown","Casy UTC; cas neznamy");
    }else{
      if(uiEpgSelection>=c->count())uiEpgSelection=c->count()-1;
      const auto& p=c->at(uiEpgSelection);
      lines[1]=String(c->title(uiEpgSelection));
      const int16_t offset=c->clockTrusted()?c->offset():0;
      lines[2]=epgTimeText(p.start,offset)+" - "+epgTimeText(p.start+p.duration,offset)+(c->clockTrusted()?"":" UTC");
      if(p.actual)lines[3]=String("Actual ")+epgTimeText(p.actual,offset)+(p.actualDuration?String(" - ")+epgTimeText(p.actual+p.actualDuration,offset):String(""));
      const String description=String(c->title(uiEpgSelection))+" | "+String(c->description(uiEpgSelection));
      for(unsigned row=0;row<4;++row)lines[4+row]=utf8Slice(description,(uiEpgScroll+row)*26,26);
      lines[8]=uiText("CH scroll; SELECT list","CH posun SELECT seznam");
    }
#else
    lines[3]="EPG disabled";
#endif
  }
  for(uint8_t i=0;i<9;++i)infoLine(i,lines[i],colors[i]);
}

bool handleInfoButton(const ButtonEvent& event) {
  const bool info=isInfoView(uiView);
  if(event.button==ButtonId::Band){
    if(event.type!=ButtonEventType::ShortPress)return true; // reserved long press
    if(scanActive() || uiBandStarting || Dab.transitionActive() || stationPreviewActive ||
       (!Dab.ready() && !Dab.busy()) || fmAfActive())return true;
    if(info){
      uiView=uiInfoReturn;renderCurrentUiView();return true;
    }
    if(!info){uiInfoReturn=uiView;uiInfoGeneration=Dab.contextGeneration();uiEpgSelection=uiEpgScroll=0;}
    serialMonitor.println(dabMode==1?"[KEY] BAND short -> EPG Now/Next":"[KEY] BAND short -> FM RDS");
    uiView=dabMode==1?UiView::EpgNow:UiView::FmRds;
    invalidateEpgBadge();invalidateInfoRows();renderInfoView();return true;
  }
  if(!info)return false;
  if(event.button==ButtonId::Select){
    if(event.type==ButtonEventType::LongPress)openSettingsMenu();
    else if(event.type==ButtonEventType::ShortPress){
      if(uiView==UiView::EpgNow)uiView=UiView::EpgList;
      else if(uiView==UiView::EpgList)uiView=UiView::EpgDetail;
      else if(uiView==UiView::EpgDetail)uiView=UiView::EpgList;
      uiEpgScroll=0;renderInfoView();
    }
    return true;
  }
  if(event.button==ButtonId::ChannelUp || event.button==ButtonId::ChannelDown){
    if(uiView==UiView::FmRds || uiView==UiView::FmInfo)return false;
    if(event.type==ButtonEventType::ShortPress || event.type==ButtonEventType::Repeat){
      const int delta=event.button==ButtonId::ChannelUp?1:-1;
#if EPG_ENABLE
      auto* c=epg_live::core();
      if(uiView==UiView::EpgList && c && c->count()){
        uiEpgSelection=uint16_t((int(uiEpgSelection)+delta+int(c->count()))%int(c->count()));
      }else
#endif
      if(uiView==UiView::EpgDetail){
        uiEpgScroll=constrain(int(uiEpgScroll)+delta,0,24);
      }
      renderInfoView();
    }
    return true; // includes repeat: never hidden tuning
  }
  return false; // volume and SCAN keep their normal meanings
}

void serviceInfoView(uint32_t now) {
  if(isInfoView(uiView) && (scanActive() || uiBandStarting || Dab.transitionActive() ||
      Dab.contextGeneration()!=uiInfoGeneration || (dabMode==0 && uiView!=UiView::FmRds && uiView!=UiView::FmInfo))){
    uiView=UiView::Text;uiMenuReturn=uiInfoReturn=UiView::Text;
    if(!scanActive() && !uiBandStarting)renderCurrentUiView();
  }
  // A bounded strip per loop returns control to Dab.task() between rows.
  if(isInfoView(uiView) && uiInfoPaintPending){renderInfoView();return;}
  if(uint32_t(now-uiInfoPoll)<250)return;
  uiInfoPoll=now;
  if(isInfoView(uiView))renderInfoView();
  if(uiView!=UiView::Text || dabMode!=1 || scanActive() || uiBandStarting){uiEpgBadge.invalidate();return;}
  const auto state=epg_live::indicator();
  const uint16_t color=state==epg_live::Indicator::Green?0x07e0:state==epg_live::Indicator::Yellow?0xffe0:0x8410;
  // Independent badge to the right of RSSI; signal updates cannot erase it.
  if(uiEpgBadge.update("EPG",color,140,ui_paint::RssiY,1,18)){
    const auto box=ui_paint::epgBounds();prepareTftSpi();
    tft.fillRect(box.x,box.y,box.w,box.h,ST77XX_BLACK);drawUtf8Text("EPG",140,ui_paint::RssiY,color,1,18);
  }
}

void serviceEpgDiagnostics(uint32_t now) {
#if EPG_ENABLE
  static uint32_t previous=0;static int last=-1;
  const auto state=epg_live::indicator();
  if(int(state)==last && uint32_t(now-previous)<10000)return;
  last=int(state);previous=now;
  const unsigned caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
  const unsigned wordCaps=MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT|MALLOC_CAP_EXEC;
  const auto* c=epg_live::core();const auto& d=epg_live::stats();
  const auto& owner=epg_live::context();
  serialMonitor.printf("[EPG/L2] state=%u reason=%s SPI=%u UA=%u/%u confirmed=%u selected=%llX count=%u route=%lu reject=%lu busy=%lu CRC=%lu dir=%lu SI=%lu PI=%lu last=%lu maxBacklog=%lu OVF=%lu RAM8=%u/%u min=%u RAM32=%u/%u stack=%u OOM=%u\n",
    unsigned(state),epg_live::oom()?"OOM":!owner.stable?"lost-context":state==epg_live::Indicator::Green?"PI-valid-selected":!owner.present?"no-SPI":c?epg::availabilityName(c->availability(true)):"SPI-confirmed-no-core",
    owner.present,owner.uaAnswered,owner.uaTotal,owner.uaConfirmed,
    c?(unsigned long long)c->selected():0ULL,c?unsigned(c->count()):0,
    (unsigned long)d.routed,(unsigned long)d.rejected,(unsigned long)d.busy,c?(unsigned long)c->stats.crcBad:0UL,
    c?(unsigned long)c->stats.directories:0UL,c?(unsigned long)c->stats.si:0UL,c?(unsigned long)c->stats.pi:0UL,
    (unsigned long)d.lastValid,(unsigned long)d.maxBacklog,(unsigned long)d.overflows,
    heap_caps_get_free_size(caps),heap_caps_get_largest_free_block(caps),heap_caps_get_minimum_free_size(caps),
    heap_caps_get_free_size(wordCaps),heap_caps_get_largest_free_block(wordCaps),uxTaskGetStackHighWaterMark(nullptr),epg_live::oom());
#else
  (void)now;
#endif
}
