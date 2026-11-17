/* ARM64 Image entry. The payload is not a Linux kernel. */
.section .text.boot, "ax"
.global _image_start
_image_start:
    b _entry
    nop
    .quad 0                       /* match the CYE5 Image text_offset */
    .quad 0                       /* image_size, filled by the build tool */
    .quad 10                      /* match CYE5: LE, 4 KiB, placement bit */
    .quad 0
    .quad 0
    .quad 0
    .ascii "ARM\x64"
    .long 0

_entry:
    msr daifset, #15
    mov x19, x0                   /* preserve the inherited live FDT */
    adr x9, _image_start
    ldr x10, =_image_start
    cmp x9, x10
    b.eq _relocated
    ldr x12, =0x40080000
    cmp x9, x12
    b.ne _halt                    /* only the two known Image placements */
    ldr x11, =__file_size
    cmp x10, x9
    b.hi 2f
1:  ldrb w12, [x9], #1
    strb w12, [x10], #1
    subs x11, x11, #1
    b.ne 1b
    b 4f
2:  add x9, x9, x11
    add x10, x10, x11
3:  ldrb w12, [x9, #-1]!
    strb w12, [x10, #-1]!
    subs x11, x11, #1
    b.ne 3b
4:  ldr x9, =_image_start
    ldr x10, =__file_size
    add x10, x9, x10
    mrs x11, ctr_el0
    ubfx x11, x11, #16, #4
    mov x12, #4
    lsl x12, x12, x11
5:  dc cvac, x9
    add x9, x9, x12
    cmp x9, x10
    b.lo 5b
    dsb sy
    ic iallu
    dsb sy
    isb
    ldr x9, =_relocated
    br x9

_relocated:
    mrs x20, CurrentEL
    cmp x20, #4
    b.eq 6f
    cmp x20, #8
    b.ne _halt
    mrs x9, sctlr_el2
    tbnz x9, #0, _halt           /* do not guess at a live MMU mapping */
    ldr x9, =exception_vectors
    msr vbar_el2, x9
    b 7f
6:  mrs x9, sctlr_el1
    tbnz x9, #0, _halt
    ldr x9, =exception_vectors
    msr vbar_el1, x9
7:  isb
    ldr x9, =__stack_top
    mov sp, x9
    ldr x9, =__bss_start
    ldr x10, =__bss_end
8:  cmp x9, x10
    b.hs 9f
    str xzr, [x9], #8
    b 8b
9:  mov x0, x19
    lsr x1, x20, #2
    bl probe_main
_halt:
    wfe
    b _halt
    .ltorg

.balign 2048
exception_vectors:
.rept 16
    b _halt
    .space 124
.endr

