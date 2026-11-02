#pragma once
#include <stdint.h>

// Timing policy only; the existing radio command engine remains the sole owner.
namespace dab_audio {
enum class Phase : uint8_t { Idle, Rf, Starting, Cooldown, Accepted, Verified, Exhausted };
inline bool reached(uint32_t now,uint32_t deadline) { return int32_t(now-deadline)>=0; }
struct Activation {
  static constexpr uint8_t FastAttempts=8, SlowAttempts=4;
  static constexpr uint32_t RfWindowMs=5000, RfRecheckMs=100, RfFreshMs=1500;
  static constexpr uint32_t VerifyWindowMs=30000;
  Phase phase=Phase::Idle;
  uint32_t due=0,rfDeadline=0,acceptedAt=0,lastUnavailable=0;
  uint8_t attempts=0,slow=0,unavailable=0;
  bool restartUsed=false,restarting=false;
  void cancel() { *this=Activation{}; }
  void request(uint32_t now) { cancel();phase=Phase::Rf;rfDeadline=now+RfWindowMs; }
  bool requested() const { return phase!=Phase::Idle; }
  bool pending() const { return phase==Phase::Rf || phase==Phase::Starting || phase==Phase::Cooldown; }
  bool verifying() const { return phase==Phase::Accepted; }
  bool dueNow(uint32_t now) const { return phase==Phase::Cooldown && reached(now,due); }
  void starting() { phase=Phase::Starting; }
  // The first seven retries preserve the original short 25ms path. Thereafter
  // four isolated attempts at 0.5/1/2/4s let the service database settle.
  bool rejected(uint32_t now,bool transient) {
    if(attempts!=255) ++attempts;
    if(transient && attempts<FastAttempts && slow==0) { due=now+25;return true; }
    phase=Phase::Cooldown;
    if(slow>=SlowAttempts) { phase=Phase::Exhausted;return false; }
    due=now+(500U<<slow);++slow;return false;
  }
  void accepted(uint32_t now) {
    phase=Phase::Accepted;acceptedAt=now;unavailable=0;restarting=false;
  }
  void verified() { if(phase==Phase::Idle) return;phase=Phase::Verified;unavailable=0;restarting=false; }
  void notAvailable(uint32_t now) {
    if(phase!=Phase::Accepted) return;
    if(unavailable!=255) ++unavailable;
    lastUnavailable=now;
  }
  bool verificationExpired(uint32_t now) const {
    return phase==Phase::Accepted && unavailable>=5 &&
      uint32_t(now-acceptedAt)>=VerifyWindowMs && uint32_t(now-lastUnavailable)<=15000;
  }
  bool restart(uint32_t now) {
    if(restartUsed) { phase=Phase::Exhausted;return false; }
    restartUsed=true;restarting=true;attempts=slow=0;unavailable=0;
    phase=Phase::Cooldown;due=now+1000;return true;
  }
  void rearm(uint32_t now) {
    if(phase!=Phase::Exhausted || restartUsed) return;
    attempts=slow=0;phase=Phase::Cooldown;due=now+500;
  }
};
} // namespace dab_audio
