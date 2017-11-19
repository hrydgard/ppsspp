#pragma once

#include <map>
#include "Assembler.h"

struct SymbolKey
{
	std::wstring name;
	int file;
	int section;
};

bool operator<(SymbolKey const& lhs, SymbolKey const& rhs);

class Label
{
public:
	Label(std::wstring name): name(name), defined(false),info(0),data(false),updateInfo(true) { };
	const std::wstring getName() { return name; };
	void setOriginalName(const std::wstring& name) { originalName = name; }
	const std::wstring getOriginalName() { return originalName.empty() ? name : originalName; }
	int64_t getValue() { return value; };
	void setValue(int64_t val) { value = val; };
	bool isDefined() { return defined; };
	void setDefined(bool b) { defined = b; };
	bool isData() { return data; };
	void setIsData(bool b) { data = b; };
	void setInfo(int inf) { info = inf; };
	int getInfo() { return info; };
	void setUpdateInfo(bool b) { updateInfo = b; };
	bool getUpdateInfo() { return updateInfo; };
	void setSection(int num) { section = num; }
	int getSection() { return section; }
private:
	std::wstring name, originalName;
	int64_t value;
	bool defined;
	bool data;
	int info;
	bool updateInfo;
	int section;
};

class SymbolTable
{
public:
	SymbolTable();
	~SymbolTable();
	void clear();
	bool symbolExists(const std::wstring& symbol, int file, int section);
	static bool isValidSymbolName(const std::wstring& symbol);
	static bool isValidSymbolCharacter(wchar_t character, bool first = false);
	static bool isLocalSymbol(const std::wstring& symbol, size_t pos = 0) { return symbol.size() >= pos+2 && symbol[pos+0] == '@' && symbol[pos+1] == '@'; };
	static bool isStaticSymbol(const std::wstring& symbol, size_t pos = 0) { return symbol.size() >= pos+1 && symbol[pos+0] == '@'; };
	static bool isGlobalSymbol(const std::wstring& symbol, size_t pos = 0) { return !isLocalSymbol(symbol) && !isStaticSymbol(symbol); };

	Label* getLabel(const std::wstring& symbol, int file, int section);
	bool addEquation(const std::wstring& name, int file, int section, size_t referenceIndex);
	bool findEquation(const std::wstring& name, int file, int section, size_t& dest);
	void addLabels(const std::vector<LabelDefinition>& labels);
	int findSection(int64_t address);

	std::wstring getUniqueLabelName(bool local = false);
	size_t getLabelCount() { return labels.size(); };
	size_t getEquationCount() { return equationsCount; };
	bool isGeneratedLabel(const std::wstring& name) { return generatedLabels.find(name) != generatedLabels.end(); }
private:
	void setFileSectionValues(const std::wstring& symbol, int& file, int& section);

	enum SymbolType { LabelSymbol, EquationSymbol };
	struct SymbolInfo
	{
		SymbolType type;
		size_t index;
	};

	std::map<SymbolKey,SymbolInfo> symbols;
	std::vector<Label*> labels;
	size_t equationsCount;
	size_t uniqueCount;
	std::set<std::wstring> generatedLabels;
};
