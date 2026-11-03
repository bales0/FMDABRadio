#include "Controls.h"
#include <vector>
#include <cassert>
#include <cstdio>
uint32_t hostMillis=100;int hostPins[256];std::vector<HostEvent> hostEvents;HostESP ESP;
static void tick(Controls& c,uint32_t delta){hostMillis+=delta;c.poll(hostMillis);}
static void edge(Controls& c,unsigned pin,int level){hostPins[pin]=level;tick(c,1);tick(c,25);}
static void expect(Controls& c,ButtonId key,ButtonEventType type){ButtonEvent e;assert(c.pop(e)&&e.button==key&&e.type==type);assert(!c.pop(e));}
static void none(Controls& c){ButtonEvent e;assert(!c.pop(e));}
int main(){
  std::fill(hostPins,hostPins+256,HIGH);Controls c;c.begin(4,5,34,35,32,16,17);
  edge(c,35,LOW);none(c);tick(c,100);none(c);edge(c,35,HIGH);expect(c,ButtonId::Band,ButtonEventType::ShortPress);
  edge(c,35,LOW);tick(c,799);none(c);tick(c,1);expect(c,ButtonId::Band,ButtonEventType::LongPress);
  tick(c,1000);none(c);edge(c,35,HIGH);none(c);
  hostPins[35]=LOW;tick(c,1);hostPins[35]=HIGH;tick(c,10);tick(c,25);none(c);
  edge(c,32,LOW);tick(c,800);expect(c,ButtonId::Select,ButtonEventType::LongPress);edge(c,32,HIGH);none(c);
  edge(c,34,LOW);tick(c,999);none(c);tick(c,1);expect(c,ButtonId::Scan,ButtonEventType::LongPress);edge(c,34,HIGH);none(c);
  hostPins[35]=LOW;Controls held;held.begin(4,5,34,35,32,16,17);tick(held,2000);none(held);edge(held,35,HIGH);none(held);
  edge(held,35,LOW);tick(held,100);edge(held,35,HIGH);expect(held,ButtonId::Band,ButtonEventType::ShortPress);
  hostMillis=0xffffff00U;edge(c,35,LOW);tick(c,800);expect(c,ButtonId::Band,ButtonEventType::LongPress);edge(c,35,HIGH);none(c);
  puts("BAND controls PASS: release-only short, 800ms boundary, no long+short, bounce, boot-held suppression, SELECT800/SCAN1000, millis wrap");
}
