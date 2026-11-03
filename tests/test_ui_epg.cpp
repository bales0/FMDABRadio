// Actual production EpgUi.ino and menu function bodies inserted by the runner.
#include "UiTextCache.h"
#include "epg_live.h"
#include "epg_test_data.h"
#include "Controls.h"
#include <string>
#include <vector>
#include <cstdio>
#include <cassert>
class String:public std::string {
 public:using std::string::string;String()=default;String(const std::string& s):std::string(s){}
  String substring(unsigned a,unsigned b)const{return substr(a,b-a);}
};
constexpr uint16_t ST77XX_BLACK=0,ST77XX_WHITE=0xffff,ST77XX_GREEN=0x7e0,ST77XX_YELLOW=0xffe0;
constexpr uint16_t UI_GRAY=0x8410,UI_DARK_LINE=0x3186;
constexpr uint8_t UI_MENU_ITEMS=11;
enum class UiView:uint8_t{Text,Slideshow,Tech,Tune,StationList,Menu,EpgNow,EpgList,EpgDetail,FmRds,FmInfo};
enum class FmRegion:uint8_t{Europe};
constexpr unsigned FM_REGION_COUNT=2;const unsigned UI_DIM_TIMEOUT_SECONDS[]={15,30,60,120};
UiView uiView=UiView::Text,uiInfoReturn=UiView::Text,uiMenuReturn=UiView::Text,uiViewBeforeModal=UiView::Text;
uint8_t dabMode=1,menuModeCandidate=1,menuModePrevious=1,menuSelection=0,menuTop=0,menuFmRegionOnOpen=0;
bool menuModeRequestPending=false;
uint8_t menuModeRequestTarget=1,menuModeRequestStatus=0;
bool paintedMenuModeItem=false;
uint32_t menuModeRequestGeneration=0,menuModeRequestDeadline=0;
int hostPins[256];std::vector<HostEvent> hostEvents;HostESP ESP;
bool uiBandStarting=false,menuBandCommitPending=false,menuPainted=false,scanning=false,af=false;
bool uiDefaultSlideshowPending=false,stationListPainted=false,bootAccept=true;
bool stationPreviewActive=false;
uint32_t uiInfoGeneration=1,uiInfoPoll=0,hostMillis=100;
uint8_t uiInfoPaintBudget=1;bool uiInfoPaintPending=false;
uint16_t uiEpgSelection=0,uiEpgScroll=0;
uint8_t screenWidth=160,paintedMenuLanguage=0,paintedMenuTheme=0;
unsigned clears=0,writes=0,boots=0,saves=0,regionApplies=0;
ui_paint::Cell<160> uiInfoRows[9];ui_paint::Cell<4> uiEpgBadge;
struct UiListRowCache{bool valid=false;uint16_t item=0;bool selected=false;String label,value;};
UiListRowCache menuRows[5];
struct Settings{uint8_t fmRegion=0,fmAfEnabled=0,brightness=100,dimLevel=20,dimTimeoutIndex=1,slideshowMode=2,signalUnits=0,theme=0,language=0,serialControl=1;}uiSettings;
struct Radio{
  bool valid=true,rdsSync=false,tp=false,ta=false,fmPilot=true,busyFlag=false,transition=false;
  uint16_t freq=10170,pi=0x1234,ServiceDataLength=0;uint8_t pty=4;int8_t signalstrength=45,snr=14;
  char ps[9]="",ServiceData[129]="";uint32_t generation=1;
  struct Af{uint8_t count=0;} af;
  const Af& fmAfList()const{return af;}
  bool transitionActive()const{return transition;}
  uint32_t contextGeneration()const{return generation;}
  bool busy()const{return busyFlag;}
  bool ready()const{return !busyFlag;}
  bool setSlideshowEnabled(bool){return true;}
}Dab;
std::vector<std::string> serialLines;
struct Serial{void println(const char* s){serialLines.push_back(s);}}serialMonitor;
struct Display{
  void fillRect(int x,int y,int w,int h,uint16_t){assert(x>=0 && y>=0 && x+w<=160 && y+h<=128);++writes;}
  void drawFastHLine(int,int,int,uint16_t){}
}tft;
struct Backlight{void configure(unsigned,unsigned,uint32_t){}void noteActivity(uint32_t){}}backlight;
int constrain(int x,int lo,int hi){return x<lo?lo:x>hi?hi:x;}
struct Region{const char* menuName="Europe";};Region fmRegionProfile(unsigned){return {};}
uint8_t sanitizeFmRegion(uint8_t r){return r;}
bool scanActive(){return scanning;}bool fmAfActive(){return af;}
const char* uiText(const char* en,const char* cs){return uiSettings.language?cs:en;}
uint16_t uiAccentColor(){return 0x7ff;}
const char* uiSignalUnitName(){return "dBm";}
String currentUiStationName(){return "Zprávy / Žluťoučký kůň";}
String decodeRdsText(const uint8_t* bytes,unsigned n){return String(reinterpret_cast<const char*>(bytes),n);}
constexpr uint8_t UI_DIRTY_STATUS=8;
void markUiDirty(uint8_t){}
void prepareTftSpi(){}
void drawUtf8Text(const String&,int x,int y,uint16_t,unsigned,unsigned){assert(x>=0 && x<160 && y>=0 && y+8<=128);++writes;}
void invalidateInfoRows();
void invalidateEpgBadge(){uiEpgBadge.invalidate();}
void clearScreen(){++clears;uiEpgBadge.invalidate();}
void renderInfoView();void renderMenu();
bool isInfoView(UiView);
void renderCurrentUiView();
void renderStationList(){}
bool renderSlideshowScreen(){return true;}
void renderListeningScreen(){uiView=UiView::Text;}
void saveUiSettingsDelayed(){++saves;}
void applyFmRegionSelection(){++regionApplies;}
void refreshFmDatabaseForRegion(){++regionApplies;}
bool startBandTransition(){++boots;if(bootAccept){uiBandStarting=true;uiView=UiView::Text;}return bootAccept;}
void moveStationList(int){}void tuneStationListSelection(){}
void openSettingsMenu();void closeSettingsMenu();void serviceMenuModeCommit(uint32_t);
// PRODUCTION_FUNCTIONS
static ButtonEvent key(ButtonId id,ButtonEventType type=ButtonEventType::ShortPress){return {id,type};}
static void finishPaint(){for(unsigned i=0;i<10;++i){const auto before=writes;renderInfoView();assert(writes-before<=2);}assert(!uiInfoPaintPending);}
int main(){
  assert(epg_live::ensure());
  epg_live::Context owner;owner.generation=1;owner.sid=0x3201;owner.eid=0x3153;owner.ecc=0xe2;owner.cid=5;owner.pad=owner.present=owner.stable=true;
  epg_live::context(owner);
  assert(handleUiButtonEvent(key(ButtonId::Band)));assert(uiView==UiView::EpgNow && dabMode==1 && boots==0);
  finishPaint();const auto count=writes;renderInfoView();assert(writes==count);
  assert(handleUiButtonEvent(key(ButtonId::ChannelUp,ButtonEventType::Repeat)));assert(uiView==UiView::EpgNow);
  assert(!handleUiButtonEvent(key(ButtonId::VolumeUp)));
  handleUiButtonEvent(key(ButtonId::Band,ButtonEventType::LongPress));assert(uiView==UiView::EpgNow);
  handleUiButtonEvent(key(ButtonId::Select));assert(uiView==UiView::EpgList);
  handleUiButtonEvent(key(ButtonId::Select,ButtonEventType::LongPress));assert(uiView==UiView::Menu && uiMenuReturn==UiView::EpgList);
  // CH navigates, VOL edits; short SELECT has no effect on any menu item.
  const auto originalSaves=saves;
  handleUiButtonEvent(key(ButtonId::VolumeDown));assert(uiSettings.brightness==90 && menuSelection==0 && saves==originalSaves);
  handleUiButtonEvent(key(ButtonId::ChannelUp));assert(menuSelection==1);
  handleUiButtonEvent(key(ButtonId::VolumeUp));assert(uiSettings.dimLevel==25 && saves==originalSaves);
  for(unsigned i=1;i<10;++i)handleUiButtonEvent(key(ButtonId::ChannelUp));
  assert(menuSelection==10);handleUiButtonEvent(key(ButtonId::VolumeDown));
  assert(menuModeCandidate==0 && dabMode==1 && boots==0 && !menuModeRequestPending);
  for(unsigned i=0;i<UI_MENU_ITEMS;++i){
    const auto before=uiSettings;const auto selected=menuSelection;const auto mode=menuModeCandidate;
    const auto drawn=writes;
    handleUiButtonEvent(key(ButtonId::Select));handleUiButtonEvent(key(ButtonId::Select,ButtonEventType::Repeat));
    assert(memcmp(&before,&uiSettings,sizeof(before))==0 && menuSelection==selected && menuModeCandidate==mode && writes==drawn);
    assert(saves==originalSaves && boots==0 && !menuModeRequestPending);
    handleUiButtonEvent(key(ButtonId::ChannelUp));
  }
  assert(menuSelection==10);handleUiButtonEvent(key(ButtonId::VolumeUp));assert(menuModeCandidate==1);
  handleUiButtonEvent(key(ButtonId::Select,ButtonEventType::LongPress));
  assert(uiView==UiView::EpgList && saves==originalSaves+1 && dabMode==1 && boots==0);
  auto queueMode=[&](bool otherRow){
    uiBandStarting=false;menuBandCommitPending=false;dabMode=1;uiView=UiView::Text;
    Dab.busyFlag=true;openSettingsMenu();
    for(unsigned i=0;i<10;++i)handleUiButtonEvent(key(ButtonId::ChannelUp));
    handleUiButtonEvent(key(ButtonId::VolumeDown));assert(menuModeCandidate==0);
    handleUiButtonEvent(key(ButtonId::Select));assert(menuModeCandidate==0 && menuSelection==10);
    if(otherRow){handleUiButtonEvent(key(ButtonId::ChannelUp));assert(menuSelection==0);}
    handleUiButtonEvent(key(ButtonId::Select,ButtonEventType::LongPress));
    assert(uiView==UiView::Text && menuModeRequestPending && dabMode==1);
  };
  queueMode(true);assert(boots==0);
  Dab.busyFlag=false;bootAccept=false;serviceMenuModeCommit(hostMillis);
  assert(boots==1 && dabMode==1 && !menuBandCommitPending && uiView==UiView::Text && menuModeRequestStatus==3);
  queueMode(false);Dab.busyFlag=false;bootAccept=true;serviceMenuModeCommit(hostMillis);
  assert(boots==2 && dabMode==0 && menuBandCommitPending && uiBandStarting);
  // Reopening Settings cancels a queued request; timeouts/context changes cannot cause a late boot.
  queueMode(false);openSettingsMenu();Dab.busyFlag=false;serviceMenuModeCommit(hostMillis);
  assert(boots==2 && !menuModeRequestPending);
  hostMillis=0xfffffff0U;queueMode(false);hostMillis+=3000U;serviceMenuModeCommit(hostMillis);
  assert(!menuModeRequestPending && menuModeRequestStatus==2 && boots==2);
  queueMode(false);Dab.generation++;serviceMenuModeCommit(hostMillis);
  assert(!menuModeRequestPending && menuModeRequestStatus==3 && boots==2 && dabMode==1);
  Dab.busyFlag=false;
  // Informational keys never commit a mode, including repeats/forbidden contexts.
  dabMode=0;uiBandStarting=false;uiView=UiView::Text;menuBandCommitPending=false;
  handleUiButtonEvent(key(ButtonId::Band));assert(uiView==UiView::FmRds && boots==2);
  handleUiButtonEvent(key(ButtonId::Band));assert(uiView==UiView::Text);
  Dab.valid=false;handleUiButtonEvent(key(ButtonId::Band));assert(uiView==UiView::FmRds);
  handleUiButtonEvent(key(ButtonId::Band));Dab.valid=true;assert(uiView==UiView::Text);
  handleUiButtonEvent(key(ButtonId::Band,ButtonEventType::LongPress));assert(uiView==UiView::Text);
  handleUiButtonEvent(key(ButtonId::Band));assert(uiView==UiView::FmRds);
  handleUiButtonEvent(key(ButtonId::Band,ButtonEventType::LongPress));assert(uiView==UiView::FmRds);
  handleUiButtonEvent(key(ButtonId::Band));assert(uiView==UiView::Text);
  scanning=true;handleUiButtonEvent(key(ButtonId::Band));assert(uiView==UiView::Text);scanning=false;
  dabMode=1;handleUiButtonEvent(key(ButtonId::Band));Dab.generation++;serviceInfoView(1000);assert(uiView==UiView::Text);
  // Real live Core data is rendered; no UI-owned programme fixtures or green shortcut.
  auto o=object(1,1,0,3,450);auto* c=epg_live::core();directory(*c,{o});select(*c,o);feed(*c,o);c->setClockTrusted(true);
  uiView=UiView::EpgList;invalidateInfoRows();renderInfoView();finishPaint();const auto stable=writes;
  renderInfoView();assert(writes==stable);
  handleUiButtonEvent(key(ButtonId::Select));assert(uiView==UiView::EpgDetail);
  handleUiButtonEvent(key(ButtonId::ChannelUp));assert(uiEpgScroll==1);
  handleUiButtonEvent(key(ButtonId::Select));assert(uiView==UiView::EpgList);
  for(unsigned language=0;language<2;++language)for(unsigned theme=0;theme<3;++theme){uiSettings.language=language;uiSettings.theme=theme;renderInfoView();}
  uiView=UiView::Text;uiInfoPoll=0;uiEpgBadge.invalidate();writes=0;serviceInfoView(2000);assert(writes==2);
  serviceInfoView(2250);assert(writes==2); // no repeated badge writes
  owner.present=false;epg_live::context(owner);serviceInfoView(2500);assert(writes==4);
  dabMode=0;serviceInfoView(2750);assert(writes==4);
  assert(utf8Slice(String("AŽčB"),1,2)=="Žč");
  // Real Controls -> production handler: long BAND is reserved (no action),
  // release-only short opens Now/Next, SELECT opens the list.
  std::fill(hostPins,hostPins+256,HIGH);Controls controls;controls.begin(4,5,34,35,32,16,17);
  c->reset();dabMode=1;uiView=UiView::Text;hostPins[35]=LOW;
  controls.poll(++hostMillis);hostMillis+=25;controls.poll(hostMillis);ButtonEvent event;
  assert(!controls.pop(event));hostMillis+=800;controls.poll(hostMillis);
  assert(controls.pop(event) && event.type==ButtonEventType::LongPress);handleUiButtonEvent(event);
  assert(uiView==UiView::Text);
  hostPins[35]=HIGH;controls.poll(++hostMillis);hostMillis+=25;controls.poll(hostMillis);assert(!controls.pop(event));
  uiView=UiView::Text;hostPins[35]=LOW;controls.poll(++hostMillis);hostMillis+=25;controls.poll(hostMillis);
  hostMillis+=100;hostPins[35]=HIGH;controls.poll(hostMillis);hostMillis+=25;controls.poll(hostMillis);
  assert(controls.pop(event) && event.type==ButtonEventType::ShortPress);handleUiButtonEvent(event);
  assert(uiView==UiView::EpgNow);finishPaint();const auto nowMessage=std::string(uiInfoRows[2].text);
  handleUiButtonEvent(key(ButtonId::Select));assert(uiView==UiView::EpgList);finishPaint();assert(nowMessage!=uiInfoRows[2].text);

  puts("L3 actual UI/menu PASS: live programme list/detail, UTF8, 160x128 bounds, stable partial redraw/badge, BAND modal/long/close, CH isolation, volume pass-through, settings return, candidate/cancel/busy/commit/failure");
}
