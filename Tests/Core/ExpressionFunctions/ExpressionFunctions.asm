.gba
.create "output.bin",0

.macro test0,name
	.notice °(name) + "(): " \
		+ name()
.endmacro

.macro test1,name,a
	.notice °(name) + "(" + °(a) + "): " \
		+ name(a)
.endmacro

.macro test1h,name,a
	.notice °(name) + "(" + °(a) + "): " \
		+ toHex(name(a),16)
.endmacro

.macro test2,name,a,b
	.notice °(name) + "(" + °(a) + "," + °(b) + "): " \
		+ name(a,b)
.endmacro

.macro test2h,name,a,b
	.notice °name + "(" + °(a) + "," + °(b) + "): " \
		+ toHex(name(a,b),16)
.endmacro

.macro test3,name,a,b,c
	.notice °(name) + "(" + °(a) + "," + °(b) + "," + °(c) + "): " \
		+ name(a,b,c)
.endmacro

fileA equ "file.bin"
fileB equ "file.dat"

test0	endianness

test1	toString,191
test2	toHex,191,6

test1	fileExists,fileA
test1	fileExists,fileB
test1	fileSize,fileA

test2h	readU8,fileA,2
test2h	readU16,fileA,2
test1h	readU32,fileA
test2h	readS8,fileA,2
test2h	readS16,fileA,2
test1h	readS32,fileA

test1	int, 3.0
test1	int, 3.7
test1	int, 3

test1	float, 3.7
test1	float, 3

test1	frac, 3.0
test1	frac, 3.7
test1	frac, -3.7

test1	abs, 3
test1	abs, -3
test1	abs, 3.7
test1	abs, -3.7

str equ "teststest"
part equ "test"

test1	strlen,str
test3	substr,str,0,4
test3	find,str,part,0
test3	find,str,part,1
test2	rfind,str,part

test2	regex_match,str,"[a-z]+"
test2	regex_match,str,"[0-9]+"
test2	regex_search,str,"sts"
test2	regex_extract,"test123test","[0-9]+"

label:
test1	defined,label
test1	defined,label1

test3	readascii,fileA,4,4

.close
