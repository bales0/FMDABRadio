#pragma once
#include <stddef.h>
#include <stdint.h>

typedef void (*JPEGOutputCallback)(void* context, int32_t x, int32_t y,
                                   int32_t width, int32_t height,
                                   const uint16_t* pixels);

// Tiny display adapter keeps the decoder independent of a particular TFT
// library and makes the exact decoder path desktop-testable.
class JPEGDisplay {
 public:
  JPEGDisplay(JPEGOutputCallback callback, void* context)
      : callback_(callback), context_(context) {}
  void pushImage(int32_t x, int32_t y, int32_t width, int32_t height,
                 uint16_t* pixels) {
    if (callback_) callback_(context_, x, y, width, height, pixels);
  }

 private:
  JPEGOutputCallback callback_;
  void* context_;
};

enum class JPEGPreflightResult : uint8_t {
  SupportedBaseline = 0,
  InvalidJpeg = 1,
  // Retained for source/ABI compatibility; new code accepts supported SOF2.
  UnsupportedProgressive = 2,
  UnsupportedMultiscan = 3,
  UnsupportedDimensions = 4,
  UnsupportedComponents = 5,
  SupportedProgressive = 6
};

struct JPEGImageInfo {
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t restartInterval = 0;
  uint16_t restartMarkers = 0;
  uint8_t components = 0;
  uint8_t maxHorizontalSampling = 0;
  uint8_t maxVerticalSampling = 0;
  uint8_t scans = 0;
  uint8_t scaleDivisor = 1;
  bool app0 = false;
  bool sof0 = false;
  bool sof2 = false;
  int16_t lastRenderedMcuRow = -1;
};

typedef void (*JPEGRowCallback)(void* context);

JPEGPreflightResult JPEGpreflight(const uint8_t* data, size_t size,
                                  int displayWidth, int displayHeight,
                                  JPEGImageInfo& info);
const char* JPEGpreflightName(JPEGPreflightResult result);

// Decode the complete entropy stream without touching TFT. The same fixed
// workspace is reused by the subsequent render pass.
bool JPEGvalidate(const uint8_t* data, size_t size,
                  int displayWidth, int displayHeight,
                  uint8_t* workspace, size_t workspaceSize,
                  JPEGImageInfo* info = nullptr,
                  JPEGRowCallback rowCallback = nullptr,
                  void* rowContext = nullptr,
                  bool verboseScanDiagnostics = false);

// Decode a JPEG directly from RAM and render it. Baseline and progressive
// Huffman streams use bounded caller-owned workspace, automatic integer
// downscaling up to 1:4 and strict end-of-scan validation; no full-frame
// coefficient buffer is allocated.
bool JPEGdecoder(const uint8_t* data, size_t size, JPEGDisplay& tft,
                 int displayWidth = 320, int displayHeight = 240,
                 uint8_t* workspace = nullptr, size_t workspaceSize = 0,
                 JPEGImageInfo* info = nullptr,
                 JPEGRowCallback rowCallback = nullptr,
                 void* rowContext = nullptr,
                 bool verboseScanDiagnostics = false);
