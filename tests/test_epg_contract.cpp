#include "epg_test_data.h"
#include <climits>
#include <cstdlib>

static unsigned allocations=0,releases=0,failAt=UINT_MAX;
static void* allocate(size_t size) {
  const unsigned at=allocations++;
  return at==failAt?nullptr:malloc(size);
}
static void release(void* data) { ++releases;free(data); }
int main() {
  uint32_t utc=0;int16_t offset=0;uint64_t bearer=0;
  assert(!epg::timePoint(nullptr,4,utc,offset));
  assert(!epg::bearerValue(nullptr,6,bearer));
  for(unsigned fail=0;fail<5;++fail) {
    allocations=releases=0;failAt=fail;
    {
      epg::Core c(allocate,release);
      assert(!c.ready() && releases==fail && allocations==fail+1);
      c.select(base+1,today+3660,60);c.reset();c.pump();c.expire(UINT_MAX);
      assert(c.feed(nullptr,0,0)==epg::Feed::Rejected && c.count()==0);
      assert(!*c.title(100) && !*c.description(100));
    }
    assert(releases==fail);
  }
  allocations=releases=0;failAt=UINT_MAX;
  {
    epg::Core c(allocate,release);assert(c.ready() && allocations==5);
    auto o=object(0,1,0,3);directory(c,{o});select(c,o);feed(c,o);
    assert(c.count()==3 && c.current()>=0 && c.next()>=0);
    const auto title=std::string(c.title(0));
    c.setClockTrusted(false);
    assert(c.current()==-1 && c.next()==-1 && c.count()==3 && title==c.title(0));
    c.setTime(0,60); // failed refresh cannot erase accepted time/cache
    assert(c.count()==3 && title==c.title(0));
    c.expire(900100U);assert(c.count()==3 && !c.fresh());
    c.setClockTrusted(true);assert(c.current()==-1);
    feed(c,o);assert(c.count()==3 && c.current()>=0);
    auto bad=group(o.tid,4,0,true,{0});bad.back()^=1;
    assert(c.feed(bad.data(),bad.size(),++clockMs)==epg::Feed::Rejected);
    assert(c.count()==3 && title==c.title(0));
    c.setClockTrusted(false);c.setClockTrusted(true);
    const int offset=c.dayOffset();c.browseDay(INT_MAX);c.browseDay(INT_MIN);
    assert(c.dayOffset()==offset);
    c.stats.frames=c.stats.invalid=c.stats.crcBad=c.stats.duplicate=UINT_MAX;
    c.feed(nullptr,0,0);c.feed(bad.data(),bad.size(),1);feed(c,o);
    assert(c.stats.frames==UINT_MAX && c.stats.invalid==UINT_MAX &&
           c.stats.crcBad==UINT_MAX && c.stats.duplicate==UINT_MAX);
    bool conflict=false;
    assert(c.findStation(uint16_t(o.bearer),uint16_t(o.bearer>>16),
                         uint8_t(o.bearer>>32),-1,conflict)==o.bearer && !conflict);
    auto other=object(1,2,0,2);directory(c,{o,other});select(c,o);feed(c,o);
    select(c,other);assert(c.count()==0);feed(c,other);assert(c.count()==2);
    select(c,o);assert(c.count()==3);
    auto scidsObject=o;scidsObject.bearer|=uint64_t(3)<<40;
    directory(c,{scidsObject});
    assert(c.findStation(uint16_t(o.bearer),uint16_t(o.bearer>>16),
                         uint8_t(o.bearer>>32),3,conflict)==scidsObject.bearer);
    assert(!c.findStation(uint16_t(o.bearer),uint16_t(o.bearer>>16),
                          uint8_t(o.bearer>>32),0,conflict));
    auto longObject=o;longObject.bearer=0x55e2315312345678ULL;
    directory(c,{longObject});
    assert(c.findStation(0x12345678U,0x3153U,0xE2U,5,conflict)==longObject.bearer);
    assert(!c.findStation(0x12345678U,0x3153U,0xE2U,0,conflict));
    directory(c,{o});select(c,o);feed(c,o);
    for(unsigned i=0;i<100;++i) { c.setClockTrusted(false);c.setClockTrusted(true);feed(c,o); }
    assert(allocations==5); // no allocation during feed/pump/cache/refresh
  }
  assert(releases==5);
  for(int offset:{0,30,60,120}) {
    epg::BroadcastClockSample sample;
    sample.valid=sample.offsetKnown=true;sample.offsetMinutes=int16_t(offset);
    sample.encoding=epg::ClockEncoding::UTC;
    sample.calendarEpoch=epg::epoch(2024,10,27,1,30);
    sample.sampledAtMonotonicMs=0xFFFFFF00U;
    const auto time=epg::resolveTime(sample,0xFFFFFF00U+1000U,true);
    assert(time.utcValid && time.localValid && time.utc==sample.calendarEpoch+1 &&
           time.local==time.utc+offset*60U);
    assert(!epg::resolveTime(sample,0xFFFFFF00U+300000U,true).utcValid);
    assert(!epg::resolveTime(sample,0xFFFFFF00U,false).utcValid);
    for(auto utc:{epg::epoch(2024,3,31,1,30),epg::epoch(2024,10,27,1,30)}) {
      int16_t paired=-1;
      assert(epg::pairedOffsetAfterUtc(utc,100,utc+offset*60+1,1100,paired));
      assert(paired==offset);
      assert(!epg::pairedOffsetAfterUtc(utc,100,utc+offset*60+3,3100,paired));
    }
  }
  printf("EPG contract PASS: OOM cleanup, no feed allocations, cache/time confidence, isolation, counters, DST offsets/wrap; Core=%zu buffers=%zu total=%zu largest=%zu\n",
         sizeof(epg::Core),epg::Core::bufferBytes(),epg::Core::memoryBytes(),epg::Core::largestBlock());
}
