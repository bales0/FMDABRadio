// Real si4684.cpp: deterministic DSRV, fake SPI/GPIO/clock; no copied assembler.
#include "../src/si4684.h"
#include "../src/BusConfig.h"
#include <SPI.h>
#include <cassert>
#include <cstdio>
#include <vector>

uint32_t hostMillis=100;
int hostPins[256];
std::vector<HostEvent> hostEvents;
HostSPI SPI;
HostESP ESP;
unsigned hostHeapAllocations=0;

struct DabHostAccess {
  static constexpr uint32_t sid=0x2AA1U,cid=10U,tid=0x125BA7U;
  DAB radio;
  std::vector<uint8_t> image;
  unsigned publications=0;

  DabHostAccess():image(640) {
    std::fill(hostPins,hostPins+256,HIGH);
    radio.configurePins(13,26,14,2);
    radio.configureSharedSpiPeer(12);
    radio.setSlideshowEnabled(true);
    radio._activeServiceValid=true;
    radio._activeServiceId=sid;radio._activeComponentId=cid;
    for (size_t i=0;i<image.size();++i) image[i]=uint8_t(i*31U+i/17U);
    const uint8_t png[]={137,80,78,71,13,10,26,10};
    std::copy(png,png+8,image.begin());
    SPI.reply({});
  }

  void feed(const std::vector<uint8_t>& packet,uint32_t service=sid,
            uint32_t component=cid,bool stale=false,uint8_t source=1) {
    std::vector<uint8_t> frame(24+packet.size(),0);
    frame[0]=0x80;frame[4]=1;frame[7]=uint8_t((source<<6)|60);
    si468x::writeLe32(frame.data()+8,service);
    si468x::writeLe32(frame.data()+12,component);
    si468x::writeLe16(frame.data()+18,uint16_t(packet.size()));
    std::copy(packet.begin(),packet.end(),frame.begin()+24);
    memcpy(radio._workspace,frame.data(),24);
    SPI.reply(frame);
    if (stale) {
      radio._commandGeneration=radio._generation-1U;
      radio._state=DAB::State::DsrvCommand;
      radio.commandCompleted();
      radio._state=DAB::State::Off;
    } else radio.processDsrvReply();
    if (radio.takeSlideshowUpdate()) ++publications;
  }

  static std::vector<uint8_t> packet(uint8_t type,uint32_t object,uint16_t segment,
                                     bool last,const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out(11+data.size(),0);
    out[0]=type;const uint16_t field=segment|(last?0x8000U:0U);
    out[2]=uint8_t(field>>8);out[3]=uint8_t(field);
    out[4]=uint8_t(object>>16);out[5]=uint8_t(object>>8);out[6]=uint8_t(object);
    out[7]=uint8_t(data.size()>>8);out[8]=uint8_t(data.size());
    std::copy(data.begin(),data.end(),out.begin()+9);
    return out;
  }
  void header(uint32_t object=tid,uint32_t size=640,unsigned declaredHeader=7) {
    std::vector<uint8_t> core(7,0);
    core[0]=uint8_t(size>>20);core[1]=uint8_t(size>>12);
    core[2]=uint8_t(size>>4);core[3]=uint8_t((size<<4)|(declaredHeader>>9));
    core[4]=uint8_t(declaredHeader>>1);core[5]=uint8_t((declaredHeader&1U)<<7);
    feed(packet(0x73,object,0,true,core));
  }
  void body(unsigned segment,bool last=false,uint32_t object=tid,
            uint32_t service=sid,uint32_t component=cid) {
    assert(segment<20);
    const auto first=image.begin()+segment*32;
    feed(packet(0x74,object,uint16_t(segment),last,
                std::vector<uint8_t>(first,first+32)),service,component);
  }
  void partial(bool last=true,bool withHeader=true) {
    if (withHeader) header();
    for (unsigned i=0;i<20;++i) if (i!=7) body(i,last && i==19);
    assert(radio._slideshowReceivedBytes==608 && !radio.slideshowAvailable());
    assert(radio.slideshowProgress()==95 || !withHeader);
  }
  void wait(uint32_t duration) {
    hostMillis+=duration;SPI.reply({});radio._state=DAB::State::Off;radio.task();
  }
  void complete() {
    body(7);
    assert(radio.slideshowAvailable() && radio.slideshowProgress()==100);
    assert(radio.slideshowLength()==image.size());
    assert(memcmp(radio.slideshowData(),image.data(),image.size())==0);
    assert(publications==1 && !radio._slideshowCollecting);
    assert(hostHeapAllocations==0); // fixed arena, not heap_caps_malloc
  }
  static void delayed() {
    for (uint32_t pause:{45000U,90000U,180000U}) for (bool repeatHeader:{false,true}) {
      hostMillis=100;DabHostAccess test;test.partial();test.wait(pause);
      assert(test.radio._slideshowReceivedBytes==608);
      if (repeatHeader) { test.header();test.body(0);test.body(19,true); }
      assert(test.radio._slideshowReceivedBytes==608);
      test.complete();
    }
    hostMillis=0xFFFFFF00U;DabHostAccess wrap;wrap.partial();wrap.wait(180000U);wrap.complete();
    // No header at all: LAST supplies the boundary, and idle never discards it.
    DabHostAccess noHeader;noHeader.partial(true,false);noHeader.wait(90000);noHeader.complete();
    // Multiple incomplete carousels, duplicate zero must not publish the prefix.
    DabHostAccess cycles;cycles.partial(false);
    for (unsigned round=0;round<3;++round) {
      cycles.wait(90000);cycles.header();
      for (int i=19;i>=0;--i) if (i!=7) cycles.body(unsigned(i));
      assert(!cycles.radio.slideshowAvailable() && cycles.publications==0);
      assert(cycles.radio._slideshowReceivedBytes==608);
    }
    cycles.body(19,true);cycles.complete();
    // Known exact BodySize also allows completion without any LAST at all.
    DabHostAccess sized;sized.partial(false);sized.wait(60000);sized.complete();
    DabHostAccess lateHeader;lateHeader.partial(false,false);lateHeader.body(7);
    assert(!lateHeader.radio.slideshowAvailable());
    lateHeader.wait(180000);lateHeader.header();
    assert(lateHeader.radio.slideshowAvailable() && lateHeader.publications==1);
  }
  static void identities() {
    DabHostAccess foreign;foreign.partial();
    foreign.header(tid+1,0);foreign.header(tid+1,640,100);
    assert(foreign.radio._slideshowTransportId==tid && foreign.radio._slideshowReceivedBytes==608);
    foreign.radio._activeDataServiceValid=true;
    foreign.radio._activeDataServiceId=sid+1;foreign.radio._activeDataComponentId=cid+1;
    foreign.body(7,false,tid,sid+1,cid+1); // active but different collector endpoint
    foreign.body(7,false,tid,sid,cid+1);  // non-active endpoint
    assert(foreign.radio._slideshowReceivedBytes==608);
    foreign.body(7,false,tid,sid,0); // unspecified CID resolves to active endpoint
    assert(foreign.radio.slideshowAvailable());

    DabHostAccess fresh;fresh.partial();fresh.header(tid+1);
    assert(fresh.radio._slideshowReceivedBytes==0 && fresh.radio._slideshowTransportId==tid+1);
    for (unsigned i=0;i<20;++i) fresh.body(i,i==19,tid+1);
    assert(fresh.radio.slideshowAvailable());
    DabHostAccess length;length.partial();length.header(tid,639);
    assert(!length.radio._slideshowCollecting && !length.radio.slideshowAvailable());

    for (unsigned action=0;action<5;++action) {
      DabHostAccess test;test.partial();
      test.radio._state=DAB::State::Ready;
      if (action==0) test.radio.setSlideshowEnabled(false);
      if (action==1) assert(test.radio.beginAsync(1));
      if (action==2) assert(test.radio.beginAsync(0)); // recovery path
      if (action==3) assert(test.radio.requestDabTune(19));
      if (action==4) assert(test.radio.requestDabService(19,sid+1,cid+1));
      assert(!test.radio._slideshowCollecting && test.radio._slideshowReceivedBytes==0);
      test.body(7);
      assert(!test.radio.slideshowAvailable());
    }
    DabHostAccess stale;stale.partial();++stale.radio._generation;
    auto p=packet(0x74,tid,7,false,std::vector<uint8_t>(32,0));
    stale.feed(p,sid,cid,true);
    assert(stale.radio._slideshowReceivedBytes==608 && !stale.radio.slideshowAvailable());
    stale.body(7); // current generation cannot combine with old bytes
    assert(stale.radio._slideshowReceivedBytes==32 && !stale.radio.slideshowAvailable());
    for (unsigned action=0;action<2;++action) {
      DabHostAccess cached;cached.partial();cached.complete();
      if (action==0) {
        cached.radio.setSlideshowEnabled(false);cached.radio.setSlideshowEnabled(true);
      } else {
        assert(cached.radio.beginAsync(0));
        cached.radio._activeServiceValid=true;
        cached.radio._activeServiceId=sid;cached.radio._activeComponentId=cid;
      }
      cached.radio._state=DAB::State::Off;
      cached.header();
      for (unsigned i=0;i<20;++i) cached.body(i,i==19);
      assert(cached.radio.slideshowAvailable() && cached.publications==2);
    }
    DabHostAccess switched;switched.partial();
    switched.radio._activeServiceId=sid+1;switched.radio._activeComponentId=cid+1;
    switched.body(7); // old SID/CID cannot contribute
    switched.body(0,false,tid+1,sid+1,cid+1);
    assert(switched.radio._slideshowReceivedBytes==32 && !switched.radio.slideshowAvailable());
  }
  static void boundsAndOwnership() {
    DabHostAccess duplicate;duplicate.partial();
    auto conflict=packet(0x74,tid,0,false,std::vector<uint8_t>(32,1));
    duplicate.feed(conflict);
    assert(!duplicate.radio._slideshowCollecting && !duplicate.radio.slideshowAvailable());
    DabHostAccess shortDuplicate;shortDuplicate.partial();
    shortDuplicate.feed(packet(0x74,tid,0,false,std::vector<uint8_t>(31,0)));
    assert(!shortDuplicate.radio._slideshowCollecting);
    DabHostAccess boundary;boundary.partial();boundary.body(18,true);
    assert(!boundary.radio._slideshowCollecting);
    DabHostAccess truncated;truncated.partial();
    auto bad=packet(0x73,tid+1,0,true,std::vector<uint8_t>(7,0));bad.pop_back();
    truncated.feed(bad);assert(truncated.radio._slideshowReceivedBytes==608);truncated.complete();
    DabHostAccess high;
    high.feed(packet(0x74,tid,255,false,{1}));
    assert(high.radio._slideshowReceivedBytes==1);
    high.feed(packet(0x74,tid,256,false,{1}));
    assert(high.radio._slideshowReceivedBytes==1 && !high.radio.slideshowAvailable());
    DabHostAccess full;
    for (unsigned i=0;i<25;++i)
      full.feed(packet(0x74,tid,uint16_t(i),false,std::vector<uint8_t>(2048,1)));
    assert(full.radio._slideshowReceivedBytes==DAB_SLS_ARENA_BYTES);
    full.feed(packet(0x74,tid,25,true,{1}));
    assert(!full.radio.slideshowAvailable() && !full.radio._slideshowCollecting);

    DabHostAccess published;published.partial();published.complete();
    published.header(tid+1);published.body(0,false,tid+1);
    assert(published.radio._slideshowPublishedPending && published.radio.slideshowAvailable());
    assert(memcmp(published.radio.slideshowData(),published.image.data(),640)==0);
    published.radio.acknowledgeSlideshow();published.body(0); // completed TID ignored
    assert(published.radio.slideshowAvailable() && published.publications==1);
    published.header(tid+1); // cache retained until valid body starts
    assert(published.radio.slideshowAvailable());
    published.body(0,false,tid+1);
    assert(!published.radio.slideshowAvailable() && published.radio._slideshowReceivedBytes==32);
  }
  static void spi() {
    DabHostAccess test;hostEvents.clear();hostPins[12]=LOW;hostPins[13]=LOW;
    const uint8_t args[]={1,2,3};SPI.reply({});
    assert(DAB::writeCommand(&test.radio,0x55,args,3));
    assert(hostEvents.size()>=5 && hostEvents[0].pin==12 && hostEvents[0].value==HIGH);
    assert(hostEvents[1].pin==13 && hostEvents[1].value==HIGH);
    assert(hostEvents[2].kind==1 && hostEvents[2].pin==board_bus::RADIO_SPI_HZ);
    assert((SPI.outgoing==std::vector<uint8_t>{0x55,1,2,3}));
    uint8_t reply[4];SPI.reply({0x80,1,2,3});hostPins[12]=LOW;hostPins[13]=LOW;
    assert(DAB::readReply(&test.radio,reply,4));
    assert(reply[0]==0x80 && reply[3]==3 && SPI.outgoing[0]==0);
    assert(hostPins[12]==HIGH && hostPins[13]==HIGH && !SPI.transaction);
  }
#ifndef FMDAB_MOT_BASELINE
  static void activity() {
    hostMillis=0;DabHostAccess test;
    test.partial();
    assert(test.radio.slideshowReceiving(hostMillis)); // timestamp zero is valid
    test.wait(29999U);assert(test.radio.slideshowReceiving(hostMillis));
    test.wait(1U);assert(!test.radio.slideshowReceiving(hostMillis));
    assert(test.radio.slideshowCollecting() && test.radio.slideshowProgress()==95);
    assert(test.radio._slideshowReceivedBytes==608);
    // Malformed/foreign/conflicting-endpoint packets do not refresh activity.
    test.feed({0x74});test.body(0,false,tid,sid+1,cid);
    assert(!test.radio.slideshowReceiving(hostMillis));
    test.body(0); // identical valid duplicate is genuine reception
    assert(test.radio.slideshowReceiving(hostMillis));
    test.wait(30000U);assert(!test.radio.slideshowReceiving(hostMillis));
    test.header();assert(test.radio.slideshowReceiving(hostMillis));
    test.wait(90000U);assert(!test.radio.slideshowReceiving(hostMillis));
    test.complete();
    assert(test.radio.slideshowAvailable() && !test.radio.slideshowReceiving(hostMillis));
    test.wait(180000U);assert(test.radio.slideshowAvailable()); // green OK remains
    hostMillis=0xFFFFFF00U;DabHostAccess wrap;wrap.partial();
    wrap.wait(29999U);assert(wrap.radio.slideshowReceiving(hostMillis));
    wrap.wait(1U);assert(!wrap.radio.slideshowReceiving(hostMillis));
    wrap.body(0);assert(wrap.radio.slideshowReceiving(hostMillis));
    wrap.radio.setSlideshowEnabled(false);
    assert(!wrap.radio.slideshowReceiving(hostMillis));
    puts("MOT LCD activity PASS: 30s boundary, valid-only refresh, resume, retained bytes/progress, ready cache, millis wrap");
  }
#endif
};
int main() {
  DabHostAccess::delayed();DabHostAccess::identities();
  DabHostAccess::boundsAndOwnership();DabHostAccess::spi();
#ifndef FMDAB_MOT_BASELINE
  DabHostAccess::activity();
#endif
  puts("MOT real-driver PASS: delayed 45/90/180s + wrap, DSRV, identity/generation, bounds, ownership, SPI CS/framing");
}
