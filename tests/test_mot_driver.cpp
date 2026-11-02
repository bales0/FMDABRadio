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
    assert(fresh.radio._slideshowReceivedBytes==608 && fresh.radio._slideshowTransportId==tid);
    for (unsigned i=0;i<20;++i) fresh.body(i,i==19,tid+1);
    assert(fresh.radio.slideshowAvailable());
    DabHostAccess length;length.partial();length.header(tid,639);
    assert(length.radio._slideshowCollecting && length.radio._slideshowReceivedBytes==608);
    length.complete();

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
    DabHostAccess warning;warning.partial();
    const uint32_t warningGeneration=warning.radio._generation;
    memset(warning.radio._workspace,0,8);warning.radio._workspace[4]=0x40;
    warning.radio.processDabEventReply();
    assert(warning.radio._generation==warningGeneration && warning.radio._slideshowReceivedBytes==608);
    warning.complete(); // A warning alone must not erase valid partial PAD.
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
    assert(duplicate.radio._slideshowCollecting && duplicate.radio._slideshowReceivedBytes==608);
    DabHostAccess shortDuplicate;shortDuplicate.partial();
    shortDuplicate.feed(packet(0x74,tid,0,false,std::vector<uint8_t>(31,0)));
    assert(shortDuplicate.radio._slideshowCollecting && shortDuplicate.radio._slideshowReceivedBytes==608);
    DabHostAccess boundary;boundary.partial();boundary.body(18,true);
    assert(boundary.radio._slideshowCollecting && boundary.radio._slideshowReceivedBytes==608);
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
    assert(!full.radio.slideshowAvailable() && full.radio._slideshowCollecting);

    DabHostAccess published;published.partial();published.complete();
    published.header(tid+1);
    assert(published.radio.slideshowAvailable());
    assert(memcmp(published.radio.slideshowData(),published.image.data(),640)==0);
    const uint32_t lease=published.radio.beginSlideshowRender();assert(lease!=0);
    published.body(0,false,tid+1);assert(published.radio.slideshowAvailable());
    published.radio.endSlideshowRender(lease,true);
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
  void sizedBody(unsigned segment,uint32_t object,unsigned stride=512,bool withLast=true) {
    const size_t at=segment*stride,n=std::min(size_t(stride),image.size()-at);
    feed(packet(0x74,object,uint16_t(segment),withLast && at+n==image.size(),
                std::vector<uint8_t>(image.begin()+at,image.begin()+at+n)));
  }
  static void carousel() {
    DabHostAccess h;h.image.resize(16309);
    for(size_t i=8;i<h.image.size();++i) h.image[i]=uint8_t(i*17U);
    h.header(tid,h.image.size());for(unsigned i=0;i<32;++i) h.sizedBody(i,tid);
    assert(h.publications==1 && h.radio.slideshowAvailable());
    // Lost notification/ACK cannot lock the arena. B enters at LAST31.
    h.radio._slideshowPublishedPending=true;h.radio._slideshowUpdate=true;
    h.sizedBody(31,tid+1);
    assert(!h.radio.slideshowAvailable() && h.radio._slideshowReceivedBytes==437);
    assert((h.radio._slideshowSegmentBitmap[3]&0x80)!=0 && h.radio._slideshowTotalSegments==32);
    h.sizedBody(0,tid+1);h.header(tid+1,h.image.size());
    for(int i=30;i>=1;--i) if(i!=7) h.sizedBody(unsigned(i),tid+1);
    assert(h.radio._slideshowReceivedBytes==h.image.size()-512 && h.radio.slideshowProgress()==96);
    assert((h.radio._slideshowSegmentBitmap[0]&0x80)==0);
    // Late A and malformed LAST cannot damage B's valid partial bitmap.
    const auto bytes=h.radio._slideshowReceivedBytes;
    h.header(tid,h.image.size());h.sizedBody(0,tid);
    h.feed(packet(0x74,tid+1,30,true,std::vector<uint8_t>(512,1)));
    assert(h.radio._slideshowReceivedBytes==bytes && h.radio._slideshowTransportId==tid+1);
    h.wait(180000);h.sizedBody(7,tid+1);
    assert(h.radio.slideshowAvailable() && h.publications==1); // identical B dedup
    assert(memcmp(h.radio.slideshowData(),h.image.data(),h.image.size())==0);
    h.image[50]^=0x55;h.sizedBody(31,tid+2);h.header(tid+2,h.image.size());
    for(unsigned i=0;i<31;++i) h.sizedBody(i,tid+2);
    assert(h.radio.slideshowAvailable() && h.publications==2);
    assert(h.radio._slideshowCompletedTransportId==tid+2);
    assert(memcmp(h.radio.slideshowData(),h.image.data(),h.image.size())==0);
    DabHostAccess abandoned;abandoned.partial();
    abandoned.body(19,true,tid+1);
    assert(abandoned.radio._slideshowTransportId==tid+1 && abandoned.radio._slideshowReceivedBytes==32);
    abandoned.header(tid+1);abandoned.body(0,false,tid); // old partial cannot hijack
    assert(abandoned.radio._slideshowTransportId==tid+1);
    for(unsigned i=0;i<19;++i) abandoned.body(i,false,tid+1);
    assert(abandoned.radio.slideshowAvailable());
    puts("MOT carousel PASS: A->B->C, LAST31/header/body0 reverse, exact bitmap/bytes, lost ACK, stale A rejected, identical B dedup, long gap");
  }
  static void recycledTid() {
    DabHostAccess h;h.partial();h.complete();h.radio.acknowledgeSlideshow();
    for(unsigned cycle=0;cycle<3;++cycle) {
      h.header();for(unsigned i=0;i<20;++i) h.body(i,i==19);
      assert(h.radio.slideshowAvailable() && !h.radio._slideshowCollecting && h.publications==1);
      assert(memcmp(h.radio.slideshowData(),h.image.data(),640)==0);
    }
    // Same TID/header/BodySize and unchanged segment0: changed segment7 must
    // preserve the already received identical prefix in this explicit cycle.
    h.image[7*32+13]^=0x5A;h.header();
    for(unsigned i=0;i<8;++i) h.body(i);
    assert(h.radio._slideshowCollecting && h.radio._slideshowReceivedBytes==256);
    assert(h.radio._slideshowSegmentBitmap[0]==0xFF && !h.radio.slideshowAvailable());
    for(unsigned i=8;i<20;++i) h.body(i,i==19);
    assert(h.publications==2 && h.radio.slideshowAvailable());
    assert(memcmp(h.radio.slideshowData(),h.image.data(),640)==0);
    h.header();for(unsigned i=0;i<20;++i) h.body(i,i==19);
    assert(h.publications==2 && h.radio.slideshowAvailable());
    // The raw cached segmentation remains valid even when the renderer sees
    // a PNG view past a transport prefix; no memmove destroys replay evidence.
    DabHostAccess prefix;
    auto expected=prefix.image;
    prefix.image.insert(prefix.image.begin(),10,0x11);
    prefix.header(tid,650);for(unsigned i=0;i<21;++i) prefix.sizedBody(i,tid,32);
    assert(prefix.radio.slideshowLength()==640 && prefix.radio._slideshowImageOffset==10);
    assert(memcmp(prefix.radio.slideshowData(),expected.data(),640)==0);
    prefix.image[400]^=0x31;expected[390]^=0x31;prefix.header(tid,650);
    for(unsigned i=0;i<21;++i) prefix.sizedBody(i,tid,32);
    assert(prefix.publications==2 && prefix.radio.slideshowLength()==640);
    assert(memcmp(prefix.radio.slideshowData(),expected.data(),640)==0);
    puts("MOT recycled TID PASS: exact unchanged cache, header+body0 cycle, changed interior same-size bytes, seeded prefix, raw offset view");
  }
  static void renderLease() {
    DabHostAccess h;h.partial();h.complete();
    const auto snapshot=h.image;const uint32_t token=h.radio.beginSlideshowRender();
    assert(token!=0 && h.radio.beginSlideshowRender()==0);
    h.header(tid+1);h.body(19,true,tid+1);h.radio.acknowledgeSlideshow();
    assert(h.radio.slideshowAvailable() && memcmp(h.radio.slideshowData(),snapshot.data(),640)==0);
    h.radio.endSlideshowRender(token+1,true);h.body(0,false,tid+1);
    assert(memcmp(h.radio.slideshowData(),snapshot.data(),640)==0);
    h.radio.endSlideshowRender(token,true);h.body(19,true,tid+1);
    assert(h.radio._slideshowReceivedBytes==32 && !h.radio.slideshowAvailable());
    h.header(tid+1);for(unsigned i=0;i<19;++i) h.body(i,false,tid+1);
    assert(h.radio.slideshowAvailable() && h.publications==1);
    const uint32_t newer=h.radio.beginSlideshowRender();h.radio.endSlideshowRender(token,false);
    assert(h.radio._slideshowRenderToken==newer && h.radio.slideshowAvailable());
    h.radio.endSlideshowRender(newer,false);assert(!h.radio.slideshowAvailable());
    h.header(tid+2);for(unsigned i=0;i<20;++i) h.body(i,i==19,tid+2);
    assert(h.radio.slideshowAvailable());
    ++h.radio._generation;h.body(19,true,tid+2);
    assert(h.radio._slideshowCollecting && h.radio._slideshowReceivedBytes==32);
    h.header(tid+2);for(unsigned i=0;i<19;++i) h.body(i,false,tid+2);
    assert(h.radio.slideshowAvailable() && h.radio._slideshowGeneration==h.radio._generation);
#if FMDAB_MOT_DIAGNOSTICS
    assert(h.radio.slideshowDiagnostics().renderBlocked>=3);
    assert(h.radio.slideshowDiagnostics().waitForZero==0);
#endif
    puts("MOT render lease PASS: immutable source during input, ACK cannot unlock, stale token cannot release newer lease, failure releases next TID");
  }
  static void dsrvRace() {
    DabHostAccess h;
    auto p=packet(0x74,tid,0,false,std::vector<uint8_t>(32,1));
    std::vector<uint8_t> frame(24+p.size(),0);frame[0]=0x90;frame[4]=1;frame[7]=0x7C;
    si468x::writeLe32(frame.data()+8,sid);si468x::writeLe32(frame.data()+12,cid);
    si468x::writeLe16(frame.data()+18,uint16_t(p.size()));std::copy(p.begin(),p.end(),frame.begin()+24);
    memcpy(h.radio._workspace,frame.data(),24);h.radio._workspace[0]=0x80;
    SPI.reply(frame);h.radio.processDsrvReply();
    assert(h.radio._dsrvPending); // fresh STATUS during payload, despite remaining=0
    // A zero-byte empty reply consumes the hint, even if DSRVINT is sticky.
    memset(h.radio._workspace,0,24);h.radio._workspace[0]=0x90;
    h.radio.processDsrvReply();assert(!h.radio._dsrvPending);
    memcpy(h.radio._workspace,frame.data(),24);frame[0]=0x80;SPI.reply(frame);
    SPI.onTransfer=[](size_t n) {
      if(n==30) { SPI.onTransfer=nullptr;hostInterruptHandler()(); }
    };
    h.radio.processDsrvReply();assert(h.radio._dsrvPending); // real ISR inside read
    SPI.onTransfer=nullptr;SPI.reply({0x80,0,0,0});h.radio._state=DAB::State::Off;h.radio.task();
    // A late physical-error flag in the full response cannot mutate the bitmap.
    const auto bytes=h.radio._slideshowReceivedBytes;
    memcpy(h.radio._workspace,frame.data(),24);frame[4]=5;SPI.reply(frame);h.radio.processDsrvReply();
    assert(h.radio._slideshowReceivedBytes==bytes);
    puts("DSRV race PASS: late STATUS and ISR retained, empty sticky reply stops, late damaged payload rejected");
  }
#endif
};
int main() {
  DabHostAccess::delayed();DabHostAccess::identities();
  DabHostAccess::boundsAndOwnership();DabHostAccess::spi();
#ifndef FMDAB_MOT_BASELINE
  DabHostAccess::activity();
  DabHostAccess::carousel();DabHostAccess::recycledTid();DabHostAccess::renderLease();
  DabHostAccess::dsrvRace();
#endif
  puts("MOT real-driver PASS: delayed 45/90/180s + wrap, DSRV, identity/generation, bounds, ownership, SPI CS/framing");
}
