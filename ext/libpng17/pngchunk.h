#ifdef _MSC_VER
#pragma warning (disable:4028)
#endif

/* pngchunk.h - PNG chunk descriptions
 *
 * Last changed in libpng 1.7.0 [(PENDING RELEASE)]
 * Copyright (c) 2016 Glenn Randers-Pehrson
 *
 * This code is released under the libpng license.
 * For conditions of distribution and use, see the disclaimer
 * and license in png.h
 *
 * If this list is changed in any way scripts/chunkhash.c must be rebuilt and
 * run to regenerate the lookup functions for the tables described from this
 * list.
 *
 * IDAT MUST be first in the list; it must have index '0'.  The order of the
 * remaining chunks comes from section 5.6 "Chunk ordering" in the ISO spec
 * plus the ordering rules in the PNG extensions documnet.
 *
 * Keep PNG_CHUNK_BEGIN and PNG_CHUNK_END at the beginning and end.
 */
PNG_CHUNK_BEGIN(IDAT,  73,  68,  65,  84, within_IDAT,  after_start)
PNG_CHUNK(      IHDR,  73,  72,  68,  82, before_start, at_start)
PNG_CHUNK(      cHRM,  99,  72,  82,  77, before_PLTE,  after_start)
PNG_CHUNK(      gAMA, 103,  65,  77,  65, before_PLTE,  after_start)
PNG_CHUNK(      iCCP, 105,  67,  67,  80, before_PLTE,  after_start)
PNG_CHUNK(      sBIT, 115,  66,  73,  84, before_PLTE,  after_start)
PNG_CHUNK(      sRGB, 115,  82,  71,  66, before_PLTE,  after_start)
PNG_CHUNK(      PLTE,  80,  76,  84,  69, before_PLTE,  after_start)
PNG_CHUNK(      bKGD,  98,  75,  71,  68, before_IDAT,  after_PLTE)
PNG_CHUNK(      hIST, 104,  73,  83,  84, before_IDAT,  after_PLTE)
PNG_CHUNK(      tRNS, 116,  82,  78,  83, before_IDAT,  after_PLTE)
PNG_CHUNK(      oFFs, 111,  70,  70, 115, before_IDAT,  after_start)
PNG_CHUNK(      pCAL, 112,  67,  65,  76, before_IDAT,  after_start)
PNG_CHUNK(      sCAL, 115,  67,  65,  76, before_IDAT,  after_start)
PNG_CHUNK(      sTER, 115,  84,  69,  82, before_IDAT,  after_start)
PNG_CHUNK(      pHYs, 112,  72,  89, 115, before_IDAT,  after_start)
PNG_CHUNK(      sPLT, 115,  80,  76,  84, before_IDAT,  after_start)
PNG_CHUNK(      tIME, 116,  73,  77,  69, before_end,   after_start)
PNG_CHUNK(      iTXt, 105,  84,  88, 116, before_end,   after_start)
PNG_CHUNK(      tEXt, 116,  69,  88, 116, before_end,   after_start)
PNG_CHUNK(      zTXt, 122,  84,  88, 116, before_end,   after_start)
PNG_CHUNK(      fRAc, 102,  82,  65,  99, before_end,   after_start)
PNG_CHUNK(      gIFg, 103,  73,  70, 103, before_end,   after_start)
PNG_CHUNK(      gIFt, 103,  73,  70, 116, before_end,   after_start)
PNG_CHUNK(      gIFx, 103,  73,  70, 120, before_end,   after_start)
PNG_CHUNK(      dSIG, 100,  83,  73,  71, before_end,   after_start)
PNG_CHUNK_END(  IEND,  73,  69,  78,  68, before_end,   after_IDAT)
