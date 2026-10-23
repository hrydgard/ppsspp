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

// PS1EbootFileLoader: small disc images in a temp directory, read back through the EBOOT made from them.

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "ext/libzip/zip.h"

#include "Common/File/DirListing.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Core/ELF/PBPReader.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/FileLoaders/PS1EbootFileLoader.h"
#include "Core/Loaders.h"
#include "Core/Util/GameManager.h"

#include "UnitTest.h"

static const u32 SECTOR = 2352;
static const u32 BLOCK = 16 * SECTOR + 5;
static const u8 SYNC[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };

static void Put32(u8 *p, u32 v) {
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
	p[2] = (v >> 16) & 0xFF;
	p[3] = v >> 24;
}

static u32 Get32(const u8 *p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

static u8 BCD(u32 v) {
	return (u8)(((v / 10) << 4) | (v % 10));
}

// The user data of a small Mode 2 Form 1 data track: a PVD at 16, its root directory at 18 with one
// SYSTEM.CNF at 20, and a pattern everywhere else.
static std::vector<u8> MakeUserData(u32 sectors) {
	std::vector<u8> data(sectors * 2048);
	for (size_t i = 0; i < data.size(); i++) {
		data[i] = (u8)(i * 7 + i / 2048);
	}
	u8 *pvd = &data[16 * 2048];
	memset(pvd, 0, 2048);
	memcpy(pvd, "\x01" "CD001", 6);
	Put32(pvd + 156 + 2, 18);
	Put32(pvd + 156 + 10, 2048);
	u8 *dir = &data[18 * 2048];
	memset(dir, 0, 2048);
	const char name[] = "SYSTEM.CNF;1";
	dir[0] = (u8)(33 + sizeof(name) - 1 + 1);
	Put32(dir + 2, 20);
	const std::string cnf = "BOOT = cdrom:\\SLUS_123.45;1\r\nTCB = 4\r\n";
	Put32(dir + 10, (u32)cnf.size());
	dir[32] = (u8)(sizeof(name) - 1);
	memcpy(dir + 33, name, sizeof(name) - 1);
	memset(&data[20 * 2048], 0, 2048);
	memcpy(&data[20 * 2048], cnf.data(), cnf.size());
	return data;
}

// Raw sectors around user data, as a .bin has them. The EDC and ECC are a pattern, which nothing checks.
static std::vector<u8> MakeRawTrack(const std::vector<u8> &user) {
	const u32 sectors = (u32)(user.size() / 2048);
	std::vector<u8> raw(sectors * SECTOR);
	for (u32 s = 0; s < sectors; s++) {
		u8 *p = &raw[s * SECTOR];
		memcpy(p, SYNC, 12);
		const u32 f = s + 150;
		p[12] = BCD(f / 4500);
		p[13] = BCD(f / 75 % 60);
		p[14] = BCD(f % 75);
		p[15] = 2;
		static const u8 subheader[8] = { 0, 0, 8, 0, 0, 0, 8, 0 };
		memcpy(p + 16, subheader, 8);
		memcpy(p + 24, &user[s * 2048], 2048);
		for (u32 i = 24 + 2048; i < SECTOR; i++) {
			p[i] = (u8)(s + i);
		}
	}
	return raw;
}

static std::vector<u8> MakeAudio(u32 sectors) {
	std::vector<u8> audio(sectors * SECTOR);
	for (size_t i = 0; i < audio.size(); i++) {
		audio[i] = (u8)(i * 13 + 5);
	}
	return audio;
}

static std::vector<u8> ReadAll(FileLoader *loader, s64 pos, size_t size) {
	std::vector<u8> out(size);
	out.resize(loader->ReadAt(pos, size, out.data()));
	return out;
}

// The PSAR's disc image as it should be: each block of 16 sectors, stored, after its DEFLATE header.
static std::vector<u8> ExpectedBlocks(const std::vector<u8> &disc) {
	const u32 blocks = (u32)((disc.size() / SECTOR + 15) / 16);
	std::vector<u8> out;
	for (u32 b = 0; b < blocks; b++) {
		static const u8 header[5] = { 0x01, 0x00, 0x93, 0xFF, 0x6C };
		out.insert(out.end(), header, header + 5);
		for (u32 i = 0; i < 16 * SECTOR; i++) {
			const size_t pos = (size_t)b * 16 * SECTOR + i;
			out.push_back(pos < disc.size() ? disc[pos] : 0);
		}
	}
	return out;
}

static bool CheckEboot(PS1EbootFileLoader &eboot, const std::vector<u8> &disc, u32 numTracks) {
	EXPECT_TRUE(eboot.IsValid());
	EXPECT_EQ_STR(eboot.DiscID(), std::string("SLUS12345"));

	std::string error;
	EXPECT_TRUE(Identify_File(&eboot, &error) == IdentifiedFileType::PSP_PS1_PBP);
	PBPReader pbp(&eboot);
	EXPECT_TRUE(pbp.IsValid());
	std::vector<u8> sfoData;
	EXPECT_TRUE(pbp.GetSubFile(PBP_PARAM_SFO, &sfoData));
	ParamSFOData sfo;
	EXPECT_TRUE(sfo.ReadSFO(sfoData));
	EXPECT_EQ_STR(sfo.GetValueString("CATEGORY"), std::string("ME"));
	EXPECT_EQ_STR(sfo.GetValueString("DISC_ID"), std::string("SLUS12345"));

	const std::vector<u8> header = ReadAll(&eboot, 0, 0x28);
	EXPECT_EQ_INT(header.size(), 0x28);
	const u32 psar = Get32(&header[0x24]);
	const std::vector<u8> psarHeader = ReadAll(&eboot, psar, 0x1300);
	EXPECT_EQ_MEM(&psarHeader[0], "PSISOIMG0000", 12);
	EXPECT_EQ_MEM(&psarHeader[0x400], "_SLUS_12345", 12);

	// The TOC: the last track, the lead-out (at 00:02:00 plus the disc's length), and the tracks.
	const u32 sectors = (u32)(disc.size() / SECTOR);
	const u8 *toc = &psarHeader[0x800];
	EXPECT_EQ_INT(toc[0], 0x41);
	EXPECT_EQ_INT(toc[2], 0xA0);
	EXPECT_EQ_INT(toc[10 + 2], 0xA1);
	EXPECT_EQ_INT(toc[10 + 7], BCD(numTracks));
	EXPECT_EQ_INT(toc[20 + 2], 0xA2);
	EXPECT_EQ_INT(toc[20 + 8], BCD((sectors + 150) / 75 % 60));
	EXPECT_EQ_INT(toc[20 + 9], BCD((sectors + 150) % 75));
	EXPECT_EQ_INT(toc[30 + 2], 0x01);
	EXPECT_EQ_INT(toc[30 + 8], 0x02);
	EXPECT_EQ_INT(toc[30 + 9], 0x00);

	// The index, then the blocks: all of them at once, and reads that start and end mid-sector and
	// cross blocks, as POPS's 0x200-aligned ones do.
	const u32 blocks = (sectors + 15) / 16;
	const std::vector<u8> index = ReadAll(&eboot, psar + 0x4000, (blocks + 1) * 32);
	for (u32 b = 0; b < blocks; b++) {
		EXPECT_EQ_INT(Get32(&index[b * 32]), b * BLOCK);
		EXPECT_EQ_INT(index[b * 32 + 4] | (index[b * 32 + 5] << 8), BLOCK);
	}
	EXPECT_EQ_INT(Get32(&index[blocks * 32]), 0);
	const std::vector<u8> expected = ExpectedBlocks(disc);
	const s64 data = psar + 0x100000;
	EXPECT_EQ_INT(eboot.FileSize(), data + expected.size());
	const std::vector<u8> all = ReadAll(&eboot, data, expected.size() + 100);
	EXPECT_EQ_INT(all.size(), expected.size());
	EXPECT_TRUE(all == expected);
	const size_t starts[] = { 0, 3, 0xB3, BLOCK - 1, BLOCK + 5 + SECTOR - 7, expected.size() - 0x300 };
	for (size_t start : starts) {
		const size_t len = std::min((size_t)0x9400, expected.size() - start);
		const std::vector<u8> part = ReadAll(&eboot, data + start, len);
		EXPECT_EQ_INT(part.size(), len);
		EXPECT_TRUE(memcmp(part.data(), &expected[start], len) == 0);
	}
	return true;
}

bool TestPS1Eboot() {
	// PS1 disc IDs have a region, as PSP ones do.
	EXPECT_TRUE(DetectGameRegionFromID("SLUS00292") == GameRegion::USA);
	EXPECT_TRUE(DetectGameRegionFromID("SCES00003") == GameRegion::EUROPE);
	EXPECT_TRUE(DetectGameRegionFromID("SLPS00444") == GameRegion::JAPAN);
	EXPECT_TRUE(DetectGameRegionFromID("SCKA20001") == GameRegion::KOREA);
	EXPECT_TRUE(DetectGameRegionFromID("ULUS10041") == GameRegion::USA);

	const Path dir = UnitTestTempPath("unittest_ps1eboot");
	File::DeleteDirRecursively(dir);
	EXPECT_TRUE(File::CreateFullPath(dir));

	const std::vector<u8> user = MakeUserData(40);
	const std::vector<u8> data = MakeRawTrack(user);
	const std::vector<u8> audio = MakeAudio(20);
	EXPECT_TRUE(File::WriteDataToFile(false, data.data(), data.size(), dir / "Game (Track 1).bin"));
	EXPECT_TRUE(File::WriteDataToFile(false, audio.data(), audio.size(), dir / "Game (Track 2).bin"));

	// A data track and an audio track in two files, the audio track's pregap ripped (INDEX 00).
	const std::string cue =
		"FILE \"Game (Track 1).bin\" BINARY\r\n"
		"  TRACK 01 MODE2/2352\r\n"
		"    INDEX 01 00:00:00\r\n"
		"FILE \"Game (Track 2).bin\" BINARY\r\n"
		"  TRACK 02 AUDIO\r\n"
		"    INDEX 00 00:00:00\r\n"
		"    INDEX 01 00:00:05\r\n";
	EXPECT_TRUE(File::WriteStringToFile(true, cue, dir / "Game.cue"));
	std::vector<u8> disc = data;
	disc.insert(disc.end(), audio.begin(), audio.end());
	{
		PS1EbootFileLoader eboot(dir / "Game.cue");
		if (!CheckEboot(eboot, disc, 2)) {
			return false;
		}
		EXPECT_EQ_STR(eboot.Title(), std::string("Game"));
		// Track 2's pregap starts at LBA 40, and the track at 45.
		const std::vector<u8> toc = ReadAll(&eboot, Get32(&ReadAll(&eboot, 0, 0x28)[0x24]) + 0x800 + 40, 10);
		EXPECT_EQ_INT(toc[0], 0x01);
		EXPECT_EQ_INT(toc[2], 0x02);
		EXPECT_EQ_INT(toc[5], BCD(190 % 75));
		EXPECT_EQ_INT(toc[9], BCD(195 % 75));
	}
	// The first file opened on its own: the cue sheet that names it describes it.
	{
		EXPECT_TRUE(FindCueSheetFor(dir / "Game (Track 1).bin") == dir / "Game.cue");
		PS1EbootFileLoader eboot(dir / "Game (Track 1).bin");
		if (!CheckEboot(eboot, disc, 2)) {
			return false;
		}
	}

	// A PREGAP that isn't in the file: generated silence before the track.
	const std::string pregapCue =
		"FILE \"Game (Track 1).bin\" BINARY\r\n"
		"  TRACK 01 MODE2/2352\r\n"
		"    INDEX 01 00:00:00\r\n"
		"FILE \"Game (Track 2).bin\" BINARY\r\n"
		"  TRACK 02 AUDIO\r\n"
		"    PREGAP 00:00:03\r\n"
		"    INDEX 01 00:00:00\r\n";
	EXPECT_TRUE(File::WriteStringToFile(true, pregapCue, dir / "Pregap.cue"));
	{
		std::vector<u8> gapped = data;
		gapped.resize(gapped.size() + 3 * SECTOR, 0);
		gapped.insert(gapped.end(), audio.begin(), audio.end());
		PS1EbootFileLoader eboot(dir / "Pregap.cue");
		if (!CheckEboot(eboot, gapped, 2)) {
			return false;
		}
	}

	// MODE2/2336: sectors without their sync and header, which are made again.
	{
		std::vector<u8> stripped;
		for (u32 s = 0; s < 40; s++) {
			stripped.insert(stripped.end(), data.begin() + s * SECTOR + 16, data.begin() + (s + 1) * SECTOR);
		}
		EXPECT_TRUE(File::WriteDataToFile(false, stripped.data(), stripped.size(), dir / "Stripped.bin"));
		EXPECT_TRUE(File::WriteStringToFile(true, "FILE \"Stripped.bin\" BINARY\n  TRACK 01 MODE2/2336\n    INDEX 01 00:00:00\n", dir / "Stripped.cue"));
		PS1EbootFileLoader eboot(dir / "Stripped.cue");
		if (!CheckEboot(eboot, data, 1)) {
			return false;
		}
	}

	// A CloneCD image, and a bare .iso of only the user data, whose sectors are made around it.
	EXPECT_TRUE(File::WriteDataToFile(false, data.data(), data.size(), dir / "Clone.img"));
	EXPECT_TRUE(File::WriteStringToFile(true, "[CloneCD]\r\nVersion=3\r\n[TRACK 1]\r\nMODE=2\r\nINDEX 1=0\r\n", dir / "Clone.ccd"));
	{
		PS1EbootFileLoader eboot(dir / "Clone.ccd");
		if (!CheckEboot(eboot, data, 1)) {
			return false;
		}
	}
	EXPECT_TRUE(File::WriteDataToFile(false, user.data(), user.size(), dir / "Cooked.iso"));
	{
		std::vector<u8> cooked = data;
		for (u32 s = 0; s < 40; s++) {
			memset(&cooked[s * SECTOR + 24 + 2048], 0, SECTOR - 24 - 2048);
		}
		PS1EbootFileLoader eboot(dir / "Cooked.iso");
		if (!CheckEboot(eboot, cooked, 1)) {
			return false;
		}
	}

	// An Alcohol .mds and .mdf: sectors with 96 bytes of subchannel data after each, and an audio track
	// whose 5 sectors of pregap are stored.
	{
		const u32 stride = SECTOR + 96;
		const std::vector<u8> audio25 = MakeAudio(25);
		std::vector<u8> mdf;
		for (const std::vector<u8> *track : { &data, &audio25 }) {
			for (size_t s = 0; s < track->size() / SECTOR; s++) {
				mdf.insert(mdf.end(), track->begin() + s * SECTOR, track->begin() + (s + 1) * SECTOR);
				mdf.resize(mdf.size() + 96, 0xAA);
			}
		}
		EXPECT_TRUE(File::WriteDataToFile(false, mdf.data(), mdf.size(), dir / "Alcohol.mdf"));
		std::vector<u8> mds(0x230, 0);
		memcpy(&mds[0], "MEDIA DESCRIPTOR", 16);
		Put32(&mds[0x50], 0x58);
		mds[0x58 + 0x0A] = 5;
		mds[0x58 + 0x0B] = 3;
		Put32(&mds[0x58 + 0x14], 0x70);
		const u8 points[5] = { 0xA0, 0xA1, 0xA2, 1, 2 };
		for (int i = 0; i < 5; i++) {
			u8 *b = &mds[0x70 + i * 0x50];
			b[4] = points[i];
			if (points[i] > 99) {
				continue;
			}
			const bool audioTrack = points[i] == 2;
			b[0] = audioTrack ? 0xA9 : 0xEC;
			b[1] = 0x08;
			Put32(b + 0x0C, audioTrack ? 0x208 : 0x200);
			b[0x10] = stride & 0xFF;
			b[0x11] = stride >> 8;
			Put32(b + 0x24, audioTrack ? 45 : 0);
			Put32(b + 0x28, audioTrack ? 40 * stride : 0);
			Put32(b + 0x30, 1);
			Put32(b + 0x34, 0x210);
		}
		Put32(&mds[0x200], 150);
		Put32(&mds[0x204], 40);
		Put32(&mds[0x208], 5);
		Put32(&mds[0x20C], 25);
		Put32(&mds[0x210], 0x220);
		memcpy(&mds[0x220], "*.mdf", 6);
		EXPECT_TRUE(File::WriteDataToFile(false, mds.data(), mds.size(), dir / "Alcohol.mds"));

		std::vector<u8> mdsDisc = data;
		mdsDisc.insert(mdsDisc.end(), audio25.begin(), audio25.end());
		PS1EbootFileLoader eboot(dir / "Alcohol.mds");
		if (!CheckEboot(eboot, mdsDisc, 2)) {
			return false;
		}
		PS1EbootFileLoader fromMdf(dir / "Alcohol.mdf");
		if (!CheckEboot(fromMdf, mdsDisc, 2)) {
			return false;
		}
	}

	// The browser shows each disc once: by its cue sheet or .ccd, and a .bin of its own only if it
	// could be a disc image.
	const std::vector<u8> loose(3 * SECTOR, 1);
	EXPECT_TRUE(File::WriteDataToFile(false, loose.data(), loose.size(), dir / "Loose.bin"));
	EXPECT_TRUE(File::WriteDataToFile(false, loose.data(), 100, dir / "Junk.bin"));
	std::vector<File::FileInfo> files;
	File::GetFilesInDir(dir, &files);
	RemoveDescribedDiscImages(&files);
	std::vector<std::string> names;
	for (const File::FileInfo &info : files) {
		names.push_back(info.name);
	}
	std::sort(names.begin(), names.end());
	const std::vector<std::string> expectedNames = { "Alcohol.mdf", "Alcohol.mds", "Clone.ccd", "Cooked.iso", "Game.cue", "Loose.bin", "Pregap.cue", "Stripped.cue" };
	EXPECT_TRUE(names == expectedNames);

	// Installing from a zip: the cue sheet and both its files, into a folder of their own.
	const Path zipPath = dir / "Game.zip";
	{
		int errorp = 0;
		zip_t *z = zip_open(zipPath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &errorp);
		EXPECT_TRUE(z != nullptr);
		const std::pair<const char *, const std::vector<u8> *> entries[] = {
			{ "Game/Game (Track 1).bin", &data },
			{ "Game/Game (Track 2).bin", &audio },
		};
		for (const auto &entry : entries) {
			zip_source_t *source = zip_source_buffer(z, entry.second->data(), entry.second->size(), 0);
			EXPECT_TRUE(zip_file_add(z, entry.first, source, ZIP_FL_ENC_UTF_8) >= 0);
		}
		zip_source_t *cueSource = zip_source_buffer(z, cue.data(), cue.size(), 0);
		EXPECT_TRUE(zip_file_add(z, "Game/Game.cue", cueSource, ZIP_FL_ENC_UTF_8) >= 0);
		zip_source_t *readme = zip_source_buffer(z, "hi", 2, 0);
		EXPECT_TRUE(zip_file_add(z, "readme.txt", readme, ZIP_FL_ENC_UTF_8) >= 0);
		EXPECT_TRUE(zip_close(z) == 0);
	}
	{
		int errorp = 0;
		zip_t *z = zip_open(zipPath.c_str(), 0, &errorp);
		EXPECT_TRUE(z != nullptr);
		ZipFileInfo info{};
		DetectZipFileContents(z, &info);
		EXPECT_TRUE(info.contents == ZipFileContents::PSX_DISC);
		EXPECT_EQ_INT(info.discFiles.size(), 3);
		EXPECT_EQ_STR(info.discFiles[0], std::string("Game/Game.cue"));
		const Path installDir = dir / "installed";
		EXPECT_TRUE(File::CreateFullPath(installDir));
		GameManager manager;
		EXPECT_TRUE(manager.InstallZippedPS1Disc(z, info.discFiles, installDir));
		zip_close(z);
		PS1EbootFileLoader eboot(installDir / "Game" / "Game.cue");
		if (!CheckEboot(eboot, disc, 2)) {
			return false;
		}
	}

	File::DeleteDirRecursively(dir);
	return true;
}
