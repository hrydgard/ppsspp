// Modified by Henrik Rydgård:
//
// * Moved many I/O and similar functions to etctool.cpp, this file
// should only concern itself with compression.
// * got rid of readCompress..



// etcpack.cxx v1.06
//
// NO WARRANTY 
// 
// BECAUSE THE PROGRAM IS LICENSED FREE OF CHARGE, ERICSSON MAKES NO
// REPRESENTATIONS OF ANY KIND, EXTENDS NO WARRANTIES OF ANY KIND; EITHER
// EXPRESS OR IMPLIED; INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE
// PROGRAM IS WITH YOU. SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME
// THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION. ERICSSON
// MAKES NO WARRANTY THAT THE MANUFACTURE, SALE, LEASE, USE OR
// IMPORTATION WILL BE FREE FROM INFRINGEMENT OF PATENTS, COPYRIGHTS OR
// OTHER INTELLECTUAL PROPERTY RIGHTS OF OTHERS, AND IT SHALL BE THE SOLE
// RESPONSIBILITY OF THE LICENSEE TO MAKE SUCH DETERMINATION AS IS
// NECESSARY WITH RESPECT TO THE ACQUISITION OF LICENSES UNDER PATENTS
// AND OTHER INTELLECTUAL PROPERTY OF THIRD PARTIES;
// 
// IN NO EVENT WILL ERICSSON, BE LIABLE TO YOU FOR DAMAGES, INCLUDING ANY
// GENERAL, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF
// THE USE OR INABILITY TO USE THE PROGRAM (INCLUDING BUT NOT LIMITED TO
// LOSS OF DATA OR DATA BEING RENDERED INACCURATE OR LOSSES SUSTAINED BY
// YOU OR THIRD PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE WITH ANY
// OTHER PROGRAMS), EVEN IF SUCH HOLDER OR OTHER PARTY HAS BEEN ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGES.
// 
// (C) Ericsson AB 2005. All Rights Reserved.
// 

#include <stdio.h>
#include <string.h>

#include "etcpack.h"
#include "etcdec.h"



#define CLAMP(ll,x,ul) (((x)<(ll)) ? (ll) : (((x)>(ul)) ? (ul) : (x)))
#define GETBITS(source, size, startpos)  (( (source) >> ((startpos)-(size)+1) ) & ((1<<(size)) -1))
#define GETBITSHIGH(source, size, startpos)  (( (source) >> (((startpos)-32)-(size)+1) ) & ((1<<(size)) -1))

#define SQUARE(x) ((x)*(x))
#define JAS_ROUND(x) (((x) < 0.0 ) ? ((int)((x)-0.5)) : ((int)((x)+0.5)))

#define RED(img,width,x,y)   img[3*(y*width+x)+0]
#define GREEN(img,width,x,y) img[3*(y*width+x)+1]
#define BLUE(img,width,x,y)  img[3*(y*width+x)+2]


// SLOW SCAN RANGE IS -5 to 5 in all three colors
#define SLOW_SCAN_MIN (-5)
#define SLOW_SCAN_MAX (5)
#define SLOW_SCAN_RANGE ((SLOW_SCAN_MAX-(SLOW_SCAN_MIN)+1))
#define SLOW_SCAN_OFFSET (-(SLOW_SCAN_MIN))
// We need this to guarrantee that at least one try is valid
#define SLOW_TRY_MIN (-4 - SLOW_SCAN_MAX)
#define SLOW_TRY_MAX (3 - (SLOW_SCAN_MIN))


// MEDIUM SCAN RANGE IS -3 to 3in all three colors
#define MEDIUM_SCAN_MIN (-3)
#define MEDIUM_SCAN_MAX (3)
#define MEDIUM_SCAN_RANGE ((MEDIUM_SCAN_MAX-(MEDIUM_SCAN_MIN)+1))
#define MEDIUM_SCAN_OFFSET (-(MEDIUM_SCAN_MIN))

// We need this to guarrantee that at least one try is valid
#define MEDIUM_TRY_MIN (-4 - MEDIUM_SCAN_MAX)
#define MEDIUM_TRY_MAX (3 - (MEDIUM_SCAN_MIN))

 
#define PUTBITS( dest, data, size, startpos) dest |= ( (data) & ((1<<(size))-1) ) << ((startpos)-(size)+1)
#define PUTBITSHIGH( dest, data, size, startpos) dest |= ( (data) & ((1<<(size))-1) ) << (((startpos)-32)-(size)+1)

int scramble[4] = {3, 2, 0, 1};

static const int compressParamsEnc[16][4] = {
  { -8, -2, 2, 8 },
  { -8, -2, 2, 8 },
  { -17, -5, 5, 17 },
  { -17, -5, 5, 17 },
  { -29, -9, 9, 29 },
  { -29, -9, 9, 29 },
  { -42, -13, 13, 42 },
  { -42, -13, 13, 42 },
  { -60, -18, 18, 60 },
  { -60, -18, 18, 60 },
  { -80, -24, 24, 80 },
  { -80, -24, 24, 80 },
  {-106, -33, 33,106 },
  {-106, -33, 33,106 },
  {-183, -47, 47,183 },
  {-183, -47, 47,183 },
};

void computeAverageColor2x4noQuantFloat(uint8 *img,int width,int height,int startx,int starty,float *avg_color) {
	int r=0,g=0,b=0;
	for(int y=starty; y<starty+4; y++) {
		for(int x=startx; x<startx+2; x++) {
			r+=RED(img,width,x,y);
			g+=GREEN(img,width,x,y);
			b+=BLUE(img,width,x,y);
		}
	}
  avg_color[0]=(float)(r/8.0);
	avg_color[1]=(float)(g/8.0);
	avg_color[2]=(float)(b/8.0);
}

void computeAverageColor4x2noQuantFloat(uint8 *img,int width,int height,int startx,int starty,float *avg_color) { 
	int r=0,g=0,b=0;
	for(int y=starty; y<starty+2; y++) {
		for(int x=startx; x<startx+4; x++) {
			r+=RED(img,width,x,y);
			g+=GREEN(img,width,x,y);
			b+=BLUE(img,width,x,y);
		}
	}
  avg_color[0]=(float)(r/8.0);
	avg_color[1]=(float)(g/8.0);
	avg_color[2]=(float)(b/8.0);
}

int compressBlockWithTable2x4(uint8 *img,int width,int height,int startx,int starty,uint8 *avg_color,int table,unsigned int *pixel_indices_MSBp, unsigned int *pixel_indices_LSBp)
{
	uint8 orig[3],approx[3];
	unsigned int pixel_indices_MSB=0, pixel_indices_LSB=0, pixel_indices = 0;
	int sum_error=0;
	int q, i;


	i = 0;
	for(int x=startx; x<startx+2; x++)
	{
		for(int y=starty; y<starty+4; y++)
		{
			int err;
			int best=0;
			int min_error=255*255*3*16;
			orig[0]=RED(img,width,x,y);
			orig[1]=GREEN(img,width,x,y);
			orig[2]=BLUE(img,width,x,y);

			for(q=0;q<4;q++)
			{
				approx[0]=CLAMP(0, avg_color[0]+compressParamsEnc[table][q],255);
				approx[1]=CLAMP(0, avg_color[1]+compressParamsEnc[table][q],255);
				approx[2]=CLAMP(0, avg_color[2]+compressParamsEnc[table][q],255);

				// Here we just use equal weights to R, G and B. Although this will
				// give visually worse results, it will give a better PSNR score. 
				err=SQUARE(approx[0]-orig[0]) + SQUARE(approx[1]-orig[1]) + SQUARE(approx[2]-orig[2]);
				if(err<min_error)
				{
					min_error=err;
					best=q;
				}

			}

			pixel_indices = scramble[best];

			PUTBITS( pixel_indices_MSB, (pixel_indices >> 1), 1, i);
			PUTBITS( pixel_indices_LSB, (pixel_indices & 1) , 1, i);

			i++;

			// In order to simplify hardware, the table {-12, -4, 4, 12} is indexed {11, 10, 00, 01}
			// so that first bit is sign bit and the other bit is size bit (4 or 12). 
			// This means that we have to scramble the bits before storing them. 

			
			sum_error+=min_error;
		}

	}

	*pixel_indices_MSBp = pixel_indices_MSB;
	*pixel_indices_LSBp = pixel_indices_LSB;

	return sum_error;
}

float compressBlockWithTable2x4percep(uint8 *img,int width,int height,int startx,int starty,uint8 *avg_color,int table,unsigned int *pixel_indices_MSBp, unsigned int *pixel_indices_LSBp)
{
	uint8 orig[3],approx[3];
	unsigned int pixel_indices_MSB=0, pixel_indices_LSB=0, pixel_indices = 0;
	float sum_error=0;
	int q, i;

	double wR2 = PERCEPTUAL_WEIGHT_R_SQUARED;
	double wG2 = PERCEPTUAL_WEIGHT_G_SQUARED;
	double wB2 = PERCEPTUAL_WEIGHT_B_SQUARED;

	i = 0;
	for(int x=startx; x<startx+2; x++)
	{
		for(int y=starty; y<starty+4; y++)
		{
			float err;
			int best=0;
			float min_error=255*255*3*16;
			orig[0]=RED(img,width,x,y);
			orig[1]=GREEN(img,width,x,y);
			orig[2]=BLUE(img,width,x,y);

			for(q=0;q<4;q++)
			{
				approx[0]=CLAMP(0, avg_color[0]+compressParamsEnc[table][q],255);
				approx[1]=CLAMP(0, avg_color[1]+compressParamsEnc[table][q],255);
				approx[2]=CLAMP(0, avg_color[2]+compressParamsEnc[table][q],255);

				// Here we just use equal weights to R, G and B. Although this will
				// give visually worse results, it will give a better PSNR score. 
  				err=(float)(wR2*SQUARE((approx[0]-orig[0])) + (float)wG2*SQUARE((approx[1]-orig[1])) + (float)wB2*SQUARE((approx[2]-orig[2])));
				if(err<min_error)
				{
					min_error=err;
					best=q;
				}

			}

			pixel_indices = scramble[best];

			PUTBITS( pixel_indices_MSB, (pixel_indices >> 1), 1, i);
			PUTBITS( pixel_indices_LSB, (pixel_indices & 1) , 1, i);

			i++;

			// In order to simplify hardware, the table {-12, -4, 4, 12} is indexed {11, 10, 00, 01}
			// so that first bit is sign bit and the other bit is size bit (4 or 12). 
			// This means that we have to scramble the bits before storing them. 

			
			sum_error+=min_error;
		}

	}

	*pixel_indices_MSBp = pixel_indices_MSB;
	*pixel_indices_LSBp = pixel_indices_LSB;

	return sum_error;
}
int compressBlockWithTable4x2(uint8 *img,int width,int height,int startx,int starty,uint8 *avg_color,int table,unsigned int *pixel_indices_MSBp, unsigned int *pixel_indices_LSBp)
{
	uint8 orig[3],approx[3];
	unsigned int pixel_indices_MSB=0, pixel_indices_LSB=0, pixel_indices = 0;
	int sum_error=0;
	int q;
	int i;


	i = 0;
	for(int x=startx; x<startx+4; x++)
	{
		for(int y=starty; y<starty+2; y++)
		{
			int err;
			int best=0;
			int min_error=255*255*3*16;
			orig[0]=RED(img,width,x,y);
			orig[1]=GREEN(img,width,x,y);
			orig[2]=BLUE(img,width,x,y);

			for(q=0;q<4;q++)
			{
				approx[0]=CLAMP(0, avg_color[0]+compressParamsEnc[table][q],255);
				approx[1]=CLAMP(0, avg_color[1]+compressParamsEnc[table][q],255);
				approx[2]=CLAMP(0, avg_color[2]+compressParamsEnc[table][q],255);

				// Here we just use equal weights to R, G and B. Although this will
				// give visually worse results, it will give a better PSNR score. 
				err=SQUARE(approx[0]-orig[0]) + SQUARE(approx[1]-orig[1]) + SQUARE(approx[2]-orig[2]);
				if(err<min_error)
				{
					min_error=err;
					best=q;
				}

			}
			pixel_indices = scramble[best];

			PUTBITS( pixel_indices_MSB, (pixel_indices >> 1), 1, i);
			PUTBITS( pixel_indices_LSB, (pixel_indices & 1) , 1, i);
			i++;

			// In order to simplify hardware, the table {-12, -4, 4, 12} is indexed {11, 10, 00, 01}
			// so that first bit is sign bit and the other bit is size bit (4 or 12). 
			// This means that we have to scramble the bits before storing them. 

			sum_error+=min_error;
		}
		i+=2;

	}

	*pixel_indices_MSBp = pixel_indices_MSB;
	*pixel_indices_LSBp = pixel_indices_LSB;


	return sum_error;
}

float compressBlockWithTable4x2percep(uint8 *img,int width,int height,int startx,int starty,uint8 *avg_color,int table,unsigned int *pixel_indices_MSBp, unsigned int *pixel_indices_LSBp)
{
	uint8 orig[3],approx[3];
	unsigned int pixel_indices_MSB=0, pixel_indices_LSB=0, pixel_indices = 0;
	float sum_error=0;
	int q;
	int i;
	float wR2 = (float) PERCEPTUAL_WEIGHT_R_SQUARED;
	float wG2 = (float) PERCEPTUAL_WEIGHT_G_SQUARED;
	float wB2 = (float) PERCEPTUAL_WEIGHT_B_SQUARED;


	i = 0;
	for(int x=startx; x<startx+4; x++)
	{
		for(int y=starty; y<starty+2; y++)
		{
			float err;
			int best=0;
			float min_error=255*255*3*16;
			orig[0]=RED(img,width,x,y);
			orig[1]=GREEN(img,width,x,y);
			orig[2]=BLUE(img,width,x,y);

			for(q=0;q<4;q++)
			{
				approx[0]=CLAMP(0, avg_color[0]+compressParamsEnc[table][q],255);
				approx[1]=CLAMP(0, avg_color[1]+compressParamsEnc[table][q],255);
				approx[2]=CLAMP(0, avg_color[2]+compressParamsEnc[table][q],255);

				// Here we just use equal weights to R, G and B. Although this will
				// give visually worse results, it will give a better PSNR score. 
				err=(float) wR2*SQUARE(approx[0]-orig[0]) + (float)wG2*SQUARE(approx[1]-orig[1]) + (float)wB2*SQUARE(approx[2]-orig[2]);
				if(err<min_error)
				{
					min_error=err;
					best=q;
				}

			}
			pixel_indices = scramble[best];

			PUTBITS( pixel_indices_MSB, (pixel_indices >> 1), 1, i);
			PUTBITS( pixel_indices_LSB, (pixel_indices & 1) , 1, i);
			i++;

			// In order to simplify hardware, the table {-12, -4, 4, 12} is indexed {11, 10, 00, 01}
			// so that first bit is sign bit and the other bit is size bit (4 or 12). 
			// This means that we have to scramble the bits before storing them. 

			sum_error+=min_error;
		}
		i+=2;

	}

	*pixel_indices_MSBp = pixel_indices_MSB;
	*pixel_indices_LSBp = pixel_indices_LSB;


	return sum_error;
}

int tryalltables_3bittable2x4(uint8 *img,int width,int height,int startx,int starty,uint8 *avg_color, unsigned int &best_table,unsigned int &best_pixel_indices_MSB, unsigned int &best_pixel_indices_LSB)
{
	int min_error = 3*255*255*16;
	int q;
	int err;
	unsigned int pixel_indices_MSB, pixel_indices_LSB;

	for(q=0;q<16;q+=2)		// try all the 8 tables. 
	{

		err=compressBlockWithTable2x4(img,width,height,startx,starty,avg_color,q,&pixel_indices_MSB, &pixel_indices_LSB);

		if(err<min_error)
		{

			min_error=err;
			best_pixel_indices_MSB = pixel_indices_MSB;
			best_pixel_indices_LSB = pixel_indices_LSB;
			best_table=q >> 1;

		}
	}
	return min_error;
}

int tryalltables_3bittable2x4percep(uint8 *img,int width,int height,int startx,int starty,uint8 *avg_color, unsigned int &best_table,unsigned int &best_pixel_indices_MSB, unsigned int &best_pixel_indices_LSB)
{
	float min_error = 3*255*255*16;
	int q;
	float err;
	unsigned int pixel_indices_MSB, pixel_indices_LSB;

	for(q=0;q<16;q+=2)		// try all the 8 tables. 
	{

		err=compressBlockWithTable2x4percep(img,width,height,startx,starty,avg_color,q,&pixel_indices_MSB, &pixel_indices_LSB);

		if(err<min_error)
		{

			min_error=err;
			best_pixel_indices_MSB = pixel_indices_MSB;
			best_pixel_indices_LSB = pixel_indices_LSB;
			best_table=q >> 1;

		}
	}
	return (int) min_error;
}

int tryalltables_3bittable4x2(uint8 *img,int width,int height,int startx,int starty,uint8 *avg_color, unsigned int &best_table,unsigned int &best_pixel_indices_MSB, unsigned int &best_pixel_indices_LSB)
{
	int min_error = 3*255*255*16;
	int q;
	int err;
	unsigned int pixel_indices_MSB, pixel_indices_LSB;

	for(q=0;q<16;q+=2)		// try all the 8 tables. 
	{

		err=compressBlockWithTable4x2(img,width,height,startx,starty,avg_color,q,&pixel_indices_MSB, &pixel_indices_LSB);

		if(err<min_error)
		{

			min_error=err;
			best_pixel_indices_MSB = pixel_indices_MSB;
			best_pixel_indices_LSB = pixel_indices_LSB;
			best_table=q >> 1;
		}
	}

	return min_error;
}
int tryalltables_3bittable4x2percep(uint8 *img,int width,int height,int startx,int starty,uint8 *avg_color, unsigned int &best_table,unsigned int &best_pixel_indices_MSB, unsigned int &best_pixel_indices_LSB)
{
	float min_error = 3*255*255*16;
	int q;
	float err;
	unsigned int pixel_indices_MSB, pixel_indices_LSB;

	for(q=0;q<16;q+=2)		// try all the 8 tables. 
	{

		err=compressBlockWithTable4x2percep(img,width,height,startx,starty,avg_color,q,&pixel_indices_MSB, &pixel_indices_LSB);

		if(err<min_error)
		{

			min_error=err;
			best_pixel_indices_MSB = pixel_indices_MSB;
			best_pixel_indices_LSB = pixel_indices_LSB;
			best_table=q >> 1;
		}
	}

	return (int) min_error;
}

// The below code quantizes a float RGB value to RGB555. 
//

void quantize444ColorCombined(float *avg_col_in, int *enc_color, uint8 *avg_color)
{
	// Detta ar nummer tva
	float dr, dg, db;
	float kr, kg, kb;
	float wR2, wG2, wB2;
	uint8 low_color[3];
	uint8 high_color[3];
	float lowhightable[8];
	int q;
	float kval = (float) (255.0/15.0);


	// These are the values that we want to have:
	float red_average, green_average, blue_average;

	int red_4bit_low, green_4bit_low, blue_4bit_low;
	int red_4bit_high, green_4bit_high, blue_4bit_high;
	
	// These are the values that we approximate with:
	int red_low, green_low, blue_low;
	int red_high, green_high, blue_high;

	red_average = avg_col_in[0];
	green_average = avg_col_in[1];
	blue_average = avg_col_in[2];

	// Find the 5-bit reconstruction levels red_low, red_high
	// so that red_average is in interval [red_low, red_high].
	// (The same with green and blue.)

	red_4bit_low = (int) (red_average/kval);
	green_4bit_low = (int) (green_average/kval);
	blue_4bit_low = (int) (blue_average/kval);

	red_4bit_high = CLAMP(0, red_4bit_low + 1, 15);
	green_4bit_high  = CLAMP(0, green_4bit_low + 1, 15);
	blue_4bit_high = CLAMP(0, blue_4bit_low + 1, 15);

	red_low   = (red_4bit_low << 4) | (red_4bit_low >> 0);
	green_low = (green_4bit_low << 4) | (green_4bit_low >> 0);
	blue_low = (blue_4bit_low << 4) | (blue_4bit_low >> 0);

	red_high   = (red_4bit_high << 4) | (red_4bit_high >> 0);
	green_high = (green_4bit_high << 4) | (green_4bit_high >> 0);
	blue_high = (blue_4bit_high << 4) | (blue_4bit_high >> 0);

	kr = (float)red_high - (float)red_low;
	kg = (float)green_high - (float)green_low;
	kb = (float)blue_high - (float)blue_low;

	// Note that dr, dg, and db are all negative.
	dr = red_low - red_average;
	dg = green_low - green_average;
	db = blue_low - blue_average;

	// Use straight (nonperceptive) weights.
	wR2 = (float) 1.0;
	wG2 = (float) 1.0;
	wB2 = (float) 1.0;

	lowhightable[0] = wR2*wG2*SQUARE( (dr+ 0) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+ 0) );
	lowhightable[1] = wR2*wG2*SQUARE( (dr+kr) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+kr) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+ 0) );
	lowhightable[2] = wR2*wG2*SQUARE( (dr+ 0) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+kg) - (db+ 0) );
	lowhightable[3] = wR2*wG2*SQUARE( (dr+ 0) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+kb) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+kb) );
	lowhightable[4] = wR2*wG2*SQUARE( (dr+kr) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+kr) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+kg) - (db+ 0) );
	lowhightable[5] = wR2*wG2*SQUARE( (dr+kr) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+kr) - (db+kb) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+kb) );
	lowhightable[6] = wR2*wG2*SQUARE( (dr+ 0) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+kb) ) + wG2*wB2*SQUARE( (dg+kg) - (db+kb) );
	lowhightable[7] = wR2*wG2*SQUARE( (dr+kr) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+kr) - (db+kb) ) + wG2*wB2*SQUARE( (dg+kg) - (db+kb) );

	float min_value = lowhightable[0];
	int min_index = 0;

	for(q = 1; q<8; q++)
	{
		if(lowhightable[q] < min_value)
		{
			min_value = lowhightable[q];
			min_index = q;
		}
	}

	low_color[0] = red_4bit_low;
	low_color[1] = green_4bit_low;
	low_color[2] = blue_4bit_low;

	high_color[0] = red_4bit_high;
	high_color[1] = green_4bit_high;
	high_color[2] = blue_4bit_high;

	switch(min_index)
	{
	case 0:
		// Since the step size is always 17 in RGB444 format (15*17=255),
		// kr = kg = kb = 17, which means that case 0 and case 7 will
		// always have equal projected error. Choose the one that is
		// closer to the desired color. 
		if(dr*dr + dg*dg + db*db > 3*8*8)
		{
			enc_color[0] = high_color[0];
			enc_color[1] = high_color[1];
			enc_color[2] = high_color[2];
		}
		else
		{
			enc_color[0] = low_color[0];
			enc_color[1] = low_color[1];
			enc_color[2] = low_color[2];
		}
		break;
	case 1:
		enc_color[0] = high_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = low_color[2];
		break;
	case 2:	
		enc_color[0] = low_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = low_color[2];
		break;
	case 3:	
		enc_color[0] = low_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = high_color[2];
		break;
	case 4:	
		enc_color[0] = high_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = low_color[2];
		break;
	case 5:	
		enc_color[0] = high_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = high_color[2];
		break;
	case 6:	
		enc_color[0] = low_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = high_color[2];
		break;
	case 7:	
		if(dr*dr + dg*dg + db*db > 3*8*8)
		{
			enc_color[0] = high_color[0];
			enc_color[1] = high_color[1];
			enc_color[2] = high_color[2];
		}
		else
		{
			enc_color[0] = low_color[0];
			enc_color[1] = low_color[1];
			enc_color[2] = low_color[2];
		}
		break;
	}

	// Expand 5-bit encoded color to 8-bit color
	avg_color[0] = (enc_color[0] << 3) | (enc_color[0] >> 2);
	avg_color[1] = (enc_color[1] << 3) | (enc_color[1] >> 2);
	avg_color[2] = (enc_color[2] << 3) | (enc_color[2] >> 2);	
}


// The below code quantizes a float RGB value to RGB555. 
//
void quantize555ColorCombined(float *avg_col_in, int *enc_color, uint8 *avg_color)
{
	float dr, dg, db;
	float kr, kg, kb;
	float wR2, wG2, wB2;
	uint8 low_color[3];
	uint8 high_color[3];
	float lowhightable[8];
	int q;
	float kval = (float) (255.0/31.0);


	// These are the values that we want to have:
	float red_average, green_average, blue_average;

	int red_5bit_low, green_5bit_low, blue_5bit_low;
	int red_5bit_high, green_5bit_high, blue_5bit_high;
	
	// These are the values that we approximate with:
	int red_low, green_low, blue_low;
	int red_high, green_high, blue_high;

	red_average = avg_col_in[0];
	green_average = avg_col_in[1];
	blue_average = avg_col_in[2];

	// Find the 5-bit reconstruction levels red_low, red_high
	// so that red_average is in interval [red_low, red_high].
	// (The same with green and blue.)

	red_5bit_low = (int) (red_average/kval);
	green_5bit_low = (int) (green_average/kval);
	blue_5bit_low = (int) (blue_average/kval);

	red_5bit_high = CLAMP(0, red_5bit_low + 1, 31);
	green_5bit_high  = CLAMP(0, green_5bit_low + 1, 31);
	blue_5bit_high = CLAMP(0, blue_5bit_low + 1, 31);

	red_low   = (red_5bit_low << 3) | (red_5bit_low >> 2);
	green_low = (green_5bit_low << 3) | (green_5bit_low >> 2);
	blue_low = (blue_5bit_low << 3) | (blue_5bit_low >> 2);

	red_high   = (red_5bit_high << 3) | (red_5bit_high >> 2);
	green_high = (green_5bit_high << 3) | (green_5bit_high >> 2);
	blue_high = (blue_5bit_high << 3) | (blue_5bit_high >> 2);

	kr = (float)red_high - (float)red_low;
	kg = (float)green_high - (float)green_low;
	kb = (float)blue_high - (float)blue_low;

	// Note that dr, dg, and db are all negative.

	dr = red_low - red_average;
	dg = green_low - green_average;
	db = blue_low - blue_average;

	// Use straight (nonperceptive) weights.
	wR2 = (float) 1.0;
	wG2 = (float) 1.0;
	wB2 = (float) 1.0;

	lowhightable[0] = wR2*wG2*SQUARE( (dr+ 0) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+ 0) );
	lowhightable[1] = wR2*wG2*SQUARE( (dr+kr) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+kr) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+ 0) );
	lowhightable[2] = wR2*wG2*SQUARE( (dr+ 0) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+kg) - (db+ 0) );
	lowhightable[3] = wR2*wG2*SQUARE( (dr+ 0) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+kb) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+kb) );
	lowhightable[4] = wR2*wG2*SQUARE( (dr+kr) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+kr) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+kg) - (db+ 0) );
	lowhightable[5] = wR2*wG2*SQUARE( (dr+kr) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+kr) - (db+kb) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+kb) );
	lowhightable[6] = wR2*wG2*SQUARE( (dr+ 0) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+kb) ) + wG2*wB2*SQUARE( (dg+kg) - (db+kb) );
	lowhightable[7] = wR2*wG2*SQUARE( (dr+kr) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+kr) - (db+kb) ) + wG2*wB2*SQUARE( (dg+kg) - (db+kb) );


	float	min_value = lowhightable[0];
	int min_index = 0;

	for(q = 1; q<8; q++)
	{
		if(lowhightable[q] < min_value)
		{
			min_value = lowhightable[q];
			min_index = q;
		}
	}

	low_color[0] = red_5bit_low;
	low_color[1] = green_5bit_low;
	low_color[2] = blue_5bit_low;

	high_color[0] = red_5bit_high;
	high_color[1] = green_5bit_high;
	high_color[2] = blue_5bit_high;

	switch(min_index)
	{
	case 0:
		enc_color[0] = low_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = low_color[2];
		break;
	case 1:
		enc_color[0] = high_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = low_color[2];
		break;
	case 2:	
		enc_color[0] = low_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = low_color[2];
		break;
	case 3:	
		enc_color[0] = low_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = high_color[2];
		break;
	case 4:	
		enc_color[0] = high_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = low_color[2];
		break;
	case 5:	
		enc_color[0] = high_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = high_color[2];
		break;
	case 6:	
		enc_color[0] = low_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = high_color[2];
		break;
	case 7:	
		enc_color[0] = high_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = high_color[2];
		break;
	}

	// Expand 5-bit encoded color to 8-bit color
	avg_color[0] = (enc_color[0] << 3) | (enc_color[0] >> 2);
	avg_color[1] = (enc_color[1] << 3) | (enc_color[1] >> 2);
	avg_color[2] = (enc_color[2] << 3) | (enc_color[2] >> 2);
	
}


// The below code quantizes a float RGB value to RGB444. 
// It is thus the same as the above function quantize444ColorCombined(), but it uses a 
// weighted error metric instead. 
// 
void quantize444ColorCombinedPerceptual(float *avg_col_in, int *enc_color, uint8 *avg_color)
{
	float dr, dg, db;
	float kr, kg, kb;
	float wR2, wG2, wB2;
	uint8 low_color[3];
	uint8 high_color[3];
	float lowhightable[8];
	int q;
	float kval = (float) (255.0/15.0);


	// These are the values that we want to have:
	float red_average, green_average, blue_average;

	int red_4bit_low, green_4bit_low, blue_4bit_low;
	int red_4bit_high, green_4bit_high, blue_4bit_high;
	
	// These are the values that we approximate with:
	int red_low, green_low, blue_low;
	int red_high, green_high, blue_high;

	red_average = avg_col_in[0];
	green_average = avg_col_in[1];
	blue_average = avg_col_in[2];

	// Find the 5-bit reconstruction levels red_low, red_high
	// so that red_average is in interval [red_low, red_high].
	// (The same with green and blue.)

	red_4bit_low = (int) (red_average/kval);
	green_4bit_low = (int) (green_average/kval);
	blue_4bit_low = (int) (blue_average/kval);

	red_4bit_high = CLAMP(0, red_4bit_low + 1, 15);
	green_4bit_high  = CLAMP(0, green_4bit_low + 1, 15);
	blue_4bit_high = CLAMP(0, blue_4bit_low + 1, 15);

	red_low   = (red_4bit_low << 4) | (red_4bit_low >> 0);
	green_low = (green_4bit_low << 4) | (green_4bit_low >> 0);
	blue_low = (blue_4bit_low << 4) | (blue_4bit_low >> 0);

	red_high   = (red_4bit_high << 4) | (red_4bit_high >> 0);
	green_high = (green_4bit_high << 4) | (green_4bit_high >> 0);
	blue_high = (blue_4bit_high << 4) | (blue_4bit_high >> 0);

	low_color[0] = red_4bit_low;
	low_color[1] = green_4bit_low;
	low_color[2] = blue_4bit_low;

	high_color[0] = red_4bit_high;
	high_color[1] = green_4bit_high;
	high_color[2] = blue_4bit_high;

	kr = (float)red_high - (float)red_low;
	kg = (float)green_high - (float)green_low;
	kb = (float)blue_high- (float)blue_low;

	// Note that dr, dg, and db are all negative.

	dr = red_low - red_average;
	dg = green_low - green_average;
	db = blue_low - blue_average;

	// Perceptual weights to use
	wR2 = (float) PERCEPTUAL_WEIGHT_R_SQUARED; 
	wG2 = (float) PERCEPTUAL_WEIGHT_G_SQUARED; 
	wB2 = (float) PERCEPTUAL_WEIGHT_B_SQUARED;

	lowhightable[0] = wR2*wG2*SQUARE( (dr+ 0) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+ 0) );
	lowhightable[1] = wR2*wG2*SQUARE( (dr+kr) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+kr) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+ 0) );
	lowhightable[2] = wR2*wG2*SQUARE( (dr+ 0) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+kg) - (db+ 0) );
	lowhightable[3] = wR2*wG2*SQUARE( (dr+ 0) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+kb) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+kb) );
	lowhightable[4] = wR2*wG2*SQUARE( (dr+kr) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+kr) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+kg) - (db+ 0) );
	lowhightable[5] = wR2*wG2*SQUARE( (dr+kr) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+kr) - (db+kb) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+kb) );
	lowhightable[6] = wR2*wG2*SQUARE( (dr+ 0) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+kb) ) + wG2*wB2*SQUARE( (dg+kg) - (db+kb) );
	lowhightable[7] = wR2*wG2*SQUARE( (dr+kr) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+kr) - (db+kb) ) + wG2*wB2*SQUARE( (dg+kg) - (db+kb) );


	float min_value = lowhightable[0];
	int min_index = 0;

	for(q = 1; q<8; q++)
	{
		if(lowhightable[q] < min_value)
		{
			min_value = lowhightable[q];
			min_index = q;
		}
	}

	switch(min_index)
	{
	case 0:
		enc_color[0] = low_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = low_color[2];
		break;
	case 1:
		enc_color[0] = high_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = low_color[2];
		break;
	case 2:	
		enc_color[0] = low_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = low_color[2];
		break;
	case 3:	
		enc_color[0] = low_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = high_color[2];
		break;
	case 4:	
		enc_color[0] = high_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = low_color[2];
		break;
	case 5:	
		enc_color[0] = high_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = high_color[2];
		break;
	case 6:	
		enc_color[0] = low_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = high_color[2];
		break;
	case 7:	
		enc_color[0] = high_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = high_color[2];
		break;
	}

	// Expand encoded color to eight bits
	avg_color[0] = (enc_color[0] << 4) | enc_color[0];
	avg_color[1] = (enc_color[1] << 4) | enc_color[1];
	avg_color[2] = (enc_color[2] << 4) | enc_color[2];
}


// The below code quantizes a float RGB value to RGB555. 
// It is thus the same as the above function quantize555ColorCombined(), but it uses a 
// weighted error metric instead. 
// 
void quantize555ColorCombinedPerceptual(float *avg_col_in, int *enc_color, uint8 *avg_color)
{
	float dr, dg, db;
	float kr, kg, kb;
	float wR2, wG2, wB2;
	uint8 low_color[3];
	uint8 high_color[3];
	float lowhightable[8];
	int q;
	float kval = (float) (255.0/31.0);


	// These are the values that we want to have:
	float red_average, green_average, blue_average;

	int red_5bit_low, green_5bit_low, blue_5bit_low;
	int red_5bit_high, green_5bit_high, blue_5bit_high;
	
	// These are the values that we approximate with:
	int red_low, green_low, blue_low;
	int red_high, green_high, blue_high;

	red_average = avg_col_in[0];
	green_average = avg_col_in[1];
	blue_average = avg_col_in[2];

	// Find the 5-bit reconstruction levels red_low, red_high
	// so that red_average is in interval [red_low, red_high].
	// (The same with green and blue.)

	red_5bit_low = (int) (red_average/kval);
	green_5bit_low = (int) (green_average/kval);
	blue_5bit_low = (int) (blue_average/kval);

	red_5bit_high = CLAMP(0, red_5bit_low + 1, 31);
	green_5bit_high  = CLAMP(0, green_5bit_low + 1, 31);
	blue_5bit_high = CLAMP(0, blue_5bit_low + 1, 31);

	red_low   = (red_5bit_low << 3) | (red_5bit_low >> 2);
	green_low = (green_5bit_low << 3) | (green_5bit_low >> 2);
	blue_low = (blue_5bit_low << 3) | (blue_5bit_low >> 2);

	red_high   = (red_5bit_high << 3) | (red_5bit_high >> 2);
	green_high = (green_5bit_high << 3) | (green_5bit_high >> 2);
	blue_high = (blue_5bit_high << 3) | (blue_5bit_high >> 2);

	low_color[0] = red_5bit_low;
	low_color[1] = green_5bit_low;
	low_color[2] = blue_5bit_low;

	high_color[0] = red_5bit_high;
	high_color[1] = green_5bit_high;
	high_color[2] = blue_5bit_high;

	kr = (float)red_high - (float)red_low;
	kg = (float)green_high - (float)green_low;
	kb = (float)blue_high - (float)blue_low;

	// Note that dr, dg, and db are all negative.

	dr = red_low - red_average;
	dg = green_low - green_average;
	db = blue_low - blue_average;

	// Perceptual weights to use
	wR2 = (float) PERCEPTUAL_WEIGHT_R_SQUARED; 
	wG2 = (float) PERCEPTUAL_WEIGHT_G_SQUARED; 
	wB2 = (float) PERCEPTUAL_WEIGHT_B_SQUARED;

	lowhightable[0] = wR2*wG2*SQUARE( (dr+ 0) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+ 0) );
	lowhightable[1] = wR2*wG2*SQUARE( (dr+kr) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+kr) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+ 0) );
	lowhightable[2] = wR2*wG2*SQUARE( (dr+ 0) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+kg) - (db+ 0) );
	lowhightable[3] = wR2*wG2*SQUARE( (dr+ 0) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+kb) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+kb) );
	lowhightable[4] = wR2*wG2*SQUARE( (dr+kr) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+kr) - (db+ 0) ) + wG2*wB2*SQUARE( (dg+kg) - (db+ 0) );
	lowhightable[5] = wR2*wG2*SQUARE( (dr+kr) - (dg+ 0) ) + wR2*wB2*SQUARE( (dr+kr) - (db+kb) ) + wG2*wB2*SQUARE( (dg+ 0) - (db+kb) );
	lowhightable[6] = wR2*wG2*SQUARE( (dr+ 0) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+ 0) - (db+kb) ) + wG2*wB2*SQUARE( (dg+kg) - (db+kb) );
	lowhightable[7] = wR2*wG2*SQUARE( (dr+kr) - (dg+kg) ) + wR2*wB2*SQUARE( (dr+kr) - (db+kb) ) + wG2*wB2*SQUARE( (dg+kg) - (db+kb) );


	float min_value = lowhightable[0];
	int min_index = 0;

	for(q = 1; q<8; q++)
	{
		if(lowhightable[q] < min_value)
		{
			min_value = lowhightable[q];
			min_index = q;
		}
	}

	switch(min_index)
	{
	case 0:
		enc_color[0] = low_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = low_color[2];
		break;
	case 1:
		enc_color[0] = high_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = low_color[2];
		break;
	case 2:	
		enc_color[0] = low_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = low_color[2];
		break;
	case 3:	
		enc_color[0] = low_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = high_color[2];
		break;
	case 4:	
		enc_color[0] = high_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = low_color[2];
		break;
	case 5:	
		enc_color[0] = high_color[0];
		enc_color[1] = low_color[1];
		enc_color[2] = high_color[2];
		break;
	case 6:	
		enc_color[0] = low_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = high_color[2];
		break;
	case 7:	
		enc_color[0] = high_color[0];
		enc_color[1] = high_color[1];
		enc_color[2] = high_color[2];
		break;
	}

	// Expand 5-bit encoded color to 8-bit color
	avg_color[0] = (enc_color[0] << 3) | (enc_color[0] >> 2);
	avg_color[1] = (enc_color[1] << 3) | (enc_color[1] >> 2);
	avg_color[2] = (enc_color[2] << 3) | (enc_color[2] >> 2);
	
}


void compressBlockDiffFlipSlow(uint8 *img,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2)
{


	unsigned int compressed1_norm_diff, compressed2_norm_diff;
	unsigned int compressed1_norm_444, compressed2_norm_444;
	unsigned int compressed1_flip_diff, compressed2_flip_diff;
	unsigned int compressed1_flip_444, compressed2_flip_444;
	unsigned int best_err_norm_diff = 255*255*8*3;
	unsigned int best_err_norm_444 = 255*255*8*3;
	unsigned int best_err_flip_diff = 255*255*8*3;
	unsigned int best_err_flip_444 = 255*255*8*3;
	uint8 avg_color_quant1[3], avg_color_quant2[3];

	float avg_color_float1[3],avg_color_float2[3];
	int enc_color1[3], enc_color2[3], diff[3];
	int enc_base1[3], enc_base2[3];
	int enc_try1[3], enc_try2[3];
	int err;
	unsigned int best_pixel_indices1_MSB=0;
	unsigned int best_pixel_indices1_LSB=0;
	unsigned int best_pixel_indices2_MSB=0;
	unsigned int best_pixel_indices2_LSB=0;

	unsigned int best_table1=0, best_table2=0;
    int diffbit;

	int norm_err=0;
	int flip_err=0;
	int minerr;
	int dr1, dg1, db1, dr2, dg2, db2;

	// First try normal blocks 2x4:

	computeAverageColor2x4noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor2x4noQuantFloat(img,width,height,startx+2,starty,avg_color_float2);

	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 

	enc_color1[0] = int( JAS_ROUND(31.0*avg_color_float1[0]/255.0) );
	enc_color1[1] = int( JAS_ROUND(31.0*avg_color_float1[1]/255.0) );
	enc_color1[2] = int( JAS_ROUND(31.0*avg_color_float1[2]/255.0) );
	enc_color2[0] = int( JAS_ROUND(31.0*avg_color_float2[0]/255.0) );
	enc_color2[1] = int( JAS_ROUND(31.0*avg_color_float2[1]/255.0) );
	enc_color2[2] = int( JAS_ROUND(31.0*avg_color_float2[2]/255.0) );
	
	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= SLOW_TRY_MIN) && (diff[0] <= SLOW_TRY_MAX) && (diff[1] >= SLOW_TRY_MIN) && (diff[1] <= SLOW_TRY_MAX) && (diff[2] >= SLOW_TRY_MIN) && (diff[2] <= SLOW_TRY_MAX) )
	{
		diffbit = 1;

		enc_base1[0] = enc_color1[0];
		enc_base1[1] = enc_color1[1];
		enc_base1[2] = enc_color1[2];
		enc_base2[0] = enc_color2[0];
		enc_base2[1] = enc_color2[1];
		enc_base2[2] = enc_color2[2];

		int err1[SLOW_SCAN_RANGE][SLOW_SCAN_RANGE][SLOW_SCAN_RANGE];
		int err2[SLOW_SCAN_RANGE][SLOW_SCAN_RANGE][SLOW_SCAN_RANGE];

		// left part of block 
		for(dr1 = SLOW_SCAN_MIN; dr1<SLOW_SCAN_MAX+1; dr1++)
		{
			for(dg1 = SLOW_SCAN_MIN; dg1<SLOW_SCAN_MAX+1; dg1++)
			{
				for(db1 = SLOW_SCAN_MIN; db1<SLOW_SCAN_MAX+1; db1++)
				{
					enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
					enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
					enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);

					avg_color_quant1[0] = enc_try1[0] << 3 | (enc_try1[0] >> 2);
					avg_color_quant1[1] = enc_try1[1] << 3 | (enc_try1[1] >> 2);
					avg_color_quant1[2] = enc_try1[2] << 3 | (enc_try1[2] >> 2);

					// left part of block
					err1[dr1+SLOW_SCAN_OFFSET][dg1+SLOW_SCAN_OFFSET][db1+SLOW_SCAN_OFFSET] = tryalltables_3bittable2x4(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
				}
			}
		}

		// right part of block
		for(dr2 = SLOW_SCAN_MIN; dr2<SLOW_SCAN_MAX+1; dr2++)
		{
			for(dg2 = SLOW_SCAN_MIN; dg2<SLOW_SCAN_MAX+1; dg2++)
			{
				for(db2 = SLOW_SCAN_MIN; db2<SLOW_SCAN_MAX+1; db2++)
				{
					enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
					enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
					enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

					avg_color_quant2[0] = enc_try2[0] << 3 | (enc_try2[0] >> 2);
					avg_color_quant2[1] = enc_try2[1] << 3 | (enc_try2[1] >> 2);
					avg_color_quant2[2] = enc_try2[2] << 3 | (enc_try2[2] >> 2);

					// left part of block
					err2[dr2+SLOW_SCAN_OFFSET][dg2+SLOW_SCAN_OFFSET][db2+SLOW_SCAN_OFFSET] = tryalltables_3bittable2x4(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);
				}
			}
		}

		// Now see what combinations are both low in error and possible to
		// encode differentially.

		minerr = 255*255*3*8*2;

		for(dr1 = SLOW_SCAN_MIN; dr1<SLOW_SCAN_MAX+1; dr1++)
		{
			for(dg1 = SLOW_SCAN_MIN; dg1<SLOW_SCAN_MAX+1; dg1++)
			{
				for(db1 = SLOW_SCAN_MIN; db1<SLOW_SCAN_MAX+1; db1++)
				{
					for(dr2 = SLOW_SCAN_MIN; dr2<SLOW_SCAN_MAX+1; dr2++)
					{
						for(dg2 = SLOW_SCAN_MIN; dg2<SLOW_SCAN_MAX+1; dg2++)
						{
							for(db2 = SLOW_SCAN_MIN; db2<SLOW_SCAN_MAX+1; db2++)
							{								
								enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
								enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
								enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);
								enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
								enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
								enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

								// We must make sure that the difference between the tries still is less than allowed

								diff[0] = enc_try2[0]-enc_try1[0];	
								diff[1] = enc_try2[1]-enc_try1[1];	
								diff[2] = enc_try2[2]-enc_try1[2];

							    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
								{
									// The diff is OK, calculate total error:
									
									err = err1[dr1+SLOW_SCAN_OFFSET][dg1+SLOW_SCAN_OFFSET][db1+SLOW_SCAN_OFFSET] + err2[dr2+SLOW_SCAN_OFFSET][dg2+SLOW_SCAN_OFFSET][db2+SLOW_SCAN_OFFSET];

									if(err < minerr)
									{
										minerr = err;

										enc_color1[0] = enc_try1[0];
										enc_color1[1] = enc_try1[1];
										enc_color1[2] = enc_try1[2];
										enc_color2[0] = enc_try2[0];
										enc_color2[1] = enc_try2[1];
										enc_color2[2] = enc_try2[2];
									}
								}
							}
						}
					}
				}
			}
		}

		best_err_norm_diff = minerr;
		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		
		// Pack bits into the first word. 


	
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_norm_diff = 0;
		PUTBITSHIGH( compressed1_norm_diff, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm_diff, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_norm_diff, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_norm_diff, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_norm_diff, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_norm_diff, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_norm_diff, diff[2],       3, 42);

		
		// left part of block
		tryalltables_3bittable2x4(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		tryalltables_3bittable2x4(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm_diff, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_norm_diff, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_norm_diff, 0,             1, 32);

		compressed2_norm_diff = 0;
		PUTBITS( compressed2_norm_diff, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm_diff, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm_diff, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm_diff, (best_pixel_indices2_LSB     ), 8, 15);

	}
	// We should do this in any case...
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.


		// Color for left block

		int besterr = 255*255*3*8;
        int bestri = 0, bestgi = 0, bestbi = 0;
		int ri, gi, bi;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color1[0] = ri;
					enc_color1[1] = gi;
					enc_color1[2] = bi;

					avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
					avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
					avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];

					// left part of block
					err = tryalltables_3bittable2x4(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB,best_pixel_indices1_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}

		norm_err = besterr;
		
		enc_color1[0] = bestri;
		enc_color1[1] = bestgi;
		enc_color1[2] = bestbi;
		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];


		// Color for right block

		besterr = 255*255*3*8;
        bestri = 0; bestgi = 0; bestbi = 0;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color2[0] = ri;
					enc_color2[1] = gi;
					enc_color2[2] = bi;

					avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
					avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
					avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];

					// left part of block
					err = tryalltables_3bittable2x4(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB,best_pixel_indices2_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}


		norm_err += besterr;
		best_err_norm_444 = norm_err;

		enc_color2[0] = bestri;
		enc_color2[1] = bestgi;
		enc_color2[2] = bestbi;
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];


		// Pack bits into the first word. 
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_norm_444 = 0;
		PUTBITSHIGH( compressed1_norm_444, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm_444, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_norm_444, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_norm_444, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_norm_444, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_norm_444, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_norm_444, enc_color2[2], 4, 43);


		// left part of block
		tryalltables_3bittable2x4(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		tryalltables_3bittable2x4(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm_444, best_table1, 3, 39);
 		PUTBITSHIGH( compressed1_norm_444, best_table2, 3, 36);
 		PUTBITSHIGH( compressed1_norm_444, 0,           1, 32);

		compressed2_norm_444 = 0;
		PUTBITS( compressed2_norm_444, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm_444, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm_444, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm_444, (best_pixel_indices2_LSB     ), 8, 15);

	}

	// Now try flipped blocks 4x2:

	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty+2,avg_color_float2);

	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 

	enc_color1[0] = int( JAS_ROUND(31.0*avg_color_float1[0]/255.0) );
	enc_color1[1] = int( JAS_ROUND(31.0*avg_color_float1[1]/255.0) );
	enc_color1[2] = int( JAS_ROUND(31.0*avg_color_float1[2]/255.0) );
	enc_color2[0] = int( JAS_ROUND(31.0*avg_color_float2[0]/255.0) );
	enc_color2[1] = int( JAS_ROUND(31.0*avg_color_float2[1]/255.0) );
	enc_color2[2] = int( JAS_ROUND(31.0*avg_color_float2[2]/255.0) );

	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= SLOW_TRY_MIN) && (diff[0] <= SLOW_TRY_MAX) && (diff[1] >= SLOW_TRY_MIN) && (diff[1] <= SLOW_TRY_MAX) && (diff[2] >= SLOW_TRY_MIN) && (diff[2] <= SLOW_TRY_MAX) )
	{
		diffbit = 1;

		enc_base1[0] = enc_color1[0];
		enc_base1[1] = enc_color1[1];
		enc_base1[2] = enc_color1[2];
		enc_base2[0] = enc_color2[0];
		enc_base2[1] = enc_color2[1];
		enc_base2[2] = enc_color2[2];

		int err1[SLOW_SCAN_RANGE][SLOW_SCAN_RANGE][SLOW_SCAN_RANGE];
		int err2[SLOW_SCAN_RANGE][SLOW_SCAN_RANGE][SLOW_SCAN_RANGE];

		// upper part of block
		for(dr1 = SLOW_SCAN_MIN; dr1<SLOW_SCAN_MAX+1; dr1++)
		{
			for(dg1 = SLOW_SCAN_MIN; dg1<SLOW_SCAN_MAX+1; dg1++)
			{
				for(db1 = SLOW_SCAN_MIN; db1<SLOW_SCAN_MAX+1; db1++)
				{
					enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
					enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
					enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);

					avg_color_quant1[0] = enc_try1[0] << 3 | (enc_try1[0] >> 2);
					avg_color_quant1[1] = enc_try1[1] << 3 | (enc_try1[1] >> 2);
					avg_color_quant1[2] = enc_try1[2] << 3 | (enc_try1[2] >> 2);

					// upper part of block
					err1[dr1+SLOW_SCAN_OFFSET][dg1+SLOW_SCAN_OFFSET][db1+SLOW_SCAN_OFFSET] = tryalltables_3bittable4x2(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
				}
			}
		}

		// lower part of block
		for(dr2 = SLOW_SCAN_MIN; dr2<SLOW_SCAN_MAX+1; dr2++)
		{
			for(dg2 = SLOW_SCAN_MIN; dg2<SLOW_SCAN_MAX+1; dg2++)
			{
				for(db2 = SLOW_SCAN_MIN; db2<SLOW_SCAN_MAX+1; db2++)
				{
					enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
					enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
					enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

					avg_color_quant2[0] = enc_try2[0] << 3 | (enc_try2[0] >> 2);
					avg_color_quant2[1] = enc_try2[1] << 3 | (enc_try2[1] >> 2);
					avg_color_quant2[2] = enc_try2[2] << 3 | (enc_try2[2] >> 2);

					// lower part of block
					err2[dr2+SLOW_SCAN_OFFSET][dg2+SLOW_SCAN_OFFSET][db2+SLOW_SCAN_OFFSET] = tryalltables_3bittable4x2(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);
				}
			}
		}

		// Now see what combinations are both low in error and possible to
		// encode differentially.

		minerr = 255*255*3*8*2;

		for(dr1 = SLOW_SCAN_MIN; dr1<SLOW_SCAN_MAX+1; dr1++)
		{
			for(dg1 = SLOW_SCAN_MIN; dg1<SLOW_SCAN_MAX+1; dg1++)
			{
				for(db1 = SLOW_SCAN_MIN; db1<SLOW_SCAN_MAX+1; db1++)
				{
					for(dr2 = SLOW_SCAN_MIN; dr2<SLOW_SCAN_MAX+1; dr2++)
					{
						for(dg2 = SLOW_SCAN_MIN; dg2<SLOW_SCAN_MAX+1; dg2++)
						{
							for(db2 = SLOW_SCAN_MIN; db2<SLOW_SCAN_MAX+1; db2++)
							{								
								enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
								enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
								enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);
								enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
								enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
								enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

								// We must make sure that the difference between the tries still is less than allowed

								diff[0] = enc_try2[0]-enc_try1[0];	
								diff[1] = enc_try2[1]-enc_try1[1];	
								diff[2] = enc_try2[2]-enc_try1[2];

							    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
								{
									// The diff is OK, calculate total error:
									
									err = err1[dr1+SLOW_SCAN_OFFSET][dg1+SLOW_SCAN_OFFSET][db1+SLOW_SCAN_OFFSET] + err2[dr2+SLOW_SCAN_OFFSET][dg2+SLOW_SCAN_OFFSET][db2+SLOW_SCAN_OFFSET];

									if(err < minerr)
									{
										minerr = err;

										enc_color1[0] = enc_try1[0];
										enc_color1[1] = enc_try1[1];
										enc_color1[2] = enc_try1[2];
										enc_color2[0] = enc_try2[0];
										enc_color2[1] = enc_try2[1];
										enc_color2[2] = enc_try2[2];
									}
								}
							}
						}
					}
				}
			}
		}


		flip_err = minerr;

		best_err_flip_diff = flip_err;

		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		// Pack bits into the first word. 

		compressed1_flip_diff = 0;
		PUTBITSHIGH( compressed1_flip_diff, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip_diff, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_flip_diff, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_flip_diff, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_flip_diff, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_flip_diff, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_flip_diff, diff[2],       3, 42);




		// upper part of block
		tryalltables_3bittable4x2(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
		// lower part of block
		tryalltables_3bittable4x2(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);


 		PUTBITSHIGH( compressed1_flip_diff, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip_diff, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip_diff, 1,             1, 32);


		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip_diff = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);
	
	}
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.


		// Color for upper block

		int besterr = 255*255*3*8;
        int bestri = 0, bestgi = 0, bestbi = 0;
		int ri, gi, bi;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color1[0] = ri;
					enc_color1[1] = gi;
					enc_color1[2] = bi;

					avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
					avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
					avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];

					// upper part of block
					err = tryalltables_3bittable4x2(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}

		flip_err = besterr;

		enc_color1[0] = bestri;
		enc_color1[1] = bestgi;
		enc_color1[2] = bestbi;
		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];


		// Color for lower block

		besterr = 255*255*3*8;
        bestri = 0; bestgi = 0; bestbi = 0;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color2[0] = ri;
					enc_color2[1] = gi;
					enc_color2[2] = bi;

					avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
					avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
					avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];

					// left part of block
					err = tryalltables_3bittable4x2(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}

		flip_err += besterr;
		best_err_flip_444 = flip_err;

		enc_color2[0] = bestri;
		enc_color2[1] = bestgi;
		enc_color2[2] = bestbi;
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];


		// Pack bits into the first word. 
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_flip_444 = 0;
		PUTBITSHIGH( compressed1_flip_444, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip_444, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_flip_444, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_flip_444, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_flip_444, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_flip_444, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_flip_444, enc_color2[2], 4, 43);

		// upper part of block
		tryalltables_3bittable4x2(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB,best_pixel_indices1_LSB);

		// lower part of block
		tryalltables_3bittable4x2(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_flip_444, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip_444, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip_444, 1,             1, 32);

		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip_444 = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);


	}

	// Now lets see which is the best table to use. Only 8 tables are possible. 

	int compressed1_norm;
	int compressed2_norm;
	int compressed1_flip;
	int compressed2_flip;


	// See which of the norm blocks is better

	if(best_err_norm_diff <= best_err_norm_444)
	{
		compressed1_norm = compressed1_norm_diff;
		compressed2_norm = compressed2_norm_diff;
		norm_err = best_err_norm_diff;
	}
	else
	{
		compressed1_norm = compressed1_norm_444;
		compressed2_norm = compressed2_norm_444;
		norm_err = best_err_norm_444;
	}

	// See which of the flip blocks is better

	if(best_err_flip_diff <= best_err_flip_444)
	{
		compressed1_flip = compressed1_flip_diff;
		compressed2_flip = compressed2_flip_diff;
		flip_err = best_err_flip_diff;
	}
	else
	{
		compressed1_flip = compressed1_flip_444;
		compressed2_flip = compressed2_flip_444;
		flip_err = best_err_flip_444;
	}

	// See if flip or norm is better

	unsigned int best_of_all;

	if(norm_err <= flip_err)
	{

		compressed1 = compressed1_norm | 0;
		compressed2 = compressed2_norm;
		best_of_all = norm_err;
	}
	else
	{

		compressed1 = compressed1_flip | 1;
		compressed2 = compressed2_flip;
		best_of_all = flip_err;
	}

}

void compressBlockDiffFlipMedium(uint8 *img,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2)
{


	unsigned int compressed1_norm_diff, compressed2_norm_diff;
	unsigned int compressed1_norm_444, compressed2_norm_444;
	unsigned int compressed1_flip_diff, compressed2_flip_diff;
	unsigned int compressed1_flip_444, compressed2_flip_444;
	unsigned int best_err_norm_diff = 255*255*16*3;
	unsigned int best_err_norm_444 = 255*255*16*3;
	unsigned int best_err_flip_diff = 255*255*16*3;
	unsigned int best_err_flip_444 = 255*255*16*3;
	uint8 avg_color_quant1[3], avg_color_quant2[3];

	float avg_color_float1[3],avg_color_float2[3];
	int enc_color1[3], enc_color2[3], diff[3];
	int enc_base1[3], enc_base2[3];
	int enc_try1[3], enc_try2[3];
	int err;
	unsigned int best_pixel_indices1_MSB=0;
	unsigned int best_pixel_indices1_LSB=0;
	unsigned int best_pixel_indices2_MSB=0;
	unsigned int best_pixel_indices2_LSB=0;

	unsigned int best_table1=0, best_table2=0;
    int diffbit;

	int norm_err=0;
	int flip_err=0;
	int minerr;
	int dr1, dg1, db1, dr2, dg2, db2;

	// First try normal blocks 2x4:

	computeAverageColor2x4noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor2x4noQuantFloat(img,width,height,startx+2,starty,avg_color_float2);



	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 

	enc_color1[0] = int( JAS_ROUND(31.0*avg_color_float1[0]/255.0) );
	enc_color1[1] = int( JAS_ROUND(31.0*avg_color_float1[1]/255.0) );
	enc_color1[2] = int( JAS_ROUND(31.0*avg_color_float1[2]/255.0) );
	enc_color2[0] = int( JAS_ROUND(31.0*avg_color_float2[0]/255.0) );
	enc_color2[1] = int( JAS_ROUND(31.0*avg_color_float2[1]/255.0) );
	enc_color2[2] = int( JAS_ROUND(31.0*avg_color_float2[2]/255.0) );
	
	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= MEDIUM_TRY_MIN) && (diff[0] <= MEDIUM_TRY_MAX) && (diff[1] >= MEDIUM_TRY_MIN) && (diff[1] <= MEDIUM_TRY_MAX) && (diff[2] >= MEDIUM_TRY_MIN) && (diff[2] <= MEDIUM_TRY_MAX) )
	{
		diffbit = 1;

		enc_base1[0] = enc_color1[0];
		enc_base1[1] = enc_color1[1];
		enc_base1[2] = enc_color1[2];
		enc_base2[0] = enc_color2[0];
		enc_base2[1] = enc_color2[1];
		enc_base2[2] = enc_color2[2];

		int err1[MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE];
		int err2[MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE];

		// left part of block 
		for(dr1 = MEDIUM_SCAN_MIN; dr1<MEDIUM_SCAN_MAX+1; dr1++)
		{
			for(dg1 = MEDIUM_SCAN_MIN; dg1<MEDIUM_SCAN_MAX+1; dg1++)
			{
				for(db1 = MEDIUM_SCAN_MIN; db1<MEDIUM_SCAN_MAX+1; db1++)
				{
					enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
					enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
					enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);

					avg_color_quant1[0] = enc_try1[0] << 3 | (enc_try1[0] >> 2);
					avg_color_quant1[1] = enc_try1[1] << 3 | (enc_try1[1] >> 2);
					avg_color_quant1[2] = enc_try1[2] << 3 | (enc_try1[2] >> 2);

					// left part of block
					err1[dr1+MEDIUM_SCAN_OFFSET][dg1+MEDIUM_SCAN_OFFSET][db1+MEDIUM_SCAN_OFFSET] = tryalltables_3bittable2x4(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
				}
			}
		}

		// right part of block
		for(dr2 = MEDIUM_SCAN_MIN; dr2<MEDIUM_SCAN_MAX+1; dr2++)
		{
			for(dg2 = MEDIUM_SCAN_MIN; dg2<MEDIUM_SCAN_MAX+1; dg2++)
			{
				for(db2 = MEDIUM_SCAN_MIN; db2<MEDIUM_SCAN_MAX+1; db2++)
				{
					enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
					enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
					enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

					avg_color_quant2[0] = enc_try2[0] << 3 | (enc_try2[0] >> 2);
					avg_color_quant2[1] = enc_try2[1] << 3 | (enc_try2[1] >> 2);
					avg_color_quant2[2] = enc_try2[2] << 3 | (enc_try2[2] >> 2);

					// left part of block
					err2[dr2+MEDIUM_SCAN_OFFSET][dg2+MEDIUM_SCAN_OFFSET][db2+MEDIUM_SCAN_OFFSET] = tryalltables_3bittable2x4(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);
				}
			}
		}

		// Now see what combinations are both low in error and possible to
		// encode differentially.

		minerr = 255*255*3*8*2;

		for(dr1 = MEDIUM_SCAN_MIN; dr1<MEDIUM_SCAN_MAX+1; dr1++)
		{
			for(dg1 = MEDIUM_SCAN_MIN; dg1<MEDIUM_SCAN_MAX+1; dg1++)
			{
				for(db1 = MEDIUM_SCAN_MIN; db1<MEDIUM_SCAN_MAX+1; db1++)
				{
					for(dr2 = MEDIUM_SCAN_MIN; dr2<MEDIUM_SCAN_MAX+1; dr2++)
					{
						for(dg2 = MEDIUM_SCAN_MIN; dg2<MEDIUM_SCAN_MAX+1; dg2++)
						{
							for(db2 = MEDIUM_SCAN_MIN; db2<MEDIUM_SCAN_MAX+1; db2++)
							{								
								enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
								enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
								enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);
								enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
								enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
								enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

								// We must make sure that the difference between the tries still is less than allowed

								diff[0] = enc_try2[0]-enc_try1[0];	
								diff[1] = enc_try2[1]-enc_try1[1];	
								diff[2] = enc_try2[2]-enc_try1[2];

							    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
								{
									// The diff is OK, calculate total error:
									
									err = err1[dr1+MEDIUM_SCAN_OFFSET][dg1+MEDIUM_SCAN_OFFSET][db1+MEDIUM_SCAN_OFFSET] + err2[dr2+MEDIUM_SCAN_OFFSET][dg2+MEDIUM_SCAN_OFFSET][db2+MEDIUM_SCAN_OFFSET];

									if(err < minerr)
									{
										minerr = err;

										enc_color1[0] = enc_try1[0];
										enc_color1[1] = enc_try1[1];
										enc_color1[2] = enc_try1[2];
										enc_color2[0] = enc_try2[0];
										enc_color2[1] = enc_try2[1];
										enc_color2[2] = enc_try2[2];
									}
								}
							}
						}
					}
				}
			}
		}

		best_err_norm_diff = minerr;
		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		
		// Pack bits into the first word. 


	
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_norm_diff = 0;
		PUTBITSHIGH( compressed1_norm_diff, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm_diff, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_norm_diff, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_norm_diff, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_norm_diff, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_norm_diff, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_norm_diff, diff[2],       3, 42);

		
		// left part of block
		tryalltables_3bittable2x4(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		tryalltables_3bittable2x4(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm_diff, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_norm_diff, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_norm_diff, 0,             1, 32);

		compressed2_norm_diff = 0;
		PUTBITS( compressed2_norm_diff, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm_diff, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm_diff, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm_diff, (best_pixel_indices2_LSB     ), 8, 15);


	}
	else
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.


		// Color for left block

		int besterr = 255*255*3*8;
        int bestri = 0, bestgi = 0, bestbi = 0;
		int ri, gi, bi;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color1[0] = ri;
					enc_color1[1] = gi;
					enc_color1[2] = bi;

					avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
					avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
					avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];

					// left part of block
					err = tryalltables_3bittable2x4(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB,best_pixel_indices1_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}

		norm_err = besterr;
		
		enc_color1[0] = bestri;
		enc_color1[1] = bestgi;
		enc_color1[2] = bestbi;
		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];


		// Color for right block

		besterr = 255*255*3*8;
        bestri = 0; bestgi = 0; bestbi = 0;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color2[0] = ri;
					enc_color2[1] = gi;
					enc_color2[2] = bi;

					avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
					avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
					avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];

					// left part of block
					err = tryalltables_3bittable2x4(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB,best_pixel_indices2_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}


		norm_err += besterr;
		best_err_norm_444 = norm_err;

		enc_color2[0] = bestri;
		enc_color2[1] = bestgi;
		enc_color2[2] = bestbi;
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];


		// Pack bits into the first word. 
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_norm_444 = 0;
		PUTBITSHIGH( compressed1_norm_444, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm_444, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_norm_444, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_norm_444, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_norm_444, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_norm_444, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_norm_444, enc_color2[2], 4, 43);


		// left part of block
		tryalltables_3bittable2x4(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		tryalltables_3bittable2x4(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm_444, best_table1, 3, 39);
 		PUTBITSHIGH( compressed1_norm_444, best_table2, 3, 36);
 		PUTBITSHIGH( compressed1_norm_444, 0,           1, 32);

		compressed2_norm_444 = 0;
		PUTBITS( compressed2_norm_444, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm_444, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm_444, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm_444, (best_pixel_indices2_LSB     ), 8, 15);

	}

	// Now try flipped blocks 4x2:

	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty+2,avg_color_float2);

	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 

	enc_color1[0] = int( JAS_ROUND(31.0*avg_color_float1[0]/255.0) );
	enc_color1[1] = int( JAS_ROUND(31.0*avg_color_float1[1]/255.0) );
	enc_color1[2] = int( JAS_ROUND(31.0*avg_color_float1[2]/255.0) );
	enc_color2[0] = int( JAS_ROUND(31.0*avg_color_float2[0]/255.0) );
	enc_color2[1] = int( JAS_ROUND(31.0*avg_color_float2[1]/255.0) );
	enc_color2[2] = int( JAS_ROUND(31.0*avg_color_float2[2]/255.0) );
	
	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= MEDIUM_TRY_MIN) && (diff[0] <= MEDIUM_TRY_MAX) && (diff[1] >= MEDIUM_TRY_MIN) && (diff[1] <= MEDIUM_TRY_MAX) && (diff[2] >= MEDIUM_TRY_MIN) && (diff[2] <= MEDIUM_TRY_MAX) )
	{
		diffbit = 1;

		enc_base1[0] = enc_color1[0];
		enc_base1[1] = enc_color1[1];
		enc_base1[2] = enc_color1[2];
		enc_base2[0] = enc_color2[0];
		enc_base2[1] = enc_color2[1];
		enc_base2[2] = enc_color2[2];

		int err1[MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE];
		int err2[MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE];

		// upper part of block
		for(dr1 = MEDIUM_SCAN_MIN; dr1<MEDIUM_SCAN_MAX+1; dr1++)
		{
			for(dg1 = MEDIUM_SCAN_MIN; dg1<MEDIUM_SCAN_MAX+1; dg1++)
			{
				for(db1 = MEDIUM_SCAN_MIN; db1<MEDIUM_SCAN_MAX+1; db1++)
				{
					enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
					enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
					enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);

					avg_color_quant1[0] = enc_try1[0] << 3 | (enc_try1[0] >> 2);
					avg_color_quant1[1] = enc_try1[1] << 3 | (enc_try1[1] >> 2);
					avg_color_quant1[2] = enc_try1[2] << 3 | (enc_try1[2] >> 2);

					// upper part of block
					err1[dr1+MEDIUM_SCAN_OFFSET][dg1+MEDIUM_SCAN_OFFSET][db1+MEDIUM_SCAN_OFFSET] = tryalltables_3bittable4x2(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
				}
			}
		}

		// lower part of block
		for(dr2 = MEDIUM_SCAN_MIN; dr2<MEDIUM_SCAN_MAX+1; dr2++)
		{
			for(dg2 = MEDIUM_SCAN_MIN; dg2<MEDIUM_SCAN_MAX+1; dg2++)
			{
				for(db2 = MEDIUM_SCAN_MIN; db2<MEDIUM_SCAN_MAX+1; db2++)
				{
					enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
					enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
					enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

					avg_color_quant2[0] = enc_try2[0] << 3 | (enc_try2[0] >> 2);
					avg_color_quant2[1] = enc_try2[1] << 3 | (enc_try2[1] >> 2);
					avg_color_quant2[2] = enc_try2[2] << 3 | (enc_try2[2] >> 2);

					// lower part of block
					err2[dr2+MEDIUM_SCAN_OFFSET][dg2+MEDIUM_SCAN_OFFSET][db2+MEDIUM_SCAN_OFFSET] = tryalltables_3bittable4x2(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);
				}
			}
		}

		// Now see what combinations are both low in error and possible to
		// encode differentially.

		minerr = 255*255*3*8*2;

		for(dr1 = MEDIUM_SCAN_MIN; dr1<MEDIUM_SCAN_MAX+1; dr1++)
		{
			for(dg1 = MEDIUM_SCAN_MIN; dg1<MEDIUM_SCAN_MAX+1; dg1++)
			{
				for(db1 = MEDIUM_SCAN_MIN; db1<MEDIUM_SCAN_MAX+1; db1++)
				{
					for(dr2 = MEDIUM_SCAN_MIN; dr2<MEDIUM_SCAN_MAX+1; dr2++)
					{
						for(dg2 = MEDIUM_SCAN_MIN; dg2<MEDIUM_SCAN_MAX+1; dg2++)
						{
							for(db2 = MEDIUM_SCAN_MIN; db2<MEDIUM_SCAN_MAX+1; db2++)
							{								
								enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
								enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
								enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);
								enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
								enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
								enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

								// We must make sure that the difference between the tries still is less than allowed

								diff[0] = enc_try2[0]-enc_try1[0];	
								diff[1] = enc_try2[1]-enc_try1[1];	
								diff[2] = enc_try2[2]-enc_try1[2];

							    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
								{
									// The diff is OK, calculate total error:
									
									err = err1[dr1+MEDIUM_SCAN_OFFSET][dg1+MEDIUM_SCAN_OFFSET][db1+MEDIUM_SCAN_OFFSET] + err2[dr2+MEDIUM_SCAN_OFFSET][dg2+MEDIUM_SCAN_OFFSET][db2+MEDIUM_SCAN_OFFSET];

									if(err < minerr)
									{
										minerr = err;

										enc_color1[0] = enc_try1[0];
										enc_color1[1] = enc_try1[1];
										enc_color1[2] = enc_try1[2];
										enc_color2[0] = enc_try2[0];
										enc_color2[1] = enc_try2[1];
										enc_color2[2] = enc_try2[2];
									}
								}
							}
						}
					}
				}
			}
		}


		flip_err = minerr;

		best_err_flip_diff = flip_err;

		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		// Pack bits into the first word. 

		compressed1_flip_diff = 0;
		PUTBITSHIGH( compressed1_flip_diff, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip_diff, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_flip_diff, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_flip_diff, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_flip_diff, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_flip_diff, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_flip_diff, diff[2],       3, 42);




		// upper part of block
		tryalltables_3bittable4x2(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
		// lower part of block
		tryalltables_3bittable4x2(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);


 		PUTBITSHIGH( compressed1_flip_diff, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip_diff, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip_diff, 1,             1, 32);


		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip_diff = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);
	
	}
	else
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.


		// Color for upper block

		int besterr = 255*255*3*8;
        int bestri = 0, bestgi = 0, bestbi = 0;
		int ri, gi, bi;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color1[0] = ri;
					enc_color1[1] = gi;
					enc_color1[2] = bi;

					avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
					avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
					avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];

					// upper part of block
					err = tryalltables_3bittable4x2(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}

		flip_err = besterr;

		enc_color1[0] = bestri;
		enc_color1[1] = bestgi;
		enc_color1[2] = bestbi;
		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];


		// Color for lower block

		besterr = 255*255*3*8;
        bestri = 0; bestgi = 0; bestbi = 0;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color2[0] = ri;
					enc_color2[1] = gi;
					enc_color2[2] = bi;

					avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
					avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
					avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];

					// left part of block
					err = tryalltables_3bittable4x2(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}

		flip_err += besterr;
		best_err_flip_444 = flip_err;

		enc_color2[0] = bestri;
		enc_color2[1] = bestgi;
		enc_color2[2] = bestbi;
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];


		// Pack bits into the first word. 
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_flip_444 = 0;
		PUTBITSHIGH( compressed1_flip_444, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip_444, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_flip_444, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_flip_444, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_flip_444, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_flip_444, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_flip_444, enc_color2[2], 4, 43);

		// upper part of block
		tryalltables_3bittable4x2(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB,best_pixel_indices1_LSB);

		// lower part of block
		tryalltables_3bittable4x2(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_flip_444, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip_444, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip_444, 1,             1, 32);

		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip_444 = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);


	}

	// Now lets see which is the best table to use. Only 8 tables are possible. 

	int compressed1_norm;
	int compressed2_norm;
	int compressed1_flip;
	int compressed2_flip;


	// See which of the norm blocks is better

	if(best_err_norm_diff <= best_err_norm_444)
	{
		compressed1_norm = compressed1_norm_diff;
		compressed2_norm = compressed2_norm_diff;
		norm_err = best_err_norm_diff;
	}
	else
	{
		compressed1_norm = compressed1_norm_444;
		compressed2_norm = compressed2_norm_444;
		norm_err = best_err_norm_444;
	}

	// See which of the flip blocks is better

	if(best_err_flip_diff <= best_err_flip_444)
	{
		compressed1_flip = compressed1_flip_diff;
		compressed2_flip = compressed2_flip_diff;
		flip_err = best_err_flip_diff;
	}
	else
	{
		compressed1_flip = compressed1_flip_444;
		compressed2_flip = compressed2_flip_444;
		flip_err = best_err_flip_444;
	}

	// See if flip or norm is better

	unsigned int best_of_all;

	if(norm_err <= flip_err)
	{

		compressed1 = compressed1_norm | 0;
		compressed2 = compressed2_norm;
		best_of_all = norm_err;
	}
	else
	{

		compressed1 = compressed1_flip | 1;
		compressed2 = compressed2_flip;
		best_of_all = flip_err;
	}

}
void compressBlockDiffFlipSlowPerceptual(uint8 *img,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2)
{


	unsigned int compressed1_norm_diff, compressed2_norm_diff;
	unsigned int compressed1_norm_444, compressed2_norm_444;
	unsigned int compressed1_flip_diff, compressed2_flip_diff;
	unsigned int compressed1_flip_444, compressed2_flip_444;
	unsigned int best_err_norm_diff = 255*255*8*3;
	unsigned int best_err_norm_444 = 255*255*8*3;
	unsigned int best_err_flip_diff = 255*255*8*3;
	unsigned int best_err_flip_444 = 255*255*8*3;
	uint8 avg_color_quant1[3], avg_color_quant2[3];

	float avg_color_float1[3],avg_color_float2[3];
	int enc_color1[3], enc_color2[3], diff[3];
	int enc_base1[3], enc_base2[3];
	int enc_try1[3], enc_try2[3];
	int err;
	unsigned int best_pixel_indices1_MSB=0;
	unsigned int best_pixel_indices1_LSB=0;
	unsigned int best_pixel_indices2_MSB=0;
	unsigned int best_pixel_indices2_LSB=0;

	unsigned int best_table1=0, best_table2=0;
    int diffbit;

	int norm_err=0;
	int flip_err=0;
	int minerr;
	int dr1, dg1, db1, dr2, dg2, db2;

	// First try normal blocks 2x4:

	computeAverageColor2x4noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor2x4noQuantFloat(img,width,height,startx+2,starty,avg_color_float2);



	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 

	enc_color1[0] = int( JAS_ROUND(31.0*avg_color_float1[0]/255.0) );
	enc_color1[1] = int( JAS_ROUND(31.0*avg_color_float1[1]/255.0) );
	enc_color1[2] = int( JAS_ROUND(31.0*avg_color_float1[2]/255.0) );
	enc_color2[0] = int( JAS_ROUND(31.0*avg_color_float2[0]/255.0) );
	enc_color2[1] = int( JAS_ROUND(31.0*avg_color_float2[1]/255.0) );
	enc_color2[2] = int( JAS_ROUND(31.0*avg_color_float2[2]/255.0) );

	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= SLOW_TRY_MIN) && (diff[0] <= SLOW_TRY_MAX) && (diff[1] >= SLOW_TRY_MIN) && (diff[1] <= SLOW_TRY_MAX) && (diff[2] >= SLOW_TRY_MIN) && (diff[2] <= SLOW_TRY_MAX) )
	{
		diffbit = 1;

		enc_base1[0] = enc_color1[0];
		enc_base1[1] = enc_color1[1];
		enc_base1[2] = enc_color1[2];
		enc_base2[0] = enc_color2[0];
		enc_base2[1] = enc_color2[1];
		enc_base2[2] = enc_color2[2];

		int err1[SLOW_SCAN_RANGE][SLOW_SCAN_RANGE][SLOW_SCAN_RANGE];
		int err2[SLOW_SCAN_RANGE][SLOW_SCAN_RANGE][SLOW_SCAN_RANGE];

		// left part of block 
		for(dr1 = SLOW_SCAN_MIN; dr1<SLOW_SCAN_MAX+1; dr1++)
		{
			for(dg1 = SLOW_SCAN_MIN; dg1<SLOW_SCAN_MAX+1; dg1++)
			{
				for(db1 = SLOW_SCAN_MIN; db1<SLOW_SCAN_MAX+1; db1++)
				{
					enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
					enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
					enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);

					avg_color_quant1[0] = enc_try1[0] << 3 | (enc_try1[0] >> 2);
					avg_color_quant1[1] = enc_try1[1] << 3 | (enc_try1[1] >> 2);
					avg_color_quant1[2] = enc_try1[2] << 3 | (enc_try1[2] >> 2);

					// left part of block
					err1[dr1+SLOW_SCAN_OFFSET][dg1+SLOW_SCAN_OFFSET][db1+SLOW_SCAN_OFFSET] = tryalltables_3bittable2x4percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
				}
			}
		}

		// right part of block
		for(dr2 = SLOW_SCAN_MIN; dr2<SLOW_SCAN_MAX+1; dr2++)
		{
			for(dg2 = SLOW_SCAN_MIN; dg2<SLOW_SCAN_MAX+1; dg2++)
			{
				for(db2 = SLOW_SCAN_MIN; db2<SLOW_SCAN_MAX+1; db2++)
				{
					enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
					enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
					enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

					avg_color_quant2[0] = enc_try2[0] << 3 | (enc_try2[0] >> 2);
					avg_color_quant2[1] = enc_try2[1] << 3 | (enc_try2[1] >> 2);
					avg_color_quant2[2] = enc_try2[2] << 3 | (enc_try2[2] >> 2);

					// left part of block
					err2[dr2+SLOW_SCAN_OFFSET][dg2+SLOW_SCAN_OFFSET][db2+SLOW_SCAN_OFFSET] = tryalltables_3bittable2x4percep(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);
				}
			}
		}

		// Now see what combinations are both low in error and possible to
		// encode differentially.

		minerr = 255*255*3*8*2;

		for(dr1 = SLOW_SCAN_MIN; dr1<SLOW_SCAN_MAX+1; dr1++)
		{
			for(dg1 = SLOW_SCAN_MIN; dg1<SLOW_SCAN_MAX+1; dg1++)
			{
				for(db1 = SLOW_SCAN_MIN; db1<SLOW_SCAN_MAX+1; db1++)
				{
					for(dr2 = SLOW_SCAN_MIN; dr2<SLOW_SCAN_MAX+1; dr2++)
					{
						for(dg2 = SLOW_SCAN_MIN; dg2<SLOW_SCAN_MAX+1; dg2++)
						{
							for(db2 = SLOW_SCAN_MIN; db2<SLOW_SCAN_MAX+1; db2++)
							{								
								enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
								enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
								enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);
								enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
								enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
								enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

								// We must make sure that the difference between the tries still is less than allowed

								diff[0] = enc_try2[0]-enc_try1[0];	
								diff[1] = enc_try2[1]-enc_try1[1];	
								diff[2] = enc_try2[2]-enc_try1[2];

							    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
								{
									// The diff is OK, calculate total error:
									
									err = err1[dr1+SLOW_SCAN_OFFSET][dg1+SLOW_SCAN_OFFSET][db1+SLOW_SCAN_OFFSET] + err2[dr2+SLOW_SCAN_OFFSET][dg2+SLOW_SCAN_OFFSET][db2+SLOW_SCAN_OFFSET];

									if(err < minerr)
									{
										minerr = err;

										enc_color1[0] = enc_try1[0];
										enc_color1[1] = enc_try1[1];
										enc_color1[2] = enc_try1[2];
										enc_color2[0] = enc_try2[0];
										enc_color2[1] = enc_try2[1];
										enc_color2[2] = enc_try2[2];
									}
								}
							}
						}
					}
				}
			}
		}

		best_err_norm_diff = minerr;
		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		
		// Pack bits into the first word. 


	
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_norm_diff = 0;
		PUTBITSHIGH( compressed1_norm_diff, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm_diff, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_norm_diff, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_norm_diff, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_norm_diff, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_norm_diff, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_norm_diff, diff[2],       3, 42);

		
		// left part of block
		tryalltables_3bittable2x4percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		tryalltables_3bittable2x4percep(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm_diff, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_norm_diff, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_norm_diff, 0,             1, 32);

		compressed2_norm_diff = 0;
		PUTBITS( compressed2_norm_diff, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm_diff, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm_diff, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm_diff, (best_pixel_indices2_LSB     ), 8, 15);


	}
	// We should do this in any case...
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.


		// Color for left block

		int besterr = 255*255*3*8;
        int bestri = 0, bestgi = 0, bestbi = 0;
		int ri, gi, bi;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color1[0] = ri;
					enc_color1[1] = gi;
					enc_color1[2] = bi;

					avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
					avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
					avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];

					// left part of block
					err = tryalltables_3bittable2x4percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB,best_pixel_indices1_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}

		norm_err = besterr;
		
		enc_color1[0] = bestri;
		enc_color1[1] = bestgi;
		enc_color1[2] = bestbi;
		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];


		// Color for right block

		besterr = 255*255*3*8;
        bestri = 0; bestgi = 0; bestbi = 0;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color2[0] = ri;
					enc_color2[1] = gi;
					enc_color2[2] = bi;

					avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
					avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
					avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];

					// left part of block
					err = tryalltables_3bittable2x4percep(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB,best_pixel_indices2_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}


		norm_err += besterr;
		best_err_norm_444 = norm_err;

		enc_color2[0] = bestri;
		enc_color2[1] = bestgi;
		enc_color2[2] = bestbi;
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];


		// Pack bits into the first word. 
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_norm_444 = 0;
		PUTBITSHIGH( compressed1_norm_444, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm_444, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_norm_444, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_norm_444, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_norm_444, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_norm_444, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_norm_444, enc_color2[2], 4, 43);


		// left part of block
		tryalltables_3bittable2x4percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		tryalltables_3bittable2x4percep(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm_444, best_table1, 3, 39);
 		PUTBITSHIGH( compressed1_norm_444, best_table2, 3, 36);
 		PUTBITSHIGH( compressed1_norm_444, 0,           1, 32);

		compressed2_norm_444 = 0;
		PUTBITS( compressed2_norm_444, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm_444, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm_444, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm_444, (best_pixel_indices2_LSB     ), 8, 15);

	}

	// Now try flipped blocks 4x2:

	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty+2,avg_color_float2);

	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 

	enc_color1[0] = int( JAS_ROUND(31.0*avg_color_float1[0]/255.0) );
	enc_color1[1] = int( JAS_ROUND(31.0*avg_color_float1[1]/255.0) );
	enc_color1[2] = int( JAS_ROUND(31.0*avg_color_float1[2]/255.0) );
	enc_color2[0] = int( JAS_ROUND(31.0*avg_color_float2[0]/255.0) );
	enc_color2[1] = int( JAS_ROUND(31.0*avg_color_float2[1]/255.0) );
	enc_color2[2] = int( JAS_ROUND(31.0*avg_color_float2[2]/255.0) );

	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= SLOW_TRY_MIN) && (diff[0] <= SLOW_TRY_MAX) && (diff[1] >= SLOW_TRY_MIN) && (diff[1] <= SLOW_TRY_MAX) && (diff[2] >= SLOW_TRY_MIN) && (diff[2] <= SLOW_TRY_MAX) )
	{
		diffbit = 1;

		enc_base1[0] = enc_color1[0];
		enc_base1[1] = enc_color1[1];
		enc_base1[2] = enc_color1[2];
		enc_base2[0] = enc_color2[0];
		enc_base2[1] = enc_color2[1];
		enc_base2[2] = enc_color2[2];

		int err1[SLOW_SCAN_RANGE][SLOW_SCAN_RANGE][SLOW_SCAN_RANGE];
		int err2[SLOW_SCAN_RANGE][SLOW_SCAN_RANGE][SLOW_SCAN_RANGE];

		// upper part of block
		for(dr1 = SLOW_SCAN_MIN; dr1<SLOW_SCAN_MAX+1; dr1++)
		{
			for(dg1 = SLOW_SCAN_MIN; dg1<SLOW_SCAN_MAX+1; dg1++)
			{
				for(db1 = SLOW_SCAN_MIN; db1<SLOW_SCAN_MAX+1; db1++)
				{
					enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
					enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
					enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);

					avg_color_quant1[0] = enc_try1[0] << 3 | (enc_try1[0] >> 2);
					avg_color_quant1[1] = enc_try1[1] << 3 | (enc_try1[1] >> 2);
					avg_color_quant1[2] = enc_try1[2] << 3 | (enc_try1[2] >> 2);

					// upper part of block
					err1[dr1+SLOW_SCAN_OFFSET][dg1+SLOW_SCAN_OFFSET][db1+SLOW_SCAN_OFFSET] = tryalltables_3bittable4x2percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
				}
			}
		}

		// lower part of block
		for(dr2 = SLOW_SCAN_MIN; dr2<SLOW_SCAN_MAX+1; dr2++)
		{
			for(dg2 = SLOW_SCAN_MIN; dg2<SLOW_SCAN_MAX+1; dg2++)
			{
				for(db2 = SLOW_SCAN_MIN; db2<SLOW_SCAN_MAX+1; db2++)
				{
					enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
					enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
					enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

					avg_color_quant2[0] = enc_try2[0] << 3 | (enc_try2[0] >> 2);
					avg_color_quant2[1] = enc_try2[1] << 3 | (enc_try2[1] >> 2);
					avg_color_quant2[2] = enc_try2[2] << 3 | (enc_try2[2] >> 2);

					// lower part of block
					err2[dr2+SLOW_SCAN_OFFSET][dg2+SLOW_SCAN_OFFSET][db2+SLOW_SCAN_OFFSET] = tryalltables_3bittable4x2percep(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);
				}
			}
		}

		// Now see what combinations are both low in error and possible to
		// encode differentially.

		minerr = 255*255*3*8*2;

		for(dr1 = SLOW_SCAN_MIN; dr1<SLOW_SCAN_MAX+1; dr1++)
		{
			for(dg1 = SLOW_SCAN_MIN; dg1<SLOW_SCAN_MAX+1; dg1++)
			{
				for(db1 = SLOW_SCAN_MIN; db1<SLOW_SCAN_MAX+1; db1++)
				{
					for(dr2 = SLOW_SCAN_MIN; dr2<SLOW_SCAN_MAX+1; dr2++)
					{
						for(dg2 = SLOW_SCAN_MIN; dg2<SLOW_SCAN_MAX+1; dg2++)
						{
							for(db2 = SLOW_SCAN_MIN; db2<SLOW_SCAN_MAX+1; db2++)
							{								
								enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
								enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
								enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);
								enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
								enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
								enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

								// We must make sure that the difference between the tries still is less than allowed

								diff[0] = enc_try2[0]-enc_try1[0];	
								diff[1] = enc_try2[1]-enc_try1[1];	
								diff[2] = enc_try2[2]-enc_try1[2];

							    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
								{
									// The diff is OK, calculate total error:
									
									err = err1[dr1+SLOW_SCAN_OFFSET][dg1+SLOW_SCAN_OFFSET][db1+SLOW_SCAN_OFFSET] + err2[dr2+SLOW_SCAN_OFFSET][dg2+SLOW_SCAN_OFFSET][db2+SLOW_SCAN_OFFSET];

									if(err < minerr)
									{
										minerr = err;

										enc_color1[0] = enc_try1[0];
										enc_color1[1] = enc_try1[1];
										enc_color1[2] = enc_try1[2];
										enc_color2[0] = enc_try2[0];
										enc_color2[1] = enc_try2[1];
										enc_color2[2] = enc_try2[2];
									}
								}
							}
						}
					}
				}
			}
		}


		flip_err = minerr;

		best_err_flip_diff = flip_err;

		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		// Pack bits into the first word. 

		compressed1_flip_diff = 0;
		PUTBITSHIGH( compressed1_flip_diff, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip_diff, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_flip_diff, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_flip_diff, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_flip_diff, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_flip_diff, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_flip_diff, diff[2],       3, 42);




		// upper part of block
		tryalltables_3bittable4x2percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
		// lower part of block
		tryalltables_3bittable4x2percep(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);


 		PUTBITSHIGH( compressed1_flip_diff, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip_diff, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip_diff, 1,             1, 32);


		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip_diff = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);
	
	}
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.


		// Color for upper block

		int besterr = 255*255*3*8;
        int bestri = 0, bestgi = 0, bestbi = 0;
		int ri, gi, bi;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color1[0] = ri;
					enc_color1[1] = gi;
					enc_color1[2] = bi;

					avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
					avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
					avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];

					// upper part of block
					err = tryalltables_3bittable4x2percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}

		flip_err = besterr;

		enc_color1[0] = bestri;
		enc_color1[1] = bestgi;
		enc_color1[2] = bestbi;
		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];


		// Color for lower block

		besterr = 255*255*3*8;
        bestri = 0; bestgi = 0; bestbi = 0;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color2[0] = ri;
					enc_color2[1] = gi;
					enc_color2[2] = bi;

					avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
					avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
					avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];

					// left part of block
					err = tryalltables_3bittable4x2percep(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}

		flip_err += besterr;
		best_err_flip_444 = flip_err;

		enc_color2[0] = bestri;
		enc_color2[1] = bestgi;
		enc_color2[2] = bestbi;
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];


		// Pack bits into the first word. 
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_flip_444 = 0;
		PUTBITSHIGH( compressed1_flip_444, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip_444, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_flip_444, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_flip_444, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_flip_444, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_flip_444, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_flip_444, enc_color2[2], 4, 43);

		// upper part of block
		tryalltables_3bittable4x2percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB,best_pixel_indices1_LSB);

		// lower part of block
		tryalltables_3bittable4x2percep(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_flip_444, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip_444, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip_444, 1,             1, 32);

		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip_444 = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);


	}

	// Now lets see which is the best table to use. Only 8 tables are possible. 

	int compressed1_norm;
	int compressed2_norm;
	int compressed1_flip;
	int compressed2_flip;


	// See which of the norm blocks is better

	if(best_err_norm_diff <= best_err_norm_444)
	{
		compressed1_norm = compressed1_norm_diff;
		compressed2_norm = compressed2_norm_diff;
		norm_err = best_err_norm_diff;
	}
	else
	{
		compressed1_norm = compressed1_norm_444;
		compressed2_norm = compressed2_norm_444;
		norm_err = best_err_norm_444;
	}

	// See which of the flip blocks is better

	if(best_err_flip_diff <= best_err_flip_444)
	{
		compressed1_flip = compressed1_flip_diff;
		compressed2_flip = compressed2_flip_diff;
		flip_err = best_err_flip_diff;
	}
	else
	{
		compressed1_flip = compressed1_flip_444;
		compressed2_flip = compressed2_flip_444;
		flip_err = best_err_flip_444;
	}

	// See if flip or norm is better

	unsigned int best_of_all;

	if(norm_err <= flip_err)
	{

		compressed1 = compressed1_norm | 0;
		compressed2 = compressed2_norm;
		best_of_all = norm_err;
	}
	else
	{

		compressed1 = compressed1_flip | 1;
		compressed2 = compressed2_flip;
		best_of_all = flip_err;
	}

}
 
void compressBlockDiffFlipMediumPerceptual(uint8 *img,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2)
{
	unsigned int compressed1_norm_diff = 0, compressed2_norm_diff = 0;
	unsigned int compressed1_norm_444 = 0, compressed2_norm_444 = 0;
	unsigned int compressed1_flip_diff = 0, compressed2_flip_diff = 0;
	unsigned int compressed1_flip_444 = 0, compressed2_flip_444 = 0;
	unsigned int best_err_norm_diff = 255*255*16*3;
	unsigned int best_err_norm_444 = 255*255*16*3;
	unsigned int best_err_flip_diff = 255*255*16*3;
	unsigned int best_err_flip_444 = 255*255*16*3;
	uint8 avg_color_quant1[3], avg_color_quant2[3];

	float avg_color_float1[3],avg_color_float2[3];
	int enc_color1[3], enc_color2[3], diff[3];
	int enc_base1[3], enc_base2[3];
	int enc_try1[3], enc_try2[3];
	int err;
	unsigned int best_pixel_indices1_MSB=0;
	unsigned int best_pixel_indices1_LSB=0;
	unsigned int best_pixel_indices2_MSB=0;
	unsigned int best_pixel_indices2_LSB=0;

	unsigned int best_table1=0, best_table2=0;
    int diffbit;

	int norm_err=0;
	int flip_err=0;
	int minerr;
	int dr1, dg1, db1, dr2, dg2, db2;

	// First try normal blocks 2x4:

	computeAverageColor2x4noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor2x4noQuantFloat(img,width,height,startx+2,starty,avg_color_float2);



	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 

	enc_color1[0] = int( JAS_ROUND(31.0*avg_color_float1[0]/255.0) );
	enc_color1[1] = int( JAS_ROUND(31.0*avg_color_float1[1]/255.0) );
	enc_color1[2] = int( JAS_ROUND(31.0*avg_color_float1[2]/255.0) );
	enc_color2[0] = int( JAS_ROUND(31.0*avg_color_float2[0]/255.0) );
	enc_color2[1] = int( JAS_ROUND(31.0*avg_color_float2[1]/255.0) );
	enc_color2[2] = int( JAS_ROUND(31.0*avg_color_float2[2]/255.0) );

	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= MEDIUM_TRY_MIN) && (diff[0] <= MEDIUM_TRY_MAX) && (diff[1] >= MEDIUM_TRY_MIN) && (diff[1] <= MEDIUM_TRY_MAX) && (diff[2] >= MEDIUM_TRY_MIN) && (diff[2] <= MEDIUM_TRY_MAX) )
	{
		diffbit = 1;

		enc_base1[0] = enc_color1[0];
		enc_base1[1] = enc_color1[1];
		enc_base1[2] = enc_color1[2];
		enc_base2[0] = enc_color2[0];
		enc_base2[1] = enc_color2[1];
		enc_base2[2] = enc_color2[2];

		int err1[MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE];
		int err2[MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE];

		// left part of block 
		for(dr1 = MEDIUM_SCAN_MIN; dr1<MEDIUM_SCAN_MAX+1; dr1++)
		{
			for(dg1 = MEDIUM_SCAN_MIN; dg1<MEDIUM_SCAN_MAX+1; dg1++)
			{
				for(db1 = MEDIUM_SCAN_MIN; db1<MEDIUM_SCAN_MAX+1; db1++)
				{
					enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
					enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
					enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);

					avg_color_quant1[0] = enc_try1[0] << 3 | (enc_try1[0] >> 2);
					avg_color_quant1[1] = enc_try1[1] << 3 | (enc_try1[1] >> 2);
					avg_color_quant1[2] = enc_try1[2] << 3 | (enc_try1[2] >> 2);

					// left part of block
					err1[dr1+MEDIUM_SCAN_OFFSET][dg1+MEDIUM_SCAN_OFFSET][db1+MEDIUM_SCAN_OFFSET] = tryalltables_3bittable2x4percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
				}
			}
		}

		// right part of block
		for(dr2 = MEDIUM_SCAN_MIN; dr2<MEDIUM_SCAN_MAX+1; dr2++)
		{
			for(dg2 = MEDIUM_SCAN_MIN; dg2<MEDIUM_SCAN_MAX+1; dg2++)
			{
				for(db2 = MEDIUM_SCAN_MIN; db2<MEDIUM_SCAN_MAX+1; db2++)
				{
					enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
					enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
					enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

					avg_color_quant2[0] = enc_try2[0] << 3 | (enc_try2[0] >> 2);
					avg_color_quant2[1] = enc_try2[1] << 3 | (enc_try2[1] >> 2);
					avg_color_quant2[2] = enc_try2[2] << 3 | (enc_try2[2] >> 2);

					// left part of block
					err2[dr2+MEDIUM_SCAN_OFFSET][dg2+MEDIUM_SCAN_OFFSET][db2+MEDIUM_SCAN_OFFSET] = tryalltables_3bittable2x4percep(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);
				}
			}
		}

		// Now see what combinations are both low in error and possible to
		// encode differentially.

		minerr = 255*255*3*8*2;

		for(dr1 = MEDIUM_SCAN_MIN; dr1<MEDIUM_SCAN_MAX+1; dr1++)
		{
			for(dg1 = MEDIUM_SCAN_MIN; dg1<MEDIUM_SCAN_MAX+1; dg1++)
			{
				for(db1 = MEDIUM_SCAN_MIN; db1<MEDIUM_SCAN_MAX+1; db1++)
				{
					for(dr2 = MEDIUM_SCAN_MIN; dr2<MEDIUM_SCAN_MAX+1; dr2++)
					{
						for(dg2 = MEDIUM_SCAN_MIN; dg2<MEDIUM_SCAN_MAX+1; dg2++)
						{
							for(db2 = MEDIUM_SCAN_MIN; db2<MEDIUM_SCAN_MAX+1; db2++)
							{								
								enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
								enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
								enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);
								enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
								enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
								enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

								// We must make sure that the difference between the tries still is less than allowed

								diff[0] = enc_try2[0]-enc_try1[0];	
								diff[1] = enc_try2[1]-enc_try1[1];	
								diff[2] = enc_try2[2]-enc_try1[2];

							    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
								{
									// The diff is OK, calculate total error:
									
									err = err1[dr1+MEDIUM_SCAN_OFFSET][dg1+MEDIUM_SCAN_OFFSET][db1+MEDIUM_SCAN_OFFSET] + err2[dr2+MEDIUM_SCAN_OFFSET][dg2+MEDIUM_SCAN_OFFSET][db2+MEDIUM_SCAN_OFFSET];

									if(err < minerr)
									{
										minerr = err;

										enc_color1[0] = enc_try1[0];
										enc_color1[1] = enc_try1[1];
										enc_color1[2] = enc_try1[2];
										enc_color2[0] = enc_try2[0];
										enc_color2[1] = enc_try2[1];
										enc_color2[2] = enc_try2[2];
									}
								}
							}
						}
					}
				}
			}
		}

		best_err_norm_diff = minerr;
		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		
		// Pack bits into the first word. 


	
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_norm_diff = 0;
		PUTBITSHIGH( compressed1_norm_diff, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm_diff, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_norm_diff, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_norm_diff, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_norm_diff, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_norm_diff, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_norm_diff, diff[2],       3, 42);

		
		// left part of block
		tryalltables_3bittable2x4percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		tryalltables_3bittable2x4percep(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm_diff, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_norm_diff, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_norm_diff, 0,             1, 32);

		compressed2_norm_diff = 0;
		PUTBITS( compressed2_norm_diff, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm_diff, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm_diff, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm_diff, (best_pixel_indices2_LSB     ), 8, 15);


	}
	else
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.


		// Color for left block

		int besterr = 255*255*3*8;
        int bestri = 0, bestgi = 0, bestbi = 0;
		int ri, gi, bi;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color1[0] = ri;
					enc_color1[1] = gi;
					enc_color1[2] = bi;

					avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
					avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
					avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];

					// left part of block
					err = tryalltables_3bittable2x4percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB,best_pixel_indices1_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}

		norm_err = besterr;
		
		enc_color1[0] = bestri;
		enc_color1[1] = bestgi;
		enc_color1[2] = bestbi;
		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];


		// Color for right block

		besterr = 255*255*3*8;
        bestri = 0; bestgi = 0; bestbi = 0;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color2[0] = ri;
					enc_color2[1] = gi;
					enc_color2[2] = bi;

					avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
					avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
					avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];

					// left part of block
					err = tryalltables_3bittable2x4percep(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB,best_pixel_indices2_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}


		norm_err += besterr;
		best_err_norm_444 = norm_err;

		enc_color2[0] = bestri;
		enc_color2[1] = bestgi;
		enc_color2[2] = bestbi;
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];


		// Pack bits into the first word. 
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_norm_444 = 0;
		PUTBITSHIGH( compressed1_norm_444, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm_444, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_norm_444, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_norm_444, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_norm_444, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_norm_444, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_norm_444, enc_color2[2], 4, 43);


		// left part of block
		tryalltables_3bittable2x4percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		tryalltables_3bittable2x4percep(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm_444, best_table1, 3, 39);
 		PUTBITSHIGH( compressed1_norm_444, best_table2, 3, 36);
 		PUTBITSHIGH( compressed1_norm_444, 0,           1, 32);

		compressed2_norm_444 = 0;
		PUTBITS( compressed2_norm_444, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm_444, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm_444, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm_444, (best_pixel_indices2_LSB     ), 8, 15);

	}

	// Now try flipped blocks 4x2:

	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty+2,avg_color_float2);

	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 

	enc_color1[0] = int( JAS_ROUND(31.0*avg_color_float1[0]/255.0) );
	enc_color1[1] = int( JAS_ROUND(31.0*avg_color_float1[1]/255.0) );
	enc_color1[2] = int( JAS_ROUND(31.0*avg_color_float1[2]/255.0) );
	enc_color2[0] = int( JAS_ROUND(31.0*avg_color_float2[0]/255.0) );
	enc_color2[1] = int( JAS_ROUND(31.0*avg_color_float2[1]/255.0) );
	enc_color2[2] = int( JAS_ROUND(31.0*avg_color_float2[2]/255.0) );

	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= MEDIUM_TRY_MIN) && (diff[0] <= MEDIUM_TRY_MAX) && (diff[1] >= MEDIUM_TRY_MIN) && (diff[1] <= MEDIUM_TRY_MAX) && (diff[2] >= MEDIUM_TRY_MIN) && (diff[2] <= MEDIUM_TRY_MAX) )
	{
		diffbit = 1;

		enc_base1[0] = enc_color1[0];
		enc_base1[1] = enc_color1[1];
		enc_base1[2] = enc_color1[2];
		enc_base2[0] = enc_color2[0];
		enc_base2[1] = enc_color2[1];
		enc_base2[2] = enc_color2[2];

		int err1[MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE];
		int err2[MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE][MEDIUM_SCAN_RANGE];

		// upper part of block
		for(dr1 = MEDIUM_SCAN_MIN; dr1<MEDIUM_SCAN_MAX+1; dr1++)
		{
			for(dg1 = MEDIUM_SCAN_MIN; dg1<MEDIUM_SCAN_MAX+1; dg1++)
			{
				for(db1 = MEDIUM_SCAN_MIN; db1<MEDIUM_SCAN_MAX+1; db1++)
				{
					enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
					enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
					enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);

					avg_color_quant1[0] = enc_try1[0] << 3 | (enc_try1[0] >> 2);
					avg_color_quant1[1] = enc_try1[1] << 3 | (enc_try1[1] >> 2);
					avg_color_quant1[2] = enc_try1[2] << 3 | (enc_try1[2] >> 2);

					// upper part of block
					err1[dr1+MEDIUM_SCAN_OFFSET][dg1+MEDIUM_SCAN_OFFSET][db1+MEDIUM_SCAN_OFFSET] = tryalltables_3bittable4x2percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
				}
			}
		}

		// lower part of block
		for(dr2 = MEDIUM_SCAN_MIN; dr2<MEDIUM_SCAN_MAX+1; dr2++)
		{
			for(dg2 = MEDIUM_SCAN_MIN; dg2<MEDIUM_SCAN_MAX+1; dg2++)
			{
				for(db2 = MEDIUM_SCAN_MIN; db2<MEDIUM_SCAN_MAX+1; db2++)
				{
					enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
					enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
					enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

					avg_color_quant2[0] = enc_try2[0] << 3 | (enc_try2[0] >> 2);
					avg_color_quant2[1] = enc_try2[1] << 3 | (enc_try2[1] >> 2);
					avg_color_quant2[2] = enc_try2[2] << 3 | (enc_try2[2] >> 2);

					// lower part of block
					err2[dr2+MEDIUM_SCAN_OFFSET][dg2+MEDIUM_SCAN_OFFSET][db2+MEDIUM_SCAN_OFFSET] = tryalltables_3bittable4x2percep(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);
				}
			}
		}

		// Now see what combinations are both low in error and possible to
		// encode differentially.

		minerr = 255*255*3*8*2;

		for(dr1 = MEDIUM_SCAN_MIN; dr1<MEDIUM_SCAN_MAX+1; dr1++)
		{
			for(dg1 = MEDIUM_SCAN_MIN; dg1<MEDIUM_SCAN_MAX+1; dg1++)
			{
				for(db1 = MEDIUM_SCAN_MIN; db1<MEDIUM_SCAN_MAX+1; db1++)
				{
					for(dr2 = MEDIUM_SCAN_MIN; dr2<MEDIUM_SCAN_MAX+1; dr2++)
					{
						for(dg2 = MEDIUM_SCAN_MIN; dg2<MEDIUM_SCAN_MAX+1; dg2++)
						{
							for(db2 = MEDIUM_SCAN_MIN; db2<MEDIUM_SCAN_MAX+1; db2++)
							{								
								enc_try1[0] = CLAMP(0,enc_base1[0]+dr1,31);
								enc_try1[1] = CLAMP(0,enc_base1[1]+dg1,31);
								enc_try1[2] = CLAMP(0,enc_base1[2]+db1,31);
								enc_try2[0] = CLAMP(0,enc_base2[0]+dr2,31);
								enc_try2[1] = CLAMP(0,enc_base2[1]+dg2,31);
								enc_try2[2] = CLAMP(0,enc_base2[2]+db2,31);

								// We must make sure that the difference between the tries still is less than allowed

								diff[0] = enc_try2[0]-enc_try1[0];	
								diff[1] = enc_try2[1]-enc_try1[1];	
								diff[2] = enc_try2[2]-enc_try1[2];

							    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
								{
									// The diff is OK, calculate total error:
									
									err = err1[dr1+MEDIUM_SCAN_OFFSET][dg1+MEDIUM_SCAN_OFFSET][db1+MEDIUM_SCAN_OFFSET] + err2[dr2+MEDIUM_SCAN_OFFSET][dg2+MEDIUM_SCAN_OFFSET][db2+MEDIUM_SCAN_OFFSET];

									if(err < minerr)
									{
										minerr = err;

										enc_color1[0] = enc_try1[0];
										enc_color1[1] = enc_try1[1];
										enc_color1[2] = enc_try1[2];
										enc_color2[0] = enc_try2[0];
										enc_color2[1] = enc_try2[1];
										enc_color2[2] = enc_try2[2];
									}
								}
							}
						}
					}
				}
			}
		}


		flip_err = minerr;

		best_err_flip_diff = flip_err;

		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		// Pack bits into the first word. 

		compressed1_flip_diff = 0;
		PUTBITSHIGH( compressed1_flip_diff, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip_diff, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_flip_diff, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_flip_diff, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_flip_diff, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_flip_diff, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_flip_diff, diff[2],       3, 42);




		// upper part of block
		tryalltables_3bittable4x2percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
		// lower part of block
		tryalltables_3bittable4x2percep(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);


 		PUTBITSHIGH( compressed1_flip_diff, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip_diff, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip_diff, 1,             1, 32);


		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip_diff = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);
	
	}
	else
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.


		// Color for upper block

		int besterr = 255*255*3*8;
        int bestri = 0, bestgi = 0, bestbi = 0;
		int ri, gi, bi;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color1[0] = ri;
					enc_color1[1] = gi;
					enc_color1[2] = bi;

					avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
					avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
					avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];

					// upper part of block
					err = tryalltables_3bittable4x2percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}

		flip_err = besterr;

		enc_color1[0] = bestri;
		enc_color1[1] = bestgi;
		enc_color1[2] = bestbi;
		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];


		// Color for lower block

		besterr = 255*255*3*8;
        bestri = 0; bestgi = 0; bestbi = 0;

		for(ri = 0; ri<15; ri++)
		{
			for(gi = 0; gi<15; gi++)
			{
				for(bi = 0; bi<15; bi++)
				{
					enc_color2[0] = ri;
					enc_color2[1] = gi;
					enc_color2[2] = bi;

					avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
					avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
					avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];

					// left part of block
					err = tryalltables_3bittable4x2percep(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

					if(err<besterr)
					{
						bestri = ri; bestgi = gi; bestbi = bi;
						besterr = err;
					}


				}
			}
		}

		flip_err += besterr;
		best_err_flip_444 = flip_err;

		enc_color2[0] = bestri;
		enc_color2[1] = bestgi;
		enc_color2[2] = bestbi;
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];


		// Pack bits into the first word. 
		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_flip_444 = 0;
		PUTBITSHIGH( compressed1_flip_444, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip_444, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_flip_444, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_flip_444, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_flip_444, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_flip_444, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_flip_444, enc_color2[2], 4, 43);

		// upper part of block
		tryalltables_3bittable4x2percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB,best_pixel_indices1_LSB);

		// lower part of block
		tryalltables_3bittable4x2percep(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_flip_444, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip_444, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip_444, 1,             1, 32);

		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip_444 = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);


	}

	// Now lets see which is the best table to use. Only 8 tables are possible. 

	int compressed1_norm;
	int compressed2_norm;
	int compressed1_flip;
	int compressed2_flip;


	// See which of the norm blocks is better

	if(best_err_norm_diff <= best_err_norm_444)
	{
		compressed1_norm = compressed1_norm_diff;
		compressed2_norm = compressed2_norm_diff;
		norm_err = best_err_norm_diff;
	}
	else
	{
		compressed1_norm = compressed1_norm_444;
		compressed2_norm = compressed2_norm_444;
		norm_err = best_err_norm_444;
	}

	// See which of the flip blocks is better

	if(best_err_flip_diff <= best_err_flip_444)
	{
		compressed1_flip = compressed1_flip_diff;
		compressed2_flip = compressed2_flip_diff;
		flip_err = best_err_flip_diff;
	}
	else
	{
		compressed1_flip = compressed1_flip_444;
		compressed2_flip = compressed2_flip_444;
		flip_err = best_err_flip_444;
	}

	// See if flip or norm is better

	unsigned int best_of_all;

	if(norm_err <= flip_err)
	{

		compressed1 = compressed1_norm | 0;
		compressed2 = compressed2_norm;
		best_of_all = norm_err;
	}
	else
	{

		compressed1 = compressed1_flip | 1;
		compressed2 = compressed2_flip;
		best_of_all = flip_err;
	}

}

void compressBlockDiffFlipAverage(uint8 *img,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2)
{
	unsigned int compressed1_norm, compressed2_norm;
	unsigned int compressed1_flip, compressed2_flip;
	uint8 avg_color_quant1[3], avg_color_quant2[3];

	float avg_color_float1[3],avg_color_float2[3];
	int enc_color1[3], enc_color2[3], diff[3];
	unsigned int best_table1=0, best_table2=0;
    int diffbit;

	int norm_err=0;
	int flip_err=0;

	// First try normal blocks 2x4:

	computeAverageColor2x4noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor2x4noQuantFloat(img,width,height,startx+2,starty,avg_color_float2);

	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 


	float eps;

	enc_color1[0] = int( JAS_ROUND(31.0*avg_color_float1[0]/255.0) );
	enc_color1[1] = int( JAS_ROUND(31.0*avg_color_float1[1]/255.0) );
	enc_color1[2] = int( JAS_ROUND(31.0*avg_color_float1[2]/255.0) );
	enc_color2[0] = int( JAS_ROUND(31.0*avg_color_float2[0]/255.0) );
	enc_color2[1] = int( JAS_ROUND(31.0*avg_color_float2[1]/255.0) );
	enc_color2[2] = int( JAS_ROUND(31.0*avg_color_float2[2]/255.0) );

	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
	{
		diffbit = 1;

		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		// Pack bits into the first word. 

		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_norm = 0;
		PUTBITSHIGH( compressed1_norm, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_norm, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_norm, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_norm, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_norm, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_norm, diff[2],       3, 42);

		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		norm_err = 0;

		// left part of block
		norm_err = tryalltables_3bittable2x4(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		norm_err += tryalltables_3bittable2x4(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_norm, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_norm,           0,   1, 32);

		compressed2_norm = 0;
		PUTBITS( compressed2_norm, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm, (best_pixel_indices2_LSB     ), 8, 15);

	}
	else
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.

		eps = (float) 0.0001;

		enc_color1[0] = int( ((float) avg_color_float1[0] / (17.0)) +0.5 + eps);
		enc_color1[1] = int( ((float) avg_color_float1[1] / (17.0)) +0.5 + eps);
		enc_color1[2] = int( ((float) avg_color_float1[2] / (17.0)) +0.5 + eps);
		enc_color2[0] = int( ((float) avg_color_float2[0] / (17.0)) +0.5 + eps);
		enc_color2[1] = int( ((float) avg_color_float2[1] / (17.0)) +0.5 + eps);
		enc_color2[2] = int( ((float) avg_color_float2[2] / (17.0)) +0.5 + eps);
		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];
	

		// Pack bits into the first word. 

		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------

		compressed1_norm = 0;
		PUTBITSHIGH( compressed1_norm, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_norm, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_norm, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_norm, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_norm, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_norm, enc_color2[2], 4, 43);

		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		
		// left part of block
		norm_err = tryalltables_3bittable2x4(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		norm_err += tryalltables_3bittable2x4(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_norm, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_norm,           0,   1, 32);

		compressed2_norm = 0;
		PUTBITS( compressed2_norm, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm, (best_pixel_indices2_LSB     ), 8, 15);

	
	}

	// Now try flipped blocks 4x2:

	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty+2,avg_color_float2);

	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 

	enc_color1[0] = int( JAS_ROUND(31.0*avg_color_float1[0]/255.0) );
	enc_color1[1] = int( JAS_ROUND(31.0*avg_color_float1[1]/255.0) );
	enc_color1[2] = int( JAS_ROUND(31.0*avg_color_float1[2]/255.0) );
	enc_color2[0] = int( JAS_ROUND(31.0*avg_color_float2[0]/255.0) );
	enc_color2[1] = int( JAS_ROUND(31.0*avg_color_float2[1]/255.0) );
	enc_color2[2] = int( JAS_ROUND(31.0*avg_color_float2[2]/255.0) );

	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
	{
		diffbit = 1;

		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		// Pack bits into the first word. 

		compressed1_flip = 0;
		PUTBITSHIGH( compressed1_flip, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_flip, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_flip, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_flip, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_flip, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_flip, diff[2],       3, 42);



		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		// upper part of block
		flip_err = tryalltables_3bittable4x2(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
		// lower part of block
		flip_err += tryalltables_3bittable4x2(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_flip, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip,           1,   1, 32);

		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);

	
	}
	else
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.
		eps = (float) 0.0001;

		enc_color1[0] = int( ((float) avg_color_float1[0] / (17.0)) +0.5 + eps);
		enc_color1[1] = int( ((float) avg_color_float1[1] / (17.0)) +0.5 + eps);
		enc_color1[2] = int( ((float) avg_color_float1[2] / (17.0)) +0.5 + eps);
		enc_color2[0] = int( ((float) avg_color_float2[0] / (17.0)) +0.5 + eps);
		enc_color2[1] = int( ((float) avg_color_float2[1] / (17.0)) +0.5 + eps);
		enc_color2[2] = int( ((float) avg_color_float2[2] / (17.0)) +0.5 + eps);

		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];

		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------


		// Pack bits into the first word. 

		compressed1_flip = 0;
		PUTBITSHIGH( compressed1_flip, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_flip, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_flip, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_flip, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_flip, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_flip, enc_color2[2], 4, 43);

		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		// upper part of block
		flip_err = tryalltables_3bittable4x2(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
		// lower part of block
		flip_err += tryalltables_3bittable4x2(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_flip, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip,           1,   1, 32);

		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);


	}

	// Now lets see which is the best table to use. Only 8 tables are possible. 

	if(norm_err <= flip_err)
	{

		compressed1 = compressed1_norm | 0;
		compressed2 = compressed2_norm;

	}
	else
	{

		compressed1 = compressed1_flip | 1;
		compressed2 = compressed2_flip;
	}
}

void compressBlockDiffFlipCombined(uint8 *img,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2)
{
	unsigned int compressed1_norm, compressed2_norm;
	unsigned int compressed1_flip, compressed2_flip;
	uint8 avg_color_quant1[3], avg_color_quant2[3];

	float avg_color_float1[3],avg_color_float2[3];
	int enc_color1[3], enc_color2[3], diff[3];
	unsigned int best_table1=0, best_table2=0;
    int diffbit;

	int norm_err=0;
	int flip_err=0;

	// First try normal blocks 2x4:

	computeAverageColor2x4noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor2x4noQuantFloat(img,width,height,startx+2,starty,avg_color_float2);

	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 

	float eps;

	uint8 dummy[3];

	quantize555ColorCombined(avg_color_float1, enc_color1, dummy);
	quantize555ColorCombined(avg_color_float2, enc_color2, dummy);

	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
	{
		diffbit = 1;

		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		// Pack bits into the first word. 

		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_norm = 0;
		PUTBITSHIGH( compressed1_norm, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_norm, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_norm, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_norm, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_norm, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_norm, diff[2],       3, 42);

		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		norm_err = 0;

		// left part of block
		norm_err = tryalltables_3bittable2x4(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		norm_err += tryalltables_3bittable2x4(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_norm, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_norm,           0,   1, 32);

		compressed2_norm = 0;
		PUTBITS( compressed2_norm, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm, (best_pixel_indices2_LSB     ), 8, 15);

	}
	else
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.

		eps = (float) 0.0001;

		uint8 dummy[3];
		quantize444ColorCombined(avg_color_float1, enc_color1, dummy);
		quantize444ColorCombined(avg_color_float2, enc_color2, dummy);

		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];
	

		// Pack bits into the first word. 

		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------

		compressed1_norm = 0;
		PUTBITSHIGH( compressed1_norm, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_norm, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_norm, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_norm, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_norm, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_norm, enc_color2[2], 4, 43);

		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		
		// left part of block
		norm_err = tryalltables_3bittable2x4(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		norm_err += tryalltables_3bittable2x4(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_norm, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_norm,           0,   1, 32);

		compressed2_norm = 0;
		PUTBITS( compressed2_norm, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm, (best_pixel_indices2_LSB     ), 8, 15);

	
	}

	// Now try flipped blocks 4x2:

	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty+2,avg_color_float2);

	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 

	quantize555ColorCombined(avg_color_float1, enc_color1, dummy);
	quantize555ColorCombined(avg_color_float2, enc_color2, dummy);

	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
	{
		diffbit = 1;

		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		// Pack bits into the first word. 

		compressed1_flip = 0;
		PUTBITSHIGH( compressed1_flip, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_flip, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_flip, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_flip, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_flip, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_flip, diff[2],       3, 42);



		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		// upper part of block
		flip_err = tryalltables_3bittable4x2(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
		// lower part of block
		flip_err += tryalltables_3bittable4x2(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_flip, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip,           1,   1, 32);

		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);

	
	}
	else
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.
		eps = (float) 0.0001;

		uint8 dummy[3];
		quantize444ColorCombined(avg_color_float1, enc_color1, dummy);
		quantize444ColorCombined(avg_color_float2, enc_color2, dummy);

		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];

		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------


		// Pack bits into the first word. 

		compressed1_flip = 0;
		PUTBITSHIGH( compressed1_flip, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_flip, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_flip, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_flip, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_flip, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_flip, enc_color2[2], 4, 43);

		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		// upper part of block
		flip_err = tryalltables_3bittable4x2(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
		// lower part of block
		flip_err += tryalltables_3bittable4x2(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_flip, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip,           1,   1, 32);

		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);


	}

	// Now lets see which is the best table to use. Only 8 tables are possible. 

	if(norm_err <= flip_err)
	{

		compressed1 = compressed1_norm | 0;
		compressed2 = compressed2_norm;

	}
	else
	{

		compressed1 = compressed1_flip | 1;
		compressed2 = compressed2_flip;
	}
}

void compressBlockDiffFlipAveragePerceptual(uint8 *img,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2)
{

	unsigned int compressed1_norm, compressed2_norm;
	unsigned int compressed1_flip, compressed2_flip;
	uint8 avg_color_quant1[3], avg_color_quant2[3];

	float avg_color_float1[3],avg_color_float2[3];
	int enc_color1[3], enc_color2[3], diff[3];
	unsigned int best_table1=0, best_table2=0;
    int diffbit;

	int norm_err=0;
	int flip_err=0;

	// First try normal blocks 2x4:

	computeAverageColor2x4noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor2x4noQuantFloat(img,width,height,startx+2,starty,avg_color_float2);

	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 


	float eps;

	enc_color1[0] = int( JAS_ROUND(31.0*avg_color_float1[0]/255.0) );
	enc_color1[1] = int( JAS_ROUND(31.0*avg_color_float1[1]/255.0) );
	enc_color1[2] = int( JAS_ROUND(31.0*avg_color_float1[2]/255.0) );
	enc_color2[0] = int( JAS_ROUND(31.0*avg_color_float2[0]/255.0) );
	enc_color2[1] = int( JAS_ROUND(31.0*avg_color_float2[1]/255.0) );
	enc_color2[2] = int( JAS_ROUND(31.0*avg_color_float2[2]/255.0) );

	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
	{
		diffbit = 1;

		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		// Pack bits into the first word. 

		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_norm = 0;
		PUTBITSHIGH( compressed1_norm, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_norm, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_norm, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_norm, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_norm, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_norm, diff[2],       3, 42);

		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		norm_err = 0;

		// left part of block 
		norm_err = tryalltables_3bittable2x4percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		norm_err += tryalltables_3bittable2x4percep(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_norm, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_norm,           0,   1, 32);

		compressed2_norm = 0;
		PUTBITS( compressed2_norm, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm, (best_pixel_indices2_LSB     ), 8, 15);

	}
	else
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.

		eps = (float) 0.0001;

		enc_color1[0] = int( ((float) avg_color_float1[0] / (17.0)) +0.5 + eps);
		enc_color1[1] = int( ((float) avg_color_float1[1] / (17.0)) +0.5 + eps);
		enc_color1[2] = int( ((float) avg_color_float1[2] / (17.0)) +0.5 + eps);
		enc_color2[0] = int( ((float) avg_color_float2[0] / (17.0)) +0.5 + eps);
		enc_color2[1] = int( ((float) avg_color_float2[1] / (17.0)) +0.5 + eps);
		enc_color2[2] = int( ((float) avg_color_float2[2] / (17.0)) +0.5 + eps);
		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];
	

		// Pack bits into the first word. 

		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------

		compressed1_norm = 0;
		PUTBITSHIGH( compressed1_norm, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_norm, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_norm, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_norm, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_norm, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_norm, enc_color2[2], 4, 43);

		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		
		// left part of block
		norm_err = tryalltables_3bittable2x4percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		norm_err += tryalltables_3bittable2x4percep(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_norm, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_norm,           0,   1, 32);

		compressed2_norm = 0;
		PUTBITS( compressed2_norm, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm, (best_pixel_indices2_LSB     ), 8, 15);

	
	}

	// Now try flipped blocks 4x2:

	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty+2,avg_color_float2);

	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 

	enc_color1[0] = int( JAS_ROUND(31.0*avg_color_float1[0]/255.0) );
	enc_color1[1] = int( JAS_ROUND(31.0*avg_color_float1[1]/255.0) );
	enc_color1[2] = int( JAS_ROUND(31.0*avg_color_float1[2]/255.0) );
	enc_color2[0] = int( JAS_ROUND(31.0*avg_color_float2[0]/255.0) );
	enc_color2[1] = int( JAS_ROUND(31.0*avg_color_float2[1]/255.0) );
	enc_color2[2] = int( JAS_ROUND(31.0*avg_color_float2[2]/255.0) );

	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
	{
		diffbit = 1;

		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		// Pack bits into the first word. 

		compressed1_flip = 0;
		PUTBITSHIGH( compressed1_flip, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_flip, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_flip, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_flip, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_flip, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_flip, diff[2],       3, 42);



		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		// upper part of block
		flip_err = tryalltables_3bittable4x2percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
		// lower part of block
		flip_err += tryalltables_3bittable4x2percep(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_flip, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip,           1,   1, 32);

		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);

	
	}
	else
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.
		eps = (float) 0.0001;

		enc_color1[0] = int( ((float) avg_color_float1[0] / (17.0)) +0.5 + eps);
		enc_color1[1] = int( ((float) avg_color_float1[1] / (17.0)) +0.5 + eps);
		enc_color1[2] = int( ((float) avg_color_float1[2] / (17.0)) +0.5 + eps);
		enc_color2[0] = int( ((float) avg_color_float2[0] / (17.0)) +0.5 + eps);
		enc_color2[1] = int( ((float) avg_color_float2[1] / (17.0)) +0.5 + eps);
		enc_color2[2] = int( ((float) avg_color_float2[2] / (17.0)) +0.5 + eps);

		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];

		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------


		// Pack bits into the first word. 

		compressed1_flip = 0;
		PUTBITSHIGH( compressed1_flip, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_flip, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_flip, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_flip, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_flip, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_flip, enc_color2[2], 4, 43);

		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		// upper part of block
		flip_err = tryalltables_3bittable4x2percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
		// lower part of block
		flip_err += tryalltables_3bittable4x2percep(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_flip, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip,           1,   1, 32);

		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);


	}

	// Now lets see which is the best table to use. Only 8 tables are possible. 

	if(norm_err <= flip_err)
	{

		compressed1 = compressed1_norm | 0;
		compressed2 = compressed2_norm;

	}
	else
	{

		compressed1 = compressed1_flip | 1;
		compressed2 = compressed2_flip;
	}
}

void compressBlockDiffFlipCombinedPerceptual(uint8 *img,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2)
{

	unsigned int compressed1_norm, compressed2_norm;
	unsigned int compressed1_flip, compressed2_flip;
	uint8 avg_color_quant1[3], avg_color_quant2[3];

	float avg_color_float1[3],avg_color_float2[3];
	int enc_color1[3], enc_color2[3], diff[3];
	unsigned int best_table1=0, best_table2=0;
    int diffbit;

	int norm_err=0;
	int flip_err=0;

	// First try normal blocks 2x4:

	computeAverageColor2x4noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor2x4noQuantFloat(img,width,height,startx+2,starty,avg_color_float2);

	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 


	float eps;

	uint8 dummy[3];

	quantize555ColorCombinedPerceptual(avg_color_float1, enc_color1, dummy);
	quantize555ColorCombinedPerceptual(avg_color_float2, enc_color2, dummy);

	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
	{
		diffbit = 1;

		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		// Pack bits into the first word. 

		//     ETC1_RGB8_OES:
		// 
		//     a) bit layout in bits 63 through 32 if diffbit = 0
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		//     
		//     b) bit layout in bits 63 through 32 if diffbit = 1
		// 
		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
		//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------
		// 
		//     c) bit layout in bits 31 through 0 (in both cases)
		// 
		//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
		//      --------------------------------------------------------------------------------------------------
		//     |       most significant pixel index bits       |         least significant pixel index bits       |  
		//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
		//      --------------------------------------------------------------------------------------------------      


		compressed1_norm = 0;
		PUTBITSHIGH( compressed1_norm, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_norm, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_norm, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_norm, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_norm, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_norm, diff[2],       3, 42);

		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		norm_err = 0;

		// left part of block
		norm_err = tryalltables_3bittable2x4percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		norm_err += tryalltables_3bittable2x4percep(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_norm, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_norm,           0,   1, 32);

		compressed2_norm = 0;
		PUTBITS( compressed2_norm, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm, (best_pixel_indices2_LSB     ), 8, 15);

	}
	else
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.

		eps = (float) 0.0001;

		quantize444ColorCombinedPerceptual(avg_color_float1, enc_color1, dummy);
		quantize444ColorCombinedPerceptual(avg_color_float2, enc_color2, dummy);

		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];
	

		// Pack bits into the first word. 

		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------

		compressed1_norm = 0;
		PUTBITSHIGH( compressed1_norm, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_norm, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_norm, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_norm, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_norm, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_norm, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_norm, enc_color2[2], 4, 43);

		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		
		// left part of block
		norm_err = tryalltables_3bittable2x4percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);

		// right part of block
		norm_err += tryalltables_3bittable2x4percep(img,width,height,startx+2,starty,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_norm, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_norm, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_norm,           0,   1, 32);

		compressed2_norm = 0;
		PUTBITS( compressed2_norm, (best_pixel_indices1_MSB     ), 8, 23);
		PUTBITS( compressed2_norm, (best_pixel_indices2_MSB     ), 8, 31);
		PUTBITS( compressed2_norm, (best_pixel_indices1_LSB     ), 8, 7);
		PUTBITS( compressed2_norm, (best_pixel_indices2_LSB     ), 8, 15);

	
	}

	// Now try flipped blocks 4x2:

	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty,avg_color_float1);
	computeAverageColor4x2noQuantFloat(img,width,height,startx,starty+2,avg_color_float2);

	// First test if avg_color1 is similar enough to avg_color2 so that
	// we can use differential coding of colors. 


	quantize555ColorCombinedPerceptual(avg_color_float1, enc_color1, dummy);
	quantize555ColorCombinedPerceptual(avg_color_float2, enc_color2, dummy);

	diff[0] = enc_color2[0]-enc_color1[0];	
	diff[1] = enc_color2[1]-enc_color1[1];	
	diff[2] = enc_color2[2]-enc_color1[2];

    if( (diff[0] >= -4) && (diff[0] <= 3) && (diff[1] >= -4) && (diff[1] <= 3) && (diff[2] >= -4) && (diff[2] <= 3) )
	{
		diffbit = 1;

		// The difference to be coded:

		diff[0] = enc_color2[0]-enc_color1[0];	
		diff[1] = enc_color2[1]-enc_color1[1];	
		diff[2] = enc_color2[2]-enc_color1[2];

		avg_color_quant1[0] = enc_color1[0] << 3 | (enc_color1[0] >> 2);
		avg_color_quant1[1] = enc_color1[1] << 3 | (enc_color1[1] >> 2);
		avg_color_quant1[2] = enc_color1[2] << 3 | (enc_color1[2] >> 2);
		avg_color_quant2[0] = enc_color2[0] << 3 | (enc_color2[0] >> 2);
		avg_color_quant2[1] = enc_color2[1] << 3 | (enc_color2[1] >> 2);
		avg_color_quant2[2] = enc_color2[2] << 3 | (enc_color2[2] >> 2);

		// Pack bits into the first word. 

		compressed1_flip = 0;
		PUTBITSHIGH( compressed1_flip, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip, enc_color1[0], 5, 63);
 		PUTBITSHIGH( compressed1_flip, enc_color1[1], 5, 55);
 		PUTBITSHIGH( compressed1_flip, enc_color1[2], 5, 47);
 		PUTBITSHIGH( compressed1_flip, diff[0],       3, 58);
 		PUTBITSHIGH( compressed1_flip, diff[1],       3, 50);
 		PUTBITSHIGH( compressed1_flip, diff[2],       3, 42);



		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		// upper part of block
		flip_err = tryalltables_3bittable4x2percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
		// lower part of block
		flip_err += tryalltables_3bittable4x2percep(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_flip, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip,           1,   1, 32);

		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);

	
	}
	else
	{
		diffbit = 0;
		// The difference is bigger than what fits in 555 plus delta-333, so we will have
		// to deal with 444 444.
		eps = (float) 0.0001;

		quantize444ColorCombinedPerceptual(avg_color_float1, enc_color1, dummy);
		quantize444ColorCombinedPerceptual(avg_color_float2, enc_color2, dummy);

		avg_color_quant1[0] = enc_color1[0] << 4 | enc_color1[0]; 
		avg_color_quant1[1] = enc_color1[1] << 4 | enc_color1[1]; 
		avg_color_quant1[2] = enc_color1[2] << 4 | enc_color1[2];
		avg_color_quant2[0] = enc_color2[0] << 4 | enc_color2[0]; 
		avg_color_quant2[1] = enc_color2[1] << 4 | enc_color2[1]; 
		avg_color_quant2[2] = enc_color2[2] << 4 | enc_color2[2];

		//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
		//      ---------------------------------------------------------------------------------------------------
		//     | base col1 | base col2 | base col1 | base col2 | base col1 | base col2 | table  | table  |diff|flip|
		//     | R1 (4bits)| R2 (4bits)| G1 (4bits)| G2 (4bits)| B1 (4bits)| B2 (4bits)| cw 1   | cw 2   |bit |bit |
		//      ---------------------------------------------------------------------------------------------------


		// Pack bits into the first word. 

		compressed1_flip = 0;
		PUTBITSHIGH( compressed1_flip, diffbit,       1, 33);
 		PUTBITSHIGH( compressed1_flip, enc_color1[0], 4, 63);
 		PUTBITSHIGH( compressed1_flip, enc_color1[1], 4, 55);
 		PUTBITSHIGH( compressed1_flip, enc_color1[2], 4, 47);
 		PUTBITSHIGH( compressed1_flip, enc_color2[0], 4, 59);
 		PUTBITSHIGH( compressed1_flip, enc_color2[1], 4, 51);
 		PUTBITSHIGH( compressed1_flip, enc_color2[2], 4, 43);

		unsigned int best_pixel_indices1_MSB;
		unsigned int best_pixel_indices1_LSB;
		unsigned int best_pixel_indices2_MSB;
		unsigned int best_pixel_indices2_LSB;

		// upper part of block
		flip_err = tryalltables_3bittable4x2percep(img,width,height,startx,starty,avg_color_quant1,best_table1,best_pixel_indices1_MSB, best_pixel_indices1_LSB);
		// lower part of block
		flip_err += tryalltables_3bittable4x2percep(img,width,height,startx,starty+2,avg_color_quant2,best_table2,best_pixel_indices2_MSB, best_pixel_indices2_LSB);

 		PUTBITSHIGH( compressed1_flip, best_table1,   3, 39);
 		PUTBITSHIGH( compressed1_flip, best_table2,   3, 36);
 		PUTBITSHIGH( compressed1_flip,           1,   1, 32);

		best_pixel_indices1_MSB |= (best_pixel_indices2_MSB << 2);
		best_pixel_indices1_LSB |= (best_pixel_indices2_LSB << 2);
		
		compressed2_flip = ((best_pixel_indices1_MSB & 0xffff) << 16) | (best_pixel_indices1_LSB & 0xffff);


	}

	// Now lets see which is the best table to use. Only 8 tables are possible. 

	if(norm_err <= flip_err)
	{

		compressed1 = compressed1_norm | 0;
		compressed2 = compressed2_norm;

	}
	else
	{

		compressed1 = compressed1_flip | 1;
		compressed2 = compressed2_flip;
	}
}

double calcBlockErrorRGB(uint8 *img, uint8 *imgdec, int width, int height, int startx, int starty)
{
	int xx,yy;
	double err;

	err = 0;

	for(xx = startx; xx< startx+4; xx++)
	{
		for(yy = starty; yy<starty+4; yy++)
		{
 			err += SQUARE(1.0*RED(img,width,xx,yy)  - 1.0*RED(imgdec, width, xx,yy));
 			err += SQUARE(1.0*GREEN(img,width,xx,yy)- 1.0*GREEN(imgdec, width, xx,yy));
 			err += SQUARE(1.0*BLUE(img,width,xx,yy) - 1.0*BLUE(imgdec, width, xx,yy));
		}
	}

	return err;
}

double calcBlockPerceptualErrorRGB(uint8 *img, uint8 *imgdec, int width, int height, int startx, int starty)
{
	int xx,yy;
	double err;

	err = 0;

	for(xx = startx; xx< startx+4; xx++)
	{
		for(yy = starty; yy<starty+4; yy++)
		{
 			err += PERCEPTUAL_WEIGHT_R_SQUARED*SQUARE(1.0*RED(img,width,xx,yy)  - 1.0*RED(imgdec, width, xx,yy));
 			err += PERCEPTUAL_WEIGHT_G_SQUARED*SQUARE(1.0*GREEN(img,width,xx,yy)- 1.0*GREEN(imgdec, width, xx,yy));
 			err += PERCEPTUAL_WEIGHT_B_SQUARED*SQUARE(1.0*BLUE(img,width,xx,yy) - 1.0*BLUE(imgdec, width, xx,yy));
		}
	}

	return err;
}

void compressBlockDiffFlipFast(uint8 *img, uint8 *imgdec,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2)
{
	unsigned int average_block1;
	unsigned int average_block2;
	double error_average;

	unsigned int combined_block1;
	unsigned int combined_block2;
	double error_combined;

	// First quantize the average color to the nearest neighbor.
	compressBlockDiffFlipAverage(img, width, height, startx, starty, average_block1, average_block2);
	decompressBlockDiffFlip(average_block1, average_block2, imgdec, width, height, startx, starty);
	error_average = calcBlockErrorRGB(img, imgdec, width, height, startx, starty);

	compressBlockDiffFlipCombined(img, width, height, startx, starty, combined_block1, combined_block2);
	decompressBlockDiffFlip(combined_block1, combined_block2, imgdec, width, height, startx, starty);
	error_combined = calcBlockErrorRGB(img, imgdec, width, height, startx, starty);

	if(error_combined < error_average)
	{
		compressed1 = combined_block1;
		compressed2 = combined_block2;
	}
	else
	{
		compressed1 = average_block1;
		compressed2 = average_block2;
	}
}

void compressBlockDiffFlipFastPerceptual(uint8 *img, uint8 *imgdec,int width,int height,int startx,int starty, unsigned int &compressed1, unsigned int &compressed2)
{
	unsigned int average_block1;
	unsigned int average_block2;
	double error_average;

	unsigned int combined_block1;
	unsigned int combined_block2;
	double error_combined;

	// First quantize the average color to the nearest neighbor.
	compressBlockDiffFlipAveragePerceptual(img, width, height, startx, starty, average_block1, average_block2);
	decompressBlockDiffFlip(average_block1, average_block2, imgdec, width, height, startx, starty);
	error_average = calcBlockPerceptualErrorRGB(img, imgdec, width, height, startx, starty);

	compressBlockDiffFlipCombinedPerceptual(img, width, height, startx, starty, combined_block1, combined_block2);
	decompressBlockDiffFlip(combined_block1, combined_block2, imgdec, width, height, startx, starty);
	error_combined = calcBlockPerceptualErrorRGB(img, imgdec, width, height, startx, starty);

	if(error_combined < error_average)
	{
		compressed1 = combined_block1;
		compressed2 = combined_block2;
	}
	else
	{
		compressed1 = average_block1;
		compressed2 = average_block2;
	}
}

static int bswap(unsigned int x) {
  return ((x & 0xFF000000) >> 24) |
         ((x & 0x00FF0000) >> 8) |
         ((x & 0x0000FF00) << 8) |
         ((x & 0x000000FF) << 24);
}

void CompressBlock(const uint8 *in, int in_width, uint8 *output, int quality) {
  uint8 rgb[4 * 4 * 3];
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      rgb[(y * 4 + x) * 3 + 0] = in[(y * in_width + x) * 4 + 0];
      rgb[(y * 4 + x) * 3 + 1] = in[(y * in_width + x) * 4 + 1];
      rgb[(y * 4 + x) * 3 + 2] = in[(y * in_width + x) * 4 + 2];
    }
  }

  uint8 imgdec[4 * 4 * 3]={0};  // temporary storage used by some of the functions.
  unsigned int compressed1 = 0, compressed2 = 0;
  switch (quality) {
  case 0:
    compressBlockDiffFlipFast(rgb, imgdec, 4, 4, 0, 0, compressed1, compressed2);
    break;
  case 1:
    compressBlockDiffFlipFastPerceptual(rgb, imgdec, 4, 4, 0, 0, compressed1, compressed2);
    break;
  case 2:
    compressBlockDiffFlipMedium(rgb, 4, 4, 0, 0, compressed1, compressed2);
    break;
  case 3:
    compressBlockDiffFlipMediumPerceptual(rgb, 4, 4, 0, 0, compressed1, compressed2);
    break;
  case 4:
    compressBlockDiffFlipSlow(rgb, 4, 4, 0, 0, compressed1, compressed2);
    break;
  case 5:
    compressBlockDiffFlipSlowPerceptual(rgb, 4, 4, 0, 0, compressed1, compressed2);
    break;
  default:
    fprintf(stderr, "ETC1: Compression level %i not defined", quality);
    return;
  }
  compressed1 = bswap(compressed1);
  compressed2 = bswap(compressed2);
  memcpy(output, &compressed1, 4);
  memcpy(output + 4, &compressed2, 4);
  memset(rgb, 0, 4 * 4 * 3);
}
