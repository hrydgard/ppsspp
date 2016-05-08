#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "base/logging.h"
#include "image/png_load.h"
#include "image/zim_save.h"

char magic[5] = "ZIMG";

bool FLAGS_flip = false;

const char *format_strings[4] = {"8888", "4444", "565", "ETC1"};
int formats[4] = {ZIM_RGBA8888, ZIM_RGBA4444, ZIM_RGB565, ZIM_ETC1};

void printusage() {
  fprintf(stderr, "Usage: zimtool infile.png outfile.zim [-f=FORMAT] [-m] [-g]\n");
  fprintf(stderr, "Formats: 8888 4444 565 ETC1\n");
}

int filesize(const char *filename) {
  FILE *f = fopen(filename, "rb");
  fseek(f, 0, SEEK_END);
  int sz = ftell(f);
  fclose(f);
  return sz;
}

int main(int argc, char **argv) {
  // Parse command line arguments.
  const char *FLAGS_infile;
  const char *FLAGS_outfile;
  if (argc >= 3) {
    FLAGS_infile = argv[1];
    FLAGS_outfile = argv[2];
  } else {
    fprintf(stderr, "ERROR: Not enough parameters.\n");
    printusage();
    return 1;
  }

  int flags = 0;
  bool format_set = false;
  for (int i = 3; i < argc; i++) {
    if (argv[i][0] != '-') {
      fprintf(stderr, "Additional arguments must start with '-'\n");
      return 1;
    }
    switch (argv[i][1]) {
      case 'm':
        flags |= ZIM_HAS_MIPS;
        // Generates mips directly here. We can generate gamma
        // corrected mips, and mips for ETC1.
        break;
      case 'g':
        flags |= ZIM_GEN_MIPS;
        break;
      case 'c':
        flags |= ZIM_CLAMP;
        break;
      case 'f':
        {
          for (int j = 0; j < 4; j++) {
            if (!strcmp(format_strings[j], argv[i] + 3)) {
              flags |= j;
              format_set = true;
            }
          }
        }
        break;
    }
  }
	// TODO: make setting?
	flags |= ZIM_ETC1_MEDIUM;
  if ((flags & ZIM_FORMAT_MASK) == ZIM_ETC1) {
    if (flags & ZIM_GEN_MIPS) {
      fprintf(stderr, "WARNING: Cannot generate ETC1 mips at runtime\n");
      flags &= ~ZIM_GEN_MIPS;
    }
  }
  if (!format_set) {
    fprintf(stderr, "Must set format\n");
    printusage();
    return 1;
  }
  
  uint8_t *image_data;
  int width, height;
  if (1 != pngLoad(FLAGS_infile, &width, &height, &image_data, FLAGS_flip)) {
    fprintf(stderr, "Input not a PNG file\n");
    printusage();
    return 1;
  }

  SaveZIM(FLAGS_outfile, width, height, width * 4, flags, image_data);
  int in_file_size = filesize(FLAGS_infile);
  int out_file_size = filesize(FLAGS_outfile);
  fprintf(stdout, "Converted %s to %s. %i b to %i b. %ix%i, %s.\n", FLAGS_infile, FLAGS_outfile, in_file_size, out_file_size, width, height, format_strings[flags & ZIM_FORMAT_MASK]);
  return 0;
}
