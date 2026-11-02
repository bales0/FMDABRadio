// Synthetic timings, real driver and command engine. No RF performance claim.
#include "si4684.h"
#include <SPI.h>
#include <deque>
#include <cassert>
#include <cstdio>
#include <vector>
uint32_t hostMillis=100;int hostPins[256];std::vector<HostEvent> hostEvents;
HostSPI SPI;HostESP ESP;unsigned hostHeapAllocations=0;
#ifndef FMDAB_BENCH_L1
#define FMDAB_BENCH_L1 1
#endif
struct Item { uint32_t at;std::vector<uint8_t> packet; };
struct DabHostAccess {
  static constexpr uint32_t sid=0x2FDA,cid=0x2C;
  DAB r;std::deque<Item> events;std::deque<std::vector<uint8_t>> queue;
  std::vector<uint8_t> image;
  unsigned deferred=0,reads=0,ua=0,completed=0,duplicates=0;
  uint32_t first=0,last=0,drainStart=0,drainEnd=0,maxPending=0,activeDrain=0;
  uint32_t hash=0,lastCompletedTid=0;bool retained=true;
  uint32_t firstGetAt=0,lastGetAt=0,maxGetGap=0,maxHintToGet=0;
  uint32_t observedTid=0,objectStart=0,lastObjectMs=0;
  DabHostAccess(unsigned size) : image(size) {
    std::fill(hostPins,hostPins+256,HIGH);SPI.reply({0x80,0,0,0});SPI.transactions.clear();
    r.configurePins(13,26,14,2);r.configureSharedSpiPeer(12);r.setSlideshowEnabled(true);
    r._band=0;r._state=DAB::State::Ready;r._volumePending=false;
    r._activeServiceValid=true;r._activeServiceId=sid;r._activeComponentId=cid;
    r._serviceId=sid;r._componentId=cid;r._dabTuneTarget=0;r.freq_index=0;r.valid=true;
    r.resetPeriodicDeadlines(hostMillis);r._dabSignalRefreshPending=true;r._dabAudioRefreshPending=true;
    r._dabAudioInfoValid=true;
#if FMDAB_BENCH_L1
    r._epgDiscoveryEnabled=EPG_DISCOVERY_ENABLE;
    // Exactly 17 component UA replies, no payload/EPG acquisition.
    std::vector<uint8_t> list(8+17*28,0);si468x::writeLe16(list.data(),uint16_t(list.size()-2));list[4]=17;
    for(unsigned i=0;i<17;++i) {
      si468x::writeLe32(list.data()+8+28*i,0x2000+i);list[8+28*i+5]=1;
      si468x::writeLe32(list.data()+8+28*i+24,0x01020000U+i+1);
    }
    assert(r._epgDiscovery.inventory(list.data(),list.size(),hostMillis));
#ifdef FMDAB_BENCH_CURRENT
    r._audioActivation.request(hostMillis);r._audioActivation.verified();
#endif
#endif
    for(unsigned i=0;i<size;++i) image[i]=uint8_t(i*31U+i/17U);
    image[0]=0xFF;image[1]=0xD8;image[2]=0xFF;image[size-2]=0xFF;image[size-1]=0xD9;
  }
  std::vector<uint8_t> packet(unsigned type,unsigned tid,unsigned segment,bool last,
                              const std::vector<uint8_t>& data) {
    std::vector<uint8_t> p(11+data.size(),0);p[0]=uint8_t(type);
    const unsigned field=segment|(last?0x8000U:0);
    p[2]=uint8_t(field>>8);p[3]=uint8_t(field);p[4]=uint8_t(tid>>16);p[5]=uint8_t(tid>>8);p[6]=uint8_t(tid);
    p[7]=uint8_t(data.size()>>8);p[8]=uint8_t(data.size());std::copy(data.begin(),data.end(),p.begin()+9);return p;
  }
  std::vector<uint8_t> body(unsigned tid,unsigned index) {
    const unsigned offset=index*512U,n=std::min(512U,unsigned(image.size())-offset);
    return packet(0x74,tid,index,offset+n==image.size(),
                  std::vector<uint8_t>(image.begin()+offset,image.begin()+offset+n));
  }
  void carousel(unsigned tid,uint32_t at,uint32_t gap=0,bool reverse=false) {
    const unsigned count=(unsigned(image.size())+511)/512;
    events.push_back({at,body(tid,count-1)}); // legitimate LAST-first entry
    std::vector<uint8_t> h(7,0);const unsigned size=unsigned(image.size());
    h[0]=uint8_t(size>>20);h[1]=uint8_t(size>>12);h[2]=uint8_t(size>>4);h[3]=uint8_t(size<<4);h[4]=3;h[5]=0x80;
    events.push_back({at,packet(0x73,tid,0,true,h)});
    auto broken=body(tid,0);broken.pop_back();events.push_back({at,broken});
    for(unsigned step=0;step<count-1;++step) {
      const unsigned i=reverse?count-2-step:step;
      if(gap && i==7) continue;
      events.push_back({at,body(tid,i)});
      if(i==0) { events.push_back({at,body(tid,i)});++duplicates; }
    }
    if(gap) events.push_back({at+gap,body(tid,7)});
  }
  std::vector<uint8_t> frame() {
    const uint8_t status=queue.empty()?0x80:0x90;
    if(r._state==DAB::State::DsrvCommand) {
      assert(!queue.empty());const auto& p=queue.front();
      std::vector<uint8_t> f(24+p.size(),0);f[0]=queue.size()==1?0x80:0x90;
      f[4]=1;f[5]=uint8_t(std::min(size_t(255),queue.size()-1));f[7]=0x7C;
      si468x::writeLe32(f.data()+8,sid);si468x::writeLe32(f.data()+12,cid);
      si468x::writeLe16(f.data()+18,uint16_t(p.size()));std::copy(p.begin(),p.end(),f.begin()+24);
      return f;
    }
    std::vector<uint8_t> f(32,0);f[0]=status;
    if(r._state==DAB::State::DabSignalCommand) {
      f[5]=5;f[8]=100;si468x::writeLe32(f.data()+12,dab_freq[0]);
    }
    if(r._state==DAB::State::DabAudioCommand) {
      si468x::writeLe16(f.data()+4,48);si468x::writeLe16(f.data()+6,48000);
    }
    return f;
  }
  void run(const char* name,bool irq,uint32_t pause=0,bool blocking=false) {
    uint32_t finish=events.back().at+6000,commandDue=0,pendingSince=0;
    bool paused=false,previousQueue=false;uint32_t previousTime=hostMillis;
    for(;hostMillis<finish;++hostMillis) {
      if(blocking && !paused && hostMillis>=110) { hostMillis+=421;paused=true; }
      if(previousQueue) activeDrain+=hostMillis-previousTime;
      previousTime=hostMillis;
      const bool empty=queue.empty();
      while(!events.empty() && events.front().at<=hostMillis) {
        queue.push_back(events.front().packet);events.pop_front();
      }
      if(empty && !queue.empty()) {
        pendingSince=hostMillis;if(!drainStart) drainStart=hostMillis;
        if(irq) hostInterruptHandler()(); // actual ISR -> status callback
      }
      hostPins[26]=queue.empty()?HIGH:LOW;
      if(r._commandPending && commandDue==0) commandDue=hostMillis+(r._state==DAB::State::DsrvCommand?3:2);
      if(r._commandPending && hostMillis<commandDue) SPI.script({{0,0,0,0}});
      else {
        auto f=frame();SPI.script({f,f,f,f});
        if(!queue.empty() && r._state==DAB::State::DsrvCommand && pendingSince)
          maxPending=std::max(maxPending,hostMillis-pendingSince);
      }
      const auto beforeReads=r._dsrvPackets;
      const auto beforeState=r._state;
      const bool pending=r._commandPending;
      const auto beforeTransactions=SPI.transactions.size();
      r.task();
      bool issued=false;
      for(size_t i=beforeTransactions;i<SPI.transactions.size();++i)
        if(!SPI.transactions[i].empty() && SPI.transactions[i][0]!=0) {
          issued=true;
          if(SPI.transactions[i][0]==0x84) {
            if(!firstGetAt) firstGetAt=hostMillis;
            if(lastGetAt) maxGetGap=std::max(maxGetGap,hostMillis-lastGetAt);
            else maxHintToGet=std::max(maxHintToGet,hostMillis-pendingSince);
            lastGetAt=hostMillis;
          }
        }
      const bool consumed=r._dsrvPackets!=beforeReads;
      if(consumed) {
        const auto& p=queue.front();
        const uint32_t tid=(uint32_t(p[4])<<16)|(uint32_t(p[5])<<8)|p[6];
        if(observedTid!=tid) { observedTid=tid;objectStart=hostMillis; }
        queue.pop_front();++reads;if(!first) first=hostMillis;drainEnd=hostMillis;
      }
      if(pending && beforeState!=DAB::State::DsrvCommand && (!r._commandPending || issued)) {
        if(!queue.empty()) ++deferred;
#if FMDAB_BENCH_L1
        if(beforeState==DAB::State::EpgComponentCommand) ++ua;
#endif
      }
      if(issued) commandDue=0;
      if(!r._commandPending) commandDue=0;
      if(r.slideshowAvailable() && r._slideshowCompletedTransportId!=lastCompletedTid) {
        lastCompletedTid=r._slideshowCompletedTransportId;
        ++completed;last=hostMillis;
        lastObjectMs=hostMillis-objectStart;
        assert(r.slideshowLength()==image.size() && memcmp(r.slideshowData(),image.data(),image.size())==0);
        hash=2166136261U;for(auto b:image) hash=(hash^b)*16777619U;
      }
      if(r.takeSlideshowUpdate()) r.acknowledgeSlideshow();
      if(pause && hostMillis==100+pause-1) retained=r._slideshowReceivedBytes!=0;
      if(queue.empty()) lastGetAt=0;
      previousQueue=!queue.empty();
      // Limit fixture telemetry storage; production heap is still fixed.
      if(hostEvents.size()>10000) hostEvents.clear();
    }
    assert(reads>30 && !r._dsrvPending && hostHeapAllocations==0);
    if(!pause) assert(completed>=1);
    std::printf("{\"case\":\"%s\",\"bytes\":%zu,\"firstToCompleteMs\":%u,\"queueWallMs\":%u,\"activeDrainMs\":%u,\"reads\":%u,\"metadataDeferrals\":%u,\"duplicates\":%u,\"complete\":%u,\"hash\":%u,\"UA\":%u,\"retained\":%s,\"maxQueueSpanMs\":%u,\"hintToGetMs\":%u,\"maxHintToGetMs\":%u,\"maxGetGapMs\":%u,\"lastObjectMs\":%u}\n",
                name,image.size(),last?last-first:0,drainEnd-drainStart,activeDrain,reads,deferred,duplicates,completed,hash,ua,retained?"true":"false",maxPending,firstGetAt-drainStart,maxHintToGet,maxGetGap,lastObjectMs);
  }
};
int main() {
  for(unsigned size:{15588U,16309U}) {
    hostMillis=100;DabHostAccess h(size);h.carousel(0x125BA7,100);h.run(size==15588?"irq-31":"irq-32",true);
  }
  hostMillis=100;{ DabHostAccess h(16309);h.carousel(0x125BA7,125);h.run("poll-32",false); }
  hostMillis=100;{ DabHostAccess h(16309);h.carousel(0x125BA7,100,0,true);h.run("reverse-32",true); }
  for(unsigned gap:{45000U,90000U,180000U}) {
    hostMillis=100;DabHostAccess h(16309);h.carousel(0x125BA7,100,gap);h.run(gap==45000?"gap45":gap==90000?"gap90":"gap180",true,gap);
  }
  hostMillis=100;{
    DabHostAccess h(16309);h.carousel(0x125BA7,100);h.carousel(0x125BA8,351);
    // Single-arena cache protection deliberately starts replacement at body0;
    // the initial LAST31 over a cached image returns in the next carousel.
    h.events.push_back({1000,h.body(0x125BA8,31)});h.run("two-TID",true);
  }
  hostMillis=100;{ DabHostAccess h(16309);h.carousel(0x125BA7,100);h.run("render-gap421",true,0,true); }
}
