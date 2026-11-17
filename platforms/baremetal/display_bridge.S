.section .text.entry_control, "ax"
.global entry_control
entry_control:
    ldr x9, =bridge_context
    stp x0, x1, [x9]
    stp x2, x3, [x9, #16]
    str x30, [x9, #32]
    mov x10, sp
    str x10, [x9, #40]
    mrs x10, SPSel
    str x10, [x9, #56]
    mrs x10, DAIF
    str x10, [x9, #64]
    mrs x10, CurrentEL
    lsr x10, x10, #2
    str x10, [x9, #80]
    adr x10, entry_control
    str x10, [x9, #96]
    // Establish the already tested stackless marker before trying more features.
    ldr x0, =KBL_BANNER_ADDRESS
    ldr x1, =KBL_PROC_MARKER_ADDRESS
    bl entry_control_cpp
    ldr x9, =bridge_context
    ldr x10, [x9, #80]
    cmp x10, #1
    b.eq 1f
    cmp x10, #2
    b.ne bridge_resume
    mrs x11, sctlr_el2
    mrs x12, vbar_el2
    b 2f
1:  mrs x11, sctlr_el1
    mrs x12, vbar_el1
2:  str x11, [x9, #88]
    str x12, [x9, #72]
    tbnz x11, #0, bridge_mmu_skip
    msr daifset, #15
    msr SPSel, #1
    mov x11, sp
    str x11, [x9, #48]
    ldr x11, =__bridge_stack_top
    mov sp, x11
    mov x11, #1
    str x11, [x9, #168]
    ldr x11, =bridge_vectors
    cmp x10, #1
    b.eq 3f
    msr vbar_el2, x11
    b 4f
3:  msr vbar_el1, x11
4:  isb
    mov x0, x9
    bl bridge_attempt
    ldr x0, =bridge_context
    bl bridge_finalize
    b bridge_resume

bridge_mmu_skip:
    // Remain stackless when the incoming MMU is on; do not guess a mapping.
    ldr x0, =KBL_PROC_MARKER_ADDRESS
    ldr x1, [x9, #80]
    bl bridge_mmu_marker
    b bridge_resume

bridge_fault:
    msr daifset, #15
    ldr x9, =bridge_context
    ldr x10, [x9, #144]
    cbnz x10, bridge_fault_return
    mov x10, #1
    str x10, [x9, #144]
    ldr x10, [x9, #80]
    cmp x10, #1
    b.eq 5f
    mrs x13, spsr_el2
    mrs x10, esr_el2
    mrs x11, elr_el2
    mrs x12, far_el2
    b 6f
5:  mrs x13, spsr_el1
    mrs x10, esr_el1
    mrs x11, elr_el1
    mrs x12, far_el1
6:  str x10, [x9, #120]
    str x11, [x9, #128]
    str x12, [x9, #136]
    str x13, [x9, #176]
    ldr x10, =__bridge_stack_top
    mov sp, x10
    mov x0, x9
    bl bridge_cleanup
    ldr x0, =bridge_context
    bl bridge_finalize

bridge_fault_return:
    // Restore pre-exception PSTATE (including PAN/UAO where present) before
    // the common path restores the incoming SP selection, DAIF and vectors.
    ldr x9, =bridge_context
    ldr x10, [x9, #80]
    ldr x11, [x9, #176]
    ldr x12, =bridge_resume
    cmp x10, #1
    b.eq 11f
    msr spsr_el2, x11
    msr elr_el2, x12
    eret
11: msr spsr_el1, x11
    msr elr_el1, x12
    eret

bridge_resume:
    ldr x9, =bridge_context
    ldr x10, [x9, #168]
    cbz x10, 9f
    ldr x10, [x9, #80]
    ldr x11, [x9, #72]
    cmp x10, #1
    b.eq 7f
    msr vbar_el2, x11
    b 8f
7:  msr vbar_el1, x11
8:  isb
    ldr x10, [x9, #48]
    mov sp, x10
    ldr x10, [x9, #56]
    msr SPSel, x10
    ldr x10, [x9, #40]
    mov sp, x10
    ldr x10, [x9, #64]
    msr DAIF, x10
9:  ldr x10, =KBL_PROC_FORMAT_ADDRESS
    ldr x11, =KBL_PROC_FORMAT_CAPACITY
10: dc cvac, x10
    add x10, x10, #16
    subs x11, x11, #16
    b.hi 10b
    ldr x10, =KBL_BANNER_ADDRESS
    dc cvac, x10
    dsb sy
    isb
    ldp x0, x1, [x9]
    ldp x2, x3, [x9, #16]
    ldr x30, [x9, #32]
    ldr x9, =KBL_KERNEL_ENTRY
    br x9
    .ltorg

.balign 4
.global bridge_psci_system_reset
.type bridge_psci_system_reset, %function
bridge_psci_system_reset:
    // AAPCS64 call boundary; PSCI SYSTEM_RESET uses SMC32 with zero arguments.
    // Preserve the platform register and our return state if firmware rejects it.
    stp x29, x30, [sp, #-32]!
    str x18, [sp, #16]
    mov x29, sp
    mov w0, w0
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
    mov x6, xzr
    mov x7, xzr
    mov x8, xzr
    mov x9, xzr
    mov x10, xzr
    mov x11, xzr
    mov x12, xzr
    mov x13, xzr
    mov x14, xzr
    mov x15, xzr
    mov x16, xzr
    mov x17, xzr
    dsb sy
    isb
    smc #0
    // SMC32 results are signed 32-bit PSCI status codes.
    sxtw x0, w0
    ldr x18, [sp, #16]
    ldp x29, x30, [sp], #32
    ret
.size bridge_psci_system_reset, .-bridge_psci_system_reset

.balign 4
.global bridge_linux_enter
.type bridge_linux_enter, %function
bridge_linux_enter:
    // All bounds and the complete DT copy have been checked before this point.
    // The destination is strictly below the loader/source and cannot overwrite
    // this code. Committing the copy ends the return-to-Android path.
    msr daifset, #15
    mov x19, x0
    mov x20, x1
    mov x21, x2
    mov x22, x3
    ldr x9, =bridge_context
    ldr x10, [x9, #80]
    ldr x11, [x9, #72]
    cmp x10, #1
    b.eq 20f
    msr vbar_el2, x11
    b 21f
20: msr vbar_el1, x11
21: isb
    ldr x10, [x9, #48]
    mov sp, x10
    ldr x10, [x9, #56]
    msr SPSel, x10
    ldr x10, [x9, #40]
    mov sp, x10
    mov x9, x19
22: ldrb w10, [x20], #1
    strb w10, [x9], #1
    subs x21, x21, #1
    b.ne 22b
    // Clean the copied image to PoC and discard stale Android instructions.
    mov x10, x19
23: dc cvac, x10
    add x10, x10, #16
    cmp x10, x9
    b.lo 23b
    dsb sy
    ic iallu
    dsb sy
    isb
    mov x0, x22
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    br x19
.size bridge_linux_enter, .-bridge_linux_enter
    .ltorg

.balign 2048
bridge_vectors:
.rept 16
    b bridge_fault
    .space 124
.endr

.section .data.bridge_context, "aw", @progbits
.balign 16
.global bridge_context
bridge_context:
    .zero 280
