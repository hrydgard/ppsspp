#pragma once

#include <zlib.h>

bool compress_string(const std::string& str, std::string *dest, int compressionlevel = Z_BEST_COMPRESSION);
bool decompress_string(const std::string& str, std::string *dest);


// Delta encoding/decoding - many formats benefit from a pass of this before zlibbing.
// WARNING : Do not use these with floating point data, especially not float16...

template <class T>
inline void delta(T *data, int length) {
	T prev = data[0];
	for (int i = 1; i < length; i++) {
		T temp = data[i] - prev;
		prev = data[i];
		data[i] = temp;
	}
}

template <class T>
inline void dedelta(T *data, int length) {
	for (int i = 1; i < length; i++) {
		data[i] += data[i - 1];
	}
}

