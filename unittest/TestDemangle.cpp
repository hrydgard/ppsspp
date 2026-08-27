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

	for (const char *name : notMangled) {
		std::string out = "unchanged";
		EXPECT_FALSE(DemangleItanium(name, &out));
		EXPECT_TRUE(out == "unchanged");
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
