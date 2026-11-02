// Actual production si4684.cpp, command engine, fake SPI/GPIO/time.
#include "../src/si4684.h"
#include <SPI.h>
#include <cassert>
#include <cstdio>
#include <vector>
uint32_t hostMillis=100;int hostPins[256];std::vector<HostEvent> hostEvents;
HostSPI SPI;HostESP ESP;unsigned hostHeapAllocations=0;
struct DabHostAccess {
  DAB r;
  static constexpr uint32_t sid=0x2FDA,cid=0x2C;
  DabHostAccess() {
    std::fill(hostPins,hostPins+256,HIGH);SPI.reply({0x80,0,0,0});SPI.transactions.clear();
    r.configurePins(13,26,14,2);r.configureSharedSpiPeer(12);
    r._band=0;r._state=DAB::State::Ready;r._volumePending=false;
    r.resetPeriodicDeadlines(hostMillis);r._activeServiceValid=false;
  }
  static std::vector<uint8_t> rf(bool valid=true,bool acquired=true,unsigned index=0) {
    std::vector<uint8_t> f(23,0);f[0]=0x80;f[5]=(valid?1:0)|(acquired?4:0);
    f[6]=39;f[8]=100;f[9]=8;f[16]=uint8_t(index);
    si468x::writeLe32(f.data()+12,index<DAB_FREQS?dab_freq[index]:0);return f;
  }
  static std::vector<uint8_t> error(uint8_t reason) {
    std::vector<uint8_t> f(28,0);f[0]=0xC0;f[4]=reason;return f;
  }
  static std::vector<uint8_t> info(unsigned bitrate=48,unsigned rate=48000) {
    std::vector<uint8_t> f(10,0);f[0]=0x80;
    si468x::writeLe16(f.data()+4,uint16_t(bitrate));si468x::writeLe16(f.data()+6,uint16_t(rate));return f;
  }
  void complete(const std::vector<uint8_t>& frame={0x80,0,0,0}) {
    assert(r._commandPending);
    SPI.script({frame,frame,frame,frame});++hostMillis;
    assert(r._radio.service()!=si468x::Result::Pending);
    r._commandPending=false;r.commandCompleted();SPI.replies.clear();
  }
  void advance(unsigned ms) { hostMillis+=ms;r.advanceState(); }
  void fresh() {
    assert(!r._commandPending);auto f=rf(true,true,r._dabTuneTarget);
    memcpy(r._workspace,f.data(),f.size());assert(r.processDabStatusReply(false));
  }
  unsigned commands(uint8_t command) const {
    unsigned n=0;for(const auto& t:SPI.transactions) if(!t.empty() && t[0]==command) ++n;return n;
  }
  void request(bool delayedRf=false) {
    assert(r.requestDabService(0,sid,cid));assert(r._state==DAB::State::DabTuneCommand);
    complete({0x81,0,0,0});r.advanceState();
    assert(r._state==DAB::State::DabSignalCommand);
    if(delayedRf) {
      complete(rf(false));assert(commands(0x81)==0 && r._state==DAB::State::DabRfWait);
      advance(100);complete(rf(true,false));assert(commands(0x81)==0);
      advance(100);
    }
    complete(rf());assert(r._state==DAB::State::DabServiceCommand && !r._activeServiceValid);
  }
  void ready() {
    request();complete();assert(r._activeServiceValid && !r._dabAudioInfoValid);
  }
  void audio(const std::vector<uint8_t>& value) {
    assert(!r._commandPending);
    r.startRaw(si468x::Command::DAB_GET_AUDIO_INFO,nullptr,0,DAB::State::DabAudioCommand,100000,10);
    complete(value);
  }
  static void startup() {
    for(unsigned failures=0;failures<3;++failures) {
      DabHostAccess h;h.request(true);
      for(unsigned i=0;i<failures;++i) {
        h.complete(error(3));assert(h.r._state==DAB::State::DabServiceRetry);
        h.advance(24);assert(!h.r._commandPending);h.advance(1);
      }
      h.complete();assert(h.r._audioActivation.verifying());
      h.audio(error(3));h.advance(500);h.audio(info());
      assert(h.r.bitrate==48 && h.r.samplerate==48000 && h.r._dabAudioInfoValid);
      assert(h.r._audioActivation.phase==dab_audio::Phase::Verified);
      const auto starts=h.commands(0x81);
      for(unsigned i=0;i<100;++i) { hostMillis+=1000;h.fresh();h.r.scheduleAudioActivation(hostMillis); }
      assert(h.commands(0x81)==starts && starts==failures+1);
      h.audio(error(3));assert(h.r.bitrate==48 && !h.r._audioActivation.verificationExpired(hostMillis+60000));
    }
    puts("Audio startup PASS: fresh STC/RF rechecks, 0/1/2 transient failures, 25ms retries, accepted != verified, late metadata cancels restart");
  }
  static void coldBoot() {
    DabHostAccess h;assert(h.r.setSlideshowEnabled(true));assert(h.r.beginAsync(0));
    unsigned steps=0;
    while(!h.r.ready() && steps++<200) {
      if(!h.r._commandPending) { h.advance(100);continue; }
      std::vector<uint8_t> frame(32,0);frame[0]=0x80;
      if(h.r._state==DAB::State::IdentifyPart) si468x::writeLe16(frame.data()+8,4684);
      if(h.r._state==DAB::State::IdentifySystem) frame[4]=uint8_t(si468x::Image::DAB);
      if(h.r._state==DAB::State::IdentifyFunction) { frame[4]=6;frame[6]=9; }
      if(h.r._state==DAB::State::VerifySlideshowRead) si468x::writeLe16(frame.data()+4,0x97);
      h.complete(frame);
    }
    assert(h.r.ready() && h.r.takeBandReady() && h.r.VerMajor==6 && h.r.VerBuild==9);
    assert(h.r.commandErrorCount()==0 && h.r.PartNo==4684);
    RadioOperation op;bool ok;assert(h.r.takeOperationResult(op,ok) && ok && op==RadioOperation::BandBoot);
    h.r._volumePending=false;h.request(true);h.complete();
    assert(h.r.takeOperationResult(op,ok) && ok && op==RadioOperation::DabService);
    h.audio(error(3));h.advance(500);h.audio(info());
    assert(h.r.bitrate==48 && h.r.samplerate==48000 && h.commands(0x81)==1);
    puts("Cold boot driver PASS: complete NVSPI/image/property/readback chain -> deferred-compatible service request -> delayed RF -> START -> audio info");
  }
  static void recovery() {
    DabHostAccess h;h.request();
    for(unsigned i=0;i<8;++i) { h.complete(error(3));if(i<7) h.advance(25); }
    assert(h.r.ready() && !h.r._activeServiceValid && !h.r._dabAudioInfoValid);
    RadioOperation op;bool success;assert(h.r.takeOperationResult(op,success) && !success);
    h.advance(500);h.fresh();assert(h.r.scheduleAudioActivation(hostMillis));
    h.complete(rf());h.complete();assert(h.r._activeServiceValid);
    h.audio(info());assert(h.r._audioActivation.phase==dab_audio::Phase::Verified);
    assert(h.commands(0x81)==9 && h.commands(0x82)==0);
    {
      DabHostAccess permanent;permanent.request();
      for(unsigned i=0;i<8;++i) { permanent.complete(error(3));if(i<7) permanent.advance(25); }
      for(unsigned i=0;i<4;++i) {
        hostMillis=permanent.r._audioActivation.due;permanent.fresh();
        assert(permanent.r.scheduleAudioActivation(hostMillis));permanent.complete(rf());
        permanent.complete(error(1));
      }
      assert(permanent.r.audioActivationFailed() && permanent.commands(0x81)==12);
      for(unsigned i=0;i<100;++i) { hostMillis+=1000;permanent.fresh();assert(!permanent.r.scheduleAudioActivation(hostMillis)); }
      assert(permanent.commands(0x81)==12 && permanent.r.ready());
      assert(permanent.r.requestDabService(0,sid+1,cid+1));
      assert(!permanent.r.audioActivationFailed());
    }
    puts("Audio recovery PASS: 8 rejected STARTs -> idle cooldown -> auto START; permanent failure capped at 12, no STOP/reset, new user request re-arms");
  }
  void list(uint16_t version,uint16_t component) {
    std::vector<uint8_t> f(40,0);f[0]=0x80;
    si468x::writeLe16(f.data()+4,34);si468x::writeLe16(f.data()+6,version);f[8]=1;
    si468x::writeLe32(f.data()+12,sid);f[17]=1;si468x::writeLe16(f.data()+36,component);
    memcpy(r._workspace,f.data(),8);SPI.script({f});r.processServiceListReply();SPI.replies.clear();
  }
  static void database() {
    DabHostAccess h;h.request();h.complete(error(1));h.fresh();h.list(1,0x31);
    assert(h.r._audioListKnown);hostMillis=h.r._audioActivation.due;h.fresh();
    assert(h.r.scheduleAudioActivation(hostMillis));h.complete(rf());
    assert(h.r._componentId==0x31 && si468x::readLe32(SPI.transactions.back().data()+8)==0x31);
    h.complete();h.audio(info());assert(h.r._activeComponentId==0x31);
    // An unchanged version, even after a stale RF sample, cannot repeatedly
    // replenish an exhausted START budget. A real new version can.
    h.r._audioActivation.phase=dab_audio::Phase::Exhausted;
    hostMillis+=2000;h.list(1,0x31);h.fresh();h.list(1,0x31);
    assert(h.r.audioActivationFailed());h.list(2,0x31);
    assert(h.r._audioActivation.phase==dab_audio::Phase::Cooldown);
    // A real reconfiguration invalidates pending generation and demands RF.
    h.r._audioActivation.phase=dab_audio::Phase::Cooldown;
    auto token=h.r._generation;memset(h.r._workspace,0,8);h.r._workspace[4]=0x80;
    // Vendor event bit 7 = reconfiguration; no unsolicited START here.
    h.r.processDabEventReply();
    assert(h.r._generation!=token && !h.r._rfParseOk && h.r.audioActivationFailed());
    h.fresh();assert(h.r._audioActivation.phase==dab_audio::Phase::Cooldown);
    puts("Audio database PASS: reject old CID -> fresh inventory CID -> safe START; reconfiguration cancels generation and needs new RF");
  }
  static void padAfterStart() {
    for(unsigned size:{15588U,16309U}) {
      DabHostAccess h;h.ready();h.audio(info());assert(h.r.setSlideshowEnabled(true));
      std::vector<uint8_t> image(size);for(unsigned i=0;i<size;++i) image[i]=uint8_t(i*13U);
      image[0]=0xFF;image[1]=0xD8;image[2]=0xFF;image[size-2]=0xFF;image[size-1]=0xD9;
      auto send=[&](unsigned type,unsigned segment,bool last,const std::vector<uint8_t>& bytes) {
        std::vector<uint8_t> f(24+11+bytes.size(),0);f[0]=0x80;f[4]=1;f[7]=0x7C;
        si468x::writeLe32(f.data()+8,sid);si468x::writeLe32(f.data()+12,cid);
        si468x::writeLe16(f.data()+18,uint16_t(f.size()-24));f[24]=uint8_t(type);
        f[26]=uint8_t((segment>>8)|(last?0x80:0));f[27]=uint8_t(segment);f[30]=1;
        f[31]=uint8_t(bytes.size()>>8);f[32]=uint8_t(bytes.size());std::copy(bytes.begin(),bytes.end(),f.begin()+33);
        h.r.startRaw(si468x::Command::GET_DIGITAL_SERVICE_DATA,nullptr,0,DAB::State::DsrvCommand,100000,24);
        h.complete(f);
      };
      std::vector<uint8_t> header(7,0);header[0]=uint8_t(size>>20);header[1]=uint8_t(size>>12);
      header[2]=uint8_t(size>>4);header[3]=uint8_t(size<<4);header[4]=3;header[5]=0x80;
      send(0x73,0,true,header);
      for(unsigned offset=0,index=0;offset<size;offset+=512,++index)
        send(0x74,index,offset+512>=size,std::vector<uint8_t>(image.begin()+offset,image.begin()+std::min(offset+512,size)));
      assert(h.r.slideshowAvailable() && h.r.slideshowLength()==size);
      assert(memcmp(h.r.slideshowData(),image.data(),size)==0 && !h.r._activeDataServiceValid);
    }
    puts("START -> audio info -> PAD MOT PASS: exact JPEG payloads 15588/31 and 16309/32, no separate data service");
  }
  static void verification() {
    DabHostAccess h;h.ready();
    for(unsigned i=0;i<6;++i) { hostMillis+=5000;h.audio(error(3)); }
    h.fresh();assert(h.r._audioActivation.verificationExpired(hostMillis));
    assert(!h.r.scheduleAudioActivation(hostMillis));assert(h.r._audioActivation.restartUsed);
    h.advance(1000);h.fresh();assert(h.r.scheduleAudioActivation(hostMillis));
    h.complete(rf());h.complete();assert(h.commands(0x81)==2 && h.commands(0x82)==0);
    for(unsigned i=0;i<6;++i) { hostMillis+=5000;h.audio(error(3)); }
    h.fresh();assert(!h.r.scheduleAudioActivation(hostMillis));assert(h.r.audioActivationFailed());
    assert(h.commands(0x81)==2);h.audio(info());assert(!h.r.audioActivationFailed());
    {
      DabHostAccess zero;zero.ready();zero.audio(info(0,0));
      assert(!zero.r._dabAudioInfoValid && zero.r._audioActivation.verifying());
      // No NOT_AVAILABLE evidence: zero/malformed values alone never restart.
      hostMillis+=60000;zero.fresh();assert(!zero.r.scheduleAudioActivation(hostMillis));
      assert(zero.commands(0x81)==1);
    }
    puts("Audio verification PASS: repeated 0x03 for >=30s -> one safe audio START (no data STOP); second failure exhausted; positive/zero metadata rules");
  }
  static void invalidation() {
    DabHostAccess h;h.request();h.complete(error(1));
    const auto generation=h.r._generation;h.r.cancelAudioActivation();
    hostMillis+=1000;h.fresh();assert(!h.r.scheduleAudioActivation(hostMillis));
    assert(h.r._generation!=generation && !h.r._audioActivation.requested());
    assert(h.r.requestDabService(1,sid+1,cid+1));
    assert(h.r._state==DAB::State::DabTuneCommand);
    h.r._commandGeneration=h.r._generation-1;h.complete();
    assert(!h.r._activeServiceValid && h.r._audioActivation.phase==dab_audio::Phase::Rf);
    {
      DabHostAccess bad;bad.request();
      // Reject wrong index/frequency and truncated parse, including stale error=0.
      bad.complete(error(1));auto f=rf();memcpy(bad.r._workspace,f.data(),23);bad.r.error=0;
      assert(!bad.r.processDabStatusReply(false,22) && bad.r.error!=0 && !bad.r.valid);
      f=rf(true,true,1);memcpy(bad.r._workspace,f.data(),23);
      assert(bad.r.processDabStatusReply(false) && !bad.r.audioRfFresh(hostMillis));
      f=rf();f[12]^=1;memcpy(bad.r._workspace,f.data(),23);assert(!bad.r.processDabStatusReply(false));
    }
    {
      DabHostAccess wait;assert(wait.r.requestDabService(0,sid,cid));
      wait.complete({0x81,0,0,0});wait.r.advanceState();wait.complete(rf(false));
      wait.advance(5001);assert(wait.r.ready() && wait.r.audioActivationFailed());
      assert(wait.commands(0x81)==0);
    }
    {
      hostMillis=0xFFFFFF00U;DabHostAccess wrap;wrap.request();wrap.complete(error(1));
      hostMillis=wrap.r._audioActivation.due;wrap.fresh();assert(wrap.r.scheduleAudioActivation(hostMillis));
      wrap.complete(rf());wrap.complete();wrap.audio(info());assert(wrap.r._dabAudioInfoValid);
    }
    puts("Audio invalidation PASS: cancel/scan, new mux/SID, stale completion, parse/lock/index/frequency rejection, RF deadline, millis wrap");
  }
  static void stops() {
    for(unsigned reason:{0U,3U,0x18U}) {
      DabHostAccess h;h.r._activeServiceValid=true;h.r._activeServiceId=sid-1;h.r._activeComponentId=cid-1;
      h.r._activeDataServiceValid=true;h.r._activeDataServiceId=sid-1;h.r._activeDataComponentId=100;
      h.r.valid=true;h.r.freq_index=0;
      assert(h.r.requestDabService(0,sid,cid));assert(h.r._state==DAB::State::DabStopDataServiceCommand);
      if(reason==0) h.complete();
      else {
        h.complete(error(uint8_t(reason)));
        if(reason==0x18) { h.advance(250);h.complete(); }
      }
      assert(h.r._state==DAB::State::DabStopServiceCommand && !h.r._activeDataServiceValid);
      h.complete(error(3));assert(h.r._state==DAB::State::DabServiceSettle);
      h.advance(200);assert(h.r._state==DAB::State::DabSignalCommand);
      h.complete(rf());h.complete();h.audio(info());
      assert(h.r._activeServiceId==sid && h.r._dabAudioInfoValid);
    }
    {
      DabHostAccess h;h.r._activeServiceValid=true;h.r._activeServiceId=sid-1;
      h.r._activeComponentId=cid-1;h.r.valid=true;h.r.freq_index=0;
      assert(h.r.requestDabService(0,sid,cid));
      hostMillis+=1001;SPI.script({{0,0,0,0}});assert(h.r._radio.service()==si468x::Result::Timeout);
      h.r._commandPending=false;h.r.commandCompleted();assert(h.r._state==DAB::State::DabStopRetry);
      h.advance(250);h.complete();h.advance(200);h.complete(rf());h.complete();
      assert(h.r._activeServiceId==sid);
    }
    {
      DabHostAccess h;h.r._activeServiceValid=true;h.r._activeServiceId=sid-1;
      h.r._activeComponentId=cid-1;h.r.valid=true;h.r.freq_index=0;
      assert(h.r.requestDabService(0,sid,cid));
      for(unsigned i=0;i<4;++i) {
        h.complete(error(0x18));if(i<3) h.advance(250*(i+1));
      }
      assert(h.r.ready() && h.r.audioActivationFailed() && h.r._serviceTransitionPending);
      h.r.valid=false;h.fresh();h.list(9,uint16_t(cid));
      assert(h.r.audioActivationFailed() && !h.r.scheduleAudioActivation(hostMillis));
      assert(h.commands(0x81)==0); // no START after an unconfirmed old STOP
    }
    puts("Actual STOP paths PASS: data/audio OK, already inactive 0x03, BUSY/backoff, timeout, settle and fresh RF before START");
  }
};
int main() {
  DabHostAccess::coldBoot();DabHostAccess::startup();DabHostAccess::recovery();DabHostAccess::database();DabHostAccess::verification();DabHostAccess::invalidation();DabHostAccess::stops();DabHostAccess::padAfterStart();
  assert(hostHeapAllocations==0);puts("Audio activation production-driver suite PASS");
}
