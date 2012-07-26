#pragma once

// Implementation of "Improved Noise"
// http://mrl.nyu.edu/~perlin/noise/ 
// doubles are only used at the very start, not a big performance worry
float Noise(double x, double y, double z);
