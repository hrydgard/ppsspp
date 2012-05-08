#ifndef _MATHUTILS_H
#define _MATHUTILS_H

#include <math.h>
#include <string.h>

typedef unsigned short float16;

// This ain't a 1.5.10 float16, it's a stupid hack format where we chop 16 bits off a float.
// This choice is subject to change. Don't think I'm using this for anything at all now anyway.
// DEPRECATED
inline float16 FloatToFloat16(float x) {
  int ix;
  memcpy(&ix, &x, sizeof(float));
  return ix >> 16;
}

inline float Float16ToFloat(float16 ix) {
  float x;
  memcpy(&x, &ix, sizeof(float));
  return x;
}


#define PI 3.141592653589793f

// The stuff in this file is from all over the web, esp. dspmusic.org. I think it's all public domain.
// In any case, very little of it is used anywhere at the moment.

// PM modulated sine
inline float sine(float t,float f,float ph,float fm) {
  return sinf((t*f+ph)*2*PI + 0.5f*PI*fm*(1 - sqrt(f*2)));
}

//fb := feedback (0 to 1) (1 max saw)

inline float saw(float t,float f,float ph, float fm, float fb = 1.0f)
{
  return sine(t,f,ph,fb*sine(t-1.0f,f,ph,fm));
}

//	pm := pulse mod (0 to 1) (1 max pulse)
//	pw := pulse width (0 to 1) (1 square)
inline float pulse(float t,float f,float ph,float fm,float fb,float pm,float pw) {
  return saw(t,f,ph,fm,fb) - saw(t,f,ph+0.5f*pw,fm,fb) * pm;
}

// Calculate pseudo-random 32 bit number based on linear congruential method. 
void SetSeed(unsigned int seed);
unsigned int GenerateRandomNumber();
inline float GenerateRandomFloat01() {
  return (float)((double)GenerateRandomNumber() / 0xFFFFFFFF);
}
inline float GenerateRandomSignedFloat() {
  return (float)((double)GenerateRandomNumber() / 0x80000000) - 1.0f;
}


inline float GaussRand()
{
  float R1 = GenerateRandomFloat01();
  float R2 = GenerateRandomFloat01();

  float X = sqrtf( -2.0f * logf(R1)) * cosf(2.0f * PI * R2);
  if (X > 4.0f) X = 4.0f;
  if (X < -4.0f) X = -4.0f;
  return X;
}

// Accuracy unknown
inline double atan_fast(double x) {
  return (x / (1.0 + 0.28 * (x * x)));
}


// linear -> dB conversion
inline float lin2dB(float lin) {
  const float LOG_2_DB = 8.6858896380650365530225783783321f;	// 20 / ln( 10 )
  return log(lin) * LOG_2_DB;
}

// dB -> linear conversion
inline float dB2lin(float dB) {
  const float DB_2_LOG = 0.11512925464970228420089957273422f;	// ln( 10 ) / 20
  return exp(dB * DB_2_LOG);
}

#endif
