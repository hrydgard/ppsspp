// ****************************************************************************
// * This file is part of the xBRZ project. It is distributed under           *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0         *
// * Copyright (C) Zenju (zenju AT gmx DOT de) - All Rights Reserved          *
// *                                                                          *
// * Additionally and as a special exception, the author gives permission     *
// * to link the code of this program with the following libraries            *
// * (or with modified versions that use the same licenses), and distribute   *
// * linked combinations including the two: MAME, FreeFileSync, Snes9x, ePSXe *
// * You must obey the GNU General Public License in all respects for all of  *
// * the code used other than MAME, FreeFileSync, Snes9x, ePSXe.              *
// * If you modify this file, you may extend this exception to your version   *
// * of the file, but you are not obligated to do so. If you do not wish to   *
// * do so, delete this exception statement from your version.                *
// ****************************************************************************

#ifndef XBRZ_HEADER_3847894708239054
#define XBRZ_HEADER_3847894708239054

#include <cstddef> //size_t
#include <cstdint> //uint32_t
#include <limits>
#include "xbrz_config.h"


namespace xbrz
{
/*
-------------------------------------------------------------------------
| xBRZ: "Scale by rules" - high quality image upscaling filter by Zenju |
-------------------------------------------------------------------------
using a modified approach of xBR:
http://board.byuu.org/viewtopic.php?f=10&t=2248
- new rule set preserving small image features
- highly optimized for performance
- support alpha channel
- support multithreading
- support 64-bit architectures
- support processing image slices
- support scaling up to 6xBRZ
*/

enum class ColorFormat //from high bits -> low bits, 8 bit per channel
{
    RGB,  //8 bit for each red, green, blue, upper 8 bits unused
    ARGB, //including alpha channel, BGRA byte order on little-endian machines
    ARGB_UNBUFFERED, //like ARGB, but without the one-time buffer creation overhead (ca. 100 - 300 ms) at the expense of a slightly slower scaling time
};

const int SCALE_FACTOR_MAX = 6;

/*
-> map source (srcWidth * srcHeight) to target (scale * width x scale * height) image, optionally processing a half-open slice of rows [yFirst, yLast) only
-> support for source/target pitch in bytes!
-> if your emulator changes only a few image slices during each cycle (e.g. DOSBox) then there's no need to run xBRZ on the complete image:
   Just make sure you enlarge the source image slice by 2 rows on top and 2 on bottom (this is the additional range the xBRZ algorithm is using during analysis)
   CAVEAT: If there are multiple changed slices, make sure they do not overlap after adding these additional rows in order to avoid a memory race condition
   in the target image data if you are using multiple threads for processing each enlarged slice!

THREAD-SAFETY: - parts of the same image may be scaled by multiple threads as long as the [yFirst, yLast) ranges do not overlap!
               - there is a minor inefficiency for the first row of a slice, so avoid processing single rows only; suggestion: process at least 8-16 rows
*/
void scale(size_t factor, //valid range: 2 - SCALE_FACTOR_MAX
           const uint32_t* src, uint32_t* trg, int srcWidth, int srcHeight,
           ColorFormat colFmt,
           const ScalerCfg& cfg = ScalerCfg(),
           int yFirst = 0, int yLast = std::numeric_limits<int>::max()); //slice of source image

void bilinearScale(const uint32_t* src, int srcWidth, int srcHeight,
                   /**/  uint32_t* trg, int trgWidth, int trgHeight);

void nearestNeighborScale(const uint32_t* src, int srcWidth, int srcHeight,
                          /**/  uint32_t* trg, int trgWidth, int trgHeight);


//parameter tuning
bool equalColorTest(uint32_t col1, uint32_t col2, ColorFormat colFmt, double luminanceWeight, double equalColorTolerance);
}

#endif
