// Host-only framebuffer spy, not a substitute production display driver.
#pragma once
#include "Arduino.h"
#include <cassert>
#include <vector>
#define ST77XX_BLACK 0
class Adafruit_ST7735 {
 public:
  std::vector<uint16_t> pixels=std::vector<uint16_t>(160U*128U,0x1234U);
  unsigned fills=0,bitmaps=0;
  void (*afterBitmap)(void*)=nullptr;
  void* hookContext=nullptr;
  void fillScreen(uint16_t color) {
    ++fills;
    std::fill(pixels.begin(),pixels.end(),color);
  }
  void drawRGBBitmap(int x,int y,const uint16_t* source,int width,int height) {
    assert(x>=0 && y>=0 && width>0 && height>0 && x+width<=160 && y+height<=128);
    for (int row=0;row<height;++row)
      std::copy(source+row*width,source+(row+1)*width,pixels.begin()+(y+row)*160+x);
    ++bitmaps;
    if (afterBitmap) afterBitmap(hookContext);
  }
};
