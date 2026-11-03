// Production UI functions inserted below; actual DAB/MOT driver linked.
#include "si4684.h"
#include <SPI.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
uint32_t hostMillis=100;int hostPins[256];std::vector<HostEvent> hostEvents;
HostSPI SPI;HostESP ESP;unsigned hostHeapAllocations=0;
DAB Dab;
enum class UiView { Text,Menu,StationList,Slideshow,EpgNow,EpgList,EpgDetail };
UiView uiView=UiView::Text;
uint8_t dabMode=1,screenWidth=160,screenHeight=128;
bool uiSlideshowDecodePending=false,scanning=false;
constexpr uint8_t UI_DIRTY_STATUS=1;
constexpr uint16_t ST77XX_YELLOW=0xFFE0,ST77XX_CYAN=0x7FF;
struct Settings { uint8_t slideshowMode=2; } uiSettings;
struct Serial:Stream { template<class... A> void printf(const char*,A...) {} } serialMonitor;
struct Tft {} tft;
unsigned renders=0,clears=0,listening=0,writes=0;
uint8_t visibleTag=0,decodeBehavior=0;
bool injectDuringRender=false;
void prepareTftSpi() {}
void clearScreen() { ++clears; }
void drawUtf8Text(const std::string&,int,int,uint16_t,int,unsigned) {}
bool scanActive() { return scanning; }
void markUiDirty(uint8_t) {}
void renderListeningScreen() { ++listening;uiView=UiView::Text;visibleTag=0; }
void renderCurrentUiView() { renderListeningScreen(); }
struct DabHostAccess {
  static constexpr uint32_t sid=0x2AA1,cid=10;
  static void reset() {
    std::fill(hostPins,hostPins+256,HIGH);Dab.configurePins(13,26,14,2);Dab.configureSharedSpiPeer(12);
    Dab.resetSlideshowAssembler(true);Dab._activeServiceValid=true;Dab._band=0;
    Dab._activeServiceId=sid;Dab._activeComponentId=cid;Dab._serviceTransitionPending=false;
    ++Dab._generation;Dab._slideshowCompletedTransportValid=false;
    Dab._slideshowLastImageLength=Dab._slideshowImageHash=0;Dab._slideshowRenderToken=0;
    Dab.setSlideshowEnabled(true);
    uiView=UiView::Text;uiSlideshowDecodePending=scanning=false;
    renders=clears=listening=writes=0;visibleTag=decodeBehavior=0;injectDuringRender=false;
  }
  static void packet(uint8_t type,uint32_t tid,unsigned segment,bool last,const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> p(11+bytes.size(),0);p[0]=type;
    p[2]=uint8_t((segment>>8)|(last?0x80:0));p[3]=uint8_t(segment);
    p[4]=uint8_t(tid>>16);p[5]=uint8_t(tid>>8);p[6]=uint8_t(tid);
    p[7]=uint8_t(bytes.size()>>8);p[8]=uint8_t(bytes.size());std::copy(bytes.begin(),bytes.end(),p.begin()+9);
    si468x::DsrvHeader h;h.serviceId=sid;h.componentId=cid;h.dscType=60;
    Dab.processMotPacket(h,p.data(),uint16_t(p.size()));
  }
  static void header(uint32_t tid) { packet(0x73,tid,0,true,{0,0,4,0,3,0x80,0}); }
  static void body(uint32_t tid,unsigned segment,uint8_t tag) {
    std::vector<uint8_t> bytes(32,tag);
    if(segment==0) {
      const uint8_t png[]={137,80,78,71,13,10,26,10};std::copy(png,png+8,bytes.begin());
    }
    packet(0x74,tid,segment,segment==1,bytes);
  }
  static void image(uint32_t tid,uint8_t tag) { header(tid);body(tid,0,tag);body(tid,1,tag); }
  static bool owned() { return Dab._slideshowRenderToken!=0; }
};
bool renderRamSlideshow(Tft&,const uint8_t* data,uint32_t size,int,int,int,int,Stream*,bool* modified) {
  assert(DabHostAccess::owned() && data && size==64);++renders;
  const std::vector<uint8_t> snapshot(data,data+size);
  if(injectDuringRender) {
    DabHostAccess::header(999);DabHostAccess::body(999,1,99);
    assert(memcmp(data,snapshot.data(),size)==0);
  }
  *modified=decodeBehavior!=1;
  if(*modified) { ++writes;visibleTag=data[8]; }
  return decodeBehavior==0;
}
// PRODUCTION_FUNCTIONS
int main() {
  for(auto view:{UiView::EpgNow,UiView::EpgList,UiView::EpgDetail}){
    DabHostAccess::reset();uiSettings.slideshowMode=2;uiView=view;
    DabHostAccess::image(101,21);serviceUiSlideshow();
    assert(uiView==view && renders==0 && uiSlideshowDecodePending && !DabHostAccess::owned());
    DabHostAccess::image(102,22);serviceUiSlideshow();assert(uiView==view && renders==0);
    uiView=UiView::Text;serviceUiSlideshow();assert(visibleTag==22 && renders==1);
  }
  for(uint8_t mode:{1,2}) {
    DabHostAccess::reset();uiSettings.slideshowMode=mode;
    DabHostAccess::image(1,1);serviceUiSlideshow();
    assert(!DabHostAccess::owned() && Dab.slideshowAvailable());
    assert(renders==(mode==2?1U:0U));
    DabHostAccess::body(2,1,2);assert(!Dab.slideshowAvailable() && Dab.slideshowProgress()==50);
    if(mode==1) { assert(renderSlideshowScreen());assert(uiSlideshowDecodePending); }
    const auto oldTag=visibleTag;
    serviceUiSlideshow();assert(visibleTag==oldTag);
    DabHostAccess::header(2);DabHostAccess::body(2,0,2);serviceUiSlideshow();
    assert(visibleTag==2 && !DabHostAccess::owned());
    uiView=UiView::Text;const auto count=renders;
    DabHostAccess::image(3,3);serviceUiSlideshow();
    assert(renders==count+(mode==2?1:0));
    uiView=UiView::Menu;DabHostAccess::image(4,4);serviceUiSlideshow();
    assert(!DabHostAccess::owned());
    DabHostAccess::image(5,5);serviceUiSlideshow();uiView=UiView::Text;serviceUiSlideshow();
    if(mode==1) assert(renderSlideshowScreen());
    assert(visibleTag==5);
    scanning=true;DabHostAccess::image(6,6);serviceUiSlideshow();
    assert(!uiSlideshowDecodePending && !DabHostAccess::owned());scanning=false;
  }
  DabHostAccess::reset();uiSettings.slideshowMode=2;
  DabHostAccess::image(10,10);injectDuringRender=true;serviceUiSlideshow();injectDuringRender=false;
  assert(visibleTag==10 && !DabHostAccess::owned());
  const auto before=writes;decodeBehavior=1;
  DabHostAccess::image(11,11);serviceUiSlideshow();
  assert(visibleTag==10 && writes==before && !Dab.slideshowAvailable() && !DabHostAccess::owned());
  decodeBehavior=0;DabHostAccess::image(12,12);serviceUiSlideshow();assert(visibleTag==12);
  // OFF neither renders nor holds ownership; ON starts a clean new image.
  uiSettings.slideshowMode=0;Dab.setSlideshowEnabled(false);DabHostAccess::image(13,13);serviceUiSlideshow();
  assert(!Dab.slideshowAvailable() && !DabHostAccess::owned());
  uiSettings.slideshowMode=2;Dab.setSlideshowEnabled(true);DabHostAccess::image(14,14);serviceUiSlideshow();
  assert(visibleTag==14);
  // Notification loss and modal delays do not pin the compressed arena.
  uiView=UiView::Text;DabHostAccess::image(15,15);DabHostAccess::image(16,16);serviceUiSlideshow();
  assert(visibleTag==16 && !DabHostAccess::owned());
  decodeBehavior=2;DabHostAccess::image(17,17);serviceUiSlideshow();
  assert(listening>0 && !DabHostAccess::owned());
  decodeBehavior=0;DabHostAccess::image(18,18);serviceUiSlideshow();assert(visibleTag==18);
  DabHostAccess::reset();uiSettings.slideshowMode=1;
  DabHostAccess::image(20,7);serviceUiSlideshow(); // manual cache, not displayed
  DabHostAccess::body(21,1,7);assert(renderSlideshowScreen());serviceUiSlideshow();
  assert(uiSlideshowDecodePending && !Dab.slideshowAvailable());
  DabHostAccess::header(21);DabHostAccess::body(21,0,7);serviceUiSlideshow();
  assert(visibleTag==7 && renders==1); // identical B must satisfy explicit open
  puts("SLS production UI + driver PASS: Manual/Auto/Off, A/B/C, partial reopen, modal return, scan, lease, decode reject/failure/recovery, lost notification");
}
