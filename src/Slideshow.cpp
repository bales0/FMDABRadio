#include "Slideshow.h"

#include <JPEGDEC.h>
#undef INTELSHORT
#undef INTELLONG
#undef MOTOSHORT
#undef MOTOLONG
#include <PNGdec.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>
#include <stdarg.h>

namespace {

constexpr uint16_t SLS_MAX_SOURCE_WIDTH = 2048;

// Both decoders contain sizeable internal state but are never active at the
// same time. A union provides one fixed, correctly aligned workspace for
// either format without allocating from the heap.
union DecoderWorkspace {
  JPEGDEC jpeg;
  PNG png;

  DecoderWorkspace() {}
  ~DecoderWorkspace() {}
};

DecoderWorkspace* decoderWorkspace = nullptr;
uint16_t pngSourceLine[SLS_MAX_SOURCE_WIDTH];
bool decoderBusy = false;

struct RenderContext {
  Adafruit_ST7735* display;
  PNG* png;
  uint16_t* sourceLine;
  uint16_t line[160];
  int sourceWidth;
  int sourceHeight;
  int destinationX;
  int destinationY;
  int destinationWidth;
  int destinationHeight;
};

void fitImage(RenderContext& context, int sourceWidth, int sourceHeight,
              int x, int y, int width, int height) {
  context.sourceWidth = sourceWidth;
  context.sourceHeight = sourceHeight;
  if (static_cast<int64_t>(sourceWidth) * height >
      static_cast<int64_t>(sourceHeight) * width) {
    context.destinationWidth = width;
    context.destinationHeight =
        max(1, static_cast<int>(static_cast<int64_t>(sourceHeight) * width /
                                sourceWidth));
  } else {
    context.destinationHeight = height;
    context.destinationWidth =
        max(1, static_cast<int>(static_cast<int64_t>(sourceWidth) * height /
                                sourceHeight));
  }
  context.destinationX = x + (width - context.destinationWidth) / 2;
  context.destinationY = y + (height - context.destinationHeight) / 2;
}

int jpegDraw(JPEGDRAW* draw) {
  RenderContext* context = static_cast<RenderContext*>(draw->pUser);
  if (context == nullptr || context->display == nullptr) return 0;
  const int blockRight = min(context->sourceWidth, draw->x + draw->iWidthUsed);
  const int blockBottom = min(context->sourceHeight, draw->y + draw->iHeight);
  const int destinationLeft =
      (draw->x * context->destinationWidth + context->sourceWidth - 1) /
      context->sourceWidth;
  const int destinationRight =
      (blockRight * context->destinationWidth + context->sourceWidth - 1) /
      context->sourceWidth;
  const int destinationTop =
      (draw->y * context->destinationHeight + context->sourceHeight - 1) /
      context->sourceHeight;
  const int destinationBottom =
      (blockBottom * context->destinationHeight + context->sourceHeight - 1) /
      context->sourceHeight;

  for (int destinationY = destinationTop;
       destinationY < destinationBottom; ++destinationY) {
    const int sourceY = static_cast<int>(
        static_cast<int64_t>(destinationY) * context->sourceHeight /
        context->destinationHeight);
    const int localY = constrain(sourceY - draw->y, 0, draw->iHeight - 1);
    int output = 0;
    for (int destinationX = destinationLeft;
         destinationX < destinationRight && output < 160; ++destinationX) {
      const int sourceX = static_cast<int>(
          static_cast<int64_t>(destinationX) * context->sourceWidth /
          context->destinationWidth);
      const int localX = constrain(sourceX - draw->x, 0, draw->iWidthUsed - 1);
      context->line[output++] =
          draw->pPixels[localY * draw->iWidth + localX];
    }
    if (output > 0) {
      context->display->drawRGBBitmap(
          context->destinationX + destinationLeft,
          context->destinationY + destinationY, context->line, output, 1);
    }
  }
  delay(0);
  return 1;
}

int pngDraw(PNGDRAW* draw) {
  RenderContext* context = static_cast<RenderContext*>(draw->pUser);
  if (context == nullptr || context->display == nullptr ||
      context->png == nullptr || context->sourceLine == nullptr) {
    return 0;
  }
  const int destinationTop =
      (draw->y * context->destinationHeight + context->sourceHeight - 1) /
      context->sourceHeight;
  const int destinationBottom =
      ((draw->y + 1) * context->destinationHeight +
       context->sourceHeight - 1) /
      context->sourceHeight;
  if (destinationBottom <= destinationTop) return 1;

  context->png->getLineAsRGB565(draw, context->sourceLine,
                                PNG_RGB565_LITTLE_ENDIAN, 0x00000000UL);
  for (int destinationX = 0;
       destinationX < context->destinationWidth; ++destinationX) {
    const int sourceX = static_cast<int>(
        static_cast<int64_t>(destinationX) * context->sourceWidth /
        context->destinationWidth);
    context->line[destinationX] = context->sourceLine[sourceX];
  }
  for (int destinationY = destinationTop;
       destinationY < destinationBottom; ++destinationY) {
    context->display->drawRGBBitmap(
        context->destinationX, context->destinationY + destinationY,
        context->line, context->destinationWidth, 1);
  }
  delay(0);
  return 1;
}

void logDecode(Stream* diagnostics, const char* format, ...) {
  if (diagnostics == nullptr) return;
  char message[160];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  diagnostics->println(message);
}

}  // namespace

bool initializeSlideshowRenderer(Stream* diagnostics) {
  if (decoderWorkspace != nullptr) return true;
  decoderWorkspace = static_cast<DecoderWorkspace*>(
      heap_caps_malloc(sizeof(DecoderWorkspace), MALLOC_CAP_8BIT));
  if (decoderWorkspace == nullptr) {
    logDecode(diagnostics,
              "[SLS][ERROR] fixed decoder workspace allocation failed: need=%u free=%u largest=%u",
              static_cast<unsigned>(sizeof(DecoderWorkspace)),
              ESP.getFreeHeap(),
              heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return false;
  }
  logDecode(diagnostics,
            "[SLS] decoder workspace allocated once: %u bytes, free=%u largest=%u",
            static_cast<unsigned>(sizeof(DecoderWorkspace)),
            ESP.getFreeHeap(),
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  return true;
}

bool renderRamSlideshow(Adafruit_ST7735& display, const uint8_t* data,
                        uint32_t length, int16_t x, int16_t y,
                        int16_t width, int16_t height,
                        Stream* diagnostics) {
  if (data == nullptr || length < 8 || width <= 0 || height <= 0 ||
      width > 160) {
    return false;
  }
  if (!initializeSlideshowRenderer(diagnostics)) return false;
  if (decoderBusy) {
    logDecode(diagnostics, "[SLS][WARN] decoder workspace is already in use");
    return false;
  }
  decoderBusy = true;

  const bool jpeg = data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
  const bool png = data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E &&
                   data[3] == 0x47 && data[4] == 0x0D && data[5] == 0x0A &&
                   data[6] == 0x1A && data[7] == 0x0A;
  RenderContext context = {};
  context.display = &display;

  const uint32_t startedAt = millis();
  bool success = false;
  logDecode(diagnostics,
            "[SLS] decode start: %s bytes=%lu fixedDecoder=%u free=%u largest=%u stackHwm=%u",
            jpeg ? "JPEG" : (png ? "PNG" : "unknown"),
            static_cast<unsigned long>(length),
            static_cast<unsigned>(jpeg ? sizeof(JPEGDEC) : sizeof(PNG)),
            ESP.getFreeHeap(),
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  if (jpeg) {
    JPEGDEC* decoder = new (&decoderWorkspace->jpeg) JPEGDEC();
    if (decoder->openRAM(const_cast<uint8_t*>(data), length, jpegDraw)) {
      const int sourceWidth = decoder->getWidth();
      const int sourceHeight = decoder->getHeight();
      if (sourceWidth > 0 && sourceHeight > 0 &&
          sourceWidth <= SLS_MAX_SOURCE_WIDTH) {
        fitImage(context, sourceWidth, sourceHeight, x, y, width, height);
        decoder->setUserPointer(&context);
        decoder->setPixelType(RGB565_LITTLE_ENDIAN);
        success = decoder->decode(0, 0, 0) != 0;
      }
      if (!success) {
        logDecode(diagnostics, "[SLS][WARN] JPEG decode failed: error=%d size=%dx%d",
                  decoder->getLastError(), sourceWidth, sourceHeight);
      }
      decoder->close();
    } else {
      logDecode(diagnostics, "[SLS][WARN] unsupported or damaged JPEG: error=%d",
                decoder->getLastError());
    }
    decoder->~JPEGDEC();
  } else if (png) {
    PNG* decoder = new (&decoderWorkspace->png) PNG();
    if (decoder->openRAM(const_cast<uint8_t*>(data), length, pngDraw) ==
        PNG_SUCCESS) {
      const int sourceWidth = decoder->getWidth();
      const int sourceHeight = decoder->getHeight();
      if (sourceWidth > 0 && sourceHeight > 0 &&
          sourceWidth <= SLS_MAX_SOURCE_WIDTH) {
        context.sourceLine = pngSourceLine;
        context.png = decoder;
        fitImage(context, sourceWidth, sourceHeight, x, y, width, height);
        success = decoder->decode(&context, 0) == PNG_SUCCESS;
        context.sourceLine = nullptr;
      }
      if (!success) {
        logDecode(diagnostics, "[SLS][WARN] PNG decode failed: error=%d size=%dx%d",
                  decoder->getLastError(), sourceWidth, sourceHeight);
      }
      decoder->close();
    } else {
      logDecode(diagnostics, "[SLS][WARN] damaged PNG: error=%d",
                decoder->getLastError());
    }
    decoder->~PNG();
  }

  logDecode(diagnostics,
            "[SLS] decode %s: %s, %lu ms, free=%u largest=%u",
            jpeg ? "JPEG" : (png ? "PNG" : "unknown"),
            success ? "OK" : "failed",
            static_cast<unsigned long>(millis() - startedAt), ESP.getFreeHeap(),
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  decoderBusy = false;
  return success;
}
