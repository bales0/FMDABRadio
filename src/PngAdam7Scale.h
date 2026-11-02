// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "PngAdam7.h"

// One fully assembled, scaled stripe. It is never published until all seven
// Adam7 passes and the zlib trailer have been verified for this decode pass.
struct PngAdam7Tile {
  uint16_t* pixels = nullptr;
  unsigned sourceWidth=0,sourceHeight=0,width=0,height=0,top=0,rows=0;

  bool fit(unsigned sw,unsigned sh,unsigned boxWidth,unsigned boxHeight) {
    if (!sw || !sh || sw>PngAdam7MaxWidth || sh>PngAdam7MaxHeight ||
        !boxWidth || !boxHeight || boxWidth>160U || boxHeight>128U) return false;
    sourceWidth=sw;sourceHeight=sh;
    if (sw*boxHeight>sh*boxWidth) {
      width=boxWidth;height=sh*boxWidth/sw;
    } else {
      height=boxHeight;width=sw*boxHeight/sh;
    }
    if (!width) width=1;
    if (!height) height=1;
    return true;
  }

  static bool collect(void* context,unsigned x,unsigned y,unsigned step,
                      const uint16_t* input,unsigned count) {
    PngAdam7Tile& tile=*static_cast<PngAdam7Tile*>(context);
    if (!tile.pixels || !input || !step || !count || !tile.width ||
        !tile.height || tile.rows>PngAdam7TileRows ||
        tile.top>tile.height || tile.rows>tile.height-tile.top) return false;
    for (unsigned row=0;row<tile.rows;++row) {
      if ((tile.top+row)*tile.sourceHeight/tile.height!=y) continue;
      for (unsigned column=0;column<tile.width;++column) {
        const unsigned sourceX=column*tile.sourceWidth/tile.width;
        if (sourceX<x || (sourceX-x)%step) continue;
        const unsigned index=(sourceX-x)/step;
        if (index<count) tile.pixels[row*tile.width+column]=input[index];
      }
    }
    return true;
  }
};
