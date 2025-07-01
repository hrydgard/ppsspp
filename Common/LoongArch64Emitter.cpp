// Copyright (c) 2025- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.


#include "ppsspp_config.h"
#include <algorithm>
#include <cstring>
#include "Common/BitScan.h"
#include "Common/CPUDetect.h"
#include "Common/LoongArch64Emitter.h"

namespace LoongArch64Gen {

enum class Opcode32 {
    // Note: invalid, just used for FixupBranch.
    ZERO = 0x0,

    ADD_W = 0x00100000,
    ADD_D = 0x00108000,
    SUB_W = 0x00110000,
    SUB_D = 0x00118000,

    ADDI_W = 0x02800000,
    ADDI_D = 0x02c00000,
    ADDU16I_D = 0x10000000,

    ALSL_W = 0x00040000,
    ALSL_D = 0X002c0000,
    ALSL_WU = 0x00060000,

    LU12I_W = 0x14000000,
    LU32I_D = 0x16000000,
    LU52I_D = 0x03000000,

    SLT = 0x00120000,
    SLTU = 0x00128000,

    SLTI = 0x02000000,
    SLTUI = 0x02400000,

    PCADDI = 0x18000000,
    PCADDU12I = 0x1c000000,
    PCADDU18I = 0x1e000000,
    PCALAU12I = 0x1a000000,

    AND = 0x00148000,
    OR = 0x00150000,
    NOR = 0x00140000,
    XOR = 0x00158000,
    ANDN = 0x00168000,
    ORN = 0x00160000,

    ANDI = 0x03400000,
    ORI = 0x03800000,
    XORI = 0x03c00000,

    MUL_W = 0x001c0000,
    MULH_W = 0x001c8000,
    MULH_WU = 0x001d0000,
    MUL_D = 0x001d8000,
    MULH_D = 0x001e0000,
    MULH_DU = 0x001e8000,

    MULW_D_W = 0x001f0000,
    MULW_D_WU = 0x001f8000,

    DIV_W = 0x00200000,
    MOD_W = 0x00208000,
    DIV_WU = 0x00210000,
    MOD_WU = 0x00218000,
    DIV_D = 0x00220000,
    MOD_D = 0x00228000,
    DIV_DU = 0x00230000,
    MOD_DU = 0x00238000,

    SLL_W = 0x00170000,
    SRL_W = 0x00178000,
    SRA_W = 0x00180000,
    ROTR_W = 0x001b0000,

    SLLI_W = 0x00408000,
    SRLI_W = 0x00448000,
    SRAI_W = 0x00488000,
    ROTRI_W = 0x004c8000,

    SLL_D = 0x00188000,
    SRL_D = 0x00190000,
    SRA_D = 0x00198000,
    ROTR_D = 0x001b8000,

    SLLI_D = 0x00410000,
    SRLI_D = 0x00450000,
    SRAI_D = 0x00490000,
    ROTRI_D = 0x004d0000,

    EXT_W_B = 0x00005c00,
    EXT_W_H = 0x00005800,

    CLO_W = 0x00001000,
    CLO_D = 0x00002000,
    CLZ_W = 0x00001400,
    CLZ_D = 0x00002400,
    CTO_W = 0x00001800,
    CTO_D = 0x00002800,
    CTZ_W = 0x00001c00,
    CTZ_D = 0x00002c00,

    BYTEPICK_W = 0x00080000,
    BYTEPICK_D = 0x000c0000,

    REVB_2H = 0x00003000,
    REVB_4H = 0x00003400,
    REVB_2W = 0x00003800,
    REVB_D = 0x00003c00,

    BITREV_4B = 0x00004800,
    BITREV_8B = 0x00004c00,

    BITREV_W = 0x00005000,
    BITREV_D = 0x00005400,

    BSTRINS_W = 0x00600000,
    BSTRINS_D = 0x00800000,

    BSTRPICK_W = 0x00608000,
    BSTRPICK_D = 0x00c00000,

    MASKEQZ = 0x00130000,
    MASKNEZ = 0x00138000,

    BEQ = 0x58000000,
    BNE = 0x5c000000,
    BLT = 0x60000000,
    BGE = 0x64000000,
    BLTU = 0x68000000,
    BGEU = 0x6c000000,

    BEQZ = 0x40000000,
    BNEZ = 0x44000000,

    B = 0x50000000,
    BL = 0x54000000,

    JIRL = 0x4c000000,

    LD_B = 0x28000000,
    LD_H = 0x28400000,
    LD_W = 0x28800000,
    LD_D = 0x28c00000,
    LD_BU = 0x2a000000,
    LD_HU = 0x2a400000,
    LD_WU = 0x2a800000,
    ST_B = 0x29000000,
    ST_H = 0x29400000,
    ST_W = 0x29800000,
    ST_D = 0x29c00000,

    LDX_B = 0x38000000,
    LDX_H = 0x38040000,
    LDX_W = 0x38080000,
    LDX_D = 0x380c0000,
    LDX_BU = 0x38200000,
    LDX_HU = 0x38240000,
    LDX_WU = 0x38280000,
    STX_B = 0x38100000,
    STX_H = 0x38140000,
    STX_W = 0x38180000,
    STX_D = 0x381c0000,

    LDPTR_W = 0x24000000,
    LDPTR_D = 0x26000000,
    STPTR_W = 0x25000000,
    STPTR_D = 0x27000000,

    PRELD = 0x2ac00000,
    PRELDX = 0x382c0000,

    LDGT_B = 0x38780000,
    LDGT_H = 0x38788000,
    LDGT_W = 0x38790000,
    LDGT_D = 0x38798000,
    LDLE_B = 0x387a0000,
    LDLE_H = 0x387a8000,
    LDLE_W = 0x387b0000,
    LDLE_D = 0x387b8000,
    STGT_B = 0x387c0000,
    STGT_H = 0x387c8000,
    STGT_W = 0x387d0000,
    STGT_D = 0x387d8000,
    STLE_B = 0x387e0000,
    STLE_H = 0x387e8000,
    STLE_W = 0x387f0000,
    STLE_D = 0x387f8000,

    AMSWAP_W = 0x38600000,
    AMSWAP_DB_W = 0x38690000,
    AMSWAP_D = 0x38608000,
    AMSWAP_DB_D = 0x38698000,

    AMADD_W = 0x38610000,
    AMADD_DB_W = 0x386a0000,
    AMADD_D = 0x38618000,
    AMADD_DB_D = 0x386a8000,

    AMAND_W = 0x38620000,
    AMAND_DB_W = 0x386b0000,
    AMAND_D = 0x38628000,
    AMAND_DB_D = 0x386b8000,

    AMOR_W = 0x38630000,
    AMOR_DB_W = 0x386c0000,
    AMOR_D = 0x38638000,
    AMOR_DB_D = 0x386c8000,

    AMXOR_W = 0x38640000,
    AMXOR_DB_W = 0x386d0000,
    AMXOR_D = 0x38648000,
    AMXOR_DB_D = 0x386d8000,

    AMMAX_W = 0x38650000,
    AMMAX_DB_W = 0x386e0000,
    AMMAX_D = 0x38658000,
    AMMAX_DB_D = 0x386e8000,

    AMMIN_W = 0x38660000,
    AMMIN_DB_W = 0x386f0000,
    AMMIN_D = 0x38668000,
    AMMIN_DB_D = 0x386f8000,

    AMMAX_WU = 0x38670000,
    AMMAX_DB_WU = 0x38700000,
    AMMAX_DU = 0x38678000,
    AMMAX_DB_DU = 0x38708000,

    AMMIN_WU = 0x38680000,
    AMMIN_DB_WU = 0x38710000,
    AMMIN_DU = 0x38688000,
    AMMIN_DB_DU = 0x38718000,

    AMSWAP_B = 0x385c0000,
    AMSWAP_DB_B = 0x385e0000,
    AMSWAP_H = 0x385c8000,
    AMSWAP_DB_H = 0x385e8000,

    AMADD_B = 0x385d0000,
    AMADD_DB_B = 0x385f0000,
    AMADD_H = 0x385d8000,
    AMADD_DB_H = 0x385f8000,

    AMCAS_B = 0x38580000,
    AMCAS_DB_B = 0x385a0000,
    AMCAS_H = 0x38588000,
    AMCAS_DB_H = 0x385a8000,
    AMCAS_W = 0x38590000,
    AMCAS_DB_W = 0x385b0000,
    AMCAS_D = 0x38598000,
    AMCAS_DB_D = 0x38698000,

    CRC_W_B_W = 0x00240000,
    CRC_W_H_W = 0x00248000,
    CRC_W_W_W = 0x00250000,
    CRC_W_D_W = 0x00258000,
    CRCC_W_B_W = 0x00260000,
    CRCC_W_H_W = 0x00268000,
    CRCC_W_W_W = 0x00270000,
    CRCC_W_D_W = 0x00278000,

    SYSCALL = 0x002b0000,
    BREAK = 0x002a0000,

    ASRTLE_D = 0x00010000,
    ASRTGT_D = 0x00018000,

    RDTIMEL_W = 0x00006000,
    RDTIMEH_W = 0x00006400,
    RDTIME_D = 0x00006800,

    CPUCFG = 0x00006c00,

    FADD_S = 0x01008000,
    FADD_D = 0x01010000,
    FSUB_S = 0x01028000,
    FSUB_D = 0x01030000,
    FMUL_S = 0x01048000,
    FMUL_D = 0x01050000,
    FDIV_S = 0x01068000,
    FDIV_D = 0x01070000,

    FMADD_S = 0x08100000,
    FMADD_D = 0x08200000,
    FMSUB_S = 0x08500000,
    FMSUB_D = 0x08600000,
    FNMADD_S= 0x08900000,
    FNMADD_D= 0x08a00000,
    FNMSUB_S= 0x08d00000,
    FNMSUB_D= 0x08e00000,

    FMAX_S = 0x01088000,
    FMAX_D = 0x01090000,
    FMIN_S = 0x010a8000,
    FMIN_D = 0x010b0000,

    FMAXA_S = 0x010c8000,
    FMAXA_D = 0x010d0000,
    FMINA_S = 0x010e8000,
    FMINA_D = 0x010f0000,

    FABS_S = 0x01140400,
    FABS_D = 0x01140800,
    FNEG_S = 0x01141400,
    FNEG_D = 0x01141800,

    FSQRT_S = 0x01144400,
    FSQRT_D = 0x01144800,
    FRECIP_S = 0x01145400,
    FRECIP_D = 0x01145800,
    FRSQRT_S = 0x01146400,
    FRSQRT_D = 0x01146800,

    FSCALEB_S = 0x01108000,
    FSCALEB_D = 0x01110000,
    FLOGB_S = 0x01142400,
    FLOGB_D = 0x01142800,
    FCOPYSIGN_S = 0x01128000,
    FCOPYSIGN_D = 0x01130000,

    FCLASS_S = 0x01143400,
    FCLASS_D = 0x01143800,
    FRECIPE_S = 0x01147400,
    FRECIPE_D = 0x01147800,
    FRSQRTE_S = 0x01148400,
    FRSQRTE_D = 0x01148800,

    FCMP_COND_S = 0x0c100000,
    FCMP_COND_D = 0x0c200000,

    FCVT_S_D = 0x01191800,
    FCVT_D_S = 0x01192400,

    FFINT_S_W = 0x011d1000,
    FFINT_S_L = 0x011d1800,
    FFINT_D_W = 0x011d2000,
    FFINT_D_L = 0x011d2800,
    FTINT_W_S = 0x011b0400,
    FTINT_W_D = 0x011b0800,
    FTINT_L_S = 0x011b2400,
    FTINT_L_D = 0x011b2800,

    FTINTRM_W_S = 0x011a0400,
    FTINTRM_W_D = 0x011a0800,
    FTINTRM_L_S = 0x011a2400,
    FTINTRM_L_D = 0x011a2800,
    FTINTRP_W_S = 0x011a4400,
    FTINTRP_W_D = 0x011a4800,
    FTINTRP_L_S = 0x011a6400,
    FTINTRP_L_D = 0x011a6800,
    FTINTRZ_W_S = 0x011a8400,
    FTINTRZ_W_D = 0x011a8800,
    FTINTRZ_L_S = 0x011aa400,
    FTINTRZ_L_D = 0x011aa800,
    FTINTRNE_W_S = 0x011ac400,
    FTINTRNE_W_D = 0x011ac800,
    FTINTRNE_L_S = 0x011ae400,
    FTINTRNE_L_D = 0x011ae800,

    FRINT_S = 0x011e4400,
    FRINT_D = 0x011e4800,

    FMOV_S = 0x01149400,
    FMOV_D = 0x01149800,

    FSEL = 0x0d000000,

    MOVGR2FR_W = 0x0114a400,
    MOVGR2FR_D = 0x00114a800,
    MOVGR2FRH_W = 0x0114ac00,

    MOVFR2GR_S = 0x0114b400,
    MOVFR2GR_D = 0x0114b800,
    MOVFRH2GR_S = 0x0114bc00,

    MOVGR2FCSR = 0x0114c000,
    MOVFCSR2GR = 0x0114c800,

    MOVFR2CF = 0x0114d000,
    MOVCF2FR = 0x0114d400,

    MOVGR2CF = 0x0114d800,
    MOVCF2GR = 0x0114dc00,

    BCEQZ = 0x48000000,
    BCNEZ = 0x48000100,

    FLD_S = 0x2b000000,
    FLD_D = 0x2b800000,
    FST_S = 0x2b400000,
    FST_D = 0x2bc00000,

    FLDX_S = 0x38300000,
    FLDX_D = 0x38340000,
    FSTX_S = 0x38380000,
    FSTX_D = 0x383c0000,

    FLDGT_S = 0x38740000,
    FLDGT_D = 0x38748000,
    FLDLE_S = 0x38750000,
    FLDLE_D = 0x38758000,
    FSTGT_S = 0x38760000,
    FSTGT_D = 0x38768000,
    FSTLE_S = 0x38770000,
    FSTLE_D = 0x38778000,

    VFMADD_S = 0x09100000,
    VFMADD_D = 0x09200000,
    VFMSUB_S = 0x09500000,
    VFMSUB_D = 0x09600000,
    VFNMADD_S = 0x09900000,
    VFNMADD_D = 0x09a00000,
    VFNMSUB_S = 0x09d00000,
    VFNMSUB_D = 0x09e00000,
    VFCMP_CAF_S = 0x0c500000,
    VFCMP_SAF_S = 0x0c508000,
    VFCMP_CLT_S = 0x0c510000,
    VFCMP_SLT_S = 0x0c518000,
    VFCMP_CEQ_S = 0x0c520000,
    VFCMP_SEQ_S = 0x0c528000,
    VFCMP_CLE_S = 0x0c530000,
    VFCMP_SLE_S = 0x0c538000,
    VFCMP_CUN_S = 0x0c540000,
    VFCMP_SUN_S = 0x0c548000,
    VFCMP_CULT_S = 0x0c550000,
    VFCMP_SULT_S = 0x0c558000,
    VFCMP_CUEQ_S = 0x0c560000,
    VFCMP_SUEQ_S = 0x0c568000,
    VFCMP_CULE_S = 0x0c570000,
    VFCMP_SULE_S = 0x0c578000,
    VFCMP_CNE_S = 0x0c580000,
    VFCMP_SNE_S = 0x0c588000,
    VFCMP_COR_S = 0x0c5a0000,
    VFCMP_SOR_S = 0x0c5a8000,
    VFCMP_CUNE_S = 0x0c5c0000,
    VFCMP_SUNE_S = 0x0c5c8000,
    VFCMP_CAF_D = 0x0c600000,
    VFCMP_SAF_D = 0x0c608000,
    VFCMP_CLT_D = 0x0c610000,
    VFCMP_SLT_D = 0x0c618000,
    VFCMP_CEQ_D = 0x0c620000,
    VFCMP_SEQ_D = 0x0c628000,
    VFCMP_CLE_D = 0x0c630000,
    VFCMP_SLE_D = 0x0c638000,
    VFCMP_CUN_D = 0x0c640000,
    VFCMP_SUN_D = 0x0c648000,
    VFCMP_CULT_D = 0x0c650000,
    VFCMP_SULT_D = 0x0c658000,
    VFCMP_CUEQ_D = 0x0c660000,
    VFCMP_SUEQ_D = 0x0c668000,
    VFCMP_CULE_D = 0x0c670000,
    VFCMP_SULE_D = 0x0c678000,
    VFCMP_CNE_D = 0x0c680000,
    VFCMP_SNE_D = 0x0c688000,
    VFCMP_COR_D = 0x0c6a0000,
    VFCMP_SOR_D = 0x0c6a8000,
    VFCMP_CUNE_D = 0x0c6c0000,
    VFCMP_SUNE_D = 0x0c6c8000,
    VBITSEL_V = 0x0d100000,
    VSHUF_B = 0x0d500000,
    VLD = 0x2c000000,
    VST = 0x2c400000,
    VLDREPL_D = 0x30100000,
    VLDREPL_W = 0x30200000,
    VLDREPL_H = 0x30400000,
    VLDREPL_B = 0x30800000,
    VSTELM_D = 0x31100000,
    VSTELM_W = 0x31200000,
    VSTELM_H = 0x31400000,
    VSTELM_B = 0x31800000,
    VLDX = 0x38400000,
    VSTX = 0x38440000,
    VSEQ_B = 0x70000000,
    VSEQ_H = 0x70008000,
    VSEQ_W = 0x70010000,
    VSEQ_D = 0x70018000,
    VSLE_B = 0x70020000,
    VSLE_H = 0x70028000,
    VSLE_W = 0x70030000,
    VSLE_D = 0x70038000,
    VSLE_BU = 0x70040000,
    VSLE_HU = 0x70048000,
    VSLE_WU = 0x70050000,
    VSLE_DU = 0x70058000,
    VSLT_B = 0x70060000,
    VSLT_H = 0x70068000,
    VSLT_W = 0x70070000,
    VSLT_D = 0x70078000,
    VSLT_BU = 0x70080000,
    VSLT_HU = 0x70088000,
    VSLT_WU = 0x70090000,
    VSLT_DU = 0x70098000,
    VADD_B = 0x700a0000,
    VADD_H = 0x700a8000,
    VADD_W = 0x700b0000,
    VADD_D = 0x700b8000,
    VSUB_B = 0x700c0000,
    VSUB_H = 0x700c8000,
    VSUB_W = 0x700d0000,
    VSUB_D = 0x700d8000,
    VADDWEV_H_B = 0x701e0000,
    VADDWEV_W_H = 0x701e8000,
    VADDWEV_D_W = 0x701f0000,
    VADDWEV_Q_D = 0x701f8000,
    VSUBWEV_H_B = 0x70200000,
    VSUBWEV_W_H = 0x70208000,
    VSUBWEV_D_W = 0x70210000,
    VSUBWEV_Q_D = 0x70218000,
    VADDWOD_H_B = 0x70220000,
    VADDWOD_W_H = 0x70228000,
    VADDWOD_D_W = 0x70230000,
    VADDWOD_Q_D = 0x70238000,
    VSUBWOD_H_B = 0x70240000,
    VSUBWOD_W_H = 0x70248000,
    VSUBWOD_D_W = 0x70250000,
    VSUBWOD_Q_D = 0x70258000,
    VADDWEV_H_BU = 0x702e0000,
    VADDWEV_W_HU = 0x702e8000,
    VADDWEV_D_WU = 0x702f0000,
    VADDWEV_Q_DU = 0x702f8000,
    VSUBWEV_H_BU = 0x70300000,
    VSUBWEV_W_HU = 0x70308000,
    VSUBWEV_D_WU = 0x70310000,
    VSUBWEV_Q_DU = 0x70318000,
    VADDWOD_H_BU = 0x70320000,
    VADDWOD_W_HU = 0x70328000,
    VADDWOD_D_WU = 0x70330000,
    VADDWOD_Q_DU = 0x70338000,
    VSUBWOD_H_BU = 0x70340000,
    VSUBWOD_W_HU = 0x70348000,
    VSUBWOD_D_WU = 0x70350000,
    VSUBWOD_Q_DU = 0x70358000,
    VADDWEV_H_BU_B = 0x703e0000,
    VADDWEV_W_HU_H = 0x703e8000,
    VADDWEV_D_WU_W = 0x703f0000,
    VADDWEV_Q_DU_D = 0x703f8000,
    VADDWOD_H_BU_B = 0x70400000,
    VADDWOD_W_HU_H = 0x70408000,
    VADDWOD_D_WU_W = 0x70410000,
    VADDWOD_Q_DU_D = 0x70418000,
    VSADD_B = 0x70460000,
    VSADD_H = 0x70468000,
    VSADD_W = 0x70470000,
    VSADD_D = 0x70478000,
    VSSUB_B = 0x70480000,
    VSSUB_H = 0x70488000,
    VSSUB_W = 0x70490000,
    VSSUB_D = 0x70498000,
    VSADD_BU = 0x704a0000,
    VSADD_HU = 0x704a8000,
    VSADD_WU = 0x704b0000,
    VSADD_DU = 0x704b8000,
    VSSUB_BU = 0x704c0000,
    VSSUB_HU = 0x704c8000,
    VSSUB_WU = 0x704d0000,
    VSSUB_DU = 0x704d8000,
    VHADDW_H_B = 0x70540000,
    VHADDW_W_H = 0x70548000,
    VHADDW_D_W = 0x70550000,
    VHADDW_Q_D = 0x70558000,
    VHSUBW_H_B = 0x70560000,
    VHSUBW_W_H = 0x70568000,
    VHSUBW_D_W = 0x70570000,
    VHSUBW_Q_D = 0x70578000,
    VHADDW_HU_BU = 0x70580000,
    VHADDW_WU_HU = 0x70588000,
    VHADDW_DU_WU = 0x70590000,
    VHADDW_QU_DU = 0x70598000,
    VHSUBW_HU_BU = 0x705a0000,
    VHSUBW_WU_HU = 0x705a8000,
    VHSUBW_DU_WU = 0x705b0000,
    VHSUBW_QU_DU = 0x705b8000,
    VADDA_B = 0x705c0000,
    VADDA_H = 0x705c8000,
    VADDA_W = 0x705d0000,
    VADDA_D = 0x705d8000,
    VABSD_B = 0x70600000,
    VABSD_H = 0x70608000,
    VABSD_W = 0x70610000,
    VABSD_D = 0x70618000,
    VABSD_BU = 0x70620000,
    VABSD_HU = 0x70628000,
    VABSD_WU = 0x70630000,
    VABSD_DU = 0x70638000,
    VAVG_B = 0x70640000,
    VAVG_H = 0x70648000,
    VAVG_W = 0x70650000,
    VAVG_D = 0x70658000,
    VAVG_BU = 0x70660000,
    VAVG_HU = 0x70668000,
    VAVG_WU = 0x70670000,
    VAVG_DU = 0x70678000,
    VAVGR_B = 0x70680000,
    VAVGR_H = 0x70688000,
    VAVGR_W = 0x70690000,
    VAVGR_D = 0x70698000,
    VAVGR_BU = 0x706a0000,
    VAVGR_HU = 0x706a8000,
    VAVGR_WU = 0x706b0000,
    VAVGR_DU = 0x706b8000,
    VMAX_B = 0x70700000,
    VMAX_H = 0x70708000,
    VMAX_W = 0x70710000,
    VMAX_D = 0x70718000,
    VMIN_B = 0x70720000,
    VMIN_H = 0x70728000,
    VMIN_W = 0x70730000,
    VMIN_D = 0x70738000,
    VMAX_BU = 0x70740000,
    VMAX_HU = 0x70748000,
    VMAX_WU = 0x70750000,
    VMAX_DU = 0x70758000,
    VMIN_BU = 0x70760000,
    VMIN_HU = 0x70768000,
    VMIN_WU = 0x70770000,
    VMIN_DU = 0x70778000,
    VMUL_B = 0x70840000,
    VMUL_H = 0x70848000,
    VMUL_W = 0x70850000,
    VMUL_D = 0x70858000,
    VMUH_B = 0x70860000,
    VMUH_H = 0x70868000,
    VMUH_W = 0x70870000,
    VMUH_D = 0x70878000,
    VMUH_BU = 0x70880000,
    VMUH_HU = 0x70888000,
    VMUH_WU = 0x70890000,
    VMUH_DU = 0x70898000,
    VMULWEV_H_B = 0x70900000,
    VMULWEV_W_H = 0x70908000,
    VMULWEV_D_W = 0x70910000,
    VMULWEV_Q_D = 0x70918000,
    VMULWOD_H_B = 0x70920000,
    VMULWOD_W_H = 0x70928000,
    VMULWOD_D_W = 0x70930000,
    VMULWOD_Q_D = 0x70938000,
    VMULWEV_H_BU = 0x70980000,
    VMULWEV_W_HU = 0x70988000,
    VMULWEV_D_WU = 0x70990000,
    VMULWEV_Q_DU = 0x70998000,
    VMULWOD_H_BU = 0x709a0000,
    VMULWOD_W_HU = 0x709a8000,
    VMULWOD_D_WU = 0x709b0000,
    VMULWOD_Q_DU = 0x709b8000,
    VMULWEV_H_BU_B = 0x70a00000,
    VMULWEV_W_HU_H = 0x70a08000,
    VMULWEV_D_WU_W = 0x70a10000,
    VMULWEV_Q_DU_D = 0x70a18000,
    VMULWOD_H_BU_B = 0x70a20000,
    VMULWOD_W_HU_H = 0x70a28000,
    VMULWOD_D_WU_W = 0x70a30000,
    VMULWOD_Q_DU_D = 0x70a38000,
    VMADD_B = 0x70a80000,
    VMADD_H = 0x70a88000,
    VMADD_W = 0x70a90000,
    VMADD_D = 0x70a98000,
    VMSUB_B = 0x70aa0000,
    VMSUB_H = 0x70aa8000,
    VMSUB_W = 0x70ab0000,
    VMSUB_D = 0x70ab8000,
    VMADDWEV_H_B = 0x70ac0000,
    VMADDWEV_W_H = 0x70ac8000,
    VMADDWEV_D_W = 0x70ad0000,
    VMADDWEV_Q_D = 0x70ad8000,
    VMADDWOD_H_B = 0x70ae0000,
    VMADDWOD_W_H = 0x70ae8000,
    VMADDWOD_D_W = 0x70af0000,
    VMADDWOD_Q_D = 0x70af8000,
    VMADDWEV_H_BU = 0x70b40000,
    VMADDWEV_W_HU = 0x70b48000,
    VMADDWEV_D_WU = 0x70b50000,
    VMADDWEV_Q_DU = 0x70b58000,
    VMADDWOD_H_BU = 0x70b60000,
    VMADDWOD_W_HU = 0x70b68000,
    VMADDWOD_D_WU = 0x70b70000,
    VMADDWOD_Q_DU = 0x70b78000,
    VMADDWEV_H_BU_B = 0x70bc0000,
    VMADDWEV_W_HU_H = 0x70bc8000,
    VMADDWEV_D_WU_W = 0x70bd0000,
    VMADDWEV_Q_DU_D = 0x70bd8000,
    VMADDWOD_H_BU_B = 0x70be0000,
    VMADDWOD_W_HU_H = 0x70be8000,
    VMADDWOD_D_WU_W = 0x70bf0000,
    VMADDWOD_Q_DU_D = 0x70bf8000,
    VDIV_B = 0x70e00000,
    VDIV_H = 0x70e08000,
    VDIV_W = 0x70e10000,
    VDIV_D = 0x70e18000,
    VMOD_B = 0x70e20000,
    VMOD_H = 0x70e28000,
    VMOD_W = 0x70e30000,
    VMOD_D = 0x70e38000,
    VDIV_BU = 0x70e40000,
    VDIV_HU = 0x70e48000,
    VDIV_WU = 0x70e50000,
    VDIV_DU = 0x70e58000,
    VMOD_BU = 0x70e60000,
    VMOD_HU = 0x70e68000,
    VMOD_WU = 0x70e70000,
    VMOD_DU = 0x70e78000,
    VSLL_B = 0x70e80000,
    VSLL_H = 0x70e88000,
    VSLL_W = 0x70e90000,
    VSLL_D = 0x70e98000,
    VSRL_B = 0x70ea0000,
    VSRL_H = 0x70ea8000,
    VSRL_W = 0x70eb0000,
    VSRL_D = 0x70eb8000,
    VSRA_B = 0x70ec0000,
    VSRA_H = 0x70ec8000,
    VSRA_W = 0x70ed0000,
    VSRA_D = 0x70ed8000,
    VROTR_B = 0x70ee0000,
    VROTR_H = 0x70ee8000,
    VROTR_W = 0x70ef0000,
    VROTR_D = 0x70ef8000,
    VSRLR_B = 0x70f00000,
    VSRLR_H = 0x70f08000,
    VSRLR_W = 0x70f10000,
    VSRLR_D = 0x70f18000,
    VSRAR_B = 0x70f20000,
    VSRAR_H = 0x70f28000,
    VSRAR_W = 0x70f30000,
    VSRAR_D = 0x70f38000,
    VSRLN_B_H = 0x70f48000,
    VSRLN_H_W = 0x70f50000,
    VSRLN_W_D = 0x70f58000,
    VSRAN_B_H = 0x70f68000,
    VSRAN_H_W = 0x70f70000,
    VSRAN_W_D = 0x70f78000,
    VSRLRN_B_H = 0x70f88000,
    VSRLRN_H_W = 0x70f90000,
    VSRLRN_W_D = 0x70f98000,
    VSRARN_B_H = 0x70fa8000,
    VSRARN_H_W = 0x70fb0000,
    VSRARN_W_D = 0x70fb8000,
    VSSRLN_B_H = 0x70fc8000,
    VSSRLN_H_W = 0x70fd0000,
    VSSRLN_W_D = 0x70fd8000,
    VSSRAN_B_H = 0x70fe8000,
    VSSRAN_H_W = 0x70ff0000,
    VSSRAN_W_D = 0x70ff8000,
    VSSRLRN_B_H = 0x71008000,
    VSSRLRN_H_W = 0x71010000,
    VSSRLRN_W_D = 0x71018000,
    VSSRARN_B_H = 0x71028000,
    VSSRARN_H_W = 0x71030000,
    VSSRARN_W_D = 0x71038000,
    VSSRLN_BU_H = 0x71048000,
    VSSRLN_HU_W = 0x71050000,
    VSSRLN_WU_D = 0x71058000,
    VSSRAN_BU_H = 0x71068000,
    VSSRAN_HU_W = 0x71070000,
    VSSRAN_WU_D = 0x71078000,
    VSSRLRN_BU_H = 0x71088000,
    VSSRLRN_HU_W = 0x71090000,
    VSSRLRN_WU_D = 0x71098000,
    VSSRARN_BU_H = 0x710a8000,
    VSSRARN_HU_W = 0x710b0000,
    VSSRARN_WU_D = 0x710b8000,
    VBITCLR_B = 0x710c0000,
    VBITCLR_H = 0x710c8000,
    VBITCLR_W = 0x710d0000,
    VBITCLR_D = 0x710d8000,
    VBITSET_B = 0x710e0000,
    VBITSET_H = 0x710e8000,
    VBITSET_W = 0x710f0000,
    VBITSET_D = 0x710f8000,
    VBITREV_B = 0x71100000,
    VBITREV_H = 0x71108000,
    VBITREV_W = 0x71110000,
    VBITREV_D = 0x71118000,
    VPACKEV_B = 0x71160000,
    VPACKEV_H = 0x71168000,
    VPACKEV_W = 0x71170000,
    VPACKEV_D = 0x71178000,
    VPACKOD_B = 0x71180000,
    VPACKOD_H = 0x71188000,
    VPACKOD_W = 0x71190000,
    VPACKOD_D = 0x71198000,
    VILVL_B = 0x711a0000,
    VILVL_H = 0x711a8000,
    VILVL_W = 0x711b0000,
    VILVL_D = 0x711b8000,
    VILVH_B = 0x711c0000,
    VILVH_H = 0x711c8000,
    VILVH_W = 0x711d0000,
    VILVH_D = 0x711d8000,
    VPICKEV_B = 0x711e0000,
    VPICKEV_H = 0x711e8000,
    VPICKEV_W = 0x711f0000,
    VPICKEV_D = 0x711f8000,
    VPICKOD_B = 0x71200000,
    VPICKOD_H = 0x71208000,
    VPICKOD_W = 0x71210000,
    VPICKOD_D = 0x71218000,
    VREPLVE_B = 0x71220000,
    VREPLVE_H = 0x71228000,
    VREPLVE_W = 0x71230000,
    VREPLVE_D = 0x71238000,
    VAND_V = 0x71260000,
    VOR_V = 0x71268000,
    VXOR_V = 0x71270000,
    VNOR_V = 0x71278000,
    VANDN_V = 0x71280000,
    VORN_V = 0x71288000,
    VFRSTP_B = 0x712b0000,
    VFRSTP_H = 0x712b8000,
    VADD_Q = 0x712d0000,
    VSUB_Q = 0x712d8000,
    VSIGNCOV_B = 0x712e0000,
    VSIGNCOV_H = 0x712e8000,
    VSIGNCOV_W = 0x712f0000,
    VSIGNCOV_D = 0x712f8000,
    VFADD_S = 0x71308000,
    VFADD_D = 0x71310000,
    VFSUB_S = 0x71328000,
    VFSUB_D = 0x71330000,
    VFMUL_S = 0x71388000,
    VFMUL_D = 0x71390000,
    VFDIV_S = 0x713a8000,
    VFDIV_D = 0x713b0000,
    VFMAX_S = 0x713c8000,
    VFMAX_D = 0x713d0000,
    VFMIN_S = 0x713e8000,
    VFMIN_D = 0x713f0000,
    VFMAXA_S = 0x71408000,
    VFMAXA_D = 0x71410000,
    VFMINA_S = 0x71428000,
    VFMINA_D = 0x71430000,
    VFCVT_H_S = 0x71460000,
    VFCVT_S_D = 0x71468000,
    VFFINT_S_L = 0x71480000,
    VFTINT_W_D = 0x71498000,
    VFTINTRM_W_D = 0x714a0000,
    VFTINTRP_W_D = 0x714a8000,
    VFTINTRZ_W_D = 0x714b0000,
    VFTINTRNE_W_D = 0x714b8000,
    VSHUF_H = 0x717a8000,
    VSHUF_W = 0x717b0000,
    VSHUF_D = 0x717b8000,
    VSEQI_B = 0x72800000,
    VSEQI_H = 0x72808000,
    VSEQI_W = 0x72810000,
    VSEQI_D = 0x72818000,
    VSLEI_B = 0x72820000,
    VSLEI_H = 0x72828000,
    VSLEI_W = 0x72830000,
    VSLEI_D = 0x72838000,
    VSLEI_BU = 0x72840000,
    VSLEI_HU = 0x72848000,
    VSLEI_WU = 0x72850000,
    VSLEI_DU = 0x72858000,
    VSLTI_B = 0x72860000,
    VSLTI_H = 0x72868000,
    VSLTI_W = 0x72870000,
    VSLTI_D = 0x72878000,
    VSLTI_BU = 0x72880000,
    VSLTI_HU = 0x72888000,
    VSLTI_WU = 0x72890000,
    VSLTI_DU = 0x72898000,
    VADDI_BU = 0x728a0000,
    VADDI_HU = 0x728a8000,
    VADDI_WU = 0x728b0000,
    VADDI_DU = 0x728b8000,
    VSUBI_BU = 0x728c0000,
    VSUBI_HU = 0x728c8000,
    VSUBI_WU = 0x728d0000,
    VSUBI_DU = 0x728d8000,
    VBSLL_V = 0x728e0000,
    VBSRL_V = 0x728e8000,
    VMAXI_B = 0x72900000,
    VMAXI_H = 0x72908000,
    VMAXI_W = 0x72910000,
    VMAXI_D = 0x72918000,
    VMINI_B = 0x72920000,
    VMINI_H = 0x72928000,
    VMINI_W = 0x72930000,
    VMINI_D = 0x72938000,
    VMAXI_BU = 0x72940000,
    VMAXI_HU = 0x72948000,
    VMAXI_WU = 0x72950000,
    VMAXI_DU = 0x72958000,
    VMINI_BU = 0x72960000,
    VMINI_HU = 0x72968000,
    VMINI_WU = 0x72970000,
    VMINI_DU = 0x72978000,
    VFRSTPI_B = 0x729a0000,
    VFRSTPI_H = 0x729a8000,
    VCLO_B = 0x729c0000,
    VCLO_H = 0x729c0400,
    VCLO_W = 0x729c0800,
    VCLO_D = 0x729c0c00,
    VCLZ_B = 0x729c1000,
    VCLZ_H = 0x729c1400,
    VCLZ_W = 0x729c1800,
    VCLZ_D = 0x729c1c00,
    VPCNT_B = 0x729c2000,
    VPCNT_H = 0x729c2400,
    VPCNT_W = 0x729c2800,
    VPCNT_D = 0x729c2c00,
    VNEG_B = 0x729c3000,
    VNEG_H = 0x729c3400,
    VNEG_W = 0x729c3800,
    VNEG_D = 0x729c3c00,
    VMSKLTZ_B = 0x729c4000,
    VMSKLTZ_H = 0x729c4400,
    VMSKLTZ_W = 0x729c4800,
    VMSKLTZ_D = 0x729c4c00,
    VMSKGEZ_B = 0x729c5000,
    VMSKNZ_B = 0x729c6000,
    VSETEQZ_V = 0x729c9800,
    VSETNEZ_V = 0x729c9c00,
    VSETANYEQZ_B = 0x729ca000,
    VSETANYEQZ_H = 0x729ca400,
    VSETANYEQZ_W = 0x729ca800,
    VSETANYEQZ_D = 0x729cac00,
    VSETALLNEZ_B = 0x729cb000,
    VSETALLNEZ_H = 0x729cb400,
    VSETALLNEZ_W = 0x729cb800,
    VSETALLNEZ_D = 0x729cbc00,
    VFLOGB_S = 0x729cc400,
    VFLOGB_D = 0x729cc800,
    VFCLASS_S = 0x729cd400,
    VFCLASS_D = 0x729cd800,
    VFSQRT_S = 0x729ce400,
    VFSQRT_D = 0x729ce800,
    VFRECIP_S = 0x729cf400,
    VFRECIP_D = 0x729cf800,
    VFRSQRT_S = 0x729d0400,
    VFRSQRT_D = 0x729d0800,
    VFRECIPE_S = 0x729d1400,
    VFRECIPE_D = 0x729d1800,
    VFRSQRTE_S = 0x729d2400,
    VFRSQRTE_D = 0x729d2800,
    VFRINT_S = 0x729d3400,
    VFRINT_D = 0x729d3800,
    VFRINTRM_S = 0x729d4400,
    VFRINTRM_D = 0x729d4800,
    VFRINTRP_S = 0x729d5400,
    VFRINTRP_D = 0x729d5800,
    VFRINTRZ_S = 0x729d6400,
    VFRINTRZ_D = 0x729d6800,
    VFRINTRNE_S = 0x729d7400,
    VFRINTRNE_D = 0x729d7800,
    VFCVTL_S_H = 0x729de800,
    VFCVTH_S_H = 0x729dec00,
    VFCVTL_D_S = 0x729df000,
    VFCVTH_D_S = 0x729df400,
    VFFINT_S_W = 0x729e0000,
    VFFINT_S_WU = 0x729e0400,
    VFFINT_D_L = 0x729e0800,
    VFFINT_D_LU = 0x729e0c00,
    VFFINTL_D_W = 0x729e1000,
    VFFINTH_D_W = 0x729e1400,
    VFTINT_W_S = 0x729e3000,
    VFTINT_L_D = 0x729e3400,
    VFTINTRM_W_S = 0x729e3800,
    VFTINTRM_L_D = 0x729e3c00,
    VFTINTRP_W_S = 0x729e4000,
    VFTINTRP_L_D = 0x729e4400,
    VFTINTRZ_W_S = 0x729e4800,
    VFTINTRZ_L_D = 0x729e4c00,
    VFTINTRNE_W_S = 0x729e5000,
    VFTINTRNE_L_D = 0x729e5400,
    VFTINT_WU_S = 0x729e5800,
    VFTINT_LU_D = 0x729e5c00,
    VFTINTRZ_WU_S = 0x729e7000,
    VFTINTRZ_LU_D = 0x729e7400,
    VFTINTL_L_S = 0x729e8000,
    VFTINTH_L_S = 0x729e8400,
    VFTINTRML_L_S = 0x729e8800,
    VFTINTRMH_L_S = 0x729e8c00,
    VFTINTRPL_L_S = 0x729e9000,
    VFTINTRPH_L_S = 0x729e9400,
    VFTINTRZL_L_S = 0x729e9800,
    VFTINTRZH_L_S = 0x729e9c00,
    VFTINTRNEL_L_S = 0x729ea000,
    VFTINTRNEH_L_S = 0x729ea400,
    VEXTH_H_B = 0x729ee000,
    VEXTH_W_H = 0x729ee400,
    VEXTH_D_W = 0x729ee800,
    VEXTH_Q_D = 0x729eec00,
    VEXTH_HU_BU = 0x729ef000,
    VEXTH_WU_HU = 0x729ef400,
    VEXTH_DU_WU = 0x729ef800,
    VEXTH_QU_DU = 0x729efc00,
    VREPLGR2VR_B = 0x729f0000,
    VREPLGR2VR_H = 0x729f0400,
    VREPLGR2VR_W = 0x729f0800,
    VREPLGR2VR_D = 0x729f0c00,
    VROTRI_B = 0x72a02000,
    VROTRI_H = 0x72a04000,
    VROTRI_W = 0x72a08000,
    VROTRI_D = 0x72a10000,
    VSRLRI_B = 0x72a42000,
    VSRLRI_H = 0x72a44000,
    VSRLRI_W = 0x72a48000,
    VSRLRI_D = 0x72a50000,
    VSRARI_B = 0x72a82000,
    VSRARI_H = 0x72a84000,
    VSRARI_W = 0x72a88000,
    VSRARI_D = 0x72a90000,
    VINSGR2VR_B = 0x72eb8000,
    VINSGR2VR_H = 0x72ebc000,
    VINSGR2VR_W = 0x72ebe000,
    VINSGR2VR_D = 0x72ebf000,
    VPICKVE2GR_B = 0x72ef8000,
    VPICKVE2GR_H = 0x72efc000,
    VPICKVE2GR_W = 0x72efe000,
    VPICKVE2GR_D = 0x72eff000,
    VPICKVE2GR_BU = 0x72f38000,
    VPICKVE2GR_HU = 0x72f3c000,
    VPICKVE2GR_WU = 0x72f3e000,
    VPICKVE2GR_DU = 0x72f3f000,
    VREPLVEI_B = 0x72f78000,
    VREPLVEI_H = 0x72f7c000,
    VREPLVEI_W = 0x72f7e000,
    VREPLVEI_D = 0x72f7f000,
    VSLLWIL_H_B = 0x73082000,
    VSLLWIL_W_H = 0x73084000,
    VSLLWIL_D_W = 0x73088000,
    VEXTL_Q_D = 0x73090000,
    VSLLWIL_HU_BU = 0x730c2000,
    VSLLWIL_WU_HU = 0x730c4000,
    VSLLWIL_DU_WU = 0x730c8000,
    VEXTL_QU_DU = 0x730d0000,
    VBITCLRI_B = 0x73102000,
    VBITCLRI_H = 0x73104000,
    VBITCLRI_W = 0x73108000,
    VBITCLRI_D = 0x73110000,
    VBITSETI_B = 0x73142000,
    VBITSETI_H = 0x73144000,
    VBITSETI_W = 0x73148000,
    VBITSETI_D = 0x73150000,
    VBITREVI_B = 0x73182000,
    VBITREVI_H = 0x73184000,
    VBITREVI_W = 0x73188000,
    VBITREVI_D = 0x73190000,
    VSAT_B = 0x73242000,
    VSAT_H = 0x73244000,
    VSAT_W = 0x73248000,
    VSAT_D = 0x73250000,
    VSAT_BU = 0x73282000,
    VSAT_HU = 0x73284000,
    VSAT_WU = 0x73288000,
    VSAT_DU = 0x73290000,
    VSLLI_B = 0x732c2000,
    VSLLI_H = 0x732c4000,
    VSLLI_W = 0x732c8000,
    VSLLI_D = 0x732d0000,
    VSRLI_B = 0x73302000,
    VSRLI_H = 0x73304000,
    VSRLI_W = 0x73308000,
    VSRLI_D = 0x73310000,
    VSRAI_B = 0x73342000,
    VSRAI_H = 0x73344000,
    VSRAI_W = 0x73348000,
    VSRAI_D = 0x73350000,
    VSRLNI_B_H = 0x73404000,
    VSRLNI_H_W = 0x73408000,
    VSRLNI_W_D = 0x73410000,
    VSRLNI_D_Q = 0x73420000,
    VSRLRNI_B_H = 0x73444000,
    VSRLRNI_H_W = 0x73448000,
    VSRLRNI_W_D = 0x73450000,
    VSRLRNI_D_Q = 0x73460000,
    VSSRLNI_B_H = 0x73484000,
    VSSRLNI_H_W = 0x73488000,
    VSSRLNI_W_D = 0x73490000,
    VSSRLNI_D_Q = 0x734a0000,
    VSSRLNI_BU_H = 0x734c4000,
    VSSRLNI_HU_W = 0x734c8000,
    VSSRLNI_WU_D = 0x734d0000,
    VSSRLNI_DU_Q = 0x734e0000,
    VSSRLRNI_B_H = 0x73504000,
    VSSRLRNI_H_W = 0x73508000,
    VSSRLRNI_W_D = 0x73510000,
    VSSRLRNI_D_Q = 0x73520000,
    VSSRLRNI_BU_H = 0x73544000,
    VSSRLRNI_HU_W = 0x73548000,
    VSSRLRNI_WU_D = 0x73550000,
    VSSRLRNI_DU_Q = 0x73560000,
    VSRANI_B_H = 0x73584000,
    VSRANI_H_W = 0x73588000,
    VSRANI_W_D = 0x73590000,
    VSRANI_D_Q = 0x735a0000,
    VSRARNI_B_H = 0x735c4000,
    VSRARNI_H_W = 0x735c8000,
    VSRARNI_W_D = 0x735d0000,
    VSRARNI_D_Q = 0x735e0000,
    VSSRANI_B_H = 0x73604000,
    VSSRANI_H_W = 0x73608000,
    VSSRANI_W_D = 0x73610000,
    VSSRANI_D_Q = 0x73620000,
    VSSRANI_BU_H = 0x73644000,
    VSSRANI_HU_W = 0x73648000,
    VSSRANI_WU_D = 0x73650000,
    VSSRANI_DU_Q = 0x73660000,
    VSSRARNI_B_H = 0x73684000,
    VSSRARNI_H_W = 0x73688000,
    VSSRARNI_W_D = 0x73690000,
    VSSRARNI_D_Q = 0x736a0000,
    VSSRARNI_BU_H = 0x736c4000,
    VSSRARNI_HU_W = 0x736c8000,
    VSSRARNI_WU_D = 0x736d0000,
    VSSRARNI_DU_Q = 0x736e0000,
    VEXTRINS_D = 0x73800000,
    VEXTRINS_W = 0x73840000,
    VEXTRINS_H = 0x73880000,
    VEXTRINS_B = 0x738c0000,
    VSHUF4I_B = 0x73900000,
    VSHUF4I_H = 0x73940000,
    VSHUF4I_W = 0x73980000,
    VSHUF4I_D = 0x739c0000,
    VBITSELI_B = 0x73c40000,
    VANDI_B = 0x73d00000,
    VORI_B = 0x73d40000,
    VXORI_B = 0x73d80000,
    VNORI_B = 0x73dc0000,
    VLDI = 0x73e00000,
    VPERMI_W = 0x73e40000,
};

static inline s32 SignReduce32(s32 v, int width) {
	int shift = 32 - width;
	return (v << shift) >> shift;
}

static inline s64 SignReduce64(s64 v, int width) {
	int shift = 64 - width;
	return (v << shift) >> shift;
}

static inline bool SupportsCPUCFG() {
    return cpu_info.LOONGARCH_CPUCFG;
}

static inline bool SupportsLAM() {
    return cpu_info.LOONGARCH_LAM;
}

static inline bool SupportsUAL() {
    return cpu_info.LOONGARCH_UAL;
}

static inline bool SupportsFPU() {
    return cpu_info.LOONGARCH_FPU;
}

static inline bool SupportsLSX() {
    return cpu_info.LOONGARCH_LSX;
}

static inline bool SupportsLASX() {
    return cpu_info.LOONGARCH_LASX;
}

static inline bool SupportsCRC32() {
    return cpu_info.LOONGARCH_CRC32;
}

static inline bool SupportsComplex() {
    return cpu_info.LOONGARCH_COMPLEX;
}

static inline bool SupportsCrypto() {
    return cpu_info.LOONGARCH_CRYPTO;
}

static inline bool SupportsLVZ() {
    return cpu_info.LOONGARCH_LVZ;
}

static inline bool SupportsLBT_X86() {
    return cpu_info.LOONGARCH_LBT_X86;
}

static inline bool SupportsLBT_ARM() {
    return cpu_info.LOONGARCH_LBT_ARM;
}

static inline bool SupportsLBT_MIPS() {
    return cpu_info.LOONGARCH_LBT_MIPS;
}

static inline bool SupportsPTW() {
    return cpu_info.LOONGARCH_PTW;
}

LoongArch64Emitter::LoongArch64Emitter(const u8 *ptr, u8 *writePtr) {
    SetCodePointer(ptr, writePtr);
}

void LoongArch64Emitter::SetCodePointer(const u8 *ptr, u8 *writePtr) {
	code_ = ptr;
	writable_ = writePtr;
    lastCacheFlushEnd_ = ptr;
}

const u8 *LoongArch64Emitter::GetCodePointer() const {
	return code_;
}

u8 *LoongArch64Emitter::GetWritableCodePtr() {
	return writable_;
}

static inline u32 EncodeDJK(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(IsGPR(rd), "DJK instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJK instruction rj must be GPR");
    _assert_msg_(IsGPR(rk), "DJK instruction rk must be GPR");
    return (u32)opcode | ((u32)rk << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeDJSk12(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(IsGPR(rd), "DJSk12 instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJSk12 instruction rj must be GPR");
    return (u32)opcode | ((u32)(si12 & 0xFFF) << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeDJUk12(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj, u16 ui12) {
    _assert_msg_(IsGPR(rd), "DJUk12 instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJUk12 instruction rj must be GPR");
    return (u32)opcode | ((u32)(ui12 & 0xFFF) << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeDJSk16(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj, s16 si16) {
    _assert_msg_(IsGPR(rd), "DJSk16 instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJSk16 instruction rj must be GPR");
    return (u32)opcode | ((u32)si16 << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeDJKUa2pp1(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk, u8 sa2) {
    _assert_msg_(IsGPR(rd), "DJKUa2pp1 instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJKUa2pp1 instruction rj must be GPR");
    _assert_msg_(IsGPR(rk), "DJKUa2pp1 instruction rk must be GPR");
    // cpu will perform as sa2 + 1
    return (u32)opcode | (u32)((sa2 - 1) & 0x3) << 15 | ((u32)rk << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeDJKUa3pp1(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk, u8 sa3) {
    _assert_msg_(IsGPR(rd), "DJKUa3pp1 instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJKUa3pp1 instruction rj must be GPR");
    _assert_msg_(IsGPR(rk), "DJKUa3pp1 instruction rk must be GPR");
    // cpu will perform as sa3 + 1
    return (u32)opcode | (u32)((sa3 - 1) & 0x7) << 15 | ((u32)rk << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeDSj20(Opcode32 opcode, LoongArch64Reg rd, s32 si20) {
    _assert_msg_(IsGPR(rd), "DSj20 instruction rd must be GPR");
    return (u32)opcode | ((u32)(si20 & 0xFFFFF) << 5) | (u32)rd;
}

static inline u32 EncodeDJUk5(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj, u8 ui5) {
    _assert_msg_(IsGPR(rd), "DJUk5 instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJUk5 instruction rj must be GPR");
    return (u32)opcode | ((u32)(ui5 & 0x1F) << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeDJUk6(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj, u8 ui6) {
    _assert_msg_(IsGPR(rd), "DJUk6 instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJUk6 instruction rj must be GPR");
    return (u32)opcode | ((u32)(ui6 & 0x3F) << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeDJ(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(IsGPR(rd), "DJ instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJ instruction rj must be GPR");
    return (u32)opcode | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeJK(Opcode32 opcode, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(IsGPR(rj), "JK instruction rj must be GPR");
    _assert_msg_(IsGPR(rk), "JK instruction rk must be GPR");
    return (u32)opcode | ((u32)rk << 5) | ((u32)rj << 5);
}

static inline u32 EncodeDJKUa2(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk, u8 sa2) {
    _assert_msg_(IsGPR(rd), "DJKUa2 instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJKUa2 instruction rj must be GPR");
    _assert_msg_(IsGPR(rk), "DJKUa2 instruction rk must be GPR");
    return (u32)opcode | ((u32)(sa2 & 0x3) << 15) | ((u32)rk << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeDJUk5Um5(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj, u8 msbw, u8 lsbw) {
    _assert_msg_(IsGPR(rd), "DJUk5Um5 instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJUk5Um5 instruction rj must be GPR");
    return (u32)opcode | ((u32)(msbw & 0x1F) << 16) | ((u32)(lsbw & 0x1f) << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeDJUk6Um6(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj, u8 msbd, u8 lsbd) {
    _assert_msg_(IsGPR(rd), "DJUk6Um6 instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJUk6Um6 instruction rj must be GPR");
    return (u32)opcode | ((u32)(msbd & 0x3F) << 16) | ((u32)(lsbd & 0x3f) << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeDJKUa3(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk, u8 sa3) {
    _assert_msg_(IsGPR(rd), "DJKUa3 instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJKUa3 instruction rj must be GPR");
    _assert_msg_(IsGPR(rk), "DJKUa3 instruction rk must be GPR");
    return (u32)opcode | (u32)(sa3 & 0x7) << 15 | ((u32)rk << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeJDSk16ps2(Opcode32 opcode, LoongArch64Reg rj, LoongArch64Reg rd, s32 offs16) {
    _assert_msg_(IsGPR(rd), "JDSk16ps2 instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "JDSk16ps2 instruction rj must be GPR");
    _assert_msg_((offs16 & 3) == 0, "offs16 immediate must be aligned to 4");
    u32 offs = (u32)(offs16 >> 2);
    return (u32)opcode | ((offs & 0xFFFF) << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeDJSk16ps2(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj, s32 offs16) {
    _assert_msg_(IsGPR(rd), "DJSk16ps2 instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJSk16ps2 instruction rj must be GPR");
    _assert_msg_((offs16 & 3) == 0, "offs16 immediate must be aligned to 4");
    u32 offs = (u32)(offs16 >> 2) & 0xFFFF;
    return (u32)opcode | ((offs & 0xFFFF) << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeJSd5k16(Opcode32 opcode, LoongArch64Reg rj, s32 offs21) {
    _assert_msg_(IsGPR(rj), "JSd5k16 instruction rj must be GPR");
    _assert_msg_((offs21 & 3) == 0, "offs21 immediate must be aligned to 4");
    u32 offs = (u32)(offs21 >> 2);
    return (u32)opcode | ((offs & 0xFFFF) << 10) | ((u32)rj << 5) | ((offs >> 16) & 0x1F);
}

static inline u32 EncodeSd10k16ps2(Opcode32 opcode, s32 offs26) {
    _assert_msg_((offs26 & 3) == 0, "offs21 immediate must be aligned to 4");
    u32 offs = (u32)(offs26 >> 2);
    return (u32)opcode | ((offs & 0xFFFF) << 10) | ((offs >> 16) & 0x3FF);
}

static inline u32 EncodeDJSk14ps2(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rj, s16 si14) {
    _assert_msg_(IsGPR(rd), "DJSk14ps2 instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DJSk14ps2 instruction rj must be GPR");
    _assert_msg_((si14 & 3) == 0, "offs21 immediate must be aligned to 4");
    u32 si = (u32)(si14 >> 2);
    return (u32)opcode | (si & 0x3FFF) << 10 | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeUd5JSk12(Opcode32 opcode, u32 hint, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(IsGPR(rj), "Ud5JSk12 instruction rj must be GPR");
    _assert_msg_((si12 & 3) == 0, "offs21 immediate must be aligned to 4");
    return (u32)opcode | ((u32)(si12 & 0xFFF) << 10) | ((u32)rj << 5) | (u32)(hint & 0x1F);
}

static inline u32 EncodeUd5JK(Opcode32 opcode, u32 hint, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(IsGPR(rj), "Ud5JK instruction rj must be GPR");
    _assert_msg_(IsGPR(rk), "Ud5JK instruction rk must be GPR");
    return (u32)opcode | ((u32)rk << 10) | ((u32)rj << 5) | (u32)(hint & 0x1F);
}

static inline u32 EncodeDKJ(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(IsGPR(rd), "DKJ instruction rd must be GPR");
    _assert_msg_(IsGPR(rj), "DKJ instruction rj must be GPR");
    _assert_msg_(IsGPR(rk), "DKJ instruction rk must be GPR");
    return (u32)opcode | ((u32)rk << 10) | ((u32)rj << 5) | (u32)rd;
}

static inline u32 EncodeUd15(Opcode32 opcode, u16 code) {
    return (u32)opcode | (u32)(code & 0x7FFF);
}

static inline u32 EncodeFdFjFk(Opcode32 opcode, LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    _assert_msg_(IsFPR(fd), "FdFjFk instruction fd must be FPR");
    _assert_msg_(IsFPR(fj), "FdFjFk instruction fj must be FPR");
    _assert_msg_(IsFPR(fk), "FdFjFk instruction fk must be FPR");
    return (u32)opcode | ((u32)DecodeReg(fk) << 10) | ((u32)DecodeReg(fj) << 5) | (u32)DecodeReg(fd);
}

static inline u32 EncodeFdFjFkFa(Opcode32 opcode, LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa) {
    _assert_msg_(IsFPR(fd), "FdFjFkFa instruction fd must be FPR");
    _assert_msg_(IsFPR(fj), "FdFjFkFa instruction fj must be FPR");
    _assert_msg_(IsFPR(fk), "FdFjFkFa instruction fk must be FPR");
    _assert_msg_(IsFPR(fa), "FdFjFkFa instruction fk must be FPR");
    return (u32)opcode | ((u32)DecodeReg(fa) << 15) | ((u32)DecodeReg(fk) << 10) | ((u32)DecodeReg(fj) << 5) | (u32)DecodeReg(fd);
}

static inline u32 EncodeFdFj(Opcode32 opcode, LoongArch64Reg fd, LoongArch64Reg fj) {
    _assert_msg_(IsFPR(fd), "FdFj instruction fd must be FPR");
    _assert_msg_(IsFPR(fj), "FdFj instruction fj must be FPR");
    return (u32)opcode | ((u32)DecodeReg(fj) << 5) | (u32)DecodeReg(fd);
}

static inline u32 EncodeCdFjFkFcond(Opcode32 opcode, LoongArch64CFR cd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Fcond cond) {
    _assert_msg_(IsCFR(cd), "CdFjFkFcond instruction cd must be CFR");
    _assert_msg_(IsFPR(fj), "CdFjFkFcond instruction fj must be FPR");
    _assert_msg_(IsFPR(fk), "CdFjFkFcond instruction fk must be FPR");
    return (u32)opcode | ((u32)cond << 15) | ((u32)DecodeReg(fk) << 10) | ((u32)DecodeReg(fj) << 5) | (u32)cd;
}

static inline u32 EncodeFdFjFkCa(Opcode32 opcode, LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64CFR ca) {
    _assert_msg_(IsFPR(fd), "FdFjFkCa instruction fd must be FPR");
    _assert_msg_(IsFPR(fj), "FdFjFkCa instruction fj must be FPR");
    _assert_msg_(IsFPR(fk), "FdFjFkCa instruction fk must be FPR");
    _assert_msg_(IsCFR(ca), "FdFjFkCa instruction ca must be CFR");
    return (u32)opcode | ((u32)ca << 15) | ((u32)DecodeReg(fk) << 10) | ((u32)DecodeReg(fj) << 5) | (u32)DecodeReg(fd);
}

static inline u32 EncodeFdJ(Opcode32 opcode, LoongArch64Reg fd, LoongArch64Reg rj) {
    _assert_msg_(IsFPR(fd), "FdJ instruction fd must be FPR");
    _assert_msg_(IsGPR(rj), "FdJ instruction rj must be GPR");
    return (u32)opcode | ((u32)rj << 5) | (u32)DecodeReg(fd);
}

static inline u32 EncodeDFj(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg fj) {
    _assert_msg_(IsGPR(rd), "DFj instruction rd must be GPR");
    _assert_msg_(IsFPR(fj), "DFj instruction fj must be FPR");
    return (u32)opcode | ((u32)fj << 5) | (u32)DecodeReg(rd);
}

static inline u32 EncodeJUd5(Opcode32 opcode, LoongArch64FCSR fcsr, LoongArch64Reg rj) {
    _assert_msg_(IsFCSR(fcsr), "JUd5 instruction fcsr must be FCSR");
    _assert_msg_(IsGPR(rj), "JUd5 instruction rj must be GPR");
    return (u32)opcode | ((u32)rj << 5) | (u32)fcsr;
}

static inline u32 EncodeDUj5(Opcode32 opcode, LoongArch64Reg rd, LoongArch64FCSR fcsr) {
    _assert_msg_(IsGPR(rd), "DUj5 instruction rd must be GPR");
    _assert_msg_(IsFCSR(fcsr), "DUj5 instruction fcsr must be FCSR");
    return (u32)opcode | ((u32)fcsr << 5) | (u32)rd;
}

static inline u32 EncodeCdFj(Opcode32 opcode, LoongArch64CFR cd, LoongArch64Reg fj) {
    _assert_msg_(IsCFR(cd), "CdFj instruction cd must be CFR");
    _assert_msg_(IsFPR(fj), "CdFj instruction fj must be FPR");
    return (u32)opcode | ((u32)DecodeReg(fj) << 5) | (u32)cd;
}

static inline u32 EncodeFdCj(Opcode32 opcode, LoongArch64Reg fd, LoongArch64CFR cj) {
    _assert_msg_(IsFPR(fd), "FdCj instruction fd must be FPR");
    _assert_msg_(IsCFR(cj), "FdCj instruction cj must be CFR");
    return (u32)opcode | ((u32)cj << 5) | (u32)DecodeReg(fd);
}

static inline u32 EncodeCdJ(Opcode32 opcode, LoongArch64CFR cd, LoongArch64Reg rj) {
    _assert_msg_(IsCFR(cd), "CdJ instruction cd must be CFR");
    _assert_msg_(IsGPR(rj), "CdJ instruction rj must be GPR");
    return (u32)opcode | ((u32)rj << 5) | (u32)cd;
}

static inline u32 EncodeDCj(Opcode32 opcode, LoongArch64Reg rd, LoongArch64CFR cj) {
    _assert_msg_(IsGPR(rd), "DCj instruction rd must be GPR");
    _assert_msg_(IsCFR(cj), "DCj instruction cj must be CFR");
    return (u32)opcode | ((u32)cj << 5) | (u32)rd;
}

static inline u32 EncodeCjSd5k16ps2(Opcode32 opcode, LoongArch64CFR cj, s32 offs21) {
    _assert_msg_(IsCFR(cj), "CjSd5k16ps2 instruction cj must be CFR");
    _assert_msg_((offs21 & 3) == 0, "offs21 immediate must be aligned to 4");
    u32 offs = (u32)(offs21 >> 2);
    return (u32)opcode | ((offs & 0xFFFF) << 10) | ((u32)cj << 5) | ((offs >> 16) & 0x1F);
}

static inline u32 EncodeFdJSk12(Opcode32 opcode, LoongArch64Reg fd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(IsFPR(fd), "FdJSk12 instruction fd must be FPR");
    _assert_msg_(IsGPR(rj), "FdJSk12 instruction rj must be GPR");
    return (u32)opcode | ((u32)(si12 & 0xFFF) << 10) | ((u32)rj << 5) | (u32)DecodeReg(fd);
}

static inline u32 EncodeFdJK(Opcode32 opcode, LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(IsFPR(fd), "FdJK instruction fd must be FPR");
    _assert_msg_(IsGPR(rj), "FdJK instruction rj must be GPR");
    _assert_msg_(IsGPR(rk), "FdJK instruction rk must be GPR");
    return (u32)opcode | ((u32)rk << 10) | ((u32)rj << 5) | (u32)DecodeReg(fd);
}

static inline u32 EncodeVdVjVkVa(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va) {
    _assert_msg_(IsVPR(vd), "VdVjVkVa instruction vd must be VPR");
    _assert_msg_(IsVPR(vj), "VdVjVkVa instruction vj must be VPR");
    _assert_msg_(IsVPR(vk), "VdVjVkVa instruction vk must be VPR");
    _assert_msg_(IsVPR(va), "VdVjVkVa instruction vk must be VPR");
    return (u32)opcode | ((u32)DecodeReg(va) << 15) | ((u32)DecodeReg(vk) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdVjVk(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
    _assert_msg_(IsVPR(vd), "VdVjVk instruction vd must be VPR");
    _assert_msg_(IsVPR(vj), "VdVjVk instruction vj must be VPR");
    _assert_msg_(IsVPR(vk), "VdVjVk instruction vk must be VPR");
    return (u32)opcode | ((u32)DecodeReg(vk) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdJSk12(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(IsVPR(vd), "VdJSk12 instruction vd must be VPR");
    _assert_msg_(IsGPR(rj), "VdJSk12 instruction rj must be GPR");
    return (u32)opcode | ((u32)(si12 & 0xFFF) << 10) | ((u32)DecodeReg(rj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdJSk11(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg rj, s16 si11) {
    _assert_msg_(IsVPR(vd), "VdJSk11 instruction vd must be VPR");
    _assert_msg_(IsGPR(rj), "VdJSk11 instruction rj must be GPR");
    return (u32)opcode | ((u32)((si11 >> 1) & 0x7FF) << 10) | ((u32)DecodeReg(rj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdJSk10(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg rj, s16 si10) {
    _assert_msg_(IsVPR(vd), "VdJSk10 instruction vd must be VPR");
    _assert_msg_(IsGPR(rj), "VdJSk10 instruction rj must be GPR");
    return (u32)opcode | ((u32)((si10 >> 2) & 0x3FF) << 10) | ((u32)DecodeReg(rj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdJSk9(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg rj, s16 si9) {
    _assert_msg_(IsVPR(vd), "VdJSk9 instruction vd must be VPR");
    _assert_msg_(IsGPR(rj), "VdJSk9 instruction rj must be GPR");
    return (u32)opcode | ((u32)((si9 >> 3) & 0x1FF) << 10) | ((u32)DecodeReg(rj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdJSk8Un1(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg rj, s16 si8, u8 idx1) {
    _assert_msg_(IsVPR(vd), "VdJSk8Un1 instruction vd must be VPR");
    _assert_msg_(IsGPR(rj), "VdJSk8Un1 instruction rj must be GPR");
    return (u32)opcode | (u32)((idx1 & 0x1) << 18) | ((u32)((si8 >> 3) & 0xFF) << 10) | ((u32)DecodeReg(rj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdJSk8Un2(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg rj, s16 si8, u8 idx2) {
    _assert_msg_(IsVPR(vd), "VdJSk8Un2 instruction vd must be VPR");
    _assert_msg_(IsGPR(rj), "VdJSk8Un2 instruction rj must be GPR");
    return (u32)opcode | (u32)((idx2 & 0x3) << 18) | ((u32)((si8 >> 2) & 0xFF) << 10) | ((u32)DecodeReg(rj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdJSk8Un3(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg rj, s16 si8, u8 idx3) {
    _assert_msg_(IsVPR(vd), "VdJSk8Un3 instruction vd must be VPR");
    _assert_msg_(IsGPR(rj), "VdJSk8Un3 instruction rj must be GPR");
    return (u32)opcode | (u32)((idx3 & 0x7) << 18) | ((u32)((si8 >> 1) & 0xFF) << 10) | ((u32)DecodeReg(rj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdJSk8Un4(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg rj, s16 si8, u8 idx4) {
    _assert_msg_(IsVPR(vd), "VdJSk8Un4 instruction vd must be VPR");
    _assert_msg_(IsGPR(rj), "VdJSk8Un4 instruction rj must be GPR");
    return (u32)opcode | (u32)((idx4 & 0xF) << 18) | ((u32)(si8 & 0xFF) << 10) | ((u32)DecodeReg(rj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdJK(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(IsVPR(vd), "VdJK instruction vd must be VPR");
    _assert_msg_(IsGPR(rj), "VdJK instruction rj must be GPR");
    _assert_msg_(IsGPR(rk), "VdJK instruction rk must be GPR");
    return (u32)opcode | ((u32)DecodeReg(rk) << 10) | ((u32)DecodeReg(rj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdVjSk5(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
    _assert_msg_(IsVPR(vd), "VdVjSk5 instruction vd must be VPR");
    _assert_msg_(IsVPR(vj), "VdVjSk5 instruction vj must be VPR");
    return (u32)opcode | ((u32)(si5 & 0x1F) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdVjUk1(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg vj, u8 ui1) {
    _assert_msg_(IsVPR(vd), "VdVjUk1 instruction vd must be VPR");
    _assert_msg_(IsVPR(vj), "VdVjUk1 instruction vj must be VPR");
    return (u32)opcode | ((u32)(ui1 & 0x1) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdVjUk2(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg vj, u8 ui2) {
    _assert_msg_(IsVPR(vd), "VdVjUk2 instruction vd must be VPR");
    _assert_msg_(IsVPR(vj), "VdVjUk2 instruction vj must be VPR");
    return (u32)opcode | ((u32)(ui2 & 0x3) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdVjUk3(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
    _assert_msg_(IsVPR(vd), "VdVjUk3 instruction vd must be VPR");
    _assert_msg_(IsVPR(vj), "VdVjUk3 instruction vj must be VPR");
    return (u32)opcode | ((u32)(ui3 & 0x7) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdVjUk4(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
    _assert_msg_(IsVPR(vd), "VdVjUk4 instruction vd must be VPR");
    _assert_msg_(IsVPR(vj), "VdVjUk4 instruction vj must be VPR");
    return (u32)opcode | ((u32)(ui4 & 0xF) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdVjUk5(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
    _assert_msg_(IsVPR(vd), "VdVjUk5 instruction vd must be VPR");
    _assert_msg_(IsVPR(vj), "VdVjUk5 instruction vj must be VPR");
    return (u32)opcode | ((u32)(ui5 & 0x1F) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdVjUk6(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
    _assert_msg_(IsVPR(vd), "VdVjUk6 instruction vd must be VPR");
    _assert_msg_(IsVPR(vj), "VdVjUk6 instruction vj must be VPR");
    return (u32)opcode | ((u32)(ui6 & 0x3F) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdVjUk7(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7) {
    _assert_msg_(IsVPR(vd), "VdVjUk7 instruction vd must be VPR");
    _assert_msg_(IsVPR(vj), "VdVjUk7 instruction vj must be VPR");
    return (u32)opcode | ((u32)(ui7 & 0x7F) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdVjUk8(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
    _assert_msg_(IsVPR(vd), "VdVjUk8 instruction vd must be VPR");
    _assert_msg_(IsVPR(vj), "VdVjUk8 instruction vj must be VPR");
    return (u32)opcode | ((u32)(ui8 & 0xFF) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdVj(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg vj) {
    _assert_msg_(IsVPR(vd), "VdVj instruction vd must be VPR");
    _assert_msg_(IsVPR(vj), "VdVj instruction vj must be VPR");
    return (u32)opcode | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdVjK(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg rk) {
    _assert_msg_(IsVPR(vd), "VdVjK instruction vd must be VPR");
    _assert_msg_(IsVPR(vj), "VdVjK instruction vj must be VPR");
    _assert_msg_(IsGPR(rk), "VdVjK instruction rk must be GPR");
    return (u32)opcode | ((u32)DecodeReg(rk) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeCdVj(Opcode32 opcode, LoongArch64CFR cd, LoongArch64Reg vj) {
    _assert_msg_(IsCFR(cd), "CdVj instruction cd must be CFR");
    _assert_msg_(IsVPR(vj), "CdVj instruction vj must be VPR");
    return (u32)opcode | ((u32)DecodeReg(vj) << 5) | (u32)(cd & 0x7);
}

static inline u32 EncodeVdJ(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg rj) {
    _assert_msg_(IsVPR(vd), "VdJ instruction vd must be VPR");
    _assert_msg_(IsGPR(rj), "VdJ instruction rj must be GPR");
    return (u32)opcode | ((u32)DecodeReg(rj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdJUk1(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg rj, u8 ui1) {
    _assert_msg_(IsVPR(vd), "VdJUk1 instruction vd must be VPR");
    _assert_msg_(IsGPR(rj), "VdJUk1 instruction rj must be GPR");
    return (u32)opcode | ((u32)(ui1 & 0x1) << 10) | ((u32)DecodeReg(rj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdJUk2(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg rj, u8 ui2) {
    _assert_msg_(IsVPR(vd), "VdJUk2 instruction vd must be VPR");
    _assert_msg_(IsGPR(rj), "VdJUk2 instruction rj must be GPR");
    return (u32)opcode | ((u32)(ui2 & 0x3) << 10) | ((u32)DecodeReg(rj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdJUk3(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg rj, u8 ui3) {
    _assert_msg_(IsVPR(vd), "VdJUk3 instruction vd must be VPR");
    _assert_msg_(IsGPR(rj), "VdJUk3 instruction rj must be GPR");
    return (u32)opcode | ((u32)(ui3 & 0x7) << 10) | ((u32)DecodeReg(rj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeVdJUk4(Opcode32 opcode, LoongArch64Reg vd, LoongArch64Reg rj, u8 ui4) {
    _assert_msg_(IsVPR(vd), "VdJUk4 instruction vd must be VPR");
    _assert_msg_(IsGPR(rj), "VdJUk4 instruction rj must be GPR");
    return (u32)opcode | ((u32)(ui4 & 0xF) << 10) | ((u32)DecodeReg(rj) << 5) | (u32)DecodeReg(vd);
}

static inline u32 EncodeDVjUk1(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg vj, u8 ui1) {
    _assert_msg_(IsGPR(rd), "DVjUk1 instruction rd must be GPR");
    _assert_msg_(IsVPR(vj), "DVjUk1 instruction vj must be VPR");
    return (u32)opcode | ((u32)(ui1 & 0x1) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(rd);
}

static inline u32 EncodeDVjUk2(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg vj, u8 ui2) {
    _assert_msg_(IsGPR(rd), "DVjUk2 instruction rd must be GPR");
    _assert_msg_(IsVPR(vj), "DVjUk2 instruction vj must be VPR");
    return (u32)opcode | ((u32)(ui2 & 0x3) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(rd);
}

static inline u32 EncodeDVjUk3(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg vj, u8 ui3) {
    _assert_msg_(IsGPR(rd), "DVjUk3 instruction rd must be GPR");
    _assert_msg_(IsVPR(vj), "DVjUk3 instruction vj must be VPR");
    return (u32)opcode | ((u32)(ui3 & 0x7) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(rd);
}

static inline u32 EncodeDVjUk4(Opcode32 opcode, LoongArch64Reg rd, LoongArch64Reg vj, u8 ui4) {
    _assert_msg_(IsGPR(rd), "DVjUk4 instruction rd must be GPR");
    _assert_msg_(IsVPR(vj), "DVjUk4 instruction vj must be VPR");
    return (u32)opcode | ((u32)(ui4 & 0xF) << 10) | ((u32)DecodeReg(vj) << 5) | (u32)DecodeReg(rd);
}

static inline u32 EncodeVdSj13(Opcode32 opcode, LoongArch64Reg vd, s16 i13) {
    _assert_msg_(IsVPR(vd), "VdJUk4 instruction vd must be VPR");
    return (u32)opcode | ((u32)(i13 & 0x1FFF) << 5) | (u32)DecodeReg(vd);
}

void LoongArch64Emitter::ReserveCodeSpace(u32 bytes)
{
	for (u32 i = 0; i < bytes / 4; i++)
		BREAK(0);
}

const u8 *LoongArch64Emitter::AlignCode16() {
	int c = int((u64)code_ & 15);
	if (c)
		ReserveCodeSpace(16 - c);
	return code_;
}

const u8 *LoongArch64Emitter::AlignCodePage() {
	int page_size = GetMemoryProtectPageSize();
	int c = int((intptr_t)code_ & ((intptr_t)page_size - 1));
	if (c)
		ReserveCodeSpace(page_size - c);
	return code_;
}

void LoongArch64Emitter::FlushIcache() {
	FlushIcacheSection(lastCacheFlushEnd_, code_);
	lastCacheFlushEnd_ = code_;
}

void LoongArch64Emitter::FlushIcacheSection(const u8 *start, const u8 *end) {
#if PPSSPP_ARCH(LOONGARCH64)
	__builtin___clear_cache((char *)start, (char *)end);
#endif
}

FixupBranch::FixupBranch(FixupBranch &&other) {
	ptr = other.ptr;
	type = other.type;
	other.ptr = nullptr;
}

FixupBranch::~FixupBranch() {
	_assert_msg_(ptr == nullptr, "FixupBranch never set (left infinite loop)");
}

FixupBranch &FixupBranch::operator =(FixupBranch &&other) {
	ptr = other.ptr;
	type = other.type;
	other.ptr = nullptr;
	return *this;
}

void LoongArch64Emitter::SetJumpTarget(FixupBranch &branch) {
	SetJumpTarget(branch, code_);
}

void LoongArch64Emitter::SetJumpTarget(FixupBranch &branch, const void *dst) {
	_assert_msg_(branch.ptr != nullptr, "Invalid FixupBranch (SetJumpTarget twice?)");

	const intptr_t srcp = (intptr_t)branch.ptr;
	const intptr_t dstp = (intptr_t)dst;
	const ptrdiff_t writable_delta = writable_ - code_;
	u32 *writableSrc = (u32 *)(branch.ptr + writable_delta);

	u32 fixup;

	_assert_msg_((dstp & 3) == 0, "Destination should be aligned (no compressed)");

	ptrdiff_t distance = dstp - srcp;
	_assert_msg_((distance & 3) == 0, "Distance should be aligned (no compressed)");

	switch (branch.type) {
	case FixupBranchType::B:
		_assert_msg_(BranchInRange(branch.ptr, dst), "B destination is too far away (%p -> %p)", branch.ptr, dst);
		memcpy(&fixup, writableSrc, sizeof(u32));
		fixup = (fixup & 0xFC0003FF) | EncodeJDSk16ps2(Opcode32::ZERO, R_ZERO, R_ZERO, (s32)distance);
		memcpy(writableSrc, &fixup, sizeof(u32));
		break;

	case FixupBranchType::J:
		_assert_msg_(JumpInRange(branch.ptr, dst), "J destination is too far away (%p -> %p)", branch.ptr, dst);
		memcpy(&fixup, writableSrc, sizeof(u32));
		fixup = (fixup & 0xFC000000) | EncodeSd10k16ps2(Opcode32::ZERO, (s32)distance);
		memcpy(writableSrc, &fixup, sizeof(u32));
		break;

    case FixupBranchType::BZ:
		_assert_msg_(BranchInRange(branch.ptr, dst), "B destination is too far away (%p -> %p)", branch.ptr, dst);
		memcpy(&fixup, writableSrc, sizeof(u32));
		fixup = (fixup & 0xFC0003E0) | EncodeJSd5k16(Opcode32::ZERO, R_ZERO, (s32)distance);
		memcpy(writableSrc, &fixup, sizeof(u32));
		break;
	}

	branch.ptr = nullptr;
}

bool LoongArch64Emitter::BranchInRange(const void *func) const {
	return BranchInRange(code_, func);
}

bool LoongArch64Emitter::BranchZeroInRange(const void *func) const {
	return BranchZeroInRange(code_, func);
}

bool LoongArch64Emitter::JumpInRange(const void *func) const {
	return JumpInRange(code_, func);
}

static inline bool BJInRange(const void *src, const void *dst, int bits) {
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)src;
	// Get rid of bits and sign extend to validate range.
	s32 encodable = SignReduce32((s32)distance, bits);
	return distance == encodable;
}

bool LoongArch64Emitter::BranchInRange(const void *src, const void *dst) const {
	return BJInRange(src, dst, 18);
}

bool LoongArch64Emitter::BranchZeroInRange(const void *src, const void *dst) const {
	return BJInRange(src, dst, 23);
}

bool LoongArch64Emitter::JumpInRange(const void *src, const void *dst) const {
	return BJInRange(src, dst, 28);
}

void LoongArch64Emitter::QuickJump(LoongArch64Reg scratchreg, LoongArch64Reg rd, const u8 *dst) {
	if (!JumpInRange(GetCodePointer(), dst)) {
		int32_t lower = (int32_t)SignReduce64((int64_t)dst, 18);
		static_assert(sizeof(intptr_t) <= sizeof(int64_t));
 LI(scratchreg, dst - lower);
		JIRL(rd, scratchreg, lower);
	} else if (rd != R_ZERO) {
		BL(dst);
	} else {
 B(dst);
    }
}

void LoongArch64Emitter::SetRegToImmediate(LoongArch64Reg rd, uint64_t value) {
	int64_t svalue = (int64_t)value;
	_assert_msg_(IsGPR(rd), "SetRegToImmediate only supports GPRs");

	if (SignReduce64(svalue, 12) == svalue) {
		// Nice and simple, small immediate fits in a single ADDI against zero.
		ADDI_D(rd, R_ZERO, (s32)svalue);
		return;
	}

	if (svalue <= 0x7fffffffl && svalue >= -0x80000000l) {
        // Use lu12i.w/ori to load 32-bits immediate.
		LU12I_W(rd, (s32)((svalue & 0xffffffff) >> 12));
		ORI(rd, rd, (s16)(svalue & 0xFFF));
        return;
	} else if (svalue <= 0x7ffffffffffffl && svalue >= -0x8000000000000l) {
        // Use lu12i.w/ori/lu32i.d to load 52-bits immediate.
		LU12I_W(rd, (s32)((svalue & 0xffffffff) >> 12));
		ORI(rd, rd, (s16)(svalue & 0xFFF));
		LU32I_D(rd, (s32)((svalue >> 32) & 0xfffff));
        return;
	}
    // Use lu12i.w/ori/lu32i.d/lu52i.d to load 64-bits immediate.
	LU12I_W(rd, (s32)((svalue & 0xffffffff) >> 12));
	ORI(rd, rd, (s16)(svalue & 0xFFF));
	LU32I_D(rd, (s32)((svalue >> 32) & 0xfffff));
	return LU52I_D(rd, rd, (s16)(svalue >> 52));
}

void LoongArch64Emitter::ADD_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::ADD_W, rd, rj, rk));
}

void LoongArch64Emitter::ADD_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::ADD_D, rd, rj, rk));
}

void LoongArch64Emitter::SUB_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::SUB_W, rd, rj, rk));
}

void LoongArch64Emitter::SUB_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::SUB_D, rd, rj, rk));
}

void LoongArch64Emitter::ADDI_W(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(rd != R_ZERO || (rj == R0 && si12 == 0), "%s write to zero is a HINT", __func__); // work as NOP
    Write32(EncodeDJSk12(Opcode32::ADDI_W, rd, rj, si12));
}

void LoongArch64Emitter::ADDI_D(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(rd != R_ZERO || (rj == R0 && si12 == 0), "%s write to zero is a HINT", __func__); // work as NOP
    Write32(EncodeDJSk12(Opcode32::ADDI_D, rd, rj, si12));
}

void LoongArch64Emitter::ADDU16I_D(LoongArch64Reg rd, LoongArch64Reg rj, s16 si16) {
    _assert_msg_(rd != R_ZERO || (rj == R0 && si16 == 0), "%s write to zero is a HINT", __func__); // work as NOP
    Write32(EncodeDJSk16(Opcode32::ADDU16I_D, rd, rj, si16));
}

void LoongArch64Emitter::ALSL_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk, u8 sa2) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    _assert_msg_( sa2 >= 1 && sa2 <= 4, "%s shift out of range", __func__);
    Write32(EncodeDJKUa2pp1(Opcode32::ALSL_W, rd, rj, rk, sa2));
}

void LoongArch64Emitter::ALSL_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk, u8 sa2) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    _assert_msg_( sa2 >= 1 && sa2 <= 4, "%s shift out of range", __func__);
    Write32(EncodeDJKUa2pp1(Opcode32::ALSL_D, rd, rj, rk, sa2));
}

void LoongArch64Emitter::ALSL_WU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk, u8 sa2) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    _assert_msg_( sa2 >= 1 && sa2 <= 4, "%s shift out of range", __func__);
    Write32(EncodeDJKUa2pp1(Opcode32::ALSL_WU, rd, rj, rk, sa2));
}

void LoongArch64Emitter::LU12I_W(LoongArch64Reg rd, s32 si20) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDSj20(Opcode32::LU12I_W, rd, si20));
}

void LoongArch64Emitter::LU32I_D(LoongArch64Reg rd, s32 si20) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDSj20(Opcode32::LU32I_D, rd, si20));
}

void LoongArch64Emitter::LU52I_D(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJSk12(Opcode32::LU52I_D, rd, rj, si12));
}

void LoongArch64Emitter::SLT(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::SLT, rd, rj, rk));
}

void LoongArch64Emitter::SLTU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::SLTU, rd, rj, rk));
}

void LoongArch64Emitter::SLTI(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJSk12(Opcode32::SLTI, rd, rj, si12));
}

void LoongArch64Emitter::SLTUI(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJSk12(Opcode32::SLTUI, rd, rj, si12));
}

void LoongArch64Emitter::PCADDI(LoongArch64Reg rd, s32 si20) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDSj20(Opcode32::PCADDI, rd, si20));
}

void LoongArch64Emitter::PCADDU12I(LoongArch64Reg rd, s32 si20) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDSj20(Opcode32::PCADDU12I, rd, si20));
}

void LoongArch64Emitter::PCADDU18I(LoongArch64Reg rd, s32 si20) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDSj20(Opcode32::PCADDU18I, rd, si20));
}

void LoongArch64Emitter::PCALAU12I(LoongArch64Reg rd, s32 si20) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDSj20(Opcode32::PCALAU12I, rd, si20));
}

void LoongArch64Emitter::AND(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::AND, rd, rj, rk));
}

void LoongArch64Emitter::OR(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::OR, rd, rj, rk));
}

void LoongArch64Emitter::NOR(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::NOR, rd, rj, rk));
}

void LoongArch64Emitter::XOR(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::XOR, rd, rj, rk));
}

void LoongArch64Emitter::ANDN(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::ANDN, rd, rj, rk));
}

void LoongArch64Emitter::ORN(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::ORN, rd, rj, rk));
}

void LoongArch64Emitter::ANDI(LoongArch64Reg rd, LoongArch64Reg rj, u16 ui12) {
    _assert_msg_(rd != R_ZERO || (rj == R0 && ui12 == 0), "%s write to zero is a HINT", __func__); // work as NOP
    Write32(EncodeDJUk12(Opcode32::ANDI, rd, rj, ui12));
}

void LoongArch64Emitter::ORI(LoongArch64Reg rd, LoongArch64Reg rj, u16 ui12) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJUk12(Opcode32::ORI, rd, rj, ui12));
}

void LoongArch64Emitter::XORI(LoongArch64Reg rd, LoongArch64Reg rj, u16 ui12) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJUk12(Opcode32::XORI, rd, rj, ui12));
}

void LoongArch64Emitter::MUL_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::MUL_W, rd, rj, rk));
}

void LoongArch64Emitter::MULH_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::MULH_W, rd, rj, rk));
}

void LoongArch64Emitter::MULH_WU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::MULH_WU, rd, rj, rk));
}

void LoongArch64Emitter::MUL_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::MUL_D, rd, rj, rk));
}

void LoongArch64Emitter::MULH_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::MULH_D, rd, rj, rk));
}

void LoongArch64Emitter::MULH_DU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::MULH_DU, rd, rj, rk));
}

void LoongArch64Emitter::MULW_D_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::MULW_D_W, rd, rj, rk));
}

void LoongArch64Emitter::MULW_D_WU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::MULW_D_WU, rd, rj, rk));
}

void LoongArch64Emitter::DIV_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::DIV_W, rd, rj, rk));
}

void LoongArch64Emitter::MOD_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::MOD_W, rd, rj, rk));
}

void LoongArch64Emitter::DIV_WU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::DIV_WU, rd, rj, rk));
}

void LoongArch64Emitter::MOD_WU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::MOD_WU, rd, rj, rk));
}

void LoongArch64Emitter::DIV_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::DIV_D, rd, rj, rk));
}

void LoongArch64Emitter::MOD_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::MOD_D, rd, rj, rk));
}

void LoongArch64Emitter::DIV_DU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::DIV_DU, rd, rj, rk));
}

void LoongArch64Emitter::MOD_DU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::MOD_DU, rd, rj, rk));
}

void LoongArch64Emitter::SLL_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::SLL_W, rd, rj, rk));
}

void LoongArch64Emitter::SRL_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::SRL_W, rd, rj, rk));
}

void LoongArch64Emitter::SRA_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::SRA_W, rd, rj, rk));
}

void LoongArch64Emitter::ROTR_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::ROTR_W, rd, rj, rk));
}

void LoongArch64Emitter::SLLI_W(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui5) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJUk5(Opcode32::SLLI_W, rd, rj, ui5));
}

void LoongArch64Emitter::SRLI_W(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui5) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJUk5(Opcode32::SRLI_W, rd, rj, ui5));
}

void LoongArch64Emitter::SRAI_W(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui5) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJUk5(Opcode32::SRAI_W, rd, rj, ui5));
}

void LoongArch64Emitter::ROTRI_W(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui5) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJUk5(Opcode32::ROTRI_W, rd, rj, ui5));
}

void LoongArch64Emitter::SLL_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::SLL_D, rd, rj, rk));
}

void LoongArch64Emitter::SRL_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::SRL_D, rd, rj, rk));
}

void LoongArch64Emitter::SRA_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::SRA_D, rd, rj, rk));
}

void LoongArch64Emitter::ROTR_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::ROTR_D, rd, rj, rk));
}

void LoongArch64Emitter::SLLI_D(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui6) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJUk6(Opcode32::SLLI_D, rd, rj, ui6));
}

void LoongArch64Emitter::SRLI_D(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui6) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJUk6(Opcode32::SRLI_D, rd, rj, ui6));
}

void LoongArch64Emitter::SRAI_D(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui6) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJUk6(Opcode32::SRAI_D, rd, rj, ui6));
}

void LoongArch64Emitter::ROTRI_D(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui6) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJUk6(Opcode32::ROTRI_D, rd, rj, ui6));
}

void LoongArch64Emitter::EXT_W_B(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::EXT_W_B, rd, rj));
}

void LoongArch64Emitter::EXT_W_H(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::EXT_W_H, rd, rj));
}

void LoongArch64Emitter::CLO_W(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::CLO_W, rd, rj));
}

void LoongArch64Emitter::CLO_D(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::CLO_D, rd, rj));
}

void LoongArch64Emitter::CLZ_W(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::CLZ_W, rd, rj));
}

void LoongArch64Emitter::CLZ_D(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::CLZ_D, rd, rj));
}

void LoongArch64Emitter::CTO_W(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::CTO_W, rd, rj));
}

void LoongArch64Emitter::CTO_D(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::CTO_D, rd, rj));
}

void LoongArch64Emitter::CTZ_W(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::CTZ_W, rd, rj));
}

void LoongArch64Emitter::CTZ_D(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::CTZ_D, rd, rj));
}

void LoongArch64Emitter::BYTEPICK_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk, u8 sa2) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJKUa2(Opcode32::BYTEPICK_W, rd, rj, rk, sa2));
}

void LoongArch64Emitter::BYTEPICK_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk, u8 sa3) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJKUa2(Opcode32::BYTEPICK_D, rd, rj, rk, sa3));
}

void LoongArch64Emitter::REVB_2H(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::REVB_2H, rd, rj));
}

void LoongArch64Emitter::REVB_4H(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::REVB_4H, rd, rj));
}

void LoongArch64Emitter::REVB_2W(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::REVB_2W, rd, rj));
}

void LoongArch64Emitter::REVB_D(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::REVB_D, rd, rj));
}

void LoongArch64Emitter::BITREV_4B(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::BITREV_4B, rd, rj));
}

void LoongArch64Emitter::BITREV_8B(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::BITREV_8B, rd, rj));
}

void LoongArch64Emitter::BITREV_W(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::BITREV_W, rd, rj));
}

void LoongArch64Emitter::BITREV_D(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::BITREV_D, rd, rj));
}

void LoongArch64Emitter::BSTRINS_W(LoongArch64Reg rd, LoongArch64Reg rj, u8 msbw, u8 lsbw) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJUk5Um5(Opcode32::BSTRINS_W, rd, rj, msbw, lsbw));
}

void LoongArch64Emitter::BSTRINS_D(LoongArch64Reg rd, LoongArch64Reg rj, u8 msbd, u8 lsbd) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJUk6Um6(Opcode32::BSTRINS_D, rd, rj, msbd, lsbd));
}

void LoongArch64Emitter::BSTRPICK_W(LoongArch64Reg rd, LoongArch64Reg rj, u8 msbw, u8 lsbw) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJUk5Um5(Opcode32::BSTRPICK_W, rd, rj, msbw, lsbw));
}

void LoongArch64Emitter::BSTRPICK_D(LoongArch64Reg rd, LoongArch64Reg rj, u8 msbd, u8 lsbd) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJUk6Um6(Opcode32::BSTRPICK_D, rd, rj, msbd, lsbd));
}

void LoongArch64Emitter::MASKEQZ(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::MASKEQZ, rd, rj, rk));
}

void LoongArch64Emitter::MASKNEZ(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::MASKNEZ, rd, rj, rk));
}

void LoongArch64Emitter::BEQ(LoongArch64Reg rj, LoongArch64Reg rd, const void *dst) {
    _assert_msg_(BranchInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
    _assert_msg_(((intptr_t)dst & 3) == 0, "%s destination should be aligned to 4", __func__);
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeJDSk16ps2(Opcode32::BEQ, rj, rd, (s32)distance));
}

void LoongArch64Emitter::BNE(LoongArch64Reg rj, LoongArch64Reg rd, const void *dst) {
    _assert_msg_(BranchInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0, "%s destination should be aligned to 4", __func__);
    ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeJDSk16ps2(Opcode32::BNE, rj, rd, (s32)distance));
}

void LoongArch64Emitter::BLT(LoongArch64Reg rj, LoongArch64Reg rd, const void *dst) {
    _assert_msg_(BranchInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0, "%s destination should be aligned to 4", __func__);
    ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeJDSk16ps2(Opcode32::BLT, rj, rd, (s32)distance));
}

void LoongArch64Emitter::BGE(LoongArch64Reg rj, LoongArch64Reg rd, const void *dst) {
    _assert_msg_(BranchInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0, "%s destination should be aligned to 4", __func__);
    ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeJDSk16ps2(Opcode32::BGE, rj, rd, (s32)distance));
}

void LoongArch64Emitter::BLTU(LoongArch64Reg rj, LoongArch64Reg rd, const void *dst) {
    _assert_msg_(BranchInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0, "%s destination should be aligned to 4", __func__);
    ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeJDSk16ps2(Opcode32::BLTU, rj, rd, (s32)distance));
}

void LoongArch64Emitter::BGEU(LoongArch64Reg rj, LoongArch64Reg rd, const void *dst) {
    _assert_msg_(BranchInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0, "%s destination should be aligned to 4", __func__);
    ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeJDSk16ps2(Opcode32::BGEU, rj, rd, (s32)distance));
}

FixupBranch LoongArch64Emitter::BEQ(LoongArch64Reg rj, LoongArch64Reg rd) {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::B };
	Write32(EncodeJDSk16ps2(Opcode32::BEQ, rj, rd, 0));
	return fixup;
}

FixupBranch LoongArch64Emitter::BNE(LoongArch64Reg rj, LoongArch64Reg rd) {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::B };
	Write32(EncodeJDSk16ps2(Opcode32::BNE, rj, rd, 0));
	return fixup;
}

FixupBranch LoongArch64Emitter::BLT(LoongArch64Reg rj, LoongArch64Reg rd) {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::B };
	Write32(EncodeJDSk16ps2(Opcode32::BLT, rj, rd, 0));
	return fixup;
}

FixupBranch LoongArch64Emitter::BGE(LoongArch64Reg rj, LoongArch64Reg rd) {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::B };
	Write32(EncodeJDSk16ps2(Opcode32::BGE, rj, rd, 0));
	return fixup;
}

FixupBranch LoongArch64Emitter::BLTU(LoongArch64Reg rj, LoongArch64Reg rd) {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::B };
	Write32(EncodeJDSk16ps2(Opcode32::BLTU, rj, rd, 0));
	return fixup;
}

FixupBranch LoongArch64Emitter::BGEU(LoongArch64Reg rj, LoongArch64Reg rd) {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::B };
	Write32(EncodeJDSk16ps2(Opcode32::BGEU, rj, rd, 0));
	return fixup;
}

void LoongArch64Emitter::BEQZ(LoongArch64Reg rj, const void *dst) {
    _assert_msg_(BranchZeroInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0, "%s destination should be aligned to 4", __func__);
    ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeJSd5k16(Opcode32::BEQZ, rj, (s32)distance));
}

void LoongArch64Emitter::BNEZ(LoongArch64Reg rj, const void *dst) {
    _assert_msg_(BranchZeroInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0, "%s destination should be aligned to 4", __func__);
    ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeJSd5k16(Opcode32::BNEZ, rj, (s32)distance));
}

FixupBranch LoongArch64Emitter::BEQZ(LoongArch64Reg rj) {
    FixupBranch fixup{ GetCodePointer(), FixupBranchType::BZ };
	Write32(EncodeJSd5k16(Opcode32::BEQZ, rj, 0));
	return fixup;
}

FixupBranch LoongArch64Emitter::BNEZ(LoongArch64Reg rj) {
    FixupBranch fixup{ GetCodePointer(), FixupBranchType::BZ };
	Write32(EncodeJSd5k16(Opcode32::BNEZ, rj, 0));
	return fixup;
}

void LoongArch64Emitter::B(const void *dst) {
    _assert_msg_(JumpInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0, "%s destination should be aligned to 4", __func__);
    ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
    Write32(EncodeSd10k16ps2(Opcode32::B, (s32)distance));
}

void LoongArch64Emitter::BL(const void *dst) {
    _assert_msg_(JumpInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0, "%s destination should be aligned to 4", __func__);
    ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
    Write32(EncodeSd10k16ps2(Opcode32::BL, (s32)distance));
}

FixupBranch LoongArch64Emitter::B() {
    FixupBranch fixup{ GetCodePointer(), FixupBranchType::J };
	Write32(EncodeSd10k16ps2(Opcode32::B, 0));
	return fixup;
}

FixupBranch LoongArch64Emitter::BL() {
    FixupBranch fixup{ GetCodePointer(), FixupBranchType::J };
	Write32(EncodeSd10k16ps2(Opcode32::BL, 0));
	return fixup;
}

void LoongArch64Emitter::JIRL(LoongArch64Reg rd, LoongArch64Reg rj, s32 offs16) {
    Write32(EncodeDJSk16ps2(Opcode32::JIRL, rd, rj, offs16));
}

void LoongArch64Emitter::LD_B(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJSk12(Opcode32::LD_B, rd, rj, si12));
}

void LoongArch64Emitter::LD_H(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJSk12(Opcode32::LD_H, rd, rj, si12));
}

void LoongArch64Emitter::LD_W(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJSk12(Opcode32::LD_W, rd, rj, si12));
}

void LoongArch64Emitter::LD_D(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJSk12(Opcode32::LD_D, rd, rj, si12));
}

void LoongArch64Emitter::LD_BU(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJSk12(Opcode32::LD_BU, rd, rj, si12));
}

void LoongArch64Emitter::LD_HU(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJSk12(Opcode32::LD_HU, rd, rj, si12));
}

void LoongArch64Emitter::LD_WU(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJSk12(Opcode32::LD_WU, rd, rj, si12));
}

void LoongArch64Emitter::ST_B(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    Write32(EncodeDJSk12(Opcode32::ST_B, rd, rj, si12));
}

void LoongArch64Emitter::ST_H(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    Write32(EncodeDJSk12(Opcode32::ST_H, rd, rj, si12));
}

void LoongArch64Emitter::ST_W(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    Write32(EncodeDJSk12(Opcode32::ST_W, rd, rj, si12));
}

void LoongArch64Emitter::ST_D(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12) {
    Write32(EncodeDJSk12(Opcode32::ST_D, rd, rj, si12));
}

void LoongArch64Emitter::LDX_B(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDX_B, rd, rj, rk));
}

void LoongArch64Emitter::LDX_H(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDX_H, rd, rj, rk));
}

void LoongArch64Emitter::LDX_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDX_W, rd, rj, rk));
}

void LoongArch64Emitter::LDX_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDX_D, rd, rj, rk));
}

void LoongArch64Emitter::LDX_BU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDX_BU, rd, rj, rk));
}

void LoongArch64Emitter::LDX_HU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDX_HU, rd, rj, rk));
}

void LoongArch64Emitter::LDX_WU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDX_WU, rd, rj, rk));
}

void LoongArch64Emitter::STX_B(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::STX_B, rd, rj, rk));
}

void LoongArch64Emitter::STX_H(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::STX_H, rd, rj, rk));
}

void LoongArch64Emitter::STX_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::STX_W, rd, rj, rk));
}

void LoongArch64Emitter::STX_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::STX_D, rd, rj, rk));
}

void LoongArch64Emitter::LDPTR_W(LoongArch64Reg rd, LoongArch64Reg rj, s16 si14) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJSk14ps2(Opcode32::LDPTR_W, rd, rj, si14));
}

void LoongArch64Emitter::LDPTR_D(LoongArch64Reg rd, LoongArch64Reg rj, s16 si14) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJSk14ps2(Opcode32::LDPTR_W, rd, rj, si14));
}

void LoongArch64Emitter::STPTR_W(LoongArch64Reg rd, LoongArch64Reg rj, s16 si14) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJSk14ps2(Opcode32::LDPTR_W, rd, rj, si14));
}

void LoongArch64Emitter::STPTR_D(LoongArch64Reg rd, LoongArch64Reg rj, s16 si14) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJSk14ps2(Opcode32::LDPTR_W, rd, rj, si14));
}

void LoongArch64Emitter::PRELD(u32 hint, LoongArch64Reg rj, s16 si12) {
    _assert_msg_(rj != R_ZERO, "%s load from zero is a HINT", __func__);
    _assert_msg_(hint == 0 || hint == 8, "%s hint represents a NOP", __func__);
    Write32(EncodeUd5JSk12(Opcode32::PRELD, hint, rj, si12));
}

void LoongArch64Emitter::PRELDX(u32 hint, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rj != R_ZERO, "%s load from zero is a HINT", __func__);
    _assert_msg_(hint == 0 || hint == 8, "%s hint represents a NOP", __func__);
    Write32(EncodeUd5JK(Opcode32::PRELDX, hint, rj, rk));
}

void LoongArch64Emitter::LDGT_B(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDGT_B, rd, rj, rk));
}

void LoongArch64Emitter::LDGT_H(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDGT_H, rd, rj, rk));
}

void LoongArch64Emitter::LDGT_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDGT_W, rd, rj, rk));
}

void LoongArch64Emitter::LDGT_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDGT_D, rd, rj, rk));
}

void LoongArch64Emitter::LDLE_B(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDLE_B, rd, rj, rk));
}

void LoongArch64Emitter::LDLE_H(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDLE_H, rd, rj, rk));
}

void LoongArch64Emitter::LDLE_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDLE_W, rd, rj, rk));
}

void LoongArch64Emitter::LDLE_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::LDLE_D, rd, rj, rk));
}

void LoongArch64Emitter::STGT_B(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::STGT_B, rd, rj, rk));
}

void LoongArch64Emitter::STGT_H(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::STGT_H, rd, rj, rk));
}

void LoongArch64Emitter::STGT_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::STGT_W, rd, rj, rk));
}

void LoongArch64Emitter::STGT_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::STGT_D, rd, rj, rk));
}

void LoongArch64Emitter::STLE_B(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::STLE_B, rd, rj, rk));
}

void LoongArch64Emitter::STLE_H(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::STLE_H, rd, rj, rk));
}

void LoongArch64Emitter::STLE_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::STLE_W, rd, rj, rk));
}

void LoongArch64Emitter::STLE_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJK(Opcode32::STLE_D, rd, rj, rk));
}

void LoongArch64Emitter::AMSWAP_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMSWAP_W, rd, rk, rj));
}

void LoongArch64Emitter::AMSWAP_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMSWAP_DB_W, rd, rk, rj));
}

void LoongArch64Emitter::AMSWAP_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMSWAP_D, rd, rk, rj));
}

void LoongArch64Emitter::AMSWAP_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMSWAP_DB_D, rd, rk, rj));
}

void LoongArch64Emitter::AMADD_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMADD_W, rd, rk, rj));
}

void LoongArch64Emitter::AMADD_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMADD_DB_W, rd, rk, rj));
}

void LoongArch64Emitter::AMADD_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMADD_D, rd, rk, rj));
}

void LoongArch64Emitter::AMADD_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMADD_DB_D, rd, rk, rj));
}

void LoongArch64Emitter::AMAND_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMAND_W, rd, rk, rj));
}

void LoongArch64Emitter::AMAND_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMAND_DB_W, rd, rk, rj));
}

void LoongArch64Emitter::AMAND_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMAND_D, rd, rk, rj));
}

void LoongArch64Emitter::AMAND_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMAND_DB_D, rd, rk, rj));
}

void LoongArch64Emitter::AMOR_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMOR_W, rd, rk, rj));
}

void LoongArch64Emitter::AMOR_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMOR_DB_W, rd, rk, rj));
}

void LoongArch64Emitter::AMOR_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMOR_D, rd, rk, rj));
}

void LoongArch64Emitter::AMOR_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMOR_DB_D, rd, rk, rj));
}

void LoongArch64Emitter::AMXOR_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMXOR_W, rd, rk, rj));
}

void LoongArch64Emitter::AMXOR_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMXOR_DB_W, rd, rk, rj));
}

void LoongArch64Emitter::AMXOR_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMXOR_D, rd, rk, rj));
}

void LoongArch64Emitter::AMXOR_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMXOR_DB_D, rd, rk, rj));
}

void LoongArch64Emitter::AMMAX_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMAX_W, rd, rk, rj));
}

void LoongArch64Emitter::AMMAX_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMAX_DB_W, rd, rk, rj));
}

void LoongArch64Emitter::AMMAX_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMAX_D, rd, rk, rj));
}

void LoongArch64Emitter::AMMAX_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMAX_DB_D, rd, rk, rj));
}

void LoongArch64Emitter::AMMIN_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMIN_W, rd, rk, rj));
}

void LoongArch64Emitter::AMMIN_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMIN_DB_W, rd, rk, rj));
}

void LoongArch64Emitter::AMMIN_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMIN_D, rd, rk, rj));
}

void LoongArch64Emitter::AMMIN_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMIN_DB_D, rd, rk, rj));
}

void LoongArch64Emitter::AMMAX_WU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMAX_WU, rd, rk, rj));
}

void LoongArch64Emitter::AMMAX_DB_WU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMAX_DB_WU, rd, rk, rj));
}

void LoongArch64Emitter::AMMAX_DU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMAX_DU, rd, rk, rj));
}

void LoongArch64Emitter::AMMAX_DB_DU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMAX_DB_DU, rd, rk, rj));
}

void LoongArch64Emitter::AMMIN_WU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMIN_WU, rd, rk, rj));
}

void LoongArch64Emitter::AMMIN_DB_WU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMIN_DB_WU, rd, rk, rj));
}

void LoongArch64Emitter::AMMIN_DU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMIN_DU, rd, rk, rj));
}

void LoongArch64Emitter::AMMIN_DB_DU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMMIN_DB_DU, rd, rk, rj));
}

void LoongArch64Emitter::AMSWAP_B(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMSWAP_B, rd, rk, rj));
}

void LoongArch64Emitter::AMSWAP_DB_B(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMSWAP_DB_B, rd, rk, rj));
}

void LoongArch64Emitter::AMSWAP_H(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMSWAP_H, rd, rk, rj));
}

void LoongArch64Emitter::AMSWAP_DB_H(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMSWAP_DB_H, rd, rk, rj));
}

void LoongArch64Emitter::AMADD_B(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMADD_B, rd, rk, rj));
}

void LoongArch64Emitter::AMADD_DB_B(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMADD_DB_B, rd, rk, rj));
}

void LoongArch64Emitter::AMADD_H(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMADD_H, rd, rk, rj));
}

void LoongArch64Emitter::AMADD_DB_H(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMADD_DB_H, rd, rk, rj));
}

void LoongArch64Emitter::AMCAS_B(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMCAS_B, rd, rk, rj));
}

void LoongArch64Emitter::AMCAS_DB_B(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMCAS_DB_B, rd, rk, rj));
}

void LoongArch64Emitter::AMCAS_H(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMCAS_H, rd, rk, rj));
}

void LoongArch64Emitter::AMCAS_DB_H(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMCAS_DB_H, rd, rk, rj));
}

void LoongArch64Emitter::AMCAS_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMCAS_W, rd, rk, rj));
}

void LoongArch64Emitter::AMCAS_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMCAS_DB_W, rd, rk, rj));
}

void LoongArch64Emitter::AMCAS_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMCAS_D, rd, rk, rj));
}

void LoongArch64Emitter::AMCAS_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::AMCAS_DB_D, rd, rk, rj));
}

void LoongArch64Emitter::CRC_W_B_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::CRC_W_B_W, rd, rk, rj));
}

void LoongArch64Emitter::CRC_W_H_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::CRC_W_H_W, rd, rk, rj));
}

void LoongArch64Emitter::CRC_W_W_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::CRC_W_W_W, rd, rk, rj));
}

void LoongArch64Emitter::CRC_W_D_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::CRC_W_D_W, rd, rk, rj));
}

void LoongArch64Emitter::CRCC_W_B_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::CRCC_W_B_W, rd, rk, rj));
}

void LoongArch64Emitter::CRCC_W_H_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::CRCC_W_H_W, rd, rk, rj));
}

void LoongArch64Emitter::CRCC_W_W_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::CRCC_W_W_W, rd, rk, rj));
}

void LoongArch64Emitter::CRCC_W_D_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDKJ(Opcode32::CRCC_W_D_W, rd, rk, rj));
}

void LoongArch64Emitter::SYSCALL(u16 code) {
    Write32(EncodeUd15(Opcode32::SYSCALL, code));
}

void LoongArch64Emitter::BREAK(u16 code) {
    Write32(EncodeUd15(Opcode32::BREAK, code));
}

void LoongArch64Emitter::ASRTLE_D(LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeJK(Opcode32::ASRTLE_D, rj, rk));
}

void LoongArch64Emitter::ASRTGT_D(LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeJK(Opcode32::ASRTGT_D, rj, rk));
}

void LoongArch64Emitter::RDTIMEL_W(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::RDTIMEL_W, rd, rj));
}

void LoongArch64Emitter::RDTIMEH_W(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::RDTIMEH_W, rd, rj));
}

void LoongArch64Emitter::RDTIME_D(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::RDTIME_D, rd, rj));
}

void LoongArch64Emitter::CPUCFG(LoongArch64Reg rd, LoongArch64Reg rj) {
    _assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
    Write32(EncodeDJ(Opcode32::CPUCFG, rd, rj));
}

void LoongArch64Emitter::FADD_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FADD_S, fd, fj, fk));
}

void LoongArch64Emitter::FADD_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FADD_D, fd, fj, fk));
}

void LoongArch64Emitter::FSUB_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FSUB_S, fd, fj, fk));
}

void LoongArch64Emitter::FSUB_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FSUB_D, fd, fj, fk));
}

void LoongArch64Emitter::FMUL_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FMUL_S, fd, fj, fk));
}

void LoongArch64Emitter::FMUL_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FMUL_D, fd, fj, fk));
}

void LoongArch64Emitter::FDIV_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FDIV_S, fd, fj, fk));
}

void LoongArch64Emitter::FDIV_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FDIV_D, fd, fj, fk));
}

void LoongArch64Emitter::FMADD_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa) {
    Write32(EncodeFdFjFkFa(Opcode32::FMADD_S, fd, fj, fk, fa));
}

void LoongArch64Emitter::FMADD_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa) {
    Write32(EncodeFdFjFkFa(Opcode32::FMADD_D, fd, fj, fk, fa));
}

void LoongArch64Emitter::FMSUB_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa) {
    Write32(EncodeFdFjFkFa(Opcode32::FMSUB_S, fd, fj, fk, fa));
}

void LoongArch64Emitter::FMSUB_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa) {
    Write32(EncodeFdFjFkFa(Opcode32::FMSUB_D, fd, fj, fk, fa));
}

void LoongArch64Emitter::FNMADD_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa) {
    Write32(EncodeFdFjFkFa(Opcode32::FNMADD_S, fd, fj, fk, fa));
}

void LoongArch64Emitter::FNMADD_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa) {
    Write32(EncodeFdFjFkFa(Opcode32::FNMADD_D, fd, fj, fk, fa));
}

void LoongArch64Emitter::FNMSUB_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa) {
    Write32(EncodeFdFjFkFa(Opcode32::FNMSUB_S, fd, fj, fk, fa));
}

void LoongArch64Emitter::FNMSUB_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa) {
    Write32(EncodeFdFjFkFa(Opcode32::FNMSUB_D, fd, fj, fk, fa));
}

void LoongArch64Emitter::FMAX_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FMAX_S, fd, fj, fk));
}

void LoongArch64Emitter::FMAX_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FMAX_D, fd, fj, fk));
}

void LoongArch64Emitter::FMIN_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FMIN_S, fd, fj, fk));
}

void LoongArch64Emitter::FMIN_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FMIN_D, fd, fj, fk));
}

void LoongArch64Emitter::FMAXA_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FMAXA_S, fd, fj, fk));
}

void LoongArch64Emitter::FMAXA_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FMAXA_D, fd, fj, fk));
}

void LoongArch64Emitter::FMINA_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FMINA_S, fd, fj, fk));
}

void LoongArch64Emitter::FMINA_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FMINA_D, fd, fj, fk));
}

void LoongArch64Emitter::FABS_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FABS_S, fd, fj));
}

void LoongArch64Emitter::FABS_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FABS_D, fd, fj));
}

void LoongArch64Emitter::FNEG_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FNEG_S, fd, fj));
}

void LoongArch64Emitter::FNEG_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FNEG_D, fd, fj));
}

void LoongArch64Emitter::FSQRT_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FSQRT_S, fd, fj));
}

void LoongArch64Emitter::FSQRT_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FSQRT_D, fd, fj));
}

void LoongArch64Emitter::FRECIP_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FRECIP_S, fd, fj));
}

void LoongArch64Emitter::FRECIP_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FRECIP_D, fd, fj));
}

void LoongArch64Emitter::FRSQRT_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FRSQRT_S, fd, fj));
}

void LoongArch64Emitter::FRSQRT_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FRSQRT_D, fd, fj));
}

void LoongArch64Emitter::FSCALEB_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FSCALEB_S, fd, fj, fk));
}

void LoongArch64Emitter::FSCALEB_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FSCALEB_D, fd, fj, fk));
}

void LoongArch64Emitter::FLOGB_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FLOGB_S, fd, fj));
}

void LoongArch64Emitter::FLOGB_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FLOGB_D, fd, fj));
}

void LoongArch64Emitter::FCOPYSIGN_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FCOPYSIGN_S, fd, fj, fk));
}

void LoongArch64Emitter::FCOPYSIGN_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk) {
    Write32(EncodeFdFjFk(Opcode32::FCOPYSIGN_D, fd, fj, fk));
}

void LoongArch64Emitter::FCLASS_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FCLASS_S, fd, fj));
}

void LoongArch64Emitter::FCLASS_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FCLASS_D, fd, fj));
}

void LoongArch64Emitter::FRECIPE_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FRECIPE_S, fd, fj));
}

void LoongArch64Emitter::FRECIPE_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FRECIPE_D, fd, fj));
}

void LoongArch64Emitter::FRSQRTE_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FRSQRTE_S, fd, fj));
}

void LoongArch64Emitter::FRSQRTE_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FRSQRTE_D, fd, fj));
}

void LoongArch64Emitter::FCMP_COND_S(LoongArch64CFR cd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Fcond cond) {
    Write32(EncodeCdFjFkFcond(Opcode32::FCMP_COND_S, cd, fj, fk, cond));
}

void LoongArch64Emitter::FCMP_COND_D(LoongArch64CFR cd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Fcond cond) {
    Write32(EncodeCdFjFkFcond(Opcode32::FCMP_COND_D, cd, fj, fk, cond));
}

void LoongArch64Emitter::FCVT_S_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FCVT_S_D, fd, fj));
}

void LoongArch64Emitter::FCVT_D_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FCVT_D_S, fd, fj));
}

void LoongArch64Emitter::FFINT_S_W(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FFINT_S_W, fd, fj));
}

void LoongArch64Emitter::FFINT_S_L(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FFINT_S_L, fd, fj));
}

void LoongArch64Emitter::FFINT_D_W(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FFINT_D_W, fd, fj));
}

void LoongArch64Emitter::FFINT_D_L(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FFINT_D_L, fd, fj));
}

void LoongArch64Emitter::FTINT_W_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINT_W_S, fd, fj));
}

void LoongArch64Emitter::FTINT_W_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINT_W_D, fd, fj));
}

void LoongArch64Emitter::FTINT_L_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINT_L_S, fd, fj));
}

void LoongArch64Emitter::FTINT_L_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINT_L_D, fd, fj));
}

void LoongArch64Emitter::FTINTRM_W_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRM_W_S, fd, fj));
}

void LoongArch64Emitter::FTINTRM_W_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRM_W_D, fd, fj));
}

void LoongArch64Emitter::FTINTRM_L_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRM_L_S, fd, fj));
}

void LoongArch64Emitter::FTINTRM_L_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRM_L_D, fd, fj));
}

void LoongArch64Emitter::FTINTRP_W_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRP_W_S, fd, fj));
}

void LoongArch64Emitter::FTINTRP_W_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRP_W_D, fd, fj));
}

void LoongArch64Emitter::FTINTRP_L_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRP_L_S, fd, fj));
}

void LoongArch64Emitter::FTINTRP_L_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRP_L_D, fd, fj));
}

void LoongArch64Emitter::FTINTRZ_W_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRZ_W_S, fd, fj));
}

void LoongArch64Emitter::FTINTRZ_W_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRZ_W_D, fd, fj));
}

void LoongArch64Emitter::FTINTRZ_L_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRZ_L_S, fd, fj));
}

void LoongArch64Emitter::FTINTRZ_L_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRZ_L_D, fd, fj));
}

void LoongArch64Emitter::FTINTRNE_W_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRNE_W_S, fd, fj));
}

void LoongArch64Emitter::FTINTRNE_W_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRNE_W_D, fd, fj));
}

void LoongArch64Emitter::FTINTRNE_L_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRNE_L_S, fd, fj));
}

void LoongArch64Emitter::FTINTRNE_L_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FTINTRNE_L_D, fd, fj));
}

void LoongArch64Emitter::FRINT_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FRINT_S, fd, fj));
}

void LoongArch64Emitter::FRINT_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FRINT_D, fd, fj));
}

void LoongArch64Emitter::FMOV_S(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FMOV_S, fd, fj));
}

void LoongArch64Emitter::FMOV_D(LoongArch64Reg fd, LoongArch64Reg fj) {
    Write32(EncodeFdFj(Opcode32::FMOV_D, fd, fj));
}

void LoongArch64Emitter::FSEL(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64CFR ca) {
    Write32(EncodeFdFjFkCa(Opcode32::FSEL, fd, fj, fk, ca));
}

void LoongArch64Emitter::MOVGR2FR_W(LoongArch64Reg fd, LoongArch64Reg rj) {
    Write32(EncodeFdJ(Opcode32::MOVGR2FR_W, fd, rj));
}

void LoongArch64Emitter::MOVGR2FR_D(LoongArch64Reg fd, LoongArch64Reg rj) {
    Write32(EncodeFdJ(Opcode32::MOVGR2FR_D, fd, rj));
}

void LoongArch64Emitter::MOVGR2FRH_W(LoongArch64Reg fd, LoongArch64Reg rj) {
    Write32(EncodeFdJ(Opcode32::MOVGR2FRH_W, fd, rj));
}

void LoongArch64Emitter::MOVFR2GR_S(LoongArch64Reg rd, LoongArch64Reg fj) {
    Write32(EncodeDFj(Opcode32::MOVFR2GR_S, rd, fj));
}

void LoongArch64Emitter::MOVFR2GR_D(LoongArch64Reg rd, LoongArch64Reg fj) {
    Write32(EncodeDFj(Opcode32::MOVFR2GR_D, rd, fj));
}

void LoongArch64Emitter::MOVFRH2GR_S(LoongArch64Reg rd, LoongArch64Reg fj) {
    Write32(EncodeDFj(Opcode32::MOVFRH2GR_S, rd, fj));
}

void LoongArch64Emitter::MOVGR2FCSR(LoongArch64FCSR fcsr, LoongArch64Reg rj) {
    Write32(EncodeJUd5(Opcode32::MOVGR2FCSR, fcsr, rj));
}

void LoongArch64Emitter::MOVFCSR2GR(LoongArch64Reg rd, LoongArch64FCSR fcsr) {
    Write32(EncodeDUj5(Opcode32::MOVFCSR2GR, rd, fcsr));
}

void LoongArch64Emitter::MOVFR2CF(LoongArch64CFR cd, LoongArch64Reg fj) {
    Write32(EncodeCdFj(Opcode32::MOVFR2CF, cd, fj));
}

void LoongArch64Emitter::MOVCF2FR(LoongArch64Reg fd, LoongArch64CFR cj) {
    Write32(EncodeFdCj(Opcode32::MOVCF2FR, fd, cj));
}

void LoongArch64Emitter::MOVGR2CF(LoongArch64CFR cd, LoongArch64Reg rj) {
    Write32(EncodeCdJ(Opcode32::MOVGR2CF, cd, rj));
}

void LoongArch64Emitter::MOVCF2GR(LoongArch64Reg rd, LoongArch64CFR cj) {
    Write32(EncodeDCj(Opcode32::MOVCF2GR, rd, cj));
}

void LoongArch64Emitter::BCEQZ(LoongArch64CFR cj, s32 offs21) {
    Write32(EncodeCjSd5k16ps2(Opcode32::BCEQZ, cj, offs21));
}

void LoongArch64Emitter::BCNEZ(LoongArch64CFR cj, s32 offs21) {
    Write32(EncodeCjSd5k16ps2(Opcode32::BCNEZ, cj, offs21));
}

void LoongArch64Emitter::FLD_S(LoongArch64Reg fd, LoongArch64Reg rj, s16 si12) {
    Write32(EncodeFdJSk12(Opcode32::FLD_S, fd, rj, si12));
}

void LoongArch64Emitter::FLD_D(LoongArch64Reg fd, LoongArch64Reg rj, s16 si12) {
    Write32(EncodeFdJSk12(Opcode32::FLD_D, fd, rj, si12));
}

void LoongArch64Emitter::FST_S(LoongArch64Reg fd, LoongArch64Reg rj, s16 si12) {
    Write32(EncodeFdJSk12(Opcode32::FST_S, fd, rj, si12));
}

void LoongArch64Emitter::FST_D(LoongArch64Reg fd, LoongArch64Reg rj, s16 si12) {
    Write32(EncodeFdJSk12(Opcode32::FST_D, fd, rj, si12));
}

void LoongArch64Emitter::FLDX_S(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeFdJK(Opcode32::FLDX_S, fd, rj, rk));
}

void LoongArch64Emitter::FLDX_D(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeFdJK(Opcode32::FLDX_D, fd, rj, rk));
}

void LoongArch64Emitter::FSTX_S(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeFdJK(Opcode32::FSTX_S, fd, rj, rk));
}

void LoongArch64Emitter::FSTX_D(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeFdJK(Opcode32::FSTX_D, fd, rj, rk));
}

void LoongArch64Emitter::FLDGT_S(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeFdJK(Opcode32::FLDGT_S, fd, rj, rk));
}

void LoongArch64Emitter::FLDGT_D(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeFdJK(Opcode32::FLDGT_D, fd, rj, rk));
}

void LoongArch64Emitter::FLDLE_S(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeFdJK(Opcode32::FLDLE_S, fd, rj, rk));
}

void LoongArch64Emitter::FLDLE_D(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeFdJK(Opcode32::FLDLE_D, fd, rj, rk));
}

void LoongArch64Emitter::FSTGT_S(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeFdJK(Opcode32::FSTGT_S, fd, rj, rk));
}

void LoongArch64Emitter::FSTGT_D(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeFdJK(Opcode32::FSTGT_D, fd, rj, rk));
}

void LoongArch64Emitter::FSTLE_S(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeFdJK(Opcode32::FSTLE_S, fd, rj, rk));
}

void LoongArch64Emitter::FSTLE_D(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeFdJK(Opcode32::FSTLE_D, fd, rj, rk));
}

void LoongArch64Emitter::QuickFLI(int bits, LoongArch64Reg fd, double v, LoongArch64Reg scratchReg) {
	if (bits == 64) {
		LI(scratchReg, v);
		MOVGR2FR_D(fd, scratchReg);
	} else if (bits <= 32) {
		QuickFLI(32, fd, (float)v, scratchReg);
	} else {
		_assert_msg_(false, "Unsupported QuickFLI bits");
	}
}

void LoongArch64Emitter::QuickFLI(int bits, LoongArch64Reg fd, uint32_t pattern, LoongArch64Reg scratchReg) {
	if (bits == 32) {
		LI(scratchReg, (int32_t)pattern);
		MOVGR2FR_W(fd, scratchReg);
	} else {
		_assert_msg_(false, "Unsupported QuickFLI bits");
	}
}

void LoongArch64Emitter::QuickFLI(int bits, LoongArch64Reg fd, float v, LoongArch64Reg scratchReg) {
	if (bits == 64) {
		QuickFLI(32, fd, (double)v, scratchReg);
	} else if (bits == 32) {
		LI(scratchReg, v);
		MOVGR2FR_W(fd, scratchReg);
	} else {
		_assert_msg_(false, "Unsupported QuickFLI bits");
	}
}

void LoongArch64Emitter::VFMADD_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va) {
    Write32(EncodeVdVjVkVa(Opcode32::VFMADD_S, vd, vj, vk, va));
}

void LoongArch64Emitter::VFMADD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va) {
	Write32(EncodeVdVjVkVa(Opcode32::VFMADD_D, vd, vj, vk, va));
}

void LoongArch64Emitter::VFMSUB_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va) {
	Write32(EncodeVdVjVkVa(Opcode32::VFMSUB_S, vd, vj, vk, va));
}

void LoongArch64Emitter::VFMSUB_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va) {
	Write32(EncodeVdVjVkVa(Opcode32::VFMSUB_D, vd, vj, vk, va));
}

void LoongArch64Emitter::VFNMADD_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va) {
	Write32(EncodeVdVjVkVa(Opcode32::VFNMADD_S, vd, vj, vk, va));
}

void LoongArch64Emitter::VFNMADD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va) {
	Write32(EncodeVdVjVkVa(Opcode32::VFNMADD_D, vd, vj, vk, va));
}

void LoongArch64Emitter::VFNMSUB_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va) {
	Write32(EncodeVdVjVkVa(Opcode32::VFNMSUB_S, vd, vj, vk, va));
}

void LoongArch64Emitter::VFNMSUB_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va) {
	Write32(EncodeVdVjVkVa(Opcode32::VFNMSUB_D, vd, vj, vk, va));
}

void LoongArch64Emitter::VFCMP_CAF_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CAF_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SAF_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SAF_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CLT_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CLT_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SLT_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SLT_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CEQ_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CEQ_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SEQ_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SEQ_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CLE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CLE_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SLE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SLE_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CUN_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CUN_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SUN_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SUN_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CULT_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CULT_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SULT_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SULT_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CUEQ_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CUEQ_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SUEQ_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SUEQ_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CULE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CULE_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SULE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SULE_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CNE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CNE_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SNE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SNE_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_COR_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_COR_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SOR_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SOR_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CUNE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CUNE_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SUNE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SUNE_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CAF_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CAF_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SAF_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SAF_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CLT_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CLT_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SLT_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SLT_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CEQ_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CEQ_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SEQ_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SEQ_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CLE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CLE_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SLE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SLE_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CUN_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CUN_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SUN_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SUN_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CULT_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CULT_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SULT_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SULT_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CUEQ_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CUEQ_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SUEQ_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SUEQ_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CULE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CULE_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SULE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SULE_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CNE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CNE_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SNE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SNE_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_COR_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_COR_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SOR_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SOR_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_CUNE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_CUNE_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCMP_SUNE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCMP_SUNE_D, vd, vj, vk));
}

void LoongArch64Emitter::VBITSEL_V(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va) {
	Write32(EncodeVdVjVkVa(Opcode32::VBITSEL_V, vd, vj, vk, va));
}

void LoongArch64Emitter::VSHUF_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va) {
	Write32(EncodeVdVjVkVa(Opcode32::VSHUF_B, vd, vj, vk, va));
}

void LoongArch64Emitter::VLD(LoongArch64Reg vd, LoongArch64Reg rj, s16 si12) {
	Write32(EncodeVdJSk12(Opcode32::VLD, vd, rj, si12));
}

void LoongArch64Emitter::VST(LoongArch64Reg vd, LoongArch64Reg rj, s16 si12) {
	Write32(EncodeVdJSk12(Opcode32::VST, vd, rj, si12));
}

void LoongArch64Emitter::VLDREPL_D(LoongArch64Reg vd, LoongArch64Reg rj, s16 si9) {
	Write32(EncodeVdJSk9(Opcode32::VLDREPL_D, vd, rj, si9));
}

void LoongArch64Emitter::VLDREPL_W(LoongArch64Reg vd, LoongArch64Reg rj, s16 si10) {
	Write32(EncodeVdJSk10(Opcode32::VLDREPL_W, vd, rj, si10));
}

void LoongArch64Emitter::VLDREPL_H(LoongArch64Reg vd, LoongArch64Reg rj, s16 si11) {
	Write32(EncodeVdJSk11(Opcode32::VLDREPL_H, vd, rj, si11));
}

void LoongArch64Emitter::VLDREPL_B(LoongArch64Reg vd, LoongArch64Reg rj, s16 si12) {
	Write32(EncodeVdJSk12(Opcode32::VLDREPL_B, vd, rj, si12));
}

void LoongArch64Emitter::VSTELM_D(LoongArch64Reg vd, LoongArch64Reg rj, s16 si8, u8 idx1) {
	Write32(EncodeVdJSk8Un1(Opcode32::VSTELM_D, vd, rj, si8, idx1));
}

void LoongArch64Emitter::VSTELM_W(LoongArch64Reg vd, LoongArch64Reg rj, s16 si8, u8 idx2) {
	Write32(EncodeVdJSk8Un2(Opcode32::VSTELM_W, vd, rj, si8, idx2));
}

void LoongArch64Emitter::VSTELM_H(LoongArch64Reg vd, LoongArch64Reg rj, s16 si8, u8 idx3) {
	Write32(EncodeVdJSk8Un3(Opcode32::VSTELM_H, vd, rj, si8, idx3));
}

void LoongArch64Emitter::VSTELM_B(LoongArch64Reg vd, LoongArch64Reg rj, s16 si8, u8 idx4) {
	Write32(EncodeVdJSk8Un4(Opcode32::VSTELM_B, vd, rj, si8, idx4));
}

void LoongArch64Emitter::VLDX(LoongArch64Reg vd, LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeVdJK(Opcode32::VLDX, vd, rj, rk));
}

void LoongArch64Emitter::VSTX(LoongArch64Reg vd, LoongArch64Reg rj, LoongArch64Reg rk) {
    Write32(EncodeVdJK(Opcode32::VSTX, vd, rj, rk));
}

void LoongArch64Emitter::VSEQ_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSEQ_B, vd, vj, vk));
}

void LoongArch64Emitter::VSEQ_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSEQ_H, vd, vj, vk));
}

void LoongArch64Emitter::VSEQ_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSEQ_W, vd, vj, vk));
}

void LoongArch64Emitter::VSEQ_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSEQ_D, vd, vj, vk));
}

void LoongArch64Emitter::VSLE_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLE_B, vd, vj, vk));
}

void LoongArch64Emitter::VSLE_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLE_H, vd, vj, vk));
}

void LoongArch64Emitter::VSLE_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLE_W, vd, vj, vk));
}

void LoongArch64Emitter::VSLE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLE_D, vd, vj, vk));
}

void LoongArch64Emitter::VSLE_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLE_BU, vd, vj, vk));
}

void LoongArch64Emitter::VSLE_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLE_HU, vd, vj, vk));
}

void LoongArch64Emitter::VSLE_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLE_WU, vd, vj, vk));
}

void LoongArch64Emitter::VSLE_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLE_DU, vd, vj, vk));
}

void LoongArch64Emitter::VSLT_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLT_B, vd, vj, vk));
}

void LoongArch64Emitter::VSLT_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLT_H, vd, vj, vk));
}

void LoongArch64Emitter::VSLT_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLT_W, vd, vj, vk));
}

void LoongArch64Emitter::VSLT_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLT_D, vd, vj, vk));
}

void LoongArch64Emitter::VSLT_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLT_BU, vd, vj, vk));
}

void LoongArch64Emitter::VSLT_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLT_HU, vd, vj, vk));
}

void LoongArch64Emitter::VSLT_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLT_WU, vd, vj, vk));
}

void LoongArch64Emitter::VSLT_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLT_DU, vd, vj, vk));
}

void LoongArch64Emitter::VADD_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADD_B, vd, vj, vk));
}

void LoongArch64Emitter::VADD_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADD_H, vd, vj, vk));
}

void LoongArch64Emitter::VADD_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADD_W, vd, vj, vk));
}

void LoongArch64Emitter::VADD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADD_D, vd, vj, vk));
}

void LoongArch64Emitter::VSUB_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUB_B, vd, vj, vk));
}

void LoongArch64Emitter::VSUB_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUB_H, vd, vj, vk));
}

void LoongArch64Emitter::VSUB_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUB_W, vd, vj, vk));
}

void LoongArch64Emitter::VSUB_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUB_D, vd, vj, vk));
}

void LoongArch64Emitter::VADDWEV_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWEV_H_B, vd, vj, vk));
}

void LoongArch64Emitter::VADDWEV_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWEV_W_H, vd, vj, vk));
}

void LoongArch64Emitter::VADDWEV_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWEV_D_W, vd, vj, vk));
}

void LoongArch64Emitter::VADDWEV_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWEV_Q_D, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWEV_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWEV_H_B, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWEV_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWEV_W_H, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWEV_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWEV_D_W, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWEV_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWEV_Q_D, vd, vj, vk));
}

void LoongArch64Emitter::VADDWOD_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWOD_H_B, vd, vj, vk));
}

void LoongArch64Emitter::VADDWOD_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWOD_W_H, vd, vj, vk));
}

void LoongArch64Emitter::VADDWOD_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWOD_D_W, vd, vj, vk));
}

void LoongArch64Emitter::VADDWOD_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWOD_Q_D, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWOD_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWOD_H_B, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWOD_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWOD_W_H, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWOD_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWOD_D_W, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWOD_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWOD_Q_D, vd, vj, vk));
}

void LoongArch64Emitter::VADDWEV_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWEV_H_BU, vd, vj, vk));
}

void LoongArch64Emitter::VADDWEV_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWEV_W_HU, vd, vj, vk));
}

void LoongArch64Emitter::VADDWEV_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWEV_D_WU, vd, vj, vk));
}

void LoongArch64Emitter::VADDWEV_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWEV_Q_DU, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWEV_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWEV_H_BU, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWEV_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWEV_W_HU, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWEV_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWEV_D_WU, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWEV_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWEV_Q_DU, vd, vj, vk));
}

void LoongArch64Emitter::VADDWOD_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWOD_H_BU, vd, vj, vk));
}

void LoongArch64Emitter::VADDWOD_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWOD_W_HU, vd, vj, vk));
}

void LoongArch64Emitter::VADDWOD_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWOD_D_WU, vd, vj, vk));
}

void LoongArch64Emitter::VADDWOD_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWOD_Q_DU, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWOD_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWOD_H_BU, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWOD_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWOD_W_HU, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWOD_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWOD_D_WU, vd, vj, vk));
}

void LoongArch64Emitter::VSUBWOD_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUBWOD_Q_DU, vd, vj, vk));
}

void LoongArch64Emitter::VADDWEV_H_BU_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWEV_H_BU_B, vd, vj, vk));
}

void LoongArch64Emitter::VADDWEV_W_HU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWEV_W_HU_H, vd, vj, vk));
}

void LoongArch64Emitter::VADDWEV_D_WU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWEV_D_WU_W, vd, vj, vk));
}

void LoongArch64Emitter::VADDWEV_Q_DU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWEV_Q_DU_D, vd, vj, vk));
}

void LoongArch64Emitter::VADDWOD_H_BU_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWOD_H_BU_B, vd, vj, vk));
}

void LoongArch64Emitter::VADDWOD_W_HU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWOD_W_HU_H, vd, vj, vk));
}

void LoongArch64Emitter::VADDWOD_D_WU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWOD_D_WU_W, vd, vj, vk));
}

void LoongArch64Emitter::VADDWOD_Q_DU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDWOD_Q_DU_D, vd, vj, vk));
}

void LoongArch64Emitter::VSADD_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSADD_B, vd, vj, vk));
}

void LoongArch64Emitter::VSADD_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSADD_H, vd, vj, vk));
}

void LoongArch64Emitter::VSADD_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSADD_W, vd, vj, vk));
}

void LoongArch64Emitter::VSADD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSADD_D, vd, vj, vk));
}

void LoongArch64Emitter::VSSUB_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSUB_B, vd, vj, vk));
}

void LoongArch64Emitter::VSSUB_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSUB_H, vd, vj, vk));
}

void LoongArch64Emitter::VSSUB_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSUB_W, vd, vj, vk));
}

void LoongArch64Emitter::VSSUB_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSUB_D, vd, vj, vk));
}

void LoongArch64Emitter::VSADD_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSADD_BU, vd, vj, vk));
}

void LoongArch64Emitter::VSADD_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSADD_HU, vd, vj, vk));
}

void LoongArch64Emitter::VSADD_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSADD_WU, vd, vj, vk));
}

void LoongArch64Emitter::VSADD_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSADD_DU, vd, vj, vk));
}

void LoongArch64Emitter::VSSUB_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSUB_BU, vd, vj, vk));
}

void LoongArch64Emitter::VSSUB_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSUB_HU, vd, vj, vk));
}

void LoongArch64Emitter::VSSUB_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSUB_WU, vd, vj, vk));
}

void LoongArch64Emitter::VSSUB_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSUB_DU, vd, vj, vk));
}

void LoongArch64Emitter::VHADDW_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHADDW_H_B, vd, vj, vk));
}

void LoongArch64Emitter::VHADDW_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHADDW_W_H, vd, vj, vk));
}

void LoongArch64Emitter::VHADDW_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHADDW_D_W, vd, vj, vk));
}

void LoongArch64Emitter::VHADDW_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHADDW_Q_D, vd, vj, vk));
}

void LoongArch64Emitter::VHSUBW_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHSUBW_H_B, vd, vj, vk));
}

void LoongArch64Emitter::VHSUBW_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHSUBW_W_H, vd, vj, vk));
}

void LoongArch64Emitter::VHSUBW_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHSUBW_D_W, vd, vj, vk));
}

void LoongArch64Emitter::VHSUBW_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHSUBW_Q_D, vd, vj, vk));
}

void LoongArch64Emitter::VHADDW_HU_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHADDW_HU_BU, vd, vj, vk));
}

void LoongArch64Emitter::VHADDW_WU_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHADDW_WU_HU, vd, vj, vk));
}

void LoongArch64Emitter::VHADDW_DU_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHADDW_DU_WU, vd, vj, vk));
}

void LoongArch64Emitter::VHADDW_QU_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHADDW_QU_DU, vd, vj, vk));
}

void LoongArch64Emitter::VHSUBW_HU_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHSUBW_HU_BU, vd, vj, vk));
}

void LoongArch64Emitter::VHSUBW_WU_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHSUBW_WU_HU, vd, vj, vk));
}

void LoongArch64Emitter::VHSUBW_DU_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHSUBW_DU_WU, vd, vj, vk));
}

void LoongArch64Emitter::VHSUBW_QU_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VHSUBW_QU_DU, vd, vj, vk));
}

void LoongArch64Emitter::VADDA_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDA_B, vd, vj, vk));
}

void LoongArch64Emitter::VADDA_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDA_H, vd, vj, vk));
}

void LoongArch64Emitter::VADDA_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDA_W, vd, vj, vk));
}

void LoongArch64Emitter::VADDA_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADDA_D, vd, vj, vk));
}

void LoongArch64Emitter::VABSD_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VABSD_B, vd, vj, vk));
}

void LoongArch64Emitter::VABSD_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VABSD_H, vd, vj, vk));
}

void LoongArch64Emitter::VABSD_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VABSD_W, vd, vj, vk));
}

void LoongArch64Emitter::VABSD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VABSD_D, vd, vj, vk));
}

void LoongArch64Emitter::VABSD_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VABSD_BU, vd, vj, vk));
}

void LoongArch64Emitter::VABSD_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VABSD_HU, vd, vj, vk));
}

void LoongArch64Emitter::VABSD_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VABSD_WU, vd, vj, vk));
}

void LoongArch64Emitter::VABSD_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VABSD_DU, vd, vj, vk));
}

void LoongArch64Emitter::VAVG_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVG_B, vd, vj, vk));
}

void LoongArch64Emitter::VAVG_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVG_H, vd, vj, vk));
}

void LoongArch64Emitter::VAVG_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVG_W, vd, vj, vk));
}

void LoongArch64Emitter::VAVG_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVG_D, vd, vj, vk));
}

void LoongArch64Emitter::VAVG_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVG_BU, vd, vj, vk));
}

void LoongArch64Emitter::VAVG_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVG_HU, vd, vj, vk));
}

void LoongArch64Emitter::VAVG_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVG_WU, vd, vj, vk));
}

void LoongArch64Emitter::VAVG_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVG_DU, vd, vj, vk));
}

void LoongArch64Emitter::VAVGR_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVGR_B, vd, vj, vk));
}

void LoongArch64Emitter::VAVGR_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVGR_H, vd, vj, vk));
}

void LoongArch64Emitter::VAVGR_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVGR_W, vd, vj, vk));
}

void LoongArch64Emitter::VAVGR_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVGR_D, vd, vj, vk));
}

void LoongArch64Emitter::VAVGR_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVGR_BU, vd, vj, vk));
}

void LoongArch64Emitter::VAVGR_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVGR_HU, vd, vj, vk));
}

void LoongArch64Emitter::VAVGR_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVGR_WU, vd, vj, vk));
}

void LoongArch64Emitter::VAVGR_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAVGR_DU, vd, vj, vk));
}

void LoongArch64Emitter::VMAX_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMAX_B, vd, vj, vk));
}

void LoongArch64Emitter::VMAX_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMAX_H, vd, vj, vk));
}

void LoongArch64Emitter::VMAX_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMAX_W, vd, vj, vk));
}

void LoongArch64Emitter::VMAX_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMAX_D, vd, vj, vk));
}

void LoongArch64Emitter::VMIN_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMIN_B, vd, vj, vk));
}

void LoongArch64Emitter::VMIN_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMIN_H, vd, vj, vk));
}

void LoongArch64Emitter::VMIN_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMIN_W, vd, vj, vk));
}

void LoongArch64Emitter::VMIN_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMIN_D, vd, vj, vk));
}

void LoongArch64Emitter::VMAX_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMAX_BU, vd, vj, vk));
}

void LoongArch64Emitter::VMAX_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMAX_HU, vd, vj, vk));
}

void LoongArch64Emitter::VMAX_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMAX_WU, vd, vj, vk));
}

void LoongArch64Emitter::VMAX_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMAX_DU, vd, vj, vk));
}

void LoongArch64Emitter::VMIN_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMIN_BU, vd, vj, vk));
}

void LoongArch64Emitter::VMIN_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMIN_HU, vd, vj, vk));
}

void LoongArch64Emitter::VMIN_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMIN_WU, vd, vj, vk));
}

void LoongArch64Emitter::VMIN_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMIN_DU, vd, vj, vk));
}

void LoongArch64Emitter::VMUL_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMUL_B, vd, vj, vk));
}

void LoongArch64Emitter::VMUL_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMUL_H, vd, vj, vk));
}

void LoongArch64Emitter::VMUL_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMUL_W, vd, vj, vk));
}

void LoongArch64Emitter::VMUL_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMUL_D, vd, vj, vk));
}

void LoongArch64Emitter::VMUH_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMUH_B, vd, vj, vk));
}

void LoongArch64Emitter::VMUH_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMUH_H, vd, vj, vk));
}

void LoongArch64Emitter::VMUH_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMUH_W, vd, vj, vk));
}

void LoongArch64Emitter::VMUH_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMUH_D, vd, vj, vk));
}

void LoongArch64Emitter::VMUH_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMUH_BU, vd, vj, vk));
}

void LoongArch64Emitter::VMUH_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMUH_HU, vd, vj, vk));
}

void LoongArch64Emitter::VMUH_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMUH_WU, vd, vj, vk));
}

void LoongArch64Emitter::VMUH_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMUH_DU, vd, vj, vk));
}

void LoongArch64Emitter::VMULWEV_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWEV_H_B, vd, vj, vk));
}

void LoongArch64Emitter::VMULWEV_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWEV_W_H, vd, vj, vk));
}

void LoongArch64Emitter::VMULWEV_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWEV_D_W, vd, vj, vk));
}

void LoongArch64Emitter::VMULWEV_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWEV_Q_D, vd, vj, vk));
}

void LoongArch64Emitter::VMULWOD_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWOD_H_B, vd, vj, vk));
}

void LoongArch64Emitter::VMULWOD_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWOD_W_H, vd, vj, vk));
}

void LoongArch64Emitter::VMULWOD_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWOD_D_W, vd, vj, vk));
}

void LoongArch64Emitter::VMULWOD_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWOD_Q_D, vd, vj, vk));
}

void LoongArch64Emitter::VMULWEV_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWEV_H_BU, vd, vj, vk));
}

void LoongArch64Emitter::VMULWEV_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWEV_W_HU, vd, vj, vk));
}

void LoongArch64Emitter::VMULWEV_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWEV_D_WU, vd, vj, vk));
}

void LoongArch64Emitter::VMULWEV_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWEV_Q_DU, vd, vj, vk));
}

void LoongArch64Emitter::VMULWOD_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWOD_H_BU, vd, vj, vk));
}

void LoongArch64Emitter::VMULWOD_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWOD_W_HU, vd, vj, vk));
}

void LoongArch64Emitter::VMULWOD_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWOD_D_WU, vd, vj, vk));
}

void LoongArch64Emitter::VMULWOD_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWOD_Q_DU, vd, vj, vk));
}

void LoongArch64Emitter::VMULWEV_H_BU_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWEV_H_BU_B, vd, vj, vk));
}

void LoongArch64Emitter::VMULWEV_W_HU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWEV_W_HU_H, vd, vj, vk));
}

void LoongArch64Emitter::VMULWEV_D_WU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWEV_D_WU_W, vd, vj, vk));
}

void LoongArch64Emitter::VMULWEV_Q_DU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWEV_Q_DU_D, vd, vj, vk));
}

void LoongArch64Emitter::VMULWOD_H_BU_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWOD_H_BU_B, vd, vj, vk));
}

void LoongArch64Emitter::VMULWOD_W_HU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWOD_W_HU_H, vd, vj, vk));
}

void LoongArch64Emitter::VMULWOD_D_WU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWOD_D_WU_W, vd, vj, vk));
}

void LoongArch64Emitter::VMULWOD_Q_DU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMULWOD_Q_DU_D, vd, vj, vk));
}

void LoongArch64Emitter::VMADD_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADD_B, vd, vj, vk));
}

void LoongArch64Emitter::VMADD_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADD_H, vd, vj, vk));
}

void LoongArch64Emitter::VMADD_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADD_W, vd, vj, vk));
}

void LoongArch64Emitter::VMADD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADD_D, vd, vj, vk));
}

void LoongArch64Emitter::VMSUB_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMSUB_B, vd, vj, vk));
}

void LoongArch64Emitter::VMSUB_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMSUB_H, vd, vj, vk));
}

void LoongArch64Emitter::VMSUB_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMSUB_W, vd, vj, vk));
}

void LoongArch64Emitter::VMSUB_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMSUB_D, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWEV_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWEV_H_B, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWEV_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWEV_W_H, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWEV_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWEV_D_W, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWEV_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWEV_Q_D, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWOD_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWOD_H_B, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWOD_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWOD_W_H, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWOD_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWOD_D_W, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWOD_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWOD_Q_D, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWEV_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWEV_H_BU, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWEV_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWEV_W_HU, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWEV_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWEV_D_WU, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWEV_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWEV_Q_DU, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWOD_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWOD_H_BU, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWOD_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWOD_W_HU, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWOD_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWOD_D_WU, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWOD_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWOD_Q_DU, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWEV_H_BU_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWEV_H_BU_B, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWEV_W_HU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWEV_W_HU_H, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWEV_D_WU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWEV_D_WU_W, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWEV_Q_DU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWEV_Q_DU_D, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWOD_H_BU_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWOD_H_BU_B, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWOD_W_HU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWOD_W_HU_H, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWOD_D_WU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWOD_D_WU_W, vd, vj, vk));
}

void LoongArch64Emitter::VMADDWOD_Q_DU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMADDWOD_Q_DU_D, vd, vj, vk));
}

void LoongArch64Emitter::VDIV_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VDIV_B, vd, vj, vk));
}

void LoongArch64Emitter::VDIV_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VDIV_H, vd, vj, vk));
}

void LoongArch64Emitter::VDIV_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VDIV_W, vd, vj, vk));
}

void LoongArch64Emitter::VDIV_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VDIV_D, vd, vj, vk));
}

void LoongArch64Emitter::VMOD_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMOD_B, vd, vj, vk));
}

void LoongArch64Emitter::VMOD_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMOD_H, vd, vj, vk));
}

void LoongArch64Emitter::VMOD_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMOD_W, vd, vj, vk));
}

void LoongArch64Emitter::VMOD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMOD_D, vd, vj, vk));
}

void LoongArch64Emitter::VDIV_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VDIV_BU, vd, vj, vk));
}

void LoongArch64Emitter::VDIV_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VDIV_HU, vd, vj, vk));
}

void LoongArch64Emitter::VDIV_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VDIV_WU, vd, vj, vk));
}

void LoongArch64Emitter::VDIV_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VDIV_DU, vd, vj, vk));
}

void LoongArch64Emitter::VMOD_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMOD_BU, vd, vj, vk));
}

void LoongArch64Emitter::VMOD_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMOD_HU, vd, vj, vk));
}

void LoongArch64Emitter::VMOD_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMOD_WU, vd, vj, vk));
}

void LoongArch64Emitter::VMOD_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VMOD_DU, vd, vj, vk));
}

void LoongArch64Emitter::VSLL_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLL_B, vd, vj, vk));
}

void LoongArch64Emitter::VSLL_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLL_H, vd, vj, vk));
}

void LoongArch64Emitter::VSLL_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLL_W, vd, vj, vk));
}

void LoongArch64Emitter::VSLL_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSLL_D, vd, vj, vk));
}

void LoongArch64Emitter::VSRL_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRL_B, vd, vj, vk));
}

void LoongArch64Emitter::VSRL_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRL_H, vd, vj, vk));
}

void LoongArch64Emitter::VSRL_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRL_W, vd, vj, vk));
}

void LoongArch64Emitter::VSRL_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRL_D, vd, vj, vk));
}

void LoongArch64Emitter::VSRA_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRA_B, vd, vj, vk));
}

void LoongArch64Emitter::VSRA_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRA_H, vd, vj, vk));
}

void LoongArch64Emitter::VSRA_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRA_W, vd, vj, vk));
}

void LoongArch64Emitter::VSRA_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRA_D, vd, vj, vk));
}

void LoongArch64Emitter::VROTR_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VROTR_B, vd, vj, vk));
}

void LoongArch64Emitter::VROTR_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VROTR_H, vd, vj, vk));
}

void LoongArch64Emitter::VROTR_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VROTR_W, vd, vj, vk));
}

void LoongArch64Emitter::VROTR_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VROTR_D, vd, vj, vk));
}

void LoongArch64Emitter::VSRLR_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRLR_B, vd, vj, vk));
}

void LoongArch64Emitter::VSRLR_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRLR_H, vd, vj, vk));
}

void LoongArch64Emitter::VSRLR_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRLR_W, vd, vj, vk));
}

void LoongArch64Emitter::VSRLR_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRLR_D, vd, vj, vk));
}

void LoongArch64Emitter::VSRAR_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRAR_B, vd, vj, vk));
}

void LoongArch64Emitter::VSRAR_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRAR_H, vd, vj, vk));
}

void LoongArch64Emitter::VSRAR_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRAR_W, vd, vj, vk));
}

void LoongArch64Emitter::VSRAR_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRAR_D, vd, vj, vk));
}

void LoongArch64Emitter::VSRLN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRLN_B_H, vd, vj, vk));
}

void LoongArch64Emitter::VSRLN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRLN_H_W, vd, vj, vk));
}

void LoongArch64Emitter::VSRLN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRLN_W_D, vd, vj, vk));
}

void LoongArch64Emitter::VSRAN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRAN_B_H, vd, vj, vk));
}

void LoongArch64Emitter::VSRAN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRAN_H_W, vd, vj, vk));
}

void LoongArch64Emitter::VSRAN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRAN_W_D, vd, vj, vk));
}

void LoongArch64Emitter::VSRLRN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRLRN_B_H, vd, vj, vk));
}

void LoongArch64Emitter::VSRLRN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRLRN_H_W, vd, vj, vk));
}

void LoongArch64Emitter::VSRLRN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRLRN_W_D, vd, vj, vk));
}

void LoongArch64Emitter::VSRARN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRARN_B_H, vd, vj, vk));
}

void LoongArch64Emitter::VSRARN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRARN_H_W, vd, vj, vk));
}

void LoongArch64Emitter::VSRARN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSRARN_W_D, vd, vj, vk));
}

void LoongArch64Emitter::VSSRLN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRLN_B_H, vd, vj, vk));
}

void LoongArch64Emitter::VSSRLN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRLN_H_W, vd, vj, vk));
}

void LoongArch64Emitter::VSSRLN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRLN_W_D, vd, vj, vk));
}

void LoongArch64Emitter::VSSRAN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRAN_B_H, vd, vj, vk));
}

void LoongArch64Emitter::VSSRAN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRAN_H_W, vd, vj, vk));
}

void LoongArch64Emitter::VSSRAN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRAN_W_D, vd, vj, vk));
}

void LoongArch64Emitter::VSSRLRN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRLRN_B_H, vd, vj, vk));
}

void LoongArch64Emitter::VSSRLRN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRLRN_H_W, vd, vj, vk));
}

void LoongArch64Emitter::VSSRLRN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRLRN_W_D, vd, vj, vk));
}

void LoongArch64Emitter::VSSRARN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRARN_B_H, vd, vj, vk));
}

void LoongArch64Emitter::VSSRARN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRARN_H_W, vd, vj, vk));
}

void LoongArch64Emitter::VSSRARN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRARN_W_D, vd, vj, vk));
}

void LoongArch64Emitter::VSSRLN_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRLN_BU_H, vd, vj, vk));
}

void LoongArch64Emitter::VSSRLN_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRLN_HU_W, vd, vj, vk));
}

void LoongArch64Emitter::VSSRLN_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRLN_WU_D, vd, vj, vk));
}

void LoongArch64Emitter::VSSRAN_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRAN_BU_H, vd, vj, vk));
}

void LoongArch64Emitter::VSSRAN_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRAN_HU_W, vd, vj, vk));
}

void LoongArch64Emitter::VSSRAN_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRAN_WU_D, vd, vj, vk));
}

void LoongArch64Emitter::VSSRLRN_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRLRN_BU_H, vd, vj, vk));
}

void LoongArch64Emitter::VSSRLRN_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRLRN_HU_W, vd, vj, vk));
}

void LoongArch64Emitter::VSSRLRN_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRLRN_WU_D, vd, vj, vk));
}

void LoongArch64Emitter::VSSRARN_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRARN_BU_H, vd, vj, vk));
}

void LoongArch64Emitter::VSSRARN_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRARN_HU_W, vd, vj, vk));
}

void LoongArch64Emitter::VSSRARN_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSSRARN_WU_D, vd, vj, vk));
}

void LoongArch64Emitter::VBITCLR_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VBITCLR_B, vd, vj, vk));
}

void LoongArch64Emitter::VBITCLR_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VBITCLR_H, vd, vj, vk));
}

void LoongArch64Emitter::VBITCLR_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VBITCLR_W, vd, vj, vk));
}

void LoongArch64Emitter::VBITCLR_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VBITCLR_D, vd, vj, vk));
}

void LoongArch64Emitter::VBITSET_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VBITSET_B, vd, vj, vk));
}

void LoongArch64Emitter::VBITSET_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VBITSET_H, vd, vj, vk));
}

void LoongArch64Emitter::VBITSET_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VBITSET_W, vd, vj, vk));
}

void LoongArch64Emitter::VBITSET_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VBITSET_D, vd, vj, vk));
}

void LoongArch64Emitter::VBITREV_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VBITREV_B, vd, vj, vk));
}

void LoongArch64Emitter::VBITREV_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VBITREV_H, vd, vj, vk));
}

void LoongArch64Emitter::VBITREV_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VBITREV_W, vd, vj, vk));
}

void LoongArch64Emitter::VBITREV_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VBITREV_D, vd, vj, vk));
}

void LoongArch64Emitter::VPACKEV_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPACKEV_B, vd, vj, vk));
}

void LoongArch64Emitter::VPACKEV_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPACKEV_H, vd, vj, vk));
}

void LoongArch64Emitter::VPACKEV_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPACKEV_W, vd, vj, vk));
}

void LoongArch64Emitter::VPACKEV_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPACKEV_D, vd, vj, vk));
}

void LoongArch64Emitter::VPACKOD_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPACKOD_B, vd, vj, vk));
}

void LoongArch64Emitter::VPACKOD_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPACKOD_H, vd, vj, vk));
}

void LoongArch64Emitter::VPACKOD_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPACKOD_W, vd, vj, vk));
}

void LoongArch64Emitter::VPACKOD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPACKOD_D, vd, vj, vk));
}

void LoongArch64Emitter::VILVL_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VILVL_B, vd, vj, vk));
}

void LoongArch64Emitter::VILVL_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VILVL_H, vd, vj, vk));
}

void LoongArch64Emitter::VILVL_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VILVL_W, vd, vj, vk));
}

void LoongArch64Emitter::VILVL_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VILVL_D, vd, vj, vk));
}

void LoongArch64Emitter::VILVH_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VILVH_B, vd, vj, vk));
}

void LoongArch64Emitter::VILVH_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VILVH_H, vd, vj, vk));
}

void LoongArch64Emitter::VILVH_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VILVH_W, vd, vj, vk));
}

void LoongArch64Emitter::VILVH_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VILVH_D, vd, vj, vk));
}

void LoongArch64Emitter::VPICKEV_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPICKEV_B, vd, vj, vk));
}

void LoongArch64Emitter::VPICKEV_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPICKEV_H, vd, vj, vk));
}

void LoongArch64Emitter::VPICKEV_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPICKEV_W, vd, vj, vk));
}

void LoongArch64Emitter::VPICKEV_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPICKEV_D, vd, vj, vk));
}

void LoongArch64Emitter::VPICKOD_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPICKOD_B, vd, vj, vk));
}

void LoongArch64Emitter::VPICKOD_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPICKOD_H, vd, vj, vk));
}

void LoongArch64Emitter::VPICKOD_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPICKOD_W, vd, vj, vk));
}

void LoongArch64Emitter::VPICKOD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VPICKOD_D, vd, vj, vk));
}

void LoongArch64Emitter::VREPLVE_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg rk) {
	Write32(EncodeVdVjK(Opcode32::VREPLVE_B, vd, vj, rk));
}

void LoongArch64Emitter::VREPLVE_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg rk) {
	Write32(EncodeVdVjK(Opcode32::VREPLVE_H, vd, vj, rk));
}

void LoongArch64Emitter::VREPLVE_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg rk) {
	Write32(EncodeVdVjK(Opcode32::VREPLVE_W, vd, vj, rk));
}

void LoongArch64Emitter::VREPLVE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg rk) {
	Write32(EncodeVdVjK(Opcode32::VREPLVE_D, vd, vj, rk));
}

void LoongArch64Emitter::VAND_V(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VAND_V, vd, vj, vk));
}

void LoongArch64Emitter::VOR_V(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VOR_V, vd, vj, vk));
}

void LoongArch64Emitter::VXOR_V(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VXOR_V, vd, vj, vk));
}

void LoongArch64Emitter::VNOR_V(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VNOR_V, vd, vj, vk));
}

void LoongArch64Emitter::VANDN_V(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VANDN_V, vd, vj, vk));
}

void LoongArch64Emitter::VORN_V(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VORN_V, vd, vj, vk));
}

void LoongArch64Emitter::VFRSTP_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFRSTP_B, vd, vj, vk));
}

void LoongArch64Emitter::VFRSTP_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFRSTP_H, vd, vj, vk));
}

void LoongArch64Emitter::VADD_Q(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VADD_Q, vd, vj, vk));
}

void LoongArch64Emitter::VSUB_Q(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSUB_Q, vd, vj, vk));
}

void LoongArch64Emitter::VSIGNCOV_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSIGNCOV_B, vd, vj, vk));
}

void LoongArch64Emitter::VSIGNCOV_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSIGNCOV_H, vd, vj, vk));
}

void LoongArch64Emitter::VSIGNCOV_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSIGNCOV_W, vd, vj, vk));
}

void LoongArch64Emitter::VSIGNCOV_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSIGNCOV_D, vd, vj, vk));
}

void LoongArch64Emitter::VFADD_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFADD_S, vd, vj, vk));
}

void LoongArch64Emitter::VFADD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFADD_D, vd, vj, vk));
}

void LoongArch64Emitter::VFSUB_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFSUB_S, vd, vj, vk));
}

void LoongArch64Emitter::VFSUB_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFSUB_D, vd, vj, vk));
}

void LoongArch64Emitter::VFMUL_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFMUL_S, vd, vj, vk));
}

void LoongArch64Emitter::VFMUL_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFMUL_D, vd, vj, vk));
}

void LoongArch64Emitter::VFDIV_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFDIV_S, vd, vj, vk));
}

void LoongArch64Emitter::VFDIV_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFDIV_D, vd, vj, vk));
}

void LoongArch64Emitter::VFMAX_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFMAX_S, vd, vj, vk));
}

void LoongArch64Emitter::VFMAX_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFMAX_D, vd, vj, vk));
}

void LoongArch64Emitter::VFMIN_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFMIN_S, vd, vj, vk));
}

void LoongArch64Emitter::VFMIN_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFMIN_D, vd, vj, vk));
}

void LoongArch64Emitter::VFMAXA_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFMAXA_S, vd, vj, vk));
}

void LoongArch64Emitter::VFMAXA_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFMAXA_D, vd, vj, vk));
}

void LoongArch64Emitter::VFMINA_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFMINA_S, vd, vj, vk));
}

void LoongArch64Emitter::VFMINA_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFMINA_D, vd, vj, vk));
}

void LoongArch64Emitter::VFCVT_H_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCVT_H_S, vd, vj, vk));
}

void LoongArch64Emitter::VFCVT_S_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFCVT_S_D, vd, vj, vk));
}

void LoongArch64Emitter::VFFINT_S_L(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFFINT_S_L, vd, vj, vk));
}

void LoongArch64Emitter::VFTINT_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFTINT_W_D, vd, vj, vk));
}

void LoongArch64Emitter::VFTINTRM_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFTINTRM_W_D, vd, vj, vk));
}

void LoongArch64Emitter::VFTINTRP_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFTINTRP_W_D, vd, vj, vk));
}

void LoongArch64Emitter::VFTINTRZ_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFTINTRZ_W_D, vd, vj, vk));
}

void LoongArch64Emitter::VFTINTRNE_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VFTINTRNE_W_D, vd, vj, vk));
}

void LoongArch64Emitter::VSHUF_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSHUF_H, vd, vj, vk));
}

void LoongArch64Emitter::VSHUF_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSHUF_W, vd, vj, vk));
}

void LoongArch64Emitter::VSHUF_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk) {
	Write32(EncodeVdVjVk(Opcode32::VSHUF_D, vd, vj, vk));
}

void LoongArch64Emitter::VSEQI_B(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VSEQI_B, vd, vj, si5));
}

void LoongArch64Emitter::VSEQI_H(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VSEQI_H, vd, vj, si5));
}

void LoongArch64Emitter::VSEQI_W(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VSEQI_W, vd, vj, si5));
}

void LoongArch64Emitter::VSEQI_D(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VSEQI_D, vd, vj, si5));
}

void LoongArch64Emitter::VSLEI_B(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VSLEI_B, vd, vj, si5));
}

void LoongArch64Emitter::VSLEI_H(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VSLEI_H, vd, vj, si5));
}

void LoongArch64Emitter::VSLEI_W(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VSLEI_W, vd, vj, si5));
}

void LoongArch64Emitter::VSLEI_D(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VSLEI_D, vd, vj, si5));
}

void LoongArch64Emitter::VSLEI_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSLEI_BU, vd, vj, ui5));
}

void LoongArch64Emitter::VSLEI_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSLEI_HU, vd, vj, ui5));
}

void LoongArch64Emitter::VSLEI_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSLEI_WU, vd, vj, ui5));
}

void LoongArch64Emitter::VSLEI_DU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSLEI_DU, vd, vj, ui5));
}

void LoongArch64Emitter::VSLTI_B(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VSLTI_B, vd, vj, si5));
}

void LoongArch64Emitter::VSLTI_H(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VSLTI_H, vd, vj, si5));
}

void LoongArch64Emitter::VSLTI_W(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VSLTI_W, vd, vj, si5));
}

void LoongArch64Emitter::VSLTI_D(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VSLTI_D, vd, vj, si5));
}

void LoongArch64Emitter::VSLTI_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSLTI_BU, vd, vj, ui5));
}

void LoongArch64Emitter::VSLTI_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSLTI_HU, vd, vj, ui5));
}

void LoongArch64Emitter::VSLTI_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSLTI_WU, vd, vj, ui5));
}

void LoongArch64Emitter::VSLTI_DU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSLTI_DU, vd, vj, ui5));
}

void LoongArch64Emitter::VADDI_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VADDI_BU, vd, vj, ui5));
}

void LoongArch64Emitter::VADDI_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VADDI_HU, vd, vj, ui5));
}

void LoongArch64Emitter::VADDI_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VADDI_WU, vd, vj, ui5));
}

void LoongArch64Emitter::VADDI_DU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VADDI_DU, vd, vj, ui5));
}

void LoongArch64Emitter::VSUBI_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSUBI_BU, vd, vj, ui5));
}

void LoongArch64Emitter::VSUBI_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSUBI_HU, vd, vj, ui5));
}

void LoongArch64Emitter::VSUBI_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSUBI_WU, vd, vj, ui5));
}

void LoongArch64Emitter::VSUBI_DU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSUBI_DU, vd, vj, ui5));
}

void LoongArch64Emitter::VBSLL_V(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VBSLL_V, vd, vj, ui5));
}

void LoongArch64Emitter::VBSRL_V(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VBSRL_V, vd, vj, ui5));
}

void LoongArch64Emitter::VMAXI_B(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VMAXI_B, vd, vj, si5));
}

void LoongArch64Emitter::VMAXI_H(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VMAXI_H, vd, vj, si5));
}

void LoongArch64Emitter::VMAXI_W(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VMAXI_W, vd, vj, si5));
}

void LoongArch64Emitter::VMAXI_D(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VMAXI_D, vd, vj, si5));
}

void LoongArch64Emitter::VMINI_B(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VMINI_B, vd, vj, si5));
}

void LoongArch64Emitter::VMINI_H(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VMINI_H, vd, vj, si5));
}

void LoongArch64Emitter::VMINI_W(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VMINI_W, vd, vj, si5));
}

void LoongArch64Emitter::VMINI_D(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5) {
	Write32(EncodeVdVjSk5(Opcode32::VMINI_D, vd, vj, si5));
}

void LoongArch64Emitter::VMAXI_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VMAXI_BU, vd, vj, ui5));
}

void LoongArch64Emitter::VMAXI_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VMAXI_HU, vd, vj, ui5));
}

void LoongArch64Emitter::VMAXI_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VMAXI_WU, vd, vj, ui5));
}

void LoongArch64Emitter::VMAXI_DU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VMAXI_DU, vd, vj, ui5));
}

void LoongArch64Emitter::VMINI_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VMINI_BU, vd, vj, ui5));
}

void LoongArch64Emitter::VMINI_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VMINI_HU, vd, vj, ui5));
}

void LoongArch64Emitter::VMINI_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VMINI_WU, vd, vj, ui5));
}

void LoongArch64Emitter::VMINI_DU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VMINI_DU, vd, vj, ui5));
}

void LoongArch64Emitter::VFRSTPI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VFRSTPI_B, vd, vj, ui5));
}

void LoongArch64Emitter::VFRSTPI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VFRSTPI_H, vd, vj, ui5));
}

void LoongArch64Emitter::VCLO_B(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VCLO_B, vd, vj));
}

void LoongArch64Emitter::VCLO_H(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VCLO_H, vd, vj));
}

void LoongArch64Emitter::VCLO_W(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VCLO_W, vd, vj));
}

void LoongArch64Emitter::VCLO_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VCLO_D, vd, vj));
}

void LoongArch64Emitter::VCLZ_B(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VCLZ_B, vd, vj));
}

void LoongArch64Emitter::VCLZ_H(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VCLZ_H, vd, vj));
}

void LoongArch64Emitter::VCLZ_W(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VCLZ_W, vd, vj));
}

void LoongArch64Emitter::VCLZ_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VCLZ_D, vd, vj));
}

void LoongArch64Emitter::VPCNT_B(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VPCNT_B, vd, vj));
}

void LoongArch64Emitter::VPCNT_H(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VPCNT_H, vd, vj));
}

void LoongArch64Emitter::VPCNT_W(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VPCNT_W, vd, vj));
}

void LoongArch64Emitter::VPCNT_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VPCNT_D, vd, vj));
}

void LoongArch64Emitter::VNEG_B(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VNEG_B, vd, vj));
}

void LoongArch64Emitter::VNEG_H(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VNEG_H, vd, vj));
}

void LoongArch64Emitter::VNEG_W(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VNEG_W, vd, vj));
}

void LoongArch64Emitter::VNEG_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VNEG_D, vd, vj));
}

void LoongArch64Emitter::VMSKLTZ_B(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VMSKLTZ_B, vd, vj));
}

void LoongArch64Emitter::VMSKLTZ_H(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VMSKLTZ_H, vd, vj));
}

void LoongArch64Emitter::VMSKLTZ_W(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VMSKLTZ_W, vd, vj));
}

void LoongArch64Emitter::VMSKLTZ_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VMSKLTZ_D, vd, vj));
}

void LoongArch64Emitter::VMSKGEZ_B(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VMSKGEZ_B, vd, vj));
}

void LoongArch64Emitter::VMSKNZ_B(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VMSKNZ_B, vd, vj));
}

void LoongArch64Emitter::VSETEQZ_V(LoongArch64CFR cd, LoongArch64Reg vj) {
	Write32(EncodeCdVj(Opcode32::VSETEQZ_V, cd, vj));
}

void LoongArch64Emitter::VSETNEZ_V(LoongArch64CFR cd, LoongArch64Reg vj) {
	Write32(EncodeCdVj(Opcode32::VSETNEZ_V, cd, vj));
}

void LoongArch64Emitter::VSETANYEQZ_B(LoongArch64CFR cd, LoongArch64Reg vj) {
	Write32(EncodeCdVj(Opcode32::VSETANYEQZ_B, cd, vj));
}

void LoongArch64Emitter::VSETANYEQZ_H(LoongArch64CFR cd, LoongArch64Reg vj) {
	Write32(EncodeCdVj(Opcode32::VSETANYEQZ_H, cd, vj));
}

void LoongArch64Emitter::VSETANYEQZ_W(LoongArch64CFR cd, LoongArch64Reg vj) {
	Write32(EncodeCdVj(Opcode32::VSETANYEQZ_W, cd, vj));
}

void LoongArch64Emitter::VSETANYEQZ_D(LoongArch64CFR cd, LoongArch64Reg vj) {
	Write32(EncodeCdVj(Opcode32::VSETANYEQZ_D, cd, vj));
}

void LoongArch64Emitter::VSETALLNEZ_B(LoongArch64CFR cd, LoongArch64Reg vj) {
	Write32(EncodeCdVj(Opcode32::VSETALLNEZ_B, cd, vj));
}

void LoongArch64Emitter::VSETALLNEZ_H(LoongArch64CFR cd, LoongArch64Reg vj) {
	Write32(EncodeCdVj(Opcode32::VSETALLNEZ_H, cd, vj));
}

void LoongArch64Emitter::VSETALLNEZ_W(LoongArch64CFR cd, LoongArch64Reg vj) {
	Write32(EncodeCdVj(Opcode32::VSETALLNEZ_W, cd, vj));
}

void LoongArch64Emitter::VSETALLNEZ_D(LoongArch64CFR cd, LoongArch64Reg vj) {
	Write32(EncodeCdVj(Opcode32::VSETALLNEZ_D, cd, vj));
}

void LoongArch64Emitter::VFLOGB_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFLOGB_S, vd, vj));
}

void LoongArch64Emitter::VFLOGB_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFLOGB_D, vd, vj));
}

void LoongArch64Emitter::VFCLASS_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFCLASS_S, vd, vj));
}

void LoongArch64Emitter::VFCLASS_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFCLASS_D, vd, vj));
}

void LoongArch64Emitter::VFSQRT_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFSQRT_S, vd, vj));
}

void LoongArch64Emitter::VFSQRT_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFSQRT_D, vd, vj));
}

void LoongArch64Emitter::VFRECIP_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRECIP_S, vd, vj));
}

void LoongArch64Emitter::VFRECIP_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRECIP_D, vd, vj));
}

void LoongArch64Emitter::VFRSQRT_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRSQRT_S, vd, vj));
}

void LoongArch64Emitter::VFRSQRT_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRSQRT_D, vd, vj));
}

void LoongArch64Emitter::VFRECIPE_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRECIPE_S, vd, vj));
}

void LoongArch64Emitter::VFRECIPE_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRECIPE_D, vd, vj));
}

void LoongArch64Emitter::VFRSQRTE_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRSQRTE_S, vd, vj));
}

void LoongArch64Emitter::VFRSQRTE_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRSQRTE_D, vd, vj));
}

void LoongArch64Emitter::VFRINT_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRINT_S, vd, vj));
}

void LoongArch64Emitter::VFRINT_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRINT_D, vd, vj));
}

void LoongArch64Emitter::VFRINTRM_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRINTRM_S, vd, vj));
}

void LoongArch64Emitter::VFRINTRM_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRINTRM_D, vd, vj));
}

void LoongArch64Emitter::VFRINTRP_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRINTRP_S, vd, vj));
}

void LoongArch64Emitter::VFRINTRP_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRINTRP_D, vd, vj));
}

void LoongArch64Emitter::VFRINTRZ_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRINTRZ_S, vd, vj));
}

void LoongArch64Emitter::VFRINTRZ_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRINTRZ_D, vd, vj));
}

void LoongArch64Emitter::VFRINTRNE_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRINTRNE_S, vd, vj));
}

void LoongArch64Emitter::VFRINTRNE_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFRINTRNE_D, vd, vj));
}

void LoongArch64Emitter::VFCVTL_S_H(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFCVTL_S_H, vd, vj));
}

void LoongArch64Emitter::VFCVTH_S_H(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFCVTH_S_H, vd, vj));
}

void LoongArch64Emitter::VFCVTL_D_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFCVTL_D_S, vd, vj));
}

void LoongArch64Emitter::VFCVTH_D_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFCVTH_D_S, vd, vj));
}

void LoongArch64Emitter::VFFINT_S_W(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFFINT_S_W, vd, vj));
}

void LoongArch64Emitter::VFFINT_S_WU(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFFINT_S_WU, vd, vj));
}

void LoongArch64Emitter::VFFINT_D_L(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFFINT_D_L, vd, vj));
}

void LoongArch64Emitter::VFFINT_D_LU(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFFINT_D_LU, vd, vj));
}

void LoongArch64Emitter::VFFINTL_D_W(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFFINTL_D_W, vd, vj));
}

void LoongArch64Emitter::VFFINTH_D_W(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFFINTH_D_W, vd, vj));
}

void LoongArch64Emitter::VFTINT_W_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINT_W_S, vd, vj));
}

void LoongArch64Emitter::VFTINT_L_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINT_L_D, vd, vj));
}

void LoongArch64Emitter::VFTINTRM_W_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRM_W_S, vd, vj));
}

void LoongArch64Emitter::VFTINTRM_L_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRM_L_D, vd, vj));
}

void LoongArch64Emitter::VFTINTRP_W_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRP_W_S, vd, vj));
}

void LoongArch64Emitter::VFTINTRP_L_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRP_L_D, vd, vj));
}

void LoongArch64Emitter::VFTINTRZ_W_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRZ_W_S, vd, vj));
}

void LoongArch64Emitter::VFTINTRZ_L_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRZ_L_D, vd, vj));
}

void LoongArch64Emitter::VFTINTRNE_W_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRNE_W_S, vd, vj));
}

void LoongArch64Emitter::VFTINTRNE_L_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRNE_L_D, vd, vj));
}

void LoongArch64Emitter::VFTINT_WU_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINT_WU_S, vd, vj));
}

void LoongArch64Emitter::VFTINT_LU_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINT_LU_D, vd, vj));
}

void LoongArch64Emitter::VFTINTRZ_WU_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRZ_WU_S, vd, vj));
}

void LoongArch64Emitter::VFTINTRZ_LU_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRZ_LU_D, vd, vj));
}

void LoongArch64Emitter::VFTINTL_L_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTL_L_S, vd, vj));
}

void LoongArch64Emitter::VFTINTH_L_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTH_L_S, vd, vj));
}

void LoongArch64Emitter::VFTINTRML_L_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRML_L_S, vd, vj));
}

void LoongArch64Emitter::VFTINTRMH_L_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRMH_L_S, vd, vj));
}

void LoongArch64Emitter::VFTINTRPL_L_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRPL_L_S, vd, vj));
}

void LoongArch64Emitter::VFTINTRPH_L_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRPH_L_S, vd, vj));
}

void LoongArch64Emitter::VFTINTRZL_L_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRZL_L_S, vd, vj));
}

void LoongArch64Emitter::VFTINTRZH_L_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRZH_L_S, vd, vj));
}

void LoongArch64Emitter::VFTINTRNEL_L_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRNEL_L_S, vd, vj));
}

void LoongArch64Emitter::VFTINTRNEH_L_S(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VFTINTRNEH_L_S, vd, vj));
}

void LoongArch64Emitter::VEXTH_H_B(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VEXTH_H_B, vd, vj));
}

void LoongArch64Emitter::VEXTH_W_H(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VEXTH_W_H, vd, vj));
}

void LoongArch64Emitter::VEXTH_D_W(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VEXTH_D_W, vd, vj));
}

void LoongArch64Emitter::VEXTH_Q_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VEXTH_Q_D, vd, vj));
}

void LoongArch64Emitter::VEXTH_HU_BU(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VEXTH_HU_BU, vd, vj));
}

void LoongArch64Emitter::VEXTH_WU_HU(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VEXTH_WU_HU, vd, vj));
}

void LoongArch64Emitter::VEXTH_DU_WU(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VEXTH_DU_WU, vd, vj));
}

void LoongArch64Emitter::VEXTH_QU_DU(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VEXTH_QU_DU, vd, vj));
}

void LoongArch64Emitter::VREPLGR2VR_B(LoongArch64Reg vd, LoongArch64Reg rj) {
	Write32(EncodeVdJ(Opcode32::VREPLGR2VR_B, vd, rj));
}

void LoongArch64Emitter::VREPLGR2VR_H(LoongArch64Reg vd, LoongArch64Reg rj) {
	Write32(EncodeVdJ(Opcode32::VREPLGR2VR_H, vd, rj));
}

void LoongArch64Emitter::VREPLGR2VR_W(LoongArch64Reg vd, LoongArch64Reg rj) {
	Write32(EncodeVdJ(Opcode32::VREPLGR2VR_W, vd, rj));
}

void LoongArch64Emitter::VREPLGR2VR_D(LoongArch64Reg vd, LoongArch64Reg rj) {
	Write32(EncodeVdJ(Opcode32::VREPLGR2VR_D, vd, rj));
}

void LoongArch64Emitter::VROTRI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeVdVjUk3(Opcode32::VROTRI_B, vd, vj, ui3));
}

void LoongArch64Emitter::VROTRI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VROTRI_H, vd, vj, ui4));
}

void LoongArch64Emitter::VROTRI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VROTRI_W, vd, vj, ui5));
}

void LoongArch64Emitter::VROTRI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VROTRI_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSRLRI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeVdVjUk3(Opcode32::VSRLRI_B, vd, vj, ui3));
}

void LoongArch64Emitter::VSRLRI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSRLRI_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSRLRI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSRLRI_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSRLRI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSRLRI_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSRARI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeVdVjUk3(Opcode32::VSRARI_B, vd, vj, ui3));
}

void LoongArch64Emitter::VSRARI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSRARI_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSRARI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSRARI_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSRARI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSRARI_D, vd, vj, ui6));
}

void LoongArch64Emitter::VINSGR2VR_B(LoongArch64Reg vd, LoongArch64Reg rj, u8 ui4) {
	Write32(EncodeVdJUk4(Opcode32::VINSGR2VR_B, vd, rj, ui4));
}

void LoongArch64Emitter::VINSGR2VR_H(LoongArch64Reg vd, LoongArch64Reg rj, u8 ui3) {
	Write32(EncodeVdJUk3(Opcode32::VINSGR2VR_H, vd, rj, ui3));
}

void LoongArch64Emitter::VINSGR2VR_W(LoongArch64Reg vd, LoongArch64Reg rj, u8 ui2) {
	Write32(EncodeVdJUk2(Opcode32::VINSGR2VR_W, vd, rj, ui2));
}

void LoongArch64Emitter::VINSGR2VR_D(LoongArch64Reg vd, LoongArch64Reg rj, u8 ui1) {
	Write32(EncodeVdJUk1(Opcode32::VINSGR2VR_D, vd, rj, ui1));
}

void LoongArch64Emitter::VPICKVE2GR_B(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeDVjUk4(Opcode32::VPICKVE2GR_B, rd, vj, ui4));
}

void LoongArch64Emitter::VPICKVE2GR_H(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeDVjUk3(Opcode32::VPICKVE2GR_H, rd, vj, ui3));
}

void LoongArch64Emitter::VPICKVE2GR_W(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui2) {
	Write32(EncodeDVjUk2(Opcode32::VPICKVE2GR_W, rd, vj, ui2));
}

void LoongArch64Emitter::VPICKVE2GR_D(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui1) {
	Write32(EncodeDVjUk1(Opcode32::VPICKVE2GR_D, rd, vj, ui1));
}

void LoongArch64Emitter::VPICKVE2GR_BU(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeDVjUk4(Opcode32::VPICKVE2GR_BU, rd, vj, ui4));
}

void LoongArch64Emitter::VPICKVE2GR_HU(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeDVjUk3(Opcode32::VPICKVE2GR_HU, rd, vj, ui3));
}

void LoongArch64Emitter::VPICKVE2GR_WU(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui2) {
	Write32(EncodeDVjUk2(Opcode32::VPICKVE2GR_WU, rd, vj, ui2));
}

void LoongArch64Emitter::VPICKVE2GR_DU(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui1) {
	Write32(EncodeDVjUk1(Opcode32::VPICKVE2GR_DU, rd, vj, ui1));
}

void LoongArch64Emitter::VREPLVEI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VREPLVEI_B, vd, vj, ui4));
}

void LoongArch64Emitter::VREPLVEI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeVdVjUk3(Opcode32::VREPLVEI_H, vd, vj, ui3));
}

void LoongArch64Emitter::VREPLVEI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui2) {
	Write32(EncodeVdVjUk2(Opcode32::VREPLVEI_W, vd, vj, ui2));
}

void LoongArch64Emitter::VREPLVEI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui1) {
	Write32(EncodeVdVjUk1(Opcode32::VREPLVEI_D, vd, vj, ui1));
}

void LoongArch64Emitter::VSLLWIL_H_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeVdVjUk3(Opcode32::VSLLWIL_H_B, vd, vj, ui3));
}

void LoongArch64Emitter::VSLLWIL_W_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSLLWIL_W_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSLLWIL_D_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSLLWIL_D_W, vd, vj, ui5));
}

void LoongArch64Emitter::VEXTL_Q_D(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VEXTL_Q_D, vd, vj));
}

void LoongArch64Emitter::VSLLWIL_HU_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeVdVjUk3(Opcode32::VSLLWIL_HU_BU, vd, vj, ui3));
}

void LoongArch64Emitter::VSLLWIL_WU_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSLLWIL_WU_HU, vd, vj, ui4));
}

void LoongArch64Emitter::VSLLWIL_DU_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSLLWIL_DU_WU, vd, vj, ui5));
}

void LoongArch64Emitter::VEXTL_QU_DU(LoongArch64Reg vd, LoongArch64Reg vj) {
	Write32(EncodeVdVj(Opcode32::VEXTL_QU_DU, vd, vj));
}

void LoongArch64Emitter::VBITCLRI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeVdVjUk3(Opcode32::VBITCLRI_B, vd, vj, ui3));
}

void LoongArch64Emitter::VBITCLRI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VBITCLRI_H, vd, vj, ui4));
}

void LoongArch64Emitter::VBITCLRI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VBITCLRI_W, vd, vj, ui5));
}

void LoongArch64Emitter::VBITCLRI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VBITCLRI_D, vd, vj, ui6));
}

void LoongArch64Emitter::VBITSETI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeVdVjUk3(Opcode32::VBITSETI_B, vd, vj, ui3));
}

void LoongArch64Emitter::VBITSETI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VBITSETI_H, vd, vj, ui4));
}

void LoongArch64Emitter::VBITSETI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VBITSETI_W, vd, vj, ui5));
}

void LoongArch64Emitter::VBITSETI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VBITSETI_D, vd, vj, ui6));
}

void LoongArch64Emitter::VBITREVI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeVdVjUk3(Opcode32::VBITREVI_B, vd, vj, ui3));
}

void LoongArch64Emitter::VBITREVI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VBITREVI_H, vd, vj, ui4));
}

void LoongArch64Emitter::VBITREVI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VBITREVI_W, vd, vj, ui5));
}

void LoongArch64Emitter::VBITREVI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VBITREVI_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSAT_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeVdVjUk3(Opcode32::VSAT_B, vd, vj, ui3));
}

void LoongArch64Emitter::VSAT_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSAT_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSAT_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSAT_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSAT_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSAT_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSAT_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
    Write32(EncodeVdVjUk3(Opcode32::VSAT_BU, vd, vj, ui3));
}

void LoongArch64Emitter::VSAT_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
    Write32(EncodeVdVjUk4(Opcode32::VSAT_HU, vd, vj, ui4));
}

void LoongArch64Emitter::VSAT_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
    Write32(EncodeVdVjUk5(Opcode32::VSAT_WU, vd, vj, ui5));
}

void LoongArch64Emitter::VSAT_DU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
    Write32(EncodeVdVjUk6(Opcode32::VSAT_DU, vd, vj, ui6));
}

void LoongArch64Emitter::VSLLI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeVdVjUk3(Opcode32::VSLLI_B, vd, vj, ui3));
}

void LoongArch64Emitter::VSLLI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSLLI_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSLLI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSLLI_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSLLI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSLLI_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSRLI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeVdVjUk3(Opcode32::VSRLI_B, vd, vj, ui3));
}

void LoongArch64Emitter::VSRLI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSRLI_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSRLI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSRLI_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSRLI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSRLI_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSRAI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3) {
	Write32(EncodeVdVjUk3(Opcode32::VSRAI_B, vd, vj, ui3));
}

void LoongArch64Emitter::VSRAI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSRAI_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSRAI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSRAI_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSRAI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSRAI_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSRLNI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSRLNI_B_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSRLNI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSRLNI_H_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSRLNI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSRLNI_W_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSRLNI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7) {
	Write32(EncodeVdVjUk7(Opcode32::VSRLNI_D_Q, vd, vj, ui7));
}

void LoongArch64Emitter::VSRLRNI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSRLRNI_B_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSRLRNI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSRLRNI_H_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSRLRNI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSRLRNI_W_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSRLRNI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7) {
	Write32(EncodeVdVjUk7(Opcode32::VSRLRNI_D_Q, vd, vj, ui7));
}

void LoongArch64Emitter::VSSRLNI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSSRLNI_B_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSSRLNI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSSRLNI_H_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSSRLNI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSSRLNI_W_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSSRLNI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7) {
	Write32(EncodeVdVjUk7(Opcode32::VSSRLNI_D_Q, vd, vj, ui7));
}

void LoongArch64Emitter::VSSRLNI_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSSRLNI_BU_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSSRLNI_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSSRLNI_HU_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSSRLNI_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSSRLNI_WU_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSSRLNI_DU_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7) {
	Write32(EncodeVdVjUk7(Opcode32::VSSRLNI_DU_Q, vd, vj, ui7));
}

void LoongArch64Emitter::VSSRLRNI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSSRLRNI_B_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSSRLRNI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSSRLRNI_H_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSSRLRNI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSSRLRNI_W_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSSRLRNI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7) {
	Write32(EncodeVdVjUk7(Opcode32::VSSRLRNI_D_Q, vd, vj, ui7));
}

void LoongArch64Emitter::VSSRLRNI_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSSRLRNI_BU_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSSRLRNI_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSSRLRNI_HU_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSSRLRNI_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSSRLRNI_WU_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSSRLRNI_DU_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7) {
	Write32(EncodeVdVjUk7(Opcode32::VSSRLRNI_DU_Q, vd, vj, ui7));
}

void LoongArch64Emitter::VSRANI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSRANI_B_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSRANI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSRANI_H_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSRANI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSRANI_W_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSRANI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7) {
	Write32(EncodeVdVjUk7(Opcode32::VSRANI_D_Q, vd, vj, ui7));
}

void LoongArch64Emitter::VSRARNI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSRARNI_B_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSRARNI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSRARNI_H_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSRARNI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSRARNI_W_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSRARNI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7) {
	Write32(EncodeVdVjUk7(Opcode32::VSRARNI_D_Q, vd, vj, ui7));
}

void LoongArch64Emitter::VSSRANI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSSRANI_B_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSSRANI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSSRANI_H_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSSRANI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSSRANI_W_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSSRANI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7) {
	Write32(EncodeVdVjUk7(Opcode32::VSSRANI_D_Q, vd, vj, ui7));
}

void LoongArch64Emitter::VSSRANI_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSSRANI_BU_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSSRANI_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSSRANI_HU_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSSRANI_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSSRANI_WU_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSSRANI_DU_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7) {
	Write32(EncodeVdVjUk7(Opcode32::VSSRANI_DU_Q, vd, vj, ui7));
}

void LoongArch64Emitter::VSSRARNI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSSRARNI_B_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSSRARNI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSSRARNI_H_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSSRARNI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSSRARNI_W_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSSRARNI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7) {
	Write32(EncodeVdVjUk7(Opcode32::VSSRARNI_D_Q, vd, vj, ui7));
}

void LoongArch64Emitter::VSSRARNI_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4) {
	Write32(EncodeVdVjUk4(Opcode32::VSSRARNI_BU_H, vd, vj, ui4));
}

void LoongArch64Emitter::VSSRARNI_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5) {
	Write32(EncodeVdVjUk5(Opcode32::VSSRARNI_HU_W, vd, vj, ui5));
}

void LoongArch64Emitter::VSSRARNI_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6) {
	Write32(EncodeVdVjUk6(Opcode32::VSSRARNI_WU_D, vd, vj, ui6));
}

void LoongArch64Emitter::VSSRARNI_DU_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7) {
	Write32(EncodeVdVjUk7(Opcode32::VSSRARNI_DU_Q, vd, vj, ui7));
}

void LoongArch64Emitter::VEXTRINS_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
	Write32(EncodeVdVjUk8(Opcode32::VEXTRINS_D, vd, vj, ui8));
}

void LoongArch64Emitter::VEXTRINS_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
	Write32(EncodeVdVjUk8(Opcode32::VEXTRINS_W, vd, vj, ui8));
}

void LoongArch64Emitter::VEXTRINS_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
	Write32(EncodeVdVjUk8(Opcode32::VEXTRINS_H, vd, vj, ui8));
}

void LoongArch64Emitter::VEXTRINS_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
	Write32(EncodeVdVjUk8(Opcode32::VEXTRINS_B, vd, vj, ui8));
}

void LoongArch64Emitter::VSHUF4I_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
	Write32(EncodeVdVjUk8(Opcode32::VSHUF4I_B, vd, vj, ui8));
}

void LoongArch64Emitter::VSHUF4I_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
	Write32(EncodeVdVjUk8(Opcode32::VSHUF4I_H, vd, vj, ui8));
}

void LoongArch64Emitter::VSHUF4I_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
	Write32(EncodeVdVjUk8(Opcode32::VSHUF4I_W, vd, vj, ui8));
}

void LoongArch64Emitter::VSHUF4I_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
	Write32(EncodeVdVjUk8(Opcode32::VSHUF4I_D, vd, vj, ui8));
}

void LoongArch64Emitter::VBITSELI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
	Write32(EncodeVdVjUk8(Opcode32::VBITSELI_B, vd, vj, ui8));
}

void LoongArch64Emitter::VANDI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
	Write32(EncodeVdVjUk8(Opcode32::VANDI_B, vd, vj, ui8));
}

void LoongArch64Emitter::VORI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
	Write32(EncodeVdVjUk8(Opcode32::VORI_B, vd, vj, ui8));
}

void LoongArch64Emitter::VXORI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
	Write32(EncodeVdVjUk8(Opcode32::VXORI_B, vd, vj, ui8));
}

void LoongArch64Emitter::VNORI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
	Write32(EncodeVdVjUk8(Opcode32::VNORI_B, vd, vj, ui8));
}

void LoongArch64Emitter::VLDI(LoongArch64Reg vd, s16 i13) {
	Write32(EncodeVdSj13(Opcode32::VLDI, vd, i13));
}

void LoongArch64Emitter::VPERMI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8) {
	Write32(EncodeVdVjUk8(Opcode32::VPERMI_W, vd, vj, ui8));
}

void LoongArch64CodeBlock::PoisonMemory(int offset) {
    // So we can adjust region to writable space.  Might be zero.
	ptrdiff_t writable = writable_ - code_;

	u32 *ptr = (u32 *)(region + offset + writable);
	u32 *maxptr = (u32 *)(region + region_size - offset + writable);
    // If our memory isn't a multiple of u32 then this won't write the last remaining bytes with anything
	// Less than optimal, but there would be nothing we could do but throw a runtime warning anyway.
	// LoongArch64: 0x002a0000 = BREAK 0
	while (ptr < maxptr)
		*ptr++ = 0x002a0000;
}

};