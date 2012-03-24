/**
 
@~English
@page licensing Licensing
 
@section etcdec etcdec.cxx License
 
etcdec.cxx is made available under the terms and conditions of the following
License Agreement.

SOFTWARE LICENSE AGREEMENT

PLEASE REVIEW THE FOLLOWING TERMS AND CONDITIONS PRIOR TO USING THE
ERICSSON TEXTURE COMPRESSION CODEC SOFTWARE (THE "SOFTWARE"). THE USE
OF THE SOFTWARE IS SUBJECT TO THE TERMS AND CONDITIONS OF THE
FOLLOWING LICENSE AGREEMENT (THE "LICENSE AGREEMENT"). IF YOU DO NOT
ACCEPT SUCH TERMS AND CONDITIONS YOU MAY NOT USE THE SOFTWARE.

Under the terms and conditions of the License Agreement, Licensee
hereby, receives a non-exclusive, non transferable, limited, free of
charge, perpetual and worldwide license, to copy, use, distribute and
modify the Software, but only for the purpose of developing,
manufacturing, selling, using and distributing products including the
Software, which products are used for (i) compression and/or
decompression to create content creation tools for usage with a
Khronos API, and/or (ii) compression and/or decompression for the
purpose of usage with a middleware API that is built on top of a
Khronos API, such as JCPs based on a Khronos API (in particular
"Mobile 3D Graphics API for J2ME" and its future versions and "Java
Bindings for OpenGL ES" and its future versions), and/or (iii)
compression and/or decompression to implement a Khronos specification.

If Licensee institutes patent litigation against Ericsson or any
licensee of the Software for using the Software for making,
developing, manufacturing, selling, using and/or distributing products
within the scope of the Khronos framework, Ericsson shall have the
right to terminate this License Agreement with immediate
effect. However, should Licensee institute patent litigation against
any other licensee of the Software based on such licensee´s use of any
other software distributed together with the Software then Ericsson
shall have no right to terminate this License Agreement.

The License Agreement does not transfer to Licensee any ownership to
any Ericsson or third party intellectual property rights.

THE SOFTWARE IS PROVIDED "AS IS". ERICSSON MAKES NO REPRESENTATIONS OF
ANY KIND, EXTENDS NO WARRANTIES OF ANY KIND, EITHER EXPRESS OR
IMPLIED; INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE ENTIRE RISK
AS TO THE QUALITY AND PERFORMANCE OF THE SOFTWARE IS WITH THE
LICENSEE. SHOULD THE SOFTWARE PROVE DEFECTIVE, THE LICENSEE ASSUMES
THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION. ERICSSON
MAKES NO WARRANTY THAT THE MANUFACTURE, SALE, DISTRIBUTION, LEASE, USE
OR IMPORTATION UNDER THE LICENSE AGREEMENT WILL BE FREE FROM
INFRINGEMENT OF PATENTS, COPYRIGHTS OR OTHER INTELLECTUAL PROPERTY
RIGHTS OF OTHERS, AND THE VALIDITY OF THE LICENSE AND THE LICENSE
AGREEMENT IS SUBJECT TO LICENSEE'S SOLE RESPONSIBILITY TO MAKE SUCH
DETERMINATION AND ACQUIRE SUCH LICENSES AS MAY BE NECESSARY WITH
RESPECT TO PATENTS AND OTHER INTELLECTUAL PROPERTY OF THIRD PARTIES.

IN NO EVENT WILL ERICSSON BE LIABLE TO THE LICENSEE FOR DAMAGES,
INCLUDING ANY GENERAL, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES
ARISING OUT OF THE USE OR INABILITY TO USE THE SOFTWARE (INCLUDING BUT
NOT LIMITED TO LOSS OF DATA OR DATA BEING RENDERED INACCURATE OR
LOSSES SUSTAINED BY THE LICENSEE OR THIRD PARTIES OR A FAILURE OF THE
SOFTWARE TO OPERATE WITH ANY OTHER SOFTWARE), EVEN IF SUCH HOLDER OR
OTHER PARTY HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.

Licensee acknowledges that "ERICSSON ///" is the corporate trademark
of Telefonaktiebolaget LM Ericsson and that both "Ericsson" and the
figure "///" are important features of the trade names of
Telefonaktiebolaget LM Ericsson. Nothing contained in these terms and
conditions shall be deemed to grant Licensee any right, title or
interest in the word "Ericsson" or the figure "///".

The parties agree that this License Agreement based on these terms and
conditions is governed by the laws of Sweden, and in the event of any
dispute as a result of this License Agreement, the parties submit
themselves to the exclusive jurisdiction of the Swedish Courts.
*/

#include <stdio.h>
#include <string.h>

#define CLAMP(ll,x,ul) (((x)<(ll)) ? (ll) : (((x)>(ul)) ? (ul) : (x)))
#define GETBITS(source, size, startpos)  (( (source) >> ((startpos)-(size)+1) ) & ((1<<(size)) -1))
#define GETBITSHIGH(source, size, startpos)  (( (source) >> (((startpos)-32)-(size)+1) ) & ((1<<(size)) -1))
#define RED(img,width,x,y)   img[3*(y*width+x)+0]
#define GREEN(img,width,x,y) img[3*(y*width+x)+1]
#define BLUE(img,width,x,y)  img[3*(y*width+x)+2]

typedef unsigned char uint8;

int unscramble[4] = {2, 3, 1, 0};

static const int compressParams[16][4] = {{-8, -2,  2, 8}, {-8, -2,  2, 8}, {-17, -5, 5, 17}, {-17, -5, 5, 17}, {-29, -9, 9, 29}, {-29, -9, 9, 29}, {-42, -13, 13, 42}, {-42, -13, 13, 42}, {-60, -18, 18, 60}, {-60, -18, 18, 60}, {-80, -24, 24, 80}, {-80, -24, 24, 80}, {-106, -33, 33, 106}, {-106, -33, 33, 106}, {-183, -47, 47, 183}, {-183, -47, 47, 183}};

void decompressBlockDiffFlip(unsigned int block_part1, unsigned int block_part2, uint8 *img,int width,int height,int startx,int starty)
{
	uint8 avg_color[3], enc_color1[3], enc_color2[3];
    char diff[3];
	int table;
	int index,shift;
	int r,g,b;
	int diffbit;
	int flipbit;
	unsigned int pixel_indices_MSB, pixel_indices_LSB;
	int x,y;

	diffbit = (GETBITSHIGH(block_part1, 1, 33));
	flipbit = (GETBITSHIGH(block_part1, 1, 32));

    if( !diffbit )
	{

		// We have diffbit = 0.

		// First decode left part of block.
		avg_color[0]= GETBITSHIGH(block_part1, 4, 63);
		avg_color[1]= GETBITSHIGH(block_part1, 4, 55);
		avg_color[2]= GETBITSHIGH(block_part1, 4, 47);

		// Here, we should really multiply by 17 instead of 16. This can
		// be done by just copying the four lower bits to the upper ones
		// while keeping the lower bits.
		avg_color[0] |= (avg_color[0] <<4);
		avg_color[1] |= (avg_color[1] <<4);
		avg_color[2] |= (avg_color[2] <<4);

		table = GETBITSHIGH(block_part1, 3, 39) << 1;

		
			
		pixel_indices_MSB = GETBITS(block_part2, 16, 31);
		pixel_indices_LSB = GETBITS(block_part2, 16, 15);

		if( (flipbit) == 0 )
		{
			// We should not flip
			shift = 0;
			for(x=startx; x<startx+2; x++)
			{
				for(y=starty; y<starty+4; y++)
				{
					index  = ((pixel_indices_MSB >> shift) & 1) << 1;
					index |= ((pixel_indices_LSB >> shift) & 1);
					shift++;
					index=unscramble[index];

 					r=RED(img,width,x,y)  =CLAMP(0,avg_color[0]+compressParams[table][index],255);
 					g=GREEN(img,width,x,y)=CLAMP(0,avg_color[1]+compressParams[table][index],255);
 					b=BLUE(img,width,x,y) =CLAMP(0,avg_color[2]+compressParams[table][index],255);


				}
			}
		}
		else
		{
			// We should flip
			shift = 0;
			for(x=startx; x<startx+4; x++)
			{
				for(y=starty; y<starty+2; y++)
				{
					index  = ((pixel_indices_MSB >> shift) & 1) << 1;
					index |= ((pixel_indices_LSB >> shift) & 1);
					shift++;
					index=unscramble[index];

 					r=RED(img,width,x,y)  =CLAMP(0,avg_color[0]+compressParams[table][index],255);
 					g=GREEN(img,width,x,y)=CLAMP(0,avg_color[1]+compressParams[table][index],255);
 					b=BLUE(img,width,x,y) =CLAMP(0,avg_color[2]+compressParams[table][index],255);
				}
				shift+=2;
			}
		}

		// Now decode other part of block. 
		avg_color[0]= GETBITSHIGH(block_part1, 4, 59);
		avg_color[1]= GETBITSHIGH(block_part1, 4, 51);
		avg_color[2]= GETBITSHIGH(block_part1, 4, 43);

		// Here, we should really multiply by 17 instead of 16. This can
		// be done by just copying the four lower bits to the upper ones
		// while keeping the lower bits.
		avg_color[0] |= (avg_color[0] <<4);
		avg_color[1] |= (avg_color[1] <<4);
		avg_color[2] |= (avg_color[2] <<4);

		table = GETBITSHIGH(block_part1, 3, 36) << 1;
		pixel_indices_MSB = GETBITS(block_part2, 16, 31);
		pixel_indices_LSB = GETBITS(block_part2, 16, 15);

		if( (flipbit) == 0 )
		{
			// We should not flip
			shift=8;
			for(x=startx+2; x<startx+4; x++)
			{
				for(y=starty; y<starty+4; y++)
				{
					index  = ((pixel_indices_MSB >> shift) & 1) << 1;
					index |= ((pixel_indices_LSB >> shift) & 1);
					shift++;
					index=unscramble[index];

 					r=RED(img,width,x,y)  =CLAMP(0,avg_color[0]+compressParams[table][index],255);
 					g=GREEN(img,width,x,y)=CLAMP(0,avg_color[1]+compressParams[table][index],255);
 					b=BLUE(img,width,x,y) =CLAMP(0,avg_color[2]+compressParams[table][index],255);

				}
			}
		}
		else
		{
			// We should flip
			shift=2;
			for(x=startx; x<startx+4; x++)
			{
				for(y=starty+2; y<starty+4; y++)
				{
					index  = ((pixel_indices_MSB >> shift) & 1) << 1;
					index |= ((pixel_indices_LSB >> shift) & 1);
					shift++;
					index=unscramble[index];

 					r=RED(img,width,x,y)  =CLAMP(0,avg_color[0]+compressParams[table][index],255);
 					g=GREEN(img,width,x,y)=CLAMP(0,avg_color[1]+compressParams[table][index],255);
 					b=BLUE(img,width,x,y) =CLAMP(0,avg_color[2]+compressParams[table][index],255);

				}
				shift += 2;
			}
		}

	}
	else
	{
		// We have diffbit = 1. 

//      63 62 61 60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 36 35 34  33  32 
//      ---------------------------------------------------------------------------------------------------
//     | base col1    | dcol 2 | base col1    | dcol 2 | base col 1   | dcol 2 | table  | table  |diff|flip|
//     | R1' (5 bits) | dR2    | G1' (5 bits) | dG2    | B1' (5 bits) | dB2    | cw 1   | cw 2   |bit |bit |
//      ---------------------------------------------------------------------------------------------------
// 
// 
//     c) bit layout in bits 31 through 0 (in both cases)
// 
//      31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3   2   1  0
//      --------------------------------------------------------------------------------------------------
//     |       most significant pixel index bits       |         least significant pixel index bits       |  
//     | p| o| n| m| l| k| j| i| h| g| f| e| d| c| b| a| p| o| n| m| l| k| j| i| h| g| f| e| d| c | b | a |
//      --------------------------------------------------------------------------------------------------      


		// First decode left part of block.
		enc_color1[0]= GETBITSHIGH(block_part1, 5, 63);
		enc_color1[1]= GETBITSHIGH(block_part1, 5, 55);
		enc_color1[2]= GETBITSHIGH(block_part1, 5, 47);


		// Expand from 5 to 8 bits
		avg_color[0] = (enc_color1[0] <<3) | (enc_color1[0] >> 2);
		avg_color[1] = (enc_color1[1] <<3) | (enc_color1[1] >> 2);
		avg_color[2] = (enc_color1[2] <<3) | (enc_color1[2] >> 2);


		table = GETBITSHIGH(block_part1, 3, 39) << 1;
			
		pixel_indices_MSB = GETBITS(block_part2, 16, 31);
		pixel_indices_LSB = GETBITS(block_part2, 16, 15);

		if( (flipbit) == 0 )
		{
			// We should not flip
			shift = 0;
			for(x=startx; x<startx+2; x++)
			{
				for(y=starty; y<starty+4; y++)
				{
					index  = ((pixel_indices_MSB >> shift) & 1) << 1;
					index |= ((pixel_indices_LSB >> shift) & 1);
					shift++;
					index=unscramble[index];

 					r=RED(img,width,x,y)  =CLAMP(0,avg_color[0]+compressParams[table][index],255);
 					g=GREEN(img,width,x,y)=CLAMP(0,avg_color[1]+compressParams[table][index],255);
 					b=BLUE(img,width,x,y) =CLAMP(0,avg_color[2]+compressParams[table][index],255);


				}
			}
		}
		else
		{
			// We should flip
			shift = 0;
			for(x=startx; x<startx+4; x++)
			{
				for(y=starty; y<starty+2; y++)
				{
					index  = ((pixel_indices_MSB >> shift) & 1) << 1;
					index |= ((pixel_indices_LSB >> shift) & 1);
					shift++;
					index=unscramble[index];

 					r=RED(img,width,x,y)  =CLAMP(0,avg_color[0]+compressParams[table][index],255);
 					g=GREEN(img,width,x,y)=CLAMP(0,avg_color[1]+compressParams[table][index],255);
 					b=BLUE(img,width,x,y) =CLAMP(0,avg_color[2]+compressParams[table][index],255);
				}
				shift+=2;
			}
		}


		// Now decode right part of block. 


		diff[0]= GETBITSHIGH(block_part1, 3, 58);
		diff[1]= GETBITSHIGH(block_part1, 3, 50);
	    diff[2]= GETBITSHIGH(block_part1, 3, 42);

		enc_color2[0]= enc_color1[0] + diff[0];
		enc_color2[1]= enc_color1[1] + diff[1];
		enc_color2[2]= enc_color1[2] + diff[2];

		// Extend sign bit to entire byte. 
		diff[0] = (diff[0] << 5);
		diff[1] = (diff[1] << 5);
		diff[2] = (diff[2] << 5);
		diff[0] = diff[0] >> 5;
		diff[1] = diff[1] >> 5;
		diff[2] = diff[2] >> 5;

		//  Calculale second color
		enc_color2[0]= enc_color1[0] + diff[0];
		enc_color2[1]= enc_color1[1] + diff[1];
		enc_color2[2]= enc_color1[2] + diff[2];

		// Expand from 5 to 8 bits
		avg_color[0] = (enc_color2[0] <<3) | (enc_color2[0] >> 2);
		avg_color[1] = (enc_color2[1] <<3) | (enc_color2[1] >> 2);
		avg_color[2] = (enc_color2[2] <<3) | (enc_color2[2] >> 2);


		table = GETBITSHIGH(block_part1, 3, 36) << 1;
		pixel_indices_MSB = GETBITS(block_part2, 16, 31);
		pixel_indices_LSB = GETBITS(block_part2, 16, 15);

		if( (flipbit) == 0 )
		{
			// We should not flip
			shift=8;
			for(x=startx+2; x<startx+4; x++)
			{
				for(y=starty; y<starty+4; y++)
				{
					index  = ((pixel_indices_MSB >> shift) & 1) << 1;
					index |= ((pixel_indices_LSB >> shift) & 1);
					shift++;
					index=unscramble[index];

 					r=RED(img,width,x,y)  =CLAMP(0,avg_color[0]+compressParams[table][index],255);
 					g=GREEN(img,width,x,y)=CLAMP(0,avg_color[1]+compressParams[table][index],255);
 					b=BLUE(img,width,x,y) =CLAMP(0,avg_color[2]+compressParams[table][index],255);

				}
			}
		}
		else
		{
			// We should flip
			shift=2;
			for(x=startx; x<startx+4; x++)
			{
				for(y=starty+2; y<starty+4; y++)
				{
					index  = ((pixel_indices_MSB >> shift) & 1) << 1;
					index |= ((pixel_indices_LSB >> shift) & 1);
					shift++;
					index=unscramble[index];

 					r=RED(img,width,x,y)  =CLAMP(0,avg_color[0]+compressParams[table][index],255);
 					g=GREEN(img,width,x,y)=CLAMP(0,avg_color[1]+compressParams[table][index],255);
 					b=BLUE(img,width,x,y) =CLAMP(0,avg_color[2]+compressParams[table][index],255);
				}
				shift += 2;
			}
		}
	}
}

static int bswap(unsigned int x) {
  return ((x & 0xFF000000) >> 24) |
         ((x & 0x00FF0000) >> 8) |
         ((x & 0x0000FF00) << 8) |
         ((x & 0x000000FF) << 24);
}

void DecompressBlock(const uint8 *compressed, uint8 *out, int out_width, int alpha=255) {
  uint8 rgb[4*4*3];
  unsigned int block_part1, block_part2;
  memcpy(&block_part1, compressed, 4);
  memcpy(&block_part2, compressed + 4, 4);
  block_part1 = bswap(block_part1);
  block_part2 = bswap(block_part2);
  decompressBlockDiffFlip(block_part1, block_part2, rgb, 4, 4, 0, 0);
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      out[(y * out_width + x) * 4 + 0] = rgb[(4 * y + x) * 3 + 0];
      out[(y * out_width + x) * 4 + 1] = rgb[(4 * y + x) * 3 + 1];
      out[(y * out_width + x) * 4 + 2] = rgb[(4 * y + x) * 3 + 2];
      out[(y * out_width + x) * 4 + 3] = alpha;
    }
  }
}
