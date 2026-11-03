// Production DSRV -> ownership -> IRAM adapter -> Core -> station programme view.
#include "si4684.h"
#include "epg_test_data.h"
#include <SPI.h>
#include <cstdlib>
uint32_t hostMillis=100;
int hostPins[256];std::vector<HostEvent> hostEvents;HostSPI SPI;HostESP ESP;
unsigned hostHeapAllocations=0;
static int failAt=-1,allocations=0,releases=0;
void* epgLiveAllocate(size_t n){return allocations++==failAt?nullptr:malloc(n);}
void epgLiveRelease(void* p){++releases;free(p);}
struct DabHostAccess {
  DAB radio;
  bool pad;
  explicit DabHostAccess(bool usePad):pad(usePad){
    std::fill(hostPins,hostPins+256,HIGH);radio.configurePins(13,26,14,2);radio.configureSharedSpiPeer(12);
    radio._band=0;radio._state=DAB::State::Ready;radio.valid=true;radio._generation=1;
    radio._activeServiceValid=true;radio._activeServiceId=radio._serviceId=0x3201;
    radio._activeComponentId=radio._componentId=5;
    radio.EnsembleID=0x3153;radio.ECC=0xe2;radio._operation=RadioOperation::None;
    radio._volumePending=false;radio.resetPeriodicDeadlines(hostMillis);
    radio.setEpgDiscoveryEnabled(true);
    Data list(8+2*28);si468x::writeLe16(list.data(),uint16_t(list.size()-2));list[4]=2;
    si468x::writeLe32(list.data()+8,0x3201);list[13]=1;si468x::writeLe32(list.data()+32,0x01fe0005);
    si468x::writeLe32(list.data()+36,0xe23153fe);list[41]=1;si468x::writeLe32(list.data()+60,0x01f2c00c);
    assert(radio._epgDiscovery.inventory(list.data(),list.size(),hostMillis));
    const uint8_t spi[]={7,0,0,0};
    for(unsigned i=0;i<2;++i){hostMillis+=250;assert(radio._epgDiscovery.issue(hostMillis));
      const bool yes=(i==0)==pad;
      assert(radio._epgDiscovery.commit(radio._epgDiscovery.generation(),yes?spi:nullptr,yes?4:0,yes?1:0,true,hostMillis));}
    if(!pad){radio._activeDataServiceValid=true;radio._epgDataOwner=true;radio._activeDataServiceId=0xe23153fe;radio._activeDataComponentId=0xc00c;}
    radio._epgClock.valid=radio._epgClock.offsetKnown=true;
    radio._epgClock.encoding=epg::ClockEncoding::UTC;
    radio._epgClock.calendarEpoch=today+3660;radio._epgClock.sampledAtMonotonicMs=hostMillis;
    radio._epgClock.generation=1;
    radio.serviceEpg(hostMillis);
  }
  void send(const Data& bytes,bool foreign=false,bool corrupt=false,bool stale=false,bool tagged=true){
    Data response(24+bytes.size());response[0]=0x80;response[4]=1;
    response[7]=uint8_t((pad?0x40:0)|60);
    si468x::writeLe32(response.data()+8,foreign?0x9999:pad?0x3201:0xe23153fe);
    si468x::writeLe32(response.data()+12,pad?5:0xc00c);
    si468x::writeLe16(response.data()+16,pad && tagged?7:0);
    si468x::writeLe16(response.data()+18,uint16_t(bytes.size()));
    std::copy(bytes.begin(),bytes.end(),response.begin()+24);
    if(corrupt)response[4]|=4;
    memcpy(radio._workspace,response.data(),24);SPI.reply(response);
    if(stale){radio._state=DAB::State::DsrvCommand;radio._commandGeneration=0;radio.commandCompleted();}
    else radio.processDsrvReply();
    for(unsigned i=0;i<1000;++i)epg_live::pump(hostMillis);
    ++hostMillis;
  }
  void loadDirectory(const std::vector<Object>& objects){
    Data out(13);out[5]=uint8_t(objects.size());
    for(const auto& o:objects){
      append(out,number(o.tid,2));Data h(7);const size_t n=o.body.size(),hs=15;
      h[0]=uint8_t(n>>20);h[1]=uint8_t(n>>12);h[2]=uint8_t(n>>4);h[3]=uint8_t(n<<4);
      h[4]=uint8_t(hs>>1);h[5]=uint8_t((hs&1)*128+14);h[6]=o.body[0]==3?0:1;
      h.push_back(0xe7);h.push_back(6);append(h,number(o.bearer,6));append(out,h);
    }
    const auto size=number(out.size(),4);std::copy(size.begin(),size.end(),out.begin());
    send(group(1,6,0,true,out));
  }
  void loadDirectory(const Object& o){loadDirectory(std::vector<Object>{o});}
  void slsPartial(){
    radio.setSlideshowEnabled(true);
    auto sendSls=[&](Data bytes){
      Data response(24+bytes.size());response[0]=0x80;response[4]=1;response[7]=0x7c;
      si468x::writeLe32(response.data()+8,0x3201);si468x::writeLe32(response.data()+12,5);
      si468x::writeLe16(response.data()+16,2);si468x::writeLe16(response.data()+18,uint16_t(bytes.size()));
      std::copy(bytes.begin(),bytes.end(),response.begin()+24);memcpy(radio._workspace,response.data(),24);
      SPI.reply(response);radio.processDsrvReply();
    };
    sendSls({0x73,0,0x80,0,0x12,0x34,0x56,0,7,0,0,4,0,3,0x80,0,0,0});
    Data bytes(43,17);bytes[0]=0x74;bytes[1]=bytes[2]=bytes[3]=0;
    bytes[4]=0x12;bytes[5]=0x34;bytes[6]=0x56;bytes[7]=0;bytes[8]=32;
    const uint8_t png[]={137,80,78,71,13,10,26,10};std::copy(png,png+8,bytes.begin()+9);
    sendSls(bytes);assert(radio.slideshowCollecting() && radio._slideshowReceivedBytes==32);
  }
  void unchangedSls(){assert(radio.slideshowCollecting() && radio._slideshowReceivedBytes==32 && radio._slideshowTransportId==0x123456);}
  void body(const Object& o){
    std::vector<Data> parts;
    for(size_t pos=0;pos<o.body.size();pos+=64){const size_t n=std::min(size_t(64),o.body.size()-pos);
      parts.push_back(group(o.tid,4,unsigned(pos/64),pos+n==o.body.size(),Data(o.body.begin()+pos,o.body.begin()+pos+n)));}
    for(size_t i=parts.size();i-->0;)send(parts[i]);
  }
  void cleanupFailureTest(){
    radio._slideshowPropertyPending=false;radio._dataServiceStopPending=true;radio._epgDataOwner=true;
    radio._activeDataServiceValid=true;radio._activeDataServiceId=0xe23153fe;radio._activeDataComponentId=0xc00c;
    radio._dabSwitch.stopRetries=dab_switch::MAX_STOP_RETRIES;radio._consecutiveCtsTimeouts=2;
    radio.startDabStopDataCommand();SPI.reply({0,0,0,0});
    si468x::Result result=si468x::Result::Pending;
    for(unsigned i=0;i<1100 && result==si468x::Result::Pending;++i){++hostMillis;result=radio._radio.service();}
    assert(result==si468x::Result::Timeout);radio._commandPending=false;radio.commandCompleted();
    assert(radio._epgCleanupFailed && !radio._dataServiceStopPending && radio._activeServiceValid &&
      radio._activeDataServiceValid && radio._consecutiveCtsTimeouts==2 && radio._state==DAB::State::Ready);
    radio.serviceEpg(hostMillis);radio.scheduleReadyWork(hostMillis);
    assert(!radio._dataServiceStopPending && radio._state==DAB::State::Ready);
  }
  void pairedTimeTest(){
    radio._epgClock=epg::BroadcastClockSample{};radio._epgUtcPending=true;
    auto reply=[&](uint8_t hour){
      const uint8_t type=radio._epgUtcPending?1:0;
      assert(radio.startRaw(si468x::Command::DAB_GET_TIME,&type,1,DAB::State::DabTimeCommand,100000UL,11));
      Data bytes(11);bytes[0]=0x80;si468x::writeLe16(bytes.data()+4,2024);bytes[6]=11;bytes[7]=12;bytes[8]=hour;bytes[9]=1;
      SPI.script({bytes,bytes});hostMillis+=2;assert(radio._radio.service()==si468x::Result::Ok);
      radio._commandPending=false;radio.commandCompleted();
    };
    reply(1);assert(!radio._epgClock.valid && radio._epgLocalPending);
    reply(2);assert(radio._epgClock.valid && radio._epgClock.offsetMinutes==60 && radio.Hours==2);
    radio.serviceEpg(hostMillis);epg_live::pump(hostMillis);
    assert(epg_live::core()->clockTrusted() && epg_live::core()->offset()==60);
  }
  void packetStartTest(){
    if(pad)return;
    radio._activeDataServiceValid=false;radio._epgDataOwner=false;radio._dataServicePending=false;
    radio._slideshowPropertyPending=false;radio._epgStartNotBefore=0;radio._lastLowPriorityCommandMs=0;
    radio.scheduleReadyWork(hostMillis);
    assert(radio._state==DAB::State::EpgStartCommand && radio._commandPending);
    assert(SPI.current[0]==0x81 && si468x::readLe32(SPI.current.data()+4)==0xe23153fe &&
      si468x::readLe32(SPI.current.data()+8)==0xc00c);
    SPI.reply({0x80,0,0,0});hostMillis+=2;assert(radio._radio.service()==si468x::Result::Ok);
    radio._commandPending=false;radio.commandCompleted();
    assert(radio._activeDataServiceValid && radio._epgDataOwner && radio._activeServiceValid);
    radio.serviceEpg(hostMillis);
  }
  void revoke(){radio._epgDiscovery.reset();radio.serviceEpg(hostMillis);}
  void change(){++radio._generation;radio.serviceEpg(hostMillis);}
  void clockUnknown(){radio._epgClock.valid=false;radio.serviceEpg(hostMillis);epg_live::pump(hostMillis);}
  void refreshProof(){hostMillis+=61000;radio._epgDiscovery.refresh(hostMillis);radio.serviceEpg(hostMillis);epg_live::pump(hostMillis);}
  void ageCache(){
    hostMillis+=900001;radio._epgClock.calendarEpoch=today+4560;
    radio._epgClock.sampledAtMonotonicMs=hostMillis;
    radio.serviceEpg(hostMillis);epg_live::pump(hostMillis);
  }
  void starveTest(){
    radio._slideshowPropertyPending=false;
    radio._epgDiscovery.reset();
    Data list(36);si468x::writeLe16(list.data(),34);list[4]=1;si468x::writeLe32(list.data()+8,0x3201);list[13]=1;
    si468x::writeLe32(list.data()+32,5);assert(radio._epgDiscovery.inventory(list.data(),list.size(),hostMillis));
    hostMillis+=1500;radio._dsrvPending=true;radio._dsrvBurstCount=4;radio._lastLowPriorityCommandMs=0;
    radio.scheduleReadyWork(hostMillis);assert(radio._state==DAB::State::EpgComponentCommand);
    assert(SPI.current[0]==0xbb);(void)radio._radio.abortCommand();radio._commandPending=false;radio._state=DAB::State::Ready;
  }
};
int main(int argc,char** argv){
  if(argc>1)failAt=atoi(argv[1]);
  if(failAt>=0){assert(!epg_live::ensure() && epg_live::oom());assert(releases==failAt);const int calls=allocations;
    assert(!epg_live::ensure() && calls==allocations);
    DabHostAccess radio(true);radio.slsPartial();radio.send(group(1,6,0,true,{0}));radio.unchangedSls();
    assert(epg_live::indicator()==epg_live::Indicator::Yellow && !epg_live::core());
    puts("Live allocation failure cleanup/latch + unchanged SLS/audio ownership, never green PASS");return 0;}
  assert(epg_live::ensure());const int calls=allocations;
#if !EPG_DISCOVERY_ENABLE
  DabHostAccess disabled(true);disabled.send(group(1,6,0,true,{0}));
  assert(epg_live::indicator()==epg_live::Indicator::Grey && !epg_live::core()->count());
  puts("Live L2 + discovery OFF PASS: no usable source or green programme");return 0;
#endif
  for(bool pad:{false,true}){
    DabHostAccess h(pad);assert(epg_live::indicator()==epg_live::Indicator::Yellow);
    h.packetStartTest();
    auto o=object(1,1,0,3,160);
    Object si{0x777,o.bearer,today,tlv(3,tlv(0x28,tlv(0x29,tlv(0x80,number(o.bearer,6)))))};
    Data service=tlv(0x29,tlv(0x80,number(o.bearer,6)));append(service,tlv(0x12,tlv(1,str("Station live SI"))));
    si.body=tlv(3,tlv(0x28,service));
    h.slsPartial();h.loadDirectory(std::vector<Object>{o,si});h.body(si);
    assert(epg_live::core()->stats.si==1 && epg_live::core()->station());
    assert(epg_live::indicator()==epg_live::Indicator::Yellow && epg_live::core()->selected()==o.bearer);
    h.body(o);auto* c=epg_live::core();
    assert(c->count()==3 && c->current()>=0 && c->next()>=0);h.unchangedSls();
    assert(epg_live::indicator()==epg_live::Indicator::Green);
    assert(std::string(c->title(0)).find("station 1")!=std::string::npos);
    h.refreshProof();assert(epg_live::indicator()==epg_live::Indicator::Green && c->count()==3);
    h.ageCache();assert(c->clockTrusted() && epg_live::indicator()==epg_live::Indicator::Yellow);
    h.send(group(o.tid,4,0,false,Data(o.body.begin(),o.body.begin()+64)));
    assert(epg_live::indicator()==epg_live::Indicator::Yellow); // no fragment-based cache refresh
    h.body(o);assert(epg_live::indicator()==epg_live::Indicator::Green);h.unchangedSls();
    const auto routed=epg_live::stats().routed;
    const auto bad=group(o.tid,4,0,true,{0});h.send(bad,true);h.send(bad,false,true);h.send(bad,false,false,true);
    assert(epg_live::stats().routed==routed && c->count()==3);
    auto crcBad=bad;crcBad.back()^=1;h.send(crcBad);assert(c->stats.crcBad && c->count()==3);
    assert(epg_live::indicator()==epg_live::Indicator::Green);
    h.clockUnknown();assert(epg_live::indicator()==epg_live::Indicator::Yellow && c->current()==-1);
    h.change();assert(epg_live::indicator()==epg_live::Indicator::Yellow && !c->count());
    auto other=object(2,2,0,3);h.loadDirectory(other);h.body(other);
    assert(epg_live::indicator()==epg_live::Indicator::Yellow && !c->count());
    h.revoke();assert(epg_live::indicator()==epg_live::Indicator::Grey);
    h.starveTest();
  }
  {DabHostAccess cleanup(false);cleanup.cleanupFailureTest();}
  {DabHostAccess clock(false);clock.pairedTimeTest();}
  assert(allocations==calls);
  puts("Live driver packet/PAD + unchanged partial SLS -> directory -> SI/PI -> selected station Now/Next PASS; CRC/foreign/old generation/clock/revoke/OOM/UA quota/no RX allocations");
}
