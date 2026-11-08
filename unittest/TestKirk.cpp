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

#include <cstdio>
#include <cstring>

#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "ext/libkirk/kirk_engine.h"
#include "UnitTest.h"

static void FromHex(u8 *out, const char *hex, size_t len) {
	for (size_t i = 0; i < len; i++) {
		unsigned v = 0;
		sscanf(hex + i * 2, "%2x", &v);
		out[i] = (u8)v;
	}
}

static bool EqualHex(const u8 *data, const char *hex, size_t len) {
	u8 expected[64];
	FromHex(expected, hex, len);
	if (memcmp(data, expected, len) == 0) {
		return true;
	}
	printf("got      ");
	for (size_t i = 0; i < len; i++) {
		printf("%02x", data[i]);
	}
	printf("\nexpected %s\n", hex);
	return false;
}

// KIRK command 1 signatures from the 6.60 firmware's ECDSA-signed (type 6) modules, as the XMB
// checks them while booting: hash, R, S.
static const char *const kirk1Vectors[][3] = {
	{ "a269699e067b96da49d66b7b2d70f7ffbebcdcbf", "664fe1f2e9d66336f7330bcab9556db6ebe805dc", "b93605ea2a5ac814ad14b153985359b160aa084c" },
	{ "1b73ae788bb2e018ba942bd12a29d46f78aab4a6", "cde388a63c1d57dc5e94eeac2e6c9f2e81c71c58", "fdbf626af2944be6deaab6d8c106ec066d812156" },
	{ "fc38663691bc1eefbbf63410a79ec657013711ee", "664fe1f2e9d66336f7330bcab9556db6ebe805dc", "102f5e879e2ad91bc9fb74d446f751fcd6569d0b" },
	{ "daa435a2eca3a3f5a0937cfebee52c14742a0727", "cde388a63c1d57dc5e94eeac2e6c9f2e81c71c58", "133c774b49bd2e8afc9979b089ce74c839f5bb72" },
	{ "267ae5575cf2e66619d0e06a24ca58e4076d6c78", "664fe1f2e9d66336f7330bcab9556db6ebe805dc", "5a07c4ce9127f3e6b7cfc7b420da4ac8e5874ede" },
	{ "12ffabc1a9e625f0b1d5c3c5e5d3cc10135cbf4c", "cde388a63c1d57dc5e94eeac2e6c9f2e81c71c58", "f162b40d5e2b7b4f6cbffd8ee2a9c57b42bdaf70" },
};

// The base point of the curve commands 0xC-0x11 use.
static const char *const curve2G = "128ec4256487fd8fdf64e2437bc0a1f6d5afde2c" "5958557eb1db001260425524dbc379d5ac5f4adf";

static const char *const pointMulScalar = "0123456789abcdeffedcba98765432100f1e2d3c";
static const char *const pointMulResult = "e22f6d6a26da003b886b2af8b720287322964ba8" "4830b64e45a95ed43664e434d8aae6e99dc36280";

// A signature made with command 0x10: public key, hash, R, S.
static const char *const cmd17Vector[4] = {
	"3663f4b14711f0494fa0fe1c3dff7b15e9cbc189" "df3a94bd2ccc095b0b386f65aa5a5a128b502125",
	"404346494c4f5255585b5e6164676a6d70737679",
	"da6c3bc1bbf907d7f3ad4cbf14a164e326bb2273",
	"641bdb12516febd68f9ccc2051912c6fa9495a40",
};

static bool Kirk1Verify(int i, int flipByte) {
	u8 hash[20], r[20], s[20];
	FromHex(hash, kirk1Vectors[i][0], 20);
	FromHex(r, kirk1Vectors[i][1], 20);
	FromHex(s, kirk1Vectors[i][2], 20);
	if (flipByte >= 0) {
		u8 *all[3] = { hash, r, s };
		all[flipByte / 20][flipByte % 20] ^= 0x10;
	}
	return kirk1_ecdsa_verify(hash, r, s) != 0;
}

static int PointMul(KirkState *kirk, u8 out[0x28], const u8 scalar[0x14], const u8 point[0x28]) {
	KIRK_CMD13_BUFFER in;
	memcpy(in.multiplier, scalar, 0x14);
	memcpy(in.public_key.x, point, 0x28);
	return kirk_sceUtilsBufferCopyWithRange(kirk, out, 0x28, (u8 *)&in, sizeof(in), KIRK_CMD_ECDSA_MULTIPLY_POINT);
}

static int Sign(KirkState *kirk, ECDSA_SIG *sig, const u8 privateKey[0x14], const u8 hash[0x14]) {
	u8 dec[0x20]{};
	memcpy(dec, privateKey, 0x14);
	KIRK_CMD16_BUFFER in;
	encrypt_kirk16_private(kirk, in.enc_private, dec);
	memcpy(in.message_hash, hash, 0x14);
	return kirk_sceUtilsBufferCopyWithRange(kirk, (u8 *)sig, sizeof(*sig), (u8 *)&in, sizeof(in), KIRK_CMD_ECDSA_SIGN);
}

static int Verify(KirkState *kirk, const KIRK_CMD17_BUFFER &buf) {
	return kirk_sceUtilsBufferCopyWithRange(kirk, nullptr, 0, (u8 *)&buf, sizeof(buf), KIRK_CMD_ECDSA_VERIFY);
}

bool TestKirkEcdsa() {
	KirkState kirk{};
	kirk_init(&kirk);

	const int numVectors = (int)ARRAY_SIZE(kirk1Vectors);
	for (int i = 0; i < numVectors; i++) {
		EXPECT_TRUE(Kirk1Verify(i, -1));
		// A bit off in the hash, R or S.
		EXPECT_FALSE(Kirk1Verify(i, 7));
		EXPECT_FALSE(Kirk1Verify(i, 20 + 19));
		EXPECT_FALSE(Kirk1Verify(i, 40));
	}

	u8 g[0x28], scalar[0x14], out[0x28];
	FromHex(g, curve2G, 0x28);
	FromHex(scalar, pointMulScalar, 0x14);
	EXPECT_EQ_INT(PointMul(&kirk, out, scalar, g), 0);
	EXPECT_TRUE(EqualHex(out, pointMulResult, 0x28));

	KIRK_CMD17_BUFFER pinned;
	FromHex(pinned.public_key.x, cmd17Vector[0], 0x28);
	FromHex(pinned.message_hash, cmd17Vector[1], 0x14);
	FromHex(pinned.signature.r, cmd17Vector[2], 0x14);
	FromHex(pinned.signature.s, cmd17Vector[3], 0x14);
	EXPECT_EQ_INT(Verify(&kirk, pinned), 0);
	pinned.signature.s[3] ^= 1;
	EXPECT_EQ_INT(Verify(&kirk, pinned), KIRK_SIG_CHECK_INVALID);

	// Fresh keys and signatures (the PRNG mixes in the time): the public key command 0xC returns has
	// to be its private key times G, and what command 0x10 signs has to pass command 0x11.
	for (int i = 0; i < 4; i++) {
		KIRK_CMD12_BUFFER keys;
		EXPECT_EQ_INT(kirk_sceUtilsBufferCopyWithRange(&kirk, (u8 *)&keys, sizeof(keys), nullptr, 0, KIRK_CMD_ECDSA_GEN_KEYS), 0);
		EXPECT_EQ_INT(PointMul(&kirk, out, keys.private_key, g), 0);
		EXPECT_TRUE(memcmp(out, keys.public_key.x, 0x28) == 0);

		KIRK_CMD17_BUFFER buf;
		memcpy(&buf.public_key, &keys.public_key, sizeof(buf.public_key));
		for (int j = 0; j < 0x14; j++) {
			buf.message_hash[j] = (u8)(i * 31 + j * 7);
		}
		EXPECT_EQ_INT(Sign(&kirk, &buf.signature, keys.private_key, buf.message_hash), 0);
		EXPECT_EQ_INT(Verify(&kirk, buf), 0);
		buf.message_hash[0] ^= 0x80;
		EXPECT_EQ_INT(Verify(&kirk, buf), KIRK_SIG_CHECK_INVALID);
	}
	return true;
}

// Microseconds per KIRK ECDSA operation. Command 1's check runs twice per ECDSA-signed module the
// firmware loads.
bool TestKirkEcdsaBench() {
	KirkState kirk{};
	kirk_init(&kirk);

	int sink = 0;
	int which = 0;
	const double kirk1 = CallsPerSecond([&] {
		sink += Kirk1Verify(which++ % ARRAY_SIZE(kirk1Vectors), -1);
	}, 0.25, 1);

	u8 g[0x28], scalar[0x14], out[0x28];
	FromHex(g, curve2G, 0x28);
	FromHex(scalar, pointMulScalar, 0x14);
	const double pointMul = CallsPerSecond([&] {
		sink += PointMul(&kirk, out, scalar, g);
	}, 0.25, 1);

	KIRK_CMD17_BUFFER buf;
	FromHex(buf.public_key.x, cmd17Vector[0], 0x28);
	FromHex(buf.message_hash, cmd17Vector[1], 0x14);
	FromHex(buf.signature.r, cmd17Vector[2], 0x14);
	FromHex(buf.signature.s, cmd17Vector[3], 0x14);
	const double verify = CallsPerSecond([&] {
		sink += Verify(&kirk, buf);
	}, 0.25, 1);

	KIRK_CMD12_BUFFER keys;
	kirk_sceUtilsBufferCopyWithRange(&kirk, (u8 *)&keys, sizeof(keys), nullptr, 0, KIRK_CMD_ECDSA_GEN_KEYS);
	ECDSA_SIG sig;
	const double sign = CallsPerSecond([&] {
		sink += Sign(&kirk, &sig, keys.private_key, buf.message_hash);
	}, 0.25, 1);

	printf("KIRK 1 ECDSA verify:     %10.1f us\n", 1e6 / kirk1);
	printf("KIRK 0xD point multiply: %10.1f us\n", 1e6 / pointMul);
	printf("KIRK 0x10 sign:          %10.1f us\n", 1e6 / sign);
	printf("KIRK 0x11 verify:        %10.1f us\n", 1e6 / verify);
	return sink != 12345;
}
