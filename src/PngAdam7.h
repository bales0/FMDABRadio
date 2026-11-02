// SPDX-License-Identifier: GPL-3.0-only
// Adapted from SI4684-FMDAB-Receiver 4a4b2d2, src/png_adam7.h.
#pragma once
#include "DataBytes.h"

constexpr unsigned PngAdam7MaxWidth = 320U;
constexpr unsigned PngAdam7MaxHeight = 320U;
constexpr size_t PngAdam7MaxFileBytes = 51200U;
constexpr size_t PngAdam7ArenaBytes = 76800U;
constexpr unsigned PngAdam7TileRows = 64U;
constexpr size_t PngAdam7TileBytes = 160U * PngAdam7TileRows * sizeof(uint16_t);

struct PngAdam7Info {
  unsigned width=0,height=0,depth=0,type=0;
  const char* stage="entry";
  const char* detail="";
  unsigned pass=0,row=0,input=0,output=0,padding=0;
};
typedef bool (*PngAdam7Row)(void*,unsigned x,unsigned y,unsigned step,
                           const uint16_t* pixels,unsigned count);
typedef void (*PngAdam7Pump)(void*);
// Returns state size excluding alignment padding (at most alignof(state)-1).
size_t PngAdam7WorkspaceBytes();
// Null row callback performs full validation with no output. Supported color
// types: 0/2/3/4/6, legal 1/2/4/8/16-bit combinations. Alpha is over black.
// State and inflate dictionary are placement-constructed in caller RAM.
bool PngAdam7Decode(radio_data::Bytes input,size_t size,void* workspace,
                   size_t capacity,unsigned maxWidth,unsigned maxHeight,
                   PngAdam7Info& info,PngAdam7Row row=nullptr,
                   void* context=nullptr,PngAdam7Pump pump=nullptr);
