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

// Itanium C++ ABI demangler. See https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling
//
// Written to match what c++filt prints, for the subset of the grammar that real binaries
// actually contain. Anything unrecognized aborts the whole parse (failed_), so a caller
// gets either a correct demangling or the original mangled name back - never a half-parsed
// mess. The trickiest part is the substitution table (S_, S0_, ...): entries have to be
// appended in exactly the order the ABI says, or every later back-reference in the symbol
// resolves to the wrong thing.
//
// Two much simpler demanglers for older compilers - Metrowerks CodeWarrior and SN Systems -
// live at the bottom of the file.

#include <cstdio>
#include <cstring>
#include <vector>

#include "Common/Data/Text/Demangle.h"

namespace {

static void JoinInto(std::string &str, const std::vector<std::string> &parts, const char *sep) {
	for (size_t i = 0; i < parts.size(); i++) {
		if (i)
			str += sep;
		str += parts[i];
	}
}

// A single type, stored split around where a declarator name would go, so that function
// and array types can be wrapped correctly: "int (*)(char)" is pre="int (*", post=")(char)".
struct Decl {
	std::string pre;
	std::string post;
	// Function types take their cv-qualifiers after the parameter list, not before.
	bool isFunction = false;
	// Whether another & would collapse into this one rather than stack up.
	bool isReference = false;

	std::string str() const { return pre + post; }
};

// A type, or a parameter pack of them. A pack keeps its elements separate rather than
// pre-joined, because an expansion (Dp) applies to each: Dp O T_ over <A const&, int> is
// "A const&, int&&", not "&&" stuck on the end of the joined text.
struct TypeStr : public Decl {
	bool isPack = false;
	std::vector<Decl> items;

	std::string str() const {
		if (!isPack)
			return Decl::str();
		std::vector<std::string> parts;
		AppendTo(&parts);
		std::string out;
		JoinInto(out, parts, ", ");
		return out;
	}
	// Appends this type to a parameter or argument list, spreading a pack out over it.
	void AppendTo(std::vector<std::string> *list) const {
		if (!isPack) {
			list->push_back(Decl::str());
			return;
		}
		for (const Decl &item : items)
			list->push_back(item.str());
	}
	void AppendTo(std::vector<Decl> *list) const {
		if (isPack)
			list->insert(list->end(), items.begin(), items.end());
		else
			list->push_back(*this);
	}
};

// Applies a declarator transformation (pointer, reference, cv-qualifier) to a type, or to
// every element of a pack.
template <typename Func>
static TypeStr MapType(const TypeStr &type, Func func) {
	if (!type.isPack) {
		TypeStr out;
		static_cast<Decl &>(out) = func(static_cast<const Decl &>(type));
		return out;
	}
	TypeStr out;
	out.isPack = true;
	for (const Decl &item : type.items)
		out.items.push_back(func(item));
	return out;
}

// "A<B<C> >", not "A<B<C>>" - matches c++filt.
static void AppendTemplateArgs(std::string &str, const std::vector<TypeStr> &args) {
	std::vector<std::string> printable;
	for (const TypeStr &arg : args)
		arg.AppendTo(&printable);
	// "operator< <int>", not "operator<<int>".
	if (!str.empty() && str.back() == '<')
		str += ' ';
	str += '<';
	JoinInto(str, printable, ", ");
	if (!str.empty() && str.back() == '>')
		str += ' ';
	str += '>';
}

// The plain class name inside a possibly-qualified, possibly-templated one - which is what
// a constructor or destructor is spelled with, since the mangling doesn't repeat it.
// "std::basic_string<char, ...>" -> "basic_string".
static std::string BaseName(const std::string &name) {
	std::string base = name.substr(0, name.find('<'));
	base = base.substr(0, base.find('['));  // An abi tag isn't repeated on the ctor either.
	const size_t sep = base.rfind("::");
	return sep == std::string::npos ? base : base.substr(sep + 2);
}

struct Operator {
	const char code[3];
	const char *name;
};

// <operator-name>. The trailing "()"-less spelling is what gets "operator" prepended.
static const Operator operators[] = {
	{"aa", "&&"}, {"ad", "&"}, {"an", "&"}, {"aN", "&="}, {"aS", "="},
	{"cl", "()"}, {"cm", ","}, {"co", "~"}, {"da", " delete[]"}, {"de", "*"},
	{"dl", " delete"}, {"dv", "/"}, {"dV", "/="}, {"eo", "^"}, {"eO", "^="},
	{"eq", "=="}, {"ge", ">="}, {"gt", ">"}, {"ix", "[]"}, {"le", "<="},
	{"ls", "<<"}, {"lS", "<<="}, {"lt", "<"}, {"mi", "-"}, {"mI", "-="},
	{"ml", "*"}, {"mL", "*="}, {"mm", "--"}, {"na", " new[]"}, {"ne", "!="},
	{"ng", "-"}, {"nt", "!"}, {"nw", " new"}, {"oo", "||"}, {"or", "|"},
	{"oR", "|="}, {"pl", "+"}, {"pL", "+="}, {"pm", "->*"}, {"pp", "++"},
	{"ps", "+"}, {"pt", "->"}, {"qu", "?"}, {"rm", "%"}, {"rM", "%="},
	{"rs", ">>"}, {"rS", ">>="}, {"ss", "<=>"},
};

class Demangler {
public:
	explicit Demangler(std::string_view s) : s_(s) {}

	bool Run(std::string *out);

private:
	char Peek(size_t ahead = 0) const {
		return pos_ + ahead < s_.size() ? s_[pos_ + ahead] : '\0';
	}
	bool ConsumeIf(char c) {
		if (Peek() == c) {
			pos_++;
			return true;
		}
		return false;
	}
	bool ConsumeIf(const char *str) {
		const size_t len = strlen(str);
		if (s_.compare(pos_, len, str) == 0) {
			pos_ += len;
			return true;
		}
		return false;
	}
	void Fail() { failed_ = true; }

	std::string ParseEncoding(bool suppressReturnType = false);
	std::string ParseSpecialName();
	std::string ParseName(bool *endsWithTemplateArgs);
	std::string ParseNestedName(bool *endsWithTemplateArgs);
	std::string ParseLocalName();
	std::string ParseUnqualifiedName(const std::string &scope);
	std::string ParseSourceName();
	std::string ParseOperatorName();
	std::string ParseAbiTags();
	std::string ParseBareFunctionParams();
	std::vector<TypeStr> ParseTemplateArgs();
	TypeStr ParseExprPrimary();
	TypeStr ParseTemplateParam();
	bool ParseSubstitution(TypeStr *out, bool *isBackReference);
	TypeStr ParseType();
	bool ParseBuiltinType(std::string *out);
	int ParseNumber();  // <number>, negatives allowed
	int ParseSeqId();   // the base-36 part of a <substitution>

	std::string_view s_;
	size_t pos_ = 0;
	bool failed_ = false;

	std::vector<TypeStr> subs_;
	// The template args of the name currently being encoded, for resolving T_ / T0_ in
	// the function's own signature.
	std::vector<TypeStr> templateArgs_;
	// cv-/ref-qualifiers from a <nested-name>, which print after the parameter list.
	std::string nameQualifiers_;
	// Set when the name just parsed was a constructor, destructor or conversion operator.
	// Those never encode a return type, not even as templates.
	bool nameHasNoReturnType_ = false;
	// The grammar is recursive, and symbol names come from a file we didn't write, so cap
	// how deep a hostile one can drive the stack ("_ZPPPPP..." otherwise recurses per P).
	int depth_ = 0;

	static const int MAX_DEPTH = 128;

	// Increments depth_ for as long as it's alive, failing the parse if we're too deep.
	struct DepthGuard {
		explicit DepthGuard(Demangler *d) : d_(d) {
			if (++d_->depth_ > MAX_DEPTH)
				d_->Fail();
		}
		~DepthGuard() { d_->depth_--; }
		Demangler *d_;
	};
};

int Demangler::ParseNumber() {
	const bool negative = ConsumeIf('n');
	if (Peek() < '0' || Peek() > '9') {
		Fail();
		return 0;
	}
	int value = 0;
	while (Peek() >= '0' && Peek() <= '9') {
		if (value > 100000000) {  // Absurd length, and keeps the multiply from overflowing.
			Fail();
			return 0;
		}
		value = value * 10 + (s_[pos_++] - '0');
	}
	return negative ? -value : value;
}

int Demangler::ParseSeqId() {
	int value = 0;
	while (true) {
		const char c = Peek();
		int digit;
		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if (c >= 'A' && c <= 'Z')
			digit = c - 'A' + 10;
		else
			break;
		if (value > 10000) {
			Fail();
			return 0;
		}
		value = value * 36 + digit;
		pos_++;
	}
	return value;
}

std::string Demangler::ParseSourceName() {
	const int length = ParseNumber();
	if (failed_ || length <= 0 || pos_ + length > s_.size()) {
		Fail();
		return "";
	}
	std::string name(s_.substr(pos_, length));
	pos_ += length;
	// GCC's spelling for an anonymous namespace, which c++filt prints in the readable form.
	if (name.compare(0, 11, "_GLOBAL__N_") == 0)
		return "(anon)";
	return name;
}

// <abi-tags> ::= B <source-name> *
std::string Demangler::ParseAbiTags() {
	std::string tags;
	while (ConsumeIf('B')) {
		const std::string tag = ParseSourceName();
		if (failed_)
			return "";
		tags += "[abi:" + tag + "]";
	}
	return tags;
}

std::string Demangler::ParseOperatorName() {
	if (ConsumeIf("cv")) {
		// Conversion operator - "operator Foo".
		nameHasNoReturnType_ = true;
		const TypeStr type = ParseType();
		if (failed_)
			return "";
		return "operator " + type.str();
	}
	if (ConsumeIf("li")) {
		// Literal operator - operator"" _x.
		const std::string name = ParseSourceName();
		if (failed_)
			return "";
		return "operator\"\" " + name;
	}
	for (const Operator &op : operators) {
		if (Peek() == op.code[0] && Peek(1) == op.code[1]) {
			pos_ += 2;
			return std::string("operator") + op.name;
		}
	}
	Fail();
	return "";
}

// <unqualified-name>. "scope" is what this name is being appended to, needed to spell out
// constructors and destructors (which only encode which one, not the class name).
std::string Demangler::ParseUnqualifiedName(const std::string &scope) {
	std::string name;
	const char c = Peek();
	if (c >= '0' && c <= '9') {
		name = ParseSourceName();
	} else if (c == 'C') {
		// <ctor-dtor-name> ::= C1 | C2 | C3 | CI1 <type> | CI2 <type>
		pos_++;
		if (Peek() == 'I')
			pos_++;
		if (Peek() < '1' || Peek() > '5') {
			Fail();
			return "";
		}
		pos_++;
		nameHasNoReturnType_ = true;
		name = scope;
	} else if (c == 'D' && Peek(1) >= '0' && Peek(1) <= '5') {
		pos_ += 2;
		nameHasNoReturnType_ = true;
		name = "~" + scope;
	} else if (c == 'U' && Peek(1) == 't') {
		// <unnamed-type-name> ::= Ut [<number>] _
		pos_ += 2;
		const int index = (Peek() >= '0' && Peek() <= '9') ? ParseNumber() : -1;
		if (failed_ || !ConsumeIf('_')) {
			Fail();
			return "";
		}
		char temp[32];
		snprintf(temp, sizeof(temp), "{unnamed type#%d}", index + 2);
		name = temp;
	} else if (c == 'U' && Peek(1) == 'l') {
		// <closure-type-name> ::= Ul <lambda-sig> E [<number>] _
		pos_ += 2;
		std::vector<std::string> params;
		while (!ConsumeIf('E')) {
			if (pos_ >= s_.size()) {
				Fail();
				return "";
			}
			const TypeStr param = ParseType();
			if (failed_)
				return "";
			param.AppendTo(&params);
		}
		if (params.size() == 1 && params[0] == "void")
			params.clear();
		const int index = (Peek() >= '0' && Peek() <= '9') ? ParseNumber() : -1;
		if (failed_ || !ConsumeIf('_')) {
			Fail();
			return "";
		}
		name = "{lambda(";
		JoinInto(name, params, ", ");
		name += ")#" + std::to_string(index + 2) + "}";
	} else if (c == 'L') {
		// Internal-linkage name, GCC extension: L <source-name>
		pos_++;
		name = ParseSourceName();
	} else {
		name = ParseOperatorName();
	}
	if (failed_)
		return "";
	return name + ParseAbiTags();
}

// <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E
std::string Demangler::ParseNestedName(bool *endsWithTemplateArgs) {
	const DepthGuard guard(this);
	if (failed_)
		return "";
	if (!ConsumeIf('N')) {
		Fail();
		return "";
	}
	bool restrict = ConsumeIf('r');
	bool volatil = ConsumeIf('V');
	bool cnst = ConsumeIf('K');
	if (cnst)
		nameQualifiers_ += " const";
	if (volatil)
		nameQualifiers_ += " volatile";
	if (restrict)
		nameQualifiers_ += " restrict";
	if (ConsumeIf('R'))
		nameQualifiers_ += " &";
	else if (ConsumeIf('O'))
		nameQualifiers_ += " &&";

	std::string soFar;
	// The name of the innermost component so far, which is the class name a C1/D1 refers to.
	std::string lastComponent;
	bool noReturnType = false;
	*endsWithTemplateArgs = false;
	while (!ConsumeIf('E')) {
		if (pos_ >= s_.size()) {
			Fail();
			return "";
		}
		const char c = Peek();
		if (c == 'I') {
			// <template-prefix> <template-args> - applies to what we have so far.
			const std::vector<TypeStr> args = ParseTemplateArgs();
			if (failed_)
				return "";
			if (soFar.empty()) {
				Fail();
				return "";
			}
			AppendTemplateArgs(soFar, args);
			templateArgs_ = args;
			*endsWithTemplateArgs = true;
		} else {
			if (c == 'S') {
				// A substitution can only stand in for the prefix, i.e. come first. Parse
				// it here rather than through ParseType, which would also swallow any
				// following <template-args> and record the combination a second time.
				TypeStr sub;
				bool isBackReference = false;
				if (!ParseSubstitution(&sub, &isBackReference) || !soFar.empty()) {
					Fail();
					return "";
				}
				soFar = sub.str();
				lastComponent = soFar;
				*endsWithTemplateArgs = false;
				if (isBackReference)
					continue;  // Already in the table - re-adding would shift every later S<n>_.
			} else {
				std::string component;
				if (c == 'T') {
					component = ParseTemplateParam().str();
				} else {
					nameHasNoReturnType_ = false;
					component = ParseUnqualifiedName(BaseName(lastComponent));
					noReturnType = nameHasNoReturnType_;
				}
				if (failed_)
					return "";
				lastComponent = component;
				if (!soFar.empty())
					soFar += "::";
				soFar += component;
				*endsWithTemplateArgs = false;
			}
		}
		// Every <prefix> is a substitution candidate, but the complete <nested-name> is
		// only one when it's used as a type - and then our caller in ParseType adds it.
		if (Peek() != 'E')
			subs_.push_back(TypeStr{ soFar, "" });
	}
	// Set last, so that anything nested (template arguments, in particular) can't clobber it.
	nameHasNoReturnType_ = noReturnType;
	return soFar;
}

// <local-name> ::= Z <encoding> E <name> [<discriminator>] | Z <encoding> E s [<discriminator>]
std::string Demangler::ParseLocalName() {
	if (!ConsumeIf('Z')) {
		Fail();
		return "";
	}
	// The enclosing function has its own qualifier state; don't let it leak out.
	const std::string savedQualifiers = nameQualifiers_;
	nameQualifiers_.clear();
	// c++filt leaves the enclosing function's return type off in this position.
	std::string outer = ParseEncoding(true);
	nameQualifiers_ = savedQualifiers;
	if (failed_ || !ConsumeIf('E'))
		return "";
	std::string inner;
	if (ConsumeIf('s')) {
		inner = "string literal";
	} else {
		bool unusedTemplateArgs = false;
		inner = ParseName(&unusedTemplateArgs);
		if (failed_)
			return "";
	}
	// <discriminator> ::= _ <non-negative number> | __ <number> _ . Purely disambiguating,
	// and c++filt doesn't print it either.
	if (ConsumeIf("__")) {
		ParseNumber();
		ConsumeIf('_');
	} else if (Peek() == '_' && Peek(1) >= '0' && Peek(1) <= '9') {
		pos_++;
		ParseNumber();
	}
	if (failed_)
		return "";
	return outer + "::" + inner;
}

TypeStr Demangler::ParseTemplateParam() {
	if (!ConsumeIf('T')) {
		Fail();
		return TypeStr();
	}
	int index = 0;
	if (Peek() != '_') {
		index = ParseNumber() + 1;
		if (failed_)
			return TypeStr();
	}
	if (!ConsumeIf('_')) {
		Fail();
		return TypeStr();
	}
	if (index < 0 || index >= (int)templateArgs_.size()) {
		Fail();
		return TypeStr();
	}
	return templateArgs_[index];
}

// <expr-primary> ::= L <type> <value> E | L <mangled-name> E
TypeStr Demangler::ParseExprPrimary() {
	if (!ConsumeIf('L')) {
		Fail();
		return TypeStr();
	}
	if (Peek() == '_' && Peek(1) == 'Z') {
		// An external name used as a template argument (function or object pointer).
		const size_t start = pos_;
		while (pos_ < s_.size() && s_[pos_] != 'E')
			pos_++;
		Demangler sub(s_.substr(start, pos_ - start));
		std::string demangled;
		if (!sub.Run(&demangled))
			demangled = std::string(s_.substr(start, pos_ - start));
		if (!ConsumeIf('E')) {
			Fail();
			return TypeStr();
		}
		return TypeStr{ demangled };
	}
	const TypeStr type = ParseType();
	if (failed_)
		return TypeStr();
	const size_t start = pos_;
	while (pos_ < s_.size() && s_[pos_] != 'E')
		pos_++;
	std::string value(s_.substr(start, pos_ - start));
	if (!ConsumeIf('E')) {
		Fail();
		return TypeStr();
	}
	if (value.empty()) {
		Fail();
		return TypeStr();
	}
	if (value[0] == 'n')
		value = "-" + value.substr(1);
	const std::string typeName = type.str();
	if (typeName == "bool")
		return TypeStr{ value == "0" ? "false" : "true" };
	if (typeName == "int")
		return TypeStr{ value };
	if (typeName == "long")
		return TypeStr{ value + "l" };
	if (typeName == "unsigned int")
		return TypeStr{ value + "u" };
	if (typeName == "unsigned long")
		return TypeStr{ value + "ul" };
	return TypeStr{ "(" + typeName + ")" + value };
}

std::vector<TypeStr> Demangler::ParseTemplateArgs() {
	std::vector<TypeStr> args;
	if (!ConsumeIf('I')) {
		Fail();
		return args;
	}
	while (!ConsumeIf('E')) {
		if (pos_ >= s_.size()) {
			Fail();
			return args;
		}
		if (Peek() == 'X') {
			// An arbitrary constant expression. We don't have an expression parser, and
			// guessing would produce nonsense - fail cleanly instead.
			Fail();
			return args;
		} else if (Peek() == 'J') {
			// <template-arg> ::= J <template-arg>* E  (parameter pack)
			pos_++;
			TypeStr pack;
			pack.isPack = true;
			while (!ConsumeIf('E')) {
				if (pos_ >= s_.size()) {
					Fail();
					return args;
				}
				const TypeStr type = ParseType();
				if (failed_)
					return args;
				type.AppendTo(&pack.items);
			}
			args.push_back(pack);
		} else if (Peek() == 'L') {
			args.push_back(ParseExprPrimary());
		} else {
			args.push_back(ParseType());
		}
		if (failed_)
			return args;
	}
	return args;
}

bool Demangler::ParseBuiltinType(std::string *out) {
	const char *name = nullptr;
	switch (Peek()) {
	case 'v': name = "void"; break;
	case 'w': name = "wchar_t"; break;
	case 'b': name = "bool"; break;
	case 'c': name = "char"; break;
	case 'a': name = "signed char"; break;
	case 'h': name = "unsigned char"; break;
	case 's': name = "short"; break;
	case 't': name = "unsigned short"; break;
	case 'i': name = "int"; break;
	case 'j': name = "unsigned int"; break;
	case 'l': name = "long"; break;
	case 'm': name = "unsigned long"; break;
	case 'x': name = "long long"; break;
	case 'y': name = "unsigned long long"; break;
	case 'n': name = "__int128"; break;
	case 'o': name = "unsigned __int128"; break;
	case 'f': name = "float"; break;
	case 'd': name = "double"; break;
	case 'e': name = "long double"; break;
	case 'g': name = "__float128"; break;
	case 'z': name = "..."; break;
	case 'D':
		switch (Peek(1)) {
		case 'a': name = "auto"; break;
		case 'c': name = "decltype(auto)"; break;
		case 'd': name = "decimal64"; break;
		case 'e': name = "decimal128"; break;
		case 'f': name = "decimal32"; break;
		case 'h': name = "half"; break;
		case 'i': name = "char32_t"; break;
		case 's': name = "char16_t"; break;
		case 'n': name = "decltype(nullptr)"; break;
		case 'u': name = "char8_t"; break;
		case 'F': {
			// DF <number> _ is _FloatN, DF <number> x is _FloatNx.
			size_t end = pos_ + 2;
			while (end < s_.size() && s_[end] >= '0' && s_[end] <= '9')
				end++;
			if (end == pos_ + 2 || end >= s_.size() || (s_[end] != '_' && s_[end] != 'x'))
				return false;
			*out = "_Float" + std::string(s_.substr(pos_ + 2, end - pos_ - 2));
			if (s_[end] == 'x')
				*out += 'x';
			pos_ = end + 1;
			return true;
		}
		default: return false;
		}
		pos_ += 2;
		*out = name;
		return true;
	default:
		return false;
	}
	pos_++;
	*out = name;
	return true;
}

// <substitution> ::= S <seq-id> _ | S_ | St | Sa | Sb | Ss | Si | So | Sd
// *isBackReference says whether the result is already in the substitution table, i.e.
// whether re-adding it would throw off every later back-reference in the symbol.
bool Demangler::ParseSubstitution(TypeStr *out, bool *isBackReference) {
	pos_++;  // 'S'
	const char abbrev = Peek();
	const char *stdName = nullptr;
	switch (abbrev) {
	case 't': stdName = "std"; break;
	case 'a': stdName = "std::allocator"; break;
	case 'b': stdName = "std::basic_string"; break;
	case 's': stdName = "std::basic_string<char, std::char_traits<char>, std::allocator<char> >"; break;
	case 'i': stdName = "std::basic_istream<char, std::char_traits<char> >"; break;
	case 'o': stdName = "std::basic_ostream<char, std::char_traits<char> >"; break;
	case 'd': stdName = "std::basic_iostream<char, std::char_traits<char> >"; break;
	default: break;
	}
	if (stdName) {
		pos_++;
		out->pre = stdName;
		// St is a namespace prefix: St <unqualified-name>. The combination is a new name,
		// so unlike the other abbreviations it is a substitution candidate.
		*isBackReference = abbrev != 't';
		if (abbrev == 't') {
			const std::string name = ParseUnqualifiedName("");
			if (failed_)
				return false;
			out->pre += "::" + name;
		}
		return true;
	}
	const int index = Peek() == '_' ? 0 : ParseSeqId() + 1;
	if (failed_ || !ConsumeIf('_')) {
		Fail();
		return false;
	}
	if (index < 0 || index >= (int)subs_.size()) {
		Fail();
		return false;
	}
	*out = subs_[index];
	*isBackReference = true;
	return true;
}

TypeStr Demangler::ParseType() {
	TypeStr result;
	bool addSub = true;

	const DepthGuard guard(this);
	if (failed_)
		return result;

	std::string builtin;
	if (ParseBuiltinType(&builtin)) {
		// Builtin types are never substitution candidates.
		result.pre = builtin;
		return result;
	}

	const char c = Peek();
	switch (c) {
	case 'P':
	case 'R':
	case 'O':
	{
		pos_++;
		const char *op = c == 'P' ? "*" : (c == 'R' ? "&" : "&&");
		const TypeStr inner = ParseType();
		if (failed_)
			return result;
		result = MapType(inner, [op](Decl type) {
			if (op[0] == '&' && type.isReference) {
				// Reference collapsing - only reachable through a template parameter that
				// was itself a reference type.
			} else if (!type.post.empty()) {
				// Function or array type - the pointer has to go inside parentheses.
				if (!type.pre.empty() && type.pre.back() != ' ')
					type.pre += ' ';
				type.pre += '(';
				type.pre += op;
				type.post = ")" + type.post;
			} else {
				type.pre += op;
			}
			type.isFunction = false;
			type.isReference = op[0] == '&' || type.isReference;
			return type;
		});
		break;
	}
	case 'C':  // complex
	case 'G':  // imaginary
	{
		pos_++;
		const TypeStr inner = ParseType();
		if (failed_)
			return result;
		const char *prefix = c == 'C' ? "complex " : "imaginary ";
		result = MapType(inner, [prefix](Decl type) {
			type.pre = prefix + type.pre;
			return type;
		});
		break;
	}
	case 'r':
	case 'V':
	case 'K':
	{
		const bool restrict = ConsumeIf('r');
		const bool volatil = ConsumeIf('V');
		const bool cnst = ConsumeIf('K');
		const TypeStr inner = ParseType();
		if (failed_)
			return result;
		std::string quals;
		if (cnst)
			quals += " const";
		if (volatil)
			quals += " volatile";
		if (restrict)
			quals += " restrict";
		result = MapType(inner, [&quals](Decl type) {
			if (type.isFunction)
				type.post += quals;
			else
				type.pre += quals;
			return type;
		});
		// A cv-qualified function type only ever appears as the pointee of a
		// pointer-to-member, where the ABI doesn't make it a candidate of its own.
		addSub = !inner.isFunction;
		break;
	}
	case 'A':
	{
		// <array-type> ::= A <number> _ <type> | A [<expression>] _ <type>
		pos_++;
		std::string bound;
		if (Peek() >= '0' && Peek() <= '9') {
			const int n = ParseNumber();
			if (failed_)
				return result;
			bound = std::to_string(n);
		} else if (Peek() != '_') {
			Fail();
			return result;
		}
		if (!ConsumeIf('_')) {
			Fail();
			return result;
		}
		TypeStr inner = ParseType();
		if (failed_)
			return result;
		result.pre = inner.pre;
		result.post = " [" + bound + "]";
		// A multidimensional array is "[5][4]", with no space between the dimensions.
		result.post += inner.post.compare(0, 2, " [") == 0 ? inner.post.substr(1) : inner.post;
		break;
	}
	case 'M':
	{
		// <pointer-to-member-type> ::= M <class type> <member type>
		pos_++;
		const TypeStr classType = ParseType();
		if (failed_)
			return result;
		TypeStr member = ParseType();
		if (failed_)
			return result;
		if (member.post.empty()) {
			result.pre = member.pre + " " + classType.str() + "::*";
		} else {
			if (!member.pre.empty() && member.pre.back() != ' ')
				member.pre += ' ';
			result.pre = member.pre + "(" + classType.str() + "::*";
			result.post = ")" + member.post;
		}
		result.isFunction = false;
		break;
	}
	case 'F':
	{
		// <function-type> ::= F [Y] <bare-function-type> [<ref-qualifier>] E
		pos_++;
		ConsumeIf('Y');
		const TypeStr ret = ParseType();
		if (failed_)
			return result;
		std::vector<std::string> params;
		while (Peek() != 'E') {
			if (pos_ >= s_.size()) {
				Fail();
				return result;
			}
			// A trailing R or O is the ref-qualifier, not another parameter.
			if ((Peek() == 'R' || Peek() == 'O') && Peek(1) == 'E')
				break;
			const TypeStr param = ParseType();
			if (failed_)
				return result;
			param.AppendTo(&params);
		}
		std::string suffix;
		if (ConsumeIf('R'))
			suffix = " &";
		else if (ConsumeIf('O'))
			suffix = " &&";
		if (!ConsumeIf('E')) {
			Fail();
			return result;
		}
		if (params.size() == 1 && params[0] == "void")
			params.clear();
		result.pre = ret.str() + " ";
		result.post = "(";
		JoinInto(result.post, params, ", ");
		result.post += ")" + suffix;
		result.isFunction = true;
		break;
	}
	case 'T':
	{
		result = ParseTemplateParam();
		if (failed_)
			return result;
		if (Peek() == 'I') {
			// A template template parameter applied to arguments.
			subs_.push_back(result);
			const std::vector<TypeStr> args = ParseTemplateArgs();
			if (failed_)
				return result;
			AppendTemplateArgs(result.pre, args);
		}
		break;
	}
	case 'S':
	{
		bool isBackReference = false;
		if (!ParseSubstitution(&result, &isBackReference))
			return result;
		addSub = !isBackReference;
		if (Peek() == 'I') {
			// The template name itself is a candidate, and so is "name<args>".
			if (addSub)
				subs_.push_back(result);
			addSub = true;
			const std::vector<TypeStr> args = ParseTemplateArgs();
			if (failed_)
				return result;
			AppendTemplateArgs(result.pre, args);
		}
		break;
	}
	case 'N':
	{
		bool unusedTemplateArgs = false;
		// A nested name used as a type carries no cv-qualifier suffix of its own, and its
		// template arguments are not the ones T_ in the enclosing signature refers to.
		const std::string savedQualifiers = nameQualifiers_;
		const std::vector<TypeStr> savedTemplateArgs = templateArgs_;
		result.pre = ParseNestedName(&unusedTemplateArgs);
		nameQualifiers_ = savedQualifiers;
		templateArgs_ = savedTemplateArgs;
		if (failed_)
			return result;
		break;
	}
	case 'Z':
	{
		const std::vector<TypeStr> savedTemplateArgs = templateArgs_;
		result.pre = ParseLocalName();
		templateArgs_ = savedTemplateArgs;
		if (failed_)
			return result;
		break;
	}
	case 'u':
	{
		// <vendor-extended-type> ::= u <source-name>
		pos_++;
		result.pre = ParseSourceName();
		if (failed_)
			return result;
		break;
	}
	case 'D':
		if (Peek(1) == 'p') {
			// <type> ::= Dp <type>, a pack expansion. The pack was already flattened when
			// its <template-arg> was parsed, so this is transparent.
			pos_ += 2;
			return ParseType();
		}
		// Dt/DT (decltype) - no expression parser, so give up rather than guess.
		Fail();
		return result;
	default:
		if (c >= '0' && c <= '9') {
			result.pre = ParseSourceName();
			if (failed_)
				return result;
			if (Peek() == 'I') {
				subs_.push_back(result);
				const std::vector<TypeStr> args = ParseTemplateArgs();
				if (failed_)
					return result;
				AppendTemplateArgs(result.pre, args);
			}
		} else {
			Fail();
			return result;
		}
		break;
	}

	if (addSub)
		subs_.push_back(result);
	return result;
}

std::string Demangler::ParseName(bool *endsWithTemplateArgs) {
	*endsWithTemplateArgs = false;
	const char c = Peek();
	if (c == 'N')
		return ParseNestedName(endsWithTemplateArgs);
	if (c == 'Z')
		return ParseLocalName();

	nameHasNoReturnType_ = false;
	std::string name;
	if (c == 'S') {
		// <unscoped-name> ::= St <unqualified-name>, or a plain <substitution> standing in
		// for the template name. Either way ParseType knows how to read it - but it would
		// also add "St <name>" to the substitution table, which for an <unscoped-name> is
		// wrong, so handle the St case here.
		if (Peek(1) == 't') {
			pos_ += 2;
			name = "std::" + ParseUnqualifiedName("");
			if (failed_)
				return "";
		} else {
			const TypeStr type = ParseType();
			if (failed_)
				return "";
			return type.str();  // Already had its template args applied, if any.
		}
	} else {
		name = ParseUnqualifiedName("");
		if (failed_)
			return "";
	}

	if (Peek() == 'I') {
		// <unscoped-template-name> <template-args> - the template name is a candidate.
		subs_.push_back(TypeStr{ name, "" });
		const std::vector<TypeStr> args = ParseTemplateArgs();
		if (failed_)
			return "";
		AppendTemplateArgs(name, args);
		templateArgs_ = args;
		*endsWithTemplateArgs = true;
	}
	return name;
}

std::string Demangler::ParseBareFunctionParams() {
	std::vector<std::string> params;
	int count = 0;
	while (pos_ < s_.size() && Peek() != 'E' && Peek() != '.') {
		const TypeStr type = ParseType();
		if (failed_)
			return "";
		count++;
		type.AppendTo(&params);
	}
	if (!count) {
		// A function always encodes at least "v" for (void), so this isn't one.
		Fail();
		return "";
	}
	if (params.size() == 1 && params[0] == "void")
		params.clear();
	std::string out = "(";
	JoinInto(out, params, ", ");
	out += ")";
	return out;
}

std::string Demangler::ParseSpecialName() {
	if (ConsumeIf("TV") || ConsumeIf("TT") || ConsumeIf("TI") || ConsumeIf("TS")) {
		const char kind = s_[pos_ - 1];
		const TypeStr type = ParseType();
		if (failed_)
			return "";
		const char *prefix = kind == 'V' ? "vtable for " : (kind == 'T' ? "VTT for " :
			(kind == 'I' ? "typeinfo for " : "typeinfo name for "));
		return prefix + type.str();
	}
	if (ConsumeIf("GTt") || ConsumeIf("GTn")) {
		const bool nonVirtual = s_[pos_ - 1] == 'n';
		const std::string target = ParseEncoding();
		if (failed_)
			return "";
		return (nonVirtual ? "non-transaction clone for " : "transaction clone for ") + target;
	}
	if (ConsumeIf("GV")) {
		bool unused = false;
		const std::string name = ParseName(&unused);
		if (failed_)
			return "";
		return "guard variable for " + name;
	}
	if (ConsumeIf("GR")) {
		bool unused = false;
		const std::string name = ParseName(&unused);
		if (failed_)
			return "";
		// Followed by a seq-id we don't need to print.
		ParseSeqId();
		ConsumeIf('_');
		return "reference temporary for " + name;
	}
	// <call-offset> thunks: Th <offset> _ <encoding>, Tv <offset> _ <offset> _ <encoding>
	if (ConsumeIf("Th") || ConsumeIf("Tv")) {
		const bool virt = s_[pos_ - 1] == 'v';
		ParseNumber();
		if (failed_ || !ConsumeIf('_'))
			return "";
		if (virt) {
			ParseNumber();
			if (failed_ || !ConsumeIf('_'))
				return "";
		}
		const std::string target = ParseEncoding();
		if (failed_)
			return "";
		return (virt ? "virtual thunk to " : "non-virtual thunk to ") + target;
	}
	Fail();
	return "";
}

std::string Demangler::ParseEncoding(bool suppressReturnType) {
	const DepthGuard guard(this);
	if (failed_)
		return "";
	if (Peek() == 'T' || Peek() == 'G')
		return ParseSpecialName();

	// Each encoding has its own qualifier and template-arg scope (local names nest them).
	const std::string savedQualifiers = nameQualifiers_;
	const std::vector<TypeStr> savedTemplateArgs = templateArgs_;
	nameQualifiers_.clear();

	bool endsWithTemplateArgs = false;
	nameHasNoReturnType_ = false;
	const std::string name = ParseName(&endsWithTemplateArgs);
	if (failed_) {
		nameQualifiers_ = savedQualifiers;
		templateArgs_ = savedTemplateArgs;
		return "";
	}

	std::string result;
	if (pos_ >= s_.size() || Peek() == 'E' || Peek() == '.') {
		// <data name>, no function type follows.
		result = name;
	} else {
		std::string returnType;
		if (endsWithTemplateArgs && !nameHasNoReturnType_) {
			// Only templates encode their return type, since it can depend on the args.
			const TypeStr type = ParseType();
			// Still has to be parsed when suppressed, or it'd be read as a parameter.
			if (!failed_ && !suppressReturnType)
				returnType = type.str() + " ";
		}
		const std::string params = failed_ ? "" : ParseBareFunctionParams();
		result = returnType + name + params + nameQualifiers_;
	}

	nameQualifiers_ = savedQualifiers;
	templateArgs_ = savedTemplateArgs;
	return failed_ ? "" : result;
}

bool Demangler::Run(std::string *out) {
	if (s_.size() < 3 || s_[0] != '_' || s_[1] != 'Z')
		return false;
	pos_ = 2;
	std::string result = ParseEncoding();
	if (failed_ || result.empty())
		return false;
	if (pos_ < s_.size()) {
		// GCC appends things like ".constprop.0" or ".isra.0" to clones of a function.
		if (s_[pos_] != '.')
			return false;
		result += " [clone " + std::string(s_.substr(pos_)) + "]";
	}
	*out = result;
	return true;
}

}  // namespace

bool DemangleItanium(std::string_view mangled, std::string *out) {
	Demangler demangler(mangled);
	return demangler.Run(out);
}


// Metrowerks CodeWarrior, a descendant of the AT&T cfront scheme:
//
//   getDistance__6KzUtilFP7st_unitP7st_unit  ->  KzUtil::getDistance(st_unit *, st_unit *)
//
// "<name>__<class><cv>F<params>", where <class> is a length-prefixed identifier or a
// Q<n>-introduced chain of them, and the parameters are cfront type codes. Rough by design:
// the goal is a correctly qualified name plus a plausible parameter list, not byte-exact
// agreement with any particular demangler.
//
// Two places deviate from cfront, both worked out from real PSP binaries rather than from
// the Macintosh ABI document (which is wrong about them): template arguments are written
// literally as "<...>" inside the length-prefixed name rather than with a "__PT" prefix, and
// there is a whole family of "@"-decorated names for symbols the compiler generates itself.
// See DemangleCodeWarriorSpecial below for those, and docs/CodeWarriorMangling.md for a
// description of the whole format.

namespace {

struct CWOperator {
	const char *code;
	const char *name;
};

// Operator codes, as spelled after the leading "__". Matched against the whole name, so the
// three-letter ones don't need to come first.
static const CWOperator cwOperators[] = {
	{"nwa", "new[]"}, {"dla", "delete[]"}, {"nw", "new"}, {"dl", "delete"},
	{"apl", "+="}, {"ami", "-="}, {"amu", "*="}, {"adv", "/="}, {"amd", "%="},
	{"aad", "&="}, {"aor", "|="}, {"aer", "^="}, {"als", "<<="}, {"ars", ">>="},
	{"pl", "+"}, {"mi", "-"}, {"ml", "*"}, {"dv", "/"}, {"md", "%"},
	{"eq", "=="}, {"ne", "!="}, {"lt", "<"}, {"gt", ">"}, {"le", "<="}, {"ge", ">="},
	{"aa", "&&"}, {"oo", "||"}, {"nt", "!"}, {"co", "~"}, {"er", "^"},
	{"ad", "&"}, {"or", "|"}, {"ls", "<<"}, {"rs", ">>"}, {"as", "="},
	{"pp", "++"}, {"mm", "--"}, {"cl", "()"}, {"vc", "[]"}, {"rf", "->"},
	{"rm", "->*"}, {"cm", ","},
};

static const char *CWBasicType(char c) {
	switch (c) {
	case 'v': return "void";
	case 'c': return "char";
	case 's': return "short";
	case 'i': return "int";
	case 'l': return "long";
	case 'x': return "long long";
	case 'f': return "float";
	case 'd': return "double";
	case 'r': return "long double";
	case 'b': return "bool";
	case 'w': return "wchar_t";  // A Metrowerks addition to the cfront set.
	default: return nullptr;
	}
}

static bool IsDigit(char c) {
	return c >= '0' && c <= '9';
}

// A type, split around where a declarator name would go, so a pointer to a function or an
// array can be wrapped: "int (*)(char)" is pre="int (*", post=")(char)". Plain types leave
// post empty. Same idea as Decl above, kept separate because the two manglings share nothing.
struct CWDecl {
	std::string pre;
	std::string post;

	std::string str() const { return pre + post; }
};

// Whether a declarator's prefix is already open for a "*" or "&" to be written into it - that
// is, whether it came from a function type ("int ("), a pointer to one ("int (*"), or a
// pointer to member ("int (Foo::").
static bool EndsDeclarator(const std::string &pre) {
	return !pre.empty() && (pre.back() == '(' || pre.back() == '*' || pre.back() == ':');
}

class CodeWarriorParser {
public:
	// strict: fail outright on a parameter we can't decode, rather than printing "...".
	CodeWarriorParser(std::string_view s, bool strict) : s_(s), strict_(strict) {}

	// Parses everything after the "__" separator, and fills in out (except for the name,
	// which the caller builds from the qualifier and the part before the separator).
	bool ParseAfterSeparator(DemangledSymbol *out);

	const std::string &qualifier() const { return qualifier_; }

	// Standalone entry point, for the type after a "__op" conversion-operator name.
	std::string ParseWholeType();

	// Standalone entry point, for a template function's arguments, which are spelled out in
	// the name rather than in the part after the "__".
	std::string ParseTemplateArgs(std::string_view args);

private:
	char Peek() const { return pos_ < s_.size() ? s_[pos_] : 0; }
	bool Fail() { failed_ = true; return false; }
	int ParseNumber();
	std::string ParseIdentifier();
	std::string ParseQualifiedName();
	CWDecl ParseDecl();
	std::string ParseType() { return ParseDecl().str(); }
	bool ParseParams(std::string *out);

	std::string_view s_;
	size_t pos_ = 0;
	bool failed_ = false;
	bool strict_ = true;
	int depth_ = 0;
	std::string qualifier_;
	std::vector<std::string> params_;  // Targets of T/N back-references.
};

int CodeWarriorParser::ParseNumber() {
	if (!IsDigit(Peek()))
		return -1;
	int value = 0;
	while (IsDigit(Peek())) {
		value = value * 10 + (s_[pos_] - '0');
		if (value > 1000000)
			return -1;
		pos_++;
	}
	return value;
}

std::string CodeWarriorParser::ParseIdentifier() {
	const int len = ParseNumber();
	// A too-long length means the symbol was truncated by whatever wrote the symbol table -
	// 127 characters is a common limit, and template names blow past it easily. Nothing
	// useful survives, so don't guess.
	if (len <= 0 || pos_ + (size_t)len > s_.size()) {
		Fail();
		return "";
	}
	std::string name(s_.substr(pos_, len));
	pos_ += len;
	// A class template's arguments live inside the length-prefixed name, spelled with the
	// same type codes as everywhere else: "39CList<Q38hlScreen5Brwsr13CContentsUnit>".
	const size_t lt = name.find('<');
	if (lt != std::string::npos && name.back() == '>') {
		const std::string_view args = std::string_view(name).substr(lt + 1, name.size() - lt - 2);
		name = name.substr(0, lt) + ParseTemplateArgs(args);
	}
	return name;
}

// The inside of a "<...>", as a comma-separated list of types and integer literals. Returns
// the whole thing including the brackets, and falls back to the raw text if any argument
// doesn't parse - half a decoded argument list is worse than the mangled one.
std::string CodeWarriorParser::ParseTemplateArgs(std::string_view args) {
	std::vector<std::string> parts;
	size_t start = 0;
	int depth = 0;
	for (size_t i = 0; i <= args.size(); i++) {
		if (i < args.size()) {
			if (args[i] == '<')
				depth++;
			else if (args[i] == '>')
				depth--;
			// Only a top-level comma separates arguments; a nested "<...>" has its own.
			if (args[i] != ',' || depth != 0)
				continue;
		}
		const std::string_view arg = args.substr(start, i - start);
		start = i + 1;
		if (arg.empty())
			return "<" + std::string(args) + ">";
		// All digits is a non-type argument ("CSimpleChar<24>"); anything else is a type.
		size_t digits = arg[0] == '-' ? 1 : 0;
		while (digits < arg.size() && IsDigit(arg[digits]))
			digits++;
		if (digits == arg.size()) {
			parts.push_back(std::string(arg));
			continue;
		}
		CodeWarriorParser sub(arg, true);
		const std::string type = sub.ParseWholeType();
		if (type.empty())
			return "<" + std::string(args) + ">";
		parts.push_back(type);
	}
	std::string out;
	JoinInto(out, parts, ", ");
	return "<" + out + ">";
}

// "9ANIMEData", or "Q33std10ctype_base4mask" for a qualified one. The count after Q is a
// single digit, with "Q_<n>_" for the rare case of ten or more components.
std::string CodeWarriorParser::ParseQualifiedName() {
	if (Peek() != 'Q')
		return ParseIdentifier();
	pos_++;
	int count;
	if (Peek() == '_') {
		pos_++;
		count = ParseNumber();
		if (Peek() != '_')
			count = -1;
		else
			pos_++;
	} else {
		count = IsDigit(Peek()) ? (s_[pos_++] - '0') : -1;
	}
	if (count <= 0) {
		Fail();
		return "";
	}
	std::vector<std::string> parts;
	for (int i = 0; i < count; i++) {
		parts.push_back(ParseIdentifier());
		if (failed_)
			return "";
	}
	std::string out;
	JoinInto(out, parts, "::");
	return out;
}

CWDecl CodeWarriorParser::ParseDecl() {
	if (failed_ || depth_ > 64) {
		Fail();
		return CWDecl();
	}
	depth_++;
	CWDecl result;
	const char c = Peek();
	switch (c) {
	case 'P':
	case 'R':
	{
		pos_++;
		result = ParseDecl();
		const char sigil = c == 'P' ? '*' : '&';
		if (result.post.empty()) {
			result.pre += ' ';
		} else if (!EndsDeclarator(result.pre)) {
			// An array, which needs parens of its own: "short (*)[64]", not "short * [64]".
			result.pre += " (";
			result.post = ")" + result.post;
		}
		// Otherwise the inner type is a function or a pointer to one, and has already opened
		// the parens for us: "int (*)(char)", not "int ()(char) *".
		result.pre += sigil;
		break;
	}
	case 'C':
	case 'V':
	{
		pos_++;
		const char *qual = c == 'C' ? "const" : "volatile";
		result = ParseDecl();
		// "char * const", but "const char *" - the qualifier binds to whatever came before.
		if (!result.pre.empty() && (result.pre.back() == '*' || result.pre.back() == '&'))
			result.pre += std::string(" ") + qual;
		else
			result.pre = std::string(qual) + " " + result.pre;
		break;
	}
	case 'U':
	case 'S':
		pos_++;
		result = ParseDecl();
		result.pre = std::string(c == 'U' ? "unsigned " : "signed ") + result.pre;
		break;
	case 'A':
	{
		pos_++;
		const int count = ParseNumber();
		if (count < 0 || Peek() != '_') {
			Fail();
			break;
		}
		pos_++;
		result = ParseDecl();
		result.post = " [" + std::to_string(count) + "]" + result.post;
		break;
	}
	case 'F':
	{
		// A function type: parameters, then "_" and the return type. The parens around the
		// declarator are left open for a P or R to put its sigil in - see above.
		pos_++;
		std::string args;
		if (!ParseParams(&args))
			break;
		std::string ret = "void";
		if (Peek() == '_') {
			pos_++;
			ret = ParseType();
		}
		result.pre = ret + " (";
		result.post = ")(" + args + ")";
		break;
	}
	case 'T':
	{
		// "T<n>": the same type as parameter n, 1-based.
		pos_++;
		const int index = IsDigit(Peek()) ? (s_[pos_++] - '0') : -1;
		if (index < 1 || (size_t)index > params_.size())
			Fail();
		else
			result.pre = params_[index - 1];
		break;
	}
	case 'e':
		pos_++;
		result.pre = "...";
		break;
	default:
		if (c == 'Q' || IsDigit(c)) {
			result.pre = ParseQualifiedName();
		} else if (const char *basic = CWBasicType(c)) {
			pos_++;
			result.pre = basic;
		} else {
			Fail();
		}
		break;
	}
	depth_--;
	return failed_ ? CWDecl() : result;
}

bool CodeWarriorParser::ParseParams(std::string *out) {
	std::vector<std::string> parts;
	bool unknown = false;
	while (pos_ < s_.size() && Peek() != '_') {
		if (Peek() == 'N') {
			// "N<count><index>": <count> parameters, each the same type as parameter <index>.
			const size_t save = pos_;
			pos_++;
			const int count = IsDigit(Peek()) ? (s_[pos_++] - '0') : -1;
			const int index = IsDigit(Peek()) ? (s_[pos_++] - '0') : -1;
			if (count >= 1 && index >= 1 && (size_t)index <= params_.size()) {
				for (int i = 0; i < count; i++) {
					parts.push_back(params_[index - 1]);
					params_.push_back(params_[index - 1]);
				}
				continue;
			}
			pos_ = save;
		}
		const std::string type = ParseType();
		if (failed_) {
			// Something we don't know. In lenient mode, say so instead of throwing the
			// whole (perfectly readable) name away.
			if (strict_)
				return false;
			failed_ = false;
			unknown = true;
			pos_ = s_.size();
			break;
		}
		// A lone "void" is how a parameterless function is spelled.
		if (type == "void" && parts.empty() && (pos_ >= s_.size() || Peek() == '_'))
			break;
		parts.push_back(type);
		params_.push_back(type);
	}
	if (unknown)
		parts.push_back("...");
	JoinInto(*out, parts, ", ");
	return true;
}

std::string CodeWarriorParser::ParseWholeType() {
	const std::string type = ParseType();
	return (failed_ || pos_ != s_.size()) ? "" : type;
}

bool CodeWarriorParser::ParseAfterSeparator(DemangledSymbol *out) {
	if (Peek() == 'Q' || IsDigit(Peek())) {
		qualifier_ = ParseQualifiedName();
		if (failed_)
			return false;
	}
	std::vector<std::string> quals;
	while (Peek() == 'C' || Peek() == 'V')
		quals.push_back(s_[pos_++] == 'C' ? "const" : "volatile");
	JoinInto(out->qualifiers, quals, " ");

	if (pos_ >= s_.size()) {
		// No function type: a static data member, which only makes sense qualified.
		if (qualifier_.empty() || !out->qualifiers.empty())
			return false;
		out->isFunction = false;
		return true;
	}
	if (Peek() != 'F')
		return false;
	pos_++;
	out->isFunction = true;
	if (!ParseParams(&out->parameters))
		return false;
	if (Peek() == '_') {
		// Templates encode their return type, same as in the Itanium mangling.
		pos_++;
		out->returnType = ParseType();
		if (failed_)
			return false;
	}
	return pos_ == s_.size();
}

// Turns the part before the "__" into a printable name, given the class it belongs to.
static std::string CodeWarriorName(std::string_view name, const std::string &qualifier) {
	std::string base;
	if (name == "__ct") {
		base = BaseName(qualifier);
	} else if (name == "__dt") {
		base = "~" + BaseName(qualifier);
	} else if (name.size() > 4 && name.compare(0, 4, "__op") == 0) {
		// A conversion operator carries its target type in the name itself.
		CodeWarriorParser typeParser(name.substr(4), true);
		const std::string type = typeParser.ParseWholeType();
		if (!type.empty())
			base = "operator " + type;
	} else if (name.size() > 2 && name.compare(0, 2, "__") == 0) {
		const std::string_view code = name.substr(2);
		for (const CWOperator &op : cwOperators) {
			if (code == op.code) {
				// "operator new", but "operator+".
				base = std::string("operator") + (op.name[0] >= 'a' ? " " : "") + op.name;
				break;
			}
		}
	}
	if (base.empty())
		base = std::string(name);
	// A function template spells its arguments out in the name: "sort<Pf>__3stdFPfPf_v".
	const size_t lt = base.find('<');
	if (lt != std::string::npos && base.back() == '>') {
		CodeWarriorParser argParser("", true);
		const std::string_view args = std::string_view(base).substr(lt + 1, base.size() - lt - 2);
		base = base.substr(0, lt) + argParser.ParseTemplateArgs(args);
	}
	return qualifier.empty() ? base : qualifier + "::" + base;
}

static bool TryCodeWarriorSplit(std::string_view mangled, size_t sep, bool strict, DemangledSymbol *out) {
	CodeWarriorParser parser(mangled.substr(sep + 2), strict);
	DemangledSymbol sym;
	if (!parser.ParseAfterSeparator(&sym))
		return false;
	// A plain C name with a "__" in it ("I3dClut__FlushCache") can look like an unqualified
	// function whose parameters happen not to decode, so in the lenient pass - where the
	// parameters are allowed not to decode - insist on a class qualifier as evidence that
	// this really is a mangled name.
	if (!strict && parser.qualifier().empty())
		return false;
	sym.name = CodeWarriorName(mangled.substr(0, sep), parser.qualifier());
	*out = sym;
	return true;
}

}  // namespace

// A compiler-generated symbol that carries a mangled name inside it, rather than being one:
//
//   __vt__3Son                              vtable for Son
//   __RTTI__Q23std9exception                typeinfo for std::exception
//   __sinit_hl_app.cpp                      static initializers for hl_app.cpp
//   @12@__dt__3SonFv                        non-virtual thunk (12) to Son::~Son()
//   @STRING@Init__Q25shTbb3TbbFPvPci@0      string literal 0 in shTbb::Tbb::Init(...)
//   @LOCAL@sort<Pf>__3stdFPfPf_v@shuffle@0  std::sort<float *>(...)::shuffle
//
// The @-forms are how CodeWarrior names things that have no C++ name of their own; the
// trailing @<n> distinguishes several of them within the same function. Returns false if
// this isn't one of them, leaving the caller to demangle the name as an ordinary symbol.
static bool DemangleCodeWarriorSpecial(std::string_view mangled, DemangledSymbol *out) {
	// Splits off a trailing "@<digits>" discriminator, which is only there when a function
	// has more than one of whatever this is. Then demangles what's left of the front half.
	auto splitIndex = [](std::string_view s, DemangledSymbol *inner, std::string *index) {
		const size_t at = s.rfind('@');
		if (at != std::string_view::npos && at + 1 < s.size() &&
				s.find_first_not_of("0123456789", at + 1) == std::string_view::npos) {
			*index = std::string(s.substr(at + 1)) + " ";
			s = s.substr(0, at);
		}
		return DemangleCodeWarrior(s, inner);
	};

	static const struct { const char *prefix; const char *text; } kinds[] = {
		{"__vt__", "vtable for "},
		{"__RTTI__", "typeinfo for "},
		{"__sinit_", "static initializers for "},
		{"__sterm_", "static destructors for "},
	};
	for (const auto &kind : kinds) {
		const size_t len = strlen(kind.prefix);
		if (mangled.size() <= len || mangled.compare(0, len, kind.prefix) != 0)
			continue;
		const std::string_view rest = mangled.substr(len);
		out->name = kind.text;
		if (kind.prefix[2] == 's') {
			// The static init/term functions are named after a source file, not a class.
			out->name += std::string(rest);
		} else {
			CodeWarriorParser parser(rest, true);
			DemangledSymbol type;
			if (!parser.ParseAfterSeparator(&type) || type.isFunction || parser.qualifier().empty())
				return false;
			out->name += parser.qualifier();
		}
		out->isFunction = false;
		return true;
	}

	if (mangled.compare(0, 8, "@STRING@") == 0) {
		// A string literal inside a function - typically the __FILE__ an assert expanded to.
		DemangledSymbol inner;
		std::string index;
		if (!splitIndex(mangled.substr(8), &inner, &index))
			return false;
		out->name = "string literal " + index + "in " + inner.ToString();
		out->isFunction = false;
		return true;
	}
	if (mangled.compare(0, 7, "@GUARD@") == 0) {
		// The "has this local static been constructed yet" flag. The name is the variable's,
		// undecorated apart from the "$<n>" that CodeWarrior gives every function-local one.
		out->name = "guard variable for " + std::string(mangled.substr(7));
		out->isFunction = false;
		return true;
	}
	if (mangled.compare(0, 7, "@LOCAL@") == 0) {
		// A function-local static: "@LOCAL@<function>@<variable>", plus the usual index.
		std::string_view body = mangled.substr(7);
		size_t at = body.rfind('@');
		if (at != std::string_view::npos && at + 1 < body.size() &&
				body.find_first_not_of("0123456789", at + 1) == std::string_view::npos)
			body = body.substr(0, at);
		at = body.rfind('@');
		if (at == std::string_view::npos)
			return false;
		DemangledSymbol inner;
		if (!DemangleCodeWarrior(body.substr(0, at), &inner))
			return false;
		out->name = inner.ToString() + "::" + std::string(body.substr(at + 1));
		out->isFunction = false;
		return true;
	}
	if (mangled[0] == '@' && IsDigit(mangled[1])) {
		// "@<this-adjustment>@<function>": a thunk for a base other than the first.
		const size_t at = mangled.find('@', 1);
		if (at == std::string_view::npos)
			return false;
		const std::string_view offset = mangled.substr(1, at - 1);
		if (offset.find_first_not_of("0123456789") != std::string_view::npos)
			return false;
		DemangledSymbol inner;
		if (!DemangleCodeWarrior(mangled.substr(at + 1), &inner))
			return false;
		out->name = "non-virtual thunk (" + std::string(offset) + ") to " + inner.ToString();
		out->isFunction = false;
		return true;
	}
	return false;
}

bool DemangleCodeWarrior(std::string_view mangled, DemangledSymbol *out) {
	if (mangled.size() > 2 && (mangled[0] == '@' || mangled[0] == '_') &&
			DemangleCodeWarriorSpecial(mangled, out))
		return true;

	// Names can themselves start with underscores ("__SetupFrameInfo__F..."), and can
	// contain "__" further in, so every candidate separator gets tried. Strict first, so a
	// split that decodes completely wins over one that only decodes its name.
	size_t start = 0;
	while (start < mangled.size() && mangled[start] == '_')
		start++;
	if (start == mangled.size())
		return false;
	for (int pass = 0; pass < 2; pass++) {
		for (size_t i = start; i + 2 < mangled.size(); i++) {
			if (mangled[i] == '_' && mangled[i + 1] == '_' &&
					TryCodeWarriorSplit(mangled, i, pass == 0, out))
				return true;
		}
	}
	return false;
}

// SN Systems (SNC / ProDG), a much more compact scheme, and unrelated to cfront:
//
//   __0fLCHeapMemoryFAlloci  ->  CHeapMemory::Alloc(int)
//
// "__0", a kind character, then the name as a chain of components whose lengths are written
// as *letters* (A = 0, so F = 5 and a = 26), then the parameters, then any qualifier. What
// the name consists of follows from the kind: a member function is class + function, a free
// function is just the function, and a "5" before a component makes it a namespace.
//
// See docs/SNSystemsMangling.md for the format; the parts that are guesses are flagged there
// and in the comments below.

namespace {

// Operator codes, matched against the two characters after the class name ("nw" and "dl" take
// an optional "a" for the array forms).
static const CWOperator snOperators[] = {
	{"nw", "new"}, {"dl", "delete"},
	{"apl", "+="}, {"ami", "-="}, {"amu", "*="}, {"adv", "/="}, {"amd", "%="},
	{"aad", "&="}, {"aor", "|="}, {"aer", "^="}, {"als", "<<="}, {"ars", ">>="},
	{"pl", "+"}, {"mi", "-"}, {"ml", "*"}, {"dv", "/"}, {"md", "%"},
	{"eq", "=="}, {"ne", "!="}, {"lt", "<"}, {"gt", ">"}, {"le", "<="}, {"ge", ">="},
	{"aa", "&&"}, {"oo", "||"}, {"nt", "!"}, {"co", "~"}, {"er", "^"},
	{"ad", "&"}, {"or", "|"}, {"ls", "<<"}, {"rs", ">>"}, {"as", "="},
	{"pp", "++"}, {"mm", "--"}, {"cl", "()"}, {"vc", "[]"}, {"rf", "->"},
	{"rm", "->*"}, {"cm", ","},
};

static const char *SNBasicType(char c) {
	switch (c) {
	case 'v': return "void";
	case 'c': return "char";
	case 's': return "short";
	case 'i': return "int";
	case 'l': return "long";
	case 'x': return "long long";
	case 'f': return "float";
	case 'd': return "double";
	case 'r': return "long double";
	case 'b': return "bool";
	case 'w': return "wchar_t";
	case 'e': return "...";
	default: return nullptr;
	}
}

// Lengths, and the small numbers in template arguments, are single letters: A-Z is 0-25 and
// a-z is 26-51. Returns -1 for anything else.
static int SNLetterValue(char c) {
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	return -1;
}

class SNSystemsParser {
public:
	explicit SNSystemsParser(std::string_view s) : s_(s) {}

	bool Parse(DemangledSymbol *out);

	// Standalone entry point, for the bare type name in a "__TID_"/"__T_" symbol. Those spell
	// an enclosing namespace as just another component rather than marking it with a "5", so
	// components are read until the symbol runs out.
	std::string ParseWholeType() {
		const std::string type = ParseType();
		return (failed_ || pos_ != s_.size()) ? "" : type;
	}

	std::string ParseWholeName() {
		std::vector<std::string> parts;
		while (pos_ < s_.size()) {
			parts.push_back(ParseName(nullptr));
			if (failed_)
				return "";
		}
		std::string out;
		JoinInto(out, parts, "::");
		return out;
	}

private:
	char Peek(size_t ahead = 0) const { return pos_ + ahead < s_.size() ? s_[pos_ + ahead] : 0; }
	bool ConsumeIf(const char *str) {
		const size_t len = strlen(str);
		if (s_.compare(pos_, len, str) != 0)
			return false;
		pos_ += len;
		return true;
	}
	void Fail() { failed_ = true; }

	std::string ParseIdentifier();
	std::string ParseNumber();
	std::string ParseName(std::vector<std::string> *templateArgs);
	std::string ParseTemplateArgs(std::vector<std::string> *args);
	CWDecl ParseDecl();
	std::string ParseType() { return ParseDecl().str(); }
	std::string ParseParams();

	std::string_view s_;
	size_t pos_ = 0;
	bool failed_ = false;
	int depth_ = 0;
	// The template arguments of the name this symbol belongs to, for resolving "9" back-
	// references in the parameters and return type.
	std::vector<std::string> templateArgs_;
	// Parameter types so far, for resolving the "T" and "N" back-references.
	std::vector<std::string> params_;
};

std::string SNSystemsParser::ParseIdentifier() {
	const int len = SNLetterValue(Peek());
	if (len < 0 || pos_ + 1 + (size_t)len > s_.size()) {
		Fail();
		return "";
	}
	pos_++;
	std::string name(s_.substr(pos_, len));
	pos_ += len;
	return name;
}

// A non-negative integer, in one of two forms: a run of '0's each worth 52 followed by a
// letter for the remainder (so "0M" is 64), or "8" plus a length letter and that many decimal
// digits, which is how values too big for the first form are written ("8E1024" is 1024).
std::string SNSystemsParser::ParseNumber() {
	if (Peek() == '8') {
		pos_++;
		const int len = SNLetterValue(Peek());
		if (len <= 0 || pos_ + 1 + (size_t)len > s_.size()) {
			Fail();
			return "";
		}
		pos_++;
		std::string digits(s_.substr(pos_, len));
		pos_ += len;
		if (digits.find_first_not_of("0123456789") != std::string::npos) {
			Fail();
			return "";
		}
		return digits;
	}
	int value = 0;
	while (Peek() == '0') {
		pos_++;
		value += 52;
		if (value > 1000000) {
			Fail();
			return "";
		}
	}
	const int digit = SNLetterValue(Peek());
	if (digit < 0) {
		Fail();
		return "";
	}
	pos_++;
	return std::to_string(value + digit);
}

// "7" then a list of arguments, terminated by "_". An argument is a type, or "4" and an
// integer for a non-type parameter.
std::string SNSystemsParser::ParseTemplateArgs(std::vector<std::string> *args) {
	pos_++;
	while (Peek() != '_') {
		if (!Peek() || failed_) {
			Fail();
			return "";
		}
		if (Peek() == '4') {
			pos_++;
			args->push_back(ParseNumber());
		} else {
			args->push_back(ParseType());
		}
		if (failed_)
			return "";
	}
	pos_++;
	std::string out;
	JoinInto(out, *args, ", ");
	return "<" + out + ">";
}

// Any number of "5"-introduced enclosing scopes, then the name itself, then its template
// arguments if it has any.
std::string SNSystemsParser::ParseName(std::vector<std::string> *templateArgs) {
	std::vector<std::string> parts;
	while (Peek() == '5') {
		pos_++;
		parts.push_back(ParseIdentifier());
		if (failed_)
			return "";
	}
	parts.push_back(ParseIdentifier());
	if (failed_)
		return "";
	if (Peek() == '7') {
		std::vector<std::string> args;
		const std::string printed = ParseTemplateArgs(&args);
		if (failed_)
			return "";
		parts.back() += printed;
		if (templateArgs && !args.empty())
			*templateArgs = args;
	}
	std::string out;
	JoinInto(out, parts, "::");
	return out;
}

CWDecl SNSystemsParser::ParseDecl() {
	if (failed_ || depth_ > 64) {
		Fail();
		return CWDecl();
	}
	depth_++;
	CWDecl result;
	const char c = Peek();
	switch (c) {
	case 'P':
	case 'R':
	{
		pos_++;
		result = ParseDecl();
		const char sigil = c == 'P' ? '*' : '&';
		if (result.post.empty()) {
			result.pre += ' ';
		} else if (!EndsDeclarator(result.pre)) {
			// An array, which needs parens of its own: "short (*)[64]", not "short * [64]".
			result.pre += " (";
			result.post = ")" + result.post;
		}
		// Otherwise the inner type is a function or a pointer to one, and has already opened
		// the parens for us: "int (*)(char)", not "int ()(char) *".
		result.pre += sigil;
		break;
	}
	case 'C':
	case 'V':
		pos_++;
		result = ParseDecl();
		result.pre = std::string(c == 'C' ? "const " : "volatile ") + result.pre;
		break;
	case 'U':
	case 'S':
		pos_++;
		result = ParseDecl();
		result.pre = std::string(c == 'U' ? "unsigned " : "signed ") + result.pre;
		break;
	case '6':
		pos_++;
		result.pre = ParseName(nullptr);
		break;
	case 'A':
	{
		pos_++;
		const std::string count = ParseNumber();
		if (failed_)
			break;
		result = ParseDecl();
		result.post = " [" + count + "]" + result.post;
		break;
	}
	case 'F':
	{
		// A function type: parameters, then "_" and the return type. The parens around the
		// declarator are left open for a P, R or M to fill in - see above.
		pos_++;
		std::vector<std::string> parts;
		while (Peek() && Peek() != '_') {
			const std::string type = ParseType();
			if (failed_)
				break;
			if (type != "void")
				parts.push_back(type);
		}
		if (failed_ || Peek() != '_') {
			Fail();
			break;
		}
		pos_++;
		const std::string ret = ParseType();
		if (failed_)
			break;
		std::string args;
		JoinInto(args, parts, ", ");
		result.pre = ret + " (";
		result.post = ")(" + args + ")";
		break;
	}
	case 'M':
	{
		// Pointer to member: the class, then the member's type. The enclosing P supplies the
		// "*", so this only has to name the class inside the parens.
		pos_++;
		const std::string cls = ParseName(nullptr);
		if (failed_)
			break;
		result = ParseDecl();
		if (result.post.empty())
			result.pre += " " + cls + "::";
		else
			result.pre += cls + "::";
		break;
	}
	case '9':
	{
		// A reference to one of the enclosing name's template arguments: the index (1-based),
		// then what appears to be a nesting level, always "A" in practice.
		pos_++;
		const int index = SNLetterValue(Peek());
		pos_++;
		if (SNLetterValue(Peek()) < 0) {
			Fail();
			break;
		}
		pos_++;
		if (index < 1 || (size_t)index > templateArgs_.size())
			Fail();
		else
			result.pre = templateArgs_[index - 1];
		break;
	}
	default:
		if (const char *basic = SNBasicType(c)) {
			pos_++;
			result.pre = basic;
		} else {
			Fail();
		}
		break;
	}
	depth_--;
	return failed_ ? CWDecl() : result;
}

// Parameters run until the return type ("_"), a qualifier, or the end of the symbol.
std::string SNSystemsParser::ParseParams() {
	while (pos_ < s_.size() && Peek() != 'K' && Peek() != '_') {
		if (Peek() == 'T') {
			// "T<index>": the same type as parameter <index>. A "T" with nothing usable after
			// it is the static marker instead, which ends the list.
			const int index = SNLetterValue(Peek(1));
			if (index < 1 || (size_t)index > params_.size())
				break;
			pos_ += 2;
			params_.push_back(params_[index - 1]);
			continue;
		}
		if (Peek() == 'N') {
			// "N<count><index>": <count> more parameters, each the type of parameter <index>.
			const int count = SNLetterValue(Peek(1));
			const int index = SNLetterValue(Peek(2));
			if (count >= 1 && index >= 1 && (size_t)index <= params_.size()) {
				pos_ += 3;
				for (int i = 0; i < count; i++)
					params_.push_back(params_[index - 1]);
				continue;
			}
		}
		const std::string type = ParseType();
		if (failed_)
			return "";
		params_.push_back(type);
	}
	// A lone "void" is how a parameterless function is spelled.
	if (params_.size() == 1 && params_[0] == "void")
		params_.clear();
	std::string out;
	JoinInto(out, params_, ", ");
	return out;
}

bool SNSystemsParser::Parse(DemangledSymbol *out) {
	if (!ConsumeIf("__0"))
		return false;
	const char kind = Peek();
	pos_++;

	std::string name;
	bool isOperator = false;
	if (kind == 'O') {
		// A global operator: no class, just the code.
		isOperator = true;
	} else if (kind == 'f' || kind == 'F' || kind == 'o' || kind == 'd') {
		name = ParseName(&templateArgs_);
		if (failed_)
			return false;
		isOperator = kind == 'o';
	} else {
		return false;
	}

	if (isOperator) {
		// "ct" and "dt" are the constructor and destructor rather than operators; the class
		// name isn't repeated, so it has to be taken from the qualifier.
		const std::string_view code = s_.substr(pos_, 2);
		const std::string base = BaseName(name);
		std::string op;
		if (code == "ct" && !base.empty()) {
			op = base;
		} else if (code == "dt" && !base.empty()) {
			op = "~" + base;
		} else {
			for (const CWOperator &candidate : snOperators) {
				if (code != candidate.code)
					continue;
				op = std::string("operator") + (candidate.name[0] >= 'a' ? " " : "") + candidate.name;
				// "nwa" and "dla" are the array forms.
				if (s_.compare(pos_ + 2, 1, "a") == 0 && (code == "nw" || code == "dl")) {
					pos_++;
					op += "[]";
				}
				break;
			}
			if (op.empty())
				return false;
		}
		pos_ += 2;
		name = name.empty() ? op : name + "::" + op;
	} else if (kind == 'f' || kind == 'd') {
		// A member: the name so far was the class, and its own name follows.
		std::vector<std::string> memberArgs;
		const std::string member = ParseName(&memberArgs);
		if (failed_)
			return false;
		if (!memberArgs.empty())
			templateArgs_ = memberArgs;
		name += "::" + member;
	}

	DemangledSymbol sym;
	sym.name = name;
	if (kind == 'd') {
		// Data, so there's nothing after the name.
		sym.isFunction = false;
		if (pos_ != s_.size())
			return false;
		*out = sym;
		return true;
	}

	// An unidentified marker that shows up between the name and the parameters on a couple of
	// template members. It doesn't affect the name, so skip it rather than giving up.
	ConsumeIf("__S");

	sym.isFunction = true;
	sym.parameters = ParseParams();
	if (failed_)
		return false;
	if (Peek() == '_') {
		// Templates encode their return type, same as in the other manglings.
		pos_++;
		sym.returnType = ParseType();
		if (failed_)
			return false;
	}
	if (Peek() == 'K') {
		pos_++;
		sym.qualifiers = "const";
	} else if (Peek() == 'T') {
		// A static member function - it's what "operator new" and every thread entry point and
		// callback in the corpus this was worked out from carries, and it never appears on a
		// free function, which wouldn't need marking.
		pos_++;
		sym.returnType = sym.returnType.empty() ? "static" : "static " + sym.returnType;
	}
	if (pos_ != s_.size())
		return false;
	*out = sym;
	return true;
}

}  // namespace

bool DemangleSNSystems(std::string_view mangled, DemangledSymbol *out) {
	if (mangled.size() > 3 && mangled.compare(0, 3, "__0") == 0) {
		SNSystemsParser parser(mangled);
		return parser.Parse(out);
	}

	// Symbols the compiler generates for a type, using the same name encoding.
	static const struct { const char *prefix; const char *text; } kinds[] = {
		{"__TID_", "type id for "},
		{"__T_", "typeinfo for "},
		{"__sti__", "static initializers for "},
	};
	for (const auto &kind : kinds) {
		const size_t len = strlen(kind.prefix);
		if (mangled.size() <= len || mangled.compare(0, len, kind.prefix) != 0)
			continue;
		out->name = kind.text;
		out->isFunction = false;
		if (kind.prefix[2] == 's') {
			// The static initializer is named after a source file, not a type.
			out->name += std::string(mangled.substr(len));
			return true;
		}
		SNSystemsParser parser(mangled.substr(len));
		std::string name = parser.ParseWholeName();
		if (name.empty()) {
			// It can also be a plain type rather than a class - "__TID_v" is void's.
			SNSystemsParser typeParser(mangled.substr(len));
			name = typeParser.ParseWholeType();
		}
		if (name.empty())
			return false;
		out->name += name;
		return true;
	}
	return false;
}

std::string DemangledSymbol::ToString() const {
	std::string out;
	if (!returnType.empty())
		out = returnType + " ";
	out += name;
	if (isFunction)
		out += "(" + parameters + ")";
	if (!qualifiers.empty())
		out += " " + qualifiers;
	return out;
}

std::string DemangleSymbolName(std::string_view name) {
	std::string out;
	if (DemangleItanium(name, &out))
		return out;
	DemangledSymbol sym;
	if (DemangleCodeWarrior(name, &sym) || DemangleSNSystems(name, &sym))
		return sym.ToString();
	return std::string(name);
}
