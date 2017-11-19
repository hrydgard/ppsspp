.gba
.create "output.bin",0

.org 100h
label:

; let's test != first before relying on it
.if 2 != 2
	.error "!= Operator broken"
.elseif 2 != 3
.else
	.error "!= Operator broken"
.endif

; test to string operator

.macro checksn,exp,value
	.if °(exp) != (value)
		.error "Error: " + °(exp) + " != " + (value) 
	.endif
.endmacro

.macro checks,exp,value
	.if °(exp) != ("(" + value + ")")
		.error "Error: " + °(exp) + " != " + ("(" + value + ")") 
	.endif
.endmacro

checksn	1, "1"
checksn	1.5, "1.5"
checksn	label, "label"
checksn "text", "\"text\""
checksn	., "."
checks	1 + 2, "1 + 2"
checks	1 - 2, "1 - 2"
checks	1 * 2, "1 * 2"
checks	1 / 2, "1 / 2"
checks	1 % 2, "1 % 2"
checks	-1, "-1"
checks	!1, "!1"
checks	~1, "~1"
checks	1 << 2, "1 << 2"
checks	1 >> 2, "1 >> 2"
checks	1 < 2, "1 < 2"
checks	1 > 2, "1 > 2"
checks	1 <= 2, "1 <= 2"
checks	1 >= 2, "1 >= 2"
checks	1 == 2, "1 == 2"
checks	1 != 2, "1 != 2"
checks	1 & 2, "1 & 2"
checks	1 | 2, "1 | 2"
checks	1 && 2, "1 && 2"
checks	1 || 2, "1 || 2"
checks	1 ^ 2, "1 ^ 2"
checks	1 ? 2 : 3, "1 ? 2 : 3"
checks	°(1+2),"°(1 + 2)"

; now test actual expression evaluation

.macro check,exp,value
	.if (exp) != value
		.error "Error: " + °(exp) + " = " + (exp) + " != " + value 
	.endif
.endmacro

; addition
check	3 + 2, 5
check	3 + 2.5, 5.5
check	3.5 + 2, 5.5
check	3.0 + 2.0, 5
check	3 + 2+1, 6

; subtraction
check	3 - 2, 1
check	3 - 2.5, 0.5
check	3.5 - 2, 1.5
check	3.0 - 2.0, 1
check	3.0 - 2.0 - 1.5, -0.5

; multiplication
check	3 * 2, 6
check	3 * 2.5, 7.5
check	3.5 * 2, 7
check	3.0 * 2.0, 6
check	3 * 2 * 1.5, 9

; division
check	5 / 2,2
check	5 / 2.0, 2.5
check	5.0 / 2, 2.5
check	1 / 0, "undef"

; modulo
check	3 % 2, 1
check	1 % 0, "undef"

; combined addition and multiplication
check	3*2+2,8
check	3+2*2,7
check	2*3+3*2,12

; comparisons
check	1 < 2, 1
check	2 < 1, 0
check	-1 < 1, 1
check	1 < -1, 0
check	1 < 1, 0
check	2.0 < 2.1, 1
check	2.1 < 2.0, 0
check	2.0 < 2.0, 0

check	1 > 2, 0
check	2 > 1, 1
check	-1 > 1, 0
check	1 > -1, 1
check	1 > 1, 0
check	2.0 > 2.1, 0
check	2.1 > 2.0, 1
check	2.0 > 2.0, 0

check	1 >= 1, 1
check	2.0 >= 2.0, 1

check	1 <= 1, 1
check	2.0 <= 2.0, 1

check	2 == 1, 0
check	2 == 2, 1
check	2.0 == 1.0, 0
check	2.0 == 2.0, 1

check	2 != 1, 1
check	2 != 2, 0
check	2.0 != 1.0, 1
check	2.0 != 2.0, 0

; bitwise operations
check	~1,0xFFFFFFFFFFFFFFFE
check	3 | 5, 7
check	3 & 5, 1
check	3 ^ 5, 6
check	1 << 3, 8
check	256 >> 4, 16

; logical operations
check	!1, 0
check	!0, 1
check	1 && 1, 1
check	1 && 0, 0
check	1 || 1, 1
check	1 || 0, 1

; ?: operator
check 1 ? 2 : 3, 2
check 0 ? 2 : 3, 3

; identifier
check	label, 0x100
check	., 0x100

.close
