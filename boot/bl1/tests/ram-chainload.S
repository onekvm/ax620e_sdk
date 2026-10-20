/* Lab-only: caller disables caches/MMU, stages a zero-padded 2 MiB U-Boot
 * at 0x52000000, then executes this independent stub at 0x50000000.
 * It never returns to the factory U-Boot being replaced in RAM.
 */
.text
.global _start
_start:
	ldr x0, =0x52000000
	ldr x1, =0x5c000400
	ldr x2, =0x200000
	mov x3, x1
1:
	ldp x4, x5, [x0], #16
	stp x4, x5, [x1], #16
	subs x2, x2, #16
	b.ne 1b
	dsb sy
	ic iallu
	dsb sy
	isb
	br x3
.ltorg
