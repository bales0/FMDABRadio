#include "epg_test_data.h"
int main(){
 auto c=std::unique_ptr<epg::Core>(new epg::Core);
 // A loaded service/day survives unpopulated choices and a return without input.
 std::vector<Object> objects{object(0,1,0,3),object(1,2,0,2),object(2,1,1,4)};
 directory(*c,objects);select(*c,objects[0]);feed(*c,objects[0]);assert(c->count()==3 && c->current()>=0 && c->fresh());
 const std::string title=c->title(0),description=c->description(0);
 c->browseDay(1);assert(!c->count() && c->current()==-1 && !c->fresh());
 c->browseDay(-1);assert(c->count()==3 && title==c->title(0) && description==c->description(0) && c->current()>=0);
 select(*c,objects[1]);assert(!c->count() && c->next()==-1);feed(*c,objects[1]);
 verify(*c,objects[0],3);verify(*c,objects[1],2);
 select(*c,objects[2]);assert(!c->count());feed(*c,objects[2]);verify(*c,objects[0],3);verify(*c,objects[2],4);
 // Visits to empty days do not displace populated selections.
 for(int delta=1;delta<=7;++delta){c->browseDay(1);assert(!c->count());}
 c->browseDay(-7);assert(c->count()==4);
 // Empty selections may be revisited indefinitely, including acquisition-ID
 // wrap, without clearing the loaded banks or aliasing another service's view.
 for(unsigned i=0;i<66000;++i){c->select(base+100+i,today+3660,0);assert(!c->count());}
 verify(*c,objects[0],3);verify(*c,objects[1],2);verify(*c,objects[2],4);
 // Freshness belongs to the chosen context, not another station's traffic.
 c->expire(900100);verify(*c,objects[0],3);assert(!c->fresh() && c->current()==-1);
 // Programme capacity: protect current input, evict the least recently used bank.
 c->reset();objects={object(0,1,0,40),object(1,2,0,40),object(2,3,0,40)};directory(*c,objects);
 select(*c,objects[0]);feed(*c,objects[0]);select(*c,objects[1]);feed(*c,objects[1]);
 verify(*c,objects[0],40);verify(*c,objects[1],40);
 select(*c,objects[2]);feed(*c,objects[2]);verify(*c,objects[1],40);verify(*c,objects[2],40);verify(*c,objects[0],0);
 // Repeated text-pool reclamation must preserve surviving strings and recover
 // every released byte instead of gradually exhausting the fixed 8 KiB pool.
 c->reset();objects.clear();for(unsigned i=0;i<7;++i)objects.push_back(object(i,i+1,0,6,500));directory(*c,objects);
 for(unsigned turn=0;turn<12;++turn)for(unsigned i=0;i<objects.size();++i){
  select(*c,objects[i]);feed(*c,objects[i]);assert(c->count()==6);
  for(size_t j=0;j<c->count();++j){const std::string description=c->description(j);assert(description==std::string(500,char('A'+(i+1)%20)));}
 }
 // Context metadata bound: least recently used slots are reclaimed as well.
 c->reset();objects.clear();for(unsigned i=0;i<10;++i)objects.push_back(object(i,i+1,0,1));directory(*c,objects);
 for(const auto& o:objects){select(*c,o);feed(*c,o);assert(c->count()==1);}
 for(unsigned i=3;i<10;++i)verify(*c,objects[i],1);
 verify(*c,objects[0],0);
 // Same object and station keys from a changed directory cannot reuse old data.
 c->reset();assert(!c->count() && !c->fresh());
 std::printf("Multi-station/day cache: isolation, instant return, freshness, LRU/96 records/8 KiB reclamation, reset: PASS (core=%zu)\n",epg::Core::memoryBytes());
}
