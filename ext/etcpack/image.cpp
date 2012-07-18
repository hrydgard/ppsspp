// image.cxx 
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
#include <stdlib.h>
#include <string.h>
#include "image.h"

// Removes comments in a .ppm file
// (i.e., lines starting with #)
//
// Written by Jacob Strom
//
void removeComments(FILE *f1)
{
	int c;

	while((c = getc(f1)) == '#')
	{
		char line[1024];
		fgets(line, 1024, f1);
	}
	ungetc(c, f1);
}


// Removes white spaces in a .ppm file
//
// Written by Jacob Strom
//
void removeSpaces(FILE *f1)
{
	int c;

	c = getc(f1);
	while(c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r')
	{
		c = getc(f1);
	}
	ungetc(c, f1);
}

// fReadPPM
//
// Written by Jacob Strom
//
// reads a ppm file with P6 header (meaning binary, as opposed to P5, which is ascII)
// and returns the image in pixels.
//
// The header must look like this:
// 
// P6
// # Comments (not necessary)
// width height
// 255
//
// after that follows RGBRGBRGB...
// 
bool fReadPPM(const char *filename, int &width, int &height, unsigned char *&pixels, bool reverse_y)
{
	FILE *f1;
	int mustbe255;
	f1 = fopen(filename, "rb");

	if(f1)
	{
		char line[255];

		removeSpaces(f1);
		removeComments(f1);
		removeSpaces(f1);

		fscanf(f1, "%s", line);

		if(strcmp(line, "P6")!=0)
		{
			printf("Error: %s is not binary\n", filename);
			printf("(Binary .ppm files start with P6).\n");
			fclose(f1);
			return false;
		}
		removeSpaces(f1);
		removeComments(f1);
		removeSpaces(f1);
		
		fscanf(f1, "%d %d", &width, &height);
		if( width<=0 || height <=0)
		{
			printf("Error: width or height negative. File: %s\n",filename);
			fclose(f1);
			return false;
		}

		removeSpaces(f1);
		removeComments(f1);
		removeSpaces(f1);

		fscanf(f1, "%d", &mustbe255);
		if( mustbe255!= 255 )
		{
			printf("Error: Color resolution must be 255. File: %s\n",filename);
			fclose(f1);
			return false;
		}

		// We need to remove the newline.
		char c = 0;
		while(c != '\n')
			fscanf(f1, "%c", &c);
		

		pixels = (unsigned char*) malloc(3*width*height);
		
		if(!pixels)
		{
			printf("Error: Could not allocate memory for image. File: %s\n", filename);
			fclose(f1);
			return false;
		}

		if(reverse_y)
		{
			for(int yy = 0; yy<height; yy++)
			{
				if(fread(&pixels[(height-yy-1)*width*3], 3*width, 1, f1) != 1)
				{
					printf("Error: Could not read all pixels. File: %s\n", filename);
					free(pixels);
					fclose(f1);
					return false;
				}
			}
		}
		else
		{
			if(fread(pixels, 3*width*height, 1, f1) != 1)
			{
				printf("Error: Could not read all pixels. File: %s\n", filename);
				free(pixels);
				fclose(f1);
				return false;
			}
		}


		// If we have reached this point, we have successfully loaded the image.

		fclose(f1);
		return true;
	}
	else
	{
		printf("Error: Coult not open file %s\n", filename);
		return false;
	}

}

// Write PPM --- Written by Jacob Strom
bool fWritePPM(const char *filename, int width, int height, unsigned char *pixels, bool reverse_y)
{
	FILE *fsave;
	fsave = fopen(filename, "wb");


	if(fsave)
	{
		int q;
		fprintf(fsave, "P6\n%d %d\n255\n", width, height);
		for(q = 0; q< height; q++)
		{
			unsigned char *adr;
			if(reverse_y) 
				adr = pixels+3*width*(height-1-q);
			else
				adr = pixels+3*width*q;
			fwrite(adr, 3*width, 1, fsave);
		}
		fclose(fsave);
		return true;
	}
	else
	{
		printf("Error: Could not open the file %s.\n",filename);
		return(false);
	}
}
