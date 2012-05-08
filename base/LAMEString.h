// DEPRECATED

// This is only here for legacy reasons.
// In new code, please use std::string.

#pragma once

#include <stdlib.h>
#include <string.h>

#include "base/basictypes.h"

#ifdef UNICODE
typedef wchar_t TCHAR;
#else
typedef char TCHAR;
#endif

class String
{
public:
	// constructors/destructor
	String();                         // construct empty string ""
	explicit String(const char * s);          // construct from string literal
	String(const String & str);    // copy constructor
	~String();                        // destructor


	// accessors

	int find(const String & str, int startpos=0) const; // index of first occurrence of str
	int find(TCHAR ch, int startpos=0) const;            // index of first occurrence of ch
	int findLast(TCHAR ch,int count=0);
	String subString(int pos, int len) const; // substring of len chars


	int length() const;                 // number of chars

	// starting at pos
	TCHAR * getPointer( ) const;                   // explicit conversion to char *
	const TCHAR *c_str() const {
		return getPointer();
	}

	// assignment
	const String & operator = ( const String & str ); // assign str
	const String & operator = ( const TCHAR * s );       // assign s
	const String & operator = ( TCHAR ch );              // assign ch

	//int operator == (String &other);
	//int operator != (String &other) {return !(*this == other);}
	// indexing
	TCHAR & operator [] ( int k );             // range-checked indexing

	// modifiers
	const String & operator += ( const String & str ); // append str
	const String & operator += ( const TCHAR * s);        // append s
	const String & operator += ( TCHAR ch );              // append char

	int parseIntoWords(String *words, int maxWords);
	int convertToInt() const;
	/*
	static String fromInt(int i) {
		TCHAR temp[15];
		_itot_s(i,temp,15,10);
		return String(temp);
	}*/
	void reverseString(); 
	void toUnicode(wchar_t *dest);
	void fromUnicode(wchar_t *src);
	String getPath();
	String getFName();

	void toUpper();
	void toLower();

private:
	int   Capacity;                   // capacity of string
	TCHAR * CString;                   // storage for characters
};

// The following free (non-member) functions operate on strings
//
// I/O functions
/*
ostream & operator << ( ostream & os, const String & str );
istream & operator >> ( istream & is, String & str );
istream & getline( istream & is, String & str );
*/
// comparison operators:

bool operator == ( const String & lhs, const String & rhs );
bool operator != ( const String & lhs, const String & rhs );
bool operator <  ( const String & lhs, const String & rhs );
bool operator <= ( const String & lhs, const String & rhs );
bool operator >  ( const String & lhs, const String & rhs );
bool operator >= ( const String & lhs, const String & rhs );

// concatenation operator +

String operator + ( const String & lhs, const String & rhs );
String operator + ( TCHAR ch, const String & str );
String operator + ( const String & str, TCHAR ch );

