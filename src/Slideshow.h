/*
 * RAM slideshow renderer.
 *
 * Compressed JPEG/PNG bytes stay in the radio driver's MOT arena. Decoders
 * stream small source blocks/scanlines directly to the ST7735; there is no
 * slideshow file and no full-screen RGB565 framebuffer.
 */
#ifndef FMDABRADIO_SLIDESHOW_H
#define FMDABRADIO_SLIDESHOW_H

#include <Adafruit_ST7735.h>
#include <Arduino.h>

bool renderRamSlideshow(Adafruit_ST7735& display, const uint8_t* data,
                        uint32_t length, int16_t x, int16_t y,
                        int16_t width, int16_t height,
                        Stream* diagnostics = nullptr);

#endif
