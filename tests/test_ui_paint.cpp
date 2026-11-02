// Actual Ui.ino function bodies are appended by run_ui_tests.py. TFT is a spy.
#include "../src/UiTextCache.h"
#include <string>
#include <vector>
#include <cassert>
#include <cstdio>
#define ST77XX_BLACK 0
#define ST77XX_WHITE 0xFFFF
#define ST77XX_GREEN 0x07E0
#define ST77XX_YELLOW 0xFFE0
#define ST77XX_RED 0xF800
#define ST77XX_CYAN 0x07FF
constexpr uint16_t UI_GRAY=0x8410,UI_DARK_LINE=0x3186,UI_ORANGE=0xFD20;
class String:public std::string {
 public: using std::string::string;
  String(const std::string& value):std::string(value) {}
};
enum class UiView { Text,Menu,Slideshow };
UiView uiView=UiView::Text;
ui_paint::ListeningCache uiListeningPaint;
uint8_t dabMode=1,ensemble=0,screenWidth=160,vol=58;
uint16_t stationFM_h=101,stationFM_l=7,currentDABchannel=1,totalDABchannels=46;
uint16_t currentFMchannel=2,totalFMchannels=15;
constexpr uint8_t DAB_FREQS=38;
const char* UI_DAB_CHANNELS[DAB_FREQS]={"5A"};
struct Settings { bool fmAfEnabled=false;uint8_t fmRegion=0; } uiSettings;
struct Af { uint8_t count=0; };
struct Radio {
  bool dabplus=true,valid=true,fmPilot=true,rdsSync=true,tp=false,ta=false;
  uint16_t bitrate=64,samplerate=48000,pi=0x1234;
  uint8_t mode=2,pty=4,progress=95;
  bool receiving=true,available=false;
  Af af;
  uint32_t freq_khz(uint8_t) const { return 174928; }
  bool slideshowReceiving(uint32_t) const { return receiving; }
  bool slideshowAvailable() const { return available; }
  uint8_t slideshowProgress() const { return progress; }
  const Af& fmAfList() const { return af; }
} Dab;
struct Region { bool rbds=false; };
Region fmRegionProfile(uint8_t) { return {}; }
bool fmAfActive() { return false; }
uint32_t millis() { return 100; }
bool scanActive() { return false; }
uint8_t quality=90;
int8_t snr=16,rssi=50;
int8_t uiDisplayedSnr() { return snr; }
int8_t uiDisplayedRssi() { return rssi; }
uint8_t uiDisplayedQuality() { return quality; }
int uiSignalValue(int8_t value) { return value; }
const char* uiSignalUnitName() { return "dBuV"; }
uint16_t uiStationScrollGlyph=0,uiTextScrollGlyph=0;
bool stationPreviewActive=false;
String stationName="Station",uiBroadcastText="Broadcast text";
constexpr uint16_t UI_TEXT_LINE_GLYPHS=26;
String currentUiStationName() { return stationName; }
uint16_t uiStationColor() { return ST77XX_RED; }
unsigned utf8CodepointCount(const String& value) { return value.length(); }
String utf8Slice(const String& value,unsigned start,unsigned count) { return value.substr(start,count); }
String uiCircularTextSlice(unsigned,unsigned) { return uiBroadcastText; }
unsigned utf8TextWidth(const String& value,uint8_t size) { return value.size()*6*size; }
struct Draw { std::string text;int x,y;uint16_t color; };
std::vector<Draw> draws;
std::vector<ui_paint::Rect> rectangles;
struct GFXcanvas1 {
  uint8_t bits[260]={};bool available=true;
  uint8_t* getBuffer() { return available?bits:nullptr; }
  void fillScreen(uint16_t) { memset(bits,0,sizeof(bits)); }
  void setTextWrap(bool) {}
} uiTextCanvas;
unsigned bitmapStarts=0,bitmapEnds=0,lcdClears=0;
std::vector<uint16_t> transferred;
void prepareTftSpi() {}
struct Display {
  void fillRect(int x,int y,int w,int h,uint16_t) {
    ++lcdClears;
    assert(x>=0 && y>=0 && x+w<=160 && y+h<=128 && w>0 && h>0);
    rectangles.push_back({int16_t(x),int16_t(y),int16_t(w),int16_t(h)});
  }
  void drawFastHLine(int,int,int,uint16_t) {}
  void startWrite() { ++bitmapStarts;transferred.clear(); }
  void setAddrWindow(int x,int y,int w,int h) {
    assert(x==0 && y==53 && w==160 && h==13);
    rectangles.push_back({0,53,160,13});
  }
  void writePixels(uint16_t* row,unsigned n) {
    assert(n==160);transferred.insert(transferred.end(),row,row+n);
  }
  void endWrite() { ++bitmapEnds;assert(transferred.size()==2080); }
} tft;
void drawUtf8Text(const String& text,int16_t x,int16_t y,uint16_t color,uint8_t,uint16_t) {
  draws.push_back({text,x,y,color});
}
void drawUtf8TextOn(GFXcanvas1&,const String& text,int16_t x,int16_t y,uint16_t color,uint8_t,uint16_t) {
  assert(color==1);draws.push_back({text,x,y+53,ST77XX_WHITE});
}
// PRODUCTION_FUNCTIONS

static void resetSpy() { draws.clear();rectangles.clear(); }
static void redraw() {
  renderUiHeader();renderUiStationName();renderUiTextArea();renderUiSignal();renderUiStatusStatic();
}
int main() {
  redraw();assert(!draws.empty());resetSpy();
  for(unsigned i=0;i<100;++i) redraw();
  assert(draws.empty() && rectangles.empty());
  ++vol;redraw();assert(draws.size()==1 && draws[0].text=="59");resetSpy();
  ++snr;redraw();assert(draws.size()==1 && draws[0].x==28 && draws[0].y==98);resetSpy();
  ++rssi;redraw();assert(draws.size()==1 && draws[0].x==110 && draws[0].y==98);resetSpy();
  quality=88;redraw();assert(draws.size()==1 && draws[0].text=="88%");resetSpy();
  Dab.progress=96;redraw();assert(draws.size()==1 && draws[0].text=="96%");resetSpy();
  Dab.valid=false;redraw();assert(draws.size()==1 && draws[0].text=="RX");resetSpy();
  Dab.bitrate=128;redraw();assert(draws.size()==1 && draws[0].y==83);resetSpy();
  Dab.receiving=false;redraw();
  assert(draws.size()==2 && draws[0].text=="MOT" && draws[1].text=="--");resetSpy();
  Dab.available=true;redraw();
  assert(draws.size()==2 && draws[1].text=="OK" && draws[1].color==ST77XX_GREEN);resetSpy();
  uiBroadcastText="Changed text";redraw();
  assert(draws.size()==1 && draws[0].y==55 && rectangles.size()==1 && rectangles[0].h==13);resetSpy();
  ++uiTextScrollGlyph;redraw();assert(draws.size()==1 && draws[0].y==55);resetSpy();
  stationName="Changed station";redraw();
  assert(draws.size()==1 && draws[0].y==16 && rectangles[0].h==24);resetSpy();
  uiView=UiView::Menu;redraw();assert(draws.empty());
  uiView=UiView::Text;invalidateListeningPaint();redraw();assert(!draws.empty());resetSpy();
  dabMode=0;invalidateListeningPaint();redraw();resetSpy();redraw();assert(draws.empty());
  uiSettings.fmAfEnabled=true;Dab.af.count=1;redraw();
  assert(draws.size()==1 && draws[0].text=="AF");resetSpy();
  ui_paint::Cell<8> unicode;
  assert(unicode.update(u8"Žď",1,0,0,1,20));
  assert(!unicode.update(u8"Žď",1,0,0,1,20));
  assert(unicode.update(u8"Žď",2,0,0,1,20));
  assert(unicode.update(u8"Žď",2,1,0,1,20));
  assert(unicode.update("too long for cache",2,1,0,1,20) && !unicode.valid);
  // Exact production monochrome-to-RGB transfer: one LCD window, 13 rows,
  // correct row stride/bit order and colour, no black pre-clear.
  resetSpy();memset(uiTextCanvas.bits,0,sizeof(uiTextCanvas.bits));
  uiTextCanvas.bits[0]=0x81;uiTextCanvas.bits[20]=0x40;uiTextCanvas.bits[259]=1;
  const unsigned starts=bitmapStarts,ends=bitmapEnds,clears=lcdClears;
  pushUiTextStrip(0x1234);
  assert(bitmapStarts==starts+1 && bitmapEnds==ends+1 && lcdClears==clears);
  for(unsigned i=0;i<2080;++i)
    assert(transferred[i]==((i==0 || i==7 || i==161 || i==2079)?0x1234:0));
  resetSpy();drawUiTextStrip("buffered",-3,ST77XX_WHITE,160);
  assert(lcdClears==clears && draws.size()==1 && draws[0].x==-3);
  uiTextCanvas.available=false;drawUiTextStrip("fallback",2,ST77XX_WHITE,156);
  assert(lcdClears==clears+1);uiTextCanvas.available=true;
  puts("LCD production render PASS: 100 unchanged frames=0 writes; independent VOL/CNR/RSSI/FIC/MOT/audio/AF; modal and full invalidation; UTF8/style/geometry");
  puts("LCD buffered text PASS: 260-byte mono source, exact 2080-pixel transfer, single window, no pre-clear, OOM fallback");
}
