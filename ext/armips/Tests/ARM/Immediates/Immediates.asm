.nds
.create "output.bin",0


	ldr	r1,=12345678h	; test pool loads
	ldr	r2,=12345678h	; test repeated load of the same immediate

	ldr	r3,=8800h	; test ldr conversion to mov
	ldr	r4,=0FFFFFFFFh	; test ldr conversion to mvn

	mov	r5,0FFFFFFFFh	; test mov conversion to mvn

	and	r6,~0FFh	; test and conversion to bic
	bic	r6,0FFFFFFh	; test bic conversion to and

	cmp	r6,~1h		; test cmp conversion to cmn

				; test shifted immediates
.macro simm,reg,imm,rot
	mov	reg,(imm >> rot) | (imm <<  (32-rot))
.endmacro

	simm	r7,0FFh,0
	simm	r7,0FFh,2
	simm	r7,0FFh,4
	simm	r7,0FFh,6
	simm	r7,0FFh,8
	simm	r7,0FFh,10
	simm	r7,0FFh,12
	simm	r7,0FFh,14
	simm	r7,0FFh,16
	simm	r7,0FFh,18
	simm	r7,0FFh,20
	simm	r7,0FFh,22
	simm	r7,0FFh,24
	simm	r7,0FFh,26
	simm	r7,0FFh,28
	simm	r7,0FFh,30

.pool

.close