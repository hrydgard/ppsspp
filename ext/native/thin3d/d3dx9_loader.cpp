#include <Windows.h>
#include <assert.h>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <stdexcept>

#include "base/logging.h"
#include "thin3d/d3dx9_loader.h"

// TODO: See if we can use the bundled D3Dcompiler_47.dll to compiler for DX9 as well.

typedef BOOL(__stdcall *TFunc_D3DXCheckVersion)(UINT D3DSDKVersion, UINT D3DXSDKVersion);
typedef HRESULT(__stdcall *TFunc_D3DXCompileShader)(
	LPCSTR pSrcData,
	UINT srcDataLen,
	CONST D3DXMACRO* pDefines,
	LPD3DXINCLUDE pInclude,
	LPCSTR pFunctionName,
	LPCSTR pProfile,
	DWORD Flags,
	LPD3DXBUFFER* ppShader,
	LPD3DXBUFFER* ppErrorMsgs,
	LPD3DXCONSTANTTABLE * ppConstantTable);

typedef HRESULT(__stdcall *TFunc_D3DXAssembleShader)(
	LPCSTR pSrcData,
	UINT SrcDataLen,
	CONST D3DXMACRO* pDefines,
	LPD3DXINCLUDE pInclude,
	DWORD Flags,
	LPD3DXBUFFER* ppShader,
	LPD3DXBUFFER * ppErrorMsgs
	);

static HMODULE hm_d3dx;
static unsigned int d3dx_version;
static int failedFunctions;

static TFunc_D3DXCheckVersion m_TFunc_D3DXCheckVersion;
static TFunc_D3DXAssembleShader m_TFunc_D3DXAssembleShader;
static TFunc_D3DXCompileShader m_TFunc_D3DXCompileShader;

void makeD3DX9dllFilename_by_versionW(std::wstring& strOut, unsigned int versionNumber, bool debugVersion = false) {
	wchar_t buf[256];
	wsprintf(buf, L"d3dx9%s_%02i.dll", debugVersion ? L"d" : L"", versionNumber);
	strOut = buf;
}

bool checkDllExistsW(const WCHAR* fname, bool bIncludeExeDir) {
	return ::GetFileAttributesW(fname) != DWORD(-1);
}

bool checkExistsDllW(const WCHAR* sDllFilename) {
	std::wstring sfullpath;
	WCHAR bufsysdir[MAX_PATH];

	if (!::GetSystemDirectoryW(bufsysdir, MAX_PATH)) {
		throw (std::runtime_error("system error"));
	}

	sfullpath = bufsysdir;
	sfullpath += L'\\';
	sfullpath += sDllFilename;

	if (checkDllExistsW(sfullpath.c_str(), true)) {
		return true;
	}

	return false;
}

bool getLatestInstalledD3DXVersion(unsigned int* piOutVers, bool debugVersion) {
	*piOutVers = 0;
	std::wstring fname;
	for (unsigned int cvers = 45; cvers > 0; cvers--) {
		makeD3DX9dllFilename_by_versionW(fname, cvers, debugVersion);
		if (checkExistsDllW(fname.c_str())) {
			*piOutVers = cvers;
			return true;
		}
	}
	return false;
}

HMODULE loadDllLastVersion(unsigned int* version, bool debugVersion) {
	failedFunctions = 0;

	if (version)
		*version = 0;

	if (!getLatestInstalledD3DXVersion(version, debugVersion)) {
		return 0;
	}

	std::wstring dllfname;
	makeD3DX9dllFilename_by_versionW(dllfname, version ? *version : 0, debugVersion);
	HMODULE hm = ::LoadLibraryW(dllfname.c_str());
	return hm;
}

#define GB_MAKE_STR(s)  # s
#define GB_MAKE_STR2(x) GB_MAKE_STR(x)

#define __HANDLE_DLL_ENTRY(funcname)   m_TFunc_##funcname =  \
  ( TFunc_##funcname ) ::GetProcAddress(hm_d3dx, GB_MAKE_STR(funcname) ); \
  \
  if(!m_TFunc_##funcname) {\
		handleNotFoundAddr( GB_MAKE_STR(funcname) ); \
  }

void handleNotFoundAddr(const char* sFuncName) {
	ELOG("Failed to find D3DX function %s", sFuncName);
	failedFunctions++;
}

int LoadD3DX9Dynamic(bool debugVersion) {
	if (hm_d3dx) {
		// Already loaded
		return d3dx_version;
	}

	hm_d3dx = loadDllLastVersion(&d3dx_version, debugVersion);
	if (!hm_d3dx) {
		ELOG("Failed to find D3DX dll.");
		return 0;
	}
	const int NERROR = -1;
	int _begin = 0;

	__HANDLE_DLL_ENTRY(D3DXCheckVersion);
	__HANDLE_DLL_ENTRY(D3DXAssembleShader);
	__HANDLE_DLL_ENTRY(D3DXCompileShader);

	if (failedFunctions > 0) {
		ELOG("Failed to load %i D3DX functions. This will not go well.", failedFunctions);
	}

	return failedFunctions > 0 ? 0 : d3dx_version;
}

int GetD3DXVersion() {
	return d3dx_version;
}

void UnloadD3DXDynamic() {
	if (hm_d3dx) {
		FreeLibrary(hm_d3dx);
		hm_d3dx = NULL;
		d3dx_version = 0;
	}
}

// version function

#define GB_D3D9_D3DXDLL_LOADER_CHECK_ENTRY_NULL_PTR(funcname)  assert(false && GB_MAKE_STR2(funcname) ); 


BOOL dyn_D3DXCheckVersion(UINT D3DSDKVersion, UINT D3DXSDKVersion) {
	if (!m_TFunc_D3DXCheckVersion) {
		GB_D3D9_D3DXDLL_LOADER_CHECK_ENTRY_NULL_PTR(D3DXCheckVersion)
	}
	return m_TFunc_D3DXCheckVersion(D3DSDKVersion, D3DXSDKVersion);
}

HRESULT dyn_D3DXAssembleShader(LPCSTR pSrcData, UINT SrcDataLen, CONST D3DXMACRO* pDefines,
	LPD3DXINCLUDE pInclude, DWORD Flags, LPD3DXBUFFER* ppShader, LPD3DXBUFFER * ppErrorMsgs)
{
	if (!m_TFunc_D3DXAssembleShader) {	
		GB_D3D9_D3DXDLL_LOADER_CHECK_ENTRY_NULL_PTR(D3DXAssembleShader);
	}
	return m_TFunc_D3DXAssembleShader(pSrcData, SrcDataLen, pDefines,
		pInclude, Flags, ppShader, ppErrorMsgs);
}

HRESULT dyn_D3DXCompileShader(LPCSTR pSrcData, UINT srcDataLen, CONST D3DXMACRO* pDefines,
	LPD3DXINCLUDE pInclude, LPCSTR pFunctionName, LPCSTR pProfile,
	DWORD Flags, LPD3DXBUFFER* ppShader, LPD3DXBUFFER* ppErrorMsgs,
	LPD3DXCONSTANTTABLE * ppConstantTable)
{
	if (!m_TFunc_D3DXCompileShader) {
		GB_D3D9_D3DXDLL_LOADER_CHECK_ENTRY_NULL_PTR(D3DXCompileShader);
	}
	return m_TFunc_D3DXCompileShader(pSrcData, srcDataLen, pDefines,
		pInclude, pFunctionName, pProfile,
		Flags, ppShader, ppErrorMsgs, ppConstantTable);
}
