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

std::string DemangleSymbolName(std::string_view name) {
	std::string out;
	if (DemangleItanium(name, &out))
		return out;
	return std::string(name);
}
