#pragma once

// These are useful on JIT-less platforms - they don't beat the jitted vertex decoders by much, but they
// will beat the function-call-stitched ones by a lot.

void VtxDec_Tu16_C8888_Pfloat(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset);
void VtxDec_Tu8_C5551_Ps16(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset);

// For the UV generation modes that don't prescale.
void VtxDec_Tu16_C8888_Pfloat_NoPrescale(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset);
void VtxDec_Tu8_C5551_Ps16_NoPrescale(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset);

// Each with a version for the UV generation modes that don't prescale.
void VtxDec_Tu8_C565_Ns16_Pfloat(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset);
void VtxDec_Tu8_C565_Ns16_Pfloat_NoPrescale(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset);
void VtxDec_Tfloat_C8888_Ns8_Ps16(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset);
void VtxDec_Tfloat_C8888_Ns8_Ps16_NoPrescale(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset);
void VtxDec_Tu16_C4444_Ps16(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset);
void VtxDec_Tu16_C4444_Ps16_NoPrescale(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset);
// Skins in the decoder, so not for callerSkins.
void VtxDec_W1float_Tu8_C4444_Ns8_Pfloat(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset);
void VtxDec_W1float_Tu8_C4444_Ns8_Pfloat_NoPrescale(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset);
