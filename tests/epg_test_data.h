#pragma once
// Adapted from SI4684-FMDAB-Receiver df0c8bc9eef4c4598742fd462216675d1e885d81 (GPLv3).
#include "../src/epg_core.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
using Data=std::vector<uint8_t>;
static const uint64_t base=0x40e231533200ULL;
static const uint32_t today=epg::epoch(2024,11,12);
inline void append(Data& out,const Data& value){out.insert(out.end(),value.begin(),value.end());}
inline Data number(uint64_t value,unsigned bytes){Data out(bytes);while(bytes){out[--bytes]=uint8_t(value);value>>=8;}return out;}
inline Data tlv(uint8_t tag,const Data& data){Data out{tag};if(data.size()<254)out.push_back(uint8_t(data.size()));else{out.push_back(254);append(out,number(data.size(),2));}append(out,data);return out;}
inline Data str(const std::string& value){return Data(value.begin(),value.end());}
inline void bits(Data& out,unsigned offset,unsigned count,uint32_t value){for(unsigned i=0;i<count;++i){const unsigned bit=offset+i;out[bit/8]|=uint8_t(((value>>(count-1-i))&1)<<(7-bit%8));}}
inline Data timeValue(uint32_t seconds){Data out(4);bits(out,1,17,seconds/86400+40587);bits(out,21,5,(seconds/3600)%24);bits(out,26,6,(seconds/60)%60);return out;}
struct Object{uint16_t tid;uint64_t bearer;uint32_t day;Data body;};
inline Object object(unsigned id,unsigned station,unsigned day,unsigned count,unsigned description=8){
 Object out{uint16_t(0x100+id),base+station,today+day*86400,{}};
 Data root=tlv(5,number(out.bearer,6));
 for(unsigned i=0;i<count;++i){
  Data p=tlv(0x81,number(i+1,3));
  append(p,tlv(0x12,tlv(1,str("station "+std::to_string(station)+" item "+std::to_string(i)))));
  append(p,tlv(0x1b,tlv(1,str(std::string(description,char('A'+station%20))))));
  Data time=tlv(0x80,timeValue(out.day+3600+i*60));append(time,tlv(0x81,number(3600,2)));
  append(p,tlv(0x2c,time));append(root,tlv(0x1c,p));
 }
 out.body=tlv(2,root);return out;
}
inline Data group(uint16_t tid,uint8_t type,unsigned segment,bool last,const Data& body){
 Data out{uint8_t(0x70|type),0,uint8_t((segment>>8)|(last?128:0)),uint8_t(segment),0x12,uint8_t(tid>>8),uint8_t(tid),uint8_t(body.size()>>8),uint8_t(body.size())};
 append(out,body);const uint16_t crc=epg::crc16(out.data(),out.size());append(out,number(crc,2));return out;
}
inline void directory(epg::Core& c,const std::vector<Object>& objects){
 Data out(13);out[5]=uint8_t(objects.size());
 for(const auto& o:objects){append(out,number(o.tid,2));Data h(7);const size_t bearerBytes=o.bearer>>48?8:6;const size_t n=o.body.size(),size=9+bearerBytes;
  h[0]=uint8_t(n>>20);h[1]=uint8_t(n>>12);h[2]=uint8_t(n>>4);h[3]=uint8_t(n<<4);h[4]=uint8_t(size>>1);h[5]=uint8_t((size&1)*128+14);h[6]=1;
  h.push_back(0xe7);h.push_back(uint8_t(bearerBytes));append(h,number(o.bearer,unsigned(bearerBytes)));append(out,h);
 }
 const Data size=number(out.size(),4);for(unsigned i=0;i<4;++i)out[i]=size[i];
 auto frame=group(1,6,0,true,out);assert(c.feed(frame.data(),frame.size(),1)==epg::Feed::Accepted);
 assert(c.entryCount()==objects.size());
}
static uint32_t clockMs=10;
inline void select(epg::Core& c,const Object& o){c.select(o.bearer,o.day+3660,0);}
inline void feed(epg::Core& c,const Object& o){
 c.focus(o.tid);
 for(size_t pos=0,segment=0;pos<o.body.size();pos+=900,++segment){const size_t n=o.body.size()-pos<900?o.body.size()-pos:900;
  Data piece(o.body.begin()+pos,o.body.begin()+pos+n);auto frame=group(o.tid,4,unsigned(segment),pos+n==o.body.size(),piece);
  assert(c.feed(frame.data(),frame.size(),++clockMs)==epg::Feed::Accepted);while(c.busy())c.pump();
 }
}
inline void verify(epg::Core& c,const Object& o,unsigned n){
 select(c,o);assert(c.count()==n);
 for(size_t i=0;i<n;++i){assert(std::string(c.title(i)).find("station "+std::to_string(unsigned(o.bearer-base))+" item ")==0);assert(c.at(i).start>=o.day && c.at(i).start<o.day+86400);}
}
