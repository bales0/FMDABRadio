// Regression: exact pre-existing PNGdec 1.1.6 path, no Adam7 fallback.
#include <PNGdec.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <string>
#include <cstdio>

struct Sink {
  PNG decoder;
  unsigned width=0,height=0,rows=0;
  bool output=false;
  std::vector<uint16_t> pixels;
};
static int row(PNGDRAW* draw) {
  Sink& sink=*static_cast<Sink*>(draw->pUser);
  assert(draw->y>=0 && unsigned(draw->y)<sink.height && draw->iWidth==int(sink.width));
  uint16_t line[2048];
  assert(sink.width<=2048);
  sink.decoder.getLineAsRGB565(draw,line,PNG_RGB565_LITTLE_ENDIAN,0);
  ++sink.rows;
  if (sink.output)
    memcpy(sink.pixels.data()+draw->y*sink.width,line,sink.width*sizeof(uint16_t));
  return 1;
}
static std::vector<uint8_t> read(const std::string& path) {
  std::ifstream stream(path.c_str(),std::ios::binary);
  assert(stream.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(stream)),{});
}
int main(int argc,char** argv) {
  for (int i=1;i<argc;++i) {
    auto data=read(argv[i]);
    const auto expected=read(std::string(argv[i])+".expected");
    Sink sink;
    memcpy(&sink.width,expected.data(),4);
    memcpy(&sink.height,expected.data()+4,4);
    sink.pixels.resize(sink.width*sink.height,0xDEAD);
    for (unsigned pass=0;pass<2;++pass) {
      sink.output=pass==1;
      sink.rows=0;
      assert(sink.decoder.openRAM(data.data(),int(data.size()),row)==PNG_SUCCESS);
      assert(sink.decoder.decode(&sink,0)==PNG_SUCCESS);
      sink.decoder.close();
      assert(sink.rows==sink.height);
      for (size_t pixel=0;pixel<sink.pixels.size();++pixel) {
        if (!pass) assert(sink.pixels[pixel]==0xDEAD);
        else {
          uint16_t value=0;
          memcpy(&value,expected.data()+8+pixel*2,2);
          // PNGdec alpha blending rounds differently; allow one RGB565 step
          // per component while still checking background and channel order.
          const int shifts[]={11,5,0},masks[]={31,63,31};
          for (unsigned c=0;c<3;++c) {
            const int difference=int((value>>shifts[c])&masks[c])-
                int((sink.pixels[pixel]>>shifts[c])&masks[c]);
            if (difference < -1 || difference > 1)
              fprintf(stderr,"%s pixel=%zu component=%u expected=%04x actual=%04x\n",
                      argv[i],pixel,c,value,sink.pixels[pixel]);
            assert(difference>=-1 && difference<=1);
          }
        }
      }
    }
  }
  printf("PNGdec noninterlaced PASS: %d fixtures, validate+render\n",argc-1);
}
