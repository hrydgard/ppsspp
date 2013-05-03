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

#include "xbrz.h"
#include <cmath>
#include <cassert>
#include <algorithm>
#include <limits>

namespace
{
template <uint32_t N> inline
unsigned char getByte(uint32_t val) { return static_cast<unsigned char>((val >> (8 * N)) & 0xff); }

// adjusted for RGBA
// - Durante
inline unsigned char getRed  (uint32_t val) { return getByte<0>(val); }
inline unsigned char getGreen(uint32_t val) { return getByte<1>(val); }
inline unsigned char getBlue (uint32_t val) { return getByte<2>(val); }
inline unsigned char getAlpha(uint32_t val) { return getByte<3>(val); }

template <class T> inline
T abs(T value)
{
	static_assert(std::numeric_limits<T>::is_signed, "abs performed on unsigned");
	return value < 0 ? -value : value;
}

const uint32_t redMask   = 0x00ff0000;
const uint32_t greenMask = 0x0000ff00;
const uint32_t blueMask  = 0x000000ff;

template <unsigned int N, unsigned int M> inline
void alphaBlend(uint32_t& dst, uint32_t col) //blend color over destination with opacity N / M
{
	static_assert(N < 256, "possible overflow of (col & redMask) * N");
	static_assert(M < 256, "possible overflow of (col & redMask  ) * N + (dst & redMask  ) * (M - N)");
	static_assert(0 < N && N < M, "");
	//dst = (redMask   & ((col & redMask  ) * N + (dst & redMask  ) * (M - N)) / M) | //this works because 8 upper bits are free
	//      (greenMask & ((col & greenMask) * N + (dst & greenMask) * (M - N)) / M) |
	//      (blueMask  & ((col & blueMask ) * N + (dst & blueMask ) * (M - N)) / M);

	// the upper 8 bits are not free in our case, so we need to do this differently
	// could probably be MUCH faster
	// - Durante
	uint8_t a = (((col	          ) >> 24) * N + ((dst	          ) >> 24) * (M - N) ) / M;
	uint8_t r = (((col &   redMask) >> 16) * N + ((dst &   redMask) >> 16) * (M - N) ) / M;
	uint8_t g = (((col & greenMask) >>  8) * N + ((dst & greenMask) >>  8) * (M - N) ) / M;
	uint8_t b = (((col &  blueMask)      ) * N + ((dst &  blueMask)      ) * (M - N) ) / M;

	dst = (a << 24) | (r << 16) | (g << 8) | (b << 0); 
}

inline
uint32_t alphaBlend2(uint32_t pix1, uint32_t pix2, double alpha)
{
	return (redMask   & static_cast<uint32_t>((pix1 & redMask  ) * alpha + (pix2 & redMask  ) * (1 - alpha))) |
	       (greenMask & static_cast<uint32_t>((pix1 & greenMask) * alpha + (pix2 & greenMask) * (1 - alpha))) |
	       (blueMask  & static_cast<uint32_t>((pix1 & blueMask ) * alpha + (pix2 & blueMask ) * (1 - alpha)));
}


uint32_t*       byteAdvance(      uint32_t* ptr, int bytes) {  return reinterpret_cast<	     uint32_t*>(reinterpret_cast<      char*>(ptr) + bytes); }
const uint32_t* byteAdvance(const uint32_t* ptr, int bytes) {  return reinterpret_cast<const uint32_t*>(reinterpret_cast<const char*>(ptr) + bytes); }


//fill block  with the given color
inline
void fillBlock(uint32_t* trg, int pitch, uint32_t col, int blockWidth, int blockHeight)
{
	//for (int y = 0; y < blockHeight; ++y, trg = byteAdvance(trg, pitch))
	//	std::fill(trg, trg + blockWidth, col);

	for (int y = 0; y < blockHeight; ++y, trg = byteAdvance(trg, pitch))
		for (int x = 0; x < blockWidth; ++x)
			trg[x] = col;
}

inline
void fillBlock(uint32_t* trg, int pitch, uint32_t col, int n) { fillBlock(trg, pitch, col, n, n); }


#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#elif defined __GNUC__
#define FORCE_INLINE __attribute__((always_inline)) inline
#else
#define FORCE_INLINE inline
#endif


enum RotationDegree //clock-wise
{
	ROT_0,
	ROT_90,
	ROT_180,
	ROT_270
};

//calculate input matrix coordinates after rotation at compile time
template <RotationDegree rotDeg, size_t I, size_t J, size_t N>
struct MatrixRotation;

template <size_t I, size_t J, size_t N>
struct MatrixRotation<ROT_0, I, J, N>
{
	static const size_t I_old = I;
	static const size_t J_old = J;
};

template <RotationDegree rotDeg, size_t I, size_t J, size_t N> //(i, j) = (row, col) indices, N = size of (square) matrix
struct MatrixRotation
{
	static const size_t I_old = N - 1 - MatrixRotation<static_cast<RotationDegree>(rotDeg - 1), I, J, N>::J_old; //old coordinates before rotation!
	static const size_t J_old =	        MatrixRotation<static_cast<RotationDegree>(rotDeg - 1), I, J, N>::I_old; //
};


template <size_t N, RotationDegree rotDeg>
class OutputMatrix
{
public:
	OutputMatrix(uint32_t* out, int outWidth) : //access matrix area, top-left at position "out" for image with given width
		out_(out),
		outWidth_(outWidth) {}

	template <size_t I, size_t J>
	uint32_t& ref() const
	{
		static const size_t I_old = MatrixRotation<rotDeg, I, J, N>::I_old;
		static const size_t J_old = MatrixRotation<rotDeg, I, J, N>::J_old;
		return *(out_ + J_old + I_old * outWidth_);
	}

private:
	uint32_t* out_;
	const int outWidth_;
};


template <class T> inline
T square(T value) { return value * value; }


/*
inline
void rgbtoLuv(uint32_t c, double& L, double& u, double& v)
{
	//http://www.easyrgb.com/index.php?X=MATH&H=02#text2
	double r = getRed  (c) / 255.0;
	double g = getGreen(c) / 255.0;
	double b = getBlue (c) / 255.0;

	if ( r > 0.04045 )
		r = std::pow(( ( r + 0.055 ) / 1.055 ) , 2.4);
	else
		r /= 12.92;
	if ( g > 0.04045 )
		g = std::pow(( ( g + 0.055 ) / 1.055 ) , 2.4);
	else
		g /=  12.92;
	if ( b > 0.04045 )
		b  = std::pow(( ( b + 0.055 ) / 1.055 ) , 2.4);
	else
		b /=  12.92;

	r *= 100;
	g *= 100;
	b *= 100;

	double x = 0.4124564 * r + 0.3575761 * g + 0.1804375 * b;
	double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
	double z = 0.0193339 * r + 0.1191920 * g + 0.9503041 * b;
	//---------------------
	double var_U =  4 * x  / ( x +  15 * y  +  3 * z  );
	double var_V =  9 * y  / ( x +  15 * y  +  3 * z  );
	double var_Y = y / 100;

	if ( var_Y > 0.008856 ) var_Y = std::pow(var_Y , 1.0/3 );
	else					var_Y =  7.787 * var_Y  +  16.0 / 116;

	const double ref_X =  95.047;		//Observer= 2°, Illuminant= D65
	const double ref_Y = 100.000;
	const double ref_Z = 108.883;

	const double ref_U = ( 4 * ref_X ) / ( ref_X + ( 15 * ref_Y ) + ( 3 * ref_Z ) );
	const double ref_V = ( 9 * ref_Y ) / ( ref_X + ( 15 * ref_Y ) + ( 3 * ref_Z ) );

	L = ( 116 * var_Y ) - 16;
	u = 13 * L * ( var_U - ref_U );
	v = 13 * L * ( var_V - ref_V );
}
*/

inline
void rgbtoLab(uint32_t c, unsigned char& L, signed char& A, signed char& B)
{
	//code: http://www.easyrgb.com/index.php?X=MATH
	//test: http://www.workwithcolor.com/color-converter-01.htm
	//------RGB to XYZ------
	double r = getRed  (c) / 255.0;
	double g = getGreen(c) / 255.0;
	double b = getBlue (c) / 255.0;

	r = r > 0.04045 ? std::pow(( r + 0.055 ) / 1.055, 2.4) : r / 12.92;
	r = g > 0.04045 ? std::pow(( g + 0.055 ) / 1.055, 2.4) : g / 12.92;
	r = b > 0.04045 ? std::pow(( b + 0.055 ) / 1.055, 2.4) : b / 12.92;

	r *= 100;
	g *= 100;
	b *= 100;

	double x = 0.4124564 * r + 0.3575761 * g + 0.1804375 * b;
	double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
	double z = 0.0193339 * r + 0.1191920 * g + 0.9503041 * b;
	//------XYZ to Lab------
	const double refX = 95.047;  //
	const double refY = 100.000; //Observer= 2°, Illuminant= D65
	const double refZ = 108.883; //
	double var_X = x / refX;
	double var_Y = y / refY;
	double var_Z = z / refZ;

	var_X = var_X > 0.008856 ? std::pow(var_X, 1.0 / 3) : 7.787 * var_X + 4.0 / 29;
	var_Y = var_Y > 0.008856 ? std::pow(var_Y, 1.0 / 3) : 7.787 * var_Y + 4.0 / 29;
	var_Z = var_Z > 0.008856 ? std::pow(var_Z, 1.0 / 3) : 7.787 * var_Z + 4.0 / 29;

	L = static_cast<unsigned char>(116 * var_Y  - 16);
	A = static_cast<  signed char>(500 * (var_X - var_Y));
	B = static_cast<  signed char>(200 * (var_Y - var_Z));
};


inline
double distLAB(uint32_t pix1, uint32_t pix2)
{
	unsigned char L1 = 0; //[0, 100]
	signed   char a1 = 0; //[-128, 127]
	signed   char b1 = 0; //[-128, 127]
	rgbtoLab(pix1, L1, a1, b1);

	unsigned char L2 = 0;
	signed   char a2 = 0;
	signed   char b2 = 0;
	rgbtoLab(pix2, L2, a2, b2);

	//-----------------------------
	//http://www.easyrgb.com/index.php?X=DELT

	//Delta E/CIE76
	return std::sqrt(square(1.0 * L1 - L2) +
					 square(1.0 * a1 - a2) +
					 square(1.0 * b1 - b2));
}


/*
inline
void rgbtoHsl(uint32_t c, double& h, double& s, double& l)
{
	//http://www.easyrgb.com/index.php?X=MATH&H=18#text18
	const int r = getRed  (c);
	const int g = getGreen(c);
	const int b = getBlue (c);

	const int varMin = numeric::min(r, g, b);
	const int varMax = numeric::max(r, g, b);
	const int delMax = varMax - varMin;

	l = (varMax + varMin) / 2.0 / 255.0;

	if (delMax == 0) //gray, no chroma...
	{
		h = 0;
		s = 0;
	}
	else
	{
		s = l < 0.5 ?
			delMax / (1.0 * varMax + varMin) :
			delMax / (2.0 * 255 - varMax - varMin);

		double delR = ((varMax - r) / 6.0 + delMax / 2.0) / delMax;
		double delG = ((varMax - g) / 6.0 + delMax / 2.0) / delMax;
		double delB = ((varMax - b) / 6.0 + delMax / 2.0) / delMax;

		if (r == varMax)
			h = delB - delG;
		else if (g == varMax)
			h = 1 / 3.0 + delR - delB;
		else if (b == varMax)
			h = 2 / 3.0 + delG - delR;

		if (h < 0)
			h += 1;
		if (h > 1)
			h -= 1;
	}
}

inline
double distHSL(uint32_t pix1, uint32_t pix2, double lightningWeight)
{
	double h1 = 0;
	double s1 = 0;
	double l1 = 0;
	rgbtoHsl(pix1, h1, s1, l1);
	double h2 = 0;
	double s2 = 0;
	double l2 = 0;
	rgbtoHsl(pix2, h2, s2, l2);

	//HSL is in cylindric coordinatates where L represents height, S radius, H angle,
	//however we interpret the cylinder as a bi-conic solid with top/bottom radius 0, middle radius 1
	assert(0 <= h1 && h1 <= 1);
	assert(0 <= h2 && h2 <= 1);

	double r1 = l1 < 0.5 ?
				l1 * 2 :
				2 - l1 * 2;

	double x1 = r1 * s1 * std::cos(h1 * 2 * numeric::pi);
	double y1 = r1 * s1 * std::sin(h1 * 2 * numeric::pi);
	double z1 = l1;

	double r2 = l2 < 0.5 ?
				l2 * 2 :
				2 - l2 * 2;

	double x2 = r2 * s2 * std::cos(h2 * 2 * numeric::pi);
	double y2 = r2 * s2 * std::sin(h2 * 2 * numeric::pi);
	double z2 = l2;

	return 255 * std::sqrt(square(x1 - x2) + square(y1 - y2) +  square(lightningWeight * (z1 - z2)));
}
*/


inline
double distRGB(uint32_t pix1, uint32_t pix2)
{
	const double r_diff = static_cast<int>(getRed  (pix1)) - getRed  (pix2);
	const double g_diff = static_cast<int>(getGreen(pix1)) - getGreen(pix2);
	const double b_diff = static_cast<int>(getBlue (pix1)) - getBlue (pix2);

	//euklidean RGB distance
	return std::sqrt(square(r_diff) + square(g_diff) + square(b_diff));
}


inline
double distNonLinearRGB(uint32_t pix1, uint32_t pix2)
{
	//non-linear rgb: http://www.compuphase.com/cmetric.htm
	const double r_diff = static_cast<int>(getRed  (pix1)) - getRed  (pix2);
	const double g_diff = static_cast<int>(getGreen(pix1)) - getGreen(pix2);
	const double b_diff = static_cast<int>(getBlue (pix1)) - getBlue (pix2);

	const double r_avg = (static_cast<double>(getRed(pix1)) + getRed(pix2)) / 2;
	return std::sqrt((2 + r_avg / 255) * square(r_diff) + 4 * square(g_diff) + (2 + (255 - r_avg) / 255) * square(b_diff));
}


inline
double distYCbCr(uint32_t pix1, uint32_t pix2, double lumaWeight)
{
	//http://en.wikipedia.org/wiki/YCbCr#ITU-R_BT.601_conversion
	//YCbCr conversion is a matrix multiplication => take advantage of linearity by subtracting first!
	const int r_diff = static_cast<int>(getRed  (pix1)) - getRed  (pix2); //we may delay division by 255 to after matrix multiplication
	const int g_diff = static_cast<int>(getGreen(pix1)) - getGreen(pix2); //
	const int b_diff = static_cast<int>(getBlue (pix1)) - getBlue (pix2); //substraction for int is noticeable faster than for double!

	const double k_b = 0.0722; //ITU-R BT.709 conversion
	const double k_r = 0.2126; //
	const double k_g = 1 - k_b - k_r;

	const double scale_b = 0.5 / (1 - k_b);
	const double scale_r = 0.5 / (1 - k_r);

	const double y   = k_r * r_diff + k_g * g_diff + k_b * b_diff; //[!], analog YCbCr!
	const double c_b = scale_b * (b_diff - y);
	const double c_r = scale_r * (r_diff - y);

	//we skip division by 255 to have similar range like other distance functions
	return std::sqrt(square(lumaWeight * y) + square(c_b) +  square(c_r));
}

// distance function taking alpha distance into account
inline
double distYCbCrA(uint32_t pix1, uint32_t pix2, double lumaWeight)
{
	//http://en.wikipedia.org/wiki/YCbCr#ITU-R_BT.601_conversion
	//YCbCr conversion is a matrix multiplication => take advantage of linearity by subtracting first!
	const int r_diff = static_cast<int>(getRed  (pix1)) - getRed  (pix2); //we may delay division by 255 to after matrix multiplication
	const int g_diff = static_cast<int>(getGreen(pix1)) - getGreen(pix2); //
	const int b_diff = static_cast<int>(getBlue (pix1)) - getBlue (pix2); //substraction for int is noticeable faster than for double!

	const double k_b = 0.0722; //ITU-R BT.709 conversion
	const double k_r = 0.2126; //
	const double k_g = 1 - k_b - k_r;

	const double scale_b = 0.5 / (1 - k_b);
	const double scale_r = 0.5 / (1 - k_r);

	const double y   = k_r * r_diff + k_g * g_diff + k_b * b_diff; //[!], analog YCbCr!
	const double c_b = scale_b * (b_diff - y);
	const double c_r = scale_r * (r_diff - y);

	//we skip division by 255 to have similar range like other distance functions
	return std::sqrt(square(lumaWeight * y) + square(c_b) +  square(c_r)+ square(static_cast<int>(getAlpha(pix1)) - getAlpha(pix2)));
}


inline
double distYUV(uint32_t pix1, uint32_t pix2, double luminanceWeight)
{
	//perf: it's not worthwhile to buffer the YUV-conversion, the direct code is faster by ~ 6%
	//since RGB -> YUV conversion is essentially a matrix multiplication, we can calculate the RGB diff before the conversion (distributive property)
	const double r_diff = static_cast<int>(getRed  (pix1)) - getRed  (pix2);
	const double g_diff = static_cast<int>(getGreen(pix1)) - getGreen(pix2);
	const double b_diff = static_cast<int>(getBlue (pix1)) - getBlue (pix2);

	//http://en.wikipedia.org/wiki/YUV#Conversion_to.2Ffrom_RGB
	const double w_b = 0.114;
	const double w_r = 0.299;
	const double w_g = 1 - w_r - w_b;

	const double u_max = 0.436;
	const double v_max = 0.615;

	const double scale_u = u_max / (1 - w_b);
	const double scale_v = v_max / (1 - w_r);

	double y = w_r * r_diff + w_g * g_diff + w_b * b_diff;//value range: 255 * [-1, 1]
	double u = scale_u * (b_diff - y);					  //value range: 255 * 2 * u_max * [-1, 1]
	double v = scale_v * (r_diff - y);					  //value range: 255 * 2 * v_max * [-1, 1]

#ifndef NDEBUG
	const double eps = 0.5;
#endif
	assert(std::abs(y) <= 255 + eps);
	assert(std::abs(u) <= 255 * 2 * u_max + eps);
	assert(std::abs(v) <= 255 * 2 * v_max + eps);

	return std::sqrt(square(luminanceWeight * y) + square(u) +  square(v));
}


inline
double colorDist(uint32_t pix1, uint32_t pix2, double luminanceWeight)
{
	if (pix1 == pix2) //about 8% perf boost
		return 0;

	//return distHSL(pix1, pix2, luminanceWeight);
	//return distRGB(pix1, pix2);
	//return distLAB(pix1, pix2);
	//return distNonLinearRGB(pix1, pix2);
	//return distYUV(pix1, pix2, luminanceWeight);
	//return distYCbCr(pix1, pix2, luminanceWeight);
	return distYCbCrA(pix1, pix2, luminanceWeight);
}


enum BlendType
{
	BLEND_NONE = 0,
	BLEND_NORMAL,   //a normal indication to blend
	BLEND_DOMINANT, //a strong indication to blend
	//attention: BlendType must fit into the value range of 2 bit!!!
};

struct BlendResult
{
	BlendType
	/**/blend_f, blend_g,
	/**/blend_j, blend_k;
};


struct Kernel_4x4 //kernel for preprocessing step
{
	uint32_t
	/**/a, b, c, d,
	/**/e, f, g, h,
	/**/i, j, k, l,
	/**/m, n, o, p;
};

/*
input kernel area naming convention:
-----------------
| A | B | C | D |
----|---|---|---|
| E | F | G | H |   //evalute the four corners between F, G, J, K
----|---|---|---|   //input pixel is at position F
| I | J | K | L |
----|---|---|---|
| M | N | O | P |
-----------------
*/
FORCE_INLINE //detect blend direction
BlendResult preProcessCorners(const Kernel_4x4& ker, const xbrz::ScalerCfg& cfg) //result: F, G, J, K corners of "GradientType"
{
	BlendResult result = {};

	if ((ker.f == ker.g &&
		 ker.j == ker.k) ||
		(ker.f == ker.j &&
		 ker.g == ker.k))
		return result;

#ifdef MEEGO_EDITION_HARMATTAN
#define dist(col1, col2) colorDist(col1, col2, cfg.luminanceWeight_)
#else
	auto dist = [&](uint32_t col1, uint32_t col2) { return colorDist(col1, col2, cfg.luminanceWeight_); };
#endif

	const int weight = 4;
	double jg = dist(ker.i, ker.f) + dist(ker.f, ker.c) + dist(ker.n, ker.k) + dist(ker.k, ker.h) + weight * dist(ker.j, ker.g);
	double fk = dist(ker.e, ker.j) + dist(ker.j, ker.o) + dist(ker.b, ker.g) + dist(ker.g, ker.l) + weight * dist(ker.f, ker.k);

	if (jg < fk) //test sample: 70% of values max(jg, fk) / min(jg, fk) are between 1.1 and 3.7 with median being 1.8
	{
		const bool dominantGradient = cfg.dominantDirectionThreshold * jg < fk;
		if (ker.f != ker.g && ker.f != ker.j)
			result.blend_f = dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL;

		if (ker.k != ker.j && ker.k != ker.g)
			result.blend_k = dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL;
	}
	else if (fk < jg)
	{
		const bool dominantGradient = cfg.dominantDirectionThreshold * fk < jg;
		if (ker.j != ker.f && ker.j != ker.k)
			result.blend_j = dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL;

		if (ker.g != ker.f && ker.g != ker.k)
			result.blend_g = dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL;
	}
	return result;
}

struct Kernel_3x3
{
	uint32_t
	/**/a,  b,  c,
	/**/d,  e,  f,
	/**/g,  h,  i;
};

#define DEF_GETTER(x) template <RotationDegree rotDeg> uint32_t inline get_##x(const Kernel_3x3& ker) { return ker.x; }
//we cannot and NEED NOT write "ker.##x" since ## concatenates preprocessor tokens but "." is not a token
DEF_GETTER(a) DEF_GETTER(b) DEF_GETTER(c)
DEF_GETTER(d) DEF_GETTER(e) DEF_GETTER(f)
DEF_GETTER(g) DEF_GETTER(h) DEF_GETTER(i)
#undef DEF_GETTER

#define DEF_GETTER(x, y) template <> inline uint32_t get_##x<ROT_90>(const Kernel_3x3& ker) { return ker.y; }
DEF_GETTER(a, g) DEF_GETTER(b, d) DEF_GETTER(c, a)
DEF_GETTER(d, h) DEF_GETTER(e, e) DEF_GETTER(f, b)
DEF_GETTER(g, i) DEF_GETTER(h, f) DEF_GETTER(i, c)
#undef DEF_GETTER

#define DEF_GETTER(x, y) template <> inline uint32_t get_##x<ROT_180>(const Kernel_3x3& ker) { return ker.y; }
DEF_GETTER(a, i) DEF_GETTER(b, h) DEF_GETTER(c, g)
DEF_GETTER(d, f) DEF_GETTER(e, e) DEF_GETTER(f, d)
DEF_GETTER(g, c) DEF_GETTER(h, b) DEF_GETTER(i, a)
#undef DEF_GETTER

#define DEF_GETTER(x, y) template <> inline uint32_t get_##x<ROT_270>(const Kernel_3x3& ker) { return ker.y; }
DEF_GETTER(a, c) DEF_GETTER(b, f) DEF_GETTER(c, i)
DEF_GETTER(d, b) DEF_GETTER(e, e) DEF_GETTER(f, h)
DEF_GETTER(g, a) DEF_GETTER(h, d) DEF_GETTER(i,	g)
#undef DEF_GETTER


//compress four blend types into a single byte
inline BlendType getTopL   (unsigned char b) { return static_cast<BlendType>(0x3 & b); }
inline BlendType getTopR   (unsigned char b) { return static_cast<BlendType>(0x3 & (b >> 2)); }
inline BlendType getBottomR(unsigned char b) { return static_cast<BlendType>(0x3 & (b >> 4)); }
inline BlendType getBottomL(unsigned char b) { return static_cast<BlendType>(0x3 & (b >> 6)); }

inline void setTopL   (unsigned char& b, BlendType bt) { b |= bt; } //buffer is assumed to be initialized before preprocessing!
inline void setTopR   (unsigned char& b, BlendType bt) { b |= (bt << 2); }
inline void setBottomR(unsigned char& b, BlendType bt) { b |= (bt << 4); }
inline void setBottomL(unsigned char& b, BlendType bt) { b |= (bt << 6); }

inline bool blendingNeeded(unsigned char b) { return b != 0; }

template <RotationDegree rotDeg> inline
unsigned char rotateBlendInfo(unsigned char b) { return b; }
template <> inline unsigned char rotateBlendInfo<ROT_90 >(unsigned char b) { return ((b << 2) | (b >> 6)) & 0xff; }
template <> inline unsigned char rotateBlendInfo<ROT_180>(unsigned char b) { return ((b << 4) | (b >> 4)) & 0xff; }
template <> inline unsigned char rotateBlendInfo<ROT_270>(unsigned char b) { return ((b << 6) | (b >> 2)) & 0xff; }


#ifndef NDEBUG
int debugPixelX = -1;
int debugPixelY = 84;
bool breakIntoDebugger = false;
#endif


/*
input kernel area naming convention:
-------------
| A | B | C |
----|---|---|
| D | E | F | //input pixel is at position E
----|---|---|
| G | H | I |
-------------
*/
template <class Scaler, RotationDegree rotDeg>
FORCE_INLINE //perf: quite worth it!
void scalePixel(const Kernel_3x3& ker,
				uint32_t* target, int trgWidth,
				unsigned char blendInfo, //result of preprocessing all four corners of pixel "e"
				const xbrz::ScalerCfg& cfg)
{
#define a get_a<rotDeg>(ker)
#define b get_b<rotDeg>(ker)
#define c get_c<rotDeg>(ker)
#define d get_d<rotDeg>(ker)
#define e get_e<rotDeg>(ker)
#define f get_f<rotDeg>(ker)
#define g get_g<rotDeg>(ker)
#define h get_h<rotDeg>(ker)
#define i get_i<rotDeg>(ker)


	const unsigned char blend = rotateBlendInfo<rotDeg>(blendInfo);

	if (getBottomR(blend) >= BLEND_NORMAL)
	{
#ifdef MEEGO_EDITION_HARMATTAN
#define eq(col1, col2) (colorDist(col1, col2, cfg.luminanceWeight_) < cfg.equalColorTolerance_)
#else
		auto eq   = [&](uint32_t col1, uint32_t col2) { return colorDist(col1, col2, cfg.luminanceWeight_) < cfg.equalColorTolerance_; };
		auto dist = [&](uint32_t col1, uint32_t col2) { return colorDist(col1, col2, cfg.luminanceWeight_); };
#endif

		bool doLineBlend = true;
		if (getBottomR(blend) < BLEND_DOMINANT)
		{
			//make sure there is no second blending in an adjacent rotation for this pixel: handles insular pixels, mario eyes
			if ((getTopR(blend) != BLEND_NONE && !eq(e, g)) || //but support double-blending for 90° corners
				(getBottomL(blend) != BLEND_NONE && !eq(e, c)) ||
				(eq(g, h) &&  eq(h , i) && eq(i, f) && eq(f, c) && !eq(e, i))) //no full blending for L-shapes; blend corner only
				doLineBlend = false;
		}

		const uint32_t px = dist(e, f) <= dist(e, h) ? f : h; //choose most similar color

		OutputMatrix<Scaler::scale, rotDeg> out(target, trgWidth);

		if (doLineBlend)
		{
			const double fg = dist(f, g); //test sample: 70% of values max(fg, hc) / min(fg, hc) are between 1.1 and 3.7 with median being 1.9
			const double hc = dist(h, c); //

			const bool haveShallowLine = cfg.steepDirectionThreshold * fg <= hc && e != g && d != g;
			const bool haveSteepLine   = cfg.steepDirectionThreshold * hc <= fg && e != c && b != c;

			if (haveShallowLine)
			{
				if (haveSteepLine)
					Scaler::blendLineSteepAndShallow(px, out);
				else
					Scaler::blendLineShallow(px, out);
			}
			else
			{
				if (haveSteepLine)
					Scaler::blendLineSteep(px, out);
				else
					Scaler::blendLineDiagonal(px,out);
			}
		}
		else
			Scaler::blendCorner(px, out);
	}

#undef a
#undef b
#undef c
#undef d
#undef e
#undef f
#undef g
#undef h
#undef i
}


template <class Scaler> //scaler policy: see "Scaler2x" reference implementation
void scaleImage(const uint32_t* src, uint32_t* trg, int srcWidth, int srcHeight, const xbrz::ScalerCfg& cfg, int yFirst, int yLast)
{
	yFirst = std::max(yFirst, 0);
	yLast  = std::min(yLast, srcHeight);
	if (yFirst >= yLast || srcWidth <= 0)
		return;

	const int trgWidth = srcWidth * Scaler::scale;

	//"use" space at the end of the image as temporary buffer for "on the fly preprocessing": we even could use larger area of
	//"sizeof(uint32_t) * srcWidth * (yLast - yFirst)" bytes without risk of accidental overwriting before accessing
	const int bufferSize = srcWidth;
	unsigned char* preProcBuffer = reinterpret_cast<unsigned char*>(trg + yLast * Scaler::scale * trgWidth) - bufferSize;
	std::fill(preProcBuffer, preProcBuffer + bufferSize, 0);
	static_assert(BLEND_NONE == 0, "");

	//initialize preprocessing buffer for first row: detect upper left and right corner blending
	//this cannot be optimized for adjacent processing stripes; we must not allow for a memory race condition!
	if (yFirst > 0)
	{
		const int y = yFirst - 1;

		const uint32_t* s_m1 = src + srcWidth * std::max(y - 1, 0);
		const uint32_t* s_0  = src + srcWidth * y; //center line
		const uint32_t* s_p1 = src + srcWidth * std::min(y + 1, srcHeight - 1);
		const uint32_t* s_p2 = src + srcWidth * std::min(y + 2, srcHeight - 1);

		for (int x = 0; x < srcWidth; ++x)
		{
			const int x_m1 = std::max(x - 1, 0);
			const int x_p1 = std::min(x + 1, srcWidth - 1);
			const int x_p2 = std::min(x + 2, srcWidth - 1);

			Kernel_4x4 ker = {}; //perf: initialization is negligable
			ker.a = s_m1[x_m1]; //read sequentially from memory as far as possible
			ker.b = s_m1[x];
			ker.c = s_m1[x_p1];
			ker.d = s_m1[x_p2];

			ker.e = s_0[x_m1];
			ker.f = s_0[x];
			ker.g = s_0[x_p1];
			ker.h = s_0[x_p2];

			ker.i = s_p1[x_m1];
			ker.j = s_p1[x];
			ker.k = s_p1[x_p1];
			ker.l = s_p1[x_p2];

			ker.m = s_p2[x_m1];
			ker.n = s_p2[x];
			ker.o = s_p2[x_p1];
			ker.p = s_p2[x_p2];

			const BlendResult res = preProcessCorners(ker, cfg);
			/*
			preprocessing blend result:
			---------
			| F | G |   //evalute corner between F, G, J, K
			----|---|   //input pixel is at position F
			| J | K |
			---------
			*/
			setTopR(preProcBuffer[x], res.blend_j);

			if (x + 1 < srcWidth)
				setTopL(preProcBuffer[x + 1], res.blend_k);
		}
	}
	//------------------------------------------------------------------------------------

	for (int y = yFirst; y < yLast; ++y)
	{
		uint32_t* out = trg + Scaler::scale * y * trgWidth; //consider MT "striped" access

		const uint32_t* s_m1 = src + srcWidth * std::max(y - 1, 0);
		const uint32_t* s_0  = src + srcWidth * y; //center line
		const uint32_t* s_p1 = src + srcWidth * std::min(y + 1, srcHeight - 1);
		const uint32_t* s_p2 = src + srcWidth * std::min(y + 2, srcHeight - 1);

		unsigned char blend_xy1 = 0; //corner blending for current (x, y + 1) position

		for (int x = 0; x < srcWidth; ++x, out += Scaler::scale)
		{
#ifndef NDEBUG
			breakIntoDebugger = debugPixelX == x && debugPixelY == y;
#endif
			//all those bounds checks have only insignificant impact on performance!
			const int x_m1 = std::max(x - 1, 0); //perf: prefer array indexing to additional pointers!
			const int x_p1 = std::min(x + 1, srcWidth - 1);
			const int x_p2 = std::min(x + 2, srcWidth - 1);

			//evaluate the four corners on bottom-right of current pixel
			unsigned char blend_xy = 0; //for current (x, y) position
			{
				Kernel_4x4 ker = {}; //perf: initialization is negligable
				ker.a = s_m1[x_m1]; //read sequentially from memory as far as possible
				ker.b = s_m1[x];
				ker.c = s_m1[x_p1];
				ker.d = s_m1[x_p2];

				ker.e = s_0[x_m1];
				ker.f = s_0[x];
				ker.g = s_0[x_p1];
				ker.h = s_0[x_p2];

				ker.i = s_p1[x_m1];
				ker.j = s_p1[x];
				ker.k = s_p1[x_p1];
				ker.l = s_p1[x_p2];

				ker.m = s_p2[x_m1];
				ker.n = s_p2[x];
				ker.o = s_p2[x_p1];
				ker.p = s_p2[x_p2];

				const BlendResult res = preProcessCorners(ker, cfg);
				/*
				preprocessing blend result:
				---------
				| F | G |   //evalute corner between F, G, J, K
				----|---|   //current input pixel is at position F
				| J | K |
				---------
				*/
				blend_xy = preProcBuffer[x];
				setBottomR(blend_xy, res.blend_f); //all four corners of (x, y) have been determined at this point due to processing sequence!

				setTopR(blend_xy1, res.blend_j); //set 2nd known corner for (x, y + 1)
				preProcBuffer[x] = blend_xy1; //store on current buffer position for use on next row

				blend_xy1 = 0;
				setTopL(blend_xy1, res.blend_k); //set 1st known corner for (x + 1, y + 1) and buffer for use on next column

				if (x + 1 < srcWidth) //set 3rd known corner for (x + 1, y)
					setBottomL(preProcBuffer[x + 1], res.blend_g);
			}

			//fill block of size scale * scale with the given color
			fillBlock(out, trgWidth * sizeof(uint32_t), s_0[x], Scaler::scale); //place *after* preprocessing step, to not overwrite the results while processing the the last pixel!

			//blend four corners of current pixel
			if (blendingNeeded(blend_xy)) //good 20% perf-improvement
			{
				Kernel_3x3 ker = {}; //perf: initialization is negligable

				ker.a = s_m1[x_m1]; //read sequentially from memory as far as possible
				ker.b = s_m1[x];
				ker.c = s_m1[x_p1];

				ker.d = s_0[x_m1];
				ker.e = s_0[x];
				ker.f = s_0[x_p1];

				ker.g = s_p1[x_m1];
				ker.h = s_p1[x];
				ker.i = s_p1[x_p1];

				scalePixel<Scaler, ROT_0  >(ker, out, trgWidth, blend_xy, cfg);
				scalePixel<Scaler, ROT_90 >(ker, out, trgWidth, blend_xy, cfg);
				scalePixel<Scaler, ROT_180>(ker, out, trgWidth, blend_xy, cfg);
				scalePixel<Scaler, ROT_270>(ker, out, trgWidth, blend_xy, cfg);
			}
		}
	}
}


struct Scaler2x
{
	static const int scale = 2;

	template <class OutputMatrix>
	static void blendLineShallow(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 4>(out.template ref<scale - 1, 0>(), col);
		alphaBlend<3, 4>(out.template ref<scale - 1, 1>(), col);
	}

	template <class OutputMatrix>
	static void blendLineSteep(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 4>(out.template ref<0, scale - 1>(), col);
		alphaBlend<3, 4>(out.template ref<1, scale - 1>(), col);
	}

	template <class OutputMatrix>
	static void blendLineSteepAndShallow(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 4>(out.template ref<1, 0>(), col);
		alphaBlend<1, 4>(out.template ref<0, 1>(), col);
		alphaBlend<5, 6>(out.template ref<1, 1>(), col); //[!] fixes 7/8 used in xBR
	}

	template <class OutputMatrix>
	static void blendLineDiagonal(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 2>(out.template ref<1, 1>(), col);
	}

	template <class OutputMatrix>
	static void blendCorner(uint32_t col, OutputMatrix& out)
	{
		//model a round corner
		alphaBlend<21, 100>(out.template ref<1, 1>(), col); //exact: 1 - pi/4 = 0.2146018366
	}
};


struct Scaler3x
{
	static const int scale = 3;

	template <class OutputMatrix>
	static void blendLineShallow(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 4>(out.template ref<scale - 1, 0>(), col);
		alphaBlend<1, 4>(out.template ref<scale - 2, 2>(), col);

		alphaBlend<3, 4>(out.template ref<scale - 1, 1>(), col);
		out.template ref<scale - 1, 2>() = col;
	}

	template <class OutputMatrix>
	static void blendLineSteep(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 4>(out.template ref<0, scale - 1>(), col);
		alphaBlend<1, 4>(out.template ref<2, scale - 2>(), col);

		alphaBlend<3, 4>(out.template ref<1, scale - 1>(), col);
		out.template ref<2, scale - 1>() = col;
	}

	template <class OutputMatrix>
	static void blendLineSteepAndShallow(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 4>(out.template ref<2, 0>(), col);
		alphaBlend<1, 4>(out.template ref<0, 2>(), col);
		alphaBlend<3, 4>(out.template ref<2, 1>(), col);
		alphaBlend<3, 4>(out.template ref<1, 2>(), col);
		out.template ref<2, 2>() = col;
	}

	template <class OutputMatrix>
	static void blendLineDiagonal(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 8>(out.template ref<1, 2>(), col);
		alphaBlend<1, 8>(out.template ref<2, 1>(), col);
		alphaBlend<7, 8>(out.template ref<2, 2>(), col);
	}

	template <class OutputMatrix>
	static void blendCorner(uint32_t col, OutputMatrix& out)
	{
		//model a round corner
		alphaBlend<45, 100>(out.template ref<2, 2>(), col); //exact: 0.4545939598
		//alphaBlend<14, 1000>(out.template ref<2, 1>(), col); //0.01413008627 -> negligable
		//alphaBlend<14, 1000>(out.template ref<1, 2>(), col); //0.01413008627
	}
};


struct Scaler4x
{
	static const int scale = 4;

	template <class OutputMatrix>
	static void blendLineShallow(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 4>(out.template ref<scale - 1, 0>(), col);
		alphaBlend<1, 4>(out.template ref<scale - 2, 2>(), col);

		alphaBlend<3, 4>(out.template ref<scale - 1, 1>(), col);
		alphaBlend<3, 4>(out.template ref<scale - 2, 3>(), col);

		out.template ref<scale - 1, 2>() = col;
		out.template ref<scale - 1, 3>() = col;
	}

	template <class OutputMatrix>
	static void blendLineSteep(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 4>(out.template ref<0, scale - 1>(), col);
		alphaBlend<1, 4>(out.template ref<2, scale - 2>(), col);

		alphaBlend<3, 4>(out.template ref<1, scale - 1>(), col);
		alphaBlend<3, 4>(out.template ref<3, scale - 2>(), col);

		out.template ref<2, scale - 1>() = col;
		out.template ref<3, scale - 1>() = col;
	}

	template <class OutputMatrix>
	static void blendLineSteepAndShallow(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<3, 4>(out.template ref<3, 1>(), col);
		alphaBlend<3, 4>(out.template ref<1, 3>(), col);
		alphaBlend<1, 4>(out.template ref<3, 0>(), col);
		alphaBlend<1, 4>(out.template ref<0, 3>(), col);
		alphaBlend<1, 3>(out.template ref<2, 2>(), col); //[!] fixes 1/4 used in xBR
		out.template ref<3, 3>() = out.template ref<3, 2>() = out.template ref<2, 3>() = col;
	}

	template <class OutputMatrix>
	static void blendLineDiagonal(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 2>(out.template ref<scale - 1, scale / 2	>(), col);
		alphaBlend<1, 2>(out.template ref<scale - 2, scale / 2 + 1>(), col);
		out.template ref<scale - 1, scale - 1>() = col;
	}

	template <class OutputMatrix>
	static void blendCorner(uint32_t col, OutputMatrix& out)
	{
		//model a round corner
		alphaBlend<68, 100>(out.template ref<3, 3>(), col); //exact: 0.6848532563
		alphaBlend< 9, 100>(out.template ref<3, 2>(), col); //0.08677704501
		alphaBlend< 9, 100>(out.template ref<2, 3>(), col); //0.08677704501
	}
};


struct Scaler5x
{
	static const int scale = 5;

	template <class OutputMatrix>
	static void blendLineShallow(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 4>(out.template ref<scale - 1, 0>(), col);
		alphaBlend<1, 4>(out.template ref<scale - 2, 2>(), col);
		alphaBlend<1, 4>(out.template ref<scale - 3, 4>(), col);

		alphaBlend<3, 4>(out.template ref<scale - 1, 1>(), col);
		alphaBlend<3, 4>(out.template ref<scale - 2, 3>(), col);

		out.template ref<scale - 1, 2>() = col;
		out.template ref<scale - 1, 3>() = col;
		out.template ref<scale - 1, 4>() = col;
		out.template ref<scale - 2, 4>() = col;
	}

	template <class OutputMatrix>
	static void blendLineSteep(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 4>(out.template ref<0, scale - 1>(), col);
		alphaBlend<1, 4>(out.template ref<2, scale - 2>(), col);
		alphaBlend<1, 4>(out.template ref<4, scale - 3>(), col);

		alphaBlend<3, 4>(out.template ref<1, scale - 1>(), col);
		alphaBlend<3, 4>(out.template ref<3, scale - 2>(), col);

		out.template ref<2, scale - 1>() = col;
		out.template ref<3, scale - 1>() = col;
		out.template ref<4, scale - 1>() = col;
		out.template ref<4, scale - 2>() = col;
	}

	template <class OutputMatrix>
	static void blendLineSteepAndShallow(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 4>(out.template ref<0, scale - 1>(), col);
		alphaBlend<1, 4>(out.template ref<2, scale - 2>(), col);
		alphaBlend<3, 4>(out.template ref<1, scale - 1>(), col);

		alphaBlend<1, 4>(out.template ref<scale - 1, 0>(), col);
		alphaBlend<1, 4>(out.template ref<scale - 2, 2>(), col);
		alphaBlend<3, 4>(out.template ref<scale - 1, 1>(), col);

		out.template ref<2, scale - 1>() = col;
		out.template ref<3, scale - 1>() = col;

		out.template ref<scale - 1, 2>() = col;
		out.template ref<scale - 1, 3>() = col;

		out.template ref<4, scale - 1>() = col;

		alphaBlend<2, 3>(out.template ref<3, 3>(), col);
	}

	template <class OutputMatrix>
	static void blendLineDiagonal(uint32_t col, OutputMatrix& out)
	{
		alphaBlend<1, 8>(out.template ref<scale - 1, scale / 2	>(), col);
		alphaBlend<1, 8>(out.template ref<scale - 2, scale / 2 + 1>(), col);
		alphaBlend<1, 8>(out.template ref<scale - 3, scale / 2 + 2>(), col);

		alphaBlend<7, 8>(out.template ref<4, 3>(), col);
		alphaBlend<7, 8>(out.template ref<3, 4>(), col);

		out.template ref<4, 4>() = col;
	}

	template <class OutputMatrix>
	static void blendCorner(uint32_t col, OutputMatrix& out)
	{
		//model a round corner
		alphaBlend<86, 100>(out.template ref<4, 4>(), col); //exact: 0.8631434088
		alphaBlend<23, 100>(out.template ref<4, 3>(), col); //0.2306749731
		alphaBlend<23, 100>(out.template ref<3, 4>(), col); //0.2306749731
		//alphaBlend<8, 1000>(out.template ref<4, 2>(), col); //0.008384061834 -> negligable
		//alphaBlend<8, 1000>(out.template ref<2, 4>(), col); //0.008384061834
	}
};
}


void xbrz::scale(size_t factor, const uint32_t* src, uint32_t* trg, int srcWidth, int srcHeight, const xbrz::ScalerCfg& cfg, int yFirst, int yLast)
{
	switch (factor)
	{
		case 2:
			return scaleImage<Scaler2x>(src, trg, srcWidth, srcHeight, cfg, yFirst, yLast);
		case 3:
			return scaleImage<Scaler3x>(src, trg, srcWidth, srcHeight, cfg, yFirst, yLast);
		case 4:
			return scaleImage<Scaler4x>(src, trg, srcWidth, srcHeight, cfg, yFirst, yLast);
		case 5:
			return scaleImage<Scaler5x>(src, trg, srcWidth, srcHeight, cfg, yFirst, yLast);
	}
	assert(false);
}


bool xbrz::equalColor(uint32_t col1, uint32_t col2, double luminanceWeight, double equalColorTolerance)
{
	return colorDist(col1, col2, luminanceWeight) < equalColorTolerance;
}


void xbrz::nearestNeighborScale(const uint32_t* src, int srcWidth, int srcHeight, int srcPitch,
								uint32_t* trg, int trgWidth, int trgHeight, int trgPitch,
								SliceType st, int yFirst, int yLast)
{
	if (srcPitch < srcWidth * static_cast<int>(sizeof(uint32_t))  ||
		trgPitch < trgWidth * static_cast<int>(sizeof(uint32_t)))
	{
		assert(false);
		return;
	}

	switch (st)
	{
		case NN_SCALE_SLICE_SOURCE:
			//nearest-neighbor (going over source image - fast for upscaling, since source is read only once
			yFirst = std::max(yFirst, 0);
			yLast  = std::min(yLast, srcHeight);
			if (yFirst >= yLast || trgWidth <= 0 || trgHeight <= 0) return;

			for (int y = yFirst; y < yLast; ++y)
			{
				//mathematically: ySrc = floor(srcHeight * yTrg / trgHeight)
				// => search for integers in: [ySrc, ySrc + 1) * trgHeight / srcHeight

				//keep within for loop to support MT input slices!
				const int yTrg_first = ( y	  * trgHeight + srcHeight - 1) / srcHeight; //=ceil(y * trgHeight / srcHeight)
				const int yTrg_last  = ((y + 1) * trgHeight + srcHeight - 1) / srcHeight; //=ceil(((y + 1) * trgHeight) / srcHeight)
				const int blockHeight = yTrg_last - yTrg_first;

				if (blockHeight > 0)
				{
					const uint32_t* srcLine = byteAdvance(src, y * srcPitch);
					uint32_t* trgLine  = byteAdvance(trg, yTrg_first * trgPitch);
					int xTrg_first = 0;

					for (int x = 0; x < srcWidth; ++x)
					{
						int xTrg_last = ((x + 1) * trgWidth + srcWidth - 1) / srcWidth;
						const int blockWidth = xTrg_last - xTrg_first;
						if (blockWidth > 0)
						{
							xTrg_first = xTrg_last;
							fillBlock(trgLine, trgPitch, srcLine[x], blockWidth, blockHeight);
							trgLine += blockWidth;
						}
					}
				}
			}
			break;

		case NN_SCALE_SLICE_TARGET:
			//nearest-neighbor (going over target image - slow for upscaling, since source is read multiple times missing out on cache! Fast for similar image sizes!)
			yFirst = std::max(yFirst, 0);
			yLast  = std::min(yLast, trgHeight);
			if (yFirst >= yLast || srcHeight <= 0 || srcWidth <= 0) return;

			for (int y = yFirst; y < yLast; ++y)
			{
				uint32_t* trgLine = byteAdvance(trg, y * trgPitch);
				const int ySrc = srcHeight * y / trgHeight;
				const uint32_t* srcLine = byteAdvance(src, ySrc * srcPitch);
				for (int x = 0; x < trgWidth; ++x)
				{
					const int xSrc = srcWidth * x / trgWidth;
					trgLine[x] = srcLine[xSrc];
				}
			}
			break;
	}
}
