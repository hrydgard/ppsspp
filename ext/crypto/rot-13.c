/*********************************************************************
* Filename:   rot-13.c
* Author:     Brad Conte (brad AT bradconte.com)
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
* Details:    Implementation of the ROT-13 encryption algorithm.
				  Algorithm specification can be found here:
				   *
				  This implementation uses little endian byte order.
*********************************************************************/

/*************************** HEADER FILES ***************************/
#include <string.h>
#include "rot-13.h"

/*********************** FUNCTION DEFINITIONS ***********************/
void rot13(char str[])
{
   int case_type, idx, len;

   for (idx = 0, len = strlen(str); idx < len; idx++) {
      // Only process alphabetic characters.
      if (str[idx] < 'A' || (str[idx] > 'Z' && str[idx] < 'a') || str[idx] > 'z')
         continue;
      // Determine if the char is upper or lower case.
      if (str[idx] >= 'a')
         case_type = 'a';
      else
         case_type = 'A';
      // Rotate the char's value, ensuring it doesn't accidentally "fall off" the end.
      str[idx] = (str[idx] + 13) % (case_type + 26);
      if (str[idx] < 26)
         str[idx] += case_type;
   }
}
