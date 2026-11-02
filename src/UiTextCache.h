#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

enum class UiTextField : uint8_t {
  Band, Detail, Volume, Station, SignalLabel, Snr, RssiLabel, Rssi,
  Audio, Rx, Fic, Quality, MotLabel, MotValue, StationIndex,
  Rds, Tp, Ta, Af, Pty, VolumeLabel, Count
};
namespace ui_paint {
struct Rect { int16_t x,y,w,h; };
template<size_t Capacity> struct Cell {
  char text[Capacity]={};
  uint16_t color=0,clip=0;
  int16_t x=0,y=0;
  uint8_t size=0;
  bool valid=false;
  void invalidate() { valid=false; }
  bool update(const char* value,uint16_t fg,int16_t px,int16_t py,
              uint8_t scale,uint16_t maxWidth) {
    if(!value) value="";
    const size_t length=strlen(value);
    if(length>=Capacity) { valid=false;return true; } // no truncation/overflow
    if(valid && color==fg && x==px && y==py && size==scale && clip==maxWidth &&
       strcmp(text,value)==0) return false;
    memcpy(text,value,length+1);color=fg;x=px;y=py;size=scale;clip=maxWidth;valid=true;
    return true;
  }
};
struct ListeningCache {
  Cell<64> fields[static_cast<uint8_t>(UiTextField::Count)];
  Cell<128> broadcast;
  bool headerLine=false,stationLine=false,statusLine=false;
  void invalidate() {
    for(auto& field:fields) field.invalidate();
    broadcast.invalidate();headerLine=stationLine=statusLine=false;
  }
};
inline Rect bounds(UiTextField field,bool dab) {
  static const Rect boxes[]={
    {0,0,28,14},{30,0,88,14},{146,0,14,14},{0,15,160,24},
    {0,96,26,15},{26,96,49,15},{76,96,32,15},{108,96,52,15},
    {0,81,160,15},{0,111,18,17},{19,111,22,17},{42,111,28,17},
    {71,111,22,17},{93,111,32,17},{127,111,33,17},
    {50,81,30,15},{84,81,24,15},{111,81,24,15},{137,81,23,15},
    {0,111,125,17},{120,0,26,14}
  };
  static_assert(sizeof(boxes)/sizeof(boxes[0])==static_cast<uint8_t>(UiTextField::Count),
                "Each cached LCD field needs disjoint clear bounds");
  Rect result=boxes[static_cast<uint8_t>(field)];
  if(!dab && field==UiTextField::Detail) result={19,0,99,14};
  if(!dab && field==UiTextField::Audio) result.w=48;
  return result;
}
} // namespace ui_paint
