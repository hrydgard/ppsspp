// NOTE: See warning in header.


#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "LAMEString.h"

#define assert(a) 

const int MAX_LENGTH = 1024;        // largest size string for input

#define _tcslen strlen
#define _tcscpy strcpy
#define _tcscmp strcmp
#define _tcsncmp strncmp


String::String()
{
	Capacity = 1;
	CString = new TCHAR[Capacity];
	CString[0] = '\0';           // make c-style string zero length
}

String::String(const TCHAR * s)
{
	Capacity = (int)_tcslen(s) + 1;
	CString = new TCHAR[Capacity];
	_tcscpy(CString,s);
}

String::String(const String & str)
{
	Capacity = str.length() + 1;
	CString = new TCHAR[Capacity];
	_tcscpy(CString,str.CString);
}

String::~String()
{
	delete [] CString;                // free memory
}

const String & String::operator = (const String & rhs)
{
	if (this != &rhs) {                      // check aliasing
		if (Capacity < rhs.length() + 1) {     // more memory needed?
			delete[] CString;                    // delete old string
			Capacity = rhs.length() + 1;         // add 1 for '\0'
			CString = new TCHAR[Capacity];
		}
		_tcscpy(CString,rhs.CString);
	}
	return *this;
}


const String & String::operator = (const TCHAR * s)
{
	int len = 0;                         // length of newly constructed string
	assert(s != 0);                      // make sure s non-NULL
	len = (int)_tcslen(s);                     // # of characters in string

	// free old string if necessary

	if (Capacity < len + 1)
	{
		delete[] CString;	// delete old string
		Capacity = len + 1;	// add 1 for '\0'
		CString = new TCHAR[Capacity];
	}
	_tcscpy(CString,s);
	return *this;
}

const String & String::operator = (TCHAR ch)
{
	if (Capacity < 2)
	{
		delete [] CString;
		Capacity = 2;
		CString = new TCHAR[Capacity];
	}
	CString[0] = ch;	// make string one character long
	CString[1] = '\0';
	return *this;
}

int String::length() const {
	int myLength = 0;
	while (CString[myLength] != '\0')
		myLength++;
	return myLength;
}

TCHAR * String::getPointer() const
{
	return CString;
}

TCHAR & String::operator [] (int k)
{
	if (k < 0 || (int)_tcslen(CString) <= k)
	{
		//cerr << "index out of range: " << k << " string: " << CString << endl;
		assert(0 <= k && k < _tcslen(CString));
	}
	return CString[k];
}


const String & String::operator += (const String & str)
{
	String copystring(str);         // copy to avoid aliasing problems

	int newLength = length() + str.length(); // self + added string
	int lastLocation = length();	     // index of '\0'

	// check to see if local buffer not big enough
	if (newLength >= Capacity)
	{
		Capacity = newLength + 1;
		TCHAR * newBuffer = new TCHAR[Capacity];
		_tcscpy(newBuffer,CString); // copy into new buffer
		delete [] CString;	     // delete old string
		CString = newBuffer;
	}

	// now catenate str (copystring) to end of CString
	_tcscpy(CString+lastLocation,copystring.getPointer() );

	return *this;
}

const String & String::operator += (const TCHAR * s)
{
	int newLength = length() + (int)_tcslen(s);    // self + added string
	int lastLocation = length();	     // index of '\0'

	// check to see if local buffer not big enough
	if (newLength >= Capacity)
	{
		Capacity = newLength + 1;
		TCHAR * newBuffer = new TCHAR[Capacity];
		_tcscpy(newBuffer,CString); // copy into new buffer
		delete [] CString;	     // delete old string
		CString = newBuffer;
	}

	// now catenate s to end of CString
	_tcscpy(CString+lastLocation,s);

	return *this;
}

const String & String::operator += ( TCHAR ch )
{
	String temp;	// make string equivalent of ch
	temp = ch;
	*this += temp;
	return *this;
}

String operator + (const String & lhs, const String & rhs)
{
	String result(lhs); // copies lhs to result
	result += rhs;	  // catenate rhs
	return result;	  // returns a copy of result
}

String operator + ( TCHAR ch, const String & str )
{
	String result;	  // make string equivalent of ch
	result = ch;
	result += str;
	return result;
}

String operator + ( const String & str, TCHAR ch )
{
	String result(str);
	result += ch;
	return result;
}


String String::subString(int pos, int len) const
{
	String result(*this);           // make sure enough space allocated

	if(pos < 0)                       // start at front when pos < 0
	{
		pos = 0;
	}

	if(pos >= (int)_tcslen(CString))
	{
		result = ""; // empty string
		return result;
	}

	int lastIndex = pos + len - 1;      // last char's index (to copy)
	if(lastIndex >= (int)_tcslen(CString)) {   // off end of string?
		lastIndex = (int)_tcslen(CString)-1;
	}

	int j,k;
	for(j=0,k=pos; k <= lastIndex; j++,k++) {
		result.CString[j] = CString[k];
	}
	result.CString[j] = '\0';         // properly terminate C-string
	return result;
}

int String::find(const String & str, int startpos)  const
{
	int len = str.length();
	int lastIndex = length() - len;
	int k;
	for (k=startpos; k <= lastIndex; k++) {
		if (_tcsncmp(CString + k,str.getPointer(),len) == 0) return k;
	}
	return -1;
}


int String::find(TCHAR ch, int startpos) const
{
	int k;
	for(k=startpos; k < (int)_tcslen(CString); k++) {
		if (CString[k] == ch) {
			return k;
		}
	}
	return -1;
}

int String::findLast(TCHAR ch, int count)
{
	for (int k=length()-1; k; k--)
	{
		if (CString[k] == ch)
		{
			if (count==0)
				return k;
			count--;
		}
	}
	return -1;
}

int String::parseIntoWords(String *words, int maxWords)
{
	int numWords=0;
	String currentWord;
	for (int i=0; i<length()+1; i++)
	{
		TCHAR c = (i<length()) ? CString[i] : ' ';
		if (c==' ')
		{
			if (currentWord.length()>0)
			{
				words[numWords] = currentWord;
				currentWord="";
				numWords++;
				if (numWords == maxWords)
					return numWords;
			}
		}
		else
		{
			currentWord += c;
		}
	}
	return numWords;
}

void String::toLower()
{
	for (int i=0; i<length(); i++)
		CString[i] = (TCHAR)tolower(CString[i]);
}
void String::toUpper()
{
	for (int i=0; i<length(); i++)
		CString[i] = (TCHAR)toupper(CString[i]);
}

bool operator == ( const String & lhs, const String & rhs )
{
	return _tcscmp(lhs.getPointer(), rhs.getPointer()) == 0;
}

bool operator != ( const String & lhs, const String & rhs )
{
	return ! (lhs == rhs);
}

bool operator < ( const String & lhs, const String & rhs )
{
	return _tcscmp(lhs.getPointer(), rhs.getPointer()) < 0;
}

bool operator <= ( const String & lhs, const String & rhs )
{
	return lhs < rhs || lhs == rhs;
}
bool operator >  ( const String & lhs, const String & rhs )
{
	return rhs < lhs;
}

bool operator >= ( const String & lhs, const String & rhs )
{
	return rhs <= lhs;
}
/*
int String::convertToInt() const
{
	return(_ttoi(CString));
}*/


void String::reverseString()
{
	int n = 0, i = 0;
	TCHAR *ny = new TCHAR[length()+1];

	for (i=length()-1; i >= 0; i--, n++) {
		ny[i] = CString[n];
	}

	ny[length()]='\0';

	_tcscpy(CString, ny);

	delete [] ny;
}




String String::getPath()
{
	int p=find(String("\\"),0);
	int lastp=-1;
	while (p!=-1)	{
		lastp=p;
		p=find(String("\\"),p+1);
	}
	if (lastp!=-1) {
		return subString(0,lastp);
	}	else {
		return String("");
	}
}

String String::getFName()
{
	int p=find(String("\\"),0);
	int lastp=-1;
	while (p!=-1)
	{
		lastp=p;
		p=find(String("\\"),p+1);
	}
	if (lastp!=-1)
	{
		return subString(lastp+1,100);
	}
	else
	{
		return String("");
	}
}


void String::toUnicode(wchar_t *dest)
{
	for (int i=0; i<length(); i++)
		dest[i]=(wchar_t)CString[i];

} 


int wideLength(wchar_t *s)
{
	int len=0;
	while (*s++) 
		len++;
	return len;
}


void String::fromUnicode(wchar_t *src)
{
	struct Local { 
		static int clamp(int i) {
			return i>255?' ':i;
		}
	};

	int newLength = wideLength(src);
	delete [] CString;
	Capacity = newLength + 1;
	CString = new TCHAR[Capacity];

	int i;
	for (i=0; i<newLength; i++)
	{
		CString[i] = Local::clamp(src[i]);
	}
	CString[i]=0;
}
