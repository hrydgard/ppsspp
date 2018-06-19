// Copyright (c) 2012- PPSSPP Project.

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

#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

#include "base/timeutil.h"
#include "ext/cityhash/city.h"
#include "Common/FileUtil.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/HLE/ReplaceTables.h"
#include "ext/xxhash.h"

using namespace MIPSCodeUtils;

// Not in a namespace because MSVC's debugger doesn't like it
typedef std::vector<MIPSAnalyst::AnalyzedFunction> FunctionsVector;
static FunctionsVector functions;
std::recursive_mutex functions_lock;

// One function can appear in multiple copies in memory, and they will all have 
// the same hash and should all be replaced if possible.
static std::unordered_multimap<u64, MIPSAnalyst::AnalyzedFunction *> hashToFunction;

struct HashMapFunc {
	char name[64];
	u64 hash;
	u32 size; //number of bytes
	bool hardcoded;  // should not be saved

	bool operator < (const HashMapFunc &other) const {
		return hash < other.hash || (hash == other.hash && size < other.size);
	}

	bool operator == (const HashMapFunc &other) const {
		return hash == other.hash && size == other.size;
	}
};

namespace std {
	template <>
	struct hash<HashMapFunc> {
		size_t operator()(const HashMapFunc &f) const {
			return std::hash<u64>()(f.hash) ^ f.size;
		}
	};
}

static std::unordered_set<HashMapFunc> hashMap;

static std::string hashmapFileName;

#define MIPSTABLE_IMM_MASK 0xFC000000

// Similar to HashMapFunc but has a char pointer for the name for efficiency.
struct HardHashTableEntry {
	uint64_t hash;
	int funcSize;
	const char *funcName;

	bool operator <(const HardHashTableEntry &e) const {
		if (hash < e.hash) return true;
		if (hash > e.hash) return false;
		return funcSize < e.funcSize;
	}
};

// Some hardcoded hashes.  Some have a comment specifying at least one game they are found in.
static const HardHashTableEntry hardcodedHashes[] = {
	{ 0x006b570008068310, 184, "strtok_r", },
	{ 0x019ba2099fb88f3c, 48, "vector_normalize_t", },
	{ 0x0266f96d740c7e03, 912, "memcpy", }, // Final Fantasy 4 (US)
	{ 0x02bd2859045d2383, 240, "bcmp", },
	{ 0x030507c9a1f0fc85, 92, "matrix_rot_x", },
	{ 0x0483fceefa4557ff, 1360, "__udivdi3", },
	{ 0x0558ad5c5be00ca1, 76, "vtfm_t", },
	{ 0x05aedd0c04b451a1, 356, "sqrt", },
	{ 0x0654fc8adbe16ef7, 28, "vmul_q", },
	{ 0x06628f6052cda3c1, 1776, "toheart2_download_frame", }, // To Heart 2 Portable
	{ 0x06b243c926fa6ab5, 24, "vf2in_q", },
	{ 0x06e2826e02056114, 56, "wcslen", },
	{ 0x073cf0b61d3b875a, 416, "hexyzforce_monoclome_thread", }, // Hexyz Force (US)
	{ 0x075fa9b234b41e9b, 32, "fmodf", },
	{ 0x0a051019bdd786c3, 184, "strcasecmp", },
	{ 0x0a1bed70958935d2, 644, "youkosohitsujimura_download_frame", }, // Youkoso Hitsuji-Mura Portable
	{ 0x0a46dc426054bb9d, 24, "vector_add_t", },
	{ 0x0c0173ed70f84f66, 48, "vnormalize_t", },
	{ 0x0c65188f5bfb3915, 24, "vsgn_q", },
	{ 0x0d898513a722ea3c, 40, "copysignf", },
	{ 0x0e99b037b852c8ea, 68, "isnan", },
	// Unsafe due to immediates.
	//{ 0x0eb5f2e95f59276a, 40, "dl_write_lightmode", },
	{ 0x0f1e7533a546f6a1, 228, "dl_write_bone_matrix_4", },
	{ 0x0f2a1106ad84fb74, 52, "strcmp", },
	{ 0x0ffa5db8396d4274, 64, "memcpy_jak", }, // CRUSH
	{ 0x1252e902d0b49bfb, 44, "vector_sub_q_2", },
	{ 0x12df3d33a58d0298, 52, "vmidt_t", },
	{ 0x12feef7b017d3431, 700, "memmove", },
	{ 0x1322c7e3fe6dff4d, 784, "_free_r", },
	{ 0x1376c115d5f1d90c, 36, "strlen", },
	{ 0x1448134dd3acd1f9, 240, "memchr", },
	{ 0x14800e59c04968d7, 100, "wcsstr", },
	{ 0x14b56e858a27a8a4, 24, "vi2f_q", },
	{ 0x15c4662d5d3c728e, 308, "acosf", },
	{ 0x1616ee7052542059, 48, "vtfm_t", },
	{ 0x16965ca11a4e7dac, 104, "vmmul_q_transp", },
	{ 0x16afe830a5dd2de2, 40, "vdiv_q", },
	{ 0x184e834a63a79016, 32, "isnanf", },
	{ 0x1874ee898c7b9f16, 512, "kokoroconnect_download_frame", }, // Kokoro Connect Yochi Random
	{ 0x189212bda9c94df1, 736, "atanf", },
	{ 0x199821ce500ef9d2, 24, "vocp_t", },
	{ 0x1a3c8e9d637ed421, 104, "__adddf3", },
	{ 0x1a7564fa3e25c992, 844, "memcpy", }, // Valkyria Chronicles 3
	{ 0x1aad94c0723edfc0, 124, "vmmul_t_transp", },
	{ 0x1ab33b12b3cb8cb0, 28, "vqmul_q", },
	{ 0x1ac05627df1f87f4, 112, "memcpy16", }, // Valkyria Chronicles 3
	{ 0x1bdf3600844373fd, 112, "strstr", },
	{ 0x1c967be07917ddc9, 92, "strcat", },
	{ 0x1d03fa48334ca966, 556, "_strtol_r", },
	{ 0x1d1311966d2243e9, 428, "suikoden1_and_2_download_frame_1", }, // Gensou Suikoden 1&2
	{ 0x1d7de04b4e87d00b, 680, "kankabanchoutbr_download_frame", }, // Kenka Banchou Bros: Tokyo Battle Royale
	{ 0x1daf6eaf0442391d, 1024, "utawarerumono_download_frame", }, // Utawarerumono portable
	{ 0x1e1525e3bc2f6703, 676, "rint", },
	{ 0x1ec055f28bb9f4d1, 88, "gu_update_stall", },
	{ 0x1ef9cfe6afd3c035, 180, "memset", }, // Kingdom Hearts (US)
	{ 0x1f53eac122f96b37, 224, "cosf", },
	{ 0x2097a8b75c8fe651, 436, "atan2", },
	{ 0x21411b3c860822c0, 36, "matrix_scale_q_t", },
	{ 0x24d82a8675800808, 220, "ceilf", },
	{ 0x26cc90cb25af9d27, 476, "log10", },
	{ 0x275c79791a2bab83, 116, "rezel_cross_download_frame", }, // Rezel Cross
	{ 0x2774614d57d4baa2, 28, "vsub_q", },
	{ 0x279c6bf9cf99cc85, 436, "strncpy", },
	{ 0x2876ed93c5fd1211, 328, "dl_write_matrix_4", },
	{ 0x2965b1ad3ca15cc1, 44, "vtfm_t", },
	{ 0x299a370587df078f, 116, "strange_copy_routine", },
	{ 0x2aa9634a9951c7df, 212, "sdgundamggenerationportable_download_frame", }, // SD Gundam G Generation Portable
	{ 0x2abca53599f09ea7, 608, "dl_write_matrix_3", },
	{ 0x2adb92e8855c454e, 48, "vtfm_q", },
	{ 0x2adc229bef7bbc75, 40, "isnan", },
	{ 0x2bcf5268dd26345a, 340, "acos", },
	{ 0x2c4cb2028a1735bf, 600, "floor", },
	{ 0x2c61a9a06a345b43, 1084, "otomenoheihou_download_frame", }, // Sangoku Koi Senki Otome no Heihou
	{ 0x2ca5958bb816c72e, 44, "vector_i2f_t", },
	{ 0x2e7022d9767c9018, 2100, "atan", },
	{ 0x2f10d3faec84b5bb, 276, "sinf", },
	{ 0x2f639673670caa0e, 772, "dl_write_matrix_2", },
	{ 0x2f718936b371fc44, 40, "vcos_s", },
	{ 0x3024e961d1811dea, 396, "fmod", },
	{ 0x3050bfd0e729dfbf, 220, "atvoffroadfuryblazintrails_download_frame", }, // ATV Offroad Fury Blazin' Trails (US)
	{ 0x30c9c4f420573eb6, 540, "expf", },
	{ 0x317afeb882ff324a, 212, "memcpy", }, // Mimana (US)
	{ 0x31ea2e192f5095a1, 52, "vector_add_t", },
	{ 0x31f523ef18898e0e, 420, "logf", },
	{ 0x32215b1d2196377f, 844, "godseaterburst_blit_texture", }, // Gods Eater Burst (US)
	{ 0x32806967fe81568b, 40, "vector_sub_t_2", },
	{ 0x32ceb9a7f72b9385, 440, "_strtoul_r", },
	{ 0x32e6bc7c151491ed, 68, "memchr", },
	{ 0x335df69db1073a8d, 96, "wcscpy", },
	{ 0x33dc6b144cb302c1, 304, "memmove", }, // Kingdom Hearts (US)
	{ 0x35d3527ff8c22ff2, 56, "matrix_scale_q", },
	{ 0x368f6cf979709a31, 744, "memmove", }, // Jui Dr. Touma Jotarou
	{ 0x373ce518eee5a2d2, 20, "matrix300_store_q", },
	{ 0x3840f5766fada4b1, 592, "dissidia_recordframe_avi", }, // Dissidia (US), Dissidia 012 (US)
	{ 0x388043e96b0e11fd, 144, "dl_write_material_2", },
	{ 0x38f19bc3be215acc, 388, "log10f", },
	{ 0x3913b81ddcbe1efe, 880, "katamari_render_check", }, // Me and My Katamari (US)
	{ 0x393047f06eceaba1, 96, "strcspn", },
	{ 0x39a651942a0b3861, 204, "tan", },
	{ 0x3a3bc2b20a55bf02, 68, "memchr", },
	{ 0x3ab08b5659de1746, 40, "vsin_s", },
	{ 0x3c421a9265f37ebc, 700, "memmove", }, // Final Fantasy 4 (US)
	{ 0x3cbc2d50a3db59e9, 100, "strncmp", },
	{ 0x3ce1806699a91d9d, 148, "dl_write_light", },
	{ 0x3d5e914011c181d4, 444, "scalbnf", },
	{ 0x3ea41eafb53fc99a, 388, "logf", },
	{ 0x3fe38bff09ac3da0, 436, "_strtoul_r", },
	{ 0x40a25c7e1fd44fe2, 24, "fabsf", },
	// Unsafe due to immediates.
	//{ 0x410d48d9b6580b4a, 36, "dl_write_ztest", },
	{ 0x42dc17c8018f30f2, 44, "vtan.s", },
	{ 0x436b07caa2aab931, 352, "acos", },
	{ 0x444472537eedf966, 32, "vmzero_q", },
	{ 0x449ff96982626338, 28, "vmidt_q", },
	{ 0x44f65b1a72c45703, 36, "strlen", },
	{ 0x45528de3948615dc, 64, "memcpy", },
	{ 0x456a0d78ac318d15, 164, "gta_dl_write_matrix", },
	{ 0x497248c9d12f44fd, 68, "strcpy", },
	{ 0x4a70207212a4c497, 24, "strlen", },
	{ 0x4b16a5c602c74c6c, 24, "vsub_t", },
	{ 0x4bb677dace6ca526, 184, "memset", }, // Final FantasyTactics (JPN)
	{ 0x4c4bdedcc13ac77c, 624, "dl_write_matrix_5", },
	{ 0x4c91c556d1aa896b, 104, "dl_write_material_3", },
	{ 0x4cf38c368078181e, 616, "dl_write_matrix", },
	{ 0x4d3e7085e01d30e4, 324, "memcpy", }, // PoPoLoCrois (JPN)
	{ 0x4d72b294501cddfb, 80, "copysign", },
	{ 0x4ddd83b7f4ed8d4e, 844, "memcpy", },
	{ 0x4e266783291b0220, 28, "vsub_t", },
	{ 0x4e5950928c0bb082, 44, "vmmul_q_transp4", },
	{ 0x4f34fc596ecf5b25, 40, "vdiv_t", },
	{ 0x500a949afb39133f, 24, "vf2iu_q", },
	{ 0x50d8f01ea8fa713d, 48, "send_commandi", },
	{ 0x50fa6db2fb14814a, 544, "rint", },
	{ 0x513ce13cd7ce97ea, 332, "scalbnf", },
	{ 0x514161da54d37416, 1416, "__umoddi3", },
	{ 0x51c52d7dd4d2191c, 360, "cos", },
	{ 0x5287d4b8abd5806b, 768, "_strtoll_r", },
	{ 0x52d5141545a75eda, 60, "dl_write_clutformat", },
	{ 0x530cbe1ce9b45d58, 108, "dl_write_light_vector", },
	{ 0x53c9aa23504a630f, 96, "vmmul_q_5", },
	{ 0x54015ccbcbc75374, 24, "strlen", }, // Metal Gear Solid: Peace Walker demo
	{ 0x5550d87a851c218c, 168, "dl_write_viewport", },
	{ 0x55c1294280bfade0, 88, "dl_write_blend_fixed", },
	{ 0x5642a63f3802a792, 456, "orenoimouto_download_frame", }, // Ore no Imouto ga Konnani Kawaii Wake ga Nai
	{ 0x56c9929e8c8c5768, 24, "fabsf", },
	{ 0x572b2d9e57e6e363, 788, "memcpy_thingy", },
	{ 0x580200b840b47c58, 1856, "_realloc_r", },
	{ 0x5961f681bbd69035, 28, "vfad_q", },
	{ 0x598b91c64cf7e036, 2388, "qsort", },
	{ 0x59a0cb08f5ecf8b6, 28, "copysignf", },
	{ 0x5ae4ec2a5e133de3, 28, "vector_cross_t", },
	{ 0x5b005f8375d7c364, 236, "floorf", },
	{ 0x5b103d973fd1dd94, 92, "matrix_rot_y", },
	{ 0x5b9d7e9d4c905694, 196, "_calloc_r", },
	{ 0x5bf7a77b028e9f66, 324, "sqrtf", },
	{ 0x5c0b3edc0e48852c, 148, "memmove", }, // Dissidia 1 (US)
	{ 0x5e898df42c4af6b8, 76, "wcsncmp", },
	{ 0x5f473780835e3458, 52, "vclamp_q", },
	{ 0x5fc58ed2c4d48b79, 40, "vtfm_q_transp", },
	{ 0x6145029ef86f0365, 76, "__extendsfdf2", },
	{ 0x62815f41fa86a131, 656, "scalbn", },
	{ 0x6301fa5149bd973a, 120, "wcscat", },
	{ 0x658b07240a690dbd, 36, "strlen", },
	{ 0x66122f0ab50b2ef9, 296, "dl_write_dither_matrix_5", },
	{ 0x66f7f1beccbc104a, 256, "memcpy_swizzled", }, // God Eater 2
	{ 0x679e647e34ecf7f1, 132, "roundf", },
	{ 0x67afe74d9ec72f52, 4380, "_strtod_r", },
	{ 0x68b22c2aa4b8b915, 400, "sqrt", },
	{ 0x6962da85a6dad937, 60, "strrchr", },
	{ 0x69a3c4f774859404, 64, "vmmul_q_transp2", },
	{ 0x6ab54910104ef000, 628, "sd_gundam_g_generation_download_frame", }, // SD Gundam G Generation World
	{ 0x6ac2cd44e042592b, 252, "atvoffroadfurypro_download_frame", }, // ATV Offroad Fury Pro (US)
	{ 0x6b022e20ee3fa733, 68, "__negdf2", },
	{ 0x6b2a6347c0dfcb57, 152, "strcpy", },
	{ 0x6b4148322c569cb3, 240, "wmemchr", },
	{ 0x6c4cb6d25851553a, 44, "vtfm_t", },
	{ 0x6c7b2462b9ec7bc7, 56, "vmmul_q", },
	{ 0x6ca9cc8fa485d096, 304, "__ieee754_sqrtf", },
	{ 0x6ccffc753d2c148e, 96, "strlwr", },
	{ 0x6e40ec681fb5c571, 40, "matrix_copy_q", },
	{ 0x6e9884c842a51142, 236, "strncasecmp", },
	{ 0x6f101c5c4311c144, 276, "floorf", },
	{ 0x6f1731f84bbf76c3, 116, "strcmp", },
	{ 0x6f4e1a1a84df1da0, 68, "dl_write_texmode", },
	{ 0x6f7c9109b5b8fa47, 688, "danganronpa1_2_download_frame", }, // Danganronpa 1
	{ 0x70649c7211f6a8da, 16, "fabsf", },
	{ 0x70a6152b265228e8, 296, "unendingbloodycall_download_frame", }, // unENDing Bloody Call
	{ 0x7245b74db370ae72, 64, "vmmul_q_transp3", },
	{ 0x7259d52b21814a5a, 40, "vtfm_t_transp", },
	{ 0x7354fd206796d817, 864, "flowers_download_frame", }, // Flowers
	{ 0x736b34ebc702d873, 104, "vmmul_q_transp", },
	{ 0x73a614c08f777d52, 792, "danganronpa2_2_download_frame", }, // Danganronpa 2
	{ 0x7499a2ce8b60d801, 12, "abs", },
	{ 0x74c77fb521740cd2, 284, "toheart2_download_frame_2", }, // To Heart 2 Portable
	{ 0x74ebbe7d341463f3, 72, "dl_write_colortest", },
	{ 0x755a41f9183bb89a, 60, "vmmul_q", },
	{ 0x757d7ab0afbc03f5, 948, "kirameki_school_life_download_frame", }, // Toradora! Portable
	{ 0x759834c69bb12c12, 68, "strcpy", },
	{ 0x75c5a88d62c9c99f, 276, "sinf", },
	{ 0x76c661fecbb39990, 364, "sin", },
	{ 0x770c9c07bf58fd14, 16, "fabsf", },
	{ 0x774e479eb9634525, 464, "_strtol_r", },
	{ 0x77aeb1c23f9aa2ad, 56, "strchr", },
	{ 0x78e8c65b5a458f33, 148, "memcmp", },
	{ 0x794d1b073c183c77, 24, "fabsf", },
	{ 0x7978a886cf70b1c9, 56, "wcschr", },
	{ 0x79faa339fff5a80c, 28, "finitef", },
	{ 0x7c50728008c288e3, 36, "vector_transform_q_4x4", },
	{ 0x7e33d4eaf573f937, 208, "memset", }, // Toukiden (JPN)
	{ 0x7f1fc0dce6be120a, 404, "fmod", },
	{ 0x8126a59ffa504614, 540, "brandish_download_frame", }, // Brandish, Zero no Kiseki, and Ao no Kiseki
	{ 0x828b98925af9ff8f, 40, "vector_distance_t", },
	{ 0x83ac39971df4b966, 336, "sqrtf", },
	{ 0x84c6cd47834f4c79, 1284, "powf", },
	{ 0x8734dc1d155ea493, 24, "vf2iz_q", },
	{ 0x87fe3f7e621ddebb, 212, "memcpy", },
	{ 0x891ca854e1c664e9, 2392, "qsort", },
	{ 0x8965d4b004adad28, 420, "log10f", },
	{ 0x89e1858ba11b84e4, 52, "memset", },
	{ 0x8a00e7207e7dbc81, 232, "_exit", },
	{ 0x8a1f9daadecbaf7f, 104, "vmmul_q_transp", },
	{ 0x8a610f34078ce360, 32, "vector_copy_q_t", },
	{ 0x8c3fd997a544d0b1, 268, "memcpy", }, // Valkyrie Profile (US)
	{ 0x8da0164e69e9b531, 1040, "grisaianokajitsu_download_frame", }, // Grisaia no Kajitsu La Fruit de la Grisaia
	{ 0x8dd0546db930ef25, 992, "memmove", }, // PoPoLoCrois (JPN)
	{ 0x8df2928848857e97, 164, "strcat", },
	{ 0x8e48cabd529ca6b5, 52, "vector_multiply_t", },
	{ 0x8e97dcb03fbaba5c, 104, "vmmul_q_transp", },
	{ 0x8ee81b03d2eef1e7, 28, "vmul_t", },
	{ 0x8f09fb8693c3c49d, 992, "kirameki_school_life_download_frame", }, // Hentai Ouji To Warawanai Neko
	{ 0x8f19c41e8b987e18, 100, "matrix_mogrify", },
	{ 0x8ff11e9bed387401, 700, "memmove", }, // God Eater 2
	{ 0x910140c1a07aa59e, 256, "rot_matrix_euler_zyx", },
	{ 0x91606bd72ae90481, 44, "wmemcpy", },
	{ 0x92c7d2de74068c9c, 32, "vcross_t", },
	{ 0x93d8a275ba288b26, 32, "vdot_t", },
	{ 0x94c7083b64a946b4, 2028, "powf", },
	{ 0x94eb1e7dccca76a4, 680, "shinigamitoshoujo_download_frame", }, // Shinigami to Shoujo (JP)
	{ 0x95a52ce1bc460108, 2036, "_malloc_r", },
	{ 0x95bd33ac373c019a, 24, "fabsf", },
	{ 0x9705934b0950d68d, 280, "dl_write_framebuffer_ptr", },
	{ 0x9734cf721bc0f3a1, 732, "atanf", },
	{ 0x99b85c5fce389911, 408, "mytranwars_upload_frame", }, // Mytran Wars
	{ 0x99c9288185c352ea, 592, "orenoimouto_download_frame_2", }, // Ore no Imouto ga Konnani Kawaii Wake ga Nai
	{ 0x9a06b9d5c16c4c20, 76, "dl_write_clut_ptrload", },
	{ 0x9b88b739267d189e, 88, "strrchr", },
	{ 0x9ce53975bb88c0e7, 96, "strncpy", },
	{ 0x9d4f5f56b52f07f2, 808, "memmove", }, // Jeanne d'Arc (US)
	{ 0x9e2941c4a5c5e847, 792, "memcpy", }, // LittleBigPlanet (US)
	{ 0x9e6ce11f9d49f954, 292, "memcpy", }, // Jeanne d'Arc (US)
	{ 0x9f269daa6f0da803, 128, "dl_write_scissor_region", },
	{ 0x9f7919eeb43982b0, 208, "__fixdfsi", },
	{ 0xa1c9b0a2c71235bf, 1752, "marvelalliance1_copy" }, // Marvel Ultimate Alliance 1 (EU)
	{ 0xa1ca0640f11182e7, 72, "strcspn", },
	{ 0xa243486be51ce224, 272, "cosf", },
	{ 0xa2bcef60a550a3ef, 92, "matrix_rot_z", },
	{ 0xa373f55c65cd757a, 312, "memcpy_swizzled" }, // God Eater Burst Demo
	{ 0xa41989db0f9bf97e, 1304, "pow", },
	{ 0xa44f6227fdbc12b1, 132, "memcmp", }, // Popolocrois (US)
	{ 0xa46cc6ea720d5775, 44, "dl_write_cull", },
	{ 0xa54967288afe8f26, 600, "ceil", },
	{ 0xa5ddbbc688e89a4d, 56, "isinf", },
	{ 0xa615f6bd33195dae, 220, "atvoffroadfuryprodemo_download_frame", }, // ATV Offroad Fury Pro (US) demo
	{ 0xa662359e30b829e4, 148, "memcmp", },
	{ 0xa6a03f0487a911b0, 392, "danganronpa1_1_download_frame", }, // Danganronpa 1
	{ 0xa8390e65fa087c62, 140, "vtfm_t_q", },
	{ 0xa85e48ee10b2dc50, 432, "omertachinmokunookitethelegacy_download_frame", }, // Omerta Chinmoku No Okite The Legacy
	{ 0xa85fe8abb88b1c6f, 52, "vector_sub_t", },
	{ 0xa9194e55cc586557, 268, "memcpy", },
	{ 0xa91b3d60bd75105b, 28, "vadd_t", },
	{ 0xab97ec58c58a7c75, 52, "vector_divide_t", },
	{ 0xac84fa7571895c9a, 68, "memcpy", }, // Marvel Ultimate Alliance 2
	{ 0xacc2c11c3ea28320, 268, "ceilf", },
	{ 0xad67add5122b8c64, 52, "matrix_q_translate_t", },
	{ 0xada952a1adcea4f5, 60, "vmmul_q_transp5", },
	{ 0xadfbf8fb8c933193, 56, "fabs", },
	{ 0xae39bac51fd6e76b, 628, "gakuenheaven_download_frame", }, // Gakuen Heaven: Boy's Love Scramble!
	{ 0xae50226363135bdd, 24, "vector_sub_t", },
	{ 0xae6cd7dfac82c244, 48, "vpow_s", },
	{ 0xaf85d47f95ad2921, 1936, "pow", },
	{ 0xafb2c7e56c04c8e9, 48, "vtfm_q", },
	{ 0xafc9968e7d246a5e, 1588, "atan", },
	{ 0xafcb7dfbc4d72588, 44, "vector_transform_3x4", },
	{ 0xb07f9d82d79deea9, 536, "brandish_download_frame", },  // Brandish, and Sora no kiseki 3rd
	{ 0xb09c9bc1343a774c, 456, "danganronpa2_1_download_frame", }, // Danganronpa 2
	{ 0xb0db731f27d3aa1b, 40, "vmax_s", },
	{ 0xb0ef265e87899f0a, 32, "vector_divide_t_s", },
	{ 0xb183a37baa12607b, 32, "vscl_t", },
	{ 0xb1a3e60a89af9857, 20, "fabs", },
	{ 0xb25670ff47b4843d, 232, "starocean_clear_framebuf" },
	{ 0xb3fef47fb27d57c9, 44, "vector_scale_t", },
	{ 0xb43fd5078ae78029, 84, "send_commandi_stall", },
	{ 0xb43ffbd4dc446dd2, 324, "atan2f", },
	{ 0xb5fdb3083e6f4b3f, 36, "vhtfm_t", },
	{ 0xb6a04277fb1e1a1a, 104, "vmmul_q_transp", },
	{ 0xb726917d688ac95b, 268, "kagaku_no_ensemble_download_frame", }, // Toaru Majutsu to Kagaku no Ensemble
	{ 0xb7448c5ffdd3b0fc, 356, "atan2f", },
	{ 0xb7d88567dc22aab1, 820, "memcpy", }, // Trails in the Sky (US)
	{ 0xb877d3c37a7aaa5d, 60, "vmmul_q_2", },
	{ 0xb89aa73b6f94ba95, 52, "vclamp_t", },
	{ 0xb8bd1f0e02e9ad87, 156, "dl_write_light_dir", },
	{ 0xb8cfaeebfeb2de20, 7548, "_vfprintf_r", },
	{ 0xb97f352e85661af6, 32, "finitef", },
	{ 0xba76a8e853426baa, 544, "soranokiseki_fc_download_frame", }, // Sora no kiseki FC
	{ 0xbb3c6592ed319ba4, 132, "dl_write_fog_params", },
	{ 0xbb7d7c93e4c08577, 124, "__truncdfsf2", },
	{ 0xbdf54d66079afb96, 200, "dl_write_bone_matrix_3", },
	{ 0xbe773f78afd1a70f, 128, "rand", },
	{ 0xbf5d02ccb8514881, 108, "strcmp", },
	{ 0xbf791954ebef4afb, 396, "expf", },
	{ 0xbfa8c16038b7753d, 868, "sakurasou_download_frame", }, // Sakurasou No Pet Na Kanojo
	{ 0xbfe07e305abc4cd1, 808, "memmove" }, // Final Fantasy Tactics (US)
	{ 0xc062f2545ef5dc39, 1076, "kirameki_school_life_download_frame", },// Kirameki School Life SP,and Boku wa Tomodati ga Sukunai
	{ 0xc0feb88cc04a1dc7, 48, "vector_negate_t", },
	{ 0xc1220040b0599a75, 472, "soranokiseki_sc_download_frame", }, // Sora no kiseki SC
	{ 0xc1f34599d0b9146b, 116, "__subdf3", },
	{ 0xc3089f66ee6f0a24, 464, "growlanser_create_saveicon", }, // Growlanswer IV
	{ 0xc319f0d107dd2f45, 888, "__muldf3", },
	{ 0xc35c10300b6b6091, 620, "floor", },
	{ 0xc3dbf3e6c80a0a51, 164, "dl_write_bone_matrix", },
	{ 0xc51519f5dab342d4, 224, "cosf", },
	{ 0xc52c14b9af8c3008, 76, "memcmp", },
	{ 0xc54eae62622f1e11, 164, "dl_write_bone_matrix_2", },
	{ 0xc6b29de7d3245198, 656, "starocean_write_stencil" }, // Star Ocean 1 (US)
	{ 0xc7b1113cfdfedab6, 104, "tonyhawkp8_upload_tutorial_frame", }, // Tony Hawk's Project 8 (US)
	{ 0xc96e3a087ebf49a9, 100, "dl_write_light_color", },
	{ 0xca7cb2c0b9410618, 680, "kudwafter_download_frame", }, // Kud Wafter
	{ 0xcb22120018386319, 692, "photokano_download_frame", }, // Photo Kano
	{ 0xcb7a2edd603ecfef, 48, "vtfm_p", },
	{ 0xcdf64d21418b2667, 24, "vzero_q", },
	{ 0xce1c95ee25b8e2ea, 448, "fmod", },
	{ 0xce4d18a75b98859f, 40, "vector_add_t_2", },
	// Unsafe due to immediates.
	//{ 0xceb5372d0003d951, 52, "dl_write_stenciltest", },
	{ 0xcee11483b550ce8f, 24, "vocp_q", },
	{ 0xcfecf208769ed5fd, 272, "cosf", },
	{ 0xd019b067b58cf6c3, 700, "memmove", }, // Star Ocean 1 (US)
	{ 0xd12a3a91e0040229, 524, "dl_write_enable_disable", },
	{ 0xd141d1efbfe13ca3, 968, "kirameki_school_life_download_frame", }, // Kirameki School Life SP,and Boku wa Tomodati ga Sukunai
	{ 0xd1db467a23ebe00d, 724, "rewrite_download_frame", }, // Rewrite Portable
	{ 0xd1faacfc711d61e8, 68, "__negdf2", },
	{ 0xd207b0650a41dd9c, 28, "vmin_q", },
	{ 0xd6d6e0bb21654778, 24, "vneg_t", },
	{ 0xd7229fee680e7851, 40, "vmin_s", },
	{ 0xd75670860a7f4b05, 144, "wcsncpy", },
	{ 0xd76d1a8804c7ec2c, 100, "dl_write_material", },
	{ 0xd7d350c0b33a4662, 28, "vadd_q", },
	{ 0xd80051931427dca0, 116, "__subdf3", },
	{ 0xd96ba6e4ff86f1bf, 276, "katamari_screenshot_to_565", }, // Me and My Katamari (US)
	{ 0xda51dab503b06979, 32, "vmidt_q", },
	{ 0xdc0cc8b400ecfbf2, 36, "strcmp", },
	{ 0xdcab869acf2bacab, 292, "strncasecmp", },
	{ 0xdcdf7e1c1a3dc260, 372, "strncmp", },
	{ 0xdcfc28e624a81bf1, 5476, "_dtoa_r", },
	{ 0xddfa5a85937aa581, 32, "vdot_q", },
	{ 0xdeb6a583659e3948, 1080, "littlebustersce_download_frame", }, // Little Busters! Converted Edition (JP)
	{ 0xe0214719d8a0aa4e, 104, "strstr", },
	{ 0xe029f0699ca3a886, 76, "matrix300_transform_by", },
	{ 0xe086d5c9ce89148f, 212, "bokunonatsuyasumi4_download_frame", }, // Boku no Natsuyasumi 2 and 4,
	{ 0xe093c2b0194d52b3, 820, "ff1_battle_effect", }, // Final Fantasy 1 (US)
	{ 0xe1107cf3892724a0, 460, "_memalign_r", },
	{ 0xe1724e6e29209d97, 24, "vector_length_t_2", },
	{ 0xe1a5d939cc308195, 68, "wcscmp", },
	{ 0xe2d9106e5b9e39e6, 80, "strnlen", },
	{ 0xe3154c81a76515fa, 208, "narisokonai_download_frame", }, // Narisokonai Eiyuutan
	{ 0xe32cb5c062d1a1c4, 700, "_strtoull_r", },
	{ 0xe3835fb2c9c04e59, 44, "vmmul_q", },
	{ 0xe527c62d8613f297, 136, "strcpy", },
	{ 0xe6002fc9affd678e, 480, "topx_create_saveicon", }, // Tales of Phantasia X
	{ 0xe7b36c2c1348551d, 148, "tan", },
	{ 0xe83a7a9d80a21c11, 4448, "_strtod_r", },
	{ 0xe894bda909a8a8f9, 1064, "expensive_wipeout_pulse", },
	{ 0xe8ad7719be44e7c8, 276, "strchr", },
	{ 0xeabb9c1b4f83d2b4, 52, "memset_jak", }, // Crisis Core (US), Jak and Daxter (this is a slow memset and needs to have slow timing)
	{ 0xeb0f7bf63d52ece9, 88, "strncat", },
	{ 0xeb8c0834d8bbc28c, 416, "fmodf", },
	{ 0xed8918f378e9a563, 628, "sd_gundam_g_generation_download_frame", }, // SD Gundam G Generation Overworld
	{ 0xedbbe9bf9fbceca8, 172, "dl_write_viewport2", },
	{ 0xedc3f476221f96e6, 148, "tanf", },
	{ 0xf1f660fdf349eac2, 1588, "_malloc_r", },
	{ 0xf38a356a359dbe06, 28, "vmax_q", },
	{ 0xf3fc2220ed0f2703, 32, "send_commandf", },
	{ 0xf4d797cef4ac88cd, 684, "_free_r", },
	{ 0xf4ea7d2ec943fa02, 224, "sinf", },
	{ 0xf4f8cdf479dfc4a4, 224, "sinf", },
	{ 0xf527d906d69005a0, 848, "photokano_download_frame_2", }, // Photo Kano
	{ 0xf52f993e444b6c52, 44, "dl_write_shademode", },
	{ 0xf56641884b36c638, 468, "scalbn", },
	{ 0xf5e91870b5b76ddc, 288, "motorstorm_download_frame", }, // MotorStorm: Arctic Edge
	{ 0xf5f7826b4a61767c, 40, "matrix_copy_q", },
	{ 0xf73c094e492bc163, 396, "hypot", },
	{ 0xf773297d89ff7a63, 532, "kumonohatateni_download_frame", }, // Amatsumi Sora ni Kumo no Hatate ni, and Hanakisou
	{ 0xf7fc691db0147e25, 96, "strspn", },
	{ 0xf842aea3baa61f29, 32, "vector_length_t", },
	{ 0xf8e0902f4099a9d6, 2260, "qsort", },
	{ 0xf972543ab7df071a, 32, "vsqrt_s", },
	{ 0xf9b00ef163e8b9d4, 32, "vscl_q", },
	{ 0xf9ea1bf2a897ef24, 588, "ceil", },
	{ 0xfa156c48461eeeb9, 24, "vf2id_q", },
	{ 0xfb4253a1d9d9df9f, 20, "isnanf", },
	{ 0xfd34a9ad94fa6241, 76, "__extendsfdf2", },
	{ 0xfe2566ad957054b7, 232, "suikoden1_and_2_download_frame_2", }, // Gensou Suikoden 1&2
	{ 0xfe4f0280240008e9, 28, "vavg_q", },
	{ 0xfe5dd338ab862291, 216, "memset", }, // Metal Gear Solid: Peace Walker demo
	{ 0xffc8f5f8f946152c, 192, "dl_write_light_color", },
};

namespace MIPSAnalyst {
	// Only can ever output a single reg.
	MIPSGPReg GetOutGPReg(MIPSOpcode op) {
		MIPSInfo opinfo = MIPSGetInfo(op);
		if (opinfo & OUT_RT) {
			return MIPS_GET_RT(op);
		}
		if (opinfo & OUT_RD) {
			return MIPS_GET_RD(op);
		}
		if (opinfo & OUT_RA) {
			return MIPS_REG_RA;
		}
		return MIPS_REG_INVALID;
	}

	bool ReadsFromGPReg(MIPSOpcode op, MIPSGPReg reg) {
		MIPSInfo info = MIPSGetInfo(op);
		if ((info & IN_RS) != 0 && MIPS_GET_RS(op) == reg) {
			return true;
		}
		if ((info & IN_RT) != 0 && MIPS_GET_RT(op) == reg) {
			return true;
		}
		return false;
	}

	bool IsDelaySlotNiceReg(MIPSOpcode branchOp, MIPSOpcode op, MIPSGPReg reg1, MIPSGPReg reg2) {
		MIPSInfo branchInfo = MIPSGetInfo(branchOp);
		MIPSInfo info = MIPSGetInfo(op);
		if (info & IS_CONDBRANCH) {
			return false;
		}
		// $0 is never an out reg, it's always 0.
		if (reg1 != MIPS_REG_ZERO && GetOutGPReg(op) == reg1) {
			return false;
		}
		if (reg2 != MIPS_REG_ZERO && GetOutGPReg(op) == reg2) {
			return false;
		}
		// If the branch is an "and link" branch, check the delay slot for RA.
		if ((branchInfo & OUT_RA) != 0) {
			return GetOutGPReg(op) != MIPS_REG_RA && !ReadsFromGPReg(op, MIPS_REG_RA);
		}
		return true;
	}

	bool IsDelaySlotNiceVFPU(MIPSOpcode branchOp, MIPSOpcode op) {
		MIPSInfo info = MIPSGetInfo(op);
		if (info & IS_CONDBRANCH) {
			return false;
		}
		return (info & OUT_VFPU_CC) == 0;
	}

	bool IsDelaySlotNiceFPU(MIPSOpcode branchOp, MIPSOpcode op) {
		MIPSInfo info = MIPSGetInfo(op);
		if (info & IS_CONDBRANCH) {
			return false;
		}
		return (info & OUT_FPUFLAG) == 0;
	}

	bool IsSyscall(MIPSOpcode op) {
		// Syscalls look like this: 0000 00-- ---- ---- ---- --00 1100
		return (op >> 26) == 0 && (op & 0x3f) == 12;
	}

	static bool IsSWInstr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0xAC000000;
	}
	static bool IsSBInstr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0xA0000000;
	}
	static bool IsSHInstr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0xA4000000;
	}

	static bool IsSWLInstr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0xA8000000;
	}
	static bool IsSWRInstr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0xB8000000;
	}

	static bool IsSWC1Instr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0xE4000000;
	}
	static bool IsSVSInstr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0xE8000000;
	}
	static bool IsSVQInstr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0xF8000000;
	}

	int OpMemoryAccessSize(u32 pc) {
		const auto op = Memory::Read_Instruction(pc, true);
		MIPSInfo info = MIPSGetInfo(op);
		if ((info & (IN_MEM | OUT_MEM)) == 0) {
			return 0;
		}

		// TODO: Verify lwl/lwr/etc.?
		switch (info & MEMTYPE_MASK) {
		case MEMTYPE_BYTE:
			return 1;
		case MEMTYPE_HWORD:
			return 2;
		case MEMTYPE_WORD:
		case MEMTYPE_FLOAT:
			return 4;
		case MEMTYPE_VQUAD:
			return 16;
		}

		return 0;
	}

	bool IsOpMemoryWrite(u32 pc) {
		const auto op = Memory::Read_Instruction(pc, true);
		MIPSInfo info = MIPSGetInfo(op);
		return (info & OUT_MEM) != 0;
	}

	bool OpHasDelaySlot(u32 pc) {
		const auto op = Memory::Read_Instruction(pc, true);
		MIPSInfo info = MIPSGetInfo(op);
		return (info & DELAYSLOT) != 0;
	}

	bool OpWouldChangeMemory(u32 pc, u32 addr, u32 size) {
		const auto op = Memory::Read_Instruction(pc, true);

		// TODO: Trap sc/ll, svl.q, svr.q?

		int gprMask = 0;
		if (IsSWInstr(op))
			gprMask = 0xFFFFFFFF;
		if (IsSHInstr(op))
			gprMask = 0x0000FFFF;
		if (IsSBInstr(op))
			gprMask = 0x000000FF;
		if (IsSWLInstr(op)) {
			const u32 shift = (addr & 3) * 8;
			gprMask = 0xFFFFFFFF >> (24 - shift);
		}
		if (IsSWRInstr(op)) {
			const u32 shift = (addr & 3) * 8;
			gprMask = 0xFFFFFFFF << shift;
		}

		u32 writeVal = 0xFFFFFFFF;
		u32 prevVal = 0x00000000;

		if (gprMask != 0)
		{
			MIPSGPReg rt = MIPS_GET_RT(op);
			writeVal = currentMIPS->r[rt] & gprMask;
			prevVal = Memory::Read_U32(addr) & gprMask;
		}

		if (IsSWC1Instr(op)) {
			int ft = MIPS_GET_FT(op);
			writeVal = currentMIPS->fi[ft];
			prevVal = Memory::Read_U32(addr);
		}

		if (IsSVSInstr(op)) {
			int vt = ((op >> 16) & 0x1f) | ((op & 3) << 5);
			writeVal = currentMIPS->vi[voffset[vt]];
			prevVal = Memory::Read_U32(addr);
		}

		if (IsSVQInstr(op)) {
			int vt = (((op >> 16) & 0x1f)) | ((op & 1) << 5);
			float rd[4];
			ReadVector(rd, V_Quad, vt);
			return memcmp(rd, Memory::GetPointer(addr), sizeof(float) * 4) != 0;
		}

		// TODO: Technically, the break might be for 1 byte in the middle of a sw.
		return writeVal != prevVal;
	}

	AnalysisResults Analyze(u32 address) {
		const int MAX_ANALYZE = 10000;

		AnalysisResults results;

		//set everything to -1 (FF)
		memset(&results, 255, sizeof(AnalysisResults));
		for (int i = 0; i < MIPS_NUM_GPRS; i++) {
			results.r[i].used = false;
			results.r[i].readCount = 0;
			results.r[i].writeCount = 0;
			results.r[i].readAsAddrCount = 0;
		}

		for (u32 addr = address, endAddr = address + MAX_ANALYZE; addr <= endAddr; addr += 4) {
			MIPSOpcode op = Memory::Read_Instruction(addr, true);
			MIPSInfo info = MIPSGetInfo(op);

			MIPSGPReg rs = MIPS_GET_RS(op);
			MIPSGPReg rt = MIPS_GET_RT(op);

			if (info & IN_RS) {
				if ((info & IN_RS_ADDR) == IN_RS_ADDR) {
					results.r[rs].MarkReadAsAddr(addr);
				} else {
					results.r[rs].MarkRead(addr);
				}
			}

			if (info & IN_RT) {
				results.r[rt].MarkRead(addr);
			}

			MIPSGPReg outReg = GetOutGPReg(op);
			if (outReg != MIPS_REG_INVALID) {
				results.r[outReg].MarkWrite(addr);
			}

			if (info & DELAYSLOT) {
				// Let's just finish the delay slot before bailing.
				endAddr = addr + 4;
			}
		}

		int numUsedRegs = 0;
		static int totalUsedRegs = 0;
		static int numAnalyzings = 0;
		for (int i = 0; i < MIPS_NUM_GPRS; i++) {
			if (results.r[i].used) {
				numUsedRegs++;
			}
		}
		totalUsedRegs += numUsedRegs;
		numAnalyzings++;
		VERBOSE_LOG(CPU, "[ %08x ] Used regs: %i Average: %f", address, numUsedRegs, (float)totalUsedRegs / (float)numAnalyzings);

		return results;
	}
	
	void Reset() {
		std::lock_guard<std::recursive_mutex> guard(functions_lock);
		functions.clear();
		hashToFunction.clear();
	}

	void UpdateHashToFunctionMap() {
		std::lock_guard<std::recursive_mutex> guard(functions_lock);
		hashToFunction.clear();
		// Really need to detect C++11 features with better defines.
#if !defined(IOS)
		hashToFunction.reserve(functions.size());
#endif
		for (auto iter = functions.begin(); iter != functions.end(); iter++) {
			AnalyzedFunction &f = *iter;
			if (f.hasHash && f.size > 16) {
				hashToFunction.insert(std::make_pair(f.hash, &f));
			}
		}
	}

	enum RegisterUsage {
		USAGE_CLOBBERED,
		USAGE_INPUT,
		USAGE_UNKNOWN,
	};

	static RegisterUsage DetermineInOutUsage(u64 inFlag, u64 outFlag, u32 addr, int instrs) {
		const u32 start = addr;
		u32 end = addr + instrs * sizeof(u32);
		bool canClobber = true;
		while (addr < end) {
			const MIPSOpcode op = Memory::Read_Instruction(addr, true);
			const MIPSInfo info = MIPSGetInfo(op);

			// Yes, used.
			if (info & inFlag)
				return USAGE_INPUT;

			// Clobbered, so not used.
			if (info & outFlag)
				return canClobber ? USAGE_CLOBBERED : USAGE_UNKNOWN;

			// Bail early if we hit a branch (could follow each path for continuing?)
			if ((info & IS_CONDBRANCH) || (info & IS_JUMP)) {
				// Still need to check the delay slot (so end after it.)
				// We'll assume likely are taken.
				end = addr + 8;
				// The reason for the start != addr check is that we compile delay slots before branches.
				// That means if we're starting at the branch, it's not safe to allow the delay slot
				// to clobber, since it might have already been compiled.
				// As for LIKELY, we don't know if it'll run the branch or not.
				canClobber = (info & LIKELY) == 0 && start != addr;
			}
			addr += 4;
		}
		return USAGE_UNKNOWN;
	}

	static RegisterUsage DetermineRegisterUsage(MIPSGPReg reg, u32 addr, int instrs) {
		switch (reg) {
		case MIPS_REG_HI:
			return DetermineInOutUsage(IN_HI, OUT_HI, addr, instrs);
		case MIPS_REG_LO:
			return DetermineInOutUsage(IN_LO, OUT_LO, addr, instrs);
		case MIPS_REG_FPCOND:
			return DetermineInOutUsage(IN_FPUFLAG, OUT_FPUFLAG, addr, instrs);
		case MIPS_REG_VFPUCC:
			return DetermineInOutUsage(IN_VFPU_CC, OUT_VFPU_CC, addr, instrs);
		default:
			break;
		}

		if (reg > 32) {
			return USAGE_UNKNOWN;
		}

		const u32 start = addr;
		u32 end = addr + instrs * sizeof(u32);
		bool canClobber = true;
		while (addr < end) {
			const MIPSOpcode op = Memory::Read_Instruction(addr, true);
			const MIPSInfo info = MIPSGetInfo(op);

			// Yes, used.
			if ((info & IN_RS) && (MIPS_GET_RS(op) == reg))
				return USAGE_INPUT;
			if ((info & IN_RT) && (MIPS_GET_RT(op) == reg))
				return USAGE_INPUT;

			// Clobbered, so not used.
			bool clobbered = false;
			if ((info & OUT_RT) && (MIPS_GET_RT(op) == reg))
				clobbered = true;
			if ((info & OUT_RD) && (MIPS_GET_RD(op) == reg))
				clobbered = true;
			if ((info & OUT_RA) && (reg == MIPS_REG_RA))
				clobbered = true;
			if (clobbered) {
				if (!canClobber || (info & IS_CONDMOVE))
					return USAGE_UNKNOWN;
				return USAGE_CLOBBERED;
			}

			// Bail early if we hit a branch (could follow each path for continuing?)
			if ((info & IS_CONDBRANCH) || (info & IS_JUMP)) {
				// Still need to check the delay slot (so end after it.)
				// We'll assume likely are taken.
				end = addr + 8;
				// The reason for the start != addr check is that we compile delay slots before branches.
				// That means if we're starting at the branch, it's not safe to allow the delay slot
				// to clobber, since it might have already been compiled.
				// As for LIKELY, we don't know if it'll run the branch or not.
				canClobber = (info & LIKELY) == 0 && start != addr;
			}
			addr += 4;
		}
		return USAGE_UNKNOWN;
	}

	bool IsRegisterUsed(MIPSGPReg reg, u32 addr, int instrs) {
		return DetermineRegisterUsage(reg, addr, instrs) == USAGE_INPUT;
	}

	bool IsRegisterClobbered(MIPSGPReg reg, u32 addr, int instrs) {
		return DetermineRegisterUsage(reg, addr, instrs) == USAGE_CLOBBERED;
	}

	void HashFunctions() {
		std::lock_guard<std::recursive_mutex> guard(functions_lock);
		std::vector<u32> buffer;

		for (auto iter = functions.begin(), end = functions.end(); iter != end; iter++) {
			AnalyzedFunction &f = *iter;
			if (!Memory::IsValidRange(f.start, f.end - f.start + 4)) {
				continue;
			}

			// This is unfortunate.  In case of emuhacks or relocs, we have to make a copy.
			buffer.resize((f.end - f.start + 4) / 4);
			size_t pos = 0;
			for (u32 addr = f.start; addr <= f.end; addr += 4) {
				u32 validbits = 0xFFFFFFFF;
				MIPSOpcode instr = Memory::ReadUnchecked_Instruction(addr, true);
				if (MIPS_IS_EMUHACK(instr)) {
					f.hasHash = false;
					goto skip;
				}

				MIPSInfo flags = MIPSGetInfo(instr);
				if (flags & IN_IMM16)
					validbits &= ~0xFFFF;
				if (flags & IN_IMM26)
					validbits &= ~0x03FFFFFF;
				buffer[pos++] = instr & validbits;
			}

			f.hash = CityHash64((const char *) &buffer[0], buffer.size() * sizeof(u32));
			f.hasHash = true;
skip:
			;
		}
	}

	void PrecompileFunction(u32 startAddr, u32 length) {
		// Direct calls to this ignore the bPreloadFunctions flag, since it's just for stubs.
		if (MIPSComp::jit) {
			MIPSComp::jit->CompileFunction(startAddr, length);
		}
	}

	void PrecompileFunctions() {
		if (!g_Config.bPreloadFunctions) {
			return;
		}
		std::lock_guard<std::recursive_mutex> guard(functions_lock);

		// TODO: Load from cache file if available instead.

		double st = real_time_now();
		for (auto iter = functions.begin(), end = functions.end(); iter != end; iter++) {
			const AnalyzedFunction &f = *iter;

			PrecompileFunction(f.start, f.end - f.start + 4);
		}
		double et = real_time_now();

		NOTICE_LOG(JIT, "Precompiled %d MIPS functions in %0.2f milliseconds", (int)functions.size(), (et - st) * 1000.0);
	}

	static const char *DefaultFunctionName(char buffer[256], u32 startAddr) {
		sprintf(buffer, "z_un_%08x", startAddr);
		return buffer;
	}

	static bool IsDefaultFunction(const char *name) {
		if (name == NULL) {
			// Must be I guess?
			return true;
		}

		// Assume any z_un, not just the address, is a default func.
		return !strncmp(name, "z_un_", strlen("z_un_")) || !strncmp(name, "u_un_", strlen("u_un_"));
	}

	static bool IsDefaultFunction(const std::string &name) {
		if (name.empty()) {
			// Must be I guess?
			return true;
		}

		return IsDefaultFunction(name.c_str());
	}

	static u32 ScanAheadForJumpback(u32 fromAddr, u32 knownStart, u32 knownEnd) {
		static const u32 MAX_AHEAD_SCAN = 0x1000;
		// Maybe a bit high... just to make sure we don't get confused by recursive tail recursion.
		static const u32 MAX_FUNC_SIZE = 0x20000;

		if (fromAddr > knownEnd + MAX_FUNC_SIZE) {
			return INVALIDTARGET;
		}

		// Code might jump halfway up to before fromAddr, but after knownEnd.
		// In that area, there could be another jump up to the valid range.
		// So we track that for a second scan.
		u32 closestJumpbackAddr = INVALIDTARGET;
		u32 closestJumpbackTarget = fromAddr;

		// We assume the furthest jumpback is within the func.
		u32 furthestJumpbackAddr = INVALIDTARGET;

		for (u32 ahead = fromAddr; ahead < fromAddr + MAX_AHEAD_SCAN; ahead += 4) {
			MIPSOpcode aheadOp = Memory::Read_Instruction(ahead, true);
			u32 target = GetBranchTargetNoRA(ahead, aheadOp);
			if (target == INVALIDTARGET && ((aheadOp & 0xFC000000) == 0x08000000)) {
				target = GetJumpTarget(ahead);
			}

			if (target != INVALIDTARGET) {
				// Only if it comes back up to known code within this func.
				if (target >= knownStart && target <= knownEnd) {
					furthestJumpbackAddr = ahead;
				}
				// But if it jumps above fromAddr, we should scan that area too...
				if (target < closestJumpbackTarget && target < fromAddr && target > knownEnd) {
					closestJumpbackAddr = ahead;
					closestJumpbackTarget = target;
				}
			}
			if (aheadOp == MIPS_MAKE_JR_RA()) {
				break;
			}
		}

		if (closestJumpbackAddr != INVALIDTARGET && furthestJumpbackAddr == INVALIDTARGET) {
			for (u32 behind = closestJumpbackTarget; behind < fromAddr; behind += 4) {
				MIPSOpcode behindOp = Memory::Read_Instruction(behind, true);
				u32 target = GetBranchTargetNoRA(behind, behindOp);
				if (target == INVALIDTARGET && ((behindOp & 0xFC000000) == 0x08000000)) {
					target = GetJumpTarget(behind);
				}

				if (target != INVALIDTARGET) {
					if (target >= knownStart && target <= knownEnd) {
						furthestJumpbackAddr = closestJumpbackAddr;
					}
				}
			}
		}

		return furthestJumpbackAddr;
	}

	bool ScanForFunctions(u32 startAddr, u32 endAddr, bool insertSymbols) {
		std::lock_guard<std::recursive_mutex> guard(functions_lock);

		AnalyzedFunction currentFunction = {startAddr};

		u32 furthestBranch = 0;
		bool looking = false;
		bool end = false;
		bool isStraightLeaf = true;
		bool decreasedSp = false;

		u32 addr;
		for (addr = startAddr; addr <= endAddr; addr += 4) {
			MIPSOpcode op = Memory::Read_Instruction(addr, true);
			u32 target = GetBranchTargetNoRA(addr, op);
			if (target != INVALIDTARGET) {
				isStraightLeaf = false;
				if (target > furthestBranch) {
					furthestBranch = target;
				}
			// j X
			} else if ((op & 0xFC000000) == 0x08000000) {
				u32 sureTarget = GetJumpTarget(addr);
				// Check for a tail call.  Might not even have a jr ra.
				if (sureTarget != INVALIDTARGET && sureTarget < currentFunction.start) {
					if (furthestBranch > addr) {
						looking = true;
						addr += 4;
					} else {
						end = true;
					}
				} else if (sureTarget != INVALIDTARGET && sureTarget > addr && sureTarget > furthestBranch) {
					static const u32 MAX_JUMP_FORWARD = 128;
					// If it's a nearby forward jump, and not a stackless leaf, assume not a tail call.
					if (sureTarget <= addr + MAX_JUMP_FORWARD && decreasedSp) {
						// But let's check the delay slot.
						MIPSOpcode op = Memory::Read_Instruction(addr + 4, true);
						// addiu sp, sp, +X
						if ((op & 0xFFFF8000) != 0x27BD0000) {
							furthestBranch = sureTarget;
							continue;
						}
					}

					// A jump later.  Probably tail, but let's check if it jumps back.
					// We use + 8 here in case it jumps right back to the delay slot.  We'll consider that inside the func.
					u32 knownEnd = furthestBranch == 0 ? addr + 8 : furthestBranch;
					u32 jumpback = ScanAheadForJumpback(sureTarget, currentFunction.start, knownEnd);
					if (jumpback != INVALIDTARGET && jumpback > addr && jumpback > knownEnd) {
						furthestBranch = jumpback;
					} else {
						if (furthestBranch > addr) {
							looking = true;
							addr += 4;
						} else {
							end = true;
						}
					}
				}
			}
			if (op == MIPS_MAKE_JR_RA()) {
				// If a branch goes to the jr ra, it's still ending here.
				if (furthestBranch > addr) {
					looking = true;
					addr += 4;
				} else {
					end = true;
				}
			}
			// addiu sp, sp, -X
			if ((op & 0xFFFF8000) == 0x27BD8000) {
				decreasedSp = true;
			}
			// addiu sp, sp, +X
			if ((op & 0xFFFF8000) == 0x27BD0000) {
				decreasedSp = false;
			}
			if (op == MIPS_MAKE_NOP() && currentFunction.start == addr) {
				// Skip nop padding at the beginning of functions (alignment?)
				currentFunction.start += 4;
			}

			if (looking) {
				if (addr >= furthestBranch) {
					u32 sureTarget = GetSureBranchTarget(addr);
					// Regular j only, jals are to new funcs.
					if (sureTarget == INVALIDTARGET && ((op & 0xFC000000) == 0x08000000)) {
						sureTarget = GetJumpTarget(addr);
					}

					if (sureTarget != INVALIDTARGET && sureTarget < addr) {
						end = true;
					} else if (sureTarget != INVALIDTARGET) {
						// Okay, we have a downward jump.  Might be an else or a tail call...
						// If there's a jump back upward in spitting distance of it, it's an else.
						u32 knownEnd = furthestBranch == 0 ? addr : furthestBranch;
						u32 jumpback = ScanAheadForJumpback(sureTarget, currentFunction.start, knownEnd);
						if (jumpback != INVALIDTARGET && jumpback > addr && jumpback > knownEnd) {
							furthestBranch = jumpback;
						}
					}
				}
			}

			if (end) {
				currentFunction.end = addr + 4;
				currentFunction.isStraightLeaf = isStraightLeaf;

				// Check if we already have symbol info starting here.  If so, skip insertion.
				// We used to use the symbols to find the functions, but sometimes we'd find
				// wrong ones due to two modules with the same name.
				u32 existingSize = g_symbolMap->GetFunctionSize(currentFunction.start);
				if (existingSize != SymbolMap::INVALID_ADDRESS) {
					currentFunction.foundInSymbolMap = true;

					// If we run into a func with a different size, skip updating the hash map.
					// This will prevent us saving incorrectly named funcs with wrong hashes.
					u32 detectedSize = currentFunction.end - currentFunction.start + 4;
					if (existingSize != detectedSize) {
						insertSymbols = false;
					}
				}

				functions.push_back(currentFunction);

				furthestBranch = 0;
				addr += 4;
				looking = false;
				end = false;
				isStraightLeaf = true;
				decreasedSp = false;
				currentFunction.start = addr + 4;
				currentFunction.foundInSymbolMap = false;
			}
		}

		if (addr <= endAddr) {
			currentFunction.end = addr + 4;
			functions.push_back(currentFunction);
		}

		for (auto iter = functions.begin(); iter != functions.end(); iter++) {
			iter->size = iter->end - iter->start + 4;
			if (insertSymbols && !iter->foundInSymbolMap) {
				char temp[256];
				g_symbolMap->AddFunction(DefaultFunctionName(temp, iter->start), iter->start, iter->end - iter->start + 4);
			}
		}

		return insertSymbols;
	}

	void FinalizeScan(bool insertSymbols) {
		HashFunctions();

		std::string hashMapFilename = GetSysDirectory(DIRECTORY_SYSTEM) + "knownfuncs.ini";
		if (g_Config.bFuncHashMap || g_Config.bFuncReplacements) {
			LoadBuiltinHashMap();
			if (g_Config.bFuncHashMap) {
				LoadHashMap(hashMapFilename);
				StoreHashMap(hashMapFilename);
			}
			if (insertSymbols) {
				ApplyHashMap();
			}
			if (g_Config.bFuncReplacements) {
				ReplaceFunctions();
			}
		}
	}

	void RegisterFunction(u32 startAddr, u32 size, const char *name) {
		std::lock_guard<std::recursive_mutex> guard(functions_lock);

		// Check if we have this already
		for (auto iter = functions.begin(); iter != functions.end(); iter++) {
			if (iter->start == startAddr) {
				// Let's just add it to the hashmap.
				if (iter->hasHash && size > 16) {
					HashMapFunc hfun;
					hfun.hash = iter->hash;
					strncpy(hfun.name, name, 64);
					hfun.name[63] = 0;
					hfun.size = size;
					hashMap.insert(hfun);
					return;
				} else if (!iter->hasHash || size == 0) {
					ERROR_LOG(HLE, "%s: %08x %08x : match but no hash (%i) or no size", name, startAddr, size, iter->hasHash);
				}
			}
		}

		// Cheats a little.
		AnalyzedFunction fun;
		fun.start = startAddr;
		fun.end = startAddr + size - 4;
		fun.isStraightLeaf = false;  // dunno really
		strncpy(fun.name, name, 64);
		fun.name[63] = 0;
		functions.push_back(fun);

		HashFunctions();
	}

	void ForgetFunctions(u32 startAddr, u32 endAddr) {
		std::lock_guard<std::recursive_mutex> guard(functions_lock);

		// It makes sense to forget functions as modules are unloaded but it breaks
		// the easy way of saving a hashmap by unloading and loading a game. I added
		// an alternative way.

		// Most of the time, functions from the same module will be contiguous in functions.
		FunctionsVector::iterator prevMatch = functions.end();
		size_t originalSize = functions.size();
		for (auto iter = functions.begin(); iter != functions.end(); ++iter) {
			const bool hadPrevMatch = prevMatch != functions.end();
			const bool match = iter->start >= startAddr && iter->start <= endAddr;

			if (!hadPrevMatch && match) {
				// Entering a range.
				prevMatch = iter;
			} else if (hadPrevMatch && !match) {
				// Left a range.
				iter = functions.erase(prevMatch, iter);
				prevMatch = functions.end();
			}
		}
		if (prevMatch != functions.end()) {
			// Cool, this is the fastest way.
			functions.erase(prevMatch, functions.end());
		}

		RestoreReplacedInstructions(startAddr, endAddr);

		if (functions.empty()) {
			hashToFunction.clear();
		} else if (originalSize != functions.size()) {
			UpdateHashToFunctionMap();
		}
	}

	void ReplaceFunctions() {
		std::lock_guard<std::recursive_mutex> guard(functions_lock);

		for (size_t i = 0; i < functions.size(); i++) {
			WriteReplaceInstructions(functions[i].start, functions[i].hash, functions[i].size);
		}
	}

	void UpdateHashMap() {
		std::lock_guard<std::recursive_mutex> guard(functions_lock);

		for (auto it = functions.begin(), end = functions.end(); it != end; ++it) {
			const AnalyzedFunction &f = *it;
			// Small functions aren't very interesting.
			if (!f.hasHash || f.size <= 16) {
				continue;
			}
			// Functions with default names aren't very interesting either.
			const std::string name = g_symbolMap->GetLabelString(f.start);
			if (IsDefaultFunction(name)) {
				continue;
			}

			HashMapFunc mf = { "", f.hash, f.size };
			strncpy(mf.name, name.c_str(), sizeof(mf.name) - 1);
			hashMap.insert(mf);
		}
	}

	const char *LookupHash(u64 hash, u32 funcsize) {
		const HashMapFunc f = { "", hash, funcsize };
		auto it = hashMap.find(f);
		if (it != hashMap.end()) {
			return it->name;
		}
		return 0;
	}

	void SetHashMapFilename(const std::string& filename) {
		if (filename.empty())
			hashmapFileName = GetSysDirectory(DIRECTORY_SYSTEM) + "knownfuncs.ini";
		else
			hashmapFileName = filename;
	}

	void StoreHashMap(std::string filename) {
		if (filename.empty())
			filename = hashmapFileName;

		UpdateHashMap();
		if (hashMap.empty()) {
			return;
		}

		FILE *file = File::OpenCFile(filename, "wt");
		if (!file) {
			WARN_LOG(LOADER, "Could not store hash map: %s", filename.c_str());
			return;
		}

		for (auto it = hashMap.begin(), end = hashMap.end(); it != end; ++it) {
			const HashMapFunc &mf = *it;
			if (!mf.hardcoded) {
				if (fprintf(file, "%016llx:%d = %s\n", mf.hash, mf.size, mf.name) <= 0) {
					WARN_LOG(LOADER, "Could not store hash map: %s", filename.c_str());
					break;
				}
			}
		}
		fclose(file);
	}

	void ApplyHashMap() {
		UpdateHashToFunctionMap();

		for (auto mf = hashMap.begin(), end = hashMap.end(); mf != end; ++mf) {
			auto range = hashToFunction.equal_range(mf->hash);
			if (range.first == range.second) {
				continue;
			}

			// Yay, found a function.
			for (auto iter = range.first; iter != range.second; ++iter) {
				AnalyzedFunction &f = *iter->second;
				if (f.hash == mf->hash && f.size == mf->size) {
					strncpy(f.name, mf->name, sizeof(mf->name) - 1);

					std::string existingLabel = g_symbolMap->GetLabelString(f.start);
					char defaultLabel[256];
					// If it was renamed, keep it.  Only change the name if it's still the default.
					if (existingLabel.empty() || existingLabel == DefaultFunctionName(defaultLabel, f.start)) {
						g_symbolMap->SetLabelName(mf->name, f.start);
					}
				}
			}
		}
	}

	void LoadBuiltinHashMap() {
		HashMapFunc mf;
		for (size_t i = 0; i < ARRAY_SIZE(hardcodedHashes); i++) {
			mf.hash = hardcodedHashes[i].hash;
			mf.size = hardcodedHashes[i].funcSize;
			strncpy(mf.name, hardcodedHashes[i].funcName, sizeof(mf.name));
			mf.name[sizeof(mf.name) - 1] = 0;
			mf.hardcoded = true;
			hashMap.insert(mf);
		}
	}

	void LoadHashMap(const std::string& filename) {
		FILE *file = File::OpenCFile(filename, "rt");
		if (!file) {
			WARN_LOG(LOADER, "Could not load hash map: %s", filename.c_str());
			return;
		}
		hashmapFileName = filename;

		while (!feof(file)) {
			HashMapFunc mf = { "" };
			mf.hardcoded = false;
			if (fscanf(file, "%llx:%d = %63s\n", &mf.hash, &mf.size, mf.name) < 3) {
				char temp[1024];
				if (!fgets(temp, 1024, file)) {
					WARN_LOG(LOADER, "Could not read from hash map: %s", filename.c_str());
				}
				continue;
			}

			hashMap.insert(mf);
		}
		fclose(file);
	}

	std::vector<MIPSGPReg> GetInputRegs(MIPSOpcode op) {
		std::vector<MIPSGPReg> vec;
		MIPSInfo info = MIPSGetInfo(op);
		if (info & IN_RS) vec.push_back(MIPS_GET_RS(op));
		if (info & IN_RT) vec.push_back(MIPS_GET_RT(op));
		return vec;
	}

	std::vector<MIPSGPReg> GetOutputRegs(MIPSOpcode op) {
		std::vector<MIPSGPReg> vec;
		MIPSInfo info = MIPSGetInfo(op);
		if (info & OUT_RD) vec.push_back(MIPS_GET_RD(op));
		if (info & OUT_RT) vec.push_back(MIPS_GET_RT(op));
		if (info & OUT_RA) vec.push_back(MIPS_REG_RA);
		return vec;
	}

	MipsOpcodeInfo GetOpcodeInfo(DebugInterface* cpu, u32 address) {
		MipsOpcodeInfo info;
		memset(&info, 0, sizeof(info));

		if (!Memory::IsValidAddress(address)) {
			info.opcodeAddress = address;
			return info;
		}

		info.cpu = cpu;
		info.opcodeAddress = address;
		info.encodedOpcode = Memory::Read_Instruction(address);

		MIPSOpcode op = info.encodedOpcode;
		MIPSInfo opInfo = MIPSGetInfo(op);
		info.isLikelyBranch = (opInfo & LIKELY) != 0;

		// gather relevant address for alu operations
		// that's usually the value of the dest register
		switch (MIPS_GET_OP(op)) {
		case 0:		// special
			switch (MIPS_GET_FUNC(op)) {
			case 0x20:	// add
			case 0x21:	// addu
				info.hasRelevantAddress = true;
				info.relevantAddress = cpu->GetRegValue(0,MIPS_GET_RS(op))+cpu->GetRegValue(0,MIPS_GET_RT(op));
				break;
			case 0x22:	// sub
			case 0x23:	// subu
				info.hasRelevantAddress = true;
				info.relevantAddress = cpu->GetRegValue(0,MIPS_GET_RS(op))-cpu->GetRegValue(0,MIPS_GET_RT(op));
				break;
			}
			break;
		case 0x08:	// addi
		case 0x09:	// adiu
			info.hasRelevantAddress = true;
			info.relevantAddress = cpu->GetRegValue(0,MIPS_GET_RS(op))+((s16)(op & 0xFFFF));
			break;
		}

		//j , jal, ...
		if (opInfo & IS_JUMP) {
			info.isBranch = true;
			if ((opInfo & OUT_RA) || (opInfo & OUT_RD)) {	// link
				info.isLinkedBranch = true;
			}

			if (opInfo & IN_RS) { // to register
				info.isBranchToRegister = true;
				info.branchRegisterNum = (int)MIPS_GET_RS(op);
				info.branchTarget = cpu->GetRegValue(0,info.branchRegisterNum);
			} else {				// to immediate
				info.branchTarget = GetJumpTarget(address);
			}
		}

		// movn, movz
		if (opInfo & IS_CONDMOVE) {
			info.isConditional = true;

			u32 rt = cpu->GetRegValue(0, (int)MIPS_GET_RT(op));
			switch (opInfo & CONDTYPE_MASK) {
			case CONDTYPE_EQ:
				info.conditionMet = (rt == 0);
				break;
			case CONDTYPE_NE:
				info.conditionMet = (rt != 0);
				break;
			}
		}

		// beq, bgtz, ...
		if (opInfo & IS_CONDBRANCH) {
			info.isBranch = true;
			info.isConditional = true;
			info.branchTarget = GetBranchTarget(address);

			if (opInfo & OUT_RA) {  // link
				info.isLinkedBranch = true;
			}

			u32 rt = cpu->GetRegValue(0, (int)MIPS_GET_RT(op));
			u32 rs = cpu->GetRegValue(0, (int)MIPS_GET_RS(op));
			switch (opInfo & CONDTYPE_MASK) {
			case CONDTYPE_EQ:
				if (opInfo & IN_FPUFLAG) {	// fpu branch
					info.conditionMet = currentMIPS->fpcond == 0;
				} else {
					info.conditionMet = (rt == rs);
					if (MIPS_GET_RT(op) == MIPS_GET_RS(op))	{	// always true
						info.isConditional = false;
					}
				}
				break;
			case CONDTYPE_NE:
				if (opInfo & IN_FPUFLAG) {	// fpu branch
					info.conditionMet = currentMIPS->fpcond != 0;
				} else {
					info.conditionMet = (rt != rs);
					if (MIPS_GET_RT(op) == MIPS_GET_RS(op))	{	// always true
						info.isConditional = false;
					}
				}
				break;
			case CONDTYPE_LEZ:
				info.conditionMet = (((s32)rs) <= 0);
				break;
			case CONDTYPE_GTZ:
				info.conditionMet = (((s32)rs) > 0);
				break;
			case CONDTYPE_LTZ:
				info.conditionMet = (((s32)rs) < 0);
				break;
			case CONDTYPE_GEZ:
				info.conditionMet = (((s32)rs) >= 0);
				break;
			}
		}

		// lw, sh, ...
		if ((opInfo & IN_MEM) || (opInfo & OUT_MEM)) {
			info.isDataAccess = true;
			switch (opInfo & MEMTYPE_MASK) {
			case MEMTYPE_BYTE:
				info.dataSize = 1;
				break;
			case MEMTYPE_HWORD:
				info.dataSize = 2;
				break;
			case MEMTYPE_WORD:
			case MEMTYPE_FLOAT:
				info.dataSize = 4;
				break;

			case MEMTYPE_VQUAD:
				info.dataSize = 16;
			}

			u32 rs = cpu->GetRegValue(0, (int)MIPS_GET_RS(op));
			s16 imm16 = op & 0xFFFF;
			info.dataAddress = rs + imm16;

			info.hasRelevantAddress = true;
			info.relevantAddress = info.dataAddress;
		}

		return info;
	}
}
