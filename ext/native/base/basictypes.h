#pragma once

#include <cstdint>
#include <cstdlib>  // for byte swapping

// Byteswapping
// Just in case this has been defined by platform
#undef swap16
#undef swap32
#undef swap64

#ifdef _WIN32
inline uint16_t swap16(uint16_t _data) {return _byteswap_ushort(_data);}
inline uint32_t swap32(uint32_t _data) {return _byteswap_ulong (_data);}
inline uint64_t swap64(uint64_t _data) {return _byteswap_uint64(_data);}
#elif defined(__GNUC__)
inline uint16_t swap16(uint16_t _data) {return __builtin_bswap16(_data);}
inline uint32_t swap32(uint32_t _data) {return __builtin_bswap32(_data);}
inline uint64_t swap64(uint64_t _data) {return __builtin_bswap64(_data);}
#else
// Slow generic implementation. Hopefully this never hits
inline uint16_t swap16(uint16_t data) {return (data >> 8) | (data << 8);}
inline uint32_t swap32(uint32_t data) {return (swap16(data) << 16) | swap16(data >> 16);}
inline uint64_t swap64(uint64_t data) {return ((uint64_t)swap32(data) << 32) | swap32(data >> 32);}
#endif

inline uint16_t swap16(const uint8_t* _pData) {return swap16(*(const uint16_t*)_pData);}
inline uint32_t swap32(const uint8_t* _pData) {return swap32(*(const uint32_t*)_pData);}
inline uint64_t swap64(const uint8_t* _pData) {return swap64(*(const uint64_t*)_pData);}
