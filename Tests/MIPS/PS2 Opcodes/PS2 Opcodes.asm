.ps2
.create "output.bin",0
				; assembled from pcsx2's disr5900asm.cpp file

	; opcodes

	j	label
	nop
	jal	label
	nop
	beq	a1,a2,label
	nop
	bne	a1,a2,label
	nop
	blez	a1,label
	nop
	bgtz	a1,label
	nop
	addi	a1,a2,3
	addiu	a1,a2,3
	slti	a1,a2,3
	sltiu	a1,a2,3
	andi	a1,a2,3
	ori	a1,a2,3
	xori	a1,a2,3
	lui	a1,3
	beql	a1,a2,label
	nop
	bnel	a1,a2,label
	nop
	blezl	a1,label
	nop
	bgtzl	a1,label
	nop
	daddi	a1,a2,3
	daddiu	a1,a2,3
	ldl	a1,2(a3)
	ldr	a1,2(a3)
	lb	a1,2(a3)
	lh	a1,2(a3)
	lwl	a1,2(a3)
	lw	a1,2(a3)
	lbu	a1,2(a3)
	lhu	a1,2(a3)
	lwr	a1,2(a3)
	lwu	a1,2(a3)
	sb	a1,2(a3)
	sh	a1,2(a3)
	swl	a1,2(a3)
	sw	a1,2(a3)
	sdl	a1,2(a3)
	sdr	a1,2(a3)
	swr	a1,2(a3)
	ld	a1,2(a3)
	sd	a1,2(a3)
	lq	a1,2(a3)
	sq	a1,2(a3)

	swc1	f1,2(a3)
	sqc2	vf1,2(a3)
	lwc1	f1,2(a3)
	lqc2	vf1,2(a3)

label:

	; special

	sll	a1,a2,3
	sra	a1,a2,3
	sllv	a1,a2,a3
	srlv	a1,a2,a3
	srav	a1,a2,a3
	jr	a1
	nop
	jalr	a1,a2

	sync
	mfhi	a1
	mthi	a1
	mflo	a1
	mtlo	a1
	dsllv	a1,a2,a3
	dsrlv	a1,a2,a3
	dsrav	a1,a2,a3
	mult	a1,a2	
	multu	a1,a2
	div	a1,a2
	divu	a1,a2

	add	a1,a2,a3
	addu	a1,a2,a3
	sub	a1,a2,a3
	subu	a1,a2,a3
	and	a1,a2,a3
	or	a1,a2,a3
	xor	a1,a2,a3
	nor	a1,a2,a3
	slt	a1,a2,a3
	sltu	a1,a2,a3
	dadd	a1,a2,a3
	daddu	a1,a2,a3
	dsub	a1,a2,a3
	dsubu	a1,a2,a3

	tge	a1,a2
	tgeu	a1,a2
	tlt	a1,a2
	tltu	a1,a2
	teq	a1,a2
	tne	a1,a2

	dsll	a1,a2,3
	dsrl	a1,a2,3
	dsra	a1,a2,3
	dsll32	a1,a2,3
	dsrl32	a1,a2,3
	dsra32	a1,a2,3

	movz	a1,a2,a3
	movn	a1,a2,a3
	mfsa	a1
	mtsa	a1


	; regimm

	syscall	1
	break	1
	cache	5,2(a3)
	bltz	a1,label
	nop
	bgez	a1,label
	nop
	bltzl	a1,label
	nop
	bgezl	a1,label
	nop
	tgei	a1,2
	nop
	tgeiu	a1,2
	nop
	tlti	a1,2
	nop
	tltiu	a1,2
	nop
	teqi	a1,2
	nop
	tnei	a1,2
	nop
	bltzal	a1,label
	nop
	bgezal	a1,label
	nop
	bltzall	a1,label
	nop
	bgezall	a1,label
	nop
	mtsab	a1,2
	mtsah	a1,2



.close
