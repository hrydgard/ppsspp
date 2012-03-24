#pragma once

typedef unsigned char uint8;

/* In etcdec.cxx */
void decompressBlockDiffFlip(unsigned int block_part1, unsigned int block_part2, uint8 *img,int width,int height,int startx,int starty);

// Writes RGBA output instead of RGB.
void DecompressBlock(const uint8 *compressed, uint8 *out, int out_width, int alpha=255);
