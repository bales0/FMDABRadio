#pragma once
#include <stdint.h>
#include <stddef.h>
#include "dab_user_application.h"

// L1 only: signalling inventory and passive diagnostics, never START/STOP,
// payload routing, programme parsing, heap allocation or changes to SLS.
namespace epg_discovery {
enum class Identity : uint8_t { Unknown, Other, Spi, Failed };
enum class Status : uint8_t { Pending, Absent, Confirmed, Incomplete };
struct Endpoint {
  uint32_t sid=0, wire=0;
  Identity identity=Identity::Unknown;
  uint16_t cid() const { return static_cast<uint16_t>(wire); }
  bool packetMot() const { return ((wire>>14)&3U)==3U && ((wire>>18)&63U)==60U; }
  bool pad() const { return ((wire>>14)&3U)==0U; }
  bool ca() const { return (wire&0x10000UL)!=0; }
};
inline uint16_t le16(const uint8_t* p) { return uint16_t(p[0])|(uint16_t(p[1])<<8); }
inline uint32_t le32(const uint8_t* p) { return le16(p)|(uint32_t(le16(p+2))<<16); }
inline bool reached(uint32_t now,uint32_t deadline) { return int32_t(now-deadline)>=0; }
class Discovery {
 public:
  static constexpr uint16_t Capacity=128;
  static constexpr uint32_t RefreshMs=60000, GapMs=250;
  void reset() {
    if(++generation_==0) ++generation_;
    count_=cursor_=0; loaded_=false; incomplete_=false; active_=false;
    next_=0; refresh_=0; packet_=pad_=0;
  }
  uint32_t generation() const { return generation_; }
  bool needsList(uint32_t now) const { return !loaded_ && reached(now,refresh_); }
  void invalid(uint32_t now) {
    reset(); incomplete_=true; refresh_=now+RefreshMs;
  }
  // Input begins at RESP4; validate the entire framing before publishing.
  bool inventory(const uint8_t* raw,size_t bytes,uint32_t now) {
    if(!raw || bytes<8 || le16(raw)<6 || le16(raw)>2694 ||
       bytes!=size_t(le16(raw))+2 || raw[4]>32) { invalid(now);return false; }
    size_t offset=8;uint16_t total=0;
    for(unsigned i=0;i<raw[4];++i) {
      if(offset>bytes || bytes-offset<24) { invalid(now);return false; }
      const unsigned n=raw[offset+5]&15U;
      if(n==15 || n>unsigned(Capacity-total)) { invalid(now);return false; }
      offset+=24;
      if(n>(bytes-offset)/4) { invalid(now);return false; }
      total+=n;offset+=n*4;
    }
    if(offset!=bytes) { invalid(now);return false; }
    bool changed=!loaded_ || count_!=total;
    offset=8;uint16_t index=0;
    for(unsigned i=0;i<raw[4];++i) {
      const uint32_t sid=le32(raw+offset);const unsigned n=raw[offset+5]&15U;
      offset+=24;
      for(unsigned j=0;j<n;++j,++index,offset+=4)
        if(entries_[index].sid!=sid || entries_[index].wire!=le32(raw+offset)) changed=true;
    }
    if(!changed) return true;
    reset();count_=total;loaded_=true;offset=8;index=0;
    for(unsigned i=0;i<raw[4];++i) {
      const uint32_t sid=le32(raw+offset);const unsigned n=raw[offset+5]&15U;
      offset+=24;
      for(unsigned j=0;j<n;++j,++index,offset+=4) {
        entries_[index].sid=sid;entries_[index].wire=le32(raw+offset);
        entries_[index].identity=Identity::Unknown;
      }
    }
    next_=now+GapMs;refresh_=now+RefreshMs;return true;
  }
  void refresh(uint32_t now) {
    if(!loaded_ || active_ || !finished() || !reached(now,refresh_)) return;
    if(++generation_==0) ++generation_;
    for(unsigned i=0;i<count_;++i) entries_[i].identity=Identity::Unknown;
    cursor_=0;next_=now;refresh_=now+RefreshMs;
  }
  bool due(uint32_t now) const { return loaded_ && !active_ && cursor_<count_ && reached(now,next_); }
  const Endpoint* issue(uint32_t now) {
    if(!due(now)) return nullptr;
    active_=true;return &entries_[cursor_];
  }
  bool commit(uint32_t token,const uint8_t* ua,size_t bytes,uint8_t count,bool ok,uint32_t now) {
    if(token!=generation_ || !active_) return false;
    Identity identity=Identity::Failed;
    if(ok && dab_ua::valid(ua,bytes,count)) {
      identity=Identity::Other;dab_ua::Reader reader(ua,bytes,count);dab_ua::Application app;
      while(reader.next(app)) if(app.type==dab_ua::SPI) identity=Identity::Spi;
    }
    entries_[cursor_].identity=identity;++cursor_;active_=false;next_=now+GapMs;
    if(finished()) refresh_=now+RefreshMs;
    return true;
  }
  bool finished() const { return loaded_ && !active_ && cursor_==count_; }
  uint16_t count() const { return count_; }
  uint16_t answered() const { return cursor_; }
  const Endpoint& entry(unsigned i) const { return entries_[i]; }
  uint16_t confirmed() const {
    uint16_t n=0;for(unsigned i=0;i<count_;++i) if(entries_[i].identity==Identity::Spi) ++n;return n;
  }
  uint16_t failed() const {
    uint16_t n=0;for(unsigned i=0;i<count_;++i) if(entries_[i].identity==Identity::Failed) ++n;return n;
  }
  Status status() const {
    if(incomplete_) return Status::Incomplete;
    if(!finished()) return Status::Pending;
    if(failed()) return Status::Incomplete;
    return confirmed()?Status::Confirmed:Status::Absent;
  }
  // Exact endpoint only. Ambiguous PAD or SLS/SPI shared components are NOT
  // proof that these bytes contain EPG. Counts explicitly mean MOT candidates.
  bool observe(uint32_t sid,uint32_t cid,uint8_t source,uint8_t type) {
    if(!loaded_ || type!=60 || cid==0 || (source!=0 && source!=1)) return false;
    for(unsigned i=0;i<count_;++i) {
      const auto& e=entries_[i];
      if(e.sid!=sid || e.cid()!=cid || e.identity!=Identity::Spi || e.ca() ||
         (source==0?!e.packetMot():!e.pad())) continue;
      uint32_t& counter=source==0?packet_:pad_;
      if(counter!=UINT32_MAX) ++counter;
      return counter==1;
    }
    return false;
  }
 private:
  Endpoint entries_[Capacity];
  uint32_t generation_=1,next_=0,refresh_=0,packet_=0,pad_=0;
  uint16_t count_=0,cursor_=0;
  bool loaded_=false,incomplete_=false,active_=false;
};
inline const char* name(Status status) {
  switch(status) {
    case Status::Pending:return "pending";
    case Status::Absent:return "not-signalled";
    case Status::Confirmed:return "SPI-signalled";
    case Status::Incomplete:return "incomplete";
  }
  return "unknown";
}
} // namespace epg_discovery
