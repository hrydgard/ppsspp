// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "Common/CommonTypes.h"

enum class VSHModuleRoute : u8 {
	RealPrx,
	HostHle,
	RealPrxWithHooks,
	Unsupported,
};

enum class VSHModuleStage : u8 {
	BootDriver,
	UiFoundation,
};

struct VSHModuleRouteEntry {
	const char *path;
	const char *moduleName;
	u32 expectedCrc;
	VSHModuleRoute route;
	VSHModuleStage stage;
	bool required;
};

struct VSHFirmwareProfile {
	const char *id;
	int apiFirmwareVersion;
	const char *generation;
	const char *primaryModulePath;
	u64 primaryModuleSize;
	const char *primaryModuleSha256;
	const char *indexPath;
	u64 indexSize;
	const char *indexSha256;
};

enum class VSHUnresolvedKind : u8 {
	KnownHleModuleMissingNid,
	NoProvider,
};

enum class VSHImportOwner : u8 {
	HostHle,
	RealPrx,
};

// This is deliberately a classification, not an instruction to blindly hook a
// function. In particular, IntentionalUnresolved entries are containment
// boundaries: returning success would let a real driver continue into hardware
// PPSSPP does not model.
enum class VSHImportDisposition : u8 {
	HostHleImplemented,
	HostHleCandidate,
	HostStateRequired,
	CompatibilityNoOpCandidate,
	IntentionalUnresolved,
	Unclassified,
};

struct VSHImportClassification {
	const char *library;
	u32 nid;
	const char *functionName;
	VSHImportDisposition disposition;
	const char *evidence;
};

const VSHFirmwareProfile &VSHGetFirmwareProfile();
bool VSHValidateFirmwareProfile(std::string *errorString);

const VSHModuleRouteEntry *VSHGetBootModuleRoutes(size_t *count);
const char *VSHModuleRouteName(VSHModuleRoute route);
const char *VSHModuleStageName(VSHModuleStage stage);
bool VSHRouteForcesRealModule(std::string_view moduleName);

const VSHImportClassification *VSHFindImportClassification(std::string_view library, u32 nid);
const char *VSHImportDispositionName(VSHImportDisposition disposition);

void VSHRouteAuditReset();
void VSHRouteAuditRecordModuleAttempt(const VSHModuleRouteEntry &entry);
void VSHRouteAuditRecordModuleLoad(
	const VSHModuleRouteEntry &entry,
	int moduleId,
	std::string_view actualModuleName,
	u32 crc,
	bool isFake
);
void VSHRouteAuditRecordModuleLoadFailure(
	const VSHModuleRouteEntry &entry,
	int result
);
void VSHRouteAuditRecordModuleStart(
	const VSHModuleRouteEntry &entry,
	int result
);
void VSHRouteAuditRecordUnresolved(
	std::string_view importingModule,
	std::string_view importLibrary,
	u32 nid,
	VSHUnresolvedKind kind,
	bool runtimeHit
);
void VSHRouteAuditRecordResolved(
	std::string_view importingModule,
	std::string_view importLibrary,
	u32 nid,
	VSHImportOwner owner,
	std::string_view provider,
	bool rejectedPlaceholder
);
void VSHRouteAuditRecordLifecycleLoad(
	std::string_view path,
	int moduleId,
	std::string_view moduleName,
	u32 crc,
	bool isFake
);
void VSHRouteAuditRecordLifecycleLoadFailure(std::string_view path, int result);
void VSHRouteAuditRecordLifecycleStart(int moduleId, int result, bool completed);
void VSHRouteAuditRecordLifecycleStop(int moduleId, int result);
void VSHRouteAuditRecordLifecycleUnload(int moduleId, int result);
void VSHRouteAuditRecordHleOverride(std::string_view exportingModule, std::string_view exportLibrary, u32 nid);

std::string VSHRouteAuditReport();
void VSHRouteAuditLogSummary(const char *phase);
