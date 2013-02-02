// Contains a lot of code from etcpack.cpp.
// Extracted by Henrik Rydgård.


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "etcpack.h"
#include "etcdec.h"
#include "image.h"

enum{FIRST_PIXEL_IN_PPM_FILE_MAPS_TO_S0T0, FIRST_PIXEL_IN_PPM_FILE_MAPS_TO_S0T1};

int orientation;

int ktx_mode;

typedef struct KTX_header_t
{
	uint8  identifier[12];
	unsigned int endianness;
	unsigned int glType;
	unsigned int glTypeSize;
	unsigned int glFormat;
	unsigned int glInternalFormat;
	unsigned int glBaseInternalFormat;
	unsigned int pixelWidth;
	unsigned int pixelHeight;
	unsigned int pixelDepth;
	unsigned int numberOfArrayElements;
	unsigned int numberOfFaces;
	unsigned int numberOfMipmapLevels;
	unsigned int bytesOfKeyValueData;
} 
KTX_header;
#define KTX_IDENTIFIER_REF  { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A }

#define KTX_ENDIAN_REF      (0x04030201)
#define KTX_ENDIAN_REF_REV  (0x01020304)

int ktx_identifier[] = KTX_IDENTIFIER_REF;


enum {GL_R=0x1903,GL_RG=0x8227,GL_RGB=0x1907,GL_RGBA=0x1908};
enum {GL_ETC1_RGB8_OES=0x8d64};

#define ETC1_RGB_NO_MIPMAPS 0
#define ETC1_RGBA_NO_MIPMAPS 1
#define ETC1_RGB_MIPMAPS 2
#define ETC1_RGBA_MIPMAPS 3

// The error metric Wr Wg Wb should be definied so that Wr^2 + Wg^2 + Wb^2 = 1.
// Hence it is easier to first define the squared values and derive the weights
// as their square-roots.


// Alternative weights
//#define PERCEPTUAL_WEIGHT_R_SQUARED 0.3086
//#define PERCEPTUAL_WEIGHT_G_SQUARED 0.6094
//#define PERCEPTUAL_WEIGHT_B_SQUARED 0.082

#define PERCEPTUAL_WEIGHT_R (sqrt(PERCEPTUAL_WEIGHT_R_SQUARED))
#define PERCEPTUAL_WEIGHT_G (sqrt(PERCEPTUAL_WEIGHT_G_SQUARED))
#define PERCEPTUAL_WEIGHT_B (sqrt(PERCEPTUAL_WEIGHT_B_SQUARED))


double wR = PERCEPTUAL_WEIGHT_R;
double wG = PERCEPTUAL_WEIGHT_G;
double wB = PERCEPTUAL_WEIGHT_B;

double wR2 = PERCEPTUAL_WEIGHT_R_SQUARED;
double wG2 = PERCEPTUAL_WEIGHT_G_SQUARED;
double wB2 = PERCEPTUAL_WEIGHT_B_SQUARED;

void read_big_endian_2byte_word(unsigned short *blockadr, FILE *f)
{
	uint8 bytes[2];
	unsigned short block;

	fread(&bytes[0], 1, 1, f);
	fread(&bytes[1], 1, 1, f);

	block = 0;

	block |= bytes[0];
	block = block << 8;
	block |= bytes[1];

	blockadr[0] = block;
}

void read_big_endian_4byte_word(unsigned int *blockadr, FILE *f)
{
	uint8 bytes[4];
	unsigned int block;

	fread(&bytes[0], 1, 1, f);
	fread(&bytes[1], 1, 1, f);
	fread(&bytes[2], 1, 1, f);
	fread(&bytes[3], 1, 1, f);

	block = 0;

	block |= bytes[0];
	block = block << 8;
	block |= bytes[1];
	block = block << 8;
	block |= bytes[2];
	block = block << 8;
	block |= bytes[3];

	blockadr[0] = block;
}



bool fileExist(const char *filename)
{
	FILE *f=NULL;
	if((f=fopen(filename,"rb"))!=NULL)
	{
		fclose(f);
		return true;
	}
	return false;
}

bool expandToWidthDivByFour(uint8 *&img, int width, int height, int &expandedwidth, int &expandedheight)
{
	int wdiv4;
	int xx, yy;
	uint8 *newimg;

	wdiv4 = width /4;
	if( !(wdiv4 *4 == width) )
	{
     	expandedwidth = (wdiv4 + 1)*4;
		expandedheight = height;
	    newimg=(uint8*)malloc(3*expandedwidth*expandedheight);
		if(!newimg)
		{
			printf("Could not allocate memory to expand width\n");
			return false;
		}

		// First copy image
		for(yy = 0; yy<height; yy++)
		{
			for(xx = 0; xx < width; xx++)
			{
				newimg[(yy * expandedwidth)*3 + xx*3 + 0] = img[(yy * width)*3 + xx*3 + 0];
				newimg[(yy * expandedwidth)*3 + xx*3 + 1] = img[(yy * width)*3 + xx*3 + 1];
				newimg[(yy * expandedwidth)*3 + xx*3 + 2] = img[(yy * width)*3 + xx*3 + 2];
			}
		}

		// Then make the last column of pixels the same as the previous column.

		for(yy = 0; yy< height; yy++)
		{
			for(xx = width; xx < expandedwidth; xx++)
			{
				newimg[(yy * expandedwidth)*3 + (xx)*3 + 0] = img[(yy * width)*3 + (width-1)*3 + 0];
				newimg[(yy * expandedwidth)*3 + (xx)*3 + 1] = img[(yy * width)*3 + (width-1)*3 + 1];
				newimg[(yy * expandedwidth)*3 + (xx)*3 + 2] = img[(yy * width)*3 + (width-1)*3 + 2];
			}
		}

		// Now free the old image
		free(img);

		// Use the new image
		img = newimg;

		return true;
	}
	else
	{
		printf("Image already of even width\n");
		expandedwidth = width;
		expandedheight = height;
		return false;
	}
}


bool expandToHeightDivByFour(uint8 *&img, int width, int height, int &expandedwidth, int &expandedheight)
{
	int hdiv4;
	int xx, yy;
	int numlinesmissing;
	uint8 *newimg;


	hdiv4 = height/4;

	if( !(hdiv4 * 4 == height) )
	{
		expandedwidth = width;
		expandedheight = (hdiv4 + 1) * 4;
		numlinesmissing = expandedheight - height;
		newimg=(uint8*)malloc(3*expandedwidth*expandedheight);
		if(!newimg)
		{
			printf("Could not allocate memory to expand height\n");
			return false;
		}
		
		// First copy image. No need to reformat data.

		for(xx = 0; xx<3*width*height; xx++)
			newimg[xx] = img[xx];

		// Then copy up to three lines.

		for(yy = height; yy < height + numlinesmissing; yy++)
		{
			for(xx = 0; xx<width; xx++)
			{
				newimg[(yy*width)*3 + xx*3 + 0] = img[((height-1)*width)*3 + xx*3 + 0];
				newimg[(yy*width)*3 + xx*3 + 1] = img[((height-1)*width)*3 + xx*3 + 1];
				newimg[(yy*width)*3 + xx*3 + 2] = img[((height-1)*width)*3 + xx*3 + 2];
			}
		}

		// Now free the old image;
		free(img);

		// Use the new image:
		img = newimg;

		return true;

	}
	else
	{
		printf("Image height already divisible by four.\n");
		expandedwidth = width;
		expandedheight = height;
		return true;
	}
}


void write_big_endian_2byte_word(unsigned short *blockadr, FILE *f)
{
	uint8 bytes[2];
	unsigned short block;

	block = blockadr[0];

	bytes[0] = (block >> 8) & 0xff;
	bytes[1] = (block >> 0) & 0xff;

	fwrite(&bytes[0],1,1,f);
	fwrite(&bytes[1],1,1,f);
}

void write_big_endian_4byte_word(unsigned int *blockadr, FILE *f)
{
	uint8 bytes[4];
	unsigned int block;

	block = blockadr[0];

	bytes[0] = (block >> 24) & 0xff;
	bytes[1] = (block >> 16) & 0xff;
	bytes[2] = (block >> 8) & 0xff;
	bytes[3] = (block >> 0) & 0xff;

	fwrite(&bytes[0],1,1,f);
	fwrite(&bytes[1],1,1,f);
	fwrite(&bytes[2],1,1,f);
	fwrite(&bytes[3],1,1,f);
}


int find_pos_of_extension(const char *src)
{
	int q=strlen(src);
	while(q>=0)		// find file name extension
	{
		if(src[q]=='.') break;
		q--;
	}
	if(q<0) 
		return -1;
	else
		return q;
}

bool readSrcFile(const char *filename,uint8 *&img,int &width,int &height, int &expandedwidth, int &expandedheight)
{
	int w1,h1;
	int wdiv4, hdiv4;
	char str[255];


	// Delete temp file if it exists.
	if(fileExist("tmp.ppm"))
	{
		sprintf(str, "del tmp.ppm\n");
		system(str);
	}


	int q = find_pos_of_extension(filename);
	if(!strcmp(&filename[q],".ppm")) 
	{
		// Already a .ppm file. Just copy. 
		sprintf(str,"copy %s tmp.ppm \n", filename);
		printf("Copying source file %s to tmp.ppm\n", filename);
	}
	else
	{
		// Converting from other format to .ppm 
		// 
		// Use your favorite command line image converter program,
		// for instance Image Magick. Just make sure the syntax can
		// be written as below:
		// 
		// C:\imconv source.jpg dest.ppm
		//
		sprintf(str,"imconv %s tmp.ppm\n", filename);
		printf("Converting source file from %s to .ppm\n", filename);
	}
	// Execute system call
	system(str);

	bool FLIP;
	if(orientation == FIRST_PIXEL_IN_PPM_FILE_MAPS_TO_S0T0)
		FLIP = false;
	else if(orientation == FIRST_PIXEL_IN_PPM_FILE_MAPS_TO_S0T1)
		FLIP = true;
	else
	{
		printf("orientation error\n");
		exit(1);
	}

	if(fReadPPM("tmp.ppm",w1,h1,img,FLIP))
	{
		width=w1;
		height=h1;
		system("del tmp.ppm");

		// Width must be divisible by 2 and height must be
		// divisible by 4. Otherwise, we will not compress 
		// the image. 

		wdiv4 = width / 4;
		hdiv4 = height / 4;

		expandedwidth = width;
		expandedheight = height;

		if( !(wdiv4 * 4 == width) )
		{
			printf(" Width = %d is not divisible by four... ", width);
			printf(" expanding image in x-dir... ");
			if(expandToWidthDivByFour(img, width, height, expandedwidth, expandedheight))
			{
				printf("OK.\n");
			}
			else
			{
				printf("\n Error: could not expand image\n");
				return false;
			}
		}
		if( !(hdiv4 * 4 == height))
		{
			printf(" Height = %d is not divisible by four... ", height);
			printf(" expanding image in y-dir...");
			if(expandToHeightDivByFour(img, expandedwidth, height, expandedwidth, expandedheight))
			{
				printf("OK.\n");
			}
			else
			{
				printf("\n Error: could not expand image\n");
				return false;
			}
		}
		if(!(expandedwidth == width && expandedheight == height))
		   printf("Active pixels: %dx%d. Expanded image: %dx%d\n",width,height,expandedwidth,expandedheight);
		return true;
	}
	return false;

}
bool readSrcFileNoExpand(const char *filename,uint8 *&img,int &width,int &height)
{
	int w1,h1;
	char str[255];


	// Delete temp file if it exists.
	if(fileExist("tmp.ppm"))
	{
		sprintf(str, "del tmp.ppm\n");
		system(str);
	}


	int q = find_pos_of_extension(filename);
	if(!strcmp(&filename[q],".ppm")) 
	{
		// Already a .ppm file. Just copy. 
		sprintf(str,"copy %s tmp.ppm \n", filename);
		printf("Copying source file %s to tmp.ppm\n", filename);
	}
	else
	{
		// Converting from other format to .ppm 
		// 
		// Use your favorite command line image converter program,
		// for instance Image Magick. Just make sure the syntax can
		// be written as below:
		// 
		// C:\imconv source.jpg dest.ppm
		//
		sprintf(str,"imconv %s tmp.ppm\n", filename);
		printf("Converting source file from %s to .ppm\n", filename);
	}
	// Execute system call
	system(str);

	// The current function is only used when comparing two ppm files --- we don't need to flip them. Hence reverse_y is false
	if(fReadPPM("tmp.ppm",w1,h1,img,false))
	{
		width=w1;
		height=h1;
		system("del tmp.ppm");

		return true;
	}
	return false;

}


void compressImageFile(uint8 *img,int width,int height,char *dstfile, int expandedwidth, int expandedheight, int action)
{
	FILE *f;
	int x,y,w,h;
	unsigned int block1, block2;
	unsigned short wi, hi;
	unsigned char magic[4];
	unsigned char version[2];
	unsigned short texture_type;
	uint8 *imgdec;

	imgdec = (unsigned char*) malloc(expandedwidth*expandedheight*3);
	if(!imgdec)
	{
		printf("Could not allocate decompression buffer --- exiting\n");
		exit(1);
	}

	magic[0]   = 'P'; magic[1]   = 'K'; magic[2] = 'M'; magic[3] = ' '; 
	version[0] = '1'; version[1] = '0';
	texture_type = ETC1_RGB_NO_MIPMAPS;

	if((f=fopen(dstfile,"wb")))
	{
		w=expandedwidth/4;  w*=4;
		h=expandedheight/4; h*=4;
		wi = w;
		hi = h;

		if(ktx_mode)
		{
			printf("Outputting to .kxt file...\n");
			//.ktx file: KTX header followed by compressed binary data.
			KTX_header header;
			//identifier
			for(int i=0; i<12; i++) 
			{
				header.identifier[i]=ktx_identifier[i];
			}
			//endianess int.. if this comes out reversed, all of the other ints will too.
			header.endianness=KTX_ENDIAN_REF;
			
			//these values are always 0/1 for compressed textures.
			header.glType=0;
			header.glTypeSize=1;
			header.glFormat=0;

			header.pixelWidth=width;
			header.pixelHeight=height;
			header.pixelDepth=0;

			//we only support single non-mipmapped non-cubemap textures..
			header.numberOfArrayElements=0;
			header.numberOfFaces=1;
			header.numberOfMipmapLevels=1;

			//and no metadata..
			header.bytesOfKeyValueData=0;
			
			int halfbytes=1;
			//header.glInternalFormat=?
			//header.glBaseInternalFormat=?
			if(texture_type==ETC1_RGB_NO_MIPMAPS) 
			{
				header.glBaseInternalFormat=GL_RGB;
				header.glInternalFormat=GL_ETC1_RGB8_OES;
			}
			else 
			{
				printf("internal error: bad format!\n");
				exit(1);
			}
			//write header
			fwrite(&header,sizeof(KTX_header),1,f);
			
			//write size of compressed data.. which depend on the expanded size..
			unsigned int imagesize=(w*h*halfbytes)/2;
			fwrite(&imagesize,sizeof(int),1,f);
		}
		else
		{
			printf("outputting to .pkm file...\n");
			// Write magic number
			fwrite(&magic[0], sizeof(unsigned char), 1, f);
			fwrite(&magic[1], sizeof(unsigned char), 1, f);
			fwrite(&magic[2], sizeof(unsigned char), 1, f);
			fwrite(&magic[3], sizeof(unsigned char), 1, f);
	
			// Write version
			fwrite(&version[0], sizeof(unsigned char), 1, f);
			fwrite(&version[1], sizeof(unsigned char), 1, f);

			// Write texture type
			write_big_endian_2byte_word(&texture_type, f);

			// Write binary header: the width and height as unsigned 16-bit words
			write_big_endian_2byte_word(&wi, f);
			write_big_endian_2byte_word(&hi, f);

			// Also write the active pixels. For instance, if we want to compress
			// a 128 x 129 image, we have to extend it to 128 x 132 pixels.
			// Then the wi and hi written above will be 128 and 132, but the
			// additional information that we write below will be 128 and 129,
			// to indicate that it is only the top 129 lines of data in the 
			// decompressed image that will be valid data, and the rest will
			// be just garbage. 

			unsigned short activew, activeh;
			activew = width;
			activeh = height;

			write_big_endian_2byte_word(&activew, f);
			write_big_endian_2byte_word(&activeh, f);
		}

		int totblocks = expandedheight/4 * expandedwidth/4;
		int countblocks = 0;

		/// xxx
		for(y=0;y<expandedheight/4;y++)
		{
			for(x=0;x<expandedwidth/4;x++)
			{
				countblocks++;

				switch(action)
				{
				case 0:
					// FAST only tests the two most likely base colors.
					compressBlockDiffFlipFast(img, imgdec, expandedwidth, expandedheight, 4*x, 4*y, block1, block2);
					break;
 				case 1:
					// The MEDIUM version tests all colors in a 3x3x3 cube around the average colors
					// This increases the likelihood that the differential mode is selected.
					compressBlockDiffFlipMedium(img,expandedwidth,expandedheight,4*x,4*y, block1, block2);		
					printf("Compressed %d of %d blocks, %.1f%% finished.\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b", countblocks, totblocks, 100.0*countblocks/(1.0*totblocks));
 					break;
 				case 2:
					// The SLOW version tests all colors in a a 5x5x5 cube around the average colors
					// It also tries the nondifferential mode for each block even if the differential succeeds.
					compressBlockDiffFlipSlow(img,expandedwidth,expandedheight,4*x,4*y, block1, block2);		
					printf("Compressed %d of %d blocks, %.1f%% finished.\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b", countblocks, totblocks, 100.0*countblocks/(1.0*totblocks));
					break;
 				case 3:
					// FAST with PERCEPTUAL error metric
					compressBlockDiffFlipFastPerceptual(img, imgdec, expandedwidth, expandedheight, 4*x, 4*y, block1, block2);
 					break;
 				case 4:
					// MEDIUM with PERCEPTUAL error metric
					compressBlockDiffFlipMediumPerceptual(img,expandedwidth,expandedheight,4*x,4*y, block1, block2);
					printf("Compressed %d of %d blocks, %.1f%% finished.\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b", countblocks, totblocks, 100.0*countblocks/(1.0*totblocks));
 					break;
				case 5:
					// SLOW with PERCEPTUAL error metric
					compressBlockDiffFlipSlowPerceptual(img,expandedwidth,expandedheight,4*x,4*y, block1, block2);		
					printf("Compressed %d of %d blocks, %.1f%% finished.\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b", countblocks, totblocks, 100.0*countblocks/(1.0*totblocks));
					break;
				default:
				    printf("Not implemented.\n");
					exit(1);
				    break;
				}
				write_big_endian_4byte_word(&block1, f);
				write_big_endian_4byte_word(&block2, f);

			}
		}
			printf("\n");

		fclose(f);
		printf("Saved file <%s>.\n",dstfile);
	}
	
	free(imgdec);
}

double calculatePSNR(uint8 *lossyimg, uint8 *origimg, int width, int height)
{
	// calculate Mean Square Error (MSE)

	int x,y;
	double MSE;
	double PSNR;
	double err;
	MSE = 0;

	// Note: This calculation of PSNR uses the formula
	//
	// PSNR = 10 * log_10 ( 255^2 / MSE ) 
	// 
	// where the MSE is calculated as
	//
	// 1/(N*M) * sum ( 1/3 * ((R' - R)^2 + (G' - G)^2 + (B' - B)^2) ) 
	//
	// The reason for having the 1/3 factor is the following:
	// Presume we have a grayscale image, that is acutally just the red component 
	// of a color image.. The squared error is then (R' - R)^2.
	// Assume that we have a certain signal to noise ratio, say 30 dB. If we add
	// another two components (say green and blue) with the same signal to noise 
	// ratio, we want the total signal to noise ratio be the same. For the
	// squared error to remain constant we must divide by three after adding
	// together the squared errors of the components. 

	for(y=0;y<height;y++)
	{
		for(x=0;x<width;x++)
		{
			err = lossyimg[y*width*3+x*3+0] - origimg[y*width*3+x*3+0];
		    MSE = MSE + ((err * err)/3.0);
			err = lossyimg[y*width*3+x*3+1] - origimg[y*width*3+x*3+1];
		    MSE = MSE + ((err * err)/3.0);
			err = lossyimg[y*width*3+x*3+2] - origimg[y*width*3+x*3+2];
		    MSE = MSE + ((err * err)/3.0);
		}
	}
	MSE = MSE / (width * height);
	if(MSE == 0)
	{
		printf("********************************************************************\n");
		printf("There is no difference at all between image files --- infinite PSNR.\n");
		printf("********************************************************************\n");
	}
	printf("RMSE = %f \n",sqrt(MSE * 3.0));
	PSNR = (float)(10*log((double)((255*255)/MSE))/log((double)10));
	return PSNR;
}
double calculatePerceptuallyWeightedPSNR(uint8 *lossyimg, uint8 *origimg, int width, int height)
{
	// calculate Perceptually Weighted Mean Square Error (wMSE)

	int x,y;
	double wMSE;
	double PSNR;
	double err;
	wMSE = 0;

	// Note: This calculation of PSNR uses the formula
	//
	// PSNR = 10 * log_10 ( 255^2 / wMSE ) 
	// 
	// where the wMSE is calculated as
	//
	// 1/(N*M) * sum ( ( w1*(R' - R)^2 + w2*(G' - G)^2 + w3*(B' - B)^2) ) 
	//
	// The reason for having the 1/3 factor is the following:
	// Presume we have a grayscale image, that is acutally just the red component 
	// of a color image.. The squared error is then (R' - R)^2.
	// Assume that we have a certain signal to noise ratio, say 30 dB. If we add
	// another two components (say green and blue) with the same signal to noise 
	// ratio, we want the total signal to noise ratio be the same. For the
	// squared error to remain constant we must divide by three after adding
	// together the squared errors of the components. 

	double w1 = 0.299, w2 = 0.587, w3 = 0.114;

	for(y=0;y<height;y++)
	{
		for(x=0;x<width;x++)
		{
			err = lossyimg[y*width*3+x*3+0] - origimg[y*width*3+x*3+0];
		    wMSE = wMSE + (w1*(err * err));
			err = lossyimg[y*width*3+x*3+1] - origimg[y*width*3+x*3+1];
		    wMSE = wMSE + (w2*(err * err));
			err = lossyimg[y*width*3+x*3+2] - origimg[y*width*3+x*3+2];
		    wMSE = wMSE + (w3*(err * err));
		}
	}
	wMSE = wMSE / (width * height);
	PSNR = (float)(10*log((double)((255*255)/wMSE))/log((double)10));
	return PSNR;
}


double calculatePSNRfile(char *srcfile, uint8 *origimg)
{
	FILE *f;
	int x,y;
	int width,height;
	unsigned int block_part1, block_part2;
	uint8 *img;
	unsigned short w, h;
	unsigned char magic[4];
	unsigned char version[2];
	unsigned short texture_type;
	int active_width;
	int active_height;
	int format;
	f=fopen(srcfile,"rb");
	if(f)
	{
		if(ktx_mode)
		{
			//read ktx header..
			KTX_header header;
			fread(&header,sizeof(KTX_header),1,f);
			//read size parameter, which we don't actually need..
			unsigned int bitsize;
			fread(&bitsize,sizeof(unsigned int),1,f);
	
			active_width = header.pixelWidth;
			active_height = header.pixelHeight;
			w = ((active_width+3)/4)*4;
			h = ((active_height+3)/4)*4;
			width=w;
			height=h;

			if(header.glInternalFormat==GL_ETC1_RGB8_OES) 
			{
				format=ETC1_RGB_NO_MIPMAPS;
			}
			else {
				printf("ktx file has unknown glInternalFormat (not etc compressed)!\n");
				exit(1);
			}
		}
		else
		{
			// Read magic nunmber
			fread(&magic[0], sizeof(unsigned char), 1, f);
			fread(&magic[1], sizeof(unsigned char), 1, f);
			fread(&magic[2], sizeof(unsigned char), 1, f);
			fread(&magic[3], sizeof(unsigned char), 1, f);
			if(!(magic[0] == 'P' && magic[1] == 'K' && magic[2] == 'M' && magic[3] == ' '))
			{
				printf("\n\n The file %s is not a .pkm file.\n",srcfile);
				exit(1);
			}

			// Read version
			fread(&version[0], sizeof(unsigned char), 1, f);
			fread(&version[1], sizeof(unsigned char), 1, f);
			if(!(version[0] == '1' && version[1] == '0'))
			{
				printf("\n\n The file %s is not of version 1.0 but of version %c.%c.\n",srcfile, version[0], version[1]);
				exit(1);
			}

			// Read texture type
			read_big_endian_2byte_word(&texture_type, f);
			if(!(texture_type == ETC1_RGB_NO_MIPMAPS))
			{
				printf("\n\n The file %s does not contain a texture of known format.\n", srcfile);
				printf("Known formats: ETC1_RGB_NO_MIPMAPS. %s\n", srcfile);
				exit(1);
			}

			read_big_endian_2byte_word(&w, f);
			read_big_endian_2byte_word(&h, f);
			width = w;
			height = h;

			read_big_endian_2byte_word(&w, f);
			read_big_endian_2byte_word(&h, f);
			active_width = w;
			active_height = h;
		}

		printf("width = %d, height = %d\n",width, height);

		img=(uint8*)malloc(3*width*height);
		if(!img)
		{
			printf("Error: could not allocate memory\n");
			exit(0);
		}
		
		for(y=0;y<height/4;y++)
		{
			for(x=0;x<width/4;x++)
			{
				read_big_endian_4byte_word(&block_part1,f);
				read_big_endian_4byte_word(&block_part2,f);
				decompressBlockDiffFlip(block_part1, block_part2,img,width,height,4*x,4*y);
			}
		}

		// calculate Mean Square Error (MSE)

		double MSE;
		double wMSE;
		double PSNR;
		double wPSNR;
		double err;
		MSE = 0;
		wMSE = 0;
		for(y=0;y<active_height;y++)
		{
			for(x=0;x<active_width;x++)
			{
				err = img[y*width*3+x*3+0] - origimg[y*width*3+x*3+0];
		        MSE  += ((err * err)/3.0);
				wMSE += PERCEPTUAL_WEIGHT_R_SQUARED * (err*err);
				err = img[y*width*3+x*3+1] - origimg[y*width*3+x*3+1];
		        MSE  += ((err * err)/3.0);
				wMSE += PERCEPTUAL_WEIGHT_G_SQUARED * (err*err);
				err = img[y*width*3+x*3+2] - origimg[y*width*3+x*3+2];
		        MSE  += ((err * err)/3.0);
				wMSE += PERCEPTUAL_WEIGHT_B_SQUARED * (err*err);
			}
		}
		MSE = MSE / (active_width * active_height);
		wMSE = wMSE / (active_width * active_height);
        PSNR = (float)(10*log((double)((255*255)/MSE))/log((double)10));
		wPSNR = (float)(10*log((double)((255*255)/wMSE))/log((double)10));

		printf("Perceptually weighted PSNR = (%f)\n",wPSNR);
		fclose(f);
		free(img);
		return PSNR;
	}
	else
	{
		printf("Error: could not open <%s>.\n",srcfile);
		return -1;
	}
}

void compressFile(char *srcfile,char *dstfile, int action)
{
	uint8 *srcimg;
	int width,height;
	int extendedwidth, extendedheight;
	double PSNR;

	printf("\n");
	switch(action)
	{
		case 0:
			printf("Using FAST compression mode and NONPERCEPTUAL error metric\n");
			break;
		case 1:
			printf("Using MEDIUM-speed compression mode and NONPERCEPTUAL error metric\n");
			break;
		case 2:
			printf("Using SLOW compression mode and NONPERCEPTUAL error metric\n");
			break;
		case 3:
			printf("Using FAST compression mode and PERCEPTUAL error metric\n");
			break;
		case 4:
			printf("Using MEDIUM-speed compression mode and PERCEPTUAL error metric\n");
			break;
		case 5:
			printf("Using SLOW compression mode and PERCEPTUAL error metric\n");
			break;
	}
	printf("Using the orientation that maps the first pixel in .ppm file to ");
	if(orientation == FIRST_PIXEL_IN_PPM_FILE_MAPS_TO_S0T0)
		printf("s=0, t=0.\n");
	else if(orientation == FIRST_PIXEL_IN_PPM_FILE_MAPS_TO_S0T1)
		printf("s=0, t=1.\n");

	if(readSrcFile(srcfile,srcimg,width,height,extendedwidth, extendedheight))
	{
		printf("Compressing...\n");
		compressImageFile(srcimg,width,height,dstfile,extendedwidth, extendedheight, action);			
		PSNR = calculatePSNRfile(dstfile, srcimg);
		free(srcimg);
		printf("PSNR = %f\n",PSNR);
	}
}

double calculatePSNRTwoFiles(char *srcfile1,char *srcfile2)
{
	uint8 *srcimg1;
	uint8 *srcimg2;
	int width1, height1;
	int width2, height2;
	double PSNR = 0.0f;
	double perceptually_weighted_PSNR;

	if(readSrcFileNoExpand(srcfile1,srcimg1,width1,height1))
	{
		if(readSrcFileNoExpand(srcfile2,srcimg2,width2,height2))
		{
			if((width1 == width2) && (height1 == height2))
			{
				PSNR = calculatePSNR(srcimg1, srcimg2, width1, height1);
				printf("PSNR = %f\n",PSNR);
				perceptually_weighted_PSNR = calculatePerceptuallyWeightedPSNR(srcimg1, srcimg2, width1, height1);
				printf("perceptually weighted PSNR = (%f)\n",perceptually_weighted_PSNR);

			}
			else
			{
				printf("\n Width and height do no not match for image: width, height = (%d, %d) and (%d, %d)\n",width1,height1, width2, height2);
			}
		}
		else
		{
			printf("Couldn't open file %s.\n",srcfile2);
		}
	}
	else
	{
		printf("Couldn't open file %s.\n",srcfile1);
	}

	return PSNR;
}


void uncompressFile(char *srcfile,char *dstfile)
{
	FILE *f;
	int width,height;
	unsigned int block_part1, block_part2;
	uint8 *img, *newimg;
	char str[300];
	unsigned short w, h;
	int xx, yy;
	unsigned char magic[4];
	unsigned char version[2];
	unsigned short texture_type;
	int active_width;
	int active_height;
	int format;

	f=fopen(srcfile,"rb");
	if (f)
	{
		if(ktx_mode)
		{
			//read ktx header..
			KTX_header header;
			fread(&header,sizeof(KTX_header),1,f);
			//read size parameter, which we don't actually need..
			unsigned int bitsize;
			fread(&bitsize,sizeof(unsigned int),1,f);
	
			active_width = header.pixelWidth;
			active_height = header.pixelHeight;
			w = ((active_width+3)/4)*4;
			h = ((active_height+3)/4)*4;
			width=w;
			height=h;

			if(header.glInternalFormat==GL_ETC1_RGB8_OES) 
			{
				format=ETC1_RGB_NO_MIPMAPS;
			}
			else {
				printf("ktx file has unknown glInternalFormat (not etc compressed)!\n");
				exit(1);
			}
		}
		else
		{
			// Read magic nunmber
			fread(&magic[0], sizeof(unsigned char), 1, f);
			fread(&magic[1], sizeof(unsigned char), 1, f);
			fread(&magic[2], sizeof(unsigned char), 1, f);
			fread(&magic[3], sizeof(unsigned char), 1, f);
			if(!(magic[0] == 'P' && magic[1] == 'K' && magic[2] == 'M' && magic[3] == ' '))
			{
				printf("\n\n The file %s is not a .pkm file.\n",srcfile);
				exit(1);
			}
	
			// Read version
			fread(&version[0], sizeof(unsigned char), 1, f);
			fread(&version[1], sizeof(unsigned char), 1, f);
			if(!(version[0] == '1' && version[1] == '0'))
			{
				printf("\n\n The file %s is not of version 1.0 but of version %c.%c.\n",srcfile, version[0], version[1]);
				exit(1);
			}

			// Read texture type
			read_big_endian_2byte_word(&texture_type, f);
			if(!(texture_type == ETC1_RGB_NO_MIPMAPS))
			{
				printf("\n\n The file %s does not contain a ETC1_RGB_NO_MIPMAPS texture.\n", srcfile);
				exit(1);
			}

			// Read how many pixels the blocks make up

			read_big_endian_2byte_word(&w, f);
			read_big_endian_2byte_word(&h, f);
			width = w;
			height = h;

			// Read how many pixels contain active data (the rest are just
			// for making sure we have a 2*a x 4*b size).

			read_big_endian_2byte_word(&w, f);
			read_big_endian_2byte_word(&h, f);
			active_width = w;
			active_height = h;
		}

		printf("Width = %d, Height = %d\n",width, height);
		printf("active pixel area: top left %d x %d area.\n",active_width, active_height);

		img=(uint8*)malloc(3*width*height);
		if(!img)
		{
			printf("Error: could not allocate memory\n");
			exit(0);
		}
		

		for(int y=0;y<height/4;y++)
		{
			for(int x=0;x<width/4;x++)
			{

				read_big_endian_4byte_word(&block_part1,f);
				read_big_endian_4byte_word(&block_part2,f);
				decompressBlockDiffFlip(block_part1, block_part2,img,width,height,4*x,4*y);
			}
		}

		// Ok, and now only write out the active pixels to the .ppm file.
		// (But only if the active pixels differ from the total pixels)

		if( !(height == active_height && width == active_width) )
		{
			newimg=(uint8*)malloc(3*active_width*active_height);
			if(!newimg)
			{
				printf("Error: could not allocate memory\n");
				exit(0);
			}
			
			// Convert from total area to active area:

			for(yy = 0; yy<active_height; yy++)
			{
				for(xx = 0; xx< active_width; xx++)
				{
					newimg[ (yy*active_width)*3 + xx*3 + 0 ] = img[ (yy*width)*3 + xx*3 + 0];
					newimg[ (yy*active_width)*3 + xx*3 + 1 ] = img[ (yy*width)*3 + xx*3 + 1];
					newimg[ (yy*active_width)*3 + xx*3 + 2 ] = img[ (yy*width)*3 + xx*3 + 2];
				}
			}

			free(img);
			img = newimg;
		}

		if(orientation == FIRST_PIXEL_IN_PPM_FILE_MAPS_TO_S0T0)
			fWritePPM("tmp.ppm",active_width,active_height,img,false);
		else if(orientation == FIRST_PIXEL_IN_PPM_FILE_MAPS_TO_S0T1)
			fWritePPM("tmp.ppm",active_width,active_height,img,true);
		else
		{
			printf("error, orientation not any of the two legal values.\n");
			exit(1);
		}

		printf("Saved file tmp.ppm \n\n");

		// Delete destination file if it exists
		if(fileExist(dstfile))
		{
			sprintf(str, "del %s\n",dstfile);	
			system(str);
		}


		int q = find_pos_of_extension(dstfile);
		if(!strcmp(&dstfile[q],".ppm")) 
		{
			// Already a .ppm file. Just rename. 
			sprintf(str,"move tmp.ppm %s\n",dstfile);
			printf("Renaming destination file to %s\n",dstfile);
		}
		else
		{
			// Converting from .ppm to other file format
			// 
			// Use your favorite command line image converter program,
			// for instance Image Magick. Just make sure the syntax can
			// be written as below:
			// 
			// C:\imconv source.ppm dest.jpg
			//
			sprintf(str,"imconv tmp.ppm %s\n",dstfile);
			printf("Converting destination file from .ppm to %s\n",dstfile);
		}
		// Execute system call
		system(str);

		fclose(f);
		free(img);
	}
	else
	{
		printf("Error: could not open <%s>.\n",srcfile);
	}
}

int determineAction(int argc,char *argv[],char *dst)
{
	char *src;
	int q;

	enum {MODE_COMPRESS, MODE_UNCOMPRESS, MODE_PSNR};
    enum {SPEED_SLOW, SPEED_FAST, SPEED_MEDIUM};
	enum {METRIC_PERCEPTUAL, METRIC_NONPERCEPTUAL};

	int mode = MODE_COMPRESS;
	int speed = SPEED_FAST;
	int metric = METRIC_PERCEPTUAL;

  // A bit hackish: First check for the orientation flag. When this flag is set, remove it from the string and proceed with the rest of the arguments as before. 

	bool orientation_flag_found = false;
	orientation = FIRST_PIXEL_IN_PPM_FILE_MAPS_TO_S0T0;

    for(q = 1; q < argc && !orientation_flag_found; q++)
	{
		src = argv[q];
	    if(!strcmp(src, "-o"))
		{
			orientation_flag_found = true;
			src = argv[q+1];
			if(!strcmp(src, "topleftmapsto_s0t0"))
			{
				orientation = FIRST_PIXEL_IN_PPM_FILE_MAPS_TO_S0T0;
			}
			else if(!strcmp(src, "bottomleftmapsto_s0t0"))
			{
				orientation = FIRST_PIXEL_IN_PPM_FILE_MAPS_TO_S0T1;
			}
			else
			{
				return -1;
			}
			// At this stage in the code we know we have a valid orientation argument.
			// Now remove it from the argument list. 
            for(int xx=q+2; xx<argc; xx++)
			{
				argv[xx-2] = argv[xx];
			} 
			argc = argc - 2;
		}
	}


	// First check the number of arguments.

	if(argc == 3)
	{
		// We have a situation similar to this one:
		// etcpack img.ppm img.pkm


		// Find the extension of the first file name:	
		src = argv[1];
		q = find_pos_of_extension(src);
		if(q<0) return -1;

		// If we have etcpack img.pkm img.any

		if(!strcmp(&src[q],".pkm") || !strcmp(&src[q],".ktx")) 
		{
			// First argument is .pkm. Uncompress. 
			mode = MODE_UNCOMPRESS;			// uncompress from binary file format .pkm
			strcpy(dst,argv[2]);

			if(!strcmp(&src[q],".ktx"))
				ktx_mode = true;
			else
				ktx_mode = false;

			// Make sure second argument is not also .pkm or .ktx
			src = argv[2];
			q = find_pos_of_extension(src);
			if(q<0) return -1;
			if(!strcmp(&src[q],".pkm") || !strcmp(&src[q],".ktx")) 
			{
				printf("At least one argument has to be uncompressed (.ppm, .png, ..., not .ktx)\n");
				return -1;
			}

		}
		else
		{
			// The first argument was not .pkm. The second argument must then be .pkm.
			src = argv[2];
			q = find_pos_of_extension(src);
			if(q<0) return -1;
			
			if(!strcmp(&src[q],".pkm") || !strcmp(&src[q],".ktx")) 
			{
				// Second argument is .pkm. Compress. 
				mode = MODE_COMPRESS;			// compress to binary file format .pkm
				strcpy(dst,argv[2]);
				if(!strcmp(&src[q],".ktx"))
					ktx_mode = true;
				else
					ktx_mode = false;
			}
			else
			{
				printf("At least one argument has to be compressed (.pkm, .ktx) unless -p flag is used.\n");
					return -1;
			}
		}
	}
	else if(argc == 4)
	{
		// We must have the following situation:
		// etcpack -p img1.ppm img2.ppm
		src = argv[1];
		ktx_mode = false;
	    if(!strcmp(src, "-p"))
		{
			// We should do PSNR between argv[2] and argv[3]
			mode = MODE_PSNR;
			strcpy(dst,argv[3]);
		}
		else
		{
			// Error
			return -1;
		}
	}
	else if(argc == 5)
	{
		// We must be having one of the two following cases:
		// etcpack -s {fast|medium|slow} img.ppm img.pkm
		// etcpack -e {perceptual|nonperceptual} img.ppm img.pkm

		// First try if we have .pkm in the end (we should, otherwise arguments do not make sense).
		
		src = argv[4];
		q = find_pos_of_extension(src);
		if(q<0) return -1;

		if(!(!strcmp(&src[q],".pkm") || !strcmp(&src[q],".ktx"))) 
		{
			// The last argument is not .pkm or . Explain and give error:
			printf("Error: argument %s %s is not valid for decompression\n",argv[1],argv[2]);
			return -1;
		}

		if(!strcmp(&src[q],".ktx"))
			ktx_mode = true;
		else
			ktx_mode = false;

		// Make sure first file argument is not also .pkm or .ktx
		src = argv[3];
		q = find_pos_of_extension(src);
		if(q<0) return -1;
		if(!strcmp(&src[q],".pkm") || !strcmp(&src[q],".ktx")) 
		{
			printf("At least one argument has to be uncompressed (.ppm, .png, ..., not .ktx)\n");
			return -1;
		}

		mode = MODE_COMPRESS;

		// Ok, we are compressing. Set dst file:
		strcpy(dst,argv[4]);
		
		// Now see what the argument is:

		if(!strcmp(argv[1],"-s")) 
		{
			// We have argument -s. Now check for slow, medium or fast.
			
			if(!strcmp(argv[2],"slow")) 
			{
				speed = SPEED_SLOW;
			}
			else if(!strcmp(argv[2],"medium")) 
			{
				speed = SPEED_MEDIUM;
			}
			else if(!strcmp(argv[2],"fast")) 
			{
				speed = SPEED_FAST;
			}
			else
			{
				printf("Error: %s not part of switch %s\n",argv[2], argv[1]);
				exit(1);
			}

		}
		else if(!strcmp(argv[1],"-e")) 
		{

			// We have argument -e. Now check for perceptual or nonperceptual
			
			if(!strcmp(argv[2],"perceptual")) 
			{
				metric = METRIC_PERCEPTUAL;
			}
			else if(!strcmp(argv[2],"nonperceptual")) 
			{
				metric = METRIC_NONPERCEPTUAL;
			}
			else
			{
				printf("Error: %s not part of switch %s\n",argv[2], argv[1]);
				exit(1);
			}
		}
		else
		{
			printf("Error: cannot interpret switch %s %s\n",argv[1], argv[2]);
			return -1;
		}
	}
	else if(argc == 7)
	{
		// We must be having one of the two following cases:
		// etcpack -s {fast|medium|slow} -e {perceptual|nonperceptual} img.ppm img.pkm
		// etcpack -e {perceptual|nonperceptual} -s {slow|medium|fast} img.ppm img.pkm

		// First try if we have .pkm in the end (we should, otherwise arguments do not make sense).
		
		src = argv[6];
		q = find_pos_of_extension(src);
		if(q<0) return -1;

		if(!(!strcmp(&src[q],".pkm") || !strcmp(&src[q],".ktx"))) 
		{
			// The last argument is not .pkm. Explain and give error:
			printf("Error: argument %s %s is not valid for decompression\n",argv[1],argv[2]);
			return -1;
		}

		if(!strcmp(&src[q],".ktx"))
			ktx_mode = true;
		else
			ktx_mode = false;

		// Make sure first file argument is not also .pkm or .ktx
		src = argv[5];
		q = find_pos_of_extension(src);
		if(q<0) return -1;
		if(!strcmp(&src[q],".pkm") || !strcmp(&src[q],".ktx")) 
		{
			printf("At least one argument has to be uncompressed (.ppm, .png, ..., not .ktx)\n");
			return -1;
		}

		mode = MODE_COMPRESS;

		// Ok, we are compressing. Set dst file:

		strcpy(dst,argv[6]);
		
		// Now see what the argument is:

		if(!strcmp(argv[1],"-s")) 
		{
			// We have argument -s. Now check for slow, medium or fast. 
			
			if(!strcmp(argv[2],"slow")) 
			{
				speed = SPEED_SLOW;
			}
			else if(!strcmp(argv[2],"medium")) 
			{
				speed = SPEED_MEDIUM;
			}
			else if(!strcmp(argv[2],"fast")) 
			{
				speed = SPEED_FAST;
			}
			else
			{
				printf("Error: %s not part of switch %s\n",argv[2], argv[1]);
				exit(1);
			}

		}
		else if(!strcmp(argv[1],"-e")) 
		{

			// We have argument -e. Now check for perceptual or nonperceptual
			
			if(!strcmp(argv[2],"perceptual")) 
			{
				metric = METRIC_PERCEPTUAL;
			}
			else if(!strcmp(argv[2],"nonperceptual")) 
			{
				metric = METRIC_NONPERCEPTUAL;
			}
			else
			{
				printf("Error: %s not part of switch %s\n",argv[2], argv[1]);
				exit(1);
			}
		}
		else
		{
			printf("Error: cannot interpret switch %s %s\n",argv[1], argv[2]);
			return -1;
		}

		// Then take on the next input argument

		if(!strcmp(argv[3],"-s")) 
		{
			// We have argument -s. Now check for slow, medium or fast.
			
			if(!strcmp(argv[4],"slow")) 
			{
				speed = SPEED_SLOW;
			}
			else if(!strcmp(argv[4],"medium")) 
			{
				speed = SPEED_MEDIUM;
			}
			else if(!strcmp(argv[4],"fast")) 
			{
				speed = SPEED_FAST;
			}
			else
			{
				printf("Error: %s not part of switch %s\n",argv[4], argv[3]);
				exit(1);
			}
		}
		else if(!strcmp(argv[3],"-e")) 
		{
			// We have argument -e. Now check for perceptual or nonperceptual
			
			if(!strcmp(argv[4],"perceptual")) 
			{
				metric = METRIC_PERCEPTUAL;
			}
			else if(!strcmp(argv[4],"nonperceptual")) 
			{
				metric = METRIC_NONPERCEPTUAL;
			}
			else
			{
				printf("Error: %s not part of switch %s\n",argv[4], argv[3]);
				exit(1);
			}

		}
		else
		{
			printf("Error: cannot interpret switch %s %s\n",argv[3], argv[4]);
			return -1;
		}
	}
	else
	{
			printf("Error: wrong number of arguments.\n");
			return -1;
	}

	// 0: compress from .any to .pkm with SPEED_FAST, METRIC_NONPERCEPTUAL, 
	// 1: compress from .any to .pkm with SPEED_MEDIUM, METRIC_NONPERCEPTUAL, 
	// 2: compress from .any to .pkm with SPEED_SLOW, METRIC_NONPERCEPTUAL, 
	// 3: compress from .any to .pkm with SPEED_FAST, METRIC_PERCEPTUAL, 
	// 4: compress from .any to .pkm with SPEED_MEDIUM, METRIC_PERCEPTUAL, 
	// 5: compress from .any to .pkm with SPEED_SLOW, METRIC_PERCEPTUAL, 
	// 6: decompress from .pkm to .any
	// 7: calculate PSNR between .any and .any

	if(mode == MODE_UNCOMPRESS)
	{
		return 6;
	}
	else if(mode == MODE_PSNR)
	{
		return 7;
	}
	else // mode == MODE_COMPRESS
	{
		if(speed == SPEED_FAST && metric == METRIC_NONPERCEPTUAL)
			return 0;
		else if (speed == SPEED_MEDIUM && metric == METRIC_NONPERCEPTUAL)
			return 1;
		else if (speed == SPEED_SLOW && metric == METRIC_NONPERCEPTUAL)
			return 2;
		else if (speed == SPEED_FAST && metric == METRIC_PERCEPTUAL)
			return 3;
		else if (speed == SPEED_MEDIUM && metric == METRIC_PERCEPTUAL)
			return 4;
		else if (speed == SPEED_SLOW && metric == METRIC_PERCEPTUAL)
			return 5;
	}
	return -1;
}

int main(int argc,char *argv[])
{
	if(argc==3 || argc==4 || argc == 5 || argc == 7 || argc == 9)
	{
		int action;
		// The source file is always the second last one. 
		char *srcfile=argv[argc-2];
	    char dstfile[200];
		action=determineAction(argc,argv,dstfile);
		if(action ==-1)
		{
			printf("Error: not a valid argument and/or file extension.\n");
			printf("Run etcpack without arguments to see argument list.\n");
		}
		else
		{
			
			if(!fileExist(srcfile))
			{
				printf("Error: file <%s> does not exist.\n",srcfile);
				exit(0);
			}
			// 0: compress from .any to .pkm with SPEED_FAST, METRIC_NONPERCEPTUAL, 
			// 1: compress from .any to .pkm with SPEED_MEDIUM, METRIC_NONPERCEPTUAL, 
			// 2: compress from .any to .pkm with SPEED_SLOW, METRIC_NONPERCEPTUAL, 
			// 3: compress from .any to .pkm with SPEED_FAST, METRIC_PERCEPTUAL, 
			// 4: compress from .any to .pkm with SPEED_MEDIUM, METRIC_PERCEPTUAL, 
			// 5: compress from .any to .pkm with SPEED_SLOW, METRIC_PERCEPTUAL, 
			// 6: decompress from .pkm to .any
			// 7: calculate PSNR between .any and .any

			if(action == 6)
			{
				printf("Uncompressing from .pkm file ...\n");
				printf("Using the orientation that maps the first pixel in .ppm file to ");
				if(orientation == FIRST_PIXEL_IN_PPM_FILE_MAPS_TO_S0T0)
					printf("s=0, t=0.\n");
				else if(orientation == FIRST_PIXEL_IN_PPM_FILE_MAPS_TO_S0T1)
					printf("s=0, t=1.\n");

				uncompressFile(srcfile,dstfile);
			}
			else if(action == 7)
			{
				printf("Calculating PSNR between files...\n");
			    calculatePSNRTwoFiles(srcfile,dstfile);
			}
			else
			{
				compressFile(srcfile, dstfile, action);
			}
		}
	} else {
		printf("ETCPACK v1.06\n");
		printf("Usage: etcpack srcfile dstfile\n\nCompresses and decompresses images using the Ericsson Texture Compression (ETC) scheme.\n\n");
		printf("      -s {fast|medium|slow}              Compression speed. Slow = best quality\n");
		printf("                                         (default: fast)\n");
		printf("      -e {perceptual|nonperceptual}      Error metric: Perceptual (nicest) or \n");
		printf("                                         nonperceptual (highest PSNR)\n");
		printf("                                         (default: perceptual)\n");
		printf("      -o {topleftmapsto_s0t0|            Orientation: Which pixel (top left or\n");
		printf("          bottomleftmapsto_s0t0}         bottom left) that will map to texture\n");
        printf("                                         coordinates (s=0, t=0). \n");
		printf("                                         (default: topleftmapsto_s0t0.) For a \n");
		printf("                                         .ppm file this means that the first \n");
        printf("                                         pixel in the file will be mapped to \n");
        printf("                                         s=0, t=0 by default.\n");
		printf("                                                             \n");
		printf("Examples: \n");
		printf("  etcpack img.ppm img.ktx                Compresses img.ppm to img.ktx\n");
		printf("  etcpack img.ppm img.pkm                Compresses img.ppm to img.pkm\n");
		printf("  etcpack img.ktx img_copy.ppm           Decompresses img.ktx to img_copy.ppm\n");
		printf("  etcpack -s slow img.ppm img.ktx        Compress using the slow mode.\n");
		printf("  etcpack -p orig.ppm copy.ppm           Calculate PSNR between orig and copy\n\n");

	}
 	return 0;

}
