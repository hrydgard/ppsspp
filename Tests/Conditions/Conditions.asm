.psx
.create "output.bin", 0

label:

			; test simple if
			; expected: 10h
.if 1+1 == 2
	.byte 10h
.endif

			; test false condition
			; expected: nothing
.if 1 == 2
	.byte 18h
.endif

			; test ifdef
			; expected: 20h
.ifdef label
	.byte 20h
.endif

			; test else
			; expected: 31h
.ifdef label2
	.byte 30h
.else
	.byte 31h
.endif

			; test .ifndef and .ifarm
			; expected: 42h

.ifndef label
	.byte 40h
.else
	.if isArm()
		.byte 41h
	.else
		.byte 42h
	.endif
.endif

.nds
.thumb

			; test nested ifs and .ifthumb
			; expected: 50h
.if 5 < 6
	.ifdef label
		.if isThumb()
			.byte 50h
		.else
			.byte 51h
		.endif
	.else
		.byte 52h
	.endif
.else
	.byte 53h
.endif

.close