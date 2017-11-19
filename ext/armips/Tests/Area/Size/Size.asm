.psx
.create "output.bin", 0

			; an area that works

.area 20h
	.word 1,2,3,4
.endarea


			; an area that doesn't

.area 4h
	.word 5,6
.endarea

			; nested areas
			; the outer area doesn't work

.area 8h
	.area 4h
		.word 7
	.endarea
	.word 8,9
.endarea

.close