// Actual application functions inserted by run_audio_tests.py.
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <deque>
#include <utility>
enum class RadioOperation { None,BandBoot,FmTune,FmSeek,DabTune,DabService };
enum class UiView { Text,StationList };
bool uiBandStarting=true,uiBandTunePending=false,uiInitialTuneDeferred=false;
bool uiDefaultSlideshowPending=false,openListAfterBandReady=false;
bool menuBandCommitPending=false;
uint8_t menuModePrevious=1;
unsigned modeSaves=0;
void saveModeToEEPROM(uint8_t){++modeSaves;}
uint32_t uiInitialTuneDeadlineMs=0,clockMs=100;
uint8_t dabMode=1,totalDABchannels=46,totalFMchannels=15,currentFMchannel=2,vol=52,uiDirtyFlags=0;
constexpr uint8_t UI_DIRTY_NONE=0;
UiView uiView=UiView::Text;
unsigned redraws=0,tunes=0;
struct Radio {
  bool bandReady=false,idle=false,accept=true;
  unsigned PartNo=4684,VerMajor=6,VerMinor=0,VerBuild=9;
  std::deque<std::pair<RadioOperation,bool>> results;
  bool ready() const { return idle; }
  bool takeBandReady() { bool b=bandReady;bandReady=false;return b; }
  void requestVolume(unsigned) {}
  bool takeOperationResult(RadioOperation& op,bool& ok) {
    if(results.empty()) return false;
    op=results.front().first;ok=results.front().second;results.pop_front();return true;
  }
} Dab;
struct Serial { template<class... T> void printf(const char*,T...) {} void println(const char*) {} } serialMonitor;
uint32_t millis() { return clockMs; }
bool scanActive() { return false; }
bool handleScanRadioResult(RadioOperation,bool) { return false; }
bool handleFmAfRadioResult(RadioOperation,bool) { return false; }
void clearScreen() {}
void Message_red(const char*,int) {}
void renderCurrentUiView() { ++redraws; }
void showSi4684FirmwareVersion(unsigned) {}
void openStationList() {}
void showNoStationsForCurrentBand() { uiBandStarting=false; }
bool DAB_SetChannel() { ++tunes;return Dab.accept; }
bool FMsetChannel(uint8_t,bool) { ++tunes;return Dab.accept; }
// PRODUCTION_FUNCTIONS
int main() {
  Dab.bandReady=true;Dab.results.push_back({RadioOperation::BandBoot,true});
  handleRadioEvents();assert(uiInitialTuneDeferred && tunes==0 && uiBandStarting);
  clockMs+=2999;handleRadioEvents();assert(uiInitialTuneDeferred && tunes==0);
  Dab.idle=true;handleRadioEvents();assert(tunes==1 && uiBandTunePending && !uiInitialTuneDeferred);
  Dab.results.push_back({RadioOperation::DabService,true});handleRadioEvents();
  assert(!uiBandStarting && !uiBandTunePending && redraws==1);
  // Deferred deadline expires even when a background command remains busy.
  uiBandStarting=true;Dab.bandReady=true;Dab.idle=false;handleRadioEvents();
  clockMs+=3000;handleRadioEvents();assert(!uiInitialTuneDeferred && !uiBandStarting && redraws==2);
  Dab.idle=true;handleRadioEvents();assert(tunes==1); // no late unwanted restore
  // A completed START failure releases UI for background supervisor retries.
  uiBandStarting=true;uiBandTunePending=true;Dab.results.push_back({RadioOperation::DabService,false});
  handleRadioEvents();assert(!uiBandStarting && !uiBandTunePending && redraws==3);
  // Deadline wraps safely; failure to accept is still bounded.
  clockMs=0xFFFFFF00U;uiBandStarting=true;Dab.bandReady=true;Dab.idle=false;handleRadioEvents();
  clockMs+=3000;handleRadioEvents();assert(!uiBandStarting && !uiInitialTuneDeferred);
  menuBandCommitPending=true;dabMode=0;uiBandStarting=true;Dab.bandReady=true;
  handleRadioEvents();assert(modeSaves==1 && !menuBandCommitPending && dabMode==0);
  menuBandCommitPending=true;menuModePrevious=1;uiBandStarting=true;
  Dab.results.push_back({RadioOperation::BandBoot,false});handleRadioEvents();
  assert(modeSaves==1 && dabMode==1 && !menuBandCommitPending && !uiBandStarting);
  puts("Application boot PASS: actual band-ready/deferred/result functions, 3s busy deadline, late acceptance blocked, UI release, millis wrap");
}
