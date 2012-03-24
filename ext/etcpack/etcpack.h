#pragma once

#define PERCEPTUAL_WEIGHT_R_SQUARED 0.299
#define PERCEPTUAL_WEIGHT_G_SQUARED 0.587
#define PERCEPTUAL_WEIGHT_B_SQUARED 0.114

typedef unsigned char uint8;

// These functions take RGB888.
void compressBlockDiffFlipSlow(uint8 *img,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2);

void compressBlockDiffFlipMedium(uint8 *img,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2);

void compressBlockDiffFlipFast(uint8 *img, uint8 *imgdec,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2);

void compressBlockDiffFlipSlowPerceptual(uint8 *img,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2);

void compressBlockDiffFlipMediumPerceptual(uint8 *img,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2);

void compressBlockDiffFlipFastPerceptual(uint8 *img, uint8 *imgdec,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2);


// This one takes RGBA8888 and converts to RGB first.
// Writes 64 bits of compressed data to output[0..7].
// Quality is 0 to 5. The odd numbers use perceptual metrics.
void CompressBlock(const uint8 *in, int in_width, uint8 *output, int quality);
