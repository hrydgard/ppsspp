#pragma once

#include <base/basictypes.h>

// Easy curve computation for fades etc.


// output range: [0.0, 1.0]
float linearInOut(int t, int fadeInLength, int solidLength, int fadeOutLength);
float linearIn(int t, int fadeInLength);
float linearOut(int t, int fadeInLength);

// smooth operator [0, 1] -> [0, 1]
float ease(float val);
float ease(int t, int fadeLength);

// need a bouncy ease

// waveforms [0, 1]
float sawtooth(int t, int period);

// output range: -1.0 to 1.0
float passWithPause(int t, int fadeInLength, int pauseLength, int fadeOutLength);
