// ****************************************************************************
// * This file is part of the HqMAME project. It is distributed under         *
// * GNU General Public License: http://www.gnu.org/licenses/gpl.html         *
// * Copyright (C) Zenju (zenju AT gmx DOT de) - All Rights Reserved          *
// *                                                                          *
// * Additionally and as a special exception, the author gives permission     *
// * to link the code of this program with the MAME library (or with modified *
// * versions of MAME that use the same license as MAME), and distribute      *
// * linked combinations including the two. You must obey the GNU General     *
// * Public License in all respects for all of the code used other than MAME. *
// * If you modify this file, you may extend this exception to your version   *
// * of the file, but you are not obligated to do so. If you do not wish to   *
// * do so, delete this exception statement from your version.                *
// ****************************************************************************

#ifndef XBRZ_HEADER_3847894708239054
#define XBRZ_HEADER_3847894708239054

#include <cstddef> //size_t
#ifdef __SYMBIAN32__
#include <libc/sys/config.h>
typedef __uint32_t uint32_t;
#elif defined(IOS)
#include <stdint.h>
#else
#include <cstdint> //uint32_t
#endif
#include <limits>
#include "config.h"

namespace xbrz
{
/*
-------------------------------------------------------------------------
| xBRZ: "Scale by rules" - high quality image upscaling filter by Zenju |
-------------------------------------------------------------------------
using a modified approach of xBR:
http://board.byuu.org/viewtopic.php?f=10&t=2248
- new rule set preserving small image features
- support multithreading
- support 64 bit architectures
*/

/*
-> map source (srcWidth * srcHeight) to target (scale * width x scale * height) image, optionally processing rows [yFirst, yLast) only
-> color format: ARGB (BGRA byte order)
-> optional source/target pitch in bytes!

THREAD-SAFETY: - parts of the same image may be scaled by multiple threads as long as the [yFirst, yLast) ranges do not overlap!
               - there is a minor inefficiency for the first row of a slice, so avoid processing single rows only
*/
void scale(size_t factor, //valid range: 2 - 5
           const uint32_t* src, uint32_t* trg, int srcWidth, int srcHeight,
           const ScalerCfg& cfg = ScalerCfg(),
           int yFirst = 0, int yLast = std::numeric_limits<int>::max()); //slice of source image

void nearestNeighborScale(const uint32_t* src, int srcWidth, int srcHeight,
                          uint32_t* trg, int trgWidth, int trgHeight);

enum SliceType
{
    NN_SCALE_SLICE_SOURCE,
    NN_SCALE_SLICE_TARGET,
};
void nearestNeighborScale(const uint32_t* src, int srcWidth, int srcHeight, int srcPitch, //pitch in bytes!
                          uint32_t* trg, int trgWidth, int trgHeight, int trgPitch,
                          SliceType st, int yFirst, int yLast);

//parameter tuning
bool equalColor(uint32_t col1, uint32_t col2, double luminanceWeight, double equalColorTolerance);





//########################### implementation ###########################
inline
void nearestNeighborScale(const uint32_t* src, int srcWidth, int srcHeight,
                          uint32_t* trg, int trgWidth, int trgHeight)
{
    nearestNeighborScale(src, srcWidth, srcHeight, srcWidth * sizeof(uint32_t),
                         trg, trgWidth, trgHeight, trgWidth * sizeof(uint32_t),
                         NN_SCALE_SLICE_TARGET, 0, trgHeight);
}
}

#endif
