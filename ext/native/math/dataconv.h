#pragma once

#include <inttypes.h>
#include <cstring>

// Utilities useful for filling in std140-layout uniform buffers.


// LSBs in f[0], etc.
// Could be SSE optimized.
inline void Uint8x4ToFloat4(float f[4], uint32_t u) {
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = ((u >> 8) & 0xFF) * (1.0f / 255.0f);
	f[2] = ((u >> 16) & 0xFF) * (1.0f / 255.0f);
	f[3] = ((u >> 24) & 0xFF) * (1.0f / 255.0f);
}

inline void Uint8x3ToFloat4(float f[4], uint32_t u) {
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = ((u >> 8) & 0xFF) * (1.0f / 255.0f);
	f[2] = ((u >> 16) & 0xFF) * (1.0f / 255.0f);
	f[3] = 0.0f;
}

inline void Uint8x3ToInt4(int i[4], uint32_t u) {
	i[0] = ((u >> 0) & 0xFF);
	i[1] = ((u >> 8) & 0xFF);
	i[2] = ((u >> 16) & 0xFF);
	i[3] = 0;
}

inline void Uint8x3ToInt4_Alpha(int i[4], uint32_t u, uint8_t alpha) {
	i[0] = ((u >> 0) & 0xFF);
	i[1] = ((u >> 8) & 0xFF);
	i[2] = ((u >> 16) & 0xFF);
	i[3] = alpha;
}

inline void Uint8x3ToFloat4_Alpha(float f[4], uint32_t u, float alpha) {
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = ((u >> 8) & 0xFF) * (1.0f / 255.0f);
	f[2] = ((u >> 16) & 0xFF) * (1.0f / 255.0f);
	f[3] = alpha;
}

inline void Uint8x3ToFloat4_AlphaUint8(float f[4], uint32_t u, uint8_t alpha) {
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = ((u >> 8) & 0xFF) * (1.0f / 255.0f);
	f[2] = ((u >> 16) & 0xFF) * (1.0f / 255.0f);
	f[3] = alpha * (1.0f / 255.0f);
}

inline void Uint8x1ToFloat4(float f[4], uint32_t u) {
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = 0.0f;
	f[2] = 0.0f;
	f[3] = 0.0f;
}

// These are just for readability.

inline void CopyFloat2(float dest[2], const float src[2]) {
	memcpy(dest, src, sizeof(float) * 2);
}

inline void CopyFloat3(float dest[3], const float src[3]) {
	memcpy(dest, src, sizeof(float) * 3);
}

inline void CopyFloat1To4(float dest[4], const float src) {
	dest[0] = src;
	dest[1] = 0.0f;
	dest[2] = 0.0f;
	dest[3] = 0.0f;
}

inline void CopyFloat2To4(float dest[4], const float src[2]) {
	memcpy(dest, src, sizeof(float) * 2);
	dest[2] = 0.0f;
	dest[3] = 0.0f;
}

inline void CopyFloat3To4(float dest[4], const float src[3]) {
	memcpy(dest, src, sizeof(float) * 3);
	dest[3] = 0.0f;
}

inline void CopyFloat4(float dest[4], const float src[4]) {
	memcpy(dest, src, sizeof(float) * 4);
}

inline void CopyMatrix4x4(float dest[16], const float src[16]) {
	memcpy(dest, src, sizeof(float) * 16);
}

inline void ExpandFloat24x3ToFloat4(float dest[4], uint32_t src[3]) {
	uint32_t temp[4] = { src[0] << 8, src[1] << 8, src[2] << 8, 0 };
	memcpy(dest, temp, sizeof(float) * 4);
}
