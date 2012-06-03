#include <string.h>

#include "base/buffer.h"
#include "base/stringutil.h"

unsigned int parseHex(const char *_szValue)
{
	int Count, Value = 0;
	size_t Finish = strlen(_szValue);
	if (Finish > 8 ) { Finish = 8; }

	for (Count = 0; Count < Finish; Count++) {
		Value = (Value << 4);
		switch( _szValue[Count] ) {
		case '0': break;
		case '1': Value += 1; break;
		case '2': Value += 2; break;
		case '3': Value += 3; break;
		case '4': Value += 4; break;
		case '5': Value += 5; break;
		case '6': Value += 6; break;
		case '7': Value += 7; break;
		case '8': Value += 8; break;
		case '9': Value += 9; break;
		case 'A': Value += 10; break;
		case 'a': Value += 10; break;
		case 'B': Value += 11; break;
		case 'b': Value += 11; break;
		case 'C': Value += 12; break;
		case 'c': Value += 12; break;
		case 'D': Value += 13; break;
		case 'd': Value += 13; break;
		case 'E': Value += 14; break;
		case 'e': Value += 14; break;
		case 'F': Value += 15; break;
		case 'f': Value += 15; break;
		default: 
			Value = (Value >> 4);
			Count = Finish;
		}
	}
	return Value;
}

void DataToHexString(const uint8 *data, size_t size, std::string *output) {
  Buffer buffer;
  for (size_t i = 0; i < size; i++) {
    buffer.Printf("%02x ", data[i]);
    if (i && !(i & 15))
      buffer.Printf("\n");
  }
  buffer.TakeAll(output);
}