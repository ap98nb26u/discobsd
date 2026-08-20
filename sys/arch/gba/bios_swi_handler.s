
/mnt/c/Users/Keiki/Downloads/gba_bios.bin:     file format binary


Disassembly of section .data:

00000000 <.data>:
reset_vector:
       0:	ea000018 	b	0x68
undef_vector:
       4:	ea000004 	b	0x1c
swi_vector:
       8:	ea00004c 	b	0x140
prefetch_abort_vector:
       c:	ea000002 	b	0x1c
data_abort_vector:
      10:	ea000001 	b	0x1c
preserved_vector:
      14:	ea000000 	b	0x1c
irq_vector:
      18:	ea000042 	b	0x128

fiq_vector:
undef_handler:
prefetch_abort_handler:
data_abort_handler:
preserved_handler:
fiq_handler:
universal_handler:
      1c:	e59fd1a0 	ldr	sp, [pc, #416]	@ 0x1c4
      20:	e92d5000 	push	{ip, lr}
      24:	e14fc000 	mrs	ip, SPSR
      28:	e10fe000 	mrs	lr, CPSR
      2c:	e92d5000 	push	{ip, lr}
      30:	e3a0c302 	mov	ip, #134217728	@ 0x8000000
      34:	e5dce09c 	ldrb	lr, [ip, #156]	@ 0x9c
      38:	e35e00a5 	cmp	lr, #165	@ 0xa5
      3c:	1a000004 	bne	0x54
      40:	05dce0b4 	ldrbeq	lr, [ip, #180]	@ 0xb4
      44:	021ee080 	andseq	lr, lr, #128	@ 0x80
      48:	e28fe004 	add	lr, pc, #4
      4c:	159ff220 	ldrne	pc, [pc, #544]	@ 0x274
      50:	059ff220 	ldreq	pc, [pc, #544]	@ 0x278
      54:	e59fd164 	ldr	sp, [pc, #356]	@ 0x1c0
      58:	e8bd5000 	pop	{ip, lr}
      5c:	e169f00c 	msr	SPSR_fc, ip
      60:	e8bd5000 	pop	{ip, lr}
      64:	e25ef004 	subs	pc, lr, #4	@ return

reset_handler:
		@ if POST == 1; then disable IRQ, FIQ
      68:	e35e0000 	cmp	lr, #0
      6c:	03a0e004 	moveq	lr, #4
      70:	e3a0c301 	mov	ip, #67108864	@ 0x4000000
      74:	e5dcc300 	ldrb	ip, [ip, #768]	@ 0x300 (POST register)
      78:	e33c0001 	teq	ip, #1
      7c:	010fc000 	mrseq	ip, CPSR
      80:	038cc0c0 	orreq	ip, ip, #192	@ 0xc0 (noIRQ, noFIQ)
      84:	0129f00c 	msreq	CPSR_fc, ip
      88:	0affffe3 	beq	0x1c		@ jump universal_handler

		@ clear IME register to disable IRQ
BIOS_HardReset
      8c:	e3a000df 	mov	r0, #223	@ 0xdf (SYS, ARM, noIRQ, noFIQ)
      90:	e129f000 	msr	CPSR_fc, r0
      94:	e3a04301 	mov	r4, #67108864	@ 0x4000000
      98:	e5c44208 	strb	r4, [r4, #520]	@ 0x208	@ store 0 to IME
      9c:	eb00000f 	bl	0xe0

		@ save future return address to stack (600 + 8(pipeline) bytes)
      a0:	e28f0f96 	add	r0, pc, #600	@ 0x258
      a4:	e58d00fc 	str	r0, [sp, #252]	@ 0xfc

		@ prepare for function call
      a8:	e59f01cc 	ldr	r0, [pc, #460]	@ 0x27c (0x00001929)
      ac:	e28fe000 	add	lr, pc, #0
      b0:	e12fff10 	bx	r0

		@ return from function (1929)
		@ also return from SWI#0
BIOS_SoftReset:
      b4:	e3a04301 	mov	r4, #67108864	@ 0x4000000
      b8:	e5542006 	ldrb	r2, [r4, #-6]	@ load 0x3FF7FFA to r2
      bc:	eb000007 	bl	0xe0

		@ if *0x3FF7FFA == 0; then restore registers jump to ROM/IWRAM
      c0:	e3520000 	cmp	r2, #0
      c4:	e9141fff 	ldmdb	r4, {r0-r9, sl, fp, ip}
      c8:	13a0e402 	movne	lr, #33554432	@ 0x2000000
      cc:	03a0e302 	moveq	lr, #134217728	@ 0x8000000
      d0:	e3a0001f 	mov	r0, #31		@ 0x1f (SYS, ARM, noIRQ, noFIQ)
      d4:	e129f000 	msr	CPSR_fc, r0
      d8:	e3a00000 	mov	r0, #0
      dc:	e12fff1e 	bx	lr

		@ initialize SVC mode stack pointer
      e0:	e3a000d3 	mov	r0, #211	@ 0xd3 (SVC, noIRQ, noFIQ)
      e4:	e129f000 	msr	CPSR_fc, r0
      e8:	e59fd0d0 	ldr	sp, [pc, #208]	@ 0x1c0 (0x03007fe0)
      ec:	e3a0e000 	mov	lr, #0
      f0:	e169f00e 	msr	SPSR_fc, lr	@ clear SPSR_fc for safety

		@ initialize IRQ mode stack pointer
      f4:	e3a000d2 	mov	r0, #210	@ 0xd2 (IRQ, noIRQ, noFIQ)
      f8:	e129f000 	msr	CPSR_fc, r0
      fc:	e59fd0b8 	ldr	sp, [pc, #184]	@ 0x1bc (0x03007fa0)
     100:	e3a0e000 	mov	lr, #0
     104:	e169f00e 	msr	SPSR_fc, lr	@ clear SPSR_fc for safety

		@ initialize SYS mode stack pointer
     108:	e3a0005f 	mov	r0, #95		@ 0x5f (SYS, noFIQ)
     10c:	e129f000 	msr	CPSR_fc, r0
     110:	e59fd0a0 	ldr	sp, [pc, #160]	@ 0x1b8 (0x03007f00)

		@ change ARM mode to Thumb mode
     114:	e28f0001 	add	r0, pc, #1	@ prepare to Thumb
     118:	e12fff10 	bx	r0		@ to Thumb

     11c:	49582000 	ldmdbmi	r8, {sp}^
     120:	1d095060 	stcne	0, cr5, [r9, #-384]	@ 0xfffffe80
     124:	4770dbfc 			@ <UNDEFINED> instruction: 0x4770dbfc

irq_handler:
     128:	e92d500f 	push	{r0-r3, ip, lr}	@ r0-r3, r12, r14
     12c:	e3a00301 	mov	r0, #67108864	@ 0x4000000
     130:	e28fe000 	add	lr, pc, #0
     134:	e510f004 	ldr	pc, [r0, #-4]	@ jump to user IRQ handler
     138:	e8bd500f 	pop	{r0-r3, ip, lr}	@ r0-r3, r12, r14
     13c:	e25ef004 	subs	pc, lr, #4	@ return from IRQ

swi_handler:
     140:	e92d5800 	push	{fp, ip, lr}	@ r11, r12, r14
     144:	e55ec002 	ldrb	ip, [lr, #-2]	@ get SWI# to ip (r12)
		@ PC=current instruction address + 8
		@ 0x148 + 8 + 120 = 0x1c8
     148:	e28fb078 	add	fp, pc, #120	@ 0x78 0x1c8 BIOS_jump_table
     14c:	e79bc10c 	ldr	ip, [fp, ip, lsl #2] @ SWI# to address

     150:	e14fb000 	mrs	fp, SPSR	@ r11
     154:	e92d0800 	stmfd	sp!, {fp}	@ push SPSR

     158:	e20bb080 	and	fp, fp, #128	@ 0x80 FIQ, ARM
     15c:	e38bb01f 	orr	fp, fp, #31	@ 0x1F SYS mode
     160:	e129f00b 	msr	CPSR_fc, fp

     164:	e92d4004 	push	{r2, lr}	@ save r2, r14
     168:	e28fe000 	add	lr, pc, #0	@ 0x170 (0x168+8) to LR
     16c:	e12fff1c 	bx	ip		@ call selected function

     170:	e8bd4004 	pop	{r2, lr}	@ restore r2, r14
     174:	e3a0c0d3 	mov	ip, #211	@ 0xd3 SVC
     178:	e129f00c 	msr	CPSR_fc, ip

		@ return from swi_handler
     17c:	e8bd0800 	ldmfd	sp!, {fp}	@ restore SPSR saved at 0x154
     180:	e169f00b 	msr	SPSR_fc, fp
		@ not reached

     184:	e8bd5800 	pop	{fp, ip, lr}	@ restore r11, r12, r14 saved at 0x140
     188:	e1b0f00e 	movs	pc, lr		@ s special: lr to pc, and spsr to cpsr
     18c:	e3a0c301 	mov	ip, #67108864	@ 0x4000000 DISPCNT
     190:	e3a02004 	mov	r2, #4
     194:	e5cc2001 	strb	r2, [ip, #1]	@ store 4 to DISPCNT+1
     198:	e3a02008 	mov	r2, #8
     19c:	e5cc2000 	strb	r2, [ip]	@ store 8 to DISPCNT

BIOS_RegisterRamReset:
     1a0:	e3a02000 	mov	r2, #0
     1a4:	ea000000 	b	0x1ac

BIOS_Halt:
     1a8:	e3a02080 	mov	r2, #128	@ 0x80

     1ac:	e3a0c301 	mov	ip, #67108864	@ 0x4000000
     1b0:	e5cc2301 	strb	r2, [ip, #769]	@ 0x301 @ HALTCNT
     1b4:	e12fff1e 	bx	lr

		@ default stack location
SP_usr:
SP_sys:
     1b8:	03007f00 	movweq	r7, #3840	@ 0xf00
SP_irq:
     1bc:	03007fa0 	movweq	r7, #4000	@ 0xfa0
SP_svc:
     1c0:	03007fe0 	movweq	r7, #4064	@ 0xfe0
SP_uni:
     1c4:	03007ff0 	movweq	r7, #4080	@ 0xff0

		@ SWI jump table
SWI_jump_table:
     1c8:	000000b4 	@ #00 SoftReset
     1cc:	000009c3 	@ #01 RegisterRamReset
     1d0:	000001a0 	@ #02 Halt
     1d4:	000001a8 	@ #03 Stop/Sleep
     1d8:	00000330 	@ #04 IntrWait
     1dc:	00000328 	@ #05 VBlankIntrWait
     1e0:	000003b4 	@ #06 Div
     1e4:	000003a8 	@ #07 DivArm
     1e8:	00000404 	@ #08 SqRt
     1ec:	00000474 	@ #09 ArcTan
     1f0:	000004fd 	@ #0A ArcTan2
     1f4:	00000b4d 	@ #0B CpuSet
     1f8:	00000bc4 	@ #0C CpuFastSet
     1fc:	00000378 	@ #0D GetBiosChecksum
     200:	00000c2c 	@ #0E BgAffineSet
     204:	00000ce0 	@ #0F ObjAffineSet
     208:	00000f60 	@ #10 BitUnPack
     20c:	000010fc 	@ #11 LZ77UnCompReadNormalWrite8bit
     210:	00001194 	@ #12 LZ77UnCompReadNormalWrite16bit
     214:	00001014 	@ #13 HuffUnCompReadNormal
     218:	00001279 	@ #14 RLUnCompReadNormalWrite8bit
     21c:	000012c1 	@ #15 RLUnCompReadNormalWrite16bit
     220:	00001333 	@ #16 Diff8bitUnFilterWrite8bit
     224:	0000135d 	@ #17 Diff8bitUnFilterWrite16bit
     228:	00001399 	@ #18 Diff16bitUnFilter
     22c:	00000801 	@ #19 SoundBias
     230:	00001665 	@ #1A SoundDriverInit
     234:	0000179d 	@ #1B SoundDriverMode
     238:	00001dc5 	@ #1C SoundDriverMain
     23c:	0000210d 	@ #1D SoundDriverVSync
     240:	00001825 	@ #1E SoundChannelClear
     244:	000018d9 	@ #1F MidiKey2Freq
     248:	000013c5 	@ #20 SoundWhatever0
     24c:	00001435 	@ #21 SoundWhatever1
     250:	000014c1 	@ #22 SoundWhatever2
     254:	000014fd 	@ #23 SoundWhatever3
     258:	00001515 	@ #24 SoundWhatever4
     25c:	000028cf 	@ #25 MultiBoot
     260:	0000008c 	@ #26 HardReset
     264:	000001ac 	@ #27 CustomHalt
     268:	00001879 	@ #28 SoundDriverVSyncOff
     26c:	000018c9 	@ #29 SoundDriverVSyncOn
     270:	00002693 	@ #2A SoundGetJumpList
		@ SWI jump table ends here, so SWI with the number greater than 0x2B
		@ will cause crash

		@ value
     274:	09fe2000 	@ 
     278:	09ffc000 	@ 

		@ pointer to function
     27c:	00001929 	@ 

     280:	fffffe00 			@ <UNDEFINED> instruction: 0xfffffe00
     284:	06242404 	strteq	r2, [r4], -r4, lsl #8
     288:	062d2505 	strteq	r2, [sp], -r5, lsl #10
     28c:	06362606 	ldrteq	r2, [r6], -r6, lsl #12
     290:	20c22100 	sbccs	r2, r2, r0, lsl #2
     294:	32801c22 	addcc	r1, r0, #8704	@ 0x2200
     298:	72507090 	subsvc	r7, r0, #144	@ 0x90
     29c:	1c8020ff 	stcne	0, cr2, [r0], {255}	@ 0xff
     2a0:	239022a0 	orrscs	r2, r0, #160, 4
     2a4:	27f09600 	ldrbcs	r9, [r0, r0, lsl #12]!
     2a8:	f0009701 			@ <UNDEFINED> instruction: 0xf0009701
     2ac:	2083fa78 	addcs	pc, r3, r8, ror sl	@ <UNPREDICTABLE>
     2b0:	81a001c0 	asrhi	r0, r0, #3
     2b4:	62a0480f 	adcvs	r4, r0, #983040	@ 0xf0000
     2b8:	02c01400 	sbceq	r1, r0, #0, 8
     2bc:	4b0e62e0 	blmi	0x398e44
     2c0:	882b602b 	stmdahi	fp!, {r0, r1, r3, r5, sp, lr}
     2c4:	0c624f0d 	stcleq	15, cr4, [r2], #-52	@ 0xffffffcc
     2c8:	80571912 	subshi	r1, r7, r2, lsl r9
     2cc:	fa20f003 	blx	0x83c2e0
     2d0:	70602004 	rsbvc	r2, r0, r4
     2d4:	1bdb7020 	blne	0xff6dc35c
     2d8:	dcf4802b 	ldclle	0, cr8, [r4], #172	@ 0xac
     2dc:	900243c8 	andls	r4, r2, r8, asr #7
     2e0:	a90234d4 	stmdbge	r2, {r2, r4, r6, r7, sl, ip, sp}
     2e4:	60666021 	rsbvs	r6, r6, r1, lsr #32
     2e8:	60a14901 	adcvs	r4, r1, r1, lsl #18
     2ec:	fa14f003 	blx	0x53c300
     2f0:	85006000 	strhi	r6, [r0, #-0]
     2f4:	ffffd800 			@ <UNDEFINED> instruction: 0xffffd800
     2f8:	7fff7bde 	svcvc	0x00ff7bde
     2fc:	00000c63 	andeq	r0, r0, r3, ror #24
     300:	e3a03301 	mov	r3, #67108864	@ 0x4000000
     304:	e5932200 	ldr	r2, [r3, #512]	@ 0x200
     308:	e0022822 	and	r2, r2, r2, lsr #16
     30c:	e2121080 	ands	r1, r2, #128	@ 0x80
     310:	159f07a0 	ldrne	r0, [pc, #1952]	@ 0xab8
     314:	02021001 	andeq	r1, r2, #1
     318:	059f079c 	ldreq	r0, [pc, #1948]	@ 0xabc
     31c:	014320b8 	strheq	r2, [r3, #-8]
     320:	e5c31202 	strb	r1, [r3, #514]	@ 0x202
     324:	e12fff10 	bx	r0
     328:	e3a00001 	mov	r0, #1
     32c:	e3a01001 	mov	r1, #1
     330:	e92d4010 	push	{r4, lr}
     334:	e3a03000 	mov	r3, #0
     338:	e3a04001 	mov	r4, #1
     33c:	e3500000 	cmp	r0, #0
     340:	1b000004 	blne	0x358
     344:	e5cc3301 	strb	r3, [ip, #769]	@ 0x301
     348:	eb000002 	bl	0x358
     34c:	0afffffc 	beq	0x344
     350:	e8bd4010 	pop	{r4, lr}
     354:	e12fff1e 	bx	lr
     358:	e3a0c301 	mov	ip, #67108864	@ 0x4000000
     35c:	e5cc3208 	strb	r3, [ip, #520]	@ 0x208
     360:	e15c20b8 	ldrh	r2, [ip, #-8]
     364:	e0110002 	ands	r0, r1, r2
     368:	10222000 	eorne	r2, r2, r0
     36c:	114c20b8 	strhne	r2, [ip, #-8]
     370:	e5cc4208 	strb	r4, [ip, #520]	@ 0x208
     374:	e12fff1e 	bx	lr
     378:	e3a00000 	mov	r0, #0
     37c:	e3a03000 	mov	r3, #0
     380:	e3a0c0df 	mov	ip, #223	@ 0xdf
     384:	e8b30004 	ldm	r3!, {r2}
     388:	e129f00c 	msr	CPSR_fc, ip
     38c:	e0800002 	add	r0, r0, r2
     390:	e1b01723 	lsrs	r1, r3, #14
     394:	0afffff9 	beq	0x380
     398:	e12fff1e 	bx	lr
     39c:	dc002800 	stcle	8, cr2, [r0], {-0}
     3a0:	47704240 	ldrbmi	r4, [r0, -r0, asr #4]!
     3a4:	4718a303 	ldrmi	sl, [r8, -r3, lsl #6]
     3a8:	e1a03000 	mov	r3, r0
     3ac:	e1a00001 	mov	r0, r1
     3b0:	e1a01003 	mov	r1, r3
     3b4:	e2113102 	ands	r3, r1, #-2147483648	@ 0x80000000
     3b8:	42611000 	rsbmi	r1, r1, #0
     3bc:	e033c040 	eors	ip, r3, r0, asr #32
     3c0:	22600000 	rsbcs	r0, r0, #0
     3c4:	e1b02001 	movs	r2, r1
     3c8:	e15200a0 	cmp	r2, r0, lsr #1
     3cc:	91a02082 	lslls	r2, r2, #1
     3d0:	3afffffc 	bcc	0x3c8
     3d4:	e1500002 	cmp	r0, r2
     3d8:	e0a33003 	adc	r3, r3, r3
     3dc:	20400002 	subcs	r0, r0, r2
     3e0:	e1320001 	teq	r2, r1
     3e4:	11a020a2 	lsrne	r2, r2, #1
     3e8:	1afffff9 	bne	0x3d4
     3ec:	e1a01000 	mov	r1, r0
     3f0:	e1a00003 	mov	r0, r3
     3f4:	e1b0c08c 	lsls	ip, ip, #1
     3f8:	22600000 	rsbcs	r0, r0, #0
     3fc:	42611000 	rsbmi	r1, r1, #0
     400:	e12fff1e 	bx	lr
     404:	e92d0010 	stmfd	sp!, {r4}
     408:	e1a0c000 	mov	ip, r0
     40c:	e3a01001 	mov	r1, #1
     410:	e1500001 	cmp	r0, r1
     414:	81a000a0 	lsrhi	r0, r0, #1
     418:	81a01081 	lslhi	r1, r1, #1
     41c:	8afffffb 	bhi	0x410
     420:	e1a0000c 	mov	r0, ip
     424:	e1a04001 	mov	r4, r1
     428:	e3a03000 	mov	r3, #0
     42c:	e1a02001 	mov	r2, r1
     430:	e15200a0 	cmp	r2, r0, lsr #1
     434:	91a02082 	lslls	r2, r2, #1
     438:	3afffffc 	bcc	0x430
     43c:	e1500002 	cmp	r0, r2
     440:	e0a33003 	adc	r3, r3, r3
     444:	20400002 	subcs	r0, r0, r2
     448:	e1320001 	teq	r2, r1
     44c:	11a020a2 	lsrne	r2, r2, #1
     450:	1afffff9 	bne	0x43c
     454:	e0811003 	add	r1, r1, r3
     458:	e1b010a1 	lsrs	r1, r1, #1
     45c:	e1510004 	cmp	r1, r4
     460:	3affffee 	bcc	0x420
     464:	e1a00004 	mov	r0, r4
     468:	e8bd0010 	ldmfd	sp!, {r4}
     46c:	e12fff1e 	bx	lr
     470:	4718a300 	ldrmi	sl, [r8, -r0, lsl #6]
     474:	e0010090 	mul	r1, r0, r0
     478:	e1a01741 	asr	r1, r1, #14
     47c:	e2611000 	rsb	r1, r1, #0
     480:	e3a030a9 	mov	r3, #169	@ 0xa9
     484:	e0030391 	mul	r3, r1, r3
     488:	e1a03743 	asr	r3, r3, #14
     48c:	e2833e39 	add	r3, r3, #912	@ 0x390
     490:	e0030391 	mul	r3, r1, r3
     494:	e1a03743 	asr	r3, r3, #14
     498:	e2833c09 	add	r3, r3, #2304	@ 0x900
     49c:	e283301c 	add	r3, r3, #28
     4a0:	e0030391 	mul	r3, r1, r3
     4a4:	e1a03743 	asr	r3, r3, #14
     4a8:	e2833c0f 	add	r3, r3, #3840	@ 0xf00
     4ac:	e28330b6 	add	r3, r3, #182	@ 0xb6
     4b0:	e0030391 	mul	r3, r1, r3
     4b4:	e1a03743 	asr	r3, r3, #14
     4b8:	e2833c16 	add	r3, r3, #5632	@ 0x1600
     4bc:	e28330aa 	add	r3, r3, #170	@ 0xaa
     4c0:	e0030391 	mul	r3, r1, r3
     4c4:	e1a03743 	asr	r3, r3, #14
     4c8:	e2833a02 	add	r3, r3, #8192	@ 0x2000
     4cc:	e2833081 	add	r3, r3, #129	@ 0x81
     4d0:	e0030391 	mul	r3, r1, r3
     4d4:	e1a03743 	asr	r3, r3, #14
     4d8:	e2833c36 	add	r3, r3, #13824	@ 0x3600
     4dc:	e2833051 	add	r3, r3, #81	@ 0x51
     4e0:	e0030391 	mul	r3, r1, r3
     4e4:	e1a03743 	asr	r3, r3, #14
     4e8:	e2833ca2 	add	r3, r3, #41472	@ 0xa200
     4ec:	e28330f9 	add	r3, r3, #249	@ 0xf9
     4f0:	e0000093 	mul	r0, r3, r0
     4f4:	e1a00840 	asr	r0, r0, #16
     4f8:	e12fff1e 	bx	lr
     4fc:	2900b5f0 	stmdbcs	r0, {r4, r5, r6, r7, r8, sl, ip, sp, pc}
     500:	2800d106 	stmdacs	r0, {r1, r2, r8, ip, lr, pc}
     504:	2000db01 	andcs	sp, r0, r1, lsl #22
     508:	2080e049 	addcs	lr, r0, r9, asr #32
     50c:	e0460200 	sub	r0, r6, r0, lsl #4
     510:	d1072800 	tstle	r7, r0, lsl #16
     514:	db022900 	blle	0x8a91c
     518:	02002040 	andeq	r2, r0, #64	@ 0x40
     51c:	20c0e03f 	sbccs	lr, r0, pc, lsr r0
     520:	e03c0200 	eors	r0, ip, r0, lsl #4
     524:	03921c02 	orrseq	r1, r2, #512	@ 0x200
     528:	039b1c0b 	orrseq	r1, fp, #2816	@ 0xb00
     52c:	424d4244 	submi	r4, sp, #68, 4	@ 0x40000004
     530:	02362640 	eorseq	r2, r6, #64, 12	@ 0x4000000
     534:	29000077 	stmdbcs	r0, {r0, r1, r2, r4, r5, r6}
     538:	2800db1b 	stmdacs	r0, {r0, r1, r3, r4, r8, r9, fp, ip, lr, pc}
     53c:	4288db0f 	addmi	sp, r8, #15360	@ 0x3c00
     540:	1c01db06 			@ <UNDEFINED> instruction: 0x1c01db06
     544:	f7ff1c18 			@ <UNDEFINED> instruction: 0xf7ff1c18
     548:	f7ffff2d 			@ <UNDEFINED> instruction: 0xf7ffff2d
     54c:	e026ff91 	mla	r6, r1, pc, pc	@ <UNPREDICTABLE>
     550:	f7ff1c10 			@ <UNDEFINED> instruction: 0xf7ff1c10
     554:	f7ffff27 			@ <UNDEFINED> instruction: 0xf7ffff27
     558:	1a30ff8b 	bne	0xc4038c
     55c:	428ce01f 	addmi	lr, ip, #31
     560:	1c01dbf6 			@ <UNDEFINED> instruction: 0x1c01dbf6
     564:	f7ff1c18 			@ <UNDEFINED> instruction: 0xf7ff1c18
     568:	f7ffff1d 			@ <UNDEFINED> instruction: 0xf7ffff1d
     56c:	1838ff81 	ldmdane	r8!, {r0, r7, r8, r9, sl, fp, ip, sp, lr, pc}
     570:	2800e015 	stmdacs	r0, {r0, r2, r4, sp, lr, pc}
     574:	42acdc09 	adcmi	sp, ip, #2304	@ 0x900
     578:	1c10dcf3 	ldcne	12, cr13, [r0], {243}	@ 0xf3
     57c:	ff12f7ff 			@ <UNDEFINED> instruction: 0xff12f7ff
     580:	ff76f7ff 			@ <UNDEFINED> instruction: 0xff76f7ff
     584:	1a3019f6 	bne	0xc06d64
     588:	42a8e009 	adcmi	lr, r8, #9
     58c:	1c01dbf5 			@ <UNDEFINED> instruction: 0x1c01dbf5
     590:	f7ff1c18 			@ <UNDEFINED> instruction: 0xf7ff1c18
     594:	f7ffff07 			@ <UNDEFINED> instruction: 0xf7ffff07
     598:	19ffff6b 	ldmibne	pc!, {r0, r1, r3, r5, r6, r8, r9, sl, fp, ip, sp, lr, pc}^	@ <UNPREDICTABLE>
     59c:	bcf01838 	ldcllt	8, cr1, [r0], #224	@ 0xe0
     5a0:	4718bc08 	ldrmi	fp, [r8, -r8, lsl #24]
     5a4:	2608b578 			@ <UNDEFINED> instruction: 0x2608b578
     5a8:	259e0636 	ldrcs	r0, [lr, #1590]	@ 0x636
     5ac:	1e6819ad 	vmulne.f16	s3, s17, s27	@ <UNPREDICTABLE>
     5b0:	f000211b 			@ <UNDEFINED> instruction: 0xf000211b
     5b4:	240cf87b 	strcs	pc, [ip], #-2171	@ 0xfffff785
     5b8:	782b4344 	stmdavc	fp!, {r2, r6, r8, r9, lr}
     5bc:	0f9b079b 	svceq	0x009b079b
     5c0:	435a2230 	cmpmi	sl, #48, 4
     5c4:	a50918a4 	strge	r1, [r9, #-2212]	@ 0xfffff75c
     5c8:	2400192d 	strcs	r1, [r0], #-2349	@ 0xfffff6d3
     5cc:	f0001c20 			@ <UNDEFINED> instruction: 0xf0001c20
     5d0:	2c03f87e 	stccs	8, cr15, [r3], {126}	@ 0x7e
     5d4:	2c09db06 			@ <UNDEFINED> instruction: 0x2c09db06
     5d8:	8829da04 	stmdahi	r9!, {r2, r9, fp, ip, lr, pc}
     5dc:	43310049 	teqmi	r1, #73	@ 0x49
     5e0:	1cad8808 	stcne	8, cr8, [sp], #32
     5e4:	2c0b1c64 	stccs	12, cr1, [fp], {100}	@ 0x64
     5e8:	bd78d1f0 	ldfltp	f5, [r8, #-960]!	@ 0xfffffc40
     5ec:	7426479b 	strtvc	r4, [r6], #-1947	@ 0xfffff865
     5f0:	6d4f11bc 	stfvse	f1, [pc, #-752]	@ 0x308
     5f4:	32f111bd 	rscscc	r1, r1, #1073741871	@ 0x4000002f
     5f8:	2ce77fd9 	stclcs	15, cr7, [r7], #868	@ 0x364
     5fc:	11bd5da5 			@ <UNDEFINED> instruction: 0x11bd5da5
     600:	5da44610 	stcpl	6, cr4, [r4, #64]!	@ 0x40
     604:	61734e90 			@ <UNDEFINED> instruction: 0x61734e90
     608:	4e912a84 	vfnmsmi.f32	s4, s3, s8
     60c:	75fe106a 	ldrbvc	r1, [lr, #106]!	@ 0x6a
     610:	783929c8 	ldmdavc	r9!, {r3, r6, r7, r8, fp, sp}
     614:	5d1b420e 	lfmpl	f4, 4, [fp, #-56]	@ 0xffffffc8
     618:	12a87838 	adcne	r7, r8, #56, 16	@ 0x380000
     61c:	67b93f7d 			@ <UNDEFINED> instruction: 0x67b93f7d
     620:	54ef26f3 	strbtpl	r2, [pc], #1779	@ 0x628
     624:	26f27c23 	ldrbtcs	r7, [r2], r3, lsr #24
     628:	41376bc6 	teqmi	r7, r6, asr #23
     62c:	730d15ab 	movwvc	r1, #54699	@ 0xd5ab
     630:	3b4f6bc7 	blcc	0x13db554
     634:	3dda5f24 	ldclcc	15, cr5, [sl, #144]	@ 0x90
     638:	1749253f 	smlaldxne	r2, r9, pc, r5	@ <UNPREDICTABLE>
     63c:	70e63ddb 	ldrdvc	r3, [r6], #219	@ 0xdb	@ <UNPREDICTABLE>
     640:	30f7746c 	rscscc	r7, r7, ip, ror #8
     644:	6738531f 			@ <UNDEFINED> instruction: 0x6738531f
     648:	1a51531e 	bne	0x14552c8
     64c:	5b7d1971 	blpl	0x1f46c18
     650:	19704ed6 	ldmdbne	r0!, {r1, r2, r4, r6, r7, r9, sl, fp, lr}^
     654:	75cb3f27 	strbvc	r3, [fp, #3879]	@ 0xf27
     658:	128c3d62 	addne	r3, ip, #6272	@ 0x1880
     65c:	2fad74b8 	svccs	0x00ad74b8
     660:	64fd74b9 	ldrbtvs	r7, [sp], #1209	@ 0x4b9
     664:	4f3a6c9a 	svcmi	0x003a6c9a
     668:	73ef276d 	mvnvc	r2, #28573696	@ 0x1b40000
     66c:	4f3b38b1 	svcmi	0x003b38b1
     670:	7ea3571e 	mcrvc	7, 5, r5, cr3, cr14, {0}
     674:	35876249 	strcc	r6, [r7, #585]	@ 0x249
     678:	35861b7c 	strcc	r1, [r6, #2940]	@ 0xb7c
     67c:	67e47afb 			@ <UNDEFINED> instruction: 0x67e47afb
     680:	67e55c92 			@ <UNDEFINED> instruction: 0x67e55c92
     684:	438c2bca 	orrmi	r2, ip, #206848	@ 0x32800
     688:	587f2e6f 	ldmdapl	pc!, {r0, r1, r2, r3, r5, r6, r9, sl, fp, sp}^	@ <UNPREDICTABLE>
     68c:	2e6e14b7 	mcrcs	4, 3, r1, cr14, cr7, {5}
     690:	6fa24cb9 	svcvs	0x00a24cb9
     694:	719e38f0 			@ <UNDEFINED> instruction: 0x719e38f0
     698:	1f3c475a 	svcne	0x003c475a
     69c:	475b6ad8 	smmlsmi	fp, r8, sl, r6
     6a0:	32645199 	rsbcc	r5, r4, #1073741862	@ 0x40000026
     6a4:	49ef7b41 	stmibmi	pc!, {r0, r6, r8, r9, fp, ip, sp, lr}^	@ <UNPREDICTABLE>
     6a8:	1cd75198 	ldfnee	f5, [r7], {152}	@ 0x98
     6ac:	2403b530 	strcs	fp, [r3], #-1328	@ 0xfffffad0
     6b0:	78022300 	stmdavc	r2, {r8, r9, sp}
     6b4:	250441e3 	strcs	r4, [r4, #-483]	@ 0xfffffe1d
     6b8:	02124053 	andseq	r4, r2, #83	@ 0x53
     6bc:	dcfb1e6d 	ldclle	14, cr1, [fp], #436	@ 0x1b4
     6c0:	1e491c40 	cdpne	12, 4, cr1, cr9, cr0, {2}
     6c4:	1c18dcf5 	ldcne	12, cr13, [r8], {245}	@ 0xf5
     6c8:	0f8006c0 	svceq	0x008006c0
     6cc:	b510bd30 	ldrlt	fp, [r0, #-3376]	@ 0xfffff2d0
     6d0:	43442414 	movtmi	r2, #17428	@ 0x4414
     6d4:	061b2308 	ldreq	r2, [fp], -r8, lsl #6
     6d8:	19001d18 	stmdbne	r0, {r3, r4, r8, sl, fp, ip}
     6dc:	190949f8 	stmdbne	r9, {r3, r4, r5, r6, r7, r8, fp, lr}
     6e0:	f000220a 			@ <UNDEFINED> instruction: 0xf000220a
     6e4:	bd10fa33 	vldrlt	s30, [r0, #-204]	@ 0xffffff34
     6e8:	49f6b570 	ldmibmi	r6!, {r4, r5, r6, r8, sl, ip, sp, pc}^
     6ec:	24ff2600 	ldrbtcs	r2, [pc], #1536	@ 0x6f4
     6f0:	d1002e98 			@ <UNDEFINED> instruction: 0xd1002e98
     6f4:	2e9a247b 	mrccs	4, 4, r2, cr10, cr11, {3}
     6f8:	24fcd100 	ldrbtcs	sp, [ip], #256	@ 0x100
     6fc:	da062e9c 	ble	0x18c174
     700:	5d8b5d82 	stcpl	13, cr5, [fp, #520]	@ 0x208
     704:	1c764022 	ldclne	0, cr4, [r6], #-136	@ 0xffffff78
     708:	d0f0429a 	smlalsle	r4, r0, sl, r2
     70c:	2419e009 	ldrcs	lr, [r9], #-9
     710:	18a45d82 	stmiane	r4!, {r1, r7, r8, sl, fp, ip, lr}
     714:	2eba1c76 	mrccs	12, 5, r1, cr10, cr6, {3}
     718:	0620dbfa 			@ <UNDEFINED> instruction: 0x0620dbfa
     71c:	2000d101 	andcs	sp, r0, r1, lsl #2
     720:	2001e000 	andcs	lr, r1, r0
     724:	4be8bd70 	blmi	0xffa2fcec
     728:	207e2208 	rsbscs	r2, lr, r8, lsl #4
     72c:	50984240 	addspl	r4, r8, r0, asr #4
     730:	2a783210 	bcs	0x1e0cf78
     734:	4770dbfb 			@ <UNDEFINED> instruction: 0x4770dbfb
     738:	1ec3b540 	cdpne	5, 12, cr11, cr3, cr0, {2}
     73c:	4356009e 	cmpmi	r6, #158	@ 0x9e
     740:	1a9b2340 	bne	0xfe6c9448
     744:	1ec0435e 	mcrne	3, 6, r4, cr0, cr14, {2}
     748:	43432318 	movtmi	r2, #13080	@ 0x3318
     74c:	1af6021b 	bne	0xffd80fc0
     750:	2a2f600e 	bcs	0xbd8790
     754:	261adc07 	ldrcs	sp, [sl], -r7, lsl #24
     758:	3a484356 	bcc	0x12114b8
     75c:	23684356 	cmncs	r8, #1476395009	@ 0x58000001
     760:	18f6021b 	ldmne	r6!, {r0, r1, r3, r4, r9}^
     764:	bd40604e 	stcllt	0, cr6, [r0, #-312]	@ 0xfffffec8
     768:	1c0fb5f0 	cfstr32ne	mvfx11, [pc], {240}	@ 0xf0
     76c:	3680c870 	sxtab16cc	ip, r0, r0, ror #16
     770:	20801c31 	addcs	r1, r0, r1, lsr ip
     774:	f7ff0400 			@ <UNDEFINED> instruction: 0xf7ff0400
     778:	0073fe15 	rsbseq	pc, r3, r5, lsl lr	@ <UNPREDICTABLE>
     77c:	81fb81bb 	ldrhhi	r8, [fp, #27]!
     780:	01c9217f 	biceq	r2, r9, pc, ror r1
     784:	60796039 	rsbsvs	r6, r9, r9, lsr r0
     788:	43411221 	movtmi	r1, #4641	@ 0x1221
     78c:	31781409 	cmncc	r8, r9, lsl #8
     790:	12298139 	eorne	r8, r9, #1073741838	@ 0x4000000e
     794:	14094341 	strne	r4, [r9], #-833	@ 0xfffffcbf
     798:	81793150 	cmnhi	r9, r0, asr r1
     79c:	b5f0bdf0 	ldrblt	fp, [r0, #3568]!	@ 0xdf0
     7a0:	9d069c05 	stcls	12, cr9, [r6, #-20]	@ 0xffffffec
     7a4:	26002700 	strcs	r2, [r0], -r0, lsl #14
     7a8:	184053a0 	stmdane	r0, {r5, r7, r8, r9, ip, lr}^
     7ac:	42961cb6 	addsmi	r1, r6, #46592	@ 0xb600
     7b0:	1964dbfa 	stmdbne	r4!, {r1, r3, r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}^
     7b4:	429f1c7f 	addsmi	r1, pc, #32512	@ 0x7f00
     7b8:	bdf0dbf5 			@ <UNDEFINED> instruction: 0xbdf0dbf5
     7bc:	2702b5f0 			@ <UNDEFINED> instruction: 0x2702b5f0
     7c0:	00434cc2 	subeq	r4, r3, r2, asr #25
     7c4:	19db181b 	ldmibne	fp, {r0, r1, r3, r4, fp, ip}^
     7c8:	191b009b 	ldmdbne	fp, {r0, r1, r3, r4, r7}
     7cc:	691e685d 	ldmdbvs	lr, {r0, r2, r3, r4, r6, fp, sp, lr}
     7d0:	1a5b2320 	bne	0x16c9458
     7d4:	434e436b 	movtmi	r4, #58219	@ 0xe36b
     7d8:	095c199b 	ldmdbeq	ip, {r0, r1, r3, r4, r7, r8, fp, ip}^
     7dc:	0533261f 	ldreq	r2, [r3, #-1567]!	@ 0xfffff9e1
     7e0:	0a9d4023 	beq	0xfe750874
     7e4:	402302b3 	strhtmi	r0, [r3], -r3
     7e8:	432b095b 			@ <UNDEFINED> instruction: 0x432b095b
     7ec:	431c4034 	tstmi	ip, #52	@ 0x34
     7f0:	005e19d3 	ldrsbeq	r1, [lr], #-147	@ 0xffffff6d
     7f4:	18f34bb6 	ldmne	r3!, {r1, r2, r4, r5, r7, r8, r9, fp, lr}^
     7f8:	1e7f801c 	mrcne	0, 3, r8, cr15, cr12, {0}
     7fc:	bdf0dae0 			@ <UNDEFINED> instruction: 0xbdf0dae0
     800:	02092102 	andeq	r2, r9, #-2147483648	@ 0x80000000
     804:	4bb3468c 	blmi	0xfecd223c
     808:	4bb2881a 	blmi	0xfeca2878
     80c:	0d890591 	cfstr32eq	mvfx0, [r9, #580]	@ 0x244
     810:	d0032800 	andle	r2, r3, r0, lsl #16
     814:	da094561 	ble	0x251da0
     818:	e0021c92 	mul	r2, r2, ip
     81c:	dd052900 	vstrle.16	s4, [r5, #-0]	@ <UNPREDICTABLE>
     820:	801a1e92 	mulshi	sl, r2, lr
     824:	1e522208 	cdpne	2, 5, cr2, cr2, cr8, {0}
     828:	e7e9d5fd 			@ <UNDEFINED> instruction: 0xe7e9d5fd
     82c:	49aa4770 	stmibmi	sl!, {r4, r5, r6, r8, r9, sl, lr}
     830:	01122237 	tsteq	r2, r7, lsr r2
     834:	e00f48a9 	and	r4, pc, r9, lsr #17
     838:	222449a7 	eorcs	r4, r4, #2736128	@ 0x29c000
     83c:	e00b48a8 	and	r4, fp, r8, lsr #17
     840:	06092107 	streq	r2, [r9], -r7, lsl #2
     844:	48a72250 	stmiami	r7!, {r4, r6, r9, sp}
     848:	49a7e006 	stmibmi	r7!, {r1, r2, sp, lr, pc}
     84c:	d0012800 	andle	r2, r1, r0, lsl #16
     850:	18090240 	stmdane	r9, {r6, r9}
     854:	48a52208 	stmiami	r5!, {r3, r9, sp}
     858:	1852b530 	ldmdane	r2, {r4, r5, r8, sl, ip, sp, pc}^
     85c:	42984b9b 	addsmi	r4, r8, #158720	@ 0x26c00
     860:	2304db07 	movwcs	sp, #19207	@ 0x4b07
     864:	4298031b 	addsmi	r0, r8, #1811939328	@ 0x6c000000
     868:	c808da03 	stmdagt	r8, {r0, r1, r9, fp, ip, lr, pc}
     86c:	4291c108 	addsmi	ip, r1, #8, 2
     870:	bd30dbf4 	vldmdblt	r0!, {d13-<overflow reg d70>}
     874:	b085b5f0 	strdlt	fp, [r5], r0
     878:	c9a0499d 	stmibgt	r0!, {r0, r2, r3, r4, r7, r8, fp, lr}
     87c:	c0a0a802 	adcgt	sl, r0, r2, lsl #16
     880:	4b9d489c 	blmi	0xfe752af8
     884:	09db781b 	ldmibeq	fp, {r0, r1, r3, r4, fp, ip, sp, lr}^
     888:	489cd100 	ldmmi	ip, {r8, ip, lr, pc}
     88c:	220a4992 	andcs	r4, sl, #2392064	@ 0x248000
     890:	f95cf000 			@ <UNDEFINED> instruction: 0xf95cf000
     894:	fe86f7ff 	mcr2	7, 4, pc, cr6, cr15, {7}	@ <UNPREDICTABLE>
     898:	1c0b4989 			@ <UNDEFINED> instruction: 0x1c0b4989
     89c:	781833ae 	ldmdavc	r8, {r1, r2, r3, r5, r7, r8, r9, ip, sp}
     8a0:	d0052896 	mulle	r5, r6, r8
     8a4:	17d34a96 	bfine	r4, r6, (invalid: 21:19)
     8a8:	a8049304 	stmdage	r4, {r2, r8, r9, ip, pc}
     8ac:	f94ef000 			@ <UNDEFINED> instruction: 0xf94ef000
     8b0:	ffbdf7ff 			@ <UNDEFINED> instruction: 0xffbdf7ff
     8b4:	49934888 	ldmibmi	r3, {r3, r7, fp, lr}
     8b8:	fbaaf000 	blx	0xfeabc8c2
     8bc:	49864891 	stmibmi	r6, {r0, r4, r7, fp, lr}
     8c0:	fc1af000 	ldc2	0, cr15, [sl], {-0}
     8c4:	00b82700 	adcseq	r2, r8, r0, lsl #14
     8c8:	4a839003 	bmi	0xfe0e48dc
     8cc:	18800238 	stmne	r0, {r3, r4, r5, r9}
     8d0:	02b94b8d 	adcseq	r4, r9, #144384	@ 0x23400
     8d4:	aa0218c9 	bge	0x86c00
     8d8:	fb40f000 	blx	0x103c8e2
     8dc:	2f081c7f 	svccs	0x00081c7f
     8e0:	270edbf1 			@ <UNDEFINED> instruction: 0x270edbf1
     8e4:	4b882403 	blmi	0xfe2098f8
     8e8:	19000078 	stmdbne	r0, {r3, r4, r5, r6}
     8ec:	18c00200 	stmiane	r0, {r9}^
     8f0:	5bda4b86 	blpl	0xff693710
     8f4:	01214b86 	smlawbeq	r1, r6, fp, r4
     8f8:	01891889 	orreq	r1, r9, r9, lsl #17
     8fc:	228018c9 	addcs	r1, r0, #13172736	@ 0xc90000
     900:	f924f000 			@ <UNDEFINED> instruction: 0xf924f000
     904:	daee1e64 	ble	0xffb8829c
     908:	daeb1ebf 	ble	0xffac840c
     90c:	f000486c 			@ <UNDEFINED> instruction: 0xf000486c
     910:	f000f81c 			@ <UNDEFINED> instruction: 0xf000f81c
     914:	f000f82f 			@ <UNDEFINED> instruction: 0xf000f82f
     918:	2220f834 	eorcs	pc, r0, #52, 16	@ 0x340000
     91c:	497d9201 	ldmdbmi	sp!, {r0, r9, ip, pc}^
     920:	23049100 	movwcs	r9, #16640	@ 0x4100
     924:	497c2204 	ldmdbmi	ip!, {r2, r9, sp}^
     928:	f7ff487c 			@ <UNDEFINED> instruction: 0xf7ff487c
     92c:	2105ff38 	tstcs	r5, r8, lsr pc	@ <UNPREDICTABLE>
     930:	43c80609 	bicmi	r0, r8, #9437184	@ 0x900000
     934:	20008008 	andcs	r8, r0, r8
     938:	ff87f7ff 			@ <UNDEFINED> instruction: 0xff87f7ff
     93c:	f7ff2001 			@ <UNDEFINED> instruction: 0xf7ff2001
     940:	f7ffff84 			@ <UNDEFINED> instruction: 0xf7ffff84
     944:	b005ff7d 	andlt	pc, r5, sp, ror pc	@ <UNPREDICTABLE>
     948:	b5f1bdf0 	ldrblt	fp, [r1, #3568]!	@ 0xdf0
     94c:	70204c74 	eorvc	r4, r0, r4, ror ip
     950:	ff72f7ff 			@ <UNDEFINED> instruction: 0xff72f7ff
     954:	49739800 	ldmdbmi	r3!, {fp, ip, pc}^
     958:	f000224e 			@ <UNDEFINED> instruction: 0xf000224e
     95c:	485ef8f7 	ldmdami	lr, {r0, r1, r2, r4, r5, r6, r7, fp, ip, sp, lr, pc}^
     960:	f0004968 			@ <UNDEFINED> instruction: 0xf0004968
     964:	4867fb55 	stmdami	r7!, {r0, r2, r4, r6, r8, r9, fp, ip, sp, lr, pc}^
     968:	60024a6f 	andvs	r4, r2, pc, ror #20
     96c:	f000495a 			@ <UNDEFINED> instruction: 0xf000495a
     970:	bdf1fd13 	ldcllt	13, cr15, [r1, #76]!	@ 0x4c
     974:	4858b5f1 	ldmdami	r8, {r0, r4, r5, r6, r7, r8, sl, ip, sp, pc}^
     978:	4a6c4962 	bmi	0x1b12f08
     97c:	faeef000 	blx	0xffbbc984
     980:	b5f1bdf1 	ldrblt	fp, [r1, #3569]!	@ 0xdf1
     984:	4c6a4e5f 	stclmi	14, cr4, [sl], #-380	@ 0xfffffe84
     988:	25342702 	ldrcs	r2, [r4, #-1794]!	@ 0xfffff8fe
     98c:	c40fce0f 	strgt	ip, [pc], #-3599	@ 0x994
     990:	dcfb1e6d 	ldclle	14, cr1, [fp], #436	@ 0x1b4
     994:	1e7f34c0 	cdpne	4, 7, cr3, cr15, cr0, {6}
     998:	2703dcf7 			@ <UNDEFINED> instruction: 0x2703dcf7
     99c:	486502bb 	stmdami	r5!, {r0, r1, r3, r4, r5, r7, r9}^
     9a0:	496518c0 	stmdbmi	r5!, {r6, r7, fp, ip}^
     9a4:	220118c9 	andcs	r1, r1, #13172736	@ 0xc90000
     9a8:	f0000212 			@ <UNDEFINED> instruction: 0xf0000212
     9ac:	1e7ff909 	vaddne.f16	s31, s30, s18	@ <UNPREDICTABLE>
     9b0:	4668dcf4 			@ <UNDEFINED> instruction: 0x4668dcf4
     9b4:	49486007 	stmdbmi	r8, {r0, r1, r2, sp, lr}^
     9b8:	02122208 	andseq	r2, r2, #8, 4	@ 0x80000000
     9bc:	f879f000 			@ <UNDEFINED> instruction: 0xf879f000
     9c0:	b5f0bdf1 	ldrblt	fp, [r0, #3569]!	@ 0xdf1
     9c4:	1c07b081 	stcne	0, cr11, [r7], {129}	@ 0x81
     9c8:	24044d5c 	strcs	r4, [r4], #-3420	@ 0xfffff2a4
     9cc:	23000624 	movwcs	r0, #1572	@ 0x624
     9d0:	21809300 	orrcs	r9, r0, r0, lsl #6
     9d4:	26808021 	strcs	r8, [r0], r1, lsr #32
     9d8:	d01d423e 	andsle	r4, sp, lr, lsr r2
     9dc:	19090c61 	stmdbne	r9, {r0, r5, r6, sl, fp}
     9e0:	f0002208 			@ <UNDEFINED> instruction: 0xf0002208
     9e4:	3920f863 	stmdbcc	r0!, {r0, r1, r5, r6, fp, ip, sp, lr, pc}
     9e8:	804843d0 	ldrdhi	r4, [r8], #-48	@ 0xffffffd0
     9ec:	19090c21 	stmdbne	r9, {r0, r5, sl, fp}
     9f0:	1d217408 	cfstrsne	mvf7, [r1, #-32]!	@ 0xffffffe0
     9f4:	f0002208 			@ <UNDEFINED> instruction: 0xf0002208
     9f8:	1f09f859 	svcne	0x0009f859
     9fc:	f0002210 			@ <UNDEFINED> instruction: 0xf0002210
     a00:	21b0f855 	asrscs	pc, r5, r8	@ <UNPREDICTABLE>
     a04:	22181909 	andscs	r1, r8, #147456	@ 0x24000
     a08:	f850f000 			@ <UNDEFINED> instruction: 0xf850f000
     a0c:	0ca0620a 	sfmeq	f6, 4, [r0], #40	@ 0x28
     a10:	86208420 	strthi	r8, [r0], -r0, lsr #8
     a14:	86e084e0 	strbthi	r8, [r0], r0, ror #9
     a18:	49492620 	stmdbmi	r9, {r5, r9, sl, sp}^
     a1c:	f0002208 			@ <UNDEFINED> instruction: 0xf0002208
     a20:	0ae2f845 	beq	0xff8beb3c
     a24:	3110808a 	tstcc	r0, sl, lsl #1
     a28:	700a2207 	andvc	r2, sl, r7, lsl #4
     a2c:	f83ef000 			@ <UNDEFINED> instruction: 0xf83ef000
     a30:	423e2640 	eorsmi	r2, lr, #64, 12	@ 0x4000000
     a34:	2180d019 	orrcs	sp, r0, r9, lsl r0
     a38:	48421909 	stmdami	r2, {r0, r3, r8, fp, ip}^
     a3c:	71097108 	tstvc	r9, r8, lsl #2
     a40:	89086008 	stmdbhi	r8, {r3, sp, lr}
     a44:	0d800580 	cfstr32eq	mvfx0, [r0, #512]	@ 0x200
     a48:	39108108 	ldmdbcc	r0, {r3, r8, pc}
     a4c:	31207009 			@ <UNDEFINED> instruction: 0x31207009
     a50:	f0002208 			@ <UNDEFINED> instruction: 0xf0002208
     a54:	3940f82b 	stmdbcc	r0, {r0, r1, r3, r5, fp, ip, sp, lr, pc}^
     a58:	3120700a 			@ <UNDEFINED> instruction: 0x3120700a
     a5c:	f0002208 			@ <UNDEFINED> instruction: 0xf0002208
     a60:	2200f825 	andcs	pc, r0, #2424832	@ 0x250000
     a64:	19092180 	stmdbne	r9, {r7, r8, sp}
     a68:	2601710a 	strcs	r7, [r1], -sl, lsl #2
     a6c:	0aa20861 	beq	0xfe882bf8
     a70:	f81cf000 			@ <UNDEFINED> instruction: 0xf81cf000
     a74:	21062608 	tstcs	r6, r8, lsl #12
     a78:	0b0a0609 	bleq	0x2822a4
     a7c:	f816f000 			@ <UNDEFINED> instruction: 0xf816f000
     a80:	21072610 	tstcs	r7, r0, lsl r6
     a84:	0ca20609 	stceq	6, cr0, [r2], #36	@ 0x24
     a88:	f810f000 			@ <UNDEFINED> instruction: 0xf810f000
     a8c:	21052604 	tstcs	r5, r4, lsl #12
     a90:	0ca20609 	stceq	6, cr0, [r2], #36	@ 0x24
     a94:	f80af000 			@ <UNDEFINED> instruction: 0xf80af000
     a98:	21032602 	tstcs	r3, r2, lsl #12
     a9c:	4a2a0609 	bmi	0xa822c8
     aa0:	f804f000 			@ <UNDEFINED> instruction: 0xf804f000
     aa4:	bcf0b001 	ldcllt	0, cr11, [r0], #4
     aa8:	4718bc08 	ldrmi	fp, [r8, -r8, lsl #24]
     aac:	d100423e 	tstle	r0, lr, lsr r2
     ab0:	46684770 			@ <UNDEFINED> instruction: 0x46684770
     ab4:	e083432a 	add	r4, r3, sl, lsr #6
     ab8:	00002d71 	andeq	r2, r0, r1, ror sp
     abc:	0000210d 	andeq	r2, r0, sp, lsl #2
     ac0:	03000088 	movweq	r0, #136	@ 0x88
     ac4:	00003290 	muleq	r0, r0, r2
     ac8:	03003580 	movweq	r3, #1408	@ 0x580
     acc:	00003200 	andeq	r3, r0, r0, lsl #4
     ad0:	05000200 	streq	r0, [r0, #-512]	@ 0xfffffe00
     ad4:	04000088 	streq	r0, [r0], #-136	@ 0xffffff78
     ad8:	03000564 	movweq	r0, #1380	@ 0x564
     adc:	0000332c 	andeq	r3, r0, ip, lsr #6
     ae0:	0000326c 	andeq	r3, r0, ip, ror #4
     ae4:	0000369c 	muleq	r0, ip, r6
     ae8:	05000038 	streq	r0, [r0, #-56]	@ 0xffffffc8
     aec:	00003264 	andeq	r3, r0, r4, ror #4
     af0:	000030c0 	andeq	r3, r0, r0, asr #1
     af4:	0bfe1fe0 	bleq	0xfff88a7c
     af8:	080000b4 	stmdaeq	r0, {r2, r4, r5, r7}
     afc:	0bffffe0 	bleq	0xa84
     b00:	85000027 	strhi	r0, [r0, #-39]	@ 0xffffffd9
     b04:	03001564 	movweq	r1, #1380	@ 0x564
     b08:	06000040 	streq	r0, [r0], -r0, asr #32
     b0c:	000030b0 	strheq	r3, [r0], -r0
     b10:	06010000 	streq	r0, [r1], -r0
     b14:	0600b880 	streq	fp, [r0], -r0, lsl #17
     b18:	00000202 	andeq	r0, r0, r2, lsl #4
     b1c:	00007271 	andeq	r7, r0, r1, ror r2
     b20:	03007ff7 	movweq	r7, #4087	@ 0xff7
     b24:	03000588 	movweq	r0, #1416	@ 0x588
     b28:	0000d082 	andeq	sp, r0, r2, lsl #1
     b2c:	000030c8 	andeq	r3, r0, r8, asr #1
     b30:	060024c0 	streq	r2, [r0], -r0, asr #9
     b34:	06002040 	streq	r2, [r0], -r0, asr #32
     b38:	06016800 	streq	r6, [r1], -r0, lsl #16
     b3c:	85000000 	strhi	r0, [r0, #-0]
     b40:	04000110 	streq	r0, [r0], #-272	@ 0xfffffef0
     b44:	880e0000 	stmdahi	lr, {}	@ <UNPREDICTABLE>
     b48:	00001f80 	andeq	r1, r0, r0, lsl #31
     b4c:	02d4b530 	sbcseq	fp, r4, #48, 10	@ 0xc000000
     b50:	f0000a64 			@ <UNDEFINED> instruction: 0xf0000a64
     b54:	d01ef823 	andsle	pc, lr, r3, lsr #16
     b58:	0ed32500 	cdpeq	5, 13, cr2, cr3, cr0, {0}
     b5c:	190dd30c 	stmdbne	sp, {r2, r3, r8, r9, ip, lr, pc}
     b60:	d3040e53 	movwle	r0, #20051	@ 0x4e53
     b64:	42a9c808 	adcmi	ip, r9, #8, 16	@ 0x80000
     b68:	c108da15 	tstgt	r8, r5, lsl sl
     b6c:	42a9e7fb 	adcmi	lr, r9, #65798144	@ 0x3ec0000
     b70:	c808da11 	stmdagt	r8, {r0, r4, r9, fp, ip, lr, pc}
     b74:	e7fac108 	ldrb	ip, [sl, r8, lsl #2]!
     b78:	0e530864 	cdpeq	8, 5, cr0, cr3, cr4, {3}
     b7c:	8803d305 	stmdahi	r3, {r0, r2, r8, r9, ip, lr, pc}
     b80:	da0842a5 	ble	0x21161c
     b84:	1cad534b 	stcne	3, cr5, [sp], #300	@ 0x12c
     b88:	42a5e7fa 	adcmi	lr, r5, #65536000	@ 0x3e80000
     b8c:	5b43da03 	blpl	0x10f73a0
     b90:	1cad534b 	stcne	3, cr5, [sp], #300	@ 0x12c
     b94:	bc30e7f9 	ldclt	7, cr14, [r0], #-996	@ 0xfffffc1c
     b98:	4718bc08 	ldrmi	fp, [r8, -r8, lsl #24]
     b9c:	46a4a301 	strtmi	sl, [r4], r1, lsl #6
     ba0:	00004718 	andeq	r4, r0, r8, lsl r7
     ba4:	e35c0000 	cmp	ip, #0
     ba8:	0a000003 	beq	0xbbc
     bac:	e3ccc4fe 	bic	ip, ip, #-33554432	@ 0xfe000000
     bb0:	e080c00c 	add	ip, r0, ip
     bb4:	e310040e 	tst	r0, #234881024	@ 0xe000000
     bb8:	131c040e 	tstne	ip, #234881024	@ 0xe000000
     bbc:	e12fff1e 	bx	lr
     bc0:	4718467b 			@ <UNDEFINED> instruction: 0x4718467b
     bc4:	e92d47f0 	push	{r4, r5, r6, r7, r8, r9, sl, lr}
     bc8:	e1a0a582 	lsl	sl, r2, #11
     bcc:	e1b0c4aa 	lsrs	ip, sl, #9
     bd0:	ebfffff3 	bl	0xba4
     bd4:	0a000012 	beq	0xc24
     bd8:	e081a4aa 	add	sl, r1, sl, lsr #9
     bdc:	e1b02ca2 	lsrs	r2, r2, #25
     be0:	3a00000b 	bcc	0xc14
     be4:	e5902000 	ldr	r2, [r0]
     be8:	e1a03002 	mov	r3, r2
     bec:	e1a04002 	mov	r4, r2
     bf0:	e1a05002 	mov	r5, r2
     bf4:	e1a06002 	mov	r6, r2
     bf8:	e1a07002 	mov	r7, r2
     bfc:	e1a08002 	mov	r8, r2
     c00:	e1a09002 	mov	r9, r2
     c04:	e151000a 	cmp	r1, sl
     c08:	b8a103fc 	stmialt	r1!, {r2, r3, r4, r5, r6, r7, r8, r9}
     c0c:	bafffffc 	blt	0xc04
     c10:	ea000003 	b	0xc24
     c14:	e151000a 	cmp	r1, sl
     c18:	b8b003fc 	ldmlt	r0!, {r2, r3, r4, r5, r6, r7, r8, r9}
     c1c:	b8a103fc 	stmialt	r1!, {r2, r3, r4, r5, r6, r7, r8, r9}
     c20:	bafffffb 	blt	0xc14
     c24:	e8bd47f0 	pop	{r4, r5, r6, r7, r8, r9, sl, lr}
     c28:	e12fff1e 	bx	lr
     c2c:	e92d0ff0 	push	{r4, r5, r6, r7, r8, r9, sl, fp}
     c30:	e2522001 	subs	r2, r2, #1
     c34:	ba000027 	blt	0xcd8
     c38:	e1d031b0 	ldrh	r3, [r0, #16]
     c3c:	e1a03423 	lsr	r3, r3, #8
     c40:	e28fcf45 	add	ip, pc, #276	@ 0x114
     c44:	e2838040 	add	r8, r3, #64	@ 0x40
     c48:	e20880ff 	and	r8, r8, #255	@ 0xff
     c4c:	e1a08088 	lsl	r8, r8, #1
     c50:	e198b0fc 	ldrsh	fp, [r8, ip]
     c54:	e1a08083 	lsl	r8, r3, #1
     c58:	e198c0fc 	ldrsh	ip, [r8, ip]
     c5c:	e1d090fc 	ldrsh	r9, [r0, #12]
     c60:	e1d0a0fe 	ldrsh	sl, [r0, #14]
     c64:	e008099b 	mul	r8, fp, r9
     c68:	e1a03748 	asr	r3, r8, #14
     c6c:	e008099c 	mul	r8, ip, r9
     c70:	e1a04748 	asr	r4, r8, #14
     c74:	e0080a9c 	mul	r8, ip, sl
     c78:	e1a05748 	asr	r5, r8, #14
     c7c:	e0080a9b 	mul	r8, fp, sl
     c80:	e1a06748 	asr	r6, r8, #14
     c84:	e8901600 	ldm	r0, {r9, sl, ip}
     c88:	e1a0b80c 	lsl	fp, ip, #16
     c8c:	e1a0b84b 	asr	fp, fp, #16
     c90:	e1a0c84c 	asr	ip, ip, #16
     c94:	e26b8000 	rsb	r8, fp, #0
     c98:	e0299893 	mla	r9, r3, r8, r9
     c9c:	e0289c94 	mla	r8, r4, ip, r9
     ca0:	e5818008 	str	r8, [r1, #8]
     ca4:	e26b8000 	rsb	r8, fp, #0
     ca8:	e02aa895 	mla	sl, r5, r8, sl
     cac:	e26c8000 	rsb	r8, ip, #0
     cb0:	e028a896 	mla	r8, r6, r8, sl
     cb4:	e581800c 	str	r8, [r1, #12]
     cb8:	e1c130b0 	strh	r3, [r1]
     cbc:	e2644000 	rsb	r4, r4, #0
     cc0:	e1c140b2 	strh	r4, [r1, #2]
     cc4:	e1c150b4 	strh	r5, [r1, #4]
     cc8:	e1c160b6 	strh	r6, [r1, #6]
     ccc:	e2800014 	add	r0, r0, #20
     cd0:	e2811010 	add	r1, r1, #16
     cd4:	eaffffd5 	b	0xc30
     cd8:	e8bd0ff0 	pop	{r4, r5, r6, r7, r8, r9, sl, fp}
     cdc:	e12fff1e 	bx	lr
     ce0:	e92d0f00 	push	{r8, r9, sl, fp}
     ce4:	e2522001 	subs	r2, r2, #1
     ce8:	ba000019 	blt	0xd54
     cec:	e1d090b4 	ldrh	r9, [r0, #4]
     cf0:	e1a09429 	lsr	r9, r9, #8
     cf4:	e28fc060 	add	ip, pc, #96	@ 0x60
     cf8:	e2898040 	add	r8, r9, #64	@ 0x40
     cfc:	e20880ff 	and	r8, r8, #255	@ 0xff
     d00:	e1a08088 	lsl	r8, r8, #1
     d04:	e198b0fc 	ldrsh	fp, [r8, ip]
     d08:	e1a08089 	lsl	r8, r9, #1
     d0c:	e198c0fc 	ldrsh	ip, [r8, ip]
     d10:	e1d090f0 	ldrsh	r9, [r0]
     d14:	e1d0a0f2 	ldrsh	sl, [r0, #2]
     d18:	e008099b 	mul	r8, fp, r9
     d1c:	e1a08748 	asr	r8, r8, #14
     d20:	e08180b3 	strh	r8, [r1], r3
     d24:	e008099c 	mul	r8, ip, r9
     d28:	e1a08748 	asr	r8, r8, #14
     d2c:	e2688000 	rsb	r8, r8, #0
     d30:	e08180b3 	strh	r8, [r1], r3
     d34:	e0080a9c 	mul	r8, ip, sl
     d38:	e1a08748 	asr	r8, r8, #14
     d3c:	e08180b3 	strh	r8, [r1], r3
     d40:	e0080a9b 	mul	r8, fp, sl
     d44:	e1a08748 	asr	r8, r8, #14
     d48:	e08180b3 	strh	r8, [r1], r3
     d4c:	e2800008 	add	r0, r0, #8
     d50:	eaffffe3 	b	0xce4
     d54:	e8bd0f00 	pop	{r8, r9, sl, fp}
     d58:	e12fff1e 	bx	lr
     d5c:	01920000 	orrseq	r0, r2, r0
     d60:	04b50323 	ldrteq	r0, [r5], #803	@ 0x323
     d64:	07d50645 	ldrbeq	r0, [r5, r5, asr #12]
     d68:	0af10964 	beq	0xffc43300
     d6c:	0e050c7c 	mcreq	12, 0, r0, cr5, cr12, {3}
     d70:	11110f8c 	tstne	r1, ip, lsl #31
     d74:	14131294 	ldrne	r1, [r3], #-660	@ 0xfffffd6c
     d78:	1708158f 	strne	r1, [r8, -pc, lsl #11]
     d7c:	19ef187d 	stmibne	pc!, {r0, r2, r3, r4, r5, r6, fp, ip}^	@ <UNPREDICTABLE>
     d80:	1cc61b5d 	fstmiaxne	r6, {d17-d62}	@ Deprecated
     d84:	1f8b1e2b 	svcne	0x008b1e2b
     d88:	223d20e7 	eorscs	r2, sp, #231	@ 0xe7
     d8c:	24da238e 	ldrbcs	r2, [sl], #910	@ 0x38e
     d90:	275f261f 	smmlacs	pc, pc, r6, r2	@ <UNPREDICTABLE>
     d94:	29cd2899 	stmibcs	sp, {r0, r3, r4, r7, fp, sp}^
     d98:	2c212afa 			@ <UNDEFINED> instruction: 0x2c212afa
     d9c:	2e5a2d41 	cdpcs	13, 5, cr2, cr10, cr1, {2}
     da0:	30762f6b 	rsbscc	r2, r6, fp, ror #30
     da4:	32743179 	rsbscc	r3, r4, #1073741854	@ 0x4000001e
     da8:	34533367 	ldrbcc	r3, [r3], #-871	@ 0xfffffc99
     dac:	36123536 			@ <UNDEFINED> instruction: 0x36123536
     db0:	37af36e5 	strcc	r3, [pc, r5, ror #13]!
     db4:	392a3871 	stmdbcc	sl!, {r0, r4, r5, r6, fp, ip, sp}
     db8:	3a8239da 	bcc	0xfe08f528
     dbc:	3bb63b20 	blcc	0xfed8fa44
     dc0:	3cc53c42 	stclcc	12, cr3, [r5], {66}	@ 0x42
     dc4:	3dae3d3e 	stccc	13, cr3, [lr, #248]!	@ 0xf8
     dc8:	3e713e14 	mrccc	14, 3, r3, cr1, cr4, {0}
     dcc:	3f0e3ec5 	svccc	0x000e3ec5
     dd0:	3f843f4e 	svccc	0x00843f4e
     dd4:	3fd33fb1 	svccc	0x00d33fb1
     dd8:	3ffb3fec 	svccc	0x00fb3fec
     ddc:	3ffb4000 	svccc	0x00fb4000
     de0:	3fd33fec 	svccc	0x00d33fec
     de4:	3f843fb1 	svccc	0x00843fb1
     de8:	3f0e3f4e 	svccc	0x000e3f4e
     dec:	3e713ec5 	cdpcc	14, 7, cr3, cr1, cr5, {6}
     df0:	3dae3e14 	stccc	14, cr3, [lr, #80]!	@ 0x50
     df4:	3cc53d3e 	stclcc	13, cr3, [r5], {62}	@ 0x3e
     df8:	3bb63c42 	blcc	0xfed8ff08
     dfc:	3a823b20 	bcc	0xfe08fa84
     e00:	392a39da 	stmdbcc	sl!, {r1, r3, r4, r6, r7, r8, fp, ip, sp}
     e04:	37af3871 			@ <UNDEFINED> instruction: 0x37af3871
     e08:	361236e5 	ldrcc	r3, [r2], -r5, ror #13
     e0c:	34533536 	ldrbcc	r3, [r3], #-1334	@ 0xfffffaca
     e10:	32743367 	rsbscc	r3, r4, #-1677721599	@ 0x9c000001
     e14:	30763179 	rsbscc	r3, r6, r9, ror r1
     e18:	2e5a2f6b 	cdpcs	15, 5, cr2, cr10, cr11, {3}
     e1c:	2c212d41 	stccs	13, cr2, [r1], #-260	@ 0xfffffefc
     e20:	29cd2afa 	stmibcs	sp, {r1, r3, r4, r5, r6, r7, r9, fp, sp}^
     e24:	275f2899 			@ <UNDEFINED> instruction: 0x275f2899
     e28:	24da261f 	ldrbcs	r2, [sl], #1567	@ 0x61f
     e2c:	223d238e 	eorscs	r2, sp, #939524098	@ 0x38000002
     e30:	1f8b20e7 	svcne	0x008b20e7
     e34:	1cc61e2b 	stclne	14, cr1, [r6], {43}	@ 0x2b
     e38:	19ef1b5d 	stmibne	pc!, {r0, r2, r3, r4, r6, r8, r9, fp, ip}^	@ <UNPREDICTABLE>
     e3c:	1708187d 	smlsdxne	r8, sp, r8, r1
     e40:	1413158f 	ldrne	r1, [r3], #-1423	@ 0xfffffa71
     e44:	11111294 			@ <UNDEFINED> instruction: 0x11111294
     e48:	0e050f8c 	cdpeq	15, 0, cr0, cr5, cr12, {4}
     e4c:	0af10c7c 	beq	0xffc44044
     e50:	07d50964 	ldrbeq	r0, [r5, r4, ror #18]
     e54:	04b50645 	ldrteq	r0, [r5], #1605	@ 0x645
     e58:	01920323 	orrseq	r0, r2, r3, lsr #6
     e5c:	fe6e0000 	cdp2	0, 6, cr0, cr14, cr0, {0}
     e60:	fb4bfcdd 	blx	0x13001de
     e64:	f82bf9bb 			@ <UNDEFINED> instruction: 0xf82bf9bb
     e68:	f50ff69c 			@ <UNDEFINED> instruction: 0xf50ff69c
     e6c:	f1fbf384 			@ <UNDEFINED> instruction: 0xf1fbf384
     e70:	eeeff074 	mcr	0, 7, pc, cr15, cr4, {3}	@ <UNPREDICTABLE>
     e74:	ebeded6c 	bl	0xffb7c42c
     e78:	e8f8ea71 	ldm	r8!, {r0, r4, r5, r6, r9, fp, sp, lr, pc}^
     e7c:	e611e783 	ldr	lr, [r1], -r3, lsl #15
     e80:	e33ae4a3 	teq	sl, #-1560281088	@ 0xa3000000
     e84:	e075e1d5 	ldrsbt	lr, [r5], #-21	@ 0xffffffeb
     e88:	ddc3df19 	stclle	15, cr13, [r3, #100]	@ 0x64
     e8c:	db26dc72 	blle	0x9b805c
     e90:	d8a1d9e1 	stmiale	r1!, {r0, r5, r6, r7, r8, fp, ip, lr, pc}
     e94:	d633d767 	ldrtle	sp, [r3], -r7, ror #14
     e98:	d3dfd506 	bicsle	sp, pc, #25165824	@ 0x1800000
     e9c:	d1a6d2bf 			@ <UNDEFINED> instruction: 0xd1a6d2bf
     ea0:	cf8ad095 	svcgt	0x008ad095
     ea4:	cd8cce87 	stcgt	14, cr12, [ip, #540]	@ 0x21c
     ea8:	cbadcc99 	blgt	0xfeb74114
     eac:	c9eecaca 	stmibgt	lr!, {r1, r3, r6, r7, r9, fp, lr, pc}^
     eb0:	c851c91b 	ldmdagt	r1, {r0, r1, r3, r4, r8, fp, lr, pc}^
     eb4:	c6d6c78f 	ldrbgt	ip, [r6], pc, lsl #15
     eb8:	c57ec626 	ldrbgt	ip, [lr, #-1574]!	@ 0xfffff9da
     ebc:	c44ac4e0 	strbgt	ip, [sl], #-1248	@ 0xfffffb20
     ec0:	c33bc3be 	teqgt	fp, #-134217726	@ 0xf8000002
     ec4:	c252c2c2 	subsgt	ip, r2, #536870924	@ 0x2000000c
     ec8:	c18fc1ec 	orrgt	ip, pc, ip, ror #3
     ecc:	c0f2c13b 	rscsgt	ip, r2, fp, lsr r1
     ed0:	c07cc0b2 	ldrhtgt	ip, [ip], #-2
     ed4:	c02dc04f 	eorgt	ip, sp, pc, asr #32
     ed8:	c005c014 	andgt	ip, r5, r4, lsl r0
     edc:	c005c000 	andgt	ip, r5, r0
     ee0:	c02dc014 	eorgt	ip, sp, r4, lsl r0
     ee4:	c07cc04f 	rsbsgt	ip, ip, pc, asr #32
     ee8:	c0f2c0b2 	ldrhtgt	ip, [r2], #2
     eec:	c18fc13b 	orrgt	ip, pc, fp, lsr r1	@ <UNPREDICTABLE>
     ef0:	c252c1ec 	subsgt	ip, r2, #236, 2	@ 0x3b
     ef4:	c33bc2c2 	teqgt	fp, #536870924	@ 0x2000000c
     ef8:	c44ac3be 	strbgt	ip, [sl], #-958	@ 0xfffffc42
     efc:	c57ec4e0 	ldrbgt	ip, [lr, #-1248]!	@ 0xfffffb20
     f00:	c6d6c626 	ldrbgt	ip, [r6], r6, lsr #12
     f04:	c851c78f 	ldmdagt	r1, {r0, r1, r2, r3, r7, r8, r9, sl, lr, pc}^
     f08:	c9eec91b 	stmibgt	lr!, {r0, r1, r3, r4, r8, fp, lr, pc}^
     f0c:	cbadcaca 	blgt	0xfeb73a3c
     f10:	cd8ccc99 	stcgt	12, cr12, [ip, #612]	@ 0x264
     f14:	cf8ace87 	svcgt	0x008ace87
     f18:	d1a6d095 			@ <UNDEFINED> instruction: 0xd1a6d095
     f1c:	d3dfd2bf 	bicsle	sp, pc, #-268435445	@ 0xf000000b
     f20:	d633d506 	ldrtle	sp, [r3], -r6, lsl #10
     f24:	d8a1d767 	stmiale	r1!, {r0, r1, r2, r5, r6, r8, r9, sl, ip, lr, pc}
     f28:	db26d9e1 	blle	0x9b76b4
     f2c:	ddc3dc72 	stclle	12, cr13, [r3, #456]	@ 0x1c8
     f30:	e075df19 	rsbs	sp, r5, r9, lsl pc
     f34:	e33ae1d5 	teq	sl, #1073741877	@ 0x40000035
     f38:	e611e4a3 	ldr	lr, [r1], -r3, lsr #9
     f3c:	e8f8e783 	ldm	r8!, {r0, r1, r7, r8, r9, sl, sp, lr, pc}^
     f40:	ebedea71 	bl	0xffb7b90c
     f44:	eeefed6c 	cdp	13, 14, cr14, cr15, cr12, {3}
     f48:	f1fbf074 			@ <UNDEFINED> instruction: 0xf1fbf074
     f4c:	f50ff384 			@ <UNDEFINED> instruction: 0xf50ff384
     f50:	f82bf69c 			@ <UNDEFINED> instruction: 0xf82bf69c
     f54:	fb4bf9bb 	blx	0x12ff64a
     f58:	fe6efcdd 	mcr2	12, 3, pc, cr14, cr13, {6}	@ <UNPREDICTABLE>
     f5c:	4718467b 			@ <UNDEFINED> instruction: 0x4718467b
     f60:	e92d4ff0 	push	{r4, r5, r6, r7, r8, r9, sl, fp, lr}
     f64:	e24dd008 	sub	sp, sp, #8
     f68:	e1d270b0 	ldrh	r7, [r2]
     f6c:	e1b0c007 	movs	ip, r7
     f70:	ebffff0b 	bl	0xba4
     f74:	0a000022 	beq	0x1004
     f78:	e5d26002 	ldrb	r6, [r2, #2]
     f7c:	e266a008 	rsb	sl, r6, #8
     f80:	e3a0e000 	mov	lr, #0
     f84:	e592b004 	ldr	fp, [r2, #4]
     f88:	e1a08fab 	lsr	r8, fp, #31
     f8c:	e592b004 	ldr	fp, [r2, #4]
     f90:	e1a0b08b 	lsl	fp, fp, #1
     f94:	e1a0b0ab 	lsr	fp, fp, #1
     f98:	e58db004 	str	fp, [sp, #4]
     f9c:	e5d22003 	ldrb	r2, [r2, #3]
     fa0:	e3a03000 	mov	r3, #0
     fa4:	e2577001 	subs	r7, r7, #1
     fa8:	ba000015 	blt	0x1004
     fac:	e3a0b0ff 	mov	fp, #255	@ 0xff
     fb0:	e1a05a5b 	asr	r5, fp, sl
     fb4:	e4d09001 	ldrb	r9, [r0], #1
     fb8:	e3a04000 	mov	r4, #0
     fbc:	e3540008 	cmp	r4, #8
     fc0:	aafffff7 	bge	0xfa4
     fc4:	e009b005 	and	fp, r9, r5
     fc8:	e1b0c43b 	lsrs	ip, fp, r4
     fcc:	03580000 	cmpeq	r8, #0
     fd0:	0a000001 	beq	0xfdc
     fd4:	e59db004 	ldr	fp, [sp, #4]
     fd8:	e08cc00b 	add	ip, ip, fp
     fdc:	e18ee31c 	orr	lr, lr, ip, lsl r3
     fe0:	e0833002 	add	r3, r3, r2
     fe4:	e3530020 	cmp	r3, #32
     fe8:	ba000002 	blt	0xff8
     fec:	e481e004 	str	lr, [r1], #4
     ff0:	e3a0e000 	mov	lr, #0
     ff4:	e3a03000 	mov	r3, #0
     ff8:	e1a05615 	lsl	r5, r5, r6
     ffc:	e0844006 	add	r4, r4, r6
    1000:	eaffffed 	b	0xfbc
    1004:	e28dd008 	add	sp, sp, #8
    1008:	e8bd4ff0 	pop	{r4, r5, r6, r7, r8, r9, sl, fp, lr}
    100c:	e12fff1e 	bx	lr
    1010:	4718467b 			@ <UNDEFINED> instruction: 0x4718467b
    1014:	e92d4ff0 	push	{r4, r5, r6, r7, r8, r9, sl, fp, lr}
    1018:	e24dd008 	sub	sp, sp, #8
    101c:	e3b0c402 	movs	ip, #33554432	@ 0x2000000
    1020:	ebfffedf 	bl	0xba4
    1024:	0a000030 	beq	0x10ec
    1028:	e2802004 	add	r2, r0, #4
    102c:	e2827001 	add	r7, r2, #1
    1030:	e5d0a000 	ldrb	sl, [r0]
    1034:	e20a400f 	and	r4, sl, #15
    1038:	e3a03000 	mov	r3, #0
    103c:	e3a0e000 	mov	lr, #0
    1040:	e204a007 	and	sl, r4, #7
    1044:	e28ab004 	add	fp, sl, #4
    1048:	e58db004 	str	fp, [sp, #4]
    104c:	e590a000 	ldr	sl, [r0]
    1050:	e1a0c42a 	lsr	ip, sl, #8
    1054:	e5d2a000 	ldrb	sl, [r2]
    1058:	e28aa001 	add	sl, sl, #1
    105c:	e082008a 	add	r0, r2, sl, lsl #1
    1060:	e1a02007 	mov	r2, r7
    1064:	e35c0000 	cmp	ip, #0
    1068:	da00001f 	ble	0x10ec
    106c:	e3a08020 	mov	r8, #32
    1070:	e4905004 	ldr	r5, [r0], #4
    1074:	e2588001 	subs	r8, r8, #1
    1078:	bafffff9 	blt	0x1064
    107c:	e3a0a001 	mov	sl, #1
    1080:	e00a9fa5 	and	r9, sl, r5, lsr #31
    1084:	e5d26000 	ldrb	r6, [r2]
    1088:	e1a06916 	lsl	r6, r6, r9
    108c:	e1a0a0a2 	lsr	sl, r2, #1
    1090:	e1a0a08a 	lsl	sl, sl, #1
    1094:	e5d2b000 	ldrb	fp, [r2]
    1098:	e20bb03f 	and	fp, fp, #63	@ 0x3f
    109c:	e28bb001 	add	fp, fp, #1
    10a0:	e08aa08b 	add	sl, sl, fp, lsl #1
    10a4:	e08a2009 	add	r2, sl, r9
    10a8:	e3160080 	tst	r6, #128	@ 0x80
    10ac:	0a00000a 	beq	0x10dc
    10b0:	e1a03433 	lsr	r3, r3, r4
    10b4:	e5d2a000 	ldrb	sl, [r2]
    10b8:	e264b020 	rsb	fp, r4, #32
    10bc:	e1833b1a 	orr	r3, r3, sl, lsl fp
    10c0:	e1a02007 	mov	r2, r7
    10c4:	e28ee001 	add	lr, lr, #1
    10c8:	e59db004 	ldr	fp, [sp, #4]
    10cc:	e15e000b 	cmp	lr, fp
    10d0:	04813004 	streq	r3, [r1], #4
    10d4:	024cc004 	subeq	ip, ip, #4
    10d8:	03a0e000 	moveq	lr, #0
    10dc:	e35c0000 	cmp	ip, #0
    10e0:	c1a05085 	lslgt	r5, r5, #1
    10e4:	caffffe2 	bgt	0x1074
    10e8:	eaffffdd 	b	0x1064
    10ec:	e28dd008 	add	sp, sp, #8
    10f0:	e8bd4ff0 	pop	{r4, r5, r6, r7, r8, r9, sl, fp, lr}
    10f4:	e12fff1e 	bx	lr
    10f8:	4718467b 			@ <UNDEFINED> instruction: 0x4718467b
    10fc:	e92d4070 	push	{r4, r5, r6, lr}
    1100:	e4905004 	ldr	r5, [r0], #4
    1104:	e1a02425 	lsr	r2, r5, #8
    1108:	e1b0c002 	movs	ip, r2
    110c:	ebfffea4 	bl	0xba4
    1110:	0a00001d 	beq	0x118c
    1114:	e3520000 	cmp	r2, #0
    1118:	da00001b 	ble	0x118c
    111c:	e4d0e001 	ldrb	lr, [r0], #1
    1120:	e3a04008 	mov	r4, #8
    1124:	e2544001 	subs	r4, r4, #1
    1128:	bafffff9 	blt	0x1114
    112c:	e31e0080 	tst	lr, #128	@ 0x80
    1130:	1a000003 	bne	0x1144
    1134:	e4d06001 	ldrb	r6, [r0], #1
    1138:	e4c16001 	strb	r6, [r1], #1
    113c:	e2422001 	sub	r2, r2, #1
    1140:	ea00000d 	b	0x117c
    1144:	e5d05000 	ldrb	r5, [r0]
    1148:	e3a06003 	mov	r6, #3
    114c:	e0863245 	add	r3, r6, r5, asr #4
    1150:	e4d06001 	ldrb	r6, [r0], #1
    1154:	e206500f 	and	r5, r6, #15
    1158:	e1a0c405 	lsl	ip, r5, #8
    115c:	e4d06001 	ldrb	r6, [r0], #1
    1160:	e186500c 	orr	r5, r6, ip
    1164:	e285c001 	add	ip, r5, #1
    1168:	e0422003 	sub	r2, r2, r3
    116c:	e751500c 	ldrb	r5, [r1, -ip]
    1170:	e4c15001 	strb	r5, [r1], #1
    1174:	e2533001 	subs	r3, r3, #1
    1178:	cafffffb 	bgt	0x116c
    117c:	e3520000 	cmp	r2, #0
    1180:	c1a0e08e 	lslgt	lr, lr, #1
    1184:	caffffe6 	bgt	0x1124
    1188:	eaffffe1 	b	0x1114
    118c:	e8bd4070 	pop	{r4, r5, r6, lr}
    1190:	e12fff1e 	bx	lr
    1194:	e92d47f0 	push	{r4, r5, r6, r7, r8, r9, sl, lr}
    1198:	e3a03000 	mov	r3, #0
    119c:	e4908004 	ldr	r8, [r0], #4
    11a0:	e1a0a428 	lsr	sl, r8, #8
    11a4:	e3a02000 	mov	r2, #0
    11a8:	e1b0c00a 	movs	ip, sl
    11ac:	ebfffe7c 	bl	0xba4
    11b0:	0a00002e 	beq	0x1270
    11b4:	e35a0000 	cmp	sl, #0
    11b8:	da00002c 	ble	0x1270
    11bc:	e4d06001 	ldrb	r6, [r0], #1
    11c0:	e3a07008 	mov	r7, #8
    11c4:	e2577001 	subs	r7, r7, #1
    11c8:	bafffff9 	blt	0x11b4
    11cc:	e3160080 	tst	r6, #128	@ 0x80
    11d0:	1a000006 	bne	0x11f0
    11d4:	e4d09001 	ldrb	r9, [r0], #1
    11d8:	e1833219 	orr	r3, r3, r9, lsl r2
    11dc:	e24aa001 	sub	sl, sl, #1
    11e0:	e2322008 	eors	r2, r2, #8
    11e4:	00c130b2 	strheq	r3, [r1], #2
    11e8:	03a03000 	moveq	r3, #0
    11ec:	ea00001b 	b	0x1260
    11f0:	e5d09000 	ldrb	r9, [r0]
    11f4:	e3a08003 	mov	r8, #3
    11f8:	e0885249 	add	r5, r8, r9, asr #4
    11fc:	e4d09001 	ldrb	r9, [r0], #1
    1200:	e209800f 	and	r8, r9, #15
    1204:	e1a04408 	lsl	r4, r8, #8
    1208:	e4d09001 	ldrb	r9, [r0], #1
    120c:	e1898004 	orr	r8, r9, r4
    1210:	e2884001 	add	r4, r8, #1
    1214:	e2628008 	rsb	r8, r2, #8
    1218:	e2049001 	and	r9, r4, #1
    121c:	e028e189 	eor	lr, r8, r9, lsl #3
    1220:	e04aa005 	sub	sl, sl, r5
    1224:	e22ee008 	eor	lr, lr, #8
    1228:	e2628008 	rsb	r8, r2, #8
    122c:	e08481a8 	add	r8, r4, r8, lsr #3
    1230:	e1a080a8 	lsr	r8, r8, #1
    1234:	e1a08088 	lsl	r8, r8, #1
    1238:	e11190b8 	ldrh	r9, [r1, -r8]
    123c:	e3a080ff 	mov	r8, #255	@ 0xff
    1240:	e0098e18 	and	r8, r9, r8, lsl lr
    1244:	e1a08e58 	asr	r8, r8, lr
    1248:	e1833218 	orr	r3, r3, r8, lsl r2
    124c:	e2322008 	eors	r2, r2, #8
    1250:	00c130b2 	strheq	r3, [r1], #2
    1254:	03a03000 	moveq	r3, #0
    1258:	e2555001 	subs	r5, r5, #1
    125c:	cafffff0 	bgt	0x1224
    1260:	e35a0000 	cmp	sl, #0
    1264:	c1a06086 	lslgt	r6, r6, #1
    1268:	caffffd5 	bgt	0x11c4
    126c:	eaffffd0 	b	0x11b4
    1270:	e8bd47f0 	pop	{r4, r5, r6, r7, r8, r9, sl, lr}
    1274:	e12fff1e 	bx	lr
    1278:	c808b5f0 	stmdagt	r8, {r4, r5, r6, r7, r8, sl, ip, sp, pc}
    127c:	1c3c0a1f 			@ <UNDEFINED> instruction: 0x1c3c0a1f
    1280:	fc8cf7ff 	stc2	7, cr15, [ip], {255}	@ 0xff
    1284:	2f00d019 	svccs	0x0000d019
    1288:	7804dd17 	stmdavc	r4, {r0, r1, r2, r4, r8, sl, fp, ip, lr, pc}
    128c:	06621c40 	strbteq	r1, [r2], -r0, asr #24
    1290:	0a230e52 	beq	0x8c4be0
    1294:	1c52d208 	lfmne	f5, 3, [r2], {8}
    1298:	78031abf 	stmdavc	r3, {r0, r1, r2, r3, r4, r5, r7, r9, fp, ip}
    129c:	1c40700b 	mcrrne	0, 0, r7, r0, cr11
    12a0:	1e521c49 	cdpne	12, 5, cr1, cr2, cr9, {2}
    12a4:	e7eedcf9 			@ <UNDEFINED> instruction: 0xe7eedcf9
    12a8:	1abf1cd2 	bne	0xfefc85f8
    12ac:	1c407805 	mcrrne	8, 0, r7, r0, cr5
    12b0:	1c49700d 	mcrrne	0, 0, r7, r9, cr13
    12b4:	dcfb1e52 	ldclle	14, cr1, [fp], #328	@ 0x148
    12b8:	bcf0e7e5 	ldcllt	7, cr14, [r0], #916	@ 0x394
    12bc:	4718bc08 	ldrmi	fp, [r8, -r8, lsl #24]
    12c0:	b083b5f0 	strdlt	fp, [r3], r0
    12c4:	c8082700 	stmdagt	r8, {r8, r9, sl, sp}
    12c8:	1c2c0a1d 			@ <UNDEFINED> instruction: 0x1c2c0a1d
    12cc:	fc66f7ff 	stc2l	7, cr15, [r6], #-1020	@ 0xfffffc04
    12d0:	2400d02b 	strcs	sp, [r0], #-43	@ 0xffffffd5
    12d4:	dd282d00 	stcle	13, cr2, [r8, #-0]
    12d8:	93017803 	movwls	r7, #6147	@ 0x1803
    12dc:	9b011c40 	blls	0x483e4
    12e0:	0e52065a 	mrceq	6, 2, r0, cr2, cr10, {2}
    12e4:	0a339e01 	beq	0xce8af0
    12e8:	1c52d20e 	lfmne	f5, 3, [r2], {14}
    12ec:	78061aad 	stmdavc	r6, {r0, r2, r3, r5, r7, r9, fp, ip}
    12f0:	433740a6 	teqmi	r7, #166	@ 0xa6
    12f4:	23081c40 	movwcs	r1, #35904	@ 0x8c40
    12f8:	d102405c 	qaddle	r4, ip, r2
    12fc:	1c89800f 	stcne	0, cr8, [r9], {15}
    1300:	1e522700 	cdpne	7, 5, cr2, cr2, cr0, {0}
    1304:	e7e5dcf3 			@ <UNDEFINED> instruction: 0xe7e5dcf3
    1308:	1aad1cd2 	bne	0xfeb48658
    130c:	96027806 	strls	r7, [r2], -r6, lsl #16
    1310:	9e021c40 	cdpls	12, 0, cr1, cr2, cr0, {2}
    1314:	433740a6 	teqmi	r7, #166	@ 0xa6
    1318:	405c2308 	subsmi	r2, ip, r8, lsl #6
    131c:	800fd102 	andhi	sp, pc, r2, lsl #2
    1320:	27001c89 	strcs	r1, [r0, -r9, lsl #25]
    1324:	dcf41e52 	ldclle	14, cr1, [r4], #328	@ 0x148
    1328:	b003e7d4 	ldrdlt	lr, [r3], -r4
    132c:	bc08bcf0 	stclt	12, cr11, [r8], {240}	@ 0xf0
    1330:	b5104718 	ldrlt	r4, [r0, #-1816]	@ 0xfffff8e8
    1334:	0a24c810 	beq	0x93337c
    1338:	fc30f7ff 	ldc2	7, cr15, [r0], #-1020	@ 0xfffffc04
    133c:	7802d00b 	stmdavc	r2, {r0, r1, r3, ip, lr, pc}
    1340:	700a1c40 	andvc	r1, sl, r0, asr #24
    1344:	1e641c49 	cdpne	12, 6, cr1, cr4, cr9, {2}
    1348:	7803dd05 	stmdavc	r3, {r0, r2, r8, sl, fp, ip, lr, pc}
    134c:	1c40189a 	mcrrne	8, 9, r1, r0, cr10
    1350:	1c49700a 	mcrrne	0, 0, r7, r9, cr10
    1354:	bc10e7f7 	ldclt	7, cr14, [r0], {247}	@ 0xf7
    1358:	4718bc08 	ldrmi	fp, [r8, -r8, lsl #24]
    135c:	c808b5f0 	stmdagt	r8, {r4, r5, r6, r7, r8, sl, ip, sp, pc}
    1360:	1c2c0a1d 			@ <UNDEFINED> instruction: 0x1c2c0a1d
    1364:	fc1af7ff 	ldc2	7, cr15, [sl], {255}	@ 0xff
    1368:	2408d013 	strcs	sp, [r8], #-19	@ 0xffffffed
    136c:	1c407807 	mcrrne	8, 0, r7, r0, cr7
    1370:	1e6d1c3a 	mcrne	12, 3, r1, cr13, cr10, {1}
    1374:	7803dd0d 	stmdavc	r3, {r0, r2, r3, r8, sl, fp, ip, lr, pc}
    1378:	1c4019df 	mcrrne	9, 13, r1, r0, cr15	@ <UNPREDICTABLE>
    137c:	0e36063e 	mrceq	6, 1, r0, cr6, cr14, {1}
    1380:	433240a6 	teqmi	r2, #166	@ 0xa6
    1384:	405c2308 	subsmi	r2, ip, r8, lsl #6
    1388:	800ad1f3 	strdhi	sp, [sl], -r3
    138c:	22001c89 	andcs	r1, r0, #35072	@ 0x8900
    1390:	bcf0e7ef 	ldcllt	7, cr14, [r0], #956	@ 0x3bc
    1394:	4718bc08 	ldrmi	fp, [r8, -r8, lsl #24]
    1398:	c810b510 	ldmdagt	r0, {r4, r8, sl, ip, sp, pc}
    139c:	f7ff0a24 			@ <UNDEFINED> instruction: 0xf7ff0a24
    13a0:	d00bfbfd 	strdle	pc, [fp], -sp
    13a4:	1c808802 	stcne	8, cr8, [r0], {2}
    13a8:	1c89800a 	stcne	0, cr8, [r9], {10}
    13ac:	dd051ea4 	stcle	14, cr1, [r5, #-656]	@ 0xfffffd70
    13b0:	189a8803 	ldmne	sl, {r0, r1, fp, pc}
    13b4:	800a1c80 	andhi	r1, sl, r0, lsl #25
    13b8:	e7f71c89 	ldrb	r1, [r7, r9, lsl #25]!
    13bc:	bc04bc10 	stclt	12, cr11, [r4], {16}
    13c0:	47084710 	smladmi	r8, r0, r7, r4
    13c4:	1c14b5b0 	cfldr32ne	mvfx11, [r4], {176}	@ 0xb0
    13c8:	1c071c0d 	stcne	12, cr1, [r7], {13}
    13cc:	db262a01 	blle	0x98bbd8
    13d0:	dd002c10 	stcle	12, cr2, [r0, #-64]	@ 0xffffffc0
    13d4:	1c382410 	cfldrsne	mvf2, [r8], #-64	@ 0xffffffc0
    13d8:	ffeaf000 			@ <UNDEFINED> instruction: 0xffeaf000
    13dc:	481162fd 	ldmdami	r1, {r0, r2, r3, r4, r5, r6, r7, r9, sp, lr}
    13e0:	6078723c 	rsbsvs	r7, r8, ip, lsr r2
    13e4:	e0042000 	and	r2, r4, r0
    13e8:	060c1e61 	streq	r1, [ip], -r1, ror #28
    13ec:	70280e24 	eorvc	r0, r8, r4, lsr #28
    13f0:	2c003550 	cfstr32cs	mvfx3, [r0], {80}	@ 0x50
    13f4:	490cdcf8 	stmdbmi	ip, {r3, r4, r5, r6, r7, sl, fp, ip, lr, pc}
    13f8:	6b094c0c 	blvs	0x254430
    13fc:	42a2680a 	adcmi	r6, r2, #655360	@ 0xa0000
    1400:	3201d10d 	andcc	sp, r1, #1073741827	@ 0x40000003
    1404:	6a0a600a 	bvs	0x299434
    1408:	d0032a00 	andle	r2, r3, r0, lsl #20
    140c:	6a4a63ba 	bvs	0x129a2fc
    1410:	620863fa 	andvs	r6, r8, #-402653181	@ 0xe8000003
    1414:	624f4806 	subvs	r4, pc, #393216	@ 0x60000
    1418:	600c6208 	andvs	r6, ip, r8, lsl #4
    141c:	bcb0637c 	ldclt	3, cr6, [r0], #496	@ 0x1f0
    1420:	4718bc08 	ldrmi	fp, [r8, -r8, lsl #24]
    1424:	80000000 	andhi	r0, r0, r0
    1428:	03007fc0 	movweq	r7, #4032	@ 0xfc0
    142c:	68736d53 	ldmdavs	r3!, {r0, r1, r4, r6, r8, sl, fp, sp, lr}^
    1430:	00002149 	andeq	r2, r0, r9, asr #2
    1434:	1c07b5f0 	cfstr32ne	mvfx11, [r7], {240}	@ 0xf0
    1438:	4b206b40 	blmi	0x81c140
    143c:	42981c0c 	addsmi	r1, r8, #12, 24	@ 0xc00
    1440:	3001d138 	andcc	sp, r1, r8, lsr r1
    1444:	21006378 	tstcs	r0, r8, ror r3
    1448:	603c6079 	eorsvs	r6, ip, r9, ror r0
    144c:	63386860 	teqvs	r8, #96, 16	@ 0x600000
    1450:	727878a0 	rsbsvc	r7, r8, #160, 16	@ 0xa00000
    1454:	83b82096 			@ <UNDEFINED> instruction: 0x83b82096
    1458:	20ff8438 	rscscs	r8, pc, r8, lsr r4	@ <UNPREDICTABLE>
    145c:	83f83001 	mvnshi	r3, #1
    1460:	84b98479 	ldrthi	r8, [r9], #1145	@ 0x479
    1464:	26006afd 			@ <UNDEFINED> instruction: 0x26006afd
    1468:	1c38e00b 	ldcne	0, cr14, [r8], #-44	@ 0xffffffd4
    146c:	f0001c29 			@ <UNDEFINED> instruction: 0xf0001c29
    1470:	20c0ffba 	strhcs	pc, [r0], #250	@ 0xfa	@ <UNPREDICTABLE>
    1474:	00b07028 	adcseq	r7, r0, r8, lsr #32
    1478:	68801900 	stmvs	r0, {r8, fp, ip}
    147c:	35506428 	ldrbcc	r6, [r0, #-1064]	@ 0xfffffbd8
    1480:	78203601 	stmdavc	r0!, {r0, r9, sl, ip, sp}
    1484:	da0b4286 	ble	0x2d1ea4
    1488:	42b07a38 	adcsmi	r7, r0, #56, 20	@ 0x38000
    148c:	e007dced 	and	sp, r7, sp, ror #25
    1490:	1c291c38 	stcne	12, cr1, [r9], #-224	@ 0xffffff20
    1494:	ffa7f000 			@ <UNDEFINED> instruction: 0xffa7f000
    1498:	70282000 	eorvc	r2, r8, r0
    149c:	36013550 			@ <UNDEFINED> instruction: 0x36013550
    14a0:	42b07a38 	adcsmi	r7, r0, #56, 20	@ 0x38000
    14a4:	78e0dcf4 	stmiavc	r0!, {r2, r4, r5, r6, r7, sl, fp, ip, lr, pc}^
    14a8:	d3010a01 	movwle	r0, #6657	@ 0x1a01
    14ac:	f976f000 			@ <UNDEFINED> instruction: 0xf976f000
    14b0:	63784802 	cmnvs	r8, #131072	@ 0x20000
    14b4:	bc08bcf0 	stclt	12, cr11, [r8], {240}	@ 0xf0
    14b8:	00004718 	andeq	r4, r0, r8, lsl r7
    14bc:	68736d53 	ldmdavs	r3!, {r0, r1, r4, r6, r8, sl, fp, sp, lr}^
    14c0:	1c07b5f0 	cfstr32ne	mvfx11, [r7], {240}	@ 0xf0
    14c4:	4e0c6b40 	vmlsmi.f64	d6, d12, d0
    14c8:	d11142b0 			@ <UNDEFINED> instruction: 0xd11142b0
    14cc:	63783001 	cmnvs	r8, #1
    14d0:	07f36878 			@ <UNDEFINED> instruction: 0x07f36878
    14d4:	60784318 	rsbsvs	r4, r8, r8, lsl r3
    14d8:	6afc7a3d 	bvs	0xfff1fdd4
    14dc:	1c38e005 	ldcne	0, cr14, [r8], #-20	@ 0xffffffec
    14e0:	f0001c21 			@ <UNDEFINED> instruction: 0xf0001c21
    14e4:	3450ff80 	ldrbcc	pc, [r0], #-3968	@ 0xfffff080	@ <UNPREDICTABLE>
    14e8:	2d003d01 	stccs	13, cr3, [r0, #-4]
    14ec:	637edcf7 	cmnvs	lr, #63232	@ 0xf700
    14f0:	bc08bcf0 	stclt	12, cr11, [r8], {240}	@ 0xf0
    14f4:	00004718 	andeq	r4, r0, r8, lsl r7
    14f8:	68736d53 	ldmdavs	r3!, {r0, r1, r4, r6, r8, sl, fp, sp, lr}^
    14fc:	49046b42 	stmdbmi	r4, {r1, r6, r8, r9, fp, sp, lr}
    1500:	d104428a 	smlabble	r4, sl, r2, r4
    1504:	63416842 	movtvs	r6, #6210	@ 0x1842
    1508:	08520052 	ldmdaeq	r2, {r1, r4, r6}^
    150c:	47706042 	ldrbmi	r6, [r0, -r2, asr #32]!
    1510:	68736d53 	ldmdavs	r3!, {r0, r1, r4, r6, r8, sl, fp, sp, lr}^
    1514:	6b47b480 	blvs	0x11ee71c
    1518:	42974a05 	addsmi	r4, r7, #20480	@ 0x5000
    151c:	84c1d105 	strbhi	sp, [r1], #261	@ 0x105
    1520:	21ff8481 	mvnscs	r8, r1, lsl #9
    1524:	85013101 	strhi	r3, [r1, #-257]	@ 0xfffffeff
    1528:	bc806342 	stclt	3, cr6, [r0], {66}	@ 0x42
    152c:	00004770 	andeq	r4, r0, r0, ror r7
    1530:	68736d53 	ldmdavs	r3!, {r0, r1, r4, r6, r8, sl, fp, sp, lr}^
    1534:	1c07b5f0 	cfstr32ne	mvfx11, [r7], {240}	@ 0xf0
    1538:	28008c80 	stmdacs	r0, {r7, sl, fp, pc}
    153c:	8cf9d019 	ldclhi	0, cr13, [r9], #100	@ 0x64
    1540:	04093901 	streq	r3, [r9], #-2305	@ 0xfffff6ff
    1544:	84f90c09 	ldrbthi	r0, [r9], #3081	@ 0xc09
    1548:	8d39d113 	ldfhid	f5, [r9, #-76]!	@ 0xffffffb4
    154c:	85393910 	ldrhi	r3, [r9, #-2320]!	@ 0xfffff6f0
    1550:	14090409 	strne	r0, [r9], #-1033	@ 0xfffffbf7
    1554:	dc0f2900 			@ <UNDEFINED> instruction: 0xdc0f2900
    1558:	6afc7a3d 	bvs	0xfff1fe54
    155c:	e0062600 	and	r2, r6, r0, lsl #12
    1560:	1c211c38 	stcne	12, cr1, [r1], #-224	@ 0xffffff20
    1564:	ff3ff000 			@ <UNDEFINED> instruction: 0xff3ff000
    1568:	34507026 	ldrbcc	r7, [r0], #-38	@ 0xffffffda
    156c:	2d003d01 	stccs	13, cr3, [r0, #-4]
    1570:	bcf0dcf6 	ldcllt	12, cr13, [r0], #984	@ 0x3d8
    1574:	4718bc08 	ldrmi	fp, [r8, -r8, lsl #24]
    1578:	7a3984f8 	bvc	0xe62960
    157c:	e00a6af8 	strd	r6, [sl], -r8
    1580:	0a137802 	beq	0x4df590
    1584:	8d3bd305 	ldchi	3, cr13, [fp, #-20]!	@ 0xffffffec
    1588:	74c3089b 	strbvc	r0, [r3], #2203	@ 0x89b
    158c:	431a2303 	tstmi	sl, #201326592	@ 0xc000000
    1590:	30507002 	subscc	r7, r0, r2
    1594:	29003901 	stmdbcs	r0, {r0, r8, fp, ip, sp}
    1598:	e7eadcf2 			@ <UNDEFINED> instruction: 0xe7eadcf2
    159c:	780db5b0 	stmdavc	sp, {r4, r5, r7, r8, sl, ip, sp, pc}
    15a0:	08691c0f 	stmdaeq	r9!, {r0, r1, r2, r3, sl, fp, ip}^
    15a4:	7cb9d330 	ldcvc	3, cr13, [r9], #192	@ 0xc0
    15a8:	7e3c7cfa 	mrcvc	12, 1, r7, cr12, cr10, {7}
    15ac:	094a4351 	stmdbeq	sl, {r0, r4, r6, r8, r9, lr}^
    15b0:	d1022c01 	tstle	r2, r1, lsl #24
    15b4:	56f92316 	usatpl	r2, #25, r6, lsl #6
    15b8:	2314188a 	tstcs	r4, #9043968	@ 0x8a0000
    15bc:	004956f9 	strdeq	r5, [r9], #-105	@ 0xffffff97
    15c0:	56fb2315 	usatpl	r2, #27, r5, lsl #6
    15c4:	2c0218c9 	stccs	8, cr1, [r2], {201}	@ 0xc9
    15c8:	2316d102 	tstcs	r6, #-2147483648	@ 0x80000000
    15cc:	185956fb 	ldmdane	r9, {r0, r1, r3, r4, r5, r6, r7, r9, sl, ip, lr}^
    15d0:	42d92380 	sbcsmi	r2, r9, #128, 6
    15d4:	4259da01 	subsmi	sp, r9, #4096	@ 0x1000
    15d8:	297fe002 	ldmdbcs	pc!, {r1, sp, lr, pc}^	@ <UNPREDICTABLE>
    15dc:	217fdd00 	cmncs	pc, r0, lsl #26
    15e0:	33791dcb 	cmncc	r9, #12992	@ 0x32c0
    15e4:	0a1b4353 	beq	0x6d2338
    15e8:	0e1b061b 	mrceq	6, 0, r0, cr11, cr11, {0}
    15ec:	d9002bff 	stmdble	r0, {r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, fp, sp}
    15f0:	743b23ff 	ldrtvc	r2, [fp], #-1023	@ 0xfffffc01
    15f4:	1a59237f 	bne	0x164a3f8
    15f8:	0a094351 	beq	0x252344
    15fc:	0e090609 	cfmadd32eq	mvax0, mvfx0, mvfx9, mvfx9
    1600:	d90029ff 	stmdble	r0, {r0, r1, r2, r3, r4, r5, r6, r7, r8, fp, sp}
    1604:	747921ff 	ldrbtvc	r2, [r9], #-511	@ 0xfffffe01
    1608:	d31c08e9 	tstle	ip, #15269888	@ 0xe90000
    160c:	56f9230e 	ldrbtpl	r2, [r9], lr, lsl #6
    1610:	43517bfa 	cmpmi	r1, #256000	@ 0x3e800
    1614:	230c0089 	movwcs	r0, #49289	@ 0xc089
    1618:	009256fa 			@ <UNDEFINED> instruction: 0x009256fa
    161c:	230a1889 	movwcs	r1, #43145	@ 0xa889
    1620:	021256fa 	andseq	r5, r2, #262144000	@ 0xfa00000
    1624:	230b1889 	movwcs	r1, #47241	@ 0xb889
    1628:	021256fa 	andseq	r5, r2, #262144000	@ 0xfa00000
    162c:	7b7a1889 	blvc	0x1e87858
    1630:	7e3a1889 	cdpvc	8, 3, cr1, cr10, cr9, {4}
    1634:	d1032a00 	tstle	r3, r0, lsl #20
    1638:	56fa2316 	usatpl	r2, #26, r6, lsl #6
    163c:	18510112 	ldmdane	r1, {r1, r4, r8}^
    1640:	723a120a 	eorsvc	r1, sl, #-1610612736	@ 0xa0000000
    1644:	4a067279 	bmi	0x19e030
    1648:	6b121c39 	blvs	0x488734
    164c:	f7ff6bd2 			@ <UNDEFINED> instruction: 0xf7ff6bd2
    1650:	7838feb7 	ldmdavc	r8!, {r0, r1, r2, r4, r5, r7, r9, sl, fp, ip, sp, lr, pc}
    1654:	43982305 	orrsmi	r2, r8, #335544320	@ 0x14000000
    1658:	bcb07038 	ldclt	0, cr7, [r0], #224	@ 0xe0
    165c:	4718bc08 	ldrmi	fp, [r8, -r8, lsl #24]
    1660:	03007fc0 	movweq	r7, #4032	@ 0xfc0
    1664:	1c07b588 	cfstr32ne	mvfx11, [r7], {136}	@ 0x88
    1668:	2000491c 	andcs	r4, r0, ip, lsl r9
    166c:	824880c8 	subhi	r8, r8, #200	@ 0xc8
    1670:	228f481b 	addcs	r4, pc, #1769472	@ 0x1b0000
    1674:	4a1b8082 	bmi	0x6e1884
    1678:	7a428042 	bvc	0x10a1788
    167c:	0e920692 	mrceq	6, 4, r0, cr2, cr2, {4}
    1680:	431a2340 	tstmi	sl, #64, 6
    1684:	011b2335 	tsteq	fp, r5, lsr r3
    1688:	18fa7242 	ldmne	sl!, {r1, r6, r9, ip, sp, lr}^
    168c:	481663c2 	ldmdami	r6, {r1, r6, r7, r8, r9, sp, lr}
    1690:	01db2313 	bicseq	r2, fp, r3, lsl r3
    1694:	18f86008 	ldmne	r8!, {r3, sp, lr}^
    1698:	48146088 	ldmdami	r4, {r3, r7, sp, lr}
    169c:	60c84a15 	sbcvs	r4, r8, r5, lsl sl
    16a0:	63074813 	movwvs	r4, #30739	@ 0x7813
    16a4:	90002000 	andls	r2, r0, r0
    16a8:	1c394668 	ldcne	6, cr4, [r9], #-416	@ 0xfffffe60
    16ac:	fa4ef7ff 	blx	0x13bf6b0
    16b0:	71b82008 			@ <UNDEFINED> instruction: 0x71b82008
    16b4:	71f8200f 	mvnsvc	r2, pc
    16b8:	63b8480f 			@ <UNDEFINED> instruction: 0x63b8480f
    16bc:	62b8480f 	adcsvs	r4, r8, #983040	@ 0xf0000
    16c0:	633862f8 	teqvs	r8, #248, 4	@ 0x8000000f
    16c4:	480e63f8 	stmdami	lr, {r3, r4, r5, r6, r7, r8, r9, sp, lr}
    16c8:	20016378 	andcs	r6, r1, r8, ror r3
    16cc:	f0000480 			@ <UNDEFINED> instruction: 0xf0000480
    16d0:	480cf81c 	stmdami	ip, {r2, r3, r4, fp, ip, sp, lr, pc}
    16d4:	bc886038 	stclt	0, cr6, [r8], {56}	@ 0x38
    16d8:	4718bc08 	ldrmi	fp, [r8, -r8, lsl #24]
    16dc:	040000c0 	streq	r0, [r0], #-192	@ 0xffffff40
    16e0:	04000080 	streq	r0, [r0], #-128	@ 0xffffff80
    16e4:	0000a90e 	andeq	sl, r0, lr, lsl #18
    16e8:	040000a0 	streq	r0, [r0], #-160	@ 0xffffff60
    16ec:	040000a4 	streq	r0, [r0], #-164	@ 0xffffff5c
    16f0:	03007fc0 	movweq	r7, #4032	@ 0xfc0
    16f4:	050003ec 	streq	r0, [r0, #-1004]	@ 0xfffffc14
    16f8:	00002425 	andeq	r2, r0, r5, lsr #8
    16fc:	00001709 	andeq	r1, r0, r9, lsl #14
    1700:	00003738 	andeq	r3, r0, r8, lsr r7
    1704:	68736d53 	ldmdavs	r3!, {r0, r1, r4, r6, r8, sl, fp, sp, lr}^
    1708:	b5904770 	ldrlt	r4, [r0, #1904]	@ 0x770
    170c:	230f491d 	movwcs	r4, #63773	@ 0xf91d
    1710:	4018041b 	andsmi	r0, r8, fp, lsl r4
    1714:	0c006b0f 			@ <UNDEFINED> instruction: 0x0c006b0f
    1718:	491b7238 	ldmdbmi	fp, {r3, r4, r5, r9, ip, sp, lr}
    171c:	18400040 	stmdane	r0, {r6}^
    1720:	8bc03820 	blhi	0xff00f7a8
    1724:	01092163 	tsteq	r9, r3, ror #2
    1728:	61381c04 	teqvs	r8, r4, lsl #24
    172c:	fff8f001 			@ <UNDEFINED> instruction: 0xfff8f001
    1730:	481672f8 	ldmdami	r6, {r3, r4, r5, r6, r7, r9, ip, sp, lr}
    1734:	43604b16 	cmnmi	r0, #22528	@ 0x5800
    1738:	005818c1 	subseq	r1, r8, r1, asr #17
    173c:	fff0f001 			@ <UNDEFINED> instruction: 0xfff0f001
    1740:	06092101 	streq	r2, [r9], -r1, lsl #2
    1744:	f0016178 			@ <UNDEFINED> instruction: 0xf0016178
    1748:	3001ffeb 	andcc	pc, r1, fp, ror #31
    174c:	61b81040 			@ <UNDEFINED> instruction: 0x61b81040
    1750:	20004c10 	andcs	r4, r0, r0, lsl ip
    1754:	69388060 	ldmdbvs	r8!, {r5, r6, pc}
    1758:	f001490f 			@ <UNDEFINED> instruction: 0xf001490f
    175c:	2101ffe1 	smlattcs	r1, r1, pc, pc	@ <UNPREDICTABLE>
    1760:	1a080409 	bne	0x20278c
    1764:	f0008020 			@ <UNDEFINED> instruction: 0xf0008020
    1768:	2001f8af 	andcs	pc, r1, pc, lsr #17
    176c:	79810680 	stmibvc	r1, {r7, r9, sl}
    1770:	d0fc299f 	smlalsle	r2, ip, pc, r9	@ <UNPREDICTABLE>
    1774:	299f7981 	ldmibcs	pc, {r0, r7, r8, fp, ip, sp, lr}	@ <UNPREDICTABLE>
    1778:	2080d1fc 	strdcs	sp, [r0], ip
    177c:	bc908060 	ldclt	0, cr8, [r0], {96}	@ 0x60
    1780:	4718bc08 	ldrmi	fp, [r8, -r8, lsl #24]
    1784:	03007fc0 	movweq	r7, #4032	@ 0xfc0
    1788:	000031e8 	andeq	r3, r0, r8, ror #3
    178c:	00091d1b 	andeq	r1, r9, fp, lsl sp
    1790:	00001388 	andeq	r1, r0, r8, lsl #7
    1794:	04000100 	streq	r0, [r0], #-256	@ 0xffffff00
    1798:	00044940 	andeq	r4, r4, r0, asr #18
    179c:	491eb5b0 	ldmdbmi	lr, {r4, r5, r7, r8, sl, ip, sp, pc}
    17a0:	6b0f4d1e 	blvs	0x3d4c20
    17a4:	42a96839 	adcmi	r6, r9, #3735552	@ 0x390000
    17a8:	3101d133 	tstcc	r1, r3, lsr r1
    17ac:	06016039 			@ <UNDEFINED> instruction: 0x06016039
    17b0:	d0020e09 	andle	r0, r2, r9, lsl #28
    17b4:	0e490649 	cdpeq	6, 4, cr0, cr9, cr9, {2}
    17b8:	210f7179 	tstcs	pc, r9, ror r1	@ <UNPREDICTABLE>
    17bc:	40010209 	andmi	r0, r1, r9, lsl #4
    17c0:	0a09d009 	beq	0x2757ec
    17c4:	210c71b9 			@ <UNDEFINED> instruction: 0x210c71b9
    17c8:	1dfa2300 	ldclne	3, cr2, [sl]
    17cc:	70133249 	andsvc	r3, r3, r9, asr #4
    17d0:	39013240 	stmdbcc	r1, {r6, r9, ip, sp}
    17d4:	210fd1fb 	strdcs	sp, [pc, -fp]
    17d8:	40010309 	andmi	r0, r1, r9, lsl #6
    17dc:	0b09d001 	bleq	0x2757e8
    17e0:	210b71f9 	strdcs	r7, [fp, -r9]
    17e4:	40010509 	andmi	r0, r1, r9, lsl #10
    17e8:	2303d009 	movwcs	sp, #12297	@ 0x3009
    17ec:	4a0c051b 	bmi	0x302c60
    17f0:	7a534019 	bvc	0x14d185c
    17f4:	069b0b89 	ldreq	r0, [fp], r9, lsl #23
    17f8:	43190e9b 	tstmi	r9, #2480	@ 0x9b0
    17fc:	240f7251 	strcs	r7, [pc], #-593	@ 0x1804
    1800:	40040424 	andmi	r0, r4, r4, lsr #8
    1804:	f000d004 			@ <UNDEFINED> instruction: 0xf000d004
    1808:	1c20f837 	stcne	8, cr15, [r0], #-220	@ 0xffffff24
    180c:	ff7df7ff 			@ <UNDEFINED> instruction: 0xff7df7ff
    1810:	bcb0603d 	ldclt	0, cr6, [r0], #244	@ 0xf4
    1814:	4718bc08 	ldrmi	fp, [r8, -r8, lsl #24]
    1818:	03007fc0 	movweq	r7, #4032	@ 0xfc0
    181c:	68736d53 	ldmdavs	r3!, {r0, r1, r4, r6, r8, sl, fp, sp, lr}^
    1820:	04000080 	streq	r0, [r0], #-128	@ 0xffffff80
    1824:	4812b5f0 	ldmdami	r2, {r4, r5, r6, r7, r8, sl, ip, sp, pc}
    1828:	6b074e12 	blvs	0x1d5078
    182c:	42b06838 	adcsmi	r6, r0, #56, 16	@ 0x380000
    1830:	3001d11a 	andcc	sp, r1, sl, lsl r1
    1834:	1df86038 	ldclne	0, cr6, [r8, #224]!	@ 0xe0
    1838:	3049210c 	subcc	r2, r9, ip, lsl #2
    183c:	70022200 	andvc	r2, r2, r0, lsl #4
    1840:	39013040 	stmdbcc	r1, {r6, ip, sp}
    1844:	dcf92900 			@ <UNDEFINED> instruction: 0xdcf92900
    1848:	2d0069fd 	vstrcs.16	s12, [r0, #-506]	@ 0xfffffe06	@ <UNPREDICTABLE>
    184c:	2401d00b 	strcs	sp, [r1], #-11
    1850:	0e000620 	cfmadd32eq	mvax1, mvfx0, mvfx0, mvfx0
    1854:	f7ff6af9 			@ <UNDEFINED> instruction: 0xf7ff6af9
    1858:	3401fdb4 	strcc	pc, [r1], #-3508	@ 0xfffff24c
    185c:	2c043540 	cfstr32cs	mvfx3, [r4], {64}	@ 0x40
    1860:	2200ddf6 	andcs	sp, r0, #15744	@ 0x3d80
    1864:	603e702a 	eorsvs	r7, lr, sl, lsr #32
    1868:	bc08bcf0 	stclt	12, cr11, [r8], {240}	@ 0xf0
    186c:	00004718 	andeq	r4, r0, r8, lsl r7
    1870:	03007fc0 	movweq	r7, #4032	@ 0xfc0
    1874:	68736d53 	ldmdavs	r3!, {r0, r1, r4, r6, r8, sl, fp, sp, lr}^
    1878:	480fb588 	stmdami	pc, {r3, r7, r8, sl, ip, sp, pc}	@ <UNPREDICTABLE>
    187c:	6b074b0f 	blvs	0x1d44c0
    1880:	42986838 	addsmi	r6, r8, #56, 16	@ 0x380000
    1884:	3301d314 	movwcc	sp, #4884	@ 0x1314
    1888:	d8114298 	ldmdale	r1, {r3, r4, r7, r9, lr}
    188c:	60383001 	eorsvs	r3, r8, r1
    1890:	490b2000 	stmdbmi	fp, {sp}
    1894:	80c82335 	sbchi	r2, r8, r5, lsr r3
    1898:	71388248 	teqvc	r8, r8, asr #4
    189c:	18f9011b 	ldmne	r9!, {r0, r1, r3, r4, r8}^
    18a0:	46689000 	strbtmi	r9, [r8], -r0
    18a4:	f7ff4a07 			@ <UNDEFINED> instruction: 0xf7ff4a07
    18a8:	6838f951 	ldmdavs	r8!, {r0, r4, r6, r8, fp, ip, sp, lr, pc}
    18ac:	60383801 	eorsvs	r3, r8, r1, lsl #16
    18b0:	bc08bc88 	stclt	12, cr11, [r8], {136}	@ 0x88
    18b4:	00004718 	andeq	r4, r0, r8, lsl r7
    18b8:	03007fc0 	movweq	r7, #4032	@ 0xfc0
    18bc:	68736d53 	ldmdavs	r3!, {r0, r1, r4, r6, r8, sl, fp, sp, lr}^
    18c0:	040000c0 	streq	r0, [r0], #-192	@ 0xffffff40
    18c4:	05000318 	streq	r0, [r0, #-792]	@ 0xfffffce8
    18c8:	4802215b 	stmdami	r2, {r0, r1, r3, r4, r6, r8, sp}
    18cc:	80c10249 	sbchi	r0, r1, r9, asr #4
    18d0:	47708241 	ldrbmi	r8, [r0, -r1, asr #4]!
    18d4:	040000c0 	streq	r0, [r0], #-192	@ 0xffffff40
    18d8:	0612b5f0 			@ <UNDEFINED> instruction: 0x0612b5f0
    18dc:	29b21c07 	ldmibcs	r2!, {r0, r1, r2, sl, fp, ip}
    18e0:	4a0fdd01 	bmi	0x3f8cec
    18e4:	480f21b2 	stmdami	pc, {r1, r4, r5, r7, r8, sp}	@ <UNPREDICTABLE>
    18e8:	071c5c43 	ldreq	r5, [ip, -r3, asr #24]
    18ec:	00a40f24 	adceq	r0, r4, r4, lsr #30
    18f0:	35ad1dc5 	strcc	r1, [sp, #3525]!	@ 0xdc5
    18f4:	091e592c 	ldmdbeq	lr, {r2, r3, r5, r8, fp, ip, lr}
    18f8:	184040f4 	stmdane	r0, {r2, r4, r5, r6, r7, lr}^
    18fc:	07017840 	streq	r7, [r1, -r0, asr #16]
    1900:	00890f09 	addeq	r0, r9, r9, lsl #30
    1904:	09005869 	stmdbeq	r0, {r0, r3, r5, r6, fp, ip, lr}
    1908:	1b0840c1 	blne	0x211c14
    190c:	f0001c11 			@ <UNDEFINED> instruction: 0xf0001c11
    1910:	1901fa51 	stmdbne	r1, {r0, r4, r6, r9, fp, ip, sp, lr, pc}
    1914:	f0006878 			@ <UNDEFINED> instruction: 0xf0006878
    1918:	bcf0fa4d 	vldmialt	r0!, {s31-s107}
    191c:	4718bc08 	ldrmi	fp, [r8, -r8, lsl #24]
    1920:	ff000000 			@ <UNDEFINED> instruction: 0xff000000
    1924:	00003104 	andeq	r3, r0, r4, lsl #2

		.thumb
    1928:	b5f0		push	{r4-r7, lr}
    192a:	b08d		sub	sp, #52
    192c:	2100		mov	r1, #0
    192e:	2000		mov	r0, #0
    1930:	9005		str	r0, [sp, #20]
    1932:	2010		mov	r0, #16

    1934:	43cf9003 	bicmi	r9, pc, #3
    1938:	910420ff 	strdls	r2, [r4, -pc]
    193c:	f7ff9100 			@ <UNDEFINED> instruction: 0xf7ff9100
    1940:	48faf840 	ldmmi	sl!, {r6, fp, ip, sp, lr, pc}^
    1944:	70052501 	andvc	r2, r5, r1, lsl #10
    1948:	f7fe2001 			@ <UNDEFINED> instruction: 0xf7fe2001
    194c:	4ef8ff59 	mrcmi	15, 7, APSR_nzcv, cr8, cr9, {2}
    1950:	05c12008 	strbeq	r2, [r1, #8]
    1954:	80888035 	addhi	r8, r8, r5, lsr r0
    1958:	0bc088b0 	bleq	0xff023c20
    195c:	f7fed001 			@ <UNDEFINED> instruction: 0xf7fed001
    1960:	f7fefc91 			@ <UNDEFINED> instruction: 0xf7fefc91
    1964:	20efff87 	rsccs	pc, pc, r7, lsl #31
    1968:	210101c0 	smlabtcs	r1, r0, r1, r0
    196c:	81c80689 	bichi	r0, r8, r9, lsl #13
    1970:	90022054 	andls	r2, r2, r4, asr r0
    1974:	90012076 	andls	r2, r1, r6, ror r0
    1978:	02802015 	addeq	r2, r0, #21
    197c:	203b6388 	eorscs	r6, fp, r8, lsl #7
    1980:	63c80240 	bicvs	r0, r8, #64, 4
    1984:	48eb49ec 	stmiami	fp!, {r2, r3, r5, r6, r7, r8, fp, lr}^
    1988:	f7fe6108 			@ <UNDEFINED> instruction: 0xf7fe6108
    198c:	f001fecc 			@ <UNDEFINED> instruction: 0xf001fecc
    1990:	48eaf9eb 	stmiami	sl!, {r0, r1, r3, r5, r6, r7, r8, fp, ip, sp, lr, pc}^
    1994:	fe66f7ff 	mcr2	7, 3, pc, cr6, cr15, {7}	@ <UNPREDICTABLE>
    1998:	f7ff48e9 			@ <UNDEFINED> instruction: 0xf7ff48e9
    199c:	49e9feff 	stmibmi	r9!, {r0, r1, r2, r3, r4, r5, r6, r7, r9, sl, fp, ip, sp, lr, pc}^
    19a0:	220648e9 	andcs	r4, r6, #15269888	@ 0xe90000
    19a4:	fd0ef7ff 	stc2	7, cr15, [lr, #-1020]	@ 0xfffffc04
    19a8:	48e949e8 	stmiami	r9!, {r3, r5, r6, r7, r8, fp, lr}^
    19ac:	f7ff2206 			@ <UNDEFINED> instruction: 0xf7ff2206
    19b0:	e156fd09 	cmp	r6, r9, lsl #26	@ <UNPREDICTABLE>
    19b4:	e0a82507 	adc	r2, r8, r7, lsl #10
    19b8:	1b422006 	blne	0x10899d8
    19bc:	18800090 	stmne	r0, {r4, r7}
    19c0:	42b83008 	adcsmi	r3, r8, #8
    19c4:	dc04920c 	sfmle	f1, 1, [r4], {12}
    19c8:	00a94be2 	adceq	r4, r9, r2, ror #23
    19cc:	3201585a 	andcc	r5, r1, #5898240	@ 0x5a0000
    19d0:	4be0505a 	blmi	0xff815b40
    19d4:	585a00a9 	ldmdapl	sl, {r0, r3, r5, r7}^
    19d8:	01294bdf 	ldrdeq	r4, [r9, -pc]!
    19dc:	00ec18c9 	rsceq	r1, ip, r9, asr #17
    19e0:	92079106 	andls	r9, r7, #-2147483647	@ 0x80000001
    19e4:	2307688e 	movwcs	r6, #30862	@ 0x788e
    19e8:	940b061b 	strls	r0, [fp], #-1563	@ 0xfffff9e5
    19ec:	2e0018e4 	cdpcs	8, 0, cr1, cr0, cr4, {7}
    19f0:	42b8da67 	adcsmi	sp, r8, #421888	@ 0x67000
    19f4:	3602dc01 	strcc	sp, [r2], -r1, lsl #24
    19f8:	0428608e 	strteq	r6, [r8], #-142	@ 0xffffff72
    19fc:	99061400 	stmdbls	r6, {sl, ip}
    1a00:	fe9af7fe 	mrc2	7, 4, pc, cr10, cr14, {7}
    1a04:	43682014 	cmnmi	r8, #20
    1a08:	184149d4 	stmdane	r1, {r2, r4, r6, r7, r8, fp, lr}^
    1a0c:	9109910a 	tstls	r9, sl, lsl #2
    1a10:	f7fe9806 			@ <UNDEFINED> instruction: 0xf7fe9806
    1a14:	0168fea9 	msreq	(UNDEF: 104), r9
    1a18:	22014bd1 	andcs	r4, r1, #214016	@ 0x34400
    1a1c:	980918c1 	stmdals	r9, {r0, r6, r7, fp, ip}
    1a20:	300c2308 	andcc	r2, ip, r8, lsl #6
    1a24:	fe84f001 	cdp2	0, 8, cr15, cr4, cr1, {0}
    1a28:	42de2360 	sbcsmi	r2, lr, #96, 6	@ 0x80000001
    1a2c:	6820dd3e 	stmdavs	r0!, {r1, r2, r3, r4, r5, r8, sl, fp, ip, lr, pc}
    1a30:	430200da 	movwmi	r0, #8410	@ 0x20da
    1a34:	43c0203f 	bicmi	r2, r0, #63	@ 0x3f
    1a38:	42861c01 	addmi	r1, r6, #256	@ 0x100
    1a3c:	da016022 	ble	0x59acc
    1a40:	da1f2d04 	ble	0x7cce58
    1a44:	42de234b 	sbcsmi	r2, lr, #738197505	@ 0x2c000001
    1a48:	2e00db1c 	vmovcs.32	d0[0], sp
    1a4c:	8820da13 	stmdahi	r0!, {r0, r1, r4, r9, fp, ip, lr, pc}
    1a50:	03db2301 	bicseq	r2, fp, #67108864	@ 0x4000000
    1a54:	80204318 	eorhi	r4, r0, r8, lsl r3
    1a58:	4ac2980b 	bmi	0xff0a7a8c
    1a5c:	88a21880 	stmiahi	r2!, {r7, fp, ip}
    1a60:	0a928880 	beq	0xfe4a3c68
    1a64:	30040292 	mulcc	r4, r2, r2
    1a68:	0d800580 	cfstr32eq	mvfx0, [r0, #512]	@ 0x200
    1a6c:	80a04310 	adchi	r4, r0, r0, lsl r3
    1a70:	43c0201f 	bicmi	r2, r0, #31
    1a74:	2303e006 	movwcs	lr, #12294	@ 0x3006
    1a78:	439a021b 	orrsmi	r0, sl, #-1342177279	@ 0xb0000001
    1a7c:	43c0200f 	bicmi	r2, r0, #15
    1a80:	60220041 	eorvs	r0, r2, r1, asr #32
    1a84:	4bb89a0a 	blmi	0xfee282b4
    1a88:	18108912 	ldmdane	r0, {r1, r4, r8, fp, pc}
    1a8c:	401a6822 	andsmi	r6, sl, r2, lsr #16
    1a90:	0dc005c0 	cfstr64eq	mvdx0, [r0, #768]	@ 0x300
    1a94:	43100400 	tstmi	r0, #0, 8
    1a98:	9a0a6020 	bls	0x299b20
    1a9c:	18518952 	ldmdane	r1, {r1, r4, r6, r8, fp, pc}^
    1aa0:	02000a00 	andeq	r0, r0, #0, 20
    1aa4:	0e090609 	cfmadd32eq	mvax0, mvfx0, mvfx9, mvfx9
    1aa8:	60204308 	eorvs	r4, r0, r8, lsl #6
    1aac:	07014270 	smlsdxeq	r1, r0, r2, r4
    1ab0:	00490f09 	subeq	r0, r9, r9, lsl #30
    1ab4:	11009a0c 	tstne	r0, ip, lsl #20
    1ab8:	30010092 	mulcc	r1, r2, r0
    1abc:	f7fe3201 			@ <UNDEFINED> instruction: 0xf7fe3201
    1ac0:	9a07fe7d 	bls	0x2014bc
    1ac4:	38381fd0 	ldmdacc	r8!, {r4, r6, r7, r8, r9, sl, fp, ip}
    1ac8:	d80c2822 	stmdale	ip, {r1, r5, fp, sp}
    1acc:	9a0748a7 	bls	0x1d3d70
    1ad0:	38401880 	stmdacc	r0, {r7, fp, ip}^
    1ad4:	68207841 	stmdavs	r0!, {r0, r6, fp, ip, sp, lr}
    1ad8:	02120a02 	andseq	r0, r2, #8192	@ 0x2000
    1adc:	06001840 	streq	r1, [r0], -r0, asr #16
    1ae0:	43100e00 	tstmi	r0, #0, 28
    1ae4:	9a076020 	bls	0x1d9b6c
    1ae8:	39591fd1 	ldmdbcc	r9, {r0, r4, r6, r7, r8, r9, sl, fp, ip}^
    1aec:	d80c2950 	stmdale	ip, {r4, r6, r8, fp, sp}
    1af0:	f0012005 			@ <UNDEFINED> instruction: 0xf0012005
    1af4:	3808fe15 	stmdacc	r8, {r0, r2, r4, r9, sl, fp, ip, sp, lr, pc}
    1af8:	fc50f7fe 	mrrc2	7, 15, pc, r0, cr14	@ <UNPREDICTABLE>
    1afc:	00819a0c 	addeq	r9, r1, ip, lsl #20
    1b00:	1c420090 	mcrrne	0, 9, r0, r2, cr0
    1b04:	f7fe2000 			@ <UNDEFINED> instruction: 0xf7fe2000
    1b08:	3d01fe59 	stccc	14, cr15, [r1, #-356]	@ 0xfffffe9c
    1b0c:	e753d400 	ldrb	sp, [r3, -r0, lsl #8]
    1b10:	06242407 	strteq	r2, [r4], -r7, lsl #8
    1b14:	d0012f6c 	andle	r2, r1, ip, ror #30
    1b18:	d10d2fb4 			@ <UNDEFINED> instruction: 0xd10d2fb4
    1b1c:	21069802 	tstcs	r6, r2, lsl #16
    1b20:	90023838 	andls	r3, r2, r8, lsr r8
    1b24:	91049801 	tstls	r4, r1, lsl #16
    1b28:	90013880 	andls	r3, r1, r0, lsl #17
    1b2c:	9003200a 	andls	r2, r3, sl
    1b30:	4981488f 	stmibmi	r1, {r0, r1, r2, r3, r7, fp, lr}
    1b34:	e0456108 	sub	r6, r5, r8, lsl #2
    1b38:	dd3f2f6c 	ldcle	15, cr2, [pc, #-432]!	@ 0x1990
    1b3c:	38039802 	stmdacc	r3, {r1, fp, ip, pc}
    1b40:	98089002 	stmdals	r8, {r1, ip, pc}
    1b44:	d1012800 	tstle	r1, r0, lsl #16
    1b48:	e0002001 	and	r2, r0, r1
    1b4c:	23032002 	movwcs	r2, #12290	@ 0x3002
    1b50:	021b6ca1 	andseq	r6, fp, #41216	@ 0xa100
    1b54:	07804399 			@ <UNDEFINED> instruction: 0x07804399
    1b58:	02000f80 	andeq	r0, r0, #128, 30	@ 0x200
    1b5c:	49824308 	stmibmi	r2, {r3, r8, r9, lr}
    1b60:	23014001 	movwcs	r4, #4097	@ 0x1001
    1b64:	18c0049b 	stmiane	r0, {r0, r1, r3, r4, r7, sl}^
    1b68:	43984b7f 	orrsmi	r4, r8, #130048	@ 0x1fc00
    1b6c:	64a04308 	strtvs	r4, [r0], #776	@ 0x308
    1b70:	00e82500 	rsceq	r2, r8, r0, lsl #10
    1b74:	1dc11900 	vstrne.16	s3, [r1]	@ <UNPREDICTABLE>
    1b78:	22033179 	andcs	r3, r3, #1073741854	@ 0x4000001e
    1b7c:	f7fe1c0e 			@ <UNDEFINED> instruction: 0xf7fe1c0e
    1b80:	8830ffe5 	ldmdahi	r0!, {r0, r2, r5, r6, r7, r8, r9, sl, fp, ip, sp, lr, pc}
    1b84:	029b2303 	addseq	r2, fp, #201326592	@ 0xc000000
    1b88:	35014058 	strcc	r4, [r1, #-88]	@ 0xffffffa8
    1b8c:	80302d09 	eorshi	r2, r0, r9, lsl #26
    1b90:	2006dbef 	andcs	sp, r6, pc, ror #23
    1b94:	f0011c39 			@ <UNDEFINED> instruction: 0xf0011c39
    1b98:	2900fdc3 	stmdbcs	r0, {r0, r1, r6, r7, r8, sl, fp, ip, sp, lr, pc}
    1b9c:	9904d109 	stmdbls	r4, {r0, r3, r8, ip, lr, pc}
    1ba0:	31019803 	tstcc	r1, r3, lsl #16
    1ba4:	90033801 	andls	r3, r3, r1, lsl #16
    1ba8:	43080200 	movwmi	r0, #33280	@ 0x8200
    1bac:	49629104 	stmdbmi	r2!, {r2, r8, ip, pc}^
    1bb0:	48708248 	ldmdami	r0!, {r3, r6, r9, pc}^
    1bb4:	81484960 	cmphi	r8, r0, ror #18
    1bb8:	e000486f 	and	r4, r0, pc, ror #16
    1bbc:	2101486f 	tstcs	r1, pc, ror #16
    1bc0:	80080689 	andhi	r0, r8, r9, lsl #13
    1bc4:	02009802 	andeq	r9, r0, #131072	@ 0x20000
    1bc8:	06892101 	streq	r2, [r9], r1, lsl #2
    1bcc:	98016388 	stmdals	r1, {r3, r7, r8, r9, sp, lr}
    1bd0:	63c80200 	bicvs	r0, r8, #0, 4
    1bd4:	db052f10 	blle	0x14d81c
    1bd8:	f8f4f000 			@ <UNDEFINED> instruction: 0xf8f4f000
    1bdc:	d1012f10 	tstle	r1, r0, lsl pc
    1be0:	e0024967 	and	r4, r2, r7, ror #18
    1be4:	d1032fa2 	smlatble	r3, r2, pc, r2	@ <UNPREDICTABLE>
    1be8:	48574966 	ldmdami	r7, {r1, r2, r5, r6, r8, fp, lr}^
    1bec:	fc22f7ff 	stc2	7, cr15, [r2], #-1020	@ 0xfffffc04
    1bf0:	383a1ff8 	ldmdacc	sl!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, ip}
    1bf4:	d211284f 	andsle	r2, r1, #5177344	@ 0x4f0000
    1bf8:	28009808 	stmdacs	r0, {r3, fp, ip, pc}
    1bfc:	4862d10e 	stmdami	r2!, {r1, r2, r3, r8, ip, lr, pc}^
    1c00:	6a402301 	bvs	0x100a80c
    1c04:	d00942d8 	ldrdle	r4, [r9], -r8
    1c08:	78004860 	stmdavc	r0, {r5, r6, fp, lr}
    1c0c:	d10528f3 	strdle	r2, [r5, -r3]
    1c10:	484f495f 	stmdami	pc, {r0, r1, r2, r3, r4, r6, r8, fp, lr}^	@ <UNPREDICTABLE>
    1c14:	fc0ef7ff 	stc2	7, cr15, [lr], {255}	@ 0xff
    1c18:	90082001 	andls	r2, r8, r1
    1c1c:	dd0d2f38 	stcle	15, cr2, [sp, #-224]	@ 0xffffff20
    1c20:	28009808 	stmdacs	r0, {r3, fp, ip, pc}
    1c24:	9900d00a 	stmdbls	r0, {r1, r3, ip, lr, pc}
    1c28:	da022920 	ble	0x8c0b0
    1c2c:	31029900 	tstcc	r2, r0, lsl #18
    1c30:	221f9100 	andscs	r9, pc, #0, 2
    1c34:	99002006 	stmdbls	r0, {r1, r2, sp}
    1c38:	fdc0f7fe 	stc2l	7, cr15, [r0, #1016]	@ 0x3f8
    1c3c:	ff7af000 			@ <UNDEFINED> instruction: 0xff7af000
    1c40:	2001493b 	andcs	r4, r1, fp, lsr r9
    1c44:	f0018108 			@ <UNDEFINED> instruction: 0xf0018108
    1c48:	2f10fd6f 	svccs	0x0010fd6f
    1c4c:	9904da09 	stmdbls	r4, {r0, r3, r9, fp, ip, lr, pc}
    1c50:	31019803 	tstcc	r1, r3, lsl #16
    1c54:	90033801 	andls	r3, r3, r1, lsl #16
    1c58:	43080200 	movwmi	r0, #33280	@ 0x8200
    1c5c:	49369104 	ldmdbmi	r6!, {r2, r8, ip, pc}
    1c60:	37018248 	strcc	r8, [r1, -r8, asr #4]
    1c64:	dc002fd2 	stcle	15, cr2, [r0], {210}	@ 0xd2
    1c68:	484ae6a4 	stmdami	sl, {r2, r5, r7, r9, sl, sp, lr, pc}^
    1c6c:	fd3cf7fe 	ldc2	7, cr15, [ip, #-1016]!	@ 0xfffffc08
    1c70:	1c072600 	stcne	6, cr2, [r7], {-0}
    1c74:	4d482800 	stclmi	8, cr2, [r8, #-0]
    1c78:	9808d102 	stmdals	r8, {r1, r8, ip, lr, pc}
    1c7c:	d02b2800 	eorle	r2, fp, r0, lsl #16
    1c80:	72e82001 	rscvc	r2, r8, #1
    1c84:	f00071ee 			@ <UNDEFINED> instruction: 0xf00071ee
    1c88:	0600ff55 			@ <UNDEFINED> instruction: 0x0600ff55
    1c8c:	72a80e00 	adcvc	r0, r8, #0, 28
    1c90:	f000d122 			@ <UNDEFINED> instruction: 0xf000d122
    1c94:	f001f897 			@ <UNDEFINED> instruction: 0xf001f897
    1c98:	2f00fd47 	svccs	0x0000fd47
    1c9c:	79e8d1f3 	stmibvc	r8!, {r0, r1, r4, r5, r6, r7, r8, ip, lr, pc}^
    1ca0:	d1f02800 	mvnsle	r2, r0, lsl #16
    1ca4:	28007ae8 	stmdacs	r0, {r3, r5, r6, r7, r9, fp, ip, sp, lr}
    1ca8:	4838d00b 	ldmdami	r8!, {r0, r1, r3, ip, lr, pc}
    1cac:	43c07800 	bicmi	r7, r0, #0, 16
    1cb0:	401823f3 			@ <UNDEFINED> instruction: 0x401823f3
    1cb4:	4939d0e7 	ldmdbmi	r9!, {r0, r1, r2, r5, r6, r7, ip, lr, pc}
    1cb8:	f7ff4825 			@ <UNDEFINED> instruction: 0xf7ff4825
    1cbc:	72eefbbb 	rscvc	pc, lr, #191488	@ 0x2ec00
    1cc0:	9900e7e1 	stmdbls	r0, {r0, r5, r6, r7, r8, r9, sl, sp, lr, pc}
    1cc4:	dd072900 	vstrle.16	s4, [r7, #-0]	@ <UNPREDICTABLE>
    1cc8:	221f9900 	andscs	r9, pc, #0, 18
    1ccc:	91003901 	tstls	r0, r1, lsl #18
    1cd0:	f7fe2006 			@ <UNDEFINED> instruction: 0xf7fe2006
    1cd4:	e7d6fd73 			@ <UNDEFINED> instruction: 0xe7d6fd73
    1cd8:	48174931 	ldmdami	r7, {r0, r4, r5, r8, fp, lr}
    1cdc:	61466101 	cmpvs	r6, r1, lsl #2
    1ce0:	00ca2100 	sbceq	r2, sl, r0, lsl #2
    1ce4:	230358a7 	movwcs	r5, #14503	@ 0x38a7
    1ce8:	439f029b 	orrsmi	r0, pc, #-1342177271	@ 0xb0000009
    1cec:	29093101 	stmdbcs	r9, {r0, r8, ip, sp}
    1cf0:	dbf650a7 	blle	0xffd95f94
    1cf4:	43ff2700 	mvnsmi	r2, #0, 14
    1cf8:	e00c1c04 	and	r1, ip, r4, lsl #24
    1cfc:	f862f000 			@ <UNDEFINED> instruction: 0xf862f000
    1d00:	fd12f001 	ldc2	0, cr15, [r2, #-4]
    1d04:	d2060878 	andle	r0, r6, #120, 16	@ 0x780000
    1d08:	28109805 	ldmdacs	r0, {r0, r2, fp, ip, pc}
    1d0c:	9805d003 	stmdals	r5, {r0, r1, ip, lr, pc}
    1d10:	90053001 	andls	r3, r5, r1
    1d14:	37016160 	strcc	r6, [r1, -r0, ror #2]
    1d18:	ddef2f32 	stclle	15, cr2, [pc, #200]!	@ 0x1de8
    1d1c:	fdacf7ff 	stc2	7, cr15, [ip, #1020]!	@ 0x3fc
    1d20:	28007aa8 	stmdacs	r0, {r3, r5, r7, r9, fp, ip, sp, lr}
    1d24:	20ded03e 	sbcscs	sp, lr, lr, lsr r0
    1d28:	e03be03d 	eors	lr, fp, sp, lsr r0
    1d2c:	04000300 	streq	r0, [r0], #-768	@ 0xfffffd00
    1d30:	04000200 	streq	r0, [r0], #-512	@ 0xfffffe00
    1d34:	10003f5f 	andne	r3, r0, pc, asr pc
    1d38:	04000040 	streq	r0, [r0], #-64	@ 0xffffffc0
    1d3c:	03003b2c 	movweq	r3, #2860	@ 0xb2c
    1d40:	00940a00 	addseq	r0, r4, r0, lsl #20
    1d44:	0300372c 	movweq	r3, #1836	@ 0x72c
    1d48:	030036ec 	movweq	r3, #1772	@ 0x6ec
    1d4c:	0300394c 	movweq	r3, #2380	@ 0x94c
    1d50:	0300390c 	movweq	r3, #2316	@ 0x90c
    1d54:	03003564 	movweq	r3, #1380	@ 0x564
    1d58:	03003580 	movweq	r3, #1408	@ 0x580
    1d5c:	030035f0 	movweq	r3, #1520	@ 0x5f0
    1d60:	07000026 	streq	r0, [r0, -r6, lsr #32]
    1d64:	0000369c 	muleq	r0, ip, r6
    1d68:	fe00ffff 	mcr2	15, 0, pc, cr0, cr15, {7}	@ <UNPREDICTABLE>
    1d6c:	000036ec 	andeq	r3, r0, ip, ror #13
    1d70:	10001f5f 	andne	r1, r0, pc, asr pc
    1d74:	00003f27 	andeq	r3, r0, r7, lsr #30
    1d78:	00009802 	andeq	r9, r0, r2, lsl #16
    1d7c:	00001002 	andeq	r1, r0, r2
    1d80:	00003908 	andeq	r3, r0, r8, lsl #18
    1d84:	000039c0 	andeq	r3, r0, r0, asr #19
    1d88:	03000064 	movweq	r0, #100	@ 0x64
    1d8c:	04000130 	streq	r0, [r0], #-304	@ 0xfffffed0
    1d90:	0000389c 	muleq	r0, ip, r8
    1d94:	03000088 	movweq	r0, #136	@ 0x88
    1d98:	03fffff0 	mvnseq	pc, #240, 30	@ 0x3c0
    1d9c:	00003818 	andeq	r3, r0, r8, lsl r8
    1da0:	00103fbf 			@ <UNDEFINED> instruction: 0x00103fbf
    1da4:	f7fe20ff 			@ <UNDEFINED> instruction: 0xf7fe20ff
    1da8:	b00dfe0c 	andlt	pc, sp, ip, lsl #28
    1dac:	bc08bcf0 	stclt	12, cr11, [r8], {240}	@ 0xf0
    1db0:	00004718 	andeq	r4, r0, r8, lsl r7
    1db4:	4710a200 	ldrmi	sl, [r0, -r0, lsl #4]
    1db8:	e0832190 	umull	r2, r3, r0, r1
    1dbc:	e2830000 	add	r0, r3, #0
    1dc0:	e12fff1e 	bx	lr
    1dc4:	680048de 	stmdavs	r0, {r1, r2, r3, r4, r6, r7, fp, lr}
    1dc8:	68034ade 	stmdavs	r3, {r1, r2, r3, r4, r6, r7, r9, fp, lr}
    1dcc:	d000429a 	mulle	r0, sl, r2
    1dd0:	1c5b4770 	mrrcne	7, 7, r4, fp, cr0
    1dd4:	b5f06003 	ldrblt	r6, [r0, #3]!
    1dd8:	464a4641 	strbmi	r4, [sl], -r1, asr #12
    1ddc:	465c4653 			@ <UNDEFINED> instruction: 0x465c4653
    1de0:	b085b41f 	addlt	fp, r5, pc, lsl r4
    1de4:	2b006a03 	blcs	0x1c5f8
    1de8:	6a40d003 	bvs	0x1035dfc
    1dec:	f989f000 			@ <UNDEFINED> instruction: 0xf989f000
    1df0:	6a839805 	bvs	0xfe0e7e0c
    1df4:	f985f000 			@ <UNDEFINED> instruction: 0xf985f000
    1df8:	69039805 	stmdbvs	r3, {r0, r2, fp, ip, pc}
    1dfc:	4dc14698 	stclmi	6, cr4, [r1, #608]	@ 0x260
    1e00:	7904182d 	stmdbvc	r4, {r0, r2, r3, r5, fp, ip}
    1e04:	d9041e67 	stmdble	r4, {r0, r1, r2, r5, r6, r9, sl, fp, ip}
    1e08:	1bc97ac1 	blne	0xff260914
    1e0c:	434a4642 	movtmi	r4, #42562	@ 0xa642
    1e10:	950218ad 	strls	r1, [r2, #-2221]	@ 0xfffff753
    1e14:	79434ebc 	stmdbvc	r3, {r2, r3, r4, r5, r7, r9, sl, fp, lr}^
    1e18:	d02b2b00 	eorle	r2, fp, r0, lsl #22
    1e1c:	4708a100 	strmi	sl, [r8, -r0, lsl #2]
    1e20:	e3540002 	cmp	r4, #2
    1e24:	02807e35 	addeq	r7, r0, #848	@ 0x350
    1e28:	10857008 	addne	r7, r5, r8
    1e2c:	e1a04008 	mov	r4, r8
    1e30:	e19500d6 	ldrsb	r0, [r5, r6]
    1e34:	e1d510d0 	ldrsb	r1, [r5]
    1e38:	e0800001 	add	r0, r0, r1
    1e3c:	e19710d6 	ldrsb	r1, [r7, r6]
    1e40:	e0800001 	add	r0, r0, r1
    1e44:	e0d710d1 	ldrsb	r1, [r7], #1
    1e48:	e0800001 	add	r0, r0, r1
    1e4c:	e0010390 	mul	r1, r0, r3
    1e50:	e1a004c1 	asr	r0, r1, #9
    1e54:	e3100080 	tst	r0, #128	@ 0x80
    1e58:	12800001 	addne	r0, r0, #1
    1e5c:	e7c50006 	strb	r0, [r5, r6]
    1e60:	e4c50001 	strb	r0, [r5], #1
    1e64:	e2544001 	subs	r4, r4, #1
    1e68:	cafffff0 	bgt	0x1e30
    1e6c:	e28f002f 	add	r0, pc, #47	@ 0x2f
    1e70:	e12fff10 	bx	r0
    1e74:	46412000 	strbmi	r2, [r1], -r0
    1e78:	08c91976 	stmiaeq	r9, {r1, r2, r4, r5, r6, r8, fp, ip}^
    1e7c:	c501d301 	strgt	sp, [r1, #-769]	@ 0xfffffcff
    1e80:	0849c601 	stmdaeq	r9, {r0, r9, sl, lr, pc}^
    1e84:	c501d303 	strgt	sp, [r1, #-771]	@ 0xfffffcfd
    1e88:	c501c601 	strgt	ip, [r1, #-1537]	@ 0xfffff9ff
    1e8c:	c501c601 	strgt	ip, [r1, #-1537]	@ 0xfffff9ff
    1e90:	c501c601 	strgt	ip, [r1, #-1537]	@ 0xfffff9ff
    1e94:	c501c601 	strgt	ip, [r1, #-1537]	@ 0xfffff9ff
    1e98:	c501c601 	strgt	ip, [r1, #-1537]	@ 0xfffff9ff
    1e9c:	1e49c601 	cdpne	6, 4, cr12, cr9, cr1, {0}
    1ea0:	9c05dcf5 	stcls	12, cr13, [r5], {245}	@ 0xf5
    1ea4:	46816960 	strmi	r6, [r1], r0, ror #18
    1ea8:	468469a0 	strmi	r6, [r4], r0, lsr #19
    1eac:	345079a0 	ldrbcc	r7, [r0], #-2464	@ 0xfffff660
    1eb0:	6a639001 	bvs	0x18e5ebc
    1eb4:	20c77826 	sbccs	r7, r7, r6, lsr #16
    1eb8:	d1004230 	tstle	r0, r0, lsr r2
    1ebc:	2080e112 	addcs	lr, r0, r2, lsl r1
    1ec0:	d0144230 	andsle	r4, r4, r0, lsr r2
    1ec4:	42302040 	eorsmi	r2, r0, #64	@ 0x40
    1ec8:	2603d119 			@ <UNDEFINED> instruction: 0x2603d119
    1ecc:	1c187026 	ldcne	0, cr7, [r8], {38}	@ 0x26
    1ed0:	62a03010 	adcvs	r3, r0, #16
    1ed4:	61a068d8 	ldrdvs	r6, [r0, r8]!
    1ed8:	72652500 	rsbvc	r2, r5, #0, 10
    1edc:	78da61e5 	ldmvc	sl, {r0, r2, r5, r6, r7, r8, sp, lr}^
    1ee0:	421020c0 	andsmi	r2, r0, #192	@ 0xc0
    1ee4:	2010d02f 	andscs	sp, r0, pc, lsr #32
    1ee8:	70264306 	eorvc	r4, r6, r6, lsl #6
    1eec:	7a65e02b 	bvc	0x1979fa0
    1ef0:	42302004 	eorsmi	r2, r0, #4
    1ef4:	7b60d006 	blvc	0x1835f14
    1ef8:	73601e40 	cmnvc	r0, #64, 28	@ 0x400
    1efc:	2000d82a 	andcs	sp, r0, sl, lsr #16
    1f00:	e0ef7020 	rsc	r7, pc, r0, lsr #32
    1f04:	42302040 	eorsmi	r2, r0, #64	@ 0x40
    1f08:	79e0d00c 	stmibvc	r0!, {r2, r3, ip, lr, pc}^
    1f0c:	0a2d4345 	beq	0xb52c28
    1f10:	42857b20 	addmi	r7, r5, #32, 22	@ 0x8000
    1f14:	7b25d81e 	blvc	0x977f94
    1f18:	d0f02d00 	rscsle	r2, r0, r0, lsl #26
    1f1c:	43062004 	movwmi	r2, #24580	@ 0x6004
    1f20:	e0177026 	ands	r7, r7, r6, lsr #32
    1f24:	40322203 	eorsmi	r2, r2, r3, lsl #4
    1f28:	d10a2a02 	tstle	sl, r2, lsl #20
    1f2c:	43457960 	movtmi	r7, #22880	@ 0x5960
    1f30:	79a00a2d 	stmibvc	r0!, {r0, r2, r3, r5, r9, fp}
    1f34:	d80d4285 	stmdale	sp, {r0, r2, r7, r9, lr}
    1f38:	d0ec1c05 	rscle	r1, ip, r5, lsl #24
    1f3c:	70261e76 	eorvc	r1, r6, r6, ror lr
    1f40:	2a03e008 	bcs	0xf9f68
    1f44:	7920d106 	stmdbvc	r0!, {r1, r2, r8, ip, lr, pc}
    1f48:	2dff182d 	ldclcs	8, cr1, [pc, #180]!	@ 0x2004
    1f4c:	25ffd302 	ldrbcs	sp, [pc, #770]!	@ 0x2256
    1f50:	70261e76 	eorvc	r1, r6, r6, ror lr
    1f54:	98057265 	stmdals	r5, {r0, r2, r5, r6, r9, ip, sp, lr}
    1f58:	1c4079c0 	mcrrne	9, 12, r7, r0, cr0	@ <UNPREDICTABLE>
    1f5c:	09054368 	stmdbeq	r5, {r3, r5, r6, r8, r9, lr}
    1f60:	436878a0 	cmnmi	r8, #160, 16	@ 0xa00000
    1f64:	72a00a00 	adcvc	r0, r0, #0, 20
    1f68:	436878e0 	cmnmi	r8, #224, 16	@ 0xe00000
    1f6c:	72e00a00 	rscvc	r0, r0, #0, 20
    1f70:	40302010 	eorsmi	r2, r0, r0, lsl r0
    1f74:	d0079004 	andle	r9, r7, r4
    1f78:	30101c18 	andscc	r1, r0, r8, lsl ip
    1f7c:	18406899 	stmdane	r0, {r0, r3, r4, r7, fp, sp, lr}^
    1f80:	68d89003 	ldmvs	r8, {r0, r1, ip, pc}^
    1f84:	90041a40 	andls	r1, r4, r0, asr #20
    1f88:	69a29d02 	stmibvs	r2!, {r1, r8, sl, fp, ip, pc}
    1f8c:	a0016aa3 	andge	r6, r1, r3, lsr #21
    1f90:	00004700 	andeq	r4, r0, r0, lsl #14
    1f94:	e58d8000 	str	r8, [sp]
    1f98:	e5d4a00a 	ldrb	sl, [r4, #10]
    1f9c:	e5d4b00b 	ldrb	fp, [r4, #11]
    1fa0:	e5d40001 	ldrb	r0, [r4, #1]
    1fa4:	e3100008 	tst	r0, #8
    1fa8:	0a000013 	beq	0x1ffc
    1fac:	e0d360d1 	ldrsb	r6, [r3], #1
    1fb0:	e0010b96 	mul	r1, r6, fp
    1fb4:	e5d50630 	ldrb	r0, [r5, #1584]	@ 0x630
    1fb8:	e0800441 	add	r0, r0, r1, asr #8
    1fbc:	e5c50630 	strb	r0, [r5, #1584]	@ 0x630
    1fc0:	e0010a96 	mul	r1, r6, sl
    1fc4:	e5d50000 	ldrb	r0, [r5]
    1fc8:	e0800441 	add	r0, r0, r1, asr #8
    1fcc:	e4c50001 	strb	r0, [r5], #1
    1fd0:	e2522001 	subs	r2, r2, #1
    1fd4:	1a000005 	bne	0x1ff0
    1fd8:	e59d2010 	ldr	r2, [sp, #16]
    1fdc:	e3520000 	cmp	r2, #0
    1fe0:	159d300c 	ldrne	r3, [sp, #12]
    1fe4:	1a000001 	bne	0x1ff0
    1fe8:	e5c42000 	strb	r2, [r4]
    1fec:	ea000039 	b	0x20d8
    1ff0:	e2588001 	subs	r8, r8, #1
    1ff4:	caffffec 	bgt	0x1fac
    1ff8:	ea000034 	b	0x20d0
    1ffc:	e594701c 	ldr	r7, [r4, #28]
    2000:	e594e020 	ldr	lr, [r4, #32]
    2004:	e1570109 	cmp	r7, r9, lsl #2
    2008:	3a000006 	bcc	0x2028
    200c:	e3520004 	cmp	r2, #4
    2010:	da00000d 	ble	0x204c
    2014:	e2422004 	sub	r2, r2, #4
    2018:	e2833004 	add	r3, r3, #4
    201c:	e0477109 	sub	r7, r7, r9, lsl #2
    2020:	e1570109 	cmp	r7, r9, lsl #2
    2024:	2afffff8 	bcs	0x200c
    2028:	e1570089 	cmp	r7, r9, lsl #1
    202c:	3a000004 	bcc	0x2044
    2030:	e3520002 	cmp	r2, #2
    2034:	da000004 	ble	0x204c
    2038:	e2422002 	sub	r2, r2, #2
    203c:	e2833002 	add	r3, r3, #2
    2040:	e0477089 	sub	r7, r7, r9, lsl #1
    2044:	e1570009 	cmp	r7, r9
    2048:	3a00000b 	bcc	0x207c
    204c:	e2522001 	subs	r2, r2, #1
    2050:	1a000005 	bne	0x206c
    2054:	e59d2010 	ldr	r2, [sp, #16]
    2058:	e3520000 	cmp	r2, #0
    205c:	159d300c 	ldrne	r3, [sp, #12]
    2060:	1a000002 	bne	0x2070
    2064:	e5c42000 	strb	r2, [r4]
    2068:	ea00001a 	b	0x20d8
    206c:	e2833001 	add	r3, r3, #1
    2070:	e0477009 	sub	r7, r7, r9
    2074:	e1570009 	cmp	r7, r9
    2078:	2afffff3 	bcs	0x204c
    207c:	e1d300d0 	ldrsb	r0, [r3]
    2080:	e1d310d1 	ldrsb	r1, [r3, #1]
    2084:	e0411000 	sub	r1, r1, r0
    2088:	e0060791 	mul	r6, r1, r7
    208c:	e0010c96 	mul	r1, r6, ip
    2090:	e0806bc1 	add	r6, r0, r1, asr #23
    2094:	e0010b96 	mul	r1, r6, fp
    2098:	e5d50630 	ldrb	r0, [r5, #1584]	@ 0x630
    209c:	e0800441 	add	r0, r0, r1, asr #8
    20a0:	e5c50630 	strb	r0, [r5, #1584]	@ 0x630
    20a4:	e0010a96 	mul	r1, r6, sl
    20a8:	e5d50000 	ldrb	r0, [r5]
    20ac:	e0800441 	add	r0, r0, r1, asr #8
    20b0:	e4c50001 	strb	r0, [r5], #1
    20b4:	e087700e 	add	r7, r7, lr
    20b8:	e2588001 	subs	r8, r8, #1
    20bc:	0a000002 	beq	0x20cc
    20c0:	e1570009 	cmp	r7, r9
    20c4:	3affffec 	bcc	0x207c
    20c8:	eaffffcd 	b	0x2004
    20cc:	e584701c 	str	r7, [r4, #28]
    20d0:	e5842018 	str	r2, [r4, #24]
    20d4:	e5843028 	str	r3, [r4, #40]	@ 0x28
    20d8:	e59d8000 	ldr	r8, [sp]
    20dc:	e28f0001 	add	r0, pc, #1
    20e0:	e12fff10 	bx	r0
    20e4:	1e409801 	cdpne	8, 4, cr9, cr0, cr1, {0}
    20e8:	3440dd01 	strbcc	sp, [r0], #-3329	@ 0xfffff2ff
    20ec:	9805e6e0 	stmdals	r5, {r5, r6, r7, r9, sl, sp, lr, pc}
    20f0:	60034b14 	andvs	r4, r3, r4, lsl fp
    20f4:	bcffb006 	ldcllt	0, cr11, [pc], #24	@ 0x2114
    20f8:	46894680 	strmi	r4, [r9], r0, lsl #13
    20fc:	469b4692 			@ <UNDEFINED> instruction: 0x469b4692
    2100:	4718bc08 	ldrmi	fp, [r8, -r8, lsl #24]
    2104:	00000350 	andeq	r0, r0, r0, asr r3
    2108:	00000630 	andeq	r0, r0, r0, lsr r6
    210c:	6800480c 	stmdavs	r0, {r2, r3, fp, lr}
    2110:	68034a0c 	stmdavs	r3, {r2, r3, r9, fp, lr}
    2114:	d10e429a 			@ <UNDEFINED> instruction: 0xd10e429a
    2118:	1e497901 	vmlane.f16	s15, s18, s2	@ <UNPREDICTABLE>
    211c:	dc0a7101 	stfles	f7, [sl], {1}
    2120:	71017ac1 	smlabtvc	r1, r1, sl, r7
    2124:	21b62000 			@ <UNDEFINED> instruction: 0x21b62000
    2128:	4a030209 	bmi	0xc2954
    212c:	80104b03 	andshi	r4, r0, r3, lsl #22
    2130:	80118018 	andshi	r8, r1, r8, lsl r0
    2134:	47708019 			@ <UNDEFINED> instruction: 0x47708019
    2138:	040000c6 	streq	r0, [r0], #-198	@ 0xffffff3a
    213c:	040000d2 	streq	r0, [r0], #-210	@ 0xffffff2e
    2140:	03007ff0 	movweq	r7, #4080	@ 0xff0
    2144:	68736d53 	ldmdavs	r3!, {r0, r1, r4, r6, r8, sl, fp, sp, lr}^
    2148:	6b434a98 	blvs	0x10d4bb0
    214c:	d000429a 	mulle	r0, sl, r2
    2150:	1c5b4770 	mrrcne	7, 7, r4, fp, cr0
    2154:	b5f06343 	ldrblt	r6, [r0, #835]!	@ 0x343
    2158:	464d4644 	strbmi	r4, [sp], -r4, asr #12
    215c:	465f4656 			@ <UNDEFINED> instruction: 0x465f4656
    2160:	1c07b4f0 	cfstrsne	mvf11, [r7], {240}	@ 0xf0
    2164:	2b006bbb 	blcs	0x1d058
    2168:	6bf8d002 	blvs	0xffe36178
    216c:	ffc9f7ff 			@ <UNDEFINED> instruction: 0xffc9f7ff
    2170:	28006878 	stmdacs	r0, {r3, r4, r5, r6, fp, sp, lr}
    2174:	e10cda00 	tst	ip, r0, lsl #20
    2178:	6800488b 	stmdavs	r0, {r0, r1, r3, r7, fp, lr}
    217c:	1c384680 	ldcne	6, cr4, [r8], #-512	@ 0xfffffe00
    2180:	f9d8f7ff 			@ <UNDEFINED> instruction: 0xf9d8f7ff
    2184:	8c398c78 	ldchi	12, cr8, [r9], #-480	@ 0xfffffe20
    2188:	e0a41840 	adc	r1, r4, r0, asr #16
    218c:	6afd7a3a 	bvs	0xfff60a7c
    2190:	24002301 	strcs	r2, [r0], #-769	@ 0xfffffcff
    2194:	21807828 	orrcs	r7, r0, r8, lsr #16
    2198:	d1004201 	tstle	r0, r1, lsl #4
    219c:	4691e08b 	ldrmi	lr, [r1], fp, lsl #1
    21a0:	431c469a 	tstmi	ip, #161480704	@ 0x9a00000
    21a4:	6a2c46a3 	bvs	0xb13c38
    21a8:	d0132c00 	andsle	r2, r3, r0, lsl #24
    21ac:	20c77821 	sbccs	r7, r7, r1, lsr #16
    21b0:	d0094208 	andle	r4, r9, r8, lsl #4
    21b4:	28007c20 	stmdacs	r0, {r5, sl, fp, ip, sp, lr}
    21b8:	1e40d009 	cdpne	0, 4, cr13, cr0, cr9, {0}
    21bc:	d1067420 	tstle	r6, r0, lsr #8
    21c0:	43012040 	movwmi	r2, #4160	@ 0x1040
    21c4:	e0027021 	and	r7, r2, r1, lsr #32
    21c8:	f0001c20 			@ <UNDEFINED> instruction: 0xf0001c20
    21cc:	6b64f8fc 	blvs	0x19405c4
    21d0:	d1eb2c00 	mvnle	r2, r0, lsl #24
    21d4:	2040782b 	subcs	r7, r0, fp, lsr #16
    21d8:	d03b4218 	eorsle	r4, fp, r8, lsl r2
    21dc:	f0001c28 			@ <UNDEFINED> instruction: 0xf0001c28
    21e0:	2080f8e7 	addcs	pc, r0, r7, ror #17
    21e4:	20027028 	andcs	r7, r2, r8, lsr #32
    21e8:	204073e8 	subcs	r7, r0, r8, ror #7
    21ec:	201674e8 	andscs	r7, r6, r8, ror #9
    21f0:	20017668 	andcs	r7, r1, r8, ror #12
    21f4:	77881da9 	strvc	r1, [r8, r9, lsr #27]
    21f8:	6c2ae02c 	stcvs	0, cr14, [sl], #-176	@ 0xffffff50
    21fc:	29807811 	stmibcs	r0, {r0, r4, fp, ip, sp, lr}
    2200:	79e9d201 	stmibvc	r9!, {r0, r9, ip, lr, pc}^
    2204:	1c52e004 	mrrcne	0, 0, lr, r2, cr4
    2208:	29bd642a 	ldmibcs	sp!, {r1, r3, r5, sl, sp, lr}
    220c:	71e9d300 	mvnvc	sp, r0, lsl #6
    2210:	d30829cf 	movwle	r2, #35279	@ 0x89cf
    2214:	6b834640 	blvs	0xfe0d3b1c
    2218:	38cf1c08 	stmiacc	pc, {r3, sl, fp, ip}^	@ <UNPREDICTABLE>
    221c:	1c2a1c39 	stcne	12, cr1, [sl], #-228	@ 0xffffff1c
    2220:	ff6ff7ff 			@ <UNDEFINED> instruction: 0xff6ff7ff
    2224:	29b0e016 	ldmibcs	r0!, {r1, r2, r4, sp, lr, pc}
    2228:	1c08d90f 			@ <UNDEFINED> instruction: 0x1c08d90f
    222c:	72b838b1 	adcsvc	r3, r8, #11599872	@ 0xb10000
    2230:	6b5b4643 	blvs	0x16d3b44
    2234:	181b0080 	ldmdane	fp, {r7}
    2238:	1c38681b 	ldcne	8, cr6, [r8], #-108	@ 0xffffff94
    223c:	f7ff1c29 			@ <UNDEFINED> instruction: 0xf7ff1c29
    2240:	7828ff60 	stmdavc	r8!, {r5, r6, r8, r9, sl, fp, ip, sp, lr, pc}
    2244:	d0332800 	eorsle	r2, r3, r0, lsl #16
    2248:	4856e004 	ldmdami	r6, {r2, sp, lr, pc}^
    224c:	18093980 	stmdane	r9, {r7, r8, fp, ip, sp}
    2250:	70687808 	rsbvc	r7, r8, r8, lsl #16
    2254:	28007868 	stmdacs	r0, {r3, r5, r6, fp, ip, sp, lr}
    2258:	1e40d0cf 	cdpne	0, 4, cr13, cr0, cr15, {6}
    225c:	7e697068 	cdpvc	0, 6, cr7, cr9, cr8, {3}
    2260:	d0252900 	eorle	r2, r5, r0, lsl #18
    2264:	28007de8 	stmdacs	r0, {r3, r5, r6, r7, r8, sl, fp, ip, sp, lr}
    2268:	7f28d022 	svcvc	0x0028d022
    226c:	d0022800 	andle	r2, r2, r0, lsl #16
    2270:	77281e40 	strvc	r1, [r8, -r0, asr #28]!
    2274:	7ea8e01c 	mcrvc	0, 5, lr, cr8, cr12, {0}
    2278:	76a81840 	strtvc	r1, [r8], r0, asr #16
    227c:	38401c01 	stmdacc	r0, {r0, sl, fp, ip}^
    2280:	d5020600 	strle	r0, [r2, #-1536]	@ 0xfffffa00
    2284:	1612060a 	ldrne	r0, [r2], -sl, lsl #12
    2288:	2080e001 	addcs	lr, r0, r1
    228c:	7de81a42 			@ <UNDEFINED> instruction: 0x7de81a42
    2290:	11824350 	orrne	r4, r2, r0, asr r3
    2294:	40507da8 	subsmi	r7, r0, r8, lsr #27
    2298:	d0090600 	andle	r0, r9, r0, lsl #12
    229c:	782875aa 	stmdavc	r8!, {r1, r3, r5, r7, r8, sl, ip, sp, lr}
    22a0:	29007e29 	stmdbcs	r0, {r0, r3, r5, r9, sl, fp, ip, sp, lr}
    22a4:	210cd101 	tstcs	ip, r1, lsl #2
    22a8:	2103e000 	mrscs	lr, (UNDEF: 3)
    22ac:	70284308 	eorvc	r4, r8, r8, lsl #6
    22b0:	4653464a 	ldrbmi	r4, [r3], -sl, asr #12
    22b4:	1e52465c 	mrcne	6, 2, r4, cr2, cr12, {2}
    22b8:	2050dd03 	subscs	sp, r0, r3, lsl #26
    22bc:	005b182d 	subseq	r1, fp, sp, lsr #16
    22c0:	465ee768 	ldrbmi	lr, [lr], -r8, ror #14
    22c4:	d1032e00 	tstle	r3, r0, lsl #28
    22c8:	06002080 	streq	r2, [r0], -r0, lsl #1
    22cc:	e0606078 	rsb	r6, r0, r8, ror r0
    22d0:	8c78607e 	ldclhi	0, cr6, [r8], #-504	@ 0xfffffe08
    22d4:	84783896 	ldrbthi	r3, [r8], #-2198	@ 0xfffff76a
    22d8:	d3002896 	movwle	r2, #2198	@ 0x896
    22dc:	7a3ae756 	bvc	0xebc03c
    22e0:	78286afd 	stmdavc	r8!, {r0, r2, r3, r4, r5, r6, r7, r9, fp, sp, lr}
    22e4:	42012180 	andmi	r2, r1, #128, 2
    22e8:	210fd04e 	tstcs	pc, lr, asr #32
    22ec:	d04b4201 	suble	r4, fp, r1, lsl #4
    22f0:	1c384691 	ldcne	6, cr4, [r8], #-580	@ 0xfffffdbc
    22f4:	f7ff1c29 			@ <UNDEFINED> instruction: 0xf7ff1c29
    22f8:	6a2cf951 	bvs	0xb40844
    22fc:	d03e2c00 	eorsle	r2, lr, r0, lsl #24
    2300:	20c77821 	sbccs	r7, r7, r1, lsr #16
    2304:	d1034208 	tstle	r3, r8, lsl #4
    2308:	f0001c20 			@ <UNDEFINED> instruction: 0xf0001c20
    230c:	e033f85c 	eors	pc, r3, ip, asr r8	@ <UNPREDICTABLE>
    2310:	26077860 	strcs	r7, [r7], -r0, ror #16
    2314:	782b4006 	stmdavc	fp!, {r1, r2, lr}
    2318:	42182003 	andsmi	r2, r8, #3
    231c:	7ca1d00e 	stcvc	0, cr13, [r1], #56	@ 0x38
    2320:	43487c28 	movtmi	r7, #35880	@ 0x8c28
    2324:	70a011c0 	adcvc	r1, r0, r0, asr #3
    2328:	43487c68 	movtmi	r7, #35944	@ 0x8c68
    232c:	70e011c0 	rscvc	r1, r0, r0, asr #3
    2330:	d0032e00 	andle	r2, r3, r0, lsl #28
    2334:	21017f60 	tstcs	r1, r0, ror #30
    2338:	77604308 	strbvc	r4, [r0, -r8, lsl #6]!
    233c:	4218200c 	andsmi	r2, r8, #12
    2340:	7a21d01a 	bvc	0x8763b0
    2344:	56282008 	strtpl	r2, [r8], -r8
    2348:	d500180a 	strle	r1, [r0, #-2058]	@ 0xfffff7f6
    234c:	2e002200 	cdpcs	2, 0, cr2, cr0, cr0, {0}
    2350:	4640d00c 	strbmi	sp, [r0], -ip
    2354:	1c116b03 			@ <UNDEFINED> instruction: 0x1c116b03
    2358:	1c307a6a 			@ <UNDEFINED> instruction: 0x1c307a6a
    235c:	fed1f7ff 	mrc2	7, 6, pc, cr1, cr15, {7}
    2360:	7f606220 	svcvc	0x00606220
    2364:	43082102 	movwmi	r2, #33026	@ 0x8102
    2368:	e0057760 	and	r7, r5, r0, ror #14
    236c:	7a6a1c11 	bvc	0x1a893b8
    2370:	f7ff6a60 			@ <UNDEFINED> instruction: 0xf7ff6a60
    2374:	6220fab1 	eorvs	pc, r0, #724992	@ 0xb1000
    2378:	2c006b64 			@ <UNDEFINED> instruction: 0x2c006b64
    237c:	7828d1c0 	stmdavc	r8!, {r6, r7, r8, ip, lr, pc}
    2380:	400821f0 	strdmi	r2, [r8], -r0
    2384:	464a7028 	strbmi	r7, [sl], -r8, lsr #32
    2388:	dd021e52 	stcle	14, cr1, [r2, #-328]	@ 0xfffffeb8
    238c:	182d2050 	stmdane	sp!, {r4, r6, sp}
    2390:	4806dca7 	stmdami	r6, {r0, r1, r2, r5, r7, sl, fp, ip, lr, pc}
    2394:	bcff6378 	ldcllt	3, cr6, [pc], #480	@ 0x257c
    2398:	46894680 	strmi	r4, [r9], r0, lsl #13
    239c:	469b4692 			@ <UNDEFINED> instruction: 0x469b4692
    23a0:	4700bc01 	strmi	fp, [r0, -r1, lsl #24]
    23a4:	000030d0 	ldrdeq	r3, [r0], -r0
    23a8:	03007ff0 	movweq	r7, #4080	@ 0xff0
    23ac:	68736d53 	ldmdavs	r3!, {r0, r1, r4, r6, r8, sl, fp, sp, lr}^
    23b0:	210046a4 	smlatbcs	r0, r4, r6, r4
    23b4:	23002200 	movwcs	r2, #512	@ 0x200
    23b8:	c01e2400 	andsgt	r2, lr, r0, lsl #8
    23bc:	c01ec01e 	andsgt	ip, lr, lr, lsl r0
    23c0:	4664c01e 			@ <UNDEFINED> instruction: 0x4664c01e
    23c4:	6ac34770 	bvs	0xff0d418c
    23c8:	d00b2b00 	andle	r2, fp, r0, lsl #22
    23cc:	6b026b41 	blvs	0x9d0d8
    23d0:	d0012a00 	andle	r2, r1, r0, lsl #20
    23d4:	e0006351 	and	r6, r0, r1, asr r3
    23d8:	29006219 	stmdbcs	r0, {r0, r3, r4, r9, sp, lr}
    23dc:	630ad000 	movwvs	sp, #40960	@ 0xa000
    23e0:	62c12100 	sbcvs	r2, r1, #0, 2
    23e4:	b5704770 	ldrblt	r4, [r0, #-1904]!	@ 0xfffff890
    23e8:	78291c0d 	stmdavc	r9!, {r0, r2, r3, sl, fp, ip}
    23ec:	42082080 	andmi	r2, r8, #128	@ 0x80
    23f0:	6a2cd015 	bvs	0xb3644c
    23f4:	d0112c00 	andsle	r2, r1, r0, lsl #24
    23f8:	78202600 	stmdavc	r0!, {r9, sl, sp}
    23fc:	d00a2800 	andle	r2, sl, r0, lsl #16
    2400:	23077860 	movwcs	r7, #30816	@ 0x7860
    2404:	d0044018 	andle	r4, r4, r8, lsl r0
    2408:	681b4b85 	ldmdavs	fp, {r0, r2, r7, r8, r9, fp, lr}
    240c:	f7ff6adb 			@ <UNDEFINED> instruction: 0xf7ff6adb
    2410:	7026fe78 	eorvc	pc, r6, r8, ror lr	@ <UNPREDICTABLE>
    2414:	6b6462e6 	blvs	0x191afb4
    2418:	d1ee2c00 	mvnle	r2, r0, lsl #24
    241c:	bc70622c 	lfmlt	f6, 2, [r0], #-176	@ 0xffffff50
    2420:	4700bc01 	strmi	fp, [r0, -r1, lsl #24]
    2424:	4644b5f0 			@ <UNDEFINED> instruction: 0x4644b5f0
    2428:	4656464d 	ldrbmi	r4, [r6], -sp, asr #12
    242c:	b4f0465f 	ldrbtlt	r4, [r0], #1631	@ 0x65f
    2430:	9100b085 	smlabbls	r0, r5, r0, fp
    2434:	497a1c15 	ldmdbmi	sl!, {r0, r2, r4, sl, fp, ip}^
    2438:	91016809 	tstls	r1, r9, lsl #16
    243c:	18404979 	stmdane	r0, {r0, r3, r4, r5, r6, r8, fp, lr}^
    2440:	71287800 			@ <UNDEFINED> instruction: 0x71287800
    2444:	78186c2b 	ldmdavc	r8, {r0, r1, r3, r5, sl, fp, sp, lr}
    2448:	d20e2880 	andle	r2, lr, #128, 16	@ 0x800000
    244c:	1c5b7168 	ldfnee	f7, [fp], {104}	@ 0x68
    2450:	28807818 	stmcs	r0, {r3, r4, fp, ip, sp, lr}
    2454:	71a8d208 			@ <UNDEFINED> instruction: 0x71a8d208
    2458:	78181c5b 	ldmdavc	r8, {r0, r1, r3, r4, r6, sl, fp, ip}
    245c:	d2032880 	andle	r2, r3, #128, 16	@ 0x800000
    2460:	18097929 	stmdane	r9, {r0, r3, r5, r8, fp, ip, sp, lr}
    2464:	1c5b7129 	ldfnee	f7, [fp], {41}	@ 0x29
    2468:	1c2c642b 	cfstrsne	mvf6, [ip], #-172	@ 0xffffff54
    246c:	78223424 	stmdavc	r2!, {r2, r5, sl, ip, sp}
    2470:	421020c0 	andsmi	r2, r0, #192	@ 0xc0
    2474:	796bd024 	stmdbvc	fp!, {r2, r5, ip, lr, pc}^
    2478:	42102040 	andsmi	r2, r0, #64	@ 0x40
    247c:	6ae9d003 	bvs	0xffa76490
    2480:	780818c9 	stmdavc	r8, {r0, r3, r6, r7, fp, ip}
    2484:	1c18e000 	ldcne	0, cr14, [r8], {-0}
    2488:	18090041 	stmdane	r9, {r0, r6}
    248c:	6aa80089 	bvs	0xfea026b8
    2490:	46891809 	strmi	r1, [r9], r9, lsl #16
    2494:	7831464e 	ldmdavc	r1!, {r1, r2, r3, r6, r9, sl, lr}
    2498:	420820c0 	andmi	r2, r8, #192	@ 0xc0
    249c:	e0b6d000 	adcs	sp, r6, r0
    24a0:	42102080 	andsmi	r2, r0, #128	@ 0x80
    24a4:	78f1d00e 	ldmvc	r1!, {r1, r2, r3, ip, lr, pc}^
    24a8:	42082080 	andmi	r2, r8, #128	@ 0x80
    24ac:	39c0d006 	stmibcc	r0, {r1, r2, ip, lr, pc}^
    24b0:	75690049 	strbvc	r0, [r9, #-73]!	@ 0xffffffb7
    24b4:	21037828 	tstcs	r3, r8, lsr #16
    24b8:	70284308 	eorvc	r4, r8, r8, lsl #6
    24bc:	e0017873 	and	r7, r1, r3, ror r8
    24c0:	796b46a1 	stmdbvc	fp!, {r0, r5, r7, r9, sl, lr}^
    24c4:	9e009302 	cdpls	3, 0, cr9, cr0, cr2, {0}
    24c8:	7f687a71 	svcvc	0x00687a71
    24cc:	28ff1840 	ldmcs	pc!, {r6, fp, ip}^	@ <UNPREDICTABLE>
    24d0:	20ffd900 	rscscs	sp, pc, r0, lsl #18
    24d4:	464e9004 	strbmi	r9, [lr], -r4
    24d8:	26077830 			@ <UNDEFINED> instruction: 0x26077830
    24dc:	96034006 	strls	r4, [r3], -r6
    24e0:	9801d018 	stmdals	r1, {r3, r4, ip, lr, pc}
    24e4:	2c0069c4 			@ <UNDEFINED> instruction: 0x2c0069c4
    24e8:	e090d100 	adds	sp, r0, r0, lsl #2
    24ec:	01b01e76 	rorseq	r1, r6, lr
    24f0:	78211824 	stmdavc	r1!, {r2, r5, fp, ip}
    24f4:	420820c7 	andmi	r2, r8, #199	@ 0xc7
    24f8:	2040d036 	subcs	sp, r0, r6, lsr r0
    24fc:	d1334208 	teqle	r3, r8, lsl #4
    2500:	98047ce1 	stmdals	r4, {r0, r5, r6, r7, sl, fp, ip, sp, lr}
    2504:	d32f4281 			@ <UNDEFINED> instruction: 0xd32f4281
    2508:	e080d000 	add	sp, r0, r0
    250c:	42a86ae0 	adcmi	r6, r8, #224, 20	@ 0xe0000
    2510:	e07cd22a 	rsbs	sp, ip, sl, lsr #4
    2514:	1c2f9e04 	stcne	14, cr9, [pc], #-16	@ 0x250c
    2518:	46902200 	ldrmi	r2, [r0], r0, lsl #4
    251c:	79a39c01 	stmibvc	r3!, {r0, sl, fp, ip, pc}
    2520:	78213450 	stmdavc	r1!, {r4, r6, sl, ip, sp}
    2524:	420820c7 	andmi	r2, r8, #199	@ 0xc7
    2528:	2040d01e 	subcs	sp, r0, lr, lsl r0
    252c:	d0054208 	andle	r4, r5, r8, lsl #4
    2530:	d1052a00 	tstle	r5, r0, lsl #20
    2534:	7ce61c52 	stclvc	12, cr1, [r6], #328	@ 0x148
    2538:	e00e6ae7 	and	r6, lr, r7, ror #21
    253c:	d10d2a00 	tstle	sp, r0, lsl #20
    2540:	42b07ce0 	adcsmi	r7, r0, #224, 24	@ 0xe000
    2544:	1c06d202 	sfmne	f5, 1, [r6], {2}
    2548:	e0066ae7 	and	r6, r6, r7, ror #21
    254c:	6ae0d806 	bvs	0xff83856c
    2550:	d90142b8 	stmdble	r1, {r3, r4, r5, r7, r9, lr}
    2554:	e0001c07 	and	r1, r0, r7, lsl #24
    2558:	46a0d300 	strtmi	sp, [r0], r0, lsl #6
    255c:	1e5b3440 	cdpne	4, 5, cr3, cr11, cr0, {2}
    2560:	4644dcdf 			@ <UNDEFINED> instruction: 0x4644dcdf
    2564:	d0522c00 	subsle	r2, r2, r0, lsl #24
    2568:	f7ff1c20 			@ <UNDEFINED> instruction: 0xf7ff1c20
    256c:	2100ff2c 	tstcs	r0, ip, lsr #30	@ <UNPREDICTABLE>
    2570:	6a2b6321 	bvs	0xadb1fc
    2574:	2b006363 	blcs	0x1b308
    2578:	631cd000 	tstvs	ip, #0
    257c:	62e5622c 	rscvs	r6, r5, #44, 4	@ 0xc0000002
    2580:	77287ee8 	strvc	r7, [r8, -r8, ror #29]!
    2584:	d0014288 	andle	r4, r1, r8, lsl #5
    2588:	75a976a9 	strvc	r7, [r9, #1705]!	@ 0x6a9
    258c:	1c299800 	stcne	8, cr9, [r9], #-0
    2590:	f804f7ff 			@ <UNDEFINED> instruction: 0xf804f7ff
    2594:	61206868 			@ <UNDEFINED> instruction: 0x61206868
    2598:	74e09804 	strbtvc	r9, [r0], #2052	@ 0x804
    259c:	72209802 	eorvc	r9, r0, #131072	@ 0x20000
    25a0:	7830464e 	ldmdavc	r0!, {r1, r2, r3, r6, r9, sl, lr}
    25a4:	68777060 	ldmdavs	r7!, {r5, r6, ip, sp, lr}^
    25a8:	68b06267 	ldmvs	r0!, {r0, r1, r2, r5, r6, r9, sp, lr}
    25ac:	8be86060 	blhi	0xffa1a734
    25b0:	7ca181a0 	stfvcd	f0, [r1], #640	@ 0x280
    25b4:	43487c28 	movtmi	r7, #35880	@ 0x8c28
    25b8:	70a011c0 	adcvc	r1, r0, r0, asr #3
    25bc:	43487c68 	movtmi	r7, #35944	@ 0x8c68
    25c0:	70e011c0 	rscvc	r1, r0, r0, asr #3
    25c4:	20087a21 	andcs	r7, r8, r1, lsr #20
    25c8:	180b5628 	stmdane	fp, {r3, r5, r9, sl, ip, lr}
    25cc:	2300d500 	movwcs	sp, #1280	@ 0x500
    25d0:	2e009e03 	cdpcs	14, 0, cr9, cr0, cr3, {0}
    25d4:	464ed00f 	strbmi	sp, [lr], -pc
    25d8:	77a078b0 			@ <UNDEFINED> instruction: 0x77a078b0
    25dc:	208078f1 	strdcs	r7, [r0], r1
    25e0:	d1004208 	tstle	r0, r8, lsl #4
    25e4:	7a6a77e1 	bvc	0x1aa0570
    25e8:	98031c19 	stmdals	r3, {r0, r3, r4, sl, fp, ip}
    25ec:	6b1b9b01 	blvs	0x6e91f8
    25f0:	fd87f7ff 	stc2	7, cr15, [r7, #1020]	@ 0x3fc
    25f4:	7a6ae004 	bvc	0x1aba60c
    25f8:	1c381c19 	ldcne	12, cr1, [r8], #-100	@ 0xffffff9c
    25fc:	f96cf7ff 			@ <UNDEFINED> instruction: 0xf96cf7ff
    2600:	20806220 	addcs	r6, r0, r0, lsr #4
    2604:	78297020 	stmdavc	r9!, {r5, ip, sp, lr}
    2608:	400820f0 	strdmi	r2, [r8], -r0
    260c:	b0057028 	andlt	r7, r5, r8, lsr #32
    2610:	4680bcff 			@ <UNDEFINED> instruction: 0x4680bcff
    2614:	46924689 	ldrmi	r4, [r2], r9, lsl #13
    2618:	bc01469b 	stclt	6, cr4, [r1], {155}	@ 0x9b
    261c:	00004700 	andeq	r4, r0, r0, lsl #14
    2620:	03007ff0 	movweq	r7, #4080	@ 0xff0
    2624:	000030d0 	ldrdeq	r3, [r0], -r0
    2628:	6c0ab510 	cfstr32vs	mvfx11, [sl], {16}
    262c:	2b807813 	blcs	0xfe020680
    2630:	714bd203 	cmpvc	fp, r3, lsl #4
    2634:	640a1c52 	strvs	r1, [sl], #-3154	@ 0xfffff3ae
    2638:	794be000 	stmdbvc	fp, {sp, lr, pc}^
    263c:	29006a09 	stmdbcs	r0, {r0, r3, r9, fp, sp, lr}
    2640:	2483d00d 	strcs	sp, [r3], #13
    2644:	4222780a 	eormi	r7, r2, #655360	@ 0xa0000
    2648:	7c48d006 	mcrrvc	0, 0, sp, r8, cr6
    264c:	d1034298 			@ <UNDEFINED> instruction: 0xd1034298
    2650:	43022040 	movwmi	r2, #8256	@ 0x2040
    2654:	e002700a 	and	r7, r2, sl
    2658:	29006b49 	stmdbcs	r0, {r0, r3, r6, r8, r9, fp, sp, lr}
    265c:	bc10d1f2 	ldfltd	f5, [r0], {242}	@ 0xf2
    2660:	4700bc01 	strmi	fp, [r0, -r1, lsl #24]
    2664:	1c0db530 	cfstr32ne	mvfx11, [sp], {48}	@ 0x30
    2668:	2c006a2c 			@ <UNDEFINED> instruction: 0x2c006a2c
    266c:	7821d00c 	stmdavc	r1!, {r2, r3, ip, lr, pc}
    2670:	420820c7 	andmi	r2, r8, #199	@ 0xc7
    2674:	2040d002 	subcs	sp, r0, r2
    2678:	70214301 	eorvc	r4, r1, r1, lsl #6
    267c:	f7ff1c20 			@ <UNDEFINED> instruction: 0xf7ff1c20
    2680:	6b64fea2 	blvs	0x1942110
    2684:	d1f22c00 	mvnsle	r2, r0, lsl #24
    2688:	70282000 	eorvc	r2, r8, r0
    268c:	bc01bc30 	stclt	12, cr11, [r1], {48}	@ 0x30
    2690:	46f44700 	ldrbtmi	r4, [r4], r0, lsl #14
    2694:	4a0a2124 	bmi	0x28ab2c
    2698:	f0006813 			@ <UNDEFINED> instruction: 0xf0006813
    269c:	c008f806 	andgt	pc, r8, r6, lsl #16
    26a0:	1e491d12 	mcrne	13, 2, r1, cr9, cr2, {0}
    26a4:	4760dcf8 			@ <UNDEFINED> instruction: 0x4760dcf8
    26a8:	b4017813 	strlt	r7, [r1], #-2067	@ 0xfffff7ed
    26ac:	d1050e50 	tstle	r5, r0, asr lr
    26b0:	42824803 	addmi	r4, r2, #196608	@ 0x30000
    26b4:	0b90d301 	bleq	0xfe4372c0
    26b8:	2300d000 	movwcs	sp, #0
    26bc:	4770bc01 	ldrbmi	fp, [r0, -r1, lsl #24]!
    26c0:	00003738 	andeq	r3, r0, r8, lsr r7
    26c4:	1c536c0a 	mrrcne	12, 0, r6, r3, cr10
    26c8:	7813640b 	ldmdavc	r3, {r0, r1, r3, sl, sp, lr}
    26cc:	b500e7ed 	strlt	lr, [r0, #-2029]	@ 0xfffff813
    26d0:	78d06c0a 	ldmvc	r0, {r1, r3, sl, fp, sp, lr}^
    26d4:	78930200 	ldmvc	r3, {r9}
    26d8:	02004318 	andeq	r4, r0, #24, 6	@ 0x60000000
    26dc:	43187853 	tstmi	r8, #5439488	@ 0x530000
    26e0:	f7ff0200 			@ <UNDEFINED> instruction: 0xf7ff0200
    26e4:	4318ffe1 	tstmi	r8, #900	@ 0x384	@ <UNPREDICTABLE>
    26e8:	bc016408 	cfstrslt	mvf6, [r1], {8}
    26ec:	788a4700 	stmvc	sl, {r8, r9, sl, lr}
    26f0:	d2082a03 	andle	r2, r8, #12288	@ 0x3000
    26f4:	188b0092 	stmne	fp, {r1, r4, r7}
    26f8:	1d126c0a 	ldcne	12, cr6, [r2, #-40]	@ 0xffffffd8
    26fc:	788a645a 	stmvc	sl, {r1, r3, r4, r6, sl, sp, lr}
    2700:	708a1c52 	addvc	r1, sl, r2, asr ip
    2704:	e7ade7e3 	str	lr, [sp, r3, ror #15]!
    2708:	2a00788a 	bcs	0x20938
    270c:	1e52d005 	cdpne	0, 5, cr13, cr2, cr5, {0}
    2710:	0092708a 	addseq	r7, r2, sl, lsl #1
    2714:	6c5a188b 	mrrcvs	8, 8, r1, sl, cr11
    2718:	4770640a 	ldrbmi	r6, [r0, -sl, lsl #8]!
    271c:	6c0ab500 	cfstr32vs	mvfx11, [sl], {-0}
    2720:	2b007813 	blcs	0x20774
    2724:	1c52d102 	ldfnep	f5, [r2], {2}
    2728:	e7d1640a 	ldrb	r6, [r1, sl, lsl #8]
    272c:	1c5b78cb 	mrrcne	8, 12, r7, fp, cr11
    2730:	469c70cb 	ldrmi	r7, [ip], fp, asr #1
    2734:	ffc6f7ff 			@ <UNDEFINED> instruction: 0xffc6f7ff
    2738:	d200459c 	andle	r4, r0, #156, 10	@ 0x27000000
    273c:	2300e7c8 	movwcs	lr, #1992	@ 0x7c8
    2740:	1d5270cb 	ldclne	0, cr7, [r2, #-812]	@ 0xfffffcd4
    2744:	bc01640a 	cfstrslt	mvf6, [r1], {10}
    2748:	46f44700 	ldrbtmi	r4, [r4], r0, lsl #14
    274c:	ffbaf7ff 			@ <UNDEFINED> instruction: 0xffbaf7ff
    2750:	4760774b 	strbmi	r7, [r0, -fp, asr #14]!
    2754:	f7ff46f4 			@ <UNDEFINED> instruction: 0xf7ff46f4
    2758:	005bffb5 	ldrheq	pc, [fp], #-245	@ 0xffffff0b	@ <UNPREDICTABLE>
    275c:	8bc28383 	blhi	0xff0a3570
    2760:	0a1b4353 	beq	0x6d34b4
    2764:	47608403 	strbmi	r8, [r0, -r3, lsl #8]!
    2768:	f7ff46f4 			@ <UNDEFINED> instruction: 0xf7ff46f4
    276c:	728bffab 	addvc	pc, fp, #684	@ 0x2ac
    2770:	220c780b 	andcs	r7, ip, #720896	@ 0xb0000
    2774:	700b4313 	andvc	r4, fp, r3, lsl r3
    2778:	46f44760 	ldrbtmi	r4, [r4], r0, ror #14
    277c:	78136c0a 	ldmdavc	r3, {r1, r3, sl, fp, sp, lr}
    2780:	640a1c52 	strvs	r1, [sl], #-3154	@ 0xfffff3ae
    2784:	18d2005a 	ldmne	r2, {r1, r3, r4, r6}^
    2788:	6b030092 	blvs	0xc29d8
    278c:	681318d2 	ldmdavs	r3, {r1, r4, r6, r7, fp, ip}
    2790:	ff8bf7ff 			@ <UNDEFINED> instruction: 0xff8bf7ff
    2794:	6853624b 	ldmdavs	r3, {r0, r1, r3, r6, r9, sp, lr}^
    2798:	ff87f7ff 			@ <UNDEFINED> instruction: 0xff87f7ff
    279c:	6893628b 	ldmvs	r3, {r0, r1, r3, r7, r9, sp, lr}
    27a0:	ff83f7ff 			@ <UNDEFINED> instruction: 0xff83f7ff
    27a4:	476062cb 	strbmi	r6, [r0, -fp, asr #5]!
    27a8:	f7ff46f4 			@ <UNDEFINED> instruction: 0xf7ff46f4
    27ac:	748bff8b 	strvc	pc, [fp], #3979	@ 0xf8b
    27b0:	2203780b 	andcs	r7, r3, #720896	@ 0xb0000
    27b4:	700b4313 	andvc	r4, fp, r3, lsl r3
    27b8:	46f44760 	ldrbtmi	r4, [r4], r0, ror #14
    27bc:	ff82f7ff 			@ <UNDEFINED> instruction: 0xff82f7ff
    27c0:	750b3b40 	strvc	r3, [fp, #-2880]	@ 0xfffff4c0
    27c4:	2203780b 	andcs	r7, r3, #720896	@ 0xb0000
    27c8:	700b4313 	andvc	r4, fp, r3, lsl r3
    27cc:	46f44760 	ldrbtmi	r4, [r4], r0, ror #14
    27d0:	ff78f7ff 			@ <UNDEFINED> instruction: 0xff78f7ff
    27d4:	738b3b40 	orrvc	r3, fp, #64, 22	@ 0x10000
    27d8:	220c780b 	andcs	r7, ip, #720896	@ 0xb0000
    27dc:	700b4313 	andvc	r4, fp, r3, lsl r3
    27e0:	46f44760 	ldrbtmi	r4, [r4], r0, ror #14
    27e4:	ff6ef7ff 			@ <UNDEFINED> instruction: 0xff6ef7ff
    27e8:	780b73cb 	stmdavc	fp, {r0, r1, r3, r6, r7, r8, r9, ip, sp, lr}
    27ec:	4313220c 	tstmi	r3, #12, 4	@ 0xc0000000
    27f0:	4760700b 	strbmi	r7, [r0, -fp]!
    27f4:	f7ff46f4 			@ <UNDEFINED> instruction: 0xf7ff46f4
    27f8:	764bff65 	strbvc	pc, [fp], -r5, ror #30	@ <UNPREDICTABLE>
    27fc:	d1002b00 	tstle	r0, r0, lsl #22
    2800:	4760758b 	strbmi	r7, [r0, -fp, lsl #11]!
    2804:	f7ff46f4 			@ <UNDEFINED> instruction: 0xf7ff46f4
    2808:	76cbff5d 			@ <UNDEFINED> instruction: 0x76cbff5d
    280c:	46f44760 	ldrbtmi	r4, [r4], r0, ror #14
    2810:	ff58f7ff 			@ <UNDEFINED> instruction: 0xff58f7ff
    2814:	2b0075cb 	blcs	0x1ff48
    2818:	758bd100 	strvc	sp, [fp, #256]	@ 0x100
    281c:	46f44760 	ldrbtmi	r4, [r4], r0, ror #14
    2820:	ff50f7ff 			@ <UNDEFINED> instruction: 0xff50f7ff
    2824:	42987e08 	addsmi	r7, r8, #8, 28	@ 0x80
    2828:	760bd004 	strvc	sp, [fp], -r4
    282c:	220f780b 	andcs	r7, pc, #720896	@ 0xb0000
    2830:	700b4313 	andvc	r4, fp, r3, lsl r3
    2834:	46f44760 	ldrbtmi	r4, [r4], r0, ror #14
    2838:	ff44f7ff 			@ <UNDEFINED> instruction: 0xff44f7ff
    283c:	730b3b40 	movwvc	r3, #47936	@ 0xbb40
    2840:	220c780b 	andcs	r7, ip, #720896	@ 0xb0000
    2844:	700b4313 	andvc	r4, fp, r3, lsl r3
    2848:	46f44760 	ldrbtmi	r4, [r4], r0, ror #14
    284c:	78136c0a 	ldmdavc	r3, {r1, r3, sl, fp, sp, lr}
    2850:	48031c52 	stmdami	r3, {r1, r4, r6, sl, fp, ip}
    2854:	f7ff18c0 			@ <UNDEFINED> instruction: 0xf7ff18c0
    2858:	7003ff36 	andvc	pc, r3, r6, lsr pc	@ <UNPREDICTABLE>
    285c:	00004760 	andeq	r4, r0, r0, ror #14
    2860:	04000060 	streq	r0, [r0], #-96	@ 0xffffffa0
    2864:	1c292620 	stcne	6, cr2, [r9], #-128	@ 0xffffff80
    2868:	086d4051 	stmdaeq	sp!, {r0, r4, r6, lr}^
    286c:	d3000849 	movwle	r0, #2121	@ 0x849
    2870:	08524045 	ldmdaeq	r2, {r0, r2, r6, lr}^
    2874:	d1f61e76 	mvnsle	r1, r6, ror lr
    2878:	b5544770 	ldrblt	r4, [r4, #-1904]	@ 0xfffff890
    287c:	657b468c 	ldrbvs	r4, [fp, #-1676]!	@ 0xfffff974
    2880:	6bb96c7b 	blvs	0xfee5da74
    2884:	10891ac9 	addne	r1, r9, r9, asr #21
    2888:	2989dd17 	stmibcs	r9, {r0, r1, r2, r4, r8, sl, fp, ip, lr, pc}
    288c:	2189dd00 	orrcs	sp, r9, r0, lsl #26
    2890:	8c3d687c 	ldchi	8, cr6, [sp], #-496	@ 0xfffffe10
    2894:	46616539 			@ <UNDEFINED> instruction: 0x46616539
    2898:	1c64434c 	stclne	3, cr4, [r4], #-304	@ 0xfffffed0
    289c:	4062681a 	rsbmi	r6, r2, sl, lsl r8
    28a0:	404a4259 	submi	r4, sl, r9, asr r2
    28a4:	404a6d79 	submi	r6, sl, r9, ror sp
    28a8:	847dc304 	ldrbthi	ip, [sp], #-772	@ 0xfffffcfc
    28ac:	ffdaf7ff 			@ <UNDEFINED> instruction: 0xffdaf7ff
    28b0:	1e496d39 	mcrne	13, 2, r6, cr9, cr9, {1}
    28b4:	843dd1ee 	ldrthi	sp, [sp], #-494	@ 0xfffffe12
    28b8:	bd54607c 	ldcllt	0, cr6, [r4, #-496]	@ 0xfffffe10
    28bc:	f000b500 			@ <UNDEFINED> instruction: 0xf000b500
    28c0:	bc02f8f2 	stclt	8, cr15, [r2], {242}	@ 0xf2
    28c4:	8931468e 	ldmdbhi	r1!, {r1, r2, r3, r7, r9, sl, lr}
    28c8:	d2fc0a09 	rscsle	r0, ip, #36864	@ 0x9000
    28cc:	b5fa4770 	ldrblt	r4, [sl, #1904]!	@ 0x770
    28d0:	a2cb23df 	sbcge	r2, fp, #2080374787	@ 0x7c000003
    28d4:	f8e6f000 			@ <UNDEFINED> instruction: 0xf8e6f000
    28d8:	24ff1c07 	ldrbtcs	r1, [pc], #3079	@ 0x28e0
    28dc:	f95ef7fe 			@ <UNDEFINED> instruction: 0xf95ef7fe
    28e0:	0d3cd045 	ldceq	0, cr13, [ip, #-276]!	@ 0xfffffeec
    28e4:	402323e8 	eormi	r2, r3, r8, ror #7
    28e8:	d1402b20 	cmple	r0, r0, lsr #22
    28ec:	29012400 	stmdbcs	r1, {sl, sp}
    28f0:	2902d003 	stmdbcs	r2, {r0, r1, ip, lr, pc}
    28f4:	4cc8dc3b 	stclmi	12, cr13, [r8], {59}	@ 0x3b
    28f8:	877c430c 	ldrbhi	r4, [ip, -ip, lsl #6]!
    28fc:	61386a38 	teqvs	r8, r8, lsr sl
    2900:	1a246a7c 	bne	0x91d2f8
    2904:	401c4bc0 	andsmi	r4, ip, r0, asr #23
    2908:	f7fe60fc 			@ <UNDEFINED> instruction: 0xf7fe60fc
    290c:	d02ef947 	eorle	pc, lr, r7, asr #18
    2910:	89604cbe 	stmdbhi	r0!, {r1, r2, r3, r4, r5, r7, sl, fp, lr}^
    2914:	43108ae2 	tstmi	r0, #925696	@ 0xe2000
    2918:	43108c62 	tstmi	r0, #25088	@ 0x6200
    291c:	43108de2 	tstmi	r0, #14464	@ 0x3880
    2920:	d2240c00 	eorle	r0, r4, #0, 24
    2924:	7fb84eba 	svcvc	0x00b84eba
    2928:	0f400700 	svceq	0x00400700
    292c:	8f7a8831 	svchi	0x007a8831
    2930:	d0022a00 	andle	r2, r2, r0, lsl #20
    2934:	0fc007c0 	svceq	0x00c007c0
    2938:	72387d39 	eorsvc	r7, r8, #3648	@ 0xe40
    293c:	4bb67139 	blmi	0xfed9ee28
    2940:	49fa0c1b 	ldmibmi	sl!, {r0, r1, r3, r4, sl, fp}^
    2944:	4cfb2a00 	vldmiami	fp!, {s5-s4}
    2948:	49f9d102 	ldmibmi	r9!, {r1, r8, ip, lr, pc}^
    294c:	4cfa4bae 	vldmiami	sl!, {d20-<overflow reg d42>}
    2950:	873b87f9 			@ <UNDEFINED> instruction: 0x873b87f9
    2954:	69b9643c 	ldmibvs	r9!, {r2, r3, r4, r5, sl, sp, lr}
    2958:	7f396039 	svcvc	0x00396039
    295c:	1c347039 	ldcne	0, cr7, [r4], #-228	@ 0xffffff1c
    2960:	d3050840 	movwle	r0, #22592	@ 0x5840
    2964:	297378e1 	ldmdbcs	r3!, {r0, r5, r6, r7, fp, ip, sp, lr}^
    2968:	1ca4d101 	stfned	f5, [r4], #4
    296c:	e08ee7f8 	strd	lr, [lr], r8
    2970:	68fdd1fb 	ldmvs	sp!, {r0, r1, r3, r4, r5, r6, r7, r8, ip, lr, pc}^
    2974:	383408a8 	ldmdacc	r4!, {r3, r5, r7, fp}
    2978:	19496939 	stmdbne	r9, {r0, r3, r4, r5, r8, fp, sp, lr}^
    297c:	49a260f9 	stmibmi	r2!, {r0, r3, r4, r5, r6, r7, sp, lr}
    2980:	d1fd1e49 	mvnsle	r1, r9, asr #28
    2984:	ff9af7ff 			@ <UNDEFINED> instruction: 0xff9af7ff
    2988:	71798871 	cmnvc	r9, r1, ror r8
    298c:	88f288b1 	ldmhi	r2!, {r0, r4, r5, r7, fp, pc}^
    2990:	2b008f7b 	blcs	0x26784
    2994:	21ffd001 	mvnscs	sp, r1
    2998:	71b922ff 			@ <UNDEFINED> instruction: 0x71b922ff
    299c:	240271fa 	strcs	r7, [r2], #-506	@ 0xfffffe06
    29a0:	693b46a4 	ldmdbvs	fp!, {r2, r5, r7, r9, sl, lr}
    29a4:	1ac96a39 	bne	0xff25d290
    29a8:	8fb80889 	svchi	0x00b80889
    29ac:	681ad217 	ldmdavs	sl, {r0, r1, r2, r4, r9, ip, lr, pc}
    29b0:	8f3d8ff8 	svchi	0x003d8ff8
    29b4:	ff56f7ff 			@ <UNDEFINED> instruction: 0xff56f7ff
    29b8:	6839873d 	ldmdavs	r9!, {r0, r2, r3, r4, r5, r8, r9, sl, pc}
    29bc:	434148e1 	movtmi	r4, #6369	@ 0x18e1
    29c0:	60391c49 	eorsvs	r1, r9, r9, asr #24
    29c4:	40486818 	submi	r6, r8, r8, lsl r8
    29c8:	1a5a6a39 	bne	0x169d2b4
    29cc:	185249f1 	ldmdane	r2, {r0, r4, r5, r6, r7, r8, fp, lr}^
    29d0:	6c3a4251 	lfmvs	f4, 4, [sl], #-324	@ 0xfffffebc
    29d4:	40484051 	submi	r4, r8, r1, asr r0
    29d8:	87ba0c02 	ldrhi	r0, [sl, r2, lsl #24]!
    29dc:	f7ff4e8c 			@ <UNDEFINED> instruction: 0xf7ff4e8c
    29e0:	6a39ff72 	bvs	0xe827b0
    29e4:	d0184299 	mulsle	r8, r9, r2
    29e8:	1a5c46a6 	bne	0x1714488
    29ec:	8f791ea4 	svchi	0x00791ea4
    29f0:	d0002900 	andle	r2, r0, r0, lsl #18
    29f4:	49e71ea4 	stmibmi	r7!, {r2, r5, r7, r9, sl, fp, ip}^
    29f8:	7a3a1864 	bvc	0xe88b90
    29fc:	08521c35 	ldmdaeq	r2, {r0, r2, r4, r5, sl, fp, ip}^
    2a00:	8869d305 	stmdahi	r9!, {r0, r2, r8, r9, ip, lr, pc}^
    2a04:	04094061 	streq	r4, [r9], #-97	@ 0xffffff9f
    2a08:	1cadd141 	stfned	f5, [sp], #260	@ 0x104
    2a0c:	d1fce7f7 	ldrshle	lr, [ip, #119]!	@ 0x77
    2a10:	45624674 	strbmi	r4, [r2, #-1652]!	@ 0xfffff98c
    2a14:	2000d101 	andcs	sp, r0, r1, lsl #2
    2a18:	f000e03a 			@ <UNDEFINED> instruction: 0xf000e03a
    2a1c:	2c00f844 	stccs	8, cr15, [r0], {68}	@ 0x44
    2a20:	1c9bd00c 	ldcne	0, cr13, [fp], {12}
    2a24:	29008f79 	stmdbcs	r0, {r0, r3, r4, r5, r6, r8, r9, sl, fp, pc}
    2a28:	1c9bd000 	ldcne	0, cr13, [fp], {0}
    2a2c:	d1022c02 	tstle	r2, r2, lsl #24
    2a30:	429968f9 	addsmi	r6, r9, #16318464	@ 0xf90000
    2a34:	2065d1b6 	strhtcs	sp, [r5], #-22	@ 0xffffffea
    2a38:	e7d01e64 	ldrb	r1, [r0, r4, ror #28]
    2a3c:	f7ff2401 			@ <UNDEFINED> instruction: 0xf7ff2401
    2a40:	7a3aff42 	bvc	0xec2750
    2a44:	08521c33 	ldmdaeq	r2, {r0, r1, r4, r5, sl, fp, ip}^
    2a48:	8859d309 	ldmdahi	r9, {r0, r3, r8, r9, ip, lr, pc}^
    2a4c:	d0042975 	andle	r2, r4, r5, ror r9
    2a50:	d11c2865 	tstle	ip, r5, ror #16
    2a54:	d11a2974 	tstle	sl, r4, ror r9
    2a58:	1c9b2400 	cfldrsne	mvf2, [fp], {0}
    2a5c:	d1fce7f3 	ldrshle	lr, [ip, #115]!	@ 0x73
    2a60:	d0052866 	andle	r2, r5, r6, ror #16
    2a64:	d0002c00 	andle	r2, r0, r0, lsl #24
    2a68:	f0002066 			@ <UNDEFINED> instruction: 0xf0002066
    2a6c:	e7e5f81c 			@ <UNDEFINED> instruction: 0xe7e5f81c
    2a70:	d00c2c00 	andle	r2, ip, r0, lsl #24
    2a74:	8f3d8ff8 	svchi	0x003d8ff8
    2a78:	f7ff687a 			@ <UNDEFINED> instruction: 0xf7ff687a
    2a7c:	4e64fef3 	mcrmi	14, 3, pc, cr4, cr3, {7}	@ <UNPREDICTABLE>
    2a80:	f7ff1c28 			@ <UNDEFINED> instruction: 0xf7ff1c28
    2a84:	2100ff1b 	tstcs	r0, fp, lsl pc	@ <UNPREDICTABLE>
    2a88:	1c04468c 	stcne	6, cr4, [r4], {140}	@ 0x8c
    2a8c:	2001e7b5 			@ <UNDEFINED> instruction: 0x2001e7b5
    2a90:	63f863b8 	mvnsvs	r6, #184, 6	@ 0xe0000002
    2a94:	1c396438 	cfldrsne	mvf6, [r9], #-224	@ 0xffffff20
    2a98:	c7013114 	smladgt	r1, r4, r1, r3
    2a9c:	d1fc42b9 	ldrhle	r4, [ip, #41]!	@ 0x29
    2aa0:	bc04bcfa 	stclt	12, cr11, [r4], {250}	@ 0xfa
    2aa4:	21964710 	orrscs	r4, r6, r0, lsl r7
    2aa8:	d1fd1e49 	mvnsle	r1, r9, asr #28
    2aac:	81706030 	cmnhi	r0, r0, lsr r0
    2ab0:	29008f79 	stmdbcs	r0, {r0, r3, r4, r5, r6, r8, r9, sl, fp, pc}
    2ab4:	49b8d100 	ldmibmi	r8!, {r8, ip, lr, pc}
    2ab8:	47708131 			@ <UNDEFINED> instruction: 0x47708131
    2abc:	48b7b500 	ldmmi	r7!, {r8, sl, ip, sp, pc}
    2ac0:	29017801 	stmdbcs	r1, {r0, fp, ip, sp, lr}
    2ac4:	7ab8d10e 	bvc	0xfee36f04
    2ac8:	d30b0640 	movwle	r0, #46656	@ 0xb640
    2acc:	7cf97cb8 	ldclvc	12, cr7, [r9], #736	@ 0x2e0
    2ad0:	d1094308 	tstle	r9, r8, lsl #6
    2ad4:	49af6bb8 	stmibmi	pc!, {r3, r4, r5, r7, r8, r9, fp, sp, lr}	@ <UNPREDICTABLE>
    2ad8:	da031a09 	ble	0xc9304
    2adc:	74b82078 	ldrtvc	r2, [r8], #120	@ 0x78
    2ae0:	e2a94899 	adc	r4, r9, #10027008	@ 0x990000
    2ae4:	bd002000 	stclt	0, cr2, [r0, #-0]
    2ae8:	035168ba 	cmpeq	r1, #12189696	@ 0xba0000
    2aec:	7d380f89 	ldcvc	15, cr0, [r8, #-548]!	@ 0xfffffddc
    2af0:	1e491cc0 	cdpne	12, 4, cr1, cr9, cr0, {6}
    2af4:	7538d5fc 	ldrvc	sp, [r8, #-1532]!	@ 0xfffffa04
    2af8:	06810880 	streq	r0, [r1], r0, lsl #17
    2afc:	40510312 	subsmi	r0, r1, r2, lsl r3
    2b00:	404817c9 	submi	r1, r8, r9, asr #15
    2b04:	4001211f 	andmi	r2, r1, pc, lsl r1
    2b08:	025068ba 	subseq	r6, r0, #12189696	@ 0xba0000
    2b0c:	28070f40 	stmdacs	r7, {r6, r8, r9, sl, fp}
    2b10:	2100db05 	tstcs	r0, r5, lsl #22
    2b14:	0f400310 	svceq	0x00400310
    2b18:	db002807 	blle	0xcb3c
    2b1c:	221f2000 	andscs	r2, pc, #0
    2b20:	fe4cf7fd 	mcr2	7, 2, pc, cr12, cr13, {7}	@ <UNPREDICTABLE>
    2b24:	1e407cb8 	mcrne	12, 2, r7, cr0, cr8, {5}
    2b28:	74b8db01 	ldrtvc	sp, [r8], #2817	@ 0xb01
    2b2c:	2005d1da 	ldrdcs	sp, [r5], -sl
    2b30:	bd0074f8 	cfstrslt	mvf7, [r0, #-992]	@ 0xfffffc20
    2b34:	b500b4f0 	strlt	fp, [r0, #-1264]	@ 0xfffffb10
    2b38:	4c354f99 	ldcmi	15, cr4, [r5], #-612	@ 0xfffffd9c
    2b3c:	49806cf8 	stmibmi	r0, {r3, r4, r5, r6, r7, sl, fp, sp, lr}
    2b40:	1c404348 	mcrrne	3, 4, r4, r0, cr8
    2b44:	e28064f8 	add	r6, r0, #248, 8	@ 0xf8000000
    2b48:	21e06cf8 	strdcs	r6, [r0, #200]!	@ 0xc8
    2b4c:	21a04388 	lslcs	r4, r8, #7
    2b50:	23804048 	orrcs	r4, r0, #72	@ 0x48
    2b54:	4398021b 	orrsmi	r0, r8, #-1342177279	@ 0xb0000001
    2b58:	780a4990 	stmdavc	sl, {r4, r7, r8, fp, lr}
    2b5c:	d0032a01 	andle	r2, r3, r1, lsl #20
    2b60:	6a4a492e 	bvs	0x1295020
    2b64:	d1001c52 	tstle	r0, r2, asr ip
    2b68:	60384318 	eorsvs	r4, r8, r8, lsl r3
    2b6c:	7bbe7bfd 	blvc	0xfefa1b68
    2b70:	28007b78 	stmdacs	r0, {r3, r4, r5, r6, r8, r9, fp, ip, sp, lr}
    2b74:	f000d11b 			@ <UNDEFINED> instruction: 0xf000d11b
    2b78:	7b3bf8f1 	blvc	0xf00f44
    2b7c:	2e028a20 	vmlacs.f32	s16, s4, s1
    2b80:	1e5bd104 	lognee	f5, f4
    2b84:	2600d510 			@ <UNDEFINED> instruction: 0x2600d510
    2b88:	e00a2306 	and	r2, sl, r6, lsl #6
    2b8c:	d1042e01 	tstle	r4, r1, lsl #28
    2b90:	d5091e5b 	strle	r1, [r9, #-3675]	@ 0xfffff1a5
    2b94:	23062602 	movwcs	r2, #26114	@ 0x6602
    2b98:	1e5be003 	cdpne	0, 5, cr14, cr11, cr3, {0}
    2b9c:	2601d504 	strcs	sp, [r1], -r4, lsl #10
    2ba0:	491c231e 	ldmdbmi	ip, {r1, r2, r3, r4, r8, r9, sp}
    2ba4:	25006379 	strcs	r6, [r0, #-889]	@ 0xfffffc87
    2ba8:	f000733b 			@ <UNDEFINED> instruction: 0xf000733b
    2bac:	2d00f8db 	stccs	8, cr15, [r0, #-876]	@ 0xfffffc94
    2bb0:	613dd136 	teqvs	sp, r6, lsr r1
    2bb4:	4a6472bd 	bmi	0x191f6b0
    2bb8:	4a7663ba 	bmi	0x1d9baa8
    2bbc:	647a63fa 	ldrbtvs	r6, [sl], #-1018	@ 0xfffffc06
    2bc0:	73fa2201 	mvnsvc	r2, #268435456	@ 0x10000000
    2bc4:	2e0073be 	mcrcs	3, 0, r7, cr0, cr14, {5}
    2bc8:	22c0d10b 	sbccs	sp, r0, #-1073741822	@ 0xc0000002
    2bcc:	82a20212 	adchi	r0, r2, #536870913	@ 0x20000001
    2bd0:	63656b21 	cmnvs	r5, #33792	@ 0x8400
    2bd4:	21078725 	tstcs	r7, r5, lsr #14
    2bd8:	21ad8421 			@ <UNDEFINED> instruction: 0x21ad8421
    2bdc:	84390149 	ldrthi	r0, [r9], #-329	@ 0xfffffeb7
    2be0:	2e01e0b9 	mcrcs	0, 0, lr, cr1, cr9, {5}
    2be4:	82a5d106 	adchi	sp, r5, #-2147483647	@ 0x80000001
    2be8:	49074a58 	stmdbmi	r7, {r3, r4, r6, r9, fp, lr}
    2bec:	81658122 	cmnhi	r5, r2, lsr #2
    2bf0:	e7f46025 	ldrb	r6, [r4, r5, lsr #32]!
    2bf4:	4ada82a5 	bmi	0xff6a3690
    2bf8:	49070c12 	stmdbmi	r7, {r1, r4, sl, fp}
    2bfc:	e7f50c09 	ldrb	r0, [r5, r9, lsl #24]!
    2c00:	e129f003 	msr	CPSR_fc, r3
    2c04:	e12fff1e 	bx	lr
    2c08:	0003fff8 	strdeq	pc, [r3], -r8
    2c0c:	040000b0 	streq	r0, [r0], #-176	@ 0xffffff50
    2c10:	04000120 	streq	r0, [r0], #-288	@ 0xfffffee0
    2c14:	0000301d 	andeq	r3, r0, sp, lsl r0
    2c18:	c3871089 	orrgt	r1, r7, #137	@ 0x89
    2c1c:	03000064 	movweq	r0, #100	@ 0x64
    2c20:	d1192d01 	tstle	r9, r1, lsl #26
    2c24:	f89af000 			@ <UNDEFINED> instruction: 0xf89af000
    2c28:	80592180 	subshi	r2, r9, r0, lsl #3
    2c2c:	430a881a 	movwmi	r8, #43034	@ 0xa81a
    2c30:	811d801a 	tsthi	sp, sl, lsl r0
    2c34:	d1052e00 	tstle	r5, r0, lsl #28
    2c38:	84212147 	strthi	r2, [r1], #-327	@ 0xfffffeb9
    2c3c:	e007a2c9 	and	sl, r7, r9, asr #5
    2c40:	e00349c7 	and	r4, r3, r7, asr #19
    2c44:	d1fb2e01 	mvnsle	r2, r1, lsl #28
    2c48:	0c094940 			@ <UNDEFINED> instruction: 0x0c094940
    2c4c:	a2558121 	subsge	r8, r5, #1073741832	@ 0x40000008
    2c50:	73f92102 	mvnsvc	r2, #-2147483648	@ 0x80000000
    2c54:	e07e637a 	rsbs	r6, lr, sl, ror r3
    2c58:	d1002d02 	tstle	r0, r2, lsl #26
    2c5c:	f7ffe07b 			@ <UNDEFINED> instruction: 0xf7ffe07b
    2c60:	2d03ff2d 	stccs	15, cr15, [r3, #-180]	@ 0xffffff4c
    2c64:	2800d002 	stmdacs	r0, {r1, ip, lr, pc}
    2c68:	e075d075 	rsbs	sp, r5, r5, ror r0
    2c6c:	f876f000 			@ <UNDEFINED> instruction: 0xf876f000
    2c70:	1e406b38 	vmovne.8	d0[1], r6
    2c74:	2e00d509 	cfsh32cs	mvfx13, mvfx0, #9
    2c78:	8f21d104 	svchi	0x0021d104
    2c7c:	40112230 	andsmi	r2, r1, r0, lsr r2
    2c80:	e780d004 	str	sp, [r0, r4]
    2c84:	d1852e01 	orrle	r2, r5, r1, lsl #28
    2c88:	6338e789 	teqvs	r8, #35913728	@ 0x2240000
    2c8c:	81182001 	tsthi	r8, r1
    2c90:	28007c78 	stmdacs	r0, {r3, r4, r5, r6, sl, fp, ip, sp, lr}
    2c94:	2e00d15c 	mcrcs	1, 0, sp, cr0, cr12, {2}
    2c98:	483fd129 	ldmdami	pc!, {r0, r3, r5, r8, ip, lr, pc}	@ <UNPREDICTABLE>
    2c9c:	49f80c00 	ldmibmi	r8!, {sl, fp}^
    2ca0:	f7ff4b26 			@ <UNDEFINED> instruction: 0xf7ff4b26
    2ca4:	647bfdea 	ldrbtvs	pc, [fp], #-3562	@ 0xfffff216	@ <UNPREDICTABLE>
    2ca8:	428b6bf9 	addmi	r6, fp, #254976	@ 0x3e400
    2cac:	6bb8d153 	blvs	0xfee37200
    2cb0:	d1504058 	cmple	r0, r8, asr r0
    2cb4:	880a1f09 	stmdahi	sl, {r0, r3, r8, r9, sl, fp, ip}
    2cb8:	42938c7b 	addsmi	r8, r3, #31488	@ 0x7b00
    2cbc:	6008d114 	andvs	sp, r8, r4, lsl r1
    2cc0:	4af668b9 	bmi	0xffd9cfac
    2cc4:	42914011 	addsmi	r4, r1, #17
    2cc8:	7ab9d10e 	bvc	0xfee77108
    2ccc:	18897a3a 	stmne	r9, {r1, r3, r4, r5, r9, fp, ip, sp, lr}
    2cd0:	18897a7a 	stmne	r9, {r1, r3, r4, r5, r6, r9, fp, ip, sp, lr}
    2cd4:	1a897afa 	bne	0xfe2618c4
    2cd8:	d1050649 	tstle	r5, r9, asr #12
    2cdc:	210148f0 	strdcs	r4, [r1, -r0]
    2ce0:	f99df000 			@ <UNDEFINED> instruction: 0xf99df000
    2ce4:	d0332800 	eorsle	r2, r3, r0, lsl #16
    2ce8:	73f82000 	mvnsvc	r2, #0
    2cec:	6bfae033 	blvs	0xffebadc0
    2cf0:	42936c7b 	addsmi	r6, r3, #31488	@ 0x7b00
    2cf4:	480ed02f 	stmdami	lr, {r0, r1, r2, r3, r5, ip, lr, pc}
    2cf8:	2e024b0f 	vmlacs.f64	d4, d2, d15
    2cfc:	480bd101 	stmdami	fp, {r0, r8, ip, lr, pc}
    2d00:	49104b0c 	ldmdbmi	r0, {r2, r3, r8, r9, fp, lr}
    2d04:	fdb9f7ff 	ldc2	7, cr15, [r9, #1020]!	@ 0x3fc
    2d08:	d10c429a 			@ <UNDEFINED> instruction: 0xd10c429a
    2d0c:	f7ff69fa 			@ <UNDEFINED> instruction: 0xf7ff69fa
    2d10:	843dfda9 	ldrthi	pc, [sp], #-3497	@ 0xfffff257	@ <UNPREDICTABLE>
    2d14:	210448e3 	smlattcs	r4, r3, r8, r4
    2d18:	1b897bbe 	blne	0xfe261c18
    2d1c:	f97ff000 			@ <UNDEFINED> instruction: 0xf97ff000
    2d20:	d1e12800 	mvnle	r2, r0, lsl #16
    2d24:	647b6bfb 	ldrbtvs	r6, [fp], #-3067	@ 0xfffff405
    2d28:	0000e015 	andeq	lr, r0, r5, lsl r0
    2d2c:	0000c37b 	andeq	ip, r0, fp, ror r3
    2d30:	0000a517 	andeq	sl, r0, r7, lsl r5
    2d34:	43202f2f 			@ <UNDEFINED> instruction: 0x43202f2f
    2d38:	6465646f 	strbtvs	r6, [r5], #-1135	@ 0xfffffb91
    2d3c:	20796220 	rsbscs	r6, r9, r0, lsr #4
    2d40:	6177614b 	cmnvs	r7, fp, asr #2
    2d44:	6f646573 	svcvs	0x00646573
    2d48:	02000000 	andeq	r0, r0, #0
    2d4c:	60032003 	andvs	r2, r3, r3
    2d50:	74782004 	ldrbtvc	r2, [r8], #-4
    2d54:	200073f8 	strdcs	r7, [r0], -r8
    2d58:	4718bcf8 			@ <UNDEFINED> instruction: 0x4718bcf8
    2d5c:	4bca2000 	blmi	0xff28ad64
    2d60:	47708118 			@ <UNDEFINED> instruction: 0x47708118
    2d64:	e7fa2001 	ldrb	r2, [sl, r1]!
    2d68:	21004bcb 	smlabtcs	r0, fp, fp, r4
    2d6c:	477073d9 			@ <UNDEFINED> instruction: 0x477073d9
    2d70:	88114ac1 	ldmdahi	r1, {r0, r6, r7, r9, fp, lr}
    2d74:	7b984bc8 	blvc	0xfe615c9c
    2d78:	d1042801 	tstle	r4, r1, lsl #16
    2d7c:	09c08910 	stmibeq	r0, {r4, r8, fp, pc}^
    2d80:	6b58d26f 	blvs	0x1637744
    2d84:	28024687 	stmdacs	r2, {r0, r1, r2, r7, r9, sl, lr}
    2d88:	8c11d0fb 	ldchi	0, cr13, [r1], {251}	@ 0xfb
    2d8c:	20078411 	andcs	r8, r7, r1, lsl r4
    2d90:	e7f64001 	ldrb	r4, [r6, r1]!
    2d94:	020000c0 	andeq	r0, r0, #192	@ 0xc0
    2d98:	a1c12083 	bicge	r2, r1, r3, lsl #1
    2d9c:	03007ffb 	movweq	r7, #4091	@ 0xffb
    2da0:	0300000c 	movweq	r0, #12
    2da4:	28620a08 	stmdacs	r2!, {r3, r9, fp}^
    2da8:	7b98d16c 	blvc	0xfe637360
    2dac:	d1012802 	tstle	r1, r2, lsl #16
    2db0:	e0022001 	and	r2, r2, r1
    2db4:	06808910 	pkhbteq	r8, r0, r0, lsl #18
    2db8:	75980f80 	ldrvc	r0, [r8, #3968]	@ 0xf80
    2dbc:	2101d051 	qaddcs	sp, r1, r1
    2dc0:	75594081 	ldrbvc	r4, [r9, #-129]	@ 0xffffff7f
    2dc4:	a00a8811 	andge	r8, sl, r1, lsl r8
    2dc8:	74196358 	ldrvc	r6, [r9], #-856	@ 0xfffffca8
    2dcc:	7318200b 	tstvc	r8, #11
    2dd0:	40082011 	andmi	r2, r8, r1, lsl r0
    2dd4:	0908d145 	stmdbeq	r8, {r0, r2, r6, r8, ip, lr, pc}
    2dd8:	090a4308 	stmdbeq	sl, {r3, r8, r9, lr}
    2ddc:	4042404a 	submi	r4, r2, sl, asr #32
    2de0:	d13e0712 	teqle	lr, r2, lsl r7
    2de4:	02002072 	andeq	r2, r0, #114	@ 0x72
    2de8:	43017d59 	movwmi	r7, #7513	@ 0x1d59
    2dec:	0000e0a7 	andeq	lr, r0, r7, lsr #1
    2df0:	28620a08 	stmdacs	r2!, {r3, r9, fp}^
    2df4:	2861d0e9 	stmdacs	r1!, {r0, r3, r5, r6, r7, ip, lr, pc}^
    2df8:	2003d133 	andcs	sp, r3, r3, lsr r1
    2dfc:	735873d8 	cmpvc	r8, #216, 6	@ 0x60000003
    2e00:	639a4aa4 	orrsvs	r4, sl, #164, 20	@ 0xa4000
    2e04:	a0012260 	andge	r2, r1, r0, ror #4
    2e08:	0000e008 	andeq	lr, r0, r8
    2e0c:	80116b9a 	mulshi	r1, sl, fp
    2e10:	639a1c92 	orrsvs	r1, sl, #37376	@ 0x9200
    2e14:	1e526c9a 	mrcne	12, 2, r6, cr2, cr10, {4}
    2e18:	a003d100 	andge	sp, r3, r0, lsl #2
    2e1c:	0212649a 	andseq	r6, r2, #-1711276032	@ 0x9a000000
    2e20:	43117d59 	tstmi	r1, #5696	@ 0x1640
    2e24:	0000e08a 	andeq	lr, r0, sl, lsl #1
    2e28:	28630a08 	stmdacs	r3!, {r3, r9, fp}^
    2e2c:	20ffd1e1 	rscscs	sp, pc, r1, ror #3
    2e30:	76d87698 			@ <UNDEFINED> instruction: 0x76d87698
    2e34:	76197299 			@ <UNDEFINED> instruction: 0x76197299
    2e38:	28027b98 	stmdacs	r2, {r3, r4, r7, r8, r9, fp, ip, sp, lr}
    2e3c:	7dd8d102 	ldfvcp	f5, [r8, #8]
    2e40:	e0057658 	and	r7, r5, r8, asr r6
    2e44:	76588850 			@ <UNDEFINED> instruction: 0x76588850
    2e48:	76988890 			@ <UNDEFINED> instruction: 0x76988890
    2e4c:	76d888d0 			@ <UNDEFINED> instruction: 0x76d888d0
    2e50:	60586998 			@ <UNDEFINED> instruction: 0x60586998
    2e54:	75da785a 	ldrbvc	r7, [sl, #2138]	@ 0x85a
    2e58:	2173a002 	cmncs	r3, r2
    2e5c:	43110209 	tstmi	r1, #-1879048192	@ 0x90000000
    2e60:	e078e06c 	rsbs	lr, r8, ip, rrx
    2e64:	28630a08 	stmdacs	r3!, {r3, r9, fp}^
    2e68:	2864d0e4 	stmdacs	r4!, {r2, r5, r6, r7, ip, lr, pc}^
    2e6c:	7719d173 			@ <UNDEFINED> instruction: 0x7719d173
    2e70:	7b98789a 	blvc	0xfe6210e0
    2e74:	d1032802 	tstle	r3, r2, lsl #16
    2e78:	20ff775a 	rscscs	r7, pc, sl, asr r7	@ <UNPREDICTABLE>
    2e7c:	77d87798 	bfivc	r7, r8, #15, #10
    2e80:	e7eaa001 	strb	sl, [sl, r1]!
    2e84:	0000e05e 	andeq	lr, r0, lr, asr r0
    2e88:	28027b98 	stmdacs	r2, {r3, r4, r7, r8, r9, fp, ip, sp, lr}
    2e8c:	8850d005 	ldmdahi	r0, {r0, r2, ip, lr, pc}^
    2e90:	88907758 	ldmhi	r0, {r3, r4, r6, r8, r9, sl, ip, sp, lr}
    2e94:	88d07798 	ldmhi	r0, {r3, r4, r7, r8, r9, sl, ip, sp, lr}^
    2e98:	008977d8 	ldrdeq	r7, [r9], r8
    2e9c:	487731c8 	ldmdami	r7!, {r3, r6, r7, r8, ip, sp}^
    2ea0:	40414008 	submi	r4, r1, r8
    2ea4:	4979d157 	ldmdbmi	r9!, {r0, r1, r2, r4, r6, r8, ip, lr, pc}^
    2ea8:	3208180a 	andcc	r1, r8, #655360	@ 0xa0000
    2eac:	a00163da 	ldrdge	r6, [r1], -sl
    2eb0:	0000e044 	andeq	lr, r0, r4, asr #32
    2eb4:	28027b98 	stmdacs	r2, {r3, r4, r7, r8, r9, fp, ip, sp, lr}
    2eb8:	80016b98 	mulhi	r1, r8, fp
    2ebc:	6811d102 	ldmdavs	r1, {r1, r8, ip, lr, pc}
    2ec0:	1c806001 	stcne	0, cr6, [r0], {1}
    2ec4:	63991c81 	orrsvs	r1, r9, #33024	@ 0x8100
    2ec8:	42886bd8 	addmi	r6, r8, #216, 22	@ 0x36000
    2ecc:	a001d137 	andge	sp, r1, r7, lsr r1
    2ed0:	0000e034 	andeq	lr, r0, r4, lsr r0
    2ed4:	d13e2965 	teqle	lr, r5, ror #18
    2ed8:	6bda6c59 	blvs	0xff69e044
    2edc:	d0014291 	mulle	r1, r1, r2
    2ee0:	e02c2174 	eor	r2, ip, r4, ror r1
    2ee4:	a0012175 	andge	r2, r1, r5, ror r1
    2ee8:	0000e028 	andeq	lr, r0, r8, lsr #32
    2eec:	d0f92965 	rscsle	r2, r9, r5, ror #18
    2ef0:	d1302966 	teqle	r0, r6, ror #18
    2ef4:	a0018c19 	andge	r8, r1, r9, lsl ip
    2ef8:	0000e020 	andeq	lr, r0, r0, lsr #32
    2efc:	42888c18 	addmi	r8, r8, #24, 24	@ 0x1800
    2f00:	7b99d129 	blvc	0xfe6773ac
    2f04:	d10c2901 	tstle	ip, r1, lsl #18
    2f08:	071b7c1b 			@ <UNDEFINED> instruction: 0x071b7c1b
    2f0c:	085b0f5b 	ldmdaeq	fp, {r0, r1, r3, r4, r6, r8, r9, sl, fp}^
    2f10:	8851d304 	ldmdahi	r1, {r2, r8, r9, ip, lr, pc}^
    2f14:	d1024288 	smlabble	r2, r8, r2, r4
    2f18:	e7f81c92 			@ <UNDEFINED> instruction: 0xe7f81c92
    2f1c:	4b5ed1fc 	blmi	0x17b7714
    2f20:	69d8d119 	ldmibvs	r8, {r0, r3, r4, r8, ip, lr, pc}^
    2f24:	1a407e59 	bne	0x1022890
    2f28:	1a407e99 	bne	0x1022994
    2f2c:	1a407ed9 	bne	0x1022a98
    2f30:	06003811 			@ <UNDEFINED> instruction: 0x06003811
    2f34:	21ffd10f 	mvnscs	sp, pc, lsl #2
    2f38:	a0387459 	eorsge	r7, r8, r9, asr r4
    2f3c:	4a4e6358 	bmi	0x139bca4
    2f40:	80518151 	subshi	r8, r1, r1, asr r1
    2f44:	28027b98 	stmdacs	r2, {r3, r4, r7, r8, r9, fp, ip, sp, lr}
    2f48:	4a4bd102 	bmi	0x12f7358
    2f4c:	81104804 	tsthi	r0, r4, lsl #16
    2f50:	6318200b 	tstvs	r8, #11
    2f54:	21004770 	tstcs	r0, r0, ror r7
    2f58:	a03073d9 	ldrsbtge	r7, [r0], -r9
    2f5c:	0000e7ee 	andeq	lr, r0, lr, ror #15
    2f60:	10085088 	andne	r5, r8, r8, lsl #1
    2f64:	d1f62901 	mvnsle	r2, r1, lsl #18
    2f68:	60586818 	subsvs	r6, r8, r8, lsl r8
    2f6c:	40484945 	submi	r4, r8, r5, asr #18
    2f70:	200b6350 	andcs	r6, fp, r0, asr r3
    2f74:	21107318 	tstcs	r0, r8, lsl r3
    2f78:	e04ba000 	sub	sl, fp, r0
    2f7c:	d1ea2904 	mvnle	r2, r4, lsl #18
    2f80:	73d82003 	bicsvc	r2, r8, #3
    2f84:	a0017358 	andge	r7, r1, r8, asr r3
    2f88:	0000e046 	andeq	lr, r0, r6, asr #32
    2f8c:	d1e22902 	mvnle	r2, r2, lsl #18
    2f90:	21026b10 	tstcs	r2, r0, lsl fp
    2f94:	40010209 	andmi	r0, r1, r9, lsl #4
    2f98:	a23909c9 	eorsge	r0, r9, #3293184	@ 0x324000
    2f9c:	68111852 	ldmdavs	r1, {r1, r4, r6, fp, ip}
    2fa0:	60984048 	addsvs	r4, r8, r8, asr #32
    2fa4:	227f0a01 	rsbscs	r0, pc, #4096	@ 0x1000
    2fa8:	04004011 	streq	r4, [r0], #-17	@ 0xffffffef
    2fac:	3180d300 	orrcc	sp, r0, r0, lsl #6
    2fb0:	40100c00 	andsmi	r0, r0, r0, lsl #24
    2fb4:	430101c9 	movwmi	r0, #4553	@ 0x11c9
    2fb8:	00c9313f 	sbceq	r3, r9, pc, lsr r1
    2fbc:	4008482f 	andmi	r4, r8, pc, lsr #16
    2fc0:	d0054288 	andle	r4, r5, r8, lsl #5
    2fc4:	06407a98 			@ <UNDEFINED> instruction: 0x06407a98
    2fc8:	72980e40 	addsvc	r0, r8, #64, 28	@ 0x400
    2fcc:	01c02089 	biceq	r2, r0, r9, lsl #1
    2fd0:	4930300c 	ldmdbmi	r0!, {r2, r3, ip, sp}
    2fd4:	64191809 	ldrvs	r1, [r9], #-2057	@ 0xfffff7f7
    2fd8:	a0012120 	andge	r2, r1, r0, lsr #2
    2fdc:	0000e01a 	andeq	lr, r0, sl, lsl r0
    2fe0:	d1b82902 			@ <UNDEFINED> instruction: 0xd1b82902
    2fe4:	21108f10 	tstcs	r0, r0, lsl pc
    2fe8:	6b104041 	blvs	0x4130f4
    2fec:	6b998711 	blvs	0xfe664c38
    2ff0:	6399c101 	orrsvs	ip, r9, #1073741824	@ 0x40000000
    2ff4:	42886bd8 	addmi	r6, r8, #216, 22	@ 0x36000
    2ff8:	4924d1a4 	stmdbmi	r4!, {r2, r5, r7, r8, ip, lr, pc}
    2ffc:	d1024288 	smlabble	r2, r8, r2, r4
    3000:	63d86c18 	bicsvs	r6, r8, #24, 24	@ 0x1800
    3004:	4822e79e 	stmdami	r2!, {r1, r2, r3, r4, r7, r8, r9, sl, sp, lr, pc}
    3008:	68006841 	stmdavs	r0, {r0, r6, fp, sp, lr}
    300c:	63504348 	cmpvs	r0, #72, 6	@ 0x20000001
    3010:	a0022100 	andge	r2, r2, r0, lsl #2
    3014:	87114a18 			@ <UNDEFINED> instruction: 0x87114a18
    3018:	e7936358 			@ <UNDEFINED> instruction: 0xe7936358
    301c:	b5004770 	strlt	r4, [r0, #-1904]	@ 0xfffff890
    3020:	60104a1c 	andsvs	r4, r0, ip, lsl sl
    3024:	71194b19 	tstvc	r9, r9, lsl fp
    3028:	d0012901 	andle	r2, r1, r1, lsl #18
    302c:	71587db8 	ldrhvc	r7, [r8, #-216]	@ 0xffffff28
    3030:	f7fd1d10 			@ <UNDEFINED> instruction: 0xf7fd1d10
    3034:	bd00fb59 	vstrlt	d15, [r0, #-356]	@ 0xfffffe9c
    3038:	f7fd1d00 			@ <UNDEFINED> instruction: 0xf7fd1d00
    303c:	8cb8fc86 	ldchi	12, cr15, [r8], #536	@ 0x218
    3040:	d1012800 	tstle	r1, r0, lsl #16
    3044:	84b8203c 	ldrthi	r2, [r8], #60	@ 0x3c
    3048:	7cb8e54e 	cfldr32vc	mvfx14, [r8], #312	@ 0x138
    304c:	d1022877 	tstle	r2, r7, ror r8
    3050:	fc90f7fd 	ldc2	7, cr15, [r0], {253}	@ 0xfd
    3054:	2876e003 	ldmdacs	r6!, {r0, r1, sp, lr, pc}^
    3058:	f7fdd101 			@ <UNDEFINED> instruction: 0xf7fdd101
    305c:	8cb8fc92 	ldchi	12, cr15, [r8], #584	@ 0x248
    3060:	d0072800 	andle	r2, r7, r0, lsl #16
    3064:	84b81e40 	ldrthi	r1, [r8], #3648	@ 0xe40
    3068:	d1032839 	tstle	r3, r9, lsr r8
    306c:	490f480e 	stmdbmi	pc, {r1, r2, r3, fp, lr}	@ <UNPREDICTABLE>
    3070:	f9e0f7fe 			@ <UNDEFINED> instruction: 0xf9e0f7fe
    3074:	0000e568 	andeq	lr, r0, r8, ror #10
    3078:	04000120 	streq	r0, [r0], #-288	@ 0xfffffee0
    307c:	0003fff8 	strdeq	pc, [r3], -r8
    3080:	6177614b 	cmnvs	r7, fp, asr #2
    3084:	6f646573 	svcvs	0x00646573
    3088:	04000200 	streq	r0, [r0], #-512	@ 0xfffffe00
    308c:	020000c0 	andeq	r0, r0, #192	@ 0xc0
    3090:	020001f8 	andeq	r0, r0, #248, 2	@ 0x3e
    3094:	02000000 	andeq	r0, r0, #0
    3098:	0300000c 	movweq	r0, #12
    309c:	80808080 	addhi	r8, r0, r0, lsl #1
    30a0:	ea000036 	b	0x3180
    30a4:	ea00002e 	b	0x3164
    30a8:	0300390c 	movweq	r3, #2316	@ 0x90c
    30ac:	00003980 	andeq	r3, r0, r0, lsl #19
    30b0:	00280022 	eoreq	r0, r8, r2, lsr #32
    30b4:	00880082 	addeq	r0, r8, r2, lsl #1
    30b8:	00e800e2 	rsceq	r0, r8, r2, ror #1
    30bc:	01480142 	cmpeq	r8, r2, asr #2
    30c0:	08020200 	stmdaeq	r2, {r9}
    30c4:	00000000 	andeq	r0, r0, r0
    30c8:	080101c0 	stmdaeq	r1, {r6, r7, r8}
    30cc:	0000001e 	andeq	r0, r0, lr, lsl r0
    30d0:	03020100 	movweq	r0, #8448	@ 0x2100
    30d4:	07060504 	streq	r0, [r6, -r4, lsl #10]
    30d8:	0b0a0908 	bleq	0x285500
    30dc:	0f0e0d0c 	svceq	0x000e0d0c
    30e0:	13121110 	tstne	r2, #16, 2
    30e4:	17161514 			@ <UNDEFINED> instruction: 0x17161514
    30e8:	201e1c18 	andscs	r1, lr, r8, lsl ip
    30ec:	2c2a2824 	stccs	8, cr2, [sl], #-144	@ 0xffffff70
    30f0:	38363430 	ldmdacc	r6!, {r4, r5, sl, ip, sp}
    30f4:	4442403c 	strbmi	r4, [r2], #-60	@ 0xffffffc4
    30f8:	504e4c48 	subpl	r4, lr, r8, asr #24
    30fc:	5c5a5854 	mrrcpl	8, 5, r5, sl, cr4
    3100:	00000060 	andeq	r0, r0, r0, rrx
    3104:	e3e2e1e0 	mvn	lr, #224, 2	@ 0x38
    3108:	e7e6e5e4 	strb	lr, [r6, r4, ror #11]!
    310c:	ebeae9e8 	bl	0xffabd8b4
    3110:	d3d2d1d0 	bicsle	sp, r2, #208, 2	@ 0x34
    3114:	d7d6d5d4 			@ <UNDEFINED> instruction: 0xd7d6d5d4
    3118:	dbdad9d8 	blle	0xff6b9880
    311c:	c3c2c1c0 	bicgt	ip, r2, #192, 2	@ 0x30
    3120:	c7c6c5c4 	strbgt	ip, [r6, r4, asr #11]
    3124:	cbcac9c8 	blgt	0xff2b584c
    3128:	b3b2b1b0 			@ <UNDEFINED> instruction: 0xb3b2b1b0
    312c:	b7b6b5b4 			@ <UNDEFINED> instruction: 0xb7b6b5b4
    3130:	bbbab9b8 	bllt	0xfeeb1818
    3134:	a3a2a1a0 			@ <UNDEFINED> instruction: 0xa3a2a1a0
    3138:	a7a6a5a4 	strge	sl, [r6, r4, lsr #11]!
    313c:	abaaa9a8 	blge	0xfeaad7e4
    3140:	93929190 	orrsls	r9, r2, #144, 2	@ 0x24
    3144:	97969594 			@ <UNDEFINED> instruction: 0x97969594
    3148:	9b9a9998 	blls	0xfe6a97b0
    314c:	83828180 	orrhi	r8, r2, #128, 2
    3150:	87868584 	strhi	r8, [r6, r4, lsl #11]
    3154:	8b8a8988 	blhi	0xfe2a577c
    3158:	73727170 	cmnvc	r2, #112, 2
    315c:	77767574 			@ <UNDEFINED> instruction: 0x77767574
    3160:	7b7a7978 	blvc	0x1ea1748
    3164:	63626160 	cmnvs	r2, #96, 2
    3168:	67666564 	strbvs	r6, [r6, -r4, ror #10]!
    316c:	6b6a6968 	blvs	0x1a9d714
    3170:	53525150 	cmppl	r2, #80, 2
    3174:	57565554 			@ <UNDEFINED> instruction: 0x57565554
    3178:	5b5a5958 	blpl	0x16996e0
    317c:	43424140 	movtmi	r4, #8512	@ 0x2140
    3180:	47464544 	strbmi	r4, [r6, -r4, asr #10]
    3184:	4b4a4948 	blmi	0x12956ac
    3188:	33323130 	teqcc	r2, #48, 2
    318c:	37363534 			@ <UNDEFINED> instruction: 0x37363534
    3190:	3b3a3938 	blcc	0xe91678
    3194:	23222120 			@ <UNDEFINED> instruction: 0x23222120
    3198:	27262524 	strcs	r2, [r6, -r4, lsr #10]!
    319c:	2b2a2928 	blcs	0xa8d644
    31a0:	13121110 	tstne	r2, #16, 2
    31a4:	17161514 			@ <UNDEFINED> instruction: 0x17161514
    31a8:	1b1a1918 	blne	0x689610
    31ac:	03020100 	movweq	r0, #8448	@ 0x2100
    31b0:	07060504 	streq	r0, [r6, -r4, lsl #10]
    31b4:	0b0a0908 	bleq	0x2855dc
    31b8:	80000000 	andhi	r0, r0, r0
    31bc:	879c7c97 			@ <UNDEFINED> instruction: 0x879c7c97
    31c0:	8facd61e 	svchi	0x00acd61e
    31c4:	9837f052 	ldmdals	r7!, {r1, r4, r6, ip, sp, lr, pc}
    31c8:	a14517cc 	smlalbtge	r1, r5, ip, r7
    31cc:	aadc0848 	bge	0xff7052f4
    31d0:	b504f334 	strlt	pc, [r4, #-820]	@ 0xfffffccc
    31d4:	bfc886bb 	svclt	0x00c886bb
    31d8:	cb2ff52a 	blgt	0xc00688
    31dc:	d744fccb 	strble	pc, [r4, -fp, asr #25]	@ <UNPREDICTABLE>
    31e0:	e411f03a 	ldr	pc, [r1], #-58	@ 0xffffffc6
    31e4:	f1a1bf39 			@ <UNDEFINED> instruction: 0xf1a1bf39
    31e8:	00840060 	addeq	r0, r4, r0, rrx
    31ec:	00e000b0 	strhteq	r0, [r0], #0
    31f0:	01300108 	teqeq	r0, r8, lsl #2
    31f4:	01c00160 	biceq	r0, r0, r0, ror #2
    31f8:	02600210 	rsbeq	r0, r0, #16, 4
    31fc:	02c002a0 	sbceq	r0, r0, #160, 4
    3200:	ffffffff 			@ <UNDEFINED> instruction: 0xffffffff
    3204:	01f0001f 	mvnseq	r0, pc, lsl r0
    3208:	01f0281f 	mvnseq	r2, pc, lsl r8
    320c:	01f0581f 	mvnseq	r5, pc, lsl r8
    3210:	01f00000 	mvnseq	r0, r0
    3214:	01f0280a 	mvnseq	r2, sl, lsl #16
    3218:	01f05816 	mvnseq	r5, r6, lsl r8
    321c:	01f07c00 	mvnseq	r7, r0, lsl #24
    3220:	01f07c0a 	mvnseq	r7, sl, lsl #24
    3224:	01f07c16 	mvnseq	r7, r6, lsl ip
    3228:	00007c00 	andeq	r7, r0, r0, lsl #24
    322c:	00a07c0a 	adceq	r7, r0, sl, lsl #24
    3230:	01607c16 	cmneq	r0, r6, lsl ip
    3234:	00007c1f 	andeq	r7, r0, pc, lsl ip
    3238:	00a07c1f 	adceq	r7, r0, pc, lsl ip
    323c:	01607c1f 	cmneq	r0, pc, lsl ip
    3240:	0000001f 	andeq	r0, r0, pc, lsl r0
    3244:	00a0281f 	adceq	r2, r0, pc, lsl r8
    3248:	0160581f 	cmneq	r0, pc, lsl r8
    324c:	01f0001f 	mvnseq	r0, pc, lsl r0
    3250:	01f0281f 	mvnseq	r2, pc, lsl r8
    3254:	01f0581f 	mvnseq	r5, pc, lsl r8
    3258:	01f07c1f 	mvnseq	r7, pc, lsl ip
    325c:	01f07c1f 	mvnseq	r7, pc, lsl ip
    3260:	01f07c1f 	mvnseq	r7, pc, lsl ip
    3264:	ff1f7c00 			@ <UNDEFINED> instruction: 0xff1f7c00
    3268:	7c1ffd5f 	ldcvc	13, cr15, [pc], {95}	@ 0x5f
    326c:	0000d424 	andeq	sp, r0, r4, lsr #8
    3270:	0000400f 	andeq	r4, r0, pc
    3274:	82810100 	addhi	r0, r1, #0, 2
    3278:	830f8382 	movwhi	r8, #62338	@ 0xf382
    327c:	8303c30c 	movwhi	ip, #13068	@ 0x330c
    3280:	c3048301 	movwgt	r8, #17153	@ 0x4301
    3284:	c2020e08 	andgt	r0, r2, #8, 28	@ 0x80
    3288:	0b07c20d 	bleq	0x1f3ac4
    328c:	09050a06 	stmdbeq	r5, {r1, r2, r9, fp}
    3290:	51aeff24 			@ <UNDEFINED> instruction: 0x51aeff24
    3294:	21a29a69 			@ <UNDEFINED> instruction: 0x21a29a69
    3298:	0a82843d 	beq	0xfe0a4394
    329c:	ad09e484 	cfstrsge	mvf14, [r9, #-528]	@ 0xfffffdf0
    32a0:	988b2411 	stmls	fp, {r0, r4, sl, sp}
    32a4:	217f81c0 	cmncs	pc, r0, asr #3
    32a8:	19be52a3 	ldmibne	lr!, {r0, r1, r5, r7, r9, ip, lr}
    32ac:	20ce0993 	smullcs	r0, lr, r3, r9
    32b0:	4a4a4610 	bmi	0x1294af8
    32b4:	ec3127f8 	ldc	7, cr2, [r1], #-992	@ 0xfffffc20
    32b8:	33e8c758 	mvncc	ip, #88, 14	@ 0x1600000
    32bc:	bfcee382 	svclt	0x00cee382
    32c0:	94dff485 	ldrbls	pc, [pc], #1157	@ 0x32c8	@ <UNPREDICTABLE>
    32c4:	c1094bce 	smlabtgt	r9, lr, fp, r4
    32c8:	c08a5694 	umullgt	r5, sl, r4, r6
    32cc:	fca77213 	stc2	2, cr7, [r7], #76	@ 0x4c
    32d0:	734d849f 	movtvc	r8, #54431	@ 0xd49f
    32d4:	619acaa3 	orrsvs	ip, sl, r3, lsr #21
    32d8:	27a39758 	sbfxcs	r9, r8, #14, #4
    32dc:	769803fc 			@ <UNDEFINED> instruction: 0x769803fc
    32e0:	61c71d23 	bicvs	r1, r7, r3, lsr #26
    32e4:	56ae0403 	strtpl	r0, [lr], r3, lsl #8
    32e8:	008438bf 			@ <UNDEFINED> instruction: 0x008438bf
    32ec:	fd0ea740 	stc2	7, cr10, [lr, #-256]	@ 0xffffff00
    32f0:	03fe52ff 	mvnseq	r5, #-268435441	@ 0xf000000f
    32f4:	f130956f 			@ <UNDEFINED> instruction: 0xf130956f
    32f8:	85c0fb97 	strbhi	pc, [r0, #2967]	@ 0xb97	@ <UNPREDICTABLE>
    32fc:	2580d660 	strcs	sp, [r0, #1632]	@ 0x660
    3300:	03be63a9 			@ <UNDEFINED> instruction: 0x03be63a9
    3304:	e2384e01 	eors	r4, r8, #1, 28
    3308:	ff34a2f9 			@ <UNDEFINED> instruction: 0xff34a2f9
    330c:	44033ebb 	strmi	r3, [r3], #-3771	@ 0xfffff145
    3310:	cb900078 	blgt	0xfe4034f8
    3314:	943a1188 	ldrtls	r1, [sl], #-392	@ 0xfffffe78
    3318:	637cc065 	cmnvs	ip, #101	@ 0x65
    331c:	af3cf087 	svcge	0x003cf087
    3320:	8be425d6 	blhi	0xff90ca80
    3324:	72ac0a38 	adcvc	r0, ip, #56, 20	@ 0x38000
    3328:	07f8d421 	ldrbeq	sp, [r8, r1, lsr #8]!
    332c:	0003c024 	andeq	ip, r3, r4, lsr #32
    3330:	0180000f 	orreq	r0, r0, pc
    3334:	42014100 	andmi	r4, r1, #0, 2
    3338:	43c20502 	bicmi	r0, r2, #8388608	@ 0x800000
    333c:	c4430143 	strbgt	r0, [r3], #-323	@ 0xfffffebd
    3340:	03c30f0a 	biceq	r0, r3, #10, 30	@ 0x28
    3344:	09c302c3 	stmibeq	r3, {r0, r1, r6, r7, r9}^
    3348:	0807040b 	stmdaeq	r7, {r0, r1, r3, sl}
    334c:	0e0c0d06 	cdpeq	13, 0, cr0, cr12, cr6, {0}
    3350:	38540300 	ldmdacc	r4, {r8, r9}^
    3354:	481c0c02 	ldmdami	ip, {r1, sl, fp}
    3358:	9956c0c3 	ldmdbls	r6, {r0, r1, r6, r7, lr, pc}^
    335c:	756d8008 	strbvc	r8, [sp, #-8]!
    3360:	441aa6d9 	ldrmi	sl, [sl], #-1753	@ 0xfffff927
    3364:	80844021 	addhi	r4, r4, r1, lsr #32
    3368:	1035b7a6 	eorsne	fp, r5, r6, lsr #15
    336c:	f1db6c98 			@ <UNDEFINED> instruction: 0xf1db6c98
    3370:	66d415a2 	ldrbvs	r1, [r4], r2, lsr #11
    3374:	002c0a07 	eoreq	r0, ip, r7, lsl #20
    3378:	b1ac0418 			@ <UNDEFINED> instruction: 0xb1ac0418
    337c:	55e19deb 	strbpl	r9, [r1, #3563]!	@ 0xdeb
    3380:	45b048de 	ldrmi	r4, [r0, #2270]!	@ 0x8de
    3384:	46ec6c1a 	usatmi	r6, #12, sl, lsl #24
    3388:	6004d858 	andvs	sp, r4, r8, asr r8
    338c:	292e0104 	stmdbcs	lr!, {r2, r8}
    3390:	e2af60c7 	adc	r6, pc, #199	@ 0xc7
    3394:	1061d661 	rsbne	sp, r1, r1, ror #12
    3398:	e40fc69d 	str	ip, [pc], #-1693	@ 0x33a0
    339c:	40381379 	eorsmi	r1, r8, r9, ror r3
    33a0:	1b775617 	blne	0x1dd8c04
    33a4:	5ab06e8d 	bpl	0xfec1ede0
    33a8:	bc36e230 	lfmlt	f6, 1, [r6], #-192	@ 0xffffff40
    33ac:	c33d6c28 	teqgt	sp, #40, 24	@ 0x2800
    33b0:	c1f0e1e7 	mvnsgt	lr, r7, ror #3
    33b4:	b49a9d14 	ldrlt	r9, [sl], #3348	@ 0xd14
    33b8:	0aa08094 	beq	0xfe823610
    33bc:	56746946 	ldrbtpl	r6, [r4], -r6, asr #18
    33c0:	da0db505 	ble	0x3707dc
    33c4:	5b043079 	blpl	0x10f5b0
    33c8:	220d1268 	andcs	r1, sp, #104, 4	@ 0x80000006
    33cc:	46a51990 	ssatmi	r1, #6, r0, lsl #19
    33d0:	91f0e3d0 	ldrsbls	lr, [r0, #48]!	@ 0x30
    33d4:	e225724d 	eor	r7, r5, #-805306364	@ 0xd0000004
    33d8:	c9401866 	stmdbgt	r0, {r1, r2, r5, r6, fp, ip}^
    33dc:	a0ac0618 	adcge	r0, ip, r8, lsl r6
    33e0:	83c84c85 	bichi	r4, r8, #34048	@ 0x8500
    33e4:	b486f181 	strlt	pc, [r6], #385	@ 0x181
    33e8:	e5d9286d 	ldrb	r2, [r9, #2157]	@ 0x86d
    33ec:	6a280ab6 	bvs	0xa05ecc
    33f0:	2f94b1b7 	svccs	0x0094b1b7
    33f4:	57a34d5b 	sbfxpl	r4, fp, #26, #4
    33f8:	cd1bf87e 	ldcgt	8, cr15, [fp, #-504]	@ 0xfffffe08
    33fc:	d3af430d 			@ <UNDEFINED> instruction: 0xd3af430d
    3400:	fa379a37 	blx	0xde9ce4
    3404:	95b69bda 	ldrls	r9, [r6, #3034]!	@ 0xbda
    3408:	b69112c4 	ldrlt	r1, [r1], r4, asr #5
    340c:	00a85a1d 	adceq	r5, r8, sp, lsl sl
    3410:	79b4a013 	ldmibvc	r4!, {r0, r1, r4, sp, pc}
    3414:	928110d2 	addls	r1, r1, #210	@ 0xd2
    3418:	0a86da46 	beq	0xfe1b9d38
    341c:	88dc30a3 	ldmhi	ip, {r0, r1, r5, r7, ip, sp}^
    3420:	306cd6c0 	rsbcc	sp, ip, r0, asr #13
    3424:	e686f19c 	pkhbt	pc, r6, ip, lsl #3	@ <UNPREDICTABLE>
    3428:	00c300f4 	strdeq	r0, [r3], #4
    342c:	161e82c6 	ldrne	r8, [lr], -r6, asr #5
    3430:	6abbd060 	bvs	0xfeef75b8
    3434:	24056320 	strcs	r6, [r5], #-800	@ 0xfffffce0
    3438:	2bfba50a 	blcs	0xffeec868
    343c:	1b9e5438 	blne	0xfe798524
    3440:	5a909790 	bpl	0xfe429288
    3444:	3890de7f 	ldmcc	r0, {r0, r1, r2, r3, r4, r5, r6, r9, sl, fp, ip, lr, pc}
    3448:	0a328283 	beq	0xca3e5c
    344c:	8012b5b0 			@ <UNDEFINED> instruction: 0x8012b5b0
    3450:	98c37904 	stmials	r3, {r2, r8, fp, ip, sp, lr}^
    3454:	d03862e3 	eorsle	r6, r8, r3, ror #5
    3458:	df465d5f 	svcle	0x00465d5f
    345c:	d91f29c3 	ldmdble	pc, {r0, r1, r6, r7, r8, fp, sp}	@ <UNPREDICTABLE>
    3460:	54a1e55f 	strtpl	lr, [r1], #1375	@ 0x55f
    3464:	ca480069 	bgt	0x1203610
    3468:	6c16eca3 	ldcvs	12, cr14, [r6], {163}	@ 0xa3
    346c:	b30c3506 	movwlt	r3, #50438	@ 0xc506
    3470:	f6adf5b7 			@ <UNDEFINED> instruction: 0xf6adf5b7
    3474:	f0da065b 			@ <UNDEFINED> instruction: 0xf0da065b
    3478:	6dbc9bc0 			@ <UNDEFINED> instruction: 0x6dbc9bc0
    347c:	4b4b69ae 	blmi	0x12ddb3c
    3480:	21870f2a 	orrcs	r0, r7, sl, lsr #30
    3484:	100ca45b 	andne	sl, ip, fp, asr r4
    3488:	6510afda 	ldrvs	sl, [r0, #-4058]	@ 0xfffff026
    348c:	5b86dcf2 	blpl	0xfe1ba85c
    3490:	b8d11bc3 	ldmlt	r1, {r0, r1, r6, r7, r8, r9, fp, ip}^
    3494:	e6987a51 			@ <UNDEFINED> instruction: 0xe6987a51
    3498:	d8b69675 	ldmle	r6!, {r0, r2, r4, r5, r6, r9, sl, ip, pc}
    349c:	ade739dd 			@ <UNDEFINED> instruction: 0xade739dd
    34a0:	ce37dbe7 	vsubgt.f64	d13, d23, d23
    34a4:	28cfce74 	stmiacs	pc, {r2, r4, r5, r6, r9, sl, fp, lr, pc}^	@ <UNPREDICTABLE>
    34a8:	5066dcd8 	ldrdpl	sp, [r6], #-200	@ 0xffffff38	@ <UNPREDICTABLE>
    34ac:	b6dadb4a 	ldrblt	sp, [sl], sl, asr #22
    34b0:	dbc826a1 	blle	0xff20cf3c
    34b4:	f620d542 			@ <UNDEFINED> instruction: 0xf620d542
    34b8:	dee14b19 	vduple.8	<illegal reg q0.5>, r4
    34bc:	aa8d37ba 	bge	0xfe3513ac
    34c0:	bcccf29c 	sfmlt	f7, 3, [ip], {156}	@ 0x9c
    34c4:	b479eec7 	ldrbtlt	lr, [r9], #-3783	@ 0xfffff139
    34c8:	cf202c02 	svcgt	0x00202c02
    34cc:	d25b4cce 	subsle	r4, fp, #52736	@ 0xce00
    34d0:	8d7abb63 	fldmdbxhi	sl!, {d27-d75}	@ Deprecated
    34d4:	0364dafa 	cmneq	r4, #1024000	@ 0xfa000
    34d8:	f999fe70 			@ <UNDEFINED> instruction: 0xf999fe70
    34dc:	7520aa9f 	strvc	sl, [r0, #-2719]!	@ 0xfffff561
    34e0:	836f272e 	cmnhi	pc, #12058624	@ 0xb80000
    34e4:	39f55052 	ldmibcc	r5!, {r1, r4, r6, ip, lr}^
    34e8:	6eb2587e 	mrcvs	8, 5, r5, cr2, cr14, {3}
    34ec:	9af255b9 	bls	0xffc98bd8
    34f0:	8a05da0d 	bhi	0x179d2c
    34f4:	bab079ab 	blt	0xfec21ba8
    34f8:	7eadf5b7 	mcrvc	5, 5, pc, cr13, cr7, {5}	@ <UNPREDICTABLE>
    34fc:	e8e68f99 	stmia	r6!, {r0, r3, r4, r7, r8, r9, sl, fp, pc}^
    3500:	77ee9248 	strbvc	r9, [lr, r8, asr #4]!
    3504:	18851954 	stmne	r5, {r2, r4, r6, r8, fp, ip}
    3508:	08485cde 	stmdaeq	r8, {r1, r2, r3, r4, r6, r7, sl, fp, ip, lr}^
    350c:	73685a4c 	cmnvc	r8, #76, 20	@ 0x4c000
    3510:	8441e67d 	strbhi	lr, [r1], #-1661	@ 0xfffff983
    3514:	f0d44883 			@ <UNDEFINED> instruction: 0xf0d44883
    3518:	36bc37b7 			@ <UNDEFINED> instruction: 0x36bc37b7
    351c:	e53a726a 	ldr	r7, [sl, #-618]!	@ 0xfffffd96
    3520:	90999eae 	addsls	r9, r9, lr, lsr #29
    3524:	2403190a 	strcs	r1, [r3], #-2314	@ 0xfffff6f6
    3528:	f0ae8574 			@ <UNDEFINED> instruction: 0xf0ae8574
    352c:	ecc5ad35 	stcl	13, cr10, [r5], {53}	@ 0x35
    3530:	61bc48df 			@ <UNDEFINED> instruction: 0x61bc48df
    3534:	3f4a628f 	svccc	0x004a628f
    3538:	93325ccd 	teqls	r2, #52480	@ 0xcd00
    353c:	99b3ccad 	ldmibls	r3!, {r0, r2, r3, r5, r7, sl, fp, lr, pc}
    3540:	8e650c2a 	cdphi	12, 6, cr0, cr5, cr10, {1}
    3544:	bbaa4950 	bllt	0xfea95a8c
    3548:	306d29cb 	rsbcc	r2, sp, fp, asr #19
    354c:	5f4e9fa3 	svcpl	0x004e9fa3
    3550:	5333746d 	teqpl	r3, #1828716544	@ 0x6d000000
    3554:	386b15a5 	stmdacc	fp!, {r0, r2, r5, r7, r8, sl, ip}^
    3558:	c62d0996 			@ <UNDEFINED> instruction: 0xc62d0996
    355c:	8a31f64e 	bhi	0xc80e9c
    3560:	7ea53de2 	cdpvc	13, 10, cr3, cr5, cr2, {7}
    3564:	a8485c24 	stmdage	r8, {r2, r5, sl, fp, ip, lr}^
    3568:	31086895 			@ <UNDEFINED> instruction: 0x31086895
    356c:	8b99421c 	blhi	0xfe653de4
    3570:	18b5c3bb 	ldmne	r5!, {r0, r1, r3, r4, r5, r7, r8, r9, lr, pc}
    3574:	9a3bd91c 	bls	0xef99ec
    3578:	cda2b5de 	cfstr32gt	mvfx11, [r2, #888]!	@ 0x378
    357c:	aa776d38 	bge	0x1ddea64
    3580:	6f438ed0 	svcvs	0x00438ed0
    3584:	ef4ae4f1 	svc	0x004ae4f1
    3588:	9c2fa805 	stcls	8, cr10, [pc], #-20	@ 0x357c
    358c:	65a50651 	strvs	r0, [r5, #1617]!	@ 0x651
    3590:	a766ea6c 	strbge	lr, [r6, -ip, ror #20]!
    3594:	ffb3f0d9 			@ <UNDEFINED> instruction: 0xffb3f0d9
    3598:	86ee4c37 			@ <UNDEFINED> instruction: 0x86ee4c37
    359c:	b46fad09 	strbtlt	sl, [pc], #-3337	@ 0x35a4
    35a0:	d069d293 	mlsle	r9, r3, r2, sp
    35a4:	a4cc2219 	strbge	r2, [ip], #537	@ 0x219
    35a8:	7bbe7ccd 	blvc	0xfefa28e4
    35ac:	107cba0b 	rsbsne	fp, ip, fp, lsl #20
    35b0:	ff2aab4e 			@ <UNDEFINED> instruction: 0xff2aab4e
    35b4:	81079132 	tsthi	r7, r2, lsr r1
    35b8:	5265a550 	rsbpl	sl, r5, #80, 10	@ 0x14000000
    35bc:	b1733016 	cmnlt	r3, r6, lsl r0
    35c0:	ad046645 	stcge	6, cr6, [r4, #-276]	@ 0xfffffeec
    35c4:	825bab5a 	subshi	sl, fp, #92160	@ 0x16800
    35c8:	ed4884d2 	cfstrd	mvd8, [r8, #-840]	@ 0xfffffcb8
    35cc:	6429185e 	strtvs	r1, [r9], #-2142	@ 0xfffff7a2
    35d0:	4beacb32 	blmi	0xffab62a0
    35d4:	025fbb58 	subseq	fp, pc, #88, 22	@ 0x16000
    35d8:	5aefdb5c 	bpl	0xffbfa350
    35dc:	ae6d77da 	mcrge	7, 3, r7, cr13, cr10, {6}
    35e0:	4febc4db 	svcmi	0x00ebc4db
    35e4:	76517466 	ldrbvc	r7, [r1], -r6, ror #8
    35e8:	156dcb58 	strbne	ip, [sp, #-2904]!	@ 0xfffff4a8
    35ec:	955ef43f 	ldrbls	pc, [lr, #-1087]	@ 0xfffffbc1	@ <UNPREDICTABLE>
    35f0:	73a2fa52 			@ <UNDEFINED> instruction: 0x73a2fa52
    35f4:	484ce119 	stmdami	ip, {r0, r3, r4, r8, sp, lr, pc}^
    35f8:	1fcd1934 	svcne	0x00cd1934
    35fc:	d1143998 			@ <UNDEFINED> instruction: 0xd1143998
    3600:	77d57fa0 	ldrbvc	r7, [r5, r0, lsr #31]
    3604:	e51a4a37 	ldr	r4, [sl, #-2615]	@ 0xfffff5c9
    3608:	216d2af6 	strdcs	r2, [sp, #-166]!	@ 0xffffff5a
    360c:	a531ec87 	ldrge	lr, [r1, #-3207]!	@ 0xfffff379
    3610:	e79f2600 	ldr	r2, [pc, r0, lsl #12]
    3614:	0502100a 	streq	r1, [r2, #-10]
    3618:	8108815c 	tsthi	r8, ip, asr r1
    361c:	88222098 	stmdahi	r2!, {r3, r4, r7, sp}
    3620:	854440b0 	strbhi	r4, [r4, #-176]	@ 0xffffff50
    3624:	68051581 	stmdavs	r5, {r0, r7, r8, sl, ip}
    3628:	d2488844 	suble	r8, r8, #68, 16	@ 0x440000
    362c:	5a2442c4 	bpl	0x914144
    3630:	135afd91 	cmpne	sl, #9280	@ 0x2440	@ <UNPREDICTABLE>
    3634:	1022e832 	eorne	lr, r2, r2, lsr r8
    3638:	22c88044 	sbccs	r8, r8, #68	@ 0x44
    363c:	88810888 	stmhi	r1, {r3, r7, fp}
    3640:	ac461514 	cfstr64ge	mvdx1, [r6], {20}
    3644:	21aa21c4 			@ <UNDEFINED> instruction: 0x21aa21c4
    3648:	6197c5aa 	orrsvs	ip, r7, sl, lsr #11
    364c:	64900888 	ldrvs	r0, [r0], #2184	@ 0x888
    3650:	b4c46b88 	strblt	r6, [r4], #2952	@ 0xb88
    3654:	45c4235b 	strbmi	r2, [r4, #859]	@ 0x35b
    3658:	69882f83 	stmibvs	r8, {r0, r1, r7, r8, r9, sl, fp, sp}
    365c:	a59ed758 	ldrge	sp, [lr, #1880]	@ 0x758
    3660:	8c21159b 	cfstr32hi	mvfx1, [r1], #-620	@ 0xfffffd94
    3664:	aa70487d 	bge	0x1c15860
    3668:	5a02e967 	bpl	0xbdc0c
    366c:	b5a62362 	strlt	r2, [r6, #866]!	@ 0x362
    3670:	3ec5900d 	cdpcc	0, 12, cr9, cr5, cr13, {0}
    3674:	421100a8 	andsmi	r0, r1, #168	@ 0xa8
    3678:	0e8298ab 	cdpeq	8, 8, cr9, cr2, cr11, {5}
    367c:	b9986158 	ldmiblt	r8, {r3, r4, r6, r8, sp, lr}
    3680:	2fa95019 	svccs	0x00a95019
    3684:	4199bedc 			@ <UNDEFINED> instruction: 0x4199bedc
    3688:	0b448804 	bleq	0x11256a0
    368c:	a1244400 			@ <UNDEFINED> instruction: 0xa1244400
    3690:	d45d9088 	ldrble	r9, [sp], #-136	@ 0xffffff78
    3694:	74294e00 	strtvc	r4, [r9], #-3584	@ 0xfffff200
    3698:	00000000 	andeq	r0, r0, r0
    369c:	c2ad2620 	adcgt	r2, sp, #32, 12	@ 0x2000000
    36a0:	01000a40 	tsteq	r0, r0, asr #20
    36a4:	c4962620 	ldrgt	r2, [r6], #1568	@ 0x620
    36a8:	0040098c 	subeq	r0, r0, ip, lsl #19
    36ac:	c67f2620 	ldrbtgt	r2, [pc], -r0, lsr #12
    36b0:	00000980 	andeq	r0, r0, r0, lsl #19
    36b4:	c8682620 	stmdagt	r8!, {r5, r9, sl, sp}^
    36b8:	000008cc 	andeq	r0, r0, ip, asr #17
    36bc:	ca502620 	bgt	0x140cf44
    36c0:	000008c0 	andeq	r0, r0, r0, asr #17
    36c4:	cc392620 	ldcgt	6, cr2, [r9], #-128	@ 0xffffff80
    36c8:	0000080c 	andeq	r0, r0, ip, lsl #16
    36cc:	ce222620 	cfmadda32gt	mvax1, mvax2, mvfx2, mvfx0
    36d0:	00000800 	andeq	r0, r0, r0, lsl #16
    36d4:	c0376466 	eorsgt	r6, r7, r6, ror #8
    36d8:	00005b40 	andeq	r5, r0, r0, asr #22
    36dc:	c0776466 	rsbsgt	r6, r7, r6, ror #8
    36e0:	00005b50 	andeq	r5, r0, r0, asr fp
    36e4:	81742268 	cmnhi	r4, r8, ror #4
    36e8:	00000290 	muleq	r0, r0, r2
    36ec:	fffefefe 			@ <UNDEFINED> instruction: 0xfffefefe
    36f0:	ff00ffff 			@ <UNDEFINED> instruction: 0xff00ffff
    36f4:	00010000 	andeq	r0, r1, r0
    36f8:	02010101 	andeq	r0, r1, #1073741824	@ 0x40000000
    36fc:	ffff0202 			@ <UNDEFINED> instruction: 0xffff0202
    3700:	0000ff00 	andeq	pc, r0, r0, lsl #30
    3704:	01010001 	tsteq	r1, r1
    3708:	0000ff00 	andeq	pc, r0, r0, lsl #30
    370c:	00000001 	andeq	r0, r0, r1
    3710:	00004778 	andeq	r4, r0, r8, ror r7
    3714:	eafff2a1 	b	0x1a0
    3718:	00004778 	andeq	r4, r0, r8, ror r7
    371c:	eafff29a 	b	0x18c
    3720:	00004778 	andeq	r4, r0, r8, ror r7
    3724:	eafff31f 	b	0x3a8
    3728:	00004778 	andeq	r4, r0, r8, ror r7
    372c:	eafff2fd 	b	0x328
    3730:	00004778 	andeq	r4, r0, r8, ror r7
    3734:	eafff569 	b	0xce0
    3738:	00002665 	andeq	r2, r0, r5, ror #12
    373c:	000026cf 	andeq	r2, r0, pc, asr #13
    3740:	000026ef 	andeq	r2, r0, pc, ror #13
    3744:	00002709 	andeq	r2, r0, r9, lsl #14
    3748:	0000271d 	andeq	r2, r0, sp, lsl r7
    374c:	00002665 	andeq	r2, r0, r5, ror #12
    3750:	00002665 	andeq	r2, r0, r5, ror #12
    3754:	00002665 	andeq	r2, r0, r5, ror #12
    3758:	00002665 	andeq	r2, r0, r5, ror #12
    375c:	0000274b 	andeq	r2, r0, fp, asr #14
    3760:	00002755 	andeq	r2, r0, r5, asr r7
    3764:	00002769 	andeq	r2, r0, r9, ror #14
    3768:	0000277b 	andeq	r2, r0, fp, ror r7
    376c:	000027a9 	andeq	r2, r0, r9, lsr #15
    3770:	000027bb 			@ <UNDEFINED> instruction: 0x000027bb
    3774:	000027cf 	andeq	r2, r0, pc, asr #15
    3778:	000027e3 	andeq	r2, r0, r3, ror #15
    377c:	000027f5 	strdeq	r2, [r0], -r5
    3780:	00002805 	andeq	r2, r0, r5, lsl #16
    3784:	0000280f 	andeq	r2, r0, pc, lsl #16
    3788:	0000281f 	andeq	r2, r0, pc, lsl r8
    378c:	00002665 	andeq	r2, r0, r5, ror #12
    3790:	00002665 	andeq	r2, r0, r5, ror #12
    3794:	00002837 	andeq	r2, r0, r7, lsr r8
    3798:	00002665 	andeq	r2, r0, r5, ror #12
    379c:	00002665 	andeq	r2, r0, r5, ror #12
    37a0:	00002665 	andeq	r2, r0, r5, ror #12
    37a4:	0000284b 	andeq	r2, r0, fp, asr #16
    37a8:	00002665 	andeq	r2, r0, r5, ror #12
    37ac:	00002629 	andeq	r2, r0, r9, lsr #12
    37b0:	0000170b 	andeq	r1, r0, fp, lsl #14
    37b4:	000023e7 	andeq	r2, r0, r7, ror #7
    37b8:	00001535 	andeq	r1, r0, r5, lsr r5
    37bc:	0000159d 	muleq	r0, sp, r5
    37c0:	000023c7 	andeq	r2, r0, r7, asr #7
    37c4:	000023b1 			@ <UNDEFINED> instruction: 0x000023b1
    37c8:	00003c00 	andeq	r3, r0, r0, lsl #24
    37cc:	0000382c 	andeq	r3, r0, ip, lsr #16
    37d0:	bc4d00ff 	mcrrlt	0, 15, r0, sp, cr15
    37d4:	00003c00 	andeq	r3, r0, r0, lsl #24
    37d8:	000039d0 	ldrdeq	r3, [r0], -r0
    37dc:	f99aa5ff 			@ <UNDEFINED> instruction: 0xf99aa5ff
    37e0:	00003c00 	andeq	r3, r0, r0, lsl #24
    37e4:	0000382c 	andeq	r3, r0, ip, lsr #16
    37e8:	f680a5ff 			@ <UNDEFINED> instruction: 0xf680a5ff
    37ec:	5fbb00bc 	svcpl	0x00bb00bc
    37f0:	4bbe00bd 	blmi	0xfef83aec
    37f4:	d58f40bf 	strle	r4, [pc, #191]	@ 0x38bb
    37f8:	b186705b 	orrlt	r7, r6, fp, asr r0
    37fc:	00bd00bc 	ldrhteq	r0, [sp], ip
    3800:	40bf4bbe 	ldrhtmi	r4, [pc], lr
    3804:	7056d58a 	subsvc	sp, r6, sl, lsl #11
    3808:	00bcb186 	adcseq	fp, ip, r6, lsl #3
    380c:	4bbe00bd 	blmi	0xfef83b08
    3810:	d58540bf 	strle	r4, [r5, #191]	@ 0xbf
    3814:	b1867053 	orrlt	r7, r6, r3, asr r0
    3818:	bc000003 	stclt	0, cr0, [r0], {3}
    381c:	000037c8 	andeq	r3, r0, r8, asr #15
    3820:	000037ec 	andeq	r3, r0, ip, ror #15
    3824:	000037fc 	strdeq	r3, [r0], -ip
    3828:	0000380a 	andeq	r3, r0, sl, lsl #16
    382c:	40000000 	andmi	r0, r0, r0
    3830:	0082b495 	umulleq	fp, r2, r5, r4
    3834:	00000001 	andeq	r0, r0, r1
    3838:	00000021 	andeq	r0, r0, r1, lsr #32
    383c:	47311900 	ldrmi	r1, [r1, -r0, lsl #18]!
    3840:	7d756a5a 	vldmdbvc	r5!, {s13-s102}
    3844:	6a757d7f 	bvs	0x1d62e48
    3848:	1931475a 	ldmdbne	r1!, {r1, r3, r4, r6, r8, r9, sl, lr}
    384c:	b9cfe700 	stmiblt	pc, {r8, r9, sl, sp, lr, pc}^	@ <UNPREDICTABLE>
    3850:	838b96a6 	orrhi	r9, fp, #174063616	@ 0xa600000
    3854:	968b8381 	strls	r8, [fp], r1, lsl #7
    3858:	e7cfb9a6 	strb	fp, [pc, r6, lsr #19]
    385c:	00001900 	andeq	r1, r0, r0, lsl #18
    3860:	54bb00bc 	ldrtpl	r0, [fp], #188	@ 0xbc
    3864:	55be00bd 	ldrpl	r0, [lr, #189]!	@ 0xbd
    3868:	d58f40bf 	strle	r4, [pc, #191]	@ 0x392f
    386c:	b1867056 	orrlt	r7, r6, r6, asr r0
    3870:	00bd00bc 	ldrhteq	r0, [sp], ip
    3874:	40bf55be 	ldrhtmi	r5, [pc], lr
    3878:	705bd58a 	subsvc	sp, fp, sl, lsl #11
    387c:	00bcb186 	adcseq	fp, ip, r6, lsl #3
    3880:	55be00bd 	ldrpl	r0, [lr, #189]!	@ 0xbd
    3884:	d58540bf 	strle	r4, [r5, #191]	@ 0xbf
    3888:	b1867056 	orrlt	r7, r6, r6, asr r0
    388c:	00bd00bc 	ldrhteq	r0, [sp], ip
    3890:	40bf55be 	ldrhtmi	r5, [pc], lr
    3894:	86705bd5 			@ <UNDEFINED> instruction: 0x86705bd5
    3898:	000000b1 	strheq	r0, [r0], -r1
    389c:	b7000004 	strlt	r0, [r0, -r4]
    38a0:	000037c8 	andeq	r3, r0, r8, asr #15
    38a4:	00003860 	andeq	r3, r0, r0, ror #16
    38a8:	00003870 	andeq	r3, r0, r0, ror r8
    38ac:	0000387e 	andeq	r3, r0, lr, ror r8
    38b0:	0000388c 	andeq	r3, r0, ip, lsl #17
    38b4:	4abb00bc 	bmi	0xfeec3bac
    38b8:	78be01bd 	ldmvc	lr!, {r0, r2, r3, r4, r5, r7, r8}
    38bc:	41e740bf 	strhmi	r4, [r7, #15]!
    38c0:	bcb19860 	ldclt	8, cr9, [r1], #384	@ 0x180
    38c4:	be01bd00 	cdplt	13, 0, cr11, cr1, cr0, {0}
    38c8:	e740bf78 	smlsldx	fp, r0, r8, pc	@ <UNPREDICTABLE>
    38cc:	b1987048 	orrslt	r7, r8, r8, asr #32
    38d0:	01bd00bc 	ldrheq	r0, [sp, ip]!
    38d4:	40bf78be 	ldrhtmi	r7, [pc], lr
    38d8:	6c4ce782 	mcrrvs	7, 8, lr, ip, cr2
    38dc:	00bcb198 	umlalseq	fp, ip, r8, r1
    38e0:	78be01bd 	ldmvc	lr!, {r0, r2, r3, r4, r5, r7, r8}
    38e4:	e78440bf 			@ <UNDEFINED> instruction: 0xe78440bf
    38e8:	b1986c4f 	orrslt	r6, r8, pc, asr #24
    38ec:	01bd00bc 	ldrheq	r0, [sp, ip]!
    38f0:	40bf78be 	ldrhtmi	r7, [pc], lr
    38f4:	6c53e786 	mrrcvs	7, 8, lr, r3, cr6
    38f8:	00bcb198 	umlalseq	fp, ip, r8, r1
    38fc:	78be01bd 	ldmvc	lr!, {r0, r2, r3, r4, r5, r7, r8}
    3900:	e78a40bf 			@ <UNDEFINED> instruction: 0xe78a40bf
    3904:	b1986056 	orrslt	r6, r8, r6, asr r0
    3908:	d0000006 	andle	r0, r0, r6
    390c:	000037c8 	andeq	r3, r0, r8, asr #15
    3910:	000038b4 			@ <UNDEFINED> instruction: 0x000038b4
    3914:	000038c3 	andeq	r3, r0, r3, asr #17
    3918:	000038d0 	ldrdeq	r3, [r0], -r0
    391c:	000038de 	ldrdeq	r3, [r0], -lr
    3920:	000038ec 	andeq	r3, r0, ip, ror #17
    3924:	000038fa 	strdeq	r3, [r0], -sl
    3928:	63bb00bc 			@ <UNDEFINED> instruction: 0x63bb00bc
    392c:	5ebe00bd 	mrcpl	0, 5, r0, cr14, cr13, {5}
    3930:	d58f40bf 	strle	r4, [pc, #191]	@ 0x39f7
    3934:	b1867864 	orrlt	r7, r6, r4, ror #16
    3938:	00bd00bc 	ldrhteq	r0, [sp], ip
    393c:	40bf5ebe 	ldrhtmi	r5, [pc], lr
    3940:	7862d58a 	stmdavc	r2!, {r1, r3, r7, r8, sl, ip, lr, pc}^
    3944:	00bcb186 	adcseq	fp, ip, r6, lsl #3
    3948:	5ebe00bd 	mrcpl	0, 5, r0, cr14, cr13, {5}
    394c:	d58540bf 	strle	r4, [r5, #191]	@ 0xbf
    3950:	b1867860 	orrlt	r7, r6, r0, ror #16
    3954:	00bd00bc 	ldrhteq	r0, [sp], ip
    3958:	40bf5ebe 	ldrhtmi	r5, [pc], lr
    395c:	86505fd5 			@ <UNDEFINED> instruction: 0x86505fd5
    3960:	bd00bcb1 	stclt	12, cr11, [r0, #-708]	@ 0xfffffd3c
    3964:	bf5ebe00 	svclt	0x005ebe00
    3968:	66d59440 	ldrbvs	r9, [r5], r0, asr #8
    396c:	bcb18678 	ldclt	6, cr8, [r1], #480	@ 0x1e0
    3970:	be00bd00 	cdplt	13, 0, cr11, cr0, cr0, {0}
    3974:	9840bf5e 	stmdals	r0, {r1, r2, r3, r4, r6, r8, r9, sl, fp, ip, sp, pc}^
    3978:	7867db81 	stmdavc	r7!, {r0, r7, r8, r9, fp, ip, lr, pc}^
    397c:	0000b18c 	andeq	fp, r0, ip, lsl #3
    3980:	b2000006 	andlt	r0, r0, #6
    3984:	000037c8 	andeq	r3, r0, r8, asr #15
    3988:	00003928 	andeq	r3, r0, r8, lsr #18
    398c:	00003938 	andeq	r3, r0, r8, lsr r9
    3990:	00003946 	andeq	r3, r0, r6, asr #18
    3994:	00003954 	andeq	r3, r0, r4, asr r9
    3998:	00003961 	andeq	r3, r0, r1, ror #18
    399c:	0000396f 	andeq	r3, r0, pc, ror #18
    39a0:	4abb00bc 	bmi	0xfeec3c98
    39a4:	55be02bd 	ldrpl	r0, [lr, #701]!	@ 0x2bd
    39a8:	d38440bf 	orrle	r4, r4, #191	@ 0xbf
    39ac:	b185786c 	orrlt	r7, r5, ip, ror #16
    39b0:	02bd00bc 	adcseq	r0, sp, #188	@ 0xbc
    39b4:	40bf55be 	ldrhtmi	r5, [pc], lr
    39b8:	847060d3 	ldrbthi	r6, [r0], #-211	@ 0xffffff2d
    39bc:	000000b1 	strheq	r0, [r0], -r1
    39c0:	d0000002 	andle	r0, r0, r2
    39c4:	000037c8 	andeq	r3, r0, r8, asr #15
    39c8:	000039a0 	andeq	r3, r0, r0, lsr #19
    39cc:	000039b0 			@ <UNDEFINED> instruction: 0x000039b0
    39d0:	40000000 	andmi	r0, r0, r0
    39d4:	002f5658 	eoreq	r5, pc, r8, asr r6	@ <UNPREDICTABLE>
    39d8:	00000000 	andeq	r0, r0, r0
    39dc:	00000520 	andeq	r0, r0, r0, lsr #10
    39e0:	3e483b0b 	vmlacc.f64	d19, d8, d11
    39e4:	c6c3eb24 	strbgt	lr, [r3], r4, lsr #22
    39e8:	37350fdf 			@ <UNDEFINED> instruction: 0x37350fdf
    39ec:	adc0ff2a 	stclge	15, cr15, [r0, #168]	@ 0xa8
    39f0:	321ae2bd 	andscc	lr, sl, #-805306357	@ 0xd000000b
    39f4:	b0d61027 	sbcslt	r1, r6, r7, lsr #32
    39f8:	440dd9bd 	strmi	sp, [sp], #-2493	@ 0xfffff643
    39fc:	d10d3346 	tstle	sp, r6, asr #6
    3a00:	32f4d1be 	rscscc	sp, r4, #-2147483601	@ 0x8000002f
    3a04:	053e5258 	ldreq	r5, [lr, #-600]!	@ 0xfffffda8
    3a08:	f6c9b5c4 			@ <UNDEFINED> instruction: 0xf6c9b5c4
    3a0c:	1b3b4431 	blne	0xed4ad8
    3a10:	c9aca5d4 	stmibgt	ip!, {r2, r4, r6, r7, r8, sl, sp, pc}
    3a14:	23353503 	teqcs	r5, #12582912	@ 0xc00000
    3a18:	c7aeb3f3 			@ <UNDEFINED> instruction: 0xc7aeb3f3
    3a1c:	415134f1 	ldrshmi	r3, [r1, #-65]	@ 0xffffffbf
    3a20:	c4bae927 	ldrtgt	lr, [sl], #2343	@ 0x927
    3a24:	5c5115df 	cfldr64pl	mvdx1, [r1], {223}	@ 0xdf
    3a28:	b0db264d 	sbcslt	r2, fp, sp, asr #12
    3a2c:	4619ddbb 			@ <UNDEFINED> instruction: 0x4619ddbb
    3a30:	b1f63045 	mvnslt	r3, r5, asr #32
    3a34:	26e5b8a2 	strbtcs	fp, [r5], r2, lsr #17
    3a38:	cb0e2f3d 	blgt	0x38f734
    3a3c:	15d8b8a6 	ldrbne	fp, [r8, #2214]	@ 0x8a6
    3a40:	08374d4e 	ldmdaeq	r7!, {r1, r2, r3, r6, r8, sl, fp, lr}
    3a44:	f8cfb7c6 			@ <UNDEFINED> instruction: 0xf8cfb7c6
    3a48:	3d58603a 	ldclcc	0, cr6, [r8, #-232]	@ 0xffffff18
    3a4c:	caafbbff 	bgt	0xfebf2a50
    3a50:	3e4e39fc 			@ <UNDEFINED> instruction: 0x3e4e39fc
    3a54:	a9a0ce15 	stmibge	r0!, {r0, r2, r4, r9, sl, fp, lr, pc}
    3a58:	3c3c09cd 			@ <UNDEFINED> instruction: 0x3c3c09cd
    3a5c:	a9aeec22 	stmibge	lr!, {r1, r5, sl, fp, sp, lr, pc}
    3a60:	5639f6c7 	ldrtpl	pc, [r9], -r7, asr #13	@ <UNPREDICTABLE>
    3a64:	b4e32244 	strbtlt	r2, [r3], #580	@ 0x244
    3a68:	561ae2be 			@ <UNDEFINED> instruction: 0x561ae2be
    3a6c:	d61e4c64 	ldrle	r4, [lr], -r4, ror #24
    3a70:	1de2b8aa 	stclne	8, cr11, [r2, #680]!	@ 0x2a8
    3a74:	f02a4c4d 			@ <UNDEFINED> instruction: 0xf02a4c4d
    3a78:	ecba9daf 	ldc	13, cr9, [sl], #700	@ 0x2bc
    3a7c:	06314529 	ldrteq	r4, [r1], -r9, lsr #10
    3a80:	dfb5a3c9 	svcle	0x00b5a3c9
    3a84:	32535019 	subscc	r5, r3, #25
    3a88:	cfb1c501 	svcgt	0x00b1c501
    3a8c:	5b663bff 	blpl	0x1992a90
    3a90:	a9b7fa34 	ldmibge	r7!, {r2, r4, r5, r9, fp, ip, sp, lr, pc}
    3a94:	563a00ce 	ldrtpl	r0, [sl], -lr, asr #1
    3a98:	9dce0c3c 	stclls	12, cr0, [lr, #240]	@ 0xf0
    3a9c:	400cd5a8 	andmi	sp, ip, r8, lsr #11
    3aa0:	b0e71a41 	rsclt	r1, r7, r1, asr #20
    3aa4:	38fecaa4 	ldmcc	lr!, {r2, r5, r7, r9, fp, lr, pc}^
    3aa8:	e217435b 	ands	r4, r7, #1811939329	@ 0x6c000001
    3aac:	1de9bab2 			@ <UNDEFINED> instruction: 0x1de9bab2
    3ab0:	15466a57 	strbne	r6, [r6, #-2647]	@ 0xfffff5a9
    3ab4:	ebbaa6d7 	bl	0xfeead618
    3ab8:	1f4f521c 	svcne	0x004f521c
    3abc:	c19ab2ee 	orrsgt	fp, sl, lr, ror #5
    3ac0:	2d4b28f2 	stclcs	8, cr2, [fp, #-968]	@ 0xfffffc38
    3ac4:	b3a2cdfd 			@ <UNDEFINED> instruction: 0xb3a2cdfd
    3ac8:	564f19e9 	strbpl	r1, [pc], -r9, ror #19
    3acc:	acc8fd29 	stclge	13, cr15, [r8], {41}	@ 0x29
    3ad0:	6a3805d3 	bvs	0xe05224
    3ad4:	b9f9275b 	ldmiblt	r9!, {r0, r1, r3, r4, r6, r8, r9, sl, sp}^
    3ad8:	3902d7a7 	stmdbcc	r2, {r0, r1, r2, r5, r7, r8, r9, sl, ip, lr, pc}
    3adc:	d403345c 	strle	r3, [r3], #-1116	@ 0xfffffba4
    3ae0:	09dfaa9c 	ldmibeq	pc, {r2, r3, r4, r7, r9, fp, sp, pc}^	@ <UNPREDICTABLE>
    3ae4:	e70d4341 	str	r4, [sp, -r1, asr #6]
    3ae8:	03d2a1b7 	bicseq	sl, r2, #-1073741779	@ 0xc000002d
    3aec:	0c3e5c33 	ldceq	12, cr5, [lr], #-204	@ 0xffffff34
    3af0:	f3bab3e7 	sha256su0.32	<illegal reg q5.5>, <illegal reg q11.5>
    3af4:	3a6b541c 	bcc	0x1ad8b6c
    3af8:	bfa5db0e 	svclt	0x00a5db0e
    3afc:	4d5319f2 	vldrmi.16	s3, [r3, #-484]	@ 0xfffffe1c	@ <UNPREDICTABLE>
    3b00:	9ab7f113 	bls	0xfedfff54
    3b04:	4d24f5ca 	cfstr32mi	mvfx15, [r4, #-808]!	@ 0xfffffcd8
    3b08:	a4d6f625 	ldrbge	pc, [r6], #1573	@ 0x625	@ <UNPREDICTABLE>
    3b0c:	4b17f2b6 	blmi	0x6005ec
    3b10:	cdfb1d53 	ldclgt	13, cr1, [fp, #332]!	@ 0x14c
    3b14:	340adaab 	strcc	sp, [sl], #-2731	@ 0xfffff555
    3b18:	fb1c5369 	blx	0x7188c6
    3b1c:	03e0abbb 	mvneq	sl, #191488	@ 0x2ec00
    3b20:	00295b37 	eoreq	r5, r9, r7, lsr fp
    3b24:	e6b19fdb 	ssat	r9, #18, fp, asr #31
    3b28:	033e3e06 	teqeq	lr, #6, 28	@ 0x60
    3b2c:	daa3beeb 	ble	0xfe8f36e0
    3b30:	35582d05 	ldrbcc	r2, [r8, #-3333]	@ 0xfffff2fb
    3b34:	bfb6ec06 	svclt	0x00b6ec06
    3b38:	66501afa 			@ <UNDEFINED> instruction: 0x66501afa
    3b3c:	aae00b2f 	bge	0xff806800
    3b40:	5117f5c7 	tstpl	r7, r7, asr #11	@ <UNPREDICTABLE>
    3b44:	bdf50b44 			@ <UNDEFINED> instruction: 0xbdf50b44
    3b48:	20f5d2a0 	rscscs	sp, r5, r0, lsr #5
    3b4c:	def51b47 	vcmple.f64	d17, #0.0
    3b50:	14f8bcaa 	ldrbtne	fp, [r8], #3242	@ 0xcaa
    3b54:	fd154b44 	ldc2	11, cr4, [r5, #-272]	@ 0xfffffef0	@ <UNPREDICTABLE>
    3b58:	0be1b1d4 	bleq	0xff8702b0
    3b5c:	1649622f 	strbne	r6, [r9], -pc, lsr #4
    3b60:	e4b4c3fc 	ldrt	ip, [r4], #1020	@ 0x3fc
    3b64:	20513302 	subscs	r3, r1, r2, lsl #6
    3b68:	baa9e100 	blt	0xfea7bf70
    3b6c:	333704e9 	teqcc	r7, #-385875968	@ 0xe9000000
    3b70:	acc6f000 	stclge	0, cr15, [r6], {0}
    3b74:	4c2705e0 	cfstr32mi	mvfx0, [r7], #-896	@ 0xfffffc80
    3b78:	bef0052c 	cdplt	5, 15, cr0, cr0, cr12, {1}
    3b7c:	4919fdc7 	ldmdbmi	r9, {r0, r1, r2, r6, r7, r8, sl, fp, ip, sp, lr, pc}
    3b80:	e40a2858 	str	r2, [sl], #-2136	@ 0xfffff7a8
    3b84:	15f5cfb6 	ldrbne	ip, [r5, #4022]!	@ 0xfb6
    3b88:	f80a3847 			@ <UNDEFINED> instruction: 0xf80a3847
    3b8c:	f6d7adc6 			@ <UNDEFINED> instruction: 0xf6d7adc6
    3b90:	f914391b 			@ <UNDEFINED> instruction: 0xf914391b
    3b94:	f8c5b5e4 			@ <UNDEFINED> instruction: 0xf8c5b5e4
    3b98:	123d3911 	eorsne	r3, sp, #278528	@ 0x44000
    3b9c:	e5bedbff 	ldr	sp, [lr, #3071]!	@ 0xbff
    3ba0:	3d522c0a 	ldclcc	12, cr2, [r2, #-40]	@ 0xffffffd8
    3ba4:	c1cffd15 	bicgt	pc, pc, r5, lsl sp	@ <UNPREDICTABLE>
    3ba8:	412c02e4 	smulwtmi	ip, r4, r2
    3bac:	b8e7021d 	stmialt	r7!, {r0, r2, r3, r4, r9}^
    3bb0:	2a04e8c2 	bcs	0x13dec0
    3bb4:	d1f50227 	mvnsle	r0, r7, lsr #4
    3bb8:	2004e1ba 			@ <UNDEFINED> instruction: 0x2004e1ba
    3bbc:	f308253a 	vqrshl.u8	d2, d26, d8
    3bc0:	18fad1cd 	ldmne	sl!, {r0, r2, r3, r6, r7, r8, ip, lr, pc}^
    3bc4:	0927483c 	stmdbeq	r7!, {r2, r3, r4, r5, fp, lr}
    3bc8:	f4d3c9eb 			@ <UNDEFINED> instruction: 0xf4d3c9eb
    3bcc:	0e2e3514 	mcreq	5, 1, r3, cr14, cr4, {0}
    3bd0:	d6bdd5fa 			@ <UNDEFINED> instruction: 0xd6bdd5fa
    3bd4:	132612f6 			@ <UNDEFINED> instruction: 0x132612f6
    3bd8:	ccc7eaff 	vstmiagt	r7, {s29-s283}
    3bdc:	2f280ff5 	svccs	0x00280ff5
    3be0:	cee50016 	mcrgt	0, 7, r0, cr5, cr6, {0}
    3be4:	3e2609e6 	vnmulcc.f16	s0, s13, s13	@ <UNPREDICTABLE>
    3be8:	dffe1736 	svcle	0x00fe1736
    3bec:	2002e2ce 	andcs	lr, r2, lr, asr #5
    3bf0:	ee051f2f 	cdp	15, 0, cr1, cr5, cr15, {1}
    3bf4:	01e6c7cd 	mvneq	ip, sp, asr #15
    3bf8:	f90b1c16 			@ <UNDEFINED> instruction: 0xf90b1c16
    3bfc:	02dfcadf 	sbcseq	ip, pc, #913408	@ 0xdf000
    3c00:	0c242515 	cfstr32eq	mvfx2, [r4], #-84	@ 0xffffffac
    3c04:	f7d9def7 			@ <UNDEFINED> instruction: 0xf7d9def7
    3c08:	28372b16 	ldmdacs	r7!, {r1, r2, r4, r8, r9, fp, sp}
    3c0c:	d5dbf50a 	ldrble	pc, [fp, #1290]	@ 0x50a	@ <UNPREDICTABLE>
    3c10:	27210ff2 			@ <UNDEFINED> instruction: 0x27210ff2
    3c14:	cbe5fd15 	blgt	0xff983070
    3c18:	1405f5d3 	strne	pc, [r5], #-1491	@ 0xfffffa2d
    3c1c:	daf10417 	ble	0xffc44c80
    3c20:	1309f1d2 	movwne	pc, #37330	@ 0x91d2	@ <UNPREDICTABLE>
    3c24:	f1021b23 			@ <UNDEFINED> instruction: 0xf1021b23
    3c28:	1b06e4de 	blne	0x1bcfa8
    3c2c:	011a3229 	tsteq	sl, r9, lsr #4
    3c30:	00e0d9f1 	strdeq	sp, [r0], #145	@ 0x91	@ <UNPREDICTABLE>
    3c34:	09231f10 	stmdbeq	r3!, {r4, r8, r9, sl, fp, ip}
    3c38:	e3cce0f8 	bic	lr, ip, #248	@ 0xf8
    3c3c:	131603fb 	tstne	r6, #-335544317	@ 0xec000003
    3c40:	ddd8effe 	ldclle	15, cr14, [r8, #1016]	@ 0x3f8
    3c44:	241306fd 	ldrcs	r0, [r3], #-1789	@ 0xfffff903
    3c48:	e0f0fe11 	rscs	pc, r0, r1, lsl lr	@ <UNPREDICTABLE>
    3c4c:	2b180ff3 	blcs	0x607c20
    3c50:	ec000d2a 	stc	13, cr0, [r0], {42}	@ 0x2a
    3c54:	0f04efd9 	svceq	0x0004efd9
    3c58:	f7021a24 			@ <UNDEFINED> instruction: 0xf7021a24
    3c5c:	f7efd2d8 			@ <UNDEFINED> instruction: 0xf7efd2d8
    3c60:	fc0b1b06 	stc2	11, cr1, [fp], {6}	@ <UNPREDICTABLE>
    3c64:	ffebd6eb 			@ <UNDEFINED> instruction: 0xffebd6eb
    3c68:	08201b01 	stmdaeq	r0!, {r0, r8, r9, fp, ip}
    3c6c:	00e5ebfe 	strdeq	lr, [r5], #190	@ 0xbe	@ <UNPREDICTABLE>
    3c70:	1e2d180c 	cdpne	8, 2, cr1, cr13, cr12, {0}
    3c74:	dfe3000a 	svcle	0x00e3000a
    3c78:	251500f8 	ldrcs	r0, [r5, #-248]	@ 0xffffff08
    3c7c:	d0ef0310 	rscle	r0, pc, r0, lsl r3	@ <UNPREDICTABLE>
    3c80:	13f4eede 	mvnsne	lr, #3552	@ 0xde0
    3c84:	e3fc061a 	mvns	r0, #27262976	@ 0x1a00000
    3c88:	07f6f2dc 	ubfxeq	pc, ip, #5, #23
    3c8c:	fc071823 	stc2	8, cr1, [r7], {35}	@ 0x23
    3c90:	0802efe6 	stmdaeq	r2, {r1, r2, r5, r6, r7, r8, r9, sl, fp, sp, lr, pc}
    3c94:	0b162a21 	bleq	0x58e520
    3c98:	f7e8ddf7 			@ <UNDEFINED> instruction: 0xf7e8ddf7
    3c9c:	0d202002 	stceq	0, cr2, [r0, #-8]!
    3ca0:	e3d2e102 	bics	lr, r2, #-2147483648	@ 0x80000000
    3ca4:	151effe8 	ldrne	pc, [lr, #-4072]	@ 0xfffff018
    3ca8:	e3ddf607 	bics	pc, sp, #7340032	@ 0x700000
    3cac:	2315f6ed 	tstcs	r5, #248512512	@ 0xed00000	@ <UNPREDICTABLE>
    3cb0:	e7f30814 			@ <UNDEFINED> instruction: 0xe7f30814
    3cb4:	270efef4 			@ <UNDEFINED> instruction: 0x270efef4
    3cb8:	eb071624 	bl	0x1c9550
    3cbc:	0ef5eade 			@ <UNDEFINED> instruction: 0x0ef5eade
    3cc0:	f80e1c26 			@ <UNDEFINED> instruction: 0xf80e1c26
    3cc4:	edded5d7 	cfldr64	mvdx13, [lr, #860]	@ 0x35c
    3cc8:	05142110 	ldreq	r2, [r4, #-272]	@ 0xfffffef0
    3ccc:	e9e1dcec 	stmib	r1!, {r2, r3, r5, r6, r7, sl, fp, ip, lr, pc}^
    3cd0:	13222001 			@ <UNDEFINED> instruction: 0x13222001
    3cd4:	f1e9ee01 			@ <UNDEFINED> instruction: 0xf1e9ee01
    3cd8:	242a1800 	strtcs	r1, [sl], #-2048	@ 0xfffff800
    3cdc:	dde3fd13 	stclle	13, cr15, [r3, #76]!	@ 0x4c
    3ce0:	291bfee8 	ldmdbcs	fp, {r3, r5, r6, r7, r9, sl, fp, ip, sp, lr, pc}
    3ce4:	d2eb081c 	rscle	r0, fp, #28, 16	@ 0x1c0000
    3ce8:	1efddfd1 	mrcne	15, 7, sp, cr13, cr1, {6}
    3cec:	e4fc1123 	ldrbt	r1, [ip], #291	@ 0x123
    3cf0:	10f0dbd6 	ldrsbtne	sp, [r0], #182	@ 0xb6
    3cf4:	fa0d2327 	blx	0x34c998
    3cf8:	08f1e3e9 	ldmeq	r1!, {r0, r3, r5, r6, r7, r8, r9, sp, lr, pc}^
    3cfc:	0a222f21 	beq	0x88f988
    3d00:	f0d9dbf4 			@ <UNDEFINED> instruction: 0xf0d9dbf4
    3d04:	162c270a 	strtne	r2, [ip], -sl, lsl #14
    3d08:	d0c7dffe 	strdle	sp, [r7], #254	@ 0xfe
    3d0c:	232a0eec 			@ <UNDEFINED> instruction: 0x232a0eec
    3d10:	ccd9f408 	cfldrdgt	mvd15, [r9], {8}
    3d14:	301dffe1 	andscc	pc, sp, r1, ror #31
    3d18:	ddf5031d 	ldclle	3, cr0, [r5, #116]!	@ 0x74
    3d1c:	2d11fbdf 	vldrcs	d15, [r1, #-892]	@ 0xfffffc84
    3d20:	e8011734 	stmda	r1, {r2, r4, r5, r8, r9, sl, ip}
    3d24:	18fddece 	ldmne	sp!, {r1, r2, r3, r6, r7, r9, sl, fp, ip, lr, pc}^
    3d28:	f40a2a35 	vst1.8	{d2-d3}, [sl :256], r5
    3d2c:	fcdcbfcd 	ldc2l	15, cr11, [ip], {205}	@ 0xcd
    3d30:	ff19341f 			@ <UNDEFINED> instruction: 0xff19341f
    3d34:	f1ccc6ea 			@ <UNDEFINED> instruction: 0xf1ccc6ea
    3d38:	0e322e0b 	cdpeq	14, 3, cr2, cr2, cr11, {0}
    3d3c:	e9d0e8ff 	ldmib	r0, {r0, r1, r2, r3, r4, r5, r6, r7, fp, sp, lr, pc}^
    3d40:	2c3e1d04 	ldccs	13, cr1, [lr], #-16
    3d44:	cad4fb0a 	bgt	0xff542974
    3d48:	3d2a08ee 	stccc	8, cr0, [sl, #-952]!	@ 0xfffffc48
    3d4c:	b9e3001a 	stmiblt	r3!, {r1, r3, r4}^
    3d50:	340cefc7 	strcc	lr, [ip], #-4039	@ 0xfffff039
    3d54:	d3f90930 	mvnsle	r0, #48, 18	@ 0xc0000
    3d58:	1dfedfb9 	ldclne	15, cr13, [lr, #740]!	@ 0x2e4
    3d5c:	f804233f 			@ <UNDEFINED> instruction: 0xf804233f
    3d60:	0cf9d2d0 	lfmeq	f5, 3, [r9], #832	@ 0x340
    3d64:	04184133 	ldreq	r4, [r8], #-307	@ 0xfffffecd
    3d68:	fdd8c0e8 	ldc2l	0, cr12, [r8, #928]	@ 0x3a0
    3d6c:	0a334116 	beq	0xcd41cc
    3d70:	deb2c6f8 	mrcle	6, 5, ip, cr2, cr8, {7}
    3d74:	1d4224fe 	cfstrdne	mvd2, [r2, #-1016]	@ 0xfffffc08
    3d78:	c4b6ea00 	ldrtgt	lr, [r6], #2560	@ 0xa00
    3d7c:	3d3809f2 			@ <UNDEFINED> instruction: 0x3d3809f2
    3d80:	c1e1000f 	mvngt	r0, pc
    3d84:	493b0be5 	ldmdbmi	fp!, {r0, r2, r5, r6, r7, r8, r9, fp}
    3d88:	b9e30319 	stmiblt	r3!, {r0, r3, r4, r8, r9}^
    3d8c:	4b1bffd6 	blmi	0x703cec
    3d90:	bef70b39 	vmovlt.u8	r0, d7[5]
    3d94:	2b00e0ac 	blcs	0x3c04c
    3d98:	e300204a 	movw	r2, #74	@ 0x4a
    3d9c:	0df2c0ab 	ldcleq	0, cr12, [r2, #684]!	@ 0x2ac
    3da0:	fc10423f 	ldc2	2, cr4, [r0], {63}	@ 0x3f
    3da4:	00e1bbd7 	ldrdeq	fp, [r1], #183	@ 0xb7	@ <UNPREDICTABLE>
    3da8:	09334f24 	ldmdbeq	r3!, {r2, r5, r8, r9, sl, fp, lr}
    3dac:	efc0c6f5 	svc	0x00c0c6f5
    3db0:	1f4e390b 	svcne	0x004e390b
    3db4:	c4a8da00 	strtgt	sp, [r8], #2560	@ 0xa00
    3db8:	3c4615f6 	cfstr64cc	mvdx1, [r6], {246}	@ 0xf6
    3dbc:	aabff30a 	bge	0xff0009ec
    3dc0:	4d2900dc 	stcmi	0, cr0, [r9, #-880]!	@ 0xfffffc90
    3dc4:	bfe90029 	svclt	0x00e90029
    3dc8:	4111f4c8 	tstmi	r1, r8, asr #9	@ <UNPREDICTABLE>
    3dcc:	dbfd1a4a 	blle	0xfff4a6fc
    3dd0:	2400d7ba 	strcs	sp, [r0], #-1978	@ 0xfffff846
    3dd4:	ed0a3b4e 	vstr	d3, [sl, #-312]	@ 0xfffffec8
    3dd8:	08e2aeb9 	stmiaeq	r2!, {r0, r3, r4, r5, r7, r9, sl, fp, sp, pc}^
    3ddc:	fb224c33 	blx	0x896eb2
    3de0:	f4c0a9d6 	vst2.32	{d26[1],d28[1]}, [r0 :64], r6
    3de4:	10424316 	subne	r4, r2, r6, lsl r3
    3de8:	ddbbcef1 	ldcle	14, cr12, [fp, #964]!	@ 0x3c4
    3dec:	354f2d05 	strbcc	r2, [pc, #-3333]	@ 0x30ef
    3df0:	c3c5e904 	bicgt	lr, r5, #4, 18	@ 0x10000
    3df4:	4e3f15ef 	cfsub64mi	mvdx1, mvdx15, mvdx15
    3df8:	abd1f820 	blge	0xff481e80
    3dfc:	4821fcc8 	stmdami	r1!, {r3, r6, r7, sl, fp, ip, sp, lr, pc}
    3e00:	b8e4093d 	stmialt	r4!, {r0, r2, r3, r4, r5, r8, fp}^
    3e04:	3109dcad 	smlatbcc	r9, sp, ip, sp
    3e08:	daf92a4b 	ble	0xffe4e73c
    3e0c:	1df4c7bf 	ldclne	7, cr12, [r4, #764]!	@ 0x2fc
    3e10:	f0194944 			@ <UNDEFINED> instruction: 0xf0194944
    3e14:	07d7c0d4 			@ <UNDEFINED> instruction: 0x07d7c0d4
    3e18:	063c4e2f 	ldrteq	r4, [ip], -pc, lsr #28
    3e1c:	e5b4badf 	ldr	fp, [r4, #2783]!	@ 0xadf
    3e20:	244a3914 	strbcs	r3, [sl], #-2324	@ 0xfffff6ec
    3e24:	c1adc8f0 	strdgt	ip, [sp, r0]!
    3e28:	404322f8 	strdmi	r2, [r3], #-40	@ 0xffffffd8
    3e2c:	bfc9e00d 	svclt	0x00c9e00d
    3e30:	4c350edb 	ldcmi	14, cr0, [r5], #-876	@ 0xfffffc94
    3e34:	cadbfc34 	bgt	0xff702f0c
    3e38:	4322f2c9 	msrmi	CPSR_x, #-1879048180	@ 0x9000000c
    3e3c:	c9ea204b 	stmibgt	sl!, {r0, r1, r3, r6, sp}^
    3e40:	2d03ccb4 	stccs	12, cr12, [r3, #-720]	@ 0xfffffd30
    3e44:	d3043c47 	movwle	r3, #19527	@ 0x4c47
    3e48:	14ddb3b7 	ldrbne	fp, [sp], #951	@ 0x3b7
    3e4c:	ed294636 	stc	6, cr4, [r9, #-216]!	@ 0xffffff28
    3e50:	f8c8c3cd 			@ <UNDEFINED> instruction: 0xf8c8c3cd
    3e54:	15454229 	strbne	r4, [r5, #-553]	@ 0xfffffdd7
    3e58:	dcc9d1e0 	stflep	f5, [r9], {224}	@ 0xe0
    3e5c:	3a4a3811 	bcc	0x1291ea8
    3e60:	bdbfd2fe 	lfmlt	f5, 1, [pc, #1016]!	@ 0x4260
    3e64:	463d21ec 	ldrtmi	r2, [sp], -ip, ror #3
    3e68:	b4bfe121 	ldrtlt	lr, [pc], #289	@ 0x3e70
    3e6c:	402c00c5 	eormi	r0, ip, r5, asr #1
    3e70:	c7d0053e 			@ <UNDEFINED> instruction: 0xc7d0053e
    3e74:	3919dec3 	ldmdbcc	r9, {r0, r1, r6, r7, r9, sl, fp, ip, lr, pc}
    3e78:	d1ef3047 	mvnle	r3, r7, asr #32
    3e7c:	2dfbd0d0 	ldclcs	0, cr13, [fp, #832]!	@ 0x340
    3e80:	dc1a4743 	ldcle	7, cr4, [sl], {67}	@ 0x43
    3e84:	0fd5bfc6 	svceq	0x00d5bfc6
    3e88:	fa384435 	blx	0xe14f64
    3e8c:	e4babac5 	ldrt	fp, [sl], #2757	@ 0xac5
    3e90:	2242381f 	subcs	r3, r2, #2031616	@ 0x1f0000
    3e94:	cdc6c6de 	stclgt	6, cr12, [r6, #888]	@ 0x378
    3e98:	40403101 	submi	r3, r0, r1, lsl #2
    3e9c:	d2d1d40a 	sbcsle	sp, r1, #167772160	@ 0xa000000
    3ea0:	473c1de6 	ldrmi	r1, [ip, -r6, ror #27]!
    3ea4:	c5c9f133 	strbgt	pc, [r9, #307]	@ 0x133	@ <UNPREDICTABLE>
    3ea8:	3e2bf8c9 	cdpcc	8, 2, cr15, cr11, cr9, {6}
    3eac:	bbd21741 	bllt	0xff489bb8
    3eb0:	320aceba 	andcc	ip, sl, #2976	@ 0xba0
    3eb4:	c6f8363d 			@ <UNDEFINED> instruction: 0xc6f8363d
    3eb8:	22e9c9c7 	rsccs	ip, r9, #3260416	@ 0x31c000
    3ebc:	e3234039 			@ <UNDEFINED> instruction: 0xe3234039
    3ec0:	07ded7cf 	ldrbeq	sp, [lr, pc, asr #15]
    3ec4:	0c3c3e31 	ldceq	14, cr3, [ip], #-196	@ 0xffffff3c
    3ec8:	e6ceccd6 			@ <UNDEFINED> instruction: 0xe6ceccd6
    3ecc:	293a3318 	ldmdbcs	sl!, {r3, r4, r8, r9, ip, sp}
    3ed0:	cbc5c5ed 	blgt	0xff17568c
    3ed4:	333121f2 	teqcc	r1, #-2147483588	@ 0x8000003c
    3ed8:	d1cbd810 	bicle	sp, fp, r0, lsl r8
    3edc:	332b0bde 			@ <UNDEFINED> instruction: 0x332b0bde
    3ee0:	dbd7fc2c 	blle	0xff602f98
    3ee4:	301ef8e1 	andscc	pc, lr, r1, ror #17
    3ee8:	d5eb1e35 	strble	r1, [fp, #3637]!	@ 0xe35
    3eec:	2504e1d7 	strcs	lr, [r4, #-471]	@ 0xfffffe29
    3ef0:	d7052c2f 	strle	r2, [r5, -pc, lsr #24]
    3ef4:	0de6d2ce 	sfmeq	f5, 3, [r6, #824]!	@ 0x338
    3ef8:	ef1c2a24 	svc	0x001c2a24
    3efc:	faded7d4 	blx	0xff7b9e54
    3f00:	0000000b 	andeq	r0, r0, fp
	...
