#include <Windows.h>
#include <assert.h>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>

#include "base/logging.h"
#include "thin3d/d3dx9_loader.h"

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
static TFunc_D3DXCheckVersion m_TFunc_D3DXCheckVersion;
static TFunc_D3DXAssembleShader m_TFunc_D3DXAssembleShader;
static TFunc_D3DXCompileShader m_TFunc_D3DXCompileShader;

void makeD3DX9dllFilename_by_version(std::string& strOut, unsigned int vers, bool bDebugV = false) {
	strOut = "";
	strOut += "d3dx9";
	if (bDebugV) {
		strOut += "d";
	}
	strOut += "_";
	char buf[32];
	sprintf(buf, "%02i", vers);
	strOut += buf;
	strOut += ".dll";
	// temp mon
	if (vers < 10) {
		int _stop = 0;
	}
}

void makeD3DX9dllFilename_by_versionW(std::wstring& strOut, unsigned int vers, bool bDebugV = false)
{
	strOut = L"";
	strOut += L"d3dx9";

	if (bDebugV) {
		strOut += L"d";
	}

	strOut += L"_";

	wchar_t buf[32];
	wsprintf(buf, L"%02i", vers);
	strOut += buf;
	strOut += L".dll";

	// temp mon
	if (vers < 10) {
		int _stop = 0;
	}
}

bool checkDllExistsA(const char* fname, bool bIncludeExeDir) {
	return ::GetFileAttributesA(fname) != DWORD(-1);
}

bool checkDllExistsW(const WCHAR* fname, bool bIncludeExeDir) {
	return ::GetFileAttributesW(fname) != DWORD(-1);
}

bool checkExistsDll(const char* sDllFilename) {
	std::string sfullpath;
	char bufsysdir[MAX_PATH];

	if (!::GetSystemDirectoryA(bufsysdir, MAX_PATH)) {
		throw (std::runtime_error("system error"));
	}
	sfullpath = bufsysdir;
	sfullpath += '\\';
	sfullpath += sDllFilename;
	if (checkDllExistsA(sfullpath.c_str(), true)) {
		return true;
	}
	return false;
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

bool getInstaledD3DXlastVersion(unsigned int* piOutVers) {
	*piOutVers = 0;
	std::string fname;
	for (unsigned int cvers = 45; cvers > 0; cvers--) {
		makeD3DX9dllFilename_by_version(fname, cvers);
		if (checkExistsDll(fname.c_str())) {
			*piOutVers = cvers;
			return true;
		}
	}
	return false;
}

//====================================================================
bool getInstaledD3DXallVersion(std::vector<unsigned int>& versions) {
	bool result = false;
	versions.clear();

	std::string fname;
	for (unsigned int cvers = 45; cvers > 0; cvers--) {
		makeD3DX9dllFilename_by_version(fname, cvers);
		if (checkExistsDll(fname.c_str())) {
			versions.push_back(cvers);
			result = true;
		}
	}
	return  result;
}

//====================================================================
HMODULE loadDllLastVersion(unsigned int* piOutVers) {
	if (piOutVers)
		*piOutVers = 0;

	// old code  ansi string
#if 0

	if (!getInstaledD3DXlastVersion(piOutVers)) {

		return 0;
	}

	std::string dllfname;
	makeD3DX9dllFilename_by_version(dllfname, *piOutVers);

	HMODULE hm = 0;
	hm = ::LoadLibraryA(dllfname.c_str());

#else 
	// unicode
	if (!getInstaledD3DXlastVersion(piOutVers)) {
		return 0;
	}

	bool bdebugvers = false;
#ifdef _DEBUG
	bdebugvers = false; // true;
#endif 
	std::wstring dllfname;
	makeD3DX9dllFilename_by_versionW(dllfname, *piOutVers, bdebugvers);
	HMODULE hm = 0;
	hm = ::LoadLibraryW(dllfname.c_str());
#endif
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

//==============================================================
void handleNotFoundAddr(const char* sFuncName) {
#ifdef _DEBUG
	std::string temp;
	temp = " Entry point not found in d3dx dll :  \n";
	temp += sFuncName;
	// ELOG(temp.c_str());

	assert(false && temp.c_str());

	static FILE* st_file = NULL;
	if (st_file == NULL) {
		st_file = fopen("__getProcAddrlog.txt", "w");
	}

	fputs(sFuncName, st_file);
	fputs("\n", st_file);

#endif
}

int LoadD3DX9Dynamic() {
	if (hm_d3dx) {
		// Already loaded
		return d3dx_version;
	}

	hm_d3dx = loadDllLastVersion(&d3dx_version);
	void* ptr = NULL;
	const int NERROR = -1;
	int _begin = 0;

	__HANDLE_DLL_ENTRY(D3DXCheckVersion);
	__HANDLE_DLL_ENTRY(D3DXAssembleShader);
	__HANDLE_DLL_ENTRY(D3DXCompileShader);

	return d3dx_version;
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