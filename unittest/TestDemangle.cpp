// Copyright (c) 2026- PPSSPP Project.

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

#include <string>

#include "Common/Data/Text/Demangle.h"
#include "UnitTest.h"

struct DemangleCase {
	const char *mangled;
	const char *expected;
};

// Expected values are what GNU c++filt prints, so they can be regenerated or checked by
// piping the left column through it.
static const DemangleCase demangleCases[] = {
	{ "_ZN12_GLOBAL__N_1L12ExitCallbackEiiPv",
	  "(anon)::ExitCallback(int, int, void*)" },
	{ "_ZN12_GLOBAL__N_14poolD1Ev",
	  "(anon)::pool::~pool()" },
	{ "_ZN12_GLOBAL__N_14pool4freeEPv.constprop.0",
	  "(anon)::pool::free(void*) [clone .constprop.0]" },
	{ "_ZNK5Thing3fooEv",
	  "Thing::foo() const" },
	{ "_Z3fooPFivE",
	  "foo(int (*)())" },
	{ "_Z3fooRA10_i",
	  "foo(int (&) [10])" },
	{ "_Z3fooM3BarFivE",
	  "foo(int (Bar::*)())" },
	{ "_Z3foorVKi",
	  "foo(int const volatile restrict)" },
	{ "_Z3fooSs",
	  "foo(std::basic_string<char, std::char_traits<char>, std::allocator<char> >)" },
	{ "_Z3fooSbIcE",
	  "foo(std::basic_string<char>)" },
	{ "_ZNSt3mapIiiSt4lessIiESaISt4pairIKiiEEE3endEv",
	  "std::map<int, int, std::less<int>, std::allocator<std::pair<int const, int> > >::end()" },
	{ "_ZNSsC1EOSs",
	  "std::basic_string<char, std::char_traits<char>, std::allocator<char> >::basic_string(std::basic_string<char, std::char_traits<char>, std::allocator<char> >&&)" },
	{ "_ZNSsC1IPKcEET_S2_RKSaIcE",
	  "std::basic_string<char, std::char_traits<char>, std::allocator<char> >::basic_string<char const*>(char const*, char const*, std::allocator<char> const&)" },
	{ "_ZNKSt6vectorIhSaIhEE11_M_data_ptrIhEEPT_S4_",
	  "unsigned char* std::vector<unsigned char, std::allocator<unsigned char> >::_M_data_ptr<unsigned char>(unsigned char*) const" },
	{ "_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE12_M_constructIPKcEEvT_S8_St20forward_iterator_tag.isra.0",
	  "void std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::_M_construct<char const*>(char const*, char const*, std::forward_iterator_tag) [clone .isra.0]" },
	{ "_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE12find_last_ofERKS4_j",
	  "std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::find_last_of(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int) const" },
	{ "_ZZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE12_M_constructIPKcEEvT_S8_St20forward_iterator_tagEN6_GuardD1Ev",
	  "std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::_M_construct<char const*>(char const*, char const*, std::forward_iterator_tag)::_Guard::~_Guard()" },
	{ "_ZN9__gnu_cxxeqIPcSsEEbRKNS_17__normal_iteratorIT_T0_EES7_",
	  "bool __gnu_cxx::operator==<char*, std::basic_string<char, std::char_traits<char>, std::allocator<char> > >(__gnu_cxx::__normal_iterator<char*, std::basic_string<char, std::char_traits<char>, std::allocator<char> > > const&, __gnu_cxx::__normal_iterator<char*, std::basic_string<char, std::char_traits<char>, std::allocator<char> > > const&)" },
	{ "_ZSt4sortIPiEvT_S1_",
	  "void std::sort<int*>(int*, int*)" },
	{ "_ZTV3Foo",
	  "vtable for Foo" },
	{ "_ZTIN3foo3barE",
	  "typeinfo for foo::bar" },
	{ "_ZThn8_N3Foo3barEv",
	  "non-virtual thunk to Foo::bar()" },
	{ "_ZGVZN3foo3barEvE5thing",
	  "guard variable for foo::bar()::thing" },
	{ "_ZTIFbRlE",
	  "typeinfo for bool (long&)" },
	{ "_ZNSt8ios_base7failureB5cxx11D0Ev",
	  "std::ios_base::failure[abi:cxx11]::~failure()" },
	{ "_ZZ3foovENKUliE0_clEi",
	  "foo()::{lambda(int)#2}::operator()(int) const" },
	{ "_ZN4llvm10AccelTableINS_20DWARF5AccelTableDataEE7addNameIJRKNS_3DIEEjbEEEvNS_23DwarfStringPoolEntryRefEDpOT_",
	  "void llvm::AccelTable<llvm::DWARF5AccelTableData>::addName<llvm::DIE const&, unsigned int, bool>(llvm::DwarfStringPoolEntryRef, llvm::DIE const&, unsigned int&&, bool&&)" },
	{ "_ZNSt6vectorIN10PxRenderer10PspTextureESaIS1_EE12emplace_backIJS1_EEERS1_DpOT_",
	  "PxRenderer::PspTexture& std::vector<PxRenderer::PspTexture, std::allocator<PxRenderer::PspTexture> >::emplace_back<PxRenderer::PspTexture>(PxRenderer::PspTexture&&)" },
	{ "_ZNK4llvm15SpecialCaseList14inSectionBlameERKNS_9StringMapINS1_INS0_7MatcherENS_15MallocAllocatorEEES3_EENS_9StringRefES8_S8_",
	  "llvm::SpecialCaseList::inSectionBlame(llvm::StringMap<llvm::StringMap<llvm::SpecialCaseList::Matcher, llvm::MallocAllocator>, llvm::MallocAllocator> const&, llvm::StringRef, llvm::StringRef, llvm::StringRef) const" },
	{ "_ZN4llvm5RTLIB22getOutlineAtomicHelperERA5_A4_KNS0_7LibcallENS_14AtomicOrderingEm",
	  "llvm::RTLIB::getOutlineAtomicHelper(llvm::RTLIB::Libcall const (&) [5][4], llvm::AtomicOrdering, unsigned long)" },
	{ "_ZNSt8_Rb_treeIN4llvm11logicalview10LVLineKindESt4pairIKS2_MNS1_6LVLineEKFbvEESt10_Select1stIS8_ESt4lessIS2_ESaIS8_EEaSERKSE_",
	  "std::_Rb_tree<llvm::logicalview::LVLineKind, std::pair<llvm::logicalview::LVLineKind const, bool (llvm::logicalview::LVLine::*)() const>, std::_Select1st<std::pair<llvm::logicalview::LVLineKind const, bool (llvm::logicalview::LVLine::*)() const> >, std::less<llvm::logicalview::LVLineKind>, std::allocator<std::pair<llvm::logicalview::LVLineKind const, bool (llvm::logicalview::LVLine::*)() const> > >::operator=(std::_Rb_tree<llvm::logicalview::LVLineKind, std::pair<llvm::logicalview::LVLineKind const, bool (llvm::logicalview::LVLine::*)() const>, std::_Select1st<std::pair<llvm::logicalview::LVLineKind const, bool (llvm::logicalview::LVLine::*)() const> >, std::less<llvm::logicalview::LVLineKind>, std::allocator<std::pair<llvm::logicalview::LVLineKind const, bool (llvm::logicalview::LVLine::*)() const> > > const&)" },
	{ "_ZlsRSoRK4Vec3",
	  "operator<<(std::basic_ostream<char, std::char_traits<char> >&, Vec3 const&)" },
	{ "_ZNK3Foocv3BarEv",
	  "Foo::operator Bar() const" },
};

// Metrowerks CodeWarrior (cfront-derived). No reference demangler was available to check
// these against, so they're what our own parser produces - readable and correctly qualified,
// but not claimed to be byte-identical to what CodeWarrior's own tools would print.
static const DemangleCase codeWarriorCases[] = {
	{ "__as__9ANIMEDataFRC9ANIMEData",
	  "ANIMEData::operator=(const ANIMEData &)" },
	{ "__SetupFrameInfo__FP12ThrowContextP13ExceptionInfo",
	  "__SetupFrameInfo(ThrowContext *, ExceptionInfo *)" },
	{ "getDistance__6KzUtilFP7st_unitP7st_unit",
	  "KzUtil::getDistance(st_unit *, st_unit *)" },
	{ "__ad__3stdFQ33std10ctype_base4maskQ33std10ctype_base4mask",
	  "std::operator&(std::ctype_base::mask, std::ctype_base::mask)" },
	{ "__ml__10DTVector3dCFf",
	  "DTVector3d::operator*(float) const" },
	{ "__ct__Q23std12basic_stringFv",
	  "std::basic_string::basic_string()" },
	{ "__dt__6KzUtilFv",
	  "KzUtil::~KzUtil()" },
	{ "__op4Type__3FooCFv",
	  "Foo::operator Type() const" },
	{ "__vc__5ArrayFi",
	  "Array::operator[](int)" },
	{ "__nw__FUi",
	  "operator new(unsigned int)" },
	{ "draw__3FooFA10_iRCf",
	  "Foo::draw(int [10], const float &)" },
	// "N<count><index>" repeats an earlier parameter, "T<index>" refers back to one.
	{ "foo__FPCcUiN21",
	  "foo(const char *, unsigned int, const char *, const char *)" },
	{ "sort__FPiT1",
	  "sort(int *, int *)" },
	// A static data member: no "F", so no parameter list.
	{ "count__Q23foo3bar",
	  "foo::bar::count" },
	// A parameter type we can't decode still gets to keep its name.
	{ "weird__3FooFZZZ",
	  "Foo::weird(...)" },
	// A pointer to a function needs the declarator wrapped, not just a "*" stuck on.
	{ "__call_static_initializers__FPPFv_vPPFv_v",
	  "__call_static_initializers(void (**)(), void (**)())" },
	{ "GetCalcHeapSize__Q26hlSave14SaveDataBufferFPFi_i",
	  "hlSave::SaveDataBuffer::GetCalcHeapSize(int (*)(int))" },
	// Template arguments are spelled inside the length-prefixed name, and are themselves
	// mangled types (or plain integers, for a non-type parameter).
	{ "Init__Q26shList39CList<Q38hlScreen5Brwsr13CContentsUnit>FPCc",
	  "shList::CList<hlScreen::Brwsr::CContentsUnit>::Init(const char *)" },
	{ "Reset__Q28shString15CSimpleChar<24>Fv",
	  "shString::CSimpleChar<24>::Reset()" },
	{ "AddItem__Q26shList47CList<Q26ssTool29tag_<Q26shFont12SysCmdString>>FPQ26ssTool29tag_<Q26shFont12SysCmdString>",
	  "shList::CList<ssTool::tag_<shFont::SysCmdString>>::AddItem(ssTool::tag_<shFont::SysCmdString> *)" },
	// A function template puts them in the base name instead, and encodes its return type.
	{ "sort<Pf>__3stdFPfPf_v",
	  "void std::sort<float *>(float *, float *)" },
	// Compiler-generated symbols that wrap a mangled name rather than being one.
	{ "__vt__Q23std9exception",
	  "vtable for std::exception" },
	{ "__RTTI__Q23std9exception",
	  "typeinfo for std::exception" },
	{ "__sinit_hl_app.cpp",
	  "static initializers for hl_app.cpp" },
	{ "@12@__dt__3SonFv",
	  "non-virtual thunk (12) to Son::~Son()" },
	{ "@STRING@what__Q23std9exceptionCFv",
	  "string literal in std::exception::what() const" },
	{ "@STRING@Get_BGM__Q25hlBHC4SBhcFi@0",
	  "string literal 0 in hlBHC::SBhc::Get_BGM(int)" },
	{ "@LOCAL@sort<Pf>__3stdFPfPf_v@shuffle@0",
	  "void std::sort<float *>(float *, float *)::shuffle" },
	{ "@GUARD@app$16079",
	  "guard variable for app$16079" },
};

// SN Systems (SNC/ProDG). Same caveat as above, plus the format itself is reverse
// engineered from a small sample - see the comments in Demangle.cpp.
static const DemangleCase snSystemsCases[] = {
	{ "__0f5DstdIbad_castEwhatvK",
	  "std::bad_cast::what() const" },
	{ "__0F5DstdJTerminatev",
	  "std::Terminate()" },
	{ "__0F5INTskMenuKEventInit_6LtagSTaskHdl",
	  "NTskMenu::EventInit_(tagSTaskHdl)" },
	// A member function, and one in a namespace. The kind character decides how many name
	// components there are; a "5" marks an extra enclosing one.
	{ "__0fLCHeapMemoryFAlloci",
	  "CHeapMemory::Alloc(int)" },
	{ "__0f5FsoundMCSoundPlayerPGetPlayerObjectUi",
	  "sound::CSoundPlayer::GetPlayerObject(unsigned int)" },
	{ "__0fLCHeapMemoryKGetUseSizevK",
	  "CHeapMemory::GetUseSize() const" },
	// Constructors, destructors and operators, member and global.
	{ "__0oLCHeapMemoryctv",
	  "CHeapMemory::CHeapMemory()" },
	{ "__0oLCHeapMemorydtv",
	  "CHeapMemory::~CHeapMemory()" },
	{ "__0OnwUi",
	  "operator new(unsigned int)" },
	// operator new is implicitly static, which is what the trailing "T" marks.
	{ "__0oECMsgnwaUiT",
	  "static CMsg::operator new[](unsigned int)" },
	// Data, which has nothing after the name at all.
	{ "__0dLCHeapMemoryG__vtbl",
	  "CHeapMemory::__vtbl" },
	// "T<index>" repeats an earlier parameter, "N<count><index>" repeats it several times,
	// and "9<index>A" refers back to a template argument.
	{ "__0FDmax7f_RC9BATB_RC9BA",
	  "const float & max<float>(const float &, const float &)" },
	{ "__0FFClamp7i_RC9BANCB_RC9BA",
	  "const int & Clamp<int>(const int &, const int &, const int &)" },
	{ "__0FKRadixSortSPUiNCBUi",
	  "RadixSortS(unsigned int *, unsigned int *, unsigned int *, unsigned int)" },
	// Non-type template arguments: "0" is worth 52 each, and "8<len>" escapes to decimal.
	{ "__0dKTFixString748D512_G__vtbl",
	  "TFixString<512>::__vtbl" },
	{ "__0fLCFileListup74000s_LGetListFilei_R6KTFixString748D512_",
	  "TFixString<512> & CFileListup<200>::GetListFile(int)" },
	// Declarators have to be wrapped, not just suffixed.
	{ "__0F5DstdNset_terminatePFv_v",
	  "std::set_terminate(void (*)())" },
	{ "__0FKdecode_mcuP6Wjpeg_decompress_structPPA0Ms",
	  "decode_mcu(jpeg_decompress_struct *, short (**) [64])" },
	{ "__0FS_AfxDispatchCmdMsgP6KCCmdTargetUiiPMKCCmdTargetFR6KCCmdTarget_vPvTCP6SAFX_CMDHANDLERINFO",
	  "_AfxDispatchCmdMsg(CCmdTarget *, unsigned int, int, void (CCmdTarget::*)(CCmdTarget &), void *, unsigned int, AFX_CMDHANDLERINFO *)" },
	// Compiler-generated symbols. These spell an enclosing namespace as a plain component
	// rather than marking it with a "5".
	{ "__TID_LCHeapMemory",
	  "type id for CHeapMemory" },
	{ "__T_DstdIbad_cast",
	  "typeinfo for std::bad_cast" },
	{ "__TID_v",
	  "type id for void" },
	{ "__sti__CHeapMemory_cpp",
	  "static initializers for CHeapMemory_cpp" },
};

// Things that aren't Itanium-mangled names, or that we deliberately don't try to parse.
// All of these have to be reported as "not demangled" rather than half-parsed.
static const char *notMangled[] = {
	"",
	"_Z",
	"main",
	"sceKernelDelayThread",
	"z_un_088c00f0",
	"_GLOBAL__sub_I__ZN9__gnu_cxx9__freeresEv",
	"_ZZZ",
	"_ZN3foo",              // Unterminated nested name.
	"_ZN3foo3barES9_",      // Substitution past the end of the table.
	"_Z3fooIiEvT0_",        // Template parameter past the end of the argument list.
	"_Z3fooIXadL_Z1xEEEvv", // An <expression> template argument - no expression parser.
	"Foo__Bar",             // A "__" that isn't a CodeWarrior separator.
	"a__b__c",
	// The same, but where the tail happens to start with an "F" and a valid type code, so
	// only the lack of a class qualifier tells it apart from a real mangled name.
	"I3dClut__FlushCache",
	"@10046",               // An anonymous string constant: nothing to demangle.
	"__adddf3",             // A C runtime name that starts like an SN Systems one.
	"__sti",
	"__0",                  // Too short to be an SN Systems name.
	// A CodeWarrior template name, truncated by a 127-character symbol table limit. The
	// length prefixes no longer match what's left, so there's nothing to recover.
	"__distance<Q23std164__wrap_iterator<Q23std100vector<Q210Metrowerks20range_map_entry<w,w>,Q23std47allocator<Q210Metrowerks20rang",
};

bool TestDemangle() {
	for (const DemangleCase &testCase : demangleCases) {
		std::string out;
		if (!DemangleItanium(testCase.mangled, &out)) {
			printf("Failed to demangle %s\n", testCase.mangled);
			return false;
		}
		const std::string expected = testCase.expected;
		EXPECT_EQ_STR(out, expected);
	}

	for (const DemangleCase &testCase : codeWarriorCases) {
		DemangledSymbol sym;
		if (!DemangleCodeWarrior(testCase.mangled, &sym)) {
			printf("Failed to demangle %s\n", testCase.mangled);
			return false;
		}
		const std::string out = sym.ToString();
		const std::string expected = testCase.expected;
		EXPECT_EQ_STR(out, expected);
		// The parts have to add up to the same thing the wrapper prints.
		const std::string full = DemangleSymbolName(testCase.mangled);
		EXPECT_EQ_STR(full, expected);
	}

	for (const DemangleCase &testCase : snSystemsCases) {
		DemangledSymbol sym;
		if (!DemangleSNSystems(testCase.mangled, &sym)) {
			printf("Failed to demangle %s\n", testCase.mangled);
			return false;
		}
		const std::string out = sym.ToString();
		const std::string expected = testCase.expected;
		EXPECT_EQ_STR(out, expected);
		const std::string full = DemangleSymbolName(testCase.mangled);
		EXPECT_EQ_STR(full, expected);
	}

	// The split-up form is there for callers that want more than the printed string.
	{
		DemangledSymbol sym;
		EXPECT_TRUE(DemangleCodeWarrior("__ml__10DTVector3dCFf", &sym));
		EXPECT_EQ_STR(sym.name, std::string("DTVector3d::operator*"));
		EXPECT_EQ_STR(sym.parameters, std::string("float"));
		EXPECT_EQ_STR(sym.qualifiers, std::string("const"));
		EXPECT_TRUE(sym.isFunction);
		EXPECT_TRUE(DemangleCodeWarrior("count__Q23foo3bar", &sym));
		EXPECT_FALSE(sym.isFunction);
	}

	for (const char *name : notMangled) {
		std::string out = "unchanged";
		EXPECT_FALSE(DemangleItanium(name, &out));
		EXPECT_TRUE(out == "unchanged");
		DemangledSymbol sym;
		EXPECT_FALSE(DemangleCodeWarrior(name, &sym));
		EXPECT_FALSE(DemangleSNSystems(name, &sym));
		// The convenience wrapper hands back the original in that case.
		const std::string passedThrough = DemangleSymbolName(name);
		const std::string original = name;
		EXPECT_EQ_STR(passedThrough, original);
	}

	// Deeply nested types must fail on the depth cap rather than blowing the stack.
	std::string out;
	DemangleItanium("_Z3foo" + std::string(10000, 'P') + "i", &out);
	return true;
}
