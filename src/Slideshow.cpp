#include "Slideshow.h"

#include "JPEGdecoder.h"
#include "PngAdam7Scale.h"
#include <PNGdec.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>
#include <stdarg.h>

namespace {

constexpr uint16_t SLS_MAX_SOURCE_WIDTH = 2048;

// JPEG and PNG are never decoded concurrently. Reserve one early, persistent
// arena and construct the PNG decoder in it only while a PNG is being handled.
// This mirrors the proven RAM layout of SI4684-FMDAB-Receiver and avoids two
// large adjacent heap allocations (sizeof(PNG) + JPEG workspace).
constexpr size_t DECODER_WORKSPACE_BYTES = 76800U;
static_assert(DECODER_WORKSPACE_BYTES == PngAdam7ArenaBytes,
              "All image decoders must share the same arena contract");
static_assert(sizeof(PNG) <= DECODER_WORKSPACE_BYTES,
              "PNG decoder does not fit in the shared workspace");
uint8_t* decoderWorkspace = nullptr;
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
  bool outputEnabled;
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

void jpegOutput(void* opaque, int32_t x, int32_t y, int32_t width,
                int32_t height, const uint16_t* pixels) {
  Adafruit_ST7735* display = static_cast<Adafruit_ST7735*>(opaque);
  if (display != nullptr && pixels != nullptr && width > 0 && height > 0) {
    display->drawRGBBitmap(x, y, pixels, width, height);
  }
  delay(0);
}

struct JpegProgress {
  uint32_t rows = 0U;
};

void jpegProgress(void* opaque) {
  if (opaque) ++static_cast<JpegProgress*>(opaque)->rows;
  yield();
}

void adam7Progress(void*) {
  yield();
}

int pngDraw(PNGDRAW* draw) {
  RenderContext* context = static_cast<RenderContext*>(draw->pUser);
  if (context == nullptr || context->png == nullptr ||
      context->sourceLine == nullptr) {
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
  if (!context->outputEnabled) return 1;
  if (context->display == nullptr) return 0;
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

void logResources(Stream* diagnostics, const char* phase) {
  constexpr unsigned internal8 = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  constexpr unsigned internal32 = MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT;
  logDecode(diagnostics,
            "[SLS/RAM] %s i8=%u/%u i32=%u/%u min8=%u stackHwm=%u",
            phase, static_cast<unsigned>(heap_caps_get_free_size(internal8)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(internal8)),
            static_cast<unsigned>(heap_caps_get_free_size(internal32)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(internal32)),
            static_cast<unsigned>(heap_caps_get_minimum_free_size(internal8)),
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

}  // namespace

bool initializeSlideshowRenderer(Stream* diagnostics) {
  if (decoderWorkspace == nullptr) {
    decoderWorkspace = static_cast<uint8_t*>(heap_caps_malloc(
        DECODER_WORKSPACE_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (decoderWorkspace == nullptr) {
    logDecode(diagnostics,
              "[SLS][ERROR] shared decoder workspace allocation failed: need=%u free=%u largest=%u",
              static_cast<unsigned>(DECODER_WORKSPACE_BYTES),
              ESP.getFreeHeap(),
              heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                               MALLOC_CAP_8BIT));
    return false;
  }
  // Allocation metrics belong to startup, not every subsequent render.
  static bool reported = false;
  if (!reported) {
    logDecode(diagnostics,
              "[SLS] shared decoder=%u PNG=%u Adam7=%u stripe=%u free=%u largest=%u",
              static_cast<unsigned>(DECODER_WORKSPACE_BYTES),
              static_cast<unsigned>(sizeof(PNG)),
              static_cast<unsigned>(PngAdam7WorkspaceBytes()),
              static_cast<unsigned>(PngAdam7TileBytes), ESP.getFreeHeap(),
              heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    reported = true;
  }
  return true;
}

bool renderRamSlideshow(Adafruit_ST7735& display, const uint8_t* data,
                        uint32_t length, int16_t x, int16_t y,
                        int16_t width, int16_t height,
                        Stream* diagnostics, bool* displayModified) {
  if (displayModified) *displayModified = false;
  if (data == nullptr || length < 8 || width <= 0 || height <= 0 ||
      width > 160 || height > 128 || length > PngAdam7MaxFileBytes) {
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
            static_cast<unsigned>(jpeg ? DECODER_WORKSPACE_BYTES : sizeof(PNG)),
            ESP.getFreeHeap(),
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  logResources(diagnostics, "before");
  if (jpeg) {
    JPEGImageInfo info;
    const JPEGPreflightResult preflight =
        JPEGpreflight(data, length, 160, 128, info);
    const bool supported =
        preflight == JPEGPreflightResult::SupportedBaseline ||
        preflight == JPEGPreflightResult::SupportedProgressive;
    // Decode the complete entropy stream without touching the display first.
    // A corrupt or unsupported object therefore leaves the previous pixels
    // intact. The same bounded workspace is reused for the render pass.
    JpegProgress validationProgress, renderProgress;
    const uint32_t validationStart = millis();
    const bool validated = supported && JPEGvalidate(
        data, length, 160, 128, decoderWorkspace, DECODER_WORKSPACE_BYTES,
        &info, jpegProgress, &validationProgress);
    const uint32_t validatedAt = millis();
    if (validated) {
      JPEGDisplay output(jpegOutput, &display);
      if (displayModified) *displayModified = true;
      display.fillScreen(ST77XX_BLACK);
      success = JPEGdecoder(data, length, output, 160, 128, decoderWorkspace,
                            DECODER_WORKSPACE_BYTES, &info,
                            jpegProgress, &renderProgress);
    }
    logDecode(diagnostics,
              "[SLS/JPEG] scans=%u renderedMCUrows=%d pumps=%lu/%lu validate=%lums render=%lums",
              info.scans, static_cast<int>(info.lastRenderedMcuRow)+1,
              static_cast<unsigned long>(validationProgress.rows),
              static_cast<unsigned long>(renderProgress.rows),
              static_cast<unsigned long>(validatedAt-validationStart),
              static_cast<unsigned long>(millis()-validatedAt));
    if (!success) {
      logDecode(diagnostics, "[SLS][WARN] JPEG rejected: %s size=%ux%u",
                JPEGpreflightName(preflight), info.width, info.height);
    }
  } else if (png && length >= 33U && data[28] == 1U) {
    // Decode to bounded stripes, never drawing sparse intermediate passes.
    // Reserve the tail of the SAME arena, disjoint from aligned inflater state.
    constexpr size_t stateCapacity = DECODER_WORKSPACE_BYTES - PngAdam7TileBytes;
    PngAdam7Info info;
    const uint32_t validationStart = millis();
    success = PngAdam7Decode(data, length, decoderWorkspace, stateCapacity,
                             PngAdam7MaxWidth, PngAdam7MaxHeight, info,
                             nullptr, nullptr, adam7Progress);
    const uint32_t validatedAt = millis();
    PngAdam7Tile tile;
    unsigned decodePasses = 1U;
    if (success) success = tile.fit(info.width, info.height, width, height);
    if (success) {
      tile.pixels = reinterpret_cast<uint16_t*>(decoderWorkspace + stateCapacity);
      const int destinationX = x + (width - static_cast<int>(tile.width)) / 2;
      const int destinationY = y + (height - static_cast<int>(tile.height)) / 2;
      // Published MOT bytes remain immutable for all passes. The pump only
      // yields; it never runs radio commands or invokes another image decoder.
      for (tile.top = 0; tile.top < tile.height; tile.top += tile.rows) {
        tile.rows = min(PngAdam7TileRows, tile.height - tile.top);
        memset(tile.pixels, 0, PngAdam7TileBytes);
        ++decodePasses;
        success = PngAdam7Decode(data, length, decoderWorkspace, stateCapacity,
                                  PngAdam7MaxWidth, PngAdam7MaxHeight, info,
                                  PngAdam7Tile::collect, &tile, adam7Progress);
        if (!success) break;
        if (tile.top == 0U) {
          if (displayModified) *displayModified = true;
          display.fillScreen(ST77XX_BLACK);
        }
        display.drawRGBBitmap(destinationX, destinationY + tile.top,
                              tile.pixels, tile.width, tile.rows);
        yield(); // outside Adafruit's SPI transaction
      }
    }
    logDecode(diagnostics,
              "[SLS/Adam7] %s %ux%u depth=%u type=%u stage=%s pass=%u row=%u runs=%u validate=%lums render=%lums",
              success ? "OK" : "rejected", info.width, info.height, info.depth,
              info.type, info.stage, info.pass, info.row, decodePasses,
              static_cast<unsigned long>(validatedAt-validationStart),
              static_cast<unsigned long>(millis()-validatedAt));
  } else if (png) {
    // First pass validates every compressed scanline without touching TFT.
    PNG* decoder = new (decoderWorkspace) PNG();
    if (decoder->openRAM(const_cast<uint8_t*>(data), length, pngDraw) ==
        PNG_SUCCESS) {
      const int sourceWidth = decoder->getWidth();
      const int sourceHeight = decoder->getHeight();
      if (sourceWidth > 0 && sourceHeight > 0 &&
          sourceWidth <= SLS_MAX_SOURCE_WIDTH) {
        context.sourceLine = pngSourceLine;
        context.png = decoder;
        context.outputEnabled = false;
        fitImage(context, sourceWidth, sourceHeight, x, y, width, height);
        success = decoder->decode(&context, 0) == PNG_SUCCESS;
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
    if (success) {
      if (displayModified) *displayModified = true;
      display.fillScreen(ST77XX_BLACK);
      decoder = new (decoderWorkspace) PNG();
      success = false;
      if (decoder->openRAM(const_cast<uint8_t*>(data), length, pngDraw) ==
          PNG_SUCCESS) {
        context.png = decoder;
        context.outputEnabled = true;
        success = decoder->decode(&context, 0) == PNG_SUCCESS;
        decoder->close();
      }
      decoder->~PNG();
    }
    context.sourceLine = nullptr;
  }

  logDecode(diagnostics,
            "[SLS] decode %s: %s, %lu ms, free=%u largest=%u",
            jpeg ? "JPEG" : (png ? "PNG" : "unknown"),
            success ? "OK" : "failed",
            static_cast<unsigned long>(millis() - startedAt), ESP.getFreeHeap(),
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  logResources(diagnostics, "after");
  decoderBusy = false;
  return success;
}
