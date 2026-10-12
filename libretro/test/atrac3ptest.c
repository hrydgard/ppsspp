/* Copyright (c) 2012- PPSSPP Project.
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.0 or later versions.
 */

#include <stdio.h>
#include <formats/ratrac3p.h>
static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
/* Mono, one coded QU, master code-table mode 3 with no indices.
 * Two frames decode to silence in the reference decoder. */
static void master_table_default(void)
{
   static const uint8_t frame[16] = {
      0x00, 0x04, 0x00, 0xC0, 0x30
   };
   float pcm[RATRAC3P_SAMPLES];
   int16_t pcm16[RATRAC3P_SAMPLES];
   ratrac3p_t *d = ratrac3p_new(1, sizeof(frame));
   unsigned i, f;
   CHECK(d != NULL, "master default: allocate");
   if (!d)
      return;
   for (f = 0; f < 2; f++)
   {
      CHECK(ratrac3p_decode_float(d, frame, sizeof(frame), pcm)
            == RATRAC3P_SAMPLES, "master default: frame %u", f);
      for (i = 0; i < RATRAC3P_SAMPLES; i++)
         if (pcm[i] != 0.0f)
            break;
      CHECK(i == RATRAC3P_SAMPLES, "master default: not silent");
   }
   ratrac3p_reset(d);
   CHECK(ratrac3p_decode_s16(d, frame, sizeof(frame), pcm16)
         == RATRAC3P_SAMPLES, "master default: s16");
   for (i = 0; i < RATRAC3P_SAMPLES; i++)
      if (pcm16[i] != 0)
         break;
   CHECK(i == RATRAC3P_SAMPLES, "master default: s16 not silent");
   ratrac3p_free(d);
}

int main(void) {
   master_table_default();
   printf("ATRAC3plus: %d failures\n", failures);
   return failures ? 1 : 0;
}
