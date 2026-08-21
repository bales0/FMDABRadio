/*
 * RAM slideshow renderer.
 *
 * Compressed JPEG/PNG bytes stay in the radio driver's fixed MOT arena.
 * Decoder state and the PNG scanline also use fixed workspaces; rendering
 * therefore performs no slideshow-related heap allocations. Pixels stream
 * directly to the ST7735 without a full-screen RGB565 framebuffer.
 */
#ifndef FMDABRADIO_SLIDESHOW_H
#define FMDABRADIO_SLIDESHOW_H

#include <Adafruit_ST7735.h>
#include <Arduino.h>

// Allocates one permanent decoder workspace during startup. It is deliberately
// never released, so repeated slideshow rendering cannot fragment the heap.
bool initializeSlideshowRenderer(Stream* diagnostics = nullptr);

bool renderRamSlideshow(Adafruit_ST7735& display, const uint8_t* data,
                        uint32_t length, int16_t x, int16_t y,
                        int16_t width, int16_t height,
                        Stream* diagnostics = nullptr);

#endif
