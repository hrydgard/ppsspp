.gba
.create "output.bin",0

EQUTEST equ "test"

; check if only whole words are replaced
@@OTHEREQUTEST equ ".test"

.ascii 0x20,EQUTEST,0x21
.ascii @@OTHEREQUTEST

.close