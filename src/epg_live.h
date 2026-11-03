// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "epg_core.h"
#include "epg_discovery.h"
namespace epg_live {
enum class Indicator:uint8_t { Grey, Yellow, Green };
struct Context {
  uint32_t generation=0,sid=0,cid=0,packetSid=0,packetCid=0;
  uint16_t eid=0; uint8_t ecc=0;
  uint16_t uaTotal=0,uaAnswered=0,uaConfirmed=0;
  bool stable=false,present=false,pad=false,packet=false;
};
struct Statistics {
  uint32_t routed=0,rejected=0,busy=0,lastValid=0,maxBacklog=0,overflows=0;
};
#if EPG_ENABLE
bool ensure();
epg::Core* core();
inline bool available(){return core()!=nullptr;}
void context(const Context& value);
void clock(const epg::BroadcastClockSample& sample,uint32_t now);
void payload(const uint8_t* bytes,size_t n,uint32_t now);
void pump(uint32_t now);
Indicator indicator();
const Context& context();
const Statistics& stats();
void backlog(uint8_t count,bool overflow);
bool oom();
#else
inline bool ensure(){return false;}
inline bool available(){return false;}
inline void context(const Context&){}
inline void clock(const epg::BroadcastClockSample&,uint32_t){}
inline void payload(const uint8_t*,size_t,uint32_t){}
inline void pump(uint32_t){}
inline Indicator indicator(){return Indicator::Grey;}
inline void backlog(uint8_t,bool){}
inline bool oom(){return false;}
#endif
}
