// Compile the real Slideshow.cpp against a host-only TFT spy/heap allocator.
#include "../src/Slideshow.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

HostESP ESP;
unsigned hostHeapAllocations=0;
static std::vector<uint8_t> read(const std::string& path) {
  std::ifstream file(path.c_str(),std::ios::binary);
  assert(file.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),{});
}
static void breakNextPass(void* context) {
  // Deliberately violate immutable-input ownership AFTER a stripe is shown,
  // to exercise the exceptional render failure path, not normal operation.
  auto& data=*static_cast<std::vector<uint8_t>*>(context);
  data[29]^=1U; // IHDR CRC
}
int main(int argc,char** argv) {
  assert(argc==3);
  auto data=read(argv[1]); // Adam7 320x320
  auto expectedBytes=read(std::string(argv[1])+".expected");
  assert(data.size()<=51200U);
  Adafruit_ST7735 tft;
  bool modified=true;
  const auto originalPixels=tft.pixels;
  auto bad=data;
  bad[29]^=1U;
  assert(!renderRamSlideshow(tft,bad.data(),bad.size(),0,0,160,128,nullptr,&modified));
  assert(!modified && tft.pixels==originalPixels && !tft.fills && !tft.bitmaps);
  assert(hostHeapAllocations==1U);
  assert(renderRamSlideshow(tft,data.data(),data.size(),0,0,160,128,nullptr,&modified));
  assert(modified && tft.fills==1U && tft.bitmaps==2U);
  // 320x320 fits as 128x128 centered with 16 black pixels each side.
  for (unsigned y=0;y<128;++y) for (unsigned x=0;x<160;++x) {
    uint16_t expected=0;
    if (x>=16 && x<144) {
      const size_t source=(y*320U/128U)*320U+(x-16)*320U/128U;
      memcpy(&expected,expectedBytes.data()+8+source*2,2);
    }
    assert(tft.pixels[y*160+x]==expected);
  }
  const auto validPixels=tft.pixels;
  for (unsigned repeat=0;repeat<100;++repeat) {
    assert(!renderRamSlideshow(tft,bad.data(),bad.size(),0,0,160,128,nullptr,&modified));
    assert(!modified && tft.pixels==validPixels);
    assert(renderRamSlideshow(tft,data.data(),data.size(),0,0,160,128,nullptr,&modified));
    assert(modified && tft.pixels==validPixels && hostHeapAllocations==1U);
  }
  auto jpeg=read("tests/fixtures/progressive_320x320_420.jpg");
  assert(renderRamSlideshow(tft,jpeg.data(),jpeg.size(),0,0,160,128,nullptr,&modified));
  auto standard=read(argv[2]);
  assert(renderRamSlideshow(tft,standard.data(),standard.size(),0,0,160,128,nullptr,&modified));
  // Alternating placement-constructed PNGdec and Adam7 must reset inflater state.
  assert(renderRamSlideshow(tft,data.data(),data.size(),0,0,160,128,nullptr,&modified));
  tft.afterBitmap=breakNextPass;tft.hookContext=&data;
  assert(!renderRamSlideshow(tft,data.data(),data.size(),0,0,160,128,nullptr,&modified));
  assert(modified); // Ui.ino must discard and restore its listening view.
  data[29]^=1U;tft.afterBitmap=nullptr;
  assert(renderRamSlideshow(tft,data.data(),data.size(),0,0,160,128,nullptr,&modified));
  assert(hostHeapAllocations==1U);
  assert(!renderRamSlideshow(tft,nullptr,8,0,0,160,128,nullptr,&modified) && !modified);
  assert(!renderRamSlideshow(tft,data.data(),51201,0,0,160,128,nullptr,&modified) && !modified);
  puts("Slideshow integration PASS: pixel scaling, validate-before-TFT, 100 reuse cycles, format overlay, render failure, one allocation");
}
