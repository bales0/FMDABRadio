// Real cooperative driver + host SPI; no tuner/COM access.
#include "../src/si4684.h"
#include <SPI.h>
#include <vector>
#include <cassert>
#include <cstdio>
uint32_t hostMillis=100;
int hostPins[256];std::vector<HostEvent> hostEvents;HostSPI SPI;HostESP ESP;
unsigned hostHeapAllocations=0;
static std::vector<uint8_t> inventory(unsigned components=2) {
  std::vector<uint8_t> out(8+24+components*4,0);
  si468x::writeLe16(out.data(),uint16_t(out.size()-2));out[4]=1;
  si468x::writeLe32(out.data()+8,0x2AA1);out[13]=uint8_t(components);
  for(unsigned i=0;i<components;++i)
    si468x::writeLe32(out.data()+32+4*i,i?0x01FCE002U:0x0102000AU);
  return out;
}
struct DabHostAccess {
  DAB radio;
  DabHostAccess() {
    std::fill(hostPins,hostPins+256,HIGH);
    SPI.reply({});
    radio.configurePins(13,26,14,2);radio.configureSharedSpiPeer(12);
    radio._band=0;radio._state=DAB::State::Ready;radio.valid=true;
    radio._activeServiceValid=true;radio._activeServiceId=0x2AA1;
    radio._activeComponentId=10;radio._serviceId=0x2AA1;
    radio._operation=RadioOperation::None;radio._volumePending=false;
    radio.resetPeriodicDeadlines(hostMillis);radio.setEpgDiscoveryEnabled(true);
    auto list=inventory();assert(radio._epgDiscovery.inventory(list.data(),list.size(),hostMillis));
    hostMillis+=250;
  }
  void query() {
    radio.scheduleReadyWork(hostMillis);
    assert(radio._state==DAB::State::EpgComponentCommand && radio._commandPending);
    assert(!SPI.outgoing.empty() && SPI.outgoing[0]==0xBB);
    assert(si468x::readLe32(SPI.outgoing.data()+4)==0x2AA1);
    assert(si468x::readLe32(SPI.outgoing.data()+8)==10); // no metadata bits
  }
  void reply(bool valid=true) {
    std::vector<uint8_t> response(32,0);response[0]=0x80;
    response[26]=1;response[27]=4;response[28]=7;
    if(!valid) response[30]=3; // claims more UA bytes than available
    memcpy(radio._workspace,response.data(),28);SPI.reply(response);
    radio.processEpgComponentReply(true);
  }
  static void integration() {
    {
      DabHostAccess h;h.query();h.reply();
      assert(h.radio._epgDiscovery.entry(0).identity==epg_discovery::Identity::Spi);
      assert(h.radio._epgDiscovery.answered()==1);
      assert(!h.radio._epgDiscovery.finished());
      const auto token=h.radio._epgQueryGeneration;
      h.radio.setEpgDiscoveryEnabled(false);h.reply();
      assert(h.radio._epgDiscovery.generation()!=token && h.radio._epgDiscovery.answered()==0);
    }
    {
      DabHostAccess h;h.query();h.reply(false);
      assert(h.radio._epgDiscovery.failed()==1);
    }
    {
      DabHostAccess h;h.query();
      (void)h.radio._radio.abortCommand();h.radio._commandPending=false;
      h.radio.commandCompleted();
      assert(h.radio._state==DAB::State::Ready && h.radio.valid && h.radio._activeServiceValid);
      assert(h.radio._epgDiscovery.failed()==1 && h.radio._epgDiscovery.answered()==1);
    }
    {
      DabHostAccess h;h.radio._consecutiveCtsTimeouts=2;h.query();
      hostMillis+=101;SPI.reply({0,0,0,0});
      assert(h.radio._radio.service()==si468x::Result::Timeout);
      h.radio._commandPending=false;h.radio.commandCompleted();
      assert(h.radio._state==DAB::State::Ready && h.radio.valid);
      assert(h.radio._consecutiveCtsTimeouts==2 && h.radio._epgDiscovery.failed()==1);
      assert(h.radio._diagDabHostStarvationCount+h.radio._diagDabGenuineCtsTimeoutCount==1);
    }
    {
      DabHostAccess h;const auto token=h.radio._epgDiscovery.generation();
      assert(h.radio.requestDabService(0,0x2FD4,4));
      assert(h.radio._epgDiscovery.generation()!=token && h.radio._epgDiscovery.count()==0);
    }
    {
      DabHostAccess h;h.radio._dsrvPending=true;h.radio.scheduleReadyWork(hostMillis);
      assert(h.radio._state==DAB::State::DsrvCommand);
      assert(h.radio._epgDiscovery.answered()==0);
    }
    {
      DabHostAccess h;h.radio._dabAudioRefreshPending=true;
      h.radio.scheduleReadyWork(hostMillis);
      assert(h.radio._state==DAB::State::DabAudioCommand);
    }
    {
      DabHostAccess h;h.radio.setEpgDiscoveryEnabled(false);
      h.radio.scheduleReadyWork(hostMillis);
      assert(h.radio._state==DAB::State::Ready && !h.radio._commandPending);
    }
    {
      DabHostAccess h;h.radio._epgDiscovery.reset();
      h.radio.scheduleReadyWork(hostMillis);
      assert(h.radio._state==DAB::State::DabServiceListCommand);
      auto raw=inventory();std::vector<uint8_t> reply(4+raw.size(),0);
      reply[0]=0x80;std::copy(raw.begin(),raw.end(),reply.begin()+4);
      memcpy(h.radio._workspace,reply.data(),8);SPI.reply(reply);
      h.radio.processServiceListReply();
      assert(h.radio._epgDiscovery.count()==2 && h.radio.numberofservices==1);
      assert(h.radio._epgDiscovery.entry(1).wire==0x01FCE002U);
    }
    puts("EPG L1 actual driver PASS: component command/UA parsing, exact CID, stale/disabled, DSRV+audio priority, full inventory");
  }
  static void disabledBuild() {
    DabHostAccess h;
    assert(!h.radio._epgDiscoveryEnabled);
    h.radio.scheduleReadyWork(hostMillis);
    assert(h.radio._state==DAB::State::Ready && !h.radio._commandPending && SPI.outgoing.empty());
    puts("EPG L1 compile OFF PASS: runtime request cannot enable discovery commands");
  }
};
int main() {
  if(!EPG_DISCOVERY_ENABLE) { DabHostAccess::disabledBuild();return 0; }
  using namespace epg_discovery;
  const uint8_t spi[]={7,0,0,0},other[]={2,0,1,0};
  Discovery d;auto list=inventory();assert(d.inventory(list.data(),list.size(),100));
  assert(d.count()==2 && d.status()==Status::Pending && !d.due(349));
  assert(d.issue(350));auto token=d.generation();
  assert(!d.commit(token-1,spi,4,1,true,350));
  assert(d.commit(token,spi,4,1,true,350) && !d.finished());
  assert(d.issue(600));assert(d.commit(token,other,4,1,true,600));
  assert(d.status()==Status::Confirmed && d.confirmed()==1);
  assert(d.observe(0x2AA1,10,1,60));assert(!d.observe(0x2AA1,10,1,60));
  assert(!d.observe(0x2AA1,0,1,60) && !d.observe(0x2AA2,10,1,60));
  assert(!d.observe(0x2AA1,10,0,60) && !d.observe(0x2AA1,10,1,0));
  assert(d.inventory(list.data(),list.size(),601) && d.generation()==token);
  d.refresh(60599);assert(d.finished());d.refresh(60600);assert(d.status()==Status::Pending);
  assert(d.confirmed()==1); // periodic re-query must not revoke existing proof
  assert(d.generation()!=token && !d.commit(token,spi,4,1,true,60600));
  // Every truncated list is incomplete, never an absence claim.
  for(size_t n=0;n<list.size();++n) {
    assert(!d.inventory(list.data(),n,100));assert(d.status()==Status::Incomplete);
    assert(!d.needsList(60099) && d.needsList(60100));
  }
  auto changed=list;changed.push_back(0);assert(!d.inventory(changed.data(),changed.size(),0));
  changed=list;changed[13]=15;assert(!d.inventory(changed.data(),changed.size(),0));
  assert(d.inventory(list.data(),list.size(),0));assert(d.issue(250));
  assert(d.commit(d.generation(),spi,3,1,true,250)); // malformed padding
  assert(d.issue(500));assert(d.commit(d.generation(),nullptr,0,0,false,500));
  assert(d.status()==Status::Incomplete && d.failed()==2);
  assert(d.inventory(inventory(0).data(),32,1000));assert(d.status()==Status::Absent);
  // Refresh/gap wrap safety.
  d.reset();assert(d.inventory(list.data(),list.size(),0xFFFFFF80));
  assert(!d.due(0xFFFFFF81) && d.due(122));
  assert(d.issue(122));token=d.generation();d.reset();
  assert(!d.commit(token,spi,4,1,true,123));
  // Inventory exceeding bounded storage is explicitly incomplete.
  std::vector<uint8_t> huge(8+10*(24+14*4),0);huge[4]=10;
  si468x::writeLe16(huge.data(),uint16_t(huge.size()-2));
  for(unsigned i=0;i<10;++i) huge[8+i*80+5]=14;
  assert(!d.inventory(huge.data(),huge.size(),0) && d.status()==Status::Incomplete);
  assert(hostHeapAllocations==0);
  DabHostAccess::integration();
  puts("EPG L1 discovery PASS: bounded atomic inventory, UA framing, all-component completion, passive candidates, 60s refresh/250ms gap, wrap");
}
