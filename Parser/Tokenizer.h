#pragma once
#include "Util/FileClasses.h"

enum class TokenType
{
	Invalid,
	Identifier,
	Integer,
	String,
	Float,
	LParen,
	RParen,
	Plus,
	Minus,
	Mult,
	Div,
	Mod,
	Caret,
	Tilde,
	LeftShift,
	RightShift,
	Less,
	Greater,
	LessEqual,
	GreaterEqual,
	Equal,
	NotEqual,
	BitAnd,
	BitOr,
	LogAnd,
	LogOr,
	Exclamation,
	Question,
	Colon,
	LBrack,
	RBrack,
	Comma,
	Assign,
	Equ,
	EquValue,
	Hash,
	LBrace,
	RBrace,
	Dollar,
	NumberString,
	Degree,
	Separator
};

struct Token
{
	friend class Tokenizer;

	Token() : originalText(nullptr), stringValue(nullptr), checked(false)
	{
	}

	Token(Token &&src)
	{
		// Move strings.
		originalText = src.originalText;
		src.originalText = nullptr;
		stringValue = src.stringValue;
		src.stringValue = nullptr;

		// Just copy the rest.
		type = src.type;
		line = src.line;
		column = src.column;
		floatValue = src.floatValue;
		checked = src.checked;
	}

	Token(const Token &src) {
		// Copy strings.
		originalText = nullptr;
		if (src.originalText)
			setOriginalText(src.originalText);
		stringValue = nullptr;
		if (src.stringValue)
			setStringValue(src.stringValue);

		// And copy the rest.
		type = src.type;
		line = src.line;
		column = src.column;
		floatValue = src.floatValue;
		checked = src.checked;
	}

	~Token()
	{
		clearOriginalText();
		clearStringValue();
	}

	Token& operator=(const Token& src)
	{
		// Copy strings.
		originalText = nullptr;
		if (src.originalText)
			setOriginalText(src.originalText);
		stringValue = nullptr;
		if (src.stringValue)
			setStringValue(src.stringValue);

		// And copy the rest.
		type = src.type;
		line = src.line;
		column = src.column;
		floatValue = src.floatValue;
		checked = src.checked;

		return *this;
	}

	void setOriginalText(const std::wstring& t)
	{
		setOriginalText(t, 0, t.length());
	}

	void setOriginalText(const std::wstring& t, const size_t pos, const size_t len)
	{
		clearOriginalText();
		originalText = new wchar_t[len + 1];
		wmemcpy(originalText, t.data() + pos, len);
		originalText[len] = 0;
	}

	std::wstring getOriginalText() const
	{
		return originalText;
	}

	void setStringValue(const std::wstring& t)
	{
		setStringValue(t, 0, t.length());
	}

	void setStringValue(const std::wstring& t, const size_t pos, const size_t len)
	{
		clearStringValue();
		stringValue = new wchar_t[len + 1];
		wmemcpy(stringValue, t.data() + pos, len);
		stringValue[len] = 0;
	}

	void setStringAndOriginalValue(const std::wstring& t)
	{
		setStringAndOriginalValue(t, 0, t.length());
	}

	void setStringAndOriginalValue(const std::wstring& t, const size_t pos, const size_t len)
	{
		setStringValue(t, pos, len);
		clearOriginalText();
		originalText = stringValue;
	}

	std::wstring getStringValue() const
	{
		if (stringValue)
			return stringValue;
		return L"";
	}

	bool stringValueStartsWith(wchar_t c) const
	{
		if (stringValue)
			return stringValue[0] == c;
		return false;
	}

	TokenType type;
	size_t line;
	size_t column;

	union
	{
		int64_t intValue;
		double floatValue;
	};

protected:
	void clearOriginalText()
	{
		if (originalText != stringValue)
			delete [] originalText;
		originalText = nullptr;
	}

	void clearStringValue()
	{
		if (stringValue != originalText)
			delete [] stringValue;
		stringValue = nullptr;
	}

	wchar_t* originalText;
	wchar_t* stringValue;

	bool checked;
};

typedef std::list<Token> TokenList;

struct TokenizerPosition
{
	friend class Tokenizer;

	TokenizerPosition previous()
	{
		TokenizerPosition pos = *this;
		pos.it--;
		return pos;
	}
private:
	TokenList::iterator it;
};

class Tokenizer
{
public:
	Tokenizer();
	const Token& nextToken();
	const Token& peekToken(int ahead = 0);
	void eatToken() { eatTokens(1); }
	void eatTokens(int num);
	bool atEnd() { return position.it == tokens.end(); }
	TokenizerPosition getPosition() { return position; }
	void setPosition(TokenizerPosition pos) { position = pos; }
	void skipLookahead();
	std::vector<Token> getTokens(TokenizerPosition start, TokenizerPosition end) const;
	void registerReplacement(const std::wstring& identifier, std::vector<Token>& tokens);
	void registerReplacement(const std::wstring& identifier, const std::wstring& newValue);
	static size_t addEquValue(const std::vector<Token>& tokens);
	static void clearEquValues() { equValues.clear(); }
	void resetLookaheadCheckMarks();
protected:
	void clearTokens() { tokens.clear(); };
	void resetPosition() { position.it = tokens.begin(); } 
	void addToken(Token token);
private:
	bool processElement(TokenList::iterator& it);

	TokenList tokens;
	TokenizerPosition position;

	struct Replacement
	{
		std::wstring identifier;
		std::vector<Token> value;
	};

	Token invalidToken;
	std::vector<Replacement> replacements;
	static std::vector<std::vector<Token>> equValues;
};

class FileTokenizer: public Tokenizer
{
public:
	bool init(TextFile* input);
protected:
	Token loadToken();
	bool isInputAtEnd() { return linePos >= currentLine.size() && input->atEnd(); };

	void skipWhitespace();
	void createToken(TokenType type, size_t length);
	void createToken(TokenType type, size_t length, int64_t value);
	void createToken(TokenType type, size_t length, double value);
	void createToken(TokenType type, size_t length, const std::wstring& value);
	void createToken(TokenType type, size_t length, const std::wstring& value, size_t valuePos, size_t valueLength);
	void createTokenCurrentString(TokenType type, size_t length);

	bool convertInteger(size_t start, size_t end, int64_t& result);
	bool convertFloat(size_t start, size_t end, double& result);
	bool parseOperator();

	TextFile* input;
	std::wstring currentLine;
	size_t lineNumber;
	size_t linePos;
	
	Token token;
	bool equActive;
};

class TokenStreamTokenizer: public Tokenizer
{
public:
	void init(const std::vector<Token>& tokens)
	{
		clearTokens();

		for (const Token &tok: tokens)
			addToken(tok);
		
		resetPosition();
	}
};
