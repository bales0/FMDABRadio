// SPDX-License-Identifier: GPL-3.0-only
#include "epg_live.h"
#if EPG_ENABLE
#include <new>
#include <stdlib.h>
#ifdef FMDAB_EPG_TEST_ALLOCATOR
extern void* epgLiveAllocate(size_t);
extern void epgLiveRelease(void*);
#endif
#ifndef FMDAB_HOST_TEST
#include <esp_heap_caps.h>
#endif
namespace epg_live {
static epg::Core* instance=nullptr;
static Context owner;
static Statistics counters;
static bool failed=false;
static epg::BroadcastClockSample timeSample;
static bool timeSeen=false;
static uint32_t decoded=0;
static void* allocateWords(size_t n) {
#ifdef FMDAB_EPG_TEST_ALLOCATOR
  return epgLiveAllocate(n);
#elif defined(FMDAB_HOST_TEST)
  return malloc(n);
#else
  return heap_caps_malloc(n,MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT|MALLOC_CAP_EXEC);
#endif
}
static void release(void* p){
#ifdef FMDAB_EPG_TEST_ALLOCATOR
  epgLiveRelease(p);
#else
  free(p);
#endif
}
bool ensure() {
  if(instance)return true;
  if(failed)return false; // no repeated allocation in the receive hot path
#ifdef FMDAB_EPG_TEST_ALLOCATOR
  void* storage=epgLiveAllocate(sizeof(epg::Core));
#elif defined(FMDAB_HOST_TEST)
  void* storage=malloc(sizeof(epg::Core));
#else
  const unsigned bytes=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
  const unsigned words=MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT|MALLOC_CAP_EXEC;
  if(heap_caps_get_free_size(bytes)<sizeof(epg::Core)+16000U ||
     heap_caps_get_largest_free_block(bytes)<sizeof(epg::Core) ||
     heap_caps_get_free_size(words)<epg::Core::bufferBytes(false)+4096U ||
     heap_caps_get_largest_free_block(words)<epg::kObjectBytes) {failed=true;return false;}
  void* storage=heap_caps_malloc(sizeof(epg::Core),bytes);
#endif
  if(storage) {
    // This LCD renders programme text only. Keep the full PI/SI capacities,
    // both RX slots and text arena, but do not reserve an unused EPG logo.
    auto* candidate=new(storage) epg::Core(allocateWords,release,true,false);
    if(candidate->ready())instance=candidate;
    else {candidate->~Core();release(storage);}
  }
  failed=!instance;
  if(instance){instance->setLive(true);instance->setClockTrusted(false);}
  return instance!=nullptr;
}
epg::Core* core(){return instance;}
bool oom(){return failed;}
const Context& context(){return owner;}
const Statistics& stats(){return counters;}
void backlog(uint8_t n,bool overflow){if(n>counters.maxBacklog)counters.maxBacklog=n;if(overflow)++counters.overflows;}
void context(const Context& value) {
  const bool changed=value.generation!=owner.generation || value.sid!=owner.sid ||
      value.cid!=owner.cid || value.eid!=owner.eid || value.ecc!=owner.ecc ||
      value.pad!=owner.pad || value.packetSid!=owner.packetSid || value.packetCid!=owner.packetCid;
  if(instance && changed){instance->reset();instance->setLive(true);instance->setClockTrusted(false);decoded=0;counters.lastValid=0;timeSeen=false;}
  owner=value;
  if(instance && !owner.stable)instance->setClockTrusted(false);
}
void clock(const epg::BroadcastClockSample& sample,uint32_t now) {
  timeSample=sample;timeSeen=sample.generation==owner.generation;
  const auto resolved=epg::resolveTime(sample,now,owner.stable && timeSeen);
  if(instance){
    instance->setClockTrusted(resolved.utcValid && resolved.offsetKnown);
    if(resolved.utcValid && resolved.offsetKnown)instance->setTime(resolved.utc,resolved.offset);
  }
}
void payload(const uint8_t* data,size_t n,uint32_t now){
  if(!instance || !owner.stable || !(owner.pad || owner.packet)){++counters.rejected;return;}
  const auto result=instance->feed(data,n,now);
  if(result==epg::Feed::Accepted)++counters.routed;
  else if(result==epg::Feed::Busy)++counters.busy;
  else ++counters.rejected;
}
void pump(uint32_t now){
  if(!instance)return;
  instance->expire(now);
  if(!owner.stable)return;
  bool conflict=false;
  const uint64_t bearer=instance->findStation(owner.sid,owner.eid,owner.ecc,-1,conflict);
  if(bearer!=instance->selected())instance->select(bearer,0,instance->offset());
  instance->identityConflict=conflict;
  if(timeSeen)clock(timeSample,now);
  instance->pump(16); // bounded TLV work; no allocation, SPI, UI, or task recursion
  const uint32_t valid=instance->stats.validUpdates;
  if(valid!=decoded){decoded=valid;counters.lastValid=now;}
}
Indicator indicator(){
  if(!owner.stable || !owner.present)return Indicator::Grey;
  if(instance && instance->selected() && !instance->ambiguous() && instance->hasPI() &&
     instance->availability(true)==epg::Availability::ReadyLive)return Indicator::Green;
  return Indicator::Yellow;
}
}
#endif
